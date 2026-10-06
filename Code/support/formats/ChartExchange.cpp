#include "ChartExchange.hpp"

#include "JsonRead.hpp"
#include "LegacyChart.hpp"
#include "../../third_party/json.hpp"
#include "../../third_party/miniz/miniz.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using ojson = nlohmann::ordered_json;

namespace fml {
namespace {

constexpr std::uintmax_t kMaxJsonBytes = 32u * 1024u * 1024u;

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<std::string> readText(const fs::path& path, std::string& error) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = "could not read " + path.u8string();
        return std::nullopt;
    }
    if (size > kMaxJsonBytes) {
        error = "JSON is larger than 32 MiB: " + path.u8string();
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "could not open " + path.u8string();
        return std::nullopt;
    }
    std::string text(static_cast<size_t>(size), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (input.gcount() != static_cast<std::streamsize>(text.size())) {
            error = "incomplete read: " + path.u8string();
            return std::nullopt;
        }
    }
    return text;
}

std::string parameterText(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_null()) return {};
    return value.dump();
}

void appendPsychEvent(const json& raw, bool global, std::vector<ChartEvent>& out,
                      int& skipped) {
    if (!raw.is_array() || raw.size() < 2 || !raw[0].is_number() ||
        !raw[1].is_array()) {
        ++skipped;
        return;
    }
    for (const json& tuple : raw[1]) {
        if (!tuple.is_array() || tuple.empty() || !tuple[0].is_string()) {
            ++skipped;
            continue;
        }
        ChartEvent event;
        event.timeMs = raw[0].get<double>();
        event.name = tuple[0].get<std::string>();
        event.global = global;
        for (size_t i = 1; i < tuple.size(); ++i) {
            event.params.push_back(parameterText(tuple[i]));
            event.paramsJson.push_back(tuple[i].dump());
        }
        out.push_back(std::move(event));
    }
}

void appendPsychEvents(const json& value, bool global,
                       std::vector<ChartEvent>& out, int& skipped) {
    if (!value.is_array()) {
        ++skipped;
        return;
    }
    for (const json& raw : value) appendPsychEvent(raw, global, out, skipped);
}

bool isPsychEventsDocument(const json& root) {
    if (!root.is_object()) return false;
    const json* document = &root;
    if (root.contains("song") && root["song"].is_object()) document = &root["song"];
    return document->contains("events") && (*document)["events"].is_array() &&
           !document->contains("notes");
}

const json* psychDocumentBody(const json& root) {
    if (root.is_object() && root.contains("song") && root["song"].is_object())
        return &root["song"];
    return &root;
}

std::string suggestedDifficulty(const fs::path& path) {
    const std::string stem = lowerAscii(path.stem().u8string());
    const std::string folder = lowerAscii(path.parent_path().filename().u8string());
    // Psych's chart path is <song>/<song>-<difficulty>. Difficulty names are
    // free strings, including legacy/old/custom labels and hyphenated labels.
    if (!folder.empty() && stem.size() > folder.size() + 1 &&
        stem.compare(0,folder.size() + 1,folder + "-") == 0)
        return stem.substr(folder.size() + 1);
    const char* known[] = {"easy", "normal", "hard", "erect", "nightmare"};
    for (const char* diff : known) {
        const std::string suffix = "-" + std::string(diff);
        if (stem.size() >= suffix.size() &&
            stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
            return diff;
    }
    return "normal";
}

void removePsychEventNotes(json& root, std::vector<ChartEvent>& events, int& skipped) {
    json* song = &root;
    if (root.is_object() && root.contains("song") && root["song"].is_object())
        song = &root["song"];
    if (!song->is_object() || !song->contains("notes") || !(*song)["notes"].is_array())
        return;
    for (json& section : (*song)["notes"]) {
        if (!section.is_object() || !section.contains("sectionNotes") ||
            !section["sectionNotes"].is_array()) continue;
        json kept = json::array();
        for (const json& note : section["sectionNotes"]) {
            if (!note.is_array() || note.size() < 2 || !note[1].is_number()) {
                kept.push_back(note);
                continue;
            }
            const int direction = note[1].get<int>();
            if (direction >= 0) {
                kept.push_back(note);
                continue;
            }
            // Psych 0.x guardaba eventos como [time, -1, name, value1, value2].
            if (note.size() < 3 || !note[0].is_number() || !note[2].is_string()) {
                ++skipped;
                continue;
            }
            ChartEvent event;
            event.timeMs = note[0].get<double>();
            event.name = note[2].get<std::string>();
            for (size_t i = 3; i < note.size(); ++i) {
                event.params.push_back(parameterText(note[i]));
                event.paramsJson.push_back(note[i].dump());
            }
            events.push_back(std::move(event));
        }
        section["sectionNotes"] = std::move(kept);
    }
}

void appendUnique(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

size_t zipRead(void* opaque, mz_uint64 offset, void* buffer, size_t bytes) {
    auto* stream = static_cast<std::ifstream*>(opaque);
    stream->clear();
    stream->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!*stream) return 0;
    stream->read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
    return static_cast<size_t>(stream->gcount());
}

class FnfcReader {
public:
    bool open(const fs::path& path) {
        m_stream.open(path, std::ios::binary);
        if (!m_stream) return false;
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);
        if (ec) return false;
        m_zip.m_pRead = zipRead;
        m_zip.m_pIO_opaque = &m_stream;
        m_open = mz_zip_reader_init(&m_zip, static_cast<mz_uint64>(size), 0) != 0;
        return m_open;
    }

    std::optional<std::string> text(const std::string& name) {
        const int index = mz_zip_reader_locate_file(&m_zip, name.c_str(), nullptr, 0);
        if (index < 0) return std::nullopt;
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&m_zip, static_cast<mz_uint>(index), &stat) ||
            stat.m_uncomp_size > kMaxJsonBytes) return std::nullopt;
        std::string out(static_cast<size_t>(stat.m_uncomp_size), '\0');
        if (!out.empty() && !mz_zip_reader_extract_to_mem(
                &m_zip, static_cast<mz_uint>(index), out.data(), out.size(), 0))
            return std::nullopt;
        return out;
    }

    ~FnfcReader() { if (m_open) mz_zip_reader_end(&m_zip); }

private:
    std::ifstream m_stream;
    mz_zip_archive m_zip{};
    bool m_open = false;
};

std::string replaceSuffix(std::string value, const std::string& from,
                          const std::string& to) {
    const std::string lower = lowerAscii(value);
    const std::string wanted = lowerAscii(from);
    const size_t at = lower.rfind(wanted);
    if (at == std::string::npos || at + wanted.size() != value.size()) return {};
    value.replace(at, from.size(), to);
    return value;
}

std::string baseStageToCodename(std::string stage) {
    const std::string id = lowerAscii(stage);
    // El juego base usa ids distintos para las revisiones normales/Erect. Las
    // instalaciones de Codename distribuyen los stages clasicos con estos ids.
    // No se inventa un alias para stages desconocidos: la UI los valida contra
    // el proyecto de destino antes de aplicar el borrador.
    if (id == "mainstage" || id == "mainstageerect") return "stage";
    if (id == "spookymansion" || id == "spookymansionerect") return "spooky";
    if (id == "phillytrain" || id == "phillytrainerect" ||
        id == "phillystreets" || id == "phillystreetserect" ||
        id == "phillyblazin") return "philly";
    if (id == "limoride" || id == "limorideerect") return "limo";
    if (id == "mallxmas" || id == "mallxmaserect") return "mall";
    if (id == "mallevil" || id == "mallevilerect") return "mall-evil";
    if (id == "school" || id == "schoolerect") return "school";
    if (id == "schoolevil" || id == "schoolevilerect") return "school-evil";
    if (id == "tankmanbattlefield" || id == "tankmanbattlefielderect") return "tank";
    return stage;
}

ChartStrumLine makeLine(int type, const std::string& position,
                        const std::string& character) {
    ChartStrumLine line;
    line.type = type;
    line.position = position;
    if (!character.empty()) line.characters.push_back(character);
    if (type == 2) line.visible = false;
    return line;
}

void appendParam(ChartEvent& event, const json& value) {
    event.params.push_back(parameterText(value));
    event.paramsJson.push_back(value.dump());
}

void appendEvent(std::vector<ChartEvent>& out, double timeMs,
                 const std::string& name, std::initializer_list<json> params) {
    ChartEvent event;
    event.timeMs = timeMs;
    event.name = name;
    for (const json& value : params) appendParam(event, value);
    out.push_back(std::move(event));
}

int lineOfType(const std::vector<ChartStrumLine>& lines, int type, int fallback) {
    for (size_t i = 0; i < lines.size(); ++i)
        if (lines[i].type == type) return static_cast<int>(i);
    return fallback;
}

struct VSliceEase {
    std::string name = "linear";
    json direction = nullptr;
};

VSliceEase parseVSliceEase(const std::string& value) {
    VSliceEase out;
    if (value.empty() || value == "INSTANT") return out;
    out.name = value;
    const char* suffixes[] = {"InOut", "In", "Out"};
    for (const char* suffix : suffixes) {
        const size_t count = std::char_traits<char>::length(suffix);
        if (out.name.size() < count ||
            out.name.compare(out.name.size() - count, count, suffix) != 0) continue;
        out.direction = suffix;
        out.name.resize(out.name.size() - count);
        if (out.name.empty()) out.name = "linear";
        break;
    }
    return out;
}

void preserveBaseEvent(const json& raw, std::vector<ChartEvent>& out,
                       bool& opaqueValue) {
    using namespace jsonread;
    ChartEvent event;
    event.timeMs = number(raw, "t", 0.0);
    event.name = stringValue(raw, "e", "Unknown");
    if (raw.contains("v") && !raw["v"].is_null()) {
        appendParam(event, raw["v"]);
        opaqueValue = opaqueValue || raw["v"].is_object() || raw["v"].is_array();
    }
    out.push_back(std::move(event));
}

// Conversion portada de CodenameEngine/source/funkin/backend/chart/
// VSliceParser.hx. Los nombres y el orden de parametros son los que consume
// Codename; los eventos que ese parser no conoce se conservan tipados.
void parseBaseEvent(const json& raw, const std::vector<ChartStrumLine>& lines,
                    std::vector<ChartEvent>& out, bool& opaqueValue) {
    using namespace jsonread;
    const double timeMs = number(raw, "t", 0.0);
    const std::string name = stringValue(raw, "e", "Unknown");
    if (!raw.contains("v") || raw["v"].is_null()) {
        preserveBaseEvent(raw, out, opaqueValue);
        return;
    }
    const json& value = raw["v"];

    if (name == "FocusCamera" && (value.is_object() || value.is_number())) {
        const int character = value.is_number()
            ? static_cast<int>(value.get<double>()) : integer(value, "char", 2);
        bool positionOnly = false;
        int target = lineOfType(lines, 2, 2);
        if (character == 0) target = lineOfType(lines, 1, 1);
        else if (character == 1) target = lineOfType(lines, 0, 0);
        else if (character == -1) { target = -1; positionOnly = true; }

        std::vector<json> movement = {target};
        const std::string ease = value.is_object()
            ? stringValue(value, "ease") : std::string();
        if (!ease.empty() && ease != "CLASSIC") {
            if (ease == "INSTANT") movement.push_back(false);
            else {
                const VSliceEase parsed = parseVSliceEase(ease);
                movement.push_back(true);
                movement.push_back(number(value, "duration", 4.0));
                movement.push_back(parsed.name);
                movement.push_back(parsed.direction);
            }
        }

        const json x = value.is_object() && value.contains("x") ? value["x"] : json(nullptr);
        const json y = value.is_object() && value.contains("y") ? value["y"] : json(nullptr);
        const bool hasOffset = value.is_object() &&
            (std::abs(number(value, "x", 0.0)) > 0.000001 ||
             std::abs(number(value, "y", 0.0)) > 0.000001);
        if (positionOnly || hasOffset) {
            const bool useNull = movement.size() <= 2;
            appendEvent(out, timeMs, "Camera Position", {
                x, y,
                useNull ? json(nullptr) : movement[1],
                useNull ? json(nullptr) : movement[2],
                useNull ? json(nullptr) : movement[3],
                useNull ? json(nullptr) : movement[4],
                true
            });
        }
        if (!positionOnly) {
            ChartEvent event;
            event.timeMs = timeMs;
            event.name = "Camera Movement";
            for (const json& param : movement) appendParam(event, param);
            out.push_back(std::move(event));
        }
        return;
    }

    if (name == "PlayAnimation" && value.is_object()) {
        const std::string target = lowerAscii(stringValue(value, "target"));
        const int line = target == "boyfriend" || target == "bf" || target == "player"
            ? lineOfType(lines, 1, 1)
            : (target == "dad" || target == "opponent"
                ? lineOfType(lines, 0, 0) : lineOfType(lines, 2, 2));
        appendEvent(out, timeMs, "Play Animation", {
            line, stringValue(value, "anim"), boolean(value, "force", false)
        });
        return;
    }

    if (name == "ScrollSpeed" && value.is_object()) {
        const std::string ease = stringValue(value, "ease");
        const VSliceEase parsed = parseVSliceEase(ease);
        appendEvent(out, timeMs, "Scroll Speed Change", {
            ease != "INSTANT", number(value, "scroll", 1.0),
            number(value, "duration", 4.0), parsed.name, parsed.direction,
            !boolean(value, "absolute", false)
        });
        return;
    }

    if (name == "SetCameraBop" && value.is_object()) {
        appendEvent(out, timeMs, "Camera Modulo Change", {
            integer(value, "rate", 4), number(value, "intensity", 1.0)
        });
        return;
    }

    if (name == "ZoomCamera" && value.is_object()) {
        const std::string ease = stringValue(value, "ease");
        const VSliceEase parsed = parseVSliceEase(ease);
        const json mode = value.contains("mode") ? value["mode"] : json(nullptr);
        appendEvent(out, timeMs, "Camera Zoom", {
            ease != "INSTANT", number(value, "zoom", 1.0), "camGame",
            number(value, "duration", 4.0), parsed.name, parsed.direction,
            mode, false
        });
        return;
    }

    preserveBaseEvent(raw, out, opaqueValue);
}

Result<ChartImportBundle> parseFunkinBasePair(const std::string& chartText,
                                               const std::string& metadataText,
                                               const std::string& source,
                                               DiagnosticSink& sink) {
    json chartRoot, metadata;
    try {
        chartRoot = json::parse(chartText, nullptr, true, true);
        metadata = json::parse(metadataText, nullptr, true, true);
    } catch (const std::exception& e) {
        sink.error("FML-XCHG-020", std::string("Funkin chart JSON is invalid: ") + e.what(), source);
        return Result<ChartImportBundle>::fail(e.what());
    }
    if (!chartRoot.is_object() || !chartRoot.contains("notes") ||
        !chartRoot["notes"].is_object() || !metadata.is_object() ||
        !metadata.contains("playData") || !metadata["playData"].is_object()) {
        sink.error("FML-XCHG-021", "chart/metadata pair is not a Funkin base-game document", source);
        return Result<ChartImportBundle>::fail("invalid Funkin chart/metadata pair");
    }
    using namespace jsonread;
    const std::string timeFormat = lowerAscii(stringValue(metadata, "timeFormat", "ms"));
    if (timeFormat != "ms") {
        const std::string message = "timeFormat '" + timeFormat +
            "' is not milliseconds; import was blocked to avoid shifting every note";
        sink.error("FML-XCHG-022", message, source);
        return Result<ChartImportBundle>::fail(message);
    }

    ChartImportBundle bundle;
    bundle.sourcePath = source;
    bundle.formatLabel = "Friday Night Funkin' base (V-Slice)";
    bundle.songName = stringValue(metadata, "songName", "Imported song");
    bundle.artist = stringValue(metadata, "artist");
    bundle.charter = stringValue(metadata, "charter");
    const json& play = metadata["playData"];
    if (play.contains("songVariations") && play["songVariations"].is_array() &&
        !play["songVariations"].empty())
        bundle.warnings.push_back("this package declares song variations; this import uses the default variation only");
    const json characters = play.contains("characters") && play["characters"].is_object()
        ? play["characters"] : json::object();
    const std::string player = stringValue(characters, "player", "bf");
    const std::string opponent = stringValue(characters, "opponent", "dad");
    const std::string girlfriend = stringValue(characters, "girlfriend", "gf");
    const std::string stage = baseStageToCodename(stringValue(play, "stage", "mainStage"));

    std::vector<BpmChange> bpmChanges;
    float firstBpm = 100.0f;
    int beatsPerMeasure = 4;
    int stepsPerBeat = 4;
    if (metadata.contains("timeChanges") && metadata["timeChanges"].is_array()) {
        float inherited = 100.0f;
        for (const json& change : metadata["timeChanges"]) {
            if (!change.is_object()) continue;
            inherited = static_cast<float>(number(change, "bpm", inherited));
            if (inherited <= 0.0f) continue;
            bpmChanges.push_back({number(change, "t", 0.0), inherited});
            if (bpmChanges.size() == 1) {
                firstBpm = inherited;
                beatsPerMeasure = std::max(1, integer(change, "n", 4));
                // V-Slice define el divisor de beat en `d`. `bt` son tuplets y
                // Codename no lo usa como stepsPerBeat (VSliceParser.parseMeta).
                stepsPerBeat = std::max(1, integer(change, "d", 4));
            }
        }
    }
    if (bpmChanges.empty()) bpmChanges.push_back({0.0, firstBpm});
    std::stable_sort(bpmChanges.begin(), bpmChanges.end(), [](const BpmChange& a, const BpmChange& b) {
        return a.timeMs < b.timeMs;
    });
    if (bpmChanges.front().timeMs > 0.0)
        bpmChanges.insert(bpmChanges.begin(), {0.0, bpmChanges.front().bpm});
    firstBpm = bpmChanges.front().bpm;

    std::vector<std::string> difficulties;
    if (play.contains("difficulties") && play["difficulties"].is_array())
        for (const json& value : play["difficulties"])
            if (value.is_string() && chartRoot["notes"].contains(value.get<std::string>()))
                appendUnique(difficulties, value.get<std::string>());
    for (auto it = chartRoot["notes"].begin(); it != chartRoot["notes"].end(); ++it)
        appendUnique(difficulties, it.key());
    if (difficulties.empty()) {
        sink.error("FML-XCHG-023", "Funkin chart contains no difficulties", source);
        return Result<ChartImportBundle>::fail("no difficulties");
    }

    bool opaqueEvents = false;
    bool noteParams = false;
    for (const std::string& difficulty : difficulties) {
        UniversalChart out;
        out.format = ChartFormat::CodenameNative;
        out.songId = bundle.songName;
        out.difficulty = difficulty;
        out.sourcePath = source;
        out.player1 = player;
        out.player2 = opponent;
        const bool opponentIsGirlfriend = lowerAscii(opponent).rfind("gf", 0) == 0;
        const bool hasGirlfriend = !opponentIsGirlfriend &&
            !girlfriend.empty() && lowerAscii(girlfriend) != "none";
        out.gfVersion = hasGirlfriend ? girlfriend : "none";
        out.stage = stage;
        out.bpm = firstBpm;
        out.beatsPerMeasure = beatsPerMeasure;
        out.stepsPerBeat = stepsPerBeat;
        out.needsVoices = true;
        out.bpmChanges = bpmChanges;
        // Orden autoritativo de VSliceParser: opponent=0, player=1, GF=2.
        // No se copian vocalsSuffix aqui: esto es un import de chart sobre una
        // cancion Codename existente, no un importador de audio V-Slice.
        out.strumLines.push_back(makeLine(
            0, opponentIsGirlfriend ? "girlfriend" : "dad", opponent));
        out.strumLines.push_back(makeLine(1, "boyfriend", player));
        if (hasGirlfriend)
            out.strumLines.push_back(makeLine(2, "girlfriend", girlfriend));

        if (chartRoot.contains("scrollSpeed") && chartRoot["scrollSpeed"].is_object()) {
            const json& speeds = chartRoot["scrollSpeed"];
            out.speed = static_cast<float>(number(speeds, difficulty.c_str(),
                          number(speeds, "default", 1.0)));
        }
        if (out.speed <= 0.0f) out.speed = 1.0f;

        const json& rawNotes = chartRoot["notes"][difficulty];
        int skipped = 0;
        if (rawNotes.is_array()) for (const json& raw : rawNotes) {
            if (!raw.is_object()) { ++skipped; continue; }
            const int data = integer(raw, "d", -1);
            if (data < 0) { ++skipped; continue; }
            // Codename reduce d a los ocho receptores base antes de escoger la
            // strumline; 0..3 son jugador y 4..7 rival.
            const int noteData = data % 8;
            const bool playerNote = noteData < 4;
            const int lineIndex = playerNote
                ? lineOfType(out.strumLines, 1, 1)
                : lineOfType(out.strumLines, 0, 0);
            ChartNote note;
            note.timeMs = number(raw, "t", 0.0);
            note.lane = noteData % 4;
            note.direction = note.lane;
            note.strumLine = lineIndex;
            note.isPlayer = playerNote;
            note.sustainMs = std::max(0.0, number(raw, "l", 0.0));
            note.type = stringValue(raw, "k");
            if (note.type == "alt") note.type = "Alt Anim Note";
            if (raw.contains("p") && raw["p"].is_array() && !raw["p"].empty())
                noteParams = true;
            appendUnique(out.noteTypes, note.type);
            out.notes.push_back(std::move(note));
        }
        if (skipped > 0)
            bundle.warnings.push_back(std::to_string(skipped) +
                " invalid notes were skipped in " + difficulty);

        // Codename serializa los cambios posteriores como eventos; conservar
        // solo bpmChanges en memoria hacia que se perdieran al guardar/reabrir.
        float currentBpm = firstBpm;
        for (size_t i = 1; i < bpmChanges.size(); ++i) {
            if (std::abs(bpmChanges[i].bpm - currentBpm) <= 0.0001f) continue;
            currentBpm = bpmChanges[i].bpm;
            appendEvent(out.events, bpmChanges[i].timeMs, "BPM Change", {currentBpm});
        }

        if (chartRoot.contains("events") && chartRoot["events"].is_array())
            for (const json& raw : chartRoot["events"]) {
                if (!raw.is_object()) continue;
                parseBaseEvent(raw, out.strumLines, out.events, opaqueEvents);
            }
        std::stable_sort(out.notes.begin(), out.notes.end(), [](const ChartNote& a, const ChartNote& b) {
            return a.timeMs < b.timeMs;
        });
        std::stable_sort(out.events.begin(), out.events.end(), [](const ChartEvent& a, const ChartEvent& b) {
            return a.timeMs < b.timeMs;
        });
        bundle.candidates.push_back({difficulty, std::move(out)});
    }
    if (opaqueEvents)
        bundle.warnings.push_back("custom V-Slice event payloads are preserved as typed JSON parameters; verify their Codename handler");
    if (noteParams)
        bundle.warnings.push_back("V-Slice note parameter arrays have no Codename chart equivalent and were not imported");
    return Result<ChartImportBundle>::ok(std::move(bundle));
}

bool parseBoolText(const ChartEvent& event, bool fallback) {
    if (event.params.empty()) return fallback;
    const std::string value = lowerAscii(event.params.front());
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return fallback;
}

int parseLineIndex(const ChartEvent& event, int fallback) {
    if (event.params.empty()) return fallback;
    try { return std::stoi(event.params.front()); } catch (...) {}
    const std::string value = lowerAscii(event.params.front());
    if (value == "boyfriend" || value == "bf" || value == "player") return 1;
    if (value == "dad" || value == "opponent" || value == "enemy") return 0;
    return fallback;
}

std::string characterForType(const UniversalChart& chart, int type,
                             const std::string& fallback) {
    for (const ChartStrumLine& line : chart.strumLines)
        if (line.type == type && !line.characters.empty()) return line.characters.front();
    return fallback;
}

}  // namespace

const std::vector<PsychEventPort>& psychEventPorts() {
    // Los de Psych salen del `switch` de `PlayState.hx:2061+`; los de Codename,
    // de `EventsData.hx:13-122`. Lo que no esta en las dos listas no se
    // convierte: se conserva y se dice.
    static const std::vector<PsychEventPort> kPorts = {
        {"Add Camera Zoom", "Add Camera Zoom",
         "el primer valor va a camGame y el segundo a camHUD"},
        {"Camera Follow Pos", "Camera Position",
         "con los dos valores vacios Psych vuelve al seguimiento automatico, "
         "que en Codename es no poner el evento"},
        {"Play Animation", "Play Animation",
         "el personaje se convierte en el indice de su strumline"},
        {"Change Scroll Speed", "Scroll Speed Change",
         "en Psych el valor MULTIPLICA la velocidad base (`PlayState.hx:2260`) "
         "y el tween va en segundos; aqui va en pasos"},
        {"Alt Idle Animation", "Alt Animation Toggle",
         "Codename lo enciende y lo apaga por strumline: el sufijo con nombre "
         "propio de Psych no viaja"},
        {"Hey!", "",
         "no hay evento equivalente: se hace con `Play Animation` (hey/cheer)"},
        {"Set GF Speed", "",
         "no hay evento equivalente: es un `HScript Call` sobre gf.danceEveryNumBeats"},
        {"Screen Shake", "", "no hay evento equivalente: `HScript Call`"},
        {"Change Character", "",
         "no hay evento equivalente: en Codename se cambia por script"},
        {"Set Property", "",
         "no hay evento equivalente, y es el mas comun: `HScript Call` con la "
         "propiedad que tocaba"},
        {"Play Sound", "", "no hay evento equivalente: `HScript Call`"},
    };
    return kPorts;
}

namespace {

std::string jsonNumber(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", value);
    return buffer;
}

double numberFrom(const std::string& text) {
    try { return std::stod(text); } catch (...) { return 0.0; }
}

// El indice de strumline al que se refiere un `bf`/`dad`/`gf` de Psych.
int strumLineFor(const UniversalChart& chart, const std::string& who) {
    std::string lowered;
    for (char ch : who)
        lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    int wantType = -1;
    if (lowered == "bf" || lowered == "boyfriend") wantType = 1;
    else if (lowered == "dad" || lowered == "opponent") wantType = 0;
    else if (lowered == "gf" || lowered == "girlfriend") wantType = 2;
    if (wantType < 0) {
        // Psych acepta tambien un indice suelto.
        try { return std::stoi(lowered); } catch (...) { return 0; }
    }
    for (std::size_t at = 0; at < chart.strumLines.size(); ++at)
        if (chart.strumLines[at].type == wantType) return static_cast<int>(at);
    return wantType == 1 ? 1 : 0;
}

}  // namespace

PsychEventReport translatePsychEvents(UniversalChart& chart, float bpm) {
    PsychEventReport report;
    const std::vector<PsychEventPort>& ports = psychEventPorts();
    auto count = [](std::vector<std::pair<std::string, int>>& list,
                    const std::string& name) {
        for (auto& entry : list)
            if (entry.first == name) { ++entry.second; return; }
        list.emplace_back(name, 1);
    };
    std::vector<ChartEvent> extra;
    for (ChartEvent& event : chart.events) {
        // A previously converted chart must not be converted a second time.
        const auto saved = json::parse(event.sourceJson, nullptr, false);
        if (saved.is_object() && saved.contains("psych")) continue;
        const PsychEventPort* port = nullptr;
        for (const PsychEventPort& candidate : ports)
            if (candidate.psych == event.name) port = &candidate;
        if (!port) continue;
        ++report.total;
        if (port->codename.empty()) {
            count(report.unresolved, event.name);
            continue;
        }
        const std::string first = event.params.size() > 0 ? event.params[0] : "";
        const std::string second = event.params.size() > 1 ? event.params[1] : "";
        auto origin = saved.is_object() ? saved : json::object();
        origin["psych"] = {{"name",event.name},{"value1",first},{"value2",second},{"native",true}};
        const auto originalEvent = origin.dump();

        if (event.name == "Add Camera Zoom") {
            const double game = first.empty() ? 0.015 : numberFrom(first);
            const double hud = second.empty() ? 0.03 : numberFrom(second);
            event.name = port->codename;
            event.params = {jsonNumber(game), "camGame"};
            event.paramsJson = {jsonNumber(game), "\"camGame\""};
            event.sourceIndex = -1;
            event.sourceJson = originalEvent;
            if (hud != 0.0) {
                ChartEvent hudEvent = event;
                hudEvent.params = {jsonNumber(hud), "camHUD"};
                hudEvent.paramsJson = {jsonNumber(hud), "\"camHUD\""};
                auto auxiliary = origin;
                auxiliary["psych"]["auxiliary"] = true;
                hudEvent.sourceJson = auxiliary.dump();
                extra.push_back(std::move(hudEvent));
            }
        } else if (event.name == "Camera Follow Pos") {
            if (first.empty() && second.empty()) {
                // Volver al automatico no se puede expresar: se deja como venia.
                count(report.unresolved, event.name);
                continue;
            }
            event.name = port->codename;
            event.params = {jsonNumber(numberFrom(first)), jsonNumber(numberFrom(second)),
                            "false", "4", "CLASSIC", "In", "false"};
            event.paramsJson = {jsonNumber(numberFrom(first)),
                                jsonNumber(numberFrom(second)),
                                "false", "4", "\"CLASSIC\"", "\"In\"", "false"};
            event.sourceIndex = -1;
            event.sourceJson = originalEvent;
        } else if (event.name == "Play Animation") {
            const int line = strumLineFor(chart, second);
            event.name = port->codename;
            event.params = {std::to_string(line), first, "true", "NONE"};
            event.paramsJson = {std::to_string(line), json(first).dump(), "true",
                                "\"NONE\""};
            event.sourceIndex = -1;
            event.sourceJson = originalEvent;
        } else if (event.name == "Change Scroll Speed") {
            const double speed = chart.speed * (first.empty() ? 1.0 : numberFrom(first));
            const double seconds = numberFrom(second);
            double currentBpm = bpm > 0.0f ? bpm : 120.0;
            for (const auto& change : chart.bpmChanges)
                if (change.timeMs <= event.timeMs && change.bpm > 0) currentBpm = change.bpm;
            const double stepMs = 60000.0 / currentBpm / std::max(1, chart.stepsPerBeat);
            const double steps = seconds > 0.0 ? seconds * 1000.0 / stepMs : 4.0;
            event.name = port->codename;
            event.params = {seconds > 0.0 ? "true" : "false", jsonNumber(speed),
                            jsonNumber(steps), "linear", "In", "false"};
            event.paramsJson = {seconds > 0.0 ? "true" : "false", jsonNumber(speed),
                                jsonNumber(steps), "\"linear\"", "\"In\"", "false"};
            event.sourceIndex = -1;
            event.sourceJson = originalEvent;
        } else if (event.name == "Alt Idle Animation") {
            const int line = strumLineFor(chart, first);
            const bool on = !second.empty();
            event.name = port->codename;
            event.params = {on ? "true" : "false", on ? "true" : "false",
                            std::to_string(line)};
            event.paramsJson = event.params;
            event.sourceIndex = -1;
            event.sourceJson = originalEvent;
        } else {
            continue;
        }
        count(report.translated, port->psych);
    }
    if (!extra.empty()) {
        chart.events.insert(chart.events.end(), extra.begin(), extra.end());
        std::stable_sort(chart.events.begin(), chart.events.end(),
                         [](const ChartEvent& a, const ChartEvent& b) {
                             return a.timeMs < b.timeMs;
                         });
    }
    return report;
}

Result<ChartImportBundle> importPsychEngineChart(const std::vector<fs::path>& paths,
                                                  DiagnosticSink& sink) {
    if (paths.empty()) return Result<ChartImportBundle>::fail("no file selected");
    fs::path chartPath;
    json chartRoot;
    std::vector<std::pair<fs::path, json>> eventDocs;
    for (const fs::path& path : paths) {
        std::string error;
        const auto text = readText(path, error);
        if (!text) {
            sink.error("FML-XCHG-001", error, path.u8string());
            return Result<ChartImportBundle>::fail(error);
        }
        json root;
        try { root = json::parse(*text, nullptr, true, true); }
        catch (const std::exception& e) {
            sink.error("FML-XCHG-002", std::string("Psych JSON is invalid: ") + e.what(), path.u8string());
            return Result<ChartImportBundle>::fail(e.what());
        }
        if (isPsychEventsDocument(root)) eventDocs.push_back({path, std::move(root)});
        else if (chartPath.empty()) { chartPath = path; chartRoot = std::move(root); }
        else {
            sink.error("FML-XCHG-003", "select one Psych chart and optionally events.json", path.u8string());
            return Result<ChartImportBundle>::fail("more than one Psych chart selected");
        }
    }
    if (chartPath.empty()) {
        sink.error("FML-XCHG-004", "no Psych chart was selected", paths.front().u8string());
        return Result<ChartImportBundle>::fail("no Psych chart selected");
    }

    // If the user chose only the difficulty file, include its standard sibling
    // events.json automatically. Explicit selections still win and avoid dupes.
    if (eventDocs.empty()) {
        const fs::path sibling = chartPath.parent_path() / "events.json";
        std::error_code ec;
        if (fs::is_regular_file(sibling, ec)) {
            std::string error;
            if (const auto text = readText(sibling, error)) {
                const json root = json::parse(*text, nullptr, false, true);
                if (!root.is_discarded() && isPsychEventsDocument(root))
                    eventDocs.push_back({sibling, root});
            }
        }
    }

    int skippedEvents = 0;
    std::vector<ChartEvent> events;
    removePsychEventNotes(chartRoot, events, skippedEvents);
    json* song = &chartRoot;
    if (chartRoot.contains("song") && chartRoot["song"].is_object()) song = &chartRoot["song"];
    if (song->contains("events")) appendPsychEvents((*song)["events"], false, events, skippedEvents);
    for (const auto& doc : eventDocs) {
        const json* body = psychDocumentBody(doc.second);
        appendPsychEvents((*body)["events"], true, events, skippedEvents);
    }

    DiagnosticSink local;
    auto parsed = parseLegacyChart(chartRoot.dump(), chartPath.u8string(), local);
    sink.append(local);
    if (!parsed) return Result<ChartImportBundle>::fail(parsed.error());
    UniversalChart chart = std::move(parsed.value());
    chart.events.insert(chart.events.end(), events.begin(), events.end());
    std::stable_sort(chart.events.begin(), chart.events.end(), [](const ChartEvent& a, const ChartEvent& b) {
        return a.timeMs < b.timeMs;
    });
    chart.difficulty = suggestedDifficulty(chartPath);

    ChartImportBundle bundle;
    bundle.sourcePath = chartPath.u8string();
    bundle.formatLabel = "Psych Engine (psych_v1 / legacy)";
    bundle.songName = chart.songId.empty() ? chartPath.stem().u8string() : chart.songId;
    if (song->is_object()) {
        bundle.artist = jsonread::stringValue(*song, "artist",
                         jsonread::stringValue(*song, "composer"));
        bundle.charter = jsonread::stringValue(*song, "charter");
    }
    bundle.candidates.push_back({chart.difficulty, std::move(chart)});
    // Los eventos que Codename sabe ejecutar se traducen aqui; los demas se
    // conservan tal cual y se cuentan, para poder decir cuales son.
    for (ChartImportCandidate& candidate : bundle.candidates) {
        const PsychEventReport part =
            translatePsychEvents(candidate.chart, candidate.chart.bpm);
        if (bundle.events.total == 0) bundle.events = part;
        for (const ChartNote& note : candidate.chart.notes) {
            if (note.type.empty()) continue;
            bool known = false;
            for (const std::string& seen : bundle.noteTypes)
                if (seen == note.type) known = true;
            if (!known) bundle.noteTypes.push_back(note.type);
        }
    }
    if (skippedEvents > 0)
        bundle.warnings.push_back(std::to_string(skippedEvents) + " malformed Psych events were skipped");
    if (!eventDocs.empty())
        bundle.warnings.push_back("events.json was imported as global events");
    return Result<ChartImportBundle>::ok(std::move(bundle));
}

Result<ChartImportBundle> importFunkinBaseChart(const std::vector<fs::path>& paths,
                                                 DiagnosticSink& sink) {
    if (paths.empty()) return Result<ChartImportBundle>::fail("no file selected");
    for (const fs::path& path : paths) {
        if (lowerAscii(path.extension().u8string()) != ".fnfc") continue;
        FnfcReader archive;
        if (!archive.open(path)) {
            sink.error("FML-XCHG-010", "could not open FNFC archive", path.u8string());
            return Result<ChartImportBundle>::fail("invalid FNFC archive");
        }
        const auto manifestText = archive.text("manifest.json");
        if (!manifestText) return Result<ChartImportBundle>::fail("FNFC has no manifest.json");
        json manifest = json::parse(*manifestText, nullptr, false, true);
        if (manifest.is_discarded() || !manifest.is_object())
            return Result<ChartImportBundle>::fail("FNFC manifest is invalid");
        const std::string songId = jsonread::stringValue(manifest, "songId");
        if (songId.empty()) return Result<ChartImportBundle>::fail("FNFC manifest has no songId");
        const auto chartText = archive.text(songId + "-chart.json");
        const auto metadataText = archive.text(songId + "-metadata.json");
        if (!chartText || !metadataText) {
            sink.error("FML-XCHG-011", "FNFC default chart or metadata is missing", path.u8string());
            return Result<ChartImportBundle>::fail("incomplete FNFC archive");
        }
        auto result = parseFunkinBasePair(*chartText, *metadataText, path.u8string(), sink);
        if (result && paths.size() > 1)
            result.value().warnings.push_back("the FNFC package was used; additional selected files were ignored");
        return result;
    }

    std::optional<std::string> chartText, metadataText;
    fs::path chartPath, metadataPath;
    for (const fs::path& path : paths) {
        std::string error;
        const auto text = readText(path, error);
        if (!text) {
            sink.error("FML-XCHG-012", error, path.u8string());
            return Result<ChartImportBundle>::fail(error);
        }
        const json root = json::parse(*text, nullptr, false, true);
        if (root.is_discarded() || !root.is_object()) continue;
        if (root.contains("notes") && root["notes"].is_object()) {
            if (chartText) return Result<ChartImportBundle>::fail("more than one Funkin chart selected");
            chartText = *text; chartPath = path;
        } else if (root.contains("playData") && root.contains("timeChanges")) {
            if (metadataText) return Result<ChartImportBundle>::fail("more than one Funkin metadata file selected");
            metadataText = *text; metadataPath = path;
        }
    }
    auto loadPair = [&](const fs::path& known, bool wantMetadata) {
        std::string candidate = replaceSuffix(known.u8string(),
            wantMetadata ? "-chart.json" : "-metadata.json",
            wantMetadata ? "-metadata.json" : "-chart.json");
        if (candidate.empty()) return std::optional<std::string>{};
        std::string error;
        return readText(fs::u8path(candidate), error);
    };
    if (!metadataText && chartText) metadataText = loadPair(chartPath, true);
    if (!chartText && metadataText) chartText = loadPair(metadataPath, false);
    if (!chartText || !metadataText) {
        sink.error("FML-XCHG-013", "select both *-chart.json and *-metadata.json, or select an .fnfc package",
                   paths.front().u8string());
        return Result<ChartImportBundle>::fail("chart/metadata pair is incomplete");
    }
    const fs::path source = chartPath.empty() ? metadataPath : chartPath;
    return parseFunkinBasePair(*chartText, *metadataText, source.u8string(), sink);
}

Result<ChartImportBundle> importFunkinBaseChartText(
    const std::string& chartText, const std::string& metadataText,
    const std::string& sourcePath, DiagnosticSink& sink) {
    return parseFunkinBasePair(chartText, metadataText, sourcePath, sink);
}

Result<PsychExportPackage> exportPsychEngineChart(const UniversalChart& chart,
                                                   const std::string& displayName,
                                                   DiagnosticSink& sink) {
    if (chart.strumLines.empty())
        return Result<PsychExportPackage>::fail("chart has no strumlines");
    int playerLine = -1, opponentLine = -1;
    for (size_t i = 0; i < chart.strumLines.size(); ++i) {
        if (playerLine < 0 && chart.strumLines[i].type == 1) playerLine = static_cast<int>(i);
        if (opponentLine < 0 && chart.strumLines[i].type == 0) opponentLine = static_cast<int>(i);
    }
    if (playerLine < 0 || opponentLine < 0) {
        sink.error("FML-XCHG-030", "Psych export needs one player and one opponent strumline", chart.sourcePath);
        return Result<PsychExportPackage>::fail("missing player/opponent strumline");
    }

    PsychExportPackage package;
    std::vector<BpmChange> bpms = chart.bpmChanges;
    if (bpms.empty()) bpms.push_back({0.0, chart.bpm > 0.0f ? chart.bpm : 100.0f});
    std::stable_sort(bpms.begin(), bpms.end(), [](const BpmChange& a, const BpmChange& b) {
        return a.timeMs < b.timeMs;
    });
    if (bpms.front().timeMs > 0.0) bpms.insert(bpms.begin(), {0.0, chart.bpm});

    struct StateEvent { double time; int kind; int value; };
    std::vector<StateEvent> stateEvents;
    ojson psychEvents = ojson::array();
    std::map<double, ojson> groupedEvents;
    bool embeddedGlobal = false;
    bool truncatedParams = false;
    for (const ChartEvent& event : chart.events) {
        // El archivo ajeno permanece ajeno tambien al exportar: mostrarlo no
        // autoriza incorporarlo silenciosamente a otro formato.
        if (!event.foreignSource.empty()) continue;
        const std::string name = lowerAscii(event.name);
        if (name == "camera movement") {
            const int line = parseLineIndex(event, opponentLine);
            const bool player = line >= 0 && line < static_cast<int>(chart.strumLines.size()) &&
                                chart.strumLines[static_cast<size_t>(line)].type == 1;
            stateEvents.push_back({event.timeMs, 0, player ? 1 : 0});
            continue;
        }
        if (name == "alt animation toggle") {
            stateEvents.push_back({event.timeMs, 1, parseBoolText(event, false) ? 1 : 0});
            continue;
        }
        if (name == "bpm change") continue;
        ojson tuple = ojson::array();
        tuple.push_back(event.name);
        for (size_t i = 0; i < 2; ++i)
            tuple.push_back(i < event.params.size() ? event.params[i] : std::string());
        if (event.params.size() > 2) truncatedParams = true;
        groupedEvents[event.timeMs].push_back(std::move(tuple));
        if (event.global) embeddedGlobal = true;
    }
    for (auto& item : groupedEvents) psychEvents.push_back(ojson::array({item.first, item.second}));
    std::stable_sort(stateEvents.begin(), stateEvents.end(), [](const StateEvent& a, const StateEvent& b) {
        return a.time < b.time;
    });

    double endTime = std::max(0.0, chart.lastNoteMs());
    for (const ChartEvent& event : chart.events)
        if (event.foreignSource.empty()) endTime = std::max(endTime, event.timeMs);
    const double firstBeatMs = 60000.0 / std::max(1.0f, bpms.front().bpm);
    endTime += firstBeatMs;

    struct SectionInfo { double start, end; float bpm; bool mustHit, alt, changeBpm; ojson json; };
    std::vector<SectionInfo> sections;
    size_t bpmIndex = 0, stateIndex = 0;
    bool mustHit = false, alt = false;
    float lastBpm = bpms.front().bpm;
    double time = 0.0;
    const double epsilon = 0.001;
    while (time < endTime + epsilon && sections.size() < 100000) {
        while (bpmIndex + 1 < bpms.size() && bpms[bpmIndex + 1].timeMs <= time + epsilon)
            ++bpmIndex;
        while (stateIndex < stateEvents.size() && stateEvents[stateIndex].time <= time + epsilon) {
            if (stateEvents[stateIndex].kind == 0) mustHit = stateEvents[stateIndex].value != 0;
            else alt = stateEvents[stateIndex].value != 0;
            ++stateIndex;
        }
        const float bpm = std::max(1.0f, bpms[bpmIndex].bpm);
        double next = time + 4.0 * (60000.0 / bpm);
        if (bpmIndex + 1 < bpms.size() && bpms[bpmIndex + 1].timeMs > time + epsilon)
            next = std::min(next, bpms[bpmIndex + 1].timeMs);
        if (stateIndex < stateEvents.size() && stateEvents[stateIndex].time > time + epsilon)
            next = std::min(next, stateEvents[stateIndex].time);
        if (next <= time + epsilon) next = time + 60000.0 / bpm;

        ojson section = ojson::object();
        section["sectionNotes"] = ojson::array();
        section["sectionBeats"] = (next - time) / (60000.0 / bpm);
        section["mustHitSection"] = mustHit;
        if (alt) section["altAnim"] = true;
        const bool change = !sections.empty() && std::abs(bpm - lastBpm) > 0.0001f;
        if (change) { section["changeBPM"] = true; section["bpm"] = bpm; }
        sections.push_back({time, next, bpm, mustHit, alt, change, std::move(section)});
        lastBpm = bpm;
        time = next;
        if (time > endTime && !sections.empty()) break;
    }
    if (sections.empty()) {
        ojson section = {{"sectionNotes", ojson::array()}, {"sectionBeats", 4},
                         {"mustHitSection", false}};
        sections.push_back({0.0, 4.0 * firstBeatMs, bpms.front().bpm, false, false, false,
                            std::move(section)});
    }

    int skippedExtra = 0;
    for (const ChartNote& note : chart.notes) {
        if (note.strumLine != playerLine && note.strumLine != opponentLine) {
            ++skippedExtra;
            continue;
        }
        size_t sectionIndex = 0;
        while (sectionIndex + 1 < sections.size() &&
               note.timeMs >= sections[sectionIndex].end - epsilon) ++sectionIndex;
        const bool player = note.strumLine == playerLine;
        int direction = ((note.lane % 4) + 4) % 4;
        if (player != sections[sectionIndex].mustHit) direction += 4;
        ojson raw = ojson::array({note.timeMs, direction, std::max(0.0, note.sustainMs)});
        if (!note.type.empty()) raw.push_back(note.type);
        sections[sectionIndex].json["sectionNotes"].push_back(std::move(raw));
    }
    if (skippedExtra > 0)
        package.warnings.push_back(std::to_string(skippedExtra) +
            " notes on additional strumlines could not be represented in Psych and were skipped");
    if (embeddedGlobal)
        package.warnings.push_back("global events were embedded in this difficulty so the export stays self-contained");
    if (truncatedParams)
        package.warnings.push_back("Psych supports two event values; additional Codename parameters were omitted");

    ojson sectionArray = ojson::array();
    for (SectionInfo& section : sections) sectionArray.push_back(std::move(section.json));
    ojson song = ojson::object();
    song["song"] = displayName.empty() ? (chart.songId.empty() ? "Imported song" : chart.songId)
                                        : displayName;
    song["notes"] = std::move(sectionArray);
    song["events"] = std::move(psychEvents);
    song["bpm"] = bpms.front().bpm;
    song["needsVoices"] = chart.needsVoices;
    song["speed"] = chart.speed > 0.0f ? chart.speed : 1.0f;
    song["offset"] = 0;
    song["player1"] = characterForType(chart, 1, "bf");
    song["player2"] = characterForType(chart, 0, "dad");
    song["gfVersion"] = characterForType(chart, 2, "gf");
    song["stage"] = chart.stage.empty() ? "stage" : chart.stage;
    // Sin `format`: los carriles van con la regla clasica (mustHitSection
    // decide quien canta y 4..7 lo invierte), que Psych 0.7 lee tal cual.
    // Psych 1.0 y Codename convierten todo lo que NO empieza por "psych_v1"
    // (Song.hx:174-180; Chart.hx:68). Con la etiqueta "psych_v1" leian estos
    // carriles como absolutos y cambiaban rival y jugador en cada seccion sin
    // mustHitSection.
    song["generatedBy"] = "Funkin Mod Lab";
    package.text = song.dump(1, '\t', false, ojson::error_handler_t::replace);
    return Result<PsychExportPackage>::ok(std::move(package));
}

bool writeExternalTextAtomic(const fs::path& target, const std::string& text,
                             std::string& error) {
    if (target.empty() || target.filename().empty()) {
        error = "invalid export path";
        return false;
    }
    std::error_code ec;
    const fs::path parent = target.parent_path().empty() ? fs::current_path(ec)
                                                         : target.parent_path();
    if (ec || !fs::is_directory(parent, ec)) {
        error = "export folder does not exist";
        return false;
    }
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temp = parent / fs::u8path(target.filename().u8string() +
        ".fmltmp-" + std::to_string(ticks));
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) { error = "could not create export temporary file"; return false; }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        if (!output) {
            error = "could not finish writing the export";
            output.close();
            fs::remove(temp, ec);
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "could not replace export target (Windows error " +
                std::to_string(GetLastError()) + ")";
        fs::remove(temp, ec);
        return false;
    }
#else
    if (fs::exists(target, ec)) fs::remove(target, ec);
    ec.clear();
    fs::rename(temp, target, ec);
    if (ec) { error = ec.message(); fs::remove(temp, ec); return false; }
#endif
    return true;
}

}  // namespace fml

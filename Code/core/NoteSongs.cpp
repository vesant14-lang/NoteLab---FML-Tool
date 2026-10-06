#include "NoteSongs.hpp"

#include "../support/io/Vfs.hpp"
#include "../support/formats/ChartExchange.hpp"
#include "../support/formats/CodenameChart.hpp"
#include "../support/formats/LegacyChart.hpp"
#include "../support/formats/PsychSongAudio.hpp"
#include "../support/formats/SongMeta.hpp"
#include "../third_party/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>

namespace fml::notelab {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool ignoredFolder(const std::string& lowPath) {
    for (const char* folder : {".fml/", ".temp/", ".git/"})
        if (lowPath.rfind(folder, 0) == 0 || lowPath.find(std::string("/") + folder) != std::string::npos) return true;
    return false;
}

std::string stringField(const json& object, const char* key) {
    return object.is_object() && object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : "";
}

std::string codenameMetaPath(const Vfs& vfs, const fs::path& songDirectory, const std::string& difficulty,
                             const std::string& variation) {
    std::vector<std::string> names;
    if (!variation.empty()) names.push_back("meta-" + variation + "-" + difficulty + ".json");
    if (!variation.empty()) names.push_back("meta-" + variation + ".json");
    names.push_back("meta-" + difficulty + ".json");
    names.push_back("meta.json");
    for (const std::string& name : names) {
        const std::string path = Vfs::normalize((songDirectory / fs::u8path(name)).u8string());
        if (vfs.find(path)) return path;
    }
    return Vfs::normalize((songDirectory / "meta.json").u8string());
}

int difficultyOrder(const json& metadata, const std::string& difficulty, bool vslice) {
    const json* list = &metadata;
    if (vslice && metadata.is_object() && metadata.contains("playData") && metadata["playData"].is_object())
        list = &metadata["playData"];
    if (!list->is_object() || !list->contains("difficulties") || !(*list)["difficulties"].is_array()) return 1000;
    const json& difficulties = (*list)["difficulties"];
    for (size_t i = 0; i < difficulties.size(); ++i)
        if (difficulties[i].is_string() && lower(difficulties[i].get<std::string>()) == lower(difficulty))
            return static_cast<int>(i);
    return 1000;
}

// El reproductor necesita el archivo en disco. Lo que vive dentro de un ZIP lo
// saca la Vfs a su cache temporal (Vfs::resolve, fuera del mod), y solo lo que
// existe: probar nombres no extrae nada.
std::string physical(const Vfs& vfs, const std::string& virtualPath) {
    const auto entry = vfs.find(virtualPath);
    if (!entry) return {};
    const auto path = vfs.resolve(*entry);
    return path ? path->u8string() : std::string();
}

std::string audioFile(const Vfs& vfs, const std::vector<std::string>& folders, const std::vector<std::string>& names) {
    for (const std::string& name : names)
        for (const std::string& folder : folders)
            for (const char* extension : {".ogg", ".mp3", ".wav", ".flac"}) {
                const std::string stem = fs::u8path(name).stem().u8string();
                const std::string found = physical(vfs, folder + "/" + stem + extension);
                if (!found.empty()) return found;
            }
    return {};
}

std::vector<std::string> stemNames(const std::string& stem, const std::string& suffix, const std::string& difficulty) {
    std::vector<std::string> names;
    if (!suffix.empty()) {
        names.push_back(stem + suffix);
        if (suffix.front() != '-' && suffix.front() != '_') names.push_back(stem + "-" + suffix);
    }
    if (!difficulty.empty()) {
        names.push_back(stem + "-" + difficulty);
        names.push_back(stem + difficulty);
    }
    names.push_back(stem);
    return names;
}

std::vector<std::string> codenameNames(const std::string& stem, const std::string& suffix,
                                       const std::string& difficulty, const std::string& variation) {
    std::vector<std::string> names;
    if (!suffix.empty()) {
        names.push_back(stem + suffix);
        if (suffix.front() != '-' && suffix.front() != '_') names.push_back(stem + "-" + suffix);
    }
    if (!variation.empty()) {
        if (!difficulty.empty()) names.push_back(stem + "-" + variation + "-" + difficulty);
        names.push_back(stem + "-" + variation);
    }
    if (!difficulty.empty()) {
        names.push_back(stem + "-" + difficulty);
        names.push_back(stem + difficulty);
    }
    names.push_back(stem);
    return names;
}

std::vector<std::string> vsliceVoiceIds(const json& characters, const char* field, const std::string& fallback) {
    std::vector<std::string> ids;
    if (characters.is_object() && characters.contains(field) && characters[field].is_array()) {
        for (const json& value : characters[field])
            if (value.is_string() && !value.get<std::string>().empty()) ids.push_back(value.get<std::string>());
    } else if (!fallback.empty()) {
        ids.push_back(fallback);
    }
    return ids;
}

std::string vsliceVoice(const Vfs& vfs, const std::vector<std::string>& folders, const std::string& singer,
                        const std::string& variation) {
    auto findVoice = [&](const std::string& suffix) {
        std::string id = singer;
        while (!id.empty()) {
            const std::string found = audioFile(vfs, folders, {"Voices-" + id + suffix});
            if (!found.empty()) return found;
            const size_t separator = id.rfind('-');
            if (separator == std::string::npos) break;
            id.resize(separator);
        }
        return std::string();
    };
    if (!variation.empty()) {
        const std::string found = findVoice("-" + variation);
        if (!found.empty()) return found;
    }
    return findVoice("");
}

// Un parametro de evento guardado como texto: numero, o el valor por defecto.
double eventNumber(const std::vector<std::string>& params, size_t index, double fallback) {
    if (index >= params.size()) return fallback;
    std::string text = params[index];
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front())
        text = text.substr(1, text.size() - 2);
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end && end != text.c_str() && std::isfinite(value) ? value : fallback;
}

bool eventBool(const std::vector<std::string>& params, size_t index, bool fallback) {
    if (index >= params.size()) return fallback;
    const std::string text = lower(params[index]);
    if (text == "true") return true;
    if (text == "false") return false;
    return fallback;
}

// Lo que el chart fija ademas de las notas, leido del chart y de su metadata.
SongValues songValuesOf(const Vfs& vfs, const SongChart& song, const UniversalChart& chart) {
    SongValues values;
    values.speed = chart.speed > 0.0f ? chart.speed : 1.0f;
    values.bpm = chart.bpm > 0.0f ? chart.bpm : 100.0f;
    values.bpmChanges = chart.bpmChanges;
    values.beatsPerMeasure = chart.beatsPerMeasure;
    values.stepsPerBeat = chart.stepsPerBeat > 0 ? chart.stepsPerBeat : 4;
    values.needsVoices = chart.needsVoices;
    values.player = !chart.player1.empty() ? chart.player1 : song.player;
    values.opponent = !chart.player2.empty() ? chart.player2 : song.opponent;
    values.girlfriend = !chart.gfVersion.empty() ? chart.gfVersion : song.girlfriend;
    values.stage = !chart.stage.empty() ? chart.stage : song.stage;
    TimeMap time;
    time.build(chart);
    auto stepMs = [&](double ms) { return 60000.0 / std::max(1.0f, time.bpmAt(ms)) / 4.0; };

    if (song.format == SongChart::Format::VSlice) {
        const json meta = json::parse(vfs.readText(song.metadataPath).value_or("{}"), nullptr, false, true);
        if (meta.is_object()) {
            if (meta.contains("playData") && meta["playData"].is_object()) {
                const json& play = meta["playData"];
                if (play.contains("noteStyle") && play["noteStyle"].is_string()) values.noteStyle = play["noteStyle"].get<std::string>();
            }
            if (meta.contains("offsets") && meta["offsets"].is_object() && meta["offsets"].contains("instrumental") &&
                meta["offsets"]["instrumental"].is_number())
                values.audioOffsetMs = meta["offsets"]["instrumental"].get<double>();
        }
        // Los eventos del chart en crudo, para no perder su `strumline`.
        const json raw = json::parse(vfs.readText(song.path).value_or("{}"), nullptr, false, true);
        if (raw.is_object() && raw.contains("events") && raw["events"].is_array())
            for (const json& event : raw["events"]) {
                if (!event.is_object() || !event.contains("e") || !event["e"].is_string() || event["e"].get<std::string>() != "ScrollSpeed")
                    continue;
                const json value = event.contains("v") && event["v"].is_object() ? event["v"] : json::object();
                auto numberField = [&](const char* key, double fallback) {
                    if (!value.contains(key)) return fallback;
                    if (value[key].is_number()) return value[key].get<double>();
                    if (value[key].is_string()) {
                        char* end = nullptr;
                        const std::string text = value[key].get<std::string>();
                        const double parsed = std::strtod(text.c_str(), &end);
                        if (end && end != text.c_str()) return parsed;
                    }
                    return fallback;
                };
                SpeedChange change;
                change.timeMs = event.contains("t") && event["t"].is_number() ? event["t"].get<double>() : 0.0;
                const bool absolute = value.contains("absolute") && value["absolute"].is_boolean() && value["absolute"].get<bool>();
                const double scroll = numberField("scroll", 1.0);
                change.speed = static_cast<float>(absolute ? scroll : scroll * values.speed);
                const std::string ease = value.contains("ease") && value["ease"].is_string() ? value["ease"].get<std::string>() : "linear";
                change.durationMs = ease == "INSTANT" ? 0.0 : std::max(0.0, numberField("duration", 4.0)) * stepMs(change.timeMs);
                const std::string line = value.contains("strumline") && value["strumline"].is_string()
                                             ? lower(value["strumline"].get<std::string>()) : "both";
                change.strumLine = line == "player" ? 1 : line == "opponent" ? 0 : -1;
                values.speedChanges.push_back(change);
            }
    } else {
        const bool psychFormat = song.format == SongChart::Format::Psych || song.format == SongChart::Format::CodenameLegacy;
        if (psychFormat) {
            const json raw = json::parse(vfs.readText(song.path).value_or("{}"), nullptr, false, true);
            const json& data = raw.is_object() && raw.contains("song") && raw["song"].is_object() ? raw["song"] : raw;
            if (data.is_object()) {
                if (data.contains("arrowSkin") && data["arrowSkin"].is_string()) values.arrowSkin = data["arrowSkin"].get<std::string>();
                if (data.contains("splashSkin") && data["splashSkin"].is_string()) values.splashSkin = data["splashSkin"].get<std::string>();
                if (song.format == SongChart::Format::Psych && data.contains("offset") && data["offset"].is_number())
                    values.audioOffsetMs = data["offset"].get<double>();
            }
            // Los eventos de Psych: los del chart, los de events.json de su
            // carpeta (PlayState.hx:1327) y, en charts viejos, las notas con
            // direccion -1. «Change Scroll Speed» multiplica la del chart y
            // tarda value2 segundos (PlayState.hx:2254-2270).
            auto text = [](const json& value) {
                if (value.is_string()) return value.get<std::string>();
                if (value.is_number()) return value.dump();
                return std::string();
            };
            auto psychEvent = [&](double timeMs, const std::string& name, const std::string& value1, const std::string& value2) {
                if (name != "Change Scroll Speed") return;
                SpeedChange change;
                change.timeMs = timeMs;
                change.speed = static_cast<float>(values.speed * eventNumber({value1}, 0, 1.0));
                change.durationMs = std::max(0.0, eventNumber({value2}, 0, 0.0)) * 1000.0;
                if (change.speed > 0.0f) values.speedChanges.push_back(change);
            };
            auto scanGroups = [&](const json& events) {
                if (!events.is_array()) return;
                for (const json& group : events) {
                    if (!group.is_array() || group.size() < 2 || !group[0].is_number() || !group[1].is_array()) continue;
                    for (const json& event : group[1])
                        if (event.is_array() && !event.empty() && event[0].is_string())
                            psychEvent(group[0].get<double>(), event[0].get<std::string>(), event.size() > 1 ? text(event[1]) : "",
                                       event.size() > 2 ? text(event[2]) : "");
                }
            };
            auto eventsOf = [](const json& document) -> const json* {
                if (!document.is_object()) return nullptr;
                const json& inner = document.contains("song") && document["song"].is_object() ? document["song"] : document;
                return inner.contains("events") ? &inner["events"] : nullptr;
            };
            if (const json* own = eventsOf(raw)) scanGroups(*own);
            if (data.is_object() && data.contains("notes") && data["notes"].is_array())
                for (const json& section : data["notes"]) {
                    if (!section.is_object() || !section.contains("sectionNotes") || !section["sectionNotes"].is_array()) continue;
                    for (const json& note : section["sectionNotes"])
                        if (note.is_array() && note.size() >= 3 && note[0].is_number() && note[1].is_number() &&
                            note[1].get<double>() < 0.0 && note[2].is_string())
                            psychEvent(note[0].get<double>(), note[2].get<std::string>(), note.size() > 3 ? text(note[3]) : "",
                                       note.size() > 4 ? text(note[4]) : "");
                }
            const std::string eventsPath = Vfs::normalize((fs::u8path(song.path).parent_path() / "events.json").u8string());
            if (lower(eventsPath) != lower(Vfs::normalize(song.path)))
                if (const auto eventsText = vfs.readText(eventsPath)) {
                    const json events = json::parse(*eventsText, nullptr, false, true);
                    if (const json* list = eventsOf(events)) scanGroups(*list);
                }
        }
        for (size_t i = 0; i < chart.strumLines.size(); ++i) {
            const ChartStrumLine& line = chart.strumLines[i];
            if ((line.type == 0 || line.type == 1) && line.hasScrollSpeed && line.scrollSpeed > 0.0f)
                values.lineSpeed[static_cast<size_t>(line.type)] = line.scrollSpeed;
        }
        std::vector<ChartEvent> events = chart.events;
        std::stable_sort(events.begin(), events.end(), [](const ChartEvent& a, const ChartEvent& b) { return a.timeMs < b.timeMs; });
        for (const ChartEvent& event : events) {
            SpeedChange change;
            change.timeMs = event.timeMs;
            if (event.name == "Change Scroll Speed") {
                if (psychFormat) continue;   // ya leidos arriba, de la fuente
                change.speed = static_cast<float>(values.speed * eventNumber(event.params, 0, 1.0));
                change.durationMs = std::max(0.0, eventNumber(event.params, 1, 0.0)) * 1000.0;
            } else if (event.name == "Scroll Speed Change") {
                // Codename: con el sexto parametro multiplica la que haya en ese momento.
                double speed = eventNumber(event.params, 1, 1.0);
                if (eventBool(event.params, 5, false)) speed *= speedAt(values, event.timeMs, -1);
                change.speed = static_cast<float>(speed);
                change.durationMs = eventBool(event.params, 0, true) ? std::max(0.0, eventNumber(event.params, 2, 4.0)) * stepMs(event.timeMs) : 0.0;
            } else {
                continue;
            }
            if (change.speed > 0.0f) values.speedChanges.push_back(change);
        }
    }
    std::stable_sort(values.speedChanges.begin(), values.speedChanges.end(),
                     [](const SpeedChange& a, const SpeedChange& b) { return a.timeMs < b.timeMs; });
    return values;
}

}  // namespace

const char* songFormatKey(SongChart::Format format) {
    switch (format) {
        case SongChart::Format::Codename: return "codename";
        case SongChart::Format::CodenameLegacy: return "codename-legacy";
        case SongChart::Format::VSlice: return "vslice";
        case SongChart::Format::Psych: return "psych";
    }
    return "?";
}

std::vector<SongChart> scanSongs(const Vfs& vfs) {
    std::vector<SongChart> songs;
    std::set<std::string> seen;
    for (const Vfs::Entry& entry : vfs.allEntries()) {
        if (entry.isDir) continue;
        const std::string path = lower(Vfs::normalize(entry.virtualPath));
        if (ignoredFolder(path)) continue;
        if (fs::u8path(path).extension() != ".json" || !seen.insert(path).second) continue;
        const fs::path chartPath = fs::u8path(entry.virtualPath);
        const bool variantChart = lower(chartPath.parent_path().parent_path().filename().u8string()) == "charts";
        const bool codenamePath = (lower(chartPath.parent_path().filename().u8string()) == "charts" || variantChart) &&
            (path.rfind("songs/", 0) == 0 || path.find("/songs/") != std::string::npos);
        const fs::path codenameSongDirectory = variantChart ? chartPath.parent_path().parent_path().parent_path()
                                                            : chartPath.parent_path().parent_path();
        const std::string codenameVariation = variantChart ? chartPath.parent_path().filename().u8string() : "";
        const std::string chartStem = lower(chartPath.stem().u8string());
        const size_t chartMarker = chartStem.rfind("-chart");
        const bool vslicePath = chartMarker != std::string::npos && chartMarker > 0 &&
            (chartMarker + 6 == chartStem.size() ||
             (chartMarker + 7 < chartStem.size() && chartStem[chartMarker + 6] == '-')) &&
            (path.rfind("songs/", 0) == 0 || path.find("/songs/") != std::string::npos);
        const bool psychPath = path.rfind("data/", 0) == 0 || path.find("/data/") != std::string::npos;
        if (!codenamePath && !vslicePath && !psychPath) continue;
        if (entry.size > 64ull * 1024ull * 1024ull) continue;
        const auto source = vfs.readText(entry);
        if (!source) continue;
        const json root = json::parse(*source, nullptr, false, true);
        if (!root.is_object()) continue;

        if (codenamePath && root.contains("strumLines") && root["strumLines"].is_array()) {
            SongChart song;
            song.format = SongChart::Format::Codename;
            song.id = codenameSongDirectory.filename().u8string();
            song.difficulty = chartPath.stem().u8string();
            song.variation = codenameVariation;
            song.path = entry.virtualPath;
            song.metadataPath = codenameMetaPath(vfs, codenameSongDirectory, song.difficulty, song.variation);
            const json metadata = json::parse(vfs.readText(song.metadataPath).value_or("{}"), nullptr, false, true);
            song.difficultyOrder = difficultyOrder(metadata, song.difficulty, false);
            song.stage = stringField(root, "stage");
            for (const json& line : root["strumLines"]) {
                if (!line.is_object() || !line.contains("characters") || !line["characters"].is_array() ||
                    line["characters"].empty() || !line["characters"][0].is_string()) continue;
                const std::string character = line["characters"][0].get<std::string>();
                const std::string position = lower(stringField(line, "position"));
                const int type = line.contains("type") && line["type"].is_number_integer() ? line["type"].get<int>() : -1;
                if (position == "boyfriend" || position == "bf" || type == 1) song.player = character;
                else if (position == "girlfriend" || position == "gf" || type == 2) song.girlfriend = character;
                else if (song.opponent.empty()) song.opponent = character;
            }
            songs.push_back(std::move(song));
            continue;
        }
        if (codenamePath && root.contains("song") && root["song"].is_object() && root["song"].contains("notes") &&
            root["song"]["notes"].is_array()) {
            const json& header = root["song"];
            SongChart song;
            song.format = SongChart::Format::CodenameLegacy;
            song.id = codenameSongDirectory.filename().u8string();
            song.difficulty = chartPath.stem().u8string();
            song.variation = codenameVariation;
            song.path = entry.virtualPath;
            song.metadataPath = codenameMetaPath(vfs, codenameSongDirectory, song.difficulty, song.variation);
            const json metadata = json::parse(vfs.readText(song.metadataPath).value_or("{}"), nullptr, false, true);
            song.difficultyOrder = difficultyOrder(metadata, song.difficulty, false);
            song.stage = stringField(header, "stage");
            song.player = stringField(header, "player1");
            song.opponent = stringField(header, "player2");
            song.girlfriend = stringField(header, "gfVersion");
            songs.push_back(std::move(song));
            continue;
        }
        if (vslicePath && root.contains("notes") && root["notes"].is_object()) {
            const std::string originalStem = chartPath.stem().u8string();
            const std::string baseName = originalStem.substr(0, chartMarker);
            const std::string variationSuffix = originalStem.substr(chartMarker + 6);
            const fs::path metaPath = chartPath.parent_path() / fs::u8path(baseName + "-metadata" + variationSuffix + ".json");
            const auto metaText = vfs.readText(Vfs::normalize(metaPath.u8string()));
            if (!metaText) continue;
            const json meta = json::parse(*metaText, nullptr, false, true);
            if (!meta.is_object() || !meta.contains("playData") || !meta["playData"].is_object()) continue;
            const json& play = meta["playData"];
            const json characters = play.contains("characters") && play["characters"].is_object() ? play["characters"] : json::object();
            for (auto notes = root["notes"].begin(); notes != root["notes"].end(); ++notes) {
                if (!notes.value().is_array()) continue;
                SongChart song;
                song.format = SongChart::Format::VSlice;
                song.id = stringField(meta, "songName");
                if (song.id.empty()) song.id = baseName;
                song.difficulty = notes.key();
                song.difficultyOrder = difficultyOrder(meta, song.difficulty, true);
                song.variation = variationSuffix.empty() ? "" : variationSuffix.substr(1);
                song.path = entry.virtualPath;
                song.metadataPath = Vfs::normalize(metaPath.u8string());
                song.stage = stringField(play, "stage");
                song.player = stringField(characters, "player");
                song.opponent = stringField(characters, "opponent");
                song.girlfriend = stringField(characters, "girlfriend");
                songs.push_back(std::move(song));
            }
            continue;
        }
        if (!psychPath || !root.contains("song") || !root["song"].is_object()) continue;
        const json& header = root["song"];
        if (!header.contains("notes") || !header["notes"].is_array()) continue;
        SongChart song;
        song.format = SongChart::Format::Psych;
        song.id = stringField(header, "song");
        if (song.id.empty()) song.id = chartPath.parent_path().filename().u8string();
        song.path = entry.virtualPath;
        song.stage = stringField(header, "stage");
        song.player = stringField(header, "player1");
        song.opponent = stringField(header, "player2");
        song.girlfriend = stringField(header, "gfVersion");
        if (song.girlfriend.empty()) song.girlfriend = stringField(header, "player3");
        const std::string stem = lower(chartPath.stem().u8string());
        const std::string folder = lower(chartPath.parent_path().filename().u8string());
        song.difficulty = stem == folder ? "normal" : stem.rfind(folder + "-", 0) == 0 ? stem.substr(folder.size() + 1) : stem;
        songs.push_back(std::move(song));
    }
    std::sort(songs.begin(), songs.end(), [](const SongChart& a, const SongChart& b) {
        if (lower(a.id) != lower(b.id)) return lower(a.id) < lower(b.id);
        if (lower(a.variation) != lower(b.variation)) return lower(a.variation) < lower(b.variation);
        if (a.difficultyOrder != b.difficultyOrder) return a.difficultyOrder < b.difficultyOrder;
        if (lower(a.difficulty) != lower(b.difficulty)) return lower(a.difficulty) < lower(b.difficulty);
        return lower(a.path) < lower(b.path);
    });
    return songs;
}

LoadedSong loadSong(const Vfs& vfs, const fs::path& root, const SongChart& song) {
    LoadedSong loaded;
    const auto source = vfs.readText(song.path);
    DiagnosticSink diagnostics;
    if (!source) {
        loaded.error = "Could not read the chart.";
        return loaded;
    }
    Result<UniversalChart> parsed = Result<UniversalChart>::fail("Unsupported chart format");
    SongMeta codenameMeta;
    if (song.format == SongChart::Format::Codename || song.format == SongChart::Format::CodenameLegacy) {
        codenameMeta = parseSongMeta(vfs.readText(song.metadataPath).value_or(""), song.id, song.metadataPath, diagnostics);
        const json rawChart = json::parse(*source, nullptr, false, true);
        if (rawChart.is_object() && rawChart.contains("meta") && rawChart["meta"].is_object()) {
            const json& chartMeta = rawChart["meta"];
            if (chartMeta.contains("instSuffix") && chartMeta["instSuffix"].is_string())
                codenameMeta.instSuffix = chartMeta["instSuffix"].get<std::string>();
            if (chartMeta.contains("vocalsSuffix") && chartMeta["vocalsSuffix"].is_string())
                codenameMeta.vocalsSuffix = chartMeta["vocalsSuffix"].get<std::string>();
        }
        if (song.format == SongChart::Format::CodenameLegacy) {
            parsed = parseLegacyChart(*source, song.path, diagnostics);
        } else {
            CodenameChartDefaults defaults;
            defaults.songId = song.id;
            defaults.displayName = codenameMeta.displayName;
            defaults.bpm = codenameMeta.bpm;
            defaults.beatsPerMeasure = codenameMeta.beatsPerMeasure;
            defaults.stepsPerBeat = codenameMeta.stepsPerBeat;
            defaults.needsVoices = codenameMeta.needsVoices;
            parsed = parseCodenameChart(*source, song.path, defaults, diagnostics);
        }
    } else if (song.format == SongChart::Format::VSlice) {
        const auto metadata = vfs.readText(song.metadataPath);
        if (!metadata) {
            loaded.error = "The V-Slice metadata file is missing.";
            return loaded;
        }
        auto bundle = importFunkinBaseChartText(*source, *metadata, song.path, diagnostics);
        if (!bundle) {
            loaded.error = bundle.error();
            return loaded;
        }
        for (auto& candidate : bundle.value().candidates)
            if (candidate.difficulty == song.difficulty) {
                parsed = Result<UniversalChart>::ok(std::move(candidate.chart));
                break;
            }
    } else {
        parsed = parseLegacyChart(*source, song.path, diagnostics);
    }
    if (!parsed) {
        loaded.error = parsed.error();
        return loaded;
    }
    loaded.chart = std::move(parsed.value());
    loaded.values = songValuesOf(vfs, song, loaded.chart);

    // Audio: la misma busqueda que Song Lab, sin mezclar las voces por linea.
    const fs::path chartPath = fs::u8path(song.path);
    const std::string chartStem = chartPath.stem().u8string();
    const std::string folder = chartPath.parent_path().filename().u8string();
    std::vector<std::string>& tracks = loaded.audio;
    if (song.format == SongChart::Format::Psych) {
        const std::string virtualChart = Vfs::normalize(song.path);
        const std::string loweredChart = lower(virtualChart);
        const size_t dataMarker = loweredChart.find("/data/");
        const std::string prefix = loweredChart.rfind("data/", 0) == 0 ? "" :
            dataMarker == std::string::npos ? "" : virtualChart.substr(0, dataMarker);
        std::vector<std::string> audioFolders;
        std::set<std::string> seenFolders;
        for (const std::string& id : {chartStem, psych::formatSongPath(song.id), folder}) {
            if (!psych::relativeKey(id)) continue;
            const std::string path = (prefix.empty() ? "" : prefix + "/") + "songs/" + id;
            if (seenFolders.insert(lower(path)).second) audioFolders.push_back(path);
        }
        if (!prefix.empty() && lower(prefix) != "assets")
            for (const std::string& id : {psych::formatSongPath(song.id), folder}) {
                const std::string path = "assets/songs/" + id;
                if (psych::relativeKey(id) && seenFolders.insert(lower(path)).second) audioFolders.push_back(path);
            }
        const auto physicalChart = vfs.resolve(song.path);
        psych::SongAudio resolved;
        if (physicalChart) resolved = psych::resolveSongAudio(psych::roots(root), *physicalChart, folder);
        const std::string difficulty = song.difficulty == "normal" ? "" : song.difficulty;
        std::string inst = audioFile(vfs, audioFolders, stemNames("Inst", "", difficulty));
        if (inst.empty()) inst = resolved.instPath;
        std::string voices = audioFile(vfs, audioFolders, stemNames("Voices", "", difficulty));
        if (voices.empty()) voices = resolved.voicesPath;
        if (!inst.empty()) tracks.push_back(inst);
        std::set<std::string> added;
        for (const std::string& split : {resolved.playerVoicesPath, resolved.opponentVoicesPath})
            if (!split.empty() && added.insert(lower(split)).second) tracks.push_back(split);
        if (added.empty() && !voices.empty()) tracks.push_back(voices);
    } else {
        const bool codenameAudio = song.format == SongChart::Format::Codename ||
                                   song.format == SongChart::Format::CodenameLegacy;
        const fs::path songDirectory = codenameAudio
            ? (song.variation.empty() ? chartPath.parent_path().parent_path() : chartPath.parent_path().parent_path().parent_path())
            : chartPath.parent_path();
        std::vector<std::string> audioFolders;
        if (codenameAudio && !song.variation.empty())
            audioFolders.push_back(Vfs::normalize((songDirectory / "song" / fs::u8path(song.variation)).u8string()));
        if (codenameAudio) audioFolders.push_back(Vfs::normalize((songDirectory / "song").u8string()));
        const std::string songKey = songDirectory.filename().u8string();
        const std::string path = Vfs::normalize(songDirectory.u8string());
        const std::string lowered = lower(path);
        const size_t dataSongs = lowered.rfind("/data/songs/");
        if (lowered.rfind("data/songs/", 0) == 0) {
            audioFolders.push_back("songs/" + songKey);
        } else if (dataSongs != std::string::npos) {
            const std::string prefix = path.substr(0, dataSongs);
            audioFolders.push_back(prefix + "/songs/" + songKey);
            const std::string loweredPrefix = lower(prefix);
            if (loweredPrefix == "preload" ||
                (loweredPrefix.size() > 8 && loweredPrefix.substr(loweredPrefix.size() - 8) == "/preload")) {
                const size_t suffixLength = loweredPrefix == "preload" ? 7 : 8;
                const std::string parent = prefix.substr(0, prefix.size() - suffixLength);
                audioFolders.push_back((parent.empty() ? "" : parent + "/") + "songs/" + songKey);
            }
        }
        if (codenameAudio && !song.variation.empty())
            audioFolders.push_back(Vfs::normalize((songDirectory / fs::u8path(song.variation)).u8string()));
        audioFolders.push_back(Vfs::normalize(songDirectory.u8string()));
        std::string suffix = codenameAudio ? codenameMeta.instSuffix : "";
        json vsliceCharacters = json::object();
        if (song.format == SongChart::Format::VSlice) {
            const json meta = json::parse(vfs.readText(song.metadataPath).value_or("{}"), nullptr, false, true);
            if (meta.is_object() && meta.contains("playData") && meta["playData"].is_object()) {
                const json& play = meta["playData"];
                if (play.contains("characters") && play["characters"].is_object()) {
                    vsliceCharacters = play["characters"];
                    if (vsliceCharacters.contains("instrumental") && vsliceCharacters["instrumental"].is_string())
                        suffix = vsliceCharacters["instrumental"].get<std::string>();
                    else suffix = song.variation;
                }
            }
        }
        const std::vector<std::string> instNames = codenameAudio
            ? codenameNames("Inst", suffix, song.difficulty, song.variation)
            : suffix.empty() ? std::vector<std::string>{"Inst"} : std::vector<std::string>{"Inst-" + suffix, "Inst"};
        const std::string inst = audioFile(vfs, audioFolders, instNames);
        if (!inst.empty()) tracks.push_back(inst);
        if (song.format == SongChart::Format::VSlice) {
            std::set<std::string> added;
            for (const auto& role : {std::make_pair("playerVocals", song.player), std::make_pair("opponentVocals", song.opponent)})
                for (const std::string& singer : vsliceVoiceIds(vsliceCharacters, role.first, role.second)) {
                    const std::string split = vsliceVoice(vfs, audioFolders, singer, song.variation);
                    if (!split.empty() && added.insert(lower(split)).second) tracks.push_back(split);
                }
            if (added.empty()) {
                std::vector<std::string> names;
                if (!song.variation.empty()) names.push_back("Voices-" + song.variation + ".ogg");
                names.push_back("Voices.ogg");
                const std::string voices = audioFile(vfs, audioFolders, names);
                if (!voices.empty()) tracks.push_back(voices);
            }
        } else {
            const std::string voices = audioFile(vfs, audioFolders,
                codenameNames("Voices", codenameMeta.vocalsSuffix, song.difficulty, song.variation));
            std::set<std::string> added;
            if (!voices.empty()) {
                tracks.push_back(voices);
                added.insert(lower(voices));
            }
            for (const ChartStrumLine& line : loaded.chart.strumLines) {
                if (line.vocalsSuffix.empty()) continue;
                std::vector<std::string> names;
                if (!song.variation.empty() && !song.difficulty.empty())
                    names.push_back("Voices" + line.vocalsSuffix + "-" + song.variation + "-" + song.difficulty);
                if (!song.variation.empty()) names.push_back("Voices" + line.vocalsSuffix + "-" + song.variation);
                if (!song.difficulty.empty()) names.push_back("Voices" + line.vocalsSuffix + "-" + song.difficulty);
                names.push_back("Voices" + line.vocalsSuffix);
                if (line.vocalsSuffix.front() != '-' && line.vocalsSuffix.front() != '_')
                    names.push_back("Voices-" + line.vocalsSuffix);
                const std::string split = audioFile(vfs, audioFolders, names);
                if (!split.empty() && added.insert(lower(split)).second) tracks.push_back(split);
            }
        }
    }
    if (tracks.empty()) loaded.warning = "No audio file was found for this chart.";
    return loaded;
}

float speedAt(const SongValues& values, double ms, int strumLine) {
    if (strumLine == 0 || strumLine == 1) {
        const float own = values.lineSpeed[static_cast<size_t>(strumLine)];
        if (own > 0.0f) return own;
    }
    auto applies = [&](const SpeedChange& change) {
        return strumLine < 0 || change.strumLine < 0 || change.strumLine == strumLine;
    };
    float current = values.speed;
    const size_t count = values.speedChanges.size();
    for (size_t i = 0; i < count; ++i) {
        const SpeedChange& change = values.speedChanges[i];
        if (change.timeMs > ms) break;
        if (!applies(change)) continue;
        // Hasta cuando manda: ahora, o hasta que empiece el siguiente cambio,
        // que arranca desde donde iba este (Codename cancela la transicion anterior).
        double until = ms;
        for (size_t k = i + 1; k < count; ++k)
            if (applies(values.speedChanges[k])) {
                if (values.speedChanges[k].timeMs <= ms) until = values.speedChanges[k].timeMs;
                break;
            }
        if (change.durationMs > 0.0 && until < change.timeMs + change.durationMs) {
            const double t = std::clamp((until - change.timeMs) / change.durationMs, 0.0, 1.0);
            current = static_cast<float>(current + (change.speed - current) * t);
        } else {
            current = change.speed;
        }
    }
    return current;
}

std::vector<PreviewNote> previewNotesOf(const UniversalChart& chart, std::vector<std::string>* types) {
    std::vector<PreviewNote> notes;
    if (types) types->clear();
    for (const ChartNote& note : chart.notes) {
        int line = note.isPlayer ? 1 : 0;
        if (note.strumLine >= 0 && static_cast<size_t>(note.strumLine) < chart.strumLines.size()) {
            const int type = chart.strumLines[static_cast<size_t>(note.strumLine)].type;
            if (type != 0 && type != 1) continue;   // adicionales (GF): aun no
            line = type;
        }
        PreviewNote preview;
        preview.strumLine = line;
        preview.lane = ((note.direction % 4) + 4) % 4;
        preview.timeMs = note.timeMs;
        preview.sustainMs = note.sustainMs;
        notes.push_back(preview);
        if (types) types->push_back(note.type);
    }
    return notes;
}

std::string patchSongNoteTypes(const std::string& original, const SongChart& song,
    const std::vector<PreviewNote>& notes, const std::vector<std::string>& types, std::string& error) {
    error.clear();
    if (notes.size() != types.size()) { error = "Note/type counts do not match."; return {}; }
    json doc = json::parse(original, nullptr, false, true);
    if (!doc.is_object()) { error = "The original chart is not readable JSON."; return {}; }
    struct Ref { json* note; double time; int lane, side; double sustain; std::string before; };
    std::vector<Ref> refs;
    const bool native = song.format == SongChart::Format::Codename;
    const bool vslice = song.format == SongChart::Format::VSlice;
    bool absolute = false;   // Psych 1.0, `format` psych_v1: carriles absolutos
    json* table = nullptr;
    try {
        if (native) {
            if (!doc.contains("strumLines") || !doc["strumLines"].is_array()) throw std::runtime_error("Missing strumLines.");
            if (!doc.contains("noteTypes")) doc["noteTypes"] = json::array();
            if (!doc["noteTypes"].is_array()) throw std::runtime_error("Invalid noteTypes table.");
            table = &doc["noteTypes"];
            for (auto& line : doc["strumLines"]) {
                if (!line.is_object() || !line.contains("notes") || !line["notes"].is_array()) continue;
                const int side = line.value("type", 0);
                if (side != 0 && side != 1) continue;
                for (auto& n : line["notes"]) {
                    if (!n.is_object() || !n.contains("time") || !n["time"].is_number() || !n.contains("id") || !n["id"].is_number_integer()) continue;
                    const int lane = n["id"].get<int>();
                    std::string before;
                    if (n.contains("type") && n["type"].is_string()) before = n["type"].get<std::string>();
                    else if (n.contains("type") && n["type"].is_number_integer()) {
                        const int id = n["type"].get<int>(); if (id > 0 && id <= static_cast<int>(table->size()) && (*table)[id - 1].is_string()) before = (*table)[id - 1].get<std::string>();
                    }
                    refs.push_back({&n, n["time"].get<double>(), ((lane % 4) + 4) % 4, side, std::max(0.0, n.value("sLen", 0.0)), before});
                }
            }
        } else if (vslice) {
            if (!doc.contains("notes") || !doc["notes"].is_object() || !doc["notes"].contains(song.difficulty) || !doc["notes"][song.difficulty].is_array())
                throw std::runtime_error("The chosen difficulty is missing.");
            for (auto& n : doc["notes"][song.difficulty]) {
                if (!n.is_object() || !n.contains("t") || !n["t"].is_number() || !n.contains("d") || !n["d"].is_number_integer()) continue;
                const int d = n["d"].get<int>();
                if (d < 0) continue;
                std::string before = n.value("k", std::string()); if (before == "alt") before = "Alt Anim Note";
                refs.push_back({&n, n["t"].get<double>(), d % 4, d % 8 < 4 ? 1 : 0, std::max(0.0, n.value("l", 0.0)), before});
            }
        } else {
            json& root = doc.contains("song") && doc["song"].is_object() ? doc["song"] : doc;
            if (!root.contains("notes") || !root["notes"].is_array()) throw std::runtime_error("Missing chart sections.");
            const int keys = root.value("keyCount", 4);
            if (keys != 4) throw std::runtime_error("Direct chart saving currently requires four-key legacy charts; the original is unchanged.");
            // Psych 1.0 (`format` psych_v1): carriles absolutos, 0..3 del
            // jugador, sin tipos empacados (LegacyChart lee igual).
            absolute = root.contains("format") && root["format"].is_string() &&
                root["format"].get<std::string>().rfind("psych_v1", 0) == 0;
            for (auto& section : root["notes"]) {
                if (!section.is_object() || !section.contains("sectionNotes") || !section["sectionNotes"].is_array()) continue;
                const bool mustHit = section.value("mustHitSection", false);
                for (auto& n : section["sectionNotes"]) {
                    if (!n.is_array() || n.size() < 3 || !n[0].is_number() || !n[1].is_number() || !n[2].is_number()) continue;
                    const int d = n[1].get<int>();
                    if (d < 0) continue;
                    std::string before;
                    int typeId = absolute ? 0 : d / 8;
                    if (n.size() > 3 && n[3].is_string()) before = n[3].get<std::string>();
                    else {
                        if (n.size() > 3 && n[3].is_number_integer()) typeId = n[3].get<int>();
                        if (root.contains("noteTypes") && root["noteTypes"].is_array() && typeId > 0 && typeId <= static_cast<int>(root["noteTypes"].size()) && root["noteTypes"][typeId - 1].is_string()) before = root["noteTypes"][typeId - 1].get<std::string>();
                    }
                    const bool player = absolute ? d < 4 : (d % 8 >= 4 ? !mustHit : mustHit);
                    refs.push_back({&n, n[0].get<double>(), d % 4, player ? 1 : 0, std::max(0.0, n[2].get<double>()), before});
                }
            }
        }
        std::stable_sort(refs.begin(), refs.end(), [](const Ref& a, const Ref& b) { return a.time < b.time; });
        if (refs.size() != notes.size()) throw std::runtime_error("Chart note mapping is incomplete. Saving is blocked to protect the original.");
        for (size_t i = 0; i < refs.size(); ++i) {
            const auto& r = refs[i]; const auto& n = notes[i];
            if (std::abs(r.time - n.timeMs) > 0.001 || std::abs(r.sustain - n.sustainMs) > 0.001 || r.lane != n.lane || r.side != n.strumLine)
                throw std::runtime_error("Chart changed or note mapping does not match. Reload it before saving.");
        }
        for (size_t i = 0; i < refs.size(); ++i) {
            if (refs[i].before == types[i]) continue;
            json& n = *refs[i].note;
            if (native) {
                if (types[i].empty()) n["type"] = 0;
                else {
                    auto found = std::find(table->begin(), table->end(), json(types[i]));
                    if (found == table->end()) { table->push_back(types[i]); found = table->end() - 1; }
                    n["type"] = static_cast<int>(std::distance(table->begin(), found)) + 1;
                }
            } else if (vslice) {
                if (types[i].empty()) n.erase("k"); else n["k"] = types[i] == "Alt Anim Note" ? "alt" : types[i];
            } else {
                while (n.size() < 4) n.push_back("");
                n[3] = types[i];
                // El tipo empacado en note[1] se quita al darle uno con nombre;
                // en psych_v1 no hay empacado y un 8+ es un carril del rival.
                const int d = n[1].get<int>();
                if (d >= 8 && !absolute) n[1] = d % 8;
            }
        }
        return doc.dump(2);
    } catch (const std::exception& e) { error = e.what(); return {}; }
}

bool saveSongChart(const fs::path& target, const std::string& text, const std::string& expected,
                   bool replaceOriginal, fs::path& backup, std::string& error) {
    backup.clear(); error.clear();
    if (lower(target.extension().u8string()) != ".json") { error = "Choose a .json destination; an archive or other asset must not be overwritten with chart text."; return false; }
    if (target.empty() || text.empty() || !json::parse(text, nullptr, false, true).is_object()) { error = "Invalid chart output."; return false; }
    std::error_code ec;
    const bool exists = fs::exists(target, ec);
    if (ec || (exists && !fs::is_regular_file(target, ec)) || (replaceOriginal && !exists)) { error = "Target is not an accessible chart file."; return false; }
    if (exists) {
        if (fs::file_size(target, ec) > 64u * 1024u * 1024u || ec) { error = "Chart exceeds the safe file size."; return false; }
        std::ifstream in(target, std::ios::binary); const std::string current((std::istreambuf_iterator<char>(in)), {});
        if (!in || (replaceOriginal && current != expected)) { error = "The chart changed on disk. Reload it before replacing it."; return false; }
        for (int i = 1; i <= 10000; ++i) {
            fs::path candidate = target; candidate += fs::u8path(".notelab-backup-" + std::to_string(i) + ".bak").native();
            if (fs::copy_file(target, candidate, fs::copy_options::none, ec)) { backup = candidate; break; }
            if (ec != std::errc::file_exists) { error = "Cannot create backup: " + ec.message(); return false; }
            ec.clear();
        }
        if (backup.empty()) { error = "Cannot reserve a backup name."; return false; }
    }
    if (!target.parent_path().empty()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) { error = "Cannot create chart destination folder: " + ec.message(); return false; }
    }
    if (!writeExternalTextAtomic(target, text, error)) return false;
    return true;
}

}  // namespace fml::notelab

#include "CodenameChart.hpp"

#include "JsonRead.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

using json = nlohmann::json;

namespace fml {
namespace {

// Lectura tolerante compartida con el parser legacy: `lower`, `number`,
// `integer`, `boolean`, `stringValue`. Ver JsonRead.hpp para el porque.
using namespace jsonread;

std::string eventNameFromLegacyType(int type) {
    switch (type) {
        case -1: return "HScript Call";
        case  0: return "Unknown";
        case  1: return "Camera Movement";
        case  2: return "BPM Change";
        case  3: return "Alt Animation Toggle";
        default: return "Unknown";
    }
}

std::string parameterText(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_null()) return {};
    return value.dump();
}

void parseEvents(const json& value, bool global, const std::string& sourcePath,
                 std::vector<ChartEvent>& out, DiagnosticSink& sink) {
    if (!value.is_array()) {
        sink.warn("FML-2420", "la propiedad events no es un arreglo", sourcePath);
        return;
    }

    int skipped = 0;
    int sourceIndex = 0;
    for (const json& raw : value) {
        if (!raw.is_object()) { ++skipped; continue; }
        ChartEvent event;
        event.sourceIndex = sourceIndex++;
        event.sourceJson = raw.dump();
        event.timeMs = number(raw, "time", 0.0);
        event.name = stringValue(raw, "name");
        if (event.name.empty() && raw.contains("type"))
            event.name = eventNameFromLegacyType(integer(raw, "type", 0));
        if (event.name.empty()) event.name = "Unknown";
        event.global = global;
        if (raw.contains("params") && raw["params"].is_array())
            for (const json& param : raw["params"]) {
                event.params.push_back(parameterText(param));
                event.paramsJson.push_back(param.dump());
            }
        out.push_back(std::move(event));
    }
    if (skipped > 0)
        sink.warn("FML-2421", std::to_string(skipped) + " eventos ilegibles se saltaron",
                  sourcePath);
}

std::string defaultPositionForType(int type) {
    if (type == 1) return "boyfriend";
    if (type == 2) return "girlfriend";
    return "dad";
}

}  // namespace

bool isCodenameChartDocument(const std::string& text) {
    const json root = json::parse(text, nullptr, false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) return false;
    bool marker = false;
    if (root.contains("codenameChart")) {
        const json& value = root["codenameChart"];
        marker = (value.is_boolean() && value.get<bool>()) ||
                 (value.is_string() && lower(value.get<std::string>()) == "true");
    }
    return marker || (root.contains("strumLines") && root["strumLines"].is_array());
}

Result<UniversalChart> parseCodenameChart(const std::string& text,
                                          const std::string& sourcePath,
                                          const CodenameChartDefaults& defaults,
                                          DiagnosticSink& sink) {
    json root;
    try {
        root = json::parse(text, nullptr, true, /*ignore_comments=*/true);
    } catch (const std::exception& error) {
        sink.error("FML-2410", std::string("chart nativo ilegible: ") + error.what(),
                   sourcePath);
        return Result<UniversalChart>::fail(error.what());
    }
    if (!root.is_object() || !root.contains("strumLines") ||
        !root["strumLines"].is_array()) {
        sink.error("FML-2411", "chart nativo sin strumLines", sourcePath);
        return Result<UniversalChart>::fail("sin strumLines");
    }

    UniversalChart out;
    out.format = ChartFormat::CodenameNative;
    out.sourcePath = sourcePath;
    out.songId = defaults.displayName.empty() ? defaults.songId : defaults.displayName;
    out.bpm = defaults.bpm > 0.0f ? defaults.bpm : 100.0f;
    out.beatsPerMeasure = std::max(1, defaults.beatsPerMeasure);
    out.stepsPerBeat = std::max(1, defaults.stepsPerBeat);
    out.needsVoices = defaults.needsVoices;
    out.speed = static_cast<float>(number(root, "scrollSpeed", 1.0));
    out.stage = stringValue(root, "stage");

    // Los charts pueden guardar overrides parciales de meta dentro del propio
    // JSON; Codename los mezcla encima de meta.json.
    if (root.contains("meta") && root["meta"].is_object()) {
        const json& meta = root["meta"];
        out.songId = stringValue(meta, "displayName",
                                 stringValue(meta, "name", out.songId));
        out.bpm = static_cast<float>(number(meta, "bpm", out.bpm));
        out.beatsPerMeasure = std::max(1, integer(meta, "beatsPerMeasure",
                                                  out.beatsPerMeasure));
        out.stepsPerBeat = std::max(1, integer(meta, "stepsPerBeat",
                                               out.stepsPerBeat));
        out.needsVoices = boolean(meta, "needsVoices", out.needsVoices);
    }
    if (out.bpm <= 0.0f) {
        sink.warn("FML-2401", "BPM invalido; se usa 100", sourcePath);
        out.bpm = 100.0f;
    }

    if (root.contains("noteTypes") && root["noteTypes"].is_array())
        for (const json& type : root["noteTypes"])
            if (type.is_string()) out.noteTypes.push_back(type.get<std::string>());

    // `bookmarks` es opcional en el formato (`ChartData.hx:16`): un chart sin
    // ellos no lleva la clave, y al guardar tampoco debe llevarla.
    if (root.contains("bookmarks") && root["bookmarks"].is_array()) {
        for (const json& item : root["bookmarks"]) {
            if (!item.is_object()) continue;
            ChartBookmark bookmark;
            bookmark.timeMs = number(item, "time", 0.0);
            if (item.contains("name") && item["name"].is_string())
                bookmark.name = item["name"].get<std::string>();
            if (item.contains("color") && item["color"].is_string())
                bookmark.color = item["color"].get<std::string>();
            bookmark.sourceJson = item.dump();
            out.bookmarks.push_back(std::move(bookmark));
        }
        std::stable_sort(out.bookmarks.begin(), out.bookmarks.end(),
            [](const ChartBookmark& a, const ChartBookmark& b) {
                return a.timeMs < b.timeMs;
            });
    }

    int skippedNotes = 0, invalidTypes = 0;
    const json& lines = root["strumLines"];
    out.strumLines.reserve(lines.size());
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const json& rawLine = lines[lineIndex];
        ChartStrumLine line;
        line.sourceIndex = static_cast<int>(lineIndex);
        if (rawLine.is_object()) {
            line.type = integer(rawLine, "type", 0);
            line.position = stringValue(rawLine, "position", defaultPositionForType(line.type));
            const std::string canonicalPosition = defaultPositionForType(line.type);
            if (lower(line.position) == canonicalPosition &&
                line.position != canonicalPosition) {
                sink.warn("FML-2416", "la posicion de strumline '" + line.position +
                          "' debe ser '" + canonicalPosition +
                          "': Codename distingue mayusculas y dejara el personaje en (0,0)",
                          sourcePath);
            }
            line.visible = boolean(rawLine, "visible", true);
            line.keyCount = std::max(1, integer(rawLine, "keyCount", 4));
            line.strumLinePos = line.type == 0 ? 0.25f : (line.type == 1 ? 0.75f : 0.5f);
            if (rawLine.contains("strumPos") && rawLine["strumPos"].is_array() &&
                rawLine["strumPos"].size() >= 2) {
                line.hasStrumPos = true;
                if (rawLine["strumPos"][0].is_number())
                    line.strumPosX = rawLine["strumPos"][0].get<float>();
                if (rawLine["strumPos"][1].is_number())
                    line.strumPosY = rawLine["strumPos"][1].get<float>();
            }
            if (rawLine.contains("strumLinePos") && rawLine["strumLinePos"].is_number()) {
                line.hasStrumLinePos = true;
                line.strumLinePos = rawLine["strumLinePos"].get<float>();
            }
            if (rawLine.contains("strumScale") && rawLine["strumScale"].is_number()) {
                line.hasStrumScale = true;
                line.strumScale = rawLine["strumScale"].get<float>();
            }
            if (rawLine.contains("strumSpacing") && rawLine["strumSpacing"].is_number()) {
                line.hasStrumSpacing = true;
                line.strumSpacing = rawLine["strumSpacing"].get<float>();
            }
            if (rawLine.contains("scrollSpeed") && rawLine["scrollSpeed"].is_number()) {
                line.hasScrollSpeed = true;
                line.scrollSpeed = rawLine["scrollSpeed"].get<float>();
            }
            line.vocalsSuffix = stringValue(rawLine, "vocalsSuffix");
            if (rawLine.contains("characters") && rawLine["characters"].is_array())
                for (const json& character : rawLine["characters"])
                    if (character.is_string()) line.characters.push_back(character.get<std::string>());

            // Las extensiones custom de una strumline deben sobrevivir aunque
            // el usuario la reordene. Las notas se guardan por separado.
            json config = rawLine;
            config.erase("notes");
            line.sourceJson = config.dump();
        } else {
            line.position = defaultPositionForType(line.type);
            line.strumLinePos = 0.25f;
            sink.warn("FML-2412", "strumline ilegible; se conserva su indice",
                      sourcePath);
        }
        out.strumLines.push_back(line); // el indice debe coincidir con eventos

        if (!rawLine.is_object() || !rawLine.contains("notes") ||
            !rawLine["notes"].is_array()) continue;
        for (const json& rawNote : rawLine["notes"]) {
            if (!rawNote.is_object() || !rawNote.contains("time") ||
                !rawNote.contains("id")) {
                ++skippedNotes;
                continue;
            }
            ChartNote note;
            note.sourceJson = rawNote.dump();
            note.timeMs = number(rawNote, "time", 0.0);
            note.lane = integer(rawNote, "id", 0);
            note.direction = ((note.lane % 4) + 4) % 4;
            note.sustainMs = std::max(0.0, number(rawNote, "sLen", 0.0));
            note.strumLine = static_cast<int>(lineIndex);
            note.singerStrumLine = integer(rawNote, "fmlSingerLine", -1);
            if (note.singerStrumLine < 0 || note.singerStrumLine >= static_cast<int>(lines.size()))
                note.singerStrumLine = -1;
            note.isPlayer = line.type == 1;

            if (rawNote.contains("type") && rawNote["type"].is_string()) {
                note.type = rawNote["type"].get<std::string>();
            } else {
                const int typeId = integer(rawNote, "type", 0);
                if (typeId > 0 && typeId <= static_cast<int>(out.noteTypes.size()))
                    note.type = out.noteTypes[static_cast<size_t>(typeId - 1)];
                else if (typeId != 0)
                    ++invalidTypes;
            }
            out.notes.push_back(std::move(note));
        }
    }

    // Campos legacy derivados para consumidores que aun muestran los tres slots.
    out.player1 = "none";
    out.player2 = "none";
    out.gfVersion = "none";
    for (const ChartStrumLine& line : out.strumLines) {
        if (line.characters.empty()) continue;
        const std::string pos = lower(line.position);
        if (pos == "boyfriend" || pos == "bf" || pos == "player")
            out.player1 = line.characters.front();
        else if (pos == "girlfriend" || pos == "gf")
            out.gfVersion = line.characters.front();
        else if (pos == "dad" || pos == "opponent" || pos == "enemy")
            out.player2 = line.characters.front();
    }

    if (root.contains("events"))
        parseEvents(root["events"], false, sourcePath, out.events, sink);

    std::stable_sort(out.notes.begin(), out.notes.end(),
                     [](const ChartNote& a, const ChartNote& b) {
                         return a.timeMs < b.timeMs;
                     });
    std::stable_sort(out.events.begin(), out.events.end(),
                     [](const ChartEvent& a, const ChartEvent& b) {
                         return a.timeMs < b.timeMs;
                     });

    if (skippedNotes > 0)
        sink.warn("FML-2413", std::to_string(skippedNotes) +
                  " notas nativas ilegibles se saltaron", sourcePath);
    if (invalidTypes > 0)
        sink.warn("FML-2414", std::to_string(invalidTypes) +
                  " notas referencian un noteType inexistente", sourcePath);
    if (out.notes.empty()) sink.warn("FML-2404", "el chart no tiene ni una nota", sourcePath);

    rebuildChartTiming(out, sink);
    return Result<UniversalChart>::ok(std::move(out));
}

Result<std::vector<ChartEvent>> parseCodenameEventsFile(const std::string& text,
                                                        const std::string& sourcePath,
                                                        DiagnosticSink& sink) {
    json root;
    try {
        root = json::parse(text, nullptr, true, /*ignore_comments=*/true);
    } catch (const std::exception& error) {
        sink.error("FML-2423", std::string("events.json ilegible: ") + error.what(),
                   sourcePath);
        return Result<std::vector<ChartEvent>>::fail(error.what());
    }
    const json* events = &root;
    if (root.is_object() && root.contains("events")) events = &root["events"];
    std::vector<ChartEvent> out;
    parseEvents(*events, true, sourcePath, out, sink);
    std::stable_sort(out.begin(), out.end(),
                     [](const ChartEvent& a, const ChartEvent& b) {
                         return a.timeMs < b.timeMs;
                     });
    return Result<std::vector<ChartEvent>>::ok(std::move(out));
}

void rebuildChartTiming(UniversalChart& chart, DiagnosticSink& sink) {
    chart.bpmChanges.clear();
    chart.bpmChanges.push_back({0.0, chart.bpm > 0.0f ? chart.bpm : 100.0f});
    bool warnedContinuous = false;
    for (const ChartEvent& event : chart.events) {
        // Los eventos sidecar importados son evidencia para el editor, no una
        // segunda fuente de timing del chart activo.
        if (!event.foreignSource.empty()) continue;
        if (event.name == "BPM Change" && !event.params.empty()) {
            try {
                const float bpm = std::stof(event.params[0]);
                if (bpm > 0.0f) chart.bpmChanges.push_back({event.timeMs, bpm});
            } catch (...) {
                sink.warn("FML-2424", "evento BPM Change con parametro invalido",
                          chart.sourcePath);
            }
        } else if (event.name == "Continuous BPM Change" && !warnedContinuous) {
            warnedContinuous = true;
            sink.warn("FML-2425", "Continuous BPM Change aun se aproxima con BPM constante",
                      chart.sourcePath);
        }
    }
    std::stable_sort(chart.bpmChanges.begin(), chart.bpmChanges.end(),
                     [](const BpmChange& a, const BpmChange& b) {
                         return a.timeMs < b.timeMs;
                     });
}

}  // namespace fml

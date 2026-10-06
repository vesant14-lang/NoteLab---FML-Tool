#include "LegacyChart.hpp"

#include "JsonRead.hpp"
#include "../../third_party/pugixml.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>

using json = nlohmann::json;

namespace fml {

// Lectura tolerante: en este formato es OBLIGATORIA. Los charts de Psych que
// circulan traen `"player3": null` y numeros como cadena, y `json::value()`
// lanza con eso. Ver JsonRead.hpp.
using namespace jsonread;

namespace {

void appendNoteType(std::vector<std::string>& noteTypes, const std::string& value) {
    if (value.empty() || value == "Default Note") return;
    if (std::find(noteTypes.begin(), noteTypes.end(), value) == noteTypes.end())
        noteTypes.push_back(value);
}

std::string declaredNoteType(const std::vector<std::string>& declared, int oneBasedId) {
    if (oneBasedId <= 0 || oneBasedId > static_cast<int>(declared.size())) return {};
    return declared[static_cast<size_t>(oneBasedId - 1)];
}

}  // namespace

void TimeMap::build(const UniversalChart& chart) {
    m_segments.clear();
    std::vector<BpmChange> changes = chart.bpmChanges;
    if (changes.empty()) changes.push_back({0.0, chart.bpm});
    std::sort(changes.begin(), changes.end(),
              [](const BpmChange& a, const BpmChange& b) { return a.timeMs < b.timeMs; });
    if (changes.front().timeMs > 0.0) changes.insert(changes.begin(), {0.0, chart.bpm});

    double beat = 0.0;
    for (size_t i = 0; i < changes.size(); ++i) {
        const float bpm = changes[i].bpm > 0.0f ? changes[i].bpm : 100.0f;
        m_segments.push_back({changes[i].timeMs, beat, bpm});
        if (i + 1 < changes.size()) {
            const double span = changes[i + 1].timeMs - changes[i].timeMs;
            beat += span / (60000.0 / bpm);
        }
    }
}

double TimeMap::beatAt(double ms) const {
    if (m_segments.empty()) return ms / 600.0;
    size_t i = 0;
    while (i + 1 < m_segments.size() && m_segments[i + 1].startMs <= ms) ++i;
    const Segment& s = m_segments[i];
    return s.startBeat + (ms - s.startMs) / (60000.0 / s.bpm);
}

double TimeMap::msAtBeat(double beat) const {
    if (m_segments.empty()) return beat * 600.0;
    size_t i = 0;
    while (i + 1 < m_segments.size() && m_segments[i + 1].startBeat <= beat) ++i;
    const Segment& s = m_segments[i];
    return s.startMs + (beat - s.startBeat) * (60000.0 / s.bpm);
}

float TimeMap::bpmAt(double ms) const {
    if (m_segments.empty()) return 100.0f;
    size_t i = 0;
    while (i + 1 < m_segments.size() && m_segments[i + 1].startMs <= ms) ++i;
    return m_segments[i].bpm;
}

// -----------------------------------------------------------------------------
Result<UniversalChart> parseLegacyChart(const std::string& text,
                                        const std::string& sourcePath,
                                        DiagnosticSink&    sink) {
    UniversalChart out;
    out.format = ChartFormat::Legacy;
    out.sourcePath = sourcePath;

    json root;
    try {
        root = json::parse(text, nullptr, true, /*ignore_comments=*/true);
    } catch (const std::exception& e) {
        sink.error("FML-2400", std::string("chart ilegible: ") + e.what(), sourcePath);
        return Result<UniversalChart>::fail(e.what());
    }

    // Codename 1.x tambien tiene un formato propio: strumLines + events. Es un
    // chart valido para el motor, pero NO es equivalente a song.notes y perderia
    // strumlines/personajes/eventos si se fingiera que es legacy.
    if (root.is_object() &&
        (root.contains("codenameChart") || root.contains("strumLines"))) {
        size_t lines = 0, notes = 0, events = 0;
        if (root.contains("strumLines") && root["strumLines"].is_array()) {
            lines = root["strumLines"].size();
            for (const auto& line : root["strumLines"])
                if (line.is_object() && line.contains("notes") && line["notes"].is_array())
                    notes += line["notes"].size();
        }
        if (root.contains("events") && root["events"].is_array())
            events = root["events"].size();
        sink.error("FML-2405",
                   "chart nativo de Codename detectado (" + std::to_string(lines) +
                   " strumlines, " + std::to_string(notes) + " notas, " +
                   std::to_string(events) +
                   " eventos); debe enviarse al parser nativo, no al legacy",
                   sourcePath);
        return Result<UniversalChart>::fail("parser incorrecto para chart nativo");
    }

    // El chart suele venir envuelto en {"song": {...}}, pero algunos mods lo
    // guardan plano. Se aceptan los dos.
    const json* song = &root;
    if (root.contains("song") && root["song"].is_object()) song = &root["song"];

    out.songId      = stringValue(*song, "song");
    out.bpm         = static_cast<float>(number(*song, "bpm", 100.0));
    out.speed       = static_cast<float>(number(*song, "speed", 1.0));
    out.needsVoices = boolean(*song, "needsVoices", true);
    out.player1     = stringValue(*song, "player1", "bf");
    out.player2     = stringValue(*song, "player2", "dad");
    // Psych/FNF legacy charts are not consistent about this slot.  Older
    // exports use `player3`, newer ones usually use `gfVersion`, while the
    // Week 7 charts bundled by Codename use simply `gf` (for example Guns).
    // Ignoring that last spelling silently replaced gf-tankmen with plain gf.
    //
    // El campo tambien puede existir con valor `null` en charts legacy reales.
    // Chronicles lo hacen—, asi que la lectura tiene que ser por tipo: `null`
    // se trata como "no declarado" y se sigue a la siguiente forma del nombre.
    out.gfVersion   = stringValue(*song, "gfVersion",
                      stringValue(*song, "gf",
                      stringValue(*song, "player3", "gf")));
    out.stage       = stringValue(*song, "stage");
    // Psych 1.0 guarda `format: "psych_v1"` (o "psych_v1_convert") y entonces
    // los carriles son absolutos: 0..3 del jugador, 4..7 del rival, y
    // mustHitSection solo mueve la camara (PlayState.hx:1355; Song.hx:174-180
    // convierte solo lo que no empieza por "psych_v1"). Codename hace lo mismo
    // (Chart.hx:68 -> PsychParser.hx:27-29). En ese formato note[1] no lleva
    // tipos empacados. Se mira donde lo miran los motores: dentro de `song`.
    const bool absoluteLanes = stringValue(*song, "format").rfind("psych_v1", 0) == 0;
    int keyCount = std::clamp(integer(*song, "keyCount", 4), 1, 18);
    // Some Psych forks store zero-based mania (3=4 keys, 8=9). Accept that
    // dialect only when the actual lane range confirms it; never treat packed
    // legacy note types as extra keys or guess from a song/mod name.
    const int mania = integer(*song, "mania", -1);
    if (!song->contains("keyCount") && mania >= 0 && mania < 18 && !song->contains("noteTypes") &&
        song->contains("notes") && (*song)["notes"].is_array()) {
        int highest = -1;
        for (const auto& section : (*song)["notes"]) if (section.is_object() && section.contains("sectionNotes") && section["sectionNotes"].is_array())
            for (const auto& n : section["sectionNotes"]) if (n.is_array() && n.size() > 1 && n[1].is_number())
                highest = std::max(highest, static_cast<int>(n[1].get<double>()));
        if (highest == (mania + 1) * 2 - 1) keyCount = mania + 1;
        else if (mania != 3) sink.warn("FML-2405", "mania ambiguo: se conservan 4 teclas; revisa el numero de columnas", sourcePath);
    }
    // FNFLegacyParser usa este arreglo como tabla 1-based tanto para note[3]
    // numerico como para el tipo empacado en floor(note[1] / 8).
    std::vector<std::string> declaredNoteTypes;
    if (song->contains("noteTypes") && (*song)["noteTypes"].is_array()) {
        for (const json& rawType : (*song)["noteTypes"]) {
            if (!rawType.is_string()) continue;
            const std::string type = rawType.get<std::string>();
            appendNoteType(declaredNoteTypes, type);
            appendNoteType(out.noteTypes, type);
        }
    }
    if (out.bpm <= 0.0f) {
        sink.warn("FML-2401", "BPM invalido; se usa 100", sourcePath);
        out.bpm = 100.0f;
    }

    out.bpmChanges.push_back({0.0, out.bpm});

    std::string player2Lower = out.player2;
    std::transform(player2Lower.begin(), player2Lower.end(), player2Lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool player2IsGf = player2Lower.size() >= 2 &&
                             player2Lower.compare(0, 2, "gf") == 0;
    ChartStrumLine opponent;
    opponent.type = 0;
    opponent.keyCount = keyCount;
    opponent.position = player2IsGf ? "girlfriend" : "dad";
    opponent.characters.push_back(out.player2);
    out.strumLines.push_back(std::move(opponent));
    ChartStrumLine player;
    player.type = 1;
    player.keyCount = keyCount;
    player.position = "boyfriend";
    player.characters.push_back(out.player1);
    out.strumLines.push_back(std::move(player));
    if (!player2IsGf && out.gfVersion != "none") {
        ChartStrumLine girlfriend;
        girlfriend.type = 2;
        girlfriend.position = "girlfriend";
        girlfriend.visible = false;
        girlfriend.characters.push_back(out.gfVersion);
        out.strumLines.push_back(std::move(girlfriend));
    }

    if (!song->contains("notes") || !(*song)["notes"].is_array()) {
        sink.error("FML-2402", "el chart no tiene secciones", sourcePath);
        return Result<UniversalChart>::fail("sin notas");
    }

    float  curBpm = out.bpm;
    double sectionStartMs = 0.0;
    int    skipped = 0;
    int    cameraLine = 0;
    bool   altAnimations = false;

    for (const json& sec : (*song)["notes"]) {
        if (!sec.is_object()) continue;

        const bool mustHit = boolean(sec, "mustHitSection", false);
        const int sectionCamera = boolean(sec,"gfSection",false) && out.strumLines.size() > 2 ? 2 : (mustHit ? 1 : 0);
        if (sectionCamera != cameraLine) {
            cameraLine = sectionCamera;
            ChartEvent event;
            event.timeMs = sectionStartMs;
            event.name = "Camera Movement";
            event.params.push_back(std::to_string(cameraLine));
            event.paramsJson = event.params;
            out.events.push_back(std::move(event));
        }

        const bool sectionAlt = boolean(sec, "altAnim", false);
        if (sectionAlt != altAnimations) {
            altAnimations = sectionAlt;
            ChartEvent event;
            event.timeMs = sectionStartMs;
            event.name = "Alt Animation Toggle";
            event.params = {altAnimations ? "true" : "false", "false", "0"};
            event.paramsJson = event.params;
            out.events.push_back(std::move(event));
        }

        // Cambio de BPM por seccion.
        if (boolean(sec, "changeBPM", false)) {
            const float b = static_cast<float>(number(sec, "bpm", curBpm));
            if (b > 0.0f && b != curBpm) {
                curBpm = b;
                out.bpmChanges.push_back({sectionStartMs, curBpm});
                ChartEvent event;
                event.timeMs = sectionStartMs;
                event.name = "BPM Change";
                event.params.push_back(std::to_string(curBpm));
                event.paramsJson = event.params;
                out.events.push_back(std::move(event));
            }
        }

        const int  steps   = integer(sec, "lengthInSteps", 16);

        if (sec.contains("sectionNotes") && sec["sectionNotes"].is_array()) {
            for (const json& n : sec["sectionNotes"]) {
                if (!n.is_array() || n.size() < 2) { ++skipped; continue; }
                ChartNote note;
                try {
                    note.timeMs    = n[0].get<double>();
                    const int encodedDirection = n[1].get<int>();
                    // Codename conserva tres datos en note[1]: direccion
                    // visual (mod 4), inversion de cantante (mod 8 >= 4) y
                    // tipo legacy (division entera entre 8). Comparar el valor
                    // completo con 3 hacia que una nota tipo 2 cambiara de lado.
                    if (encodedDirection < 0) { ++skipped; continue; }
                    const int noteData = encodedDirection % (keyCount * 2);
                    const int encodedTypeId = absoluteLanes ? 0 : encodedDirection / (keyCount * 2);
                    note.lane      = noteData % keyCount;
                    note.sustainMs = (n.size() > 2 && n[2].is_number()) ? n[2].get<double>() : 0.0;

                    if (n.size() > 3 && n[3].is_string()) {
                        note.type = n[3].get<std::string>();
                    } else if (n.size() > 3 && n[3].is_number()) {
                        note.type = declaredNoteType(declaredNoteTypes,
                            static_cast<int>(n[3].get<double>()));
                    } else {
                        note.type = declaredNoteType(declaredNoteTypes, encodedTypeId);
                    }
                    appendNoteType(out.noteTypes, note.type);

                    // Regla clasica del motor: la seccion dice quien canta, y una
                    // direccion 4..7 la invierte. El tipo empacado no participa.
                    // En psych_v1 decide el valor tal cual: `songNotes[1] < 4`.
                    bool player = mustHit;
                    if (noteData >= keyCount) player = !player;
                    if (absoluteLanes) player = encodedDirection < keyCount;
                    note.isPlayer  = player;
                    note.strumLine = player ? 1 : 0;
                    note.direction = note.lane % 4;
                    if ((note.type == "GF Sing" || (boolean(sec, "gfSection", false) && player == mustHit)) &&
                        out.strumLines.size() > 2) note.singerStrumLine = 2;
                } catch (...) { ++skipped; continue; }
                if (note.sustainMs < 0.0) note.sustainMs = 0.0;
                out.notes.push_back(std::move(note));
            }
        }

        // Avance de la seccion: `lengthInSteps` pasos al BPM vigente.
        sectionStartMs += steps * (60000.0 / curBpm / 4.0);
    }

    if (skipped > 0)
        sink.warn("FML-2403", std::to_string(skipped) + " notas ilegibles se saltaron",
                  sourcePath);

    std::stable_sort(out.notes.begin(), out.notes.end(),
                     [](const ChartNote& a, const ChartNote& b) {
                         return a.timeMs < b.timeMs;
                     });
    std::stable_sort(out.events.begin(), out.events.end(),
                     [](const ChartEvent& a, const ChartEvent& b) {
                         return a.timeMs < b.timeMs;
                     });

    if (out.notes.empty()) sink.warn("FML-2404", "el chart no tiene ni una nota", sourcePath);
    return Result<UniversalChart>::ok(std::move(out));
}


std::optional<ForeignEventFile> parseForeignEventFile(const std::string& text) {
    json root;
    try {
        root = json::parse(text, nullptr, true, /*ignore_comments=*/true);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    const json* events = nullptr;
    if (root.is_array()) {
        events = &root;
    } else if (root.is_object()) {
        const auto direct = root.find("events");
        if (direct != root.end() && direct->is_array()) events = &*direct;
        if (!events) {
            const auto song = root.find("song");
            if (song != root.end() && song->is_object()) {
                const auto nested = song->find("events");
                if (nested != song->end() && nested->is_array()) events = &*nested;
            }
        }
    }
    if (!events || events->empty()) return std::nullopt;

    auto numeric = [](const json& value, double& out) {
        if (value.is_number()) {
            out = value.get<double>();
            return std::isfinite(out);
        }
        if (!value.is_string()) return false;
        const std::string raw = value.get<std::string>();
        if (raw.empty()) return false;
        char* end = nullptr;
        out = std::strtod(raw.c_str(), &end);
        return end != raw.c_str() && end && *end == '\0' && std::isfinite(out);
    };
    auto appendParam = [](ChartEvent& event, const json& value) {
        event.params.push_back(value.is_string() ? value.get<std::string>()
                                                  : value.dump());
        event.paramsJson.push_back(value.dump());
    };
    auto finish = [](ChartEvent& event, const json& source, int ordinal) {
        if (event.name.empty() || !std::isfinite(event.timeMs)) return false;
        event.sourceIndex = ordinal;
        event.sourceJson = source.dump();
        return true;
    };

    ForeignEventFile parsed;
    int ordinal = 0;
    const json& first = (*events)[0];

    // Forma posicional por evento: [nombre, milisegundos, ...parametros].
    // Se reconoce por tipos, no por nombre de archivo ni por motor.
    double firstTime = 0.0;
    if (first.is_array() && first.size() >= 2 && first[0].is_string() &&
        numeric(first[1], firstTime)) {
        parsed.dialect = "name-time-positional";
        for (const json& entry : *events) {
            double time = 0.0;
            if (!entry.is_array() || entry.size() < 2 || !entry[0].is_string() ||
                !numeric(entry[1], time)) return std::nullopt;
            ChartEvent event;
            event.name = entry[0].get<std::string>();
            event.timeMs = time;
            for (std::size_t i = 2; i < entry.size(); ++i) appendParam(event, entry[i]);
            if (!finish(event, entry, ordinal++)) return std::nullopt;
            parsed.events.push_back(std::move(event));
        }
    // Forma agrupada por instante: [milisegundos, [[nombre, ...params], ...]].
    // Es comun en exports de otros motores y puede contener varios eventos al
    // mismo tiempo. Exigir que TODO el documento encaje evita tomar cualquier
    // array de datos como si fuera una pista de eventos.
    } else if (first.is_array() && first.size() >= 2 &&
               numeric(first[0], firstTime) && first[1].is_array()) {
        parsed.dialect = "time-grouped-events";
        for (const json& group : *events) {
            double time = 0.0;
            if (!group.is_array() || group.size() < 2 || !numeric(group[0], time) ||
                !group[1].is_array() || group[1].empty()) return std::nullopt;
            for (const json& entry : group[1]) {
                if (!entry.is_array() || entry.empty() || !entry[0].is_string())
                    return std::nullopt;
                ChartEvent event;
                event.name = entry[0].get<std::string>();
                event.timeMs = time;
                for (std::size_t i = 1; i < entry.size(); ++i) appendParam(event, entry[i]);
                if (!finish(event, entry, ordinal++)) return std::nullopt;
                parsed.events.push_back(std::move(event));
            }
        }
    // Forma nominal: {name/event, time/timeMs/strumTime, params/values/...}.
    // Solo se acepta si cada fila tiene nombre y tiempo inequívocos.
    } else if (first.is_object()) {
        parsed.dialect = "named-event-objects";
        for (const json& entry : *events) {
            if (!entry.is_object()) return std::nullopt;
            const json* name = nullptr;
            for (const char* key : {"name", "event", "eventName"}) {
                const auto at = entry.find(key);
                if (at != entry.end() && at->is_string()) { name = &*at; break; }
            }
            const json* timeValue = nullptr;
            for (const char* key : {"time", "timeMs", "strumTime", "timestamp"}) {
                const auto at = entry.find(key);
                if (at != entry.end()) { timeValue = &*at; break; }
            }
            double time = 0.0;
            if (!name || !timeValue || !numeric(*timeValue, time)) return std::nullopt;
            ChartEvent event;
            event.name = name->get<std::string>();
            event.timeMs = time;
            const json* params = nullptr;
            for (const char* key : {"params", "values", "parameters", "args"}) {
                const auto at = entry.find(key);
                if (at != entry.end()) { params = &*at; break; }
            }
            if (params) {
                if (params->is_array()) {
                    for (const json& value : *params) appendParam(event, value);
                } else {
                    appendParam(event, *params);
                }
            } else {
                for (const char* key : {"value1", "value2"}) {
                    const auto at = entry.find(key);
                    if (at != entry.end()) appendParam(event, *at);
                }
            }
            if (!finish(event, entry, ordinal++)) return std::nullopt;
            parsed.events.push_back(std::move(event));
        }
    } else {
        return std::nullopt;
    }

    if (parsed.events.empty()) return std::nullopt;
    return parsed;
}

std::optional<ForeignEventFile> parseForeignXmlEventFile(
        const std::string& text, const TimeMap& map) {
    pugi::xml_document document;
    const pugi::xml_parse_result loaded = document.load_buffer(
        text.data(), text.size(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!loaded) return std::nullopt;

    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    auto numeric = [](const char* raw, double& value) {
        if (!raw || !*raw) return false;
        char* end = nullptr;
        value = std::strtod(raw, &end);
        return end != raw && end && *end == '\0' && std::isfinite(value);
    };
    auto initializationContainer = [&](pugi::xml_node node) {
        for (pugi::xml_node parent = node.parent(); parent; parent = parent.parent()) {
            const std::string name = lower(parent.name());
            if (name == "init" || name == "initialize" || name == "setup" ||
                name == "bootstrap" || name == "defaults") return true;
        }
        return false;
    };
    struct Candidate {
        ChartEvent event;
        std::string unit;
    };
    std::vector<Candidate> candidates;
    bool timelineContainer = false;
    int ordinal = 0;
    std::function<void(pugi::xml_node)> visit = [&](pugi::xml_node node) {
        for (pugi::xml_node child : node.children()) visit(child);
        if (node.type() != pugi::node_element) return;
        const std::string nodeName = lower(node.name());
        if (nodeName == "events" || nodeName == "timeline" ||
            nodeName == "modchart" || nodeName == "effects" ||
            nodeName == "keyframes") timelineContainer = true;

        pugi::xml_attribute identity;
        for (const char* key : {"type", "event", "eventName", "action", "name"}) {
            identity = node.attribute(key);
            if (identity && *identity.value()) break;
        }
        if (!identity) return;

        pugi::xml_attribute timeAttribute;
        std::string unit;
        for (const auto& option : std::vector<std::pair<const char*, const char*>>{
                 {"timeMs", "ms"}, {"milliseconds", "ms"}, {"timestamp", "ms"},
                 {"beat", "beat"}, {"step", "step"}, {"time", "ms"}}) {
            const pugi::xml_attribute at = node.attribute(option.first);
            double ignored = 0.0;
            if (at && numeric(at.value(), ignored)) {
                timeAttribute = at;
                unit = option.second;
                break;
            }
        }
        const bool init = initializationContainer(node);
        if (!timeAttribute && !init) return;

        double sourceTime = 0.0;
        if (timeAttribute && !numeric(timeAttribute.value(), sourceTime)) return;
        ChartEvent event;
        event.name = identity.value();
        event.timeMs = unit == "step" ? map.msAtBeat(sourceTime / 4.0)
                     : unit == "beat" ? map.msAtBeat(sourceTime)
                                        : sourceTime;
        event.global = init;
        event.sourceIndex = ordinal++;

        json source = json::object();
        source["node"] = node.name();
        source["attributes"] = json::object();
        for (pugi::xml_attribute attribute : node.attributes()) {
            source["attributes"][attribute.name()] = attribute.value();
            if (std::string(attribute.name()) == identity.name() ||
                (timeAttribute && std::string(attribute.name()) ==
                                      timeAttribute.name())) continue;
            const std::string parameter = std::string(attribute.name()) + "=" +
                                          attribute.value();
            event.params.push_back(parameter);
            event.paramsJson.push_back(json(parameter).dump());
        }
        event.sourceJson = source.dump();
        candidates.push_back({std::move(event), unit.empty() ? "init" : unit});
    };
    visit(document);
    if (candidates.empty() || (candidates.size() < 2 && !timelineContainer))
        return std::nullopt;

    ForeignEventFile parsed;
    parsed.noteModchart = document.document_element().attribute("noteModchart")
        .as_bool(false);
    std::set<std::string> units;
    parsed.events.reserve(candidates.size());
    for (Candidate& candidate : candidates) {
        units.insert(candidate.unit);
        parsed.events.push_back(std::move(candidate.event));
    }
    parsed.dialect = "xml-attribute-timeline";
    if (units.size() == 1) parsed.dialect += "-" + *units.begin();
    else parsed.dialect += "-mixed-time";
    std::stable_sort(parsed.events.begin(), parsed.events.end(),
        [](const ChartEvent& a, const ChartEvent& b) {
            return a.timeMs == b.timeMs ? a.sourceIndex < b.sourceIndex
                                        : a.timeMs < b.timeMs;
        });
    return parsed;
}

}  // namespace fml

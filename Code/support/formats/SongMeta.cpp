#include "SongMeta.hpp"

#include "../../third_party/json.hpp"

#include <algorithm>
#include <cctype>

// ordered_json y no json: `json` ordena las claves alfabeticamente al
// serializar, asi que reescribir un meta.json existente lo dejaria con las
// claves barajadas. Un diff de 8 lineas donde solo cambio una es exactamente lo
// que DESIGN §1.4 dice que no puede pasar.
using ojson = nlohmann::ordered_json;

namespace fml {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    size_t a = 0, b = value.size();
    while (a < b && std::isspace(static_cast<unsigned char>(value[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(value[b - 1]))) --b;
    return value.substr(a, b - a);
}

// El motor acepta el color como string web ("#941653") o como entero. Se
// normaliza a string para no perder el formato que el modder escribio.
std::string colorToString(const ojson& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number()) {
        unsigned int rgb = static_cast<unsigned int>(value.get<long long>()) & 0xFFFFFFu;
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%06X", rgb);
        return buffer;
    }
    return {};
}

// Los meta.json del corpus vienen en CRLF (medido: 23 de 23 en Codename base).
// nlohmann emite siempre LF, asi que reescribir uno sin cambiar nada generaba un
// diff de archivo entero. Es exactamente el fallo que mato el round-trip de XML
// (DESIGN §1.4) repetido en JSON: la serializacion normaliza cosas invisibles.
std::string matchLineEndings(std::string text, const std::string& pristine) {
    const bool pristineIsCrlf = pristine.find("\r\n") != std::string::npos;
    if (!pristineIsCrlf) return text;

    std::string out;
    out.reserve(text.size() + text.size() / 16);
    for (char c : text) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

template <typename T>
void setIfPresentOrChanged(ojson& doc, const char* key, const T& value, const T& engineDefault) {
    // Solo se escribe si ya estaba (se actualiza en su sitio) o si difiere del
    // default. Asi un meta.json minimo no se hincha con quince campos que el
    // motor ya sabe rellenar solo.
    if (doc.contains(key) || !(value == engineDefault)) doc[key] = value;
}

}  // namespace

void applyEngineDifficultyOrder(std::vector<std::string>& difficulties) {
    // Chart.hx:146-153 — solo reordena cuando hay EXACTAMENTE tres y las tres
    // son easy/normal/hard. Con cualquier otra combinacion respeta el orden que
    // venga, porque las dificultades son cadenas libres (CORPUS §19).
    if (difficulties.size() != 3) return;
    std::vector<std::string> sorted(3);
    int found = 0;
    for (const auto& d : difficulties) {
        const std::string key = lower(d);
        if      (key == "easy")   { sorted[0] = d; ++found; }
        else if (key == "normal") { sorted[1] = d; ++found; }
        else if (key == "hard")   { sorted[2] = d; ++found; }
    }
    if (found == 3) difficulties = sorted;
}

SongMeta parseSongMeta(const std::string& text, const std::string& songId,
                       const std::string& sourcePath, DiagnosticSink& sink) {
    SongMeta meta;
    meta.name        = songId;
    meta.displayName = songId;
    meta.sourcePath  = sourcePath;
    meta.sourceText  = text;

    if (text.empty()) return meta;

    ojson doc;
    try {
        doc = ojson::parse(text, nullptr, true, /*ignore_comments=*/true);
    } catch (const std::exception& ex) {
        sink.error("FML-3020", std::string("meta.json invalido: ") + ex.what(), sourcePath);
        return meta;
    }
    if (!doc.is_object()) {
        sink.error("FML-3020", "meta.json no es un objeto", sourcePath);
        return meta;
    }

    if (doc.contains("displayName") && doc["displayName"].is_string())
        meta.displayName = doc["displayName"].get<std::string>();
    if (doc.contains("bpm") && doc["bpm"].is_number())
        meta.bpm = doc["bpm"].get<float>();
    if (doc.contains("beatsPerMeasure") && doc["beatsPerMeasure"].is_number())
        meta.beatsPerMeasure = doc["beatsPerMeasure"].get<int>();
    if (doc.contains("stepsPerBeat") && doc["stepsPerBeat"].is_number())
        meta.stepsPerBeat = doc["stepsPerBeat"].get<int>();
    if (doc.contains("icon") && doc["icon"].is_string())
        meta.icon = doc["icon"].get<std::string>();
    if (doc.contains("color"))
        meta.color = colorToString(doc["color"]);
    if (doc.contains("coopAllowed") && doc["coopAllowed"].is_boolean())
        meta.coopAllowed = doc["coopAllowed"].get<bool>();
    if (doc.contains("opponentModeAllowed") && doc["opponentModeAllowed"].is_boolean())
        meta.opponentModeAllowed = doc["opponentModeAllowed"].get<bool>();
    if (doc.contains("needsVoices") && doc["needsVoices"].is_boolean())
        meta.needsVoices = doc["needsVoices"].get<bool>();
    if (doc.contains("instSuffix") && doc["instSuffix"].is_string())
        meta.instSuffix = doc["instSuffix"].get<std::string>();
    if (doc.contains("vocalsSuffix") && doc["vocalsSuffix"].is_string())
        meta.vocalsSuffix = doc["vocalsSuffix"].get<std::string>();

    if (doc.contains("difficulties") && doc["difficulties"].is_array()) {
        for (const auto& d : doc["difficulties"])
            if (d.is_string()) {
                const std::string name = trim(d.get<std::string>());
                if (!name.empty()) meta.difficulties.push_back(name);
            }
    }
    if (doc.contains("variants") && doc["variants"].is_array()) {
        for (const auto& v : doc["variants"])
            if (v.is_string()) meta.variants.push_back(v.get<std::string>());
    }

    if (meta.displayName.empty()) meta.displayName = songId;
    return meta;
}

std::string serializeSongMeta(const SongMeta& meta, const std::string& pristineText) {
    ojson doc = ojson::object();
    if (!pristineText.empty()) {
        try {
            ojson parsed = ojson::parse(pristineText, nullptr, true, /*ignore_comments=*/true);
            if (parsed.is_object()) doc = std::move(parsed);
        } catch (const std::exception&) {
            // Si el original no parsea se escribe uno limpio. Perder el texto
            // roto es aceptable: la copia de seguridad de SafeWriter lo guarda.
        }
    }

    using namespace codename_defaults;
    setIfPresentOrChanged<std::string>(doc, "displayName", meta.displayName, meta.name);
    setIfPresentOrChanged<double>(doc, "bpm", meta.bpm, kBpm);
    setIfPresentOrChanged<int>(doc, "beatsPerMeasure", meta.beatsPerMeasure, kBeatsPerMeasure);
    setIfPresentOrChanged<int>(doc, "stepsPerBeat", meta.stepsPerBeat, kStepsPerBeat);
    setIfPresentOrChanged<std::string>(doc, "icon", meta.icon, kHealthIcon);
    setIfPresentOrChanged<std::string>(doc, "color", meta.color, kColor);
    setIfPresentOrChanged<bool>(doc, "coopAllowed", meta.coopAllowed, kCoopAllowed);
    setIfPresentOrChanged<bool>(doc, "opponentModeAllowed", meta.opponentModeAllowed, kOpponentMode);
    setIfPresentOrChanged<bool>(doc, "needsVoices", meta.needsVoices, true);

    // filterMetaForSaving (Chart.hx:355): los sufijos vacios se BORRAN, no se
    // escriben como "".
    if (meta.instSuffix.empty())   doc.erase("instSuffix");
    else                           doc["instSuffix"] = meta.instSuffix;
    if (meta.vocalsSuffix.empty()) doc.erase("vocalsSuffix");
    else                           doc["vocalsSuffix"] = meta.vocalsSuffix;

    // Si las dificultades se dedujeron de charts/, no se escriben: el motor las
    // vuelve a deducir igual y asi el archivo no adquiere una lista que hay que
    // mantener a mano cada vez que se añade un chart.
    if (!meta.difficultiesInferred && !meta.difficulties.empty())
        doc["difficulties"] = meta.difficulties;
    else if (meta.difficulties.empty())
        doc.erase("difficulties");

    if (meta.variants.empty()) doc.erase("variants");
    else                       doc["variants"] = meta.variants;

    // Campos que el motor borra siempre al guardar.
    doc.erase("name");
    doc.erase("variant");
    doc.erase("metas");
    doc.erase("parsedColor");

    // Tabulador: Flags.JSON_PRETTY_PRINT = "\t".
    std::string text = doc.dump(1, '\t', /*ensure_ascii=*/false,
                                ojson::error_handler_t::replace);
    text = matchLineEndings(std::move(text), pristineText);

    // Y el salto final, que tampoco lo pone el serializador.
    if (!pristineText.empty() && pristineText.back() == '\n') {
        if (pristineText.size() >= 2 &&
            pristineText.compare(pristineText.size() - 2, 2, "\r\n") == 0)
            text += "\r\n";
        else
            text += "\n";
    }
    return text;
}

}  // namespace fml

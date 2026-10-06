// fml_formats — Lectura TOLERANTE de JSON.
//
// Por que existe este archivo: `json::value("clave", defecto)` de nlohmann
// LANZA cuando el campo existe pero es de otro tipo. Y en el corpus real eso no
// es un caso raro:
//
//     "player3": null        <- forma valida observada en charts legacy
//     "bpm": "230"           <- numeros escritos como cadena
//
// Un `null` ahi tumbaba el proceso entero (json.exception.type_error.302), que
// es exactamente lo que DESIGN §1.3 prohibe: un archivo raro DEGRADA, no mata.
// Estas funciones nunca lanzan: si el tipo no encaja, devuelven el valor por
// defecto, igual que haria el motor al leer un campo que no entiende.
//
// CORPUS §4 dice lo mismo desde el otro lado: los mods amplian el esquema por su
// cuenta, asi que el parser tiene que ser tolerante por diseño, no por parche.
#pragma once

#include "../../third_party/json.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace fml {
namespace jsonread {

using json = nlohmann::json;

inline std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Numero. Acepta tambien el numero escrito como cadena, que aparece en mods.
inline double number(const json& object, const char* key, double fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const json& value = object[key];
    if (value.is_number()) return value.get<double>();
    if (value.is_string()) {
        try { return std::stod(value.get<std::string>()); } catch (...) {}
    }
    return fallback;
}

inline int integer(const json& object, const char* key, int fallback) {
    return static_cast<int>(number(object, key, static_cast<double>(fallback)));
}

inline bool boolean(const json& object, const char* key, bool fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const json& value = object[key];
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<int>() != 0;
    if (value.is_string()) {
        const std::string text = lower(value.get<std::string>());
        if (text == "true"  || text == "1") return true;
        if (text == "false" || text == "0") return false;
    }
    return fallback;
}

inline std::string stringValue(const json& object, const char* key,
                               const std::string& fallback = {}) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const json& value = object[key];
    return value.is_string() ? value.get<std::string>() : fallback;
}

}  // namespace jsonread
}  // namespace fml

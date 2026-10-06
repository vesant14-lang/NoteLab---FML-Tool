#pragma once
// Texto y rutas que vienen de fuera del programa, sin excepciones.
//
// En Windows, `std::getenv` y el `argv` de `main` llegan en la pagina de
// codigos ANSI (1252 en un Windows en espanol), no en UTF-8. `fs::u8path`
// sobre esos bytes LANZA con cualquier letra no ASCII: un usuario llamado
// "José" hacia que FML y Funkin Atlas se cerraran al arrancar (su
// %APPDATA% es C:\Users\José\...), y arrastrar una carpeta con una ñ al
// .exe hacia lo mismo. Y un mod guardado en ANSI (una "ñ" en un XML) llevaba
// bytes que no son UTF-8 hasta `json::dump`, que tambien lanza.
//
// Aqui: variables de entorno como rutas UTF-16 de verdad, conversion de
// UTF-8 a ruta que nunca lanza, y texto ANSI (Windows-1252) a UTF-8.

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace fml {

// UTF-8 bien formado: sin continuaciones sueltas, sin formas largas, sin
// sustitutos y sin pasar de U+10FFFF.
inline bool isValidUtf8(std::string_view text) {
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) { ++i; continue; }
        size_t length = 0;
        unsigned int code = 0;
        if (c >= 0xC2 && c <= 0xDF) { length = 2; code = c & 0x1Fu; }
        else if (c >= 0xE0 && c <= 0xEF) { length = 3; code = c & 0x0Fu; }
        else if (c >= 0xF0 && c <= 0xF4) { length = 4; code = c & 0x07u; }
        else return false;
        if (i + length > n) return false;
        for (size_t k = 1; k < length; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u) return false;
            code = (code << 6) | (next & 0x3Fu);
        }
        if ((length == 3 && (code < 0x800 || (code >= 0xD800 && code <= 0xDFFF))) ||
            (length == 4 && (code < 0x10000 || code > 0x10FFFF)))
            return false;
        i += length;
    }
    return true;
}

inline void appendUtf8(std::string& out, unsigned int code) {
    if (code < 0x80) out.push_back(static_cast<char>(code));
    else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// Windows-1252 a UTF-8. Es lo que guarda el Bloc de notas en "ANSI" en un
// Windows occidental; 0x80-0x9F son comillas, guiones, el euro... y no los
// controles de Latin-1. Los cinco huecos sin asignar quedan como U+FFFD.
inline std::string windows1252ToUtf8(std::string_view text) {
    static constexpr unsigned short high[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80) out.push_back(ch);
        else if (c < 0xA0) appendUtf8(out, high[c - 0x80]);
        else appendUtf8(out, c);
    }
    return out;
}

// CP437 a UTF-8: la codificacion de los nombres de un ZIP que no llevan la
// marca UTF-8 (bit 11), que es lo que escribe la compresion clasica de
// Windows. Las letras espanolas coinciden con CP850 (a con tilde 0xA0,
// n con virgulilla 0xA4...).
inline std::string cp437ToUtf8(std::string_view text) {
    static constexpr unsigned short high[128] = {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
        0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
        0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
        0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
        0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
        0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
        0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
        0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
        0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
        0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
        0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};
    std::string out;
    out.reserve(text.size() + text.size() / 4);
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80) out.push_back(ch);
        else appendUtf8(out, high[c - 0x80]);
    }
    return out;
}

// El nombre de una entrada ZIP en UTF-8: tal cual si ya lo es (lleve o no la
// marca del bit 11); si no, CP437 (APPNOTE.TXT, apendice D). Un ZIP que dice
// UTF-8 y no lo es tambien acaba en una ruta valida, aunque rara.
inline std::string zipEntryNameToUtf8(const std::string& raw) {
    return isValidUtf8(raw) ? raw : cp437ToUtf8(raw);
}

// El texto tal cual si ya es UTF-8; si no, se lee como Windows-1252. Un
// archivo suele estar entero en una codificacion o en la otra.
inline std::string ensureUtf8(std::string text) {
    if (isValidUtf8(text)) return text;
    return windows1252ToUtf8(text);
}

// Ruta desde texto UTF-8 que NUNCA lanza: si los bytes no son UTF-8 (argv o
// getenv en ANSI, un nombre guardado en otra codificacion) se leen como
// Windows-1252 antes de convertir.
inline std::filesystem::path pathFromUtf8(const std::string& text) {
    try {
        return std::filesystem::u8path(isValidUtf8(text) ? text : windows1252ToUtf8(text));
    } catch (...) {
        return {};
    }
}

// Una variable de entorno como ruta: en Windows se lee en UTF-16 (_wgetenv),
// que conserva "José" o "日本" tal cual. Vacia si no existe.
inline std::filesystem::path environmentPath(const char* name) {
#ifdef _WIN32
    std::wstring wide;
    for (const char* p = name; *p; ++p) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    const wchar_t* value = _wgetenv(wide.c_str());
    if (!value || !*value) return {};
    return std::filesystem::path(value);
#else
    const char* value = std::getenv(name);
    if (!value || !*value) return {};
    return pathFromUtf8(value);
#endif
}

}  // namespace fml

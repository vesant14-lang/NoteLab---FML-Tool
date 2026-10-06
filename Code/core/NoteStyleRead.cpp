#include "NoteStyleRead.hpp"

#include "../support/formats/SparrowAtlas.hpp"
#include "../support/io/Vfs.hpp"
#include "../third_party/json.hpp"
#include "../third_party/pugixml.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>

namespace fml::notelab {
namespace {

using json = nlohmann::json;

const char* const kColors[4] = {"purple", "blue", "green", "red"};

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Donde empieza un tramo como `images/` en el limite de una carpeta.
size_t segmentAt(const std::string& lowerPath, const std::string& segment) {
    size_t pos = 0;
    while ((pos = lowerPath.find(segment, pos)) != std::string::npos) {
        if (pos == 0 || lowerPath[pos - 1] == '/') return pos;
        ++pos;
    }
    return std::string::npos;
}

std::string fileName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stemOf(const std::string& path) {
    const std::string name = fileName(path);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string withoutExtension(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path;
    return path.substr(0, dot);
}

Finding finding(Severity severity, std::string code, std::string style, std::string subject,
                std::string path, std::string detail) {
    Finding f;
    f.severity = severity;
    f.code = std::move(code);
    f.style = std::move(style);
    f.subject = std::move(subject);
    f.path = std::move(path);
    f.detail = std::move(detail);
    return f;
}

// `0..3,5` como CoolUtil.parseNumberRange de Codename.
std::vector<int> parseIndices(const std::string& text) {
    std::vector<int> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t comma = text.find(',', start);
        if (comma == std::string::npos) comma = text.size();
        const std::string item = text.substr(start, comma - start);
        const size_t range = item.find("..");
        try {
            if (range != std::string::npos) {
                const int from = std::stoi(item.substr(0, range));
                const int to = std::stoi(item.substr(range + 2));
                if (to >= from && to - from < 4096)
                    for (int i = from; i <= to; ++i) out.push_back(i);
            } else if (item.find_first_not_of(" \t") != std::string::npos) {
                out.push_back(std::stoi(item));
            }
        } catch (...) {
        }
        start = comma + 1;
    }
    return out;
}

// Los mismos nombres de animacion en Codename y Psych, heredados del juego base.
void bindBaseGameNotes(NoteStyle& style, int sheet, Engine engine, bool withStrums) {
    for (int d = 0; d < 4; ++d) {
        // addByPrefix sin mas argumentos: 30 FPS y en bucle (Flixel).
        Animation scroll;
        scroll.prefix = std::string(kColors[d]) + "0";
        scroll.fps = 30.0f;
        scroll.loop = true;
        bindPart(style, Part::Note, d, sheet, scroll);

        Animation hold;
        hold.prefix = std::string(kColors[d]) + " hold piece";
        hold.fps = engine == Engine::Psych ? 24.0f : 30.0f;
        hold.loop = true;
        bindPart(style, Part::HoldPiece, d, sheet, hold);

        Animation end = hold;
        if (d == 0) {
            // Codename prueba primero la errata (Note.hx:174-177); Psych, al
            // reves (Note.hx:439-441). Los dos aceptan cualquiera de las dos.
            end.prefix = engine == Engine::Psych ? "purple hold end" : "pruple end hold";
            end.alternatives = {engine == Engine::Psych ? "pruple end hold" : "purple hold end"};
        } else {
            end.prefix = std::string(kColors[d]) + " hold end";
        }
        bindPart(style, Part::HoldEnd, d, sheet, end);
    }
    if (!withStrums) return;
    for (int d = 0; d < 4; ++d) {
        const std::string dir = directionKey(d);
        Animation still;
        still.prefix = "arrow" + upper(dir);
        still.fps = 30.0f;
        still.loop = true;
        bindPart(style, Part::StrumStatic, d, sheet, still);
        Animation press;
        press.prefix = dir + " press";
        press.fps = 24.0f;
        bindPart(style, Part::StrumPress, d, sheet, press);
        Animation confirm;
        confirm.prefix = dir + " confirm";
        confirm.fps = 24.0f;
        bindPart(style, Part::StrumConfirm, d, sheet, confirm);
    }
}

void setHudImage(const AssetIndex& index, const std::string& scope, HudAsset& asset,
                 const std::string& declared, const std::string& rel) {
    asset.declared = declared;
    asset.image = index.find(scope, rel);
    if (asset.image.empty()) asset.nearby = index.sameName(scope, fileName(rel));
}

void setHudSound(const AssetIndex& index, const std::string& scope, HudAsset& asset,
                 const std::string& declared, const std::string& relNoExt) {
    asset.soundDeclared = declared;
    asset.sound = index.findSound(scope, relNoExt);
}

// ---------------------------------------------------------------- Codename --

Animation codenameXmlAnimation(const pugi::xml_node& node) {
    Animation anim;
    anim.prefix = node.attribute("anim").as_string("");
    anim.fps = node.attribute("fps").as_float(24.0f);
    anim.loop = std::string(node.attribute("loop").as_string("false")) == "true";
    anim.offsetX = node.attribute("x").as_float(0.0f);
    anim.offsetY = node.attribute("y").as_float(0.0f);
    if (node.attribute("indices")) anim.indices = parseIndices(node.attribute("indices").as_string(""));
    return anim;
}

// data/splashes/<nombre>.xml (SplashGroup.hx:32-112).
void readCodenameSplash(const Vfs& vfs, const AssetIndex& index, const std::string& xmlPath,
                        const std::string& scope, NoteStyle& style, Catalog& catalog) {
    const auto text = vfs.readText(xmlPath);
    pugi::xml_document doc;
    if (!text || !doc.load_string(text->c_str())) {
        catalog.findings.push_back(finding(Severity::Error, "FML-NOTE-010", style.id, "splash",
                                           xmlPath, "xml"));
        return;
    }
    const pugi::xml_node root = doc.document_element();
    const std::string sprite = root.attribute("sprite").as_string("");
    if (sprite.empty()) {
        // El motor lanza: "The <splash> element requires a sprite attribute."
        catalog.findings.push_back(finding(Severity::Error, "FML-NOTE-011", style.id, "splash",
                                           xmlPath, "sprite"));
        return;
    }
    Sheet sheet;
    sheet.kind = SheetKind::Sparrow;
    sheet.declared = sprite;
    sheet.image = index.find(scope, "images/" + sprite + ".png");
    sheet.atlas = index.find(scope, "images/" + sprite + ".xml");
    sheet.scale = root.attribute("scale").as_float(1.0f);
    sheet.alpha = root.attribute("alpha").as_float(1.0f);
    const int s = addSheet(style, sheet);

    std::vector<Animation> global;
    for (const pugi::xml_node anim : root.children("anim"))
        if (anim.attribute("name")) global.push_back(codenameXmlAnimation(anim));
    bool anyStrum = false;
    for (const pugi::xml_node strum : root.children("strum")) {
        const int id = strum.attribute("id").as_int(-1);
        if (id < 0) continue;
        anyStrum = true;
        int variant = 0;
        for (const pugi::xml_node anim : strum.children("anim"))
            if (anim.attribute("name"))
                bindPart(style, Part::Splash, id % 4, s, codenameXmlAnimation(anim), variant++);
        for (const Animation& anim : global) bindPart(style, Part::Splash, id % 4, s, anim, variant++);
    }
    if (!anyStrum) {
        // Sin <strum> la lista de animaciones queda vacia y no sale ninguna
        // salpicadura (SplashGroup.hx:108).
        catalog.findings.push_back(finding(Severity::Warning, "FML-NOTE-012", style.id, "splash",
                                           xmlPath, ""));
    }
}

// ------------------------------------------------ Codename: scripts de tipo --
//
// `data/notes/<tipo>` es un script mas de la partida (PlayState.hx:797-806) y
// su onNoteCreation llega antes de que la nota ponga su aspecto. Si cancela el
// evento (CancellableEvent.hx:20-26), el motor no carga `event.noteSprite` ni
// anade las animaciones del juego base (Note.hx:164-195): valen las que anade
// el script. Se lee como texto, sin ejecutar nada; lo que no se sabe leer se
// marca y no se comprueba.

bool identChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string trimSpaces(const std::string& text) {
    size_t first = 0, last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

// Donde acaba la cadena cuya comilla abre en `at`, saltando los escapes.
size_t stringEnd(const std::string& code, size_t at) {
    const char quote = code[at];
    size_t i = at + 1;
    while (i < code.size() && code[i] != quote) i += code[i] == '\\' ? 2 : 1;
    return std::min(i + 1, code.size());
}

// El codigo sin comentarios: pasan a espacios y los saltos de linea se quedan,
// para que una llamada comentada no cuente.
std::string withoutComments(std::string code) {
    size_t i = 0;
    while (i < code.size()) {
        const char c = code[i];
        const char next = i + 1 < code.size() ? code[i + 1] : '\0';
        if (c == '"' || c == '\'') {
            i = stringEnd(code, i);
        } else if (c == '/' && next == '/') {
            while (i < code.size() && code[i] != '\n') code[i++] = ' ';
        } else if (c == '/' && next == '*') {
            const size_t close = code.find("*/", i + 2);
            const size_t end = close == std::string::npos ? code.size() : close + 2;
            for (; i < end; ++i)
                if (code[i] != '\n') code[i] = ' ';
        } else {
            ++i;
        }
    }
    return code;
}

// El que cierra el parentesis, corchete o llave que abre en `open`; npos si no cierra.
size_t closingOf(const std::string& code, size_t open) {
    int depth = 0;
    for (size_t i = open; i < code.size();) {
        const char c = code[i];
        if (c == '"' || c == '\'') {
            i = stringEnd(code, i);
            continue;
        }
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if ((c == ')' || c == ']' || c == '}') && --depth == 0) return i;
        ++i;
    }
    return std::string::npos;
}

// Los argumentos de la llamada cuyo parentesis abre en `open`, sin espacios a
// los lados; `close` queda en el que cierra (npos si no cierra).
std::vector<std::string> callArguments(const std::string& code, size_t open, size_t& close) {
    std::vector<std::string> args;
    close = closingOf(code, open);
    if (close == std::string::npos) return args;
    int depth = 0;
    size_t start = open + 1;
    for (size_t i = open + 1; i < close;) {
        const char c = code[i];
        if (c == '"' || c == '\'') {
            i = stringEnd(code, i);
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == ',' && depth == 0) {
            args.push_back(trimSpaces(code.substr(start, i - start)));
            start = i + 1;
        }
        ++i;
    }
    const std::string last = trimSpaces(code.substr(start, close - start));
    if (!last.empty() || !args.empty()) args.push_back(last);
    return args;
}

// El cuerpo de `function <nombre>(...)` y el nombre de su primer parametro
// (`event`, `e`, `event:NoteCreationEvent`). HScript admite el cuerpo sin
// llaves (`function f(e) if (...) {...}`): entonces llega hasta la siguiente
// `function` de fuera de todo bloque.
bool scriptFunction(const std::string& code, const std::string& name, std::string& param, std::string& body) {
    auto spaces = [&](size_t i) {
        while (i < code.size() && std::isspace(static_cast<unsigned char>(code[i]))) ++i;
        return i;
    };
    auto wordAt = [&](size_t i, const std::string& word) {
        return code.compare(i, word.size(), word) == 0 && (i == 0 || !identChar(code[i - 1])) &&
               (i + word.size() >= code.size() || !identChar(code[i + word.size()]));
    };
    for (size_t at = code.find("function"); at != std::string::npos; at = code.find("function", at + 1)) {
        if (!wordAt(at, "function")) continue;
        size_t i = spaces(at + 8);
        if (!wordAt(i, name)) continue;
        i = spaces(i + name.size());
        if (i >= code.size() || code[i] != '(') continue;
        size_t close = 0;
        const std::vector<std::string> params = callArguments(code, i, close);
        if (close == std::string::npos) return false;
        param.clear();
        if (!params.empty()) {
            size_t p = params[0].find_first_not_of('?');
            while (p < params[0].size() && identChar(params[0][p])) param += params[0][p++];
        }
        i = spaces(close + 1);
        if (i < code.size() && code[i] == ':') {   // tipo de retorno: `):Void {`
            i = spaces(i + 1);
            while (i < code.size() && (identChar(code[i]) || code[i] == '.')) ++i;
            i = spaces(i);
        }
        if (i < code.size() && code[i] == '{') {
            const size_t end = closingOf(code, i);
            body = code.substr(i + 1, (end == std::string::npos ? code.size() : end) - i - 1);
            return true;
        }
        size_t end = i;
        int depth = 0;
        while (end < code.size()) {
            const char c = code[end];
            if (c == '"' || c == '\'') {
                end = stringEnd(code, end);
                continue;
            }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') --depth;
            else if (depth <= 0 && wordAt(end, "function")) break;
            ++end;
        }
        body = code.substr(i, end - i);
        return true;
    }
    return false;
}

// Lo que pone el script de un tipo al cancelar onNoteCreation.
struct ScriptLook {
    bool cancels = false;
    bool framesKnown = false;
    std::string frames;   // lo que carga como fotogramas: `game/notes/<tipo>` u otra ruta
    // Por pieza (0 `scroll`, 1 `hold`, 2 `holdend`: lo que reproduce
    // Note.hx:199-213) y direccion, las animaciones en el orden en que las
    // anade y si alguna llamada no se pudo leer.
    std::array<std::array<std::vector<Animation>, 4>, 3> added;
    std::array<std::array<bool, 4>, 3> unread{};
};

constexpr int kOtherAnimation = -2;

// La pieza de la nota que anima un nombre: -1 si el nombre no es un texto
// fijo (puede ser cualquiera de las tres), kOtherAnimation si es otra.
int scriptPiece(const std::string& argument) {
    const std::string name = quotedValue(argument);
    if (name.empty()) return argument == "''" || argument == "\"\"" ? kOtherAnimation : -1;
    if (name == "scroll") return 0;
    if (name == "hold") return 1;
    if (name == "holdend") return 2;
    return kOtherAnimation;
}

// `type` es el nombre del tipo (el del archivo), para los `case` sobre noteType.
ScriptLook readTypeScript(const std::string& text, const std::string& type) {
    ScriptLook look;
    const std::string code = withoutComments(text);
    std::string param, body;
    if (!scriptFunction(code, "onNoteCreation", param, body) || param.empty()) return look;

    // Un switch abierto. Sus `case` sobre strumID o noteData eligen direccion;
    // los de noteType dejan fuera el codigo de otros tipos, porque el script
    // recibe las notas de todos los tipos de la cancion.
    struct Switch {
        int depth = 0;
        int kind = 0;             // 0 otro, 1 direccion, 2 tipo
        unsigned outerDirs = 0xF;
        bool outerSkip = false;
        unsigned dirs = 0xF;      // las del `case` en curso, un bit por direccion
        bool skip = false;        // el `case` en curso es de otro tipo
        unsigned listed = 0;      // direcciones de los `case` ya vistos
        bool typeListed = false;  // el tipo ya salio en un `case`
    };
    // Una asignacion a `frames` o a `noteSprite` y donde esta.
    struct Assignment {
        size_t at = 0;
        int depth = 0;
        int kind = 0;             // 0 no se sabe, 1 `event.noteSprite`, 2 ruta fija
        std::string path;
    };
    std::vector<Switch> switches;
    std::vector<Assignment> loads, sprites;
    int depth = 0;

    auto spaces = [&](size_t i) {
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
        return i;
    };
    // El identificador de delante de `.<palabra>`, si la palabra empieza en `at`
    // tras un punto; vacio si no.
    auto receiver = [&](size_t at) {
        size_t i = at;
        while (i > 0 && std::isspace(static_cast<unsigned char>(body[i - 1]))) --i;
        if (i == 0 || body[i - 1] != '.') return std::string();
        --i;
        while (i > 0 && std::isspace(static_cast<unsigned char>(body[i - 1]))) --i;
        size_t start = i;
        while (start > 0 && identChar(body[start - 1])) --start;
        return body.substr(start, i - start);
    };
    // `= <valor>` (no `==`) tras la palabra que acaba en `end`: el valor hasta
    // `;`, `,` o el fin de la linea, fuera de parentesis.
    auto assignedValue = [&](size_t end, std::string& value) {
        size_t i = spaces(end);
        if (i >= body.size() || body[i] != '=' || (i + 1 < body.size() && body[i + 1] == '=')) return false;
        const size_t start = ++i;
        int nest = 0;
        while (i < body.size()) {
            const char c = body[i];
            if (c == '"' || c == '\'') {
                i = stringEnd(body, i);
                continue;
            }
            if (c == '(' || c == '[' || c == '{') {
                ++nest;
            } else if (c == ')' || c == ']' || c == '}') {
                if (nest == 0) break;
                --nest;
            } else if (nest == 0 && (c == ';' || c == ',' || c == '\n')) {
                break;
            }
            ++i;
        }
        value = trimSpaces(body.substr(start, i - start));
        return true;
    };
    auto dirs = [&] { return switches.empty() ? 0xFu : switches.back().dirs; };
    auto skipping = [&] { return !switches.empty() && switches.back().skip; };
    auto markUnread = [&](int piece) {   // -1: las tres
        for (int p = 0; p < 3; ++p)
            for (int d = 0; d < 4; ++d)
                if ((piece < 0 || piece == p) && (dirs() & (1u << d)))
                    look.unread[static_cast<size_t>(p)][static_cast<size_t>(d)] = true;
    };

    for (size_t i = 0; i < body.size();) {
        const char c = body[i];
        if (c == '"' || c == '\'') {
            i = stringEnd(body, i);
            continue;
        }
        if (c == '{' || c == '}') {
            if (c == '}' && !switches.empty() && switches.back().depth == depth) switches.pop_back();
            depth += c == '{' ? 1 : -1;
            ++i;
            continue;
        }
        if (!identChar(c)) {
            ++i;
            continue;
        }
        const size_t at = i;
        size_t end = i;
        while (end < body.size() && identChar(body[end])) ++end;
        i = end;
        const std::string word = body.substr(at, end - at);
        const size_t next = spaces(end);
        const bool call = next < body.size() && body[next] == '(';

        if (word == "switch" && call) {
            const size_t close = closingOf(body, next);
            if (close == std::string::npos) break;
            const size_t open = spaces(close + 1);
            i = close + 1;
            if (open >= body.size() || body[open] != '{') continue;
            const std::string subject = body.substr(next, close - next);
            Switch s;
            s.depth = depth + 1;
            if (subject.find("strumID") != std::string::npos || subject.find("noteData") != std::string::npos) s.kind = 1;
            else if (subject.find("noteType") != std::string::npos) s.kind = 2;
            s.outerDirs = s.dirs = dirs();
            s.outerSkip = s.skip = skipping();
            switches.push_back(s);
            ++depth;
            i = open + 1;
            continue;
        }
        if ((word == "case" || word == "default") && !switches.empty() && switches.back().depth == depth) {
            // Los patrones hasta `:`: `case 0:`, `case 0, 1:`, `case 0 | 1:`.
            Switch& s = switches.back();
            std::vector<std::string> patterns;
            size_t k = end, start = end;
            int nest = 0;
            while (k < body.size()) {
                const char ch = body[k];
                if (ch == '"' || ch == '\'') {
                    k = stringEnd(body, k);
                    continue;
                }
                if (ch == '(' || ch == '[' || ch == '{') {
                    ++nest;
                } else if (ch == ')' || ch == ']' || ch == '}') {
                    --nest;
                } else if (nest == 0 && (ch == ',' || ch == '|' || ch == ':')) {
                    patterns.push_back(trimSpaces(body.substr(start, k - start)));
                    start = k + 1;
                    if (ch == ':') break;
                }
                ++k;
            }
            if (k >= body.size()) break;
            i = k + 1;
            s.dirs = s.outerDirs;
            s.skip = s.outerSkip;
            if (word == "default") {
                if (s.kind == 1) s.dirs = s.outerDirs & ~s.listed;
                if (s.kind == 2 && s.typeListed) s.skip = true;
            } else if (s.kind == 1) {
                // La direccion es `i % 4` en los tres motores.
                unsigned bits = 0;
                bool numbers = true;
                for (const std::string& pattern : patterns) {
                    char* stop = nullptr;
                    const long value = std::strtol(pattern.c_str(), &stop, 10);
                    if (pattern.empty() || *stop != '\0') numbers = false;
                    else bits |= 1u << (((value % 4) + 4) % 4);
                }
                if (numbers) {
                    s.dirs = s.outerDirs & bits;
                    s.listed |= bits;
                }
            } else if (s.kind == 2) {
                bool literal = true, mine = false;
                for (const std::string& pattern : patterns) {
                    const std::string name = quotedValue(pattern);
                    if (name.empty()) literal = false;
                    else if (lower(name) == lower(type)) mine = true;
                }
                if (literal) {
                    s.skip = s.outerSkip || !mine;
                    s.typeListed = s.typeListed || mine;
                }
            }
            continue;
        }
        if (skipping()) continue;

        const std::string owner = receiver(at);
        if (owner.empty()) continue;
        if (call && owner == param && (word == "cancel" || word == "preventDefault")) {
            look.cancels = true;
        } else if (word == "cancelled" && owner == param) {
            std::string value;
            if (assignedValue(end, value) && value == "true") look.cancels = true;
        } else if (call && word == "addByPrefix") {
            size_t close = 0;
            const std::vector<std::string> args = callArguments(body, next, close);
            const int piece = args.empty() ? -1 : scriptPiece(args[0]);
            const std::string prefix = args.size() > 1 ? quotedValue(args[1]) : std::string();
            if (piece == kOtherAnimation) continue;
            if (piece < 0 || prefix.empty()) {
                markUnread(piece);
                continue;
            }
            // addByPrefix(nombre, prefijo, fps = 30, bucle = true)
            // (FlxAnimationController.hx:481).
            Animation anim;
            anim.prefix = prefix;
            anim.fps = 30.0f;
            anim.loop = true;
            if (args.size() > 2) {
                char* stop = nullptr;
                const float fps = std::strtof(args[2].c_str(), &stop);
                if (!args[2].empty() && *stop == '\0') anim.fps = fps;
            }
            if (args.size() > 3 && (args[3] == "true" || args[3] == "false")) anim.loop = args[3] == "true";
            for (int d = 0; d < 4; ++d)
                if (dirs() & (1u << d)) look.added[static_cast<size_t>(piece)][static_cast<size_t>(d)].push_back(anim);
        } else if (call && (word == "addByIndices" || word == "addByNames" || word == "addByStringIndices" ||
                            (word == "add" && owner == "animation"))) {
            // Por indices o por fotogramas sueltos: Note Lab no lo lee.
            size_t close = 0;
            const std::vector<std::string> args = callArguments(body, next, close);
            const int piece = args.empty() ? -1 : scriptPiece(args[0]);
            if (piece != kOtherAnimation) markUnread(piece);
        } else if (call && (word == "loadGraphic" || word == "makeGraphic")) {
            markUnread(-1);   // una imagen sin prefijos
        } else if (word == "frames") {
            std::string value;
            if (!assignedValue(end, value)) continue;
            Assignment load;
            load.at = at;
            load.depth = depth;
            // Paths.getFrames(<ruta>) o Paths.getSparrowAtlas(<ruta>)
            // (Paths.hx:175-176, :206-215).
            for (const std::string loader : {"getFrames", "getSparrowAtlas"}) {
                const size_t found = value.find(loader);
                if (found == std::string::npos) continue;
                size_t open = found + loader.size();
                while (open < value.size() && std::isspace(static_cast<unsigned char>(value[open]))) ++open;
                if (open >= value.size() || value[open] != '(') continue;
                size_t close = 0;
                const std::vector<std::string> args = callArguments(value, open, close);
                if (args.empty()) break;
                std::string argument = args[0];
                argument.erase(std::remove_if(argument.begin(), argument.end(),
                                              [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; }),
                               argument.end());
                if (argument == param + ".noteSprite") {
                    load.kind = 1;
                } else {
                    load.path = quotedValue(args[0]);
                    load.kind = load.path.empty() ? 0 : 2;
                }
                break;
            }
            loads.push_back(load);
        } else if (word == "noteSprite" && owner == param) {
            std::string value;
            if (!assignedValue(end, value)) continue;
            Assignment sprite;
            sprite.at = at;
            sprite.depth = depth;
            sprite.path = quotedValue(value);
            sprite.kind = sprite.path.empty() ? 0 : 2;
            sprites.push_back(sprite);
        }
    }
    if (!look.cancels) return look;

    // Lo que cargan sus fotogramas. `event.noteSprite` es `game/notes/<tipo>`
    // (Note.hx:156-158) salvo que el script lo cambie antes; un cambio dentro
    // de un bloque que la carga no tiene depende de una condicion y no se sabe.
    look.framesKnown = !loads.empty();
    for (const Assignment& load : loads) {
        bool known = load.kind != 0;
        std::string path = load.path;
        if (load.kind == 1) {
            path = "game/notes/" + type;
            for (const Assignment& sprite : sprites) {
                if (sprite.at > load.at) continue;
                if (sprite.kind == 0 || sprite.depth > load.depth) known = false;
                else path = sprite.path;
            }
        }
        if (!known || (!look.frames.empty() && lower(look.frames) != lower(path))) {
            look.framesKnown = false;
            break;
        }
        look.frames = path;
    }
    return look;
}

// Paths.script prueba las extensiones de Script.scriptExtensions en orden
// (Paths.hx:118-130, Script.hx:190-194); `pack` y `lua` no son de un tipo.
std::string codenameTypeScript(const AssetIndex& index, const std::string& scope, const std::string& type) {
    for (const char* extension : {".hx", ".hscript", ".hsc", ".hxs"}) {
        const std::string path = index.exact(scope + "data/notes/" + type + extension);
        if (!path.empty()) return path;
    }
    return {};
}

// Las piezas de un tipo cuyo script cancela onNoteCreation. En cada direccion
// va lo que anade el script: la ultima llamada que encuentra fotogramas es la
// que queda (FlxAnimationController.hx:481-500, en la copia de Psych 0.7), asi
// que las anteriores van de alternativas. Lo que el script no anade no existe
// para el motor y queda sin prefijo; lo que no se sabe leer, marcado.
void bindScriptLook(const AssetIndex& index, NoteStyle& style, int ownSheet, const std::string& script,
                    const ScriptLook& look) {
    style.lookScript = script;
    int sheet = ownSheet;
    bool framesKnown = look.framesKnown;
    if (framesKnown && lower(look.frames) != lower(style.sheets[static_cast<size_t>(ownSheet)].declared)) {
        // Otra hoja: solo la de Sparrow; con otro formato no se sabe que carga.
        const std::string xml = index.find(style.scope, "images/" + look.frames + ".xml");
        if (xml.empty()) {
            framesKnown = false;
        } else {
            Sheet other;
            other.kind = SheetKind::Sparrow;
            other.declared = look.frames;
            other.atlas = xml;
            other.image = index.exact(withoutExtension(xml) + ".png");
            other.scale = style.sheets[static_cast<size_t>(ownSheet)].scale;
            sheet = addSheet(style, other);
        }
    }
    static const Part parts[3] = {Part::Note, Part::HoldPiece, Part::HoldEnd};
    for (size_t d = 0; d < 4; ++d)
        for (size_t p = 0; p < 3; ++p) {
            const std::vector<Animation>& added = look.added[p][d];
            const bool unread = !framesKnown || look.unread[p][d];
            Animation anim;
            if (!unread && !added.empty()) {
                anim = added.back();
                for (auto it = added.rbegin() + 1; it != added.rend(); ++it)
                    if (it->prefix != anim.prefix &&
                        std::find(anim.alternatives.begin(), anim.alternatives.end(), it->prefix) == anim.alternatives.end())
                        anim.alternatives.push_back(it->prefix);
            }
            bindPart(style, parts[p], static_cast<int>(d), sheet, anim);
            style.parts.back().unread = unread;
        }
}

void readCodename(const Vfs& vfs, const AssetIndex& index, Catalog& catalog) {
    static const std::string kNotes = "images/game/notes/";
    static const std::string kSplashes = "data/splashes/";
    std::set<std::string> defaultScopes;
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (!endsWith(low, ".xml")) continue;
        const size_t seg = segmentAt(low, kNotes);
        if (seg == std::string::npos) continue;
        if (low.find('/', seg + kNotes.size()) != std::string::npos) continue;
        const std::string scope = path.substr(0, seg);
        const std::string name = stemOf(path);
        const bool isDefault = lower(name) == "default";

        NoteStyle style;
        style.engine = Engine::Codename;
        style.scope = scope;
        style.definition = path;
        style.name = name;
        style.id = "codename:" + scope + "game/notes/" + name;
        style.use = isDefault ? StyleUse::Default : StyleUse::NoteType;
        if (!isDefault) style.useDetail = name;

        Sheet sheet;
        sheet.kind = SheetKind::Sparrow;
        sheet.declared = "game/notes/" + name;
        sheet.atlas = path;
        sheet.image = index.exact(withoutExtension(path) + ".png");
        sheet.scale = 0.7f;   // Flags.DEFAULT_NOTE_SCALE (Flags.hx:139)
        const int s = addSheet(style, sheet);
        // El script del tipo, en el mismo scope, puede cancelar la creacion
        // de la nota y poner el aspecto el mismo.
        const std::string script = isDefault ? std::string() : codenameTypeScript(index, scope, name);
        ScriptLook look;
        if (!script.empty())
            if (const auto text = vfs.readText(script)) look = readTypeScript(*text, name);
        // Los receptores salen siempre de `game/notes/default` salvo que un
        // script cambie `StrumCreationEvent.sprite` (StrumCreationEvent.hx:32).
        if (look.cancels) bindScriptLook(index, style, s, script, look);
        else bindBaseGameNotes(style, s, Engine::Codename, isDefault);

        if (isDefault) {
            defaultScopes.insert(scope);
            const std::string splash = index.exact(scope + kSplashes + "default.xml");
            if (!splash.empty()) readCodenameSplash(vfs, index, splash, scope, style, catalog);
            style.hasHud = true;
            for (int i = 0; i < 4; ++i)
                setHudImage(index, scope, style.judgements[i], std::string("game/score/") + judgementKey(i),
                            std::string("images/game/score/") + judgementKey(i) + ".png");
            setHudImage(index, scope, style.combo, "game/score/combo", "images/game/score/combo.png");
            for (int i = 0; i < 10; ++i)
                setHudImage(index, scope, style.digits[i], "game/score/num" + std::to_string(i),
                            "images/game/score/num" + std::to_string(i) + ".png");
            // Flags.hx:200 (imagenes) y :247 (sonidos).
            static const char* sprites[4] = {nullptr, "game/ready", "game/set", "game/go"};
            static const char* sounds[4] = {"intro3", "intro2", "intro1", "introGo"};
            for (int i = 0; i < 4; ++i) {
                HudAsset& step = style.countdown[i];
                if (sprites[i]) setHudImage(index, scope, step, sprites[i],
                                            std::string("images/") + sprites[i] + ".png");
                else step.imageOptional = true;
                setHudSound(index, scope, step, sounds[i], std::string("sounds/") + sounds[i]);
            }
        }
        catalog.styles.push_back(std::move(style));
    }
    // Las demas salpicaduras: una nota puede pedir otra por `note.splash`
    // (Note.hx:76), asi que cada XML es un estilo aparte.
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (!endsWith(low, ".xml")) continue;
        const size_t seg = segmentAt(low, kSplashes);
        if (seg == std::string::npos) continue;
        if (low.find('/', seg + kSplashes.size()) != std::string::npos) continue;
        const std::string scope = path.substr(0, seg);
        const std::string name = stemOf(path);
        if (lower(name) == "default" && defaultScopes.count(scope)) continue;
        NoteStyle style;
        style.engine = Engine::Codename;
        style.scope = scope;
        style.definition = path;
        style.name = "splash " + name;
        style.id = "codename:" + scope + "data/splashes/" + name;
        style.use = StyleUse::Declared;
        style.useDetail = name;
        // Solo se usa si una nota lo pide por script o tipo de nota.
        style.referenced = false;
        readCodenameSplash(vfs, index, path, scope, style, catalog);
        catalog.styles.push_back(std::move(style));
    }
}

// ------------------------------------------------------------------- Psych --

std::set<std::string> psychListed(const Vfs& vfs, const AssetIndex& index, const std::string& scope,
                                  const std::string& listRel, const std::string& spaceTo) {
    std::set<std::string> out;
    const std::string path = index.find(scope, listRel);
    if (path.empty()) return out;
    const auto text = vfs.readText(path);
    if (!text) return out;
    size_t start = 0;
    while (start < text->size()) {
        size_t end = text->find('\n', start);
        if (end == std::string::npos) end = text->size();
        std::string line = text->substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        while (!line.empty() && line.front() == ' ') line.erase(line.begin());
        // Psych convierte el nombre elegido en sufijo: minusculas y los espacios
        // a `_` en las notas (Note.hx:429; :367 en 0.7) y en las salpicaduras a
        // `-` en 1.0 (NoteSplash.hx:366) y a `_` en 0.7 (NoteSplash.hx:108).
        for (const char to : spaceTo) {
            std::string key = lower(line);
            std::replace(key.begin(), key.end(), ' ', to);
            if (!key.empty()) out.insert(key);
        }
        start = end + 1;
    }
    return out;
}

// noteSplashes/<nombre>: JSON de 1.0, TXT de 0.7 o, sin nada, `note splash
// <color> <n>` mientras existan los cuatro colores (NoteSplash.hx:123-180).
void readPsychSplash(const Vfs& vfs, const AssetIndex& index, AtlasCache& atlases,
                     const std::string& xmlPath, const std::string& scope, NoteStyle& style,
                     Catalog& catalog) {
    Sheet sheet;
    sheet.kind = SheetKind::Sparrow;
    sheet.atlas = xmlPath;
    sheet.image = index.exact(withoutExtension(xmlPath) + ".png");
    const size_t seg = segmentAt(lower(xmlPath), "images/");
    sheet.declared = seg == std::string::npos ? stemOf(xmlPath)
                                              : withoutExtension(xmlPath.substr(seg + 7));
    const std::string base = withoutExtension(xmlPath);
    const std::string jsonPath = index.exact(base + ".json");
    if (!jsonPath.empty()) {
        const auto text = vfs.readText(jsonPath);
        const json doc = text ? json::parse(*text, nullptr, false, true) : json();
        if (!doc.is_object()) {
            catalog.findings.push_back(finding(Severity::Error, "FML-NOTE-010", style.id, "splash",
                                               jsonPath, "json"));
            return;
        }
        if (doc.contains("scale") && doc["scale"].is_number()) sheet.scale = doc["scale"].get<float>();
        const int s = addSheet(style, sheet);
        std::map<int, int> variants;
        if (doc.contains("animations") && doc["animations"].is_object()) {
            for (auto it = doc["animations"].begin(); it != doc["animations"].end(); ++it) {
                const json& a = it.value();
                if (!a.is_object()) continue;
                Animation anim;
                anim.prefix = a.value("prefix", std::string());
                if (a.contains("indices") && a["indices"].is_array())
                    for (const json& i : a["indices"]) if (i.is_number_integer()) anim.indices.push_back(i.get<int>());
                if (a.contains("fps") && a["fps"].is_array() && !a["fps"].empty() && a["fps"][0].is_number())
                    anim.fps = a["fps"][0].get<float>();
                if (a.contains("offsets") && a["offsets"].is_array() && a["offsets"].size() >= 2 &&
                    a["offsets"][0].is_number() && a["offsets"][1].is_number()) {
                    anim.offsetX = a["offsets"][0].get<float>();
                    anim.offsetY = a["offsets"][1].get<float>();
                }
                const int data = a.contains("noteData") && a["noteData"].is_number_integer()
                                     ? a["noteData"].get<int>() : 0;
                const int direction = ((data % 4) + 4) % 4;
                bindPart(style, Part::Splash, direction, s, anim, variants[direction]++);
            }
        }
        return;
    }
    std::string animBase = "note splash";
    float fps = 22.0f;
    // Desde la tercera linea, un offset por animacion; la animacion n toma el
    // n-esimo dando la vuelta (FlxMath.wrap: NoteSplash.hx:138-150 de 1.0 y
    // :143-149, :83 de 0.7).
    std::vector<std::pair<float, float>> offsets;
    const std::string txtPath = index.exact(base + ".txt");
    if (!txtPath.empty()) {
        if (const auto text = vfs.readText(txtPath)) {
            std::vector<std::string> lines;
            size_t start = 0;
            while (start < text->size()) {
                size_t end = text->find('\n', start);
                if (end == std::string::npos) end = text->size();
                std::string line = text->substr(start, end - start);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                lines.push_back(line);
                start = end + 1;
            }
            if (!lines.empty() && !lines[0].empty()) animBase = lines[0];
            if (lines.size() > 1) {
                try { fps = std::stof(lines[1]); } catch (...) {}
            }
            for (size_t i = 2; i < lines.size(); ++i) {
                float x = 0.0f, y = 0.0f;
                if (std::sscanf(lines[i].c_str(), "%f %f", &x, &y) >= 1) offsets.emplace_back(x, y);
            }
        }
    }
    const int s = addSheet(style, sheet);
    int variants = 0;
    if (const std::vector<std::string>* names = atlases.names(vfs, xmlPath)) {
        for (;;) {
            bool all = true;
            for (int d = 0; d < 4 && all; ++d)
                all = countPrefix(*names, animBase + " " + kColors[d] + " " + std::to_string(variants + 1)) > 0;
            if (!all || variants >= 64) break;
            ++variants;
        }
    }
    // Sin ninguna variante completa se enlaza la primera para que el validador
    // diga que color falta: el motor no dibujaria salpicadura alguna.
    for (int v = 0; v < std::max(variants, 1); ++v)
        for (int d = 0; d < 4; ++d) {
            Animation anim;
            anim.prefix = animBase + " " + kColors[d] + " " + std::to_string(v + 1);
            anim.fps = fps;
            if (!offsets.empty()) {
                const auto& offset = offsets[static_cast<size_t>(d + v * 4) % offsets.size()];
                anim.offsetX = offset.first;
                anim.offsetY = offset.second;
            }
            bindPart(style, Part::Splash, d, s, anim, v);
        }
}

void readPsychHud(const AssetIndex& index, const std::string& scope, NoteStyle& style) {
    style.hasHud = true;
    for (int i = 0; i < 4; ++i)
        setHudImage(index, scope, style.judgements[i], judgementKey(i),
                    std::string("images/") + judgementKey(i) + ".png");
    setHudImage(index, scope, style.combo, "combo", "images/combo.png");
    for (int i = 0; i < 10; ++i)
        setHudImage(index, scope, style.digits[i], "num" + std::to_string(i),
                    "images/num" + std::to_string(i) + ".png");
    static const char* sprites[4] = {nullptr, "ready", "set", "go"};
    static const char* sounds[4] = {"intro3", "intro2", "intro1", "introGo"};
    for (int i = 0; i < 4; ++i) {
        HudAsset& step = style.countdown[i];
        if (sprites[i]) setHudImage(index, scope, step, sprites[i], std::string("images/") + sprites[i] + ".png");
        else step.imageOptional = true;
        setHudSound(index, scope, step, sounds[i], std::string("sounds/") + sounds[i]);
    }
}

// Nombres por letra de los forks con mas teclas: al menos cuatro letras con su
// `hold` y su `tail`, y ningun `purple0`.
bool letteredAtlas(const std::vector<std::string>& names) {
    if (countPrefix(names, "purple0") > 0) return false;
    int letters = 0;
    for (char letter = 'A'; letter <= 'R'; ++letter) {
        const std::string base(1, letter);
        if (countPrefix(names, base + " hold") > 0 && countPrefix(names, base + " tail") > 0) ++letters;
    }
    return letters >= 4;
}

// Los nombres de Psych Engine Extra Keys, el fork de los mods con mas teclas
// (github.com/Sonsk200/FNF-PsychEngine-ExtraKeys, 70b0641): con 4 teclas (la
// `mania` 3, la de por defecto: Note.hx:49, :55) las letras A, B, C y D son
// izquierda, abajo, arriba y derecha. La nota es `<letra>0` y el sostenido
// `<letra> hold` y `<letra> tail`, a 30 FPS en bucle (Note.hx:342-350); el
// receptor en reposo sigue siendo `arrowLEFT`... y el pulsado y el acierto son
// `<letra> press` y `<letra> confirm` (StrumNote.hx:38-39, :81-83).
void bindLetteredNotes(NoteStyle& style) {
    static const char* const letters[4] = {"A", "B", "C", "D"};
    for (PartBinding& binding : style.parts) {
        const std::string letter = letters[binding.direction & 3];
        Animation& anim = binding.animation;
        switch (binding.part) {
            case Part::Note: anim.prefix = letter + "0"; break;
            case Part::HoldPiece: anim.prefix = letter + " hold"; break;
            case Part::HoldEnd: anim.prefix = letter + " tail"; break;
            case Part::StrumPress: anim.prefix = letter + " press"; continue;
            case Part::StrumConfirm: anim.prefix = letter + " confirm"; continue;
            default: continue;
        }
        anim.alternatives.clear();
        anim.fps = 30.0f;
        anim.loop = true;
    }
}

// Psych busca en `assets/<biblioteca>/images` y en `assets/images` (Paths): el
// skin de `assets/` y la salpicadura de `assets/shared/` son la misma instalacion.
std::string psychInstallRoot(const std::string& scope) {
    const std::string low = lower(scope);
    const size_t assets = segmentAt(low, "assets/");
    if (assets == std::string::npos) return low;
    return low.substr(0, assets + 7);
}

// La carpeta del ejecutable de Psych: lo que va antes de `assets/` o `mods/`.
std::string psychExecutableRoot(const std::string& scope) {
    const std::string low = lower(scope);
    size_t cut = segmentAt(low, "assets/");
    const size_t mods = segmentAt(low, "mods/");
    if (mods != std::string::npos && (cut == std::string::npos || mods < cut)) cut = mods;
    return cut == std::string::npos ? low : low.substr(0, cut);
}

NoteStyle psychSkin(const AssetIndex& index, const std::string& xmlPath, const std::string& scope,
                    const std::string& declared, bool rgb) {
    NoteStyle style;
    style.engine = Engine::Psych;
    style.scope = scope;
    style.definition = xmlPath;
    style.name = stemOf(xmlPath);
    style.id = "psych:" + scope + declared;
    style.rgbPalette = rgb;
    Sheet sheet;
    sheet.kind = SheetKind::Sparrow;
    sheet.declared = declared;
    sheet.atlas = xmlPath;
    sheet.image = index.exact(withoutExtension(xmlPath) + ".png");
    sheet.scale = 0.7f;
    const int s = addSheet(style, sheet);
    // StrumNote.hx:55-58: los receptores usan el mismo skin que las notas.
    bindBaseGameNotes(style, s, Engine::Psych, true);
    return style;
}

void readPsych(const Vfs& vfs, const AssetIndex& index, AtlasCache& atlases, Catalog& catalog) {
    static const std::string kSkins = "images/noteskins/";
    static const std::string kSplashes = "images/notesplashes/";
    std::map<std::string, size_t> defaultByScope;   // scope -> indice del skin por defecto
    std::map<std::string, std::set<std::string>> listedSkins, listedSplashes;

    // V-Slice sigue trayendo `images/NOTE_assets` y llama `noteSplashes` al atlas
    // de sus salpicaduras, pero los usa a traves de sus notestyles. Dentro de una
    // instalacion o un mod de V-Slice (su `_polymod_meta.json` o sus
    // `data/notestyles/`) esos nombres no son skins de Psych.
    std::vector<std::string> vsliceRoots;
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (fileName(low) == "_polymod_meta.json") vsliceRoots.push_back(low.substr(0, low.size() - 18));
        const size_t styles = segmentAt(low, "data/notestyles/");
        if (styles != std::string::npos) vsliceRoots.push_back(low.substr(0, styles));
        const size_t gameplay = segmentAt(low, "gameplay/notestyles/");
        if (gameplay != std::string::npos) vsliceRoots.push_back(low.substr(0, gameplay));
    }
    auto insideVSlice = [&](const std::string& scope) {
        const std::string lowScope = lower(scope);
        return std::any_of(vsliceRoots.begin(), vsliceRoots.end(),
                           [&](const std::string& root) { return startsWith(lowScope, root); });
    };
    auto markLettered = [&](NoteStyle& style) {
        if (style.sheets.empty()) return;
        if (const std::vector<std::string>* names = atlases.names(vfs, style.sheets[0].atlas))
            style.letteredNaming = letteredAtlas(*names);
        if (style.letteredNaming) bindLetteredNotes(style);
    };
    std::vector<std::string> pixelSkins;
    std::vector<size_t> legacyDefaults;

    auto skinsListed = [&](const std::string& scope) -> const std::set<std::string>& {
        auto it = listedSkins.find(scope);
        if (it == listedSkins.end())
            it = listedSkins.emplace(scope, psychListed(vfs, index, scope, "images/noteSkins/list.txt", "_")).first;
        return it->second;
    };
    auto splashesListed = [&](const std::string& scope) -> const std::set<std::string>& {
        auto it = listedSplashes.find(scope);
        if (it == listedSplashes.end())
            it = listedSplashes.emplace(scope, psychListed(vfs, index, scope, "images/noteSplashes/list.txt", "-_")).first;
        return it->second;
    };

    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (endsWith(low, ".png") && (segmentAt(low, "images/pixelui/") != std::string::npos) &&
            lower(fileName(path)).rfind("note_assets", 0) == 0) {
            pixelSkins.push_back(path);
            continue;
        }
        if (!endsWith(low, ".xml")) continue;
        size_t seg = segmentAt(low, kSkins);
        if (seg != std::string::npos && low.find('/', seg + kSkins.size()) == std::string::npos &&
            lower(stemOf(path)).rfind("note_assets", 0) == 0) {
            // Psych 0.7+: noteSkins/NOTE_assets[-<skin>] y la paleta RGB.
            const std::string scope = path.substr(0, seg);
            const std::string stem = stemOf(path);
            NoteStyle style = psychSkin(index, path, scope, "noteSkins/" + stem, true);
            markLettered(style);
            const std::string lowStem = lower(stem);
            if (lowStem == "note_assets") {
                style.use = StyleUse::Default;
                defaultByScope[psychInstallRoot(scope)] = catalog.styles.size();
            } else {
                const std::string suffix = lowStem.size() > 12 ? lowStem.substr(12) : lowStem;
                style.useDetail = suffix;
                style.use = skinsListed(scope).count(suffix) ? StyleUse::PlayerChoice : StyleUse::Declared;
                style.referenced = style.use == StyleUse::PlayerChoice;
            }
            catalog.styles.push_back(std::move(style));
            continue;
        }
        seg = segmentAt(low, "images/note_assets.xml");
        if (seg != std::string::npos && seg + std::strlen("images/note_assets.xml") == low.size()) {
            // Psych 0.6 y el juego base antiguo: images/NOTE_assets, sin paleta.
            const std::string scope = path.substr(0, seg);
            if (insideVSlice(scope)) continue;
            NoteStyle style = psychSkin(index, path, scope, "NOTE_assets", false);
            markLettered(style);
            style.use = StyleUse::Default;
            legacyDefaults.push_back(catalog.styles.size());
            if (!defaultByScope.count(psychInstallRoot(scope)))
                defaultByScope[psychInstallRoot(scope)] = catalog.styles.size();
            catalog.styles.push_back(std::move(style));
        }
    }
    // Con noteSkins/NOTE_assets en la misma instalacion, 0.7+ ya no carga el
    // images/NOTE_assets antiguo (Note.hx:99).
    for (size_t i : legacyDefaults) {
        const auto owner = defaultByScope.find(psychInstallRoot(catalog.styles[i].scope));
        if (owner != defaultByScope.end() && owner->second != i) {
            catalog.styles[i].use = StyleUse::Declared;
            catalog.styles[i].referenced = false;
        }
    }

    // Salpicaduras: la de por defecto va con el skin por defecto de su scope.
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (!endsWith(low, ".xml")) continue;
        std::string scope;
        std::string stem = stemOf(path);
        bool legacy = false;
        size_t seg = segmentAt(low, kSplashes);
        if (seg != std::string::npos && low.find('/', seg + kSplashes.size()) == std::string::npos) {
            scope = path.substr(0, seg);
        } else {
            seg = segmentAt(low, "images/notesplashes.xml");
            if (seg == std::string::npos || seg + std::strlen("images/notesplashes.xml") != low.size()) continue;
            scope = path.substr(0, seg);
            legacy = true;
        }
        if (insideVSlice(scope)) continue;
        const std::string lowStem = lower(stem);
        const bool isDefault = lowStem == "notesplashes";
        auto owner = defaultByScope.find(psychInstallRoot(scope));
        if (isDefault && owner != defaultByScope.end()) {
            NoteStyle& style = catalog.styles[owner->second];
            bool alreadyHasSplash = std::any_of(style.parts.begin(), style.parts.end(),
                [](const PartBinding& b) { return b.part == Part::Splash; });
            // Con las dos (0.6 y 0.7+) manda la de la carpeta noteSplashes.
            if (!alreadyHasSplash || !legacy) {
                if (alreadyHasSplash)
                    style.parts.erase(std::remove_if(style.parts.begin(), style.parts.end(),
                        [](const PartBinding& b) { return b.part == Part::Splash; }), style.parts.end());
                readPsychSplash(vfs, index, atlases, path, scope, style, catalog);
            }
            continue;
        }
        NoteStyle style;
        style.engine = Engine::Psych;
        style.scope = scope;
        style.definition = path;
        style.name = stem;
        style.id = "psych:" + scope + (legacy ? "" : "noteSplashes/") + stem;
        style.rgbPalette = !legacy;
        const std::string suffix = lowStem.size() > 13 ? lowStem.substr(13) : lowStem;
        style.useDetail = suffix;
        style.use = !legacy && splashesListed(scope).count(suffix) ? StyleUse::PlayerChoice : StyleUse::Declared;
        // La salpicadura por defecto se carga aunque no haya skin al lado; una
        // suelta sin list.txt solo la pediria un chart o un script.
        style.referenced = isDefault || style.use == StyleUse::PlayerChoice;
        readPsychSplash(vfs, index, atlases, path, scope, style, catalog);
        catalog.styles.push_back(std::move(style));
    }

    for (const auto& entry : defaultByScope) {
        NoteStyle& style = catalog.styles[entry.second];
        readPsychHud(index, style.scope, style);
    }

    // Skins pixel: en un escenario pixel Psych carga `pixelUI/` + el skin y lo
    // corta en una rejilla de 4 columnas: 5 filas las notas y los receptores y
    // 2 los sostenidos, `<skin>ENDS<sufijo>` (Note.hx:326-336 de 0.7, :388-398
    // de 1.0; StrumNote.hx:63-68). La nota es la celda 4 + direccion, el tramo
    // la direccion y el final 4 + direccion (Note.hx:384-389); el receptor en
    // reposo la direccion, pulsado 4 y 8 + direccion a 12 FPS y acierto 12 y 16
    // + direccion a 24 (12 el de arriba) (StrumNote.hx:80-94). Todo por
    // PlayState.daPixelZoom (6) y sin suavizado.
    for (const std::string& path : pixelSkins) {
        const std::string low = lower(path);
        const size_t seg = segmentAt(low, "images/pixelui/");
        const std::string stem = stemOf(path);
        const std::string lowStem = lower(stem);
        if (lowStem.find("ends") != std::string::npos) continue;   // los sostenidos de otro skin
        const std::string rel = withoutExtension(path.substr(seg + 7));   // pixelUI/noteSkins/NOTE_assets-chip
        const bool modern = segmentAt(low, "images/pixelui/noteskins/") == seg;   // 0.7 y 1.0
        if (!modern && low.find('/', seg + std::strlen("images/pixelui/")) != std::string::npos) continue;
        const std::string scope = path.substr(0, seg);
        if (insideVSlice(scope)) continue;
        const std::string folder = rel.substr(0, rel.size() - stem.size());
        const std::string base = stem.substr(0, 11);              // NOTE_assets
        const std::string postfix = stem.substr(11);              // "" o "-chip"
        NoteStyle style;
        style.engine = Engine::Psych;
        style.scope = scope;
        style.definition = path;
        style.name = stem + " (pixel)";
        style.id = "psych:" + scope + rel;
        style.pixel = true;
        style.rgbPalette = modern;   // arrowRGBPixel (Note.hx:134 de 0.7)
        const std::string suffix = postfix.size() > 1 ? lower(postfix.substr(1)) : std::string();
        if (suffix.empty()) {
            style.use = StyleUse::Declared;
            style.useDetail = "pixel";
        } else {
            style.use = skinsListed(scope).count(suffix) ? StyleUse::PlayerChoice : StyleUse::Declared;
            style.useDetail = suffix + " (pixel)";
            style.referenced = style.use == StyleUse::PlayerChoice;
        }
        Sheet notes;
        notes.kind = SheetKind::Grid;
        notes.declared = rel;
        notes.image = path;
        notes.columns = 4;
        notes.rows = 5;
        notes.scale = 6.0f;
        notes.pixel = true;
        const int n = addSheet(style, notes);
        Sheet ends = notes;
        ends.declared = folder + base + "ENDS" + postfix;
        ends.image = index.exact(scope + "images/" + ends.declared + ".png");
        ends.rows = 2;
        const int e = addSheet(style, ends);
        for (int d = 0; d < 4; ++d) {
            auto cells = [](std::vector<int> list, float fps, bool loop) {
                Animation anim;
                anim.indices = std::move(list);
                anim.fps = fps;
                anim.loop = loop;
                return anim;
            };
            bindPart(style, Part::Note, d, n, cells({d + 4}, 24.0f, true));
            bindPart(style, Part::HoldPiece, d, e, cells({d}, 24.0f, true));
            bindPart(style, Part::HoldEnd, d, e, cells({d + 4}, 24.0f, true));
            bindPart(style, Part::StrumStatic, d, n, cells({d}, 30.0f, true));
            bindPart(style, Part::StrumPress, d, n, cells({d + 4, d + 8}, 12.0f, false));
            bindPart(style, Part::StrumConfirm, d, n, cells({d + 12, d + 16}, d == 2 ? 12.0f : 24.0f, false));
        }
        catalog.styles.push_back(std::move(style));
    }

    // Tipos de nota con aspecto propio. Uno de custom_notetypes que asigna una
    // textura la carga sin la paleta RGB (Note.hx:369: `else rgbShader.enabled
    // = false`). Solo cuerpo y sostenido: los receptores no cambian por el tipo
    // de una nota.
    auto typeStyle = [&](const std::string& scope, const std::string& name, const std::string& texture,
                         const std::string& definition) {
        const std::string xml = index.find(scope, "images/" + texture + ".xml");
        if (xml.empty()) return;
        NoteStyle style;
        style.engine = Engine::Psych;
        style.scope = scope;
        style.definition = definition;
        style.name = name;
        style.id = "psych:" + scope + "notetype/" + name;
        style.use = StyleUse::NoteType;
        style.useDetail = name;
        Sheet sheet;
        sheet.kind = SheetKind::Sparrow;
        sheet.declared = texture;
        sheet.atlas = xml;
        sheet.image = index.exact(withoutExtension(xml) + ".png");
        sheet.scale = 0.7f;
        const int s = addSheet(style, sheet);
        bindBaseGameNotes(style, s, Engine::Psych, false);
        markLettered(style);
        catalog.styles.push_back(std::move(style));
    };
    // La textura de cada tipo: la del .txt se aplica al crear la nota
    // (Note.hx:229) y el script corre despues, con las notas ya creadas, asi
    // que manda el script.
    std::map<std::pair<std::string, std::string>, std::pair<std::string, std::string>> typeTextures;
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        const size_t seg = segmentAt(low, "custom_notetypes/");
        const bool config = endsWith(low, ".txt");
        if (seg == std::string::npos || !(endsWith(low, ".lua") || endsWith(low, ".hx") || config)) continue;
        if (low.find('/', seg + std::strlen("custom_notetypes/")) != std::string::npos) continue;
        const std::string scope = path.substr(0, seg);
        if (insideVSlice(scope)) continue;
        const auto text = vfs.readText(path);
        if (!text) continue;
        // Lua: setPropertyFromGroup('unspawnNotes', i, 'texture', '<ruta>');
        // HScript: note.texture = '<ruta>' o note.reloadNote('<ruta>');
        // .txt: texture: '<ruta>' (NoteTypesConfig.hx).
        std::string texture = quotedValue(config ? psychConfigValue(*text, "texture")
                                                 : scriptAssignment(*text, "texture"));
        const size_t call = texture.empty() && !config ? text->find("reloadNote(") : std::string::npos;
        if (call != std::string::npos) {
            const size_t from = call + std::strlen("reloadNote(");
            const size_t to = text->find_first_of(",)", from);
            if (to != std::string::npos) {
                std::string argument = text->substr(from, to - from);
                while (!argument.empty() && std::isspace(static_cast<unsigned char>(argument.back()))) argument.pop_back();
                while (!argument.empty() && std::isspace(static_cast<unsigned char>(argument.front()))) argument.erase(0, 1);
                texture = quotedValue(argument);
            }
        }
        if (texture.empty()) continue;
        auto& slot = typeTextures[{scope, stemOf(path)}];
        if (slot.first.empty() || !config) slot = {texture, path};
    }
    for (const auto& [key, value] : typeTextures) typeStyle(key.first, key.second, value.first, value.second);
    // La Hurt Note de serie. Desde 0.7 recolorea el skin normal en negro y rojo
    // (Note.hx:199-208) y salpica con noteSplashes-electric, con el rojo y el
    // verde que fija el tipo y el azul de la nota (Note.hx:211-213,
    // NoteSplash.hx:58-63 de 0.7). En 0.6 cambiaba la textura a HURTNOTE_assets.
    for (const auto& entry : defaultByScope) {
        const NoteStyle normal = catalog.styles[entry.second];   // copia: push_back mueve el vector
        const bool already = std::any_of(catalog.styles.begin(), catalog.styles.end(), [&](const NoteStyle& s) {
            return s.engine == Engine::Psych && s.use == StyleUse::NoteType && s.scope == normal.scope &&
                   lower(s.useDetail) == "hurt note";
        });
        if (already) continue;
        if (!normal.rgbPalette) {
            typeStyle(normal.scope, "Hurt Note", "HURTNOTE_assets", "Note.hx 0.6 (Hurt Note)");
            continue;
        }
        NoteStyle hurt;
        hurt.engine = Engine::Psych;
        hurt.scope = normal.scope;
        hurt.definition = "Note.hx (Hurt Note)";
        hurt.name = "Hurt Note";
        hurt.id = "psych:" + normal.scope + "notetype/Hurt Note";
        hurt.use = StyleUse::NoteType;
        hurt.useDetail = "Hurt Note";
        hurt.rgbPalette = true;
        hurt.letteredNaming = normal.letteredNaming;
        std::map<int, int> sheetOf;
        for (const PartBinding& binding : normal.parts) {
            if (binding.part != Part::Note && binding.part != Part::HoldPiece && binding.part != Part::HoldEnd) continue;
            if (binding.sheet < 0 || binding.sheet >= static_cast<int>(normal.sheets.size())) continue;
            auto mapped = sheetOf.find(binding.sheet);
            if (mapped == sheetOf.end()) {
                Sheet sheet = normal.sheets[static_cast<size_t>(binding.sheet)];
                sheet.rgbFixed = {0xFF101010u, 0xFFFF0000u, 0xFF990022u};
                mapped = sheetOf.emplace(binding.sheet, addSheet(hurt, sheet)).first;
            }
            PartBinding copy = binding;
            copy.sheet = mapped->second;
            hurt.parts.push_back(copy);
        }
        if (hurt.parts.empty()) continue;
        const std::string electric = index.find(normal.scope, "images/noteSplashes/noteSplashes-electric.xml");
        if (!electric.empty()) {
            const size_t firstSplash = hurt.sheets.size();
            readPsychSplash(vfs, index, atlases, electric, normal.scope, hurt, catalog);
            for (size_t i = firstSplash; i < hurt.sheets.size(); ++i)
                hurt.sheets[i].rgbFixed = {0xFFFF0000u, 0xFF101010u, 0xFF990022u};
        }
        catalog.styles.push_back(std::move(hurt));
    }
    // Un skin por letra delata un fork: el mismo ejecutable carga assets/ y
    // mods/, asi que sus tipos de nota siguen los nombres del fork.
    std::set<std::string> forkRoots;
    for (const NoteStyle& style : catalog.styles)
        if (style.engine == Engine::Psych && style.letteredNaming) forkRoots.insert(psychExecutableRoot(style.scope));
    for (NoteStyle& style : catalog.styles)
        if (style.engine == Engine::Psych && style.use == StyleUse::NoteType && !style.letteredNaming &&
            forkRoots.count(psychExecutableRoot(style.scope)))
            style.forkNaming = true;

    // Skins por cancion: `arrowSkin` y `splashSkin` del chart (Song.hx:31-32).
    std::map<std::string, size_t> songSkins;   // skin en minusculas -> estilo
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (!endsWith(low, ".json")) continue;
        const size_t dataSeg = segmentAt(low, "data/");
        if (dataSeg == std::string::npos) continue;
        if (segmentAt(low, "data/notestyles/") != std::string::npos) continue;
        const auto entry = vfs.find(path);
        if (!entry || entry->size > 32ull * 1024ull * 1024ull) continue;
        const auto text = vfs.readText(*entry);
        if (!text || text->find("arrowSkin") == std::string::npos) continue;
        const json doc = json::parse(*text, nullptr, false, true);
        if (!doc.is_object()) continue;
        const json& song = doc.contains("song") && doc["song"].is_object() ? doc["song"] : doc;
        const std::string arrow = song.contains("arrowSkin") && song["arrowSkin"].is_string()
                                      ? song["arrowSkin"].get<std::string>() : std::string();
        if (arrow.size() <= 1) continue;   // StrumNote.hx:55 exige mas de un caracter
        const std::string lowArrow = lower(arrow);
        if (lowArrow == "notes/note_assets" || lowArrow == "noteskins/note_assets") continue;
        std::string songName = song.contains("song") && song["song"].is_string()
                                   ? song["song"].get<std::string>() : stemOf(path);
        auto found = songSkins.find(lowArrow);
        if (found == songSkins.end()) {
            const std::string scope = path.substr(0, dataSeg);
            const std::string xml = index.find(scope, "images/" + arrow + ".xml");
            NoteStyle style = psychSkin(index, xml, scope, arrow, true);
            markLettered(style);
            style.id = "psych:" + scope + "song/" + arrow;
            style.name = stemOf(arrow);
            style.definition = path;
            style.use = StyleUse::Song;
            if (xml.empty()) {
                style.sheets[0].image = index.find(scope, "images/" + arrow + ".png");
                catalog.findings.push_back(finding(Severity::Error, "FML-NOTE-003", style.id, "sheet",
                                                   path, arrow));
            }
            if (song.contains("splashSkin") && song["splashSkin"].is_string()) {
                const std::string splash = song["splashSkin"].get<std::string>();
                const std::string splashXml = splash.empty() ? std::string()
                                                             : index.find(scope, "images/" + splash + ".xml");
                if (!splashXml.empty()) readPsychSplash(vfs, index, atlases, splashXml, scope, style, catalog);
            }
            found = songSkins.emplace(lowArrow, catalog.styles.size()).first;
            catalog.styles.push_back(std::move(style));
        }
        NoteStyle& style = catalog.styles[found->second];
        const std::string detail = style.useDetail.empty() ? songName : style.useDetail + ", " + songName;
        if (style.useDetail.find(songName) == std::string::npos && style.useDetail.size() < 240)
            style.useDetail = detail;
    }
}

// ----------------------------------------------------------------- V-Slice --

struct VSliceDeclared {
    std::set<std::string> groups;
    std::array<bool, 4> countdown{};
    std::array<bool, 4> judgements{};
    std::array<bool, 10> digits{};
};

std::string resolveVSlice(const AssetIndex& index, const std::string& scope, const std::string& declared,
                          const char* folder, const char* extension) {
    if (declared.empty()) return {};
    for (const std::string& rel : vsliceCandidates(declared, folder, extension)) {
        const std::string direct = index.exact(scope + rel);
        if (!direct.empty()) return direct;
    }
    for (const std::string& rel : vsliceCandidates(declared, folder, extension)) {
        const std::string anywhere = index.find(scope, rel);
        if (!anywhere.empty()) return anywhere;
    }
    return {};
}

std::string resolveVSliceSound(const AssetIndex& index, const std::string& scope, const std::string& declared) {
    for (const char* ext : {".ogg", ".mp3", ".wav"}) {
        const std::string found = resolveVSlice(index, scope, declared, "sounds", ext);
        if (!found.empty()) return found;
    }
    return {};
}

std::string jsonString(const json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_string()) return {};
    return object[key].get<std::string>();
}

float jsonFloat(const json& object, const char* key, float fallback) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number()) return fallback;
    return object[key].get<float>();
}

bool jsonBool(const json& object, const char* key, bool fallback) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_boolean()) return fallback;
    return object[key].get<bool>();
}

// UnnamedAnimationData (AnimationData.hx): prefix, offsets, looped, frameRate 24, frameIndices.
Animation vsliceAnimation(const json& data) {
    Animation anim;
    anim.prefix = jsonString(data, "prefix");
    anim.fps = jsonFloat(data, "frameRate", 24.0f);
    anim.loop = jsonBool(data, "looped", false);
    if (data.is_object() && data.contains("offsets") && data["offsets"].is_array() &&
        data["offsets"].size() >= 2 && data["offsets"][0].is_number() && data["offsets"][1].is_number()) {
        anim.offsetX = data["offsets"][0].get<float>();
        anim.offsetY = data["offsets"][1].get<float>();
    }
    if (data.is_object() && data.contains("frameIndices") && data["frameIndices"].is_array())
        for (const json& i : data["frameIndices"]) if (i.is_number_integer()) anim.indices.push_back(i.get<int>());
    return anim;
}

Sheet vsliceSheet(const AssetIndex& index, const std::string& scope, const json& asset,
                  const std::string& declared, SheetKind kind) {
    Sheet sheet;
    sheet.kind = kind;
    sheet.declared = declared;
    sheet.image = resolveVSlice(index, scope, declared, "images", ".png");
    if (kind == SheetKind::Sparrow) sheet.atlas = resolveVSlice(index, scope, declared, "images", ".xml");
    sheet.scale = jsonFloat(asset, "scale", 1.0f);
    sheet.alpha = jsonFloat(asset, "alpha", 1.0f);
    sheet.pixel = jsonBool(asset, "isPixel", false);
    if (asset.is_object() && asset.contains("offsets") && asset["offsets"].is_array() &&
        asset["offsets"].size() >= 2 && asset["offsets"][0].is_number() && asset["offsets"][1].is_number()) {
        sheet.offsetX = asset["offsets"][0].get<float>();
        sheet.offsetY = asset["offsets"][1].get<float>();
    }
    return sheet;
}

void readVSliceHud(const AssetIndex& index, const std::string& scope, const json& asset, HudAsset& hud) {
    hud.declared = jsonString(asset, "assetPath");
    hud.imageOptional = asset.is_object() && asset.contains("assetPath") && asset["assetPath"].is_null();
    hud.image = resolveVSlice(index, scope, hud.declared, "images", ".png");
    if (hud.image.empty() && !hud.declared.empty()) {
        const size_t colon = hud.declared.find(':');
        hud.nearby = index.sameName(scope, fileName(colon == std::string::npos ? hud.declared
                                                                               : hud.declared.substr(colon + 1)) + ".png");
    }
    hud.scale = jsonFloat(asset, "scale", 1.0f);
    hud.pixel = jsonBool(asset, "isPixel", false);
    if (asset.is_object() && asset.contains("data") && asset["data"].is_object()) {
        hud.soundDeclared = jsonString(asset["data"], "audioPath");
        hud.sound = resolveVSliceSound(index, scope, hud.soundDeclared);
    }
}

const char* partGroup(Part part) {
    switch (part) {
        case Part::Note: return "note";
        case Part::HoldPiece: case Part::HoldEnd: return "holdNote";
        case Part::StrumStatic: case Part::StrumPress: case Part::StrumConfirm:
        case Part::StrumConfirmHold: return "noteStrumline";
        case Part::Splash: return "noteSplash";
        case Part::HoldCoverStart: case Part::HoldCover: case Part::HoldCoverEnd: return "holdNoteCover";
    }
    return "";
}

void readVSlice(const Vfs& vfs, const AssetIndex& index, Catalog& catalog) {
    static const std::string kStyles = "data/notestyles/";
    std::vector<std::pair<size_t, VSliceDeclared>> declaredByStyle;
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (!endsWith(low, ".json")) continue;
        size_t seg = segmentAt(low, kStyles);
        if (seg == std::string::npos || low.find('/', seg + kStyles.size()) != std::string::npos) {
            // La 0.9 de prueba: gameplay/notestyles/<id>/<id>.json, con sus
            // imagenes en la misma carpeta y las rutas desde assets/.
            static const std::string kGameplay = "gameplay/notestyles/";
            seg = segmentAt(low, kGameplay);
            if (seg == std::string::npos) continue;
            const size_t folderEnd = low.find('/', seg + kGameplay.size());
            if (folderEnd == std::string::npos || low.find('/', folderEnd + 1) != std::string::npos) continue;
        }
        const std::string scope = path.substr(0, seg);
        const std::string stem = stemOf(path);
        const auto text = vfs.readText(path);
        const json doc = text ? json::parse(*text, nullptr, false, true) : json();
        const std::string id = "vslice:" + scope + stem;
        if (!doc.is_object() || !doc.contains("assets") || !doc["assets"].is_object()) {
            // manifest.json y otros JSON sin `assets` no son estilos; solo se
            // avisa cuando parecia uno y no se pudo leer.
            if (!doc.is_object() && lower(stem) != "manifest")
                catalog.findings.push_back(finding(Severity::Error, "FML-NOTE-010", id, "definition", path, "json"));
            continue;
        }
        NoteStyle style;
        style.engine = Engine::VSlice;
        style.scope = scope;
        style.definition = path;
        style.id = id;
        style.name = jsonString(doc, "name").empty() ? stem : jsonString(doc, "name");
        style.author = jsonString(doc, "author");
        style.fallback = jsonString(doc, "fallback");
        style.use = StyleUse::Declared;
        style.useDetail = stem;
        VSliceDeclared declared;
        const json& assets = doc["assets"];

        if (assets.contains("note") && assets["note"].is_object()) {
            declared.groups.insert("note");
            const json& a = assets["note"];
            const Sheet sheet = vsliceSheet(index, scope, a, jsonString(a, "assetPath"), SheetKind::Sparrow);
            style.pixel = style.pixel || sheet.pixel;
            const int s = addSheet(style, sheet);
            const json data = a.contains("data") ? a["data"] : json::object();
            for (int d = 0; d < 4; ++d)
                bindPart(style, Part::Note, d, s, vsliceAnimation(data.contains(directionKey(d)) ? data[directionKey(d)] : json()));
        }
        if (assets.contains("holdNote") && assets["holdNote"].is_object()) {
            declared.groups.insert("holdNote");
            const json& a = assets["holdNote"];
            Sheet sheet = vsliceSheet(index, scope, a, jsonString(a, "assetPath"), SheetKind::Strip);
            // Ocho columnas: pieza y final de cada direccion (SustainTrail.hx:231, :354-359).
            sheet.columns = 8;
            sheet.rows = 1;
            const int s = addSheet(style, sheet);
            for (int d = 0; d < 4; ++d) {
                Animation piece;
                piece.indices = {d * 2};
                bindPart(style, Part::HoldPiece, d, s, piece);
                Animation end;
                end.indices = {d * 2 + 1};
                bindPart(style, Part::HoldEnd, d, s, end);
            }
        }
        if (assets.contains("noteStrumline") && assets["noteStrumline"].is_object()) {
            declared.groups.insert("noteStrumline");
            const json& a = assets["noteStrumline"];
            const int s = addSheet(style, vsliceSheet(index, scope, a, jsonString(a, "assetPath"), SheetKind::Sparrow));
            const json data = a.contains("data") ? a["data"] : json::object();
            static const std::pair<Part, const char*> states[] = {
                {Part::StrumStatic, "Static"}, {Part::StrumPress, "Press"},
                {Part::StrumConfirm, "Confirm"}, {Part::StrumConfirmHold, "ConfirmHold"}};
            for (int d = 0; d < 4; ++d)
                for (const auto& state : states) {
                    const std::string key = std::string(directionKey(d)) + state.second;
                    bindPart(style, state.first, d, s, vsliceAnimation(data.contains(key) ? data[key] : json()));
                }
        }
        if (assets.contains("noteSplash") && assets["noteSplash"].is_object()) {
            declared.groups.insert("noteSplash");
            const json& a = assets["noteSplash"];
            const json data = a.contains("data") ? a["data"] : json::object();
            if (jsonBool(data, "enabled", true)) {
                const int s = addSheet(style, vsliceSheet(index, scope, a, jsonString(a, "assetPath"), SheetKind::Sparrow));
                const float fps = jsonFloat(data, "framerateDefault", 24.0f);
                for (int d = 0; d < 4; ++d) {
                    const std::string key = std::string(directionKey(d)) + "Splashes";
                    if (!data.contains(key) || !data[key].is_array()) continue;
                    int variant = 0;
                    for (const json& entry : data[key]) {
                        Animation anim = vsliceAnimation(entry);
                        if (!entry.contains("frameRate")) anim.fps = fps;
                        int sheet = s;
                        const std::string own = jsonString(entry, "assetPath");
                        if (!own.empty()) sheet = addSheet(style, vsliceSheet(index, scope, a, own, SheetKind::Sparrow));
                        bindPart(style, Part::Splash, d, sheet, anim, variant++);
                    }
                }
            }
        }
        if (assets.contains("holdNoteCover") && assets["holdNoteCover"].is_object()) {
            declared.groups.insert("holdNoteCover");
            const json& a = assets["holdNoteCover"];
            const json data = a.contains("data") ? a["data"] : json::object();
            if (jsonBool(data, "enabled", true)) {
                const std::string main = jsonString(a, "assetPath");
                for (int d = 0; d < 4; ++d) {
                    if (!data.contains(directionKey(d)) || !data[directionKey(d)].is_object()) continue;
                    const json& dir = data[directionKey(d)];
                    const std::string own = jsonString(dir, "assetPath");
                    const int s = addSheet(style, vsliceSheet(index, scope, a, own.empty() ? main : own, SheetKind::Sparrow));
                    if (dir.contains("start")) bindPart(style, Part::HoldCoverStart, d, s, vsliceAnimation(dir["start"]));
                    if (dir.contains("hold")) bindPart(style, Part::HoldCover, d, s, vsliceAnimation(dir["hold"]));
                    if (dir.contains("end")) bindPart(style, Part::HoldCoverEnd, d, s, vsliceAnimation(dir["end"]));
                }
            }
        }
        static const char* countdownKeys[4] = {"countdownThree", "countdownTwo", "countdownOne", "countdownGo"};
        static const char* judgementKeys[4] = {"judgementSick", "judgementGood", "judgementBad", "judgementShit"};
        for (int i = 0; i < 4; ++i) {
            if (assets.contains(countdownKeys[i]) && assets[countdownKeys[i]].is_object()) {
                declared.countdown[i] = true;
                readVSliceHud(index, scope, assets[countdownKeys[i]], style.countdown[i]);
            }
            if (assets.contains(judgementKeys[i]) && assets[judgementKeys[i]].is_object()) {
                declared.judgements[i] = true;
                readVSliceHud(index, scope, assets[judgementKeys[i]], style.judgements[i]);
            }
        }
        for (int i = 0; i < 10; ++i) {
            const std::string key = "comboNumber" + std::to_string(i);
            if (assets.contains(key) && assets[key].is_object()) {
                declared.digits[i] = true;
                readVSliceHud(index, scope, assets[key], style.digits[i]);
            }
        }
        style.hasHud = true;
        declaredByStyle.emplace_back(catalog.styles.size(), declared);
        catalog.styles.push_back(std::move(style));
    }

    // `fallback`: lo que un estilo no declara lo pone el otro, por grupos enteros
    // (NoteStyleData.hx:35). Primero el mismo scope, despues cualquiera.
    std::map<size_t, const VSliceDeclared*> declaredOf;
    for (const auto& entry : declaredByStyle) declaredOf[entry.first] = &entry.second;
    auto findStyle = [&](const std::string& name, const std::string& scope) -> long long {
        long long any = -1;
        for (const auto& entry : declaredByStyle) {
            const NoteStyle& candidate = catalog.styles[entry.first];
            if (lower(stemOf(candidate.definition)) != lower(name)) continue;
            if (candidate.scope == scope) return static_cast<long long>(entry.first);
            if (any < 0) any = static_cast<long long>(entry.first);
        }
        return any;
    };
    std::set<size_t> done, visiting;
    std::function<void(size_t)> resolve = [&](size_t i) {
        if (done.count(i) || visiting.count(i)) return;
        visiting.insert(i);
        NoteStyle& style = catalog.styles[i];
        if (!style.fallback.empty()) {
            const long long target = findStyle(style.fallback, style.scope);
            if (target < 0) {
                catalog.findings.push_back(finding(Severity::Warning, "FML-NOTE-005", style.id, "fallback",
                                                   style.definition, style.fallback));
            } else if (static_cast<size_t>(target) != i && !visiting.count(static_cast<size_t>(target))) {
                resolve(static_cast<size_t>(target));
                const NoteStyle& from = catalog.styles[static_cast<size_t>(target)];
                NoteStyle& to = catalog.styles[i];
                const VSliceDeclared& own = *declaredOf[i];
                for (const PartBinding& binding : from.parts) {
                    if (own.groups.count(partGroup(binding.part))) continue;
                    PartBinding copy = binding;
                    copy.sheet = binding.sheet >= 0 ? addSheet(to, from.sheets[static_cast<size_t>(binding.sheet)]) : -1;
                    copy.inherited = true;
                    to.parts.push_back(std::move(copy));
                }
                for (int k = 0; k < 4; ++k) {
                    if (!own.countdown[k]) { to.countdown[k] = from.countdown[k]; to.countdown[k].inherited = true; }
                    if (!own.judgements[k]) { to.judgements[k] = from.judgements[k]; to.judgements[k].inherited = true; }
                }
                for (int k = 0; k < 10; ++k)
                    if (!own.digits[k]) { to.digits[k] = from.digits[k]; to.digits[k].inherited = true; }
                to.pixel = to.pixel || from.pixel;
            }
        }
        visiting.erase(i);
        done.insert(i);
    };
    for (const auto& entry : declaredByStyle) resolve(entry.first);
}

// ------------------------------------------ Codename: imagenes de game/notes --

// Una imagen de game/notes/ es el aspecto de un tipo de nota cuando algo la
// pide como tal: el motor la carga para las notas de ese tipo (Note.hx:156-158)
// y el tipo sale del chart (`noteTypes`, ChartData.hx:12, PlayState.hx:797) o
// tiene su script en data/notes/. Una que nadie pide asi y que no trae los
// nombres del juego base es otra cosa: el sprite de una salpicadura
// (`<splashes sprite="game/notes/Smoke">`) o un skin que pone el script de una
// cancion. No es un tipo: el motor no la carga por si solo y sus fallos se
// dicen como informacion (FML-NOTE-015), no como errores de un tipo.
// Los tipos que piden los charts de Codename: su lista `noteTypes`
// (ChartData.hx:12); PlayState.hx:797 carga el script de cada uno.
std::vector<std::string> codenameChartTypes(const Vfs& vfs, const AssetIndex& index) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (!endsWith(low, ".json") || segmentAt(low, "songs/") == std::string::npos ||
            low.find("/charts/") == std::string::npos) continue;
        const auto text = vfs.readText(path);
        const size_t key = text ? text->find("\"noteTypes\"") : std::string::npos;
        if (key == std::string::npos) continue;
        const size_t open = text->find('[', key);
        const size_t close = open == std::string::npos ? std::string::npos : text->find(']', open);
        if (close == std::string::npos) continue;
        const json list = json::parse(text->substr(open, close - open + 1), nullptr, false);
        if (!list.is_array()) continue;
        for (const json& item : list)
            if (item.is_string() && !item.get<std::string>().empty() && seen.insert(lower(item.get<std::string>())).second)
                out.push_back(item.get<std::string>());
    }
    return out;
}

// Codename: el aspecto de un tipo que pone un script general con una ruta
// hecha, `event.noteSprite = "game/voiid/notes/" + skin`: el motor carga
// `images/<noteSprite>` (NoteCreationEvent; Note.hx:156-158). El script no se
// ejecuta, asi que no se sabe que nombre pone: se busca en esa carpeta la hoja
// que se llama como el tipo o, si no hay, la mas corta que empieza por el, y
// se dice (FML-NOTE-026). Una ruta fija la lee el lector del script del tipo.
// Codename con multikey: el mod declara en `data/multikeyData.xml`
// (`<!DOCTYPE multikey-data>`) los nombres de cada tecla por numero de teclas;
// el grupo de 4 da los de las cuatro direcciones (note, noteHold,
// noteHoldEnd, strumStatic, strumPress, strumConfirm). Es dato del mod, como
// el notestyle de V-Slice.
struct MultikeyNames {
    std::string path;
    std::array<std::array<std::string, 6>, 4> names{};   // por direccion: nota, tramo, final, reposo, pulsado, acierto
    bool found() const { return !path.empty(); }
};

MultikeyNames readMultikeyNames(const Vfs& vfs, const AssetIndex& index) {
    MultikeyNames out;
    std::string path;
    for (const std::string& file : index.files())
        if (endsWith(lower(file), "data/multikeydata.xml") && !insideHiddenFolder(lower(file)) &&
            (path.empty() || file.size() < path.size()))
            path = file;
    if (path.empty()) return out;
    const auto text = vfs.readText(path);
    if (!text) return out;
    pugi::xml_document doc;
    if (!doc.load_string(text->c_str())) return out;
    for (const pugi::xml_node group : doc.document_element().children("keyGroup")) {
        std::vector<pugi::xml_node> keys;
        for (const pugi::xml_node key : group.children("key")) keys.push_back(key);
        if (keys.size() != 4) continue;
        static const char* attributes[6] = {"note", "noteHold", "noteHoldEnd", "strumStatic", "strumPress", "strumConfirm"};
        for (size_t d = 0; d < 4; ++d)
            for (size_t a = 0; a < 6; ++a) out.names[d][a] = keys[d].attribute(attributes[a]).as_string();
        if (out.names[0][0].empty()) return out;
        out.path = path;
        return out;
    }
    return out;
}

// Una hoja de nota que no trae los nombres del juego base pero si los de
// multikey del mod se lee con estos.
bool bindMultikeyNotes(const Vfs& vfs, AtlasCache& atlases, const MultikeyNames& multikey, NoteStyle& style, int sheet) {
    if (!multikey.found() || sheet < 0 || static_cast<size_t>(sheet) >= style.sheets.size()) return false;
    const std::vector<std::string>* names = atlases.names(vfs, style.sheets[static_cast<size_t>(sheet)].atlas);
    if (!names) return false;
    bool baseNames = false;
    for (const char* color : kColors) baseNames = baseNames || countPrefix(*names, std::string(color) + "0") > 0;
    if (baseNames || countPrefix(*names, multikey.names[0][0]) == 0) return false;
    style.parts.erase(std::remove_if(style.parts.begin(), style.parts.end(), [&](const PartBinding& b) {
        return b.sheet == sheet && (b.part == Part::Note || b.part == Part::HoldPiece || b.part == Part::HoldEnd);
    }), style.parts.end());
    static const Part parts[3] = {Part::Note, Part::HoldPiece, Part::HoldEnd};
    for (int d = 0; d < 4; ++d)
        for (size_t p = 0; p < 3; ++p) {
            Animation anim;
            anim.prefix = multikey.names[static_cast<size_t>(d)][p];
            if (!anim.prefix.empty()) bindPart(style, parts[p], d, sheet, anim);
        }
    style.multikeyData = multikey.path;
    return true;
}

void readCodenameScriptSprites(const Vfs& vfs, const AssetIndex& index, AtlasCache& atlases, Catalog& catalog) {
    const MultikeyNames multikey = readMultikeyNames(vfs, index);
    std::map<std::string, std::string> folders;   // carpeta -> script que la pone
    std::map<std::string, std::pair<std::string, std::string>> skins;
    for (const std::string& path : index.files()) {
        const std::string low = lower(path);
        if (insideHiddenFolder(low)) continue;
        if (!endsWith(low, ".hx") && !endsWith(low, ".hscript") && !endsWith(low, ".hsc") && !endsWith(low, ".hxs")) continue;
        const auto text = vfs.readText(path);
        if (!text || text->size() > 2u * 1024u * 1024u) continue;
        const std::string s = withoutComments(*text);
        size_t registry = 0;
        while ((registry = s.find(".set(", registry)) != std::string::npos) {
            size_t keyAt = registry + 5; registry = keyAt;
            while (keyAt < s.size() && std::isspace(static_cast<unsigned char>(s[keyAt]))) ++keyAt;
            if (keyAt >= s.size() || (s[keyAt] != '"' && s[keyAt] != '\'')) continue;
            const size_t endKey = s.find(s[keyAt], keyAt + 1);
            if (endKey == std::string::npos) continue;
            const size_t begin = s.find('{', endKey), end = begin == std::string::npos ? std::string::npos : s.find('}', begin);
            const size_t close = s.find(')', endKey);
            if (begin == std::string::npos || end == std::string::npos || (close != std::string::npos && close < begin) || end - begin > 4096) continue;
            const std::string body = s.substr(begin + 1, end - begin - 1);
            size_t skinAt = body.find("skin");
            if (skinAt == std::string::npos || (skinAt > 0 && (std::isalnum(static_cast<unsigned char>(body[skinAt - 1])) || body[skinAt - 1] == '_'))) continue;
            skinAt += 4;
            while (skinAt < body.size() && std::isspace(static_cast<unsigned char>(body[skinAt]))) ++skinAt;
            if (skinAt >= body.size() || body[skinAt++] != ':') continue;
            while (skinAt < body.size() && std::isspace(static_cast<unsigned char>(body[skinAt]))) ++skinAt;
            if (skinAt >= body.size() || (body[skinAt] != '"' && body[skinAt] != '\'')) continue;
            const size_t skinEnd = body.find(body[skinAt], skinAt + 1);
            if (skinEnd == std::string::npos) continue;
            const std::string name = s.substr(keyAt + 1, endKey - keyAt - 1), skin = body.substr(skinAt + 1, skinEnd - skinAt - 1);
            if (name.empty() || skin.empty() || skins.size() >= 1024) continue;
            skins.emplace(lower(name), std::make_pair(skin, path));
            if (std::none_of(catalog.chartTypes.begin(), catalog.chartTypes.end(), [&](const std::string& t) { return lower(t) == lower(name); })) catalog.chartTypes.push_back(name);
            registry = end + 1;
        }
        size_t at = 0;
        while ((at = s.find("noteSprite", at)) != std::string::npos) {
            size_t i = at + 10;
            at = i;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            if (i + 1 >= s.size() || s[i] != '=' || s[i + 1] == '=') continue;
            ++i;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) continue;
            const size_t end = s.find(s[i], i + 1);
            if (end == std::string::npos) continue;
            const std::string literal = s.substr(i + 1, end - i - 1);
            size_t j = end + 1;
            while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
            if (j >= s.size() || s[j] != '+' || literal.empty() || literal.back() != '/') continue;
            folders.emplace(literal, path);
        }
    }
    if (folders.empty()) return;
    std::set<std::string> known;
    for (const NoteStyle& style : catalog.styles)
        if (style.engine == Engine::Codename && style.use == StyleUse::NoteType) known.insert(lower(style.useDetail));
    for (const std::string& type : catalog.chartTypes) {
        if (known.count(lower(type))) continue;
        // Con su propio script en data/notes/ manda el lector de ese script.
        if (!codenameTypeScript(index, "", type).empty()) continue;
        std::string found, script;
        const auto registered = skins.find(lower(type));
        const std::string sprite = registered == skins.end() ? type : registered->second.first;
        for (const auto& [folder, from] : folders) {
            const std::string exact = index.find("", "images/" + folder + sprite + ".xml");
            if (!exact.empty()) {
                found = exact;
                script = registered == skins.end() ? from : registered->second.second;
                break;
            }
        }
        if (found.empty())
            for (const auto& [folder, from] : folders) {
                const std::string start = lower("images/" + folder + sprite);
                for (const std::string& path : index.files()) {
                    const std::string low = lower(path);
                    const size_t seg = segmentAt(low, start);
                    if (seg == std::string::npos || !endsWith(low, ".xml") || low.find('/', seg + start.size()) != std::string::npos) continue;
                    if (found.empty() || path.size() < found.size()) {
                        found = path;
                        script = from;
                    }
                }
            }
        if (found.empty()) continue;
        const size_t images = segmentAt(lower(found), "images/");
        // Con su propio script en data/notes/ (en la carpeta de su mod) manda
        // el lector de ese script.
        if (!codenameTypeScript(index, found.substr(0, images), type).empty()) continue;
        NoteStyle style;
        style.engine = Engine::Codename;
        style.scope = found.substr(0, images);
        style.definition = found;
        style.name = type;
        style.id = "codename:" + style.scope + "script:" + type;
        style.use = StyleUse::NoteType;
        style.useDetail = type;
        style.nameScript = script;
        Sheet sheet;
        sheet.kind = SheetKind::Sparrow;
        sheet.declared = withoutExtension(found.substr(images + 7));
        sheet.atlas = found;
        sheet.image = index.exact(withoutExtension(found) + ".png");
        sheet.scale = 0.7f;   // Flags.DEFAULT_NOTE_SCALE (Flags.hx:139)
        const int s = addSheet(style, sheet);
        bindBaseGameNotes(style, s, Engine::Codename, false);
        // Solo si la hoja trae nombres que se sepan leer: los del juego base o
        // los de multikey del mod. Si no, sus animaciones las pone otro script
        // y el tipo se queda como estaba, con el skin.
        if (!bindMultikeyNotes(vfs, atlases, multikey, style, s)) {
            const std::vector<std::string>* names = atlases.names(vfs, sheet.atlas);
            bool baseNames = false;
            if (names)
                for (const char* color : kColors) baseNames = baseNames || countPrefix(*names, std::string(color) + "0") > 0;
            if (!baseNames) continue;
        }
        catalog.styles.push_back(std::move(style));
        known.insert(lower(type));
    }
}

void settleCodenameImages(const Vfs& vfs, const AssetIndex& index, AtlasCache& atlases, Catalog& catalog) {
    const bool anyType = std::any_of(catalog.styles.begin(), catalog.styles.end(), [](const NoteStyle& style) {
        return style.engine == Engine::Codename && style.use == StyleUse::NoteType;
    });
    if (!anyType) return;
    std::set<std::string> chartTypes;
    for (const std::string& type : catalog.chartTypes) chartTypes.insert(lower(type));
    std::set<std::string> splashImages;
    for (const NoteStyle& style : catalog.styles) {
        if (style.engine != Engine::Codename) continue;
        for (const PartBinding& binding : style.parts)
            if (binding.part == Part::Splash && binding.sheet >= 0 && static_cast<size_t>(binding.sheet) < style.sheets.size())
                splashImages.insert(lower(style.sheets[static_cast<size_t>(binding.sheet)].image));
    }
    for (NoteStyle& style : catalog.styles) {
        if (style.engine != Engine::Codename || style.use != StyleUse::NoteType || style.sheets.empty()) continue;
        if (chartTypes.count(lower(style.useDetail)) || !style.lookScript.empty() ||
            !codenameTypeScript(index, style.scope, style.useDetail).empty()) continue;
        const Sheet& sheet = style.sheets[0];
        const bool splash = !sheet.image.empty() && splashImages.count(lower(sheet.image)) > 0;
        if (!splash)
            if (const std::vector<std::string>* names = atlases.names(vfs, sheet.atlas)) {
                bool baseNames = false;
                for (const char* color : kColors) baseNames = baseNames || countPrefix(*names, std::string(color) + "0") > 0;
                if (baseNames) continue;   // lista para ponerla en un chart
            }
        style.use = StyleUse::Declared;
        style.referenced = false;
    }
}

}  // namespace

AssetIndex::AssetIndex(const Vfs& vfs) {
    for (const Vfs::Entry& entry : vfs.allEntries()) {
        if (entry.isDir) continue;
        const std::string path = Vfs::normalize(entry.virtualPath);
        const std::string low = lower(path);
        if (insideHiddenFolder(low)) continue;
        if (!m_byLower.emplace(low, path).second) continue;
        m_files.push_back(path);
        m_byName[fileName(low)].push_back(path);
        for (const char* segment : {"images/", "sounds/"}) {
            size_t pos = 0;
            while ((pos = low.find(segment, pos)) != std::string::npos) {
                if (pos == 0 || low[pos - 1] == '/') m_bySegment[low.substr(pos)].push_back(path);
                ++pos;
            }
        }
    }
    std::sort(m_files.begin(), m_files.end());
}

std::string AssetIndex::exact(const std::string& virtualPath) const {
    const auto it = m_byLower.find(lower(Vfs::normalize(virtualPath)));
    return it == m_byLower.end() ? std::string() : it->second;
}

std::string AssetIndex::find(const std::string& scope, const std::string& rel) const {
    if (rel.empty()) return {};
    const std::string direct = exact(scope + rel);
    if (!direct.empty()) return direct;
    const std::string lowRel = lower(Vfs::normalize(rel));
    size_t seg = segmentAt(lowRel, "images/");
    if (seg == std::string::npos) seg = segmentAt(lowRel, "sounds/");
    if (seg == std::string::npos) return {};
    const auto it = m_bySegment.find(lowRel.substr(seg));
    if (it == m_bySegment.end()) return {};
    std::string best;
    for (const std::string& candidate : it->second) {
        const std::string low = lower(candidate);
        if (!endsWith(low, lowRel)) continue;
        const size_t at = low.size() - lowRel.size();
        if (at > 0 && low[at - 1] != '/') continue;
        if (best.empty() || candidate.size() < best.size() ||
            (candidate.size() == best.size() && candidate < best))
            best = candidate;
    }
    return best;
}

std::string AssetIndex::sameName(const std::string& scope, const std::string& name) const {
    const auto it = m_byName.find(lower(name));
    if (it == m_byName.end()) return {};
    const std::string lowScope = lower(scope);
    const size_t slash = lowScope.find('/');
    const std::string top = slash == std::string::npos ? std::string() : lowScope.substr(0, slash + 1);
    std::string best;
    for (const std::string& candidate : it->second) {
        if (!top.empty() && !startsWith(lower(candidate), top)) continue;
        if (best.empty() || candidate.size() < best.size() ||
            (candidate.size() == best.size() && candidate < best))
            best = candidate;
    }
    return best;
}

std::string AssetIndex::findSound(const std::string& scope, const std::string& relNoExt) const {
    for (const char* ext : {".ogg", ".mp3", ".wav"}) {
        const std::string found = find(scope, relNoExt + ext);
        if (!found.empty()) return found;
    }
    return {};
}

std::vector<std::string> vsliceCandidates(const std::string& declared, const char* folder,
                                          const char* extension) {
    std::string library;
    std::string path = declared;
    const size_t colon = declared.find(':');
    if (colon != std::string::npos) {
        library = declared.substr(0, colon);
        path = declared.substr(colon + 1);
    }
    const std::string tail = std::string(folder) + "/" + path + extension;
    // La 0.9 de prueba declara rutas desde assets/ sin images/ ni sounds/
    // («gameplay/notestyles/funkin/notes»), con los archivos junto al estilo.
    if (library.empty()) return {"shared/" + tail, tail, "preload/" + tail, path + extension};
    if (lower(library) == "default") return {tail};
    return {library + "/" + tail};
}

const std::vector<std::string>* AtlasCache::names(const Vfs& vfs, const std::string& atlasPath) {
    if (atlasPath.empty()) return nullptr;
    const auto hit = m_names.find(atlasPath);
    if (hit != m_names.end()) return &hit->second;
    if (m_failed.count(atlasPath)) return nullptr;
    const auto text = vfs.readText(atlasPath);
    if (!text) {
        m_failed[atlasPath] = true;
        return nullptr;
    }
    DiagnosticSink sink;
    const bool packer = endsWith(lower(atlasPath), ".txt");
    const Result<SparrowAtlas> atlas = packer ? parsePackerAtlas(*text, atlasPath, sink)
                                              : parseSparrowAtlas(*text, atlasPath, sink);
    if (!atlas) {
        m_failed[atlasPath] = true;
        return nullptr;
    }
    std::vector<std::string> list;
    list.reserve(atlas.value().frames.size());
    for (const AtlasFrame& frame : atlas.value().frames) list.push_back(frame.name);
    return &(m_names[atlasPath] = std::move(list));
}

int countPrefix(const std::vector<std::string>& names, const std::string& prefix) {
    if (prefix.empty()) return 0;
    int count = 0;
    for (const std::string& name : names)
        if (startsWith(name, prefix)) ++count;
    return count;
}

EngineGuess guessEngine(const Vfs& vfs, const Catalog& catalog) {
    struct Marker { int engine; int weight; const char* segment; const char* label; bool file; };
    // Estructura propia de cada motor; `file` = el tramo es un nombre de archivo.
    static const Marker markers[] = {
        {0, 4, "images/game/notes/", "images/game/notes", false},
        {0, 3, "data/notes/", "data/notes", false},
        {0, 2, "data/splashes/", "data/splashes", false},
        {0, 2, "data/stages/", "data/stages", false},
        {0, 2, "songs/", "songs/<song>/charts", false},
        {1, 4, "images/noteskins/", "images/noteSkins", false},
        {1, 3, "custom_notetypes/", "custom_notetypes", false},
        {1, 2, "custom_events/", "custom_events", false},
        {1, 2, "weeks/", "weeks", false},
        {1, 1, "pack.json", "pack.json", true},
        {2, 5, "_polymod_meta.json", "_polymod_meta.json", true},
        {2, 3, "data/notestyles/", "data/notestyles", false},
        {2, 2, "scripts/notekinds/", "scripts/notekinds", false},
        // La 0.9 de prueba guarda cada notestyle en su carpeta, con sus
        // imagenes, y los NoteKind junto a ellos.
        {2, 3, "gameplay/notestyles/", "gameplay/notestyles", false},
        {2, 2, "gameplay/notekinds/", "gameplay/notekinds", false},
    };
    // Motores que no son ninguno de los tres, por sus marcas propias. Leather
    // y Kade tambien usan Polymod (`_polymod_meta.json`), asi que sin esto se
    // tomaban por V-Slice o Psych y daban errores que no eran. Note Lab no los
    // abre: solo es para los tres.
    struct OtherMarker { const char* engine; int weight; const char* segment; const char* label; bool file; };
    static const OtherMarker others[] = {
        // Leather Engine (github.com/Leather128/LeatherEngine; su build en
        // Voiid Chronicles 2.0.2): carpetas de datos con espacio.
        {"Leather Engine", 4, "data/song data/", "data/song data", false},
        {"Leather Engine", 4, "data/ui skins/", "data/ui skins", false},
        {"Leather Engine", 3, "data/arrow types/", "data/arrow types", false},
        {"Leather Engine", 2, "data/mania data/", "data/mania data", false},
        // Kade Engine: su ejecutable y la carpeta de charts de StepMania.
        {"Kade Engine", 6, "kade engine.exe", "Kade Engine.exe", true},
        {"Kade Engine", 2, "sm/", "sm", false},
    };
    EngineGuess guess;
    std::array<std::vector<std::string>, 3> evidence;
    std::array<bool, sizeof(markers) / sizeof(markers[0])> seen{};
    std::array<bool, sizeof(others) / sizeof(others[0])> seenOther{};
    std::map<std::string, int> otherScore;
    std::map<std::string, std::vector<std::string>> otherEvidence;
    for (const Vfs::Entry& entry : vfs.allEntries()) {
        if (entry.isDir) continue;
        const std::string low = lower(Vfs::normalize(entry.virtualPath));
        for (size_t m = 0; m < seenOther.size(); ++m) {
            if (seenOther[m]) continue;
            const OtherMarker& marker = others[m];
            const bool hit = marker.file ? fileName(low) == marker.segment
                                         : segmentAt(low, marker.segment) != std::string::npos;
            if (!hit) continue;
            seenOther[m] = true;
            otherScore[marker.engine] += marker.weight;
            otherEvidence[marker.engine].push_back(marker.label);
        }
        for (size_t m = 0; m < seen.size(); ++m) {
            if (seen[m]) continue;
            const Marker& marker = markers[m];
            bool hit = false;
            if (marker.file) {
                hit = fileName(low) == marker.segment;
            } else {
                const size_t at = segmentAt(low, marker.segment);
                hit = at != std::string::npos;
                // `songs/` solo cuenta con la carpeta charts de Codename debajo.
                if (hit && std::strcmp(marker.segment, "songs/") == 0)
                    hit = low.find("/charts/", at) != std::string::npos;
            }
            if (!hit) continue;
            seen[m] = true;
            guess.score[static_cast<size_t>(marker.engine)] += marker.weight;
            evidence[static_cast<size_t>(marker.engine)].push_back(marker.label);
        }
    }
    // Un skin por defecto leido pesa como una marca fuerte de su motor, una
    // vez por motor.
    std::array<bool, 3> defaultSeen{};
    for (const NoteStyle& style : catalog.styles) {
        const size_t e = static_cast<size_t>(style.engine);
        if (style.use != StyleUse::Default || defaultSeen[e]) continue;
        defaultSeen[e] = true;
        guess.score[e] += 3;
        evidence[e].push_back(style.definition);
    }
    size_t best = 0;
    for (size_t e = 1; e < 3; ++e)
        if (guess.score[e] > guess.score[best]) best = e;
    guess.found = guess.score[best] > 0;
    guess.engine = static_cast<Engine>(best);
    guess.evidence = evidence[best];
    // Otro motor gana si sus marcas pesan mas que las del mejor de los tres.
    int otherBest = 0;
    for (const auto& [name, score] : otherScore)
        if (score > guess.score[best] && score > otherBest) {
            otherBest = score;
            guess.other = name;
            guess.otherEvidence = otherEvidence[name];
        }
    return guess;
}

void settleCodenameReceptors(const Vfs& vfs, const AssetIndex& index, AtlasCache& atlases, Catalog& catalog) {
    const MultikeyNames mapping = readMultikeyNames(vfs, index);
    if (!mapping.found()) return;
    for (NoteStyle& style : catalog.styles) {
        if (style.engine != Engine::Codename || style.use != StyleUse::Default) continue;
        for (PartBinding& binding : style.parts) {
            int column = -1;
            if (binding.part == Part::StrumStatic) column = 3;
            else if (binding.part == Part::StrumPress) column = 4;
            else if (binding.part == Part::StrumConfirm) column = 5;
            if (column < 0 || binding.direction < 0 || binding.direction > 3 || binding.sheet < 0 ||
                static_cast<size_t>(binding.sheet) >= style.sheets.size()) continue;
            const auto* names = atlases.names(vfs, style.sheets[static_cast<size_t>(binding.sheet)].atlas);
            const std::string& prefix = mapping.names[static_cast<size_t>(binding.direction)][static_cast<size_t>(column)];
            if (names && !prefix.empty() && countPrefix(*names, binding.animation.prefix) == 0 && countPrefix(*names, prefix) > 0) {
                binding.animation.prefix = prefix;
                style.multikeyData = mapping.path;
            }
        }
    }
}

Catalog scanNoteStyles(const Vfs& vfs) {
    Catalog catalog;
    const AssetIndex index(vfs);
    AtlasCache atlases;
    readCodename(vfs, index, catalog);
    catalog.chartTypes = codenameChartTypes(vfs, index);
    readCodenameScriptSprites(vfs, index, atlases, catalog);
    settleCodenameReceptors(vfs, index, atlases, catalog);
    settleCodenameImages(vfs, index, atlases, catalog);
    readPsych(vfs, index, atlases, catalog);
    readVSlice(vfs, index, catalog);
    std::stable_sort(catalog.styles.begin(), catalog.styles.end(), [](const NoteStyle& a, const NoteStyle& b) {
        if (a.engine != b.engine) return a.engine < b.engine;
        if (a.scope != b.scope) return a.scope < b.scope;
        if (a.use != b.use) return a.use < b.use;
        return lower(a.name) < lower(b.name);
    });
    return catalog;
}

std::string scriptAssignment(const std::string& text, const std::string& key) {
    auto identChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    size_t at = 0;
    while (!key.empty() && (at = text.find(key, at)) != std::string::npos) {
        const size_t begin = at;
        size_t cursor = at + key.size();
        at = cursor;
        if (cursor < text.size() && identChar(text[cursor])) continue;   // palabra entera
        // Una linea comentada no asigna nada.
        const size_t newline = begin == 0 ? std::string::npos : text.rfind('\n', begin - 1);
        const size_t lineStart = newline == std::string::npos ? 0 : newline + 1;
        const std::string before = text.substr(lineStart, begin - lineStart);
        if (before.find("--") != std::string::npos || before.find("//") != std::string::npos) continue;
        char quote = 0;
        if (begin > 0 && (text[begin - 1] == '\'' || text[begin - 1] == '"')) {
            quote = text[begin - 1];
            if (cursor >= text.size() || text[cursor] != quote) continue;
            ++cursor;
        } else if (begin > 0 && identChar(text[begin - 1])) {
            continue;
        }
        while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t')) ++cursor;
        if (cursor >= text.size()) break;
        if (quote) {
            if (text[cursor] != ',') continue;                       // 'clave', <valor>
        } else if (text[cursor] != '=' || (cursor + 1 < text.size() && text[cursor + 1] == '=')) {
            continue;                                                 // x.clave = <valor>, no ==
        }
        ++cursor;
        while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t')) ++cursor;
        const size_t valueStart = cursor;
        int depth = 0;
        while (cursor < text.size()) {
            const char c = text[cursor];
            if (c == '\'' || c == '"') {
                const size_t close = text.find(c, cursor + 1);
                cursor = close == std::string::npos ? text.size() : close + 1;
                continue;
            }
            const char next = cursor + 1 < text.size() ? text[cursor + 1] : '\0';
            if ((c == '-' && next == '-') || (c == '/' && next == '/')) break;
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
            } else if (c == ')' || c == ']' || c == '}') {
                if (depth == 0) break;
                --depth;
            } else if (depth == 0 && (c == ',' || c == ';' || c == '\n' || c == '\r')) {
                break;
            }
            ++cursor;
        }
        std::string value = text.substr(valueStart, cursor - valueStart);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        if (!value.empty()) return value;
    }
    return {};
}

std::string quotedValue(const std::string& expression) {
    if (expression.size() < 2) return {};
    const char quote = expression.front();
    if ((quote != '\'' && quote != '"') || expression.back() != quote) return {};
    std::string inner = expression.substr(1, expression.size() - 2);
    if (inner.find(quote) != std::string::npos) return {};
    return inner;
}

bool insideHiddenFolder(const std::string& path) {
    size_t start = 0;
    while (start < path.size()) {
        const size_t slash = path.find('/', start);
        if (slash == std::string::npos) return false;   // el ultimo tramo es el archivo
        if (path[start] == '.') return true;
        start = slash + 1;
    }
    return false;
}

std::string psychConfigValue(const std::string& text, const std::string& key) {
    auto trimmed = [](std::string value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
        return value.substr(first);
    };
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        // Primero ':' y, si no hay, '=' (NoteTypesConfig.hx:27-32).
        size_t sep = line.find(':');
        if (sep == std::string::npos) sep = line.find('=');
        if (sep == std::string::npos) continue;
        if (trimmed(line.substr(0, sep)) == key) return trimmed(line.substr(sep + 1));
    }
    return {};
}

}  // namespace fml::notelab

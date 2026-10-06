// Source lookup shared by Psych import, dependency packaging and previews.
// Psych 0.7/1.0.4: Paths.modFolders/getPath and Mods.directoriesWithFile.
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
#include "../../third_party/json.hpp"

namespace fml {
namespace psych {
namespace fs = std::filesystem;
inline std::string extensionLower(const fs::path& path) {
    auto ext = path.extension().u8string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}
inline bool directory(const fs::path& p) { std::error_code ec; return fs::is_directory(p, ec); }
inline bool file(const fs::path& p) { std::error_code ec; return fs::is_regular_file(p, ec); }
inline std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
inline bool relativeKey(const std::string& key) {
    if (key.empty() || key.find(':') != std::string::npos || key.find('\\') != std::string::npos) return false;
    const fs::path p = fs::u8path(key);
    if (p.is_absolute()) return false;
    for (const auto& part : p) if (part == ".." || part == ".") return false;
    return true;
}
inline void append(std::vector<fs::path>& paths, const fs::path& p) {
    if (!directory(p)) return;
    const fs::path clean = p.lexically_normal();
    if (std::find(paths.begin(), paths.end(), clean) == paths.end()) paths.push_back(clean);
}
inline bool contentRoot(const fs::path& p) {
    return directory(p / "characters") || directory(p / "stages") || directory(p / "data");
}
// Unwrap archive container directories (the supplied corpus includes 1-4).
inline fs::path unwrap(fs::path p) {
    if (file(p)) p = p.parent_path();
    for (int depth = 0; depth < 6; ++depth) {
        if (contentRoot(p) || directory(p / "assets") || directory(p / "mods")) break;
        std::error_code ec;
        std::vector<fs::path> dirs;
        for (const auto& e : fs::directory_iterator(p, ec)) if (e.is_directory(ec)) dirs.push_back(e.path());
        if (dirs.size() != 1) break;
        p = dirs.front();
    }
    return p;
}
inline fs::path ownerRoot(fs::path p) {
    if (file(p) || p.has_extension()) p = p.parent_path();
    for (int i = 0; !p.empty() && i < 10; ++i, p = p.parent_path()) {
        if (p.filename() == "data" || p.filename() == "stages" || p.filename() == "characters") return p.parent_path();
        if (p == p.root_path()) break;
    }
    return {};
}
inline fs::path engineRoot(fs::path p) {
    for (int i = 0; !p.empty() && i < 10; ++i, p = p.parent_path()) {
        if (directory(p / "assets")) return p;
        if (p == p.root_path()) break;
    }
    return {};
}
// A concrete selected mod owns precedence. Shared/base assets are fallback,
// never arbitrary sibling mods. Enabled global packs follow modsList order.
inline std::vector<fs::path> roots(const fs::path& selected,
                                  const fs::path& engineAssets = {},
                                  const std::string& library = "") {
    fs::path picked = unwrap(selected);
    const fs::path owner = ownerRoot(selected);
    if (!owner.empty()) picked = owner;
    if (picked.filename() == "characters" || picked.filename() == "stages") picked = picked.parent_path();
    fs::path engine = engineRoot(picked);
    if (engine.empty() && !engineAssets.empty()) engine = engineRoot(unwrap(engineAssets));
    std::vector<fs::path> out;
    const bool specificMod = picked.filename() == "mods" || picked.parent_path().filename() == "mods";
    if (specificMod) append(out, picked);
    if (!engine.empty()) {
        // A selected engine uses its first enabled pack as currentModDirectory.
        std::ifstream list(engine / "modsList.txt");
        std::string line;
        bool currentAdded = specificMod;
        while (std::getline(list, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto bar = line.find('|');
            if (bar == std::string::npos || line.substr(bar + 1) != "1") continue;
            const auto name = line.substr(0, bar);
            if (!relativeKey(name)) continue;
            const fs::path pack = engine / "mods" / fs::u8path(name);
            const auto metadata = nlohmann::json::parse(read(pack / "pack.json"), nullptr, false, true);
            const bool global = metadata.is_object() && metadata.contains("runsGlobally") &&
                metadata["runsGlobally"].is_boolean() && metadata["runsGlobally"].get<bool>();
            if (!currentAdded || global) {
                append(out, pack); currentAdded = true;
            }
        }
        append(out, engine / "mods");
    }
    const auto withinAssets = engine.empty() ? fs::path() : picked.lexically_relative(engine / "assets");
    const bool assetLibrary = !engine.empty() && (picked == engine / "assets" ||
        (!withinAssets.empty() && *withinAssets.begin() != ".."));
    if (!assetLibrary) append(out, picked);
    fs::path assets = engine.empty() ? unwrap(engineAssets) : engine / "assets";
    if (!assets.empty()) {
        if (directory(assets / "assets")) assets /= "assets";
        if (relativeKey(library)) append(out, assets / fs::u8path(library));
        append(out, assets / "shared");
        append(out, assets);
    }
    return out;
}
// ---------------------------------------------------------------------------
// BUSCAR DONDE ESTA, NO SOLO DONDE DEBERIA ESTAR. El motor busca cada recurso
// en una ruta fija de cada raiz (stages/<id>.json, characters/<id>.json,
// images/<x>.png, songs/<id>/Inst.ogg). Un mod que no sigue esa disposicion
// -un fork, un pack reorganizado, un stage en data/stages/- no se importaba y
// no decia por que. Si la ruta fija no existe, se busca el archivo por su
// NOMBRE en todas las raices (con un indice que se construye una vez por raiz)
// y se acepta solo si es INEQUIVOCO: el candidato que mas cola de la ruta
// pedida comparte, sin empate, y que ES del tipo pedido (un dad.json que es
// un atlas no es un personaje). Cada hallazgo se anota para poder decirlo.
struct SourceIndex {
    bool built = false;
    std::size_t files = 0;
    // nombre de archivo en minusculas -> rutas
    std::map<std::string, std::vector<fs::path>> byName;
    // ruta relativa a la raiz en minusculas -> presente. Responde "existe
    // root/key" sin ir al disco: el importador pregunta decenas de miles de
    // rutas por cancion (cada literal de cada script, con sus variantes) y
    // cada pregunta al disco costaba ~0.1 ms. Medido: 70 569 preguntas y 6.7 s
    // en importar cuatro canciones de Mario (DESIGN §60).
    std::unordered_set<std::string> byRelative;
    // El recorrido vio todo lo que cuelga de la raiz: sin tope de archivos o
    // de profundidad ni enlaces saltados. Si no, se pregunta al disco.
    bool complete = true;
    // Carpetas de primer nivel que no se indexaron (packs dentro de `mods/`):
    // lo que cuelga de ellas se sigue preguntando al disco.
    std::set<std::string> skippedTop;
};
inline std::string lowerAscii(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
// Las carpetas de CONTENIDO de un mod; lo demas dentro de `mods/` son
// packs, cada uno un mod aparte (Mods.hx:16-31, ignoreModFolders).
inline bool contentFolder(const std::string& lowerName) {
    for (const char* known : {"characters", "custom_events", "custom_notetypes", "data", "songs", "music", "sounds",
                              "shaders", "videos", "images", "stages", "weeks", "fonts", "scripts", "achievements"})
        if (lowerName == known) return true;
    return false;
}
inline std::map<std::string, SourceIndex>& sourceIndexCache() {
    static std::map<std::string, SourceIndex> cache;
    return cache;
}
// El indice es una foto del disco.
inline void forgetSourceIndexes() { sourceIndexCache().clear(); }
// Una operacion del importador (leer un mod, pedir o escribir una importacion,
// traer un stage o un personaje) da el disco por quieto mientras dura: dentro,
// `resolve` contesta con la foto; al empezar la operacion mas externa la foto
// se tira, asi que un archivo que el usuario anadio entre dos clics se ve.
// Fuera de una operacion `resolve` pregunta al disco, como siempre.
inline int& sourceIndexSessionDepth() { static int depth = 0; return depth; }
class SourceIndexSession {
public:
    SourceIndexSession() { if (sourceIndexSessionDepth()++ == 0) forgetSourceIndexes(); }
    ~SourceIndexSession() { --sourceIndexSessionDepth(); }
    SourceIndexSession(const SourceIndexSession&) = delete;
    SourceIndexSession& operator=(const SourceIndexSession&) = delete;
};
inline const SourceIndex& sourceIndex(const fs::path& root) {
    SourceIndex& index = sourceIndexCache()[root.lexically_normal().u8string()];
    if (index.built) return index;
    index.built = true;
    std::error_code ec;
    // La raiz `mods/` de un motor es la carpeta global: sus subcarpetas que
    // no son de contenido son packs -otros mods, activos o no- y no se
    // indexan: un mod apagado no puede prestar archivos.
    const bool modsRoot = lowerAscii(root.filename().u8string()) == "mods" && directory(root.parent_path() / "assets");
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator() && !ec; it.increment(ec)) {
        if (it.depth() > 10 || it->is_symlink(ec)) {
            index.complete = false;
            it.disable_recursion_pending(); continue;
        }
        if (modsRoot && it.depth() == 0 && it->is_directory(ec)) {
            const std::string top = lowerAscii(it->path().filename().u8string());
            if (!contentFolder(top)) {
                index.skippedTop.insert(top);
                it.disable_recursion_pending(); continue;
            }
        }
        ec.clear();
        if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
        if (++index.files > 300000) { index.complete = false; break; }
        index.byName[lowerAscii(it->path().filename().u8string())].push_back(it->path());
        index.byRelative.insert(lowerAscii(it->path().lexically_relative(root).generic_u8string()));
    }
    // Un recorrido cortado por un error de E/S tampoco es una foto completa.
    if (ec) index.complete = false;
    return index;
}
struct Discovery { std::string key; fs::path found; };
// Lo que se encontro fuera de su sitio desde la ultima limpieza.
inline std::vector<Discovery>& discoveries() { static std::vector<Discovery> log; return log; }
// Que un archivo con el nombre pedido sea ademas del TIPO pedido: se mira
// dentro cuando es JSON, porque el nombre no basta.
inline bool plausibleKind(const std::string& key, const fs::path& candidate) {
    if (extensionLower(candidate) != ".json") return true;
    const fs::path relative = fs::u8path(key);
    const std::string first = relative.begin() == relative.end() ? "" : lowerAscii(relative.begin()->u8string());
    if (first != "characters" && first != "stages" && first != "data" && first != "weeks") return true;
    std::error_code ec;
    if (fs::file_size(candidate, ec) > 64u * 1024u * 1024u || ec) return false;
    const auto node = nlohmann::json::parse(read(candidate), nullptr, false, true);
    if (!node.is_object()) return false;
    if (first == "characters") return node.contains("animations") && node["animations"].is_array() && node.contains("image");
    if (first == "stages")
        for (const char* mark : {"defaultZoom", "boyfriend", "girlfriend", "opponent", "directory", "isPixelStage", "camera_speed", "objects"})
            if (node.contains(mark)) return true;
    if (first == "data") {
        const auto& song = node.contains("song") && node["song"].is_object() ? node["song"] : node;
        return (song.contains("notes") && song["notes"].is_array()) || node.contains("events");
    }
    if (first == "weeks") return node.contains("songs") && node["songs"].is_array();
    return false;
}
inline fs::path discover(const std::vector<fs::path>& sources, const std::string& key) {
    const fs::path relative = fs::u8path(key);
    const std::string name = lowerAscii(relative.filename().u8string());
    if (name.empty()) return {};
    std::vector<std::string> wanted;   // componentes de la ruta pedida, sin el nombre
    for (const auto& part : relative.parent_path()) wanted.push_back(lowerAscii(part.u8string()));
    // Raiz por raiz, en el orden del motor: el mismo archivo en mods/ y en
    // assets/ no es una ambiguedad, es precedencia. Dentro de una raiz, entre
    // iguales no se elige: un empate es una ambiguedad, no un hallazgo.
    fs::path best;
    int bestScore = -1;
    for (const fs::path& root : sources) {
        const SourceIndex& index = sourceIndex(root);
        const auto hit = index.byName.find(name);
        if (hit == index.byName.end()) continue;
        fs::path local;
        int localScore = -1, ties = 0;
        for (const fs::path& candidate : hit->second) {
            // Cuanta cola de la ruta pedida comparte: images/stages/back.png
            // prefiere .../stages/back.png a cualquier otro back.png.
            std::vector<std::string> parts;
            for (const auto& part : candidate.lexically_relative(root).parent_path()) parts.push_back(lowerAscii(part.u8string()));
            int score = 0;
            for (std::size_t i = 0; i < wanted.size() && i < parts.size(); ++i) {
                if (wanted[wanted.size() - 1 - i] != parts[parts.size() - 1 - i]) break;
                ++score;
            }
            if (score > localScore) { local = candidate; localScore = score; ties = 0; }
            else if (score == localScore) ++ties;
        }
        if (ties > 0 || local.empty()) continue;
        // La ruta pedida ENTERA tiene que ser el final de la encontrada: un
        // hallazgo es "en otra raiz" o "dentro de otra carpeta"
        // (data/characters/x.json por characters/x.json), nunca "en una
        // carpeta con otro nombre". `images/characters/bf.png` no es
        // `images/icons/characters/bf.png` por llamarse igual. El importador
        // PRUEBA muchas rutas que no existen (iconos por cada literal,
        // variantes), y sin esta condicion cada prueba encontraba un homonimo
        // en otra carpeta y lo copiaba con la ruta falsa: tres copias de cada
        // spritesheet por cancion, 100 MB de mas y la importacion lenta.
        if (localScore < static_cast<int>(wanted.size())) continue;
        if (localScore > bestScore) { best = local; bestScore = localScore; }
    }
    const int ties = 0;
    if (best.empty() || ties > 0) return {};
    if (!plausibleKind(key, best)) return {};
    discoveries().push_back({key, best});
    return best;
}
// La ruta del motor primero; si no esta, donde este de verdad.
inline fs::path resolve(const std::vector<fs::path>& sources, const std::string& key) {
    if (!relativeKey(key)) return {};
    // Mismo orden y misma respuesta que preguntar `root/key` al disco en cada
    // raiz (Windows no distingue mayusculas), pero contra la foto cuando hay
    // una operacion en curso y la foto lo vio todo.
    const bool snapshot = sourceIndexSessionDepth() > 0;
    const std::string wanted = snapshot ? lowerAscii(fs::u8path(key).lexically_normal().generic_u8string()) : std::string();
    const std::string top = wanted.substr(0, wanted.find('/'));
    for (const auto& root : sources) {
        const SourceIndex* index = snapshot ? &sourceIndex(root) : nullptr;
        if (index && index->complete && !index->skippedTop.count(top)) {
            if (index->byRelative.count(wanted)) return root / fs::u8path(key);
            continue;
        }
        auto p = root / fs::u8path(key); if (file(p)) return p;
    }
    return discover(sources, key);
}
inline std::string vanillaStage(const std::string& song) {
    if (song == "spookeez" || song == "south" || song == "monster") return "spooky";
    if (song == "pico" || song == "blammed" || song == "philly" || song == "philly-nice") return "philly";
    if (song == "milf" || song == "satin-panties" || song == "high") return "limo";
    if (song == "cocoa" || song == "eggnog") return "mall";
    if (song == "winter-horrorland") return "mallEvil";
    if (song == "senpai" || song == "roses") return "school";
    if (song == "thorns") return "schoolEvil";
    if (song == "ugh" || song == "guns" || song == "stress") return "tank";
    return "stage";
}
} // namespace psych
} // namespace fml

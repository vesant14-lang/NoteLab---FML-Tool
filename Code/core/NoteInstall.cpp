#include "NoteInstall.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

#include <fstream>

namespace fml::notelab {
namespace {

namespace fs = std::filesystem;

bool isFile(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool isFolder(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::string lowerText(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Donde guarda cada motor su skin por defecto, desde la carpeta de la instalacion.
std::vector<const char*> defaultSkinPaths(Engine engine) {
    switch (engine) {
        case Engine::Codename:
            return {"assets/images/game/notes/default.xml"};
        case Engine::Psych:
            // Paths busca en la biblioteca shared y despues en assets/images.
            return {"assets/shared/images/noteSkins/NOTE_assets.xml", "assets/images/noteSkins/NOTE_assets.xml",
                    "assets/shared/images/NOTE_assets.xml", "assets/images/NOTE_assets.xml"};
        case Engine::VSlice:
            return {"assets/data/notestyles/funkin.json", "assets/preload/data/notestyles/funkin.json",
                    "assets/gameplay/notestyles/funkin/funkin.json"};
    }
    return {};
}

// El archivo que delata el skin por defecto; vacio si la carpeta no lo tiene.
fs::path defaultSkinOf(const fs::path& folder, Engine engine) {
    for (const char* candidate : defaultSkinPaths(engine)) {
        const fs::path path = folder / fs::u8path(candidate);
        if (isFile(path)) return path;
    }
    return {};
}

const char* executableOf(Engine engine) {
    return engine == Engine::Codename ? "CodenameEngine" : engine == Engine::Psych ? "PsychEngine" : "Funkin";
}

bool hasExecutable(const fs::path& folder, Engine engine) {
    const std::string name = executableOf(engine);
    return isFile(folder / (name + ".exe")) || isFile(folder / name);
}

// Carpetas que nunca son una instalacion y pueden ser enormes.
bool skippedFolder(const fs::path& folder) {
    const std::string name = lowerText(folder.filename().u8string());
    if (name.empty() || name[0] == '.' || name[0] == '$') return true;
    for (const char* skip : {"node_modules", "windows", "program files", "program files (x86)", "appdata"})
        if (name == skip) return true;
    return false;
}

}  // namespace

bool hasEngineDefault(const Catalog& catalog, Engine engine) {
    // El skin por defecto en el sitio de una instalacion (bajo assets/), no el
    // que un mod sobrescribe desde su carpeta: ese mod sigue necesitando lo
    // demas del juego base (salpicaduras, HUD).
    for (const NoteStyle& style : catalog.styles) {
        if (style.engine != engine) continue;
        const std::string definition = lowerText(style.definition);
        for (const char* candidate : defaultSkinPaths(engine)) {
            const std::string tail = lowerText(candidate);
            if (definition.size() >= tail.size() &&
                definition.compare(definition.size() - tail.size(), tail.size(), tail) == 0 &&
                (definition.size() == tail.size() || definition[definition.size() - tail.size() - 1] == '/'))
                return true;
        }
    }
    return false;
}

bool isEngineInstall(const fs::path& folder, Engine engine) {
    return !defaultSkinOf(folder, engine).empty();
}

bool isOfficialInstall(const fs::path& folder, Engine engine) {
    if (!isEngineInstall(folder, engine)) return false;
    if (hasExecutable(folder, engine)) return true;
    // El codigo fuente: el proyecto de Lime y la carpeta source/.
    if (!isFolder(folder / "source")) return false;
    if (engine == Engine::VSlice) return isFile(folder / "project.hxp");
    return isFile(folder / "project.xml") || isFile(folder / "Project.xml");
}

bool looksLikeModFolder(const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return false;
    for (const char* name : {"data", "songs", "images", "characters", "content", "shaders", "sounds", "music", "source",
                             "scripts", "manifest", "pack.json", "polymod.json", "mod.json", "_polymod_meta.json",
                             "custom_notetypes", "stages", "weeks"})
        if (fs::exists(folder / name, ec)) return true;
    return false;
}

namespace {

std::vector<std::string> modFoldersIn(const fs::path& mods) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(mods, ec)) return out;
    // La carpeta mods/ de Psych tambien es una capa de contenido: sus
    // carpetas de recursos no son mods (Mods.hx:16-32, ignoreModFolders).
    static const char* notMods[] = {"characters", "custom_events", "custom_notetypes", "data", "songs", "music", "sounds",
                                    "shaders", "videos", "images", "stages", "weeks", "fonts", "scripts", "achievements"};
    for (const auto& entry : fs::directory_iterator(mods, ec)) {
        if (ec) break;
        std::string name = entry.path().filename().u8string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (std::find_if(std::begin(notMods), std::end(notMods), [&](const char* n) { return name == n; }) != std::end(notMods)) continue;
        if (entry.is_directory(ec) && looksLikeModFolder(entry.path())) out.push_back(entry.path().filename().u8string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string trimmed(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    text.erase(0, start);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    return text;
}

// Los paquetes `content/` del mod en orden de prioridad (el primero manda).
std::vector<fs::path> contentPacks(const fs::path& mod) {
    std::vector<fs::path> out;
    std::error_code ec;
    const fs::path content = mod / "content";
    if (!fs::is_directory(content, ec)) return out;
    std::vector<std::string> names;
    std::ifstream order(content / "order.txt", std::ios::binary);
    std::string line;
    while (std::getline(order, line)) {
        const std::string name = trimmed(line);
        if (!name.empty() && name[0] != '#' && name[0] != ';' && fs::is_directory(content / fs::u8path(name), ec) &&
            std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    }
    std::vector<std::string> extra;
    for (const auto& entry : fs::directory_iterator(content, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        const std::string name = entry.path().filename().u8string();
        if (std::find(names.begin(), names.end(), name) == names.end()) extra.push_back(name);
    }
    std::sort(extra.begin(), extra.end());
    names.insert(names.end(), extra.begin(), extra.end());
    for (const std::string& name : names) out.push_back(content / fs::u8path(name));
    return out;
}

// Los addons de Codename: carpetas o ZIP, `[HIGH]` encima de todo, `[LOW]`
// debajo del mod y los demas encima del mod.
void addons(const fs::path& folder, std::vector<fs::path>& high, std::vector<fs::path>& normal, std::vector<fs::path>& low) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return;
    std::vector<fs::path> entries;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (ec) break;
        const bool zip = entry.is_regular_file(ec) && entry.path().extension().u8string().size() == 4 &&
                         (entry.path().extension() == ".zip" || entry.path().extension() == ".ZIP");
        if (entry.is_directory(ec) || zip) entries.push_back(entry.path());
    }
    std::sort(entries.begin(), entries.end());
    for (const fs::path& path : entries) {
        const std::string name = path.filename().u8string();
        if (name.rfind("[HIGH]", 0) == 0) high.push_back(path);
        else if (name.rfind("[LOW]", 0) == 0) low.push_back(path);
        else normal.push_back(path);
    }
}

}  // namespace

// Una carpeta que solo envuelve a otra (la que deja descomprimir un ZIP en
// una carpeta con su nombre): lo de dentro, si eso si es un mod o una
// instalacion. Como la VFS con un ZIP de una sola carpeta.
fs::path unwrapFolder(fs::path root) {
    std::error_code ec;
    for (int depth = 0; depth < 3; ++depth) {
        if (looksLikeModFolder(root) || fs::is_directory(root / "assets", ec)) break;
        fs::path only;
        int folders = 0, files = 0;
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (ec) break;
            const std::string name = entry.path().filename().u8string();
            if (!name.empty() && name[0] == '.') continue;
            if (entry.is_directory(ec)) {
                ++folders;
                only = entry.path();
            } else if (name != "desktop.ini" && name != "Thumbs.db") {
                ++files;
            }
        }
        if (folders != 1 || files > 0) break;
        if (!looksLikeModFolder(only) && !fs::is_directory(only / "assets", ec) && !fs::is_directory(only / "mods", ec)) break;
        root = only;
    }
    return root;
}

OpenLayout openLayoutOf(const fs::path& chosen, const std::string& preferredMod) {
    OpenLayout layout;
    layout.mod = chosen;
    std::error_code ec;
    if (!fs::is_directory(chosen, ec)) return layout;   // un ZIP se abre tal cual
    const fs::path root = unwrapFolder(chosen);
    layout.mod = root;
    const fs::path parent = root.parent_path();
    std::string parentName = parent.filename().u8string();
    std::transform(parentName.begin(), parentName.end(), parentName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (fs::is_directory(root / "assets", ec) && fs::is_directory(root / "mods", ec)) {
        layout.mods = modFoldersIn(root / "mods");
        // El motor tal cual (su ejecutable oficial o su codigo) es el juego
        // base: se abre el y sus mods quedan a mano. La build de un mod (otro
        // ejecutable, como «Voiid Chronicles.exe») se abre por su mod.
        bool official = false;
        for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice})
            official = official || isOfficialInstall(root, engine);
        if (!layout.mods.empty() && (!official || !preferredMod.empty())) {
            std::string chosen = layout.mods.front();
            if (!preferredMod.empty() && std::find(layout.mods.begin(), layout.mods.end(), preferredMod) != layout.mods.end())
                chosen = preferredMod;
            layout.install = root;
            layout.mod = root / "mods" / fs::u8path(chosen);
        }
    } else if (parentName == "mods" && looksLikeModFolder(root) && fs::is_directory(parent.parent_path() / "assets", ec)) {
        layout.install = parent.parent_path();
        layout.mods = modFoldersIn(parent);
    }
    // Encima del mod, por prioridad: addons [HIGH], paquetes content/ (los
    // registra el mod despues de cargarse) y addons normales.
    std::vector<fs::path> high, normal, low;
    if (!layout.install.empty()) addons(layout.install / "addons", high, normal, low);
    addons(layout.mod / "addons", high, normal, low);
    layout.packs = high;
    const std::vector<fs::path> packs = contentPacks(layout.mod);
    layout.packs.insert(layout.packs.end(), packs.begin(), packs.end());
    layout.packs.insert(layout.packs.end(), normal.begin(), normal.end());
    layout.lowPacks = low;
    return layout;
}

fs::path baseMountOf(const fs::path& base) {
    const fs::path assets = base / "assets";
    return isFolder(assets) ? assets : base;
}

const char* baseFoundKey(BaseFound how) {
    switch (how) {
        case BaseFound::None: return "none";
        case BaseFound::User: return "user";
        case BaseFound::Contains: return "contains";
        case BaseFound::Remembered: return "remembered";
        case BaseFound::Nearby: return "nearby";
    }
    return "?";
}

BaseSearch findEngineBase(const fs::path& modRoot, Engine engine, const std::vector<fs::path>& remembered) {
    BaseSearch found;
    std::error_code absoluteError;
    fs::path root = fs::absolute(modRoot, absoluteError);
    if (absoluteError) root = modRoot;
    // 1. El mod vive dentro de una instalacion: esa es la que lo carga.
    fs::path parent = root.parent_path();
    for (int level = 0; level < 6 && !parent.empty() && parent != parent.parent_path(); ++level) {
        if (isEngineInstall(parent, engine)) {
            found.folder = parent;
            found.how = BaseFound::Contains;
            return found;
        }
        parent = parent.parent_path();
    }
    // 2. Una instalacion oficial que ya se uso.
    for (const fs::path& folder : remembered)
        if (!folder.empty() && isOfficialInstall(folder, engine)) {
            found.folder = folder;
            found.how = BaseFound::Remembered;
            return found;
        }
    // 3. Junto al mod: en la carpeta que lo contiene y en la de encima, hasta
    // dos niveles por debajo de cada una (un ZIP descomprimido suele dejar la
    // instalacion una carpeta mas abajo). Con un tope, para no recorrer un
    // disco entero.
    bool foundBuild = false;
    fs::file_time_type newest{};
    int visited = 0;
    constexpr int kMaxVisited = 4000;
    auto consider = [&](const fs::path& folder) {
        ++visited;
        if (folder == root || !isOfficialInstall(folder, engine)) return;
        const bool build = hasExecutable(folder, engine);
        std::error_code ec;
        const fs::file_time_type time = fs::last_write_time(defaultSkinOf(folder, engine), ec);
        const bool better = found.folder.empty() || (build && !foundBuild) ||
                            (build == foundBuild && !ec && time > newest);
        if (!better) return;
        found.folder = folder;
        found.how = BaseFound::Nearby;
        foundBuild = build;
        newest = ec ? fs::file_time_type{} : time;
    };
    std::vector<fs::path> levels;
    const fs::path first = root.parent_path();
    if (!first.empty()) levels.push_back(first);
    if (!first.empty() && first != first.parent_path() && !first.parent_path().empty()) levels.push_back(first.parent_path());
    for (const fs::path& level : levels) {
        if (!isFolder(level)) continue;
        std::error_code ec;
        for (fs::directory_iterator it(level, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && visited < kMaxVisited; it.increment(ec)) {
            std::error_code typeError;
            if (!it->is_directory(typeError) || skippedFolder(it->path())) continue;
            consider(it->path());
            std::error_code inner;
            for (fs::directory_iterator sub(it->path(), fs::directory_options::skip_permission_denied, inner), stop;
                 !inner && sub != stop && visited < kMaxVisited; sub.increment(inner)) {
                std::error_code subType;
                if (!sub->is_directory(subType) || skippedFolder(sub->path())) continue;
                consider(sub->path());
            }
        }
        if (!found.folder.empty()) break;   // la carpeta mas cercana manda
    }
    return found;
}

}  // namespace fml::notelab

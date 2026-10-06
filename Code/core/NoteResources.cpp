#include "NoteResources.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fml::notelab {

namespace {
std::string extensionOf(const std::string& path) {
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of("/\\");
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : Vfs::toLower(path.substr(dot));
}
const char* folderOf(ResourceKind kind) {
    return kind == ResourceKind::Image ? "images/" : kind == ResourceKind::Video ? "videos/" : "sounds/";
}
}

const char* resourceKindName(ResourceKind kind, bool spanish) {
    if (kind == ResourceKind::Image) return spanish ? "Imágenes" : "Images";
    if (kind == ResourceKind::Video) return "Videos";
    return spanish ? "Sonidos" : "Sounds";
}

namespace {
std::vector<std::string> pathSegments(const std::string& path) {
    std::vector<std::string> parts;
    std::string part;
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!part.empty()) parts.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    if (!part.empty()) parts.push_back(part);
    return parts;
}
std::string loweredText(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool songStem(const std::string& file) {
    const std::string name = loweredText(file);
    // Inst.ogg, Voices.ogg, Voices-bf.ogg, Inst-erect.ogg, song-Inst.ogg...
    return name.rfind("inst", 0) == 0 || name.rfind("voices", 0) == 0 || name.find("-inst") != std::string::npos ||
           name.find("-voices") != std::string::npos || name.find("_inst") != std::string::npos || name.find("_voices") != std::string::npos;
}
}

SoundUse soundUseOf(const std::string& path) {
    const std::vector<std::string> parts = pathSegments(path);
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string folder = loweredText(parts[i]);
        if (folder == "songs") return SoundUse::Song;
        if (folder == "music") return SoundUse::Music;
    }
    if (!parts.empty() && songStem(parts.back())) return SoundUse::Song;
    return SoundUse::Effect;
}

std::string soundSongOf(const std::string& path) {
    const std::vector<std::string> parts = pathSegments(path);
    for (size_t i = 0; i + 2 < parts.size(); ++i)
        if (loweredText(parts[i]) == "songs") return parts[i + 1];
    if (parts.size() >= 2 && songStem(parts.back())) return parts[parts.size() - 2];
    return {};
}

std::optional<ResourceKind> resourceKindOf(const std::string& path) {
    const std::string ext = extensionOf(path);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif") return ResourceKind::Image;
    if (ext == ".mp4" || ext == ".webm" || ext == ".ogv" || ext == ".wmv" || ext == ".avi" || ext == ".mov" || ext == ".mkv") return ResourceKind::Video;
    if (ext == ".ogg" || ext == ".wav" || ext == ".mp3" || ext == ".flac") return ResourceKind::Sound;
    return std::nullopt;
}

bool safeResourceReference(const std::string& value) {
    if (value.empty() || value.size() > 2048 || value.front() == '/' || value.front() == '\\') return false;
    if (value.find(':') != std::string::npos || value.find('\0') != std::string::npos || value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) return false;
    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    size_t start = 0;
    while (start <= normalized.size()) {
        const size_t end = normalized.find('/', start);
        const std::string part = normalized.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

std::string resourceNativeKey(const std::string& value, ResourceKind kind) {
    if (!safeResourceReference(value)) return {};
    std::string key = Vfs::normalize(value);
    const std::string folder = folderOf(kind);
    const std::string lower = Vfs::toLower(key);
    if (lower.rfind(folder, 0) == 0) key.erase(0, folder.size());
    else {
        const size_t at = lower.rfind('/' + folder);
        if (at != std::string::npos) key.erase(0, at + folder.size() + 1);
    }
    const auto known = resourceKindOf(key);
    if (known && *known == kind) key.resize(key.size() - extensionOf(key).size());
    return key;
}

std::string resourcePackagePath(const std::string& value, ResourceKind kind) {
    const std::string key = resourceNativeKey(value, kind);
    if (key.empty()) return {};
    const std::string ext = extensionOf(value);
    return std::string(folderOf(kind)) + key + (ext.empty() ? kind == ResourceKind::Image ? ".png" : kind == ResourceKind::Video ? ".mp4" : ".ogg" : ext);
}

std::vector<ModResource> scanModResources(const Vfs& vfs, bool includeBase, size_t limit) {
    std::vector<ModResource> result;
    std::set<std::string> seen;
    for (const auto& entry : vfs.allEntries()) {
        const auto kind = resourceKindOf(entry.virtualPath);
        if (!kind || !safeResourceReference(entry.virtualPath) || entry.rootIndex >= vfs.roots().size()) continue;
        const auto& root = vfs.roots()[entry.rootIndex];
        const bool base = root.kind == MountProviderKind::BaseGame;
        if (base && !includeBase) continue;
        const auto winner = vfs.find(entry.virtualPath);
        if (!winner || winner->rootIndex != entry.rootIndex || winner->discoveryOrder != entry.discoveryOrder) continue;
        if (!seen.insert(Vfs::toLower(entry.virtualPath)).second) continue;
        result.push_back({*kind, entry.virtualPath, root.label, entry.rootIndex, entry.size, entry.fromArchive, base});
        if (limit && result.size() >= limit) break;
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return Vfs::toLower(a.path) < Vfs::toLower(b.path);
    });
    return result;
}

const ModResource* findModResource(const std::vector<ModResource>& resources, const std::string& value, ResourceKind kind) {
    if (!safeResourceReference(value)) return nullptr;
    const std::string key = Vfs::toLower(Vfs::normalize(value));
    for (const auto& resource : resources)
        if (resource.kind == kind && Vfs::toLower(resource.path) == key) return &resource;
    const std::string native = Vfs::toLower(resourceNativeKey(value, kind));
    const ModResource* matched = nullptr;
    for (const auto& resource : resources) {
        if (resource.kind != kind || Vfs::toLower(resourceNativeKey(resource.path, kind)) != native) continue;
        if (matched) return nullptr;
        matched = &resource;
    }
    return matched;
}

std::vector<ResourceUse> blockResourceUses(const BlockProgram& program) {
    std::vector<ResourceUse> uses;
    std::map<std::pair<ResourceKind, std::string>, size_t> indices;
    for (const auto& item : program.nodes) {
        const BlockDef* def = blockDef(item.second.key);
        if (!def) continue;
        for (size_t i = 0; i < std::min(def->args.size(), item.second.args.size()); ++i) {
            const auto kind = def->args[i].kind;
            if (kind != ArgKind::Sound && kind != ArgKind::Image && kind != ArgKind::Video) continue;
            const auto& value = item.second.args[i].value;
            if (value.empty()) continue;
            const auto media = kind == ArgKind::Sound ? ResourceKind::Sound : kind == ArgKind::Image ? ResourceKind::Image : ResourceKind::Video;
            const auto identity = std::make_pair(media, Vfs::toLower(Vfs::normalize(value)));
            auto found = indices.find(identity);
            if (found == indices.end()) { indices[identity] = uses.size(); uses.push_back({media, value, {}, false}); found = indices.find(identity); }
            auto& use = uses[found->second];
            use.slots.emplace_back(item.first, static_cast<int>(i));
            use.active |= (placeOf(program, item.first) & (kPlaceCreate | kPlaceHit | kPlaceMiss)) != 0;
        }
    }
    return uses;
}

ResourceImportResult importMediaFiles(const std::vector<std::filesystem::path>& files, const std::filesystem::path& root,
    const std::string& destinationFolder, bool createFolder) {
    namespace fs = std::filesystem;
    ResourceImportResult result;
    auto fail = [&](const std::string& error) { result.error = error; return result; };
    if (files.empty() || files.size() > 128) return fail("Choose between 1 and 128 media files.");
    if (!safeResourceReference(destinationFolder)) return fail("Destination must be a relative folder without '..' or absolute paths.");
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(root, ec);
    if (ec || !fs::is_directory(base, ec)) return fail("Destination root is not an existing directory.");
    const fs::path folder = base / fs::u8path(Vfs::normalize(destinationFolder));
    auto validName = [](const fs::path& part) {
        const std::string name = part.u8string(), lower = Vfs::toLower(part.stem().u8string());
        if (name.empty() || name.back() == '.' || name.back() == ' ' || name.find_first_of("<>:\"|?*") != std::string::npos) return false;
        if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) return false;
        if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul") return false;
        return !(lower.size() == 4 && (lower.rfind("com", 0) == 0 || lower.rfind("lpt", 0) == 0) && lower[3] >= '1' && lower[3] <= '9');
    };
    fs::path current = base;
    for (const auto& part : fs::u8path(destinationFolder)) {
        if (!validName(part)) return fail("Destination contains an invalid folder name.");
        current /= part;
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(current.wstring().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return fail("Destination cannot traverse a symbolic link or junction.");
#endif
        if (fs::is_symlink(current, ec)) return fail("Destination cannot traverse a symbolic link.");
        ec.clear();
        if (fs::exists(current, ec) && !fs::is_directory(current, ec)) return fail("A destination folder is already a file.");
    }
    std::set<std::string> names;
    size_t total = 0;
    std::vector<std::pair<fs::path, std::string>> planned;
    for (const auto& input : files) {
        if (!fs::is_regular_file(input, ec)) return fail("An input is missing or is not a regular file.");
        const auto kind = resourceKindOf(input.u8string());
        if (!kind || !validName(input.filename())) return fail("Only supported images, sounds and videos can be imported.");
        const auto bytes = fs::file_size(input, ec);
        const size_t limit = *kind == ResourceKind::Video ? 128u * 1024u * 1024u : 32u * 1024u * 1024u;
        if (ec || bytes == 0 || bytes > limit || total + bytes > 256u * 1024u * 1024u) return fail("Input exceeds the media size limit or is empty.");
        total += static_cast<size_t>(bytes);
        const std::string virtualPath = Vfs::normalize(destinationFolder + "/" + input.filename().u8string());
        if (!names.insert(Vfs::toLower(virtualPath)).second || fs::exists(base / fs::u8path(virtualPath), ec))
            return fail("A destination file already exists or two inputs have the same name. Choose a different folder.");
        planned.emplace_back(input, virtualPath);
    }
    if (!fs::exists(folder, ec)) {
        if (!createFolder) return fail("Destination folder does not exist. Enable Create destination folder.");
        if (!fs::create_directories(folder, ec) || ec) return fail("Destination folder could not be created.");
    }
    for (const auto& item : planned) {
        const fs::path target = base / fs::u8path(item.second);
        if (!fs::copy_file(item.first, target, fs::copy_options::none, ec) || ec)
            return fail("Copy failed. Previously copied files remain available; no existing file was overwritten.");
        result.files.emplace_back(target, item.second);
    }
    return result;
}

}

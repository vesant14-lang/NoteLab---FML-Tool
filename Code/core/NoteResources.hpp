#pragma once

#include "NoteBlocks.hpp"
#include "../support/io/Vfs.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fml::notelab {

enum class ResourceKind { Image, Video, Sound };

struct ModResource {
    ResourceKind kind = ResourceKind::Image;
    std::string path, provider;
    size_t rootIndex = 0;
    std::uintmax_t bytes = 0;
    bool archive = false, base = false;
};

struct ResourceUse {
    ResourceKind kind = ResourceKind::Image;
    std::string path;
    std::vector<std::pair<int, int>> slots;
    bool active = false;
};

const char* resourceKindName(ResourceKind kind, bool spanish);
// Para que sirve un sonido, por su ruta, igual en los tres motores: el audio
// de una cancion (todo lo que cuelga de una carpeta songs/, y los Inst y
// Voices de donde sean: Psych songs/<cancion>/Inst.ogg, Codename
// songs/<cancion>/song/Voices.ogg, V-Slice songs/<cancion>/Voices-bf.ogg),
// musica (una carpeta music/: menus, pausa) o un efecto (lo demas).
enum class SoundUse { Effect, Song, Music };
SoundUse soundUseOf(const std::string& path);
// La cancion de un audio de cancion: la carpeta que sigue a songs/ (o la del
// archivo, si no esta en songs/). Vacio si no es de una cancion.
std::string soundSongOf(const std::string& path);
std::optional<ResourceKind> resourceKindOf(const std::string& path);
bool safeResourceReference(const std::string& value);
std::string resourceNativeKey(const std::string& value, ResourceKind kind);
std::string resourcePackagePath(const std::string& value, ResourceKind kind);
std::vector<ModResource> scanModResources(const Vfs& vfs, bool includeBase = false, size_t limit = 20000);
const ModResource* findModResource(const std::vector<ModResource>& resources, const std::string& value, ResourceKind kind);
std::vector<ResourceUse> blockResourceUses(const BlockProgram& program);

struct ResourceImportResult {
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    std::string error;
};
ResourceImportResult importMediaFiles(const std::vector<std::filesystem::path>& files, const std::filesystem::path& root,
    const std::string& destinationFolder, bool createFolder);

}

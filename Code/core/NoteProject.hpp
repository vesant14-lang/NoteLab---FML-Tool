// fml_notelab — El proyecto de Note Lab: el archivo .fmlnote (DESIGN_PLUGIN_NOTE_LAB §21).
//
// JSON versionado. Guarda las fuentes abiertas por su ruta, las hojas
// importadas, cada estilo editado entero con la huella (SHA-256) de los
// archivos que usa, lo elegido y las preferencias de la vista previa. Al
// reabrir, lo que cambio o ya no esta se dice; nunca se escribe en el mod.
#pragma once

#include "NoteBlocks.hpp"
#include "NoteCreate.hpp"
#include "NotePreview.hpp"
#include "NoteStyle.hpp"
#include "NoteTypes.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fml::notelab {

constexpr int kProjectVersion = 2;
constexpr size_t kProjectByteLimit = 16u * 1024u * 1024u;

struct CreationRecipe {
    std::string styleId, baseStyle, kind, font;
    std::string sprite;   // el .nlsprite con las capas de lo dibujado (ruta en disco), si lo hay
    ArrowRecipe arrows;
    TypeLookRecipe look;
    RatingRecipe rating;
    CustomRecipe custom;
};

// Una hoja importada: sus archivos en disco y donde se montaron.
struct ProjectImport {
    std::string image, atlas;                // rutas en disco (UTF-8)
    std::string imageVirtual, atlasVirtual;  // rutas virtuales en el mod
};

// Un archivo que usaba un estilo editado, con su huella al guardar.
struct ProjectFile {
    std::string virtualPath;
    std::string sha256;
};

struct ProjectEdit {
    std::string styleId;
    NoteStyle style;                 // el estilo tal como quedo
    std::vector<ProjectFile> files;
    bool created = false;            // una variante creada en Note Lab, no del mod
};

// El programa de bloques de un tipo de nota (NoteBlocks.hpp); tambien el de
// un tipo creado en Note Lab, que solo existe aqui.
struct ProjectTypeBlocks {
    std::string type;
    BlockProgram program;
};

struct ProjectSource {
    std::string path;                // carpeta o ZIP del mod (UTF-8)
    // Dentro de una instalacion con mods: el que se abrio, o que se abrio la
    // instalacion como juego base (NoteInstall.hpp, openLayoutOf).
    std::string mod;
    bool baseOnly = false;
    // El aspecto que se le dio en Note Lab a cada tipo: tipo -> id del estilo
    // creado (una variante del proyecto, DESIGN_PLUGIN_NOTE_LAB §31).
    std::vector<std::pair<std::string, std::string>> typeLooks;
    std::vector<ProjectImport> imports;
    std::vector<ProjectEdit> edits;
    std::vector<ProjectTypeBlocks> typeBlocks;
    std::vector<CreationRecipe> recipes;
    std::vector<std::pair<std::string, BotProfile>> bots;
};

// Preferencias de la vista previa que viajan con el proyecto.
struct ProjectView {
    int visibleLines = 2;            // 0 rival, 1 jugador, 2 los dos
    int playSide = 1;
    bool manual = false;
    bool downscroll = false;
    bool chartSpeed = true;
    bool chartSkin = true;
    bool psychColors = true;
    float scrollSpeed = 1.0f;
    float bpm = 120.0f;
    HudLayer judgement, combo, score;
};

struct NoteProject {
    int version = kProjectVersion;
    int engineChoice = 0;            // 0 Auto, 1 Codename, 2 Psych, 3 V-Slice
    // El juego base que fijo la persona, por motor (Codename, Psych, V-Slice;
    // UTF-8, vacio = se busca solo). `baseRoot` es el de los proyectos de
    // antes, uno solo: al leerlo se mira de que motor es.
    std::array<std::string, 3> bases;
    std::string baseRoot;
    std::vector<ProjectSource> sources;
    int selectedSource = -1;
    std::string selectedStyle;       // id del estilo elegido
    std::string selectedType;        // nombre del tipo de nota elegido
    int songSource = -1;             // -1 = patron de prueba
    std::string songId, songDifficulty, songVariation;
    ProjectView view;
    // «Distribuir» en el chart elegido: el paquete y si esta aplicado. Con la
    // misma semilla sale el mismo reparto, asi que basta con guardar esto.
    DistributeRequest distribute;
    bool distributed = false;
};

std::string writeProject(const NoteProject& project);
// Nulo si el texto no es un proyecto o es de una version posterior; `error`
// dice por que.
std::optional<NoteProject> readProject(const std::string& text, std::string& error);
bool writeProjectFile(const NoteProject& project, const std::filesystem::path& path, std::string& error);

// Un estilo a JSON y de vuelta (para el proyecto y para compararlos).
std::string writeStyle(const NoteStyle& style);
std::optional<NoteStyle> readStyle(const std::string& text);

// Un programa de bloques a JSON y de vuelta (el proyecto y deshacer).
std::string writeProgram(const BlockProgram& program);
BlockProgram readProgram(const std::string& text);

}  // namespace fml::notelab

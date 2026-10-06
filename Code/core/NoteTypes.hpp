// fml_notelab — Catalogo de las notas custom que trae el mod (DESIGN_PLUGIN_NOTE_LAB §19.3).
//
// Todas las del mod, no solo las creadas: Codename `data/notes/<tipo>.hx` y su
// aspecto en `game/notes/<tipo>`; Psych `custom_notetypes/<tipo>.lua|.hx|.txt`
// y la textura que el tipo le pone; V-Slice las clases `NoteKind` de
// `scripts/notekinds/*.hxc` (NoteKind.hx:9) con su `noteStyleId`. Los tipos de
// serie de Psych (Note.hx:43-50) se incluyen como tales. Nunca ejecuta nada:
// lee los archivos como texto.
#pragma once

#include "NotePreview.hpp"
#include "NoteStyleRead.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fml { class Vfs; }

namespace fml::notelab {

// Lo que hace el bot del motor con las notas del tipo, leido del script:
// Psych `ignoreNote` (PlayState.hx:1822-1833, :3026; la Hurt Note lo pone solo
// del lado del jugador, `ignoreNote = mustPress`, Note.hx:200) y Codename
// `avoid` (Note.hx:42, StrumLine.hx:231).
enum class BotRule { Hits, Ignores, IgnoresOnPlayerSide };

struct NoteTypeEntry {
    Engine engine = Engine::Codename;
    std::string name;          // como la escribe el chart
    std::string script;        // ruta virtual del script; vacia si no tiene
    std::string config;        // Psych: el .txt del tipo
    std::string lookStyle;     // id del estilo con su aspecto; vacio = el de las notas normales
    std::string texture;       // Psych: la textura que declara el tipo
    std::string description;   // V-Slice: la del NoteKind
    bool builtin = false;      // de serie en el motor
    BotRule bot = BotRule::Hits;
    bool hitMisses = false;    // Psych `hitCausesMiss`: tocarla cuenta como fallo (PlayState.hx:3042)
};

std::vector<NoteTypeEntry> scanNoteTypes(const Vfs& vfs, const Catalog& catalog);

// Un tipo por su nombre en el chart (sin distinguir mayusculas); nulo si no
// esta. Con motor, solo los de ese motor.
const NoteTypeEntry* findNoteType(const std::vector<NoteTypeEntry>& types, const std::string& name);
const NoteTypeEntry* findNoteType(const std::vector<NoteTypeEntry>& types, const std::string& name, Engine engine);

// Si el bot deja pasar una nota del tipo en esa linea (1 = la del jugador).
bool botSkips(const NoteTypeEntry& type, int strumLine);

// Psych: un tipo que no trae textura y recolorea el skin EN USO, como la Hurt
// Note (Note.hx:199-208): sus hojas de nota llevan `rgbFixed`.
bool recolorsSkin(const NoteStyle& type);
// Ese tipo sobre un skin concreto (el del jugador, uno por cancion...): las
// notas y los sostenidos del skin con los colores del tipo, y la salpicadura
// del tipo si tiene una propia (la electrica de la Hurt Note).
NoteStyle recolorOver(const NoteStyle& skin, const NoteStyle& type);

// «Distribuir» (DESIGN_PLUGIN_NOTE_LAB §18, §19.5): meter tipos de nota custom
// en una copia del chart por porcentaje, con semilla. Las notas que ya tienen
// tipo se respetan; el resultado es el mismo con la misma semilla en cualquier
// equipo (generador propio, no el de la biblioteca estandar).
struct DistributeRule {
    std::string type;          // el tipo que se pone
    float percent = 10.0f;     // de las notas candidatas, 0-100
    std::uint32_t seed = 0;    // 0 = la del paquete
    int count = -1;           // -1 = porcentaje, >= 0 = cantidad exacta
};

struct DistributeFilter {
    int side = 2;                                // 0 rival, 1 jugador, 2 los dos
    std::array<bool, 4> lanes{true, true, true, true};
    bool skipSustains = false;                   // no en notas largas
    bool skipChords = false;                     // no en notas a la vez que otra de su linea
    double minGapMs = 0.0;                       // entre dos del mismo tipo puesto, en su linea
    bool replaceExisting = false;
};

struct DistributeRequest {
    std::vector<DistributeRule> rules;
    DistributeFilter filter;
    std::uint32_t seed = 1;                      // la del paquete
};

struct DistributeResult {
    std::vector<std::string> types;   // por nota: el tipo resultante (el suyo si ya tenia)
    std::vector<int> placed;          // por regla: cuantas se pusieron
    int candidates = 0;               // notas que podian recibir un tipo
    int protectedTypes = 0, filtered = 0;
    std::vector<int> requested;
};

struct BotProfile {
    bool enabled = false;
    int opponent = 0, player = 0;  // 0 heredar, 1 tocar, 2 evitar
};
bool botSkipsWithProfile(bool inherited, int side, const BotProfile& profile);

DistributeResult distributeTypes(const std::vector<PreviewNote>& notes, const std::vector<std::string>& currentTypes,
                                 const DistributeRequest& request);

}  // namespace fml::notelab

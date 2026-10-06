// fml_formats — `songs/<id>/meta.json` de Codename Engine.
//
// Este archivo es el que decide como se llama la cancion, a que BPM va, que
// icono saca y — lo importante para el importador — QUE DIFICULTADES tiene.
//
// Dos reglas que salen de leer el motor, no de suponer:
//
//   1. `Chart.hx:145` — si `meta.difficulties` esta vacio, las dificultades SON
//      los nombres de archivo de `songs/<id>/charts/*.json`. Y si salen
//      exactamente tres, el motor las reordena a easy/normal/hard. Es decir: el
//      orden importa y NO es alfabetico. Ordenarlas por nombre (lo que haciamos)
//      cambia el orden en el que aparecen en el juego.
//
//   2. `Chart.hx:348 filterMetaForSaving` — al guardar, el motor BORRA campos:
//      `name`, `variant`, `metas`, `parsedColor`, y los sufijos vacios. Si los
//      escribimos, generamos un meta.json que el motor jamas habria escrito.
//
// Se conserva el JSON original entero (`ordered_json`, que mantiene el orden de
// insercion) y solo se tocan las claves que cambian. Asi sobreviven los campos
// que no entendemos, que es la regla de DESIGN §1.4.
#pragma once

#include "../core/Diagnostics.hpp"
#include <algorithm>

#include <string>
#include <vector>

namespace fml {

// Valores de `funkin/backend/system/Flags.hx`, copiados literales.
namespace codename_defaults {
constexpr float       kBpm             = 100.0f;
constexpr int         kBeatsPerMeasure = 4;
constexpr int         kStepsPerBeat    = 4;
inline const char*    kHealthIcon      = "face";
inline const char*    kColor           = "#9271FD";
inline const char*    kDifficulty      = "normal";
inline const char*    kStage           = "stage";
inline const char*    kCharacter       = "bf";
inline const char*    kOpponent        = "dad";
inline const char*    kGirlfriend      = "gf";
constexpr float       kScrollSpeed     = 2.0f;
constexpr bool        kCoopAllowed     = false;
constexpr bool        kOpponentMode    = false;
}  // namespace codename_defaults

struct SongMeta {
    // `name` NO se escribe al json: el motor lo deduce de la carpeta y lo borra
    // en filterMetaForSaving. Se guarda aqui solo para no tener que arrastrarlo
    // por separado.
    std::string name;
    std::string displayName;

    float bpm             = codename_defaults::kBpm;
    int   beatsPerMeasure = codename_defaults::kBeatsPerMeasure;
    int   stepsPerBeat    = codename_defaults::kStepsPerBeat;

    std::string icon  = codename_defaults::kHealthIcon;
    std::string color = codename_defaults::kColor;

    bool coopAllowed         = codename_defaults::kCoopAllowed;
    bool opponentModeAllowed = codename_defaults::kOpponentMode;
    bool needsVoices         = true;

    std::string instSuffix;
    std::string vocalsSuffix;

    // CORPUS §19: cadenas libres. Ni easy/normal/hard ni minusculas garantizadas.
    std::vector<std::string> difficulties;
    std::vector<std::string> variants;

    // De donde vino, y el texto tal cual. Vacio = la cancion no tiene meta.json.
    std::string sourcePath;
    std::string sourceText;

    // true si `difficulties` se dedujo del contenido de charts/ en vez de venir
    // declarado. Cambia lo que hay que escribir al añadir una dificultad.
    bool difficultiesInferred = false;
};

// Lee un meta.json. Con `text` vacio devuelve los defaults del motor, que es
// exactamente lo que hace `Chart.loadChartMeta` cuando el archivo no existe.
SongMeta parseSongMeta(const std::string& text, const std::string& songId,
                       const std::string& sourcePath, DiagnosticSink& sink);

// Aplica el orden del motor: si hay exactamente tres y son easy/normal/hard
// (en cualquier caja), las coloca en ese orden. Si no, respeta el orden dado.
void applyEngineDifficultyOrder(std::vector<std::string>& difficulties);

// Explicit user preference: keep every other difficulty in its current order.
inline bool setBaseDifficulty(SongMeta& meta, const std::vector<std::string>& available,
                               const std::string& selected) {
    const auto found = std::find(available.begin(), available.end(), selected);
    if (found == available.end()) return false;
    meta.difficulties = available;
    meta.difficulties.erase(meta.difficulties.begin() + (found - available.begin()));
    meta.difficulties.insert(meta.difficulties.begin(), selected);
    meta.difficultiesInferred = false;
    return true;
}

// Serializa sobre `pristineText` conservando orden y campos desconocidos.
// Con `pristineText` vacio escribe un meta.json nuevo con el orden de claves
// que usa el motor.
std::string serializeSongMeta(const SongMeta& meta, const std::string& pristineText);

}  // namespace fml

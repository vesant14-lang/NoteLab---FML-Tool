// fml_notelab — Canciones del mod para la vista previa (DESIGN_PLUGIN_NOTE_LAB §12.2).
//
// Encuentra los charts de Codename (nativo y antiguo), Psych y V-Slice que haya
// en lo montado, los lee con los lectores del nucleo y localiza su audio. La
// busqueda sigue la de Song Lab de Funkin Atlas (ModExplorer/src/SongLab.hpp),
// llevada al nucleo sin la app: Atlas puede adoptarla despues sin duplicarla.
#pragma once

#include "NotePreview.hpp"
#include "../support/core/Chart.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace fml { class Vfs; }

namespace fml::notelab {

struct SongChart {
    enum class Format { Codename, CodenameLegacy, VSlice, Psych };
    Format format = Format::Psych;
    std::string id;
    std::string difficulty;
    std::string variation;
    std::string path;            // chart, ruta virtual
    std::string metadataPath;    // meta.json de Codename o -metadata de V-Slice
    std::string stage, player, opponent, girlfriend;
    int difficultyOrder = 1000;
};
const char* songFormatKey(SongChart::Format format);

// Todos los charts de lo montado, ordenados por cancion y dificultad.
std::vector<SongChart> scanSongs(const Vfs& vfs);

// Un cambio de velocidad de scroll del chart, ya resuelto a la que queda
// (DESIGN_PLUGIN_NOTE_LAB §27): Psych «Change Scroll Speed» (PlayState.hx:2254-2270),
// V-Slice «ScrollSpeed» (ScrollSpeedEvent.hx:37-90) y Codename «Scroll Speed
// Change» (PlayState.hx:1695-1705).
struct SpeedChange {
    double timeMs = 0.0;
    float speed = 1.0f;          // la que queda
    double durationMs = 0.0;     // 0 = de golpe; si no, la transicion (lineal aqui)
    int strumLine = -1;          // -1 las dos, 0 rival, 1 jugador (V-Slice `strumline`)
};

// Lo que el chart fija ademas de sus notas, para que la vista previa lo use
// todo al cargarlo.
struct SongValues {
    float speed = 1.0f;                     // la de la dificultad
    std::vector<SpeedChange> speedChanges;  // por tiempo
    // Codename: la `scrollSpeed` propia de una strumline manda sobre la del
    // chart y sus eventos (StrumLine.hx:176-178, :390-391); 0 = no tiene.
    std::array<float, 2> lineSpeed{0.0f, 0.0f};
    float bpm = 100.0f;
    std::vector<BpmChange> bpmChanges;      // el primero es el inicial
    int beatsPerMeasure = 4, stepsPerBeat = 4;
    // Posicion de la cancion = la del audio + esto: Psych `offset`
    // (PlayState.hx:679, :1651) y V-Slice `offsets.instrumental`
    // (Conductor.hx:433, PlayState.hx:874). Codename no lo guarda en el chart.
    double audioOffsetMs = 0.0;
    std::string noteStyle;                  // V-Slice `playData.noteStyle`
    std::string arrowSkin, splashSkin;      // Psych (Song.hx:31-32)
    std::string player, opponent, girlfriend, stage;
    bool needsVoices = true;
};

// La velocidad de scroll en un momento para una linea (0 rival, 1 jugador).
float speedAt(const SongValues& values, double ms, int strumLine);

struct LoadedSong {
    UniversalChart chart;
    SongValues values;
    std::vector<std::string> audio;   // rutas fisicas: instrumental primero, despues voces
    std::string error;                // vacio si se leyo el chart
    std::string warning;              // audio que no aparecio
};

// Lee el chart y localiza el audio. `root` es la carpeta montada del mod, la
// que usa la resolucion de audio de Psych. El reproductor necesita un archivo
// en disco: el audio de un ZIP se saca a la cache temporal de la Vfs.
LoadedSong loadSong(const Vfs& vfs, const std::filesystem::path& root, const SongChart& song);

// Las notas del chart en el formato de la vista previa: la strumline del rival
// (tipo 0) y la del jugador (tipo 1); las adicionales no se dibujan todavia.
// `types` recibe, por nota, su tipo de nota ("" si no tiene).
std::vector<PreviewNote> previewNotesOf(const UniversalChart& chart, std::vector<std::string>* types = nullptr);

std::string patchSongNoteTypes(const std::string& original, const SongChart& song,
    const std::vector<PreviewNote>& notes, const std::vector<std::string>& types, std::string& error);
bool saveSongChart(const std::filesystem::path& target, const std::string& text,
    const std::string& expectedOriginal, bool replaceOriginal, std::filesystem::path& backup, std::string& error);

}  // namespace fml::notelab

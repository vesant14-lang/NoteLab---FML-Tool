// fml_core — Chart universal y conversion beat <-> ms.
//
// Modelo comun para charts legacy de FNF y charts nativos de Codename
// (`strumLines` + `events`). Conserva la identidad de cada strumline para no
// reducir personajes adicionales al binario player/opponent.
//
// Una nota del chart es `[tiempoMs, direccion, sustainMs, tipo?]` con direccion
// 0..7. Quien la canta NO sale de la direccion sola: sale de cruzarla con
// `mustHitSection` de su seccion, que es la regla clasica del motor:
//
//     gottaHitNote = section.mustHitSection;
//     if (dir > 3) gottaHitNote = !gottaHitNote;
#pragma once

#include <string>
#include <vector>

namespace fml {

enum class ChartFormat { Legacy, CodenameNative };

inline const char* toString(ChartFormat format) {
    switch (format) {
        case ChartFormat::Legacy:         return "legacy";
        case ChartFormat::CodenameNative: return "codename-native";
    }
    return "unknown";
}

struct ChartStrumLine {
    int                      type = 0;       // 0 opponent, 1 player, 2 additional
    std::string              position;       // dad, boyfriend, girlfriend, custom
    std::vector<std::string> characters;
    bool                     visible = true; // visibilidad de receptores, no del personaje
    int                      keyCount = 4;

    // Opciones que expone CharterStrumlineScreen en Codename. Son opcionales:
    // la ausencia de `scrollSpeed`, por ejemplo, significa "heredar la del
    // chart", que no es lo mismo que escribir el mismo numero a mano.
    bool  hasStrumPos     = false;
    float strumPosX       = 0.0f;
    float strumPosY       = 50.0f;
    bool  hasStrumLinePos = false;
    float strumLinePos    = 0.25f;
    bool  hasStrumScale   = false;
    float strumScale      = 1.0f;
    bool  hasStrumSpacing = false;
    float strumSpacing    = 1.0f;
    bool  hasScrollSpeed  = false;
    float scrollSpeed     = 1.0f;
    std::string vocalsSuffix;

    // Identidad de la linea en el documento que se abrio y objeto sin `notes`.
    // Permiten reordenar/agregar lineas conservando extensiones custom que FML
    // no conoce, en vez de reconstruirlas como un objeto minimo.
    int         sourceIndex = -1;
    std::string sourceJson;
};

// Los parametros se conservan como texto sin perder strings, bools ni numeros.
// El runtime interpreta solo los eventos que sabe previsualizar; los demas
// siguen disponibles para diagnostico y futuras capas de comportamiento.
struct ChartEvent {
    double                   timeMs = 0.0;
    std::string              name;
    // Texto legible para enseñar. Pierde el tipo: la cadena "true" y el booleano
    // true se ven igual.
    std::vector<std::string> params;
    // El literal JSON EXACTO de cada parametro (`"dad"`, `1`, `true`). Es lo que
    // se vuelve a escribir al guardar: sin esto, reescribir un evento convierte
    // numeros en cadenas y el motor deja de entenderlo.
    std::vector<std::string> paramsJson;
    bool                     global = false; // procede de songs/<id>/events.json
    int                      sourceIndex = -1;
    std::string              sourceJson; // conserva extensiones custom al reescribir
    // Origen ajeno: el evento vive en un archivo que el chart de Codename no
    // referencia y que carga un script del propio mod. Se lee y se ensena, pero
    // NUNCA se reescribe: FML no es dueno de ese formato y reescribirlo seria
    // convertir un dialecto en "el formato". Vacio = el evento es del chart.
    std::string              foreignSource;   // ruta virtual del archivo
    std::string              foreignDialect;  // como se reconocio
};

struct ChartNote {
    double      timeMs    = 0.0;
    int         lane      = 0;      // columna original de la strumline
    int         direction = 0;      // 0..3 = izquierda, abajo, arriba, derecha
    double      sustainMs = 0.0;
    bool        isPlayer  = false;  // true = la canta BF
    int         strumLine = -1;     // identidad real en charts Codename
    std::string type;               // tipo de nota custom, si lo declara
    std::string sourceJson;         // objeto original, incluidos campos desconocidos
    // Singer is independent from the receptor/player that owns the note.
    // -1 inherits strumLine. Psych GF Sing/gfSection can target a third actor.
    int         singerStrumLine = -1;
};

// `ChartBookmark` de Codename (`ChartData.hx:66`): un punto con nombre y color
// dentro de la cancion. Es lo unico del formato nativo que FML no representaba,
// y en una cancion de siete minutos es la diferencia entre buscar a ojo y saltar
// donde toca.
struct ChartBookmark {
    double      timeMs = 0.0;
    std::string name;
    std::string color;      // literal tal cual viene; el motor acepta "#rrggbb"
    std::string sourceJson; // objeto original, con los campos que no conozcamos
};

struct BpmChange {
    double timeMs = 0.0;
    float  bpm    = 100.0f;
};

struct UniversalChart {
    ChartFormat format = ChartFormat::Legacy;
    std::string songId, difficulty, sourcePath;
    std::string player1, player2, gfVersion, stage;
    float       bpm   = 100.0f;
    float       speed = 1.0f;
    int         beatsPerMeasure = 4;
    int         stepsPerBeat    = 4;
    bool        needsVoices = true;

    std::vector<ChartNote> notes;       // ordenadas por tiempo
    std::vector<BpmChange> bpmChanges;  // ordenadas por tiempo; siempre >= 1
    std::vector<ChartStrumLine> strumLines;
    std::vector<ChartEvent>     events;
    std::vector<std::string>    noteTypes;
    std::vector<ChartBookmark>  bookmarks;   // ordenados por tiempo

    double lastNoteMs() const {
        double t = 0.0;
        for (const auto& n : notes) t = (n.timeMs + n.sustainMs > t) ? n.timeMs + n.sustainMs : t;
        return t;
    }
};

// Conversion bidireccional beat <-> ms respetando los cambios de BPM.
// Es lo que hace funcionar «ir al beat 240» y la barra de tiempo.
class TimeMap {
public:
    void build(const UniversalChart& chart);

    double beatAt(double ms) const;
    double msAtBeat(double beat) const;
    float  bpmAt(double ms) const;

    // Duracion de un paso (1/4 de beat) en ms, en ese punto.
    double stepMsAt(double ms) const { return 60000.0 / bpmAt(ms) / 4.0; }

private:
    struct Segment { double startMs; double startBeat; float bpm; };
    std::vector<Segment> m_segments;
};

}  // namespace fml

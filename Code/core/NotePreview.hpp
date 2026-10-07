// fml_notelab — Vista previa de un estilo de notas, sin OpenGL.
//
// Construye una RenderList en coordenadas de juego (1280x720, la camHUD) con
// receptores, notas, sostenidos y salpicaduras de CUALQUIER motor, a partir del
// modelo neutro: las hojas separadas y la tira de sostenidos de V-Slice se
// dibujan igual que el atlas unico de Codename y Psych. El renderer de FML la
// pinta; este modulo no sabe nada de GL.
//
// La colocacion es la comun a los tres motores (carriles de 160 * escala, la
// linea de receptores a 50 px del borde, la distancia 0.45 * velocidad por ms),
// no la exacta de cada uno: eso es la fase del HUD (DESIGN_PLUGIN_NOTE_LAB §12.5).
#pragma once

#include "NoteStyle.hpp"
#include "../support/runtime/Scene.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fml::notelab {

enum class StrumState { Static, Press, Confirm };

struct PreviewNote {
    int strumLine = 1;        // 0 rival, 1 jugador
    int lane = 0;             // 0..3
    double timeMs = 0.0;
    double sustainMs = 0.0;
    // Lo que su tipo le dice al motor: el bot la deja pasar sin fallo (Psych
    // ignoreNote, PlayState.hx:1833 y :3026; Codename avoid, StrumLine.hx:231)
    // y tocarla es un fallo (Psych hitCausesMiss, PlayState.hx:3042).
    bool botSkips = false;
    bool hitMisses = false;
    float alpha = 1.0f, scaleX = 1.0f, scaleY = 1.0f, angle = 0.0f;
};

// Una capa del HUD de la vista previa (el popup de juicio o las cifras del
// combo): visible, tamano, opacidad y desplazamiento en px de juego desde su
// sitio. Es de la vista, no del estilo: no cambia lo que se exporta.
struct HudLayer {
    bool visible = true;
    float scale = 1.0f;
    float alpha = 1.0f;
    float x = 0.0f, y = 0.0f;
};

struct PreviewSettings {
    bool downscroll = false;
    bool showOpponent = true;
    bool showPlayer = true;
    float scrollSpeed = 1.0f;
    // La de cada linea en este momento (la del chart con sus cambios); 0 =
    // `scrollSpeed`.
    std::array<float, 2> lineSpeed{0.0f, 0.0f};
    HudLayer judgement, combo;
    // Un juicio y un combo de muestra, quietos, para colocarlos sin jugar.
    bool hudSample = false;
};

// Lo que cambia de un fotograma a otro.
struct PreviewState {
    double songMs = 0.0;
    std::array<StrumState, 8> strum{};          // strumLine * 4 + lane
    std::array<double, 8> strumSinceMs{};       // cuando empezo ese estado
    std::array<double, 8> confirmUntilMs{};     // el autojuego suelta aqui
    // `roll` elige la variante al dibujar, con las que tenga el estilo de la
    // nota que la causo (`note`, -1 = sin nota).
    struct Splash { int strumLine = 1; int lane = 0; std::uint32_t roll = 0; double startMs = 0.0; int note = -1; };
    std::vector<Splash> splashes;
    // La linea que juega la persona: solo esa salpica y puntua, como el
    // jugador en los tres motores (Codename PlayState.hx:1993-1995, Psych
    // PlayState.hx:2576, V-Slice PlayState.hx:3150).
    int playerLine = 1;
    std::vector<std::uint8_t> hit;              // por nota: ya acertada
    std::uint32_t splashSeed = 0x5EED2026u;
    // Popup de juicio y combo del jugador (la strumline 1), con las imagenes
    // del HUD del propio estilo.
    struct Popup { int judgement = 0; int combo = 0; double startMs = 0.0; };
    std::vector<Popup> popups;
    int combo = 0;
};

class NotePreview {
public:
    // `images` da el tamano de las imagenes sin atlas (la tira de V-Slice);
    // puede ser nulo y entonces esas piezas no se dibujan.
    void build(const NoteStyle& style, AtlasStore& atlases, IImageInfo* images,
               const PreviewSettings& settings, const PreviewState& state,
               const std::vector<PreviewNote>& notes, RenderList& out);

    // Centro de un carril y de la linea de receptores, en coordenadas de juego.
    static float laneCenterX(int strumLine, int lane, float laneWidth);
    static float strumCenterY(bool downscroll, float laneWidth);
    // Separacion de los carriles: 112 px en los tres motores, sea cual sea la
    // escala de las piezas (Codename y Psych `Note.swagWidth = 160 * 0.7`,
    // Psych Note.hx:110; V-Slice `Strumline.NOTE_SPACING`, Strumline.hx:42).
    static float laneWidthOf(const NoteStyle& style);
    // Centro del juicio y de las cifras del combo, en px de juego, con el
    // desplazamiento de sus capas.
    static void hudCenters(const PreviewSettings& settings, float& judgementX, float& judgementY,
                           float& comboX, float& comboY);

    // Cuantas variantes de salpicadura tiene una direccion.
    static int splashVariants(const NoteStyle& style, int lane);
    // Duracion de una animacion en ms (0 si no se puede saber o hace bucle).
    double animationLengthMs(const NoteStyle& style, AtlasStore& atlases, Part part, int lane,
                             int variant);

    void clearCache() {
        m_frames.clear();
        m_grids.clear();
    }

    // Textura para una pieza de un carril; vacio = la imagen de la hoja. La
    // app la usa para la paleta RGB de Psych, que colorea cada carril a su
    // manera (RGBPalette.hx:149-153) salvo el receptor en reposo
    // (StrumNote.hx:169).
    std::function<std::string(const NoteStyle&, const Sheet&, Part, int lane)> textureFor;
    // Estilo de una nota concreta (su tipo de nota custom); nulo = el del mod.
    std::function<const NoteStyle*(size_t note)> styleForNote;
    // Las salpicaduras que pide la cancion (Psych `splashSkin`); nulo = las
    // del estilo.
    const NoteStyle* splashStyle = nullptr;
    const NoteStyle* receptorFallback = nullptr;
    // Los receptores de otro estilo, solo para verlos con estas notas (el
    // estilo no cambia); nulo = los suyos.
    const NoteStyle* receptorStyle = nullptr;

private:
    struct Frames {
        const SparrowAtlas* atlas = nullptr;
        std::vector<size_t> indices;
    };
    const Frames& framesOf(AtlasStore& atlases, const Sheet& sheet, const Animation& anim);
    // Una rejilla pixel como atlas: un fotograma por celda, numeradas como
    // Flixel (FlxTileFrames.hx:296-305). Nulo sin el tamano de la imagen.
    const SparrowAtlas* gridAtlas(const Sheet& sheet);
    std::map<std::string, Frames> m_frames;
    std::map<std::string, SparrowAtlas> m_grids;
    IImageInfo* m_images = nullptr;   // el de la ultima llamada a build()
};

const PartBinding* findPart(const NoteStyle& style, Part part, int lane, int variant = 0);

// Los fotogramas de una animacion como los busca Flixel: el prefijo y, si no da
// ninguno, cada alternativa; con `indices`, los de ese numero tras el prefijo.
std::vector<size_t> animationFrames(const SparrowAtlas& atlas, const Animation& anim);

// Donde cae el centro de la caja de una salpicadura (`boxW` x `boxH`, sin
// escalar) respecto al centro del receptor cuando su offset es 0, en px de
// juego; con offset, el centro queda en ancla - offset. Codename la centra
// (SplashGroup.hx:129). Psych la pone en (x - 160 * 0.7 * 0.95, y - 160 * 0.7)
// con un offset base de 10 (NoteSplash.hx:210, :279) y el receptor mide
// 160 * 0.7. V-Slice depende del receptor: false, sin ancla conocida.
bool splashAnchor(Engine engine, float boxW, float boxH, float& x, float& y);

// Patron sin chart: los dos lados se contestan cada cuatro tiempos, con
// sostenidos. `lengthMs` devuelve cuanto dura antes de repetirse.
std::vector<PreviewNote> demoPattern(double bpm, double& lengthMs);

// Mas patrones de prueba (pedido del autor, 6 oct 2026): 0 basico (el de
// arriba), 1 escalera, 2 repeticiones (jacks), 3 acordes, 4 sostenidos largos,
// 5 rafaga (stream) y 6 aleatorio (con su semilla). Cuatro compases, los dos
// lados se turnan.
constexpr int kDemoKinds = 7;
std::vector<PreviewNote> demoPatternOf(int kind, double bpm, double& lengthMs, std::uint32_t seed = 1);

// Un patron propio, hecho en una cuadricula de semicorcheas (cuatro por
// tiempo, dieciseis por compas) y guardado en las preferencias.
struct PatternNote {
    int side = 1;      // 0 rival, 1 jugador
    int lane = 0;      // 0..3
    int step = 0;      // semicorchea desde el principio
    int hold = 0;      // largo del sostenido en semicorcheas (0 = nota sin sostenido)
};
struct CustomPattern {
    std::string name;
    int bars = 4;      // 1..16
    std::vector<PatternNote> notes;
};
constexpr int kPatternMaxBars = 16;
constexpr size_t kPatternMaxNotes = 4096;
std::vector<PreviewNote> customPatternNotes(const CustomPattern& pattern, double bpm, double& lengthMs);
// Un patron de serie pasado a la cuadricula, para editarlo como propio.
CustomPattern patternFromNotes(const std::vector<PreviewNote>& notes, double bpm, int bars);

// Autojuego perfecto: cada nota de un lado automatico que llega a su tiempo
// entre `fromMs` y `toMs` enciende el confirm y, en la linea del jugador, la
// salpicadura y el popup. Las que el bot deja pasar siguen de largo.
// `autoSide[linea]` dice que lados juega la maquina.
void advanceAutoplay(PreviewState& state, const NoteStyle& style,
                     const std::vector<PreviewNote>& notes, double fromMs, double toMs,
                     const std::array<bool, 2>& autoSide, double splashLengthMs);

// Pulsaciones del jugador: pulsar enciende press (o confirm si hay nota en la
// ventana) y soltar devuelve el receptor a static.
struct PressResult { bool hit = false; int note = -1; double offsetMs = 0.0; };
PressResult pressLane(PreviewState& state, const NoteStyle& style, const std::vector<PreviewNote>& notes,
                      int strumLine, int lane, double windowMs);
void releaseLane(PreviewState& state, int strumLine, int lane);

}  // namespace fml::notelab

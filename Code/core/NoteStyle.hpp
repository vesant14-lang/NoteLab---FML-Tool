// fml_notelab — Modelo neutro de un estilo de notas (DESIGN_PLUGIN_NOTE_LAB §10-§11).
//
// Es el `notestyle` de V-Slice (NoteStyleData.hx), que DECLARA lo que Codename y
// Psych fijan en su codigo: cada lector rellena los prefijos que impone su motor
// y el validador los busca en el atlas como lo haria Flixel. Sobre este modelo
// se exporta despues a los tres motores.
//
// REGLA (la de fml_formats): este modulo no enlaza ImGui ni OpenGL ni conoce la
// app. Lo usan igual FML y el standalone de Note Lab.
#pragma once

#include "../support/core/Diagnostics.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fml::notelab {

enum class Engine { Codename, Psych, VSlice };
const char* engineKey(Engine engine);   // "codename", "psych", "vslice"

// Las piezas de un estilo. Las del receptor son las de V-Slice
// (NoteStyleData.hx:223-235); Codename y Psych tienen static, press y confirm
// (StrumLine.hx:408-410, StrumNote.hx:119-121).
enum class Part {
    Note, HoldPiece, HoldEnd,
    StrumStatic, StrumPress, StrumConfirm, StrumConfirmHold,
    Splash, HoldCoverStart, HoldCover, HoldCoverEnd,
};
constexpr int kPartCount = 11;
const char* partKey(Part part);
// left, down, up, right: la direccion es `i % 4` en los tres motores.
const char* directionKey(int direction);

enum class SheetKind {
    Sparrow,   // PNG + XML (TextureAtlas)
    Packer,    // PNG + TXT de Packer
    Strip,     // imagen partida en columnas: sostenidos de V-Slice (SustainTrail.hx:231)
    Grid,      // imagen partida en celdas iguales: notas pixel
    Image,     // imagen suelta, un fotograma
};
const char* sheetKindKey(SheetKind kind);

struct Sheet {
    std::string image;      // ruta virtual del PNG, ya resuelta; vacia si no se encontro
    std::string atlas;      // ruta virtual del XML/TXT; vacia en Strip, Grid e Image
    std::string declared;   // como lo pidio el origen: "shared:notes", "game/notes/default"
    SheetKind kind = SheetKind::Sparrow;
    int columns = 0, rows = 0;   // Strip y Grid
    float scale = 1.0f;
    float offsetX = 0.0f, offsetY = 0.0f;
    float alpha = 1.0f;
    bool pixel = false;
    // Psych: los tres colores (ARGB) de la paleta RGB cuando un tipo de nota
    // los fija, como la Hurt Note (Note.hx:206-208); vacio = los del jugador
    // en cada carril (ClientPrefs.hx:28-37).
    std::vector<std::uint32_t> rgbFixed;
};

// Lo que Flixel hace con addByPrefix/addByIndices. `alternatives` son los
// prefijos que el motor prueba si el primero no da fotogramas: el final morado
// del juego base se llama `pruple end hold` (Codename Note.hx:174-177, Psych
// Note.hx:439-441).
struct Animation {
    std::string prefix;
    std::vector<std::string> alternatives;
    std::vector<int> indices;   // vacio = todos los del prefijo
    float fps = 24.0f;
    bool loop = false;
    float offsetX = 0.0f, offsetY = 0.0f;
};

struct PartBinding {
    Part part = Part::Note;
    int direction = 0;   // 0..3
    int variant = 0;     // salpicaduras: una direccion puede tener varias
    int sheet = -1;      // indice en NoteStyle::sheets
    Animation animation;
    bool inherited = false;   // viene del `fallback` de V-Slice
    // La pone el script del tipo (`NoteStyle::lookScript`) de una forma que
    // Note Lab no sabe leer: no se comprueba y se dice como informacion
    // (FML-NOTE-022).
    bool unread = false;
};

// Imagen de HUD: cuenta atras, juicio, cifra o rotulo del combo.
struct HudAsset {
    std::string image;          // ruta virtual resuelta; vacia si no se encontro
    std::string declared;       // como lo pidio el origen
    std::string sound;          // cuenta atras: ruta virtual resuelta
    std::string soundDeclared;
    // Sin imagen donde la busca el motor: otro archivo con el mismo nombre en la
    // misma instalacion (Mario's Madness guarda `rating/sick.png`), si lo hay.
    std::string nearby;
    float scale = 1.0f;
    bool pixel = false;
    // Sin imagen por diseno: el primer paso de la cuenta atras solo suena
    // (Flags.hx:200 en Codename, `assetPath: null` en V-Slice).
    bool imageOptional = false;
    bool inherited = false;
};

// Donde lo usa el motor. Psych distingue tres skins que no son lo mismo
// (DESIGN_PLUGIN_NOTE_LAB §14.4).
enum class StyleUse {
    Default,        // todo el mod
    NoteType,       // un tipo de nota (Codename game/notes/<tipo>)
    Song,           // una cancion (arrowSkin de Psych)
    PlayerChoice,   // elegible en opciones (Psych noteSkins/NOTE_assets-<x>)
    Declared,       // un notestyle de V-Slice o una salpicadura suelta
};
const char* styleUseKey(StyleUse use);

struct StyleSound { std::string name, path; };

struct NoteStyle {
    Engine engine = Engine::Codename;
    std::string id;           // unico en el catalogo: "vslice:assets/funkin"
    std::string name;         // para la interfaz
    std::string author;
    std::string definition;   // archivo que lo declara
    std::string scope;        // carpeta raiz de su contenido, acabada en '/' o vacia
    std::string fallback;     // V-Slice: id declarado de otro estilo
    StyleUse use = StyleUse::Default;
    std::string useDetail;    // tipo de nota, cancion...
    // Psych 0.7+: un sombreador recolorea notas, receptores y salpicaduras con
    // los colores del jugador salvo `disableNoteRGB` (Note.hx:106, :176-177).
    bool rgbPalette = false;
    bool pixel = false;
    // El motor lo carga por si mismo: el skin por defecto, uno elegible en
    // `list.txt`, el que pide un chart, un notestyle de V-Slice... Un atlas
    // suelto en noteSkins/ o una salpicadura que solo pediria un script no lo
    // esta, y sus fallos se dicen como informacion.
    bool referenced = true;
    // Los forks de Psych con mas teclas nombran los fotogramas por letra
    // (`A`, `A hold`, `A tail`, `A press`, `A confirm`): Psych normal no los
    // dibujaria, el ejecutable del fork si.
    bool letteredNaming = false;
    // Es de una instalacion cuyo skin por defecto ya va por letra: el motor es
    // un fork y sus tipos de nota siguen sus propios nombres, que Note Lab no
    // comprueba pieza a pieza.
    bool forkNaming = false;
    // Codename: el script `data/notes/<tipo>` cancela onNoteCreation, asi que
    // el motor ya no carga el sprite ni anade las animaciones del juego base
    // (Note.hx:164-195) y las piezas llevan lo que anade el script. Vacio si
    // no hay tal script.
    std::string lookScript;
    // Codename: un script general elige la hoja con una ruta hecha
    // (`event.noteSprite = "game/voiid/notes/" + skin`) y Note Lab la encontro
    // por el nombre del tipo; el script es este (FML-NOTE-026).
    std::string nameScript;
    // Codename con multikey: los fotogramas siguen los nombres que declara el
    // mod para 4 teclas en este archivo (`data/multikeyData.xml`), no los del
    // juego base (FML-NOTE-027).
    std::string multikeyData;
    std::vector<Sheet> sheets;
    std::vector<PartBinding> parts;
    bool hasHud = false;
    std::array<HudAsset, 4> countdown;    // three, two (ready), one (set), go
    std::array<HudAsset, 4> judgements;   // sick, good, bad, shit
    std::array<HudAsset, 10> digits;
    HudAsset combo;
    std::vector<StyleSound> sounds;
};

struct StyleComponents {
    bool notes = false;
    bool receptors = true;
    bool splashes = false;
    bool holdCovers = false;
    bool judgements = false;
    bool combo = false;
    bool countdown = false;
    bool sounds = false;
    bool includes(Part part) const;
};

int composeStyle(NoteStyle& target, const NoteStyle& donor, const StyleComponents& components);

const char* judgementKey(int index);   // sick, good, bad, shit
const char* countdownKey(int index);   // three, two, one, go

// Anade la hoja si no hay ya una con la misma imagen y atlas; devuelve su indice.
int addSheet(NoteStyle& style, const Sheet& sheet);
void bindPart(NoteStyle& style, Part part, int direction, int sheet, Animation animation,
              int variant = 0);

// Lo que se encontro al leer o validar. El texto se compone en ingles o en
// espanol con `describe` (NoteStyleCheck.hpp), no se guarda traducido.
struct Finding {
    Severity severity = Severity::Warning;
    std::string code;      // FML-NOTE-###
    std::string style;     // id del estilo; vacio si es de lectura general
    std::string subject;   // "note/left", "splash/up#2", "judgement/sick"
    std::string path;      // archivo implicado
    std::string detail;    // prefijo, nombre declarado o motivo
};

}  // namespace fml::notelab

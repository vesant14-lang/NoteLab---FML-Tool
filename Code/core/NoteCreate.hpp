// fml_notelab — Crear sin dibujar (DESIGN_PLUGIN_NOTE_LAB §31): pintar un
// estilo pieza a pieza (HUD de flechas nuevo, aspecto de una nota custom),
// marcas encima de las notas, textos del ranking como imagenes y reconocer lo
// que se importa por los nombres que usan los motores.
//
// Como el resto del nucleo: sin ImGui ni OpenGL. Las imagenes entran y salen
// como bytes; montarlas y guardarlas es cosa de quien llama.
#pragma once

#include "NoteExport.hpp"
#include "NoteImage.hpp"
#include "NoteStyle.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fml::notelab {

// ------------------------------------------------------------------- color --

enum class PaintMode {
    Keep,       // los pixeles como estan
    Hue,        // a un color: tono, saturacion y luz se llevan al del color
    Gradient,   // tres colores por la luz de cada pixel: oscuro, medio y claro
};

enum class MarkPlace { Center, Top, Corner };

// Como se pinta una pieza. Los colores son ARGB, como los guarda Haxe.
struct Paint {
    PaintMode mode = PaintMode::Keep;
    std::uint32_t color = 0xFFFFFFFFu;           // Hue
    // Gradient: oscuro (el contorno), medio (el relleno) y claro (el brillo).
    // Sobre una plantilla RGB de Psych son su paleta: b, r y g
    // (RGBPalette.hx:149-153).
    std::array<std::uint32_t, 3> stops{0xFF000000u, 0xFF808080u, 0xFFFFFFFFu};
    float strength = 1.0f;                       // 0 el original, 1 del todo
    float alpha = 1.0f;                          // opacidad horneada
    const Image* mark = nullptr;                 // marca encima: solo notas
    MarkPlace markPlace = MarkPlace::Center;
    float markSize = 0.45f;                      // del lado menor de lo visible
    float markAlpha = 1.0f;
    bool changes() const;
};

void paintImage(Image& image, const Paint& paint);
// La marca sobre lo visible (alfa > 1/16) de la imagen: centrada, arriba o en
// la esquina de arriba a la derecha, con su proporcion.
void stampMark(Image& image, const Image& mark, MarkPlace place, float size, float alpha);

// Las hojas de un estilo pintadas region a region: cada fotograma (o celda o
// columna) con el Paint de la pieza y la direccion que lo usan. Una plantilla
// RGB de Psych (`rgbPalette`) se hornea antes con los colores por defecto de
// cada carril (ClientPrefs.hx:28-37) para que lo pintado sea lo que se ve; la
// variante resultante ya no lleva paleta. Una region de Sparrow que comparten
// piezas pintadas distinto (los cuatro tramos de la plantilla RGB de Psych
// 0.7) se copia debajo de la hoja para cada aspecto y el atlas se reescribe:
// `atlas` es la ruta del original y `atlasText`/`xml` el nuevo (vacios si no
// cambia). En una rejilla o tira la region se queda con la primera pieza.
// Salen solo las hojas que cambian: su ruta virtual y el PNG nuevo.
struct PaintedSheet {
    std::string image;
    std::vector<unsigned char> png;
    std::string atlas;
    std::string xml;
};
std::vector<PaintedSheet> paintStyleSheets(const NoteStyle& style, const std::function<Paint(Part, int)>& paintFor,
                                           const ExportIo& io);
// Lo mismo sin codificar: para verlo en vivo mientras se cambian los colores.
struct PaintedImage {
    std::string image;
    Image pixels;
    std::string atlas;
    std::string atlasText;
};
std::vector<PaintedImage> paintStyleImages(const NoteStyle& style, const std::function<Paint(Part, int)>& paintFor,
                                           const ExportIo& io);

// ------------------------------------------------------------------- texto --

struct TextLook {
    float size = 120.0f;                         // cuerpo de la letra (em), px
    std::uint32_t top = 0xFFFFFFFFu;             // degradado vertical del relleno
    std::uint32_t bottom = 0xFFFFFFFFu;
    std::uint32_t outline = 0xFF000000u;
    float outlineWidth = 0.0f;                   // px
    bool shadow = false;
    std::uint32_t shadowColor = 0x8C000000u;
    float shadowX = 6.0f, shadowY = 8.0f;
    float tiltDeg = 0.0f;                        // inclinacion, como una cursiva
    float spacing = 0.0f;                        // px entre letras
    bool pixel = false;                          // sin suavizado
};

// El texto como imagen, recortada a lo que se ve. Vacia si la fuente no se
// puede leer o no tiene ninguno de los caracteres.
Image renderText(const std::vector<unsigned char>& font, const std::string& utf8, const TextLook& look);
// Familia de la fuente ("Segoe UI Black"), de su tabla name; vacia si no la trae.
std::string fontFamilyName(const std::vector<unsigned char>& font);
// La fuente tiene ese caracter.
bool fontHasCodepoint(const std::vector<unsigned char>& font, unsigned codepoint);

// ------------------------------------------------------------------- marcas --

enum class BuiltinMark { Skull, Bolt, Heart, Star, Exclamation, Dot, Eye };
constexpr int kBuiltinMarkCount = 7;
const char* builtinMarkKey(BuiltinMark mark);                 // "skull", "bolt"...
unsigned builtinMarkCodepoint(BuiltinMark mark);              // U+2620...
// La marca de serie blanca con contorno negro, de `symbolFont` (Segoe UI
// Symbol en Windows). Sin fuente o sin el caracter, un circulo.
Image builtinMarkImage(BuiltinMark mark, int size, const std::vector<unsigned char>& symbolFont);

// ------------------------------------------------------- reconocer lo importado --

// La pieza que es un fotograma por su nombre, como lo buscan los motores:
// Codename y Psych (`purple0`, `purple hold piece`, `pruple end hold`,
// `arrowLEFT`, `left press`, `left confirm`; Note.hx:163-183,
// StrumNote.hx:119-121), las salpicaduras de los dos (`note impact 1 purple`,
// `note splash purple 1`), V-Slice (`noteLeft`, `staticLeft`, `pressLeft`,
// `confirmLeft`, `holdCoverStartPurple`...) y las letras de Extra Keys
// (`A0`, `A hold`, `A press`...).
struct PieceGuess {
    bool found = false;
    Part part = Part::Note;
    int direction = 0;
    int variant = 0;
};
PieceGuess guessPiece(const std::string& frameName);

// La imagen del HUD que es un archivo por su nombre, sin carpeta ni extension:
// 0-3 juicios (sick, good, bad, shit), 4 el rotulo combo, 5-14 cifras, 15-18
// cuenta atras (three, two/ready, one/set, go). -1 si no es ninguna. Admite
// los sufijos pixel (`sick-pixel`) y los sonidos de la cuenta atras
// (`intro3`, `introGo`), que dan su paso.
int guessHudIndex(const std::string& fileStem);

// ------------------------------------------------------------------ recetas --

// Un HUD de flechas nuevo a partir de un estilo (DESIGN_PLUGIN_NOTE_LAB §31.2):
// el color de cada direccion y como quedan receptores, aciertos, sostenidos y
// salpicaduras. Se pinta con paintStyleSheets y arrowPaint.
enum class StrumLook { Gray, Colored, Clear };     // receptor en reposo
enum class ConfirmLook { Colored, White };         // receptor al acertar
enum class HoldLook { Colored, Lighter };          // tramo y final del sostenido
// Las piezas que se pueden dibujar con el editor de sprites (pedido del autor,
// 4 oct 2026: «crear todo lo relacionado a la nota, splash, hold»).
// El receptor pulsado y al acertar, si no se dibujan, salen del de reposo.
enum class DrawnPiece { Note, Strum, HoldPiece, HoldEnd, Splash, StrumPress, StrumConfirm };
constexpr int kDrawnPieceCount = 7;
struct ArrowRecipe {
    std::array<std::uint32_t, 4> colors{0xFFC24B99u, 0xFF00FFFFu, 0xFF12FA05u, 0xFFF9393Fu};
    // Contorno propio (ARGB); 0 = el del punto de partida. Con contorno, cada
    // pieza pasa a tres colores por la luz: contorno, su color y blanco.
    std::uint32_t outline = 0;
    StrumLook strums = StrumLook::Gray;
    ConfirmLook confirm = ConfirmLook::Colored;
    HoldLook holds = HoldLook::Colored;
    bool splashes = true;
    float strength = 1.0f;
    // Las piezas dibujadas con el editor de sprites, por DrawnPiece (vacia =
    // la del punto de partida): ruta virtual de su PNG sin tenir. Se tinen con
    // el color de cada direccion; sin receptor dibujado, los receptores pueden
    // tener la forma de la nota (`drawnStrums`); nota y receptor de ← se giran
    // para las otras tres direcciones o se quedan iguales (`drawnRotate`).
    // Un PNG por pieza con sus fotogramas uno al lado de otro (cuadrados: el
    // numero de fotogramas es ancho / alto), y sus fotogramas por segundo.
    std::array<std::string, kDrawnPieceCount> drawn{};
    std::array<int, kDrawnPieceCount> drawnFps{12, 24, 12, 12, 24, 24, 24};
    bool drawnTint = true, drawnStrums = true, drawnRotate = true;
};
Paint arrowPaint(const ArrowRecipe& recipe, Part part, int direction);
// Colores de un preset: classic, pastel, neon, mono, inverted, fireice, random
// (con `seed`). Vacio si la clave no existe.
bool arrowPreset(const std::string& key, std::array<std::uint32_t, 4>& colors, std::uint32_t seed = 0);
const std::vector<std::string>& arrowPresetKeys();

// El aspecto de una nota custom (§31.1): color, marca encima, opacidad, y que
// pasa con su sostenido y su salpicadura. Se pinta sobre su punto de partida.
enum class TypeColor { Keep, One, PerDirection, Palette };
enum class TypeHold { LikeNote, Keep, Hidden };
enum class TypeSplash { Recolored, Keep, None };
struct TypeLookRecipe {
    TypeColor color = TypeColor::One;
    std::uint32_t one = 0xFF3FD14Au;
    std::array<std::uint32_t, 4> perDirection{0xFFC24B99u, 0xFF00FFFFu, 0xFF12FA05u, 0xFFF9393Fu};
    std::array<std::uint32_t, 3> palette{0xFF101010u, 0xFFFF0000u, 0xFFFFFFFFu};   // oscuro, medio, claro
    float strength = 0.85f;
    int mark = -1;                     // BuiltinMark; -1 sin marca de serie
    std::string markImage;             // o una imagen propia (ruta virtual)
    MarkPlace markPlace = MarkPlace::Center;
    float markSize = 0.46f;
    float markAlpha = 1.0f;
    float alpha = 1.0f;
    TypeHold hold = TypeHold::LikeNote;
    TypeSplash splash = TypeSplash::Recolored;
};
Paint typePaint(const TypeLookRecipe& recipe, Part part, int direction, const Image* mark);
// Presets: poison, hurt, ice, fire, gold, ghost, rainbow.
bool typeLookPreset(const std::string& key, TypeLookRecipe& recipe);
const std::vector<std::string>& typeLookPresetKeys();

// El HUD de ranking hecho con texto (§31.3): los cuatro juicios, el rotulo y
// las cifras con la misma letra.
struct RatingRecipe {
    std::array<std::string, 5> texts{};
    // Degradado (arriba, abajo) de cada juicio y del rotulo con las cifras.
    std::array<std::array<std::uint32_t, 2>, 5> colors{{{0xFF78F0FFu, 0xFF2878FFu}, {0xFF8CFF8Cu, 0xFF28AA46u},
                                                         {0xFFFFC86Eu, 0xFFF07828u}, {0xFFC8A078u, 0xFF785032u},
                                                         {0xFFFFFFFFu, 0xFFC8C8E6u}}};
    float size = 120.0f;
    float digitSize = 110.0f;
    float tiltDeg = -6.0f;
    float spacing = 0.0f;
    std::uint32_t outline = 0xFF141432u;
    float outlineWidth = 13.0f;
    bool shadow = true;
    bool pixel = false;
};
// Las 15 imagenes en el orden del HUD: 0-3 juicios, 4 rotulo, 5-14 cifras.
std::vector<Image> renderRatingSet(const RatingRecipe& recipe, const std::vector<unsigned char>& font);

// ------------------------------------------------------------------ importar --

// Fotogramas sueltos a un atlas Sparrow: se recortan los bordes transparentes
// (con frameX/frameY para que no se muevan), los repetidos comparten region y
// se empaquetan en estantes. `imageName` es el PNG que nombra el XML.
struct NamedFrame {
    std::string name;   // "purple0000", como lo buscara el motor
    Image image;
};
struct SparrowOut {
    bool ok = false;
    std::vector<unsigned char> png;
    std::string xml;
    int frames = 0;
};
SparrowOut packSparrow(const std::vector<NamedFrame>& frames, const std::string& imageName, int padding = 2);

// Un GIF animado: sus fotogramas ya compuestos y lo que dura cada uno.
struct GifFrames {
    std::vector<Image> frames;
    std::vector<int> delaysMs;
};
bool decodeGif(const std::vector<unsigned char>& bytes, GifFrames& out);

// Lo que se suelta o se elige para importar, mirado sin cambiar nada: que es
// cada cosa y que pieza o imagen del HUD parece por su nombre.
enum class ImportKind {
    Atlas,     // PNG con su XML (Sparrow) o TXT (Packer) al lado
    Image,     // PNG suelto
    Frames,    // fotogramas sueltos de una carpeta: `nombre0000.png`...
    Gif,       // GIF animado
    Sound,     // OGG, WAV o MP3
    Font,      // TTF u OTF, para el ranking
    Unknown,
};
struct ImportAnimation {
    std::string name;      // prefijo en un atlas, nombre base en una carpeta
    int frames = 0;
    PieceGuess piece;      // la pieza que parece
    int hudIndex = -1;     // o la imagen del HUD que parece
};
struct ImportItem {
    ImportKind kind = ImportKind::Unknown;
    std::filesystem::path path;      // el PNG, la carpeta, el GIF, el sonido o la fuente
    std::filesystem::path atlas;     // Atlas: el XML o el TXT
    std::vector<std::filesystem::path> files;   // Frames: los PNG en orden
    std::string label;
    int width = 0, height = 0;
    std::vector<ImportAnimation> animations;    // Atlas, Frames y Gif
    PieceGuess piece;                // Image: la pieza que parece
    int hudIndex = -1;               // Image y Sound: la imagen o el sonido del HUD
    std::string family;              // Font
};
// Una carpeta se abre: sus PNG con nombres del HUD salen como imagenes sueltas
// y los que llevan numero de fotograma, como animaciones.
std::vector<ImportItem> scanImport(const std::vector<std::filesystem::path>& paths);
const char* importKindKey(ImportKind kind);

// Los fotogramas de una animacion de lo importado, en orden: los de un prefijo
// en un atlas (recortados con su caja), los PNG de una carpeta o los de un
// GIF. `animation` indexa ImportItem::animations; una imagen suelta es uno.
std::vector<Image> importFrames(const ImportItem& item, int animation);

// El aspecto de una nota custom hecho con imagenes propias: por cada
// direccion, los fotogramas de la nota, del tramo y del final del sostenido
// (vacios = sin esa pieza). Sale un atlas Sparrow con los nombres que buscan
// Codename y Psych (`purple0`, `purple hold piece`, `pruple end hold`;
// Note.hx:163-183 de Codename, :436-441 de Psych) y las piezas ya atadas.
struct TypeLookFrames {
    std::array<std::vector<Image>, 4> note, holdPiece, holdEnd;
};
struct TypeLookSheet {
    bool ok = false;
    SparrowOut atlas;
    std::vector<PartBinding> parts;   // hoja 0
};
TypeLookSheet buildTypeLookSheet(const TypeLookFrames& frames, const std::string& imageName);
// Una imagen de la flecha izquierda girada a cada direccion (90 grados por
// paso, como el juego base dibuja ↓ ↑ → con la de ←).
Image rotateForDirection(const Image& left, int direction);

// Un HUD de flechas con piezas dibujadas. Cada pieza son uno o mas fotogramas
// cuadrados (drawnPieceSize: 160 nota y receptor, como una nota del juego
// base; 64 el sostenido; 200 la salpicadura) y de ellos salen las cuatro
// direcciones, tenidas con los colores de la receta: cada pixel pasa a un
// degradado de su color por su luz, asi que sale el color elegido sea cual sea
// el del dibujo. La nota y el receptor de ← se giran (drawnRotate); el
// receptor en reposo y al pulsar usan su primer fotograma y al acertar todos
// (con brillo alrededor); una salpicadura de un solo fotograma se anima sola
// (crece y se desvanece en kDrawnSplashFrames).
using DrawnPieces = std::array<std::vector<Image>, kDrawnPieceCount>;
constexpr int kDrawnArrowCell = 192;
constexpr int kDrawnSplashFrames = 5;
int drawnPieceSize(DrawnPiece piece);
// Para verlo en vivo: rejillas con una columna por direccion y una fila por
// fotograma (vacias si no hay nada que poner).
//  - arrows: notas, receptor en reposo, al pulsar y fotogramas del acierto.
//  - holds: fotogramas del tramo y del final.
//  - splashes: fotogramas de la salpicadura.
struct DrawnSheets {
    Image arrows, holds, splashes;
};
DrawnSheets drawnPieceSheets(const DrawnPieces& pieces, const ArrowRecipe& recipe);
// Las rejillas en el estilo (rutas virtuales: arrows, holds, splashes): las
// piezas dibujadas pasan a sus celdas y las demas se quedan como en el estilo.
void bindDrawnPieces(NoteStyle& style, const DrawnPieces& pieces, const ArrowRecipe& recipe,
                     const std::array<std::string, 3>& images, float scale);
// Lo mismo en un solo atlas Sparrow, el archivo con todas las piezas
// dibujadas y los nombres del juego base (`purple0`, `arrowLEFT`, `left press`,
// `left confirm`, `purple hold piece`, `pruple end hold`, `note splash purple
// 1`), con sus piezas por prefijo (la hoja la pone bindDrawnAtlas).
struct DrawnAtlas {
    SparrowOut atlas;
    std::vector<PartBinding> parts;
};
DrawnAtlas drawnPieceAtlas(const DrawnPieces& pieces, const ArrowRecipe& recipe, const std::string& imageName);
// El atlas montado (rutas virtuales del PNG y del XML) como una hoja mas del
// estilo: sus piezas sustituyen a las del estilo; las demas se quedan.
void bindDrawnAtlas(NoteStyle& style, const DrawnAtlas& drawn, const std::string& image, const std::string& atlas, float scale);
// El nombre del juego base de una pieza (sin numero de fotograma).
std::string basePieceName(Part part, int direction, int variant = 0);
// Todo un estilo en un solo atlas Sparrow, el archivo con todos sus assets de
// notas: cada pieza con sus fotogramas y su nombre del juego base, a la escala
// de las notas del juego base y con la paleta RGB de Psych horneada. `name` es
// el nombre del archivo sin extension; `pieces`, cuantas piezas lleva.
SparrowOut styleSheetAtlas(const NoteStyle& style, const ExportIo& io, const std::string& name, int* pieces = nullptr);
// El archivo del editor de sprites (.nlsprite; pedido del autor, 5 oct 2026:
// «archivos especiales para el paint para conservar capas, frames, etc.»):
// un ZIP con sprite.json (lo que guarde el editor y cada pestana con sus
// capas: nombre, si se ve y su opacidad) y un PNG por capa. Los `meta` son
// JSON del editor; el nucleo los guarda tal cual.
struct SpriteFileLayer {
    std::string name;
    Image image;
    bool visible = true;
    float opacity = 1.0f;
};
struct SpriteFileDoc {
    std::string meta;
    std::vector<SpriteFileLayer> layers;
};
struct SpriteFile {
    std::string meta;
    std::vector<SpriteFileDoc> docs;
};
std::vector<unsigned char> writeSpriteFile(const SpriteFile& file);
// Falso si no es un .nlsprite o esta roto (hasta 32 pestanas de 32 capas).
bool readSpriteFile(const std::vector<unsigned char>& bytes, SpriteFile& out);

// Los fotogramas de una pieza de un estilo como imagenes (con su caja entera),
// como se ven: paleta de Psych horneada y a la escala de las notas del juego
// base. Para cargar un estilo en el editor de sprites.
std::vector<Image> stylePieceFrames(const NoteStyle& style, Part part, int direction, const ExportIo& io, int variant = 0);

enum class CustomRole { Piece, HudImage, CountdownSound, SoundEffect };
struct CustomRect { int x = 0, y = 0, w = 0, h = 0; };
struct CustomResource {
    ImportItem input;
    int cellWidth = 0, cellHeight = 0;
    std::vector<CustomRect> regions;
};
struct CustomAssignment {
    CustomRole role = CustomRole::Piece;
    int resource = -1, animation = 0;
    std::vector<int> order;
    Part part = Part::Note;
    int direction = 0, variant = 0, hudIndex = 0;
    std::string soundName;
    float fps = 24.0f, scale = 0.7f, offsetX = 0.0f, offsetY = 0.0f;
    bool loop = false, pixel = false;
};
struct CustomRecipe {
    std::vector<CustomResource> resources;
    std::vector<CustomAssignment> assignments;
    bool noteType = false;
    std::string type;
};
struct CustomInspection {
    Image sheet;
    std::vector<Image> frames;
    std::vector<CustomRect> boxes;
    std::string error;
};
struct CustomFile { std::string path; std::vector<unsigned char> bytes; };
struct CustomBuild {
    NoteStyle style;
    std::vector<CustomFile> files;
    std::vector<std::string> errors;
    bool ok = false;
};
constexpr int kCustomFrameLimit = 512;
bool bindCustomAtlas(CustomResource& resource, const std::filesystem::path& atlas, std::string& error);
CustomInspection inspectCustomResource(const CustomResource& resource, int animation);
CustomBuild buildCustomStyle(const CustomRecipe& recipe, Engine engine, const std::string& name);
bool parseCustomOrder(const std::string& text, int frameCount, std::vector<int>& order, std::string& error);

}  // namespace fml::notelab

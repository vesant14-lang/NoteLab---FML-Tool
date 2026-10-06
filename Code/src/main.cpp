// Note Lab — standalone (DESIGN_PLUGIN_NOTE_LAB.md §9-§26).
//
// Fase 2 de la v1: se abren uno o varios mods (carpeta o ZIP) y, si hace falta,
// el juego base; el motor de cada mod se detecta solo (como Atlas, con opcion de
// fijarlo a mano); se ven sus estilos de notas con la validacion del nucleo y
// sus notas custom con su script, se prueban en una vista previa jugable con un
// patron o con una cancion del mod y su musica (cada nota con el aspecto y las
// reglas de su tipo), se inspeccionan sus hojas en el visor de assets y se
// editan sus piezas con deshacer. La logica vive en src/fml_notelab; aqui solo
// hay interfaz, con el tema de Theme.hpp.
#include "../core/NoteStyleCheck.hpp"
#include "../core/NotePreview.hpp"
#include "../core/NoteSongs.hpp"
#include "../core/NoteTypes.hpp"
#include "../core/NoteProject.hpp"
#include "../core/NoteExport.hpp"
#include "../core/NoteInstall.hpp"
#include "../core/NoteCreate.hpp"
#include "../core/NoteCode.hpp"
#include "../core/NoteResources.hpp"
#include "../support/core/Hash.hpp"
#include "../support/io/Vfs.hpp"
#include "../support/audio/AudioEngine.hpp"
#include "../support/formats/SparrowAtlas.hpp"
#include "../support/render/GlRenderer.hpp"
#include "../support/core/TextEncoding.hpp"
#include "../third_party/json.hpp"
#include "../third_party/stb_image.h"
#include "../third_party/stb_image_write.h"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_internal.h"
#include "../third_party/imgui/imgui_impl_sdl3.h"
#include "../third_party/imgui/imgui_impl_opengl3.h"
#include "Theme.hpp"
#include "BlockCanvas.hpp"
#include "CodeEditor.hpp"
#include "BuildPolicy.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include "Branding.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace fml;
using namespace fml::notelab;

#include "SpriteShapes.hpp"
namespace ui = nlui;

namespace {

constexpr int kMaxSources = 4;
constexpr size_t kMaxUndo = 200;
constexpr bool kTextRankingVisible = false;
constexpr const char* kPublicVersion = nlbuild::version;

enum class DialogAction { None, AddModFolder, AddModZip, SetBaseFolder, ImportSheet, OpenProject, SaveProject,
                          ImportHudImage, ImportHudSound, ExportFolder, LookFiles, LookFolder, RatingFont, CustomFiles, CustomFolder, CustomAtlas, SaveChartCopy, ModMediaFiles,
                          SpriteSheetFile, SaveStyleSheet, SaveFinalSheet, SaveProgram, OpenProgram, SaveCodeFolder,
                          SaveSpriteFile };
// Lo que espera a saber si se guardan los cambios del proyecto.
enum class PendingAction { None, Quit, NewProject, OpenProject, OpenRecent };

// Algo que cambio o falta al abrir un proyecto o recargar un mod.
struct Issue { std::string en, es; };
enum class Judgement { None, Sick, Good, Bad, Shit, Miss };

struct Source {
    fs::path root;
    // Lo que se abrio de verdad (NoteInstall.hpp, openLayoutOf): en una
    // instalacion con mods, su mod (`modName`, o el primero) con los paquetes
    // de Codename encima y la instalacion debajo. `baseOnly`: la instalacion
    // abierta como juego base, sin su mod.
    std::string modName;
    bool baseOnly = false;
    OpenLayout layout;
    size_t packsMounted = 0;                          // paquetes de Codename montados encima
    size_t baseRootIndex = static_cast<size_t>(-1);   // la raiz del juego base en la VFS
    std::unique_ptr<Vfs> vfs;
    Catalog catalog;
    std::vector<StyleReport> reports;
    EngineGuess guess;                    // del mod solo, sin el juego base
    bool withBase = false;                // el juego base esta montado debajo
    fs::path basePath;                    // cual, y como se encontro (DESIGN_PLUGIN_NOTE_LAB §30)
    BaseFound baseHow = BaseFound::None;
    bool baseMissing = false;             // lo necesita y no se encontro ninguno
    std::vector<std::uint8_t> fromBase;   // por estilo: viene del juego base
    std::vector<std::uint8_t> typeFromBase, songFromBase;   // por tipo y por chart: son del juego base
    std::vector<SongChart> songs;
    std::vector<std::uint8_t> edited;     // por estilo: tiene cambios
    std::map<size_t, NoteStyle> originals;
    AtlasCache atlasCache;
    int imports = 0;
    std::vector<NoteTypeEntry> noteTypes; // las notas custom que trae (y las de serie)
    bool expanded = true;                 // su lista de estilos, abierta en el panel
    std::vector<ProjectImport> importedFiles;   // hojas importadas, para volver a montarlas
    // El programa de bloques de cada tipo (DESIGN_PLUGIN_NOTE_LAB §17, §29),
    // por su nombre en el chart; un tipo que solo esta aqui es uno creado en
    // Note Lab.
    std::map<std::string, BlockProgram> typeBlocks;
    // El aspecto que se le dio en Note Lab a cada tipo: tipo -> estilo creado.
    std::map<std::string, std::string> typeLooks;
    std::map<std::string, CreationRecipe> recipes;
    std::map<std::string, BotProfile> bots;
    // Los sonidos de sounds/ para «tocar sonido», leidos una vez por montaje.
    std::vector<std::string> sounds;
    const Vfs* soundsFrom = nullptr;
    std::vector<ModResource> resourceCatalog;
    const Vfs* resourcesFrom = nullptr;
};

// Como se llama un mod abierto: el mod que se abrio de una instalacion, o la
// carpeta o el ZIP elegidos.
std::string sourceName(const Source& source) {
    if (!source.baseOnly && !source.layout.install.empty() && !source.layout.mod.empty())
        return source.layout.mod.filename().u8string();
    return source.root.filename().u8string();
}

struct UndoEntry {
    int source = -1;
    int style = -1;
    NoteStyle before;
    std::map<std::string, CreationRecipe> recipes;
};

struct NoteLabApp {
    struct MediaBrowser {
        bool requestOpen = false, includeBase = false, isOpen = false, startWhenReady = false;
        int source = -1, block = -1, argument = -1, category = 0;
        int soundFilter = 1;             // 0 todos, 1 efectos, 2 canciones, 3 musica
        std::string type, selected, message, imagePath;
        std::vector<ModResource> resources;
        std::set<std::string> usedPaths;
        std::array<char, 160> search{};
        std::unique_ptr<AudioEngine> audio;
        float volume = 0.5f, zoom = 1.0f;
        float audioSpeed = 1.0f, audioBalance = 0.0f;
        float audioStart = 0.0f, audioEnd = 0.0f;
        bool audioLoop = false, audioRange = false;
        ImVec2 pan{0.0f, 0.0f};
        ImVec4 closeRect{}, assignRect{};
        ImVec4 listenRect{}, pauseRect{}, stopRect{}, loopRect{}, rangeRect{}, speedRect{}, balanceRect{};
        std::map<std::string, ImVec4> fileRects;
        bool importRequestOpen = false, importIsOpen = false, importToMod = false, createFolder = false, applySettings = false;
        std::vector<fs::path> importFiles;
        int importSelected = 0, fitMode = 0;
        std::array<char, 512> importFolder{};
        float seconds = 1.0f, opacity = 1.0f;
    } media;
    struct ResourceSidebar {
        int page = 0, category = -1, soundFilter = 1;
        bool includeBase = false;
        std::array<char, 160> search{};
        std::string selected;
        ImVec4 bounds{}, libraryRect{}, usesRect{}, searchRect{}, screamerRect{};
        std::array<ImVec4, 3> categoryRects{};
        std::map<std::string, ImVec4> itemRects;
    } resourceSidebar;
    int leftPanel = 0;
    ImVec4 sourcesPanelRect{}, resourcesPanelRect{};
    bool spanish = false;
    int engineChoice = 0;                 // 0 Auto, 1 Codename, 2 Psych, 3 V-Slice
    bool showOtherEngines = false;
    bool showUnused = false;
    std::string statusEn, statusEs;

    std::vector<std::unique_ptr<Source>> sources;
    // El juego base de cada motor (DESIGN_PLUGIN_NOTE_LAB §30): el que fija la
    // persona, por motor (Codename, Psych, V-Slice); vacio = buscarlo solo
    // para cada mod que no trae el de su motor.
    std::array<fs::path, 3> baseRoots;
    bool autoBase = true;
    // Instalaciones oficiales ya vistas, la ultima primero: la busqueda las
    // prueba antes de mirar junto al mod. Se guardan en las preferencias.
    std::vector<std::pair<Engine, fs::path>> knownInstalls;
    int selSource = -1;
    int selStyle = -1;

    int renderSource = -1;
    GlRenderer renderer;
    bool rendererReady = false;
    AtlasStore atlases;
    NotePreview preview;
    std::set<std::string> rgbReady, rgbFailed;

    // Reproduccion: el patron de prueba o una cancion del mod con su musica.
    PreviewSettings settings;
    PreviewState state;
    std::vector<PreviewNote> notes;
    std::vector<std::string> noteTypes;   // por nota, en una cancion
    std::vector<std::uint8_t> missed;
    double patternMs = 1.0;
    float bpm = 120.0f;
    bool playing = true;
    int songSource = -1, songIndex = -1;  // -1 = patron de prueba
    std::string songLabel;
    // La ultima cancion cargada: volver a ella tras pasar al patron de prueba.
    const Source* lastSongOwner = nullptr;
    int lastSongIndex = -1;
    std::string lastSongLabel;
    AudioEngine audio;
    bool audioReady = false, audioPending = false, audioLoaded = false;
    float musicVolume = 0.8f;

    int visibleLines = 2;                 // 0 rival, 1 jugador, 2 los dos
    int playSide = 1;
    bool manual = false;

    // Crear sin dibujar (DESIGN_PLUGIN_NOTE_LAB §31): lo pintado se ve en vivo
    // con texturas del renderer y no se escribe nada hasta confirmar.
    struct LiveSheets {
        int generation = 0, painted = -1, running = -1;
        double changedAt = 0.0;
        std::future<std::vector<PaintedImage>> job;
        NoteStyle style;
        std::vector<PaintedImage> images;
        std::vector<std::string> uploaded;
    };
    struct CreateHud {
        bool requestOpen = false;
        int source = -1, base = -1;
        int editStyle = -1;
        std::array<char, 64> name{};
        ArrowRecipe recipe;
        std::string preset = "pastel";
        std::uint32_t seed = 1;
        LiveSheets live;
        // La forma de las notas dibujada con el editor de sprites (pedido del
        // autor, 4 oct 2026): la flecha de la izquierda sin tenir; como se tine,
        // gira y hace receptores va en `recipe` (drawnTint, drawnStrums...).
        bool useDrawn = false;
        DrawnPieces drawn;                // los fotogramas de cada pieza dibujada, sin tenir
        std::string layersKey;            // la hoja de esta sesion en spriteEditor.drawn
        int drawnFor = -1, drawnVersion = 0, drawnVersionFor = -1;
        std::array<std::string, 3> drawnLive;   // las rejillas en vivo (notas, sostenido, salpicadura)
        std::string spriteFile;           // el .nlsprite con las capas y pestanas de lo dibujado
        // Por pasos (pedido del autor, 5 oct 2026): 0 base, 1 colores, 2 detalles, 3 piezas.
        int step = 0;
        int thumbsFor = -1;               // version de lo dibujado subida como miniaturas
    } createHud;
    struct TypeLook {
        bool requestOpen = false, isOpen = false;
        int source = -1, mode = 0, base = -1;   // mode 0 pintar un skin, 1 imagenes propias
        int editStyle = -1;
        std::string type, preset, message;
        TypeLookRecipe recipe;
        LiveSheets live;
        std::shared_ptr<Image> markImage;
        int markFor = -2;
        std::vector<ImportItem> items;
        std::array<std::array<int, 3>, 4> pick{{{-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}}};
        std::array<int, 4> previewKey{-1, -1, -1, -1};
        bool sameForAll = true, rotate = true;
    } typeLook;
    struct CreateRating {
        bool requestOpen = false;
        int source = -1, style = -1;
        RatingRecipe recipe;
        std::array<std::array<char, 32>, 5> texts{};
        std::vector<std::pair<std::string, std::string>> fonts;   // familia, archivo
        std::future<std::vector<std::pair<std::string, std::string>>> fontJob;
        std::string fontPath, fontLoaded;
        std::shared_ptr<std::vector<unsigned char>> fontBytes;
        int generation = 0, painted = -1, running = -1;
        double changedAt = 0.0;
        std::future<std::vector<Image>> job;
        std::vector<Image> images;
        std::vector<std::string> live;
    } createRating;
    struct CustomCreator {
        bool requestOpen = false, isOpen = false, ranking = false, trace = false, playing = true, inspectDirty = true, fit = true;
        bool createdWorkspace = false;
        int source = -1, editStyle = -1, resource = -1, animation = 0, frame = 0, uploadedFrame = -1, assignment = -1;
        Engine engine = Engine::Codename;
        std::array<char, 96> name{}, type{}, soundName{};
        std::array<char, 4096> order{};
        CustomRecipe recipe;
        CustomAssignment draft;
        CustomInspection inspected;
        CustomBuild review;
        std::string message;
        float zoom = 1.0f, volume = 0.7f;
        ImVec2 pan{}, traceStart{};
        bool tracing = false, audioPending = false;
        std::unique_ptr<AudioEngine> audio;
        double clockStart = 0.0;
    } custom;
    std::vector<unsigned char> symbolFont;   // Segoe UI Symbol, para las marcas
    bool markIcons = false;                  // las marcas subidas como texturas para sus botones
    int lookSerial = 0;
    bool autoCommit = false;                 // --commit-create: confirmar en cuanto este listo
    // La ayuda (F1): el tema y lo que se busca.
    bool openHelp = false;
    int helpTopic = 0;
    std::array<char, 64> helpSearch{};
    // El tutorial (Tutorial.hpp): las misiones hechas por su clave, si ya se
    // pregunto en el primer arranque, si su ventana esta abierta, el modo foco,
    // la guia de bloques saltada u oculta y lo contestado en cada zona
    // (1 haciendolo, 2 saltado: no se vuelve a preguntar).
    std::set<std::string> tutorialDone;
    bool tutorialSeen = false;
    bool tutorialOpen = false;
    bool tutorialFocus = true;
    bool tutorialBlocksHidden = false;
    std::map<std::string, int> tutorialAreas;
    bool tutorialSounds = true;            // los avisos del tutorial suenan (los del editor no)
    std::vector<std::uint32_t> spriteColors;  // la paleta propia del editor de sprites (ImU32), en las preferencias
    // Lo soltado que no es un mod: Note Lab pregunta para que es.
    struct DropImport {
        std::vector<fs::path> pending, files;
        std::vector<ImportItem> items;
        int hudImages = 0, fonts = 0;
        std::array<char, 96> type{};
        bool requestOpen = false;
    } dropImport;
    bool psychColors = true;
    std::array<ImGuiKey, 4> keys{ImGuiKey_D, ImGuiKey_F, ImGuiKey_J, ImGuiKey_K};
    bool arrowsToo = true;
    int bindingLane = -1;
    Judgement lastJudgement = Judgement::None;
    std::array<int, 6> counts{};
    int maxCombo = 0;
    // La pasada que puntua (jugar o no, y el lado): si cambia, el marcador
    // vuelve a cero. `finished`: jugando, la cancion acabo y se para al final
    // con el resultado a la vista hasta reproducir otra vez.
    int scoreRun = -1;
    bool finished = false;
    ImVec4 resetScoreRect{};              // para --ui-test=score

    int requestedTab = -1;
    int assetKind = 0;                    // 0 hoja, 1 imagen de HUD
    int assetScope = 0;                   // 0 este estilo, 1 todo el mod
    std::string assetAnim;                // la animacion resaltada en la hoja
    int assetIndex = 0;
    int assetPart = -1;
    float assetZoom = 1.0f;
    ImVec2 assetPan{0.0f, 0.0f};
    bool assetFit = true;
    bool assetFrames = true;
    double assetClockMs = 0.0;
    bool assetDragged = false;

    // Edicion.
    int editPart = -1;
    int editHud = -1;                      // 0-3 juicios, 4 combo, 5-14 cifras, 15-18 cuenta atras
    // Varias imagenes del HUD a la vez (Ctrl y Mayus + clic): vale mientras
    // contenga a `editHud`, que es la ultima elegida.
    std::set<int> hudSelection;
    int hudAnchor = -1;
    int importHud = -1;
    double editClockMs = 0.0;
    std::array<char, 128> nameBuffer{};
    std::string nameFor;
    std::array<char, 256> prefixBuffer{};
    std::array<char, 256> indicesBuffer{};
    std::string buffersFor;
    std::vector<UndoEntry> undo, redo;
    int importSource = -1, importStyle = -1, importSheet = -1;

    bool showFindings = false;
    int findingsSeverity = 2;

    std::mutex dialogMutex;
    std::string dialogPath;
    std::vector<std::string> dialogPaths;   // con seleccion multiple
    bool dialogReady = false;
    DialogAction dialogAction = DialogAction::None;

    // Notas custom: el tipo elegido (de cualquier mod abierto: `typeSource` y
    // su indice alli), su script y cuantas usa el chart cargado.
    int typeSource = -1;
    int selType = -1;
    std::string codeKey, codePath;
    std::vector<std::string> codeLines;
    struct TypeUse { int count = 0; double firstMs = 0.0; };
    std::map<std::string, TypeUse> typeUses;   // tipo en minusculas -> uso en el chart
    std::map<std::string, NoteStyle> derivedLooks;
    // «Distribuir» sobre la copia del chart cargado (el chart del mod no se toca).
    DistributeRequest distribute;
    bool distributed = false;
    DistributeResult distributionPreview;
    std::string distributionPreviewKey;
    std::string chartOriginal, chartExpected;
    struct ChartSave {
        bool requestOpen = false, canReplace = false;
        std::string text, error;
        fs::path original, backup;
        int source = -1, song = -1;
    } chartSave;
    struct CustomBot {
        bool requestOpen = false;
        int source = -1;
        std::string type;
        BotProfile profile;
    } customBot;
    struct StyleComposer {
        bool requestOpen = false;
        int targetSource = -1, targetStyle = -1, donorSource = -1, donorStyle = -1;
        StyleComponents components;
        std::string message;
    } composer;
    bool compactLayout = false;
    int sourceToClose = -1;
    bool openSourceClose = false;
    std::vector<std::string> chartTypes;   // los tipos del chart tal como vino
    int typesView = 0;                     // 0 catalogo, 1 bloques, 2 distribuir
    // Bloques (§25.6): el tipo que se edita (y de que mod), el motor del
    // codigo que se ve y el nombre de un tipo nuevo a medio escribir.
    int blocksSource = -1;
    std::string blocksType;
    int blocksEngine = -1;                 // -1 = el del mod
    int blocksView = 0;                    // 0 bloques, 1 codigo, 2 los dos
    float blocksCodeRatio = 0.38f;
    ImVec4 blocksDividerRect{};
    ImVec4 blocksToolbarViewsRect{};
    int blocksFile = 0;
    std::string blockCodeOwner, blockCodeShown, blockCodeError;
    std::vector<char> blockCodeBuffer;
    nlcode::EditorState blockEditor;
    std::string blockCommentOwner;
    std::string blockCommentShown;
    std::array<char, 4096> blockCommentBuffer{};
    int centerTab = 0;                     // la pestana del centro que se ve (0 vista previa, 1 notas custom, 2 assets)
    nlblocks::CanvasState canvas;          // el editor de bloques del tipo que se edita
    bool uiSounds = false;                 // los sonidos del editor de bloques
    bool headless = false;                 // captura, prueba o exportacion por linea de ordenes: sin sonidos
    bool transient = false;
    // El asistente «Nueva nota custom» (NewNote.hpp).
    // Pasar el script de un tipo a bloques: lo que salio, para ensenarlo; y el
    // tipo con el que se guardan o abren programa y codigo.
    struct ScriptImportView {
        bool requestOpen = false;
        int source = -1;
        std::string type, from;
        int blocks = 0, kept = 0;
        std::vector<std::pair<std::string, std::string>> notes;
    } scriptImport;
    int programSource = -1;
    std::string programType;
    struct NewNote {
        bool requestOpen = false;
        int source = -1;
        std::array<char, 64> name{};
        int mode = 0;            // 0 con bloques, 1 solo codigo
        std::string preset;      // con que empieza (bloques); vacio = nada
        int look = 0;            // 0 como las normales, 1 pintarla, 2 mis imagenes, 3 dibujarla
        std::string sound;       // al tocarla (bloques); vacio = ninguno
        int bot = 0;             // 0 como una normal, 1 configurarlo al crearla
        std::string message;
        // Creada: la ventana pasa a «Lista» y ofrece lo siguiente (sus bloques,
        // su aspecto, su bot); nada se abre solo (pedido del autor, 5 oct 2026).
        bool done = false;
        std::string created;
        bool fromBlocks = false;   // abierta desde Bloques: al crearla, a sus bloques
    } newNote;
    // El editor de sprites (SpriteEditor.hpp): la hoja de una nota con sus
    // piezas y fotogramas, capas, pinceles, figuras, seleccion y pestanas.
    struct SpriteEditor {
        bool requestOpen = false;
        int target = 0;                  // 0 el aspecto de un tipo, 1 las piezas de Crear HUD
        int source = -1;
        std::string type;
        // Las pestanas: la hoja general (la primera), dibujos sueltos y otras
        // hojas para copiar. La de delante vive en los campos de abajo.
        std::vector<SpriteDoc> docs;
        int doc = 0, selectTab = -1;
        std::string docName;
        bool docSingle = false, docHud = false, docReference = false;
        DrawnPiece docPiece = DrawnPiece::Note;
        int fromRow = -1, fromSlot = -1;
        // Un lienzo libre (pedido del autor, 5 oct 2026): del tamano que se
        // quiera, y luego «Pasar a la hoja general» lo mete en su hueco.
        int docFreeW = 0, docFreeH = 0;
        bool docPixel = false;
        std::vector<SpriteLayer> layers;
        int layer = 0;
        std::vector<SpriteUndo> undo, redo;
        SpriteRect selection;
        float zoom = 0.0f, zoomTo = 0.0f, lastScale = 1.0f;   // zoom <= 0: toda la hoja
        ImVec2 pan{};
        bool focusPending = false;
        int row = 0, slot = 0;           // el hueco elegido (la linea de tiempo)
        // Las herramientas y los colores (los mismos en todas las pestanas).
        int tool = 0;                    // 0 pincel, 1 goma, 2 figura, 3 relleno, 4 cuentagotas, 5 seleccion, 6 mover
        ImU32 color = IM_COL32(194, 75, 153, 255), second = IM_COL32(28, 12, 36, 255);
        float brushSize = 10.0f, hardness = 0.7f, brushOpacity = 1.0f;
        SpriteShapeKind shapeKind = SpriteShapeKind::Arrow;
        bool shapeFill = true;
        float shapeOutline = 8.0f;
        int tolerance = 24;
        bool rotate = true, stroking = false, panning = false, hasLast = false;
        ImVec2 start{}, last{}, lastPoint{};
        std::array<int, kDrawnPieceCount> fps{12, 24, 12, 12, 24};
        bool playing = true, onion = false, listShown = false;
        Image clipboard;
        std::map<std::string, std::vector<SpriteLayer>> drawn;   // la hoja de cada tipo o HUD, en esta sesion
        // Lo que se ve: compuesto, que huecos tienen algo y que rango falta.
        Image composite;
        std::vector<char> used;
        SpriteRect dirty;
        int dirtyFor = -1, composed = -1;
        // El PNG final: el archivo con todo junto (todo el HUD o solo lo dibujado).
        bool showFinal = false;
        int finalMode = 0, finalFor = -1;
        SparrowOut finalSheet;
        std::vector<AtlasFrame> finalFrames;
        int finalW = 0, finalH = 0;
        float finalZoom = 0.0f;
        ImVec2 finalPan{};
        int generation = 0, shown = -1, painted = -1;
        double changedAt = 0.0;
        std::string message;
        // Antes de pintar, como empezar: seguir, la plantilla con huecos, un
        // lienzo libre o el lienzo de una pieza (pedido del autor, 5 oct 2026).
        bool choosing = false, hasWork = false;
        int startKind = 1;               // 0 seguir, 1 plantilla, 2 lienzo libre, 3 lienzo de una pieza
        int startTemplate = 0;           // 0 flecha, 1 mina, 2 corazon, 3 vacia, 4 la del estilo
        int freeW = 32, freeH = 32;
        bool freePixel = true;
        DrawnPiece startPiece = DrawnPiece::Note;
        // Espejo al pintar: 0 no, 1 arriba-abajo, 2 izquierda-derecha.
        int mirror = 0;
        bool showHelp = false, timelineOpen = false;
        bool strokeAlt = false;          // el trazo de ahora va con el color de contorno (clic derecho)
    } spriteEditor;

    // Lo que fija el chart cargado (DESIGN_PLUGIN_NOTE_LAB §27).
    SongValues songValues;
    bool hasSongValues = false;
    bool chartSpeed = true;      // seguir la velocidad del chart y sus cambios
    bool chartSkin = true;       // elegir el skin que pide la cancion
    double audioOffsetMs = 0.0;  // cancion = audio + esto
    int songSplash = -1;         // estilo con las salpicaduras que pide (Psych splashSkin)
    std::string previewReceptors; // id del estilo cuyos receptores se ven en la vista previa; vacio = los del estilo
    std::string songSkinMissing; // el skin que pide y no esta entre lo abierto

    // HUD de la vista previa: el juicio y el combo van en `settings.judgement`
    // y `settings.combo`; el marcador (fichas y precision) es de la app. Todo
    // en px de juego, para que siga al lienzo al cambiar la ventana.
    HudLayer score;
    bool hudEdit = false;
    bool openHud = false;
    std::string frameMenu;       // fotograma del menu contextual del visor

    bool openKeys = false, openShortcuts = false, openAbout = false;
    bool quit = false;

    // Proyecto .fmlnote (DESIGN_PLUGIN_NOTE_LAB §21).
    fs::path projectPath;
    bool dirty = false;                   // cambios sin guardar
    std::vector<std::string> recentProjects;
    PendingAction pending = PendingAction::None;
    fs::path pendingPath;
    bool openUnsaved = false, saveThenContinue = false;
    std::vector<Issue> issues;
    bool openIssues = false;
    std::string windowTitle;

    // Exportar (DESIGN_PLUGIN_NOTE_LAB §20, §21, §25.5): el paquete se prepara
    // en seco al cambiar algo del dialogo y solo se escribe al pulsar «Exportar».
    struct Exporting {
        bool open = false;
        int source = -1, style = -1;      // el estilo que se exporta
        ExportOptions options;
        bool zip = false;
        std::array<char, 96> name{};
        std::array<char, 128> noteType{};
        fs::path folder;                  // donde se crea el paquete
        ExportPackage package;            // la vista previa, sin guias
        std::string preparedKey;
        double changedAt = -1.0;          // un nombre a medio escribir espera un poco
        fs::path written;                 // lo ultimo escrito
        bool writtenVerified = false;
        std::string error;
    } exporting;
    fs::path exportFolder;                // la ultima carpeta de exportacion

    std::string capturePath;
    int captureFrames = 45;
};

const char* tr(const NoteLabApp& app, const char* en, const char* es) { return app.spanish ? es : en; }

// Esc cierra una ventana solo si es la de arriba del todo (sin un desplegable,
// un selector de color ni una pregunta encima) y no se estaba escribiendo en un
// campo: ahi Esc cancela la edicion del campo, no la ventana con todo lo hecho.
// El campo se suelta en el mismo fotograma, por eso tambien se mira el anterior.
bool escapeClosesWindow() {
    ImGuiContext& g = *GImGui;
    if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsAnyItemActive() || g.ActiveIdPreviousFrame != 0) return false;
    const ImGuiWindow* window = ImGui::GetCurrentWindow()->RootWindow;
    return !g.OpenPopupStack.empty() && g.OpenPopupStack.back().Window == window;
}

void openExport(NoteLabApp& app);
void openCreateHud(NoteLabApp& app);
void openTypeLook(NoteLabApp& app, int sourceIndex, const std::string& type);
void openCreateRating(NoteLabApp& app);
void openCustomCreator(NoteLabApp& app, bool ranking = false, const std::string& type = {});
void addCustomFiles(NoteLabApp& app, const std::vector<fs::path>& paths);
void prepareMediaImport(NoteLabApp& app, const std::vector<fs::path>& paths);
void clearMediaPreview(NoteLabApp& app);
void openCustomBot(NoteLabApp& app, int source, const std::string& type);
bool prepareChartSave(NoteLabApp& app);
bool commitChartSave(NoteLabApp& app, const fs::path& target, bool replaceOriginal);
void applyTypeLooks(Source& source);
void addLookFiles(NoteLabApp& app, const std::vector<fs::path>& files);
void openHelp(NoteLabApp& app, int topic);
void helpButton(NoteLabApp& app, const char* id, int topic);
void openDialog(NoteLabApp& app, SDL_Window* window, DialogAction action);
void copyText(NoteLabApp& app, const std::string& text);
void drawInspector(NoteLabApp& app, SDL_Window* window);
void openStyleComposer(NoteLabApp& app);
void openSpriteEditor(NoteLabApp& app, int sourceIndex, const std::string& type);
void openHudSpriteEditor(NoteLabApp& app, DrawnPiece piece, bool focus);
void loadSpriteSheetFile(NoteLabApp& app, const fs::path& path);
const char* drawnPieceName(const NoteLabApp& app, DrawnPiece piece);
void saveStyleSheetTo(NoteLabApp& app, const fs::path& path);
void saveFinalSheetTo(NoteLabApp& app, const fs::path& path);
void saveProgramTo(NoteLabApp& app, const fs::path& path);
void saveSpriteFileTo(NoteLabApp& app, const fs::path& path);
void openProgramFrom(NoteLabApp& app, const fs::path& path);
void saveCodeTo(NoteLabApp& app, const fs::path& folder);
void importTypeScript(NoteLabApp& app, int sourceIndex, const std::string& type);
void drawSpriteEditor(NoteLabApp& app, int host);
void releaseSpritePreview(NoteLabApp& app);
void openModResources(NoteLabApp& app, int source, const std::string& type, int block, int argument, ResourceKind kind);

void setStatus(NoteLabApp& app, std::string en, std::string es) {
    app.statusEn = std::move(en);
    app.statusEs = std::move(es);
}

std::string lowerText(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool endsWithText(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

const char* engineLabel(Engine engine) {
    switch (engine) {
        case Engine::Codename: return "Codename";
        case Engine::Psych: return "Psych";
        case Engine::VSlice: return "V-Slice";
    }
    return "?";
}

const char* useLabel(const NoteLabApp& app, StyleUse use) {
    switch (use) {
        case StyleUse::Default: return tr(app, "whole mod", "todo el mod");
        case StyleUse::NoteType: return tr(app, "note type", "tipo de nota");
        case StyleUse::Song: return tr(app, "song", "canción");
        case StyleUse::PlayerChoice: return tr(app, "player option", "opción del jugador");
        case StyleUse::Declared: return tr(app, "declared", "declarado");
    }
    return "?";
}

fs::path settingsFolder() {
    fs::path base = environmentPath("LOCALAPPDATA");
    if (base.empty()) base = fs::temp_directory_path();
    return base / "FunkinNoteLab";
}

json hudLayerJson(const HudLayer& layer) {
    return json{{"visible", layer.visible}, {"scale", layer.scale}, {"alpha", layer.alpha}, {"x", layer.x}, {"y", layer.y}};
}

void readHudLayer(const json& doc, const char* key, HudLayer& layer) {
    if (!doc.contains(key) || !doc[key].is_object()) return;
    const json& value = doc[key];
    if (value.contains("visible") && value["visible"].is_boolean()) layer.visible = value["visible"].get<bool>();
    if (value.contains("scale") && value["scale"].is_number()) layer.scale = std::clamp(value["scale"].get<float>(), 0.25f, 3.0f);
    if (value.contains("alpha") && value["alpha"].is_number()) layer.alpha = std::clamp(value["alpha"].get<float>(), 0.1f, 1.0f);
    if (value.contains("x") && value["x"].is_number()) layer.x = std::clamp(value["x"].get<float>(), -1280.0f, 1280.0f);
    if (value.contains("y") && value["y"].is_number()) layer.y = std::clamp(value["y"].get<float>(), -1280.0f, 1280.0f);
}

void loadSettings(NoteLabApp& app) {
    const fs::path path = settingsFolder() / "settings.json";
    std::error_code ec;
    if (fs::file_size(path, ec) > 1024u * 1024u || ec) return;
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    json doc;
    try {
        size_t nodes = 0;
        doc = json::parse(text, [&](int depth, json::parse_event_t, json&) {
            if (depth > 24 || ++nodes > 20000) throw std::runtime_error("settings limit");
            return true;
        }, false, true);
    } catch (...) { return; }
    if (!doc.is_object()) return;
    if (doc.contains("spanish") && doc["spanish"].is_boolean()) app.spanish = doc["spanish"].get<bool>();
    if (doc.contains("arrowsToo") && doc["arrowsToo"].is_boolean()) app.arrowsToo = doc["arrowsToo"].get<bool>();
    if (doc.contains("psychColors") && doc["psychColors"].is_boolean()) app.psychColors = doc["psychColors"].get<bool>();
    if (nlbuild::audioPlayback && doc.contains("musicVolume") && doc["musicVolume"].is_number()) {
        const double volume = doc["musicVolume"].get<double>();
        if (std::isfinite(volume)) {
            app.musicVolume = static_cast<float>(std::clamp(volume, 0.0, 1.0));
            const bool restoredAudioSettings = doc.contains("audioSettingsVersion") &&
                doc["audioSettingsVersion"].is_number_integer() && doc["audioSettingsVersion"].get<int>() >= 1;
            if (!restoredAudioSettings && app.musicVolume == 0.0f) app.musicVolume = 0.8f;
        }
    }
    if (doc.contains("keys") && doc["keys"].is_array() && doc["keys"].size() == 4)
        for (size_t i = 0; i < 4; ++i)
            if (doc["keys"][i].is_number_integer()) {
                const int key = doc["keys"][i].get<int>();
                if (key >= ImGuiKey_NamedKey_BEGIN && key < ImGuiKey_NamedKey_END) app.keys[i] = static_cast<ImGuiKey>(key);
            }
    if (doc.contains("recentProjects") && doc["recentProjects"].is_array())
        for (const json& item : doc["recentProjects"])
            if (item.is_string() && app.recentProjects.size() < 8) app.recentProjects.push_back(item.get<std::string>());
    if (doc.contains("exportFolder") && doc["exportFolder"].is_string())
        app.exportFolder = pathFromUtf8(doc["exportFolder"].get<std::string>());
    if (doc.contains("blocksView") && doc["blocksView"].is_number_integer())
        app.blocksView = std::clamp(doc["blocksView"].get<int>(), 0, 2);
    if (doc.contains("leftPanel") && doc["leftPanel"].is_number_integer())
        app.leftPanel = std::clamp(doc["leftPanel"].get<int>(), 0, 1);
    if (doc.contains("resourceSidebarPage") && doc["resourceSidebarPage"].is_number_integer())
        app.resourceSidebar.page = std::clamp(doc["resourceSidebarPage"].get<int>(), 0, 1);
    if (doc.contains("blocksCodeRatio") && doc["blocksCodeRatio"].is_number()) {
        const float ratio = doc["blocksCodeRatio"].get<float>();
        if (std::isfinite(ratio)) app.blocksCodeRatio = std::clamp(ratio, 0.15f, 0.65f);
    }
    if (doc.contains("blockCategoriesCollapsed") && doc["blockCategoriesCollapsed"].is_array())
        for (size_t i = 0; i < app.canvas.collapsed.size() && i < doc["blockCategoriesCollapsed"].size(); ++i)
            if (doc["blockCategoriesCollapsed"][i].is_boolean()) app.canvas.collapsed[i] = doc["blockCategoriesCollapsed"][i].get<bool>();
    app.uiSounds = false;
    if (doc.contains("autoBase") && doc["autoBase"].is_boolean()) app.autoBase = doc["autoBase"].get<bool>();
    if (doc.contains("tutorialSeen") && doc["tutorialSeen"].is_boolean()) app.tutorialSeen = doc["tutorialSeen"].get<bool>();
    if (doc.contains("tutorialFocus") && doc["tutorialFocus"].is_boolean()) app.tutorialFocus = doc["tutorialFocus"].get<bool>();
    if (doc.contains("tutorialBlocksHidden") && doc["tutorialBlocksHidden"].is_boolean())
        app.tutorialBlocksHidden = doc["tutorialBlocksHidden"].get<bool>();
    if (doc.contains("tutorialDone") && doc["tutorialDone"].is_array())
        for (const json& item : doc["tutorialDone"])
            if (item.is_string() && app.tutorialDone.size() < 128) app.tutorialDone.insert(item.get<std::string>());
    if (doc.contains("tutorialSounds") && doc["tutorialSounds"].is_boolean()) app.tutorialSounds = doc["tutorialSounds"].get<bool>();
    if (doc.contains("spriteColors") && doc["spriteColors"].is_array())
        for (const json& item : doc["spriteColors"])
            if (item.is_number_unsigned() && app.spriteColors.size() < 48) app.spriteColors.push_back(item.get<std::uint32_t>());
    if (doc.contains("tutorialAreas") && doc["tutorialAreas"].is_object())
        for (const auto& [key, value] : doc["tutorialAreas"].items())
            if (value.is_number_integer() && app.tutorialAreas.size() < 16) app.tutorialAreas[key] = std::clamp(value.get<int>(), 1, 2);
    if (doc.contains("engineInstalls") && doc["engineInstalls"].is_array())
        for (const json& item : doc["engineInstalls"]) {
            if (!item.is_object() || !item.contains("engine") || !item.contains("path") || !item["engine"].is_string() ||
                !item["path"].is_string() || app.knownInstalls.size() >= 12) continue;
            const std::string key = item["engine"].get<std::string>();
            for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice})
                if (key == engineKey(engine)) app.knownInstalls.emplace_back(engine, pathFromUtf8(item["path"].get<std::string>()));
        }
    if (doc.contains("hud") && doc["hud"].is_object()) {
        readHudLayer(doc["hud"], "judgement", app.settings.judgement);
        readHudLayer(doc["hud"], "combo", app.settings.combo);
        readHudLayer(doc["hud"], "score", app.score);
    }
}

void saveSettings(const NoteLabApp& app) {
    if (app.transient) return;
    json doc;
    doc["spanish"] = app.spanish;
    doc["arrowsToo"] = app.arrowsToo;
    doc["psychColors"] = app.psychColors;
    doc["musicVolume"] = app.musicVolume;
    doc["audioSettingsVersion"] = 1;
    doc["keys"] = json::array();
    for (ImGuiKey key : app.keys) doc["keys"].push_back(static_cast<int>(key));
    doc["hud"] = json{{"judgement", hudLayerJson(app.settings.judgement)}, {"combo", hudLayerJson(app.settings.combo)},
                      {"score", hudLayerJson(app.score)}};
    doc["recentProjects"] = app.recentProjects;
    if (!app.exportFolder.empty()) doc["exportFolder"] = app.exportFolder.u8string();
    doc["blocksView"] = app.blocksView;
    doc["leftPanel"] = app.leftPanel;
    doc["resourceSidebarPage"] = app.resourceSidebar.page;
    doc["blocksCodeRatio"] = app.blocksCodeRatio;
    doc["blockCategoriesCollapsed"] = app.canvas.collapsed;
    doc["uiSounds"] = false;
    doc["autoBase"] = app.autoBase;
    doc["tutorialSeen"] = app.tutorialSeen;
    doc["tutorialDone"] = app.tutorialDone;
    doc["tutorialFocus"] = app.tutorialFocus;
    doc["tutorialBlocksHidden"] = app.tutorialBlocksHidden;
    doc["tutorialAreas"] = app.tutorialAreas;
    doc["tutorialSounds"] = app.tutorialSounds;
    doc["spriteColors"] = app.spriteColors;
    doc["engineInstalls"] = json::array();
    for (const auto& install : app.knownInstalls)
        doc["engineInstalls"].push_back(json{{"engine", engineKey(install.first)}, {"path", install.second.u8string()}});
    std::error_code ec;
    fs::create_directories(settingsFolder(), ec);
    std::ofstream file(settingsFolder() / "settings.json", std::ios::binary);
    if (file) file << doc.dump(2);
}

bool sourceEngine(const NoteLabApp& app, const Source& source, Engine& engine) {
    if (app.engineChoice > 0) {
        engine = static_cast<Engine>(app.engineChoice - 1);
        return true;
    }
    engine = source.guess.engine;
    return source.guess.found;
}

bool styleVisible(const NoteLabApp& app, const Source& source, const NoteStyle& style) {
    if (!style.referenced && !app.showUnused) return false;
    if (app.showOtherEngines) return true;
    Engine engine;
    if (!sourceEngine(app, source, engine)) return true;
    return style.engine == engine;
}

// Los tipos de nota del propio mod que se ven: sin los de su juego base ni los
// de otro motor (como `typeVisible`).
size_t ownTypeCount(const NoteLabApp& app, const Source& source) {
    Engine engine;
    const bool known = sourceEngine(app, source, engine);
    size_t count = 0;
    for (size_t t = 0; t < source.noteTypes.size(); ++t) {
        if (t < source.typeFromBase.size() && source.typeFromBase[t]) continue;
        if (!app.showOtherEngines && known && source.noteTypes[t].engine != engine) continue;
        ++count;
    }
    return count;
}

Source* selectedSource(NoteLabApp& app) {
    if (app.selSource < 0 || app.selSource >= static_cast<int>(app.sources.size())) return nullptr;
    return app.sources[static_cast<size_t>(app.selSource)].get();
}

const NoteStyle* selectedStyle(const NoteLabApp& app) {
    if (app.selSource < 0 || app.selSource >= static_cast<int>(app.sources.size())) return nullptr;
    const Source& source = *app.sources[static_cast<size_t>(app.selSource)];
    if (app.selStyle < 0 || app.selStyle >= static_cast<int>(source.catalog.styles.size())) return nullptr;
    return &source.catalog.styles[static_cast<size_t>(app.selStyle)];
}

NoteStyle* mutableStyle(NoteLabApp& app) {
    return const_cast<NoteStyle*>(selectedStyle(app));
}

const StyleReport* selectedReport(const NoteLabApp& app) {
    if (!selectedStyle(app)) return nullptr;
    return &app.sources[static_cast<size_t>(app.selSource)]->reports[static_cast<size_t>(app.selStyle)];
}

std::string formatTime(double ms) {
    const int total = static_cast<int>(std::max(0.0, ms) / 1000.0);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
    return buffer;
}

// ------------------------------------------------------ paleta RGB de Psych --

// Colores por defecto de las opciones de Psych (ClientPrefs.hx:28-37): uno para
// el canal rojo de la mascara, otro para el verde y otro para el azul.
const std::uint32_t kPsychRgb[4][3] = {
    {0xFFC24B99u, 0xFFFFFFFFu, 0xFF3C1F56u},
    {0xFF00FFFFu, 0xFFFFFFFFu, 0xFF1542B7u},
    {0xFF12FA05u, 0xFFFFFFFFu, 0xFF0A4447u},
    {0xFFF9393Fu, 0xFFFFFFFFu, 0xFF651038u}};
const std::uint32_t kPsychRgbPixel[4][3] = {
    {0xFFE276FFu, 0xFFFFF9FFu, 0xFF60008Du},
    {0xFF3DCAFFu, 0xFFF4FFFFu, 0xFF003060u},
    {0xFF71E300u, 0xFFF6FFE6u, 0xFF003100u},
    {0xFFFF884Eu, 0xFFFFFAF5u, 0xFF6C0000u}};

// Una copia de la imagen por carril con la formula del sombreador de Psych:
// rgb = min(r * colorR + g * colorG + b * colorB, 1) (RGBPalette.hx:149-153).
// Un tipo que fija sus colores (la Hurt Note) usa los suyos en los cuatro.
std::string psychRgbTexture(NoteLabApp& app, const NoteStyle& style, const Sheet& sheet, Part part, int lane) {
    if (!app.psychColors || !style.rgbPalette || sheet.image.empty() || part == Part::StrumStatic) return {};
    if (app.renderSource < 0 || app.renderSource >= static_cast<int>(app.sources.size())) return {};
    const bool fixed = sheet.rgbFixed.size() == 3;
    std::string key = "notelab-rgb:" + std::to_string(lane & 3) + ":" + sheet.image;
    if (fixed) {
        char palette[40];
        std::snprintf(palette, sizeof(palette), "%08X-%08X-%08X", sheet.rgbFixed[0], sheet.rgbFixed[1], sheet.rgbFixed[2]);
        key = std::string("notelab-rgb:") + palette + ":" + sheet.image;
    }
    if (app.rgbReady.count(key)) return key;
    if (app.rgbFailed.count(key)) return {};
    const Source& source = *app.sources[static_cast<size_t>(app.renderSource)];
    const auto bytes = source.vfs->readBytes(sheet.image, 64u * 1024u * 1024u);
    int width = 0, height = 0, channels = 0;
    unsigned char* rgba = bytes && !bytes->empty()
        ? stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &width, &height, &channels, 4)
        : nullptr;
    if (!rgba) {
        app.rgbFailed.insert(key);
        return {};
    }
    const std::uint32_t* colors = fixed ? sheet.rgbFixed.data()
                                : (sheet.pixel || style.pixel) ? kPsychRgbPixel[lane & 3] : kPsychRgb[lane & 3];
    float palette[3][3];
    for (int c = 0; c < 3; ++c) {
        palette[c][0] = static_cast<float>((colors[c] >> 16) & 0xFF) / 255.0f;
        palette[c][1] = static_cast<float>((colors[c] >> 8) & 0xFF) / 255.0f;
        palette[c][2] = static_cast<float>(colors[c] & 0xFF) / 255.0f;
    }
    std::vector<unsigned char> bgra(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
        const float r = rgba[i * 4 + 0] / 255.0f, g = rgba[i * 4 + 1] / 255.0f, b = rgba[i * 4 + 2] / 255.0f;
        float out[3];
        for (int k = 0; k < 3; ++k) out[k] = std::min(1.0f, r * palette[0][k] + g * palette[1][k] + b * palette[2][k]);
        bgra[i * 4 + 0] = static_cast<unsigned char>(std::lround(out[2] * 255.0f));
        bgra[i * 4 + 1] = static_cast<unsigned char>(std::lround(out[1] * 255.0f));
        bgra[i * 4 + 2] = static_cast<unsigned char>(std::lround(out[0] * 255.0f));
        bgra[i * 4 + 3] = rgba[i * 4 + 3];
    }
    stbi_image_free(rgba);
    if (!app.renderer.uploadDynamicFrame(key, bgra.data(), width, height)) {
        app.rgbFailed.insert(key);
        return {};
    }
    app.rgbReady.insert(key);
    return key;
}

// ------------------------------------------------------------ carga de mods --

bool activateRenderer(NoteLabApp& app, int sourceIndex) {
    if (app.renderSource == sourceIndex && app.rendererReady) return true;
    if (app.rendererReady) app.renderer.shutdown();
    app.rendererReady = false;
    app.renderSource = -1;
    app.atlases = AtlasStore{};
    app.preview.clearCache();
    app.rgbReady.clear();
    app.rgbFailed.clear();
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size())) return false;
    Vfs* vfs = app.sources[static_cast<size_t>(sourceIndex)]->vfs.get();
    app.atlases.readText = [vfs](const std::string& path) {
        const auto text = vfs->readText(path);
        return text ? *text : std::string();
    };
    std::string error;
    app.rendererReady = app.renderer.init([vfs](const std::string& path) {
        const auto found = vfs->resolve(path);
        return found ? found->u8string() : std::string();
    }, &error);
    if (!app.rendererReady) {
        setStatus(app, "Preview setup failed: " + error, "No se pudo preparar la vista previa: " + error);
        return false;
    }
    app.renderSource = sourceIndex;
    return true;
}

// La cancion va `audioOffsetMs` por delante del audio (Psych `offset`,
// V-Slice `offsets.instrumental`): el audio se coloca en cancion - desfase.
void audioSeek(NoteLabApp& app, double songMs) {
    if (nlbuild::audioPlayback && app.audioLoaded) app.audio.seekMs(std::max(0.0, songMs - app.audioOffsetMs));
}

// El marcador de «Jugar» a cero: juicios, fallos, combo y el ultimo juicio.
// Pasa en cada pasada nueva (reiniciar, saltar, otro chart, empezar a jugar o
// cambiar de lado) y a mano; antes solo lo hacia «Reiniciar» y el marcador
// sumaba vuelta tras vuelta (pedido del autor del 30 sep).
void resetScore(NoteLabApp& app) {
    app.counts = {};
    app.maxCombo = 0;
    app.lastJudgement = Judgement::None;
    app.state.combo = 0;
    app.state.popups.clear();
    app.finished = false;
}

void resetPlayback(NoteLabApp& app) {
    app.state = PreviewState{};
    app.state.hit.assign(app.notes.size(), 0);
    app.missed.assign(app.notes.size(), 0);
    resetScore(app);
    audioSeek(app, 0.0);
}

void stopSong(NoteLabApp& app) {
    if (app.audioReady) {
        app.audio.pause();
        app.audio.clearTracks();
    }
    app.audioPending = false;
    app.audioLoaded = false;
    app.songSource = -1;
    app.songIndex = -1;
    app.songLabel.clear();
    app.noteTypes.clear();
    app.typeUses.clear();
    app.songValues = SongValues{};
    app.hasSongValues = false;
    app.audioOffsetMs = 0.0;
    app.songSplash = -1;
    app.songSkinMissing.clear();
    app.settings.lineSpeed = {0.0f, 0.0f};
}

void chooseDemo(NoteLabApp& app) {
    stopSong(app);
    app.notes = demoPattern(app.bpm, app.patternMs);
    app.settings.scrollSpeed = std::max(0.5f, app.settings.scrollSpeed);
    resetPlayback(app);
}

void seekTo(NoteLabApp& app, double ms) {
    const NoteStyle* style = selectedStyle(app);
    resetPlayback(app);
    app.state.songMs = std::clamp(ms, 0.0, std::max(0.0, app.patternMs - 1.0));
    audioSeek(app, app.state.songMs);
    if (!style) return;
    advanceAutoplay(app.state, *style, app.notes, -1.0, app.state.songMs, {true, true}, 0.0);
    // Saltar no es jugar: las notas de antes cuentan como pasadas, sin combo.
    app.state.splashes.clear();
    app.state.popups.clear();
    app.state.combo = 0;
    for (size_t slot = 0; slot < app.state.strum.size(); ++slot) {
        app.state.strum[slot] = StrumState::Static;
        app.state.strumSinceMs[slot] = app.state.songMs;
        app.state.confirmUntilMs[slot] = 0.0;
    }
}

void selectStyle(NoteLabApp& app, int sourceIndex, int styleIndex) {
    // Los receptores de la vista previa son automaticos: un estilo que trae
    // los suyos los enseña al elegirlo, aunque antes se vieran los de otro.
    if (sourceIndex != app.selSource || styleIndex != app.selStyle)
        if (sourceIndex >= 0 && sourceIndex < static_cast<int>(app.sources.size())) {
            const auto& styles = app.sources[static_cast<size_t>(sourceIndex)]->catalog.styles;
            if (styleIndex >= 0 && styleIndex < static_cast<int>(styles.size()) &&
                std::any_of(styles[static_cast<size_t>(styleIndex)].parts.begin(), styles[static_cast<size_t>(styleIndex)].parts.end(),
                            [](const PartBinding& b) { return b.part == Part::StrumStatic; }))
                app.previewReceptors.clear();
        }
    app.selSource = sourceIndex;
    app.selStyle = styleIndex;
    activateRenderer(app, sourceIndex);
    seekTo(app, app.state.songMs);
    app.assetKind = 0;
    app.assetIndex = 0;
    app.assetPart = -1;
    app.editPart = -1;
    app.editHud = -1;
    app.assetFit = true;
}

// El primero que se ve: los del mod antes que los del juego base y, entre
// ellos, el de todo el mod.
int firstVisibleStyle(const NoteLabApp& app, const Source& source) {
    int chosen = -1;
    auto rank = [&](size_t i) {
        const bool base = i < source.fromBase.size() && source.fromBase[i];
        return (base ? 0 : 2) + (source.catalog.styles[i].use == StyleUse::Default ? 1 : 0);
    };
    for (size_t i = 0; i < source.catalog.styles.size(); ++i) {
        if (!styleVisible(app, source, source.catalog.styles[i])) continue;
        if (chosen < 0 || rank(i) > rank(static_cast<size_t>(chosen))) chosen = static_cast<int>(i);
    }
    return chosen;
}

std::unique_ptr<Vfs> mountVfs(const std::vector<fs::path>& roots, size_t baseRoot = static_cast<size_t>(-1)) {
    auto vfs = std::make_unique<Vfs>();
    vfs->setUtf8Text(true);
    vfs->setScanLimit(250000);
    for (size_t i = 0; i < roots.size(); ++i) vfs->pushRoot(roots[i], i == baseRoot ? "base" : "mod");
    return vfs;
}

// El motor de una carpeta elegida como juego base: por su skin por defecto y,
// si no lo dice, por lo que contiene.
bool baseEngineOf(const fs::path& folder, Engine& engine) {
    int found = 0;
    for (Engine candidate : {Engine::Codename, Engine::Psych, Engine::VSlice})
        if (isEngineInstall(folder, candidate)) {
            engine = candidate;
            ++found;
        }
    if (found == 1) return true;
    auto vfs = mountVfs({folder});
    if (vfs->roots().empty() || vfs->scanLimitReached()) return false;
    const EngineGuess guess = guessEngine(*vfs, scanNoteStyles(*vfs));
    engine = guess.engine;
    return guess.found && guess.other.empty();
}

// Una instalacion oficial vista (abierta o elegida como base) se recuerda para
// la proxima busqueda, la ultima primero.
void rememberInstall(NoteLabApp& app, Engine engine, const fs::path& folder) {
    auto& list = app.knownInstalls;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& item) {
        return item.first == engine && item.second == folder;
    }), list.end());
    list.insert(list.begin(), {engine, folder});
    if (list.size() > 12) list.resize(12);
}

std::vector<fs::path> rememberedFor(const NoteLabApp& app, Engine engine) {
    std::vector<fs::path> out;
    for (const auto& item : app.knownInstalls)
        if (item.first == engine) out.push_back(item.second);
    return out;
}

// Como se encontro el juego base de un mod, en palabras.
const char* baseHowText(BaseFound how, bool spanish) {
    switch (how) {
        case BaseFound::User: return spanish ? "elegido por ti" : "chosen by you";
        case BaseFound::Contains: return spanish ? "el mod está dentro" : "the mod is inside it";
        case BaseFound::Remembered: return spanish ? "ya usado antes" : "used before";
        case BaseFound::Nearby: return spanish ? "encontrado junto al mod" : "found next to the mod";
        case BaseFound::None: break;
    }
    return "";
}

// Valida todos los estilos y pasa a informacion lo de otro motor
// (FML-NOTE-020).
void recheck(NoteLabApp& app, Source& source) {
    source.reports.clear();
    for (const NoteStyle& style : source.catalog.styles)
        source.reports.push_back(checkNoteStyle(*source.vfs, style, source.atlasCache));
    Engine engine;
    if (sourceEngine(app, source, engine)) settleOtherEngines(source.catalog, source.reports, engine);
}

// Note Lab es solo para Codename, Psych y V-Slice (DESIGN_PLUGIN_NOTE_LAB §30):
// un mod de otro motor no se abre, y se dice cual es y por que se sabe.
void sayOtherEngine(NoteLabApp& app, const Source& source) {
    const std::string name = sourceName(source);
    std::string marks;
    for (size_t e = 0; e < source.guess.otherEvidence.size() && e < 4; ++e)
        marks += (e ? ", " : "") + source.guess.otherEvidence[e];
    const std::string& other = source.guess.other;
    setStatus(app, name + " is " + other + " (" + marks + "). Note Lab works only with Codename, Psych and V-Slice, so it does not open it.",
              name + " es de " + other + " (" + marks + "). Note Lab solo trabaja con Codename, Psych y V-Slice, así que no lo abre.");
}

// El mod se lee primero solo, para saber su motor. Debajo va el juego base de
// ese motor (DESIGN_PLUGIN_NOTE_LAB §30): el que fijo la persona o, si el mod no
// trae el skin por defecto de su motor, el que se encuentre solo. Nunca uno de
// otro motor: mezclaria su `assets/` con el del mod (VS Matt con el juego base
// de V-Slice perdia sus salpicaduras).
bool loadSource(NoteLabApp& app, Source& source) {
    source.catalog = Catalog{};
    source.reports.clear();
    source.guess = EngineGuess{};
    source.withBase = false;
    source.basePath.clear();
    source.baseHow = BaseFound::None;
    source.baseMissing = false;
    source.fromBase.clear();
    source.songs.clear();
    source.edited.clear();
    source.originals.clear();
    source.atlasCache = AtlasCache{};
    source.baseRootIndex = static_cast<size_t>(-1);
    source.packsMounted = 0;
    source.layout = OpenLayout{};
    source.layout.mod = source.root;
    if (!source.baseOnly) source.layout = openLayoutOf(source.root, source.modName);
    else source.layout.mods = openLayoutOf(source.root).mods;
    const fs::path modRoot = source.layout.mod;
    source.vfs = mountVfs({modRoot});
    if (source.vfs->scanLimitReached()) {
        setStatus(app, "The selected folder has more than 250000 files: choose the mod folder itself.",
                  "La carpeta elegida tiene más de 250 000 archivos: elige la carpeta del mod.");
        return false;
    }
    if (source.vfs->roots().empty()) {
        setStatus(app, "That is not a readable folder or ZIP archive.",
                  "Eso no es una carpeta ni un ZIP que se pueda leer.");
        return false;
    }
    source.catalog = scanNoteStyles(*source.vfs);
    source.guess = guessEngine(*source.vfs, source.catalog);
    if (!source.guess.other.empty()) {
        source.catalog = Catalog{};
        source.noteTypes.clear();
        source.typeFromBase.clear();
        source.songFromBase.clear();
        sayOtherEngine(app, source);
        return false;
    }
    Engine engine;
    if (sourceEngine(app, source, engine)) {
        // Codename: los paquetes y addons del mod encima de el y los [LOW]
        // debajo, como los monta el motor (y FML, CodenameAdapter.cpp:845-905).
        std::vector<fs::path> roots;
        if (engine == Engine::Codename) roots = source.layout.packs;
        roots.push_back(modRoot);
        if (engine == Engine::Codename) roots.insert(roots.end(), source.layout.lowPacks.begin(), source.layout.lowPacks.end());
        const bool ownDefault = hasEngineDefault(source.catalog, engine);
        if (ownDefault && isOfficialInstall(modRoot, engine)) rememberInstall(app, engine, modRoot);
        const fs::path& chosen = app.baseRoots[static_cast<size_t>(engine)];
        BaseSearch base;
        if (!chosen.empty()) {
            base.folder = chosen;
            base.how = BaseFound::User;
        } else if (!source.layout.install.empty() && isEngineInstall(source.layout.install, engine)) {
            // El mod esta dentro de la instalacion: debajo va siempre su
            // `assets/`, como lo carga el motor, aunque traiga su propio skin
            // (le faltan el HUD, las salpicaduras o los sonidos que hereda).
            base.folder = source.layout.install;
            base.how = BaseFound::Contains;
        } else if (app.autoBase && !ownDefault) {
            base = findEngineBase(modRoot, engine, rememberedFor(app, engine));
            source.baseMissing = base.folder.empty();
        }
        if (base.folder == modRoot) base = BaseSearch{};
        size_t baseRoot = static_cast<size_t>(-1);
        if (!base.folder.empty()) {
            baseRoot = roots.size();
            roots.push_back(baseMountOf(base.folder));
        }
        if (roots.size() > 1) {
            auto mounted = mountVfs(roots, baseRoot);
            if (!mounted->roots().empty() && !mounted->scanLimitReached()) {
                source.vfs = std::move(mounted);
                source.catalog = scanNoteStyles(*source.vfs);
                if (engine == Engine::Codename) source.packsMounted = source.layout.packs.size();
                if (!base.folder.empty()) {
                    source.withBase = true;
                    source.basePath = base.folder;
                    source.baseHow = base.how;
                    source.baseRootIndex = baseRoot;
                }
            }
        }
    }
    recheck(app, source);
    source.noteTypes = scanNoteTypes(*source.vfs, source.catalog);
    source.fromBase.assign(source.catalog.styles.size(), 0);
    source.edited.assign(source.catalog.styles.size(), 0);
    if (source.withBase)
        for (size_t i = 0; i < source.catalog.styles.size(); ++i) {
            const auto entry = source.vfs->find(source.catalog.styles[i].definition);
            source.fromBase[i] = entry && entry->rootIndex == source.baseRootIndex;
        }
    source.songs = scanSongs(*source.vfs);
    // Los tipos y los charts del juego base van aparte de los del mod: los
    // tiene a mano, pero no son suyos.
    source.typeFromBase.assign(source.noteTypes.size(), 0);
    source.songFromBase.assign(source.songs.size(), 0);
    if (source.withBase) {
        auto rootOf = [&](const std::string& path) -> int {
            if (path.empty()) return -1;
            const auto entry = source.vfs->find(path);
            return entry ? static_cast<int>(entry->rootIndex) : -1;
        };
        for (size_t t = 0; t < source.noteTypes.size(); ++t) {
            const NoteTypeEntry& type = source.noteTypes[t];
            std::string look;
            for (const NoteStyle& style : source.catalog.styles)
                if (style.id == type.lookStyle) look = style.definition;
            int known = 0;
            bool allBase = true;
            const std::string* paths[] = {&type.script, &type.config, &look};
            for (const std::string* path : paths) {
                const int root = rootOf(*path);
                if (root < 0) continue;
                ++known;
                allBase = allBase && static_cast<size_t>(root) == source.baseRootIndex;
            }
            source.typeFromBase[t] = known > 0 && allBase;
        }
        for (size_t i = 0; i < source.songs.size(); ++i) {
            const int root = rootOf(source.songs[i].path);
            source.songFromBase[i] = root >= 0 && static_cast<size_t>(root) == source.baseRootIndex;
        }
    }
    return true;
}

void describeLoaded(NoteLabApp& app, const Source& source) {
    int visible = 0;
    for (const NoteStyle& style : source.catalog.styles) if (styleVisible(app, source, style)) ++visible;
    const std::string name = sourceName(source);
    const std::string n = std::to_string(visible);
    const std::string songs = std::to_string(source.songs.size());
    if (!source.guess.other.empty()) {
        sayOtherEngine(app, source);
        return;
    }
    if (!source.guess.found && source.catalog.styles.empty()) {
        setStatus(app, name + ": no note styles and no engine structure found here.",
                  name + ": aquí no hay estilos de notas ni estructura de ningún motor.");
        return;
    }
    const std::string engine = source.guess.found ? engineLabel(source.guess.engine) : "?";
    if (source.baseMissing) {
        setStatus(app, name + " (" + engine + "): it does not bring the default skin of its engine and no " + engine +
                           " install was found next to it. Add it with \"Base game\". " + n + " note styles, " + songs + " charts.",
                  name + " (" + engine + "): no trae el skin por defecto de su motor y no encontré una instalación de " + engine +
                           " junto a él. Añádela con «Juego base». " + n + " estilos de notas, " + songs + " charts.");
        return;
    }
    if (visible == 0 && !source.withBase) {
        setStatus(app, name + " (" + engine + "): no note styles of its own; it uses the base game's. Add the base game to see them. " + songs + " charts.",
                  name + " (" + engine + "): no trae estilos de notas propios; usa los del juego base. Añade el juego base para verlos. " + songs + " charts.");
        return;
    }
    std::string baseEn, baseEs;
    if (source.withBase) {
        const std::string folder = source.basePath.filename().u8string();
        baseEn = std::string(" (with the base game ") + folder + ", " + baseHowText(source.baseHow, false) + ")";
        baseEs = std::string(" (con el juego base ") + folder + ", " + baseHowText(source.baseHow, true) + ")";
    }
    if (!source.baseOnly && !source.layout.install.empty() && source.layout.mods.size() > 1) {
        const std::string count = std::to_string(source.layout.mods.size());
        baseEn += "; the install has " + count + " mods (right click on its card to open another)";
        baseEs += "; la instalación tiene " + count + " mods (clic derecho en su tarjeta para abrir otro)";
    }
    if (source.packsMounted > 0) {
        const std::string count = std::to_string(source.packsMounted);
        baseEn += ", " + count + " content packs on top";
        baseEs += ", " + count + " paquetes de contenido encima";
    }
    setStatus(app, name + ": " + engine + " detected, " + n + " note styles" + baseEn + ", " + songs + " charts.",
              name + ": detectado " + engine + ", " + n + " estilos de notas" + baseEs + ", " + songs + " charts.");
}

void addSource(NoteLabApp& app, const fs::path& root, const std::string& modName = {}, bool baseOnly = false) {
    for (const auto& existing : app.sources)
        if (existing->root == root && existing->modName == modName && existing->baseOnly == baseOnly) {
            setStatus(app, "That mod is already open.", "Ese mod ya está abierto.");
            return;
        }
    if (static_cast<int>(app.sources.size()) >= kMaxSources) {
        setStatus(app, "Up to four mods at once: close one first.", "Hasta cuatro mods a la vez: cierra uno antes.");
        return;
    }
    auto source = std::make_unique<Source>();
    source->root = root;
    source->modName = modName;
    source->baseOnly = baseOnly;
    if (!loadSource(app, *source)) return;
    app.sources.push_back(std::move(source));
    app.dirty = true;
    const int index = static_cast<int>(app.sources.size()) - 1;
    describeLoaded(app, *app.sources.back());
    const int style = firstVisibleStyle(app, *app.sources.back());
    if (style >= 0) selectStyle(app, index, style);
}

void removeSource(NoteLabApp& app, int index) {
    if (index < 0 || index >= static_cast<int>(app.sources.size())) return;
    if (app.media.source == index) {
        clearMediaPreview(app);
        app.media.source = -1; app.media.requestOpen = false; app.media.importRequestOpen = false;
        app.media.importFiles.clear(); app.media.resources.clear(); app.media.usedPaths.clear();
        app.media.selected.clear(); app.resourceSidebar.selected.clear();
    } else if (app.media.source > index) --app.media.source;
    app.dirty = true;
    if (app.songSource == index) chooseDemo(app);
    else if (app.songSource > index) --app.songSource;
    if (app.renderSource == index) activateRenderer(app, -1);
    else if (app.renderSource > index) --app.renderSource;
    app.sources.erase(app.sources.begin() + index);
    auto remapHistory = [&](std::vector<UndoEntry>& history) {
        history.erase(std::remove_if(history.begin(), history.end(), [&](const UndoEntry& entry) { return entry.source == index; }), history.end());
        for (UndoEntry& entry : history) if (entry.source > index) --entry.source;
    };
    remapHistory(app.undo);
    remapHistory(app.redo);
    if (app.exporting.source == index) { app.exporting.open = false; app.exporting.source = -1; app.exporting.preparedKey.clear(); }
    else if (app.exporting.source > index) --app.exporting.source;
    if (app.selSource == index) { app.selSource = -1; app.selStyle = -1; }
    else if (app.selSource > index) --app.selSource;
    if (app.typeSource == index) { app.typeSource = -1; app.selType = -1; }
    else if (app.typeSource > index) --app.typeSource;
    if (app.blocksSource == index) { app.blocksSource = -1; app.blocksType.clear(); }
    else if (app.blocksSource > index) --app.blocksSource;
    if (app.selSource < 0)
        for (size_t i = 0; i < app.sources.size(); ++i) {
            const int style = firstVisibleStyle(app, *app.sources[i]);
            if (style >= 0) { selectStyle(app, static_cast<int>(i), style); break; }
        }
}

void requestSourceClose(NoteLabApp& app, int index) {
    if (index < 0 || index >= static_cast<int>(app.sources.size())) return;
    const Source& source = *app.sources[static_cast<size_t>(index)];
    const bool hasWork = std::any_of(source.edited.begin(), source.edited.end(), [](std::uint8_t value) { return value != 0; }) ||
                         !source.typeBlocks.empty() || !source.recipes.empty() || !source.bots.empty() || !source.importedFiles.empty();
    if (app.dirty && hasWork) { app.sourceToClose = index; app.openSourceClose = true; }
    else removeSource(app, index);
}

// ----------------------------------------------------------------- proyecto --

// Lo editado de un mod: cada estilo cambiado entero y la huella de los
// archivos que usa, para el proyecto y para volver a ponerlo tras recargar.
// Una variante creada en Note Lab: no esta en el mod, vive en el proyecto.
bool isVariant(const NoteStyle& style) {
    return style.id.rfind("notelab:", 0) == 0;
}

#include "Tutorial.hpp"

std::vector<ProjectEdit> editsOf(const Source& source) {
    std::vector<ProjectEdit> edits;
    for (size_t i = 0; i < source.catalog.styles.size() && i < source.edited.size(); ++i) {
        const NoteStyle& style = source.catalog.styles[i];
        if (!source.edited[i] && !isVariant(style)) continue;
        ProjectEdit edit;
        edit.styleId = style.id;
        edit.style = style;
        edit.created = isVariant(style);
        std::set<std::string> paths;
        for (const Sheet& sheet : style.sheets) {
            if (!sheet.image.empty()) paths.insert(sheet.image);
            if (!sheet.atlas.empty()) paths.insert(sheet.atlas);
        }
        auto hudPaths = [&](const HudAsset& hud) {
            if (!hud.image.empty()) paths.insert(hud.image);
            if (!hud.sound.empty()) paths.insert(hud.sound);
        };
        for (const auto& hud : style.judgements) hudPaths(hud);
        for (const auto& hud : style.digits) hudPaths(hud);
        for (const auto& hud : style.countdown) hudPaths(hud);
        hudPaths(style.combo);
        for (const auto& sound : style.sounds) if (!sound.path.empty()) paths.insert(sound.path);
        for (const std::string& path : paths)
            if (const auto bytes = source.vfs->readBytes(path, 256u * 1024u * 1024u))
                edit.files.push_back({path, sha256Hex(std::string(bytes->begin(), bytes->end()))});
        edits.push_back(std::move(edit));
    }
    return edits;
}

// Vuelve a montar las hojas importadas en la VFS nueva de un mod.
void remountImports(Source& source, std::vector<Issue>& issues) {
    for (const ProjectImport& item : source.importedFiles) {
        const bool image = source.vfs->pushFile(pathFromUtf8(item.image), item.imageVirtual, "import");
        const bool atlas = item.atlas.empty() || source.vfs->pushFile(pathFromUtf8(item.atlas), item.atlasVirtual, "import");
        if (!image || !atlas)
            issues.push_back({"The imported sheet " + item.image + " is no longer on disk.",
                              "La hoja importada " + item.image + " ya no está en el disco."});
        const size_t slash = item.imageVirtual.find('/', 15);
        if (item.imageVirtual.rfind("notelab-import/", 0) == 0 && slash != std::string::npos)
            source.imports = std::max(source.imports, std::atoi(item.imageVirtual.substr(15, slash - 15).c_str()));
    }
}

// Pone lo editado sobre lo leido del mod. Lo que ya no esta o cambio desde
// que se guardo se dice; el estilo editado se queda con la version nueva.
void applyEdits(NoteLabApp& app, Source& source, const std::vector<ProjectEdit>& edits, std::vector<Issue>& issues) {
    const std::string mod = sourceName(source);
    for (const ProjectEdit& edit : edits) {
        size_t index = source.catalog.styles.size();
        for (size_t i = 0; i < source.catalog.styles.size(); ++i)
            if (source.catalog.styles[i].id == edit.styleId) { index = i; break; }
        if (index == source.catalog.styles.size() && (edit.created || isVariant(edit.style))) {
            // Una variante: se vuelve a crear.
            source.catalog.styles.push_back(edit.style);
            source.reports.push_back(checkNoteStyle(*source.vfs, edit.style, source.atlasCache));
            source.fromBase.push_back(0);
            source.edited.push_back(1);
            continue;
        }
        if (index == source.catalog.styles.size()) {
            issues.push_back({"The edited style " + edit.styleId + " is no longer in " + mod + ": its changes stay out.",
                              "El estilo editado " + edit.styleId + " ya no está en " + mod + ": sus cambios se quedan fuera."});
            continue;
        }
        for (const ProjectFile& file : edit.files) {
            const auto bytes = source.vfs->readBytes(file.virtualPath, 256u * 1024u * 1024u);
            if (!bytes)
                issues.push_back({file.virtualPath + " (used by " + edit.style.name + ") is no longer there.",
                                  file.virtualPath + " (lo usa " + edit.style.name + ") ya no está."});
            else if (!file.sha256.empty() && sha256Hex(std::string(bytes->begin(), bytes->end())) != file.sha256)
                issues.push_back({file.virtualPath + " changed since the project was saved.",
                                  file.virtualPath + " cambió desde que se guardó el proyecto."});
        }
        if (!source.originals.count(index)) source.originals[index] = source.catalog.styles[index];
        source.catalog.styles[index] = edit.style;
        if (index < source.edited.size()) source.edited[index] = 1;
        if (index < source.reports.size()) source.reports[index] = checkNoteStyle(*source.vfs, source.catalog.styles[index], source.atlasCache);
    }
    Engine engine;
    if (sourceEngine(app, source, engine)) settleOtherEngines(source.catalog, source.reports, engine);
}

void showIssues(NoteLabApp& app, std::vector<Issue> issues) {
    if (issues.empty()) return;
    app.issues = std::move(issues);
    app.openIssues = true;
}

// Recargar un mod lo vuelve a leer de disco y le pone encima lo editado.
void reloadKeepingEdits(NoteLabApp& app, Source& source, std::vector<Issue>& issues) {
    const std::vector<ProjectEdit> keep = editsOf(source);
    Source candidate;
    candidate.root = source.root;
    candidate.modName = source.modName;
    candidate.baseOnly = source.baseOnly;
    candidate.expanded = source.expanded;
    candidate.imports = source.imports;
    candidate.importedFiles = source.importedFiles;
    candidate.typeBlocks = source.typeBlocks;
    candidate.typeLooks = source.typeLooks;
    candidate.recipes = source.recipes;
    candidate.bots = source.bots;
    if (!loadSource(app, candidate)) {
        issues.push_back({"Reload failed; the previous version and your edits were kept.", "Falló la recarga; se conserva la versión anterior y tus cambios."});
        return;
    }
    remountImports(candidate, issues);
    applyEdits(app, candidate, keep, issues);
    applyTypeLooks(candidate);
    source = std::move(candidate);
}

void reloadAll(NoteLabApp& app) {
    const int previousSource = app.selSource;
    const std::string previousStyle = selectedStyle(app) ? selectedStyle(app)->id : std::string();
    chooseDemo(app);
    activateRenderer(app, -1);
    app.selSource = -1;
    app.selStyle = -1;
    app.undo.clear();
    app.redo.clear();
    std::vector<Issue> issues;
    for (auto& source : app.sources) reloadKeepingEdits(app, *source, issues);
    for (size_t i = 0; i < app.sources.size(); ++i) {
        const int style = firstVisibleStyle(app, *app.sources[i]);
        if (style >= 0) { selectStyle(app, static_cast<int>(i), style); break; }
    }
    if (previousSource >= 0 && previousSource < static_cast<int>(app.sources.size()))
        for (size_t index = 0; index < app.sources[static_cast<size_t>(previousSource)]->catalog.styles.size(); ++index)
            if (app.sources[static_cast<size_t>(previousSource)]->catalog.styles[index].id == previousStyle) {
                selectStyle(app, previousSource, static_cast<int>(index)); break;
            }
    showIssues(app, std::move(issues));
}

// «Juego base…»: la carpeta elegida pasa a ser el juego base de su motor para
// todos los mods de ese motor, traigan o no su skin (DESIGN_PLUGIN_NOTE_LAB §30).
void setBaseFolder(NoteLabApp& app, const fs::path& folder) {
    Engine engine;
    if (!baseEngineOf(folder, engine)) {
        setStatus(app, "That folder does not look like a Codename, Psych or V-Slice install: it has no default note skin.",
                  "Esa carpeta no parece una instalación de Codename, Psych ni V-Slice: no tiene el skin de notas por defecto.");
        return;
    }
    app.baseRoots[static_cast<size_t>(engine)] = folder;
    if (isOfficialInstall(folder, engine)) rememberInstall(app, engine, folder);
    app.dirty = true;
    reloadAll(app);
    const std::string label = engineLabel(engine);
    setStatus(app, "Base game of " + label + ": " + folder.filename().u8string() + ", under every open " + label + " mod.",
              "Juego base de " + label + ": " + folder.filename().u8string() + ", debajo de cada mod de " + label + " abierto.");
}

// Quitar el juego base fijado de un motor: vuelve a buscarse solo.
void removeBase(NoteLabApp& app, Engine engine) {
    app.baseRoots[static_cast<size_t>(engine)].clear();
    app.dirty = true;
    reloadAll(app);
}

// ---------------------------------------------------------------- canciones --

// El skin que pide la cancion: el `noteStyle` de V-Slice o el `arrowSkin` y el
// `splashSkin` de Psych (Song.hx:31-32). Si esta entre lo abierto se elige; si
// no, se dice.
void applyChartSkin(NoteLabApp& app, int sourceIndex) {
    app.songSplash = -1;
    app.songSkinMissing.clear();
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size())) return;
    const Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    const SongValues& values = app.songValues;
    auto find = [&](auto&& match) {
        for (size_t i = 0; i < source.catalog.styles.size(); ++i)
            if (match(source.catalog.styles[i])) return static_cast<int>(i);
        return -1;
    };
    std::string wanted;
    int style = -1;
    if (!values.noteStyle.empty()) {
        wanted = values.noteStyle;
        const std::string key = lowerText(values.noteStyle);
        style = find([&](const NoteStyle& s) {
            return s.engine == Engine::VSlice && lowerText(fs::u8path(s.definition).stem().u8string()) == key;
        });
    } else if (!values.arrowSkin.empty()) {
        wanted = values.arrowSkin;
        const std::string key = lowerText(values.arrowSkin);
        style = find([&](const NoteStyle& s) {
            return s.engine == Engine::Psych && s.use != StyleUse::NoteType && !s.sheets.empty() && lowerText(s.sheets[0].declared) == key;
        });
    }
    if (style >= 0) {
        if (app.selSource != sourceIndex || app.selStyle != style) selectStyle(app, sourceIndex, style);
    } else if (!wanted.empty()) {
        app.songSkinMissing = wanted;
    }
    if (!values.splashSkin.empty()) {
        const std::string key = lowerText(values.splashSkin);
        const int splash = find([&](const NoteStyle& s) {
            if (s.engine != Engine::Psych) return false;
            for (const PartBinding& binding : s.parts) {
                if (binding.part != Part::Splash || binding.sheet < 0 || static_cast<size_t>(binding.sheet) >= s.sheets.size()) continue;
                const std::string declared = lowerText(s.sheets[static_cast<size_t>(binding.sheet)].declared);
                if (declared == key || declared == "notesplashes/" + key) return true;
            }
            return false;
        });
        if (splash >= 0 && !(app.selSource == sourceIndex && app.selStyle == splash)) app.songSplash = splash;
    }
}

// Lo que el tipo de cada nota le dice al motor: si el bot la deja pasar y si
// tocarla es un fallo (NoteTypes.hpp); y cuantas hay de cada tipo.
void applyTypeRules(NoteLabApp& app) {
    app.typeUses.clear();
    if (app.songSource < 0 || app.songSource >= static_cast<int>(app.sources.size())) return;
    const Source& source = *app.sources[static_cast<size_t>(app.songSource)];
    Engine engine;
    const bool known = sourceEngine(app, source, engine);
    for (size_t i = 0; i < app.notes.size(); ++i) {
        app.notes[i].botSkips = false;
        app.notes[i].hitMisses = false;
        app.notes[i].alpha = app.notes[i].scaleX = app.notes[i].scaleY = 1.0f;
        app.notes[i].angle = 0.0f;
        if (i >= app.noteTypes.size() || app.noteTypes[i].empty()) continue;
        const std::string& type = app.noteTypes[i];
        NoteLabApp::TypeUse& use = app.typeUses[lowerText(type)];
        if (use.count++ == 0) use.firstMs = app.notes[i].timeMs;
        // Los bloques del tipo mandan sobre lo que se leyo de su script.
        const auto blocks = source.typeBlocks.find(type);
        if (blocks != source.typeBlocks.end() && !blocks->second.empty()) {
            const bool custom = std::any_of(blocks->second.nodes.begin(), blocks->second.nodes.end(), [&](const auto& item) {
                return item.second.key == "code.file" && item.second.args.size() == 3 && (!known || item.second.args[0].value == engineKey(engine));
            });
            if (!custom) {
                app.notes[i].botSkips = blocksAvoid(blocks->second);
                app.notes[i].hitMisses = blocksHitMisses(blocks->second);
                const BlockAppearance look = blocksAppearance(blocks->second);
                app.notes[i].alpha = look.alpha; app.notes[i].scaleX = look.scaleX; app.notes[i].scaleY = look.scaleY; app.notes[i].angle = look.angle;
            }
            continue;
        }
        const NoteTypeEntry* entry = known ? findNoteType(source.noteTypes, type, engine) : findNoteType(source.noteTypes, type);
        if (!entry) continue;
        app.notes[i].botSkips = botSkips(*entry, app.notes[i].strumLine);
        app.notes[i].hitMisses = entry->hitMisses;
    }
    for (size_t i = 0; i < app.notes.size() && i < app.noteTypes.size(); ++i) {
        const auto custom = source.bots.find(lowerText(app.noteTypes[i]));
        if (custom != source.bots.end()) app.notes[i].botSkips = botSkipsWithProfile(app.notes[i].botSkips, app.notes[i].strumLine, custom->second);
    }
}

void applyDistribution(NoteLabApp& app) {
    if (app.songSource < 0) return;
    const DistributeResult result = distributeTypes(app.notes, app.chartTypes, app.distribute);
    app.noteTypes = result.types;
    resetPlayback(app);
    app.distributed = true;
    app.dirty = true;
    applyTypeRules(app);
    int placed = 0;
    for (int count : result.placed) placed += count;
    setStatus(app, std::to_string(placed) + " notes got a custom type in a copy of " + app.songLabel + " (the mod's chart does not change).",
              std::to_string(placed) + " notas con tipo nuevo en una copia de " + app.songLabel + " (el chart del mod no cambia).");
}

void undoDistribution(NoteLabApp& app) {
    app.noteTypes = app.chartTypes;
    resetPlayback(app);
    app.distributed = false;
    app.dirty = true;
    applyTypeRules(app);
    setStatus(app, "The chart is back as it came.", "El chart vuelve a estar como vino.");
}

void chooseSong(NoteLabApp& app, int sourceIndex, int songIndex) {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size())) return;
    Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    if (songIndex < 0 || songIndex >= static_cast<int>(source.songs.size())) return;
    const SongChart& entry = source.songs[static_cast<size_t>(songIndex)];
    LoadedSong loaded = loadSong(*source.vfs, source.root, entry);
    if (!loaded.error.empty()) {
        setStatus(app, "Could not load " + entry.id + ": " + loaded.error, "No se pudo cargar " + entry.id + ": " + loaded.error);
        return;
    }
    stopSong(app);
    app.songSource = sourceIndex;
    app.songIndex = songIndex;
    app.songLabel = entry.id + " · " + entry.difficulty + (entry.variation.empty() ? "" : " (" + entry.variation + ")");
    app.lastSongOwner = &source;
    app.lastSongIndex = songIndex;
    app.lastSongLabel = app.songLabel;
    app.notes = previewNotesOf(loaded.chart, &app.noteTypes);
    app.chartTypes = app.noteTypes;
    app.distributionPreviewKey.clear();
    app.chartOriginal = source.vfs->readText(entry.path).value_or("");
    const auto originalBytes = source.vfs->readBytes(entry.path, 64u * 1024u * 1024u);
    app.chartExpected = originalBytes ? std::string(originalBytes->begin(), originalBytes->end()) : std::string();
    app.distributed = false;
    applyTypeRules(app);
    app.patternMs = loaded.chart.lastNoteMs() + 2500.0;
    // Todo lo que fija el chart, no solo sus notas (DESIGN_PLUGIN_NOTE_LAB §27).
    app.songValues = loaded.values;
    app.hasSongValues = true;
    app.audioOffsetMs = loaded.values.audioOffsetMs;
    app.settings.scrollSpeed = loaded.values.speed;
    if (app.chartSkin) applyChartSkin(app, sourceIndex);
    resetPlayback(app);
    if (nlbuild::audioPlayback && !loaded.audio.empty()) {
        if (!app.audioReady) {
            std::string error;
            app.audioReady = app.audio.init(&error);
            if (!app.audioReady)
                setStatus(app, "Audio could not start: " + error, "No se pudo iniciar el audio: " + error);
        }
        if (app.audioReady) {
            app.audio.loadTracksAsync(loaded.audio);
            app.audioPending = true;
        }
    }
    const std::string count = std::to_string(app.notes.size());
    if (!nlbuild::audioPlayback)
        setStatus(app, app.songLabel + ": " + count + " notes · audio disabled in this build.",
                  app.songLabel + ": " + count + " notas · audio deshabilitado en esta build.");
    else if (loaded.audio.empty())
        setStatus(app, app.songLabel + ": " + count + " notes, no audio found on disk.",
                  app.songLabel + ": " + count + " notas, sin audio en disco.");
    else
        setStatus(app, app.songLabel + ": " + count + " notes, decoding audio…",
                  app.songLabel + ": " + count + " notas, decodificando audio…");
}

#include "SongTools.hpp"

void pollAudio(NoteLabApp& app) {
    if (!nlbuild::audioPlayback) {
        if (app.audioReady) app.audio.pause();
        app.audioPending = false;
        app.audioLoaded = false;
        return;
    }
    if (!app.audioPending) return;
    int loaded = 0;
    std::string error;
    if (!app.audio.pollTrackLoad(&loaded, &error)) return;
    app.audioPending = false;
    app.audioLoaded = loaded > 0;
    if (app.audioLoaded) {
        app.audio.setMasterGain(app.musicVolume);
        audioSeek(app, app.state.songMs);
        if (app.playing) app.audio.play();
        setStatus(app, app.songLabel + ": audio ready.", app.songLabel + ": audio listo.");
    } else {
        setStatus(app, "Audio could not be decoded: " + error, "No se pudo decodificar el audio: " + error);
    }
}

// ----------------------------------------------------------------- edicion --

// El prefijo mas corto que elige exactamente la animacion de un fotograma: con
// el nombre sin numero bastaria, salvo que otra animacion empiece igual
// (`purple` tambien es el principio de `purple hold piece`).
std::string uniquePrefixFor(const SparrowAtlas& atlas, const std::string& frameName) {
    const std::string base = SparrowAtlas::stripFrameNumber(frameName);
    auto unique = [&](const std::string& prefix) {
        for (const AtlasFrame& frame : atlas.frames)
            if (frame.name.compare(0, prefix.size(), prefix) == 0 && SparrowAtlas::stripFrameNumber(frame.name) != base)
                return false;
        return true;
    };
    if (!base.empty() && unique(base)) return base;
    for (size_t length = base.size() + 1; length <= frameName.size(); ++length) {
        const std::string prefix = frameName.substr(0, length);
        if (unique(prefix)) return prefix;
    }
    return frameName;
}

void revalidate(NoteLabApp& app) {
    Source* source = selectedSource(app);
    const NoteStyle* style = selectedStyle(app);
    if (!source || !style) return;
    source->reports[static_cast<size_t>(app.selStyle)] = checkNoteStyle(*source->vfs, *style, source->atlasCache);
    Engine engine;
    if (sourceEngine(app, *source, engine)) settleOtherEngines(source->catalog, source->reports, engine);
}

void beginEdit(NoteLabApp& app) {
    Source* source = selectedSource(app);
    const NoteStyle* style = selectedStyle(app);
    if (!source || !style) return;
    std::map<std::string, CreationRecipe> recipes;
    for (const auto& item : source->recipes) if (item.second.styleId == style->id) recipes.insert(item);
    app.undo.push_back({app.selSource, app.selStyle, *style, std::move(recipes)});
    if (app.undo.size() > kMaxUndo) app.undo.erase(app.undo.begin());
    app.redo.clear();
    const size_t index = static_cast<size_t>(app.selStyle);
    if (!source->originals.count(index)) source->originals[index] = *style;
}

void afterEdit(NoteLabApp& app) {
    Source* source = selectedSource(app);
    if (!source || app.selStyle < 0) return;
    source->edited[static_cast<size_t>(app.selStyle)] = 1;
    source->soundsFrom = nullptr;
    app.dirty = true;
    revalidate(app);
}

void swapHistory(NoteLabApp& app, std::vector<UndoEntry>& from, std::vector<UndoEntry>& to) {
    if (from.empty()) return;
    UndoEntry entry = std::move(from.back());
    from.pop_back();
    if (entry.source < 0 || entry.source >= static_cast<int>(app.sources.size())) return;
    Source& source = *app.sources[static_cast<size_t>(entry.source)];
    if (entry.style < 0 || entry.style >= static_cast<int>(source.catalog.styles.size())) return;
    NoteStyle& current = source.catalog.styles[static_cast<size_t>(entry.style)];
    std::map<std::string, CreationRecipe> recipes;
    for (auto it = source.recipes.begin(); it != source.recipes.end();) {
        if (it->second.styleId == current.id) { recipes.insert(*it); it = source.recipes.erase(it); }
        else ++it;
    }
    to.push_back({entry.source, entry.style, current, std::move(recipes)});
    source.recipes.insert(entry.recipes.begin(), entry.recipes.end());
    current = std::move(entry.before);
    source.soundsFrom = nullptr;
    if (app.selSource != entry.source || app.selStyle != entry.style) selectStyle(app, entry.source, entry.style);
    source.reports[static_cast<size_t>(entry.style)] = checkNoteStyle(*source.vfs, current, source.atlasCache);
    source.edited[static_cast<size_t>(entry.style)] = 1;
    app.buffersFor.clear();
}

void revertStyle(NoteLabApp& app) {
    Source* source = selectedSource(app);
    if (!source || app.selStyle < 0) return;
    const auto original = source->originals.find(static_cast<size_t>(app.selStyle));
    if (original == source->originals.end()) return;
    beginEdit(app);
    source->catalog.styles[static_cast<size_t>(app.selStyle)] = original->second;
    source->soundsFrom = nullptr;
    for (auto it = source->recipes.begin(); it != source->recipes.end();)
        if (it->second.styleId == original->second.id) it = source->recipes.erase(it);
        else ++it;
    source->edited[static_cast<size_t>(app.selStyle)] = 0;
    app.dirty = true;
    revalidate(app);
    app.buffersFor.clear();
    setStatus(app, "Style back to how the mod has it.", "Estilo devuelto a como lo tiene el mod.");
}

// Una variante nueva de un estilo: una copia editable que no toca la del mod
// y viaja en el proyecto. Sirve para notas y para HUD nuevos.
void createVariant(NoteLabApp& app) {
    Source* source = selectedSource(app);
    const NoteStyle* style = selectedStyle(app);
    if (!source || !style) return;
    NoteStyle copy = *style;
    int next = 1;
    auto taken = [&](const std::string& id) {
        return std::any_of(source->catalog.styles.begin(), source->catalog.styles.end(), [&](const NoteStyle& s) { return s.id == id; });
    };
    do {
        copy.id = "notelab:variant" + std::to_string(next++) + ":" + style->id;
    } while (taken(copy.id));
    copy.name = style->name + tr(app, " (variant)", " (variante)");
    copy.use = StyleUse::Declared;
    copy.useDetail.clear();
    copy.referenced = true;
    source->catalog.styles.push_back(copy);
    source->reports.push_back(checkNoteStyle(*source->vfs, copy, source->atlasCache));
    source->fromBase.push_back(0);
    source->edited.push_back(1);
    Engine engine;
    if (sourceEngine(app, *source, engine)) settleOtherEngines(source->catalog, source->reports, engine);
    app.dirty = true;
    selectStyle(app, app.selSource, static_cast<int>(source->catalog.styles.size()) - 1);
    setStatus(app, "Variant created: edit it without touching the mod's style. It is saved with the project.",
              "Variante creada: edítala sin tocar el estilo del mod. Se guarda con el proyecto.");
}

void deleteVariant(NoteLabApp& app, int sourceIndex, int styleIndex) {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size())) return;
    Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    if (styleIndex < 0 || styleIndex >= static_cast<int>(source.catalog.styles.size())) return;
    const size_t index = static_cast<size_t>(styleIndex);
    if (!isVariant(source.catalog.styles[index])) return;
    const std::string removedId = source.catalog.styles[index].id;
    for (auto it = source.recipes.begin(); it != source.recipes.end();)
        if (it->second.styleId == removedId) it = source.recipes.erase(it);
        else ++it;
    source.catalog.styles.erase(source.catalog.styles.begin() + styleIndex);
    if (index < source.reports.size()) source.reports.erase(source.reports.begin() + styleIndex);
    if (index < source.fromBase.size()) source.fromBase.erase(source.fromBase.begin() + styleIndex);
    if (index < source.edited.size()) source.edited.erase(source.edited.begin() + styleIndex);
    std::map<size_t, NoteStyle> shifted;
    for (auto& entry : source.originals)
        if (entry.first < index) shifted.emplace(entry.first, std::move(entry.second));
        else if (entry.first > index) shifted.emplace(entry.first - 1, std::move(entry.second));
    source.originals = std::move(shifted);
    app.undo.clear();
    app.redo.clear();
    app.dirty = true;
    if (app.selSource == sourceIndex) {
        app.selStyle = -1;
        const int first = firstVisibleStyle(app, source);
        if (first >= 0) selectStyle(app, sourceIndex, first);
    }
    setStatus(app, "Variant deleted.", "Variante borrada.");
}

HudAsset* hudAssetAt(NoteStyle& style, int index) {
    if (index >= 0 && index < 4) return &style.judgements[static_cast<size_t>(index)];
    if (index == 4) return &style.combo;
    if (index >= 5 && index < 15) return &style.digits[static_cast<size_t>(index - 5)];
    if (index >= 15 && index < 19) return &style.countdown[static_cast<size_t>(index - 15)];
    return nullptr;
}

// Una imagen o un sonido propio para una pieza del HUD: se monta en la VFS del
// mod bajo notelab-import/, sin copiar nada dentro del mod.
void importHudFile(NoteLabApp& app, const fs::path& file, bool sound) {
    Source* source = selectedSource(app);
    NoteStyle* style = mutableStyle(app);
    if (!source || !style || !hudAssetAt(*style, app.importHud)) return;
    const std::string virtualPath = "notelab-import/" + std::to_string(++source->imports) + "/" + file.filename().u8string();
    if (!source->vfs->pushFile(file, virtualPath, "import")) {
        setStatus(app, "The file could not be mounted.", "No se pudo montar el archivo.");
        return;
    }
    source->importedFiles.push_back({file.u8string(), std::string(), virtualPath, std::string()});
    beginEdit(app);
    style = mutableStyle(app);
    HudAsset* asset = hudAssetAt(*style, app.importHud);
    if (sound) {
        asset->sound = virtualPath;
        asset->soundDeclared = file.stem().u8string();
    } else {
        asset->image = virtualPath;
        asset->declared = file.stem().u8string();
        asset->imageOptional = false;
        asset->inherited = false;
    }
    style->hasHud = true;
    afterEdit(app);
    app.editHud = app.importHud;
    setStatus(app, "HUD file changed: " + file.filename().u8string(), "Archivo del HUD cambiado: " + file.filename().u8string());
}

// La escala con que el motor dibuja cada imagen del HUD sin que el estilo la
// declare: juicios y rotulo a 0.7, cifras a 0.5 (Codename RatingsShowEvent.hx:23,
// :31; Psych PlayState.hx:2625-2626, :2650). V-Slice la declara en el notestyle.
float hudEngineScale(Engine engine, int index) {
    if (engine == Engine::VSlice) return 1.0f;
    if (index <= 4) return 0.7f;
    if (index <= 14) return 0.5f;
    return 1.0f;
}

// El HUD de otro estilo, tambien de otro mod abierto, en el estilo elegido sin
// cambiar de mod (pedido del autor del 29 sep). Sus imagenes y sonidos se
// montan en la VFS de este mod como una importacion (nada se escribe en ningun
// mod) y la escala se pasa a la del motor de este estilo: la misma en pantalla.
void borrowHud(NoteLabApp& app, int fromSource, int fromStyle) {
    Source* target = selectedSource(app);
    if (!target || !mutableStyle(app)) return;
    if (fromSource < 0 || fromSource >= static_cast<int>(app.sources.size())) return;
    Source& from = *app.sources[static_cast<size_t>(fromSource)];
    if (fromStyle < 0 || fromStyle >= static_cast<int>(from.catalog.styles.size())) return;
    const NoteStyle donor = from.catalog.styles[static_cast<size_t>(fromStyle)];
    if (!donor.hasHud) return;
    int missing = 0;
    auto mount = [&](const std::string& virtualPath) -> std::string {
        if (virtualPath.empty()) return {};
        if (&from == target) return virtualPath;   // el mismo mod: la misma VFS
        std::optional<fs::path> real = from.vfs->resolve(virtualPath);
        if (!real) {
            // Dentro de un ZIP: una copia en la carpeta de preferencias, con su
            // huella en el nombre, para que el proyecto la vuelva a montar.
            if (const auto bytes = from.vfs->readBytes(virtualPath, 64u * 1024u * 1024u)) {
                const std::string content(bytes->begin(), bytes->end());
                const fs::path cache = settingsFolder() / "borrowed" /
                    pathFromUtf8(sha256Hex(content).substr(0, 16) + "-" + pathFromUtf8(virtualPath).filename().u8string());
                std::error_code ec;
                fs::create_directories(cache.parent_path(), ec);
                if (!fs::exists(cache, ec)) {
                    std::ofstream out(cache, std::ios::binary);
                    out.write(content.data(), static_cast<std::streamsize>(content.size()));
                }
                if (fs::exists(cache, ec)) real = cache;
            }
        }
        if (!real) {
            ++missing;
            return {};
        }
        const std::string mounted = "notelab-import/" + std::to_string(++target->imports) + "/" + real->filename().u8string();
        if (!target->vfs->pushFile(*real, mounted, "import")) {
            ++missing;
            return {};
        }
        target->importedFiles.push_back({real->u8string(), std::string(), mounted, std::string()});
        return mounted;
    };
    beginEdit(app);
    NoteStyle* style = mutableStyle(app);
    int copied = 0;
    for (int i = 0; i < 19; ++i) {
        const HudAsset* source = hudAssetAt(const_cast<NoteStyle&>(donor), i);
        HudAsset* asset = hudAssetAt(*style, i);
        if (!source || !asset || (source->image.empty() && source->sound.empty() && !source->imageOptional)) continue;
        HudAsset copy = *source;
        copy.image = mount(source->image);
        copy.sound = mount(source->sound);
        copy.nearby.clear();
        copy.inherited = false;
        copy.scale = source->scale * hudEngineScale(donor.engine, i) / hudEngineScale(style->engine, i);
        *asset = copy;
        ++copied;
    }
    style->hasHud = true;
    afterEdit(app);
    const std::string mod = sourceName(from);
    std::string en = "HUD of " + donor.name + " (" + mod + ") in " + style->name + ": " + std::to_string(copied) + " images and sounds, mounted without switching mods.";
    std::string es = "HUD de " + donor.name + " (" + mod + ") en " + style->name + ": " + std::to_string(copied) + " imágenes y sonidos, montados sin cambiar de mod.";
    if (missing > 0) {
        en += " " + std::to_string(missing) + " could not be read.";
        es += " " + std::to_string(missing) + " no se pudieron leer.";
    }
    setStatus(app, en, es);
}

#include "StyleComposer.hpp"

// Una hoja propia en lugar de la del mod: el PNG y su XML (o TXT de Packer) se
// montan en la VFS del mod bajo notelab-import/, sin copiar nada dentro del mod.
void importSheet(NoteLabApp& app, const fs::path& png) {
    if (app.importSource < 0 || app.importSource >= static_cast<int>(app.sources.size())) return;
    Source& source = *app.sources[static_cast<size_t>(app.importSource)];
    if (app.importStyle < 0 || app.importStyle >= static_cast<int>(source.catalog.styles.size())) return;
    if (app.importSheet < 0 || app.importSheet >= static_cast<int>(source.catalog.styles[static_cast<size_t>(app.importStyle)].sheets.size())) return;
    fs::path atlas = png;
    atlas.replace_extension(".xml");
    std::error_code ec;
    if (!fs::exists(atlas, ec)) {
        atlas.replace_extension(".txt");
        if (!fs::exists(atlas, ec)) {
            setStatus(app, "That PNG has no XML (or TXT) with the same name next to it.",
                      "Ese PNG no tiene al lado un XML (o TXT) con el mismo nombre.");
            return;
        }
    }
    const std::string folder = "notelab-import/" + std::to_string(++source.imports) + "/";
    const std::string imageVirtual = folder + png.filename().u8string();
    const std::string atlasVirtual = folder + atlas.filename().u8string();
    if (!source.vfs->pushFile(png, imageVirtual, "import") || !source.vfs->pushFile(atlas, atlasVirtual, "import")) {
        setStatus(app, "The files could not be mounted.", "No se pudieron montar los archivos.");
        return;
    }
    source.importedFiles.push_back({png.u8string(), atlas.u8string(), imageVirtual, atlasVirtual});
    if (app.selSource != app.importSource || app.selStyle != app.importStyle) selectStyle(app, app.importSource, app.importStyle);
    beginEdit(app);
    Sheet& sheet = source.catalog.styles[static_cast<size_t>(app.importStyle)].sheets[static_cast<size_t>(app.importSheet)];
    sheet.image = imageVirtual;
    sheet.atlas = atlasVirtual;
    sheet.kind = lowerText(atlas.extension().u8string()) == ".txt" ? SheetKind::Packer : SheetKind::Sparrow;
    sheet.declared = png.stem().u8string();
    afterEdit(app);
    app.preview.clearCache();
    setStatus(app, "Sheet imported: pick each piece's animation in the Assets tab or in the piece editor.",
              "Hoja importada: elige la animación de cada pieza en la pestaña Assets o en el editor de pieza.");
}

void SDLCALL dialogSelected(void* userdata, const char* const* filelist, int) {
    auto* app = static_cast<NoteLabApp*>(userdata);
    std::lock_guard<std::mutex> lock(app->dialogMutex);
    app->dialogPath = filelist && filelist[0] ? filelist[0] : "";
    app->dialogPaths.clear();
    for (int i = 0; filelist && filelist[i]; ++i) app->dialogPaths.push_back(filelist[i]);
    app->dialogReady = true;
}

void openDialog(NoteLabApp& app, SDL_Window* window, DialogAction action) {
    {
        std::lock_guard<std::mutex> lock(app.dialogMutex);
        app.dialogAction = action;
    }
    if (action == DialogAction::AddModZip) {
        static const SDL_DialogFileFilter filters[] = {{"ZIP", "zip"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::ImportSheet) {
        static const SDL_DialogFileFilter filters[] = {{"PNG", "png"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::ImportHudImage) {
        static const SDL_DialogFileFilter filters[] = {{"PNG", "png"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::ImportHudSound) {
        static const SDL_DialogFileFilter filters[] = {{"Audio", "ogg;mp3;wav"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::LookFiles) {
        static const SDL_DialogFileFilter filters[] = {{"PNG, XML, TXT, GIF", "png;xml;txt;gif"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, true);
    } else if (action == DialogAction::ModMediaFiles) {
        static const SDL_DialogFileFilter filters[] = {{"Images, videos and sounds", "png;jpg;jpeg;bmp;gif;mp4;webm;ogv;wmv;avi;mov;mkv;ogg;wav;mp3;flac"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, true);
    } else if (action == DialogAction::CustomFiles) {
        static const SDL_DialogFileFilter filters[] = {{"Images, atlases and audio", "png;xml;txt;gif;ogg;wav;mp3"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, true);
    } else if (action == DialogAction::CustomAtlas) {
        static const SDL_DialogFileFilter filters[] = {{"Sparrow XML / Packer TXT", "xml;txt"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::RatingFont) {
        static const SDL_DialogFileFilter filters[] = {{"TrueType / OpenType", "ttf;otf"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::OpenProject) {
        static const SDL_DialogFileFilter filters[] = {{"Note Lab", "fmlnote"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::SaveProject) {
        static const SDL_DialogFileFilter filters[] = {{"Note Lab", "fmlnote"}};
        const std::string start = app.projectPath.empty() ? std::string() : app.projectPath.u8string();
        SDL_ShowSaveFileDialog(dialogSelected, &app, window, filters, 1, start.empty() ? nullptr : start.c_str());
    } else if (action == DialogAction::SpriteSheetFile) {
        static const SDL_DialogFileFilter filters[] = {{"Note Lab drawing, PNG, XML, TXT, GIF", "nlsprite;png;xml;txt;gif"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::SaveSpriteFile) {
        static const SDL_DialogFileFilter filters[] = {{"Note Lab drawing (*.nlsprite)", "nlsprite"}};
        SDL_ShowSaveFileDialog(dialogSelected, &app, window, filters, 1, nullptr);
    } else if (action == DialogAction::SaveStyleSheet || action == DialogAction::SaveFinalSheet) {
        static const SDL_DialogFileFilter filters[] = {{"PNG + XML (Sparrow)", "png"}};
        SDL_ShowSaveFileDialog(dialogSelected, &app, window, filters, 1, nullptr);
    } else if (action == DialogAction::SaveProgram) {
        static const SDL_DialogFileFilter filters[] = {{"Note Lab blocks (*.nlblocks)", "nlblocks"}};
        SDL_ShowSaveFileDialog(dialogSelected, &app, window, filters, 1, nullptr);
    } else if (action == DialogAction::OpenProgram) {
        static const SDL_DialogFileFilter filters[] = {{"Note Lab blocks (*.nlblocks)", "nlblocks"}};
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
    } else if (action == DialogAction::SaveChartCopy) {
        static const SDL_DialogFileFilter filters[] = {{"Chart JSON", "json"}};
        SDL_ShowSaveFileDialog(dialogSelected, &app, window, filters, 1, nullptr);
    } else if (action == DialogAction::ExportFolder) {
        std::error_code ec;
        const std::string start = fs::is_directory(app.exporting.folder, ec) ? app.exporting.folder.u8string() : std::string();
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, start.empty() ? nullptr : start.c_str(), false);
    } else {
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
}

NoteProject projectFromApp(NoteLabApp& app) {
    NoteProject project;
    project.engineChoice = app.engineChoice;
    for (size_t e = 0; e < app.baseRoots.size(); ++e) {
        project.bases[e] = app.baseRoots[e].u8string();
        if (project.baseRoot.empty()) project.baseRoot = project.bases[e];   // para los Note Lab de antes
    }
    for (const auto& source : app.sources) {
        ProjectSource entry;
        entry.path = source->root.u8string();
        entry.mod = source->modName;
        entry.baseOnly = source->baseOnly;
        for (const auto& look : source->typeLooks) entry.typeLooks.push_back(look);
        entry.imports = source->importedFiles;
        for (const auto& recipe : source->recipes) entry.recipes.push_back(recipe.second);
        for (const auto& bot : source->bots) entry.bots.push_back(bot);
        entry.edits = editsOf(*source);
        for (const auto& type : source->typeBlocks)
            if (!type.second.empty()) entry.typeBlocks.push_back({type.first, type.second});
        project.sources.push_back(std::move(entry));
    }
    project.selectedSource = app.selSource;
    if (const NoteStyle* style = selectedStyle(app)) project.selectedStyle = style->id;
    if (const Source* source = selectedSource(app))
        if (app.selType >= 0 && app.selType < static_cast<int>(source->noteTypes.size()))
            project.selectedType = source->noteTypes[static_cast<size_t>(app.selType)].name;
    if (app.songSource >= 0 && app.songSource < static_cast<int>(app.sources.size()) && app.songIndex >= 0) {
        const Source& source = *app.sources[static_cast<size_t>(app.songSource)];
        if (app.songIndex < static_cast<int>(source.songs.size())) {
            const SongChart& song = source.songs[static_cast<size_t>(app.songIndex)];
            project.songSource = app.songSource;
            project.songId = song.id;
            project.songDifficulty = song.difficulty;
            project.songVariation = song.variation;
        }
    }
    ProjectView& view = project.view;
    view.visibleLines = app.visibleLines;
    view.playSide = app.playSide;
    view.manual = app.manual;
    view.downscroll = app.settings.downscroll;
    view.chartSpeed = app.chartSpeed;
    view.chartSkin = app.chartSkin;
    view.psychColors = app.psychColors;
    view.scrollSpeed = app.settings.scrollSpeed;
    view.bpm = app.bpm;
    view.judgement = app.settings.judgement;
    view.combo = app.settings.combo;
    view.score = app.score;
    project.distribute = app.distribute;
    project.distributed = app.distributed;
    return project;
}

void rememberProject(NoteLabApp& app, const fs::path& path) {
    const std::string text = path.u8string();
    app.recentProjects.erase(std::remove(app.recentProjects.begin(), app.recentProjects.end(), text), app.recentProjects.end());
    app.recentProjects.insert(app.recentProjects.begin(), text);
    if (app.recentProjects.size() > 8) app.recentProjects.resize(8);
    saveSettings(app);
}

// Se escribe a un temporal y se renombra: un corte a medias no deja el
// proyecto roto.
bool saveProjectTo(NoteLabApp& app, fs::path path) {
    if (lowerText(path.extension().u8string()) != ".fmlnote") path += ".fmlnote";
    std::string error;
    if (!writeProjectFile(projectFromApp(app), path, error)) {
        setStatus(app, "The project could not be saved: " + error, "No se pudo guardar el proyecto: " + error);
        return false;
    }
    app.projectPath = path;
    app.dirty = false;
    rememberProject(app, path);
    setStatus(app, "Project saved: " + path.filename().u8string(), "Proyecto guardado: " + path.filename().u8string());
    return true;
}

void saveProject(NoteLabApp& app, SDL_Window* window) {
    if (app.projectPath.empty()) openDialog(app, window, DialogAction::SaveProject);
    else saveProjectTo(app, app.projectPath);
}

void closeEverything(NoteLabApp& app) {
    clearMediaPreview(app);
    app.media.source = -1; app.media.requestOpen = false; app.media.importRequestOpen = false;
    app.media.importFiles.clear(); app.media.resources.clear(); app.media.usedPaths.clear();
    app.media.selected.clear(); app.resourceSidebar.selected.clear();
    chooseDemo(app);
    activateRenderer(app, -1);
    app.sources.clear();
    for (fs::path& base : app.baseRoots) base.clear();
    app.selSource = -1;
    app.selStyle = -1;
    app.selType = -1;
    app.undo.clear();
    app.redo.clear();
    app.codeKey.clear();
    app.codeLines.clear();
    app.blockCodeOwner.clear();
    app.blockCodeBuffer.clear();
    app.projectPath.clear();
    app.dirty = false;
}

bool openProjectFile(NoteLabApp& app, const fs::path& path) {
    std::error_code projectError;
    const auto projectSize = fs::file_size(path, projectError);
    if (projectError || projectSize > kProjectByteLimit) {
        setStatus(app, "Cannot open the project: missing file or size exceeds 16 MiB. Your current work is unchanged.",
                      "No se puede abrir el proyecto: falta el archivo o supera 16 MiB. Tu trabajo actual no cambia.");
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setStatus(app, "The project could not be read: " + path.u8string(), "No se pudo leer el proyecto: " + path.u8string());
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string error;
    const std::optional<NoteProject> project = readProject(text, error);
    if (!project) {
        setStatus(app, "That is not a Note Lab project (" + error + ").", "Eso no es un proyecto de Note Lab (" + error + ").");
        return false;
    }
    for (const ProjectSource& source : project->sources) {
        std::error_code ec;
        if (!fs::exists(pathFromUtf8(source.path), ec) || ec) {
            setStatus(app, "A source is missing: " + source.path + ". The current project has been kept; reconnect the source before opening.",
                          "Falta una fuente: " + source.path + ". Se conserva el proyecto actual; reconecta la fuente antes de abrir.");
            return false;
        }
    }
    std::vector<Issue> issues;
    auto prepared = std::make_unique<NoteLabApp>();
    prepared->transient = true;
    prepared->headless = true;
    prepared->autoBase = app.autoBase;
    prepared->engineChoice = project->engineChoice;
    prepared->knownInstalls = app.knownInstalls;
    // Los juegos base fijados, por motor; un proyecto de antes trae uno solo.
    std::array<std::string, 3> bases = project->bases;
    const bool legacyBase = std::all_of(bases.begin(), bases.end(), [](const std::string& b) { return b.empty(); }) &&
                            !project->baseRoot.empty();
    for (size_t e = 0; e < bases.size() + (legacyBase ? 1 : 0); ++e) {
        const std::string& saved = e < bases.size() ? bases[e] : project->baseRoot;
        if (saved.empty()) continue;
        const fs::path base = pathFromUtf8(saved);
        std::error_code ec;
        Engine engine = static_cast<Engine>(e < bases.size() ? e : 0);
        if (!fs::exists(base, ec)) {
            issues.push_back({"The base game is no longer at " + saved + ".", "El juego base ya no está en " + saved + "."});
        } else if (e < bases.size() || baseEngineOf(base, engine)) {
            prepared->baseRoots[static_cast<size_t>(engine)] = base;
        }
    }
    std::vector<int> sourceOf;
    for (const ProjectSource& saved : project->sources) {
        const fs::path root = pathFromUtf8(saved.path);
        std::error_code ec;
        if (!fs::exists(root, ec)) {
            setStatus(app, "A source disappeared while opening the project. Your current work is unchanged.",
                          "Una fuente desapareció al abrir el proyecto. Tu trabajo actual no cambia.");
            return false;
        }
        auto stagedSource = std::make_unique<Source>();
        stagedSource->root = root;
        stagedSource->modName = saved.mod;
        stagedSource->baseOnly = saved.baseOnly;
        if (!loadSource(*prepared, *stagedSource)) {
            setStatus(app, "Project not opened: " + prepared->statusEn + " Your current work is unchanged.",
                          "Proyecto no abierto: " + prepared->statusEs + " Tu trabajo actual no cambia.");
            return false;
        }
        Source& source = *stagedSource;
        source.importedFiles = saved.imports;
        remountImports(source, issues);
        applyEdits(*prepared, source, saved.edits, issues);
        for (const ProjectTypeBlocks& type : saved.typeBlocks) source.typeBlocks[type.type] = type.program;
        for (const auto& look : saved.typeLooks) source.typeLooks[look.first] = look.second;
        for (const CreationRecipe& recipe : saved.recipes) source.recipes[recipe.kind + ":" + recipe.styleId] = recipe;
        for (const auto& bot : saved.bots) source.bots[lowerText(bot.first)] = bot.second;
        applyTypeLooks(source);
        prepared->sources.push_back(std::move(stagedSource));
        sourceOf.push_back(static_cast<int>(prepared->sources.size()) - 1);
    }
    closeEverything(app);
    app.engineChoice = prepared->engineChoice;
    app.baseRoots = prepared->baseRoots;
    app.knownInstalls = std::move(prepared->knownInstalls);
    app.sources = std::move(prepared->sources);
    for (size_t index = 0; index < app.sources.size(); ++index) {
        const int style = firstVisibleStyle(app, *app.sources[index]);
        if (style >= 0) { selectStyle(app, static_cast<int>(index), style); break; }
    }
    const ProjectView& view = project->view;
    app.visibleLines = view.visibleLines;
    app.playSide = view.playSide;
    app.manual = view.manual;
    app.settings.downscroll = view.downscroll;
    app.chartSpeed = view.chartSpeed;
    app.chartSkin = view.chartSkin;
    app.psychColors = view.psychColors;
    app.settings.scrollSpeed = view.scrollSpeed;
    app.bpm = view.bpm;
    app.settings.judgement = view.judgement;
    app.settings.combo = view.combo;
    app.score = view.score;
    app.distribute = project->distribute;
    auto mapped = [&](int saved) { return saved >= 0 && saved < static_cast<int>(sourceOf.size()) ? sourceOf[static_cast<size_t>(saved)] : -1; };
    if (const int songSource = mapped(project->songSource); songSource >= 0) {
        const Source& source = *app.sources[static_cast<size_t>(songSource)];
        for (size_t i = 0; i < source.songs.size(); ++i)
            if (source.songs[i].id == project->songId && source.songs[i].difficulty == project->songDifficulty &&
                source.songs[i].variation == project->songVariation) {
                chooseSong(app, songSource, static_cast<int>(i));
                if (project->distributed) applyDistribution(app);
                break;
            }
    } else {
        chooseDemo(app);
    }
    if (const int selected = mapped(project->selectedSource); selected >= 0) {
        const Source& source = *app.sources[static_cast<size_t>(selected)];
        for (size_t i = 0; i < source.catalog.styles.size(); ++i)
            if (source.catalog.styles[i].id == project->selectedStyle) {
                if (!styleVisible(app, source, source.catalog.styles[i])) {
                    app.showOtherEngines = true;
                    app.showUnused = true;
                }
                selectStyle(app, selected, static_cast<int>(i));
                break;
            }
        for (size_t i = 0; i < source.noteTypes.size(); ++i)
            if (source.noteTypes[i].name == project->selectedType) {
                app.typeSource = selected;
                app.selType = static_cast<int>(i);
                break;
            }
    }
    app.undo.clear();
    app.redo.clear();
    app.projectPath = path;
    app.dirty = false;
    rememberProject(app, path);
    setStatus(app, "Project opened: " + path.filename().u8string(), "Proyecto abierto: " + path.filename().u8string());
    showIssues(app, std::move(issues));
    return true;
}

void performPending(NoteLabApp& app, SDL_Window* window) {
    const PendingAction action = app.pending;
    app.pending = PendingAction::None;
    switch (action) {
        case PendingAction::Quit: app.quit = true; break;
        case PendingAction::NewProject:
            closeEverything(app);
            setStatus(app, "New project: open a mod to start.", "Proyecto nuevo: abre un mod para empezar.");
            break;
        case PendingAction::OpenProject: openDialog(app, window, DialogAction::OpenProject); break;
        case PendingAction::OpenRecent: openProjectFile(app, app.pendingPath); break;
        case PendingAction::None: break;
    }
}

// Lo que cierra el proyecto actual pregunta antes si hay cambios sin guardar.
void requestAction(NoteLabApp& app, SDL_Window* window, PendingAction action, const fs::path& path = {}) {
    app.pending = action;
    app.pendingPath = path;
    if (app.dirty && app.capturePath.empty()) {
        app.openUnsaved = true;
        return;
    }
    performPending(app, window);
}

void processDialog(NoteLabApp& app, SDL_Window* window) {
    std::string path;
    std::vector<std::string> paths;
    DialogAction action = DialogAction::None;
    {
        std::lock_guard<std::mutex> lock(app.dialogMutex);
        if (!app.dialogReady) return;
        path = app.dialogPath;
        paths = app.dialogPaths;
        action = app.dialogAction;
        app.dialogReady = false;
        app.dialogAction = DialogAction::None;
    }
    if (path.empty()) {
        // Cancelado: si iba a guardar antes de otra cosa, esa otra cosa tampoco se hace.
        if (action == DialogAction::SaveProject) {
            app.saveThenContinue = false;
            app.pending = PendingAction::None;
        }
        return;
    }
    const fs::path chosen = pathFromUtf8(path);
    if (action == DialogAction::ExportFolder) {
        app.exporting.folder = chosen;
        app.exporting.written.clear();
        app.exporting.error.clear();
        return;
    }
    if (action == DialogAction::SaveChartCopy) {
        fs::path target = chosen;
        if (target.extension().empty()) target += L".json";
        std::error_code ec;
        const bool same = !app.chartSave.original.empty() && fs::exists(target, ec) && fs::equivalent(target, app.chartSave.original, ec);
        commitChartSave(app, target, same);
        return;
    }
    if (action == DialogAction::ImportHudImage || action == DialogAction::ImportHudSound) {
        importHudFile(app, chosen, action == DialogAction::ImportHudSound);
        return;
    }
    if (action == DialogAction::ModMediaFiles) {
        std::vector<fs::path> files;
        for (const auto& path : paths) files.push_back(pathFromUtf8(path));
        if (files.empty()) files.push_back(chosen);
        prepareMediaImport(app, files);
        return;
    }
    if (action == DialogAction::CustomAtlas) {
        auto& c = app.custom;
        if (c.resource >= 0 && c.resource < static_cast<int>(c.recipe.resources.size()) &&
            bindCustomAtlas(c.recipe.resources[static_cast<size_t>(c.resource)], chosen, c.message)) {
            c.animation = 0; c.assignment = -1; c.inspectDirty = true; c.fit = true;
            c.draft.order.clear(); c.order.fill(0);
        }
        return;
    }
    if (action == DialogAction::CustomFiles || action == DialogAction::CustomFolder) {
        std::vector<fs::path> files;
        for (const std::string& one : paths) files.push_back(pathFromUtf8(one));
        if (files.empty()) files.push_back(chosen);
        addCustomFiles(app, files);
        return;
    }
    if (action == DialogAction::SpriteSheetFile) {
        loadSpriteSheetFile(app, chosen);
        return;
    }
    if (action == DialogAction::SaveStyleSheet) {
        saveStyleSheetTo(app, chosen);
        return;
    }
    if (action == DialogAction::SaveFinalSheet) {
        saveFinalSheetTo(app, chosen);
        return;
    }
    if (action == DialogAction::SaveProgram) {
        saveProgramTo(app, chosen);
        return;
    }
    if (action == DialogAction::SaveSpriteFile) {
        saveSpriteFileTo(app, chosen);
        return;
    }
    if (action == DialogAction::OpenProgram) {
        openProgramFrom(app, chosen);
        return;
    }
    if (action == DialogAction::SaveCodeFolder) {
        saveCodeTo(app, chosen);
        return;
    }
    if (action == DialogAction::LookFiles || action == DialogAction::LookFolder) {
        std::vector<fs::path> files;
        for (const std::string& one : paths) files.push_back(pathFromUtf8(one));
        if (files.empty()) files.push_back(chosen);
        addLookFiles(app, files);
        return;
    }
    if (action == DialogAction::RatingFont) {
        auto& r = app.createRating;
        r.fontPath = chosen.u8string();
        std::ifstream in(chosen, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string family = fontFamilyName(bytes);
        r.fonts.insert(r.fonts.begin(), {family.empty() ? chosen.stem().u8string() : family, r.fontPath});
        r.generation++;
        r.changedAt = -10.0;
        return;
    }
    if (action == DialogAction::OpenProject) {
        openProjectFile(app, chosen);
        return;
    }
    if (action == DialogAction::SaveProject) {
        const bool saved = saveProjectTo(app, chosen);
        if (app.saveThenContinue) {
            app.saveThenContinue = false;
            if (saved) performPending(app, window);
            else app.pending = PendingAction::None;
        }
        return;
    }
    if (action == DialogAction::SetBaseFolder) {
        setBaseFolder(app, chosen);
    } else if (action == DialogAction::ImportSheet) {
        importSheet(app, chosen);
    } else {
        addSource(app, chosen);
    }
}

// ------------------------------------------------------------- vista previa --

void pressJudged(NoteLabApp& app, int lane) {
    const NoteStyle* style = selectedStyle(app);
    if (!style) return;
    const PressResult result = pressLane(app.state, *style, app.notes, app.playSide, lane, 166.0);
    if (!result.hit) return;
    // Un tipo que se toca como fallo (la Hurt Note: PlayState.hx:3042): fallo,
    // sin juicio, y la salpicadura de su tipo (PlayState.hx:3116).
    if (result.note >= 0 && app.notes[static_cast<size_t>(result.note)].hitMisses) {
        app.lastJudgement = Judgement::Miss;
        ++app.counts[static_cast<size_t>(Judgement::Miss)];
        app.state.combo = 0;
        return;
    }
    const double off = std::fabs(result.offsetMs);
    // Ventanas por defecto de Psych (ClientPrefs.hx:72-74); el ranking de cada
    // motor llega en una actualizacion (DESIGN_PLUGIN_NOTE_LAB §25).
    const Judgement judgement = off <= 45.0 ? Judgement::Sick : off <= 90.0 ? Judgement::Good
                              : off <= 135.0 ? Judgement::Bad : Judgement::Shit;
    app.lastJudgement = judgement;
    ++app.counts[static_cast<size_t>(judgement)];
    // La salpicadura sale con el sick, como en los tres motores.
    if (judgement != Judgement::Sick && !app.state.splashes.empty()) app.state.splashes.pop_back();
    ++app.state.combo;
    app.maxCombo = std::max(app.maxCombo, app.state.combo);
    app.state.popups.push_back({static_cast<int>(judgement) - 1, app.state.combo, app.state.songMs});
}

void updatePlayback(NoteLabApp& app, float deltaMs) {
    const NoteStyle* style = selectedStyle(app);
    if (!style) return;
    pollAudio(app);
    app.settings.showOpponent = app.visibleLines != 1;
    app.settings.showPlayer = app.visibleLines != 0;
    // Empezar o dejar de jugar y cambiar de lado es otra pasada.
    const int run = (app.manual ? 2 : 0) + app.playSide;
    if (run != app.scoreRun) {
        app.scoreRun = run;
        resetScore(app);
    }
    if (app.manual && app.finished && !app.playing) return;
    double from = app.state.songMs;
    if (nlbuild::audioPlayback && app.audioLoaded) {
        if (app.playing && !app.audio.playing()) {
            const double duration = app.audio.durationMs();
            if (duration > 0.0 && app.audio.positionMs() >= duration - 1.0) {
                if (app.manual) {
                    app.state.songMs = duration + app.audioOffsetMs;
                    app.finished = true;
                    app.playing = false;
                    return;
                }
                resetPlayback(app);
                from = -1.0;
            }
            app.audio.play();
        }
        if (!app.playing && app.audio.playing()) app.audio.pause();
        app.audio.setMasterGain(app.musicVolume);
        app.state.songMs = app.audio.positionMs() + app.audioOffsetMs;
    } else if (app.playing && !app.audioPending) {
        app.state.songMs += deltaMs;
    }
    // Solo el final reinicia. Justo despues de un salto el audio aun puede
    // informar la posicion anterior: tomarlo por una vuelta al principio
    // devolvia la cancion al 0 en cada salto.
    if (app.state.songMs >= app.patternMs) {
        // Jugando, la pasada acaba aqui: se para con el resultado a la vista y
        // reproducir empieza otra. Sin jugar, vuelve a empezar sola.
        if (app.manual) {
            if (!app.finished) {
                app.finished = true;
                app.playing = false;
                if (app.audioLoaded) app.audio.pause();
            }
            return;
        }
        resetPlayback(app);
        return;
    }
    std::array<bool, 2> autoSide{true, true};
    if (app.manual) autoSide[static_cast<size_t>(app.playSide)] = false;
    app.state.playerLine = app.playSide;
    // La velocidad del chart en este momento, con sus cambios, en cada linea.
    if (app.hasSongValues && app.chartSpeed)
        app.settings.lineSpeed = {speedAt(app.songValues, app.state.songMs, 0), speedAt(app.songValues, app.state.songMs, 1)};
    else
        app.settings.lineSpeed = {0.0f, 0.0f};
    const double splashMs = app.preview.animationLengthMs(*style, app.atlases, Part::Splash, 0, 0);
    advanceAutoplay(app.state, *style, app.notes, from, app.state.songMs, autoSide, splashMs);
    if (!app.manual) return;
    if (!ImGui::GetIO().WantTextInput && app.bindingLane < 0) {
        static const std::array<ImGuiKey, 4> arrows{ImGuiKey_LeftArrow, ImGuiKey_DownArrow, ImGuiKey_UpArrow, ImGuiKey_RightArrow};
        for (int lane = 0; lane < 4; ++lane) {
            const bool down = ImGui::IsKeyPressed(app.keys[static_cast<size_t>(lane)], false) ||
                              (app.arrowsToo && ImGui::IsKeyPressed(arrows[static_cast<size_t>(lane)], false));
            const bool up = ImGui::IsKeyReleased(app.keys[static_cast<size_t>(lane)]) ||
                            (app.arrowsToo && ImGui::IsKeyReleased(arrows[static_cast<size_t>(lane)]));
            if (down) pressJudged(app, lane);
            if (up) releaseLane(app.state, app.playSide, lane);
        }
    }
    for (size_t i = 0; i < app.notes.size(); ++i) {
        const PreviewNote& note = app.notes[i];
        // Lo que el bot deja pasar tampoco es fallo si pasa (Psych ignoreNote,
        // PlayState.hx:1833).
        if (note.strumLine != app.playSide || app.state.hit[i] || app.missed[i] || note.botSkips) continue;
        if (app.state.songMs - note.timeMs > 166.0) {
            app.missed[i] = 1;
            app.lastJudgement = Judgement::Miss;
            ++app.counts[static_cast<size_t>(Judgement::Miss)];
            app.state.combo = 0;
        }
    }
}

// Estilo de un tipo de nota custom del chart, dentro del mod que se dibuja,
// por el catalogo de tipos: Codename game/notes/<tipo> (Note.hx:156-158), la
// textura del tipo en Psych y el `noteStyleId` del NoteKind en V-Slice. Un
// tipo de Psych que recolorea el skin (la Hurt Note) se aplica sobre el skin
// elegido, que es el que el motor recolorearia.
const NoteStyle* styleForType(NoteLabApp& app, const std::string& type) {
    if (type.empty() || app.renderSource < 0 || app.renderSource >= static_cast<int>(app.sources.size())) return nullptr;
    const Source& source = *app.sources[static_cast<size_t>(app.renderSource)];
    const std::string key = lowerText(type);
    Engine engine;
    const bool known = sourceEngine(app, source, engine);
    const NoteTypeEntry* entry = known ? findNoteType(source.noteTypes, type, engine) : findNoteType(source.noteTypes, type);
    const NoteStyle* look = nullptr;
    for (const auto& own : source.typeLooks)
        if (lowerText(own.first) == key)
            for (const NoteStyle& style : source.catalog.styles)
                if (style.id == own.second) { look = &style; break; }
    if (!look && entry && !entry->lookStyle.empty())
        for (const NoteStyle& style : source.catalog.styles)
            if (style.id == entry->lookStyle) { look = &style; break; }
    if (!look)
        for (const NoteStyle& style : source.catalog.styles)
            if (style.use == StyleUse::NoteType && lowerText(style.useDetail) == key && (!known || style.engine == engine)) {
                look = &style;
                break;
            }
    const NoteStyle* skin = selectedStyle(app);
    if (look && skin && recolorsSkin(*look) && skin->rgbPalette) {
        NoteStyle& derived = app.derivedLooks[key];
        derived = recolorOver(*skin, *look);
        return &derived;
    }
    return look;
}

GlRenderer::PreviewImage renderPreview(NoteLabApp& app, int width, int height) {
    const NoteStyle* style = selectedStyle(app);
    if (!style || !app.rendererReady || app.renderSource != app.selSource || width < 8 || height < 8) return {};
    app.preview.textureFor = [&app](const NoteStyle& owner, const Sheet& sheet, Part part, int lane) {
        return psychRgbTexture(app, owner, sheet, part, lane);
    };
    std::map<std::string, const NoteStyle*> typeStyles;
    app.derivedLooks.clear();
    for (const std::string& type : app.noteTypes)
        if (!type.empty() && !typeStyles.count(type)) typeStyles[type] = styleForType(app, type);
    app.preview.styleForNote = [&app, &typeStyles](size_t note) -> const NoteStyle* {
        if (note >= app.noteTypes.size() || app.noteTypes[note].empty()) return nullptr;
        const auto found = typeStyles.find(app.noteTypes[note]);
        return found == typeStyles.end() ? nullptr : found->second;
    };
    // Las salpicaduras que pide la cancion, si son de este mod.
    app.preview.splashStyle = nullptr;
    app.preview.receptorFallback = nullptr;
    for (const NoteStyle& candidate : app.sources[static_cast<size_t>(app.selSource)]->catalog.styles)
        if (candidate.engine == style->engine && candidate.use == StyleUse::Default) {
            app.preview.receptorFallback = &candidate;
            break;
        }
    // Los receptores de otro estilo del mod, solo para verlos (no cambia nada).
    app.preview.receptorStyle = nullptr;
    if (!app.previewReceptors.empty())
        for (const NoteStyle& candidate : app.sources[static_cast<size_t>(app.selSource)]->catalog.styles)
            if (candidate.id == app.previewReceptors && &candidate != style) {
                app.preview.receptorStyle = &candidate;
                break;
            }
    if (app.songSplash >= 0 && app.songSource == app.renderSource) {
        const Source& songSource = *app.sources[static_cast<size_t>(app.renderSource)];
        if (app.songSplash < static_cast<int>(songSource.catalog.styles.size()))
            app.preview.splashStyle = &songSource.catalog.styles[static_cast<size_t>(app.songSplash)];
    }
    RenderList list;
    app.preview.build(*style, app.atlases, &app.renderer, app.settings, app.state, app.notes, list);
    app.preview.styleForNote = nullptr;
    app.preview.splashStyle = nullptr;
    app.preview.receptorFallback = nullptr;
    app.preview.receptorStyle = nullptr;
    if (!app.renderer.beginOffscreenFrame(width, height)) return {};
    glClearColor(0.055f, 0.06f, 0.075f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ViewportTransform view;
    app.renderer.draw(list, view, width, height, true, false);
    return app.renderer.finishOffscreenPreview();
}

const char* judgementLabel(const NoteLabApp& app, Judgement judgement) {
    switch (judgement) {
        case Judgement::Sick: return "Sick!";
        case Judgement::Good: return tr(app, "Good", "Bien");
        case Judgement::Bad: return tr(app, "Bad", "Mal");
        case Judgement::Shit: return tr(app, "Shit", "Pésimo");
        case Judgement::Miss: return tr(app, "Miss", "Fallo");
        default: return "";
    }
}

// ---------------------------------------------------------------- paneles --

ImU32 engineColor(Engine engine) {
    switch (engine) {
        case Engine::Codename: return ui::color::Codename;
        case Engine::Psych: return ui::color::Psych;
        case Engine::VSlice: return ui::color::VSlice;
    }
    return ui::color::Muted;
}

const char* useIcon(StyleUse use) {
    switch (use) {
        case StyleUse::Default: return ui::icon::Home;
        case StyleUse::NoteType: return ui::icon::Puzzle;
        case StyleUse::Song: return ui::icon::Music;
        case StyleUse::PlayerChoice: return ui::icon::Contact;
        case StyleUse::Declared: return ui::icon::Document;
    }
    return ui::icon::Document;
}

const char* severityIcon(Severity severity) {
    return severity == Severity::Error ? ui::icon::Error : severity == Severity::Warning ? ui::icon::Warning : ui::icon::Info;
}

ImU32 severityColor(Severity severity) {
    return severity == Severity::Error ? ui::color::Error : severity == Severity::Warning ? ui::color::Warning : ui::color::Info;
}

int countSeverity(const NoteLabApp& app, Severity severity) {
    int total = 0;
    for (const auto& source : app.sources) {
        for (const Finding& f : source->catalog.findings) if (f.severity == severity) ++total;
        for (size_t i = 0; i < source->reports.size(); ++i) {
            if (!styleVisible(app, *source, source->catalog.styles[i])) continue;
            for (const Finding& f : source->reports[i].findings) if (f.severity == severity) ++total;
        }
    }
    return total;
}

std::string detectedText(const NoteLabApp& app) {
    std::string text;
    for (const auto& source : app.sources) {
        if (!text.empty()) text += ", ";
        text += source->guess.found && source->guess.other.empty() ? std::string(engineLabel(source->guess.engine))
                                                                   : std::string("?");
    }
    return text;
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Faint));
    ImGui::TextUnformatted(ui::fonts().icons ? ui::icon::Info : "(?)");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
}

// ------------------------------------------------------ menus contextuales --

bool menuItem(const char* glyph, const char* text, const char* shortcut = nullptr, bool selected = false, bool enabled = true) {
    return ImGui::MenuItem(ui::label(glyph, text).c_str(), shortcut, selected, enabled);
}

void copyText(NoteLabApp& app, const std::string& text) {
    ImGui::SetClipboardText(text.c_str());
    setStatus(app, "Copied: " + text, "Copiado: " + text);
}

#ifdef _WIN32
void revealInExplorer(const fs::path& path) {
    const std::wstring arguments = L"/select,\"" + path.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

void openFolder(const fs::path& folder) {
    const std::wstring arguments = L"\"" + folder.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}
#endif

// Un archivo de lo montado, en su carpeta del Explorador (si esta en disco y
// no dentro de un ZIP).
void revealVirtual(NoteLabApp& app, const Source& source, const std::string& virtualPath) {
#ifdef _WIN32
    if (const auto real = source.vfs->resolve(virtualPath)) {
        revealInExplorer(*real);
        return;
    }
#endif
    setStatus(app, "That file is not a file on disk (inside a ZIP, or built into the engine).",
              "Eso no es un archivo en disco (va dentro de un ZIP o es de serie del motor).");
}

void showSourceInFolder(NoteLabApp& app, const Source& source) {
#ifdef _WIN32
    std::error_code ec;
    if (fs::is_directory(source.root, ec)) openFolder(source.root);
    else revealInExplorer(source.root);
#else
    (void)source;
    setStatus(app, "Not available on this system.", "No disponible en este sistema.");
#endif
}

// Vuelve a leer un mod (lo cambiado fuera de Note Lab) sin tocar los demas.
void reloadSource(NoteLabApp& app, int index) {
    if (index < 0 || index >= static_cast<int>(app.sources.size())) return;
    if (app.songSource == index) chooseDemo(app);
    if (app.renderSource == index) activateRenderer(app, -1);
    app.undo.clear();
    app.redo.clear();
    Source& source = *app.sources[static_cast<size_t>(index)];
    std::vector<Issue> issues;
    reloadKeepingEdits(app, source, issues);
    showIssues(app, std::move(issues));
    if (app.selSource == index) {
        app.selStyle = -1;
        app.selType = -1;
        const int first = firstVisibleStyle(app, source);
        if (first >= 0) selectStyle(app, index, first);
    }
    describeLoaded(app, source);
}

// Cambiar el motor cambia que es «de otro motor»: se revalida todo.
void engineChoiceChanged(NoteLabApp& app) {
    app.dirty = true;
    for (auto& source : app.sources) recheck(app, *source);
    if (Source* source = selectedSource(app)) {
        const NoteStyle* style = selectedStyle(app);
        if (!style || !styleVisible(app, *source, *style)) {
            const int first = firstVisibleStyle(app, *source);
            if (first >= 0) selectStyle(app, app.selSource, first);
        }
    }
    app.selType = -1;
}

// Desde el principio, con el marcador a cero (seekTo -> resetPlayback).
void restartPreview(NoteLabApp& app) {
    seekTo(app, 0.0);
}

// Reproducir o pausar. Despues del final de una pasada jugada, reproducir es
// otra pasada desde el principio.
void togglePlay(NoteLabApp& app) {
    if (!app.playing && app.finished) restartPreview(app);
    app.playing = !app.playing;
}

// ------------------------------------------------------------------ menus --

void drawMenuBar(NoteLabApp& app, SDL_Window* window) {
    if (!ImGui::BeginMainMenuBar()) return;
    {
        // Donde esta «Archivo» en la barra, para que el tutorial lo senale.
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::CalcTextSize(tr(app, "File", "Archivo"));
        tutorialMarkRect("menu-file", at, ImVec2(at.x + size.x + ImGui::GetStyle().ItemSpacing.x * 2.0f, at.y + ImGui::GetFrameHeight()));
    }
    if (ImGui::BeginMenu(tr(app, "File", "Archivo"))) {
        if (menuItem(ui::icon::Add, tr(app, "New project", "Nuevo proyecto"), "Ctrl+N")) requestAction(app, window, PendingAction::NewProject);
        if (menuItem(ui::icon::Document, tr(app, "Open project…", "Abrir proyecto…"), "Ctrl+Shift+O"))
            requestAction(app, window, PendingAction::OpenProject);
        if (ImGui::BeginMenu(ui::label(ui::icon::List, tr(app, "Recent projects", "Proyectos recientes")).c_str(), !app.recentProjects.empty())) {
            for (size_t i = 0; i < app.recentProjects.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const fs::path recent = pathFromUtf8(app.recentProjects[i]);
                if (ImGui::MenuItem(recent.filename().u8string().c_str())) requestAction(app, window, PendingAction::OpenRecent, recent);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", app.recentProjects[i].c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (menuItem(ui::icon::Check, tr(app, "Save project", "Guardar proyecto"), "Ctrl+S")) saveProject(app, window);
        if (menuItem(ui::icon::Check, tr(app, "Save project as…", "Guardar proyecto como…"), "Ctrl+Shift+S"))
            openDialog(app, window, DialogAction::SaveProject);
        ImGui::Separator();
        if (menuItem(ui::icon::Zip, tr(app, "Export note style…", "Exportar estilo de notas…"), "Ctrl+E", false, selectedStyle(app) != nullptr))
            openExport(app);
        // El archivo con todos los assets de las notas del estilo: una hoja.
        if (menuItem(ui::icon::Photo, tr(app, "Save note sheet (PNG + XML)…", "Guardar hoja de notas (PNG + XML)…"), nullptr, false, selectedStyle(app) != nullptr))
            openDialog(app, window, DialogAction::SaveStyleSheet);
        ImGui::Separator();
        if (menuItem(ui::icon::Brush, tr(app, "Create notes · Basic…", "Crear notas · Básico…"), nullptr, false, !app.sources.empty())) openCreateHud(app);
        if (menuItem(ui::icon::Layers, tr(app, "Custom creator · Advanced…", "Creador custom · Avanzado…"))) openCustomCreator(app);
        if (menuItem(ui::icon::Star, tr(app, "Create ranking HUD · Images…", "Crear HUD de ranking · Imágenes…"), nullptr, false, selectedStyle(app) != nullptr))
            openCustomCreator(app, true);
        if (kTextRankingVisible &&
            menuItem(ui::icon::Font, tr(app, "Create ranking HUD · Text…", "Crear HUD de ranking · Texto…"), nullptr, false, selectedStyle(app) != nullptr))
            openCreateRating(app);
        ImGui::Separator();
        if (ImGui::MenuItem(ui::label(ui::icon::FolderOpen, tr(app, "Open mod folder…", "Abrir carpeta de mod…")).c_str(), "Ctrl+O"))
            openDialog(app, window, DialogAction::AddModFolder);
        if (ImGui::MenuItem(ui::label(ui::icon::Zip, tr(app, "Open mod ZIP…", "Abrir ZIP de mod…")).c_str()))
            openDialog(app, window, DialogAction::AddModZip);
        ImGui::Separator();
        if (ImGui::MenuItem(ui::label(ui::icon::Game, tr(app, "Base game folder…", "Carpeta del juego base…")).c_str()))
            openDialog(app, window, DialogAction::SetBaseFolder);
        const bool anyBase = std::any_of(app.baseRoots.begin(), app.baseRoots.end(), [](const fs::path& b) { return !b.empty(); });
        if (ImGui::BeginMenu(tr(app, "Remove base game", "Quitar juego base"), anyBase)) {
            for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
                const fs::path& base = app.baseRoots[static_cast<size_t>(engine)];
                if (base.empty()) continue;
                const std::string item = std::string(engineLabel(engine)) + ": " + base.filename().u8string();
                if (ImGui::MenuItem(item.c_str())) removeBase(app, engine);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", base.u8string().c_str());
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr(app, "Find the base game by itself", "Buscar el juego base solo"), nullptr, app.autoBase)) {
            app.autoBase = !app.autoBase;
            reloadAll(app);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tr(app, "For a mod that does not bring its engine's default skin: the install it lives in, one used before or an official one next to it.",
                                            "Para un mod que no trae el skin por defecto de su motor: la instalación en la que está, una ya usada o una oficial junto a él."));
        ImGui::Separator();
        if (ImGui::BeginMenu(tr(app, "Close mod", "Cerrar mod"), !app.sources.empty())) {
            int remove = -1;
            for (size_t i = 0; i < app.sources.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::MenuItem(sourceName(*app.sources[i]).c_str())) remove = static_cast<int>(i);
                ImGui::PopID();
            }
            ImGui::EndMenu();
            if (remove >= 0) requestSourceClose(app, remove);
        }
        if (ImGui::MenuItem(ui::label(ui::icon::Restart, tr(app, "Reload all", "Recargar todo")).c_str(), "F5", false, !app.sources.empty()))
            reloadAll(app);
        ImGui::Separator();
        if (ImGui::MenuItem(tr(app, "Exit", "Salir"), "Alt+F4")) requestAction(app, window, PendingAction::Quit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr(app, "Edit", "Editar"))) {
        if (ImGui::MenuItem(ui::label(ui::icon::Undo, tr(app, "Undo", "Deshacer")).c_str(), "Ctrl+Z", false, !app.undo.empty()))
            swapHistory(app, app.undo, app.redo);
        if (ImGui::MenuItem(ui::label(ui::icon::Redo, tr(app, "Redo", "Rehacer")).c_str(), "Ctrl+Y", false, !app.redo.empty()))
            swapHistory(app, app.redo, app.undo);
        ImGui::Separator();
        const Source* source = selectedSource(app);
        const bool canRevert = source && app.selStyle >= 0 && source->originals.count(static_cast<size_t>(app.selStyle));
        if (ImGui::MenuItem(ui::label(ui::icon::Restart, tr(app, "Back to the mod's version", "Volver a la versión del mod")).c_str(),
                            nullptr, false, canRevert))
            revertStyle(app);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr(app, "View", "Ver"))) {
        if (ImGui::MenuItem(ui::label(ui::icon::Layers, tr(app, "Styles of other engines", "Estilos de otros motores")).c_str(), nullptr,
                            &app.showOtherEngines))
            app.selType = -1;
        ImGui::MenuItem(ui::label(ui::icon::EyeOff, tr(app, "What the engine does not load", "Lo que el motor no carga")).c_str(), nullptr,
                        &app.showUnused);
        if (ImGui::MenuItem(ui::label(ui::icon::Brush, tr(app, "Use the note skin the chart asks for", "Usar el skin que pide el chart")).c_str(),
                            nullptr, &app.chartSkin) && app.chartSkin && app.hasSongValues)
            applyChartSkin(app, app.songSource);
        ImGui::Separator();
        ImGui::MenuItem(ui::label(ui::icon::Warning, tr(app, "Findings", "Hallazgos")).c_str(), nullptr, &app.showFindings);
        ImGui::Separator();
        if (ImGui::BeginMenu(ui::label(ui::icon::Globe, tr(app, "Language", "Idioma")).c_str())) {
            if (ImGui::MenuItem("English", nullptr, !app.spanish)) { app.spanish = false; saveSettings(app); }
            if (ImGui::MenuItem("Español", nullptr, app.spanish)) { app.spanish = true; saveSettings(app); }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr(app, "Preview", "Vista previa"))) {
        if (ImGui::MenuItem(ui::label(app.playing ? ui::icon::Pause : ui::icon::Play,
                                      app.playing ? tr(app, "Pause", "Pausar") : tr(app, "Play", "Reproducir")).c_str(), "Space"))
            togglePlay(app);
        if (ImGui::MenuItem(ui::label(ui::icon::Restart, tr(app, "Restart", "Reiniciar")).c_str(), "R")) restartPreview(app);
        if (ImGui::MenuItem(ui::label(ui::icon::Delete, tr(app, "Reset the scoreboard", "Poner el marcador a cero")).c_str(), nullptr, false,
                            app.manual))
            resetScore(app);
        ImGui::Separator();
        ImGui::MenuItem(ui::label(ui::icon::Game, tr(app, "Play a side with the keyboard", "Jugar un lado con el teclado")).c_str(), nullptr,
                        &app.manual);
        ImGui::MenuItem(ui::label(ui::icon::Down, "Downscroll").c_str(), nullptr, &app.settings.downscroll);
        if (ImGui::MenuItem(ui::label(ui::icon::Palette, tr(app, "Psych colors", "Colores de Psych")).c_str(), nullptr, &app.psychColors))
            saveSettings(app);
        ImGui::Separator();
        if (ImGui::MenuItem(ui::label(ui::icon::Keyboard, tr(app, "Keys…", "Teclas…")).c_str())) app.openKeys = true;
        ImGui::EndMenu();
    }
    drawTutorialMenu(app);
    if (ImGui::BeginMenu(tr(app, "Help", "Ayuda"))) {
        if (ImGui::MenuItem(ui::label(ui::icon::Keyboard, tr(app, "Keyboard shortcuts", "Atajos de teclado")).c_str(), "F1"))
            app.openShortcuts = true;
        if (menuItem(ui::icon::Help, tr(app, "Note Lab help", "Ayuda de Note Lab"), "F1")) openHelp(app, 0);
        if (ImGui::MenuItem(ui::label(ui::icon::Info, tr(app, "About Note Lab", "Acerca de Note Lab")).c_str())) app.openAbout = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// --------------------------------------------------------- barra superior --

void sameLineOrWrap(float nextWidth);

float buttonWidth(const std::string& text) {
    return ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

void drawToolbar(NoteLabApp& app, SDL_Window* window) {
    if (ui::flatButton(ui::label(ui::icon::FolderOpen, tr(app, "Open mod", "Abrir mod")),
                       tr(app, "Open a mod folder (Ctrl+O). You can also drop it on the window.",
                              "Abrir la carpeta de un mod (Ctrl+O). También puedes soltarla en la ventana.")))
        openDialog(app, window, DialogAction::AddModFolder);
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::flatButton(ui::label(ui::icon::Zip, "ZIP"), tr(app, "Open a mod packed as ZIP", "Abrir un mod empaquetado en ZIP")))
        openDialog(app, window, DialogAction::AddModZip);
    ImGui::SameLine(0.0f, 4.0f);
    const std::string baseText = ui::label(ui::icon::Game, tr(app, "Base game", "Juego base"));
    if (ui::flatButton(baseText, tr(app, "The engine install a mod runs on, for what it inherits (default skin, splashes, HUD). "
                                        "It is found by itself; pick a folder to fix it for its engine.",
                                        "La instalación del motor en la que se juega un mod, para lo que hereda (skin por defecto, "
                                        "salpicaduras, HUD). Se busca sola; elige una carpeta para fijarla para su motor.")))
        openDialog(app, window, DialogAction::SetBaseFolder);
    // Que juego base tiene cada mod abierto y como se encontro.
    std::string mounted;
    int missing = 0;
    for (const auto& source : app.sources) {
        if (source->baseMissing) ++missing;
        if (!source->withBase) continue;
        mounted += (mounted.empty() ? "" : "\n") + sourceName(*source) + " -> " + source->basePath.u8string() +
                   " (" + baseHowText(source->baseHow, app.spanish) + ")";
    }
    if (!mounted.empty() || missing > 0) {
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::AlignTextToFramePadding();
        if (missing > 0) ui::pill(tr(app, "not found", "no encontrado"), ui::color::Warning);
        else ui::pill(tr(app, "mounted", "montado"), ui::color::Success);
        if (ImGui::IsItemHovered()) {
            std::string tip = mounted;
            if (missing > 0)
                tip += std::string(tip.empty() ? "" : "\n") +
                       tr(app, "Some mod needs the base game of its engine and none was found: pick its folder.",
                               "Algún mod necesita el juego base de su motor y no se encontró: elige su carpeta.");
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    ui::vrule();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    ImGui::TextUnformatted(tr(app, "Engine", "Motor"));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(190.0f);
    std::string autoLabel = "Auto";
    if (!app.sources.empty()) autoLabel += " (" + detectedText(app) + ")";
    const char* engines[] = {autoLabel.c_str(), "Codename", "Psych", "V-Slice"};
    if (ImGui::Combo("##engine", &app.engineChoice, engines, 4)) engineChoiceChanged(app);
    ui::tooltip(tr(app, "Auto: each mod is read with the engine its files show. Pick one to force it.",
                        "Auto: cada mod se lee con el motor que muestran sus archivos. Elige uno para forzarlo."));
    ui::vrule();
    ImGui::BeginDisabled(app.undo.empty());
    if (ui::iconButton("undo", ui::icon::Undo, tr(app, "Undo", "Deshacer"), tr(app, "Undo (Ctrl+Z)", "Deshacer (Ctrl+Z)")))
        swapHistory(app, app.undo, app.redo);
    ImGui::EndDisabled();
    sameLineOrWrap(buttonWidth(ui::label(ui::icon::Redo, tr(app, "Redo", "Rehacer"))));
    ImGui::BeginDisabled(app.redo.empty());
    if (ui::iconButton("redo", ui::icon::Redo, tr(app, "Redo", "Rehacer"), tr(app, "Redo (Ctrl+Y)", "Rehacer (Ctrl+Y)")))
        swapHistory(app, app.redo, app.undo);
    ImGui::EndDisabled();

    // A la derecha: los hallazgos de todo lo abierto.
    const int errors = countSeverity(app, Severity::Error);
    const int warnings = countSeverity(app, Severity::Warning);
    const std::string errorText = std::string(ui::fonts().icons ? ui::icon::Error : "") + (ui::fonts().icons ? "  " : "") + std::to_string(errors);
    const std::string warnText = std::string(ui::fonts().icons ? ui::icon::Warning : "") + (ui::fonts().icons ? "  " : "") + std::to_string(warnings);
    const float needed = buttonWidth(errorText) + buttonWidth(warnText) + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine();
    const float x = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - needed);
    ImGui::SetCursorPosX(x);
    if (ui::countBadge("errors", ui::icon::Error, errors, ui::color::Error,
                       tr(app, "Errors: what the engine would not draw or load", "Errores: lo que el motor no dibujaría o no cargaría"))) {
        app.showFindings = true;
        app.findingsSeverity = 2;
    }
    ImGui::SameLine();
    if (ui::countBadge("warnings", ui::icon::Warning, warnings, ui::color::Warning,
                       tr(app, "Warnings: what works, with a catch", "Avisos: lo que funciona, con un pero"))) {
        app.showFindings = true;
        app.findingsSeverity = 1;
    }
}

void drawStatusBar(NoteLabApp& app) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::AlignTextToFramePadding();
    if (!app.sources.empty()) {
        const Source& first = *app.sources.front();
        Engine engine;
        const bool known = sourceEngine(app, first, engine);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float r = 4.0f;
        const float cy = at.y + ImGui::GetFrameHeight() * 0.5f;
        draw->AddCircleFilled(ImVec2(at.x + r, cy), r, known ? engineColor(engine) : ui::color::Faint);
        ImGui::Dummy(ImVec2(r * 2.0f, ImGui::GetFrameHeight()));
        ImGui::SameLine(0.0f, 8.0f);
        // Los tipos y charts de los mods; los de su juego base no cuentan.
        size_t styles = 0, charts = 0, types = 0;
        for (const auto& source : app.sources) {
            for (const NoteStyle& style : source->catalog.styles) if (styleVisible(app, *source, style)) ++styles;
            charts += source->songs.size() -
                      static_cast<size_t>(std::count(source->songFromBase.begin(), source->songFromBase.end(), 1));
            types += ownTypeCount(app, *source);
        }
        char summary[256];
        std::snprintf(summary, sizeof(summary), app.spanish ? "%s · %zu mod%s · %zu estilos · %zu tipos de nota · %zu charts"
                                                            : "%s · %zu mod%s · %zu styles · %zu note types · %zu charts",
                      detectedText(app).c_str(), app.sources.size(), app.sources.size() == 1 ? "" : "s", styles, types, charts);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
        ImGui::TextUnformatted(summary);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 18.0f);
    }
    const std::string& status = app.spanish ? app.statusEs : app.statusEn;
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Text));
    const float langWidth = 86.0f;
    const float room = ImGui::GetContentRegionAvail().x - langWidth - 12.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    draw->PushClipRect(at, ImVec2(at.x + std::max(0.0f, room), at.y + ImGui::GetFrameHeight()), true);
    ImGui::TextUnformatted(status.c_str());
    draw->PopClipRect();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(status.c_str()).x > room) ImGui::SetTooltip("%s", status.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - langWidth));
    int language = app.spanish ? 1 : 0;
    const char* languages[] = {"EN", "ES"};
    if (ui::segmented("lang", &language, languages, 2, 40.0f)) {
        app.spanish = language == 1;
        saveSettings(app);
    }
}

// --------------------------------------------------------- panel de mods --

// Una fila de la lista de estilos: icono de su uso, nombre y, a la derecha,
// sus errores o avisos y la marca de editado.
bool styleRow(const NoteStyle& style, bool chosen, int errors, int warnings, bool edited, bool showEngine) {
    const float rowHeight = ImGui::GetTextLineHeight() + 12.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool pressed = ImGui::Selectable("##row", chosen, 0, ImVec2(width, rowHeight));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float textY = at.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
    if (chosen) draw->AddRectFilled(ImVec2(at.x, at.y + 4.0f), ImVec2(at.x + 3.0f, at.y + rowHeight - 4.0f), ui::color::Accent, 2.0f);
    float x = at.x + 10.0f;
    if (ui::fonts().icons) {
        draw->AddText(ImVec2(x, textY), chosen ? ui::color::Accent : ui::color::Muted,
                      style.id.rfind("notelab:", 0) == 0 ? ui::icon::Brush : useIcon(style.use));
        x += 24.0f;
    }
    float right = at.x + width - 6.0f;
    auto badge = [&](const std::string& text, ImU32 tint) {
        ImGui::PushFont(ui::fonts().semibold, 12.0f);
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        ImGui::PopFont();
        const float w = size.x + 12.0f, h = 17.0f;
        const ImVec2 a(right - w, at.y + (rowHeight - h) * 0.5f);
        draw->AddRectFilled(a, ImVec2(right, a.y + h), ui::withAlpha(tint, 48), h * 0.5f);
        draw->AddText(ui::fonts().semibold, 12.0f, ImVec2(a.x + 6.0f, a.y + (h - size.y) * 0.5f), tint, text.c_str());
        right -= w + 5.0f;
    };
    if (errors) badge(std::to_string(errors), ui::color::Error);
    else if (warnings) badge(std::to_string(warnings), ui::color::Warning);
    if (edited && ui::fonts().icons) {
        const ImVec2 size = ImGui::CalcTextSize(ui::icon::Edit);
        draw->AddText(ImVec2(right - size.x, textY), ui::color::Accent, ui::icon::Edit);
        right -= size.x + 6.0f;
    }
    std::string name = style.name;
    if (showEngine) name = std::string(engineLabel(style.engine)) + " · " + name;
    draw->PushClipRect(ImVec2(x, at.y), ImVec2(std::max(x, right - 4.0f), at.y + rowHeight), true);
    draw->AddText(ImVec2(x, textY), style.referenced ? ui::color::Text : ui::color::Faint, name.c_str());
    draw->PopClipRect();
    return pressed;
}

void drawModsPanel(NoteLabApp& app, SDL_Window* window) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ui::fonts().semibold, 13.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    ImGui::TextUnformatted(tr(app, "MODS", "MODS"));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight()));
    if (ui::iconButton("addmod", ui::icon::Add, "+", tr(app, "Open a mod folder (Ctrl+O)", "Abrir la carpeta de un mod (Ctrl+O)")))
        openDialog(app, window, DialogAction::AddModFolder);
    ui::toggle("others", ui::label(ui::icon::Layers, tr(app, "Other engines", "Otros motores")), &app.showOtherEngines,
               tr(app, "Also list what belongs to another engine (the engine of this install does not load it).",
                       "Listar también lo que es de otro motor (el de esta instalación no lo carga)."));
    ImGui::SameLine(0.0f, 6.0f);
    ui::toggle("unused", ui::label(ui::icon::EyeOff, tr(app, "Unused", "Sin uso")), &app.showUnused,
               tr(app, "Also list what the engine does not load by itself (loose splashes, atlases only a script uses).",
                       "Listar también lo que el motor no carga por sí solo (salpicaduras sueltas, atlas que solo usa un script)."));
    ImGui::Spacing();
    if (app.sources.empty()) {
        ImGui::Spacing();
        ui::caption(tr(app, "No mods open yet. Open a folder or a ZIP, or drop it on the window: the engine is detected by itself. Up to four at once.",
                            "Aún no hay mods abiertos. Abre una carpeta o un ZIP, o suéltalo en la ventana: el motor se detecta solo. Hasta cuatro a la vez."));
        ImGui::Spacing();
        if (ui::primaryButton(ui::label(ui::icon::FolderOpen, tr(app, "Open mod folder…", "Abrir carpeta de mod…")),
                              ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            openDialog(app, window, DialogAction::AddModFolder);
        return;
    }
    int removeIndex = -1, reloadIndex = -1;
    int openMod = -1;
    bool openBase = false;
    std::string openModName;
    fs::path openModRoot;
    // Lo que cambia la lista se hace al acabar de recorrerla.
    int variantSource = -1, variantStyle = -1, deleteSource = -1, deleteStyle = -1;
    int borrowSource = -1, borrowStyle = -1;
    for (size_t s = 0; s < app.sources.size(); ++s) {
        Source& source = *app.sources[s];
        ImGui::PushID(static_cast<int>(s));
        Engine engine;
        const bool known = sourceEngine(app, source, engine);
        int visible = 0;
        for (const NoteStyle& style : source.catalog.styles) if (styleVisible(app, source, style)) ++visible;

        // Tarjeta del mod: motor, nombre, cifras y cerrar.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        const float cardWidth = ImGui::GetContentRegionAvail().x;
        const float cardHeight = ImGui::GetTextLineHeight() * 2.0f + 22.0f;
        const bool clicked = ImGui::InvisibleButton("card", ImVec2(cardWidth - 34.0f, cardHeight));
        const bool cardHovered = ImGui::IsItemHovered();
        if (clicked) source.expanded = !source.expanded;
        bool cardMenu = false;
        if (ImGui::BeginPopupContextItem("cardmenu")) {
            cardMenu = true;
            if (menuItem(source.expanded ? ui::icon::EyeOff : ui::icon::Eye,
                         source.expanded ? tr(app, "Collapse its styles", "Contraer sus estilos") : tr(app, "Expand its styles", "Desplegar sus estilos")))
                source.expanded = !source.expanded;
            if (menuItem(ui::icon::Restart, tr(app, "Reload this mod", "Recargar este mod"))) reloadIndex = static_cast<int>(s);
            // Una instalacion con varios mods: abrir otro, o la instalacion sola.
            const fs::path install = source.layout.install.empty() ? source.root : source.layout.install;
            const std::string current = source.layout.install.empty() ? std::string() : source.layout.mod.filename().u8string();
            if (!source.layout.mods.empty() &&
                ImGui::BeginMenu(ui::label(ui::icon::FolderOpen, tr(app, "Open a mod of this install", "Abrir un mod de esta instalación")).c_str())) {
                for (const std::string& mod : source.layout.mods)
                    if (ImGui::MenuItem(mod.c_str(), nullptr, mod == current && !source.baseOnly)) {
                        openMod = static_cast<int>(s);
                        openModName = mod;
                    }
                ImGui::EndMenu();
            }
            if (!source.layout.install.empty() && !source.baseOnly &&
                menuItem(ui::icon::Game, tr(app, "Open the install as the base game", "Abrir la instalación como juego base"))) {
                openMod = static_cast<int>(s);
                openModName.clear();
                openBase = true;
                openModRoot = install;
            }
            if (menuItem(ui::icon::Warning, tr(app, "See the findings", "Ver los hallazgos"))) app.showFindings = true;
            ImGui::Separator();
            if (menuItem(ui::icon::FolderOpen, tr(app, "Show in folder", "Mostrar en la carpeta"))) showSourceInFolder(app, source);
            if (menuItem(ui::icon::Copy, tr(app, "Copy its path", "Copiar su ruta"))) copyText(app, source.root.u8string());
            ImGui::Separator();
            if (menuItem(ui::icon::Close, tr(app, "Close this mod", "Cerrar este mod"))) removeIndex = static_cast<int>(s);
            ImGui::EndPopup();
        }
        if (cardHovered && !cardMenu) {
            std::string tip = source.root.u8string();
            if (!source.baseOnly && !source.layout.install.empty()) {
                tip = source.layout.mod.u8string() + "\n" + tr(app, "Mod of the install ", "Mod de la instalación ") +
                      source.layout.install.filename().u8string();
                if (source.layout.mods.size() > 1)
                    tip += " (" + std::to_string(source.layout.mods.size()) + tr(app, " mods; right click to open another)", " mods; clic derecho para abrir otro)");
            } else if (source.baseOnly) {
                tip += tr(app, "\nThe install as the base game, without its mods.", "\nLa instalación como juego base, sin sus mods.");
            } else if (!source.layout.mods.empty()) {
                tip += "\n" + std::to_string(source.layout.mods.size()) +
                       tr(app, " mods inside: right click to open one.", " mods dentro: clic derecho para abrir uno.");
            }
            if (source.packsMounted > 0)
                tip += "\n" + std::to_string(source.packsMounted) +
                       tr(app, " content packs on top of the mod (content/order.txt)", " paquetes de contenido encima del mod (content/order.txt)");
            if (!source.guess.evidence.empty()) {
                tip += std::string("\n") + tr(app, "Detected by: ", "Detectado por: ");
                for (size_t e = 0; e < source.guess.evidence.size() && e < 5; ++e) tip += (e ? ", " : "") + source.guess.evidence[e];
            }
            if (source.withBase)
                tip += std::string("\n") + tr(app, "Base game: ", "Juego base: ") + source.basePath.u8string() + " (" +
                       baseHowText(source.baseHow, app.spanish) + ")";
            else if (source.baseMissing)
                tip += tr(app, "\nIt needs the base game of its engine and none was found: pick its folder with \"Base game\".",
                               "\nNecesita el juego base de su motor y no se encontró: elige su carpeta con «Juego base».");
            else if (known)
                tip += tr(app, "\nIt brings its engine's default skin: no base game needed.",
                               "\nTrae el skin por defecto de su motor: no necesita juego base.");
            ImGui::SetTooltip("%s", tip.c_str());
        }
        const ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);
        draw->AddRectFilled(cardMin, cardMax, cardHovered ? IM_COL32(34, 38, 49, 255) : ui::color::Raised, 8.0f);
        const ImU32 tint = known ? engineColor(engine) : ui::color::Muted;
        draw->AddRectFilled(cardMin, ImVec2(cardMin.x + 4.0f, cardMax.y), tint, 8.0f, ImDrawFlags_RoundCornersLeft);
        const std::string engineText = known ? engineLabel(engine) : "?";
        const float top = cardMin.y + 9.0f;
        ImGui::PushFont(ui::fonts().semibold, 11.5f);
        const ImVec2 engineSize = ImGui::CalcTextSize(engineText.c_str());
        ImGui::PopFont();
        draw->AddRectFilled(ImVec2(cardMin.x + 14.0f, top + 1.0f), ImVec2(cardMin.x + 26.0f + engineSize.x, top + 18.0f),
                            ui::withAlpha(tint, 50), 9.0f);
        draw->AddText(ui::fonts().semibold, 11.5f, ImVec2(cardMin.x + 20.0f, top + 3.0f), tint, engineText.c_str());
        const float nameX = cardMin.x + 34.0f + engineSize.x;
        std::string modName = sourceName(source);
        const float room = cardMax.x - 36.0f - nameX;
        ImGui::PushFont(ui::fonts().semibold, 15.0f);
        if (ImGui::CalcTextSize(modName.c_str()).x > room) {
            while (modName.size() > 1 && ImGui::CalcTextSize((modName + "…").c_str()).x > room) {
                size_t cut = modName.size() - 1;
                while (cut > 0 && (static_cast<unsigned char>(modName[cut]) & 0xC0) == 0x80) --cut;   // sin partir un caracter UTF-8
                modName.erase(cut);
            }
            modName += "…";
        }
        ImGui::PopFont();
        draw->AddText(ui::fonts().semibold, 15.0f, ImVec2(nameX, top), ui::color::Text, modName.c_str());
        // Tipos y charts del propio mod; los del juego base van aparte.
        const size_t ownTypes = ownTypeCount(app, source);
        const size_t ownSongs = source.songs.size() -
            static_cast<size_t>(std::count(source.songFromBase.begin(), source.songFromBase.end(), 1));
        char numbers[160];
        std::snprintf(numbers, sizeof(numbers), app.spanish ? "%d estilos · %zu tipos · %zu charts%s" : "%d styles · %zu types · %zu charts%s",
                      visible, ownTypes, ownSongs,
                      source.withBase ? (app.spanish ? " · con base" : " · with base")
                      : source.baseMissing ? (app.spanish ? " · sin base" : " · no base") : "");
        draw->AddText(ui::fonts().ui, 13.0f, ImVec2(cardMin.x + 14.0f, top + ImGui::GetTextLineHeight() + 6.0f), ui::color::Muted, numbers);
        ImGui::SetCursorScreenPos(ImVec2(cardMax.x - 30.0f, cardMin.y + (cardHeight - 26.0f) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (ui::iconButton("close", ui::icon::Close, "x", tr(app, "Close this mod", "Cerrar este mod"), false, 26.0f))
            removeIndex = static_cast<int>(s);
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y + 6.0f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));

        if (source.expanded) {
            std::string lastScope = "\x01";
            int shown = 0;
            for (size_t i = 0; i < source.catalog.styles.size(); ++i) {
                const NoteStyle& style = source.catalog.styles[i];
                if (!styleVisible(app, source, style)) continue;
                ++shown;
                const bool base = i < source.fromBase.size() && source.fromBase[i];
                // Lo creado en Note Lab (variantes, HUD y aspectos) va aparte.
                const bool created = isVariant(style);
                const std::string scopeKey = created ? std::string("created|") : (base ? "base|" : "mod|") + style.scope;
                if (scopeKey != lastScope) {
                    lastScope = scopeKey;
                    std::string scopeTitle = style.scope.empty() ? tr(app, "mod folder", "carpeta del mod") : style.scope;
                    // Del juego base se monta su assets/ (baseMountOf).
                    if (base) scopeTitle = std::string(tr(app, "base game · ", "juego base · ")) + "assets/" + style.scope;
                    if (created) scopeTitle = tr(app, "made in Note Lab", "creado en Note Lab");
                    ImGui::Dummy(ImVec2(1.0f, 2.0f));
                    ImGui::PushFont(ui::fonts().mono, 12.5f);
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Faint));
                    ImGui::Indent(8.0f);
                    ImGui::TextUnformatted(scopeTitle.c_str());
                    ImGui::Unindent(8.0f);
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                const StyleReport& report = source.reports[i];
                int errors = 0, warnings = 0;
                for (const Finding& f : report.findings) {
                    if (f.severity == Severity::Error) ++errors;
                    if (f.severity == Severity::Warning) ++warnings;
                }
                ImGui::PushID(static_cast<int>(i));
                const bool chosen = app.selSource == static_cast<int>(s) && app.selStyle == static_cast<int>(i);
                const bool edited = i < source.edited.size() && source.edited[i];
                if (styleRow(style, chosen, errors, warnings, edited, app.showOtherEngines))
                    selectStyle(app, static_cast<int>(s), static_cast<int>(i));
                bool styleMenu = false;
                if (ImGui::BeginPopupContextItem("stylemenu")) {
                    styleMenu = true;
                    if (menuItem(ui::icon::Check, tr(app, "Select", "Elegir"))) selectStyle(app, static_cast<int>(s), static_cast<int>(i));
                    if (menuItem(ui::icon::Photo, tr(app, "See its assets", "Ver sus assets"))) {
                        selectStyle(app, static_cast<int>(s), static_cast<int>(i));
                        app.requestedTab = 2;
                    }
                    if (menuItem(ui::icon::Restart, tr(app, "Back to the mod's version", "Volver a la versión del mod"), nullptr, false,
                                 source.originals.count(i) > 0)) {
                        selectStyle(app, static_cast<int>(s), static_cast<int>(i));
                        revertStyle(app);
                    }
                    if (menuItem(ui::icon::Copy, tr(app, "Duplicate as a new variant", "Duplicar como variante nueva"))) {
                        variantSource = static_cast<int>(s);
                        variantStyle = static_cast<int>(i);
                    }
                    if (isVariant(style) && menuItem(ui::icon::Close, tr(app, "Delete this variant", "Borrar esta variante"))) {
                        deleteSource = static_cast<int>(s);
                        deleteStyle = static_cast<int>(i);
                    }
                    if (menuItem(ui::icon::Zip, tr(app, "Export…", "Exportar…"), "Ctrl+E")) {
                        selectStyle(app, static_cast<int>(s), static_cast<int>(i));
                        openExport(app);
                    }
                    // Su HUD en el estilo elegido, sin cambiar de mod ni de estilo.
                    const NoteStyle* current = selectedStyle(app);
                    if (current && !chosen && style.hasHud) {
                        const std::string text = std::string(tr(app, "Use its HUD in ", "Usar su HUD en «")) + current->name +
                                                 tr(app, "", "»");
                        if (menuItem(ui::icon::Layers, text.c_str())) {
                            borrowSource = static_cast<int>(s);
                            borrowStyle = static_cast<int>(i);
                        }
                    }
                    ImGui::Separator();
                    if (menuItem(ui::icon::Copy, tr(app, "Copy its id", "Copiar su id"))) copyText(app, style.id);
                    if (menuItem(ui::icon::Copy, tr(app, "Copy the definition path", "Copiar la ruta de la definición"))) copyText(app, style.definition);
                    if (menuItem(ui::icon::FolderOpen, tr(app, "Show in folder", "Mostrar en la carpeta"))) revealVirtual(app, source, style.definition);
                    ImGui::EndPopup();
                }
                if (!styleMenu && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    std::string tip = std::string(useLabel(app, style.use));
                    if (!style.useDetail.empty() && style.use != StyleUse::Default) tip += ": " + style.useDetail;
                    if (!style.referenced) tip += tr(app, "\nThe engine does not load it by itself.", "\nEl motor no lo carga por sí solo.");
                    tip += "\n" + style.definition;
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                ImGui::PopID();
            }
            int hidden = 0;
            for (const NoteStyle& style : source.catalog.styles)
                if (!style.referenced && !app.showUnused) ++hidden;
            if (shown == 0 && hidden == 0)
                ui::caption(tr(app, "No note styles of its own for this engine: it uses the base game's (add it with “Base game”).",
                                    "Sin estilos de notas propios de este motor: usa los del juego base (añádelo con «Juego base»)."));
            if (hidden > 0)
                ui::caption((std::to_string(hidden) + tr(app, " more the engine does not load by itself (“Unused”).",
                                                             " más que el motor no carga por sí solo («Sin uso»).")).c_str());
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    if (removeIndex >= 0) requestSourceClose(app, removeIndex);
    else if (reloadIndex >= 0) reloadSource(app, reloadIndex);
    else if (openMod >= 0 && openMod < static_cast<int>(app.sources.size())) {
        const Source& from = *app.sources[static_cast<size_t>(openMod)];
        const fs::path root = openBase ? openModRoot : (from.layout.install.empty() ? from.root : from.layout.install);
        addSource(app, root, openModName, openBase);
    }
    else if (borrowSource >= 0) borrowHud(app, borrowSource, borrowStyle);
    else if (deleteSource >= 0) deleteVariant(app, deleteSource, deleteStyle);
    else if (variantSource >= 0) {
        selectStyle(app, variantSource, variantStyle);
        createVariant(app);
    }
}

void drawKeysPopup(NoteLabApp& app) {
    if (!ImGui::BeginPopup("keys")) {
        app.bindingLane = -1;
        return;
    }
    ui::title(tr(app, "Keys", "Teclas"), 17.0f);
    ui::caption(tr(app, "To play the chosen side. Click a lane, then press the new key (Esc cancels).",
                        "Para jugar el lado elegido. Pulsa un carril y después la tecla nueva (Esc cancela)."));
    ImGui::Spacing();
    const char* lanes[] = {tr(app, "Left", "Izquierda"), tr(app, "Down", "Abajo"), tr(app, "Up", "Arriba"), tr(app, "Right", "Derecha")};
    for (int lane = 0; lane < 4; ++lane) {
        ImGui::PushID(lane);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(lanes[lane]);
        ImGui::SameLine(110.0f);
        const bool binding = app.bindingLane == lane;
        const std::string label = binding ? std::string(tr(app, "press a key…", "pulsa una tecla…"))
                                          : std::string(ImGui::GetKeyName(app.keys[static_cast<size_t>(lane)]));
        if (binding) {
            if (ui::primaryButton(label, ImVec2(150.0f, 0.0f))) app.bindingLane = -1;
        } else if (ImGui::Button(label.c_str(), ImVec2(150.0f, 0.0f))) {
            app.bindingLane = lane;
        }
        ImGui::PopID();
    }
    if (app.bindingLane >= 0) {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            const ImGuiKey key = static_cast<ImGuiKey>(k);
            if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) continue;
            if (key == ImGuiKey_Escape && ImGui::IsKeyPressed(key, false)) { app.bindingLane = -1; break; }
            if (ImGui::IsKeyPressed(key, false)) {
                app.keys[static_cast<size_t>(app.bindingLane)] = key;
                app.bindingLane = -1;
                saveSettings(app);
                break;
            }
        }
    }
    ImGui::Spacing();
    if (ImGui::Checkbox(tr(app, "Arrow keys too", "También las flechas"), &app.arrowsToo)) saveSettings(app);
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Default (D F J K)", "Por defecto (D F J K)"))) {
        app.keys = {ImGuiKey_D, ImGuiKey_F, ImGuiKey_J, ImGuiKey_K};
        saveSettings(app);
    }
    ImGui::EndPopup();
}

// --------------------------------------------------------- vista previa --

// La siguiente pieza va en la misma linea si cabe; si no, en la siguiente.
void sameLineOrWrap(float nextWidth) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < nextWidth) ImGui::NewLine();
}

// Precision como la calcula el motor del estilo: Psych suma 1, 0.67, 0.34 y 0
// por juicio (Rating.hx:31-46) y Codename 1, 0.75, 0.45 y 0.25
// (RatingManager.hx:56-59); los fallos cuentan 0 en los dos (PlayState.hx:2951
// de Psych, :2007-2008 de Codename). V-Slice no la ensena durante la cancion.
bool accuracyOf(const NoteLabApp& app, Engine engine, double& accuracy) {
    if (engine == Engine::VSlice) return false;
    static const double psych[4] = {1.0, 0.67, 0.34, 0.0};
    static const double codename[4] = {1.0, 0.75, 0.45, 0.25};
    const double* weight = engine == Engine::Psych ? psych : codename;
    double sum = 0.0;
    int played = app.counts[static_cast<size_t>(Judgement::Miss)];
    for (int j = 0; j < 4; ++j) {
        const int count = app.counts[static_cast<size_t>(1 + j)];
        sum += weight[j] * count;
        played += count;
    }
    if (played <= 0) return false;
    accuracy = sum / played;
    return true;
}

// El marcador: fichas de juicios, combo y precision, y el ultimo juicio. Va en
// px de juego desde la esquina del juego (`game`), con su capa del HUD.
ImRect drawScoreboard(NoteLabApp& app, ImVec2 game, float fit) {
    const HudLayer& layer = app.score;
    const float s = std::max(0.25f, layer.scale);
    const float font = 13.0f * s;
    const int alpha = static_cast<int>(std::clamp(layer.alpha, 0.0f, 1.0f) * 255.0f);
    struct Chip { std::string text; ImU32 tint; };
    std::vector<Chip> chips = {
        {"Sick!  " + std::to_string(app.counts[1]), ui::color::Accent},
        {std::string(tr(app, "Good", "Bien")) + "  " + std::to_string(app.counts[2]), ui::color::Success},
        {std::string(tr(app, "Bad", "Mal")) + "  " + std::to_string(app.counts[3]), ui::color::Warning},
        {std::string(tr(app, "Shit", "Pésimo")) + "  " + std::to_string(app.counts[4]), IM_COL32(210, 120, 80, 255)},
        {std::string(tr(app, "Misses", "Fallos")) + "  " + std::to_string(app.counts[5]), ui::color::Error},
        {"Combo  " + std::to_string(app.state.combo), ui::color::Text},
    };
    double accuracy = 0.0;
    if (const NoteStyle* style = selectedStyle(app))
        if (accuracyOf(app, style->engine, accuracy)) {
            char text[64];
            std::snprintf(text, sizeof(text), "%s  %.2f%%", tr(app, "Accuracy", "Precisión"), accuracy * 100.0);
            chips.push_back({text, ui::color::Info});
        }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start(game.x + (12.0f + layer.x) * fit, game.y + (12.0f + layer.y) * fit);
    float x = start.x, bottom = start.y;
    for (const Chip& chip : chips) {
        ImGui::PushFont(ui::fonts().semibold, font);
        const ImVec2 size = ImGui::CalcTextSize(chip.text.c_str());
        ImGui::PopFont();
        const ImVec2 a(x, start.y), b(x + size.x + 16.0f * s, start.y + size.y + 10.0f * s);
        draw->AddRectFilled(a, b, IM_COL32(12, 13, 18, 200 * alpha / 255), 6.0f * s);
        draw->AddRect(a, b, ui::withAlpha(chip.tint, 110 * alpha / 255), 6.0f * s);
        draw->AddText(ui::fonts().semibold, font, ImVec2(a.x + 8.0f * s, a.y + 5.0f * s), ui::withAlpha(chip.tint, alpha), chip.text.c_str());
        x = b.x + 6.0f * s;
        bottom = b.y;
    }
    float right = x - 6.0f * s;
    if (app.lastJudgement != Judgement::None) {
        const char* last = judgementLabel(app, app.lastJudgement);
        ImGui::PushFont(ui::fonts().semibold, 22.0f * s);
        const ImVec2 lastSize = ImGui::CalcTextSize(last);
        ImGui::PopFont();
        const ImVec2 at(start.x, bottom + 8.0f * s);
        draw->AddText(ui::fonts().semibold, 22.0f * s, at,
                      ui::withAlpha(app.lastJudgement == Judgement::Miss ? ui::color::Error : ui::color::Text, alpha), last);
        bottom = at.y + lastSize.y;
        right = std::max(right, at.x + lastSize.x);
    }
    return ImRect(start, ImVec2(right, bottom));
}

// El final de una pasada jugada: el resultado en el centro del lienzo y
// «Jugar otra vez». Los juicios y la precision son los del marcador.
void drawRunResults(NoteLabApp& app, ImVec2 origin, ImVec2 size) {
    const char* names[5] = {"Sick!", tr(app, "Good", "Bien"), tr(app, "Bad", "Mal"), tr(app, "Shit", "Pésimo"), tr(app, "Misses", "Fallos")};
    const ImU32 tints[5] = {ui::color::Accent, ui::color::Success, ui::color::Warning, IM_COL32(210, 120, 80, 255), ui::color::Error};
    std::string accuracyText;
    double accuracy = 0.0;
    if (const NoteStyle* style = selectedStyle(app))
        if (accuracyOf(app, style->engine, accuracy)) {
            char text[64];
            std::snprintf(text, sizeof(text), "%.2f %%", accuracy * 100.0);
            accuracyText = text;
        }
    const float w = 360.0f, h = accuracyText.empty() ? 196.0f : 222.0f;
    const ImVec2 a(origin.x + (size.x - w) * 0.5f, origin.y + (size.y - h) * 0.5f), b(a.x + w, a.y + h);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(a, b, IM_COL32(18, 20, 27, 240), 10.0f);
    draw->AddRect(a, b, ui::withAlpha(ui::color::Accent, 150), 10.0f);
    draw->AddText(ui::fonts().semibold, 18.0f, ImVec2(a.x + 18.0f, a.y + 14.0f), ui::color::Text, tr(app, "End of the run", "Fin de la pasada"));
    float y = a.y + 48.0f;
    for (int j = 0; j < 5; ++j) {
        const float x = a.x + 18.0f + (j % 3) * 110.0f;
        if (j == 3) y += 26.0f;
        const std::string text = std::string(names[j]) + "  " + std::to_string(app.counts[static_cast<size_t>(j + 1)]);
        draw->AddText(ui::fonts().semibold, 15.0f, ImVec2(x, y), tints[j], text.c_str());
    }
    y += 34.0f;
    const std::string combo = std::string(tr(app, "Best combo", "Mejor combo")) + "  " + std::to_string(app.maxCombo);
    draw->AddText(ui::fonts().ui, 15.0f, ImVec2(a.x + 18.0f, y), ui::color::Text, combo.c_str());
    if (!accuracyText.empty()) {
        y += 26.0f;
        draw->AddText(ui::fonts().ui, 15.0f, ImVec2(a.x + 18.0f, y), ui::color::Info,
                      (std::string(tr(app, "Accuracy", "Precisión")) + "  " + accuracyText).c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x + 18.0f, b.y - 44.0f));
    if (ui::primaryButton(ui::label(ui::icon::Restart, tr(app, "Play again", "Jugar otra vez")).c_str(), ImVec2(160.0f, 0.0f))) {
        restartPreview(app);
        app.playing = true;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::caption(tr(app, "or Space", "o Espacio"));
}

void resetHud(NoteLabApp& app) {
    app.settings.judgement = HudLayer{};
    app.settings.combo = HudLayer{};
    app.score = HudLayer{};
    saveSettings(app);
}

// Ajustes del HUD de la vista previa: juicio, combo y marcador.
void drawHudPopup(NoteLabApp& app) {
    if (!ImGui::BeginPopup("hud")) return;
    ui::title(tr(app, "Preview HUD", "HUD de la vista previa"), 17.0f);
    ui::caption(tr(app, "How the preview shows the judgement, the combo and the scoreboard. It changes neither the mod nor what is exported.",
                        "Cómo enseña la vista previa el juicio, el combo y el marcador. No cambia el mod ni lo que se exporta."));
    bool changed = false;
    ui::toggle("hudedit", ui::label(ui::icon::Edit, tr(app, "Move on the canvas", "Mover en el lienzo")), &app.hudEdit,
               tr(app, "Drag each layer; the wheel resizes it and right click has more. A sample judgement and combo stay on screen meanwhile.",
                       "Arrastra cada capa; la rueda cambia su tamaño y el clic derecho tiene más. Mientras, se ven un juicio y un combo de muestra."));
    auto layerControls = [&](const char* id, const char* name, const char* glyph, HudLayer& layer) {
        ImGui::PushID(id);
        ui::sectionHeader(name, glyph);
        changed |= ImGui::Checkbox(tr(app, "Visible", "Visible"), &layer.visible);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(app, "Reset", "Restablecer"))) {
            layer = HudLayer{};
            changed = true;
        }
        ImGui::SetNextItemWidth(230.0f);
        changed |= ImGui::SliderFloat(tr(app, "Size", "Tamaño"), &layer.scale, 0.25f, 3.0f, "%.2fx");
        ImGui::SetNextItemWidth(230.0f);
        changed |= ImGui::SliderFloat(tr(app, "Opacity", "Opacidad"), &layer.alpha, 0.1f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(230.0f);
        changed |= ImGui::DragFloat2(tr(app, "Position", "Posición"), &layer.x, 1.0f, -1280.0f, 1280.0f, "%.0f px");
        ImGui::PopID();
    };
    layerControls("judgement", tr(app, "Judgement (Sick!, Good…)", "Juicio (Sick!, Good…)"), ui::icon::Check, app.settings.judgement);
    layerControls("combo", tr(app, "Combo numbers", "Números del combo"), ui::icon::Pulse, app.settings.combo);
    layerControls("score", tr(app, "Scoreboard (when playing)", "Marcador (al jugar)"), ui::icon::Grid, app.score);
    ImGui::Spacing();
    if (ImGui::Button(ui::label(ui::icon::Restart, tr(app, "Reset everything", "Restablecer todo")).c_str())) resetHud(app);
    if (changed) saveSettings(app);
    ImGui::EndPopup();
}

// Clic derecho en el lienzo.
void drawCanvasMenu(NoteLabApp& app) {
    if (!ImGui::BeginPopupContextItem("canvasmenu")) return;
    if (menuItem(app.playing ? ui::icon::Pause : ui::icon::Play, app.playing ? tr(app, "Pause", "Pausar") : tr(app, "Play", "Reproducir"), "Space"))
        togglePlay(app);
    if (menuItem(ui::icon::Restart, tr(app, "Restart", "Reiniciar"), "R")) restartPreview(app);
    if (menuItem(ui::icon::Delete, tr(app, "Reset the scoreboard", "Poner el marcador a cero"), nullptr, false, app.manual)) resetScore(app);
    ImGui::Separator();
    ImGui::MenuItem(ui::label(ui::icon::Edit, tr(app, "Move the HUD", "Mover el HUD")).c_str(), nullptr, &app.hudEdit);
    bool changed = false;
    changed |= ImGui::MenuItem(ui::label(ui::icon::Check, tr(app, "Judgement", "Juicio")).c_str(), nullptr, &app.settings.judgement.visible);
    changed |= ImGui::MenuItem(ui::label(ui::icon::Pulse, tr(app, "Combo", "Combo")).c_str(), nullptr, &app.settings.combo.visible);
    changed |= ImGui::MenuItem(ui::label(ui::icon::Grid, tr(app, "Scoreboard", "Marcador")).c_str(), nullptr, &app.score.visible);
    if (menuItem(ui::icon::Restart, tr(app, "Reset the HUD", "Restablecer el HUD"))) resetHud(app);
    if (menuItem(ui::icon::Settings, tr(app, "HUD settings…", "Ajustes del HUD…"))) app.openHud = true;
    ImGui::Separator();
    ImGui::MenuItem(ui::label(ui::icon::Game, tr(app, "Play a side", "Jugar un lado")).c_str(), nullptr, &app.manual);
    ImGui::MenuItem(ui::label(ui::icon::Down, "Downscroll").c_str(), nullptr, &app.settings.downscroll);
    if (changed) saveSettings(app);
    ImGui::EndPopup();
}

// Marco de una capa en el modo «mover el HUD»: esquinas y su nombre.
void drawLayerFrame(ImDrawList* draw, const ImRect& r, const char* name, bool active) {
    const ImU32 color = ui::withAlpha(ui::color::Accent, active ? 255 : 190);
    draw->AddRectFilled(r.Min, r.Max, ui::withAlpha(ui::color::Accent, active ? 34 : 16), 4.0f);
    const float len = std::min(14.0f, std::min(r.GetWidth(), r.GetHeight()) * 0.4f);
    const float t = active ? 2.5f : 1.5f;
    const ImVec2 corners[4] = {r.Min, ImVec2(r.Max.x, r.Min.y), r.Max, ImVec2(r.Min.x, r.Max.y)};
    const float dx[4] = {1.0f, -1.0f, -1.0f, 1.0f}, dy[4] = {1.0f, 1.0f, -1.0f, -1.0f};
    for (int k = 0; k < 4; ++k) {
        draw->AddLine(corners[k], ImVec2(corners[k].x + dx[k] * len, corners[k].y), color, t);
        draw->AddLine(corners[k], ImVec2(corners[k].x, corners[k].y + dy[k] * len), color, t);
    }
    ImGui::PushFont(ui::fonts().semibold, 11.5f);
    const ImVec2 size = ImGui::CalcTextSize(name);
    ImGui::PopFont();
    const ImVec2 a(r.Min.x, r.Min.y - size.y - 7.0f), b(a.x + size.x + 12.0f, r.Min.y - 2.0f);
    draw->AddRectFilled(a, b, color, 4.0f);
    draw->AddText(ui::fonts().semibold, 11.5f, ImVec2(a.x + 6.0f, a.y + 2.0f), IM_COL32(18, 16, 28, 255), name);
}

// Modo «mover el HUD»: cada capa visible tiene su marco; se arrastra para
// moverla, la rueda la agranda o la achica y el clic derecho tiene sus acciones.
void drawHudHandles(NoteLabApp& app, const NoteStyle& style, ImVec2 game, float fit, const ImRect& scoreRect, bool showScore) {
    float jx = 0.0f, jy = 0.0f, cx = 0.0f, cy = 0.0f;
    NotePreview::hudCenters(app.settings, jx, jy, cx, cy);
    const bool vslice = style.engine == Engine::VSlice;
    auto imageSize = [&](const HudAsset& asset, float fallbackW, float fallbackH) {
        if (style.hasHud && !asset.image.empty()) {
            const GlRenderer::PreviewImage image = app.renderer.previewImage(asset.image);
            if (image.ok) {
                const float s = asset.scale > 0.0f ? asset.scale : 1.0f;
                return ImVec2(image.width * s, image.height * s);
            }
        }
        return ImVec2(fallbackW, fallbackH);
    };
    struct Handle { const char* id; const char* name; HudLayer* layer; ImRect rect; };
    std::vector<Handle> handles;
    if (app.settings.judgement.visible) {
        const ImVec2 base = imageSize(style.judgements[0], 250.0f, 100.0f);
        const float s = (vslice ? 1.0f : 0.7f) * app.settings.judgement.scale * fit;
        const ImVec2 c(game.x + jx * fit, game.y + jy * fit);
        handles.push_back({"judgement", tr(app, "Judgement", "Juicio"), &app.settings.judgement,
                           ImRect(c.x - base.x * s * 0.5f, c.y - base.y * s * 0.5f, c.x + base.x * s * 0.5f, c.y + base.y * s * 0.5f)});
    }
    if (app.settings.combo.visible) {
        const ImVec2 digit = imageSize(style.digits[0], 90.0f, 110.0f);
        const float s = (vslice ? 1.0f : 0.5f) * app.settings.combo.scale;
        const float halfW = (43.0f * app.settings.combo.scale + digit.x * s * 0.5f) * fit;
        const float halfH = digit.y * s * 0.5f * fit;
        const ImVec2 c(game.x + cx * fit, game.y + cy * fit);
        handles.push_back({"combo", tr(app, "Combo", "Combo"), &app.settings.combo, ImRect(c.x - halfW, c.y - halfH, c.x + halfW, c.y + halfH)});
    }
    if (showScore) handles.push_back({"score", tr(app, "Scoreboard", "Marcador"), &app.score, scoreRect});
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (Handle& handle : handles) {
        ImGui::PushID(handle.id);
        ImRect r = handle.rect;
        r.Expand(4.0f);
        ImGui::SetCursorScreenPos(r.Min);
        ImGui::InvisibleButton("layer", ImVec2(std::max(8.0f, r.GetWidth()), std::max(8.0f, r.GetHeight())));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f) && fit > 0.0f) {
            handle.layer->x = std::clamp(handle.layer->x + io.MouseDelta.x / fit, -1280.0f, 1280.0f);
            handle.layer->y = std::clamp(handle.layer->y + io.MouseDelta.y / fit, -1280.0f, 1280.0f);
        }
        if (ImGui::IsItemDeactivated()) saveSettings(app);
        if (hovered && io.MouseWheel != 0.0f) {
            handle.layer->scale = std::clamp(handle.layer->scale * (io.MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f), 0.25f, 3.0f);
            saveSettings(app);
        }
        if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        bool menu = false;
        if (ImGui::BeginPopupContextItem("layermenu")) {
            menu = true;
            ui::caption(handle.name);
            if (menuItem(ui::icon::EyeOff, tr(app, "Hide", "Ocultar"))) handle.layer->visible = false;
            if (menuItem(ui::icon::Fit, tr(app, "Normal size", "Tamaño normal"))) handle.layer->scale = 1.0f;
            if (menuItem(ui::icon::Restart, tr(app, "Reset", "Restablecer"))) *handle.layer = HudLayer{};
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat(tr(app, "Opacity", "Opacidad"), &handle.layer->alpha, 0.1f, 1.0f, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings(app);
            ImGui::EndPopup();
            saveSettings(app);
        }
        if (hovered && !active && !menu)
            ImGui::SetTooltip("%s", tr(app, "Drag to move · wheel to resize · right click for more",
                                             "Arrastra para mover · rueda para el tamaño · clic derecho para más"));
        drawLayerFrame(draw, r, handle.name, hovered || active);
        ImGui::PopID();
    }
}

// Una ficha de dato del chart: icono, texto y su explicacion al pasar por encima.
void infoChip(const char* id, const char* glyph, const std::string& text, const std::string& tip, ImU32 tint) {
    ImGui::PushID(id);
    const std::string content = ui::label(glyph, text);
    ImGui::PushFont(nullptr, 13.5f);
    const ImVec2 size = ImGui::CalcTextSize(content.c_str());
    ImGui::PopFont();
    const ImVec2 box(size.x + 18.0f, size.y + 8.0f);
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < box.x) ImGui::NewLine();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("chip", box);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + box.x, at.y + box.y), ui::withAlpha(tint, ImGui::IsItemHovered() ? 34 : 22), 6.0f);
    draw->AddRect(at, ImVec2(at.x + box.x, at.y + box.y), ui::withAlpha(tint, 70), 6.0f);
    draw->AddText(ui::fonts().ui, 13.5f, ImVec2(at.x + 9.0f, at.y + 4.0f), tint == ui::color::Muted ? ui::color::Text : tint, content.c_str());
    if (!tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tip.c_str());
    ImGui::PopID();
}

// Lo que fija el chart cargado, a la vista: BPM, compas, velocidad y sus
// cambios, personajes, escenario, skin y desfase del audio.
void drawSongInfo(NoteLabApp& app) {
    const SongValues& v = app.songValues;
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(app, "Chart", "Chart"));
    ImGui::PopStyleColor();
    auto fmt = [](const char* format, double value) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), format, value);
        return std::string(buffer);
    };
    // BPM: el de ahora y sus cambios.
    float bpmNow = v.bpm;
    for (const BpmChange& change : v.bpmChanges)
        if (change.timeMs <= app.state.songMs && change.bpm > 0.0f) bpmNow = change.bpm;
    std::string bpmText = fmt("%.0f BPM", bpmNow);
    std::string bpmTip = tr(app, "Tempo at the playhead.", "Tempo en este punto.");
    if (v.bpmChanges.size() > 1) {
        const size_t count = v.bpmChanges.size() - 1;
        bpmText += " · " + std::to_string(count) + (count == 1 ? tr(app, " change", " cambio") : tr(app, " changes", " cambios"));
        for (const BpmChange& change : v.bpmChanges) bpmTip += "\n" + formatTime(change.timeMs) + "  " + fmt("%.2f BPM", change.bpm);
    }
    infoChip("bpm", ui::icon::Music, bpmText, bpmTip, ui::color::Muted);
    infoChip("signature", ui::icon::Grid, std::to_string(v.beatsPerMeasure) + "/" + std::to_string(v.stepsPerBeat),
             tr(app, "Beats per measure / steps per beat", "Pulsos por compás / pasos por pulso"), ui::color::Muted);
    // Velocidad: la del chart y sus cambios.
    std::string speedText = fmt("%.2fx", v.speed);
    std::string speedTip = tr(app, "The chart's scroll speed for this difficulty.", "La velocidad de scroll del chart en esta dificultad.");
    if (!v.speedChanges.empty()) {
        const size_t count = v.speedChanges.size();
        speedText += " · " + std::to_string(count) + (count == 1 ? tr(app, " change", " cambio") : tr(app, " changes", " cambios"));
        for (const SpeedChange& change : v.speedChanges) {
            speedTip += "\n" + formatTime(change.timeMs) + "  -> " + fmt("%.2fx", change.speed);
            if (change.durationMs > 0.0) speedTip += fmt(app.spanish ? " en %.2f s" : " over %.2f s", change.durationMs / 1000.0);
            if (change.strumLine == 0) speedTip += tr(app, " (opponent)", " (rival)");
            if (change.strumLine == 1) speedTip += tr(app, " (player)", " (jugador)");
        }
    }
    for (int line = 0; line < 2; ++line)
        if (v.lineSpeed[static_cast<size_t>(line)] > 0.0f)
            speedTip += "\n" + std::string(line == 0 ? tr(app, "Opponent line: ", "Línea del rival: ") : tr(app, "Player line: ", "Línea del jugador: ")) +
                        fmt("%.2fx", v.lineSpeed[static_cast<size_t>(line)]) + tr(app, " (its own)", " (propia)");
    infoChip("speed", ui::icon::Speed, speedText, speedTip, ui::color::Muted);
    if (!v.player.empty() || !v.opponent.empty()) {
        std::string cast = v.player + " vs " + v.opponent;
        // V-Slice escribe `none` cuando no hay girlfriend.
        if (!v.girlfriend.empty() && lowerText(v.girlfriend) != "none") cast += " · " + v.girlfriend;
        infoChip("cast", ui::icon::Contact, cast, tr(app, "Player vs opponent · girlfriend", "Jugador vs rival · girlfriend"), ui::color::Muted);
    }
    if (!v.stage.empty()) infoChip("stage", ui::icon::Home, v.stage, tr(app, "Stage", "Escenario"), ui::color::Muted);
    const std::string skin = !v.noteStyle.empty() ? v.noteStyle : v.arrowSkin;
    if (!skin.empty()) {
        if (!app.songSkinMissing.empty())
            infoChip("skin", ui::icon::Warning, skin,
                     std::string(tr(app, "The chart asks for this note skin and it is not among the opened folders: the preview keeps the selected one.",
                                         "El chart pide este skin de notas y no está entre lo abierto: la vista previa sigue con el elegido.")),
                     ui::color::Warning);
        else
            infoChip("skin", ui::icon::Brush, skin,
                     app.chartSkin ? tr(app, "The chart's note skin, selected by itself (View menu to turn it off).",
                                         "El skin de notas del chart, elegido solo (se apaga en el menú Ver).")
                                   : tr(app, "The chart's note skin (automatic selection is off).", "El skin de notas del chart (la elección automática está apagada)."),
                     ui::color::Accent);
    }
    if (!v.splashSkin.empty())
        infoChip("splash", ui::icon::Brush, v.splashSkin,
                 app.songSplash >= 0 ? tr(app, "The chart's splashes, used in the preview.", "Las salpicaduras del chart, usadas en la vista previa.")
                                     : tr(app, "The chart's splashes (same as the style's, or not among the opened folders).",
                                               "Las salpicaduras del chart (las mismas del estilo o no están entre lo abierto)."),
                 ui::color::Muted);
    if (v.audioOffsetMs != 0.0)
        infoChip("offset", ui::icon::Volume, fmt("%+.0f ms", v.audioOffsetMs),
                 tr(app, "The chart's audio offset: the song runs this far from the audio (Psych offset, V-Slice offsets.instrumental).",
                         "Desfase del audio del chart: la canción va así de separada del audio (offset de Psych, offsets.instrumental de V-Slice)."),
                 ui::color::Muted);
    if (!v.needsVoices) infoChip("voices", ui::icon::Volume, tr(app, "no voices", "sin voces"), "needsVoices = false", ui::color::Muted);
}

void drawPreviewTab(NoteLabApp& app) {
    // Transporte: reproducir, reiniciar, tiempo y chart.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.38f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.46f, 0.96f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.44f, 0.32f, 0.82f, 1.0f));
    if (ui::iconButton("play", app.playing ? ui::icon::Pause : ui::icon::Play,
                       app.playing ? tr(app, "Pause", "Pausa") : tr(app, "Play", "Reproducir"),
                       app.playing ? tr(app, "Pause (Space)", "Pausa (Espacio)") : tr(app, "Play (Space)", "Reproducir (Espacio)")))
        togglePlay(app);
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::iconButton("restart", ui::icon::Restart, tr(app, "Restart", "Reiniciar"), tr(app, "Restart (R)", "Reiniciar (R)")))
        restartPreview(app);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ui::fonts().mono, 14.5f);
    ImGui::Text("%s / %s", formatTime(app.state.songMs).c_str(), formatTime(app.patternMs).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    const float chartWidth = std::min(300.0f, std::max(170.0f, ImGui::GetContentRegionAvail().x * 0.34f));
    float position = static_cast<float>(app.state.songMs);
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - chartWidth - ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::SliderFloat("##time", &position, 0.0f, static_cast<float>(app.patternMs), "")) seekTo(app, position);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(chartWidth);
    const std::string current = ui::label(ui::icon::Music, app.songIndex < 0 ? std::string(tr(app, "Demo pattern", "Patrón de prueba"))
                                                                           : app.songLabel);
    const bool songOpen = ImGui::BeginCombo("##song", current.c_str(), ImGuiComboFlags_HeightLarge);
    if (!songOpen) tutorialMark("song-combo");
    if (songOpen) {
        if (ImGui::Selectable(tr(app, "Demo pattern", "Patrón de prueba"), app.songIndex < 0)) chooseDemo(app);
        for (size_t s = 0; s < app.sources.size(); ++s) {
            const Source& source = *app.sources[s];
            if (source.songs.empty()) continue;
            // Primero los charts del mod y despues, aparte, los del juego base.
            for (int pass = 0; pass < 2; ++pass) {
                bool titled = false;
                for (size_t i = 0; i < source.songs.size(); ++i) {
                    const bool base = i < source.songFromBase.size() && source.songFromBase[i];
                    if (base != (pass == 1)) continue;
                    if (!titled) {
                        titled = true;
                        const std::string title = sourceName(source) +
                                                  (base ? tr(app, " · base game", " · juego base") : "");
                        ImGui::SeparatorText(title.c_str());
                    }
                    const SongChart& song = source.songs[i];
                    ImGui::PushID(static_cast<int>(s * 100000 + i));
                    std::string label = song.id + "  ·  " + song.difficulty;
                    if (!song.variation.empty()) label += "  (" + song.variation + ")";
                    const bool chosen = app.songSource == static_cast<int>(s) && app.songIndex == static_cast<int>(i);
                    if (ImGui::Selectable(label.c_str(), chosen)) chooseSong(app, static_cast<int>(s), static_cast<int>(i));
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndCombo();
    }
    ui::tooltip(tr(app, "The demo pattern or a chart of an open mod, with its music.",
                        "El patrón de prueba o un chart de un mod abierto, con su música."));

    // Opciones de la vista, agrupadas.
    const char* sides[] = {tr(app, "Opponent", "Rival"), tr(app, "Player", "Jugador"), tr(app, "Both", "Ambos")};
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    ImGui::TextUnformatted(ui::fonts().icons ? ui::icon::Eye : tr(app, "Show", "Ver"));
    ImGui::PopStyleColor();
    ui::tooltip(tr(app, "Strumlines to show", "Líneas que se ven"));
    ImGui::SameLine();
    ui::segmented("show", &app.visibleLines, sides, 3);
    ui::vrule();
    ui::toggle("play", ui::label(ui::icon::Game, tr(app, "Play", "Jugar")), &app.manual,
               tr(app, "Play one side with the keyboard; the other plays itself.", "Juega un lado con el teclado; el otro se juega solo."));
    tutorialMark("play-toggle");
    if (app.manual) {
        ImGui::SameLine();
        ui::segmented("side", &app.playSide, sides, 2);
        ImGui::SameLine(0.0f, 4.0f);
        if (ui::iconButton("keys", ui::icon::Keyboard, tr(app, "Keys", "Teclas"), tr(app, "Keys…", "Teclas…"))) app.openKeys = true;
        ImGui::SameLine(0.0f, 4.0f);
        if (ui::iconButton("resetscore", ui::icon::Delete, tr(app, "Reset", "A cero"),
                           tr(app, "Reset the scoreboard without stopping the song. It also resets by itself on every new run.",
                                   "Poner el marcador a cero sin parar la canción. También vuelve a cero solo en cada pasada nueva.")))
            resetScore(app);
        app.resetScoreRect = ImVec4(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y);
    }
    sameLineOrWrap(160.0f);
    ui::toggle("downscroll", ui::label(ui::icon::Down, "Downscroll"), &app.settings.downscroll);
    // Ver estas notas con los receptores de otro estilo del mod (solo aqui).
    if (app.selSource >= 0 && app.selSource < static_cast<int>(app.sources.size())) {
        const Source& source = *app.sources[static_cast<size_t>(app.selSource)];
        const NoteStyle* chosen = nullptr;
        for (const NoteStyle& candidate : source.catalog.styles)
            if (candidate.id == app.previewReceptors) chosen = &candidate;
        sameLineOrWrap(190.0f);
        ImGui::SetNextItemWidth(180.0f);
        const std::string current = chosen ? std::string(tr(app, "Receptors: ", "Receptores: ")) + chosen->name : std::string(tr(app, "Receptors: the style's", "Receptores: los del estilo"));
        if (ImGui::BeginCombo("##previewreceptors", current.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable(tr(app, "The style's own", "Los del estilo"), !chosen)) app.previewReceptors.clear();
            for (const NoteStyle& candidate : source.catalog.styles) {
                const bool strums = std::any_of(candidate.parts.begin(), candidate.parts.end(), [](const PartBinding& b) { return b.part == Part::StrumStatic; });
                if (!strums || &candidate == selectedStyle(app)) continue;
                if (ImGui::Selectable((candidate.name + "##" + candidate.id).c_str(), chosen == &candidate)) app.previewReceptors = candidate.id;
            }
            ImGui::EndCombo();
        }
        tutorialMark("preview-receptors");
        ui::tooltip(tr(app, "See these notes with another style's receptors. Only in the preview: the style doesn't change (to keep them, Combine styles / HUD…).",
                            "Ver estas notas con los receptores de otro estilo. Solo en la vista previa: el estilo no cambia (para quedártelos, Combinar estilos / HUD…)."));
    }
    sameLineOrWrap(app.hasSongValues ? 270.0f : 170.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    ImGui::TextUnformatted(ui::fonts().icons ? ui::icon::Speed : tr(app, "Speed", "Velocidad"));
    ImGui::PopStyleColor();
    ui::tooltip(tr(app, "Scroll speed", "Velocidad de desplazamiento"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    if (app.hasSongValues && app.chartSpeed) {
        // La del chart ahora mismo (la del jugador), con sus cambios.
        float shown = speedAt(app.songValues, app.state.songMs, 1);
        ImGui::BeginDisabled();
        ImGui::SliderFloat("##speed", &shown, 0.5f, 4.0f, "%.2fx");
        ImGui::EndDisabled();
    } else {
        ImGui::SliderFloat("##speed", &app.settings.scrollSpeed, 0.5f, 4.0f, "%.1fx");
    }
    if (app.hasSongValues) {
        ImGui::SameLine(0.0f, 4.0f);
        ui::toggle("chartspeed", tr(app, "Chart", "Del chart"), &app.chartSpeed,
                   tr(app, "Follow the chart's speed and its changes during the song. Off: a fixed speed of your choice.",
                           "Seguir la velocidad del chart y sus cambios durante la canción. Apagado: una velocidad fija a tu gusto."));
    }
    if (app.songIndex < 0) {
        sameLineOrWrap(140.0f);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderFloat("##bpm", &app.bpm, 60.0f, 240.0f, "%.0f BPM")) chooseDemo(app);
    } else if (nlbuild::audioPlayback && app.audioLoaded) {
        sameLineOrWrap(150.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
        ImGui::TextUnformatted(ui::fonts().icons ? ui::icon::Volume : tr(app, "Volume", "Volumen"));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::SliderFloat("##volume", &app.musicVolume, 0.0f, 1.0f, "%.2f")) saveSettings(app);
    }
    const NoteStyle* style = selectedStyle(app);
    if (style && style->rgbPalette) {
        // Sin sitio, solo el icono (el texto va en la ayuda).
        const std::string full = ui::label(ui::icon::Palette, tr(app, "Psych colors", "Colores de Psych"));
        ImGui::SameLine();
        const bool compact = ui::fonts().icons && ImGui::GetContentRegionAvail().x < buttonWidth(full) + 4.0f;
        if (!compact && ImGui::GetContentRegionAvail().x < buttonWidth(full) + 4.0f) ImGui::NewLine();
        if (ui::toggle("psychcolors", compact ? std::string(ui::icon::Palette) : full, &app.psychColors,
                       tr(app, "Psych recolors notes with the player's RGB options; this uses the defaults (ClientPrefs).",
                               "Psych recolorea las notas con las opciones RGB del jugador; aquí se usan las de por defecto (ClientPrefs).")))
            saveSettings(app);
    }
    sameLineOrWrap(ImGui::GetFrameHeight() + 8.0f);
    if (ui::iconButton("hudbutton", ui::icon::Layers, "HUD", tr(app, "Preview HUD: judgement, combo and scoreboard…",
                                                              "HUD de la vista previa: juicio, combo y marcador…"), app.hudEdit))
        app.openHud = true;
    if (app.openHud) {
        ImGui::OpenPopup("hud");
        app.openHud = false;
    }
    drawHudPopup(app);
    if (app.audioPending) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ui::pill(tr(app, "decoding audio…", "decodificando audio…"), ui::color::Info);
    }
    if (app.hasSongValues) drawSongInfo(app);

    updatePlayback(app, ImGui::GetIO().DeltaTime * 1000.0f);

    // El lienzo, con esquinas redondeadas y el marcador encima.
    ImGui::Spacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float footer = ImGui::GetTextLineHeightWithSpacing() + 2.0f;
    const ImVec2 size(std::max(8.0f, avail.x), std::max(8.0f, avail.y - footer));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x > 0.0f ? ImGui::GetIO().DisplayFramebufferScale.x : 1.0f;
    app.settings.hudSample = app.hudEdit;
    const GlRenderer::PreviewImage image = renderPreview(app, static_cast<int>(size.x * scale), static_cast<int>(size.y * scale));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + size.x, origin.y + size.y);
    draw->AddRectFilled(origin, max, IM_COL32(14, 15, 19, 255), 10.0f);
    if (image.ok)
        draw->AddImageRounded(ImTextureRef(static_cast<ImTextureID>(image.texture)), origin, max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f),
                              IM_COL32(255, 255, 255, 255), 10.0f);
    draw->AddRect(origin, max, ui::color::Border, 10.0f);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("canvas", size);
    const ImVec2 afterCanvas = ImGui::GetCursorScreenPos();
    drawCanvasMenu(app);
    // El juego (1280x720) encajado y centrado, como lo dibuja el renderer
    // (GlRenderer.cpp:1315-1316).
    const float fit = std::min(size.x / 1280.0f, size.y / 720.0f);
    const ImVec2 game(origin.x + size.x * 0.5f - 640.0f * fit, origin.y + size.y * 0.5f - 360.0f * fit);
    const bool showScore = (app.manual || app.hudEdit) && app.score.visible;
    ImRect scoreRect;
    if (showScore) scoreRect = drawScoreboard(app, game, fit);
    if (app.hudEdit && style) drawHudHandles(app, *style, game, fit, scoreRect, showScore);
    if (app.manual && app.finished && !app.hudEdit) drawRunResults(app, origin, size);
    ImGui::SetCursorScreenPos(afterCanvas);
    if (!image.ok && !style)
        draw->AddText(ImVec2(origin.x + 16.0f, origin.y + 16.0f), ui::color::Muted, tr(app, "Pick a note style on the left.", "Elige un estilo de notas a la izquierda."));
    if (app.songIndex < 0)
        ui::caption(tr(app, "Demo pattern: the two sides take turns every bar. Common layout, not each engine's exact HUD.",
                            "Patrón de prueba: los dos lados se turnan cada compás. Colocación común, no el HUD exacto de cada motor."));
    else {
        const std::string text = app.songLabel + "  ·  " + std::to_string(app.notes.size()) +
                                 tr(app, " notes (opponent and player lines)", " notas (líneas del rival y del jugador)");
        ui::caption(text.c_str());
    }
}

// ------------------------------------------------------------ visor de assets --

void drawChecker(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    draw->AddRectFilled(min, max, IM_COL32(34, 37, 45, 255));
    const float cell = 12.0f;
    for (float y = min.y; y < max.y; y += cell)
        for (float x = min.x + (static_cast<int>((y - min.y) / cell) % 2) * cell; x < max.x; x += cell * 2.0f)
            draw->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, max.x), std::min(y + cell, max.y)), IM_COL32(44, 48, 58, 255));
}

std::vector<size_t> framesOfAnimation(const SparrowAtlas& atlas, const Animation& anim) {
    std::vector<std::string> prefixes{anim.prefix};
    prefixes.insert(prefixes.end(), anim.alternatives.begin(), anim.alternatives.end());
    for (const std::string& prefix : prefixes) {
        if (prefix.empty()) continue;
        const std::vector<size_t> frames = atlas.framesFor(prefix);
        if (!frames.empty()) return frames;
    }
    return {};
}

// Las cuatro notas de un estilo, como las veria el motor (con su paleta en
// Psych), para las tarjetas de los tipos de nota.
void drawNoteThumbs(NoteLabApp& app, const NoteStyle& style, float cell) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    for (int lane = 0; lane < 4; ++lane) {
        const ImVec2 a(start.x + lane * (cell + 8.0f), start.y);
        const ImVec2 b(a.x + cell, a.y + cell);
        draw->AddRectFilled(a, b, IM_COL32(26, 29, 37, 255), 8.0f);
        const PartBinding* binding = findPart(style, Part::Note, lane);
        if (!binding || binding->sheet < 0 || static_cast<size_t>(binding->sheet) >= style.sheets.size()) continue;
        const Sheet& sheet = style.sheets[static_cast<size_t>(binding->sheet)];
        if (sheet.image.empty() || sheet.atlas.empty()) continue;
        std::string key = psychRgbTexture(app, style, sheet, Part::Note, lane);
        if (key.empty()) key = sheet.image;
        const GlRenderer::PreviewImage image = app.renderer.previewImage(key);
        const SparrowAtlas* atlas = app.atlases.get(sheet.atlas);
        if (!image.ok || !atlas) continue;
        std::vector<size_t> frames = framesOfAnimation(*atlas, binding->animation);
        // Solo para la miniatura: un fork que nombra sus fotogramas por
        // direccion (`left`, `down`...) se ve igualmente; validarlo es otra cosa.
        if (frames.empty() && (style.forkNaming || style.letteredNaming)) frames = atlas->framesFor(directionKey(lane));
        if (frames.empty()) continue;
        const AtlasFrame& frame = atlas->frames[frames.front()];
        const float w = static_cast<float>(frame.rotated ? frame.h : frame.w);
        const float h = static_cast<float>(frame.rotated ? frame.w : frame.h);
        const float fit = std::min((cell - 12.0f) / std::max(1.0f, w), (cell - 12.0f) / std::max(1.0f, h));
        const ImVec2 size(w * fit, h * fit);
        const ImVec2 at(a.x + (cell - size.x) * 0.5f, a.y + (cell - size.y) * 0.5f);
        const ImVec2 uv0(frame.x / static_cast<float>(image.width), frame.y / static_cast<float>(image.height));
        const ImVec2 uv1((frame.x + w) / static_cast<float>(image.width), (frame.y + h) / static_cast<float>(image.height));
        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(image.texture)), at, ImVec2(at.x + size.x, at.y + size.y), uv0, uv1);
    }
    ImGui::Dummy(ImVec2(cell * 4.0f + 24.0f, cell));
}

std::string hudName(const NoteLabApp& app, int index);

// Todo lo de un mod en la vista de assets: las hojas y las imagenes del HUD de
// cada estilo, una vez cada una; al elegir una se abre con su estilo.
void drawModAssetList(NoteLabApp& app) {
    Source* source = selectedSource(app);
    if (!source) return;
    std::set<std::string> seen;
    int pick = -1, pickKind = 0, pickIndex = 0;
    for (size_t s = 0; s < source->catalog.styles.size(); ++s) {
        const NoteStyle& style = source->catalog.styles[s];
        if (!styleVisible(app, *source, style)) continue;
        bool titled = false;
        auto row = [&](const std::string& image, const std::string& label, int kind, int index) {
            if (!titled) {
                titled = true;
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
                ImGui::TextUnformatted(style.name.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PushID(static_cast<int>(s * 1000 + kind * 500 + index));
            const GlRenderer::PreviewImage thumb = app.renderer.previewImage(image);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            drawChecker(ImGui::GetWindowDrawList(), at, ImVec2(at.x + 24.0f, at.y + 24.0f));
            if (thumb.ok && thumb.width > 0 && thumb.height > 0) {
                const float fit = 24.0f / static_cast<float>(std::max(thumb.width, thumb.height));
                const float w = thumb.width * fit, h = thumb.height * fit;
                ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(thumb.texture)),
                                                     ImVec2(at.x + (24.0f - w) * 0.5f, at.y + (24.0f - h) * 0.5f),
                                                     ImVec2(at.x + (24.0f + w) * 0.5f, at.y + (24.0f + h) * 0.5f));
            }
            ImGui::Dummy(ImVec2(24.0f, 24.0f));
            ImGui::SameLine(0.0f, 6.0f);
            const bool chosen = app.selStyle == static_cast<int>(s) && app.assetKind == kind && app.assetIndex == index;
            if (ImGui::Selectable(label.c_str(), chosen, 0, ImVec2(0.0f, 24.0f))) {
                pick = static_cast<int>(s);
                pickKind = kind;
                pickIndex = index;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", image.c_str());
            ImGui::PopID();
        };
        for (size_t i = 0; i < style.sheets.size(); ++i) {
            const Sheet& sheet = style.sheets[i];
            if (sheet.image.empty() || !seen.insert(sheet.image).second) continue;
            row(sheet.image, sheet.declared.empty() ? pathFromUtf8(sheet.image).filename().u8string() : sheet.declared, 0, static_cast<int>(i));
        }
        if (!style.hasHud) continue;
        // En el orden de la lista del HUD de drawAssetsTab.
        std::vector<std::pair<const HudAsset*, int>> hud;
        for (int i = 0; i < 4; ++i) hud.push_back({&style.judgements[static_cast<size_t>(i)], i});
        for (int i = 0; i < 4; ++i) hud.push_back({&style.countdown[static_cast<size_t>(i)], 15 + i});
        hud.push_back({&style.combo, 4});
        for (int i = 0; i < 10; ++i) hud.push_back({&style.digits[static_cast<size_t>(i)], 5 + i});
        for (size_t k = 0; k < hud.size(); ++k) {
            const HudAsset* asset = hud[k].first;
            if (asset->image.empty() || !seen.insert(asset->image).second) continue;
            row(asset->image, hudName(app, hud[k].second), 1, static_cast<int>(k));
        }
    }
    if (pick >= 0) {
        if (pick != app.selStyle) selectStyle(app, app.selSource, pick);
        app.assetKind = pickKind;
        app.assetIndex = pickIndex;
        app.assetPart = -1;
        app.assetFit = true;
        app.assetAnim.clear();
    }
}

void drawAssetsTab(NoteLabApp& app) {
    NoteStyle* style = mutableStyle(app);
    if (!style || !app.rendererReady || app.renderSource != app.selSource) {
        ui::caption(tr(app, "Pick a note style on the left.", "Elige un estilo de notas a la izquierda."));
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::vec(ui::color::Raised));
    ImGui::BeginChild("assetlist", ImVec2(260.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    const char* scopes[] = {tr(app, "This style", "Este estilo"), tr(app, "Whole mod", "Todo el mod")};
    ui::segmented("assetscope", &app.assetScope, scopes, 2);
    ImGui::SameLine();
    helpButton(app, "assetshelp", 4);
    if (app.assetScope == 1) {
        drawModAssetList(app);
        style = mutableStyle(app);
        if (!style) {
            ImGui::EndChild();
            return;
        }
    }
    if (app.assetScope == 0) ui::sectionHeader(tr(app, "Sheets", "Hojas"), ui::icon::Photo);
    for (size_t i = 0; app.assetScope == 0 && i < style->sheets.size(); ++i) {
        const Sheet& sheet = style->sheets[i];
        ImGui::PushID(static_cast<int>(i));
        std::string label = sheet.declared.empty() ? sheet.image : sheet.declared;
        if (sheet.image.empty()) label += tr(app, " (missing)", " (falta)");
        if (ImGui::Selectable(label.c_str(), app.assetKind == 0 && app.assetIndex == static_cast<int>(i))) {
            app.assetKind = 0;
            app.assetIndex = static_cast<int>(i);
            app.assetPart = -1;
            app.assetFit = true;
        }
        if (ImGui::BeginPopupContextItem("sheetmenu")) {
            if (menuItem(ui::icon::Copy, tr(app, "Copy the image path", "Copiar la ruta de la imagen"), nullptr, false, !sheet.image.empty()))
                copyText(app, sheet.image);
            if (menuItem(ui::icon::Copy, tr(app, "Copy the atlas path", "Copiar la ruta del atlas"), nullptr, false, !sheet.atlas.empty()))
                copyText(app, sheet.atlas);
            if (menuItem(ui::icon::FolderOpen, tr(app, "Show in folder", "Mostrar en la carpeta"), nullptr, false, !sheet.image.empty()))
                if (const Source* owner = selectedSource(app)) revealVirtual(app, *owner, sheet.image);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    struct HudRef { const HudAsset* asset; std::string label; };
    std::vector<HudRef> hud;
    if (style->hasHud) {
        for (int i = 0; i < 4; ++i) hud.push_back({&style->judgements[i], hudName(app, i)});
        for (int i = 0; i < 4; ++i) hud.push_back({&style->countdown[i], hudName(app, 15 + i)});
        hud.push_back({&style->combo, hudName(app, 4)});
        for (int i = 0; i < 10; ++i) hud.push_back({&style->digits[i], hudName(app, 5 + i)});
    }
    bool hudTitle = false;
    for (size_t i = 0; app.assetScope == 0 && i < hud.size(); ++i) {
        if (hud[i].asset->image.empty()) continue;
        if (!hudTitle) { ui::sectionHeader("HUD", ui::icon::Layers); hudTitle = true; }
        ImGui::PushID(1000 + static_cast<int>(i));
        if (ImGui::Selectable(hud[i].label.c_str(), app.assetKind == 1 && app.assetIndex == static_cast<int>(i))) {
            app.assetKind = 1;
            app.assetIndex = static_cast<int>(i);
            app.assetFit = true;
        }
        ImGui::PopID();
    }
    // Las animaciones de la hoja elegida: cada prefijo con sus fotogramas; al
    // elegir una se resaltan en la hoja.
    if (app.assetKind == 0 && app.assetIndex >= 0 && app.assetIndex < static_cast<int>(style->sheets.size())) {
        const Sheet& chosen = style->sheets[static_cast<size_t>(app.assetIndex)];
        const SparrowAtlas* atlas = chosen.atlas.empty() ? nullptr : app.atlases.get(chosen.atlas);
        if (atlas) {
            std::vector<std::pair<std::string, int>> anims;
            std::map<std::string, size_t> index;
            for (const AtlasFrame& frame : atlas->frames) {
                const std::string base = SparrowAtlas::stripFrameNumber(frame.name);
                const auto found = index.find(base);
                if (found != index.end()) ++anims[found->second].second;
                else {
                    index.emplace(base, anims.size());
                    anims.push_back({base, 1});
                }
            }
            ui::sectionHeader((std::string(tr(app, "Animations", "Animaciones")) + " · " + std::to_string(anims.size())).c_str(), ui::icon::Play);
            for (const auto& anim : anims) {
                ImGui::PushID(anim.first.c_str());
                const std::string label = anim.first + "  (" + std::to_string(anim.second) + ")";
                if (ImGui::Selectable(label.c_str(), app.assetAnim == anim.first)) app.assetAnim = app.assetAnim == anim.first ? std::string() : anim.first;
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("assetview", ImVec2(0.0f, 0.0f));
    std::string image;
    Sheet* sheet = nullptr;
    if (app.assetKind == 0 && app.assetIndex >= 0 && app.assetIndex < static_cast<int>(style->sheets.size())) {
        sheet = &style->sheets[static_cast<size_t>(app.assetIndex)];
        image = sheet->image;
    } else if (app.assetKind == 1 && app.assetIndex >= 0 && app.assetIndex < static_cast<int>(hud.size())) {
        image = hud[static_cast<size_t>(app.assetIndex)].asset->image;
    }
    if (image.empty()) {
        ui::caption(tr(app, "This asset is not in the opened folders.", "Este asset no está en las carpetas abiertas."));
        ImGui::EndChild();
        return;
    }
    const GlRenderer::PreviewImage preview = app.renderer.previewImage(image);
    if (ui::iconButton("fit", ui::icon::Fit, tr(app, "Fit", "Encajar"), tr(app, "Fit to the view", "Encajar en la vista"))) app.assetFit = true;
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::iconButton("zoomout", ui::icon::ZoomOut, "-", tr(app, "Zoom out", "Alejar"))) {
        app.assetZoom = std::max(0.02f, app.assetZoom / 1.25f);
        app.assetFit = false;
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::SliderFloat("##zoom", &app.assetZoom, 0.05f, 8.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) app.assetFit = false;
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::iconButton("zoomin", ui::icon::ZoomIn, "+", tr(app, "Zoom in", "Acercar"))) {
        app.assetZoom = std::min(16.0f, app.assetZoom * 1.25f);
        app.assetFit = false;
    }
    ImGui::SameLine();
    if (ui::iconButton("frames", ui::icon::Grid, tr(app, "Frames", "Fotogramas"), tr(app, "Show the frames of the atlas", "Ver los fotogramas del atlas"),
                       app.assetFrames))
        app.assetFrames = !app.assetFrames;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ui::fonts().mono, 13.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    if (preview.ok) {
        ImGui::Text("%d x %d", preview.width, preview.height);
        ImGui::SameLine();
    }
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float room = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->PushClipRect(at, ImVec2(at.x + room, at.y + ImGui::GetFrameHeight()), true);
        ImGui::TextUnformatted(image.c_str());
        ImGui::GetWindowDrawList()->PopClipRect();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", image.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
    const bool picking = sheet && app.assetPart >= 0 && sheet->kind != SheetKind::Strip && sheet->kind != SheetKind::Grid;
    if (picking) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Warning));
        ImGui::TextUnformatted(ui::label(ui::icon::Edit, tr(app, "Click a frame to use its animation for the selected piece",
                                                             "Haz clic en un fotograma para usar su animación en la pieza elegida")).c_str());
        ImGui::PopStyleColor();
    }

    std::vector<size_t> sheetParts;
    if (sheet)
        for (size_t i = 0; i < style->parts.size(); ++i)
            if (style->parts[i].sheet == app.assetIndex) sheetParts.push_back(i);
    const float partsHeight = sheetParts.empty() ? 0.0f : std::min(200.0f, 34.0f + sheetParts.size() * ImGui::GetTextLineHeightWithSpacing());
    const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, std::max(80.0f, ImGui::GetContentRegionAvail().y - partsHeight));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool activated = ImGui::IsItemActivated();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 canvasMax(origin.x + canvasSize.x, origin.y + canvasSize.y);
    draw->PushClipRect(origin, canvasMax, true);
    drawChecker(draw, origin, canvasMax);
    const SparrowAtlas* atlas = sheet && !sheet->atlas.empty() ? app.atlases.get(sheet->atlas) : nullptr;
    if (preview.ok) {
        if (app.assetFit) {
            app.assetZoom = std::min(canvasSize.x / preview.width, canvasSize.y / preview.height) * 0.95f;
            app.assetPan = ImVec2(0.0f, 0.0f);
        }
        ImGuiIO& io = ImGui::GetIO();
        if (hovered && io.MouseWheel != 0.0f) {
            app.assetZoom = std::clamp(app.assetZoom * (io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f), 0.02f, 16.0f);
            app.assetFit = false;
        }
        if (activated) app.assetDragged = false;
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 3.0f))) {
            app.assetPan.x += io.MouseDelta.x;
            app.assetPan.y += io.MouseDelta.y;
            app.assetFit = false;
            app.assetDragged = true;
        }
        const float z = app.assetZoom;
        const ImVec2 imageMin(origin.x + (canvasSize.x - preview.width * z) * 0.5f + app.assetPan.x,
                              origin.y + (canvasSize.y - preview.height * z) * 0.5f + app.assetPan.y);
        const ImVec2 imageMax(imageMin.x + preview.width * z, imageMin.y + preview.height * z);
        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(preview.texture)), imageMin, imageMax);
        std::string hoverName;
        if (sheet && app.assetFrames) {
            if (sheet->kind == SheetKind::Strip && sheet->columns > 0) {
                for (int c = 1; c < sheet->columns; ++c) {
                    const float x = imageMin.x + preview.width * z * c / sheet->columns;
                    draw->AddLine(ImVec2(x, imageMin.y), ImVec2(x, imageMax.y), IM_COL32(255, 210, 90, 160));
                }
            } else if (sheet->kind == SheetKind::Grid && sheet->columns > 0 && sheet->rows > 0) {
                // Las celdas como las numera el motor (FlxTileFrames.hx:296-305);
                // en amarillo las de la pieza elegida.
                const int cellW = preview.width / sheet->columns, cellH = preview.height / sheet->rows;
                std::set<int> highlighted;
                if (app.assetPart >= 0 && app.assetPart < static_cast<int>(style->parts.size()))
                    for (int cell : style->parts[static_cast<size_t>(app.assetPart)].animation.indices) highlighted.insert(cell);
                if (cellW > 0 && cellH > 0) {
                    const int perRow = preview.width / cellW, perColumn = preview.height / cellH;
                    for (int cell = 0; cell < perRow * perColumn; ++cell) {
                        const ImVec2 a(imageMin.x + (cell % perRow) * cellW * z, imageMin.y + (cell / perRow) * cellH * z);
                        const ImVec2 b(a.x + cellW * z, a.y + cellH * z);
                        const bool lit = highlighted.count(cell) > 0;
                        const bool over = hovered && ImGui::IsMouseHoveringRect(a, b);
                        draw->AddRect(a, b, lit ? IM_COL32(255, 210, 90, 255) : over ? IM_COL32(200, 215, 255, 230) : IM_COL32(150, 170, 255, 70),
                                      0.0f, 0, lit || over ? 2.0f : 1.0f);
                        if (over) hoverName = std::string(tr(app, "cell ", "celda ")) + std::to_string(cell);
                    }
                }
            } else if (atlas) {
                std::set<size_t> highlighted;
                if (app.assetPart >= 0 && app.assetPart < static_cast<int>(style->parts.size())) {
                    const std::vector<size_t> frames = framesOfAnimation(*atlas, style->parts[static_cast<size_t>(app.assetPart)].animation);
                    highlighted.insert(frames.begin(), frames.end());
                }
                // La animacion elegida en la lista de la izquierda.
                if (!app.assetAnim.empty())
                    for (size_t f = 0; f < atlas->frames.size(); ++f)
                        if (SparrowAtlas::stripFrameNumber(atlas->frames[f].name) == app.assetAnim) highlighted.insert(f);
                for (size_t f = 0; f < atlas->frames.size(); ++f) {
                    const AtlasFrame& frame = atlas->frames[f];
                    const float w = static_cast<float>(frame.rotated ? frame.h : frame.w);
                    const float h = static_cast<float>(frame.rotated ? frame.w : frame.h);
                    const ImVec2 a(imageMin.x + frame.x * z, imageMin.y + frame.y * z);
                    const ImVec2 b(a.x + w * z, a.y + h * z);
                    const bool lit = highlighted.count(f) > 0;
                    const bool over = hovered && ImGui::IsMouseHoveringRect(a, b);
                    draw->AddRect(a, b, lit ? IM_COL32(255, 210, 90, 255) : over ? IM_COL32(200, 215, 255, 230) : IM_COL32(150, 170, 255, 70),
                                  0.0f, 0, lit || over ? 2.0f : 1.0f);
                    if (over) hoverName = frame.name;
                }
            }
        }
        if (!hoverName.empty()) {
            ImGui::SetTooltip("%s", hoverName.c_str());
            // Elegir un fotograma = usar su animacion en la pieza elegida.
            if (picking && atlas && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !app.assetDragged && hovered) {
                const std::string prefix = uniquePrefixFor(*atlas, hoverName);
                const std::string subject = partSubject(style->parts[static_cast<size_t>(app.assetPart)]);
                if (style->parts[static_cast<size_t>(app.assetPart)].animation.prefix != prefix) {
                    beginEdit(app);
                    style = mutableStyle(app);
                    Animation& animation = style->parts[static_cast<size_t>(app.assetPart)].animation;
                    animation.prefix = prefix;
                    animation.alternatives.clear();
                    animation.indices.clear();
                    afterEdit(app);
                    app.buffersFor.clear();
                    setStatus(app, subject + " now uses \"" + prefix + "\".", subject + " usa ahora «" + prefix + "».");
                }
            }
        }
        // Clic derecho sobre un fotograma: sus acciones.
        if (!hoverName.empty() && hovered && atlas && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            app.frameMenu = hoverName;
            ImGui::OpenPopup("framemenu");
        }
    } else {
        draw->AddText(ImVec2(origin.x + 12.0f, origin.y + 12.0f), ui::color::Muted, tr(app, "Loading…", "Cargando…"));
    }
    draw->PopClipRect();
    draw->AddRect(origin, canvasMax, ui::color::Border, 6.0f);
    if (ImGui::BeginPopup("framemenu")) {
        ui::caption(app.frameMenu.c_str());
        if (menuItem(ui::icon::Copy, tr(app, "Copy the frame name", "Copiar el nombre del fotograma"))) copyText(app, app.frameMenu);
        if (menuItem(ui::icon::Copy, tr(app, "Copy the prefix that selects it", "Copiar el prefijo que lo elige"), nullptr, false, atlas != nullptr))
            copyText(app, uniquePrefixFor(*atlas, app.frameMenu));
        if (menuItem(ui::icon::Edit, tr(app, "Use it for the selected piece", "Usarlo en la pieza elegida"), nullptr, false, picking && atlas != nullptr)) {
            const std::string prefix = uniquePrefixFor(*atlas, app.frameMenu);
            beginEdit(app);
            style = mutableStyle(app);
            Animation& animation = style->parts[static_cast<size_t>(app.assetPart)].animation;
            animation.prefix = prefix;
            animation.alternatives.clear();
            animation.indices.clear();
            afterEdit(app);
            app.buffersFor.clear();
        }
        ImGui::EndPopup();
    }

    if (!sheetParts.empty()) {
        const float thumb = 96.0f;
        ImGui::BeginChild("assetparts", ImVec2(std::max(160.0f, ImGui::GetContentRegionAvail().x - thumb - 26.0f), 0.0f));
        if (ImGui::BeginTable("partstable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY |
                                                   ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(tr(app, "Piece", "Pieza"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(tr(app, "Prefix", "Prefijo"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn(tr(app, "Frames", "Fotogramas"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();
            const StyleReport* report = selectedReport(app);
            for (size_t i : sheetParts) {
                const PartBinding& part = style->parts[i];
                const int frames = report && i < report->parts.size() ? report->parts[i].frames : 0;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                std::string readable = std::string(partLabel(part.part, app.spanish)) + " " + directionLabel(part.direction, app.spanish);
                if (part.variant > 0) readable += " " + std::to_string(part.variant + 1);
                if (ImGui::Selectable(readable.c_str(), app.assetPart == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    app.assetPart = static_cast<int>(i);
                    app.editPart = static_cast<int>(i);
                    app.editHud = -1;
                    app.assetClockMs = 0.0;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s\n%s", partHelp(part.part, app.spanish), partSubject(part).c_str());
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::PushFont(ui::fonts().mono, 12.5f);
                ImGui::TextUnformatted(part.animation.prefix.empty() ? "-" : part.animation.prefix.c_str());
                ImGui::PopFont();
                ImGui::TableNextColumn();
                if (frames > 0) ImGui::Text("%d%s", frames, part.inherited ? " *" : "");
                else ImGui::TextColored(ui::vec(ui::color::Error), "0");
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::vec(ui::color::Raised));
        ImGui::BeginChild("assetthumb", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();
        if (app.assetPart >= 0 && app.assetPart < static_cast<int>(style->parts.size()) && preview.ok && atlas) {
            const PartBinding& part = style->parts[static_cast<size_t>(app.assetPart)];
            app.assetClockMs += ImGui::GetIO().DeltaTime * 1000.0;
            const std::vector<size_t> frames = framesOfAnimation(*atlas, part.animation);
            if (!frames.empty()) {
                const float fps = part.animation.fps > 0.0f ? part.animation.fps : 24.0f;
                const size_t index = static_cast<size_t>(app.assetClockMs * fps / 1000.0) % frames.size();
                const AtlasFrame& frame = atlas->frames[frames[index]];
                const float w = static_cast<float>(frame.rotated ? frame.h : frame.w);
                const float h = static_cast<float>(frame.rotated ? frame.w : frame.h);
                const float fit = std::min(thumb / std::max(1.0f, w), thumb / std::max(1.0f, h));
                const ImVec2 uv0(frame.x / static_cast<float>(preview.width), frame.y / static_cast<float>(preview.height));
                const ImVec2 uv1((frame.x + w) / static_cast<float>(preview.width), (frame.y + h) / static_cast<float>(preview.height));
                const ImVec2 at = ImGui::GetCursorScreenPos();
                drawChecker(ImGui::GetWindowDrawList(), at, ImVec2(at.x + thumb, at.y + thumb));
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(preview.texture)), ImVec2(w * fit, h * fit), uv0, uv1);
                ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + thumb + 6.0f));
                ui::caption((std::to_string(index + 1) + "/" + std::to_string(frames.size()) + " · " +
                             std::to_string(static_cast<int>(fps)) + " fps").c_str());
            } else {
                ui::caption(tr(app, "No frames: pick one on the sheet.", "Sin fotogramas: elige uno en la hoja."));
            }
        } else {
            ui::caption(tr(app, "Pick a piece to animate it.", "Elige una pieza para animarla."));
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

// ------------------------------------------------------ notas custom --

const char* botText(const NoteLabApp& app, BotRule rule) {
    switch (rule) {
        case BotRule::Hits: return tr(app, "The bot hits it", "El bot la toca");
        case BotRule::Ignores: return tr(app, "The bot lets it pass", "El bot la deja pasar");
        case BotRule::IgnoresOnPlayerSide: return tr(app, "The bot lets it pass on the player's side; the opponent hits it",
                                                    "El bot la deja pasar en el lado del jugador; el rival la toca");
    }
    return "";
}

// Lo que hacen los tipos de serie de Psych (Note.hx:199-228, PlayState.hx).
const char* builtinText(const NoteLabApp& app, const std::string& name) {
    const std::string key = lowerText(name);
    if (key == "hurt note")
        return tr(app, "Built into Psych: a black and red recolor of the note skin; hitting it counts as a miss and drains health, "
                       "letting it pass is fine. It splashes with noteSplashes-electric.",
                  "De serie en Psych: recolorea el skin de notas en negro y rojo; tocarla cuenta como fallo y quita vida, dejarla pasar "
                  "no. Salpica con noteSplashes-electric.");
    if (key == "alt animation")
        return tr(app, "Built into Psych: the singer plays the -alt version of the animation.",
                  "De serie en Psych: quien canta usa la versión -alt de la animación.");
    if (key == "hey!")
        return tr(app, "Built into Psych: the character plays its \"hey\" animation.",
                  "De serie en Psych: el personaje hace su animación de «hey».");
    if (key == "gf sing")
        return tr(app, "Built into Psych: Girlfriend sings the note instead of the character of that side.",
                  "De serie en Psych: la nota la canta Girlfriend en vez del personaje de ese lado.");
    if (key == "no animation")
        return tr(app, "Built into Psych: nobody animates, neither on hit nor on miss.",
                  "De serie en Psych: nadie se anima, ni al acertar ni al fallar.");
    return tr(app, "Built into the engine.", "De serie en el motor.");
}

bool isKeyword(const std::string& word, bool lua) {
    static const std::set<std::string> luaWords{"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
                                                "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
    static const std::set<std::string> haxeWords{"var", "final", "function", "return", "if", "else", "for", "while", "do", "switch",
                                                 "case", "default", "break", "continue", "new", "class", "extends", "implements", "import",
                                                 "package", "static", "public", "private", "override", "inline", "true", "false", "null",
                                                 "this", "super", "try", "catch", "throw", "cast", "in"};
    return lua ? luaWords.count(word) > 0 : haxeWords.count(word) > 0;
}

// El script, con numeros de linea y un coloreado sencillo (comentarios,
// cadenas, numeros y palabras clave) como el de un editor.
// Texto en lineas para el visor de codigo, con los tabuladores a cuatro espacios.
std::vector<std::string> codeLinesOf(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string expanded;
        for (char c : line) {
            if (c == '\t') expanded += "    ";
            else expanded += c;
        }
        lines.push_back(std::move(expanded));
        if (end == text.size()) break;
        start = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

void drawCodeLines(NoteLabApp& app, const char* id, const std::vector<std::string>& codeLines, const std::string& path,
                   float height, const Source* source);

void drawCodeView(NoteLabApp& app, const Source& source, float height) {
    drawCodeLines(app, "code", app.codeLines, app.codePath, height, &source);
}

// Codigo con numeros de linea y colores (HScript, Lua, .hxc y el .txt de
// Psych). `source` da «Mostrar en la carpeta» cuando es un archivo del mod.
void drawCodeLines(NoteLabApp& app, const char* id, const std::vector<std::string>& codeLines, const std::string& path,
                   float height, const Source* source) {
    const bool lua = endsWithText(lowerText(path), ".lua");
    const bool config = endsWithText(lowerText(path), ".txt");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.070f, 0.078f, 0.098f, 1.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleColor();
    ImGui::PushFont(ui::fonts().mono, 14.0f);
    // El avance exacto: CalcTextSize redondea hacia arriba y abriria huecos entre fichas.
    const float charWidth = ImGui::GetFontBaked()->GetCharAdvance(static_cast<ImWchar>('M'));
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const int digits = static_cast<int>(std::to_string(std::max<size_t>(1, codeLines.size())).size());
    const float gutter = charWidth * digits + 18.0f;
    size_t longest = 0;
    for (const std::string& line : codeLines) longest = std::max(longest, line.size());
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(codeLines.size()), lineHeight);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 comment = IM_COL32(106, 153, 85, 255), string = IM_COL32(206, 145, 120, 255),
                number = IM_COL32(181, 206, 168, 255), keyword = IM_COL32(197, 134, 192, 255), text = IM_COL32(212, 212, 212, 255);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::string& line = codeLines[static_cast<size_t>(row)];
            const ImVec2 at = ImGui::GetCursorScreenPos();
            char lineNo[16];
            std::snprintf(lineNo, sizeof(lineNo), "%*d", digits, row + 1);
            draw->AddText(at, ui::color::Faint, lineNo);
            const float x0 = at.x + gutter;
            size_t i = 0;
            while (i < line.size()) {
                const char c = line[i];
                size_t end = i + 1;
                ImU32 tint = text;
                const bool commentStart = config ? false : (lua ? (c == '-' && i + 1 < line.size() && line[i + 1] == '-')
                                                                : (c == '/' && i + 1 < line.size() && line[i + 1] == '/'));
                if (commentStart) {
                    end = line.size();
                    tint = comment;
                } else if (c == '\'' || c == '"') {
                    const size_t close = line.find(c, i + 1);
                    end = close == std::string::npos ? line.size() : close + 1;
                    tint = string;
                } else if (std::isdigit(static_cast<unsigned char>(c))) {
                    while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '.')) ++end;
                    tint = number;
                } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) ++end;
                    if (!config && isKeyword(line.substr(i, end - i), lua)) tint = keyword;
                }
                const std::string token = line.substr(i, end - i);
                draw->AddText(ImVec2(x0 + static_cast<float>(i) * charWidth, at.y), tint, token.c_str());
                i = end;
            }
            ImGui::Dummy(ImVec2(gutter + static_cast<float>(longest) * charWidth, lineHeight - ImGui::GetStyle().ItemSpacing.y));
        }
    }
    ImGui::PopFont();
    if (ImGui::BeginPopupContextWindow("codemenu")) {
        if (menuItem(ui::icon::Copy, tr(app, "Copy all the code", "Copiar todo el código"))) {
            std::string all;
            for (const std::string& line : codeLines) all += line + "\n";
            copyText(app, all);
        }
        if (menuItem(ui::icon::Copy, tr(app, "Copy the file path", "Copiar la ruta del archivo"))) copyText(app, path);
        if (source && menuItem(ui::icon::FolderOpen, tr(app, "Show in folder", "Mostrar en la carpeta"))) revealVirtual(app, *source, path);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}

void loadCode(NoteLabApp& app, const Source& source, const std::string& path) {
    const std::string key = source.root.u8string() + "|" + path;
    if (app.codeKey == key) return;
    app.codeKey = key;
    app.codePath = path;
    app.codeLines.clear();
    if (path.empty()) return;
    const auto entry = source.vfs->find(path);
    if (!entry || entry->size > 2ull * 1024ull * 1024ull) {
        app.codeLines.push_back(entry ? "(archivo demasiado grande para verlo aqui)" : "(no se pudo leer)");
        return;
    }
    const auto text = source.vfs->readText(*entry);
    if (!text) return;
    size_t start = 0;
    while (start <= text->size()) {
        size_t end = text->find('\n', start);
        if (end == std::string::npos) end = text->size();
        std::string line = text->substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string expanded;
        for (char c : line) {
            if (c == '\t') expanded += "    ";
            else expanded += c;
        }
        app.codeLines.push_back(std::move(expanded));
        start = end + 1;
    }
}

// Un tipo sale en el catalogo si es del motor de su mod, si se piden los de
// otros motores o si el motor del mod no se sabe.
bool typeVisible(const NoteLabApp& app, const Source& source, const NoteTypeEntry& type) {
    Engine engine;
    return app.showOtherEngines || !sourceEngine(app, source, engine) || type.engine == engine;
}

bool validType(const NoteLabApp& app, int sourceIndex, int typeIndex) {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size())) return false;
    const Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    return typeIndex >= 0 && typeIndex < static_cast<int>(source.noteTypes.size()) &&
           typeVisible(app, source, source.noteTypes[static_cast<size_t>(typeIndex)]);
}

// El primer tipo visible, empezando por el mod elegido.
void firstType(NoteLabApp& app) {
    const int count = static_cast<int>(app.sources.size());
    const int start = app.selSource >= 0 && app.selSource < count ? app.selSource : 0;
    // Primero los tipos del propio mod y, si no tiene, los de su juego base.
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < count; ++k) {
            const int s = (start + k) % count;
            const Source& source = *app.sources[static_cast<size_t>(s)];
            for (size_t t = 0; t < source.noteTypes.size(); ++t) {
                const bool base = t < source.typeFromBase.size() && source.typeFromBase[t];
                if (base != (pass == 1) || !typeVisible(app, source, source.noteTypes[t])) continue;
                app.typeSource = s;
                app.selType = static_cast<int>(t);
                return;
            }
        }
    app.typeSource = -1;
    app.selType = -1;
}


// --------------------------------------------------------------- bloques --

ImU32 realizationColor(Realization how) {
    switch (how) {
        case Realization::Data: return ui::color::Success;
        case Realization::Script: return ui::color::Info;
        case Realization::Approx: return ui::color::Warning;
        case Realization::None: return ui::color::Error;
    }
    return ui::color::Faint;
}

// Tras cambiar los bloques: la vista previa los aplica y el proyecto cambia.
void blocksChanged(NoteLabApp& app) {
    app.dirty = true;
    applyTypeRules(app);
    app.exporting.preparedKey.clear();
}

// Exportar un tipo: con su aspecto si lo tiene; si no, solo sus bloques
// (se puede encender «Notas y sostenidos» para darle el del estilo elegido).
void openExportForType(NoteLabApp& app, int sourceIndex, const std::string& type) {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size())) return;
    Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    int look = -1;
    for (const NoteTypeEntry& entry : source.noteTypes)
        if (entry.name == type && !entry.lookStyle.empty())
            for (size_t i = 0; i < source.catalog.styles.size(); ++i)
                if (source.catalog.styles[i].id == entry.lookStyle) look = static_cast<int>(i);
    const auto own = source.typeLooks.find(type);
    if (own != source.typeLooks.end())
        for (size_t i = 0; i < source.catalog.styles.size(); ++i)
            if (source.catalog.styles[i].id == own->second) look = static_cast<int>(i);
    if (look >= 0) selectStyle(app, sourceIndex, look);
    else if (app.selSource != sourceIndex || !selectedStyle(app)) {
        const int first = firstVisibleStyle(app, source);
        if (first >= 0) selectStyle(app, sourceIndex, first);
    }
    if (!selectedStyle(app)) return;
    openExport(app);
    NoteLabApp::Exporting& e = app.exporting;
    e.options.role = ExportRole::NoteType;
    std::snprintf(e.noteType.data(), e.noteType.size(), "%s", type.c_str());
    std::snprintf(e.name.data(), e.name.size(), "%s", exportName(type).c_str());
    if (look < 0) {
        e.options.notes = false;
        e.options.splashes = false;
    }
    e.preparedKey.clear();
}

// Los sonidos del mod para «tocar sonido»: lo que haya en una carpeta
// sounds/, con el nombre que espera Paths.sound (sin la extension). Se leen
// una vez por montaje del mod.
const std::vector<std::string>& modSounds(Source& source) {
    if (source.soundsFrom != source.vfs.get()) {
        std::set<std::string> found;
        if (source.vfs)
            for (const Vfs::Entry& entry : source.vfs->allEntries()) {
                const std::string lower = lowerText(entry.virtualPath);
                const size_t at = lower.rfind("sounds/");
                if (at == std::string::npos || !endsWithText(lower, ".ogg")) continue;
                found.insert(entry.virtualPath.substr(at + 7, entry.virtualPath.size() - at - 7 - 4));
            }
        for (const auto& style : source.catalog.styles)
            for (const auto& sound : style.sounds) found.insert(sound.name);
        source.sounds.assign(found.begin(), found.end());
        source.soundsFrom = source.vfs.get();
    }
    return source.sounds;
}

// Un tipo que se puede editar en la vista Bloques: de un mod abierto o creado
// en Note Lab (solo esta en su programa).
#include "NewNote.hpp"

struct TypeChoice {
    int source = -1;
    std::string name;
    Engine engine = Engine::Codename;
    bool created = false;
    bool builtin = false;
};

std::vector<TypeChoice> blockTypeChoices(const NoteLabApp& app) {
    std::vector<TypeChoice> choices;
    for (size_t s = 0; s < app.sources.size(); ++s) {
        const Source& source = *app.sources[s];
        Engine engine;
        if (!sourceEngine(app, source, engine)) engine = Engine::Codename;
        std::set<std::string> seen;
        for (const NoteTypeEntry& type : source.noteTypes)
            if (typeVisible(app, source, type) && seen.insert(type.name).second)
                choices.push_back({static_cast<int>(s), type.name, type.engine, false, type.builtin});
        for (const auto& entry : source.typeBlocks)
            if (seen.insert(entry.first).second) choices.push_back({static_cast<int>(s), entry.first, engine, true, false});
    }
    return choices;
}

// El codigo que se exportaria en un motor, con sus avisos.
void drawBlocksCode(NoteLabApp& app, const std::string& type, BlockProgram& program, const BlockCode& code, Engine target, bool renamed) {
    int codeEngine = static_cast<int>(target);
    const char* engines[] = {"Codename", "Psych", "V-Slice"};
    const float controlRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float engineWidth = std::min(104.0f, std::max(1.0f, (ImGui::GetContentRegionAvail().x - 16.0f) / 3.0f));
    if (ui::segmented("blocksengine", &codeEngine, engines, 3, engineWidth)) app.blocksEngine = codeEngine;
    ui::caption(tr(app, "Edit code, then apply it to the blocks. Drafts are saved with the project.", "Edita el código y aplícalo a los bloques. Los borradores se guardan en el proyecto."));
    for (const BlockConflict& conflict : code.conflicts) {
        if (conflict.severity == Severity::Info && conflict.key.empty()) continue;
        const bool warning = conflict.severity != Severity::Info;
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(warning ? ui::color::Warning : ui::color::Info));
        ImGui::TextUnformatted(ui::fonts().icons ? (warning ? ui::icon::Warning : ui::icon::Info) : (warning ? "!" : "i"));
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextWrapped("%s%s", conflict.key.empty() ? "" : (blockName(conflict.key, app.spanish) + ": ").c_str(),
                           (app.spanish ? conflict.es : conflict.en).c_str());
    }
    if (renamed)
        ui::caption(tr(app, "The name has characters a file can't have: the engine wouldn't find it under the chart's name.",
                            "El nombre tiene caracteres que un archivo no admite: el motor no lo encontraría con el nombre del chart."));
    if (code.files.empty()) {
        ui::caption(tr(app, "Nothing to write in this engine yet: add blocks under an event.",
                            "Aún no hay nada que escribir en este motor: pon bloques bajo un evento."));
        return;
    }
    app.blocksFile = std::clamp(app.blocksFile, 0, static_cast<int>(code.files.size()) - 1);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##codefile", code.files[static_cast<size_t>(app.blocksFile)].path.c_str())) {
        for (size_t i = 0; i < code.files.size(); ++i)
            if (ImGui::Selectable(code.files[i].path.c_str(), app.blocksFile == static_cast<int>(i))) app.blocksFile = static_cast<int>(i);
        ImGui::EndCombo();
    }
    const BlockFile generated = code.files[static_cast<size_t>(app.blocksFile)];
    const std::string key = draftKey(target, generated.path);
    const std::string owner = std::to_string(app.blocksSource) + ":" + type + ":" + key;
    const auto pending = program.drafts.find(key);
    const std::string displayed = pending == program.drafts.end() ? generated.text : pending->second.text;
    if (owner != app.blockCodeOwner || displayed != app.blockCodeShown) {
        app.blockCodeOwner = owner; app.blockCodeShown = displayed;
        app.blockCodeBuffer.assign(262145, '\0');
        std::copy_n(displayed.begin(), std::min(displayed.size(), size_t(262144)), app.blockCodeBuffer.begin());
        app.blockCodeError = pending == program.drafts.end() ? std::string() : pending->second.error;
    }
    const bool hasDraft = program.drafts.count(key) != 0;
    auto continueControls = [&](const char* label) {
        const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= controlRight) ImGui::SameLine();
    };
    // Una nota «solo codigo»: su script se guarda tal cual (no hay bloques a los
    // que aplicarlo) y va con la nota a los charts y al exportar.
    if (codeOnlyType(program)) {
        ImGui::BeginDisabled(!hasDraft);
        const bool save = ui::primaryButton(ui::label(ui::icon::Check, tr(app, "Save code", "Guardar código")));
        ImGui::EndDisabled();
        tutorialMark("code-save");
        if (save) {
            const CodeImportResult result = keepCustomSource(target, program, generated.path, app.blockCodeBuffer.data());
            if (result.applied) {
                nlblocks::remember(app.canvas, program); program = result.program; blocksChanged(app);
                app.blockCodeOwner.clear(); app.blockCodeError.clear();
                setStatus(app, "Code saved with the project: it goes with the note to charts and exports.",
                               "Código guardado en el proyecto: va con la nota a los charts y al exportarla.");
            } else {
                app.blockCodeError = result.error + (result.line > 0 ? " (line " + std::to_string(result.line) + ")" : "");
                program.drafts[key].error = app.blockCodeError;
            }
        }
        continueControls(tr(app, "Discard changes", "Descartar cambios"));
        ImGui::BeginDisabled(!hasDraft);
        if (ImGui::Button(tr(app, "Discard changes", "Descartar cambios"))) {
            nlblocks::remember(app.canvas, program); program.drafts.erase(key); blocksChanged(app);
            app.blockCodeOwner.clear(); app.blockCodeError.clear();
        }
        ImGui::EndDisabled();
        const std::string copyCode = ui::label(ui::icon::Copy, tr(app, "Copy", "Copiar"));
        continueControls(copyCode.c_str());
        if (ImGui::SmallButton(copyCode.c_str())) copyText(app, displayed);
        if (!app.blockCodeError.empty()) ImGui::TextWrapped("%s", app.blockCodeError.c_str());
        ui::caption(hasDraft ? tr(app, "Unsaved changes: «Save code» keeps them (export waits until then).",
                                      "Cambios sin guardar: «Guardar código» los conserva (el export espera hasta entonces).")
                             : tr(app, "Code only: you write the script. Only basic syntax is checked; it is not run in the preview.",
                                      "Solo código: el script lo escribes tú. Solo se revisa la sintaxis básica; no se ejecuta en la vista previa."));
    } else {
    ImGui::BeginDisabled(!hasDraft);
    if (ui::primaryButton(tr(app, "Apply to blocks", "Aplicar a bloques"))) {
        const CodeImportResult result = applyBlockSource(target, type, pathFromUtf8(generated.path).stem().u8string(), program, generated.path, app.blockCodeBuffer.data());
        if (result.applied) {
            nlblocks::remember(app.canvas, program); program = result.program; blocksChanged(app);
            app.blockCodeOwner.clear(); app.blockCodeError.clear();
            setStatus(app, "Code applied: recognized changes and comments are now in the blocks.", "Código aplicado: los cambios reconocidos y comentarios ya están en los bloques.");
        } else {
            app.blockCodeError = result.error + (result.line > 0 ? " (line " + std::to_string(result.line) + ")" : "");
            program.drafts[key].error = app.blockCodeError;
        }
    }
    continueControls(tr(app, "Discard draft", "Descartar borrador"));
    if (ImGui::Button(tr(app, "Discard draft", "Descartar borrador"))) {
        nlblocks::remember(app.canvas, program); program.drafts.erase(key); blocksChanged(app);
        app.blockCodeOwner.clear(); app.blockCodeError.clear();
    }
    continueControls(tr(app, "Custom file…", "Archivo propio…"));
    if (ImGui::Button(tr(app, "Custom file…", "Archivo propio…"))) ImGui::OpenPopup("customcodeconfirm");
    ImGui::EndDisabled();
    const std::string copyLabel = ui::label(ui::icon::Copy, tr(app, "Copy", "Copiar"));
    continueControls(copyLabel.c_str());
    if (ImGui::SmallButton(ui::label(ui::icon::Copy, tr(app, "Copy", "Copiar")).c_str())) copyText(app, displayed);
    if (ImGui::BeginPopup("customcodeconfirm")) {
        ImGui::TextWrapped("%s", tr(app, "This file will override the generated file for this engine. It appears as a custom-file block and is NOT executed in the preview. Only basic syntax is checked, not engine compilation.",
            "Este archivo reemplazará al generado para este motor. Aparecerá como bloque de archivo propio y NO se ejecuta en la preview. Sólo se revisa sintaxis básica, no se compila con el motor."));
        if (ImGui::Button(tr(app, "Keep as custom file", "Conservar como archivo propio"))) {
            const CodeImportResult result = keepCustomSource(target, program, generated.path, app.blockCodeBuffer.data());
            if (result.applied) {
                nlblocks::remember(app.canvas, program); program = result.program; blocksChanged(app);
                app.blockCodeOwner.clear(); app.blockCodeError.clear(); ImGui::CloseCurrentPopup();
            } else { app.blockCodeError = result.error; program.drafts[key].error = result.error; }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(app, "Cancel", "Cancelar"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!app.blockCodeError.empty()) ImGui::TextWrapped("%s", app.blockCodeError.c_str());
    ui::caption(tr(app, "Export is blocked while a draft is pending. Automatic descriptions update; @user comments are preserved.",
        "El export se bloquea mientras haya un borrador. Las descripciones automáticas se actualizan; los comentarios @user se conservan."));
    }
    const bool tooLarge = displayed.size() > 262144;
    if (tooLarge) ui::caption(tr(app, "File exceeds the editable limit; copy it to an external editor.", "El archivo supera el límite editable; cópialo a un editor externo."));
    ImGui::PushFont(ui::fonts().mono, 14.0f);
    ImGui::PushID(owner.c_str());
    const bool changed = nlcode::edit(app.blockEditor, "##sourceeditor", app.blockCodeBuffer.data(), app.blockCodeBuffer.size(),
        ImVec2(-1.0f, std::max(100.0f, ImGui::GetContentRegionAvail().y - 8.0f)), target,
        endsWithText(lowerText(generated.path), ".txt"), ImGuiInputTextFlags_AllowTabInput | (tooLarge ? ImGuiInputTextFlags_ReadOnly : 0));
    ImGui::PopID();
    ImGui::PopFont();
    if (changed) {
        if (!hasDraft) nlblocks::remember(app.canvas, program);
        BlockDraft& draft = program.drafts[key];
        if (draft.path.empty()) { draft.engine = target; draft.path = generated.path; draft.baseline = generated.text; }
        draft.text = app.blockCodeBuffer.data(); draft.error.clear();
        app.blockCodeShown = draft.text; app.blockCodeError.clear();
        app.dirty = true; app.exporting.preparedKey.clear();
    }
}

// La vista Bloques (DESIGN_PLUGIN_NOTE_LAB §29): arriba el tipo (de cualquier
// mod abierto), los presets, la vista y exportar; debajo el editor de bloques,
// el codigo de cada motor o los dos a la vez.
#include "ModResources.hpp"

// Guardar una nota como otra: sus bloques (o su codigo) y su aspecto, con otro nombre.
bool copyNoteAs(NoteLabApp& app, int sourceIndex, const std::string& from, const std::string& to, const BlockProgram& program) {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size())) return false;
    Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    std::string name = to;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (name.empty() || newNoteNameTaken(source, name)) return false;
    source.typeBlocks[name] = program;
    const auto look = source.typeLooks.find(from);
    if (look != source.typeLooks.end()) source.typeLooks[name] = look->second;
    app.blocksSource = sourceIndex;
    app.blocksType = name;
    blocksChanged(app);
    setStatus(app, "Saved as a new note: «" + name + "» (in the Catalog, Your notes).", "Guardada como nota nueva: «" + name + "» (en el Catálogo, Tus notas).");
    return true;
}

// Lo de la nota abierta, a la vista (pedido del autor, 5 oct 2026): guardar,
// guardar como, pasarla a codigo (o su script a bloques), su aspecto y su bot.
void drawNoteActions(NoteLabApp& app, int sourceIndex, const std::string& type, BlockProgram& program, Engine engine) {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    const bool code = codeOnlyType(program);
    // Guardar: la nota vive en el proyecto; guardarla es guardar el proyecto.
    if (ui::flatButton(ui::label(ui::icon::Document, tr(app, "Save", "Guardar")),
                       app.projectPath.empty() ? tr(app, "Save the project (with this note) in a .fmlnote file (Ctrl+S)", "Guardar el proyecto (con esta nota) en un archivo .fmlnote (Ctrl+S)")
                                               : tr(app, "Save the project with this note (Ctrl+S)", "Guardar el proyecto con esta nota (Ctrl+S)")))
        saveProject(app, window);
    tutorialMark("blocks-save");
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::flatButton(ui::label(ui::icon::Copy, tr(app, "Save as", "Guardar como")) + "  " + ui::icon::Down, tr(app, "Another note, a block program, its code or the project", "Otra nota, un programa de bloques, su código o el proyecto")))
        ImGui::OpenPopup("noteSaveAs");
    tutorialMark("blocks-saveas");
    static std::array<char, 64> copyName{};
    if (ImGui::BeginPopup("noteSaveAs")) {
        ImGui::TextDisabled("%s", tr(app, "As another note", "Como otra nota"));
        if (ImGui::IsWindowAppearing()) std::snprintf(copyName.data(), copyName.size(), "%s %s", type.c_str(), tr(app, "(copy)", "(copia)"));
        ImGui::SetNextItemWidth(220.0f);
        const bool enter = ImGui::InputText("##copyname", copyName.data(), copyName.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool taken = newNoteNameTaken(*app.sources[static_cast<size_t>(sourceIndex)], copyName.data());
        ImGui::BeginDisabled(taken || copyName[0] == 0);
        if ((ui::primaryButton(tr(app, "Save", "Guardar")) || enter) && !taken && copyName[0] != 0) {
            const BlockProgram copy = program;
            if (copyNoteAs(app, sourceIndex, type, copyName.data(), copy)) ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (taken) ImGui::TextColored(ui::vec(ui::color::Warning), "%s", tr(app, "That name is taken.", "Ese nombre ya está."));
        ImGui::Separator();
        app.programSource = sourceIndex;
        app.programType = type;
        if (menuItem(ui::icon::Puzzle, tr(app, "Block program (.nlblocks)…", "Programa de bloques (.nlblocks)…"), nullptr, false, !program.empty()))
            openDialog(app, window, DialogAction::SaveProgram);
        if (menuItem(ui::icon::Code, tr(app, "Its code, in a folder…", "Su código, en una carpeta…"), nullptr, false, !program.empty()))
            openDialog(app, window, DialogAction::SaveCodeFolder);
        if (menuItem(ui::icon::FolderOpen, tr(app, "The project as…", "El proyecto como…"))) openDialog(app, window, DialogAction::SaveProject);
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0f, 12.0f);
    if (!code) {
        // Pasar a codigo: convertir esta nota (Ctrl+Z vuelve) o una copia.
        if (ui::flatButton(ui::label(ui::icon::Code, tr(app, "To code", "Pasar a código")) + "  " + ui::icon::Down,
                           tr(app, "Turn its blocks into code you write yourself", "Convertir sus bloques en código que escribes tú")))
            ImGui::OpenPopup("noteToCode");
        tutorialMark("blocks-tocode");
        if (ImGui::BeginPopup("noteToCode")) {
            const BlockCode generated = generateBlocks(engine, type, engine == Engine::VSlice ? exportName(type) : newNoteFile(engine, type), program);
            const BlockFile* script = nullptr;
            for (const BlockFile& file : generated.files)
                if (!script && (endsWithText(file.path, ".lua") || endsWithText(file.path, ".hx") || endsWithText(file.path, ".hxc"))) script = &file;
            ImGui::TextDisabled("%s", (std::string(engineLabel(engine)) + tr(app, " code of this note", ", el código de esta nota")).c_str());
            ImGui::BeginDisabled(!script);
            if (menuItem(ui::icon::Restart, tr(app, "Turn this note into code (Ctrl+Z undoes it)", "Convertir esta nota en código (Ctrl+Z lo deshace)"))) {
                const CodeImportResult result = keepCustomSource(engine, BlockProgram{}, script->path, script->text);
                if (result.applied) {
                    nlblocks::remember(app.canvas, program);
                    program = result.program;
                    app.blocksView = 1;
                    blocksChanged(app);
                    setStatus(app, "Now it's a code-only note: its script is yours to edit («Save code»). Ctrl+Z brings the blocks back.",
                              "Ahora es una nota de solo código: su script lo editas tú («Guardar código»). Ctrl+Z devuelve los bloques.");
                }
            }
            if (menuItem(ui::icon::Copy, tr(app, "Make a code copy (the blocks stay)", "Hacer una copia en código (los bloques se quedan)"))) {
                const CodeImportResult result = keepCustomSource(engine, BlockProgram{}, script->path, script->text);
                std::string name = type + tr(app, " (code)", " (código)");
                for (int k = 2; newNoteNameTaken(*app.sources[static_cast<size_t>(sourceIndex)], name) && k < 50; ++k)
                    name = type + tr(app, " (code ", " (código ") + std::to_string(k) + ")";
                if (result.applied && copyNoteAs(app, sourceIndex, type, name, result.program)) app.blocksView = 1;
            }
            ImGui::EndDisabled();
            if (!script) ui::caption(tr(app, "Nothing to write yet: add blocks under an event.", "Aún no hay nada que escribir: pon bloques bajo un evento."));
            ImGui::EndPopup();
        }
    } else {
        // Una nota de solo codigo: su script, a bloques (lo que se pueda).
        if (ui::flatButton(ui::label(ui::icon::Puzzle, tr(app, "To blocks…", "Pasar a bloques…")),
                           tr(app, "Turn what it can of its script into blocks; the rest stays as code blocks", "Pasar a bloques lo que se pueda de su script; lo demás queda en bloques de código")))
            importTypeScript(app, sourceIndex, type);
        tutorialMark("blocks-toblocks");
    }
    ImGui::SameLine(0.0f, 12.0f);
    if (ui::flatButton(ui::label(ui::icon::Brush, tr(app, "Look", "Aspecto")) + "  " + ui::icon::Down, tr(app, "Paint it, your images or draw it", "Pintarla, tus imágenes o dibujarla")))
        ImGui::OpenPopup("noteLook");
    tutorialMark("blocks-look");
    if (ImGui::BeginPopup("noteLook")) {
        if (menuItem(ui::icon::Palette, tr(app, "Paint it (color, mark)…", "Pintarla (color, marca)…"))) openTypeLook(app, sourceIndex, type);
        if (menuItem(ui::icon::Photo, tr(app, "My images…", "Mis imágenes…"))) {
            openTypeLook(app, sourceIndex, type);
            app.typeLook.mode = 1;
        }
        if (menuItem(ui::icon::Edit, tr(app, "Draw it…", "Dibujarla…"))) openSpriteEditor(app, sourceIndex, type);
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::flatButton(ui::label(ui::icon::Bot, tr(app, "Bot", "Bot")), tr(app, "Whether the bot hits it or avoids it", "Si el bot la toca o la evita")))
        openCustomBot(app, sourceIndex, type);
    // Si hay algo sin guardar en el proyecto, se dice.
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::AlignTextToFramePadding();
    if (app.dirty) ImGui::TextColored(ui::vec(ui::color::Warning), "%s", tr(app, "• Unsaved changes", "• Cambios sin guardar"));
    else if (!app.projectPath.empty()) ImGui::TextColored(ui::vec(ui::color::Success), "%s", ui::label(ui::icon::Check, tr(app, "Saved", "Guardada")).c_str());
}

void drawBlocksPanel(NoteLabApp& app) {
    const std::vector<TypeChoice> choices = blockTypeChoices(app);
    auto listed = [&](int source, const std::string& name) {
        for (const TypeChoice& choice : choices)
            if (choice.source == source && choice.name == name) return true;
        return false;
    };
    if (!listed(app.blocksSource, app.blocksType)) {
        // Un tipo pedido por su nombre (--block-type) en el mod elegido, o el primero.
        int found = -1;
        for (size_t i = 0; i < choices.size() && found < 0; ++i)
            if (choices[i].name == app.blocksType && (app.blocksSource < 0 || choices[i].source == app.blocksSource)) found = static_cast<int>(i);
        // Sin nota pedida no se escoge ninguna sola: el editor espera a que se
        // elija una, se cree o se elija escribir solo codigo.
        if (found >= 0) {
            app.blocksSource = choices[static_cast<size_t>(found)].source;
            app.blocksType = choices[static_cast<size_t>(found)].name;
        } else {
            app.blocksType.clear();
        }
    }
    const TypeChoice* current = nullptr;
    for (const TypeChoice& choice : choices)
        if (choice.source == app.blocksSource && choice.name == app.blocksType) current = &choice;

    drawBlocksGuide(app);

    // Barra: el tipo, nuevo tipo, presets, la vista y exportar.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr(app, "Type", "Tipo"));
    ImGui::SameLine();
    const std::string comboLabel = current ? current->name + "   \xC2\xB7 " + sourceName(*app.sources[static_cast<size_t>(current->source)])
                                           : std::string(tr(app, "(none)", "(ninguno)"));
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("##blocktype", comboLabel.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
            bool header = false;
            int lastSource = -1;
            for (const TypeChoice& choice : choices) {
                if (choice.engine != engine) continue;
                if (!header) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(engineColor(engine)));
                    ImGui::SeparatorText(engineLabel(engine));
                    ImGui::PopStyleColor();
                    header = true;
                }
                if (choice.source != lastSource) {
                    ImGui::TextDisabled("%s", ui::label(ui::icon::FolderOpen, sourceName(*app.sources[static_cast<size_t>(choice.source)])).c_str());
                    lastSource = choice.source;
                }
                ImGui::PushID(choice.source);
                ImGui::PushID(choice.name.c_str());
                const std::string label = "   " + ui::label(choice.created ? ui::icon::Add : choice.builtin ? ui::icon::Home : ui::icon::Puzzle, choice.name);
                if (ImGui::Selectable(label.c_str(), current && current->source == choice.source && current->name == choice.name)) {
                    app.blocksSource = choice.source;
                    app.blocksType = choice.name;
                }
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    if (current) {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::AlignTextToFramePadding();
        ui::pill(engineLabel(current->engine), engineColor(current->engine));
        ImGui::SameLine(0.0f, 6.0f);
        ui::pill(current->created ? tr(app, "new in Note Lab", "nuevo en Note Lab")
                                  : current->builtin ? tr(app, "built-in", "de serie") : tr(app, "from the mod", "del mod"),
                 current->created ? ui::color::Accent : ui::color::Muted);
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (ui::iconButton("newtype", ui::icon::Add, "+", tr(app, "New custom note…", "Nueva nota custom…"))) openNewNote(app);
    tutorialMark("blocks-newtype");
    if (!current) {
        drawBlocksEmpty(app);
        return;
    }
    Source& source = *app.sources[static_cast<size_t>(current->source)];
    const std::string type = current->name;
    const bool createdType = current->created;
    BlockProgram& program = source.typeBlocks[type];
    const std::string owner = std::to_string(current->source) + "|" + type;
    if (app.canvas.owner != owner) {
        const auto search = app.canvas.search;
        const auto collapsed = app.canvas.collapsed;
        const bool showAllBlocks = app.canvas.showAllBlocks;
        app.canvas = nlblocks::CanvasState{};
        app.canvas.search = search;
        app.canvas.collapsed = collapsed;
        app.canvas.showAllBlocks = showAllBlocks;
        app.canvas.owner = owner;
        app.canvas.fitPending = true;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ui::iconButton("typemore", ui::icon::More, "...", tr(app, "More", "Más"))) ImGui::OpenPopup("typemorepopup");
    if (ImGui::BeginPopup("typemorepopup")) {
        if (menuItem(ui::icon::Copy, tr(app, "Copy its name", "Copiar su nombre"))) copyText(app, type);
        // Lo demas de la nota, tambien desde aqui: su aspecto y su bot.
        if (menuItem(ui::icon::Brush, tr(app, "Change its look…", "Cambiar su aspecto…"))) openTypeLook(app, current->source, type);
        if (menuItem(ui::icon::Edit, tr(app, "Draw its sprite…", "Dibujar su sprite…"))) openSpriteEditor(app, current->source, type);
        if (menuItem(ui::icon::Bot, tr(app, "Set up its bot…", "Configurar su bot…"))) openCustomBot(app, current->source, type);
        ImGui::Separator();
        // El codigo de texto de la nota, a bloques (lo que se pueda), y sus archivos.
        if (menuItem(ui::icon::Puzzle, tr(app, "Turn its script into blocks…", "Pasar su script a bloques…"))) importTypeScript(app, current->source, type);
        app.programSource = current->source;
        app.programType = type;
        if (menuItem(ui::icon::Document, tr(app, "Save block program (.nlblocks)…", "Guardar programa de bloques (.nlblocks)…"), nullptr, false, !program.empty()))
            openDialog(app, SDL_GL_GetCurrentWindow(), DialogAction::SaveProgram);
        if (menuItem(ui::icon::FolderOpen, tr(app, "Open block program (.nlblocks)…", "Abrir programa de bloques (.nlblocks)…")))
            openDialog(app, SDL_GL_GetCurrentWindow(), DialogAction::OpenProgram);
        if (menuItem(ui::icon::Code, tr(app, "Save its code in a folder…", "Guardar su código en una carpeta…"), nullptr, false, !program.empty()))
            openDialog(app, SDL_GL_GetCurrentWindow(), DialogAction::SaveCodeFolder);
        ImGui::Separator();
        if (menuItem(ui::icon::Sort, tr(app, "Tidy up the blocks", "Ordenar los bloques"), nullptr, false, !program.tops.empty())) {
            nlblocks::remember(app.canvas, program);
            nlblocks::arrange(program);
            nlblocks::playCue(nlblocks::Cue::Tidy, !app.headless && app.uiSounds);
            blocksChanged(app);
        }
        if (menuItem(ui::icon::Restart, tr(app, "Remove all its blocks", "Quitar todos sus bloques"), nullptr, false, !program.empty())) {
            nlblocks::remember(app.canvas, program);
            program = BlockProgram{};
            nlblocks::playCue(nlblocks::Cue::Delete, !app.headless && app.uiSounds);
            blocksChanged(app);
        }
        if (createdType && menuItem(ui::icon::Close, tr(app, "Delete this new type", "Borrar este tipo nuevo"))) {
            nlblocks::playCue(nlblocks::Cue::Delete, !app.headless && app.uiSounds);
            source.typeBlocks.erase(type);
            app.blocksType.clear();
            blocksChanged(app);
            ImGui::EndPopup();
            return;
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button((ui::label(ui::icon::Layers, tr(app, "Presets", "Presets")) + (ui::fonts().icons ? std::string("  ") + ui::icon::Down : std::string())).c_str()))
        ImGui::OpenPopup("presetspopup");
    if (ImGui::BeginPopup("presetspopup")) {
        for (int advanced = 0; advanced < 2; ++advanced) {
            ImGui::SeparatorText(advanced ? tr(app, "Advanced", "Avanzados") : tr(app, "Basic", "Básicos"));
            for (const BlockPreset& preset : blockPresets()) {
                if (preset.advanced != (advanced == 1)) continue;
                if (ImGui::MenuItem(app.spanish ? preset.nameEs : preset.nameEn)) {
                    nlblocks::remember(app.canvas, program);
                    const std::vector<int> before = program.tops;
                    addPreset(program, preset);
                    nlblocks::placeNewStacks(program, before);
                    // Lo nuevo brilla un momento y suena un acorde.
                    for (int top : program.tops)
                        if (std::find(before.begin(), before.end(), top) == before.end()) {
                            app.canvas.glowId = top;
                            app.canvas.glowStart = ImGui::GetTime();
                            break;
                        }
                    nlblocks::playCue(nlblocks::Cue::Preset, !app.headless && app.uiSounds);
                    blocksChanged(app);
                    setStatus(app, std::string("Preset added: ") + preset.nameEn + ".", std::string("Preset añadido: ") + preset.nameEs + ".");
                }
                ui::tooltip(app.spanish ? preset.helpEs : preset.helpEn);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 6.0f);
    if (ui::iconButton("blockresources", ui::icon::Photo, "R", tr(app, "Show resources in the left panel", "Mostrar recursos en el panel izquierdo"))) {
        app.leftPanel = 1; saveSettings(app);
    }

    drawBlocksGuideButton(app);

    // A la derecha: la vista y exportar.
    const std::string viewLabels[3] = {ui::label(ui::icon::Puzzle, tr(app, "Blocks", "Bloques")), ui::label(ui::icon::Code, tr(app, "Code", "Código")),
                                       ui::label(ui::icon::Layers, tr(app, "Both", "Los dos"))};
    const char* views[] = {viewLabels[0].c_str(), viewLabels[1].c_str(), viewLabels[2].c_str()};
    float viewsW = 0.0f;
    for (const char* view : views) viewsW += ImGui::CalcTextSize(view).x + 22.0f;
    const std::string exportLabel = ui::label(ui::icon::Zip, tr(app, "Export this type…", "Exportar este tipo…"));
    const float exportW = ImGui::CalcTextSize(exportLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float beforeViewsRoom = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ImGui::GetItemRectMax().x;
    if (beforeViewsRoom >= viewsW + ImGui::GetFrameHeight() + 30.0f) ImGui::SameLine();
    const float startX = ImGui::GetCursorPosX();
    const float room = ImGui::GetContentRegionAvail().x;
    const bool compact = room < viewsW + exportW + 30.0f;
    const float wanted = viewsW + (compact ? ImGui::GetFrameHeight() : exportW) + 18.0f;
    ImGui::SetCursorPosX(startX + std::max(8.0f, room - wanted));
    ImGui::BeginGroup();
    if (ui::segmented("blocksview", &app.blocksView, views, 3)) saveSettings(app);
    ImGui::EndGroup();
    tutorialMark("blocks-view");
    app.blocksToolbarViewsRect = mediaItemRect();
    ImGui::SameLine(0.0f, 10.0f);
    if (compact) {
        if (ui::iconButton("exporttype", ui::icon::Zip, "E", tr(app, "Export this type…", "Exportar este tipo…"))) openExportForType(app, current->source, type);
    } else if (ui::primaryButton(exportLabel)) {
        openExportForType(app, current->source, type);
    }
    tutorialMark("blocks-export");

    // El codigo del motor elegido (el del mod si no se eligio otro).
    Engine engine;
    if (!sourceEngine(app, source, engine)) engine = Engine::Codename;
    const Engine target = app.blocksEngine >= 0 ? static_cast<Engine>(app.blocksEngine) : engine;
    drawNoteActions(app, current->source, type, program, engine);
    bool renamed = false;
    std::string file = type;
    for (char& c : file)
        if (std::strchr("<>:\"/\\|?*", c)) {
            c = '_';
            renamed = true;
        }
    const BlockCode code = generateBlocks(target, type, target == Engine::VSlice ? exportName(type) : file, program);
    nlblocks::CanvasEnv env;
    env.spanish = app.spanish;
    env.typeName = type;
    env.engine = target;
    env.sounds = modSounds(source);
    env.conflicts = code.conflicts;
    env.soundOn = nullptr;
    env.resourcePicker = [&](int id, int argument, ArgKind kind) {
        openModResources(app, current->source, type, id, argument, kind == ArgKind::Image ? ResourceKind::Image : kind == ArgKind::Video ? ResourceKind::Video : ResourceKind::Sound);
    };

    ImGui::Spacing();
    if (!program.comment.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader(tr(app, "Comments and preview coverage", "Comentarios y cobertura de preview"))) {
        BlockNode* selected = program.node(app.canvas.selected);
        std::string& comment = selected ? selected->comment : program.comment;
        const std::string owner = std::to_string(app.blocksSource) + ":" + type + ":" + std::to_string(selected ? app.canvas.selected : -1);
        ImGui::TextUnformatted(selected ? blockName(selected->key, app.spanish).c_str() : tr(app, "Note type comment", "Comentario del tipo de nota"));
        if (selected) ui::caption(previewCoverage(selected->key, app.spanish));
        if (owner != app.blockCommentOwner || comment != app.blockCommentShown) {
            app.blockCommentOwner = owner; app.blockCommentShown = comment;
            std::snprintf(app.blockCommentBuffer.data(), app.blockCommentBuffer.size(), "%s", comment.c_str());
        }
        const bool longComment = comment.size() >= app.blockCommentBuffer.size();
        if (longComment) ui::caption(tr(app, "This comment exceeds the panel limit; edit it in the code view to preserve all of it.",
            "Este comentario supera el límite del panel; edítalo desde código para conservarlo completo."));
        if (ImGui::InputTextMultiline("##blockcomment", app.blockCommentBuffer.data(), app.blockCommentBuffer.size(), ImVec2(-1.0f, 64.0f),
                longComment ? ImGuiInputTextFlags_ReadOnly : 0)) {
            nlblocks::remember(app.canvas, program); comment = app.blockCommentBuffer.data();
            app.blockCommentShown = comment; blocksChanged(app);
        }
        ui::caption(tr(app, "Select a block to edit its comment; clear selection to edit the type comment. Comments survive code regeneration.",
            "Selecciona un bloque para editar su comentario; sin selección editas el del tipo. Se conservan al regenerar código."));
    }
    const bool custom = std::any_of(program.nodes.begin(), program.nodes.end(), [&](const auto& item) {
        return item.second.key == "code.file" && item.second.args.size() == 3 && item.second.args[0].value == engineKey(target);
    });
    ui::caption(custom ? tr(app, "Custom file overrides this engine: previous blocks do not describe or simulate its runtime behavior.",
        "Hay un archivo propio para este motor: los bloques anteriores no describen ni simulan su comportamiento real.")
        : tr(app, "Preview applies avoid / hit-is-miss and note opacity, scale, rotation. Script actions are exported, not executed here.",
            "La preview aplica evitar / acierto como fallo y opacidad, escala y giro. Las acciones de script se exportan; no se ejecutan aquí."));

    ImGui::Spacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (app.blocksView == 1) {
        ImGui::BeginChild("blockscode", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        drawBlocksCode(app, type, program, code, target, renamed);
        ImGui::EndChild();
        return;
    }
    const bool both = app.blocksView == 2;
    const bool stacked = both && avail.x < 760.0f;
    const float span = stacked ? avail.y : avail.x;
    const float codeSize = both ? std::clamp(span * app.blocksCodeRatio, std::min(stacked ? 230.0f : 300.0f, span * 0.5f),
        std::max(span * 0.5f, span - (stacked ? 200.0f : 420.0f) - 8.0f)) : 0.0f;
    const float editorSize = std::max(1.0f, span - codeSize - (both ? 8.0f : 0.0f));
    const ImVec2 region = ImGui::GetCursorScreenPos();
    ImGui::BeginChild("blockseditor", stacked ? ImVec2(avail.x, editorSize) : ImVec2(editorSize, avail.y), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (nlblocks::drawCanvas(app.canvas, program, env)) blocksChanged(app);
    tutorialMarkCanvas(app.canvas, program);
    if (app.canvas.settingsChanged) {
        app.canvas.settingsChanged = false;
        saveSettings(app);
    }
    ImGui::EndChild();
    app.blocksDividerRect = {};
    if (both) {
        const ImVec2 divider = stacked ? ImVec2(region.x, region.y + editorSize) : ImVec2(region.x + editorSize, region.y);
        ImGui::SetCursorScreenPos(divider);
        ImGui::InvisibleButton("##blocksdivider", stacked ? ImVec2(avail.x, 8.0f) : ImVec2(8.0f, avail.y));
        app.blocksDividerRect = {divider.x, divider.y, stacked ? avail.x : 8.0f, stacked ? 8.0f : avail.y};
        const bool resizing = ImGui::IsItemActive();
        if (ImGui::IsItemHovered() || resizing) ImGui::SetMouseCursor(stacked ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
        if (resizing && span > 0.0f) {
            const float delta = stacked ? ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.x;
            app.blocksCodeRatio = std::clamp(app.blocksCodeRatio - delta / span, 0.15f, 0.65f);
        }
        if (ImGui::IsItemDeactivated()) saveSettings(app);
        const ImU32 dividerColor = ImGui::IsItemHovered() || resizing ? ui::color::Accent : IM_COL32(93, 100, 118, 125);
        const ImVec2 lineStart = stacked ? ImVec2(divider.x, divider.y + 3.0f) : ImVec2(divider.x + 3.0f, divider.y);
        const ImVec2 lineEnd = stacked ? ImVec2(divider.x + avail.x, divider.y + 5.0f) : ImVec2(divider.x + 5.0f, divider.y + avail.y);
        ImGui::GetWindowDrawList()->AddRectFilled(lineStart, lineEnd, dividerColor, 1.0f);
        ImGui::SetCursorScreenPos(stacked ? ImVec2(region.x, divider.y + 8.0f) : ImVec2(divider.x + 8.0f, region.y));
        ImGui::BeginChild("blockscode", stacked ? ImVec2(avail.x, codeSize) : ImVec2(codeSize, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
        drawBlocksCode(app, type, program, code, target, renamed);
        ImGui::EndChild();
    }
}

// «Distribuir»: un paquete de tipos con su porcentaje y su semilla, los
// filtros, el recuento antes de aplicar y deshacer.
void drawDistributePanel(NoteLabApp& app, Source& source) {
    ui::sectionHeader(tr(app, "Distribute in the chart", "Distribuir en el chart"), ui::icon::Puzzle);
    const bool chartHere = app.songSource >= 0 && app.songSource < static_cast<int>(app.sources.size()) && app.sources[static_cast<size_t>(app.songSource)].get() == &source;
    if (!chartHere) {
        ui::caption(tr(app, "«Distribute» works on a chart of this mod: pick one here (or in the Preview tab).",
                            "«Distribuir» trabaja sobre un chart de este mod: elígelo aquí (o en la pestaña Vista previa)."));
        // La cancion se elige aqui mismo; si ya se uso una de este mod (y se
        // volvio al patron de prueba), un boton la vuelve a cargar.
        int sourceIndex = -1;
        for (size_t i = 0; i < app.sources.size(); ++i)
            if (app.sources[i].get() == &source) sourceIndex = static_cast<int>(i);
        if (sourceIndex < 0) return;
        ImGui::BeginGroup();
        if (app.lastSongOwner == &source && app.lastSongIndex >= 0 && app.lastSongIndex < static_cast<int>(source.songs.size())) {
            if (ui::primaryButton(ui::label(ui::icon::Music, std::string(tr(app, "Load «", "Volver a cargar «")) + app.lastSongLabel + "»")))
                chooseSong(app, sourceIndex, app.lastSongIndex);
            ImGui::SameLine(0.0f, 8.0f);
        }
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::BeginCombo("##distributesong", tr(app, "Pick a song…", "Elige una canción…"), ImGuiComboFlags_HeightLarge)) {
            for (size_t i = 0; i < source.songs.size(); ++i) {
                const SongChart& song = source.songs[i];
                const std::string label = song.id + " · " + song.difficulty + (song.variation.empty() ? "" : " (" + song.variation + ")") +
                                          "##distsong" + std::to_string(i);
                if (ImGui::Selectable(label.c_str())) chooseSong(app, sourceIndex, static_cast<int>(i));
            }
            if (source.songs.empty()) ImGui::TextDisabled("%s", tr(app, "This mod has no charts.", "Este mod no tiene charts."));
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
        tutorialMark("distribute-song");
        return;
    }
    ui::caption(tr(app, "Puts custom note types of the mod into a copy of the chart, at random but with a seed: the same seed always gives "
                        "the same result. Notes that already have a type are kept. The mod's chart does not change.",
                        "Mete tipos de nota del mod en una copia del chart, al azar pero con semilla: la misma semilla da siempre el mismo "
                        "reparto. Las notas que ya tienen tipo se respetan. El chart del mod no cambia."));
    ImGui::TextUnformatted(ui::label(ui::icon::Music, app.songLabel).c_str());
    Engine engine;
    const bool known = sourceEngine(app, source, engine);
    std::vector<std::string> names;
    for (const NoteTypeEntry& type : source.noteTypes)
        if (!known || type.engine == engine) names.push_back(type.name);
    const bool populate = ui::primaryButton(tr(app, "Populate with this mod's custom notes", "Poner las notas custom de este mod"));
    tutorialMark("distribute-fill");
    if (populate) {
        app.distribute.rules.clear(); app.distribute.filter = {};
        std::vector<std::string> custom;
        for (const auto& type : source.noteTypes)
            if ((!known || type.engine == engine) && !type.builtin && lowerText(type.name) != "default note" && lowerText(type.name) != "normal") custom.push_back(type.name);
        if (!custom.empty()) {
            for (const auto& type : custom) app.distribute.rules.push_back({type, 25.0f / static_cast<float>(custom.size()), 0});
            applyDistribution(app);
        } else setStatus(app, "No custom note types were found in this mod.", "No se encontraron tipos custom en este mod.");
    }
    ui::caption(tr(app, "Quick fill: 25% across the detected types. Adjust percentages or exact amounts below; existing custom notes stay protected.",
        "Reparto rápido: 25% entre los tipos detectados. Ajusta porcentajes o cantidades abajo; las custom existentes se protegen."));

    ui::sectionHeader(tr(app, "Package", "Paquete"), ui::icon::List);
    int remove = -1;
    if (ImGui::BeginTable("rules", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(tr(app, "Type", "Tipo"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr(app, "Seed", "Semilla"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableHeadersRow();
        for (size_t r = 0; r < app.distribute.rules.size(); ++r) {
            DistributeRule& rule = app.distribute.rules[r];
            ImGui::PushID(static_cast<int>(r));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##type", rule.type.empty() ? tr(app, "(pick one)", "(elige uno)") : rule.type.c_str())) {
                for (const std::string& name : names)
                    if (ImGui::Selectable(name.c_str(), name == rule.type)) rule.type = name;
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##percent", &rule.percent, 0.0f, 100.0f, "%.1f %%");
            bool exact = rule.count >= 0;
            if (ImGui::Checkbox(tr(app, "Exact amount", "Cantidad exacta"), &exact)) rule.count = exact ? 25 : -1;
            if (exact) { ImGui::SetNextItemWidth(-1); ImGui::InputInt("##exactnotes", &rule.count, 0, 0); rule.count = std::max(0, rule.count); }
            ImGui::TableNextColumn();
            int seed = static_cast<int>(rule.seed);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt("##seed", &seed, 0, 0)) rule.seed = static_cast<std::uint32_t>(std::max(0, seed));
            ui::tooltip(tr(app, "0 = the package's seed", "0 = la semilla del paquete"));
            ImGui::TableNextColumn();
            if (ui::iconButton("remove", ui::icon::Close, "x", tr(app, "Remove from the package", "Quitar del paquete"), false, 26.0f))
                remove = static_cast<int>(r);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove >= 0) app.distribute.rules.erase(app.distribute.rules.begin() + remove);
    if (ImGui::Button(ui::label(ui::icon::Add, tr(app, "Add a type", "Añadir un tipo")).c_str()))
        app.distribute.rules.push_back({names.empty() ? std::string() : names.front(), 10.0f, 0});
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(app, "Package seed", "Semilla del paquete"));
    ImGui::SameLine();
    int packageSeed = static_cast<int>(app.distribute.seed);
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::InputInt("##packageseed", &packageSeed, 0, 0)) app.distribute.seed = static_cast<std::uint32_t>(std::max(1, packageSeed));
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::iconButton("reroll", ui::icon::Restart, tr(app, "New", "Nueva"), tr(app, "Another seed", "Otra semilla")))
        app.distribute.seed = static_cast<std::uint32_t>(1 + (app.distribute.seed * 1103515245u + 12345u) % 2147483646u);

    ui::sectionHeader(tr(app, "Filters", "Filtros"), ui::icon::Filter);
    if (ImGui::Button(tr(app, "Reset filters", "Restablecer filtros"))) app.distribute.filter = {};
    ImGui::Checkbox(tr(app, "Also replace existing custom notes", "Reemplazar también las custom existentes"), &app.distribute.filter.replaceExisting);
    const char* sides[] = {tr(app, "Opponent", "Rival"), tr(app, "Player", "Jugador"), tr(app, "Both", "Ambos")};
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(app, "Side", "Lado"));
    ImGui::SameLine(110.0f);
    ui::segmented("side", &app.distribute.filter.side, sides, 3);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(app, "Lanes", "Carriles"));
    ImGui::SameLine(110.0f);
    for (int d = 0; d < 4; ++d) {
        if (d) ImGui::SameLine(0.0f, 4.0f);
        const std::string text = directionLabel(d, app.spanish);
        bool on = app.distribute.filter.lanes[static_cast<size_t>(d)];
        ImGui::PushID(d);
        if (ui::toggle("lane", text.substr(0, text.find(' ')), &on, text.c_str())) app.distribute.filter.lanes[static_cast<size_t>(d)] = on;
        ImGui::PopID();
    }
    ImGui::Checkbox(tr(app, "No long notes", "Sin notas largas"), &app.distribute.filter.skipSustains);
    ImGui::SameLine();
    ImGui::Checkbox(tr(app, "No chords", "Sin acordes"), &app.distribute.filter.skipChords);
    helpMarker(tr(app, "Chords: notes of the same line at the same time.", "Acordes: notas de la misma línea a la vez."));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(app, "Minimum gap", "Separación mínima"));
    ImGui::SameLine(150.0f);
    float gap = static_cast<float>(app.distribute.filter.minGapMs);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderFloat("##gap", &gap, 0.0f, 3000.0f, "%.0f ms")) app.distribute.filter.minGapMs = gap;
    helpMarker(tr(app, "Between two notes of the same new type on the same line.", "Entre dos notas del mismo tipo nuevo en la misma línea."));

    // Recuento en vivo: lo que saldria al aplicar.
    ui::sectionHeader(tr(app, "Result", "Resultado"), ui::icon::Check);
    const auto& filter = app.distribute.filter;
    std::string distributionKey = std::to_string(app.songSource) + ":" + std::to_string(app.songIndex) + ":" + std::to_string(app.notes.size()) +
        ":" + std::to_string(app.distribute.seed) + ":" + std::to_string(filter.side) + ":" + std::to_string(filter.skipSustains) +
        ":" + std::to_string(filter.skipChords) + ":" + std::to_string(filter.replaceExisting) + ":" + std::to_string(filter.minGapMs);
    for (bool lane : filter.lanes) distributionKey += lane ? "1" : "0";
    for (const auto& rule : app.distribute.rules) distributionKey += ";" + rule.type + ":" + std::to_string(rule.percent) + ":" + std::to_string(rule.count) + ":" + std::to_string(rule.seed);
    if (app.distributionPreviewKey != distributionKey) { app.distributionPreview = distributeTypes(app.notes, app.chartTypes, app.distribute); app.distributionPreviewKey = distributionKey; }
    const DistributeResult& preview = app.distributionPreview;
    ui::pill((std::string(tr(app, "candidates ", "candidatas ")) + std::to_string(preview.candidates)).c_str(), ui::color::Muted);
    ImGui::Text("%zu total · %d %s · %d %s", app.notes.size(), preview.protectedTypes, tr(app, "protected", "protegidas"), preview.filtered, tr(app, "filtered out", "filtradas"));
    for (size_t r = 0; r < app.distribute.rules.size() && r < preview.placed.size(); ++r) {
        ImGui::SameLine(0.0f, 6.0f);
        const std::string text = app.distribute.rules[r].type + "  " + std::to_string(preview.placed[r]);
        ui::pill(text.c_str(), ui::color::Accent);
        if (r < preview.requested.size() && preview.placed[r] < preview.requested[r]) ImGui::TextWrapped("%s: %d / %d · %s", app.distribute.rules[r].type.c_str(), preview.placed[r], preview.requested[r],
            tr(app, "not enough free notes or the gap filter limits the amount", "faltan notas libres o la separación limita la cantidad"));
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(app.distribute.rules.empty());
    if (ui::primaryButton(ui::label(ui::icon::Check, tr(app, "Apply to the chart copy", "Aplicar a la copia del chart")))) applyDistribution(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!app.distributed);
    if (ImGui::Button(ui::label(ui::icon::Undo, tr(app, "Undo the distribution", "Deshacer la distribución")).c_str())) undoDistribution(app);
    ImGui::EndDisabled();
    ui::caption(app.distributed ? tr(app, "Applied: the preview plays the chart with the new types.",
                                        "Aplicado: la vista previa juega el chart con los tipos nuevos.")
                                : tr(app, "Not applied yet.", "Aún sin aplicar."));
    ImGui::BeginDisabled(!app.distributed);
    if (ImGui::Button(ui::label(ui::icon::Check, tr(app, "Save distribution as chart…", "Guardar distribución como chart…")).c_str()))
        if (prepareChartSave(app)) app.chartSave.requestOpen = true;
    ImGui::EndDisabled();
}


// Una fila del catalogo: un tipo de un mod, con su menu.
void drawTypeRow(NoteLabApp& app, int sourceIndex, int index) {
    Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    const NoteTypeEntry& type = source.noteTypes[static_cast<size_t>(index)];
    const bool chartHere = app.songSource == sourceIndex;
    const bool selected = app.typeSource == sourceIndex && app.selType == index;
    ImGui::TableNextRow(0, ImGui::GetTextLineHeight() + 10.0f);
    ImGui::TableNextColumn();
    ImGui::PushID(index);
    const std::string label = ui::label(type.builtin ? ui::icon::Home : ui::icon::Puzzle, type.name);
    ImGui::AlignTextToFramePadding();
    if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
        app.typeSource = sourceIndex;
        app.selType = index;
    }
    if (ImGui::BeginPopupContextItem("typemenu")) {
        if (menuItem(ui::icon::Info, tr(app, "See its detail", "Ver su detalle"))) {
            app.typeSource = sourceIndex;
            app.selType = index;
        }
        if (menuItem(ui::icon::Puzzle, tr(app, "Edit its blocks", "Editar sus bloques"))) {
            app.blocksSource = sourceIndex;
            app.blocksType = type.name;
            app.typesView = 1;
        }
        if (menuItem(ui::icon::Bot, tr(app, "Custom bot…", "Bot custom…"))) openCustomBot(app, sourceIndex, type.name);
        if (menuItem(ui::icon::Layers, tr(app, "Advanced custom creator…", "Creador custom avanzado…"))) {
            app.selSource = sourceIndex; openCustomCreator(app, false, type.name);
        }
        const auto use = chartHere ? app.typeUses.find(lowerText(type.name)) : app.typeUses.end();
        if (menuItem(ui::icon::Play, tr(app, "Go to the first one in the chart", "Ir a la primera en el chart"), nullptr, false,
                     chartHere && use != app.typeUses.end())) {
            seekTo(app, std::max(0.0, use->second.firstMs - 1500.0));
            app.requestedTab = 0;
        }
        int lookIndex = -1;
        for (size_t k = 0; k < source.catalog.styles.size(); ++k)
            if (!type.lookStyle.empty() && source.catalog.styles[k].id == type.lookStyle) lookIndex = static_cast<int>(k);
        if (menuItem(ui::icon::Edit, tr(app, "Open its style", "Abrir su estilo"), nullptr, false, lookIndex >= 0)) {
            if (!styleVisible(app, source, source.catalog.styles[static_cast<size_t>(lookIndex)])) {
                app.showOtherEngines = true;
                app.showUnused = true;
            }
            selectStyle(app, sourceIndex, lookIndex);
            app.requestedTab = 2;
        }
        ImGui::Separator();
        if (menuItem(ui::icon::Copy, tr(app, "Copy its name", "Copiar su nombre"))) copyText(app, type.name);
        const std::string file = !type.script.empty() ? type.script : type.config;
        if (menuItem(ui::icon::FolderOpen, tr(app, "Show its script in the folder", "Mostrar su script en la carpeta"), nullptr, false,
                     !file.empty()))
            revealVirtual(app, source, file);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(type.lookStyle.empty() ? ui::color::Faint : ui::color::Text));
    ImGui::TextUnformatted(type.lookStyle.empty() ? tr(app, "normal", "normal") : tr(app, "own", "propio"));
    ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    if (type.bot != BotRule::Hits || type.hitMisses) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(type.hitMisses ? ui::color::Error : ui::color::Warning));
        ImGui::TextUnformatted(ui::fonts().icons ? (type.hitMisses ? ui::icon::Warning : ui::icon::Bot) : (type.hitMisses ? "!" : "*"));
        ImGui::PopStyleColor();
        ui::tooltip(type.hitMisses ? tr(app, "Hitting it counts as a miss", "Tocarla cuenta como fallo") : botText(app, type.bot));
    }
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    const auto uses = chartHere ? app.typeUses.find(lowerText(type.name)) : app.typeUses.end();
    if (chartHere && uses != app.typeUses.end()) ImGui::Text("%d", uses->second.count);
    else ImGui::TextDisabled("-");
}

void drawTypesTab(NoteLabApp& app) {
    if (app.sources.empty()) {
        ui::caption(tr(app, "Open a mod to see its custom notes.", "Abre un mod para ver sus notas custom."));
        return;
    }
    const char* views[] = {tr(app, "Catalog", "Catálogo"), tr(app, "Blocks", "Bloques"), tr(app, "Distribute", "Distribuir")};
    ImGui::BeginGroup();
    ui::segmented("typesview", &app.typesView, views, 3);
    ImGui::EndGroup();
    tutorialMark("types-views");
    if (app.typesView == 1) {
        ImGui::BeginChild("blocks", ImVec2(0.0f, 0.0f));
        drawBlocksPanel(app);
        ImGui::EndChild();
        return;
    }
    if (app.typesView == 2) {
        // «Distribuir» trabaja sobre el chart cargado: el de su mod.
        Source* chartSource = app.songSource >= 0 && app.songSource < static_cast<int>(app.sources.size())
                                  ? app.sources[static_cast<size_t>(app.songSource)].get()
                                  : selectedSource(app);
        if (!chartSource) chartSource = app.sources.front().get();
        ImGui::BeginChild("distribute", ImVec2(0.0f, 0.0f));
        drawDistributePanel(app, *chartSource);
        ImGui::EndChild();
        return;
    }
    if (!validType(app, app.typeSource, app.selType)) firstType(app);

    // Lista: los tipos de todos los mods abiertos, por motor y por mod. No
    // depende del mod elegido: el catalogo es de todo lo abierto.
    int total = 0, mods = 0;
    for (const auto& source : app.sources) {
        int here = 0;
        for (const NoteTypeEntry& type : source->noteTypes)
            if (typeVisible(app, *source, type)) ++here;
        total += here;
        if (here > 0) ++mods;
    }
    const float listWidth = std::max(340.0f, ImGui::GetContentRegionAvail().x * 0.42f);
    ImGui::BeginChild("typelist", ImVec2(listWidth, 0.0f));
    const bool createNote = ui::primaryButton(ui::label(ui::icon::Add, tr(app, "Create custom note…", "Crear nota custom…")), ImVec2(-1.0f, 34.0f));
    tutorialMark("catalog-create");
    if (createNote) openNewNote(app);
    ui::tooltip(tr(app, "Name, blocks or code, look, sound and bot, in one place; then on to its blocks.",
                        "Nombre, bloques o código, aspecto, sonido y bot en un sitio; después, a sus bloques."));
    drawCreatedNotes(app);
    std::string heading = std::string(tr(app, "Note types", "Tipos de nota")) + " · ";
    if (app.sources.size() == 1) heading += sourceName(*app.sources.front());
    else heading += std::to_string(total) + tr(app, " in ", " en ") + std::to_string(mods) + " mods";
    ui::sectionHeader(heading.c_str(), ui::icon::Puzzle);
    if (total == 0) {
        ui::caption(tr(app, "The open mods bring no custom notes for their engine. Codename reads them from data/notes/, Psych from "
                            "custom_notetypes/ and V-Slice from scripts/notekinds/.",
                            "Los mods abiertos no traen notas custom de su motor. Codename las lee de data/notes/, Psych de "
                            "custom_notetypes/ y V-Slice de scripts/notekinds/."));
    } else if (ImGui::BeginTable("types", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH |
                                                 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr(app, "Type", "Tipo"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(tr(app, "Look", "Aspecto"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn(tr(app, "Bot", "Bot"), ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn(tr(app, "Chart", "Chart"), ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableHeadersRow();
        const ImGuiTreeNodeFlags group = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_LabelSpanAllColumns |
                                         ImGuiTreeNodeFlags_DefaultOpen;
        for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
            std::vector<std::vector<int>> rows(app.sources.size());
            int engineCount = 0, baseCount = 0;
            for (size_t s = 0; s < app.sources.size(); ++s) {
                const Source& source = *app.sources[s];
                for (size_t t = 0; t < source.noteTypes.size(); ++t)
                    if (source.noteTypes[t].engine == engine && typeVisible(app, source, source.noteTypes[t])) {
                        rows[s].push_back(static_cast<int>(t));
                        if (t < source.typeFromBase.size() && source.typeFromBase[t]) ++baseCount;
                        else ++engineCount;
                    }
            }
            if (engineCount + baseCount == 0) continue;
            ImGui::PushID(static_cast<int>(engine));
            ImGui::TableNextRow(0, ImGui::GetTextLineHeight() + 12.0f);
            ImGui::TableNextColumn();
            ImGui::PushFont(ui::fonts().semibold, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(engineColor(engine)));
            std::string engineTitle = std::string(engineLabel(engine)) + "   " + std::to_string(engineCount) +
                                      (engineCount == 1 ? tr(app, " type", " tipo") : tr(app, " types", " tipos"));
            if (baseCount > 0)
                engineTitle += " (+" + std::to_string(baseCount) + tr(app, " of the base game)", " del juego base)");
            const bool engineOpen = ImGui::TreeNodeEx("engine", group, "%s", engineTitle.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            if (engineOpen) {
                for (size_t s = 0; s < app.sources.size(); ++s) {
                    if (rows[s].empty()) continue;
                    const int sourceIndex = static_cast<int>(s);
                    const Source& source = *app.sources[s];
                    // Los del juego base, aparte y plegados: el mod los tiene a
                    // mano, pero no son suyos.
                    std::vector<int> own, inherited;
                    for (int index : rows[s])
                        (static_cast<size_t>(index) < source.typeFromBase.size() && source.typeFromBase[static_cast<size_t>(index)]
                             ? inherited : own).push_back(index);
                    ImGui::PushID(sourceIndex);
                    ImGui::TableNextRow(0, ImGui::GetTextLineHeight() + 10.0f);
                    ImGui::TableNextColumn();
                    std::string modTitle = sourceName(source) + "   " + std::to_string(own.size());
                    if (sourceIndex == app.selSource) modTitle += tr(app, "   · selected", "   · el elegido");
                    if (sourceIndex == app.songSource) modTitle += tr(app, "   · chart loaded", "   · con el chart cargado");
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
                    const bool modOpen = ImGui::TreeNodeEx("mod", group, "%s", ui::label(ui::icon::FolderOpen, modTitle).c_str());
                    ImGui::PopStyleColor();
                    if (modOpen) {
                        for (int index : own) drawTypeRow(app, sourceIndex, index);
                        if (!inherited.empty()) {
                            ImGui::TableNextRow(0, ImGui::GetTextLineHeight() + 8.0f);
                            ImGui::TableNextColumn();
                            const std::string baseTitle = std::string(tr(app, "base game", "juego base")) + "   " +
                                                          std::to_string(inherited.size());
                            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Faint));
                            const bool baseOpen = ImGui::TreeNodeEx("base", ImGuiTreeNodeFlags_SpanAllColumns |
                                                                            ImGuiTreeNodeFlags_LabelSpanAllColumns,
                                                                    "%s", ui::label(ui::icon::Game, baseTitle).c_str());
                            ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("%s", tr(app, "Note types of the base game mounted under this mod: its charts can use them.",
                                                               "Tipos de nota del juego base montado debajo de este mod: sus charts pueden usarlos."));
                            if (baseOpen) {
                                for (int index : inherited) drawTypeRow(app, sourceIndex, index);
                                ImGui::TreePop();
                            }
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (!validType(app, app.typeSource, app.selType)) {
        ImGui::BeginChild("typedetail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        ui::caption(tr(app, "Pick a note type.", "Elige un tipo de nota."));
        ImGui::EndChild();
        return;
    }
    const int sourceIndex = app.typeSource;
    Source* source = app.sources[static_cast<size_t>(sourceIndex)].get();
    const bool chartHere = app.songSource == sourceIndex;

    // Detalle del tipo elegido.
    ImGui::BeginChild("typedetail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    const NoteTypeEntry& type = source->noteTypes[static_cast<size_t>(app.selType)];
    const bool typeFromBase = static_cast<size_t>(app.selType) < source->typeFromBase.size() &&
                              source->typeFromBase[static_cast<size_t>(app.selType)];
    ui::title(type.name.c_str(), 21.0f);
    ui::pill(engineLabel(type.engine), engineColor(type.engine));
    ImGui::SameLine(0.0f, 6.0f);
    ui::pill(type.builtin ? tr(app, "built-in", "de serie") : typeFromBase ? tr(app, "from the base game", "del juego base")
                                                                           : tr(app, "from the mod", "del mod"),
             type.builtin || typeFromBase ? ui::color::Muted : ui::color::Accent);
    ImGui::SameLine(0.0f, 6.0f);
    ui::pill(sourceName(*source).c_str(), ui::color::Muted);
    if (type.hitMisses) {
        ImGui::SameLine(0.0f, 6.0f);
        ui::pill(tr(app, "hit = miss", "tocarla = fallo"), ui::color::Error);
    }
    if (type.bot != BotRule::Hits) {
        ImGui::SameLine(0.0f, 6.0f);
        ui::pill(tr(app, "bot avoids it", "el bot la evita"), ui::color::Warning);
    }
    ImGui::Spacing();
    if (!type.description.empty()) ImGui::TextWrapped("%s", type.description.c_str());
    else if (type.builtin) ui::caption(builtinText(app, type.name));

    ui::sectionHeader(tr(app, "Look", "Aspecto"), ui::icon::Brush);
    const NoteStyle* look = nullptr;
    for (const NoteStyle& style : source->catalog.styles)
        if (style.id == type.lookStyle) { look = &style; break; }
    const NoteStyle* skin = selectedStyle(app);
    NoteStyle derived;
    const bool rendererHere = app.rendererReady && app.renderSource == sourceIndex;
    if (!rendererHere) {
        // La vista previa dibuja las notas de un mod a la vez: el de este tipo
        // se abre al pedirlo, no al elegir el tipo.
        ui::caption(look ? (look->name + "  ·  " + look->definition).c_str()
                         : tr(app, "Same look as the normal notes of its mod.", "El mismo aspecto que las notas normales de su mod."));
        int lookIndex = -1;
        for (size_t i = 0; look && i < source->catalog.styles.size(); ++i)
            if (&source->catalog.styles[i] == look) lookIndex = static_cast<int>(i);
        if (lookIndex < 0) lookIndex = firstVisibleStyle(app, *source);
        ImGui::BeginDisabled(lookIndex < 0);
        if (ImGui::Button(ui::label(ui::icon::Eye, tr(app, "See it with its mod", "Verla con su mod")).c_str())) {
            if (look && !styleVisible(app, *source, *look)) { app.showOtherEngines = true; app.showUnused = true; }
            selectStyle(app, sourceIndex, lookIndex);
        }
        ImGui::EndDisabled();
        ui::tooltip(tr(app, "Note Lab draws the notes of one mod at a time: this makes its mod the selected one.",
                            "Note Lab dibuja las notas de un mod a la vez: esto hace que su mod sea el elegido."));
    } else if (look && recolorsSkin(*look) && skin && skin->rgbPalette) {
        derived = recolorOver(*skin, *look);
        if (rendererHere) drawNoteThumbs(app, derived, 58.0f);
        ui::caption((std::string(tr(app, "Recolors the skin in use: ", "Recolorea el skin en uso: ")) + skin->name).c_str());
    } else if (look) {
        if (rendererHere) drawNoteThumbs(app, *look, 58.0f);
        ui::caption((look->name + "  ·  " + look->definition).c_str());
    } else {
        if (rendererHere && skin) drawNoteThumbs(app, *skin, 58.0f);
        ui::caption(tr(app, "Same look as the normal notes (the style selected on the left).",
                            "El mismo aspecto que las notas normales (el estilo elegido a la izquierda)."));
    }
    if (look && rendererHere) {
        int lookIndex = -1;
        for (size_t i = 0; i < source->catalog.styles.size(); ++i)
            if (&source->catalog.styles[i] == look) lookIndex = static_cast<int>(i);
        if (lookIndex >= 0 && ImGui::Button(ui::label(ui::icon::Edit, tr(app, "Open its style", "Abrir su estilo")).c_str())) {
            if (!styleVisible(app, *source, *look)) { app.showOtherEngines = true; app.showUnused = true; }
            selectStyle(app, sourceIndex, lookIndex);
            app.requestedTab = 2;
        }
        ui::tooltip(tr(app, "Edit its pieces in the inspector and the asset viewer", "Editar sus piezas en el inspector y el visor de assets"));
    }
    if (rendererHere) {
        if (look) ImGui::SameLine();
        if (ImGui::Button(ui::label(ui::icon::Brush, tr(app, "Change its look…", "Cambiar su aspecto…")).c_str()))
            openTypeLook(app, sourceIndex, type.name);
        ui::tooltip(tr(app, "Import your images for this note or paint it from a skin: color, mark, opacity.",
                            "Importa tus imágenes para esta nota o píntala desde un skin: color, marca, opacidad."));
    }

    ui::sectionHeader(tr(app, "Behavior", "Comportamiento"), ui::icon::Bot);
    if (ImGui::Button(ui::label(ui::icon::Bot, tr(app, "Configure custom bot…", "Configurar bot custom…")).c_str())) openCustomBot(app, sourceIndex, type.name);
    // Con bloques de Note Lab, mandan ellos (en la vista previa y al exportar).
    const auto ownBlocks = source->typeBlocks.find(type.name);
    if (ownBlocks != source->typeBlocks.end() && !ownBlocks->second.empty()) {
        for (const std::string& line : programSummary(ownBlocks->second, app.spanish))
            ImGui::TextWrapped("%s", ui::label(ui::icon::Puzzle, line).c_str());
        ui::caption(tr(app, "Note Lab blocks: they rule over what was read from its script.",
                            "Bloques de Note Lab: mandan sobre lo que se leyó de su script."));
    } else {
        ImGui::TextUnformatted(ui::label(ui::icon::Bot, botText(app, type.bot)).c_str());
        if (type.hitMisses)
            ImGui::TextColored(ui::vec(ui::color::Error), "%s", ui::label(ui::icon::Warning, tr(app, "Hitting it counts as a miss", "Tocarla cuenta como fallo")).c_str());
    }
    if (ImGui::SmallButton(ui::label(ui::icon::Puzzle, tr(app, "Edit its blocks", "Editar sus bloques")).c_str())) {
        app.blocksSource = sourceIndex;
        app.blocksType = type.name;
        app.typesView = 1;
    }
    if (!type.script.empty() || !type.config.empty())
        ui::caption(tr(app, "Read from the script as text, without running it: a condition around the line is not taken into account.",
                            "Leído del script como texto, sin ejecutarlo: una condición alrededor de la línea no se tiene en cuenta."));
    const auto uses = chartHere ? app.typeUses.find(lowerText(type.name)) : app.typeUses.end();
    if (chartHere && uses != app.typeUses.end()) {
        ImGui::Text("%s %d %s", tr(app, "In", "En"), uses->second.count,
                    (std::string(tr(app, "notes of ", "notas de ")) + app.songLabel).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(ui::label(ui::icon::Play, tr(app, "Go to the first", "Ir a la primera")).c_str())) {
            seekTo(app, std::max(0.0, uses->second.firstMs - 1500.0));
            app.requestedTab = 0;
        }
    } else if (chartHere) {
        ui::caption(tr(app, "The loaded chart does not use it.", "El chart cargado no la usa."));
    }

    const std::string file = !type.script.empty() ? type.script : type.config;
    if (!file.empty()) {
        ui::sectionHeader(type.script.empty() ? tr(app, "Config", "Configuración") : "Script", ui::icon::Code);
        loadCode(app, *source, file);
        ui::monoText(file);
        if (ImGui::SmallButton(ui::label(ui::icon::Copy, tr(app, "Copy code", "Copiar código")).c_str())) {
            std::string all;
            for (const std::string& line : app.codeLines) all += line + "\n";
            ImGui::SetClipboardText(all.c_str());
            setStatus(app, "Code copied.", "Código copiado.");
        }
#ifdef _WIN32
        if (const auto real = source->vfs->resolve(file)) {
            ImGui::SameLine();
            if (ImGui::SmallButton(ui::label(ui::icon::FolderOpen, tr(app, "Show in folder", "Mostrar en la carpeta")).c_str()))
                revealInExplorer(*real);
        }
#endif
        drawCodeView(app, *source, std::max(140.0f, ImGui::GetContentRegionAvail().y));
    }
    ImGui::EndChild();
}

// --------------------------------------------------------------- centro --

void drawWelcome(NoteLabApp& app, SDL_Window* window) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float width = std::max(180.0f, std::min(660.0f, avail.x - 40.0f));
    const float height = width >= 520.0f ? 425.0f : 545.0f;
    const ImVec2 at(ImGui::GetCursorScreenPos().x + (avail.x - width) * 0.5f,
                    ImGui::GetCursorScreenPos().y + std::max(20.0f, (avail.y - height) * 0.4f));
    ImGui::SetCursorScreenPos(at);
    ImGui::BeginGroup();
    auto centered = [&](const char* text, ImFont* font, float size, ImU32 tint) {
        ImGui::PushFont(font, size);
        const float w = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorScreenPos(ImVec2(at.x + (width - w) * 0.5f, ImGui::GetCursorScreenPos().y));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(tint));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    };
    if (!app.rendererReady) {
        std::string error;
        app.rendererReady = app.renderer.init([](const std::string&) { return std::string(); }, &error);
        app.renderSource = -1;
    }
    const auto brand = app.rendererReady ? nlbrand::preview(app.renderer) : GlRenderer::PreviewImage{};
    if (brand.ok) {
        const float logoHeight = 96.0f;
        const float logoWidth = logoHeight * static_cast<float>(brand.width) / brand.height;
        ImGui::SetCursorScreenPos(ImVec2(at.x + (width - logoWidth) * 0.5f, ImGui::GetCursorScreenPos().y));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(brand.texture)), ImVec2(logoWidth, logoHeight));
        ImGui::Dummy(ImVec2(width, 12.0f));
    }
    centered("Note Lab", ui::fonts().semibold, 28.0f, ui::color::Text);
    centered(tr(app, "Create notes and HUD. Explore your mods.", "Crea notas y HUD. Explora tus mods."),
             ui::fonts().ui, 16.0f, ui::color::Muted);
    const std::string version = std::string(kPublicVersion) + " · FML Tool";
    centered(version.c_str(), ui::fonts().ui, 12.5f, ui::color::Faint);
    ImGui::Dummy(ImVec2(width, 14.0f));
    const float createWidth = (width - 10.0f) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(at.x, ImGui::GetCursorScreenPos().y));
    if (ui::primaryButton(ui::label(ui::icon::Layers, tr(app, "Create custom notes…", "Crear notas custom…")), ImVec2(createWidth, 40.0f)))
        openCustomCreator(app);
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button(ui::label(ui::icon::Photo, tr(app, "Ranking from images…", "Ranking con imágenes…")).c_str(), ImVec2(createWidth, 40.0f)))
        openCustomCreator(app, true);
    ImGui::Dummy(ImVec2(width, 8.0f));
    const bool row = width >= 520.0f;
    const float openWidth = row ? (width - 20.0f) / 3.0f : width;
    ImGui::SetCursorScreenPos(ImVec2(at.x, ImGui::GetCursorScreenPos().y));
    const bool openFolder = ImGui::Button(ui::label(ui::icon::FolderOpen, tr(app, "Open mod folder…", "Abrir carpeta de mod…")).c_str(), ImVec2(openWidth, 36.0f));
    tutorialMark("welcome-open");
    if (openFolder) openDialog(app, window, DialogAction::AddModFolder);
    if (row) ImGui::SameLine(0.0f, 10.0f); else ImGui::SetCursorScreenPos(ImVec2(at.x, ImGui::GetCursorScreenPos().y));
    if (ImGui::Button(ui::label(ui::icon::Zip, tr(app, "Open ZIP…", "Abrir ZIP…")).c_str(), ImVec2(openWidth, 36.0f)))
        openDialog(app, window, DialogAction::AddModZip);
    if (row) ImGui::SameLine(0.0f, 10.0f); else ImGui::SetCursorScreenPos(ImVec2(at.x, ImGui::GetCursorScreenPos().y));
    if (ImGui::Button(ui::label(ui::icon::Document, tr(app, "Open project…", "Abrir proyecto…")).c_str(), ImVec2(openWidth, 36.0f)))
        requestAction(app, window, PendingAction::OpenProject);
    ImGui::Dummy(ImVec2(width, 8.0f));
    centered(tr(app, "Drag & drop a mod, ZIP, project or your images.", "Arrastra un mod, ZIP, proyecto o tus imágenes."), ui::fonts().ui, 14.0f, ui::color::Faint);
    ImGui::Dummy(ImVec2(width, 16.0f));
    ImGui::PushFont(ui::fonts().semibold, 12.5f);
    const float pills = ImGui::CalcTextSize("CodenamePsychV-Slice").x + 3.0f * 14.0f + 2.0f * 8.0f;
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(at.x + (width - pills) * 0.5f, ImGui::GetCursorScreenPos().y));
    ui::pill("Codename", ui::color::Codename);
    ImGui::SameLine(0.0f, 8.0f);
    ui::pill("Psych", ui::color::Psych);
    ImGui::SameLine(0.0f, 8.0f);
    ui::pill("V-Slice", ui::color::VSlice);
    ImGui::Dummy(ImVec2(width, 6.0f));
    centered(tr(app, "The engine of each mod is detected by itself.", "El motor de cada mod se detecta solo."), ui::fonts().ui, 14.0f, ui::color::Faint);
    {
        // El tutorial, para quien llega por primera vez.
        ImGui::Dummy(ImVec2(width, 12.0f));
        const std::string label = ui::label(ui::icon::Star, tr(app, "First time? Start the tutorial", "¿Primera vez? Empieza el tutorial"));
        const float w = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f + 24.0f;
        ImGui::SetCursorScreenPos(ImVec2(at.x + (width - w) * 0.5f, ImGui::GetCursorScreenPos().y));
        if (ui::flatButton(label, nullptr, ImVec2(w, 34.0f))) startTutorial(app, kTrackGeneral, false);
    }
    if (!app.recentProjects.empty()) {
        ImGui::Dummy(ImVec2(width, 18.0f));
        ImGui::SetCursorScreenPos(ImVec2(at.x, ImGui::GetCursorScreenPos().y));
        ImGui::BeginGroup();
        ImGui::PushItemWidth(width);
        ui::sectionHeader(tr(app, "Recent projects", "Proyectos recientes"), ui::icon::List);
        for (size_t i = 0; i < app.recentProjects.size() && i < 5; ++i) {
            ImGui::PushID(static_cast<int>(i));
            const fs::path recent = pathFromUtf8(app.recentProjects[i]);
            const std::string label = ui::label(ui::icon::Document, recent.filename().u8string());
            if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(width, 0.0f))) requestAction(app, window, PendingAction::OpenRecent, recent);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", app.recentProjects[i].c_str());
            ImGui::PopID();
        }
        ImGui::PopItemWidth();
        ImGui::EndGroup();
    }
    ImGui::EndGroup();
}

void drawCenter(NoteLabApp& app, SDL_Window* window) {
    if (app.sources.empty()) {
        drawWelcome(app, window);
        return;
    }
    if (ImGui::BeginTabBar("center")) {
        const bool previewOpen = ImGui::BeginTabItem(ui::label(ui::icon::Play, tr(app, "Preview", "Vista previa")).append("###tabpreview").c_str(),
                                                     nullptr, app.requestedTab == 0 ? ImGuiTabItemFlags_SetSelected : 0);
        if (previewOpen) {
            app.centerTab = 0;
            drawPreviewTab(app);
            ImGui::EndTabItem();
        } else {
            // La cancion sigue sonando y avanzando aunque se mire otra pestana.
            updatePlayback(app, ImGui::GetIO().DeltaTime * 1000.0f);
        }
        const bool typesOpen = ImGui::BeginTabItem(ui::label(ui::icon::Puzzle, tr(app, "Custom notes", "Notas custom")).append("###tabtypes").c_str(), nullptr,
                                                   app.requestedTab == 1 ? ImGuiTabItemFlags_SetSelected : 0);
        tutorialMark("tab-types");
        if (typesOpen) {
            app.centerTab = 1;
            drawTypesTab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ui::label(ui::icon::Photo, tr(app, "Assets", "Assets")).append("###tabassets").c_str(), nullptr,
                                app.requestedTab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
            app.centerTab = 2;
            drawAssetsTab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ui::label(ui::icon::FolderOpen, tr(app, "Resources", "Recursos")).append("###tabresources").c_str(), nullptr,
                                app.requestedTab == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
            app.centerTab = 3;
            drawResourcesPanel(app, window);
            ImGui::EndTabItem();
        }
        if (app.compactLayout && ImGui::BeginTabItem(ui::label(ui::icon::Edit, tr(app, "Inspector", "Inspector")).append("###tabinspector").c_str())) {
            app.centerTab = 3;
            drawInspector(app, window);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    app.requestedTab = -1;
}

// ------------------------------------------------------------ inspector --

// ------------------------------------------------------------ miniaturas --

// Lo necesario para dibujar una miniatura: textura y rectangulo de la imagen.
struct Thumb {
    unsigned int texture = 0;
    ImVec2 uv0{0.0f, 0.0f}, uv1{1.0f, 1.0f};
    float w = 0.0f, h = 0.0f;
};

// El fotograma de una pieza: el primero o, con `clockMs` >= 0, el que toca a
// sus FPS; con la paleta de Psych si la lleva.
bool partThumb(NoteLabApp& app, const NoteStyle& style, const PartBinding& binding, double clockMs, Thumb& out) {
    if (binding.sheet < 0 || static_cast<size_t>(binding.sheet) >= style.sheets.size()) return false;
    const Sheet& sheet = style.sheets[static_cast<size_t>(binding.sheet)];
    if (sheet.image.empty()) return false;
    std::string key = psychRgbTexture(app, style, sheet, binding.part, binding.direction);
    if (key.empty()) key = sheet.image;
    const GlRenderer::PreviewImage image = app.renderer.previewImage(key);
    if (!image.ok || image.width <= 0 || image.height <= 0) return false;
    out.texture = image.texture;
    if (sheet.kind == SheetKind::Strip && sheet.columns > 0) {
        int column = binding.animation.indices.empty() ? binding.direction * 2 + (binding.part == Part::HoldEnd ? 1 : 0)
                                                       : binding.animation.indices[0];
        column = std::clamp(column, 0, sheet.columns - 1);
        out.uv0 = ImVec2(static_cast<float>(column) / sheet.columns, 0.0f);
        out.uv1 = ImVec2(static_cast<float>(column + 1) / sheet.columns, 1.0f);
        out.w = static_cast<float>(image.width) / sheet.columns;
        out.h = static_cast<float>(image.height);
        return true;
    }
    if (sheet.kind == SheetKind::Grid && sheet.columns > 0 && sheet.rows > 0) {
        // La celda que pide la pieza, numerada como Flixel: celdas de
        // floor(w / columnas) x floor(h / filas), tantas por fila como caben
        // (FlxTileFrames.hx:296-305).
        const int cellW = image.width / sheet.columns, cellH = image.height / sheet.rows;
        if (cellW <= 0 || cellH <= 0) return false;
        const int perRow = image.width / cellW;
        const std::vector<int> cells = binding.animation.indices.empty() ? std::vector<int>{0} : binding.animation.indices;
        size_t pick = 0;
        if (clockMs >= 0.0 && binding.animation.fps > 0.0f)
            pick = static_cast<size_t>(clockMs * binding.animation.fps / 1000.0) % cells.size();
        const int cell = std::max(0, cells[pick]);
        const float x = static_cast<float>((cell % perRow) * cellW), y = static_cast<float>((cell / perRow) * cellH);
        out.uv0 = ImVec2(x / image.width, y / image.height);
        out.uv1 = ImVec2((x + cellW) / image.width, (y + cellH) / image.height);
        out.w = static_cast<float>(cellW);
        out.h = static_cast<float>(cellH);
        return true;
    }
    if (sheet.kind == SheetKind::Image || sheet.kind == SheetKind::Grid || sheet.atlas.empty()) {
        out.w = static_cast<float>(image.width);
        out.h = static_cast<float>(image.height);
        return true;
    }
    const SparrowAtlas* atlas = app.atlases.get(sheet.atlas);
    if (!atlas) return false;
    std::vector<size_t> frames = framesOfAnimation(*atlas, binding.animation);
    if (frames.empty() && (style.forkNaming || style.letteredNaming)) frames = atlas->framesFor(directionKey(binding.direction));
    if (frames.empty()) return false;
    size_t pick = 0;
    if (clockMs >= 0.0) {
        const float fps = binding.animation.fps > 0.0f ? binding.animation.fps : 24.0f;
        pick = static_cast<size_t>(clockMs * fps / 1000.0) % frames.size();
    }
    const AtlasFrame& frame = atlas->frames[frames[pick]];
    const float w = static_cast<float>(frame.rotated ? frame.h : frame.w);
    const float h = static_cast<float>(frame.rotated ? frame.w : frame.h);
    out.uv0 = ImVec2(frame.x / static_cast<float>(image.width), frame.y / static_cast<float>(image.height));
    out.uv1 = ImVec2((frame.x + w) / static_cast<float>(image.width), (frame.y + h) / static_cast<float>(image.height));
    out.w = w;
    out.h = h;
    return true;
}

bool hudThumb(NoteLabApp& app, const HudAsset& asset, Thumb& out) {
    if (asset.image.empty()) return false;
    const GlRenderer::PreviewImage image = app.renderer.previewImage(asset.image);
    if (!image.ok) return false;
    out.texture = image.texture;
    out.w = static_cast<float>(image.width);
    out.h = static_cast<float>(image.height);
    return true;
}

void drawThumbAt(ImDrawList* draw, const Thumb& thumb, ImVec2 min, float size, float pad) {
    const float fit = std::min((size - pad * 2.0f) / std::max(1.0f, thumb.w), (size - pad * 2.0f) / std::max(1.0f, thumb.h));
    const float w = thumb.w * fit, h = thumb.h * fit;
    const ImVec2 at(min.x + (size - w) * 0.5f, min.y + (size - h) * 0.5f);
    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(thumb.texture)), at, ImVec2(at.x + w, at.y + h), thumb.uv0, thumb.uv1);
}

// La cabecera del editor: miniatura grande, que es en palabras, para que
// sirve y los datos tecnicos (la clave del motor, el prefijo...).
template <typename DrawThumb>
void drawEditingHeader(NoteLabApp& app, const std::string& title, const char* help, const std::string& technical, DrawThumb&& drawThumb) {
    ui::sectionHeader(tr(app, "Editing", "Editando"), ui::icon::Edit);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float box = 72.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    drawChecker(draw, at, ImVec2(at.x + box, at.y + box));
    drawThumb(draw, at, box);
    draw->AddRect(at, ImVec2(at.x + box, at.y + box), ui::color::Border, 6.0f);
    ImGui::Dummy(ImVec2(box, box));
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginGroup();
    ImGui::PushFont(ui::fonts().semibold, 16.0f);
    ImGui::TextWrapped("%s", title.c_str());
    ImGui::PopFont();
    ui::caption(help);
    ui::monoText(technical);
    ImGui::EndGroup();
    ImGui::Spacing();
}

// Menu de clic derecho de una pieza.
void drawPartMenu(NoteLabApp& app, Source& source, const PartBinding& part, size_t i) {
    if (!ImGui::BeginPopupContextItem("partmenu")) return;
    ui::caption((std::string(partLabel(part.part, app.spanish)) + " " + directionLabel(part.direction, app.spanish)).c_str());
    if (menuItem(ui::icon::Edit, tr(app, "Edit", "Editar"))) {
        app.editPart = static_cast<int>(i);
        app.editHud = -1;
    }
    if (menuItem(ui::icon::Photo, tr(app, "See it in the asset viewer", "Verla en el visor de assets"))) {
        app.editPart = static_cast<int>(i);
        app.editHud = -1;
        app.assetKind = 0;
        app.assetIndex = part.sheet;
        app.assetPart = static_cast<int>(i);
        app.assetClockMs = 0.0;
        app.requestedTab = 2;
    }
    if (menuItem(ui::icon::Copy, tr(app, "Copy its prefix", "Copiar su prefijo"), nullptr, false, !part.animation.prefix.empty()))
        copyText(app, part.animation.prefix);
    const auto original = source.originals.find(static_cast<size_t>(app.selStyle));
    const bool restorable = original != source.originals.end() && i < original->second.parts.size();
    if (menuItem(ui::icon::Restart, tr(app, "Back to the mod's piece", "Volver a la pieza del mod"), nullptr, false, restorable)) {
        const Animation animation = original->second.parts[i].animation;
        beginEdit(app);
        if (NoteStyle* current = mutableStyle(app)) current->parts[i].animation = animation;
        afterEdit(app);
        app.buffersFor.clear();
    }
    ImGui::EndPopup();
}

// Las piezas en una cuadricula: una fila por clase de pieza (y variante) y una
// columna por direccion, cada una con su miniatura. Clic para editarla.
void drawPartsGrid(NoteLabApp& app, Source& source, const NoteStyle& style, const StyleReport& report) {
    struct Row { Part part; int variant; };
    std::vector<Row> rows;
    for (int p = 0; p < kPartCount; ++p) {
        int maxVariant = -1;
        for (const PartBinding& binding : style.parts)
            if (binding.part == static_cast<Part>(p)) maxVariant = std::max(maxVariant, binding.variant);
        for (int v = 0; v <= maxVariant; ++v) rows.push_back({static_cast<Part>(p), v});
    }
    const float cell = 44.0f, gap = 6.0f;
    const float labelWidth = std::max(90.0f, ImGui::GetContentRegionAvail().x - 4.0f * (cell + gap));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // Cabecera: las cuatro direcciones.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        for (int d = 0; d < 4; ++d) {
            const std::string text = directionLabel(d, app.spanish);
            const std::string arrow = text.substr(0, text.find(' '));
            ImGui::PushFont(ui::fonts().semibold, 15.0f);
            const ImVec2 size = ImGui::CalcTextSize(arrow.c_str());
            ImGui::PopFont();
            const float x = at.x + labelWidth + gap + d * (cell + gap) + (cell - size.x) * 0.5f;
            draw->AddText(ui::fonts().semibold, 15.0f, ImVec2(x, at.y), ui::color::Muted, arrow.c_str());
        }
        ImGui::Dummy(ImVec2(labelWidth, 18.0f));
    }
    for (const Row& row : rows) {
        ImGui::PushID(static_cast<int>(row.part) * 16 + row.variant);
        std::string label = partLabel(row.part, app.spanish);
        if (row.variant > 0) label += " " + std::to_string(row.variant + 1);
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        ImGui::PushFont(nullptr, 13.0f);
        ImGui::PushTextWrapPos(rowStart.x + labelWidth);
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str(), nullptr, false, labelWidth);
        ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + std::max(0.0f, (cell - labelSize.y) * 0.5f)));
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", partHelp(row.part, app.spanish));
        ImGui::SetCursorScreenPos(ImVec2(rowStart.x + labelWidth, rowStart.y));
        for (int d = 0; d < 4; ++d) {
            ImGui::SameLine(0.0f, gap);
            if (d == 0) ImGui::SetCursorScreenPos(ImVec2(rowStart.x + labelWidth + gap, rowStart.y));
            int index = -1;
            for (size_t i = 0; i < style.parts.size(); ++i)
                if (style.parts[i].part == row.part && style.parts[i].direction == d && style.parts[i].variant == row.variant) {
                    index = static_cast<int>(i);
                    break;
                }
            ImGui::PushID(d);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("cell", ImVec2(cell, cell));
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 max(at.x + cell, at.y + cell);
            draw->AddRectFilled(at, max, hovered ? IM_COL32(38, 43, 55, 255) : ui::color::Raised, 7.0f);
            if (index >= 0) {
                const PartBinding& binding = style.parts[static_cast<size_t>(index)];
                const int frames = static_cast<size_t>(index) < report.parts.size() ? report.parts[static_cast<size_t>(index)].frames : 0;
                Thumb thumb;
                if (partThumb(app, style, binding, -1.0, thumb)) drawThumbAt(draw, thumb, at, cell, 5.0f);
                const bool chosen = app.editPart == index && app.editHud < 0;
                // Lo que pone un script de una forma que no se lee no se comprueba: no va en rojo.
                const bool missing = frames <= 0 && !binding.inherited && !binding.unread;
                if (chosen) draw->AddRect(at, max, ui::color::Accent, 7.0f, 0, 2.0f);
                else if (missing) draw->AddRect(at, max, ui::color::Error, 7.0f, 0, 1.5f);
                if (binding.inherited) draw->AddCircleFilled(ImVec2(max.x - 6.0f, at.y + 6.0f), 3.0f, ui::color::Info);
                if (frames > 1) {
                    const std::string count = std::to_string(frames);
                    draw->AddText(ui::fonts().semibold, 11.0f, ImVec2(max.x - 5.0f - count.size() * 6.0f, max.y - 14.0f), ui::color::Muted, count.c_str());
                }
                if (clicked) {
                    app.editPart = index;
                    app.editHud = -1;
                    app.assetKind = 0;
                    app.assetIndex = binding.sheet;
                    app.assetPart = index;
                    app.assetClockMs = 0.0;
                    app.editClockMs = 0.0;
                }
                if (hovered) {
                    std::string tip = std::string(partLabel(binding.part, app.spanish)) + " · " + directionLabel(binding.direction, app.spanish) +
                                      "\n" + partHelp(binding.part, app.spanish) + "\n\n" + tr(app, "Prefix: ", "Prefijo: ") +
                                      (binding.animation.prefix.empty() ? "-" : binding.animation.prefix) + " · " + std::to_string(frames) +
                                      tr(app, " frames", " fotogramas");
                    if (missing) tip += tr(app, "\nThe engine would draw nothing here.", "\nEl motor no dibujaría nada aquí.");
                    if (binding.inherited) tip += tr(app, "\nInherited from the fallback style.", "\nHeredada del estilo de respaldo.");
                    if (binding.unread)
                        tip += tr(app, "\nSet by the note type's script in a way Note Lab cannot read; not checked.",
                                  "\nLa pone el script del tipo de una forma que Note Lab no sabe leer; no se comprueba.");
                    else if (!style.lookScript.empty())
                        tip += tr(app, "\nFrom the note type's script: ", "\nDel script del tipo: ") + style.lookScript;
                    tip += "\n(" + partSubject(binding) + ")";
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                drawPartMenu(app, source, binding, static_cast<size_t>(index));
            } else {
                draw->AddText(ImVec2(at.x + cell * 0.5f - 3.0f, at.y + cell * 0.5f - 8.0f), ui::color::Faint, "-");
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + cell + gap));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::PopID();
    }
}

std::string hudName(const NoteLabApp& app, int index) {
    if (index >= 0 && index < 4) return std::string(tr(app, "Judgement \"", "Juicio «")) + fml::notelab::judgementLabel(index, app.spanish) + tr(app, "\"", "»");
    if (index == 4) return tr(app, "\"combo\" image", "Imagen «combo»");
    if (index >= 5 && index < 15) return std::string(tr(app, "Digit ", "Cifra ")) + std::to_string(index - 5);
    if (index >= 15 && index < 19) return std::string(tr(app, "Countdown \"", "Cuenta atrás «")) + countdownLabel(index - 15, app.spanish) + tr(app, "\"", "»");
    return "?";
}

const char* hudHelp(const NoteLabApp& app, int index) {
    if (index >= 0 && index < 4) return tr(app, "The image that pops up when a note is hit with this judgement.",
                                                "La imagen que salta al acertar una nota con este juicio.");
    if (index == 4) return tr(app, "The \"combo\" word shown next to the digits.", "La palabra «combo» que acompaña a las cifras.");
    if (index >= 5 && index < 15) return tr(app, "One digit of the combo counter.", "Una cifra del contador de combo.");
    return tr(app, "The image and sound of this step of the countdown before the song (\"3\" is usually sound only).",
                   "La imagen y el sonido de este paso de la cuenta atrás antes de la canción («3» suele ser solo sonido).");
}

// El HUD del estilo: juicios, combo, cifras y cuenta atras, con miniaturas.
// Las imagenes del HUD que se editan: la del editor y, si esta entre ellas,
// las que se sumaron con Ctrl o Mayus (pedido del autor del 29 sep: aplicar
// valores en grupo y no una a una).
std::vector<int> hudSelected(const NoteLabApp& app) {
    if (app.editHud < 0) return {};
    if (app.hudSelection.size() > 1 && app.hudSelection.count(app.editHud))
        return std::vector<int>(app.hudSelection.begin(), app.hudSelection.end());
    return {app.editHud};
}

// Como en un explorador: clic, una; Ctrl+clic, suma o quita; Mayus+clic, el
// tramo desde la ultima.
void clickHud(NoteLabApp& app, int index, bool ctrl, bool shift) {
    const bool active = app.editHud >= 0 && (app.hudSelection.count(app.editHud) || app.hudSelection.empty());
    if (shift && active && app.hudAnchor >= 0) {
        app.hudSelection.clear();
        for (int i = std::min(app.hudAnchor, index); i <= std::max(app.hudAnchor, index); ++i) app.hudSelection.insert(i);
        app.editHud = index;
    } else if (ctrl && active) {
        if (app.hudSelection.empty()) app.hudSelection.insert(app.editHud);
        if (app.hudSelection.count(index) && app.hudSelection.size() > 1) {
            app.hudSelection.erase(index);
            if (app.editHud == index) app.editHud = *app.hudSelection.begin();
        } else {
            app.hudSelection.insert(index);
            app.editHud = index;
        }
        app.hudAnchor = index;
    } else {
        app.hudSelection = {index};
        app.editHud = index;
        app.hudAnchor = index;
    }
    app.editPart = -1;
}

void selectHudRange(NoteLabApp& app, int from, int to) {
    app.hudSelection.clear();
    for (int i = from; i <= to; ++i) app.hudSelection.insert(i);
    app.editHud = from;
    app.hudAnchor = from;
    app.editPart = -1;
}

void drawHudGrid(NoteLabApp& app, const NoteStyle& style) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const std::vector<int> selected = hudSelected(app);
    const bool group = selected.size() > 1;
    auto cellFor = [&](int index, const HudAsset& asset, const std::string& caption, float size) {
        ImGui::PushID(index);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("hud", ImVec2(size, size + (caption.empty() ? 0.0f : 16.0f)));
        const bool hovered = ImGui::IsItemHovered();
        const bool chosen = std::find(selected.begin(), selected.end(), index) != selected.end();
        const ImVec2 max(at.x + size, at.y + size);
        draw->AddRectFilled(at, max, hovered ? IM_COL32(38, 43, 55, 255) : chosen && group ? IM_COL32(48, 42, 72, 255) : ui::color::Raised, 7.0f);
        Thumb thumb;
        if (hudThumb(app, asset, thumb)) drawThumbAt(draw, thumb, at, size, 4.0f);
        else if (!asset.imageOptional) draw->AddText(ImVec2(at.x + size * 0.5f - 3.0f, at.y + size * 0.5f - 8.0f), ui::color::Faint, "-");
        if (!asset.sound.empty() && ui::fonts().icons)
            draw->AddText(ui::fonts().ui, 11.0f, ImVec2(max.x - 13.0f, at.y + 3.0f), ui::color::Info, ui::icon::Volume);
        if (app.editHud == index) draw->AddRect(at, max, ui::color::Accent, 7.0f, 0, 2.0f);
        else if (chosen) draw->AddRect(at, max, ui::withAlpha(ui::color::Accent, 170), 7.0f, 0, 1.5f);
        else if (asset.image.empty() && !asset.imageOptional) draw->AddRect(at, max, ui::withAlpha(ui::color::Warning, 150), 7.0f, 0, 1.0f);
        if (!caption.empty()) {
            ImGui::PushFont(nullptr, 12.0f);
            const float w = ImGui::CalcTextSize(caption.c_str()).x;
            ImGui::PopFont();
            draw->AddText(ui::fonts().ui, 12.0f, ImVec2(at.x + (size - w) * 0.5f, max.y + 1.0f), ui::color::Muted, caption.c_str());
        }
        if (clicked) clickHud(app, index, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
        if (ImGui::BeginPopupContextItem("hudmenu")) {
            if (menuItem(ui::icon::Check, tr(app, "Only this one", "Solo esta"))) clickHud(app, index, false, false);
            if (menuItem(ui::icon::Add, tr(app, "Add to the selection", "Añadir a la selección"), "Ctrl+clic", false, app.editHud >= 0 && !chosen))
                clickHud(app, index, true, false);
            ImGui::Separator();
            if (menuItem(ui::icon::Grid, tr(app, "All the judgements", "Todos los juicios"))) selectHudRange(app, 0, 3);
            if (menuItem(ui::icon::Grid, tr(app, "All the combo digits", "Todas las cifras del combo"))) selectHudRange(app, 5, 14);
            if (menuItem(ui::icon::Grid, tr(app, "The whole countdown", "Toda la cuenta atrás"))) selectHudRange(app, 15, 18);
            if (menuItem(ui::icon::Layers, tr(app, "The whole HUD", "Todo el HUD"))) selectHudRange(app, 0, 18);
            ImGui::EndPopup();
        } else if (hovered) {
            std::string tip = hudName(app, index) + "\n" + hudHelp(app, index);
            tip += "\n\n" + (asset.image.empty() ? std::string(tr(app, "No image", "Sin imagen")) : asset.image);
            if (!asset.sound.empty()) tip += "\n" + asset.sound;
            tip += std::string("\n\n") + tr(app, "Ctrl+click adds it to the selection; Shift+click picks a range.",
                                           "Ctrl+clic la suma a la selección; Mayús+clic elige un tramo.");
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::PopID();
    };
    // La etiqueta de la fila y «todas», que elige la fila entera.
    auto rowLabel = [&](const char* text, int from, int to) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
        ImGui::PushFont(nullptr, 13.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::PushID(from);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Accent));
        ImGui::PushFont(nullptr, 12.5f);
        if (ImGui::Selectable(tr(app, "all", "todas"), false, ImGuiSelectableFlags_None, ImGui::CalcTextSize(tr(app, "all", "todas"))))
            selectHudRange(app, from, to);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ui::tooltip(tr(app, "Select the whole row to change it at once", "Elegir la fila entera para cambiarla de una vez"));
        ImGui::PopID();
    };
    rowLabel(tr(app, "Judgements", "Juicios"), 0, 3);
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0.0f, 6.0f);
        cellFor(i, style.judgements[static_cast<size_t>(i)], fml::notelab::judgementLabel(i, app.spanish), 52.0f);
    }
    ImGui::SameLine(0.0f, 14.0f);
    cellFor(4, style.combo, "combo", 52.0f);
    rowLabel(tr(app, "Combo digits", "Cifras del combo"), 5, 14);
    for (int i = 0; i < 10; ++i) {
        if (i) ImGui::SameLine(0.0f, 4.0f);
        cellFor(5 + i, style.digits[static_cast<size_t>(i)], std::string(), 28.0f);
    }
    rowLabel(tr(app, "Countdown", "Cuenta atrás"), 15, 18);
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0.0f, 6.0f);
        cellFor(15 + i, style.countdown[static_cast<size_t>(i)], countdownLabel(i, app.spanish), 44.0f);
    }
}

// «Usar el HUD de…»: cualquier estilo con HUD de los mods abiertos, agrupados
// por mod. Devuelve true si se tomo uno (el estilo cambio).
bool drawBorrowHudButton(NoteLabApp& app) {
    if (ImGui::SmallButton(ui::label(ui::icon::Layers, tr(app, "Use the HUD of…", "Usar el HUD de…")).c_str()))
        ImGui::OpenPopup("borrowhud");
    ui::tooltip(tr(app, "Take the judgements, combo, numbers and countdown of another style, also from another open mod, "
                        "without switching mods. Nothing is written into any mod.",
                        "Tomar los juicios, el combo, los números y la cuenta atrás de otro estilo, también de otro mod "
                        "abierto, sin cambiar de mod. No se escribe nada en ningún mod."));
    int pickSource = -1, pickStyle = -1;
    if (ImGui::BeginPopup("borrowhud")) {
        bool any = false;
        for (size_t s = 0; s < app.sources.size(); ++s) {
            const Source& source = *app.sources[s];
            bool header = false;
            for (size_t i = 0; i < source.catalog.styles.size(); ++i) {
                const NoteStyle& style = source.catalog.styles[i];
                if (!style.hasHud || (static_cast<int>(s) == app.selSource && static_cast<int>(i) == app.selStyle)) continue;
                if (!header) {
                    ImGui::SeparatorText(sourceName(source).c_str());
                    header = true;
                }
                ImGui::PushID(static_cast<int>(s * 100000 + i));
                const std::string label = style.name + "  ·  " + engineLabel(style.engine);
                if (ImGui::MenuItem(label.c_str())) {
                    pickSource = static_cast<int>(s);
                    pickStyle = static_cast<int>(i);
                }
                ImGui::PopID();
                any = true;
            }
        }
        if (!any) ImGui::TextDisabled("%s", tr(app, "No other open style has a HUD.", "Ningún otro estilo abierto tiene HUD."));
        ImGui::EndPopup();
    }
    if (pickSource < 0) return false;
    borrowHud(app, pickSource, pickStyle);
    return true;
}

// Varias imagenes del HUD a la vez: la escala comun o relativa, el pixel art
// y volver a lo del mod, con un solo paso de deshacer por cambio.
void drawHudGroupEditor(NoteLabApp& app, const std::vector<int>& selected) {
    NoteStyle* style = mutableStyle(app);
    if (!style) return;
    std::vector<HudAsset*> assets;
    for (int index : selected)
        if (HudAsset* asset = hudAssetAt(*style, index)) assets.push_back(asset);
    if (assets.empty()) return;
    std::string names;
    for (size_t i = 0; i < selected.size() && i < 6; ++i) names += (i ? ", " : "") + hudName(app, selected[i]);
    if (selected.size() > 6) names += ", …";
    const std::string title = std::to_string(selected.size()) + tr(app, " HUD images", " imágenes del HUD");
    drawEditingHeader(app, title, tr(app, "What you change here applies to all of them at once, with a single undo step.",
                                          "Lo que cambies aquí se aplica a todas a la vez, con un solo paso de deshacer."),
                      names, [&](ImDrawList* draw, ImVec2 at, float box) {
                          Thumb thumb;
                          if (HudAsset* first = hudAssetAt(*style, app.editHud))
                              if (hudThumb(app, *first, thumb)) drawThumbAt(draw, thumb, at, box, 6.0f);
                      });
    // Aplica `change` a cada imagen elegida del estilo actual.
    auto applyAll = [&](auto&& change) {
        NoteStyle* current = mutableStyle(app);
        if (!current) return;
        for (int index : selected)
            if (HudAsset* asset = hudAssetAt(*current, index)) change(*asset);
    };
    float low = assets.front()->scale, high = low;
    int pixels = 0;
    for (const HudAsset* asset : assets) {
        low = std::min(low, asset->scale);
        high = std::max(high, asset->scale);
        pixels += asset->pixel ? 1 : 0;
    }
    const bool mixed = high - low > 0.0005f;
    const float column = 96.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    ImGui::TextUnformatted(tr(app, "Scale", "Escala"));
    ImGui::PopStyleColor();
    ImGui::SameLine(column);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 26.0f);
    float scale = hudAssetAt(*style, app.editHud) ? hudAssetAt(*style, app.editHud)->scale : low;
    const std::string format = mixed ? std::string("%.3f  (") + tr(app, "mixed", "distintas") + ")" : std::string("%.3f");
    const bool scaleChanged = ImGui::DragFloat("##groupscale", &scale, 0.005f, 0.05f, 4.0f, format.c_str());
    if (ImGui::IsItemActivated()) beginEdit(app);
    if (scaleChanged) {
        applyAll([&](HudAsset& asset) { asset.scale = std::clamp(scale, 0.05f, 4.0f); });
        afterEdit(app);
    }
    helpMarker(tr(app, "Dragging gives them all the same scale. The buttons below change each one keeping their differences.",
                      "Arrastrar les da a todas la misma escala. Los botones de abajo cambian cada una manteniendo sus diferencias."));
    ImGui::Dummy(ImVec2(column - ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::SameLine();
    auto nudge = [&](const char* label, float factor) {
        if (ImGui::SmallButton(label)) {
            beginEdit(app);
            applyAll([&](HudAsset& asset) { asset.scale = std::clamp(asset.scale * factor, 0.05f, 4.0f); });
            afterEdit(app);
        }
        ImGui::SameLine(0.0f, 4.0f);
    };
    nudge("-10 %", 0.9f);
    nudge("+10 %", 1.1f);
    nudge("x0.5", 0.5f);
    nudge("x2", 2.0f);
    if (ImGui::SmallButton("= 1")) {
        beginEdit(app);
        applyAll([&](HudAsset& asset) { asset.scale = 1.0f; });
        afterEdit(app);
    }
    // Pixel art: casilla a medias si solo lo son algunas.
    bool pixel = pixels == static_cast<int>(assets.size());
    const bool pixelMixed = pixels > 0 && !pixel;
    if (pixelMixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    if (ImGui::Checkbox(tr(app, "Pixel art", "Pixel art"), &pixel)) {
        beginEdit(app);
        const bool value = pixelMixed ? true : pixel;
        applyAll([&](HudAsset& asset) { asset.pixel = value; });
        afterEdit(app);
    }
    if (pixelMixed) ImGui::PopItemFlag();
    Source* source = selectedSource(app);
    const auto original = source ? source->originals.find(static_cast<size_t>(app.selStyle)) : std::map<size_t, NoteStyle>::iterator{};
    const bool restorable = source && original != source->originals.end();
    ImGui::BeginDisabled(!restorable);
    if (ImGui::Button(ui::label(ui::icon::Restart, tr(app, "Back to the mod's", "Volver a las del mod")).c_str())) {
        const NoteStyle copy = original->second;
        beginEdit(app);
        NoteStyle* current = mutableStyle(app);
        for (int index : selected) {
            const HudAsset* before = hudAssetAt(const_cast<NoteStyle&>(copy), index);
            if (HudAsset* asset = current ? hudAssetAt(*current, index) : nullptr) *asset = before ? *before : HudAsset{};
        }
        afterEdit(app);
    }
    ImGui::EndDisabled();
    ui::caption(tr(app, "To change the image or the sound, pick a single one.",
                        "Para cambiar la imagen o el sonido, elige una sola."));
}

void drawHudEditor(NoteLabApp& app, SDL_Window* window) {
    const std::vector<int> selected = hudSelected(app);
    if (selected.size() > 1) {
        drawHudGroupEditor(app, selected);
        return;
    }
    NoteStyle* style = mutableStyle(app);
    HudAsset* asset = style ? hudAssetAt(*style, app.editHud) : nullptr;
    if (!asset) return;
    const int index = app.editHud;
    const std::string technical = asset->image.empty() ? std::string(tr(app, "no image", "sin imagen")) : asset->image;
    drawEditingHeader(app, hudName(app, index), hudHelp(app, index), technical, [&](ImDrawList* draw, ImVec2 at, float box) {
        Thumb thumb;
        if (hudThumb(app, *asset, thumb)) drawThumbAt(draw, thumb, at, box, 6.0f);
    });
    if (asset->inherited)
        ui::caption(tr(app, "Inherited from the fallback style.", "Heredada del estilo de respaldo."));
    const float column = 96.0f;
    auto name = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::SameLine(column);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 26.0f);
    };
    name(tr(app, "Scale", "Escala"));
    float scale = asset->scale;
    const bool scaleChanged = ImGui::DragFloat("##hudscale", &scale, 0.005f, 0.05f, 4.0f, "%.3f");
    if (ImGui::IsItemActivated()) beginEdit(app);
    if (scaleChanged) {
        style = mutableStyle(app);
        if (HudAsset* current = hudAssetAt(*style, index)) current->scale = std::clamp(scale, 0.05f, 4.0f);
        afterEdit(app);
    }
    helpMarker(tr(app, "Size of the image in the game (1 = its pixels).", "Tamaño de la imagen en el juego (1 = sus píxeles)."));
    bool pixel = asset->pixel;
    if (ImGui::Checkbox(tr(app, "Pixel art", "Pixel art"), &pixel)) {
        beginEdit(app);
        style = mutableStyle(app);
        if (HudAsset* current = hudAssetAt(*style, index)) current->pixel = pixel;
        afterEdit(app);
    }
    if (ImGui::Button(ui::label(ui::icon::Photo, tr(app, "Change image…", "Cambiar imagen…")).c_str())) {
        app.importHud = index;
        openDialog(app, window, DialogAction::ImportHudImage);
    }
    ui::tooltip(tr(app, "Use your own PNG for this HUD image. Nothing is written into the mod.",
                        "Usar tu propio PNG para esta imagen del HUD. No se escribe nada en el mod."));
    if (index >= 15) {
        ImGui::SameLine();
        if (ImGui::Button(ui::label(ui::icon::Volume, tr(app, "Change sound…", "Cambiar sonido…")).c_str())) {
            app.importHud = index;
            openDialog(app, window, DialogAction::ImportHudSound);
        }
        ui::monoText(asset->sound.empty() ? std::string(tr(app, "no sound", "sin sonido")) : asset->sound);
    }
    Source* source = selectedSource(app);
    const auto original = source ? source->originals.find(static_cast<size_t>(app.selStyle)) : std::map<size_t, NoteStyle>::iterator{};
    const bool restorable = source && original != source->originals.end();
    ImGui::BeginDisabled(!restorable);
    if (ImGui::Button(ui::label(ui::icon::Restart, tr(app, "Back to the mod's", "Volver a la del mod")).c_str())) {
        NoteStyle copy = original->second;
        const HudAsset* before = hudAssetAt(copy, index);
        const HudAsset saved = before ? *before : HudAsset{};
        beginEdit(app);
        style = mutableStyle(app);
        if (HudAsset* current = hudAssetAt(*style, index)) *current = saved;
        afterEdit(app);
    }
    ImGui::EndDisabled();
}

void drawPieceEditor(NoteLabApp& app, SDL_Window* window) {
    NoteStyle* style = mutableStyle(app);
    if (!style || app.editPart < 0 || app.editPart >= static_cast<int>(style->parts.size())) {
        ui::caption(tr(app, "Click a piece in the grid (or a HUD image) to edit it.",
                            "Haz clic en una pieza de la cuadrícula (o en una imagen del HUD) para editarla."));
        return;
    }
    const size_t partIndex = static_cast<size_t>(app.editPart);
    const std::string key = std::to_string(app.selSource) + ":" + std::to_string(app.selStyle) + ":" + std::to_string(app.editPart);
    if (app.buffersFor != key) {
        app.buffersFor = key;
        const Animation& anim = style->parts[partIndex].animation;
        std::snprintf(app.prefixBuffer.data(), app.prefixBuffer.size(), "%s", anim.prefix.c_str());
        std::string indices;
        for (size_t i = 0; i < anim.indices.size(); ++i) indices += (i ? "," : "") + std::to_string(anim.indices[i]);
        std::snprintf(app.indicesBuffer.data(), app.indicesBuffer.size(), "%s", indices.c_str());
    }
    {
        // Que se edita, en palabras, con su miniatura animada.
        const PartBinding& binding = style->parts[partIndex];
        const StyleReport* report = selectedReport(app);
        const int frames = report && partIndex < report->parts.size() ? report->parts[partIndex].frames : 0;
        std::string title = std::string(partLabel(binding.part, app.spanish)) + " · " + directionLabel(binding.direction, app.spanish);
        if (binding.variant > 0) title += std::string(tr(app, " · variant ", " · variante ")) + std::to_string(binding.variant + 1);
        char tech[256];
        std::snprintf(tech, sizeof(tech), "%s · «%s» · %d %s · %.0f fps", partSubject(binding).c_str(),
                      binding.animation.prefix.empty() ? "-" : binding.animation.prefix.c_str(), frames,
                      tr(app, "frames", "fotogramas"), binding.animation.fps);
        drawEditingHeader(app, title, partHelp(binding.part, app.spanish), tech,
                          [&](ImDrawList* draw, ImVec2 at, float box) {
                              Thumb thumb;
                              app.editClockMs += ImGui::GetIO().DeltaTime * 1000.0;
                              if (partThumb(app, *style, binding, app.editClockMs, thumb)) drawThumbAt(draw, thumb, at, box, 6.0f);
                          });
        if (binding.inherited)
            ui::caption(tr(app, "Inherited from the fallback style: editing it here changes this style only.",
                                "Heredada del estilo de respaldo: editarla aquí cambia solo este estilo."));
    }
    const int sheetIndex = style->parts[partIndex].sheet;
    const bool hasSheet = sheetIndex >= 0 && sheetIndex < static_cast<int>(style->sheets.size());
    const bool strip = hasSheet && style->sheets[static_cast<size_t>(sheetIndex)].kind == SheetKind::Strip;
    const SparrowAtlas* atlas = hasSheet && !style->sheets[static_cast<size_t>(sheetIndex)].atlas.empty() &&
                                app.renderSource == app.selSource
                                ? app.atlases.get(style->sheets[static_cast<size_t>(sheetIndex)].atlas) : nullptr;
    const float column = 96.0f;
    const float field = ImGui::GetContentRegionAvail().x - column - 26.0f;
    auto name = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::SameLine(column);
        ImGui::SetNextItemWidth(field);
    };
    // Aplica un cambio puntual (combo, casilla, texto) con su paso de deshacer.
    auto apply = [&](auto&& change) {
        beginEdit(app);
        style = mutableStyle(app);
        change(*style);
        afterEdit(app);
    };

    if (strip) {
        ui::caption(tr(app, "This piece is a column of a hold strip (V-Slice): its image is edited as a whole sheet below.",
                            "Esta pieza es una columna de una tira de sostenidos (V-Slice): su imagen se edita como hoja, abajo."));
    } else {
        name(tr(app, "Animation", "Animación"));
        const std::string currentPrefix = style->parts[partIndex].animation.prefix;
        if (ImGui::BeginCombo("##anim", currentPrefix.empty() ? "-" : currentPrefix.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (atlas) {
                for (const auto& group : atlas->byPrefix) {
                    if (group.second.empty()) continue;
                    const std::string label = group.first + "  (" + std::to_string(group.second.size()) + ")";
                    if (ImGui::Selectable(label.c_str(), currentPrefix == group.first)) {
                        const std::string prefix = uniquePrefixFor(*atlas, atlas->frames[group.second.front()].name);
                        apply([&](NoteStyle& s) {
                            Animation& a = s.parts[partIndex].animation;
                            a.prefix = prefix;
                            a.alternatives.clear();
                            a.indices.clear();
                        });
                        app.buffersFor.clear();
                    }
                }
            } else {
                ImGui::TextDisabled("%s", tr(app, "The atlas could not be read.", "No se pudo leer el atlas."));
            }
            ImGui::EndCombo();
        }
        name(tr(app, "Prefix", "Prefijo"));
        const bool prefixEntered = ImGui::InputText("##prefix", app.prefixBuffer.data(), app.prefixBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if ((prefixEntered || ImGui::IsItemDeactivatedAfterEdit()) && style->parts[partIndex].animation.prefix != app.prefixBuffer.data()) {
            const std::string prefix = app.prefixBuffer.data();
            apply([&](NoteStyle& s) {
                s.parts[partIndex].animation.prefix = prefix;
                s.parts[partIndex].animation.alternatives.clear();
            });
        }
        helpMarker(tr(app, "The engine takes every frame whose name STARTS with this text (Flixel findByPrefix).",
                          "El motor toma todos los fotogramas cuyo nombre EMPIEZA por este texto (findByPrefix de Flixel)."));
        if (!style->parts[partIndex].animation.alternatives.empty()) {
            std::string alternatives;
            for (const std::string& a : style->parts[partIndex].animation.alternatives) alternatives += (alternatives.empty() ? "" : ", ") + a;
            ui::caption((std::string(tr(app, "Also accepted: ", "También vale: ")) + alternatives).c_str());
        }
        name(tr(app, "Indices", "Índices"));
        const bool indicesEntered = ImGui::InputTextWithHint("##indices", tr(app, "all frames", "todos los fotogramas"),
                                                             app.indicesBuffer.data(), app.indicesBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (indicesEntered || ImGui::IsItemDeactivatedAfterEdit()) {
            std::vector<int> indices;
            const std::string text = app.indicesBuffer.data();
            size_t start = 0;
            while (start <= text.size()) {
                size_t comma = text.find(',', start);
                if (comma == std::string::npos) comma = text.size();
                const std::string item = text.substr(start, comma - start);
                try { if (item.find_first_not_of(" \t") != std::string::npos) indices.push_back(std::stoi(item)); } catch (...) {}
                start = comma + 1;
            }
            if (indices != style->parts[partIndex].animation.indices)
                apply([&](NoteStyle& s) { s.parts[partIndex].animation.indices = indices; });
        }
        helpMarker(tr(app, "Optional: only these frame numbers, in this order (addByIndices).",
                          "Opcional: solo estos números de fotograma, en este orden (addByIndices)."));
        name("FPS");
        ImGui::SetNextItemWidth(field * 0.5f);
        float fps = style->parts[partIndex].animation.fps;
        const bool fpsChanged = ImGui::DragFloat("##fps", &fps, 0.25f, 1.0f, 120.0f, "%.1f");
        if (ImGui::IsItemActivated()) beginEdit(app);
        if (fpsChanged) {
            style = mutableStyle(app);
            style->parts[partIndex].animation.fps = std::clamp(fps, 1.0f, 120.0f);
            afterEdit(app);
        }
        ImGui::SameLine();
        bool loop = style->parts[partIndex].animation.loop;
        if (ImGui::Checkbox(tr(app, "Loop", "Bucle"), &loop))
            apply([&](NoteStyle& s) { s.parts[partIndex].animation.loop = loop; });
        name("Offset");
        float offset[2] = {style->parts[partIndex].animation.offsetX, style->parts[partIndex].animation.offsetY};
        const bool offsetChanged = ImGui::DragFloat2("##offset", offset, 0.5f, -2000.0f, 2000.0f, "%.1f");
        if (ImGui::IsItemActivated()) beginEdit(app);
        if (offsetChanged) {
            style = mutableStyle(app);
            style->parts[partIndex].animation.offsetX = offset[0];
            style->parts[partIndex].animation.offsetY = offset[1];
            afterEdit(app);
        }
    }

    if (!hasSheet) return;
    const size_t s = static_cast<size_t>(sheetIndex);
    ui::sectionHeader((std::string(tr(app, "Sheet · ", "Hoja · ")) +
                       (style->sheets[s].declared.empty() ? style->sheets[s].image : style->sheets[s].declared)).c_str(), ui::icon::Photo);
    ui::monoText(style->sheets[s].image.empty() ? tr(app, "image not found", "imagen no encontrada") : style->sheets[s].image);
    name(tr(app, "Scale", "Escala"));
    float scale = style->sheets[s].scale;
    const bool scaleChanged = ImGui::DragFloat("##scale", &scale, 0.005f, 0.05f, 4.0f, "%.3f");
    if (ImGui::IsItemActivated()) beginEdit(app);
    if (scaleChanged) {
        style = mutableStyle(app);
        style->sheets[s].scale = std::clamp(scale, 0.05f, 4.0f);
        afterEdit(app);
    }
    name(tr(app, "Opacity", "Opacidad"));
    float alpha = style->sheets[s].alpha;
    const bool alphaChanged = ImGui::SliderFloat("##alpha", &alpha, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemActivated()) beginEdit(app);
    if (alphaChanged) {
        style = mutableStyle(app);
        style->sheets[s].alpha = alpha;
        afterEdit(app);
    }
    name("Offset");
    float sheetOffset[2] = {style->sheets[s].offsetX, style->sheets[s].offsetY};
    const bool sheetOffsetChanged = ImGui::DragFloat2("##sheetoffset", sheetOffset, 0.5f, -2000.0f, 2000.0f, "%.1f");
    if (ImGui::IsItemActivated()) beginEdit(app);
    if (sheetOffsetChanged) {
        style = mutableStyle(app);
        style->sheets[s].offsetX = sheetOffset[0];
        style->sheets[s].offsetY = sheetOffset[1];
        afterEdit(app);
    }
    bool pixel = style->sheets[s].pixel;
    if (ImGui::Checkbox(tr(app, "Pixel art", "Pixel art"), &pixel))
        apply([&](NoteStyle& st) { st.sheets[s].pixel = pixel; });
    if (!strip) {
        ImGui::SameLine();
        if (ImGui::Button(ui::label(ui::icon::Photo, tr(app, "Import sheet…", "Importar hoja…")).c_str())) {
            app.importSource = app.selSource;
            app.importStyle = app.selStyle;
            app.importSheet = sheetIndex;
            openDialog(app, window, DialogAction::ImportSheet);
        }
        ui::tooltip(tr(app, "Use your own PNG + XML (Sparrow) or TXT (Packer) for this sheet. Nothing is written into the mod.",
                            "Usar tu propio PNG + XML (Sparrow) o TXT (Packer) para esta hoja. No se escribe nada en el mod."));
    }
}

void drawFindingRow(NoteLabApp& app, const Finding& f, const Source* source = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(severityColor(f.severity)));
    ImGui::TextUnformatted(ui::fonts().icons ? severityIcon(f.severity) : toString(f.severity));
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(f.severity == Severity::Info ? ui::color::Muted : ui::color::Text));
    const std::string text = describe(f, app.spanish);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
    if (ImGui::BeginPopupContextItem("findingmenu")) {
        if (menuItem(ui::icon::Copy, tr(app, "Copy the text", "Copiar el texto"))) copyText(app, text);
        if (menuItem(ui::icon::Copy, tr(app, "Copy the code", "Copiar el código"))) copyText(app, f.code);
        if (menuItem(ui::icon::Copy, tr(app, "Copy the path", "Copiar la ruta"), nullptr, false, !f.path.empty())) copyText(app, f.path);
        if (menuItem(ui::icon::FolderOpen, tr(app, "Show in folder", "Mostrar en la carpeta"), nullptr, false, source && !f.path.empty()))
            revealVirtual(app, *source, f.path);
        ImGui::EndPopup();
    }
}

void drawInspectorTop(NoteLabApp& app, SDL_Window* window) {
    const NoteStyle* style = selectedStyle(app);
    const StyleReport* report = selectedReport(app);
    Source* source = selectedSource(app);
    if (!style || !report || !source) {
        // Vacio: un icono y una frase, centrados.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const char* text = tr(app, "Pick a note style to see what the engine would draw and to edit it.",
                                   "Elige un estilo de notas para ver qué dibujaría el motor y editarlo.");
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float y = at.y + avail.y * 0.38f;
        if (ui::fonts().icons) {
            ImGui::PushFont(ui::fonts().ui, 34.0f);
            const ImVec2 glyph = ImGui::CalcTextSize(ui::icon::Layers);
            ImGui::PopFont();
            draw->AddText(ui::fonts().ui, 34.0f, ImVec2(at.x + (avail.x - glyph.x) * 0.5f, y - 50.0f), ui::color::Faint, ui::icon::Layers);
        }
        ImGui::SetCursorScreenPos(ImVec2(at.x + 20.0f, y));
        ImGui::PushTextWrapPos(at.x + avail.x - 20.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        return;
    }
    const bool edited = app.selStyle < static_cast<int>(source->edited.size()) && source->edited[static_cast<size_t>(app.selStyle)];
    const bool variant = isVariant(*style);
    if (variant) {
        // El nombre de una variante se cambia aqui mismo.
        if (app.nameFor != style->id) {
            app.nameFor = style->id;
            std::snprintf(app.nameBuffer.data(), app.nameBuffer.size(), "%s", style->name.c_str());
        }
        ImGui::PushFont(ui::fonts().semibold, 18.0f);
        ImGui::SetNextItemWidth(-1.0f);
        const bool entered = ImGui::InputText("##variantname", app.nameBuffer.data(), app.nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopFont();
        ui::tooltip(tr(app, "Name of the variant", "Nombre de la variante"));
        if ((entered || ImGui::IsItemDeactivatedAfterEdit()) && style->name != app.nameBuffer.data() && app.nameBuffer[0] != '\0') {
            const std::string name = app.nameBuffer.data();
            beginEdit(app);
            if (NoteStyle* current = mutableStyle(app)) current->name = name;
            afterEdit(app);
            style = selectedStyle(app);
            report = selectedReport(app);
            if (!style || !report) return;
        }
    } else {
        ImGui::PushFont(ui::fonts().semibold, 20.0f);
        ImGui::TextWrapped("%s", style->name.c_str());
        ImGui::PopFont();
    }
    ui::pill(engineLabel(style->engine), engineColor(style->engine));
    ImGui::SameLine(0.0f, 6.0f);
    std::string use = useLabel(app, style->use);
    if (!style->useDetail.empty() && (style->use == StyleUse::NoteType || style->use == StyleUse::PlayerChoice)) use += ": " + style->useDetail;
    ui::pill(use.c_str(), ui::color::Muted);
    if (variant) {
        ImGui::SameLine(0.0f, 6.0f);
        ui::pill(tr(app, "variant", "variante"), ui::color::Accent, true);
    } else if (edited) {
        ImGui::SameLine(0.0f, 6.0f);
        ui::pill(tr(app, "edited", "editado"), ui::color::Accent, true);
    }
    if (!style->referenced) {
        ImGui::SameLine(0.0f, 6.0f);
        ui::pill(tr(app, "not loaded", "no se carga"), ui::color::Faint);
    }
    ui::monoText(style->definition);
    if (style->use == StyleUse::Song && !style->useDetail.empty())
        ui::caption((std::string(tr(app, "Songs: ", "Canciones: ")) + style->useDetail).c_str());
    if (!style->fallback.empty()) ui::caption((std::string("Fallback: ") + style->fallback).c_str());
    ImGui::Spacing();
    const float cardWidth = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
    const bool complete = report->partsResolved == static_cast<int>(style->parts.size());
    std::string pieces = std::to_string(report->partsResolved) + " / " + std::to_string(style->parts.size());
    if (report->partsInherited) pieces += "  (" + std::to_string(report->partsInherited) + tr(app, " inherited)", " heredadas)");
    ui::statCard(tr(app, "Pieces that draw", "Piezas que se dibujan"), pieces, complete ? ui::color::Success : ui::color::Warning, cardWidth);
    ImGui::SameLine(0.0f, 8.0f);
    if (style->hasHud)
        ui::statCard("HUD", std::to_string(report->hudFound) + " / " + std::to_string(report->hudExpected),
                     report->hudFound == report->hudExpected ? ui::color::Success : ui::color::Warning, cardWidth);
    else
        ui::statCard("HUD", tr(app, "not its own", "no propio"), ui::color::Faint, cardWidth);
    ImGui::Spacing();
    ImGui::BeginDisabled(app.undo.empty());
    if (ImGui::Button(ui::label(ui::icon::Undo, tr(app, "Undo", "Deshacer")).c_str())) swapHistory(app, app.undo, app.redo);
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(app.redo.empty());
    if (ImGui::Button(ui::label(ui::icon::Redo, tr(app, "Redo", "Rehacer")).c_str())) swapHistory(app, app.redo, app.undo);
    ImGui::EndDisabled();
    sameLineOrWrap(buttonWidth(ui::label(ui::icon::Restart, tr(app, "Mod's version", "Versión del mod"))));
    ImGui::BeginDisabled(!source->originals.count(static_cast<size_t>(app.selStyle)));
    if (ImGui::Button(ui::label(ui::icon::Restart, tr(app, "Mod's version", "Versión del mod")).c_str())) revertStyle(app);
    ImGui::EndDisabled();
    ui::tooltip(tr(app, "Undo every change to this style", "Deshacer todos los cambios de este estilo"));
    const bool newVariant = ImGui::Button(ui::label(ui::icon::Copy, tr(app, "New variant", "Nueva variante")).c_str());
    tutorialMark("new-variant");
    if (newVariant) {
        createVariant(app);
        return;
    }
    ui::tooltip(tr(app, "A copy of this style to make your own notes or HUD, without touching the mod's. It is saved with the project.",
                        "Una copia de este estilo para hacer tus propias notas o tu HUD, sin tocar el del mod. Se guarda con el proyecto."));
    const std::string combineText = ui::label(ui::icon::Layers, tr(app, "Combine styles / HUD…", "Combinar estilos / HUD…"));
    sameLineOrWrap(buttonWidth(combineText));
    if (ImGui::Button(combineText.c_str())) openStyleComposer(app);
    ui::tooltip(tr(app, "Take selected groups from another style; keep the remaining files and undo history.",
        "Toma grupos de otro estilo; conserva los archivos restantes y el historial de deshacer."));
    const std::string createText = ui::label(ui::icon::Brush, tr(app, "Create HUD…", "Crear HUD…"));
    sameLineOrWrap(buttonWidth(createText));
    if (ImGui::Button(createText.c_str())) openCreateHud(app);
    ui::tooltip(tr(app, "Your own note HUD from this style: choose the colors of each direction, the receptors and the splashes, and see it moving.",
                        "Tu propio HUD de notas a partir de este estilo: elige los colores de cada dirección, los receptores y las salpicaduras, y míralo moverse."));
    if (variant) {
        const std::string deleteText = ui::label(ui::icon::Close, tr(app, "Delete variant", "Borrar variante"));
        sameLineOrWrap(buttonWidth(deleteText));
        if (ImGui::Button(deleteText.c_str())) {
            deleteVariant(app, app.selSource, app.selStyle);
            return;
        }
    }
    const std::string exportText = ui::label(ui::icon::Zip, tr(app, "Export…", "Exportar…"));
    sameLineOrWrap(buttonWidth(exportText));
    const bool exportStyle = ui::primaryButton(exportText);
    tutorialMark("export-style");
    if (exportStyle) openExport(app);
    ui::tooltip(tr(app, "Export this style to Codename, Psych or V-Slice, as a folder or ZIP, with its install guide (Ctrl+E).",
                        "Exportar este estilo a Codename, Psych o V-Slice, en carpeta o ZIP, con su guía de instalación (Ctrl+E)."));

    style = selectedStyle(app);
    report = selectedReport(app);
    if (!style || !report) return;
    ui::sectionHeader((std::string(tr(app, "Pieces", "Piezas")) + " · " + std::to_string(style->parts.size())).c_str(), ui::icon::Grid);
    ImGui::BeginGroup();
    drawPartsGrid(app, *source, *style, *report);
    ImGui::EndGroup();
    tutorialMark("parts-grid");
    if (style->hasHud) {
        ui::sectionHeader(tr(app, "HUD", "HUD"), ui::icon::Layers);
        drawHudGrid(app, *style);
        if (drawBorrowHudButton(app)) return;
        const std::string rankingText = ui::label(ui::icon::Star, tr(app, "Ranking from images…", "Ranking con imágenes…"));
        sameLineOrWrap(buttonWidth(rankingText));
        if (ImGui::SmallButton(rankingText.c_str())) openCustomCreator(app, true);
        if (kTextRankingVisible) {
            const std::string rankingFontText = ui::label(ui::icon::Font, tr(app, "Ranking with text…", "Ranking con texto…"));
            sameLineOrWrap(buttonWidth(rankingFontText));
            if (ImGui::SmallButton(rankingFontText.c_str())) openCreateRating(app);
            ui::tooltip(tr(app, "Judgements, combo and digits written with any font: no drawing needed.",
                                "Juicios, combo y cifras escritos con cualquier letra: sin dibujar nada."));
        }
    } else {
        ui::sectionHeader(tr(app, "HUD", "HUD"), ui::icon::Layers);
        ui::caption(tr(app, "This style has no HUD of its own: the engine uses the default one.",
                            "Este estilo no tiene HUD propio: el motor usa el de por defecto."));
        if (ImGui::SmallButton(ui::label(ui::icon::Add, tr(app, "Give it its own HUD", "Darle un HUD propio")).c_str())) {
            beginEdit(app);
            if (NoteStyle* current = mutableStyle(app)) current->hasHud = true;
            afterEdit(app);
            return;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (drawBorrowHudButton(app)) return;
        if (kTextRankingVisible) {
            const std::string rankingFontText = ui::label(ui::icon::Font, tr(app, "Ranking with text…", "Ranking con texto…"));
            sameLineOrWrap(buttonWidth(rankingFontText));
            if (ImGui::SmallButton(rankingFontText.c_str())) openCreateRating(app);
            ui::tooltip(tr(app, "Its own ranking HUD with any font; the style gets its own HUD.",
                                "Su propio HUD de ranking con cualquier letra; el estilo pasa a tener HUD propio."));
        }
    }
    style = selectedStyle(app);
    report = selectedReport(app);
    if (!style || !report) return;
    ui::sectionHeader((std::string(tr(app, "Findings", "Hallazgos")) + " · " + std::to_string(report->findings.size())).c_str(), ui::icon::Warning);
    if (report->findings.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Success));
        ImGui::TextUnformatted(ui::label(ui::icon::Check, tr(app, "Everything the engine needs is here.", "Está todo lo que necesita el motor.")).c_str());
        ImGui::PopStyleColor();
    }
    for (size_t k = 0; k < report->findings.size(); ++k) {
        ImGui::PushID(static_cast<int>(k));
        drawFindingRow(app, report->findings[k], source);
        ImGui::PopID();
    }
}

// El inspector en dos zonas: arriba el estilo con sus piezas, su HUD y sus
// hallazgos; abajo, siempre a la vista, lo que se esta editando.
void drawInspector(NoteLabApp& app, SDL_Window* window) {
    if (!selectedStyle(app) || !selectedReport(app) || !selectedSource(app)) {
        drawInspectorTop(app, window);
        return;
    }
    const float total = ImGui::GetContentRegionAvail().y;
    const float editorHeight = std::clamp(total * 0.44f, 200.0f, std::max(200.0f, total - 180.0f));
    ImGui::BeginChild("inspectortop", ImVec2(0.0f, std::max(120.0f, total - editorHeight - 8.0f)));
    drawInspectorTop(app, window);
    ImGui::EndChild();
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + 3.0f), ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + 3.0f),
                                            ui::color::Border, 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }
    ImGui::BeginChild("inspectoredit", ImVec2(0.0f, 0.0f));
    if (app.editHud >= 0) drawHudEditor(app, window);
    else drawPieceEditor(app, window);
    ImGui::EndChild();
    if (app.editHud < 0 && app.editPart >= 0) tutorialMark("piece-edit");
}

void drawFindingsWindow(NoteLabApp& app) {
    if (!app.showFindings) return;
    ImGui::SetNextWindowSize(ImVec2(780.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::vec(ui::color::Panel));
    const bool open = ImGui::Begin((ui::label(ui::icon::Warning, tr(app, "Findings", "Hallazgos")) + "###findings").c_str(), &app.showFindings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }
    const char* levels[] = {tr(app, "Everything", "Todo"), tr(app, "Warnings and errors", "Avisos y errores"), tr(app, "Errors", "Errores")};
    ui::segmented("level", &app.findingsSeverity, levels, 3);
    ImGui::Spacing();
    auto shown = [&](Severity severity) {
        const int level = severity == Severity::Error ? 2 : severity == Severity::Warning ? 1 : 0;
        return level >= app.findingsSeverity;
    };
    ImGui::BeginChild("findinglist");
    int total = 0;
    for (size_t s = 0; s < app.sources.size(); ++s) {
        const Source& source = *app.sources[s];
        ui::sectionHeader(sourceName(source).c_str(), ui::icon::Layers);
        for (size_t k = 0; k < source.catalog.findings.size(); ++k) {
            const Finding& f = source.catalog.findings[k];
            if (!shown(f.severity)) continue;
            ImGui::PushID(static_cast<int>(s * 1000000 + k));
            drawFindingRow(app, f, &source);
            ImGui::PopID();
            ++total;
        }
        for (size_t i = 0; i < source.reports.size(); ++i) {
            const NoteStyle& style = source.catalog.styles[i];
            if (!styleVisible(app, source, style)) continue;
            for (size_t k = 0; k < source.reports[i].findings.size(); ++k) {
                const Finding& f = source.reports[i].findings[k];
                if (!shown(f.severity)) continue;
                ++total;
                ImGui::PushID(static_cast<int>(s * 100000 + i * 100 + k));
                if (ImGui::SmallButton(style.name.c_str())) selectStyle(app, static_cast<int>(s), static_cast<int>(i));
                ImGui::SameLine();
                drawFindingRow(app, f, &source);
                ImGui::PopID();
            }
        }
    }
    if (total == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Success));
        ImGui::TextUnformatted(ui::label(ui::icon::Check, tr(app, "Nothing at this level.", "Nada en este nivel.")).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::End();
}

void drawShortcutsModal(NoteLabApp& app) {
    if (app.openShortcuts) {
        ImGui::OpenPopup("###shortcuts");
        app.openShortcuts = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Keyboard, tr(app, "Keyboard shortcuts", "Atajos de teclado")) + "###shortcuts").c_str(), &open))
        return;
    struct Row { const char* keys; const char* en; const char* es; };
    const Row rows[] = {
        {"Ctrl+N", "New project", "Proyecto nuevo"},
        {"Ctrl+Shift+O", "Open a project (.fmlnote)", "Abrir un proyecto (.fmlnote)"},
        {"Ctrl+S / Ctrl+Shift+S", "Save the project / save as", "Guardar el proyecto / guardar como"},
        {"Ctrl+E", "Export the chosen note style", "Exportar el estilo de notas elegido"},
        {"Ctrl+O", "Open a mod folder", "Abrir la carpeta de un mod"},
        {"F5", "Reload everything", "Recargar todo"},
        {"Ctrl+Z / Ctrl+Y", "Undo / redo an edit", "Deshacer / rehacer una edición"},
        {"Space", "Play / pause the preview", "Reproducir / pausar la vista previa"},
        {"R", "Restart the preview", "Reiniciar la vista previa"},
        {"D F J K", "Play the chosen side (Keys… changes them)", "Jugar el lado elegido (Teclas… las cambia)"},
        {tr(app, "Mouse wheel", "Rueda del ratón"), "Zoom the asset viewer", "Acercar el visor de assets"},
        {"F1", "This list", "Esta lista"},
    };
    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        for (const Row& row : rows) {
            ImGui::TableNextRow(0, ImGui::GetTextLineHeight() + 10.0f);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(ui::fonts().mono, 14.0f);
            ImGui::TextColored(ui::vec(ui::color::Accent), "%s", row.keys);
            ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr(app, row.en, row.es));
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ui::primaryButton(tr(app, "Close", "Cerrar"), ImVec2(120.0f, 0.0f)) || escapeClosesWindow())
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawAboutModal(NoteLabApp& app) {
    if (app.openAbout) {
        ImGui::OpenPopup("###about");
        app.openAbout = false;
    }
    ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((std::string(tr(app, "About", "Acerca de")) + "###about").c_str(), &open)) return;
    ui::title("Note Lab", 24.0f);
    ui::caption(tr(app, "1.0.0 · FML Tool · MIT license", "1.0.0 · FML Tool · Licencia MIT"));
    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr(app, "Opens Codename, Psych and V-Slice mods, reads their note styles, custom notes and charts the way each "
                                     "engine does (every rule cites the engine source), plays them in a preview and lets you edit their "
                                     "pieces. Mod assets are read-only; replacing a chart requires explicit approval and a backup.",
                                "Abre mods de Codename, Psych y V-Slice, lee sus estilos de notas, sus notas custom y sus charts como lo hace "
                                "cada motor (cada regla cita la fuente del motor), los reproduce en una vista previa y deja editar sus "
                                "piezas. Los assets del mod son de solo lectura; reemplazar un chart exige confirmación y backup."));
    ImGui::Spacing();
    ui::pill("Codename", ui::color::Codename);
    ImGui::SameLine(0.0f, 8.0f);
    ui::pill("Psych", ui::color::Psych);
    ImGui::SameLine(0.0f, 8.0f);
    ui::pill("V-Slice", ui::color::VSlice);
    ImGui::Spacing();
    if (ui::primaryButton(tr(app, "Close", "Cerrar"), ImVec2(120.0f, 0.0f)) || escapeClosesWindow())
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawSourceCloseModal(NoteLabApp& app, SDL_Window* window) {
    if (app.openSourceClose) { ImGui::OpenPopup("###sourceclose"); app.openSourceClose = false; }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(540.0f, viewport->WorkSize.x - 24.0f), 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((std::string(tr(app, "Close source?", "¿Cerrar fuente?")) + "###sourceclose").c_str(), &open)) return;
    const int index = app.sourceToClose;
    if (index < 0 || index >= static_cast<int>(app.sources.size())) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    ImGui::TextWrapped("%s", (sourceName(*app.sources[static_cast<size_t>(index)]) + tr(app, " has project work. Save it before closing this source.", " tiene trabajo del proyecto. Guárdalo antes de cerrar esta fuente.")).c_str());
    if (ui::primaryButton(tr(app, "Save project first", "Guardar proyecto primero"))) {
        saveProject(app, window);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(tr(app, "Close and discard", "Cerrar y descartar"))) { removeSource(app, index); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawUnsavedModal(NoteLabApp& app, SDL_Window* window) {
    if (app.openUnsaved) {
        ImGui::OpenPopup("###unsaved");
        app.openUnsaved = false;
    }
    ImGui::SetNextWindowSize(ImVec2(470.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Warning, tr(app, "Unsaved changes", "Cambios sin guardar")) + "###unsaved").c_str(), nullptr))
        return;
    const std::string name = app.projectPath.empty() ? std::string(tr(app, "Untitled", "Sin título")) : app.projectPath.filename().u8string();
    ImGui::TextWrapped("%s", (std::string(tr(app, "The project ", "El proyecto «")) + name +
                              tr(app, " has unsaved changes. If you do not save them, they are lost.",
                                      "» tiene cambios sin guardar. Si no los guardas, se pierden.")).c_str());
    ImGui::Spacing();
    if (ui::primaryButton(ui::label(ui::icon::Check, tr(app, "Save", "Guardar")), ImVec2(130.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        if (!app.projectPath.empty()) {
            if (saveProjectTo(app, app.projectPath)) performPending(app, window);
            else app.pending = PendingAction::None;
        } else {
            app.saveThenContinue = true;
            openDialog(app, window, DialogAction::SaveProject);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Don't save", "No guardar"), ImVec2(130.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        performPending(app, window);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow()) {
        ImGui::CloseCurrentPopup();
        app.pending = PendingAction::None;
    }
    ImGui::EndPopup();
}

void drawIssuesModal(NoteLabApp& app) {
    if (app.openIssues) {
        ImGui::OpenPopup("###issues");
        app.openIssues = false;
    }
    ImGui::SetNextWindowSize(ImVec2(600.0f, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Warning, tr(app, "What changed", "Lo que cambió")) + "###issues").c_str(), &open)) return;
    ui::caption(tr(app, "This changed or is missing since it was saved. Everything else was opened; nothing was written into the mod.",
                        "Esto cambió o falta desde que se guardó. Lo demás se abrió; no se escribió nada en el mod."));
    ImGui::Spacing();
    for (const Issue& issue : app.issues) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Warning));
        ImGui::TextUnformatted(ui::fonts().icons ? ui::icon::Warning : "!");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextWrapped("%s", app.spanish ? issue.es.c_str() : issue.en.c_str());
    }
    ImGui::Spacing();
    if (ui::primaryButton(tr(app, "Got it", "Entendido"), ImVec2(120.0f, 0.0f)) || escapeClosesWindow())
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- exportar --

// Los usos de un motor, en el orden del segmentado.
std::vector<ExportRole> rolesFor(Engine target) {
    std::vector<ExportRole> out;
    for (ExportRole role : {ExportRole::ModSkin, ExportRole::Selectable, ExportRole::SongSkin, ExportRole::NoteType})
        if (exportRoleAvailable(target, role)) out.push_back(role);
    return out;
}

const char* roleLabel(const NoteLabApp& app, ExportRole role) {
    switch (role) {
        case ExportRole::ModSkin: return tr(app, "Mod skin", "Skin del mod");
        case ExportRole::Selectable: return tr(app, "In Options", "En Opciones");
        case ExportRole::SongSkin: return tr(app, "A song", "Una canción");
        case ExportRole::NoteType: return tr(app, "Note type", "Tipo de nota");
    }
    return "?";
}

const char* roleHelp(const NoteLabApp& app, Engine target, ExportRole role) {
    switch (role) {
        case ExportRole::ModSkin:
            return target == Engine::Codename
                ? tr(app, "Replaces game/notes/default: the notes, receptors, splashes and HUD of the whole mod.",
                          "Sustituye game/notes/default: notas, receptores, salpicaduras y HUD de todo el mod.")
                : tr(app, "Replaces noteSkins/NOTE_assets while the mod is loaded.",
                          "Sustituye noteSkins/NOTE_assets mientras el mod esté cargado.");
        case ExportRole::Selectable:
            return tr(app, "One more skin in Options > Visuals > Note Skins, for any song (list.txt in a global mod).",
                           "Un skin más en Opciones > Visuales > Note Skins, para cualquier canción (list.txt en un mod global).");
        case ExportRole::SongSkin:
            return target == Engine::VSlice
                ? tr(app, "A new note style; each song picks it in its metadata (playData.noteStyle).",
                          "Un notestyle nuevo; cada canción lo elige en su metadata (playData.noteStyle).")
                : tr(app, "The chart asks for it with arrowSkin and splashSkin.", "Lo pide el chart con arrowSkin y splashSkin.");
        case ExportRole::NoteType:
            if (target == Engine::Codename)
                return tr(app, "The look of the notes of one type (game/notes/<type>); receptors don't change.",
                               "El aspecto de las notas de un tipo (game/notes/<tipo>); los receptores no cambian.");
            if (target == Engine::Psych)
                return tr(app, "The look of the notes of one type (custom_notetypes/<type>.txt), without Psych's RGB palette.",
                               "El aspecto de las notas de un tipo (custom_notetypes/<tipo>.txt), sin la paleta RGB de Psych.");
            return tr(app, "A NoteKind that gives its notes this note style (scripts/notekinds).",
                           "Un NoteKind que da este notestyle a sus notas (scripts/notekinds).");
    }
    return "";
}

fs::path defaultExportFolder() {
    fs::path home = environmentPath("USERPROFILE");
    if (home.empty()) return settingsFolder() / "exports";
    return home / "Documents" / "Note Lab";
}

const NoteStyle* exportStyle(const NoteLabApp& app) {
    const NoteLabApp::Exporting& e = app.exporting;
    if (e.source < 0 || e.source >= static_cast<int>(app.sources.size())) return nullptr;
    const Source& source = *app.sources[static_cast<size_t>(e.source)];
    if (e.style < 0 || e.style >= static_cast<int>(source.catalog.styles.size())) return nullptr;
    return &source.catalog.styles[static_cast<size_t>(e.style)];
}

// Lo que el dialogo va a crear: la carpeta del paquete o su ZIP.
fs::path exportTarget(const NoteLabApp& app) {
    const NoteLabApp::Exporting& e = app.exporting;
    const std::string folder = e.package.folder.empty() ? std::string("notelab") : e.package.folder;
    return e.folder / pathFromUtf8(folder + (e.zip ? ".zip" : ""));
}

// Nunca se escribe dentro de un mod abierto, ni encima de uno.
bool insideOpenMod(const NoteLabApp& app, const fs::path& target) {
    std::error_code ec;
    const fs::path wanted = fs::weakly_canonical(target, ec);
    auto within = [](const fs::path& inner, const fs::path& outer) {
        auto a = inner.begin();
        for (auto b = outer.begin(); b != outer.end(); ++a, ++b)
            if (a == inner.end() || lowerText(a->u8string()) != lowerText(b->u8string())) return false;
        return true;
    };
    for (const auto& source : app.sources) {
        const fs::path root = fs::weakly_canonical(source->root, ec);
        if (within(wanted, root) || within(root, wanted)) return true;
    }
    return false;
}

// ----------------------------------------- script a bloques y sus archivos --

// Pasar el script de un tipo a bloques (pedido del autor, 5 oct 2026: «pasarla
// de codigo texto a bloques lo que se pueda»). El script es el del mod o el de
// una nota «solo codigo». Nunca se borra trabajo: si el tipo ya tiene bloques,
// lo importado va a una nota nueva «<tipo> (bloques)».
void importTypeScript(NoteLabApp& app, int sourceIndex, const std::string& type) {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size()) || type.empty()) return;
    Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    Engine engine;
    if (!sourceEngine(app, source, engine)) engine = Engine::Codename;
    std::string text, from;
    const auto existing = source.typeBlocks.find(type);
    if (existing != source.typeBlocks.end())
        for (const auto& [id, node] : existing->second.nodes)
            if (node.key == "code.file" && node.args.size() == 3 && node.args[0].value == engineKey(engine)) {
                text = node.args[2].value;
                from = node.args[1].value;
            }
    if (text.empty())
        if (const NoteTypeEntry* entry = findNoteType(source.noteTypes, type, engine))
            if (!entry->script.empty())
                if (const auto read = source.vfs->readText(entry->script)) {
                    text = *read;
                    from = entry->script;
                }
    if (text.empty()) {
        setStatus(app, "This note has no script to turn into blocks.", "Esta nota no tiene script que pasar a bloques.");
        return;
    }
    ScriptImport imported = importNoteScript(engine, type, text);
    if (imported.blocks + imported.kept == 0) {
        setStatus(app, "Nothing in that script hangs from a note event (goodNoteHit, onPlayerHit, onNoteHit…): it stays as it is.",
                       "Nada de ese script cuelga de un evento de nota (goodNoteHit, onPlayerHit, onNoteHit…): se queda como está.");
        return;
    }
    std::string target = type;
    if (existing != source.typeBlocks.end() && !existing->second.empty()) {
        target = type + (app.spanish ? " (bloques)" : " (blocks)");
        for (int n = 2; newNoteNameTaken(source, target) || source.typeBlocks.count(target); ++n)
            target = type + (app.spanish ? " (bloques " : " (blocks ") + std::to_string(n) + ")";
    }
    imported.program.comment = std::string(app.spanish ? "Pasado a bloques desde " : "Turned into blocks from ") + from;
    source.typeBlocks[target] = std::move(imported.program);
    app.blocksSource = sourceIndex;
    app.blocksType = target;
    app.blocksView = 0;
    app.typesView = 1;
    app.requestedTab = 1;
    blocksChanged(app);
    auto& view = app.scriptImport;
    view.requestOpen = true;
    view.source = sourceIndex;
    view.type = target;
    view.from = from;
    view.blocks = imported.blocks;
    view.kept = imported.kept;
    view.notes = std::move(imported.notes);
    tutorialSignal("blocks-import");
}

// Lo que salio al pasar a bloques: cuanto es bloque, cuanto codigo y los avisos.
void drawScriptImportModal(NoteLabApp& app) {
    auto& view = app.scriptImport;
    if (view.requestOpen) {
        ImGui::OpenPopup("###scriptimport");
        view.requestOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Puzzle, tr(app, "Script turned into blocks", "Script pasado a bloques")) + "###scriptimport").c_str(),
                                nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("%s", (std::string(tr(app, "From ", "De ")) + view.from + "  →  " + view.type).c_str());
    ImGui::Spacing();
    ImGui::TextColored(ui::vec(ui::color::Success), "%s %d %s", ui::icon::Check, view.blocks,
                       view.blocks == 1 ? tr(app, "statement became a block", "sentencia se hizo bloque")
                                        : tr(app, "statements became blocks", "sentencias se hicieron bloques"));
    ImGui::TextColored(ui::vec(view.kept ? ui::color::Warning : ui::color::Muted), "%s %d %s", ui::icon::Code, view.kept,
                       view.kept == 1 ? tr(app, "line stays as a «code» block, as it was (only in this engine)",
                                           "línea se queda como bloque «código», tal cual (solo en este motor)")
                                      : tr(app, "lines stay as «code» blocks, as they were (only in this engine)",
                                           "líneas se quedan como bloques «código», tal cual (solo en este motor)"));
    for (const auto& note : view.notes) ImGui::TextWrapped("%s %s", ui::icon::Info, app.spanish ? note.second.c_str() : note.first.c_str());
    ImGui::Spacing();
    ui::caption(tr(app, "The mod's script is not touched: the blocks live in the project until you export them.",
                        "El script del mod no se toca: los bloques viven en el proyecto hasta que los exportas."));
    if (ui::primaryButton(tr(app, "See the blocks", "Ver los bloques"), ImVec2(160.0f, 0.0f)) || escapeClosesWindow()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

const BlockProgram* programFor(const NoteLabApp& app) {
    if (app.programSource < 0 || app.programSource >= static_cast<int>(app.sources.size())) return nullptr;
    const Source& source = *app.sources[static_cast<size_t>(app.programSource)];
    const auto found = source.typeBlocks.find(app.programType);
    return found == source.typeBlocks.end() ? nullptr : &found->second;
}

// El programa de bloques del tipo en un archivo .nlblocks (para guardarlo o
// llevarlo a otro proyecto); nunca dentro de un mod abierto.
void saveProgramTo(NoteLabApp& app, const fs::path& chosen) {
    const BlockProgram* program = programFor(app);
    if (!program) return;
    fs::path path = chosen;
    if (lowerText(path.extension().u8string()) != ".nlblocks") path += ".nlblocks";
    if (insideOpenMod(app, path)) {
        setStatus(app, "That folder is inside an open mod: Note Lab never writes there.", "Esa carpeta está dentro de un mod abierto: Note Lab nunca escribe ahí.");
        return;
    }
    std::ofstream out(path, std::ios::binary);
    const std::string text = writeProgram(*program);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        setStatus(app, "The program could not be written there.", "No se pudo escribir el programa ahí.");
        return;
    }
    setStatus(app, "Block program saved: " + path.filename().u8string() + ".", "Programa de bloques guardado: " + path.filename().u8string() + ".");
}

// Abrir un .nlblocks: en un tipo vacio, como su programa; si ya tiene bloques,
// como pilas nuevas al lado (no se borra nada).
void openProgramFrom(NoteLabApp& app, const fs::path& path) {
    if (app.programSource < 0 || app.programSource >= static_cast<int>(app.sources.size()) || app.programType.empty()) return;
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const BlockProgram loaded = readProgram(text);
    if (loaded.empty()) {
        setStatus(app, "That file has no block program.", "Ese archivo no tiene un programa de bloques.");
        return;
    }
    BlockProgram& program = app.sources[static_cast<size_t>(app.programSource)]->typeBlocks[app.programType];
    if (program.empty()) {
        program = loaded;
    } else {
        float right = 0.0f;
        for (const auto& [id, node] : program.nodes) right = std::max(right, node.x);
        for (int top : loaded.tops) {
            const int copy = copyStackFrom(program, loaded, top);
            if (BlockNode* node = program.node(copy)) {
                node->x += right + 360.0f;
            }
        }
    }
    blocksChanged(app);
    setStatus(app, "Block program opened in " + app.programType + ".", "Programa de bloques abierto en " + app.programType + ".");
}

// El codigo de sus bloques para el motor del mod, en una carpeta (con las rutas
// que pide el motor: custom_notetypes/, data/notes/, scripts/notekinds/).
void saveCodeTo(NoteLabApp& app, const fs::path& folder) {
    const BlockProgram* program = programFor(app);
    if (!program) return;
    if (insideOpenMod(app, folder)) {
        setStatus(app, "That folder is inside an open mod: Note Lab never writes there.", "Esa carpeta está dentro de un mod abierto: Note Lab nunca escribe ahí.");
        return;
    }
    const Source& source = *app.sources[static_cast<size_t>(app.programSource)];
    Engine engine;
    if (!sourceEngine(app, source, engine)) engine = Engine::Codename;
    std::string file = app.programType;
    for (char& c : file)
        if (std::strchr("<>:\"/\\|?*", c)) c = '_';
    const BlockCode code = generateBlocks(engine, app.programType, engine == Engine::VSlice ? exportName(app.programType) : file, *program);
    int written = 0;
    for (const BlockFile& one : code.files) {
        const fs::path target = folder / pathFromUtf8(one.path);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary);
        out.write(one.text.data(), static_cast<std::streamsize>(one.text.size()));
        if (out) ++written;
    }
    setStatus(app, std::to_string(written) + " code file(s) saved in " + folder.filename().u8string() + ", with the paths the engine expects.",
              std::to_string(written) + " archivo(s) de código guardados en " + folder.filename().u8string() + ", con las rutas que pide el motor.");
}

// El programa de bloques del tipo que se exporta, si lo es (Source::typeBlocks).
const BlockProgram* exportBlocks(const NoteLabApp& app) {
    const NoteLabApp::Exporting& e = app.exporting;
    if (e.options.role != ExportRole::NoteType || e.source < 0 || e.source >= static_cast<int>(app.sources.size())) return nullptr;
    const Source& source = *app.sources[static_cast<size_t>(e.source)];
    const auto found = source.typeBlocks.find(e.noteType.data());
    return found == source.typeBlocks.end() ? nullptr : &found->second;
}

std::string exportKey(const NoteLabApp& app) {
    const NoteLabApp::Exporting& e = app.exporting;
    const ExportOptions& o = e.options;
    std::string key = std::to_string(e.source) + "|" + std::to_string(e.style) + "|" + engineKey(o.target) + "|" + exportRoleKey(o.role) + "|" +
                      e.name.data() + "|" + e.noteType.data() + "|" + (o.notes ? "n" : "") + (o.strums ? "s" : "") + (o.splashes ? "p" : "") +
                      (o.holdCovers ? "c" : "") + (o.hud ? "h" : "");
    if (const BlockProgram* program = exportBlocks(app)) key += "|" + writeProgram(*program);
    return key;
}

// Arma el paquete en seco (sin escribir nada) con lo elegido.
void prepareExport(NoteLabApp& app) {
    NoteLabApp::Exporting& e = app.exporting;
    const std::string key = exportKey(app);
    if (key == e.preparedKey) return;
    const NoteStyle* style = exportStyle(app);
    if (!style) return;
    Source& source = *app.sources[static_cast<size_t>(e.source)];
    e.options.name = e.name.data();
    e.options.title = e.name.data();
    e.options.noteType = e.noteType.data();
    // Un tipo de nota lleva sus bloques, y se avisa de los archivos que el mod
    // ya tiene para ese tipo en el motor de destino.
    e.options.blocks = BlockProgram{};
    e.options.existingFiles.clear();
    if (const BlockProgram* program = exportBlocks(app)) e.options.blocks = *program;
    if (e.options.role == ExportRole::NoteType)
        for (const NoteTypeEntry& type : source.noteTypes)
            if (type.engine == e.options.target && type.name == e.options.noteType) {
                if (!type.script.empty()) e.options.existingFiles.push_back(type.script);
                if (!type.config.empty()) e.options.existingFiles.push_back(type.config);
            }
    ExportIo io;
    io.readBytes = [&source](const std::string& path) { return source.vfs->readBytes(path, 256u * 1024u * 1024u); };
    io.readText = [&source](const std::string& path) { return source.vfs->readText(path); };
    e.package = buildExport(*style, e.options, io);
    e.preparedKey = key;
}

void openExport(NoteLabApp& app) {
    const NoteStyle* style = selectedStyle(app);
    if (!style) {
        setStatus(app, "Pick a note style to export.", "Elige un estilo de notas para exportarlo.");
        return;
    }
    NoteLabApp::Exporting& e = app.exporting;
    if (e.source != app.selSource || e.style != app.selStyle) {
        e.source = app.selSource;
        e.style = app.selStyle;
        ExportOptions o;
        o.target = style->engine;
        // Para lo que ya sirve en su motor.
        if (style->use == StyleUse::NoteType) {
            o.role = ExportRole::NoteType;
            o.noteType = style->useDetail;
        } else if (style->use == StyleUse::PlayerChoice) {
            o.role = ExportRole::Selectable;
        } else if (style->use == StyleUse::Song || style->engine == Engine::VSlice) {
            o.role = ExportRole::SongSkin;
        }
        if (!exportRoleAvailable(o.target, o.role)) o.role = rolesFor(o.target).front();
        o.hud = style->hasHud;
        e.options = o;
        std::string name = o.role == ExportRole::NoteType ? o.noteType : style->name;
        const std::string low = lowerText(name);
        if (low.empty() || low == "default" || low.rfind("note_assets", 0) == 0)
            name = app.sources[static_cast<size_t>(app.selSource)]->root.stem().u8string();
        name = exportName(name);
        std::snprintf(e.name.data(), e.name.size(), "%s", name.c_str());
        std::snprintf(e.noteType.data(), e.noteType.size(), "%s", o.noteType.c_str());
        e.written.clear();
        e.error.clear();
    }
    if (e.folder.empty()) e.folder = app.exportFolder.empty() ? defaultExportFolder() : app.exportFolder;
    e.preparedKey.clear();
    e.changedAt = -1.0;
    e.open = true;
}

// Relee el paquete con los lectores de Note Lab, le pone las guias y lo escribe.
bool runExport(NoteLabApp& app) {
    NoteLabApp::Exporting& e = app.exporting;
    e.preparedKey.clear();
    prepareExport(app);
    e.error.clear();
    e.written.clear();
    const bool sound = nlbuild::blockAudioPlayback && !app.headless && app.uiSounds;
    if (e.package.hasErrors()) {
        e.error = tr(app, "The package has errors (see the list): it is not written.",
                          "El paquete tiene errores (mira la lista): no se escribe.");
        nlblocks::playCue(nlblocks::Cue::Error, sound);
        return false;
    }
    const fs::path target = exportTarget(app);
    if (insideOpenMod(app, target)) {
        e.error = tr(app, "Note Lab never writes into an open mod: pick another folder.",
                          "Note Lab nunca escribe dentro de un mod abierto: elige otra carpeta.");
        nlblocks::playCue(nlblocks::Cue::Error, sound);
        return false;
    }
    ExportPackage package = e.package;
    std::error_code ec;
    const fs::path scratch = fs::temp_directory_path(ec);
    if (ec || scratch.empty() || !verifyExport(package, scratch)) {
        e.error = tr(app, "Export verification failed: nothing was written. Check the source assets and destination permissions.",
                          "Falló la verificación del export: no se escribió nada. Revisa los recursos de origen y los permisos del destino.");
        e.writtenVerified = false;
        nlblocks::playCue(nlblocks::Cue::Error, sound);
        return false;
    }
    addInstallGuides(package);
    fs::create_directories(e.folder, ec);
    std::string error;
    const bool ok = e.zip ? writeExportZip(package, target, error, app.spanish)
                          : writeExportFolder(package, target, error, app.spanish);
    if (!ok) {
        e.error = error;
        nlblocks::playCue(nlblocks::Cue::Error, sound);
        return false;
    }
    nlblocks::playCue(nlblocks::Cue::Success, sound);
    e.written = target;
    e.writtenVerified = package.verified;
    app.exportFolder = e.folder;
    // Una captura de prueba no cambia las preferencias de quien usa la app.
    if (!app.headless) saveSettings(app);
    setStatus(app, "Exported to " + target.u8string(), "Exportado a " + target.u8string());
    std::printf("exported %s, files %zu, frames %d, verified %d, errors %d\n", target.u8string().c_str(), package.files.size(),
                package.frames, package.verified ? 1 : 0, package.verifyErrors);
    return true;
}

std::string sizeText(size_t bytes) {
    char text[32];
    if (bytes >= 1024u * 1024u) std::snprintf(text, sizeof(text), "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024u) std::snprintf(text, sizeof(text), "%.0f KB", bytes / 1024.0);
    else std::snprintf(text, sizeof(text), "%zu B", bytes);
    return text;
}

const char* fileGlyph(const std::string& path) {
    const std::string low = lowerText(path);
    if (endsWithText(low, ".png")) return ui::icon::Photo;
    if (endsWithText(low, ".ogg")) return ui::icon::Music;
    if (endsWithText(low, ".hx") || endsWithText(low, ".hxc")) return ui::icon::Code;
    return ui::icon::Document;
}

// Una nota del paquete con su icono y su color.
void exportNoteRow(const NoteLabApp& app, const ExportNote& n) {
    const bool error = n.severity == Severity::Error;
    const bool warning = n.severity == Severity::Warning;
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(error ? ui::color::Error : warning ? ui::color::Warning : ui::color::Info));
    ImGui::TextUnformatted(ui::fonts().icons ? (error ? ui::icon::Error : warning ? ui::icon::Warning : ui::icon::Info)
                                             : (error ? "x" : warning ? "!" : "i"));
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextWrapped("%s", describeExportNote(n, app.spanish).c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", n.code.c_str());
}

// --------------------------------------------------------------- ayuda (F1) --

// Los temas de la ayuda: un parrafo por linea; «- » es un punto de lista y
// «# » un subtitulo. Ingles y espanol van juntos.
struct HelpTopic {
    const char* en;
    const char* es;
    std::vector<std::pair<const char*, const char*>> lines;
};

const std::vector<HelpTopic>& helpTopics() {
    static const std::vector<HelpTopic> topics = {
        {"Getting started", "Empezar", {
            {"Note Lab opens mods of Codename, Psych and V-Slice: a folder, a ZIP or a whole install. Drop it on the window or use Open mod (Ctrl+O). The engine is detected by itself.",
             "Note Lab abre mods de Codename, Psych y V-Slice: una carpeta, un ZIP o una instalación entera. Suéltalo en la ventana o usa Abrir mod (Ctrl+O). El motor se detecta solo."},
            {"A mod's build (with its own .exe) opens by its mod, with the install underneath, like the engine loads it. Right click on a mod's card to open another mod of the same install or the install alone.",
             "La build de un mod (con su propio .exe) se abre por su mod, con la instalación debajo, como la carga el motor. Clic derecho en la tarjeta del mod para abrir otro mod de la misma instalación o la instalación sola."},
            {"If a mod needs the base game of its engine (its default skin, splashes or HUD), Note Lab looks for it next to the mod. You can choose it with Base game.",
             "Si un mod necesita el juego base de su motor (su skin, sus salpicaduras o su HUD), Note Lab lo busca junto al mod. Puedes elegirlo con Juego base."},
            {"Nothing is ever written inside a mod: what you edit, create or import lives in the project (.fmlnote) and in Note Lab's folder until you export it.",
             "Nunca se escribe nada dentro de un mod: lo que editas, creas o importas vive en el proyecto (.fmlnote) y en la carpeta de Note Lab hasta que lo exportas."},
        }},
        {"Create your note HUD", "Crear tu HUD de notas", {
            {"File > Create note HUD…, or Create HUD… in the inspector.", "Archivo > Crear HUD de notas…, o Crear HUD… en el inspector."},
            {"- Starting point: any style of the mod with notes. Its shapes stay; you choose the colors.",
             "- Punto de partida: cualquier estilo del mod con notas. Sus formas se quedan; tú eliges los colores."},
            {"- Colors: one per direction, or a preset (Classic, Pastel, Neon, Mono, Inverted, Fire and ice, Random). An own outline color is optional.",
             "- Colores: uno por dirección, o un preset (Clásico, Pastel, Neón, Monocromo, Invertido, Fuego y hielo, Al azar). El contorno de otro color es opcional."},
            {"- Details: gray, colored or see-through receptors; the hit glow with its color or white; lighter holds; painted splashes.",
             "- Detalles: receptores grises, de su color o transparentes; el brillo al acertar con su color o blanco; sostenidos más claros; salpicaduras pintadas."},
            {"- Note shape: the style's, or Drawn by me in the sprite editor (layers, brushes, shapes, fill…). You draw the left one; the others are rotated, tinted with each direction's color, and the receptors can take the same shape.",
             "- Forma de las notas: la del estilo, o Dibujada por mí en el editor de sprites (capas, pinceles, figuras, relleno…). Dibujas la de la izquierda; las demás salen giradas y teñidas con el color de cada dirección, y los receptores pueden tener la misma forma."},
            {"The preview on the right moves like in the game. Create HUD adds a new style to the project, already selected: edit its pieces, play with it and export it to any of the three engines.",
             "La vista de la derecha se mueve como en el juego. Crear HUD añade un estilo nuevo al proyecto, ya elegido: edita sus piezas, juega con él y expórtalo a cualquiera de los tres motores."},
            {"The colors are painted into the images, so the three engines show the same. A Psych 0.7+ RGB template is baked first with its default colors.",
             "Los colores se pintan en las imágenes, así que los tres motores enseñan lo mismo. Una plantilla RGB de Psych 0.7+ se hornea antes con sus colores por defecto."},
        }},
        {"Look of a custom note", "Aspecto de una nota custom", {
            {"In Custom notes, pick a type and press Change its look…. Two ways:", "En Notas custom, elige un tipo y pulsa Cambiar su aspecto…. Dos caminos:"},
            {"# Your images", "# Tus imágenes"},
            {"- Your sheet (PNG + XML or TXT), loose PNG images, a folder of numbered frames or an animated GIF. You can also drop them on the window while the dialog is open.",
             "- Tu hoja (PNG + XML o TXT), imágenes PNG sueltas, una carpeta de fotogramas numerados o un GIF animado. También puedes soltarlos en la ventana con el diálogo abierto."},
            {"- Frames named like the base game (purple0, purple hold piece...) or after the direction go to their place by themselves. The rest you choose in What goes where.",
             "- Los fotogramas con nombres del juego base (purple0, purple hold piece...) o de la dirección van solos a su sitio. Lo demás lo eliges en Qué va en cada sitio."},
            {"- One image can serve the four directions; if it's the left arrow, Note Lab rotates it for the others.",
             "- Una sola imagen sirve para las cuatro direcciones; si es la flecha izquierda, Note Lab la gira para las demás."},
            {"# Paint a skin", "# Pintar un skin"},
            {"- Start from a skin and change its color (one, per direction or three), put a mark on it (skull, bolt, heart, star...), make it see-through, and choose what happens to its hold and splash. Presets: Poison, Hurt, Ice, Fire, Gold, Ghost, Rainbow.",
             "- Parte de un skin y cámbiale el color (uno, por dirección o tres), ponle una marca (calavera, rayo, corazón, estrella...), hazlo transparente y elige qué pasa con su sostenido y su salpicadura. Presets: Veneno, Daño, Hielo, Fuego, Oro, Fantasma, Arcoíris."},
            {"The look shows on the notes of that type in the preview and in the catalog, and goes with the type when you export it. Back to the mod's look removes it.",
             "El aspecto se ve en las notas de ese tipo en la vista previa y en el catálogo, y va con el tipo al exportarlo. Volver al aspecto del mod lo quita."},
        }},
        {"Ranking HUD with text", "HUD de ranking con texto", {
            {"File > Create ranking HUD · Text…, or Ranking with text… under the HUD of the inspector.", "Archivo > Crear HUD de ranking · Texto…, o Ranking con texto… bajo el HUD del inspector."},
            {"- Write the four judgements and the combo label; each one has two colors (top and bottom). The digits use the combo's.",
             "- Escribe los cuatro juicios y el rótulo del combo; cada uno lleva dos colores (arriba y abajo). Las cifras usan los del rótulo."},
            {"- Any Windows font or an imported .ttf/.otf; size, tilt, spacing, outline, shadow and pixel art.",
             "- Cualquier letra de Windows o un .ttf/.otf importado; tamaño, inclinación, espaciado, contorno, sombra y pixel art."},
            {"The preview shows them as Codename and Psych draw them: judgements at 0.7 and digits at 0.5. Use in the HUD puts the 15 images in the chosen style, with undo.",
             "La vista los enseña como los dibujan Codename y Psych: juicios a 0,7 y cifras a 0,5. Usar en el HUD pone las 15 imágenes en el estilo elegido, con deshacer."},
            {"The letters become images: the engine doesn't need the font.", "Las letras se convierten en imágenes: el motor no necesita la fuente."},
        }},
        {"Importing images", "Importar imágenes", {
            {"Note Lab reads PNG with its XML (Sparrow) or TXT (Packer), loose PNG, folders of numbered frames (name0000.png...) and GIF.",
             "Note Lab lee PNG con su XML (Sparrow) o TXT (Packer), PNG sueltos, carpetas de fotogramas numerados (nombre0000.png...) y GIF."},
            {"It recognizes pieces by the names the engines use: purple0, purple hold piece, pruple end hold, arrowLEFT, left press, left confirm, note impact 1 purple, note splash purple 1, noteLeft, staticLeft, holdCoverStartPurple and the Extra Keys letters (A0, A hold...). And HUD files by name: sick, good, bad, shit, combo, num0-num9, ready, set, go, intro3...",
             "Reconoce las piezas por los nombres que usan los motores: purple0, purple hold piece, pruple end hold, arrowLEFT, left press, left confirm, note impact 1 purple, note splash purple 1, noteLeft, staticLeft, holdCoverStartPurple y las letras de Extra Keys (A0, A hold...). Y los archivos del HUD por su nombre: sick, good, bad, shit, combo, num0-num9, ready, set, go, intro3..."},
            {"Loose frames and GIFs are packed into one Sparrow sheet with the names the engines look for. What you import is copied to Note Lab's folder and travels with the project.",
             "Los fotogramas sueltos y los GIF se empaquetan en una hoja Sparrow con los nombres que buscan los motores. Lo importado se copia a la carpeta de Note Lab y viaja con el proyecto."},
            {"Drop images on the window: Note Lab asks what they are for (the look of a custom note or the HUD of the chosen style).",
             "Suelta imágenes en la ventana: Note Lab pregunta para qué son (el aspecto de una nota custom o el HUD del estilo elegido)."},
        }},
        {"Preview and play", "Vista previa y jugar", {
            {"The preview plays a demo pattern or any chart of an open mod, with its music, speed and BPM. See the opponent, the player or both, downscroll and the Psych colors.",
             "La vista previa toca un patrón de prueba o cualquier chart de un mod abierto, con su música, velocidad y BPM. Ve el rival, el jugador o los dos, downscroll y los colores de Psych."},
            {"Play: turn it on and play one side with D F J K (Keys… changes them). The scoreboard starts from zero on every run: restarting, jumping, changing the chart, starting or stopping and changing side. Reset puts it to zero without stopping the song.",
             "Jugar: actívalo y juega un lado con D F J K (Teclas… las cambia). El marcador empieza de cero en cada pasada: reiniciar, saltar, cambiar de chart, empezar o dejar de jugar y cambiar de lado. A cero lo pone a cero sin parar la canción."},
            {"When a played run ends, it stops and shows the result; Play again or Space starts another.",
             "Al acabar una pasada jugada se para y enseña el resultado; Jugar otra vez o Espacio empiezan otra."},
        }},
        {"Custom note blocks", "Bloques de una nota custom", {
            {"In Custom notes > Blocks, a type's behavior is built like in Scratch or App Inventor: drag blocks from the palette and snap them.",
             "En Notas custom > Bloques, el comportamiento de un tipo se arma como en Scratch o App Inventor: arrastra bloques de la paleta y encájalos."},
            {"- When created: properties, what the note is (avoid it, health when hit or missed, no splash...).",
             "- Al crear: propiedades, lo que la nota es (hay que evitarla, vida al tocarla o fallarla, sin salpicadura...)."},
            {"- When ... hits and When the player misses: actions, conditions and values (health, score, camera, sounds, animations...).",
             "- Cuando ... toca y Cuando el jugador falla: acciones, condiciones y valores (vida, puntos, cámara, sonidos, animaciones...)."},
            {"Blocks | Code | Both shows the code each engine gets; clashes are marked. Presets add ready-made types.",
             "Bloques | Código | Los dos enseña el código que recibe cada motor; los choques se marcan. Los presets añaden tipos listos."},
        }},
        {"Export and install", "Exportar e instalar", {
            {"Export (Ctrl+E) writes a package with the shape of a mod of the engine you choose, to a folder or a ZIP, with an install guide in English and Spanish. It never writes into an open mod.",
             "Exportar (Ctrl+E) escribe un paquete con la forma de un mod del motor que elijas, en carpeta o ZIP, con una guía de instalación en inglés y español. Nunca escribe dentro de un mod abierto."},
            {"- Codename: images/game/notes/default (the mod's skin) or images/game/notes/<type> and data/notes/<type>.hx (a type).",
             "- Codename: images/game/notes/default (el skin del mod) o images/game/notes/<tipo> y data/notes/<tipo>.hx (un tipo)."},
            {"- Psych: images/noteSkins/NOTE_assets (the mod's skin or one to choose in Options) or images/notetypes/<type> and custom_notetypes/<type> (a type).",
             "- Psych: images/noteSkins/NOTE_assets (el skin del mod o uno elegible en Opciones) o images/notetypes/<tipo> y custom_notetypes/<tipo> (un tipo)."},
            {"- V-Slice: data/notestyles/<name>.json with its images, and scripts/notekinds/<type>.hxc for a type.",
             "- V-Slice: data/notestyles/<nombre>.json con sus imágenes, y scripts/notekinds/<tipo>.hxc para un tipo."},
            {"Before writing, Note Lab reads the package back with its own readers: «structure verified». Opening it in the engine is the last check.",
             "Antes de escribir, Note Lab relee el paquete con sus propios lectores: «estructura verificada». Abrirlo en el motor es la última prueba."},
        }},
        {"When something shows red", "Si algo sale en rojo", {
            {"Each finding has a code and says the piece in words. The most common:", "Cada hallazgo lleva un código y dice la pieza con palabras. Los más comunes:"},
            {"- FML-NOTE-001: no frame starts with the name the engine looks for; the engine would draw nothing. Pick the piece's animation in the asset viewer or import your sheet.",
             "- FML-NOTE-001: ningún fotograma empieza por el nombre que busca el motor; el motor no dibujaría nada. Elige la animación de la pieza en el visor de assets o importa tu hoja."},
            {"- FML-NOTE-002 / 003: the image or its atlas is not in the opened folders (maybe it's in the base game: add it with Base game).",
             "- FML-NOTE-002 / 003: la imagen o su atlas no están en las carpetas abiertas (quizá esté en el juego base: añádelo con Juego base)."},
            {"- FML-NOTE-018: a note type without holds. It only matters if a chart gives it long notes.",
             "- FML-NOTE-018: un tipo de nota sin sostenidos. Solo importa si un chart le da notas largas."},
            {"- FML-NOTE-026 / 027: information. A script picks the sheet by name, or the mod uses its own multikey names: Note Lab reads them.",
             "- FML-NOTE-026 / 027: información. Un script elige la hoja por su nombre, o el mod usa sus propios nombres de multikey: Note Lab los lee."},
            {"Blue findings are information: they don't stop anything.", "Los hallazgos azules son información: no impiden nada."},
        }},
        {"Keyboard shortcuts", "Atajos de teclado", {}},
    };
    return topics;
}

void openHelp(NoteLabApp& app, int topic) {
    app.helpTopic = std::clamp(topic, 0, static_cast<int>(helpTopics().size()) - 1);
    app.openHelp = true;
}

// Un «?» que abre la ayuda en su tema, tambien dentro de otro dialogo.
void helpButton(NoteLabApp& app, const char* id, int topic) {
    if (ui::iconButton(id, ui::icon::Help, "?", tr(app, "Help (F1)", "Ayuda (F1)"))) openHelp(app, topic);
}

void drawShortcutRows(NoteLabApp& app) {
    struct Row { const char* keys; const char* en; const char* es; };
    const Row rows[] = {
        {"F1", "This help", "Esta ayuda"},
        {"Ctrl+N", "New project", "Proyecto nuevo"},
        {"Ctrl+Shift+O", "Open a project (.fmlnote)", "Abrir un proyecto (.fmlnote)"},
        {"Ctrl+S / Ctrl+Shift+S", "Save the project / save as", "Guardar el proyecto / guardar como"},
        {"Ctrl+E", "Export the chosen note style", "Exportar el estilo de notas elegido"},
        {"Ctrl+O", "Open a mod folder", "Abrir la carpeta de un mod"},
        {"F5", "Reload everything", "Recargar todo"},
        {"Ctrl+Z / Ctrl+Y", "Undo / redo an edit", "Deshacer / rehacer una edición"},
        {"Space", "Play / pause the preview", "Reproducir / pausar la vista previa"},
        {"R", "Restart the preview", "Reiniciar la vista previa"},
        {"D F J K", "Play the chosen side (Keys… changes them)", "Jugar el lado elegido (Teclas… las cambia)"},
        {"Wheel", "Zoom the asset viewer", "Acercar el visor de assets"},
    };
    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        for (const Row& row : rows) {
            ImGui::TableNextRow(0, ImGui::GetTextLineHeight() + 10.0f);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(ui::fonts().mono, 14.0f);
            ImGui::TextColored(ui::vec(ui::color::Accent), "%s", row.keys);
            ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr(app, row.en, row.es));
        }
        ImGui::EndTable();
    }
}

void drawHelpModal(NoteLabApp& app) {
    if (app.openHelp) {
        ImGui::OpenPopup("###help");
        app.openHelp = false;
    }
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 640.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Help, tr(app, "Note Lab help", "Ayuda de Note Lab")) + "###help").c_str(), &open)) return;
    const auto& topics = helpTopics();
    const std::string filter = lowerText(app.helpSearch.data());
    auto matches = [&](const HelpTopic& topic) {
        // El ranking con texto esta oculto en esta build: su tema tambien.
        if (!kTextRankingVisible && std::string(topic.en) == "Ranking HUD with text") return false;
        if (filter.empty()) return true;
        if (lowerText(tr(app, topic.en, topic.es)).find(filter) != std::string::npos) return true;
        for (const auto& line : topic.lines)
            if (lowerText(app.spanish ? line.second : line.first).find(filter) != std::string::npos) return true;
        return false;
    };
    ImGui::BeginChild("helptopics", ImVec2(260.0f, -40.0f), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##helpsearch", tr(app, "Search the help", "Buscar en la ayuda"), app.helpSearch.data(), app.helpSearch.size());
    ImGui::Spacing();
    for (size_t i = 0; i < topics.size(); ++i) {
        if (!matches(topics[i])) continue;
        if (ImGui::Selectable(tr(app, topics[i].en, topics[i].es), app.helpTopic == static_cast<int>(i))) app.helpTopic = static_cast<int>(i);
    }
    ImGui::Spacing();
    ui::caption(tr(app, "Every ? in Note Lab opens its topic here.", "Cada «?» de Note Lab abre aquí su tema."));
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("helptext", ImVec2(0.0f, -40.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    const HelpTopic& topic = topics[static_cast<size_t>(std::clamp(app.helpTopic, 0, static_cast<int>(topics.size()) - 1))];
    ui::title(tr(app, topic.en, topic.es), 21.0f);
    ImGui::Spacing();
    if (topic.lines.empty()) {
        drawShortcutRows(app);
    } else {
        ImGui::PushTextWrapPos(0.0f);
        for (const auto& line : topic.lines) {
            const std::string text = app.spanish ? line.second : line.first;
            if (text.rfind("# ", 0) == 0) {
                ui::sectionHeader(text.substr(2).c_str(), ui::icon::Info);
            } else if (text.rfind("- ", 0) == 0) {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", text.substr(2).c_str());
            } else {
                ImGui::TextWrapped("%s", text.c_str());
            }
            ImGui::Spacing();
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    if (ui::primaryButton(tr(app, "Close", "Cerrar"), ImVec2(120.0f, 0.0f)) || escapeClosesWindow())
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// --------------------------------------------- crear sin dibujar (§31) --

// Lo que se crea en Note Lab se guarda en la carpeta de preferencias, nunca en
// el mod, con su huella en el nombre, y se monta en la VFS del mod como una
// importacion: el proyecto lo vuelve a montar al abrirse.
fs::path createdFolder() { return settingsFolder() / "created"; }

std::string saveCreatedBytes(const std::vector<unsigned char>& bytes, const std::string& name) {
    if (bytes.empty()) return {};
    const std::string content(bytes.begin(), bytes.end());
    const fs::path file = createdFolder() / pathFromUtf8(sha256Hex(content).substr(0, 16) + "-" + name);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (!fs::exists(file, ec)) {
        std::ofstream out(file, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) return {};
    }
    return file.u8string();
}

// Un archivo creado o importado, montado en la VFS del mod. Devuelve su ruta virtual.
std::string mountCreated(Source& source, const std::string& file, const std::string& name, const std::string& atlasFile = {},
                         const std::string& atlasName = {}) {
    const std::string folder = "notelab-import/" + std::to_string(++source.imports) + "/";
    const std::string image = folder + name;
    if (!source.vfs->pushFile(pathFromUtf8(file), image, "import")) return {};
    std::string atlas;
    if (!atlasFile.empty()) {
        atlas = folder + atlasName;
        if (!source.vfs->pushFile(pathFromUtf8(atlasFile), atlas, "import")) atlas.clear();
    }
    source.importedFiles.push_back({file, atlasFile, image, atlas});
    return image;
}

// Las hojas y atlas de un estilo leidos de una vez, para pintarlo en otro
// hilo: la VFS (un ZIP abierto con miniz) no se lee desde dos hilos.
ExportIo memoryIo(const Source& source, const NoteStyle& style) {
    auto files = std::make_shared<std::map<std::string, std::vector<unsigned char>>>();
    for (const Sheet& sheet : style.sheets)
        for (const std::string* path : {&sheet.image, &sheet.atlas})
            if (!path->empty() && !files->count(*path))
                if (auto bytes = source.vfs->readBytes(*path, 96u * 1024u * 1024u)) (*files)[*path] = std::move(*bytes);
    ExportIo io;
    io.readBytes = [files](const std::string& path) -> std::optional<std::vector<unsigned char>> {
        const auto it = files->find(path);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
    io.readText = [files](const std::string& path) -> std::optional<std::string> {
        const auto it = files->find(path);
        if (it == files->end()) return std::nullopt;
        return std::string(it->second.begin(), it->second.end());
    };
    return io;
}

// Una imagen en memoria como textura del renderer (BGRA), con una ruta fija:
// se reutiliza en cada cambio, sin llenar la memoria de video.
void uploadLiveImage(NoteLabApp& app, const std::string& path, const Image& image) {
    if (image.empty()) return;
    std::vector<unsigned char> bgra(image.rgba.size());
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
        bgra[i] = image.rgba[i + 2];
        bgra[i + 1] = image.rgba[i + 1];
        bgra[i + 2] = image.rgba[i];
        bgra[i + 3] = image.rgba[i + 3];
    }
    app.renderer.uploadDynamicFrame(path, bgra.data(), image.w, image.h);
}

void dropLive(NoteLabApp& app, NoteLabApp::LiveSheets& live) {
    for (const std::string& path : live.uploaded) app.renderer.removeDynamicFrame(path);
    live.uploaded.clear();
    live.painted = -1;
    live.images.clear();
}

// Pinta en otro hilo cuando la receta lleva un momento quieta y sube lo
// pintado: `live.style` es el estilo de partida con sus hojas cambiadas.
void pumpLive(NoteLabApp& app, NoteLabApp::LiveSheets& live, const Source& source, const NoteStyle& base,
              const std::function<Paint(Part, int)>& paintFor, const char* tag) {
    if (live.job.valid() && live.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        live.images = live.job.get();
        live.painted = live.running;
        live.running = -1;
        live.style = base;
        for (size_t i = 0; i < live.images.size(); ++i) {
            const std::string path = std::string("notelab-live/") + tag + "/" + std::to_string(i) + ".png";
            uploadLiveImage(app, path, live.images[i].pixels);
            if (std::find(live.uploaded.begin(), live.uploaded.end(), path) == live.uploaded.end()) live.uploaded.push_back(path);
            // Con regiones separadas, su atlas nuevo va a la cache en memoria.
            std::string atlas;
            if (!live.images[i].atlasText.empty()) {
                DiagnosticSink sink;
                Result<SparrowAtlas> parsed = parseSparrowAtlas(live.images[i].atlasText, live.images[i].atlas, sink);
                if (parsed) {
                    atlas = std::string("notelab-live/") + tag + "/" + std::to_string(i) + ".xml";
                    app.atlases.putSparrow(atlas, std::move(parsed.value()));
                }
            }
            for (Sheet& sheet : live.style.sheets)
                if (sheet.image == live.images[i].image) {
                    if (!atlas.empty() && sheet.atlas == live.images[i].atlas) sheet.atlas = atlas;
                    sheet.image = path;
                }
        }
        // La plantilla RGB ya va horneada en lo pintado.
        live.style.rgbPalette = false;
    }
    if (live.job.valid() || live.painted == live.generation) return;
    if (ImGui::GetTime() - live.changedAt < 0.18) return;
    live.running = live.generation;
    const ExportIo io = memoryIo(source, base);
    live.job = std::async(std::launch::async, [base, paintFor, io]() { return paintStyleImages(base, paintFor, io); });
}

// La carpeta de un estilo creado: el siguiente id libre en ese mod.
std::string freeVariantId(const Source& source, const std::string& prefix) {
    int next = 1;
    auto taken = [&](const std::string& id) {
        return std::any_of(source.catalog.styles.begin(), source.catalog.styles.end(), [&](const NoteStyle& s) { return s.id == id; });
    };
    std::string id;
    do id = prefix + std::to_string(next++);
    while (taken(id));
    return id;
}

int addCreatedStyle(NoteLabApp& app, int sourceIndex, NoteStyle style) {
    Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    source.catalog.styles.push_back(std::move(style));
    source.reports.push_back(checkNoteStyle(*source.vfs, source.catalog.styles.back(), source.atlasCache));
    source.fromBase.push_back(0);
    source.edited.push_back(1);
    Engine engine;
    if (sourceEngine(app, source, engine)) settleOtherEngines(source.catalog, source.reports, engine);
    app.dirty = true;
    return static_cast<int>(source.catalog.styles.size()) - 1;
}

// Las hojas pintadas a disco (carpeta de preferencias) y montadas: el estilo
// queda con rutas que el proyecto puede volver a montar.
bool commitPainted(NoteLabApp& app, Source& source, const std::vector<PaintedImage>& images, NoteStyle& style,
                   const std::string& stem) {
    for (const PaintedImage& painted : images) {
        const std::string base = exportName(stem) + "-" + exportName(pathFromUtf8(painted.image).stem().u8string());
        const std::string name = base + ".png";
        const std::string file = saveCreatedBytes(encodePng(painted.pixels), name);
        if (file.empty()) return false;
        // Con regiones separadas la hoja lleva su atlas reescrito al lado.
        std::string atlasFile;
        if (!painted.atlasText.empty()) {
            atlasFile = saveCreatedBytes(std::vector<unsigned char>(painted.atlasText.begin(), painted.atlasText.end()), base + ".xml");
            if (atlasFile.empty()) return false;
        }
        const std::string mounted = mountCreated(source, file, name, atlasFile, atlasFile.empty() ? std::string() : base + ".xml");
        if (mounted.empty()) return false;
        const std::string atlas = atlasFile.empty() ? std::string() : source.importedFiles.back().atlasVirtual;
        if (!atlasFile.empty() && atlas.empty()) return false;
        for (Sheet& sheet : style.sheets)
            if (sheet.image == painted.image) {
                if (!atlas.empty() && sheet.atlas == painted.atlas) sheet.atlas = atlas;
                sheet.image = mounted;
            }
    }
    style.rgbPalette = style.rgbPalette && images.empty();
    return true;
}

// Los estilos de un mod donde se puede partir para crear: los que tienen notas.
std::vector<int> paintableStyles(const Source& source) {
    std::vector<int> out;
    for (size_t i = 0; i < source.catalog.styles.size(); ++i) {
        const NoteStyle& style = source.catalog.styles[i];
        if (std::any_of(style.parts.begin(), style.parts.end(), [](const PartBinding& b) { return b.part == Part::Note; }))
            out.push_back(static_cast<int>(i));
    }
    return out;
}

bool colorEdit(const char* id, std::uint32_t& argb, const char* tip = nullptr) {
    float c[3] = {((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f};
    const bool changed = ImGui::ColorEdit3(id, c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    if (tip) ui::tooltip(tip);
    if (!changed) return false;
    argb = 0xFF000000u | (static_cast<std::uint32_t>(std::lround(c[0] * 255.0f)) << 16) |
           (static_cast<std::uint32_t>(std::lround(c[1] * 255.0f)) << 8) | static_cast<std::uint32_t>(std::lround(c[2] * 255.0f));
    return true;
}

// Una linea de juego en miniatura con un estilo: receptores, notas que caen,
// un sostenido, un acierto y una salpicadura, para ver lo que se cambia.
void drawMiniLane(NoteLabApp& app, const NoteStyle& style, ImVec2 size) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(11, 13, 17, 255), 8.0f);
    draw->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), ui::color::Border, 8.0f);
    const double clock = ImGui::GetTime() * 1000.0;
    const float lane = std::min(size.x / 4.6f, 96.0f);
    const float left = at.x + (size.x - lane * 4.0f) * 0.5f;
    const float top = at.y + 14.0f;
    auto bindingOf = [&](Part part, int d) -> const PartBinding* {
        for (const PartBinding& b : style.parts)
            if (b.part == part && b.direction == d && b.variant == 0) return &b;
        return nullptr;
    };
    auto drawPart = [&](Part part, int d, ImVec2 center, float box, double ms) {
        const PartBinding* b = bindingOf(part, d);
        Thumb thumb;
        if (!b || !partThumb(app, style, *b, ms, thumb)) return false;
        drawThumbAt(draw, thumb, ImVec2(center.x - box * 0.5f, center.y - box * 0.5f), box, 0.0f);
        return true;
    };
    draw->PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);
    const double cycle = 2400.0;
    for (int d = 0; d < 4; ++d) {
        const ImVec2 strum(left + lane * (d + 0.5f), top + lane * 0.5f);
        // Cada carril acierta a su tiempo: el receptor se ilumina al llegar la nota.
        const double phase = std::fmod(clock + d * cycle / 4.0, cycle);
        const bool hit = phase > cycle - 260.0;
        if (!(hit && drawPart(Part::StrumConfirm, d, strum, lane * 1.15f, phase - (cycle - 260.0))))
            drawPart(Part::StrumStatic, d, strum, lane, -1.0);
        const float travel = size.y - lane;
        const float y = strum.y + travel * static_cast<float>(1.0 - phase / cycle);
        if (d == 1) {
            // Un sostenido en el carril de abajo.
            const PartBinding* piece = bindingOf(Part::HoldPiece, d);
            const PartBinding* end = bindingOf(Part::HoldEnd, d);
            Thumb t;
            if (piece && partThumb(app, style, *piece, -1.0, t)) {
                const float w = lane * 0.42f;
                const float from = y, to = std::min(at.y + size.y - lane * 0.4f, y + lane * 2.2f);
                if (to > from)
                    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(t.texture)), ImVec2(strum.x - w * 0.5f, from),
                                   ImVec2(strum.x + w * 0.5f, to), t.uv0, t.uv1);
                if (end && partThumb(app, style, *end, -1.0, t))
                    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(t.texture)), ImVec2(strum.x - w * 0.5f, to),
                                   ImVec2(strum.x + w * 0.5f, to + w * 0.6f), t.uv0, t.uv1);
            }
        }
        if (!hit) drawPart(Part::Note, d, ImVec2(strum.x, y), lane, -1.0);
        else drawPart(Part::Splash, d, strum, lane * 2.0f, phase - (cycle - 260.0));
    }
    draw->PopClipRect();
    ImGui::Dummy(size);
}

// La rejilla de piezas: una fila por clase de pieza, una columna por direccion.
void drawPieceRows(NoteLabApp& app, const NoteStyle& style, std::initializer_list<Part> parts, float cell) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (Part part : parts) {
        bool any = false;
        for (const PartBinding& b : style.parts) any = any || b.part == part;
        if (!any) continue;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(partLabel(part, app.spanish));
        ImGui::SameLine(110.0f);
        for (int d = 0; d < 4; ++d) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            drawChecker(draw, at, ImVec2(at.x + cell, at.y + cell));
            for (const PartBinding& b : style.parts) {
                if (b.part != part || b.direction != d || b.variant != 0) continue;
                Thumb thumb;
                if (partThumb(app, style, b, ImGui::GetTime() * 1000.0, thumb)) drawThumbAt(draw, thumb, at, cell, 3.0f);
                break;
            }
            ImGui::Dummy(ImVec2(cell, cell));
            if (d < 3) ImGui::SameLine(0.0f, 4.0f);
        }
    }
}

const char* arrowPresetLabel(const NoteLabApp& app, const std::string& key) {
    if (key == "classic") return tr(app, "Classic", "Clásico");
    if (key == "pastel") return "Pastel";
    if (key == "neon") return tr(app, "Neon", "Neón");
    if (key == "mono") return tr(app, "Mono", "Monocromo");
    if (key == "inverted") return tr(app, "Inverted", "Invertido");
    if (key == "fireice") return tr(app, "Fire and ice", "Fuego y hielo");
    if (key == "random") return tr(app, "Random", "Al azar");
    return key.c_str();
}

const char* lookPresetLabel(const NoteLabApp& app, const std::string& key) {
    if (key == "poison") return tr(app, "Poison", "Veneno");
    if (key == "hurt") return tr(app, "Hurt", "Daño");
    if (key == "ice") return tr(app, "Ice", "Hielo");
    if (key == "fire") return tr(app, "Fire", "Fuego");
    if (key == "gold") return tr(app, "Gold", "Oro");
    if (key == "ghost") return tr(app, "Ghost", "Fantasma");
    if (key == "rainbow") return tr(app, "Rainbow", "Arcoíris");
    return key.c_str();
}

// ------------------------------------------------------- HUD de notas nuevo --

void openCreateHud(NoteLabApp& app) {
    if (app.selSource < 0 || app.selSource >= static_cast<int>(app.sources.size())) {
        setStatus(app, "Open a mod first: the new HUD starts from one of its styles.",
                  "Abre un mod antes: el HUD nuevo parte de uno de sus estilos.");
        return;
    }
    auto& c = app.createHud;
    dropLive(app, c.live);
    c.source = app.selSource;
    const Source& source = *app.sources[static_cast<size_t>(c.source)];
    const std::vector<int> styles = paintableStyles(source);
    c.base = std::find(styles.begin(), styles.end(), app.selStyle) != styles.end() ? app.selStyle : (styles.empty() ? -1 : styles.front());
    c.recipe = ArrowRecipe{};
    arrowPreset(c.preset, c.recipe.colors, c.seed);
    c.editStyle = -1;
    if (const NoteStyle* selected = selectedStyle(app)) {
        const auto saved = source.recipes.find("arrows:" + selected->id);
        if (saved != source.recipes.end()) {
            c.recipe = saved->second.arrows;
            c.editStyle = app.selStyle;
            c.base = -1;
            for (size_t i = 0; i < source.catalog.styles.size(); ++i)
                if (source.catalog.styles[i].id == saved->second.baseStyle) c.base = static_cast<int>(i);
        }
    }
    std::snprintf(c.name.data(), c.name.size(), "%s", tr(app, "My notes", "Mis notas"));
    if (c.editStyle >= 0) std::snprintf(c.name.data(), c.name.size(), "%s", source.catalog.styles[static_cast<size_t>(c.editStyle)].name.c_str());
    // Las piezas dibujadas se vuelven a leer de su PNG (sin tenir, sus
    // fotogramas en fila), guardado con lo creado.
    c.useDrawn = false;
    c.drawn = DrawnPieces{};
    for (size_t p = 0; p < c.recipe.drawn.size(); ++p) {
        if (c.recipe.drawn[p].empty()) continue;
        if (const auto bytes = source.vfs->readBytes(c.recipe.drawn[p], 32u * 1024u * 1024u)) {
            Image strip;
            const int side = drawnPieceSize(static_cast<DrawnPiece>(p));
            if (decodePng(*bytes, strip) && strip.h == side && strip.w >= side && strip.w % side == 0)
                for (int x = 0; x < strip.w; x += side) c.drawn[p].push_back(cropRect(strip, x, 0, side, side));
        }
        c.useDrawn = c.useDrawn || !c.drawn[p].empty();
    }
    c.spriteFile.clear();
    if (c.editStyle >= 0) {
        const auto saved = source.recipes.find("arrows:" + source.catalog.styles[static_cast<size_t>(c.editStyle)].id);
        if (saved != source.recipes.end()) c.spriteFile = saved->second.sprite;
    }
    c.layersKey = "hud|" + std::to_string(c.source) + "|" +
                  (c.editStyle >= 0 ? source.catalog.styles[static_cast<size_t>(c.editStyle)].id : std::string("new"));
    c.drawnFor = c.drawnVersionFor = -1;
    ++c.drawnVersion;
    c.live.generation++;
    c.live.changedAt = -10.0;
    c.step = 0;
    c.thumbsFor = -1;
    c.requestOpen = true;
}

bool drawnAny(const DrawnPieces& pieces) {
    return std::any_of(pieces.begin(), pieces.end(), [](const std::vector<Image>& frames) { return !frames.empty(); });
}

// La escala de las notas del punto de partida, para que lo dibujado (160 px,
// como una nota del juego base) salga del mismo tamano; las pixel no sirven.
float drawnArrowScale(const NoteStyle& base) {
    for (const PartBinding& b : base.parts)
        if (b.part == Part::Note && b.sheet >= 0 && b.sheet < static_cast<int>(base.sheets.size())) {
            const Sheet& sheet = base.sheets[static_cast<size_t>(b.sheet)];
            if (!sheet.pixel && sheet.scale > 0.3f && sheet.scale < 1.5f) return sheet.scale;
            break;
        }
    return 0.7f;
}

void createHudNow(NoteLabApp& app) {
    auto& c = app.createHud;
    if (c.source < 0 || c.source >= static_cast<int>(app.sources.size())) return;
    Source& source = *app.sources[static_cast<size_t>(c.source)];
    if (c.base < 0 || c.base >= static_cast<int>(source.catalog.styles.size()) || c.live.painted != c.live.generation) return;
    const NoteStyle& base = source.catalog.styles[static_cast<size_t>(c.base)];
    NoteStyle style = base;
    style.id = freeVariantId(source, "notelab:hud") + ":" + base.id;
    if (c.editStyle >= 0 && c.editStyle < static_cast<int>(source.catalog.styles.size())) {
        style = source.catalog.styles[static_cast<size_t>(c.editStyle)];
        style.sheets = base.sheets;
        style.parts = base.parts;
    }
    style.name = c.name[0] ? std::string(c.name.data()) : std::string(tr(app, "My notes", "Mis notas"));
    style.use = StyleUse::Declared;
    style.useDetail.clear();
    style.referenced = true;
    if (!commitPainted(app, source, c.live.images, style, style.name)) {
        setStatus(app, "The painted sheets could not be saved.", "No se pudieron guardar las hojas pintadas.");
        return;
    }
    if (!c.live.images.empty()) style.rgbPalette = false;
    c.recipe.drawn = {};
    if (c.useDrawn && drawnAny(c.drawn)) {
        // Cada pieza sin tenir (sus fotogramas en fila), para volver a editarla,
        // y el archivo con todas las piezas dibujadas ya tenidas: un atlas con
        // los nombres que buscan los motores. Se guardan con lo creado.
        const std::string stem = exportName(style.name);
        static const char* pieceFiles[kDrawnPieceCount] = {"nota", "receptor", "tramo", "final", "salpicadura", "receptor-pulsado", "receptor-acierto"};
        for (size_t p = 0; p < c.drawn.size(); ++p) {
            if (c.drawn[p].empty()) continue;
            const int side = c.drawn[p].front().h;
            Image strip = blankImage(side * static_cast<int>(c.drawn[p].size()), side);
            for (size_t k = 0; k < c.drawn[p].size(); ++k) blit(strip, c.drawn[p][k], static_cast<int>(k) * side, 0);
            const std::string name = stem + "-" + pieceFiles[p] + ".png";
            const std::string file = saveCreatedBytes(encodePng(strip), name);
            c.recipe.drawn[p] = file.empty() ? std::string() : mountCreated(source, file, name);
            if (c.recipe.drawn[p].empty()) {
                setStatus(app, "The drawn pieces could not be saved.", "No se pudieron guardar las piezas dibujadas.");
                return;
            }
        }
        const DrawnAtlas atlas = drawnPieceAtlas(c.drawn, c.recipe, stem + "-dibujado.png");
        const std::string png = atlas.atlas.ok ? saveCreatedBytes(atlas.atlas.png, stem + "-dibujado.png") : std::string();
        const std::string xml = atlas.atlas.ok ? saveCreatedBytes(std::vector<unsigned char>(atlas.atlas.xml.begin(), atlas.atlas.xml.end()), stem + "-dibujado.xml")
                                               : std::string();
        const std::string image = png.empty() || xml.empty() ? std::string() : mountCreated(source, png, stem + "-dibujado.png", xml, stem + "-dibujado.xml");
        if (image.empty() || source.importedFiles.back().atlasVirtual.empty()) {
            setStatus(app, "The drawn pieces could not be saved.", "No se pudieron guardar las piezas dibujadas.");
            return;
        }
        bindDrawnAtlas(style, atlas, image, source.importedFiles.back().atlasVirtual, drawnArrowScale(base));
    }
    CreationRecipe recipe;
    recipe.styleId = style.id; recipe.baseStyle = base.id; recipe.kind = "arrows"; recipe.arrows = c.recipe;
    if (c.useDrawn && drawnAny(c.drawn)) recipe.sprite = c.spriteFile;
    int index = c.editStyle;
    if (index >= 0 && index < static_cast<int>(source.catalog.styles.size())) {
        selectStyle(app, c.source, index);
        beginEdit(app);
        source.catalog.styles[static_cast<size_t>(index)] = std::move(style);
        afterEdit(app);
    } else index = addCreatedStyle(app, c.source, std::move(style));
    source.recipes["arrows:" + recipe.styleId] = std::move(recipe);
    dropLive(app, c.live);
    // Las capas de lo dibujado siguen a mano si se vuelve a editar este HUD.
    const auto layers = app.spriteEditor.drawn.find(c.layersKey);
    if (layers != app.spriteEditor.drawn.end())
        app.spriteEditor.drawn["hud|" + std::to_string(c.source) + "|" + source.catalog.styles[static_cast<size_t>(index)].id] = layers->second;
    selectStyle(app, c.source, index);
    if (c.editStyle >= 0) setStatus(app, "Note HUD updated. Its recipe and existing ranking are preserved; the mod is not touched.",
                                  "HUD de notas actualizado. Conserva su receta y el ranking existente; el mod no se toca.");
    else setStatus(app, "Note HUD created: it's a new style of the project (the mod is not touched). Export it to any of the three engines.",
                   "HUD de notas creado: es un estilo nuevo del proyecto (el mod no se toca). Expórtalo a cualquiera de los tres motores.");
}

void drawCreateHudModal(NoteLabApp& app) {
    auto& c = app.createHud;
    if (c.requestOpen) {
        ImGui::OpenPopup("###createhud");
        c.requestOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(1060.0f, 690.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Brush, tr(app, "Create note HUD", "Crear HUD de notas")) + "###createhud").c_str(), &open)) {
        if (app.spriteEditor.target == 1 && (app.spriteEditor.painted >= 0 || app.spriteEditor.shown >= 0)) releaseSpritePreview(app);
        c.drawnFor = -1;
        if (c.live.job.valid() || !c.live.uploaded.empty()) {
            if (c.live.job.valid()) c.live.job.wait();
            c.live.job = {};
            dropLive(app, c.live);
        }
        return;
    }
    if (c.source < 0 || c.source >= static_cast<int>(app.sources.size())) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    Source& source = *app.sources[static_cast<size_t>(c.source)];
    const std::vector<int> styles = paintableStyles(source);
    bool changed = false;
    tutorialZone(app, kTrackHud);
    helpButton(app, "createhudhelp", 1);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::caption(tr(app, "Your own note HUD, step by step: start from a style, give it your colors and, if you want, draw its pieces.",
                        "Tu propio HUD de notas, paso a paso: parte de un estilo, ponle tus colores y, si quieres, dibuja sus piezas."));
    ImGui::BeginChild("hudleft", ImVec2(450.0f, -48.0f));
    // Por pasos (pedido del autor, 5 oct 2026): base, colores, detalles y piezas.
    // Cada paso se puede elegir en cualquier orden; lo que el tutorial senala en
    // otro paso lo senala en su boton.
    {
        static const char* stepKeys[4][2] = {{"hud-name", "hud-base"}, {"hud-colors", nullptr}, {"hud-details", nullptr}, {"hud-shape", "hud-sheet"}};
        const char* names[4] = {tr(app, "Base", "Base"), tr(app, "Colors", "Colores"), tr(app, "Details", "Detalles"), tr(app, "Pieces", "Piezas")};
        const float gap = 4.0f;
        const float w = (ImGui::GetContentRegionAvail().x - gap * 3.0f) / 4.0f;
        for (int k = 0; k < 4; ++k) {
            ImGui::PushID(k);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton("step", ImVec2(w, 36.0f));
            const bool hovered = ImGui::IsItemHovered();
            const bool on = c.step == k;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + w, at.y + 36.0f), on ? IM_COL32(155, 123, 245, 46) : hovered ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 8), 8.0f);
            if (on) draw->AddRect(at, ImVec2(at.x + w, at.y + 36.0f), ui::color::Accent, 8.0f, 0, 1.5f);
            const ImVec2 dot(at.x + 18.0f, at.y + 18.0f);
            draw->AddCircleFilled(dot, 10.0f, on ? ui::color::Accent : ui::color::Border);
            const std::string number = std::to_string(k + 1);
            const ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
            draw->AddText(ImVec2(dot.x - numberSize.x * 0.5f, dot.y - numberSize.y * 0.5f), on ? IM_COL32(255, 255, 255, 255) : ui::color::Muted, number.c_str());
            draw->AddText(ImVec2(at.x + 34.0f, at.y + 18.0f - ImGui::GetFontSize() * 0.5f), on ? ui::color::Text : ui::color::Muted, names[k]);
            if (pressed) c.step = k;
            if (!on)
                for (const char* key : stepKeys[k])
                    if (key) tutorialMark(key);
            tutorialMark(("hud-step-" + std::to_string(k)).c_str());
            ImGui::PopID();
            if (k < 3) ImGui::SameLine(0.0f, gap);
        }
    }
    ImGui::Spacing();
    if (c.step == 0) {
        ui::sectionHeader(tr(app, "Name", "Nombre"), ui::icon::Edit);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##hudname", c.name.data(), c.name.size());
        tutorialMark("hud-name");
        ui::sectionHeader(tr(app, "Starting point", "Punto de partida"), ui::icon::Layers);
        ImGui::SetNextItemWidth(-1.0f);
        const std::string current = c.base >= 0 && c.base < static_cast<int>(source.catalog.styles.size())
                                        ? source.catalog.styles[static_cast<size_t>(c.base)].name
                                        : std::string("-");
        const bool baseOpen = ImGui::BeginCombo("##hudbase", current.c_str());
        if (!baseOpen) tutorialMark("hud-base");
        if (baseOpen) {
            tutorialSignal("hud-base");
            for (int i : styles) {
                const NoteStyle& s = source.catalog.styles[static_cast<size_t>(i)];
                const std::string label = s.name + "  ·  " + useLabel(app, s.use) + "##" + s.id;
                if (ImGui::Selectable(label.c_str(), i == c.base)) {
                    c.base = i;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ui::caption(tr(app, "Its shapes stay; you choose the colors. What you don't change comes out as in that style.",
                            "Sus formas se quedan; tú eliges los colores. Lo que no cambies sale como en ese estilo."));
        if (c.base < 0) ui::caption(tr(app, "The saved starting style is missing. Choose a base explicitly; your existing HUD is kept until you apply changes.",
            "Falta el estilo base guardado. Elige una base; tu HUD actual se conserva hasta aplicar los cambios."));
        ImGui::Spacing();
        if (ui::flatButton(ui::label(ui::icon::Settings, tr(app, "Advanced: own resources…", "Avanzado: recursos propios…")),
                           tr(app, "Your own images, sheets and sounds for each piece", "Tus propias imágenes, hojas y sonidos para cada pieza"))) {
            ImGui::CloseCurrentPopup();
            openCustomCreator(app);
        }
    } else if (c.step == 1) {
        ui::sectionHeader(tr(app, "Colors", "Colores"), ui::icon::Palette);
        ImGui::BeginGroup();
        for (const std::string& key : arrowPresetKeys()) {
            const bool chosen = c.preset == key;
            ImGui::PushStyleColor(ImGuiCol_Button, chosen ? IM_COL32(155, 123, 245, 90) : ImGui::GetColorU32(ImGuiCol_Button));
            if (ImGui::Button((std::string(arrowPresetLabel(app, key)) + "##p" + key).c_str())) {
                c.preset = key;
                if (key == "random") ++c.seed;
                arrowPreset(key, c.recipe.colors, c.seed);
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::GetContentRegionAvail().x < 90.0f) ImGui::NewLine();
        }
        ImGui::NewLine();
        static const char* arrows[4] = {"←", "↓", "↑", "→"};
        for (int d = 0; d < 4; ++d) {
            ImGui::PushID(d);
            changed |= colorEdit("##dir", c.recipe.colors[static_cast<size_t>(d)], tr(app, "Color of this direction", "Color de esta dirección"));
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextUnformatted(arrows[d]);
            ImGui::PopID();
            if (d < 3) ImGui::SameLine(0.0f, 16.0f);
        }
        bool ownOutline = c.recipe.outline != 0;
        if (ImGui::Checkbox(tr(app, "Own outline color", "Contorno de otro color"), &ownOutline)) {
            c.recipe.outline = ownOutline ? 0xFF2A1A3Au : 0;
            changed = true;
        }
        if (ownOutline) {
            ImGui::SameLine();
            changed |= colorEdit("##outline", c.recipe.outline);
        }
        ImGui::SetNextItemWidth(-1.0f);
        float strength = c.recipe.strength * 100.0f;
        if (ImGui::SliderFloat("##strength", &strength, 10.0f, 100.0f, tr(app, "Strength %.0f %%", "Fuerza %.0f %%"))) {
            c.recipe.strength = strength / 100.0f;
            changed = true;
        }
        ImGui::EndGroup();
        tutorialMark("hud-colors");
        if (changed) tutorialSignal("hud-colors");
    } else if (c.step == 2) {
        ui::sectionHeader(tr(app, "Details", "Detalles"), ui::icon::Settings);
        ImGui::BeginGroup();
        const char* strums[] = {tr(app, "Gray", "Grises"), tr(app, "Their color", "De su color"), tr(app, "See-through", "Transparentes")};
        int strumMode = static_cast<int>(c.recipe.strums);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "Receptors", "Receptores"));
        ImGui::SameLine(120.0f);
        if (ui::segmented("hudstrums", &strumMode, strums, 3)) {
            c.recipe.strums = static_cast<StrumLook>(strumMode);
            changed = true;
        }
        const char* confirms[] = {tr(app, "Glow with its color", "Brilla con su color"), tr(app, "White", "Blanco")};
        int confirmMode = static_cast<int>(c.recipe.confirm);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "On hit", "Al acertar"));
        ImGui::SameLine(120.0f);
        if (ui::segmented("hudconfirm", &confirmMode, confirms, 2)) {
            c.recipe.confirm = static_cast<ConfirmLook>(confirmMode);
            changed = true;
        }
        const char* holds[] = {tr(app, "Their color", "De su color"), tr(app, "Lighter", "Más claros")};
        int holdMode = static_cast<int>(c.recipe.holds);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "Holds", "Sostenidos"));
        ImGui::SameLine(120.0f);
        if (ui::segmented("hudholds", &holdMode, holds, 2)) {
            c.recipe.holds = static_cast<HoldLook>(holdMode);
            changed = true;
        }
        changed |= ImGui::Checkbox(tr(app, "Paint the splashes too", "Pintar también las salpicaduras"), &c.recipe.splashes);
        ImGui::EndGroup();
        tutorialMark("hud-details");
        if (changed) tutorialSignal("hud-details");
    } else {
        // Las piezas: las del estilo o dibujadas por ti. Elegir «Dibujadas por
        // mi» no abre nada solo: cada pieza tiene su tarjeta.
        ui::sectionHeader(tr(app, "Pieces", "Piezas"), ui::icon::Brush);
        const char* shapes[] = {tr(app, "The style's", "Las del estilo"), tr(app, "Drawn by me", "Dibujadas por mí")};
        int shape = c.useDrawn ? 1 : 0;
        if (ui::segmented("hudshape", &shape, shapes, 2)) {
            c.useDrawn = shape == 1;
            ++c.drawnVersion;
            tutorialSignal("hud-shape");
        }
        tutorialMark("hud-shape");
        if (!c.useDrawn) {
            ui::caption(tr(app, "Its shapes are the starting style's, with your colors. «Drawn by me» lets you draw any piece: the note, receptors, holds or splashes.",
                                "Sus formas son las del estilo de partida, con tus colores. «Dibujadas por mí» deja dibujar cualquier pieza: nota, receptores, sostenidos o salpicaduras."));
        } else {
            if (ui::flatButton(ui::label(ui::icon::Grid, tr(app, "General sheet…", "Hoja general…")),
                               tr(app, "Every piece in one sheet: each row a piece, each box a frame", "Todas las piezas en una hoja: cada fila una pieza, cada hueco un fotograma")))
                openHudSpriteEditor(app, DrawnPiece::Note, false);
            tutorialMark("hud-sheet");
            ImGui::SameLine(0.0f, 6.0f);
            if (ui::flatButton(ui::label(ui::icon::Photo, tr(app, "Final PNG…", "PNG final…")), tr(app, "The file with everything together, as it will be saved",
                                                                                                "El archivo con todo junto, como se guardará"))) {
                openHudSpriteEditor(app, DrawnPiece::Note, false);
                app.spriteEditor.choosing = false;
                app.spriteEditor.showFinal = true;
            }
            // Las miniaturas de lo dibujado, al cambiar.
            if (c.thumbsFor != c.drawnVersion && app.rendererReady) {
                for (int p = 0; p < kDrawnPieceCount; ++p) {
                    const auto& frames = c.drawn[static_cast<size_t>(p)];
                    if (frames.empty()) continue;
                    const std::string path = "notelab-live/hud/piece-" + std::to_string(p) + ".png";
                    uploadLiveImage(app, path, frames.front());
                    if (std::find(c.live.uploaded.begin(), c.live.uploaded.end(), path) == c.live.uploaded.end()) c.live.uploaded.push_back(path);
                }
                c.thumbsFor = c.drawnVersion;
            }
            const NoteStyle* shownStyle = c.live.painted >= 0 ? &c.live.style
                                        : c.base >= 0 && c.base < static_cast<int>(source.catalog.styles.size()) ? &source.catalog.styles[static_cast<size_t>(c.base)]
                                                                                                                  : nullptr;
            // Cada pieza, una tarjeta: su miniatura, si es dibujada o del estilo, y un clic para dibujarla.
            const float gap = 8.0f;
            const float cardW = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f, cardH = 112.0f;
            static const DrawnPiece order[kDrawnPieceCount] = {DrawnPiece::Note, DrawnPiece::Strum, DrawnPiece::StrumPress, DrawnPiece::StrumConfirm,
                                                               DrawnPiece::HoldPiece, DrawnPiece::HoldEnd, DrawnPiece::Splash};
            int dropPiece = -1;
            for (int k = 0; k < kDrawnPieceCount; ++k) {
                const DrawnPiece piece = order[k];
                const int p = static_cast<int>(piece);
                const auto& frames = c.drawn[static_cast<size_t>(p)];
                ImGui::PushID(p);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const bool pressed = ImGui::InvisibleButton("piece", ImVec2(cardW, cardH));
                const bool hovered = ImGui::IsItemHovered();
                ImDrawList* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(at, ImVec2(at.x + cardW, at.y + cardH), hovered ? IM_COL32(255, 255, 255, 20) : IM_COL32(255, 255, 255, 8), 8.0f);
                draw->AddRect(at, ImVec2(at.x + cardW, at.y + cardH), frames.empty() ? ui::color::Border : ui::withAlpha(ui::color::Success, 160), 8.0f, 0,
                              frames.empty() ? 1.0f : 1.5f);
                const ImVec2 thumbA(at.x + (cardW - 58.0f) * 0.5f, at.y + 8.0f), thumbB(thumbA.x + 58.0f, thumbA.y + 58.0f);
                drawChecker(draw, thumbA, thumbB);
                bool shown = false;
                if (!frames.empty()) {
                    const GlRenderer::PreviewImage image = app.renderer.previewImage("notelab-live/hud/piece-" + std::to_string(p) + ".png");
                    if (image.ok) {
                        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(image.texture)), thumbA, thumbB);
                        shown = true;
                    }
                }
                if (!shown && shownStyle) {
                    const Part part = piece == DrawnPiece::Note ? Part::Note : piece == DrawnPiece::Strum ? Part::StrumStatic
                                    : piece == DrawnPiece::StrumPress ? Part::StrumPress : piece == DrawnPiece::StrumConfirm ? Part::StrumConfirm
                                    : piece == DrawnPiece::HoldPiece ? Part::HoldPiece : piece == DrawnPiece::HoldEnd ? Part::HoldEnd : Part::Splash;
                    for (const PartBinding& b : shownStyle->parts) {
                        if (b.part != part || b.direction != 0 || b.variant != 0) continue;
                        Thumb thumb;
                        if (partThumb(app, *shownStyle, b, -1.0, thumb)) {
                            ImGui::PushClipRect(thumbA, thumbB, true);
                            drawThumbAt(draw, thumb, thumbA, 58.0f, 3.0f);
                            ImGui::PopClipRect();
                        }
                        break;
                    }
                }
                const char* name = drawnPieceName(app, piece);
                const ImVec2 nameSize = ImGui::CalcTextSize(name);
                const float k2 = std::min(1.0f, (cardW - 10.0f) / std::max(1.0f, nameSize.x));
                draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * k2, ImVec2(at.x + (cardW - nameSize.x * k2) * 0.5f, at.y + 70.0f), ui::color::Text, name);
                const bool strumDrawn = !c.drawn[static_cast<size_t>(DrawnPiece::Strum)].empty() || (c.recipe.drawnStrums && !c.drawn[0].empty());
                const bool fromNote = piece == DrawnPiece::Strum && frames.empty() && c.recipe.drawnStrums && !c.drawn[0].empty();
                const bool fromStrum = (piece == DrawnPiece::StrumPress || piece == DrawnPiece::StrumConfirm) && frames.empty() && strumDrawn;
                const std::string status = !frames.empty() ? std::string(ui::icon::Check) + " " + std::to_string(frames.size()) +
                                                                 (app.spanish ? (frames.size() == 1 ? " fotograma" : " fotogramas") : (frames.size() == 1 ? " frame" : " frames"))
                                         : fromNote ? tr(app, "the note's shape", "forma de la nota")
                                         : fromStrum ? tr(app, "from the receptor", "sale del receptor")
                                         : hovered ? tr(app, "Click to draw it", "Clic para dibujarla") : tr(app, "the style's", "la del estilo");
                const ImVec2 statusSize = ImGui::CalcTextSize(status.c_str());
                const float k3 = std::min(0.86f, (cardW - 10.0f) / std::max(1.0f, statusSize.x));
                draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * k3, ImVec2(at.x + (cardW - statusSize.x * k3) * 0.5f, at.y + 90.0f),
                              !frames.empty() ? ui::color::Success : hovered ? ui::color::Accent : ui::color::Faint, status.c_str());
                ui::tooltip(frames.empty() ? tr(app, "Draw this piece", "Dibujar esta pieza") : tr(app, "Edit this piece", "Editar esta pieza"));
                tutorialMark(("hud-piece-" + std::to_string(p)).c_str());
                if (pressed) {
                    openHudSpriteEditor(app, piece, true);
                    // Lo ya dibujado se edita directamente; lo nuevo pregunta como empezar.
                    if (!frames.empty()) app.spriteEditor.choosing = false;
                }
                if (!frames.empty()) {
                    // Volver a la del estilo: una x pequena en la esquina.
                    const ImVec2 x(at.x + cardW - 22.0f, at.y + 4.0f);
                    ImGui::SetCursorScreenPos(x);
                    if (ImGui::InvisibleButton("drop", ImVec2(18.0f, 18.0f))) dropPiece = p;
                    draw->AddText(ImVec2(x.x + 3.0f, x.y + 1.0f), ImGui::IsItemHovered() ? ui::color::Error : ui::color::Faint, ui::icon::Close);
                    ui::tooltip(tr(app, "Back to the style's", "Volver a la del estilo"));
                    ImGui::SetCursorScreenPos(ImVec2(at.x + cardW, at.y));
                    ImGui::Dummy(ImVec2(0.0f, cardH));
                }
                ImGui::PopID();
                if (k % 3 != 2 && k + 1 < kDrawnPieceCount) ImGui::SameLine(0.0f, gap);
            }
            if (dropPiece >= 0) {
                c.drawn[static_cast<size_t>(dropPiece)].clear();
                app.spriteEditor.drawn.erase(c.layersKey);   // la hoja se rehace con lo que queda
                ++c.drawnVersion;
            }
            bool shapeChanged = ImGui::Checkbox(tr(app, "Tint them with each direction's color", "Teñirlas con el color de cada dirección"), &c.recipe.drawnTint);
            if (c.drawn[static_cast<size_t>(DrawnPiece::Strum)].empty())
                shapeChanged |= ImGui::Checkbox(tr(app, "Receptors with the note's shape", "Receptores con la forma de la nota"), &c.recipe.drawnStrums);
            if (shapeChanged) ++c.drawnVersion;
            ui::caption(tr(app, "Note and receptor are drawn looking left (←): the others are rotated. What you don't draw is the style's, with your colors.",
                                "Nota y receptor se dibujan mirando a la izquierda (←): las demás salen giradas. Lo que no dibujes es del estilo, con tus colores."));
        }
    }
    // Abajo: atras y siguiente.
    {
        const float bottom = ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - ImGui::GetStyle().WindowPadding.y - 2.0f;
        if (ImGui::GetCursorPosY() < bottom) ImGui::SetCursorPosY(bottom);
        static const char* previous = "\xEE\x9D\xAB";   // U+E76B, ChevronLeft
        static const char* next = "\xEE\x9D\xAC";       // U+E76C, ChevronRight
        const char* names[4] = {tr(app, "Base", "Base"), tr(app, "Colors", "Colores"), tr(app, "Details", "Detalles"), tr(app, "Pieces", "Piezas")};
        if (c.step > 0) {
            if (ImGui::Button(ui::label(previous, tr(app, "Back", "Atrás")).c_str())) c.step = std::max(0, c.step - 1);
        } else {
            ImGui::Dummy(ImVec2(1.0f, ImGui::GetFrameHeight()));
        }
        if (c.step < 3) {
            const std::string label = std::string(tr(app, "Next: ", "Siguiente: ")) + names[c.step + 1] + "  " + next;
            const float width = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - width);
            if (ImGui::Button(label.c_str())) c.step = std::min(3, c.step + 1);
            tutorialMark("hud-next");
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("hudright", ImVec2(0.0f, -48.0f));
    if (changed) {
        c.live.generation++;
        c.live.changedAt = ImGui::GetTime();
    }
    if (c.base >= 0 && c.base < static_cast<int>(source.catalog.styles.size()) && app.rendererReady) {
        const NoteStyle& base = source.catalog.styles[static_cast<size_t>(c.base)];
        const ArrowRecipe recipe = c.recipe;
        pumpLive(app, c.live, source, base, [recipe](Part part, int d) { return arrowPaint(recipe, part, d); }, "hud");
        const NoteStyle& painted = c.live.painted >= 0 ? c.live.style : base;
        // Lo dibujado, como una hoja mas: se rehace cuando cambian los colores
        // (un momento despues, como lo pintado) o el dibujo.
        NoteStyle withDrawn;
        const bool drawnOn = c.useDrawn && drawnAny(c.drawn);
        if (drawnOn && (c.drawnFor != c.live.generation || c.drawnVersionFor != c.drawnVersion) &&
            (c.drawnFor < 0 || c.drawnVersionFor != c.drawnVersion || ImGui::GetTime() - c.live.changedAt >= 0.18)) {
            const DrawnSheets sheets = drawnPieceSheets(c.drawn, c.recipe);
            static const char* paths[3] = {"notelab-live/hud/drawn-arrows.png", "notelab-live/hud/drawn-holds.png", "notelab-live/hud/drawn-splashes.png"};
            const Image* images[3] = {&sheets.arrows, &sheets.holds, &sheets.splashes};
            for (size_t i = 0; i < 3; ++i) {
                c.drawnLive[i].clear();
                if (images[i]->empty()) continue;
                uploadLiveImage(app, paths[i], *images[i]);
                if (std::find(c.live.uploaded.begin(), c.live.uploaded.end(), paths[i]) == c.live.uploaded.end()) c.live.uploaded.push_back(paths[i]);
                c.drawnLive[i] = paths[i];
            }
            c.drawnFor = c.live.generation;
            c.drawnVersionFor = c.drawnVersion;
        }
        if (drawnOn && c.drawnFor >= 0) {
            withDrawn = painted;
            bindDrawnPieces(withDrawn, c.drawn, c.recipe, c.drawnLive, drawnArrowScale(base));
        }
        const NoteStyle& shown = drawnOn && c.drawnFor >= 0 ? withDrawn : painted;
        drawMiniLane(app, shown, ImVec2(ImGui::GetContentRegionAvail().x, 300.0f));
        if (c.live.job.valid())
            ui::caption(tr(app, "Painting…", "Pintando…"));
        else
            ui::caption(tr(app, "It moves like in the game. The colors are painted into the images, so the three engines show the same.",
                                "Se mueve como en el juego. Los colores se pintan en las imágenes: los tres motores enseñan lo mismo."));
        drawPieceRows(app, shown, {Part::Note, Part::HoldPiece, Part::StrumStatic, Part::StrumPress, Part::StrumConfirm, Part::Splash}, 44.0f);
    }
    ImGui::EndChild();
    ImGui::Separator();
    ui::caption(c.editStyle >= 0 ? tr(app, "Updates this project style in place; existing ranking assets are kept.", "Actualiza este estilo del proyecto; conserva las imágenes del ranking.")
        : tr(app, "It's created as a new style of the project: the mod is not touched. Then edit it or export it.",
                  "Se crea como estilo nuevo del proyecto: el mod no se toca. Después edítalo o expórtalo."));
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 250.0f);
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow()) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const bool ready = c.base >= 0 && c.live.painted == c.live.generation && !c.live.job.valid();
    ImGui::BeginDisabled(!ready);
    const bool createPressed = ui::primaryButton(ui::label(ui::icon::Check, c.editStyle >= 0 ? tr(app, "Apply changes", "Aplicar cambios") : tr(app, "Create HUD", "Crear HUD")), ImVec2(130.0f, 0.0f));
    tutorialMark("hud-create");
    if (createPressed || (ready && app.autoCommit)) {
        app.autoCommit = false;
        createHudNow(app);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    drawSpriteEditor(app, 1);
    ImGui::EndPopup();
}

// ------------------------------------------- aspecto de una nota custom --

void applyTypeLooks(Source& source) {
    for (const auto& [type, styleId] : source.typeLooks)
        for (NoteTypeEntry& entry : source.noteTypes)
            if (lowerText(entry.name) == lowerText(type)) entry.lookStyle = styleId;
}

void openTypeLook(NoteLabApp& app, int sourceIndex, const std::string& type) {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size()) || type.empty()) return;
    auto& t = app.typeLook;
    dropLive(app, t.live);
    t.source = sourceIndex;
    t.type = type;
    const Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    const std::vector<int> styles = paintableStyles(source);
    // De partida, el skin del mod (el de todo el mod) o el elegido.
    t.base = styles.empty() ? -1 : styles.front();
    for (int i : styles)
        if (source.catalog.styles[static_cast<size_t>(i)].use == StyleUse::Default && !source.fromBase[static_cast<size_t>(i)]) {
            t.base = i;
            break;
        }
    if (app.selSource == sourceIndex && std::find(styles.begin(), styles.end(), app.selStyle) != styles.end()) t.base = app.selStyle;
    typeLookPreset("poison", t.recipe);
    t.preset = "poison";
    t.editStyle = -1;
    const auto own = source.typeLooks.find(type);
    if (own != source.typeLooks.end()) {
        const auto saved = source.recipes.find("look:" + own->second);
        if (saved != source.recipes.end()) {
            t.recipe = saved->second.look;
            t.mode = 0;
            t.base = -1;
            for (size_t i = 0; i < source.catalog.styles.size(); ++i) {
                if (source.catalog.styles[i].id == saved->second.baseStyle) t.base = static_cast<int>(i);
                if (source.catalog.styles[i].id == own->second) t.editStyle = static_cast<int>(i);
            }
        }
    }
    t.live.generation++;
    t.live.changedAt = -10.0;
    t.message.clear();
    t.requestOpen = true;
}

const Image* typeLookMark(NoteLabApp& app) {
    auto& t = app.typeLook;
    if (t.recipe.mark < 0) return nullptr;
    if (!t.markImage || t.markFor != t.recipe.mark) {
        if (app.symbolFont.empty()) {
            std::ifstream in(fs::path("C:/Windows/Fonts/seguisym.ttf"), std::ios::binary);
            app.symbolFont.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        t.markImage = std::make_shared<Image>(builtinMarkImage(static_cast<BuiltinMark>(t.recipe.mark), 128, app.symbolFont));
        t.markFor = t.recipe.mark;
    }
    return t.markImage.get();
}

void typeLookFromPaint(NoteLabApp& app) {
    auto& t = app.typeLook;
    Source& source = *app.sources[static_cast<size_t>(t.source)];
    if (t.base < 0 || t.base >= static_cast<int>(source.catalog.styles.size()) || t.live.painted != t.live.generation) return;
    NoteStyle style = source.catalog.styles[static_cast<size_t>(t.base)];
    const std::string baseId = style.id;
    style.id = "notelab:look:" + t.type + ":" + std::to_string(++app.lookSerial);
    if (t.editStyle >= 0 && t.editStyle < static_cast<int>(source.catalog.styles.size())) style.id = source.catalog.styles[static_cast<size_t>(t.editStyle)].id;
    style.name = t.type;
    style.use = StyleUse::NoteType;
    style.useDetail = t.type;
    style.referenced = true;
    style.hasHud = false;
    // Un tipo de nota no trae receptores ni coberturas: solo nota, sostenido y salpicadura.
    style.parts.erase(std::remove_if(style.parts.begin(), style.parts.end(), [](const PartBinding& b) {
        return b.part != Part::Note && b.part != Part::HoldPiece && b.part != Part::HoldEnd && b.part != Part::Splash;
    }), style.parts.end());
    if (!commitPainted(app, source, t.live.images, style, t.type)) {
        setStatus(app, "The painted sheets could not be saved.", "No se pudieron guardar las hojas pintadas.");
        return;
    }
    if (!t.live.images.empty()) style.rgbPalette = false;
    const std::string id = style.id;
    if (t.editStyle >= 0 && t.editStyle < static_cast<int>(source.catalog.styles.size())) {
        selectStyle(app, t.source, t.editStyle); beginEdit(app);
        source.catalog.styles[static_cast<size_t>(t.editStyle)] = std::move(style); afterEdit(app);
    } else addCreatedStyle(app, t.source, std::move(style));
    CreationRecipe recipe;
    recipe.styleId = id; recipe.baseStyle = baseId; recipe.kind = "look"; recipe.look = t.recipe;
    source.recipes["look:" + id] = std::move(recipe);
    dropLive(app, t.live);
    source.typeLooks[t.type] = id;
    applyTypeLooks(source);
    setStatus(app, "Look of " + t.type + " ready: it shows in the preview and goes with the type when you export it.",
              "Aspecto de " + t.type + " listo: se ve en la vista previa y va con el tipo al exportarlo.");
}

// Lo importado para el aspecto de un tipo: una opcion por pieza y direccion.
struct LookChoice { int item = -1, animation = -1; };

std::vector<std::pair<LookChoice, std::string>> lookChoices(const NoteLabApp& app) {
    std::vector<std::pair<LookChoice, std::string>> out;
    const auto& t = app.typeLook;
    for (size_t i = 0; i < t.items.size(); ++i) {
        const ImportItem& item = t.items[i];
        if (item.kind == ImportKind::Image) {
            out.push_back({{static_cast<int>(i), 0}, item.label});
        } else if (item.kind == ImportKind::Atlas || item.kind == ImportKind::Frames || item.kind == ImportKind::Gif) {
            for (size_t a = 0; a < item.animations.size(); ++a)
                out.push_back({{static_cast<int>(i), static_cast<int>(a)},
                               item.label + " · " + item.animations[a].name + " (" + std::to_string(item.animations[a].frames) +
                                   tr(app, " frames)", " fot.)")});
        }
    }
    return out;
}

// Lo que se reconoce por los nombres va solo a su sitio.
void autoPickLook(NoteLabApp& app) {
    auto& t = app.typeLook;
    for (auto& row : t.pick) row = {-1, -1, -1};
    int images = 0;
    LookChoice firstImage;
    for (size_t i = 0; i < t.items.size(); ++i) {
        const ImportItem& item = t.items[i];
        auto place = [&](const PieceGuess& guess, int animation) {
            if (!guess.found) return;
            int slot = guess.part == Part::Note ? 0 : guess.part == Part::HoldPiece ? 1 : guess.part == Part::HoldEnd ? 2 : -1;
            if (slot < 0) return;
            t.pick[static_cast<size_t>(guess.direction)][static_cast<size_t>(slot)] = static_cast<int>(i) * 1000 + animation;
        };
        if (item.kind == ImportKind::Image) {
            place(item.piece, 0);
            if (!item.piece.found) {
                ++images;
                if (firstImage.item < 0) firstImage = {static_cast<int>(i), 0};
            }
        }
        for (size_t a = 0; a < item.animations.size(); ++a) place(item.animations[a].piece, static_cast<int>(a));
    }
    bool anyNote = false;
    for (const auto& row : t.pick) anyNote = anyNote || row[0] >= 0;
    // Una sola imagen sin nombre de pieza: es la nota, para las cuatro.
    t.sameForAll = !anyNote;
    if (!anyNote && firstImage.item >= 0) t.pick[0][0] = firstImage.item * 1000 + firstImage.animation;
    else if (!anyNote)
        for (size_t i = 0; i < t.items.size(); ++i)
            if (!t.items[i].animations.empty()) {
                t.pick[0][0] = static_cast<int>(i) * 1000;
                break;
            }
}

void addLookFiles(NoteLabApp& app, const std::vector<fs::path>& files) {
    auto& t = app.typeLook;
    std::vector<ImportItem> found = scanImport(files);
    int added = 0;
    for (ImportItem& item : found) {
        if (item.kind == ImportKind::Sound || item.kind == ImportKind::Font || item.kind == ImportKind::Unknown) continue;
        t.items.push_back(std::move(item));
        ++added;
    }
    autoPickLook(app);
    t.mode = 1;
    t.message = added > 0 ? std::string() : std::string(tr(app, "Nothing there is an image, a sheet, a folder of frames or a GIF.",
                                                               "Ahí no hay ninguna imagen, hoja, carpeta de fotogramas ni GIF."));
}

std::vector<Image> lookFramesOf(const NoteLabApp& app, int code) {
    const auto& t = app.typeLook;
    if (code < 0) return {};
    const int item = code / 1000, animation = code % 1000;
    if (item < 0 || item >= static_cast<int>(t.items.size())) return {};
    return importFrames(t.items[static_cast<size_t>(item)], animation);
}

// Unas imagenes como aspecto de un tipo: una hoja con los nombres que buscan
// los motores, guardada con lo creado y montada en el mod (sin escribir en el
// mod), y su estilo de tipo. Devuelve el id del estilo; vacio si no se pudo.
std::string commitTypeLookFrames(NoteLabApp& app, int sourceIndex, const std::string& type, const TypeLookFrames& frames,
                                 std::string& message, int* packed = nullptr) {
    Source& source = *app.sources[static_cast<size_t>(sourceIndex)];
    const std::string stem = exportName(type);
    const TypeLookSheet sheet = buildTypeLookSheet(frames, stem + ".png");
    if (!sheet.ok) {
        message = tr(app, "Pick at least the note image.", "Elige al menos la imagen de la nota.");
        return {};
    }
    const std::string png = saveCreatedBytes(sheet.atlas.png, stem + ".png");
    const std::string xmlText = sheet.atlas.xml;
    const std::string xml = saveCreatedBytes(std::vector<unsigned char>(xmlText.begin(), xmlText.end()), stem + ".xml");
    if (png.empty() || xml.empty()) {
        message = tr(app, "The images could not be saved.", "No se pudieron guardar las imágenes.");
        return {};
    }
    const std::string image = mountCreated(source, png, stem + ".png", xml, stem + ".xml");
    if (image.empty()) return {};
    const std::string atlas = source.importedFiles.back().atlasVirtual;
    Engine engine = Engine::Codename;
    sourceEngine(app, source, engine);
    NoteStyle style;
    style.engine = engine;
    style.id = "notelab:look:" + type + ":" + std::to_string(++app.lookSerial);
    style.name = type;
    style.definition = atlas;
    style.use = StyleUse::NoteType;
    style.useDetail = type;
    Sheet s;
    s.kind = SheetKind::Sparrow;
    s.image = image;
    s.atlas = atlas;
    s.declared = "notelab/" + stem;
    // La escala de las notas del skin del mod, para que salga del mismo tamano.
    s.scale = 0.7f;
    for (const NoteStyle& other : source.catalog.styles)
        if (other.use == StyleUse::Default && !other.sheets.empty()) {
            s.scale = other.sheets[0].scale;
            break;
        }
    style.sheets.push_back(s);
    style.parts = sheet.parts;
    const std::string id = style.id;
    addCreatedStyle(app, sourceIndex, std::move(style));
    source.typeLooks[type] = id;
    applyTypeLooks(source);
    if (packed) *packed = sheet.atlas.frames;
    return id;
}

void typeLookFromImport(NoteLabApp& app) {
    auto& t = app.typeLook;
    TypeLookFrames frames;
    for (int d = 0; d < 4; ++d) {
        const auto& row = t.pick[static_cast<size_t>(t.sameForAll ? 0 : d)];
        auto take = [&](int slot) {
            std::vector<Image> list = lookFramesOf(app, row[static_cast<size_t>(slot)]);
            // La misma para las cuatro: la nota se gira si es una flecha.
            if (t.sameForAll && t.rotate && slot == 0)
                for (Image& image : list) image = rotateForDirection(image, d);
            return list;
        };
        frames.note[static_cast<size_t>(d)] = take(0);
        frames.holdPiece[static_cast<size_t>(d)] = take(1);
        frames.holdEnd[static_cast<size_t>(d)] = take(2);
    }
    int packed = 0;
    if (commitTypeLookFrames(app, t.source, t.type, frames, t.message, &packed).empty()) return;
    setStatus(app, "Look of " + t.type + " imported: " + std::to_string(packed) + " frames packed with the names the engines look for.",
              "Aspecto de " + t.type + " importado: " + std::to_string(packed) + " fotogramas empaquetados con los nombres que buscan los motores.");
}

void drawTypeLookModal(NoteLabApp& app, SDL_Window* window) {
    auto& t = app.typeLook;
    if (t.requestOpen) {
        ImGui::OpenPopup("###typelook");
        t.requestOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(1080.0f, 700.0f), ImGuiCond_Appearing);
    bool open = true;
    const std::string title = ui::label(ui::icon::Brush, std::string(tr(app, "Look of ", "Aspecto de ")) + t.type) + "###typelook";
    t.isOpen = ImGui::BeginPopupModal(title.c_str(), &open);
    if (!t.isOpen) {
        if (t.live.job.valid() || !t.live.uploaded.empty()) {
            if (t.live.job.valid()) t.live.job.wait();
            t.live.job = {};
            dropLive(app, t.live);
        }
        return;
    }
    if (t.source < 0 || t.source >= static_cast<int>(app.sources.size())) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    Source& source = *app.sources[static_cast<size_t>(t.source)];
    helpButton(app, "typelookhelp", 2);
    ImGui::SameLine();
    const char* modes[] = {tr(app, "Your images", "Tus imágenes"), tr(app, "Paint a skin", "Pintar un skin")};
    int mode = t.mode == 1 ? 0 : 1;
    if (ui::segmented("lookmode", &mode, modes, 2)) t.mode = mode == 0 ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::Button(ui::label(ui::icon::Edit, tr(app, "Draw my sprite…", "Dibujar mi sprite…")).c_str())) {
        const std::string type = t.type;
        const int source = t.source;
        ImGui::CloseCurrentPopup();
        openSpriteEditor(app, source, type);
    }
    ui::tooltip(tr(app, "A small drawing editor: layers, brushes, shapes, fill.", "Un editor de dibujo pequeño: capas, pinceles, figuras, relleno."));
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Advanced…", "Avanzado…"))) {
        const std::string type = t.type;
        const int source = t.source;
        ImGui::CloseCurrentPopup(); app.selSource = source; openCustomCreator(app, false, type);
        std::vector<fs::path> files;
        for (const auto& item : t.items) files.push_back(item.path);
        if (!files.empty()) addCustomFiles(app, files);
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::caption(t.mode == 1 ? tr(app, "Your sheet (PNG + XML), PNG images, a folder of frames or a GIF. You can also drop them on the window.",
                                      "Tu hoja (PNG + XML), imágenes PNG, una carpeta de fotogramas o un GIF. También puedes soltarlos en la ventana.")
                            : tr(app, "Start from a skin and change its color, put a mark on it or make it see-through.",
                                      "Parte de un skin y cámbiale el color, ponle una marca o hazlo transparente."));
    bool changed = false;
    ImGui::BeginChild("lookleft", ImVec2(500.0f, -48.0f));
    if (t.mode == 1) {
        ui::sectionHeader(tr(app, "Files", "Archivos"), ui::icon::FolderOpen);
        if (ImGui::Button(ui::label(ui::icon::Add, tr(app, "Add files…", "Añadir archivos…")).c_str())) openDialog(app, window, DialogAction::LookFiles);
        ImGui::SameLine();
        if (ImGui::Button(ui::label(ui::icon::FolderOpen, tr(app, "Add a folder…", "Añadir una carpeta…")).c_str())) openDialog(app, window, DialogAction::LookFolder);
        if (!t.items.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(ui::label(ui::icon::Delete, tr(app, "Clear", "Vaciar")).c_str())) {
                t.items.clear();
                for (auto& row : t.pick) row = {-1, -1, -1};
            }
        }
        for (const ImportItem& item : t.items) {
            const char* kind = item.kind == ImportKind::Atlas ? tr(app, "sheet", "hoja") : item.kind == ImportKind::Frames ? tr(app, "frames", "fotogramas")
                             : item.kind == ImportKind::Gif ? "GIF" : tr(app, "image", "imagen");
            ImGui::BulletText("%s  ·  %s", item.label.c_str(), kind);
        }
        if (!t.items.empty()) {
            ui::sectionHeader(tr(app, "What goes where", "Qué va en cada sitio"), ui::icon::Grid);
            ImGui::Checkbox(tr(app, "The same image for the four directions", "La misma imagen para las cuatro direcciones"), &t.sameForAll);
            if (t.sameForAll) {
                ImGui::SameLine();
                ImGui::Checkbox(tr(app, "It's an arrow ←: rotate it", "Es una flecha ←: girarla"), &t.rotate);
            }
            const auto choices = lookChoices(app);
            const char* slotNames[3] = {tr(app, "Note", "Nota"), tr(app, "Hold piece", "Tramo"), tr(app, "Hold end", "Final")};
            static const char* arrows[4] = {"←", "↓", "↑", "→"};
            const int rows = t.sameForAll ? 1 : 4;
            for (int d = 0; d < rows; ++d)
                for (int slot = 0; slot < 3; ++slot) {
                    ImGui::PushID(d * 10 + slot);
                    ImGui::AlignTextToFramePadding();
                    if (t.sameForAll) ImGui::TextUnformatted(slotNames[slot]);
                    else ImGui::Text("%s %s", slotNames[slot], arrows[d]);
                    ImGui::SameLine(110.0f);
                    int& value = t.pick[static_cast<size_t>(d)][static_cast<size_t>(slot)];
                    std::string shown = tr(app, "— none —", "— ninguna —");
                    for (const auto& choice : choices)
                        if (choice.first.item * 1000 + choice.first.animation == value) shown = choice.second;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##pick", shown.c_str())) {
                        if (ImGui::Selectable(tr(app, "— none —", "— ninguna —"), value < 0)) value = -1;
                        for (const auto& choice : choices) {
                            const int code = choice.first.item * 1000 + choice.first.animation;
                            if (ImGui::Selectable(choice.second.c_str(), code == value)) value = code;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
            ui::caption(tr(app, "Frames named like the base game (purple0, left press...) or after the direction find their place by themselves.",
                                "Los fotogramas con nombres del juego base (purple0, left press...) o de la dirección van solos a su sitio."));
        }
    } else {
        const std::vector<int> styles = paintableStyles(source);
        ui::sectionHeader(tr(app, "Starting point", "Punto de partida"), ui::icon::Layers);
        ImGui::SetNextItemWidth(-1.0f);
        const std::string current = t.base >= 0 && t.base < static_cast<int>(source.catalog.styles.size())
                                        ? source.catalog.styles[static_cast<size_t>(t.base)].name : std::string("-");
        if (ImGui::BeginCombo("##lookbase", current.c_str())) {
            for (int i : styles) {
                const NoteStyle& s = source.catalog.styles[static_cast<size_t>(i)];
                if (ImGui::Selectable((s.name + "  ·  " + useLabel(app, s.use) + "##" + s.id).c_str(), i == t.base)) {
                    t.base = i;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ui::sectionHeader(tr(app, "Presets", "Presets"), ui::icon::Palette);
        for (const std::string& key : typeLookPresetKeys()) {
            if (ImGui::Button((std::string(lookPresetLabel(app, key)) + "##l" + key).c_str())) {
                typeLookPreset(key, t.recipe);
                t.preset = key;
                changed = true;
            }
            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::GetContentRegionAvail().x < 90.0f) ImGui::NewLine();
        }
        ImGui::NewLine();
        ui::sectionHeader(tr(app, "Color", "Color"), ui::icon::Palette);
        const char* colorModes[] = {tr(app, "Keep", "Sin cambio"), tr(app, "One color", "Un color"), tr(app, "By direction", "Por dirección"),
                                    tr(app, "Three colors", "Tres colores")};
        int colorMode = static_cast<int>(t.recipe.color);
        if (ui::segmented("lookcolor", &colorMode, colorModes, 4)) {
            t.recipe.color = static_cast<TypeColor>(colorMode);
            changed = true;
        }
        if (t.recipe.color == TypeColor::One) changed |= colorEdit("##one", t.recipe.one);
        if (t.recipe.color == TypeColor::PerDirection)
            for (int d = 0; d < 4; ++d) {
                ImGui::PushID(d);
                changed |= colorEdit("##dir", t.recipe.perDirection[static_cast<size_t>(d)]);
                ImGui::PopID();
                if (d < 3) ImGui::SameLine(0.0f, 8.0f);
            }
        if (t.recipe.color == TypeColor::Palette) {
            changed |= colorEdit("##dark", t.recipe.palette[0], tr(app, "Dark (outline)", "Oscuro (contorno)"));
            ImGui::SameLine(0.0f, 8.0f);
            changed |= colorEdit("##mid", t.recipe.palette[1], tr(app, "Middle (fill)", "Medio (relleno)"));
            ImGui::SameLine(0.0f, 8.0f);
            changed |= colorEdit("##light", t.recipe.palette[2], tr(app, "Light (shine)", "Claro (brillo)"));
        }
        if (t.recipe.color != TypeColor::Keep) {
            float strength = t.recipe.strength * 100.0f;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##lookstrength", &strength, 10.0f, 100.0f, tr(app, "Strength %.0f %%", "Fuerza %.0f %%"))) {
                t.recipe.strength = strength / 100.0f;
                changed = true;
            }
        }
        ui::sectionHeader(tr(app, "Mark on top", "Marca encima"), ui::icon::Star);
        // Las marcas se dibujan con su propia imagen: la letra de la interfaz
        // no trae esos simbolos.
        if (app.symbolFont.empty()) {
            std::ifstream in(fs::path("C:/Windows/Fonts/seguisym.ttf"), std::ios::binary);
            app.symbolFont.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (!app.markIcons) {
            for (int m = 0; m < kBuiltinMarkCount; ++m)
                uploadLiveImage(app, "notelab-live/mark/" + std::to_string(m) + ".png",
                                builtinMarkImage(static_cast<BuiltinMark>(m), 40, app.symbolFont));
            app.markIcons = true;
        }
        if (ImGui::Button(tr(app, "None", "Ninguna"), ImVec2(0.0f, 30.0f))) {
            t.recipe.mark = -1;
            changed = true;
        }
        static const char* markNamesEn[kBuiltinMarkCount] = {"Skull", "Bolt", "Heart", "Star", "Exclamation", "Dot", "Eye"};
        static const char* markNamesEs[kBuiltinMarkCount] = {"Calavera", "Rayo", "Corazón", "Estrella", "Exclamación", "Punto", "Ojo"};
        for (int m = 0; m < kBuiltinMarkCount; ++m) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::PushID(m);
            const bool on = t.recipe.mark == m;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ui::vec(ui::color::AccentSoft));
            const GlRenderer::PreviewImage icon = app.renderer.previewImage("notelab-live/mark/" + std::to_string(m) + ".png");
            bool pressed = false;
            if (icon.ok && icon.width > 0 && icon.height > 0) {
                const float fit = 22.0f / static_cast<float>(std::max(icon.width, icon.height));
                pressed = ImGui::ImageButton("##mark", ImTextureRef(static_cast<ImTextureID>(icon.texture)),
                                             ImVec2(icon.width * fit, icon.height * fit));
            } else {
                pressed = ImGui::Button(std::to_string(m + 1).c_str(), ImVec2(30.0f, 30.0f));
            }
            ui::tooltip(app.spanish ? markNamesEs[m] : markNamesEn[m]);
            if (pressed) {
                t.recipe.mark = m;
                changed = true;
            }
            if (on) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (t.recipe.mark >= 0) {
            const char* places[] = {tr(app, "Center", "Centro"), tr(app, "Top", "Arriba"), tr(app, "Corner", "Esquina")};
            int place = static_cast<int>(t.recipe.markPlace);
            if (ui::segmented("markplace", &place, places, 3)) {
                t.recipe.markPlace = static_cast<MarkPlace>(place);
                changed = true;
            }
            float size = t.recipe.markSize * 100.0f;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##marksize", &size, 15.0f, 90.0f, tr(app, "Size %.0f %%", "Tamaño %.0f %%"))) {
                t.recipe.markSize = size / 100.0f;
                changed = true;
            }
        }
        ui::sectionHeader(tr(app, "Shape", "Forma"), ui::icon::Settings);
        float alpha = t.recipe.alpha * 100.0f;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##lookalpha", &alpha, 10.0f, 100.0f, tr(app, "Opacity %.0f %%", "Opacidad %.0f %%"))) {
            t.recipe.alpha = alpha / 100.0f;
            changed = true;
        }
        const char* holds[] = {tr(app, "Like the note", "Como la nota"), tr(app, "Unchanged", "Sin cambio"), tr(app, "Hidden", "Oculto")};
        int hold = static_cast<int>(t.recipe.hold);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "Hold", "Sostenido"));
        ImGui::SameLine(110.0f);
        if (ui::segmented("lookhold", &hold, holds, 3)) {
            t.recipe.hold = static_cast<TypeHold>(hold);
            changed = true;
        }
        const char* splashes[] = {tr(app, "Recolored", "Recoloreada"), tr(app, "Unchanged", "Sin cambio")};
        int splash = t.recipe.splash == TypeSplash::Recolored ? 0 : 1;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "Splash", "Salpicadura"));
        ImGui::SameLine(110.0f);
        if (ui::segmented("looksplash", &splash, splashes, 2)) {
            t.recipe.splash = splash == 0 ? TypeSplash::Recolored : TypeSplash::Keep;
            changed = true;
        }
    }
    if (!t.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Warning));
        ImGui::TextWrapped("%s", t.message.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("lookright", ImVec2(0.0f, -48.0f));
    if (changed) {
        t.live.generation++;
        t.live.changedAt = ImGui::GetTime();
    }
    bool ready = false;
    if (t.mode == 0 && t.base >= 0 && t.base < static_cast<int>(source.catalog.styles.size()) && app.rendererReady) {
        const NoteStyle& base = source.catalog.styles[static_cast<size_t>(t.base)];
        const TypeLookRecipe recipe = t.recipe;
        const Image* markPtr = typeLookMark(app);
        std::shared_ptr<Image> mark = markPtr ? t.markImage : nullptr;
        pumpLive(app, t.live, source, base, [recipe, mark](Part part, int d) { return typePaint(recipe, part, d, mark.get()); }, "look");
        const NoteStyle& shown = t.live.painted >= 0 ? t.live.style : base;
        drawMiniLane(app, shown, ImVec2(ImGui::GetContentRegionAvail().x, 300.0f));
        ui::caption(t.live.job.valid() ? tr(app, "Painting…", "Pintando…")
                                        : tr(app, "The color and the mark are painted into the image: they look the same in the three engines.",
                                                  "El color y la marca se pintan en la imagen: se ven igual en los tres motores."));
        drawPieceRows(app, shown, {Part::Note, Part::HoldPiece, Part::HoldEnd, Part::Splash}, 52.0f);
        ready = t.live.painted == t.live.generation && !t.live.job.valid();
    } else if (t.mode == 1) {
        // Lo elegido, en miniatura: las imagenes de la nota de cada direccion.
        ui::sectionHeader(tr(app, "Preview", "Vista"), ui::icon::Eye);
        bool any = false;
        for (int d = 0; d < 4; ++d) {
            const auto& row = t.pick[static_cast<size_t>(t.sameForAll ? 0 : d)];
            std::vector<Image> frames = lookFramesOf(app, row[0]);
            if (frames.empty()) continue;
            Image first = frames.front();
            if (t.sameForAll && t.rotate) first = rotateForDirection(first, d);
            const std::string path = "notelab-live/import/" + std::to_string(d) + ".png";
            if (t.previewKey[static_cast<size_t>(d)] != row[0] * 8 + (t.sameForAll ? 1 : 0) + (t.rotate ? 2 : 0)) {
                uploadLiveImage(app, path, first);
                t.previewKey[static_cast<size_t>(d)] = row[0] * 8 + (t.sameForAll ? 1 : 0) + (t.rotate ? 2 : 0);
                if (std::find(t.live.uploaded.begin(), t.live.uploaded.end(), path) == t.live.uploaded.end()) t.live.uploaded.push_back(path);
            }
            const GlRenderer::PreviewImage image = app.renderer.previewImage(path);
            if (!image.ok) continue;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            drawChecker(ImGui::GetWindowDrawList(), at, ImVec2(at.x + 110.0f, at.y + 110.0f));
            Thumb thumb;
            thumb.texture = image.texture;
            thumb.w = static_cast<float>(image.width);
            thumb.h = static_cast<float>(image.height);
            drawThumbAt(ImGui::GetWindowDrawList(), thumb, at, 110.0f, 6.0f);
            ImGui::Dummy(ImVec2(110.0f, 110.0f));
            if (d < 3) ImGui::SameLine(0.0f, 8.0f);
            any = true;
        }
        if (!any) ui::caption(tr(app, "Add your images on the left.", "Añade tus imágenes a la izquierda."));
        ready = any;
    }
    ImGui::EndChild();
    ImGui::Separator();
    const auto own = source.typeLooks.find(t.type);
    if (own != source.typeLooks.end()) {
        if (ImGui::Button(ui::label(ui::icon::Restart, tr(app, "Back to the mod's look", "Volver al aspecto del mod")).c_str())) {
            const std::string styleId = own->second;
            source.typeLooks.erase(own);
            source.noteTypes = scanNoteTypes(*source.vfs, source.catalog);
            applyTypeLooks(source);
            for (size_t i = 0; i < source.catalog.styles.size(); ++i)
                if (source.catalog.styles[i].id == styleId) {
                    deleteVariant(app, t.source, static_cast<int>(i));
                    break;
                }
            app.dirty = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    ui::caption(tr(app, "Saved with the project; it goes with the type when you export it. The mod is not touched.",
                        "Se guarda con el proyecto y va con el tipo al exportarlo. El mod no se toca."));
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 260.0f);
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow())
        ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ready);
    if (ui::primaryButton(ui::label(ui::icon::Check, tr(app, "Use this look", "Usar este aspecto")), ImVec2(140.0f, 0.0f)) || (ready && app.autoCommit)) {
        app.autoCommit = false;
        // Uno de antes, creado aqui, se sustituye.
        const auto before = source.typeLooks.find(t.type);
        const std::string oldId = before != source.typeLooks.end() ? before->second : std::string();
        if (t.mode == 0) typeLookFromPaint(app);
        else typeLookFromImport(app);
        if (!oldId.empty() && source.typeLooks[t.type] != oldId)
            for (size_t i = 0; i < source.catalog.styles.size(); ++i)
                if (source.catalog.styles[i].id == oldId) {
                    deleteVariant(app, t.source, static_cast<int>(i));
                    break;
                }
        if (t.message.empty()) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

#include "CustomCreator.hpp"
#include "SpriteEditor.hpp"

// ----------------------------------------------------- HUD de ranking nuevo --

std::vector<std::pair<std::string, std::string>> scanSystemFonts() {
    std::vector<std::pair<std::string, std::string>> out;
    std::error_code ec;
    std::set<std::string> seen;
    for (const auto& entry : fs::directory_iterator(fs::path("C:/Windows/Fonts"), ec)) {
        if (ec) break;
        const std::string ext = lowerText(entry.path().extension().u8string());
        if (ext != ".ttf" && ext != ".otf") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string family = fontFamilyName(bytes);
        if (family.empty() || !seen.insert(family).second) continue;
        out.push_back({family, entry.path().u8string()});
    }
    std::sort(out.begin(), out.end());
    return out;
}

void openCreateRating(NoteLabApp& app) {
    if (!selectedStyle(app)) {
        setStatus(app, "Pick a note style first: the ranking goes into its HUD.", "Elige antes un estilo de notas: el ranking va en su HUD.");
        return;
    }
    auto& r = app.createRating;
    r.source = app.selSource;
    r.style = app.selStyle;
    r.recipe = RatingRecipe{};
    const Source& source = *app.sources[static_cast<size_t>(app.selSource)];
    const auto saved = source.recipes.find("rating:" + selectedStyle(app)->id);
    if (saved != source.recipes.end()) {
        r.recipe = saved->second.rating;
        r.fontPath = saved->second.font;
    }
    for (size_t i = 0; i < 5; ++i) std::snprintf(r.texts[i].data(), r.texts[i].size(), "%s", r.recipe.texts[i].c_str());
    r.images.clear();
    for (const std::string& path : r.live) app.renderer.removeDynamicFrame(path);
    r.live.clear();
    r.painted = -1;
    if (!r.fontJob.valid() && r.fonts.empty()) r.fontJob = std::async(std::launch::async, scanSystemFonts);
    if (r.fontPath.empty()) r.fontPath = "C:/Windows/Fonts/seguibl.ttf";
    r.generation++;
    r.changedAt = -10.0;
    r.requestOpen = true;
}

void applyRatingNow(NoteLabApp& app) {
    auto& r = app.createRating;
    if (r.painted != r.generation || r.images.size() != 15 ||
        std::any_of(r.images.begin(), r.images.end(), [](const Image& image) { return image.empty(); })) return;
    if (r.source < 0 || r.source >= static_cast<int>(app.sources.size())) return;
    if (app.selSource != r.source || app.selStyle != r.style) selectStyle(app, r.source, r.style);
    Source& source = *app.sources[static_cast<size_t>(r.source)];
    static const char* names[15] = {"sick", "good", "bad", "shit", "combo", "num0", "num1", "num2", "num3", "num4",
                                    "num5", "num6", "num7", "num8", "num9"};
    std::array<std::string, 15> mounted;
    for (size_t i = 0; i < 15; ++i) {
        const std::string file = saveCreatedBytes(encodePng(r.images[i]), std::string(names[i]) + ".png");
        mounted[i] = file.empty() ? std::string() : mountCreated(source, file, std::string(names[i]) + ".png");
        if (mounted[i].empty()) {
            setStatus(app, "The ranking images could not be saved.", "No se pudieron guardar las imágenes del ranking.");
            return;
        }
    }
    beginEdit(app);
    NoteStyle* style = mutableStyle(app);
    if (!style) return;
    for (int i = 0; i < 15; ++i) {
        HudAsset* asset = hudAssetAt(*style, i);
        if (!asset) continue;
        asset->image = mounted[static_cast<size_t>(i)];
        asset->declared = std::string("notelab/") + names[i];
        asset->imageOptional = false;
        asset->inherited = false;
        asset->nearby.clear();
        asset->scale = 1.0f;
        asset->pixel = r.recipe.pixel;
    }
    style->hasHud = true;
    CreationRecipe recipe;
    recipe.styleId = style->id; recipe.kind = "rating"; recipe.rating = r.recipe; recipe.font = r.fontPath;
    source.recipes["rating:" + style->id] = std::move(recipe);
    afterEdit(app);
    setStatus(app, "Ranking HUD made with text: judgements, combo and digits in " + style->name + ". Undo or go back to the mod's version whenever you want.",
              "HUD de ranking hecho con texto: juicios, combo y cifras en " + style->name + ". Deshazlo o vuelve a la versión del mod cuando quieras.");
}

void drawCreateRatingModal(NoteLabApp& app, SDL_Window* window) {
    auto& r = app.createRating;
    if (r.requestOpen) {
        ImGui::OpenPopup("###createrating");
        r.requestOpen = false;
    }
    if (r.fontJob.valid() && r.fontJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) r.fonts = r.fontJob.get();
    ImGui::SetNextWindowSize(ImVec2(1060.0f, 690.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Star, tr(app, "Create ranking HUD", "Crear HUD de ranking")) + "###createrating").c_str(), &open)) {
        if (!r.live.empty()) {
            if (r.job.valid()) r.job.wait();
            r.job = {};
            for (const std::string& path : r.live) app.renderer.removeDynamicFrame(path);
            r.live.clear();
            r.painted = -1;
        }
        return;
    }
    bool changed = false;
    helpButton(app, "ratinghelp", 3);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::caption(tr(app, "Judgements, combo and digits made with any letter.", "Juicios, combo y cifras hechos con cualquier letra."));
    ImGui::BeginChild("ratingleft", ImVec2(470.0f, -48.0f));
    ui::sectionHeader(tr(app, "Texts", "Textos"), ui::icon::Edit);
    const char* rows[5] = {"Sick!", tr(app, "Good", "Bien"), tr(app, "Bad", "Mal"), tr(app, "Shit", "Pésimo"), tr(app, "Combo", "Rótulo")};
    for (size_t i = 0; i < 5; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(rows[i]);
        ImGui::SameLine(80.0f);
        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::InputText("##text", r.texts[i].data(), r.texts[i].size())) {
            r.recipe.texts[i] = r.texts[i].data();
            changed = true;
        }
        ImGui::SameLine();
        changed |= colorEdit("##top", r.recipe.colors[i][0], tr(app, "Color at the top", "Color de arriba"));
        ImGui::SameLine(0.0f, 4.0f);
        changed |= colorEdit("##bottom", r.recipe.colors[i][1], tr(app, "Color at the bottom", "Color de abajo"));
        ImGui::PopID();
    }
    ui::caption(tr(app, "The digits 0 to 9 use the combo's letter and colors.", "Las cifras del 0 al 9 llevan la letra y los colores del rótulo."));
    ui::sectionHeader(tr(app, "Letter", "Letra"), ui::icon::Font);
    std::string fontName = fs::path(pathFromUtf8(r.fontPath)).stem().u8string();
    for (const auto& font : r.fonts)
        if (font.second == r.fontPath) fontName = font.first;
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("##font", fontName.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (r.fonts.empty()) ImGui::TextDisabled("%s", tr(app, "Reading the fonts…", "Leyendo las letras…"));
        for (const auto& font : r.fonts)
            if (ImGui::Selectable(font.first.c_str(), font.second == r.fontPath)) {
                r.fontPath = font.second;
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui::label(ui::icon::Add, tr(app, "Import .ttf…", "Importar .ttf…")).c_str())) openDialog(app, window, DialogAction::RatingFont);
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##size", &r.recipe.size, 50.0f, 200.0f, tr(app, "Size %.0f px", "Tamaño %.0f px"));
    r.recipe.digitSize = r.recipe.size * 110.0f / 120.0f;
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##tilt", &r.recipe.tiltDeg, -20.0f, 20.0f, tr(app, "Tilt %.0f°", "Inclinación %.0f°"));
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##spacing", &r.recipe.spacing, -10.0f, 30.0f, tr(app, "Spacing %.0f px", "Espaciado %.0f px"));
    ui::sectionHeader(tr(app, "Outline and shadow", "Contorno y sombra"), ui::icon::Palette);
    changed |= colorEdit("##outline", r.recipe.outline);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##outlinew", &r.recipe.outlineWidth, 0.0f, 30.0f, tr(app, "Outline %.0f px", "Contorno %.0f px"));
    changed |= ImGui::Checkbox(tr(app, "Shadow", "Sombra"), &r.recipe.shadow);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(tr(app, "Pixel art (no smoothing)", "Pixel art (sin suavizado)"), &r.recipe.pixel);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("ratingright", ImVec2(0.0f, -48.0f));
    const bool hasText = std::any_of(r.recipe.texts.begin(), r.recipe.texts.end(), [](const std::string& text) {
        return text.find_first_not_of(" \t\r\n") != std::string::npos;
    });
    if (changed) {
        r.generation++;
        r.changedAt = ImGui::GetTime();
    }
    // La letra: se lee al cambiarla; despues se genera en otro hilo.
    if (r.fontLoaded != r.fontPath) {
        std::ifstream in(pathFromUtf8(r.fontPath), std::ios::binary);
        r.fontBytes = std::make_shared<std::vector<unsigned char>>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        r.fontLoaded = r.fontPath;
    }
    if (r.job.valid() && r.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::vector<Image> images = r.job.get();
        if (r.running == r.generation) {
            r.images = std::move(images);
            r.painted = r.running;
            for (size_t i = 0; i < r.images.size(); ++i) {
                const std::string path = "notelab-live/rating/" + std::to_string(i) + ".png";
                uploadLiveImage(app, path, r.images[i]);
                if (std::find(r.live.begin(), r.live.end(), path) == r.live.end()) r.live.push_back(path);
            }
        }
    }
    if (hasText && !r.job.valid() && r.painted != r.generation && ImGui::GetTime() - r.changedAt > 0.12 && r.fontBytes && !r.fontBytes->empty()) {
        r.running = r.generation;
        const RatingRecipe recipe = r.recipe;
        const auto font = r.fontBytes;
        r.job = std::async(std::launch::async, [recipe, font]() { return renderRatingSet(recipe, *font); });
    }
    // Como en el juego: el juicio, el rotulo y un combo de tres cifras.
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = 330.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + w, at.y + h), IM_COL32(11, 13, 17, 255), 8.0f);
    if (!hasText) {
        const char* message = tr(app, "Write your own labels to preview your HUD.", "Escribe tus textos para ver tu HUD.");
        const ImVec2 size = ImGui::CalcTextSize(message);
        draw->AddText(ImVec2(at.x + std::max(12.0f, (w - size.x) * 0.5f), at.y + (h - size.y) * 0.5f), ui::color::Muted, message);
    }
    auto image = [&](size_t index, ImVec2 center, float scale) {
        if (!hasText || r.painted != r.generation || index >= r.images.size() || r.images[index].empty() || index >= r.live.size()) return 0.0f;
        const GlRenderer::PreviewImage tex = app.renderer.previewImage(r.live[index]);
        if (!tex.ok) return 0.0f;
        const float iw = r.images[index].w * scale, ih = r.images[index].h * scale;
        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(tex.texture)), ImVec2(center.x - iw * 0.5f, center.y - ih * 0.5f),
                       ImVec2(center.x + iw * 0.5f, center.y + ih * 0.5f));
        return iw;
    };
    // Codename y Psych dibujan los juicios a 0,7 y las cifras a 0,5 (Codename
    // RatingsShowEvent.hx:23, :31; Psych PlayState.hx:2625-2626, :2650).
    image(0, ImVec2(at.x + w * 0.5f, at.y + h * 0.36f), 0.7f);
    const float digit = 0.5f;
    float x = at.x + w * 0.5f - 150.0f;
    for (int n : {1, 2, 3}) {
        const float dw = image(static_cast<size_t>(5 + n), ImVec2(x, at.y + h * 0.68f), digit);
        x += std::max(30.0f, dw * 0.9f);
    }
    image(4, ImVec2(x + 90.0f, at.y + h * 0.68f), 0.5f);
    ImGui::Dummy(ImVec2(w, h));
    ui::caption(!hasText ? tr(app, "No example is loaded. Text is rendered from the local font; no AI is used.",
                                  "No se carga un ejemplo. El texto se dibuja con la fuente local; no usa IA.")
                        : r.job.valid() ? tr(app, "Drawing…", "Dibujando…")
                              : tr(app, "On screen as in Codename and Psych: judgements at 0.7, digits at 0.5. V-Slice declares its scale.",
                                        "En pantalla como en Codename y Psych: juicios a 0,7 y cifras a 0,5. V-Slice declara su escala."));
    ui::sectionHeader(tr(app, "All the images", "Todas las imágenes"), ui::icon::Photo);
    for (size_t i = 0; hasText && r.painted == r.generation && i < r.images.size() && i < r.live.size(); ++i) {
        const GlRenderer::PreviewImage tex = app.renderer.previewImage(r.live[i]);
        if (!tex.ok || r.images[i].empty()) continue;
        const float scale = std::min(64.0f / r.images[i].h, (i < 4 ? 150.0f : 64.0f) / r.images[i].w);
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(tex.texture)), ImVec2(r.images[i].w * scale, r.images[i].h * scale));
        if (i != 3 && i != 4 && i != 14) ImGui::SameLine(0.0f, 6.0f);
    }
    ImGui::EndChild();
    ImGui::Separator();
    const NoteStyle* target = r.source >= 0 && r.source < static_cast<int>(app.sources.size()) &&
                                      r.style >= 0 && r.style < static_cast<int>(app.sources[static_cast<size_t>(r.source)]->catalog.styles.size())
                                  ? &app.sources[static_cast<size_t>(r.source)]->catalog.styles[static_cast<size_t>(r.style)]
                                  : nullptr;
    ui::caption((std::string(tr(app, "Goes into the HUD of ", "Va en el HUD de ")) + (target ? target->name : std::string("-")) +
                 tr(app, ". The letter becomes images: the engine doesn't need the font.", ". La letra se convierte en imágenes: el motor no necesita la fuente."))
                    .c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 250.0f);
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow())
        ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    const bool ready = r.painted == r.generation && r.images.size() == 15 && !r.job.valid() &&
                       std::none_of(r.images.begin(), r.images.end(), [](const Image& image) { return image.empty(); });
    ImGui::BeginDisabled(!ready || !target);
    if (ui::primaryButton(ui::label(ui::icon::Check, tr(app, "Use in the HUD", "Usar en el HUD")), ImVec2(130.0f, 0.0f)) || (ready && target && app.autoCommit)) {
        app.autoCommit = false;
        applyRatingNow(app);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

// -------------------------------------------------- importar lo soltado --

// Lo que se suelta y no es un mod (imagenes, hojas, GIF, sonidos, fuentes):
// Note Lab pregunta para que es.
bool isAssetFile(const fs::path& path) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) return !looksLikeModFolder(path) && !fs::is_directory(path / "assets", ec) && !fs::is_directory(path / "mods", ec);
    const std::string ext = lowerText(path.extension().u8string());
    return ext == ".png" || ext == ".xml" || ext == ".txt" || ext == ".gif" || ext == ".ttf" || ext == ".otf" ||
           ext == ".ogg" || ext == ".wav" || ext == ".mp3";
}

void openDropImport(NoteLabApp& app) {
    auto& d = app.dropImport;
    d.items = scanImport(d.pending);
    d.files = d.pending;
    d.pending.clear();
    d.hudImages = 0;
    d.fonts = 0;
    for (const ImportItem& item : d.items) {
        if ((item.kind == ImportKind::Image && item.hudIndex >= 0) || (item.kind == ImportKind::Sound && item.hudIndex >= 15)) ++d.hudImages;
        if (item.kind == ImportKind::Font) ++d.fonts;
    }
    d.type[0] = '\0';
    if (const Source* source = selectedSource(app)) {
        for (const NoteTypeEntry& type : source->noteTypes)
            if (!type.builtin) {
                std::snprintf(d.type.data(), d.type.size(), "%s", type.name.c_str());
                break;
            }
        if (d.type[0] == '\0' && !source->typeBlocks.empty())
            std::snprintf(d.type.data(), d.type.size(), "%s", source->typeBlocks.begin()->first.c_str());
    }
    d.requestOpen = true;
}

// Las imagenes y sonidos con nombre del HUD, copiados a la carpeta de Note
// Lab y puestos en el HUD del estilo elegido.
void applyDroppedHud(NoteLabApp& app) {
    auto& d = app.dropImport;
    Source* source = selectedSource(app);
    if (!source || !mutableStyle(app)) return;
    struct Placed { int index; bool sound; std::string path; std::string name; };
    std::vector<Placed> placed;
    for (const ImportItem& item : d.items) {
        const bool image = item.kind == ImportKind::Image && item.hudIndex >= 0;
        const bool sound = item.kind == ImportKind::Sound && item.hudIndex >= 15;
        if (!image && !sound) continue;
        std::ifstream in(item.path, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string name = item.path.filename().u8string();
        const std::string file = saveCreatedBytes(bytes, name);
        if (file.empty()) continue;
        const std::string mounted = mountCreated(*source, file, name);
        if (!mounted.empty()) placed.push_back({item.hudIndex, sound, mounted, item.path.stem().u8string()});
    }
    if (placed.empty()) return;
    beginEdit(app);
    NoteStyle* style = mutableStyle(app);
    for (const Placed& p : placed) {
        HudAsset* asset = hudAssetAt(*style, p.index);
        if (!asset) continue;
        if (p.sound) {
            asset->sound = p.path;
            asset->soundDeclared = p.name;
        } else {
            asset->image = p.path;
            asset->declared = p.name;
            asset->imageOptional = false;
            asset->inherited = false;
            asset->nearby.clear();
        }
    }
    style->hasHud = true;
    afterEdit(app);
    setStatus(app, std::to_string(placed.size()) + " HUD files in " + style->name + ", each in its place by its name.",
              std::to_string(placed.size()) + " archivos del HUD en " + style->name + ", cada uno en su sitio por su nombre.");
}

void drawDropImportModal(NoteLabApp& app) {
    auto& d = app.dropImport;
    if (!d.pending.empty() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) openDropImport(app);
    if (d.requestOpen) {
        ImGui::OpenPopup("###dropimport");
        d.requestOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(720.0f, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::FolderOpen, tr(app, "Import", "Importar")) + "###dropimport").c_str(), &open)) return;
    ui::caption(tr(app, "What you dropped is not a mod. What is it for?", "Lo que soltaste no es un mod. ¿Para qué es?"));
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 30.0f);
    helpButton(app, "dropimporthelp", 4);
    ImGui::Spacing();
    for (const ImportItem& item : d.items) {
        std::string what;
        if (item.kind == ImportKind::Atlas) what = std::string(tr(app, "sheet · ", "hoja · ")) + std::to_string(item.animations.size()) + tr(app, " animations", " animaciones");
        else if (item.kind == ImportKind::Frames) what = std::string(tr(app, "frames · ", "fotogramas · ")) + std::to_string(item.files.size());
        else if (item.kind == ImportKind::Gif) what = "GIF";
        else if (item.kind == ImportKind::Font) what = std::string(tr(app, "font · ", "letra · ")) + item.family;
        else if (item.kind == ImportKind::Sound) what = tr(app, "sound", "sonido");
        else what = tr(app, "image", "imagen");
        if (item.hudIndex >= 0) what += std::string(" · HUD: ") + hudName(app, item.hudIndex);
        else if (item.piece.found) what += std::string(" · ") + partLabel(item.piece.part, app.spanish) + " " + directionLabel(item.piece.direction, app.spanish);
        ImGui::BulletText("%s  —  %s", item.label.c_str(), what.c_str());
    }
    ImGui::Spacing();
    const Source* source = selectedSource(app);
    ui::sectionHeader(tr(app, "The look of a custom note", "El aspecto de una nota custom"), ui::icon::Brush);
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##droptype", tr(app, "Note type", "Tipo de nota"), d.type.data(), d.type.size());
    if (source && ImGui::BeginPopupContextItem("droptypes")) {
        for (const NoteTypeEntry& type : source->noteTypes)
            if (!type.builtin && ImGui::Selectable(type.name.c_str())) std::snprintf(d.type.data(), d.type.size(), "%s", type.name.c_str());
        ImGui::EndPopup();
    }
    ui::tooltip(tr(app, "Write its name or right click to pick one of the mod's types", "Escribe su nombre o haz clic derecho para elegir uno de los tipos del mod"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!source || d.type[0] == '\0');
    if (ImGui::Button(ui::label(ui::icon::Check, tr(app, "Make it its look…", "Hacerlo su aspecto…")).c_str())) {
        const std::vector<fs::path> files = d.files;
        const std::string type = d.type.data();
        ImGui::CloseCurrentPopup();
        openTypeLook(app, app.selSource, type);
        addLookFiles(app, files);
    }
    ImGui::EndDisabled();
    if (d.hudImages > 0) {
        ui::sectionHeader(tr(app, "The HUD of the chosen style", "El HUD del estilo elegido"), ui::icon::Layers);
        const NoteStyle* style = selectedStyle(app);
        const std::string text = std::to_string(d.hudImages) + tr(app, " judgement, combo, digit or countdown files go to their place in ",
                                                                      " archivos de juicios, combo, cifras o cuenta atrás van a su sitio en ") +
                                 (style ? style->name : std::string("-"));
        ui::caption(text.c_str());
        ImGui::BeginDisabled(!style);
        if (ImGui::Button(ui::label(ui::icon::Layers, tr(app, "Put them in the HUD", "Ponerlos en el HUD")).c_str())) {
            applyDroppedHud(app);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
    }
    if (kTextRankingVisible && d.fonts > 0) {
        ui::sectionHeader(tr(app, "A font for the ranking", "Una letra para el ranking"), ui::icon::Font);
        if (ImGui::Button(ui::label(ui::icon::Star, tr(app, "Make a ranking HUD with it…", "Hacer un HUD de ranking con ella…")).c_str())) {
            for (const ImportItem& item : d.items)
                if (item.kind == ImportKind::Font) {
                    auto& r = app.createRating;
                    ImGui::CloseCurrentPopup();
                    openCreateRating(app);
                    r.fontPath = item.path.u8string();
                    r.fonts.insert(r.fonts.begin(), {item.family.empty() ? item.path.stem().u8string() : item.family, r.fontPath});
                    break;
                }
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawExportModal(NoteLabApp& app, SDL_Window* window) {
    NoteLabApp::Exporting& e = app.exporting;
    if (e.open) {
        ImGui::OpenPopup("###export");
        e.open = false;
    }
    const ImVec2 screen = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(1000.0f, screen.x - 40.0f), std::min(760.0f, screen.y - 40.0f)), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Zip, tr(app, "Export", "Exportar")) + "###export").c_str(), &open)) return;
    const NoteStyle* style = exportStyle(app);
    if (!style) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const Source& source = *app.sources[static_cast<size_t>(e.source)];
    ExportOptions& o = e.options;
    const double now = ImGui::GetTime();
    tutorialZone(app, kTrackExport);

    // Cabecera: que se exporta y de donde.
    ui::title(("«" + style->name + "»").c_str(), 20.0f);
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::AlignTextToFramePadding();
    ui::pill(engineLabel(style->engine), engineColor(style->engine));
    ImGui::SameLine(0.0f, 6.0f);
    ui::pill(useLabel(app, style->use), ui::color::Faint);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    ImGui::TextUnformatted(sourceName(source).c_str());
    ImGui::PopStyleColor();
    ui::caption(tr(app, "A new package shaped like a mod of the chosen engine, with its install guide in English and Spanish. "
                        "Nothing is written into a mod.",
                        "Un paquete nuevo con la forma de un mod del motor elegido, con su guía de instalación en inglés y español. "
                        "No se escribe nada dentro de un mod."));
    ImGui::Spacing();

    const float bottom = ImGui::GetFrameHeightWithSpacing() * 2.0f + 14.0f;
    const float bodyHeight = std::max(260.0f, ImGui::GetContentRegionAvail().y - bottom);
    bool refresh = false;   // algo discreto cambio: se prepara ya
    if (ImGui::BeginTable("exportlayout", 2, ImGuiTableFlags_BordersInnerV, ImVec2(0.0f, bodyHeight))) {
        ImGui::TableSetupColumn("options", ImGuiTableColumnFlags_WidthFixed, 450.0f);
        ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        // -------------------------------------------------------- opciones --
        ImGui::TableNextColumn();
        ImGui::BeginChild("exportoptions", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_None);
        ui::sectionHeader(tr(app, "Target engine", "Motor de destino"), ui::icon::Game);
        ImGui::BeginGroup();
        int target = static_cast<int>(o.target);
        const char* engines[] = {"Codename", "Psych", "V-Slice"};
        if (ui::segmented("target", &target, engines, 3, 120.0f)) {
            o.target = static_cast<Engine>(target);
            if (!exportRoleAvailable(o.target, o.role)) o.role = rolesFor(o.target).front();
            refresh = true;
            tutorialSignal("ex-engine");
        }
        if (o.target != style->engine) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Warning));
            ImGui::TextWrapped("%s", (std::string(tr(app, "Not the source engine (", "No es el motor de origen (")) + engineLabel(style->engine) +
                                      tr(app, "): you'll be asked to confirm, with what changes.", "): se pedirá confirmar, con lo que cambia.")).c_str());
            ImGui::PopStyleColor();
        }

        ui::sectionHeader(tr(app, "What it's for", "Para qué"), ui::icon::Puzzle);
        const std::vector<ExportRole> roles = rolesFor(o.target);
        std::vector<const char*> roleNames;
        int roleIndex = 0;
        for (size_t i = 0; i < roles.size(); ++i) {
            roleNames.push_back(roleLabel(app, roles[i]));
            if (roles[i] == o.role) roleIndex = static_cast<int>(i);
        }
        if (ui::segmented("role", &roleIndex, roleNames.data(), static_cast<int>(roleNames.size()))) {
            o.role = roles[static_cast<size_t>(roleIndex)];
            refresh = true;
            tutorialSignal("ex-engine");
        }
        ui::caption(roleHelp(app, o.target, o.role));
        ImGui::EndGroup();
        tutorialMark("ex-engine");

        ui::sectionHeader(tr(app, "Name", "Nombre"), ui::icon::Edit);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##exportname", tr(app, "my-skin", "mi-skin"), e.name.data(), e.name.size())) {
            e.changedAt = now;
            tutorialSignal("ex-name");
        }
        tutorialMark("ex-name");
        const std::string clean = exportName(e.name.data());
        ui::monoText(std::string(tr(app, "files and id: ", "archivos e id: ")) + clean);

        if (o.role == ExportRole::NoteType) {
            ui::sectionHeader(tr(app, "Note type", "Tipo de nota"), ui::icon::Puzzle);
            ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 6.0f);
            if (ImGui::InputTextWithHint("##exporttype", tr(app, "as the chart writes it", "tal como lo escribe el chart"),
                                         e.noteType.data(), e.noteType.size()))
                e.changedAt = now;
            ImGui::SameLine(0.0f, 6.0f);
            if (ui::iconButton("types", ui::icon::Down, "v", tr(app, "The mod's note types", "Los tipos de nota del mod")))
                ImGui::OpenPopup("exporttypes");
            if (ImGui::BeginPopup("exporttypes")) {
                bool any = false;
                std::set<std::string> shown;
                for (const NoteTypeEntry& type : source.noteTypes) {
                    if (type.builtin || !shown.insert(type.name).second) continue;
                    any = true;
                    if (ImGui::MenuItem(type.name.c_str())) {
                        std::snprintf(e.noteType.data(), e.noteType.size(), "%s", type.name.c_str());
                        std::snprintf(e.name.data(), e.name.size(), "%s", exportName(type.name).c_str());
                        refresh = true;
                    }
                }
                // Los tipos creados en la vista Bloques.
                for (const auto& entry : source.typeBlocks) {
                    if (!shown.insert(entry.first).second) continue;
                    any = true;
                    if (ImGui::MenuItem(entry.first.c_str())) {
                        std::snprintf(e.noteType.data(), e.noteType.size(), "%s", entry.first.c_str());
                        std::snprintf(e.name.data(), e.name.size(), "%s", exportName(entry.first).c_str());
                        refresh = true;
                    }
                }
                if (!any) ImGui::TextDisabled("%s", tr(app, "This mod has no custom note types.", "Este mod no tiene tipos de nota propios."));
                ImGui::EndPopup();
            }
            // Sus bloques van en los archivos del tipo (vista Bloques).
            const BlockProgram* typeBlocks = exportBlocks(app);
            const int blockCount = typeBlocks ? activeBlocks(*typeBlocks) : 0;
            ui::caption(blockCount > 0
                            ? (std::to_string(blockCount) + tr(app, " block(s): their code goes with the type (edit them in Custom notes > Blocks).",
                                                                 " bloque(s): su código va con el tipo (se editan en Notas custom > Bloques).")).c_str()
                            : tr(app, "No blocks: only its look. Blocks are added in Custom notes > Blocks.",
                                      "Sin bloques: solo su aspecto. Los bloques se añaden en Notas custom > Bloques."));
        }

        ui::sectionHeader(tr(app, "Include", "Incluir"), ui::icon::Layers);
        auto has = [&](Part part) {
            return std::any_of(style->parts.begin(), style->parts.end(), [&](const PartBinding& b) { return b.part == part; });
        };
        auto include = [&](const char* id, const std::string& text, bool* value, bool enabled, const char* tip) {
            ImGui::BeginDisabled(!enabled);
            if (ui::toggle(id, text, value, tip)) refresh = true;
            ImGui::EndDisabled();
        };
        const bool typed = o.role == ExportRole::NoteType;
        const bool shared = exportStrumsShareAtlas(o.target, o.role);
        include("notes", ui::label(ui::icon::Check, tr(app, "Notes and holds", "Notas y sostenidos")), &o.notes, has(Part::Note),
                shared ? tr(app, "In this engine the receptors go in the same file as the notes.",
                                 "En este motor los receptores van en el mismo archivo que las notas.")
                       : nullptr);
        ImGui::SameLine(0.0f, 6.0f);
        // V-Slice solo viste con el estilo de un tipo sus notas y sostenidos
        // (Strumline.hx:1146-1150, :1194-1198).
        const char* kindOnlyNotes = tr(app, "V-Slice only dresses a note kind's notes and holds with its style.",
                                            "V-Slice solo viste con el estilo de un tipo sus notas y sostenidos.");
        const bool vsliceKind = o.target == Engine::VSlice && typed;
        if (o.target == Engine::VSlice) {
            include("strums", ui::label(ui::icon::Check, tr(app, "Receptors", "Receptores")), &o.strums, has(Part::StrumStatic) && !typed,
                    typed ? kindOnlyNotes : tr(app, "V-Slice keeps them in their own atlas.", "V-Slice los guarda en su propio atlas."));
        } else {
            bool locked = shared && o.notes;
            include("strums", ui::label(ui::icon::Check, tr(app, "Receptors", "Receptores")), &locked, false,
                    typed ? tr(app, "A note type doesn't change the receptors.", "Un tipo de nota no cambia los receptores.")
                          : tr(app, "They go with the notes: same file in this engine.", "Van con las notas: el mismo archivo en este motor."));
        }
        include("splashes", ui::label(ui::icon::Check, tr(app, "Splashes", "Salpicaduras")), &o.splashes, has(Part::Splash) && !vsliceKind,
                vsliceKind ? kindOnlyNotes
                           : has(Part::Splash) ? nullptr : tr(app, "This style has no splashes.", "Este estilo no tiene salpicaduras."));
        ImGui::SameLine(0.0f, 6.0f);
        include("covers", ui::label(ui::icon::Check, tr(app, "Hold covers", "Coberturas")), &o.holdCovers,
                o.target == Engine::VSlice && !typed && (has(Part::HoldCover) || has(Part::HoldCoverStart)),
                vsliceKind ? kindOnlyNotes : tr(app, "Only V-Slice has them.", "Solo V-Slice las tiene."));
        ImGui::SameLine(0.0f, 6.0f);
        include("hud", ui::label(ui::icon::Check, "HUD"), &o.hud, style->hasHud && !typed,
                typed ? tr(app, "The HUD belongs to the mod or the song, not to a note type.",
                               "El HUD es del mod o de la canción, no de un tipo de nota.")
                      : style->hasHud ? tr(app, "Judgements, combo, numbers and countdown.", "Juicios, combo, números y cuenta atrás.")
                                      : tr(app, "This style has no HUD of its own.", "Este estilo no tiene HUD propio."));

        ui::sectionHeader(tr(app, "Output", "Salida"), ui::icon::FolderOpen);
        ImGui::BeginGroup();
        int zip = e.zip ? 1 : 0;
        const char* outputs[] = {tr(app, "Folder", "Carpeta"), "ZIP"};
        if (ui::segmented("output", &zip, outputs, 2, 100.0f)) {
            e.zip = zip == 1;
            e.written.clear();
            tutorialSignal("ex-output");
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(ui::label(ui::icon::FolderOpen, tr(app, "Choose…", "Elegir…")).c_str())) {
            tutorialSignal("ex-output");
            openDialog(app, window, DialogAction::ExportFolder);
        }
        ui::monoText(e.folder.u8string(), ui::color::Text);
        ImGui::EndGroup();
        tutorialMark("ex-output");
        const fs::path finalPath = exportTarget(app);
        std::error_code ec;
        const bool exists = fs::exists(finalPath, ec);
        const bool ours = exists && (e.zip || fs::exists(finalPath / "notelab-export.json", ec) || fs::is_empty(finalPath, ec));
        const bool blocked = insideOpenMod(app, finalPath) || (exists && !ours);
        ui::caption((std::string(tr(app, "It creates: ", "Se crea: ")) + finalPath.filename().u8string()).c_str());
        if (insideOpenMod(app, finalPath)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Error));
            ImGui::TextWrapped("%s", tr(app, "That's inside an open mod: Note Lab never writes there.",
                                             "Eso cae dentro de un mod abierto: Note Lab nunca escribe ahí."));
            ImGui::PopStyleColor();
        } else if (exists && !ours) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Error));
            ImGui::TextWrapped("%s", tr(app, "A folder with that name exists and isn't a Note Lab export: pick another name or folder.",
                                             "Ya hay una carpeta con ese nombre que no es una exportación de Note Lab: elige otro nombre u otra carpeta."));
            ImGui::PopStyleColor();
        } else if (exists) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Warning));
            ImGui::TextWrapped("%s", tr(app, "It already exists: exporting replaces it.", "Ya existe: exportar la reemplaza."));
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        // ------------------------------------------------ lo que se crea --
        ImGui::TableNextColumn();
        const bool waiting = exportKey(app) != e.preparedKey;
        ImGui::BeginChild("exportpreview", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_None);
        const ExportPackage& p = e.package;
        ui::sectionHeader(tr(app, "What it creates", "Lo que se crea"), ui::icon::Document);
        if (waiting) {
            ui::caption(tr(app, "Preparing…", "Preparando…"));
        } else {
            size_t total = 0;
            for (const ExportFile& file : p.files) total += file.bytes.size();
            const std::string summary = std::to_string(p.files.size()) + tr(app, " files · ", " archivos · ") + sizeText(total) + " · " +
                                        std::to_string(p.frames) + tr(app, " frames in ", " fotogramas en ") + std::to_string(p.atlases) +
                                        tr(app, " atlases · ", " atlas · ") + std::to_string(p.images) + tr(app, " images · ", " imágenes · ") +
                                        std::to_string(p.sounds) + tr(app, " sounds", " sonidos");
            ui::caption(summary.c_str());
            ImGui::BeginChild("exportfiles", ImVec2(0.0f, 210.0f), ImGuiChildFlags_Borders);
            for (const ExportFile& file : p.files) {
                const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
                ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
                ImGui::TextUnformatted(ui::fonts().icons ? fileGlyph(file.path) : "-");
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::PushFont(ui::fonts().mono, 13.0f);
                ImGui::TextUnformatted(file.path.c_str());
                ImGui::PopFont();
                const std::string size = sizeText(file.bytes.size());
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowRight - ImGui::CalcTextSize(size.c_str()).x));
                ImGui::TextDisabled("%s", size.c_str());
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Faint));
            ImGui::TextUnformatted(tr(app, "+ LEEME_INSTALAR.txt, INSTALL.txt and notelab-export.json when writing",
                                           "+ LEEME_INSTALAR.txt, INSTALL.txt y notelab-export.json al escribir"));
            ImGui::PopStyleColor();
            ImGui::EndChild();

            std::vector<const ExportNote*> problems, changes;
            for (const ExportNote& n : p.notes) (n.severity == Severity::Info ? changes : problems).push_back(&n);
            ui::sectionHeader((std::string(tr(app, "Conflicts and warnings", "Choques y avisos")) + " · " + std::to_string(problems.size())).c_str(),
                              ui::icon::Warning);
            if (problems.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Success));
                ImGui::TextUnformatted(tr(app, "None.", "Ninguno."));
                ImGui::PopStyleColor();
            }
            for (const ExportNote* n : problems) exportNoteRow(app, *n);
            ui::sectionHeader((std::string(tr(app, "What changes from the original", "Qué cambia respecto al original")) + " · " +
                               std::to_string(changes.size())).c_str(), ui::icon::Info);
            if (changes.empty()) ui::caption(tr(app, "Nothing: it's exported as it is.", "Nada: se exporta tal cual."));
            for (const ExportNote* n : changes) exportNoteRow(app, *n);
        }
        ImGui::EndChild();
        ImGui::EndTable();

        // Un nombre a medio escribir espera un poco; lo demas se prepara ya.
        if (refresh) e.changedAt = -1.0;
        if (waiting && (e.changedAt < 0.0 || now - e.changedAt > 0.35)) prepareExport(app);

        // -------------------------------------------------------- acciones --
        ImGui::Separator();
        if (!e.written.empty()) {
            ui::pill(tr(app, "Exported", "Exportado"), ui::color::Success, true);
            ImGui::SameLine(0.0f, 8.0f);
            ui::pill(e.writtenVerified ? tr(app, "structure verified", "estructura verificada") : tr(app, "not verified", "sin verificar"),
                     e.writtenVerified ? ui::color::Success : ui::color::Warning);
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
            ImGui::TextUnformatted(e.written.u8string().c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tr(app, "Structure verified: Note Lab read the package back with its readers and found the style "
                                                "whole, with no errors. It hasn't been tested inside the engine.",
                                                "Estructura verificada: Note Lab releyó el paquete con sus lectores y encontró el estilo "
                                                "entero, sin errores. No se ha probado dentro del motor."));
        } else if (!e.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Error));
            ImGui::TextWrapped("%s", e.error.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::NewLine();
        }
        const bool canExport = !waiting && !p.hasErrors() && !p.files.empty() && !blocked;
        ImGui::BeginDisabled(!canExport);
        const std::string exportText = ui::label(ui::icon::Check, exists && ours ? tr(app, "Replace", "Reemplazar") : tr(app, "Export", "Exportar"));
        const bool exportPressed = ui::primaryButton(exportText, ImVec2(150.0f, 0.0f));
        tutorialMark("ex-write");
        if (exportPressed) {
            if (o.target != style->engine) ImGui::OpenPopup("###exportconfirm");
            else runExport(app);
        }
        ImGui::EndDisabled();
        if (!canExport && !waiting && p.hasErrors())
            ui::tooltip(tr(app, "Fix the errors in the list first.", "Primero hay que arreglar los errores de la lista."));
        ImGui::SameLine(0.0f, 8.0f);
        if (!e.written.empty()) {
#ifdef _WIN32
            if (ImGui::Button(ui::label(ui::icon::FolderOpen, tr(app, "Show in folder", "Mostrar en la carpeta")).c_str()))
                revealInExplorer(e.written);
            ImGui::SameLine(0.0f, 8.0f);
#endif
        }
        if (ImGui::Button(tr(app, "Close", "Cerrar"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow())
            ImGui::CloseCurrentPopup();

        // Otro motor: confirmar con el resumen de lo que cambia (§20).
        ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal((ui::label(ui::icon::Warning, tr(app, "Another engine", "Otro motor")) + "###exportconfirm").c_str(), nullptr)) {
            ImGui::TextWrapped("%s", (std::string(tr(app, "You're exporting a ", "Vas a exportar un estilo de ")) + engineLabel(style->engine) +
                                      tr(app, " style to ", " a ") + engineLabel(o.target) + ". " +
                                      tr(app, "This is what changes:", "Esto es lo que cambia:")).c_str());
            ImGui::Spacing();
            int shown = 0;
            for (const ExportNote& n : p.notes) {
                if (shown >= 8) break;
                exportNoteRow(app, n);
                ++shown;
            }
            if (p.notes.size() > 8)
                ui::caption((std::string(tr(app, "…and ", "…y ")) + std::to_string(p.notes.size() - 8) +
                             tr(app, " more in the list and in the guide.", " más en la lista y en la guía.")).c_str());
            if (p.notes.empty()) ui::caption(tr(app, "Nothing is lost on the way.", "No se pierde nada por el camino."));
            ImGui::Spacing();
            if (ui::primaryButton(ui::label(ui::icon::Check, tr(app, "Export anyway", "Exportar igualmente")), ImVec2(180.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                runExport(app);
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (ImGui::Button(tr(app, "Back", "Volver"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow())
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ImGui::EndPopup();
}

void handleShortcuts(NoteLabApp& app, SDL_Window* window) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || app.bindingLane >= 0) return;
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) openDialog(app, window, DialogAction::AddModFolder);
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) requestAction(app, window, PendingAction::OpenProject);
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false)) requestAction(app, window, PendingAction::NewProject);
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveProject(app, window);
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) openDialog(app, window, DialogAction::SaveProject);
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_E, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        openExport(app);
    // Con el raton en el editor de bloques, deshacer, Supr y las letras son suyos.
    const bool blocksKeys = app.canvas.wantsKeys;
    if (!blocksKeys && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) swapHistory(app, app.undo, app.redo);
    if (!blocksKeys && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) swapHistory(app, app.redo, app.undo);
    // Jugando, Espacio y R pueden ser teclas de carril; tras el final, no.
    const bool transportKeys = !blocksKeys && (!app.manual || app.finished);
    if (transportKeys && ImGui::IsKeyPressed(ImGuiKey_Space, false)) togglePlay(app);
    if (transportKeys && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) restartPreview(app);
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && !app.sources.empty()) reloadAll(app);
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        openHelp(app, app.centerTab == 1 ? (app.typesView == 1 ? 6 : 2) : app.centerTab == 2 ? 4 : 0);
}

void draw(NoteLabApp& app, SDL_Window* window) {
    app.canvas.wantsKeys = false;
    tutorialBeginFrame();
    drawMenuBar(app, window);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##notelab", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    // Barra de herramientas.
    const float toolbarHeight = ImGui::GetFrameHeight() + 18.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::vec(ui::color::Toolbar));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 9.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("toolbar", ImVec2(0.0f, toolbarHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawToolbar(app, window);
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(at, ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y), ui::color::Border);
    }

    // Cuerpo: mods, centro e inspector.
    const float statusHeight = ImGui::GetFrameHeight() + 8.0f;
    const float bodyHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y - statusHeight);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::BeginChild("body", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    const float innerHeight = ImGui::GetContentRegionAvail().y;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5.0f, 0.0f));
    // El editor de bloques no usa el inspector de estilos: su sitio es del lienzo.
    const bool wide = !app.sources.empty() && app.centerTab == 1 && app.typesView == 1;
    const bool welcome = app.sources.empty();
    app.compactLayout = !welcome && ImGui::GetContentRegionAvail().x < 1100.0f;
    const bool hideInspector = wide || welcome || app.compactLayout;
    // Misma tabla con la columna apagada: cambiar de tabla perderia la pestana elegida.
    if (ImGui::BeginTable("layout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody, ImVec2(0.0f, innerHeight))) {
        ImGui::TableSetupColumn("mods", ImGuiTableColumnFlags_WidthFixed | (welcome ? ImGuiTableColumnFlags_Disabled : 0),
                               app.compactLayout ? 230.0f : 300.0f);
        ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("inspector", ImGuiTableColumnFlags_WidthFixed | (hideInspector ? ImGuiTableColumnFlags_Disabled : 0), 380.0f);
        ImGui::TableNextRow();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        if (!welcome) {
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginChild("mods", ImVec2(0.0f, innerHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            drawSourceWorkspace(app, window);
            ImGui::EndChild();
            tutorialPanel(0);
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("center", ImVec2(0.0f, innerHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        drawCenter(app, window);
        ImGui::EndChild();
        tutorialPanel(1);
        if (!hideInspector) {
            ImGui::TableSetColumnIndex(2);
            ImGui::BeginChild("inspector", ImVec2(0.0f, innerHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            drawInspector(app, window);
            ImGui::EndChild();
            tutorialPanel(2);
        }
        ImGui::PopStyleVar();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    // Barra de estado.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(at, ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y), ui::color::Border);
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::vec(ui::color::Toolbar));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("status", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawStatusBar(app);
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    if (app.openKeys) {
        ImGui::OpenPopup("keys");
        app.openKeys = false;
    }
    drawKeysPopup(app);
    drawShortcutsModal(app);
    drawAboutModal(app);
    drawStyleComposer(app);
    drawSourceCloseModal(app, window);
    drawUnsavedModal(app, window);
    drawIssuesModal(app);
    drawExportModal(app, window);
    drawCreateHudModal(app);
    drawTypeLookModal(app, window);
    drawCreateRatingModal(app, window);
    drawCustomCreator(app, window);
    drawNewNoteModal(app);
    drawScriptImportModal(app);
    drawSpriteEditor(app, 0);
    drawCustomBot(app);
    drawChartSave(app, window);
    drawDropImportModal(app);
    drawHelpModal(app);
    drawModResources(app, window);
    drawMediaImport(app, window);
    ImGui::End();
    handleShortcuts(app, window);
    drawFindingsWindow(app);
    drawTutorial(app);
}

// ------------------------------------------------------------ prueba de UI --

// --ui-test=blocks: el editor de bloques con el raton y el teclado simulados,
// de punta a punta: arrastrar de la paleta, encajar, enchufar un valor, meter
// un bloque en un «si», tirar a la papelera, escribir en una ranura, deshacer
// y el clic rapido. Cada paso se comprueba en el programa; la salida dice
// [ok] o [FAIL] y el codigo de salida es el numero de fallos.
struct UiFrame {
    ImVec2 pos{-1.0f, -1.0f};
    int button = -1;             // -1 igual, 0 suelta, 1 pulsa
    std::string text;            // caracteres escritos
    ImGuiKey key = ImGuiKey_None;
    int keyDown = -1;            // -1 nada, 0 suelta, 1 pulsa
    bool ctrl = false;
};

struct UiStep {
    const char* name;
    std::function<std::vector<UiFrame>(NoteLabApp&)> plan;
    std::function<bool(NoteLabApp&)> check;
};

struct UiTest {
    bool active = false;
    std::vector<UiStep> steps;
    size_t step = 0;
    std::vector<UiFrame> frames;
    size_t frame = 0;
    int settle = 0;
    int failures = 0;
    bool planned = false;
    bool done = false;
    ImVec2 lastPos{-1.0f, -1.0f};   // se repite cada cuadro: el raton real no se cuela
    int framesRun = 0;
    double waitUntil = -1.0;        // un marcador «\x03wait=s»: esperar s segundos de reloj
};

UiTest g_uiTest;

ImVec2 rectPoint(const ImVec4& r, float fx, float fy) { return ImVec2(r.x + r.z * fx, r.y + r.w * fy); }

std::vector<UiFrame> uiClick(ImVec2 at) {
    std::vector<UiFrame> out;
    UiFrame move;
    move.pos = at;
    out.push_back(move);
    UiFrame down = move;
    down.button = 1;
    out.push_back(down);
    UiFrame up = move;
    up.button = 0;
    out.push_back(up);
    return out;
}

std::vector<UiFrame> uiDrag(ImVec2 from, ImVec2 to) {
    std::vector<UiFrame> out;
    UiFrame f;
    f.pos = from;
    out.push_back(f);
    f.button = 1;
    out.push_back(f);
    f.button = -1;
    for (int i = 1; i <= 12; ++i) {
        const float t = static_cast<float>(i) / 12.0f;
        f.pos = ImVec2(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
        out.push_back(f);
    }
    out.push_back(f);
    f.button = 0;
    out.push_back(f);
    return out;
}

std::vector<UiFrame> uiThen(std::vector<UiFrame> a, const std::vector<UiFrame>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

BlockProgram* uiProgram(NoteLabApp& app) {
    if (app.blocksSource < 0 || app.blocksSource >= static_cast<int>(app.sources.size())) return nullptr;
    auto& blocks = app.sources[static_cast<size_t>(app.blocksSource)]->typeBlocks;
    const auto found = blocks.find(app.blocksType);
    return found == blocks.end() ? nullptr : &found->second;
}

// El primer bloque con esa clave (los ids crecen: el mas antiguo).
int uiFind(NoteLabApp& app, const std::string& key) {
    if (BlockProgram* p = uiProgram(app))
        for (const auto& entry : p->nodes)
            if (entry.second.key == key) return entry.first;
    return -1;
}

ImVec4 uiRect(NoteLabApp& app, int id) {
    const auto found = app.canvas.blockRects.find(id);
    return found == app.canvas.blockRects.end() ? ImVec4(-1.0f, -1.0f, 0.0f, 0.0f) : found->second;
}

// Soltar un bloque de la paleta (cogido a 10 px de su esquina) con su esquina en `corner`.
std::vector<UiFrame> uiFromPalette(NoteLabApp& app, int category, const std::string& key, ImVec2 corner) {
    const ImVec4 rail = app.canvas.categoryRects[static_cast<size_t>(category)];
    std::vector<UiFrame> out = uiClick(rectPoint(rail, 0.5f, 0.4f));
    // Unos cuadros para que la paleta se desplace a la categoria.
    UiFrame idle;
    idle.pos = rectPoint(rail, 0.5f, 0.4f);
    for (int i = 0; i < 4; ++i) out.push_back(idle);
    UiFrame marker;
    marker.text = "\x01" + key;   // el origen se lee cuando toca, con la paleta ya desplazada
    marker.pos = ImVec2(corner.x, corner.y);
    out.push_back(marker);
    return out;
}

std::vector<UiStep> codeUiSteps() {
    auto baseline = std::make_shared<std::string>();
    const std::string prefix = "// @user: edición con color\n";
    auto keys = [](ImGuiKey key, bool ctrl) {
        UiFrame down; down.key = key; down.keyDown = 1; down.ctrl = ctrl;
        UiFrame up = down; up.keyDown = 0; up.ctrl = false;
        return std::vector<UiFrame>{down, up};
    };
    std::vector<UiStep> steps;
    steps.push_back({"codigo editable dibuja comentarios, palabras clave y numeros con colores y lineas",
        [baseline](NoteLabApp& app) { *baseline = app.blockCodeBuffer.data(); return std::vector<UiFrame>(3); },
        [](NoteLabApp& app) { return app.blockEditor.drawn[1] > 0 && app.blockEditor.drawn[3] > 0 && app.blockEditor.drawn[4] > 0 && app.blockEditor.lines.size() > 10; }});
    steps.push_back({"escribir UTF-8 conserva el borrador y actualiza el coloreado",
        [keys, prefix](NoteLabApp& app) {
            auto frames = uiThen(uiClick(rectPoint(app.blockEditor.rect, 0.4f, 0.2f)), keys(ImGuiKey_Home, true));
            UiFrame text; text.text = prefix; frames.push_back(text); return frames;
        },
        [prefix](NoteLabApp& app) { const BlockProgram* p = uiProgram(app); return p && !p->drafts.empty() && std::string(app.blockCodeBuffer.data()).rfind(prefix, 0) == 0 && app.blockEditor.text == app.blockCodeBuffer.data() && app.blockEditor.drawn[1] > 0; }});
    steps.push_back({"deshacer revierte la ultima edicion sin perder colores",
        [keys](NoteLabApp&) { return keys(ImGuiKey_Z, true); },
        [baseline, prefix](NoteLabApp& app) {
            const std::string text = app.blockCodeBuffer.data();
            return text != prefix + *baseline && text.size() >= baseline->size() &&
                text.compare(text.size() - baseline->size(), baseline->size(), *baseline) == 0 &&
                app.blockEditor.text == text && app.blockEditor.drawn[4] > 0;
        }});
    steps.push_back({"rehacer recupera el comentario editado",
        [keys](NoteLabApp&) { return keys(ImGuiKey_Y, true); },
        [prefix](NoteLabApp& app) { return std::string(app.blockCodeBuffer.data()).rfind(prefix, 0) == 0 && app.blockEditor.text == app.blockCodeBuffer.data(); }});
    steps.push_back({"seleccionar todo sigue usando el editor real",
        [keys](NoteLabApp&) { return keys(ImGuiKey_A, true); },
        [](NoteLabApp& app) { auto* input = app.blockEditor.id ? ImGui::GetInputTextState(app.blockEditor.id) : nullptr; return input && input->HasSelection() && std::abs(input->GetSelectionEnd() - input->GetSelectionStart()) == static_cast<int>(app.blockEditor.text.size()); }});
    steps.push_back({"ir al final desplaza texto coloreado y numeracion juntos",
        [keys](NoteLabApp&) { return keys(ImGuiKey_End, true); },
        [](NoteLabApp& app) { auto* input = app.blockEditor.id ? ImGui::GetInputTextState(app.blockEditor.id) : nullptr; return input && input->GetCursorPos() == static_cast<int>(app.blockEditor.text.size()) && app.blockEditor.firstLine > 0 && app.blockEditor.visibleLines > 0; }});
    steps.push_back({"volver al inicio conserva el texto y recupera los colores visibles",
        [keys](NoteLabApp&) { return keys(ImGuiKey_Home, true); },
        [prefix](NoteLabApp& app) { return app.blockEditor.firstLine == 0 && std::string(app.blockCodeBuffer.data()).rfind(prefix, 0) == 0 && app.blockEditor.drawn[1] > 0 && app.blockEditor.drawn[4] > 0; }});
    return steps;
}

std::vector<UiStep> resourcesUiSteps() {
    auto initial = std::make_shared<std::string>();
    auto image = std::make_shared<std::string>();
    auto keyFrames = [](ImGuiKey key, bool ctrl) {
        UiFrame down; down.key = key; down.keyDown = 1; down.ctrl = ctrl;
        UiFrame up = down; up.keyDown = 0;
        return std::vector<UiFrame>{down, up};
    };
    std::vector<UiStep> steps;
    steps.push_back({"recursos: abrir el lateral no modifica el programa",
        [initial](NoteLabApp& app) { *initial = writeProgram(*uiProgram(app)); return uiClick(rectPoint(app.resourcesPanelRect, 0.5f, 0.5f)); },
        [initial](NoteLabApp& app) { return app.leftPanel == 1 && app.resourceSidebar.bounds.z > 0.0f && writeProgram(*uiProgram(app)) == *initial && app.canvas.canvasMin.y < 410.0f && app.blocksToolbarViewsRect.x + app.blocksToolbarViewsRect.z <= ImGui::GetMainViewport()->WorkSize.x - 10.0f; }});
    steps.push_back({"recursos: filtrar imagenes muestra tarjetas sin desbordar el lateral",
        [image](NoteLabApp& app) {
            const auto& catalog = modResourceCatalog(*app.sources[app.blocksSource]);
            for (const auto& resource : catalog) if (resource.kind == ResourceKind::Image) { *image = resource.path; break; }
            return uiClick(rectPoint(app.resourceSidebar.categoryRects[0], 0.5f, 0.5f));
        },
        [image](NoteLabApp& app) { const auto& side = app.resourceSidebar; return side.category == 0 && !image->empty() && side.itemRects.count(*image) && side.itemRects.at(*image).z <= side.bounds.z && side.itemRects.at(*image).z > 100.0f; }});
    steps.push_back({"recursos: buscar una ruta inexistente vacia solo la lista",
        [](NoteLabApp& app) { auto frames = uiClick(rectPoint(app.resourceSidebar.searchRect, 0.5f, 0.5f)); UiFrame text; text.text = "not-found-resource-qa"; frames.push_back(text); return frames; },
        [initial](NoteLabApp& app) { return std::string(app.resourceSidebar.search.data()) == "not-found-resource-qa" && app.resourceSidebar.itemRects.empty() && writeProgram(*uiProgram(app)) == *initial; }});
    steps.push_back({"recursos: limpiar busqueda restaura las tarjetas",
        [keyFrames](NoteLabApp&) { return uiThen(keyFrames(ImGuiKey_A, true), keyFrames(ImGuiKey_Backspace, false)); },
        [image](NoteLabApp& app) { return app.resourceSidebar.search[0] == 0 && app.resourceSidebar.itemRects.count(*image); }});
    steps.push_back({"recursos: clic en tarjeta abre inspeccion real sin reproducir audio",
        [image](NoteLabApp& app) { return uiClick(rectPoint(app.resourceSidebar.itemRects.at(*image), 0.5f, 0.5f)); },
        [image](NoteLabApp& app) { return app.media.isOpen && app.media.selected == *image && !app.media.imagePath.empty() && !app.media.audio; }});
    steps.push_back({"recursos: cerrar inspeccion libera la imagen y conserva el programa",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.closeRect, 0.5f, 0.5f)); },
        [initial](NoteLabApp& app) {
            const bool ok = !app.media.isOpen && app.media.imagePath.empty() && writeProgram(*uiProgram(app)) == *initial;
            if (!ok) std::printf("resource modal: open=%d close=(%.0f,%.0f,%.0f,%.0f) mouse=(%.0f,%.0f) image=%zu\n", app.media.isOpen, app.media.closeRect.x, app.media.closeRect.y, app.media.closeRect.z, app.media.closeRect.w, ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, app.media.imagePath.size());
            return ok;
        }});
    steps.push_back({"recursos: En uso abre vacio sin robar area al lienzo",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.resourceSidebar.usesRect, 0.5f, 0.5f)); },
        [](NoteLabApp& app) { return app.resourceSidebar.page == 1 && app.resourceSidebar.itemRects.empty() && app.canvas.canvasMax.x > app.canvas.canvasMin.x && app.canvas.canvasMax.y > app.canvas.canvasMin.y; }});
    steps.push_back({"recursos: volver a mods conserva seleccion y contenido",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.sourcesPanelRect, 0.5f, 0.5f)); },
        [initial](NoteLabApp& app) { return app.leftPanel == 0 && !app.blocksType.empty() && writeProgram(*uiProgram(app)) == *initial; }});
    steps.push_back({"recursos: volver al lateral conserva su pestaña",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.resourcesPanelRect, 0.5f, 0.5f)); },
        [](NoteLabApp& app) { return app.leftPanel == 1 && app.resourceSidebar.page == 1; }});
    steps.push_back({"recursos: añadir screamer crea ranuras de imagen y sonido conectadas",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.resourceSidebar.screamerRect, 0.5f, 0.5f)); },
        [](NoteLabApp& app) { auto* program = uiProgram(app); const int id = uiFind(app, "do.screamer"); const auto* node = program->node(id); const auto* def = node ? blockDef(node->key) : nullptr; return node && def && app.media.isOpen && app.media.block == id && app.media.argument == 0 && def->args[0].kind == ArgKind::Image && def->args[1].kind == ArgKind::Sound && (placeOf(*program, id) & kPlaceHit) != 0; }});
    steps.push_back({"recursos: elegir PNG en el selector permite asignarlo",
        [image](NoteLabApp& app) { return uiClick(rectPoint(app.media.fileRects.at(*image), 0.5f, 0.5f)); },
        [image](NoteLabApp& app) { return app.media.selected == *image && app.media.assignRect.z > 0.0f && !app.media.imagePath.empty(); }});
    steps.push_back({"recursos: asignar PNG actualiza bloque y tarjeta En uso",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.assignRect, 0.5f, 0.5f)); },
        [image](NoteLabApp& app) { const auto uses = blockResourceUses(*uiProgram(app)); return !app.media.isOpen && app.resourceSidebar.page == 1 && app.resourceSidebar.itemRects.count(*image) && std::any_of(uses.begin(), uses.end(), [&](const auto& use) { return use.kind == ResourceKind::Image && use.path == *image && use.active; }); }});
    return steps;
}

std::vector<UiStep> auditionUiSteps() {
    auto initial = std::make_shared<std::string>();
    auto selected = std::make_shared<std::string>();
    std::vector<UiStep> steps;
    steps.push_back({"audio: inspeccion abre sin autoplay ni modificar bloques",
        [initial, selected](NoteLabApp& app) {
            *initial = writeProgram(*uiProgram(app));
            auto& source = *app.sources[app.blocksSource];
            for (const auto& resource : modResourceCatalog(source)) if (resource.kind == ResourceKind::Sound) { *selected = resource.path; break; }
            openModResources(app, app.blocksSource, app.blocksType, -1, -1, ResourceKind::Sound);
            if (const auto* resource = findModResource(app.media.resources, *selected, ResourceKind::Sound)) selectMediaResource(app, source, *resource);
            return std::vector<UiFrame>(3);
        },
        [initial, selected](NoteLabApp& app) { return !selected->empty() && app.media.isOpen && !app.media.audio && !app.media.audioLoop && !app.media.audioRange && app.media.listenRect.z > 0.0f && writeProgram(*uiProgram(app)) == *initial; }});
    steps.push_back({"audio: Escuchar carga archivo largo y activa controles reales",
        [](NoteLabApp& app) {
            app.media.volume = 0.0f; auto frames = uiClick(rectPoint(app.media.listenRect, 0.5f, 0.5f));
            UiFrame wait; wait.text = "\x03" "audio-ready"; frames.push_back(wait); return frames;
        },
        [](NoteLabApp& app) { return app.media.audio && app.media.audio->playing() && app.media.audio->durationMs() > 64000.0 && app.media.loopRect.z > 0.0f && app.media.balanceRect.z > 0.0f; }});
    steps.push_back({"audio: boton Loop activa repeticion del motor",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.loopRect, 0.5f, 0.5f)); },
        [](NoteLabApp& app) { return app.media.audioLoop && app.media.audio && app.media.audio->hasLoop(); }});
    steps.push_back({"audio: velocidad responde al slider sin tocar la cancion",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.speedRect, 0.7f, 0.5f)); },
        [](NoteLabApp& app) { return app.media.audio && app.media.audioSpeed > 1.0f && std::abs(app.media.audio->rate() - app.media.audioSpeed) < 0.01f && !app.audio.ready(); }});
    steps.push_back({"audio: balance responde al slider en todo el ancho disponible",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.balanceRect, 0.8f, 0.5f)); },
        [](NoteLabApp& app) { return app.media.audioBalance > 0.4f && app.media.balanceRect.x + app.media.balanceRect.z < ImGui::GetMainViewport()->WorkSize.x; }});
    steps.push_back({"audio: rango A-B permite repetir una seleccion valida",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.rangeRect, 0.2f, 0.5f)); },
        [](NoteLabApp& app) { return app.media.audioRange && app.media.audioStart < app.media.audioEnd && app.media.audio && app.media.audio->hasLoop(); }});
    steps.push_back({"audio: Pausar detiene la reproduccion sin perder el recurso",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.pauseRect, 0.5f, 0.5f)); },
        [selected](NoteLabApp& app) { return app.media.audio && !app.media.audio->playing() && app.media.selected == *selected; }});
    steps.push_back({"audio: Continuar vuelve a reproducir con los ajustes",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.pauseRect, 0.5f, 0.5f)); },
        [](NoteLabApp& app) { return app.media.audio && app.media.audio->playing() && app.media.audioLoop && app.media.audioRange; }});
    steps.push_back({"audio: Detener vuelve a A sin reinicio automatico",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.stopRect, 0.5f, 0.5f)); },
        [](NoteLabApp& app) { return app.media.audio && !app.media.audio->playing() && !app.media.startWhenReady && std::abs(app.media.audio->positionMs() - app.media.audioStart * 1000.0) < 5.0; }});
    steps.push_back({"audio: boton Loop tambien desactiva repeticion",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.loopRect, 0.5f, 0.5f)); },
        [](NoteLabApp& app) { return !app.media.audioLoop && app.media.audio && !app.media.audio->hasLoop(); }});
    steps.push_back({"audio: cerrar libera reproduccion y conserva el programa",
        [](NoteLabApp& app) { return uiClick(rectPoint(app.media.closeRect, 0.5f, 0.5f)); },
        [initial](NoteLabApp& app) { return !app.media.isOpen && !app.media.audio && !app.media.audioRange && writeProgram(*uiProgram(app)) == *initial; }});
    return steps;
}

std::vector<UiStep> blocksUiSteps() {
    const float grab = 10.0f / 0.82f;   // cogido a 10 px de la esquina en la paleta (escala 0,82)
    std::vector<UiStep> steps;
    steps.push_back({"el lienzo abre con «cuando cualquiera toca» y «hacer «Hey!»»",
                     [](NoteLabApp&) { return std::vector<UiFrame>(3); },
                     [](NoteLabApp& app) { return uiFind(app, "event.hit") >= 0 && uiFind(app, "do.hey") >= 0 && uiRect(app, uiFind(app, "do.hey")).z > 0.0f; }});
    steps.push_back({"arrastrar «destello» de la paleta debajo de «Hey!»: encaja",
                     [grab](NoteLabApp& app) {
                         const ImVec4 hey = uiRect(app, uiFind(app, "do.hey"));
                         return uiFromPalette(app, static_cast<int>(BlockCategory::Camera), "do.flash", ImVec2(hey.x + grab, hey.y + hey.w + grab));
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         return p && chainOf(*p, uiFind(app, "event.hit")) ==
                                         std::vector<int>({uiFind(app, "event.hit"), uiFind(app, "do.hey"), uiFind(app, "do.flash")});
                     }});
    steps.push_back({"arrastrar «si» de la paleta debajo de «destello»",
                     [grab](NoteLabApp& app) {
                         const ImVec4 flash = uiRect(app, uiFind(app, "do.flash"));
                         return uiFromPalette(app, static_cast<int>(BlockCategory::Control), "ctl.if", ImVec2(flash.x + grab, flash.y + flash.w + grab));
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         return p && chainOf(*p, uiFind(app, "event.hit")).size() == 4 && p->node(uiFind(app, "do.flash"))->next == uiFind(app, "ctl.if");
                     }});
    steps.push_back({"enchufar «probabilidad de» en la condicion del «si»",
                     [grab](NoteLabApp& app) {
                         const auto slot = app.canvas.slotRects.find(static_cast<long long>(uiFind(app, "ctl.if")) * 16);
                         const ImVec4 s = slot == app.canvas.slotRects.end() ? ImVec4() : slot->second;
                         return uiFromPalette(app, static_cast<int>(BlockCategory::Operators), "op.chance", ImVec2(s.x + grab, s.y + s.w * 0.5f));
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         const int check = uiFind(app, "ctl.if");
                         return p && check >= 0 && p->node(check)->args[0].block == uiFind(app, "op.chance") && uiFind(app, "op.chance") >= 0;
                     }});
    steps.push_back({"arrastrar el «si» (con su valor) entre «Hey!» y «destello»",
                     [](NoteLabApp& app) {
                         const ImVec4 check = uiRect(app, uiFind(app, "ctl.if"));
                         const ImVec4 hey = uiRect(app, uiFind(app, "do.hey"));
                         return uiDrag(ImVec2(check.x + 6.0f, check.y + 8.0f), ImVec2(hey.x + 6.0f, hey.y + hey.w + 8.0f));
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         return p && chainOf(*p, uiFind(app, "event.hit")) == std::vector<int>({uiFind(app, "event.hit"), uiFind(app, "do.hey"),
                                                                                                 uiFind(app, "ctl.if"), uiFind(app, "do.flash")});
                     }});
    steps.push_back({"meter «destello» dentro del «si»",
                     [](NoteLabApp& app) {
                         const ImVec4 flash = uiRect(app, uiFind(app, "do.flash"));
                         const auto body = app.canvas.bodyPoints.find(static_cast<long long>(uiFind(app, "ctl.if")) * 16 + 1);
                         const ImVec2 to = body == app.canvas.bodyPoints.end() ? ImVec2() : body->second;
                         return uiDrag(ImVec2(flash.x + 6.0f, flash.y + 8.0f), ImVec2(to.x + 6.0f, to.y + 8.0f));
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         const int check = uiFind(app, "ctl.if");
                         return p && check >= 0 && p->node(check)->args[1].block == uiFind(app, "do.flash") && p->node(check)->next < 0;
                     }});
    steps.push_back({"tirar el valor a la papelera: el «si» se queda sin condicion",
                     [](NoteLabApp& app) {
                         const ImVec4 value = uiRect(app, uiFind(app, "op.chance"));
                         return uiDrag(ImVec2(value.x + 8.0f, value.y + 8.0f), rectPoint(app.canvas.trashRect, 0.5f, 0.5f));
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         const int check = uiFind(app, "ctl.if");
                         return p && uiFind(app, "op.chance") < 0 && check >= 0 && p->node(check)->args[0].block < 0;
                     }});
    steps.push_back({"escribir 0.5 en la duracion del destello",
                     [](NoteLabApp& app) {
                         const auto slot = app.canvas.slotRects.find(static_cast<long long>(uiFind(app, "do.flash")) * 16 + 1);
                         const ImVec4 s = slot == app.canvas.slotRects.end() ? ImVec4() : slot->second;
                         std::vector<UiFrame> out = uiClick(rectPoint(s, 0.5f, 0.5f));
                         UiFrame idle;
                         idle.pos = rectPoint(s, 0.5f, 0.5f);
                         out.push_back(idle);
                         out.push_back(idle);
                         UiFrame typed = idle;
                         typed.text = "0.5";
                         out.push_back(typed);
                         UiFrame enter = idle;
                         enter.key = ImGuiKey_Enter;
                         enter.keyDown = 1;
                         out.push_back(enter);
                         enter.keyDown = 0;
                         out.push_back(enter);
                         return out;
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         const int flash = uiFind(app, "do.flash");
                         return p && flash >= 0 && p->node(flash)->args[1].value == "0.5";
                     }});
    steps.push_back({"Ctrl+Z con el raton en el lienzo deshace la escritura",
                     [](NoteLabApp& app) {
                         const ImVec2 empty(app.canvas.canvasMax.x - 200.0f, app.canvas.canvasMin.y + 40.0f);
                         std::vector<UiFrame> out;
                         UiFrame move;
                         move.pos = empty;
                         out.push_back(move);
                         UiFrame press = move;
                         press.ctrl = true;
                         press.key = ImGuiKey_Z;
                         press.keyDown = 1;
                         out.push_back(press);
                         press.keyDown = 0;
                         out.push_back(press);
                         UiFrame release = move;
                         out.push_back(release);
                         return out;
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         const int flash = uiFind(app, "do.flash");
                         return p && flash >= 0 && p->node(flash)->args[1].value == "0.3";
                     }});
    steps.push_back({"un clic en «temblor» lo pone bajo «cuando el jugador toca» (nuevo)",
                     [](NoteLabApp& app) {
                         const ImVec4 rail = app.canvas.categoryRects[static_cast<size_t>(BlockCategory::Camera)];
                         std::vector<UiFrame> out = uiClick(rectPoint(rail, 0.5f, 0.4f));
                         UiFrame idle;
                         idle.pos = rectPoint(rail, 0.5f, 0.4f);
                         for (int i = 0; i < 4; ++i) out.push_back(idle);
                         UiFrame marker;
                         marker.text = "\x02" + std::string("do.shake");
                         out.push_back(marker);
                         return out;
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         if (!p) return false;
                         for (int top : p->tops) {
                             const BlockNode* hat = p->node(top);
                             if (hat && hat->key == "event.hit" && hat->args[0].value == "player")
                                 for (int id : chainOf(*p, top))
                                     if (p->node(id)->key == "do.shake") return true;
                         }
                         return false;
                     }});
    steps.push_back({"una propiedad no encaja bajo «cuando toca»: queda suelta",
                     [grab](NoteLabApp& app) {
                         const ImVec4 shake = uiRect(app, uiFind(app, "do.shake"));
                         return uiFromPalette(app, static_cast<int>(BlockCategory::Properties), "play.avoid",
                                              ImVec2(shake.x + grab, shake.y + shake.w + grab));
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         const int avoid = uiFind(app, "play.avoid");
                         return p && avoid >= 0 && linkOf(*p, avoid).parent < 0 &&
                                std::find(p->tops.begin(), p->tops.end(), avoid) != p->tops.end();
                     }});
    steps.push_back({"soltar «al crear» encima de la propiedad suelta: la recoge",
                     [grab](NoteLabApp& app) {
                         const ImVec4 avoid = uiRect(app, uiFind(app, "play.avoid"));
                         // La cabecera mide 16 de joroba + 34 de fila: su borde de abajo sobre la propiedad.
                         return uiFromPalette(app, static_cast<int>(BlockCategory::Events), "event.create",
                                              ImVec2(avoid.x + grab, avoid.y - 50.0f + grab));
                     },
                     [](NoteLabApp& app) {
                         BlockProgram* p = uiProgram(app);
                         const int create = uiFind(app, "event.create");
                         return p && create >= 0 && chainOf(*p, create) == std::vector<int>({create, uiFind(app, "play.avoid")}) &&
                                blocksAvoid(*p);
                     }});
    steps.push_back({"buscar bloques en la paleta filtra sin borrar el programa",
                     [](NoteLabApp& app) {
                         auto out = uiClick(rectPoint(app.canvas.searchRect, 0.3f, 0.5f));
                         UiFrame type; type.pos = rectPoint(app.canvas.searchRect, 0.3f, 0.5f); type.text = "sustain"; out.push_back(type); return out;
                     },
                     [](NoteLabApp& app) { return std::string(app.canvas.search.data()) == "sustain" && app.canvas.paletteRects.count("is.sustain") && !app.canvas.paletteRects.count("do.flash") && uiFind(app, "do.flash") >= 0; }});
    steps.push_back({"un acceso de categoria limpia la busqueda y la despliega",
                     [](NoteLabApp& app) { return uiClick(rectPoint(app.canvas.categoryRects[static_cast<size_t>(BlockCategory::Camera)], 0.5f, 0.4f)); },
                     [](NoteLabApp& app) { return app.canvas.search[0] == '\0' && app.canvas.paletteRects.count("do.flash") && !app.canvas.collapsed[static_cast<size_t>(BlockCategory::Camera)]; }});
    steps.push_back({"la flecha de categoria pliega la paleta, no la logica",
                     [](NoteLabApp& app) { return uiClick(rectPoint(app.canvas.categoryHeaderRects[static_cast<size_t>(BlockCategory::Camera)], 0.12f, 0.5f)); },
                     [](NoteLabApp& app) { return app.canvas.collapsed[static_cast<size_t>(BlockCategory::Camera)] && !app.canvas.paletteRects.count("do.flash") && uiFind(app, "do.flash") >= 0; }});
    steps.push_back({"la categoria vuelve a expandirse",
                     [](NoteLabApp& app) { return uiClick(rectPoint(app.canvas.categoryHeaderRects[static_cast<size_t>(BlockCategory::Camera)], 0.12f, 0.5f)); },
                     [](NoteLabApp& app) { return !app.canvas.collapsed[static_cast<size_t>(BlockCategory::Camera)] && app.canvas.paletteRects.count("do.flash"); }});
    steps.push_back({"la vista de bloques y codigo muestra su separador",
                     [](NoteLabApp& app) { app.blocksView = 2; return std::vector<UiFrame>(4); },
                     [](NoteLabApp& app) { return app.blocksDividerRect.z > 0 && app.blocksDividerRect.w > 0; }});
    steps.push_back({"arrastrar el separador cambia el tamano del codigo sin modificar los bloques",
                     [](NoteLabApp& app) {
                         const ImVec2 from = rectPoint(app.blocksDividerRect, 0.5f, 0.3f);
                         return uiDrag(from, app.blocksDividerRect.z > 20 ? ImVec2(from.x, from.y + 35) : ImVec2(from.x + 90, from.y));
                     },
                     [](NoteLabApp& app) { return app.blocksCodeRatio < 0.37f && uiFind(app, "do.flash") >= 0; }});
    steps.push_back({"Variables es una categoria accesible con su declaracion",
                     [](NoteLabApp& app) { return uiClick(rectPoint(app.canvas.categoryRects[static_cast<size_t>(BlockCategory::Variables)], 0.5f, 0.4f)); },
                     [](NoteLabApp& app) { return app.canvas.paletteRects.count("var.define") && app.canvas.paletteRects.count("var.set"); }});
    steps.push_back({"la declaracion numerica se agrega como raiz del tipo",
                     [](NoteLabApp&) { UiFrame marker; marker.text = "\x02" + std::string("var.define"); return std::vector<UiFrame>{marker}; },
                     [](NoteLabApp& app) { const BlockProgram* p = uiProgram(app); return p && blockVariables(*p).size() == 1; }});
    steps.push_back({"fijar variable elige su identidad y se conecta a un acierto",
                     [](NoteLabApp&) { UiFrame marker; marker.text = "\x02" + std::string("var.set"); return std::vector<UiFrame>{marker}; },
                     [](NoteLabApp& app) { const BlockProgram* p = uiProgram(app); const int set = uiFind(app, "var.set"); return p && set >= 0 && referencedVariable(*p, p->node(set)->args[0].value) && placeOf(*p, set) == kPlaceHit; }});
    return steps;
}

// Antes de cada cuadro: el siguiente evento del paso en curso.
// --ui-test=score: el marcador de «Jugar» vuelve a cero en cada pasada nueva
// y a mano, y el final de una pasada jugada se para con el resultado hasta
// reproducir otra vez (pedido del autor del 30 sep: «nunca se reinician»).
std::vector<UiStep> scoreUiSteps() {
    // Lo que dejaria una pasada a medias.
    auto fill = [](NoteLabApp& app) {
        app.counts = {0, 7, 3, 2, 1, 4};
        app.state.combo = 5;
        app.maxCombo = 9;
        app.lastJudgement = Judgement::Good;
    };
    auto zero = [](const NoteLabApp& app) {
        return std::all_of(app.counts.begin(), app.counts.end(), [](int c) { return c == 0; }) && app.state.combo == 0 &&
               app.maxCombo == 0 && app.lastJudgement == Judgement::None;
    };
    auto idle = [](int n) { return std::vector<UiFrame>(static_cast<size_t>(n)); };
    auto key = [](ImGuiKey k) {
        std::vector<UiFrame> out(2);
        out[0].key = k;
        out[0].keyDown = 1;
        out[1].key = k;
        out[1].keyDown = 0;
        return out;
    };
    std::vector<UiStep> steps;
    steps.push_back({"empezar a jugar pone el marcador a cero",
                     [=](NoteLabApp& app) { fill(app); app.manual = true; app.playSide = 1; app.playing = false; return idle(4); },
                     [=](NoteLabApp& app) { return zero(app); }});
    steps.push_back({"en la misma pasada, el marcador se queda",
                     [=](NoteLabApp& app) { fill(app); return idle(4); },
                     [](NoteLabApp& app) { return app.counts[1] == 7 && app.state.combo == 5 && app.maxCombo == 9; }});
    steps.push_back({"saltar a otro momento lo pone a cero",
                     [=](NoteLabApp& app) { seekTo(app, 3000.0); return idle(3); },
                     [=](NoteLabApp& app) { return zero(app); }});
    steps.push_back({"cambiar de lado lo pone a cero",
                     [=](NoteLabApp& app) { fill(app); app.playSide = 0; return idle(3); },
                     [=](NoteLabApp& app) { return zero(app); }});
    steps.push_back({"volver al lado del jugador tambien es otra pasada",
                     [=](NoteLabApp& app) { fill(app); app.playSide = 1; return idle(3); },
                     [=](NoteLabApp& app) { return zero(app); }});
    steps.push_back({"otro chart (el patron de prueba) lo pone a cero",
                     [=](NoteLabApp& app) { fill(app); chooseDemo(app); return idle(3); },
                     [=](NoteLabApp& app) { return zero(app); }});
    steps.push_back({"al acabar una pasada jugada se para con el resultado a la vista",
                     [=](NoteLabApp& app) {
                         fill(app);
                         app.state.songMs = app.patternMs + 10.0;
                         app.playing = true;
                         return idle(3);
                     },
                     [](NoteLabApp& app) { return app.finished && !app.playing && app.counts[1] == 7 && app.maxCombo == 9; }});
    steps.push_back({"Espacio tras el final: otra pasada desde el principio, con el marcador a cero",
                     [=](NoteLabApp&) { return key(ImGuiKey_Space); },
                     [](NoteLabApp& app) {
                         return !app.finished && app.playing && app.state.songMs < 1500.0 && app.counts[1] == 0 && app.maxCombo == 0;
                     }});
    steps.push_back({"«A cero» a mano: el marcador a cero sin parar la cancion",
                     [=](NoteLabApp& app) {
                         fill(app);
                         const ImVec4 r = app.resetScoreRect;
                         return uiClick(ImVec2(r.x + r.z * 0.5f, r.y + r.w * 0.5f));
                     },
                     [=](NoteLabApp& app) { return app.resetScoreRect.z > 0.0f && zero(app) && app.playing; }});
    return steps;
}

// --ui-test=tutorial: el aviso discreto de una zona, su franja, la sombra y el
// aviso que se queda, Esc (con la sombra, sin ella y escribiendo en un campo),
// saltar sin que vuelva a preguntar, empezarla desde el menu y cerrarse sola
// al terminar; con la ruta general en marcha, la guia de bloques espera y la
// paleta ensena solo el bundle; «Juega tu» vuelve a automatico con tres
// aciertos. Las demas pruebas apagan el tutorial; esta lo mira a el.
std::vector<UiStep> tutorialUiSteps() {
    auto idle = [](int n) { return std::vector<UiFrame>(static_cast<size_t>(n)); };
    auto key = [](ImGuiKey k) {
        std::vector<UiFrame> out(2);
        out[0].key = k;
        out[0].keyDown = 1;
        out[1].key = k;
        out[1].keyDown = 0;
        return out;
    };
    auto wait = [](double seconds) {
        UiFrame f;
        f.text = "\x03" "wait=" + std::to_string(seconds);
        return std::vector<UiFrame>{f};
    };
    auto center = [](const ImRect& r) { return ImVec2((r.Min.x + r.Max.x) * 0.5f, (r.Min.y + r.Max.y) * 0.5f); };
    auto mission = [](const char* name) {
        for (int i = 0; i < kTutorialCount; ++i)
            if (std::string(kTutorialMissions[i].key) == name) return i;
        return -1;
    };
    auto name = [](const NoteLabApp& app) { return std::string(app.createHud.name.data()); };
    auto shared = std::make_shared<std::string>();
    std::vector<UiStep> steps;
    steps.push_back({"entrar por primera vez en Crear HUD lo ofrece con un aviso discreto",
        [=](NoteLabApp& app) { openCreateHud(app); return idle(8); },
        [](NoteLabApp& app) {
            return g_tutorial.lastAsk == "hud" && tutorialAreaState(app, kTrackHud) == 0 && !g_tutorial.stripVisible &&
                   GImGui->OpenPopupStack.Size == 1;
        }});
    steps.push_back({"empezar pone su franja y la sombra sobre la primera mision",
        [=](NoteLabApp&) { return uiClick(center(g_tutorial.askAcceptRect)); },
        [=](NoteLabApp& app) {
            return tutorialAreaState(app, kTrackHud) == 1 && g_tutorial.stripVisible && g_tutorial.area == kTrackHud &&
                   g_tutorial.lastAsk != "hud" && tutorialCurrent(app, kTrackHud) == mission("hud-base") && g_tutorial.dimShown;
        }});
    steps.push_back({"saltar mision pasa a la siguiente",
        [=](NoteLabApp&) { return uiClick(center(g_tutorial.stripNextRect)); },
        [=](NoteLabApp& app) { return app.tutorialDone.count("hud-base") > 0 && tutorialCurrent(app, kTrackHud) == mission("hud-colors"); }});
    steps.push_back({"Esc quita la sombra, deja el aviso y no cierra la ventana",
        [=](NoteLabApp&) { return uiThen(idle(4), key(ImGuiKey_Escape)); },
        [](NoteLabApp&) {
            return g_tutorial.focusDismissed[kTrackHud] && g_tutorial.area == kTrackHud && !g_tutorial.dimShown && g_tutorial.hintShown;
        }});
    steps.push_back({"Esc sin sombra cierra la ventana",
        [=](NoteLabApp&) { return key(ImGuiKey_Escape); },
        [](NoteLabApp&) { return g_tutorial.area == -1; }});
    steps.push_back({"volver a entrar sigue el tutorial sin preguntar",
        [=](NoteLabApp& app) { openCreateHud(app); return idle(8); },
        [](NoteLabApp&) { return g_tutorial.lastAsk != "hud" && g_tutorial.stripVisible && g_tutorial.area == kTrackHud; }});
    steps.push_back({"un clic en el control quita la sombra pero el aviso se queda",
        [=](NoteLabApp& app) {
            tutorialShow(app, kTutorialMissions[mission("hud-colors")]);
            *shared = name(app);
            const auto field = g_tutorial.marks.find("hud-name");
            std::vector<UiFrame> frames = uiThen(wait(0.8), uiClick(field == g_tutorial.marks.end() ? ImVec2(-1.0f, -1.0f) : center(field->second)));
            UiFrame text;
            text.text = "x";
            frames.push_back(text);
            return frames;
        },
        [=](NoteLabApp& app) { return name(app) == *shared + "x" && !g_tutorial.dimShown && g_tutorial.hintShown; }});
    steps.push_back({"Esc mientras se escribe cancela el campo y no cierra la ventana",
        [=](NoteLabApp&) { return key(ImGuiKey_Escape); },
        [](NoteLabApp&) { return g_tutorial.area == kTrackHud; }});
    steps.push_back({"saltar tutorial lo quita sin cerrar la ventana",
        [=](NoteLabApp&) { return uiClick(center(g_tutorial.stripLeaveRect)); },
        [](NoteLabApp& app) { return tutorialAreaState(app, kTrackHud) == 2 && !g_tutorial.stripVisible && g_tutorial.area == kTrackHud; }});
    steps.push_back({"cerrar la ventana con Esc",
        [=](NoteLabApp&) { return key(ImGuiKey_Escape); },
        [](NoteLabApp&) { return g_tutorial.area == -1; }});
    steps.push_back({"saltado, volver a entrar ya no pregunta ni ensena la franja",
        [=](NoteLabApp& app) { openCreateHud(app); return idle(8); },
        [](NoteLabApp&) { return g_tutorial.lastAsk != "hud" && !g_tutorial.stripVisible && g_tutorial.area == kTrackHud; }});
    steps.push_back({"el menu Tutorial la empieza otra vez desde el principio",
        [=](NoteLabApp& app) { startTutorial(app, kTrackHud, true); return idle(8); },
        [=](NoteLabApp& app) {
            return tutorialAreaState(app, kTrackHud) == 1 && tutorialDoneCount(app, kTrackHud) == 0 && g_tutorial.stripVisible &&
                   tutorialCurrent(app, kTrackHud) == mission("hud-base");
        }});
    steps.push_back({"terminada la zona, su franja se cierra sola",
        [=](NoteLabApp&) {
            std::vector<UiFrame> frames;
            for (int i = 0; i < 5; ++i) frames = uiThen(uiThen(frames, uiClick(center(g_tutorial.stripNextRect))), idle(3));
            return uiThen(frames, wait(4.2));
        },
        [](NoteLabApp& app) { return tutorialAreaState(app, kTrackHud) == 2 && !g_tutorial.stripVisible && g_tutorial.area == kTrackHud; }});
    steps.push_back({"cerrar la ventana",
        [=](NoteLabApp&) { return key(ImGuiKey_Escape); },
        [](NoteLabApp&) { return g_tutorial.area == -1; }});
    steps.push_back({"con la ruta general en marcha, en Bloques no salta la guia de bloques",
        [=](NoteLabApp& app) {
            startTutorial(app, kTrackGeneral, false);
            g_tutorial.focus[kTrackGeneral] = mission("distribute");
            app.requestedTab = 1;
            app.typesView = 1;
            return idle(10);
        },
        [](NoteLabApp& app) {
            return g_tutorial.lastAsk.empty() && !g_tutorial.cardVisible && g_tutorial.windowVisible && tutorialAreaState(app, kTrackBlocks) == 0;
        }});
    steps.push_back({"en Distribuir sin cancion, la mision pide elegirla alli mismo",
        [=](NoteLabApp& app) {
            chooseDemo(app);
            app.typesView = 2;
            return idle(8);
        },
        [](NoteLabApp&) { return g_tutorial.marks.count("distribute-song") > 0 && g_tutorial.hintShown; }});
    steps.push_back({"en «Juega tu», jugar no cambia nada hasta el tercer acierto",
        [=](NoteLabApp& app) {
            g_tutorial.focus[kTrackGeneral] = mission("play");
            app.requestedTab = 0;
            app.manual = true;
            return idle(6);
        },
        [](NoteLabApp& app) { return app.manual && !app.tutorialDone.count("play"); }});
    steps.push_back({"tres aciertos devuelven el modo automatico y el tutorial sigue",
        [=](NoteLabApp& app) {
            app.counts[static_cast<size_t>(Judgement::Sick)] = 3;
            return idle(4);
        },
        [](NoteLabApp& app) { return !app.manual && app.tutorialDone.count("play") > 0 && app.tutorialOpen; }});
    steps.push_back({"repetir desde el principio no da por hecho lo que ya se cumplia",
        [=](NoteLabApp& app) {
            startTutorial(app, kTrackGeneral, true);
            return idle(4);
        },
        [=](NoteLabApp& app) {
            return tutorialCurrent(app, kTrackGeneral) == mission("open-mod") && !app.tutorialDone.count("open-mod") &&
                   tutorialAlreadyDone("open-mod") && app.tutorialOpen;
        }});
    steps.push_back({"sin la ruta general, Bloques ofrece la guia con un aviso",
        [=](NoteLabApp& app) {
            skipTutorial(app);
            app.requestedTab = 1;
            app.typesView = 1;
            return idle(10);
        },
        [](NoteLabApp&) { return g_tutorial.lastAsk == "blocks"; }});
    steps.push_back({"empezar la guia: la paleta ensena solo el bundle de la mision",
        [=](NoteLabApp&) { return uiThen(uiClick(center(g_tutorial.askAcceptRect)), idle(8)); },
        [](NoteLabApp& app) {
            const auto& shown = app.canvas.paletteRects;
            return g_tutorial.cardVisible && app.canvas.onlyBlocks == std::vector<std::string>{"event.hit", "do.health"} &&
                   shown.size() == 2 && shown.count("event.hit") && shown.count("do.health");
        }});
    steps.push_back({"la mision senala el bloque concreto de la paleta",
        [=](NoteLabApp&) { return idle(4); },
        [](NoteLabApp&) { return g_tutorial.marks.count("pal:do.health") > 0 && g_tutorial.dimShown && g_tutorial.hintShown; }});
    steps.push_back({"saltar el aviso de la guia la oculta y no vuelve a preguntar",
        [=](NoteLabApp& app) {
            app.tutorialAreas.erase("blocks");
            app.tutorialBlocksHidden = true;
            return idle(6);
        },
        [](NoteLabApp&) { return g_tutorial.lastAsk == "blocks"; }});
    steps.push_back({"... y la paleta vuelve a tenerlos todos",
        [=](NoteLabApp&) { return uiThen(uiClick(center(g_tutorial.askSkipRect)), idle(6)); },
        [](NoteLabApp& app) {
            return tutorialAreaState(app, kTrackBlocks) == 2 && app.tutorialBlocksHidden && g_tutorial.lastAsk.empty() &&
                   app.canvas.onlyBlocks.empty() && app.canvas.paletteRects.size() > 10;
        }});
    return steps;
}

// --ui-test=notes: crear una nota con el asistente (con bloques y solo codigo),
// la vista Bloques sin nota elegida y el editor de sprites (pincel, figura,
// usarlo como aspecto), con el raton y el teclado simulados.
std::vector<UiStep> notesUiSteps() {
    auto idle = [](int n) { return std::vector<UiFrame>(static_cast<size_t>(n)); };
    auto center = [](const ImRect& r) { return ImVec2((r.Min.x + r.Max.x) * 0.5f, (r.Min.y + r.Max.y) * 0.5f); };
    auto mark = [](const char* key) {
        const auto found = g_tutorial.marks.find(key);
        return found == g_tutorial.marks.end() ? ImRect() : found->second;
    };
    auto typed = [](const char* text) {
        UiFrame f;
        f.text = text;
        return std::vector<UiFrame>{f};
    };
    auto ink = [](const Image& image) {
        int count = 0;
        for (size_t i = 3; i < image.rgba.size(); i += 4) count += image.rgba[i] > 0 ? 1 : 0;
        return count;
    };
    auto shared = std::make_shared<int>(0);
    std::vector<UiStep> steps;
    steps.push_back({"el asistente se abre desde el Catalogo",
        [=](NoteLabApp& app) { app.requestedTab = 1; app.typesView = 0; openNewNote(app); return idle(8); },
        [](NoteLabApp&) { return g_tutorial.marks.count("newnote-name") > 0 && g_tutorial.marks.count("newnote-create") > 0; }});
    steps.push_back({"con nombre, la crea en el Catalogo y la ventana pasa a «Lista» sin abrir nada",
        [=](NoteLabApp&) { return uiThen(uiThen(uiThen(uiThen(idle(6), typed("Nueva")), idle(4)), uiClick(center(mark("newnote-create")))), idle(4)); },
        [](NoteLabApp& app) {
            const Source* source = app.blocksSource >= 0 ? app.sources[static_cast<size_t>(app.blocksSource)].get() : nullptr;
            return source && source->typeBlocks.count("Nueva") && app.newNote.done && app.typesView == 0 &&
                   g_tutorial.marks.count("newnote-next-blocks") > 0 && GImGui->OpenPopupStack.Size == 1;
        }});
    steps.push_back({"«Editar sus bloques» lleva a sus bloques",
        [=](NoteLabApp&) { return uiThen(uiClick(center(mark("newnote-next-blocks"))), idle(4)); },
        [](NoteLabApp& app) { return app.blocksType == "Nueva" && app.typesView == 1 && GImGui->OpenPopupStack.Size == 0; }});
    steps.push_back({"«solo codigo» se elige en el asistente",
        [=](NoteLabApp& app) { openNewNote(app, 1); return uiThen(idle(6), typed("Script")); },
        [](NoteLabApp& app) { return app.newNote.mode == 1 && std::string(app.newNote.name.data()) == "Script"; }});
    steps.push_back({"«solo codigo» crea su script y abre el codigo",
        [=](NoteLabApp&) { return uiThen(uiClick(center(mark("newnote-create"))), idle(4)); },
        [](NoteLabApp& app) {
            const Source* source = app.blocksSource >= 0 ? app.sources[static_cast<size_t>(app.blocksSource)].get() : nullptr;
            const auto found = source ? source->typeBlocks.find("Script") : decltype(source->typeBlocks.end()){};
            return source && found != source->typeBlocks.end() && codeOnlyType(found->second) && app.blocksView == 1 && app.blocksType == "Script";
        }});
    steps.push_back({"sin nota elegida, Bloques no deja tocar nada",
        [=](NoteLabApp& app) { app.blocksType.clear(); app.blocksView = 0; return idle(6); },
        [](NoteLabApp&) { return g_tutorial.marks.count("blocks-choose") > 0 && g_tutorial.marks.count("blocks-canvas") == 0; }});
    steps.push_back({"el editor de sprites pregunta antes como empezar",
        [=](NoteLabApp& app) { openSpriteEditor(app, app.blocksSource >= 0 ? app.blocksSource : 0, "Nueva"); return idle(8); },
        [](NoteLabApp& app) {
            return app.spriteEditor.choosing && g_tutorial.marks.count("sprite-start-1") > 0 && g_tutorial.marks.count("sprite-start-2") > 0 &&
                   g_tutorial.marks.count("sprite-canvas") == 0;
        }});
    steps.push_back({"un lienzo libre de 32x32 se abre en su pestana",
        [=](NoteLabApp&) {
            return uiThen(uiThen(uiThen(uiClick(center(mark("sprite-start-2"))), idle(3)), uiThen(uiClick(center(mark("sprite-start-size-32"))), idle(3))),
                          uiThen(uiClick(center(mark("sprite-start-go"))), idle(6)));
        },
        [](NoteLabApp& app) {
            const auto& e = app.spriteEditor;
            return !e.choosing && e.docSingle && e.docFreeW == 32 && e.docFreeH == 32 && e.docPixel && e.docs.size() == 2 &&
                   g_tutorial.marks.count("sprite-canvas") > 0;
        }});
    steps.push_back({"lo pintado en el lienzo libre pasa a la hoja, ajustado al hueco de la nota",
        [=](NoteLabApp& app) {
            auto& e = app.spriteEditor;
            spritePushStroke(e);
            spritePaintPreset(e.layers.back().image, SpritePreset::Arrow, {0, 0, 32, 32}, true);
            ++e.generation;
            return uiThen(idle(2), uiThen(uiClick(center(mark("sprite-insert"))), idle(6)));
        },
        [=](NoteLabApp& app) {
            const auto& e = app.spriteEditor;
            if (e.docSingle || e.doc != 0) return false;
            const SheetLayout layout = spriteLayout(e);
            const SpriteRect cell = layout.cell(layout.rowOf(DrawnPiece::Note), e.slot);
            // Crecio por enteros (32 -> 160): un pixel del lienzo es un bloque de 5x5 del mismo color.
            const Image sheet = composeSpriteLayers(e.layers);
            int inked = 0;
            for (int y = cell.y0; y < cell.y1; ++y)
                for (int x = cell.x0; x < cell.x1; ++x) inked += sheet.at(x, y)[3] > 0 ? 1 : 0;
            return e.row == layout.rowOf(DrawnPiece::Note) && inked > 2000 && e.layers.size() == 3;
        }});
    steps.push_back({"el pincel pinta en el hueco elegido",
        [=](NoteLabApp& app) {
            *shared = ink(app.spriteEditor.layers.back().image);
            app.spriteEditor.tool = 0;
            const ImRect cell = mark("sprite-cell");
            return uiDrag(ImVec2(cell.Min.x + 20.0f, cell.Min.y + 30.0f), ImVec2(cell.Max.x - 20.0f, cell.Min.y + 60.0f));
        },
        [=](NoteLabApp& app) { return ink(app.spriteEditor.layers.back().image) > *shared + 100 && !app.spriteEditor.undo.empty(); }});
    steps.push_back({"deshacer quita la pincelada",
        [=](NoteLabApp& app) { spriteUndo(app.spriteEditor, false); return idle(3); },
        [=](NoteLabApp& app) { return ink(app.spriteEditor.layers.back().image) == *shared; }});
    steps.push_back({"una figura arrastrando se pinta al soltar",
        [=](NoteLabApp& app) {
            app.spriteEditor.tool = 2;
            app.spriteEditor.shapeKind = SpriteShapeKind::Star;
            const ImRect cell = mark("sprite-cell");
            return uiDrag(ImVec2(cell.Min.x + 15.0f, cell.Min.y + cell.GetHeight() * 0.45f), ImVec2(cell.Min.x + cell.GetWidth() * 0.6f, cell.Max.y - 10.0f));
        },
        [=](NoteLabApp& app) { return ink(app.spriteEditor.layers.back().image) > *shared + 400; }});
    steps.push_back({"usarlo como su aspecto le da su estilo a la nota",
        [=](NoteLabApp&) { return uiThen(uiClick(center(mark("sprite-use"))), idle(6)); },
        [](NoteLabApp& app) {
            const Source* source = app.blocksSource >= 0 ? app.sources[static_cast<size_t>(app.blocksSource)].get() : nullptr;
            return source && source->typeLooks.count("Nueva") && g_tutorial.marks.count("sprite-canvas") == 0;
        }});
    steps.push_back({"Crear HUD ofrece dibujar sus piezas",
        [=](NoteLabApp& app) {
            // De partida, el skin del mod (no el aspecto que se acaba de dibujar).
            if (app.selSource >= 0)
                for (size_t i = 0; i < app.sources[static_cast<size_t>(app.selSource)]->catalog.styles.size(); ++i)
                    if (app.sources[static_cast<size_t>(app.selSource)]->catalog.styles[i].use == StyleUse::Default) {
                        selectStyle(app, app.selSource, static_cast<int>(i));
                        break;
                    }
            openCreateHud(app);
            return idle(10);
        },
        [](NoteLabApp& app) { return g_tutorial.marks.count("hud-step-3") > 0 && app.createHud.step == 0 && !app.createHud.useDrawn; }});
    steps.push_back({"el paso «Piezas» ofrece dibujarlas",
        [=](NoteLabApp&) { return uiThen(uiClick(center(mark("hud-step-3"))), idle(4)); },
        [](NoteLabApp& app) { return app.createHud.step == 3 && g_tutorial.marks.count("hud-shape") > 0; }});
    steps.push_back({"«Dibujadas por mi» enseña las piezas en tarjetas, sin abrir el editor",
        [=](NoteLabApp&) {
            const ImRect shape = mark("hud-shape");
            return uiThen(uiClick(ImVec2(shape.Max.x - 24.0f, (shape.Min.y + shape.Max.y) * 0.5f)), idle(6));
        },
        [](NoteLabApp& app) {
            return app.createHud.useDrawn && g_tutorial.marks.count("hud-piece-0") > 0 && g_tutorial.marks.count("hud-piece-4") > 0 &&
                   GImGui->OpenPopupStack.Size == 1;
        }});
    steps.push_back({"la tarjeta de la nota abre el editor encima, que pregunta como empezar",
        [=](NoteLabApp&) { return uiThen(uiClick(center(mark("hud-piece-0"))), idle(8)); },
        [](NoteLabApp& app) {
            return app.spriteEditor.target == 1 && app.spriteEditor.choosing && g_tutorial.marks.count("sprite-start-go") > 0 &&
                   GImGui->OpenPopupStack.Size == 2;
        }});
    steps.push_back({"con la plantilla, la hoja general con sus huecos",
        [=](NoteLabApp&) { return uiThen(uiClick(center(mark("sprite-start-go"))), idle(6)); },
        [](NoteLabApp& app) { return !app.spriteEditor.choosing && !app.spriteEditor.docSingle && g_tutorial.marks.count("sprite-canvas") > 0; }});
    steps.push_back({"se pinta la nota en su hueco de la hoja general",
        [=](NoteLabApp& app) {
            *shared = ink(app.spriteEditor.layers.back().image);
            app.spriteEditor.tool = 0;
            const ImRect cell = mark("sprite-cell");
            return uiThen(uiDrag(ImVec2(cell.Min.x + 20.0f, cell.Max.y - 40.0f), ImVec2(cell.Max.x - 20.0f, cell.Max.y - 25.0f)), idle(4));
        },
        [=](NoteLabApp& app) { return ink(app.spriteEditor.layers.back().image) > *shared + 100; }});
    auto splashRow = [](NoteLabApp& app) { return spriteLayout(app.spriteEditor).rowOf(DrawnPiece::Splash); };
    auto usedCell = [](NoteLabApp& app, int row, int slot) {
        const size_t index = static_cast<size_t>(row * kSheetSlots + slot);
        return index < app.spriteEditor.used.size() && app.spriteEditor.used[index] != 0;
    };
    steps.push_back({"un preset de salpicadura va a su fila en una capa nueva",
        [=](NoteLabApp& app) {
            *shared = static_cast<int>(app.spriteEditor.layers.size());
            app.spriteEditor.selection = {};
            return uiThen(uiClick(center(mark("sprite-preset-7"))), idle(6));
        },
        [=](NoteLabApp& app) {
            return static_cast<int>(app.spriteEditor.layers.size()) == *shared + 1 && usedCell(app, splashRow(app), 0) &&
                   app.spriteEditor.row == splashRow(app);
        }});
    steps.push_back({"copiar el hueco y pegarlo en uno libre crea otro fotograma",
        [=](NoteLabApp& app) {
            spriteSelectCell(app.spriteEditor, splashRow(app), 0);
            spriteCopy(app, false);
            return uiThen(idle(2), uiThen(uiClick(center(mark("sprite-paste-free"))), idle(6)));
        },
        [=](NoteLabApp& app) { return usedCell(app, splashRow(app), 1) && !app.spriteEditor.clipboard.empty(); }});
    steps.push_back({"un dibujo suelto del sostenido se abre en su pestana",
        [=](NoteLabApp& app) { spriteNewSingle(app, DrawnPiece::HoldPiece, false); return idle(6); },
        [](NoteLabApp& app) {
            return app.spriteEditor.docSingle && app.spriteEditor.docs.size() == 2 && g_tutorial.marks.count("sprite-insert") > 0;
        }});
    steps.push_back({"«Pasar a la hoja general» lo pone en su fila",
        [=](NoteLabApp&) { return uiThen(uiClick(center(mark("sprite-insert"))), idle(6)); },
        [=](NoteLabApp& app) {
            const int row = spriteLayout(app.spriteEditor).rowOf(DrawnPiece::HoldPiece);
            return app.spriteEditor.doc == 0 && !app.spriteEditor.docSingle && row >= 0 && usedCell(app, row, 0);
        }});
    steps.push_back({"«Usar en el HUD» vuelve a Crear HUD con las piezas y sus fotogramas",
        [=](NoteLabApp&) { return uiThen(uiClick(center(mark("sprite-use"))), idle(8)); },
        [](NoteLabApp& app) {
            const auto& d = app.createHud.drawn;
            return !d[static_cast<size_t>(DrawnPiece::Note)].empty() && d[static_cast<size_t>(DrawnPiece::Splash)].size() == 2 &&
                   !d[static_cast<size_t>(DrawnPiece::HoldPiece)].empty() && g_tutorial.marks.count("sprite-canvas") == 0 &&
                   g_tutorial.marks.count("hud-create") > 0 && GImGui->OpenPopupStack.Size == 1;
        }});
    steps.push_back({"crear el HUD pone lo dibujado en un solo atlas y lo guarda en su receta",
        [=](NoteLabApp&) {
            UiFrame wait;
            wait.text = "\x03" "wait=2.5";
            return uiThen(uiThen(std::vector<UiFrame>{wait}, uiClick(center(mark("hud-create")))), idle(8));
        },
        [](NoteLabApp& app) {
            const NoteStyle* style = selectedStyle(app);
            if (!style || app.selSource < 0 || GImGui->OpenPopupStack.Size != 0) return false;
            const Source& source = *app.sources[static_cast<size_t>(app.selSource)];
            const auto recipe = source.recipes.find("arrows:" + style->id);
            int notes = 0, splashes = 0, holds = 0;
            for (const PartBinding& b : style->parts) {
                if (b.sheet < 0 || style->sheets[static_cast<size_t>(b.sheet)].declared != "notelab/drawn") continue;
                notes += b.part == Part::Note;
                splashes += b.part == Part::Splash;
                holds += b.part == Part::HoldPiece;
            }
            return recipe != source.recipes.end() && !recipe->second.arrows.drawn[static_cast<size_t>(DrawnPiece::Note)].empty() &&
                   !recipe->second.arrows.drawn[static_cast<size_t>(DrawnPiece::Splash)].empty() && notes == 4 && splashes == 4 && holds == 4;
        }});
    auto key = [](ImGuiKey k) {
        std::vector<UiFrame> out(2);
        out[0].key = k;
        out[0].keyDown = 1;
        out[1].key = k;
        out[1].keyDown = 0;
        return out;
    };
    steps.push_back({"el dibujo (.nlsprite) vuelve con sus capas al reabrir el HUD",
        [=](NoteLabApp& app) {
            app.spriteEditor.drawn.clear();   // sin la hoja de la sesion: solo el archivo
            openCreateHud(app);
            openHudSpriteEditor(app, DrawnPiece::Note, false);
            return idle(10);
        },
        [](NoteLabApp& app) {
            std::error_code ec;
            return !app.createHud.spriteFile.empty() && fs::exists(pathFromUtf8(app.createHud.spriteFile), ec) && app.spriteEditor.layers.size() >= 4 &&
                   GImGui->OpenPopupStack.Size == 2;
        }});
    steps.push_back({"cerrar el editor y Crear HUD",
        [=](NoteLabApp&) { return uiThen(uiThen(key(ImGuiKey_Escape), idle(4)), uiThen(key(ImGuiKey_Escape), idle(4))); },
        [](NoteLabApp&) { return GImGui->OpenPopupStack.Size == 0; }});
    steps.push_back({"pasar el script de una nota «solo codigo» a bloques va a una nota nueva",
        [=](NoteLabApp& app) {
            Source& source = *app.sources[static_cast<size_t>(app.blocksSource)];
            Engine engine;
            if (!sourceEngine(app, source, engine)) engine = Engine::Codename;
            for (auto& [id, node] : source.typeBlocks["Script"].nodes)
                if (node.key == "code.file" && node.args.size() == 3)
                    node.args[2].value += engine == Engine::Psych ? "\nfunction goodNoteHit(id, d, noteType, s)\n\taddHealth(0.1)\nend\n"
                                        : engine == Engine::VSlice ? "\nfunction onNoteHit(event) {\n\thealth += 0.1;\n}\n"
                                                                   : "\nfunction onPlayerHit(event) {\n\thealth += 0.1;\n}\n";
            importTypeScript(app, app.blocksSource, "Script");
            return idle(8);
        },
        [](NoteLabApp& app) {
            const Source& source = *app.sources[static_cast<size_t>(app.blocksSource)];
            const auto made = source.typeBlocks.find("Script (blocks)");
            const auto original = source.typeBlocks.find("Script");
            bool health = false;
            if (made != source.typeBlocks.end())
                for (const auto& [id, node] : made->second.nodes) health = health || node.key == "do.health";
            return health && original != source.typeBlocks.end() && codeOnlyType(original->second) && app.blocksType == "Script (blocks)" &&
                   g_tutorial.marks.count("blocks-canvas") > 0;
        }});
    return steps;
}

void uiTestFeed(NoteLabApp& app) {
    UiTest& t = g_uiTest;
    if (!t.active || t.done) return;
    ImGuiIO& io = ImGui::GetIO();
    if (++t.framesRun > 4000) {
        std::printf("[FAIL] la prueba de interfaz no termino a tiempo (paso %zu)\n", t.step);
        ++t.failures;
        t.done = true;
        return;
    }
    if (t.lastPos.x >= 0.0f) io.AddMousePosEvent(t.lastPos.x, t.lastPos.y);
    if (t.settle > 0) {
        --t.settle;
        return;
    }
    if (!t.planned) {
        if (t.step >= t.steps.size()) {
            t.done = true;
            return;
        }
        t.frames = t.steps[t.step].plan(app);
        t.frame = 0;
        t.planned = true;
    }
    if (t.frame < t.frames.size()) {
        UiFrame f = t.frames[t.frame++];
        if (f.text.rfind("\x03" "wait=", 0) == 0) {
            if (t.waitUntil < 0.0) t.waitUntil = ImGui::GetTime() + std::atof(f.text.c_str() + 6);
            if (ImGui::GetTime() < t.waitUntil) { --t.frame; SDL_Delay(5); return; }
            t.waitUntil = -1.0;
            return;
        }
        if (f.text == "\x03" "audio-ready") {
            if (app.media.audio && (app.media.startWhenReady || app.media.audio->trackLoadPending())) { --t.frame; SDL_Delay(5); }
            return;
        }
        // Un marcador: arrastrar ese bloque de la paleta (\x01) o hacerle clic (\x02),
        // con su sitio leido ahora que la paleta ya se desplazo.
        if (!f.text.empty() && (f.text[0] == '\x01' || f.text[0] == '\x02')) {
            const std::string key = f.text.substr(1);
            const auto found = app.canvas.paletteRects.find(key);
            const ImVec4 r = found == app.canvas.paletteRects.end() ? ImVec4() : found->second;
            const ImVec2 from(r.x + 10.0f, r.y + 10.0f);
            const std::vector<UiFrame> rest = f.text[0] == '\x01' ? uiDrag(from, f.pos) : uiClick(from);
            t.frames.insert(t.frames.begin() + static_cast<std::ptrdiff_t>(t.frame), rest.begin(), rest.end());
            return;
        }
        if (f.pos.x >= 0.0f) {
            t.lastPos = f.pos;
            io.AddMousePosEvent(f.pos.x, f.pos.y);
        }
        if (f.button >= 0) io.AddMouseButtonEvent(ImGuiMouseButton_Left, f.button == 1);
        if (f.ctrl || f.keyDown >= 0) io.AddKeyEvent(ImGuiMod_Ctrl, f.ctrl && f.keyDown != 0);
        if (f.key != ImGuiKey_None && f.keyDown >= 0) io.AddKeyEvent(f.key, f.keyDown == 1);
        if (!f.text.empty()) io.AddInputCharactersUTF8(f.text.c_str());
        if (t.frame >= t.frames.size()) t.settle = 6;
        return;
    }
    const bool ok = t.steps[t.step].check(app);
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", t.steps[t.step].name);
    if (!ok) {
        ++t.failures;
        std::printf("tutorial debug: area=%d strip=%d track=%d ask=%s dim=%d hint=%d hud=%d state=%d popups=%d marks=%zu\n", g_tutorial.area,
            g_tutorial.stripVisible, g_tutorial.stripTrack, g_tutorial.lastAsk.c_str(), g_tutorial.dimShown, g_tutorial.hintShown,
            tutorialCurrent(app, kTrackHud), tutorialAreaState(app, kTrackHud), GImGui->OpenPopupStack.Size, g_tutorial.marks.size());
        std::printf("media debug: open=%d selected=%s renderer=%d image=%s audio=%d tracks=%zu start=%d loop=%d range=%d speed=%.2f message=%s mouse=(%.0f,%.0f) listen=(%.0f,%.0f,%.0f,%.0f)\n",
            app.media.isOpen, app.media.selected.c_str(), app.rendererReady, app.media.imagePath.c_str(), app.media.audio ? 1 : 0,
            app.media.audio ? app.media.audio->trackCount() : 0, app.media.startWhenReady, app.media.audioLoop, app.media.audioRange,
            app.media.audioSpeed, app.media.message.c_str(), io.MousePos.x, io.MousePos.y,
            app.media.listenRect.x, app.media.listenRect.y, app.media.listenRect.z, app.media.listenRect.w);
    }
    ++t.step;
    t.planned = false;
    t.settle = 3;
}

bool writePng(const fs::path& target, int width, int height, const unsigned char* rgba) {
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    std::vector<unsigned char> bytes;
    const int ok = stbi_write_png_to_func([](void* context, void* data, int size) {
        auto* out = static_cast<std::vector<unsigned char>*>(context);
        const auto* begin = static_cast<const unsigned char*>(data);
        out->insert(out->end(), begin, begin + size);
    }, &bytes, width, height, 4, rgba, width * 4);
    if (!ok) return false;
    std::ofstream file(target, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

std::vector<std::string> utf8Arguments(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        try {
            for (int i = 0; i < count; ++i) out.push_back(fs::path(wide[i]).u8string());
        } catch (...) {
            out.clear();
        }
        LocalFree(wide);
        if (!out.empty()) return out;
    }
#endif
    for (int i = 0; i < argc; ++i) out.push_back(ensureUtf8(argv[i] ? argv[i] : ""));
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    NoteLabApp app;
    // Las pruebas de interfaz empiezan con las preferencias por defecto y ni
    // ellas ni las capturas escriben las de la persona: el resultado no depende
    // de lo que haya guardado (un panel de codigo al minimo tumbaba --ui-test=code)
    // y lo que fijan sus banderas no se queda guardado.
    {
        const std::vector<std::string> early = utf8Arguments(argc, argv);
        const auto has = [&](const char* prefix) {
            return std::any_of(early.begin(), early.end(), [&](const std::string& a) { return a.rfind(prefix, 0) == 0; });
        };
        if (!has("--ui-test=")) loadSettings(app);
        if (has("--ui-test=") || has("--capture=")) app.transient = true;
    }
    std::vector<fs::path> requestedRoots;
    fs::path requestedBase;
    std::string requestedSelect, requestedSong, requestedDifficulty, requestedEdit, requestedType, requestedProject, requestedSave;
    double requestedSeek = -1.0;
    int requestedAssetPart = -1;
    int requestedHud = -1;
    std::string requestedDistribute;
    std::string requestedChartSave;
    int windowWidth = 1480, windowHeight = 880;
    bool requestedVariant = false, requestedComposer = false;
    // Crear sin dibujar (§31): abrir los dialogos y, con --commit-create,
    // confirmar en cuanto esten listos.
    std::string requestedCreateHud, requestedLookType, requestedLookPreset, requestedLookFiles;
    bool requestedRating = false;
    bool requestedCustom = false, requestedCustomRanking = false;
    std::string requestedCustomFiles, requestedBot;
    bool requestedExport = false, exportZip = false, exportNoLook = false;
    std::string exportEngine, exportRole, exportNameArg, exportType, exportTo;
    std::string requestedHudList, requestedBorrow, requestedPresets;
    int requestedNewNote = -1;
    std::string requestedSprite;
    int requestedHudDrawn = 0, requestedSpritePreset = -1, requestedSpriteSingle = -1, requestedSpriteFocus = 0, requestedSpriteFinal = -1;
    // El editor de sprites en las capturas: su pantalla de inicio (--sprite-start[=0..3]) o un lienzo libre (--sprite-free=32x32[p]).
    int requestedSpriteStart = -2, requestedFreeW = 0, requestedFreeH = 0;
    bool requestedFreePixel = false;
    std::vector<int> requestedSpritePresets;
    int requestedSpriteTemplate = -1, requestedSpriteTool = -1;
    std::string requestedNewNoteName;
    bool requestedNewNoteDone = false;
    int requestedHudStep = -1;
    bool requestedCatalog = false;
    std::string requestedMedia;
    int requestedSoundFilter = -1;
    std::string requestedImportScript, requestedSpriteColors, requestedPreviewReceptors;
    const std::vector<std::string> arguments = utf8Arguments(argc, argv);
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string& a = arguments[i];
        if (a.rfind("--root=", 0) == 0) requestedRoots.push_back(pathFromUtf8(a.substr(7)));
        else if (a.rfind("--base=", 0) == 0) requestedBase = pathFromUtf8(a.substr(7));
        else if (a == "--no-auto-base") app.autoBase = false;
        else if (a.rfind("--engine=", 0) == 0) {
            const std::string e = a.substr(9);
            app.engineChoice = e == "codename" ? 1 : e == "psych" ? 2 : e == "vslice" ? 3 : 0;
        }
        else if (a.rfind("--select=", 0) == 0) requestedSelect = a.substr(9);
        else if (a == "--mod-resources") requestedMedia = "*";
        else if (a.rfind("--mod-resources=", 0) == 0) requestedMedia = a.substr(16);
        // Capturas: el filtro de sonidos, pasar un script a bloques, la paleta propia y otros receptores.
        else if (a.rfind("--sound-filter=", 0) == 0) requestedSoundFilter = std::atoi(a.c_str() + 15);
        else if (a.rfind("--import-script=", 0) == 0) requestedImportScript = a.substr(16);
        else if (a.rfind("--sprite-colors=", 0) == 0) requestedSpriteColors = a.substr(16);
        else if (a.rfind("--preview-receptors=", 0) == 0) requestedPreviewReceptors = a.substr(20);
        else if (a.rfind("--song=", 0) == 0) requestedSong = lowerText(a.substr(7));
        else if (a.rfind("--difficulty=", 0) == 0) requestedDifficulty = lowerText(a.substr(13));
        else if (a.rfind("--edit-prefix=", 0) == 0) requestedEdit = a.substr(14);
        else if (a.rfind("--capture=", 0) == 0) {
            app.capturePath = a.substr(10);
            app.headless = true;
        }
        else if (a.rfind("--write-sounds=", 0) == 0) return nlblocks::writeCues(a.substr(15)) ? 0 : 1;
        else if (a == "--ui-test=blocks") {
            app.headless = true;
            g_uiTest.active = true;
            g_uiTest.steps = blocksUiSteps();
            g_uiTest.settle = 40;
            app.typesView = 1;
            app.requestedTab = 1;
            app.blocksView = 0;
        }
        else if (a == "--ui-test=code") {
            app.headless = true;
            g_uiTest.active = true;
            g_uiTest.steps = codeUiSteps();
            g_uiTest.settle = 40;
            app.typesView = 1;
            app.requestedTab = 1;
            app.blocksView = 2;
        }
        else if (a == "--ui-test=resources") {
            app.headless = true;
            g_uiTest.active = true;
            g_uiTest.steps = resourcesUiSteps();
            g_uiTest.settle = 40;
            app.typesView = 1; app.requestedTab = 1; app.blocksView = 2;
            app.leftPanel = 0; app.resourceSidebar.page = 0; app.resourceSidebar.category = -1;
        }
        else if (a == "--ui-test=audition") {
            app.headless = true;
            g_uiTest.active = true;
            g_uiTest.steps = auditionUiSteps();
            g_uiTest.settle = 40;
            app.typesView = 1; app.requestedTab = 1; app.blocksView = 2;
        }
        else if (a == "--ui-test=notes") {
            app.headless = true;
            g_uiTest.active = true;
            g_uiTest.steps = notesUiSteps();
            g_uiTest.settle = 40;
        }
        else if (a == "--ui-test=tutorial") {
            app.headless = true;
            g_uiTest.active = true;
            g_uiTest.steps = tutorialUiSteps();
            g_uiTest.settle = 40;
            g_tutorial.testing = true;
        }
        else if (a == "--ui-test=score") {
            app.headless = true;
            g_uiTest.active = true;
            g_uiTest.steps = scoreUiSteps();
            g_uiTest.settle = 40;
            app.requestedTab = 0;
        }
        else if (a.rfind("--capture-frames=", 0) == 0) app.captureFrames = std::clamp(std::atoi(a.c_str() + 17), 5, 5000);
        else if (a.rfind("--seek=", 0) == 0) requestedSeek = std::atof(a.c_str() + 7);
        else if (a == "--downscroll") app.settings.downscroll = true;
        else if (a == "--lang=es") app.spanish = true;
        else if (a == "--lang=en") app.spanish = false;
        else if (a == "--manual") app.manual = true;
        else if (a == "--assets") app.requestedTab = 2;
        else if (a == "--resources") app.requestedTab = 3;
        else if (a == "--types") app.requestedTab = 1;
        else if (a.rfind("--type=", 0) == 0) { requestedType = lowerText(a.substr(7)); app.requestedTab = 1; }
        else if (a == "--findings") app.showFindings = true;
        else if (a == "--hud-edit") app.hudEdit = true;
        else if (a.rfind("--project=", 0) == 0) requestedProject = a.substr(10);
        else if (a.rfind("--edit-hud=", 0) == 0) {
            requestedHudList = a.substr(11);
            requestedHud = std::atoi(a.c_str() + 11);
        }
        else if (a.rfind("--borrow-hud=", 0) == 0) requestedBorrow = a.substr(13);
        else if (a == "--new-variant") requestedVariant = true;
        else if (a == "--combine-styles") requestedComposer = true;
        else if (a == "--create-hud") requestedCreateHud = "pastel";
        // Crear HUD con la forma dibujada (una plantilla), y el editor abierto encima.
        else if (a == "--hud-drawn") requestedHudDrawn = 1;
        else if (a == "--hud-draw") requestedHudDrawn = 2;
        else if (a == "--hud-sheet") requestedHudDrawn = 3;
        else if (a.rfind("--sprite-preset=", 0) == 0) {
            // Uno o varios, separados por comas, en ese orden.
            std::stringstream list(a.substr(16));
            std::string one;
            while (std::getline(list, one, ',')) requestedSpritePresets.push_back(std::atoi(one.c_str()));
            requestedSpritePreset = requestedSpritePresets.empty() ? -1 : requestedSpritePresets.front();
        }
        else if (a.rfind("--sprite-focus=", 0) == 0) requestedSpriteFocus = std::atoi(a.c_str() + 15);
        else if (a.rfind("--sprite-final=", 0) == 0) requestedSpriteFinal = std::atoi(a.c_str() + 15);
        else if (a.rfind("--sprite-single=", 0) == 0) requestedSpriteSingle = std::atoi(a.c_str() + 16);
        else if (a == "--sprite-start") requestedSpriteStart = -1;
        else if (a.rfind("--sprite-start=", 0) == 0) requestedSpriteStart = std::clamp(std::atoi(a.c_str() + 15), 0, 3);
        else if (a.rfind("--sprite-free=", 0) == 0) {
            const std::string size = a.substr(14);
            const size_t x = size.find('x');
            requestedFreeW = std::atoi(size.c_str());
            requestedFreeH = x == std::string::npos ? requestedFreeW : std::atoi(size.c_str() + x + 1);
            requestedFreePixel = !size.empty() && size.back() == 'p';
        }
        // El asistente «Nueva nota custom» (capturas): con bloques o solo codigo, y un nombre.
        else if (a.rfind("--draw-sprite=", 0) == 0) requestedSprite = a.substr(14);
        // Capturas del editor: con que plantilla empieza y que herramienta tiene.
        else if (a.rfind("--sprite-template=", 0) == 0) requestedSpriteTemplate = std::atoi(a.c_str() + 18);
        else if (a.rfind("--sprite-tool=", 0) == 0) requestedSpriteTool = std::atoi(a.c_str() + 14);
        else if (a == "--new-note") requestedNewNote = 0;
        else if (a == "--new-note=code") requestedNewNote = 1;
        else if (a.rfind("--new-note-name=", 0) == 0) requestedNewNoteName = a.substr(16);
        else if (a == "--new-note-done") requestedNewNoteDone = true;
        else if (a.rfind("--hud-step=", 0) == 0) requestedHudStep = std::clamp(std::atoi(a.c_str() + 11), 0, 3);
        else if (a == "--catalog") requestedCatalog = true;
        else if (a == "--check-sounds") {
            // De donde sale cada sonido del tutorial: su .wav o, sin el, el sintetizado.
            const char* names[3] = {"mission", "finished", "offer"};
            for (int cue = 0; cue < 3; ++cue) {
                const std::vector<float> samples = nlblocks::loadTutorialCue(static_cast<nlblocks::TutorialCue>(cue));
                std::printf("tutorial-%s: %s, %.2f s\n", names[cue], samples.empty() ? "synthesized" : "wav", samples.size() / 2.0 / 48000.0);
            }
            return 0;
        }
        else if (a.rfind("--create-hud=", 0) == 0) requestedCreateHud = a.substr(13);
        else if (a.rfind("--type-look=", 0) == 0) requestedLookType = a.substr(12);
        else if (a.rfind("--look-preset=", 0) == 0) requestedLookPreset = a.substr(14);
        else if (a.rfind("--look-files=", 0) == 0) requestedLookFiles = a.substr(13);
        else if (a == "--create-rating") requestedRating = true;
        else if (a == "--custom-create") requestedCustom = true;
        else if (a == "--custom-ranking") { requestedCustom = true; requestedCustomRanking = true; }
        else if (a.rfind("--custom-files=", 0) == 0) { requestedCustom = true; requestedCustomFiles = a.substr(15); }
        else if (a.rfind("--custom-bot=", 0) == 0) requestedBot = a.substr(13);
        else if (a == "--commit-create") app.autoCommit = true;
        else if (a.rfind("--distribute=", 0) == 0) requestedDistribute = a.substr(13);
        else if (a.rfind("--save-chart=", 0) == 0) requestedChartSave = a.substr(13);
        else if (a.rfind("--seed=", 0) == 0) app.distribute.seed = static_cast<std::uint32_t>(std::max(1, std::atoi(a.c_str() + 7)));
        else if (a == "--distribute-view") app.typesView = 2;
        else if (a == "--blocks") { app.typesView = 1; app.requestedTab = 1; }
        else if (a == "--block-resources") { app.leftPanel = 1; app.typesView = 1; app.requestedTab = 1; }
        else if (a == "--block-resources=used") { app.leftPanel = 1; app.resourceSidebar.page = 1; app.typesView = 1; app.requestedTab = 1; }
        else if (a.rfind("--block-type=", 0) == 0) { app.blocksType = a.substr(13); app.typesView = 1; app.requestedTab = 1; }
        else if (a.rfind("--blocks-view=", 0) == 0) {
            const std::string view = a.substr(14);
            app.blocksView = view == "code" ? 1 : view == "both" ? 2 : 0;
        }
        else if (a.rfind("--presets=", 0) == 0) requestedPresets = a.substr(10);
        else if (a.rfind("--block-engine=", 0) == 0) {
            const std::string e = a.substr(15);
            app.blocksEngine = e == "psych" ? 1 : e == "vslice" ? 2 : 0;
        }
        else if (a.rfind("--window=", 0) == 0) {
            const size_t x = a.find('x', 9);
            if (x != std::string::npos) {
                windowWidth = std::clamp(std::atoi(a.c_str() + 9), 640, 3840);
                windowHeight = std::clamp(std::atoi(a.c_str() + x + 1), 480, 2400);
            }
        }
        else if (a.rfind("--save-project=", 0) == 0) requestedSave = a.substr(15);
        else if (a == "--shortcuts") app.openShortcuts = true;
        else if (a.rfind("--help-topic=", 0) == 0) openHelp(app, std::atoi(a.c_str() + 13));
        else if (a == "--asset-scope=mod") app.assetScope = 1;
        else if (a.rfind("--drop=", 0) == 0) {
            // Como soltar archivos en la ventana: separados por «;».
            const std::string list = a.substr(7);
            size_t start = 0;
            while (start <= list.size()) {
                const size_t end = list.find(';', start);
                const std::string one = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!one.empty()) app.dropImport.pending.push_back(pathFromUtf8(one));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        else if (a == "--about") app.openAbout = true;
        // El tutorial: abrirlo, fijar lo hecho (claves separadas por «,», «all»
        // o «none»), desplegar su lista, ensenar una mision, el modo foco, la
        // guia de bloques y el mini video quieto en un segundo (capturas).
        else if (a == "--tutorial") app.tutorialOpen = true;
        else if (a == "--tutorial-list") { app.tutorialOpen = true; g_tutorial.listOpen = true; }
        else if (a.rfind("--tutorial-done=", 0) == 0) {
            app.tutorialOpen = true;
            app.tutorialDone.clear();
            const std::string list = a.substr(16);
            if (list == "all") for (const TutorialMission& mission : kTutorialMissions) app.tutorialDone.insert(mission.key);
            else if (list != "none") {
                size_t start = 0;
                while (start <= list.size()) {
                    const size_t end = list.find(',', start);
                    const std::string one = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!one.empty()) app.tutorialDone.insert(one);
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
            }
        }
        else if (a.rfind("--tutorial-mission=", 0) == 0) {
            app.tutorialOpen = true;
            for (int i = 0; i < kTutorialCount; ++i)
                if (a.substr(19) == kTutorialMissions[i].key) g_tutorial.focus[static_cast<size_t>(kTutorialMissions[i].track)] = i;
        }
        else if (a == "--tutorial-focus=off") app.tutorialFocus = false;
        else if (a == "--tutorial-focus=on") app.tutorialFocus = true;
        else if (a == "--blocks-guide") { app.tutorialBlocksHidden = false; app.tutorialAreas["blocks"] = 1; }
        // Las zonas: su franja ya empezada, o la pregunta tambien en una captura.
        else if (a.rfind("--tutorial-zone=", 0) == 0) app.tutorialAreas[a.substr(16)] = 1;
        else if (a.rfind("--tutorial-ask=", 0) == 0) g_tutorial.forceAsk = a.substr(15);
        else if (a == "--tutorial-menu") g_tutorial.openMenu = true;
        else if (a.rfind("--tutorial-demo-time=", 0) == 0) g_tutorial.demoClock = std::max(0.0, std::atof(a.c_str() + 21));
        else if (a.rfind("--asset-part=", 0) == 0) requestedAssetPart = std::atoi(a.c_str() + 13);
        else if (a == "--no-psych-colors") app.psychColors = false;
        else if (a == "--export") requestedExport = true;
        else if (a.rfind("--export-engine=", 0) == 0) { exportEngine = a.substr(16); requestedExport = true; }
        else if (a.rfind("--export-role=", 0) == 0) { exportRole = a.substr(14); requestedExport = true; }
        else if (a.rfind("--export-name=", 0) == 0) { exportNameArg = a.substr(14); requestedExport = true; }
        else if (a.rfind("--export-type=", 0) == 0) { exportType = a.substr(14); requestedExport = true; }
        else if (a == "--export-zip") { exportZip = true; requestedExport = true; }
        else if (a == "--export-no-look") { exportNoLook = true; requestedExport = true; }
        else if (a.rfind("--export-to=", 0) == 0) { exportTo = a.substr(12); requestedExport = true; }
        else if (a.rfind("--show=", 0) == 0) {
            const std::string s = a.substr(7);
            app.visibleLines = s == "opponent" ? 0 : s == "player" ? 1 : 2;
        }
    }

    if (app.headless) std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Las pruebas de interfaz miden el editor, no el tutorial: sin su ventana ni
    // la guia de bloques, que movería la disposicion que comprueban.
    if (g_uiTest.active) {
        app.tutorialOpen = false;
        app.tutorialBlocksHidden = true;
        g_tutorial.quiet = !g_tutorial.testing;
    }
    // La primera vez que se abre Note Lab de verdad (no en una captura o una
    // prueba), una ventana pregunta si hacer el tutorial o saltarlo; hasta que
    // se conteste vuelve a preguntar. Despues, desde el menu Tutorial.
    if ((!app.tutorialSeen && !app.headless && !g_uiTest.active) || g_tutorial.forceAsk == "general") g_tutorial.askGeneral = true;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    const std::string initialTitle = std::string("Note Lab ") + kPublicVersion + " · FML Tool";
    SDL_Window* window = SDL_CreateWindow(initialTitle.c_str(), windowWidth, windowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) { SDL_Quit(); return 2; }
    nlbrand::applyWindowIcon(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return 3; }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(app.headless ? 0 : 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    std::error_code settingsError;
    fs::create_directories(settingsFolder(), settingsError);
    const std::string iniPath = (settingsFolder() / "imgui.ini").u8string();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = settingsError ? nullptr : iniPath.c_str();
    // Igual que Atlas: un error recuperable de ImGui no cierra la app.
    io.ConfigErrorRecoveryEnableAssert = false;
    ui::loadFonts(io);
    ui::applyStyle();
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 330");

    chooseDemo(app);
    if (!requestedBase.empty()) {
        Engine engine;
        if (baseEngineOf(requestedBase, engine)) app.baseRoots[static_cast<size_t>(engine)] = requestedBase;
    }
    for (const fs::path& root : requestedRoots) addSource(app, root);
    if (!requestedProject.empty()) openProjectFile(app, pathFromUtf8(requestedProject));
    app.dirty = false;
    // Sin pisar por que no se abrio lo pedido (un mod de otro motor, una ruta que no es un mod).
    if (app.sources.empty() && requestedRoots.empty() && requestedProject.empty())
        setStatus(app, "Open a mod folder or ZIP (Ctrl+O). The engine is detected by itself. F1 lists the shortcuts.",
                  "Abre la carpeta o el ZIP de un mod (Ctrl+O). El motor se detecta solo. F1 enseña los atajos.");
    if (!requestedSelect.empty()) {
        for (size_t s = 0; s < app.sources.size(); ++s) {
            const Source& source = *app.sources[s];
            bool found = false;
            for (size_t i = 0; i < source.catalog.styles.size() && !found; ++i)
                if (source.catalog.styles[i].id == requestedSelect ||
                    source.catalog.styles[i].id.find(requestedSelect) != std::string::npos) {
                    if (!styleVisible(app, source, source.catalog.styles[i])) { app.showOtherEngines = true; app.showUnused = true; }
                    selectStyle(app, static_cast<int>(s), static_cast<int>(i));
                    found = true;
                }
            if (found) break;
        }
    }
    if (!requestedSong.empty()) {
        bool found = false;
        for (size_t s = 0; s < app.sources.size() && !found; ++s)
            for (size_t i = 0; i < app.sources[s]->songs.size() && !found; ++i) {
                const SongChart& song = app.sources[s]->songs[i];
                if (lowerText(song.id).find(requestedSong) == std::string::npos) continue;
                if (!requestedDifficulty.empty() && lowerText(song.difficulty) != requestedDifficulty) continue;
                chooseSong(app, static_cast<int>(s), static_cast<int>(i));
                found = true;
            }
        if (!found) std::fprintf(stderr, "song not found: %s\n", requestedSong.c_str());
    }
    // --edit-prefix=<pieza>:<prefijo>: la misma edicion que el editor, para
    // comprobar sin tocar la interfaz que la vista y la validacion la siguen.
    if (!requestedEdit.empty()) {
        const size_t colon = requestedEdit.find(':');
        if (mutableStyle(app) && colon != std::string::npos) {
            const int part = std::atoi(requestedEdit.substr(0, colon).c_str());
            if (part >= 0 && part < static_cast<int>(mutableStyle(app)->parts.size())) {
                beginEdit(app);
                Animation& animation = mutableStyle(app)->parts[static_cast<size_t>(part)].animation;
                animation.prefix = requestedEdit.substr(colon + 1);
                animation.alternatives.clear();
                afterEdit(app);
                app.editPart = part;
            }
        }
    }
    if (requestedSeek >= 0.0) seekTo(app, requestedSeek);
    if (!requestedType.empty())
        if (const Source* source = selectedSource(app))
            for (size_t i = 0; i < source->noteTypes.size(); ++i)
                if (lowerText(source->noteTypes[i].name) == requestedType) {
                    app.typeSource = app.selSource;
                    app.selType = static_cast<int>(i);
                    break;
                }
    if (requestedAssetPart >= 0) {
        if (const NoteStyle* chosen = selectedStyle(app))
            if (requestedAssetPart < static_cast<int>(chosen->parts.size())) {
                app.assetPart = requestedAssetPart;
                app.editPart = requestedAssetPart;
                app.assetIndex = chosen->parts[static_cast<size_t>(requestedAssetPart)].sheet;
                app.requestedTab = 2;
            }
    }

    if (requestedVariant) createVariant(app);
    // Tras abrir el editor en una captura: su pantalla de inicio (--sprite-start),
    // un lienzo libre (--sprite-free) o, sin nada, el editor como antes.
    auto spriteStartFlags = [&]() {
        auto& e = app.spriteEditor;
        if (requestedFreeW > 0) {
            e.freeW = std::clamp(requestedFreeW, kFreeCanvasMin, kFreeCanvasMax);
            e.freeH = std::clamp(requestedFreeH > 0 ? requestedFreeH : requestedFreeW, kFreeCanvasMin, kFreeCanvasMax);
            e.freePixel = requestedFreePixel;
            e.startKind = 2;
            spriteBegin(app);
        } else if (requestedSpriteStart == -2) {
            e.choosing = false;
        } else if (requestedSpriteStart >= 0) {
            e.startKind = std::max(requestedSpriteStart, e.hasWork ? 0 : 1);
        }
    };
    if (!requestedSprite.empty()) {
        openSpriteEditor(app, app.selSource >= 0 ? app.selSource : 0, requestedSprite);
        spriteStartFlags();
        if (requestedSpriteTemplate >= 0 && !app.spriteEditor.docSingle) {
            app.spriteEditor.layers = spriteStartSheet(app, spriteLayout(app.spriteEditor), std::clamp(requestedSpriteTemplate, 0, 3));
            app.spriteEditor.layer = static_cast<int>(app.spriteEditor.layers.size()) - 1;
            ++app.spriteEditor.generation;
        }
        if (requestedSpriteTool >= 0) app.spriteEditor.tool = std::clamp(requestedSpriteTool, 0, 6);
        for (int preset : requestedSpritePresets) applySpritePreset(app, static_cast<SpritePreset>(std::clamp(preset, 0, kSpritePresets - 1)));
        if (requestedSpriteSingle >= 0) spriteNewSingle(app, static_cast<DrawnPiece>(std::clamp(requestedSpriteSingle, 0, kDrawnPieceCount - 1)), false);
        app.spriteEditor.showFinal = requestedSpriteFinal >= 0;
    }
    if (requestedNewNote >= 0) {
        openNewNote(app, requestedNewNote);
        std::snprintf(app.newNote.name.data(), app.newNote.name.size(), "%s", requestedNewNoteName.c_str());
        if (requestedNewNoteDone) createNewNote(app);
    }
    if (!requestedCreateHud.empty()) {
        app.createHud.preset = requestedCreateHud;
        openCreateHud(app);
        if (requestedHudStep >= 0) app.createHud.step = requestedHudStep;
        else if (requestedHudDrawn > 0) app.createHud.step = 3;
        if (requestedHudDrawn > 0) {
            auto& c = app.createHud;
            c.useDrawn = true;
            c.drawn[static_cast<size_t>(DrawnPiece::Note)] = {rasterSprite(spriteTemplate(std::clamp(requestedSpriteTemplate, 0, 2)))};
            if (requestedHudDrawn == 1 && requestedSpritePreset == static_cast<int>(SpritePreset::FullHud)) {
                // Las piezas de serie, como las pone el preset «HUD completo».
                const SheetLayout layout = sheetLayout(true);
                SpriteLayer layer;
                layer.image = blankImage(layout.w, layout.h);
                const std::pair<DrawnPiece, SpritePreset> parts[5] = {{DrawnPiece::Note, SpritePreset::Arrow}, {DrawnPiece::Strum, SpritePreset::Strum},
                                                                      {DrawnPiece::HoldPiece, SpritePreset::HoldPiece}, {DrawnPiece::HoldEnd, SpritePreset::HoldEnd},
                                                                      {DrawnPiece::Splash, SpritePreset::Splash}};
                for (const auto& [piece, preset] : parts) spritePaintPreset(layer.image, preset, layout.cell(layout.rowOf(piece), 0));
                c.drawn = spriteSheetFrames(layout, layer.image);
                c.drawn[static_cast<size_t>(DrawnPiece::Note)] = {rasterSprite(spriteTemplate(std::clamp(requestedSpriteTemplate, 0, 2)))};
            }
            ++c.drawnVersion;
            if (requestedHudDrawn >= 2) {
                const DrawnPiece focus = static_cast<DrawnPiece>(std::clamp(requestedSpriteFocus, 0, kDrawnPieceCount - 1));
                openHudSpriteEditor(app, focus, requestedHudDrawn == 2);
                spriteStartFlags();
                if (requestedSpriteTool >= 0) app.spriteEditor.tool = std::clamp(requestedSpriteTool, 0, 6);
                for (int preset : requestedSpritePresets) {
                    applySpritePreset(app, static_cast<SpritePreset>(std::clamp(preset, 0, kSpritePresets - 1)));
                    app.spriteEditor.selection = {};
                    spriteSelectCell(app.spriteEditor, spriteLayout(app.spriteEditor).rowOf(focus), 0);
                    app.spriteEditor.selection = {};
                }
                if (requestedSpriteSingle >= 0) spriteNewSingle(app, static_cast<DrawnPiece>(std::clamp(requestedSpriteSingle, 0, kDrawnPieceCount - 1)), false);
                app.spriteEditor.showFinal = requestedSpriteFinal >= 0;
                app.spriteEditor.finalMode = std::max(0, requestedSpriteFinal);
            }
        }
    }
    if (!requestedLookType.empty() && app.selSource >= 0) {
        openTypeLook(app, app.selSource, requestedLookType);
        if (!requestedLookPreset.empty() && typeLookPreset(requestedLookPreset, app.typeLook.recipe)) {
            app.typeLook.preset = requestedLookPreset;
            app.typeLook.live.generation++;
        }
        if (!requestedLookFiles.empty()) {
            std::vector<fs::path> files;
            size_t start = 0;
            while (start <= requestedLookFiles.size()) {
                const size_t end = requestedLookFiles.find(';', start);
                const std::string one = requestedLookFiles.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!one.empty()) files.push_back(pathFromUtf8(one));
                if (end == std::string::npos) break;
                start = end + 1;
            }
            addLookFiles(app, files);
        }
    }
    if (requestedRating) openCreateRating(app);
    if (requestedCustom) {
        openCustomCreator(app, requestedCustomRanking);
        std::vector<fs::path> files;
        size_t start = 0;
        while (start < requestedCustomFiles.size()) {
            const size_t end = requestedCustomFiles.find(';', start);
            const std::string file = requestedCustomFiles.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!file.empty()) files.push_back(pathFromUtf8(file));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (!files.empty()) addCustomFiles(app, files);
    }
    if (!requestedBot.empty()) openCustomBot(app, app.selSource, requestedBot);
    // --distribute=<tipo>:<porcentaje>[,<tipo>:<porcentaje>...] sobre el chart cargado.
    if (!requestedDistribute.empty() && app.songSource >= 0) {
        size_t start = 0;
        while (start < requestedDistribute.size()) {
            size_t end = requestedDistribute.find(',', start);
            if (end == std::string::npos) end = requestedDistribute.size();
            const std::string item = requestedDistribute.substr(start, end - start);
            const size_t colon = item.rfind(':');
            if (colon != std::string::npos)
                app.distribute.rules.push_back({item.substr(0, colon), static_cast<float>(std::atof(item.c_str() + colon + 1)), 0});
            start = end + 1;
        }
        applyDistribution(app);
    }
    // --save-chart=<archivo>: la distribucion como chart separado, como
    // «Guardar como chart separado…» (el del mod no se toca).
    if (!requestedChartSave.empty()) {
        if (prepareChartSave(app) && commitChartSave(app, pathFromUtf8(requestedChartSave), false))
            std::printf("saved chart %s\n", requestedChartSave.c_str());
        else
            std::printf("chart not saved: %s\n", app.chartSave.error.c_str());
    }
    // --block-type=<tipo> --presets=<clave>[,<clave>]: los presets en ese tipo
    // (lo crea si el mod no lo trae), como los botones de la vista Bloques.
    if (!requestedPresets.empty() && !app.blocksType.empty()) {
        Source* source = selectedSource(app);
        if (!source && !app.sources.empty()) source = app.sources.front().get();
        if (source) {
            for (size_t s = 0; s < app.sources.size(); ++s)
                if (app.sources[s].get() == source) app.blocksSource = static_cast<int>(s);
            BlockProgram& program = source->typeBlocks[app.blocksType];
            size_t start = 0;
            while (start < requestedPresets.size()) {
                size_t end = requestedPresets.find(',', start);
                if (end == std::string::npos) end = requestedPresets.size();
                const std::string key = requestedPresets.substr(start, end - start);
                for (const BlockPreset& preset : blockPresets())
                    if (key == preset.key) {
                        const std::vector<int> before = program.tops;
                        addPreset(program, preset);
                        nlblocks::placeNewStacks(program, before);
                    }
                start = end + 1;
            }
            blocksChanged(app);
        }
    }
    if (requestedComposer) openStyleComposer(app);
    if (requestedCatalog) { app.requestedTab = 1; app.typesView = 0; }
    if (g_uiTest.active && app.typesView == 1) {
        const BlockProgram* testProgram = uiProgram(app);
        if (!testProgram || testProgram->nodes.empty()) {
            std::fprintf(stderr, "[FAIL] UI test needs a seeded block program: use --block-type and --presets.\n");
            g_uiTest.failures = 1;
            g_uiTest.done = true;
        }
    }
    // --borrow-hud=<id o parte>: el HUD de ese estilo (de cualquier mod abierto)
    // en el elegido, como «Usar el HUD de…».
    if (!requestedBorrow.empty() && selectedStyle(app)) {
        bool found = false;
        for (size_t s = 0; s < app.sources.size() && !found; ++s)
            for (size_t i = 0; i < app.sources[s]->catalog.styles.size() && !found; ++i) {
                const NoteStyle& donor = app.sources[s]->catalog.styles[i];
                if (static_cast<int>(s) == app.selSource && static_cast<int>(i) == app.selStyle) continue;
                if (donor.hasHud && donor.id.find(requestedBorrow) != std::string::npos) {
                    borrowHud(app, static_cast<int>(s), static_cast<int>(i));
                    found = true;
                }
            }
        if (!found) std::fprintf(stderr, "no style with a HUD matches: %s\n", requestedBorrow.c_str());
    }
    // --edit-hud=<n> o una lista con tramos (0-3,5): varias elegidas a la vez.
    if (requestedHud >= 0 && requestedHud < 19) {
        app.hudSelection.clear();
        size_t start = 0;
        while (start < requestedHudList.size()) {
            size_t end = requestedHudList.find(',', start);
            if (end == std::string::npos) end = requestedHudList.size();
            const std::string item = requestedHudList.substr(start, end - start);
            const size_t dash = item.find('-', 1);
            const int from = std::atoi(item.c_str());
            const int to = dash == std::string::npos ? from : std::atoi(item.c_str() + dash + 1);
            for (int i = std::max(0, from); i <= std::min(18, to); ++i) app.hudSelection.insert(i);
            start = end + 1;
        }
        app.editHud = app.hudSelection.empty() ? requestedHud : *app.hudSelection.rbegin();
        app.hudAnchor = app.editHud;
        app.editPart = -1;
    }
    auto saveRequested = [&]() {
        if (!requestedSave.empty() && saveProjectTo(app, pathFromUtf8(requestedSave)))
            std::printf("saved project %s\n", requestedSave.c_str());
    };
    // --export[-engine=|-role=|-name=|-type=|-zip|-to=]: el dialogo de exportar
    // con esas opciones; con --export-to escribe el paquete ahi.
    auto exportRequested = [&]() {
        if (!requestedExport) return;
        openExport(app);
        NoteLabApp::Exporting& e = app.exporting;
        if (!exportEngine.empty())
            e.options.target = exportEngine == "psych" ? Engine::Psych : exportEngine == "vslice" ? Engine::VSlice : Engine::Codename;
        for (ExportRole role : {ExportRole::ModSkin, ExportRole::Selectable, ExportRole::SongSkin, ExportRole::NoteType})
            if (exportRole == exportRoleKey(role)) e.options.role = role;
        if (!exportRoleAvailable(e.options.target, e.options.role)) e.options.role = rolesFor(e.options.target).front();
        if (!exportNameArg.empty()) std::snprintf(e.name.data(), e.name.size(), "%s", exportNameArg.c_str());
        if (!exportType.empty()) std::snprintf(e.noteType.data(), e.noteType.size(), "%s", exportType.c_str());
        // Un tipo sin --export-name se llama como el tipo, igual que en el dialogo.
        if (exportNameArg.empty() && !exportType.empty() && e.options.role == ExportRole::NoteType)
            std::snprintf(e.name.data(), e.name.size(), "%s", exportName(exportType).c_str());
        e.zip = exportZip;
        if (exportNoLook) {
            e.options.notes = false;
            e.options.splashes = false;
            e.options.hud = false;
        }
        if (!exportTo.empty()) {
            e.folder = pathFromUtf8(exportTo);
            if (!runExport(app)) std::printf("export failed: %s\n", e.error.c_str());
        }
    };
    // Con --commit-create, guardar y exportar esperan a que lo creado se
    // confirme: asi se prueba en el motor lo mismo que daria la ventana.
    bool afterCommit = app.autoCommit && (!requestedSave.empty() || requestedExport);
    if (!afterCommit) {
        saveRequested();
        exportRequested();
    }

    if (!requestedMedia.empty() && app.selSource >= 0) {
        auto& source = *app.sources[app.selSource];
        const auto kind = resourceKindOf(requestedMedia).value_or(ResourceKind::Image);
        openModResources(app, app.selSource, app.blocksType, -1, -1, kind);
        app.media.includeBase = true;
        app.media.resources = scanModResources(*source.vfs, true);
        if (requestedMedia != "*") {
            if (const auto* resource = findModResource(app.media.resources, requestedMedia, kind)) selectMediaResource(app, source, *resource);
        }
        if (requestedSoundFilter >= 0) app.media.soundFilter = std::clamp(requestedSoundFilter, 0, 3);
    }
    if (!requestedSpriteColors.empty()) {
        std::stringstream list(requestedSpriteColors);
        std::string one;
        while (std::getline(list, one, ',')) {
            const unsigned long rgb = std::strtoul(one.c_str(), nullptr, 16);
            app.spriteColors.push_back(IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255));
        }
    }
    if (!requestedImportScript.empty() && app.selSource >= 0) importTypeScript(app, app.selSource, requestedImportScript);
    if (!requestedPreviewReceptors.empty() && app.selSource >= 0)
        for (const NoteStyle& style : app.sources[static_cast<size_t>(app.selSource)]->catalog.styles)
            if (lowerText(style.name).find(lowerText(requestedPreviewReceptors)) != std::string::npos && &style != selectedStyle(app)) {
                app.previewReceptors = style.id;
                break;
            }
    bool running = true;
    int frameCount = 0;
    int exitCode = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) requestAction(app, window, PendingAction::Quit);
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                requestAction(app, window, PendingAction::Quit);
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
                const fs::path dropped = pathFromUtf8(event.drop.data);
                if (app.media.isOpen || app.centerTab == 3) prepareMediaImport(app, {dropped});
                else if (app.custom.isOpen) addCustomFiles(app, {dropped});
                else if (app.typeLook.isOpen) addLookFiles(app, {dropped});
                else if (isAssetFile(dropped)) app.dropImport.pending.push_back(dropped);
                else addSource(app, dropped);
            }
        }
        if (app.quit) running = false;
        if (afterCommit && !app.autoCommit) {
            afterCommit = false;
            saveRequested();
            exportRequested();
        }
        processDialog(app, window);
        // El nombre del proyecto en la ventana, con un punto si hay cambios sin guardar.
        {
            const std::string title = std::string("Note Lab ") + kPublicVersion + " · FML Tool — " +
                (app.projectPath.empty() ? std::string(tr(app, "Untitled", "Sin título")) : app.projectPath.filename().u8string()) +
                (app.dirty ? " •" : "");
            if (title != app.windowTitle) {
                SDL_SetWindowTitle(window, title.c_str());
                app.windowTitle = title;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        uiTestFeed(app);
        ImGui::NewFrame();
        ImGuiErrorRecoveryState recovery;
        ImGui::ErrorRecoveryStoreState(&recovery);
        try {
            draw(app, window);
        } catch (const std::exception& error) {
            ImGui::ErrorRecoveryTryToRecoverState(&recovery);
            const std::string detail = ensureUtf8(error.what());
            setStatus(app, "Drawing error (Note Lab is still open): " + detail,
                      "Error al dibujar (Note Lab sigue abierto): " + detail);
        } catch (...) {
            ImGui::ErrorRecoveryTryToRecoverState(&recovery);
            setStatus(app, "Drawing error (Note Lab is still open).", "Error al dibujar (Note Lab sigue abierto).");
        }
        ImGui::Render();
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(15.0f / 255.0f, 17.0f / 255.0f, 22.0f / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        // La prueba de interfaz termina cuando acaba su ultimo paso (y captura entonces).
        if (g_uiTest.active && g_uiTest.done && app.capturePath.empty()) {
            std::printf("UI test: %s, %d fallos\n", g_uiTest.failures == 0 ? "PASS" : "FAIL", g_uiTest.failures);
            exitCode = g_uiTest.failures;
            running = false;
        }
        const bool captureNow = g_uiTest.active ? g_uiTest.done : ++frameCount >= app.captureFrames;
        if (!app.capturePath.empty() && captureNow) {
            if (g_uiTest.active) {
                std::printf("UI test: %s, %d fallos\n", g_uiTest.failures == 0 ? "PASS" : "FAIL", g_uiTest.failures);
                exitCode = g_uiTest.failures;
            }
            std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            stbi_flip_vertically_on_write(1);
            if (!writePng(pathFromUtf8(app.capturePath), width, height, pixels.data())) exitCode = 4;
            stbi_flip_vertically_on_write(0);
            const NoteStyle* chosen = selectedStyle(app);
            const StyleReport* report = selectedReport(app);
            int errors = 0;
            if (report) for (const Finding& f : report->findings) if (f.severity == Severity::Error) ++errors;
            int editedStyles = 0;
            for (const auto& source : app.sources)
                for (std::uint8_t edited : source->edited) editedStyles += edited ? 1 : 0;
            std::printf("sources %zu, detected %s, selected %s, errors %d, song %s, notes %zu, audio %s, time %.0f ms, "
                        "project %s, edited %d, dirty %d, issues %zu, typed %zu\n",
                        app.sources.size(), detectedText(app).c_str(), chosen ? chosen->id.c_str() : "-", errors,
                        app.songLabel.empty() ? "demo" : app.songLabel.c_str(), app.notes.size(),
                        app.audioLoaded ? "loaded" : app.audioPending ? "pending" : "none", app.state.songMs,
                        app.projectPath.empty() ? "-" : app.projectPath.filename().u8string().c_str(), editedStyles,
                        app.dirty ? 1 : 0, app.issues.size(),
                        static_cast<size_t>(std::count_if(app.noteTypes.begin(), app.noteTypes.end(), [](const std::string& t) { return !t.empty(); })));
            if (app.tutorialOpen || !app.tutorialDone.empty() || !app.tutorialAreas.empty() || g_tutorial.askGeneral)
                std::printf("tutorial %s\n", tutorialSummary(app).c_str());
            running = false;
        }
        SDL_GL_SwapWindow(window);
    }
    if (app.audioReady) app.audio.shutdown();
    if (app.rendererReady) app.renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}

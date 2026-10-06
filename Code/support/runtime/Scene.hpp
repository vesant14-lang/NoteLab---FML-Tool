// fml_runtime — Escena y lista de dibujo.
//
// REGLA DURA (DESIGN §3.2): este modulo NO enlaza OpenGL ni ImGui. Produce una
// `RenderList` —comandos de dibujo ordenados— que fml_render consume.
//
// La consecuencia util es que la escena es testeable headless: se puede afirmar
// "el stage philly produce 6 comandos, el tercero usa esta textura y este
// scroll" sin abrir una ventana. Sin eso, la fidelidad no es verificable.
#pragma once

#include "../core/Diagnostics.hpp"
#include "../core/Model.hpp"
#include "../formats/AnimateAtlas.hpp"
#include "../formats/SparrowAtlas.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fml {

// Resolucion nativa de FNF. Los stages estan autorizados contra ESTE tamano:
// `startCamPos` es el punto que la camara centra dentro de un viewport de
// 1280x720, y los scrollFactor se calculan respecto a su centro. Ignorarlo era
// la causa del desencuadre.
constexpr float kGameWidth  = 1280.0f;
constexpr float kGameHeight = 720.0f;

// LA CAMARA DEL MOTOR. Reproduce lo que mostraria Codename. No se toca para
// navegar: para eso esta ViewportTransform.
struct Camera {
    float x = 0.0f, y = 0.0f;   // punto del mundo que queda centrado (camFollow)
    float zoom = 1.0f;
    float angle = 0.0f;
};

// LA NAVEGACION DEL EDITOR. Encaja el viewport de juego en la ventana y permite
// moverse y alejarse SIN alterar lo que el motor mostraria.
struct ViewportTransform {
    float panX = 0.0f, panY = 0.0f;
    float zoom = 1.0f;          // 1 = el viewport de juego encaja en la ventana
};

// Un shader puede venir de HScript, pero la lista de dibujo sigue siendo
// independiente de OpenGL. El proceso principal llena `program` y los
// uniforms ya compilados al preparar EL frame; Scene solo conserva el contrato
// y el renderer consume los valores. Un sampler apunta a una instancia runtime
// (por ejemplo camera:game), nunca a memoria privada del motor.
struct SpriteShaderUniform {
    std::string name;
    int         location = -1;
    int         type = 0;
    int         components = 1;
    static constexpr int MaxValues = 64;
    float       value[MaxValues] = {};
    int         valueCount = 1;
    int         arraySize = 1;
    int         matrixColumns = 0;
    bool        isSampler = false;
    std::string samplerObjectId;
    // True cuando esta instancia asigno el uniform (o cuando heredo el estado
    // compatible del mismo programa). Separarlo de cero es esencial: cero es
    // un valor valido y no significa "sin asignar".
    bool        assigned = false;
};

struct SpriteShaderBinding {
    unsigned int program = 0;
    bool customVertex = false;
    std::string sourceKey;
    std::vector<SpriteShaderUniform> uniforms;
};

// OpenFL/FunkinShader comparte el estado de uniforms entre instancias que usan
// el mismo programa enlazado. Combina solo el estado del programa que el
// llamador proporcione; nunca cruza shaders de fuente distinta.
void inheritSpriteShaderUniforms(
    SpriteShaderBinding& binding,
    std::map<std::string, SpriteShaderUniform>& programState);

struct DrawCmd {
    // -2 usa la textura blanca 1x1 interna del renderer para <box>/<solid>.
    static constexpr int SolidTexture = -2;
    // -3 usa una mascara radial interna para el perfil coloredVignette.
    static constexpr int VignetteTexture = -3;
    // -4 no es una textura: la orden lleva un rotulo y lo compone la capa de
    // texto, que si tiene glifos. Ver el bloque de texto al final del struct.
    static constexpr int TextTexture = -4;
    int   texture = -1;                 // indice en RenderList::textures
    float sx = 0, sy = 0, sw = 0, sh = 0;   // rect fuente, en pixeles del atlas
    float x  = 0, y  = 0;               // posicion en mundo (esquina sup-izq)
    float w  = 0, h  = 0;               // tamano destino

    // Parte 2x2 de la transformacion local. Identidad para los sprites normales;
    // los simbolos de Adobe Animate llegan rotados y escalados, asi que la
    // esquina + ancho/alto no basta. Las esquinas en mundo son
    //     (x,y) + M * (u,v)   con u en [0,w], v en [0,h]
    float ma = 1, mb = 0, mc = 0, md = 1;
    float scrollX = 1.0f, scrollY = 1.0f;
    float alpha   = 1.0f;
    float colorR  = 1.0f, colorG = 1.0f, colorB = 1.0f;
    float zoomFactor = 1.0f;
    bool  antialiasing = true;
    bool  flipX   = false;
    bool  flipY   = false;
    bool  visible = true;
    // Identidad de camara conservada desde StageObject/FlxSprite.cameras.
    // Vacio significa la camara por defecto del juego. Una orden puede vivir
    // en varias camaras y se compone una vez en cada una, como hace Flixel.
    std::vector<std::string> cameraIds;
    // Capa concreta que esta copia recorrera al dibujarse. `cameraIds` conserva
    // la asignacion del objeto; este campo se completa al partir la lista y
    // permite demostrar en el diagnostico donde termino cada copia sin cambiar
    // la semantica ni inferirla despues por el orden.
    std::string renderCameraId;
    // La HUD se dibuja encima del stage pero no participa en la seleccion: un
    // receptor delante de un sprite no debe impedir tocar el sprite.
    bool  pickable = true;
    // Elementos continuos (barra/iconos de vida) no deben parpadear cuando su
    // ruta cambia y la nueva textura aun se esta decodificando. El renderer
    // conserva la ultima textura residente de esta identidad hasta que la
    // sustituta este completa. False mantiene el comportamiento asincrono de
    // sprites de escenario y efectos transitorios.
    bool  retainTextureWhileLoading = false;

    // ---- Modo quad -------------------------------------------------------
    //
    // Con `quad=true` las cuatro esquinas se dan explicitamente en espacio de
    // mundo y NO se derivan de x/y + la matriz 2x2. Hace falta porque una
    // proyeccion en perspectiva convierte el rectangulo en un trapecio general,
    // y eso no lo produce NINGUNA matriz 2x2 mas traslacion: `ma/mb/mc/md` solo
    // alcanza a rotar, escalar y sesgar.
    //
    // Es la frontera que comparten las dos familias de modchart. La libreria
    // oficial `funkin-modchart` proyecta cada esquina por separado en la CPU
    // (`ArrowRenderer.prepare` + `View3D.transformVector`) y emite un mesh; el
    // modchart propio de Voiid hace lo mismo en la GPU con un vertex shader por
    // nota. Las dos necesitan exactamente esto, no un shader por sprite.
    //
    // Orden de las esquinas: 0 arriba-izq, 1 arriba-der, 2 abajo-der,
    // 3 abajo-izq. Es el mismo que recorre el renderer en la ruta afin, para
    // que ambas compartan todo lo que viene despues.
    bool  quad = false;
    float qx[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float qy[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // Reciproco de la profundidad proyectada, por vertice. Interpolar UV de
    // forma lineal sobre un trapecio deforma la textura; el shader divide por
    // este valor para corregirlo. 1 = sin perspectiva, y entonces la division
    // es exacta y no cambia un solo pixel de lo que ya se dibujaba.
    float qw[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    // Trazabilidad hacia el modelo: el inspector necesita saber que objeto es.
    int         objectIndex = -1;
    std::string debugName;
    // Identidad de runtime para resolver el shader que el script le asigno.
    // Queda vacia para datos XML puros y para el fallback declarativo.
    std::string runtimeObjectId;
    std::shared_ptr<const SpriteShaderBinding> spriteShader;
    // Un sprite generado de 1x1 solo existe para que un shader produzca cada
    // pixel. Se mantiene oculto hasta que el shader se haya resuelto; dibujarlo
    // con el batch normal seria convertir un efecto en un rectangulo plano.
    bool runtimeShaderOnlyGenerated = false;

    // ---- Texto -----------------------------------------------------------
    //
    // Un FlxText no tiene textura en la VFS: OpenFL rasteriza el rotulo con la
    // fuente registrada y FlxText lo guarda como el grafico de un sprite
    // normal. Aqui pasa lo mismo. La orden sale de `Scene` con `TextTexture` y
    // este bloque relleno; antes de dibujar, la app compone el bitmap, lo sube
    // como textura dinamica y sustituye `texture` por su indice. A partir de
    // ahi es un sprite mas: mismo batch, mismo orden, mismo picking y —lo que
    // importa— el MISMO camino de shader por sprite.
    //
    // Eso ultimo no es un detalle. Hay mods cuyo texto ES un shader: dibujan
    // relleno y bordes en colores clave (rojo/verde/azul puros) y un fragment
    // shader sustituye cada canal por un degradado. Con el rotulo pintado
    // aparte no habia forma de que el shader lo tocara y se veia el color
    // clave crudo.
    std::string text;
    // Ruta virtual de la fuente del mod. Vacia = no se puede componer: no se
    // sustituye por otra tipografia porque cambiaria el ancho del rotulo.
    std::string textFont;
    float textSize      = 8.0f;
    float letterSpacing = 0.0f;
    // 0 = campo autoajustado; con ancho declarado el texto se parte.
    float fieldWidth    = 0.0f;
    bool  wordWrap      = true;
    // `alignment` de FlxText: 0 izquierda, 1 centro, 2 derecha.
    int   textAlign     = 0;
    enum class TextBorder { None, Shadow, Outline };
    TextBorder textBorderStyle = TextBorder::None;
    float textBorder        = 0.0f;
    float textBorderQuality = 1.0f;
    // ARGB tal cual los guarda Haxe. NO se convierten a tinte del sprite: el
    // color de un FlxText se hornea en el bitmap, y un shader que lea canales
    // necesita el valor exacto, no uno multiplicado por el tinte.
    unsigned int textColor       = 0xFFFFFFFFu;
    unsigned int textBorderColor = 0xFF000000u;
    struct TextOutlineLayer {
        float        size  = 0.0f;
        unsigned int color = 0xFF000000u;
    };
    std::vector<TextOutlineLayer> textOutlineLayers;
    bool textOutlineLayersReplaceBorder = false;
    int textOutlineIterations = 0;
    // Escala y angulo del objeto, sin aplicar. La app no puede reusar w/h ni la
    // matriz porque el bitmap real tiene otro tamano que la caja estimada: los
    // conserva crudos y termina la transformacion cuando ya lo ha compuesto.
    float textScaleX = 1.0f, textScaleY = 1.0f;
    float textAngle  = 0.0f;
};

// Las cuatro esquinas en espacio de mundo, vengan del quad explicito o de
// x/y/w/h + matriz. UNA sola definicion para el dibujo y para el picking: si
// cada uno derivase las suyas, un quad seria visible donde no se puede tocar.
void drawCmdCorners(const DrawCmd& cmd, float outX[4], float outY[4], float outW[4]);

// Gira una orden alrededor de un punto del mundo. Vive aqui porque el texto
// termina su geometria fuera de Scene -la app es quien conoce la fuente y el
// tamano real del bitmap-, y dos implementaciones de lo mismo acabarian
// discrepando justo en los casos raros.
void rotateAround(DrawCmd& cmd, float degrees, float pivotX, float pivotY);

// Lee un color de Haxe en cualquiera de sus formas: Int ARGB con signo tal como
// cruza JSON, `#RGB`, `#RRGGBB`, `AARRGGBB`, `0x...`. Vive aqui porque es el
// unico sitio del proyecto que las conoce todas, y el flash de camara -que se
// compone en la app- necesita exactamente la misma lectura que un <box>.
void parseSolidColor(std::string raw, float& r, float& g, float& b, float& a);

struct RenderList {
    std::vector<std::string> textures;   // rutas virtuales
    std::vector<DrawCmd>     cmds;       // EN ORDEN DE DIBUJO
    Camera                   camera;

    int internTexture(const std::string& path);
};

// El runtime necesita el tamano de las imagenes pero NO puede tocar OpenGL.
// fml_render implementa esto; asi la costura queda limpia.
struct ImageInfo { int w = 0, h = 0; bool ok = false; };

class IImageInfo {
public:
    virtual ~IImageInfo() = default;
    virtual ImageInfo imageInfo(const std::string& virtualPath) = 0;
};

// Cache de atlas ya parseados, compartida por la escena.
// Des-rota un frame que el atlas guarda girado 90 grados (`rotated="true"` de
// Sparrow). Se aplica con x/y/w/h ya puestos y antes del angulo del objeto.
// Ver `Scene.cpp` para el porque de cada linea.
void unrotateFrame(DrawCmd& c, const AtlasFrame& f);
// Lo que ocupa un frame al dibujarse: con `rotated`, cambiados.
float frameDrawW(const AtlasFrame& f);
float frameDrawH(const AtlasFrame& f);

class AtlasStore {
public:
    const SparrowAtlas* getCharacter(const UniversalCharacter& character);
    // `kindHint`: "xml" (Sparrow) o "txt" (Packer). Se deduce de la extension.
    const SparrowAtlas* get(const std::string& virtualPath);
    void putSparrow(const std::string& virtualPath, SparrowAtlas atlas) {
        m_failed.erase(virtualPath);
        m_cache[virtualPath] = std::move(atlas);
    }

    // Atlas de Adobe Animate. `virtualPath` apunta al Animation.json; el
    // spritemap se busca en la misma carpeta.
    const AnimateAtlas* getAnimate(const std::string& virtualPath);

    // true si la ruta es un atlas de Animate y no un Sparrow/Packer.
    static bool isAnimatePath(const std::string& p);

    // Hot reload: olvidar un atlas ya parseado (y su marca de fallo).
    bool invalidate(const std::string& virtualPath);

    // Inyectado por la app: como leer un archivo de texto de la VFS.
    std::function<std::string(const std::string&)> readText;

    DiagnosticSink* sink = nullptr;

private:
    std::map<std::string, SparrowAtlas> m_cache;
    std::map<std::string, AnimateAtlas> m_animate;
    std::map<std::string, bool>         m_failed;
};

// -----------------------------------------------------------------------------
// Animacion.
//
// Replica `FunkinSprite.beatHit` (verificado contra el fuente del motor):
//
//   if (beatAnims.length > 0 && (curBeat + beatOffset) % beatInterval == 0)
//       playAnim(beatAnims[wrap(countedBeat++, 0, beatAnims.length - 1)])
//
// Los sprites con type="onbeat" van rotando entre sus animaciones a cada beat
// (las bailarinas de `limo` alternan danceLeft/danceRight). El resto reproducen
// su primera animacion en bucle.
// -----------------------------------------------------------------------------
// Que personaje ocupa cada marcador del stage. El stage NO lo dice: lo dice la
// cancion (player1/player2/gfVersion en el chart). Para previsualizar un stage
// suelto hacen falta valores por defecto, y que el usuario pueda cambiarlos.
struct CharacterBinding {
    const UniversalCharacter* player     = nullptr;
    const UniversalCharacter* opponent   = nullptr;
    const UniversalCharacter* girlfriend = nullptr;
    std::vector<const UniversalCharacter*> players;
    std::vector<const UniversalCharacter*> opponents;
    std::vector<const UniversalCharacter*> girlfriends;
    std::vector<float> playerPositionIndices;
    std::vector<float> opponentPositionIndices;
    std::vector<float> girlfriendPositionIndices;

    const UniversalCharacter* forKind(StageObject::Kind k) const {
        switch (k) {
            case StageObject::Kind::Player:     return player;
            case StageObject::Kind::Opponent:   return opponent;
            case StageObject::Kind::Girlfriend: return girlfriend;
            default:                            return nullptr;
        }
    }

    std::vector<const UniversalCharacter*> allForKind(StageObject::Kind k) const {
        const std::vector<const UniversalCharacter*>* list = nullptr;
        switch (k) {
            case StageObject::Kind::Player:     list = &players; break;
            case StageObject::Kind::Opponent:   list = &opponents; break;
            case StageObject::Kind::Girlfriend: list = &girlfriends; break;
            default: break;
        }
        if (list && !list->empty()) return *list;
        const UniversalCharacter* single = forKind(k);
        return single ? std::vector<const UniversalCharacter*>{single}
                      : std::vector<const UniversalCharacter*>{};
    }

    float positionIndexForKind(StageObject::Kind k, size_t characterIndex) const {
        const std::vector<float>* indices = nullptr;
        switch (k) {
            case StageObject::Kind::Player:     indices = &playerPositionIndices; break;
            case StageObject::Kind::Opponent:   indices = &opponentPositionIndices; break;
            case StageObject::Kind::Girlfriend: indices = &girlfriendPositionIndices; break;
            default: break;
        }
        return indices && characterIndex < indices->size()
            ? (*indices)[characterIndex] : static_cast<float>(characterIndex);
    }
};

// Animaciones de baile de un personaje: danceLeft/danceRight si las tiene,
// si no idle. Codename fuerza beatInterval = 1 en los personajes.
std::vector<int> danceAnimsOf(const UniversalCharacter& ch,
                              const std::string& suffix = {});

enum class CharacterAnimContext {
    None,
    Sing,
    Dance,
    Miss,
    Lock,
};

struct ObjectAnimState {
    int   animIndex   = -1;   // indice en StageObject::anims; -1 = ninguna
    // El NOMBRE de esa animacion. El indice solo vale mientras la lista no
    // cambie, y un cambio de personaje la cambia entera: el mismo numero pasa
    // a senalar otra animacion, normalmente la de fallo. Con el nombre se
    // vuelve a encontrar la correcta en la lista nueva.
    std::string animName;
    // Instante DEL RELOJ en que arranco la animacion. El frame se deriva de
    // (reloj - startMs), no de acumular dt: asi la escena es funcion del
    // songPosition y saltar a un beat da siempre el mismo fotograma.
    double startMs    = 0.0;
    int   frame       = 0;
    int   countedBeat = 0;

    // Beat hasta el que el personaje sigue cantando. El motor hace lo mismo con
    // lastHit + holdTime: mientras canta, el beatHit no le impone el baile.
    double singUntilBeat = -1.0;

    // Que personajes DEL MARCADOR participan en la animacion en curso.
    //
    // Un marcador puede llevar varios (`StageCharPos.prepareCharacter`), y por
    // defecto cantan todos: `PlayState.hx:1834` recorre `event.characters`, que
    // arranca siendo el roster completo de la strumline. VACIO significa
    // exactamente eso y es el caso normal.
    //
    // Un script puede estrechar esa lista -es como los mods enrutan una nota a
    // uno de los dos personajes de una linea-. Cuando lo hace, aqui quedan los
    // indices que si cantan y el resto sigue bailando.
    std::vector<int> singPlacements;

    // Character.tryDance no trata igual una animacion narrativa, una pose de
    // canto y un bloqueo. Guardar el contexto evita convertir todos los eventos
    // Play Animation en notas y perder las cinematicas al siguiente beat.
    CharacterAnimContext context = CharacterAnimContext::Dance;

    // RETENIDA: el usuario la eligio a mano y el ciclo de beat NO debe pisarla.
    // Sin esto, seleccionar `singUP` para mirarla duraba hasta el siguiente beat.
    // El motor hace algo equivalente: mientras un personaje esta cantando, el
    // beatHit no le impone el baile (Character.hx usa lastHit + holdTime).
    bool  manual      = false;
    std::string idleSuffix;
    std::string idleAnimation;
};

class StageAnimator {
public:
    void reset(const UniversalStage& stage);
    void update(const UniversalStage& stage, const CharacterBinding& chars,
                AtlasStore& atlases, float dtMs, float bpm);

    // El stage cambio de verdad: hay que reconstruir el estado. No basta con
    // id+cantidad: un HScript puede insertar/reordenar objetos conservando el
    // mismo total, y entonces un indice que era BF pasa a señalar un overlay.
    // Las propiedades editables no forman parte de esta identidad, para que un
    // drag no reinicie todas las animaciones.
    bool matches(const UniversalStage& stage) const;

    // Un script puede insertar o reordenar overlays sin cambiar de personaje.
    // Reasocia el estado por identidad (kind+name) para que la animacion activa
    // viaje con su marcador en vez de reiniciarse por un cambio de indice.
    void rebind(const UniversalStage& stage);

    // Indices de la disposicion que recibio el ultimo reset/update. El gameplay
    // debe usar estos, no los indices del XML original, porque los scripts
    // pueden insertar objetos antes de los marcadores de personaje.
    int objectIndexOfKind(StageObject::Kind kind) const;
    int objectIndexNamed(const std::string& name,
                         StageObject::Kind kind = StageObject::Kind::Unknown) const;

    // Igual, pero con el beat que manda OTRO reloj (el audio). Es lo que hace que
    // la escena sea funcion del songPosition y no de su propio contador.
    // `clockMs` y `beat` los manda el audio. `playing` decide si avanzan los
    // fotogramas: con la cancion en pausa la escena tiene que congelarse entera.
    void updateWithBeat(const UniversalStage& stage, const CharacterBinding& chars,
                        AtlasStore& atlases, double clockMs, double beat, bool playing);

    // El reloj salto (seek): hay que invalidar el estado que dependia del beat
    // anterior. Sin esto, una nota disparada en el beat 500 deja al personaje
    // "cantando hasta el beat 501"; si saltas al beat 20, esa condicion sigue
    // siendo futura y el personaje se queda congelado para siempre.
    void resync(double beat, double clockMs = 0.0);

    // Dispara una animacion de canto y la sostiene `holdBeats`.
    // false permite al diagnostico distinguir una nota disparada de una
    // animacion que el personaje realmente no declara.
    bool sing(size_t objectIndex, const UniversalCharacter& ch,
              const std::string& animName, double holdBeats,
              double elapsedInAnimationMs = 0.0);

    // Semantica de Character.playAnim usada por el evento integrado. `force`
    // decide si la misma animacion se reinicia y `context` decide cuando puede
    // volver a bailar (NONE al terminar, SING/MISS por holdTime, DANCE en beat,
    // LOCK hasta que otra animacion la sustituya).
    bool playCharacterAnimation(size_t objectIndex, const UniversalCharacter& ch,
                                const std::string& animName, bool force,
                                CharacterAnimContext context,
                                double holdBeats = 0.0,
                                double elapsedInAnimationMs = 0.0);

    // Alt Animation Toggle cambia el sufijo de baile por strumline.
    void setIdleSuffix(size_t objectIndex, const std::string& suffix);
    void setIdleAnimation(size_t objectIndex, const std::string& name);

    // Indices de personaje del marcador que participan en la animacion actual.
    // nullptr o vacio = todos, que es el defecto del motor.
    const std::vector<int>* singPlacementsOf(size_t objectIndex) const;
    // La respuesta del script llega despues de haber lanzado la animacion, asi
    // que el enrutado se aplica como CORRECCION sobre la que ya esta sonando.
    void setSingPlacements(size_t objectIndex, std::vector<int> placements);

    int  animIndexOf(size_t objectIndex) const;
    // Igual, pero resolviendo por nombre contra el objeto que se va a dibujar.
    int  animIndexOf(size_t objectIndex, const StageObject& object) const;
    int  frameOf(size_t objectIndex) const;
    bool isManual(size_t objectIndex) const;

    // Reproduce y RETIENE: no la pisara el ciclo de beat hasta soltarla.
    void play(size_t objectIndex, int animIndex);
    // Devuelve el objeto al baile automatico.
    void release(size_t objectIndex);

    void   setPlaying(bool p) { m_playing = p; }
    bool   playing() const    { return m_playing; }
    double timeMs() const     { return m_timeMs; }
    double beat() const       { return m_beat; }
    void   rewind();

private:
    void stepAnimations(const UniversalStage& stage, const CharacterBinding& chars,
                        AtlasStore& atlases, double clockMs, bool advance);

    std::vector<ObjectAnimState> m_states;
    std::vector<StageObject::Kind> m_objectKinds;
    std::vector<std::string> m_objectNames;
    std::string m_stageId;
    double m_timeMs   = 0.0;
    double m_beat     = 0.0;
    int    m_lastBeat = -1;
    bool   m_playing  = true;
};

// Punto al que el motor lleva la camara cuando canta el rival
// (Character.getCameraPosition). Accion explicita del editor: NO se aplica sola,
// porque el encuadre por defecto de un stage sin startCamPos no esta definido.
bool characterCameraPoint(const UniversalStage&   stage,
                          const CharacterBinding& chars,
                          AtlasStore&             atlases,
                          StageObject::Kind       kind,
                          Vec2&                   out);

inline bool opponentCameraPoint(const UniversalStage& stage, const CharacterBinding& chars,
                                AtlasStore& atlases, Vec2& out) {
    return characterCameraPoint(stage, chars, atlases, StageObject::Kind::Opponent, out);
}

// Construye la lista de dibujo de un stage. Cada objeto del stage produce como
// mucho un comando; los marcadores de personaje (<boyfriend/>, <dad/>...) se
// saltan por ahora, pero conservan su hueco en el orden de render.
// `anim` opcional: si es nulo, se dibuja el frame 0 de cada objeto.
void buildStageRenderList(const UniversalStage&   stage,
                          const CharacterBinding& chars,
                          AtlasStore&             atlases,
                          IImageInfo&             images,
                          RenderList&             out,
                          const StageAnimator*    anim = nullptr);

}  // namespace fml

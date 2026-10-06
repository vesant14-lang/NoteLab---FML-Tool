// fml_render — Consumidor de RenderList: cache de texturas + batcher de sprites.
//
// Es el UNICO modulo que toca OpenGL. El runtime le pasa comandos y no sabe que
// existe una GPU (DESIGN §3.2).
#pragma once

#include "../runtime/Scene.hpp"
#include "ShaderLibrary.hpp"
#include "../core/CreativeFx.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fml {

class GlRenderer : public IImageInfo {
public:
    ~GlRenderer() override;

    // `resolve` traduce ruta virtual -> ruta real. La inyecta la app desde la VFS.
    using Resolver = std::function<std::string(const std::string&)>;

    bool init(Resolver resolve, std::string* error);
    void shutdown();

    // IImageInfo: el runtime pregunta tamanos sin tocar GL.
    ImageInfo imageInfo(const std::string& virtualPath) override;

    // Vista de solo lectura para paneles ImGui. Comparte la misma textura y
    // cache que la escena: inspeccionar un PNG no crea un segundo cargador ni
    // duplica VRAM. El consumidor puede usar UV para recortar un frame de atlas.
    struct PreviewImage {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        bool ok = false;
    };
    PreviewImage previewImage(const std::string& virtualPath);
    // Dear ImGui comparte las texturas de la escena. La vista avanzada puede
    // pedir nearest-neighbour para personajes pixel sin duplicar la imagen ni
    // salirse de fml_render (el unico modulo autorizado a tocar OpenGL).
    bool setPreviewFiltering(const std::string& virtualPath, bool linear);

    // Frames producidos por decoders nativos. Usan la misma RenderList que un
    // sprite normal, pero no pasan por VFS/stb_image porque cambian mientras
    // corre la cancion. BGRA8 coincide con la salida RV32 de libVLC en Windows.
    bool uploadDynamicFrame(const std::string& virtualPath,
                            const unsigned char* bgra, int width, int height);
    bool dynamicFrameReady(const std::string& virtualPath) const;
    void removeDynamicFrame(const std::string& virtualPath);

    // --- destino fuera de pantalla ------------------------------------------
    //
    // Para exportar un fotograma a la resolucion del lienzo, no a la del hueco
    // que ocupe el panel. Vive AQUI y no en el visualizador porque este es el
    // unico sitio que tiene cargados los punteros de framebuffer: el resto de
    // la aplicacion no habla GL directamente y no debe empezar a hacerlo.
    //
    // Entre `begin` y `end` el llamante dibuja lo que quiera; lo normal es
    // pedirle a ImGui que pinte una lista de dibujo suya. `end` devuelve la
    // imagen en RGBA de arriba abajo, ya dada la vuelta respecto a como la
    // entrega GL, que es lo que esperan tanto stb como el escritor de GIF.
    bool beginOffscreenFrame(int width, int height);
    bool endOffscreenFrame(std::vector<unsigned char>& rgba);
    void releaseOffscreenFrame();
    PreviewImage finishOffscreenPreview();

    // Correccion y textura sobre un rectangulo YA compuesto. A diferencia de
    // los shaders de un sprite, esto ve el fotograma entero: un blur master
    // alcanza stages, texto, espectros y cualquier otra capa en el mismo pase.
    // Los valores neutros dejan la imagen exactamente igual y evitan abrir el
    // FBO de postproceso cuando no hay ningun efecto activo.
    struct VisualizerPostProcess {
        fx::CreativePass creative;
        float blurPx = 0.0f;
        float grain = 0.0f;
        float grainSizePx = 1.0f;
        float timeSeconds = 0.0f;
        float brightness = 0.0f;
        float contrast = 1.0f;
        float saturation = 1.0f;
        float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;
        float tintMix = 0.0f;
        float vignette = 0.0f;
        float vignetteSoftness = 0.45f;
        float chromaticPx = 0.0f;
        float scanlines = 0.0f;
        float scanlineSizePx = 3.0f;
        float glitch = 0.0f;
        float glitchBands = 24.0f;
        // Ya cuantizado por quien lo calcula: el shader hace `floor`. El mismo
        // instante tiene que dar el mismo desgarro en la preview y al exportar.
        float glitchTime = 0.0f;
        float pixelSize = 1.0f;
        // Normalizados: 0..1 la mezcla y el pliegue, 0/1/2 el eje.
        float mirrorAmount = 0.0f;
        float mirrorAxis = 0.0f;
        float mirrorCenter = 0.5f;
        // La estela. `feedbackReset` la borra: sin eso, saltar el cabezal deja
        // un manchon del sitio del que vienes, que es el fallo clasico de un
        // trail en un editor -en un juego no pasa porque el tiempo solo avanza-.
        float feedbackAmount = 0.0f;
        float feedbackZoom = 1.0f;
        float feedbackRotate = 0.0f;
        float feedbackHalfLife=0,feedbackDriftX=0,feedbackDriftY=0;
        bool  feedbackReset = false;
        uint64_t feedbackKey=0,feedbackRevision=0;
        float feedbackAtMs=-1;
        bool preserveAlpha=false;
        // Cuanto de cada pieza va a cada eje, X e Y por separado. Los pares
        // por defecto reproducen lo que hacia cada una antes de tener eje.
        float blurX = 1.0f, blurY = 1.0f;
        float chromaticX = 1.0f, chromaticY = 0.0f;
        float glitchX = 1.0f, glitchY = 0.0f;
        float pixelX = 1.0f, pixelY = 1.0f;
        float vignetteX = 1.0f, vignetteY = 1.0f;
        float scanlineH = 1.0f, scanlineV = 0.0f;
        float feedbackX = 1.0f, feedbackY = 1.0f;
        // Posicion de los patrones, en pixeles del cuadro. Cero es donde
        // estaban.
        float pixelOffsetX = 0.0f, pixelOffsetY = 0.0f;
        float scanlineOffsetX = 0.0f, scanlineOffsetY = 0.0f;
        float vignetteCenterX = 0.0f, vignetteCenterY = 0.0f;

        bool active() const {
            return creative.active() || blurPx > 0.01f || grain > 0.001f ||
                   brightness < -0.001f || brightness > 0.001f ||
                   contrast < 0.999f || contrast > 1.001f ||
                   saturation < 0.999f || saturation > 1.001f ||
                   tintMix > 0.001f || vignette > 0.001f ||
                   chromaticPx > 0.01f || scanlines > 0.001f ||
                   glitch > 0.001f || pixelSize > 1.01f ||
                   mirrorAmount > 0.001f || feedbackAmount > 0.001f;
        }
    };

    // `x`,`y` usan el origen superior izquierdo de ImGui. El metodo conserva
    // el framebuffer que estuviera activo, asi sirve tanto en la preview como
    // dentro del FBO de exportacion. Debe llamarse desde un callback de dibujo
    // despues de la composicion y antes de las guias de seleccion.
    bool applyVisualizerPostProcess(const VisualizerPostProcess& settings,
                                    int x, int y, int width, int height,
                                    int viewportW, int viewportH);
    // Isolate one object before its filter tree, then composite premultiplied
    // pixels over the saved destination. No readback; targets are reused.
    bool beginVisualizerIsolation(int viewportW,int viewportH);
    bool endVisualizerIsolation(int blendMode=0);

    // Viewport + limpiado. Existe para que la app NO tenga que incluir GL:
    // fml_render sigue siendo el unico modulo que toca OpenGL.
    void beginFrame(int viewportW, int viewportH, float r, float g, float b);

    // Captura camGame en el espacio logico del motor (normalmente 1280x720)
    // para sampler2D de shaders por sprite. Es independiente de la captura de
    // la ventana usada por cadenas de camara: mezclar ambas escalas es lo que
    // reducia la imagen dentro de pantallas fisicas de los stages.
    bool beginCameraSamplerCapture(int width, int height,
                                   float r, float g, float b);
    void endCameraSamplerCapture(int restoreViewportW, int restoreViewportH);

    // Dibuja la lista completa. `view` es solo navegacion del editor: no altera
    // lo que el motor mostraria.
    // `clipRectGame` es el viewport de la camara en coordenadas logicas
    // (1280x720); nulo significa el marco entero. Solo se usa con
    // `clipToGameFrame`.
    void draw(const RenderList& list, const ViewportTransform& view,
              int viewportW, int viewportH, bool resetDrawCalls = true,
              bool clipToGameFrame = false,
              const float* clipRectGame = nullptr);

    // Cronometro de diagnostico para una espera dentro del driver. El
    // desglose normal de la app ya separa capas/cadenas/capturas, pero una
    // pausa puede quedar cargada a cualquiera de ellas simplemente porque esa
    // fue la primera llamada GL que encontro a la GPU ocupada. Se activa solo
    // con FML_TRACE_GL_DRAWS=1: medir cada upload/draw permanentemente tambien
    // alteraria justo el frame que intentamos observar.
    struct DrawTiming {
        double drawTotalMs = 0.0;
        double textureAcquireMs = 0.0;
        double batchBindMs = 0.0;
        double batchUploadMs = 0.0;
        double batchIssueMs = 0.0;
        double customSetupMs = 0.0;
        double customUploadMs = 0.0;
        double customIssueMs = 0.0;
        double samplerAllocateMs = 0.0;
        double samplerAttachMs = 0.0;
        double samplerClearMs = 0.0;
        std::size_t drawInvocations = 0;
        std::size_t textureAcquires = 0;
        std::size_t batchFlushes = 0;
        std::size_t customSprites = 0;
    };
    bool drawTimingEnabled() const { return m_traceDrawTiming; }
    const DrawTiming& drawTiming() const { return m_drawTiming; }

    // Las ubicaciones de uniforms pertenecen al programa enlazado y no cambian
    // entre sprites ni entre frames. El hot reload elimina programas, por eso
    // la app invalida explicitamente esta cache antes de borrarlos.
    void forgetShaderProgram(unsigned int program);
    void clearShaderProgramCache();

    // Copia el frame de juego ya dibujado a una textura compartida con ImGui.
    // Permite que herramientas en otra ventana (FX Editor) vean exactamente la
    // misma preview, sin mantener un segundo renderer divergente.
    unsigned int capturePreviewFrame(int viewportW, int viewportH);
    unsigned int previewFrameTexture() const { return m_previewTex; }
    int previewFrameWidth() const { return m_previewW; }
    int previewFrameHeight() const { return m_previewH; }

    // Sube un frame BGRA8 que ya fue compuesto por el runtime exacto. Vive en
    // una textura distinta para que nunca se mezcle con la escena declarativa.
    bool uploadExactFrame(const unsigned char* bgra, int width, int height);
    unsigned int exactFrameTexture() const { return m_exactTex; }
    int exactFrameWidth() const { return m_exactW; }
    int exactFrameHeight() const { return m_exactH; }
    void clearExactFrame();

    // Presenta el framebuffer exacto como lienzo principal, con la misma
    // navegacion (fit/pan/zoom) del viewport declarativo. No interpreta ni
    // recompone sus sprites: es un solo quad con los pixeles de Codename.
    void drawExactFrame(const ViewportTransform& view, int viewportW, int viewportH);

    // Marco de 1280x720: lo que se veria de verdad en el juego.
    void drawGameBounds(const ViewportTransform& view, int viewportW, int viewportH);

    // Rectangulo en pantalla que ocupa un comando de dibujo.
    struct ScreenQuad { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
    ScreenQuad quadOf(const DrawCmd& c, const Camera& cam, const ViewportTransform& view,
                      int viewportW, int viewportH) const;

    // Indice del comando bajo el cursor, o -1. Recorre de arriba abajo y usa la
    // MASCARA DE ALFA: con caja envolvente, un sprite transparente grande se
    // comeria todos los clicks del stage.
    int pick(const RenderList& list, const ViewportTransform& view,
             int viewportW, int viewportH, float mouseX, float mouseY);

    // Cuantos pixeles de pantalla equivale una unidad de mundo para ese comando.
    float worldToScreenScale(const DrawCmd& c, const Camera& cam,
                             const ViewportTransform& view, int viewportW, int viewportH) const;

    void drawOutline(float x0, float y0, float x1, float y1,
                     float r, float g, float b, float a, float thickness);

    // Un pase de la cadena de shaders de camara. Los VALORES no viven aqui: los
    // pone quien conduce la escena (inspector hoy, evaluador de modchart
    // manana), asi que el renderer no decide nada sobre el efecto.
    struct ShaderPass {
        unsigned int program = 0;
        const std::vector<ShaderUniform>* uniforms = nullptr;
    };

    // Aplica la cadena sobre lo ya dibujado, igual que el motor aplica filtros
    // sobre el bitmap de la camara (`ShaderResizeFix.hx` los lee de
    // `cam._filters`). Encadena en ping-pong sobre dos texturas.
    //
    // Se aplica SOLO al marco de juego, no a la ventana entera. En el motor la
    // textura del filtro ES la camara, asi que `openfl_TextureCoordv` va de 0 a
    // 1 sobre los 1280x720; filtrar la ventana pondria el centro de una vineta
    // donde el jugador nunca lo veria. `view` es la navegacion del editor y
    // decide donde cae ese marco en pantalla, con la misma cuenta que
    // `drawGameBounds`.
    //
    // Devuelve false si no hay pases utiles o si falta OpenGL; en ese caso lo
    // dibujado se queda como estaba, que es lo que debe pasar cuando el jugador
    // tiene `Options.gameplayShaders` apagado.
    // La cadena de shaders NO puede partir de leer la ventana. Copiar el
    // framebuffer por defecto con glCopyTexSubImage2D hace que el efecto
    // dependa de que la ventana sea legible en ese instante: si esa lectura
    // devuelve negro —ventana tapada, recien presentada, o compitiendo con otro
    // contexto GL por la GPU— la cadena entera trabaja sobre negro y el
    // resultado cubre el marco de camara. Reproducido con dos instancias a la
    // vez (§23.3).
    //
    // Con esto la escena se dibuja en una textura propia, la cadena parte de
    // ahi, y la ventana deja de estar en el camino.
    bool beginSceneCapture(int viewportW, int viewportH, float r, float g, float b,
                           float alpha = 1.0f);
    // Vuelca la escena capturada a la ventana. La llama `applyShaderChain`, y
    // hay que llamarla a mano si al final no se aplica ninguna cadena.
    void presentSceneCapture(int viewportW, int viewportH,
                             bool alphaComposite = false);
    bool sceneCaptured() const { return m_sceneCaptured; }

    // Vigilancia del cuadro negro (DESIGN §23.5). Tres hipotesis murieron
    // midiendolas y la seccion cierra diciendo exactamente esto: hay que mirar
    // DENTRO del frame malo —si la escena llego al buffer de trabajo, si el
    // primer pase se la comio, y como acaba— en vez de proponer otra causa a
    // ciegas. Cada medida es un `glReadPixels`, o sea una sincronizacion con la
    // GPU: por eso es opcional y no vive encendida.
    struct BlackFrameReport {
        bool  fired = false;        // el marco salio negro en esta pasada
        bool  sceneCaptured = false;
        bool  blitOk = false;
        // Como se compuso la capa: decide que umbral significa "negro".
        bool  alphaComposite = false;
        int   x = 0, y = 0, w = 0, h = 0;
        int   passes = 0;
        // Luminancia media (0-255) de un parche del centro, en tres momentos.
        float sceneLuma = -1.0f;    // lo que llego del FBO de escena
        float sceneAlpha = -1.0f;   // su alfa: los shaders de OpenFL trabajan
                                    // en alfa premultiplicado y con 0 devuelven
                                    // negro aunque el RGB venga lleno
        unsigned int glError = 0;   // el error, con la cola vaciada antes
        // En que llamada del primer pase salto: 1 UseProgram, 2 uniformes de
        // sistema, 3 uniformes del mod, 4 BindTexture, 5 el draw. 0 = ninguna.
        int   glErrorStep = 0;
        float firstLuma = -1.0f;    // despues del primer pase
        float finalLuma = -1.0f;    // despues del ultimo
        // Lo que el PROGRAMA tiene tras subir los uniforms del primer pase,
        // leido con glGetUniformfv. El valor que calcula la app y el que acaba
        // en la GPU no tienen por que coincidir, y sin este no se distinguen.
        std::string firstPassUniforms;
        size_t evictedSoFar = 0;    // texturas desalojadas hasta este frame
        std::uint64_t frame = 0;
    };
    void setBlackFrameWatch(bool on) { m_watchBlackFrames = on; }
    bool blackFrameWatch() const { return m_watchBlackFrames; }
    const BlackFrameReport& lastBlackFrame() const { return m_blackFrame; }
    size_t blackFrameCount() const { return m_blackFrames; }

    bool applyShaderChain(const std::vector<ShaderPass>& passes,
                          const ViewportTransform& view,
                          int viewportW, int viewportH,
                          bool alphaComposite = false);

    // Vuelca el framebuffer a PNG. Es la base del harness de comparacion por
    // captura (DESIGN §1.5): sin poder guardar un frame, la fidelidad no se
    // puede verificar contra el motor real ni proteger de regresiones.
    bool screenshot(const std::string& path, int viewportW, int viewportH);

    // El mismo volcado, pero a memoria. Es lo que permite juntar N frames del
    // stage en un GIF sin pasar por disco ni duplicar la lectura de pixeles.
    bool readFramebuffer(std::vector<unsigned char>& rgba,
                         int viewportW, int viewportH);

    // Hot reload: tirar la textura de la cache para que se recargue del disco.
    // Devuelve true si estaba cargada.
    bool invalidateTexture(const std::string& virtualPath);

    size_t textureCount() const { return m_textures.size(); }
    size_t vramBytes()    const { return m_vramBytes; }
    size_t drawCallsLastFrame() const { return m_drawCalls; }

    // Un mod real trae atlas de 7851x5889: 185 MB en RGBA8 UNA sola imagen, y
    // sus imagenes juntas piden mas de 12 GB. Sin techo, la cache crecia hasta
    // agotar la VRAM, el driver empezaba a paginar contra RAM y acababa en un
    // TDR, que congela el escritorio entero y no solo el programa. El techo se
    // respeta desalojando lo que no se ha usado en el frame actual (§12).
    size_t vramBudget()   const { return m_vramBudget; }
    void   setVramBudget(size_t bytes) { m_vramBudget = bytes; }
    // Cargar una textura es leer del disco, decodificar el PNG y subirlo, todo
    // dentro del frame que la necesita. Un atlas grande que aparece a mitad de
    // cancion cuesta decenas de milisegundos y se ve como un tiron sin causa
    // aparente. Esto deja constancia de cual, cuanto y cuando.
    struct TextureLoad {
        std::string path;
        int   width = 0, height = 0;
        float ms = 0.0f;
        std::string phase;
    };
    std::vector<TextureLoad> takeTextureLoads() { return std::move(m_textureLoads); }

    // Un elemento continuo de UI no tenia textura residente y tampoco una
    // anterior que conservar, por lo que esa orden de dibujo se perdio. Se
    // deduplica por ruta durante la vida del renderer: una causa, una voz.
    struct TextureResidencyMiss {
        std::string path;
        std::string runtimeObjectId;
        std::string debugName;
        std::string cameraLayer;
    };
    std::vector<TextureResidencyMiss> takeTextureResidencyMisses() {
        return std::move(m_textureResidencyMisses);
    }

    // Shaders de sprite que un frame tuvo que dejar sin aplicar.
    //
    // Existia el caso de identidad equivocada (FML-2505) pero no el de captura
    // ausente: ese se caia en silencio y el sprite salia crudo, que es
    // exactamente lo que pasa con el jumbotron de un stage. Un sprite opaco
    // tapando la escena sin una sola linea de diagnostico es lo contrario de lo
    // que este programa promete, asi que ahora se cuenta y se dice.
    struct SpriteShaderSkip {
        std::string sampler;
        std::string wanted;      // identidad que pedia el sampler
        bool captureMissing = false;  // la identidad era buena; falto el FBO
    };
    std::vector<SpriteShaderSkip> takeSpriteShaderSkips() {
        return std::move(m_spriteShaderSkips);
    }

    // Trazas opt-in de la frontera shader de sprite -> captura de camara. Solo
    // se generan para shaders que declaran un sampler adicional y una vez por
    // combinacion estable; asi sirven para pantallas, espejos y postprocesos
    // embebidos sin llenar el log con cada nota de cada frame.
    struct SpriteShaderTrace {
        std::string runtimeObjectId;
        std::string debugName;
        std::string sourceKey;
        std::string cameraLayer;
        std::string sampler;
        std::string wanted;
        bool participatedInCameraCapture = false;
        bool cameraCaptureReady = false;
        bool drawn = false;
    };
    std::vector<SpriteShaderTrace> takeSpriteShaderTraces() {
        return std::move(m_spriteShaderTraces);
    }
    float cameraSamplerTraceLuma() const { return m_cameraSamplerTraceLuma; }

    // Trae a memoria las texturas de una lista sin dibujar nada. BLOQUEA, y por
    // eso solo vale al terminar de cargar una cancion: ahi una pausa se espera
    // y no molesta. Llamarlo con la cancion sonando convierte varias cargas
    // pequenas en un unico frame larguisimo, que es peor que el problema que
    // venia a resolver.
    void warmTextures(const std::vector<std::string>& paths);

    // Lo mismo pero sin bloquear: encola para el worker y vuelve. Es lo que hay
    // que usar cuando algo se descubre a mitad de cancion —por ejemplo, la raiz
    // de judgements que un script solo revela al primer acierto—.
    void prefetchTextures(const std::vector<std::string>& paths);
    // Igual, pero coloca recursos continuos de UI delante de la cola. No
    // interrumpe una subida ya iniciada y nunca toca OpenGL desde el worker.
    void prefetchPriorityTextures(const std::vector<std::string>& paths);

    // Techo de tiempo que el dibujo puede gastar trayendo texturas nuevas en un
    // frame. Al superarlo, los sprites cuya textura aun no esta se saltan y se
    // reintenta en el siguiente: aparecen un par de frames tarde en vez de
    // congelar el frame entero. 0 = sin techo.
    void setTextureLoadBudgetMs(float ms) { m_textureBudgetMs = ms; }

    // Decodificacion en segundo plano.
    //
    // De los tres pasos de traer una textura -leer, decodificar y subir- solo
    // el ultimo necesita OpenGL, y es el barato. Leer un PNG de 4096x2048 y
    // construir su mascara de alfa son ~90 ms de CPU pura que no tienen por que
    // caer dentro del frame. Con esto el hilo del frame solo paga la subida.
    //
    // Mientras la decodificacion esta en vuelo el sprite no se dibuja: aparece
    // un par de frames tarde. Es un intercambio deliberado y es el que hace que
    // una captura de video no tenga tirones.
    void setAsyncTextureDecode(bool enabled);
    // Sube lo que el worker haya dejado listo. Se llama una vez por frame,
    // antes de dibujar. Devuelve cuantas texturas quedaron residentes.
    int  pumpTextureUploads();

    size_t texturesEvicted()    const { return m_texturesEvicted; }
    size_t texturesDownscaled() const { return m_texturesDownscaled; }
    int    maxTextureSize()     const { return m_maxTextureSize; }

private:
    struct Texture {
        unsigned int id = 0;
        // `w`/`h` es el tamano LOGICO, el que tiene el archivo: las UV se
        // normalizan con el (`c.sx / textureW`), asi que reducir lo que se sube
        // a la GPU no cambia ni un pixel de la geometria. `texW`/`texH` es lo
        // que ocupa de verdad, y puede ser menor.
        int w = 0, h = 0;
        int texW = 0, texH = 0;
        bool ok = false;
        bool dynamic = false;
        unsigned long long lastUsedFrame = 0;
        // Mascara de alfa submuestreada 1/4 en cada eje (1/16 de memoria) para
        // seleccionar por pixel sin guardar la textura entera en RAM.
        int                        maskW = 0, maskH = 0;
        std::vector<unsigned char> alpha;
    };

    // Trabajo del worker: todo lo que no toca OpenGL.
    struct DecodedTexture {
        std::string path;
        std::vector<unsigned char> pixels;   // RGBA, texW*texH
        std::vector<unsigned char> alpha;
        int  w = 0, h = 0, texW = 0, texH = 0, maskW = 0, maskH = 0;
        bool downscaled = false;
        bool ok = false;
        float decodeMs = 0.0f;
    };
    // Una textura grande no se sube completa en un solo frame. La
    // decodificacion termina en el worker y esta estructura conserva el
    // resultado mientras el hilo de OpenGL lo envia por franjas. El sprite no
    // se publica en `m_textures` hasta que todas las filas estan completas, de
    // modo que nunca se ve una imagen a medio cargar.
    struct PendingTextureUpload {
        DecodedTexture decoded;
        unsigned int id = 0;
        int nextRow = 0;
        float uploadMs = 0.0f;
        float allocateMs = 0.0f;
        float maxSliceMs = 0.0f;

        bool active() const { return !decoded.path.empty(); }
    };
    // Lee y decodifica sin tocar OpenGL. La ruta REAL llega ya resuelta: la VFS
    // vive en el hilo principal y no se consulta desde el worker.
    static DecodedTexture decodeTexture(const std::string& virtualPath,
                                        const std::string& realPath,
                                        int maxTextureSize, std::size_t vramBudget);
    const Texture* residentFromDecode(DecodedTexture&& decoded);
    void           startDecodeWorker();
    void           stopDecodeWorker();

    const Texture* acquire(const std::string& virtualPath);
    void           flush();
    bool           ensurePassTargets(int viewportW, int viewportH);
    struct BuiltinUniformLocations {
        int uProjection = -1;
        int openflMatrix = -1;
        int bitmap = -1;
        int uTexture = -1;
        int textureSize = -1;
        int textureUvScale = -1;
        int camSize = -1;
        int hasTransform = -1;
        int hasColorTransform = -1;
        int openflHasColorTransform = -1;
    };
    const BuiltinUniformLocations& builtinUniforms(unsigned int program);

    Resolver                     m_resolve;
    std::map<std::string, Texture> m_textures;
    // Backing stores ya comprometidos para FlxText del runtime. Reservar la
    // primera textura a mitad de una cancion puede bloquear decenas de ms en
    // algunos drivers; estas superficies pagan ese coste durante init.
    std::vector<Texture>         m_textTexturePool;
    size_t                       m_vramBytes = 0;
    unsigned int                 m_sceneFbo = 0;
    unsigned int                 m_sceneTex = 0;
    int                          m_sceneW = 0, m_sceneH = 0;
    bool                         m_sceneCaptured = false;
    unsigned int                 m_offscreenFbo = 0;
    unsigned int                 m_offscreenTex = 0;
    int                          m_offscreenW = 0, m_offscreenH = 0;
    int                          m_offscreenViewport[4]{};
    unsigned int                 m_cameraSamplerFbo = 0;
    unsigned int                 m_cameraSamplerTex = 0;
    int                          m_cameraSamplerW = 0, m_cameraSamplerH = 0;
    bool                         m_cameraSamplerReady = false;
    bool                         m_cameraSamplerCapturing = false;
    bool                         m_traceSpriteShaderBindings = false;
    float                        m_cameraSamplerTraceLuma = -1.0f;
    bool                         m_watchBlackFrames = false;
    BlackFrameReport             m_blackFrame;
    std::vector<SpriteShaderSkip> m_spriteShaderSkips;
    std::vector<SpriteShaderTrace> m_spriteShaderTraces;
    std::set<std::string>         m_spriteShaderTraceSeen;
    size_t                       m_blackFrames = 0;
    size_t                       m_vramBudget = 768u * 1024u * 1024u;
    unsigned long long           m_frameIndex = 1;
    std::vector<TextureLoad>     m_textureLoads;
    std::vector<TextureResidencyMiss> m_textureResidencyMisses;
    std::set<std::string>        m_textureResidencyMissSeen;
    // identidad de dibujo -> ultima ruta residente. Solo se llena para
    // DrawCmd que optan al contrato continuo, no para particulas/notas.
    std::map<std::string, std::string> m_retainedTexturePaths;
    float                        m_textureBudgetMs = 0.0f;
    float                        m_textureSpentMs  = 0.0f;
    bool                         m_traceDrawTiming = false;
    DrawTiming                   m_drawTiming;
    std::map<unsigned int, BuiltinUniformLocations> m_builtinUniforms;

    bool                         m_asyncDecode = false;
    std::thread                  m_decodeThread;
    std::mutex                   m_decodeMutex;
    std::condition_variable      m_decodeWake;
    std::deque<std::pair<std::string, std::string>> m_decodeQueue;  // virtual, real
    std::vector<DecodedTexture>  m_decodeReady;
    PendingTextureUpload         m_pendingUpload;
    std::set<std::string>        m_decodeInFlight;
    bool                         m_decodeStop = false;
    size_t                       m_texturesEvicted = 0;
    size_t                       m_texturesDownscaled = 0;
    int                          m_maxTextureSize = 0;
    // Desaloja por LRU hasta que quepan `needed` bytes. Nunca toca una textura
    // usada en el frame en curso: los punteros de `acquire()` viven dentro del
    // bucle de dibujo y liberar una en uso seria un puntero colgante.
    void                         evictUntilFits(size_t needed);
    size_t                       m_drawCalls = 0;

    unsigned int m_program = 0, m_vao = 0, m_vbo = 0;
    int          m_uProjection = -1, m_uTexture = -1;
    unsigned int m_batchTexture = 0;
    bool         m_batchAntialiasing = true;
    unsigned int m_whiteTex = 0, m_vignetteTex = 0;
    unsigned int m_previewTex = 0;
    int          m_previewW = 0, m_previewH = 0;
    unsigned int m_exactTex = 0;
    int          m_exactW = 0, m_exactH = 0;
    // Ping-pong de la cadena de shaders. Dos texturas del tamano del viewport y
    // un solo FBO al que se le va cambiando el adjunto de color.
    unsigned int m_blitProgram = 0;   // copia sin descarte de alfa
    // Un programa por CONJUNTO de efectos activos, no uno solo. Dos escenas
    // con los mismos efectos comparten shader; una escena que apaga la vineta
    // no paga su codigo. La clave es la mascara de `fml::fx`.
    std::map<unsigned int, unsigned int> m_visualizerPostPrograms;
    unsigned int m_passFbo = 0;
    unsigned int m_visualizerLayerFbo=0, m_visualizerLayerTex=0;
    int m_visualizerLayerW=0,m_visualizerLayerH=0;
    int m_visualizerLayerDraw=0,m_visualizerLayerRead=0;
    int m_visualizerLayerViewport[4]{};
    bool m_visualizerLayerActive=false;
    unsigned int m_passTex[2] = {0, 0};
    int          m_passW = 0, m_passH = 0;
    // LA TEXTURA QUE SOBREVIVE AL FOTOGRAMA. `m_passTex` no vale para esto: se
    // reescribe entera en cada pase, y una estela necesita justo lo contrario.
    // Se llena con el RESULTADO del postproceso, asi que la estela arrastra
    // tambien su propia estela -de ahi el tunel-.
    struct FeedbackTexture { unsigned int texture=0;int width=0,height=0; };
    struct FeedbackHistory {
        FeedbackTexture previous,output;
        float atMs=-1;
        float deltaMs=1000.f/60;
        uint64_t revision=0,lastUse=0;
    };
    std::map<uint64_t,FeedbackHistory> m_feedbackHistory;
    uint64_t m_feedbackClock=0;
    bool ensureFeedbackTarget(FeedbackTexture& target,int width,int height,bool reset);
    void*        m_scratch = nullptr;   // std::vector<float> oculto
};

}  // namespace fml

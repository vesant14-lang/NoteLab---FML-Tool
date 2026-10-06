#pragma once
#include <string>
#include <vector>

// Composicion de un FlxText a bitmap.
//
// OpenFL rasteriza el rotulo con la fuente registrada y FlxText lo guarda como
// el grafico de un sprite normal; a partir de ahi el objeto se dibuja, se
// transforma y recibe shaders como cualquier otro. Esa es la razon de que esto
// exista: mientras el texto se pintaba aparte -con la tipografia de la
// interfaz y despues de la escena- no habia forma de que un shader por sprite
// lo tocara, y hay mods cuyo texto ES un shader (el relleno y los bordes se
// dibujan en colores clave que el fragment shader sustituye por degradados).
//
// Aqui solo se produce el bitmap. Ni OpenGL ni VFS: los bytes de la fuente
// llegan resueltos y quien suba la textura es el renderer.
namespace fml {

struct TextOutlineLayer {
    float        size  = 0.0f;
    unsigned int color = 0xFF000000u;
};

struct TextRasterRequest {
    // Bytes de un .ttf/.otf. Vacio = no se puede componer y se devuelve fallo,
    // porque inventar una tipografia distinta cambia el ancho del rotulo y con
    // el la posicion de todo lo que el script coloque a partir de `text.width`.
    const unsigned char* fontData = nullptr;
    std::size_t          fontSize = 0;

    std::string text;
    // `size` de FlxText es el cuerpo de la fuente en pixeles (em), no la altura
    // de linea. Es lo que espera OpenFL en TextFormat.size.
    float size          = 8.0f;
    float letterSpacing = 0.0f;
    // 0 = campo autoajustado. Con ancho declarado el texto se parte por
    // palabras, igual que `FlxText.wordWrap`.
    float fieldWidth    = 0.0f;
    bool  wordWrap      = true;
    // 0 izquierda, 1 centro, 2 derecha.
    int   align         = 0;

    // 0 none, 1 shadow, 2 outline. Es `FlxTextBorderStyle`.
    int   borderStyle   = 0;
    float borderSize    = 0.0f;
    // Flixel dibuja `borderSize * borderQuality` pasadas por direccion; con una
    // sola el trazo se ve escalonado en bordes gruesos.
    float borderQuality = 1.0f;

    // ARGB de 32 bits, tal como los guarda Haxe.
    unsigned int fillColor   = 0xFFFFFFFFu;
    unsigned int borderColor = 0xFF000000u;

    // Some source-imported FlxText subclasses replace regenGraphic() and draw
    // several semantic outlines before the fill. The order is outside-in and
    // is significant for channel-mask shaders.
    std::vector<TextOutlineLayer> outlineLayers;
    bool outlineLayersReplaceBorder = false;
    // A custom rasterizer can place every semantic outline on a circle with a
    // fixed segment count instead of Flixel's eight-direction growth passes.
    // Zero keeps the ordinary FlxText algorithm.
    int outlineIterations = 0;

    // --- Estilo, para el visualizador. Los valores neutros dejan a FlxText
    // exactamente como estaba: el runtime no pasa por aqui con otra cosa.
    // Multiplica la altura de linea de la fuente. 1 = la de la fuente.
    float lineHeightScale = 1.0f;
    // Negrita falsa: el glifo se repite hacia la derecha estos pixeles. Se
    // aplica a todas las pasadas, tambien al borde, o el borde saldria mas
    // fino que el cuerpo.
    float fauxBoldPx = 0.0f;
    // Cursiva falsa: desplazamiento horizontal por cada pixel por encima de la
    // linea base (0.2 = unos 11 grados).
    float italicShear = 0.0f;
    // Sombra proyectada, aparte del borde de FlxText: con desplazamiento
    // propio, desenfoque y color. Va DEBAJO de todo lo demas.
    bool         dropShadow   = false;
    float        shadowX      = 0.0f;
    float        shadowY      = 0.0f;
    float        shadowBlurPx = 0.0f;
    unsigned int shadowColor  = 0x99000000u;
};

struct TextRasterResult {
    // BGRA8 premultiplicado por nada: el renderer ya mezcla con alfa recta,
    // igual que hace con cualquier PNG del mod.
    std::vector<unsigned char> bgra;
    int   width  = 0;
    int   height = 0;
    // El borde hace crecer el bitmap por los cuatro lados. FlxText conserva la
    // posicion del objeto y agranda el grafico, asi que quien dibuje tiene que
    // restar este margen para que el rotulo no se desplace al ganar borde.
    float originX = 0.0f;
    float originY = 0.0f;
    // Medida del texto sin el margen del borde: es el `width`/`height` que
    // FlxText publica y que los scripts leen para centrar.
    float textWidth  = 0.0f;
    float textHeight = 0.0f;
    bool  ok = false;
};

TextRasterResult rasterizeText(const TextRasterRequest& request);

// Ancho y alto que tendria el rotulo, sin componer el bitmap. Sirve para que la
// medida y el dibujo salgan del MISMO codigo: si difirieran, un texto centrado
// quedaria corrido justo en la cantidad en que discrepan.
bool measureText(const TextRasterRequest& request, float& width, float& height);

}  // namespace fml

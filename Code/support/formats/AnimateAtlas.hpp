// fml_formats — Atlas de Adobe Animate (el que consume FlxAnimate).
//
// Tercer formato de FNF, y el unico que no es una simple tabla de rectangulos.
// Una carpeta con:
//
//   spritemap1.json   {"ATLAS":{"SPRITES":[{"SPRITE":{"name","x","y","w","h"}}]}}
//   spritemap1.png
//   Animation.json    la linea de tiempo y el diccionario de simbolos
//
// Animation.json usa claves abreviadas: AN (animation), TL/L (timeline/layers),
// FR (frames), E (elements), SD/S (symbol dictionary), y cada elemento es
//
//   ASI  {N, M3D}            hoja: un sprite del spritemap
//   SI   {SN, M3D, TRP, ...} instancia de otro simbolo -> recursion
//
// M3D es una matriz 4x4; la parte 2D son los indices 0,1,4,5,12,13.
//
// Los nombres de animacion del XML del personaje son NOMBRES DE SIMBOLO:
// `anim="GF Cheer"` busca el simbolo `GF Cheer` del diccionario.
#pragma once

#include "../core/Diagnostics.hpp"

#include <map>
#include <string>
#include <vector>

namespace fml {

struct AnimateSprite {
    std::string name;
    int  x = 0, y = 0, w = 0, h = 0;
    bool rotated = false;
    std::string sourceImage;
    int width() const { return rotated ? h : w; }
    int height() const { return rotated ? w : h; }
};

// Un sprite ya resuelto, con la matriz acumulada de toda la cadena de simbolos.
struct AnimateElement {
    const AnimateSprite* sprite = nullptr;
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};

struct AnimateBounds {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool valid = false;
};

class AnimateAtlas {
public:
    bool load(const std::string& spritemapJson, const std::string& animationJson,
              const std::string& sourcePath, DiagnosticSink& sink);
    bool loadPages(const std::vector<std::pair<std::string, std::string>>& pages,
                   const std::string& animationJson, const std::string& sourcePath,
                   DiagnosticSink& sink);

    bool   hasSymbol(const std::string& name) const;
    int    frameCount(const std::string& symbol) const;
    size_t symbolCount() const { return m_symbols.size(); }
    std::vector<std::string> symbolNames() const;
    std::vector<std::string> imagePaths() const;

    // Aplana `symbol` en el frame `frame` a una lista de sprites con matriz,
    // en orden de dibujo (fondo primero).
    void flatten(const std::string& symbol, int frame,
                 std::vector<AnimateElement>& out) const;

    const std::string& imageFile() const { return m_imageFile; }
    // Stable envelope across the complete symbol, cached once per atlas.
    AnimateBounds bounds(const std::string& symbol) const;
    bool canMountTimeline(const std::string& original, std::string& reason) const;
    // Resolve nested 2D timelines into explicit poses for engine interoperability.
    // Returns empty for masks/filters/unsupported data; never silently drops them.
    std::string compatibleTimeline(const std::string& original, std::string& reason) const;

private:
    struct Element {
        bool  isSprite = false;
        std::string name;          // sprite del spritemap, o nombre de simbolo
        float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
        int   firstFrame = 0;
        std::string loopMode;
        std::string symbolType;
    };
    struct Frame  { int index = 0, duration = 1; std::vector<Element> elements; };
    struct Layer  { std::string name; std::vector<Frame> frames; };
    struct Symbol { std::string name; std::vector<Layer> layers; };

    // Busca tolerando espacios sobrantes: el juego base declara anim="GF FEAR"
    // mientras el simbolo se llama "GF FEAR " (con espacio final). FlxAnimate lo
    // tolera; una comparacion exacta daria la animacion por rota.
    const Symbol* findSymbol(const std::string& name) const;

    void flattenSymbol(const Symbol& s, int frame, float pa, float pb, float pc, float pd,
                       float ptx, float pty, int depth, std::vector<AnimateElement>& out) const;
    const Frame* frameAt(const Layer& l, int t) const;

    std::map<std::string, AnimateSprite> m_sprites;
    std::map<std::string, Symbol>        m_symbols;
    std::string                          m_imageFile = "spritemap1.png";
    mutable std::map<std::string, AnimateBounds> m_bounds;
};

}  // namespace fml

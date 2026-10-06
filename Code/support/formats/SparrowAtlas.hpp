// fml_formats — Atlas Sparrow (el XML de TextureAtlas que exporta Adobe Animate).
//
// Es el formato dominante del corpus: 721 atlas Sparrow frente a 24 de Animate
// (CORPUS §7). Estructura:
//
//   <TextureAtlas imagePath="bfCar.png">
//     <SubTexture name="BF NOTE DOWN0000" x="0" y="0" width="374" height="357"
//                 frameX="-1" frameY="-5" frameWidth="375" frameHeight="362"/>
//
// frameX/frameY/frameWidth/frameHeight describen el RECORTE: el packer quito el
// espacio transparente, y hay que volver a colocarlo al dibujar o el sprite sale
// desplazado. Es una de las causas clasicas de "mis offsets no cuadran".
#pragma once

#include "../core/Diagnostics.hpp"

#include <map>
#include <string>
#include <vector>

namespace fml {

struct AtlasFrame {
    std::string name;                     // "BF NOTE DOWN0000"
    int x = 0, y = 0, w = 0, h = 0;       // rectangulo dentro de la textura
    int frameX = 0, frameY = 0;           // desplazamiento por el recorte
    int frameW = 0, frameH = 0;           // tamano original antes de recortar
    bool rotated = false;
    bool trimmed = false;
    std::string sourceImage;
};

class SparrowAtlas {
public:
    std::string             imagePath;    // tal cual viene en el XML
    std::vector<AtlasFrame> frames;

    // prefijo de animacion -> indices en `frames`, en orden de numero de frame.
    // "BF NOTE DOWN0000".."0004"  ->  prefijo "BF NOTE DOWN"
    std::map<std::string, std::vector<size_t>> byPrefix;

    // OJO: la semantica correcta es "el nombre del frame EMPIEZA POR el prefijo",
    // no "el nombre sin sus digitos finales es igual al prefijo".
    //
    // Los personajes del juego base declaran anim="BF NOTE UP0" mientras que los
    // frames se llaman "BF NOTE UP0000".."BF NOTE UP0003". Y declaran "BF HEY"
    // para frames "BF HEY!!0000". Con igualdad exacta, 9 de 15 animaciones de
    // `bf` daban por rotas. Con startsWith, ninguna. Es lo que hace
    // FlxSprite.animation.addByPrefix, que es a quien hay que imitar.
    std::vector<size_t> framesFor(const std::string& prefix) const;

    size_t prefixCount() const { return byPrefix.size(); }

    // Quita los digitos finales: "BF NOTE DOWN0000" -> "BF NOTE DOWN"
    static std::string stripFrameNumber(const std::string& name);
};

Result<SparrowAtlas> parseSparrowAtlas(const std::string& xmlText,
                                       const std::string& sourcePath,
                                       DiagnosticSink&    sink);

// Atlas Packer: el .txt que acompana a algunos PNG. Tercer formato de FNF,
// junto a Sparrow y Adobe Animate. Una linea por frame:
//
//   idle spirit_0 = 0 1028 128 128
//   <nombre>      = x y ancho alto
//
// Sin recorte: frameX/frameY siempre 0. Lo usa `spirit` en el juego base.
Result<SparrowAtlas> parsePackerAtlas(const std::string& text,
                                      const std::string& sourcePath,
                                      DiagnosticSink&    sink);

}  // namespace fml

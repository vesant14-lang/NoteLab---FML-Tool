// fml_notelab — Imagenes para exportar: recortar fotogramas de un atlas,
// escalarlos, recolorearlos con la paleta de Psych y reempaquetarlos.
//
// Sin OpenGL ni ImGui, como el resto del nucleo. Decodifica y codifica PNG con
// stb; la implementacion de stb la pone quien enlaza (el renderer en la app,
// la prueba en notelabcheck).
#pragma once

#include "../support/formats/SparrowAtlas.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fml::notelab {

// RGBA de 8 bits, sin premultiplicar, fila a fila.
struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    bool empty() const { return w <= 0 || h <= 0; }
    std::uint8_t* at(int x, int y) { return rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u; }
    const std::uint8_t* at(int x, int y) const { return rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u; }
};

Image blankImage(int w, int h);
bool decodePng(const std::vector<unsigned char>& bytes, Image& out);
std::vector<unsigned char> encodePng(const Image& image);

// El rectangulo de un fotograma tal como esta en la textura, con el giro de
// Sparrow deshecho: `rotated` guarda el fotograma girado 90 grados a la
// derecha, y se devuelve derecho (Scene.cpp, unrotateFrame).
Image cropFrame(const Image& sheet, const AtlasFrame& frame);
Image cropRect(const Image& sheet, int x, int y, int w, int h);

// Nuevo tamano: vecino mas cercano para pixel art; si no, media del area al
// reducir y bilineal al ampliar, con el alfa premultiplicado para que los
// bordes no se oscurezcan.
Image resizeImage(const Image& image, int w, int h, bool pixel);
void blit(Image& target, const Image& source, int x, int y);

// El sombreador de Psych: rgb = min(r * colorR + g * colorG + b * colorB, 1)
// (RGBPalette.hx:149-153). `colors` son tres ARGB.
void applyPsychPalette(Image& image, const std::uint32_t colors[3]);
// Los colores por defecto de las opciones de Psych por carril (ClientPrefs.hx:28-37).
const std::uint32_t* psychDefaultPalette(int lane, bool pixel);

// Empaquetado en estantes, sin girar, con `padding` px entre fotogramas.
struct PackResult {
    int w = 0, h = 0;
    std::vector<std::array<int, 2>> positions;   // una por rectangulo, en el mismo orden
    bool ok = false;
};
PackResult packRects(const std::vector<std::array<int, 2>>& sizes, int padding, int maxSide = 8192);

}  // namespace fml::notelab

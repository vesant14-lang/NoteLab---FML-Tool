#include "TextRaster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

// stb_truetype ya vive en el arbol dentro de Dear ImGui, que lo encierra en su
// propio namespace al generar la implementacion. Generarla aqui a nivel global
// no colisiona en el enlace y evita traer una dependencia nueva para algo que
// ya esta disponible.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../third_party/imgui/imstb_truetype.h"

namespace fml {
namespace {

struct Face {
    stbtt_fontinfo info{};
    float scale   = 0.0f;
    float ascent  = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    bool  ok      = false;
};

Face openFace(const TextRasterRequest& request) {
    Face face;
    if (!request.fontData || request.fontSize == 0 || request.size <= 0.0f) return face;
    const int offset = stbtt_GetFontOffsetForIndex(request.fontData, 0);
    if (offset < 0) return face;
    if (!stbtt_InitFont(&face.info, request.fontData, offset)) return face;
    // OpenFL interpreta TextFormat.size como el cuerpo de la fuente, es decir
    // el em. `ScaleForPixelHeight` mapearia ascent-descent y saldrian rotulos
    // sensiblemente mas pequenos que en el juego.
    face.scale = stbtt_ScaleForMappingEmToPixels(&face.info, request.size);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&face.info, &ascent, &descent, &lineGap);
    face.ascent  = static_cast<float>(ascent)  * face.scale;
    face.descent = static_cast<float>(descent) * face.scale;
    face.lineGap = static_cast<float>(lineGap) * face.scale;
    face.ok = true;
    return face;
}

// UTF-8 -> puntos de codigo. Un rotulo de mod puede llevar acentos y simbolos;
// tratar los bytes de uno en uno partiria el glifo en dos cuadrados.
std::vector<int> decodeUtf8(const std::string& text) {
    std::vector<int> out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        int codepoint = lead;
        int extra = 0;
        if (lead >= 0xF0)      { codepoint = lead & 0x07; extra = 3; }
        else if (lead >= 0xE0) { codepoint = lead & 0x0F; extra = 2; }
        else if (lead >= 0xC0) { codepoint = lead & 0x1F; extra = 1; }
        if (i + static_cast<std::size_t>(extra) >= text.size()) extra = 0;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cont = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if ((cont & 0xC0) != 0x80) { extra = 0; codepoint = lead; break; }
            codepoint = (codepoint << 6) | (cont & 0x3F);
        }
        out.push_back(codepoint);
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

float advanceOf(const Face& face, int codepoint, int previous, float letterSpacing) {
    int advance = 0, bearing = 0;
    stbtt_GetCodepointHMetrics(&face.info, codepoint, &advance, &bearing);
    float width = static_cast<float>(advance) * face.scale;
    if (previous > 0)
        width += static_cast<float>(
            stbtt_GetCodepointKernAdvance(&face.info, previous, codepoint)) * face.scale;
    return width + letterSpacing;
}

struct Line {
    std::vector<int> glyphs;
    float width = 0.0f;
};

std::vector<Line> layout(const Face& face, const TextRasterRequest& request) {
    std::vector<Line> lines;
    const std::vector<int> codepoints = decodeUtf8(request.text);
    const bool wrap = request.wordWrap && request.fieldWidth > 0.0f;

    Line current;
    // Ultimo punto de corte valido y el ancho acumulado hasta el: partir por
    // palabras y no por letras es lo que hace FlxText con wordWrap.
    std::size_t breakAt = std::string::npos;
    float breakWidth = 0.0f;
    int previous = 0;

    const auto flush = [&]() {
        lines.push_back(current);
        current = Line();
        breakAt = std::string::npos;
        breakWidth = 0.0f;
        previous = 0;
    };

    for (const int codepoint : codepoints) {
        if (codepoint == '\n') { flush(); continue; }
        if (codepoint == '\r') continue;
        const float advance = advanceOf(face, codepoint, previous, request.letterSpacing);
        if (wrap && !current.glyphs.empty() &&
            current.width + advance > request.fieldWidth) {
            if (breakAt != std::string::npos) {
                // Se corta en el ultimo espacio: lo que va detras arranca la
                // linea siguiente.
                std::vector<int> carry(current.glyphs.begin() +
                                           static_cast<std::ptrdiff_t>(breakAt) + 1,
                                       current.glyphs.end());
                current.glyphs.resize(breakAt);
                current.width = breakWidth;
                flush();
                previous = 0;
                for (const int carried : carry) {
                    current.width += advanceOf(face, carried, previous, request.letterSpacing);
                    current.glyphs.push_back(carried);
                    previous = carried;
                }
            } else {
                flush();
            }
        }
        if (codepoint == ' ' && !current.glyphs.empty()) {
            breakAt = current.glyphs.size();
            breakWidth = current.width;
        }
        current.width += advanceOf(face, codepoint, previous, request.letterSpacing);
        current.glyphs.push_back(codepoint);
        previous = codepoint;
    }
    lines.push_back(current);
    return lines;
}

float lineHeightOf(const Face& face) {
    return std::max(1.0f, face.ascent - face.descent + face.lineGap);
}

float lineHeightOf(const Face& face, const TextRasterRequest& request) {
    return lineHeightOf(face) * std::max(0.25f, request.lineHeightScale);
}

// Desenfoque de caja separable, tres pasadas: se acerca al gaussiano y es lo
// que se espera de la «suavidad» de una sombra. Sobre el acumulador
// premultiplicado, canal a canal, alfa incluida.
void boxBlur(std::vector<float>& pixels, int width, int height, int radius) {
    if (radius <= 0 || width <= 0 || height <= 0) return;
    std::vector<float> scratch(pixels.size(), 0.0f);
    const float weight = 1.0f / static_cast<float>(radius * 2 + 1);
    for (int pass = 0; pass < 3; ++pass) {
        // Horizontal.
        for (int y = 0; y < height; ++y) {
            for (int c = 0; c < 4; ++c) {
                float sum = 0.0f;
                auto at = [&](int x) -> float& {
                    return pixels[(static_cast<std::size_t>(y) * width + x) * 4u + c];
                };
                for (int x = -radius; x <= radius; ++x)
                    sum += at(std::clamp(x, 0, width - 1));
                for (int x = 0; x < width; ++x) {
                    scratch[(static_cast<std::size_t>(y) * width + x) * 4u + c] = sum * weight;
                    sum += at(std::clamp(x + radius + 1, 0, width - 1)) -
                           at(std::clamp(x - radius, 0, width - 1));
                }
            }
        }
        // Vertical.
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < 4; ++c) {
                float sum = 0.0f;
                auto at = [&](int y) -> float& {
                    return scratch[(static_cast<std::size_t>(y) * width + x) * 4u + c];
                };
                for (int y = -radius; y <= radius; ++y)
                    sum += at(std::clamp(y, 0, height - 1));
                for (int y = 0; y < height; ++y) {
                    pixels[(static_cast<std::size_t>(y) * width + x) * 4u + c] = sum * weight;
                    sum += at(std::clamp(y + radius + 1, 0, height - 1)) -
                           at(std::clamp(y - radius, 0, height - 1));
                }
            }
        }
    }
}

// Los desplazamientos que Flixel aplica segun el estilo de borde
// (`FlxText.applyBorderStyle`). OUTLINE recorre las ocho direcciones; SHADOW
// dibuja una sola copia abajo-derecha.
std::vector<std::pair<float, float>> borderOffsets(int style, float size,
                                                   float quality) {
    std::vector<std::pair<float, float>> offsets;
    if (style == 0 || size <= 0.0f) return offsets;
    if (style == 1) {
        offsets.emplace_back(size, size);
        return offsets;
    }
    const int iterations = std::max(1, static_cast<int>(
        size * std::max(0.1f, quality)));
    const float delta = size / static_cast<float>(iterations);
    for (int step = 1; step <= iterations; ++step) {
        const float d = delta * static_cast<float>(step);
        offsets.emplace_back(-d, -d); offsets.emplace_back(0.0f, -d);
        offsets.emplace_back( d, -d); offsets.emplace_back(   d, 0.0f);
        offsets.emplace_back( d,  d); offsets.emplace_back(0.0f,  d);
        offsets.emplace_back(-d,  d); offsets.emplace_back(  -d, 0.0f);
    }
    return offsets;
}

std::vector<std::pair<float, float>> circularOutlineOffsets(float size,
                                                            int iterations) {
    std::vector<std::pair<float, float>> offsets;
    if (size <= 0.0f || iterations <= 0) return offsets;
    iterations = std::clamp(iterations, 1, 256);
    offsets.reserve(static_cast<std::size_t>(iterations));
    constexpr float tau = 6.28318530717958647692f;
    for (int index = 0; index < iterations; ++index) {
        const float angle = tau * static_cast<float>(index) /
                            static_cast<float>(iterations);
        offsets.emplace_back(std::cos(angle) * size,
                             std::sin(angle) * size);
    }
    return offsets;
}

struct PremultipliedColor {
    float blue = 0.0f, green = 0.0f, red = 0.0f, alpha = 0.0f;
};

PremultipliedColor premultipliedColor(unsigned int argb) {
    PremultipliedColor color;
    color.alpha = static_cast<float>((argb >> 24) & 0xFFu) / 255.0f;
    color.red   = static_cast<float>((argb >> 16) & 0xFFu) / 255.0f;
    color.green = static_cast<float>((argb >>  8) & 0xFFu) / 255.0f;
    color.blue  = static_cast<float>( argb        & 0xFFu) / 255.0f;
    return color;
}

// Acumula en alfa premultiplicada. La formula es la misma mezcla source-over
// que usaba el bitmap BGRA recto, pero no divide ni redondea cada canal en CADA
// pasada de cada outline. Un titulo semantico grande puede tocar el mismo pixel
// decenas de veces: conservar precision y convertir una sola vez al final baja
// el coste sin cambiar orden, cobertura ni colores.
void blendPremultiplied(std::vector<float>& pixels, int width, int height,
                        int x, int y, unsigned char coverage,
                        const PremultipliedColor& color) {
    if (coverage == 0 || x < 0 || y < 0 || x >= width || y >= height) return;
    const float sa = color.alpha * static_cast<float>(coverage) / 255.0f;
    if (sa <= 0.0f) return;
    float* pixel = pixels.data() +
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) * 4u;
    const float inverse = 1.0f - sa;
    pixel[0] = color.blue  * sa + pixel[0] * inverse;
    pixel[1] = color.green * sa + pixel[1] * inverse;
    pixel[2] = color.red   * sa + pixel[2] * inverse;
    pixel[3] = sa + pixel[3] * inverse;
}

}  // namespace

bool measureText(const TextRasterRequest& request, float& width, float& height) {
    const Face face = openFace(request);
    if (!face.ok) return false;
    const std::vector<Line> lines = layout(face, request);
    float widest = 0.0f;
    for (const Line& line : lines) widest = std::max(widest, line.width);
    width = request.fieldWidth > 0.0f ? request.fieldWidth : widest;
    height = lineHeightOf(face, request) * static_cast<float>(std::max<std::size_t>(1, lines.size()));
    return true;
}

TextRasterResult rasterizeText(const TextRasterRequest& request) {
    TextRasterResult result;
    const Face face = openFace(request);
    if (!face.ok || request.text.empty()) return result;

    const std::vector<Line> lines = layout(face, request);
    float widest = 0.0f;
    for (const Line& line : lines) widest = std::max(widest, line.width);
    const float lineHeight = lineHeightOf(face, request);
    result.textWidth = request.fieldWidth > 0.0f ? request.fieldWidth : widest;
    result.textHeight = lineHeight * static_cast<float>(std::max<std::size_t>(1, lines.size()));
    const int boldSteps = std::clamp(static_cast<int>(std::lround(request.fauxBoldPx)), 0, 32);
    const float shear = std::clamp(request.italicShear, -1.0f, 1.0f);
    const int shadowBlur = request.dropShadow
        ? std::clamp(static_cast<int>(std::lround(request.shadowBlurPx)), 0, 64) : 0;

    const std::vector<std::pair<float, float>> offsets =
        request.outlineLayersReplaceBorder
            ? std::vector<std::pair<float, float>>{}
            : borderOffsets(request.borderStyle, request.borderSize,
                            request.borderQuality);
    std::vector<std::vector<std::pair<float, float>>> layerOffsets;
    layerOffsets.reserve(std::min<std::size_t>(16, request.outlineLayers.size()));
    for (std::size_t index = 0;
         index < request.outlineLayers.size() && index < 16; ++index) {
        layerOffsets.push_back(request.outlineIterations > 0
            ? circularOutlineOffsets(request.outlineLayers[index].size,
                                     request.outlineIterations)
            : borderOffsets(2, request.outlineLayers[index].size,
                            request.borderQuality));
    }
    float margin = 0.0f;
    for (const auto& offset : offsets)
        margin = std::max(margin, std::max(std::abs(offset.first), std::abs(offset.second)));
    for (const auto& layer : layerOffsets)
        for (const auto& offset : layer)
            margin = std::max(margin,
                              std::max(std::abs(offset.first), std::abs(offset.second)));
    // La negrita, la cursiva y la sombra tambien se salen de la caja.
    margin = std::max(margin, static_cast<float>(boldSteps));
    margin = std::max(margin, std::abs(shear) * result.textHeight);
    if (request.dropShadow)
        margin = std::max(margin, std::max(std::abs(request.shadowX),
                                           std::abs(request.shadowY)) +
                                  static_cast<float>(shadowBlur) * 3.0f);
    // Un pixel extra por lado: el rasterizador reparte cobertura fuera de la
    // caja exacta del glifo y sin holgura se recorta el antialias.
    margin = std::ceil(margin) + 1.0f;

    const int width  = static_cast<int>(std::ceil(result.textWidth  + margin * 2.0f));
    const int height = static_cast<int>(std::ceil(result.textHeight + margin * 2.0f));
    // Techo defensivo: un `size` corrupto no puede pedir gigabytes de bitmap.
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return result;

    result.bgra.assign(static_cast<std::size_t>(width) *
                       static_cast<std::size_t>(height) * 4u, 0u);
    std::vector<float> premultiplied(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        0.0f);
    result.width  = width;
    result.height = height;
    result.originX = margin;
    result.originY = margin;

    // Un borde de OUTLINE son ocho direcciones por iteracion: con borderSize 5
    // eso son 41 pasadas del texto completo. Rasterizar el glifo en cada una
    // multiplicaba por 41 el trabajo, y a cuerpo 128 eso es decimas de segundo
    // en un frame. El glifo se rasteriza una vez por posicion subpixel y se
    // reutiliza; la posicion subpixel se cuantiza en cuartos porque la
    // diferencia por debajo de eso no se distingue y en cambio anula el cache.
    struct Glyph {
        std::vector<unsigned char> mask;
        int width = 0, height = 0, offsetX = 0, offsetY = 0;
    };
    std::map<int, Glyph> glyphs;
    const auto glyphFor = [&](int codepoint, int bucket) -> const Glyph& {
        const int key = codepoint * 4 + bucket;
        const auto found = glyphs.find(key);
        if (found != glyphs.end()) return found->second;
        Glyph glyph;
        unsigned char* mask = stbtt_GetCodepointBitmapSubpixel(
            &face.info, face.scale, face.scale,
            static_cast<float>(bucket) * 0.25f, 0.0f, codepoint,
            &glyph.width, &glyph.height, &glyph.offsetX, &glyph.offsetY);
        if (mask) {
            glyph.mask.assign(mask, mask + static_cast<std::size_t>(glyph.width) *
                                            static_cast<std::size_t>(glyph.height));
            stbtt_FreeBitmap(mask, nullptr);
        } else {
            glyph.width = glyph.height = 0;
        }
        return glyphs.emplace(key, std::move(glyph)).first->second;
    };

    // Cada pasada compone el mismo texto desplazado. El relleno va el ultimo
    // para quedar encima de todas las capas de borde. `target` es el
    // acumulador: el de siempre, o el de la sombra, que se desenfoca aparte.
    const auto compose = [&](std::vector<float>& target, float shiftX, float shiftY,
                             unsigned int color) {
        const PremultipliedColor source = premultipliedColor(color);
        float baseline = margin + face.ascent + shiftY;
        for (const Line& line : lines) {
            float penX = margin + shiftX;
            if (request.align == 1) penX += (result.textWidth - line.width) * 0.5f;
            else if (request.align == 2) penX += result.textWidth - line.width;

            int previous = 0;
            for (const int codepoint : line.glyphs) {
                if (previous > 0)
                    penX += static_cast<float>(stbtt_GetCodepointKernAdvance(
                        &face.info, previous, codepoint)) * face.scale;
                const int bucket = std::clamp(
                    static_cast<int>((penX - std::floor(penX)) * 4.0f), 0, 3);
                const Glyph& glyph = glyphFor(codepoint, bucket);
                if (glyph.width > 0 && glyph.height > 0) {
                    const int left = static_cast<int>(std::floor(penX)) + glyph.offsetX;
                    const int top  = static_cast<int>(std::floor(baseline)) + glyph.offsetY;
                    const int baseRow = static_cast<int>(std::floor(baseline));
                    for (int row = 0; row < glyph.height; ++row) {
                        // La cursiva falsa corre cada fila hacia la derecha
                        // segun lo alta que este sobre la linea base.
                        const int lean = shear != 0.0f
                            ? static_cast<int>(std::lround(
                                  static_cast<float>(baseRow - (top + row)) * shear))
                            : 0;
                        for (int column = 0; column < glyph.width; ++column) {
                            const unsigned char coverage =
                                glyph.mask[static_cast<std::size_t>(row) *
                                           static_cast<std::size_t>(glyph.width) +
                                           static_cast<std::size_t>(column)];
                            // La negrita falsa repite el glifo hacia la
                            // derecha: cada fila un poco mas ancha.
                            for (int bold = 0; bold <= boldSteps; ++bold)
                                blendPremultiplied(
                                    target, width, height,
                                    left + column + lean + bold, top + row,
                                    coverage, source);
                        }
                    }
                }
                int advance = 0, bearing = 0;
                stbtt_GetCodepointHMetrics(&face.info, codepoint, &advance, &bearing);
                penX += static_cast<float>(advance) * face.scale + request.letterSpacing;
                previous = codepoint;
            }
            baseline += lineHeight;
        }
    };

    // LA SOMBRA PROYECTADA va en su propio acumulador, se desenfoca ahi y se
    // pone DEBAJO de todo: es lo que hace que se lea como sombra y no como
    // un borde mas.
    if (request.dropShadow) {
        std::vector<float> shadow(premultiplied.size(), 0.0f);
        for (const auto& offset : offsets)
            compose(shadow, offset.first + request.shadowX,
                    offset.second + request.shadowY, request.shadowColor);
        compose(shadow, request.shadowX, request.shadowY, request.shadowColor);
        boxBlur(shadow, width, height, shadowBlur);
        premultiplied.swap(shadow);
    }
    for (const auto& offset : offsets)
        compose(premultiplied, offset.first, offset.second, request.borderColor);
    for (std::size_t index = 0; index < layerOffsets.size(); ++index)
        for (const auto& offset : layerOffsets[index])
            compose(premultiplied, offset.first, offset.second,
                    request.outlineLayers[index].color);
    compose(premultiplied, 0.0f, 0.0f, request.fillColor);

    // El renderer espera BGRA con alfa recta, igual que un PNG. La division se
    // hace una sola vez por pixel ya terminado; durante las capas se conserva
    // la precision completa del acumulador premultiplicado.
    for (std::size_t index = 0; index < premultiplied.size(); index += 4u) {
        const float alpha = std::clamp(premultiplied[index + 3u], 0.0f, 1.0f);
        if (alpha <= 0.0f) continue;
        const auto channel = [&](float value) {
            return static_cast<unsigned char>(std::lround(
                std::clamp(value / alpha, 0.0f, 1.0f) * 255.0f));
        };
        result.bgra[index + 0u] = channel(premultiplied[index + 0u]);
        result.bgra[index + 1u] = channel(premultiplied[index + 1u]);
        result.bgra[index + 2u] = channel(premultiplied[index + 2u]);
        result.bgra[index + 3u] = static_cast<unsigned char>(
            std::lround(alpha * 255.0f));
    }

    result.ok = true;
    return result;
}

}  // namespace fml

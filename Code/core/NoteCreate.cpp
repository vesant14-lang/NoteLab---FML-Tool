#include "NoteCreate.hpp"

#include "NotePreview.hpp"
#include "../support/runtime/Scene.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>

// stb_truetype ya esta en el arbol dentro de Dear ImGui; con STBTT_STATIC la
// implementacion queda dentro de esta unidad y no choca con la de ImGui ni con
// la de fml_render/TextRaster.cpp.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/imgui/imstb_truetype.h"

// Los GIF con una copia propia de stb_image, estatica y solo GIF: la de la
// app (GlRenderer.cpp) y la de las pruebas solo traen PNG y JPEG.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#include "../third_party/stb_image.h"

#include "../support/formats/SparrowAtlas.hpp"
#include "../third_party/pugixml.hpp"

#include <fstream>

namespace fml::notelab {
namespace {

// ------------------------------------------------------------------- color --

struct Hsl { float h = 0.0f, s = 0.0f, l = 0.0f; };

Hsl toHsl(float r, float g, float b) {
    const float hi = std::max({r, g, b}), lo = std::min({r, g, b});
    Hsl out;
    out.l = (hi + lo) * 0.5f;
    const float d = hi - lo;
    if (d <= 1e-5f) return out;
    out.s = out.l > 0.5f ? d / (2.0f - hi - lo) : d / (hi + lo);
    if (hi == r) out.h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (hi == g) out.h = (b - r) / d + 2.0f;
    else out.h = (r - g) / d + 4.0f;
    out.h /= 6.0f;
    return out;
}

float hueChannel(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

void fromHsl(const Hsl& c, float& r, float& g, float& b) {
    if (c.s <= 1e-5f) {
        r = g = b = c.l;
        return;
    }
    const float q = c.l < 0.5f ? c.l * (1.0f + c.s) : c.l + c.s - c.l * c.s;
    const float p = 2.0f * c.l - q;
    r = hueChannel(p, q, c.h + 1.0f / 3.0f);
    g = hueChannel(p, q, c.h);
    b = hueChannel(p, q, c.h - 1.0f / 3.0f);
}

float channel(std::uint32_t argb, int shift) { return static_cast<float>((argb >> shift) & 0xFFu) / 255.0f; }

// Lleva `value` de modo que `from` pase a ser `to` y 0 y 1 se queden: lo mas
// oscuro y lo mas claro de la pieza no cambian de sitio.
float remap(float value, float from, float to) {
    from = std::clamp(from, 0.02f, 0.98f);
    if (value <= from) return value * to / from;
    return to + (value - from) * (1.0f - to) / (1.0f - from);
}

std::uint8_t byte(float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

// El color que manda en la imagen: la media de lo saturado y opaco.
bool dominantColor(const Image& image, Hsl& out, float& share) {
    double sumR = 0.0, sumG = 0.0, sumB = 0.0, weight = 0.0, opaque = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(image.w) * static_cast<size_t>(image.h); ++i) {
        const std::uint8_t* p = image.rgba.data() + i * 4u;
        if (p[3] < 128) continue;
        opaque += 1.0;
        const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
        const Hsl c = toHsl(r, g, b);
        if (c.s < 0.35f || c.l < 0.12f || c.l > 0.92f) continue;
        sumR += r;
        sumG += g;
        sumB += b;
        weight += 1.0;
    }
    share = opaque > 0.0 ? static_cast<float>(weight / opaque) : 0.0f;
    if (weight < 1.0) return false;
    out = toHsl(static_cast<float>(sumR / weight), static_cast<float>(sumG / weight), static_cast<float>(sumB / weight));
    return true;
}

float averageLight(const Image& image) {
    double sum = 0.0, n = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(image.w) * static_cast<size_t>(image.h); ++i) {
        const std::uint8_t* p = image.rgba.data() + i * 4u;
        if (p[3] < 128) continue;
        sum += toHsl(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f).l;
        n += 1.0;
    }
    return n > 0.0 ? static_cast<float>(sum / n) : 0.5f;
}

void paintHue(Image& image, std::uint32_t target, float strength) {
    const Hsl to = toHsl(channel(target, 16), channel(target, 8), channel(target, 0));
    Hsl ref;
    float share = 0.0f;
    // Sin nada de color (un receptor gris): se colorea todo, conservando la luz.
    const bool colorize = !dominantColor(image, ref, share) || share < 0.08f;
    if (colorize) {
        ref.h = to.h;
        ref.s = 0.0f;
        ref.l = averageLight(image);
    }
    for (size_t i = 0; i < static_cast<size_t>(image.w) * static_cast<size_t>(image.h); ++i) {
        std::uint8_t* p = image.rgba.data() + i * 4u;
        if (p[3] == 0) continue;
        const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
        const Hsl c = toHsl(r, g, b);
        float weight = 1.0f;
        Hsl n = c;
        if (colorize) {
            n.h = to.h;
            n.s = to.s * 0.85f * std::clamp(1.0f - std::fabs(c.l - 0.5f) * 1.6f, 0.0f, 1.0f);
            n.l = remap(c.l, ref.l, to.l);
        } else {
            // Solo lo que tiene color: el brillo blanco y el contorno oscuro se quedan.
            weight = std::clamp((c.s - 0.10f) / 0.20f, 0.0f, 1.0f);
            float dh = c.h - ref.h;
            if (dh > 0.5f) dh -= 1.0f;
            if (dh < -0.5f) dh += 1.0f;
            n.h = to.h + dh * 0.35f;
            if (n.h < 0.0f) n.h += 1.0f;
            if (n.h > 1.0f) n.h -= 1.0f;
            n.s = std::clamp(remap(c.s, ref.s, to.s), 0.0f, 1.0f);
            n.l = std::clamp(remap(c.l, ref.l, to.l), 0.0f, 1.0f);
        }
        float nr = 0.0f, ng = 0.0f, nb = 0.0f;
        fromHsl(n, nr, ng, nb);
        const float k = weight * strength;
        p[0] = byte(r + (nr - r) * k);
        p[1] = byte(g + (ng - g) * k);
        p[2] = byte(b + (nb - b) * k);
    }
}

void paintGradient(Image& image, const std::array<std::uint32_t, 3>& stops, float strength) {
    float s[3][3];
    for (int k = 0; k < 3; ++k) {
        s[k][0] = channel(stops[static_cast<size_t>(k)], 16);
        s[k][1] = channel(stops[static_cast<size_t>(k)], 8);
        s[k][2] = channel(stops[static_cast<size_t>(k)], 0);
    }
    for (size_t i = 0; i < static_cast<size_t>(image.w) * static_cast<size_t>(image.h); ++i) {
        std::uint8_t* p = image.rgba.data() + i * 4u;
        if (p[3] == 0) continue;
        const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
        const float y = 0.299f * r + 0.587f * g + 0.114f * b;
        const int lo = y < 0.5f ? 0 : 1;
        const float t = y < 0.5f ? y / 0.5f : (y - 0.5f) / 0.5f;
        for (int c = 0; c < 3; ++c) {
            const float target = s[lo][c] + (s[lo + 1][c] - s[lo][c]) * t;
            const float from = c == 0 ? r : c == 1 ? g : b;
            p[c] = byte(from + (target - from) * strength);
        }
    }
}

// Lo visible de una imagen: alfa por encima de 1/16.
bool visibleBox(const Image& image, int& x0, int& y0, int& x1, int& y1) {
    x0 = image.w;
    y0 = image.h;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < image.h; ++y)
        for (int x = 0; x < image.w; ++x)
            if (image.at(x, y)[3] > 16) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    return x1 >= x0 && y1 >= y0;
}

// Un color encima de un pixel (sin premultiplicar), con su cobertura.
void blendPixel(Image& target, int x, int y, float r, float g, float b, float a) {
    if (a <= 0.0f || x < 0 || y < 0 || x >= target.w || y >= target.h) return;
    std::uint8_t* t = target.at(x, y);
    const float ta = t[3] / 255.0f;
    const float oa = a + ta * (1.0f - a);
    const float k = 1.0f / std::max(oa, 1e-6f);
    t[0] = byte((r * a + t[0] / 255.0f * ta * (1.0f - a)) * k);
    t[1] = byte((g * a + t[1] / 255.0f * ta * (1.0f - a)) * k);
    t[2] = byte((b * a + t[2] / 255.0f * ta * (1.0f - a)) * k);
    t[3] = byte(oa);
}

// `source` encima de `target` en (x, y), con su alfa por `alpha`.
void blendOver(Image& target, const Image& source, int x, int y, float alpha) {
    for (int sy = 0; sy < source.h; ++sy) {
        const int ty = y + sy;
        if (ty < 0 || ty >= target.h) continue;
        for (int sx = 0; sx < source.w; ++sx) {
            const int tx = x + sx;
            if (tx < 0 || tx >= target.w) continue;
            const std::uint8_t* s = source.at(sx, sy);
            const float sa = s[3] / 255.0f * alpha;
            if (sa <= 0.0f) continue;
            std::uint8_t* t = target.at(tx, ty);
            const float ta = t[3] / 255.0f;
            const float oa = sa + ta * (1.0f - sa);
            for (int c = 0; c < 3; ++c) {
                const float v = (s[c] / 255.0f * sa + t[c] / 255.0f * ta * (1.0f - sa)) / std::max(oa, 1e-6f);
                t[c] = byte(v);
            }
            t[3] = byte(oa);
        }
    }
}

// Lo contrario de cropFrame: la imagen derecha de vuelta a su region, girada
// si el atlas la guarda girada.
void putFrame(Image& sheet, const AtlasFrame& frame, const Image& content) {
    if (!frame.rotated) {
        for (int y = 0; y < content.h && y < frame.h; ++y)
            for (int x = 0; x < content.w && x < frame.w; ++x) {
                const int sx = frame.x + x, sy = frame.y + y;
                if (sx < 0 || sy < 0 || sx >= sheet.w || sy >= sheet.h) continue;
                std::copy_n(content.at(x, y), 4, sheet.at(sx, sy));
            }
        return;
    }
    for (int oy = 0; oy < content.h; ++oy)
        for (int ox = 0; ox < content.w; ++ox) {
            const int sx = frame.x + frame.w - 1 - oy;
            const int sy = frame.y + ox;
            if (sx < 0 || sy < 0 || sx >= sheet.w || sy >= sheet.h) continue;
            std::copy_n(content.at(ox, oy), 4, sheet.at(sx, sy));
        }
}

// Las regiones que usa una pieza en su hoja, como las corta el motor.
std::vector<AtlasFrame> regionsOf(const Sheet& sheet, const Image& image, const PartBinding& binding, AtlasStore& atlases) {
    std::vector<AtlasFrame> out;
    auto whole = [&](int x, int y, int w, int h) {
        AtlasFrame f;
        f.x = x;
        f.y = y;
        f.w = std::max(1, w);
        f.h = std::max(1, h);
        f.frameW = f.w;
        f.frameH = f.h;
        out.push_back(f);
    };
    switch (sheet.kind) {
        case SheetKind::Strip: {
            const int columns = sheet.columns > 0 ? sheet.columns : 8;
            const int column = binding.animation.indices.empty() ? 0 : binding.animation.indices[0];
            if (column < 0 || column >= columns) return out;
            const int x0 = static_cast<int>(std::lround(static_cast<double>(image.w) * column / columns));
            const int x1 = static_cast<int>(std::lround(static_cast<double>(image.w) * (column + 1) / columns));
            whole(x0, 0, x1 - x0, image.h);
            return out;
        }
        case SheetKind::Image:
            whole(0, 0, image.w, image.h);
            return out;
        case SheetKind::Grid: {
            const int columns = std::max(1, sheet.columns), rows = std::max(1, sheet.rows);
            const int cellW = image.w / columns, cellH = image.h / rows;
            if (cellW <= 0 || cellH <= 0) return out;
            // Como Flixel: las celdas enteras que caben (FlxTileFrames.hx:296-305).
            const int perRow = image.w / cellW, perColumn = image.h / cellH;
            std::vector<int> cells = binding.animation.indices;
            if (cells.empty()) cells.push_back(0);
            for (int cell : cells)
                if (cell >= 0 && cell < perRow * perColumn) whole((cell % perRow) * cellW, (cell / perRow) * cellH, cellW, cellH);
            return out;
        }
        case SheetKind::Sparrow:
        case SheetKind::Packer:
            break;
    }
    const SparrowAtlas* atlas = sheet.atlas.empty() ? nullptr : atlases.get(sheet.atlas);
    if (!atlas) return out;
    for (size_t index : animationFrames(*atlas, binding.animation)) out.push_back(atlas->frames[index]);
    return out;
}

// ------------------------------------------------------------------- texto --

// Los caracteres de un texto UTF-8; los bytes rotos se saltan.
std::vector<unsigned> codepoints(const std::string& text) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        unsigned cp = 0;
        int extra = 0;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
        else { ++i; continue; }
        if (i + static_cast<size_t>(extra) >= text.size()) {
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + static_cast<size_t>(k)]);
            if ((cc >> 6) != 0x2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += static_cast<size_t>(extra) + 1;
        if (ok) out.push_back(cp);
    }
    return out;
}

bool openFont(const std::vector<unsigned char>& font, stbtt_fontinfo& info) {
    if (font.size() < 12) return false;
    const int offset = stbtt_GetFontOffsetForIndex(font.data(), 0);
    if (offset < 0) return false;
    return stbtt_InitFont(&info, font.data(), offset) != 0;
}

// Distancia euclidea al cuadrado (Felzenszwalb y Huttenlocher), en una fila.
void distance1d(const std::vector<float>& f, std::vector<float>& d, int n) {
    std::vector<int> v(static_cast<size_t>(n));
    std::vector<float> z(static_cast<size_t>(n) + 1);
    const float inf = std::numeric_limits<float>::infinity();
    int k = 0;
    v[0] = 0;
    z[0] = -inf;
    z[1] = inf;
    for (int q = 1; q < n; ++q) {
        float s = ((f[static_cast<size_t>(q)] + static_cast<float>(q * q)) -
                   (f[static_cast<size_t>(v[static_cast<size_t>(k)])] + static_cast<float>(v[static_cast<size_t>(k)] * v[static_cast<size_t>(k)]))) /
                  static_cast<float>(2 * q - 2 * v[static_cast<size_t>(k)]);
        while (s <= z[static_cast<size_t>(k)]) {
            --k;
            s = ((f[static_cast<size_t>(q)] + static_cast<float>(q * q)) -
                 (f[static_cast<size_t>(v[static_cast<size_t>(k)])] + static_cast<float>(v[static_cast<size_t>(k)] * v[static_cast<size_t>(k)]))) /
                static_cast<float>(2 * q - 2 * v[static_cast<size_t>(k)]);
        }
        ++k;
        v[static_cast<size_t>(k)] = q;
        z[static_cast<size_t>(k)] = s;
        z[static_cast<size_t>(k) + 1] = inf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[static_cast<size_t>(k) + 1] < static_cast<float>(q)) ++k;
        const int dq = q - v[static_cast<size_t>(k)];
        d[static_cast<size_t>(q)] = static_cast<float>(dq * dq) + f[static_cast<size_t>(v[static_cast<size_t>(k)])];
    }
}

// La mascara engordada `radius` px: el contorno. Distancia al pixel de dentro
// mas cercano, con medio pixel de suavizado.
std::vector<float> dilate(const std::vector<float>& mask, int w, int h, float radius) {
    const float inf = 1e20f;
    std::vector<float> grid(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < grid.size(); ++i) grid[i] = mask[i] >= 0.5f ? 0.0f : inf;
    std::vector<float> f(static_cast<size_t>(std::max(w, h))), d(f.size());
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) f[static_cast<size_t>(y)] = grid[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        distance1d(f, d, h);
        for (int y = 0; y < h; ++y) grid[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = d[static_cast<size_t>(y)];
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) f[static_cast<size_t>(x)] = grid[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        distance1d(f, d, w);
        for (int x = 0; x < w; ++x) grid[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = d[static_cast<size_t>(x)];
    }
    std::vector<float> out(grid.size());
    for (size_t i = 0; i < grid.size(); ++i)
        out[i] = std::max(mask[i], std::clamp(radius + 0.5f - std::sqrt(grid[i]), 0.0f, 1.0f));
    return out;
}

std::string utf16beToUtf8(const char* data, int length) {
    std::string out;
    for (int i = 0; i + 1 < length; i += 2) {
        unsigned cp = (static_cast<unsigned char>(data[i]) << 8) | static_cast<unsigned char>(data[i + 1]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < length) {
            const unsigned low = (static_cast<unsigned char>(data[i + 2]) << 8) | static_cast<unsigned char>(data[i + 3]);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            i += 2;
        }
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// ---------------------------------------------------------- reconocer nombres --

std::string lowerTrim(const std::string& text) {
    std::string out;
    for (char c : text) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
    size_t start = 0;
    while (start < out.size() && std::isspace(static_cast<unsigned char>(out[start]))) ++start;
    return out.substr(start);
}

// Sin el numero de fotograma del final ("purple0001" -> "purple").
std::string withoutFrameNumber(std::string name) {
    while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back()))) name.pop_back();
    while (!name.empty() && (name.back() == ' ' || name.back() == '_' || name.back() == '-')) name.pop_back();
    return name;
}

int colorIndex(const std::string& word) {
    static const char* colors[4] = {"purple", "blue", "green", "red"};
    for (int i = 0; i < 4; ++i)
        if (word == colors[i]) return i;
    return -1;
}

int directionIndex(const std::string& word) {
    static const char* dirs[4] = {"left", "down", "up", "right"};
    for (int i = 0; i < 4; ++i)
        if (word == dirs[i]) return i;
    return -1;
}

// La direccion por un color o una direccion del nombre: primero por palabras
// (`note impact 1 purple`) y, pegadas (`splash1Left`), por como acaba.
int directionIn(const std::string& name) {
    std::string word;
    for (size_t i = 0; i <= name.size(); ++i) {
        const char c = i < name.size() ? name[i] : ' ';
        if (std::isalpha(static_cast<unsigned char>(c))) {
            word += c;
            continue;
        }
        if (!word.empty()) {
            const int color = colorIndex(word);
            if (color >= 0) return color;
            const int dir = directionIndex(word);
            if (dir >= 0) return dir;
        }
        word.clear();
    }
    static const char* endings[8] = {"purple", "blue", "green", "red", "left", "down", "right", "up"};
    static const int lanes[8] = {0, 1, 2, 3, 0, 1, 3, 2};
    std::string letters;
    for (char c : name)
        if (std::isalpha(static_cast<unsigned char>(c))) letters += c;
    for (int i = 0; i < 8; ++i) {
        const std::string e = endings[i];
        if (letters.size() > e.size() && letters.compare(letters.size() - e.size(), e.size(), e) == 0) return lanes[i];
    }
    return -1;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

// ------------------------------------------------------------------- color --

bool Paint::changes() const {
    return (mode != PaintMode::Keep && strength > 0.001f) || alpha < 0.999f || (mark && !mark->empty() && markAlpha > 0.001f);
}

void paintImage(Image& image, const Paint& paint) {
    if (image.empty()) return;
    const float strength = std::clamp(paint.strength, 0.0f, 1.0f);
    if (paint.mode == PaintMode::Hue && strength > 0.001f) paintHue(image, paint.color, strength);
    if (paint.mode == PaintMode::Gradient && strength > 0.001f) paintGradient(image, paint.stops, strength);
    if (paint.mark && !paint.mark->empty() && paint.markAlpha > 0.001f)
        stampMark(image, *paint.mark, paint.markPlace, paint.markSize, paint.markAlpha);
    if (paint.alpha < 0.999f) {
        const float a = std::clamp(paint.alpha, 0.0f, 1.0f);
        for (size_t i = 0; i < static_cast<size_t>(image.w) * static_cast<size_t>(image.h); ++i)
            image.rgba[i * 4u + 3u] = byte(image.rgba[i * 4u + 3u] / 255.0f * a);
    }
}

void stampMark(Image& image, const Image& mark, MarkPlace place, float size, float alpha) {
    if (image.empty() || mark.empty()) return;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!visibleBox(image, x0, y0, x1, y1)) return;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    const float side = std::max(2.0f, std::min(bw, bh) * std::clamp(size, 0.05f, 1.0f));
    const float fit = side / static_cast<float>(std::max(mark.w, mark.h));
    const int mw = std::max(1, static_cast<int>(std::lround(mark.w * fit)));
    const int mh = std::max(1, static_cast<int>(std::lround(mark.h * fit)));
    const Image scaled = resizeImage(mark, mw, mh, false);
    int x = x0 + (bw - mw) / 2, y = y0 + (bh - mh) / 2;
    if (place == MarkPlace::Top) y = y0 + static_cast<int>(std::lround(bh * 0.08f));
    if (place == MarkPlace::Corner) {
        x = x1 - mw - static_cast<int>(std::lround(bw * 0.04f));
        y = y0 + static_cast<int>(std::lround(bh * 0.04f));
    }
    blendOver(image, scaled, x, y, std::clamp(alpha, 0.0f, 1.0f));
}

std::vector<PaintedSheet> paintStyleSheets(const NoteStyle& style, const std::function<Paint(Part, int)>& paintFor,
                                           const ExportIo& io) {
    std::vector<PaintedSheet> out;
    for (PaintedImage& painted : paintStyleImages(style, paintFor, io))
        out.push_back({painted.image, encodePng(painted.pixels), painted.atlas, painted.atlasText});
    return out;
}

namespace {

// Como queda una region pintada: dos piezas con la misma firma pueden
// compartirla; con otra, no.
std::string lookKey(const Paint& paint, const std::uint32_t* palette) {
    const bool tint = paint.mode != PaintMode::Keep && paint.strength > 0.001f;
    const bool marked = paint.mark && !paint.mark->empty() && paint.markAlpha > 0.001f;
    char key[320];
    std::snprintf(key, sizeof(key), "%d|%08X|%08X%08X%08X|%.4f|%.4f|%p|%d|%.4f|%.4f|%d|%08X%08X%08X",
                  tint ? static_cast<int>(paint.mode) : 0, tint ? paint.color : 0u, tint ? paint.stops[0] : 0u,
                  tint ? paint.stops[1] : 0u, tint ? paint.stops[2] : 0u, tint ? paint.strength : 0.0f,
                  std::clamp(paint.alpha, 0.0f, 1.0f), marked ? static_cast<const void*>(paint.mark) : nullptr,
                  marked ? static_cast<int>(paint.markPlace) : 0, marked ? paint.markSize : 0.0f, marked ? paint.markAlpha : 0.0f,
                  palette ? 1 : 0, palette ? palette[0] : 0u, palette ? palette[1] : 0u, palette ? palette[2] : 0u);
    return key;
}

// El atlas de Sparrow con los fotogramas de `moved` en su sitio nuevo; el resto
// de atributos se quedan como estaban. Vacio si no se puede leer.
std::string movedSparrow(const std::string& xml, const std::map<std::string, std::array<int, 2>>& moved) {
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str())) return {};
    pugi::xml_node atlas = doc.child("TextureAtlas");
    if (!atlas) return {};
    for (pugi::xml_node frame : atlas.children("SubTexture")) {
        const auto found = moved.find(frame.attribute("name").value());
        if (found == moved.end()) continue;
        frame.attribute("x").set_value(found->second[0]);
        frame.attribute("y").set_value(found->second[1]);
    }
    std::ostringstream out;
    doc.save(out, "\t", pugi::format_default, pugi::encoding_utf8);
    return out.str();
}

}  // namespace

std::vector<PaintedImage> paintStyleImages(const NoteStyle& style, const std::function<Paint(Part, int)>& paintFor,
                                           const ExportIo& io) {
    // Una region que usan varias piezas (la plantilla RGB de Psych 0.7 da la
    // misma a los cuatro tramos y a los cuatro finales, y `chip` a los aciertos
    // de las cuatro direcciones) se pinta una vez por cada aspecto distinto: el
    // primero en su sitio y los demas en copias debajo de la hoja, con sus
    // fotogramas movidos en el atlas. Solo en Sparrow: una rejilla o una tira
    // no puede mover una celda, y ahi la region es de la primera pieza.
    struct Work {
        Image image, original;
        bool loaded = false, failed = false, changed = false;
        std::string atlas;                                                         // el que se reescribe
        std::map<std::array<int, 5>, std::map<std::string, std::array<int, 2>>> looks;  // region -> aspecto -> donde
        std::map<std::string, std::string> lookOf;                                 // fotograma -> su aspecto
        std::map<std::string, std::array<int, 2>> moved;                           // fotograma -> su copia
        int shelfX = 0, shelfY = 0, shelfH = 0, usedH = 0;
    };
    std::map<std::string, Work> works;
    AtlasStore atlases;
    atlases.readText = [&io](const std::string& path) {
        if (!io.readText) return std::string();
        const auto text = io.readText(path);
        return text ? *text : std::string();
    };
    struct Job {
        const PartBinding* binding;
        const Sheet* sheet;
        Paint paint;
        const std::uint32_t* palette;
    };
    std::vector<Job> jobs;
    std::set<std::string> touched;
    for (const PartBinding& binding : style.parts) {
        if (binding.sheet < 0 || static_cast<size_t>(binding.sheet) >= style.sheets.size()) continue;
        const Sheet& sheet = style.sheets[static_cast<size_t>(binding.sheet)];
        if (sheet.image.empty()) continue;
        Job job{&binding, &sheet, paintFor ? paintFor(binding.part, binding.direction) : Paint{}, nullptr};
        // La plantilla RGB de Psych se hornea con los colores por defecto del
        // carril, menos el receptor en reposo, que el motor no recolorea nunca
        // (StrumNote.hx:169); como NoteExport con el destino sin paleta.
        if (style.rgbPalette && binding.part != Part::StrumStatic)
            job.palette = sheet.rgbFixed.size() == 3 ? sheet.rgbFixed.data() : psychDefaultPalette(binding.direction, sheet.pixel || style.pixel);
        if (job.paint.changes() || job.palette) touched.insert(sheet.image);
        jobs.push_back(job);
    }
    // Un hueco libre debajo de lo que ya hay, por estantes del ancho de la hoja.
    auto place = [](Work& work, int w, int h) -> std::array<int, 2> {
        const int width = std::max(work.original.w, w);
        if (work.shelfY == 0) work.shelfY = work.original.h + 2;
        if (work.shelfX > 0 && work.shelfX + w > width) {
            work.shelfY += work.shelfH + 2;
            work.shelfX = 0;
            work.shelfH = 0;
        }
        if (work.shelfY + h > 8192) return {-1, -1};
        const std::array<int, 2> at{work.shelfX, work.shelfY};
        work.shelfX += w + 2;
        work.shelfH = std::max(work.shelfH, h);
        work.usedH = std::max(work.usedH, at[1] + h);
        if (work.image.w < width || work.image.h < work.usedH) {
            Image grown = blankImage(std::max(work.image.w, width), std::max(work.usedH, work.image.h + 512));
            blit(grown, work.image, 0, 0);
            work.image = std::move(grown);
        }
        return at;
    };
    std::vector<std::string> order;
    // Las piezas que no cambian tambien reclaman su region, para que otra que
    // si cambia no les pinte encima.
    for (const Job& job : jobs) {
        const Sheet& sheet = *job.sheet;
        if (!touched.count(sheet.image)) continue;
        Work& work = works[sheet.image];
        if (!work.loaded) {
            work.loaded = true;
            order.push_back(sheet.image);
            const auto bytes = io.readBytes ? io.readBytes(sheet.image) : std::nullopt;
            work.failed = !bytes || !decodePng(*bytes, work.image);
            work.original = work.image;
            work.usedH = work.image.h;
            work.atlas = sheet.atlas;
        }
        if (work.failed) continue;
        const bool movable = sheet.kind == SheetKind::Sparrow && !sheet.atlas.empty() && sheet.atlas == work.atlas;
        const bool alters = job.paint.changes() || job.palette;
        // En una rejilla no hay copia posible: lo que no cambia no reclama nada.
        if (!alters && !movable) continue;
        const std::string look = lookKey(job.paint, job.palette);
        for (const AtlasFrame& frame : regionsOf(sheet, work.original, *job.binding, atlases)) {
            const std::array<int, 5> key{frame.x, frame.y, frame.w, frame.h, frame.rotated ? 1 : 0};
            auto& looks = work.looks[key];
            std::array<int, 2> at{frame.x, frame.y};
            const auto same = looks.find(look);
            if (same != looks.end()) {
                at = same->second;
            } else {
                const bool first = looks.empty();
                if (!first) {
                    if (!movable || frame.name.empty()) continue;
                    const auto owner = work.lookOf.find(frame.name);
                    if (owner != work.lookOf.end() && owner->second != look) continue;
                    at = place(work, frame.w, frame.h);
                    if (at[0] < 0) continue;
                }
                looks.emplace(look, at);
                if (alters || !first) {
                    Image content = cropFrame(work.original, frame);
                    if (job.palette) applyPsychPalette(content, job.palette);
                    paintImage(content, job.paint);
                    AtlasFrame target = frame;
                    target.x = at[0];
                    target.y = at[1];
                    putFrame(work.image, target, content);
                    work.changed = true;
                }
            }
            if (!movable || frame.name.empty()) continue;
            const auto owner = work.lookOf.emplace(frame.name, look);
            if (!owner.second && owner.first->second != look) continue;
            if (at[0] != frame.x || at[1] != frame.y) work.moved[frame.name] = at;
        }
    }
    std::vector<PaintedImage> out;
    for (const std::string& path : order) {
        Work& work = works[path];
        if (!work.changed || work.failed) continue;
        PaintedImage painted{path, std::move(work.image), {}, {}};
        if (!work.moved.empty()) {
            const std::string text = atlases.readText ? atlases.readText(work.atlas) : std::string();
            painted.atlasText = text.empty() ? std::string() : movedSparrow(text, work.moved);
            if (painted.atlasText.empty()) {
                // Sin atlas que reescribir, las copias no las veria nadie.
                painted.pixels = cropRect(painted.pixels, 0, 0, work.original.w, work.original.h);
            } else {
                painted.atlas = work.atlas;
                painted.pixels = cropRect(painted.pixels, 0, 0, painted.pixels.w, work.usedH);
            }
        } else if (painted.pixels.h != work.original.h || painted.pixels.w != work.original.w) {
            painted.pixels = cropRect(painted.pixels, 0, 0, work.original.w, work.original.h);
        }
        out.push_back(std::move(painted));
    }
    return out;
}

// ------------------------------------------------------------------- texto --

std::string fontFamilyName(const std::vector<unsigned char>& font) {
    stbtt_fontinfo info;
    if (!openFont(font, info)) return {};
    // Microsoft, Unicode BMP, ingles de EE. UU.: la familia (1) y, si hay, la
    // tipografica (16), que junta las variantes ("Segoe UI" + "Black").
    for (int id : {16, 1}) {
        int length = 0;
        const char* name = stbtt_GetFontNameString(&info, &length, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                   STBTT_MS_LANG_ENGLISH, id);
        if (name && length > 0) {
            std::string family = utf16beToUtf8(name, length);
            if (id == 16) {
                int subLength = 0;
                const char* sub = stbtt_GetFontNameString(&info, &subLength, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                          STBTT_MS_LANG_ENGLISH, 17);
                const std::string style = sub && subLength > 0 ? utf16beToUtf8(sub, subLength) : std::string();
                if (!style.empty() && style != "Regular") family += " " + style;
            } else {
                int subLength = 0;
                const char* sub = stbtt_GetFontNameString(&info, &subLength, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                          STBTT_MS_LANG_ENGLISH, 2);
                const std::string style = sub && subLength > 0 ? utf16beToUtf8(sub, subLength) : std::string();
                if (!style.empty() && style != "Regular") family += " " + style;
            }
            return family;
        }
    }
    return {};
}

bool fontHasCodepoint(const std::vector<unsigned char>& font, unsigned codepoint) {
    stbtt_fontinfo info;
    return openFont(font, info) && stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) != 0;
}

Image renderText(const std::vector<unsigned char>& font, const std::string& utf8, const TextLook& look) {
    stbtt_fontinfo info;
    if (!openFont(font, info) || look.size <= 0.0f) return {};
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, look.size);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    struct Glyph { int index; float x; };
    std::vector<Glyph> glyphs;
    float pen = 0.0f;
    int previous = 0;
    for (unsigned cp : codepoints(utf8)) {
        const int glyph = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
        if (glyph == 0 && cp != ' ') continue;
        if (previous) pen += stbtt_GetGlyphKernAdvance(&info, previous, glyph) * scale;
        glyphs.push_back({glyph, pen});
        int advance = 0, bearing = 0;
        stbtt_GetGlyphHMetrics(&info, glyph, &advance, &bearing);
        pen += advance * scale + look.spacing;
        previous = glyph;
    }
    if (glyphs.empty()) return {};
    const float outline = std::max(0.0f, look.outlineWidth);
    const float shadowReach = look.shadow ? std::max(std::fabs(look.shadowX), std::fabs(look.shadowY)) : 0.0f;
    const int pad = static_cast<int>(std::ceil(outline + shadowReach)) + 4;
    const float baseline = ascent * scale;
    const int textH = static_cast<int>(std::ceil((ascent - descent) * scale));
    const int w = static_cast<int>(std::ceil(pen)) + pad * 2 + 4;
    const int h = textH + pad * 2;
    std::vector<float> mask(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    int top = h, bottom = 0;
    for (const Glyph& g : glyphs) {
        const float shift = g.x - std::floor(g.x);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&info, g.index, scale, scale, shift, 0.0f, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw <= 0 || gh <= 0) continue;
        std::vector<unsigned char> bitmap(static_cast<size_t>(gw) * static_cast<size_t>(gh));
        stbtt_MakeGlyphBitmapSubpixel(&info, bitmap.data(), gw, gh, gw, scale, scale, shift, 0.0f, g.index);
        const int ox = pad + 2 + static_cast<int>(std::floor(g.x)) + x0;
        const int oy = pad + static_cast<int>(std::lround(baseline)) + y0;
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x) {
                const int tx = ox + x, ty = oy + y;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
                float& m = mask[static_cast<size_t>(ty) * static_cast<size_t>(w) + static_cast<size_t>(tx)];
                m = std::max(m, bitmap[static_cast<size_t>(y) * static_cast<size_t>(gw) + static_cast<size_t>(x)] / 255.0f);
            }
        top = std::min(top, oy);
        bottom = std::max(bottom, oy + gh);
    }
    if (bottom <= top) return {};
    if (look.pixel)
        for (float& m : mask) m = m >= 0.5f ? 1.0f : 0.0f;
    std::vector<float> shape = outline > 0.0f ? dilate(mask, w, h, outline) : mask;
    if (look.pixel)
        for (float& m : shape) m = m >= 0.5f ? 1.0f : 0.0f;
    Image out = blankImage(w, h);
    auto put = [&](int x, int y, std::uint32_t argb, float coverage) {
        if (coverage > 0.0f) blendPixel(out, x, y, channel(argb, 16), channel(argb, 8), channel(argb, 0), coverage * channel(argb, 24));
    };
    if (look.shadow) {
        const int sx = static_cast<int>(std::lround(look.shadowX)), sy = static_cast<int>(std::lround(look.shadowY));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) put(x + sx, y + sy, look.shadowColor, shape[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]);
    }
    if (outline > 0.0f)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) put(x, y, look.outline, shape[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]);
    // El relleno, en degradado de arriba abajo de lo escrito.
    const float span = static_cast<float>(std::max(1, bottom - top - 1));
    for (int y = 0; y < h; ++y) {
        const float t = std::clamp((y - top) / span, 0.0f, 1.0f);
        float c[4];
        for (int k = 0; k < 4; ++k) {
            const int shift = k == 3 ? 24 : 16 - k * 8;
            c[k] = channel(look.top, shift) + (channel(look.bottom, shift) - channel(look.top, shift)) * t;
        }
        for (int x = 0; x < w; ++x)
            blendPixel(out, x, y, c[0], c[1], c[2], c[3] * mask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]);
    }
    // Inclinada como una cursiva, sobre la base del texto.
    if (std::fabs(look.tiltDeg) > 0.05f) {
        const float slope = std::tan(look.tiltDeg * 3.14159265f / 180.0f);
        const int extra = static_cast<int>(std::ceil(std::fabs(slope) * h));
        Image tilted = blankImage(w + extra, h);
        for (int y = 0; y < h; ++y) {
            const float dx = slope * static_cast<float>(h - y) + (slope < 0.0f ? static_cast<float>(extra) : 0.0f);
            for (int x = 0; x < tilted.w; ++x) {
                const float source = static_cast<float>(x) - dx;
                if (look.pixel) {
                    const int sx = static_cast<int>(std::floor(source + 0.5f));
                    if (sx >= 0 && sx < w) std::copy_n(out.at(sx, y), 4, tilted.at(x, y));
                    continue;
                }
                const int sx = static_cast<int>(std::floor(source));
                const float f = source - static_cast<float>(sx);
                float acc[4] = {0, 0, 0, 0};
                for (int k = 0; k < 2; ++k) {
                    const int px = sx + k;
                    if (px < 0 || px >= w) continue;
                    const float wgt = k == 0 ? 1.0f - f : f;
                    const std::uint8_t* p = out.at(px, y);
                    const float a = p[3] / 255.0f * wgt;
                    acc[0] += p[0] / 255.0f * a;
                    acc[1] += p[1] / 255.0f * a;
                    acc[2] += p[2] / 255.0f * a;
                    acc[3] += a;
                }
                std::uint8_t* t = tilted.at(x, y);
                for (int c = 0; c < 3; ++c) t[c] = byte(acc[3] > 0.0f ? acc[c] / acc[3] : 0.0f);
                t[3] = byte(acc[3]);
            }
        }
        out = std::move(tilted);
    }
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!visibleBox(out, x0, y0, x1, y1)) return {};
    return cropRect(out, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

// ------------------------------------------------------------------- marcas --

const char* builtinMarkKey(BuiltinMark mark) {
    switch (mark) {
        case BuiltinMark::Skull: return "skull";
        case BuiltinMark::Bolt: return "bolt";
        case BuiltinMark::Heart: return "heart";
        case BuiltinMark::Star: return "star";
        case BuiltinMark::Exclamation: return "exclamation";
        case BuiltinMark::Dot: return "dot";
        case BuiltinMark::Eye: return "eye";
    }
    return "";
}

unsigned builtinMarkCodepoint(BuiltinMark mark) {
    switch (mark) {
        case BuiltinMark::Skull: return 0x2620;
        case BuiltinMark::Bolt: return 0x26A1;
        case BuiltinMark::Heart: return 0x2665;
        case BuiltinMark::Star: return 0x2605;
        case BuiltinMark::Exclamation: return 0x0021;
        case BuiltinMark::Dot: return 0x25CF;
        case BuiltinMark::Eye: return 0x25C9;
    }
    return 0x25CF;
}

Image builtinMarkImage(BuiltinMark mark, int size, const std::vector<unsigned char>& symbolFont) {
    size = std::max(8, size);
    const unsigned cp = builtinMarkCodepoint(mark);
    if (fontHasCodepoint(symbolFont, cp)) {
        TextLook look;
        look.size = static_cast<float>(size);
        look.outline = 0xFF141414u;
        look.outlineWidth = std::max(1.0f, size / 12.0f);
        std::string text;
        if (cp < 0x80) text += static_cast<char>(cp);
        else {
            text += static_cast<char>(0xE0 | (cp >> 12));
            text += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (cp & 0x3F));
        }
        Image glyph = renderText(symbolFont, text, look);
        if (!glyph.empty()) return glyph;
    }
    // Sin la fuente: un circulo blanco con borde, que siempre se ve.
    Image dot = blankImage(size, size);
    const float r = size * 0.5f, border = std::max(1.0f, size / 12.0f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float d = std::hypot(x + 0.5f - r, y + 0.5f - r);
            const float outside = std::clamp(r - d, 0.0f, 1.0f);
            if (outside <= 0.0f) continue;
            const float inner = std::clamp(r - border - d, 0.0f, 1.0f);
            std::uint8_t* p = dot.at(x, y);
            const float v = 0.08f + 0.88f * inner;
            p[0] = p[1] = p[2] = byte(v);
            p[3] = byte(outside);
        }
    return dot;
}

// ------------------------------------------------------- reconocer lo importado --

PieceGuess guessPiece(const std::string& frameName) {
    PieceGuess g;
    const std::string full = lowerTrim(frameName);
    const std::string n = withoutFrameNumber(full);
    if (n.empty()) return g;
    auto set = [&](Part part, int direction, int variant = 0) {
        g.found = direction >= 0;
        g.part = part;
        g.direction = std::max(0, direction);
        g.variant = variant;
        return g;
    };
    // Codename y Psych (Note.hx:163-183 de Codename, :436-441 de Psych): el
    // final morado con la errata del juego base.
    if (n == "pruple end hold") return set(Part::HoldEnd, 0);
    {
        const size_t space = n.find(' ');
        const std::string first = space == std::string::npos ? n : n.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : n.substr(space + 1);
        const int color = colorIndex(first);
        if (color >= 0) {
            if (rest.empty() || rest == "scroll") return set(Part::Note, color);
            if (rest == "hold piece" || rest == "hold") return set(Part::HoldPiece, color);
            if (rest == "hold end" || rest == "end hold" || rest == "tail") return set(Part::HoldEnd, color);
        }
        const int dir = directionIndex(first);
        if (dir >= 0) {
            if (rest == "press") return set(Part::StrumPress, dir);
            if (rest == "confirm") return set(Part::StrumConfirm, dir);
            if (rest == "confirm hold") return set(Part::StrumConfirmHold, dir);
        }
        // Las letras de Psych Engine Extra Keys con 4 teclas (Note.hx:55 y
        // :342-350, StrumNote.hx:81-83 del fork): A, B, C y D.
        if (first.size() == 1 && first[0] >= 'a' && first[0] <= 'd') {
            const int letter = first[0] - 'a';
            if (rest.empty()) return set(Part::Note, letter);
            if (rest == "hold") return set(Part::HoldPiece, letter);
            if (rest == "tail") return set(Part::HoldEnd, letter);
            if (rest == "press") return set(Part::StrumPress, letter);
            if (rest == "confirm") return set(Part::StrumConfirm, letter);
        }
    }
    if (startsWith(n, "arrow")) return set(Part::StrumStatic, directionIndex(n.substr(5)));
    // V-Slice (assets/data/notestyles/funkin.json): noteLeft, staticLeft,
    // pressLeft, confirmLeft y las coberturas holdCoverStart/holdCover/holdCoverEnd.
    for (const auto& [prefix, part] : std::initializer_list<std::pair<const char*, Part>>{
             {"note", Part::Note}, {"static", Part::StrumStatic}, {"press", Part::StrumPress},
             {"confirmhold", Part::StrumConfirmHold}, {"confirm", Part::StrumConfirm}}) {
        if (!startsWith(n, prefix)) continue;
        const int dir = directionIndex(n.substr(std::string(prefix).size()));
        if (dir >= 0) return set(part, dir);
    }
    if (startsWith(n, "holdcover")) {
        std::string rest = n.substr(9);
        Part part = Part::HoldCover;
        if (startsWith(rest, "start")) { part = Part::HoldCoverStart; rest = rest.substr(5); }
        else if (startsWith(rest, "end")) { part = Part::HoldCoverEnd; rest = rest.substr(3); }
        int dir = colorIndex(rest);
        if (dir < 0) dir = directionIndex(rest);
        if (dir >= 0) return set(part, dir);
    }
    // Salpicaduras: `note impact 1 purple` (Codename y Psych 0.6), `note splash
    // purple 1` (Psych 0.7+), `splash1Left`... El numero es la variante.
    if (n.find("impact") != std::string::npos || n.find("splash") != std::string::npos) {
        const int dir = directionIn(n);
        int variant = 0;
        bool numbered = false;
        for (char c : n)
            if (c >= '1' && c <= '9') {
                variant = c - '1';
                numbered = true;
                break;
            }
        // `note splash purple 1` lleva la variante pegada al numero de
        // fotograma (`...10003`): 1 cifra sola o 1 + 4 es variante y fotograma.
        if (!numbered && full.size() > n.size()) {
            std::string digits;
            for (char c : full.substr(n.size()))
                if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
            if ((digits.size() == 1 || digits.size() == 5) && digits[0] >= '1' && digits[0] <= '9') variant = digits[0] - '1';
        }
        if (dir >= 0) return set(Part::Splash, dir, variant);
    }
    return g;
}

// ------------------------------------------------------------------ recetas --

namespace {

std::uint32_t mixColor(std::uint32_t a, std::uint32_t b, float t) {
    std::uint32_t out = 0;
    for (int shift : {24, 16, 8, 0}) {
        const float v = channel(a, shift) + (channel(b, shift) - channel(a, shift)) * t;
        out |= static_cast<std::uint32_t>(byte(v)) << shift;
    }
    return out;
}

Paint huePaint(std::uint32_t color, float strength) {
    Paint p;
    p.mode = PaintMode::Hue;
    p.color = color;
    p.strength = strength;
    return p;
}

// Tres colores por la luz: el contorno, el color y casi blanco.
Paint outlinePaint(std::uint32_t outline, std::uint32_t color, float strength) {
    Paint p;
    p.mode = PaintMode::Gradient;
    p.stops = {outline, color, mixColor(color, 0xFFFFFFFFu, 0.85f)};
    p.strength = strength;
    return p;
}

std::uint32_t hsvColor(float h, float s, float v) {
    Hsl c;
    // De HSV a HSL para fromHsl.
    const float l = v * (1.0f - s * 0.5f);
    c.h = h - std::floor(h);
    c.s = (l <= 0.0f || l >= 1.0f) ? 0.0f : (v - l) / std::min(l, 1.0f - l);
    c.l = l;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    fromHsl(c, r, g, b);
    return 0xFF000000u | (static_cast<std::uint32_t>(byte(r)) << 16) | (static_cast<std::uint32_t>(byte(g)) << 8) | byte(b);
}

}  // namespace

Paint arrowPaint(const ArrowRecipe& recipe, Part part, int direction) {
    const std::uint32_t color = recipe.colors[static_cast<size_t>(direction & 3)];
    auto colored = [&](std::uint32_t c) {
        return recipe.outline ? outlinePaint(recipe.outline, c, recipe.strength) : huePaint(c, recipe.strength);
    };
    switch (part) {
        case Part::Note:
            return colored(color);
        case Part::HoldPiece:
        case Part::HoldEnd:
            return colored(recipe.holds == HoldLook::Lighter ? mixColor(color, 0xFFFFFFFFu, 0.35f) : color);
        case Part::StrumStatic:
            if (recipe.strums == StrumLook::Colored) return colored(mixColor(color, 0xFF808080u, 0.35f));
            if (recipe.strums == StrumLook::Clear) {
                Paint p;
                p.alpha = 0.45f;
                return p;
            }
            return Paint{};
        case Part::StrumPress:
            return colored(mixColor(color, 0xFF808080u, 0.25f));
        case Part::StrumConfirm:
        case Part::StrumConfirmHold:
            if (recipe.confirm == ConfirmLook::White) {
                Paint p = huePaint(0xFFFFFFFFu, recipe.strength);
                p.mode = PaintMode::Gradient;
                p.stops = {recipe.outline ? recipe.outline : 0xFF202020u, 0xFFE8E8E8u, 0xFFFFFFFFu};
                return p;
            }
            return colored(color);
        case Part::Splash:
        case Part::HoldCoverStart:
        case Part::HoldCover:
        case Part::HoldCoverEnd:
            return recipe.splashes ? huePaint(color, recipe.strength) : Paint{};
    }
    return Paint{};
}

const std::vector<std::string>& arrowPresetKeys() {
    static const std::vector<std::string> keys = {"classic", "pastel", "neon", "mono", "inverted", "fireice", "random"};
    return keys;
}

bool arrowPreset(const std::string& key, std::array<std::uint32_t, 4>& colors, std::uint32_t seed) {
    // Los del juego base (Psych ClientPrefs.hx:28-37, los mismos que pinta
    // Codename en default.png).
    if (key == "classic") colors = {0xFFC24B99u, 0xFF00FFFFu, 0xFF12FA05u, 0xFFF9393Fu};
    else if (key == "pastel") colors = {0xFFC99BD8u, 0xFF8EC7E6u, 0xFF93DDA2u, 0xFFE89AA6u};
    else if (key == "neon") colors = {0xFFFF2BD6u, 0xFF00E5FFu, 0xFF39FF14u, 0xFFFF3131u};
    else if (key == "mono") colors = {0xFFD8D8D8u, 0xFFB8B8B8u, 0xFFB8B8B8u, 0xFFD8D8D8u};
    else if (key == "inverted") colors = {0xFF3DB466u, 0xFFFF0000u, 0xFFED05FAu, 0xFF06C6C0u};
    else if (key == "fireice") colors = {0xFFFF5A1Fu, 0xFF4FC3FFu, 0xFF9BE7FFu, 0xFFFFB020u};
    else if (key == "random") {
        // Cuatro tonos separados con la misma semilla, siempre iguales.
        std::uint32_t state = seed * 2654435761u + 0x9E3779B9u;
        auto next = [&]() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (state & 0xFFFFu) / 65535.0f;
        };
        const float start = next();
        for (int d = 0; d < 4; ++d)
            colors[static_cast<size_t>(d)] = hsvColor(start + d * 0.25f + (next() - 0.5f) * 0.08f, 0.65f + next() * 0.3f, 0.85f + next() * 0.15f);
    } else {
        return false;
    }
    return true;
}

Paint typePaint(const TypeLookRecipe& recipe, Part part, int direction, const Image* mark) {
    Paint paint;
    switch (recipe.color) {
        case TypeColor::Keep:
            break;
        case TypeColor::One:
            paint = huePaint(recipe.one, recipe.strength);
            break;
        case TypeColor::PerDirection:
            paint = huePaint(recipe.perDirection[static_cast<size_t>(direction & 3)], recipe.strength);
            break;
        case TypeColor::Palette:
            paint.mode = PaintMode::Gradient;
            paint.stops = recipe.palette;
            paint.strength = recipe.strength;
            break;
    }
    paint.alpha = recipe.alpha;
    switch (part) {
        case Part::Note:
            paint.mark = mark;
            paint.markPlace = recipe.markPlace;
            paint.markSize = recipe.markSize;
            paint.markAlpha = recipe.markAlpha;
            return paint;
        case Part::HoldPiece:
        case Part::HoldEnd:
            if (recipe.hold == TypeHold::Keep) return Paint{};
            if (recipe.hold == TypeHold::Hidden) {
                Paint hidden;
                hidden.alpha = 0.0f;
                return hidden;
            }
            return paint;
        case Part::Splash:
            if (recipe.splash != TypeSplash::Recolored) return Paint{};
            paint.alpha = 1.0f;
            return paint;
        default:
            return Paint{};
    }
}

const std::vector<std::string>& typeLookPresetKeys() {
    static const std::vector<std::string> keys = {"poison", "hurt", "ice", "fire", "gold", "ghost", "rainbow"};
    return keys;
}

bool typeLookPreset(const std::string& key, TypeLookRecipe& recipe) {
    TypeLookRecipe r;
    if (key == "poison") {
        r.one = 0xFF3FD14Au;
        r.mark = static_cast<int>(BuiltinMark::Skull);
    } else if (key == "hurt") {
        // Como la Hurt Note de Psych (Note.hx:206-208): casi negra con rojo.
        r.color = TypeColor::Palette;
        r.palette = {0xFF101010u, 0xFF990022u, 0xFFFF0000u};
        r.strength = 1.0f;
    } else if (key == "ice") {
        r.one = 0xFF5ED8F0u;
        r.mark = static_cast<int>(BuiltinMark::Star);
        r.markSize = 0.36f;
    } else if (key == "fire") {
        r.one = 0xFFFF8A2Au;
        r.mark = static_cast<int>(BuiltinMark::Bolt);
    } else if (key == "gold") {
        r.one = 0xFFF2C230u;
        r.mark = static_cast<int>(BuiltinMark::Star);
        r.markSize = 0.36f;
    } else if (key == "ghost") {
        r.one = 0xFFAAB2C4u;
        r.strength = 1.0f;
        r.alpha = 0.55f;
    } else if (key == "rainbow") {
        r.color = TypeColor::PerDirection;
        r.perDirection = {0xFFFF4D4Du, 0xFFFFD84Du, 0xFF4DFF88u, 0xFF4D9BFFu};
    } else {
        return false;
    }
    recipe = r;
    return true;
}

std::vector<Image> renderRatingSet(const RatingRecipe& recipe, const std::vector<unsigned char>& font) {
    std::vector<Image> out;
    TextLook look;
    look.size = recipe.size;
    look.tiltDeg = recipe.tiltDeg;
    look.spacing = recipe.spacing;
    look.outline = recipe.outline;
    look.outlineWidth = recipe.outlineWidth;
    look.shadow = recipe.shadow;
    look.pixel = recipe.pixel;
    const float shadowScale = recipe.size / 120.0f;
    look.shadowX = 6.0f * shadowScale;
    look.shadowY = 8.0f * shadowScale;
    for (size_t i = 0; i < 5; ++i) {
        look.top = recipe.colors[i][0];
        look.bottom = recipe.colors[i][1];
        out.push_back(renderText(font, recipe.texts[i], look));
    }
    // Las cifras derechas: el motor las pone en fila y la inclinacion las
    // separaria de la base comun.
    TextLook digit = look;
    digit.size = recipe.digitSize;
    digit.tiltDeg = 0.0f;
    digit.top = recipe.colors[4][0];
    digit.bottom = recipe.colors[4][1];
    const bool hasCombo = recipe.texts[4].find_first_not_of(" \t\r\n") != std::string::npos;
    for (int n = 0; n < 10; ++n)
        out.push_back(hasCombo ? renderText(font, std::string(1, static_cast<char>('0' + n)), digit) : Image{});
    return out;
}

// ------------------------------------------------------------------ importar --

SparrowOut packSparrow(const std::vector<NamedFrame>& frames, const std::string& imageName, int padding) {
    SparrowOut out;
    struct Block { Image image; };
    std::vector<Image> blocks;
    std::map<std::vector<std::uint8_t>, int> blockOf;
    struct Placed { std::string name; int block = -1; int frameX = 0, frameY = 0, frameW = 0, frameH = 0; };
    std::vector<Placed> placed;
    for (const NamedFrame& frame : frames) {
        if (frame.image.empty()) continue;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        Image trimmed;
        if (visibleBox(frame.image, x0, y0, x1, y1)) trimmed = cropRect(frame.image, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
        else {
            x0 = y0 = 0;
            trimmed = blankImage(1, 1);
        }
        std::vector<std::uint8_t> key = trimmed.rgba;
        key.push_back(static_cast<std::uint8_t>(trimmed.w & 0xFF));
        key.push_back(static_cast<std::uint8_t>((trimmed.w >> 8) & 0xFF));
        int block = -1;
        const auto found = blockOf.find(key);
        if (found != blockOf.end()) block = found->second;
        else {
            block = static_cast<int>(blocks.size());
            blocks.push_back(std::move(trimmed));
            blockOf.emplace(std::move(key), block);
        }
        // Sparrow guarda el desplazamiento negativo: el contenido empieza en
        // (-frameX, -frameY) de la caja del fotograma.
        placed.push_back({frame.name, block, -x0, -y0, frame.image.w, frame.image.h});
    }
    if (placed.empty()) return out;
    std::vector<std::array<int, 2>> sizes;
    for (const Image& b : blocks) sizes.push_back({b.w, b.h});
    const PackResult pack = packRects(sizes, std::max(0, padding));
    if (!pack.ok) return out;
    Image sheet = blankImage(pack.w, pack.h);
    for (size_t i = 0; i < blocks.size(); ++i) blit(sheet, blocks[i], pack.positions[i][0], pack.positions[i][1]);
    std::string escaped;
    auto escape = [](const std::string& text) {
        std::string e;
        for (char c : text) {
            if (c == '&') e += "&amp;";
            else if (c == '<') e += "&lt;";
            else if (c == '>') e += "&gt;";
            else if (c == '"') e += "&quot;";
            else e += c;
        }
        return e;
    };
    out.xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- Note Lab (Funkin Mod Lab) -->\n<TextureAtlas imagePath=\"" +
              escape(imageName) + "\">\n";
    for (const Placed& p : placed) {
        const Image& b = blocks[static_cast<size_t>(p.block)];
        const auto& at = pack.positions[static_cast<size_t>(p.block)];
        out.xml += "\t<SubTexture name=\"" + escape(p.name) + "\" x=\"" + std::to_string(at[0]) + "\" y=\"" + std::to_string(at[1]) +
                   "\" width=\"" + std::to_string(b.w) + "\" height=\"" + std::to_string(b.h) + "\"";
        if (p.frameX != 0 || p.frameY != 0 || p.frameW != b.w || p.frameH != b.h)
            out.xml += " frameX=\"" + std::to_string(p.frameX) + "\" frameY=\"" + std::to_string(p.frameY) + "\" frameWidth=\"" +
                       std::to_string(p.frameW) + "\" frameHeight=\"" + std::to_string(p.frameH) + "\"";
        out.xml += "/>\n";
    }
    out.xml += "</TextureAtlas>\n";
    out.png = encodePng(sheet);
    out.frames = static_cast<int>(placed.size());
    out.ok = !out.png.empty();
    return out;
}

bool decodeGif(const std::vector<unsigned char>& bytes, GifFrames& out) {
    out = GifFrames{};
    if (bytes.size() < 13 || bytes.size() > 64u * 1024u * 1024u || bytes[0] != 'G' || bytes[1] != 'I' || bytes[2] != 'F') return false;
    const int gw = bytes[6] | bytes[7] << 8, gh = bytes[8] | bytes[9] << 8;
    if (gw <= 0 || gh <= 0 || gw > 8192 || gh > 8192 || static_cast<size_t>(gw) * gh > 32u * 1024u * 1024u) return false;
    size_t cursor = 13 + (bytes[10] & 0x80 ? 3u * (2u << (bytes[10] & 7)) : 0u);
    int frameCount = 0;
    auto skipBlocks = [&]() {
        while (cursor < bytes.size()) { const size_t n = bytes[cursor++]; if (n == 0) return true; if (n > bytes.size() - cursor) return false; cursor += n; }
        return false;
    };
    while (cursor < bytes.size()) {
        const unsigned char tag = bytes[cursor++];
        if (tag == 0x3b) break;
        if (tag == 0x21) { if (cursor >= bytes.size()) return false; ++cursor; if (!skipBlocks()) return false; }
        else if (tag == 0x2c) {
            if (bytes.size() - cursor < 9) return false;
            const unsigned char flags = bytes[cursor + 8]; cursor += 9;
            if (flags & 0x80) cursor += 3u * (2u << (flags & 7));
            if (cursor >= bytes.size()) return false;
            ++cursor;
            if (++frameCount > 512 || static_cast<size_t>(frameCount) * gw * gh > 32u * 1024u * 1024u || !skipBlocks()) return false;
        } else return false;
    }
    if (frameCount == 0) return false;
    int* delays = nullptr;
    int w = 0, h = 0, layers = 0, channels = 0;
    unsigned char* pixels = stbi_load_gif_from_memory(bytes.data(), static_cast<int>(bytes.size()), &delays, &w, &h, &layers, &channels, 4);
    if (!pixels) return false;
    const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    for (int i = 0; i < layers; ++i) {
        Image frame = blankImage(w, h);
        std::copy_n(pixels + frameBytes * static_cast<size_t>(i), frameBytes, frame.rgba.data());
        out.frames.push_back(std::move(frame));
        // Un GIF sin retardo se ve a 10 FPS en los navegadores.
        out.delaysMs.push_back(delays && delays[i] > 0 ? delays[i] : 100);
    }
    stbi_image_free(pixels);
    if (delays) STBI_FREE(delays);
    return !out.frames.empty();
}

namespace {

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().u8string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

std::vector<unsigned char> readFileBytes(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 64u * 1024u * 1024u) return {};
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool pngSize(const std::filesystem::path& path, int& w, int& h) {
    // La cabecera IHDR: ancho y alto en big endian tras la firma.
    std::ifstream in(path, std::ios::binary);
    unsigned char head[24] = {};
    in.read(reinterpret_cast<char*>(head), 24);
    if (in.gcount() < 24 || head[1] != 'P' || head[2] != 'N' || head[3] != 'G') return false;
    w = (head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19];
    h = (head[20] << 24) | (head[21] << 16) | (head[22] << 8) | head[23];
    return true;
}

// Los nombres de animacion de un atlas: el de cada fotograma sin su numero,
// en el orden en que aparecen.
std::vector<ImportAnimation> atlasAnimations(const std::filesystem::path& atlasPath) {
    std::vector<ImportAnimation> out;
    std::error_code ec;
    const auto size = std::filesystem::file_size(atlasPath, ec);
    if (ec || size > 4u * 1024u * 1024u) return out;
    const auto bytes = readFileBytes(atlasPath);
    const std::string text(bytes.begin(), bytes.end());
    DiagnosticSink sink;
    const bool packer = lowerExtension(atlasPath) == ".txt";
    const std::string source = atlasPath.u8string();
    const Result<SparrowAtlas> atlas = packer ? parsePackerAtlas(text, source, sink) : parseSparrowAtlas(text, source, sink);
    if (!atlas) return out;
    std::map<std::string, size_t> index;
    for (const AtlasFrame& frame : atlas.value().frames) {
        const std::string base = withoutFrameNumber(frame.name);
        const auto found = index.find(base);
        if (found != index.end()) {
            ++out[found->second].frames;
            continue;
        }
        index.emplace(base, out.size());
        ImportAnimation anim;
        anim.name = base;
        anim.frames = 1;
        anim.piece = guessPiece(frame.name);
        if (!anim.piece.found) anim.hudIndex = guessHudIndex(base);
        out.push_back(anim);
    }
    return out;
}

void scanOne(const std::filesystem::path& path, std::vector<ImportItem>& out, std::set<std::filesystem::path>& used, int depth);

void scanFolder(const std::filesystem::path& folder, std::vector<ImportItem>& out, std::set<std::filesystem::path>& used, int depth) {
    std::error_code ec;
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entries.size() >= 4096 || out.size() >= 512) break;
        entries.push_back(entry.path());
    }
    std::sort(entries.begin(), entries.end());
    // Los PNG con numero de fotograma y sin atlas al lado, juntos por nombre base.
    std::map<std::string, std::vector<std::filesystem::path>> sequences;
    for (const auto& path : entries) {
        if (lowerExtension(path) != ".png") continue;
        std::filesystem::path xml = path, txt = path;
        xml.replace_extension(".xml");
        txt.replace_extension(".txt");
        if (std::filesystem::exists(xml, ec) || std::filesystem::exists(txt, ec)) continue;
        const std::string stem = path.stem().u8string();
        if (guessHudIndex(stem) >= 0) continue;
        const std::string base = withoutFrameNumber(lowerTrim(stem));
        if (base.size() == lowerTrim(stem).size()) continue;   // sin numero: imagen suelta
        sequences[base].push_back(path);
    }
    for (auto& [base, files] : sequences) {
        if (files.size() < 2) continue;
        ImportItem item;
        item.kind = ImportKind::Frames;
        item.path = folder;
        item.files = files;
        item.label = folder.filename().u8string() + "/" + base;
        pngSize(files.front(), item.width, item.height);
        ImportAnimation anim;
        anim.name = base;
        anim.frames = static_cast<int>(files.size());
        anim.piece = guessPiece(files.front().stem().u8string());
        if (!anim.piece.found) anim.hudIndex = guessHudIndex(base);
        item.animations.push_back(anim);
        for (const auto& f : files) used.insert(f);
        out.push_back(std::move(item));
    }
    for (const auto& path : entries)
        if (!used.count(path)) scanOne(path, out, used, depth + 1);
}

void scanOne(const std::filesystem::path& path, std::vector<ImportItem>& out, std::set<std::filesystem::path>& used, int depth) {
    std::error_code ec;
    if (used.count(path) || out.size() >= 512) return;
    if (std::filesystem::is_directory(path, ec)) {
        if (depth < 2) scanFolder(path, out, used, depth);
        return;
    }
    used.insert(path);
    const std::string ext = lowerExtension(path);
    ImportItem item;
    item.path = path;
    item.label = path.filename().u8string();
    if (ext == ".png") {
        std::filesystem::path xml = path, txt = path;
        xml.replace_extension(".xml");
        txt.replace_extension(".txt");
        pngSize(path, item.width, item.height);
        if (std::filesystem::exists(xml, ec) || std::filesystem::exists(txt, ec)) {
            item.kind = ImportKind::Atlas;
            item.atlas = std::filesystem::exists(xml, ec) ? xml : txt;
            used.insert(item.atlas);
            item.label += " + " + item.atlas.extension().u8string().substr(1);
            item.animations = atlasAnimations(item.atlas);
        } else {
            item.kind = ImportKind::Image;
            item.hudIndex = guessHudIndex(path.stem().u8string());
            if (item.hudIndex < 0) item.piece = guessPiece(path.stem().u8string());
        }
    } else if (ext == ".xml" || ext == ".txt") {
        // Un atlas elegido por su XML: va con su PNG.
        std::filesystem::path png = path;
        png.replace_extension(".png");
        if (std::filesystem::exists(png, ec) && !used.count(png)) {
            used.erase(path);
            scanOne(png, out, used, depth);
        }
        return;
    } else if (ext == ".gif") {
        item.kind = ImportKind::Gif;
        GifFrames gif;
        if (decodeGif(readFileBytes(path), gif)) {
            item.width = gif.frames.front().w;
            item.height = gif.frames.front().h;
            ImportAnimation anim;
            anim.name = path.stem().u8string();
            anim.frames = static_cast<int>(gif.frames.size());
            anim.piece = guessPiece(anim.name);
            if (!anim.piece.found) anim.hudIndex = guessHudIndex(anim.name);
            item.animations.push_back(anim);
        }
    } else if (ext == ".ogg" || ext == ".wav" || ext == ".mp3") {
        item.kind = ImportKind::Sound;
        item.hudIndex = guessHudIndex(path.stem().u8string());
    } else if (ext == ".ttf" || ext == ".otf") {
        item.kind = ImportKind::Font;
        item.family = fontFamilyName(readFileBytes(path));
    } else {
        return;
    }
    out.push_back(std::move(item));
}

}  // namespace

std::vector<ImportItem> scanImport(const std::vector<std::filesystem::path>& paths) {
    std::vector<ImportItem> out;
    std::set<std::filesystem::path> used;
    for (const auto& path : paths) scanOne(path, out, used, 0);
    return out;
}

const char* importKindKey(ImportKind kind) {
    switch (kind) {
        case ImportKind::Atlas: return "atlas";
        case ImportKind::Image: return "image";
        case ImportKind::Frames: return "frames";
        case ImportKind::Gif: return "gif";
        case ImportKind::Sound: return "sound";
        case ImportKind::Font: return "font";
        case ImportKind::Unknown: break;
    }
    return "unknown";
}

std::vector<Image> importFrames(const ImportItem& item, int animation) {
    std::vector<Image> out;
    switch (item.kind) {
        case ImportKind::Image: {
            Image image;
            if (decodePng(readFileBytes(item.path), image)) out.push_back(std::move(image));
            break;
        }
        case ImportKind::Frames:
            for (const auto& file : item.files) {
                Image image;
                if (decodePng(readFileBytes(file), image)) out.push_back(std::move(image));
            }
            break;
        case ImportKind::Gif: {
            GifFrames gif;
            if (decodeGif(readFileBytes(item.path), gif)) out = std::move(gif.frames);
            break;
        }
        case ImportKind::Atlas: {
            if (animation < 0 || static_cast<size_t>(animation) >= item.animations.size()) break;
            Image sheet;
            if (!decodePng(readFileBytes(item.path), sheet)) break;
            std::ifstream in(item.atlas, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            DiagnosticSink sink;
            const bool packer = lowerExtension(item.atlas) == ".txt";
            const Result<SparrowAtlas> atlas = packer ? parsePackerAtlas(text, item.atlas.u8string(), sink)
                                                      : parseSparrowAtlas(text, item.atlas.u8string(), sink);
            if (!atlas) break;
            const std::string& base = item.animations[static_cast<size_t>(animation)].name;
            for (const AtlasFrame& frame : atlas.value().frames) {
                if (withoutFrameNumber(lowerTrim(frame.name)) != base) continue;
                // La caja entera del fotograma, con el recorte en su sitio.
                Image content = cropFrame(sheet, frame);
                if (frame.frameW > 0 && frame.frameH > 0) {
                    Image box = blankImage(frame.frameW, frame.frameH);
                    blit(box, content, -frame.frameX, -frame.frameY);
                    out.push_back(std::move(box));
                } else {
                    out.push_back(std::move(content));
                }
            }
            break;
        }
        default:
            break;
    }
    return out;
}

Image rotateForDirection(const Image& left, int direction) {
    direction &= 3;
    if (direction == 0 || left.empty()) return left;
    // ↓ es la de ← girada 90 grados a la izquierda, ↑ a la derecha y → media vuelta.
    const bool half = direction == 3;
    Image out = half ? blankImage(left.w, left.h) : blankImage(left.h, left.w);
    for (int y = 0; y < left.h; ++y)
        for (int x = 0; x < left.w; ++x) {
            int tx = 0, ty = 0;
            if (direction == 1) { tx = y; ty = left.w - 1 - x; }          // a la izquierda
            else if (direction == 2) { tx = left.h - 1 - y; ty = x; }     // a la derecha
            else { tx = left.w - 1 - x; ty = left.h - 1 - y; }            // media vuelta
            std::copy_n(left.at(x, y), 4, out.at(tx, ty));
        }
    return out;
}

TypeLookSheet buildTypeLookSheet(const TypeLookFrames& frames, const std::string& imageName) {
    TypeLookSheet out;
    static const char* colors[4] = {"purple", "blue", "green", "red"};
    std::vector<NamedFrame> named;
    auto add = [&](const std::vector<Image>& list, const std::string& base) {
        char suffix[8];
        for (size_t i = 0; i < list.size(); ++i) {
            std::snprintf(suffix, sizeof(suffix), "%04d", static_cast<int>(i));
            named.push_back({base + suffix, list[i]});
        }
    };
    for (int d = 0; d < 4; ++d) {
        add(frames.note[static_cast<size_t>(d)], std::string(colors[d]) + "0");
        add(frames.holdPiece[static_cast<size_t>(d)], std::string(colors[d]) + " hold piece");
        // El final morado lleva la errata del juego base, que buscan los dos
        // motores primero (Codename Note.hx:174-177, Psych Note.hx:439-441).
        add(frames.holdEnd[static_cast<size_t>(d)], d == 0 ? std::string("pruple end hold") : std::string(colors[d]) + " hold end");
    }
    out.atlas = packSparrow(named, imageName);
    if (!out.atlas.ok) return out;
    for (int d = 0; d < 4; ++d) {
        auto bind = [&](const std::vector<Image>& list, Part part, const std::string& prefix) {
            if (list.empty()) return;
            PartBinding binding;
            binding.part = part;
            binding.direction = d;
            binding.sheet = 0;
            binding.animation.prefix = prefix;
            binding.animation.fps = 24.0f;
            binding.animation.loop = list.size() > 1;
            out.parts.push_back(binding);
        };
        bind(frames.note[static_cast<size_t>(d)], Part::Note, std::string(colors[d]) + "0");
        bind(frames.holdPiece[static_cast<size_t>(d)], Part::HoldPiece, std::string(colors[d]) + " hold piece");
        bind(frames.holdEnd[static_cast<size_t>(d)], Part::HoldEnd, d == 0 ? std::string("pruple end hold") : std::string(colors[d]) + " hold end");
    }
    out.ok = !out.parts.empty();
    return out;
}

namespace {

// Los colores hacia otro, en `amount` (el alfa se queda).
void mixPixels(Image& image, std::uint32_t argb, float amount) {
    const float to[3] = {channel(argb, 16), channel(argb, 8), channel(argb, 0)};
    for (size_t i = 0; i < static_cast<size_t>(image.w) * static_cast<size_t>(image.h); ++i) {
        std::uint8_t* p = image.rgba.data() + i * 4u;
        if (p[3] == 0) continue;
        for (int c = 0; c < 3; ++c) p[c] = byte(p[c] / 255.0f + (to[c] - p[c] / 255.0f) * amount);
    }
}

// Un brillo alrededor de lo visible: el alfa desenfocado (dos pasadas de caja
// en cada eje, casi una gaussiana) de un color, con la imagen encima.
Image glowBehind(const Image& image, std::uint32_t argb, int radius, float strength) {
    const int w = image.w, h = image.h;
    std::vector<float> a(static_cast<size_t>(w) * h), tmp(a.size());
    for (size_t i = 0; i < a.size(); ++i) a[i] = image.rgba[i * 4u + 3u] / 255.0f;
    const float window = static_cast<float>(radius * 2 + 1);
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < h; ++y) {
            float sum = 0.0f;
            for (int x = -radius; x <= radius; ++x) sum += x >= 0 && x < w ? a[static_cast<size_t>(y) * w + x] : 0.0f;
            for (int x = 0; x < w; ++x) {
                tmp[static_cast<size_t>(y) * w + x] = sum / window;
                const int out = x - radius, in = x + radius + 1;
                if (out >= 0) sum -= a[static_cast<size_t>(y) * w + out];
                if (in < w) sum += a[static_cast<size_t>(y) * w + in];
            }
        }
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int y = -radius; y <= radius; ++y) sum += y >= 0 && y < h ? tmp[static_cast<size_t>(y) * w + x] : 0.0f;
            for (int y = 0; y < h; ++y) {
                a[static_cast<size_t>(y) * w + x] = sum / window;
                const int out = y - radius, in = y + radius + 1;
                if (out >= 0) sum -= tmp[static_cast<size_t>(out) * w + x];
                if (in < h) sum += tmp[static_cast<size_t>(in) * w + x];
            }
        }
    }
    Image result = blankImage(w, h);
    const std::uint8_t color[3] = {byte(channel(argb, 16)), byte(channel(argb, 8)), byte(channel(argb, 0))};
    for (size_t i = 0; i < a.size(); ++i) {
        std::uint8_t* dst = result.rgba.data() + i * 4u;
        const std::uint8_t* src = image.rgba.data() + i * 4u;
        const float glow = std::clamp(a[i] * strength * 1.6f, 0.0f, 1.0f);
        const float top = src[3] / 255.0f;
        const float alpha = top + glow * (1.0f - top);
        if (alpha <= 0.0f) continue;
        for (int c = 0; c < 3; ++c)
            dst[c] = byte((src[c] / 255.0f * top + color[c] / 255.0f * glow * (1.0f - top)) / alpha);
        dst[3] = byte(alpha);
    }
    return result;
}

// Tenir un dibujo: cada pixel a un degradado de ese color por su luz (oscuro,
// el color, casi blanco), como un contorno propio. Asi sale el color de la
// receta sea cual sea el del dibujo; por tono, un color puro (saturacion 1)
// no se movia de su saturacion.
Paint drawnPaint(const ArrowRecipe& recipe, std::uint32_t color) {
    Paint p;
    p.mode = PaintMode::Gradient;
    p.stops = {recipe.outline ? recipe.outline : mixColor(color, 0xFF000000u, 0.78f), color, mixColor(color, 0xFFFFFFFFu, 0.85f)};
    p.strength = recipe.strength;
    return p;
}

}  // namespace

int drawnPieceSize(DrawnPiece piece) {
    switch (piece) {
        case DrawnPiece::Note:
        case DrawnPiece::Strum:
        case DrawnPiece::StrumPress:
        case DrawnPiece::StrumConfirm: return 160;
        case DrawnPiece::HoldPiece:
        case DrawnPiece::HoldEnd: return 64;
        case DrawnPiece::Splash: return 200;
    }
    return 160;
}

std::string basePieceName(Part part, int direction, int variant) {
    static const char* colors[4] = {"purple", "blue", "green", "red"};
    static const char* lower[4] = {"left", "down", "up", "right"};
    static const char* upper[4] = {"LEFT", "DOWN", "UP", "RIGHT"};
    static const char* capital[4] = {"Purple", "Blue", "Green", "Red"};
    const int d = direction & 3;
    switch (part) {
        case Part::Note: return std::string(colors[d]) + "0";
        case Part::HoldPiece: return std::string(colors[d]) + " hold piece";
        // El final morado lleva la errata del juego base, que buscan los dos
        // motores primero (Codename Note.hx:174-177, Psych Note.hx:439-441).
        case Part::HoldEnd: return d == 0 ? std::string("pruple end hold") : std::string(colors[d]) + " hold end";
        case Part::StrumStatic: return std::string("arrow") + upper[d];
        case Part::StrumPress: return std::string(lower[d]) + " press";
        case Part::StrumConfirm: return std::string(lower[d]) + " confirm";
        case Part::StrumConfirmHold: return std::string(lower[d]) + " confirm hold";
        case Part::Splash: return std::string("note splash ") + colors[d] + " " + std::to_string(variant + 1);
        case Part::HoldCoverStart: return std::string("holdCoverStart") + capital[d];
        case Part::HoldCover: return std::string("holdCover") + capital[d];
        case Part::HoldCoverEnd: return std::string("holdCoverEnd") + capital[d];
    }
    return "piece";
}

namespace {

// Los fotogramas de cada pieza ya tenidos, por direccion, cada uno en su caja
// (las cajas de una pieza miden lo mismo: la animacion no baila).
struct DrawnFrames {
    std::array<std::vector<Image>, 4> note, still, press, confirm, holdPiece, holdEnd, splash;
};

const std::vector<Image>& strumSource(const DrawnPieces& pieces, const ArrowRecipe& recipe, std::vector<Image>& scratch) {
    const std::vector<Image>& strum = pieces[static_cast<size_t>(DrawnPiece::Strum)];
    const std::vector<Image>& note = pieces[static_cast<size_t>(DrawnPiece::Note)];
    if (!strum.empty() || !recipe.drawnStrums || note.empty()) return strum;
    scratch = {note.front()};   // sin receptor dibujado: la forma de la nota
    return scratch;
}

DrawnFrames drawnFrames(const DrawnPieces& pieces, const ArrowRecipe& recipe) {
    DrawnFrames out;
    auto painted = [](Image image, const Paint& paint) {
        paintImage(image, paint);
        return image;
    };
    auto boxed = [](const Image& image, int w, int h) {
        Image box = blankImage(w, h);
        blit(box, image, (w - image.w) / 2, (h - image.h) / 2);
        return box;
    };
    const bool tint = recipe.drawnTint;
    std::vector<Image> scratch;
    const std::vector<Image>& note = pieces[static_cast<size_t>(DrawnPiece::Note)];
    const std::vector<Image>& strum = strumSource(pieces, recipe, scratch);
    const std::vector<Image>& piece = pieces[static_cast<size_t>(DrawnPiece::HoldPiece)];
    const std::vector<Image>& end = pieces[static_cast<size_t>(DrawnPiece::HoldEnd)];
    const std::vector<Image>& splash = pieces[static_cast<size_t>(DrawnPiece::Splash)];
    const std::vector<Image>& pressDrawn = pieces[static_cast<size_t>(DrawnPiece::StrumPress)];
    const std::vector<Image>& confirmDrawn = pieces[static_cast<size_t>(DrawnPiece::StrumConfirm)];
    constexpr int cell = kDrawnArrowCell;
    // Los receptores grises: el gris azulado de los del juego base, por la luz
    // de cada pixel (un dibujo de colores puros tambien queda gris).
    Paint gray;
    gray.mode = PaintMode::Gradient;
    gray.stops = {0xFF1C2329u, 0xFF87A3ADu, 0xFFFFFFFFu};
    int holdW = 0, holdH = 0, splashCell = 0;
    for (const Image& image : piece) { holdW = std::max(holdW, image.w); holdH = std::max(holdH, image.h); }
    for (const Image& image : end) { holdW = std::max(holdW, image.w); holdH = std::max(holdH, image.h); }
    for (const Image& image : splash) splashCell = std::max(splashCell, static_cast<int>(std::lround(std::max(image.w, image.h) * 1.28f)));
    for (int d = 0; d < 4; ++d) {
        const size_t i = static_cast<size_t>(d);
        const std::uint32_t color = recipe.colors[i];
        auto turned = [&](const Image& image) { return recipe.drawnRotate ? rotateForDirection(image, d) : image; };
        for (const Image& frame : note) {
            const Image shape = turned(frame);
            out.note[i].push_back(boxed(tint ? painted(shape, drawnPaint(recipe, color)) : shape, cell, cell));
        }
        if (!strum.empty()) {
            const Image shape = turned(strum.front());
            // En reposo: gris, de su color o transparente (gris a medias).
            Image still;
            if (recipe.strums == StrumLook::Colored) {
                still = tint ? painted(shape, drawnPaint(recipe, mixColor(color, 0xFF808080u, 0.35f))) : shape;
                if (!tint) mixPixels(still, 0xFF808080u, 0.35f);
            } else {
                still = painted(shape, gray);
                if (recipe.strums == StrumLook::Clear) {
                    Paint clear;
                    clear.alpha = 0.45f;
                    paintImage(still, clear);
                }
            }
            out.still[i].push_back(boxed(still, cell, cell));
            // Al pulsar, sin dibujar: el de reposo algo mas pequeno y apagado.
            if (pressDrawn.empty()) {
                Image press = tint ? painted(shape, drawnPaint(recipe, mixColor(color, 0xFF808080u, 0.25f))) : shape;
                if (!tint) mixPixels(press, 0xFF808080u, 0.25f);
                out.press[i].push_back(boxed(resizeImage(press, std::max(1, static_cast<int>(std::lround(press.w * 0.92f))),
                                                         std::max(1, static_cast<int>(std::lround(press.h * 0.92f))), false),
                                             cell, cell));
            }
        }
        // Dibujados a mano: el pulsado y el acierto, con sus fotogramas, tenidos.
        for (const Image& frame : pressDrawn) {
            const Image form = turned(frame);
            Image press = tint ? painted(form, drawnPaint(recipe, mixColor(color, 0xFF808080u, 0.25f))) : form;
            out.press[i].push_back(boxed(press, cell, cell));
        }
        const bool white = recipe.confirm == ConfirmLook::White;
        for (const Image& frame : confirmDrawn) {
            const Image form = turned(frame);
            out.confirm[i].push_back(boxed(white ? painted(form, arrowPaint(recipe, Part::StrumConfirm, d)) : tint ? painted(form, drawnPaint(recipe, color)) : form,
                                           cell, cell));
        }
        // Al acertar, sin dibujar: su color (o blanco), mas claro y con un brillo alrededor.
        if (confirmDrawn.empty())
            for (const Image& frame : strum) {
                const Image form = turned(frame);
                Image hit = white ? painted(form, arrowPaint(recipe, Part::StrumConfirm, d)) : tint ? painted(form, drawnPaint(recipe, color)) : form;
                mixPixels(hit, 0xFFFFFFFFu, 0.12f);
                out.confirm[i].push_back(glowBehind(boxed(hit, cell, cell), white || !tint ? 0xFFFFFFFFu : color, 9, 0.9f));
            }
        // El sostenido: sin girar, del color de su direccion (o mas claro). El
        // tramo llena su caja de arriba abajo: el motor lo estira.
        std::uint32_t holdColor = color;
        if (recipe.holds == HoldLook::Lighter) holdColor = mixColor(color, 0xFFFFFFFFu, 0.35f);
        for (const Image& frame : piece) out.holdPiece[i].push_back(boxed(tint ? painted(frame, drawnPaint(recipe, holdColor)) : frame, holdW, holdH));
        for (const Image& frame : end) out.holdEnd[i].push_back(boxed(tint ? painted(frame, drawnPaint(recipe, holdColor)) : frame, holdW, holdH));
        // La salpicadura: sus fotogramas o, si es uno, crece y se desvanece.
        for (const Image& frame : splash) {
            const Image colored = tint && recipe.splashes ? painted(frame, drawnPaint(recipe, color)) : frame;
            if (splash.size() > 1) {
                out.splash[i].push_back(boxed(colored, splashCell, splashCell));
                continue;
            }
            for (int k = 0; k < kDrawnSplashFrames; ++k) {
                const float grow = 0.75f + 0.12f * static_cast<float>(k);
                Image scaled = resizeImage(colored, std::max(1, static_cast<int>(std::lround(colored.w * grow))),
                                           std::max(1, static_cast<int>(std::lround(colored.h * grow))), false);
                Paint fade;
                fade.alpha = k < 2 ? 1.0f : 1.0f - 0.3f * static_cast<float>(k - 1);
                paintImage(scaled, fade);
                out.splash[i].push_back(boxed(scaled, splashCell, splashCell));
            }
        }
    }
    return out;
}

// Una rejilla de 4 columnas (direcciones) con las filas de varias piezas, una
// detras de otra.
Image drawnGrid(std::initializer_list<const std::array<std::vector<Image>, 4>*> rows) {
    int w = 0, h = 0, count = 0;
    for (const auto* part : rows)
        for (const Image& image : (*part)[0]) {
            w = std::max(w, image.w);
            h = std::max(h, image.h);
            ++count;
        }
    if (count == 0) return Image{};
    Image grid = blankImage(w * 4, h * count);
    int row = 0;
    for (const auto* part : rows) {
        for (size_t k = 0; k < (*part)[0].size(); ++k, ++row)
            for (int d = 0; d < 4; ++d)
                if (k < (*part)[static_cast<size_t>(d)].size()) blit(grid, (*part)[static_cast<size_t>(d)][k], d * w, row * h);
    }
    return grid;
}

}  // namespace

DrawnSheets drawnPieceSheets(const DrawnPieces& pieces, const ArrowRecipe& recipe) {
    const DrawnFrames frames = drawnFrames(pieces, recipe);
    DrawnSheets out;
    out.arrows = drawnGrid({&frames.note, &frames.still, &frames.press, &frames.confirm});
    out.holds = drawnGrid({&frames.holdPiece, &frames.holdEnd});
    out.splashes = drawnGrid({&frames.splash});
    return out;
}

namespace {

// Cuantos fotogramas sale de cada pieza, como los hace drawnFrames.
struct DrawnCounts {
    size_t note = 0, still = 0, press = 0, confirm = 0, holdPiece = 0, holdEnd = 0, splash = 0;
};
DrawnCounts drawnCounts(const DrawnPieces& pieces, const ArrowRecipe& recipe) {
    DrawnCounts c;
    std::vector<Image> scratch;
    const size_t strum = strumSource(pieces, recipe, scratch).size();
    const size_t press = pieces[static_cast<size_t>(DrawnPiece::StrumPress)].size();
    const size_t confirm = pieces[static_cast<size_t>(DrawnPiece::StrumConfirm)].size();
    c.note = pieces[static_cast<size_t>(DrawnPiece::Note)].size();
    c.still = strum > 0 ? 1 : 0;
    c.press = press > 0 ? press : c.still;
    c.confirm = confirm > 0 ? confirm : strum;
    c.holdPiece = pieces[static_cast<size_t>(DrawnPiece::HoldPiece)].size();
    c.holdEnd = pieces[static_cast<size_t>(DrawnPiece::HoldEnd)].size();
    const size_t splash = pieces[static_cast<size_t>(DrawnPiece::Splash)].size();
    c.splash = splash == 1 ? static_cast<size_t>(kDrawnSplashFrames) : splash;
    return c;
}

bool replacedBy(const DrawnCounts& c, Part part) {
    switch (part) {
        case Part::Note: return c.note > 0;
        case Part::StrumStatic: return c.still > 0;
        case Part::StrumPress: return c.press > 0;
        case Part::StrumConfirm:
        case Part::StrumConfirmHold: return c.confirm > 0;
        case Part::HoldPiece: return c.holdPiece > 0;
        case Part::HoldEnd: return c.holdEnd > 0;
        case Part::Splash: return c.splash > 0;
        default: return false;
    }
}

// Lo que se dibujo deja de salir de las hojas del estilo; StrumConfirmHold
// solo se ata si el estilo ya lo tenia.
bool dropReplaced(NoteStyle& style, const DrawnCounts& c) {
    const bool confirmHold = std::any_of(style.parts.begin(), style.parts.end(),
                                         [](const PartBinding& b) { return b.part == Part::StrumConfirmHold; });
    style.parts.erase(std::remove_if(style.parts.begin(), style.parts.end(), [&](const PartBinding& b) { return replacedBy(c, b.part); }),
                      style.parts.end());
    return confirmHold;
}

PartBinding drawnBinding(Part part, int d, int sheet, int fps, bool loop) {
    PartBinding binding;
    binding.part = part;
    binding.direction = d;
    binding.sheet = sheet;
    binding.animation.fps = static_cast<float>(std::clamp(fps, 1, 60));
    binding.animation.loop = loop;
    return binding;
}

std::vector<int> cellRange(size_t first, size_t count, int d) {
    std::vector<int> cells;
    for (size_t k = 0; k < count; ++k) cells.push_back(static_cast<int>((first + k) * 4) + d);
    return cells;
}

}  // namespace

void bindDrawnPieces(NoteStyle& style, const DrawnPieces& pieces, const ArrowRecipe& recipe,
                     const std::array<std::string, 3>& images, float scale) {
    DrawnCounts c = drawnCounts(pieces, recipe);
    if (images[0].empty()) c.note = c.still = c.press = c.confirm = 0;
    if (images[1].empty()) c.holdPiece = c.holdEnd = 0;
    if (images[2].empty()) c.splash = 0;
    const bool confirmHold = dropReplaced(style, c);
    auto addSheet = [&](const std::string& image, size_t rows, const char* declared) {
        Sheet sheet;
        sheet.kind = SheetKind::Grid;
        sheet.image = image;
        sheet.declared = declared;
        sheet.columns = 4;
        sheet.rows = static_cast<int>(rows);
        sheet.scale = scale;
        style.sheets.push_back(sheet);
        return static_cast<int>(style.sheets.size()) - 1;
    };
    const auto& fps = recipe.drawnFps;
    const int noteFps = fps[static_cast<size_t>(DrawnPiece::Note)], strumFps = fps[static_cast<size_t>(DrawnPiece::Strum)];
    const int holdFps = fps[static_cast<size_t>(DrawnPiece::HoldPiece)], endFps = fps[static_cast<size_t>(DrawnPiece::HoldEnd)];
    const int splashFps = fps[static_cast<size_t>(DrawnPiece::Splash)];
    const int pressFps = fps[static_cast<size_t>(DrawnPiece::StrumPress)];
    const int confirmFps = pieces[static_cast<size_t>(DrawnPiece::StrumConfirm)].empty() ? strumFps : fps[static_cast<size_t>(DrawnPiece::StrumConfirm)];
    if (c.note + c.still + c.press + c.confirm > 0) {
        const int sheet = addSheet(images[0], c.note + c.still + c.press + c.confirm, "notelab/drawn-arrows");
        for (int d = 0; d < 4; ++d) {
            if (c.note) {
                PartBinding b = drawnBinding(Part::Note, d, sheet, noteFps, true);
                b.animation.indices = cellRange(0, c.note, d);
                style.parts.push_back(b);
            }
            if (c.still) {
                PartBinding still = drawnBinding(Part::StrumStatic, d, sheet, 24, true);
                still.animation.indices = cellRange(c.note, 1, d);
                style.parts.push_back(still);
            }
            if (c.press) {
                PartBinding press = drawnBinding(Part::StrumPress, d, sheet, pressFps, false);
                press.animation.indices = cellRange(c.note + c.still, c.press, d);
                style.parts.push_back(press);
            }
            if (c.confirm) {
                PartBinding hit = drawnBinding(Part::StrumConfirm, d, sheet, confirmFps, false);
                hit.animation.indices = cellRange(c.note + c.still + c.press, c.confirm, d);
                style.parts.push_back(hit);
                if (confirmHold) {
                    hit.part = Part::StrumConfirmHold;
                    hit.animation.loop = true;
                    style.parts.push_back(hit);
                }
            }
        }
    }
    if (c.holdPiece + c.holdEnd > 0) {
        const int sheet = addSheet(images[1], c.holdPiece + c.holdEnd, "notelab/drawn-holds");
        for (int d = 0; d < 4; ++d) {
            if (c.holdPiece) {
                PartBinding b = drawnBinding(Part::HoldPiece, d, sheet, holdFps, true);
                b.animation.indices = cellRange(0, c.holdPiece, d);
                style.parts.push_back(b);
            }
            if (c.holdEnd) {
                PartBinding b = drawnBinding(Part::HoldEnd, d, sheet, endFps, true);
                b.animation.indices = cellRange(c.holdPiece, c.holdEnd, d);
                style.parts.push_back(b);
            }
        }
    }
    if (c.splash > 0) {
        const int sheet = addSheet(images[2], c.splash, "notelab/drawn-splashes");
        for (int d = 0; d < 4; ++d) {
            PartBinding b = drawnBinding(Part::Splash, d, sheet, splashFps, false);
            b.animation.indices = cellRange(0, c.splash, d);
            style.parts.push_back(b);
        }
    }
}

DrawnAtlas drawnPieceAtlas(const DrawnPieces& pieces, const ArrowRecipe& recipe, const std::string& imageName) {
    const int sheet = -1;
    DrawnAtlas out;
    const DrawnFrames frames = drawnFrames(pieces, recipe);
    std::vector<NamedFrame> named;
    const auto& fps = recipe.drawnFps;
    auto add = [&](Part part, const std::array<std::vector<Image>, 4>& list, int rate, bool loop) {
        for (int d = 0; d < 4; ++d) {
            const std::vector<Image>& one = list[static_cast<size_t>(d)];
            if (one.empty()) continue;
            const std::string base = basePieceName(part, d);
            char suffix[8];
            for (size_t k = 0; k < one.size(); ++k) {
                std::snprintf(suffix, sizeof(suffix), "%04d", static_cast<int>(k));
                named.push_back({base + suffix, one[k]});
            }
            PartBinding b = drawnBinding(part, d, sheet, rate, loop);
            b.animation.prefix = base;
            out.parts.push_back(b);
        }
    };
    add(Part::Note, frames.note, fps[static_cast<size_t>(DrawnPiece::Note)], true);
    add(Part::StrumStatic, frames.still, 24, true);
    add(Part::StrumPress, frames.press, fps[static_cast<size_t>(DrawnPiece::StrumPress)], false);
    add(Part::StrumConfirm, frames.confirm,
        fps[static_cast<size_t>(pieces[static_cast<size_t>(DrawnPiece::StrumConfirm)].empty() ? DrawnPiece::Strum : DrawnPiece::StrumConfirm)], false);
    add(Part::HoldPiece, frames.holdPiece, fps[static_cast<size_t>(DrawnPiece::HoldPiece)], true);
    add(Part::HoldEnd, frames.holdEnd, fps[static_cast<size_t>(DrawnPiece::HoldEnd)], true);
    add(Part::Splash, frames.splash, fps[static_cast<size_t>(DrawnPiece::Splash)], false);
    if (named.empty()) return out;
    out.atlas = packSparrow(named, imageName);
    if (!out.atlas.ok) out.parts.clear();
    return out;
}

void bindDrawnAtlas(NoteStyle& style, const DrawnAtlas& drawn, const std::string& image, const std::string& atlas, float scale) {
    if (!drawn.atlas.ok || drawn.parts.empty()) return;
    std::set<Part> parts;
    for (const PartBinding& b : drawn.parts) parts.insert(b.part);
    const bool confirmHold = parts.count(Part::StrumConfirm) &&
                             std::any_of(style.parts.begin(), style.parts.end(), [](const PartBinding& b) { return b.part == Part::StrumConfirmHold; });
    if (parts.count(Part::StrumConfirm)) parts.insert(Part::StrumConfirmHold);
    style.parts.erase(std::remove_if(style.parts.begin(), style.parts.end(), [&](const PartBinding& b) { return parts.count(b.part) > 0; }),
                      style.parts.end());
    Sheet sheet;
    sheet.kind = SheetKind::Sparrow;
    sheet.image = image;
    sheet.atlas = atlas;
    sheet.declared = "notelab/drawn";
    sheet.scale = scale;
    style.sheets.push_back(sheet);
    const int index = static_cast<int>(style.sheets.size()) - 1;
    for (PartBinding b : drawn.parts) {
        b.sheet = index;
        style.parts.push_back(b);
        if (confirmHold && b.part == Part::StrumConfirm) {
            b.part = Part::StrumConfirmHold;
            b.animation.loop = true;
            style.parts.push_back(b);
        }
    }
}

namespace {
constexpr size_t kCustomPixels = 32u * 1024u * 1024u;
bool customFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes, std::string& error) {
    bytes = readFileBytes(path);
    if (bytes.empty()) { error = "Missing, empty or oversized file (64 MiB limit): " + path.u8string(); return false; }
    return true;
}
bool customImage(const std::filesystem::path& path, Image& image, std::string& error) {
    int w = 0, h = 0;
    if (!pngSize(path, w, h) || w <= 0 || h <= 0 || w > 8192 || h > 8192 ||
        static_cast<size_t>(w) * h > kCustomPixels) {
        error = "Invalid image or exceeds 8192 px / 32 megapixels: " + path.u8string(); return false;
    }
    std::vector<unsigned char> bytes;
    if (!customFileBytes(path, bytes, error)) return false;
    if (!decodePng(bytes, image)) { error = "Cannot decode PNG: " + path.u8string(); return false; }
    return true;
}
HudAsset* customHud(NoteStyle& s, int i) {
    if (i >= 0 && i < 4) return &s.judgements[static_cast<size_t>(i)];
    if (i == 4) return &s.combo;
    if (i >= 5 && i < 15) return &s.digits[static_cast<size_t>(i - 5)];
    if (i >= 15 && i < 19) return &s.countdown[static_cast<size_t>(i - 15)];
    return nullptr;
}
bool customOgg(const std::vector<unsigned char>& bytes) {
    const char sig[] = "vorbis";
    return bytes.size() >= 32 && std::equal(bytes.begin(), bytes.begin() + 4, "OggS") &&
        std::search(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 256), sig, sig + 6) !=
            bytes.begin() + std::min<size_t>(bytes.size(), 256);
}
}

bool bindCustomAtlas(CustomResource& r, const std::filesystem::path& path, std::string& error) {
    error.clear();
    const auto extension = lowerExtension(path);
    if ((r.input.kind != ImportKind::Image && r.input.kind != ImportKind::Atlas) || (extension != ".xml" && extension != ".txt")) {
        error = "Select a PNG resource and a Sparrow XML or Packer TXT file."; return false;
    }
    auto animations = atlasAnimations(path);
    if (animations.empty()) { error = "Cannot read atlas metadata (4 MiB max). The current resource was not changed."; return false; }
    r.input.kind = ImportKind::Atlas; r.input.atlas = path; r.input.animations = std::move(animations);
    r.input.label = r.input.path.filename().u8string() + " + " + path.filename().u8string();
    r.cellWidth = r.cellHeight = 0; r.regions.clear();
    return true;
}

CustomInspection inspectCustomResource(const CustomResource& r, int animation) {
    CustomInspection out;
    size_t pixels = 0;
    auto append = [&](Image image, CustomRect box) {
        pixels += static_cast<size_t>(image.w) * image.h;
        if (image.empty() || out.frames.size() >= kCustomFrameLimit || pixels > kCustomPixels) {
            out.error = "Too many frames or pixels (512 frames / 32 megapixels per sequence)."; return false;
        }
        out.frames.push_back(std::move(image)); out.boxes.push_back(box); return true;
    };
    const auto kind = r.input.kind;
    if (kind == ImportKind::Sound) {
        std::vector<unsigned char> bytes;
        customFileBytes(r.input.path, bytes, out.error);
        return out;
    }
    if (kind == ImportKind::Image || kind == ImportKind::Atlas) {
        if (!customImage(r.input.path, out.sheet, out.error)) return out;
        std::vector<CustomRect> boxes = r.regions;
        if (boxes.empty() && (r.cellWidth > 0 || r.cellHeight > 0)) {
            if (r.cellWidth <= 0 || r.cellHeight <= 0 || r.cellWidth > out.sheet.w || r.cellHeight > out.sheet.h) {
                out.error = "Cell size must fit inside the image."; return out;
            }
            const int cols = out.sheet.w / r.cellWidth, rows = out.sheet.h / r.cellHeight;
            if (static_cast<size_t>(cols) * rows > kCustomFrameLimit) { out.error = "Grid exceeds 512 frames. Increase cell size."; return out; }
            for (int y = 0; y < rows; ++y) for (int x = 0; x < cols; ++x)
                boxes.push_back({x * r.cellWidth, y * r.cellHeight, r.cellWidth, r.cellHeight});
        }
        if (!boxes.empty()) {
            if (boxes.size() > kCustomFrameLimit) { out.error = "Too many manually defined frames."; return out; }
            for (const CustomRect& b : boxes) {
                if (b.x < 0 || b.y < 0 || b.w <= 0 || b.h <= 0 || b.w > out.sheet.w || b.h > out.sheet.h ||
                    b.x > out.sheet.w - b.w || b.y > out.sheet.h - b.h) { out.error = "A frame rectangle is outside the image."; break; }
                if (!append(cropRect(out.sheet, b.x, b.y, b.w, b.h), b)) break;
            }
        } else if (kind == ImportKind::Image) {
            append(out.sheet, {0, 0, out.sheet.w, out.sheet.h});
        } else {
            if (animation < 0 || animation >= static_cast<int>(r.input.animations.size())) { out.error = "Select a valid atlas animation."; return out; }
            std::vector<unsigned char> bytes;
            if (!customFileBytes(r.input.atlas, bytes, out.error)) return out;
            if (bytes.size() > 4u * 1024u * 1024u) { out.error = "Atlas metadata exceeds 4 MiB."; return out; }
            const std::string text(bytes.begin(), bytes.end());
            DiagnosticSink sink;
            const auto atlas = lowerExtension(r.input.atlas) == ".txt" ? parsePackerAtlas(text, r.input.atlas.u8string(), sink)
                                                                         : parseSparrowAtlas(text, r.input.atlas.u8string(), sink);
            if (!atlas) { out.error = "Cannot read atlas metadata."; return out; }
            const std::string prefix = lowerTrim(r.input.animations[static_cast<size_t>(animation)].name);
            for (const AtlasFrame& f : atlas.value().frames) {
                if (withoutFrameNumber(lowerTrim(f.name)) != prefix) continue;
                if (f.x < 0 || f.y < 0 || f.w <= 0 || f.h <= 0 || f.w > out.sheet.w || f.h > out.sheet.h ||
                    f.x > out.sheet.w - f.w || f.y > out.sheet.h - f.h || f.frameW > 8192 || f.frameH > 8192 ||
                    static_cast<size_t>(std::max(f.w, f.frameW)) * std::max(f.h, f.frameH) > kCustomPixels) {
                    out.error = "Atlas frame is outside the image or too large."; break;
                }
                Image image = cropFrame(out.sheet, f);
                if (f.frameW > 0 && f.frameH > 0) {
                    Image box = blankImage(f.frameW, f.frameH); blit(box, image, -f.frameX, -f.frameY); image = std::move(box);
                }
                if (!append(std::move(image), {f.x, f.y, f.w, f.h})) break;
            }
        }
    } else if (kind == ImportKind::Frames) {
        if (r.input.files.size() > kCustomFrameLimit) { out.error = "Sequence exceeds 512 frames."; return out; }
        for (const auto& path : r.input.files) {
            Image image;
            if (!customImage(path, image, out.error) || !append(std::move(image), {})) break;
        }
    } else if (kind == ImportKind::Gif) {
        std::vector<unsigned char> bytes;
        if (!customFileBytes(r.input.path, bytes, out.error)) return out;
        GifFrames gif;
        if (!decodeGif(bytes, gif)) { out.error = "Cannot decode GIF."; return out; }
        for (Image& image : gif.frames) if (!append(std::move(image), {})) break;
    } else out.error = "Unsupported resource. Choose PNG, PNG + XML/TXT, image sequences, GIF or audio.";
    if (!out.error.empty()) { out.frames.clear(); out.boxes.clear(); }
    else if (out.frames.empty()) out.error = "No usable frames found.";
    return out;
}

bool parseCustomOrder(const std::string& text, int frames, std::vector<int>& order, std::string& error) {
    order.clear(); error.clear();
    std::istringstream in(text);
    while (in >> std::ws && !in.eof()) {
        int n;
        if (!(in >> n) || n < 1 || n > frames || order.size() >= kCustomFrameLimit) {
            error = "Frame order uses numbers from 1 to " + std::to_string(frames) + " (512 entries max)."; order.clear(); return false;
        }
        order.push_back(n - 1); in >> std::ws;
        if (in.peek() == ',' || in.peek() == ';') in.get();
        else if (!in.eof()) { error = "Separate frame numbers with commas."; order.clear(); return false; }
    }
    return true;
}

CustomBuild buildCustomStyle(const CustomRecipe& recipe, Engine engine, const std::string& name) {
    CustomBuild out;
    out.style.engine = engine; out.style.name = name; out.style.referenced = true;
    out.style.use = recipe.noteType ? StyleUse::NoteType : StyleUse::Default; out.style.useDetail = recipe.type;
    if (recipe.resources.size() > 128 || recipe.assignments.size() > 192) { out.errors.push_back("Resource or assignment limit exceeded (128 / 192)."); return out; }
    if (recipe.assignments.empty()) { out.errors.push_back("Assign at least one resource to a role."); return out; }
    if (recipe.noteType && recipe.type.empty()) { out.errors.push_back("Enter the custom note type name."); return out; }
    std::map<std::pair<int, int>, CustomInspection> inspections;
    std::set<std::string> roles;
    size_t totalBytes = 0;
    for (size_t i = 0; i < recipe.assignments.size(); ++i) {
        const auto& a = recipe.assignments[i];
        auto fail = [&](const std::string& error) { out.errors.push_back("Assignment " + std::to_string(i + 1) + ": " + error); };
        if (a.resource < 0 || a.resource >= static_cast<int>(recipe.resources.size())) { fail("Resource is missing."); continue; }
        if (!std::isfinite(a.scale) || a.scale <= 0.0f || a.scale > 8.0f || !std::isfinite(a.fps) || a.fps < 1.0f || a.fps > 240.0f ||
            !std::isfinite(a.offsetX) || !std::isfinite(a.offsetY) || std::abs(a.offsetX) > 4096 || std::abs(a.offsetY) > 4096) { fail("Invalid FPS, scale or offset."); continue; }
        const auto& r = recipe.resources[static_cast<size_t>(a.resource)];
        const bool sound = a.role == CustomRole::CountdownSound || a.role == CustomRole::SoundEffect;
        std::string role = std::to_string(static_cast<int>(a.role)) + ":";
        if (a.role == CustomRole::Piece) role += std::to_string(static_cast<int>(a.part)) + ":" + std::to_string(a.direction) + ":" + std::to_string(a.variant);
        else role += a.role == CustomRole::SoundEffect ? a.soundName : std::to_string(a.hudIndex);
        if (!roles.insert(role).second) { fail("This role is assigned twice. Replace or remove the previous assignment."); continue; }
        const std::string stem = "custom/" + exportName(name) + "/part-" + std::to_string(i);
        if (sound) {
            if (r.input.kind != ImportKind::Sound) { fail("This role needs an audio file."); continue; }
            std::vector<unsigned char> bytes; std::string error;
            if (!customFileBytes(r.input.path, bytes, error)) { fail(error); continue; }
            if (!customOgg(bytes)) { fail("Engine audio must be OGG Vorbis. WAV/MP3 need conversion, not renaming."); continue; }
            std::string path = stem + ".ogg";
            if (a.role == CustomRole::CountdownSound) {
                if (a.hudIndex < 15 || a.hudIndex >= 19) { fail("Select a countdown step (3, 2, 1, Go)."); continue; }
                auto* hud = customHud(out.style, a.hudIndex); hud->sound = path; hud->soundDeclared = path; out.style.hasHud = true;
            } else {
                if (a.soundName.empty() || exportName(a.soundName) != a.soundName) { fail("Sound name must be a safe file name (letters, numbers, hyphen or underscore)."); continue; }
                const std::string key = "notelab/" + exportName(name) + "/" + a.soundName;
                path = "sounds/" + key + ".ogg"; out.style.sounds.push_back({key, path});
            }
            totalBytes += bytes.size(); out.files.push_back({path, std::move(bytes)});
        } else {
            if (r.input.kind == ImportKind::Sound) { fail("This role needs image frames."); continue; }
            const auto key = std::make_pair(a.resource, a.animation);
            auto it = inspections.find(key);
            if (it == inspections.end()) {
                inspections.clear();
                it = inspections.emplace(key, inspectCustomResource(r, a.animation)).first;
            }
            const auto& viewed = it->second;
            if (!viewed.error.empty()) { fail(viewed.error); continue; }
            std::vector<int> order = a.order;
            if (order.empty()) for (size_t f = 0; f < viewed.frames.size(); ++f) order.push_back(static_cast<int>(f));
            if (order.size() > kCustomFrameLimit || std::any_of(order.begin(), order.end(), [&](int f) { return f < 0 || f >= static_cast<int>(viewed.frames.size()); })) { fail("Frame order is invalid."); continue; }
            if (a.role == CustomRole::HudImage) {
                auto* hud = customHud(out.style, a.hudIndex);
                if (!hud || order.size() != 1) { fail("HUD ranking/countdown graphics need one frame. Select its frame explicitly."); continue; }
                hud->image = stem + ".png"; hud->declared = hud->image; hud->scale = a.scale; hud->pixel = a.pixel; out.style.hasHud = true;
                auto png = encodePng(viewed.frames[static_cast<size_t>(order[0])]); totalBytes += png.size(); out.files.push_back({hud->image, std::move(png)});
            } else {
                if (static_cast<int>(a.part) < 0 || static_cast<int>(a.part) >= kPartCount || a.direction < 0 || a.direction > 3 || a.variant < 0 || a.variant > 15) { fail("Invalid piece, direction or variant."); continue; }
                size_t pixels = 0;
                for (int f : order) pixels += static_cast<size_t>(viewed.frames[static_cast<size_t>(f)].w) * viewed.frames[static_cast<size_t>(f)].h;
                if (pixels > kCustomPixels) { fail("Ordered frames exceed the pixel budget."); continue; }
                std::vector<NamedFrame> frames;
                for (size_t f = 0; f < order.size(); ++f) {
                    char suffix[12]; std::snprintf(suffix, sizeof(suffix), "%04d", static_cast<int>(f));
                    frames.push_back({std::string("frame") + suffix, viewed.frames[static_cast<size_t>(order[f])]});
                }
                const std::string path = stem + ".png";
                auto atlas = packSparrow(frames, std::filesystem::u8path(path).filename().u8string());
                if (!atlas.ok) { fail("Cannot pack these frames into an 8192 px atlas."); continue; }
                Sheet sheet; sheet.image = path; sheet.atlas = stem + ".xml"; sheet.scale = a.scale; sheet.pixel = a.pixel;
                Animation anim; anim.prefix = "frame"; anim.fps = a.fps; anim.loop = a.loop; anim.offsetX = a.offsetX; anim.offsetY = a.offsetY;
                bindPart(out.style, a.part, a.direction, addSheet(out.style, sheet), anim, a.variant);
                totalBytes += atlas.png.size() + atlas.xml.size();
                out.files.push_back({path, std::move(atlas.png)});
                out.files.push_back({sheet.atlas, std::vector<unsigned char>(atlas.xml.begin(), atlas.xml.end())});
            }
        }
        if (totalBytes > 128u * 1024u * 1024u) { fail("Created resources exceed 128 MiB."); break; }
    }
    out.ok = out.errors.empty() && !out.files.empty();
    if (!out.ok) { out.files.clear(); out.style = {}; }
    return out;
}

int guessHudIndex(const std::string& fileStem) {
    std::string n = lowerTrim(fileStem);
    for (const char* suffix : {"-pixel", "_pixel", " pixel", "pixel"})
        if (n.size() > std::string(suffix).size() && n.compare(n.size() - std::string(suffix).size(), std::string::npos, suffix) == 0) {
            n.erase(n.size() - std::string(suffix).size());
            break;
        }
    static const char* judgements[4] = {"sick", "good", "bad", "shit"};
    for (int i = 0; i < 4; ++i)
        if (n == judgements[i]) return i;
    if (n == "combo") return 4;
    if (n.size() == 4 && startsWith(n, "num") && std::isdigit(static_cast<unsigned char>(n[3]))) return 5 + (n[3] - '0');
    // La cuenta atras (Codename Flags.hx:200, Psych PlayState.hx, V-Slice
    // notestyle countdownThree...Go): imagenes ready/set/go y sonidos introN.
    if (n == "three" || n == "intro3" || n == "introthree") return 15;
    if (n == "two" || n == "ready" || n == "intro2" || n == "introtwo") return 16;
    if (n == "one" || n == "set" || n == "intro1" || n == "introone") return 17;
    if (n == "go" || n == "introgo") return 18;
    return -1;
}

}  // namespace fml::notelab

#pragma once

// El nucleo del editor de sprites (pedido del autor, 4 oct 2026: «capas,
// figuras, pinceles, tamanos... como Krita»; despues, «todo lo relacionado a
// la nota, splash, hold», con linea de tiempo, la hoja completa con sus huecos,
// seleccion por rango y presets): figuras con relleno y contorno, pinceladas
// con dureza, goma, bote de pintura, capas con opacidad y la hoja con una fila
// por pieza. Todo pintado en imagenes de verdad con bordes suavizados y, si hay
// seleccion, solo dentro de ella. Sin dependencias de la interfaz: la ventana
// esta en SpriteEditor.hpp.

enum class SpriteShapeKind { Rect, Round, Ellipse, Triangle, Diamond, Star, Arrow, Heart };
constexpr int kSpriteShapeKinds = 8;
constexpr int kSpriteSize = 160;   // el lado de una nota, en pixeles (una nota del juego base ronda los 157)

// Un rango de la imagen en pixeles: [x0, x1) x [y0, y1).
struct SpriteRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    int w() const { return x1 - x0; }
    int h() const { return y1 - y0; }
    bool contains(float x, float y) const { return x >= x0 && y >= y0 && x < x1 && y < y1; }
    bool operator==(const SpriteRect& o) const { return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1; }
};

SpriteRect spriteClip(SpriteRect r, int w, int h) {
    r.x0 = std::clamp(r.x0, 0, w); r.x1 = std::clamp(r.x1, 0, w);
    r.y0 = std::clamp(r.y0, 0, h); r.y1 = std::clamp(r.y1, 0, h);
    return r;
}

struct SpriteShape {
    SpriteShapeKind kind = SpriteShapeKind::Rect;
    float x = 80.0f, y = 80.0f;    // centro, en pixeles del sprite
    float w = 80.0f, h = 80.0f;
    float angle = 0.0f;            // grados
    ImU32 fill = IM_COL32(194, 75, 153, 255);
    ImU32 outline = IM_COL32(30, 14, 40, 255);
    float outlineWidth = 8.0f;
    bool filled = true;
};

// El contorno de una figura como poligono, ya girado y en su sitio.
std::vector<ImVec2> spriteOutline(const SpriteShape& s) {
    std::vector<ImVec2> local;
    const float hw = s.w * 0.5f, hh = s.h * 0.5f;
    constexpr float pi = 3.14159265f;
    switch (s.kind) {
        case SpriteShapeKind::Rect:
            local = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
            break;
        case SpriteShapeKind::Round: {
            const float r = std::min(hw, hh) * 0.45f;
            const ImVec2 centers[4] = {{hw - r, -hh + r}, {hw - r, hh - r}, {-hw + r, hh - r}, {-hw + r, -hh + r}};
            for (int c = 0; c < 4; ++c)
                for (int i = 0; i <= 6; ++i) {
                    const float a = (-90.0f + 90.0f * static_cast<float>(c) + 15.0f * static_cast<float>(i)) * pi / 180.0f;
                    local.push_back({centers[c].x + std::cos(a) * r, centers[c].y + std::sin(a) * r});
                }
            break;
        }
        case SpriteShapeKind::Ellipse:
            for (int i = 0; i < 48; ++i) {
                const float a = 2.0f * pi * static_cast<float>(i) / 48.0f;
                local.push_back({std::cos(a) * hw, std::sin(a) * hh});
            }
            break;
        case SpriteShapeKind::Triangle:
            local = {{0.0f, -hh}, {hw, hh}, {-hw, hh}};
            break;
        case SpriteShapeKind::Diamond:
            local = {{0.0f, -hh}, {hw, 0.0f}, {0.0f, hh}, {-hw, 0.0f}};
            break;
        case SpriteShapeKind::Star:
            for (int i = 0; i < 10; ++i) {
                const float a = (-90.0f + 36.0f * static_cast<float>(i)) * pi / 180.0f;
                const float k = i % 2 == 0 ? 1.0f : 0.45f;
                local.push_back({std::cos(a) * hw * k, std::sin(a) * hh * k});
            }
            break;
        case SpriteShapeKind::Arrow:   // mira a la izquierda, como la nota de la izquierda del juego base
            local = {{-hw, 0.0f}, {-hw * 0.05f, -hh}, {-hw * 0.05f, -hh * 0.36f}, {hw, -hh * 0.36f},
                     {hw, hh * 0.36f}, {-hw * 0.05f, hh * 0.36f}, {-hw * 0.05f, hh}};
            break;
        case SpriteShapeKind::Heart:
            for (int i = 0; i < 48; ++i) {
                const float t = 2.0f * pi * static_cast<float>(i) / 48.0f;
                const float x = 16.0f * std::pow(std::sin(t), 3.0f);
                const float y = -(13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t));
                local.push_back({x / 16.0f * hw, (y - 2.5f) / 14.5f * hh});   // y va de -12 (lobulos) a 17 (punta)
            }
            break;
    }
    const float a = s.angle * pi / 180.0f, c = std::cos(a), sn = std::sin(a);
    std::vector<ImVec2> out;
    out.reserve(local.size());
    for (const ImVec2& p : local) out.push_back({s.x + p.x * c - p.y * sn, s.y + p.x * sn + p.y * c});
    return out;
}

bool spriteInside(const std::vector<ImVec2>& poly, float x, float y) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const ImVec2& a = poly[i];
        const ImVec2& b = poly[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

float spriteEdgeDistance(const std::vector<ImVec2>& poly, float x, float y) {
    float best = FLT_MAX;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const ImVec2 a = poly[j], b = poly[i];
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = dx * dx + dy * dy;
        float t = len > 0.0f ? ((x - a.x) * dx + (y - a.y) * dy) / len : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float px = a.x + dx * t - x, py = a.y + dy * t - y;
        best = std::min(best, px * px + py * py);
    }
    return std::sqrt(best);
}

// Los limites donde se puede pintar: la imagen o, si la hay, la seleccion.
SpriteRect spriteBounds(const Image& img, const SpriteRect* clip) {
    SpriteRect all{0, 0, img.w, img.h};
    return clip && !clip->empty() ? spriteClip(*clip, img.w, img.h) : all;
}

// Pintar una figura sobre una imagen (encima de lo que ya tenga), con el
// contorno centrado en el borde y 3 x 3 muestras por pixel.
void paintSpriteShape(Image& out, const SpriteShape& shape, const SpriteRect* clip = nullptr) {
    const std::vector<ImVec2> poly = spriteOutline(shape);
    if (poly.size() < 3 || out.empty()) return;
    const SpriteRect bounds = spriteBounds(out, clip);
    const float half = shape.outlineWidth > 0.0f ? shape.outlineWidth * 0.5f : 0.0f;
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (const ImVec2& p : poly) {
        minX = std::min(minX, p.x); minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y);
    }
    const int x0 = std::max(bounds.x0, static_cast<int>(std::floor(minX - half - 1.0f)));
    const int y0 = std::max(bounds.y0, static_cast<int>(std::floor(minY - half - 1.0f)));
    const int x1 = std::min(bounds.x1 - 1, static_cast<int>(std::ceil(maxX + half + 1.0f)));
    const int y1 = std::min(bounds.y1 - 1, static_cast<int>(std::ceil(maxY + half + 1.0f)));
    const float fill[4] = {static_cast<float>(shape.fill & 0xFF), static_cast<float>((shape.fill >> 8) & 0xFF),
                           static_cast<float>((shape.fill >> 16) & 0xFF), static_cast<float>((shape.fill >> 24) & 0xFF) / 255.0f};
    const float line[4] = {static_cast<float>(shape.outline & 0xFF), static_cast<float>((shape.outline >> 8) & 0xFF),
                           static_cast<float>((shape.outline >> 16) & 0xFF), static_cast<float>((shape.outline >> 24) & 0xFF) / 255.0f};
    for (int py = y0; py <= y1; ++py)
        for (int px = x0; px <= x1; ++px) {
            // Lo que cubre cada muestra: el contorno manda sobre el relleno.
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (int sy = 0; sy < 3; ++sy)
                for (int sx = 0; sx < 3; ++sx) {
                    const float x = static_cast<float>(px) + (static_cast<float>(sx) + 0.5f) / 3.0f;
                    const float y = static_cast<float>(py) + (static_cast<float>(sy) + 0.5f) / 3.0f;
                    const float* color = nullptr;
                    if (half > 0.0f && spriteEdgeDistance(poly, x, y) <= half) color = line;
                    else if (shape.filled && spriteInside(poly, x, y)) color = fill;
                    if (!color) continue;
                    r += color[0] * color[3];
                    g += color[1] * color[3];
                    b += color[2] * color[3];
                    a += color[3];
                }
            if (a <= 0.0f) continue;
            const float cover = a / 9.0f;
            const float sr = r / a, sg = g / a, sb = b / a;
            std::uint8_t* dst = out.at(px, py);
            const float da = static_cast<float>(dst[3]) / 255.0f;
            const float oa = cover + da * (1.0f - cover);
            if (oa <= 0.0f) continue;
            dst[0] = static_cast<std::uint8_t>(std::lround((sr * cover + dst[0] * da * (1.0f - cover)) / oa));
            dst[1] = static_cast<std::uint8_t>(std::lround((sg * cover + dst[1] * da * (1.0f - cover)) / oa));
            dst[2] = static_cast<std::uint8_t>(std::lround((sb * cover + dst[2] * da * (1.0f - cover)) / oa));
            dst[3] = static_cast<std::uint8_t>(std::lround(oa * 255.0f));
        }
}

Image rasterSprite(const std::vector<SpriteShape>& shapes, int size = kSpriteSize) {
    Image out = blankImage(size, size);
    for (const SpriteShape& shape : shapes) paintSpriteShape(out, shape);
    return out;
}

// Una pincelada redonda: dura en el centro y suave hasta el borde segun su
// dureza; con la goma, quita en vez de poner.
// Pixel art: un sello de `size` pixeles enteros, sin suavizar el borde (con 1,
// el pixel justo bajo el raton).
void stampPixelBrush(Image& img, float cx, float cy, int size, ImU32 color, float opacity, bool erase, const SpriteRect* clip = nullptr) {
    if (img.empty() || size <= 0) return;
    const SpriteRect bounds = spriteBounds(img, clip);
    const int left = static_cast<int>(std::floor(cx)) - (size - 1) / 2, top = static_cast<int>(std::floor(cy)) - (size - 1) / 2;
    const float r = size * 0.5f;
    const std::uint8_t a = static_cast<std::uint8_t>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * ((color >> 24) & 0xFF)));
    for (int py = top; py < top + size; ++py)
        for (int px = left; px < left + size; ++px) {
            if (px < bounds.x0 || py < bounds.y0 || px >= bounds.x1 || py >= bounds.y1) continue;
            const float dx = px + 0.5f - (left + r), dy = py + 0.5f - (top + r);
            if (size >= 4 && dx * dx + dy * dy > r * r) continue;
            std::uint8_t* dst = img.at(px, py);
            if (erase) {
                dst[3] = static_cast<std::uint8_t>(std::lround(dst[3] * (1.0f - std::clamp(opacity, 0.0f, 1.0f))));
                continue;
            }
            dst[0] = static_cast<std::uint8_t>(color & 0xFF);
            dst[1] = static_cast<std::uint8_t>((color >> 8) & 0xFF);
            dst[2] = static_cast<std::uint8_t>((color >> 16) & 0xFF);
            dst[3] = std::max(dst[3], a);
        }
}

void stampSpriteBrush(Image& img, float cx, float cy, float radius, float hardness, ImU32 color, float opacity, bool erase,
                      const SpriteRect* clip = nullptr) {
    if (img.empty() || radius <= 0.0f) return;
    const SpriteRect bounds = spriteBounds(img, clip);
    const int x0 = std::max(bounds.x0, static_cast<int>(std::floor(cx - radius - 1.0f)));
    const int y0 = std::max(bounds.y0, static_cast<int>(std::floor(cy - radius - 1.0f)));
    const int x1 = std::min(bounds.x1 - 1, static_cast<int>(std::ceil(cx + radius + 1.0f)));
    const int y1 = std::min(bounds.y1 - 1, static_cast<int>(std::ceil(cy + radius + 1.0f)));
    const float inner = radius * std::clamp(hardness, 0.0f, 1.0f);
    const float sr = static_cast<float>(color & 0xFF), sg = static_cast<float>((color >> 8) & 0xFF), sb = static_cast<float>((color >> 16) & 0xFF);
    const float colorAlpha = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    for (int py = y0; py <= y1; ++py)
        for (int px = x0; px <= x1; ++px) {
            const float dx = static_cast<float>(px) + 0.5f - cx, dy = static_cast<float>(py) + 0.5f - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > radius + 0.5f) continue;
            float cover = d <= inner ? 1.0f : std::clamp(1.0f - (d - inner) / std::max(0.75f, radius - inner), 0.0f, 1.0f);
            cover = std::min(cover, std::clamp(radius + 0.5f - d, 0.0f, 1.0f));   // el borde, suavizado
            cover *= opacity;
            if (cover <= 0.0f) continue;
            std::uint8_t* dst = img.at(px, py);
            if (erase) {
                dst[3] = static_cast<std::uint8_t>(std::lround(static_cast<float>(dst[3]) * (1.0f - cover)));
                continue;
            }
            const float a = cover * colorAlpha;
            const float da = static_cast<float>(dst[3]) / 255.0f;
            const float oa = a + da * (1.0f - a);
            if (oa <= 0.0f) continue;
            dst[0] = static_cast<std::uint8_t>(std::lround((sr * a + dst[0] * da * (1.0f - a)) / oa));
            dst[1] = static_cast<std::uint8_t>(std::lround((sg * a + dst[1] * da * (1.0f - a)) / oa));
            dst[2] = static_cast<std::uint8_t>(std::lround((sb * a + dst[2] * da * (1.0f - a)) / oa));
            dst[3] = static_cast<std::uint8_t>(std::lround(oa * 255.0f));
        }
}

// El bote de pintura: rellena la zona contigua del mismo color (con
// tolerancia), sin salir de `bounds` (el hueco o la seleccion).
void floodSprite(Image& img, int sx, int sy, ImU32 color, int tolerance, const SpriteRect* bounds = nullptr) {
    const SpriteRect area = spriteBounds(img, bounds);
    if (img.empty() || !area.contains(static_cast<float>(sx), static_cast<float>(sy))) return;
    const std::uint8_t* seed = img.at(sx, sy);
    const int target[4] = {seed[0], seed[1], seed[2], seed[3]};
    const std::uint8_t paint[4] = {static_cast<std::uint8_t>(color & 0xFF), static_cast<std::uint8_t>((color >> 8) & 0xFF),
                                   static_cast<std::uint8_t>((color >> 16) & 0xFF), static_cast<std::uint8_t>((color >> 24) & 0xFF)};
    if (target[0] == paint[0] && target[1] == paint[1] && target[2] == paint[2] && target[3] == paint[3]) return;
    auto close = [&](const std::uint8_t* p) {
        return std::abs(p[0] - target[0]) <= tolerance && std::abs(p[1] - target[1]) <= tolerance &&
               std::abs(p[2] - target[2]) <= tolerance && std::abs(p[3] - target[3]) <= tolerance;
    };
    std::vector<std::uint8_t> seen(static_cast<size_t>(img.w) * static_cast<size_t>(img.h), 0);
    std::vector<std::pair<int, int>> stack{{sx, sy}};
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        if (!area.contains(static_cast<float>(x), static_cast<float>(y))) continue;
        const size_t index = static_cast<size_t>(y) * static_cast<size_t>(img.w) + static_cast<size_t>(x);
        if (seen[index]) continue;
        seen[index] = 1;
        std::uint8_t* p = img.at(x, y);
        if (!close(p)) continue;
        std::copy(paint, paint + 4, p);
        stack.push_back({x + 1, y});
        stack.push_back({x - 1, y});
        stack.push_back({x, y + 1});
        stack.push_back({x, y - 1});
    }
}

// Vaciar un rango (la tecla Supr con una seleccion).
void clearSprite(Image& img, const SpriteRect& r) {
    const SpriteRect area = spriteClip(r, img.w, img.h);
    for (int y = area.y0; y < area.y1; ++y)
        std::fill_n(img.at(area.x0, y), static_cast<size_t>(area.w()) * 4u, std::uint8_t{0});
}

// Una imagen encima de otra en (x, y), mezclando por su alfa.
void overSprite(Image& dst, const Image& src, int x, int y, float opacity = 1.0f) {
    for (int row = 0; row < src.h; ++row) {
        const int ty = y + row;
        if (ty < 0 || ty >= dst.h) continue;
        for (int col = 0; col < src.w; ++col) {
            const int tx = x + col;
            if (tx < 0 || tx >= dst.w) continue;
            const std::uint8_t* s = src.at(col, row);
            const float a = static_cast<float>(s[3]) / 255.0f * opacity;
            if (a <= 0.0f) continue;
            std::uint8_t* d = dst.at(tx, ty);
            const float da = static_cast<float>(d[3]) / 255.0f;
            const float oa = a + da * (1.0f - a);
            for (int c = 0; c < 3; ++c) d[c] = static_cast<std::uint8_t>(std::lround((s[c] * a + d[c] * da * (1.0f - a)) / oa));
            d[3] = static_cast<std::uint8_t>(std::lround(oa * 255.0f));
        }
    }
}

bool spriteHasInk(const Image& img, const SpriteRect& r) {
    const SpriteRect area = spriteClip(r, img.w, img.h);
    for (int y = area.y0; y < area.y1; ++y)
        for (int x = area.x0; x < area.x1; ++x)
            if (img.at(x, y)[3] > 0) return true;
    return false;
}

// Una capa del editor: su imagen, si se ve y con que opacidad.
struct SpriteLayer {
    std::string name;
    Image image;
    bool visible = true;
    float opacity = 1.0f;
};

// Las capas, una encima de otra (de abajo arriba), como se veran. Todas miden
// lo que la primera.
Image composeSpriteLayers(const std::vector<SpriteLayer>& layers) {
    if (layers.empty()) return blankImage(kSpriteSize, kSpriteSize);
    Image out = blankImage(layers.front().image.w, layers.front().image.h);
    for (const SpriteLayer& layer : layers) {
        if (!layer.visible || layer.image.w != out.w || layer.image.h != out.h) continue;
        overSprite(out, layer.image, 0, 0, layer.opacity);
    }
    return out;
}

// Rehacer solo un rango de lo compuesto (una pincelada en una hoja grande no
// recompone la hoja entera).
void composeSpriteRegion(Image& out, const std::vector<SpriteLayer>& layers, const SpriteRect& r) {
    const SpriteRect area = spriteClip(r, out.w, out.h);
    if (area.empty()) return;
    clearSprite(out, area);
    for (const SpriteLayer& layer : layers) {
        if (!layer.visible || layer.image.w != out.w || layer.image.h != out.h || layer.opacity <= 0.0f) continue;
        for (int y = area.y0; y < area.y1; ++y)
            for (int x = area.x0; x < area.x1; ++x) {
                const std::uint8_t* s = layer.image.at(x, y);
                const float a = static_cast<float>(s[3]) / 255.0f * layer.opacity;
                if (a <= 0.0f) continue;
                std::uint8_t* d = out.at(x, y);
                const float da = static_cast<float>(d[3]) / 255.0f;
                const float oa = a + da * (1.0f - a);
                for (int c = 0; c < 3; ++c) d[c] = static_cast<std::uint8_t>(std::lround((s[c] * a + d[c] * da * (1.0f - a)) / oa));
                d[3] = static_cast<std::uint8_t>(std::lround(oa * 255.0f));
            }
    }
}

SpriteRect spriteUnion(const SpriteRect& a, const SpriteRect& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

// Deshacer: un trazo guarda solo la capa que toco; lo que cambia las capas
// (nueva, borrar, presets...), todas.
struct SpriteUndo {
    int layer = -1;                    // >= 0: solo esa capa
    Image image;
    std::vector<SpriteLayer> layers;   // layer < 0: todas
    int selected = 0;
};

// ------------------------------------------------------------- la hoja --

// La hoja: una fila por pieza (con su rotulo encima) y kSheetSlots huecos para
// sus fotogramas, cada uno del lado de la pieza (drawnPieceSize).
constexpr int kSheetSlots = 6, kSheetGap = 12, kSheetLabel = 24;
struct SheetRow {
    DrawnPiece piece = DrawnPiece::Note;
    int y = 0, cell = 0;
    int width = 0;   // ancho del hueco si no es cuadrado (un lienzo libre); 0 = `cell`
};
struct SheetLayout {
    int w = 0, h = 0;
    int slots = kSheetSlots, gap = kSheetGap;
    std::vector<SheetRow> rows;
    SpriteRect cell(int row, int slot) const {
        if (row < 0 || row >= static_cast<int>(rows.size()) || slot < 0 || slot >= slots) return {};
        const SheetRow& r = rows[static_cast<size_t>(row)];
        const int cw = r.width > 0 ? r.width : r.cell;
        const int x = gap + slot * (cw + gap);
        return {x, r.y, x + cw, r.y + r.cell};
    }
    int rowOf(DrawnPiece piece) const {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].piece == piece) return static_cast<int>(i);
        return -1;
    }
    bool hit(float x, float y, int& row, int& slot) const {
        for (int r = 0; r < static_cast<int>(rows.size()); ++r)
            for (int s = 0; s < slots; ++s)
                if (cell(r, s).contains(x, y)) {
                    row = r;
                    slot = s;
                    return true;
                }
        return false;
    }
};

// Crear HUD: las cinco piezas; el aspecto de un tipo: nota y sostenido.
SheetLayout sheetLayout(bool hud) {
    SheetLayout layout;
    const std::vector<DrawnPiece> pieces = hud ? std::vector<DrawnPiece>{DrawnPiece::Note, DrawnPiece::Strum, DrawnPiece::StrumPress,
                                                                          DrawnPiece::StrumConfirm, DrawnPiece::HoldPiece, DrawnPiece::HoldEnd,
                                                                          DrawnPiece::Splash}
                                               : std::vector<DrawnPiece>{DrawnPiece::Note, DrawnPiece::HoldPiece, DrawnPiece::HoldEnd};
    int y = 0;
    for (DrawnPiece piece : pieces) {
        SheetRow row;
        row.piece = piece;
        row.cell = drawnPieceSize(piece);
        y += kSheetLabel;
        row.y = y;
        y += row.cell + kSheetGap;
        layout.w = std::max(layout.w, kSheetGap + kSheetSlots * (row.cell + kSheetGap));
        layout.rows.push_back(row);
    }
    layout.h = y;
    return layout;
}

// Un dibujo suelto: un solo hueco del lado de su pieza, sin rotulo ni margen.
SheetLayout singleLayout(DrawnPiece piece) {
    SheetLayout layout;
    SheetRow row;
    row.piece = piece;
    row.cell = drawnPieceSize(piece);
    layout.rows.push_back(row);
    layout.slots = 1;
    layout.gap = 0;
    layout.w = layout.h = row.cell;
    return layout;
}

// Un lienzo libre: un solo hueco de `w` x `h` (de 8 a 512 px), sin rotulo ni
// margen. `piece` es lo que sera al pasarlo a la hoja general.
constexpr int kFreeCanvasMin = 8, kFreeCanvasMax = 512;
SheetLayout freeLayout(int w, int h, DrawnPiece piece) {
    SheetLayout layout;
    SheetRow row;
    row.piece = piece;
    row.cell = std::clamp(h, kFreeCanvasMin, kFreeCanvasMax);
    row.width = std::clamp(w, kFreeCanvasMin, kFreeCanvasMax);
    layout.rows.push_back(row);
    layout.slots = 1;
    layout.gap = 0;
    layout.w = row.width;
    layout.h = row.cell;
    return layout;
}

// Una pestana del editor que no esta delante: la hoja general o un dibujo
// suelto de una pieza, con sus capas, su deshacer y su vista.
struct SpriteDoc {
    std::string name;
    bool single = false;               // un dibujo suelto de una pieza
    bool hud = false;                  // hoja de un HUD (cinco filas) o de un tipo (nota y sostenido)
    bool reference = false;            // otra hoja abierta para copiar de ella (no se usa al aplicar)
    DrawnPiece piece = DrawnPiece::Note;
    int fromRow = -1, fromSlot = -1;   // el hueco de la general del que salio
    int freeW = 0, freeH = 0;          // un lienzo libre (0: del tamano de su pieza)
    bool pixel = false;                // pixel art: se pasa a la hoja sin suavizar
    std::vector<SpriteLayer> layers;
    int layer = 0;
    std::vector<SpriteUndo> undo, redo;
    SpriteRect selection;
    float zoom = 0.0f;
    ImVec2 pan{};
    int row = 0, slot = 0;
};

// Una imagen dentro de un hueco: centrada y, si no cabe, reducida. El tramo
// del sostenido se estira a todo el alto (el motor lo repite).
Image fitInCell(const Image& image, int cell, bool stretchHeight) {
    if (image.empty()) return image;
    float k = std::min(1.0f, std::min(static_cast<float>(cell) / image.w, static_cast<float>(cell) / image.h));
    int w = std::max(1, static_cast<int>(std::lround(image.w * k)));
    int h = std::max(1, static_cast<int>(std::lround(image.h * k)));
    if (stretchHeight) {
        h = cell;
        w = std::min(cell, std::max(1, static_cast<int>(std::lround(image.w * std::min(1.0f, static_cast<float>(cell) / image.w)))));
    }
    return w == image.w && h == image.h ? image : resizeImage(image, w, h, false);
}

// Lo dibujado en un lienzo libre, al tamano de un hueco de `cw` x `ch`: crece o
// encoge para llenarlo sin deformarse. En pixel art crece por un entero (cada
// pixel, un bloque nitido) y nunca se suaviza. El tramo del sostenido se
// estira a todo el alto, como en el motor.
Image fitIntoCell(const Image& image, int cw, int ch, bool pixel, bool stretchHeight) {
    if (image.empty() || cw <= 0 || ch <= 0) return image;
    float k = std::min(static_cast<float>(cw) / image.w, static_cast<float>(ch) / image.h);
    if (pixel && k >= 1.0f) k = std::floor(k);
    int w = std::clamp(static_cast<int>(std::lround(image.w * k)), 1, cw);
    int h = std::clamp(static_cast<int>(std::lround(image.h * k)), 1, ch);
    if (stretchHeight) h = ch;
    return w == image.w && h == image.h ? image : resizeImage(image, w, h, pixel);
}

// ------------------------------------------------------------ presets --

// Formas listas para poner en un hueco o en una seleccion: una pieza o el HUD
// entero. Los colores son los del juego base (el morado de ←): al usarlas en
// un HUD se tinen con los de cada direccion.
enum class SpritePreset { Arrow, Strum, Mine, Heart, Star, HoldPiece, HoldEnd, Splash, FullHud };
constexpr int kSpritePresets = 9;

DrawnPiece presetPiece(SpritePreset preset) {
    switch (preset) {
        case SpritePreset::Strum: return DrawnPiece::Strum;
        case SpritePreset::HoldPiece: return DrawnPiece::HoldPiece;
        case SpritePreset::HoldEnd: return DrawnPiece::HoldEnd;
        case SpritePreset::Splash: return DrawnPiece::Splash;
        default: return DrawnPiece::Note;
    }
}

std::vector<SpriteShape> spritePreset(SpritePreset preset, const SpriteRect& r) {
    std::vector<SpriteShape> shapes;
    const float w = static_cast<float>(r.w()), h = static_cast<float>(r.h());
    const float cx = r.x0 + w * 0.5f, cy = r.y0 + h * 0.5f, k = std::min(w, h) / 160.0f;
    const ImU32 purple = IM_COL32(194, 75, 153, 255), dark = IM_COL32(28, 12, 36, 255);
    auto shape = [&](SpriteShapeKind kind, float x, float y, float sw, float sh, ImU32 fill, ImU32 outline, float line) {
        SpriteShape s;
        s.kind = kind;
        s.x = x; s.y = y; s.w = sw; s.h = sh;
        s.fill = fill;
        s.outline = outline;
        s.outlineWidth = line;
        shapes.push_back(s);
        return &shapes.back();
    };
    switch (preset) {
        case SpritePreset::Arrow:
            shape(SpriteShapeKind::Arrow, cx, cy, w * 0.85f, h * 0.8f, purple, dark, 10.0f * k);
            break;
        case SpritePreset::Strum:
            shape(SpriteShapeKind::Arrow, cx, cy, w * 0.85f, h * 0.8f, IM_COL32(135, 163, 173, 255), IM_COL32(30, 36, 44, 255), 10.0f * k);
            break;
        case SpritePreset::Mine:
            shape(SpriteShapeKind::Ellipse, cx, cy, w * 0.74f, h * 0.74f, IM_COL32(48, 48, 56, 255), IM_COL32(12, 12, 16, 255), 8.0f * k);
            for (float angle : {45.0f, -45.0f}) {
                SpriteShape* bar = shape(SpriteShapeKind::Round, cx, cy, w * 0.575f, h * 0.11f, IM_COL32(230, 52, 64, 255), 0, 0.0f);
                bar->angle = angle;
            }
            break;
        case SpritePreset::Heart:
            shape(SpriteShapeKind::Heart, cx, cy, w * 0.8f, h * 0.74f, IM_COL32(255, 92, 140, 255), IM_COL32(255, 255, 255, 255), 8.0f * k);
            break;
        case SpritePreset::Star:
            shape(SpriteShapeKind::Star, cx, cy, w * 0.86f, h * 0.86f, IM_COL32(255, 210, 60, 255), IM_COL32(60, 40, 10, 255), 8.0f * k);
            break;
        case SpritePreset::HoldPiece: {
            // Un tramo que se repite: sin bordes arriba ni abajo.
            const float bar = w * 0.56f;
            shape(SpriteShapeKind::Rect, cx, cy, bar, h, purple, 0, 0.0f);
            shape(SpriteShapeKind::Rect, cx - bar * 0.5f + w * 0.04f, cy, w * 0.08f, h, IM_COL32(120, 40, 95, 255), 0, 0.0f);
            shape(SpriteShapeKind::Rect, cx + bar * 0.5f - w * 0.04f, cy, w * 0.08f, h, IM_COL32(120, 40, 95, 255), 0, 0.0f);
            break;
        }
        case SpritePreset::HoldEnd: {
            // El final: recto arriba (sigue al tramo) y redondo abajo.
            const float bar = w * 0.56f;
            shape(SpriteShapeKind::Round, cx, r.y0 + h * 0.42f, bar, h * 0.84f, purple, 0, 0.0f);
            shape(SpriteShapeKind::Rect, cx, r.y0 + h * 0.25f, bar, h * 0.5f, purple, 0, 0.0f);
            break;
        }
        case SpritePreset::Splash: {
            shape(SpriteShapeKind::Star, cx, cy, w * 0.52f, h * 0.52f, purple, IM_COL32(255, 255, 255, 255), 6.0f * k);
            constexpr float pi = 3.14159265f;
            for (int i = 0; i < 6; ++i) {
                const float a = (static_cast<float>(i) * 60.0f + 30.0f) * pi / 180.0f;
                shape(SpriteShapeKind::Ellipse, cx + std::cos(a) * w * 0.36f, cy + std::sin(a) * h * 0.36f, w * 0.12f, h * 0.12f, purple,
                      IM_COL32(255, 255, 255, 255), 3.0f * k);
            }
            break;
        }
        case SpritePreset::FullHud:
            break;
    }
    for (SpriteShape& s : shapes) s.filled = true;
    return shapes;
}

// Para empezar: una flecha como las del juego base, una mina, un corazon o nada.
std::vector<SpriteShape> spriteTemplate(int which) {
    const SpriteRect box{0, 0, kSpriteSize, kSpriteSize};
    if (which == 0) return spritePreset(SpritePreset::Arrow, box);
    if (which == 1) return spritePreset(SpritePreset::Mine, box);
    if (which == 2) return spritePreset(SpritePreset::Heart, box);
    return {};
}

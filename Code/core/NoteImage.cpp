#include "NoteImage.hpp"

#include "../third_party/stb_image.h"
#include "../third_party/stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fml::notelab {

Image blankImage(int w, int h) {
    Image image;
    image.w = std::max(0, w);
    image.h = std::max(0, h);
    image.rgba.assign(static_cast<size_t>(image.w) * static_cast<size_t>(image.h) * 4u, 0);
    return image;
}

bool decodePng(const std::vector<unsigned char>& bytes, Image& out) {
    if (bytes.empty() || bytes.size() > 128u * 1024u * 1024u) return false;
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels) ||
        w <= 0 || h <= 0 || w > 16384 || h > 16384 || static_cast<uint64_t>(w) * h > 64u * 1024u * 1024u) return false;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) return false;
    out.w = w;
    out.h = h;
    out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    stbi_image_free(pixels);
    return true;
}

std::vector<unsigned char> encodePng(const Image& image) {
    std::vector<unsigned char> out;
    if (image.empty()) return out;
    stbi_write_png_to_func([](void* context, void* data, int size) {
        auto* target = static_cast<std::vector<unsigned char>*>(context);
        const auto* bytes = static_cast<const unsigned char*>(data);
        target->insert(target->end(), bytes, bytes + size);
    }, &out, image.w, image.h, 4, image.rgba.data(), image.w * 4);
    return out;
}

Image cropRect(const Image& sheet, int x, int y, int w, int h) {
    Image out = blankImage(w, h);
    for (int row = 0; row < h; ++row) {
        const int sy = y + row;
        if (sy < 0 || sy >= sheet.h) continue;
        for (int col = 0; col < w; ++col) {
            const int sx = x + col;
            if (sx < 0 || sx >= sheet.w) continue;
            std::copy_n(sheet.at(sx, sy), 4, out.at(col, row));
        }
    }
    return out;
}

Image cropFrame(const Image& sheet, const AtlasFrame& frame) {
    if (!frame.rotated) return cropRect(sheet, frame.x, frame.y, frame.w, frame.h);
    // En la textura ocupa w x h girado a la derecha; derecho mide h x w. El
    // pixel (ox, oy) derecho es el (w - 1 - oy, ox) de la region.
    Image out = blankImage(frame.h, frame.w);
    for (int oy = 0; oy < out.h; ++oy)
        for (int ox = 0; ox < out.w; ++ox) {
            const int sx = frame.x + frame.w - 1 - oy;
            const int sy = frame.y + ox;
            if (sx < 0 || sy < 0 || sx >= sheet.w || sy >= sheet.h) continue;
            std::copy_n(sheet.at(sx, sy), 4, out.at(ox, oy));
        }
    return out;
}

namespace {

// Premultiplicado en coma flotante: cada pixel pesa por su alfa.
void premultiplied(const Image& image, std::vector<float>& out) {
    out.resize(static_cast<size_t>(image.w) * static_cast<size_t>(image.h) * 4u);
    for (size_t i = 0; i < static_cast<size_t>(image.w) * static_cast<size_t>(image.h); ++i) {
        const float a = image.rgba[i * 4 + 3] / 255.0f;
        out[i * 4 + 0] = image.rgba[i * 4 + 0] / 255.0f * a;
        out[i * 4 + 1] = image.rgba[i * 4 + 1] / 255.0f * a;
        out[i * 4 + 2] = image.rgba[i * 4 + 2] / 255.0f * a;
        out[i * 4 + 3] = a;
    }
}

void storePixel(Image& image, int x, int y, const float value[4]) {
    std::uint8_t* p = image.at(x, y);
    const float a = std::clamp(value[3], 0.0f, 1.0f);
    for (int c = 0; c < 3; ++c) {
        const float straight = a > 0.0f ? value[c] / a : 0.0f;
        p[c] = static_cast<std::uint8_t>(std::lround(std::clamp(straight, 0.0f, 1.0f) * 255.0f));
    }
    p[3] = static_cast<std::uint8_t>(std::lround(a * 255.0f));
}

}  // namespace

Image resizeImage(const Image& image, int w, int h, bool pixel) {
    w = std::max(1, w);
    h = std::max(1, h);
    if (image.empty()) return blankImage(w, h);
    if (w == image.w && h == image.h) return image;
    Image out = blankImage(w, h);
    const double sx = static_cast<double>(image.w) / w;
    const double sy = static_cast<double>(image.h) / h;
    if (pixel) {
        for (int y = 0; y < h; ++y) {
            const int from = std::min(image.h - 1, static_cast<int>((y + 0.5) * sy));
            for (int x = 0; x < w; ++x)
                std::copy_n(image.at(std::min(image.w - 1, static_cast<int>((x + 0.5) * sx)), from), 4, out.at(x, y));
        }
        return out;
    }
    std::vector<float> src;
    premultiplied(image, src);
    auto sample = [&](int x, int y, int c) {
        x = std::clamp(x, 0, image.w - 1);
        y = std::clamp(y, 0, image.h - 1);
        return src[(static_cast<size_t>(y) * static_cast<size_t>(image.w) + static_cast<size_t>(x)) * 4u + static_cast<size_t>(c)];
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float value[4] = {0, 0, 0, 0};
            if (sx >= 1.0 && sy >= 1.0) {
                // Reducir: media de los pixeles de origen que cubre este, con
                // los bordes ponderados por la parte que cae dentro.
                const double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
                double total = 0.0;
                double sum[4] = {0, 0, 0, 0};
                for (int py = static_cast<int>(std::floor(y0)); py < static_cast<int>(std::ceil(y1)); ++py) {
                    const double wy = std::min<double>(py + 1, y1) - std::max<double>(py, y0);
                    if (wy <= 0.0) continue;
                    for (int px = static_cast<int>(std::floor(x0)); px < static_cast<int>(std::ceil(x1)); ++px) {
                        const double wx = std::min<double>(px + 1, x1) - std::max<double>(px, x0);
                        if (wx <= 0.0) continue;
                        for (int c = 0; c < 4; ++c) sum[c] += sample(px, py, c) * wx * wy;
                        total += wx * wy;
                    }
                }
                for (int c = 0; c < 4; ++c) value[c] = total > 0.0 ? static_cast<float>(sum[c] / total) : 0.0f;
            } else {
                const double fx = (x + 0.5) * sx - 0.5, fy = (y + 0.5) * sy - 0.5;
                const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
                const float tx = static_cast<float>(fx - ix), ty = static_cast<float>(fy - iy);
                for (int c = 0; c < 4; ++c) {
                    const float top = sample(ix, iy, c) * (1 - tx) + sample(ix + 1, iy, c) * tx;
                    const float bottom = sample(ix, iy + 1, c) * (1 - tx) + sample(ix + 1, iy + 1, c) * tx;
                    value[c] = top * (1 - ty) + bottom * ty;
                }
            }
            storePixel(out, x, y, value);
        }
    return out;
}

void blit(Image& target, const Image& source, int x, int y) {
    for (int row = 0; row < source.h; ++row) {
        const int ty = y + row;
        if (ty < 0 || ty >= target.h) continue;
        for (int col = 0; col < source.w; ++col) {
            const int tx = x + col;
            if (tx < 0 || tx >= target.w) continue;
            std::copy_n(source.at(col, row), 4, target.at(tx, ty));
        }
    }
}

void applyPsychPalette(Image& image, const std::uint32_t colors[3]) {
    float palette[3][3];
    for (int c = 0; c < 3; ++c) {
        palette[c][0] = static_cast<float>((colors[c] >> 16) & 0xFF) / 255.0f;
        palette[c][1] = static_cast<float>((colors[c] >> 8) & 0xFF) / 255.0f;
        palette[c][2] = static_cast<float>(colors[c] & 0xFF) / 255.0f;
    }
    for (size_t i = 0; i < static_cast<size_t>(image.w) * static_cast<size_t>(image.h); ++i) {
        std::uint8_t* p = image.rgba.data() + i * 4u;
        if (p[3] == 0) continue;
        const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
        for (int k = 0; k < 3; ++k)
            p[k] = static_cast<std::uint8_t>(std::lround(std::min(1.0f, r * palette[0][k] + g * palette[1][k] + b * palette[2][k]) * 255.0f));
    }
}

const std::uint32_t* psychDefaultPalette(int lane, bool pixel) {
    static const std::uint32_t normal[4][3] = {
        {0xFFC24B99u, 0xFFFFFFFFu, 0xFF3C1F56u},
        {0xFF00FFFFu, 0xFFFFFFFFu, 0xFF1542B7u},
        {0xFF12FA05u, 0xFFFFFFFFu, 0xFF0A4447u},
        {0xFFF9393Fu, 0xFFFFFFFFu, 0xFF651038u}};
    static const std::uint32_t pixelArt[4][3] = {
        {0xFFE276FFu, 0xFFFFF9FFu, 0xFF60008Du},
        {0xFF3DCAFFu, 0xFFF4FFFFu, 0xFF003060u},
        {0xFF71E300u, 0xFFF6FFE6u, 0xFF003100u},
        {0xFFFF884Eu, 0xFFFFFAF5u, 0xFF6C0000u}};
    return pixel ? pixelArt[lane & 3] : normal[lane & 3];
}

PackResult packRects(const std::vector<std::array<int, 2>>& sizes, int padding, int maxSide) {
    PackResult result;
    result.positions.assign(sizes.size(), {0, 0});
    if (sizes.empty()) {
        result.ok = true;
        return result;
    }
    long long area = 0;
    int widest = 0;
    for (const auto& size : sizes) {
        area += static_cast<long long>(size[0] + padding) * (size[1] + padding);
        widest = std::max(widest, size[0] + padding);
    }
    std::vector<size_t> order(sizes.size());
    std::iota(order.begin(), order.end(), size_t{0});
    // Los altos primero: cada estante mide lo que su primer fotograma.
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sizes[a][1] > sizes[b][1]; });
    int width = std::max(widest, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(area)) * 1.15)));
    for (; width <= maxSide; width = std::max(width + 64, static_cast<int>(width * 1.25))) {
        int x = padding, y = padding, shelf = 0;
        bool fits = true;
        for (size_t i : order) {
            const int w = sizes[i][0], h = sizes[i][1];
            if (x + w + padding > width && x > padding) {
                y += shelf + padding;
                x = padding;
                shelf = 0;
            }
            if (x + w + padding > width) { fits = false; break; }
            result.positions[i] = {x, y};
            x += w + padding;
            shelf = std::max(shelf, h);
        }
        const int height = y + shelf + padding;
        if (fits && height <= maxSide) {
            result.w = width;
            result.h = height;
            result.ok = true;
            return result;
        }
    }
    return result;
}

}  // namespace fml::notelab

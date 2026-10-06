#pragma once

namespace nlbrand {
inline const fml::notelab::Image& logo() {
    static const auto image = [] {
        fml::notelab::Image result;
#ifdef _WIN32
        const auto module = GetModuleHandleW(nullptr);
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(102), MAKEINTRESOURCEW(10));
        if (resource) {
            const auto loaded = LoadResource(module, resource);
            const auto size = SizeofResource(module, resource);
            const auto data = static_cast<const unsigned char*>(LockResource(loaded));
            if (data && size && size <= 16u * 1024u * 1024u)
                fml::notelab::decodePng(std::vector<unsigned char>(data, data + size), result);
        }
#endif
        return result;
    }();
    return image;
}

inline bool applyWindowIcon(SDL_Window* window) {
    const auto& image = logo();
    if (image.empty()) return false;
    auto* surface = SDL_CreateSurfaceFrom(image.w, image.h, SDL_PIXELFORMAT_RGBA32,
        const_cast<unsigned char*>(image.rgba.data()), image.w * 4);
    if (!surface) return false;
    const bool applied = SDL_SetWindowIcon(window, surface);
    SDL_DestroySurface(surface);
    return applied;
}

inline fml::GlRenderer::PreviewImage preview(fml::GlRenderer& renderer) {
    constexpr const char* key = "notelab-ui/logo.png";
    if (!renderer.dynamicFrameReady(key)) {
        const auto& image = logo();
        if (image.empty()) return {};
        auto bgra = image.rgba;
        for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
        if (!renderer.uploadDynamicFrame(key, bgra.data(), image.w, image.h)) return {};
    }
    return renderer.previewImage(key);
}
}

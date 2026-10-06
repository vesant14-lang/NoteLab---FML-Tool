#include "SparrowAtlas.hpp"

#include "../../third_party/pugixml.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fml {
namespace {

int intAttr(const pugi::xml_node& n, const char* name, int def = 0) {
    if (pugi::xml_attribute a = n.attribute(name)) return a.as_int(def);
    return def;
}

// El numero de frame va al final del nombre. Puede tener 4 digitos (lo habitual)
// o cualquier otra cantidad, asi que se quitan todos los digitos finales.
int trailingNumber(const std::string& name) {
    size_t i = name.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]))) --i;
    if (i == name.size()) return -1;   // sin numero
    try { return std::stoi(name.substr(i)); } catch (...) { return -1; }
}

}  // namespace

std::string SparrowAtlas::stripFrameNumber(const std::string& name) {
    size_t i = name.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]))) --i;
    std::string base = name.substr(0, i);
    // Los exportadores suelen dejar un espacio antes del numero.
    while (!base.empty() && base.back() == ' ') base.pop_back();
    return base;
}

std::vector<size_t> SparrowAtlas::framesFor(const std::string& prefix) const {
    std::vector<size_t> out;
    if (prefix.empty()) return out;
    auto sortFrames = [&](size_t prefixLength) {
        // FlxFrame.sort: XML packing order is not animation order. Keep this
        // in sync with the host's frame UV selection, including padded names.
        std::stable_sort(out.begin(), out.end(), [&](size_t a, size_t b) {
            const auto number = [&](size_t i) {
                try { return std::stoi(frames[i].name.substr(prefixLength)); }
                catch (...) { return 0; } // FlxFrame.sort treats non-numbers as 0.
            };
            return number(a) < number(b);
        });
    };
    for (size_t i = 0; i < frames.size(); ++i) {
        const std::string& n = frames[i].name;
        if (n.size() >= prefix.size() && n.compare(0, prefix.size(), prefix) == 0)
            out.push_back(i);
    }
    if (!out.empty()) { sortFrames(prefix.size()); return out; }

    // Segundo intento sin espacios al final: los XML de mods los traen a menudo.
    std::string p = prefix;
    while (!p.empty() && p.back() == ' ') p.pop_back();
    if (p.empty() || p == prefix) return out;
    for (size_t i = 0; i < frames.size(); ++i) {
        const std::string& n = frames[i].name;
        if (n.size() >= p.size() && n.compare(0, p.size(), p) == 0) out.push_back(i);
    }
    sortFrames(p.size());
    return out;
}

Result<SparrowAtlas> parseSparrowAtlas(const std::string& xmlText,
                                       const std::string& sourcePath,
                                       DiagnosticSink&    sink) {
    pugi::xml_document doc;
    // pugixml se come el BOM UTF-8 solo; estos XML casi siempre lo traen.
    const pugi::xml_parse_result pr = doc.load_buffer(xmlText.data(), xmlText.size());
    if (!pr) {
        sink.error("FML-2201", std::string("atlas XML invalido: ") + pr.description(), sourcePath);
        return Result<SparrowAtlas>::fail(pr.description());
    }

    pugi::xml_node root = doc.child("TextureAtlas");
    if (!root) {
        sink.error("FML-2202", "no se encontro <TextureAtlas>", sourcePath);
        return Result<SparrowAtlas>::fail("sin <TextureAtlas>");
    }

    SparrowAtlas atlas;
    if (pugi::xml_attribute ip = root.attribute("imagePath")) atlas.imagePath = ip.value();

    // prefijo -> (numero de frame, indice) para poder ordenar por numero y no
    // por orden de aparicion, que en algunos exportadores no coincide.
    std::map<std::string, std::vector<std::pair<int, size_t>>> ordering;

    for (pugi::xml_node n = root.child("SubTexture"); n; n = n.next_sibling("SubTexture")) {
        AtlasFrame f;
        f.name   = n.attribute("name").value();
        f.x      = intAttr(n, "x");
        f.y      = intAttr(n, "y");
        f.w      = intAttr(n, "width");
        f.h      = intAttr(n, "height");
        f.frameX = intAttr(n, "frameX");
        f.frameY = intAttr(n, "frameY");
        f.frameW = intAttr(n, "frameWidth",  f.w);
        f.frameH = intAttr(n, "frameHeight", f.h);
        if (pugi::xml_attribute r = n.attribute("rotated"))
            f.rotated = (std::string(r.value()) == "true");
        f.trimmed = (f.frameX != 0 || f.frameY != 0 || f.frameW != f.w || f.frameH != f.h);
        // FlxAtlasFrames.hx:281-282 — sin recorte, el tamano de origen de un
        // frame rotado va CAMBIADO respecto al rect del atlas: lo que el XML
        // guarda como ancho es el alto de lo que se ve. Con recorte no se toca:
        // ahi `frameWidth`/`frameHeight` ya vienen en el espacio de la imagen.
        if (f.rotated && !f.trimmed) {
            f.frameW = f.h;
            f.frameH = f.w;
        }

        if (f.w <= 0 || f.h <= 0) {
            sink.warn("FML-2203", "frame con tamano invalido: " + f.name, sourcePath);
            continue;
        }

        const std::string prefix = SparrowAtlas::stripFrameNumber(f.name);
        const int num = trailingNumber(f.name);

        const size_t idx = atlas.frames.size();
        atlas.frames.push_back(std::move(f));
        ordering[prefix].push_back({num, idx});
    }

    if (atlas.frames.empty()) {
        sink.warn("FML-2205", "atlas sin frames", sourcePath);
    }

    for (auto& kv : ordering) {
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const std::pair<int, size_t>& a, const std::pair<int, size_t>& b) {
                      if (a.first != b.first) return a.first < b.first;
                      return a.second < b.second;
                  });
        std::vector<size_t> idxs;
        idxs.reserve(kv.second.size());
        for (const auto& p : kv.second) idxs.push_back(p.second);
        atlas.byPrefix.emplace(kv.first, std::move(idxs));
    }

    return Result<SparrowAtlas>::ok(std::move(atlas));
}

Result<SparrowAtlas> parsePackerAtlas(const std::string& text,
                                      const std::string& sourcePath,
                                      DiagnosticSink&    sink) {
    SparrowAtlas atlas;
    std::map<std::string, std::vector<std::pair<int, size_t>>> ordering;

    size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) { if (nl >= text.size()) break; continue; }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            sink.warn("FML-2210", "linea sin '=' en atlas Packer", sourcePath, lineNo);
            if (nl >= text.size()) break;
            continue;
        }

        std::string name = line.substr(0, eq);
        while (!name.empty() && name.back() == ' ') name.pop_back();

        AtlasFrame f;
        f.name = name;
        std::istringstream nums(line.substr(eq + 1));
        if (!(nums >> f.x >> f.y >> f.w >> f.h)) {
            sink.warn("FML-2211", "coordenadas ilegibles en atlas Packer: " + name,
                      sourcePath, lineNo);
            if (nl >= text.size()) break;
            continue;
        }
        f.frameW = f.w;
        f.frameH = f.h;
        if (f.w <= 0 || f.h <= 0) {
            sink.warn("FML-2203", "frame con tamano invalido: " + f.name, sourcePath, lineNo);
            if (nl >= text.size()) break;
            continue;
        }

        const std::string prefix = SparrowAtlas::stripFrameNumber(f.name);
        const int num = trailingNumber(f.name);
        const size_t idx = atlas.frames.size();
        atlas.frames.push_back(std::move(f));
        ordering[prefix].push_back({num, idx});

        if (nl >= text.size()) break;
    }

    if (atlas.frames.empty()) {
        sink.warn("FML-2205", "atlas Packer sin frames", sourcePath);
        return Result<SparrowAtlas>::fail("sin frames");
    }

    for (auto& kv : ordering) {
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const std::pair<int, size_t>& a, const std::pair<int, size_t>& b) {
                      if (a.first != b.first) return a.first < b.first;
                      return a.second < b.second;
                  });
        std::vector<size_t> idxs;
        idxs.reserve(kv.second.size());
        for (const auto& p : kv.second) idxs.push_back(p.second);
        atlas.byPrefix.emplace(kv.first, std::move(idxs));
    }
    return Result<SparrowAtlas>::ok(std::move(atlas));
}

}  // namespace fml

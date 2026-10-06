#include "AnimateAtlas.hpp"

#include "../../third_party/json.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace fml {
namespace {

// Los exportadores de Animate alternan entre claves largas y abreviadas segun
// version. Se aceptan las dos.
const json* pick(const json& o, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        auto it = o.find(k);
        if (it != o.end()) return &(*it);
    }
    return nullptr;
}

// M3D es 4x4; la parte afin 2D esta en 0,1,4,5,12,13.
bool readSixMatrixValues(const json& matrix, float (&values)[6]) {
    if (matrix.is_array() && matrix.size() == 6) {
        for (int index = 0; index < 6; ++index) {
            if (!matrix[index].is_number()) return false;
            values[index] = matrix[index].get<float>();
        }
    } else if (matrix.is_string()) {
        std::string text = matrix.get<std::string>();
        if (text.size() > 1024) return false;
        std::replace(text.begin(), text.end(), ',', ' ');
        std::istringstream input(text);
        for (float& value : values) if (!(input >> value)) return false;
        input >> std::ws;
        if (!input.eof()) return false;
    } else return false;
    return std::all_of(std::begin(values), std::end(values), [](float value) { return std::isfinite(value); });
}

void readMatrix(const json& e, float& a, float& b, float& c, float& d, float& tx, float& ty) {
    a = 1; b = 0; c = 0; d = 1; tx = 0; ty = 0;
    if (const json* m = pick(e, {"M3D", "Matrix3D"})) {
        if (m->is_array() && m->size() >= 14) {
            a  = (*m)[0].get<float>();  b  = (*m)[1].get<float>();
            c  = (*m)[4].get<float>();  d  = (*m)[5].get<float>();
            tx = (*m)[12].get<float>(); ty = (*m)[13].get<float>();
        } else if (m->is_object()) {   // algunas versiones lo dan como objeto
            a  = m->value("m00", 1.0f); b  = m->value("m01", 0.0f);
            c  = m->value("m10", 0.0f); d  = m->value("m11", 1.0f);
            tx = m->value("m30", 0.0f); ty = m->value("m31", 0.0f);
        }
    } else if (const json* m = pick(e, {"MX", "Matrix"})) {
        float values[6];
        if (!readSixMatrixValues(*m, values)) throw std::invalid_argument("Invalid six-component Animate matrix");
        a = values[0]; b = values[1]; c = values[2]; d = values[3]; tx = values[4]; ty = values[5];
    }
}

std::string str(const json& o, std::initializer_list<const char*> keys) {
    if (const json* v = pick(o, keys)) if (v->is_string()) return v->get<std::string>();
    return {};
}

}  // namespace

bool AnimateAtlas::load(const std::string& spritemapJson, const std::string& animationJson,
                        const std::string& sourcePath, DiagnosticSink& sink) {
    m_sprites.clear(); m_symbols.clear(); m_bounds.clear(); m_imageFile = "spritemap1.png";
    // ---------------- spritemap ----------------
    try {
        const json sm = json::parse(spritemapJson, nullptr, true, true);
        const json* atlas = pick(sm, {"ATLAS"});
        const json* sprites = atlas ? pick(*atlas, {"SPRITES"}) : nullptr;
        if (!sprites || !sprites->is_array()) {
            sink.error("FML-2301", "spritemap sin ATLAS.SPRITES", sourcePath);
            return false;
        }
        for (const json& entry : *sprites) {
            const json* sp = pick(entry, {"SPRITE"});
            if (!sp) sp = &entry;
            AnimateSprite s;
            s.name    = str(*sp, {"name", "n"});
            s.sourceImage = str(*sp, {"_fmlSourceImage"});
            s.x       = sp->value("x", 0);
            s.y       = sp->value("y", 0);
            s.w       = sp->value("w", 0);
            s.h       = sp->value("h", 0);
            s.rotated = sp->value("rotated", false);
            if (s.name.empty() || s.w <= 0 || s.h <= 0) continue;
            m_sprites[s.name] = std::move(s);
        }
        if (const json* meta = pick(sm, {"meta"})) {
            const std::string img = str(*meta, {"image"});
            if (!img.empty()) m_imageFile = img;
        }
    } catch (const std::exception& e) {
        sink.error("FML-2303", std::string("spritemap ilegible: ") + e.what(), sourcePath);
        return false;
    }

    // ---------------- Animation.json ----------------
    try {
        const json an = json::parse(animationJson, nullptr, true, true);

        auto readSymbol = [&](const json& node, const std::string& fallbackName) {
            Symbol sym;
            sym.name = str(node, {"SN", "SYMBOL_name"});
            if (sym.name.empty()) sym.name = fallbackName;

            const json* tl = pick(node, {"TL", "TIMELINE"});
            const json* layers = tl ? pick(*tl, {"L", "LAYERS"}) : nullptr;
            if (!layers || !layers->is_array()) return sym;

            for (const json& lj : *layers) {
                Layer layer;
                layer.name = str(lj, {"LN", "Layer_name"});
                const json* frames = pick(lj, {"FR", "Frames"});
                if (!frames || !frames->is_array()) { sym.layers.push_back(std::move(layer)); continue; }

                for (const json& fj : *frames) {
                    Frame f;
                    f.index    = fj.value("I", fj.value("index", 0));
                    f.duration = fj.value("DU", fj.value("duration", 1));
                    if (f.duration < 1) f.duration = 1;

                    const json* els = pick(fj, {"E", "elements"});
                    if (els && els->is_array()) {
                        for (const json& ej : *els) {
                            Element el;
                            if (const json* asi = pick(ej, {"ASI", "ATLAS_SPRITE_instance"})) {
                                el.isSprite = true;
                                el.name = str(*asi, {"N", "name"});
                                readMatrix(*asi, el.a, el.b, el.c, el.d, el.tx, el.ty);
                            } else if (const json* si = pick(ej, {"SI", "SYMBOL_Instance"})) {
                                el.isSprite = false;
                                el.name = str(*si, {"SN", "SYMBOL_name"});
                                el.firstFrame = si->value("FF", si->value("firstFrame", 0));
                                el.loopMode = str(*si, {"LP", "loop"});
                                el.symbolType = str(*si, {"ST", "symbolType"});
                                readMatrix(*si, el.a, el.b, el.c, el.d, el.tx, el.ty);
                            } else {
                                continue;
                            }
                            if (!el.name.empty()) f.elements.push_back(std::move(el));
                        }
                    }
                    layer.frames.push_back(std::move(f));
                }
                sym.layers.push_back(std::move(layer));
            }
            return sym;
        };

        // Diccionario de simbolos.
        if (const json* sd = pick(an, {"SD", "SYMBOL_DICTIONARY"})) {
            if (const json* syms = pick(*sd, {"S", "Symbols"})) {
                if (syms->is_array()) {
                    for (const json& sj : *syms) {
                        Symbol s = readSymbol(sj, "");
                        if (!s.name.empty()) m_symbols[s.name] = std::move(s);
                    }
                }
            }
        }

        // Linea de tiempo principal, por si alguna animacion la referencia.
        if (const json* main = pick(an, {"AN", "ANIMATION"})) {
            Symbol s = readSymbol(*main, str(*main, {"N", "name"}));
            if (!s.name.empty() && !m_symbols.count(s.name)) m_symbols[s.name] = std::move(s);
        }
    } catch (const std::exception& e) {
        sink.error("FML-2304", std::string("Animation.json ilegible: ") + e.what(), sourcePath);
        return false;
    }

    if (m_symbols.empty()) {
        sink.error("FML-2305", "el atlas de Animate no declara simbolos", sourcePath);
        return false;
    }
    return true;
}

namespace {
std::string trimmed(const std::string& v) {
    size_t a = 0, b = v.size();
    while (a < b && (v[a] == ' ' || v[a] == '	')) ++a;
    while (b > a && (v[b - 1] == ' ' || v[b - 1] == '	')) --b;
    return v.substr(a, b - a);
}
}  // namespace

const AnimateAtlas::Symbol* AnimateAtlas::findSymbol(const std::string& name) const {
    auto it = m_symbols.find(name);
    if (it != m_symbols.end()) return &it->second;
    const std::string t = trimmed(name);
    for (const auto& kv : m_symbols)
        if (trimmed(kv.first) == t) return &kv.second;
    return nullptr;
}

bool AnimateAtlas::loadPages(const std::vector<std::pair<std::string, std::string>>& pages,
                             const std::string& animationJson, const std::string& sourcePath,
                             DiagnosticSink& sink) {
    if (pages.empty() || pages.size() > 8) return false;
    json merged;
    merged["ATLAS"]["SPRITES"] = json::array();
    std::set<std::string> names;
    try {
        for (const auto& page : pages) {
            if (page.second.size() > 32u * 1024u * 1024u) return false;
            const json data = json::parse(page.second, nullptr, true, true);
            const json& sprites = data.at("ATLAS").at("SPRITES");
            if (!sprites.is_array() || merged["ATLAS"]["SPRITES"].size() + sprites.size() > 32768) return false;
            std::string image = page.first;
            if (data.contains("meta") && data["meta"].is_object() && data["meta"].contains("image") && data["meta"]["image"].is_string()) {
                const std::string declared = data["meta"]["image"].get<std::string>();
                if (declared.find("..") != std::string::npos || declared.find(':') != std::string::npos ||
                    declared.empty() || declared.front() == '/' || declared.front() == '\\') return false;
                const size_t slash = image.find_last_of('/');
                image = (slash == std::string::npos ? "" : image.substr(0, slash + 1)) + declared;
            }
            for (json sprite : sprites) {
                json& value = sprite.contains("SPRITE") ? sprite["SPRITE"] : sprite;
                const std::string name = str(value, {"name", "n"});
                if (name.empty() || !names.insert(name).second) return false;
                value["_fmlSourceImage"] = image;
                merged["ATLAS"]["SPRITES"].push_back(std::move(sprite));
            }
        }
        return load(merged.dump(), animationJson, sourcePath, sink);
    } catch (const std::exception& error) {
        sink.error("FML-2303", std::string("Multi-page Animate: ") + error.what(), sourcePath);
        return false;
    }
}

std::vector<std::string> AnimateAtlas::imagePaths() const {
    std::set<std::string> paths;
    for (const auto& sprite : m_sprites)
        if (!sprite.second.sourceImage.empty()) paths.insert(sprite.second.sourceImage);
    return {paths.begin(), paths.end()};
}

bool AnimateAtlas::hasSymbol(const std::string& name) const {
    return findSymbol(name) != nullptr;
}

std::vector<std::string> AnimateAtlas::symbolNames() const {
    std::vector<std::string> out;
    out.reserve(m_symbols.size());
    for (const auto& kv : m_symbols) out.push_back(kv.first);
    return out;
}

const AnimateAtlas::Frame* AnimateAtlas::frameAt(const Layer& l, int t) const {
    for (const Frame& f : l.frames)
        if (t >= f.index && t < f.index + f.duration) return &f;
    return nullptr;
}

int AnimateAtlas::frameCount(const std::string& symbol) const {
    const Symbol* sym = findSymbol(symbol);
    if (!sym) return 0;
    int total = 0;
    for (const Layer& l : sym->layers)
        for (const Frame& f : l.frames)
            total = std::max(total, f.index + f.duration);
    return total;
}

void AnimateAtlas::flattenSymbol(const Symbol& s, int frame,
                                 float pa, float pb, float pc, float pd, float ptx, float pty,
                                 int depth, std::vector<AnimateElement>& out) const {
    if (depth > 12) return;   // corta ciclos: un simbolo corrupto no cuelga la app

    // En Animate la primera capa es la de ARRIBA, asi que se dibuja al reves.
    for (auto it = s.layers.rbegin(); it != s.layers.rend(); ++it) {
        const Frame* f = frameAt(*it, frame);
        if (!f) continue;
        for (const Element& e : f->elements) {
            // Composicion: hijo * padre
            const float a  = e.a * pa + e.b * pc;
            const float b  = e.a * pb + e.b * pd;
            const float c  = e.c * pa + e.d * pc;
            const float d  = e.c * pb + e.d * pd;
            const float tx = e.tx * pa + e.ty * pc + ptx;
            const float ty = e.tx * pb + e.ty * pd + pty;

            if (e.isSprite) {
                auto sp = m_sprites.find(e.name);
                if (sp == m_sprites.end()) continue;
                out.push_back({&sp->second, a, b, c, d, tx, ty});
            } else {
                const Symbol* sub = findSymbol(e.name);
                if (!sub) continue;
                const int subCount = frameCount(e.name);
                int subFrame = e.firstFrame;
                if (subCount > 0) {
                    // FlxKeyFrame subtracts its index before advancing children.
                    // SF is a held pose, PO clamps, LP loops (including reverse).
                    std::string mode = e.loopMode;
                    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    const bool reverse = mode == "lpr" || mode == "por" || mode == "loopreverse" || mode == "playoncereverse";
                    const bool single = mode == "sf" || mode == "singleframe" || mode == "single frame";
                    const bool once = mode == "po" || mode == "por" || mode == "playonce" || mode == "play once" || mode == "playoncereverse";
                    if (!single) subFrame += (reverse ? -1 : 1) * (frame - f->index);
                    if (e.symbolType == "MC" || e.symbolType == "movieclip") subFrame = 0;
                    else if (once) subFrame = std::clamp(subFrame, 0, subCount - 1);
                    else if (!single) subFrame = ((subFrame % subCount) + subCount) % subCount;
                }
                flattenSymbol(*sub, subFrame, a, b, c, d, tx, ty, depth + 1, out);
            }
        }
    }
}

void AnimateAtlas::flatten(const std::string& symbol, int frame,
                           std::vector<AnimateElement>& out) const {
    out.clear();
    const Symbol* sym = findSymbol(symbol);
    if (!sym) return;
    const int count = frameCount(symbol);
    if (count > 0) frame = ((frame % count) + count) % count;
    flattenSymbol(*sym, frame, 1, 0, 0, 1, 0, 0, 0, out);
}

AnimateBounds AnimateAtlas::bounds(const std::string& symbol) const {
    const auto found = m_bounds.find(symbol);
    if (found != m_bounds.end()) return found->second;
    AnimateBounds result;
    std::vector<AnimateElement> parts;
    for (int frame = 0; frame < frameCount(symbol); ++frame) {
        flatten(symbol, frame, parts);
        for (const auto& p : parts) if (p.sprite) {
            const float w = static_cast<float>(p.sprite->width()), h = static_cast<float>(p.sprite->height());
            for (int corner = 0; corner < 4; ++corner) {
                const float x = p.tx + p.a * ((corner & 1) ? w : 0) + p.c * ((corner & 2) ? h : 0);
                const float y = p.ty + p.b * ((corner & 1) ? w : 0) + p.d * ((corner & 2) ? h : 0);
                if (!result.valid) { result = {x, y, x, y, true}; }
                else { result.left = std::min(result.left, x); result.top = std::min(result.top, y);
                       result.right = std::max(result.right, x); result.bottom = std::max(result.bottom, y); }
            }
        }
    }
    m_bounds[symbol] = result;
    return result;
}

bool AnimateAtlas::canMountTimeline(const std::string& original, std::string& reason) const {
    reason.clear();
    json root = json::parse(original, nullptr, false, true);
    if (!root.is_object()) { reason = "Animation.json no valido"; return false; }
    bool safe = true;
    size_t visitedNodes = 0;
    std::function<void(const json&, int)> check = [&](const json& node, int depth) {
        if (!safe) return;
        if (depth > 128 || ++visitedNodes > 1000000) { safe = false; return; }
        if (node.is_array()) { for (const auto& child : node) { check(child, depth + 1); if (!safe) break; } return; }
        if (!node.is_object()) return;
        if (const auto* type = pick(node, {"LT", "Layer_type"}); type && pick(node, {"FR", "Frames"}))
            if (!type->is_string() || (type->get<std::string>() != "normal" && type->get<std::string>() != "N")) safe = false;
        if (const auto* elements = pick(node, {"E", "elements"})) if (elements->is_array())
            for (const auto& element : *elements) {
                const auto* instance = pick(element, {"SI", "SYMBOL_Instance", "ASI", "ATLAS_SPRITE_instance"});
                if (!instance || !instance->is_object()) { safe = false; continue; }
                if (pick(element, {"SI", "SYMBOL_Instance"})) {
                    if (!hasSymbol(str(*instance, {"SN", "SYMBOL_name"}))) safe = false;
                } else if (!m_sprites.count(str(*instance, {"N", "name"}))) safe = false;
                static const std::set<std::string> allowed{"SN","SYMBOL_name","IN","instanceName","ST","symbolType","FF","firstFrame","LP","loop","TRP","transformationPoint","M3D","Matrix3D","MX","Matrix","B","N","name"};
                for (auto it = instance->begin(); it != instance->end(); ++it)
                    if (!allowed.count(it.key())) safe = false;
                if (const auto* blend = pick(*instance, {"B"}); blend && (!blend->is_number_integer() || blend->get<int>() != 0)) safe = false;
                if (const auto* matrix = pick(*instance, {"MX", "Matrix"})) {
                    float values[6];
                    if (!readSixMatrixValues(*matrix, values)) safe = false;
                }
                const auto* m = pick(*instance, {"M3D", "Matrix3D"});
                if (m && m->is_array() && m->size() >= 16) {
                    for (int index : {2,3,6,7,8,9,11,14})
                        if (!(*m)[index].is_number() || std::abs((*m)[index].get<double>()) > 0.000001) safe = false;
                }
            }
        for (const auto& child : node) { check(child, depth + 1); if (!safe) break; }
    };
    check(root, 0);
    std::set<std::string> stack;
    std::map<std::string, std::pair<size_t, int>> estimates;
    std::function<std::pair<size_t, int>(const Symbol&)> checkGraph = [&](const Symbol& symbol) {
        if (!safe) return std::pair<size_t, int>{0, 0};
        const auto cached = estimates.find(symbol.name);
        if (cached != estimates.end()) return cached->second;
        if (stack.size() > 12 || !stack.insert(symbol.name).second) {
            safe = false;
            return std::pair<size_t, int>{0, 0};
        }
        size_t parts = 0;
        int height = 0;
        for (const auto& layer : symbol.layers) {
            size_t layerParts = 0;
            for (const auto& frame : layer.frames) {
                size_t frameParts = 0;
                for (const auto& element : frame.elements) {
                    if (element.isSprite) ++frameParts;
                    else if (const auto* sub = findSymbol(element.name)) {
                        const auto child = checkGraph(*sub);
                        frameParts += child.first;
                        height = std::max(height, child.second + 1);
                    } else safe = false;
                    if (!safe || frameParts > 16384 || height > 12) { safe = false; break; }
                }
                layerParts = std::max(layerParts, frameParts);
                if (!safe) break;
            }
            parts += layerParts;
            if (!safe || parts > 16384) { safe = false; break; }
        }
        stack.erase(symbol.name);
        const std::pair<size_t, int> estimate{parts, height};
        if (safe) estimates[symbol.name] = estimate;
        return estimate;
    };
    for (const auto& symbol : m_symbols) {
        checkGraph(symbol.second);
        if (!safe) break;
    }
    if (!safe) {
        reason = "masks, filters, unsupported data or excessive timeline depth/parts";
        return false;
    }
    return true;
}

std::string AnimateAtlas::compatibleTimeline(const std::string& original, std::string& reason) const {
    if (!canMountTimeline(original, reason)) return {};
    json root = json::parse(original, nullptr, false, true);
    bool safe = true;
    std::size_t totalParts = 0, totalFrames = 0;
    auto bake = [&](json& node, const std::string& fallback) {
        std::string name = str(node, {"SN", "SYMBOL_name"});
        if (name.empty()) name = fallback;
        if (!hasSymbol(name)) { safe = false; return; }
        // Keep frame labels at their authored start index. A label must not be
        // duplicated over the held frames when one keyframe becomes many poses.
        std::map<int,json> labels;
        const auto* timeline = pick(node, {"TL","TIMELINE"});
        const auto* layers = timeline ? pick(*timeline, {"L","LAYERS"}) : nullptr;
        if (layers && layers->is_array()) for (const auto& layer : *layers) {
            const auto* authored = pick(layer, {"FR","Frames"});
            if (!authored || !authored->is_array()) continue;
            for (const auto& keyframe : *authored) {
                const auto label = str(keyframe, {"N","name"});
                if (label.empty()) continue;
                const int at = keyframe.value("I",keyframe.value("index",0));
                if (labels.count(at) && str(labels[at],{"N","name"}) != label) { safe = false; return; }
                labels[at] = {{"N",label}};
                for (const char* field : {"LT","labelType"}) if (keyframe.contains(field)) labels[at][field] = keyframe[field];
            }
        }
        json frames = json::array();
        const int count = frameCount(name);
        totalFrames += static_cast<std::size_t>(std::max(0, count));
        if (totalFrames > 50000) { safe = false; return; }
        std::vector<AnimateElement> parts;
        for (int frame = 0; frame < count; ++frame) {
            flatten(name, frame, parts);
            totalParts += parts.size();
            if (totalParts > 500000) { safe = false; return; }
            json elements = json::array();
            for (const auto& p : parts) {
                if (!p.sprite) { safe = false; return; }
                for (float v : {p.a,p.b,p.c,p.d,p.tx,p.ty}) if (!std::isfinite(v)) { safe = false; return; }
                elements.push_back({{"ASI", {{"N",p.sprite->name}, {"M3D",{p.a,p.b,0,0,p.c,p.d,0,0,0,0,1,0,p.tx,p.ty,0,1}}}}});
            }
            json pose = {{"I",frame},{"DU",1},{"E",std::move(elements)}};
            if (labels.count(frame)) for (auto it=labels[frame].begin();it!=labels[frame].end();++it) pose[it.key()]=it.value();
            frames.push_back(std::move(pose));
        }
        node.erase("TIMELINE");
        node["TL"] = {{"L",json::array({{{"LN","Resolved poses"},{"FR",std::move(frames)}}})}};
    };
    const char* dictionaryKey = root.contains("SD") ? "SD" : "SYMBOL_DICTIONARY";
    if (root.contains(dictionaryKey) && root[dictionaryKey].is_object()) {
        auto& dictionary = root[dictionaryKey];
        const char* symbolsKey = dictionary.contains("S") ? "S" : "Symbols";
        if (dictionary.contains(symbolsKey) && dictionary[symbolsKey].is_array())
            for (auto& node : dictionary[symbolsKey]) { bake(node, ""); if (!safe) break; }
    }
    const char* mainKey = root.contains("AN") ? "AN" : "ANIMATION";
    if (safe && root.contains(mainKey)) bake(root[mainKey], str(root[mainKey], {"N","name"}));
    if (!safe) { reason = "limite de conversion o simbolo no resuelto: se conserva el original"; return {}; }
    reason = "Animate compatible: poses anidadas resueltas, original conservado";
    return root.dump();
}

}  // namespace fml

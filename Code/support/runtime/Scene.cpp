#include "Scene.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <set>

namespace fml {

void inheritSpriteShaderUniforms(
        SpriteShaderBinding& binding,
        std::map<std::string, SpriteShaderUniform>& programState) {
    for (SpriteShaderUniform& uniform : binding.uniforms) {
        if (uniform.isSampler) continue;
        if (uniform.assigned) {
            programState[uniform.name] = uniform;
            continue;
        }
        const auto previous = programState.find(uniform.name);
        if (previous == programState.end()) continue;
        const SpriteShaderUniform& inherited = previous->second;
        uniform.valueCount = inherited.valueCount;
        uniform.arraySize = inherited.arraySize;
        uniform.matrixColumns = inherited.matrixColumns;
        for (int index = 0; index < SpriteShaderUniform::MaxValues; ++index)
            uniform.value[index] = inherited.value[index];
        uniform.assigned = true;
    }
}

int RenderList::internTexture(const std::string& path) {
    for (size_t i = 0; i < textures.size(); ++i)
        if (textures[i] == path) return static_cast<int>(i);
    textures.push_back(path);
    return static_cast<int>(textures.size()) - 1;
}

const SparrowAtlas* AtlasStore::get(const std::string& virtualPath) {
    if (virtualPath.empty()) return nullptr;
    auto it = m_cache.find(virtualPath);
    if (it != m_cache.end()) return &it->second;
    if (m_failed.count(virtualPath)) return nullptr;
    if (!readText) return nullptr;

    const std::string text = readText(virtualPath);
    if (text.empty()) { m_failed[virtualPath] = true; return nullptr; }

    DiagnosticSink local;
    DiagnosticSink& s = sink ? *sink : local;

    const bool isPacker = virtualPath.size() > 4 &&
                          virtualPath.compare(virtualPath.size() - 4, 4, ".txt") == 0;
    auto parsed = isPacker ? parsePackerAtlas(text, virtualPath, s)
                           : parseSparrowAtlas(text, virtualPath, s);
    if (!parsed) { m_failed[virtualPath] = true; return nullptr; }
    return &m_cache.emplace(virtualPath, std::move(parsed.value())).first->second;
}

const SparrowAtlas* AtlasStore::getCharacter(const UniversalCharacter& character) {
    if (character.resolvedAtlases.size() < 2 ||
        character.resolvedAtlas != character.resolvedAtlases.front()) return get(character.resolvedAtlas);
    if (character.resolvedAtlases.size() != character.resolvedImages.size() ||
        character.resolvedAtlases.size() > 32) return nullptr;
    std::string key = "multi:";
    for (size_t page = 0; page < character.resolvedAtlases.size(); ++page)
        key += character.resolvedAtlases[page] + "\n" + character.resolvedImages[page] + "\n";
    const auto existing = m_cache.find(key);
    if (existing != m_cache.end()) return &existing->second;
    if (m_failed.count(key)) return nullptr;
    SparrowAtlas combined;
    std::set<std::string> names;
    for (size_t page = 0; page < character.resolvedAtlases.size(); ++page) {
        const SparrowAtlas* atlas = get(character.resolvedAtlases[page]);
        if (!atlas || combined.frames.size() + atlas->frames.size() > 32768) { m_failed[key] = true; return nullptr; }
        for (const AtlasFrame& original : atlas->frames) {
            if (!names.insert(original.name).second) { m_failed[key] = true; return nullptr; }
            AtlasFrame frame = original;
            frame.sourceImage = character.resolvedImages[page];
            combined.byPrefix[SparrowAtlas::stripFrameNumber(frame.name)].push_back(combined.frames.size());
            combined.frames.push_back(std::move(frame));
        }
    }
    return &m_cache.emplace(std::move(key), std::move(combined)).first->second;
}


// -----------------------------------------------------------------------------
// StageAnimator
// -----------------------------------------------------------------------------
void rotateAround(DrawCmd& c, float degrees, float pivotX, float pivotY);

namespace {

bool isBeatType(const std::string& t) {
    return t == "beat" || t == "onbeat";
}

int findAnim(const std::vector<AnimationDef>& anims, const char* name) {
    for (size_t i = 0; i < anims.size(); ++i) if (anims[i].name == name) return (int)i;
    return -1;
}

std::vector<size_t> framesOf(const SparrowAtlas& atlas, const AnimationDef& anim) {
    if (!anim.allAtlasFrames) return atlas.framesFor(anim.atlasPrefix);
    std::vector<size_t> frames(atlas.frames.size());
    std::iota(frames.begin(), frames.end(), size_t{0});
    return frames;
}

}  // namespace

void rotateAround(DrawCmd& c, float degrees, float pivotX, float pivotY) {
    if (std::abs(degrees) < 0.0001f) return;
    constexpr float kPi = 3.14159265358979323846f;
    const float radians = degrees * kPi / 180.0f;
    const float cs = std::cos(radians), sn = std::sin(radians);

    const float dx = c.x - pivotX, dy = c.y - pivotY;
    c.x = pivotX + cs * dx - sn * dy;
    c.y = pivotY + sn * dx + cs * dy;

    const float ma = c.ma, mb = c.mb, mc = c.mc, md = c.md;
    c.ma = cs * ma - sn * mb;
    c.mb = sn * ma + cs * mb;
    c.mc = cs * mc - sn * md;
    c.md = sn * mc + cs * md;
}

void parseSolidColor(std::string raw, float& r, float& g, float& b, float& a) {
    raw.erase(std::remove_if(raw.begin(), raw.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), raw.end());
    bool explicitHex = false;
    if (!raw.empty() && raw.front() == '#') {
        explicitHex = true;
        raw.erase(raw.begin());
    }
    if (raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X')) {
        explicitHex = true;
        raw.erase(0, 2);
    }
    if (raw.size() == 3) {
        std::string expanded;
        for (char ch : raw) { expanded += ch; expanded += ch; }
        raw = std::move(expanded);
    }
    if (raw.empty()) return;

    // Los colores de Haxe son Int ARGB de 32 bits. Al cruzar JSON, valores
    // como 0xFF000000 llegan como el decimal con signo "-16777216". La ruta
    // anterior lo leia en base 16 y fabricaba 0xE9888DEA: un overlay negro se
    // convertia en un rectangulo azul/violeta sobre toda la preview.
    //
    // Las formas de seis digitos y los prefijos #/0x siguen siendo texto hex
    // de assets/XML. El resto de cadenas compuestas solo por digitos conserva
    // la semantica numerica ARGB que tenia el valor al salir de Haxe.
    const bool signedDecimal = raw.front() == '-' || raw.front() == '+';
    const size_t digitOffset = signedDecimal ? 1u : 0u;
    const bool decimalDigits = digitOffset < raw.size() &&
        std::all_of(raw.begin() + static_cast<std::ptrdiff_t>(digitOffset), raw.end(),
                    [](unsigned char ch) { return std::isdigit(ch) != 0; });
    const bool decimalArgb = !explicitHex && decimalDigits &&
                             (signedDecimal || (raw.size() != 3 && raw.size() != 6));
    if (decimalArgb) {
        char* decimalEnd = nullptr;
        const long long signedValue = std::strtoll(raw.c_str(), &decimalEnd, 10);
        if (decimalEnd && *decimalEnd == '\0') {
            const std::uint32_t value = static_cast<std::uint32_t>(signedValue);
            a *= static_cast<float>((value >> 24) & 0xFFu) / 255.0f;
            r = static_cast<float>((value >> 16) & 0xFFu) / 255.0f;
            g = static_cast<float>((value >> 8) & 0xFFu) / 255.0f;
            b = static_cast<float>(value & 0xFFu) / 255.0f;
            return;
        }
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(raw.c_str(), &end, 16);
    if (!end || *end != '\0') return;
    if (raw.size() > 6) {
        a *= static_cast<float>((value >> 24) & 0xFFu) / 255.0f;
        r = static_cast<float>((value >> 16) & 0xFFu) / 255.0f;
        g = static_cast<float>((value >> 8) & 0xFFu) / 255.0f;
        b = static_cast<float>(value & 0xFFu) / 255.0f;
    } else {
        r = static_cast<float>((value >> 16) & 0xFFu) / 255.0f;
        g = static_cast<float>((value >> 8) & 0xFFu) / 255.0f;
        b = static_cast<float>(value & 0xFFu) / 255.0f;
    }
}

namespace {

const PropertyValue* runtimeProperty(const StageObject& object,
                                     const char* name) {
    const auto found = object.properties.find(name);
    return found == object.properties.end() ? nullptr : &found->second;
}

// Un color de Haxe es un Int ARGB de 32 bits, pero al cruzar JSON puede llegar
// como decimal con signo o como cadena hex de un asset. `parseSolidColor` ya
// resuelve todas esas formas; esto solo recompone el entero, que es lo que
// necesita quien hornea el color dentro de un bitmap.
unsigned int runtimeArgb(const StageObject& object, const char* name,
                         unsigned int fallback) {
    const PropertyValue* property = runtimeProperty(object, name);
    if (!property || property->raw.empty()) return fallback;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    parseSolidColor(property->raw, r, g, b, a);
    const auto channel = [](float value) {
        return static_cast<unsigned int>(
            std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return (channel(a) << 24) | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

void applyRuntimeTint(const StageObject& object, DrawCmd& command) {
    const auto multiplyColor = [&](const PropertyValue* property) {
        if (!property) return;
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        parseSolidColor(property->raw, r, g, b, a);
        command.colorR *= r;
        command.colorG *= g;
        command.colorB *= b;
        command.alpha *= a;
    };
    multiplyColor(runtimeProperty(object, "runtime.color"));
    multiplyColor(runtimeProperty(object, "runtime.colorTransform.color"));
    if (const auto* value = runtimeProperty(
            object, "runtime.colorTransform.redMultiplier"))
        command.colorR *= value->asFloat();
    if (const auto* value = runtimeProperty(
            object, "runtime.colorTransform.greenMultiplier"))
        command.colorG *= value->asFloat();
    if (const auto* value = runtimeProperty(
            object, "runtime.colorTransform.blueMultiplier"))
        command.colorB *= value->asFloat();
    if (const auto* value = runtimeProperty(
            object, "runtime.colorTransform.alphaMultiplier"))
        command.alpha *= value->asFloat();
}

void applyRuntimeClip(const StageObject& object, DrawCmd& command,
                      bool solidTexture) {
    const auto* enabled = runtimeProperty(object, "runtime.clip.enabled");
    if (!enabled || !enabled->asBool()) return;
    const auto number = [&](const char* name, float fallback) {
        const auto* value = runtimeProperty(object, name);
        return value ? value->asFloat() : fallback;
    };
    const float logicalW = object.width > 0.0f ? object.width : command.sw;
    const float logicalH = object.height > 0.0f ? object.height : command.sh;
    const float left = std::clamp(number("runtime.clip.x", 0.0f), 0.0f, logicalW);
    const float top = std::clamp(number("runtime.clip.y", 0.0f), 0.0f, logicalH);
    const float right = std::clamp(
        left + std::max(0.0f, number("runtime.clip.width", logicalW)),
        left, logicalW);
    const float bottom = std::clamp(
        top + std::max(0.0f, number("runtime.clip.height", logicalH)),
        top, logicalH);
    if (right <= left || bottom <= top) {
        command.visible = false;
        command.w = command.h = 0.0f;
        return;
    }

    const float scaleX = logicalW > 0.0f ? command.w / logicalW : 1.0f;
    const float scaleY = logicalH > 0.0f ? command.h / logicalH : 1.0f;
    command.x += left * scaleX;
    command.y += top * scaleY;
    command.w = (right - left) * scaleX;
    command.h = (bottom - top) * scaleY;
    if (!solidTexture) {
        const float sourceScaleX = logicalW > 0.0f ? command.sw / logicalW : 1.0f;
        const float sourceScaleY = logicalH > 0.0f ? command.sh / logicalH : 1.0f;
        command.sx += left * sourceScaleX;
        command.sy += top * sourceScaleY;
        command.sw = (right - left) * sourceScaleX;
        command.sh = (bottom - top) * sourceScaleY;
    }
}

bool isUsableAnimateElement(const AnimateElement& element) {
    if (!element.sprite || element.sprite->w <= 0 || element.sprite->h <= 0)
        return false;

    // Animate exports sometimes park hidden placeholders around +/-1.0e8.
    // FlxAnimate clips those pieces, but treating them as real geometry makes
    // auto-fit and camera bounds encompass hundreds of millions of pixels.
    // A legitimate symbol-local FNF coordinate remains vastly below this.
    constexpr float kMaxLocalCoordinate = 1000000.0f;
    const float w = static_cast<float>(element.sprite->w);
    const float h = static_cast<float>(element.sprite->h);
    const float xs[4] = {
        element.tx,
        element.tx + element.a * w,
        element.tx + element.c * h,
        element.tx + element.a * w + element.c * h
    };
    const float ys[4] = {
        element.ty,
        element.ty + element.b * w,
        element.ty + element.d * h,
        element.ty + element.b * w + element.d * h
    };
    for (int corner = 0; corner < 4; ++corner) {
        if (!std::isfinite(xs[corner]) || !std::isfinite(ys[corner]) ||
            std::abs(xs[corner]) > kMaxLocalCoordinate ||
            std::abs(ys[corner]) > kMaxLocalCoordinate)
            return false;
    }
    return true;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

struct CharacterPlacement {
    const UniversalCharacter* character = nullptr;
    StageObject::Kind role = StageObject::Kind::Unknown;
    float positionIndex = 0.0f;
};

bool hasCustomMarker(const UniversalStage& stage, const std::string& characterId) {
    const std::string wanted = lowerAscii(characterId);
    for (const StageObject& object : stage.objects)
        if (object.kind == StageObject::Kind::Character &&
            lowerAscii(object.name) == wanted)
            return true;
    return false;
}

std::vector<CharacterPlacement> placementsForMarker(const UniversalStage& stage,
                                                      const StageObject& marker,
                                                      const CharacterBinding& chars) {
    std::vector<CharacterPlacement> result;
    const StageObject::Kind roles[] = {StageObject::Kind::Opponent,
                                       StageObject::Kind::Player,
                                       StageObject::Kind::Girlfriend};
    for (const StageObject::Kind role : roles) {
        const auto characters = chars.allForKind(role);
        for (size_t index = 0; index < characters.size(); ++index) {
            const UniversalCharacter* character = characters[index];
            if (!character) continue;
            const bool custom = hasCustomMarker(stage, character->id);
            const bool matches = marker.kind == StageObject::Kind::Character
                ? lowerAscii(marker.name) == lowerAscii(character->id)
                : marker.kind == role && !custom;
            if (!matches) continue;
            result.push_back({character, role,
                              chars.positionIndexForKind(role, index)});
        }
    }
    return result;
}

const UniversalCharacter* primaryCharacterForMarker(const UniversalStage& stage,
                                                     const StageObject& marker,
                                                     const CharacterBinding& chars) {
    const auto placements = placementsForMarker(stage, marker, chars);
    return placements.empty() ? nullptr : placements.front().character;
}

}  // namespace

// Un frame rotado esta guardado GIRADO en el atlas: Sparrow empaqueta el
// sub-rect a 90 grados cuando le cuadra mejor, y quien lo dibuja tiene que
// des-rotarlo. Flixel lo hace con `ANGLE_NEG_90` (`FlxAtlasFrames.hx:276`) y
// aqui con la matriz 2x2 que `DrawCmd` ya tenia: el rect local sigue siendo el
// del atlas y el giro lo pone de pie. Hasta ahora se dibujaban tumbados y solo
// se avisaba (FML-2204): 96 de los XML de Dustin los traen.
//
// Se llama con x/y/w/h ya puestos y ANTES de componer el angulo del objeto,
// porque el giro del frame es intrinseco al sprite y va primero.
void drawCmdCorners(const DrawCmd& cmd, float outX[4], float outY[4], float outW[4]) {
    if (cmd.quad) {
        for (int k = 0; k < 4; ++k) {
            outX[k] = cmd.qx[k];
            outY[k] = cmd.qy[k];
            outW[k] = cmd.qw[k] > 0.0f ? cmd.qw[k] : 1.0f;
        }
        return;
    }
    // El rectangulo local recorrido en el mismo orden que las esquinas del
    // quad, pasado por la parte 2x2 de la transformacion local.
    const float uu[4] = {0.0f, cmd.w, cmd.w, 0.0f};
    const float vv[4] = {0.0f, 0.0f, cmd.h, cmd.h};
    for (int k = 0; k < 4; ++k) {
        outX[k] = cmd.x + cmd.ma * uu[k] + cmd.mc * vv[k];
        outY[k] = cmd.y + cmd.mb * uu[k] + cmd.md * vv[k];
        outW[k] = 1.0f;   // sin perspectiva: la interpolacion lineal ya es exacta
    }
}

void unrotateFrame(DrawCmd& c, const AtlasFrame& f) {
    if (!f.rotated) return;
    // (u,v) -> (v, -u): el eje X del atlas pasa a subir y el Y a ir hacia la
    // derecha. Es el giro que deshace el empaquetado.
    c.ma = 0.0f; c.mb = -1.0f; c.mc = 1.0f; c.md = 0.0f;
    // Con ese giro el quad queda POR ENCIMA de su base —su esquina de arriba a
    // la izquierda cae en y - w—, asi que la base baja otro tanto para dejarlo
    // donde el llamante creia ponerlo.
    c.y += c.w;
    // El volteo se aplica sobre las UV, o sea sobre el rect del ATLAS. Con el
    // giro puesto, voltear el eje X del atlas se ve como voltear en Y: se
    // cruzan las dos banderas.
    const bool flipX = c.flipX;
    c.flipX = c.flipY;
    c.flipY = flipX;
}

// Lo que OCUPA un frame al dibujarse. Con `rotated`, ancho y alto van
// cambiados respecto al rect del atlas.
float frameDrawW(const AtlasFrame& f) {
    return static_cast<float>(f.rotated ? f.h : f.w);
}
float frameDrawH(const AtlasFrame& f) {
    return static_cast<float>(f.rotated ? f.w : f.h);
}

std::vector<int> danceAnimsOf(const UniversalCharacter& ch, const std::string& suffix,
                              const std::string& idleAnimation = {}) {
    std::vector<int> out;
    if (!idleAnimation.empty()) {
        const int selected = findAnim(ch.anims, idleAnimation.c_str());
        if (selected >= 0) { out.push_back(selected); return out; }
    }
    const std::string left = "danceLeft" + suffix;
    const std::string right = "danceRight" + suffix;
    int l = findAnim(ch.anims, left.c_str());
    int r = findAnim(ch.anims, right.c_str());
    if ((l < 0 || r < 0) && !suffix.empty()) {
        l = findAnim(ch.anims, "danceLeft");
        r = findAnim(ch.anims, "danceRight");
    }
    if (l >= 0 && r >= 0) { out.push_back(l); out.push_back(r); return out; }
    const std::string idleName = "idle" + suffix;
    int idle = findAnim(ch.anims, idleName.c_str());
    if (idle < 0 && !suffix.empty()) idle = findAnim(ch.anims, "idle");
    if (idle >= 0) out.push_back(idle);
    else if (!ch.anims.empty()) out.push_back(0);
    return out;
}

ObjectAnimState initialAnimState(const StageObject& object) {
    ObjectAnimState state;
    state.animIndex = object.anims.empty() || object.type == "none" ? -1 : 0;
    if (object.kind == StageObject::Kind::Player ||
        object.kind == StageObject::Kind::Opponent ||
        object.kind == StageObject::Kind::Girlfriend ||
        object.kind == StageObject::Kind::Character)
        state.animIndex = 0;
    return state;
}

bool startCharacterDance(ObjectAnimState& state,
                         const UniversalCharacter& character,
                         double clockMs) {
    const auto dance = danceAnimsOf(character, state.idleSuffix, state.idleAnimation);
    if (dance.empty()) return false;
    state.animIndex = dance[static_cast<size_t>(state.countedBeat) % dance.size()];
    state.countedBeat = (state.countedBeat + 1) % static_cast<int>(dance.size());
    state.startMs = clockMs;
    state.frame = 0;
    state.singUntilBeat = -1.0;
    state.context = CharacterAnimContext::Dance;
    return true;
}

void StageAnimator::resync(double beat, double clockMs) {
    m_beat = beat;
    m_timeMs = clockMs;
    // Se vuelve a procesar el beat actual. Asi danceLeft/danceRight queda
    // determinado por la posicion de la cancion incluso despues de un seek.
    m_lastBeat = static_cast<int>(std::floor(beat)) - 1;
    for (auto& st : m_states) {
        st.singUntilBeat = -1.0;   // el canto pendiente ya no aplica
        // Los loops de stage nacen en t=0 y deben dar el mismo frame para una
        // misma posicion incluso despues de seek. Los bailes/personajes se
        // reinician abajo al procesar el beat actual.
        st.startMs = 0.0;
        st.frame = 0;
        st.countedBeat = std::max(0, static_cast<int>(std::floor(beat)));
        st.manual = false;
        st.idleSuffix.clear();
        st.context = CharacterAnimContext::Dance;
    }
}

void StageAnimator::reset(const UniversalStage& stage) {
    m_stageId = stage.id;
    m_states.clear();
    m_objectKinds.clear();
    m_objectNames.clear();
    m_states.reserve(stage.objects.size());
    m_objectKinds.reserve(stage.objects.size());
    m_objectNames.reserve(stage.objects.size());
    for (const StageObject& object : stage.objects) {
        m_states.push_back(initialAnimState(object));
        m_objectKinds.push_back(object.kind);
        m_objectNames.push_back(object.name);
    }
    m_timeMs = 0.0;
    m_beat = 0.0;
    m_lastBeat = -1;
}

bool StageAnimator::matches(const UniversalStage& stage) const {
    if (m_stageId != stage.id || m_states.size() != stage.objects.size() ||
        m_objectKinds.size() != stage.objects.size() ||
        m_objectNames.size() != stage.objects.size())
        return false;
    for (size_t i = 0; i < stage.objects.size(); ++i)
        if (m_objectKinds[i] != stage.objects[i].kind ||
            m_objectNames[i] != stage.objects[i].name)
            return false;
    return true;
}

void StageAnimator::rebind(const UniversalStage& stage) {
    // Un stage distinto no comparte identidad aunque tenga marcadores con los
    // mismos nombres. En ese caso corresponde un arranque limpio.
    if (m_stageId != stage.id || m_states.size() != m_objectKinds.size() ||
        m_states.size() != m_objectNames.size()) {
        reset(stage);
        return;
    }

    std::vector<ObjectAnimState> rebound;
    std::vector<bool> claimed(m_states.size(), false);
    rebound.reserve(stage.objects.size());
    for (const StageObject& object : stage.objects) {
        size_t match = m_states.size();
        for (size_t old = 0; old < m_states.size(); ++old) {
            if (claimed[old] || m_objectKinds[old] != object.kind ||
                m_objectNames[old] != object.name) continue;
            match = old;
            break;
        }
        if (match < m_states.size()) {
            claimed[match] = true;
            rebound.push_back(m_states[match]);
        } else {
            rebound.push_back(initialAnimState(object));
        }
    }
    m_states = std::move(rebound);
    m_objectKinds.clear();
    m_objectNames.clear();
    m_objectKinds.reserve(stage.objects.size());
    m_objectNames.reserve(stage.objects.size());
    for (const StageObject& object : stage.objects) {
        m_objectKinds.push_back(object.kind);
        m_objectNames.push_back(object.name);
    }
}

int StageAnimator::objectIndexOfKind(StageObject::Kind kind) const {
    for (size_t i = 0; i < m_objectKinds.size(); ++i)
        if (m_objectKinds[i] == kind) return static_cast<int>(i);
    return -1;
}

int StageAnimator::objectIndexNamed(const std::string& name,
                                    StageObject::Kind kind) const {
    for (size_t i = 0; i < m_objectNames.size(); ++i) {
        if (m_objectNames[i] != name) continue;
        if (kind != StageObject::Kind::Unknown && m_objectKinds[i] != kind) continue;
        return static_cast<int>(i);
    }
    return -1;
}

void StageAnimator::rewind() {
    m_timeMs = 0.0;
    m_beat = 0.0;
    m_lastBeat = -1;
    for (auto& st : m_states) { st.startMs = 0.0; st.frame = 0; st.countedBeat = 0;
                                st.singUntilBeat = -1.0; }
}

const std::vector<int>* StageAnimator::singPlacementsOf(size_t i) const {
    return i < m_states.size() ? &m_states[i].singPlacements : nullptr;
}

void StageAnimator::setSingPlacements(size_t i, std::vector<int> placements) {
    if (i >= m_states.size()) return;
    m_states[i].singPlacements = std::move(placements);
}

int StageAnimator::animIndexOf(size_t i) const {
    return i < m_states.size() ? m_states[i].animIndex : -1;
}

int StageAnimator::animIndexOf(size_t i, const StageObject& object) const {
    const int index = animIndexOf(i);
    if (i >= m_states.size()) return index;
    const std::string& name = m_states[i].animName;
    if (name.empty()) return index;
    // Si el indice sigue apuntando a la misma animacion no hay nada que
    // resolver. Solo cuando la lista cambio bajo los pies se busca de nuevo.
    if (index >= 0 && index < static_cast<int>(object.anims.size()) &&
        object.anims[static_cast<size_t>(index)].name == name)
        return index;
    for (size_t k = 0; k < object.anims.size(); ++k)
        if (object.anims[k].name == name) return static_cast<int>(k);
    return index;
}

int StageAnimator::frameOf(size_t i) const {
    return i < m_states.size() ? m_states[i].frame : 0;
}

void StageAnimator::play(size_t i, int animIndex) {
    if (i >= m_states.size()) return;
    m_states[i].animIndex = animIndex;
    m_states[i].animName.clear();
    m_states[i].startMs = m_timeMs;
    m_states[i].frame = 0;
    m_states[i].manual = true;
    m_states[i].context = CharacterAnimContext::Lock;
    m_states[i].singUntilBeat = -1.0;
}

void StageAnimator::release(size_t i) {
    if (i >= m_states.size()) return;
    m_states[i].manual = false;
    m_states[i].startMs = m_timeMs;
    m_states[i].frame = 0;
    m_states[i].context = CharacterAnimContext::Dance;
    m_states[i].singUntilBeat = -1.0;
}

bool StageAnimator::isManual(size_t i) const {
    return i < m_states.size() && m_states[i].manual;
}

bool StageAnimator::sing(size_t i, const UniversalCharacter& ch,
                         const std::string& animName, double holdBeats,
                         double elapsedInAnimationMs) {
    if (i >= m_states.size()) return false;
    for (size_t k = 0; k < ch.anims.size(); ++k) {
        if (ch.anims[k].name != animName) continue;
        m_states[i].animIndex = static_cast<int>(k);
        m_states[i].startMs = m_timeMs - std::max(0.0, elapsedInAnimationMs);
        m_states[i].frame = 0;
        m_states[i].singUntilBeat = m_beat + holdBeats;
        m_states[i].manual = false;
        m_states[i].context = CharacterAnimContext::Sing;
        return true;
    }
    return false;
}

bool StageAnimator::playCharacterAnimation(
        size_t i, const UniversalCharacter& ch, const std::string& animName,
        bool force, CharacterAnimContext context, double holdBeats,
        double elapsedInAnimationMs) {
    if (i >= m_states.size()) return false;
    for (size_t k = 0; k < ch.anims.size(); ++k) {
        if (ch.anims[k].name != animName) continue;
        ObjectAnimState& state = m_states[i];
        // FlxAnimationController.play no reinicia el mismo clip sin Force,
        // pero Character.playAnim si actualiza su contexto igualmente.
        if (force || state.animIndex != static_cast<int>(k)) {
            state.animIndex = static_cast<int>(k);
            state.startMs = m_timeMs - std::max(0.0, elapsedInAnimationMs);
            state.frame = 0;
        }
        state.manual = false;
        state.context = context;
        state.singUntilBeat =
            (context == CharacterAnimContext::Sing ||
             context == CharacterAnimContext::Miss)
                ? m_beat + std::max(0.0, holdBeats)
                : -1.0;
        return true;
    }
    return false;
}

void StageAnimator::setIdleSuffix(size_t i, const std::string& suffix) {
    if (i < m_states.size()) m_states[i].idleSuffix = suffix;
}

void StageAnimator::setIdleAnimation(size_t i, const std::string& name) {
    if (i < m_states.size()) m_states[i].idleAnimation = name;
}

void StageAnimator::updateWithBeat(const UniversalStage& stage, const CharacterBinding& chars,
                                   AtlasStore& atlases, double clockMs, double beat,
                                   bool isPlaying) {
    if (!matches(stage)) rebind(stage);
    m_beat   = beat;
    m_timeMs = clockMs;   // el reloj es el del audio, no un acumulador propio
    stepAnimations(stage, chars, atlases, clockMs, isPlaying);
}

void StageAnimator::update(const UniversalStage& stage, const CharacterBinding& chars,
                           AtlasStore& atlases, float dtMs, float bpm) {
    if (!matches(stage)) rebind(stage);
    if (!m_playing) return;
    if (bpm <= 0.0f) bpm = 100.0f;

    m_timeMs += dtMs;
    const double msPerBeat = 60000.0 / bpm;
    m_beat = m_timeMs / msPerBeat;

    // El beat hit y el avance de fotogramas viven SOLO en stepAnimations,
    // compartidos con el modo de reloj externo. Tenerlos duplicados aqui era
    // pedir que las dos copias divergieran.
    stepAnimations(stage, chars, atlases, m_timeMs, true);
}

void StageAnimator::stepAnimations(const UniversalStage& stage, const CharacterBinding& chars,
                                   AtlasStore& atlases, double clockMs, bool advance) {
    // La lista de animaciones de un marcador de personaje ES la del personaje
    // que lo ocupa, y un cambio de personaje la sustituye entera. El indice
    // guardado pasa entonces a senalar otra animacion -a menudo la de fallo-,
    // asi que antes de nada se vuelve a buscar por nombre en la lista nueva.
    for (size_t i = 0; i < stage.objects.size() && i < m_states.size(); ++i)
        m_states[i].animIndex = animIndexOf(i, stage.objects[i]);

    // --- beat hit con el reloj externo ---
    const int curBeat = static_cast<int>(m_beat);
    if (curBeat != m_lastBeat) {
        for (int b = m_lastBeat + 1; b <= curBeat; ++b) {
            for (size_t i = 0; i < stage.objects.size(); ++i) {
                const StageObject& o = stage.objects[i];
                ObjectAnimState& st = m_states[i];
                if (st.manual) continue;
                if (st.context == CharacterAnimContext::Lock ||
                    st.context == CharacterAnimContext::None)
                    continue;
                if ((st.context == CharacterAnimContext::Sing ||
                     st.context == CharacterAnimContext::Miss) &&
                    st.singUntilBeat > m_beat)
                    continue;

                if (const UniversalCharacter* ch =
                        primaryCharacterForMarker(stage, o, chars)) {
                    // pico-speakers conserva la direccion elegida por su ultimo
                    // disparo y solo reinicia el idle correspondiente al bailar.
                    // Su script no usa danceLeft/danceRight ni un "idle" plano.
                    if (ch->id == "pico-speakers") {
                        if (st.animIndex >= 0 && st.animIndex < (int)ch->anims.size() &&
                            ch->anims[st.animIndex].name.rfind("idle", 0) == 0) {
                            st.startMs = clockMs;
                            st.frame = 0;
                        }
                        continue;
                    }
                    const auto dance = danceAnimsOf(*ch, st.idleSuffix, st.idleAnimation);
                    if (dance.empty()) continue;
                    const int interval = ch->hasBeatInterval
                        ? std::max(1, ch->beatInterval)
                        : (dance.size() == 2 ? 1 : 2);
                    if (b % interval != 0) continue;
                    startCharacterDance(st, *ch, clockMs);
                    continue;
                }
                if (!isBeatType(o.type) || o.anims.empty()) continue;
                if ((b + o.beatOffset) % o.beatInterval != 0) continue;
                const int n = static_cast<int>(o.anims.size());
                st.animIndex = st.countedBeat % n;
                st.countedBeat = (st.countedBeat + 1) % n;
                st.startMs = clockMs; st.frame = 0;
            }
        }
        m_lastBeat = curBeat;
    }

    // --- avance de frames ---
    for (size_t i = 0; i < stage.objects.size(); ++i) {
        const StageObject& o = stage.objects[i];
        ObjectAnimState& st = m_states[i];

        const UniversalCharacter* ch = primaryCharacterForMarker(stage, o, chars);
        const std::vector<AnimationDef>& anims = ch ? ch->anims : o.anims;
        const std::string& atlasPath = ch ? ch->resolvedAtlas : o.resolvedAtlas;

        // Character.update llama tryDance cada frame. SING y MISS vuelven al
        // baile en cuanto vence holdTime, aunque ese instante caiga entre dos
        // beats; limitarlo a beatHit acortaba o alargaba poses segun el BPM.
        if (ch && !st.manual &&
            (st.context == CharacterAnimContext::Sing ||
             st.context == CharacterAnimContext::Miss) &&
            st.singUntilBeat >= 0.0 && st.singUntilBeat <= m_beat)
            startCharacterDance(st, *ch, clockMs);

        if (st.animIndex < 0 || st.animIndex >= (int)anims.size()) continue;
        const AnimationDef& a = anims[st.animIndex];

        int total = a.resolvedFrames;
        if (total <= 0) {
            if (AtlasStore::isAnimatePath(atlasPath)) {
                const AnimateAtlas* aa = atlases.getAnimate(atlasPath);
                total = aa ? aa->frameCount(a.atlasPrefix) : 0;
            } else {
                const SparrowAtlas* atlas = ch ? atlases.getCharacter(*ch) : atlases.get(atlasPath);
                total = atlas ? static_cast<int>(framesOf(*atlas, a).size()) : 0;
            }
        }
        if (!a.indices.empty()) {
            // addByIndices ignora indices inexistentes. Contarlos igualmente
            // hacia que el animador avanzara por frames que el renderer habia
            // descartado y volviera intermitentemente al frame 0.
            int valid = 0;
            for (int index : a.indices)
                if (index >= 0 && index < total) ++valid;
            total = valid;
        }
        if (total <= 0) continue;

        const double fps = a.fps > 0 ? static_cast<double>(a.fps) : 24.0;
        if (!advance) continue;                    // congelado: no se toca el frame
        double elapsed = clockMs - st.startMs;
        if (elapsed < 0.0) { st.startMs = clockMs; elapsed = 0.0; }   // salto atras
        int f = static_cast<int>(elapsed * fps / 1000.0);
        if (a.loop) {
            f %= total;
        } else if (f >= total) {
            // data/characters/pico-speakers.hx: al terminar shootN vuelve a
            // idleN. Es la coreografia usada por Stress para disparar a los
            // Tankmen de fondo.
            if (ch && ch->id == "pico-speakers" && a.name.rfind("shoot", 0) == 0) {
                const std::string idleName = "idle" + a.name.substr(5);
                const int idleIndex = findAnim(anims, idleName.c_str());
                if (idleIndex >= 0) {
                    st.animIndex = idleIndex;
                    st.startMs = clockMs;
                    st.frame = 0;
                    continue;
                }
            }
            if (!ch && o.name == "bg" && a.name == "lightning") {
                const int idleIndex = findAnim(anims, "idle");
                if (idleIndex >= 0) {
                    st.animIndex = idleIndex;
                    st.startMs = clockMs;
                    st.frame = 0;
                    continue;
                }
            }
            const std::string loopName = a.name + "-loop";
            const int loopIndex = findAnim(anims, loopName.c_str());
            if (loopIndex >= 0 && loopIndex != st.animIndex) {
                st.animIndex = loopIndex;
                st.startMs = clockMs;
                st.frame = 0;
                continue;
            }
            // Contexto NONE: no baila mientras el clip siga vivo, pero vuelve
            // inmediatamente al terminar. LOCK conserva el ultimo frame;
            // DANCE espera su beat y SING/MISS dependen de holdTime.
            if (ch && !st.manual && st.context == CharacterAnimContext::None &&
                startCharacterDance(st, *ch, clockMs))
                continue;
            f = total - 1;
        }
        st.frame = f;
    }
    // Y al cerrar, el nombre queda anotado desde el indice ya definitivo: es
    // lo unico que sobrevive con sentido a que le cambien la lista debajo.
    for (size_t i = 0; i < stage.objects.size() && i < m_states.size(); ++i) {
        const int index = m_states[i].animIndex;
        m_states[i].animName =
            index >= 0 && index < static_cast<int>(stage.objects[i].anims.size())
                ? stage.objects[i].anims[static_cast<size_t>(index)].name
                : std::string();
    }
}

bool characterCameraPoint(const UniversalStage&   stage,
                          const CharacterBinding& chars,
                          AtlasStore&             atlases,
                          StageObject::Kind       kind,
                          Vec2&                   out) {
    const auto boundCharacters = chars.allForKind(kind);
    Vec2 sum{};
    int count = 0;
    for (size_t characterIndex = 0; characterIndex < boundCharacters.size();
         ++characterIndex) {
        const UniversalCharacter* ch = boundCharacters[characterIndex];
        if (!ch || ch->anims.empty()) continue;

        const StageObject* marker = nullptr;
        const std::string wanted = lowerAscii(ch->id);
        for (const StageObject& object : stage.objects)
            if (object.kind == StageObject::Kind::Character &&
                lowerAscii(object.name) == wanted) {
                marker = &object;
                break;
            }
        if (!marker)
            for (const StageObject& object : stage.objects)
                if (object.kind == kind) { marker = &object; break; }
        if (!marker) continue;

        float sourceW = 0.0f, sourceH = 0.0f;
        if (AtlasStore::isAnimatePath(ch->resolvedAtlas)) {
            const AnimateAtlas* atlas = atlases.getAnimate(ch->resolvedAtlas);
            if (!atlas) continue;
            std::vector<AnimateElement> elements;
            const AnimationDef& animation = ch->anims.front();
            const int frame = animation.indices.empty()
                ? 0 : std::max(0, animation.indices.front());
            atlas->flatten(animation.atlasPrefix, frame, elements);
            bool haveBounds = false;
            float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
            for (const AnimateElement& element : elements) {
                if (!isUsableAnimateElement(element)) continue;
                const float xs[4] = {
                    element.tx,
                    element.tx + element.a * element.sprite->w,
                    element.tx + element.c * element.sprite->h,
                    element.tx + element.a * element.sprite->w +
                        element.c * element.sprite->h
                };
                const float ys[4] = {
                    element.ty,
                    element.ty + element.b * element.sprite->w,
                    element.ty + element.d * element.sprite->h,
                    element.ty + element.b * element.sprite->w +
                        element.d * element.sprite->h
                };
                for (int corner = 0; corner < 4; ++corner) {
                    if (!haveBounds) {
                        minX = maxX = xs[corner]; minY = maxY = ys[corner];
                        haveBounds = true;
                    } else {
                        minX = std::min(minX, xs[corner]); maxX = std::max(maxX, xs[corner]);
                        minY = std::min(minY, ys[corner]); maxY = std::max(maxY, ys[corner]);
                    }
                }
            }
            if (!haveBounds) continue;
            sourceW = maxX - minX; sourceH = maxY - minY;
        } else {
            const SparrowAtlas* atlas = atlases.getCharacter(*ch);
            if (!atlas) continue;
            const auto frames = atlas->framesFor(ch->anims.front().atlasPrefix);
            if (frames.empty()) continue;
            const AtlasFrame& frame = atlas->frames[frames.front()];
            sourceW = static_cast<float>(frame.frameW > 0 ? frame.frameW : frame.w);
            sourceH = static_cast<float>(frame.frameH > 0 ? frame.frameH : frame.h);
        }

        const float positionIndex = chars.positionIndexForKind(kind, characterIndex);
        const float markerX = ch->hasRuntimeStagePosition
            ? ch->runtimeStagePosition.x
            : marker->position.x + positionIndex * marker->characterSpacing.x;
        const float markerY = ch->hasRuntimeStagePosition
            ? ch->runtimeStagePosition.y
            : marker->position.y + positionIndex * marker->characterSpacing.y;
        // PlayState no pasa directamente `strumLine.type == PLAYER` a
        // Character.isPlayer. Pasa Stage.isCharFlipped(...), que usa el `flip`
        // efectivo del marcador (incluidos marcadores con nombre de personaje).
        // Character.getCameraPosition() decide el -100/+150 con ese valor.
        const float side = marker->flipX ? -100.0f : 150.0f;
        const float midpointX = ch->centeredCamera ? sourceW * ch->scale * 0.5f : 0.0f;
        const float midpointY = ch->centeredCamera ? sourceH * ch->scale * 0.5f : 0.0f;
        sum.x += markerX + midpointX + side + ch->position.x +
                 ch->camOffset.x + marker->camOffset.x;
        sum.y += markerY + midpointY - 100.0f + ch->position.y +
                 ch->camOffset.y + marker->camOffset.y;
        ++count;
    }
    if (count == 0) return false;
    out.x = sum.x / static_cast<float>(count);
    out.y = sum.y / static_cast<float>(count);
    return true;
}

bool AtlasStore::invalidate(const std::string& virtualPath) {
    bool had = m_cache.erase(virtualPath) > 0;
    for (auto item = m_cache.begin(); item != m_cache.end();) {
        if (item->first.rfind("multi:", 0) == 0) { item = m_cache.erase(item); had = true; }
        else ++item;
    }
    had = (m_animate.erase(virtualPath) > 0) || had;
    m_failed.erase(virtualPath);
    for (auto item = m_failed.begin(); item != m_failed.end();) {
        if (item->first.rfind("multi:", 0) == 0) item = m_failed.erase(item);
        else ++item;
    }
    // Un spritemap tocado invalida su Animation.json hermano.
    const size_t slash = virtualPath.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string sib = virtualPath.substr(0, slash) + "/Animation.json";
        had = (m_animate.erase(sib) > 0) || had;
        m_failed.erase(sib);
    }
    return had;
}

bool AtlasStore::isAnimatePath(const std::string& p) {
    static const std::string tail = "Animation.json";
    return p.size() >= tail.size() &&
           p.compare(p.size() - tail.size(), tail.size(), tail) == 0;
}

const AnimateAtlas* AtlasStore::getAnimate(const std::string& virtualPath) {
    if (virtualPath.empty() || !readText) return nullptr;
    auto it = m_animate.find(virtualPath);
    if (it != m_animate.end()) return &it->second;
    if (m_failed.count(virtualPath)) return nullptr;

    const size_t slash = virtualPath.find_last_of('/');
    const std::string dir = (slash == std::string::npos) ? std::string()
                                                         : virtualPath.substr(0, slash);
    // El spritemap suele ser spritemap1.json; algunos exportan spritemap.json.
    std::vector<std::pair<std::string, std::string>> pages;
    for (int page = 1; page <= 8; ++page) {
        const std::string base = dir + "/spritemap" + std::to_string(page);
        const std::string data = readText(base + ".json");
        if (!data.empty()) pages.emplace_back(base + ".png", data);
    }
    if (pages.empty()) {
        const std::string data = readText(dir + "/spritemap.json");
        if (!data.empty()) pages.emplace_back(dir + "/spritemap.png", data);
    }
    const std::string anim = readText(virtualPath);
    if (pages.empty() || anim.empty()) { m_failed[virtualPath] = true; return nullptr; }

    DiagnosticSink local;
    DiagnosticSink& s = sink ? *sink : local;
    AnimateAtlas at;
    if (!at.loadPages(pages, anim, virtualPath, s)) { m_failed[virtualPath] = true; return nullptr; }
    return &m_animate.emplace(virtualPath, std::move(at)).first->second;
}

void buildStageRenderList(const UniversalStage&   stage,
                          const CharacterBinding& chars,
                          AtlasStore&             atlases,
                          IImageInfo&             images,
                          RenderList&             out,
                          const StageAnimator*    anim) {
    out.cmds.clear();
    out.camera.zoom = stage.zoom;

    // Punto de camara por defecto: el del rival, con la formula de
    // Character.getCameraPosition() -> midpoint + (isPlayer ? -100 : 150), y -100.
    // Sin personajes cargados todavia no hay midpoint, asi que se usa la
    // posicion declarada; cuando entren los personajes esto mejora solo.
    // Eje sin declarar: encuadre NEUTRO, no un valor inventado.
    //
    // El motor literalmente tiene camFollow = (0,0) hasta el primer evento de
    // camara, asi que no hay un "valor correcto" que copiar. Centrar en el rival
    // es lo que se vera al empezar la cancion, pero como encuadre de preview
    // deja fuera al resto del stage. Se ofrece como accion explicita
    // (opponentCameraPoint) en vez de aplicarse a escondidas.
    const Vec2 fallback{kGameWidth * 0.5f, kGameHeight * 0.5f};

    // Cada eje, por separado.
    out.camera.x = stage.hasStartCamPosX ? stage.startCamPos.x : fallback.x;
    out.camera.y = stage.hasStartCamPosY ? stage.startCamPos.y : fallback.y;

    for (size_t i = 0; i < stage.objects.size(); ++i) {
        const StageObject& o = stage.objects[i];

        // <box>/<solid> usa makeGraphic() en Codename. Se representa con la
        // textura blanca interna para conservar color, alpha, scroll, escala y
        // orden de render sin inventar un PNG temporal.
        if (o.kind == StageObject::Kind::Box) {
            if (o.width <= 0.0f || o.height <= 0.0f) continue;
            const auto shaderOnly = o.properties.find("runtime.shader-only-generated");
            const bool shaderOnlyGenerated = shaderOnly != o.properties.end() &&
                shaderOnly->second.asBool();
            DrawCmd c;
            c.objectIndex = static_cast<int>(i);
            c.debugName = o.name;
            c.runtimeObjectId = o.runtimeObjectId;
            c.runtimeShaderOnlyGenerated = shaderOnlyGenerated;
            // Hasta que main resuelva el shader no hay una representacion segura
            // de este 1x1: esconderlo evita una franja solida gigante.
            if (shaderOnlyGenerated) c.visible = false;
            c.texture = DrawCmd::SolidTexture;
            c.sx = c.sy = 0.0f;
            c.sw = c.sh = 1.0f;
            c.x = o.position.x;
            c.y = o.position.y;
            c.w = o.width * o.scale.x;
            c.h = o.height * o.scale.y;
            if (!o.updateHitbox) {
                c.x += (1.0f - o.scale.x) * o.width * 0.5f;
                c.y += (1.0f - o.scale.y) * o.height * 0.5f;
            }
            c.scrollX = o.scroll.x;
            c.scrollY = o.scroll.y;
            c.alpha = o.alpha;
            c.zoomFactor = o.zoomFactor;
            c.cameraIds = o.cameraIds;
            c.antialiasing = false;
            parseSolidColor(o.color, c.colorR, c.colorG, c.colorB, c.alpha);
            auto vis = o.properties.find("visible");
            if (vis != o.properties.end()) c.visible = vis->second.asBool();
            auto alp = o.properties.find("alpha");
            if (alp != o.properties.end()) c.alpha *= alp->second.asFloat();
            const float pivotW = o.updateHitbox ? c.w : o.width;
            const float pivotH = o.updateHitbox ? c.h : o.height;
            applyRuntimeTint(o, c);
            applyRuntimeClip(o, c, true);
            rotateAround(c, o.angle, o.position.x + pivotW * 0.5f,
                         o.position.y + pivotH * 0.5f);
            out.cmds.push_back(std::move(c));
            continue;
        }

        // --- Marcadores de personaje ---
        // Semantica de StageCharPos.prepareCharacter (Stage.hx:434):
        //   char.setPosition(x, y)      <- el marcador REEMPLAZA la posicion
        //   char.scale   *= scale
        //   char.alpha   *= alpha
        //   char.zoomFactor *= zoomFactor
        // y de Character.playAnim (Character.hx:304):
        //   offset.set((isPlayer != playerOffsets) ? globalOffset.x
        //                                          : -globalOffset.x,
        //              -globalOffset.y)
        // donde `globalOffset` es el x/y del XML del PERSONAJE, no del stage.
        // Flixel dibuja en (x - offset), asi que el signo queda invertido.
        //
        // El signo horizontal depende de DOS cosas, no solo del slot: el slot
        // runtime (isPlayer) y el atributo isPlayer="true" del XML del personaje
        // (playerOffsets). Coinciden -> -1; difieren -> +1. Las dos fuentes del
        // motor dicen lo mismo, asi que no es deriva de version:
        //   CodenameEngine-main/source/funkin/game/Character.hx:304
        //   dustin-build-master/source/funkin/game/Character.hx:282
        // El eje vertical no lleva signo: siempre suma globalOffset.y.
        const auto placements = placementsForMarker(stage, o, chars);
        if (!placements.empty()) {
            const UniversalCharacter* primary = placements.front().character;
            const int primaryAnimation = anim ? anim->animIndexOf(i) : 0;
            const int animationFrame = anim ? anim->frameOf(i) : 0;
            std::string animationName;
            if (primary && primaryAnimation >= 0 &&
                primaryAnimation < static_cast<int>(primary->anims.size()))
                animationName = primary->anims[static_cast<size_t>(primaryAnimation)].name;

            // Codename crea todos los personajes de la linea y llama
            // StageCharPos.prepareCharacter(char, indice). Comparten el golpe,
            // pero cada XML resuelve por nombre su propia animacion.
            for (size_t characterIndex = 0; characterIndex < placements.size();
                 ++characterIndex) {
                const CharacterPlacement& placement = placements[characterIndex];
                const UniversalCharacter* ch = placement.character;
                if (!ch || ch->resolvedImage.empty() || ch->anims.empty()) continue;
                if (!ch->runtimeVisible || ch->runtimeAlpha <= 0.0f) continue;

                StageObject marker = o;
                marker.alpha *= ch->runtimeAlpha;
                const float positionIndex = placement.positionIndex;
                if (ch->hasRuntimeStagePosition) {
                    marker.position = ch->runtimeStagePosition;
                } else {
                    marker.position.x += positionIndex * o.characterSpacing.x;
                    marker.position.y += positionIndex * o.characterSpacing.y;
                }

                // Enrutado: si el script dijo QUIENES cantan, los que no estan
                // en la lista no reproducen la animacion del marcador. No se
                // quedan congelados -eso pareceria un fallo-: siguen con la
                // suya, que es lo que hacen en el motor cuando la nota no es
                // suya.
                bool participates = true;
                if (anim) {
                    const std::vector<int>* routed =
                        anim->singPlacementsOf(i);
                    if (routed && !routed->empty())
                        participates = std::find(routed->begin(), routed->end(),
                            static_cast<int>(characterIndex)) != routed->end();
                }
                int ai = participates ? primaryAnimation : -1;
                if (participates && characterIndex > 0 && !animationName.empty())
                    ai = findAnim(ch->anims, animationName.c_str());
                if (!participates) {
                    // Su propia animacion de reposo, resuelta por nombre en SU
                    // lista: dos personajes del mismo marcador pueden llamarla
                    // distinto.
                    ai = findAnim(ch->anims, "idle");
                    if (ai < 0) ai = findAnim(ch->anims, "danceLeft");
                }
                if (ai < 0 || ai >= static_cast<int>(ch->anims.size())) ai = 0;
                const AnimationDef& ad = ch->anims[static_cast<size_t>(ai)];

                // ---- Atlas de Adobe Animate: un simbolo son N sprites con matriz ----
                if (AtlasStore::isAnimatePath(ch->resolvedAtlas)) {
                    const AnimateAtlas* aa = atlases.getAnimate(ch->resolvedAtlas);
                    if (!aa || !aa->hasSymbol(ad.atlasPrefix)) continue;
                    int fi = animationFrame;
                    if (!ad.indices.empty()) {
                        if (fi < 0 || fi >= static_cast<int>(ad.indices.size())) fi = 0;
                        fi = std::max(0, ad.indices[static_cast<size_t>(fi)]);
                    }
                    std::vector<AnimateElement> parts;
                    aa->flatten(ad.atlasPrefix, fi, parts);

                    const float sc = ch->scale;
                    const float scaleX = sc * marker.scale.x;
                    const float scaleY = sc * marker.scale.y;
                    const bool inPlayerSlot =
                        (placement.role == StageObject::Kind::Player);
                    // Character.hx:304 -> (isPlayer != playerOffsets) ? +1 : -1.
                    // `playerOffsets` es el isPlayer="true" del XML: dice para
                    // que lado se autoro el personaje. Un bf autorado para el
                    // lado del jugador y puesto en el slot del jugador NO lleva
                    // el offset espejado.
                    const float sign =
                        (inPlayerSlot != ch->playerOffsets) ? 1.0f : -1.0f;
                    const int tex = out.internTexture(ch->resolvedImage);
                    const float baseX = marker.position.x - ch->position.x * sign
                                      - ad.offset.x * scaleX;
                    const float baseY = marker.position.y + ch->position.y
                                      - ad.offset.y * scaleY;

                    for (const AnimateElement& e : parts) {
                        if (!isUsableAnimateElement(e)) continue;
                        DrawCmd c;
                        c.objectIndex = static_cast<int>(i);
                        c.debugName   = ch->id;
                        c.runtimeObjectId = ch->runtimeObjectId;
                        c.texture     = e.sprite->sourceImage.empty() ? tex : out.internTexture(e.sprite->sourceImage);
                        c.sx = (float)e.sprite->x; c.sy = (float)e.sprite->y;
                        c.sw = (float)e.sprite->w; c.sh = (float)e.sprite->h;
                        c.w  = c.sw;  c.h = c.sh;
                        c.ma = e.a * scaleX; c.mb = e.b * scaleY;
                        c.mc = e.c * scaleX; c.md = e.d * scaleY;
                        c.x  = baseX + e.tx * scaleX;
                        c.y  = baseY + e.ty * scaleY;
                        if (e.sprite->rotated) {
                            // Compose the packed-rectangle unrotation with the symbol matrix.
                            c.x += e.c * c.sw * scaleX; c.y += e.d * c.sw * scaleY;
                            c.ma = -e.c * scaleX; c.mb = -e.d * scaleY;
                            c.mc = e.a * scaleX; c.md = e.b * scaleY;
                        }
                        c.scrollX = marker.scroll.x; c.scrollY = marker.scroll.y;
                        c.alpha = marker.alpha; c.zoomFactor = marker.zoomFactor;
                        c.cameraIds = marker.cameraIds;
                        c.antialiasing = ch->antialiasing && marker.antialiasing;
                        out.cmds.push_back(std::move(c));
                    }
                    continue;
                }

                const SparrowAtlas* atlas = atlases.getCharacter(*ch);
                if (!atlas) continue;
                auto frames = framesOf(*atlas, ad);
                if (!ad.indices.empty() && !frames.empty()) {
                    std::vector<size_t> selected;
                    for (int index : ad.indices)
                        if (index >= 0 && index < static_cast<int>(frames.size()))
                            selected.push_back(frames[static_cast<size_t>(index)]);
                    if (!selected.empty()) frames.swap(selected);
                }
                if (frames.empty()) continue;

                int fi = animationFrame;
                if (fi < 0 || fi >= static_cast<int>(frames.size())) fi = 0;
                const AtlasFrame& f = atlas->frames[frames[static_cast<size_t>(fi)]];

                const bool inPlayerSlot = (placement.role == StageObject::Kind::Player);
                const float sc = ch->scale;
                const float scaleX = sc * marker.scale.x;
                const float scaleY = sc * marker.scale.y;
                // Mismo signo que arriba: Character.hx:304.
                const float sign =
                    (inPlayerSlot != ch->playerOffsets) ? 1.0f : -1.0f;
                // flipX si depende solo del slot (Character.hx:114).
                bool flip = ch->flipX;
                if (inPlayerSlot) flip = !flip;
                const bool finalFlipX = flip != ad.flipX;
                const bool finalFlipY = ad.flipY;
                const float fullW = static_cast<float>(f.frameW > 0 ? f.frameW : f.w);
                const float fullH = static_cast<float>(f.frameH > 0 ? f.frameH : f.h);
                // FlxSprite::resetHelpers centra origin una sola vez al cargar
                // las frames. set_frame solo llama resetFrameSize: no vuelve a
                // centrarlo. Usar fullW/fullH del frame actual hacia que un
                // atlas con sourceSize variable teletransportara al personaje
                // en cada frame (Spirit/Thorns). frameWidth/frameHeight son ese
                // origin estable observado por Character y HaxeFlixel.
                const float originX = (ch->frameWidth > 0
                    ? static_cast<float>(ch->frameWidth) : fullW) * 0.5f;
                const float originY = (ch->frameHeight > 0
                    ? static_cast<float>(ch->frameHeight) : fullH) * 0.5f;
                float trimX = static_cast<float>(-f.frameX);
                float trimY = static_cast<float>(-f.frameY);
                if (finalFlipX) trimX = fullW - trimX - static_cast<float>(f.w);
                if (finalFlipY) trimY = fullH - trimY - static_cast<float>(f.h);

                DrawCmd c;
                c.objectIndex = static_cast<int>(i);
                c.debugName   = ch->id;
                c.runtimeObjectId = ch->runtimeObjectId;
                c.texture     = out.internTexture(f.sourceImage.empty() ? ch->resolvedImage : f.sourceImage);
                c.sx = static_cast<float>(f.x); c.sy = static_cast<float>(f.y);
                c.sw = static_cast<float>(f.w); c.sh = static_cast<float>(f.h);
                c.x = marker.position.x - ch->position.x * sign
                    - ad.offset.x * scaleX
                    + (1.0f - scaleX) * originX + trimX * scaleX;
                c.y = marker.position.y + ch->position.y - ad.offset.y * scaleY
                    + (1.0f - scaleY) * originY + trimY * scaleY;
                c.w = c.sw * scaleX;
                c.h = c.sh * scaleY;
                c.scrollX = marker.scroll.x; c.scrollY = marker.scroll.y;
                c.alpha = marker.alpha; c.zoomFactor = marker.zoomFactor;
                c.cameraIds = marker.cameraIds;
                c.antialiasing = ch->antialiasing && marker.antialiasing;
                c.flipX = finalFlipX; c.flipY = finalFlipY;
                unrotateFrame(c, f);
                const float pivotX = marker.position.x - ch->position.x * sign
                                   - ad.offset.x * scaleX + originX;
                const float pivotY = marker.position.y + ch->position.y
                                   - ad.offset.y * scaleY
                                   + originY;
                rotateAround(c, marker.angle, pivotX, pivotY);
                out.cmds.push_back(std::move(c));
            }
            continue;
        }

        // Texto de runtime. Un FlxText no tiene asset, asi que la rama de
        // sprite lo descartaba por `resolvedImage` vacio: existia en la escena
        // y no se dibujaba nunca. Aqui no se compone -fml_runtime no tiene
        // tipografia-: la orden viaja con la misma geometria, camara y orden
        // que un sprite y la rasteriza la capa que si tiene glifos.
        if (const PropertyValue* label = runtimeProperty(o, "runtime.text")) {
            if (label->raw.empty()) continue;
            DrawCmd c;
            c.objectIndex = static_cast<int>(i);
            c.debugName = o.name;
            c.runtimeObjectId = o.runtimeObjectId;
            c.texture = DrawCmd::TextTexture;
            c.text = label->raw;
            c.sx = c.sy = 0.0f;
            c.sw = c.sh = 1.0f;
            c.x = o.position.x;
            c.y = o.position.y;
            // La caja de partida es la que publica el host. La app la corrige
            // con la medida real al componer el bitmap con la fuente del mod.
            c.w = o.width * o.scale.x;
            c.h = o.height * o.scale.y;
            c.scrollX = o.scroll.x;
            c.scrollY = o.scroll.y;
            c.alpha = o.alpha;
            c.zoomFactor = o.zoomFactor;
            c.cameraIds = o.cameraIds;
            c.antialiasing = o.antialiasing;
            if (const PropertyValue* font = runtimeProperty(o, "runtime.text-font"))
                c.textFont = font->raw;
            if (const PropertyValue* size = runtimeProperty(o, "runtime.text-size"))
                c.textSize = std::max(1.0f, size->asFloat());
            if (const PropertyValue* spacing =
                    runtimeProperty(o, "runtime.text-letter-spacing"))
                c.letterSpacing = spacing->asFloat();
            if (const PropertyValue* field = runtimeProperty(o, "runtime.text-field-width"))
                c.fieldWidth = std::max(0.0f, field->asFloat());
            if (const PropertyValue* wrap = runtimeProperty(o, "runtime.text-word-wrap"))
                c.wordWrap = wrap->asBool();
            if (const PropertyValue* align = runtimeProperty(o, "runtime.text-align")) {
                if (align->raw == "center") c.textAlign = 1;
                else if (align->raw == "right") c.textAlign = 2;
            }
            // El color del rotulo se hornea en el bitmap, como hace FlxText. Si
            // se pasara como tinte del sprite, un shader que lea los canales del
            // grafico -relleno y bordes en colores clave- recibiria el valor ya
            // multiplicado y el efecto saldria mal.
            c.textColor = runtimeArgb(o, "runtime.text-color", 0xFFFFFFFFu);
            // NONE y OUTLINE_FAST se distinguen: el primero no dibuja borde y
            // el segundo si, aunque el motor lo componga de otra forma.
            const PropertyValue* borderStyle =
                runtimeProperty(o, "runtime.text-border-style");
            const bool hasBorder = borderStyle && !borderStyle->raw.empty() &&
                                   borderStyle->raw != "none";
            if (hasBorder) {
                c.textBorder = 1.0f;
                c.textBorderStyle = borderStyle->raw == "shadow" ||
                                    borderStyle->raw == "shadow_xy"
                    ? DrawCmd::TextBorder::Shadow : DrawCmd::TextBorder::Outline;
                if (const PropertyValue* size =
                        runtimeProperty(o, "runtime.text-border-size"))
                    c.textBorder = std::max(0.0f, size->asFloat());
                if (const PropertyValue* quality =
                        runtimeProperty(o, "runtime.text-border-quality"))
                    c.textBorderQuality = std::clamp(quality->asFloat(), 0.1f, 4.0f);
                c.textBorderColor =
                    runtimeArgb(o, "runtime.text-border-color", 0xFF000000u);
            }
            if (const PropertyValue* count =
                    runtimeProperty(o, "runtime.text-outline-count")) {
                const int total = std::clamp(count->asInt(), 0, 16);
                c.textOutlineLayers.reserve(static_cast<size_t>(total));
                for (int index = 0; index < total; ++index) {
                    const std::string prefix = "runtime.text-outline." +
                        std::to_string(index);
                    const PropertyValue* size = runtimeProperty(
                        o, (prefix + ".size").c_str());
                    if (!size || size->asFloat() <= 0.0f) continue;
                    DrawCmd::TextOutlineLayer layer;
                    layer.size = size->asFloat();
                    layer.color = runtimeArgb(
                        o, (prefix + ".color").c_str(), 0xFF000000u);
                    c.textOutlineLayers.push_back(layer);
                }
            }
            if (const PropertyValue* replace =
                    runtimeProperty(o, "runtime.text-outline-replace"))
                c.textOutlineLayersReplaceBorder = replace->asBool();
            if (const PropertyValue* iterations =
                    runtimeProperty(o, "runtime.text-outline-iterations"))
                c.textOutlineIterations = std::clamp(iterations->asInt(), 0, 256);
            // La transformacion se termina en la app, cuando el bitmap ya tiene
            // tamano real; girar aqui la dejaria clavada al tamano estimado.
            c.textScaleX = o.scale.x;
            c.textScaleY = o.scale.y;
            c.textAngle  = o.angle;
            if (const auto vis = o.properties.find("visible"); vis != o.properties.end())
                c.visible = vis->second.asBool();
            if (const auto alp = o.properties.find("alpha"); alp != o.properties.end())
                c.alpha *= alp->second.asFloat();
            applyRuntimeTint(o, c);
            out.cmds.push_back(std::move(c));
            continue;
        }

        // Marcadores sin personaje enlazado conservan su hueco en el orden.
        if (o.kind != StageObject::Kind::Sprite) continue;
        if (o.resolvedImage.empty()) continue;

        if (AtlasStore::isAnimatePath(o.resolvedAtlas) && !o.anims.empty()) {
            const AnimateAtlas* atlas = atlases.getAnimate(o.resolvedAtlas);
            if (!atlas) continue;
            const int index = std::clamp(anim ? anim->animIndexOf(i) : 0, 0, static_cast<int>(o.anims.size()) - 1);
            const AnimationDef& definition = o.anims[static_cast<size_t>(index)];
            int frame = std::max(0, anim ? anim->frameOf(i) : 0);
            if (!definition.indices.empty()) frame = definition.indices[static_cast<size_t>(std::clamp(frame, 0, static_cast<int>(definition.indices.size()) - 1))];
            std::vector<AnimateElement> parts;
            atlas->flatten(definition.atlasPrefix, frame, parts);
            const AnimateBounds bounds = atlas->bounds(definition.atlasPrefix);
            const bool flipX = o.flipX != definition.flipX;
            const bool flipY = definition.flipY;
            const float scaleX = o.scale.x * (flipX ? -1.0f : 1.0f);
            const float scaleY = o.scale.y * (flipY ? -1.0f : 1.0f);
            const float baseX = o.position.x - (o.frameOffset.x + definition.offset.x) * o.scale.x + (flipX ? (bounds.left + bounds.right) * o.scale.x : 0);
            const float baseY = o.position.y - (o.frameOffset.y + definition.offset.y) * o.scale.y + (flipY ? (bounds.top + bounds.bottom) * o.scale.y : 0);
            for (const AnimateElement& part : parts) {
                if (!isUsableAnimateElement(part)) continue;
                DrawCmd command;
                command.objectIndex = static_cast<int>(i); command.debugName = o.name; command.runtimeObjectId = o.runtimeObjectId;
                command.texture = out.internTexture(part.sprite->sourceImage.empty() ? o.resolvedImage : part.sprite->sourceImage);
                command.sx = static_cast<float>(part.sprite->x); command.sy = static_cast<float>(part.sprite->y);
                command.sw = static_cast<float>(part.sprite->w); command.sh = static_cast<float>(part.sprite->h);
                command.w = command.sw; command.h = command.sh;
                command.ma = part.a * scaleX; command.mb = part.b * scaleY; command.mc = part.c * scaleX; command.md = part.d * scaleY;
                command.x = baseX + part.tx * scaleX; command.y = baseY + part.ty * scaleY;
                if (part.sprite->rotated) {
                    command.x += part.c * command.sw * scaleX; command.y += part.d * command.sw * scaleY;
                    command.ma = -part.c * scaleX; command.mb = -part.d * scaleY; command.mc = part.a * scaleX; command.md = part.b * scaleY;
                }
                command.scrollX = o.scroll.x; command.scrollY = o.scroll.y; command.alpha = o.alpha; command.zoomFactor = o.zoomFactor;
                command.cameraIds = o.cameraIds; command.antialiasing = o.antialiasing;
                if (const auto visible = o.properties.find("visible"); visible != o.properties.end()) command.visible = visible->second.asBool();
                if (const auto alpha = o.properties.find("alpha"); alpha != o.properties.end()) command.alpha = alpha->second.asFloat();
                applyRuntimeTint(o, command); applyRuntimeClip(o, command, false);
                rotateAround(command, o.angle, o.position.x + (bounds.left + bounds.right) * o.scale.x * 0.5f, o.position.y + (bounds.top + bounds.bottom) * o.scale.y * 0.5f);
                out.cmds.push_back(std::move(command));
            }
            continue;
        }

                DrawCmd c;
                c.objectIndex = static_cast<int>(i);
                c.debugName   = o.name;
                c.runtimeObjectId = o.runtimeObjectId;
        c.texture     = out.internTexture(o.resolvedImage);
        c.x           = o.position.x;
        c.y           = o.position.y;
        c.scrollX     = o.scroll.x;
        c.scrollY     = o.scroll.y;
        c.alpha       = o.alpha;
        c.zoomFactor  = o.zoomFactor;
        c.cameraIds   = o.cameraIds;
        c.antialiasing = o.antialiasing;
        c.flipX       = o.flipX;

        // La property <property name="visible" type="bool" value="false"/> del
        // XML manda sobre el valor por defecto.
        auto vis = o.properties.find("visible");
        if (vis != o.properties.end()) c.visible = vis->second.asBool();
        auto alp = o.properties.find("alpha");
        if (alp != o.properties.end()) c.alpha = alp->second.asFloat();

        const SparrowAtlas* atlas = atlases.get(o.resolvedAtlas);
        bool haveRect = false;
        // El frame que se acabo dibujando: hace falta despues, cuando ya estan
        // w/h, para des-rotarlo si venia girado en el atlas.
        const AtlasFrame* drawnFrame = nullptr;

        if (atlas && !o.anims.empty()) {
            // Animacion y frame actuales; sin animador, el frame 0 de la primera.
            int ai = anim ? anim->animIndexOf(i) : 0;
            if (ai < 0 || ai >= (int)o.anims.size()) ai = 0;
            const AnimationDef& ad = o.anims[ai];

            auto frames = framesOf(*atlas, ad);
            // `indices` selecciona un subconjunto del prefijo.
            if (!ad.indices.empty() && !frames.empty()) {
                std::vector<size_t> sel;
                sel.reserve(ad.indices.size());
                for (int idx : ad.indices)
                    if (idx >= 0 && idx < (int)frames.size()) sel.push_back(frames[idx]);
                if (!sel.empty()) frames.swap(sel);
            }
            if (!frames.empty()) {
                int fi = anim ? anim->frameOf(i) : 0;
                if (fi < 0 || fi >= (int)frames.size()) fi = 0;
                const AtlasFrame& f = atlas->frames[frames[fi]];
                c.sx = static_cast<float>(f.x);
                c.sy = static_cast<float>(f.y);
                c.sw = static_cast<float>(f.w);
                c.sh = static_cast<float>(f.h);
                const float fullW = static_cast<float>(f.frameW > 0 ? f.frameW : f.w);
                const float fullH = static_cast<float>(f.frameH > 0 ? f.frameH : f.h);
                float trimX = static_cast<float>(-f.frameX);
                float trimY = static_cast<float>(-f.frameY);
                c.flipX = o.flipX != ad.flipX;
                c.flipY = ad.flipY;
                if (c.flipX) trimX = fullW - trimX - c.sw;
                if (c.flipY) trimY = fullH - trimY - c.sh;
                // Sin updateHitbox, Flixel escala alrededor de `origin`, que se
                // congela con el frame 0 del ATLAS y no se recalcula al cambiar
                // de frame. Usar el tamano del frame actual movia el sprite
                // media diferencia por (1 - escala) en cada frame: invisible
                // con escala 1 y enorme en los stages pixel, que van a 6.
                const AtlasFrame& anchor = atlas->frames.front();
                const float anchorW = static_cast<float>(
                    anchor.frameW > 0 ? anchor.frameW : anchor.w);
                const float anchorH = static_cast<float>(
                    anchor.frameH > 0 ? anchor.frameH : anchor.h);
                if (!o.updateHitbox) {
                    c.x += (1.0f - o.scale.x) * anchorW * 0.5f;
                    c.y += (1.0f - o.scale.y) * anchorH * 0.5f;
                }
                c.x += trimX * o.scale.x;
                c.y += trimY * o.scale.y;
                drawnFrame = &f;
                haveRect = true;
            }
        } else if (atlas && !atlas->frames.empty()) {
            // Sin animaciones declaradas pero con atlas: primer frame.
            const AtlasFrame& f = atlas->frames.front();
            c.sx = static_cast<float>(f.x);
            c.sy = static_cast<float>(f.y);
            c.sw = static_cast<float>(f.w);
            c.sh = static_cast<float>(f.h);
            const float fullW = static_cast<float>(f.frameW > 0 ? f.frameW : f.w);
            const float fullH = static_cast<float>(f.frameH > 0 ? f.frameH : f.h);
            float trimX = static_cast<float>(-f.frameX);
            float trimY = static_cast<float>(-f.frameY);
            if (c.flipX) trimX = fullW - trimX - c.sw;
            if (!o.updateHitbox) {
                c.x += (1.0f - o.scale.x) * fullW * 0.5f;
                c.y += (1.0f - o.scale.y) * fullH * 0.5f;
            }
            c.x += trimX * o.scale.x;
            c.y += trimY * o.scale.y;
            drawnFrame = &f;
            haveRect = true;
        }

        if (!haveRect) {
            // Sin atlas: la imagen entera.
            const ImageInfo info = images.imageInfo(o.resolvedImage);
            if (!info.ok) continue;
            c.sx = 0.0f;
            c.sy = 0.0f;
            c.sw = static_cast<float>(info.w);
            c.sh = static_cast<float>(info.h);
            if (!o.updateHitbox) {
                c.x += (1.0f - o.scale.x) * c.sw * 0.5f;
                c.y += (1.0f - o.scale.y) * c.sh * 0.5f;
            }
        }

        // FlxSprite prepara la matriz con (x - frameOffset * scale). Mantener
        // este desplazamiento fuera de position evita que un script de baile
        // convierta el sway visual en teletransportes autorales.
        c.x -= o.frameOffset.x * o.scale.x;
        c.y -= o.frameOffset.y * o.scale.y;

        c.w = c.sw * o.scale.x;
        c.h = c.sh * o.scale.y;
        if (drawnFrame) unrotateFrame(c, *drawnFrame);
        // El pivote se mide sobre lo que se VE, y un frame rotado se ve con el
        // ancho y el alto cambiados.
        const float drawW = drawnFrame ? frameDrawW(*drawnFrame) : c.sw;
        const float drawH = drawnFrame ? frameDrawH(*drawnFrame) : c.sh;
        const float logicalW = o.updateHitbox ? drawW * o.scale.x : drawW;
        const float logicalH = o.updateHitbox ? drawH * o.scale.y : drawH;
        applyRuntimeTint(o, c);
        applyRuntimeClip(o, c, false);
        rotateAround(c, o.angle, o.position.x + logicalW * 0.5f,
                     o.position.y + logicalH * 0.5f);
        out.cmds.push_back(std::move(c));
    }
}

}  // namespace fml

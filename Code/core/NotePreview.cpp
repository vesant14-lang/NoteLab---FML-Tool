#include "NotePreview.hpp"

#include "../support/formats/SparrowAtlas.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace fml::notelab {
namespace {

constexpr float kGameW = 1280.0f;
constexpr float kGameH = 720.0f;
constexpr float kStrumMargin = 50.0f;     // strumLine y (PlayState.hx:792 en Codename)
constexpr float kPixelsPerMs = 0.45f;     // distancia = 0.45 * (strumTime - songPos) * velocidad

int frameIndexFor(const Animation& anim, size_t count, double elapsedMs) {
    if (count == 0) return -1;
    if (anim.fps <= 0.0f) return 0;
    const long long frame = static_cast<long long>(std::floor(std::max(0.0, elapsedMs) * anim.fps / 1000.0));
    if (anim.loop) return static_cast<int>(frame % static_cast<long long>(count));
    return static_cast<int>(std::min<long long>(frame, static_cast<long long>(count) - 1));
}

long trailingNumber(const std::string& name, size_t from) {
    if (from >= name.size() || !std::isdigit(static_cast<unsigned char>(name[from]))) return -1;
    long value = 0;
    while (from < name.size() && std::isdigit(static_cast<unsigned char>(name[from])) && value < 1000000)
        value = value * 10 + (name[from++] - '0');
    return value;
}

// Un fotograma centrado en (cx, cy), con el recorte y el giro del atlas como
// los aplica Scene (Scene.cpp, colocacion de personajes). El pixel art va sin
// suavizado, como lo dibujan los motores (`antialiasing = false`).
void pushFrame(RenderList& out, int texture, const AtlasFrame& f, float cx, float cy, float scale,
               float alpha, float stretchH = -1.0f, bool anchorTop = false, bool pixel = false) {
    const float fullW = f.frameW > 0 ? static_cast<float>(f.frameW) : frameDrawW(f);
    const float fullH = f.frameH > 0 ? static_cast<float>(f.frameH) : frameDrawH(f);
    DrawCmd c;
    c.texture = texture;
    c.sx = static_cast<float>(f.x);
    c.sy = static_cast<float>(f.y);
    c.sw = static_cast<float>(f.w);
    c.sh = static_cast<float>(f.h);
    const float left = cx - fullW * scale * 0.5f;
    const float top = anchorTop ? cy : cy - fullH * scale * 0.5f;
    c.x = left + static_cast<float>(-f.frameX) * scale;
    c.y = top + static_cast<float>(-f.frameY) * scale;
    c.w = static_cast<float>(f.w) * scale;
    c.h = static_cast<float>(f.h) * scale;
    if (stretchH >= 0.0f && !f.rotated) {
        c.y = top;
        c.h = stretchH;
    }
    c.alpha = alpha;
    c.antialiasing = !pixel;
    c.scrollX = 0.0f;
    c.scrollY = 0.0f;
    c.zoomFactor = 0.0f;
    c.pickable = false;
    unrotateFrame(c, f);
    out.cmds.push_back(std::move(c));
}

// Una columna de la tira, de la fila `v0` a la `v1` (en fraccion del alto).
void pushStripColumn(RenderList& out, int texture, const ImageInfo& image, int column, int columns,
                     float cx, float top, float height, float scale, float alpha, bool flipY,
                     float v0 = 0.0f, float v1 = 1.0f) {
    if (!image.ok || columns <= 0 || height <= 0.0f || v1 <= v0) return;
    const float colW = static_cast<float>(image.w) / static_cast<float>(columns);
    DrawCmd c;
    c.texture = texture;
    // Medio texel hacia dentro en horizontal: con filtro lineal, el borde
    // exacto mezclaria la columna de al lado.
    c.sx = colW * static_cast<float>(column) + 0.5f;
    c.sy = v0 * static_cast<float>(image.h);
    c.sw = std::max(1.0f, colW - 1.0f);
    c.sh = (v1 - v0) * static_cast<float>(image.h);
    c.w = colW * scale;
    c.h = height;
    c.x = cx - c.w * 0.5f;
    c.y = top;
    c.alpha = alpha;
    c.flipY = flipY;
    c.scrollX = 0.0f;
    c.scrollY = 0.0f;
    c.zoomFactor = 0.0f;
    c.pickable = false;
    out.cmds.push_back(std::move(c));
}

}  // namespace

std::vector<size_t> animationFrames(const SparrowAtlas& atlas, const Animation& anim) {
    std::vector<std::string> prefixes{anim.prefix};
    prefixes.insert(prefixes.end(), anim.alternatives.begin(), anim.alternatives.end());
    for (const std::string& prefix : prefixes) {
        if (prefix.empty()) continue;
        std::vector<size_t> found = atlas.framesFor(prefix);
        if (!anim.indices.empty()) {
            std::vector<size_t> picked;
            for (int index : anim.indices)
                for (size_t f : found)
                    if (trailingNumber(atlas.frames[f].name, prefix.size()) == index) {
                        picked.push_back(f);
                        break;
                    }
            found = std::move(picked);
        }
        if (!found.empty()) return found;
    }
    return {};
}

bool splashAnchor(Engine engine, float boxW, float boxH, float& x, float& y) {
    constexpr float lane = 160.0f * 0.7f;   // Note.swagWidth
    if (engine == Engine::Codename) {
        x = 0.0f;
        y = 0.0f;
        return true;
    }
    if (engine == Engine::Psych) {
        x = boxW * 0.5f - lane * 0.95f - 10.0f - lane * 0.5f;
        y = boxH * 0.5f - lane - 10.0f - lane * 0.5f;
        return true;
    }
    return false;
}

const PartBinding* findPart(const NoteStyle& style, Part part, int lane, int variant) {
    const PartBinding* fallback = nullptr;
    for (const PartBinding& binding : style.parts) {
        if (binding.part != part || binding.direction != lane) continue;
        if (binding.variant == variant) return &binding;
        if (!fallback) fallback = &binding;
    }
    return fallback;
}

float NotePreview::laneWidthOf(const NoteStyle&) {
    // Antes era 160 * la escala de las notas: un estilo pixel de V-Slice
    // (escala 6) ponia los carriles a 960 px y se salia de la pantalla.
    return 160.0f * 0.7f;
}

float NotePreview::laneCenterX(int strumLine, int lane, float laneWidth) {
    const float center = strumLine == 0 ? kGameW * 0.25f : kGameW * 0.75f;
    return center + (static_cast<float>(lane) - 1.5f) * laneWidth;
}

float NotePreview::strumCenterY(bool downscroll, float laneWidth) {
    return downscroll ? kGameH - kStrumMargin - laneWidth * 0.5f : kStrumMargin + laneWidth * 0.5f;
}

int NotePreview::splashVariants(const NoteStyle& style, int lane) {
    int variants = 0;
    for (const PartBinding& binding : style.parts)
        if (binding.part == Part::Splash && binding.direction == lane)
            variants = std::max(variants, binding.variant + 1);
    return variants;
}

const SparrowAtlas* NotePreview::gridAtlas(const Sheet& sheet) {
    if (sheet.image.empty() || sheet.columns <= 0 || sheet.rows <= 0) return nullptr;
    const std::string key = sheet.image + "|" + std::to_string(sheet.columns) + "x" + std::to_string(sheet.rows);
    const auto hit = m_grids.find(key);
    if (hit != m_grids.end()) return &hit->second;
    if (!m_images) return nullptr;
    const ImageInfo info = m_images->imageInfo(sheet.image);
    if (!info.ok) return nullptr;
    // Celdas de floor(w / columnas) por floor(h / filas), y tantas como caben
    // enteras, fila a fila (Psych Note.hx:329-333; FlxTileFrames.hx:296-305).
    const int cellW = info.w / sheet.columns, cellH = info.h / sheet.rows;
    SparrowAtlas atlas;
    if (cellW > 0 && cellH > 0)
        for (int y = 0; y + cellH <= info.h; y += cellH)
            for (int x = 0; x + cellW <= info.w; x += cellW) {
                AtlasFrame frame;
                frame.x = x;
                frame.y = y;
                frame.w = frame.frameW = cellW;
                frame.h = frame.frameH = cellH;
                atlas.frames.push_back(frame);
            }
    return &m_grids.emplace(key, std::move(atlas)).first->second;
}

const NotePreview::Frames& NotePreview::framesOf(AtlasStore& atlases, const Sheet& sheet,
                                                 const Animation& anim) {
    std::string key = (sheet.kind == SheetKind::Grid ? "grid:" + sheet.image : sheet.atlas) + "|" + anim.prefix;
    for (const std::string& alternative : anim.alternatives) key += "|" + alternative;
    key += "#";
    for (int index : anim.indices) key += std::to_string(index) + ",";
    const auto hit = m_frames.find(key);
    if (hit != m_frames.end()) return hit->second;
    Frames frames;
    if (sheet.kind == SheetKind::Grid) {
        // La animacion pide celdas por numero (Note.hx:387-389, StrumNote.hx:80-94).
        frames.atlas = gridAtlas(sheet);
        if (!frames.atlas) {
            static const Frames none;
            return none;   // sin el tamano todavia: se vuelve a probar
        }
        for (int cell : anim.indices.empty() ? std::vector<int>{0} : anim.indices)
            if (cell >= 0 && static_cast<size_t>(cell) < frames.atlas->frames.size())
                frames.indices.push_back(static_cast<size_t>(cell));
        return m_frames.emplace(std::move(key), std::move(frames)).first->second;
    }
    frames.atlas = sheet.atlas.empty() ? nullptr : atlases.get(sheet.atlas);
    if (frames.atlas) frames.indices = animationFrames(*frames.atlas, anim);
    return m_frames.emplace(std::move(key), std::move(frames)).first->second;
}

double NotePreview::animationLengthMs(const NoteStyle& style, AtlasStore& atlases, Part part, int lane,
                                      int variant) {
    const PartBinding* binding = findPart(style, part, lane, variant);
    if (!binding || binding->sheet < 0 || static_cast<size_t>(binding->sheet) >= style.sheets.size()) return 0.0;
    if (binding->animation.loop || binding->animation.fps <= 0.0f) return 0.0;
    const Frames& frames = framesOf(atlases, style.sheets[static_cast<size_t>(binding->sheet)], binding->animation);
    return frames.indices.size() * 1000.0 / binding->animation.fps;
}

void NotePreview::build(const NoteStyle& style, AtlasStore& atlases, IImageInfo* images,
                        const PreviewSettings& settings, const PreviewState& state,
                        const std::vector<PreviewNote>& notes, RenderList& out) {
    m_images = images;
    const float laneWidth = laneWidthOf(style);
    const float strumY = strumCenterY(settings.downscroll, laneWidth);
    const float direction = settings.downscroll ? -1.0f : 1.0f;
    auto speedOf = [&](int line) {
        const float own = line == 0 || line == 1 ? settings.lineSpeed[static_cast<size_t>(line)] : 0.0f;
        return std::max(0.1f, own > 0.0f ? own : settings.scrollSpeed);
    };
    auto lineVisible = [&](int line) { return line == 0 ? settings.showOpponent : settings.showPlayer; };
    auto textureOf = [&](const NoteStyle& owner, const Sheet& sheet, Part part, int lane) -> std::string {
        if (textureFor) {
            std::string custom = textureFor(owner, sheet, part, lane);
            if (!custom.empty()) return custom;
        }
        return sheet.image;
    };

    // Dibuja una pieza de un estilo en (cx, cy); devuelve false si no hay con que.
    auto drawPartOf = [&](const NoteStyle& owner, Part part, int lane, int variant, float cx, float cy,
                          double elapsedMs, float alphaScale = 1.0f) -> bool {
        const PartBinding* binding = findPart(owner, part, lane, variant);
        if (!binding || binding->sheet < 0 || static_cast<size_t>(binding->sheet) >= owner.sheets.size()) return false;
        const Sheet& sheet = owner.sheets[static_cast<size_t>(binding->sheet)];
        if (sheet.image.empty()) return false;
        const Frames& frames = framesOf(atlases, sheet, binding->animation);
        const int index = frameIndexFor(binding->animation, frames.indices.size(), elapsedMs);
        if (index < 0 || !frames.atlas) return false;
        const AtlasFrame& frame = frames.atlas->frames[frames.indices[static_cast<size_t>(index)]];
        // Cada pieza se centra en su carril y los offsets declarados (los de
        // la hoja y los de la animacion) la desplazan como el `offset` de
        // Flixel: se dibuja en posicion - offset. La colocacion exacta de cada
        // motor es de la fase del HUD.
        float offsetX = sheet.offsetX + binding->animation.offsetX;
        float offsetY = sheet.offsetY + binding->animation.offsetY;
        // Una salpicadura se coloca con el ancla de su motor: la de Psych no
        // esta centrada y sus offsets la corrigen.
        float anchorX = 0.0f, anchorY = 0.0f;
        if (part == Part::Splash &&
            splashAnchor(owner.engine, frame.frameW > 0 ? static_cast<float>(frame.frameW) : frameDrawW(frame),
                         frame.frameH > 0 ? static_cast<float>(frame.frameH) : frameDrawH(frame), anchorX, anchorY)) {
            offsetX -= anchorX;
            offsetY -= anchorY;
        }
        pushFrame(out, out.internTexture(textureOf(owner, sheet, part, lane)), frame, cx - offsetX, cy - offsetY,
                  sheet.scale, sheet.alpha * alphaScale, -1.0f, false, sheet.pixel || owner.pixel);
        return true;
    };
    auto drawPart = [&](Part part, int lane, int variant, float cx, float cy, double elapsedMs) -> bool {
        return drawPartOf(style, part, lane, variant, cx, cy, elapsedMs);
    };
    auto drawReceptor = [&](Part part, int lane, float cx, float cy, double elapsedMs) {
        if (receptorStyle) return drawPartOf(*receptorStyle, part, lane, 0, cx, cy, elapsedMs);
        if (drawPart(part, lane, 0, cx, cy, elapsedMs)) return true;
        return receptorFallback && receptorFallback != &style &&
               drawPartOf(*receptorFallback, part, lane, 0, cx, cy, elapsedMs);
    };

    // Receptores.
    for (int line = 0; line < 2; ++line) {
        if (!lineVisible(line)) continue;
        for (int lane = 0; lane < 4; ++lane) {
            const int slot = line * 4 + lane;
            const float cx = laneCenterX(line, lane, laneWidth);
            const double since = state.songMs - state.strumSinceMs[static_cast<size_t>(slot)];
            Part part = Part::StrumStatic;
            if (state.strum[static_cast<size_t>(slot)] == StrumState::Press) part = Part::StrumPress;
            if (state.strum[static_cast<size_t>(slot)] == StrumState::Confirm) part = Part::StrumConfirm;
            if (!drawReceptor(part, lane, cx, strumY, since) && part != Part::StrumStatic)
                drawReceptor(Part::StrumStatic, lane, cx, strumY, since);
        }
    }

    // Notas y sostenidos, del mas lejano al mas cercano para que las cercanas queden encima.
    const float visibleFrom = -laneWidth * 2.0f;
    const float visibleTo = kGameH + laneWidth * 2.0f;
    for (size_t order = notes.size(); order-- > 0;) {
        const PreviewNote& note = notes[order];
        if (note.strumLine < 0 || note.strumLine > 1 || !lineVisible(note.strumLine)) continue;
        // Una nota custom se dibuja con el estilo de su tipo, si lo tiene; si
        // no, con el del mod, como hace el motor (Note.hx:156-158 en Codename).
        const NoteStyle* custom = styleForNote ? styleForNote(order) : nullptr;
        const NoteStyle& noteStyle = custom ? *custom : style;
        // Sostenidos al 60 % en Codename y Psych (Note.hx:201; Note.hx:282-283),
        // opacos en V-Slice (SustainTrail.hx:240).
        const float holdAlpha = noteStyle.engine == Engine::VSlice ? 1.0f : 0.6f;
        const bool hit = order < state.hit.size() && state.hit[order];
        const float cx = laneCenterX(note.strumLine, note.lane, laneWidth);
        const float speed = speedOf(note.strumLine);
        const float headY = strumY + direction * static_cast<float>(note.timeMs - state.songMs) * kPixelsPerMs * speed;
        const float tailLength = static_cast<float>(note.sustainMs) * kPixelsPerMs * speed;
        const size_t holdBegin = out.cmds.size();
        if (note.sustainMs > 0.0) {
            // Un sostenido acertado se consume desde el receptor.
            float start = hit ? std::max(0.0f, (static_cast<float>(state.songMs - note.timeMs)) * kPixelsPerMs * speed) : 0.0f;
            if (start < tailLength) {
                const float from = headY + direction * start;
                const float to = headY + direction * tailLength;
                const float top = std::min(from, to);
                const float bottom = std::max(from, to);
                if (bottom >= visibleFrom && top <= visibleTo) {
                    const PartBinding* piece = findPart(noteStyle, Part::HoldPiece, note.lane);
                    const PartBinding* end = findPart(noteStyle, Part::HoldEnd, note.lane);
                    if (piece && piece->sheet >= 0 && static_cast<size_t>(piece->sheet) < noteStyle.sheets.size()) {
                        const Sheet& sheet = noteStyle.sheets[static_cast<size_t>(piece->sheet)];
                        if (sheet.kind == SheetKind::Strip) {
                            const ImageInfo info = images && !sheet.image.empty() ? images->imageInfo(sheet.image) : ImageInfo{};
                            const int texture = out.internTexture(textureOf(noteStyle, sheet, Part::HoldPiece, note.lane));
                            const int pieceColumn = piece->animation.indices.empty() ? note.lane * 2 : piece->animation.indices[0];
                            const int endColumn = end && !end->animation.indices.empty() ? end->animation.indices[0] : pieceColumn + 1;
                            // SustainTrail.hx:106-112, :217-224, :320-409: el final
                            // ocupa media imagen dentro de la nota (endOffset) y
                            // sobresale hasta el 90 % de la imagen (bottomClip), donde
                            // se corta: la tira trae una fila de relleno abajo que no
                            // se ve. En pixel los dos son 1. Si queda menos sostenido
                            // que ese final, el final se recorta por arriba.
                            const float endOffset = sheet.pixel ? 1.0f : 0.5f;
                            const float bottomClip = sheet.pixel ? 1.0f : 0.9f;
                            const float imageH = info.ok ? static_cast<float>(info.h) * sheet.scale : 0.0f;
                            const float length = bottom - top;
                            const float bottomHeight = imageH * endOffset;
                            const float partHeight = std::max(0.0f, length - bottomHeight);
                            const float capV0 = length >= bottomHeight || imageH <= 0.0f ? 0.0f : (bottomHeight - length) / imageH;
                            const float capH = std::min(length, bottomHeight) + imageH * (bottomClip - endOffset);
                            const float alpha = sheet.alpha * holdAlpha;
                            if (!settings.downscroll) {
                                pushStripColumn(out, texture, info, pieceColumn, sheet.columns, cx, top, partHeight, sheet.scale, alpha, false);
                                pushStripColumn(out, texture, info, endColumn, sheet.columns, cx, top + partHeight, capH, sheet.scale, alpha, false,
                                                capV0, bottomClip);
                            } else {
                                pushStripColumn(out, texture, info, endColumn, sheet.columns, cx, top - imageH * (bottomClip - endOffset), capH,
                                                sheet.scale, alpha, true, capV0, bottomClip);
                                pushStripColumn(out, texture, info, pieceColumn, sheet.columns, cx, top + std::min(length, bottomHeight), partHeight,
                                                sheet.scale, alpha, true);
                            }
                        } else {
                            const Frames& pieceFrames = framesOf(atlases, sheet, piece->animation);
                            const Frames* endFrames = nullptr;
                            const Sheet* endSheet = nullptr;
                            if (end && end->sheet >= 0 && static_cast<size_t>(end->sheet) < noteStyle.sheets.size()) {
                                endSheet = &noteStyle.sheets[static_cast<size_t>(end->sheet)];
                                endFrames = &framesOf(atlases, *endSheet, end->animation);
                            }
                            float endH = 0.0f;
                            const AtlasFrame* endFrame = nullptr;
                            if (endFrames && endFrames->atlas && !endFrames->indices.empty()) {
                                endFrame = &endFrames->atlas->frames[endFrames->indices[0]];
                                endH = frameDrawH(*endFrame) * endSheet->scale;
                            }
                            const float bodyH = std::max(0.0f, (bottom - top) - endH);
                            if (pieceFrames.atlas && !pieceFrames.indices.empty() && !sheet.image.empty()) {
                                const AtlasFrame& frame = pieceFrames.atlas->frames[pieceFrames.indices[0]];
                                const float bodyTop = settings.downscroll ? top + endH : top;
                                pushFrame(out, out.internTexture(textureOf(noteStyle, sheet, Part::HoldPiece, note.lane)), frame,
                                          cx, bodyTop, sheet.scale, sheet.alpha * holdAlpha, bodyH, true,
                                          sheet.pixel || noteStyle.pixel);
                                if (settings.downscroll) out.cmds.back().flipY = true;
                            }
                            if (endFrame && !endSheet->image.empty()) {
                                const float endTop = settings.downscroll ? top : top + bodyH;
                                pushFrame(out, out.internTexture(textureOf(noteStyle, *endSheet, Part::HoldEnd, note.lane)), *endFrame, cx,
                                          endTop + endH * 0.5f, endSheet->scale, endSheet->alpha * holdAlpha, -1.0f, false,
                                          endSheet->pixel || noteStyle.pixel);
                                if (settings.downscroll) out.cmds.back().flipY = true;
                            }
                        }
                    }
                }
            }
        }
        const float noteAlpha = std::isfinite(note.alpha) ? std::clamp(note.alpha, 0.0f, 1.0f) : 1.0f;
        for (size_t i = holdBegin; i < out.cmds.size(); ++i) out.cmds[i].alpha *= noteAlpha;
        if (hit) continue;
        if (headY < visibleFrom || headY > visibleTo) continue;
        const size_t headBegin = out.cmds.size();
        drawPartOf(noteStyle, Part::Note, note.lane, 0, cx, headY, state.songMs);
        const float scaleX = std::isfinite(note.scaleX) ? std::clamp(note.scaleX, 0.1f, 4.0f) : 1.0f;
        const float scaleY = std::isfinite(note.scaleY) ? std::clamp(note.scaleY, 0.1f, 4.0f) : 1.0f;
        for (size_t i = headBegin; i < out.cmds.size(); ++i) {
            DrawCmd& c = out.cmds[i];
            c.x = cx + (c.x - cx) * scaleX; c.y = headY + (c.y - headY) * scaleY;
            c.ma *= scaleX; c.mc *= scaleX; c.mb *= scaleY; c.md *= scaleY;
            c.alpha *= noteAlpha;
            if (std::isfinite(note.angle)) rotateAround(c, std::clamp(note.angle, -360.0f, 360.0f), cx, headY);
        }
    }

    // Salpicaduras, encima de todo: la del tipo de la nota si tiene una propia
    // (la electrica de la Hurt Note de Psych) y si no la del estilo.
    for (const PreviewState::Splash& splash : state.splashes) {
        if (!lineVisible(splash.strumLine)) continue;
        const NoteStyle* custom = splash.note >= 0 && styleForNote ? styleForNote(static_cast<size_t>(splash.note)) : nullptr;
        const NoteStyle& owner = custom && splashVariants(*custom, splash.lane) > 0 ? *custom
                               : splashStyle && splashVariants(*splashStyle, splash.lane) > 0 ? *splashStyle : style;
        const int variants = splashVariants(owner, splash.lane);
        if (variants <= 0) continue;
        drawPartOf(owner, Part::Splash, splash.lane, static_cast<int>(splash.roll % static_cast<std::uint32_t>(variants)),
                   laneCenterX(splash.strumLine, splash.lane, laneWidth), strumY, state.songMs - splash.startMs);
    }

    // Popup de juicio y cifras del combo: sube un poco y se desvanece. La
    // posicion y el movimiento son aproximados (los exactos son de la fase del HUD).
    if (!style.hasHud || !images) return;
    auto pushImage = [&](const HudAsset& asset, float cx, float cy, float extraScale, float alpha) {
        if (asset.image.empty()) return 0.0f;
        const ImageInfo info = images->imageInfo(asset.image);
        if (!info.ok) return 0.0f;
        const float scale = (asset.scale > 0.0f ? asset.scale : 1.0f) * extraScale;
        DrawCmd c;
        c.texture = out.internTexture(asset.image);
        c.sx = 0.0f;
        c.sy = 0.0f;
        c.sw = static_cast<float>(info.w);
        c.sh = static_cast<float>(info.h);
        c.w = info.w * scale;
        c.h = info.h * scale;
        c.x = cx - c.w * 0.5f;
        c.y = cy - c.h * 0.5f;
        c.alpha = alpha;
        c.antialiasing = !asset.pixel;
        c.scrollX = 0.0f;
        c.scrollY = 0.0f;
        c.zoomFactor = 0.0f;
        c.pickable = false;
        out.cmds.push_back(std::move(c));
        return info.w * scale;
    };
    const bool vslice = style.engine == Engine::VSlice;
    const float ratingScale = vslice ? 1.0f : 0.7f;   // PlayState.hx:2060 (0.7)
    const float numberScale = vslice ? 1.0f : 0.5f;   // PlayState.hx:2148 (0.5)
    float judgementX = 0.0f, judgementY = 0.0f, comboX = 0.0f, comboY = 0.0f;
    hudCenters(settings, judgementX, judgementY, comboX, comboY);
    const HudLayer& judgementLayer = settings.judgement;
    const HudLayer& comboLayer = settings.combo;
    auto drawPopup = [&](int judgement, int combo, float rise, float alpha) {
        if (judgementLayer.visible && judgement >= 0 && judgement <= 3)
            pushImage(style.judgements[static_cast<size_t>(judgement)], judgementX, judgementY - rise,
                      ratingScale * judgementLayer.scale, alpha * judgementLayer.alpha);
        if (combo <= 0 || !comboLayer.visible) return;
        std::string digits = std::to_string(combo);
        while (digits.size() < 3) digits.insert(digits.begin(), '0');
        const float step = 43.0f * comboLayer.scale;
        float x = comboX - (static_cast<float>(digits.size()) - 1.0f) * step * 0.5f;
        for (char d : digits) {
            pushImage(style.digits[static_cast<size_t>(d - '0')], x, comboY - rise * 0.5f,
                      numberScale * comboLayer.scale, alpha * comboLayer.alpha);
            x += step;
        }
    };
    for (const PreviewState::Popup& popup : state.popups) {
        const double age = state.songMs - popup.startMs;
        if (age < 0.0 || age > 900.0 || popup.judgement < 0 || popup.judgement > 3) continue;
        const float rise = static_cast<float>(std::min(age, 250.0)) * 0.12f;
        const float alpha = age < 550.0 ? 1.0f : static_cast<float>(1.0 - (age - 550.0) / 350.0);
        drawPopup(popup.judgement, popup.combo, rise, alpha);
    }
    if (settings.hudSample) drawPopup(0, 123, 0.0f, 1.0f);
}

void NotePreview::hudCenters(const PreviewSettings& settings, float& judgementX, float& judgementY,
                             float& comboX, float& comboY) {
    const float baseX = kGameW * 0.55f;
    const float baseY = settings.downscroll ? kGameH * 0.35f : kGameH * 0.45f;
    judgementX = baseX + settings.judgement.x;
    judgementY = baseY + settings.judgement.y;
    comboX = baseX + settings.combo.x;
    comboY = baseY + 80.0f + settings.combo.y;
}

std::vector<PreviewNote> demoPattern(double bpm, double& lengthMs) {
    const double beat = 60000.0 / std::max(30.0, bpm);
    const double lead = beat * 2.0;
    std::vector<PreviewNote> notes;
    // Cuatro compases de cuatro tiempos; el rival y el jugador se turnan cada compas.
    for (int bar = 0; bar < 4; ++bar) {
        const int line = bar % 2;
        for (int step = 0; step < 8; ++step) {
            PreviewNote note;
            note.strumLine = line;
            note.lane = (step + bar) % 4;
            note.timeMs = lead + (bar * 4.0 + step * 0.5) * beat;
            if (step == 6) note.sustainMs = beat * 0.9;
            if (step == 7) continue;   // hueco tras el sostenido
            notes.push_back(note);
        }
    }
    lengthMs = lead + 16.0 * beat + beat * 2.0;
    return notes;
}

namespace {

void sortNotes(std::vector<PreviewNote>& notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const PreviewNote& a, const PreviewNote& b) {
        if (a.timeMs != b.timeMs) return a.timeMs < b.timeMs;
        if (a.strumLine != b.strumLine) return a.strumLine < b.strumLine;
        return a.lane < b.lane;
    });
}

}  // namespace

std::vector<PreviewNote> demoPatternOf(int kind, double bpm, double& lengthMs, std::uint32_t seed) {
    if (kind <= 0 || kind >= kDemoKinds) return demoPattern(bpm, lengthMs);
    const double beat = 60000.0 / std::max(30.0, bpm);
    const double lead = beat * 2.0;
    std::vector<PreviewNote> notes;
    auto add = [&](int line, int lane, double beats, double holdBeats = 0.0) {
        PreviewNote note;
        note.strumLine = line;
        note.lane = ((lane % 4) + 4) % 4;
        note.timeMs = lead + beats * beat;
        note.sustainMs = holdBeats > 0.0 ? holdBeats * beat : 0.0;
        notes.push_back(note);
    };
    std::uint32_t state = seed ? seed : 1u;
    auto next = [&](std::uint32_t range) {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 8) % range);
    };
    for (int bar = 0; bar < 4; ++bar) {
        const int line = bar % 2;
        const double start = bar * 4.0;
        switch (kind) {
            case 1: {   // escalera: corcheas que suben y bajan
                static const int stairs[8] = {0, 1, 2, 3, 3, 2, 1, 0};
                for (int k = 0; k < 8; ++k) add(line, bar % 2 ? 3 - stairs[k] : stairs[k], start + k * 0.5);
                break;
            }
            case 2: {   // repeticiones: tres semicorcheas en el mismo carril y un hueco
                for (int k = 0; k < 16; ++k)
                    if (k % 4 != 3) add(line, k / 4 + bar, start + k * 0.25);
                break;
            }
            case 3: {   // acordes: de dos en un compas, de tres en el siguiente
                static const int pairs[4][2] = {{0, 3}, {1, 2}, {0, 1}, {2, 3}};
                static const int triples[4][3] = {{0, 1, 2}, {1, 2, 3}, {0, 2, 3}, {0, 1, 3}};
                for (int b = 0; b < 4; ++b) {
                    if (bar % 2 == 0)
                        for (int lane : pairs[b]) add(line, lane, start + b);
                    else
                        for (int lane : triples[b]) add(line, lane, start + b);
                }
                break;
            }
            case 4: {   // sostenidos largos, con toques en los otros carriles
                add(line, bar, start, 1.5);
                add(line, bar + 2, start + 2.0, 1.5);
                add(line, bar + 1, start + 1.0);
                add(line, bar + 3, start + 3.0);
                add(line, bar + 1, start + 3.5);
                break;
            }
            case 5: {   // rafaga: semicorcheas sin repetir carril
                int last = -1;
                for (int k = 0; k < 16; ++k) {
                    int lane = next(4);
                    if (lane == last) lane = (lane + 1 + next(3)) % 4;
                    last = lane;
                    add(line, lane, start + k * 0.25);
                }
                break;
            }
            case 6: {   // aleatorio: corcheas con algun sostenido y algun acorde
                for (int k = 0; k < 8; ++k) {
                    if (next(10) < 2) continue;   // un silencio de vez en cuando
                    const int lane = next(4);
                    const bool hold = next(4) == 0;
                    add(line, lane, start + k * 0.5, hold ? 0.5 * (1 + next(3)) : 0.0);
                    if (!hold && next(7) == 0) add(line, lane + 1 + next(3), start + k * 0.5);
                }
                break;
            }
            default: break;
        }
    }
    sortNotes(notes);
    lengthMs = lead + 16.0 * beat + beat * 2.0;
    return notes;
}

std::vector<PreviewNote> customPatternNotes(const CustomPattern& pattern, double bpm, double& lengthMs) {
    const double beat = 60000.0 / std::max(30.0, bpm);
    const double lead = beat * 2.0;
    const int bars = std::clamp(pattern.bars, 1, kPatternMaxBars);
    const int steps = bars * 16;
    std::vector<PreviewNote> notes;
    for (const PatternNote& one : pattern.notes) {
        if (one.step < 0 || one.step >= steps || one.lane < 0 || one.lane > 3 || one.side < 0 || one.side > 1) continue;
        if (notes.size() >= kPatternMaxNotes) break;
        PreviewNote note;
        note.strumLine = one.side;
        note.lane = one.lane;
        note.timeMs = lead + one.step * beat / 4.0;
        note.sustainMs = std::clamp(one.hold, 0, steps - one.step) * beat / 4.0;
        notes.push_back(note);
    }
    sortNotes(notes);
    lengthMs = lead + bars * 4.0 * beat + beat * 2.0;
    return notes;
}

CustomPattern patternFromNotes(const std::vector<PreviewNote>& notes, double bpm, int bars) {
    CustomPattern pattern;
    pattern.bars = std::clamp(bars, 1, kPatternMaxBars);
    const double beat = 60000.0 / std::max(30.0, bpm);
    const double lead = beat * 2.0;
    const int steps = pattern.bars * 16;
    for (const PreviewNote& note : notes) {
        const int step = static_cast<int>(std::lround((note.timeMs - lead) / (beat / 4.0)));
        if (step < 0 || step >= steps || note.lane < 0 || note.lane > 3) continue;
        PatternNote one;
        one.side = note.strumLine == 0 ? 0 : 1;
        one.lane = note.lane;
        one.step = step;
        one.hold = std::max(0, static_cast<int>(std::lround(note.sustainMs / (beat / 4.0))));
        pattern.notes.push_back(one);
    }
    return pattern;
}

namespace {

void pushSplash(PreviewState& state, int strumLine, int lane, double startMs, size_t note) {
    state.splashSeed = state.splashSeed * 1664525u + 1013904223u;
    PreviewState::Splash splash;
    splash.strumLine = strumLine;
    splash.lane = lane;
    splash.roll = state.splashSeed >> 16;
    splash.startMs = startMs;
    splash.note = static_cast<int>(note);
    state.splashes.push_back(splash);
}

}  // namespace

void advanceAutoplay(PreviewState& state, const NoteStyle& style, const std::vector<PreviewNote>& notes,
                     double fromMs, double toMs, const std::array<bool, 2>& autoSide, double splashLengthMs) {

    if (state.hit.size() != notes.size()) state.hit.assign(notes.size(), 0);
    for (size_t i = 0; i < notes.size(); ++i) {
        const PreviewNote& note = notes[i];
        if (note.strumLine < 0 || note.strumLine > 1 || !autoSide[static_cast<size_t>(note.strumLine)]) continue;
        if (state.hit[i] || note.botSkips || note.timeMs <= fromMs || note.timeMs > toMs) continue;
        state.hit[i] = 1;
        const size_t slot = static_cast<size_t>(note.strumLine * 4 + note.lane);
        state.strum[slot] = StrumState::Confirm;
        state.strumSinceMs[slot] = note.timeMs;
        state.confirmUntilMs[slot] = std::max(state.confirmUntilMs[slot], note.timeMs + std::max(150.0, note.sustainMs));
        if (note.strumLine != state.playerLine) continue;
        ++state.combo;
        state.popups.push_back({0, state.combo, note.timeMs});
        pushSplash(state, note.strumLine, note.lane, note.timeMs, i);
    }
    for (size_t slot = 0; slot < 8; ++slot) {
        const int line = static_cast<int>(slot / 4);
        if (!autoSide[static_cast<size_t>(line)]) continue;
        if (state.strum[slot] == StrumState::Confirm && toMs >= state.confirmUntilMs[slot]) {
            state.strum[slot] = StrumState::Static;
            state.strumSinceMs[slot] = toMs;
        }
    }
    state.popups.erase(std::remove_if(state.popups.begin(), state.popups.end(),
        [&](const PreviewState::Popup& p) { return toMs - p.startMs > 900.0 || toMs < p.startMs; }),
        state.popups.end());
    const double life = splashLengthMs > 0.0 ? splashLengthMs : 500.0;
    state.splashes.erase(std::remove_if(state.splashes.begin(), state.splashes.end(),
        [&](const PreviewState::Splash& s) { return toMs - s.startMs > life || toMs < s.startMs; }),
        state.splashes.end());
}

PressResult pressLane(PreviewState& state, const NoteStyle& style, const std::vector<PreviewNote>& notes,
                      int strumLine, int lane, double windowMs) {
    PressResult result;
    if (state.hit.size() != notes.size()) state.hit.assign(notes.size(), 0);
    const size_t slot = static_cast<size_t>(strumLine * 4 + lane);
    double best = windowMs + 1.0;
    for (size_t i = 0; i < notes.size(); ++i) {
        const PreviewNote& note = notes[i];
        if (note.strumLine != strumLine || note.lane != lane || state.hit[i]) continue;
        const double offset = note.timeMs - state.songMs;
        if (std::fabs(offset) <= windowMs && std::fabs(offset) < std::fabs(best)) {
            best = offset;
            result.note = static_cast<int>(i);
        }
    }
    if (result.note >= 0) {
        result.hit = true;
        result.offsetMs = best;
        state.hit[static_cast<size_t>(result.note)] = 1;
        state.strum[slot] = StrumState::Confirm;
        pushSplash(state, strumLine, lane, state.songMs, static_cast<size_t>(result.note));
    } else {
        state.strum[slot] = StrumState::Press;
    }
    state.strumSinceMs[slot] = state.songMs;
    state.confirmUntilMs[slot] = 0.0;
    return result;
}

void releaseLane(PreviewState& state, int strumLine, int lane) {
    const size_t slot = static_cast<size_t>(strumLine * 4 + lane);
    state.strum[slot] = StrumState::Static;
    state.strumSinceMs[slot] = state.songMs;
}

}  // namespace fml::notelab

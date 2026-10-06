#include "NoteStyle.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace fml::notelab {

const char* engineKey(Engine engine) {
    switch (engine) {
        case Engine::Codename: return "codename";
        case Engine::Psych:    return "psych";
        case Engine::VSlice:   return "vslice";
    }
    return "?";
}

const char* partKey(Part part) {
    switch (part) {
        case Part::Note:             return "note";
        case Part::HoldPiece:        return "holdPiece";
        case Part::HoldEnd:          return "holdEnd";
        case Part::StrumStatic:      return "strumStatic";
        case Part::StrumPress:       return "strumPress";
        case Part::StrumConfirm:     return "strumConfirm";
        case Part::StrumConfirmHold: return "strumConfirmHold";
        case Part::Splash:           return "splash";
        case Part::HoldCoverStart:   return "holdCoverStart";
        case Part::HoldCover:        return "holdCover";
        case Part::HoldCoverEnd:     return "holdCoverEnd";
    }
    return "?";
}

const char* directionKey(int direction) {
    static const char* names[] = {"left", "down", "up", "right"};
    return names[((direction % 4) + 4) % 4];
}

const char* sheetKindKey(SheetKind kind) {
    switch (kind) {
        case SheetKind::Sparrow: return "sparrow";
        case SheetKind::Packer:  return "packer";
        case SheetKind::Strip:   return "strip";
        case SheetKind::Grid:    return "grid";
        case SheetKind::Image:   return "image";
    }
    return "?";
}

const char* styleUseKey(StyleUse use) {
    switch (use) {
        case StyleUse::Default:      return "default";
        case StyleUse::NoteType:     return "noteType";
        case StyleUse::Song:         return "song";
        case StyleUse::PlayerChoice: return "playerChoice";
        case StyleUse::Declared:     return "declared";
    }
    return "?";
}

const char* judgementKey(int index) {
    static const char* names[] = {"sick", "good", "bad", "shit"};
    return index >= 0 && index < 4 ? names[index] : "?";
}

const char* countdownKey(int index) {
    static const char* names[] = {"three", "two", "one", "go"};
    return index >= 0 && index < 4 ? names[index] : "?";
}

int addSheet(NoteStyle& style, const Sheet& sheet) {
    for (size_t i = 0; i < style.sheets.size(); ++i) {
        const Sheet& existing = style.sheets[i];
        if (existing.image == sheet.image && existing.atlas == sheet.atlas &&
            existing.declared == sheet.declared && existing.kind == sheet.kind &&
            existing.columns == sheet.columns && existing.rows == sheet.rows &&
            existing.scale == sheet.scale && existing.offsetX == sheet.offsetX &&
            existing.offsetY == sheet.offsetY && existing.alpha == sheet.alpha &&
            existing.pixel == sheet.pixel && existing.rgbFixed == sheet.rgbFixed)
            return static_cast<int>(i);
    }
    style.sheets.push_back(sheet);
    return static_cast<int>(style.sheets.size() - 1);
}

void bindPart(NoteStyle& style, Part part, int direction, int sheet, Animation animation,
              int variant) {
    PartBinding binding;
    binding.part = part;
    binding.direction = direction;
    binding.variant = variant;
    binding.sheet = sheet;
    binding.animation = std::move(animation);
    style.parts.push_back(std::move(binding));
}

bool StyleComponents::includes(Part part) const {
    switch (part) {
        case Part::Note: case Part::HoldPiece: case Part::HoldEnd: return notes;
        case Part::StrumStatic: case Part::StrumPress: case Part::StrumConfirm:
        case Part::StrumConfirmHold: return receptors;
        case Part::Splash: return splashes;
        case Part::HoldCoverStart: case Part::HoldCover: case Part::HoldCoverEnd: return holdCovers;
    }
    return false;
}

int composeStyle(NoteStyle& target, const NoteStyle& donor, const StyleComponents& c) {
    NoteStyle next = target;
    int count = 0;
    std::map<int, int> mapped;
    for (const PartBinding& binding : donor.parts) {
        if (!c.includes(binding.part) || binding.direction < 0 || binding.direction > 3 || binding.variant < 0 ||
            binding.sheet < 0 || static_cast<size_t>(binding.sheet) >= donor.sheets.size()) continue;
        const Sheet& sheet = donor.sheets[static_cast<size_t>(binding.sheet)];
        if (sheet.image.empty() || !std::isfinite(sheet.scale) || sheet.scale <= 0.0f) continue;
        auto found = mapped.find(binding.sheet);
        if (found == mapped.end()) found = mapped.emplace(binding.sheet, addSheet(next, sheet)).first;
        next.parts.erase(std::remove_if(next.parts.begin(), next.parts.end(), [&](const PartBinding& old) {
            return old.part == binding.part && old.direction == binding.direction && old.variant == binding.variant;
        }), next.parts.end());
        PartBinding copy = binding;
        copy.sheet = found->second;
        copy.inherited = false;
        next.parts.push_back(std::move(copy));
        ++count;
    }
    auto copyHud = [&](HudAsset& dst, const HudAsset& src, float factor) {
        if (src.image.empty() && src.sound.empty() && !src.imageOptional) return;
        dst = src;
        dst.scale *= factor;
        dst.inherited = false;
        next.hasHud = true;
        ++count;
    };
    const float judgementScale = (donor.engine == Engine::VSlice ? 1.0f : 0.7f) /
                                 (target.engine == Engine::VSlice ? 1.0f : 0.7f);
    const float digitScale = (donor.engine == Engine::VSlice ? 1.0f : 0.5f) /
                             (target.engine == Engine::VSlice ? 1.0f : 0.5f);
    if (c.judgements) for (size_t i = 0; i < 4; ++i) copyHud(next.judgements[i], donor.judgements[i], judgementScale);
    if (c.combo) {
        copyHud(next.combo, donor.combo, judgementScale);
        for (size_t i = 0; i < 10; ++i) copyHud(next.digits[i], donor.digits[i], digitScale);
    }
    if (c.countdown) for (size_t i = 0; i < 4; ++i) copyHud(next.countdown[i], donor.countdown[i], 1.0f);
    if (c.sounds) for (const auto& sound : donor.sounds) {
        if (sound.name.empty() || sound.path.empty()) continue;
        auto found = std::find_if(next.sounds.begin(), next.sounds.end(), [&](const StyleSound& item) { return item.name == sound.name; });
        if (found == next.sounds.end()) next.sounds.push_back(sound); else *found = sound;
        ++count;
    }
    if (next.sheets.size() > 512 || next.parts.size() > 4096 || next.sounds.size() > 512) return 0;
    if (count > 0) target = std::move(next);
    return count;
}

}  // namespace fml::notelab

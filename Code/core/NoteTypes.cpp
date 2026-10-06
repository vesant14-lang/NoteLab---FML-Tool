#include "NoteTypes.hpp"

#include "../support/io/Vfs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace fml::notelab {
namespace {

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

size_t segmentAt(const std::string& lowerPath, const std::string& segment) {
    size_t pos = 0;
    while ((pos = lowerPath.find(segment, pos)) != std::string::npos) {
        if (pos == 0 || lowerPath[pos - 1] == '/') return pos;
        ++pos;
    }
    return std::string::npos;
}

std::string fileName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stemOf(const std::string& path) {
    const std::string name = fileName(path);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string extensionOf(const std::string& lowPath) {
    const size_t dot = lowPath.find_last_of('.');
    const size_t slash = lowPath.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return lowPath.substr(dot);
}

// Los argumentos de texto de cada `super(...)` de un .hxc: el NoteKind de V-Slice
// se construye como new NoteKind(noteKind, description, ?noteStyleId, ...)
// (NoteKind.hx:48).
std::vector<std::vector<std::string>> superCalls(const std::string& text) {
    std::vector<std::vector<std::string>> calls;
    size_t at = 0;
    while ((at = text.find("super", at)) != std::string::npos) {
        size_t cursor = at + 5;
        while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
        if (cursor >= text.size() || text[cursor] != '(') { at = cursor; continue; }
        ++cursor;
        std::vector<std::string> args;
        while (cursor < text.size()) {
            while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
            if (cursor >= text.size() || (text[cursor] != '\'' && text[cursor] != '"')) break;
            const char quote = text[cursor];
            const size_t close = text.find(quote, cursor + 1);
            if (close == std::string::npos) break;
            args.push_back(text.substr(cursor + 1, close - cursor - 1));
            cursor = close + 1;
            while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
            if (cursor >= text.size() || text[cursor] != ',') break;
            ++cursor;
        }
        if (!args.empty()) calls.push_back(std::move(args));
        at = cursor;
    }
    return calls;
}

// Un .txt de custom_notetypes es configuracion si alguna linea asigna un
// literal de Psych a una propiedad (NoteTypesConfig.hx:109-129); el readme.txt
// que trae la plantilla de mods no lo es.
bool looksLikePsychConfig(const std::string& text) {
    auto trimmed = [](std::string value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
        return value.substr(first);
    };
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        size_t sep = line.find(':');
        if (sep == std::string::npos) sep = line.find('=');
        if (sep == std::string::npos) continue;
        const std::string key = trimmed(line.substr(0, sep));
        const std::string value = trimmed(line.substr(sep + 1));
        bool property = !key.empty() && (std::isalpha(static_cast<unsigned char>(key[0])) || key[0] == '_');
        for (char c : key)
            property = property && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '[' || c == ']');
        if (!property || value.empty()) continue;
        if (!quotedValue(value).empty() || value == "true" || value == "false" || value == "null") return true;
        size_t digits = value[0] == '-' ? 1 : 0;
        bool number = digits < value.size();
        for (size_t i = digits; i < value.size(); ++i)
            number = number && (std::isdigit(static_cast<unsigned char>(value[i])) || value[i] == '.');
        if (number) return true;
    }
    return false;
}

// `ignoreNote` de Psych: `true`, o el `mustPress` de la nota como la Hurt Note.
BotRule psychIgnore(const std::string& value) {
    if (value == "true") return BotRule::Ignores;
    if (value.find("mustPress") != std::string::npos) return BotRule::IgnoresOnPlayerSide;
    return BotRule::Hits;
}

}  // namespace

const NoteTypeEntry* findNoteType(const std::vector<NoteTypeEntry>& types, const std::string& name) {
    const std::string key = lower(name);
    for (const NoteTypeEntry& type : types)
        if (lower(type.name) == key) return &type;
    return nullptr;
}

const NoteTypeEntry* findNoteType(const std::vector<NoteTypeEntry>& types, const std::string& name, Engine engine) {
    const std::string key = lower(name);
    for (const NoteTypeEntry& type : types)
        if (type.engine == engine && lower(type.name) == key) return &type;
    return nullptr;
}

bool botSkips(const NoteTypeEntry& type, int strumLine) {
    return type.bot == BotRule::Ignores || (type.bot == BotRule::IgnoresOnPlayerSide && strumLine == 1);
}

bool recolorsSkin(const NoteStyle& type) {
    if (type.engine != Engine::Psych || type.use != StyleUse::NoteType) return false;
    for (const PartBinding& binding : type.parts)
        if (binding.part == Part::Note && binding.sheet >= 0 && static_cast<size_t>(binding.sheet) < type.sheets.size() &&
            !type.sheets[static_cast<size_t>(binding.sheet)].rgbFixed.empty())
            return true;
    return false;
}

NoteStyle recolorOver(const NoteStyle& skin, const NoteStyle& type) {
    NoteStyle out = type;
    out.id = type.id + "@" + skin.id;
    out.sheets.clear();
    out.parts.clear();
    out.rgbPalette = skin.rgbPalette;
    out.letteredNaming = skin.letteredNaming;
    std::vector<std::uint32_t> palette;
    for (const PartBinding& binding : type.parts)
        if (binding.part == Part::Note && binding.sheet >= 0 && static_cast<size_t>(binding.sheet) < type.sheets.size()) {
            palette = type.sheets[static_cast<size_t>(binding.sheet)].rgbFixed;
            break;
        }
    auto copyParts = [&](const NoteStyle& from, bool splash, bool recolor) {
        std::map<int, int> sheetOf;
        for (const PartBinding& binding : from.parts) {
            const bool body = binding.part == Part::Note || binding.part == Part::HoldPiece || binding.part == Part::HoldEnd;
            if (splash ? binding.part != Part::Splash : !body) continue;
            if (binding.sheet < 0 || static_cast<size_t>(binding.sheet) >= from.sheets.size()) continue;
            auto mapped = sheetOf.find(binding.sheet);
            if (mapped == sheetOf.end()) {
                Sheet sheet = from.sheets[static_cast<size_t>(binding.sheet)];
                if (recolor) sheet.rgbFixed = palette;
                out.sheets.push_back(sheet);
                mapped = sheetOf.emplace(binding.sheet, static_cast<int>(out.sheets.size()) - 1).first;
            }
            PartBinding copy = binding;
            copy.sheet = mapped->second;
            out.parts.push_back(copy);
        }
    };
    copyParts(skin, false, true);
    copyParts(type, true, false);
    return out;
}

namespace {

// SplitMix64: pequeno, rapido y el mismo en todas partes.
struct SeededRandom {
    std::uint64_t state;
    explicit SeededRandom(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
};

std::uint64_t textHash(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;   // FNV-1a
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace

DistributeResult distributeTypes(const std::vector<PreviewNote>& notes, const std::vector<std::string>& currentTypes,
                                 const DistributeRequest& request) {
    DistributeResult result;
    result.types.assign(notes.size(), std::string());
    for (size_t i = 0; i < notes.size() && i < currentTypes.size(); ++i) result.types[i] = currentTypes[i];
    result.placed.assign(request.rules.size(), 0);
    result.requested.assign(request.rules.size(), 0);
    const DistributeFilter& filter = request.filter;
    // Acordes: otra nota de la misma linea a menos de 1 ms.
    std::vector<std::uint8_t> chord(notes.size(), 0);
    if (filter.skipChords) {
        std::vector<size_t> order(notes.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (notes[a].strumLine != notes[b].strumLine) return notes[a].strumLine < notes[b].strumLine;
            return notes[a].timeMs < notes[b].timeMs;
        });
        for (size_t k = 1; k < order.size(); ++k) {
            const PreviewNote& a = notes[order[k - 1]];
            const PreviewNote& b = notes[order[k]];
            if (a.strumLine == b.strumLine && b.timeMs - a.timeMs < 1.0) chord[order[k - 1]] = chord[order[k]] = 1;
        }
    }
    std::vector<size_t> candidates;
    for (size_t i = 0; i < notes.size(); ++i) {
        const PreviewNote& note = notes[i];
        const std::string type = lower(result.types[i]);
        const bool regular = type.empty() || type == "default note" || type == "normal";
        if (!regular && !filter.replaceExisting) { ++result.protectedTypes; continue; }
        if ((filter.side != 2 && note.strumLine != filter.side) || note.lane < 0 || note.lane > 3 ||
            !filter.lanes[static_cast<size_t>(note.lane)] || (filter.skipSustains && note.sustainMs > 0.0) || chord[i]) {
            ++result.filtered; continue;
        }
        candidates.push_back(i);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) { return notes[a].timeMs < notes[b].timeMs; });
    result.candidates = static_cast<int>(candidates.size());
    std::vector<std::uint8_t> taken(notes.size(), 0);
    for (size_t r = 0; r < request.rules.size(); ++r) {
        const DistributeRule& rule = request.rules[r];
        if (rule.type.empty() || (rule.count < 0 && (!std::isfinite(rule.percent) || rule.percent <= 0.0f))) continue;
        const std::uint64_t seed = rule.seed != 0 ? rule.seed : (static_cast<std::uint64_t>(request.seed) << 32) ^ textHash(rule.type);
        SeededRandom random(seed);
        const int wanted = rule.count >= 0 ? rule.count :
            static_cast<int>(std::floor(result.candidates * std::clamp(rule.percent, 0.0f, 100.0f) / 100.0 + 0.5));
        result.requested[r] = wanted;
        std::vector<size_t> ranked = candidates;
        for (size_t k = ranked.size(); k > 1; --k) std::swap(ranked[k - 1], ranked[static_cast<size_t>(random.next() % k)]);
        std::array<std::set<double>, 2> placedTimes;
        for (const size_t i : ranked) {
            if (result.placed[r] >= wanted) break;
            if (taken[i]) continue;
            const PreviewNote& note = notes[i];
            const size_t line = note.strumLine == 0 ? 0 : 1;
            if (filter.minGapMs > 0.0) {
                const auto next = placedTimes[line].lower_bound(note.timeMs);
                if ((next != placedTimes[line].end() && *next - note.timeMs < filter.minGapMs) ||
                    (next != placedTimes[line].begin() && note.timeMs - *std::prev(next) < filter.minGapMs)) continue;
            }
            taken[i] = 1;
            result.types[i] = rule.type;
            if (filter.minGapMs > 0.0) placedTimes[line].insert(note.timeMs);
            ++result.placed[r];
        }
    }
    return result;
}

bool botSkipsWithProfile(bool inherited, int side, const BotProfile& profile) {
    if (!profile.enabled) return inherited;
    const int mode = side == 1 ? profile.player : profile.opponent;
    return mode == 1 ? false : mode == 2 ? true : inherited;
}

std::vector<NoteTypeEntry> scanNoteTypes(const Vfs& vfs, const Catalog& catalog) {
    std::vector<NoteTypeEntry> types;
    auto merge = [&](NoteTypeEntry entry) {
        for (NoteTypeEntry& existing : types) {
            if (existing.engine != entry.engine || lower(existing.name) != lower(entry.name)) continue;
            if (existing.script.empty()) existing.script = entry.script;
            if (existing.config.empty()) existing.config = entry.config;
            if (existing.lookStyle.empty()) existing.lookStyle = entry.lookStyle;
            if (existing.texture.empty()) existing.texture = entry.texture;
            if (existing.description.empty()) existing.description = entry.description;
            if (existing.bot == BotRule::Hits) existing.bot = entry.bot;
            existing.hitMisses = existing.hitMisses || entry.hitMisses;
            existing.builtin = existing.builtin || entry.builtin;
            return;
        }
        types.push_back(std::move(entry));
    };
    bool psych = false;
    for (const Vfs::Entry& file : vfs.allEntries()) {
        if (file.isDir) continue;
        const std::string path = Vfs::normalize(file.virtualPath);
        const std::string low = lower(path);
        if (insideHiddenFolder(low)) continue;
        const std::string extension = extensionOf(low);
        size_t seg = segmentAt(low, "data/notes/");
        if (seg != std::string::npos && low.find('/', seg + 11) == std::string::npos &&
            (extension == ".hx" || extension == ".hscript" || extension == ".hsc" || extension == ".hxs")) {
            NoteTypeEntry entry;
            entry.engine = Engine::Codename;
            entry.name = stemOf(path);
            entry.script = path;
            if (const auto text = vfs.readText(file))
                if (scriptAssignment(*text, "avoid") == "true") entry.bot = BotRule::Ignores;
            merge(std::move(entry));
            continue;
        }
        seg = segmentAt(low, "custom_notetypes/");
        if (seg != std::string::npos && low.find('/', seg + 17) == std::string::npos &&
            (extension == ".lua" || extension == ".hx" || extension == ".txt")) {
            const bool config = extension == ".txt";
            const auto text = vfs.readText(file);
            if (config && (!text || !looksLikePsychConfig(*text))) continue;
            psych = true;
            NoteTypeEntry entry;
            entry.engine = Engine::Psych;
            entry.name = stemOf(path);
            if (config) entry.config = path;
            else entry.script = path;
            if (text) {
                auto value = [&](const char* key) {
                    return config ? psychConfigValue(*text, key) : scriptAssignment(*text, key);
                };
                // El .txt solo admite literales (NoteTypesConfig.hx:109-129):
                // ahi `mustPress` no es la propiedad de la nota.
                const std::string ignore = value("ignoreNote");
                entry.bot = config ? (ignore == "true" ? BotRule::Ignores : BotRule::Hits) : psychIgnore(ignore);
                entry.hitMisses = value("hitCausesMiss") == "true";
                entry.texture = quotedValue(value("texture"));
            }
            merge(std::move(entry));
            continue;
        }
        seg = segmentAt(low, "scripts/notekinds/");
        // La 0.9 de prueba los guarda en gameplay/notekinds/.
        if (seg == std::string::npos) seg = segmentAt(low, "gameplay/notekinds/");
        if (seg != std::string::npos && extension == ".hxc") {
            const auto text = vfs.readText(file);
            if (!text) continue;
            for (const std::vector<std::string>& args : superCalls(*text)) {
                NoteTypeEntry entry;
                entry.engine = Engine::VSlice;
                entry.name = args[0];
                entry.script = path;
                if (args.size() > 1) entry.description = args[1];
                if (args.size() > 2 && !args[2].empty()) {
                    for (const NoteStyle& style : catalog.styles)
                        if (style.engine == Engine::VSlice && lower(stemOf(style.definition)) == lower(args[2])) {
                            entry.lookStyle = style.id;
                            break;
                        }
                }
                merge(std::move(entry));
            }
        }
    }
    // El aspecto que el lector ya encontro: game/notes/<tipo> en Codename y la
    // textura del tipo (o la recoloracion de serie) en Psych.
    for (const NoteStyle& style : catalog.styles) {
        if (style.use != StyleUse::NoteType) continue;
        NoteTypeEntry entry;
        entry.engine = style.engine;
        entry.name = style.useDetail;
        entry.lookStyle = style.id;
        if (style.engine == Engine::Codename) entry.script = !style.lookScript.empty() ? style.lookScript : style.nameScript;
        if (style.engine == Engine::Psych) {
            psych = true;
            if (!style.sheets.empty() && style.sheets[0].rgbFixed.empty()) entry.texture = style.sheets[0].declared;
        }
        merge(std::move(entry));
    }
    // Codename: los tipos que piden los charts, aunque no tengan script ni
    // imagen (el motor los dibuja con el skin y no hacen nada mas).
    for (const std::string& name : catalog.chartTypes) {
        NoteTypeEntry entry;
        entry.engine = Engine::Codename;
        entry.name = name;
        merge(std::move(entry));
    }
    // Los tipos de serie de Psych (Note.hx:43-50, :199-228), para reconocerlos
    // en un chart. La Hurt Note se deja pasar del lado del jugador y tocarla es
    // un fallo.
    if (psych)
        for (const char* name : {"Alt Animation", "Hey!", "Hurt Note", "GF Sing", "No Animation"}) {
            NoteTypeEntry entry;
            entry.engine = Engine::Psych;
            entry.name = name;
            entry.builtin = true;
            if (lower(name) == "hurt note") {
                entry.bot = BotRule::IgnoresOnPlayerSide;
                entry.hitMisses = true;
            }
            merge(std::move(entry));
        }
    std::stable_sort(types.begin(), types.end(), [](const NoteTypeEntry& a, const NoteTypeEntry& b) {
        if (a.engine != b.engine) return a.engine < b.engine;
        if (a.builtin != b.builtin) return !a.builtin;
        return lower(a.name) < lower(b.name);
    });
    return types;
}

}  // namespace fml::notelab

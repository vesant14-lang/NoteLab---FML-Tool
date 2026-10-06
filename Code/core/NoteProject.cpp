#include "NoteProject.hpp"

#include "../third_party/json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fml::notelab {
namespace {

using json = nlohmann::json;

std::string text(const json& doc, const char* key, const std::string& fallback = {}) {
    return doc.is_object() && doc.contains(key) && doc[key].is_string() ? doc[key].get<std::string>() : fallback;
}

float real(const json& doc, const char* key, float fallback) {
    if (!doc.is_object() || !doc.contains(key) || !doc[key].is_number()) return fallback;
    const double value = doc[key].get<double>();
    return std::isfinite(value) && std::abs(value) <= 100000000.0 ? static_cast<float>(value) : fallback;
}

int whole(const json& doc, const char* key, int fallback) {
    if (!doc.is_object() || !doc.contains(key) || !doc[key].is_number_integer()) return fallback;
    const double value = doc[key].get<double>();
    return value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max() ? doc[key].get<int>() : fallback;
}

bool flag(const json& doc, const char* key, bool fallback) {
    return doc.is_object() && doc.contains(key) && doc[key].is_boolean() ? doc[key].get<bool>() : fallback;
}

// Los enumerados se guardan con la misma clave que el resto del nucleo
// (engineKey, partKey...), para que el archivo se lea sin el codigo delante.
template <typename Enum, typename KeyOf>
Enum enumFrom(const std::string& key, int count, KeyOf keyOf, Enum fallback) {
    for (int i = 0; i < count; ++i)
        if (key == keyOf(static_cast<Enum>(i))) return static_cast<Enum>(i);
    return fallback;
}

json hudJson(const HudAsset& asset) {
    return json{{"image", asset.image}, {"declared", asset.declared}, {"sound", asset.sound},
                {"soundDeclared", asset.soundDeclared}, {"nearby", asset.nearby}, {"scale", asset.scale},
                {"pixel", asset.pixel}, {"imageOptional", asset.imageOptional}, {"inherited", asset.inherited}};
}

HudAsset hudFrom(const json& doc) {
    HudAsset asset;
    asset.image = text(doc, "image");
    asset.declared = text(doc, "declared");
    asset.sound = text(doc, "sound");
    asset.soundDeclared = text(doc, "soundDeclared");
    asset.nearby = text(doc, "nearby");
    asset.scale = real(doc, "scale", 1.0f);
    asset.pixel = flag(doc, "pixel", false);
    asset.imageOptional = flag(doc, "imageOptional", false);
    asset.inherited = flag(doc, "inherited", false);
    return asset;
}

json styleJson(const NoteStyle& style) {
    json sheets = json::array();
    for (const Sheet& sheet : style.sheets)
        sheets.push_back(json{{"image", sheet.image}, {"atlas", sheet.atlas}, {"declared", sheet.declared},
                              {"kind", sheetKindKey(sheet.kind)}, {"columns", sheet.columns}, {"rows", sheet.rows},
                              {"scale", sheet.scale}, {"offsetX", sheet.offsetX}, {"offsetY", sheet.offsetY},
                              {"alpha", sheet.alpha}, {"pixel", sheet.pixel}, {"rgbFixed", sheet.rgbFixed}});
    json parts = json::array();
    for (const PartBinding& part : style.parts)
        parts.push_back(json{{"part", partKey(part.part)}, {"direction", part.direction}, {"variant", part.variant},
                             {"sheet", part.sheet}, {"inherited", part.inherited}, {"unread", part.unread},
                             {"animation", json{{"prefix", part.animation.prefix}, {"alternatives", part.animation.alternatives},
                                                {"indices", part.animation.indices}, {"fps", part.animation.fps},
                                                {"loop", part.animation.loop}, {"offsetX", part.animation.offsetX},
                                                {"offsetY", part.animation.offsetY}}}});
    json countdown = json::array(), judgements = json::array(), digits = json::array();
    for (const HudAsset& asset : style.countdown) countdown.push_back(hudJson(asset));
    for (const HudAsset& asset : style.judgements) judgements.push_back(hudJson(asset));
    for (const HudAsset& asset : style.digits) digits.push_back(hudJson(asset));
    json sounds = json::array();
    for (const auto& sound : style.sounds) sounds.push_back({{"name", sound.name}, {"path", sound.path}});
    return json{{"engine", engineKey(style.engine)}, {"id", style.id}, {"name", style.name}, {"author", style.author},
                {"definition", style.definition}, {"scope", style.scope}, {"fallback", style.fallback},
                {"use", styleUseKey(style.use)}, {"useDetail", style.useDetail}, {"rgbPalette", style.rgbPalette},
                {"pixel", style.pixel}, {"referenced", style.referenced}, {"letteredNaming", style.letteredNaming},
                {"forkNaming", style.forkNaming}, {"lookScript", style.lookScript}, {"sheets", sheets},
                {"parts", parts}, {"hasHud", style.hasHud},
                {"countdown", countdown}, {"judgements", judgements}, {"digits", digits}, {"combo", hudJson(style.combo)}, {"sounds", sounds}};
}

NoteStyle styleFrom(const json& doc) {
    NoteStyle style;
    style.engine = enumFrom<Engine>(text(doc, "engine"), 3, engineKey, Engine::Codename);
    style.id = text(doc, "id");
    style.name = text(doc, "name");
    style.author = text(doc, "author");
    style.definition = text(doc, "definition");
    style.scope = text(doc, "scope");
    style.fallback = text(doc, "fallback");
    style.use = enumFrom<StyleUse>(text(doc, "use"), 5, styleUseKey, StyleUse::Declared);
    style.useDetail = text(doc, "useDetail");
    style.rgbPalette = flag(doc, "rgbPalette", false);
    style.pixel = flag(doc, "pixel", false);
    style.referenced = flag(doc, "referenced", true);
    style.letteredNaming = flag(doc, "letteredNaming", false);
    style.forkNaming = flag(doc, "forkNaming", false);
    style.lookScript = text(doc, "lookScript");
    style.hasHud = flag(doc, "hasHud", false);
    if (doc.contains("sheets") && doc["sheets"].is_array())
        for (const json& item : doc["sheets"]) {
            Sheet sheet;
            sheet.image = text(item, "image");
            sheet.atlas = text(item, "atlas");
            sheet.declared = text(item, "declared");
            sheet.kind = enumFrom<SheetKind>(text(item, "kind"), 5, sheetKindKey, SheetKind::Sparrow);
            sheet.columns = whole(item, "columns", 0);
            sheet.rows = whole(item, "rows", 0);
            sheet.scale = real(item, "scale", 1.0f);
            sheet.offsetX = real(item, "offsetX", 0.0f);
            sheet.offsetY = real(item, "offsetY", 0.0f);
            sheet.alpha = real(item, "alpha", 1.0f);
            sheet.pixel = flag(item, "pixel", false);
            if (item.contains("rgbFixed") && item["rgbFixed"].is_array())
                for (const json& color : item["rgbFixed"])
                    if (color.is_number_unsigned() || color.is_number_integer()) sheet.rgbFixed.push_back(color.get<std::uint32_t>());
            style.sheets.push_back(sheet);
        }
    if (doc.contains("parts") && doc["parts"].is_array())
        for (const json& item : doc["parts"]) {
            PartBinding part;
            part.part = enumFrom<Part>(text(item, "part"), kPartCount, partKey, Part::Note);
            part.direction = whole(item, "direction", 0);
            part.variant = whole(item, "variant", 0);
            part.sheet = whole(item, "sheet", -1);
            part.inherited = flag(item, "inherited", false);
            part.unread = flag(item, "unread", false);
            const json animation = item.contains("animation") ? item["animation"] : json::object();
            part.animation.prefix = text(animation, "prefix");
            if (animation.contains("alternatives") && animation["alternatives"].is_array())
                for (const json& alternative : animation["alternatives"])
                    if (alternative.is_string()) part.animation.alternatives.push_back(alternative.get<std::string>());
            if (animation.contains("indices") && animation["indices"].is_array())
                for (const json& index : animation["indices"])
                    if (index.is_number_integer()) part.animation.indices.push_back(index.get<int>());
            part.animation.fps = real(animation, "fps", 24.0f);
            part.animation.loop = flag(animation, "loop", false);
            part.animation.offsetX = real(animation, "offsetX", 0.0f);
            part.animation.offsetY = real(animation, "offsetY", 0.0f);
            style.parts.push_back(part);
        }
    auto readArray = [&](const char* key, auto& target) {
        if (!doc.contains(key) || !doc[key].is_array()) return;
        for (size_t i = 0; i < target.size() && i < doc[key].size(); ++i) target[i] = hudFrom(doc[key][i]);
    };
    readArray("countdown", style.countdown);
    readArray("judgements", style.judgements);
    readArray("digits", style.digits);
    if (doc.contains("combo")) style.combo = hudFrom(doc["combo"]);
    if (doc.contains("sounds") && doc["sounds"].is_array())
        for (const auto& sound : doc["sounds"])
            if (style.sounds.size() < 128) style.sounds.push_back({text(sound, "name"), text(sound, "path")});
    return style;
}

json layerJson(const HudLayer& layer) {
    return json{{"visible", layer.visible}, {"scale", layer.scale}, {"alpha", layer.alpha}, {"x", layer.x}, {"y", layer.y}};
}

HudLayer layerFrom(const json& doc) {
    HudLayer layer;
    layer.visible = flag(doc, "visible", true);
    layer.scale = std::clamp(real(doc, "scale", 1.0f), 0.25f, 3.0f);
    layer.alpha = std::clamp(real(doc, "alpha", 1.0f), 0.1f, 1.0f);
    layer.x = std::clamp(real(doc, "x", 0.0f), -1280.0f, 1280.0f);
    layer.y = std::clamp(real(doc, "y", 0.0f), -1280.0f, 1280.0f);
    return layer;
}

json customJson(const CustomRecipe& recipe) {
    json resources = json::array(), assignments = json::array();
    for (const auto& r : recipe.resources) {
        json files = json::array(), animations = json::array(), regions = json::array();
        for (const auto& file : r.input.files) files.push_back(file.u8string());
        for (const auto& a : r.input.animations) animations.push_back({{"name", a.name}, {"frames", a.frames}});
        for (const auto& b : r.regions) regions.push_back({b.x, b.y, b.w, b.h});
        resources.push_back({{"kind", static_cast<int>(r.input.kind)}, {"path", r.input.path.u8string()}, {"atlas", r.input.atlas.u8string()},
            {"label", r.input.label}, {"files", files}, {"animations", animations}, {"cellWidth", r.cellWidth}, {"cellHeight", r.cellHeight}, {"regions", regions}});
    }
    for (const auto& a : recipe.assignments) assignments.push_back({{"role", static_cast<int>(a.role)}, {"resource", a.resource},
        {"animation", a.animation}, {"order", a.order}, {"part", partKey(a.part)}, {"direction", a.direction}, {"variant", a.variant},
        {"hudIndex", a.hudIndex}, {"soundName", a.soundName}, {"fps", a.fps}, {"scale", a.scale}, {"offsetX", a.offsetX},
        {"offsetY", a.offsetY}, {"loop", a.loop}, {"pixel", a.pixel}});
    return {{"version", 1}, {"noteType", recipe.noteType}, {"type", recipe.type}, {"resources", resources}, {"assignments", assignments}};
}

CustomRecipe customFrom(const json& doc) {
    CustomRecipe out; out.noteType = flag(doc, "noteType", false); out.type = text(doc, "type");
    if (doc.contains("resources") && doc["resources"].is_array()) for (const auto& item : doc["resources"]) {
        if (out.resources.size() >= 128 || !item.is_object()) break;
        CustomResource r; r.input.kind = static_cast<ImportKind>(std::clamp(whole(item, "kind", 6), 0, 6));
        r.input.path = std::filesystem::u8path(text(item, "path")); r.input.atlas = std::filesystem::u8path(text(item, "atlas"));
        r.input.label = text(item, "label"); r.cellWidth = whole(item, "cellWidth", 0); r.cellHeight = whole(item, "cellHeight", 0);
        if (item.contains("files") && item["files"].is_array()) for (const auto& f : item["files"])
            if (f.is_string() && r.input.files.size() < kCustomFrameLimit) r.input.files.push_back(std::filesystem::u8path(f.get<std::string>()));
        if (item.contains("animations") && item["animations"].is_array()) for (const auto& a : item["animations"])
            if (r.input.animations.size() < kCustomFrameLimit) r.input.animations.push_back({text(a, "name"), whole(a, "frames", 0)});
        if (item.contains("regions") && item["regions"].is_array()) for (const auto& b : item["regions"])
            if (b.is_array() && b.size() == 4 && std::all_of(b.begin(), b.end(), [](const json& n) { return n.is_number_integer(); }) && r.regions.size() < kCustomFrameLimit)
                r.regions.push_back({b[0].get<int>(), b[1].get<int>(), b[2].get<int>(), b[3].get<int>()});
        out.resources.push_back(std::move(r));
    }
    if (doc.contains("assignments") && doc["assignments"].is_array()) for (const auto& item : doc["assignments"]) {
        if (out.assignments.size() >= 192 || !item.is_object()) break;
        CustomAssignment a; a.role = static_cast<CustomRole>(std::clamp(whole(item, "role", 0), 0, 3));
        a.resource = whole(item, "resource", -1); a.animation = whole(item, "animation", 0);
        a.part = enumFrom<Part>(text(item, "part"), kPartCount, partKey, Part::Note);
        a.direction = whole(item, "direction", 0); a.variant = whole(item, "variant", 0); a.hudIndex = whole(item, "hudIndex", 0);
        a.soundName = text(item, "soundName"); a.fps = real(item, "fps", 24); a.scale = real(item, "scale", 0.7f);
        a.offsetX = real(item, "offsetX", 0); a.offsetY = real(item, "offsetY", 0); a.loop = flag(item, "loop", false); a.pixel = flag(item, "pixel", false);
        if (item.contains("order") && item["order"].is_array()) for (const auto& f : item["order"])
            if (f.is_number_integer() && a.order.size() < kCustomFrameLimit) a.order.push_back(f.get<int>());
        out.assignments.push_back(std::move(a));
    }
    return out;
}

json recipeJson(const CreationRecipe& r) {
    return json{{"version", 1}, {"style", r.styleId}, {"base", r.baseStyle}, {"kind", r.kind}, {"font", r.font}, {"sprite", r.sprite}, {"custom", customJson(r.custom)},
        {"arrows", {{"colors", r.arrows.colors}, {"outline", r.arrows.outline}, {"strums", static_cast<int>(r.arrows.strums)},
            {"confirm", static_cast<int>(r.arrows.confirm)}, {"holds", static_cast<int>(r.arrows.holds)},
            {"splashes", r.arrows.splashes}, {"strength", r.arrows.strength}, {"drawn", r.arrows.drawn},
            {"drawnFps", r.arrows.drawnFps}, {"drawnTint", r.arrows.drawnTint}, {"drawnStrums", r.arrows.drawnStrums}, {"drawnRotate", r.arrows.drawnRotate}}},
        {"look", {{"color", static_cast<int>(r.look.color)}, {"one", r.look.one}, {"perDirection", r.look.perDirection},
            {"palette", r.look.palette}, {"strength", r.look.strength}, {"mark", r.look.mark}, {"markImage", r.look.markImage},
            {"markPlace", static_cast<int>(r.look.markPlace)}, {"markSize", r.look.markSize}, {"markAlpha", r.look.markAlpha},
            {"alpha", r.look.alpha}, {"hold", static_cast<int>(r.look.hold)}, {"splash", static_cast<int>(r.look.splash)}}},
        {"rating", {{"texts", r.rating.texts}, {"colors", r.rating.colors}, {"size", r.rating.size},
            {"digitSize", r.rating.digitSize}, {"tilt", r.rating.tiltDeg}, {"spacing", r.rating.spacing},
            {"outline", r.rating.outline}, {"outlineWidth", r.rating.outlineWidth}, {"shadow", r.rating.shadow}, {"pixel", r.rating.pixel}}}};
}

template<size_t N> void recipeColors(const json& doc, const char* key, std::array<std::uint32_t, N>& out) {
    if (!doc.contains(key) || !doc[key].is_array() || doc[key].size() != N) return;
    for (size_t i = 0; i < N; ++i)
        if (doc[key][i].is_number_unsigned()) out[i] = doc[key][i].get<std::uint32_t>();
}

CreationRecipe recipeFrom(const json& doc) {
    CreationRecipe r;
    r.styleId = text(doc, "style"); r.baseStyle = text(doc, "base"); r.kind = text(doc, "kind"); r.font = text(doc, "font");
    r.sprite = text(doc, "sprite");
    const json a = doc.contains("arrows") ? doc["arrows"] : json::object();
    recipeColors(a, "colors", r.arrows.colors);
    if (a.contains("outline") && a["outline"].is_number_unsigned()) r.arrows.outline = a["outline"].get<std::uint32_t>();
    r.arrows.strums = static_cast<StrumLook>(std::clamp(whole(a, "strums", 0), 0, 2));
    r.arrows.confirm = static_cast<ConfirmLook>(std::clamp(whole(a, "confirm", 0), 0, 1));
    r.arrows.holds = static_cast<HoldLook>(std::clamp(whole(a, "holds", 0), 0, 1));
    r.arrows.splashes = flag(a, "splashes", true);
    r.arrows.strength = std::clamp(real(a, "strength", 1.0f), 0.0f, 1.0f);
    if (a.contains("drawn") && a["drawn"].is_array() && a["drawn"].size() <= static_cast<size_t>(kDrawnPieceCount))
        for (size_t i = 0; i < a["drawn"].size(); ++i)
            if (a["drawn"][i].is_string()) r.arrows.drawn[i] = a["drawn"][i].get<std::string>();
    if (a.contains("drawnFps") && a["drawnFps"].is_array() && a["drawnFps"].size() <= static_cast<size_t>(kDrawnPieceCount))
        for (size_t i = 0; i < a["drawnFps"].size(); ++i)
            if (a["drawnFps"][i].is_number_integer()) r.arrows.drawnFps[i] = std::clamp(a["drawnFps"][i].get<int>(), 1, 60);
    r.arrows.drawnTint = flag(a, "drawnTint", true);
    r.arrows.drawnStrums = flag(a, "drawnStrums", true);
    r.arrows.drawnRotate = flag(a, "drawnRotate", true);
    const json l = doc.contains("look") ? doc["look"] : json::object();
    r.look.color = static_cast<TypeColor>(std::clamp(whole(l, "color", 1), 0, 3));
    if (l.contains("one") && l["one"].is_number_unsigned()) r.look.one = l["one"].get<std::uint32_t>();
    recipeColors(l, "perDirection", r.look.perDirection); recipeColors(l, "palette", r.look.palette);
    r.look.strength = std::clamp(real(l, "strength", 0.85f), 0.0f, 1.0f);
    r.look.mark = std::clamp(whole(l, "mark", -1), -1, kBuiltinMarkCount - 1);
    r.look.markImage = text(l, "markImage");
    r.look.markPlace = static_cast<MarkPlace>(std::clamp(whole(l, "markPlace", 0), 0, 2));
    r.look.markSize = std::clamp(real(l, "markSize", 0.46f), 0.01f, 2.0f);
    r.look.markAlpha = std::clamp(real(l, "markAlpha", 1.0f), 0.0f, 1.0f);
    r.look.alpha = std::clamp(real(l, "alpha", 1.0f), 0.0f, 1.0f);
    r.look.hold = static_cast<TypeHold>(std::clamp(whole(l, "hold", 0), 0, 2));
    r.look.splash = static_cast<TypeSplash>(std::clamp(whole(l, "splash", 0), 0, 2));
    const json t = doc.contains("rating") ? doc["rating"] : json::object();
    if (t.contains("texts") && t["texts"].is_array() && t["texts"].size() == 5)
        for (size_t i = 0; i < 5; ++i) if (t["texts"][i].is_string()) r.rating.texts[i] = t["texts"][i].get<std::string>();
    if (t.contains("colors") && t["colors"].is_array() && t["colors"].size() == 5)
        for (size_t i = 0; i < 5; ++i) {
            const json colors{{"pair", t["colors"][i]}};
            recipeColors(colors, "pair", r.rating.colors[i]);
        }
    r.rating.size = std::clamp(real(t, "size", 120.0f), 1.0f, 512.0f);
    r.rating.digitSize = std::clamp(real(t, "digitSize", 110.0f), 1.0f, 512.0f);
    r.rating.tiltDeg = std::clamp(real(t, "tilt", -6.0f), -45.0f, 45.0f);
    r.rating.spacing = std::clamp(real(t, "spacing", 0.0f), -64.0f, 128.0f);
    if (t.contains("outline") && t["outline"].is_number_unsigned()) r.rating.outline = t["outline"].get<std::uint32_t>();
    r.rating.outlineWidth = std::clamp(real(t, "outlineWidth", 13.0f), 0.0f, 64.0f);
    r.rating.shadow = flag(t, "shadow", true); r.rating.pixel = flag(t, "pixel", false);
    if (doc.contains("custom") && doc["custom"].is_object()) r.custom = customFrom(doc["custom"]);
    return r;
}

json programJson(const BlockProgram& program) {
    json nodes = json::array();
    for (const auto& [id, node] : program.nodes) {
        json args = json::array();
        for (const BlockArg& arg : node.args) args.push_back(json{{"value", arg.value}, {"block", arg.block}});
        nodes.push_back(json{{"id", id}, {"key", node.key}, {"args", args}, {"next", node.next}, {"x", node.x}, {"y", node.y}, {"comment", node.comment}});
    }
    json drafts = json::array();
    for (const auto& item : program.drafts) {
        const BlockDraft& d = item.second;
        drafts.push_back({{"key", item.first}, {"engine", static_cast<int>(d.engine)}, {"path", d.path},
            {"text", d.text}, {"baseline", d.baseline}, {"error", d.error}});
    }
    return json{{"version", 2}, {"nextId", program.nextId}, {"tops", program.tops}, {"nodes", nodes},
        {"comment", program.comment}, {"drafts", drafts}};
}

// Lo que venga de fuera pasa por repairProgram: un programa mal formado no
// debe colgar el editor.
BlockProgram programFrom(const json& doc) {
    BlockProgram program;
    if (!doc.is_object()) return program;
    program.nextId = std::max(1, whole(doc, "nextId", 1));
    if (doc.contains("nodes") && doc["nodes"].is_array())
        for (const json& item : doc["nodes"]) {
            if (!item.is_object()) continue;
            const int id = whole(item, "id", -1);
            if (id <= 0 || program.nodes.count(id)) continue;
            BlockNode node;
            node.key = text(item, "key");
            node.comment = text(item, "comment");
            node.next = whole(item, "next", -1);
            node.x = real(item, "x", 0.0f);
            node.y = real(item, "y", 0.0f);
            if (item.contains("args") && item["args"].is_array())
                for (const json& arg : item["args"]) {
                    if (!arg.is_object()) continue;
                    node.args.push_back({text(arg, "value"), whole(arg, "block", -1)});
                }
            program.nodes[id] = std::move(node);
        }
    if (doc.contains("tops") && doc["tops"].is_array())
        for (const json& top : doc["tops"])
            if (top.is_number_integer()) program.tops.push_back(top.get<int>());
    program.comment = text(doc, "comment");
    if (doc.contains("drafts") && doc["drafts"].is_array())
        for (const json& item : doc["drafts"]) {
            if (!item.is_object()) continue;
            BlockDraft d;
            d.engine = static_cast<Engine>(std::clamp(whole(item, "engine", 0), 0, 2));
            d.path = text(item, "path"); d.text = text(item, "text"); d.baseline = text(item, "baseline"); d.error = text(item, "error");
            if (d.text.size() <= 262144 && d.baseline.size() <= 262144 && !d.path.empty())
                program.drafts[std::string(engineKey(d.engine)) + ":" + d.path] = std::move(d);
        }
    repairProgram(program);
    return program;
}

}  // namespace

std::string writeProgram(const BlockProgram& program) { return programJson(program).dump(); }

BlockProgram readProgram(const std::string& text) {
    const json doc = json::parse(text, nullptr, false, true);
    return programFrom(doc);
}

std::string writeStyle(const NoteStyle& style) {
    return styleJson(style).dump(2);
}

std::optional<NoteStyle> readStyle(const std::string& text) {
    const json doc = json::parse(text, nullptr, false, true);
    if (!doc.is_object()) return std::nullopt;
    return styleFrom(doc);
}

std::string writeProject(const NoteProject& project) {
    json sources = json::array();
    for (const ProjectSource& source : project.sources) {
        json imports = json::array();
        for (const ProjectImport& item : source.imports)
            imports.push_back(json{{"image", item.image}, {"atlas", item.atlas}, {"imageVirtual", item.imageVirtual},
                                   {"atlasVirtual", item.atlasVirtual}});
        json edits = json::array();
        for (const ProjectEdit& edit : source.edits) {
            json files = json::array();
            for (const ProjectFile& file : edit.files) files.push_back(json{{"path", file.virtualPath}, {"sha256", file.sha256}});
            edits.push_back(json{{"styleId", edit.styleId}, {"style", styleJson(edit.style)}, {"files", files}, {"created", edit.created}});
        }
        json typeBlocks = json::array();
        for (const ProjectTypeBlocks& type : source.typeBlocks)
            typeBlocks.push_back(json{{"type", type.type}, {"program", programJson(type.program)}});
        json entry{{"path", source.path}, {"imports", imports}, {"edits", edits}, {"typeBlocks", typeBlocks}};
        entry["recipes"] = json::array();
        for (const CreationRecipe& recipe : source.recipes) entry["recipes"].push_back(recipeJson(recipe));
        entry["bots"] = json::array();
        for (const auto& bot : source.bots) entry["bots"].push_back({{"type", bot.first}, {"enabled", bot.second.enabled}, {"opponent", bot.second.opponent}, {"player", bot.second.player}});
        if (!source.mod.empty()) entry["mod"] = source.mod;
        if (source.baseOnly) entry["baseOnly"] = true;
        if (!source.typeLooks.empty()) {
            json looks = json::array();
            for (const auto& look : source.typeLooks) looks.push_back(json{{"type", look.first}, {"style", look.second}});
            entry["typeLooks"] = looks;
        }
        sources.push_back(std::move(entry));
    }
    const ProjectView& view = project.view;
    json doc{{"format", "fml-notelab-project"},
             {"version", project.version},
             {"engineChoice", project.engineChoice},
             {"baseRoot", project.baseRoot},
             {"bases", json{{"codename", project.bases[0]}, {"psych", project.bases[1]}, {"vslice", project.bases[2]}}},
             {"sources", sources},
             {"selection", json{{"source", project.selectedSource}, {"style", project.selectedStyle}, {"type", project.selectedType}}},
             {"song", json{{"source", project.songSource}, {"id", project.songId}, {"difficulty", project.songDifficulty},
                           {"variation", project.songVariation}}},
             {"view", json{{"visibleLines", view.visibleLines}, {"playSide", view.playSide}, {"manual", view.manual},
                           {"downscroll", view.downscroll}, {"chartSpeed", view.chartSpeed}, {"chartSkin", view.chartSkin},
                           {"psychColors", view.psychColors}, {"scrollSpeed", view.scrollSpeed}, {"bpm", view.bpm},
                           {"judgement", layerJson(view.judgement)}, {"combo", layerJson(view.combo)},
                           {"score", layerJson(view.score)}}}};
    json rules = json::array();
    for (const DistributeRule& rule : project.distribute.rules)
        rules.push_back(json{{"type", rule.type}, {"percent", rule.percent}, {"seed", rule.seed}, {"count", rule.count}});
    const DistributeFilter& filter = project.distribute.filter;
    doc["distribute"] = json{{"applied", project.distributed}, {"seed", project.distribute.seed}, {"rules", rules},
                             {"filter", json{{"side", filter.side}, {"lanes", filter.lanes}, {"skipSustains", filter.skipSustains},
                                             {"skipChords", filter.skipChords}, {"minGapMs", filter.minGapMs}, {"replaceExisting", filter.replaceExisting}}}};
    return doc.dump(2);
}

std::optional<NoteProject> readProject(const std::string& text, std::string& error) {
    error.clear();
    if (text.size() > kProjectByteLimit) { error = "project exceeds 16 MiB"; return std::nullopt; }
    try {
    size_t values = 0;
    const json doc = json::parse(text, [&](int depth, json::parse_event_t event, json& value) {
        if (depth > 32 || ++values > 300000) throw std::runtime_error("project structure exceeds safe limits");
        if ((event == json::parse_event_t::value || event == json::parse_event_t::key) && value.is_string() &&
            value.get_ref<const std::string&>().size() > 1024u * 1024u) throw std::runtime_error("project string exceeds 1 MiB");
        return true;
    }, false, true);
    if (!doc.is_object() || notelab::text(doc, "format") != "fml-notelab-project") {
        error = "not a Note Lab project";
        return std::nullopt;
    }
    NoteProject project;
    project.version = whole(doc, "version", 0);
    if (project.version < 1) {
        error = "the project has no version";
        return std::nullopt;
    }
    if (project.version > kProjectVersion) {
        error = "the project was saved by a newer Note Lab (version " + std::to_string(project.version) + ")";
        return std::nullopt;
    }
    project.engineChoice = std::clamp(whole(doc, "engineChoice", 0), 0, 3);
    project.baseRoot = notelab::text(doc, "baseRoot");
    if (doc.contains("bases") && doc["bases"].is_object()) {
        project.bases[0] = notelab::text(doc["bases"], "codename");
        project.bases[1] = notelab::text(doc["bases"], "psych");
        project.bases[2] = notelab::text(doc["bases"], "vslice");
    }
    if (doc.contains("sources") && doc["sources"].is_array())
        for (const json& item : doc["sources"]) {
            ProjectSource source;
            source.path = notelab::text(item, "path");
            source.mod = notelab::text(item, "mod");
            source.baseOnly = item.contains("baseOnly") && item["baseOnly"].is_boolean() && item["baseOnly"].get<bool>();
            if (item.contains("recipes") && item["recipes"].is_array())
                for (const json& entry : item["recipes"]) {
                    if (!entry.is_object() || whole(entry, "version", 0) != 1) continue;
                    CreationRecipe recipe = recipeFrom(entry);
                    if (!recipe.styleId.empty() && (recipe.kind == "arrows" || recipe.kind == "look" || recipe.kind == "rating" || recipe.kind == "custom"))
                        source.recipes.push_back(std::move(recipe));
                }
            if (item.contains("typeLooks") && item["typeLooks"].is_array())
                for (const json& look : item["typeLooks"])
                    if (look.is_object()) source.typeLooks.push_back({notelab::text(look, "type"), notelab::text(look, "style")});
            if (item.contains("bots") && item["bots"].is_array()) for (const auto& b : item["bots"])
                if (b.is_object() && source.bots.size() < 512) source.bots.push_back({notelab::text(b, "type"),
                    {flag(b, "enabled", false), std::clamp(whole(b, "opponent", 0), 0, 2), std::clamp(whole(b, "player", 0), 0, 2)}});
            if (item.contains("imports") && item["imports"].is_array())
                for (const json& entry : item["imports"])
                    source.imports.push_back({notelab::text(entry, "image"), notelab::text(entry, "atlas"),
                                              notelab::text(entry, "imageVirtual"), notelab::text(entry, "atlasVirtual")});
            if (item.contains("edits") && item["edits"].is_array())
                for (const json& entry : item["edits"]) {
                    ProjectEdit edit;
                    edit.styleId = notelab::text(entry, "styleId");
                    edit.created = flag(entry, "created", false);
                    if (entry.contains("style") && entry["style"].is_object()) edit.style = styleFrom(entry["style"]);
                    if (entry.contains("files") && entry["files"].is_array())
                        for (const json& file : entry["files"])
                            edit.files.push_back({notelab::text(file, "path"), notelab::text(file, "sha256")});
                    if (!edit.styleId.empty()) source.edits.push_back(std::move(edit));
                }
            if (item.contains("typeBlocks") && item["typeBlocks"].is_array())
                for (const json& entry : item["typeBlocks"]) {
                    ProjectTypeBlocks type;
                    type.type = notelab::text(entry, "type");
                    if (entry.contains("program")) type.program = programFrom(entry["program"]);
                    if (!type.type.empty()) source.typeBlocks.push_back(std::move(type));
                }
            if (!source.path.empty()) project.sources.push_back(std::move(source));
            if (project.sources.size() > 4) { error = "a project supports up to four sources"; return std::nullopt; }
        }
    const json selection = doc.contains("selection") ? doc["selection"] : json::object();
    project.selectedSource = whole(selection, "source", -1);
    project.selectedStyle = notelab::text(selection, "style");
    project.selectedType = notelab::text(selection, "type");
    const json song = doc.contains("song") ? doc["song"] : json::object();
    project.songSource = whole(song, "source", -1);
    project.songId = notelab::text(song, "id");
    project.songDifficulty = notelab::text(song, "difficulty");
    project.songVariation = notelab::text(song, "variation");
    const json view = doc.contains("view") ? doc["view"] : json::object();
    project.view.visibleLines = std::clamp(whole(view, "visibleLines", 2), 0, 2);
    project.view.playSide = std::clamp(whole(view, "playSide", 1), 0, 1);
    project.view.manual = flag(view, "manual", false);
    project.view.downscroll = flag(view, "downscroll", false);
    project.view.chartSpeed = flag(view, "chartSpeed", true);
    project.view.chartSkin = flag(view, "chartSkin", true);
    project.view.psychColors = flag(view, "psychColors", true);
    project.view.scrollSpeed = std::clamp(real(view, "scrollSpeed", 1.0f), 0.1f, 10.0f);
    project.view.bpm = std::clamp(real(view, "bpm", 120.0f), 30.0f, 400.0f);
    if (view.contains("judgement")) project.view.judgement = layerFrom(view["judgement"]);
    if (view.contains("combo")) project.view.combo = layerFrom(view["combo"]);
    if (view.contains("score")) project.view.score = layerFrom(view["score"]);
    const json distribute = doc.contains("distribute") ? doc["distribute"] : json::object();
    project.distributed = flag(distribute, "applied", false);
    if (distribute.contains("seed") && distribute["seed"].is_number_unsigned()) project.distribute.seed = distribute["seed"].get<std::uint32_t>();
    if (distribute.contains("rules") && distribute["rules"].is_array())
        for (const json& item : distribute["rules"]) {
            DistributeRule rule;
            rule.type = notelab::text(item, "type");
            rule.percent = std::clamp(real(item, "percent", 10.0f), 0.0f, 100.0f);
            rule.count = std::max(-1, whole(item, "count", -1));
            if (item.contains("seed") && item["seed"].is_number_unsigned()) rule.seed = item["seed"].get<std::uint32_t>();
            if (!rule.type.empty()) project.distribute.rules.push_back(rule);
        }
    const json filter = distribute.contains("filter") ? distribute["filter"] : json::object();
    project.distribute.filter.side = std::clamp(whole(filter, "side", 2), 0, 2);
    if (filter.contains("lanes") && filter["lanes"].is_array())
        for (size_t i = 0; i < 4 && i < filter["lanes"].size(); ++i)
            if (filter["lanes"][i].is_boolean()) project.distribute.filter.lanes[i] = filter["lanes"][i].get<bool>();
    project.distribute.filter.skipSustains = flag(filter, "skipSustains", false);
    project.distribute.filter.skipChords = flag(filter, "skipChords", false);
    project.distribute.filter.replaceExisting = flag(filter, "replaceExisting", false);
    project.distribute.filter.minGapMs = std::clamp(static_cast<double>(real(filter, "minGapMs", 0.0f)), 0.0, 10000.0);
    return project;
    } catch (const std::exception& failure) {
        error = failure.what();
        return std::nullopt;
    } catch (...) {
        error = "project could not be decoded";
        return std::nullopt;
    }
}

bool writeProjectFile(const NoteProject& project, const std::filesystem::path& path, std::string& error) {
    namespace fs = std::filesystem;
    error.clear();
    fs::path scratch;
    auto cleanup = [&]() { std::error_code ec; if (!scratch.empty()) fs::remove_all(scratch, ec); };
    try {
        const std::string content = writeProject(project);
        if (!readProject(content, error)) return false;
        std::error_code ec;
        const fs::path destination = fs::absolute(path, ec);
        if (ec || destination.filename().empty() || !fs::is_directory(destination.parent_path(), ec) || ec) {
            error = "the destination folder is not available"; return false;
        }
        if (fs::exists(destination, ec) && !fs::is_regular_file(destination, ec)) {
            error = "the destination is not a regular file"; return false;
        }
        static std::atomic<unsigned long long> serial{0};
        bool reserved = false;
        for (int attempt = 0; attempt < 64 && !reserved; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            scratch = destination.parent_path() / fs::u8path(".notelab-save-" + std::to_string(stamp) + "-" + std::to_string(++serial));
            reserved = fs::create_directory(scratch, ec);
            if (!reserved && ec && ec != std::errc::file_exists) { scratch.clear(); error = ec.message(); return false; }
        }
        if (!reserved) { scratch.clear(); error = "could not reserve a private save file"; return false; }
        const fs::path temporary = scratch / "project.fmlnote";
        std::ofstream file(temporary, std::ios::binary);
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();
        if (!file) { error = "project data could not be written"; file.close(); cleanup(); return false; }
        file.close();
        if (!file) { error = "project file could not be closed"; cleanup(); return false; }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = std::system_category().message(static_cast<int>(GetLastError())); cleanup(); return false;
        }
#else
        fs::rename(temporary, destination, ec);
        if (ec) { error = ec.message(); cleanup(); return false; }
#endif
        cleanup();
        return true;
    } catch (const std::exception& failure) {
        error = failure.what(); cleanup(); return false;
    }
}

}  // namespace fml::notelab

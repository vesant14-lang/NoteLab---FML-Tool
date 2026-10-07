#include "NoteExport.hpp"
#include "NoteCreate.hpp"

#include "NoteImage.hpp"
#include "NotePreview.hpp"
#include "NoteStyleCheck.hpp"
#include "NoteTypes.hpp"
#include "NoteResources.hpp"
#include "../support/io/Vfs.hpp"
#include "../support/runtime/Scene.hpp"
#include "../third_party/json.hpp"
#include "../third_party/miniz/miniz.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fml::notelab {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

const char* const kColors[4] = {"purple", "blue", "green", "red"};
const char* const kUpper[4] = {"LEFT", "DOWN", "UP", "RIGHT"};
const char* const kTitle[4] = {"Left", "Down", "Up", "Right"};

// Lo que Codename y Psych no dejan declarar en un skin: la escala de notas,
// sostenidos y receptores (Codename Flags.hx:139; Psych Note.hx:445 y
// StrumNote.hx:114) y la del HUD (Codename RatingsShowEvent.hx:23 y :31; Psych
// PlayState.hx:2625-2626 y :2650).
constexpr float kFixedNoteScale = 0.7f;
constexpr float kRatingScale = 0.7f;
constexpr float kNumberScale = 0.5f;

// El sostenido de V-Slice: el final ocupa la mitad de la imagen dentro de la
// nota y sobresale hasta el 90 %; en pixel, entero dentro (SustainTrail.hx:106-112, :217-224).
constexpr float kHoldEndOffset = 0.5f;
constexpr float kHoldBottomClip = 0.9f;

const char* const kExportMarker = "notelab-export.json";

std::string lowerAscii(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string frameName(const std::string& base, int index) {
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%04d", index);
    return base + digits;
}

std::string xmlEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// Los float pasan a JSON con cuatro decimales: 0.7f no es 0.699999988.
double tidy(float value) { return std::round(static_cast<double>(value) * 10000.0) / 10000.0; }

std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(value));
    return text;
}

// Un nombre de tipo tal cual lo escribe el chart sirve de nombre de archivo
// salvo los caracteres que Windows no admite.
std::string typeFileName(const std::string& type, bool& changed) {
    std::string out;
    changed = false;
    for (char c : type) {
        if (std::strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32) {
            out += '_';
            changed = true;
        } else {
            out += c;
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
        changed = true;
    }
    return out;
}

std::string engineTitle(Engine engine) {
    switch (engine) {
        case Engine::Codename: return "Codename Engine";
        case Engine::Psych: return "Psych Engine";
        case Engine::VSlice: return "V-Slice";
    }
    return "?";
}

void addText(ExportPackage& package, const std::string& path, const std::string& text) {
    package.files.push_back({path, std::vector<unsigned char>(text.begin(), text.end())});
}

void addBytes(ExportPackage& package, const std::string& path, std::vector<unsigned char> bytes) {
    package.files.push_back({path, std::move(bytes)});
}

void note(ExportPackage& package, Severity severity, const char* code, std::string subject, std::string detail = {}) {
    for (const ExportNote& existing : package.notes)
        if (existing.code == code && existing.subject == subject && existing.detail == detail) return;
    package.notes.push_back({severity, code, std::move(subject), std::move(detail)});
}

// ------------------------------------------------------------------ lectura --

class Reader {
public:
    explicit Reader(const ExportIo& io) : m_io(io) {
        m_atlases.readText = [this](const std::string& path) {
            if (!m_io.readText) return std::string();
            const auto text = m_io.readText(path);
            return text ? *text : std::string();
        };
    }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    const SparrowAtlas* atlas(const std::string& path) { return m_atlases.get(path); }

    const Image* image(const std::string& path) {
        const auto hit = m_images.find(path);
        if (hit != m_images.end()) return &hit->second;
        if (path.empty() || m_failed.count(path) || !m_io.readBytes) return nullptr;
        const auto bytes = m_io.readBytes(path);
        Image decoded;
        if (!bytes || !decodePng(*bytes, decoded)) {
            m_failed.insert(path);
            return nullptr;
        }
        return &m_images.emplace(path, std::move(decoded)).first->second;
    }

    std::optional<std::vector<unsigned char>> bytes(const std::string& path) {
        if (path.empty() || !m_io.readBytes) return std::nullopt;
        return m_io.readBytes(path);
    }

private:
    const ExportIo& m_io;
    AtlasStore m_atlases;
    std::map<std::string, Image> m_images;
    std::set<std::string> m_failed;
};

// Un fotograma de origen: su hoja y su region, con el recorte.
struct SourceFrame {
    const Image* sheet = nullptr;
    std::string sheetPath;
    AtlasFrame frame;
};

// El ancho de la columna `column` de una imagen partida en `columns`, sin
// perder pixeles cuando el ancho no es multiplo.
void columnBounds(int width, int columns, int column, int& x0, int& x1) {
    x0 = static_cast<int>(std::lround(static_cast<double>(width) * column / columns));
    x1 = static_cast<int>(std::lround(static_cast<double>(width) * (column + 1) / columns));
}

std::vector<SourceFrame> sourceFrames(Reader& reader, const NoteStyle& style, const PartBinding& binding) {
    std::vector<SourceFrame> out;
    if (binding.sheet < 0 || static_cast<size_t>(binding.sheet) >= style.sheets.size()) return out;
    const Sheet& sheet = style.sheets[static_cast<size_t>(binding.sheet)];
    const Image* image = reader.image(sheet.image);
    if (!image) return out;
    auto whole = [&](int x, int y, int w, int h) {
        AtlasFrame f;
        f.x = x;
        f.y = y;
        f.w = std::max(1, w);
        f.h = std::max(1, h);
        f.frameW = f.w;
        f.frameH = f.h;
        out.push_back({image, sheet.image, f});
    };
    switch (sheet.kind) {
        case SheetKind::Strip: {
            // Una columna de la tira de sostenidos de V-Slice. Del final solo
            // lo que se dibuja: hasta el 90 % del alto (la fila de abajo es
            // relleno contra el filtrado).
            const int columns = sheet.columns > 0 ? sheet.columns : 8;
            const int column = binding.animation.indices.empty() ? 0 : binding.animation.indices[0];
            if (column < 0 || column >= columns) return out;
            int x0 = 0, x1 = 0;
            columnBounds(image->w, columns, column, x0, x1);
            int h = image->h;
            if (binding.part == Part::HoldEnd && !sheet.pixel)
                h = static_cast<int>(std::lround(image->h * kHoldBottomClip));
            whole(x0, 0, x1 - x0, h);
            return out;
        }
        case SheetKind::Image:
            whole(0, 0, image->w, image->h);
            return out;
        case SheetKind::Grid: {
            const int columns = std::max(1, sheet.columns), rows = std::max(1, sheet.rows);
            const int cellW = image->w / columns, cellH = image->h / rows;
            std::vector<int> cells = binding.animation.indices;
            if (cells.empty()) cells.push_back(0);
            for (int cell : cells)
                if (cell >= 0 && cell < columns * rows) whole((cell % columns) * cellW, (cell / columns) * cellH, cellW, cellH);
            return out;
        }
        case SheetKind::Sparrow:
        case SheetKind::Packer:
            break;
    }
    const SparrowAtlas* atlas = reader.atlas(sheet.atlas);
    if (!atlas) return out;
    for (size_t index : animationFrames(*atlas, binding.animation))
        out.push_back({image, sheet.image, atlas->frames[index]});
    return out;
}

// La caja completa del fotograma (sin recorte), con el contenido en su sitio.
Image frameBox(const SourceFrame& source, const std::uint32_t* palette) {
    Image content = cropFrame(*source.sheet, source.frame);
    if (palette) applyPsychPalette(content, palette);
    const AtlasFrame& f = source.frame;
    if (f.frameW <= 0 || f.frameH <= 0) return content;
    Image box = blankImage(f.frameW, f.frameH);
    blit(box, content, -f.frameX, -f.frameY);
    return box;
}

// ------------------------------------------------------------ atlas de salida --

// Como pasa un fotograma de su hoja al atlas nuevo.
struct Recipe {
    float scale = 1.0f;                     // remuestreo
    bool pixel = false;                     // vecino mas cercano
    const std::uint32_t* palette = nullptr; // paleta de Psych cocida en los pixeles
    int shiftX = 0, shiftY = 0;             // desplazamiento cocido en frameX/frameY
};

struct PackedFrame {
    std::string name;
    int block = -1;
    int frameX = 0, frameY = 0, frameW = 0, frameH = 0;
};

class AtlasOut {
public:
    explicit AtlasOut(std::string path) : m_path(std::move(path)) {}

    const std::string& path() const { return m_path; }
    bool empty() const { return m_frames.empty(); }
    int frameCount() const { return static_cast<int>(m_frames.size()); }

    // Los fotogramas de una animacion, con nombres `base0000`, `base0001`...
    // Dos fotogramas con los mismos pixeles comparten region.
    int add(const std::vector<SourceFrame>& frames, const std::string& base, const Recipe& recipe) {
        int index = 0;
        for (const SourceFrame& source : frames) {
            const AtlasFrame& f = source.frame;
            const int drawW = f.rotated ? f.h : f.w;
            const int drawH = f.rotated ? f.w : f.h;
            const bool resample = std::fabs(recipe.scale - 1.0f) > 0.004f;
            const int outW = resample ? std::max(1, static_cast<int>(std::lround(drawW * recipe.scale))) : drawW;
            const int outH = resample ? std::max(1, static_cast<int>(std::lround(drawH * recipe.scale))) : drawH;
            char key[256];
            std::snprintf(key, sizeof(key), "|%d,%d,%d,%d,%d|%08X%08X%08X|%d,%d,%d", f.x, f.y, f.w, f.h, f.rotated ? 1 : 0,
                          recipe.palette ? recipe.palette[0] : 0u, recipe.palette ? recipe.palette[1] : 0u,
                          recipe.palette ? recipe.palette[2] : 0u, outW, outH, recipe.pixel ? 1 : 0);
            const std::string blockKey = source.sheetPath + key;
            int block = -1;
            const auto found = m_blockOf.find(blockKey);
            if (found != m_blockOf.end()) {
                block = found->second;
            } else {
                Image content = cropFrame(*source.sheet, f);
                if (recipe.palette) applyPsychPalette(content, recipe.palette);
                if (resample) content = resizeImage(content, outW, outH, recipe.pixel);
                block = static_cast<int>(m_blocks.size());
                m_blocks.push_back(std::move(content));
                m_blockOf.emplace(blockKey, block);
            }
            int fx = f.frameX, fy = f.frameY, fw = f.frameW, fh = f.frameH;
            if (fw <= 0 || fh <= 0) {
                fx = 0;
                fy = 0;
                fw = drawW;
                fh = drawH;
            }
            if (resample) {
                fx = static_cast<int>(std::lround(fx * recipe.scale));
                fy = static_cast<int>(std::lround(fy * recipe.scale));
                fw = std::max(1, static_cast<int>(std::lround(fw * recipe.scale)));
                fh = std::max(1, static_cast<int>(std::lround(fh * recipe.scale)));
            }
            PackedFrame packed;
            packed.name = frameName(base, index++);
            packed.block = block;
            packed.frameX = fx + recipe.shiftX;
            packed.frameY = fy + recipe.shiftY;
            packed.frameW = fw;
            packed.frameH = fh;
            m_frames.push_back(std::move(packed));
        }
        return index;
    }

    // PNG y XML de Sparrow. Devuelve false si no cabe en 8192 px.
    bool write(ExportPackage& package) {
        std::vector<std::array<int, 2>> sizes;
        for (const Image& block : m_blocks) sizes.push_back({block.w, block.h});
        const PackResult packed = packRects(sizes, 2);
        if (!packed.ok) {
            note(package, Severity::Error, "FML-EXPORT-024", "sheet:" + m_path);
            return false;
        }
        Image sheet = blankImage(packed.w, packed.h);
        for (size_t i = 0; i < m_blocks.size(); ++i)
            blit(sheet, m_blocks[i], packed.positions[i][0], packed.positions[i][1]);
        const size_t slash = m_path.find_last_of('/');
        const std::string file = slash == std::string::npos ? m_path : m_path.substr(slash + 1);
        std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                          "<!-- Note Lab (Funkin Mod Lab) -->\n"
                          "<TextureAtlas imagePath=\"" + xmlEscape(file) + ".png\">\n";
        for (const PackedFrame& frame : m_frames) {
            const Image& block = m_blocks[static_cast<size_t>(frame.block)];
            const auto& at = packed.positions[static_cast<size_t>(frame.block)];
            xml += "\t<SubTexture name=\"" + xmlEscape(frame.name) + "\" x=\"" + std::to_string(at[0]) + "\" y=\"" +
                   std::to_string(at[1]) + "\" width=\"" + std::to_string(block.w) + "\" height=\"" + std::to_string(block.h) + "\"";
            if (frame.frameX != 0 || frame.frameY != 0 || frame.frameW != block.w || frame.frameH != block.h)
                xml += " frameX=\"" + std::to_string(frame.frameX) + "\" frameY=\"" + std::to_string(frame.frameY) +
                       "\" frameWidth=\"" + std::to_string(frame.frameW) + "\" frameHeight=\"" + std::to_string(frame.frameH) + "\"";
            xml += "/>\n";
        }
        xml += "</TextureAtlas>\n";
        addBytes(package, m_path + ".png", encodePng(sheet));
        addText(package, m_path + ".xml", xml);
        package.frames += frameCount();
        ++package.atlases;
        return true;
    }

private:
    std::string m_path;
    std::vector<Image> m_blocks;
    std::map<std::string, int> m_blockOf;
    std::vector<PackedFrame> m_frames;
};

// ------------------------------------------------------------------ contexto --

struct Ctx {
    const NoteStyle& style;
    ExportOptions options;
    Reader& reader;
    ExportPackage& package;
    // La paleta RGB de Psych se cuece en los pixeles: el destino dibuja la
    // textura tal cual (Codename, V-Slice, o un tipo de Psych con textura
    // propia, que apaga el sombreador: Note.hx:369).
    bool bakePalette = false;
    // El destino es Psych y el motor recolorea: la textura se queda en rojo,
    // verde y azul.
    bool keepsRgb = false;
    // Lo que el aspecto de un tipo de nota pone en los archivos del tipo.
    TypeLook look;
};

// El codigo del tipo de nota: sus bloques y lo que aporta el aspecto, en los
// archivos que lee el motor (NoteBlocks.hpp). Los choques pasan a notas y se
// avisa si el mod ya tiene ese archivo: el del paquete lo sustituiria.
void addTypeCode(Ctx& c, const std::string& fileName) {
    size_t totalMedia = 0;
    for (const auto& resource : blockResourceUses(c.options.blocks)) {
        if (!resource.active) continue;
        const std::string destination = resourcePackagePath(resource.path, resource.kind);
        auto fail = [&](const std::string& en, const std::string& es) {
            c.package.notes.push_back({Severity::Error, "FML-EXPORT-043", resource.path, "", en, es});
        };
        if (destination.empty()) { fail("Invalid resource reference.", "Referencia de recurso inválida."); continue; }
        const std::string ext = lowerAscii(fs::u8path(destination).extension().u8string());
        if ((resource.kind == ResourceKind::Image && ext != ".png") ||
            (resource.kind == ResourceKind::Sound && ext != ".ogg") ||
            (resource.kind == ResourceKind::Video && ext != ".mp4")) {
            fail("Engine-safe media exports require PNG images, OGG sounds and MP4 videos; convert this resource first.",
                 "La exportación segura requiere imágenes PNG, sonidos OGG y vídeos MP4; convierte este recurso primero.");
            continue;
        }
        auto bytes = c.reader.bytes(resource.path);
        if (!bytes && resource.path.find('.') == std::string::npos) bytes = c.reader.bytes(destination);
        if (!bytes || bytes->empty()) { fail("Referenced mod resource was not found.", "No se encontró el recurso del mod referenciado."); continue; }
        const size_t limit = resource.kind == ResourceKind::Video ? 128u * 1024u * 1024u : 32u * 1024u * 1024u;
        if (bytes->size() > limit || totalMedia + bytes->size() > 256u * 1024u * 1024u) {
            fail("Media exceeds the export size limit.", "El recurso excede el límite de tamaño de exportación."); continue;
        }
        if (resource.kind == ResourceKind::Image) {
            Image decoded;
            const std::array<unsigned char, 8> signature = {137, 'P', 'N', 'G', 13, 10, 26, 10};
            if (bytes->size() < signature.size() || !std::equal(signature.begin(), signature.end(), bytes->begin()) || !decodePng(*bytes, decoded)) {
                fail("Image is not a valid PNG.", "La imagen no es un PNG válido."); continue;
            }
        } else if (resource.kind == ResourceKind::Sound) {
            const std::array<unsigned char, 7> vorbis = {1, 'v', 'o', 'r', 'b', 'i', 's'};
            if (bytes->size() < 64 || std::memcmp(bytes->data(), "OggS", 4) != 0 ||
                std::search(bytes->begin(), bytes->begin() + std::min<size_t>(bytes->size(), 4096), vorbis.begin(), vorbis.end()) ==
                bytes->begin() + std::min<size_t>(bytes->size(), 4096)) {
                fail("Sound must contain OGG Vorbis audio.", "El sonido debe contener audio OGG Vorbis."); continue;
            }
        } else if (bytes->size() < 12 || std::memcmp(bytes->data() + 4, "ftyp", 4) != 0) {
            fail("Video has no valid MP4 container header.", "El vídeo no contiene una cabecera MP4 válida."); continue;
        }
        auto existing = std::find_if(c.package.files.begin(), c.package.files.end(), [&](const auto& f) { return lowerAscii(f.path) == lowerAscii(destination); });
        if (existing != c.package.files.end()) {
            if (existing->bytes != *bytes) fail("Two resources map to the same engine path; rename one before exporting.", "Dos recursos usan la misma ruta del motor; renombra uno antes de exportar.");
            continue;
        }
        totalMedia += bytes->size();
        addBytes(c.package, destination, std::move(*bytes));
        if (resource.kind == ResourceKind::Image) ++c.package.images;
        if (resource.kind == ResourceKind::Sound) ++c.package.sounds;
    }
    const BlockCode code = generateBlocks(c.options.target, c.options.noteType, fileName, c.options.blocks, c.look);
    for (const BlockFile& file : code.files) {
        for (const std::string& existing : c.options.existingFiles)
            if (endsWith(lowerAscii(existing), lowerAscii(file.path)))
                note(c.package, Severity::Warning, "FML-EXPORT-035", file.path, existing);
        addText(c.package, file.path, file.text);
    }
    for (const BlockConflict& conflict : code.conflicts) {
        ExportNote entry{conflict.severity, "FML-EXPORT-034", conflict.key, "", conflict.en, conflict.es};
        c.package.notes.push_back(std::move(entry));
    }
    if (const int active = activeBlocks(c.options.blocks); active > 0)
        note(c.package, Severity::Info, "FML-EXPORT-036", "blocks", std::to_string(active));
}

const Sheet* sheetOf(const NoteStyle& style, const PartBinding& binding) {
    if (binding.sheet < 0 || static_cast<size_t>(binding.sheet) >= style.sheets.size()) return nullptr;
    return &style.sheets[static_cast<size_t>(binding.sheet)];
}

bool isPixel(const Ctx& c, const Sheet& sheet) { return sheet.pixel || c.style.pixel; }

const std::uint32_t* paletteFor(const Ctx& c, const Sheet& sheet, const PartBinding& binding) {
    // El receptor en reposo no se recolorea nunca (StrumNote.hx:169).
    if (!c.bakePalette || binding.part == Part::StrumStatic) return nullptr;
    if (sheet.rgbFixed.size() == 3) return sheet.rgbFixed.data();
    return psychDefaultPalette(binding.direction, isPixel(c, sheet));
}

std::vector<const PartBinding*> bindingsOf(const NoteStyle& style, Part part, int direction) {
    std::vector<const PartBinding*> out;
    for (const PartBinding& binding : style.parts)
        if (binding.part == part && binding.direction == direction) out.push_back(&binding);
    std::stable_sort(out.begin(), out.end(), [](const PartBinding* a, const PartBinding* b) { return a->variant < b->variant; });
    return out;
}

bool hasPart(const NoteStyle& style, Part part, bool ownOnly = false) {
    return std::any_of(style.parts.begin(), style.parts.end(),
                       [&](const PartBinding& b) { return b.part == part && (!ownOnly || !b.inherited); });
}

// Codename y Psych fijan el FPS y el bucle de cada pieza en su codigo
// (Codename Note.hx:163-183, StrumLine.hx:408-410; Psych Note.hx:436-441,
// StrumNote.hx:119-121): addByPrefix sin mas es 30 FPS en bucle.
void fixedTiming(Engine target, Part part, float& fps, bool& loop) {
    fps = 30.0f;
    loop = true;
    if (part == Part::HoldPiece || part == Part::HoldEnd) fps = target == Engine::Psych ? 24.0f : 30.0f;
    if (part == Part::StrumPress || part == Part::StrumConfirm) {
        fps = 24.0f;
        loop = false;
    }
}

// Una pieza con nombre fijo en Codename y Psych: su escala, su paleta y su
// offset van cocidos en los pixeles y en el recorte.
int addFixedPart(Ctx& c, AtlasOut& atlas, Part part, int direction, const std::string& base) {
    const PartBinding* binding = findPart(c.style, part, direction, 0);
    const std::string subject = std::string(partKey(part)) + "/" + directionKey(direction);
    if (!binding) return 0;
    const Sheet* sheet = sheetOf(c.style, *binding);
    const std::vector<SourceFrame> frames = sourceFrames(c.reader, c.style, *binding);
    if (!sheet || frames.empty()) {
        note(c.package, Severity::Warning, "FML-EXPORT-001", subject);
        return 0;
    }
    Recipe recipe;
    recipe.pixel = isPixel(c, *sheet);
    recipe.palette = paletteFor(c, *sheet, *binding);
    recipe.scale = (sheet->scale > 0.0f ? sheet->scale : kFixedNoteScale) / kFixedNoteScale;
    const float offsetX = sheet->offsetX + binding->animation.offsetX;
    const float offsetY = sheet->offsetY + binding->animation.offsetY;
    // Se dibuja en posicion - offset; en el recorte, el contenido se mueve lo
    // mismo si frameX crece offset / escala.
    recipe.shiftX = static_cast<int>(std::lround(offsetX / kFixedNoteScale));
    recipe.shiftY = static_cast<int>(std::lround(offsetY / kFixedNoteScale));
    if (recipe.shiftX != 0 || recipe.shiftY != 0) note(c.package, Severity::Info, "FML-EXPORT-007", subject);
    if (std::fabs(recipe.scale - 1.0f) > 0.004f)
        note(c.package, Severity::Info, "FML-EXPORT-006", "sheet:" + sheet->declared, number(sheet->scale));
    if (recipe.palette) note(c.package, Severity::Info, "FML-EXPORT-005", "sheet:" + sheet->declared);
    if (recipe.pixel) note(c.package, Severity::Info, "FML-EXPORT-014", "sheet:" + sheet->declared);
    if (sheet->alpha < 0.999f) note(c.package, Severity::Info, "FML-EXPORT-015", "sheet:" + sheet->declared, number(sheet->alpha));
    float fps = 0.0f;
    bool loop = false;
    fixedTiming(c.options.target, part, fps, loop);
    if (frames.size() > 1 && (std::fabs(binding->animation.fps - fps) > 0.5f || binding->animation.loop != loop))
        note(c.package, Severity::Info, "FML-EXPORT-008", subject, number(fps));
    return atlas.add(frames, base, recipe);
}

// --------------------------------------------------------- salpicaduras --

struct SplashAnim {
    const PartBinding* binding = nullptr;
    int direction = 0, variant = 0;
    std::string base;          // nombre en el atlas nuevo
    float offsetX = 0.0f, offsetY = 0.0f;   // ya convertidos al destino
    int boxW = 0, boxH = 0;
};

// Las salpicaduras de un estilo en un atlas propio, con los nombres que da
// `nameOf(direccion, variante)`. `targetScale` es la escala con que las dibuja
// el destino (Psych: 1, cocida en los pixeles).
std::vector<SplashAnim> addSplashes(Ctx& c, AtlasOut& atlas, float targetScale, int maxVariants,
                                    const std::function<std::string(int, int)>& nameOf) {
    std::vector<SplashAnim> out;
    bool converted = false, verbatim = false;
    for (int d = 0; d < 4; ++d) {
        int variant = 0;
        for (const PartBinding* binding : bindingsOf(c.style, Part::Splash, d)) {
            const std::string subject = partSubject(*binding);
            if (variant >= maxVariants) {
                note(c.package, Severity::Warning, "FML-EXPORT-016", subject, std::to_string(maxVariants));
                continue;
            }
            const Sheet* sheet = sheetOf(c.style, *binding);
            const std::vector<SourceFrame> frames = sourceFrames(c.reader, c.style, *binding);
            if (!sheet || frames.empty()) {
                note(c.package, Severity::Warning, "FML-EXPORT-001", subject);
                continue;
            }
            Recipe recipe;
            recipe.pixel = isPixel(c, *sheet);
            recipe.palette = paletteFor(c, *sheet, *binding);
            recipe.scale = (sheet->scale > 0.0f ? sheet->scale : 1.0f) / targetScale;
            if (recipe.palette) note(c.package, Severity::Info, "FML-EXPORT-005", "sheet:" + sheet->declared);
            SplashAnim anim;
            anim.binding = binding;
            anim.direction = d;
            anim.variant = variant;
            anim.base = nameOf(d, variant);
            const AtlasFrame& first = frames.front().frame;
            const int srcW = first.frameW > 0 ? first.frameW : (first.rotated ? first.h : first.w);
            const int srcH = first.frameH > 0 ? first.frameH : (first.rotated ? first.w : first.h);
            anim.boxW = static_cast<int>(std::lround(srcW * recipe.scale));
            anim.boxH = static_cast<int>(std::lround(srcH * recipe.scale));
            anim.offsetX = sheet->offsetX + binding->animation.offsetX;
            anim.offsetY = sheet->offsetY + binding->animation.offsetY;
            // Mismo sitio respecto al receptor (splashAnchor, NotePreview.hpp):
            // offset destino = offset origen + ancla destino - ancla origen.
            float fromX = 0.0f, fromY = 0.0f, toX = 0.0f, toY = 0.0f;
            if (c.style.engine != c.options.target) {
                if (splashAnchor(c.style.engine, static_cast<float>(srcW), static_cast<float>(srcH), fromX, fromY) &&
                    splashAnchor(c.options.target, static_cast<float>(anim.boxW), static_cast<float>(anim.boxH), toX, toY)) {
                    anim.offsetX += toX - fromX;
                    anim.offsetY += toY - fromY;
                    converted = true;
                } else {
                    verbatim = true;
                }
            }
            atlas.add(frames, anim.base, recipe);
            out.push_back(anim);
            ++variant;
        }
    }
    if (converted) note(c.package, Severity::Info, "FML-EXPORT-018", "splash");
    if (verbatim) note(c.package, Severity::Info, "FML-EXPORT-017", "splash");
    return out;
}

float splashSheetScale(const Ctx& c) {
    for (const PartBinding& binding : c.style.parts)
        if (binding.part == Part::Splash)
            if (const Sheet* sheet = sheetOf(c.style, binding)) return sheet->scale > 0.0f ? sheet->scale : 1.0f;
    return 1.0f;
}

const Sheet* firstSheet(const Ctx& c, Part part) {
    for (const PartBinding& binding : c.style.parts)
        if (binding.part == part)
            if (const Sheet* sheet = sheetOf(c.style, binding)) return sheet;
    return nullptr;
}

// ------------------------------------------------------------------ HUD --

struct HudTarget {
    std::string image;      // ruta del PNG en el paquete; vacia = el destino no lo tiene
    std::string declared;   // V-Slice: como se declara en el notestyle
    float engineScale = 1.0f;
};

// Una imagen del HUD: se copia tal cual o, si el destino la dibuja a otra
// escala fija, se remuestrea. En V-Slice la escala se declara.
bool exportHudImage(Ctx& c, const HudAsset& asset, const std::string& subject, const HudTarget& target,
                    float sourceEngineScale, float& declaredScale) {
    if (asset.image.empty()) return false;
    const float effective = sourceEngineScale * (asset.scale > 0.0f ? asset.scale : 1.0f);
    declaredScale = effective;
    float k = 1.0f;
    if (c.options.target != Engine::VSlice) k = effective / target.engineScale;
    auto bytes = c.reader.bytes(asset.image);
    if (!bytes) {
        note(c.package, Severity::Warning, "FML-EXPORT-020", subject, asset.image);
        return false;
    }
    if (std::fabs(k - 1.0f) > 0.004f) {
        Image image;
        if (!decodePng(*bytes, image)) {
            note(c.package, Severity::Warning, "FML-EXPORT-020", subject, asset.image);
            return false;
        }
        image = resizeImage(image, static_cast<int>(std::lround(image.w * k)), static_cast<int>(std::lround(image.h * k)),
                            asset.pixel);
        *bytes = encodePng(image);
        // Una nota por escala, no una por imagen.
        note(c.package, Severity::Info, "FML-EXPORT-032", "hud", number(effective));
    }
    if (asset.pixel && c.options.target != Engine::VSlice) note(c.package, Severity::Info, "FML-EXPORT-014", "hud");
    addBytes(c.package, target.image, std::move(*bytes));
    ++c.package.images;
    return true;
}

// Los sonidos van en OGG en los tres motores de escritorio (Paths.sound).
bool exportHudSound(Ctx& c, const HudAsset& asset, const std::string& subject, const std::string& target) {
    if (asset.sound.empty()) return false;
    if (!endsWith(lowerAscii(asset.sound), ".ogg")) {
        note(c.package, Severity::Warning, "FML-EXPORT-013", subject + ":sound", asset.sound);
        return false;
    }
    auto bytes = c.reader.bytes(asset.sound);
    if (!bytes) {
        note(c.package, Severity::Warning, "FML-EXPORT-020", subject + ":sound", asset.sound);
        return false;
    }
    addBytes(c.package, target, std::move(*bytes));
    ++c.package.sounds;
    return true;
}

float sourceHudScale(Engine engine, float fixedScale) { return engine == Engine::VSlice ? 1.0f : fixedScale; }

// Codename y Psych: el HUD tiene rutas fijas y reemplaza el del mod.
void exportFixedHud(Ctx& c) {
    const bool codename = c.options.target == Engine::Codename;
    const std::string images = codename ? "images/game/score/" : "images/";
    float unused = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const HudAsset& asset = c.style.judgements[static_cast<size_t>(i)];
        if (asset.inherited) continue;
        exportHudImage(c, asset, std::string("judgement/") + judgementKey(i),
                       {images + judgementKey(i) + ".png", {}, kRatingScale}, sourceHudScale(c.style.engine, kRatingScale), unused);
    }
    if (!c.style.combo.inherited)
        exportHudImage(c, c.style.combo, "combo", {images + "combo.png", {}, kRatingScale},
                       sourceHudScale(c.style.engine, kRatingScale), unused);
    for (int i = 0; i < 10; ++i) {
        const HudAsset& asset = c.style.digits[static_cast<size_t>(i)];
        if (asset.inherited) continue;
        exportHudImage(c, asset, "digit/" + std::to_string(i), {images + "num" + std::to_string(i) + ".png", {}, kNumberScale},
                       sourceHudScale(c.style.engine, kNumberScale), unused);
    }
    // Flags.hx:200 y :247 en Codename; Psych, `ready`/`set`/`go` e `intro3`...
    static const char* sprites[4] = {nullptr, "ready", "set", "go"};
    static const char* sounds[4] = {"intro3", "intro2", "intro1", "introGo"};
    for (int i = 0; i < 4; ++i) {
        const HudAsset& step = c.style.countdown[static_cast<size_t>(i)];
        if (step.inherited) continue;
        const std::string subject = std::string("countdown/") + countdownKey(i);
        if (sprites[i]) {
            exportHudImage(c, step, subject, {(codename ? "images/game/" : "images/") + std::string(sprites[i]) + ".png", {}, 1.0f},
                           1.0f, unused);
        } else if (!step.image.empty()) {
            note(c.package, Severity::Info, "FML-EXPORT-012", subject);
        }
        exportHudSound(c, step, subject, std::string("sounds/") + sounds[i] + ".ogg");
    }
}

// ------------------------------------------------------------------ Codename --

void exportCodename(Ctx& c) {
    const ExportOptions& o = c.options;
    const bool modSkin = o.role == ExportRole::ModSkin;
    bool renamed = false;
    const std::string file = modSkin ? "default" : typeFileName(o.noteType, renamed);
    if (renamed) note(c.package, Severity::Warning, "FML-EXPORT-023", o.noteType, file);
    if (o.notes) {
        AtlasOut atlas("images/game/notes/" + file);
        // Los nombres que busca el motor (Note.hx:163-183, StrumLine.hx:408-410);
        // el final morado con la errata del juego base, que prueba primero.
        for (int d = 0; d < 4; ++d) {
            addFixedPart(c, atlas, Part::Note, d, kColors[d]);
            addFixedPart(c, atlas, Part::HoldPiece, d, std::string(kColors[d]) + " hold piece");
            addFixedPart(c, atlas, Part::HoldEnd, d, d == 0 ? std::string("pruple end hold") : std::string(kColors[d]) + " hold end");
        }
        if (modSkin) {
            if (!hasPart(c.style, Part::StrumStatic)) note(c.package, Severity::Error, "FML-EXPORT-002", "strumStatic");
            for (int d = 0; d < 4; ++d) {
                const std::string dir = directionKey(d);
                addFixedPart(c, atlas, Part::StrumStatic, d, std::string("arrow") + kUpper[d]);
                addFixedPart(c, atlas, Part::StrumPress, d, dir + " press");
                addFixedPart(c, atlas, Part::StrumConfirm, d, dir + " confirm");
            }
        }
        if (!atlas.empty()) atlas.write(c.package);
    }
    if (o.splashes && hasPart(c.style, Part::Splash)) {
        AtlasOut atlas("images/game/splashes/" + file);
        const float scale = splashSheetScale(c);
        const std::vector<SplashAnim> anims = addSplashes(c, atlas, scale, 99, [](int d, int v) {
            return "note impact " + std::to_string(v + 1) + " " + kColors[d];
        });
        if (!anims.empty() && atlas.write(c.package)) {
            const Sheet* sheet = firstSheet(c, Part::Splash);
            // SplashGroup.hx:32-112: sprite, scale, alpha y antialiasing; una
            // <strum> por direccion con sus variantes.
            std::string xml = "<!DOCTYPE codename-engine-splashes>\n<!-- Note Lab (Funkin Mod Lab) -->\n"
                              "<splashes sprite=\"game/splashes/" + xmlEscape(file) + "\" scale=\"" + number(scale) + "\"";
            if (sheet && sheet->alpha < 0.999f) xml += " alpha=\"" + number(sheet->alpha) + "\"";
            if (sheet && isPixel(c, *sheet)) xml += " antialiasing=\"false\"";
            xml += ">\n";
            for (int d = 0; d < 4; ++d) {
                xml += "\t<strum id=\"" + std::to_string(d) + "\">\n";
                for (const SplashAnim& anim : anims) {
                    if (anim.direction != d) continue;
                    xml += "\t\t<anim name=\"splash" + std::string(directionKey(d)) + std::to_string(anim.variant + 1) +
                           "\" anim=\"" + xmlEscape(anim.base) + "\" fps=\"" + number(anim.binding->animation.fps) + "\"";
                    if (anim.binding->animation.loop) xml += " loop=\"true\"";
                    xml += " x=\"" + number(anim.offsetX) + "\" y=\"" + number(anim.offsetY) + "\"/>\n";
                }
                xml += "\t</strum>\n";
            }
            xml += "</splashes>\n";
            addText(c.package, "data/splashes/" + file + ".xml", xml);
            // El script del tipo se carga antes de crear las notas
            // (PlayState.hx:797-806 y :864-865): la salpicadura se elige al
            // crear cada nota, y el motor la precarga (Note.hx:216).
            if (!modSkin) c.look.codenameSplash = file;
        }
    }
    if (o.hud && c.style.hasHud) {
        if (modSkin) exportFixedHud(c);
        else note(c.package, Severity::Info, "FML-EXPORT-019", "hud");
    }
    if (!modSkin) addTypeCode(c, file);
}

// ------------------------------------------------------------------- Psych --

void exportPsych(Ctx& c) {
    const ExportOptions& o = c.options;
    const bool typed = o.role == ExportRole::NoteType;
    std::string notesPath, splashPath;
    switch (o.role) {
        case ExportRole::ModSkin:
            notesPath = "images/noteSkins/NOTE_assets";
            splashPath = "images/noteSplashes/noteSplashes";
            break;
        case ExportRole::NoteType:
            notesPath = "images/notetypes/" + o.name;
            splashPath = "images/noteSplashes/noteSplashes-" + o.name;
            break;
        default:
            notesPath = "images/noteSkins/NOTE_assets-" + o.name;
            splashPath = "images/noteSplashes/noteSplashes-" + o.name;
            break;
    }
    if (o.notes) {
        AtlasOut atlas(notesPath);
        // Note.hx:436-441 (el final morado prueba primero `purple hold end`
        // y se queda con la errata si no la hay) y StrumNote.hx:119-121.
        for (int d = 0; d < 4; ++d) {
            addFixedPart(c, atlas, Part::Note, d, kColors[d]);
            addFixedPart(c, atlas, Part::HoldPiece, d, std::string(kColors[d]) + " hold piece");
            addFixedPart(c, atlas, Part::HoldEnd, d, d == 0 ? std::string("pruple end hold") : std::string(kColors[d]) + " hold end");
        }
        if (!typed) {
            if (!hasPart(c.style, Part::StrumStatic)) note(c.package, Severity::Error, "FML-EXPORT-002", "strumStatic");
            for (int d = 0; d < 4; ++d) {
                const std::string dir = directionKey(d);
                addFixedPart(c, atlas, Part::StrumStatic, d, std::string("arrow") + kUpper[d]);
                addFixedPart(c, atlas, Part::StrumPress, d, dir + " press");
                addFixedPart(c, atlas, Part::StrumConfirm, d, dir + " confirm");
            }
        }
        if (!atlas.empty() && atlas.write(c.package) && typed) c.look.psychTexture = "notetypes/" + o.name;
    }
    bool splashWritten = false;
    if (o.splashes && hasPart(c.style, Part::Splash)) {
        AtlasOut atlas(splashPath);
        // `note splash <color> <n>` es lo que 0.7 busca con su TXT y 1.0 sin
        // JSON (NoteSplash.hx:163-176); con mas de nueve variantes el prefijo
        // «... 1» cogeria tambien la 10.
        const std::vector<SplashAnim> anims = addSplashes(c, atlas, 1.0f, 9, [](int d, int v) {
            return "note splash " + std::string(kColors[d]) + " " + std::to_string(v + 1);
        });
        if (!anims.empty() && atlas.write(c.package)) {
            splashWritten = true;
            const Sheet* sheet = firstSheet(c, Part::Splash);
            // 1.0: JSON con cada animacion, su noteData (direccion + variante * 4),
            // FPS y offsets (NoteSplash.hx:84-117, :381-410).
            json doc;
            json animations = json::object();
            float fps = 24.0f;
            for (const SplashAnim& anim : anims) {
                const std::string key = std::string(kColors[anim.direction]) + std::to_string(anim.variant + 1);
                const int animFps = static_cast<int>(std::lround(anim.binding->animation.fps > 0.0f ? anim.binding->animation.fps : 24.0f));
                if (&anim == &anims.front()) fps = static_cast<float>(animFps);
                animations[key] = {{"name", key}, {"noteData", anim.direction + anim.variant * 4},
                                   {"prefix", anim.base}, {"indices", json::array()},
                                   {"offsets", {tidy(anim.offsetX), tidy(anim.offsetY)}}, {"fps", {animFps, animFps}}};
            }
            doc["animations"] = animations;
            doc["scale"] = 1;
            doc["allowRGB"] = c.keepsRgb;
            doc["allowPixel"] = true;
            if (c.keepsRgb && sheet && sheet->rgbFixed.size() == 3) {
                json rgb = json::array();
                for (std::uint32_t color : sheet->rgbFixed)
                    rgb.push_back({{"r", static_cast<int>((color >> 16) & 0xFF)}, {"g", static_cast<int>((color >> 8) & 0xFF)},
                                   {"b", static_cast<int>(color & 0xFF)}});
                doc["rgb"] = rgb;
            } else {
                doc["rgb"] = nullptr;
            }
            addText(c.package, splashPath + ".json", doc.dump(1, '\t') + "\n");
            // 0.7: nombre base, FPS y un offset por animacion (NoteSplash.hx:135-159 en 0.7).
            std::string txt = "note splash\n" + std::to_string(static_cast<int>(fps)) + " " + std::to_string(static_cast<int>(fps)) + "\n";
            int variants = 0;
            for (const SplashAnim& anim : anims) variants = std::max(variants, anim.variant + 1);
            for (int data = 0; data < variants * 4; ++data) {
                float x = 0.0f, y = 0.0f;
                for (const SplashAnim& anim : anims)
                    if (anim.direction + anim.variant * 4 == data) {
                        x = anim.offsetX;
                        y = anim.offsetY;
                    }
                txt += number(x) + " " + number(y) + "\n";
            }
            addText(c.package, splashPath + ".txt", txt);
            if (sheet && sheet->alpha < 0.999f) note(c.package, Severity::Info, "FML-EXPORT-015", "splash", number(sheet->alpha));
            if (!c.keepsRgb) note(c.package, Severity::Info, "FML-EXPORT-026", "splash");
            if (typed) {
                c.look.psychSplash = "noteSplashes/noteSplashes-" + o.name;
                c.look.psychSplashRgb = c.keepsRgb;
            }
        }
    }
    if (o.role == ExportRole::Selectable) {
        // Opciones > Visuales lee list.txt de los mods globales y de mods/
        // (VisualsSettingsSubState.hx:37, :54; Mods.hx:94-131).
        addText(c.package, "images/noteSkins/list.txt", o.name + "\n");
        if (splashWritten) addText(c.package, "images/noteSplashes/list.txt", o.name + "\n");
        json pack;
        pack["name"] = o.title.empty() ? o.name : o.title;
        pack["description"] = "Note Lab (Funkin Mod Lab)";
        pack["restart"] = false;
        pack["runsGlobally"] = true;
        addText(c.package, "pack.json", pack.dump(1, '\t') + "\n");
        note(c.package, Severity::Info, "FML-EXPORT-025", "list.txt");
    }
    if (typed) {
        // NoteTypesConfig.hx: `clave: valor`, las cadenas entre comillas; la
        // textura propia apaga la paleta RGB (Note.hx:369).
        bool renamed = false;
        const std::string file = typeFileName(o.noteType, renamed);
        if (renamed) note(c.package, Severity::Warning, "FML-EXPORT-023", o.noteType, file);
        addTypeCode(c, file);
    }
    if (!c.keepsRgb && !c.style.rgbPalette && o.notes && !typed) {
        if (o.role == ExportRole::SongSkin) note(c.package, Severity::Info, "FML-EXPORT-004", "rgb");
        else note(c.package, Severity::Warning, "FML-EXPORT-003", "rgb");
    }
    if (o.hud && c.style.hasHud) {
        if (!typed) exportFixedHud(c);
        else note(c.package, Severity::Info, "FML-EXPORT-019", "hud");
    }
}

// ----------------------------------------------------------------- V-Slice --

json offsetsJson(float x, float y) { return json::array({tidy(x), tidy(y)}); }

// La animacion de V-Slice (UnnamedAnimationData): prefijo y lo que no sea
// por defecto.
json vsliceAnim(const std::string& prefix, const Animation& from, float extraX, float extraY) {
    json anim;
    anim["prefix"] = prefix;
    if (std::fabs(from.fps - 24.0f) > 0.01f && from.fps > 0.0f) anim["frameRate"] = tidy(from.fps);
    if (from.loop) anim["looped"] = true;
    const float x = from.offsetX + extraX, y = from.offsetY + extraY;
    if (std::fabs(x) > 0.001f || std::fabs(y) > 0.001f) anim["offsets"] = offsetsJson(x, y);
    return anim;
}

// Un grupo del notestyle: un atlas con escala, offsets y pixel de la primera
// hoja; las demas se remuestrean a esa escala y su offset va en la animacion.
struct VSliceGroup {
    float scale = 1.0f;
    float offsetX = 0.0f, offsetY = 0.0f;
    float alpha = 1.0f;
    bool pixel = false;
    bool set = false;
};

void groupFrom(const Ctx& c, VSliceGroup& group, const Sheet& sheet) {
    if (group.set) return;
    group.scale = sheet.scale > 0.0f ? sheet.scale : 1.0f;
    group.offsetX = sheet.offsetX;
    group.offsetY = sheet.offsetY;
    group.alpha = sheet.alpha;
    group.pixel = isPixel(c, sheet);
    group.set = true;
}

json groupJson(const VSliceGroup& group, const std::string& assetPath) {
    json asset;
    asset["assetPath"] = assetPath;
    asset["scale"] = tidy(group.scale);
    if (std::fabs(group.offsetX) > 0.001f || std::fabs(group.offsetY) > 0.001f)
        asset["offsets"] = offsetsJson(group.offsetX, group.offsetY);
    if (group.pixel) asset["isPixel"] = true;
    return asset;
}

// Anade una pieza de V-Slice al atlas del grupo; devuelve la animacion o null.
// Con `minFrames` el ultimo fotograma se repite hasta tenerlos (comparten
// region): ver el acierto del receptor en exportVSlice.
json addVSlicePart(Ctx& c, AtlasOut& atlas, VSliceGroup& group, const PartBinding& binding, const std::string& base,
                   size_t minFrames = 1) {
    const Sheet* sheet = sheetOf(c.style, binding);
    std::vector<SourceFrame> frames = sourceFrames(c.reader, c.style, binding);
    if (!sheet || frames.empty()) {
        note(c.package, Severity::Warning, "FML-EXPORT-001", partSubject(binding));
        return nullptr;
    }
    while (frames.size() < minFrames) frames.push_back(frames.back());
    groupFrom(c, group, *sheet);
    Recipe recipe;
    recipe.pixel = isPixel(c, *sheet);
    recipe.palette = paletteFor(c, *sheet, binding);
    recipe.scale = (sheet->scale > 0.0f ? sheet->scale : 1.0f) / group.scale;
    if (recipe.palette) note(c.package, Severity::Info, "FML-EXPORT-005", "sheet:" + sheet->declared);
    if (atlas.add(frames, base, recipe) <= 0) return nullptr;
    return vsliceAnim(base, binding.animation, sheet->offsetX - group.offsetX, sheet->offsetY - group.offsetY);
}

// La tira de ocho columnas de V-Slice (SustainTrail.hx:231, :354-359): pieza y
// final de cada direccion. La pieza se repite a lo alto cada alto de imagen
// (sus UV van de -parte/alto a 0) y el final ocupa [0, endOffset] dentro del
// sostenido y [endOffset, bottomClip] por fuera (SustainTrail.hx:320-409). De
// un atlas de Codename o Psych: el final se apoya al fondo de [0, endOffset]
// (queda dentro de la nota, como en su motor) con la pieza encima, y la pieza
// se estira al alto de la tira.
bool exportHoldStrip(Ctx& c, const std::string& path, VSliceGroup& group) {
    const PartBinding* firstPiece = findPart(c.style, Part::HoldPiece, 0);
    if (!firstPiece) return false;
    const Sheet* pieceSheet = sheetOf(c.style, *firstPiece);
    if (!pieceSheet) return false;
    groupFrom(c, group, *pieceSheet);
    if (pieceSheet->kind == SheetKind::Strip) {
        // Ya es una tira de V-Slice: se copia.
        auto bytes = c.reader.bytes(pieceSheet->image);
        if (!bytes) {
            note(c.package, Severity::Warning, "FML-EXPORT-020", "holdPiece", pieceSheet->image);
            return false;
        }
        addBytes(c.package, path, std::move(*bytes));
        ++c.package.images;
        return true;
    }
    std::array<Image, 4> pieces, ends;
    int colW = 0, endH = 0;
    bool any = false;
    for (int d = 0; d < 4; ++d) {
        for (Part part : {Part::HoldPiece, Part::HoldEnd}) {
            const PartBinding* binding = findPart(c.style, part, d);
            if (!binding) continue;
            const std::vector<SourceFrame> frames = sourceFrames(c.reader, c.style, *binding);
            const Sheet* sheet = sheetOf(c.style, *binding);
            if (frames.empty() || !sheet) {
                note(c.package, Severity::Warning, "FML-EXPORT-001", partSubject(*binding));
                continue;
            }
            Image box = frameBox(frames.front(), paletteFor(c, *sheet, *binding));
            const float k = (sheet->scale > 0.0f ? sheet->scale : 1.0f) / group.scale;
            if (std::fabs(k - 1.0f) > 0.004f)
                box = resizeImage(box, static_cast<int>(std::lround(box.w * k)), static_cast<int>(std::lround(box.h * k)), group.pixel);
            colW = std::max(colW, box.w);
            if (part == Part::HoldEnd) endH = std::max(endH, box.h);
            (part == Part::HoldPiece ? pieces : ends)[static_cast<size_t>(d)] = std::move(box);
            any = true;
        }
    }
    if (!any || colW <= 0) return false;
    if (endH <= 0) endH = 1;
    const int height = group.pixel ? endH : static_cast<int>(std::lround(endH / kHoldEndOffset));
    const int inside = group.pixel ? height : endH;
    Image strip = blankImage(colW * 8, height);
    for (int d = 0; d < 4; ++d) {
        const Image& piece = pieces[static_cast<size_t>(d)];
        const Image& end = ends[static_cast<size_t>(d)];
        Image column;
        if (!piece.empty()) {
            column = resizeImage(piece, piece.w, height, group.pixel);
            blit(strip, column, d * 2 * colW + (colW - column.w) / 2, 0);
        }
        const int top = end.empty() ? inside : inside - end.h;
        if (top > 0 && !column.empty())
            blit(strip, cropRect(column, 0, 0, column.w, top), (d * 2 + 1) * colW + (colW - column.w) / 2, 0);
        if (!end.empty()) blit(strip, end, (d * 2 + 1) * colW + (colW - end.w) / 2, std::max(0, top));
    }
    addBytes(c.package, path, encodePng(strip));
    ++c.package.images;
    note(c.package, Severity::Info, "FML-EXPORT-021", "holdPiece");
    return true;
}

void exportVSlice(Ctx& c) {
    const ExportOptions& o = c.options;
    const bool typed = o.role == ExportRole::NoteType;
    const std::string folder = "notelab/" + o.name + "/";
    const bool fromVSlice = c.style.engine == Engine::VSlice;
    json doc;
    doc["version"] = "1.1.0";   // NoteStyleRegistry.NOTE_STYLE_DATA_VERSION
    doc["name"] = o.title.empty() ? o.name : o.title;
    doc["author"] = o.author.empty() ? c.style.author : o.author;
    // Lo que no trae el estilo lo pone otro, por grupos (NoteStyleData.hx:35).
    doc["fallback"] = fromVSlice && !c.style.fallback.empty() ? c.style.fallback : "funkin";
    json assets = json::object();
    // Un grupo heredado de V-Slice se deja al fallback.
    auto own = [&](Part part) { return hasPart(c.style, part, fromVSlice); };

    if (o.notes && own(Part::Note)) {
        AtlasOut atlas("images/" + folder + "notes");
        VSliceGroup group;
        json data = json::object();
        for (int d = 0; d < 4; ++d)
            if (const PartBinding* binding = findPart(c.style, Part::Note, d)) {
                json anim = addVSlicePart(c, atlas, group, *binding, std::string("note") + kTitle[d]);
                if (!anim.is_null()) data[directionKey(d)] = anim;
            }
        if (!atlas.empty() && atlas.write(c.package)) {
            json asset = groupJson(group, folder + "notes");
            asset["data"] = data;
            assets["note"] = asset;
        }
    }
    if (o.notes && own(Part::HoldPiece)) {
        VSliceGroup group;
        if (exportHoldStrip(c, "images/" + folder + "holds.png", group)) {
            json asset = groupJson(group, folder + "holds");
            asset["data"] = json::object();
            assets["holdNote"] = asset;
            if (c.style.engine != Engine::VSlice) note(c.package, Severity::Info, "FML-EXPORT-027", "holdPiece");
        }
    }
    // El notestyle de un NoteKind solo viste sus notas y sus sostenidos
    // (Strumline.hx:1146-1150, :1194-1198); receptores, salpicaduras y
    // coberturas son del estilo de la linea.
    if (typed && ((o.strums && own(Part::StrumStatic)) || (o.splashes && own(Part::Splash)) || (o.holdCovers && own(Part::HoldCover))))
        note(c.package, Severity::Info, "FML-EXPORT-037", "noteType");
    if (o.strums && own(Part::StrumStatic) && !typed) {
        AtlasOut atlas("images/" + folder + "strums");
        VSliceGroup group;
        json data = json::object();
        for (int d = 0; d < 4; ++d) {
            const std::string title = kTitle[d];
            const std::string dir = directionKey(d);
            json confirm;
            // Las claves de NoteStyleData_NoteStrumline y un prefijo propio por estado.
            struct State { Part part; const char* key; const char* base; };
            static const State states[] = {{Part::StrumStatic, "Static", "static"},
                                           {Part::StrumPress, "Press", "press"},
                                           {Part::StrumConfirm, "Confirm", "confirm"}};
            // El acierto necesita dos fotogramas o mas: con uno, playConfirm
            // deja el receptor inactivo (active = isAnimationDynamic, que es
            // numFrames > 1: StrumlineNote.hx:143, FunkinSprite.hx:398) y el
            // del rival y el del modo bot no vuelven nunca a reposo
            // (StrumlineNote.hx:88-97).
            for (const State& state : states)
                if (const PartBinding* binding = findPart(c.style, state.part, d)) {
                    json anim = addVSlicePart(c, atlas, group, *binding, state.base + title, state.part == Part::StrumConfirm ? 2 : 1);
                    if (anim.is_null()) continue;
                    data[dir + state.key] = anim;
                    if (state.part == Part::StrumConfirm) confirm = anim;
                }
            // Codename y Psych no tienen «confirm mantenido»: el juego base usa
            // el mismo confirm (funkin.json).
            const PartBinding* hold = findPart(c.style, Part::StrumConfirmHold, d);
            json holdAnim = hold ? addVSlicePart(c, atlas, group, *hold, "confirmHold" + title) : json();
            if (!holdAnim.is_null()) data[dir + "ConfirmHold"] = holdAnim;
            else if (!confirm.is_null()) data[dir + "ConfirmHold"] = confirm;
        }
        if (!atlas.empty() && atlas.write(c.package)) {
            json asset = groupJson(group, folder + "strums");
            asset["data"] = data;
            assets["noteStrumline"] = asset;
        }
    }
    if (o.splashes && own(Part::Splash) && !typed) {
        AtlasOut atlas("images/" + folder + "splashes");
        VSliceGroup group;
        if (const Sheet* sheet = firstSheet(c, Part::Splash)) groupFrom(c, group, *sheet);
        // V-Slice cambia el FPS de cada salpicadura por framerateDefault ±
        // variacion (NoteSplash.hx:52-56): el de la primera manda.
        const std::vector<SplashAnim> anims = addSplashes(c, atlas, group.scale, 99, [](int d, int v) {
            return "splash" + std::to_string(v + 1) + kTitle[d];
        });
        if (!anims.empty() && atlas.write(c.package)) {
            json asset = groupJson(group, folder + "splashes");
            if (group.alpha < 0.999f) asset["alpha"] = tidy(group.alpha);   // NoteStyle.hx:896
            json data = json::object();
            data["enabled"] = true;
            data["framerateDefault"] = static_cast<int>(std::lround(anims.front().binding->animation.fps > 0.0f
                                                                     ? anims.front().binding->animation.fps : 24.0f));
            for (int d = 0; d < 4; ++d) {
                json list = json::array();
                for (const SplashAnim& anim : anims) {
                    if (anim.direction != d) continue;
                    json entry;
                    entry["prefix"] = anim.base;
                    const float x = anim.offsetX - group.offsetX, y = anim.offsetY - group.offsetY;
                    if (std::fabs(x) > 0.001f || std::fabs(y) > 0.001f) entry["offsets"] = offsetsJson(x, y);
                    list.push_back(entry);
                }
                if (!list.empty()) data[std::string(directionKey(d)) + "Splashes"] = list;
            }
            asset["data"] = data;
            assets["noteSplash"] = asset;
        }
    }
    if (o.holdCovers && own(Part::HoldCover) && !typed) {
        AtlasOut atlas("images/" + folder + "holdCovers");
        VSliceGroup group;
        json data = json::object();
        data["enabled"] = true;
        for (int d = 0; d < 4; ++d) {
            json dir = json::object();
            static const std::pair<Part, const char*> states[] = {
                {Part::HoldCoverStart, "start"}, {Part::HoldCover, "hold"}, {Part::HoldCoverEnd, "end"}};
            for (const auto& state : states)
                if (const PartBinding* binding = findPart(c.style, state.first, d)) {
                    const std::string base = std::string("holdCover") + (state.first == Part::HoldCoverStart ? "Start"
                                             : state.first == Part::HoldCoverEnd ? "End" : "") + kTitle[d];
                    json anim = addVSlicePart(c, atlas, group, *binding, base);
                    if (!anim.is_null()) dir[state.second] = anim;
                }
            if (!dir.empty()) data[directionKey(d)] = dir;
        }
        if (!atlas.empty() && atlas.write(c.package)) {
            json asset = groupJson(group, folder + "holdCovers");
            asset["data"] = data;
            assets["holdNoteCover"] = asset;
        }
    }
    if (o.hud && c.style.hasHud && !typed) {
        auto hud = [&](const HudAsset& asset, const std::string& subject, const std::string& key, const std::string& file,
                       float fixedScale) {
            if (asset.inherited) return;
            float scale = 1.0f;
            if (!exportHudImage(c, asset, subject, {"images/" + folder + file + ".png", folder + file, 1.0f},
                                sourceHudScale(c.style.engine, fixedScale), scale))
                return;
            json entry;
            entry["assetPath"] = folder + file;
            entry["scale"] = tidy(scale);
            if (asset.pixel) entry["isPixel"] = true;
            assets[key] = entry;
        };
        static const char* judgementKeys[4] = {"judgementSick", "judgementGood", "judgementBad", "judgementShit"};
        for (int i = 0; i < 4; ++i)
            hud(c.style.judgements[static_cast<size_t>(i)], std::string("judgement/") + judgementKey(i), judgementKeys[i],
                std::string("popup/") + judgementKey(i), kRatingScale);
        for (int i = 0; i < 10; ++i)
            hud(c.style.digits[static_cast<size_t>(i)], "digit/" + std::to_string(i), "comboNumber" + std::to_string(i),
                "popup/num" + std::to_string(i), kNumberScale);
        if (!c.style.combo.image.empty() && !c.style.combo.inherited)
            note(c.package, Severity::Info, "FML-EXPORT-011", "combo");
        static const char* countdownKeys[4] = {"countdownThree", "countdownTwo", "countdownOne", "countdownGo"};
        static const char* images[4] = {"three", "ready", "set", "go"};
        static const char* sounds[4] = {"introTHREE", "introTWO", "introONE", "introGO"};
        for (int i = 0; i < 4; ++i) {
            const HudAsset& step = c.style.countdown[static_cast<size_t>(i)];
            if (step.inherited) continue;
            const std::string subject = std::string("countdown/") + countdownKey(i);
            json entry;
            float scale = 1.0f;
            const bool image = exportHudImage(c, step, subject, {"images/" + folder + "countdown/" + images[i] + ".png", {}, 1.0f},
                                              1.0f, scale);
            // `assetPath: null` = sin imagen, solo suena (NoteStyleData.hx; Countdown).
            if (image) {
                entry["assetPath"] = folder + "countdown/" + images[i];
                entry["scale"] = tidy(scale);
                if (step.pixel) entry["isPixel"] = true;
            } else {
                entry["assetPath"] = nullptr;
            }
            if (exportHudSound(c, step, subject, "sounds/" + folder + sounds[i] + ".ogg"))
                entry["data"] = {{"audioPath", folder + sounds[i]}};
            if (image || entry.contains("data")) assets[countdownKeys[i]] = entry;
        }
    } else if (o.hud && c.style.hasHud && typed) {
        note(c.package, Severity::Info, "FML-EXPORT-019", "hud");
    }
    doc["assets"] = assets;
    // Un tipo de nota sin aspecto (solo sus bloques) no necesita notestyle.
    const bool hasStyle = !assets.empty() || !typed;
    if (hasStyle) addText(c.package, "data/notestyles/" + o.name + ".json", doc.dump(2) + "\n");
    // _polymod_meta.json: api_version dentro de API_VERSION_RULE (PolymodHandler.hx:49).
    json meta;
    meta["title"] = o.title.empty() ? o.name : o.title;
    meta["description"] = "Note Lab (Funkin Mod Lab)";
    meta["contributors"] = json::array();
    if (!o.author.empty()) meta["contributors"].push_back({{"name", o.author}});
    meta["api_version"] = "0.8.0";
    meta["mod_version"] = "1.0.0";
    meta["license"] = "Unknown";
    addText(c.package, "_polymod_meta.json", meta.dump(2) + "\n");
    if (typed) {
        // El NoteKind del tipo, con su notestyle si lo hay (NoteKind.hx:48).
        if (hasStyle) c.look.vsliceStyle = o.name;
        addTypeCode(c, o.name);
    }
    if (c.style.engine != Engine::VSlice && (hasPart(c.style, Part::Splash) || hasPart(c.style, Part::StrumStatic)))
        note(c.package, Severity::Info, "FML-EXPORT-028", "strumStatic");
}

// La misma pieza sin fotogramas en las cuatro flechas: una nota, no cuatro.
void groupMissing(ExportPackage& package) {
    for (int p = 0; p < kPartCount; ++p) {
        const std::string head = std::string(partKey(static_cast<Part>(p))) + "/";
        auto missing = [&](const ExportNote& n) { return n.code == "FML-EXPORT-001" && n.subject.rfind(head, 0) == 0; };
        if (std::count_if(package.notes.begin(), package.notes.end(), missing) < 4) continue;
        const auto first = std::find_if(package.notes.begin(), package.notes.end(), missing);
        const size_t at = static_cast<size_t>(first - package.notes.begin());
        package.notes.erase(std::remove_if(package.notes.begin(), package.notes.end(), missing), package.notes.end());
        package.notes.insert(package.notes.begin() + static_cast<std::ptrdiff_t>(std::min(at, package.notes.size())),
                             ExportNote{Severity::Warning, "FML-EXPORT-001", partKey(static_cast<Part>(p)), ""});
    }
}

// Lo que el destino no tiene.
void noteDropped(Ctx& c) {
    if (c.options.target == Engine::VSlice) return;
    if (hasPart(c.style, Part::HoldCover) || hasPart(c.style, Part::HoldCoverStart))
        note(c.package, Severity::Info, "FML-EXPORT-009", "holdCover");
    // El confirm mantenido solo importa si no es el mismo que el confirm.
    for (int d = 0; d < 4; ++d) {
        const PartBinding* hold = findPart(c.style, Part::StrumConfirmHold, d);
        const PartBinding* confirm = findPart(c.style, Part::StrumConfirm, d);
        if (hold && (!confirm || hold->animation.prefix != confirm->animation.prefix || hold->sheet != confirm->sheet)) {
            note(c.package, Severity::Info, "FML-EXPORT-010", "strumConfirmHold");
            break;
        }
    }
    if (c.style.engine == Engine::VSlice && c.options.notes && hasPart(c.style, Part::HoldPiece))
        note(c.package, Severity::Info, "FML-EXPORT-022", "holdPiece");
}

}  // namespace

// ============================================================== publico ==

const char* exportRoleKey(ExportRole role) {
    switch (role) {
        case ExportRole::ModSkin: return "modSkin";
        case ExportRole::Selectable: return "selectable";
        case ExportRole::SongSkin: return "songSkin";
        case ExportRole::NoteType: return "noteType";
    }
    return "?";
}

bool exportRoleAvailable(Engine target, ExportRole role) {
    switch (target) {
        case Engine::Codename: return role == ExportRole::ModSkin || role == ExportRole::NoteType;
        case Engine::Psych: return true;
        case Engine::VSlice: return role == ExportRole::SongSkin || role == ExportRole::NoteType;
    }
    return false;
}

bool exportStrumsShareAtlas(Engine target, ExportRole role) {
    return target != Engine::VSlice && role != ExportRole::NoteType;
}

bool ExportPackage::hasErrors() const {
    return std::any_of(notes.begin(), notes.end(), [](const ExportNote& n) { return n.severity == Severity::Error; });
}

std::string exportName(const std::string& text) {
    std::string out;
    // Letras y cifras en minusculas; `_` se queda; el guion y todo lo demas
    // separa, sin guiones dobles («Wii Funkin' - VS Matt» -> wii-funkin-vs-matt).
    for (unsigned char c : text) {
        if (std::isalnum(c) && c < 128) out += static_cast<char>(std::tolower(c));
        else if (c == '_') out += '_';
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && (out.back() == '-' || out.back() == '_')) out.pop_back();
    while (!out.empty() && (out.front() == '-' || out.front() == '_')) out.erase(out.begin());
    return out.empty() ? std::string("notelab") : out;
}

bool noteExportNamesCollide(const std::string& first, const std::string& second) {
    auto folded = [](std::string value) {
        for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    };
    bool changed = false;
    return folded(first) == folded(second) || exportName(first) == exportName(second) ||
           folded(typeFileName(first, changed)) == folded(typeFileName(second, changed));
}

ExportPackage buildExport(const NoteStyle& style, const ExportOptions& options, const ExportIo& io) {
    ExportPackage package;
    package.source = style.engine;
    package.options = options;
    // Sin nombre: el del tipo de nota o el del estilo.
    std::string wanted = options.name;
    if (wanted.empty()) wanted = options.role == ExportRole::NoteType && !options.noteType.empty() ? options.noteType : style.name;
    package.options.name = exportName(wanted);
    if (package.options.title.empty()) package.options.title = wanted;
    package.styleName = style.name;
    package.folder = package.options.name + "-" + engineKey(options.target);
    if (!exportRoleAvailable(options.target, options.role)) {
        note(package, Severity::Error, "FML-EXPORT-029", exportRoleKey(options.role));
        return package;
    }
    if (options.role == ExportRole::NoteType && options.noteType.empty()) {
        note(package, Severity::Error, "FML-EXPORT-030", "noteType");
        return package;
    }
    Reader reader(io);
    Ctx c{style, package.options, reader, package};
    const bool psychTarget = options.target == Engine::Psych;
    c.keepsRgb = psychTarget && style.rgbPalette && options.role != ExportRole::NoteType;
    c.bakePalette = style.rgbPalette && !c.keepsRgb;
    switch (options.target) {
        case Engine::Codename: exportCodename(c); break;
        case Engine::Psych: exportPsych(c); break;
        case Engine::VSlice: exportVSlice(c); break;
    }
    std::set<std::string> effectNames;
    for (const StyleSound& sound : style.sounds) {
        const std::string path = "sounds/" + sound.name + ".ogg";
        const auto normalized = fs::u8path(sound.name).lexically_normal().generic_u8string();
        if (sound.name.empty() || normalized != sound.name || sound.name.find(':') != std::string::npos ||
            sound.name.find('\\') != std::string::npos || sound.name.front() == '/' || sound.name.find("..") != std::string::npos ||
            !effectNames.insert(lowerAscii(path)).second) {
            note(package, Severity::Error, "FML-EXPORT-020", "sound", "Invalid or duplicate sound path: " + sound.name); continue;
        }
        auto bytes = reader.bytes(sound.path);
        const char signature[] = "vorbis";
        if (!bytes || bytes->size() < 32 || !std::equal(bytes->begin(), bytes->begin() + 4, "OggS") ||
            std::search(bytes->begin(), bytes->begin() + std::min<size_t>(bytes->size(), 256), signature, signature + 6) ==
                bytes->begin() + std::min<size_t>(bytes->size(), 256)) {
            note(package, Severity::Error, "FML-EXPORT-020", "sound/" + sound.name, sound.path); continue;
        }
        if (std::any_of(package.files.begin(), package.files.end(), [&](const ExportFile& file) { return lowerAscii(file.path) == lowerAscii(path); })) {
            note(package, Severity::Error, "FML-EXPORT-020", "sound", "Target path collision: " + path); continue;
        }
        addBytes(package, path, std::move(*bytes));
        note(package, Severity::Info, "FML-EXPORT-038", "sound/" + sound.name);
    }
    noteDropped(c);
    groupMissing(package);
    if (package.files.empty()) note(package, Severity::Error, "FML-EXPORT-031", "package");
    return package;
}

// ---------------------------------------------------------------- verificar --

namespace {

std::vector<std::string> expectedStyles(const ExportPackage& package) {
    const ExportOptions& o = package.options;
    std::vector<std::string> ids;
    auto has = [&](const std::string& path) {
        return std::any_of(package.files.begin(), package.files.end(), [&](const ExportFile& f) { return f.path == path; });
    };
    bool renamed = false;
    switch (o.target) {
        case Engine::Codename: {
            const std::string file = o.role == ExportRole::ModSkin ? "default" : typeFileName(o.noteType, renamed);
            if (has("images/game/notes/" + file + ".xml")) ids.push_back("codename:game/notes/" + file);
            if (has("data/splashes/" + file + ".xml")) {
                if (o.role == ExportRole::ModSkin && !ids.empty()) break;   // va dentro del skin por defecto
                ids.push_back("codename:data/splashes/" + file);
            }
            break;
        }
        case Engine::Psych: {
            const std::string suffix = o.role == ExportRole::ModSkin ? "" : "-" + o.name;
            if (o.role == ExportRole::NoteType) {
                if (has("images/notetypes/" + o.name + ".xml")) ids.push_back("psych:notetype/" + typeFileName(o.noteType, renamed));
            } else if (has("images/noteSkins/NOTE_assets" + suffix + ".xml")) {
                ids.push_back("psych:noteSkins/NOTE_assets" + suffix);
            }
            if (has("images/noteSplashes/noteSplashes" + suffix + ".xml") && !(o.role == ExportRole::ModSkin && !ids.empty()))
                ids.push_back("psych:noteSplashes/noteSplashes" + suffix);
            break;
        }
        case Engine::VSlice:
            if (has("data/notestyles/" + o.name + ".json")) ids.push_back("vslice:" + o.name);
            break;
    }
    return ids;
}

bool writeTree(const ExportPackage& package, const fs::path& root, std::string& failed) {
    std::error_code ec;
    for (const ExportFile& file : package.files) {
        const fs::path target = root / fs::u8path(file.path);
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
        out.close();
        if (!out) {
            failed = file.path;
            return false;
        }
    }
    return true;
}

}  // namespace

bool verifyExport(ExportPackage& package, const fs::path& scratch) {
    package.verified = false;
    package.verifyErrors = -1;
    package.verifiedStyles.clear();
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = scratch / ("notelab-verify-" + std::to_string(stamp));
    std::string failed;
    if (!writeTree(package, root, failed)) {
        fs::remove_all(root, ec);
        return false;
    }
    int errors = 0;
    bool all = true;
    {
        Vfs vfs;
        vfs.setUtf8Text(true);
        vfs.pushRoot(root, "export");
        const Catalog catalog = scanNoteStyles(vfs);
        const std::vector<StyleReport> reports = checkCatalog(vfs, catalog);
        for (const std::string& id : expectedStyles(package)) {
            bool found = false;
            for (size_t i = 0; i < catalog.styles.size(); ++i) {
                if (catalog.styles[i].id != id) continue;
                found = true;
                for (const Finding& finding : reports[i].findings)
                    if (finding.severity == Severity::Error) ++errors;
            }
            if (found) package.verifiedStyles.push_back(id);
            all = all && found;
        }
        if (package.options.role == ExportRole::NoteType) {
            const bool custom = std::any_of(package.options.blocks.nodes.begin(), package.options.blocks.nodes.end(), [&](const auto& item) {
                return item.second.key == "code.file" && item.second.args.size() == 3 && item.second.args[0].value == engineKey(package.options.target);
            });
            // El tipo: que el lector de tipos lo encuentre y, donde el motor
            // lo deja ver (avoid en Codename, ignoreNote y hitCausesMiss en
            // Psych), con lo que dicen sus bloques.
            bool found = false;
            for (const NoteTypeEntry& type : scanNoteTypes(vfs, catalog)) {
                if (type.engine != package.options.target || lowerAscii(type.name) != lowerAscii(package.options.noteType)) continue;
                found = true;
                if (!custom && package.options.target != Engine::VSlice && blocksAvoid(package.options.blocks) != (type.bot != BotRule::Hits))
                    ++errors;
                if (!custom && package.options.target == Engine::Psych && blocksHitMisses(package.options.blocks) != type.hitMisses) ++errors;
                break;
            }
            if (found) package.verifiedStyles.push_back("type:" + package.options.noteType);
            all = all && found;
        }
        all = all && !package.verifiedStyles.empty();
    }
    fs::remove_all(root, ec);
    package.verifyErrors = errors;
    package.verified = all && errors == 0;
    return package.verified;
}

// -------------------------------------------------------------------- textos --

std::string describeExportNote(const ExportNote& n, bool es) {
    // Un choque de bloques: el bloque en palabras y su texto.
    if (!n.textEn.empty() || !n.textEs.empty()) {
        const std::string text = es ? n.textEs : n.textEn;
        return n.subject.empty() ? text : blockName(n.subject, es) + ": " + text;
    }
    std::string subject = n.subject == "hud" ? std::string("HUD") : readableSubject(n.subject, es);
    // Una pieza entera (groupMissing): su nombre y «las cuatro flechas».
    for (int p = 0; p < kPartCount; ++p)
        if (n.subject == partKey(static_cast<Part>(p)))
            subject = std::string(partLabel(static_cast<Part>(p), es)) + (es ? " (las cuatro flechas)" : " (all four arrows)");
    const std::string& d = n.detail;
    if (n.code == "FML-EXPORT-001")
        return es ? subject + ": sin fotogramas en el origen; no se exporta."
                  : subject + ": no frames in the source; not exported.";
    if (n.code == "FML-EXPORT-002")
        return es ? "El estilo no tiene receptores y el skin del motor los lleva en el mismo archivo: el juego se quedaría sin flechas fijas."
                  : "The style has no receptors and the engine keeps them in the same file: the game would have no receptors.";
    if (n.code == "FML-EXPORT-003")
        return es ? "Psych recolorea con los colores del jugador el skin por defecto y los de Opciones (Note.hx:106, :264), y este skin no está pintado en rojo, verde y azul para eso: se vería mal. Úsalo en una canción con «disableNoteRGB» o como tipo de nota."
                  : "Psych recolors the default skin and the ones from Options with the player's colors (Note.hx:106, :264), and this skin isn't painted in red, green and blue for it: it would look wrong. Use it in a song with \"disableNoteRGB\" or as a note type.";
    if (n.code == "FML-EXPORT-004")
        return es ? "Pon «\"disableNoteRGB\": true» en el chart: si no, Psych lo recolorea con los colores del jugador (Note.hx:264)."
                  : "Set \"disableNoteRGB\": true in the chart; otherwise Psych recolors it with the player's colors (Note.hx:264).";
    if (n.code == "FML-EXPORT-005")
        return es ? subject + ": la paleta RGB de Psych se ha pintado en la imagen con los colores por defecto (el destino no la aplica)."
                  : subject + ": Psych's RGB palette was baked into the image with the default colors (the target doesn't apply it).";
    if (n.code == "FML-EXPORT-006")
        return es ? subject + ": escala " + d + " remuestreada en la imagen, porque el destino dibuja a una escala fija."
                  : subject + ": scale " + d + " resampled into the image, because the target draws at a fixed scale.";
    if (n.code == "FML-EXPORT-007")
        return es ? subject + ": el offset va dentro del recorte del fotograma (el destino no deja declararlo)."
                  : subject + ": the offset went into the frame's trim (the target can't declare it).";
    if (n.code == "FML-EXPORT-008")
        return es ? subject + ": el motor la anima a " + d + " FPS con su bucle propio; el FPS y el bucle del original no se pueden declarar."
                  : subject + ": the engine plays it at " + d + " FPS with its own looping; the original FPS and looping can't be declared.";
    if (n.code == "FML-EXPORT-009")
        return es ? "Las coberturas del sostenido son solo de V-Slice: se quedan fuera." : "Hold covers only exist in V-Slice: left out.";
    if (n.code == "FML-EXPORT-010")
        return es ? "El «confirm mantenido» es solo de V-Slice: el destino usa el confirm normal." : "The held confirm only exists in V-Slice: the target uses the normal confirm.";
    if (n.code == "FML-EXPORT-011")
        return es ? "V-Slice no tiene la imagen «combo»: se queda fuera." : "V-Slice has no \"combo\" image: left out.";
    if (n.code == "FML-EXPORT-012")
        return es ? subject + ": en este motor el «3» solo suena; su imagen se queda fuera." : subject + ": in this engine \"3\" is only a sound; its image is left out.";
    if (n.code == "FML-EXPORT-013")
        return es ? subject + ": el motor carga OGG y este sonido es " + d + "; conviértelo a OGG." : subject + ": the engine loads OGG and this sound is " + d + "; convert it to OGG.";
    if (n.code == "FML-EXPORT-014")
        return es ? subject + ": es pixel art, y el destino no deja quitar el suavizado a un skin (solo en sus escenarios pixel)."
                  : subject + ": it's pixel art, and the target can't turn smoothing off for a skin (only on its pixel stages).";
    if (n.code == "FML-EXPORT-015")
        return es ? subject + ": la opacidad " + d + " no se puede declarar en el destino." : subject + ": opacity " + d + " can't be declared in the target.";
    if (n.code == "FML-EXPORT-016")
        return es ? subject + ": Psych admite hasta " + d + " variantes con un nombre que no se pise; esta se queda fuera."
                  : subject + ": Psych takes up to " + d + " variants without names clashing; this one is left out.";
    if (n.code == "FML-EXPORT-017")
        return es ? "Cada motor coloca las salpicaduras a su manera: sus offsets se copian tal cual; revísalas en el juego."
                  : "Each engine places splashes its own way: their offsets were copied as they are; check them in game.";
    if (n.code == "FML-EXPORT-018")
        return es ? "Los offsets de las salpicaduras se han convertido para que queden en el mismo sitio respecto a la flecha (aprox.)."
                  : "Splash offsets were converted so they sit in the same place relative to the arrow (approx.).";
    if (n.code == "FML-EXPORT-019")
        return es ? "El HUD es del mod o de la canción, no de un tipo de nota: no va en este paquete." : "The HUD belongs to the mod or the song, not to a note type: it's not in this package.";
    if (n.code == "FML-EXPORT-020")
        return es ? subject + ": no se pudo leer " + d + "." : subject + ": couldn't read " + d + ".";
    if (n.code == "FML-EXPORT-021")
        return es ? "La tira de sostenidos de V-Slice se ha hecho con la pieza y el final de cada flecha; el final queda dentro de la nota, como en el motor de origen."
                  : "The V-Slice hold strip was built from each arrow's piece and end; the end stays inside the note, as in the source engine.";
    if (n.code == "FML-EXPORT-022")
        return es ? "El sostenido de V-Slice se ha partido en pieza y final; en V-Slice el final sobresale un poco de la nota y aquí acaba con ella."
                  : "The V-Slice hold was split into piece and end; in V-Slice the end sticks out a bit past the note, here it ends with it.";
    if (n.code == "FML-EXPORT-023")
        return es ? "El tipo «" + n.subject + "» tiene caracteres que no valen en un nombre de archivo: se escribe como «" + d + "» y el motor no lo encontraría con el nombre del chart."
                  : "The type \"" + n.subject + "\" has characters a file name can't have: written as \"" + d + "\", and the engine wouldn't find it under the chart's name.";
    if (n.code == "FML-EXPORT-024")
        return es ? subject + ": no cabe en una imagen de 8192 px." : subject + ": doesn't fit in an 8192 px image.";
    if (n.code == "FML-EXPORT-025")
        return es ? "Si lo metes en un mod que ya tiene list.txt, añade la línea en vez de reemplazar el archivo." : "If you put it in a mod that already has list.txt, add the line instead of replacing the file.";
    if (n.code == "FML-EXPORT-026")
        return es ? "Las salpicaduras llevan «allowRGB: false» para que Psych 1.0 no las recoloree; Psych 0.7 no lo lee."
                  : "The splashes carry \"allowRGB\": false so Psych 1.0 doesn't recolor them; Psych 0.7 doesn't read it.";
    if (n.code == "FML-EXPORT-027")
        return es ? "V-Slice dibuja los sostenidos opacos (SustainTrail.hx:240); en Codename y Psych van al 60 %."
                  : "V-Slice draws holds opaque (SustainTrail.hx:240); Codename and Psych draw them at 60%.";
    if (n.code == "FML-EXPORT-028")
        return es ? "V-Slice coloca receptores y salpicaduras con sus propias medidas: revisa su posición en el juego."
                  : "V-Slice places receptors and splashes with its own measurements: check their position in game.";
    if (n.code == "FML-EXPORT-029")
        return es ? "Ese uso no existe en el motor de destino." : "That use doesn't exist in the target engine.";
    if (n.code == "FML-EXPORT-030")
        return es ? "Falta el nombre del tipo de nota." : "The note type name is missing.";
    if (n.code == "FML-EXPORT-031")
        return es ? "No hay nada que exportar con lo elegido." : "Nothing to export with what was chosen.";
    if (n.code == "FML-EXPORT-032")
        return es ? "Las imágenes del HUD a escala " + d + " se han remuestreado: el destino dibuja los juicios a 0.7 y los números a 0.5."
                  : "The HUD images at scale " + d + " were resampled: the target draws judgements at 0.7 and numbers at 0.5.";
    if (n.code == "FML-EXPORT-035")
        return es ? "El mod ya tiene " + d + ": el " + n.subject + " del paquete lo sustituiría. Junta los dos a mano."
                  : "The mod already has " + d + ": the package's " + n.subject + " would replace it. Merge them by hand.";
    if (n.code == "FML-EXPORT-036")
        return es ? "El tipo lleva " + d + " bloque(s): su código va en los archivos del tipo."
                  : "The type carries " + d + " block(s): their code goes in the type's files.";
    if (n.code == "FML-EXPORT-037")
        return es ? "V-Slice solo viste con el estilo de un tipo sus notas y sostenidos (Strumline.hx:1146-1150, :1194-1198): receptores, salpicaduras y coberturas son de la línea y se quedan fuera."
                  : "V-Slice only dresses a note kind's notes and holds with its style (Strumline.hx:1146-1150, :1194-1198): receptors, splashes and covers belong to the strumline and are left out.";
    if (n.code == "FML-EXPORT-038")
        return es ? subject + ": recurso incluido; conéctalo al bloque «tocar sonido» para dispararlo. No se activa por importarlo."
                  : subject + ": resource included; connect it to a play sound block to trigger it. Importing does not trigger it.";
    return n.code + " " + subject + " " + d;
}

namespace {

std::string roleTitle(const ExportPackage& p, bool es) {
    const ExportOptions& o = p.options;
    switch (o.role) {
        case ExportRole::ModSkin: return es ? "skin del mod" : "mod skin";
        case ExportRole::Selectable: return es ? "skin elegible en Opciones" : "skin selectable in Options";
        case ExportRole::SongSkin: return es ? "skin de canción" : "song skin";
        case ExportRole::NoteType: return (es ? "tipo de nota «" : "note type \"") + o.noteType + (es ? "»" : "\"");
    }
    return "";
}

std::string todayText() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
    return text;
}

// Pasos para instalarlo, por motor y uso.
std::string installSteps(const ExportPackage& p, bool es) {
    const ExportOptions& o = p.options;
    const std::string name = o.name;
    std::string s;
    auto step = [&](int n, const std::string& text) { s += std::to_string(n) + ". " + text + "\n"; };
    switch (o.target) {
        case Engine::Codename:
            if (o.role == ExportRole::ModSkin) {
                step(1, es ? "Copia el contenido de esta carpeta dentro de la de tu mod (mods/<tu mod>/). Sustituye el skin por defecto de ese mod: notas, receptores, salpicaduras y HUD de todas sus canciones."
                           : "Copy this folder's contents into your mod's folder (mods/<your mod>/). It replaces that mod's default skin: notes, receptors, splashes and HUD in all its songs.");
                step(2, es ? "Nada que tocar en los charts." : "Nothing to change in the charts.");
            } else {
                step(1, es ? "Copia el contenido de esta carpeta dentro de la de tu mod (mods/<tu mod>/)."
                           : "Copy this folder's contents into your mod's folder (mods/<your mod>/).");
                step(2, es ? "Las notas del tipo «" + o.noteType + "» en el chart toman este aspecto solas (Note.hx:156-158)."
                           : "Notes of type \"" + o.noteType + "\" in the chart take this look on their own (Note.hx:156-158).");
                step(3, es ? "Si ya tienes data/notes/" + o.noteType + ".hx, añade su línea de onNoteCreation a tu script en vez de reemplazarlo."
                           : "If you already have data/notes/" + o.noteType + ".hx, add its onNoteCreation line to your script instead of replacing it.");
            }
            break;
        case Engine::Psych:
            if (o.role == ExportRole::Selectable) {
                step(1, es ? "Pon esta carpeta en mods/ como un mod propio. Lleva pack.json con «runsGlobally», así que el skin sale en Opciones > Visuales > Note Skins como «" + name + "»."
                           : "Put this folder in mods/ as its own mod. Its pack.json has \"runsGlobally\", so the skin shows up in Options > Visuals > Note Skins as \"" + name + "\".");
                step(2, es ? "Para meterlo en un mod que ya tienes: copia images/ y añade la línea «" + name + "» a su images/noteSkins/list.txt (y a noteSplashes/list.txt)."
                           : "To add it to a mod you already have: copy images/ and add the line \"" + name + "\" to its images/noteSkins/list.txt (and noteSplashes/list.txt).");
            } else if (o.role == ExportRole::SongSkin) {
                step(1, es ? "Copia el contenido de esta carpeta dentro de la de tu mod (mods/<tu mod>/)."
                           : "Copy this folder's contents into your mod's folder (mods/<your mod>/).");
                step(2, es ? "En el chart (data/<canción>/<canción>-<dificultad>.json, dentro de \"song\") pon:\n     \"arrowSkin\": \"noteSkins/NOTE_assets-" + name + "\",\n     \"splashSkin\": \"noteSplashes/noteSplashes-" + name + "\""
                           : "In the chart (data/<song>/<song>-<difficulty>.json, inside \"song\") set:\n     \"arrowSkin\": \"noteSkins/NOTE_assets-" + name + "\",\n     \"splashSkin\": \"noteSplashes/noteSplashes-" + name + "\"");
                const bool colored = std::any_of(p.notes.begin(), p.notes.end(), [](const ExportNote& n) { return n.code == "FML-EXPORT-004"; });
                if (colored) step(3, es ? "y también \"disableNoteRGB\": true, para que Psych no lo recoloree." : "and also \"disableNoteRGB\": true, so Psych doesn't recolor it.");
            } else if (o.role == ExportRole::NoteType) {
                step(1, es ? "Copia el contenido de esta carpeta dentro de la de tu mod (mods/<tu mod>/)."
                           : "Copy this folder's contents into your mod's folder (mods/<your mod>/).");
                step(2, es ? "custom_notetypes/" + o.noteType + ".txt da este aspecto a las notas del tipo «" + o.noteType + "» (NoteTypesConfig.hx). Si ya tienes su .lua o .hx, déjalo: se leen los dos, y si el script pone otra textura, manda el script."
                           : "custom_notetypes/" + o.noteType + ".txt gives this look to notes of type \"" + o.noteType + "\" (NoteTypesConfig.hx). If you already have its .lua or .hx, keep it: both are read, and if the script sets another texture, the script wins.");
            } else {
                step(1, es ? "Copia el contenido de esta carpeta dentro de la de tu mod (mods/<tu mod>/). Sustituye el skin por defecto mientras ese mod esté cargado."
                           : "Copy this folder's contents into your mod's folder (mods/<your mod>/). It replaces the default skin while that mod is loaded.");
            }
            break;
        case Engine::VSlice:
            step(1, es ? "Pon esta carpeta en mods/ (ya es un mod: lleva _polymod_meta.json)."
                       : "Put this folder in mods/ (it's already a mod: it has _polymod_meta.json).");
            if (o.role == ExportRole::NoteType) {
                step(2, es ? "Las notas del tipo «" + o.noteType + "» usan el estilo «" + name + "» (scripts/notekinds/" + name + ".hxc)."
                           : "Notes of kind \"" + o.noteType + "\" use the \"" + name + "\" style (scripts/notekinds/" + name + ".hxc).");
            } else {
                step(2, es ? "Para usarlo en una canción: en data/songs/<canción>/<canción>-metadata.json, dentro de \"playData\", pon \"noteStyle\": \"" + name + "\"."
                           : "To use it in a song: in data/songs/<song>/<song>-metadata.json, inside \"playData\", set \"noteStyle\": \"" + name + "\".");
            }
            step(3, es ? "Lo que el estilo no trae lo pone «funkin» (fallback)." : "Whatever the style doesn't bring comes from \"funkin\" (fallback).");
            break;
    }
    return s;
}

}  // namespace

std::string installGuide(const ExportPackage& p, bool es) {
    const ExportOptions& o = p.options;
    std::string s;
    const std::string title = "Note Lab — " + (o.title.empty() ? o.name : o.title) + " (" + engineTitle(o.target) + ", " + roleTitle(p, es) + ")";
    s += title + "\n" + std::string(std::min<size_t>(title.size(), 78), '=') + "\n\n";
    s += (es ? "Hecho con Note Lab (Funkin Mod Lab) el " : "Made with Note Lab (Funkin Mod Lab) on ") + todayText() +
         (es ? " a partir de «" : " from \"") + p.styleName + (es ? "» (" : "\" (") + engineTitle(p.source) + ").\n";
    if (p.verified) {
        s += es ? "Estado: estructura verificada. Note Lab abrió el paquete con sus lectores y encontró el estilo entero, sin errores. No se ha probado dentro del motor.\n"
                : "Status: structure verified. Note Lab opened the package with its readers and found the whole style, with no errors. It hasn't been tested inside the engine.\n";
    } else if (p.verifyErrors > 0) {
        s += es ? "Estado: la relectura encontró " + std::to_string(p.verifyErrors) + " error(es). Revisa los avisos de abajo.\n"
                : "Status: reading it back found " + std::to_string(p.verifyErrors) + " error(s). Check the notes below.\n";
    } else {
        s += es ? "Estado: sin verificar.\n" : "Status: not verified.\n";
    }
    s += es ? "\nCómo instalarlo\n---------------\n" : "\nHow to install\n--------------\n";
    s += installSteps(p, es);
    s += es ? "\nQué hay en el paquete\n---------------------\n" : "\nWhat's in the package\n---------------------\n";
    for (const ExportFile& file : p.files) {
        if (file.path == "LEEME_INSTALAR.txt" || file.path == "INSTALL.txt" || file.path == kExportMarker) continue;
        s += "  " + file.path + "\n";
    }
    s += es ? "  (" + std::to_string(p.frames) + " fotogramas en " + std::to_string(p.atlases) + " atlas, " + std::to_string(p.images) +
                  " imágenes sueltas, " + std::to_string(p.sounds) + " sonidos)\n"
            : "  (" + std::to_string(p.frames) + " frames in " + std::to_string(p.atlases) + " atlases, " + std::to_string(p.images) +
                  " loose images, " + std::to_string(p.sounds) + " sounds)\n";
    std::vector<const ExportNote*> problems, changes;
    for (const ExportNote& n : p.notes) (n.severity == Severity::Info ? changes : problems).push_back(&n);
    s += es ? "\nChoques y avisos\n----------------\n" : "\nConflicts and warnings\n----------------------\n";
    if (problems.empty()) s += es ? "  Ninguno.\n" : "  None.\n";
    for (const ExportNote* n : problems)
        s += std::string("  [") + (n->severity == Severity::Error ? (es ? "error" : "error") : (es ? "aviso" : "warning")) + "] " +
             describeExportNote(*n, es) + "\n";
    s += es ? "\nQué cambia respecto al original\n-------------------------------\n" : "\nWhat changes from the original\n------------------------------\n";
    if (changes.empty()) s += es ? "  Nada.\n" : "  Nothing.\n";
    for (const ExportNote* n : changes) s += "  - " + describeExportNote(*n, es) + "\n";
    return s;
}

void addInstallGuides(ExportPackage& package) {
    package.files.erase(std::remove_if(package.files.begin(), package.files.end(), [](const ExportFile& f) {
        return f.path == "LEEME_INSTALAR.txt" || f.path == "INSTALL.txt" || f.path == kExportMarker;
    }), package.files.end());
    json marker;
    marker["format"] = "fml-notelab-export";
    marker["version"] = 1;
    marker["engine"] = engineKey(package.options.target);
    marker["role"] = exportRoleKey(package.options.role);
    marker["name"] = package.options.name;
    marker["title"] = package.options.title;
    if (!package.options.noteType.empty()) marker["noteType"] = package.options.noteType;
    marker["source"] = {{"engine", engineKey(package.source)}, {"style", package.styleName}};
    marker["created"] = todayText();
    marker["verified"] = package.verified;
    json files = json::array();
    for (const ExportFile& file : package.files) files.push_back(file.path);
    marker["files"] = files;
    // La guia en espanol y en ingles; en Windows el Bloc de notas lee UTF-8 con BOM.
    const std::string bom = "\xEF\xBB\xBF";
    addText(package, "LEEME_INSTALAR.txt", bom + installGuide(package, true));
    addText(package, "INSTALL.txt", bom + installGuide(package, false));
    addText(package, kExportMarker, marker.dump(2) + "\n");
}

// ----------------------------------------------------------------- escribir --

bool writeExportFolder(const ExportPackage& package, const fs::path& folder, std::string& error, bool es) {
    if (package.hasErrors() || !package.verified || package.verifyErrors != 0) {
        error = es ? "La exportación no tiene una verificación válida: no se escribe." : "The export has not passed verification: nothing is written.";
        return false;
    }
    std::error_code ec;
    if (fs::exists(folder, ec)) {
        if (!fs::is_directory(folder, ec)) {
            error = es ? "Ya hay un archivo con ese nombre: " + folder.u8string() : "There's already a file with that name: " + folder.u8string();
            return false;
        }
        if (!fs::is_empty(folder, ec) && !fs::exists(folder / kExportMarker, ec)) {
            error = es ? "La carpeta ya existe y no es una exportación de Note Lab; elige otra: " + folder.u8string()
                       : "The folder already exists and isn't a Note Lab export; pick another: " + folder.u8string();
            return false;
        }
    }
    fs::path part = folder;
    const std::string stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    part += ".notelab-part-" + stamp;
    std::string failed;
    if (!writeTree(package, part, failed)) {
        fs::remove_all(part, ec);
        error = (es ? "No se pudo escribir " : "Couldn't write ") + failed;
        return false;
    }
    if (fs::exists(folder, ec)) {
        fs::path old = folder;
        old += ".notelab-old-" + stamp;
        fs::rename(folder, old, ec);
        if (ec) {
            error = (es ? "No se pudo reemplazar la exportación anterior (¿abierta en otro programa?). La nueva quedó en " : "Couldn't replace the previous export (open in another program?). The new one is in ") +
                    part.u8string();
            return false;
        }
        fs::rename(part, folder, ec);
        if (ec) {
            fs::rename(old, folder, ec);
            error = (es ? "No se pudo publicar la exportación; la anterior sigue en su sitio." : "Couldn't publish the export; the previous one is still in place.");
            return false;
        }
        fs::remove_all(old, ec);
        return true;
    }
    fs::create_directories(folder.parent_path(), ec);
    fs::rename(part, folder, ec);
    if (ec) {
        error = (es ? "La exportación quedó en " : "The export is in ") + part.u8string() + (es ? " porque no se pudo renombrar." : " because it couldn't be renamed.");
        return false;
    }
    return true;
}

bool writeExportZip(const ExportPackage& package, const fs::path& zipPath, std::string& error, bool es) {
    if (package.hasErrors() || !package.verified || package.verifyErrors != 0) {
        error = es ? "La exportación no tiene una verificación válida: no se escribe." : "The export has not passed verification: nothing is written.";
        return false;
    }
    std::error_code directoryError;
    if (!zipPath.parent_path().empty()) fs::create_directories(zipPath.parent_path(), directoryError);
    if (directoryError) {
        error = (es ? "No se pudo crear la carpeta de destino: " : "Couldn't create the destination folder: ") + zipPath.parent_path().u8string();
        return false;
    }
    // En memoria y despues al disco con una ruta ancha: miniz abre archivos con
    // la pagina de codigos y una ruta con acentos fallaria.
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 1u << 20)) {
        error = es ? "No se pudo crear el ZIP." : "Couldn't create the ZIP.";
        return false;
    }
    bool ok = true;
    for (const ExportFile& file : package.files) {
        const std::string name = package.folder + "/" + file.path;
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), file.bytes.data(), file.bytes.size(), MZ_DEFAULT_COMPRESSION)) {
            ok = false;
            error = (es ? "No se pudo añadir al ZIP: " : "Couldn't add to the ZIP: ") + file.path;
            break;
        }
    }
    void* buffer = nullptr;
    size_t size = 0;
    if (ok && !mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
        ok = false;
        error = es ? "No se pudo cerrar el ZIP." : "Couldn't finish the ZIP.";
    }
    std::vector<unsigned char> bytes;
    if (ok && buffer) bytes.assign(static_cast<unsigned char*>(buffer), static_cast<unsigned char*>(buffer) + size);
    // El buffer ya es de quien lo pidio (mz_zip_writer_finalize_heap_archive).
    if (buffer) mz_free(buffer);
    mz_zip_writer_end(&zip);
    if (!ok) return false;
    fs::path temporary = zipPath;
    temporary += ".notelab-part-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    {
        std::ofstream out(temporary, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            fs::remove(temporary, ec);
            error = (es ? "No se pudo escribir " : "Couldn't write ") + zipPath.u8string();
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), zipPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    fs::rename(temporary, zipPath, ec);
#endif
    if (ec) {
        error = (es ? "El ZIP quedó en " : "The ZIP is in ") + temporary.u8string() + (es ? " porque no se pudo reemplazar el destino." : " because the target couldn't be replaced.");
        return false;
    }
    return true;
}

SparrowOut styleSheetAtlas(const NoteStyle& style, const ExportIo& io, const std::string& name, int* pieces) {
    SparrowOut out;
    Reader reader(io);
    AtlasOut atlas(name);
    int count = 0;
    for (int p = 0; p < kPartCount; ++p)
        for (int d = 0; d < 4; ++d) {
            const Part part = static_cast<Part>(p);
            for (const PartBinding* binding : bindingsOf(style, part, d)) {
                const Sheet* sheet = sheetOf(style, *binding);
                const std::vector<SourceFrame> frames = sourceFrames(reader, style, *binding);
                if (!sheet || frames.empty()) continue;
                Recipe recipe;
                recipe.pixel = sheet->pixel || style.pixel;
                // Lo que se ve: la paleta RGB de Psych con los colores por defecto
                // de cada carril, salvo el receptor en reposo (StrumNote.hx:169).
                if (style.rgbPalette && part != Part::StrumStatic)
                    recipe.palette = sheet->rgbFixed.size() == 3 ? sheet->rgbFixed.data() : psychDefaultPalette(d, recipe.pixel);
                // Todo a la escala de las notas del juego base, como lo exporta.
                recipe.scale = (sheet->scale > 0.0f ? sheet->scale : kFixedNoteScale) / kFixedNoteScale;
                if (recipe.pixel) recipe.scale = 1.0f;
                atlas.add(frames, basePieceName(part, d, binding->variant), recipe);
                ++count;
            }
        }
    if (pieces) *pieces = count;
    if (atlas.empty()) return out;
    ExportPackage package;
    if (!atlas.write(package)) return out;
    for (ExportFile& file : package.files) {
        if (file.path.size() > 4 && file.path.substr(file.path.size() - 4) == ".png") out.png = std::move(file.bytes);
        else out.xml.assign(file.bytes.begin(), file.bytes.end());
    }
    out.frames = atlas.frameCount();
    out.ok = !out.png.empty() && !out.xml.empty();
    return out;
}

std::vector<Image> stylePieceFrames(const NoteStyle& style, Part part, int direction, const ExportIo& io, int variant) {
    std::vector<Image> out;
    const PartBinding* binding = nullptr;
    for (const PartBinding& b : style.parts)
        if (b.part == part && b.direction == direction && b.variant == variant) {
            binding = &b;
            break;
        }
    if (!binding) return out;
    const Sheet* sheet = sheetOf(style, *binding);
    if (!sheet) return out;
    Reader reader(io);
    const bool pixel = sheet->pixel || style.pixel;
    const std::uint32_t* palette = nullptr;
    if (style.rgbPalette && part != Part::StrumStatic)
        palette = sheet->rgbFixed.size() == 3 ? sheet->rgbFixed.data() : psychDefaultPalette(direction, pixel);
    const float scale = (sheet->scale > 0.0f ? sheet->scale : kFixedNoteScale) / kFixedNoteScale;
    for (const SourceFrame& frame : sourceFrames(reader, style, *binding)) {
        Image box = frameBox(frame, palette);
        if (std::fabs(scale - 1.0f) > 0.004f && !box.empty())
            box = resizeImage(box, std::max(1, static_cast<int>(std::lround(box.w * scale))),
                              std::max(1, static_cast<int>(std::lround(box.h * scale))), pixel);
        out.push_back(std::move(box));
    }
    return out;
}

std::vector<unsigned char> writeSpriteFile(const SpriteFile& file) {
    using json = nlohmann::json;
    auto parsed = [](const std::string& text) {
        json value = json::parse(text.empty() ? std::string("{}") : text, nullptr, false);
        return value.is_discarded() ? json::object() : value;
    };
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 1u << 20)) return {};
    json doc = {{"format", "notelab-sprite"}, {"version", 1}, {"meta", parsed(file.meta)}, {"docs", json::array()}};
    bool ok = true;
    for (size_t d = 0; d < file.docs.size() && ok; ++d) {
        json entry = {{"meta", parsed(file.docs[d].meta)}, {"layers", json::array()}};
        for (size_t l = 0; l < file.docs[d].layers.size() && ok; ++l) {
            const SpriteFileLayer& layer = file.docs[d].layers[l];
            const std::string name = "docs/" + std::to_string(d) + "/layer-" + std::to_string(l) + ".png";
            const std::vector<unsigned char> png = encodePng(layer.image);
            ok = !png.empty() && mz_zip_writer_add_mem(&zip, name.c_str(), png.data(), png.size(), MZ_BEST_SPEED);
            entry["layers"].push_back({{"name", layer.name}, {"visible", layer.visible}, {"opacity", layer.opacity}, {"file", name}});
        }
        doc["docs"].push_back(entry);
    }
    const std::string text = doc.dump(1);
    ok = ok && mz_zip_writer_add_mem(&zip, "sprite.json", text.data(), text.size(), MZ_DEFAULT_COMPRESSION);
    void* buffer = nullptr;
    size_t size = 0;
    if (ok) ok = mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    std::vector<unsigned char> bytes;
    if (ok && buffer) bytes.assign(static_cast<unsigned char*>(buffer), static_cast<unsigned char*>(buffer) + size);
    if (buffer) mz_free(buffer);
    mz_zip_writer_end(&zip);
    return bytes;
}

bool readSpriteFile(const std::vector<unsigned char>& bytes, SpriteFile& out) {
    using json = nlohmann::json;
    out = SpriteFile{};
    if (bytes.empty()) return false;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return false;
    auto extract = [&](const std::string& name, std::vector<unsigned char>& data) {
        size_t size = 0;
        void* p = mz_zip_reader_extract_file_to_heap(&zip, name.c_str(), &size, 0);
        if (!p) return false;
        data.assign(static_cast<unsigned char*>(p), static_cast<unsigned char*>(p) + size);
        mz_free(p);
        return true;
    };
    bool ok = false;
    std::vector<unsigned char> text;
    if (extract("sprite.json", text)) {
        const json doc = json::parse(text.begin(), text.end(), nullptr, false);
        if (!doc.is_discarded() && doc.is_object() && doc.value("format", std::string()) == "notelab-sprite" && doc.contains("docs") && doc["docs"].is_array()) {
            ok = true;
            out.meta = doc.contains("meta") ? doc["meta"].dump() : std::string("{}");
            for (const json& entry : doc["docs"]) {
                if (out.docs.size() >= 32 || !entry.is_object() || !entry.contains("layers") || !entry["layers"].is_array()) continue;
                SpriteFileDoc one;
                one.meta = entry.contains("meta") ? entry["meta"].dump() : std::string("{}");
                for (const json& layer : entry["layers"]) {
                    if (one.layers.size() >= 32 || !layer.is_object() || !layer.contains("file") || !layer["file"].is_string()) continue;
                    std::vector<unsigned char> png;
                    SpriteFileLayer read;
                    if (!extract(layer["file"].get<std::string>(), png) || !decodePng(png, read.image) || read.image.w > 8192 || read.image.h > 8192) continue;
                    read.name = layer.value("name", std::string());
                    read.visible = layer.value("visible", true);
                    read.opacity = std::clamp(layer.value("opacity", 1.0f), 0.0f, 1.0f);
                    one.layers.push_back(std::move(read));
                }
                if (!one.layers.empty()) out.docs.push_back(std::move(one));
            }
            ok = !out.docs.empty();
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

}  // namespace fml::notelab

#include "NoteStyleCheck.hpp"

#include "NoteImage.hpp"
#include "../support/io/Vfs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>

namespace fml::notelab {
namespace {

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// addByIndices de Flixel: el fotograma cuyo nombre empieza por el prefijo y cuyo
// resto, leido como numero, es el indice pedido (FlxAnimationController
// findSpriteFrame).
int countIndices(const std::vector<std::string>& names, const std::string& prefix,
                 const std::vector<int>& indices) {
    int found = 0;
    for (int index : indices) {
        for (const std::string& name : names) {
            if (!startsWith(name, prefix)) continue;
            size_t i = prefix.size();
            if (i >= name.size() || !std::isdigit(static_cast<unsigned char>(name[i]))) continue;
            long value = 0;
            while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])) && value < 1000000)
                value = value * 10 + (name[i++] - '0');
            if (value == index) {
                ++found;
                break;
            }
        }
    }
    return found;
}

int framesFor(const std::vector<std::string>& names, const Animation& anim, std::string& matched) {
    std::vector<std::string> prefixes{anim.prefix};
    prefixes.insert(prefixes.end(), anim.alternatives.begin(), anim.alternatives.end());
    for (const std::string& prefix : prefixes) {
        if (prefix.empty()) continue;
        const int frames = anim.indices.empty() ? countPrefix(names, prefix)
                                                : countIndices(names, prefix, anim.indices);
        if (frames > 0) {
            matched = prefix;
            return frames;
        }
    }
    return 0;
}

// Una imagen partida en celdas como `loadGraphic(imagen, true, ancho, alto)`:
// Psych pide celdas de `floor(w / 4)` por `floor(h / 5)` (notas y receptores) o
// `floor(h / 2)` (sostenidos) (Note.hx:329-333, StrumNote.hx:65-68 de 0.7), y
// Flixel cuenta tantas columnas y filas como caben enteras, fila a fila
// (FlxTileFrames.hx:296-305 de Flixel 5.6.1). Con una medida que no se reparte
// entera, las celdas se corren; aqui se numeran igual que el motor.
struct GridImage {
    int w = 0, h = 0;
    int cellW = 0, cellH = 0;
    int columns = 0, rows = 0;              // las que cuenta Flixel
    std::vector<std::uint8_t> filled;       // por celda: tiene algun pixel visible
    bool cellFilled(int cell) const {
        return cell >= 0 && static_cast<size_t>(cell) < filled.size() && filled[static_cast<size_t>(cell)];
    }
};

const GridImage* gridOf(const Vfs& vfs, const Sheet& sheet, std::map<std::string, GridImage>& cache) {
    if (sheet.image.empty() || sheet.columns <= 0 || sheet.rows <= 0) return nullptr;
    const std::string key = sheet.image + "|" + std::to_string(sheet.columns) + "x" + std::to_string(sheet.rows);
    auto hit = cache.find(key);
    if (hit == cache.end()) {
        GridImage grid;
        Image image;
        const auto bytes = vfs.readBytes(sheet.image, 64u * 1024u * 1024u);
        if (bytes && decodePng(*bytes, image) && !image.empty()) {
            grid.w = image.w;
            grid.h = image.h;
            grid.cellW = image.w / sheet.columns;
            grid.cellH = image.h / sheet.rows;
            if (grid.cellW > 0 && grid.cellH > 0) {
                grid.columns = image.w / grid.cellW;
                grid.rows = image.h / grid.cellH;
                grid.filled.assign(static_cast<size_t>(grid.columns) * static_cast<size_t>(grid.rows), 0);
                for (int row = 0; row < grid.rows; ++row)
                    for (int column = 0; column < grid.columns; ++column) {
                        bool any = false;
                        for (int y = row * grid.cellH; y < (row + 1) * grid.cellH && !any; ++y)
                            for (int x = column * grid.cellW; x < (column + 1) * grid.cellW && !any; ++x)
                                any = image.at(x, y)[3] != 0;
                        grid.filled[static_cast<size_t>(row * grid.columns + column)] = any ? 1 : 0;
                    }
            }
        }
        hit = cache.emplace(key, std::move(grid)).first;
    }
    return hit->second.w > 0 ? &hit->second : nullptr;
}

Finding makeFinding(Severity severity, const char* code, const std::string& style,
                    std::string subject, std::string path, std::string detail) {
    Finding f;
    f.severity = severity;
    f.code = code;
    f.style = style;
    f.subject = std::move(subject);
    f.path = std::move(path);
    f.detail = std::move(detail);
    return f;
}

void checkHud(const NoteStyle& style, const HudAsset& asset, const std::string& subject,
              StyleReport& report) {
    if (!asset.imageOptional && !asset.declared.empty()) {
        ++report.hudExpected;
        if (!asset.image.empty()) ++report.hudFound;
        else if (!asset.nearby.empty())
            report.findings.push_back(makeFinding(Severity::Warning, "FML-NOTE-017", style.id, subject,
                                                  asset.nearby, asset.declared));
        else report.findings.push_back(makeFinding(Severity::Warning, "FML-NOTE-004", style.id, subject,
                                                   style.definition, asset.declared));
    }
    if (!asset.soundDeclared.empty() && asset.sound.empty())
        report.findings.push_back(makeFinding(Severity::Warning, "FML-NOTE-004", style.id, subject + ":sound",
                                              style.definition, asset.soundDeclared));
}

std::string fill(std::string text, const Finding& f, bool spanish) {
    const std::string subject = readableSubject(f.subject, spanish);
    const std::pair<const char*, const std::string*> keys[] = {
        {"{subject}", &subject}, {"{path}", &f.path}, {"{detail}", &f.detail}, {"{style}", &f.style}};
    for (const auto& key : keys) {
        size_t pos = 0;
        const std::string token = key.first;
        while ((pos = text.find(token, pos)) != std::string::npos) {
            text.replace(pos, token.size(), *key.second);
            pos += key.second->size();
        }
    }
    return text;
}

}  // namespace

const char* partLabel(Part part, bool spanish) {
    switch (part) {
        case Part::Note: return spanish ? "Nota" : "Note";
        case Part::HoldPiece: return spanish ? "Tramo del sostenido" : "Hold body";
        case Part::HoldEnd: return spanish ? "Final del sostenido" : "Hold end";
        case Part::StrumStatic: return spanish ? "Receptor en reposo" : "Receptor at rest";
        case Part::StrumPress: return spanish ? "Receptor pulsado" : "Receptor pressed";
        case Part::StrumConfirm: return spanish ? "Receptor al acertar" : "Receptor on hit";
        case Part::StrumConfirmHold: return spanish ? "Receptor sosteniendo" : "Receptor holding";
        case Part::Splash: return spanish ? "Salpicadura" : "Splash";
        case Part::HoldCoverStart: return spanish ? "Cubierta: inicio" : "Hold cover: start";
        case Part::HoldCover: return spanish ? "Cubierta: bucle" : "Hold cover: loop";
        case Part::HoldCoverEnd: return spanish ? "Cubierta: final" : "Hold cover: end";
    }
    return "?";
}

const char* partHelp(Part part, bool spanish) {
    switch (part) {
        case Part::Note:
            return spanish ? "La flecha que baja por el carril hasta el receptor." : "The arrow that scrolls down the lane to the receptor.";
        case Part::HoldPiece:
            return spanish ? "El trozo que se estira a lo largo de una nota larga." : "The piece stretched along a long note.";
        case Part::HoldEnd:
            return spanish ? "La punta con la que acaba una nota larga." : "The tip a long note ends with.";
        case Part::StrumStatic:
            return spanish ? "La flecha fija de arriba mientras no pulsas esa tecla." : "The fixed arrow at the top while that key is not pressed.";
        case Part::StrumPress:
            return spanish ? "El receptor cuando pulsas sin que haya nota." : "The receptor when you press with no note there.";
        case Part::StrumConfirm:
            return spanish ? "El receptor cuando aciertas una nota (el «confirm»)." : "The receptor when you hit a note (the \"confirm\").";
        case Part::StrumConfirmHold:
            return spanish ? "El receptor mientras mantienes una nota larga (V-Slice)." : "The receptor while you hold a long note (V-Slice).";
        case Part::Splash:
            return spanish ? "El destello que sale al acertar con el mejor juicio." : "The burst that appears when you hit with the best judgement.";
        case Part::HoldCoverStart:
            return spanish ? "El efecto sobre el receptor al empezar a mantener (V-Slice)." : "The effect over the receptor when a hold starts (V-Slice).";
        case Part::HoldCover:
            return spanish ? "El efecto que se repite mientras mantienes (V-Slice)." : "The effect that loops while you hold (V-Slice).";
        case Part::HoldCoverEnd:
            return spanish ? "El efecto al soltar una nota larga (V-Slice)." : "The effect when a long note is released (V-Slice).";
    }
    return "";
}

std::string directionLabel(int direction, bool spanish) {
    static const char* arrows[4] = {"\xE2\x86\x90", "\xE2\x86\x93", "\xE2\x86\x91", "\xE2\x86\x92"};   // ← ↓ ↑ →
    static const char* en[4] = {"left", "down", "up", "right"};
    static const char* es[4] = {"izquierda", "abajo", "arriba", "derecha"};
    const int d = ((direction % 4) + 4) % 4;
    return std::string(arrows[d]) + " " + (spanish ? es[d] : en[d]);
}

const char* judgementLabel(int index, bool spanish) {
    static const char* en[4] = {"Sick!", "Good", "Bad", "Shit"};
    static const char* es[4] = {"Sick!", "Bien", "Mal", "Pésimo"};
    return index >= 0 && index < 4 ? (spanish ? es[index] : en[index]) : "?";
}

const char* countdownLabel(int index, bool spanish) {
    static const char* en[4] = {"3", "2", "1", "Go!"};
    static const char* es[4] = {"3", "2", "1", "¡Ya!"};
    return index >= 0 && index < 4 ? (spanish ? es[index] : en[index]) : "?";
}

std::string readableSubject(const std::string& subject, bool spanish) {
    if (subject.rfind("sheet:", 0) == 0)
        return std::string(spanish ? "hoja «" : "sheet \"") + subject.substr(6) + (spanish ? "»" : "\"");
    const bool sound = subject.size() > 6 && subject.compare(subject.size() - 6, 6, ":sound") == 0;
    const std::string base = sound ? subject.substr(0, subject.size() - 6) : subject;
    const size_t slash = base.find('/');
    if (slash == std::string::npos) {
        if (base == "combo") return spanish ? "imagen «combo»" : "\"combo\" image";
        return subject;
    }
    const std::string head = base.substr(0, slash);
    std::string tail = base.substr(slash + 1);
    if (head == "countdown") {
        for (int i = 0; i < 4; ++i)
            if (tail == countdownKey(i))
                return std::string(sound ? (spanish ? "sonido de la cuenta atrás «" : "countdown sound \"")
                                         : (spanish ? "cuenta atrás «" : "countdown \"")) +
                       countdownLabel(i, spanish) + (spanish ? "»" : "\"");
        return subject;
    }
    if (head == "judgement") {
        for (int i = 0; i < 4; ++i)
            if (tail == judgementKey(i))
                return std::string(spanish ? "juicio «" : "judgement \"") + judgementLabel(i, spanish) + (spanish ? "»" : "\"");
        return subject;
    }
    if (head == "digit") return std::string(spanish ? "número " : "digit ") + tail;
    int variant = 0;
    const size_t hash = tail.find('#');
    if (hash != std::string::npos) {
        variant = std::atoi(tail.c_str() + hash + 1);
        tail = tail.substr(0, hash);
    }
    for (int p = 0; p < kPartCount; ++p) {
        if (head != partKey(static_cast<Part>(p))) continue;
        for (int d = 0; d < 4; ++d)
            if (tail == directionKey(d)) {
                std::string text = std::string(partLabel(static_cast<Part>(p), spanish)) + " " + directionLabel(d, spanish);
                if (variant > 0) text += std::string(spanish ? " (variante " : " (variant ") + std::to_string(variant + 1) + ")";
                return text;
            }
    }
    return subject;
}

std::string partSubject(const PartBinding& binding) {
    std::string subject = std::string(partKey(binding.part)) + "/" + directionKey(binding.direction);
    if (binding.variant > 0) subject += "#" + std::to_string(binding.variant + 1);
    return subject;
}

StyleReport checkNoteStyle(const Vfs& vfs, const NoteStyle& style, AtlasCache& atlases) {
    StyleReport report;
    report.style = style.id;
    std::map<std::string, GridImage> grids;
    std::vector<bool> sheetUsable(style.sheets.size(), false);
    for (size_t i = 0; i < style.sheets.size(); ++i) {
        const Sheet& sheet = style.sheets[i];
        const std::string subject = "sheet:" + sheet.declared;
        bool usable = true;
        if (sheet.image.empty()) {
            report.findings.push_back(makeFinding(Severity::Error, "FML-NOTE-002", style.id, subject,
                                                  style.definition, sheet.declared));
            usable = false;
        } else if (sheet.kind == SheetKind::Grid) {
            // Una medida que no se reparte entera entre las celdas: el motor las
            // corta hacia abajo y lo que sobra se pierde (FML-NOTE-024).
            const GridImage* grid = gridOf(vfs, sheet, grids);
            if (!grid) {
                report.findings.push_back(makeFinding(Severity::Error, "FML-NOTE-013", style.id, subject,
                                                      sheet.image, sheet.declared));
                usable = false;
            } else if (grid->w % sheet.columns != 0 || grid->h % sheet.rows != 0) {
                report.findings.push_back(makeFinding(Severity::Warning, "FML-NOTE-024", style.id, subject, sheet.image,
                                                      std::to_string(grid->w) + "x" + std::to_string(grid->h) + " / " +
                                                      std::to_string(sheet.columns) + "x" + std::to_string(sheet.rows)));
            }
        }
        if (sheet.kind == SheetKind::Sparrow || sheet.kind == SheetKind::Packer) {
            if (sheet.atlas.empty()) {
                report.findings.push_back(makeFinding(Severity::Error, "FML-NOTE-003", style.id, subject,
                                                      style.definition, sheet.declared));
                usable = false;
            } else if (!atlases.names(vfs, sheet.atlas)) {
                report.findings.push_back(makeFinding(Severity::Error, "FML-NOTE-013", style.id, subject,
                                                      sheet.atlas, sheet.declared));
                usable = false;
            }
        }
        sheetUsable[i] = usable;
    }

    report.parts.resize(style.parts.size());
    // Codename: el script del tipo pone el aspecto; se dice de donde salen los
    // prefijos antes de lo que falte.
    if (!style.lookScript.empty())
        report.findings.push_back(makeFinding(Severity::Info, "FML-NOTE-021", style.id, "script",
                                              style.lookScript, ""));
    if (!style.nameScript.empty())
        report.findings.push_back(makeFinding(Severity::Info, "FML-NOTE-026", style.id, "script", style.nameScript,
                                              style.sheets.empty() ? std::string() : style.sheets[0].declared));
    if (!style.multikeyData.empty())
        report.findings.push_back(makeFinding(Severity::Info, "FML-NOTE-027", style.id, "script", style.multikeyData, ""));
    int letteredUnresolved = 0;
    int holdsMissing = 0;
    int unread = 0;
    std::string holdsAtlas;
    for (size_t i = 0; i < style.parts.size(); ++i) {
        const PartBinding& binding = style.parts[i];
        PartResult& result = report.parts[i];
        if (binding.inherited) ++report.partsInherited;
        // Lo que el script pone de una forma que Note Lab no lee no se compara
        // con el atlas: se dice una vez para todo el estilo (FML-NOTE-022).
        if (binding.unread) {
            ++unread;
            continue;
        }
        if (binding.sheet < 0 || static_cast<size_t>(binding.sheet) >= style.sheets.size()) continue;
        const Sheet& sheet = style.sheets[static_cast<size_t>(binding.sheet)];
        if (!sheetUsable[static_cast<size_t>(binding.sheet)]) continue;   // ya dicho por la hoja
        if (sheet.kind == SheetKind::Grid) {
            // Una rejilla pixel: el motor corta la imagen en celdas iguales y la
            // animacion pide celdas por numero; las que no estan o estan vacias
            // no dibujan nada (FML-NOTE-025).
            const GridImage* grid = gridOf(vfs, sheet, grids);
            if (!grid) continue;   // sin imagen: ya dicho por la hoja
            std::vector<int> cells = binding.animation.indices;
            if (cells.empty()) cells.push_back(0);
            int drawn = 0;
            std::string missing;
            for (int cell : cells) {
                if (grid->cellFilled(cell)) ++drawn;
                else missing += (missing.empty() ? "" : ", ") + std::to_string(cell);
            }
            result.frames = drawn;
            if (drawn > 0) ++report.partsResolved;
            if (!missing.empty())
                report.findings.push_back(makeFinding(drawn > 0 ? Severity::Warning : Severity::Error, "FML-NOTE-025",
                                                      style.id, partSubject(binding), sheet.image, missing));
            continue;
        }
        if (sheet.kind == SheetKind::Strip || sheet.kind == SheetKind::Image) {
            result.frames = 1;
            ++report.partsResolved;
            continue;
        }
        const std::vector<std::string>* names = atlases.names(vfs, sheet.atlas);
        if (!names) continue;
        result.frames = framesFor(*names, binding.animation, result.matched);
        if (result.frames > 0) {
            ++report.partsResolved;
            if (result.matched != binding.animation.prefix)
                report.findings.push_back(makeFinding(Severity::Info, "FML-NOTE-008", style.id,
                                                      partSubject(binding), sheet.atlas, result.matched));
            continue;
        }
        // Los forks nombran sus tipos de nota (FML-NOTE-019) y las salpicaduras
        // de un skin por letra a su manera: lo que no aparece se dice una vez
        // para todo el estilo, no pieza a pieza. Notas, sostenidos y receptores
        // por letra ya se leen con los nombres del fork (FML-NOTE-016).
        if (style.forkNaming || (style.letteredNaming && binding.part == Part::Splash)) {
            ++letteredUnresolved;
            continue;
        }
        // Un tipo de nota sin sostenidos solo se nota si un chart le da notas
        // largas: un aviso para todo el estilo (FML-NOTE-018), no un error por pieza.
        // Sin prefijo en un tipo con script, es el script el que no los anade.
        if (style.use == StyleUse::NoteType && (binding.part == Part::HoldPiece || binding.part == Part::HoldEnd)) {
            if (holdsMissing++ == 0)
                holdsAtlas = binding.animation.prefix.empty() && !style.lookScript.empty() ? style.lookScript : sheet.atlas;
            continue;
        }
        std::string wanted = binding.animation.prefix;
        for (const std::string& alternative : binding.animation.alternatives) wanted += " | " + alternative;
        report.findings.push_back(makeFinding(Severity::Error, wanted.empty() ? "FML-NOTE-014" : "FML-NOTE-001",
                                              style.id, partSubject(binding), sheet.atlas, wanted));
    }
    if (style.letteredNaming)
        report.findings.push_back(makeFinding(Severity::Info, "FML-NOTE-016", style.id, "naming",
                                              style.sheets.empty() ? style.definition : style.sheets[0].atlas,
                                              std::to_string(letteredUnresolved)));
    else if (style.forkNaming && letteredUnresolved > 0)
        report.findings.push_back(makeFinding(Severity::Warning, "FML-NOTE-019", style.id, "naming",
                                              style.sheets.empty() ? style.definition : style.sheets[0].atlas,
                                              std::to_string(letteredUnresolved)));
    if (holdsMissing > 0)
        report.findings.push_back(makeFinding(Severity::Warning, "FML-NOTE-018", style.id, "hold", holdsAtlas,
                                              std::to_string(holdsMissing)));
    if (unread > 0)
        report.findings.push_back(makeFinding(Severity::Info, "FML-NOTE-022", style.id, "script",
                                              style.lookScript.empty() ? style.definition : style.lookScript,
                                              std::to_string(unread)));

    if (style.hasHud) {
        for (int i = 0; i < 4; ++i) checkHud(style, style.countdown[i], std::string("countdown/") + countdownKey(i), report);
        for (int i = 0; i < 4; ++i) checkHud(style, style.judgements[i], std::string("judgement/") + judgementKey(i), report);
        for (int i = 0; i < 10; ++i) checkHud(style, style.digits[i], "digit/" + std::to_string(i), report);
        checkHud(style, style.combo, "combo", report);
    }
    if (style.rgbPalette)
        report.findings.push_back(makeFinding(Severity::Info, "FML-NOTE-006", style.id, "color",
                                              style.definition, ""));
    // Un skin pixel: de que rejilla sale y cuando lo usa el motor.
    for (const Sheet& sheet : style.sheets) {
        if (sheet.kind != SheetKind::Grid || !sheet.pixel) continue;
        if (const GridImage* grid = gridOf(vfs, sheet, grids))
            report.findings.push_back(makeFinding(Severity::Info, "FML-NOTE-007", style.id, "pixel", sheet.image,
                                                  std::to_string(grid->columns) + " x " + std::to_string(grid->rows) + ", " +
                                                  std::to_string(grid->cellW) + " x " + std::to_string(grid->cellH) + " px"));
        break;
    }
    if (!style.referenced) {
        // Lo que el motor no carga por si mismo no rompe nada al jugar: sus
        // fallos se dicen, pero como informacion.
        for (Finding& f : report.findings) f.severity = Severity::Info;
        report.findings.insert(report.findings.begin(),
                               makeFinding(Severity::Info, "FML-NOTE-015", style.id, "use", style.definition, ""));
    }
    return report;
}

std::vector<StyleReport> checkCatalog(const Vfs& vfs, const Catalog& catalog) {
    AtlasCache atlases;
    std::vector<StyleReport> reports;
    reports.reserve(catalog.styles.size());
    for (const NoteStyle& style : catalog.styles) reports.push_back(checkNoteStyle(vfs, style, atlases));
    return reports;
}

void settleOtherEngines(const Catalog& catalog, std::vector<StyleReport>& reports, Engine engine) {
    for (size_t i = 0; i < catalog.styles.size() && i < reports.size(); ++i) {
        const NoteStyle& style = catalog.styles[i];
        if (style.engine == engine) continue;
        StyleReport& report = reports[i];
        const bool already = std::any_of(report.findings.begin(), report.findings.end(),
                                         [](const Finding& f) { return f.code == "FML-NOTE-020"; });
        if (already) continue;
        for (Finding& f : report.findings) f.severity = Severity::Info;
        report.findings.insert(report.findings.begin(),
                               makeFinding(Severity::Info, "FML-NOTE-020", style.id, "engine", style.definition,
                                           std::string(engineKey(style.engine)) + "/" + engineKey(engine)));
    }
}

std::string describe(const Finding& f, bool spanish) {
    struct Text { const char* code; const char* en; const char* es; };
    static const Text texts[] = {
        {"FML-NOTE-001",
         "{subject}: no frame starts with \"{detail}\" in {path}; the engine would draw nothing.",
         "{subject}: ningún fotograma empieza por «{detail}» en {path}; el motor no dibujaría nada."},
        {"FML-NOTE-002",
         "{subject}: the image \"{detail}\" is not in the opened folders.",
         "{subject}: la imagen «{detail}» no está en las carpetas abiertas."},
        {"FML-NOTE-003",
         "{subject}: the atlas (XML/TXT) of \"{detail}\" is not in the opened folders.",
         "{subject}: el atlas (XML/TXT) de «{detail}» no está en las carpetas abiertas."},
        {"FML-NOTE-004",
         "{subject}: \"{detail}\" is not in the opened folders; the engine uses another file or shows nothing.",
         "{subject}: «{detail}» no está en las carpetas abiertas; el motor usa otro archivo o no enseña nada."},
        {"FML-NOTE-005",
         "the fallback style \"{detail}\" was not found; whatever this style does not declare will be missing.",
         "no se encontró el estilo de respaldo «{detail}»; faltará todo lo que este estilo no declara."},
        {"FML-NOTE-006",
         "the colors come from the player's options (RGB palette) unless the song sets disableNoteRGB.",
         "los colores salen de las opciones del jugador (paleta RGB) salvo que la canción ponga disableNoteRGB."},
        {"FML-NOTE-007",
         "{path}: pixel skin. The engine cuts it into a grid of equal cells ({detail}) and uses it on pixel stages "
         "(the stage's isPixelStage), zoomed 6 times and without smoothing.",
         "{path}: skin pixel. El motor lo corta en una rejilla de celdas iguales ({detail}) y lo usa en los "
         "escenarios pixel (isPixelStage del escenario), ampliado 6 veces y sin suavizado."},
        {"FML-NOTE-008",
         "{subject}: found only under the alternative name \"{detail}\", which the engine also accepts.",
         "{subject}: solo está con el nombre alternativo «{detail}», que el motor también acepta."},
        {"FML-NOTE-010",
         "{path}: the {detail} could not be read.",
         "{path}: no se pudo leer el {detail}."},
        {"FML-NOTE-011",
         "{path}: the splash list needs a sprite attribute; Codename stops with an error.",
         "{path}: la lista de salpicaduras necesita el atributo sprite; Codename se detiene con un error."},
        {"FML-NOTE-012",
         "{path}: there is no <strum> entry, so Codename shows no splash.",
         "{path}: no hay ninguna entrada <strum>, así que Codename no enseña salpicaduras."},
        {"FML-NOTE-013",
         "{subject}: the atlas {path} could not be parsed.",
         "{subject}: no se pudo interpretar el atlas {path}."},
        {"FML-NOTE-014",
         "{subject}: no animation prefix is declared; the engine would draw nothing.",
         "{subject}: no hay prefijo de animación declarado; el motor no dibujaría nada."},
        {"FML-NOTE-015",
         "{path}: the engine does not load this by itself (not the default, not listed, not asked for by a chart); "
         "it is probably used by a script, so its problems are shown as information.",
         "{path}: el motor no lo carga por sí solo (no es el de por defecto, no está en la lista ni lo pide un chart); "
         "seguramente lo usa un script, así que sus problemas se dicen como información."},
        {"FML-NOTE-017",
         "{subject}: \"{detail}\" is not where the engine looks for it, but there is a file with that name at {path}; "
         "a modified engine or a script may use it.",
         "{subject}: «{detail}» no está donde lo busca el motor, pero hay un archivo con ese nombre en {path}; "
         "puede usarlo un motor modificado o un script."},
        {"FML-NOTE-018",
         "{path}: this note type has nothing to draw its holds with ({detail} pieces). Its short notes look right, but a "
         "long note of this type would not draw its tail properly. It only matters if a chart gives this type long notes.",
         "{path}: este tipo de nota no tiene con qué dibujar los sostenidos ({detail} piezas). Sus notas cortas se ven bien, "
         "pero una nota larga de este tipo no dibujaría bien su cola. Solo importa si un chart le da notas largas a este tipo."},
        {"FML-NOTE-021",
         "{path}: this note type's script cancels onNoteCreation and sets the look itself, so the engine does not add "
         "the base game animations; its pieces use the prefixes the script adds.",
         "{path}: el script de este tipo de nota cancela onNoteCreation y pone él mismo el aspecto, así que el motor no "
         "añade las animaciones del juego base; sus piezas usan los prefijos que añade el script."},
        {"FML-NOTE-027",
         "{path}: this sheet uses the mod's multikey names for 4 keys (left0, left hold0, left hold end0...), not the "
         "base game ones; its multikey script loads them. Note Lab reads them from that file.",
         "{path}: esta hoja usa los nombres de multikey del mod para 4 teclas (left0, left hold0, left hold end0...), no "
         "los del juego base; los carga su script de multikey. Note Lab los lee de ese archivo."},
        {"FML-NOTE-026",
         "{path}: a script picks this note type's sheet with a built path (noteSprite = \"folder/\" + ...). Note Lab does "
         "not run it: it found «{detail}» by the type's name in that folder. If the script picks another name, the game "
         "shows that one.",
         "{path}: un script elige la hoja de este tipo de nota con una ruta hecha (noteSprite = \"carpeta/\" + ...). "
         "Note Lab no lo ejecuta: encontró «{detail}» por el nombre del tipo en esa carpeta. Si el script pone otro "
         "nombre, el juego enseña ese."},
        {"FML-NOTE-022",
         "{path}: the script sets {detail} pieces in a way Note Lab cannot read (it reads addByPrefix with fixed text, "
         "by direction); they are not checked and what is drawn depends on the script.",
         "{path}: el script pone {detail} piezas de una forma que Note Lab no sabe leer (lee addByPrefix con texto fijo, "
         "por dirección); no se comprueban y lo que se dibuje depende del script."},
        {"FML-NOTE-019",
         "{path}: this install's note skin is named by letter, so its engine is a fork with its own frame names. "
         "This note type follows them and Note Lab does not check it piece by piece ({detail} pieces).",
         "{path}: el skin de notas de esta instalación va por letra, así que su motor es un fork con sus propios "
         "nombres de fotograma. Este tipo de nota los sigue y Note Lab no lo comprueba pieza a pieza ({detail} piezas)."},
        {"FML-NOTE-020",
         "{path}: this belongs to another engine ({detail}: its engine / this install's); the engine of this install "
         "does not load it, so its problems are shown as information.",
         "{path}: es de otro motor ({detail}: el suyo / el de esta instalación); el motor de esta instalación no lo "
         "carga, así que sus problemas se dicen como información."},
        {"FML-NOTE-016",
         "{path}: frames are named by letter, as in Psych Engine Extra Keys (the fork of the mods with more keys). "
         "They are read with its 4-key names: A, B, C and D are left, down, up and right (A0, A hold, A tail, "
         "A press, A confirm; the receptor at rest is still arrowLEFT...). Vanilla Psych would not draw them; the "
         "mod's own fork does. Splash pieces with the fork's own names are not checked: {detail}.",
         "{path}: los fotogramas van por letra, como en Psych Engine Extra Keys (el fork de los mods con más teclas). "
         "Se leen con sus nombres de 4 teclas: A, B, C y D son izquierda, abajo, arriba y derecha (A0, A hold, "
         "A tail, A press, A confirm; el receptor en reposo sigue siendo arrowLEFT...). Psych normal no los "
         "dibujaría; el fork del propio mod sí. Las piezas de salpicadura con nombres propios del fork no se "
         "comprueban: {detail}."},
        {"FML-NOTE-024",
         "{subject}: the image does not split evenly into the grid ({detail}). The engine rounds the cells down and "
         "loses the rest; if another whole cell fits, the numbering shifts and the wrong arrows show up.",
         "{subject}: la imagen no se reparte entera en la rejilla ({detail}). El motor redondea las celdas hacia abajo "
         "y pierde lo que sobra; si cabe otra celda entera, la numeración se corre y salen flechas equivocadas."},
        {"FML-NOTE-025",
         "{subject}: the engine asks for cell {detail} of {path}, which is empty or outside the image; it would draw "
         "nothing there.",
         "{subject}: el motor pide la celda {detail} de {path}, que está vacía o fuera de la imagen; ahí no dibujaría "
         "nada."},
    };
    for (const Text& text : texts)
        if (f.code == text.code) return fill(spanish ? text.es : text.en, f, spanish);
    return f.code + ": " + f.subject + " " + f.detail;
}

}  // namespace fml::notelab

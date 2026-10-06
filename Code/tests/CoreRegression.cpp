// NoteLabCoreTests — Pruebas del nucleo de Note Lab (DESIGN_PLUGIN_NOTE_LAB §12.1).
//
//   NoteLabCoreTests                       casos sinteticos de los tres motores
//   NoteLabCoreTests --scan <raiz>... [--es] [--no-base]
//                                          lee y valida carpetas reales, sin escribir;
//                                          con una raiz que no es una instalacion, monta
//                                          debajo el juego base que encuentre, como la app
//
// Los casos sinteticos se escriben en una carpeta temporal propia y se borran al
// terminar. Casi todas las imagenes son bytes de relleno: comprueban nombres,
// rutas y fotogramas. La exportacion usa PNG de verdad y mira pixeles.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

#include "../core/NoteStyleCheck.hpp"
#include "../core/NotePreview.hpp"
#include "../core/NoteSongs.hpp"
#include "../core/NoteTypes.hpp"
#include "../core/NoteProject.hpp"
#include "../core/NoteBlocks.hpp"
#include "../core/NoteCode.hpp"
#include "../core/NoteExport.hpp"
#include "../core/NoteImage.hpp"
#include "../core/NoteInstall.hpp"
#include "../core/NoteCreate.hpp"
#include "../core/NoteResources.hpp"
#include "../support/formats/LegacyChart.hpp"
#include "../support/formats/ChartExchange.hpp"
#include "../support/io/Vfs.hpp"
#include "../third_party/json.hpp"
#include "../third_party/miniz/miniz.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace fml;
using namespace fml::notelab;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    std::printf("%s %s\n", condition ? "[ok]  " : "[FAIL]", what.c_str());
    if (!condition) ++g_failures;
}

void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string atlasXml(const std::vector<std::string>& frames) {
    std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<TextureAtlas imagePath=\"sheet.png\">\n";
    int x = 0;
    for (const std::string& frame : frames) {
        xml += "  <SubTexture name=\"" + frame + "\" x=\"" + std::to_string(x) +
               "\" y=\"0\" width=\"8\" height=\"8\"/>\n";
        x += 8;
    }
    return xml + "</TextureAtlas>\n";
}

void writeSheet(const fs::path& withoutExtension, const std::vector<std::string>& frames) {
    writeFile(fs::u8path(withoutExtension.u8string() + ".png"), "PNG-placeholder");
    writeFile(fs::u8path(withoutExtension.u8string() + ".xml"), atlasXml(frames));
}

std::vector<std::string> baseGameFrames(bool typo, bool withStrums, const std::string& skip = {}) {
    static const char* colors[4] = {"purple", "blue", "green", "red"};
    static const char* dirs[4] = {"left", "down", "up", "right"};
    static const char* upper[4] = {"LEFT", "DOWN", "UP", "RIGHT"};
    std::vector<std::string> frames;
    for (int d = 0; d < 4; ++d) {
        frames.push_back(std::string(colors[d]) + "0000");
        frames.push_back(std::string(colors[d]) + " hold piece0000");
        if (d == 0 && typo) frames.push_back("pruple end hold0000");
        else frames.push_back(std::string(colors[d]) + " hold end0000");
        if (!withStrums) continue;
        frames.push_back(std::string("arrow") + upper[d] + "0000");
        frames.push_back(std::string(dirs[d]) + " press0000");
        frames.push_back(std::string(dirs[d]) + " confirm0000");
    }
    std::vector<std::string> kept;
    for (const std::string& frame : frames)
        if (skip.empty() || frame.rfind(skip, 0) != 0) kept.push_back(frame);
    return kept;
}

struct Scan {
    Vfs vfs;
    Catalog catalog;
    std::vector<StyleReport> reports;
};

void scan(Scan& out, const std::vector<fs::path>& roots) {
    out.vfs.setUtf8Text(true);
    out.vfs.setScanLimit(400000);
    for (const fs::path& root : roots) out.vfs.pushRoot(root, "source");
    out.catalog = scanNoteStyles(out.vfs);
    out.reports = checkCatalog(out.vfs, out.catalog);
}

const NoteStyle* styleById(const Scan& s, const std::string& id, const StyleReport** report = nullptr) {
    for (size_t i = 0; i < s.catalog.styles.size(); ++i)
        if (s.catalog.styles[i].id == id) {
            if (report) *report = &s.reports[i];
            return &s.catalog.styles[i];
        }
    return nullptr;
}

int countCode(const StyleReport& report, const std::string& code) {
    int n = 0;
    for (const Finding& f : report.findings) if (f.code == code) ++n;
    return n;
}

int countErrors(const StyleReport& report) {
    int n = 0;
    for (const Finding& f : report.findings) if (f.severity == Severity::Error) ++n;
    return n;
}

const Finding* findingOf(const StyleReport& report, const std::string& code) {
    for (const Finding& f : report.findings) if (f.code == code) return &f;
    return nullptr;
}

int countParts(const NoteStyle& style, Part part) {
    int n = 0;
    for (const PartBinding& b : style.parts) if (b.part == part) ++n;
    return n;
}

bool usesImage(const RenderList& list, const std::string& image) {
    for (const DrawCmd& cmd : list.cmds)
        if (cmd.texture >= 0 && static_cast<size_t>(cmd.texture) < list.textures.size() &&
            list.textures[static_cast<size_t>(cmd.texture)] == image) return true;
    return false;
}

AtlasStore atlasStoreOf(const Scan& s) {
    AtlasStore atlases;
    atlases.readText = [&s](const std::string& path) {
        const auto text = s.vfs.readText(path);
        return text ? *text : std::string();
    };
    return atlases;
}

void codenameCase(const fs::path& base) {
    std::printf("\n== Codename ==\n");
    const fs::path root = base / "cn";
    writeSheet(root / "assets/images/game/notes/default", baseGameFrames(true, true));
    writeSheet(root / "assets/images/game/notes/Hurt", {"purple0000", "blue0000", "green0000", "red0000"});
    writeFile(root / "assets/data/splashes/default.xml",
              "<!DOCTYPE codename-engine-splashes>\n<splashes sprite=\"game/splashes/default\" alpha=\"0.6\">\n"
              "<strum id=\"0\"><anim name=\"l1\" anim=\"note impact 1 purple\" fps=\"24\"/></strum>\n"
              "<strum id=\"1\"><anim name=\"d1\" anim=\"note impact 1 blue\" fps=\"24\"/></strum>\n"
              "<strum id=\"2\"><anim name=\"u1\" anim=\"note impact 1 green\" fps=\"24\"/></strum>\n"
              "<strum id=\"3\"><anim name=\"r1\" anim=\"note impact 1 red\" fps=\"24\"/></strum>\n</splashes>\n");
    writeSheet(root / "assets/images/game/splashes/default",
               {"note impact 1 purple0000", "note impact 1 blue0000", "note impact 1 green0000", "note impact 1 red0000"});
    writeFile(root / "assets/data/splashes/nostrum.xml", "<splashes sprite=\"game/splashes/default\"><anim name=\"a\" anim=\"x\"/></splashes>");
    for (const char* name : {"sick", "good", "bad", "shit", "combo"})
        writeFile(root / "assets/images/game/score" / (std::string(name) + ".png"), "PNG");
    for (int i = 0; i < 9; ++i)   // falta num9 a proposito
        writeFile(root / "assets/images/game/score" / ("num" + std::to_string(i) + ".png"), "PNG");
    for (const char* name : {"ready", "set", "go"})
        writeFile(root / "assets/images/game" / (std::string(name) + ".png"), "PNG");
    for (const char* name : {"intro3", "intro2", "intro1", "introGo"})
        writeFile(root / "assets/sounds" / (std::string(name) + ".ogg"), "OGG");
    // Scripts de tipos de nota (PlayState.hx:798): uno que el bot evita. Su
    // cancel() de onPlayerMiss no es el de la creacion de la nota.
    writeFile(root / "assets/data/notes/Hurt.hx",
              "function onNoteCreation(event) {\n    event.note.avoid = true; // el bot no la toca\n}\n"
              "function onPlayerMiss(event) { event.cancel(); }\n");
    writeFile(root / "assets/data/notes/Plain.hscript", "function onPlayerHit(event) {}\n");
    // Tipos cuyo script cancela onNoteCreation y pone el aspecto el mismo
    // (Note.hx:164): Fire con addByPrefix por direccion; Pixel con
    // loadGraphic, que no se lee. Ice cancela solo en el `case` de otro tipo.
    writeSheet(root / "assets/images/game/notes/Fire", {"purple fire0000", "blue fire0000", "green fire0000", "red fire0000"});
    writeFile(root / "assets/data/notes/Fire.hx",
              "function onNoteCreation(event:NoteCreationEvent) {\n"
              "    if (event.noteType != \"Fire\") return;\n"
              "    event.cancel(true); /* sin las del juego base */\n"
              "    event.note.frames = Paths.getFrames(event.noteSprite);\n"
              "    switch(event.strumID % 4) {\n"
              "        case 0: event.note.animation.addByPrefix('scroll', 'purple fire');\n"
              "        case 1: event.note.animation.addByPrefix('scroll', 'blue fire', 24, false);\n"
              "        case 2: event.note.animation.addByPrefix('scroll', 'green fire');\n"
              "        // case 3: event.note.animation.addByPrefix('scroll', 'red0');\n"
              "        case 3: event.note.animation.addByPrefix(\"scroll\", \"red fire\");\n"
              "    }\n"
              "}\n");
    writeSheet(root / "assets/images/game/notes/Pixel", {"pixel0000"});
    writeFile(root / "assets/data/notes/Pixel.hsc",
              "function onNoteCreation(e) if (e.noteType == \"Pixel\") {\n"
              "    e.preventDefault();\n"
              "    e.note.loadGraphic(Paths.image('game/pixel/notes'), true, 17, 17);\n"
              "    e.note.animation.add('scroll', [4 + e.strumID]);\n"
              "}\n"
              "function onPlayerHit(e) {}\n");
    writeSheet(root / "assets/images/game/notes/Ice", baseGameFrames(true, false));
    writeFile(root / "assets/data/notes/Ice.hx",
              "function onNoteCreation(event) {\n"
              "    switch (event.noteType) {\n"
              "        case \"Snow\":\n"
              "            event.cancel();\n"
              "            event.note.frames = Paths.getFrames('game/notes/Snow');\n"
              "        default:\n"
              "    }\n"
              "}\n");

    Scan s;
    scan(s, {root});
    const EngineGuess guess = guessEngine(s.vfs, s.catalog);
    expect(guess.found && guess.engine == Engine::Codename, "motor detectado: Codename");
    {
        const std::vector<NoteTypeEntry> types = scanNoteTypes(s.vfs, s.catalog);
        const NoteTypeEntry* hurtType = findNoteType(types, "hurt", Engine::Codename);
        const NoteTypeEntry* plain = findNoteType(types, "Plain");
        expect(types.size() == 5, "Codename: cinco tipos de nota en data/notes (" + std::to_string(types.size()) + ")");
        expect(hurtType && hurtType->script == "assets/data/notes/Hurt.hx" &&
               hurtType->lookStyle == "codename:assets/game/notes/Hurt", "Hurt: su script y su aspecto en game/notes");
        expect(hurtType && hurtType->bot == BotRule::Ignores, "Hurt: avoid = true, el bot la deja pasar");
        expect(plain && plain->bot == BotRule::Hits && plain->lookStyle.empty(),
               "Plain (.hscript): sin aspecto propio y el bot la toca");
    }
    const StyleReport* report = nullptr;
    const NoteStyle* def = styleById(s, "codename:assets/game/notes/default", &report);
    expect(def != nullptr, "default de Codename encontrado en assets/");
    if (def) {
        expect(def->use == StyleUse::Default, "default es de todo el mod");
        expect(countParts(*def, Part::Splash) == 4, "salpicadura por defecto: una por direccion");
        expect(report->partsResolved == static_cast<int>(def->parts.size()),
               "todas las piezas del default resuelven (" + std::to_string(report->partsResolved) + "/" +
               std::to_string(def->parts.size()) + ")");
        expect(countErrors(*report) == 0, "el default no tiene errores (la errata pruple end hold es el nombre principal)");
        expect(countCode(*report, "FML-NOTE-004") == 1, "falta exactamente una cifra del combo (num9)");
    }
    const NoteStyle* hurt = styleById(s, "codename:assets/game/notes/Hurt", &report);
    expect(hurt != nullptr && hurt->use == StyleUse::NoteType && hurt->useDetail == "Hurt",
           "game/notes/Hurt es un tipo de nota");
    if (hurt) {
        expect(countParts(*hurt, Part::StrumStatic) == 0, "un tipo de nota no trae receptores");
        expect(countCode(*report, "FML-NOTE-018") == 1 && countCode(*report, "FML-NOTE-001") == 0 &&
               countErrors(*report) == 0, "Hurt sin sostenidos: un aviso (solo importa con notas largas), no 8 errores");
        expect(hurt->lookScript.empty() && !hurt->parts.empty() && hurt->parts[0].animation.prefix == "purple0",
               "Hurt: el cancel() de onPlayerMiss no cuenta; sigue con los nombres del juego base");
    }
    // Si el script cancela onNoteCreation valen sus addByPrefix por direccion,
    // no los del juego base (Note.hx:164-195).
    auto partOf = [](const NoteStyle& style, Part part, int direction) -> const PartBinding* {
        for (const PartBinding& b : style.parts)
            if (b.part == part && b.direction == direction) return &b;
        return nullptr;
    };
    const NoteStyle* fire = styleById(s, "codename:assets/game/notes/Fire", &report);
    expect(fire != nullptr && fire->lookScript == "assets/data/notes/Fire.hx", "Fire: su script cancela y pone el aspecto");
    if (fire) {
        const PartBinding* left = partOf(*fire, Part::Note, 0);
        const PartBinding* down = partOf(*fire, Part::Note, 1);
        const PartBinding* right = partOf(*fire, Part::Note, 3);
        expect(left && left->animation.prefix == "purple fire" && left->animation.fps == 30.0f && left->animation.loop &&
               right && right->animation.prefix == "red fire" && right->animation.alternatives.empty(),
               "Fire: cada direccion con el prefijo de su case (el comentado no cuenta)");
        expect(down && down->animation.prefix == "blue fire" && down->animation.fps == 24.0f && !down->animation.loop,
               "Fire: addByPrefix con fps y bucle");
        expect(report->partsResolved == 4 && countErrors(*report) == 0 && countCode(*report, "FML-NOTE-001") == 0,
               "Fire: las cuatro notas resuelven, sin errores por purple0..red0");
        expect(countCode(*report, "FML-NOTE-021") == 1 && countCode(*report, "FML-NOTE-018") == 1 &&
               countCode(*report, "FML-NOTE-022") == 0,
               "Fire: se dice que el aspecto es del script; sin sostenidos en el script, un aviso");
    }
    const NoteStyle* pixel = styleById(s, "codename:assets/game/notes/Pixel", &report);
    expect(pixel != nullptr && pixel->lookScript == "assets/data/notes/Pixel.hsc" && countErrors(*report) == 0 &&
           countCode(*report, "FML-NOTE-022") == 1 && countCode(*report, "FML-NOTE-018") == 0,
           "Pixel: cancela y usa loadGraphic, que no se lee: informacion, no errores");
    if (pixel) {
        int unread = 0;
        for (const PartBinding& b : pixel->parts) unread += b.unread ? 1 : 0;
        expect(unread == 12, "Pixel: sus 12 piezas quedan marcadas como no leidas (" + std::to_string(unread) + ")");
    }
    const NoteStyle* ice = styleById(s, "codename:assets/game/notes/Ice", &report);
    expect(ice != nullptr && ice->lookScript.empty() && report->partsResolved == 12 && countErrors(*report) == 0,
           "Ice: cancela solo en el case de otro tipo; sigue con los nombres del juego base");
    const NoteStyle* nostrum = styleById(s, "codename:assets/data/splashes/nostrum", &report);
    expect(nostrum != nullptr, "otra salpicadura es un estilo aparte");
    bool warned = false;
    for (const Finding& f : s.catalog.findings) warned = warned || f.code == "FML-NOTE-012";
    expect(warned, "salpicadura sin <strum>: aviso de que Codename no enseña nada");

    // Una nota de tipo Hurt se dibuja con game/notes/Hurt, no con el default
    // (Note.hx:156-158); sin estilo propio, con el del mod.
    if (def && hurt) {
        AtlasStore atlases = atlasStoreOf(s);
        NotePreview preview;
        PreviewSettings settings;
        PreviewState state;
        const std::vector<PreviewNote> notes{{1, 0, 300.0, 0.0}};
        RenderList plain;
        preview.build(*def, atlases, nullptr, settings, state, notes, plain);
        expect(!usesImage(plain, hurt->sheets[0].image), "sin tipo, la nota usa el skin del mod");
        preview.styleForNote = [&](size_t) { return hurt; };
        RenderList typed;
        preview.build(*def, atlases, nullptr, settings, state, notes, typed);
        expect(usesImage(typed, hurt->sheets[0].image), "una nota Hurt se dibuja con game/notes/Hurt");
        expect(usesImage(typed, def->sheets[0].image), "los receptores siguen con el skin del mod");

        // Solo la linea del jugador salpica y puntua (PlayState.hx:1993-1995) y
        // el bot deja pasar la nota que su tipo evita.
        PreviewState played;
        std::vector<PreviewNote> mixed{{0, 1, 100.0, 0.0}, {1, 2, 200.0, 0.0}, {1, 3, 300.0, 0.0}};
        mixed[2].botSkips = true;
        advanceAutoplay(played, *def, mixed, 0.0, 400.0, {true, true}, 0.0);
        expect(played.hit.size() == 3 && played.hit[0] && played.hit[1] && !played.hit[2],
               "el bot toca las notas salvo la que su tipo evita");
        expect(played.splashes.size() == 1 && played.splashes[0].strumLine == 1 && played.splashes[0].note == 1,
               "solo salpica la linea del jugador, con la nota que la causo");
        expect(played.popups.size() == 1 && played.combo == 1, "solo puntua la linea del jugador");
        PreviewState swapped;
        swapped.playerLine = 0;
        advanceAutoplay(swapped, *def, mixed, 0.0, 400.0, {true, true}, 0.0);
        expect(swapped.splashes.size() == 1 && swapped.splashes[0].strumLine == 0,
               "jugando la linea del rival, salpica esa");

        // Capas del HUD de la vista previa: el juicio y el combo se ocultan,
        // se redimensionan y se mueven; la muestra se ve sin jugar.
        struct AnySize : IImageInfo {
            ImageInfo imageInfo(const std::string&) override { return {100, 40, true}; }
        } sizes;
        auto countImage = [](const RenderList& list, const std::string& prefix) {
            int n = 0;
            for (const DrawCmd& cmd : list.cmds)
                if (cmd.texture >= 0 && list.textures[static_cast<size_t>(cmd.texture)].rfind(prefix, 0) == 0) ++n;
            return n;
        };
        auto findImage = [](const RenderList& list, const std::string& image) -> const DrawCmd* {
            for (const DrawCmd& cmd : list.cmds)
                if (cmd.texture >= 0 && list.textures[static_cast<size_t>(cmd.texture)] == image) return &cmd;
            return nullptr;
        };
        const std::string sickImage = def->judgements[0].image;
        const std::string digitPrefix = "assets/images/game/score/num";
        PreviewSettings hud;
        hud.hudSample = true;
        PreviewState quiet;
        RenderList sample;
        preview.build(*def, atlases, &sizes, hud, quiet, {}, sample);
        const DrawCmd* sick = findImage(sample, sickImage);
        expect(sick != nullptr && countImage(sample, digitPrefix) == 3, "HUD de muestra: juicio y combo de tres cifras, sin jugar");
        hud.judgement.scale = 2.0f;
        hud.judgement.x = 30.0f;
        hud.judgement.alpha = 0.5f;
        RenderList moved;
        preview.build(*def, atlases, &sizes, hud, quiet, {}, moved);
        const DrawCmd* big = findImage(moved, sickImage);
        expect(sick && big && std::fabs(big->w - sick->w * 2.0f) < 0.01f &&
               std::fabs((big->x + big->w * 0.5f) - (sick->x + sick->w * 0.5f) - 30.0f) < 0.01f && std::fabs(big->alpha - 0.5f) < 0.001f,
               "el juicio se redimensiona, se mueve y se atenua");
        hud.judgement.visible = false;
        hud.combo.visible = false;
        RenderList hidden;
        preview.build(*def, atlases, &sizes, hud, quiet, {}, hidden);
        expect(!findImage(hidden, sickImage) && countImage(hidden, digitPrefix) == 0, "juicio y combo se ocultan");
    }
}

void psychCase(const fs::path& base) {
    std::printf("\n== Psych ==\n");
    const fs::path mod = base / "ps/mods/Test";
    writeSheet(mod / "images/noteSkins/NOTE_assets", baseGameFrames(false, true, "left confirm"));
    writeSheet(mod / "images/noteSkins/NOTE_assets-chip", baseGameFrames(true, true));
    writeFile(mod / "images/noteSkins/list.txt", "Chip\r\nFuture Funk\n");
    // Un nombre con espacio: `_` en las notas y `-` en las salpicaduras de 1.0.
    writeSheet(mod / "images/noteSkins/NOTE_assets-future_funk", baseGameFrames(false, true));
    writeSheet(mod / "images/noteSplashes/noteSplashes-future-funk",
               {"note splash purple 10000", "note splash blue 10000", "note splash green 10000", "note splash red 10000"});
    writeFile(mod / "images/noteSplashes/list.txt", "Future Funk\n");
    writeSheet(mod / "images/noteSplashes/noteSplashes",
               {"note splash diamond purple 10000", "note splash diamond blue 10000",
                "note splash diamond green 10000", "note splash diamond red 10000",
                "note splash diamond purple 20000", "note splash diamond blue 20000",
                "note splash diamond green 20000", "note splash diamond red 20000",
                "note splash diamond purple 30000"});
    writeFile(mod / "images/noteSplashes/noteSplashes.txt", "note splash diamond\n22 26\n-28 -52\n");
    writeSheet(mod / "images/customNotes", baseGameFrames(false, true));
    writeFile(mod / "data/bopeebo/bopeebo-hard.json",
              "{\"song\":{\"song\":\"Bopeebo\",\"arrowSkin\":\"customNotes\",\"splashSkin\":\"\",\"notes\":[]}}");
    writeFile(mod / "data/fresh/fresh.json",
              "{\"song\":{\"song\":\"Fresh\",\"arrowSkin\":\"customNotes\",\"notes\":[]}}");
    // Tipos de nota: un Lua que pone textura (con una linea comentada que no
    // cuenta), un .txt de 0.7 y la Hurt Note de serie.
    writeSheet(mod / "images/BULLETNOTE_assets", baseGameFrames(false, false));
    writeFile(mod / "custom_notetypes/Bullet Note.lua",
              "function onCreate()\n"
              "  -- setPropertyFromGroup('unspawnNotes', i, 'texture', 'OLD_assets')\n"
              "  for i = 0, getProperty('unspawnNotes.length')-1 do\n"
              "    if getPropertyFromGroup('unspawnNotes', i, 'noteType') == 'Bullet Note' then\n"
              "      setPropertyFromGroup('unspawnNotes', i, 'texture', 'BULLETNOTE_assets');\n"
              "      setPropertyFromGroup('unspawnNotes', i, 'ignoreNote', getPropertyFromGroup('unspawnNotes', i, 'mustPress'));\n"
              "    end\n  end\nend\n");
    writeSheet(mod / "images/MINE_assets", baseGameFrames(false, false));
    writeSheet(mod / "images/noteSplashes/noteSplashes-electric",
               {"note splash electric purple 10000", "note splash electric blue 10000",
                "note splash electric green 10000", "note splash electric red 10000"});
    writeFile(mod / "images/noteSplashes/noteSplashes-electric.txt", "note splash electric\n0 0\n");
    writeFile(mod / "custom_notetypes/Mine.txt", "texture: 'MINE_assets'\nhitCausesMiss: true\nignoreNote = true\n");
    // Lo que no es un tipo: el readme de la plantilla y una copia de seguridad oculta.
    writeFile(mod / "custom_notetypes/readme.txt", "Add your custom note type's .lua file here\nNote: one per type\n");
    writeFile(base / "ps/.fml/backups/1/mods/Test/custom_notetypes/Ghost.lua", "-- copia vieja\n");

    Scan s;
    scan(s, {base / "ps"});
    const EngineGuess guess = guessEngine(s.vfs, s.catalog);
    expect(guess.found && guess.engine == Engine::Psych, "motor detectado: Psych");
    const StyleReport* report = nullptr;
    const NoteStyle* def = styleById(s, "psych:mods/Test/noteSkins/NOTE_assets", &report);
    expect(def != nullptr && def->use == StyleUse::Default, "NOTE_assets de Psych es el skin del mod");
    if (def) {
        expect(def->rgbPalette, "Psych 0.7+ lleva paleta RGB");
        expect(countCode(*report, "FML-NOTE-001") == 1, "falta left confirm: una pieza sin dibujar");
        expect(countParts(*def, Part::Splash) == 8, "salpicaduras: dos variantes completas, la tercera no cuenta");
        const PartBinding* splashUp2 = findPart(*def, Part::Splash, 2, 1);
        expect(splashUp2 && splashUp2->animation.offsetX == -28.0f && splashUp2->animation.offsetY == -52.0f,
               "offsets del TXT: la unica linea vale para todas, dando la vuelta (NoteSplash.hx:143-149 de 0.7)");
        expect(countCode(*report, "FML-NOTE-004") == 22, "el mod no trae HUD: 22 recursos que pone el juego base o nadie");
    }
    const NoteStyle* chip = styleById(s, "psych:mods/Test/noteSkins/NOTE_assets-chip", &report);
    expect(chip != nullptr && chip->use == StyleUse::PlayerChoice, "NOTE_assets-chip es elegible (list.txt)");
    if (chip) expect(countCode(*report, "FML-NOTE-008") == 1 && countErrors(*report) == 0,
                     "la errata del juego base se acepta como alternativa, sin errores");
    const NoteStyle* funk = styleById(s, "psych:mods/Test/noteSkins/NOTE_assets-future_funk");
    const NoteStyle* funkSplash = styleById(s, "psych:mods/Test/noteSplashes/noteSplashes-future-funk");
    expect(funk && funk->use == StyleUse::PlayerChoice && funkSplash && funkSplash->use == StyleUse::PlayerChoice,
           "«Future Funk» en list.txt: NOTE_assets-future_funk y noteSplashes-future-funk (Note.hx:429, NoteSplash.hx:366)");
    const NoteStyle* song = styleById(s, "psych:mods/Test/song/customNotes", &report);
    expect(song != nullptr && song->use == StyleUse::Song, "arrowSkin de un chart es un skin por cancion");
    if (song) {
        expect(song->useDetail.find("Bopeebo") != std::string::npos &&
               song->useDetail.find("Fresh") != std::string::npos, "el skin por cancion lista sus dos canciones");
        expect(countErrors(*report) == 0, "el skin por cancion resuelve entero");
    }

    const NoteStyle* bullet = styleById(s, "psych:mods/Test/notetype/Bullet Note", &report);
    expect(bullet && bullet->use == StyleUse::NoteType && bullet->sheets.size() == 1 &&
           bullet->sheets[0].declared == "BULLETNOTE_assets", "Lua con textura: estilo del tipo (la linea comentada no cuenta)");
    if (bullet) {
        expect(!bullet->rgbPalette, "con textura propia Psych apaga la paleta RGB (Note.hx:369)");
        expect(countParts(*bullet, Part::StrumStatic) == 0 && countErrors(*report) == 0,
               "el tipo solo trae nota y sostenido, sin errores");
    }
    const NoteStyle* mine = styleById(s, "psych:mods/Test/notetype/Mine");
    expect(mine && mine->sheets[0].declared == "MINE_assets", ".txt de 0.7: su textura es la del tipo");
    const NoteStyle* hurtLook = styleById(s, "psych:mods/Test/notetype/Hurt Note", &report);
    expect(hurtLook != nullptr, "Hurt Note de serie: recolorea el skin del mod");
    if (hurtLook && def) {
        const std::vector<std::uint32_t> black{0xFF101010u, 0xFFFF0000u, 0xFF990022u};
        expect(hurtLook->rgbPalette && hurtLook->sheets[0].rgbFixed == black &&
               hurtLook->sheets[0].image == def->sheets[0].image, "Hurt Note: la imagen del skin con negro, rojo y granate fijos");
        expect(countParts(*hurtLook, Part::Note) == 4 && countParts(*hurtLook, Part::StrumStatic) == 0 &&
               countErrors(*report) == 0, "Hurt Note: cuatro notas y sus sostenidos, sin receptores ni errores");
        const PartBinding* electric = findPart(*hurtLook, Part::Splash, 0);
        const std::vector<std::uint32_t> sparks{0xFFFF0000u, 0xFF101010u, 0xFF990022u};
        expect(countParts(*hurtLook, Part::Splash) == 4 && electric && electric->sheet >= 0 &&
               hurtLook->sheets[static_cast<size_t>(electric->sheet)].rgbFixed == sparks,
               "Hurt Note: salpica con noteSplashes-electric en rojo y negro (Note.hx:211-213)");
        // La salpicadura de una nota Hurt sale de su tipo, no del skin.
        if (electric && electric->sheet >= 0) {
            AtlasStore atlases = atlasStoreOf(s);
            NotePreview preview;
            PreviewState state;
            state.songMs = 10.0;
            PreviewState::Splash splash;
            splash.note = 0;
            state.splashes.push_back(splash);
            preview.styleForNote = [&](size_t) { return hurtLook; };
            RenderList list;
            preview.build(*def, atlases, nullptr, PreviewSettings{}, state, {{1, 0, 5000.0, 0.0}}, list);
            expect(usesImage(list, hurtLook->sheets[static_cast<size_t>(electric->sheet)].image),
                   "la salpicadura de una nota Hurt es la electrica de su tipo");
        }
    }
    // En Psych la Hurt Note recolorea el skin EN USO: con el que eligio el
    // jugador (chip), sus notas son las de chip en negro y rojo.
    if (hurtLook && chip && bullet) {
        expect(recolorsSkin(*hurtLook) && !recolorsSkin(*bullet), "la Hurt Note recolorea el skin; Bullet trae su textura");
        const NoteStyle over = recolorOver(*chip, *hurtLook);
        const PartBinding* note = findPart(over, Part::Note, 2);
        const std::vector<std::uint32_t> black{0xFF101010u, 0xFFFF0000u, 0xFF990022u};
        expect(note && note->sheet >= 0 && over.sheets[static_cast<size_t>(note->sheet)].image == chip->sheets[0].image &&
               over.sheets[static_cast<size_t>(note->sheet)].rgbFixed == black,
               "Hurt Note sobre chip: la imagen de chip con los colores del tipo");
        expect(countParts(over, Part::Splash) == 4 && countParts(over, Part::StrumStatic) == 0 && over.id != hurtLook->id,
               "conserva la salpicadura electrica del tipo y no trae receptores");
    }
    const std::vector<NoteTypeEntry> types = scanNoteTypes(s.vfs, s.catalog);
    expect(types.size() == 7, "Psych: dos tipos del mod y cinco de serie, sin el readme ni la copia oculta (" +
           std::to_string(types.size()) + ")");
    expect(!findNoteType(types, "readme") && !findNoteType(types, "Ghost"), "readme.txt y .fml/ no son tipos");
    expect(!types.empty() && !types[0].builtin, "los del mod van antes que los de serie");
    const NoteTypeEntry* bulletType = findNoteType(types, "bullet note");
    expect(bulletType && bulletType->texture == "BULLETNOTE_assets" && bulletType->lookStyle == "psych:mods/Test/notetype/Bullet Note",
           "Bullet Note: su textura y su estilo");
    expect(bulletType && bulletType->bot == BotRule::IgnoresOnPlayerSide && !bulletType->hitMisses,
           "Bullet Note: ignoreNote = mustPress, el bot solo la deja pasar en el lado del jugador");
    const NoteTypeEntry* mineType = findNoteType(types, "Mine");
    expect(mineType && mineType->config == "mods/Test/custom_notetypes/Mine.txt" && mineType->script.empty() &&
           mineType->hitMisses && mineType->bot == BotRule::Ignores, "Mine (.txt): tocarla es fallo y el bot la ignora");
    const NoteTypeEntry* hurtType = findNoteType(types, "Hurt Note", Engine::Psych);
    expect(hurtType && hurtType->builtin && hurtType->hitMisses && hurtType->lookStyle == "psych:mods/Test/notetype/Hurt Note",
           "Hurt Note: de serie, tocarla es fallo y su aspecto es la recoloracion");
    expect(hurtType && botSkips(*hurtType, 1) && !botSkips(*hurtType, 0),
           "Hurt Note: el bot la deja pasar del lado del jugador y el rival la toca");
    const NoteTypeEntry* alt = findNoteType(types, "Alt Animation");
    expect(alt && alt->builtin && alt->lookStyle.empty() && alt->bot == BotRule::Hits, "Alt Animation: de serie, sin aspecto propio");
}

// Lo que el corpus real enseño el 28 sep: forks con nombres por letra, atlas que
// solo usan scripts y las dos generaciones de Psych en una misma instalacion.
void psychRealWorldCase(const fs::path& base) {
    std::printf("\n== Psych: casos del corpus ==\n");
    {
        // Psych Engine Extra Keys: notas por letra y receptores en reposo por
        // direccion (Note.hx:342-350, StrumNote.hx:81-83 del fork). Falta
        // `C confirm` a proposito.
        std::vector<std::string> lettered;
        for (const char* letter : {"A", "B", "C", "D", "E"})
            for (const char* suffix : {"", " hold", " tail", " press", " confirm"})
                if (std::string(letter) + suffix != "C confirm") lettered.push_back(std::string(letter) + suffix + "0000");
        for (const char* dir : {"LEFT", "DOWN", "UP", "RIGHT", "SPACE"}) lettered.push_back(std::string("arrow") + dir + "0000");
        writeSheet(base / "pk/ek/mods/EK/images/noteSkins/NOTE_assets", lettered);
        // Un tipo del fork con sus propios nombres (como RevPunch de Rev-Mixed).
        writeSheet(base / "pk/ek/mods/EK/images/Punch", {"left0000", "left hold0000", "left hold end0000"});
        writeFile(base / "pk/ek/mods/EK/custom_notetypes/Punch.lua",
                  "setPropertyFromGroup('unspawnNotes', i, 'texture', 'Punch')\n");
        Scan s;
        scan(s, {base / "pk/ek"});
        const StyleReport* report = nullptr;
        const NoteStyle* style = styleById(s, "psych:mods/EK/noteSkins/NOTE_assets", &report);
        expect(style != nullptr && style->letteredNaming, "fork con mas teclas: nombres por letra detectados");
        if (style) {
            const PartBinding* up = findPart(*style, Part::Note, 2);
            const PartBinding* rightEnd = findPart(*style, Part::HoldEnd, 3);
            const PartBinding* leftPress = findPart(*style, Part::StrumPress, 0);
            const PartBinding* downStill = findPart(*style, Part::StrumStatic, 1);
            expect(up && up->animation.prefix == "C0" && rightEnd && rightEnd->animation.prefix == "D tail" &&
                   leftPress && leftPress->animation.prefix == "A press" && downStill && downStill->animation.prefix == "arrowDOWN",
                   "letras de Extra Keys con 4 teclas: A B C D = izquierda abajo arriba derecha (Note.hx:55 del fork)");
            expect(countCode(*report, "FML-NOTE-016") == 1 && findingOf(*report, "FML-NOTE-016")->severity == Severity::Info,
                   "se dice una vez, como informacion, que va por letra y como se lee");
            expect(report->partsResolved == static_cast<int>(style->parts.size()) - 1 && countCode(*report, "FML-NOTE-001") == 1 &&
                   countErrors(*report) == 1, "pieza a pieza: solo falta el acierto de arriba (C confirm)");
        }
        const NoteStyle* punch = styleById(s, "psych:mods/EK/notetype/Punch", &report);
        expect(punch && punch->forkNaming && countCode(*report, "FML-NOTE-019") == 1 && countErrors(*report) == 0,
               "un tipo de nota del fork sigue sus nombres: un aviso, sin errores");
    }
    {
        // Scripts de Psych guardados en una instalacion de Codename (lo que hace
        // FML con psych-scripts/): Codename no los carga.
        const fs::path root = base / "pk/cnhost";
        writeSheet(root / "assets/images/game/notes/default", baseGameFrames(true, true));
        writeFile(root / "assets/data/notes/Punch.hx", "function onNoteCreation(e) {}\n");
        writeSheet(root / "assets/psych-scripts/songs/x/source/images/RevPunch", {"left0000"});
        writeFile(root / "assets/psych-scripts/songs/x/source/custom_notetypes/Punch.lua",
                  "setPropertyFromGroup('unspawnNotes', i, 'texture', 'RevPunch')\n");
        Scan s;
        scan(s, {root});
        const EngineGuess guess = guessEngine(s.vfs, s.catalog);
        expect(guess.found && guess.engine == Engine::Codename, "la instalacion sigue siendo de Codename");
        const StyleReport* report = nullptr;
        const NoteStyle* punch = styleById(s, "psych:assets/psych-scripts/songs/x/source/notetype/Punch", &report);
        expect(punch && countErrors(*report) > 0, "leido como Psych, su atlas no tiene los nombres de Psych");
        settleOtherEngines(s.catalog, s.reports, guess.engine);
        punch = styleById(s, "psych:assets/psych-scripts/songs/x/source/notetype/Punch", &report);
        expect(punch && countErrors(*report) == 0 && countCode(*report, "FML-NOTE-020") == 1,
               "de otro motor: sus fallos pasan a informacion, con el motivo");
        const NoteStyle* def = styleById(s, "codename:assets/game/notes/default", &report);
        expect(def && countCode(*report, "FML-NOTE-020") == 0, "lo del motor de la instalacion no cambia");
    }
    {
        writeSheet(base / "pk/old/assets/images/NOTE_assets", baseGameFrames(true, true));
        writeSheet(base / "pk/old/assets/shared/images/noteSplashes",
                   {"note splash purple 10000", "note splash blue 10000", "note splash green 10000", "note splash red 10000"});
        Scan s;
        scan(s, {base / "pk/old"});
        const NoteStyle* style = styleById(s, "psych:assets/NOTE_assets");
        expect(style != nullptr && countParts(*style, Part::Splash) == 4,
               "0.6: la salpicadura de assets/shared va con el skin de assets/ (misma instalacion)");
        expect(s.catalog.styles.size() == 1, "sin estilo suelto para esa salpicadura");
    }
    {
        writeSheet(base / "pk/mixed/assets/shared/images/NOTE_assets", baseGameFrames(true, true));
        writeSheet(base / "pk/mixed/assets/shared/images/noteSkins/NOTE_assets", baseGameFrames(false, true));
        Scan s;
        scan(s, {base / "pk/mixed"});
        const NoteStyle* legacy = styleById(s, "psych:assets/shared/NOTE_assets");
        const NoteStyle* current = styleById(s, "psych:assets/shared/noteSkins/NOTE_assets");
        expect(current != nullptr && current->use == StyleUse::Default, "0.7+: el de noteSkins es el de por defecto");
        expect(legacy != nullptr && !legacy->referenced, "el images/NOTE_assets antiguo queda como no cargado");
    }
    {
        writeSheet(base / "pk/matt/mods/M/images/noteSkins/NOTE_assets", baseGameFrames(false, true));
        writeSheet(base / "pk/matt/mods/M/images/noteSplashes/sustain_cover", {"cover0000"});
        Scan s;
        scan(s, {base / "pk/matt"});
        const StyleReport* report = nullptr;
        const NoteStyle* cover = styleById(s, "psych:mods/M/noteSplashes/sustain_cover", &report);
        expect(cover != nullptr && !cover->referenced, "atlas suelto en noteSplashes: no lo carga el motor");
        if (cover) expect(countErrors(*report) == 0 && countCode(*report, "FML-NOTE-015") == 1,
                          "sus fallos pasan a informacion, con el motivo");
    }
}

void vsliceCase(const fs::path& base) {
    std::printf("\n== V-Slice ==\n");
    const fs::path root = base / "vs";
    writeFile(root / "assets/data/notestyles/funkin.json", R"JSON({
  "version": "1.1.0", "name": "Funkin'", "author": "Test", "fallback": null,
  "assets": {
    "note": {"assetPath": "shared:notes", "scale": 0.7, "data": {
      "left": {"prefix": "noteLeft"}, "down": {"prefix": "noteDown"},
      "up": {"prefix": "noteUp"}, "right": {"prefix": "noteRight"}}},
    "holdNote": {"assetPath": "NOTE_hold_assets", "scale": 0.7, "data": {}},
    "noteStrumline": {"assetPath": "shared:noteStrumline", "scale": 0.7, "data": {
      "leftStatic": {"prefix": "staticLeft0"}, "leftPress": {"prefix": "pressLeft0"},
      "leftConfirm": {"prefix": "confirmLeft0"}, "leftConfirmHold": {"prefix": "confirmLeft0"},
      "downStatic": {"prefix": "staticDown0"}, "downPress": {"prefix": "pressDown0"},
      "downConfirm": {"prefix": "confirmDown0"}, "downConfirmHold": {"prefix": "confirmDown0"},
      "upStatic": {"prefix": "staticUp0"}, "upPress": {"prefix": "pressUp0"},
      "upConfirm": {"prefix": "confirmUp0"}, "upConfirmHold": {"prefix": "confirmUp0"},
      "rightStatic": {"prefix": "staticRight0"}, "rightPress": {"prefix": "pressRight0"},
      "rightConfirm": {"prefix": "confirmRight0"}, "rightConfirmHold": {"prefix": "confirmRight0"}}},
    "noteSplash": {"assetPath": "shared:noteSplashes", "data": {"enabled": true,
      "leftSplashes": [{"prefix": "note impact 1 purple0"}],
      "downSplashes": [{"prefix": "note impact 1  blue0"}],
      "upSplashes": [{"prefix": "note impact 1 green0"}],
      "rightSplashes": [{"prefix": "note impact 1 red0"}]}},
    "countdownThree": {"data": {"audioPath": "shared:gameplay/countdown/funkin/introTHREE"}, "assetPath": null},
    "countdownTwo": {"data": {"audioPath": "shared:gameplay/countdown/funkin/introTWO"},
                     "assetPath": "shared:ui/countdown/funkin/ready"},
    "judgementSick": {"assetPath": "default:ui/popup/funkin/sick", "scale": 0.65}
  }
})JSON");
    writeSheet(root / "assets/shared/images/notes", {"noteLeft0001", "noteDown0001", "noteUp0001", "noteRight0001"});
    writeFile(root / "assets/shared/images/NOTE_hold_assets.png", "PNG");
    std::vector<std::string> strum;
    for (const char* d : {"Left", "Down", "Up", "Right"})
        for (const char* st : {"static", "press", "confirm"}) strum.push_back(std::string(st) + d + "0001");
    writeSheet(root / "assets/shared/images/noteStrumline", strum);
    writeSheet(root / "assets/shared/images/noteSplashes",
               {"note impact 1 purple0001", "note impact 1  blue0001", "note impact 1 green0001", "note impact 1 red0001"});
    writeFile(root / "assets/shared/sounds/gameplay/countdown/funkin/introTHREE.ogg", "OGG");
    writeFile(root / "assets/shared/sounds/gameplay/countdown/funkin/introTWO.ogg", "OGG");
    writeFile(root / "assets/shared/images/ui/countdown/funkin/ready.png", "PNG");
    writeFile(root / "assets/images/ui/popup/funkin/sick.png", "PNG");
    // Lo que V-Slice aun trae del juego antiguo y no usa como Psych.
    writeSheet(root / "assets/images/NOTE_assets", {"noteLeft0001"});

    writeFile(root / "mods/sky/data/notestyles/sky.json", R"JSON({
  "name": "Sky", "fallback": "funkin",
  "assets": {"note": {"assetPath": "shared:ui/notes/sky_notes", "offsets": [15, 0], "data": {
    "left": {"prefix": "noteLeft"}, "down": {"prefix": "noteDown"},
    "up": {"prefix": "noteUp"}, "right": {"prefix": "noteRight"}}}}
})JSON");
    writeSheet(root / "mods/sky/shared/images/ui/notes/sky_notes", {"noteLeft0001", "noteDown0001", "noteUp0001", "noteRight0001"});
    writeFile(root / "mods/sky/data/notestyles/broken.json", R"JSON({"name": "Broken", "fallback": "missing", "assets": {}})JSON");
    writeFile(root / "mods/sky/data/notestyles/manifest.json", R"JSON({"entries": []})JSON");
    writeFile(root / "mods/sky/scripts/notekinds/SkyKinds.hxc",
              "class HurtKind extends NoteKind {\n  function new() { super('hurt', 'Hurts the player'); }\n}\n"
              "class SkyKind extends NoteKind {\n  function new() {\n    super(\"sky\", \"Sky note\", \"sky\");\n  }\n}\n");

    const auto shared = vsliceCandidates("shared:notes", "images", ".png");
    expect(shared.size() == 1 && shared[0] == "shared/images/notes.png", "shared:notes -> shared/images/notes.png");
    const auto def = vsliceCandidates("default:ui/popup/funkin/sick", "images", ".png");
    expect(def.size() == 1 && def[0] == "images/ui/popup/funkin/sick.png", "default: -> images/ de la raiz");
    const auto bare = vsliceCandidates("NOTE_hold_assets", "images", ".png");
    expect(bare.size() == 4 && bare[0] == "shared/images/NOTE_hold_assets.png" && bare[3] == "NOTE_hold_assets.png",
           "sin biblioteca: shared primero, como Paths; al final la ruta tal cual (0.9 de prueba)");

    Scan s;
    scan(s, {root});
    const EngineGuess guess = guessEngine(s.vfs, s.catalog);
    expect(guess.found && guess.engine == Engine::VSlice, "motor detectado: V-Slice");
    const StyleReport* report = nullptr;
    const NoteStyle* funkin = styleById(s, "vslice:assets/funkin", &report);
    expect(funkin != nullptr, "funkin de V-Slice encontrado");
    if (funkin) {
        expect(funkin->parts.size() == 32, "funkin: 4 notas, 8 sostenidos, 16 receptores y 4 salpicaduras");
        expect(report->partsResolved == 32, "funkin resuelve las 32 piezas (" + std::to_string(report->partsResolved) + ")");
        expect(report->findings.empty(), "funkin sin hallazgos: el doble espacio de «note impact 1  blue» coincide literal");
        expect(!funkin->countdown[1].sound.empty() && !funkin->countdown[1].image.empty(),
               "cuenta atras: imagen y sonido por biblioteca shared");
        expect(funkin->countdown[0].imageOptional, "el primer paso de la cuenta atras solo suena (assetPath null)");
        expect(funkin->judgements[0].image == "assets/images/ui/popup/funkin/sick.png", "default: resuelve en assets/images");
    }
    const NoteStyle* sky = styleById(s, "vslice:mods/sky/sky", &report);
    expect(sky != nullptr, "sky encontrado en su mod");
    if (sky) {
        expect(sky->sheets[0].image == "mods/sky/shared/images/ui/notes/sky_notes.png", "sky: su imagen en el mod");
        expect(report->partsInherited == 28 && report->partsResolved == 32,
               "sky hereda de funkin sostenidos, receptores y salpicaduras (" +
               std::to_string(report->partsInherited) + " heredadas)");
        expect(sky->sheets[0].offsetX == 15.0f, "sky conserva sus offsets");
    }
    bool missingFallback = false;
    for (const Finding& f : s.catalog.findings)
        missingFallback = missingFallback || (f.code == "FML-NOTE-005" && f.detail == "missing");
    expect(missingFallback, "fallback inexistente: aviso FML-NOTE-005");
    expect(styleById(s, "vslice:mods/sky/manifest") == nullptr, "manifest.json no es un estilo");
    bool psychInside = false;
    for (const NoteStyle& style : s.catalog.styles) psychInside = psychInside || style.engine == Engine::Psych;
    expect(!psychInside, "dentro de V-Slice, images/NOTE_assets y noteSplashes no se leen como Psych");
    // El final del sostenido como SustainTrail: medio alto dentro de la nota,
    // sobresale hasta el 90 % de la imagen y la ultima fila (el relleno que
    // pintaba una raya) no se ve.
    if (funkin) {
        struct StripSize : IImageInfo {
            ImageInfo imageInfo(const std::string&) override { return {416, 87, true}; }
        } sizes;
        AtlasStore atlases = atlasStoreOf(s);
        NotePreview preview;
        PreviewSettings settings;
        PreviewState state;
        const std::vector<PreviewNote> held{{1, 1, 500.0, 1000.0}};
        RenderList list;
        preview.build(*funkin, atlases, &sizes, settings, state, held, list);
        const DrawCmd* cap = nullptr;
        const DrawCmd* body = nullptr;
        for (const DrawCmd& cmd : list.cmds) {
            if (cmd.texture < 0 || list.textures[static_cast<size_t>(cmd.texture)] != "assets/shared/images/NOTE_hold_assets.png") continue;
            if (cmd.sx > 52.0f * 3.0f && cmd.sx < 52.0f * 4.0f) cap = &cmd;
            if (cmd.sx > 52.0f * 2.0f && cmd.sx < 52.0f * 3.0f) body = &cmd;
        }
        expect(cap && std::fabs(cap->sy) < 0.01f && std::fabs(cap->sh - 87.0f * 0.9f) < 0.01f,
               "V-Slice: el final se corta al 90 % de la tira (SustainTrail bottomClip)");
        if (cap && body) {
            const float tail = 1000.0f * 0.45f;    // kPixelsPerMs * velocidad 1
            const float nominalEnd = body->y + tail;
            expect(std::fabs(cap->y - (nominalEnd - 87.0f * 0.7f * 0.5f)) < 0.5f &&
                   std::fabs(cap->y + cap->h - (nominalEnd + 87.0f * 0.7f * 0.4f)) < 0.5f,
                   "V-Slice: el final empieza media imagen antes del final de la nota y sobresale un 40 %");
        }
    }
    const std::vector<NoteTypeEntry> kinds = scanNoteTypes(s.vfs, s.catalog);
    const NoteTypeEntry* hurtKind = findNoteType(kinds, "hurt", Engine::VSlice);
    const NoteTypeEntry* skyKind = findNoteType(kinds, "sky", Engine::VSlice);
    expect(kinds.size() == 2, "V-Slice: dos NoteKind en un mismo .hxc (" + std::to_string(kinds.size()) + ")");
    expect(hurtKind && hurtKind->description == "Hurts the player" && hurtKind->lookStyle.empty(),
           "hurt: su descripcion y sin notestyle propio");
    expect(skyKind && skyKind->lookStyle == "vslice:mods/sky/sky", "sky: su noteStyleId apunta al notestyle sky");
}

void textCase() {
    std::printf("\n== Textos ==\n");
    expect(scriptAssignment("setPropertyFromGroup('unspawnNotes', i, 'texture', 'X_assets');", "texture") == "'X_assets'",
           "Lua: el valor de setPropertyFromGroup");
    expect(scriptAssignment("note.texture = \"Y\"; // comentario", "texture") == "\"Y\"", "HScript: el valor de una asignacion");
    expect(scriptAssignment("if (note.texture == 'Z') trace(1);\n-- note.texture = 'W'", "texture").empty(),
           "ni una comparacion ni una linea comentada asignan");
    expect(scriptAssignment("var myTexture = 'Q';", "texture").empty(), "solo la palabra entera");
    expect(quotedValue("'A B'") == "A B" && quotedValue("A").empty() && quotedValue("'A' .. b").empty(),
           "texto entre comillas solo si es literal");
    expect(psychConfigValue("hitCausesMiss: true\r\nmissHealth = 0.5\n", "missHealth") == "0.5",
           ".txt de Psych: dos puntos o igual");
    Finding f;
    f.code = "FML-NOTE-001";
    f.subject = "note/left";
    f.detail = "purple0";
    f.path = "x.xml";
    const std::string en = describe(f, false);
    const std::string es = describe(f, true);
    expect(en.find("Note") != std::string::npos && en.find("left") != std::string::npos && en.find("purple0") != std::string::npos,
           "texto en ingles, con la pieza en palabras");
    expect(es.find("Nota") != std::string::npos && es.find("izquierda") != std::string::npos && es != en,
           "texto en espanol distinto, con la pieza en palabras");
    expect(readableSubject("strumStatic/left", true).find("Receptor en reposo") == 0 &&
           readableSubject("splash/up#1", true).find("variante 2") != std::string::npos &&
           readableSubject("countdown/three:sound", true).find("sonido de la cuenta") == 0 &&
           readableSubject("judgement/good", true).find("Bien") != std::string::npos &&
           readableSubject("sheet:shared:notes", false) == "sheet \"shared:notes\"",
           "sujetos legibles: piezas, variantes, cuenta atras, juicios y hojas");
}

// Canciones de los tres motores: se encuentran, se leen y su audio aparece.
void songsCase(const fs::path& base) {
    std::printf("\n== Canciones ==\n");
    {
        const fs::path root = base / "sg/cn";
        writeFile(root / "songs/tutorial/charts/hard.json", R"JSON({
  "codenameChart": true, "scrollSpeed": 2.0, "noteTypes": ["Hurt"],
  "events": [{"time": 500, "name": "Scroll Speed Change", "params": [false, 2.0, 4, "linear", null, true]}],
  "strumLines": [
    {"type": 0, "position": "dad", "characters": ["dad"], "scrollSpeed": 3.0, "notes": [
      {"time": 1000, "id": 0, "sLen": 0, "type": 0},
      {"time": 1500, "id": 1, "sLen": 500, "type": 1}]},
    {"type": 1, "position": "boyfriend", "characters": ["bf"], "notes": [
      {"time": 2000, "id": 2, "sLen": 0, "type": 0}]}]
})JSON");
        writeFile(root / "songs/tutorial/meta.json", R"JSON({"name": "tutorial", "bpm": 100, "difficulties": ["easy", "normal", "hard"]})JSON");
        writeFile(root / "songs/tutorial/song/Inst.ogg", "OGG");
        writeFile(root / "songs/tutorial/song/Voices.ogg", "OGG");
        Scan s;
        scan(s, {root});
        const std::vector<SongChart> songs = scanSongs(s.vfs);
        expect(songs.size() == 1 && songs[0].format == SongChart::Format::Codename && songs[0].id == "tutorial" &&
               songs[0].difficulty == "hard", "Codename: songs/<id>/charts/<dificultad>.json");
        if (!songs.empty()) {
            const LoadedSong loaded = loadSong(s.vfs, root, songs[0]);
            expect(loaded.error.empty() && loaded.chart.notes.size() == 3, "Codename: el chart se lee con sus 3 notas");
            expect(loaded.audio.size() == 2, "Codename: Inst y Voices en song/");
            std::vector<std::string> types;
            const std::vector<PreviewNote> notes = previewNotesOf(loaded.chart, &types);
            int opponent = 0, player = 0, hurt = 0;
            for (size_t i = 0; i < notes.size(); ++i) {
                (notes[i].strumLine == 0 ? opponent : player)++;
                if (i < types.size() && types[i] == "Hurt") ++hurt;
            }
            expect(opponent == 2 && player == 1, "Codename: dos notas del rival y una del jugador");
            expect(hurt == 1, "Codename: el tipo de nota viaja con su nota");
            const SongValues& values = loaded.values;
            expect(values.speed == 2.0f && values.bpm == 100.0f, "Codename: velocidad del chart y BPM de su meta.json");
            expect(values.speedChanges.size() == 1 && values.speedChanges[0].speed == 4.0f && values.speedChanges[0].durationMs == 0.0,
                   "Codename: «Scroll Speed Change» con el sexto parametro multiplica la de ese momento (PlayState.hx:1699-1703)");
            expect(speedAt(values, 100.0, 1) == 2.0f && speedAt(values, 1000.0, 1) == 4.0f,
                   "Codename: el jugador va a 2 y desde los 500 ms a 4");
            expect(speedAt(values, 1000.0, 0) == 3.0f, "Codename: la scrollSpeed propia de la linea del rival manda (StrumLine.hx:176-178)");
            expect(values.audioOffsetMs == 0.0, "Codename: el chart no guarda desfase de audio");
        }
    }
    {
        const fs::path root = base / "sg/ps";
        writeFile(root / "data/bopeebo/bopeebo-hard.json", R"JSON({"song": {
  "song": "Bopeebo", "bpm": 100, "speed": 1.5, "player1": "bf", "player2": "dad", "offset": 25,
  "arrowSkin": "customNotes", "splashSkin": "noteSplashes/noteSplashes-electric", "stage": "stage",
  "events": [[1000, [["Change Scroll Speed", "2", "0.5"]]]],
  "notes": [{"mustHitSection": true, "lengthInSteps": 16, "sectionNotes": [[1000, 0, 0], [1500, 5, 300]]}]
}})JSON");
        writeFile(root / "songs/bopeebo/Inst.ogg", "OGG");
        writeFile(root / "songs/bopeebo/Voices.ogg", "OGG");
        writeFile(root / "data/bopeebo/events.json", R"JSON({"song": {"events": [[3000, [["Change Scroll Speed", "1", "0"]]]]}})JSON");
        Scan s;
        scan(s, {root});
        const std::vector<SongChart> songs = scanSongs(s.vfs);
        expect(songs.size() == 1 && songs[0].format == SongChart::Format::Psych && songs[0].difficulty == "hard",
               "Psych: data/<cancion>/<cancion>-<dificultad>.json");
        if (!songs.empty()) {
            const LoadedSong loaded = loadSong(s.vfs, root, songs[0]);
            expect(loaded.error.empty() && loaded.chart.notes.size() == 2, "Psych: el chart se lee con sus 2 notas");
            expect(loaded.audio.size() == 2, "Psych: Inst y Voices en songs/<cancion>");
            const std::vector<PreviewNote> notes = previewNotesOf(loaded.chart);
            bool both = notes.size() == 2 && notes[0].strumLine != notes[1].strumLine;
            expect(both, "Psych: mustHitSection reparte las notas entre jugador y rival");
            const SongValues& values = loaded.values;
            expect(values.speed == 1.5f && values.audioOffsetMs == 25.0, "Psych: velocidad y `offset` del chart (PlayState.hx:679)");
            expect(values.arrowSkin == "customNotes" && values.splashSkin == "noteSplashes/noteSplashes-electric",
                   "Psych: arrowSkin y splashSkin de la cancion");
            expect(values.player == "bf" && values.opponent == "dad" && values.stage == "stage", "Psych: personajes y escenario");
            expect(values.speedChanges.size() == 2 && values.speedChanges[0].speed == 3.0f && values.speedChanges[0].durationMs == 500.0,
                   "Psych: «Change Scroll Speed» multiplica la del chart y tarda value2 segundos (PlayState.hx:2260-2264)");
            expect(speedAt(values, 3500.0, 1) == 1.5f, "Psych: tambien cuentan los eventos de events.json (PlayState.hx:1327)");
            expect(std::fabs(speedAt(values, 1250.0, 1) - 2.25f) < 0.001f && speedAt(values, 2000.0, 1) == 3.0f,
                   "Psych: a mitad de la transicion va por la mitad; despues, a 3");
        }
    }
    {
        const fs::path root = base / "sg/vs";
        writeFile(root / "data/songs/test/test-chart.json", R"JSON({"version": "2.0.0",
  "scrollSpeed": {"hard": 1.8}, "events": [],
  "notes": {"hard": [{"t": 1000, "d": 0}, {"t": 1200, "d": 5, "l": 400, "k": "hurt"}]},
  "events": [{"t": 1000, "e": "ScrollSpeed", "v": {"scroll": 2, "duration": 4, "ease": "linear", "strumline": "player", "absolute": false}},
             {"t": 3000, "e": "ScrollSpeed", "v": {"scroll": 1, "ease": "INSTANT", "absolute": true}}]})JSON");
        writeFile(root / "data/songs/test/test-metadata.json", R"JSON({"version": "2.2.0", "songName": "Test",
  "timeChanges": [{"t": 0, "bpm": 120}],
  "playData": {"difficulties": ["hard"], "stage": "mainStage", "noteStyle": "funkin",
               "characters": {"player": "bf", "opponent": "dad", "girlfriend": "gf"}},
  "offsets": {"instrumental": -10}})JSON");
        writeFile(root / "songs/test/Inst.ogg", "OGG");
        writeFile(root / "songs/test/Voices-bf.ogg", "OGG");
        writeFile(root / "songs/test/Voices-dad.ogg", "OGG");
        Scan s;
        scan(s, {root});
        const std::vector<SongChart> songs = scanSongs(s.vfs);
        expect(songs.size() == 1 && songs[0].format == SongChart::Format::VSlice && songs[0].id == "Test",
               "V-Slice: <id>-chart.json con su -metadata.json");
        if (!songs.empty()) {
            const LoadedSong loaded = loadSong(s.vfs, root, songs[0]);
            expect(loaded.error.empty() && loaded.chart.notes.size() == 2, "V-Slice: el chart se lee con sus 2 notas");
            expect(loaded.audio.size() == 3, "V-Slice: Inst y las voces separadas de jugador y rival");
            std::vector<std::string> types;
            const std::vector<PreviewNote> notes = previewNotesOf(loaded.chart, &types);
            bool sides = notes.size() == 2 && notes[0].strumLine == 1 && notes[1].strumLine == 0;
            expect(sides, "V-Slice: d 0-3 es el jugador y 4-7 el rival");
            expect(types.size() == 2 && types[1] == "hurt", "V-Slice: el kind de la nota viaja con ella");
            const SongValues& values = loaded.values;
            expect(values.speed == 1.8f && values.noteStyle == "funkin" && values.audioOffsetMs == -10.0,
                   "V-Slice: velocidad de la dificultad, noteStyle y offsets.instrumental");
            expect(values.speedChanges.size() == 2 && values.speedChanges[0].strumLine == 1 &&
                   std::fabs(values.speedChanges[0].speed - 3.6f) < 0.001f && values.speedChanges[0].durationMs == 500.0,
                   "V-Slice: «ScrollSpeed» relativo multiplica la de la dificultad y dura 4 pasos a 120 BPM (ScrollSpeedEvent.hx:62-90)");
            expect(speedAt(values, 2000.0, 1) > 3.5f && speedAt(values, 2000.0, 0) == 1.8f,
                   "V-Slice: el cambio de la linea del jugador no toca al rival");
            expect(speedAt(values, 3000.0, 0) == 1.0f && speedAt(values, 3000.0, 1) == 1.0f,
                   "V-Slice: INSTANT y absoluto, en las dos lineas");
        }
    }
}

// «Distribuir»: porcentaje con semilla sobre una copia, respetando los tipos
// que ya tiene el chart.
void distributeCase() {
    std::printf("\n== Distribuir ==\n");
    std::vector<PreviewNote> notes;
    std::vector<std::string> types;
    for (int i = 0; i < 200; ++i) {
        PreviewNote note;
        note.strumLine = i % 2;
        note.lane = i % 4;
        note.timeMs = 100.0 * i;
        note.sustainMs = i % 10 == 0 ? 300.0 : 0.0;
        notes.push_back(note);
        types.push_back(i % 25 == 0 ? "Bullet" : "");
    }
    PreviewNote chordA;
    chordA.strumLine = 1;
    chordA.lane = 0;
    chordA.timeMs = 50000.0;
    PreviewNote chordB = chordA;
    chordB.lane = 3;
    notes.push_back(chordA);
    types.push_back("");
    notes.push_back(chordB);
    types.push_back("");

    DistributeRequest request;
    request.seed = 7;
    request.rules.push_back({"Hurt Note", 20.0f, 0});
    const DistributeResult first = distributeTypes(notes, types, request);
    const DistributeResult again = distributeTypes(notes, types, request);
    expect(first.types == again.types && first.placed[0] > 0, "misma semilla, mismo resultado");
    int preserved = 0;
    for (size_t i = 0; i < types.size(); ++i)
        if (types[i] == "Bullet" && first.types[i] == "Bullet") ++preserved;
    expect(preserved == 8, "las notas que ya traen un tipo se respetan");
    expect(first.placed[0] > first.candidates * 0.12 && first.placed[0] < first.candidates * 0.28,
           "alrededor del 20 % de las candidatas (" + std::to_string(first.placed[0]) + " de " + std::to_string(first.candidates) + ")");
    request.seed = 8;
    expect(distributeTypes(notes, types, request).types != first.types, "otra semilla, otro reparto");
    request.rules[0].percent = 100.0f;
    const DistributeResult all = distributeTypes(notes, types, request);
    expect(all.placed[0] == all.candidates && all.candidates == 194, "100 %: todas las candidatas (202 notas menos las 8 que ya traen tipo)");
    request.filter.side = 1;
    request.filter.skipSustains = true;
    request.filter.skipChords = true;
    const DistributeResult filtered = distributeTypes(notes, types, request);
    bool onlyAllowed = true;
    for (size_t i = 0; i < notes.size(); ++i)
        if (filtered.types[i] == "Hurt Note" && (notes[i].strumLine != 1 || notes[i].sustainMs > 0.0 || notes[i].timeMs == 50000.0))
            onlyAllowed = false;
    expect(onlyAllowed && filtered.placed[0] > 0, "filtros: solo el jugador, sin notas largas ni acordes");
    request.filter = DistributeFilter{};
    request.filter.minGapMs = 350.0;
    const DistributeResult spaced = distributeTypes(notes, types, request);
    double last[2] = {-1e9, -1e9};
    bool gapKept = true;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (spaced.types[i] != "Hurt Note") continue;
        const size_t line = notes[i].strumLine == 0 ? 0 : 1;
        if (notes[i].timeMs - last[line] < 350.0) gapKept = false;
        last[line] = notes[i].timeMs;
    }
    expect(gapKept && spaced.placed[0] > 0, "separacion minima entre dos del mismo tipo en su linea");
    request.filter = DistributeFilter{};
    request.rules[0].percent = 10.0f;
    request.rules.push_back({"Bullet Note", 50.0f, 99});
    const DistributeResult both = distributeTypes(notes, types, request);
    int hurt = 0, bullet = 0;
    for (const std::string& type : both.types) {
        if (type == "Hurt Note") ++hurt;
        if (type == "Bullet Note") ++bullet;
    }
    expect(hurt == both.placed[0] && bullet == both.placed[1] && hurt > 0 && bullet > 0,
           "dos tipos en el mismo paquete, ninguna nota con los dos");
}

// El proyecto .fmlnote: un estilo entero y un proyecto van y vuelven iguales.
void projectCase() {
    std::printf("\n== Proyecto .fmlnote ==\n");
    NoteStyle style;
    style.engine = Engine::VSlice;
    style.id = "vslice:mods/x/sky";
    style.name = "Sky";
    style.use = StyleUse::Declared;
    style.fallback = "funkin";
    style.referenced = false;
    style.forkNaming = true;
    Sheet notes;
    notes.image = "mods/x/images/notes.png";
    notes.atlas = "mods/x/images/notes.xml";
    notes.declared = "shared:notes";
    notes.scale = 0.7f;
    notes.offsetX = 15.0f;
    notes.rgbFixed = {0xFF101010u, 0xFFFF0000u, 0xFF990022u};
    Sheet strip;
    strip.kind = SheetKind::Strip;
    strip.columns = 8;
    strip.image = "mods/x/images/NOTE_hold_assets.png";
    strip.pixel = true;
    const int a = addSheet(style, notes);
    const int b = addSheet(style, strip);
    Animation anim;
    anim.prefix = "noteLeft";
    anim.alternatives = {"purple0"};
    anim.indices = {0, 2, 1};
    anim.fps = 30.0f;
    anim.loop = true;
    anim.offsetX = -3.5f;
    bindPart(style, Part::Note, 0, a, anim);
    Animation holdEnd;
    holdEnd.indices = {1};
    bindPart(style, Part::HoldEnd, 0, b, holdEnd);
    style.parts.back().inherited = true;
    style.parts.front().unread = true;
    style.lookScript = "mods/x/data/notes/Fire.hx";
    style.hasHud = true;
    style.judgements[0].image = "mods/x/images/sick.png";
    style.judgements[0].scale = 0.65f;
    style.countdown[0].imageOptional = true;
    style.countdown[0].sound = "mods/x/sounds/three.ogg";

    const auto again = readStyle(writeStyle(style));
    expect(again && again->id == style.id && again->engine == Engine::VSlice && again->use == StyleUse::Declared &&
           again->fallback == "funkin" && !again->referenced && again->forkNaming, "estilo: identidad, uso y banderas");
    expect(again && again->sheets.size() == 2 && again->sheets[0].rgbFixed == notes.rgbFixed && again->sheets[0].offsetX == 15.0f &&
           again->sheets[1].kind == SheetKind::Strip && again->sheets[1].columns == 8 && again->sheets[1].pixel,
           "estilo: hojas, paleta fija y tira");
    expect(again && again->parts.size() == 2 && again->parts[0].animation.prefix == "noteLeft" &&
           again->parts[0].animation.alternatives == anim.alternatives && again->parts[0].animation.indices == anim.indices &&
           again->parts[0].animation.loop && again->parts[0].animation.offsetX == -3.5f && again->parts[1].part == Part::HoldEnd &&
           again->parts[1].inherited, "estilo: piezas con su animacion");
    expect(again && again->lookScript == style.lookScript && again->parts.size() == 2 && again->parts[0].unread &&
           !again->parts[1].unread, "estilo: el script que pone el aspecto y las piezas que no se leen");
    expect(again && again->hasHud && again->judgements[0].scale == 0.65f && again->countdown[0].imageOptional &&
           again->countdown[0].sound == "mods/x/sounds/three.ogg", "estilo: HUD");

    NoteProject project;
    project.engineChoice = 3;
    project.baseRoot = "C:/Juego base";
    ProjectSource source;
    source.path = "C:/Mods/Mi mod \xC3\xB1";
    source.imports.push_back({"C:/img/a.png", "C:/img/a.xml", "notelab-import/1/a.png", "notelab-import/1/a.xml"});
    ProjectEdit edit;
    edit.styleId = style.id;
    edit.style = style;
    edit.files = {{"mods/x/images/notes.png", "abc123"}};
    source.edits.push_back(edit);
    ProjectEdit variant = edit;
    variant.styleId = "notelab:variant1:" + style.id;
    variant.style.id = variant.styleId;
    variant.created = true;
    source.edits.push_back(variant);
    ProjectTypeBlocks mina{"Mina \xC3\xB1", {}};
    for (const BlockPreset& preset : blockPresets())
        if (std::string(preset.key) == "mine" || std::string(preset.key) == "coin") addPreset(mina.program, preset);
    source.typeBlocks.push_back(mina);
    ProjectTypeBlocks grito{"Grito", {}};
    for (const BlockPreset& preset : blockPresets())
        if (std::string(preset.key) == "altAnim") addPreset(grito.program, preset);
    source.typeBlocks.push_back(grito);
    project.sources.push_back(source);
    project.selectedSource = 0;
    project.selectedStyle = style.id;
    project.selectedType = "Hurt Note";
    project.songSource = 0;
    project.songId = "bopeebo";
    project.songDifficulty = "hard";
    project.songVariation = "erect";
    project.view.manual = true;
    project.view.playSide = 0;
    project.view.judgement.scale = 1.5f;
    project.view.score.visible = false;
    project.view.scrollSpeed = 2.5f;
    project.distributed = true;
    project.distribute.seed = 4242;
    project.distribute.rules.push_back({"Hurt Note", 12.5f, 0});
    project.distribute.rules.push_back({"Bullet", 3.0f, 99});
    project.distribute.filter.side = 1;
    project.distribute.filter.lanes = {true, false, true, false};
    project.distribute.filter.minGapMs = 250.0;
    std::string error;
    const auto loaded = readProject(writeProject(project), error);
    expect(loaded && error.empty() && loaded->engineChoice == 3 && loaded->baseRoot == "C:/Juego base", "proyecto: motor y juego base");
    expect(loaded && loaded->sources.size() == 1 && loaded->sources[0].path == source.path && loaded->sources[0].imports.size() == 1 &&
           loaded->sources[0].imports[0].atlasVirtual == "notelab-import/1/a.xml", "proyecto: fuentes (con acentos) e importaciones");
    expect(loaded && loaded->sources[0].edits.size() == 2 && loaded->sources[0].edits[1].created && !loaded->sources[0].edits[0].created,
           "proyecto: las variantes creadas se distinguen de lo editado del mod");
    expect(loaded && loaded->sources[0].edits.size() == 2 && loaded->sources[0].edits[0].files[0].sha256 == "abc123" &&
           loaded->sources[0].edits[0].style.parts[0].animation.prefix == "noteLeft",
           "proyecto: estilos editados con la huella de sus archivos");
    expect(loaded && loaded->selectedStyle == style.id && loaded->selectedType == "Hurt Note" && loaded->songId == "bopeebo" &&
           loaded->songVariation == "erect", "proyecto: lo elegido y la cancion");
    expect(loaded && loaded->sources[0].typeBlocks.size() == 2 && loaded->sources[0].typeBlocks[0].type == "Mina \xC3\xB1" &&
           writeProgram(loaded->sources[0].typeBlocks[0].program) == writeProgram(mina.program) &&
           writeProgram(loaded->sources[0].typeBlocks[1].program) == writeProgram(grito.program) &&
           blocksAvoid(loaded->sources[0].typeBlocks[0].program) && activeBlocks(loaded->sources[0].typeBlocks[0].program) > 5,
           "proyecto: el programa de bloques de cada tipo, tambien los de tipos nuevos");
    expect(loaded && loaded->view.manual && loaded->view.playSide == 0 && loaded->view.judgement.scale == 1.5f &&
           !loaded->view.score.visible && loaded->view.scrollSpeed == 2.5f, "proyecto: la vista previa");
    expect(loaded && loaded->distributed && loaded->distribute.seed == 4242 && loaded->distribute.rules.size() == 2 &&
           loaded->distribute.rules[0].percent == 12.5f && loaded->distribute.rules[1].seed == 99 && loaded->distribute.filter.side == 1 &&
           !loaded->distribute.filter.lanes[1] && loaded->distribute.filter.minGapMs == 250.0,
           "proyecto: el paquete de «Distribuir» con sus semillas y filtros");
    std::string refused;
    expect(!readProject("{\"format\":\"fml-notelab-project\",\"version\":99}", refused) && refused.find("99") != std::string::npos,
           "proyecto de una version posterior: se rechaza y se dice");
    expect(!readProject("no es json", refused), "texto que no es un proyecto: se rechaza");
}

// ------------------------------------------------------------- exportar --

// Una hoja de verdad: cada fotograma un cuadrado de `size` px de su color, con
// un borde transparente de 1 px, en fila.
void writeRealSheet(const fs::path& withoutExtension, const std::vector<std::string>& frames, int size,
                    const std::function<std::uint32_t(size_t)>& colorOf) {
    Image sheet = blankImage(size * static_cast<int>(frames.size()), size);
    std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<TextureAtlas imagePath=\"sheet.png\">\n";
    for (size_t i = 0; i < frames.size(); ++i) {
        const std::uint32_t color = colorOf(i);
        for (int y = 1; y < size - 1; ++y)
            for (int x = 1; x < size - 1; ++x) {
                std::uint8_t* p = sheet.at(static_cast<int>(i) * size + x, y);
                p[0] = static_cast<std::uint8_t>((color >> 16) & 0xFF);
                p[1] = static_cast<std::uint8_t>((color >> 8) & 0xFF);
                p[2] = static_cast<std::uint8_t>(color & 0xFF);
                p[3] = static_cast<std::uint8_t>(color >> 24);
            }
        xml += "  <SubTexture name=\"" + frames[i] + "\" x=\"" + std::to_string(i * size) + "\" y=\"0\" width=\"" +
               std::to_string(size) + "\" height=\"" + std::to_string(size) + "\"/>\n";
    }
    xml += "</TextureAtlas>\n";
    const std::vector<unsigned char> png = encodePng(sheet);
    writeFile(fs::u8path(withoutExtension.u8string() + ".png"), std::string(png.begin(), png.end()));
    writeFile(fs::u8path(withoutExtension.u8string() + ".xml"), xml);
}

void writeRealPng(const fs::path& path, int w, int h, std::uint32_t color) {
    Image image = blankImage(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = image.at(x, y);
            p[0] = static_cast<std::uint8_t>((color >> 16) & 0xFF);
            p[1] = static_cast<std::uint8_t>((color >> 8) & 0xFF);
            p[2] = static_cast<std::uint8_t>(color & 0xFF);
            p[3] = 0xFF;
        }
    const std::vector<unsigned char> png = encodePng(image);
    writeFile(path, std::string(png.begin(), png.end()));
}

const ExportFile* fileOf(const ExportPackage& p, const std::string& path) {
    for (const ExportFile& file : p.files)
        if (file.path == path) return &file;
    return nullptr;
}

std::string textOf(const ExportPackage& p, const std::string& path) {
    const ExportFile* file = fileOf(p, path);
    return file ? std::string(file->bytes.begin(), file->bytes.end()) : std::string();
}

int countNote(const ExportPackage& p, const std::string& code) {
    int n = 0;
    for (const ExportNote& note : p.notes) if (note.code == code) ++n;
    return n;
}

// Un fotograma del atlas exportado: su rectangulo y el color de su centro (ARGB).
bool exportedFrame(const ExportPackage& p, const std::string& atlasPath, const std::string& name, AtlasFrame& frame,
                   std::uint32_t& center) {
    const ExportFile* png = fileOf(p, atlasPath + ".png");
    if (!png) return false;
    Image image;
    if (!decodePng(png->bytes, image)) return false;
    DiagnosticSink sink;
    auto atlas = parseSparrowAtlas(textOf(p, atlasPath + ".xml"), atlasPath, sink);
    if (!atlas) return false;
    for (const AtlasFrame& f : atlas.value().frames) {
        if (f.name != name) continue;
        frame = f;
        const std::uint8_t* px = image.at(f.x + f.w / 2, f.y + f.h / 2);
        center = (static_cast<std::uint32_t>(px[3]) << 24) | (static_cast<std::uint32_t>(px[0]) << 16) |
                 (static_cast<std::uint32_t>(px[1]) << 8) | px[2];
        return true;
    }
    return false;
}

ExportIo ioOf(const Scan& s) {
    ExportIo io;
    io.readBytes = [&s](const std::string& path) { return s.vfs.readBytes(path); };
    io.readText = [&s](const std::string& path) { return s.vfs.readText(path); };
    return io;
}

// Exportar a los tres motores: nombres que busca cada uno, escalas, paleta de
// Psych, la tira de V-Slice, la relectura con los lectores de Note Lab, la
// guia y la escritura a carpeta y ZIP.
void exportCase(const fs::path& base) {
    std::printf("\n== Exportar ==\n");
    expect(exportName("Mí Skin!") == "m-skin" && exportName("  ") == "notelab" && exportName("Future_Funk 2") == "future_funk-2" &&
               exportName("Wii Funkin' - VS Matt") == "wii-funkin-vs-matt",
           "exportName: minusculas, cifras, - y _");
    float anchorX = 0.0f, anchorY = 0.0f;
    const bool psychAnchor = splashAnchor(Engine::Psych, 225.0f, 226.0f, anchorX, anchorY);
    expect(psychAnchor && std::fabs(anchorX + 58.0f + 1.9f) < 0.01f && std::fabs(anchorY + 55.0f + 10.0f) < 0.01f,
           "ancla de Psych: la salpicadura por defecto (225x226, offsets -58 -55) queda casi centrada en la flecha");
    expect(splashAnchor(Engine::Codename, 10.0f, 10.0f, anchorX, anchorY) && anchorX == 0.0f &&
           !splashAnchor(Engine::VSlice, 10.0f, 10.0f, anchorX, anchorY),
           "Codename la centra; V-Slice no tiene ancla conocida");

    // Codename: el skin por defecto con receptores, dos salpicaduras por
    // flecha y parte del HUD.
    const fs::path cn = base / "export-cn/mods/Skin";
    const auto colorOf = [](size_t i) { return 0xFF000000u | (static_cast<std::uint32_t>(i * 0x2F4A6Bu + 0x303030u) & 0xFFFFFFu); };
    writeRealSheet(cn / "images/game/notes/default", baseGameFrames(true, true), 16, colorOf);
    std::vector<std::string> impacts;
    for (int v = 1; v <= 2; ++v)
        for (const char* color : {"purple", "blue", "green", "red"})
            impacts.push_back("note impact " + std::to_string(v) + " " + color + "0000");
    writeRealSheet(cn / "images/game/splashes/default", impacts, 24, colorOf);
    std::string splashXml = "<splashes sprite=\"game/splashes/default\" alpha=\"0.6\">\n";
    for (int d = 0; d < 4; ++d) {
        static const char* colors[4] = {"purple", "blue", "green", "red"};
        splashXml += "<strum id=\"" + std::to_string(d) + "\">";
        for (int v = 1; v <= 2; ++v)
            splashXml += "<anim name=\"s" + std::to_string(v) + "\" anim=\"note impact " + std::to_string(v) + " " + colors[d] + "\" fps=\"24\"/>";
        splashXml += "</strum>\n";
    }
    writeFile(cn / "data/splashes/default.xml", splashXml + "</splashes>\n");
    writeRealPng(cn / "images/game/score/sick.png", 40, 20, 0xFF40C040u);
    writeRealPng(cn / "images/game/score/num0.png", 10, 12, 0xFFFFFFFFu);
    writeRealPng(cn / "images/game/ready.png", 30, 10, 0xFFFFFF00u);
    writeFile(cn / "sounds/intro3.ogg", "OggS-test");
    writeFile(cn / "sounds/intro2.mp3", "ID3-test");

    Scan s;
    scan(s, {base / "export-cn"});
    const NoteStyle* skin = styleById(s, "codename:mods/Skin/game/notes/default");
    expect(skin != nullptr, "exportar: el skin de Codename de prueba se lee");
    if (!skin) return;
    const ExportIo io = ioOf(s);

    ExportOptions o;
    o.target = Engine::Codename;
    o.role = ExportRole::SongSkin;
    expect(countNote(buildExport(*skin, o, io), "FML-EXPORT-029") == 1, "Codename no tiene skin por cancion: se dice");
    o.role = ExportRole::NoteType;
    expect(countNote(buildExport(*skin, o, io), "FML-EXPORT-030") == 1, "tipo de nota sin nombre: se dice");

    // Codename -> Psych, elegible en Opciones.
    o.target = Engine::Psych;
    o.role = ExportRole::Selectable;
    o.name = "Mi Skin";
    ExportPackage psych = buildExport(*skin, o, io);
    expect(psych.options.name == "mi-skin" && fileOf(psych, "images/noteSkins/NOTE_assets-mi-skin.png") &&
           textOf(psych, "images/noteSkins/list.txt") == "mi-skin\n" && textOf(psych, "pack.json").find("\"runsGlobally\": true") != std::string::npos,
           "Psych elegible: el skin, su linea de list.txt y pack.json global");
    AtlasFrame frame;
    std::uint32_t center = 0;
    const std::string skinXml = textOf(psych, "images/noteSkins/NOTE_assets-mi-skin.xml");
    expect(skinXml.find("\"purple0000\"") != std::string::npos && skinXml.find("\"pruple end hold0000\"") != std::string::npos &&
           skinXml.find("\"arrowLEFT0000\"") != std::string::npos && skinXml.find("\"left confirm0000\"") != std::string::npos,
           "los nombres que buscan Note.hx y StrumNote.hx, con la errata del final morado");
    expect(exportedFrame(psych, "images/noteSkins/NOTE_assets-mi-skin", "purple0000", frame, center) &&
           frame.w == 16 && center == colorOf(0), "misma escala 0.7: el fotograma y su color no cambian");
    const std::string splashJson = textOf(psych, "images/noteSplashes/noteSplashes-mi-skin.json");
    const nlohmann::json splash = nlohmann::json::parse(splashJson, nullptr, false);
    expect(splash.is_object() && splash["animations"]["purple2"]["noteData"] == 4 &&
           splash["animations"]["purple1"]["prefix"] == "note splash purple 1" && splash["allowRGB"] == false,
           "salpicaduras de 1.0: noteData = flecha + variante * 4 y sin paleta");
    expect(splash.is_object() && std::fabs(splash["animations"]["purple1"]["offsets"][0].get<double>() - (12.0 - 172.4)) < 0.01,
           "offset de la salpicadura convertido del centro de Codename al ancla de Psych");
    expect(textOf(psych, "images/noteSplashes/noteSplashes-mi-skin.txt").rfind("note splash\n24 24\n", 0) == 0,
           "y el TXT de 0.7: nombre, FPS y un offset por animacion");
    expect(countNote(psych, "FML-EXPORT-003") == 1, "aviso: Psych recolorearia un skin de colores elegido en Opciones");
    expect(fileOf(psych, "images/sick.png") && fileOf(psych, "images/num0.png") && fileOf(psych, "images/ready.png") &&
           fileOf(psych, "sounds/intro3.ogg") && !fileOf(psych, "sounds/intro2.ogg") && countNote(psych, "FML-EXPORT-013") == 1,
           "HUD a sus rutas de Psych; el sonido que no es OGG se dice");
    expect(verifyExport(psych, base) && psych.verifiedStyles.size() == 2 && psych.verifyErrors == 0,
           "Note Lab relee el paquete de Psych: skin y salpicaduras, sin errores");

    // Codename -> V-Slice.
    o.target = Engine::VSlice;
    o.role = ExportRole::SongSkin;
    o.name = "mi-skin";
    ExportPackage vs = buildExport(*skin, o, io);
    const nlohmann::json style = nlohmann::json::parse(textOf(vs, "data/notestyles/mi-skin.json"), nullptr, false);
    expect(style.is_object() && style["fallback"] == "funkin" && style["assets"]["note"]["scale"] == 0.7 &&
           style["assets"]["note"]["data"]["left"]["prefix"] == "noteLeft" &&
           style["assets"]["noteStrumline"]["data"]["leftConfirmHold"]["prefix"] == "confirmLeft" &&
           style["assets"]["noteSplash"]["alpha"] == 0.6 && style["assets"].contains("judgementSick") &&
           style["assets"]["judgementSick"]["scale"] == 0.7 && style["assets"]["comboNumber0"]["scale"] == 0.5,
           "notestyle de V-Slice: grupos, escalas, confirm mantenido = confirm y HUD con su escala declarada");
    Image strip;
    const ExportFile* holds = fileOf(vs, "images/notelab/mi-skin/holds.png");
    const bool stripRead = holds && decodePng(holds->bytes, strip);
    expect(stripRead && strip.w == 8 * 16 && strip.h == 2 * 16,
           "tira de sostenidos: ocho columnas y el doble del final de alto (" + std::to_string(strip.w) + "x" + std::to_string(strip.h) + ")");
    expect(!strip.empty() && strip.at(16 + 8, 8)[3] == 0xFF && strip.at(16 + 8, 24)[3] == 0,
           "el final dentro de la mitad de arriba; debajo, transparente");
    expect(fileOf(vs, "_polymod_meta.json") && textOf(vs, "_polymod_meta.json").find("\"api_version\": \"0.8.0\"") != std::string::npos,
           "V-Slice: es un mod entero, con _polymod_meta.json");
    expect(verifyExport(vs, base) && vs.verifiedStyles.size() == 1 && vs.verifiedStyles[0] == "vslice:mi-skin",
           "Note Lab relee el notestyle exportado sin errores");

    // Codename -> Codename: tipo de nota con su salpicadura.
    o.target = Engine::Codename;
    o.role = ExportRole::NoteType;
    o.noteType = "Bala Roja";
    ExportPackage typed = buildExport(*skin, o, io);
    expect(fileOf(typed, "images/game/notes/Bala Roja.png") && textOf(typed, "images/game/notes/Bala Roja.xml").find("arrowLEFT") == std::string::npos &&
           textOf(typed, "data/notes/Bala Roja.hx").find("event.note.splash = \"Bala Roja\"") != std::string::npos &&
           !fileOf(typed, "images/game/score/sick.png") && countNote(typed, "FML-EXPORT-019") == 1,
           "tipo de Codename: sin receptores, la salpicadura por script y sin HUD");
    expect(verifyExport(typed, base) && typed.verifiedStyles.size() == 3, "Note Lab relee el tipo, su aspecto y su salpicadura");

    // Psych con paleta RGB: las notas pintadas en rojo puro.
    const fs::path ps = base / "export-ps/mods/Rgb";
    writeRealSheet(ps / "images/noteSkins/NOTE_assets", baseGameFrames(false, true), 16, [](size_t) { return 0xFFFF0000u; });
    Scan rgbScan;
    scan(rgbScan, {base / "export-ps"});
    const NoteStyle* rgb = styleById(rgbScan, "psych:mods/Rgb/noteSkins/NOTE_assets");
    expect(rgb && rgb->rgbPalette, "exportar: skin de Psych con paleta RGB");
    if (rgb) {
        const ExportIo rgbIo = ioOf(rgbScan);
        ExportOptions r;
        r.target = Engine::Codename;
        r.role = ExportRole::NoteType;
        r.noteType = "Rojo";
        r.name = "rojo";
        const ExportPackage baked = buildExport(*rgb, r, rgbIo);
        std::uint32_t blue = 0, still = 0;
        AtlasFrame f2;
        expect(exportedFrame(baked, "images/game/notes/Rojo", "purple0000", frame, center) && center == 0xFFC24B99u &&
               exportedFrame(baked, "images/game/notes/Rojo", "blue0000", f2, blue) && blue == 0xFF00FFFFu,
               "a Codename la paleta se pinta: el rojo pasa al color de cada flecha (ClientPrefs.hx:28-37)");
        r.target = Engine::Psych;
        r.role = ExportRole::SongSkin;
        const ExportPackage kept = buildExport(*rgb, r, rgbIo);
        expect(exportedFrame(kept, "images/noteSkins/NOTE_assets-rojo", "purple0000", frame, center) && center == 0xFFFF0000u &&
               exportedFrame(kept, "images/noteSkins/NOTE_assets-rojo", "arrowLEFT0000", f2, still) && still == 0xFFFF0000u &&
               countNote(kept, "FML-EXPORT-005") == 0,
               "a Psych, de cancion, se queda en rojo: la recolorea el motor");
        r.role = ExportRole::NoteType;
        const ExportPackage typedRgb = buildExport(*rgb, r, rgbIo);
        expect(exportedFrame(typedRgb, "images/notetypes/rojo", "purple0000", frame, center) && center == 0xFFC24B99u &&
               textOf(typedRgb, "custom_notetypes/Rojo.txt") == "texture: 'notetypes/rojo'\n",
               "tipo de Psych: textura propia (sin paleta, Note.hx:369) y el .txt de NoteTypesConfig");
        ExportPackage typedCheck = typedRgb;
        expect(verifyExport(typedCheck, base) && typedCheck.verifiedStyles.size() == 2 && typedCheck.verifiedStyles[0] == "psych:notetype/Rojo" &&
               typedCheck.verifiedStyles[1] == "type:Rojo",
               "Note Lab relee el tipo de Psych por su .txt");
    }

    // V-Slice a escala 1 con su tira: a Codename se remuestrea a 0.7.
    const fs::path vr = base / "export-vs";
    writeFile(vr / "assets/data/notestyles/big.json", R"JSON({
  "version": "1.1.0", "name": "Big", "author": "Test",
  "assets": {
    "note": {"assetPath": "shared:bignotes", "scale": 1.0, "data": {
      "left": {"prefix": "noteLeft"}, "down": {"prefix": "noteDown"}, "up": {"prefix": "noteUp"}, "right": {"prefix": "noteRight"}}},
    "holdNote": {"assetPath": "bigholds", "scale": 1.0, "data": {}},
    "noteStrumline": {"assetPath": "shared:bigstrums", "scale": 1.0, "data": {
      "leftStatic": {"prefix": "staticLeft"}, "leftPress": {"prefix": "pressLeft"}, "leftConfirm": {"prefix": "confirmLeft"},
      "downStatic": {"prefix": "staticDown"}, "downPress": {"prefix": "pressDown"}, "downConfirm": {"prefix": "confirmDown"},
      "upStatic": {"prefix": "staticUp"}, "upPress": {"prefix": "pressUp"}, "upConfirm": {"prefix": "confirmUp"},
      "rightStatic": {"prefix": "staticRight"}, "rightPress": {"prefix": "pressRight"}, "rightConfirm": {"prefix": "confirmRight"}}}
  }
})JSON");
    writeRealSheet(vr / "assets/shared/images/bignotes", {"noteLeft0000", "noteDown0000", "noteUp0000", "noteRight0000"}, 16, colorOf);
    std::vector<std::string> strumFrames;
    for (const char* d : {"Left", "Down", "Up", "Right"})
        for (const char* st : {"static", "press", "confirm"}) strumFrames.push_back(std::string(st) + d + "0000");
    writeRealSheet(vr / "assets/shared/images/bigstrums", strumFrames, 16, colorOf);
    writeRealPng(vr / "assets/shared/images/bigholds.png", 80, 20, 0xFF8080FFu);
    Scan bigScan;
    scan(bigScan, {vr});
    const NoteStyle* big = styleById(bigScan, "vslice:assets/big");
    expect(big != nullptr, "exportar: notestyle de V-Slice a escala 1");
    if (big) {
        ExportOptions b;
        b.target = Engine::Codename;
        b.role = ExportRole::ModSkin;
        ExportPackage cnBig = buildExport(*big, b, ioOf(bigScan));
        AtlasFrame end;
        std::uint32_t endColor = 0;
        expect(exportedFrame(cnBig, "images/game/notes/default", "purple0000", frame, center) && frame.w == 23 &&
               exportedFrame(cnBig, "images/game/notes/default", "pruple end hold0000", end, endColor) && end.w == 14 && end.h == 26,
               "a 0.7 fija: x1/0.7 (16 -> 23) y el final de la tira hasta el 90 % (10x18 -> 14x26)");
        expect(countNote(cnBig, "FML-EXPORT-006") >= 1 && countNote(cnBig, "FML-EXPORT-022") == 1,
               "se dice el remuestreo y que el final de V-Slice ya no sobresale");
        expect(verifyExport(cnBig, base) && cnBig.verifiedStyles.size() == 1 && cnBig.verifiedStyles[0] == "codename:game/notes/default",
               "Note Lab relee el skin por defecto de Codename sin errores");

        // Guia, carpeta y ZIP.
        addInstallGuides(psych);
        addInstallGuides(vs);
        const std::string guideEs = installGuide(psych, true);
        const std::string guideEn = installGuide(vs, false);
        expect(guideEs.find("estructura verificada") != std::string::npos && guideEs.find("list.txt") != std::string::npos &&
               guideEs.find("[aviso]") != std::string::npos,
               "guia en espanol: estado verificado, list.txt y los avisos");
        expect(guideEn.find("\"noteStyle\": \"mi-skin\"") != std::string::npos && fileOf(vs, "INSTALL.txt") &&
               fileOf(vs, "LEEME_INSTALAR.txt") && fileOf(vs, "notelab-export.json"),
               "guia en ingles de V-Slice: playData.noteStyle; y las dos guias y la marca van en el paquete");
        const fs::path out = fs::u8path((base / "salida").u8string() + "\xC3\xB1/mi-skin-psych");
        std::string error;
        const bool folderOk = writeExportFolder(psych, out, error, true);
        expect(folderOk && fs::exists(out / "LEEME_INSTALAR.txt") && fs::exists(out / "images/noteSkins/NOTE_assets-mi-skin.png"),
               "carpeta con acentos en la ruta" + (folderOk ? std::string() : ": " + error));
        expect(writeExportFolder(psych, out, error, true), "reemplaza una exportacion anterior de Note Lab");
        writeFile(base / "ajena/mod.txt", "no tocar");
        error.clear();
        expect(!writeExportFolder(psych, base / "ajena", error, true) && fs::exists(base / "ajena/mod.txt") && !error.empty(),
               "no pisa una carpeta que no es suya");
        const fs::path zipPath = out.parent_path() / "mi-skin.zip";
        error.clear();
        const bool zipOk = writeExportZip(vs, zipPath, error, false);
        expect(zipOk, "ZIP con acentos en la ruta" + (zipOk ? std::string() : ": " + error));
        std::ifstream zipFile(zipPath, std::ios::binary);
        const std::string zipBytes((std::istreambuf_iterator<char>(zipFile)), std::istreambuf_iterator<char>());
        mz_zip_archive zip{};
        size_t entries = 0;
        char first[256] = {0};
        if (mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0)) {
            entries = mz_zip_reader_get_num_files(&zip);
            mz_zip_reader_get_filename(&zip, 0, first, sizeof(first));
            mz_zip_reader_end(&zip);
        }
        expect(entries == vs.files.size() && std::string(first).rfind("mi-skin-vslice/", 0) == 0,
               "el ZIP trae todo dentro de su carpeta (" + std::to_string(entries) + " archivos)");
        ExportPackage unchecked = psych;
        unchecked.verified = false;
        unchecked.verifyErrors = -1;
        expect(!writeExportFolder(unchecked, out, error, true) && fs::exists(out / "INSTALL.txt"),
               "un paquete no verificado no reemplaza una carpeta ya exportada");
        expect(!writeExportZip(unchecked, zipPath, error, true) && fs::file_size(zipPath) == zipBytes.size(),
               "un paquete no verificado no reemplaza un ZIP ya exportado");
        unchecked.verifyErrors = 1;
        unchecked.verified = true;
        expect(!writeExportFolder(unchecked, base / "export-invalido", error, false) && !fs::exists(base / "export-invalido") &&
               !writeExportZip(unchecked, base / "export-invalido.zip", error, false) && !fs::exists(base / "export-invalido.zip"),
               "errores de relectura bloquean carpeta y ZIP sin escribir archivos");
        fs::path formerPart = out; formerPart += ".notelab-part";
        fs::path formerOld = out; formerOld += ".notelab-old";
        writeFile(formerPart / "no-borrar.txt", "conservar");
        writeFile(formerOld / "no-borrar.txt", "conservar");
        expect(writeExportFolder(psych, out, error, false) && fs::exists(formerPart / "no-borrar.txt") && fs::exists(formerOld / "no-borrar.txt"),
               "los temporales unicos no borran carpetas ajenas con los nombres anteriores");
    }
}

// Un programa de prueba: un bloque con sus valores, colgado donde toque.
int addBlock(BlockProgram& program, const std::string& key, std::vector<std::string> values = {}) {
    const int id = newBlock(program, key);
    for (size_t i = 0; i < values.size() && i < program.node(id)->args.size(); ++i) program.node(id)->args[i].value = values[i];
    return id;
}

int addHat(BlockProgram& program, const std::string& key, const std::string& who = {}) {
    const int id = addBlock(program, key, who.empty() ? std::vector<std::string>{} : std::vector<std::string>{who});
    placeTop(program, id, 20.0f, 20.0f + 200.0f * static_cast<float>(program.tops.size()));
    return id;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool hasConflict(const BlockCode& code, const std::string& key, Severity severity, int node = -1) {
    return std::any_of(code.conflicts.begin(), code.conflicts.end(), [&](const BlockConflict& c) {
        return c.key == key && c.severity == severity && (node < 0 || c.node == node);
    });
}

const BlockPreset* presetByKey(const std::string& key) {
    for (const BlockPreset& preset : blockPresets())
        if (key == preset.key) return &preset;
    return nullptr;
}

BlockProgram presetProgram(const std::string& key) {
    BlockProgram program;
    if (const BlockPreset* preset = presetByKey(key)) addPreset(program, *preset);
    return program;
}

void codeCase() {
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        BlockProgram raw;
        const int hit = addHat(raw, "event.hit", "player");
        const int line = addBlock(raw, "code.line");
        attachAfter(raw, hit, line);
        expect(hasConflict(generateBlocks(engine, "Raw", "raw", raw), "code.line", Severity::Warning, line),
               "linea de codigo vacia: un aviso explica por que no exporta una accion");
        raw.node(line)->args[0].value = engineKey(engine == Engine::Psych ? Engine::Codename : Engine::Psych);
        raw.node(line)->args[1].value = "unsupportedNativeCall()";
        expect(hasConflict(generateBlocks(engine, "Raw", "raw", raw), "code.line", Severity::Warning, line),
               "codigo de otro motor: se avisa de que solo se exporta como comentario");
    }
    std::printf("\n== Codigo, comentarios y recetas editables ==\n");
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        const std::string label = engineKey(engine);
        bool identity = true;
        for (const BlockPreset& preset : blockPresets()) {
            BlockProgram p; addPreset(p, preset);
            for (const BlockFile& file : generateBlocks(engine, "T", "T", p).files) {
                const auto result = applyBlockSource(engine, "T", "T", p, file.path, file.text);
                identity = identity && result.applied && writeProgram(result.program) == writeProgram(p);
                if (!result.applied) std::printf("identity detail %s/%s: %s\n", label.c_str(), preset.key, result.error.c_str());
            }
        }
        expect(identity, label + ": todos los presets reconocen su codigo, incluido el .txt de Psych");
        BlockProgram original;
        const int hat = addHat(original, "event.hit", "player");
        const int action = addBlock(original, "do.health", {"0.1"});
        const int log = addBlock(original, "do.log", {"before"});
        attachAfter(original, hat, action); attachAfter(original, action, log);
        original.comment = "Mi tipo";
        original.node(action)->comment = "Mi bloque";
        const BlockCode initial = generateBlocks(engine, "T", "T", original);
        expect(!initial.files.empty(), label + ": archivo inicial para el editor");
        if (initial.files.empty()) continue;
        const BlockFile file = initial.files.back();
        BlockProgram expected = original;
        expected.node(action)->args[0].value = "0.75";
        expected.node(log)->args[0].value = "after";
        expected.node(hat)->args[0].value = "any";
        const BlockFile edited = generateBlocks(engine, "T", "T", expected).files.back();
        auto result = applyBlockSource(engine, "T", "T", original, file.path, edited.text);
        expect(result.applied && writeProgram(result.program) == writeProgram(expected),
               label + ": valores, texto y lado editados en codigo vuelven a los mismos bloques e ids");
        if (!result.applied) std::printf("sync detail %s: %s\n", label.c_str(), result.error.c_str());
        BlockProgram changedAction = original;
        changedAction.node(action)->key = "do.setHealth";
        const std::string alternate = generateBlocks(engine, "T", "T", changedAction).files.back().text;
        result = applyBlockSource(engine, "T", "T", original, file.path, alternate);
        expect(result.applied && result.program.node(action)->key == "do.setHealth",
               label + ": cambiar una accion reconocida cambia el bloque");
        BlockProgram added = original;
        const int score = addBlock(added, "do.score", {"534"});
        attachAfter(added, log, score);
        const auto withAction = generateBlocks(engine, "T", "T", added).files.back();
        result = applyBlockSource(engine, "T", "T", original, file.path, withAction.text);
        expect(result.applied && result.program.nodes.size() == added.nodes.size() &&
               generateBlocks(engine, "T", "T", result.program).files.back().text == withAction.text,
               label + ": una accion nueva reconocida en codigo aparece como bloque conectado");
        BlockProgram removed = original;
        removeBlock(removed, log);
        const auto withoutAction = generateBlocks(engine, "T", "T", removed).files.back();
        result = applyBlockSource(engine, "T", "T", original, file.path, withoutAction.text);
        expect(result.applied && !result.program.node(log), label + ": borrar una accion en codigo borra su bloque sin perder el resto");
        BlockProgram nested = presetProgram("roulette");
        BlockProgram nestedEdit = nested;
        for (auto& node : nestedEdit.nodes) if (node.second.key == "op.chance") { node.second.args[0].value = "37"; break; }
        const auto nestedFile = generateBlocks(engine, "T", "T", nestedEdit).files.back();
        result = applyBlockSource(engine, "T", "T", nested, nestedFile.path, nestedFile.text);
        expect(result.applied && writeProgram(result.program) == writeProgram(nestedEdit),
               label + ": un valor de una condicion anidada vuelve al bloque sin aplanar la estructura");
        if (!result.applied) std::printf("nested detail %s: %s\n", label.c_str(), result.error.c_str());
        else if (writeProgram(result.program) != writeProgram(nestedEdit)) {
            std::printf("nested metadata %s: %s\n", label.c_str(), result.program.comment.c_str());
            for (const auto& node : result.program.nodes)
                for (size_t i = 0; i < node.second.args.size(); ++i)
                    if (node.second.args[i].value != nestedEdit.nodes[node.first].args[i].value)
                        std::printf("node mismatch: %d %s wanted %s got %s\n", node.first, node.second.key.c_str(), nestedEdit.nodes[node.first].args[i].value.c_str(), node.second.args[i].value.c_str());
        }
        BlockProgram multiline = original;
        multiline.node(log)->args[0].value = "first\nsecond";
        const auto multilineFile = generateBlocks(engine, "T", "T", multiline).files.back();
        result = applyBlockSource(engine, "T", "T", original, file.path, multilineFile.text);
        expect(result.applied && result.program.node(log)->args[0].value == "first\nsecond" &&
               multilineFile.text.find("\nsecond") == std::string::npos,
               label + ": texto multilinea no rompe los comentarios automaticos ni el codigo");
        if (!result.applied) std::printf("multiline detail %s: %s\n", label.c_str(), result.error.c_str());
        std::string comments = file.text;
        const size_t own = comments.find("@user: Mi tipo");
        if (own != std::string::npos) comments.replace(own, std::string("@user: Mi tipo").size(), "@user: Mi tipo editado");
        comments += engine == Engine::Psych ? "\n-- comentario libre\n" : "\n// comentario libre\n";
        result = applyBlockSource(engine, "T", "T", original, file.path, comments);
        expect(result.applied && result.program.comment == "Mi tipo editado\ncomentario libre" &&
               result.program.node(action)->comment == "Mi bloque", label + ": comentarios de tipo, bloque y codigo libre se conservan");
        if (result.applied) {
            const auto saved = readProgram(writeProgram(result.program));
            const std::string generated = generateBlocks(engine, "T", "T", saved).files.back().text;
            expect(contains(generated, "@user: Mi tipo editado") && contains(generated, "@user#" + std::to_string(action) + ": Mi bloque"),
                   label + ": los comentarios editados reaparecen al guardar y regenerar");
        }
        expect(file.text != edited.text && contains(edited.text, "0.75") && contains(edited.text, "after"),
               label + ": la descripcion y el codigo automaticos siguen los valores actuales");
        const auto bad = applyBlockSource(engine, "T", "T", original, file.path,
            engine == Engine::Psych ? "function bad(\n" : "function bad() {\n");
        expect(!bad.applied && !bad.syntaxValid && writeProgram(bad.program) == writeProgram(original),
               label + ": sintaxis incompleta no modifica ni pierde bloques");
        const std::string unknown = file.text + (engine == Engine::Psych ? "\nunrecognizedCall()\n" : "\nunrecognizedCall();\n");
        BlockProgram draft = original;
        const std::string key = draftKey(engine, file.path);
        draft.drafts[key] = {engine, file.path, unknown, file.text, "pending"};
        result = applyBlockSource(engine, "T", "T", draft, file.path, unknown);
        expect(!result.applied && result.syntaxValid && writeProgram(result.program) == writeProgram(draft) &&
               hasConflict(generateBlocks(engine, "T", "T", draft), "", Severity::Error),
               label + ": codigo no representable queda como borrador y bloquea su export");
        expect(writeProgram(readProgram(writeProgram(draft))) == writeProgram(draft), label + ": borrador, base y error sobreviven al guardado");
        const auto custom = keepCustomSource(engine, draft, file.path, unknown);
        const auto customCode = generateBlocks(engine, "T", "T", custom.program);
        bool preserved = false;
        for (const auto& f : customCode.files) if (f.path == file.path && f.text == unknown) preserved = true;
        expect(custom.applied && custom.program.drafts.empty() && preserved && hasConflict(customCode, "code.file", Severity::Warning),
               label + ": archivo propio explicito conserva el codigo y declara que no se simula");
        const auto twice = keepCustomSource(engine, custom.program, file.path, unknown + "\n");
        expect(twice.applied && twice.program.nodes.size() == custom.program.nodes.size(), label + ": reeditar archivo propio no duplica el bloque");
        const auto customBack = applyBlockSource(engine, "T", "T", custom.program, file.path, unknown + "\n");
        expect(customBack.applied && writeProgram(readProgram(writeProgram(customBack.program))) == writeProgram(customBack.program),
               label + ": el archivo propio puede reeditarse y guardarse");
        expect(!keepCustomSource(engine, original, file.path + "/evil.hx", unknown).applied &&
               !keepCustomSource(engine, original, "../outside.hx", unknown).applied,
               label + ": archivo propio no puede salir del directorio de tipos ni declarar carpetas");
        draft.node(action)->args[0].value = "0.2";
        result = applyBlockSource(engine, "T", "T", draft, file.path, unknown);
        expect(!result.applied && contains(result.error, "Blocks changed"), label + ": un borrador viejo no pisa cambios nuevos en los bloques");
    }
    BlockProgram onlyComment;
    onlyComment.comment = "Notas del autor";
    expect(!onlyComment.empty() && readProgram(writeProgram(onlyComment)).comment == onlyComment.comment,
           "un tipo que solo tiene comentarios tambien se guarda");
    NoteProject project;
    ProjectSource source;
    source.path = "fixture";
    CreationRecipe arrows;
    arrows.styleId = "custom"; arrows.baseStyle = "default"; arrows.kind = "arrows";
    arrows.arrows.colors[0] = 0xFF123456u; arrows.arrows.strength = 0.6f;
    arrows.arrows.strums = StrumLook::Clear;
    arrows.arrows.drawn[static_cast<size_t>(DrawnPiece::Note)] = "notelab-import/3/mis-notas-dibujo.png";
    arrows.arrows.drawn[static_cast<size_t>(DrawnPiece::Splash)] = "notelab-import/3/mis-notas-salpicadura.png";
    arrows.arrows.drawnFps[static_cast<size_t>(DrawnPiece::Splash)] = 30;
    arrows.sprite = "C:/notelab/created/mis-notas.nlsprite";
    arrows.arrows.drawnTint = false;
    arrows.arrows.drawnRotate = false;
    CreationRecipe ranking;
    ranking.styleId = "custom"; ranking.kind = "rating"; ranking.font = "my-font.ttf";
    ranking.rating.texts = {"S", "G", "B", "M", "C"}; ranking.rating.tiltDeg = -9.0f;
    CreationRecipe look;
    look.styleId = "custom-type"; look.baseStyle = "default"; look.kind = "look";
    look.look.markImage = "custom/mark.png"; look.look.markSize = 0.7f;
    source.recipes = {arrows, ranking, look};
    project.sources.push_back(source);
    std::string error;
    const auto back = readProject(writeProject(project), error);
    expect(back && back->version == 2 && back->sources[0].recipes.size() == 3 && writeProject(*back) == writeProject(project),
           "proyecto v2: las recetas de notas, ranking y aspecto conservan parametros, origen y fuente");
    expect(back && back->sources[0].recipes[0].arrows.drawn == arrows.arrows.drawn && !back->sources[0].recipes[0].arrows.drawnTint &&
               back->sources[0].recipes[0].arrows.drawnStrums && !back->sources[0].recipes[0].arrows.drawnRotate &&
               back->sources[0].recipes[0].arrows.drawnFps[static_cast<size_t>(DrawnPiece::Splash)] == 30 &&
               readProject(writeProject(project), error)->sources[0].recipes[1].arrows.drawn[0].empty(),
           "proyecto: la forma dibujada de un HUD (su PNG y como se tine, gira y hace receptores) se guarda con la receta");
    auto legacy = nlohmann::json::parse(writeProject(project));
    legacy["version"] = 1; legacy["sources"][0].erase("recipes");
    expect(readProject(legacy.dump(), error).has_value(), "los proyectos v1 siguen abriendo sin recetas");
}

// Bloques (§17, §24, §25.6, §29): el catalogo con su cita en cada motor, los
// presets, editar el programa, el codigo de cada motor (comentarios en
// ingles), los avisos y los paquetes de un tipo de nota.
void blocksCase(const fs::path& base) {
    std::printf("\n== Bloques ==\n");
    int functions = 0, events = 0;
    std::set<std::string> keys;
    bool cited = true, explained = true;
    for (const BlockDef& def : blockDefs()) {
        keys.insert(def.key);
        if (def.category == BlockCategory::Events && def.shape == BlockShape::Hat) ++events;
        if (def.category != BlockCategory::Events && def.category != BlockCategory::Control && def.category != BlockCategory::Operators) ++functions;
        for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
            const BlockSupport support = blockSupport(engine, def.key);
            if (!*support.cite) cited = false;
            if ((support.how == Realization::None || support.how == Realization::Approx) && (!*support.noteEn || !*support.noteEs)) explained = false;
        }
    }
    expect(keys.size() == blockDefs().size() && events == 3, "catalogo: claves unicas y tres eventos (al crear, tocar, fallar)");
    expect(functions >= 30, "catalogo: al menos 30 bloques con funcion (" + std::to_string(functions) + ")");
    expect(cited && explained, "cada bloque cita su realizacion en los tres motores, y lo aproximado o imposible lo explica");
    int advanced = 0;
    bool clean = true;
    for (const BlockPreset& preset : blockPresets()) {
        if (preset.advanced) ++advanced;
        BlockProgram program;
        addPreset(program, preset);
        for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
            const BlockCode code = generateBlocks(engine, "T", "T", program);
            for (const BlockConflict& c : code.conflicts)
                if (c.severity != Severity::Info && c.key != "miss.noAnim") clean = false;
            if (code.files.empty()) clean = false;
        }
    }
    expect(blockPresets().size() >= 20 && advanced >= 8, "presets: basicos y avanzados (" + std::to_string(blockPresets().size()) + ")");
    expect(clean, "cada preset genera codigo en los tres motores sin avisos");

    // Editar el programa: encajar, sacar, borrar, duplicar y reparar.
    {
        BlockProgram p;
        const int hat = addHat(p, "event.hit", "player");
        const int flash = addBlock(p, "do.flash");
        const int shake = addBlock(p, "do.shake");
        attachAfter(p, hat, flash);
        attachAfter(p, flash, shake);
        const int check = addBlock(p, "ctl.if");
        attachAfter(p, hat, check);
        const int chance = addBlock(p, "op.chance", {"25"});
        attachValue(p, check, 0, chance, 0.0f, 0.0f);
        expect(chainOf(p, hat) == std::vector<int>({hat, check, flash, shake}) && linkOf(p, chance).parent == check && rootOf(p, chance) == hat,
               "programa: la cadena y los valores enchufados");
        expect(placeOf(p, flash) == kPlaceHit && placeOf(p, chance) == kPlaceHit && placeOf(p, hat) == kPlaceTop,
               "programa: cada bloque sabe bajo que evento esta");
        attachBody(p, check, 1, flash);
        expect(chainOf(p, hat) == std::vector<int>({hat, check}) && chainOf(p, p.node(check)->args[1].block) == std::vector<int>({flash, shake}),
               "programa: meter un bloque en un «si» se lleva los de debajo (como Scratch)");
        removeBlock(p, flash);
        expect(chainOf(p, p.node(check)->args[1].block) == std::vector<int>({shake}) && !p.node(flash), "programa: borrar un bloque sube los de debajo");
        const int copy = duplicateStack(p, check);
        placeTop(p, copy, 300.0f, 20.0f);
        expect(p.node(copy) && p.node(copy)->args[0].block >= 0 && p.node(copy)->args[0].block != chance && p.tops.size() == 2,
               "programa: duplicar copia lo que va dentro con ids nuevos");
        const int create = addHat(p, "event.create");
        expect(slotPlace(p, create, -1) == kPlaceCreate && slotPlace(p, check, 1) == kPlaceHit && fits(*blockDef("play.avoid"), kPlaceCreate) &&
               !fits(*blockDef("play.avoid"), kPlaceHit) && !fits(*blockDef("do.flash"), kPlaceCreate) && !fits(*blockDef("is.rating"), kPlaceMiss),
               "programa: las propiedades solo van bajo «al crear» y el juicio solo al tocarla");
        const BlockProgram back = readProgram(writeProgram(p));
        expect(writeProgram(back) == writeProgram(p), "programa: JSON de ida y vuelta sin perder nada");
        BlockProgram broken = readProgram(writeProgram(p));
        broken.node(shake)->next = hat;          // un ciclo
        broken.node(check)->args[0].block = 999; // un enlace roto
        broken.tops.push_back(12345);
        BlockNode stray;
        stray.key = "no.existe";
        broken.nodes[77] = stray;
        repairProgram(broken);
        bool reachable = true;
        for (const auto& entry : broken.nodes) {
            int current = entry.first;
            for (size_t guard = 0; guard <= broken.nodes.size() && linkOf(broken, current).parent >= 0; ++guard) current = linkOf(broken, current).parent;
            if (std::find(broken.tops.begin(), broken.tops.end(), current) == broken.tops.end()) reachable = false;
        }
        expect(!broken.node(77) && broken.node(check)->args[0].block == -1 && reachable &&
                   std::find(broken.tops.begin(), broken.tops.end(), 12345) == broken.tops.end(),
               "programa: repairProgram corta ciclos y enlaces rotos y quita lo que no conoce");
    }

    // Daño: las propiedades de siempre, en cada motor.
    const BlockProgram hurt = presetProgram("hurt");
    expect(blocksAvoid(hurt) && blocksHitMisses(hurt), "Daño: hay que evitarla y tocarla es un fallo");
    const BlockCode cn = generateBlocks(Engine::Codename, "Bala", "Bala", hurt);
    const std::string cnText = cn.files.empty() ? std::string() : cn.files[0].text;
    expect(cn.files.size() == 1 && cn.files[0].path == "data/notes/Bala.hx" && contains(cnText, "event.note.avoid = true") &&
               contains(cnText, "event.misses = true;") && contains(cnText, "event.healthGain = -0.2;") &&
               contains(cnText, "function onPlayerMiss(event)") && contains(cnText, "event.cancel();"),
           "Codename: avoid al crearla, el acierto contado como fallo y el fallo cancelado");
    const BlockCode ps = generateBlocks(Engine::Psych, "Bala", "Bala", hurt);
    expect(ps.files.size() == 1 && ps.files[0].path == "custom_notetypes/Bala.txt" &&
               ps.files[0].text == "ignoreNote: true\nhitCausesMiss: true\nmissHealth: 0.2\n",
           "Psych: .txt de NoteTypesConfig (con hitCausesMiss la vida al tocarla va en missHealth)");
    const BlockCode vs = generateBlocks(Engine::VSlice, "Bala", "bala", hurt);
    const std::string vsText = vs.files.empty() ? std::string() : vs.files[0].text;
    expect(vs.files.size() == 1 && vs.files[0].path == "scripts/notekinds/bala.hxc" &&
               contains(vsText, "super(\"Bala\", \"Bala (Note Lab)\", null, null, true, null);") && contains(vsText, "event.judgement == 'perfect'") &&
               contains(vsText, "event.note.handledMiss = true;") && contains(vsText, "Scoring.getMissScore()"),
           "V-Slice: NoteKind que cancela el acierto de la maquina y el fallo, y cuenta el suyo como fallo");
    // Los comentarios del codigo van en ingles.
    const std::string all = cnText + vsText;
    expect(contains(all, "note type \"Bala\"") && contains(all, "// Hitting it is a miss") && !contains(all, "Tocarla") && !contains(all, "la CPU") &&
               !contains(all, "Dejarla") && !contains(all, "tipo \""),
           "el codigo generado se comenta en ingles");

    // Acciones, condiciones y valores en los tres motores.
    BlockProgram p;
    const int hit = addHat(p, "event.hit", "player");
    const int choice = addBlock(p, "ctl.ifElse");
    attachAfter(p, hit, choice);
    attachValue(p, choice, 0, addBlock(p, "op.chance", {"50"}), 0.0f, 0.0f);
    const int heal = addBlock(p, "do.health", {"0.3"});
    attachBody(p, choice, 1, heal);
    attachAfter(p, heal, addBlock(p, "do.flash", {"ff0000", "0.3"}));
    const int shake = addBlock(p, "do.shake", {"0.02", "0.2"});
    attachBody(p, choice, 2, shake);
    attachAfter(p, shake, addBlock(p, "do.sound", {"grito", "1"}));
    attachAfter(p, choice, addBlock(p, "do.anim", {"bf", "hey"}));
    const int miss = addHat(p, "event.miss");
    const int big = addBlock(p, "ctl.if");
    attachAfter(p, miss, big);
    const int compare = addBlock(p, "op.compare", {"0", "gt", "10"});
    attachValue(p, big, 0, compare, 0.0f, 0.0f);
    attachValue(p, compare, 0, addBlock(p, "get.combo"), 0.0f, 0.0f);
    attachBody(p, big, 1, addBlock(p, "do.score", {"-500"}));
    const BlockCode actionsCn = generateBlocks(Engine::Codename, "Suerte", "Suerte", p);
    const std::string a = actionsCn.files.empty() ? std::string() : actionsCn.files[0].text;
    expect(contains(a, "function onPostNoteHit(event)") && contains(a, "if (event.player) {") && contains(a, "if (FlxG.random.bool(50.0)) {") &&
               contains(a, "health += 0.3;") && contains(a, "camGame.flash(0xFFFF0000, 0.3, null, true);") &&
               contains(a, "camGame.shake(0.02, 0.2, null, true);") && contains(a, "FlxG.sound.play(Paths.sound(\"grito\"), 1.0);") &&
               contains(a, "if (boyfriend != null) boyfriend.playAnim(\"hey\", true);") && contains(a, "function onPostPlayerMiss(event)") &&
               contains(a, "if (combo > 10.0) {") && contains(a, "songScore += -500;") && contains(a, "event.note.isSustainNote"),
           "Codename: las acciones van en onPostNoteHit / onPostPlayerMiss, despues del motor (PlayState.hx:2055, :1942)");
    const BlockCode actionsPs = generateBlocks(Engine::Psych, "Suerte", "Suerte", p);
    const std::string b = actionsPs.files.empty() ? std::string() : actionsPs.files.back().text;
    expect(actionsPs.files.size() == 1 && actionsPs.files[0].path == "custom_notetypes/Suerte.lua" &&
               contains(b, "function goodNoteHit(id, direction, noteType, isSustainNote)") && contains(b, "if getRandomBool(50.0) then") &&
               contains(b, "addHealth(0.3)") && contains(b, "cameraFlash('camGame', 'FF0000', 0.3, true)") &&
               contains(b, "cameraShake('camGame', 0.02, 0.2)") && contains(b, "playSound('grito', 1.0)") &&
               contains(b, "playAnim('boyfriend', 'hey', true)") && contains(b, "function noteMiss(id, direction, noteType, isSustainNote)") &&
               contains(b, "if (getProperty('combo') > 10.0) then") && contains(b, "addScore(-500)") && !contains(b, "opponentNoteHit"),
           "Psych: las acciones en Lua (FunkinLua.hx:697, :841, :837, :1317, :988)");
    const BlockCode actionsVs = generateBlocks(Engine::VSlice, "Suerte", "suerte", p);
    const std::string c = actionsVs.files.empty() ? std::string() : actionsVs.files[0].text;
    expect(contains(c, "import flixel.FlxG;") && contains(c, "import funkin.play.PlayState;") && contains(c, "import funkin.Highscore;") &&
               contains(c, "import funkin.audio.FunkinSound;") && contains(c, "if (event.note.noteData.getMustHitNote())") &&
               contains(c, "PlayState.instance.health += 0.3;") && contains(c, "PlayState.instance.camGame.flash(0xFFFF0000, 0.3, null, true);") &&
               contains(c, "FunkinSound.playOnce(Paths.sound(\"grito\"), 1.0);") && contains(c, "if (Highscore.tallies.combo > 10.0) {") &&
               contains(c, "PlayState.instance.songScore += -500;") && contains(c, "public override function onNoteMiss"),
           "V-Slice: las acciones en el NoteKind con sus imports");
    expect(hasConflict(actionsVs, "get.combo", Severity::Info), "V-Slice: leer el combo avisa de que el acierto aun no cuenta (aprox.)");

    // Veneno: temporizadores en los tres.
    const BlockProgram poison = presetProgram("poison");
    const std::string poisonCn = generateBlocks(Engine::Codename, "Veneno", "Veneno", poison).files[0].text;
    const BlockCode poisonPs = generateBlocks(Engine::Psych, "Veneno", "Veneno", poison);
    const std::string poisonVs = generateBlocks(Engine::VSlice, "Veneno", "veneno", poison).files[0].text;
    expect(contains(poisonCn, "var notelabTimer") && contains(poisonCn, "new FlxTimer().start(0.5, function(_) { health += -0.05; }, 8);") &&
               poisonPs.files.size() == 2 && contains(poisonPs.files[1].text, "runTimer('notelab_Veneno_") &&
               contains(poisonPs.files[1].text, "function onTimerCompleted(tag, loops, loopsLeft)") &&
               contains(poisonPs.files[1].text, "addHealth(-0.05)") && contains(poisonVs, "import flixel.util.FlxTimer;") &&
               contains(poisonVs, ":FlxTimer = null;"),
           "Veneno: un temporizador por bloque que se reinicia al tocar otra (FunkinLua.hx:655)");

    // Avisos: cada bloque en su sitio.
    BlockProgram wrong;
    const int wrongHit = addHat(wrong, "event.hit", "player");
    const int misplaced = addBlock(wrong, "play.avoid");
    attachAfter(wrong, wrongHit, misplaced);
    const int wrongCreate = addHat(wrong, "event.create");
    const int action = addBlock(wrong, "do.flash");
    attachAfter(wrong, wrongCreate, action);
    const int wrongMiss = addHat(wrong, "event.miss");
    const int ratingIf = addBlock(wrong, "ctl.if");
    attachAfter(wrong, wrongMiss, ratingIf);
    const int rating = addBlock(wrong, "is.rating");
    attachValue(wrong, ratingIf, 0, rating, 0.0f, 0.0f);
    const int loose = addBlock(wrong, "do.shake");
    placeTop(wrong, loose, 400.0f, 400.0f);
    const int silent = addBlock(wrong, "do.sound");
    attachBody(wrong, ratingIf, 1, silent);
    const BlockCode warned = generateBlocks(Engine::Codename, "X", "X", wrong);
    expect(hasConflict(warned, "play.avoid", Severity::Warning, misplaced) && hasConflict(warned, "do.flash", Severity::Warning, action) &&
               hasConflict(warned, "is.rating", Severity::Warning, rating) && hasConflict(warned, "do.shake", Severity::Info, loose) &&
               hasConflict(warned, "do.sound", Severity::Warning, silent),
           "avisos: propiedad fuera de «al crear», accion en «al crear», juicio al fallar, bloques sueltos y sonido sin elegir");
    BlockProgram avoidMiss = presetProgram("mine");
    addPreset(avoidMiss, *presetByKey("missPenalty"));
    const BlockCode avoidCode = generateBlocks(Engine::Psych, "X", "X", avoidMiss);
    expect(hasConflict(avoidCode, "event.miss", Severity::Warning) &&
               std::none_of(avoidCode.files.begin(), avoidCode.files.end(), [](const BlockFile& f) { return contains(f.text, "noteMiss"); }),
           "choque: con «hay que evitarla» lo de «cuando falla» no se ejecuta ni se exporta");
    BlockProgram noMissAnim;
    attachAfter(noMissAnim, addHat(noMissAnim, "event.create"), addBlock(noMissAnim, "miss.noAnim"));
    expect(hasConflict(generateBlocks(Engine::VSlice, "X", "x", noMissAnim), "miss.noAnim", Severity::Warning) &&
               contains(generateBlocks(Engine::Psych, "X", "X", noMissAnim).files[0].text, "noMissAnimation: true"),
           "choque de V-Slice: sin animación de fallo no tiene equivalente (BaseCharacter.hx:605-626)");
    BlockProgram psychClash;
    const int clashHat = addHat(psychClash, "event.create");
    attachAfter(psychClash, clashHat, addBlock(psychClash, "hit.causesMiss"));
    attachAfter(psychClash, lastOf(psychClash, clashHat), addBlock(psychClash, "hit.health", {"-0.2"}));
    attachAfter(psychClash, lastOf(psychClash, clashHat), addBlock(psychClash, "miss.health", {"-0.1"}));
    expect(hasConflict(generateBlocks(Engine::Psych, "X", "X", psychClash), "miss.health", Severity::Warning),
           "choque de Psych: missHealth vale para las dos cosas cuando tocarla es un fallo");
    const BlockProgram hey = presetProgram("hey");
    const BlockCode heyPs = generateBlocks(Engine::Psych, "Grito", "Grito", hey);
    expect(heyPs.files.size() == 1 && contains(heyPs.files[0].text, "function goodNoteHit") && contains(heyPs.files[0].text, "function opponentNoteHit") &&
               contains(heyPs.files[0].text, "playAnim('dad', 'hey', true)"),
           "Psych: «Hey!» de cualquiera va en goodNoteHit y opponentNoteHit (PlayState.hx:3069)");

    // Un tipo solo con bloques (sin aspecto), exportado a los tres motores.
    const fs::path mod = base / "blocks-cn/mods/Blocks";
    writeRealSheet(mod / "images/game/notes/default", baseGameFrames(true, true), 16,
                   [](size_t i) { return 0xFF000000u | static_cast<std::uint32_t>(i * 0x10101u + 0x404040u); });
    Scan s;
    scan(s, {base / "blocks-cn"});
    const NoteStyle* skin = styleById(s, "codename:mods/Blocks/game/notes/default");
    expect(skin != nullptr, "bloques: el skin de prueba se lee");
    if (!skin) return;
    ExportOptions o;
    o.role = ExportRole::NoteType;
    o.noteType = "Mina";
    o.notes = false;
    o.splashes = false;
    o.blocks = presetProgram("mine");
    attachAfter(o.blocks, addHat(o.blocks, "event.hit", "player"), addBlock(o.blocks, "do.shake"));
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        o.target = engine;
        o.existingFiles = engine == Engine::Psych ? std::vector<std::string>{"mods/Otro/custom_notetypes/Mina.txt"} : std::vector<std::string>{};
        ExportPackage pkg = buildExport(*skin, o, ioOf(s));
        const bool styleFile = std::any_of(pkg.files.begin(), pkg.files.end(), [](const ExportFile& f) {
            return f.path.find("images/") == 0 || f.path.find("data/notestyles/") == 0;
        });
        const bool verified = verifyExport(pkg, base);
        const std::string label = std::string(engineKey(engine));
        expect(!styleFile && !pkg.files.empty() && verified && pkg.verifiedStyles.size() == 1 && pkg.verifiedStyles[0] == "type:Mina",
               "solo bloques a " + label + ": sin imagenes ni notestyle y el tipo se relee con lo que dicen sus bloques");
        if (engine == Engine::Psych)
            expect(std::any_of(pkg.notes.begin(), pkg.notes.end(), [](const ExportNote& n) { return n.code == "FML-EXPORT-035"; }) &&
                       fileOf(pkg, "custom_notetypes/Mina.lua"),
                   "Psych: el .txt y el .lua del tipo, y se avisa si el mod ya tiene ese archivo");
    }

    // Aspecto y bloques en el mismo script de Codename.
    o.target = Engine::Codename;
    o.notes = true;
    o.existingFiles.clear();
    const ExportPackage both = buildExport(*skin, o, ioOf(s));
    const std::string script = textOf(both, "data/notes/Mina.hx");
    expect(fileOf(both, "images/game/notes/Mina.png") && contains(script, "event.note.avoid = true") && contains(script, "event.healthGain = -0.3;") &&
               contains(script, "camGame.shake("),
           "Codename: el aspecto en game/notes/<tipo> y los bloques en data/notes/<tipo>.hx");
}

void expandedBlocksCase(const fs::path& base) {
    std::printf("\n== Bundle de 20 bloques ==\n");
    const std::vector<std::string> keys = {"is.sustain", "get.noteTime", "get.sustainLength", "get.noteLane", "get.strumlineIndex", "get.hitOffset", "get.bpm", "get.beat", "var.define", "var.get", "var.set", "var.change", "op.clamp", "op.remap", "look.alpha", "look.scale", "look.angle", "ctl.repeat", "ctl.after", "ctl.cancel"};
    expect(blockDefs().size() == 70 && kBlockCategories == 10, "70 definiciones: 68 bloques de paleta, archivo propio y codigo de un script, en 10 categorias");
    auto valid = [](const BlockCode& code) {
        return !code.files.empty() && std::none_of(code.conflicts.begin(), code.conflicts.end(), [](const BlockConflict& c) { return c.severity == Severity::Error; });
    };
    auto single = [&](const std::string& key) {
        BlockProgram p;
        const int declaration = addHat(p, "var.define");
        p.node(declaration)->args[0].value = "contador";
        if (key == "var.define") return p;
        const BlockDef& def = *blockDef(key);
        const int hat = addHat(p, def.category == BlockCategory::Properties ? "event.create" : "event.hit", def.category == BlockCategory::Properties ? "" : "any");
        const int id = addBlock(p, key);
        if (def.shape == BlockShape::Number) {
            const int action = addBlock(p, "var.set"); attachAfter(p, hat, action); attachValue(p, action, 1, id, 0, 0);
        } else if (def.shape == BlockShape::Boolean) {
            const int condition = addBlock(p, "ctl.if"); attachAfter(p, hat, condition); attachValue(p, condition, 0, id, 0, 0);
            attachBody(p, condition, 1, addBlock(p, "do.log", {"sostenido"}));
        } else {
            attachAfter(p, hat, id);
            if (def.shape == BlockShape::If) attachBody(p, id, 1, addBlock(p, "do.log", {"accion"}));
        }
        return p;
    };
    for (const auto& key : keys) {
        const BlockProgram p = single(key);
        expect(writeProgram(readProgram(writeProgram(p))) == writeProgram(p), key + ": guardar/reabrir sin cambiar ids ni parametros");
        for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
            const BlockCode code = generateBlocks(engine, "Bundle", "Bundle", p);
            for (const auto& file : code.files) writeFile(base / "block-samples" / key / engineKey(engine) / fs::u8path(file.path), file.text);
            expect(valid(code), key + ": genera codigo valido estructuralmente en " + engineKey(engine));
            bool identity = valid(code);
            for (const auto& file : code.files) {
                const auto sync = applyBlockSource(engine, "Bundle", "Bundle", p, file.path, file.text);
                identity = identity && sync.applied && writeProgram(sync.program) == writeProgram(p);
                if (!sync.applied) std::printf("bundle sync %s/%s: %s\n", engineKey(engine), key.c_str(), sync.error.c_str());
            }
            expect(identity, key + ": codigo/bloques conserva el programa en " + engineKey(engine));
        }
    }
    BlockProgram p;
    const int declaration = addHat(p, "var.define");
    p.node(declaration)->args[0].value = "nombre; no es codigo";
    p.node(declaration)->args[1].value = "7";
    const int second = addHat(p, "var.define"); p.node(second)->args[0].value = "otro";
    const int hit = addHat(p, "event.hit", "any");
    const int repeat = addBlock(p, "ctl.repeat", {"64"}); attachAfter(p, hit, repeat);
    const int inner = addBlock(p, "ctl.repeat", {"64"}); attachBody(p, repeat, 1, inner);
    const int change = addBlock(p, "var.change"); attachBody(p, inner, 1, change);
    const int delayed = addBlock(p, "ctl.after", {"0.5", "", "pulse"}); attachAfter(p, repeat, delayed);
    const int set = addBlock(p, "var.set"); attachBody(p, delayed, 1, set);
    const int time = addBlock(p, "get.noteTime"); attachValue(p, set, 1, time, 0, 0);
    const int condition = addBlock(p, "ctl.if"); attachAfter(p, set, condition);
    const int direction = addBlock(p, "is.direction", {"left"}); attachValue(p, condition, 0, direction, 0, 0);
    attachBody(p, condition, 1, addBlock(p, "do.log", {"captured"}));
    const int miss = addHat(p, "event.miss"); attachAfter(p, miss, addBlock(p, "ctl.cancel", {"pulse"}));
    p.comment = "Bundle de prueba"; p.node(delayed)->comment = "Contexto inmutable";
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        const auto code = generateBlocks(engine, "Bundle", "Bundle", p);
        const std::string text = code.files.back().text;
        expect(valid(code) && contains(text, "notelabBudget") && contains(text, "512") && contains(text, "64") && contains(text, "notelabPending") && contains(text, "32"),
               std::string(engineKey(engine)) + ": presupuestos compartidos y cola acotada");
        expect(contains(text, "notelabContext.time") && contains(text, "notelabContext.lane == 0") && contains(text, "notelabTokens") && contains(text, "notelabCancel"),
               std::string(engineKey(engine)) + ": contexto capturado, reemplazo y cancelacion propios");
        expect(contains(text, "now < notelabClock - 1") && contains(text, "now - notelabClock > 1000") && contains(text, "now <= previous") && contains(text, "notelabVar1 = 7.0"),
               std::string(engineKey(engine)) + ": pausa, salto y reinicio sin variable por nota");
        for (const auto& file : code.files) {
            const auto sync = applyBlockSource(engine, "Bundle", "Bundle", p, file.path, file.text);
            expect(sync.applied && writeProgram(sync.program) == writeProgram(p), std::string(engineKey(engine)) + ": condiciones y diferidos anidados conservan comentarios e ids");
            writeFile(base / "block-runtime" / engineKey(engine) / fs::u8path(file.path), file.text);
        }
        BlockProgram edit = p; edit.node(change)->args[1].value = "2";
        auto edited = generateBlocks(engine, "Bundle", "Bundle", edit).files.back();
        auto result = applyBlockSource(engine, "Bundle", "Bundle", p, edited.path, edited.text);
        expect(result.applied && result.program.node(change)->args[1].value == "2", std::string(engineKey(engine)) + ": una cantidad editada vuelve al bloque dentro de una repeticion");
        edit = p; edit.node(set)->args[0].value = std::to_string(second);
        edited = generateBlocks(engine, "Bundle", "Bundle", edit).files.back();
        result = applyBlockSource(engine, "Bundle", "Bundle", p, edited.path, edited.text);
        expect(result.applied && result.program.node(set)->args[0].value == std::to_string(second), std::string(engineKey(engine)) + ": cambiar el simbolo numerico en codigo vuelve a la variable elegida");
        edit = p; removeBlock(edit, declaration);
        expect(hasConflict(generateBlocks(engine, "Bundle", "Bundle", edit), "var.change", Severity::Error), std::string(engineKey(engine)) + ": variable borrada bloquea export en lugar de inventar un valor");
        edit = p; edit.node(declaration)->args[0].value = "renombrado";
        expect(valid(generateBlocks(engine, "Bundle", "Bundle", edit)) && edit.node(set)->args[0].value == std::to_string(declaration), std::string(engineKey(engine)) + ": renombrar no rompe las referencias");
    }
    BlockProgram copy;
    addHat(copy, "var.define");
    const int copied = copyStackFrom(copy, p, hit); placeTop(copy, copied, 0, 0);
    bool remapped = blockVariables(copy).size() == 2;
    for (const auto& entry : copy.nodes) {
        const auto* def = blockDef(entry.second.key);
        for (size_t i = 0; def && i < def->args.size(); ++i) if (def->args[i].kind == ArgKind::Variable)
            remapped = remapped && entry.second.args[i].value != "1" && referencedVariable(copy, entry.second.args[i].value);
    }
    expect(remapped && valid(generateBlocks(Engine::Codename, "Copy", "Copy", copy)), "copiar entre tipos trae la declaracion y remapea ids, sin unirse a otra variable");
    NoteProject project; ProjectSource source; source.path = "fixture"; source.typeBlocks.push_back({"Bundle", p}); project.sources.push_back(source);
    writeFile(base / "bundle.fmlnote", writeProject(project));
    BlockProgram limited = p; limited.node(repeat)->args[0].value = "2"; limited.node(inner)->args[0].value = "2";
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice})
        for (const auto& file : generateBlocks(engine, "Bundle", "Bundle", limited).files)
            writeFile(base / "runtime-limited" / engineKey(engine) / fs::u8path(file.path), file.text);
    std::string error; const auto loaded = readProject(writeProject(project), error);
    expect(loaded && writeProgram(loaded->sources[0].typeBlocks[0].program) == writeProgram(p), "el proyecto v2 conserva todo el bundle y sus comentarios");
    BlockProgram wrong = p;
    const int offset = addBlock(wrong, "get.hitOffset"); const int action = addBlock(wrong, "var.set");
    attachAfter(wrong, miss, action); attachValue(wrong, action, 1, offset, 0, 0);
    expect(hasConflict(generateBlocks(Engine::Codename, "T", "T", wrong), "get.hitOffset", Severity::Error), "no se exporta un desfase de acierto desde un fallo");
    wrong = p; wrong.node(delayed)->args[2].value.clear();
    expect(hasConflict(generateBlocks(Engine::Psych, "T", "T", wrong), "ctl.after", Severity::Error), "un diferido sin clave no se exporta");
    wrong = p; wrong.node(change)->next = repeat;
    expect(hasConflict(generateBlocks(Engine::Codename, "T", "T", wrong), "", Severity::Error, -1) || !generateBlocks(Engine::Codename, "T", "T", wrong).conflicts.empty(), "grafo ciclico o compartido se rechaza antes de generar");
    wrong = p; wrong.node(change)->args[1].block = direction; wrong.node(condition)->args[0].block = -1;
    expect(generateBlocks(Engine::Psych, "T", "T", wrong).files.empty(), "no se enchufa un booleano en una ranura numerica desde un proyecto externo");
    repairProgram(wrong);
    expect(wrong.node(change)->args[1].block == -1, "reparar saca a la vista valores del tipo incorrecto");
    BlockProgram deep; int parent = addHat(deep, "event.hit", "any");
    for (int i = 0; i < 52; ++i) { int child = addBlock(deep, "ctl.repeat"); if (i == 0) attachAfter(deep, parent, child); else attachBody(deep, parent, 1, child); parent = child; }
    expect(generateBlocks(Engine::Codename, "T", "T", deep).files.empty(), "la profundidad maxima evita desbordar el generador");
    BlockProgram look;
    const int creation = addHat(look, "event.create");
    const int alpha = addBlock(look, "look.alpha", {"0.4"}); attachAfter(look, creation, alpha);
    const int scale = addBlock(look, "look.scale", {"2", "0.5"}); attachAfter(look, alpha, scale);
    const int angle = addBlock(look, "look.angle", {"45"}); attachAfter(look, scale, angle);
    const BlockAppearance appearance = blocksAppearance(look);
    expect(std::abs(appearance.alpha - 0.4f) < 0.001f && appearance.scaleX == 2 && appearance.scaleY == 0.5f && appearance.angle == 45, "la preview comparte propiedades de apariencia del nucleo");
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        const auto code = generateBlocks(engine, "Look", "Look", look);
        const auto text = code.files.back().text;
        expect(valid(code) && contains(text, "0.4") && contains(text, "2.0") && contains(text, "0.5") && contains(text, "45.0"), std::string(engineKey(engine)) + ": X e Y distintos y alfa sobre la apariencia nativa");
        expect(engine != Engine::Codename || (contains(text, "postUpdate") && text.find("copyStrumAngle = false") == std::string::npos), "Codename: el giro no altera la trayectoria nativa");
    }
    const fs::path mod = base / "bundle-source";
    writeRealSheet(mod / "images/game/notes/default", baseGameFrames(true, true), 16, [](size_t) { return 0xFF40C090u; });
    Scan scanResult; scan(scanResult, {mod});
    const NoteStyle* skin = styleById(scanResult, "codename:game/notes/default");
    expect(skin != nullptr, "bundle: atlas real para preview y export");
    if (skin) {
        auto atlases = atlasStoreOf(scanResult);
        NotePreview preview; PreviewSettings settings; settings.showOpponent = false;
        PreviewState state; PreviewNote note; note.timeMs = 0; note.sustainMs = 200;
        RenderList plain, decorated;
        preview.build(*skin, atlases, nullptr, settings, state, {note}, plain);
        note.alpha = appearance.alpha; note.scaleX = appearance.scaleX; note.scaleY = appearance.scaleY; note.angle = appearance.angle;
        preview.build(*skin, atlases, nullptr, settings, state, {note}, decorated);
        bool transforms = plain.cmds.size() == decorated.cmds.size() && plain.cmds.size() > 5;
        if (transforms) {
            const auto& head = decorated.cmds.back();
            transforms = std::abs(head.alpha - 0.4f) < 0.001f && std::abs(head.ma - 1.4142135f) < 0.001f && std::abs(head.md - 0.3535534f) < 0.001f;
            for (size_t i = 4; i + 1 < plain.cmds.size(); ++i)
                transforms = transforms && decorated.cmds[i].w == plain.cmds[i].w && decorated.cmds[i].h == plain.cmds[i].h && std::abs(decorated.cmds[i].alpha - plain.cmds[i].alpha * 0.4f) < 0.001f;
            for (size_t i = 0; i < 4; ++i) transforms = transforms && decorated.cmds[i].alpha == plain.cmds[i].alpha;
        }
        expect(transforms, "preview: cabeza girada/escalada; sostenido conserva longitud y receptores no cambian");
        ExportOptions options; options.role = ExportRole::NoteType; options.noteType = "Bundle"; options.name = "bundle"; options.splashes = options.hud = options.holdCovers = false;
        options.blocks = limited;
        const int copiedLook = copyStackFrom(options.blocks, look, creation); placeTop(options.blocks, copiedLook, 0, 0);
        for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
            options.target = engine;
            ExportPackage package = buildExport(*skin, options, ioOf(scanResult));
            const bool verified = !package.hasErrors() && verifyExport(package, base);
            expect(verified, std::string(engineKey(engine)) + ": aspecto mas nuevos bloques en un paquete releido sin errores");
            if (!verified) for (const auto& finding : package.notes) if (finding.severity == Severity::Error) std::printf("bundle export %s: %s\n", engineKey(engine), describeExportNote(finding, false).c_str());
            addInstallGuides(package);
            std::string issue;
            const fs::path output = base / fs::u8path("bundle-José") / engineKey(engine);
            expect(verified && writeExportFolder(package, output, issue, true), std::string(engineKey(engine)) + ": nuevo bundle a carpeta con acentos");
            expect(verified && writeExportZip(package, output.parent_path() / (std::string(engineKey(engine)) + ".zip"), issue, true), std::string(engineKey(engine)) + ": nuevo bundle a ZIP con acentos");
            options.blocks.node(change)->args[0].value.clear();
            const ExportPackage invalid = buildExport(*skin, options, ioOf(scanResult));
            expect(invalid.hasErrors(), std::string(engineKey(engine)) + ": el exporter propaga errores de variable sin resolver");
            options.blocks.node(change)->args[0].value = std::to_string(declaration);
        }
    }
}

// Una rejilla pixel de verdad: cada celda con un cuadrado opaco (menos el borde),
// salvo las de `empty`. Las celdas se numeran como Flixel, fila a fila.
void writeGridPng(const fs::path& path, int w, int h, int columns, int rows, const std::vector<int>& empty) {
    Image image = blankImage(w, h);
    const int cellW = w / columns, cellH = h / rows;
    const int flixelColumns = cellW > 0 ? w / cellW : 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (cellW <= 0 || cellH <= 0 || x % cellW == 0 || y % cellH == 0) continue;
            const int cell = (y / cellH) * flixelColumns + x / cellW;
            if (std::find(empty.begin(), empty.end(), cell) != empty.end()) continue;
            std::uint8_t* p = image.at(x, y);
            p[0] = p[1] = p[2] = p[3] = 0xFF;
        }
    const std::vector<unsigned char> png = encodePng(image);
    writeFile(path, std::string(png.begin(), png.end()));
}

// Mejor busqueda (DESIGN_PLUGIN_NOTE_LAB §30): el juego base de cada motor,
// los motores que no son ninguno de los tres, las imagenes de game/notes que
// no son tipos, la 0.9 de prueba de V-Slice y las rejillas pixel de Psych.
void searchCase(const fs::path& base) {
    std::printf("\n== Busqueda: juego base, otros motores, pixel ==\n");
    {
        // Juego base: una instalacion oficial de Codename, un mod dentro, un mod
        // al lado y la build de un mod que trae el motor con su skin cambiado.
        const fs::path bg = base / "bg";
        const fs::path official = bg / "Codename.Engine-Windows";
        writeFile(official / "CodenameEngine.exe", "MZ");
        writeSheet(official / "assets/images/game/notes/default", baseGameFrames(true, true));
        writeFile(official / "mods/inner/data/notes/Bullet.hx", "function onNoteCreation(e) {}\n");
        writeFile(bg / "fanclub/Fan Club/data/stages/concert.xml", "<stage/>");
        // El codigo fuente del motor, mas reciente: la build gana igual.
        writeFile(bg / "CodenameEngine-main/project.xml", "<project/>");
        writeFile(bg / "CodenameEngine-main/source/Main.hx", "class Main {}");
        writeSheet(bg / "CodenameEngine-main/assets/images/game/notes/default", baseGameFrames(true, true));
        std::error_code touch;
        fs::last_write_time(bg / "CodenameEngine-main/assets/images/game/notes/default.xml",
                            fs::last_write_time(official / "assets/images/game/notes/default.xml", touch) + std::chrono::hours(24), touch);
        expect(isOfficialInstall(bg / "CodenameEngine-main", Engine::Codename), "el codigo fuente tambien es oficial");
        const fs::path lone = base / "bg2";
        writeFile(lone / "gorefield/gorefield.exe", "MZ");
        writeSheet(lone / "gorefield/assets/images/game/notes/default", baseGameFrames(true, true));
        writeFile(lone / "othermod/Other/data/stages/x.xml", "<stage/>");
        expect(isEngineInstall(official, Engine::Codename) && isOfficialInstall(official, Engine::Codename),
               "instalacion oficial de Codename: skin por defecto y CodenameEngine.exe");
        expect(isEngineInstall(lone / "gorefield", Engine::Codename) && !isOfficialInstall(lone / "gorefield", Engine::Codename),
               "la build de un mod trae el motor, pero no es la oficial (otro ejecutable)");
        const BaseSearch inner = findEngineBase(official / "mods/inner", Engine::Codename, {});
        expect(inner.how == BaseFound::Contains && inner.folder == official, "un mod dentro de mods/ usa la instalacion que lo contiene");
        const BaseSearch nearby = findEngineBase(bg / "fanclub/Fan Club", Engine::Codename, {});
        expect(nearby.how == BaseFound::Nearby && nearby.folder == official,
               "un mod suelto encuentra la build oficial de al lado, antes que el codigo fuente mas reciente");
        writeFile(bg / "fanclub.zip", "PK");
        const BaseSearch zipped = findEngineBase(bg / "fanclub.zip", Engine::Codename, {});
        expect(zipped.how == BaseFound::Nearby && zipped.folder == official, "un ZIP busca entre sus vecinos");
        const BaseSearch none = findEngineBase(lone / "othermod/Other", Engine::Codename, {});
        expect(none.how == BaseFound::None, "la build de otro mod no se toma por juego base");
        const BaseSearch remembered = findEngineBase(lone / "othermod/Other", Engine::Codename, {lone / "gorefield", official});
        expect(remembered.how == BaseFound::Remembered && remembered.folder == official,
               "de las recordadas solo vale una oficial");
        writeSheet(base / "bgp/Psych/assets/shared/images/NOTE_assets", baseGameFrames(true, true));
        writeFile(base / "bgv/Funkin/assets/data/notestyles/funkin.json", "{}");
        expect(isEngineInstall(base / "bgp/Psych", Engine::Psych) && isEngineInstall(base / "bgv/Funkin", Engine::VSlice) &&
               !isEngineInstall(base / "bgv/Funkin", Engine::Codename), "instalaciones de Psych (0.6) y V-Slice por su skin");
        // Un mod de Codename sin skin propio, con el juego base debajo, ya tiene
        // el estilo por defecto que dibujar.
        Scan alone;
        scan(alone, {bg / "fanclub/Fan Club"});
        expect(!hasEngineDefault(alone.catalog, Engine::Codename), "el mod sin skin no trae el de por defecto: necesita juego base");
        // Del juego base se monta assets/, como hace el motor: los otros mods de
        // su mods/ no se cuelan.
        writeSheet(official / "mods/other/images/game/notes/Other", baseGameFrames(true, false));
        expect(baseMountOf(official) == official / "assets", "del juego base se monta su assets/");
        Scan s;
        scan(s, {bg / "fanclub/Fan Club", baseMountOf(nearby.folder)});
        expect(styleById(s, "codename:game/notes/default") != nullptr, "con el juego base, el mod sin skin tiene el default");
        expect(styleById(s, "codename:mods/other/game/notes/Other") == nullptr && s.catalog.styles.size() == 1,
               "y no los estilos de otros mods del juego base");
        // La build de un mod trae el motor entero, una carpeta mas abajo: no
        // necesita juego base. Un mod que solo sobrescribe el skin, si.
        Scan build;
        scan(build, {lone});
        expect(hasEngineDefault(build.catalog, Engine::Codename), "la build de un mod ya trae el skin por defecto del motor");
        writeSheet(base / "ovr/mods/Skin/images/game/notes/default", baseGameFrames(true, true));
        Scan overrides;
        scan(overrides, {base / "ovr/mods/Skin"});
        expect(!hasEngineDefault(overrides.catalog, Engine::Codename), "un mod que sobrescribe el skin sigue necesitando el resto");
    }
    {
        // Leather y Kade usan Polymod como V-Slice, pero no son ninguno de los
        // tres: Note Lab no los abre (DESIGN_PLUGIN_NOTE_LAB §30).
        const fs::path leather = base / "lea";
        writeFile(leather / "assets/data/song data/tutorial/tutorial.json", "{}");
        writeFile(leather / "assets/data/ui skins/default/config.json", "{}");
        writeFile(leather / "assets/data/arrow types/default.json", "{}");
        writeFile(leather / "mods/Mod/_polymod_meta.json", "{\"api_version\":\"0.4.1\"}");
        Scan l;
        scan(l, {leather});
        const EngineGuess leatherGuess = guessEngine(l.vfs, l.catalog);
        expect(leatherGuess.other == "Leather Engine" && !leatherGuess.otherEvidence.empty(),
               "Leather Engine: sus carpetas con espacio pesan mas que el _polymod_meta.json");
        const fs::path kade = base / "kade";
        writeFile(kade / "Kade Engine.exe", "MZ");
        writeSheet(kade / "assets/shared/images/NOTE_assets", {"purple alone0000", "purple hold0000", "purple tail0000"});
        Scan k;
        scan(k, {kade});
        const EngineGuess kadeGuess = guessEngine(k.vfs, k.catalog);
        expect(kadeGuess.other == "Kade Engine", "Kade Engine: su ejecutable");
        const StyleReport* report = nullptr;
        styleById(k, "psych:assets/shared/NOTE_assets", &report);
        expect(report && countErrors(*report) > 0,
               "leido como Psych, su skin de nombres propios daria errores que no son: por eso no se abre");
        const fs::path tricky = base / "trk";
        writeSheet(tricky / "assets/shared/images/NOTE_assets", baseGameFrames(true, true));
        Scan t;
        scan(t, {tricky});
        const EngineGuess trickyGuess = guessEngine(t.vfs, t.catalog);
        expect(trickyGuess.other.empty() && trickyGuess.found && trickyGuess.engine == Engine::Psych,
               "sin marcas de otro motor, el skin antiguo sigue siendo de Psych");
    }
    {
        // Codename: lo que hay en game/notes/ sin ser un tipo.
        const fs::path root = base / "cni";
        writeSheet(root / "assets/images/game/notes/default", baseGameFrames(true, true));
        writeSheet(root / "assets/images/game/notes/Smoke", {"smoke0000", "smoke0001"});
        writeFile(root / "assets/data/splashes/smoke.xml",
                  "<splashes sprite=\"game/notes/Smoke\"><strum id=\"0\"><anim name=\"a\" anim=\"smoke\"/></strum></splashes>");
        writeSheet(root / "assets/images/game/notes/Kahoot", {"purple_note0000", "blue_note0000"});
        writeSheet(root / "assets/images/game/notes/Bullet", baseGameFrames(true, false));
        writeSheet(root / "assets/images/game/notes/Weird", {"weird0000"});
        writeFile(root / "assets/songs/song/charts/hard.json", "{\"strumLines\":[],\"noteTypes\":[\"Weird\"]}");
        Scan s;
        scan(s, {root});
        const StyleReport* report = nullptr;
        const NoteStyle* smoke = styleById(s, "codename:assets/game/notes/Smoke", &report);
        expect(smoke && smoke->use == StyleUse::Declared && !smoke->referenced && countErrors(*report) == 0,
               "Smoke es el sprite de una salpicadura: no es un tipo y no da errores");
        const NoteStyle* kahoot = styleById(s, "codename:assets/game/notes/Kahoot", &report);
        expect(kahoot && kahoot->use == StyleUse::Declared && countErrors(*report) == 0 &&
               countCode(*report, "FML-NOTE-015") == 1, "Kahoot (un skin que pone un script): informacion, no errores");
        const NoteStyle* bullet = styleById(s, "codename:assets/game/notes/Bullet");
        expect(bullet && bullet->use == StyleUse::NoteType, "Bullet trae los nombres del juego base: un tipo listo para el chart");
        const NoteStyle* weird = styleById(s, "codename:assets/game/notes/Weird", &report);
        expect(weird && weird->use == StyleUse::NoteType && countCode(*report, "FML-NOTE-001") == 4,
               "Weird lo usa un chart: sigue siendo un tipo, con sus errores");
        const std::vector<NoteTypeEntry> types = scanNoteTypes(s.vfs, s.catalog);
        expect(!findNoteType(types, "Smoke") && !findNoteType(types, "Kahoot") && findNoteType(types, "Bullet") &&
               findNoteType(types, "Weird"), "el catalogo de tipos no lista la salpicadura ni el skin del script");
    }
    {
        // V-Slice 0.9 de prueba: el notestyle en su carpeta, con las rutas desde
        // assets/ y sin images/.
        const fs::path root = base / "v09";
        const fs::path folder = root / "assets/gameplay/notestyles/funkin";
        writeFile(folder / "funkin.json",
                  "{\"version\":\"1.1.0\",\"name\":\"Funkin'\",\"assets\":{"
                  "\"note\":{\"assetPath\":\"gameplay/notestyles/funkin/notes\",\"scale\":0.7,\"data\":{"
                  "\"left\":{\"prefix\":\"noteLeft\"},\"down\":{\"prefix\":\"noteDown\"},"
                  "\"up\":{\"prefix\":\"noteUp\"},\"right\":{\"prefix\":\"noteRight\"}}},"
                  "\"holdNote\":{\"assetPath\":\"gameplay/notestyles/funkin/note-holds\",\"data\":{}},"
                  "\"judgementSick\":{\"assetPath\":\"gameplay/notestyles/funkin/popup/sick\"}}}");
        writeSheet(folder / "notes", {"noteLeft0000", "noteDown0000", "noteUp0000", "noteRight0000"});
        writeFile(folder / "note-holds.png", "PNG");
        writeFile(folder / "popup/sick.png", "PNG");
        writeFile(root / "assets/gameplay/notekinds/mom.hxc",
                  "class MomNoteKind extends NoteKind { function new() { super('mom', 'Mom sings'); } }\n");
        Scan s;
        scan(s, {root});
        const StyleReport* report = nullptr;
        const NoteStyle* style = styleById(s, "vslice:assets/funkin", &report);
        expect(style && report->partsResolved == static_cast<int>(style->parts.size()) && countErrors(*report) == 0 &&
               style->judgements[0].image == "assets/gameplay/notestyles/funkin/popup/sick.png",
               "V-Slice 0.9: el notestyle y sus imagenes, junto a el");
        const EngineGuess guess = guessEngine(s.vfs, s.catalog);
        expect(guess.found && guess.engine == Engine::VSlice, "V-Slice 0.9: motor detectado por gameplay/notestyles");
        const std::vector<NoteTypeEntry> types = scanNoteTypes(s.vfs, s.catalog);
        expect(findNoteType(types, "mom", Engine::VSlice) != nullptr, "V-Slice 0.9: los NoteKind de gameplay/notekinds");
    }
    {
        // Psych: skins pixel (Note.hx:326-336 y StrumNote.hx:63-94 de 0.7).
        const fs::path root = base / "pix";
        const fs::path ui = root / "assets/shared/images/pixelUI/noteSkins";
        writeSheet(root / "assets/shared/images/noteSkins/NOTE_assets", baseGameFrames(true, true));
        writeGridPng(ui / "NOTE_assets.png", 68, 85, 4, 5, {17});
        writeGridPng(ui / "NOTE_assetsENDS.png", 28, 12, 4, 2, {});
        writeGridPng(ui / "NOTE_assets-chip.png", 70, 85, 4, 5, {});
        Scan s;
        scan(s, {root});
        const StyleReport* report = nullptr;
        const NoteStyle* pixel = styleById(s, "psych:assets/shared/pixelUI/noteSkins/NOTE_assets", &report);
        expect(pixel && pixel->pixel && pixel->use == StyleUse::Declared && pixel->useDetail == "pixel" && pixel->referenced &&
               pixel->sheets.size() == 2 && pixel->sheets[0].kind == SheetKind::Grid && pixel->sheets[1].rows == 2,
               "Psych: el skin pixel es un estilo con sus dos rejillas (notas 4x5, sostenidos 4x2)");
        if (pixel) {
            const PartBinding* up = findPart(*pixel, Part::Note, 2);
            const PartBinding* confirmDown = findPart(*pixel, Part::StrumConfirm, 1);
            const PartBinding* confirmUp = findPart(*pixel, Part::StrumConfirm, 2);
            const PartBinding* endRight = findPart(*pixel, Part::HoldEnd, 3);
            expect(up && up->animation.indices == std::vector<int>{6} && confirmDown &&
                   confirmDown->animation.indices == std::vector<int>({13, 17}) && confirmUp && confirmUp->animation.fps == 12.0f &&
                   endRight && endRight->sheet == 1 && endRight->animation.indices == std::vector<int>{7},
                   "celdas como Psych: nota 4+d, acierto 12+d y 16+d (el de arriba a 12 FPS), final 4+d de ENDS");
            const Finding* info = findingOf(*report, "FML-NOTE-007");
            expect(info && info->severity == Severity::Info && info->detail == "4 x 5, 17 x 17 px",
                   "se dice de que rejilla sale (FML-NOTE-007)");
            const Finding* hole = findingOf(*report, "FML-NOTE-025");
            expect(hole && hole->detail == "17" && hole->severity == Severity::Warning && countErrors(*report) == 0,
                   "la celda 17 esta vacia: el acierto de abajo pierde un fotograma (FML-NOTE-025)");
            struct Sizes : IImageInfo {
                ImageInfo imageInfo(const std::string& path) override {
                    if (path.find("ENDS") != std::string::npos) return {28, 12, true};
                    return {68, 85, true};
                }
            } sizes;
            AtlasStore atlases = atlasStoreOf(s);
            NotePreview preview;
            PreviewState state;
            RenderList list;
            preview.build(*pixel, atlases, &sizes, PreviewSettings{}, state, {{1, 2, 300.0, 400.0}}, list);
            int cells = 0;
            bool sharp = true;
            for (const DrawCmd& cmd : list.cmds)
                if (cmd.texture >= 0 && list.textures[static_cast<size_t>(cmd.texture)].find("pixelUI") != std::string::npos) {
                    ++cells;
                    sharp = sharp && !cmd.antialiasing && (cmd.sw == 17.0f || cmd.sw == 7.0f);
                }
            expect(cells >= 10 && sharp, "la vista previa dibuja las celdas, sin suavizado");
            expect(NotePreview::laneWidthOf(*pixel) == 160.0f * 0.7f, "carriles de 112 px aunque la escala sea 6");
        }
        const NoteStyle* chip = styleById(s, "psych:assets/shared/pixelUI/noteSkins/NOTE_assets-chip", &report);
        expect(chip && countCode(*report, "FML-NOTE-024") == 1 && countCode(*report, "FML-NOTE-002") == 1,
               "chip pixel: 70 px no se reparte en 4 columnas (FML-NOTE-024) y le falta su ENDS");
    }
    {
        // Un mod en ZIP: su musica se saca a la cache temporal de la Vfs para
        // poder oirla (antes no sonaba nada).
        const fs::path zipPath = base / "zipsong.zip";
        mz_zip_archive zip{};
        bool written = mz_zip_writer_init_file(&zip, zipPath.u8string().c_str(), 0) != 0;
        const std::string chart = "{\"strumLines\":[{\"type\":1,\"position\":\"boyfriend\",\"characters\":[\"bf\"],"
                                  "\"notes\":[{\"time\":1000,\"id\":0,\"sLen\":0}]}]}";
        const std::string meta = "{\"name\":\"song\",\"bpm\":100}";
        const std::string ogg = "OggS-not-really";
        written = written && mz_zip_writer_add_mem(&zip, "songs/song/charts/hard.json", chart.data(), chart.size(), MZ_BEST_SPEED);
        written = written && mz_zip_writer_add_mem(&zip, "songs/song/meta.json", meta.data(), meta.size(), MZ_BEST_SPEED);
        written = written && mz_zip_writer_add_mem(&zip, "songs/song/song/Inst.ogg", ogg.data(), ogg.size(), MZ_BEST_SPEED);
        // Con una sola carpeta arriba, la Vfs la trataria como envoltorio.
        written = written && mz_zip_writer_add_mem(&zip, "data/stages/stage.xml", "<stage/>", 8, MZ_BEST_SPEED);
        written = written && mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
        Scan s;
        scan(s, {zipPath});
        const std::vector<SongChart> songs = scanSongs(s.vfs);
        expect(written && songs.size() == 1, "ZIP: su chart de Codename");
        if (!songs.empty()) {
            const LoadedSong loaded = loadSong(s.vfs, zipPath, songs[0]);
            std::error_code ec;
            expect(loaded.audio.size() == 1 && fs::file_size(fs::u8path(loaded.audio[0]), ec) == ogg.size() &&
                   loaded.audio[0].find("zip-cache") != std::string::npos,
                   "ZIP: el Inst se saca a la cache temporal para oirlo");
        }
    }
}

// ------------------------------------------------------------------- crear --

std::vector<unsigned char> readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool near(const std::uint8_t* p, int r, int g, int b, int tolerance = 6) {
    return std::abs(p[0] - r) <= tolerance && std::abs(p[1] - g) <= tolerance && std::abs(p[2] - b) <= tolerance;
}

// Una flecha de prueba de 32x32 en (ox, 0): brillo blanco en el centro,
// relleno de su color y contorno negro, como las del juego base.
void drawTestArrow(Image& sheet, int ox, std::uint32_t fill) {
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const float d = std::hypot(x + 0.5f - 16.0f, y + 0.5f - 16.0f);
            std::uint8_t* p = sheet.at(ox + x, y);
            if (d >= 15.0f) continue;
            p[3] = 255;
            if (d < 4.0f) p[0] = p[1] = p[2] = 255;
            else if (d < 12.5f) {
                p[0] = static_cast<std::uint8_t>((fill >> 16) & 0xFF);
                p[1] = static_cast<std::uint8_t>((fill >> 8) & 0xFF);
                p[2] = static_cast<std::uint8_t>(fill & 0xFF);
            } else p[0] = p[1] = p[2] = 10;
        }
}

void createCase() {
    std::printf("\n== Crear: pintar estilos, marcas, textos y nombres ==\n");
    static const char* colors[4] = {"purple", "blue", "green", "red"};
    static const std::uint32_t fills[4] = {0xFFC24B99u, 0xFF00FFFFu, 0xFF12FA05u, 0xFFF9393Fu};
    Image sheet = blankImage(160, 32);
    std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<TextureAtlas imagePath=\"sheet.png\">\n";
    for (int d = 0; d < 4; ++d) {
        drawTestArrow(sheet, d * 32, fills[d]);
        xml += "  <SubTexture name=\"" + std::string(colors[d]) + "0000\" x=\"" + std::to_string(d * 32) + "\" y=\"0\" width=\"32\" height=\"32\"/>\n";
    }
    drawTestArrow(sheet, 128, 0xFF87A3ADu);
    xml += "  <SubTexture name=\"arrowLEFT0000\" x=\"128\" y=\"0\" width=\"32\" height=\"32\"/>\n</TextureAtlas>\n";
    NoteStyle style;
    style.engine = Engine::Codename;
    style.id = "test:create";
    Sheet s;
    s.image = "img/sheet.png";
    s.atlas = "img/sheet.xml";
    s.kind = SheetKind::Sparrow;
    const int index = addSheet(style, s);
    for (int d = 0; d < 4; ++d) {
        Animation anim;
        anim.prefix = colors[d];
        bindPart(style, Part::Note, d, index, anim);
    }
    Animation strum;
    strum.prefix = "arrowLEFT";
    bindPart(style, Part::StrumStatic, 0, index, strum);
    const std::vector<unsigned char> sheetPng = encodePng(sheet);
    ExportIo io;
    io.readBytes = [&](const std::string& path) -> std::optional<std::vector<unsigned char>> {
        if (path == "img/sheet.png") return sheetPng;
        return std::nullopt;
    };
    io.readText = [&](const std::string& path) -> std::optional<std::string> {
        if (path == "img/sheet.xml") return xml;
        return std::nullopt;
    };

    // Las notas a verde; el receptor se queda.
    const auto painted = paintStyleSheets(style, [](Part part, int) {
        Paint paint;
        if (part == Part::Note) {
            paint.mode = PaintMode::Hue;
            paint.color = 0xFF3FD14Au;
        }
        return paint;
    }, io);
    Image out;
    const bool decoded = painted.size() == 1 && painted[0].image == "img/sheet.png" && decodePng(painted[0].png, out);
    expect(decoded, "pintar: sale la hoja que cambia, con su ruta");
    if (decoded) {
        bool green = true;
        for (int d = 0; d < 4; ++d) {
            const std::uint8_t* p = out.at(d * 32 + 24, 16);
            green = green && p[1] > p[0] + 40 && p[1] > p[2] + 40;
        }
        expect(green, "a un color: el relleno de las cuatro notas pasa a verde");
        expect(near(out.at(16, 16), 255, 255, 255, 12) && near(out.at(29, 16), 10, 10, 10, 12),
               "a un color: el brillo blanco y el contorno negro se quedan");
        expect(std::equal(out.at(128, 0), out.at(128, 0) + 4, sheet.at(128, 0)) &&
               std::equal(out.at(152, 16), out.at(152, 16) + 4, sheet.at(152, 16)),
               "el receptor, sin pintar, sale igual");
    }

    // Tres colores por la luz: negro al oscuro y blanco al claro.
    Image two = blankImage(2, 1);
    two.at(0, 0)[3] = 255;
    two.at(1, 0)[0] = two.at(1, 0)[1] = two.at(1, 0)[2] = two.at(1, 0)[3] = 255;
    Paint gradient;
    gradient.mode = PaintMode::Gradient;
    gradient.stops = {0xFF000080u, 0xFF808000u, 0xFFFFFF00u};
    paintImage(two, gradient);
    expect(near(two.at(0, 0), 0, 0, 128) && near(two.at(1, 0), 255, 255, 0), "tres colores: el negro al oscuro, el blanco al claro");

    Paint half;
    half.alpha = 0.5f;
    Image solid = blankImage(4, 4);
    for (size_t i = 0; i < 16; ++i) solid.rgba[i * 4 + 3] = 200;
    paintImage(solid, half);
    expect(solid.rgba[3] == 100, "la opacidad se hornea en el alfa");

    // Una marca blanca en el centro de un cuadrado rojo.
    Image square = blankImage(64, 64);
    for (size_t i = 0; i < 64 * 64; ++i) {
        square.rgba[i * 4] = 255;
        square.rgba[i * 4 + 3] = 255;
    }
    Image mark = blankImage(16, 16);
    for (auto& v : mark.rgba) v = 255;
    stampMark(square, mark, MarkPlace::Center, 0.5f, 1.0f);
    expect(near(square.at(32, 32), 255, 255, 255) && near(square.at(4, 4), 255, 0, 0) && near(square.at(14, 32), 255, 0, 0),
           "marca: en el centro de lo visible, con su tamano");
    Image corner = blankImage(64, 64);
    for (size_t i = 0; i < 64 * 64; ++i) corner.rgba[i * 4 + 3] = 255;
    stampMark(corner, mark, MarkPlace::Corner, 0.25f, 1.0f);
    expect(corner.at(54, 6)[0] == 255 && corner.at(10, 54)[0] == 0, "marca en la esquina: arriba a la derecha");

    // La plantilla RGB de Psych se hornea con la paleta del carril.
    Image red = blankImage(8, 8);
    for (size_t i = 0; i < 64; ++i) {
        red.rgba[i * 4] = 255;
        red.rgba[i * 4 + 3] = 255;
    }
    const std::vector<unsigned char> redPng = encodePng(red);
    const std::string redXml = "<?xml version=\"1.0\"?>\n<TextureAtlas imagePath=\"r.png\"><SubTexture name=\"purple0000\" x=\"0\" y=\"0\" width=\"8\" height=\"8\"/></TextureAtlas>\n";
    NoteStyle templ;
    templ.engine = Engine::Psych;
    templ.rgbPalette = true;
    Sheet rs;
    rs.image = "r.png";
    rs.atlas = "r.xml";
    const int ri = addSheet(templ, rs);
    Animation purple;
    purple.prefix = "purple";
    bindPart(templ, Part::Note, 0, ri, purple);
    ExportIo rio;
    rio.readBytes = [&](const std::string& p) -> std::optional<std::vector<unsigned char>> { return p == "r.png" ? std::optional<std::vector<unsigned char>>(redPng) : std::nullopt; };
    rio.readText = [&](const std::string& p) -> std::optional<std::string> { return p == "r.xml" ? std::optional<std::string>(redXml) : std::nullopt; };
    const auto baked = paintStyleSheets(templ, [](Part, int) { return Paint{}; }, rio);
    Image bakedImage;
    expect(baked.size() == 1 && decodePng(baked[0].png, bakedImage) && near(bakedImage.at(3, 3), 0xC2, 0x4B, 0x99, 2),
           "plantilla RGB: se hornea con el morado por defecto de Psych (ClientPrefs.hx:28-37)");

    // La plantilla RGB de Psych 0.7 da la misma region a los cuatro tramos
    // (NOTE_assets.xml): cada direccion sale con su copia y su color.
    {
        const std::string sharedXml =
            "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<TextureAtlas imagePath=\"r.png\">\n"
            "\t<!-- Adobe Animate -->\n"
            "\t<SubTexture name=\"blue hold piece0000\" x=\"0\" y=\"0\" width=\"8\" height=\"8\"/>\n"
            "\t<SubTexture name=\"green hold piece0000\" x=\"0\" y=\"0\" width=\"8\" height=\"8\"/>\n"
            "\t<SubTexture name=\"purple hold piece0000\" x=\"0\" y=\"0\" width=\"8\" height=\"8\"/>\n"
            "\t<SubTexture name=\"red hold piece0000\" x=\"0\" y=\"0\" width=\"8\" height=\"8\" frameX=\"0\" frameY=\"0\" frameWidth=\"8\" frameHeight=\"8\"/>\n"
            "</TextureAtlas>\n";
        NoteStyle holds;
        holds.engine = Engine::Psych;
        holds.rgbPalette = true;
        Sheet hs;
        hs.image = "r.png";
        hs.atlas = "r.xml";
        const int hi = addSheet(holds, hs);
        static const char* colors[4] = {"purple", "blue", "green", "red"};
        for (int d = 0; d < 4; ++d) {
            Animation piece;
            piece.prefix = std::string(colors[d]) + " hold piece";
            bindPart(holds, Part::HoldPiece, d, hi, piece);
        }
        ExportIo hio;
        hio.readBytes = rio.readBytes;
        hio.readText = [&](const std::string& p) -> std::optional<std::string> { return p == "r.xml" ? std::optional<std::string>(sharedXml) : std::nullopt; };
        const auto split = paintStyleImages(holds, [](Part, int) { return Paint{}; }, hio);
        bool ok = split.size() == 1 && split[0].atlas == "r.xml" && !split[0].atlasText.empty();
        if (ok) {
            DiagnosticSink sink;
            const Result<SparrowAtlas> atlas = parseSparrowAtlas(split[0].atlasText, "r.xml", sink);
            ok = atlas && atlas.value().frames.size() == 4;
            std::set<std::pair<int, int>> places;
            int matched = 0;
            for (int d = 0; ok && d < 4; ++d)
                for (const AtlasFrame& f : atlas.value().frames) {
                    if (f.name != std::string(colors[d]) + " hold piece0000") continue;
                    places.insert({f.x, f.y});
                    const std::uint32_t* pal = psychDefaultPalette(d, false);
                    const std::uint8_t* px = split[0].pixels.at(f.x + 3, f.y + 3);
                    if (f.w == 8 && f.h == 8 && near(px, (pal[0] >> 16) & 0xFF, (pal[0] >> 8) & 0xFF, pal[0] & 0xFF, 2)) ++matched;
                }
            ok = ok && places.size() == 4 && matched == 4 && split[0].pixels.w == 8 && split[0].pixels.h > 8;
            ok = ok && split[0].atlasText.find("frameWidth=\"8\"") != std::string::npos;
        }
        expect(ok, "plantilla RGB: los cuatro tramos que comparten region salen con su copia y el color de su carril");

        // Lo que no cambia conserva su region; lo que si, va a una copia.
        NoteStyle mixed = holds;
        mixed.rgbPalette = false;
        const auto kept = paintStyleImages(mixed, [](Part, int d) {
            Paint p;
            if (d == 2) { p.mode = PaintMode::Hue; p.color = 0xFF00FF00u; }
            return p;
        }, hio);
        bool keptOk = kept.size() == 1 && !kept[0].atlasText.empty() && near(kept[0].pixels.at(3, 3), 255, 0, 0);
        if (keptOk) {
            DiagnosticSink sink;
            const Result<SparrowAtlas> atlas = parseSparrowAtlas(kept[0].atlasText, "r.xml", sink);
            int moved = 0;
            for (const AtlasFrame& f : atlas.value().frames) {
                if (f.x == 0 && f.y == 0) continue;
                ++moved;
                keptOk = keptOk && f.name == "green hold piece0000" && kept[0].pixels.at(f.x + 3, f.y + 3)[1] > 200;
            }
            keptOk = keptOk && moved == 1;
        }
        expect(keptOk, "regiones compartidas: la pieza sin cambios conserva la original y la pintada va a su copia");
    }

    // Textos del ranking con una fuente de Windows.
    const fs::path fonts = "C:/Windows/Fonts";
    const std::vector<unsigned char> black = readAll(fonts / "seguibl.ttf");
    const std::vector<unsigned char> regular = readAll(fonts / "segoeui.ttf");
    const std::vector<unsigned char> symbols = readAll(fonts / "seguisym.ttf");
    if (black.empty() || regular.empty()) {
        std::printf("[skip] sin Segoe UI en C:/Windows/Fonts: textos sin probar\n");
    } else {
        expect(fontFamilyName(regular) == "Segoe UI" && fontFamilyName(black) == "Segoe UI Black",
               "el nombre de la fuente sale de su tabla name: " + fontFamilyName(black));
        TextLook look;
        look.size = 64.0f;
        look.top = 0xFF78F0FFu;
        look.bottom = 0xFF2878FFu;
        look.outline = 0xFF141432u;
        look.outlineWidth = 6.0f;
        look.tiltDeg = -6.0f;
        const auto t0 = std::chrono::steady_clock::now();
        const Image sick = renderText(black, "SICK!!", look);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        expect(!sick.empty() && sick.w > sick.h * 2, "texto: SICK!! sale como imagen apaisada");
        int outlinePx = 0, topCyan = 0, bottomBlue = 0;
        for (int y = 0; y < sick.h; ++y)
            for (int x = 0; x < sick.w; ++x) {
                const std::uint8_t* p = sick.at(x, y);
                if (p[3] < 250) continue;
                if (p[0] < 40 && p[1] < 40 && p[2] < 70) ++outlinePx;
                if (y < sick.h / 3 && p[1] > 180 && p[2] > 200) ++topCyan;
                if (y > sick.h * 2 / 3 && p[2] > 200 && p[1] < 150) ++bottomBlue;
            }
        expect(outlinePx > 200 && topCyan > 20 && bottomBlue > 20, "texto: contorno oscuro y degradado de arriba abajo");
        expect(ms < 400.0, "texto: se genera rapido para verlo al momento (" + std::to_string(static_cast<int>(ms)) + " ms)");
        TextLook pixel = look;
        pixel.pixel = true;
        pixel.tiltDeg = 0.0f;
        const Image hard = renderText(black, "GOOD", pixel);
        bool crisp = !hard.empty();
        for (size_t i = 0; i < hard.rgba.size() / 4 && crisp; ++i) crisp = hard.rgba[i * 4 + 3] == 0 || hard.rgba[i * 4 + 3] == 255;
        expect(crisp, "texto pixel art: sin suavizado, alfa 0 o 255");
        expect(renderText(black, "", look).empty() && renderText({}, "SICK", look).empty(), "texto vacio o sin fuente: imagen vacia");
    }
    const Image skull = builtinMarkImage(BuiltinMark::Skull, 48, symbols);
    expect(!skull.empty() && (symbols.empty() || fontHasCodepoint(symbols, 0x2620)), "marca de serie: la calavera de Segoe UI Symbol");
    const Image dot = builtinMarkImage(BuiltinMark::Star, 48, {});
    expect(dot.w == 48 && dot.at(24, 24)[3] == 255 && dot.at(1, 1)[3] == 0, "marca sin fuente: un circulo que siempre se ve");

    // Reconocer lo importado por los nombres de los motores.
    struct Case { const char* name; Part part; int direction; int variant; };
    const Case cases[] = {
        {"purple0000", Part::Note, 0, 0}, {"blue hold piece0000", Part::HoldPiece, 1, 0},
        {"pruple end hold0000", Part::HoldEnd, 0, 0}, {"red hold end0000", Part::HoldEnd, 3, 0},
        {"arrowUP0000", Part::StrumStatic, 2, 0}, {"left press0002", Part::StrumPress, 0, 0},
        {"down confirm0003", Part::StrumConfirm, 1, 0}, {"noteRight0000", Part::Note, 3, 0},
        {"staticDown0000", Part::StrumStatic, 1, 0}, {"pressUp0001", Part::StrumPress, 2, 0},
        {"confirmLeft0001", Part::StrumConfirm, 0, 0}, {"holdCoverStartPurple0000", Part::HoldCoverStart, 0, 0},
        {"holdCoverRed0002", Part::HoldCover, 3, 0}, {"holdCoverEndGreen0000", Part::HoldCoverEnd, 2, 0},
        {"note impact 1 purple0000", Part::Splash, 0, 0}, {"note impact 2 blue0001", Part::Splash, 1, 1},
        {"note splash green 10002", Part::Splash, 2, 0}, {"note splash red 20000", Part::Splash, 3, 1},
        {"splash2Left0003", Part::Splash, 0, 1}, {"A00000", Part::Note, 0, 0}, {"B hold0000", Part::HoldPiece, 1, 0},
        {"C tail0000", Part::HoldEnd, 2, 0}, {"D press0001", Part::StrumPress, 3, 0}, {"A confirm0002", Part::StrumConfirm, 0, 0},
    };
    int right = 0;
    std::string wrong;
    for (const Case& c : cases) {
        const PieceGuess g = guessPiece(c.name);
        if (g.found && g.part == c.part && g.direction == c.direction && g.variant == c.variant) ++right;
        else wrong += std::string(" ") + c.name;
    }
    expect(right == static_cast<int>(sizeof(cases) / sizeof(cases[0])),
           "nombres de los tres motores y de Extra Keys: " + std::to_string(right) + "/" + std::to_string(sizeof(cases) / sizeof(cases[0])) + wrong);
    expect(!guessPiece("healthBar").found && !guessPiece("gf dance0000").found, "lo que no es una pieza no se adivina");
    expect(guessHudIndex("sick") == 0 && guessHudIndex("Good") == 1 && guessHudIndex("bad-pixel") == 2 && guessHudIndex("shit") == 3 &&
           guessHudIndex("combo") == 4 && guessHudIndex("num0") == 5 && guessHudIndex("num9") == 14 && guessHudIndex("num5-pixel") == 10,
           "HUD: juicios, combo y cifras por su nombre, tambien pixel");
    expect(guessHudIndex("ready") == 16 && guessHudIndex("set") == 17 && guessHudIndex("go") == 18 && guessHudIndex("intro3") == 15 &&
           guessHudIndex("introGo") == 18 && guessHudIndex("introTWO") == 16 && guessHudIndex("healthBar") == -1,
           "HUD: cuenta atras por imagen o por sonido");

    // Recetas: presets y como pinta cada pieza.
    std::array<std::uint32_t, 4> classic{}, again{}, other{};
    expect(arrowPreset("classic", classic) && classic[0] == 0xFFC24B99u && !arrowPreset("nada", other),
           "presets de flechas: el clasico son los colores del juego base");
    expect(arrowPreset("random", again, 7) && arrowPreset("random", other, 7) && again == other && arrowPreset("random", classic, 8) && classic != again,
           "preset al azar: la misma semilla da los mismos colores");
    ArrowRecipe recipe;
    expect(arrowPaint(recipe, Part::StrumStatic, 0).mode == PaintMode::Keep && arrowPaint(recipe, Part::Note, 2).color == recipe.colors[2],
           "HUD de flechas: receptores grises por defecto, cada nota con su color");
    recipe.strums = StrumLook::Clear;
    recipe.outline = 0xFF102030u;
    const Paint outlined = arrowPaint(recipe, Part::Note, 1);
    expect(arrowPaint(recipe, Part::StrumStatic, 0).alpha < 0.5f && outlined.mode == PaintMode::Gradient && outlined.stops[0] == 0xFF102030u,
           "HUD de flechas: receptores transparentes y contorno propio en tres colores");
    TypeLookRecipe look;
    expect(typeLookPreset("poison", look) && look.mark == static_cast<int>(BuiltinMark::Skull) && !typeLookPreset("nada", look),
           "presets de aspecto: veneno lleva la calavera");
    const Image skullMark = builtinMarkImage(BuiltinMark::Skull, 32, symbols);
    expect(typePaint(look, Part::Note, 0, &skullMark).mark == &skullMark && typePaint(look, Part::HoldPiece, 0, &skullMark).mark == nullptr,
           "aspecto: la marca va en la nota, no en el sostenido");
    look.hold = TypeHold::Hidden;
    look.splash = TypeSplash::None;
    expect(typePaint(look, Part::HoldEnd, 3, nullptr).alpha == 0.0f && !typePaint(look, Part::Splash, 1, nullptr).changes() &&
           !typePaint(look, Part::StrumStatic, 0, nullptr).changes(),
           "aspecto: sostenido oculto, salpicadura sin pintar y el tipo no toca receptores");
    if (!black.empty()) {
        RatingRecipe rating;
        const std::vector<Image> empty = renderRatingSet(rating, black);
        expect(empty.size() == 15 && std::all_of(empty.begin(), empty.end(), [](const Image& image) { return image.empty(); }),
               "ranking: el creador empieza vacio, sin maqueta ni cifras de ejemplo");
        rating.texts[0] = "Custom";
        const std::vector<Image> partial = renderRatingSet(rating, black);
        expect(partial.size() == 15 && !partial[0].empty() && partial[4].empty() && partial[5].empty(),
               "ranking: solo muestra lo configurado y no inventa un rotulo ni cifras");
        rating.texts = {"SICK!!", "GOOD", "BAD", "SHIT", "COMBO"};
        const std::vector<Image> set = renderRatingSet(rating, black);
        bool all = set.size() == 15;
        for (const Image& image : set) all = all && !image.empty();
        expect(all && set[0].w > set[5].w * 2, "ranking con texto: 15 imagenes (juicios, rotulo y cifras)");
    }
}

void writeBytes(const fs::path& path, const std::vector<unsigned char>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

Image squareImage(int size, int inset, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    Image image = blankImage(size, size);
    for (int y = inset; y < size - inset; ++y)
        for (int x = inset; x < size - inset; ++x) {
            std::uint8_t* p = image.at(x, y);
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    return image;
}

void importCase(const fs::path& base) {
    std::printf("\n== Importar: empaquetar, GIF y reconocer ==\n");
    // Tres fotogramas de 16x16; los dos primeros iguales, con borde transparente.
    const std::vector<NamedFrame> frames = {{"purple0000", squareImage(16, 4, 255, 0, 0)},
                                            {"purple0001", squareImage(16, 4, 255, 0, 0)},
                                            {"blue0000", squareImage(16, 2, 0, 0, 255)}};
    const SparrowOut packed = packSparrow(frames, "mis_flechas.png");
    DiagnosticSink sink;
    const Result<SparrowAtlas> atlas = parseSparrowAtlas(packed.xml, "mis_flechas.xml", sink);
    Image sheet;
    expect(packed.ok && packed.frames == 3 && atlas && atlas.value().frames.size() == 3 && decodePng(packed.png, sheet),
           "empaquetar: tres fotogramas en un Sparrow que se relee");
    if (atlas && atlas.value().frames.size() == 3) {
        const AtlasFrame& a = atlas.value().frames[0];
        const AtlasFrame& b = atlas.value().frames[1];
        expect(a.x == b.x && a.y == b.y && a.w == 8 && a.frameX == -4 && a.frameW == 16,
               "empaquetar: los repetidos comparten region y el recorte guarda su sitio (frameX -4)");
        const Image back = cropFrame(sheet, atlas.value().frames[2]);
        expect(back.w == 12 && near(back.at(5, 5), 0, 0, 255), "empaquetar: cada fotograma sale con sus pixeles");
    }

    // Un GIF de 1x1 con dos fotogramas (rojo 100 ms, azul 200 ms), hecho a mano.
    const std::vector<unsigned char> gif = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
        0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
        0x02, 0x02, 0x44, 0x01, 0x00,
        0x21, 0xF9, 0x04, 0x00, 0x14, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
        0x02, 0x02, 0x4C, 0x01, 0x00, 0x3B};
    GifFrames decoded;
    expect(decodeGif(gif, decoded) && decoded.frames.size() == 2 && decoded.delaysMs[0] == 100 && decoded.delaysMs[1] == 200 &&
           near(decoded.frames[0].at(0, 0), 255, 0, 0, 1) && near(decoded.frames[1].at(0, 0), 0, 0, 255, 1),
           "GIF: dos fotogramas con su color y lo que dura cada uno");
    expect(!decodeGif({'P', 'N', 'G'}, decoded), "GIF: lo que no es un GIF no se lee");

    // Lo que se suelta: un atlas, juicios sueltos, una carpeta de cifras, una
    // secuencia de fotogramas, un sonido y un GIF.
    const fs::path drop = base / "soltar";
    writeBytes(drop / "mis_flechas.png", packed.png);
    writeFile(drop / "mis_flechas.xml", packed.xml);
    writeBytes(drop / "sick.png", encodePng(squareImage(8, 0, 0, 255, 255)));
    for (int n = 0; n < 3; ++n) writeBytes(drop / "numeros" / ("num" + std::to_string(n) + ".png"), encodePng(squareImage(8, 0, 255, 255, 255)));
    for (int n = 0; n < 4; ++n) writeBytes(drop / "brillo" / ("left confirm000" + std::to_string(n) + ".png"), encodePng(squareImage(8, n, 255, 255, 255)));
    writeFile(drop / "intro3.ogg", "OggS");
    writeBytes(drop / "chispa.gif", gif);
    const std::vector<ImportItem> items = scanImport({drop / "mis_flechas.xml", drop / "sick.png", drop / "numeros", drop / "brillo",
                                                      drop / "intro3.ogg", drop / "chispa.gif"});
    auto find = [&](ImportKind kind) -> const ImportItem* {
        for (const ImportItem& item : items) if (item.kind == kind) return &item;
        return nullptr;
    };
    const ImportItem* atlasItem = find(ImportKind::Atlas);
    expect(atlasItem && atlasItem->animations.size() == 2 && atlasItem->animations[0].piece.found &&
           atlasItem->animations[0].piece.part == Part::Note && atlasItem->animations[0].frames == 2 &&
           atlasItem->animations[1].piece.direction == 1,
           "importar: el atlas elegido por su XML va con su PNG, dos animaciones reconocidas");
    int digits = 0;
    bool sick = false;
    for (const ImportItem& item : items) {
        if (item.kind == ImportKind::Image && item.hudIndex >= 5 && item.hudIndex <= 7) ++digits;
        if (item.kind == ImportKind::Image && item.hudIndex == 0 && item.width == 8) sick = true;
    }
    expect(sick && digits == 3, "importar: los juicios y las cifras de una carpeta, cada uno con su sitio en el HUD");
    const ImportItem* sequence = find(ImportKind::Frames);
    expect(sequence && sequence->files.size() == 4 && sequence->animations.size() == 1 &&
           sequence->animations[0].piece.part == Part::StrumConfirm && sequence->animations[0].piece.direction == 0,
           "importar: una carpeta de fotogramas numerados es una animacion (receptor acierto izquierda)");
    const ImportItem* sound = find(ImportKind::Sound);
    const ImportItem* gifItem = find(ImportKind::Gif);
    expect(sound && sound->hudIndex == 15 && gifItem && gifItem->animations.size() == 1 && gifItem->animations[0].frames == 2,
           "importar: el sonido de la cuenta atras y el GIF con sus fotogramas");

    // Los fotogramas de lo importado: del atlas por animacion, con su caja.
    const std::vector<Image> purple = atlasItem ? importFrames(*atlasItem, 0) : std::vector<Image>{};
    expect(purple.size() == 2 && purple[0].w == 16 && near(purple[0].at(8, 8), 255, 0, 0),
           "importar: los fotogramas de una animacion del atlas, con su caja entera");
    expect(sequence && importFrames(*sequence, 0).size() == 4 && gifItem && importFrames(*gifItem, 0).size() == 2,
           "importar: los de una carpeta numerada y los de un GIF");

    // Una flecha de 8x4 que apunta a la izquierda (columna 0 roja) girada.
    Image left = blankImage(8, 4);
    for (int y = 0; y < 4; ++y) {
        left.at(0, y)[0] = 255;
        left.at(0, y)[3] = 255;
    }
    const Image down = rotateForDirection(left, 1), up = rotateForDirection(left, 2), right = rotateForDirection(left, 3);
    expect(down.w == 4 && down.h == 8 && down.at(0, 7)[0] == 255 && down.at(0, 0)[0] == 0 &&
           up.w == 4 && up.at(0, 0)[0] == 255 && right.w == 8 && right.at(7, 0)[0] == 255,
           "girar la imagen de la izquierda: abajo, arriba y derecha");

    // El aspecto de un tipo con imagenes propias: se empaqueta con los nombres
    // del juego base y Note Lab lo lee como un tipo de Codename.
    TypeLookFrames look;
    for (int d = 0; d < 4; ++d) {
        look.note[static_cast<size_t>(d)] = {rotateForDirection(squareImage(16, 2, 255, 0, 0), d)};
        look.holdPiece[static_cast<size_t>(d)] = {squareImage(8, 0, 0, 255, 0)};
        look.holdEnd[static_cast<size_t>(d)] = {squareImage(8, 1, 0, 0, 255)};
    }
    const TypeLookSheet sheetOut = buildTypeLookSheet(look, "Bomba.png");
    expect(sheetOut.ok && sheetOut.parts.size() == 12, "aspecto propio: 12 piezas atadas a un atlas");
    const fs::path typeMod = base / "tipo-propio";
    writeSheet(typeMod / "images/game/notes/default", baseGameFrames(true, true));
    writeBytes(typeMod / "images/game/notes/Bomba.png", sheetOut.atlas.png);
    writeFile(typeMod / "images/game/notes/Bomba.xml", sheetOut.atlas.xml);
    writeFile(typeMod / "songs/x/charts/hard.json", "{\"noteTypes\":[\"Bomba\"]}");
    Scan typeScan;
    scan(typeScan, {typeMod});
    const StyleReport* bombReport = nullptr;
    const NoteStyle* bomb = styleById(typeScan, "codename:game/notes/Bomba", &bombReport);
    expect(bomb && bomb->use == StyleUse::NoteType && bombReport && countErrors(*bombReport) == 0 && bombReport->partsResolved == 12,
           "aspecto propio: el atlas sale como un tipo de Codename con sus 12 piezas, sin errores");

    // Como lo monta la app (typeLookFromImport) y lo exporta como tipo de
    // nota a los tres motores: el paquete se relee sin errores.
    NoteStyle own;
    own.engine = Engine::Codename;
    own.id = "notelab:look:Bomba:1";
    own.name = "Bomba";
    own.use = StyleUse::NoteType;
    own.useDetail = "Bomba";
    Sheet ownSheet;
    ownSheet.kind = SheetKind::Sparrow;
    ownSheet.image = "notelab-import/1/bomba.png";
    ownSheet.atlas = "notelab-import/1/bomba.xml";
    ownSheet.declared = "notelab/bomba";
    ownSheet.scale = 0.7f;
    own.sheets.push_back(ownSheet);
    own.parts = sheetOut.parts;
    ExportIo ownIo;
    ownIo.readBytes = [&](const std::string& p) -> std::optional<std::vector<unsigned char>> {
        if (p == ownSheet.image) return sheetOut.atlas.png;
        if (p == ownSheet.atlas) return std::vector<unsigned char>(sheetOut.atlas.xml.begin(), sheetOut.atlas.xml.end());
        return std::nullopt;
    };
    ownIo.readText = [&](const std::string& p) -> std::optional<std::string> {
        if (p == ownSheet.atlas) return sheetOut.atlas.xml;
        return std::nullopt;
    };
    int verified = 0;
    std::string failed;
    for (Engine target : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        ExportOptions options;
        options.target = target;
        options.role = ExportRole::NoteType;
        options.name = "bomba";
        options.noteType = "Bomba";
        options.hud = false;
        ExportPackage package = buildExport(own, options, ownIo);
        const fs::path scratch = base / ("verificar-" + std::string(engineKey(target)));
        if (!package.hasErrors() && verifyExport(package, scratch) && package.verifyErrors == 0) ++verified;
        else failed += std::string(" ") + engineKey(target);
    }
    expect(verified == 3, "aspecto propio: se exporta como tipo de nota a Codename, Psych y V-Slice y se relee sin errores" + failed);

    // Crear HUD con piezas dibujadas en el editor de sprites: de la flecha de
    // la izquierda salen notas y receptores de las cuatro direcciones.
    Image drawn = blankImage(160, 160);
    for (int y = 40; y < 120; ++y)
        for (int x = 30; x < 130; ++x) {
            std::uint8_t* p = drawn.at(x, y);
            p[0] = 194; p[1] = 75; p[2] = 153; p[3] = 255;
        }
    auto only = [](DrawnPiece piece, std::vector<Image> frames) {
        DrawnPieces pieces;
        pieces[static_cast<size_t>(piece)] = std::move(frames);
        return pieces;
    };
    ArrowRecipe hud;
    arrowPreset("neon", hud.colors);
    const Image hudSheet = drawnPieceSheets(only(DrawnPiece::Note, {drawn}), hud).arrows;
    constexpr int cell = kDrawnArrowCell;
    const std::uint8_t* downNote = hudSheet.at(cell + cell / 2, cell / 2);
    const std::uint8_t* leftHit = hudSheet.at(40, 3 * cell + cell / 2);
    expect(hudSheet.w == 4 * cell && hudSheet.h == 4 * cell && downNote[3] == 255 && downNote[1] > 150 && downNote[0] < 100 &&
               hudSheet.at(40, cell / 2)[3] == 0 && leftHit[3] > 0,
           "HUD dibujado: la nota de abajo sale del dibujo tenida de su color, y el acierto brilla alrededor");
    hud.strums = StrumLook::Clear;
    hud.drawnTint = false;
    const Image plainSheet = drawnPieceSheets(only(DrawnPiece::Note, {drawn}), hud).arrows;
    const std::uint8_t* plainNote = plainSheet.at(cell / 2, cell / 2);
    const std::uint8_t* clearStrum = plainSheet.at(cell / 2, cell + cell / 2);
    expect(plainNote[0] == 194 && plainNote[1] == 75 && plainNote[2] == 153 && clearStrum[3] > 100 && clearStrum[3] < 130,
           "HUD dibujado: sin tenir conserva sus colores, y los receptores transparentes van a medias");
    // Un dibujo de rojo puro con colores pastel: sale pastel (no un azul puro)
    // y sus receptores grises quedan grises.
    const Image pureRed = squareImage(160, 30, 255, 0, 0);
    ArrowRecipe soft;
    arrowPreset("pastel", soft.colors);
    const Image softSheet = drawnPieceSheets(only(DrawnPiece::Note, {pureRed}), soft).arrows;
    const std::uint8_t* softDown = softSheet.at(cell + cell / 2, cell / 2);
    const std::uint8_t* grayDown = softSheet.at(cell + cell / 2, cell + cell / 2);
    expect(softDown[2] > softDown[0] && softDown[0] > 60 && std::abs(grayDown[0] - grayDown[2]) < 40 && std::abs(grayDown[0] - grayDown[1]) < 40,
           "HUD dibujado: un dibujo de colores puros toma el color elegido y sus receptores grises quedan grises");

    // Todas las piezas: nota animada (2 fotogramas), receptor, sostenido y
    // una salpicadura de un fotograma, que se anima sola.
    DrawnPieces all;
    all[static_cast<size_t>(DrawnPiece::Note)] = {drawn, rotateForDirection(drawn, 3)};
    all[static_cast<size_t>(DrawnPiece::Strum)] = {squareImage(160, 20, 200, 200, 200)};
    all[static_cast<size_t>(DrawnPiece::HoldPiece)] = {squareImage(64, 16, 255, 255, 255)};
    all[static_cast<size_t>(DrawnPiece::HoldEnd)] = {squareImage(64, 20, 255, 255, 255)};
    all[static_cast<size_t>(DrawnPiece::Splash)] = {squareImage(200, 40, 255, 255, 255)};
    ArrowRecipe full;
    const DrawnSheets fullSheets = drawnPieceSheets(all, full);
    const int splashCell = static_cast<int>(std::lround(200 * 1.28f));
    expect(fullSheets.arrows.h == 5 * cell && fullSheets.holds.w == 4 * 64 && fullSheets.holds.h == 2 * 64 &&
               fullSheets.splashes.w == 4 * splashCell && fullSheets.splashes.h == kDrawnSplashFrames * splashCell,
           "HUD dibujado: rejillas en vivo con una fila por fotograma (nota animada, sostenido y salpicadura animada)");
    const DrawnAtlas fullAtlas = drawnPieceAtlas(all, full, "mis-notas.png");
    std::set<std::string> prefixes;
    for (const PartBinding& b : fullAtlas.parts) prefixes.insert(b.animation.prefix);
    expect(fullAtlas.atlas.ok && fullAtlas.parts.size() == 28 && prefixes.count("purple0") && prefixes.count("arrowLEFT") &&
               prefixes.count("left confirm") && prefixes.count("pruple end hold") && prefixes.count("note splash red 1") &&
               contains(fullAtlas.atlas.xml, "purple00001") && contains(fullAtlas.atlas.xml, "note splash purple 10004"),
           "HUD dibujado: un solo atlas con todas las piezas y los nombres que buscan los motores");

    // Los receptores a mano: el de reposo, el pulsado y un acierto de tres
    // fotogramas; sin el pulsado, sale del de reposo.
    DrawnPieces strums;
    strums[static_cast<size_t>(DrawnPiece::Strum)] = {squareImage(160, 20, 200, 200, 200)};
    strums[static_cast<size_t>(DrawnPiece::StrumConfirm)] = {squareImage(160, 10, 255, 0, 0), squareImage(160, 20, 255, 0, 0), squareImage(160, 30, 255, 0, 0)};
    const DrawnAtlas strumAtlas = drawnPieceAtlas(strums, ArrowRecipe{}, "receptores.png");
    NoteStyle strumStyle;
    bindDrawnPieces(strumStyle, strums, ArrowRecipe{}, {"notelab-live/hud/arrows.png", "", ""}, 0.7f);
    size_t confirmFrames = 0, pressFrames = 0;
    for (const PartBinding& b : strumStyle.parts) {
        if (b.part == Part::StrumConfirm && b.direction == 2) confirmFrames = b.animation.indices.size();
        if (b.part == Part::StrumPress && b.direction == 2) pressFrames = b.animation.indices.size();
    }
    expect(strumAtlas.atlas.ok && contains(strumAtlas.atlas.xml, "up confirm0002") && contains(strumAtlas.atlas.xml, "up press0000") &&
               !contains(strumAtlas.atlas.xml, "up confirm0003") && confirmFrames == 3 && pressFrames == 1,
           "HUD dibujado: receptor en reposo, pulsado y al acertar se pueden dibujar aparte (y animar); lo que no, sale del de reposo");

    // Un skin de partida con pixeles de verdad (el de arriba es un PNG de
    // relleno): sus sostenidos tienen que salir en el paquete.
    std::vector<NamedFrame> baseFrames;
    for (const std::string& name : baseGameFrames(true, true))
        baseFrames.push_back({name, squareImage(48, 4, static_cast<std::uint8_t>(40 + baseFrames.size() * 7), 120, 200)});
    const SparrowOut basePacked = packSparrow(baseFrames, "default.png");
    const fs::path hudMod = base / "hud-dibujado";
    writeBytes(hudMod / "images/game/notes/default.png", basePacked.png);
    writeFile(hudMod / "images/game/notes/default.xml", basePacked.xml);
    writeFile(hudMod / "data/global.hx", "function create() {}");
    Scan hudScan;
    scan(hudScan, {hudMod});
    const NoteStyle* hudBase = styleById(hudScan, "codename:game/notes/default");
    // En vivo: las rejillas como hojas; solo el sostenido dibujado.
    NoteStyle liveStyle = hudBase ? *hudBase : NoteStyle{};
    bindDrawnPieces(liveStyle, only(DrawnPiece::HoldPiece, {squareImage(64, 16, 255, 255, 255)}), ArrowRecipe{},
                    {"", "notelab-live/hud/holds.png", ""}, 0.7f);
    int liveHolds = 0, liveNotes = 0;
    for (const PartBinding& b : liveStyle.parts) {
        if (b.part == Part::HoldPiece && b.sheet == static_cast<int>(liveStyle.sheets.size()) - 1) ++liveHolds;
        if (b.part == Part::Note && b.sheet == 0) ++liveNotes;
    }
    expect(hudBase && liveHolds == 4 && liveNotes == 4 && liveStyle.sheets.back().kind == SheetKind::Grid,
           "HUD dibujado: dibujar solo el tramo del sostenido cambia solo ese; las notas siguen en el estilo");
    // Al crear: el atlas montado como una hoja mas.
    NoteStyle hudStyle = hudBase ? *hudBase : NoteStyle{};
    hudStyle.id = "notelab:hud1:codename:game/notes/default";
    const std::string drawnPng = "notelab-import/2/mis-notas.png", drawnXml = "notelab-import/2/mis-notas.xml";
    bindDrawnAtlas(hudStyle, fullAtlas, drawnPng, drawnXml, 0.7f);
    int onDrawn = 0;
    for (const PartBinding& b : hudStyle.parts)
        if (b.sheet == static_cast<int>(hudStyle.sheets.size()) - 1) ++onDrawn;
    expect(onDrawn == 28 && hudStyle.sheets.back().kind == SheetKind::Sparrow, "HUD dibujado: al crearlo, todas sus piezas salen del atlas dibujado");
    ExportIo hudIo;
    hudIo.readBytes = [&](const std::string& p) -> std::optional<std::vector<unsigned char>> {
        if (p == drawnPng) return fullAtlas.atlas.png;
        if (p == drawnXml) return std::vector<unsigned char>(fullAtlas.atlas.xml.begin(), fullAtlas.atlas.xml.end());
        return hudScan.vfs.readBytes(p, 64u * 1024u * 1024u);
    };
    hudIo.readText = [&](const std::string& p) -> std::optional<std::string> {
        if (p == drawnXml) return fullAtlas.atlas.xml;
        return hudScan.vfs.readText(p);
    };
    // El archivo con todos los assets de las notas: el estilo entero en un atlas.
    int sheetPieces = 0;
    const SparrowOut whole = styleSheetAtlas(hudStyle, hudIo, "mis-notas", &sheetPieces);
    const std::vector<Image> splashFrames = stylePieceFrames(hudStyle, Part::Splash, 0, hudIo);
    expect(whole.ok && sheetPieces == 28 && contains(whole.xml, "arrowLEFT0000") && contains(whole.xml, "purple hold piece0000") &&
               contains(whole.xml, "note splash purple 10004") && splashFrames.size() == static_cast<size_t>(kDrawnSplashFrames),
           "el estilo entero sale en un solo archivo de hoja (PNG + XML) y sus piezas se pueden volver a cargar");
    int hudVerified = 0;
    std::string hudFailed;
    for (Engine target : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        ExportOptions options;
        options.target = target;
        // V-Slice no tiene skin de todo el mod: va como el de una cancion.
        options.role = exportRoleAvailable(target, ExportRole::ModSkin) ? ExportRole::ModSkin : ExportRole::SongSkin;
        options.name = "mis-notas";
        options.hud = false;
        ExportPackage package = buildExport(hudStyle, options, hudIo);
        if (target == Engine::VSlice) {
            // El acierto dibujado es un fotograma: V-Slice necesita dos para que
            // el receptor vuelva a reposo (StrumlineNote.hx:143).
            std::string strums;
            for (const ExportFile& f : package.files)
                if (f.path == "images/notelab/mis-notas/strums.xml") strums.assign(f.bytes.begin(), f.bytes.end());
            DiagnosticSink sink;
            const Result<SparrowAtlas> parsed = parseSparrowAtlas(strums, "strums.xml", sink);
            int moving = 0;
            for (const char* title : {"Left", "Down", "Up", "Right"}) {
                if (!parsed) break;
                const std::vector<size_t> frames = parsed.value().framesFor(std::string("confirm") + title);
                const std::vector<size_t> still = parsed.value().framesFor(std::string("static") + title);
                if (frames.size() == 2 && still.size() == 1 && parsed.value().frames[frames[0]].x == parsed.value().frames[frames[1]].x &&
                    parsed.value().frames[frames[0]].y == parsed.value().frames[frames[1]].y)
                    ++moving;
            }
            expect(moving == 4, "V-Slice: el acierto de un fotograma sale con dos (misma region) para que el receptor vuelva a reposo");
        }
        const fs::path scratch = base / ("verificar-dibujado-" + std::string(engineKey(target)));
        if (!package.hasErrors() && verifyExport(package, scratch) && package.verifyErrors == 0) {
            ++hudVerified;
            continue;
        }
        hudFailed += std::string(" ") + engineKey(target) + " (verify " + std::to_string(package.verifyErrors);
        for (const ExportNote& note : package.notes)
            if (note.severity == Severity::Error) hudFailed += ", " + note.code + " " + note.subject + " " + note.detail;
        hudFailed += ")";
    }
    expect(hudVerified == 3, "HUD dibujado: se exporta como skin a Codename, Psych y V-Slice y se relee sin errores" + hudFailed);
    // Recursos: los sonidos de canciones no se mezclan con los efectos.
    expect(soundUseOf("content/Extras/songs/Total Bravery/song/Voices.ogg") == SoundUse::Song &&
               soundSongOf("content/Extras/songs/Total Bravery/song/Voices.ogg") == "Total Bravery" &&
               soundUseOf("songs/bopeebo/Inst.ogg") == SoundUse::Song && soundUseOf("assets/songs/tutorial/Voices-bf.ogg") == SoundUse::Song &&
               soundUseOf("mods/x/songs/fresh/Inst-erect.ogg") == SoundUse::Song && soundUseOf("music/freakyMenu.ogg") == SoundUse::Music &&
               soundUseOf("assets/shared/sounds/hitsound.ogg") == SoundUse::Effect && soundUseOf("sounds/intro3.ogg") == SoundUse::Effect &&
               soundSongOf("sounds/intro3.ogg").empty(),
           "recursos: canciones (Inst/Voices bajo songs/, en los tres motores), musica y efectos se separan por la ruta");
    // Pasar el script de un tipo a bloques, lo que se pueda.
    const std::string luaScript =
        "function onCreate()\n"
        "  for i = 0, getProperty('unspawnNotes.length')-1 do\n"
        "    if getPropertyFromGroup('unspawnNotes', i, 'noteType') == 'Bomba' then\n"
        "      setPropertyFromGroup('unspawnNotes', i, 'texture', 'BOMB_assets');\n"
        "      setPropertyFromGroup('unspawnNotes', i, 'hitHealth', '-0.3');\n"
        "      setPropertyFromGroup('unspawnNotes', i, 'ignoreNote', true);\n"
        "    end\n"
        "  end\n"
        "end\n"
        "function goodNoteHit(id, noteData, noteType, isSustainNote)\n"
        "  if noteType == 'Bomba' then -- la bomba\n"
        "    addHealth(-0.2)\n"
        "    playSound('explosion', 0.6)\n"
        "    cameraShake('game', 0.03, 0.25)\n"
        "    doTweenAlpha('fade', 'camHUD', 0.5, 0.2)\n"
        "  end\n"
        "end\n"
        "function onUpdate(elapsed)\n"
        "  local x = 1\n"
        "end\n";
    const ScriptImport imported = importNoteScript(Engine::Psych, "Bomba", luaScript);
    std::string importedText;
    for (const BlockFile& file : generateBlocks(Engine::Psych, "Bomba", "Bomba", imported.program).files) importedText += file.text;
    expect(imported.blocks == 5 && imported.kept == 1 && imported.notes.size() == 2 && contains(importedText, "addHealth(-0.2)") &&
               contains(importedText, "explosion") && contains(importedText, "doTweenAlpha('fade', 'camHUD', 0.5, 0.2)"),
           "script a bloques (Psych): propiedades, vida, sonido y temblor pasan a bloques; lo demas queda como codigo y se avisa");
    const ScriptImport hx = importNoteScript(Engine::Codename, "Bomba",
        "function onPlayerHit(event) {\n    health -= 0.1;\n    FlxG.camera.shake(0.01, 0.1);\n    boyfriend.playAnim('hey', true);\n}\n");
    std::string hxText;
    for (const BlockFile& file : generateBlocks(Engine::Codename, "Bomba", "Bomba", hx.program).files) hxText += file.text;
    expect(hx.blocks == 3 && hx.kept == 0 && contains(hxText, "health += -0.1"),
           "script a bloques (Codename): vida, temblor y «Hey!» pasan a bloques");
    // V-Slice: los metodos de una clase NoteKind, con llaves en otra linea y
    // las guardas que el bloque de evento ya hace.
    const ScriptImport vslice = importNoteScript(Engine::VSlice, "Bomba",
        "import funkin.play.notes.notekind.NoteKind;\n"
        "class BombaNoteKind extends NoteKind\n"
        "{\n"
        "  public function new()\n"
        "  {\n"
        "    super(\"Bomba\", \"Bomba\", null, null, false, null);\n"
        "  }\n"
        "  public override function onNoteHit(event:HitNoteScriptEvent):Void\n"
        "  {\n"
        "    if (event.eventCanceled) return;\n"
        "    PlayState.instance.health += 0.1;\n"
        "  }\n"
        "}\n");
    const ScriptImport guarded = importNoteScript(Engine::Psych, "Bomba",
        "function goodNoteHit(id, direction, noteType, isSustainNote)\n"
        "\tif noteType ~= 'Bomba' or isSustainNote then return end\n"
        "\taddScore(50)\n"
        "end\n");
    expect(vslice.blocks == 1 && vslice.kept == 0 && guarded.blocks == 1 && guarded.kept == 0,
           "script a bloques: metodos de V-Slice con llaves en otra linea, y las guardas del tipo se quitan (las hace el evento)");
    // El archivo del editor de sprites: pestanas con sus capas, tal cual.
    SpriteFile sprite;
    sprite.meta = "{\"target\":1,\"fps\":[12,24]}";
    SpriteFileDoc general;
    general.meta = "{\"name\":\"Hoja general\",\"hud\":true}";
    general.layers.push_back({"Base", squareImage(64, 4, 255, 0, 0), true, 1.0f});
    general.layers.push_back({"Dibujo", squareImage(64, 20, 0, 0, 255), false, 0.5f});
    SpriteFileDoc loose;
    loose.meta = "{\"name\":\"Dibujo suelto\",\"single\":true}";
    loose.layers.push_back({"Capa 1", squareImage(32, 2, 0, 255, 0), true, 1.0f});
    sprite.docs = {general, loose};
    SpriteFile spriteBack;
    const bool spriteRead = readSpriteFile(writeSpriteFile(sprite), spriteBack);
    expect(spriteRead && spriteBack.docs.size() == 2 && spriteBack.docs[0].layers.size() == 2 && !spriteBack.docs[0].layers[1].visible &&
               std::fabs(spriteBack.docs[0].layers[1].opacity - 0.5f) < 0.001f && spriteBack.docs[0].layers[0].image.w == 64 &&
               spriteBack.docs[0].layers[0].image.at(10, 10)[0] == 255 && contains(spriteBack.meta, "\"target\":1") &&
               contains(spriteBack.docs[1].meta, "Dibujo suelto") && !readSpriteFile(std::vector<unsigned char>{1, 2, 3}, spriteBack),
           "archivo .nlsprite: pestanas, capas (nombre, visibilidad, opacidad, pixeles) y ajustes se guardan y se vuelven a abrir");
}

void layoutCase(const fs::path& base) {
    std::printf("\n== Abrir: instalaciones con mods, paquetes y hojas de un script ==\n");
    // La build de un mod: su propio ejecutable, el motor y dos mods.
    const fs::path build = base / "build";
    writeFile(build / "Mi Mod.exe", "MZ");
    writeSheet(build / "assets/images/game/notes/default", baseGameFrames(true, true));
    writeFile(build / "mods/Mi Mod/data/global.hx", "function create() {}");
    writeFile(build / "mods/Otro/songs/x/charts/hard.json", "{}");
    writeFile(build / "mods/vacio/leeme.txt", "nada");
    writeFile(build / "mods/images/characters/leeme.txt", "la capa global de Psych");
    const OpenLayout first = openLayoutOf(build);
    expect(first.mod == build / "mods" / "Mi Mod" && first.install == build && first.mods.size() == 2,
           "la build de un mod se abre por su primer mod, con la instalacion debajo (ni una carpeta sin nada de mod ni "
           "mods/images, que en Psych es contenido, cuentan como mod: Mods.hx:16-32)");
    const OpenLayout other = openLayoutOf(build, "Otro");
    expect(other.mod == build / "mods" / "Otro" && other.install == build, "abrir otro mod de la instalacion por su nombre");
    const OpenLayout inside = openLayoutOf(build / "mods" / "Otro");
    expect(inside.mod == build / "mods" / "Otro" && inside.install == build && inside.mods.size() == 2,
           "un mod abierto desde dentro de mods/ sabe su instalacion");
    // El motor tal cual es el juego base: se abre el, con sus mods a mano.
    const fs::path official = base / "official";
    writeFile(official / "CodenameEngine.exe", "MZ");
    writeSheet(official / "assets/images/game/notes/default", baseGameFrames(true, true));
    writeFile(official / "mods/Prueba/data/global.hx", "");
    const OpenLayout engine = openLayoutOf(official);
    expect(engine.mod == official && engine.install.empty() && engine.mods.size() == 1,
           "una instalacion oficial se abre como juego base y sus mods quedan a mano");
    expect(openLayoutOf(official, "Prueba").mod == official / "mods" / "Prueba", "...y uno de sus mods, si se pide");
    // Paquetes content/ por order.txt (el primero manda) y addons.
    const fs::path mod = build / "mods" / "Mi Mod";
    writeFile(mod / "content/order.txt", "\xEF\xBB\xBF" "B\r\nA\r\n# comentario\r\n");
    for (const char* pack : {"A", "B", "C"}) writeFile(mod / "content" / pack / "data/x.txt", pack);
    writeFile(build / "addons/[HIGH] Arriba/data/x.txt", "h");
    writeFile(build / "addons/Normal/data/x.txt", "n");
    writeFile(mod / "addons/[LOW] Abajo/data/x.txt", "l");
    const OpenLayout packs = openLayoutOf(build);
    std::string order;
    for (const fs::path& p : packs.packs) order += p.filename().u8string() + "|";
    expect(order == "[HIGH] Arriba|B|A|C|Normal|" && packs.lowPacks.size() == 1,
           "paquetes: [HIGH], content/ en el orden de order.txt y el resto, y addons; [LOW] debajo del mod (" + order + ")");

    // Codename: un script general elige la hoja con una ruta hecha.
    const fs::path voiid = base / "scriptsprite";
    writeSheet(voiid / "images/game/notes/default", baseGameFrames(true, true));
    writeFile(voiid / "songs/x/charts/hard.json", "{\"strumLines\":[],\"noteTypes\":[\"Punch\",\"Blade\",\"Fantasma\"]}");
    writeFile(voiid / "songs/tipos.hx",
              "function onNoteCreation(event) {\n"
              "    var data = noteTypeData.get(event.noteType);\n"
              "    if (data != null) event.noteSprite = \"game/custom/notes/\" + data.skin;\n"
              "}\n");
    writeSheet(voiid / "images/game/custom/notes/Punch", baseGameFrames(false, false));
    writeSheet(voiid / "images/game/custom/notes/BladeAlt", baseGameFrames(false, false));
    // Una hoja con sus propios nombres: la anima otro script, no se toma.
    writeFile(voiid / "songs/x/charts/easy.json", "{\"noteTypes\":[\"Raro\"]}");
    writeSheet(voiid / "images/game/custom/notes/Raro", {"bala izquierda0000", "bala abajo0000"});
    writeSheet(voiid / "images/game/custom/notes/BladeAltOld", baseGameFrames(false, false));
    Scan s;
    scan(s, {voiid});
    const StyleReport* punchReport = nullptr;
    const NoteStyle* punch = styleById(s, "codename:script:Punch", &punchReport);
    const NoteStyle* blade = styleById(s, "codename:script:Blade");
    expect(punch && punch->use == StyleUse::NoteType && punch->sheets[0].declared == "game/custom/notes/Punch" &&
           punch->nameScript == "songs/tipos.hx",
           "la hoja del tipo, en la carpeta que pone el script, por su nombre");
    expect(blade && blade->sheets[0].declared == "game/custom/notes/BladeAlt",
           "sin una con su nombre, la mas corta que empieza por el (BladeAlt, no BladeAltOld)");
    expect(punchReport && countErrors(*punchReport) == 0 && countCode(*punchReport, "FML-NOTE-026") == 1,
           "se dice que la eligio el nombre, sin errores (FML-NOTE-026)");
    const std::vector<NoteTypeEntry> types = scanNoteTypes(s.vfs, s.catalog);
    const NoteTypeEntry* ghost = findNoteType(types, "Fantasma");
    const NoteTypeEntry* punchType = findNoteType(types, "punch");
    expect(ghost && ghost->lookStyle.empty() && punchType && punchType->lookStyle == "codename:script:Punch",
           "los tipos que pide un chart salen en el catalogo, con su hoja si se encontro");
    const NoteTypeEntry* odd = findNoteType(types, "Raro");
    expect(!styleById(s, "codename:script:Raro") && odd && odd->lookStyle.empty(),
           "una hoja con nombres que no se saben leer no se toma: el tipo sigue con el skin, sin errores");

    // Multikey: el mod declara los nombres de 4 teclas y sus hojas los usan.
    writeFile(voiid / "data/multikeyData.xml",
              "<!DOCTYPE multikey-data>\n<keyData>\n"
              "<keyGroup><key note=\"square0\" noteHold=\"square hold0\" noteHoldEnd=\"square hold end0\"/></keyGroup>\n"
              "<keyGroup>"
              "<key note=\"left0\" noteHold=\"left hold0\" noteHoldEnd=\"left hold end0\" strumStatic=\"left static\"/>"
              "<key note=\"down0\" noteHold=\"down hold0\" noteHoldEnd=\"down hold end0\"/>"
              "<key note=\"up0\" noteHold=\"up hold0\" noteHoldEnd=\"up hold end0\"/>"
              "<key note=\"right0\" noteHold=\"right hold0\" noteHoldEnd=\"right hold end0\"/>"
              "</keyGroup>\n</keyData>\n");
    writeSheet(voiid / "images/game/custom/notes/Punch",
               {"left0000", "left hold0000", "left hold end0000", "down0000", "down hold0000", "down hold end0000",
                "up0000", "up hold0000", "up hold end0000", "right0000", "right hold0000", "right hold end0000"});
    Scan mk;
    scan(mk, {voiid});
    const StyleReport* mkReport = nullptr;
    const NoteStyle* mkPunch = styleById(mk, "codename:script:Punch", &mkReport);
    expect(mkPunch && mkPunch->multikeyData == "data/multikeyData.xml" && mkReport && countErrors(*mkReport) == 0 &&
           countCode(*mkReport, "FML-NOTE-027") == 1 && mkReport->partsResolved == 12,
           "multikey: la hoja con los nombres de 4 teclas del mod se lee entera, sin errores (FML-NOTE-027)");

    // Una carpeta que solo envuelve a otra se abre por dentro.
    const fs::path wrapper = base / "envoltorio";
    writeFile(wrapper / "Mi Mod Build/Mi Mod.exe", "MZ");
    writeSheet(wrapper / "Mi Mod Build/assets/images/game/notes/default", baseGameFrames(true, true));
    writeFile(wrapper / "Mi Mod Build/mods/Mi Mod/data/global.hx", "");
    const OpenLayout wrapped = openLayoutOf(wrapper);
    expect(wrapped.mod == wrapper / "Mi Mod Build" / "mods" / "Mi Mod" && wrapped.install == wrapper / "Mi Mod Build",
           "una carpeta que solo contiene la build se abre por dentro");
    writeFile(base / "solo-imagenes/images/x.png", "PNG");
    expect(openLayoutOf(base / "solo-imagenes").mod == base / "solo-imagenes",
           "un mod con una sola carpeta (images/) no se desenvuelve");

    NoteProject project;
    ProjectSource one;
    one.path = "C:/mods/build";
    one.mod = "Otro";
    ProjectSource two;
    two.path = "C:/mods/build";
    two.baseOnly = true;
    project.sources = {one, two};
    std::string error;
    const auto back = readProject(writeProject(project), error);
    expect(back && back->sources.size() == 2 && back->sources[0].mod == "Otro" && !back->sources[0].baseOnly &&
           back->sources[1].mod.empty() && back->sources[1].baseOnly,
           "el proyecto guarda que mod de la instalacion se abrio, o que se abrio como juego base");
    NoteProject looks;
    ProjectSource withLooks;
    withLooks.path = "C:/mods/x";
    withLooks.typeLooks = {{"Bomba", "notelab:look:Bomba:1"}, {"Veneno Ñ", "notelab:look:Veneno Ñ:2"}};
    looks.sources = {withLooks};
    const auto looksBack = readProject(writeProject(looks), error);
    expect(looksBack && looksBack->sources.size() == 1 && looksBack->sources[0].typeLooks == withLooks.typeLooks,
           "el proyecto guarda el aspecto que se le dio a cada tipo (tambien con acentos)");
}

// --create-samples=<carpeta>: lo que generan NoteCreate (juicios, cifras y
// marcas de serie) como PNG, para mirarlo.
int writeCreateSamples(const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const std::vector<unsigned char> black = readAll("C:/Windows/Fonts/seguibl.ttf");
    const std::vector<unsigned char> symbols = readAll("C:/Windows/Fonts/seguisym.ttf");
    auto save = [&](const std::string& name, const Image& image) {
        const std::vector<unsigned char> png = encodePng(image);
        std::ofstream out(folder / (name + ".png"), std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        std::printf("%s %dx%d\n", name.c_str(), image.w, image.h);
    };
    struct Rating { const char* name; const char* text; std::uint32_t top, bottom; };
    const Rating ratings[] = {{"sick", "SICK!!", 0xFF78F0FFu, 0xFF2878FFu}, {"good", "GOOD", 0xFF8CFF8Cu, 0xFF28AA46u},
                              {"bad", "BAD", 0xFFFFC86Eu, 0xFFF07828u}, {"shit", "SHIT", 0xFFC8A078u, 0xFF785032u},
                              {"combo", "COMBO", 0xFFFFFFFFu, 0xFFC8C8E6u}};
    for (const Rating& r : ratings) {
        TextLook look;
        look.size = 120.0f;
        look.top = r.top;
        look.bottom = r.bottom;
        look.outline = 0xFF141432u;
        look.outlineWidth = 13.0f;
        look.shadow = true;
        look.tiltDeg = -6.0f;
        save(r.name, renderText(black, r.text, look));
        look.pixel = true;
        look.size = 40.0f;
        look.outlineWidth = 4.0f;
        look.shadowX = 3.0f;
        look.shadowY = 3.0f;
        save(std::string(r.name) + "-pixel", renderText(black, r.text, look));
    }
    for (int n = 0; n < 10; ++n) {
        TextLook look;
        look.size = 110.0f;
        look.top = 0xFFFFFFFFu;
        look.bottom = 0xFFC8C8E6u;
        look.outline = 0xFF141432u;
        look.outlineWidth = 12.0f;
        look.shadow = true;
        save("num" + std::to_string(n), renderText(black, std::to_string(n), look));
    }
    for (int m = 0; m < kBuiltinMarkCount; ++m)
        save(std::string("mark-") + builtinMarkKey(static_cast<BuiltinMark>(m)), builtinMarkImage(static_cast<BuiltinMark>(m), 96, symbols));
    // Los skins por defecto del disco pintados con un preset y con el aspecto
    // «veneno», hoja entera, para mirarlos.
    struct Real { const char* name; const char* root; const char* id; };
    const Real reals[] = {
        {"codename", std::getenv("FML_NOTE_SAMPLE_CODENAME"), "codename:assets/game/notes/default"},
        {"psych07", std::getenv("FML_NOTE_SAMPLE_PSYCH"), "psych:assets/shared/noteSkins/NOTE_assets"},
        {"vslice", std::getenv("FML_NOTE_SAMPLE_VSLICE"), "vslice:assets/funkin"},
    };
    const Image skull = builtinMarkImage(BuiltinMark::Skull, 96, symbols);
    for (const Real& real : reals) {
        if (!real.root || !*real.root) continue;
        std::error_code exists;
        if (!fs::exists(fs::u8path(real.root), exists)) continue;
        Scan s;
        scan(s, {fs::u8path(real.root)});
        const NoteStyle* style = styleById(s, real.id);
        if (!style) {
            std::printf("%s: no esta %s\n", real.name, real.id);
            continue;
        }
        ExportIo io;
        io.readBytes = [&](const std::string& p) { return s.vfs.readBytes(p, 64u * 1024u * 1024u); };
        io.readText = [&](const std::string& p) { return s.vfs.readText(p); };
        ArrowRecipe pastel;
        arrowPreset("pastel", pastel.colors);
        for (const PaintedSheet& sheet : paintStyleSheets(*style, [&](Part part, int d) { return arrowPaint(pastel, part, d); }, io)) {
            Image image;
            if (decodePng(sheet.png, image)) save(std::string(real.name) + "-pastel-" + fs::u8path(sheet.image).parent_path().filename().u8string() + "-" + fs::u8path(sheet.image).stem().u8string(), image);
        }
        TypeLookRecipe poison;
        typeLookPreset("poison", poison);
        for (const PaintedSheet& sheet : paintStyleSheets(*style, [&](Part part, int d) { return typePaint(poison, part, d, &skull); }, io)) {
            Image image;
            if (decodePng(sheet.png, image)) save(std::string(real.name) + "-poison-" + fs::u8path(sheet.image).parent_path().filename().u8string() + "-" + fs::u8path(sheet.image).stem().u8string(), image);
        }
    }
    return 0;
}

void customCreatorCase(const fs::path& base) {
    using json = nlohmann::json;
    std::printf("\n== Creador avanzado, bot y charts ==\n");
    const fs::path folder = base / "creator";
    Image pixels = blankImage(48, 16);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 48; ++x) {
        auto* p = pixels.at(x, y); p[(x / 16) % 3] = 255; p[3] = 255;
    }
    writeBytes(folder / "custom.png", encodePng(pixels));
    auto items = scanImport({folder / "custom.png"});
    CustomResource r; r.input = items.front(); r.cellWidth = r.cellHeight = 16;
    const auto view = inspectCustomResource(r, 0);
    expect(view.error.empty() && view.frames.size() == 3 && view.boxes[2].x == 32 && near(view.frames[1].at(5, 5), 0, 255, 0), "custom: grid 16x16 y pixeles reales antes de importar");
    r.regions = {{4, 2, 8, 9}};
    expect(inspectCustomResource(r, 0).frames.front().w == 8, "custom: recorte manual tiene prioridad sobre grid");
    r.regions = {{45, 0, 16, 16}};
    expect(!inspectCustomResource(r, 0).error.empty(), "custom: bloquea recortes fuera de la imagen");
    r.regions.clear();
    std::vector<int> order; std::string error;
    expect(parseCustomOrder("3, 1, 2, 2", 3, order, error) && order == std::vector<int>({2, 0, 1, 1}), "custom: orden editable y frames repetidos");
    expect(!parseCustomOrder("0, 4", 3, order, error) && order.empty(), "custom: no acepta indices fuera del rango");
    expect(parseCustomOrder("", 3, order, error) && order.empty(), "custom: vacio recupera todos los frames");
    writeFile(folder / "otro-nombre.xml", R"(<TextureAtlas imagePath="otra-imagen.png"><SubTexture name="manual0000" x="0" y="0" width="16" height="16"/><SubTexture name="manual0001" x="16" y="0" width="16" height="16"/></TextureAtlas>)");
    CustomResource linked = r;
    expect(bindCustomAtlas(linked, folder / "otro-nombre.xml", error) && linked.input.kind == ImportKind::Atlas && linked.cellWidth == 0 && linked.input.animations.size() == 1,
           "custom: asocia XML con otro nombre al PNG seleccionado sin imponer plantilla");
    const auto linkedView = inspectCustomResource(linked, 0);
    expect(linkedView.error.empty() && linkedView.frames.size() == 2 && near(linkedView.frames[1].at(5, 5), 0, 255, 0), "custom: XML manual resuelve los mismos pixeles de la preview");
    expect(!bindCustomAtlas(linked, folder / "missing.xml", error) && linked.input.atlas == folder / "otro-nombre.xml", "custom: metadata faltante no destruye la asociacion anterior");
    CustomRecipe recipe; recipe.resources.push_back(r);
    for (int part = 0; part < kPartCount; ++part) for (int d = 0; d < 4; ++d) {
        CustomAssignment a; a.resource = 0; a.part = static_cast<Part>(part); a.direction = d; a.order = {2, 0, 1, 1}; a.fps = 18; a.loop = true; a.pixel = true;
        recipe.assignments.push_back(a);
    }
    for (int h = 0; h < 19; ++h) {
        CustomAssignment a; a.resource = 0; a.role = CustomRole::HudImage; a.hudIndex = h; a.order = {h % 3};
        recipe.assignments.push_back(a);
    }
    std::vector<unsigned char> ogg(40, 0); std::copy_n("OggS", 4, ogg.begin()); std::copy_n("vorbis", 6, ogg.begin() + 29);
    writeBytes(folder / "effect.ogg", ogg);
    CustomResource sound; sound.input = scanImport({folder / "effect.ogg"}).front(); recipe.resources.push_back(sound);
    CustomAssignment s; s.resource = 1; s.role = CustomRole::SoundEffect; s.soundName = "hit"; recipe.assignments.push_back(s);
    s.role = CustomRole::CountdownSound; s.hudIndex = 18; recipe.assignments.push_back(s);
    auto built = buildCustomStyle(recipe, Engine::Codename, "Custom");
    expect(built.ok && built.style.parts.size() == 44 && built.style.hasHud && built.style.sounds.size() == 1 && !built.style.countdown[3].sound.empty(), "custom: todas las piezas, ranking, countdown y banco de efectos sin plantilla");
    expect(built.style.parts.front().animation.fps == 18 && built.style.parts.front().animation.loop && built.style.sheets.front().pixel, "custom: conserva FPS, loop y pixel art");
    std::map<std::string, std::vector<unsigned char>> files;
    for (const auto& f : built.files) files[f.path] = f.bytes;
    ExportIo io;
    io.readBytes = [&](const std::string& path) -> std::optional<std::vector<unsigned char>> { const auto it = files.find(path); return it == files.end() ? std::nullopt : std::optional<std::vector<unsigned char>>(it->second); };
    io.readText = [&](const std::string& path) -> std::optional<std::string> { const auto b = io.readBytes(path); return b ? std::optional<std::string>(std::string(b->begin(), b->end())) : std::nullopt; };
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        ExportOptions o; o.target = engine; o.role = engine == Engine::VSlice ? ExportRole::SongSkin : ExportRole::ModSkin; o.name = "Custom";
        auto package = buildExport(built.style, o, io);
        expect(!package.hasErrors() && verifyExport(package, base / "custom-verify"), std::string("custom: export completo releido en ") + engineKey(engine));
        expect(std::any_of(package.files.begin(), package.files.end(), [](const ExportFile& f) { return f.path == "sounds/notelab/custom/hit.ogg"; }), std::string("custom: efecto con ruta estable en ") + engineKey(engine));
    }
    CustomRecipe bad = recipe; bad.assignments.push_back(bad.assignments.front());
    expect(!buildCustomStyle(bad, Engine::Psych, "Bad").ok, "custom: no duplica asignaciones");
    bad = recipe; bad.assignments.back().resource = 999;
    expect(!buildCustomStyle(bad, Engine::VSlice, "Bad").ok, "custom: no pierde recursos faltantes en silencio");
    bad = recipe; bad.assignments[44].order.clear();
    expect(!buildCustomStyle(bad, Engine::Codename, "Bad").ok, "custom: ranking animado no se reduce al primer frame sin avisar");
    bad = recipe; bad.assignments[0].order = {99};
    expect(!buildCustomStyle(bad, Engine::Codename, "Bad").ok, "custom: valida el orden tambien en el nucleo");
    writeFile(folder / "effect.wav", "RIFFnotanogg"); bad = recipe; bad.resources[1].input.path = folder / "effect.wav";
    expect(!buildCustomStyle(bad, Engine::Codename, "Bad").ok, "custom: WAV no se exporta renombrado como OGG");
    CreationRecipe cr; cr.kind = "custom"; cr.styleId = "notelab:custom/1"; cr.custom = recipe;
    NoteProject project; project.sources.push_back({}); project.sources[0].recipes.push_back(cr);
    project.sources[0].path = folder.u8string();
    project.sources[0].edits.push_back({cr.styleId, built.style, {}, true}); project.sources[0].bots.push_back({"custom", {true, 1, 2}});
    project.distribute.rules.push_back({"custom", 33, 7, 90}); project.distribute.filter.replaceExisting = true;
    const auto restored = readProject(writeProject(project), error);
    expect(restored && writeProject(*restored) == writeProject(project), "custom: proyecto restaura archivos, recetas, frames, audio, bot y cantidades");
    expect(botSkipsWithProfile(false, 1, {true, 1, 2}) && !botSkipsWithProfile(true, 0, {true, 1, 2}) && botSkipsWithProfile(true, 1, {false, 1, 1}), "bot: heredar, tocar, evitar y desactivar por lado");
    std::vector<PreviewNote> notes(1000); std::vector<std::string> types(1000);
    for (int i = 0; i < 1000; ++i) { notes[i].timeMs = i * 100; notes[i].lane = i % 4; notes[i].strumLine = i % 2; }
    DistributeRequest req; req.rules = {{"A", 25, 0}, {"B", 25, 0}};
    const auto spread = distributeTypes(notes, types, req);
    expect(spread.placed == std::vector<int>({250, 250}), "distribucion: cuotas exactas sobre total, no probabilidad residual");
    req.rules = {{"A", 1, 0, 123}};
    expect(distributeTypes(notes, types, req).placed[0] == 123, "distribucion: cantidad exacta independiente del porcentaje");
    req.rules[0].count = 1500;
    const auto shortage = distributeTypes(notes, types, req);
    expect(shortage.placed[0] == 1000 && shortage.requested[0] == 1500, "distribucion: conserva objetivo exacto si faltan notas para avisar al usuario");
    types.assign(1000, "Default Note"); req.rules[0].count = -1; req.rules[0].percent = 100;
    expect(distributeTypes(notes, types, req).placed[0] == 1000, "distribucion: Default Note no bloquea las notas normales");
    types.assign(1000, "ModType");
    expect(distributeTypes(notes, types, req).protectedTypes == 1000, "distribucion: las custom existentes se protegen por defecto");
    req.filter.replaceExisting = true;
    expect(distributeTypes(notes, types, req).placed[0] == 1000, "distribucion: reemplazo de tipos existentes solo si se elige");
    SongChart song; song.format = SongChart::Format::Codename;
    const std::string original = R"({"codenameChart":true,"noteTypes":["Old"],"custom":{"keep":7},"events":[{"time":0,"name":"X","params":[true]}],"strumLines":[{"type":0,"extra":42,"notes":[{"time":100,"id":0,"sLen":0,"type":1,"extra":3}]},{"type":1,"notes":[{"time":200,"id":2,"sLen":20,"type":0}]}]})";
    std::vector<PreviewNote> two(2); two[0].strumLine = 0; two[0].timeMs = 100; two[1].strumLine = 1; two[1].timeMs = 200; two[1].lane = 2; two[1].sustainMs = 20;
    const auto text = patchSongNoteTypes(original, song, two, {"Old", "New"}, error);
    const auto doc = json::parse(text, nullptr, false);
    expect(error.empty() && doc["noteTypes"] == json({"Old", "New"}) && doc["strumLines"][1]["notes"][0]["type"] == 2 && doc["custom"]["keep"] == 7 && doc["strumLines"][0]["notes"][0]["extra"] == 3 && doc["events"][0]["params"][0] == true, "chart: Codename agrega tipos sin perder campos ni eventos");
    fs::path freshBackup;
    expect(saveSongChart(folder / "fresh.json", text, "", false, freshBackup, error) && freshBackup.empty(), "chart: guardar una copia nueva no necesita un destino existente");
    expect(saveSongChart(folder / "fresh-folder/new.json", text, "", false, freshBackup, error) && freshBackup.empty(), "chart: guardar copia crea su nueva carpeta sin alterar el original");
    const auto originalPath = folder / fs::u8path("José-chart.json"); writeFile(originalPath, original); fs::path backup;
    expect(saveSongChart(originalPath, text, original, true, backup, error) && !backup.empty(), "chart: guarda con backup incluso en rutas con acentos");
    std::ifstream savedBackup(backup, std::ios::binary); const std::string old((std::istreambuf_iterator<char>(savedBackup)), {});
    expect(old == original, "chart: backup es exactamente el archivo anterior");
    expect(!saveSongChart(originalPath, text, original, true, backup, error), "chart: bloquea reemplazo si cambio en disco");
    writeFile(folder / "source.zip", "keep-archive");
    expect(!saveSongChart(folder / "source.zip", text, "", false, backup, error), "chart: guardar copia no permite sobrescribir el ZIP con JSON");
    two[1].timeMs = 201;
    expect(patchSongNoteTypes(original, song, two, {"Old", "New"}, error).empty(), "chart: bloquea mapeo de notas distinto"); two[1].timeMs = 200;
    song.format = SongChart::Format::Psych;
    const std::string psych = R"({"song":{"notes":[{"mustHitSection":false,"sectionNotes":[[100,0,0,"Old"]]},{"mustHitSection":true,"sectionNotes":[[200,2,20,"","keep"]]}],"events":[[50,[["X",1,true]]]],"custom":2}})";
    const auto psy = json::parse(patchSongNoteTypes(psych, song, two, {"Old", "New"}, error), nullptr, false);
    expect(error.empty() && psy["song"]["notes"][1]["sectionNotes"][0][3] == "New" && psy["song"]["notes"][1]["sectionNotes"][0][4] == "keep" && psy["song"]["custom"] == 2, "chart: Psych conserva secciones, extras y eventos");
    // Psych 1.0 (`format` psych_v1): el rival en 4..7 aunque la seccion no sea
    // mustHitSection (PlayState.hx:1355). Con la regla clasica no casaba y no
    // se podia guardar; un 8+ es un carril del rival y no se reescribe.
    const std::string psychV1 = R"({"song":{"format":"psych_v1","notes":[{"mustHitSection":false,"sectionNotes":[[100,4,0,"Old"]]},{"mustHitSection":true,"sectionNotes":[[200,2,20,""]]}],"events":[]}})";
    const auto v1 = json::parse(patchSongNoteTypes(psychV1, song, two, {"Old", "New"}, error), nullptr, false);
    expect(error.empty() && v1.is_object() && v1["song"]["notes"][1]["sectionNotes"][0][3] == "New" &&
           v1["song"]["notes"][0]["sectionNotes"][0][1] == 4 && v1["song"]["format"] == "psych_v1",
           "chart: Psych 1.0 (psych_v1) guarda cada tipo en su nota con carriles absolutos");
    const std::string psychV1High = R"({"song":{"format":"psych_v1","notes":[{"mustHitSection":true,"sectionNotes":[[100,8,0,""]]},{"mustHitSection":true,"sectionNotes":[[200,2,20,""]]}],"events":[]}})";
    const auto high = json::parse(patchSongNoteTypes(psychV1High, song, two, {"New", "New"}, error), nullptr, false);
    expect(error.empty() && high.is_object() && high["song"]["notes"][0]["sectionNotes"][0][1] == 8,
           "chart: en psych_v1 un 8+ es del rival y no se reescribe al darle tipo");
    {
        // Leer: el lado de cada nota como lo decide Psych 1.0 (PlayState.hx:1355).
        const std::string absolute = R"({"song":{"song":"Absolute","bpm":120,"format":"psych_v1_convert","player1":"bf","player2":"dad","notes":[{"sectionBeats":4,"mustHitSection":false,"sectionNotes":[[0,0,0],[100,5,0],[200,9,0]]},{"sectionBeats":4,"mustHitSection":true,"sectionNotes":[[2000,1,0,"Hurt Note"],[2100,6,0]]}],"events":[]}})";
        DiagnosticSink sink;
        auto parsed = parseLegacyChart(absolute, "absolute.json", sink);
        auto at = [&](double time) -> const ChartNote* {
            if (!parsed) return nullptr;
            for (const ChartNote& note : parsed.value().notes) if (note.timeMs == time) return &note;
            return nullptr;
        };
        expect(parsed && parsed.value().notes.size() == 5 && at(0) && at(0)->isPlayer && at(100) && !at(100)->isPlayer &&
               at(2000) && at(2000)->isPlayer && at(2100) && !at(2100)->isPlayer,
               "chart: psych_v1 lee 0..3 como jugador y 4..7 como rival, sin mirar mustHitSection");
        expect(at(200) && !at(200)->isPlayer && at(200)->lane == 1 && at(200)->type.empty(),
               "chart: en psych_v1 note[1] >= 8 es un carril del rival, no un tipo empacado");
        // Exportar: ni Psych 0.7 ni Psych 1.0 cambian ningun lado.
        if (parsed) {
            DiagnosticSink exportSink;
            auto exported = exportPsychEngineChart(parsed.value(), "Absolute", exportSink);
            const json out = exported ? json::parse(exported.value().text, nullptr, false) : json();
            const json& body = out.is_object() && out.contains("song") && out["song"].is_object() ? out["song"] : out;
            bool labelled = body.is_object() && body.contains("format") && body["format"].is_string() &&
                            body["format"].get<std::string>().rfind("psych_v1", 0) == 0;
            std::vector<std::pair<double, bool>> source, psych07, psych10;
            for (const ChartNote& note : parsed.value().notes) source.push_back({note.timeMs, note.isPlayer});
            if (body.is_object() && body.contains("notes") && body["notes"].is_array())
                for (const json& section : body["notes"]) {
                    const bool must = section.value("mustHitSection", false);
                    for (const json& note : section["sectionNotes"]) {
                        const int lane = note[1].get<int>();
                        psych07.push_back({note[0].get<double>(), lane > 3 ? !must : must});
                        psych10.push_back({note[0].get<double>(), labelled ? lane < 4 : (lane > 3 ? !must : must)});
                    }
                }
            std::sort(source.begin(), source.end());
            std::sort(psych07.begin(), psych07.end());
            std::sort(psych10.begin(), psych10.end());
            expect(exported && !labelled && psych07 == source && psych10 == source,
                   "chart: el export a Psych no lleva la etiqueta psych_v1 y Psych 0.7 y 1.0 ponen cada nota en su lado");
        }
    }
    song.format = SongChart::Format::VSlice; song.difficulty = "erect";
    const std::string vs = R"({"notes":{"erect":[{"t":100,"d":4,"l":0,"k":"alt"},{"t":200,"d":2,"l":20,"p":[1]}],"nightmare":[{"t":999,"d":1,"k":"Other"}]},"events":[{"t":20,"e":"X","v":true}],"custom":1})";
    const auto v = json::parse(patchSongNoteTypes(vs, song, two, {"Alt Anim Note", "New"}, error), nullptr, false);
    expect(error.empty() && v["notes"]["erect"][0]["k"] == "alt" && v["notes"]["erect"][1]["k"] == "New" && v["notes"]["nightmare"][0]["k"] == "Other" && v["notes"]["erect"][1]["p"][0] == 1, "chart: V-Slice edita dificultad elegida y preserva alt, otras dificultades y params");
}

void codeColorCase() {
    using K = CodeColorKind;
    auto colored = [&](Engine engine, const std::string& source, const std::string& word, K kind, bool config = false) {
        const size_t offset = source.find(word);
        const auto spans = colorBlockSource(engine, source, config);
        return offset != std::string::npos && std::any_of(spans.begin(), spans.end(), [&](const auto& span) {
            return span.begin <= offset && span.end >= offset + word.size() && span.kind == kind;
        });
    };
    const std::string haxe = "// á comentario\nvar amount:Float = 0xFF + 1.5e-2;\nfunction hit() { trace(\"a\\\" // b\"); return true; } /* multi\nline */";
    expect(colored(Engine::Codename, haxe, "á comentario", K::Comment), "coloreado HScript: comentarios UTF-8");
    expect(colored(Engine::Codename, haxe, "var", K::Keyword), "coloreado HScript: palabras clave");
    expect(colored(Engine::VSlice, haxe, "Float", K::Type), "coloreado V-Slice: tipos");
    expect(colored(Engine::VSlice, haxe, "hit", K::Function), "coloreado V-Slice: funciones");
    expect(colored(Engine::Codename, haxe, "0xFF", K::Number) && colored(Engine::Codename, haxe, "1.5e-2", K::Number), "coloreado: hexadecimales y exponentes");
    expect(colored(Engine::Codename, haxe, "a\\\" // b", K::String), "coloreado: escape y comentario dentro de una cadena");
    expect(colored(Engine::Codename, haxe, "multi\nline", K::Comment), "coloreado Haxe: comentario multilinea");
    expect(colored(Engine::Codename, haxe, "true", K::Literal), "coloreado: literal booleano");
    const std::string lua = "--[=[ long\ncomment ]=]\nlocal value = [==[if\n-- not a comment]==]\nfunction hit() return nil end\n-- incomplete [=[";
    expect(colored(Engine::Psych, lua, "long\ncomment", K::Comment), "coloreado Lua: comentarios largos con delimitador");
    expect(colored(Engine::Psych, lua, "if\n-- not a comment", K::String), "coloreado Lua: cadenas largas sin palabras clave falsas");
    expect(colored(Engine::Psych, lua, "local", K::Keyword) && colored(Engine::Psych, lua, "nil", K::Literal), "coloreado Lua: palabras clave y nil");
    expect(colored(Engine::Psych, "local a = 1 // 2", "//", K::Text), "coloreado Lua: division no se confunde con comentario Haxe");
    expect(colored(Engine::Psych, "function idle\n0.7\n", "function", K::Text, true) &&
        colored(Engine::Psych, "function idle\n0.7\n", "0.7", K::Number, true), "coloreado: configuracion txt no se trata como script");
    for (const std::string& source : {haxe, lua, std::string("/* unfinished\nα"), std::string("\"unfinished\nvar x = 2;"), std::string(262144, 'x')}) {
        size_t previous = 0;
        bool complete = true;
        for (const auto& span : colorBlockSource(Engine::Codename, source)) {
            complete &= span.begin == previous && span.end > span.begin && span.end <= source.size();
            previous = span.end;
        }
        expect(complete && previous == source.size(), "coloreado tolerante: spans completos incluso con borradores incompletos");
    }
    expect(colorBlockSource(Engine::Codename, "").empty(), "coloreado: buffer vacio seguro");
}

#include "PublicCoreCases.hpp"

int runSynthetic() {
    const fs::path base = fs::temp_directory_path() /
        ("fml-notelab-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(base);
    codenameCase(base);
    psychCase(base);
    psychRealWorldCase(base);
    songsCase(base);
    vsliceCase(base);
    textCase();
    projectCase();
    distributeCase();
    exportCase(base);
    blocksCase(base);
    expandedBlocksCase(base);
    codeCase();
    codeColorCase();
    searchCase(base);
    createCase();
    importCase(base);
    customCreatorCase(base);
    layoutCase(base);
    publicCoreCases(base);
    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("\n%s: %d fallos\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}

int runScan(const std::vector<fs::path>& roots, bool spanish, bool autoBase) {
    // Como la app (DESIGN_PLUGIN_NOTE_LAB §30): el motor sale del mod solo y,
    // si el mod no es una instalacion de su motor, se monta debajo el juego
    // base que se encuentre (sin recordar ninguno: aqui no hay preferencias).
    EngineGuess guess;
    std::vector<fs::path> mounted = roots;
    std::string baseLine, layoutLine;
    {
        // Una instalacion con mods dentro se abre por su mod (NoteInstall.hpp,
        // openLayoutOf), como la app.
        OpenLayout layout;
        if (roots.size() == 1) layout = openLayoutOf(roots[0]);
        const fs::path mod = roots.size() == 1 ? layout.mod : fs::path();
        Scan probe;
        scan(probe, roots.size() == 1 ? std::vector<fs::path>{mod} : roots);
        guess = guessEngine(probe.vfs, probe.catalog);
        if (roots.size() == 1) {
            mounted = {mod};
            if (guess.found && guess.engine == Engine::Codename) {
                mounted = layout.packs;
                mounted.push_back(mod);
                mounted.insert(mounted.end(), layout.lowPacks.begin(), layout.lowPacks.end());
                if (!layout.packs.empty()) layoutLine += "paquetes encima del mod: " + std::to_string(layout.packs.size()) + "\n";
            }
            if (!layout.install.empty())
                layoutLine += "mod " + mod.filename().u8string() + " de la instalacion " + layout.install.filename().u8string() + " (" +
                              std::to_string(layout.mods.size()) + " mods)\n";
            else if (!layout.mods.empty())
                layoutLine += "instalacion abierta como juego base, con " + std::to_string(layout.mods.size()) + " mods dentro\n";
        }
        if (autoBase && roots.size() == 1 && guess.found && guess.other.empty()) {
            BaseSearch base;
            if (!layout.install.empty() && isEngineInstall(layout.install, guess.engine)) {
                base.folder = layout.install;
                base.how = BaseFound::Contains;
            } else if (!hasEngineDefault(probe.catalog, guess.engine)) {
                base = findEngineBase(mod, guess.engine, {});
                if (base.folder.empty()) baseLine = "juego base: no encontrado";
            }
            if (!base.folder.empty() && base.folder != mod) {
                mounted.push_back(baseMountOf(base.folder));
                baseLine = "juego base: " + base.folder.u8string() + " (" + baseFoundKey(base.how) + ")";
            }
        }
    }
    // Note Lab es solo para Codename, Psych y V-Slice: otro motor no se abre.
    if (!guess.other.empty()) {
        std::string marks;
        for (const std::string& mark : guess.otherEvidence) marks += (marks.empty() ? "" : ", ") + mark;
        std::printf("otro motor: %s (%s). Note Lab solo trabaja con Codename, Psych y V-Slice: no se abre.\n",
                    guess.other.c_str(), marks.c_str());
        return 0;
    }
    Scan s;
    scan(s, mounted);
    if (guess.found) settleOtherEngines(s.catalog, s.reports, guess.engine);
    std::printf("%zu archivos, %zu estilos, motor %s\n", s.vfs.fileCount(), s.catalog.styles.size(),
                guess.found ? engineKey(guess.engine) : "sin detectar");
    if (!layoutLine.empty()) std::printf("%s", layoutLine.c_str());
    if (!baseLine.empty()) std::printf("%s\n", baseLine.c_str());
    for (const Finding& f : s.catalog.findings)
        std::printf("  [lectura] %s %s\n", f.code.c_str(), describe(f, spanish).c_str());
    int errors = 0, warnings = 0;
    for (size_t i = 0; i < s.catalog.styles.size(); ++i) {
        const NoteStyle& style = s.catalog.styles[i];
        const StyleReport& report = s.reports[i];
        const auto definition = s.vfs.find(style.definition);
        const bool fromBase = !baseLine.empty() && baseLine.find("no encontrado") == std::string::npos && definition &&
                              definition->rootIndex == mounted.size() - 1;
        std::printf("\n%s  [%s, %s%s%s]%s\n", style.id.c_str(), engineKey(style.engine), styleUseKey(style.use),
                    style.useDetail.empty() ? "" : ": ", style.useDetail.c_str(), fromBase ? " [juego base]" : "");
        std::printf("  piezas %d/%zu", report.partsResolved, style.parts.size());
        if (report.partsInherited) std::printf(" (%d heredadas)", report.partsInherited);
        if (style.hasHud) std::printf(", HUD %d/%d", report.hudFound, report.hudExpected);
        std::printf("\n");
        std::map<std::string, int> shown;
        for (const Finding& f : report.findings) {
            if (f.severity == Severity::Error) ++errors;
            if (f.severity == Severity::Warning) ++warnings;
            if (++shown[f.code] > 6) continue;
            std::printf("  %s %s %s\n", toString(f.severity), f.code.c_str(), describe(f, spanish).c_str());
        }
        for (const auto& entry : shown)
            if (entry.second > 6) std::printf("  ... %d mas de %s\n", entry.second - 6, entry.first.c_str());
    }
    const std::vector<NoteTypeEntry> types = scanNoteTypes(s.vfs, s.catalog);
    std::printf("\nTipos de nota: %zu\n", types.size());
    for (const NoteTypeEntry& type : types) {
        const char* bot = type.bot == BotRule::Ignores ? "bot la ignora"
                        : type.bot == BotRule::IgnoresOnPlayerSide ? "bot la ignora (jugador)" : "";
        std::printf("  [%s] %s%s  %s%s%s %s\n", engineKey(type.engine), type.name.c_str(), type.builtin ? " (de serie)" : "",
                    type.lookStyle.empty() ? "sin aspecto propio" : type.lookStyle.c_str(),
                    type.hitMisses ? ", tocarla es fallo" : "", *bot ? ", " : "", bot);
        if (!type.script.empty() || !type.config.empty())
            std::printf("      %s\n", (type.script.empty() ? type.config : type.script).c_str());
    }
    const std::vector<SongChart> songs = scanSongs(s.vfs);
    std::map<std::string, int> byFormat;
    for (const SongChart& song : songs) ++byFormat[songFormatKey(song.format)];
    std::printf("\nCharts: %zu", songs.size());
    for (const auto& entry : byFormat) std::printf(", %s %d", entry.first.c_str(), entry.second);
    std::printf("\n\n%zu estilos, %d errores, %d avisos\n", s.catalog.styles.size(), errors, warnings);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::vector<fs::path> roots;
    bool scanMode = false, spanish = false, autoBase = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--scan") scanMode = true;
        else if (arg == "--es") spanish = true;
        else if (arg == "--no-base") autoBase = false;
        else if (arg.rfind("--create-samples=", 0) == 0) return writeCreateSamples(fs::u8path(arg.substr(17)));
        else if (arg.rfind("--block-samples=", 0) == 0) {
            expandedBlocksCase(fs::u8path(arg.substr(16)));
            return g_failures == 0 ? 0 : 1;
        }
        else roots.push_back(fs::u8path(arg));
    }
    if (scanMode) {
        if (roots.empty()) {
            std::printf("uso: NoteLabCoreTests --scan <raiz> [<raiz>...] [--es] [--no-base]\n");
            return 2;
        }
        return runScan(roots, spanish, autoBase);
    }
    return runSynthetic();
}

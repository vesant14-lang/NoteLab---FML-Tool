#define main noteLabApplicationMain
#include "../src/main.cpp"
#undef main
#include "PublicMatrix.hpp"
#include "PublicMediaCases.hpp"

int main(int argc, char** argv) {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks; if (!ok) ++failures;
        std::printf("[%s] %s\n", ok ? "ok" : "FAIL", label);
        return ok;
    };
    const fs::path root = fs::absolute(".qa") / ("workflow-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    auto textFile = [&](const fs::path& file, const std::string& text) { fs::create_directories(file.parent_path()); std::ofstream out(file, std::ios::binary); out << text; };
    auto sheet = [&](const fs::path& file, bool receptors, unsigned char tint) {
        std::vector<std::string> names;
        const char* colors[] = {"purple", "blue", "green", "red"};
        const char* upper[] = {"LEFT", "DOWN", "UP", "RIGHT"};
        for (int i = 0; i < 4; ++i) {
            names.push_back(std::string(colors[i]) + "0000");
            names.push_back(std::string(colors[i]) + " hold piece0000");
            names.push_back(std::string(colors[i]) + " hold end0000");
            if (receptors) {
                names.push_back(std::string("arrow") + upper[i] + "0000");
                names.push_back(std::string(directionKey(i)) + " press0000");
                names.push_back(std::string(directionKey(i)) + " confirm0000");
            }
        }
        Image image = blankImage(static_cast<int>(names.size()) * 8, 8);
        for (size_t i = 0; i < image.rgba.size(); i += 4) { image.rgba[i] = tint; image.rgba[i + 1] = 100; image.rgba[i + 2] = 210; image.rgba[i + 3] = 255; }
        std::string xml = "<TextureAtlas imagePath=\"sheet.png\">";
        for (size_t i = 0; i < names.size(); ++i) xml += "<SubTexture name=\"" + names[i] + "\" x=\"" + std::to_string(i * 8) + "\" y=\"0\" width=\"8\" height=\"8\"/>";
        fs::path png = file; png += L".png";
        fs::path meta = file; meta += L".xml";
        textFile(meta, xml + "</TextureAtlas>");
        const auto bytes = encodePng(image);
        std::ofstream output(png, std::ios::binary); output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const fs::path first = root / "first", second = root / "second";
    sheet(first / "images/game/notes/default", true, 60);
    sheet(first / "images/game/notes/Custom", false, 180);
    sheet(second / "images/game/notes/default", true, 240);
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 2; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("Note Lab workflow tests", 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) { std::fprintf(stderr, "%s\n", SDL_GetError()); if (window) SDL_DestroyWindow(window); SDL_Quit(); return 2; }
    SDL_GL_MakeCurrent(window, context);
    ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
    if (argc > 1 && std::string(argv[1]) == "--playback-only") {
        runMediaAudioCases(root, [&](bool ok, const std::string& label) { check(ok, label.c_str()); });
        runSongLoopCases(root, [&](bool ok, const std::string& label) { check(ok, label.c_str()); });
        ImGui::DestroyContext(); SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
        std::printf("Playback workflow: %d checks, %d failures\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
    {
        NoteLabApp app; app.transient = true; app.headless = true; app.autoBase = false;
        check(nlbuild::audioPlayback && !nlbuild::blockAudioPlayback && !app.uiSounds && app.musicVolume == 0.8f, "song and imported-file playback are enabled; block editor cues stay disabled");
        nlblocks::playCue(nlblocks::Cue::Pick, true);
        nlblocks::playCue(nlblocks::Cue::Success, true);
        check(SDL_WasInit(SDL_INIT_AUDIO) == 0 && !app.audio.ready(), "even forced editor cues do not initialize an audio device");
        const auto oldSettings = settingsFolder() / "settings.json";
        textFile(oldSettings, R"({"musicVolume":1.0,"uiSounds":true})");
        loadSettings(app);
        check(!app.uiSounds && app.musicVolume == 1.0f, "legacy settings restore song volume but cannot reactivate block editor cues");
        textFile(oldSettings, R"({"musicVolume":0.0,"uiSounds":false})");
        loadSettings(app);
        check(app.musicVolume == 0.8f, "legacy zero volume from the silent build is restored once");
        app.transient = false; app.musicVolume = 0.0f; saveSettings(app); app.transient = true;
        NoteLabApp muted; loadSettings(muted);
        check(muted.musicVolume == 0.0f && !muted.uiSounds, "a new deliberate song mute persists without enabling block sounds");
        app.transient = false; app.musicVolume = 0.35f; saveSettings(app); app.transient = true;
        NoteLabApp restoredVolume; loadSettings(restoredVolume);
        check(std::abs(restoredVolume.musicVolume - 0.35f) < 0.001f && !restoredVolume.uiSounds, "song volume roundtrips independently from muted block cues");
        chooseDemo(app);
        addSource(app, first); addSource(app, second);
        check(app.sources.size() == 2 && app.rendererReady, "two independent sources open with the native renderer");
        int custom = -1, donor = -1;
        for (size_t i = 0; i < app.sources[0]->catalog.styles.size(); ++i) if (app.sources[0]->catalog.styles[i].useDetail == "Custom") custom = static_cast<int>(i);
        for (size_t i = 0; i < app.sources[1]->catalog.styles.size(); ++i) if (app.sources[1]->catalog.styles[i].use == StyleUse::Default) donor = static_cast<int>(i);
        check(custom >= 0 && donor >= 0, "partial note sheet and separate receptor donor are found");
        if (custom >= 0 && donor >= 0) {
            selectStyle(app, 0, custom);
            check(renderPreview(app, 640, 360).ok, "partial note sheet renders with the automatic receptor fallback");
            createVariant(app);
            const std::string originalNotes = selectedStyle(app)->sheets.front().image;
            StyleComponents components;
            const bool composed = applyStyleComposition(app, 1, donor, components);
            check(composed, "cross-source composition follows the actual application path");
            if (!composed) std::printf("composition detail: %s\n", app.composer.message.c_str());
            const auto* note = findPart(*selectedStyle(app), Part::Note, 0);
            const auto* strum = findPart(*selectedStyle(app), Part::StrumStatic, 0);
            check(note && strum && note->sheet != strum->sheet && selectedStyle(app)->sheets[note->sheet].image == originalNotes,
                  "composition keeps the original note sheet and a separate receptor sheet");
            const std::string receptorImage = strum ? selectedStyle(app)->sheets[strum->sheet].image : "";
            check(receptorImage.rfind("notelab-compose/", 0) == 0 && app.sources[0]->vfs->readBytes(receptorImage).has_value(), "donor files are preserved in owned storage");
            requestSourceClose(app, 0);
            check(app.openSourceClose && app.sources.size() == 2, "closing edited material asks before discarding work");
            app.openSourceClose = false;
            const std::string id = selectedStyle(app)->id;
            removeSource(app, 1);
            check(selectedStyle(app) && selectedStyle(app)->id == id && !app.undo.empty(), "closing a different source preserves selection and undo history");
            swapHistory(app, app.undo, app.redo);
            check(findPart(*selectedStyle(app), Part::StrumStatic, 0) == nullptr, "undo restores the previous partial style");
            swapHistory(app, app.redo, app.undo);
            check(app.sources[0]->vfs->readBytes(receptorImage).has_value() && findPart(*selectedStyle(app), Part::StrumStatic, 0), "redo restores receptor data after the donor closes");
            const fs::path project = root / fs::u8path("José.fmlnote");
            check(saveProjectTo(app, project) && saveProjectTo(app, project), "first and repeat application saves work on Unicode paths");
            check(openProjectFile(app, project) && selectedStyle(app) && selectedStyle(app)->id == id && renderPreview(app, 640, 360).ok,
                  "project reopens the combined style and actual imported files");
            Source& source = *app.sources[0];
            ExportIo io;
            io.readBytes = [&](const std::string& path) { return source.vfs->readBytes(path, 64u * 1024u * 1024u); };
            io.readText = [&](const std::string& path) { return source.vfs->readText(path); };
            for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
                ExportOptions options; options.target = engine; options.hud = false;
                options.role = engine == Engine::VSlice ? ExportRole::SongSkin : ExportRole::ModSkin;
                options.name = "combined";
                auto package = buildExport(*selectedStyle(app), options, io);
                std::string error;
                check(!package.hasErrors() && verifyExport(package, root / "verify"), "combined sheets pass target-engine structural re-reading");
                addInstallGuides(package);
                const fs::path folder = root / "exports" / engineKey(engine);
                check(writeExportFolder(package, folder, error, false), "verified combined package writes as a folder");
                fs::path zip = folder; zip += L".zip";
                check(writeExportZip(package, zip, error, false), "verified combined package writes as ZIP");
            }
            const std::string before = writeProject(projectFromApp(app));
            const fs::path broken = root / "broken.fmlnote";
            textFile(broken, R"({"format":false,"version":2})");
            check(!openProjectFile(app, broken) && writeProject(projectFromApp(app)) == before, "malformed projects preserve the current work");
            app.dirty = true; app.pending = PendingAction::OpenRecent; app.pendingPath = broken;
            performPending(app, window);
            check(app.dirty && writeProject(projectFromApp(app)) == before, "failed replacement preserves the unsaved-work flag");
            app.dialogReady = true; app.dialogAction = DialogAction::OpenProject; app.dialogPath.clear(); app.dialogPaths.clear();
            processDialog(app, window);
            check(app.dirty && writeProject(projectFromApp(app)) == before, "cancelled project selection preserves unsaved work");
            NoteProject missing; missing.sources.push_back({}); missing.sources[0].path = (root / "missing").u8string();
            textFile(broken, writeProject(missing));
            check(!openProjectFile(app, broken) && writeProject(projectFromApp(app)) == before, "missing project sources preserve the current work");
            textFile(root / "not-an-archive.zip", "invalid ZIP bytes");
            missing.sources[0].path = (root / "not-an-archive.zip").u8string(); textFile(broken, writeProject(missing));
            check(!openProjectFile(app, broken) && writeProject(projectFromApp(app)) == before, "unreadable sources cannot partially replace the current project");
            std::vector<Issue> issues;
            const fs::path realRoot = source.root;
            source.root = root / "missing-after-open";
            const std::string keep = writeStyle(source.catalog.styles.back());
            reloadKeepingEdits(app, source, issues);
            check(!issues.empty() && writeStyle(source.catalog.styles.back()) == keep, "failed reload preserves styles and edits");
            source.root = realRoot;
        }
        if (app.audioReady) app.audio.shutdown();
        if (app.rendererReady) app.renderer.shutdown();
    }
    {
        NoteLabApp app; app.transient = true; app.autoBase = false;
        openCustomCreator(app);
        check(app.sources.size() == 1 && app.custom.createdWorkspace, "empty welcome creates a dedicated custom workspace");
        check(!customRoleIssue(app).empty(), "creator explains what to select instead of allowing an empty role");
        finishCustomCreator(app);
        check(app.sources.empty() && !app.dirty, "cancelling an empty custom creator returns to the welcome screen");
        openCustomCreator(app);
        app.custom.recipe.resources.push_back({}); app.custom.recipe.resources[0].input.kind = ImportKind::Sound; app.custom.resource = 0;
        check(!customRoleIssue(app).empty(), "audio cannot accidentally be assigned as an image role");
        app.custom.draft.role = CustomRole::SoundEffect;
        check(!customRoleIssue(app).empty(), "named audio assignment requires its name");
        std::snprintf(app.custom.soundName.data(), app.custom.soundName.size(), "%s", "hit");
        check(customRoleIssue(app).empty() && !app.custom.audio && nlbuild::audioPlayback, "audio assignment supports audition without starting it automatically");
        finishCustomCreator(app);
    }
    runCreationMatrix(root, [&](bool ok, const std::string& label) { check(ok, label.c_str()); });
    runMediaCases(root, [&](bool ok, const std::string& label) { check(ok, label.c_str()); });
    runMediaAudioCases(root, [&](bool ok, const std::string& label) { check(ok, label.c_str()); });
    runSongLoopCases(root, [&](bool ok, const std::string& label) { check(ok, label.c_str()); });
    runRealEngineDisplayMatrix(root, [&](bool ok, const std::string& label) { check(ok, label.c_str()); });
    ImGui::DestroyContext(); SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::printf("Public workflow: %d checks, %d failures\n", checks, failures);
    std::printf("Fixtures kept in %s\n", root.u8string().c_str());
    return failures == 0 ? 0 : 1;
}

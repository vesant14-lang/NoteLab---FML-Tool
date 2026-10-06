#pragma once

#include <limits>

template<class Check>
void runMediaCases(const fs::path& qaRoot, Check check) {
    const fs::path root = qaRoot / "media", mod = root / fs::u8path("Mod José"), base = root / "base";
    auto write = [](const fs::path& path, const std::vector<unsigned char>& bytes) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    Image image = blankImage(16, 16);
    for (size_t i = 0; i < image.rgba.size(); i += 4) { image.rgba[i] = 230; image.rgba[i + 3] = 255; }
    const auto png = encodePng(image);
    std::vector<unsigned char> ogg(80, 0); std::memcpy(ogg.data(), "OggS", 4); std::memcpy(ogg.data() + 28, "\1vorbis", 7);
    std::vector<unsigned char> mp4(48, 0); mp4[3] = 24; std::memcpy(mp4.data() + 4, "ftypisom", 8);
    write(mod / fs::u8path("images/screamer/cara-é.png"), png);
    write(mod / "sounds/screamer.ogg", ogg); write(mod / "videos/intro.mp4", mp4);
    write(mod / "data/readme.txt", {1, 2, 3});
    write(base / "images/only-base.png", png);
    write(base / fs::u8path("images/screamer/cara-é.png"), {1, 2, 3});
    Vfs vfs; vfs.pushRoot(mod, "Selected mod"); vfs.pushRoot(base, "Base game", MountProviderKind::BaseGame);
    auto catalog = scanModResources(vfs);
    check(catalog.size() == 3, "media catalog sections contain image, sound and video without text or base duplicates");
    check(scanModResources(vfs, true).size() == 4, "base game resources are explicitly opt-in");
    check(scanModResources(vfs, true, 2).size() == 2, "resource catalog has a bounded item count");
    const auto* selected = findModResource(catalog, "screamer/cara-é", ResourceKind::Image);
    check(selected && selected->provider == "Selected mod", "winning mod resource shadows the base and resolves native keys");
    check(findModResource(catalog, "IMAGES/SCREAMER/CARA-é.PNG", ResourceKind::Image) != nullptr, "resource lookup supports actual path case differences");
    check(resourcePackagePath("shared/images/screamer/cara-é.png", ResourceKind::Image) == "images/screamer/cara-é.png", "scope prefixes map to an engine-local export path");
    check(resourceNativeKey("sounds/screamer.OGG", ResourceKind::Sound) == "screamer", "sound keys omit the source folder and extension");
    for (const std::string unsafe : {"../image.png", "images/../../file.png", "C:/file.png", "//host/file.png", "images//file.png", "images/./file.png", "images/file.png/"})
        check(!safeResourceReference(unsafe), "unsafe or noncanonical media reference is rejected");
    auto ambiguous = catalog;
    ambiguous.push_back({ResourceKind::Image, "alternate/images/screamer/cara-é.png", "Other", 0, 0, false, false});
    check(!findModResource(ambiguous, "screamer/cara-é", ResourceKind::Image), "ambiguous short media key is not silently guessed");
    BlockProgram program;
    const int hat = newBlock(program, "event.hit"), screamer = newBlock(program, "do.screamer"), video = newBlock(program, "do.video");
    placeTop(program, hat, 24, 24); attachAfter(program, hat, screamer); attachAfter(program, screamer, video);
    program.node(screamer)->args[0].value = "images/screamer/cara-é.png";
    program.node(screamer)->args[1].value = "sounds/screamer.ogg";
    program.node(video)->args[0].value = "videos/intro.mp4";
    const int loose = newBlock(program, "do.image"); placeTop(program, loose, 24, 260); program.node(loose)->args[0].value = "images/not-in-export.png";
    auto uses = blockResourceUses(program);
    check(uses.size() == 4 && std::count_if(uses.begin(), uses.end(), [](const auto& u) { return u.active; }) == 3, "usage list marks connected media separately from disconnected blocks");
    check(blockResourceUses(readProgram(writeProgram(program))).size() == uses.size(), "media references persist through project serialization");
    NoteLabApp app;
    auto source = std::make_unique<Source>(); source->vfs = std::make_unique<Vfs>(); source->vfs->pushRoot(mod, "Media fixture");
    source->typeBlocks["Scare"] = program; app.sources.push_back(std::move(source));
    openModResources(app, 0, "Scare", screamer, 0, ResourceKind::Image);
    check(app.media.resources.size() == 3 && !app.media.audio, "opening the resource browser never autoplays");
    const auto* sound = findModResource(app.media.resources, "sounds/screamer.ogg", ResourceKind::Sound);
    check(sound && !assignModResource(app, *sound), "image slot rejects a sound resource");
    const auto* picture = findModResource(app.media.resources, "images/screamer/cara-é.png", ResourceKind::Image);
    check(picture && assignModResource(app, *picture) && app.dirty, "assigning a mod resource uses the real application path and marks it dirty");
    check(!app.canvas.undo.empty(), "resource assignment participates in block undo");
    selectMediaResource(app, *app.sources.front(), *picture);
    check(app.media.selected == picture->path && !app.media.audio, "selecting an image stays silent");
    clearMediaPreview(app);
    const fs::path imports = root / "import-target"; fs::create_directories(imports);
    const auto sourceImage = mod / fs::u8path("images/screamer/cara-é.png");
    check(!importMediaFiles({sourceImage}, imports, "images/custom", false).error.empty(), "media import requires explicit approval to create a missing folder");
    auto imported = importMediaFiles({sourceImage}, imports, "images/custom", true);
    if (!imported.error.empty()) std::printf("media import detail: %s\n", imported.error.c_str());
    check(imported.error.empty() && imported.files.size() == 1 && fs::exists(imported.files.front().first), "media import creates the approved folder and copies Unicode file names");
    check(!importMediaFiles({sourceImage}, imports, "images/custom", true).error.empty(), "media import never overwrites an existing file");
    check(!importMediaFiles({sourceImage, sourceImage}, imports, "images/duplicates", true).error.empty() && !fs::exists(imports / "images/duplicates"), "duplicate inputs fail before creating or writing any destination");
    for (const std::string badFolder : {"../escape", "images/../../escape", "C:/escape", "images/CON", "images/bad.", "images/bad "})
        check(!importMediaFiles({sourceImage}, imports, badFolder, true).error.empty(), "media import rejects unsafe or reserved destination folders");
    prepareMediaImport(app, {sourceImage}); app.media.createFolder = true;
    const bool mediaCommitted = commitMediaImport(app);
    check(mediaCommitted && !app.sources.front()->importedFiles.empty(), "project-only media import mounts an owned copy without changing the original mod");
    if (!mediaCommitted) std::printf("owned media detail: %s\n", app.media.message.c_str());
    auto saved = projectFromApp(app);
    check(!saved.sources.empty() && !saved.sources.front().imports.empty(), "project-only resource import is included in saved project references");
    clearMediaPreview(app);
    NoteLabApp lifecycle; lifecycle.transient = true; lifecycle.headless = true;
    for (const auto& path : {base, mod}) {
        auto item = std::make_unique<Source>(); item->root = path; item->vfs = std::make_unique<Vfs>(); item->vfs->pushRoot(path, "Lifecycle fixture");
        lifecycle.sources.push_back(std::move(item));
    }
    openModResources(lifecycle, 1, "Scare");
    removeSource(lifecycle, 0);
    check(lifecycle.media.source == 0 && lifecycle.sources.size() == 1 && !lifecycle.media.resources.empty(), "closing another source remaps the resource browser to the surviving source");
    lifecycle.media.importRequestOpen = true;
    removeSource(lifecycle, 0);
    check(lifecycle.media.source == -1 && !lifecycle.media.requestOpen && !lifecycle.media.importRequestOpen && lifecycle.media.resources.empty(), "closing the resource source invalidates its catalog, import request and preview safely");
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        NoteStyle style; style.id = "media-only"; style.name = "Media"; style.engine = engine;
        ExportOptions options; options.target = engine; options.role = ExportRole::NoteType; options.name = "scare"; options.noteType = "Scare";
        options.notes = options.strums = options.splashes = options.holdCovers = options.hud = false;
        options.blocks = program;
        ExportIo io; io.readBytes = [&](const std::string& path) { return vfs.readBytes(path, 128u * 1024u * 1024u); };
        io.readText = [&](const std::string& path) { return vfs.readText(path); };
        auto package = buildExport(style, options, io);
        check(!package.hasErrors(), "connected image, sound and video resources export for the selected engine");
        for (const auto& expected : std::vector<std::pair<std::string, std::vector<unsigned char>>>{{"images/screamer/cara-é.png", png}, {"sounds/screamer.ogg", ogg}, {"videos/intro.mp4", mp4}}) {
            auto file = std::find_if(package.files.begin(), package.files.end(), [&](const auto& f) { return f.path == expected.first; });
            check(file != package.files.end() && file->bytes == expected.second, "export copies referenced media byte-for-byte without absolute source paths");
        }
        check(std::none_of(package.files.begin(), package.files.end(), [](const auto& f) { return f.path == "images/not-in-export.png"; }), "disconnected media is not packaged");
        const auto code = generateBlocks(engine, "Scare", "scare", program);
        std::string text; for (const auto& f : code.files) text += f.text;
        check(text.find("screamer/cara-é") != std::string::npos && text.find(mod.u8string()) == std::string::npos &&
            (text.find("Paths.video(\"intro\")") != std::string::npos || text.find("Paths.video") != std::string::npos), "generated media calls use engine-relative keys");
        check(text.find("stopSound") != std::string::npos || text.find(".stop()") != std::string::npos, "screamer generated code stops its sound on cleanup");
        check(std::any_of(code.conflicts.begin(), code.conflicts.end(), [](const auto& f) { return f.key == "do.video" && !f.en.empty(); }), "video scripting/version restrictions are visible before export");
        auto modified = program; modified.node(screamer)->args[0].value = "images/missing.png"; options.blocks = modified;
        check(buildExport(style, options, io).hasErrors(), "missing custom image blocks export rather than delivering a broken package");
        modified.node(screamer)->args[0].value = "images/screamer/cara-é.jpg"; options.blocks = modified;
        check(buildExport(style, options, io).hasErrors(), "unsupported engine image format is a preflight error");
        modified.node(screamer)->args[0].value = "../outside.png"; options.blocks = modified;
        check(buildExport(style, options, io).hasErrors(), "unsafe custom reference blocks export");
        const fs::path zip = root / (std::string(engineKey(engine)) + "-resources.zip");
        std::string error;
        check(verifyExport(package, root / (std::string(engineKey(engine)) + "-verify")), "media-only note type export passes native structural rereading");
        addInstallGuides(package);
        check(writeExportZip(package, zip, error, false), "media export ZIP writes successfully");
        Vfs zipped; check(zipped.pushZip(zip, "Export ZIP") > 0, "media package reopens through the standard ZIP provider");
        const auto zippedCatalog = scanModResources(zipped);
        const auto* zippedImage = findModResource(zippedCatalog, "images/screamer/cara-é.png", ResourceKind::Image);
        check(zippedImage && zippedImage->archive && zipped.readBytes(zippedImage->path) == std::optional<std::vector<unsigned char>>(png), "ZIP media discovery and extraction preserve Unicode references");
    }
}

template<class Check>
void runMediaAudioCases(const fs::path& qaRoot, Check check) {
    const fs::path audioRoot = qaRoot / "first";
    const std::string relative = "sounds/long-preview.wav";
    const int rate = 8000, frames = rate * 65;
    std::vector<unsigned char> wave(44 + static_cast<size_t>(frames) * 4, 0);
    auto put16 = [&](size_t at, unsigned value) { wave[at] = value & 255; wave[at + 1] = (value >> 8) & 255; };
    auto put32 = [&](size_t at, unsigned value) { put16(at, value & 65535); put16(at + 2, value >> 16); };
    std::memcpy(wave.data(), "RIFF", 4); put32(4, static_cast<unsigned>(wave.size() - 8));
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8); put32(16, 16); put16(20, 1); put16(22, 2);
    put32(24, rate); put32(28, rate * 4); put16(32, 4); put16(34, 16);
    std::memcpy(wave.data() + 36, "data", 4); put32(40, frames * 4);
    for (int i = 0; i < frames; ++i) {
        const auto sample = static_cast<short>(std::sin(i * 440.0 * 6.283185307179586 / rate) * 4000.0);
        put16(44 + static_cast<size_t>(i) * 4, static_cast<unsigned short>(sample));
        put16(46 + static_cast<size_t>(i) * 4, static_cast<unsigned short>(sample));
    }
    fs::create_directories((audioRoot / relative).parent_path());
    {
        std::ofstream file(audioRoot / relative, std::ios::binary);
        file.write(reinterpret_cast<const char*>(wave.data()), static_cast<std::streamsize>(wave.size()));
    }
    Source source; source.vfs = std::make_unique<Vfs>(); source.vfs->pushRoot(audioRoot, "Private audio fixture");
    const ModResource resource{ResourceKind::Sound, relative, "Private audio fixture", 0, wave.size()};
    NoteLabApp app; app.transient = true; app.headless = true;
    check(!app.media.audioLoop && !app.media.audioRange && app.media.audioSpeed == 1.0f && app.media.audioBalance == 0.0f,
        "audition loop/range default off with normal speed and centered balance");
    app.media.volume = 0.0f;
    auditionMediaResource(app, source, resource);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (app.media.startWhenReady && std::chrono::steady_clock::now() < deadline) { pollMediaAudition(app); SDL_Delay(5); }
    const bool loaded = app.media.audio && app.media.audio->trackCount() == 1 && app.media.audio->durationMs() > 64000.0;
    check(loaded && !app.media.startWhenReady, "a 65-second resource decodes and starts through the real audition path");
    if (!loaded) { clearMediaPreview(app); return; }
    auto& audio = *app.media.audio;
    audio.seekMs(31000.0); SDL_Delay(80);
    ImGui::GetCurrentContext()->Time += 31.0;
    pollMediaAudition(app);
    check(audio.playing() && audio.positionMs() >= 30900.0, "audition remains playing beyond 30 seconds of media and simulated UI time");
    audio.pause(); const auto paused = audio.positionMs(); SDL_Delay(80);
    check(!audio.playing() && std::abs(audio.positionMs() - paused) < 100.0, "audition pause holds its position");
    resumeMediaAudition(app); SDL_Delay(80);
    check(audio.playing() && audio.positionMs() >= paused, "audition resume continues without a new cutoff");
    audio.pause();
    app.media.audioSpeed = 1.75f; app.media.audioBalance = -1.0f;
    applyMediaAudioSettings(app);
    check(std::abs(audio.rate() - 1.75f) < 0.001f && app.media.audioBalance == -1.0f, "audition speed and balance configure the shared audio engine");
    std::vector<short> left, right;
    const std::vector<float> leftPan{app.media.audioBalance};
    audio.setMasterGain(1.0f);
    audio.renderMix(1000.0, 1024, {1.0f}, left, &leftPan);
    app.media.audioBalance = 1.0f; applyMediaAudioSettings(app);
    const std::vector<float> rightPan{app.media.audioBalance};
    audio.setMasterGain(1.0f);
    audio.renderMix(1000.0, 1024, {1.0f}, right, &rightPan);
    long long ll = 0, lr = 0, rl = 0, rr = 0;
    for (size_t i = 0; i + 1 < left.size() && i + 1 < right.size(); i += 2) {
        ll += std::abs(static_cast<int>(left[i])); lr += std::abs(static_cast<int>(left[i + 1]));
        rl += std::abs(static_cast<int>(right[i])); rr += std::abs(static_cast<int>(right[i + 1]));
    }
    check(ll > 0 && rr > 0 && lr == 0 && rl == 0, "configured stereo balance routes offline samples to the selected speaker");
    app.media.audioSpeed = 1.0f; app.media.audioBalance = 0.0f;
    app.media.audioLoop = true; applyMediaAudioSettings(app);
    check(audio.hasLoop(), "Loop enables a full-file loop in the shared audio engine");
    audio.seekMs(audio.durationMs() - 20.0); resumeMediaAudition(app); SDL_Delay(250);
    check(audio.playing() && audio.positionMs() < 2000.0, "full-file looping wraps at the real file end without stopping");
    app.media.audioRange = true; app.media.audioStart = 31.0f; app.media.audioEnd = 31.2f;
    applyMediaAudioSettings(app); audio.seekMs(31200.0); resumeMediaAudition(app); SDL_Delay(250);
    check(audio.playing() && audio.positionMs() >= 30900.0 && audio.positionMs() < 31250.0, "A-B looping repeats the selected range on the real audio callback");
    stopMediaAudition(app);
    check(!audio.playing() && !app.media.startWhenReady && std::abs(audio.positionMs() - 31000.0) < 5.0, "Stop returns to A and cancels automatic start");
    app.media.audioLoop = false; applyMediaAudioSettings(app);
    check(!audio.hasLoop(), "turning Loop off clears the engine loop");
    app.media.audioRange = false; stopMediaAudition(app);
    check(!audio.playing() && audio.positionMs() < 5.0, "Stop without a range returns to the file start");
    audio.seekMs(audio.durationMs() - 20.0); resumeMediaAudition(app); SDL_Delay(250);
    check(!audio.playing(), "a non-looping resource ends naturally instead of repeating");
    app.media.audioSpeed = std::numeric_limits<float>::infinity(); app.media.audioBalance = 8.0f;
    app.media.audioStart = 1000.0f; app.media.audioEnd = -10.0f; app.media.volume = -1.0f;
    applyMediaAudioSettings(app);
    check(audio.rate() == 1.0f && app.media.audioBalance == 1.0f && audio.masterGain() == 0.0f &&
        app.media.audioStart < app.media.audioEnd && app.media.audioEnd <= 65.01f, "invalid speed, balance, volume and reversed ranges normalize safely");
    check(!app.audio.ready() && app.musicVolume == 0.8f && !app.dirty, "audition settings remain independent of song playback and saved block data");
    app.media.audioRange = true; app.media.audioLoop = true;
    clearMediaPreview(app);
    check(!app.media.audio && !app.media.audioRange && app.media.audioStart == 0.0f && app.media.audioEnd == 0.0f && app.media.audioLoop,
        "closing/changing the preview releases audio and resets the per-file range without losing the Loop preference");
    auditionMediaResource(app, source, resource); stopMediaAudition(app);
    const auto stoppedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (app.media.audio && app.media.audio->trackLoadPending() && std::chrono::steady_clock::now() < stoppedDeadline) { pollMediaAudition(app); SDL_Delay(5); }
    pollMediaAudition(app);
    check(app.media.audio && !app.media.audio->playing() && !app.media.startWhenReady,
        "Stop during asynchronous loading does not restart when decoding completes");
    clearMediaPreview(app);
}

template<class Check>
void runSongLoopCases(const fs::path& root, Check check) {
    NoteLabApp app; app.transient = true; app.headless = true; app.autoBase = false; app.musicVolume = 0.0f;
    chooseDemo(app); addSource(app, root / "first");
    const NoteStyle* style = selectedStyle(app);
    std::string error;
    app.audioReady = app.audio.init(&error);
    app.audioLoaded = app.audioReady && app.audio.loadTracks({(root / "first/sounds/long-preview.wav").u8string()}, &error) == 1;
    check(style && app.audioLoaded, "song loop: a real audio track and native note style are ready");
    if (!style || !app.audioLoaded) return;
    app.audio.setMasterGain(0.0f);
    app.notes = {{1, 0, 500.0, 0.0}, {1, 1, 650.0, 800.0}};
    app.patternMs = app.audio.durationMs() + 2500.0;
    for (bool down : {false, true}) {
        app.settings.downscroll = down;
        for (int lap = 0; lap < 3; ++lap) {
            app.manual = false; app.playing = true; app.finished = false;
            app.state.songMs = app.audio.durationMs();
            app.state.hit.assign(2, 1); app.missed.assign(2, 1);
            app.audio.pause(); app.audio.seekMs(app.audio.durationMs());
            updatePlayback(app, 0.0f);
            check(app.state.hit == std::vector<std::uint8_t>(2, 0) && app.missed == std::vector<std::uint8_t>(2, 0),
                  "song loop: each audio-end lap clears tap and sustain hit/miss flags");
            PreviewState fresh = app.state; fresh.hit.assign(2, 0);
            RenderList expected, actual;
            app.preview.build(*style, app.atlases, &app.renderer, app.settings, fresh, app.notes, expected);
            app.preview.build(*style, app.atlases, &app.renderer, app.settings, app.state, app.notes, actual);
            check(actual.cmds.size() == expected.cmds.size() && actual.cmds.size() >= 12,
                  "song loop: taps and sustain heads remain in the render list, in both scroll directions");
            check(app.playing && app.audio.playing() && app.state.songMs < 1000.0,
                  "song loop: chart and audio restart together when audio ends before chart padding");
            app.audio.seekMs(700.0); updatePlayback(app, 0.0f);
            check(app.state.hit == std::vector<std::uint8_t>(2, 1) && app.state.combo == 2,
                  "song loop: taps and sustain heads trigger again on every new lap");
        }
    }
    app.playing = false; app.state.hit.assign(2, 1); app.state.songMs = app.audio.durationMs();
    app.audio.pause(); app.audio.seekMs(app.audio.durationMs()); updatePlayback(app, 0.0f);
    check(!app.audio.playing() && app.state.hit == std::vector<std::uint8_t>(2, 1),
          "song loop: paused transport at audio end does not unexpectedly restart");
    app.playing = true; updatePlayback(app, 0.0f);
    check(app.state.hit == std::vector<std::uint8_t>(2, 0), "song loop: resuming at the end starts a fresh preview pass");
    app.manual = true; app.playing = true; app.scoreRun = 2 + app.playSide; app.state.combo = 17;
    app.audio.pause(); app.audio.seekMs(app.audio.durationMs()); updatePlayback(app, 0.0f);
    check(app.finished && !app.playing && !app.audio.playing() && app.state.combo == 17,
          "song loop: a manual run finishes once and preserves its results at audio end");
    app.manual = false; app.playing = false;
    seekTo(app, 700.0); updatePlayback(app, 0.0f);
    check(app.state.hit == std::vector<std::uint8_t>(2, 1) && std::abs(app.state.songMs - 700.0) < 80.0,
          "song loop: explicit seeking retains the passed-note state without an unwanted reset");
    app.audio.shutdown();
    if (app.rendererReady) app.renderer.shutdown();
}

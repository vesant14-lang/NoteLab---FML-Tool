#pragma once

template<class Check>
void runCreationMatrix(const fs::path& root, Check check) {
    auto write = [](const fs::path& path, const std::vector<unsigned char>& data) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    };
    auto text = [&](const fs::path& path, const std::string& data) { write(path, {data.begin(), data.end()}); };
    for (int cell : {16, 32}) {
        const auto folder = root / "matrix-input" / std::to_string(cell);
        Image pixels = blankImage(cell * 4, cell * 2);
        for (int y = 0; y < pixels.h; ++y) for (int x = 0; x < pixels.w; ++x) {
            auto* p = pixels.at(x, y);
            p[0] = static_cast<unsigned char>(30 + (x / cell) * 55);
            p[1] = static_cast<unsigned char>(60 + (y / cell) * 145);
            p[2] = static_cast<unsigned char>(230 - (x / cell) * 40);
            p[3] = (x % cell < 2 || y % cell < 2) ? 0 : 255;
        }
        const auto png = folder / fs::u8path("creación.png");
        write(png, encodePng(pixels));
        std::string xml = "<TextureAtlas imagePath=\"creación.png\">";
        std::string packer;
        for (int i = 0; i < 8; ++i) {
            const std::string name = "manual000" + std::to_string(i);
            xml += "<SubTexture name=\"" + name + "\" x=\"" + std::to_string((i % 4) * cell) + "\" y=\"" + std::to_string((i / 4) * cell) + "\" width=\"" + std::to_string(cell) + "\" height=\"" + std::to_string(cell) + "\"/>";
            packer += name + " = " + std::to_string((i % 4) * cell) + " " + std::to_string((i / 4) * cell) + " " + std::to_string(cell) + " " + std::to_string(cell) + "\n";
        }
        text(folder / "manual.xml", xml + "</TextureAtlas>");
        text(folder / "manual.txt", packer);
        std::vector<fs::path> frames;
        for (int i = 0; i < 3; ++i) {
            Image frame = blankImage(cell, cell);
            for (int y = 0; y < cell; ++y) for (int x = 0; x < cell; ++x)
                std::copy_n(pixels.at(x + i * cell, y), 4, frame.at(x, y));
            frames.push_back(folder / ("sequence" + std::to_string(i) + ".png"));
            write(frames.back(), encodePng(frame));
        }
        for (int mode = 0; mode < 6; ++mode) {
            CustomResource resource;
            resource.input.kind = ImportKind::Image;
            resource.input.path = png;
            if (mode == 1) resource.cellWidth = resource.cellHeight = cell;
            if (mode == 2) resource.regions = {{cell, 0, cell, cell}, {cell * 2, cell, cell, cell}, {0, cell, cell, cell}};
            if (mode == 3 || mode == 4) {
                std::string error;
                check(bindCustomAtlas(resource, folder / (mode == 3 ? "manual.xml" : "manual.txt"), error), "creation: manual XML/TXT binding on Unicode PNG");
            }
            if (mode == 5) {
                resource.input.kind = ImportKind::Frames;
                resource.input.files = frames;
                resource.input.animations = {{"sequence", 3}};
            }
            const auto inspection = inspectCustomResource(resource, 0);
            check(inspection.error.empty() && !inspection.frames.empty(), "creation: all six image import modes resolve actual pixels");
            if (inspection.frames.empty()) continue;
            if (mode == 1 || mode == 3 || mode == 4)
                check(inspection.frames.size() == 8 && inspection.frames.front().w == cell, "creation: atlas/grid frame count and cell size");
            if (mode == 2 || mode == 5)
                check(inspection.frames.size() == 3 && inspection.frames.front().w == cell, "creation: sequence/manual crops preserve three frames");
            CustomRecipe recipe;
            recipe.resources.push_back(resource);
            const int last = static_cast<int>(inspection.frames.size()) - 1;
            for (int part = 0; part < kPartCount; ++part) for (int lane = 0; lane < 4; ++lane) {
                CustomAssignment assignment;
                assignment.resource = 0; assignment.part = static_cast<Part>(part); assignment.direction = lane;
                assignment.order = {last, 0, last}; assignment.fps = 12.0f + lane * 6; assignment.pixel = cell == 16;
                assignment.loop = part == static_cast<int>(Part::Note) || part == static_cast<int>(Part::StrumStatic);
                recipe.assignments.push_back(assignment);
            }
            for (int h = 0; h < 19; ++h) {
                CustomAssignment assignment;
                assignment.role = CustomRole::HudImage; assignment.resource = 0; assignment.hudIndex = h; assignment.order = {h % static_cast<int>(inspection.frames.size())};
                recipe.assignments.push_back(assignment);
            }
            for (const auto source : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
                const auto built = buildCustomStyle(recipe, source, "Matrix");
                check(built.ok && built.style.parts.size() == static_cast<size_t>(kPartCount * 4) && built.style.hasHud, "creation: every note/receptor/hold/splash/cover/HUD role for each source engine");
                if (!built.ok) { for (const auto& error : built.errors) std::printf("matrix detail: %s\n", error.c_str()); continue; }
                check(built.style.parts.front().animation.fps == 12 && built.style.parts.front().animation.loop, "creation: FPS and loop survive construction");
                std::map<std::string, std::vector<unsigned char>> files;
                for (const auto& file : built.files) files[file.path] = file.bytes;
                ExportIo io;
                io.readBytes = [&](const std::string& path) -> std::optional<std::vector<unsigned char>> {
                    const auto it = files.find(path); return it == files.end() ? std::nullopt : std::optional<std::vector<unsigned char>>(it->second);
                };
                io.readText = [&](const std::string& path) -> std::optional<std::string> {
                    const auto data = io.readBytes(path); return data ? std::optional<std::string>(std::string(data->begin(), data->end())) : std::nullopt;
                };
                for (const auto target : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
                    for (const auto role : {ExportRole::ModSkin, ExportRole::SongSkin, ExportRole::NoteType, ExportRole::Selectable}) {
                        ExportOptions options;
                        options.target = target; options.role = role; options.name = "matrix"; options.noteType = "Matrix";
                        const auto& preset = blockPresets().front(); addPreset(options.blocks, preset);
                        auto package = buildExport(built.style, options, io);
                        if (!exportRoleAvailable(target, role)) {
                            check(package.hasErrors(), "export: unsupported engine roles fail explicitly");
                            continue;
                        }
                        check(!package.hasErrors(), "export: source/target/role cross-engine matrix builds");
                        if (package.hasErrors()) continue;
                        check(verifyExport(package, root / "matrix-verify"), "export: generated engine definitions and images reread successfully");
                        if (cell == 16 && mode == 1 && source == target &&
                            role == (target == Engine::VSlice ? ExportRole::SongSkin : ExportRole::ModSkin)) {
                            addInstallGuides(package);
                            std::string error;
                            check(writeExportFolder(package, root / "engine-exports" / engineKey(target) / "skin", error, false), "export: installable default skin prepared for actual engine test");
                        }
                        if (cell == 16 && mode == 1 && source == target && role == ExportRole::NoteType) {
                            options.blocks = {};
                            const int hit = newBlock(options.blocks, "event.hit");
                            options.blocks.node(hit)->args[0].value = "player";
                            const int log = newBlock(options.blocks, "do.log");
                            options.blocks.node(log)->args[0].value = "NOTELAB_QA_BLOCK_HIT";
                            placeTop(options.blocks, hit, 0, 0); attachAfter(options.blocks, hit, log);
                            options.noteType = "NoteLabQA"; options.name = "notelabqa";
                            auto gamePackage = buildExport(built.style, options, io);
                            check(verifyExport(gamePackage, root / "matrix-verify"), "export: logging custom-note package passes mandatory pre-write verification");
                            addInstallGuides(gamePackage);
                            std::string error;
                            check(!gamePackage.hasErrors() && writeExportFolder(gamePackage, root / "engine-exports" / engineKey(target) / "type", error, false), "export: custom type and generated logging block prepared for gameplay");
                        }
                        if (source == target && role == ExportRole::ModSkin && mode == 2) {
                            addInstallGuides(package);
                            const auto destination = root / "matrix-zip" / engineKey(target) / fs::u8path("José") / (std::to_string(cell) + ".zip");
                            std::string error;
                            check(writeExportZip(package, destination, error, false) && writeExportZip(package, destination, error, false), "export: Unicode ZIP first and repeat write");
                            auto vfs = mountVfs({destination});
                            const auto catalog = scanNoteStyles(*vfs);
                            check(!catalog.styles.empty(), "export: generated ZIP is readable as a source");
                        }
                    }
                }
                for (int bad = 0; bad < 5; ++bad) {
                    auto invalid = recipe;
                    if (bad == 0) invalid.assignments[0].resource = 999;
                    if (bad == 1) invalid.assignments[0].order = {9999};
                    if (bad == 2) invalid.assignments.push_back(invalid.assignments.front());
                    if (bad == 3) {
                        invalid.resources[0].input.path = root / "missing.png";
                        if (!invalid.resources[0].input.files.empty()) invalid.resources[0].input.files.front() = root / "missing.png";
                    }
                    if (bad == 4) invalid.assignments[kPartCount * 4].order = {0, 0};
                    const auto rejected = buildCustomStyle(invalid, source, "Bad");
                    check(!rejected.ok && !rejected.errors.empty(), "creation: invalid references, order, duplicate roles, missing files and animated ranking rejected");
                }
            }
        }
    }
    for (const auto engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        for (const auto& preset : blockPresets()) {
            BlockProgram program; addPreset(program, preset);
            program.comment = "Matrix comments · editable";
            for (auto& item : program.nodes) item.second.comment = std::string("Block ") + item.second.key;
            const auto generated = generateBlocks(engine, "Matrix", "matrix", program);
            check(!generated.files.empty(), "blocks: every preset produces target-engine files");
            for (const auto& file : generated.files) {
                const auto parsed = applyBlockSource(engine, "Matrix", "matrix", program, file.path, file.text);
                check(parsed.applied && writeProgram(parsed.program) == writeProgram(program), "blocks: generated code and editable comments roundtrip for every engine and preset");
                const auto spans = colorBlockSource(engine, file.text, fs::u8path(file.path).extension() == ".json");
                check(!spans.empty() && std::all_of(spans.begin(), spans.end(), [&](const auto& span) { return span.begin < span.end && span.end <= file.text.size(); }), "display: syntax-color spans stay within source text");
            }
        }
        for (const auto& definition : blockDefs()) {
            BlockProgram program;
            const int id = newBlock(program, definition.key);
            placeTop(program, id, 0, 0);
            if (definition.shape != BlockShape::Hat && (definition.places & (kPlaceCreate | kPlaceHit | kPlaceMiss))) {
                const char* event = (definition.places & kPlaceCreate) ? "event.create" : (definition.places & kPlaceHit) ? "event.hit" : "event.miss";
                const int hat = newBlock(program, event); placeTop(program, hat, 120, 0); attachAfter(program, hat, id);
            }
            const auto generated = generateBlocks(engine, "Matrix", "matrix", program);
            check(id > 0 && program.node(id) && !blockName(definition.key, false).empty() && !blockName(definition.key, true).empty(), "blocks: every palette block constructs and has EN/ES labels");
            const auto serialized = writeProgram(program);
            const bool emitsOrDiagnoses = !serialized.empty() && (!generated.files.empty() || !generated.conflicts.empty() || activeBlocks(program) == 0);
            check(emitsOrDiagnoses, "blocks: individual connected blocks generate or diagnose; unattached values remain dormant");
            if (!emitsOrDiagnoses) std::printf("block matrix detail: %s / %s\n", engineKey(engine), definition.key);
        }
    }
}

template<class Check>
void runRealEngineDisplayMatrix(const fs::path& root, Check check) {
    const std::pair<Engine, const char*> inputs[] = {{Engine::Codename, "NOTELAB_TEST_CODENAME"}, {Engine::Psych, "NOTELAB_TEST_PSYCH"}, {Engine::VSlice, "NOTELAB_TEST_VSLICE"}};
    for (const auto& input : inputs) {
        const char* path = std::getenv(input.second);
        if (!path || !*path) continue;
        NoteLabApp app; app.transient = true; app.headless = true; app.autoBase = false; app.musicVolume = 0.0f;
        chooseDemo(app); addSource(app, fs::u8path(path));
        check(app.sources.size() == 1 && !app.sources.front()->catalog.styles.empty(), "real-engine display: installed base assets produce styles");
        if (app.sources.empty()) continue;
        std::printf("Real input %s: %zu styles, %zu charts\n", engineKey(input.first), app.sources.front()->catalog.styles.size(), app.sources.front()->songs.size());
        for (size_t i = 0; i < app.sources.front()->catalog.styles.size(); ++i) {
            const auto& style = app.sources.front()->catalog.styles[i];
            if (style.engine != input.first) continue;
            selectStyle(app, 0, static_cast<int>(i));
            for (int dimensions : {0, 1, 2}) for (int lines : {0, 1, 2}) for (bool down : {false, true}) {
                app.visibleLines = lines; app.settings.downscroll = down;
                updatePlayback(app, 16.0f);
                const int width = dimensions == 0 ? 320 : dimensions == 1 ? 640 : 1280;
                const int height = dimensions == 0 ? 180 : dimensions == 1 ? 360 : 720;
                check(renderPreview(app, width, height).ok, "real-engine display: every loaded style renders in three sizes, both scroll directions and each side");
            }
        }
        int song = -1;
        for (size_t i = 0; i < app.sources.front()->songs.size(); ++i) {
            const auto& candidate = app.sources.front()->songs[i];
            if (candidate.id == "bopeebo" && candidate.difficulty == "normal") { song = static_cast<int>(i); break; }
        }
        if (song < 0 && !app.sources.front()->songs.empty()) song = 0;
        check(song >= 0, "real-engine chart: at least one playable source chart found");
        if (song >= 0) {
            chooseSong(app, 0, song);
            check(!app.notes.empty() && app.audioReady && app.audioPending, "real-engine chart: native notes load and instrumental/vocals decoding starts");
            app.audio.setMasterGain(0.0f);
            const auto audioDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (app.audioPending && std::chrono::steady_clock::now() < audioDeadline) {
                pollAudio(app);
                SDL_Delay(10);
            }
            check(app.audioLoaded && !app.audioPending && app.audio.trackCount() > 0 && app.audio.durationMs() > 1000.0, "real-engine audio: native instrumental/vocals decode to usable tracks");
            const auto peaks = app.audio.waveformPeaks(80);
            check(!peaks.empty() && std::any_of(peaks.begin(), peaks.end(), [](float value) { return value > 0.001f; }), "real-engine audio: decoded waveform contains actual non-silent samples");
            app.playing = true; app.manual = false;
            const double before = app.state.songMs;
            updatePlayback(app, 20.0f); SDL_Delay(40); updatePlayback(app, 20.0f);
            check(app.state.songMs > before && app.audio.playing() && SDL_WasInit(SDL_INIT_AUDIO) == 0, "real-engine chart: playback clock advances while block SDL cue device remains off");
            app.playing = false; updatePlayback(app, 0.0f);
            check(!app.audio.playing(), "real-engine audio: song pause stops general playback");
            audioSeek(app, 1500.0);
            check(std::abs(app.audio.positionMs() + app.audioOffsetMs - 1500.0) < 80.0, "real-engine audio: seeking follows the chart audio offset");
            const auto destination = root / "real-projects" / fs::u8path(std::string(engineKey(input.first)) + "-José.fmlnote");
            fs::create_directories(destination.parent_path());
            check(saveProjectTo(app, destination) && openProjectFile(app, destination), "real-engine project: native styles/chart save and reopen on Unicode paths");
        }
        if (app.audioReady) app.audio.shutdown();
        if (app.rendererReady) app.renderer.shutdown();
    }
}

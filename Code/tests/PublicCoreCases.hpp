#pragma once

void publicCoreCases(const fs::path& base) {
    std::printf("\n== Public v1: composition, receptors and project safety ==\n");
    const fs::path root = base / "composition";
    writeSheet(root / "images/game/notes/default", baseGameFrames(false, true));
    writeSheet(root / "images/game/notes/Custom", baseGameFrames(false, false));
    Scan scanned;
    scan(scanned, {root});
    const NoteStyle* defaultStyle = styleById(scanned, "codename:game/notes/default");
    const NoteStyle* customStyle = styleById(scanned, "codename:game/notes/Custom");
    expect(defaultStyle && customStyle, "composition fixture resolves separate note and receptor sheets");
    if (!defaultStyle || !customStyle) return;
    NoteStyle composed = *customStyle;
    const std::string notesImage = composed.sheets.front().image;
    StyleComponents receptors;
    expect(composeStyle(composed, *defaultStyle, receptors) == 12, "receptors can be composed independently");
    const PartBinding* note = findPart(composed, Part::Note, 0);
    const PartBinding* strum = findPart(composed, Part::StrumStatic, 0);
    expect(note && strum && note->sheet != strum->sheet && composed.sheets[note->sheet].image == notesImage,
           "composition preserves the chosen notes and remaps the receptor source");
    expect(composed.id == customStyle->id && composed.engine == customStyle->engine, "composition preserves target identity and engine");
    const auto sheetCount = composed.sheets.size();
    composeStyle(composed, *defaultStyle, receptors);
    expect(composed.sheets.size() == sheetCount && countParts(composed, Part::StrumStatic) == 4, "reapplying does not duplicate bindings or identical sheets");
    Sheet different = composed.sheets.front(); different.scale *= 2.0f; different.offsetX = 19.0f;
    const int scaled = addSheet(composed, different);
    expect(scaled != 0 && composed.sheets[scaled].offsetX == 19.0f, "different scale and offsets are not deduplicated as the same sheet");
    NoteStyle broken = *defaultStyle; broken.parts.front().sheet = 999;
    StyleComponents notes; notes.receptors = false; notes.notes = true;
    const int copied = composeStyle(composed, broken, notes);
    expect(copied == 11 && findPart(composed, Part::Note, 0), "bad donor bindings do not erase existing valid pieces");
    NoteStyle hud; hud.engine = Engine::VSlice; hud.hasHud = true;
    hud.judgements[0].image = "images/sick.png"; hud.judgements[0].scale = 0.8f;
    hud.combo.image = "images/combo.png"; hud.digits[0].image = "images/num0.png";
    hud.countdown[1].image = "images/ready.png";
    hud.sounds.push_back({"hit", "sounds/hit.ogg"});
    composed.countdown[1].image = "images/original-ready.png";
    StyleComponents ranking; ranking.receptors = false; ranking.judgements = true; ranking.combo = true;
    expect(composeStyle(composed, hud, ranking) == 3 && std::abs(composed.judgements[0].scale - 0.8f / 0.7f) < 0.001f,
           "cross-engine ranking composition preserves visible scale");
    expect(composed.countdown[1].image == "images/original-ready.png" && composed.sounds.empty(), "unselected countdown and sound groups are preserved");
    StyleComponents effects; effects.receptors = false; effects.sounds = true;
    composeStyle(composed, hud, effects); composeStyle(composed, hud, effects);
    expect(composed.sounds.size() == 1 && composed.sounds[0].path == "sounds/hit.ogg", "named effects keep one stable export identity");
    AtlasStore atlases = atlasStoreOf(scanned);
    NotePreview preview;
    PreviewSettings settings; settings.showOpponent = false;
    PreviewState state;
    RenderList list;
    preview.build(*customStyle, atlases, nullptr, settings, state, {}, list);
    expect(list.cmds.empty(), "partial note sheet does not invent receptor frames");
    preview.receptorFallback = defaultStyle;
    preview.build(*customStyle, atlases, nullptr, settings, state, {}, list);
    expect(list.cmds.size() == 4 && usesImage(list, defaultStyle->sheets[0].image), "four missing receptors come from the base sheet");
    state.strum.fill(StrumState::Confirm);
    list = {};
    preview.build(*customStyle, atlases, nullptr, settings, state, {}, list);
    expect(list.cmds.size() == 4, "confirm animations use the receptor fallback too");
    NoteStyle invisible = *defaultStyle; invisible.sheets[0].alpha = 0.0f;
    list = {};
    preview.build(invisible, atlases, nullptr, settings, state, {}, list);
    expect(list.cmds.size() == 4 && list.cmds[0].alpha == 0.0f, "an explicitly hidden receptor is not replaced by a visible fallback");
    NoteStyle invalidPrefix = *customStyle;
    for (int lane = 0; lane < 4; ++lane) { Animation a; a.prefix = "not-in-the-atlas"; bindPart(invalidPrefix, Part::StrumStatic, lane, 0, a); }
    state.strum.fill(StrumState::Static);
    list = {};
    preview.build(invalidPrefix, atlases, nullptr, settings, state, {}, list);
    expect(list.cmds.size() == 4, "declared but unresolved receptor prefixes fall back safely");
    std::vector<std::string> renamed = baseGameFrames(false, true);
    for (std::string& frame : renamed) {
        for (int lane = 0; lane < 4; ++lane) {
            const std::string from = std::string("arrow") + std::array<const char*, 4>{"LEFT", "DOWN", "UP", "RIGHT"}[lane];
            if (frame.rfind(from, 0) == 0) frame = std::string(directionKey(lane)) + " static0000";
        }
    }
    writeSheet(root / "images/game/notes/default", renamed);
    std::string mapping = "<keyData><keyGroup>";
    for (int lane = 0; lane < 4; ++lane) {
        const std::string d = directionKey(lane);
        mapping += "<key note=\"" + d + "0\" strumStatic=\"" + d + " static\" strumPress=\"" + d + " press\" strumConfirm=\"" + d + " confirm\"/>";
    }
    writeFile(root / "data/multikeyData.xml", mapping + "</keyGroup></keyData>");
    Scan renamedScan; scan(renamedScan, {root});
    const StyleReport* report = nullptr;
    const NoteStyle* restored = styleById(renamedScan, "codename:game/notes/default", &report);
    expect(restored && report && countErrors(*report) == 0 && findPart(*restored, Part::StrumStatic, 0)->animation.prefix == "left static",
           "declared multikey receptor names are resolved without mod-name exceptions");
    std::string error;
    expect(!readProject(R"({"format":false,"version":2})", error) && !error.empty(), "wrong format types are rejected without exceptions");
    expect(!readProject(std::string(kProjectByteLimit + 1, ' '), error), "projects larger than 16 MiB are rejected");
    const std::string nested = "{\"format\":\"fml-notelab-project\",\"version\":2,\"extra\":" + std::string(70, '[') + "0" + std::string(70, ']') + "}";
    expect(!readProject(nested, error), "deeply nested project data cannot overflow the parser stack");
    const std::string giant = "{\"format\":\"fml-notelab-project\",\"version\":2,\"extra\":\"" + std::string(1024u * 1024u + 1, 'x') + "\"}";
    expect(!readProject(giant, error), "individual oversized strings are rejected");
    const auto safe = readProject(R"({"format":"fml-notelab-project","version":2,"engineChoice":9223372036854775807,"view":{"bpm":1e80}})", error);
    expect(safe && safe->engineChoice == 0 && safe->view.bpm == 120.0f, "extreme numeric settings use safe defaults");
    NoteProject project;
    project.selectedStyle = "before";
    const fs::path unicode = base / fs::u8path("José") / "work.fmlnote";
    fs::create_directories(unicode.parent_path());
    writeFile(fs::path(unicode.wstring() + L".tmp"), "unrelated temporary file");
    expect(writeProjectFile(project, unicode, error), "first atomic project save accepts accented folders");
    project.selectedStyle = "after";
    expect(writeProjectFile(project, unicode, error), "a second atomic save replaces only the requested project");
    std::ifstream saved(unicode, std::ios::binary);
    const std::string savedText((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    const auto loaded = readProject(savedText, error);
    expect(loaded && loaded->selectedStyle == "after", "saved content can be reopened after replacement");
    std::ifstream tmp(fs::path(unicode.wstring() + L".tmp"), std::ios::binary);
    const std::string tmpText((std::istreambuf_iterator<char>(tmp)), std::istreambuf_iterator<char>());
    expect(tmpText == "unrelated temporary file", "saving never consumes a pre-existing .tmp file");
    expect(!writeProjectFile(project, unicode.parent_path(), error), "a directory cannot be overwritten as a project");
    expect(!writeProjectFile(project, base / "missing-folder" / "work.fmlnote", error), "missing destination folders fail cleanly");
    project.sources.resize(5);
    for (size_t i = 0; i < project.sources.size(); ++i) project.sources[i].path = "source-" + std::to_string(i);
    expect(!readProject(writeProject(project), error), "unsupported source counts are not silently truncated");
    expect(!writeProjectFile(project, unicode, error), "invalid project content cannot overwrite the last good save");
}

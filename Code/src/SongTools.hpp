#pragma once

void openCustomBot(NoteLabApp& app, int source, const std::string& type) {
    if (source < 0 || source >= static_cast<int>(app.sources.size())) return;
    auto& b = app.customBot; b.source = source; b.type = type;
    const auto saved = app.sources[static_cast<size_t>(source)]->bots.find(lowerText(type));
    b.profile = saved == app.sources[static_cast<size_t>(source)]->bots.end() ? BotProfile{} : saved->second;
    b.requestOpen = true;
}

void drawCustomBot(NoteLabApp& app) {
    auto& b = app.customBot;
    if (b.requestOpen) { ImGui::OpenPopup("###custombot"); b.requestOpen = false; }
    ImGui::SetNextWindowSize(ImVec2(520, 350), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Bot, tr(app, "Custom bot · v1", "Bot custom · v1")) + "###custombot").c_str(), &open)) return;
    if (b.source < 0 || b.source >= static_cast<int>(app.sources.size())) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    Source& source = *app.sources[static_cast<size_t>(b.source)];
    ImGui::TextUnformatted(ui::label(ui::icon::Puzzle, b.type).c_str()); ui::caption(sourceName(source).c_str());
    ImGui::Checkbox(tr(app, "Use this custom bot profile", "Usar este perfil de bot custom"), &b.profile.enabled);
    const char* options[] = {tr(app, "Use the mod / blocks rule", "Usar la regla del mod / bloques"), tr(app, "Hit this note", "Tocar esta nota"), tr(app, "Avoid this note", "Evitar esta nota")};
    ImGui::TextUnformatted(tr(app, "Opponent", "Rival"));
    ImGui::SetNextItemWidth(-1); ImGui::Combo("##botopponent", &b.profile.opponent, options, 3);
    ImGui::TextUnformatted(tr(app, "Player", "Jugador"));
    ImGui::SetNextItemWidth(-1); ImGui::Combo("##botplayer", &b.profile.player, options, 3);
    ImGui::TextWrapped("%s", tr(app, "This controls the preview bot and is saved in the project. It does not execute the mod's scripts or change engine gameplay. Hitting a harmful note can still count as a miss.",
        "Controla el bot de la preview y se guarda en el proyecto. No ejecuta scripts del mod ni cambia el juego del motor. Tocar una nota dañina todavía puede contar como fallo."));
    ImGui::Separator();
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ui::primaryButton(tr(app, "Apply profile", "Aplicar perfil"))) {
        source.bots[lowerText(b.type)] = b.profile; applyTypeRules(app); app.dirty = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

fs::path canonicalChartDestination(const fs::path& target) {
    std::error_code ec;
    auto cursor = fs::absolute(target, ec).lexically_normal();
    if (ec) return {};
    fs::path tail;
    while (!cursor.empty()) {
        auto resolved = fs::canonical(cursor, ec);
        if (!ec) {
            if (!tail.empty()) resolved /= tail;
            resolved = resolved.lexically_normal(); resolved.make_preferred(); return resolved;
        }
        const auto parent = cursor.parent_path();
        if (parent == cursor) break;
        tail = tail.empty() ? cursor.filename() : cursor.filename() / tail;
        cursor = parent;
    }
    return {};
}

bool protectedChartTarget(const NoteLabApp& app, const fs::path& target) {
    const auto wanted = canonicalChartDestination(target);
    if (wanted.empty()) return true;
    auto within = [&](const fs::path& folder) {
        if (folder.empty()) return false;
        const auto root = canonicalChartDestination(folder);
        if (root.empty()) return false;
        auto a = wanted.begin();
        for (auto b = root.begin(); b != root.end(); ++a, ++b)
            if (a == wanted.end() || lowerText(a->u8string()) != lowerText(b->u8string())) return false;
        return true;
    };
    for (const auto& base : app.baseRoots) if (!base.empty() && within(baseMountOf(base))) return true;
    for (const auto& source : app.sources) {
        if (source->baseOnly && within(source->root)) return true;
        if (source->withBase && within(baseMountOf(source->basePath))) return true;
    }
    return false;
}

bool prepareChartSave(NoteLabApp& app) {
    auto& s = app.chartSave; s.error.clear(); s.backup.clear(); s.canReplace = false; s.original.clear();
    s.source = app.songSource; s.song = app.songIndex;
    if (s.source < 0 || s.source >= static_cast<int>(app.sources.size()) || s.song < 0 || s.song >= static_cast<int>(app.sources[static_cast<size_t>(s.source)]->songs.size())) return false;
    Source& source = *app.sources[static_cast<size_t>(s.source)]; const auto& song = source.songs[static_cast<size_t>(s.song)];
    s.text = patchSongNoteTypes(app.chartOriginal, song, app.notes, app.noteTypes, s.error);
    if (s.text.empty()) { setStatus(app, "Cannot save chart: " + s.error, "No se puede guardar el chart: " + s.error); return false; }
    const auto file = source.vfs->find(song.path);
    if (file && !file->fromArchive) {
        const auto resolved = source.vfs->resolve(*file);
        if (resolved) { s.original = *resolved; s.canReplace = file->rootIndex != source.baseRootIndex && !protectedChartTarget(app, *resolved); }
    }
    return true;
}

bool commitChartSave(NoteLabApp& app, const fs::path& target, bool replaceOriginal) {
    auto& s = app.chartSave;
    if (s.source != app.songSource || s.song != app.songIndex || (replaceOriginal && !s.canReplace)) {
        setStatus(app, "The active chart changed, or its source is read-only. Open the save dialog again.",
                  "El chart activo cambió o su origen es de sólo lectura. Abre de nuevo el diálogo.");
        s.error = app.spanish ? app.statusEs : app.statusEn; return false;
    }
    if (protectedChartTarget(app, target)) {
        setStatus(app, "Choose a separate destination outside the base-game files.",
                  "Elige un destino separado fuera de los archivos del juego base.");
        s.error = app.spanish ? app.statusEs : app.statusEn; return false;
    }
    if (!saveSongChart(target, s.text, app.chartExpected, replaceOriginal, s.backup, s.error)) {
        setStatus(app, "Chart was not replaced: " + s.error, "No se reemplazó el chart: " + s.error); return false;
    }
    if (replaceOriginal) {
        app.chartOriginal = s.text; app.chartExpected = s.text; app.chartTypes = app.noteTypes; app.distributed = false;
        app.distributionPreviewKey.clear();
        Source& source = *app.sources[static_cast<size_t>(s.source)];
        source.vfs->notifyFileWritten(source.songs[static_cast<size_t>(s.song)].path);
    }
    const std::string backup = s.backup.empty() ? "" : " · Backup: " + s.backup.u8string();
    setStatus(app, "Chart saved: " + target.u8string() + backup, "Chart guardado: " + target.u8string() + backup);
    app.dirty = true; return true;
}

void drawChartSave(NoteLabApp& app, SDL_Window* window) {
    auto& s = app.chartSave;
    if (s.requestOpen) { ImGui::OpenPopup("###chartdistribution"); s.requestOpen = false; }
    ImGui::SetNextWindowSize(ImVec2(670, 430), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Music, tr(app, "Save chart distribution", "Guardar distribución del chart")) + "###chartdistribution").c_str(), &open)) return;
    ui::sectionHeader(app.songLabel.c_str(), ui::icon::Music);
    ImGui::TextWrapped("%s", tr(app, "Only note types are changed. Events, timing, sustain lengths, other difficulties and unknown fields stay in the original document.",
        "Sólo cambian los tipos de nota. Se conservan los eventos, tiempos, sostenidos, otras dificultades y campos desconocidos del documento."));
    if (ui::primaryButton(tr(app, "Save as a separate chart…", "Guardar como chart separado…"), ImVec2(-1, 0))) {
        openDialog(app, window, DialogAction::SaveChartCopy); ImGui::CloseCurrentPopup();
    }
    ui::caption(tr(app, "Folder or ZIP sources: this option does not alter the source. An existing destination also gets a backup.",
        "Origen en carpeta o ZIP: esta opción no lo modifica. Si el destino ya existe, también se hace backup."));
    if (!s.original.empty()) ImGui::TextWrapped("%s", s.original.u8string().c_str());
    if (!s.canReplace) ui::caption(tr(app, "Replacing the source is disabled for ZIPs and base-game files.", "Reemplazar el origen está desactivado para ZIP y archivos del juego base."));
    ImGui::BeginDisabled(!s.canReplace);
    if (ImGui::Button(tr(app, "Replace original WITH BACKUP", "Reemplazar original CON BACKUP"), ImVec2(-1, 0)))
        if (commitChartSave(app, s.original, true)) ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();
    ui::caption(tr(app, "The old file is preserved as .notelab-backup-N.bak. If the chart changed on disk, replacement is blocked.",
        "El archivo anterior se conserva como .notelab-backup-N.bak. Si cambió en disco, se bloquea el reemplazo."));
    if (ImGui::Button(tr(app, "Copy chart JSON", "Copiar JSON del chart"))) copyText(app, s.text);
    ImGui::SameLine(); if (ImGui::Button(tr(app, "Cancel", "Cancelar"))) ImGui::CloseCurrentPopup();
    if (!s.error.empty()) ImGui::TextWrapped("%s", s.error.c_str());
    ImGui::EndPopup();
}

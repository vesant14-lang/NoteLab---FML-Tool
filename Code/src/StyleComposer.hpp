#pragma once

void openStyleComposer(NoteLabApp& app) {
    if (!selectedStyle(app)) return;
    auto& c = app.composer;
    c = {};
    c.targetSource = app.selSource;
    c.targetStyle = app.selStyle;
    c.components = {};
    for (size_t s = 0; s < app.sources.size(); ++s)
        for (size_t i = 0; i < app.sources[s]->catalog.styles.size(); ++i) {
            const NoteStyle& style = app.sources[s]->catalog.styles[i];
            if (style.engine == selectedStyle(app)->engine && style.use == StyleUse::Default &&
                (static_cast<int>(s) != c.targetSource || static_cast<int>(i) != c.targetStyle)) {
                c.donorSource = static_cast<int>(s);
                c.donorStyle = static_cast<int>(i);
                break;
            }
        }
    c.requestOpen = true;
}

bool applyStyleComposition(NoteLabApp& app, int donorSource, int donorStyle, const StyleComponents& components) {
    Source* destination = selectedSource(app);
    const NoteStyle* original = selectedStyle(app);
    if (!destination || !original || donorSource < 0 || donorSource >= static_cast<int>(app.sources.size())) return false;
    Source& donorOwner = *app.sources[static_cast<size_t>(donorSource)];
    if (donorStyle < 0 || donorStyle >= static_cast<int>(donorOwner.catalog.styles.size())) return false;
    const NoteStyle donor = donorOwner.catalog.styles[static_cast<size_t>(donorStyle)];
    NoteStyle candidate = *original;
    const int combined = composeStyle(candidate, donor, components);
    if (combined == 0) { app.composer.message = tr(app, "No usable pieces in the selected groups.", "Los grupos elegidos no tienen piezas utilizables."); return false; }
    struct MountedFile { std::string virtualPath; fs::path disk; };
    std::vector<MountedFile> mounted;
    std::map<std::string, std::string> paths;
    size_t bytesUsed = 0;
    const std::string prefix = "notelab-compose/" + std::to_string(destination->imports + 1) + "/";
    auto transfer = [&](std::string& value) {
        if (value.empty() || &donorOwner == destination) return true;
        const auto old = paths.find(value);
        if (old != paths.end()) { value = old->second; return true; }
        const auto bytes = donorOwner.vfs->readBytes(value, 64u * 1024u * 1024u);
        if (!bytes || bytes->empty() || bytesUsed + bytes->size() > 256u * 1024u * 1024u) return false;
        bytesUsed += bytes->size();
        const std::string digest = sha256Hex(std::string(bytes->begin(), bytes->end()));
        const std::string name = digest + "-" + pathFromUtf8(value).filename().u8string();
        const fs::path file = settingsFolder() / "composed" / pathFromUtf8(name);
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        if (ec) return false;
        bool available = false;
        if (fs::is_regular_file(file, ec) && !ec && fs::file_size(file, ec) == bytes->size() && !ec) {
            std::ifstream existing(file, std::ios::binary);
            const std::string data((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
            available = sha256Hex(data) == digest;
        }
        if (!available) {
            std::ofstream output(file, std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
            output.flush();
            if (!output) return false;
            output.close();
            if (!output) return false;
        }
        const std::string virtualPath = prefix + name;
        mounted.push_back({virtualPath, file});
        paths.emplace(value, virtualPath);
        value = virtualPath;
        return true;
    };
    NoteStyle imported = donor;
    for (size_t index = 0; index < imported.sheets.size(); ++index) {
        Sheet& sheet = imported.sheets[index];
        const bool used = std::any_of(imported.parts.begin(), imported.parts.end(), [&](const PartBinding& binding) {
            return components.includes(binding.part) && binding.sheet == static_cast<int>(index);
        });
        if (used && (!transfer(sheet.image) || !transfer(sheet.atlas))) {
            app.composer.message = tr(app, "A selected image or atlas could not be copied. No style was changed.", "No se pudo copiar una imagen o atlas. El estilo no cambia.");
            return false;
        }
    }
    auto hud = [&](HudAsset& asset) { return transfer(asset.image) && transfer(asset.sound); };
    bool ready = true;
    if (components.judgements) for (auto& asset : imported.judgements) ready = ready && hud(asset);
    if (components.combo) { ready = ready && hud(imported.combo); for (auto& asset : imported.digits) ready = ready && hud(asset); }
    if (components.countdown) for (auto& asset : imported.countdown) ready = ready && hud(asset);
    if (components.sounds) for (auto& sound : imported.sounds) ready = ready && transfer(sound.path);
    if (!ready) { app.composer.message = tr(app, "A selected HUD resource is missing. No style was changed.", "Falta un recurso del HUD elegido. El estilo no cambia."); return false; }
    candidate = *original;
    if (!composeStyle(candidate, imported, components)) return false;
    for (const auto& item : mounted)
        if (!destination->vfs->pushFile(item.disk, item.virtualPath, "composition")) {
            app.composer.message = tr(app, "Cannot mount the copied resource. No style was changed.", "No se puede montar el recurso copiado. El estilo no cambia."); return false;
        }
    for (const auto& item : mounted)
        destination->importedFiles.push_back({item.disk.u8string(), {}, item.virtualPath, {}});
    if (!mounted.empty()) ++destination->imports;
    beginEdit(app);
    *mutableStyle(app) = std::move(candidate);
    afterEdit(app);
    app.preview.clearCache();
    app.buffersFor.clear();
    app.exporting.preparedKey.clear();
    app.composer.message.clear();
    setStatus(app, std::to_string(combined) + " pieces combined. The mod files were not changed.",
                  std::to_string(combined) + " piezas combinadas. Los archivos del mod no cambian.");
    return true;
}

void drawStyleComposer(NoteLabApp& app) {
    auto& c = app.composer;
    if (c.requestOpen) { ImGui::OpenPopup("###stylecomposer"); c.requestOpen = false; }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(650.0f, viewport->WorkSize.x - 24.0f), std::min(520.0f, viewport->WorkSize.y - 32.0f)), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((std::string(tr(app, "Combine styles / HUD", "Combinar estilos / HUD")) + "###stylecomposer").c_str(), &open)) return;
    const NoteStyle* target = selectedStyle(app);
    if (!target || app.selSource != c.targetSource || app.selStyle != c.targetStyle) {
        ui::caption(tr(app, "The target changed. Reopen this panel.", "Cambió el destino. Abre este panel otra vez."));
        if (ImGui::Button(tr(app, "Close", "Cerrar"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup(); return;
    }
    ImGui::TextColored(ui::vec(ui::color::Accent), "%s: %s", tr(app, "Editing", "Editando"), target->name.c_str());
    ui::caption(tr(app, "Choose a source, then the groups to take. Other pieces keep their own files.", "Elige una fuente y los grupos que quieres tomar. Las demás piezas conservan sus archivos."));
    const NoteStyle* donor = nullptr;
    if (c.donorSource >= 0 && c.donorSource < static_cast<int>(app.sources.size())) {
        const auto& styles = app.sources[static_cast<size_t>(c.donorSource)]->catalog.styles;
        if (c.donorStyle >= 0 && c.donorStyle < static_cast<int>(styles.size())) donor = &styles[static_cast<size_t>(c.donorStyle)];
    }
    const std::string title = donor ? sourceName(*app.sources[static_cast<size_t>(c.donorSource)]) + " / " + donor->name : tr(app, "Select source…", "Elegir fuente…");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##composesource", title.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (size_t s = 0; s < app.sources.size(); ++s) {
            ImGui::PushID(static_cast<int>(s));
            for (size_t i = 0; i < app.sources[s]->catalog.styles.size(); ++i) {
                if (static_cast<int>(s) == c.targetSource && static_cast<int>(i) == c.targetStyle) continue;
                ImGui::PushID(static_cast<int>(i));
                const NoteStyle& choice = app.sources[s]->catalog.styles[i];
                const std::string text = sourceName(*app.sources[s]) + " / " + choice.name + " · " + engineLabel(choice.engine);
                if (ImGui::Selectable(text.c_str(), c.donorSource == static_cast<int>(s) && c.donorStyle == static_cast<int>(i))) {
                    c.donorSource = static_cast<int>(s); c.donorStyle = static_cast<int>(i); c.message.clear();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::Checkbox(tr(app, "Notes and holds", "Notas y sostenidos"), &c.components.notes);
    ImGui::Checkbox(tr(app, "Receptors: static, press and confirm", "Receptores: reposo, pulsación y confirmación"), &c.components.receptors);
    ImGui::Checkbox(tr(app, "Splashes", "Salpicaduras"), &c.components.splashes);
    ImGui::Checkbox(tr(app, "Hold covers", "Coberturas de sostenidos"), &c.components.holdCovers);
    ImGui::Checkbox(tr(app, "Judgements", "Juicios"), &c.components.judgements);
    ImGui::Checkbox(tr(app, "Combo and digits", "Combo y cifras"), &c.components.combo);
    ImGui::Checkbox(tr(app, "Countdown and its sounds", "Cuenta atrás y sus sonidos"), &c.components.countdown);
    ImGui::Checkbox(tr(app, "Named sound effects", "Efectos de sonido con nombre"), &c.components.sounds);
    ui::caption(tr(app, "Saved with the project. Undo restores the previous composition. Engine export limits still apply.",
                        "Se guarda en el proyecto. Deshacer recupera la composición anterior. Se mantienen los límites del motor al exportar."));
    if (!c.message.empty()) ImGui::TextWrapped("%s", c.message.c_str());
    ImGui::BeginDisabled(!donor);
    if (ui::primaryButton(tr(app, "Apply composition", "Aplicar combinación")) && applyStyleComposition(app, c.donorSource, c.donorStyle, c.components)) ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

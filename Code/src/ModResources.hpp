#pragma once

const std::vector<ModResource>& modResourceCatalog(Source& source, bool refresh = false) {
    if (refresh || source.resourcesFrom != source.vfs.get()) {
        source.resourceCatalog = source.vfs ? scanModResources(*source.vfs, true) : std::vector<ModResource>{};
        source.resourcesFrom = source.vfs.get();
    }
    return source.resourceCatalog;
}

void refreshMediaCatalog(NoteLabApp& app, bool refresh = false) {
    auto& m = app.media;
    if (m.source < 0 || m.source >= static_cast<int>(app.sources.size())) return;
    Source& source = *app.sources[m.source];
    m.resources.clear(); m.usedPaths.clear();
    for (const auto& resource : modResourceCatalog(source, refresh))
        if (m.includeBase || !resource.base) m.resources.push_back(resource);
    auto found = source.typeBlocks.find(m.type);
    if (found != source.typeBlocks.end())
        for (const auto& use : blockResourceUses(found->second))
            if (const auto* resolved = findModResource(m.resources, use.path, use.kind)) m.usedPaths.insert(resolved->path);
}

void clearMediaPreview(NoteLabApp& app) {
    if (app.media.audio) app.media.audio->shutdown();
    app.media.audio.reset();
    app.media.startWhenReady = false;
    app.media.audioRange = false;
    app.media.audioStart = app.media.audioEnd = 0.0f;
    if (!app.media.imagePath.empty() && app.rendererReady) app.renderer.removeDynamicFrame(app.media.imagePath);
    app.media.imagePath.clear();
    app.media.pan = {0.0f, 0.0f};
    app.media.zoom = 1.0f;
}

void openModResources(NoteLabApp& app, int source, const std::string& type, int block = -1, int argument = -1, ResourceKind kind = ResourceKind::Image) {
    if (source < 0 || source >= static_cast<int>(app.sources.size()) || !app.sources[source]->vfs) return;
    clearMediaPreview(app);
    auto& m = app.media;
    m.source = source; m.type = type; m.block = block; m.argument = argument;
    m.category = static_cast<int>(kind); m.selected.clear(); m.message.clear(); m.search.fill(0);
    if (app.sources[source]->baseOnly) m.includeBase = true;
    refreshMediaCatalog(app);
    auto found = app.sources[source]->typeBlocks.find(type);
    if (found != app.sources[source]->typeBlocks.end()) {
        const auto* node = found->second.node(block);
        if (node && argument >= 0 && argument < static_cast<int>(node->args.size())) {
            const auto* resource = findModResource(m.resources, node->args[argument].value, kind);
            if (resource) m.selected = resource->path;
        }
    }
    m.requestOpen = true;
}

bool assignModResource(NoteLabApp& app, const ModResource& resource) {
    auto& m = app.media;
    if (m.source < 0 || m.source >= static_cast<int>(app.sources.size())) return false;
    auto found = app.sources[m.source]->typeBlocks.find(m.type);
    if (found == app.sources[m.source]->typeBlocks.end()) return false;
    auto* node = found->second.node(m.block);
    const auto* def = node ? blockDef(node->key) : nullptr;
    if (!def || m.argument < 0 || m.argument >= static_cast<int>(def->args.size()) || m.argument >= static_cast<int>(node->args.size())) return false;
    const ArgKind expected = def->args[m.argument].kind;
    if ((expected == ArgKind::Image && resource.kind != ResourceKind::Image) ||
        (expected == ArgKind::Video && resource.kind != ResourceKind::Video) ||
        (expected == ArgKind::Sound && resource.kind != ResourceKind::Sound) ||
        (expected != ArgKind::Image && expected != ArgKind::Video && expected != ArgKind::Sound)) return false;
    nlblocks::remember(app.canvas, found->second);
    node->args[m.argument].value = resource.path;
    node->args[m.argument].block = -1;
    if (m.applySettings) {
        const std::string fit = m.fitMode == 1 ? "cover" : m.fitMode == 2 ? "stretch" : "contain";
        if (node->key == "do.image") { node->args[1].value = std::to_string(m.seconds); node->args[2].value = std::to_string(m.opacity); node->args[3].value = fit; }
        if (node->key == "do.video") { node->args[1].value = std::to_string(m.seconds); node->args[2].value = std::to_string(m.volume); node->args[3].value = fit; }
        if (node->key == "do.screamer") { node->args[2].value = std::to_string(m.seconds); node->args[3].value = std::to_string(m.volume); node->args[5].value = fit; }
        if (node->key == "do.sound") node->args[1].value = std::to_string(m.volume);
    }
    blocksChanged(app);
    return true;
}

bool openMediaExternal(NoteLabApp& app, Source& source, const ModResource& resource) {
    if (!source.vfs || !resourceKindOf(resource.path)) return false;
    const auto real = source.vfs->resolve(resource.path);
    if (!real || !fs::is_regular_file(*real)) return false;
#ifdef _WIN32
    const auto result = ShellExecuteW(nullptr, L"open", real->wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        app.media.message = tr(app, "No associated player was available. Use Show in folder.", "No hay un reproductor asociado. Usa Mostrar en la carpeta.");
        return false;
    }
    return true;
#else
    return false;
#endif
}

void selectMediaResource(NoteLabApp& app, Source& source, const ModResource& resource) {
    clearMediaPreview(app);
    auto& m = app.media;
    m.selected = resource.path; m.message.clear();
    if (resource.kind != ResourceKind::Image || !app.rendererReady) return;
    const auto bytes = source.vfs->readBytes(resource.path, 32u * 1024u * 1024u);
    Image image;
    if (!bytes || !decodePng(*bytes, image)) {
        m.message = tr(app, "Inline image preview supports PNG. Other formats can be opened in the system viewer.",
            "La preview integrada admite PNG. Otros formatos se pueden abrir en el visor del sistema.");
        return;
    }
    std::vector<unsigned char> bgra = image.rgba;
    for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    m.imagePath = "notelab-live/mod-resource.png";
    if (!app.renderer.uploadDynamicFrame(m.imagePath, bgra.data(), image.w, image.h)) m.imagePath.clear();
}

void auditionMediaResource(NoteLabApp& app, Source& source, const ModResource& resource) {
    auto& m = app.media;
    if (resource.bytes > 32u * 1024u * 1024u) { m.message = tr(app, "Audio exceeds the 32 MB audition limit.", "El audio supera el límite de prueba de 32 MB."); return; }
    const auto real = source.vfs->resolve(resource.path);
    if (!real) { m.message = tr(app, "Resource could not be resolved.", "No se pudo resolver el recurso."); return; }
    if (m.audio) m.audio->shutdown();
    m.audio = std::make_unique<AudioEngine>();
    std::string error;
    if (!m.audio->init(&error)) { m.message = error; m.audio.reset(); return; }
    m.audio->setMasterGain(m.volume);
    m.audio->loadTracksAsync({real->u8string()});
    m.startWhenReady = true;
}

void applyMediaAudioSettings(NoteLabApp& app) {
    auto& m = app.media;
    m.volume = std::clamp(std::isfinite(m.volume) ? m.volume : 0.5f, 0.0f, 1.0f);
    m.audioSpeed = std::clamp(std::isfinite(m.audioSpeed) ? m.audioSpeed : 1.0f, 0.25f, 2.0f);
    m.audioBalance = std::clamp(std::isfinite(m.audioBalance) ? m.audioBalance : 0.0f, -1.0f, 1.0f);
    if (!m.audio) return;
    m.audio->setMasterGain(m.volume);
    m.audio->setRate(m.audioSpeed);
    m.audio->setTrackPan(0, m.audioBalance);
    const float duration = static_cast<float>(m.audio->durationMs() / 1000.0);
    if (duration <= 0.0f) { m.audio->clearLoop(); return; }
    const float span = std::min(0.01f, duration);
    m.audioStart = std::clamp(std::isfinite(m.audioStart) ? m.audioStart : 0.0f, 0.0f, duration - span);
    m.audioEnd = std::clamp(std::isfinite(m.audioEnd) && m.audioEnd > 0.0f ? m.audioEnd : duration, m.audioStart + span, duration);
    if (m.audioLoop) m.audio->setLoop(m.audioRange ? m.audioStart * 1000.0 : 0.0, m.audioRange ? m.audioEnd * 1000.0 : m.audio->durationMs());
    else m.audio->clearLoop();
}

void pollMediaAudition(NoteLabApp& app) {
    auto& m = app.media;
    if (!m.audio || !m.audio->ready()) return;
    int loaded = 0; std::string error;
    if (!m.audio->pollTrackLoad(&loaded, &error)) return;
    const bool play = m.startWhenReady;
    m.startWhenReady = false;
    if (loaded <= 0) { m.message = error; return; }
    applyMediaAudioSettings(app);
    if (play) {
        m.audio->seekMs(m.audioRange ? m.audioStart * 1000.0 : 0.0);
        m.audio->play();
    }
}

void resumeMediaAudition(NoteLabApp& app) {
    auto& m = app.media;
    if (!m.audio || !m.audio->trackCount()) return;
    applyMediaAudioSettings(app);
    const double at = m.audio->positionMs();
    if (m.audioLoop && m.audioRange && (at < m.audioStart * 1000.0 || at >= m.audioEnd * 1000.0))
        m.audio->seekMs(m.audioStart * 1000.0);
    m.audio->play();
}

void stopMediaAudition(NoteLabApp& app) {
    auto& m = app.media;
    m.startWhenReady = false;
    if (m.audio) { m.audio->pause(); m.audio->seekMs(m.audioRange ? m.audioStart * 1000.0 : 0.0); }
}

void drawMediaAudioControls(NoteLabApp& app) {
    auto& m = app.media;
    pollMediaAudition(app);
    auto rect = []() {
        const auto at = ImGui::GetItemRectMin(), end = ImGui::GetItemRectMax();
        return ImVec4(at.x, at.y, end.x - at.x, end.y - at.y);
    };
    auto sameLineIfFits = [](const char* text, float padding) {
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (right - ImGui::GetItemRectMax().x >= ImGui::CalcTextSize(text).x + padding + ImGui::GetStyle().ItemSpacing.x)
            ImGui::SameLine();
    };
    const bool ready = m.audio && m.audio->ready() && m.audio->trackCount() && !m.audio->trackLoadPending();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(m.audio && m.audio->playing() ? tr(app, "Pause", "Pausar") : tr(app, "Play / resume", "Reproducir / continuar"))) {
        if (m.audio->playing()) m.audio->pause(); else resumeMediaAudition(app);
    }
    m.pauseRect = rect();
    sameLineIfFits(tr(app, "Stop", "Detener"), ImGui::GetStyle().FramePadding.x * 2.0f);
    if (ImGui::Button(tr(app, "Stop", "Detener"))) stopMediaAudition(app);
    m.stopRect = rect();
    ImGui::EndDisabled();
    sameLineIfFits(tr(app, "Loop", "Repetir"), ImGui::GetStyle().FramePadding.x * 2.0f + 24.0f);
    if (ui::toggle("media-loop", tr(app, "Loop", "Repetir"), &m.audioLoop)) {
        applyMediaAudioSettings(app);
        if (ready && m.audio->playing() && m.audioLoop && m.audioRange &&
            (m.audio->positionMs() < m.audioStart * 1000.0 || m.audio->positionMs() >= m.audioEnd * 1000.0))
            m.audio->seekMs(m.audioStart * 1000.0);
    }
    m.loopRect = rect();
    if (m.startWhenReady) ui::caption(tr(app, "Loading audio…", "Cargando audio…"));
    const float duration = ready ? static_cast<float>(m.audio->durationMs() / 1000.0) : 0.0f;
    ui::caption(tr(app, "Time", "Tiempo"));
    float time = ready ? static_cast<float>(m.audio->positionMs() / 1000.0) : 0.0f;
    ImGui::BeginDisabled(!ready);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##mediaseek", &time, 0.0f, duration, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) m.audio->seekMs(time * 1000.0);
    ImGui::EndDisabled();
    ui::caption(tr(app, "Volume", "Volumen"));
    float percent = m.volume * 100.0f;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##mediavolume", &percent, 0.0f, 100.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp)) { m.volume = percent / 100.0f; applyMediaAudioSettings(app); }
    ui::caption(tr(app, "Speed · also changes pitch", "Velocidad · también cambia el tono"));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##mediaspeed", &m.audioSpeed, 0.25f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) applyMediaAudioSettings(app);
    m.speedRect = rect();
    ui::caption(tr(app, "Stereo balance · left / center / right", "Balance estéreo · izquierda / centro / derecha"));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##mediabalance", &m.audioBalance, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) applyMediaAudioSettings(app);
    m.balanceRect = rect();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Checkbox(tr(app, "Custom A–B loop range", "Rango A–B para repetir"), &m.audioRange)) {
        applyMediaAudioSettings(app);
        if (m.audioLoop && m.audioRange && m.audio->playing()) m.audio->seekMs(m.audioStart * 1000.0);
    }
    m.rangeRect = rect();
    if (m.audioRange) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloatRange2("##mediarange", &m.audioStart, &m.audioEnd, 0.05f, 0.0f, duration, "A %.2f s", "B %.2f s", ImGuiSliderFlags_AlwaysClamp)) applyMediaAudioSettings(app);
        if (ImGui::Button(tr(app, "Set A here", "Marcar A aquí"))) { m.audioStart = time; applyMediaAudioSettings(app); }
        sameLineIfFits(tr(app, "Set B here", "Marcar B aquí"), ImGui::GetStyle().FramePadding.x * 2.0f);
        if (ImGui::Button(tr(app, "Set B here", "Marcar B aquí"))) { m.audioEnd = time; applyMediaAudioSettings(app); }
        if (!m.audioLoop) ui::caption(tr(app, "Enable Loop to repeat this range.", "Activa Repetir para usar este rango."));
    }
    ImGui::EndDisabled();
    if (ImGui::SmallButton(tr(app, "Reset audio settings", "Restablecer ajustes de audio"))) {
        m.audioLoop = m.audioRange = false; m.audioSpeed = 1.0f; m.audioBalance = 0.0f;
        m.audioStart = 0.0f; m.audioEnd = duration;
        applyMediaAudioSettings(app);
    }
    ui::caption(tr(app, "Full-length playback. Preview settings do not change the file, song or exported block.",
        "Reproducción completa. Los ajustes de prueba no cambian el archivo, la canción ni el bloque exportado."));
}

void drawMediaImage(NoteLabApp& app) {
    auto& m = app.media;
    const auto texture = app.renderer.previewImage(m.imagePath);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##mediacanvas", available, ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    auto& io = ImGui::GetIO();
    const float fit = std::min(available.x / std::max(1, texture.width), available.y / std::max(1, texture.height));
    const ImVec2 center(at.x + available.x * 0.5f + m.pan.x, at.y + available.y * 0.5f + m.pan.y);
    if (hovered && io.MouseWheel != 0.0f) {
        const float old = m.zoom;
        m.zoom = std::clamp(m.zoom * std::pow(1.15f, io.MouseWheel), 0.1f, 20.0f);
        m.pan.x -= (io.MousePos.x - center.x) * (m.zoom / old - 1.0f);
        m.pan.y -= (io.MousePos.y - center.y) * (m.zoom / old - 1.0f);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m.pan.x += io.MouseDelta.x; m.pan.y += io.MouseDelta.y;
    }
    const ImVec2 size(texture.width * fit * m.zoom, texture.height * fit * m.zoom);
    const ImVec2 origin(at.x + available.x * 0.5f + m.pan.x - size.x * 0.5f, at.y + available.y * 0.5f + m.pan.y - size.y * 0.5f);
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(at, ImVec2(at.x + available.x, at.y + available.y), true);
    draw->AddRectFilled(at, ImVec2(at.x + available.x, at.y + available.y), IM_COL32(12, 14, 18, 255));
    if (texture.texture) draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture.texture)), origin, ImVec2(origin.x + size.x, origin.y + size.y));
    draw->PopClipRect();
}

void mediaContextActions(NoteLabApp& app, Source& source, const ModResource& resource, bool openInspector = false) {
    if (ImGui::MenuItem(tr(app, "Inspect", "Inspeccionar"))) {
        if (openInspector) openModResources(app, app.media.source, app.blocksSource == app.media.source ? app.blocksType : "", -1, -1, resource.kind);
        selectMediaResource(app, source, resource);
    }
    if (ImGui::MenuItem(tr(app, "Show in folder", "Mostrar en la carpeta"))) revealVirtual(app, source, resource.path);
    if (ImGui::MenuItem(tr(app, "Copy path", "Copiar ruta"))) copyText(app, resource.path);
    if (ImGui::MenuItem(tr(app, "Open in system viewer / player", "Abrir en visor / reproductor"))) openMediaExternal(app, source, resource);
    const auto found = source.typeBlocks.find(app.blocksType);
    if (found != source.typeBlocks.end() && app.blocksSource == app.media.source) {
        const auto* node = found->second.node(app.canvas.selected);
        const auto* def = node ? blockDef(node->key) : nullptr;
        if (def && ImGui::BeginMenu(tr(app, "Assign to selected block", "Asignar al bloque seleccionado"))) {
            for (size_t i = 0; i < def->args.size(); ++i) {
                const auto arg = def->args[i].kind;
                const bool matches = arg == ArgKind::Image ? resource.kind == ResourceKind::Image : arg == ArgKind::Video ? resource.kind == ResourceKind::Video : arg == ArgKind::Sound && resource.kind == ResourceKind::Sound;
                if (!matches) continue;
                if (ImGui::MenuItem((std::string(resourceKindName(resource.kind, app.spanish)) + " · #" + std::to_string(i + 1)).c_str())) {
                    app.media.type = app.blocksType; app.media.block = app.canvas.selected; app.media.argument = static_cast<int>(i);
                    assignModResource(app, resource);
                }
            }
            ImGui::EndMenu();
        }
    }
}

// Los sonidos, separados por su uso: los efectos primero, que es lo que se
// busca para un bloque; las canciones (un mar de Inst y Voices) y la musica,
// aparte (soundUseOf, igual en los tres motores).
bool soundFilterMatches(int filter, const ModResource& resource) {
    if (resource.kind != ResourceKind::Sound || filter <= 0) return true;
    const SoundUse use = soundUseOf(resource.path);
    return (filter == 1 && use == SoundUse::Effect) || (filter == 2 && use == SoundUse::Song) || (filter == 3 && use == SoundUse::Music);
}

bool drawSoundFilter(NoteLabApp& app, const char* id, int& filter, const std::vector<const ModResource*>& all) {
    std::array<int, 4> counts{};
    for (const ModResource* resource : all)
        if (resource->kind == ResourceKind::Sound) {
            ++counts[0];
            ++counts[static_cast<size_t>(soundUseOf(resource->path)) + 1];
        }
    const std::string labels[4] = {std::string(tr(app, "All", "Todos")) + " " + std::to_string(counts[0]),
                                   std::string(tr(app, "Effects", "Efectos")) + " " + std::to_string(counts[1]),
                                   std::string(tr(app, "Songs", "Canciones")) + " " + std::to_string(counts[2]),
                                   std::string(tr(app, "Music", "Música")) + " " + std::to_string(counts[3])};
    // En el orden de uso: efectos, canciones, musica, todos.
    const char* shown[4] = {labels[1].c_str(), labels[2].c_str(), labels[3].c_str(), labels[0].c_str()};
    int index = filter == 0 ? 3 : filter - 1;
    const bool changed = ui::segmented(id, &index, shown, 4);
    if (changed) filter = index == 3 ? 0 : index + 1;
    ui::tooltip(tr(app, "Song audio (Inst and Voices under songs/) and menu music apart from the effects.",
                        "El audio de las canciones (Inst y Voices bajo songs/) y la música de menús, aparte de los efectos."));
    return changed;
}

void drawModResources(NoteLabApp& app, SDL_Window* window) {
    auto& m = app.media;
    if (m.requestOpen) { ImGui::OpenPopup("###modresources"); m.requestOpen = false; }
    const auto screen = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(1080.0f, screen.x - 30.0f), std::min(720.0f, screen.y - 30.0f)), ImGuiCond_Appearing);
    bool open = true;
    const bool wasOpen = m.isOpen;
    m.isOpen = ImGui::BeginPopupModal((std::string(tr(app, "Mod resources", "Recursos del mod")) + "###modresources").c_str(), &open);
    if (!m.isOpen) {
        if (wasOpen) clearMediaPreview(app);
        return;
    }
    if (m.source < 0 || m.source >= static_cast<int>(app.sources.size()) || !app.sources[m.source]->vfs) {
        ImGui::CloseCurrentPopup(); ImGui::EndPopup(); clearMediaPreview(app); return;
    }
    Source& source = *app.sources[m.source];
    tutorialZone(app, kTrackResources);
    ImGui::Text("%s · %s", sourceName(source).c_str(), m.block >= 0 ? m.type.c_str() : tr(app, "Browse only", "Solo explorar"));
    ui::caption(tr(app, "Select to inspect; nothing plays automatically. PNG / OGG Vorbis / MP4 for engine exports.",
        "Selecciona para inspeccionar; nada se reproduce automáticamente. PNG / OGG Vorbis / MP4 para exportar al motor."));
    if (m.resources.size() >= 20000) ui::caption(tr(app, "Catalog limited to 20,000 items.", "Catálogo limitado a 20.000 elementos."));
    const bool importPressed = ImGui::Button(tr(app, "Import custom resources…", "Importar recursos custom…"));
    tutorialMark("rs-import");
    if (importPressed) {
        tutorialSignal("rs-import");
        openDialog(app, window, DialogAction::ModMediaFiles);
    }
    if (m.block >= 0 && ImGui::CollapsingHeader(tr(app, "Block settings on assignment", "Características al asignar al bloque"))) {
        ImGui::Checkbox(tr(app, "Apply these values", "Aplicar estos valores"), &m.applySettings);
        ImGui::SetNextItemWidth(-1.0f); ImGui::SliderFloat("##media-duration", &m.seconds, 0.05f, 30.0f, "%.2f s");
        ImGui::SetNextItemWidth(-1.0f); ImGui::SliderFloat("##media-opacity", &m.opacity, 0.0f, 1.0f, tr(app, "Opacity %.2f", "Opacidad %.2f"));
        ImGui::SetNextItemWidth(-1.0f); ImGui::SliderFloat("##media-default-volume", &m.volume, 0.0f, 1.0f, tr(app, "Volume %.2f", "Volumen %.2f"));
        const char* fits[] = {tr(app, "Contain", "Contener"), tr(app, "Cover", "Cubrir"), tr(app, "Stretch", "Estirar")};
        ImGui::SetNextItemWidth(-1.0f); ImGui::Combo("##media-fit", &m.fitMode, fits, 3);
    }
    const char* categories[] = {resourceKindName(ResourceKind::Image, app.spanish), resourceKindName(ResourceKind::Video, app.spanish), resourceKindName(ResourceKind::Sound, app.spanish)};
    // Tipo, base y busqueda, en un grupo: el tutorial lo senala.
    ImGui::BeginGroup();
    if (ui::segmented("mediakinds", &m.category, categories, 3)) {
        clearMediaPreview(app); m.selected.clear(); m.message.clear();
        tutorialSignal("rs-kind");
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(tr(app, "Include base game", "Incluir juego base"), &m.includeBase)) {
        clearMediaPreview(app); m.selected.clear(); refreshMediaCatalog(app);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Refresh", "Actualizar"))) {
        clearMediaPreview(app); m.selected.clear(); m.message.clear(); refreshMediaCatalog(app, true);
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##resourcesearch", tr(app, "Search resource path…", "Buscar ruta del recurso…"), m.search.data(), m.search.size()))
        tutorialSignal("rs-kind");
    if (m.category == static_cast<int>(ResourceKind::Sound)) {
        std::vector<const ModResource*> all;
        for (const auto& resource : m.resources) all.push_back(&resource);
        if (drawSoundFilter(app, "mediasounds", m.soundFilter, all)) {
            clearMediaPreview(app); m.selected.clear(); m.message.clear();
        }
    }
    ImGui::EndGroup();
    tutorialMark("rs-kinds");
    const std::string search = Vfs::toLower(m.search.data());
    const float browserHeight = std::max(64.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 12.0f);
    ImGui::BeginTable("mediabrowser", 2, ImGuiTableFlags_Resizable, ImVec2(0.0f, browserHeight));
    ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, 380.0f);
    ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
    ImGui::BeginChild("mediafiles", ImVec2(0.0f, browserHeight - ImGui::GetStyle().CellPadding.y * 2.0f), ImGuiChildFlags_Borders);
    m.fileRects.clear();
    std::vector<const ModResource*> visible;
    for (const auto& resource : m.resources)
        if (static_cast<int>(resource.kind) == m.category && soundFilterMatches(m.soundFilter, resource) &&
            (search.empty() || Vfs::toLower(resource.path).find(search) != std::string::npos)) visible.push_back(&resource);
    // Las canciones, agrupadas: un rotulo con su nombre antes de sus archivos.
    std::vector<std::pair<std::string, const ModResource*>> rows;
    std::string lastSong;
    for (const ModResource* resource : visible) {
        if (m.category == static_cast<int>(ResourceKind::Sound) && m.soundFilter == 2) {
            const std::string song = soundSongOf(resource->path);
            if (song != lastSong) rows.push_back({song, nullptr});
            lastSong = song;
        }
        rows.push_back({std::string(), resource});
    }
    ImGuiListClipper fileClipper;
    fileClipper.Begin(static_cast<int>(rows.size()));
    while (fileClipper.Step()) for (int row = fileClipper.DisplayStart; row < fileClipper.DisplayEnd; ++row) {
        if (!rows[static_cast<size_t>(row)].second) {
            ImGui::TextColored(ui::vec(ui::color::Accent), "%s %s", ui::icon::Music, rows[static_cast<size_t>(row)].first.c_str());
            continue;
        }
        const auto& resource = *rows[static_cast<size_t>(row)].second;
        ImGui::PushID(resource.path.c_str());
        const bool inUse = m.usedPaths.count(resource.path) != 0;
        const std::string label = resource.path + (inUse ? "  •" : "");
        if (ImGui::Selectable(label.c_str(), m.selected == resource.path)) selectMediaResource(app, source, resource);
        const auto fileAt = ImGui::GetItemRectMin(), fileEnd = ImGui::GetItemRectMax();
        m.fileRects[resource.path] = ImVec4(fileAt.x, fileAt.y, fileEnd.x - fileAt.x, fileEnd.y - fileAt.y);
        ui::tooltip((resource.path + "\n" + resource.provider).c_str());
        if (ImGui::BeginPopupContextItem("resourcecontext")) {
            mediaContextActions(app, source, resource);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (visible.empty()) ui::caption(tr(app, "No resources in this category / search.", "Sin recursos para esta categoría / búsqueda."));
    ImGui::EndChild(); tutorialMark("rs-list"); ImGui::TableSetColumnIndex(1);
    ImGui::BeginChild("mediapreview", ImVec2(0.0f, browserHeight - ImGui::GetStyle().CellPadding.y * 2.0f), ImGuiChildFlags_Borders,
        m.category == static_cast<int>(ResourceKind::Image) ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
    const auto* selected = findModResource(m.resources, m.selected, static_cast<ResourceKind>(m.category));
    if (selected) {
        if (selected->kind == ResourceKind::Image && m.imagePath.empty() && m.message.empty()) selectMediaResource(app, source, *selected);
        ImGui::TextWrapped("%s", selected->path.c_str());
        ui::caption((selected->provider + " · " + std::to_string(selected->bytes / 1024) + " KB" + (selected->archive ? " · ZIP" : "")).c_str());
        if (ImGui::Button(tr(app, "Show in folder", "Mostrar en la carpeta"))) revealVirtual(app, source, selected->path);
        ImGui::SameLine();
        if (ImGui::Button(tr(app, "Copy path", "Copiar ruta"))) copyText(app, selected->path);
        if (selected->kind == ResourceKind::Sound) {
            if (ImGui::Button(tr(app, "Listen", "Escuchar"))) {
                tutorialSignal("rs-listen");
                auditionMediaResource(app, source, *selected);
            }
            tutorialMark("rs-listen");
            const auto listenAt = ImGui::GetItemRectMin(), listenEnd = ImGui::GetItemRectMax();
            m.listenRect = ImVec4(listenAt.x, listenAt.y, listenEnd.x - listenAt.x, listenEnd.y - listenAt.y);
            drawMediaAudioControls(app);
        } else {
            if (ImGui::Button(selected->kind == ResourceKind::Video ? tr(app, "Preview video · system player", "Ver vídeo · reproductor del sistema") : tr(app, "Open in system viewer", "Abrir en el visor del sistema"))) openMediaExternal(app, source, *selected);
            if (!m.imagePath.empty()) {
                if (ImGui::SmallButton(tr(app, "Reset view", "Restablecer vista"))) { m.zoom = 1.0f; m.pan = {0.0f, 0.0f}; }
                ui::caption(tr(app, "Middle-drag: pan · Wheel: zoom", "Arrastre central: desplazar · Rueda: zoom"));
                drawMediaImage(app);
            }
        }
    } else ui::caption(tr(app, "Choose a resource to see its details and preview.", "Elige un recurso para ver detalles y preview."));
    if (!m.message.empty()) ImGui::TextWrapped("%s", m.message.c_str());
    ImGui::EndChild(); ImGui::EndTable();
    if (m.block >= 0) {
        ImGui::BeginDisabled(!selected);
        if (ui::primaryButton(tr(app, "Use this resource", "Usar este recurso"))) {
            if (selected && assignModResource(app, *selected)) { ImGui::CloseCurrentPopup(); clearMediaPreview(app); }
            else m.message = tr(app, "Choose the resource category required by this block slot.", "Elige la categoría de recurso que requiere esta ranura.");
        }
        const auto assignAt = ImGui::GetItemRectMin(), assignEnd = ImGui::GetItemRectMax();
        m.assignRect = ImVec4(assignAt.x, assignAt.y, assignEnd.x - assignAt.x, assignEnd.y - assignAt.y);
        ImGui::EndDisabled(); ImGui::SameLine();
    }
    if (ImGui::Button(tr(app, "Close", "Cerrar"))) { ImGui::CloseCurrentPopup(); clearMediaPreview(app); }
    const auto closeAt = ImGui::GetItemRectMin(), closeEnd = ImGui::GetItemRectMax();
    m.closeRect = ImVec4(closeAt.x, closeAt.y, closeEnd.x - closeAt.x, closeEnd.y - closeAt.y);
    ImGui::EndPopup();
}

ImVec4 mediaItemRect() {
    const auto at = ImGui::GetItemRectMin(), end = ImGui::GetItemRectMax();
    return ImVec4(at.x, at.y, end.x - at.x, end.y - at.y);
}

const char* mediaKindIcon(ResourceKind kind) {
    return kind == ResourceKind::Image ? ui::icon::Photo : kind == ResourceKind::Video ? ui::icon::Play : ui::icon::Volume;
}

ImU32 mediaKindColor(ResourceKind kind) {
    return kind == ResourceKind::Image ? ui::color::Info : kind == ResourceKind::Video ? ui::color::Accent : ui::color::Success;
}

std::string mediaEllipsis(std::string text, ImFont* font, float size, float width) {
    if (width <= 0.0f) return {};
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= width) return text;
    const float suffix = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "…").x;
    if (suffix > width) return {};
    while (!text.empty() && font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x + suffix > width) {
        size_t cut = text.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
        text.erase(cut);
    }
    return text + "…";
}

bool mediaSidebarCard(NoteLabApp& app, ResourceKind kind, const std::string& path, const std::string& detail,
                      ImU32 detailColor, bool selected, const std::string& hint) {
    const auto at = ImGui::GetCursorScreenPos();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x), height = 68.0f;
    const bool clicked = ImGui::InvisibleButton("##resourcecard", ImVec2(width, height));
    app.resourceSidebar.itemRects[path] = mediaItemRect();
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + width, at.y + height);
    draw->AddRectFilled(at, end, selected ? ui::color::AccentSoft : hovered ? IM_COL32(35, 39, 50, 255) : ui::color::Raised, 7.0f);
    if (selected) draw->AddRect(at, end, ui::withAlpha(ui::color::Accent, 150), 7.0f);
    auto* font = ui::fonts().ui ? ui::fonts().ui : ImGui::GetFont();
    auto* bold = ui::fonts().semibold ? ui::fonts().semibold : font;
    const float textX = at.x + 36.0f, room = std::max(0.0f, width - 47.0f);
    const auto slash = path.find_last_of('/');
    const std::string filename = path.empty() ? tr(app, "No file assigned", "Sin archivo asignado") : slash == std::string::npos ? path : path.substr(slash + 1);
    const std::string folder = slash == std::string::npos ? tr(app, "Resource key", "Clave del recurso") : path.substr(0, slash);
    draw->PushClipRect(at, end, true);
    if (ui::fonts().icons) draw->AddText(font, 17.0f, ImVec2(at.x + 11.0f, at.y + 10.0f), mediaKindColor(kind), mediaKindIcon(kind));
    else draw->AddCircleFilled(ImVec2(at.x + 18.0f, at.y + 20.0f), 5.0f, mediaKindColor(kind));
    draw->AddText(bold, 14.0f, ImVec2(textX, at.y + 8.0f), ui::color::Text, mediaEllipsis(filename, bold, 14.0f, room).c_str());
    draw->AddText(font, 12.0f, ImVec2(textX, at.y + 28.0f), ui::color::Muted, mediaEllipsis(folder, font, 12.0f, room).c_str());
    draw->AddText(font, 12.0f, ImVec2(textX, at.y + 46.0f), detailColor, mediaEllipsis(detail, font, 12.0f, room).c_str());
    draw->PopClipRect();
    if (hovered) ImGui::SetTooltip("%s", hint.c_str());
    return clicked;
}

void addMediaScreamer(NoteLabApp& app, Source& source, int sourceIndex, const std::string& type) {
    auto found = source.typeBlocks.find(type);
    if (found == source.typeBlocks.end()) return;
    auto& program = found->second;
    nlblocks::remember(app.canvas, program);
    const int top = newBlock(program, "event.hit"), effect = newBlock(program, "do.screamer");
    placeTop(program, top, 36.0f, 180.0f);
    attachAfter(program, top, effect);
    app.canvas.selected = effect; app.canvas.fitPending = true; blocksChanged(app);
    app.resourceSidebar.page = 1;
    openModResources(app, sourceIndex, type, effect, 0, ResourceKind::Image);
}

void drawBlockResourcesSidebar(NoteLabApp& app, SDL_Window* window) {
    auto& side = app.resourceSidebar;
    side.itemRects.clear(); side.screamerRect = {};
    const auto at = ImGui::GetCursorScreenPos(), available = ImGui::GetContentRegionAvail();
    side.bounds = ImVec4(at.x, at.y, available.x, available.y);
    const int sourceIndex = app.blocksSource >= 0 && app.blocksSource < static_cast<int>(app.sources.size()) ? app.blocksSource : app.selSource;
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size()) || !app.sources[sourceIndex]->vfs) {
        ui::caption(tr(app, "Open a mod to browse its resources.", "Abre un mod para explorar sus recursos."));
        return;
    }
    Source& source = *app.sources[sourceIndex];
    const std::string type = app.blocksSource == sourceIndex ? app.blocksType : "";
    auto found = source.typeBlocks.find(type);
    const auto uses = found == source.typeBlocks.end() ? std::vector<ResourceUse>{} : blockResourceUses(found->second);
    const auto& catalog = modResourceCatalog(source);
    const bool includeBase = source.baseOnly || side.includeBase;
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(mediaEllipsis(sourceName(source), ImGui::GetFont(), ImGui::GetFontSize(), width).c_str());
    ui::tooltip(sourceName(source).c_str());
    const std::string owner = std::string(tr(app, "Editing: ", "Editando: ")) + (type.empty() ? tr(app, "no note type", "ningún tipo") : type);
    ImGui::TextDisabled("%s", mediaEllipsis(owner, ImGui::GetFont(), ImGui::GetFontSize(), width).c_str());
    ui::tooltip(owner.c_str());
    ImGui::Spacing();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float tabWidth = std::max(1.0f, (width - gap) * 0.5f);
    for (int page = 0; page < 2; ++page) {
        if (page) ImGui::SameLine(0.0f, gap);
        ImGui::PushID(page);
        const bool active = side.page == page;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ui::vec(ui::color::AccentSoft));
        const std::string label = page == 0 ? tr(app, "Library", "Biblioteca") : std::string(tr(app, "In use", "En uso")) + " · " + std::to_string(uses.size());
        if (ImGui::Button(label.c_str(), ImVec2(tabWidth, 0.0f))) { side.page = page; saveSettings(app); }
        if (page == 0) side.libraryRect = mediaItemRect(); else side.usesRect = mediaItemRect();
        if (active) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::Spacing();
    const float refreshWidth = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(std::max(1.0f, width - refreshWidth - gap));
    ImGui::InputTextWithHint("##sidebarsearch", tr(app, "Find a resource…", "Buscar recurso…"), side.search.data(), side.search.size());
    side.searchRect = mediaItemRect();
    ImGui::SameLine(0.0f, gap);
    if (ui::iconButton("refreshresources", ui::icon::Restart, "R", tr(app, "Refresh resource catalog", "Actualizar catálogo de recursos"))) modResourceCatalog(source, true);
    if (!source.baseOnly && side.page == 0) ImGui::Checkbox(tr(app, "Include base game", "Incluir juego base"), &side.includeBase);
    std::vector<const ModResource*> resolvedUses;
    std::set<const ModResource*> usedResources;
    for (const auto& use : uses) {
        const auto* resource = findModResource(catalog, use.path, use.kind);
        resolvedUses.push_back(resource);
        if (resource) usedResources.insert(resource);
    }
    std::array<size_t, 3> counts{};
    if (side.page == 0) {
        for (const auto& resource : catalog) if (includeBase || !resource.base) ++counts[static_cast<int>(resource.kind)];
    } else for (const auto& use : uses) ++counts[static_cast<int>(use.kind)];
    const float tileWidth = std::max(1.0f, (width - gap * 2.0f) / 3.0f);
    for (int kind = 0; kind < 3; ++kind) {
        if (kind) ImGui::SameLine(0.0f, gap);
        ImGui::PushID(kind);
        const auto start = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##mediakind", ImVec2(tileWidth, 48.0f))) side.category = side.category == kind ? -1 : kind;
        side.categoryRects[kind] = mediaItemRect();
        const auto resourceKind = static_cast<ResourceKind>(kind);
        const ImU32 tint = mediaKindColor(resourceKind);
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(start.x + tileWidth, start.y + 48.0f);
        draw->AddRectFilled(start, end, ui::withAlpha(tint, side.category == kind ? 55 : 18), 6.0f);
        if (side.category == kind || ImGui::IsItemHovered()) draw->AddRect(start, end, ui::withAlpha(tint, 180), 6.0f);
        auto* font = ui::fonts().ui ? ui::fonts().ui : ImGui::GetFont();
        const std::string label = std::string(ui::fonts().icons ? mediaKindIcon(resourceKind) : "") + " " + std::to_string(counts[kind]);
        const float labelWidth = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, label.c_str()).x;
        draw->PushClipRect(start, end, true);
        draw->AddText(font, 14.0f, ImVec2(start.x + std::max(3.0f, (tileWidth - labelWidth) * 0.5f), start.y + 5.0f), tint, label.c_str());
        const std::string name = mediaEllipsis(resourceKindName(resourceKind, app.spanish), font, 11.5f, tileWidth - 8.0f);
        const float nameWidth = font->CalcTextSizeA(11.5f, FLT_MAX, 0.0f, name.c_str()).x;
        draw->AddText(font, 11.5f, ImVec2(start.x + (tileWidth - nameWidth) * 0.5f, start.y + 27.0f), ui::color::Text, name.c_str());
        draw->PopClipRect();
        ui::tooltip(tr(app, "Filter by kind; click again for all.", "Filtra por tipo; vuelve a pulsar para ver todos."));
        ImGui::PopID();
    }
    if (side.category == static_cast<int>(ResourceKind::Sound) && side.page == 0) {
        std::vector<const ModResource*> all;
        for (const auto& resource : catalog) if (includeBase || !resource.base) all.push_back(&resource);
        drawSoundFilter(app, "sidebarsounds", side.soundFilter, all);
    }
    const std::string search = Vfs::toLower(side.search.data());
    std::vector<size_t> visible;
    const auto matches = [&](ResourceKind kind, const std::string& path) {
        ModResource probe;
        probe.kind = kind;
        probe.path = path;
        const bool sound = side.category != static_cast<int>(ResourceKind::Sound) || side.page != 0 || soundFilterMatches(side.soundFilter, probe);
        return (side.category < 0 || static_cast<int>(kind) == side.category) && sound && (search.empty() || Vfs::toLower(path).find(search) != std::string::npos);
    };
    if (side.page == 0) {
        for (size_t i = 0; i < catalog.size(); ++i) if ((includeBase || !catalog[i].base) && matches(catalog[i].kind, catalog[i].path)) visible.push_back(i);
    } else for (size_t i = 0; i < uses.size(); ++i) if (matches(uses[i].kind, uses[i].path)) visible.push_back(i);
    const std::string total = std::to_string(visible.size()) + tr(app, " resources · Right-click", " recursos · Clic derecho");
    ImGui::TextDisabled("%s", total.c_str());
    ui::tooltip(side.page == 0 ? tr(app, "Select to inspect. Right-click to assign, reveal or copy.", "Selecciona para inspeccionar. Clic derecho para asignar, mostrar o copiar.")
        : tr(app, "References from this type's blocks; hand-written code is not scanned. Disconnected blocks are not exported.", "Referencias de los bloques de este tipo; no se analiza código escrito a mano. Los bloques desconectados no se exportan."));
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y + 10.0f;
    ImGui::BeginChild("sidebarresourcefiles", ImVec2(0.0f, std::max(40.0f, ImGui::GetContentRegionAvail().y - footer)), 0, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()), 68.0f + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        ImGui::PushID(row);
        if (side.page == 0) {
            const auto& resource = catalog[visible[row]];
            const bool used = usedResources.count(&resource) != 0;
            const std::string detail = source.baseOnly || resource.base ? tr(app, "Base game", "Juego base") : tr(app, "Source", "Fuente");
            if (mediaSidebarCard(app, resource.kind, resource.path, detail + " · " + std::to_string(resource.bytes / 1024) + " KB" + (used ? tr(app, " · In use", " · En uso") : ""),
                    used ? ui::color::Success : ui::color::Muted, side.selected == resource.path, resource.path + "\n" + resource.provider)) {
                side.selected = resource.path;
                openModResources(app, sourceIndex, type, -1, -1, resource.kind);
                selectMediaResource(app, source, resource);
            }
            if (ImGui::BeginPopupContextItem("sidebarresourcecontext")) {
                app.media.source = sourceIndex;
                mediaContextActions(app, source, resource, true);
                ImGui::EndPopup();
            }
        } else {
            const auto& use = uses[visible[row]];
            const auto* resource = resolvedUses[visible[row]];
            const std::string state = !resource ? tr(app, "Missing / ambiguous", "Faltante / ambiguo") : !use.active ? tr(app, "Disconnected", "Desconectado") : tr(app, "Found · Connected", "Encontrado · Conectado");
            const std::string count = std::to_string(use.slots.size()) + tr(app, " block(s)", " bloque(s)");
            if (mediaSidebarCard(app, use.kind, use.path, state + " · " + count, !resource ? ui::color::Error : !use.active ? ui::color::Warning : ui::color::Success,
                    side.selected == use.path, use.path + "\n" + state + "\n" + tr(app, "Click to inspect or replace; right-click to locate the block.", "Clic para inspeccionar o cambiar; clic derecho para localizar el bloque."))) {
                side.selected = use.path;
                openModResources(app, sourceIndex, type, use.slots.front().first, use.slots.front().second, use.kind);
                if (resource) selectMediaResource(app, source, *resource);
            }
            if (ImGui::BeginPopupContextItem("sidebarusecontext")) {
                for (const auto& slot : use.slots) {
                    if (ImGui::MenuItem((std::string(tr(app, "Select block #", "Seleccionar bloque #")) + std::to_string(slot.first)).c_str())) {
                        app.canvas.selected = slot.first; app.canvas.fitPending = true;
                    }
                }
                if (ImGui::MenuItem(tr(app, "Inspect / replace", "Inspeccionar / cambiar"))) {
                    openModResources(app, sourceIndex, type, use.slots.front().first, use.slots.front().second, use.kind);
                    if (resource) selectMediaResource(app, source, *resource);
                }
                if (resource) {
                    ImGui::Separator(); app.media.source = sourceIndex;
                    mediaContextActions(app, source, *resource, true);
                }
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
    }
    if (visible.empty()) {
        ImGui::Spacing();
        ui::caption(side.page == 1 && uses.empty() ? tr(app, "No resource references yet. Add an image, sound or video block and assign a file.", "Aún no hay referencias. Añade un bloque de imagen, sonido o vídeo y asigna un archivo.")
            : tr(app, "No matching resources. Clear the search or change the category.", "Sin coincidencias. Limpia la búsqueda o cambia la categoría."));
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ui::primaryButton(ui::label(ui::icon::Add, tr(app, "Import resources…", "Importar recursos…")), ImVec2(width, 0.0f))) {
        app.media.source = sourceIndex; app.media.type = type;
        openDialog(app, window, DialogAction::ModMediaFiles);
    }
    ImGui::BeginDisabled(found == source.typeBlocks.end());
    if (ImGui::Button(ui::label(ui::icon::Puzzle, tr(app, "Add screamer…", "Añadir screamer…")).c_str(), ImVec2(width, 0.0f))) addMediaScreamer(app, source, sourceIndex, type);
    side.screamerRect = mediaItemRect();
    ImGui::EndDisabled();
    ui::tooltip(tr(app, "Add image + sound on note hit. Then choose both files in their block slots.", "Añade imagen + sonido al acertar la nota. Luego elige ambos archivos en las ranuras del bloque."));
}

void drawResourcesPanel(NoteLabApp& app, SDL_Window* window) {
    Source* source = selectedSource(app);
    if (!source || !source->vfs) { ui::caption(tr(app, "Open a source to browse or import its resources.", "Abre una fuente para explorar o importar sus recursos.")); return; }
    auto& m = app.media;
    if (m.source != app.selSource) {
        clearMediaPreview(app); m.source = app.selSource; m.type.clear(); m.block = m.argument = -1; m.selected.clear();
        if (source->baseOnly) m.includeBase = true;
        refreshMediaCatalog(app);
    }
    ui::sectionHeader(tr(app, "Resource library", "Biblioteca de recursos"), ui::icon::FolderOpen);
    ImGui::TextUnformatted(sourceName(*source).c_str());
    if (ImGui::Button(tr(app, "Import custom resources…", "Importar recursos custom…"))) openDialog(app, window, DialogAction::ModMediaFiles);
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Refresh", "Actualizar"))) { clearMediaPreview(app); m.selected.clear(); refreshMediaCatalog(app, true); }
    ImGui::SameLine();
    if (ImGui::Checkbox(tr(app, "Include base game", "Incluir juego base"), &m.includeBase)) refreshMediaCatalog(app);
    ImGui::SetNextItemWidth(-1.0f); ImGui::InputTextWithHint("##generalressearch", tr(app, "Search resource path…", "Buscar ruta del recurso…"), m.search.data(), m.search.size());
    ui::caption(tr(app, "Images / videos / sounds · Right-click for actions · Drag and drop your files to import.",
        "Imágenes / vídeos / sonidos · Clic derecho para acciones · Arrastra archivos para importar."));
    const auto& catalog = modResourceCatalog(*source);
    const std::string search = Vfs::toLower(m.search.data());
    ImGui::BeginChild("generalresources", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    for (int kind = 0; kind < 3; ++kind) {
        const size_t count = std::count_if(catalog.begin(), catalog.end(), [&](const auto& resource) {
            return static_cast<int>(resource.kind) == kind && (m.includeBase || !resource.base) && soundFilterMatches(m.soundFilter, resource) &&
                (search.empty() || Vfs::toLower(resource.path).find(search) != std::string::npos);
        });
        const std::string label = std::string(resourceKindName(static_cast<ResourceKind>(kind), app.spanish)) + " · " + std::to_string(count);
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (!ImGui::TreeNode(label.c_str())) continue;
        if (kind == static_cast<int>(ResourceKind::Sound)) {
            std::vector<const ModResource*> all;
            for (const auto& resource : catalog) if (m.includeBase || !resource.base) all.push_back(&resource);
            drawSoundFilter(app, "generalsounds", m.soundFilter, all);
        }
        int index = 0; ImGuiListClipper clipper;
        std::vector<const ModResource*> visible;
        for (const auto& resource : catalog) if (static_cast<int>(resource.kind) == kind && (m.includeBase || !resource.base) &&
            soundFilterMatches(m.soundFilter, resource) && (search.empty() || Vfs::toLower(resource.path).find(search) != std::string::npos))
            visible.push_back(&resource);
        // Las canciones, agrupadas por su nombre.
        std::vector<std::pair<std::string, const ModResource*>> rows;
        std::string lastSong;
        for (const ModResource* resource : visible) {
            if (kind == static_cast<int>(ResourceKind::Sound) && m.soundFilter == 2) {
                const std::string song = soundSongOf(resource->path);
                if (song != lastSong) rows.push_back({song, nullptr});
                lastSong = song;
            }
            rows.push_back({std::string(), resource});
        }
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) for (index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            if (!rows[static_cast<size_t>(index)].second) {
                ImGui::TextColored(ui::vec(ui::color::Accent), "%s %s", ui::icon::Music, rows[static_cast<size_t>(index)].first.c_str());
                continue;
            }
            const auto& resource = *rows[static_cast<size_t>(index)].second;
            ImGui::PushID(resource.path.c_str());
            if (ImGui::Selectable(resource.path.c_str(), m.selected == resource.path)) {
                openModResources(app, app.selSource, app.blocksType, -1, -1, resource.kind);
                selectMediaResource(app, *source, resource);
            }
            ui::tooltip(resource.path.c_str());
            if (ImGui::BeginPopupContextItem("generalresourcecontext")) { mediaContextActions(app, *source, resource); ImGui::EndPopup(); }
            ImGui::PopID();
        }
        if (!count) ui::caption(tr(app, "No resources match.", "No hay recursos coincidentes."));
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

void drawSourceWorkspace(NoteLabApp& app, SDL_Window* window) {
    const bool blocks = app.centerTab == 1 && app.typesView == 1;
    if (blocks) {
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float width = std::max(1.0f, (ImGui::GetContentRegionAvail().x - gap) * 0.5f);
        for (int panel = 0; panel < 2; ++panel) {
            if (panel) ImGui::SameLine(0.0f, gap);
            ImGui::PushID(panel);
            const bool active = app.leftPanel == panel;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ui::vec(ui::color::AccentSoft));
            if (ImGui::Button(ui::label(panel ? ui::icon::Photo : ui::icon::Layers, panel ? tr(app, "Resources", "Recursos") : tr(app, "Mods", "Mods")).c_str(), ImVec2(width, 0.0f))) {
                app.leftPanel = panel; saveSettings(app);
            }
            if (panel) app.resourcesPanelRect = mediaItemRect(); else app.sourcesPanelRect = mediaItemRect();
            if (active) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (app.leftPanel == 1) { drawBlockResourcesSidebar(app, window); return; }
        ImGui::BeginChild("modsources", ImVec2(0.0f, 0.0f));
        drawModsPanel(app, window);
        ImGui::EndChild();
        return;
    }
    drawModsPanel(app, window);
}

void prepareMediaImport(NoteLabApp& app, const std::vector<fs::path>& paths) {
    auto& m = app.media;
    clearMediaPreview(app);
    m.importFiles.clear(); m.message.clear(); m.importSelected = 0; m.importToMod = false; m.createFolder = false;
    for (const auto& path : paths)
        if (resourceKindOf(path.u8string()) && m.importFiles.size() < 128) m.importFiles.push_back(path);
    if (m.source < 0 && app.selSource >= 0) m.source = app.selSource;
    if (m.importFiles.empty()) { setStatus(app, "No supported media files selected.", "No se seleccionaron archivos multimedia compatibles."); return; }
    const auto kind = resourceKindOf(m.importFiles.front().u8string()).value_or(ResourceKind::Image);
    std::snprintf(m.importFolder.data(), m.importFolder.size(), "%s", kind == ResourceKind::Image ? "images/custom" : kind == ResourceKind::Video ? "videos/custom" : "sounds/custom");
    m.importRequestOpen = true;
}

fs::path writableMediaRoot(const Source& source) {
    if (source.baseOnly || !source.vfs) return {};
    const auto index = source.vfs->writeRootIndex();
    if (index >= source.vfs->roots().size() || !source.vfs->isRootWritable(index)) return {};
    const auto& root = source.vfs->roots()[index];
    return root.kind == MountProviderKind::Directory ? root.path : fs::path{};
}

bool commitMediaImport(NoteLabApp& app) {
    auto& m = app.media;
    if (m.source < 0 || m.source >= static_cast<int>(app.sources.size())) return false;
    Source& source = *app.sources[m.source];
    if (!source.vfs) return false;
    const std::string folder = m.importFolder.data();
    for (const auto& file : m.importFiles) if (source.vfs->exists(folder + "/" + file.filename().u8string())) {
        m.message = tr(app, "That resource path already exists in the source. Choose a different folder.", "Esa ruta de recurso ya existe en la fuente. Elige otra carpeta."); return false;
    }
    fs::path root = m.importToMod ? writableMediaRoot(source) :
        settingsFolder() / "media-imports" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    if (root.empty()) { m.message = tr(app, "This source cannot be modified; use project-only import.", "Esta fuente no se puede modificar; importa solo al proyecto."); return false; }
    if (!m.importToMod) { std::error_code ec; fs::create_directories(root, ec); if (ec) { m.message = ec.message(); return false; } }
    auto result = importMediaFiles(m.importFiles, root, folder, m.createFolder);
    for (const auto& file : result.files) {
        if (!source.vfs->pushFile(file.first, file.second, "Note Lab media import")) {
            m.message = tr(app, "File copied, but the source could not mount it. Refresh or reopen the source.", "Archivo copiado, pero no se pudo montar. Actualiza o vuelve a abrir la fuente."); return false;
        }
        source.importedFiles.push_back({file.first.u8string(), "", file.second, ""});
        m.selected = file.second;
    }
    if (!result.files.empty()) {
        source.resourcesFrom = nullptr;
        refreshMediaCatalog(app, true); app.dirty = true; app.exporting.preparedKey.clear();
        setStatus(app, std::to_string(result.files.size()) + " resources imported.", std::to_string(result.files.size()) + " recursos importados.");
    }
    m.message = result.error;
    return result.error.empty() && !result.files.empty();
}

void drawMediaImport(NoteLabApp& app, SDL_Window*) {
    auto& m = app.media;
    if (m.importRequestOpen) { ImGui::OpenPopup("###mediaimport"); m.importRequestOpen = false; }
    bool open = true;
    const auto screen = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, screen.x - 32.0f), std::min(720.0f, screen.y - 32.0f)), ImGuiCond_Appearing);
    const bool wasOpen = m.importIsOpen;
    m.importIsOpen = ImGui::BeginPopupModal((std::string(tr(app, "Import custom resources", "Importar recursos custom")) + "###mediaimport").c_str(), &open);
    if (!m.importIsOpen) {
        if (wasOpen) { clearMediaPreview(app); m.importFiles.clear(); }
        return;
    }
    if (m.source < 0 || m.source >= static_cast<int>(app.sources.size())) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); clearMediaPreview(app); return; }
    Source& source = *app.sources[m.source];
    ImGui::TextUnformatted(sourceName(source).c_str());
    const float importHeight = std::max(64.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 12.0f);
    ImGui::BeginChild("importmediacontent", ImVec2(0.0f, importHeight));
    ImGui::BeginChild("importmedialist", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < m.importFiles.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const auto kind = resourceKindOf(m.importFiles[i].u8string()).value_or(ResourceKind::Image);
        const std::string label = std::string(resourceKindName(kind, app.spanish)) + " · " + m.importFiles[i].filename().u8string();
        if (ImGui::Selectable(label.c_str(), m.importSelected == static_cast<int>(i))) { m.importSelected = static_cast<int>(i); clearMediaPreview(app); }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (!m.importFiles.empty()) {
        const fs::path input = m.importFiles[std::clamp(m.importSelected, 0, static_cast<int>(m.importFiles.size()) - 1)];
        const auto kind = resourceKindOf(input.u8string()).value_or(ResourceKind::Image);
        const std::string path = input.filename().u8string();
        std::error_code ec; const auto size = fs::file_size(input, ec);
        ImGui::TextWrapped("%s", input.u8string().c_str());
        if (!ec) ui::caption((std::to_string(size / 1024) + " KB").c_str());
        if (ImGui::Button(tr(app, "Inspect input before import", "Inspeccionar antes de importar"))) {
            Source temporary; temporary.vfs = std::make_unique<Vfs>(); temporary.vfs->pushFile(input, path, "Selected input");
            ModResource resource{kind, path, "Selected input", 0, ec ? 0 : size};
            if (kind == ResourceKind::Image) selectMediaResource(app, temporary, resource);
            else if (kind == ResourceKind::Sound) auditionMediaResource(app, temporary, resource);
            else openMediaExternal(app, temporary, resource);
        }
        ImGui::SameLine(); if (ImGui::Button(tr(app, "Show input in folder", "Mostrar archivo de origen"))) revealInExplorer(input);
        if (m.audio) {
            drawMediaAudioControls(app);
        }
        if (!m.imagePath.empty()) {
            ImGui::BeginChild("importimage", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
            drawMediaImage(app); ImGui::EndChild();
        }
    }
    ImGui::SeparatorText(tr(app, "Destination and properties", "Destino y características"));
    const fs::path nativeRoot = writableMediaRoot(source);
    ImGui::BeginDisabled(nativeRoot.empty());
    ImGui::Checkbox(tr(app, "Copy into this mod (explicit file write)", "Copiar a este mod (escribe archivos)"), &m.importToMod);
    ImGui::EndDisabled();
    ui::caption(m.importToMod ? nativeRoot.u8string().c_str() : tr(app, "Project-only: originals / ZIP / base game remain untouched. Resources travel with export.", "Solo proyecto: originales / ZIP / juego base no se modifican. Los recursos viajan con el export."));
    ImGui::SetNextItemWidth(-1.0f); ImGui::InputTextWithHint("##importmediafolder", "images/custom · sounds/custom · videos/custom", m.importFolder.data(), m.importFolder.size());
    ImGui::Checkbox(tr(app, "Create destination folder if missing", "Crear carpeta de destino si no existe"), &m.createFolder);
    ui::caption(tr(app, "Categories follow the file extension. Inspect before importing. Duration, volume, opacity and fit are assigned by the block. No overwrite.",
        "La categoría procede de la extensión. Inspecciona antes de importar. Duración, volumen, opacidad y ajuste se asignan en el bloque. No se sobrescribe."));
    if (!m.message.empty()) ImGui::TextWrapped("%s", m.message.c_str());
    ImGui::EndChild();
    if (ui::primaryButton(tr(app, "Confirm import", "Confirmar importación"))) {
        if (commitMediaImport(app)) { clearMediaPreview(app); m.importFiles.clear(); ImGui::CloseCurrentPopup(); }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app, "Cancel", "Cancelar")) || !open) { clearMediaPreview(app); m.importFiles.clear(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

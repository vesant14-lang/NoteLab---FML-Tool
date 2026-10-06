#pragma once

void customSameLineIfFits(const char* label) {
    const float end = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= end) ImGui::SameLine();
}

// Los mensajes del nucleo (compartido con FML) llegan en ingles: aqui se dicen
// en espanol por su texto. Uno que no se conozca sale tal cual.
std::string customText(const NoteLabApp& app, const std::string& text) {
    if (!app.spanish || text.empty()) return text;
    static const std::pair<const char*, const char*> exact[] = {
        {"Select a PNG resource and a Sparrow XML or Packer TXT file.", "Elige un recurso PNG y un archivo XML de Sparrow o TXT de Packer."},
        {"Cannot read atlas metadata (4 MiB max). The current resource was not changed.",
         "No se pueden leer los datos del atlas (4 MiB como máximo). El recurso actual no cambia."},
        {"Too many frames or pixels (512 frames / 32 megapixels per sequence).", "Demasiados frames o píxeles (512 frames / 32 megapíxeles por secuencia)."},
        {"Cell size must fit inside the image.", "El tamaño de celda tiene que caber en la imagen."},
        {"Grid exceeds 512 frames. Increase cell size.", "La cuadrícula pasa de 512 frames. Aumenta el tamaño de celda."},
        {"Too many manually defined frames.", "Demasiados frames trazados a mano."},
        {"A frame rectangle is outside the image.", "Un rectángulo de frame se sale de la imagen."},
        {"Select a valid atlas animation.", "Elige una animación válida del atlas."},
        {"Atlas metadata exceeds 4 MiB.", "Los datos del atlas pasan de 4 MiB."},
        {"Cannot read atlas metadata.", "No se pueden leer los datos del atlas."},
        {"Atlas frame is outside the image or too large.", "Un frame del atlas se sale de la imagen o es demasiado grande."},
        {"Sequence exceeds 512 frames.", "La secuencia pasa de 512 frames."},
        {"Cannot decode GIF.", "No se puede leer el GIF."},
        {"Unsupported resource. Choose PNG, PNG + XML/TXT, image sequences, GIF or audio.",
         "Recurso no compatible. Elige PNG, PNG + XML/TXT, secuencias de imágenes, GIF o audio."},
        {"No usable frames found.", "No hay frames utilizables."},
        {"Separate frame numbers with commas.", "Separa los números de frame con comas."},
        {"Resource or assignment limit exceeded (128 / 192).", "Se pasó el límite de recursos o asignaciones (128 / 192)."},
        {"Assign at least one resource to a role.", "Asigna al menos un recurso a una función."},
        {"Enter the custom note type name.", "Escribe el nombre del tipo de nota custom."},
        {"Resource is missing.", "Falta el recurso."},
        {"Invalid FPS, scale or offset.", "FPS, escala o desplazamiento no válidos."},
        {"This role is assigned twice. Replace or remove the previous assignment.",
         "Esta función está asignada dos veces. Reemplaza o quita la asignación anterior."},
        {"This role needs an audio file.", "Esta función necesita un archivo de audio."},
        {"Engine audio must be OGG Vorbis. WAV/MP3 need conversion, not renaming.",
         "El audio del motor tiene que ser OGG Vorbis. Un WAV o MP3 hay que convertirlo, no renombrarlo."},
        {"Select a countdown step (3, 2, 1, Go).", "Elige un paso de la cuenta atrás (3, 2, 1, Go)."},
        {"Sound name must be a safe file name (letters, numbers, hyphen or underscore).",
         "El nombre del sonido tiene que valer como nombre de archivo (letras, números, guion o guion bajo)."},
        {"This role needs image frames.", "Esta función necesita frames de imagen."},
        {"Frame order is invalid.", "El orden de frames no es válido."},
        {"HUD ranking/countdown graphics need one frame. Select its frame explicitly.",
         "Las imágenes del ranking y de la cuenta atrás llevan un solo frame: elígelo."},
        {"Invalid piece, direction or variant.", "Pieza, dirección o variante no válidas."},
        {"Ordered frames exceed the pixel budget.", "Los frames elegidos pasan del límite de píxeles."},
        {"Cannot pack these frames into an 8192 px atlas.", "Estos frames no caben en un atlas de 8192 px."},
        {"Created resources exceed 128 MiB.", "Los recursos creados pasan de 128 MiB."},
    };
    static const std::pair<const char*, const char*> prefixes[] = {
        {"Missing, empty or oversized file (64 MiB limit): ", "Archivo que falta, vacío o demasiado grande (límite de 64 MiB): "},
        {"Invalid image or exceeds 8192 px / 32 megapixels: ", "Imagen no válida o de más de 8192 px / 32 megapíxeles: "},
        {"Cannot decode PNG: ", "No se puede leer el PNG: "},
    };
    for (const auto& [en, es] : exact)
        if (text == en) return es;
    for (const auto& [en, es] : prefixes)
        if (text.rfind(en, 0) == 0) return es + text.substr(std::string(en).size());
    // «Assignment 3: …» y «Frame order uses numbers from 1 to 12 (512 entries max).»
    if (text.rfind("Assignment ", 0) == 0) {
        const size_t colon = text.find(": ");
        if (colon != std::string::npos) return "Asignación " + text.substr(11, colon - 11) + ": " + customText(app, text.substr(colon + 2));
    }
    const std::string order = "Frame order uses numbers from 1 to ";
    if (text.rfind(order, 0) == 0) {
        const size_t end = text.find(' ', order.size());
        return "El orden de frames usa números del 1 al " + text.substr(order.size(), end - order.size()) + " (512 como máximo).";
    }
    return text;
}

// Lo que es cada recurso importado, dicho en el idioma de la interfaz.
const char* customKindLabel(const NoteLabApp& app, ImportKind kind) {
    switch (kind) {
        case ImportKind::Atlas: return tr(app, "sheet (PNG + XML/TXT)", "hoja (PNG + XML/TXT)");
        case ImportKind::Image: return tr(app, "image", "imagen");
        case ImportKind::Frames: return tr(app, "frame sequence", "secuencia de frames");
        case ImportKind::Gif: return "GIF";
        case ImportKind::Sound: return tr(app, "sound", "sonido");
        case ImportKind::Font: return tr(app, "font", "letra");
        case ImportKind::Unknown: break;
    }
    return tr(app, "unknown", "desconocido");
}

void clearCustomPreview(NoteLabApp& app) {
    for (const char* path : {"notelab-live/custom-sheet.png", "notelab-live/custom-frame.png"}) app.renderer.removeDynamicFrame(path);
    app.custom.inspected = {}; app.custom.review = {}; app.custom.uploadedFrame = -1;
    app.custom.audioPending = false;
    if (app.custom.audio) { app.custom.audio->clearTracks(); app.custom.audio->pause(); }
}

void openCustomCreator(NoteLabApp& app, bool ranking, const std::string& type) {
    clearCustomPreview(app);
    app.custom.createdWorkspace = app.sources.empty();
    if (app.sources.empty()) {
        auto source = std::make_unique<Source>();
        source->root = settingsFolder() / "Custom workspace";
        std::error_code ec; fs::create_directories(source->root, ec);
        source->vfs = mountVfs({source->root});
        if (ec || source->vfs->roots().empty()) { setStatus(app, "Cannot create workspace.", "No se pudo crear el espacio de trabajo."); return; }
        app.sources.push_back(std::move(source)); app.selSource = 0; app.selStyle = -1;
    }
    auto& c = app.custom;
    c.source = app.selSource >= 0 ? app.selSource : 0;
    c.ranking = ranking; c.recipe = {}; c.resource = -1; c.animation = 0; c.assignment = -1; c.editStyle = -1;
    c.draft = {}; c.message.clear(); c.order.fill(0); c.soundName.fill(0); c.type.fill(0);
    c.recipe.noteType = !type.empty(); c.recipe.type = type;
    const Source& source = *app.sources[static_cast<size_t>(c.source)];
    sourceEngine(app, source, c.engine);
    const NoteStyle* style = selectedStyle(app);
    std::snprintf(c.name.data(), c.name.size(), "%s", tr(app, "My custom notes", "Mis notas custom"));
    if (style && app.selSource == c.source) {
        const auto saved = source.recipes.find("custom:" + style->id);
        if (saved != source.recipes.end() && (type.empty() || style->useDetail == type)) {
            c.recipe = saved->second.custom; c.editStyle = app.selStyle;
            std::snprintf(c.name.data(), c.name.size(), "%s", style->name.c_str());
        } else if (ranking) {
            c.editStyle = app.selStyle; std::snprintf(c.name.data(), c.name.size(), "%s", style->name.c_str());
        }
    }
    std::snprintf(c.type.data(), c.type.size(), "%s", c.recipe.type.c_str());
    if (ranking) { c.draft.role = CustomRole::HudImage; c.draft.hudIndex = 0; c.draft.scale = 0.7f; }
    c.resource = c.recipe.resources.empty() ? -1 : 0;
    c.inspectDirty = true; c.fit = true; c.zoom = 1; c.pan = {}; c.trace = false; c.tracing = false;
    c.requestOpen = true;
}

void finishCustomCreator(NoteLabApp& app) {
    clearCustomPreview(app);
    auto& c = app.custom;
    if (c.createdWorkspace && app.sources.size() == 1 && app.sources.front()->catalog.styles.empty() && !app.dirty)
        closeEverything(app);
    c.createdWorkspace = false;
    c.isOpen = false;
}

std::string customRoleIssue(const NoteLabApp& app) {
    const auto& c = app.custom;
    if (c.resource < 0 || c.resource >= static_cast<int>(c.recipe.resources.size()))
        return tr(app, "Select a resource before assigning a role.", "Selecciona un recurso antes de asignar una función.");
    const bool sound = c.recipe.resources[static_cast<size_t>(c.resource)].input.kind == ImportKind::Sound;
    const bool audioRole = c.draft.role == CustomRole::CountdownSound || c.draft.role == CustomRole::SoundEffect;
    if (sound != audioRole)
        return sound ? tr(app, "Choose Countdown sound or Named sound effect for this file.", "Elige Sonido de cuenta atrás o Efecto de sonido con nombre para este archivo.") :
            tr(app, "Use an image role for this resource; import an OGG for a sound role.", "Usa una función de imagen para este recurso; importa un OGG para una función de sonido.");
    if (!sound && (!c.inspected.error.empty() || c.inspected.frames.empty()))
        return tr(app, "Resolve the inspection error before assigning this resource.", "Resuelve el error de inspección antes de asignar este recurso.");
    if (c.draft.role == CustomRole::SoundEffect && !c.soundName[0])
        return tr(app, "Give this sound effect a name.", "Ponle un nombre a este efecto de sonido.");
    return {};
}

void addCustomFiles(NoteLabApp& app, const std::vector<fs::path>& paths) {
    auto& c = app.custom;
    const auto found = scanImport(paths);
    int added = 0;
    for (const auto& item : found) {
        if (item.kind == ImportKind::Font || item.kind == ImportKind::Unknown) continue;
        if (c.recipe.resources.size() >= 128) { c.message = tr(app, "128 resources at most.", "Como máximo, 128 recursos."); break; }
        if (std::any_of(c.recipe.resources.begin(), c.recipe.resources.end(), [&](const CustomResource& r) { return r.input.path == item.path && r.input.kind == item.kind; })) continue;
        c.recipe.resources.push_back({item}); ++added;
    }
    if (added) {
        c.resource = static_cast<int>(c.recipe.resources.size()) - 1; c.animation = 0; c.inspectDirty = true; c.fit = true;
        c.order.fill(0); c.draft.order.clear(); c.review = {}; c.message.clear();
    } else if (found.empty()) c.message = tr(app, "No supported resources found.", "No se encontraron recursos compatibles.");
}

std::string customAssignmentLabel(const NoteLabApp& app, const CustomAssignment& a) {
    if (a.role == CustomRole::Piece) return std::string(partLabel(a.part, app.spanish)) + " · " + directionLabel(a.direction, app.spanish) +
        (a.part == Part::Splash ? " #" + std::to_string(a.variant + 1) : "");
    if (a.role == CustomRole::SoundEffect) return std::string(tr(app, "Sound: ", "Sonido: ")) + a.soundName;
    return std::string(a.role == CustomRole::CountdownSound ? tr(app, "Audio · ", "Audio · ") : "") + hudName(app, a.hudIndex);
}

bool sameCustomRole(const CustomAssignment& a, const CustomAssignment& b) {
    if (a.role != b.role) return false;
    if (a.role == CustomRole::Piece) return a.part == b.part && a.direction == b.direction && a.variant == b.variant;
    return a.role == CustomRole::SoundEffect ? a.soundName == b.soundName : a.hudIndex == b.hudIndex;
}

void customOrderText(NoteLabApp::CustomCreator& c) {
    std::string text;
    for (int frame : c.draft.order) { if (!text.empty()) text += ", "; text += std::to_string(frame + 1); }
    std::snprintf(c.order.data(), c.order.size(), "%s", text.c_str()); c.review = {};
}

void suggestCustomRoles(NoteLabApp& app) {
    auto& c = app.custom;
    int added = 0;
    for (size_t i = 0; i < c.recipe.resources.size(); ++i) {
        const auto& item = c.recipe.resources[i].input;
        const int animations = std::max(1, static_cast<int>(item.animations.size()));
        for (int n = 0; n < animations; ++n) {
            const auto piece = item.animations.empty() ? item.piece : item.animations[static_cast<size_t>(n)].piece;
            const int hud = item.animations.empty() ? item.hudIndex : item.animations[static_cast<size_t>(n)].hudIndex;
            CustomAssignment a; a.resource = static_cast<int>(i); a.animation = n;
            if (item.kind == ImportKind::Sound && hud >= 15) { a.role = CustomRole::CountdownSound; a.hudIndex = hud; }
            else if (item.kind != ImportKind::Sound && hud >= 0) { a.role = CustomRole::HudImage; a.hudIndex = hud; a.order = {0}; a.scale = c.engine == Engine::VSlice ? 1.0f : hud >= 5 && hud < 15 ? 0.5f : 0.7f; }
            else if (piece.found && item.kind != ImportKind::Sound && !c.ranking) { a.part = piece.part; a.direction = piece.direction; a.variant = piece.variant; a.loop = a.part == Part::Note || a.part == Part::StrumStatic || a.part == Part::HoldCover; }
            else continue;
            if (std::none_of(c.recipe.assignments.begin(), c.recipe.assignments.end(), [&](const auto& old) { return sameCustomRole(a, old); }) && c.recipe.assignments.size() < 192) {
                c.recipe.assignments.push_back(std::move(a)); ++added;
            }
        }
    }
    c.review = {}; c.message = std::to_string(added) + tr(app, " suggested roles added; review or change them freely.", " funciones sugeridas añadidas; revísalas o cámbialas libremente.");
}

void refreshCustomInspection(NoteLabApp& app) {
    auto& c = app.custom;
    if (!c.inspectDirty) return;
    c.inspected = {}; c.uploadedFrame = -1; c.frame = 0; c.clockStart = ImGui::GetTime(); c.inspectDirty = false;
    c.audioPending = false;
    if (c.audio) { c.audio->clearTracks(); c.audio->pause(); }
    c.review = {};
    if (c.resource < 0 || c.resource >= static_cast<int>(c.recipe.resources.size())) return;
    c.inspected = inspectCustomResource(c.recipe.resources[static_cast<size_t>(c.resource)], c.animation);
    if (!c.inspected.sheet.empty()) uploadLiveImage(app, "notelab-live/custom-sheet.png", c.inspected.sheet);
    c.message = c.inspected.error;
}

bool createCustomNow(NoteLabApp& app) {
    auto& c = app.custom;
    if (c.source < 0 || c.source >= static_cast<int>(app.sources.size())) return false;
    c.recipe.type = c.type.data();
    c.review = buildCustomStyle(c.recipe, c.engine, c.name.data());
    if (!c.review.ok) {
        c.message = c.review.errors.empty() ? tr(app, "The resources could not be built.", "No se pudieron construir los recursos.") : c.review.errors.front();
        return false;
    }
    Source& source = *app.sources[static_cast<size_t>(c.source)];
    CreationRecipe saved; saved.kind = "custom"; saved.custom = c.recipe;
    size_t inputBytes = 0;
    auto preserveInput = [&](fs::path& path) {
        if (path.empty()) return true;
        std::error_code ec; const auto size = fs::file_size(path, ec);
        if (ec || size > 64u * 1024u * 1024u || inputBytes + size > 256u * 1024u * 1024u) return false;
        inputBytes += static_cast<size_t>(size);
        std::ifstream in(path, std::ios::binary); const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
        const auto copy = saveCreatedBytes(bytes, path.filename().u8string());
        if (copy.empty()) return false;
        path = pathFromUtf8(copy); return true;
    };
    std::set<int> used;
    for (const auto& a : c.recipe.assignments) used.insert(a.resource);
    for (int index : used) {
        auto& r = saved.custom.resources[static_cast<size_t>(index)];
        if (r.input.kind != ImportKind::Frames) {
            if (!preserveInput(r.input.path) || !preserveInput(r.input.atlas)) {
                c.message = tr(app, "The source resources could not be kept (64 MiB per file, 256 MiB in all).",
                                    "No se pudieron conservar los recursos de origen (64 MiB por archivo, 256 MiB en total).");
                return false;
            }
        } else for (auto& file : r.input.files) if (!preserveInput(file)) {
            c.message = tr(app, "The frame sequence could not be kept.", "No se pudo conservar la secuencia de frames.");
            return false;
        }
    }
    std::map<std::string, std::string> mounted;
    for (const auto& f : c.review.files) {
        const std::string name = pathFromUtf8(f.path).filename().u8string();
        const auto file = saveCreatedBytes(f.bytes, name);
        if (file.empty()) { c.message = tr(app, "A created file could not be saved.", "No se pudo guardar un archivo creado."); return false; }
        const std::string path = mountCreated(source, file, name);
        if (path.empty()) { c.message = tr(app, "A created file could not be mounted.", "No se pudo montar un archivo creado."); return false; }
        mounted[f.path] = path;
    }
    NoteStyle style = c.review.style;
    for (auto& sheet : style.sheets) { sheet.image = mounted.at(sheet.image); sheet.atlas = mounted.at(sheet.atlas); }
    auto remapHud = [&](HudAsset& asset) {
        if (!asset.image.empty()) asset.image = mounted.at(asset.image);
        if (!asset.sound.empty()) asset.sound = mounted.at(asset.sound);
    };
    for (auto& asset : style.judgements) remapHud(asset);
    for (auto& asset : style.digits) remapHud(asset);
    for (auto& asset : style.countdown) remapHud(asset);
    remapHud(style.combo);
    for (auto& sound : style.sounds) sound.path = mounted.at(sound.path);
    const bool edit = c.editStyle >= 0 && c.editStyle < static_cast<int>(source.catalog.styles.size());
    style.id = edit ? source.catalog.styles[static_cast<size_t>(c.editStyle)].id : freeVariantId(source, "notelab:custom/");
    if (edit && c.ranking) {
        NoteStyle target = source.catalog.styles[static_cast<size_t>(c.editStyle)];
        for (int h = 0; h < 19; ++h) {
            const HudAsset* incoming = hudAssetAt(style, h); HudAsset* current = hudAssetAt(target, h);
            if (!incoming->image.empty()) { current->image = incoming->image; current->declared = incoming->declared; current->scale = incoming->scale; current->pixel = incoming->pixel; current->inherited = false; }
            if (!incoming->sound.empty()) { current->sound = incoming->sound; current->soundDeclared = incoming->soundDeclared; current->inherited = false; }
        }
        target.hasHud = true;
        for (const auto& sound : style.sounds) {
            const auto old = std::find_if(target.sounds.begin(), target.sounds.end(), [&](const StyleSound& s) { return s.name == sound.name; });
            if (old != target.sounds.end()) *old = sound; else target.sounds.push_back(sound);
        }
        style = std::move(target);
    }
    int index;
    if (edit) {
        index = c.editStyle; selectStyle(app, c.source, index); beginEdit(app);
        source.catalog.styles[static_cast<size_t>(index)] = style; afterEdit(app);
    } else index = addCreatedStyle(app, c.source, style);
    saved.styleId = style.id;
    source.recipes["custom:" + style.id] = std::move(saved);
    if (style.use == StyleUse::NoteType) source.typeLooks[style.useDetail] = style.id;
    source.noteTypes = scanNoteTypes(*source.vfs, source.catalog); source.typeFromBase.assign(source.noteTypes.size(), 0);
    source.soundsFrom = nullptr;
    selectStyle(app, c.source, index); applyTypeRules(app); app.dirty = true;
    setStatus(app, "Custom resources applied. Save the project to keep editing them.", "Recursos propios aplicados. Guarda el proyecto para seguir editándolos.");
    return true;
}

void drawCustomImageView(NoteLabApp& app) {
    auto& c = app.custom;
    const bool sheet = !c.inspected.sheet.empty();
    if (c.inspected.frames.empty()) {
        ImGui::TextWrapped("%s", tr(app, "Select a resource to inspect it before using it.", "Selecciona un recurso para revisarlo antes de usarlo.")); return;
    }
    const Image& image = sheet ? c.inspected.sheet : c.inspected.frames[static_cast<size_t>(std::clamp(c.frame, 0, static_cast<int>(c.inspected.frames.size()) - 1))];
    const char* path = sheet ? "notelab-live/custom-sheet.png" : "notelab-live/custom-frame.png";
    const auto texture = app.renderer.previewImage(path);
    const ImVec2 at = ImGui::GetCursorScreenPos(), size(std::max(80.0f, ImGui::GetContentRegionAvail().x), std::max(80.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("customcanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hover = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    if (c.fit) { c.zoom = std::min(size.x / image.w, size.y / image.h) * 0.9f; c.pan = {}; c.fit = false; }
    ImVec2 center(at.x + size.x * 0.5f + c.pan.x, at.y + size.y * 0.5f + c.pan.y);
    if (hover && io.MouseWheel != 0) {
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetItemID());
        const float old = c.zoom; c.zoom = std::clamp(c.zoom * std::pow(1.18f, io.MouseWheel), 0.02f, 32.0f);
        c.pan.x += (io.MousePos.x - center.x) * (1 - c.zoom / old); c.pan.y += (io.MousePos.y - center.y) * (1 - c.zoom / old);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { c.pan.x += io.MouseDelta.x; c.pan.y += io.MouseDelta.y; }
    center = ImVec2(at.x + size.x * 0.5f + c.pan.x, at.y + size.y * 0.5f + c.pan.y);
    const ImVec2 origin(center.x - image.w * c.zoom * 0.5f, center.y - image.h * c.zoom * 0.5f);
    auto point = [&](ImVec2 mouse) { return ImVec2((mouse.x - origin.x) / c.zoom, (mouse.y - origin.y) / c.zoom); };
    const ImVec2 local = point(io.MousePos);
    auto* draw = ImGui::GetWindowDrawList(); draw->PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);
    drawChecker(draw, at, ImVec2(at.x + size.x, at.y + size.y));
    if (texture.texture) draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture.texture)), origin, ImVec2(origin.x + image.w * c.zoom, origin.y + image.h * c.zoom));
    int clickedFrame = -1;
    if (sheet) for (size_t f = 0; f < c.inspected.boxes.size(); ++f) {
        const auto& b = c.inspected.boxes[f];
        const bool picked = std::find(c.draft.order.begin(), c.draft.order.end(), static_cast<int>(f)) != c.draft.order.end();
        draw->AddRect(ImVec2(origin.x + b.x * c.zoom, origin.y + b.y * c.zoom), ImVec2(origin.x + (b.x + b.w) * c.zoom, origin.y + (b.y + b.h) * c.zoom),
            picked ? IM_COL32(167, 130, 255, 255) : IM_COL32(170, 170, 190, 150), 0, 0, picked ? 2.0f : 1.0f);
        if (hover && !c.trace && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && local.x >= b.x && local.x < b.x + b.w && local.y >= b.y && local.y < b.y + b.h) {
            if (clickedFrame < 0 || c.frame == static_cast<int>(f)) clickedFrame = static_cast<int>(f);
        }
    }
    if (clickedFrame >= 0) {
        if (c.draft.order.size() < kCustomFrameLimit) c.draft.order.push_back(clickedFrame);
        customOrderText(c); c.frame = clickedFrame; c.playing = false;
    }
    if (sheet && c.trace && hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && local.x >= 0 && local.y >= 0 && local.x < image.w && local.y < image.h) {
        c.tracing = true; c.traceStart = local;
    }
    if (c.tracing) {
        draw->AddRect(ImVec2(origin.x + c.traceStart.x * c.zoom, origin.y + c.traceStart.y * c.zoom), io.MousePos, IM_COL32(130, 255, 180, 255), 0.0f, ImDrawFlags_None, 2.0f);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            c.tracing = false;
            const int x = std::clamp(static_cast<int>(std::min(local.x, c.traceStart.x)), 0, image.w - 1);
            const int y = std::clamp(static_cast<int>(std::min(local.y, c.traceStart.y)), 0, image.h - 1);
            const int endX = std::clamp(static_cast<int>(std::max(local.x, c.traceStart.x)), x + 1, image.w);
            const int endY = std::clamp(static_cast<int>(std::max(local.y, c.traceStart.y)), y + 1, image.h);
            auto& r = c.recipe.resources[static_cast<size_t>(c.resource)];
            if (r.regions.size() < kCustomFrameLimit) { r.regions.push_back({x, y, endX - x, endY - y}); c.inspectDirty = true; }
            c.draft.order.clear(); c.order.fill(0);
        }
    }
    draw->PopClipRect();
}

void drawCustomCreator(NoteLabApp& app, SDL_Window* window) {
    auto& c = app.custom;
    if (c.requestOpen) { ImGui::OpenPopup("###customcreator"); c.requestOpen = false; }
    const auto screen = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(1380.0f, screen.x - 30), std::min(820.0f, screen.y - 30)), ImGuiCond_Appearing);
    bool open = true;
    const bool wasOpen = c.isOpen;
    // Con recursos o funciones sin aplicar no hay aspa: «Cancelar» y Esc preguntan
    // antes de perderlos (al reabrir el creador empieza de cero).
    const bool unsaved = !c.recipe.resources.empty() || !c.recipe.assignments.empty();
    c.isOpen = ImGui::BeginPopupModal((ui::label(ui::icon::Layers, c.ranking ? tr(app, "Ranking from images", "Ranking con imágenes") :
        tr(app, "Custom creator · Advanced", "Creador custom · Avanzado")) + "###customcreator").c_str(), unsaved ? nullptr : &open);
    if (!c.isOpen) {
        if (wasOpen || !open) finishCustomCreator(app);
        else if (!c.inspected.frames.empty() || c.audio) clearCustomPreview(app);
        return;
    }
    tutorialZone(app, kTrackCustom);
    ui::caption(tr(app, "Your files, your frame order, your roles. No template is required. The source mod is untouched.",
        "Tus archivos, tu orden de frames, tus funciones. No requiere plantilla. El mod de origen no se modifica."));
    const bool importFiles = ImGui::Button(tr(app, "Import files…", "Importar archivos…"));
    tutorialMark("cc-import");
    if (importFiles) openDialog(app, window, DialogAction::CustomFiles);
    customSameLineIfFits(tr(app, "Import folder / sequence…", "Importar carpeta / secuencia…"));
    if (ImGui::Button(tr(app, "Import folder / sequence…", "Importar carpeta / secuencia…"))) openDialog(app, window, DialogAction::CustomFolder);
    customSameLineIfFits(tr(app, "Drag and drop also works here", "También puedes arrastrar y soltar aquí"));
    ImGui::PushTextWrapPos(0); ui::caption(tr(app, "Drag and drop also works here", "También puedes arrastrar y soltar aquí")); ImGui::PopTextWrapPos();
    if (ImGui::Button(tr(app, "Suggest roles from names", "Sugerir funciones por nombres"))) suggestCustomRoles(app);
    refreshCustomInspection(app);
    const float height = std::max(220.0f, ImGui::GetContentRegionAvail().y - 65);
    const float width = ImGui::GetContentRegionAvail().x;
    const float left = std::clamp(width * 0.20f, 170.0f, 250.0f), right = std::clamp(width * 0.26f, 235.0f, 340.0f);
    ImGui::BeginChild("customresources", ImVec2(left, height), ImGuiChildFlags_Borders);
    ui::sectionHeader(tr(app, "Project", "Proyecto"), ui::icon::Edit);
    ImGui::SetNextItemWidth(-1); if (ImGui::InputTextWithHint("##customname", tr(app, "Name", "Nombre"), c.name.data(), c.name.size())) c.review = {};
    int engine = static_cast<int>(c.engine);
    const char* engines[] = {"Codename", "Psych", "V-Slice"}; ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##customengine", &engine, engines, 3)) { c.engine = static_cast<Engine>(engine); c.review = {}; }
    if (!c.ranking) {
        if (ImGui::Checkbox(tr(app, "Custom note type", "Tipo de nota custom"), &c.recipe.noteType)) c.review = {};
        if (c.recipe.noteType) { ImGui::SetNextItemWidth(-1); if (ImGui::InputTextWithHint("##customtype", tr(app, "Type in the chart", "Tipo en el chart"), c.type.data(), c.type.size())) c.review = {}; }
    }
    ui::sectionHeader(tr(app, "Resources", "Recursos"), ui::icon::FolderOpen);
    for (size_t i = 0; i < c.recipe.resources.size(); ++i) {
        const auto& r = c.recipe.resources[i];
        const std::string label = r.input.label + "##resource" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), c.resource == static_cast<int>(i))) {
            c.resource = static_cast<int>(i); c.animation = 0; c.frame = 0; c.inspectDirty = true; c.fit = true;
            c.draft.order.clear(); c.order.fill(0); c.assignment = -1;
            if (c.audio) c.audio->clearTracks();
        }
        ui::caption(customKindLabel(app, r.input.kind));
    }
    if (c.recipe.resources.empty()) ui::caption(tr(app, "Import your first image, sheet, sequence or sound.", "Importa tu primera imagen, hoja, secuencia o sonido."));
    if (ImGui::Button(tr(app, "Make PNG sequence", "Crear secuencia PNG"), ImVec2(-1, 0))) {
        ImportItem item; item.kind = ImportKind::Frames; item.label = tr(app, "Manual image sequence", "Secuencia manual de imágenes");
        for (const auto& r : c.recipe.resources) if (r.input.kind == ImportKind::Image) item.files.push_back(r.input.path);
        if (item.files.size() >= 2 && c.recipe.resources.size() < 128) {
            item.path = item.files.front().parent_path(); item.animations.push_back({"sequence", static_cast<int>(item.files.size())});
            c.recipe.resources.push_back({item}); c.resource = static_cast<int>(c.recipe.resources.size()) - 1;
            c.animation = 0; c.inspectDirty = true; c.fit = true; c.draft.order.clear(); c.order.fill(0);
        } else c.message = tr(app, "Import at least two single PNG images first.", "Importa al menos dos imágenes PNG sueltas primero.");
    }
    if (c.resource >= 0 && ImGui::Button(tr(app, "Remove selected resource", "Quitar recurso seleccionado"), ImVec2(-1, 0))) {
        const int old = c.resource;
        c.recipe.assignments.erase(std::remove_if(c.recipe.assignments.begin(), c.recipe.assignments.end(), [&](const auto& a) { return a.resource == old; }), c.recipe.assignments.end());
        for (auto& a : c.recipe.assignments) if (a.resource > old) --a.resource;
        c.recipe.resources.erase(c.recipe.resources.begin() + old); c.resource = c.recipe.resources.empty() ? -1 : 0; c.animation = 0;
        c.inspectDirty = true; c.review = {}; c.draft.order.clear(); c.order.fill(0); c.assignment = -1;
    }
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("custominspector", ImVec2(std::max(130.0f, width - left - right - 20), height), ImGuiChildFlags_Borders);
    ui::sectionHeader(tr(app, "Inspect before importing", "Revisar antes de importar"), ui::icon::Eye);
    if (c.resource >= 0 && c.resource < static_cast<int>(c.recipe.resources.size())) {
        auto& r = c.recipe.resources[static_cast<size_t>(c.resource)];
        ImGui::TextWrapped("%s", r.input.path.u8string().c_str());
        if (!r.input.atlas.empty()) ui::caption(r.input.atlas.u8string().c_str());
        if (r.input.kind == ImportKind::Sound) {
            ui::caption(tr(app, "Countdown or named sound effect. Engine export requires OGG Vorbis; WAV/MP3 are not renamed or silently converted.",
                "Cuenta atrás o efecto con nombre. El motor necesita OGG Vorbis; WAV/MP3 no se renombran ni se convierten a escondidas."));
            ui::caption(tr(app, "Listen before assigning this audio resource.",
                "Escucha el recurso de audio antes de asignarlo."));
            if (nlbuild::audioPlayback && ImGui::Button(tr(app, "Listen to this file", "Escuchar este archivo"))) {
                if (!c.audio) c.audio = std::make_unique<AudioEngine>();
                std::string error;
                if (c.audio->ready() || c.audio->init(&error)) { c.audio->loadTracksAsync({r.input.path.u8string()}); c.audioPending = true; }
                else c.message = error;
            }
            if (nlbuild::audioPlayback && c.audio && c.audioPending) {
                int loaded = 0; std::string error;
                if (c.audio->pollTrackLoad(&loaded, &error)) { c.audioPending = false; if (loaded) { c.audio->setMasterGain(c.volume); c.audio->play(); } else c.message = error; }
            }
            if (nlbuild::audioPlayback && c.audio && c.audio->trackCount()) {
                ImGui::SameLine(); if (ImGui::Button(tr(app, "Stop", "Detener"))) c.audio->pause();
                ImGui::SetNextItemWidth(-1); if (ImGui::SliderFloat("##soundvolume", &c.volume, 0, 1, tr(app, "Volume %.2f", "Volumen %.2f"))) c.audio->setMasterGain(c.volume);
                float time = static_cast<float>(c.audio->positionMs() / 1000.0);
                ImGui::SetNextItemWidth(-1); if (ImGui::SliderFloat("##soundtime", &time, 0, static_cast<float>(c.audio->durationMs() / 1000.0), "%.2f s")) c.audio->seekMs(time * 1000.0);
                const auto peaks = c.audio->waveformPeaks(160); if (!peaks.empty()) ImGui::PlotHistogram("##soundwave", peaks.data(), static_cast<int>(peaks.size()), 0, nullptr, 0, 1, ImVec2(-1, 90));
                ui::caption(tr(app, "Audition is separate from the song player.", "Esta escucha es independiente de la canción."));
            }
        } else {
            if (r.input.kind == ImportKind::Image || r.input.kind == ImportKind::Atlas) {
                if (ImGui::Button(tr(app, "Choose XML / TXT…", "Elegir XML / TXT…"))) openDialog(app, window, DialogAction::CustomAtlas);
                if (r.input.kind == ImportKind::Atlas) {
                    customSameLineIfFits(tr(app, "Use PNG only", "Usar sólo PNG"));
                    if (ImGui::Button(tr(app, "Use PNG only", "Usar sólo PNG"))) {
                        r.input.kind = ImportKind::Image; r.input.atlas.clear(); r.input.animations.clear();
                        r.input.label = r.input.path.filename().u8string(); r.regions.clear(); r.cellWidth = r.cellHeight = 0;
                        c.animation = 0; c.assignment = -1; c.inspectDirty = true; c.draft.order.clear(); c.order.fill(0);
                    }
                }
            }
            if (!r.input.animations.empty()) {
                c.animation = std::clamp(c.animation, 0, static_cast<int>(r.input.animations.size()) - 1); ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##resourceanim", r.input.animations[static_cast<size_t>(c.animation)].name.c_str())) {
                    for (size_t i = 0; i < r.input.animations.size(); ++i) if (ImGui::Selectable((r.input.animations[i].name + " · " + std::to_string(r.input.animations[i].frames)).c_str(), c.animation == static_cast<int>(i))) {
                        c.animation = static_cast<int>(i); c.inspectDirty = true; c.draft.order.clear(); c.order.fill(0);
                    }
                    ImGui::EndCombo();
                }
            }
            if (r.input.kind == ImportKind::Image || r.input.kind == ImportKind::Atlas) {
                ui::caption(tr(app, "Cell size (0 = original)", "Tamaño de celda (0 = original)"));
                ImGui::SetNextItemWidth(-1);
                int cell[2] = {r.cellWidth, r.cellHeight};
                if (ImGui::InputInt2("##customcell", cell)) {
                    r.cellWidth = std::clamp(cell[0], 0, 8192); r.cellHeight = std::clamp(cell[1], 0, 8192); r.regions.clear(); c.inspectDirty = true; c.draft.order.clear(); c.order.fill(0);
                }
                ImGui::Checkbox(tr(app, "Trace frames", "Trazar frames"), &c.trace);
                customSameLineIfFits(tr(app, "Clear traced areas", "Borrar áreas trazadas"));
                if (ImGui::Button(tr(app, "Clear traced areas", "Borrar áreas trazadas"))) { r.regions.clear(); c.inspectDirty = true; c.draft.order.clear(); c.order.fill(0); }
            }
            if (!c.inspected.frames.empty()) {
                const int count = static_cast<int>(c.inspected.frames.size());
                if (c.playing) {
                    const int step = static_cast<int>((ImGui::GetTime() - c.clockStart) * c.draft.fps);
                    const int ordered = c.draft.order.empty() ? count : static_cast<int>(c.draft.order.size());
                    const int index = c.draft.loop ? step % std::max(1, ordered) : std::min(step, ordered - 1);
                    c.frame = c.draft.order.empty() ? index : c.draft.order[static_cast<size_t>(index)];
                }
                c.frame = std::clamp(c.frame, 0, count - 1);
                if (c.uploadedFrame != c.frame) { uploadLiveImage(app, "notelab-live/custom-frame.png", c.inspected.frames[static_cast<size_t>(c.frame)]); c.uploadedFrame = c.frame; }
                const auto tex = app.renderer.previewImage("notelab-live/custom-frame.png");
                Thumb thumb; thumb.texture = tex.texture; thumb.w = static_cast<float>(c.inspected.frames[static_cast<size_t>(c.frame)].w); thumb.h = static_cast<float>(c.inspected.frames[static_cast<size_t>(c.frame)].h);
                const auto at = ImGui::GetCursorScreenPos(); drawThumbAt(ImGui::GetWindowDrawList(), thumb, at, 76, 3); ImGui::Dummy(ImVec2(76, 76));
                if (ImGui::GetContentRegionAvail().x > 280) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text("%d x %d · %d frames", c.inspected.sheet.empty() ? static_cast<int>(thumb.w) : c.inspected.sheet.w,
                    c.inspected.sheet.empty() ? static_cast<int>(thumb.h) : c.inspected.sheet.h, count);
                if (ImGui::Button(c.playing ? tr(app, "Pause preview", "Pausar preview") : tr(app, "Play preview", "Reproducir preview"))) { c.playing = !c.playing; c.clockStart = ImGui::GetTime(); }
                customSameLineIfFits(tr(app, "Fit", "Encuadrar")); if (ImGui::Button(tr(app, "Fit", "Encuadrar"))) c.fit = true;
                ImGui::EndGroup();
                ImGui::SetNextItemWidth(-1); int frame = c.frame + 1;
                if (ImGui::SliderInt("##frame", &frame, 1, count, "Frame %d")) { c.frame = frame - 1; c.playing = false; }
                if (ImGui::Button(tr(app, "Append this frame", "Añadir este frame"))) { if (c.draft.order.size() < kCustomFrameLimit) c.draft.order.push_back(c.frame); customOrderText(c); }
                customSameLineIfFits(tr(app, "Use all frames", "Usar todos los frames")); if (ImGui::Button(tr(app, "Use all frames", "Usar todos los frames"))) { c.draft.order.clear(); customOrderText(c); }
                ui::caption(tr(app, "Middle drag: pan · Wheel: zoom · Click a frame: append to order", "Arrastre central: pan · Rueda: zoom · Clic en frame: añadir al orden"));
            }
            drawCustomImageView(app);
        }
    }
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("customroles", ImVec2(0, height), ImGuiChildFlags_Borders);
    ui::sectionHeader(tr(app, "Define its role", "Definir su función"), ui::icon::Grid);
    int role = static_cast<int>(c.draft.role);
    const char* roles[] = {tr(app, "Note / receptor / splash", "Nota / receptor / splash"), tr(app, "HUD image / ranking", "Imagen HUD / ranking"),
        tr(app, "Countdown sound", "Sonido de cuenta atrás"), tr(app, "Named sound effect", "Efecto de sonido con nombre")};
    ImGui::SetNextItemWidth(-1); if (ImGui::Combo("##customrole", &role, roles, 4)) {
        c.draft.role = static_cast<CustomRole>(role); c.draft.hudIndex = role == 2 ? 15 : 0; c.assignment = -1; c.review = {};
    }
    if (c.draft.role == CustomRole::Piece) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##custompart", partLabel(c.draft.part, app.spanish))) {
            for (int p = 0; p < kPartCount; ++p) if (ImGui::Selectable(partLabel(static_cast<Part>(p), app.spanish), p == static_cast<int>(c.draft.part))) { c.draft.part = static_cast<Part>(p); c.draft.variant = 0; }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##customdir", directionLabel(c.draft.direction, app.spanish).c_str())) {
            for (int d = 0; d < 4; ++d) if (ImGui::Selectable(directionLabel(d, app.spanish).c_str(), c.draft.direction == d)) c.draft.direction = d;
            ImGui::EndCombo();
        }
        if (c.draft.part == Part::Splash) { ui::caption(tr(app, "Variant", "Variante")); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##customvariant", &c.draft.variant); c.draft.variant = std::clamp(c.draft.variant, 0, 15); }
    } else if (c.draft.role == CustomRole::SoundEffect) {
        ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##effectname", tr(app, "Sound name", "Nombre del sonido"), c.soundName.data(), c.soundName.size());
        ui::caption(tr(app, "Included for play-sound blocks; no automatic trigger.", "Se incluye para bloques de sonido; no se dispara por importarlo."));
    } else {
        if (c.draft.role == CustomRole::CountdownSound && c.draft.hudIndex < 15) c.draft.hudIndex = 15;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##customhud", hudName(app, c.draft.hudIndex).c_str())) {
            const int first = c.draft.role == CustomRole::CountdownSound ? 15 : 0;
            for (int h = first; h < 19; ++h) if (ImGui::Selectable(hudName(app, h).c_str(), c.draft.hudIndex == h)) c.draft.hudIndex = h;
            ImGui::EndCombo();
        }
    }
    if (c.draft.role == CustomRole::Piece || c.draft.role == CustomRole::HudImage) {
        ui::caption(tr(app, "Frame order (1, 3, 2, 2…); empty = all", "Orden de frames (1, 3, 2, 2…); vacío = todos"));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##customorder", c.order.data(), c.order.size())) {
            parseCustomOrder(c.order.data(), static_cast<int>(c.inspected.frames.size()), c.draft.order, c.message); c.review = {};
        }
        if (ImGui::Button(tr(app, "Clear selection", "Borrar selección"))) { c.draft.order.clear(); c.order.fill(0); c.review = {}; }
        if (c.draft.role == CustomRole::HudImage) ui::caption(tr(app, "HUD graphics are static in these engines: select exactly one frame.", "Las imágenes del HUD son estáticas en estos motores: elige exactamente un frame."));
        else { ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##customfps", &c.draft.fps, 1, 120, "%.0f FPS"); ImGui::Checkbox(tr(app, "Loop animation", "Loop de animación"), &c.draft.loop); }
        ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##customscale", &c.draft.scale, 0.05f, 4, tr(app, "Scale %.2f", "Escala %.2f"));
        ImGui::Checkbox(tr(app, "Pixel art", "Pixel art"), &c.draft.pixel);
        if (c.draft.role == CustomRole::Piece) {
            ui::caption(tr(app, "Offsets (X / Y)", "Desplazamiento (X / Y)"));
            float xy[] = {c.draft.offsetX, c.draft.offsetY}; ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat2("##customoffsets", xy)) { c.draft.offsetX = xy[0]; c.draft.offsetY = xy[1]; }
        }
    }
    const std::string roleIssue = customRoleIssue(app);
    if (!roleIssue.empty()) { ImGui::PushTextWrapPos(0); ui::caption(roleIssue.c_str()); ImGui::PopTextWrapPos(); }
    ImGui::BeginDisabled(!roleIssue.empty());
    if (ui::primaryButton(tr(app, "Assign / replace role", "Asignar / reemplazar función"), ImVec2(-1, 0))) {
        c.draft.resource = c.resource; c.draft.animation = c.animation; c.draft.soundName = c.soundName.data();
        std::string error;
        if (parseCustomOrder(c.order.data(), static_cast<int>(c.inspected.frames.size()), c.draft.order, error)) {
            const auto old = std::find_if(c.recipe.assignments.begin(), c.recipe.assignments.end(), [&](const auto& a) { return sameCustomRole(a, c.draft); });
            if (old != c.recipe.assignments.end()) *old = c.draft;
            else if (c.recipe.assignments.size() < 192) c.recipe.assignments.push_back(c.draft);
            c.review = {}; c.message.clear();
        } else c.message = error;
    }
    ImGui::EndDisabled();
    ui::sectionHeader(tr(app, "Assigned resources", "Recursos asignados"), ui::icon::Layers);
    int remove = -1;
    for (size_t i = 0; i < c.recipe.assignments.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(customAssignmentLabel(app, c.recipe.assignments[i]).c_str(), c.assignment == static_cast<int>(i))) {
            c.assignment = static_cast<int>(i); c.draft = c.recipe.assignments[i]; c.resource = c.draft.resource; c.animation = c.draft.animation;
            c.inspectDirty = true; c.fit = true; customOrderText(c); std::snprintf(c.soundName.data(), c.soundName.size(), "%s", c.draft.soundName.c_str());
        }
        if (ImGui::BeginPopupContextItem("assignmentmenu")) {
            if (ImGui::MenuItem(tr(app, "Remove assignment", "Quitar asignación"))) remove = static_cast<int>(i);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (remove >= 0) { c.recipe.assignments.erase(c.recipe.assignments.begin() + remove); c.assignment = -1; c.review = {}; }
    ui::caption(tr(app, "Right click to remove. Unassigned files are not exported. Engine limitations are reported by the exporter.",
        "Clic derecho para quitar. Los archivos sin asignar no se exportan. El exportador avisa las limitaciones del motor."));
    const bool validate = ImGui::Button(tr(app, "Validate resources", "Validar recursos"), ImVec2(-1, 0));
    tutorialMark("cc-validate");
    if (validate) {
        c.recipe.type = c.type.data(); c.review = buildCustomStyle(c.recipe, c.engine, c.name.data());
        if (c.review.ok) tutorialSignal("cc-validate");
        c.message = c.review.ok ? tr(app, "Resources validated. Review export before installing.", "Recursos validados. Revisa la exportación antes de instalarlos.") :
            (c.review.errors.empty() ? std::string(tr(app, "Validation failed.", "La validación falló.")) : c.review.errors.front());
    }
    for (const auto& error : c.review.errors) ImGui::TextWrapped("%s", customText(app, error).c_str());
    ImGui::EndChild();
    tutorialMark("cc-roles");
    if (!c.message.empty()) { ImGui::PushTextWrapPos(0); ui::caption(customText(app, c.message).c_str()); ImGui::PopTextWrapPos(); }
    ImGui::Separator();
    if (ImGui::Button(tr(app, "Cancel", "Cancelar")) || escapeClosesWindow()) {
        if (unsaved) ImGui::OpenPopup("###customdiscard");
        else ImGui::CloseCurrentPopup();
    }
    bool discard = false;
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Always);
    if (ImGui::BeginPopupModal((ui::label(ui::icon::Warning, tr(app, "Discard your work?", "¿Descartar lo hecho?")) + "###customdiscard").c_str(), nullptr,
                               ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", (std::to_string(c.recipe.resources.size()) + tr(app, " imported resource(s) and ", " recurso(s) importado(s) y ") +
                                  std::to_string(c.recipe.assignments.size()) +
                                  tr(app, " assigned role(s) have not been applied. If you close, they are lost.",
                                          " función(es) asignada(s) sin aplicar. Si cierras, se pierden.")).c_str());
        ImGui::Spacing();
        if (ui::primaryButton(tr(app, "Keep editing", "Seguir editando"), ImVec2(160.0f, 0.0f)) || escapeClosesWindow()) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button(tr(app, "Discard", "Descartar"), ImVec2(120.0f, 0.0f))) {
            discard = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (discard) ImGui::CloseCurrentPopup();
    ImGui::SameLine(); ImGui::BeginDisabled(c.recipe.assignments.empty() || !c.name[0]);
    const bool applyPressed = ui::primaryButton(tr(app, "Apply custom resources", "Aplicar recursos propios"));
    tutorialMark("cc-apply");
    if (applyPressed || (app.autoCommit && !c.recipe.assignments.empty())) {
        if (createCustomNow(app)) ImGui::CloseCurrentPopup();
        app.autoCommit = false;
    }
    ImGui::EndDisabled(); ImGui::EndPopup();
}

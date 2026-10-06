#pragma once

// Crear una nota custom (pedido del autor, 4 oct 2026): desde el Catalogo o el
// «+» de Bloques, todo lo de una nota en un sitio: nombre, que hace (bloques o
// solo codigo), con que empieza, aspecto (sin obligar a un sprite propio),
// sonido al tocarla y bot. Al crearla, a sus bloques o a su codigo. Nada se
// escribe en el mod: la nota vive en el proyecto hasta exportarla.

// El nombre de archivo de un tipo en cada motor (el mismo que usa la vista Bloques).
std::string newNoteFile(Engine engine, const std::string& name) {
    if (engine == Engine::VSlice) return exportName(name);
    std::string file = name;
    for (char& c : file)
        if (std::strchr("<>:\"/\\|?*", c)) c = '_';
    return file;
}

std::string newNotePath(Engine engine, const std::string& name) {
    const std::string file = newNoteFile(engine, name);
    if (engine == Engine::Codename) return "data/notes/" + file + ".hx";
    if (engine == Engine::Psych) return "custom_notetypes/" + file + ".lua";
    return "scripts/notekinds/" + file + ".hxc";
}

// Lo basico de un script de tipo de nota, con los mismos eventos que escriben
// los bloques (NoteBlocks.cpp): al tocarla y al fallarla, solo para este tipo.
std::string newNoteTemplate(Engine engine, const std::string& name, bool es) {
    std::string safe;
    for (char c : name) {
        if (c == '"' || c == '\'' || c == '\\') safe += '\\';
        if (c != '\n' && c != '\r') safe += c;
    }
    const std::string hitLine = es ? "al tocarla" : "when it is hit";
    const std::string missLine = es ? "al fallarla" : "when it is missed";
    if (engine == Engine::Psych)
        return std::string("-- Note Lab: ") + (es ? "tipo de nota «" : "note type «") + name + (es ? "» (solo código)\n" : "» (code only)\n") +
               "\nfunction goodNoteHit(id, direction, noteType, isSustainNote)\n"
               "\tif noteType ~= '" + safe + "' or isSustainNote then return end\n"
               "\t-- " + hitLine + "\nend\n"
               "\nfunction noteMiss(id, direction, noteType, isSustainNote)\n"
               "\tif noteType ~= '" + safe + "' or isSustainNote then return end\n"
               "\t-- " + missLine + "\nend\n";
    if (engine == Engine::Codename)
        return std::string("// Note Lab: ") + (es ? "tipo de nota «" : "note type «") + name + (es ? "» (solo código)\n" : "» (code only)\n") +
               "\nfunction onPostNoteHit(event) {\n"
               "\tif (event.cancelled || event.noteType != \"" + safe + "\" || event.note == null || event.note.isSustainNote) return;\n"
               "\t// " + hitLine + "\n}\n"
               "\nfunction onPostPlayerMiss(event) {\n"
               "\tif (event.cancelled || event.noteType != \"" + safe + "\" || event.note == null || event.note.isSustainNote) return;\n"
               "\t// " + missLine + "\n}\n";
    std::string klass = "NoteLab";
    for (char c : exportName(name))
        if (std::isalnum(static_cast<unsigned char>(c))) klass += c;
    return std::string("// Note Lab: ") + (es ? "tipo de nota «" : "note type «") + name + (es ? "» (solo código)\n" : "» (code only)\n") +
           "import funkin.play.notes.notekind.NoteKind;\n"
           "\nclass " + klass + "NoteKind extends NoteKind\n{\n"
           "  public function new()\n  {\n    super(\"" + safe + "\", \"" + safe + " (Note Lab)\", null, null, false, null);\n  }\n"
           "\n  public override function onNoteHit(event:HitNoteScriptEvent):Void\n  {\n    if (event.eventCanceled) return;\n    // " + hitLine + "\n  }\n"
           "\n  public override function onNoteMiss(event:NoteScriptEvent):Void\n  {\n    if (event.eventCanceled) return;\n    // " + missLine + "\n  }\n"
           "}\n";
}

// Un tipo «solo codigo»: lleva su archivo propio y ningun evento de bloques.
bool codeOnlyType(const BlockProgram& program) {
    bool file = false;
    for (const auto& [id, node] : program.nodes) {
        if (node.key == "code.file") file = true;
        else if (node.key.rfind("event.", 0) == 0) return false;
    }
    return file;
}

bool newNoteNameTaken(const Source& source, const std::string& name) {
    const std::string key = lowerText(name);
    for (const NoteTypeEntry& type : source.noteTypes)
        if (lowerText(type.name) == key) return true;
    for (const auto& entry : source.typeBlocks)
        if (lowerText(entry.first) == key) return true;
    return false;
}

void openNewNote(NoteLabApp& app, int mode = 0) {
    if (app.sources.empty()) {
        setStatus(app, "Open a mod first: the new note goes into one of its mods.", "Abre un mod antes: la nota nueva va en uno de sus mods.");
        return;
    }
    auto& n = app.newNote;
    n = NoteLabApp::NewNote{};
    n.source = app.blocksSource >= 0 && app.blocksSource < static_cast<int>(app.sources.size()) ? app.blocksSource
             : app.selSource >= 0 && app.selSource < static_cast<int>(app.sources.size())       ? app.selSource
                                                                                                  : 0;
    n.mode = mode;
    // Desde Bloques se quiere editar: al crearla se va a sus bloques. Desde el
    // Catalogo, la ventana pasa a «Lista» y se elige que hacer.
    n.fromBlocks = app.centerTab == 1 && app.typesView == 1;
    n.requestOpen = true;
}

// Crearla: el tipo en el proyecto (en el Catalogo, «Tus notas»), con lo
// elegido. No abre nada: la ventana pasa a «Lista» y ofrece lo siguiente.
bool createNewNote(NoteLabApp& app) {
    auto& n = app.newNote;
    if (n.source < 0 || n.source >= static_cast<int>(app.sources.size())) return false;
    std::string name = n.name.data();
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    Source& source = *app.sources[static_cast<size_t>(n.source)];
    if (name.empty() || newNoteNameTaken(source, name)) return false;
    Engine engine;
    if (!sourceEngine(app, source, engine)) engine = Engine::Codename;
    BlockProgram program;
    if (n.mode == 1) {
        const CodeImportResult result = keepCustomSource(engine, program, newNotePath(engine, name), newNoteTemplate(engine, name, app.spanish));
        if (!result.applied) {
            n.message = result.error;
            return false;
        }
        program = result.program;
    } else {
        for (const BlockPreset& preset : blockPresets())
            if (n.preset == preset.key) {
                const std::vector<int> before = program.tops;
                addPreset(program, preset);
                nlblocks::placeNewStacks(program, before);
            }
        if (!n.sound.empty()) {
            // «cuando el jugador toca» (el que ya haya o uno nuevo) y el sonido debajo.
            int hat = -1;
            for (int top : program.tops)
                if (const BlockNode* node = program.node(top); node && node->key == "event.hit") hat = top;
            if (hat < 0) {
                hat = newBlock(program, "event.hit");
                placeTop(program, hat, 36.0f, 40.0f + 120.0f * static_cast<float>(program.tops.size()));
            }
            const int sound = newBlock(program, "do.sound");
            if (BlockNode* node = program.node(sound); node && !node->args.empty()) node->args[0].value = n.sound;
            attachAfter(program, lastOf(program, hat), sound);
        }
    }
    source.typeBlocks[name] = std::move(program);
    // Elegida en Bloques (por si se va alli), pero sin cambiar de vista.
    app.blocksSource = n.source;
    app.blocksType = name;
    app.blocksEngine = -1;
    blocksChanged(app);
    n.done = true;
    n.created = name;
    setStatus(app, "New note «" + name + "» in the Catalog (Your notes): it lives in the project until you export it; the mod is not touched.",
              "Nota nueva «" + name + "» en el Catálogo (Tus notas): vive en el proyecto hasta que la exportes; el mod no se toca.");
    return true;
}

// Ir a sus bloques (o a su codigo, si es «solo codigo»).
void newNoteToBlocks(NoteLabApp& app, int source, const std::string& name) {
    const auto& programs = app.sources[static_cast<size_t>(source)]->typeBlocks;
    const auto found = programs.find(name);
    app.blocksSource = source;
    app.blocksType = name;
    app.requestedTab = 1;
    app.typesView = 1;
    if (found != programs.end() && codeOnlyType(found->second)) app.blocksView = 1;
    else if (app.blocksView == 1) app.blocksView = 0;
}

// Un boton grande con su icono, su nombre y una linea: las opciones del creador.
bool noteCard(const char* id, const char* glyph, const char* title, const char* text, bool selected, ImVec2 size) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y),
                        selected ? IM_COL32(155, 123, 245, 46) : hovered ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 8), 9.0f);
    draw->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), selected ? ui::color::Accent : ui::color::Border, 9.0f, 0, selected ? 2.0f : 1.0f);
    const ImU32 ink = selected ? ui::color::Accent : ui::color::Muted;
    const float top = size.y < 60.0f ? (size.y - 22.0f) * 0.5f : 10.0f;
    draw->AddText(ImGui::GetFont(), 20.0f, ImVec2(at.x + 12.0f, at.y + top), ink, glyph);
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + 42.0f, at.y + top + 2.0f), ui::color::Text, title);
    if (text && *text) {
        ImGui::PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.88f, ImVec2(at.x + 12.0f, at.y + 38.0f), ui::color::Muted, text, nullptr, size.x - 24.0f);
        ImGui::PopClipRect();
    }
    return pressed;
}

// Lista: la nota ya esta en el Catalogo; lo siguiente lo eliges tu.
void drawNewNoteDone(NoteLabApp& app) {
    auto& n = app.newNote;
    const float width = 600.0f;
    const bool code = n.mode == 1;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Success));
    ImGui::PushFont(ui::fonts().semibold, 19.0f);
    ImGui::TextUnformatted(ui::label(ui::icon::Check, std::string("«") + n.created + tr(app, "» is ready", "» está lista")).c_str());
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ui::caption(tr(app, "It is saved in the Catalog, under «Your notes» (Custom notes). Nothing opens by itself: choose what to do now, or close and come back whenever you want.",
                        "Está guardada en el Catálogo, en «Tus notas» (Notas custom). No se abre nada solo: elige qué hacer ahora, o cierra y vuelve cuando quieras."));
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    const float gap = 10.0f;
    const ImVec2 size((width - gap * 2.0f) / 3.0f, 96.0f);
    int action = -1;
    if (noteCard("donecode", code ? ui::icon::Code : ui::icon::Puzzle, code ? tr(app, "Write its code", "Escribir su código") : tr(app, "Edit its blocks", "Editar sus bloques"),
                 code ? tr(app, "Its script, with the basics already in place.", "Su script, con lo básico ya puesto.")
                      : tr(app, "What it does when hit or missed.", "Lo que hace al tocarla o fallarla."),
                 false, size))
        action = 0;
    tutorialMark("newnote-next-blocks");
    ImGui::SameLine(0.0f, gap);
    const char* lookTitle = n.look == 1 ? tr(app, "Paint it", "Pintarla") : n.look == 2 ? tr(app, "Add my images", "Añadir mis imágenes")
                          : n.look == 3 ? tr(app, "Draw it", "Dibujarla") : tr(app, "Give it a look", "Darle aspecto");
    const char* lookText = n.look == 3 ? tr(app, "You choose how to start: template, free canvas…", "Eliges cómo empezar: plantilla, lienzo libre…")
                         : n.look == 0 ? tr(app, "Now it looks like the normal notes.", "Ahora se ve como las normales.")
                                       : tr(app, "Color, mark or your images on top of a skin.", "Color, marca o tus imágenes sobre un skin.");
    if (noteCard("donelook", n.look == 3 ? ui::icon::Edit : ui::icon::Brush, lookTitle, lookText, n.look != 0, size)) action = 1;
    tutorialMark("newnote-next-look");
    ImGui::SameLine(0.0f, gap);
    if (noteCard("donebot", ui::icon::Bot, tr(app, "Set up its bot", "Configurar su bot"),
                 tr(app, "Whether the bot hits it or avoids it.", "Si el bot la toca o la evita."), n.bot == 1, size))
        action = 2;
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    const bool another = ImGui::Button(ui::label(ui::icon::Add, tr(app, "Another note", "Otra nota")).c_str(), ImVec2(150.0f, 0.0f));
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, width - 150.0f - 8.0f - 160.0f));
    const bool close = ui::primaryButton(ui::label(ui::icon::Check, tr(app, "Done", "Hecho")), ImVec2(160.0f, 0.0f)) || escapeClosesWindow();
    tutorialMark("newnote-done");
    if (action < 0 && !close && !another) return;
    const int source = n.source;
    const std::string name = n.created;
    const int look = n.look;
    const int mode = n.mode;
    ImGui::CloseCurrentPopup();
    if (another) {
        openNewNote(app, mode);
        return;
    }
    if (action == 0) newNoteToBlocks(app, source, name);
    if (action == 1) {
        if (look == 3) openSpriteEditor(app, source, name);
        else {
            openTypeLook(app, source, name);
            app.typeLook.mode = look == 2 ? 1 : 0;
        }
    }
    if (action == 2) openCustomBot(app, source, name);
    if (close) {
        // A la vista: el Catalogo con la nota nueva.
        app.requestedTab = 1;
        if (app.typesView == 2) app.typesView = 0;
    }
}

void drawNewNoteModal(NoteLabApp& app) {
    auto& n = app.newNote;
    if (n.requestOpen) {
        ImGui::OpenPopup("###newnote");
        n.requestOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal((ui::label(ui::icon::Add, tr(app, "New custom note", "Nueva nota custom")) + "###newnote").c_str(), &open,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (n.source < 0 || n.source >= static_cast<int>(app.sources.size())) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (n.done) {
        drawNewNoteDone(app);
        ImGui::EndPopup();
        return;
    }
    Source& source = *app.sources[static_cast<size_t>(n.source)];
    Engine engine;
    if (!sourceEngine(app, source, engine)) engine = Engine::Codename;
    const float width = 600.0f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ui::caption(tr(app, "Everything a note needs in one place. Only the name is required: the rest can stay as it is and be changed later.",
                        "Todo lo de una nota en un sitio. Solo hace falta el nombre: lo demás puede quedarse como está y cambiarse después."));
    // El mod donde va, si hay varios abiertos.
    if (app.sources.size() > 1) {
        ui::sectionHeader(tr(app, "Mod", "Mod"), ui::icon::FolderOpen);
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##newnotesource", sourceName(source).c_str())) {
            for (size_t s = 0; s < app.sources.size(); ++s)
                if (ImGui::Selectable(sourceName(*app.sources[s]).c_str(), static_cast<int>(s) == n.source)) n.source = static_cast<int>(s);
            ImGui::EndCombo();
        }
    }
    // 1. El nombre.
    ui::sectionHeader(tr(app, "1 · Name", "1 · Nombre"), ui::icon::Edit);
    ImGui::SetNextItemWidth(width);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##newnotename", tr(app, "Mine, Bullet, Damage…", "Mina, Bala, Daño…"), n.name.data(), n.name.size());
    tutorialMark("newnote-name");
    std::string name = n.name.data();
    while (!name.empty() && name.back() == ' ') name.pop_back();
    const bool taken = !name.empty() && newNoteNameTaken(source, name);
    if (taken) ImGui::TextColored(ui::vec(ui::color::Warning), "%s", tr(app, "That mod already has a note with that name.", "Ese mod ya tiene una nota con ese nombre."));
    else ui::caption(tr(app, "As the chart writes it in its notes.", "Tal como lo escribe el chart en sus notas."));
    // 2. Que hace: bloques o solo codigo.
    ui::sectionHeader(tr(app, "2 · What it does", "2 · Qué hace"), ui::icon::Puzzle);
    {
        const float gap = 10.0f;
        const ImVec2 size((width - gap) * 0.5f, 72.0f);
        if (noteCard("modeblocks", ui::icon::Puzzle, tr(app, "With blocks", "Con bloques"),
                     tr(app, "Snap blocks like in Scratch; Note Lab writes each engine's code.", "Encaja bloques como en Scratch; Note Lab escribe el código de cada motor."),
                     n.mode == 0, size))
            n.mode = 0;
        tutorialMark("newnote-mode-0");
        ImGui::SameLine(0.0f, gap);
        const std::string codeText = std::string(tr(app, "You write its ", "Escribes tú su ")) + engineLabel(engine) + tr(app, " script, without blocks.", " script, sin bloques.");
        if (noteCard("modecode", ui::icon::Code, tr(app, "Code only", "Solo código"), codeText.c_str(), n.mode == 1, size)) n.mode = 1;
        tutorialMark("newnote-mode-1");
    }
    if (n.mode == 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "Start with", "Empezar con"));
        ImGui::SameLine(120.0f);
        std::string chosen = tr(app, "Nothing (empty)", "Nada (vacía)");
        for (const BlockPreset& preset : blockPresets())
            if (n.preset == preset.key) chosen = app.spanish ? preset.nameEs : preset.nameEn;
        ImGui::SetNextItemWidth(width - 120.0f);
        if (ImGui::BeginCombo("##newnotepreset", chosen.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable(tr(app, "Nothing (empty)", "Nada (vacía)"), n.preset.empty())) n.preset.clear();
            for (const BlockPreset& preset : blockPresets()) {
                if (preset.advanced) continue;
                if (ImGui::Selectable(app.spanish ? preset.nameEs : preset.nameEn, n.preset == preset.key)) n.preset = preset.key;
                ui::tooltip(app.spanish ? preset.helpEs : preset.helpEn);
            }
            ImGui::EndCombo();
        }
    }
    // 3. Aspecto: sin sprite propio por defecto. Nada se abre al crearla.
    ui::sectionHeader(tr(app, "3 · Look", "3 · Aspecto"), ui::icon::Brush);
    {
        const float gap = 8.0f;
        const ImVec2 size((width - gap * 3.0f) / 4.0f, 46.0f);
        const char* glyphs[4] = {ui::icon::Eye, ui::icon::Palette, ui::icon::Photo, ui::icon::Edit};
        const char* looks[4] = {tr(app, "Like the normal ones", "Como las normales"), tr(app, "Paint it", "Pintarla"),
                                tr(app, "My images", "Mis imágenes"), tr(app, "Draw it", "Dibujarla")};
        for (int k = 0; k < 4; ++k) {
            ImGui::PushID(k);
            if (noteCard("look", glyphs[k], looks[k], "", n.look == k, size)) n.look = k;
            tutorialMark(("newnote-look-" + std::to_string(k)).c_str());
            ImGui::PopID();
            if (k < 3) ImGui::SameLine(0.0f, gap);
        }
    }
    ui::caption(n.look == 0 ? tr(app, "No sprite of its own needed: it looks like the mod's normal notes. You can change it whenever you want.",
                                     "No hace falta sprite propio: se ve como las notas normales del mod. Puedes cambiarlo cuando quieras.")
                : n.look == 1 ? tr(app, "Its color, a mark or its opacity on top of a skin. You do it when you want, after creating it.",
                                        "Su color, una marca o su opacidad sobre un skin. Lo haces cuando quieras, después de crearla.")
                : n.look == 2 ? tr(app, "Your images, sheet or GIF. You add them when you want, after creating it.",
                                        "Tus imágenes, hoja o GIF. Las añades cuando quieras, después de crearla.")
                              : tr(app, "You draw it yourself: template with boxes, free canvas or a piece's canvas. When you want, after creating it.",
                                        "La dibujas tú: plantilla con huecos, lienzo libre o el de una pieza. Cuando quieras, después de crearla."));
    // 4. Sonido al tocarla (con bloques).
    if (n.mode == 0) {
        ui::sectionHeader(tr(app, "4 · Sound when hit", "4 · Sonido al tocarla"), ui::icon::Volume);
        const std::vector<std::string>& sounds = modSounds(source);
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##newnotesound", n.sound.empty() ? tr(app, "None", "Ninguno") : n.sound.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable(tr(app, "None", "Ninguno"), n.sound.empty())) n.sound.clear();
            for (const std::string& sound : sounds)
                if (ImGui::Selectable(sound.c_str(), n.sound == sound)) n.sound = sound;
            if (sounds.empty()) ImGui::TextDisabled("%s", tr(app, "This mod has no sounds.", "Este mod no tiene sonidos."));
            ImGui::EndCombo();
        }
        ui::caption(tr(app, "From the mod. Your own sounds come in through Resources.", "Del mod. Los tuyos entran por Recursos."));
    }
    // 5. Bot.
    ui::sectionHeader(n.mode == 0 ? tr(app, "5 · Bot", "5 · Bot") : tr(app, "4 · Bot", "4 · Bot"), ui::icon::Bot);
    const char* bots[] = {tr(app, "Like a normal note", "Como una normal"), tr(app, "Set it up after", "Configurarlo después")};
    ui::segmented("newnotebot", &n.bot, bots, 2);
    ui::caption(tr(app, "Whether the bot hits it or avoids it in the preview, for each side.", "Si el bot la toca o la evita en la vista previa, en cada lado."));
    if (!n.message.empty()) ImGui::TextColored(ui::vec(ui::color::Error), "%s", n.message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    const bool ready = !name.empty() && !taken;
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow()) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, width - 110.0f - 8.0f - 220.0f));
    ImGui::BeginDisabled(!ready);
    const bool create = ui::primaryButton(ui::label(ui::icon::Check, tr(app, "Create note", "Crear nota")), ImVec2(220.0f, 0.0f));
    tutorialMark("newnote-create");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", ready ? tr(app, "It goes to the Catalog (Your notes); then you choose what to do.", "Va al Catálogo (Tus notas); luego eliges qué hacer.")
                                      : tr(app, "Give it a name first.", "Ponle un nombre antes."));
    if ((create || (ready && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive())) && createNewNote(app)) {
        tutorialSignal("newnote");
        if (n.fromBlocks) {
            n.done = false;
            newNoteToBlocks(app, n.source, n.created);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

// La vista Bloques sin nota elegida: no hay nada que tocar hasta elegir una
// (arriba, en «Tipo»), crear una nueva o escribir solo codigo.
void drawBlocksEmpty(NoteLabApp& app) {
    const float width = std::min(580.0f, ImGui::GetContentRegionAvail().x);
    ImGui::Dummy(ImVec2(1.0f, 28.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::PushFont(ui::fonts().semibold, 20.0f);
    ImGui::TextUnformatted(tr(app, "Choose the note you are going to edit", "Elige la nota que vas a editar"));
    ImGui::PopFont();
    ImGui::TextColored(ui::vec(ui::color::Muted), "%s", tr(app,
        "The blocks editor wakes up with a note chosen: pick one above in «Type», create a new one, or write code only, save it and use it later.",
        "El editor de bloques se activa con una nota elegida: elígela arriba, en «Tipo», crea una nueva o escribe solo código, guárdalo y úsalo después."));
    ImGui::Dummy(ImVec2(1.0f, 8.0f));
    if (ui::primaryButton(ui::label(ui::icon::Add, tr(app, "Create a new note…", "Crear una nota nueva…")), ImVec2(230.0f, 38.0f))) openNewNote(app, 0);
    ImGui::SameLine(0.0f, 10.0f);
    if (ui::flatButton(ui::label(ui::icon::Code, tr(app, "Code only…", "Solo código…")),
                       tr(app, "A note whose script you write yourself, without blocks.", "Una nota cuyo script escribes tú, sin bloques."),
                       ImVec2(170.0f, 38.0f)))
        openNewNote(app, 1);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    tutorialMark("blocks-choose");
}

// En el Catalogo, las notas creadas en Note Lab (las del mod van debajo), cada
// una con sus botones: editarla (bloques o codigo), su aspecto y su bot. Un
// clic en el nombre la abre en Bloques.
void drawCreatedNotes(NoteLabApp& app) {
    std::vector<std::pair<int, std::string>> created;
    for (size_t s = 0; s < app.sources.size(); ++s)
        for (const auto& entry : app.sources[s]->typeBlocks)
            if (tutorialCreatedType(*app.sources[s], entry.first)) created.push_back({static_cast<int>(s), entry.first});
    if (created.empty()) return;
    ui::sectionHeader((std::string(tr(app, "Your notes", "Tus notas")) + " · " + std::to_string(created.size())).c_str(), ui::icon::Star);
    int act = -1, actSource = -1;
    std::string actName;
    for (const auto& [source, name] : created) {
        const Source& from = *app.sources[static_cast<size_t>(source)];
        const BlockProgram& program = from.typeBlocks.at(name);
        const bool code = codeOnlyType(program);
        const bool look = from.typeLooks.count(name) > 0;
        ImGui::PushID(source);
        ImGui::PushID(name.c_str());
        const float rowY = ImGui::GetCursorPosY();
        const float buttons = 3.0f * (ImGui::GetFrameHeight() + 4.0f) + 8.0f;
        const std::string label = ui::label(code ? ui::icon::Code : ui::icon::Puzzle, name) + "   " +
                                  (code ? tr(app, "code only", "solo código") : (std::to_string(program.nodes.size()) + tr(app, " blocks", " bloques"))) +
                                  (look ? tr(app, " · own look", " · aspecto propio") : "");
        if (ImGui::Selectable(label.c_str(), app.blocksSource == source && app.blocksType == name, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - buttons), ImGui::GetFrameHeight()))) {
            act = 0; actSource = source; actName = name;
        }
        ui::tooltip(code ? tr(app, "Open its code.", "Abrir su código.") : tr(app, "Open it in Blocks.", "Abrirla en Bloques."));
        ImGui::SetCursorPosY(rowY);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - buttons + 8.0f);
        if (ui::iconButton("edit", code ? ui::icon::Code : ui::icon::Puzzle, code ? "C" : "B",
                           code ? tr(app, "Write its code", "Escribir su código") : tr(app, "Edit its blocks", "Editar sus bloques"))) {
            act = 0; actSource = source; actName = name;
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ui::iconButton("look", ui::icon::Brush, "L", tr(app, "Its look…", "Su aspecto…"))) ImGui::OpenPopup("notelook");
        if (ImGui::BeginPopup("notelook")) {
            if (menuItem(ui::icon::Palette, tr(app, "Paint it (color, mark)…", "Pintarla (color, marca)…"))) { act = 1; actSource = source; actName = name; }
            if (menuItem(ui::icon::Photo, tr(app, "My images…", "Mis imágenes…"))) { act = 2; actSource = source; actName = name; }
            if (menuItem(ui::icon::Edit, tr(app, "Draw it…", "Dibujarla…"))) { act = 3; actSource = source; actName = name; }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ui::iconButton("bot", ui::icon::Bot, "T", tr(app, "Set up its bot…", "Configurar su bot…"))) { act = 4; actSource = source; actName = name; }
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (act < 0) return;
    if (act == 0) newNoteToBlocks(app, actSource, actName);
    else if (act == 1 || act == 2) {
        openTypeLook(app, actSource, actName);
        app.typeLook.mode = act == 2 ? 1 : 0;
    } else if (act == 3) openSpriteEditor(app, actSource, actName);
    else openCustomBot(app, actSource, actName);
}

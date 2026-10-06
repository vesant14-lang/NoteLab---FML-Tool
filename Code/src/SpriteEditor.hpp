#pragma once

// El editor de sprites (pedidos del autor, 4 oct 2026): un editor de dibujo
// pequeno «como Krita» sobre la hoja de una nota, sin salir de Note Lab.
//  - La hoja tiene una fila por pieza (nota, receptor, tramo y final del
//    sostenido, salpicadura) con huecos para sus fotogramas: los huecos con
//    algo de una fila son su animacion, en orden (la linea de tiempo).
//  - Herramientas: pincel y goma (tamano, dureza, opacidad), figuras
//    arrastrando, bote de pintura (no sale del hueco), cuentagotas, seleccion
//    por rango (o un clic en un hueco para cogerlo entero) y mover.
//  - Copiar, cortar, pegar en la seleccion o en el siguiente hueco libre.
//  - Presets de cada pieza (o el HUD entero) en una capa nueva, para no
//    llevarse lo dibujado.
//  - Capas con visibilidad y opacidad, deshacer, zoom (con presets y un mapa
//    de la hoja para las grandes) y desplazamiento; con Shift, el pincel y la
//    goma tiran una linea recta desde el ultimo punto.
//  - Pestanas: la hoja general y dibujos sueltos de una pieza (nuevos o
//    sacados de un hueco) que se meten en la general; otras hojas abiertas
//    para copiar de ellas, elegidas de una lista con vista previa.
// Para el aspecto de un tipo de nota (filas de nota y sostenido) o para las
// piezas de Crear HUD (las cinco); el mod no se toca.

namespace sprite_icon {
inline constexpr const char* Paste = "\xEE\x9D\xBF";      // U+E77F
inline constexpr const char* Cut = "\xEE\xA3\x86";        // U+E8C6
inline constexpr const char* Up = "\xEE\x9C\x8E";         // U+E70E
inline constexpr const char* DownArrow = "\xEE\x9C\x8D";  // U+E70D
inline constexpr const char* Previous = "\xEE\xA2\x92";   // U+E892
inline constexpr const char* Next = "\xEE\xA2\x93";       // U+E893
inline constexpr const char* SelectAll = "\xEE\xA2\xB3";  // U+E8B3
inline constexpr const char* Erase = "\xEE\x9D\x9C";      // U+E75C
}  // namespace sprite_icon

const char* spriteShapeName(const NoteLabApp& app, SpriteShapeKind kind) {
    switch (kind) {
        case SpriteShapeKind::Rect: return tr(app, "Rectangle", "Rectángulo");
        case SpriteShapeKind::Round: return tr(app, "Rounded", "Redondeado");
        case SpriteShapeKind::Ellipse: return tr(app, "Circle", "Círculo");
        case SpriteShapeKind::Triangle: return tr(app, "Triangle", "Triángulo");
        case SpriteShapeKind::Diamond: return tr(app, "Diamond", "Rombo");
        case SpriteShapeKind::Star: return tr(app, "Star", "Estrella");
        case SpriteShapeKind::Arrow: return tr(app, "Arrow", "Flecha");
        case SpriteShapeKind::Heart: return tr(app, "Heart", "Corazón");
    }
    return "?";
}

const char* drawnPieceName(const NoteLabApp& app, DrawnPiece piece) {
    switch (piece) {
        case DrawnPiece::Note: return tr(app, "Note", "Nota");
        case DrawnPiece::Strum: return tr(app, "Receptor", "Receptor");
        case DrawnPiece::StrumPress: return tr(app, "Receptor pressed", "Receptor pulsado");
        case DrawnPiece::StrumConfirm: return tr(app, "Receptor on hit", "Receptor al acertar");
        case DrawnPiece::HoldPiece: return tr(app, "Hold piece", "Tramo del sostenido");
        case DrawnPiece::HoldEnd: return tr(app, "Hold end", "Final del sostenido");
        case DrawnPiece::Splash: return tr(app, "Splash", "Salpicadura");
    }
    return "?";
}

const char* spritePresetName(const NoteLabApp& app, SpritePreset preset) {
    switch (preset) {
        case SpritePreset::Arrow: return tr(app, "Arrow", "Flecha");
        case SpritePreset::Strum: return tr(app, "Receptor", "Receptor");
        case SpritePreset::Mine: return tr(app, "Mine", "Mina");
        case SpritePreset::Heart: return tr(app, "Heart", "Corazón");
        case SpritePreset::Star: return tr(app, "Star", "Estrella");
        case SpritePreset::HoldPiece: return tr(app, "Hold piece", "Tramo");
        case SpritePreset::HoldEnd: return tr(app, "Hold end", "Final");
        case SpritePreset::Splash: return tr(app, "Splash", "Salpicadura");
        case SpritePreset::FullHud: return tr(app, "Whole HUD", "HUD completo");
    }
    return "?";
}

std::string spriteKey(int source, const std::string& type) { return std::to_string(source) + "|" + type; }

bool openSpriteFile(NoteLabApp& app, const fs::path& path, bool replace);
std::string keepSpriteFile(NoteLabApp& app, const std::string& name);

SheetLayout spriteLayout(const NoteLabApp::SpriteEditor& e) {
    if (e.docSingle && e.docFreeW > 0 && e.docFreeH > 0) return freeLayout(e.docFreeW, e.docFreeH, e.docPiece);
    return e.docSingle ? singleLayout(e.docPiece) : sheetLayout(e.docHud);
}

SheetLayout spriteDocLayout(const SpriteDoc& d) {
    if (d.single && d.freeW > 0 && d.freeH > 0) return freeLayout(d.freeW, d.freeH, d.piece);
    return d.single ? singleLayout(d.piece) : sheetLayout(d.hud);
}

// Algo cambio en un rango: si lo compuesto estaba al dia (o solo faltaban otros
// rangos), solo se recompone ese rango; si no, la hoja entera.
void spriteTouch(NoteLabApp::SpriteEditor& e, const SpriteRect& r) {
    if (e.composed == e.generation || e.dirtyFor == e.generation) {
        e.dirty = spriteUnion(e.dirty, r);
        ++e.generation;
        e.dirtyFor = e.generation;
    } else {
        ++e.generation;
    }
}

SpriteRect spriteAround(float x0, float y0, float x1, float y1, float pad) {
    return {static_cast<int>(std::floor(std::min(x0, x1) - pad)), static_cast<int>(std::floor(std::min(y0, y1) - pad)),
            static_cast<int>(std::ceil(std::max(x0, x1) + pad)) + 1, static_cast<int>(std::ceil(std::max(y0, y1) + pad)) + 1};
}

SpriteLayer spriteSheetLayer(const SheetLayout& layout, const std::string& name) {
    SpriteLayer layer;
    layer.name = name;
    layer.image = blankImage(layout.w, layout.h);
    return layer;
}

// Pixel art: sin bordes suaves en un rango (cada pixel, del todo o nada).
void spritePixelize(Image& image, const SpriteRect& r) {
    const SpriteRect box = spriteClip(r, image.w, image.h);
    for (int y = box.y0; y < box.y1; ++y)
        for (int x = box.x0; x < box.x1; ++x) {
            std::uint8_t* p = image.at(x, y);
            p[3] = p[3] >= 128 ? 255 : 0;
        }
}

// Un preset pintado en un rango de una capa (sin salirse de el).
void spritePaintPreset(Image& image, SpritePreset preset, const SpriteRect& target, bool pixel = false) {
    for (const SpriteShape& shape : spritePreset(preset, target)) paintSpriteShape(image, shape, &target);
    if (pixel) spritePixelize(image, target);
}

// Unas imagenes en los huecos de una fila, a partir de `slot`.
void spritePlaceFrames(Image& image, const SheetLayout& layout, int row, int slot, const std::vector<Image>& frames) {
    if (row < 0 || row >= static_cast<int>(layout.rows.size())) return;
    const SheetRow& r = layout.rows[static_cast<size_t>(row)];
    for (size_t k = 0; k < frames.size() && slot + static_cast<int>(k) < layout.slots; ++k) {
        const SpriteRect cell = layout.cell(row, slot + static_cast<int>(k));
        // En un lienzo libre (hueco no cuadrado) se ajusta a su tamano.
        const Image fitted = r.width > 0 ? fitIntoCell(frames[k], cell.w(), cell.h(), false, false)
                                         : fitInCell(frames[k], r.cell, r.piece == DrawnPiece::HoldPiece);
        clearSprite(image, cell);
        blit(image, fitted, cell.x0 + (cell.w() - fitted.w) / 2, cell.y0 + (cell.h() - fitted.h) / 2);
    }
}

// Para empezar: la hoja con una plantilla en el primer hueco de la nota
// (0 flecha, 1 mina, 2 corazon, 3 nada) y una capa vacia encima.
std::vector<SpriteLayer> spriteStartSheet(const NoteLabApp& app, const SheetLayout& layout, int which) {
    std::vector<SpriteLayer> layers{spriteSheetLayer(layout, tr(app, "Base", "Base")), spriteSheetLayer(layout, tr(app, "Drawing", "Dibujo"))};
    static const SpritePreset templates[3] = {SpritePreset::Arrow, SpritePreset::Mine, SpritePreset::Heart};
    if (which >= 0 && which < 3) spritePaintPreset(layers[0].image, templates[which], layout.cell(layout.rowOf(DrawnPiece::Note), 0));
    return layers;
}

void spriteReset(NoteLabApp::SpriteEditor& e) {
    e.undo.clear();
    e.redo.clear();
    e.stroking = e.panning = e.hasLast = false;
    e.selection = {};
    e.message.clear();
    e.zoom = 0.0f;
    e.pan = ImVec2(0.0f, 0.0f);
    e.composed = e.dirtyFor = -1;
    e.dirty = {};
    e.painted = -1;
    ++e.generation;
}

void spritePushUndo(NoteLabApp::SpriteEditor& e) {
    SpriteUndo entry;
    entry.layers = e.layers;
    entry.selected = e.layer;
    e.undo.push_back(std::move(entry));
    if (e.undo.size() > 30) e.undo.erase(e.undo.begin());
    e.redo.clear();
}

// Antes de un trazo: solo la capa que se toca.
void spritePushStroke(NoteLabApp::SpriteEditor& e) {
    SpriteUndo entry;
    entry.layer = e.layer;
    entry.image = e.layers[static_cast<size_t>(e.layer)].image;
    e.undo.push_back(std::move(entry));
    if (e.undo.size() > 30) e.undo.erase(e.undo.begin());
    e.redo.clear();
}

void spriteUndo(NoteLabApp::SpriteEditor& e, bool forward) {
    auto& from = forward ? e.redo : e.undo;
    auto& to = forward ? e.undo : e.redo;
    if (from.empty()) return;
    SpriteUndo entry = std::move(from.back());
    from.pop_back();
    SpriteUndo inverse;
    if (entry.layer >= 0 && entry.layer < static_cast<int>(e.layers.size())) {
        inverse.layer = entry.layer;
        inverse.image = std::move(e.layers[static_cast<size_t>(entry.layer)].image);
        e.layers[static_cast<size_t>(entry.layer)].image = std::move(entry.image);
    } else {
        inverse.layers = std::move(e.layers);
        inverse.selected = e.layer;
        e.layers = std::move(entry.layers);
        e.layer = entry.selected;
    }
    to.push_back(std::move(inverse));
    e.layer = std::clamp(e.layer, 0, static_cast<int>(e.layers.size()) - 1);
    ++e.generation;
}

// Elegir un hueco: la linea de tiempo lo sigue y queda seleccionado.
void spriteSelectCell(NoteLabApp::SpriteEditor& e, int row, int slot) {
    const SheetLayout layout = spriteLayout(e);
    if (row < 0 || row >= static_cast<int>(layout.rows.size())) return;
    e.row = row;
    e.slot = std::clamp(slot, 0, layout.slots - 1);
    e.selection = layout.cell(e.row, e.slot);
}

// ------------------------------------------------------------ pestanas --

// La pestana de delante vive en los campos del editor; las demas, en `docs`.
void spriteStoreDoc(NoteLabApp::SpriteEditor& e) {
    if (e.doc < 0 || e.doc >= static_cast<int>(e.docs.size())) return;
    SpriteDoc& d = e.docs[static_cast<size_t>(e.doc)];
    d.name = e.docName;
    d.single = e.docSingle;
    d.hud = e.docHud;
    d.reference = e.docReference;
    d.piece = e.docPiece;
    d.fromRow = e.fromRow;
    d.fromSlot = e.fromSlot;
    d.freeW = e.docFreeW;
    d.freeH = e.docFreeH;
    d.pixel = e.docPixel;
    d.layers = std::move(e.layers);
    d.layer = e.layer;
    d.undo = std::move(e.undo);
    d.redo = std::move(e.redo);
    d.selection = e.selection;
    d.zoom = e.zoom;
    d.pan = e.pan;
    d.row = e.row;
    d.slot = e.slot;
}

void spriteLoadDoc(NoteLabApp::SpriteEditor& e, int index) {
    if (index < 0 || index >= static_cast<int>(e.docs.size())) return;
    e.doc = index;
    SpriteDoc& d = e.docs[static_cast<size_t>(index)];
    e.docName = d.name;
    e.docSingle = d.single;
    e.docHud = d.hud;
    e.docReference = d.reference;
    e.docPiece = d.piece;
    e.fromRow = d.fromRow;
    e.fromSlot = d.fromSlot;
    e.docFreeW = d.freeW;
    e.docFreeH = d.freeH;
    e.docPixel = d.pixel;
    e.layers = std::move(d.layers);
    e.layer = std::clamp(d.layer, 0, std::max(0, static_cast<int>(e.layers.size()) - 1));
    e.undo = std::move(d.undo);
    e.redo = std::move(d.redo);
    e.selection = d.selection;
    e.zoom = d.zoom;
    e.pan = d.pan;
    e.row = d.row;
    e.slot = d.slot;
    e.stroking = e.panning = e.hasLast = false;
    e.composed = e.dirtyFor = -1;
    e.dirty = {};
    e.painted = -1;
    e.selectTab = index;
    ++e.generation;
}

void spriteSwitchDoc(NoteLabApp::SpriteEditor& e, int index) {
    if (index == e.doc || index < 0 || index >= static_cast<int>(e.docs.size())) return;
    spriteStoreDoc(e);
    spriteLoadDoc(e, index);
}

// Abrir una pestana nueva y ponerla delante.
void spritePushDoc(NoteLabApp::SpriteEditor& e, SpriteDoc doc) {
    spriteStoreDoc(e);
    e.docs.push_back(std::move(doc));
    spriteLoadDoc(e, static_cast<int>(e.docs.size()) - 1);
}

void spriteCloseDoc(NoteLabApp::SpriteEditor& e, int index) {
    if (index <= 0 || index >= static_cast<int>(e.docs.size())) return;
    if (index == e.doc) {
        e.docs.erase(e.docs.begin() + index);
        e.doc = -1;
        spriteLoadDoc(e, 0);
        return;
    }
    e.docs.erase(e.docs.begin() + index);
    if (e.doc > index) --e.doc;
    e.selectTab = e.doc;
}

// Las capas de la hoja general, este delante o no.
const std::vector<SpriteLayer>& spriteGeneralLayers(const NoteLabApp::SpriteEditor& e) {
    return e.doc == 0 || e.docs.empty() ? e.layers : e.docs.front().layers;
}

// Empezar con la hoja general como unica pestana.
void spriteStartDocs(NoteLabApp::SpriteEditor& e, const std::string& name, bool hud) {
    e.docs.assign(1, SpriteDoc{});
    e.doc = 0;
    e.docName = name;
    e.docSingle = e.docReference = false;
    e.docHud = hud;
    e.docPiece = DrawnPiece::Note;
    e.fromRow = e.fromSlot = -1;
    e.docFreeW = e.docFreeH = 0;
    e.docPixel = false;
    e.selectTab = 0;
}

void openSpriteEditor(NoteLabApp& app, int sourceIndex, const std::string& type) {
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(app.sources.size()) || type.empty()) return;
    auto& e = app.spriteEditor;
    e.target = 0;
    e.source = sourceIndex;
    e.type = type;
    spriteStartDocs(e, std::string(tr(app, "Look of ", "Aspecto de ")) + type, false);
    const SheetLayout layout = spriteLayout(e);
    // Lo dibujado antes para este tipo (en esta sesion), o una flecha para empezar.
    const auto saved = e.drawn.find(spriteKey(sourceIndex, type));
    const bool session = saved != e.drawn.end() && !saved->second.empty() && saved->second.front().image.w == layout.w;
    if (session) e.layers = saved->second;
    else e.layers = spriteStartSheet(app, layout, 0);
    e.layer = static_cast<int>(e.layers.size()) - 1;
    spriteReset(e);
    e.requestOpen = true;
    // Sin hoja de esta sesion: la que se guardo con el proyecto (capas y pestanas).
    const auto& recipes = app.sources[static_cast<size_t>(sourceIndex)]->recipes;
    const auto kept = recipes.find("sprite:" + type);
    const bool file = !session && kept != recipes.end() && !kept->second.sprite.empty() && openSpriteFile(app, pathFromUtf8(kept->second.sprite), true);
    spriteSelectCell(e, layout.rowOf(DrawnPiece::Note), 0);
    e.selection = {};
    e.focusPending = true;
    // Antes de pintar, como empezar (se salta con lo de antes ya elegido).
    e.choosing = true;
    e.hasWork = session || file;
    e.startKind = e.hasWork ? 0 : 1;
    e.startPiece = DrawnPiece::Note;
}

// Las piezas de Crear HUD (pedido del autor, 4 oct 2026: «meter ese editor de
// imagenes como opcion para el creador de HUD de notas»): el mismo editor,
// encima de la ventana del HUD; lo dibujado vuelve a ella y no se crea nada
// hasta «Crear HUD». `focus`: abrir acercado al primer hueco de esa pieza.
void openHudSpriteEditor(NoteLabApp& app, DrawnPiece piece, bool focus) {
    auto& c = app.createHud;
    auto& e = app.spriteEditor;
    if (c.source < 0 || c.source >= static_cast<int>(app.sources.size())) return;
    e.target = 1;
    e.source = c.source;
    e.type.clear();
    spriteStartDocs(e, tr(app, "General sheet", "Hoja general"), true);
    const SheetLayout layout = spriteLayout(e);
    // La hoja de esta sesion, o lo dibujado (guardado con el proyecto), o una flecha.
    const auto saved = e.drawn.find(c.layersKey);
    if (saved != e.drawn.end() && !saved->second.empty() && saved->second.front().image.w == layout.w) {
        e.layers = saved->second;
    } else {
        bool any = false;
        for (const auto& frames : c.drawn) any = any || !frames.empty();
        e.layers = spriteStartSheet(app, layout, any ? -1 : 0);
        for (size_t p = 0; p < c.drawn.size(); ++p)
            spritePlaceFrames(e.layers[0].image, layout, layout.rowOf(static_cast<DrawnPiece>(p)), 0, c.drawn[p]);
    }
    e.layer = static_cast<int>(e.layers.size()) - 1;
    e.rotate = c.recipe.drawnRotate;
    e.fps = c.recipe.drawnFps;
    spriteReset(e);
    e.requestOpen = true;
    // Sin hoja de esta sesion: la que se guardo con el HUD (capas y pestanas).
    const bool session = saved != e.drawn.end() && !saved->second.empty() && saved->second.front().image.w == layout.w;
    if (!session && !c.spriteFile.empty()) openSpriteFile(app, pathFromUtf8(c.spriteFile), true);
    spriteSelectCell(e, std::max(0, layout.rowOf(piece)), 0);
    e.selection = {};
    e.focusPending = focus;
    bool drawnBefore = false;
    for (const auto& frames : c.drawn) drawnBefore = drawnBefore || !frames.empty();
    e.choosing = true;
    e.hasWork = session || drawnBefore || !c.spriteFile.empty();
    e.startKind = e.hasWork ? 0 : 1;
    e.startPiece = piece;
}

void releaseSpritePreview(NoteLabApp& app) {
    app.renderer.removeDynamicFrame("notelab-live/sprite/canvas.png");
    for (int d = 0; d < 4; ++d) app.renderer.removeDynamicFrame("notelab-live/sprite/" + std::to_string(d) + ".png");
    for (int i = 0; i < 12; ++i) app.renderer.removeDynamicFrame("notelab-live/sprite/list-" + std::to_string(i) + ".png");
    app.spriteEditor.painted = app.spriteEditor.shown = -1;
    app.spriteEditor.listShown = false;
}

// Los fotogramas de cada pieza: los huecos con algo de su fila, en orden.
DrawnPieces spriteSheetFrames(const SheetLayout& layout, const Image& composite) {
    DrawnPieces out;
    for (int r = 0; r < static_cast<int>(layout.rows.size()); ++r)
        for (int s = 0; s < layout.slots; ++s) {
            const SpriteRect cell = layout.cell(r, s);
            if (spriteHasInk(composite, cell))
                out[static_cast<size_t>(layout.rows[static_cast<size_t>(r)].piece)].push_back(cropRect(composite, cell.x0, cell.y0, cell.w(), cell.h()));
        }
    return out;
}

// Pintarlo y ponerlo como el aspecto del tipo (el de antes, si era de Note Lab, se cambia).
bool applySpriteLook(NoteLabApp& app) {
    auto& e = app.spriteEditor;
    if (e.source < 0 || e.source >= static_cast<int>(app.sources.size())) return false;
    Source& source = *app.sources[static_cast<size_t>(e.source)];
    const std::vector<SpriteLayer>& general = spriteGeneralLayers(e);
    const DrawnPieces frames = spriteSheetFrames(sheetLayout(false), composeSpriteLayers(general));
    const auto& notes = frames[static_cast<size_t>(DrawnPiece::Note)];
    if (notes.empty()) {
        e.message = tr(app, "Draw the note first (its row's first box).", "Dibuja antes la nota (el primer hueco de su fila).");
        return false;
    }
    TypeLookFrames look;
    for (int d = 0; d < 4; ++d) {
        for (const Image& image : notes) look.note[static_cast<size_t>(d)].push_back(e.rotate ? rotateForDirection(image, d) : image);
        look.holdPiece[static_cast<size_t>(d)] = frames[static_cast<size_t>(DrawnPiece::HoldPiece)];
        look.holdEnd[static_cast<size_t>(d)] = frames[static_cast<size_t>(DrawnPiece::HoldEnd)];
    }
    const auto before = source.typeLooks.find(e.type);
    const std::string oldId = before != source.typeLooks.end() ? before->second : std::string();
    const std::string id = commitTypeLookFrames(app, e.source, e.type, look, e.message);
    if (id.empty()) return false;
    if (!oldId.empty() && oldId != id)
        for (size_t i = 0; i < source.catalog.styles.size(); ++i)
            if (source.catalog.styles[i].id == oldId) {
                deleteVariant(app, e.source, static_cast<int>(i));
                break;
            }
    e.drawn[spriteKey(e.source, e.type)] = general;
    // Las capas y pestanas, con el proyecto (en su receta «sprite:<tipo>»).
    CreationRecipe kept;
    kept.styleId = id;
    kept.kind = "sprite";
    kept.sprite = keepSpriteFile(app, e.type);
    if (!kept.sprite.empty()) source.recipes["sprite:" + e.type] = kept;
    tutorialSignal("sp-use");
    setStatus(app, "Sprite of " + e.type + " ready: it shows in the preview and goes with the note when you export it.",
              "Sprite de " + e.type + " listo: se ve en la vista previa y va con la nota al exportarla.");
    return true;
}

// Lo dibujado como las piezas del HUD que se esta creando.
void applySpriteHud(NoteLabApp& app) {
    auto& c = app.createHud;
    auto& e = app.spriteEditor;
    const std::vector<SpriteLayer>& general = spriteGeneralLayers(e);
    c.drawn = spriteSheetFrames(sheetLayout(true), composeSpriteLayers(general));
    c.useDrawn = std::any_of(c.drawn.begin(), c.drawn.end(), [](const std::vector<Image>& frames) { return !frames.empty(); });
    c.recipe.drawnRotate = e.rotate;
    c.recipe.drawnFps = e.fps;
    ++c.drawnVersion;
    e.drawn[c.layersKey] = general;
    c.spriteFile = keepSpriteFile(app, std::string(c.name.data()));
    tutorialSignal("sp-use");
    tutorialSignal("hud-shape");
}

// Una capa nueva encima de la elegida (deshacer la quita).
SpriteLayer& spriteAddLayer(NoteLabApp::SpriteEditor& e, SpriteLayer layer) {
    spritePushUndo(e);
    e.layers.insert(e.layers.begin() + e.layer + 1, std::move(layer));
    ++e.layer;
    ++e.generation;
    return e.layers[static_cast<size_t>(e.layer)];
}

// Un preset en una capa nueva: en la seleccion, en el hueco elegido si es de
// su clase, o en el primer hueco libre de su fila. El HUD completo, en el
// primer hueco de cada fila.
void applySpritePreset(NoteLabApp& app, SpritePreset preset) {
    auto& e = app.spriteEditor;
    const SheetLayout layout = spriteLayout(e);
    SpriteLayer layer = spriteSheetLayer(layout, std::string(tr(app, "Preset · ", "Preset · ")) + spritePresetName(app, preset));
    tutorialSignal("sp-preset");
    if (preset == SpritePreset::FullHud) {
        for (int r = 0; r < static_cast<int>(layout.rows.size()); ++r) {
            const DrawnPiece piece = layout.rows[static_cast<size_t>(r)].piece;
            // El pulsado y el acierto se quedan sin dibujar: salen del de reposo.
            if (piece == DrawnPiece::StrumPress || piece == DrawnPiece::StrumConfirm) continue;
            const SpritePreset one = piece == DrawnPiece::Strum ? SpritePreset::Strum : piece == DrawnPiece::HoldPiece ? SpritePreset::HoldPiece
                                   : piece == DrawnPiece::HoldEnd ? SpritePreset::HoldEnd : piece == DrawnPiece::Splash ? SpritePreset::Splash
                                                                                         : SpritePreset::Arrow;
            spritePaintPreset(layer.image, one, layout.cell(r, 0));
        }
        spriteAddLayer(e, std::move(layer));
        return;
    }
    SpriteRect target = e.selection;
    if (target.empty()) {
        // Los de una pieza (receptor, sostenido, salpicadura) van a su fila; los
        // demas (flecha, mina...), al hueco elegido si no es de sostenido.
        const bool specific = preset == SpritePreset::Strum || preset == SpritePreset::HoldPiece || preset == SpritePreset::HoldEnd ||
                              preset == SpritePreset::Splash;
        const DrawnPiece current = layout.rows[static_cast<size_t>(std::clamp(e.row, 0, static_cast<int>(layout.rows.size()) - 1))].piece;
        const bool holdRow = current == DrawnPiece::HoldPiece || current == DrawnPiece::HoldEnd;
        const bool strumRow = current == DrawnPiece::Strum || current == DrawnPiece::StrumPress || current == DrawnPiece::StrumConfirm;
        const bool fits = specific ? current == presetPiece(preset) || (preset == SpritePreset::Strum && strumRow) : !holdRow;
        int row = e.row, slot = e.slot;
        if (!e.docSingle && !fits) {
            row = layout.rowOf(presetPiece(preset));
            if (row < 0) row = layout.rowOf(DrawnPiece::Note);
            slot = 0;
            for (int s = 0; s < layout.slots; ++s)
                if (!e.used.empty() && !e.used[static_cast<size_t>(row * kSheetSlots + s)]) {
                    slot = s;
                    break;
                }
        }
        spriteSelectCell(e, row, slot);
        target = e.selection;
    }
    spritePaintPreset(layer.image, preset, target, e.docPixel);
    spriteAddLayer(e, std::move(layer));
}

// ---------------------------------------------------- copiar y pegar --

SpriteRect spriteWorkRect(const NoteLabApp::SpriteEditor& e) {
    return e.selection.empty() ? spriteLayout(e).cell(e.row, e.slot) : e.selection;
}

void spriteCopy(NoteLabApp& app, bool cut) {
    auto& e = app.spriteEditor;
    const Image visible = composeSpriteLayers(e.layers);
    const SpriteRect r = spriteClip(spriteWorkRect(e), visible.w, visible.h);
    if (r.empty()) return;
    // Se copia lo que se ve (todas las capas visibles); sirve entre pestanas.
    e.clipboard = cropRect(visible, r.x0, r.y0, r.w(), r.h());
    e.message = tr(app, "Copied: paste it in a selection, a box or another tab.", "Copiado: pégalo en una selección, un hueco u otra pestaña.");
    if (!cut) return;
    spritePushUndo(e);
    for (SpriteLayer& layer : e.layers)
        if (layer.visible) clearSprite(layer.image, r);
    ++e.generation;
}

// Pegar en una capa nueva, centrado en `target`.
void spritePasteAt(NoteLabApp& app, const SpriteRect& target) {
    auto& e = app.spriteEditor;
    if (e.clipboard.empty() || target.empty()) return;
    const SheetLayout layout = spriteLayout(e);
    SpriteLayer layer = spriteSheetLayer(layout, tr(app, "Pasted", "Pegado"));
    const int x = target.x0 + (target.w() - e.clipboard.w) / 2, y = target.y0 + (target.h() - e.clipboard.h) / 2;
    blit(layer.image, e.clipboard, x, y);
    spriteAddLayer(e, std::move(layer));
    e.selection = spriteClip({x, y, x + e.clipboard.w, y + e.clipboard.h}, layout.w, layout.h);
    e.message.clear();
}

// El siguiente hueco libre de la fila elegida (despues del elegido).
int spriteFreeSlot(const NoteLabApp::SpriteEditor& e, int row, int after) {
    const int slots = spriteLayout(e).slots;
    for (int k = 1; k <= slots; ++k) {
        const int s = (after + k) % slots;
        const size_t index = static_cast<size_t>(row * kSheetSlots + s);
        if (index < e.used.size() && !e.used[index]) return s;
    }
    return -1;
}

void spritePasteFree(NoteLabApp& app) {
    auto& e = app.spriteEditor;
    if (e.clipboard.empty()) {
        e.message = tr(app, "Copy something first (Ctrl+C).", "Copia algo antes (Ctrl+C).");
        return;
    }
    const int slot = spriteFreeSlot(e, e.row, e.slot);
    if (slot < 0) {
        e.message = tr(app, "This row has no free box.", "Esta fila no tiene huecos libres.");
        return;
    }
    spriteSelectCell(e, e.row, slot);
    spritePasteAt(app, spriteLayout(e).cell(e.row, slot));
    tutorialSignal("sp-copy");
}

// --------------------------------------------------- fotogramas --

// Copia el hueco `from` sobre `to` en todas las capas (vacio = borrar).
void spriteCopyCell(NoteLabApp::SpriteEditor& e, const SpriteRect& from, const SpriteRect& to) {
    for (SpriteLayer& layer : e.layers) {
        const Image piece = from.empty() ? Image{} : cropRect(layer.image, from.x0, from.y0, from.w(), from.h());
        clearSprite(layer.image, to);
        if (!piece.empty()) blit(layer.image, piece, to.x0, to.y0);
    }
}

void spriteDuplicateFrame(NoteLabApp& app) {
    auto& e = app.spriteEditor;
    const SheetLayout layout = spriteLayout(e);
    const int slot = spriteFreeSlot(e, e.row, e.slot);
    if (slot < 0) {
        e.message = tr(app, "This row has no free box.", "Esta fila no tiene huecos libres.");
        return;
    }
    spritePushUndo(e);
    spriteCopyCell(e, layout.cell(e.row, e.slot), layout.cell(e.row, slot));
    spriteSelectCell(e, e.row, slot);
    ++e.generation;
    tutorialSignal("sp-frames");
}

// Borrar un fotograma corre los de detras para no dejar hueco.
void spriteDeleteFrame(NoteLabApp& app) {
    auto& e = app.spriteEditor;
    const SheetLayout layout = spriteLayout(e);
    spritePushUndo(e);
    for (int s = e.slot; s < layout.slots; ++s)
        spriteCopyCell(e, s + 1 < layout.slots ? layout.cell(e.row, s + 1) : SpriteRect{}, layout.cell(e.row, s));
    ++e.generation;
}

void spriteSwapFrames(NoteLabApp& app, int other) {
    auto& e = app.spriteEditor;
    const SheetLayout layout = spriteLayout(e);
    if (other < 0 || other >= layout.slots) return;
    spritePushUndo(e);
    const SpriteRect a = layout.cell(e.row, e.slot), b = layout.cell(e.row, other);
    for (SpriteLayer& layer : e.layers) {
        const Image first = cropRect(layer.image, a.x0, a.y0, a.w(), a.h());
        const Image second = cropRect(layer.image, b.x0, b.y0, b.w(), b.h());
        clearSprite(layer.image, a);
        clearSprite(layer.image, b);
        blit(layer.image, second, a.x0, a.y0);
        blit(layer.image, first, b.x0, b.y0);
    }
    spriteSelectCell(e, e.row, other);
    ++e.generation;
}

// ----------------------------------------------------------- cargar --

// El estilo que corresponde: el de partida del HUD, o el aspecto del tipo (o
// el skin del mod).
int spriteStyleFor(const NoteLabApp& app) {
    const auto& e = app.spriteEditor;
    const Source& source = *app.sources[static_cast<size_t>(e.source)];
    if (e.target == 1) return app.createHud.base;
    int index = -1;
    const auto look = source.typeLooks.find(e.type);
    for (size_t i = 0; i < source.catalog.styles.size() && look != source.typeLooks.end(); ++i)
        if (source.catalog.styles[i].id == look->second) index = static_cast<int>(i);
    const std::vector<int> styles = paintableStyles(source);
    for (int i : styles)
        if (index < 0 && source.catalog.styles[static_cast<size_t>(i)].use == StyleUse::Default) index = i;
    if (index < 0 && !styles.empty()) index = styles.front();
    return index;
}

// Las piezas de un estilo (la flecha de la izquierda) en una capa de una hoja.
int spriteStyleLayer(const NoteLabApp& app, int styleIndex, const SheetLayout& layout, SpriteLayer& layer) {
    const Source& source = *app.sources[static_cast<size_t>(app.spriteEditor.source)];
    if (styleIndex < 0 || styleIndex >= static_cast<int>(source.catalog.styles.size())) return 0;
    const NoteStyle& style = source.catalog.styles[static_cast<size_t>(styleIndex)];
    const ExportIo io = memoryIo(source, style);
    layer = spriteSheetLayer(layout, std::string(tr(app, "From ", "De ")) + style.name);
    int placed = 0;
    for (int r = 0; r < static_cast<int>(layout.rows.size()); ++r) {
        const DrawnPiece piece = layout.rows[static_cast<size_t>(r)].piece;
        const Part part = piece == DrawnPiece::Note ? Part::Note : piece == DrawnPiece::Strum ? Part::StrumStatic
                        : piece == DrawnPiece::StrumPress ? Part::StrumPress : piece == DrawnPiece::StrumConfirm ? Part::StrumConfirm
                        : piece == DrawnPiece::HoldPiece ? Part::HoldPiece : piece == DrawnPiece::HoldEnd ? Part::HoldEnd : Part::Splash;
        const std::vector<Image> frames = stylePieceFrames(style, part, 0, io);
        spritePlaceFrames(layer.image, layout, r, 0, frames);
        placed += static_cast<int>(std::min<size_t>(frames.size(), static_cast<size_t>(layout.slots)));
    }
    return placed;
}

void spriteLoadStyle(NoteLabApp& app) {
    auto& e = app.spriteEditor;
    if (e.source < 0 || e.source >= static_cast<int>(app.sources.size()) || e.docSingle) return;
    SpriteLayer layer;
    if (spriteStyleLayer(app, spriteStyleFor(app), spriteLayout(e), layer) == 0) {
        e.message = tr(app, "That style's pieces could not be read.", "No se pudieron leer las piezas de ese estilo.");
        return;
    }
    spriteAddLayer(e, std::move(layer));
    e.message.clear();
}

// ----------------------------------------------- dibujos sueltos --

// Un dibujo suelto de una pieza en su propia pestana: nuevo (con su preset de
// base) o sacado del hueco elegido de la general, para volver a el.
void spriteNewSingle(NoteLabApp& app, DrawnPiece piece, bool fromCell) {
    auto& e = app.spriteEditor;
    SpriteDoc doc;
    doc.single = true;
    doc.hud = e.docHud;
    doc.piece = piece;
    const SheetLayout single = singleLayout(piece);
    SpriteLayer base = spriteSheetLayer(single, tr(app, "Base", "Base"));
    if (fromCell && !e.docSingle) {
        const SheetLayout layout = spriteLayout(e);
        const SpriteRect cell = layout.cell(e.row, e.slot);
        doc.piece = layout.rows[static_cast<size_t>(e.row)].piece;
        doc.fromRow = e.row;
        doc.fromSlot = e.slot;
        base = spriteSheetLayer(singleLayout(doc.piece), tr(app, "Base", "Base"));
        base.image = cropRect(composeSpriteLayers(e.layers), cell.x0, cell.y0, cell.w(), cell.h());
        doc.name = std::string(drawnPieceName(app, doc.piece)) + " " + std::to_string(e.slot + 1);
    } else {
        const bool strumPiece = piece == DrawnPiece::Strum || piece == DrawnPiece::StrumPress || piece == DrawnPiece::StrumConfirm;
        const SpritePreset preset = strumPiece ? SpritePreset::Strum : piece == DrawnPiece::HoldPiece ? SpritePreset::HoldPiece
                                  : piece == DrawnPiece::HoldEnd ? SpritePreset::HoldEnd : piece == DrawnPiece::Splash ? SpritePreset::Splash
                                                                                        : SpritePreset::Arrow;
        spritePaintPreset(base.image, preset, {0, 0, single.w, single.h});
        doc.name = std::string(tr(app, "Drawing · ", "Dibujo · ")) + drawnPieceName(app, piece);
    }
    doc.layers = {base, spriteSheetLayer(singleLayout(doc.piece), tr(app, "Drawing", "Dibujo"))};
    doc.layer = 1;
    spritePushDoc(e, std::move(doc));
    e.focusPending = false;
}

// Un lienzo libre en su pestana (pedido del autor, 5 oct 2026): del tamano que
// se quiera; `piece` es lo que sera al pasarlo a la hoja general.
void spriteNewFree(NoteLabApp& app, int w, int h, bool pixel, DrawnPiece piece) {
    auto& e = app.spriteEditor;
    SpriteDoc doc;
    doc.single = true;
    doc.hud = e.docHud;
    doc.piece = piece;
    doc.freeW = std::clamp(w, kFreeCanvasMin, kFreeCanvasMax);
    doc.freeH = std::clamp(h, kFreeCanvasMin, kFreeCanvasMax);
    doc.pixel = pixel;
    const SheetLayout free = freeLayout(doc.freeW, doc.freeH, piece);
    doc.layers = {spriteSheetLayer(free, tr(app, "Base", "Base")), spriteSheetLayer(free, tr(app, "Drawing", "Dibujo"))};
    doc.layer = 1;
    doc.name = std::string(tr(app, "Canvas ", "Lienzo ")) + std::to_string(doc.freeW) + "×" + std::to_string(doc.freeH);
    spritePushDoc(e, std::move(doc));
    // Un pincel que tenga sentido en ese tamano: un pixel en pixel art.
    if (pixel) {
        e.brushSize = 1.0f;
        e.hardness = 1.0f;
        e.shapeOutline = std::min(e.shapeOutline, 1.0f);
    } else {
        e.brushSize = std::clamp(static_cast<float>(std::min(w, h)) / 16.0f, 2.0f, 24.0f);
    }
    e.focusPending = false;
    e.zoom = 0.0f;
}

// Pasar el dibujo suelto (o el lienzo libre) a la hoja general, en una capa
// nueva y como `piece`. `slot`: -2 de vuelta al hueco de donde salio (o el
// siguiente libre), -1 el siguiente hueco libre de su fila, >= 0 ese hueco
// (lo sustituye). Lo de otro tamano se ajusta al hueco sin deformarse; el pixel
// art crece por enteros, nitido.
void spriteSendToSheet(NoteLabApp& app, DrawnPiece piece, int slot) {
    auto& e = app.spriteEditor;
    if (!e.docSingle || e.docs.empty()) return;
    const Image drawing = composeSpriteLayers(e.layers);
    const bool pixel = e.docPixel;
    const int fromRow = e.fromRow, fromSlot = e.fromSlot;
    const std::string name = e.docName;
    spriteSwitchDoc(e, 0);
    const SheetLayout layout = spriteLayout(e);
    int row = layout.rowOf(piece);
    if (row < 0) {
        e.message = tr(app, "This sheet has no row for that piece.", "Esta hoja no tiene fila para esa pieza.");
        return;
    }
    bool replace = slot >= 0;
    if (slot == -2) {
        slot = -1;
        if (fromRow >= 0 && fromRow < static_cast<int>(layout.rows.size()) && layout.rows[static_cast<size_t>(fromRow)].piece == piece) {
            row = fromRow;
            slot = fromSlot;
            replace = true;
        }
    }
    if (slot < 0) {
        // Lo compuesto puede no estar al dia: se mira la hoja otra vez.
        const Image visible = composeSpriteLayers(e.layers);
        for (int s = 0; s < layout.slots && slot < 0; ++s)
            if (!spriteHasInk(visible, layout.cell(row, s))) slot = s;
        if (slot < 0) {
            e.message = tr(app, "That row has no free box: free one or pick a box.", "Esa fila no tiene huecos libres: libera uno o elige un hueco.");
            return;
        }
    }
    const SpriteRect cell = layout.cell(row, std::clamp(slot, 0, layout.slots - 1));
    spritePushUndo(e);
    if (replace)
        for (SpriteLayer& layer : e.layers) clearSprite(layer.image, cell);
    const Image fitted = drawing.w == cell.w() && drawing.h == cell.h()
                             ? drawing
                             : fitIntoCell(drawing, cell.w(), cell.h(), pixel, piece == DrawnPiece::HoldPiece);
    SpriteLayer layer = spriteSheetLayer(layout, name);
    blit(layer.image, fitted, cell.x0 + (cell.w() - fitted.w) / 2, cell.y0 + (cell.h() - fitted.h) / 2);
    e.layers.insert(e.layers.begin() + e.layer + 1, std::move(layer));
    ++e.layer;
    ++e.generation;
    spriteSelectCell(e, row, slot);
    e.selection = {};
    e.focusPending = true;
    e.message = std::string(tr(app, "In the general sheet: ", "En la hoja general: ")) + drawnPieceName(app, piece) + " " + std::to_string(slot + 1) + ".";
    tutorialSignal("sp-tabs");
}

void spriteInsertSingle(NoteLabApp& app) { spriteSendToSheet(app, app.spriteEditor.docPiece, -2); }

// Otra hoja (de esta sesion o de un estilo del mod) en una pestana, para copiar de ella.
void spriteOpenReference(NoteLabApp& app, const std::string& name, std::vector<SpriteLayer> layers, bool hud) {
    auto& e = app.spriteEditor;
    SpriteDoc doc;
    doc.name = name;
    doc.reference = true;
    doc.hud = hud;
    doc.layers = std::move(layers);
    doc.layer = static_cast<int>(doc.layers.size()) - 1;
    spritePushDoc(e, std::move(doc));
}

// Una hoja (PNG + XML/TXT), unas imagenes o un GIF: lo que tenga nombre de
// pieza va a su fila; una imagen suelta, al hueco elegido.
void loadSpriteSheetFile(NoteLabApp& app, const fs::path& path) {
    auto& e = app.spriteEditor;
    if (lowerText(path.extension().u8string()) == ".nlsprite") {
        e.message = openSpriteFile(app, path, false)
                        ? std::string(tr(app, "Opened: ", "Abierto: ")) + path.filename().u8string()
                        : std::string(tr(app, "That file isn't a Note Lab drawing.", "Ese archivo no es un dibujo de Note Lab."));
        return;
    }
    const SheetLayout layout = spriteLayout(e);
    SpriteLayer layer = spriteSheetLayer(layout, path.stem().u8string());
    int placed = 0;
    for (const ImportItem& item : scanImport({path})) {
        if (item.kind == ImportKind::Image || item.kind == ImportKind::Gif || item.kind == ImportKind::Frames) {
            const std::vector<Image> frames = importFrames(item, 0);
            spritePlaceFrames(layer.image, layout, e.row, e.slot, frames);
            placed += static_cast<int>(frames.size());
            continue;
        }
        for (size_t a = 0; a < item.animations.size(); ++a) {
            const PieceGuess& guess = item.animations[a].piece;
            if (!guess.found || guess.direction != 0 || guess.variant != 0) continue;
            const DrawnPiece piece = guess.part == Part::Note ? DrawnPiece::Note : guess.part == Part::StrumStatic ? DrawnPiece::Strum
                                   : guess.part == Part::StrumPress ? DrawnPiece::StrumPress : guess.part == Part::StrumConfirm ? DrawnPiece::StrumConfirm
                                   : guess.part == Part::HoldPiece ? DrawnPiece::HoldPiece : guess.part == Part::HoldEnd ? DrawnPiece::HoldEnd
                                   : guess.part == Part::Splash ? DrawnPiece::Splash : DrawnPiece::Note;
            if (guess.part != Part::Note && piece == DrawnPiece::Note) continue;
            const int row = layout.rowOf(piece);
            if (row < 0) continue;
            const std::vector<Image> frames = importFrames(item, static_cast<int>(a));
            spritePlaceFrames(layer.image, layout, row, 0, frames);
            placed += static_cast<int>(frames.size());
        }
    }
    if (placed == 0) {
        e.message = tr(app, "Nothing there looks like a note piece (purple0, arrowLEFT, purple hold piece…).",
                            "Ahí no hay nada con nombre de pieza (purple0, arrowLEFT, purple hold piece…).");
        return;
    }
    spriteAddLayer(e, std::move(layer));
    e.message.clear();
}

// ---------------------------------------------------------- botones --

// Los iconos de las herramientas, dibujados (no todos estan en la fuente).
void drawSpriteToolIcon(ImDrawList* draw, int tool, ImVec2 c, float s, ImU32 col) {
    auto p = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    const float t = std::max(1.5f, s * 0.13f);
    switch (tool) {
        case 0:   // pincel: mango y punta
            draw->AddLine(p(0.62f, -0.62f), p(-0.02f, 0.02f), col, t * 1.3f);
            draw->AddCircleFilled(p(-0.28f, 0.28f), s * 0.3f, col);
            draw->AddTriangleFilled(p(-0.5f, 0.4f), p(-0.75f, 0.75f), p(-0.4f, 0.5f), col);
            break;
        case 1: {  // goma
            const ImVec2 q[4] = {p(-0.72f, 0.22f), p(0.08f, -0.58f), p(0.62f, -0.04f), p(-0.18f, 0.76f)};
            draw->AddConvexPolyFilled(q, 4, ui::withAlpha(col, 70));
            const ImVec2 tip[4] = {p(-0.72f, 0.22f), p(-0.32f, -0.18f), p(0.22f, 0.36f), p(-0.18f, 0.76f)};
            draw->AddConvexPolyFilled(tip, 4, col);
            draw->AddPolyline(q, 4, col, ImDrawFlags_Closed, t);
            break;
        }
        case 2:   // figura: cuadrado y circulo
            draw->AddRect(p(-0.78f, -0.05f), p(0.12f, 0.78f), col, 2.0f, 0, t);
            draw->AddCircleFilled(p(0.28f, -0.3f), s * 0.42f, ui::withAlpha(col, 90));
            draw->AddCircle(p(0.28f, -0.3f), s * 0.42f, col, 0, t);
            break;
        case 3: {  // bote de pintura con su gota
            const ImVec2 q[4] = {p(-0.62f, -0.06f), p(-0.02f, -0.66f), p(0.5f, -0.14f), p(-0.1f, 0.46f)};
            draw->AddConvexPolyFilled(q, 4, ui::withAlpha(col, 80));
            draw->AddPolyline(q, 4, col, ImDrawFlags_Closed, t);
            draw->AddCircleFilled(p(0.6f, 0.52f), s * 0.17f, col);
            draw->AddTriangleFilled(p(0.6f, 0.2f), p(0.46f, 0.46f), p(0.74f, 0.46f), col);
            break;
        }
        case 4:   // cuentagotas
            draw->AddLine(p(0.4f, -0.4f), p(-0.55f, 0.55f), col, t * 1.4f);
            draw->AddCircleFilled(p(0.52f, -0.52f), s * 0.24f, col);
            draw->AddCircleFilled(p(-0.66f, 0.66f), s * 0.1f, col);
            break;
        case 5: {  // seleccion: rectangulo a trazos
            const ImVec2 a = p(-0.75f, -0.6f), b = p(0.75f, 0.6f);
            const float dash = s * 0.22f;
            for (float x = a.x; x < b.x; x += dash * 2.0f) {
                draw->AddLine(ImVec2(x, a.y), ImVec2(std::min(x + dash, b.x), a.y), col, t);
                draw->AddLine(ImVec2(x, b.y), ImVec2(std::min(x + dash, b.x), b.y), col, t);
            }
            for (float y = a.y; y < b.y; y += dash * 2.0f) {
                draw->AddLine(ImVec2(a.x, y), ImVec2(a.x, std::min(y + dash, b.y)), col, t);
                draw->AddLine(ImVec2(b.x, y), ImVec2(b.x, std::min(y + dash, b.y)), col, t);
            }
            break;
        }
        case 6:   // mover: cuatro flechas
            draw->AddLine(p(-0.7f, 0.0f), p(0.7f, 0.0f), col, t);
            draw->AddLine(p(0.0f, -0.7f), p(0.0f, 0.7f), col, t);
            draw->AddTriangleFilled(p(-0.85f, 0.0f), p(-0.55f, -0.22f), p(-0.55f, 0.22f), col);
            draw->AddTriangleFilled(p(0.85f, 0.0f), p(0.55f, -0.22f), p(0.55f, 0.22f), col);
            draw->AddTriangleFilled(p(0.0f, -0.85f), p(-0.22f, -0.55f), p(0.22f, -0.55f), col);
            draw->AddTriangleFilled(p(0.0f, 0.85f), p(-0.22f, 0.55f), p(0.22f, 0.55f), col);
            break;
    }
}

// Un boton cuadrado con icono dibujado; encendido si es la herramienta en uso.
bool spriteToolButton(int tool, bool selected, const std::string& tip) {
    const float size = 40.0f;
    ImGui::PushID(tool);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("tool", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 back = selected ? IM_COL32(155, 123, 245, 90) : hovered ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 8);
    draw->AddRectFilled(at, ImVec2(at.x + size, at.y + size), back, 7.0f);
    if (selected) draw->AddRect(at, ImVec2(at.x + size, at.y + size), ui::color::Accent, 7.0f, 0, 1.5f);
    drawSpriteToolIcon(draw, tool, ImVec2(at.x + size * 0.5f, at.y + size * 0.5f), size * 0.32f, selected ? ui::color::Text : ui::color::Muted);
    ui::tooltip(tip.c_str());
    tutorialMark(("sprite-tool-" + std::to_string(tool)).c_str());
    ImGui::PopID();
    return pressed;
}

// Un boton pequeno con un icono de la fuente y su explicacion al pasar.
bool spriteIconButton(const char* id, const char* glyph, const char* tip, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 5.0f));
    const bool pressed = ImGui::Button((std::string(glyph) + "##" + id).c_str(), ImVec2(32.0f, 0.0f));
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    ui::tooltip(tip);
    return pressed;
}

bool spriteColorButton(const char* id, ImU32& value) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(value);
    const bool changed = ImGui::ColorEdit4(id, &c.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar);
    if (changed) value = ImGui::ColorConvertFloat4ToU32(c);
    return changed;
}

// La miniatura de un preset: sus figuras dibujadas como vectores.
void drawPresetThumb(ImDrawList* draw, SpritePreset preset, ImVec2 a, ImVec2 b) {
    if (preset == SpritePreset::FullHud) {
        const float w = (b.x - a.x) / 3.0f, h = (b.y - a.y) / 2.0f;
        static const SpritePreset parts[5] = {SpritePreset::Arrow, SpritePreset::Strum, SpritePreset::HoldPiece, SpritePreset::HoldEnd, SpritePreset::Splash};
        for (int i = 0; i < 5; ++i) {
            const ImVec2 at(a.x + (i % 3) * w, a.y + (i / 3) * h);
            drawPresetThumb(draw, parts[i], ImVec2(at.x + 2.0f, at.y + 2.0f), ImVec2(at.x + w - 2.0f, at.y + h - 2.0f));
        }
        return;
    }
    const float side = std::min(b.x - a.x, b.y - a.y);
    const SpriteRect box{0, 0, 160, 160};
    const float k = side / 160.0f;
    const ImVec2 at(a.x + (b.x - a.x - side) * 0.5f, a.y + (b.y - a.y - side) * 0.5f);
    for (const SpriteShape& shape : spritePreset(preset, box)) {
        std::vector<ImVec2> points = spriteOutline(shape);
        for (ImVec2& pt : points) pt = ImVec2(at.x + pt.x * k, at.y + pt.y * k);
        if (shape.filled) draw->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), shape.fill);
        if (shape.outlineWidth > 0.0f && (shape.outline >> 24) != 0)
            draw->AddPolyline(points.data(), static_cast<int>(points.size()), shape.outline, ImDrawFlags_Closed, std::max(1.0f, shape.outlineWidth * k));
    }
}

void drawDashedRect(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 col, float dash = 5.0f) {
    for (float x = a.x; x < b.x; x += dash * 2.0f) {
        draw->AddLine(ImVec2(x, a.y), ImVec2(std::min(x + dash, b.x), a.y), col);
        draw->AddLine(ImVec2(x, b.y), ImVec2(std::min(x + dash, b.x), b.y), col);
    }
    for (float y = a.y; y < b.y; y += dash * 2.0f) {
        draw->AddLine(ImVec2(a.x, y), ImVec2(a.x, std::min(y + dash, b.y)), col);
        draw->AddLine(ImVec2(b.x, y), ImVec2(b.x, std::min(y + dash, b.y)), col);
    }
}

// ------------------------------------------------------ el archivo .nlsprite --

// Todo el editor en un .nlsprite: cada pestana con sus capas, y los ajustes.
SpriteFile spriteToFile(const NoteLabApp::SpriteEditor& e) {
    SpriteFile file;
    file.meta = json{{"target", e.target}, {"type", e.type}, {"fps", e.fps}, {"rotate", e.rotate}, {"doc", e.doc}}.dump();
    for (int i = 0; i < static_cast<int>(e.docs.size()); ++i) {
        const bool active = i == e.doc;
        const SpriteDoc& d = e.docs[static_cast<size_t>(i)];
        SpriteFileDoc out;
        out.meta = json{{"name", active ? e.docName : d.name}, {"single", active ? e.docSingle : d.single}, {"hud", active ? e.docHud : d.hud},
                        {"reference", active ? e.docReference : d.reference}, {"piece", static_cast<int>(active ? e.docPiece : d.piece)},
                        {"fromRow", active ? e.fromRow : d.fromRow}, {"fromSlot", active ? e.fromSlot : d.fromSlot},
                        {"row", active ? e.row : d.row}, {"slot", active ? e.slot : d.slot},
                        {"freeW", active ? e.docFreeW : d.freeW}, {"freeH", active ? e.docFreeH : d.freeH},
                        {"pixel", active ? e.docPixel : d.pixel}}.dump();
        for (const SpriteLayer& layer : active ? e.layers : d.layers) out.layers.push_back({layer.name, layer.image, layer.visible, layer.opacity});
        file.docs.push_back(std::move(out));
    }
    return file;
}

// Abrir un .nlsprite en el editor. Su hoja general sustituye a la de ahora
// solo si esta esta vacia (o si `replace`); si no, va en otra pestana para
// copiar de ella: no se pisa trabajo. Lo que no encaje, se salta.
bool spriteFromFile(NoteLabApp& app, const SpriteFile& file, bool replace) {
    auto& e = app.spriteEditor;
    const json meta = json::parse(file.meta, nullptr, false);
    if (meta.is_object() && meta.contains("fps") && meta["fps"].is_array())
        for (size_t i = 0; i < meta["fps"].size() && i < e.fps.size(); ++i)
            if (meta["fps"][i].is_number_integer()) e.fps[i] = std::clamp(meta["fps"][i].get<int>(), 1, 60);
    if (meta.is_object() && meta.contains("rotate") && meta["rotate"].is_boolean()) e.rotate = meta["rotate"].get<bool>();
    spriteStoreDoc(e);
    bool generalUsed = false;
    for (const SpriteLayer& layer : e.docs.front().layers) generalUsed = generalUsed || spriteHasInk(layer.image, {0, 0, layer.image.w, layer.image.h});
    std::vector<SpriteDoc> added;
    bool replaced = false;
    for (const SpriteFileDoc& one : file.docs) {
        const json m = json::parse(one.meta, nullptr, false);
        auto num = [&](const char* key, int fallback) { return m.is_object() && m.contains(key) && m[key].is_number_integer() ? m[key].get<int>() : fallback; };
        auto flag = [&](const char* key) { return m.is_object() && m.contains(key) && m[key].is_boolean() && m[key].get<bool>(); };
        SpriteDoc doc;
        doc.name = m.is_object() && m.contains("name") && m["name"].is_string() ? m["name"].get<std::string>() : std::string("?");
        doc.single = flag("single");
        doc.hud = flag("hud");
        doc.reference = flag("reference");
        doc.piece = static_cast<DrawnPiece>(std::clamp(num("piece", 0), 0, kDrawnPieceCount - 1));
        doc.fromRow = num("fromRow", -1);
        doc.fromSlot = num("fromSlot", -1);
        doc.row = num("row", 0);
        doc.slot = num("slot", 0);
        const int freeW = num("freeW", 0), freeH = num("freeH", 0);
        if (doc.single && freeW > 0 && freeH > 0) {
            doc.freeW = std::clamp(freeW, kFreeCanvasMin, kFreeCanvasMax);
            doc.freeH = std::clamp(freeH, kFreeCanvasMin, kFreeCanvasMax);
        }
        doc.pixel = flag("pixel");
        const SheetLayout layout = spriteDocLayout(doc);
        for (const SpriteFileLayer& layer : one.layers)
            if (layer.image.w == layout.w && layer.image.h == layout.h) doc.layers.push_back({layer.name, layer.image, layer.visible, layer.opacity});
        if (doc.layers.empty()) continue;
        doc.layer = static_cast<int>(doc.layers.size()) - 1;
        const bool general = !doc.single && !doc.reference && doc.hud == (e.target == 1);
        if (general && !replaced && (replace || !generalUsed)) {
            doc.name = e.docs.front().name;
            e.docs.front() = std::move(doc);
            replaced = true;
            continue;
        }
        if (general) doc.reference = true;   // otra hoja general: para copiar de ella
        if (!doc.single && doc.hud != (e.target == 1)) doc.reference = true;
        added.push_back(std::move(doc));
    }
    for (SpriteDoc& doc : added) e.docs.push_back(std::move(doc));
    e.doc = -1;
    spriteLoadDoc(e, 0);
    return replaced || !added.empty();
}

// Guardarlo donde se elija (nunca dentro de un mod abierto).
void saveSpriteFileTo(NoteLabApp& app, const fs::path& chosen) {
    fs::path path = chosen;
    if (lowerText(path.extension().u8string()) != ".nlsprite") path += ".nlsprite";
    if (insideOpenMod(app, path)) {
        setStatus(app, "That folder is inside an open mod: Note Lab never writes there.", "Esa carpeta está dentro de un mod abierto: Note Lab nunca escribe ahí.");
        return;
    }
    const std::vector<unsigned char> bytes = writeSpriteFile(spriteToFile(app.spriteEditor));
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (bytes.empty() || !out) {
        app.spriteEditor.message = tr(app, "The drawing could not be saved there.", "No se pudo guardar el dibujo ahí.");
        return;
    }
    app.spriteEditor.message = std::string(tr(app, "Saved: ", "Guardado: ")) + path.filename().u8string();
}

bool openSpriteFile(NoteLabApp& app, const fs::path& path, bool replace) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    SpriteFile file;
    if (!readSpriteFile(bytes, file)) return false;
    return spriteFromFile(app, file, replace);
}

// Lo dibujado se guarda solo con lo creado (para volver a editarlo con sus
// capas y pestanas otro dia): devuelve la ruta del .nlsprite.
std::string keepSpriteFile(NoteLabApp& app, const std::string& name) {
    const std::vector<unsigned char> bytes = writeSpriteFile(spriteToFile(app.spriteEditor));
    return bytes.empty() ? std::string() : saveCreatedBytes(bytes, exportName(name) + ".nlsprite");
}

// ------------------------------------------------------------ el PNG final --

// El archivo con todo junto, como quedara: en Crear HUD, el estilo de partida
// pintado con tus colores y lo dibujado encima (o solo lo dibujado); en un
// tipo, su aspecto. Se empaqueta con los nombres que buscan los motores.
void spriteBuildFinal(NoteLabApp& app) {
    auto& e = app.spriteEditor;
    const SheetLayout layout = sheetLayout(e.target == 1);
    const DrawnPieces pieces = spriteSheetFrames(layout, composeSpriteLayers(spriteGeneralLayers(e)));
    const std::string name = exportName(e.target == 1 ? std::string(app.createHud.name.data()) : e.type);
    SparrowOut out;
    if (e.target == 1) {
        auto& c = app.createHud;
        ArrowRecipe recipe = c.recipe;
        recipe.drawnRotate = e.rotate;
        recipe.drawnFps = e.fps;
        const Source& source = *app.sources[static_cast<size_t>(c.source)];
        if (e.finalMode == 1 || c.base < 0 || c.base >= static_cast<int>(source.catalog.styles.size())) {
            out = drawnPieceAtlas(pieces, recipe, name + ".png").atlas;
        } else {
            // El estilo de partida tal como se ve en Crear HUD (pintado) y lo dibujado
            // encima; sus imagenes salen de memoria, el resto del mod.
            const NoteStyle& base = source.catalog.styles[static_cast<size_t>(c.base)];
            NoteStyle style = c.live.painted >= 0 ? c.live.style : base;
            const DrawnSheets sheets = drawnPieceSheets(pieces, recipe);
            const std::array<std::string, 3> paths{sheets.arrows.empty() ? std::string() : "notelab-final/arrows.png",
                                                   sheets.holds.empty() ? std::string() : "notelab-final/holds.png",
                                                   sheets.splashes.empty() ? std::string() : "notelab-final/splashes.png"};
            bindDrawnPieces(style, pieces, recipe, paths, drawnArrowScale(base));
            auto memory = std::make_shared<std::map<std::string, std::vector<unsigned char>>>();
            for (size_t i = 0; i < c.live.images.size() && c.live.painted >= 0; ++i)
                (*memory)["notelab-live/hud/" + std::to_string(i) + ".png"] = encodePng(c.live.images[i].pixels);
            if (!paths[0].empty()) (*memory)[paths[0]] = encodePng(sheets.arrows);
            if (!paths[1].empty()) (*memory)[paths[1]] = encodePng(sheets.holds);
            if (!paths[2].empty()) (*memory)[paths[2]] = encodePng(sheets.splashes);
            const ExportIo mod = memoryIo(source, base);
            ExportIo io;
            io.readBytes = [memory, mod](const std::string& path) -> std::optional<std::vector<unsigned char>> {
                const auto found = memory->find(path);
                if (found != memory->end()) return found->second;
                return mod.readBytes(path);
            };
            io.readText = mod.readText;
            out = styleSheetAtlas(style, io, name);
        }
    } else {
        TypeLookFrames look;
        for (int d = 0; d < 4; ++d) {
            for (const Image& image : pieces[static_cast<size_t>(DrawnPiece::Note)])
                look.note[static_cast<size_t>(d)].push_back(e.rotate ? rotateForDirection(image, d) : image);
            look.holdPiece[static_cast<size_t>(d)] = pieces[static_cast<size_t>(DrawnPiece::HoldPiece)];
            look.holdEnd[static_cast<size_t>(d)] = pieces[static_cast<size_t>(DrawnPiece::HoldEnd)];
        }
        out = buildTypeLookSheet(look, name + ".png").atlas;
    }
    e.finalSheet = out;
    e.finalFrames.clear();
    e.finalW = e.finalH = 0;
    Image image;
    if (out.ok && decodePng(out.png, image)) {
        e.finalW = image.w;
        e.finalH = image.h;
        if (app.rendererReady) uploadLiveImage(app, "notelab-live/sprite/final.png", image);
        DiagnosticSink sink;
        const Result<SparrowAtlas> atlas = parseSparrowAtlas(out.xml, name + ".xml", sink);
        if (atlas) e.finalFrames = atlas.value().frames;
    }
    e.finalFor = e.generation * 4 + e.finalMode;
}

// La vista del PNG final, en el sitio de la hoja: rueda para acercar, boton
// central para mover y el nombre de cada fotograma al pasar por encima.
void drawSpriteFinal(NoteLabApp& app, ImVec2 areaMin, ImVec2 areaSize, bool hovered, bool active) {
    auto& e = app.spriteEditor;
    if (e.finalFor != e.generation * 4 + e.finalMode) spriteBuildFinal(app);
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 areaMax(areaMin.x + areaSize.x, areaMin.y + areaSize.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(areaMin, areaMax, true);
    draw->AddRectFilled(areaMin, areaMax, IM_COL32(12, 14, 18, 255));
    const GlRenderer::PreviewImage image = app.renderer.previewImage("notelab-live/sprite/final.png");
    if (!e.finalSheet.ok || e.finalW <= 0 || !image.ok) {
        const char* text = tr(app, "Nothing to show yet: draw something in the general sheet.", "Nada que enseñar todavía: dibuja algo en la hoja general.");
        draw->AddText(ImVec2(areaMin.x + 16.0f, areaMin.y + 16.0f), ui::color::Muted, text);
        draw->PopClipRect();
        return;
    }
    const float fit = std::min(2.0f, std::min((areaSize.x - 24.0f) / e.finalW, (areaSize.y - 44.0f) / e.finalH));
    float scale = e.finalZoom > 0.0f ? e.finalZoom : fit;
    ImVec2 origin = e.finalZoom > 0.0f ? ImVec2(areaMin.x + e.finalPan.x, areaMin.y + e.finalPan.y)
                                       : ImVec2(areaMin.x + (areaSize.x - e.finalW * scale) * 0.5f, areaMin.y + (areaSize.y - 24.0f - e.finalH * scale) * 0.5f);
    if (hovered && io.MouseWheel != 0.0f) {
        const float next = std::clamp(scale * std::pow(1.15f, io.MouseWheel), 0.05f, 16.0f);
        const ImVec2 p((io.MousePos.x - origin.x) / scale, (io.MousePos.y - origin.y) / scale);
        e.finalZoom = next;
        e.finalPan = ImVec2(io.MousePos.x - p.x * next - areaMin.x, io.MousePos.y - p.y * next - areaMin.y);
        scale = next;
        origin = ImVec2(areaMin.x + e.finalPan.x, areaMin.y + e.finalPan.y);
    }
    if (active && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        if (e.finalZoom <= 0.0f) {
            e.finalZoom = scale;
            e.finalPan = ImVec2(origin.x - areaMin.x, origin.y - areaMin.y);
        }
        e.finalPan.x += io.MouseDelta.x;
        e.finalPan.y += io.MouseDelta.y;
        origin = ImVec2(areaMin.x + e.finalPan.x, areaMin.y + e.finalPan.y);
    }
    const ImVec2 imageMax(origin.x + e.finalW * scale, origin.y + e.finalH * scale);
    drawChecker(draw, origin, imageMax);
    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(image.texture)), origin, imageMax);
    draw->AddRect(origin, imageMax, ui::color::Border);
    const AtlasFrame* over = nullptr;
    for (const AtlasFrame& frame : e.finalFrames) {
        const ImVec2 a(origin.x + frame.x * scale, origin.y + frame.y * scale);
        const ImVec2 b(origin.x + (frame.x + frame.w) * scale, origin.y + (frame.y + frame.h) * scale);
        draw->AddRect(a, b, IM_COL32(170, 180, 210, 70));
        if (hovered && io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y) over = &frame;
    }
    if (over) {
        const ImVec2 a(origin.x + over->x * scale, origin.y + over->y * scale);
        const ImVec2 b(origin.x + (over->x + over->w) * scale, origin.y + (over->y + over->h) * scale);
        draw->AddRect(a, b, IM_COL32(235, 238, 245, 240), 0.0f, 0, 2.0f);
        ImGui::SetTooltip("%s  ·  %d x %d", over->name.c_str(), over->w, over->h);
    }
    char info[160];
    std::snprintf(info, sizeof(info), app.spanish ? "PNG final · %d x %d px · %zu fotogramas · %.0f %%" : "Final PNG · %d x %d px · %zu frames · %.0f %%",
                  e.finalW, e.finalH, e.finalFrames.size(), scale * 100.0f);
    draw->AddText(ImVec2(areaMin.x + 10.0f, areaMax.y - ImGui::GetFontSize() - 8.0f), ui::color::Muted, info);
    draw->PopClipRect();
}

// Guardar el PNG final (y su XML) donde se elija; nunca dentro de un mod abierto.
void saveFinalSheetTo(NoteLabApp& app, const fs::path& chosen) {
    const auto& e = app.spriteEditor;
    if (!e.finalSheet.ok) return;
    fs::path png = chosen;
    if (lowerText(png.extension().u8string()) != ".png") png += ".png";
    fs::path xml = png;
    xml.replace_extension(".xml");
    if (insideOpenMod(app, png)) {
        setStatus(app, "That folder is inside an open mod: Note Lab never writes there. Pick another one.",
                       "Esa carpeta está dentro de un mod abierto: Note Lab nunca escribe ahí. Elige otra.");
        return;
    }
    // El XML nombra su PNG: con el nombre elegido.
    std::string text = e.finalSheet.xml;
    const size_t at = text.find("imagePath=\"");
    if (at != std::string::npos) {
        const size_t end = text.find('"', at + 11);
        if (end != std::string::npos) text.replace(at + 11, end - at - 11, png.filename().u8string());
    }
    std::ofstream image(png, std::ios::binary), atlas(xml, std::ios::binary);
    image.write(reinterpret_cast<const char*>(e.finalSheet.png.data()), static_cast<std::streamsize>(e.finalSheet.png.size()));
    atlas.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!image || !atlas) {
        setStatus(app, "The sheet could not be written there.", "No se pudo escribir la hoja ahí.");
        return;
    }
    setStatus(app, "Final PNG saved: " + png.filename().u8string() + " + " + xml.filename().u8string() + ".",
              "PNG final guardado: " + png.filename().u8string() + " + " + xml.filename().u8string() + ".");
}

// --------------------------------------------------------- la ventana --

// `host` 0: fuera de cualquier ventana (el aspecto de un tipo); 1: dentro de
// Crear HUD, para que se abra encima sin cerrarla.
// ------------------------------------------------------- como empezar --

// Lo elegido en «¿Cómo quieres empezar?»: seguir, la plantilla con huecos, un
// lienzo libre o el lienzo de una pieza. Lo de antes nunca se pierde: la
// plantilla nueva se puede deshacer y los lienzos van en su pestana.
void spriteBegin(NoteLabApp& app) {
    auto& e = app.spriteEditor;
    const bool hud = e.target == 1;
    const SheetLayout general = sheetLayout(hud);
    if (e.doc != 0) spriteSwitchDoc(e, 0);
    if (e.startKind == 1) {
        if (e.hasWork) spritePushUndo(e);
        e.layers = spriteStartSheet(app, general, e.startTemplate <= 2 ? e.startTemplate : -1);
        if (e.startTemplate == 4) {
            SpriteLayer layer;
            if (spriteStyleLayer(app, spriteStyleFor(app), general, layer) > 0) e.layers[0] = std::move(layer);
        }
        e.layer = static_cast<int>(e.layers.size()) - 1;
        ++e.generation;
        spriteSelectCell(e, std::max(0, general.rowOf(e.startPiece)), 0);
        e.selection = {};
        e.focusPending = true;
    } else if (e.startKind == 2 || e.startKind == 3) {
        // La hoja general, vacia si no habia nada: ahi acaba lo que se dibuje.
        if (!e.hasWork) {
            e.layers = spriteStartSheet(app, general, -1);
            e.layer = static_cast<int>(e.layers.size()) - 1;
            ++e.generation;
        }
        if (e.startKind == 2) spriteNewFree(app, e.freeW, e.freeH, e.freePixel, e.startPiece);
        else spriteNewSingle(app, e.startPiece, false);
    }
    e.choosing = false;
    tutorialSignal("sp-start");
}

// Un dibujo pequeno para cada forma de empezar.
void drawStartGlyph(ImDrawList* draw, int kind, ImVec2 c, float s, ImU32 col) {
    const ImU32 faint = ui::withAlpha(col, 90);
    switch (kind) {
        case 0:   // seguir: capas apiladas
            for (int i = 2; i >= 0; --i) {
                const float o = static_cast<float>(i) * s * 0.16f;
                draw->AddRectFilled(ImVec2(c.x - s * 0.42f + o, c.y - s * 0.3f - o), ImVec2(c.x + s * 0.3f + o, c.y + s * 0.3f - o),
                                    i == 0 ? col : faint, 5.0f);
            }
            break;
        case 1:   // plantilla: filas de huecos, la flecha en el primero
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 4; ++k) {
                    const ImVec2 a(c.x - s * 0.62f + k * s * 0.32f, c.y - s * 0.42f + r * s * 0.3f);
                    const ImVec2 b(a.x + s * 0.26f, a.y + s * 0.24f);
                    if (r == 0 && k == 0) draw->AddRectFilled(a, b, col, 3.0f);
                    else draw->AddRect(a, b, faint, 3.0f);
                }
            break;
        case 2: {  // lienzo libre: un cuadro con sus medidas
            const ImVec2 a(c.x - s * 0.36f, c.y - s * 0.36f), b(c.x + s * 0.36f, c.y + s * 0.36f);
            draw->AddRect(a, b, col, 3.0f, 0, 2.0f);
            const float step = s * 0.72f / 6.0f;
            for (int i = 1; i < 6; ++i) {
                draw->AddLine(ImVec2(a.x + i * step, a.y), ImVec2(a.x + i * step, b.y), ui::withAlpha(col, 40));
                draw->AddLine(ImVec2(a.x, a.y + i * step), ImVec2(b.x, a.y + i * step), ui::withAlpha(col, 40));
            }
            draw->AddRectFilled(ImVec2(a.x + step * 2, a.y + step * 2), ImVec2(a.x + step * 4, a.y + step * 4), col);
            break;
        }
        default:  // lienzo de una pieza: tres tamanos
            draw->AddRectFilled(ImVec2(c.x - s * 0.62f, c.y - s * 0.3f), ImVec2(c.x - s * 0.02f, c.y + s * 0.3f), col, 4.0f);
            draw->AddRectFilled(ImVec2(c.x + s * 0.08f, c.y - s * 0.05f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), faint, 3.0f);
            draw->AddRectFilled(ImVec2(c.x + s * 0.38f, c.y + s * 0.06f), ImVec2(c.x + s * 0.6f, c.y + s * 0.3f), faint, 3.0f);
            break;
    }
}

// Un boton grande de una opcion (con su dibujo, su nombre y una linea).
bool startCard(const char* id, int kind, bool selected, const char* title, const char* text, ImVec2 size) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y),
                        selected ? IM_COL32(155, 123, 245, 46) : hovered ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 8), 10.0f);
    draw->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), selected ? ui::color::Accent : ui::color::Border, 10.0f, 0, selected ? 2.0f : 1.0f);
    drawStartGlyph(draw, kind, ImVec2(at.x + size.x * 0.5f, at.y + 46.0f), 58.0f, selected ? ui::color::Accent : ui::color::Muted);
    ImGui::PushFont(ui::fonts().semibold, 16.0f);
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    draw->AddText(ImVec2(at.x + (size.x - titleSize.x) * 0.5f, at.y + 92.0f), ui::color::Text, title);
    ImGui::PopFont();
    ImGui::PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.92f, ImVec2(at.x + 12.0f, at.y + 118.0f), ui::color::Muted, text, nullptr, size.x - 24.0f);
    ImGui::PopClipRect();
    return pressed;
}

// «¿Como quieres empezar?»: antes de pintar, la plantilla con un hueco para
// cada pieza, un lienzo libre de la medida que se quiera (luego se pasa a la
// hoja general) o el lienzo de una de las piezas (pedido del autor, 5 oct 2026).
void drawSpriteStart(NoteLabApp& app) {
    auto& e = app.spriteEditor;
    const bool hud = e.target == 1;
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::PushFont(ui::fonts().semibold, 20.0f);
    ImGui::TextUnformatted(tr(app, "How do you want to start?", "¿Cómo quieres empezar?"));
    ImGui::PopFont();
    ui::caption(hud ? tr(app, "Whatever you choose ends up in the general sheet: one row per piece of the HUD, each box a frame.",
                              "Elijas lo que elijas, acaba en la hoja general: una fila por pieza del HUD y cada hueco un fotograma.")
                    : tr(app, "Whatever you choose ends up in the note's sheet: the note, its hold piece and its end, each box a frame.",
                              "Elijas lo que elijas, acaba en la hoja de la nota: la nota, el tramo y el final de su sostenido, cada hueco un fotograma."));
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const int first = e.hasWork ? 0 : 1;
    const int cards = 4 - first;
    const float gap = 12.0f;
    const float cardW = std::min(270.0f, (ImGui::GetContentRegionAvail().x - gap * (cards - 1)) / cards);
    const ImVec2 size(cardW, 178.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - cardW * cards - gap * (cards - 1)) * 0.5f));
    struct Option { const char* en; const char* es; const char* textEn; const char* textEs; };
    const Option options[4] = {
        {"Continue", "Seguir", "What you had: its layers, frames and tabs.", "Lo que tenías: sus capas, fotogramas y pestañas."},
        {"Template with boxes", "Plantilla con huecos", "A box for each piece it needs, in rows; draw right in the sheet.",
         "Un hueco para cada pieza que necesita, en filas; dibujas directamente en la hoja."},
        {"Free canvas", "Lienzo libre", "The size you want (32×32, pixel art…). When done, «To the general sheet».",
         "Del tamaño que quieras (32×32, pixel art…). Al terminar, «Pasar a la hoja general»."},
        {"Canvas of a piece", "Lienzo de una pieza", "The exact size of one piece in the game, with its shape to start.",
         "Del tamaño exacto de una pieza en el juego, con su forma para empezar."},
    };
    for (int k = first; k < 4; ++k) {
        ImGui::PushID(k);
        if (startCard("startcard", k, e.startKind == k, app.spanish ? options[k].es : options[k].en, app.spanish ? options[k].textEs : options[k].textEn, size))
            e.startKind = k;
        tutorialMark(("sprite-start-" + std::to_string(k)).c_str());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            e.startKind = k;
            spriteBegin(app);
        }
        ImGui::PopID();
        if (k < 3) ImGui::SameLine(0.0f, gap);
    }
    if (!e.choosing) return;
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    // Lo de la opcion elegida.
    const SheetLayout general = sheetLayout(hud);
    auto pieceChips = [&](bool sizes) {
        for (size_t r = 0; r < general.rows.size(); ++r) {
            const DrawnPiece piece = general.rows[r].piece;
            ImGui::PushID(static_cast<int>(r));
            std::string label = drawnPieceName(app, piece);
            if (sizes) label += "  " + std::to_string(drawnPieceSize(piece)) + "×" + std::to_string(drawnPieceSize(piece));
            const bool chosen = e.startPiece == piece;
            ImGui::PushStyleColor(ImGuiCol_Button, chosen ? IM_COL32(155, 123, 245, 90) : ImGui::GetColorU32(ImGuiCol_Button));
            if (ImGui::Button(label.c_str())) e.startPiece = piece;
            ImGui::PopStyleColor();
            ImGui::PopID();
            if (r + 1 < general.rows.size()) ImGui::SameLine(0.0f, 4.0f);
        }
    };
    if (e.startKind == 0) {
        ui::caption(tr(app, "You go on where you left it.", "Sigues donde lo dejaste."));
    } else if (e.startKind == 1) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "Start with", "Empezar con"));
        ImGui::SameLine(0.0f, 12.0f);
        const char* templates[5] = {tr(app, "Arrow", "Flecha"), tr(app, "Mine", "Mina"), tr(app, "Heart", "Corazón"),
                                    tr(app, "Empty", "Vacía"), tr(app, "The style's", "La del estilo")};
        ui::segmented("starttemplate", &e.startTemplate, templates, 5);
        if (e.hasWork) ui::caption(tr(app, "It replaces what you had (Ctrl+Z brings it back).", "Sustituye lo que tenías (Ctrl+Z lo devuelve)."));
    } else if (e.startKind == 2) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "Size", "Tamaño"));
        ImGui::SameLine(0.0f, 12.0f);
        static const int sizes[7] = {16, 32, 48, 64, 128, 160, 256};
        for (int i = 0; i < 7; ++i) {
            ImGui::PushID(i);
            const bool chosen = e.freeW == sizes[i] && e.freeH == sizes[i];
            ImGui::PushStyleColor(ImGuiCol_Button, chosen ? IM_COL32(155, 123, 245, 90) : ImGui::GetColorU32(ImGuiCol_Button));
            if (ImGui::Button((std::to_string(sizes[i]) + "×" + std::to_string(sizes[i])).c_str())) {
                e.freeW = e.freeH = sizes[i];
                e.freePixel = sizes[i] <= 64;
            }
            ImGui::PopStyleColor();
            tutorialMark(("sprite-start-size-" + std::to_string(sizes[i])).c_str());
            ImGui::PopID();
            ImGui::SameLine(0.0f, 4.0f);
        }
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::InputInt("##freew", &e.freeW, 0, 0)) e.freeW = std::clamp(e.freeW, kFreeCanvasMin, kFreeCanvasMax);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextUnformatted("×");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::InputInt("##freeh", &e.freeH, 0, 0)) e.freeH = std::clamp(e.freeH, kFreeCanvasMin, kFreeCanvasMax);
        ImGui::SameLine(0.0f, 4.0f);
        ui::caption("px");
        ImGui::Checkbox(tr(app, "Pixel art (crisp pixels, nothing smoothed)", "Pixel art (píxeles nítidos, nada suavizado)"), &e.freePixel);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "It will be", "Será"));
        ImGui::SameLine(0.0f, 12.0f);
        pieceChips(false);
        ui::caption(tr(app, "You can change it when you pass it: the arrow next to «To the general sheet» lets you pick the piece and the box.",
                            "Lo puedes cambiar al pasarlo: la flecha junto a «Pasar a la hoja general» deja elegir la pieza y el hueco."));
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app, "Piece", "Pieza"));
        ImGui::SameLine(0.0f, 12.0f);
        pieceChips(true);
    }
    // Abajo: cancelar o empezar.
    const float bottom = ImGui::GetWindowContentRegionMax().y - ImGui::GetFrameHeight() - 4.0f;
    if (ImGui::GetCursorPosY() < bottom) ImGui::SetCursorPosY(bottom);
    ImGui::Separator();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 300.0f);
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"), ImVec2(110.0f, 0.0f)) || escapeClosesWindow()) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    const bool go = ui::primaryButton(ui::label(ui::icon::Brush, tr(app, "Start drawing", "Empezar a dibujar")), ImVec2(180.0f, 0.0f));
    tutorialMark("sprite-start-go");
    if (go || (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput)) spriteBegin(app);
}

void drawSpriteEditor(NoteLabApp& app, int host) {
    auto& e = app.spriteEditor;
    if (e.target != host) return;
    const bool hud = e.target == 1;
    if (e.requestOpen) {
        ImGui::OpenPopup("###spriteeditor");
        e.requestOpen = false;
    }
    const ImVec2 screen = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(1440.0f, screen.x - 40.0f), std::min(880.0f, screen.y - 30.0f)), ImGuiCond_Appearing);
    bool open = true;
    const std::string title = hud ? std::string(tr(app, "Draw the HUD's pieces", "Dibujar las piezas del HUD"))
                                  : std::string(tr(app, "Draw the look of ", "Dibujar el aspecto de ")) + e.type;
    const bool visible = ImGui::BeginPopupModal((ui::label(ui::icon::Brush, title) + "###spriteeditor").c_str(), &open);
    if (!visible) {
        if (e.painted >= 0 || e.shown >= 0) releaseSpritePreview(app);
        return;
    }
    if (e.source < 0 || e.source >= static_cast<int>(app.sources.size()) || e.layers.empty()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const bool es = app.spanish;
    ImGuiIO& io = ImGui::GetIO();
    tutorialZone(app, kTrackSprite);
    if (e.choosing) {
        drawSpriteStart(app);
        ImGui::EndPopup();
        return;
    }

    // Las pestanas: la hoja general y lo abierto aparte (dibujos sueltos y
    // otras hojas para copiar de ellas).
    {
        int switchTo = -1, closeIndex = -1;
        for (int i = 0; i < static_cast<int>(e.docs.size()); ++i) {
            const bool active = i == e.doc;
            const bool single = active ? e.docSingle : e.docs[static_cast<size_t>(i)].single;
            const bool reference = active ? e.docReference : e.docs[static_cast<size_t>(i)].reference;
            const std::string& name = active ? e.docName : e.docs[static_cast<size_t>(i)].name;
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? IM_COL32(155, 123, 245, 80) : IM_COL32(255, 255, 255, 10));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? IM_COL32(155, 123, 245, 110) : IM_COL32(255, 255, 255, 26));
            const char* glyph = i == 0 ? ui::icon::Grid : single ? ui::icon::Brush : ui::icon::Document;
            if (ImGui::Button(ui::label(glyph, name).c_str())) switchTo = i;
            ImGui::PopStyleColor(2);
            ui::tooltip(i == 0 ? (e.target == 1 ? tr(app, "The general sheet: every piece, each row an animation. This is what gets used.",
                                                     "La hoja general: todas las piezas, cada fila una animación. Es la que se usa.")
                                                : tr(app, "This note's look: the note and its hold, each row an animation. This is what gets used.",
                                                     "El aspecto de esta nota: la nota y su sostenido, cada fila una animación. Es lo que se usa."))
                      : single ? tr(app, "A loose drawing: «To the general sheet» puts it in its row.", "Un dibujo suelto: «Pasar a la hoja general» lo pone en su fila.")
                      : reference ? tr(app, "Another sheet, open to copy from it. It is not used.", "Otra hoja, abierta para copiar de ella. No se usa.")
                                  : name.c_str());
            if (i > 0) {
                ImGui::SameLine(0.0f, 1.0f);
                if (ImGui::Button((std::string(ui::icon::Close) + "##close").c_str())) closeIndex = i;
                ui::tooltip(tr(app, "Close this tab", "Cerrar esta pestaña"));
            }
            ImGui::PopID();
            ImGui::SameLine(0.0f, 6.0f);
        }
        if (ImGui::Button(ui::label(ui::icon::Add, tr(app, "Open…", "Abrir…")).c_str())) {
            ImGui::OpenPopup("spritenewtab");
            e.listShown = false;
        }
        tutorialMark("sprite-newtab");
        ui::tooltip(tr(app, "A loose drawing of a piece, the chosen box apart, or another sheet to copy from",
                            "Un dibujo suelto de una pieza, el hueco elegido aparte u otra hoja para copiar de ella"));
        if (ImGui::BeginPopup("spritenewtab")) {
            if (ImGui::MenuItem(ui::label(ui::icon::Grid, tr(app, "Free canvas (you choose the size)…", "Lienzo libre (tú eliges el tamaño)…")).c_str())) {
                e.choosing = true;
                e.hasWork = true;
                e.startKind = 2;
            }
            ImGui::Separator();
            ImGui::TextDisabled("%s", tr(app, "Loose drawing", "Dibujo suelto"));
            const SheetLayout general = sheetLayout(e.target == 1);
            for (const SheetRow& row : general.rows)
                if (ImGui::MenuItem(ui::label(ui::icon::Brush, drawnPieceName(app, row.piece)).c_str())) spriteNewSingle(app, row.piece, false);
            if (ImGui::MenuItem(ui::label(ui::icon::Edit, tr(app, "The chosen box, apart", "El hueco elegido, aparte")).c_str(), nullptr, false, !e.docSingle))
                spriteNewSingle(app, DrawnPiece::Note, true);
            ImGui::Separator();
            ImGui::TextDisabled("%s", e.target == 1 ? tr(app, "Other sheets, to copy from them", "Otras hojas, para copiar de ellas")
                                                    : tr(app, "Other notes, to copy from them", "Otras notas, para copiar de ellas"));
            // La lista de hojas: las dibujadas en esta sesion y los estilos del
            // mod, con su vista previa (lo que tiene el primer hueco de la nota).
            struct Entry { std::string name; std::vector<SpriteLayer> layers; bool hud = false; int style = -1; };
            static std::vector<Entry> entries;
            if (!e.listShown) {
                entries.clear();
                const std::string self = e.target == 1 ? app.createHud.layersKey : spriteKey(e.source, e.type);
                for (const auto& [key, layers] : e.drawn) {
                    if (key == self || layers.empty()) continue;
                    // El aspecto de una nota no mira hojas de HUD: solo otras notas.
                    if (e.target == 0 && key.rfind("hud|", 0) == 0) continue;
                    Entry entry;
                    entry.hud = key.rfind("hud|", 0) == 0;
                    entry.name = entry.hud ? std::string(tr(app, "HUD sheet · ", "Hoja de HUD · ")) + key.substr(key.rfind('|') + 1)
                                           : std::string(tr(app, "Note type · ", "Tipo de nota · ")) + key.substr(key.find('|') + 1);
                    entry.layers = layers;
                    entries.push_back(std::move(entry));
                }
                const Source& source = *app.sources[static_cast<size_t>(e.source)];
                for (int i : paintableStyles(source)) {
                    if (entries.size() >= 12) break;
                    Entry entry;
                    entry.name = std::string(e.target == 1 ? tr(app, "Style · ", "Estilo · ") : tr(app, "Notes of ", "Notas de ")) +
                                 source.catalog.styles[static_cast<size_t>(i)].name;
                    entry.style = i;
                    entry.hud = e.target == 1;
                    entries.push_back(std::move(entry));
                }
                for (size_t i = 0; i < entries.size() && i < 12; ++i) {
                    Image preview;
                    if (!entries[i].layers.empty()) {
                        const SheetLayout layout = sheetLayout(entries[i].hud);
                        const SpriteRect cell = layout.cell(layout.rowOf(DrawnPiece::Note), 0);
                        preview = cropRect(composeSpriteLayers(entries[i].layers), cell.x0, cell.y0, cell.w(), cell.h());
                    } else {
                        const Source& from = *app.sources[static_cast<size_t>(e.source)];
                        const NoteStyle& style = from.catalog.styles[static_cast<size_t>(entries[i].style)];
                        const std::vector<Image> frames = stylePieceFrames(style, Part::Note, 0, memoryIo(from, style));
                        if (!frames.empty()) preview = frames.front();
                    }
                    if (!preview.empty() && app.rendererReady) uploadLiveImage(app, "notelab-live/sprite/list-" + std::to_string(i) + ".png", preview);
                }
                e.listShown = true;
            }
            if (entries.empty()) ImGui::TextDisabled("%s", tr(app, "(nothing else yet)", "(nada más todavía)"));
            for (size_t i = 0; i < entries.size() && i < 12; ++i) {
                ImGui::PushID(static_cast<int>(i));
                const GlRenderer::PreviewImage thumb = app.renderer.previewImage("notelab-live/sprite/list-" + std::to_string(i) + ".png");
                const ImVec2 at = ImGui::GetCursorScreenPos();
                drawChecker(ImGui::GetWindowDrawList(), at, ImVec2(at.x + 36.0f, at.y + 36.0f));
                if (thumb.ok) ImGui::Image(ImTextureRef(static_cast<ImTextureID>(thumb.texture)), ImVec2(36.0f, 36.0f));
                else ImGui::Dummy(ImVec2(36.0f, 36.0f));
                ImGui::SameLine();
                if (ImGui::Selectable(entries[i].name.c_str(), false, 0, ImVec2(260.0f, 36.0f))) {
                    std::vector<SpriteLayer> layers = entries[i].layers;
                    if (layers.empty()) {
                        SpriteLayer layer;
                        spriteStyleLayer(app, entries[i].style, sheetLayout(entries[i].hud), layer);
                        layers.push_back(std::move(layer));
                    }
                    spriteOpenReference(app, entries[i].name, std::move(layers), entries[i].hud);
                    tutorialSignal("sp-tabs");
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        if (e.docSingle) {
            ImGui::SameLine(0.0f, 16.0f);
            const bool insert = ui::primaryButton(ui::label(ui::icon::Check, tr(app, "To the general sheet", "Pasar a la hoja general")));
            tutorialMark("sprite-insert");
            ui::tooltip(e.fromRow >= 0 ? tr(app, "Back to the box it came from (it replaces it), in a new layer", "Vuelve al hueco de donde salió (lo sustituye), en una capa nueva")
                                       : (std::string(tr(app, "As ", "Como ")) + drawnPieceName(app, e.docPiece) +
                                          tr(app, ", in the next free box of its row, in a new layer. Another size is fitted to the box.",
                                                  ", en el siguiente hueco libre de su fila, en una capa nueva. Si es de otro tamaño, se ajusta al hueco."))
                                             .c_str());
            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::Button((std::string(ui::icon::Down) + "##sendto").c_str())) ImGui::OpenPopup("spritesendto");
            tutorialMark("sprite-sendto");
            ui::tooltip(tr(app, "Choose the piece and the box", "Elegir la pieza y el hueco"));
            DrawnPiece sendPiece = e.docPiece;
            int sendSlot = -3;
            if (ImGui::BeginPopup("spritesendto")) {
                const SheetLayout general = sheetLayout(e.target == 1);
                ImGui::TextDisabled("%s", tr(app, "As this piece, in the next free box", "Como esta pieza, en el siguiente hueco libre"));
                for (const SheetRow& row : general.rows)
                    if (ImGui::MenuItem(ui::label(ui::icon::Add, drawnPieceName(app, row.piece)).c_str(), nullptr, row.piece == e.docPiece)) {
                        sendPiece = row.piece;
                        sendSlot = -1;
                    }
                ImGui::Separator();
                const SpriteDoc& sheet = e.docs.front();
                if (sheet.row >= 0 && sheet.row < static_cast<int>(general.rows.size())) {
                    const DrawnPiece chosenPiece = general.rows[static_cast<size_t>(sheet.row)].piece;
                    const std::string label = std::string(tr(app, "In the chosen box: ", "En el hueco elegido: ")) + drawnPieceName(app, chosenPiece) + " " +
                                              std::to_string(sheet.slot + 1) + tr(app, " (replaces it)", " (lo sustituye)");
                    if (ImGui::MenuItem(ui::label(ui::icon::Edit, label).c_str())) {
                        sendPiece = chosenPiece;
                        sendSlot = sheet.slot;
                    }
                }
                if (e.fromRow >= 0 && ImGui::MenuItem(ui::label(ui::icon::Undo, tr(app, "Back to the box it came from", "Devolver al hueco de donde salió")).c_str())) {
                    sendPiece = e.docPiece;
                    sendSlot = -2;
                }
                ImGui::EndPopup();
            }
            if (insert) spriteInsertSingle(app);
            else if (sendSlot != -3) spriteSendToSheet(app, sendPiece, sendSlot);
        }
        if (switchTo >= 0) spriteSwitchDoc(e, switchTo);
        if (closeIndex > 0) spriteCloseDoc(e, closeIndex);
    }
    if (e.layers.empty()) {
        ImGui::EndPopup();
        return;
    }
    const SheetLayout layout = spriteLayout(e);
    e.layer = std::clamp(e.layer, 0, static_cast<int>(e.layers.size()) - 1);
    e.row = std::clamp(e.row, 0, static_cast<int>(layout.rows.size()) - 1);
    e.slot = std::clamp(e.slot, 0, layout.slots - 1);

    // Lo que se ve: las capas juntas, y que huecos tienen algo. Si solo cambio
    // un rango (una pincelada), solo se recompone ese rango.
    if (e.composed != e.generation || e.composite.w != layout.w || e.composite.h != layout.h) {
        const bool region = e.dirtyFor == e.generation && !e.dirty.empty() && e.composite.w == layout.w && e.composite.h == layout.h &&
                            e.used.size() == layout.rows.size() * kSheetSlots;
        if (region) composeSpriteRegion(e.composite, e.layers, e.dirty);
        else e.composite = composeSpriteLayers(e.layers);
        if (!region) e.used.assign(layout.rows.size() * kSheetSlots, 0);
        for (int r = 0; r < static_cast<int>(layout.rows.size()); ++r)
            for (int s = 0; s < layout.slots; ++s) {
                const SpriteRect cell = layout.cell(r, s);
                const bool touched = !region || !(cell.x1 <= e.dirty.x0 || cell.x0 >= e.dirty.x1 || cell.y1 <= e.dirty.y0 || cell.y0 >= e.dirty.y1);
                if (touched) e.used[static_cast<size_t>(r * kSheetSlots + s)] = spriteHasInk(e.composite, cell) ? 1 : 0;
            }
        e.composed = e.generation;
        e.dirty = {};
        e.dirtyFor = -1;
    }
    if (app.rendererReady && e.shown != e.generation) {
        uploadLiveImage(app, "notelab-live/sprite/canvas.png", e.composite);
        e.shown = e.generation;
        e.changedAt = ImGui::GetTime();
    }
    const GlRenderer::PreviewImage sheetImage = app.renderer.previewImage("notelab-live/sprite/canvas.png");
    auto uvOf = [&](const SpriteRect& r, ImVec2& uv0, ImVec2& uv1) {
        uv0 = ImVec2(static_cast<float>(r.x0) / layout.w, static_cast<float>(r.y0) / layout.h);
        uv1 = ImVec2(static_cast<float>(r.x1) / layout.w, static_cast<float>(r.y1) / layout.h);
    };

    // Arriba: deshacer, la vista, la seleccion y cargar.
    if (spriteIconButton("undo", ui::icon::Undo, tr(app, "Undo (Ctrl+Z)", "Deshacer (Ctrl+Z)"), !e.undo.empty())) spriteUndo(e, false);
    ImGui::SameLine(0.0f, 4.0f);
    if (spriteIconButton("redo", ui::icon::Redo, tr(app, "Redo (Ctrl+Y)", "Rehacer (Ctrl+Y)"), !e.redo.empty())) spriteUndo(e, true);
    ImGui::SameLine(0.0f, 16.0f);
    bool zoomOut = spriteIconButton("zoomout", ui::icon::ZoomOut, tr(app, "Zoom out (mouse wheel)", "Alejar (rueda del ratón)"));
    ImGui::SameLine(0.0f, 4.0f);
    bool zoomIn = spriteIconButton("zoomin", ui::icon::ZoomIn, tr(app, "Zoom in (mouse wheel)", "Acercar (rueda del ratón)"));
    ImGui::SameLine(0.0f, 4.0f);
    if (spriteIconButton("fit", ui::icon::Fit, tr(app, "The whole sheet", "Toda la hoja"))) e.zoom = 0.0f;
    ImGui::SameLine(0.0f, 4.0f);
    if (spriteIconButton("focus", ui::icon::Search, tr(app, "Zoom to the chosen box (double click with Select)", "Acercar al hueco elegido (doble clic con Selección)")))
        e.focusPending = true;
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(86.0f);
    char zoomLabel[24];
    std::snprintf(zoomLabel, sizeof(zoomLabel), "%.0f %%", e.lastScale * 100.0f);
    if (ImGui::BeginCombo("##zoompreset", zoomLabel)) {
        static const float presets[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
        for (float preset : presets) {
            char text[16];
            std::snprintf(text, sizeof(text), "%.0f %%", preset * 100.0f);
            if (ImGui::Selectable(text, std::fabs(preset - e.lastScale) < 0.01f)) e.zoomTo = preset;
        }
        ImGui::EndCombo();
    }
    ui::tooltip(tr(app, "Zoom: 100 % is one pixel of the sheet per screen pixel", "Zoom: 100 % es un píxel de la hoja por píxel de pantalla"));
    ImGui::SameLine(0.0f, 16.0f);
    const bool hasWork = !spriteWorkRect(e).empty();
    if (spriteIconButton("copy", ui::icon::Copy, tr(app, "Copy the selection or the chosen box (Ctrl+C)", "Copiar la selección o el hueco elegido (Ctrl+C)"), hasWork))
        spriteCopy(app, false);
    ImGui::SameLine(0.0f, 4.0f);
    if (spriteIconButton("cut", sprite_icon::Cut, tr(app, "Cut (Ctrl+X)", "Cortar (Ctrl+X)"), hasWork)) spriteCopy(app, true);
    ImGui::SameLine(0.0f, 4.0f);
    if (spriteIconButton("paste", sprite_icon::Paste, tr(app, "Paste in a new layer, in the selection or the chosen box (Ctrl+V)",
                                                        "Pegar en una capa nueva, en la selección o el hueco elegido (Ctrl+V)"), !e.clipboard.empty()))
        spritePasteAt(app, spriteWorkRect(e));
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(e.clipboard.empty());
    const bool pasteFree = ImGui::Button(ui::label(sprite_icon::Paste, tr(app, "In a free box", "En un hueco libre")).c_str());
    ImGui::EndDisabled();
    tutorialMark("sprite-paste-free");
    ui::tooltip(tr(app, "Paste what you copied in the next free box of this row: a new frame", "Pega lo copiado en el siguiente hueco libre de esta fila: un fotograma nuevo"));
    if (pasteFree) spritePasteFree(app);
    ImGui::SameLine(0.0f, 4.0f);
    if (spriteIconButton("clearsel", sprite_icon::Erase, tr(app, "Clear the selection in this layer (Del)", "Vaciar la selección en esta capa (Supr)"), !e.selection.empty())) {
        spritePushStroke(e);
        clearSprite(e.layers[static_cast<size_t>(e.layer)].image, e.selection);
        ++e.generation;
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (spriteIconButton("deselect", ui::icon::Close, tr(app, "Deselect (Ctrl+D)", "Quitar la selección (Ctrl+D)"), !e.selection.empty())) e.selection = {};
    ImGui::SameLine(0.0f, 16.0f);
    // Lo de archivos y empezar de nuevo, en un menu: la barra queda para dibujar.
    if (ImGui::Button((ui::label(ui::icon::FolderOpen, tr(app, "File", "Archivo")) + "  " + ui::icon::Down).c_str())) ImGui::OpenPopup("spritefile");
    tutorialMark("sprite-filemenu");
    tutorialMark("sprite-savefile");
    if (ImGui::BeginPopup("spritefile")) {
        if (menuItem(ui::icon::Restart, tr(app, "Start again… (template, free canvas)", "Empezar de nuevo… (plantilla, lienzo libre)"))) {
            e.choosing = true;
            e.hasWork = true;
            e.startKind = 2;
        }
        if (menuItem(ui::icon::Layers, tr(app, "Load from the style", "Cargar del estilo"), nullptr, false, !e.docSingle && !e.showFinal)) spriteLoadStyle(app);
        if (menuItem(ui::icon::FolderOpen, tr(app, "Open sheet… (.nlsprite, PNG + XML, images, GIF)", "Abrir hoja… (.nlsprite, PNG + XML, imágenes, GIF)"), nullptr, false,
                     !e.docSingle && !e.showFinal))
            openDialog(app, SDL_GL_GetCurrentWindow(), DialogAction::SpriteSheetFile);
        if (menuItem(ui::icon::Document, tr(app, "Save drawing (.nlsprite)…", "Guardar dibujo (.nlsprite)…")))
            openDialog(app, SDL_GL_GetCurrentWindow(), DialogAction::SaveSpriteFile);
        if (!hud && menuItem(ui::icon::Photo, tr(app, "See the final PNG", "Ver el PNG final"), nullptr, e.showFinal)) {
            e.showFinal = !e.showFinal;
            e.finalFor = -1;
        }
        ImGui::EndPopup();
    }
    ui::tooltip(tr(app, "Start again, load the style, open or save a drawing", "Empezar de nuevo, cargar el estilo, abrir o guardar un dibujo"));
    if (hud || e.showFinal) {
        ImGui::SameLine(0.0f, 8.0f);
        // El PNG final: el archivo con todo junto, en el sitio de la hoja.
        ImGui::PushStyleColor(ImGuiCol_Button, e.showFinal ? IM_COL32(155, 123, 245, 90) : ImGui::GetColorU32(ImGuiCol_Button));
        if (ImGui::Button(ui::label(ui::icon::Photo, tr(app, "Final PNG", "PNG final")).c_str())) {
            e.showFinal = !e.showFinal;
            e.finalFor = -1;
        }
        ImGui::PopStyleColor();
        tutorialMark("sprite-final");
        ui::tooltip(tr(app, "See the file with everything together, as it will be saved (hover a frame to see its name)",
                            "Ver el archivo con todo junto, como se guardará (pasa por un fotograma para ver su nombre)"));
        if (e.showFinal) {
            if (e.target == 1) {
                ImGui::SameLine(0.0f, 6.0f);
                const char* modes[] = {tr(app, "Whole HUD", "Todo el HUD"), tr(app, "Only the drawn", "Solo lo dibujado")};
                if (ui::segmented("finalmode", &e.finalMode, modes, 2)) e.finalZoom = 0.0f;
            }
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::BeginDisabled(!e.finalSheet.ok);
            if (ImGui::Button(ui::label(ui::icon::Document, tr(app, "Save PNG + XML…", "Guardar PNG + XML…")).c_str()))
                openDialog(app, SDL_GL_GetCurrentWindow(), DialogAction::SaveFinalSheet);
            ImGui::EndDisabled();
        }
    }

    // Lo de la herramienta elegida.
    ImGui::AlignTextToFramePadding();
    static const char* toolNamesEs[7] = {"Pincel", "Goma", "Figura", "Relleno", "Cuentagotas", "Selección", "Mover"};
    static const char* toolNamesEn[7] = {"Brush", "Eraser", "Shape", "Fill", "Picker", "Select", "Move"};
    ImGui::TextColored(ui::vec(ui::color::Accent), "%s", (es ? toolNamesEs : toolNamesEn)[std::clamp(e.tool, 0, 6)]);
    ImGui::SameLine(0.0f, 12.0f);
    if (e.tool <= 1) {
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("##brushsize", &e.brushSize, 1.0f, 80.0f, tr(app, "Size %.0f px", "Tamaño %.0f px"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::SliderFloat("##hardness", &e.hardness, 0.0f, 1.0f, tr(app, "Hardness %.2f", "Dureza %.2f"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::SliderFloat("##brushopacity", &e.brushOpacity, 0.05f, 1.0f, tr(app, "Opacity %.2f", "Opacidad %.2f"));
        // Espejo: lo que se pinta sale tambien reflejado en el hueco (las
        // flechas son simetricas: la de la izquierda, de arriba a abajo).
        ImGui::SameLine(0.0f, 12.0f);
        const char* mirrors[3] = {tr(app, "No mirror", "Sin espejo"), tr(app, "Mirror top-bottom", "Espejo arriba-abajo"),
                                  tr(app, "Mirror left-right", "Espejo izq.-der.")};
        ui::segmented("spritemirror", &e.mirror, mirrors, 3);
        tutorialMark("sprite-mirror");
        if (e.docPixel) {
            ImGui::SameLine(0.0f, 10.0f);
            ui::pill(tr(app, "Pixel art", "Pixel art"), ui::color::Accent);
        }
    } else if (e.tool == 2) {
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##shapekind", spriteShapeName(app, e.shapeKind))) {
            for (int k = 0; k < kSpriteShapeKinds; ++k)
                if (ImGui::Selectable(spriteShapeName(app, static_cast<SpriteShapeKind>(k)), e.shapeKind == static_cast<SpriteShapeKind>(k)))
                    e.shapeKind = static_cast<SpriteShapeKind>(k);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Checkbox(tr(app, "Filled", "Rellena"), &e.shapeFill);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("##shapeoutline", &e.shapeOutline, 0.0f, 24.0f, tr(app, "Outline %.0f px", "Contorno %.0f px"));
    } else if (e.tool == 3) {
        ImGui::SetNextItemWidth(170.0f);
        ImGui::SliderInt("##tolerance", &e.tolerance, 0, 128, tr(app, "Tolerance %d", "Tolerancia %d"));
        ImGui::SameLine();
        ui::caption(tr(app, "Fills inside the box (or the selection).", "Rellena dentro del hueco (o de la selección)."));
        ImGui::SameLine();
        ui::caption(tr(app, "Shift: straight line from the last point", "Shift: línea recta desde el último punto"));
    } else if (e.tool == 4) {
        ui::caption(tr(app, "Click the drawing to take its color.", "Haz clic en el dibujo para coger su color."));
    } else if (e.tool == 5) {
        ui::caption(tr(app, "Drag to select a range · click a box to select it whole · double click to zoom to it · Ctrl+A all",
                            "Arrastra para seleccionar un rango · clic en un hueco para cogerlo entero · doble clic para acercarte · Ctrl+A todo"));
    } else {
        ui::caption(tr(app, "Drag to move the selection (or the whole layer) · arrows: 1 px", "Arrastra para mover la selección (o la capa entera) · flechas: 1 px"));
    }
    // Los atajos, siempre a mano.
    ImGui::SameLine(0.0f, 12.0f);
    spriteIconButton("spritehelp", ui::icon::Help, tr(app, "Shortcuts", "Atajos"));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tr(app, "Shortcuts", "Atajos"));
        ImGui::Separator();
        static const char* lines[][2] = {
            {"B E U G I M V  ·  tools", "B E U G I M V  ·  herramientas"},
            {"[ ]  ·  smaller / bigger brush (Shift: hardness)", "[ ]  ·  pincel más pequeño / más grande (Shift: dureza)"},
            {"Alt + click  ·  pick a color with any tool", "Alt + clic  ·  coger un color con cualquier herramienta"},
            {"Right click  ·  paint with the outline color", "Clic derecho  ·  pintar con el color de contorno"},
            {"Shift + click  ·  straight line from the last point", "Shift + clic  ·  línea recta desde el último punto"},
            {"X  ·  swap colors", "X  ·  intercambiar colores"},
            {"Wheel  ·  zoom · Space or middle button + drag  ·  move", "Rueda  ·  zoom · Espacio o botón central + arrastrar  ·  mover"},
            {"0  ·  whole sheet · 1  ·  100 % · F  ·  the chosen box", "0  ·  toda la hoja · 1  ·  100 % · F  ·  el hueco elegido"},
            {"Ctrl+Z / Ctrl+Y  ·  undo / redo", "Ctrl+Z / Ctrl+Y  ·  deshacer / rehacer"},
            {"Ctrl+C / X / V  ·  copy, cut, paste · Ctrl+A / D  ·  select all / none", "Ctrl+C / X / V  ·  copiar, cortar, pegar · Ctrl+A / D  ·  todo / nada"},
            {"Del  ·  clear the selection", "Supr  ·  vaciar la selección"},
        };
        for (const auto& line : lines) ImGui::TextUnformatted(es ? line[1] : line[0]);
        ImGui::EndTooltip();
    }

    const float timelineHeight = 104.0f;
    const float bodyHeight = std::max(300.0f, ImGui::GetContentRegionAvail().y - timelineHeight - 52.0f);

    // Izquierda: las herramientas, como en un programa de dibujo.
    ImGui::BeginChild("spritetools", ImVec2(52.0f, bodyHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    static const char* keys = "BEUGIMV";
    for (int t = 0; t < 7; ++t) {
        const std::string tip = std::string((es ? toolNamesEs : toolNamesEn)[t]) + " (" + keys[t] + ")";
        if (spriteToolButton(t, e.tool == t, tip) && e.tool != t) {
            e.tool = t;
            tutorialSignal("sp-tools");
        }
    }
    // Los dos colores, uno encima de otro como en Krita.
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    spriteColorButton("##primary", e.color);
    ui::tooltip(tr(app, "Main color", "Color principal"));
    spriteColorButton("##secondary", e.second);
    ui::tooltip(tr(app, "Outline color", "Color de contorno"));
    if (spriteIconButton("swap", ui::icon::Sort, tr(app, "Swap colors (X)", "Intercambiar colores (X)"))) std::swap(e.color, e.second);
    ImGui::EndChild();

    // Al lado: la paleta y los presets.
    ImGui::SameLine();
    ImGui::BeginChild("spritepresets", ImVec2(212.0f, bodyHeight), ImGuiChildFlags_Borders);
    ui::sectionHeader(tr(app, "Palette", "Paleta"), ui::icon::Palette);
    // Los colores de las flechas del juego base y unos basicos.
    static const ImU32 palette[] = {IM_COL32(194, 75, 153, 255), IM_COL32(0, 255, 255, 255), IM_COL32(18, 250, 5, 255),
                                    IM_COL32(249, 57, 63, 255),  IM_COL32(255, 255, 255, 255), IM_COL32(28, 12, 36, 255),
                                    IM_COL32(135, 163, 173, 255), IM_COL32(255, 210, 60, 255), IM_COL32(255, 140, 40, 255),
                                    IM_COL32(60, 110, 255, 255), IM_COL32(255, 92, 140, 255), IM_COL32(0, 0, 0, 0)};
    for (int i = 0; i < 12; ++i) {
        ImGui::PushID(i);
        if (ImGui::ColorButton("##swatch", ImGui::ColorConvertU32ToFloat4(palette[i]), ImGuiColorEditFlags_AlphaPreview, ImVec2(28.0f, 28.0f))) e.color = palette[i];
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) e.second = palette[i];
        ui::tooltip(tr(app, "Click: main color · right click: outline", "Clic: principal · clic derecho: contorno"));
        ImGui::PopID();
        if (i % 6 != 5) ImGui::SameLine(0.0f, 3.0f);
    }
    // Mi paleta: los colores que se anaden, guardados en las preferencias.
    ImGui::Spacing();
    ImGui::TextColored(ui::vec(ui::color::Muted), "%s", tr(app, "My colors", "Mis colores"));
    ImGui::SameLine();
    const bool known = std::find(app.spriteColors.begin(), app.spriteColors.end(), static_cast<std::uint32_t>(e.color)) != app.spriteColors.end();
    ImGui::BeginDisabled(known || app.spriteColors.size() >= 48);
    if (ImGui::SmallButton((std::string(ui::icon::Add) + "##addcolor").c_str())) {
        app.spriteColors.push_back(static_cast<std::uint32_t>(e.color));
        if (!app.headless) saveSettings(app);
    }
    ImGui::EndDisabled();
    tutorialMark("sprite-addcolor");
    ui::tooltip(known ? tr(app, "The main color is already in your palette", "El color principal ya está en tu paleta")
                      : tr(app, "Add the main color to your palette", "Añadir el color principal a tu paleta"));
    int removeColor = -1;
    for (size_t i = 0; i < app.spriteColors.size(); ++i) {
        ImGui::PushID(static_cast<int>(100 + i));
        const ImU32 swatch = static_cast<ImU32>(app.spriteColors[i]);
        if (ImGui::ColorButton("##mine", ImGui::ColorConvertU32ToFloat4(swatch), ImGuiColorEditFlags_AlphaPreview, ImVec2(28.0f, 28.0f))) e.color = swatch;
        if (ImGui::BeginPopupContextItem("minecolor")) {
            if (ImGui::MenuItem(tr(app, "Use as outline", "Usar como contorno"))) e.second = swatch;
            if (ImGui::MenuItem(ui::label(ui::icon::Delete, tr(app, "Remove from my palette", "Quitar de mi paleta")).c_str())) removeColor = static_cast<int>(i);
            ImGui::EndPopup();
        }
        ui::tooltip(tr(app, "Click: main color · right click: outline or remove", "Clic: principal · clic derecho: contorno o quitar"));
        ImGui::PopID();
        if (i % 6 != 5 && i + 1 < app.spriteColors.size()) ImGui::SameLine(0.0f, 3.0f);
    }
    if (removeColor >= 0) {
        app.spriteColors.erase(app.spriteColors.begin() + removeColor);
        if (!app.headless) saveSettings(app);
    }
    if (app.spriteColors.empty())
        ui::caption(tr(app, "Pick a color and press + to keep it here.", "Elige un color y pulsa + para guardarlo aquí."));
    ui::sectionHeader(tr(app, "Presets", "Presets"), ui::icon::Star);
    ui::caption(e.selection.empty() ? tr(app, "In a new layer, in the chosen box.", "En una capa nueva, en el hueco elegido.")
                                    : tr(app, "In a new layer, filling the selection.", "En una capa nueva, llenando la selección."));
    int shown = 0;
    for (int p = 0; p < kSpritePresets; ++p) {
        const SpritePreset preset = static_cast<SpritePreset>(p);
        if (!e.docHud && (preset == SpritePreset::Strum || preset == SpritePreset::Splash || preset == SpritePreset::FullHud)) continue;
        if (e.docSingle && preset == SpritePreset::FullHud) continue;
        ImGui::PushID(p);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("preset", ImVec2(60.0f, 60.0f));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, ImVec2(at.x + 60.0f, at.y + 60.0f), hovered ? IM_COL32(255, 255, 255, 26) : IM_COL32(255, 255, 255, 9), 7.0f);
        drawPresetThumb(draw, preset, ImVec2(at.x + 7.0f, at.y + 5.0f), ImVec2(at.x + 53.0f, at.y + 42.0f));
        const char* name = spritePresetName(app, preset);
        const ImVec2 size = ImGui::CalcTextSize(name);
        const float scaleText = std::min(1.0f, 56.0f / std::max(1.0f, size.x));
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f * scaleText, ImVec2(at.x + (60.0f - size.x * 0.78f * scaleText) * 0.5f, at.y + 44.0f),
                      ui::color::Muted, name);
        tutorialMark(("sprite-preset-" + std::to_string(p)).c_str());
        ui::tooltip(preset == SpritePreset::FullHud ? tr(app, "Note, receptor, hold and splash in the first box of each row", "Nota, receptor, sostenido y salpicadura en el primer hueco de cada fila")
                                                    : name);
        if (pressed) applySpritePreset(app, preset);
        ImGui::PopID();
        if (++shown % 3 != 0) ImGui::SameLine(0.0f, 4.0f);
    }
    if (shown % 3 != 0) ImGui::NewLine();
    ImGui::EndChild();

    // En medio: la hoja.
    ImGui::SameLine();
    const float rightWidth = 250.0f;
    ImGui::BeginChild("spritecanvas", ImVec2(ImGui::GetContentRegionAvail().x - rightWidth - 8.0f, bodyHeight), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 areaMin = ImGui::GetCursorScreenPos();
    const ImVec2 areaSize = ImGui::GetContentRegionAvail();
    const ImVec2 areaMax(areaMin.x + areaSize.x, areaMin.y + areaSize.y);
    ImGui::InvisibleButton("spritearea", areaSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    tutorialMark("sprite-canvas");
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float fit = std::max(0.05f, std::min((areaSize.x - 16.0f) / layout.w, (areaSize.y - 16.0f) / layout.h));
    if (e.focusPending) {
        const SpriteRect cell = layout.cell(e.row, e.slot);
        e.zoom = std::clamp(std::min(areaSize.x * 0.78f / cell.w(), areaSize.y * 0.78f / cell.h()), 0.1f, 12.0f);
        e.pan = ImVec2(areaSize.x * 0.5f - (cell.x0 + cell.w() * 0.5f) * e.zoom, areaSize.y * 0.5f - (cell.y0 + cell.h() * 0.5f) * e.zoom);
        e.focusPending = false;
    }
    float scale = e.zoom > 0.0f ? e.zoom : fit;
    ImVec2 origin = e.zoom > 0.0f ? ImVec2(areaMin.x + e.pan.x, areaMin.y + e.pan.y)
                                  : ImVec2(areaMin.x + (areaSize.x - layout.w * scale) * 0.5f, areaMin.y + (areaSize.y - layout.h * scale) * 0.5f);
    auto zoomAround = [&](ImVec2 at, float factor) {
        const float next = std::clamp(scale * factor, 0.08f, 16.0f);
        const ImVec2 sheet((at.x - origin.x) / scale, (at.y - origin.y) / scale);
        e.zoom = next;
        e.pan = ImVec2(at.x - sheet.x * next - areaMin.x, at.y - sheet.y * next - areaMin.y);
        scale = next;
        origin = ImVec2(areaMin.x + e.pan.x, areaMin.y + e.pan.y);
    };
    const ImVec2 areaCenter((areaMin.x + areaMax.x) * 0.5f, (areaMin.y + areaMax.y) * 0.5f);
    if (e.zoomTo > 0.0f) {
        zoomAround(areaCenter, e.zoomTo / scale);
        e.zoomTo = 0.0f;
    }
    if (zoomIn) zoomAround(areaCenter, 1.25f);
    if (zoomOut) zoomAround(areaCenter, 0.8f);
    if (hovered && !e.showFinal && io.MouseWheel != 0.0f) zoomAround(io.MousePos, std::pow(1.15f, io.MouseWheel));
    // Desplazar: boton central o espacio + arrastrar.
    const bool space = ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput;
    if (active && !e.showFinal && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (space && ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
        if (e.zoom <= 0.0f) {
            e.zoom = scale;
            e.pan = ImVec2(origin.x - areaMin.x, origin.y - areaMin.y);
        }
        e.pan.x += io.MouseDelta.x;
        e.pan.y += io.MouseDelta.y;
        origin = ImVec2(areaMin.x + e.pan.x, areaMin.y + e.pan.y);
        e.panning = true;
    } else if (!active) {
        e.panning = false;
    }
    e.lastScale = scale;
    auto toScreen = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
    const ImVec2 mouse((io.MousePos.x - origin.x) / scale, (io.MousePos.y - origin.y) / scale);

    const SpriteRect current = layout.cell(e.row, e.slot);
    if (e.showFinal) {
        drawSpriteFinal(app, areaMin, areaSize, hovered, active);
    } else {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(areaMin, areaMax, true);
        draw->AddRectFilled(areaMin, areaMax, IM_COL32(12, 14, 18, 255));
        const ImVec2 sheetMin = toScreen(0.0f, 0.0f), sheetMax = toScreen(static_cast<float>(layout.w), static_cast<float>(layout.h));
        // Los huecos: cuadricula, rotulo de cada fila, libres a trazos.
        for (int r = 0; r < static_cast<int>(layout.rows.size()); ++r) {
            const SheetRow& row = layout.rows[static_cast<size_t>(r)];
            if (!e.docSingle)
            {
                // El rotulo de la fila, con fondo: con poco zoom cae sobre la fila de arriba.
                const char* label = drawnPieceName(app, row.piece);
                const ImVec2 at(toScreen(static_cast<float>(kSheetGap), 0.0f).x, toScreen(0.0f, static_cast<float>(row.y)).y - ImGui::GetFontSize() - 3.0f);
                const ImVec2 size = ImGui::CalcTextSize(label);
                draw->AddRectFilled(ImVec2(at.x - 4.0f, at.y - 1.0f), ImVec2(at.x + size.x + 4.0f, at.y + size.y + 1.0f), IM_COL32(12, 14, 18, 215), 4.0f);
                draw->AddText(at, r == e.row ? ui::color::Accent : ui::color::Muted, label);
            }
            for (int s = 0; s < layout.slots; ++s) {
                const SpriteRect cell = layout.cell(r, s);
                const ImVec2 a = toScreen(static_cast<float>(cell.x0), static_cast<float>(cell.y0));
                const ImVec2 b = toScreen(static_cast<float>(cell.x1), static_cast<float>(cell.y1));
                drawChecker(draw, a, b);
                const bool used = e.used.size() > static_cast<size_t>(r * kSheetSlots + s) && e.used[static_cast<size_t>(r * kSheetSlots + s)];
                if (!used) {
                    drawDashedRect(draw, a, b, ui::color::Faint);
                    if (b.x - a.x > 60.0f) {
                        const char* free = tr(app, "free", "libre");
                        const ImVec2 size = ImGui::CalcTextSize(free);
                        draw->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, (a.y + b.y - size.y) * 0.5f), ui::withAlpha(ui::color::Faint, 160), free);
                    }
                }
            }
        }
        // De cerca (o en pixel art) los pixeles se ven nitidos, como en un programa de dibujo.
        // El backend de ImGui usa su propio sampler (lineal) y no hace caso del
        // filtro de la textura: se le pide el cercano con su callback, y se
        // vuelve al lineal despues.
        const bool crisp = e.docPixel || scale >= 3.0f;
        const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        if (sheetImage.ok) app.renderer.setPreviewFiltering("notelab-live/sprite/canvas.png", !crisp);
        if (sheetImage.ok && crisp && platform.DrawCallback_SetSamplerNearest) draw->AddCallback(platform.DrawCallback_SetSamplerNearest, nullptr);
        if (sheetImage.ok) draw->AddImage(ImTextureRef(static_cast<ImTextureID>(sheetImage.texture)), sheetMin, sheetMax);
        // Papel cebolla: el fotograma anterior, transparente, sobre el elegido.
        if (e.onion && e.slot > 0 && sheetImage.ok) {
            ImVec2 uv0, uv1;
            uvOf(layout.cell(e.row, e.slot - 1), uv0, uv1);
            draw->AddImage(ImTextureRef(static_cast<ImTextureID>(sheetImage.texture)), toScreen(static_cast<float>(current.x0), static_cast<float>(current.y0)),
                           toScreen(static_cast<float>(current.x1), static_cast<float>(current.y1)), uv0, uv1, IM_COL32(255, 160, 200, 90));
        }
        if (sheetImage.ok && crisp && platform.DrawCallback_SetSamplerLinear) draw->AddCallback(platform.DrawCallback_SetSamplerLinear, nullptr);
        for (int r = 0; r < static_cast<int>(layout.rows.size()); ++r)
            for (int s = 0; s < layout.slots; ++s) {
                const SpriteRect cell = layout.cell(r, s);
                const bool used = e.used.size() > static_cast<size_t>(r * kSheetSlots + s) && e.used[static_cast<size_t>(r * kSheetSlots + s)];
                if (used) draw->AddRect(toScreen(static_cast<float>(cell.x0), static_cast<float>(cell.y0)), toScreen(static_cast<float>(cell.x1), static_cast<float>(cell.y1)), ui::color::Border);
            }
        const ImVec2 currentA = toScreen(static_cast<float>(current.x0), static_cast<float>(current.y0));
        const ImVec2 currentB = toScreen(static_cast<float>(current.x1), static_cast<float>(current.y1));
        draw->AddRect(ImVec2(currentA.x - 1.0f, currentA.y - 1.0f), ImVec2(currentB.x + 1.0f, currentB.y + 1.0f), ui::color::Accent, 0.0f, 0, 2.0f);
        tutorialMarkRect("sprite-cell", currentA, currentB);

        // Las herramientas sobre la hoja.
        SpriteLayer& layer = e.layers[static_cast<size_t>(e.layer)];
        const SpriteRect* clip = e.selection.empty() ? nullptr : &e.selection;
        auto shapeFrom = [&](ImVec2 a, ImVec2 b) {
            SpriteShape s;
            s.kind = e.shapeKind;
            float w = std::fabs(b.x - a.x), h = std::fabs(b.y - a.y);
            if (io.KeyShift) w = h = std::max(w, h);   // Shift: proporcion 1:1
            s.x = a.x + (b.x >= a.x ? w : -w) * 0.5f;
            s.y = a.y + (b.y >= a.y ? h : -h) * 0.5f;
            s.w = std::max(2.0f, w);
            s.h = std::max(2.0f, h);
            s.fill = e.strokeAlt ? e.second : e.color;
            s.outline = e.strokeAlt ? e.color : e.second;
            s.outlineWidth = e.shapeOutline;
            s.filled = e.shapeFill;
            return s;
        };
        auto rectFrom = [&](ImVec2 a, ImVec2 b) {
            return spriteClip({static_cast<int>(std::floor(std::min(a.x, b.x))), static_cast<int>(std::floor(std::min(a.y, b.y))),
                               static_cast<int>(std::ceil(std::max(a.x, b.x))), static_cast<int>(std::ceil(std::max(a.y, b.y)))},
                              layout.w, layout.h);
        };
        // Clic derecho: pintar con el color de contorno (pincel, figura y relleno).
        const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right) && e.tool <= 3 && !space;
        const bool clicked = (ImGui::IsItemClicked(ImGuiMouseButton_Left) || rightClicked) && !space;
        if (clicked) e.strokeAlt = rightClicked;
        const ImU32 paintColor = e.strokeAlt ? e.second : e.color;
        // Alt + clic: coger el color con cualquier herramienta de pintar.
        const bool picking = e.tool == 4 || (io.KeyAlt && e.tool <= 3);
        const float radius = e.docPixel ? std::max(0.5f, std::round(e.brushSize) * 0.5f) : e.brushSize * 0.5f;
        // Espejo: el punto reflejado dentro de su hueco (o del lienzo).
        auto mirrored = [&](float x, float y) {
            SpriteRect box{0, 0, layout.w, layout.h};
            int r = 0, s = 0;
            if (layout.hit(x, y, r, s)) box = layout.cell(r, s);
            return ImVec2(e.mirror == 2 ? static_cast<float>(box.x0 + box.x1) - x : x, e.mirror == 1 ? static_cast<float>(box.y0 + box.y1) - y : y);
        };
        auto stampAt = [&](float x, float y) {
            auto one = [&](float px, float py) {
                if (e.docPixel) stampPixelBrush(layer.image, px, py, std::max(1, static_cast<int>(std::lround(e.brushSize))), paintColor, e.brushOpacity, e.tool == 1, clip);
                else stampSpriteBrush(layer.image, px, py, radius, e.hardness, paintColor, e.brushOpacity, e.tool == 1, clip);
            };
            one(x, y);
            if (e.mirror != 0) {
                const ImVec2 m = mirrored(x, y);
                one(m.x, m.y);
            }
        };
        auto touchStroke = [&](float x0, float y0, float x1, float y1) {
            SpriteRect r = spriteAround(x0, y0, x1, y1, radius + 2.0f);
            if (e.mirror != 0) {
                const ImVec2 a = mirrored(x0, y0), b = mirrored(x1, y1);
                r = spriteUnion(r, spriteAround(a.x, a.y, b.x, b.y, radius + 2.0f));
            }
            spriteTouch(e, r);
        };
        // Shift con el pincel o la goma: una linea recta desde el ultimo punto
        // (se ve mientras se mueve el raton; el clic la pinta).
        const bool shiftLine = io.KeyShift && e.tool <= 1 && e.hasLast && !e.stroking && !space;
        auto stampLine = [&](ImVec2 a, ImVec2 b) {
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float spacing = std::max(0.5f, e.brushSize * 0.15f);
            const int steps = std::max(1, static_cast<int>(std::sqrt(dx * dx + dy * dy) / spacing));
            for (int i = 0; i <= steps; ++i) {
                const float k = static_cast<float>(i) / static_cast<float>(steps);
                stampAt(a.x + dx * k, a.y + dy * k);
            }
            touchStroke(a.x, a.y, b.x, b.y);
        };
        if (clicked) {
            e.message.clear();
            if (picking) {
                const int px = static_cast<int>(mouse.x), py = static_cast<int>(mouse.y);
                if (px >= 0 && py >= 0 && px < e.composite.w && py < e.composite.h && e.composite.at(px, py)[3] > 0) {
                    const std::uint8_t* p = e.composite.at(px, py);
                    (e.strokeAlt ? e.second : e.color) = IM_COL32(p[0], p[1], p[2], 255);
                }
            } else if (e.tool == 5 || e.tool == 6) {
                e.stroking = true;
                e.start = e.last = mouse;
            } else if (!layer.visible) {
                e.message = tr(app, "That layer is hidden: show it to draw on it.", "Esa capa está oculta: enséñala para dibujar en ella.");
            } else if (e.tool == 3) {
                // El bote no sale de la seleccion o, sin ella, del hueco.
                int row = 0, slot = 0;
                SpriteRect bounds{0, 0, layout.w, layout.h};
                if (clip && clip->contains(mouse.x, mouse.y)) bounds = *clip;
                else if (layout.hit(mouse.x, mouse.y, row, slot)) bounds = layout.cell(row, slot);
                spritePushStroke(e);
                floodSprite(layer.image, static_cast<int>(mouse.x), static_cast<int>(mouse.y), paintColor, e.tolerance, &bounds);
                spriteTouch(e, bounds);
                tutorialSignal("sp-draw");
            } else if (shiftLine) {
                spritePushStroke(e);
                stampLine(e.lastPoint, mouse);
                e.lastPoint = mouse;
                tutorialSignal("sp-draw");
            } else {
                spritePushStroke(e);
                e.stroking = true;
                e.start = e.last = mouse;
                if (e.tool <= 1) {
                    stampAt(mouse.x, mouse.y);
                    touchStroke(mouse.x, mouse.y, mouse.x, mouse.y);
                }
            }
        }
        if (hovered && e.tool == 5 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            int row = 0, slot = 0;
            if (layout.hit(mouse.x, mouse.y, row, slot)) {
                spriteSelectCell(e, row, slot);
                e.focusPending = true;
            }
        }
        if (active && e.stroking && e.tool <= 1 && !space) {
            // La pincelada sigue al raton sin huecos: sellos cada poco.
            const float dx = mouse.x - e.last.x, dy = mouse.y - e.last.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float spacing = std::max(0.5f, e.brushSize * 0.15f);
            if (distance >= spacing) {
                const int steps = static_cast<int>(distance / spacing);
                for (int i = 1; i <= steps; ++i) {
                    const float k = static_cast<float>(i) / static_cast<float>(steps);
                    stampAt(e.last.x + dx * k, e.last.y + dy * k);
                }
                touchStroke(e.last.x, e.last.y, mouse.x, mouse.y);
                e.last = mouse;
            }
        }
        if (e.stroking && !active) {
            const float movedScreen = std::sqrt((mouse.x - e.start.x) * (mouse.x - e.start.x) + (mouse.y - e.start.y) * (mouse.y - e.start.y)) * scale;
            if (e.tool <= 1) {
                e.hasLast = true;
                e.lastPoint = e.last;
                tutorialSignal("sp-draw");
            } else if (e.tool == 2 && movedScreen > 2.0f) {
                const SpriteShape shape = shapeFrom(e.start, mouse);
                paintSpriteShape(layer.image, shape, clip);
                float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
                for (const ImVec2& p : spriteOutline(shape)) {
                    x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
                    x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
                }
                if (e.docPixel) spritePixelize(layer.image, spriteAround(x0, y0, x1, y1, shape.outlineWidth + 2.0f));
                spriteTouch(e, spriteAround(x0, y0, x1, y1, shape.outlineWidth + 2.0f));
                tutorialSignal("sp-draw");
            } else if (e.tool == 5) {
                int row = 0, slot = 0;
                if (movedScreen > 4.0f) e.selection = rectFrom(e.start, mouse);
                else if (layout.hit(mouse.x, mouse.y, row, slot)) spriteSelectCell(e, row, slot);
                else e.selection = {};
            } else if (e.tool == 6 && movedScreen > 1.0f) {
                int dx = static_cast<int>(std::lround(mouse.x - e.start.x)), dy = static_cast<int>(std::lround(mouse.y - e.start.y));
                // Shift: solo en horizontal o en vertical.
                if (io.KeyShift) (std::abs(dx) >= std::abs(dy) ? dy : dx) = 0;
                spritePushStroke(e);
                if (!e.selection.empty()) {
                    const SpriteRect r = e.selection;
                    const Image moved = cropRect(layer.image, r.x0, r.y0, r.w(), r.h());
                    clearSprite(layer.image, r);
                    overSprite(layer.image, moved, r.x0 + dx, r.y0 + dy);
                    e.selection = spriteClip({r.x0 + dx, r.y0 + dy, r.x1 + dx, r.y1 + dy}, layout.w, layout.h);
                    spriteTouch(e, spriteUnion(r, {r.x0 + dx, r.y0 + dy, r.x1 + dx, r.y1 + dy}));
                } else {
                    const Image moved = layer.image;
                    layer.image = blankImage(layout.w, layout.h);
                    blit(layer.image, moved, dx, dy);
                    ++e.generation;
                }
            }
            e.stroking = false;
        }
        // Atajos de teclado (sin escribir en un campo).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
            if (io.KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) spriteUndo(e, false);
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) spriteUndo(e, true);
                if (ImGui::IsKeyPressed(ImGuiKey_C, false)) spriteCopy(app, false);
                if (ImGui::IsKeyPressed(ImGuiKey_X, false)) spriteCopy(app, true);
                if (ImGui::IsKeyPressed(ImGuiKey_V, false)) spritePasteAt(app, spriteWorkRect(e));
                if (ImGui::IsKeyPressed(ImGuiKey_A, false)) e.selection = {0, 0, layout.w, layout.h};
                if (ImGui::IsKeyPressed(ImGuiKey_D, false)) e.selection = {};
            } else {
                static const ImGuiKey toolKeys[7] = {ImGuiKey_B, ImGuiKey_E, ImGuiKey_U, ImGuiKey_G, ImGuiKey_I, ImGuiKey_M, ImGuiKey_V};
                for (int t = 0; t < 7; ++t)
                    if (ImGui::IsKeyPressed(toolKeys[t], false) && e.tool != t) {
                        e.tool = t;
                        tutorialSignal("sp-tools");
                    }
                if (ImGui::IsKeyPressed(ImGuiKey_X, false)) std::swap(e.color, e.second);
                // [ y ]: el pincel mas pequeno o mas grande (con Shift, su dureza).
                const bool smaller = ImGui::IsKeyPressed(ImGuiKey_LeftBracket), bigger = ImGui::IsKeyPressed(ImGuiKey_RightBracket);
                if (smaller || bigger) {
                    if (io.KeyShift) e.hardness = std::clamp(e.hardness + (bigger ? 0.1f : -0.1f), 0.0f, 1.0f);
                    else e.brushSize = std::clamp(e.brushSize + (bigger ? 1.0f : -1.0f) * std::max(1.0f, std::round(e.brushSize * 0.15f)), 1.0f, 80.0f);
                }
                // La vista: 0 toda la hoja, 1 al 100 %, F el hueco elegido.
                if (ImGui::IsKeyPressed(ImGuiKey_0, false)) e.zoom = 0.0f;
                if (ImGui::IsKeyPressed(ImGuiKey_1, false)) e.zoomTo = 1.0f;
                if (ImGui::IsKeyPressed(ImGuiKey_F, false)) e.focusPending = true;
                if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !e.selection.empty()) {
                    spritePushStroke(e);
                    clearSprite(layer.image, e.selection);
                    spriteTouch(e, e.selection);
                }
                // Mover 1 px con las flechas.
                const int ax = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? -1 : ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : 0;
                const int ay = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0;
                if (e.tool == 6 && (ax || ay)) {
                    spritePushStroke(e);
                    const SpriteRect r = e.selection.empty() ? SpriteRect{0, 0, layout.w, layout.h} : e.selection;
                    const Image moved = cropRect(layer.image, r.x0, r.y0, r.w(), r.h());
                    clearSprite(layer.image, r);
                    overSprite(layer.image, moved, r.x0 + ax, r.y0 + ay);
                    if (!e.selection.empty()) e.selection = spriteClip({r.x0 + ax, r.y0 + ay, r.x1 + ax, r.y1 + ay}, layout.w, layout.h);
                    ++e.generation;
                }
            }
        }
        // Lo que se esta haciendo: la figura, el rango o lo movido, antes de soltar.
        if (e.stroking && e.tool == 2) {
            std::vector<ImVec2> points = spriteOutline(shapeFrom(e.start, mouse));
            for (ImVec2& p : points) p = toScreen(p.x, p.y);
            if (e.shapeFill) draw->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), ui::withAlpha(e.color, 160));
            draw->AddPolyline(points.data(), static_cast<int>(points.size()), e.shapeOutline > 0.0f ? e.second : ui::color::Accent,
                              ImDrawFlags_Closed, std::max(1.5f, e.shapeOutline * scale));
        }
        SpriteRect showSelection = e.selection;
        if (e.stroking && e.tool == 5) showSelection = rectFrom(e.start, mouse);
        if (e.stroking && e.tool == 6) {
            const float dx = std::round(mouse.x - e.start.x), dy = std::round(mouse.y - e.start.y);
            const SpriteRect r = e.selection.empty() ? SpriteRect{0, 0, layout.w, layout.h} : e.selection;
            drawDashedRect(draw, toScreen(r.x0 + dx, r.y0 + dy), toScreen(r.x1 + dx, r.y1 + dy), ui::color::Accent);
        }
        if (!showSelection.empty()) {
            const ImVec2 a = toScreen(static_cast<float>(showSelection.x0), static_cast<float>(showSelection.y0));
            const ImVec2 b = toScreen(static_cast<float>(showSelection.x1), static_cast<float>(showSelection.y1));
            draw->AddRectFilled(a, b, IM_COL32(155, 123, 245, 26));
            draw->AddRect(a, b, IM_COL32(0, 0, 0, 200), 0.0f, 0, 3.0f);
            drawDashedRect(draw, a, b, IM_COL32(255, 255, 255, 230), 4.0f);
        }
        if (hovered && shiftLine) {
            const ImVec2 from = toScreen(e.lastPoint.x, e.lastPoint.y);
            draw->AddLine(from, io.MousePos, ui::withAlpha(e.tool == 1 ? IM_COL32(255, 255, 255, 255) : e.color, 150), std::max(1.5f, e.brushSize * scale));
            draw->AddLine(from, io.MousePos, IM_COL32(255, 255, 255, 200), 1.0f);
            draw->AddCircleFilled(from, 3.0f, ui::color::Accent);
        }
        // Pixel art de cerca: la rejilla de pixeles.
        if (e.docPixel && scale >= 6.0f) {
            const ImU32 grid = IM_COL32(255, 255, 255, 22);
            const int fromX = std::max(0, static_cast<int>((areaMin.x - origin.x) / scale)), toX = std::min(layout.w, static_cast<int>((areaMax.x - origin.x) / scale) + 1);
            const int fromY = std::max(0, static_cast<int>((areaMin.y - origin.y) / scale)), toY = std::min(layout.h, static_cast<int>((areaMax.y - origin.y) / scale) + 1);
            for (int x = fromX; x <= toX; ++x) draw->AddLine(toScreen(static_cast<float>(x), static_cast<float>(fromY)), toScreen(static_cast<float>(x), static_cast<float>(toY)), grid);
            for (int y = fromY; y <= toY; ++y) draw->AddLine(toScreen(static_cast<float>(fromX), static_cast<float>(y)), toScreen(static_cast<float>(toX), static_cast<float>(y)), grid);
        }
        if (hovered && e.tool <= 1 && !space && !picking) {
            if (e.docPixel) {
                // El tamano del pincel en pixel art: los pixeles exactos que pinta.
                const int size = std::max(1, static_cast<int>(std::lround(e.brushSize)));
                const float left = std::floor(mouse.x) - static_cast<float>((size - 1) / 2), top = std::floor(mouse.y) - static_cast<float>((size - 1) / 2);
                draw->AddRect(toScreen(left, top), toScreen(left + size, top + size), IM_COL32(255, 255, 255, 220), 0.0f, 0, 1.5f);
                const ImVec2 a = toScreen(left, top), b = toScreen(left + size, top + size);
                draw->AddRect(ImVec2(a.x - 1.5f, a.y - 1.5f), ImVec2(b.x + 1.5f, b.y + 1.5f), IM_COL32(0, 0, 0, 160));
            } else {
                // El tamano del pincel, como un circulo bajo el raton.
                draw->AddCircle(io.MousePos, e.brushSize * 0.5f * scale, IM_COL32(255, 255, 255, 200), 0, 1.5f);
                draw->AddCircle(io.MousePos, e.brushSize * 0.5f * scale + 1.5f, IM_COL32(0, 0, 0, 160), 0, 1.0f);
            }
            if (e.mirror != 0) {
                // Donde cae el espejo.
                const ImVec2 m = mirrored(mouse.x, mouse.y);
                draw->AddCircle(toScreen(m.x, m.y), std::max(3.0f, radius * scale), IM_COL32(155, 123, 245, 200), 0, 1.5f);
            }
        }
        if (hovered && picking) {
            // El cuentagotas: el color bajo el raton.
            const int px = static_cast<int>(mouse.x), py = static_cast<int>(mouse.y);
            if (px >= 0 && py >= 0 && px < e.composite.w && py < e.composite.h && e.composite.at(px, py)[3] > 0) {
                const std::uint8_t* p = e.composite.at(px, py);
                draw->AddCircleFilled(ImVec2(io.MousePos.x + 18.0f, io.MousePos.y - 18.0f), 11.0f, IM_COL32(p[0], p[1], p[2], 255));
                draw->AddCircle(ImVec2(io.MousePos.x + 18.0f, io.MousePos.y - 18.0f), 11.0f, IM_COL32(255, 255, 255, 220), 0, 2.0f);
            }
        }
        char zoomText[32];
        std::snprintf(zoomText, sizeof(zoomText), "%.0f %%", scale * 100.0f);
        draw->AddText(ImVec2(areaMax.x - ImGui::CalcTextSize(zoomText).x - 8.0f, areaMax.y - ImGui::GetFontSize() - 6.0f), ui::color::Faint, zoomText);
        draw->PopClipRect();
    }
    if (!e.message.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(areaMin.x + 8.0f, areaMin.y + 6.0f));
        ImGui::TextColored(ui::vec(ui::color::Warning), "%s", e.message.c_str());
    }
    ImGui::EndChild();

    // Derecha: las capas y como queda.
    ImGui::SameLine();
    ImGui::BeginChild("spritelayers", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders);
    if (!e.docSingle && hud && sheetImage.ok) {
        // El mapa: toda la hoja y, recuadrado, lo que se ve; clic o arrastrar para ir.
        ui::sectionHeader(tr(app, "Map", "Mapa"), ui::icon::Grid);
        const float mapW = ImGui::GetContentRegionAvail().x;
        const float mapH = mapW * static_cast<float>(layout.h) / static_cast<float>(layout.w);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("sheetmap", ImVec2(mapW, mapH));
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(at, ImVec2(at.x + mapW, at.y + mapH), IM_COL32(12, 14, 18, 255));
        list->AddImage(ImTextureRef(static_cast<ImTextureID>(sheetImage.texture)), at, ImVec2(at.x + mapW, at.y + mapH));
        const float k = mapW / static_cast<float>(layout.w);
        const ImVec2 seenA((areaMin.x - origin.x) / scale * k, (areaMin.y - origin.y) / scale * k);
        const ImVec2 seenB((areaMax.x - origin.x) / scale * k, (areaMax.y - origin.y) / scale * k);
        list->PushClipRect(at, ImVec2(at.x + mapW, at.y + mapH), true);
        list->AddRect(ImVec2(at.x + seenA.x, at.y + seenA.y), ImVec2(at.x + seenB.x, at.y + seenB.y), ui::color::Accent, 0.0f, 0, 1.5f);
        list->PopClipRect();
        list->AddRect(at, ImVec2(at.x + mapW, at.y + mapH), ui::color::Border);
        if (ImGui::IsItemActive()) {
            const ImVec2 p((io.MousePos.x - at.x) / k, (io.MousePos.y - at.y) / k);
            e.zoom = scale;
            e.pan = ImVec2(areaSize.x * 0.5f - p.x * scale, areaSize.y * 0.5f - p.y * scale);
        }
        ui::tooltip(tr(app, "Click or drag to move the view", "Clic o arrastra para mover la vista"));
    }
    ui::sectionHeader(tr(app, "Layers", "Capas"), ui::icon::Layers);
    if (spriteIconButton("newlayer", ui::icon::Add, tr(app, "New layer", "Capa nueva"), e.layers.size() < 16))
        spriteAddLayer(e, spriteSheetLayer(layout, (es ? "Capa " : "Layer ") + std::to_string(e.layers.size() + 1)));
    ImGui::SameLine(0.0f, 3.0f);
    if (spriteIconButton("duplayer", ui::icon::Copy, tr(app, "Duplicate layer", "Duplicar capa"), e.layers.size() < 16)) {
        SpriteLayer copy = e.layers[static_cast<size_t>(e.layer)];
        copy.name += es ? " (copia)" : " (copy)";
        spriteAddLayer(e, std::move(copy));
    }
    ImGui::SameLine(0.0f, 3.0f);
    if (spriteIconButton("dellayer", ui::icon::Delete, tr(app, "Delete layer", "Borrar capa"), e.layers.size() > 1)) {
        spritePushUndo(e);
        e.layers.erase(e.layers.begin() + e.layer);
        e.layer = std::max(0, e.layer - 1);
        ++e.generation;
    }
    ImGui::SameLine(0.0f, 3.0f);
    if (spriteIconButton("uplayer", sprite_icon::Up, tr(app, "Move up", "Subir"), e.layer + 1 < static_cast<int>(e.layers.size()))) {
        spritePushUndo(e);
        std::swap(e.layers[static_cast<size_t>(e.layer)], e.layers[static_cast<size_t>(e.layer + 1)]);
        ++e.layer;
        ++e.generation;
    }
    ImGui::SameLine(0.0f, 3.0f);
    if (spriteIconButton("downlayer", sprite_icon::DownArrow, tr(app, "Move down", "Bajar"), e.layer > 0)) {
        spritePushUndo(e);
        std::swap(e.layers[static_cast<size_t>(e.layer)], e.layers[static_cast<size_t>(e.layer - 1)]);
        --e.layer;
        ++e.generation;
    }
    ImGui::SameLine(0.0f, 3.0f);
    if (spriteIconButton("mergelayer", ui::icon::Down, tr(app, "Merge down", "Unir abajo"), e.layer > 0)) {
        spritePushUndo(e);
        std::vector<SpriteLayer> pair{e.layers[static_cast<size_t>(e.layer - 1)], e.layers[static_cast<size_t>(e.layer)]};
        pair[0].visible = pair[1].visible = true;
        SpriteLayer merged = e.layers[static_cast<size_t>(e.layer - 1)];
        merged.image = composeSpriteLayers(pair);
        merged.opacity = 1.0f;
        e.layers[static_cast<size_t>(e.layer - 1)] = merged;
        e.layers.erase(e.layers.begin() + e.layer);
        --e.layer;
        ++e.generation;
    }
    ImGui::BeginChild("layerlist", ImVec2(0.0f, std::min(220.0f, 30.0f * static_cast<float>(e.layers.size()) + 8.0f)));
    for (int i = static_cast<int>(e.layers.size()) - 1; i >= 0; --i) {
        SpriteLayer& one = e.layers[static_cast<size_t>(i)];
        ImGui::PushID(i);
        if (ImGui::Button((std::string(one.visible ? ui::icon::Eye : ui::icon::EyeOff) + "##vis").c_str(), ImVec2(30.0f, 0.0f))) {
            one.visible = !one.visible;
            ++e.generation;
        }
        ui::tooltip(tr(app, "Show or hide", "Ver u ocultar"));
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Selectable(one.name.c_str(), e.layer == i, 0, ImVec2(0.0f, ImGui::GetFrameHeight()))) e.layer = i;
        ImGui::PopID();
    }
    ImGui::EndChild();
    SpriteLayer& chosen = e.layers[static_cast<size_t>(e.layer)];
    ImGui::SetNextItemWidth(-1.0f);
    float opacity = chosen.opacity;
    if (ImGui::SliderFloat("##layeropacity", &opacity, 0.0f, 1.0f, tr(app, "Layer opacity %.2f", "Opacidad de la capa %.2f"))) {
        chosen.opacity = opacity;
        ++e.generation;
    }
    if (ImGui::SmallButton(ui::label(sprite_icon::Erase, tr(app, "Clear layer", "Vaciar capa")).c_str())) {
        spritePushStroke(e);
        e.layers[static_cast<size_t>(e.layer)].image = blankImage(layout.w, layout.h);
        ++e.generation;
    }

    ui::sectionHeader(tr(app, "How it looks", "Cómo queda"), ui::icon::Eye);
    const DrawnPiece piece = layout.rows[static_cast<size_t>(e.row)].piece;
    // La animacion de la fila: sus huecos con algo, a sus fotogramas por segundo.
    std::vector<int> frames;
    for (int s = 0; s < layout.slots; ++s)
        if (e.used.size() > static_cast<size_t>(e.row * kSheetSlots + s) && e.used[static_cast<size_t>(e.row * kSheetSlots + s)]) frames.push_back(s);
    // En el aspecto de una nota: «En el juego» (la nota con su sostenido) a la
    // izquierda y lo demas a su lado, para que quepa sin desplazar el panel.
    const bool noteMode = !hud && !e.docSingle && sheetImage.ok;
    bool holdDrawn = false;
    if (noteMode) {
        auto firstUsed = [&](int r) {
            for (int s = 0; r >= 0 && s < layout.slots; ++s)
                if (e.used.size() > static_cast<size_t>(r * kSheetSlots + s) && e.used[static_cast<size_t>(r * kSheetSlots + s)]) return s;
            return -1;
        };
        const int noteRow = layout.rowOf(DrawnPiece::Note), pieceRow = layout.rowOf(DrawnPiece::HoldPiece), endRow = layout.rowOf(DrawnPiece::HoldEnd);
        const int noteSlot = firstUsed(noteRow), pieceSlot = firstUsed(pieceRow), endSlot = firstUsed(endRow);
        holdDrawn = pieceSlot >= 0 || endSlot >= 0;
        ImGui::BeginGroup();
        ImGui::TextColored(ui::vec(ui::color::Muted), "%s", tr(app, "In the game", "En el juego"));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float columnW = 60.0f, columnH = 172.0f, k = columnW / static_cast<float>(drawnPieceSize(DrawnPiece::Note));
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(at, ImVec2(at.x + columnW + 14.0f, at.y + columnH), IM_COL32(12, 14, 18, 255), 6.0f);
        const float cx = at.x + 7.0f + columnW * 0.5f;
        const float noteY = at.y + 6.0f;
        const float holdSide = static_cast<float>(drawnPieceSize(DrawnPiece::HoldPiece)) * k;
        const float endY = at.y + columnH - 6.0f - holdSide;
        ImVec2 uv0, uv1;
        if (pieceSlot >= 0) {
            uvOf(layout.cell(pieceRow, pieceSlot), uv0, uv1);
            list->AddImage(ImTextureRef(static_cast<ImTextureID>(sheetImage.texture)), ImVec2(cx - holdSide * 0.5f, noteY + columnW * 0.5f),
                           ImVec2(cx + holdSide * 0.5f, endY), uv0, uv1);
        }
        if (endSlot >= 0) {
            uvOf(layout.cell(endRow, endSlot), uv0, uv1);
            list->AddImage(ImTextureRef(static_cast<ImTextureID>(sheetImage.texture)), ImVec2(cx - holdSide * 0.5f, endY), ImVec2(cx + holdSide * 0.5f, endY + holdSide), uv0, uv1);
        }
        if (noteSlot >= 0) {
            uvOf(layout.cell(noteRow, noteSlot), uv0, uv1);
            list->AddImage(ImTextureRef(static_cast<ImTextureID>(sheetImage.texture)), ImVec2(cx - columnW * 0.5f, noteY), ImVec2(cx + columnW * 0.5f, noteY + columnW), uv0, uv1);
        }
        ImGui::Dummy(ImVec2(columnW + 14.0f, columnH));
        tutorialMark("sprite-ingame");
        ui::tooltip(holdDrawn ? tr(app, "The hold piece is repeated as long as the note lasts.", "El tramo se repite lo que dure la nota.")
                              : tr(app, "Without a drawn hold, the mod's stays.", "Sin sostenido dibujado, se queda el del mod."));
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::BeginGroup();
    }
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float box = noteMode ? 58.0f : 76.0f;
        drawChecker(ImGui::GetWindowDrawList(), at, ImVec2(at.x + box, at.y + box));
        if (!frames.empty() && sheetImage.ok) {
            const int fps = std::max(1, e.fps[static_cast<size_t>(piece)]);
            const int pick = e.playing ? frames[static_cast<size_t>(static_cast<long long>(ImGui::GetTime() * fps) % static_cast<long long>(frames.size()))]
                                       : (std::find(frames.begin(), frames.end(), e.slot) != frames.end() ? e.slot : frames.front());
            ImVec2 uv0, uv1;
            uvOf(layout.cell(e.row, pick), uv0, uv1);
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(sheetImage.texture)), at, ImVec2(at.x + box, at.y + box), uv0, uv1);
        }
        ImGui::Dummy(ImVec2(box, box));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(drawnPieceName(app, piece));
        ui::caption(frames.empty() ? tr(app, "Nothing drawn: the style's stays.", "Nada dibujado: se queda la del estilo.")
                                   : (std::to_string(frames.size()) + (es ? (frames.size() == 1 ? " fotograma" : " fotogramas") : (frames.size() == 1 ? " frame" : " frames"))).c_str());
        ImGui::EndGroup();
    }
    if (piece == DrawnPiece::Note || piece == DrawnPiece::Strum || piece == DrawnPiece::StrumPress || piece == DrawnPiece::StrumConfirm) {
        if (ImGui::Checkbox(noteMode ? tr(app, "Rotate it", "Girarla") : tr(app, "Rotate it for each direction", "Girarla para cada dirección"), &e.rotate)) e.painted = -1;
        if (noteMode) ui::tooltip(tr(app, "Rotate it for each direction", "Girarla para cada dirección"));
        // Las cuatro direcciones del hueco elegido, al dejar de pintar.
        const int previewKey = e.generation * 64 + e.row * kSheetSlots + e.slot + (e.rotate ? 0 : 32);
        if (app.rendererReady && e.painted != previewKey && !e.stroking && ImGui::GetTime() - e.changedAt > 0.12) {
            const Image cell = cropRect(e.composite, current.x0, current.y0, current.w(), current.h());
            for (int d = 0; d < 4; ++d) uploadLiveImage(app, "notelab-live/sprite/" + std::to_string(d) + ".png", e.rotate ? rotateForDirection(cell, d) : cell);
            e.painted = previewKey;
        }
        const float side = noteMode ? 38.0f : 46.0f;
        for (int d = 0; d < 4 && e.painted >= 0; ++d) {
            const GlRenderer::PreviewImage image = app.renderer.previewImage("notelab-live/sprite/" + std::to_string(d) + ".png");
            if (!image.ok) continue;
            const ImVec2 cellAt = ImGui::GetCursorScreenPos();
            drawChecker(ImGui::GetWindowDrawList(), cellAt, ImVec2(cellAt.x + side, cellAt.y + side));
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image.texture)), ImVec2(side, side));
            const bool wrap = noteMode && d == 1;
            if (d < 3 && !wrap) ImGui::SameLine(0.0f, 4.0f);
        }
    }
    if (noteMode) ImGui::EndGroup();
    if (e.docFreeW > 0)
        ui::caption((std::to_string(e.docFreeW) + "×" + std::to_string(e.docFreeH) + tr(app, " px: in the sheet it is fitted to its box.", " px: en la hoja se ajusta a su hueco.")).c_str());
    else
        ui::caption(hud ? tr(app, "In the HUD they are tinted with each direction's color.", "En el HUD se tiñen con el color de cada dirección.")
                        : tr(app, "160 px, the size of the mod's notes in the game.", "160 px, del tamaño de las notas del mod en el juego."));
    ImGui::EndChild();

    // Abajo: la linea de tiempo de la pieza elegida.
    ImGui::BeginChild("spritetimeline", ImVec2(0.0f, timelineHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    if (e.docSingle) {
        // Un dibujo suelto es un solo fotograma: va a la hoja general.
        const bool freeCanvas = e.docFreeW > 0;
        ui::sectionHeader(freeCanvas ? tr(app, "Free canvas", "Lienzo libre") : tr(app, "Loose drawing", "Dibujo suelto"), freeCanvas ? ui::icon::Grid : ui::icon::Brush);
        if (freeCanvas) {
            const int cell = drawnPieceSize(e.docPiece);
            ui::caption((std::to_string(e.docFreeW) + "×" + std::to_string(e.docFreeH) + " px" + (e.docPixel ? " · pixel art" : "") +
                         (app.spanish ? ". «Pasar a la hoja general» lo pone como " : ". «To the general sheet» puts it as ") + drawnPieceName(app, e.docPiece) +
                         (app.spanish ? " en el siguiente hueco libre, ajustado a " : " in the next free box, fitted to ") + std::to_string(cell) + "×" + std::to_string(cell) +
                         (e.docPixel ? (app.spanish ? " px, nítido (cada píxel crece entero)." : " px, crisp (each pixel grows whole).")
                                     : (app.spanish ? " px." : " px.")) +
                         (app.spanish ? " La flecha de al lado elige otra pieza o el hueco." : " The arrow next to it picks another piece or the box."))
                            .c_str());
        } else if (e.fromRow >= 0) {
            ui::caption(tr(app, "It came from a box of the sheet: «To the general sheet» puts it back there.",
                                "Salió de un hueco de la hoja: «Pasar a la hoja general» lo devuelve ahí."));
        } else {
            ui::caption(tr(app, "«To the general sheet» puts it in the next free box of its row: one more frame. The arrow next to it picks another piece or the box.",
                                "«Pasar a la hoja general» lo pone en el siguiente hueco libre de su fila: un fotograma más. La flecha de al lado elige otra pieza o el hueco."));
        }
        ImGui::EndChild();
    } else {
    std::vector<const char*> rowNames;
    for (const SheetRow& row : layout.rows) rowNames.push_back(drawnPieceName(app, row.piece));
    int row = e.row;
    if (ui::segmented("spriterows", &row, rowNames.data(), static_cast<int>(rowNames.size()))) {
        spriteSelectCell(e, row, 0);
        e.selection = {};
        if (e.zoom > 0.0f) e.focusPending = true;
    }
    ImGui::SameLine(0.0f, 16.0f);
    if (spriteIconButton("prevframe", sprite_icon::Previous, tr(app, "Previous frame", "Fotograma anterior"), e.slot > 0)) {
        spriteSelectCell(e, e.row, e.slot - 1);
        if (e.zoom > 0.0f) e.focusPending = true;
    }
    ImGui::SameLine(0.0f, 3.0f);
    if (spriteIconButton("play", e.playing ? ui::icon::Pause : ui::icon::Play, e.playing ? tr(app, "Pause", "Pausa") : tr(app, "Play the animation", "Reproducir la animación"))) {
        e.playing = !e.playing;
        tutorialSignal("sp-frames");
    }
    ImGui::SameLine(0.0f, 3.0f);
    if (spriteIconButton("nextframe", sprite_icon::Next, tr(app, "Next frame", "Fotograma siguiente"), e.slot + 1 < layout.slots)) {
        spriteSelectCell(e, e.row, e.slot + 1);
        if (e.zoom > 0.0f) e.focusPending = true;
    }
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderInt("##fps", &e.fps[static_cast<size_t>(piece)], 1, 30, tr(app, "%d FPS", "%d FPS"));
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::Checkbox(tr(app, "Onion skin", "Papel cebolla"), &e.onion);
    ui::tooltip(tr(app, "Shows the previous frame faded over the chosen one", "Enseña el fotograma anterior, transparente, sobre el elegido"));
    // Los huecos de la fila, como miniaturas.
    const SheetRow& timelineRow = layout.rows[static_cast<size_t>(e.row)];
    for (int s = 0; s < layout.slots; ++s) {
        ImGui::PushID(s);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float box = 50.0f;
        const bool pressed = ImGui::InvisibleButton("frame", ImVec2(box, box));
        ImDrawList* list = ImGui::GetWindowDrawList();
        drawChecker(list, at, ImVec2(at.x + box, at.y + box));
        const bool used = e.used.size() > static_cast<size_t>(e.row * kSheetSlots + s) && e.used[static_cast<size_t>(e.row * kSheetSlots + s)];
        if (used && sheetImage.ok) {
            ImVec2 uv0, uv1;
            uvOf(layout.cell(e.row, s), uv0, uv1);
            list->AddImage(ImTextureRef(static_cast<ImTextureID>(sheetImage.texture)), at, ImVec2(at.x + box, at.y + box), uv0, uv1);
        } else {
            drawDashedRect(list, at, ImVec2(at.x + box, at.y + box), ui::color::Faint, 4.0f);
        }
        list->AddText(ImVec2(at.x + 3.0f, at.y + 1.0f), used ? ui::color::Text : ui::color::Faint, std::to_string(s + 1).c_str());
        if (s == e.slot) list->AddRect(at, ImVec2(at.x + box, at.y + box), ui::color::Accent, 0.0f, 0, 2.0f);
        ui::tooltip(used ? tr(app, "Frame with a drawing", "Fotograma con dibujo") : tr(app, "Free box", "Hueco libre"));
        if (pressed) {
            spriteSelectCell(e, e.row, s);
            if (e.zoom > 0.0f) e.focusPending = true;
        }
        ImGui::PopID();
        ImGui::SameLine(0.0f, 4.0f);
    }
    (void)timelineRow;
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginGroup();
    if (ImGui::Button(ui::label(ui::icon::Copy, tr(app, "Duplicate frame", "Duplicar fotograma")).c_str())) spriteDuplicateFrame(app);
    ui::tooltip(tr(app, "Copies this box (every layer) to the next free one", "Copia este hueco (todas las capas) al siguiente libre"));
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button(ui::label(ui::icon::Delete, tr(app, "Delete frame", "Borrar fotograma")).c_str())) spriteDeleteFrame(app);
    ui::tooltip(tr(app, "Empties this box and moves the following ones back", "Vacía este hueco y corre los de detrás"));
    if (spriteIconButton("frameleft", sprite_icon::Previous, tr(app, "Move the frame left", "Mover el fotograma a la izquierda"), e.slot > 0)) spriteSwapFrames(app, e.slot - 1);
    ImGui::SameLine(0.0f, 3.0f);
    if (spriteIconButton("frameright", sprite_icon::Next, tr(app, "Move the frame right", "Mover el fotograma a la derecha"), e.slot + 1 < layout.slots)) spriteSwapFrames(app, e.slot + 1);
    ImGui::EndGroup();
    tutorialMarkRect("sprite-timeline", ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
    ImGui::EndChild();
    }

    // Lo de abajo del todo.
    ImGui::Separator();
    ui::caption(hud ? tr(app, "It goes to the HUD you are creating: each row with something replaces that piece. Nothing is created until Create HUD.",
                             "Va al HUD que estás creando: cada fila con algo sustituye esa pieza. No se crea nada hasta «Crear HUD».")
                    : tr(app, "It goes as the note's look (note and hold), saved with the project. The mod is not touched.",
                             "Va como el aspecto de la nota (nota y sostenido) y se guarda con el proyecto. El mod no se toca."));
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 290.0f);
    bool escape = escapeClosesWindow();
    if (escape && !e.selection.empty()) {
        // Esc con una seleccion la quita; otra vez, cierra.
        e.selection = {};
        escape = false;
    }
    if (ImGui::Button(tr(app, "Cancel", "Cancelar"), ImVec2(110.0f, 0.0f)) || escape) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    const bool use = ui::primaryButton(ui::label(ui::icon::Check, hud ? tr(app, "Use in the HUD", "Usar en el HUD")
                                                                      : tr(app, "Use as its look", "Usar como su aspecto")),
                                       ImVec2(170.0f, 0.0f));
    tutorialMark("sprite-use");
    // En Crear HUD, la captura automatica es la de esa ventana: aqui no se usa.
    if (use || (app.autoCommit && !hud)) {
        if (!hud) app.autoCommit = false;
        if (hud) {
            applySpriteHud(app);
            ImGui::CloseCurrentPopup();
        } else if (applySpriteLook(app)) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

// ------------------------------------------------ la hoja de un estilo --

// El archivo con todos los assets de las notas de un estilo: una hoja Sparrow
// (PNG + XML) con cada pieza y sus fotogramas, con los nombres del juego base,
// lista para un mod o para volver a abrirla aqui. Nunca dentro de un mod abierto.
void saveStyleSheetTo(NoteLabApp& app, const fs::path& chosen) {
    const NoteStyle* style = selectedStyle(app);
    if (!style || app.selSource < 0 || app.selSource >= static_cast<int>(app.sources.size())) return;
    const Source& source = *app.sources[static_cast<size_t>(app.selSource)];
    fs::path png = chosen;
    if (lowerText(png.extension().u8string()) != ".png") png += ".png";
    fs::path xml = png;
    xml.replace_extension(".xml");
    if (insideOpenMod(app, png)) {
        setStatus(app, "That folder is inside an open mod: Note Lab never writes there. Pick another one.",
                       "Esa carpeta está dentro de un mod abierto: Note Lab nunca escribe ahí. Elige otra.");
        return;
    }
    int pieces = 0;
    const SparrowOut sheet = styleSheetAtlas(*style, memoryIo(source, *style), png.stem().u8string(), &pieces);
    if (!sheet.ok) {
        setStatus(app, "That style has no readable pieces to put in a sheet.", "Ese estilo no tiene piezas legibles para poner en una hoja.");
        return;
    }
    std::ofstream image(png, std::ios::binary), atlas(xml, std::ios::binary);
    image.write(reinterpret_cast<const char*>(sheet.png.data()), static_cast<std::streamsize>(sheet.png.size()));
    atlas.write(sheet.xml.data(), static_cast<std::streamsize>(sheet.xml.size()));
    if (!image || !atlas) {
        setStatus(app, "The sheet could not be written there.", "No se pudo escribir la hoja ahí.");
        return;
    }
    setStatus(app, "Note sheet saved: " + std::to_string(pieces) + " pieces in " + png.filename().u8string() + " + " + xml.filename().u8string() +
                       ", with the base game's names.",
              "Hoja de notas guardada: " + std::to_string(pieces) + " piezas en " + png.filename().u8string() + " + " + xml.filename().u8string() +
                  ", con los nombres del juego base.");
}

// fml_notelab — Los bloques de un tipo de nota: un editor de bloques de
// verdad, como Scratch o App Inventor, especializado en notas de FNF
// (DESIGN_PLUGIN_NOTE_LAB §17, §24, §25.6 y §29).
//
// Un tipo de nota lleva un programa: pilas de bloques sueltas en un lienzo.
// Solo hacen algo las que empiezan con un evento:
//   - «al crear <tipo>»: sus propiedades, lo que la nota es (datos que cada
//     motor ya lee: `ignoreNote`, `hitHealth`, `noteSplashData`...);
//   - «cuando <quien> toca <tipo>» y «cuando el jugador falla <tipo>»:
//     acciones, condiciones y valores, que salen como codigo de cada motor.
// De ahi sale `data/notes/<tipo>.hx` en Codename, `custom_notetypes/<tipo>.txt`
// y `.lua` en Psych y un NoteKind en `scripts/notekinds/` en V-Slice. Solo
// entran bloques cuya realizacion esta leida en la fuente del motor; cada una
// lleva su cita, y lo que un motor no puede hacer es un choque dicho.
#pragma once

#include "NoteStyle.hpp"

#include <map>
#include <string>
#include <vector>

namespace fml::notelab {

// -------------------------------------------------------------- catalogo --

// Las categorias de la paleta, con los colores de Scratch 3, en el orden en
// que se usan: el evento, lo que la nota es, lo que hace y, al final, la
// logica (control, operadores y sensores).
enum class BlockCategory { Events, Properties, Game, Animation, Camera, Sound, Control, Operators, Sensing, Variables };
constexpr int kBlockCategories = 10;
const char* categoryName(BlockCategory category, bool spanish);

// La forma: sombrero (un evento), instruccion, C (si), C doble (si / si no),
// valor redondo (un numero) y valor hexagonal (si o no).
enum class BlockShape { Hat, Statement, If, IfElse, Number, Boolean };

// Un argumento: una ranura que admite un valor escrito o un bloque (numero,
// texto, si/no), un desplegable, un color, un sonido del mod o el cuerpo de
// un bloque C.
enum class ArgKind { Number, Text, Boolean, Choice, Color, Sound, Body, Variable, Image, Video };

struct ArgChoice {
    const char* value;
    const char* en;
    const char* es;
};

struct ArgDef {
    ArgKind kind = ArgKind::Number;
    const char* def = "";
    std::vector<ArgChoice> choices;
    float min = -1.0e6f, max = 1.0e6f;
    bool plug = true;         // una ranura de numero o si/no admite un bloque
};

// Donde puede ir un bloque.
enum BlockPlace : unsigned {
    kPlaceTop = 1u,      // suelto en el lienzo: los eventos
    kPlaceCreate = 2u,   // directamente bajo «al crear»: las propiedades
    kPlaceHit = 4u,      // bajo «cuando ... toca», tambien dentro de un «si»
    kPlaceMiss = 8u,     // bajo «cuando el jugador falla»
};

struct BlockDef {
    const char* key;          // clave estable (§17), en ingles
    BlockCategory category;
    BlockShape shape;
    const char* textEn;       // «change health by {0}»: {n} = argumento n, {type} = el tipo
    const char* textEs;
    const char* elseEn;       // la segunda linea de un «si / si no»
    const char* elseEs;
    const char* helpEn;
    const char* helpEs;
    std::vector<ArgDef> args;
    unsigned places;
};
const std::vector<BlockDef>& blockDefs();
const BlockDef* blockDef(const std::string& key);
// El texto de una opcion de un desplegable.
const char* choiceLabel(const ArgDef& arg, const std::string& value, bool spanish);
// El nombre corto de un bloque, sin sus valores: «vida al tocarla».
std::string blockName(const std::string& key, bool spanish);

// -------------------------------------------------------------- programa --

struct BlockArg {
    std::string value;        // lo escrito o elegido
    int block = -1;           // el bloque enchufado (valor) o el primero del cuerpo
};

struct BlockNode {
    std::string key;
    std::vector<BlockArg> args;
    int next = -1;            // el siguiente de la pila
    float x = 0.0f, y = 0.0f; // en el lienzo, si encabeza una pila suelta
    std::string comment;
};

struct BlockDraft {
    Engine engine = Engine::Codename;
    std::string path, text, baseline, error;
};

struct BlockProgram {
    std::map<int, BlockNode> nodes;
    std::vector<int> tops;    // las pilas sueltas, de atras adelante
    int nextId = 1;
    std::string comment;
    std::map<std::string, BlockDraft> drafts;

    bool empty() const { return nodes.empty() && drafts.empty() && comment.empty(); }
    BlockNode* node(int id);
    const BlockNode* node(int id) const;
};

struct BlockVariable {
    int id = -1;
    std::string name;
    double initial = 0.0;
};

std::vector<BlockVariable> blockVariables(const BlockProgram& program);
const BlockNode* referencedVariable(const BlockProgram& program, const std::string& reference);
constexpr int kBlockRepeatLimit = 64;
constexpr int kBlockExecutionBudget = 512;
constexpr int kBlockPendingLimit = 32;

// Un bloque nuevo con sus valores por defecto, aun sin colocar.
int newBlock(BlockProgram& program, const std::string& key);

// De donde cuelga un bloque: de nada (encabeza una pila), del `next` de otro
// (`arg` = -1) o de un argumento de otro.
struct BlockLink {
    int parent = -1;
    int arg = -1;
};
BlockLink linkOf(const BlockProgram& program, int id);
int rootOf(const BlockProgram& program, int id);
int lastOf(const BlockProgram& program, int id);           // el ultimo de su cadena
std::vector<int> chainOf(const BlockProgram& program, int first);

// Editar. Arrastrar un bloque se lleva los que tiene debajo (como Scratch).
void detach(BlockProgram& program, int id);
void placeTop(BlockProgram& program, int id, float x, float y);
void attachAfter(BlockProgram& program, int target, int id);
void attachBody(BlockProgram& program, int target, int arg, int id);
// El valor que ya habia en la ranura sale suelto en (`bumpX`, `bumpY`).
void attachValue(BlockProgram& program, int target, int arg, int id, float bumpX, float bumpY);
// La pila `id` encima de la pila suelta `top`, que queda colgando de ella.
void attachAbove(BlockProgram& program, int top, int id, float x, float y);
void removeStack(BlockProgram& program, int id);           // el bloque y todo lo que cuelga
void removeBlock(BlockProgram& program, int id);           // solo el bloque; los de debajo suben
// Copias sin colocar (hay que llamar a placeTop o a un attach despues).
int duplicateStack(BlockProgram& program, int id);         // el bloque y los de debajo
int duplicateBlock(BlockProgram& program, int id);         // el bloque solo, con lo que lleva dentro
// Una pila de otro programa, copiada con ids nuevos y sin colocar.
int copyStackFrom(BlockProgram& program, const BlockProgram& from, int id);

// Deja un programa leido de fuera bien formado: quita los bloques que no
// conoce, ajusta sus argumentos, corta enlaces rotos, repetidos o en ciclo y
// saca a la vista (como pilas sueltas) los bloques que no cuelgan de nada.
void repairProgram(BlockProgram& program);

// Donde esta un bloque: kPlaceCreate (directamente bajo «al crear»),
// kPlaceHit, kPlaceMiss, kPlaceTop (encabeza una pila) o 0 (en una pila
// suelta, o dentro de un «si» bajo «al crear»).
unsigned placeOf(const BlockProgram& program, int id);
// Donde quedaria un bloque puesto tras `target` (`arg` = -1) o en su argumento.
unsigned slotPlace(const BlockProgram& program, int target, int arg);
bool fits(const BlockDef& def, unsigned place);

// ------------------------------------------------------ como lo hace cada --

// Como lo realiza un motor (§17): un dato que ya lee, codigo generado, algo
// parecido con la diferencia escrita, o nada (un choque).
enum class Realization { Data, Script, Approx, None };
const char* realizationKey(Realization how);   // "dato", "script", "aprox.", "—"
struct BlockSupport {
    Realization how = Realization::None;
    const char* cite = "";      // archivo:linea en la fuente del motor
    const char* noteEn = "";    // la diferencia (aprox.) o el motivo (—)
    const char* noteEs = "";
};
BlockSupport blockSupport(Engine engine, const std::string& key);

// ---------------------------------------------------------------- codigo --

// Lo que pone la exportacion del aspecto en los mismos archivos.
struct TypeLook {
    std::string codenameSplash;   // `note.splash` (data/splashes/<x>.xml)
    std::string psychTexture;     // `texture` (images/<x>)
    std::string psychSplash;      // `noteSplashData.texture`
    bool psychSplashRgb = true;   // false: `noteSplashData.useRGBShader: false`
    std::string vsliceStyle;      // `noteStyleId` del NoteKind
};

struct BlockFile {
    std::string path;             // relativa a la raiz del mod
    std::string text;
};
struct BlockConflict {
    Severity severity = Severity::Info;
    std::string key;              // bloque implicado; vacio = el tipo entero
    std::string en, es;
    int node = -1;                // el bloque en el programa, para marcarlo
};
struct BlockCode {
    std::vector<BlockFile> files;
    std::vector<BlockConflict> conflicts;
};

// El codigo de un tipo en un motor. `fileName` es el tipo como nombre de
// archivo (Codename, Psych) o el id limpio del paquete (V-Slice). Los
// comentarios del codigo van en ingles.
BlockCode generateBlocks(Engine engine, const std::string& type, const std::string& fileName,
                         const BlockProgram& program, const TypeLook& look = {});

// Lo que los bloques le dicen a la vista previa.
bool blocksAvoid(const BlockProgram& program);
bool blocksHitMisses(const BlockProgram& program);

struct BlockAppearance {
    float alpha = 1.0f, scaleX = 1.0f, scaleY = 1.0f, angle = 0.0f;
};
BlockAppearance blocksAppearance(const BlockProgram& program);

// Cuantos bloques hacen algo (los que cuelgan de un evento).
int activeBlocks(const BlockProgram& program);
// Un bloque en una frase, con sus valores: «cambiar vida por (vida × 2)».
std::string blockText(const BlockProgram& program, int id, bool spanish);
// El programa en frases: «al crear: hay que evitarla», «cuando el jugador
// toca: si ... cambiar vida por 0.3».
std::vector<std::string> programSummary(const BlockProgram& program, bool spanish);

// ---------------------------------------------------------------- presets --

struct BlockPreset {
    const char* key;              // "hurt", "instakill", "heal"...
    const char* nameEn;
    const char* nameEs;
    const char* helpEn;
    const char* helpEs;
    void (*build)(BlockProgram& program);
    bool advanced = false;        // con condiciones, valores o varios eventos
};
const std::vector<BlockPreset>& blockPresets();
// Suma un preset: sus propiedades van bajo el «al crear» que ya haya (y
// sustituyen a las de la misma clave); sus eventos se juntan con los iguales.
void addPreset(BlockProgram& program, const BlockPreset& preset);

}  // namespace fml::notelab

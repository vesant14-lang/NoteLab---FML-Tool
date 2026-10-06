#include "NoteBlocks.hpp"
#include "NoteResources.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace fml::notelab {
namespace {

// Las claves de las propiedades (§17): van directamente bajo «al crear».
constexpr const char* kAvoid = "play.avoid";
constexpr const char* kCausesMiss = "hit.causesMiss";
constexpr const char* kHitHealth = "hit.health";
constexpr const char* kMissHealth = "miss.health";
constexpr const char* kNoSplash = "look.noSplash";
constexpr const char* kNoAnim = "hit.noAnim";
constexpr const char* kAnimSuffix = "hit.animSuffix";
constexpr const char* kSinger = "hit.singer";
constexpr const char* kNoMissAnim = "miss.noAnim";

constexpr const char* kCreate = "event.create";
constexpr const char* kHit = "event.hit";
constexpr const char* kMiss = "event.miss";

// ---------------------------------------------------------------- textos --

// Un numero con punto: el .txt de Psych lee con punto como Float y sin el
// como Int (NoteTypesConfig.hx:109-129).
std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(value));
    std::string out = text;
    if (out.find_first_of(".eEn") == std::string::npos) out += ".0";
    return out;
}

std::string integer(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%ld", static_cast<long>(std::lround(value)));
    return text;
}

std::string quoted(const std::string& text, char quote) {
    std::string out(1, quote);
    for (char c : text) {
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\r') { out += "\\r"; continue; }
        if (c == '\t') { out += "\\t"; continue; }
        if (c == quote || c == '\\') out += '\\';
        out += c;
    }
    out += quote;
    return out;
}

std::string hx(const std::string& text) { return quoted(text, '"'); }
std::string lua(const std::string& text) { return quoted(text, '\''); }

float parseNumber(const std::string& text, float fallback) {
    const char* begin = text.c_str();
    char* end = nullptr;
    const float value = std::strtof(begin, &end);
    if (end == begin || !std::isfinite(value)) return fallback;
    return value;
}

// Seis cifras hexadecimales en mayusculas; blanco si no lo son.
std::string hexColor(const std::string& text) {
    std::string out;
    for (char c : text)
        if (std::isxdigit(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (out.size() > 6) out = out.substr(out.size() - 6);
    return out.size() == 6 ? out : std::string("FFFFFF");
}

int directionIndex(const std::string& value) {
    if (value == "down") return 1;
    if (value == "up") return 2;
    if (value == "right") return 3;
    return 0;
}

// Un nombre de funcion o variable a partir de un texto: letras y cifras.
std::string identifier(const std::string& text) {
    std::string out;
    bool upper = true;
    for (char c : text) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            upper = true;
            continue;
        }
        out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return out;
}

void conflict(BlockCode& code, Severity severity, const std::string& key, int node, std::string en, std::string es) {
    for (const BlockConflict& existing : code.conflicts)
        if (existing.key == key && existing.node == node && existing.en == en) return;
    BlockConflict item;
    item.severity = severity;
    item.key = key;
    item.en = std::move(en);
    item.es = std::move(es);
    item.node = node;
    code.conflicts.push_back(std::move(item));
}

// -------------------------------------------------------------- catalogo --

ArgDef num(const char* def, float min = -1.0e6f, float max = 1.0e6f, bool plug = true) {
    ArgDef arg;
    arg.kind = ArgKind::Number;
    arg.def = def;
    arg.min = min;
    arg.max = max;
    arg.plug = plug;
    return arg;
}

ArgDef text(const char* def) {
    ArgDef arg;
    arg.kind = ArgKind::Text;
    arg.def = def;
    arg.plug = false;
    return arg;
}

ArgDef condition() {
    ArgDef arg;
    arg.kind = ArgKind::Boolean;
    return arg;
}

ArgDef choice(const char* def, std::vector<ArgChoice> choices) {
    ArgDef arg;
    arg.kind = ArgKind::Choice;
    arg.def = def;
    arg.choices = std::move(choices);
    arg.plug = false;
    return arg;
}

ArgDef color(const char* def) {
    ArgDef arg;
    arg.kind = ArgKind::Color;
    arg.def = def;
    arg.plug = false;
    return arg;
}

ArgDef sound() {
    ArgDef arg;
    arg.kind = ArgKind::Sound;
    arg.plug = false;
    return arg;
}

ArgDef media(ArgKind kind) {
    ArgDef arg;
    arg.kind = kind;
    arg.plug = false;
    return arg;
}

ArgDef mediaNumber(const char* value, float low, float high) {
    ArgDef arg = num(value, low, high);
    arg.plug = false;
    return arg;
}

ArgDef mediaFit(const char* value) {
    return choice(value, {{"contain", "fit inside", "encajar"}, {"cover", "cover screen", "cubrir pantalla"}, {"stretch", "stretch", "estirar"}});
}

ArgDef body() {
    ArgDef arg;
    arg.kind = ArgKind::Body;
    arg.plug = false;
    return arg;
}

ArgDef variable() {
    ArgDef arg;
    arg.kind = ArgKind::Variable;
    arg.plug = false;
    return arg;
}

const std::vector<ArgChoice>& whoChoices() {
    static const std::vector<ArgChoice> choices = {
        {"player", "the player", "el jugador"}, {"opponent", "the opponent", "el rival"}, {"any", "anyone", "cualquiera"}};
    return choices;
}

}  // namespace

const char* categoryName(BlockCategory category, bool spanish) {
    switch (category) {
        case BlockCategory::Events: return spanish ? "Eventos" : "Events";
        case BlockCategory::Control: return "Control";
        case BlockCategory::Operators: return spanish ? "Operadores" : "Operators";
        case BlockCategory::Sensing: return spanish ? "Sensores" : "Sensing";
        case BlockCategory::Properties: return spanish ? "Propiedades" : "Properties";
        case BlockCategory::Game: return spanish ? "Juego" : "Game";
        case BlockCategory::Animation: return spanish ? "Animación" : "Animation";
        case BlockCategory::Camera: return spanish ? "Cámara" : "Camera";
        case BlockCategory::Sound: return spanish ? "Sonido" : "Sound";
        case BlockCategory::Variables: return spanish ? "Variables" : "Variables";
    }
    return "?";
}

const std::vector<BlockDef>& blockDefs() {
    using C = BlockCategory;
    using S = BlockShape;
    constexpr unsigned run = kPlaceHit | kPlaceMiss;
    static const std::vector<ArgChoice> compare = {{"eq", "=", "="}, {"ne", "\xE2\x89\xA0", "\xE2\x89\xA0"}, {"lt", "<", "<"},
                                                   {"gt", ">", ">"}, {"le", "\xE2\x89\xA4", "\xE2\x89\xA4"}, {"ge", "\xE2\x89\xA5", "\xE2\x89\xA5"}};
    static const std::vector<ArgChoice> math = {{"add", "+", "+"}, {"sub", "\xE2\x88\x92", "\xE2\x88\x92"},
                                                {"mul", "\xC3\x97", "\xC3\x97"}, {"div", "\xC3\xB7", "\xC3\xB7"}};
    static const std::vector<ArgChoice> direction = {
        {"left", "left", "izquierda"}, {"down", "down", "abajo"}, {"up", "up", "arriba"}, {"right", "right", "derecha"}};
    static const std::vector<ArgChoice> rating = {{"sick", "sick", "sick"}, {"good", "good", "good"}, {"bad", "bad", "bad"}, {"shit", "shit", "shit"}};
    static const std::vector<ArgChoice> character = {{"bf", "BF", "BF"}, {"dad", "the opponent", "el rival"}, {"gf", "GF", "GF"}};
    static const std::vector<BlockDef> defs = {
        {"code.file", C::Control, S::Hat, "custom file {1} ({0})", "archivo propio {1} ({0})", "", "",
         "Explicit engine-specific file override. Not simulated; edit it in the Code view.",
         "Archivo específico del motor que reemplaza el generado. No se simula; edítalo en Código.",
         {choice("codename", {{"codename", "Codename", "Codename"}, {"psych", "Psych", "Psych"}, {"vslice", "V-Slice", "V-Slice"}}), text(""), text("")}, kPlaceTop},
        // Eventos.
        {kCreate, C::Events, S::Hat, "when {type} is created", "al crear {type}", "", "",
         "Its properties: what the note is from the moment the chart creates it. Only Properties blocks go here.",
         "Sus propiedades: lo que la nota es desde que el chart la crea. Aquí solo van bloques de Propiedades.", {}, kPlaceTop},
        {kHit, C::Events, S::Hat, "when {0} hits {type}", "cuando {0} toca {type}", "", "",
         "Runs once per note (not on sustain pieces), after the engine has handled the hit.",
         "Se ejecuta una vez por nota (no en los tramos del sostenido), después de que el motor procese el acierto.",
         {choice("player", whoChoices())}, kPlaceTop},
        {kMiss, C::Events, S::Hat, "when the player misses {type}", "cuando el jugador falla {type}", "", "",
         "The note passed without being hit. Only the player misses; with «must be avoided» it never runs.",
         "La nota pasó sin tocarla. Solo falla el jugador; con «hay que evitarla» no se ejecuta nunca.", {}, kPlaceTop},
        // Control.
        {"ctl.if", C::Control, S::If, "if {0} then", "si {0} entonces", "", "",
         "Runs the blocks inside only when the condition is true. An empty condition is false.",
         "Ejecuta lo de dentro solo si la condición se cumple. Una condición vacía no se cumple.", {condition(), body()}, run},
        {"ctl.ifElse", C::Control, S::IfElse, "if {0} then", "si {0} entonces", "else", "si no",
         "Runs the first part when the condition is true and the second one when it isn't.",
         "Ejecuta la primera parte si la condición se cumple y la segunda si no.", {condition(), body(), body()}, run},
        {"do.log", C::Control, S::Statement, "write {0} in the console", "escribir {0} en la consola", "", "",
         "A message to see what happens while testing: Codename and V-Slice print it in the console; Psych shows it on screen.",
         "Un mensaje para ver qué pasa al probar: Codename y V-Slice lo escriben en la consola; Psych lo enseña en pantalla.",
         {text("hi!")}, run},
        // Lo que no se pudo pasar a bloques de un script: va tal cual, en su sitio.
        {"code.line", C::Control, S::Statement, "code {1} ({0})", "código {1} ({0})", "", "",
         "A line of the original script that couldn't become a block. It goes as is, only in its engine; in the others it stays as a comment.",
         "Una línea del script original que no se pudo pasar a bloques. Va tal cual, solo en su motor; en los demás queda como comentario.",
         {choice("psych", {{"codename", "Codename", "Codename"}, {"psych", "Psych", "Psych"}, {"vslice", "V-Slice", "V-Slice"}}), text("")}, run},
        // Operadores.
        {"op.compare", C::Operators, S::Boolean, "{0} {1} {2}", "{0} {1} {2}", "", "", "Compares two numbers.", "Compara dos números.",
         {num("0"), choice("gt", compare), num("0")}, run},
        {"op.and", C::Operators, S::Boolean, "{0} and {1}", "{0} y {1}", "", "", "True when both are true.", "Se cumple si se cumplen las dos.",
         {condition(), condition()}, run},
        {"op.or", C::Operators, S::Boolean, "{0} or {1}", "{0} o {1}", "", "", "True when either one is true.",
         "Se cumple si se cumple una de las dos.", {condition(), condition()}, run},
        {"op.not", C::Operators, S::Boolean, "not {0}", "no {0}", "", "", "True when the condition isn't.", "Se cumple si la condición no.",
         {condition()}, run},
        {"op.chance", C::Operators, S::Boolean, "chance of {0} %", "probabilidad de {0} %", "", "",
         "True that percentage of the times, at random.", "Se cumple ese porcentaje de las veces, al azar.", {num("50", 0.0f, 100.0f)}, run},
        {"op.random", C::Operators, S::Number, "pick random {0} to {1}", "al azar entre {0} y {1}", "", "",
         "A whole number at random between the two, both included.", "Un número entero al azar entre los dos, incluidos.",
         {num("1"), num("10")}, run},
        {"op.math", C::Operators, S::Number, "{0} {1} {2}", "{0} {1} {2}", "", "", "Adds, subtracts, multiplies or divides.",
         "Suma, resta, multiplica o divide.", {num("0"), choice("add", math), num("0")}, run},
        // Sensores.
        {"is.direction", C::Sensing, S::Boolean, "direction is {0}", "la dirección es {0}", "", "", "The lane of the note.",
         "El carril de la nota.", {choice("left", direction)}, run},
        {"is.rating", C::Sensing, S::Boolean, "rating is {0}", "el juicio es {0}", "", "",
         "How well it was hit. Only when it's hit.", "Lo bien que se tocó. Solo al tocarla.", {choice("sick", rating)}, kPlaceHit},
        {"is.player", C::Sensing, S::Boolean, "the player hit it", "la tocó el jugador", "", "",
         "Tells the player from the opponent in «when anyone hits».", "Distingue al jugador del rival en «cuando cualquiera toca».", {},
         kPlaceHit},
        {"is.downscroll", C::Sensing, S::Boolean, "downscroll is on", "downscroll activado", "", "",
         "The player's downscroll option.", "La opción de downscroll del jugador.", {}, run},
        {"get.health", C::Sensing, S::Number, "health", "vida", "", "", "The health: 0 empty, 2 full.",
         "La vida: 0 vacía, 2 llena.", {}, run},
        {"get.combo", C::Sensing, S::Number, "combo", "combo", "", "", "The current combo.", "El combo de ahora.", {}, run},
        {"get.misses", C::Sensing, S::Number, "misses", "fallos", "", "", "The misses so far.", "Los fallos hasta ahora.", {}, run},
        {"get.score", C::Sensing, S::Number, "score", "puntos", "", "", "The score so far.", "Los puntos hasta ahora.", {}, run},
        {"get.songTime", C::Sensing, S::Number, "song time (s)", "tiempo de canción (s)", "", "",
         "Seconds since the song started.", "Segundos desde que empezó la canción.", {}, run},
        // Propiedades: datos que el motor ya lee, directamente bajo «al crear».
        {kAvoid, C::Properties, S::Statement, "must be avoided", "hay que evitarla", "", "",
         "The bot doesn't hit it and letting it pass isn't a miss.", "El bot no la toca y dejarla pasar no es un fallo.", {}, kPlaceCreate},
        {kCausesMiss, C::Properties, S::Statement, "hitting it is a miss", "tocarla es un fallo", "", "",
         "Breaks the combo, counts as a miss and plays the miss animation and sound.",
         "Rompe el combo, cuenta como fallo y hace la animación y el sonido de fallo.", {}, kPlaceCreate},
        {kHitHealth, C::Properties, S::Statement, "health when hit {0}", "vida al tocarla {0}", "", "",
         "The health it gives when hit, instead of the engine's; negative takes it. The whole bar is 2.",
         "La vida que da al tocarla, en lugar de la del motor; en negativo, la quita. La barra entera es 2.",
         {num("-0.2", -2.0f, 2.0f, false)}, kPlaceCreate},
        {kMissHealth, C::Properties, S::Statement, "health when missed {0}", "vida al fallarla {0}", "", "",
         "The health that letting it pass changes, instead of the engine's; negative takes it.",
         "La vida que cambia dejarla pasar, en lugar de la del motor; en negativo, la quita.", {num("-0.1", -2.0f, 2.0f, false)},
         kPlaceCreate},
        {kNoSplash, C::Properties, S::Statement, "no splash", "sin salpicadura", "", "", "Hitting it doesn't splash.",
         "Tocarla no saca salpicadura.", {}, kPlaceCreate},
        {kNoAnim, C::Properties, S::Statement, "no sing animation", "sin animación de canto", "", "",
         "The character doesn't sing when it's hit.", "El personaje no canta al tocarla.", {}, kPlaceCreate},
        {kAnimSuffix, C::Properties, S::Statement, "sings with suffix {0}", "canta con sufijo {0}", "", "",
         "Sings with this suffix (singLEFT-alt…).", "Canta con este sufijo (singLEFT-alt…).", {text("-alt")}, kPlaceCreate},
        {kSinger, C::Properties, S::Statement, "GF sings it", "la canta GF", "", "",
         "GF sings it instead of the character on its side.", "La canta GF en lugar del personaje de su lado.", {}, kPlaceCreate},
        {kNoMissAnim, C::Properties, S::Statement, "no miss animation", "sin animación de fallo", "", "",
         "Missing it doesn't play the miss animation.", "Fallarla no hace la animación de fallo.", {}, kPlaceCreate},
        // Juego.
        {"do.health", C::Game, S::Statement, "change health by {0}", "cambiar vida por {0}", "", "",
         "Adds health (negative takes it). The whole bar is 2.", "Suma vida (en negativo, la quita). La barra entera es 2.",
         {num("0.1", -2.0f, 2.0f)}, run},
        {"do.setHealth", C::Game, S::Statement, "set health to {0}", "fijar vida a {0}", "", "",
         "Sets the health: 0 empty, 2 full.", "Pone la vida: 0 vacía, 2 llena.", {num("1", 0.0f, 2.0f)}, run},
        {"do.drain", C::Game, S::Statement, "change health by {0} every {1} s, {2} times", "cambiar vida por {0} cada {1} s, {2} veces",
         "", "", "Poison or regeneration: changes the health again and again. Hitting another one starts over.",
         "Veneno o regeneración: cambia la vida una y otra vez. Tocar otra vuelve a empezar.",
         {num("-0.05", -2.0f, 2.0f, false), num("0.5", 0.05f, 60.0f, false), num("6", 1.0f, 100.0f, false)}, run},
        {"do.score", C::Game, S::Statement, "change score by {0}", "cambiar puntos por {0}", "", "", "Adds points (negative takes them).",
         "Suma puntos (en negativo, los quita).", {num("100")}, run},
        {"do.setScore", C::Game, S::Statement, "set score to {0}", "fijar puntos a {0}", "", "", "Sets the score.", "Pone los puntos.",
         {num("0")}, run},
        {"do.misses", C::Game, S::Statement, "change misses by {0}", "cambiar fallos por {0}", "", "", "Adds misses to the count.",
         "Suma fallos a la cuenta.", {num("1")}, run},
        {"do.combo", C::Game, S::Statement, "set combo to {0}", "fijar combo a {0}", "", "", "Sets the combo; 0 breaks it.",
         "Pone el combo; 0 lo rompe.", {num("0", 0.0f, 1.0e6f)}, run},
        // Animacion.
        {"do.hey", C::Animation, S::Statement, "do «Hey!»", "hacer «Hey!»", "", "",
         "The character on its side does «hey» and GF cheers, like Psych's built-in type.",
         "El personaje de su lado hace «hey» y GF anima, como el tipo de serie de Psych.", {}, run},
        {"do.anim", C::Animation, S::Statement, "{0} plays animation {1}", "{0} hace la animación {1}", "", "",
         "A character plays an animation by its name; if it doesn't have it, nothing happens.",
         "Un personaje hace una animación por su nombre; si no la tiene, no pasa nada.", {choice("bf", character), text("hey")}, run},
        // Camara.
        {"do.flash", C::Camera, S::Statement, "flash {0} for {1} s", "destello {0} durante {1} s", "", "",
         "The game camera flashes that color and fades.", "La cámara del juego se ilumina de ese color y se desvanece.",
         {color("FFFFFF"), num("0.3", 0.0f, 10.0f)}, run},
        {"do.shake", C::Camera, S::Statement, "shake {0} for {1} s", "temblor de {0} durante {1} s", "", "",
         "The game camera shakes; 0.01 is soft, 0.05 is strong.", "La cámara del juego tiembla; 0.01 es suave, 0.05 es fuerte.",
         {num("0.02", 0.0f, 1.0f), num("0.2", 0.0f, 10.0f)}, run},
        {"do.zoom", C::Camera, S::Statement, "zoom bump {0}", "golpe de zoom {0}", "", "",
         "The camera zooms in and goes back, like on the beat (the HUD twice as much).",
         "La cámara se acerca y vuelve, como en el ritmo (el HUD, el doble).", {num("0.015", -1.0f, 1.0f)}, run},
        {"do.hideHud", C::Camera, S::Statement, "hide the HUD for {0} s", "ocultar el HUD durante {0} s", "", "",
         "Hides the HUD camera (arrows, notes and score) and shows it again.",
         "Oculta la cámara del HUD (flechas, notas y marcador) y la vuelve a enseñar.", {num("1", 0.05f, 60.0f)}, run},
        // Sonido.
        {"do.sound", C::Sound, S::Statement, "play sound {0} volume {1}", "tocar sonido {0} volumen {1}", "", "",
         "A sound from the mod's sounds/ folder.", "Un sonido de la carpeta sounds/ del mod.", {sound(), num("1", 0.0f, 1.0f)}, run},
        {"do.image", C::Camera, S::Statement, "show image {0} for {1} s opacity {2} {3} cooldown {4} s", "mostrar imagen {0} por {1} s opacidad {2} {3} espera {4} s", "", "",
         "Show a PNG from the mod on the HUD. Reuses one sprite and timer per block; cooldown prevents repeated note hits from stacking media.",
         "Muestra un PNG del mod en el HUD. Reutiliza un sprite y temporizador por bloque; la espera evita acumular recursos.",
         {media(ArgKind::Image), mediaNumber("1", 0.05f, 30), mediaNumber("1", 0, 1), mediaFit("contain"), mediaNumber("0.25", 0.05f, 30)}, run},
        {"do.screamer", C::Camera, S::Statement, "screamer image {0} sound {1} for {2} s volume {3} cooldown {4} s {5}", "screamer imagen {0} sonido {1} por {2} s volumen {3} espera {4} s {5}", "", "",
         "Show a mod PNG and play its OGG together. Duration, fit, volume and retrigger cooldown are explicit. Preview only starts when requested.",
         "Muestra un PNG del mod y toca su OGG juntos. Duración, ajuste, volumen y espera son explícitos. La preview sólo inicia al pedirla.",
         {media(ArgKind::Image), sound(), mediaNumber("1", 0.05f, 30), mediaNumber("1", 0, 1), mediaNumber("1", 0.05f, 30), mediaFit("cover")}, run},
        {"do.video", C::Camera, S::Statement, "play video {0} max {1} s volume {2} {3} cooldown {4} s", "reproducir video {0} máximo {1} s volumen {2} {3} espera {4} s", "", "",
         "Play a mod MP4 overlay on video-enabled engine builds. Native preview uses your system player; codec and scripting restrictions are reported.",
         "Reproduce un MP4 del mod como overlay en builds con video. La preview usa el reproductor del sistema; se informan restricciones de codecs y scripts.",
         {media(ArgKind::Video), mediaNumber("5", 0.05f, 30), mediaNumber("1", 0, 1), mediaFit("contain"), mediaNumber("1", 0.05f, 30)}, run},
        {"is.sustain", C::Sensing, S::Boolean, "this note has a sustain", "esta nota tiene sostenido", "", "",
         "Checks the whole note's duration, not whether this is a sustain piece.", "Comprueba la duración completa, no si este sprite es un tramo.", {}, run},
        {"get.noteTime", C::Sensing, S::Number, "this note's time (s)", "tiempo de esta nota (s)", "", "",
         "Scheduled time of the note, in song seconds.", "Tiempo previsto de la nota, en segundos de canción.", {}, run},
        {"get.sustainLength", C::Sensing, S::Number, "sustain duration (s)", "duración del sostenido (s)", "", "",
         "Full sustain duration; zero for a tap.", "Duración completa; cero para una nota corta.", {}, run},
        {"get.noteLane", C::Sensing, S::Number, "this note's lane", "carril de esta nota", "", "",
         "Zero-based logical lane index.", "Índice lógico del carril, desde cero.", {}, run},
        {"get.strumlineIndex", C::Sensing, S::Number, "this note's strumline", "strumline de esta nota", "", "",
         "Codename's real line; Psych and V-Slice use 0 opponent, 1 player.", "Línea real de Codename; Psych y V-Slice usan 0 rival y 1 jugador.", {}, run},
        {"get.hitOffset", C::Sensing, S::Number, "hit offset (ms)", "desfase del acierto (ms)", "", "",
         "Negative early, positive late. V-Slice uses precise input; other engines use the callback clock.",
         "Negativo temprano, positivo tarde. V-Slice usa la entrada precisa; los otros motores, el reloj del callback.", {}, kPlaceHit},
        {"get.bpm", C::Sensing, S::Number, "current BPM", "BPM actual", "", "",
         "The conductor's current tempo, including BPM changes.", "Tempo actual del conductor, incluidos sus cambios.", {}, run},
        {"get.beat", C::Sensing, S::Number, "current beat", "beat actual", "", "",
         "Current whole beat, starting at zero.", "Beat entero actual, desde cero.", {}, run},
        {"var.define", C::Variables, S::Hat, "define number {0} = {1}", "definir número {0} = {1}", "", "",
         "A numeric variable owned by this note type. Reset on initialization or detected clock jumps; not per note.",
         "Variable numérica de este tipo. Se reinicia al inicializar o detectar saltos del reloj; no por cada nota.", {text("counter"), num("0", -1.0e6f, 1.0e6f, false)}, kPlaceTop},
        {"var.get", C::Variables, S::Number, "value of {0}", "valor de {0}", "", "",
         "Reads the selected numeric variable. References survive renaming.", "Lee la variable numérica elegida. La referencia sobrevive al renombrarla.", {variable()}, run},
        {"var.set", C::Variables, S::Statement, "set {0} to {1}", "fijar {0} a {1}", "", "",
         "Sets this type's numeric variable.", "Fija la variable numérica de este tipo.", {variable(), num("0")}, run},
        {"var.change", C::Variables, S::Statement, "change {0} by {1}", "cambiar {0} por {1}", "", "",
         "Adds to a numeric variable, bounded to a finite value.", "Suma a una variable numérica, limitada a un valor finito.", {variable(), num("1")}, run},
        {"op.clamp", C::Operators, S::Number, "limit {0} between {1} and {2}", "limitar {0} entre {1} y {2}", "", "",
         "Clamps a number; inverted bounds are swapped.", "Limita un número; ordena los límites invertidos.", {num("0"), num("0"), num("1")}, run},
        {"op.remap", C::Operators, S::Number, "map {0} from {1}..{2} to {3}..{4}", "mapear {0} de {1}..{2} a {3}..{4}", "", "",
         "Maps ranges without clamping. A zero input range returns the output minimum.",
         "Convierte rangos sin limitar. Un rango de entrada cero devuelve el mínimo de salida.", {num("0"), num("0"), num("1"), num("0"), num("100")}, run},
        {"look.alpha", C::Properties, S::Statement, "note opacity {0}", "opacidad de nota {0}", "", "",
         "Relative opacity, 0 invisible to 1 native. Applied without accumulating.", "Opacidad relativa, 0 invisible y 1 nativa. No se acumula.", {num("1", 0, 1, false)}, kPlaceCreate},
        {"look.scale", C::Properties, S::Statement, "note scale X {0} Y {1}", "escala de nota X {0} Y {1}", "", "",
         "Relative head scale; sustain length remains controlled by the engine.", "Escala relativa de la cabeza; la longitud del sostenido sigue al motor.", {num("1", 0.1f, 4, false), num("1", 0.1f, 4, false)}, kPlaceCreate},
        {"look.angle", C::Properties, S::Statement, "note rotation {0} degrees", "giro de nota {0} grados", "", "",
         "Relative head rotation, not the note's travel direction.", "Giro relativo de la cabeza, no de su trayectoria.", {num("0", -360, 360, false)}, kPlaceCreate},
        {"ctl.repeat", C::Control, S::If, "repeat {0} times", "repetir {0} veces", "", "",
         "A finite loop, at most 64 iterations and 512 actions per event, including nested loops.",
         "Bucle finito: hasta 64 iteraciones y 512 acciones por evento, incluidos bucles anidados.", {num("3", 0, 64), body()}, run},
        {"ctl.after", C::Control, S::If, "after {0} s as {2}", "después de {0} s como {2}", "", "",
         "Schedules a body on the song clock. The same name replaces its task; detected clock jumps cancel tasks.",
         "Programa un cuerpo con el reloj de canción. El mismo nombre reemplaza su tarea; los saltos detectados cancelan pendientes.", {num("0.5", 0, 60), body(), text("effect")}, run},
        {"ctl.cancel", C::Control, S::Statement, "cancel delayed {0}", "cancelar diferido {0}", "", "",
         "Cancels only the named delayed task of this note type.", "Cancela sólo el diferido con ese nombre de este tipo.", {text("effect")}, run},
    };
    return defs;
}

const BlockDef* blockDef(const std::string& key) {
    for (const BlockDef& def : blockDefs())
        if (key == def.key) return &def;
    return nullptr;
}

const char* choiceLabel(const ArgDef& arg, const std::string& value, bool spanish) {
    for (const ArgChoice& item : arg.choices)
        if (value == item.value) return spanish ? item.es : item.en;
    return arg.choices.empty() ? "" : (spanish ? arg.choices.front().es : arg.choices.front().en);
}

std::string blockName(const std::string& key, bool spanish) {
    if (key == kCreate) return spanish ? "al crear" : "when created";
    if (key == kHit) return spanish ? "al tocarla" : "when hit";
    if (key == kMiss) return spanish ? "al fallarla" : "when missed";
    if (key == "op.compare") return spanish ? "comparar" : "compare";
    if (key == "op.math") return spanish ? "operación" : "math";
    const BlockDef* def = blockDef(key);
    if (!def) return key;
    const std::string pattern = spanish ? def->textEs : def->textEn;
    std::string out;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const size_t close = pattern.find('}', i);
            if (close != std::string::npos) {
                i = close;
                continue;
            }
        }
        if (pattern[i] == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += pattern[i];
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// -------------------------------------------------------------- programa --

BlockNode* BlockProgram::node(int id) {
    const auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

const BlockNode* BlockProgram::node(int id) const {
    const auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

int newBlock(BlockProgram& program, const std::string& key) {
    BlockNode node;
    node.key = key;
    if (const BlockDef* def = blockDef(key))
        for (const ArgDef& arg : def->args) {
            std::string initial = arg.def;
            if (arg.kind == ArgKind::Variable) {
                const auto vars = blockVariables(program);
                if (!vars.empty()) initial = std::to_string(vars.front().id);
            }
            node.args.push_back({std::move(initial), -1});
        }
    const int id = program.nextId++;
    program.nodes[id] = std::move(node);
    return id;
}

std::vector<BlockVariable> blockVariables(const BlockProgram& program) {
    std::vector<BlockVariable> vars;
    for (int id : program.tops) {
        const auto* n = program.node(id);
        if (!n || n->key != "var.define" || n->args.size() != 2) continue;
        vars.push_back({id, n->args[0].value, std::clamp(static_cast<double>(parseNumber(n->args[1].value, 0)), -1.0e6, 1.0e6)});
    }
    return vars;
}

const BlockNode* referencedVariable(const BlockProgram& program, const std::string& reference) {
    if (reference.empty() || reference.front() == '0' || reference.size() > 10 || reference.find_first_not_of("0123456789") != std::string::npos) return nullptr;
    const long long id = std::strtoll(reference.c_str(), nullptr, 10);
    if (id <= 0 || id > 2147483646 || std::find(program.tops.begin(), program.tops.end(), static_cast<int>(id)) == program.tops.end()) return nullptr;
    const auto* n = program.node(static_cast<int>(id));
    return n && n->key == "var.define" && n->args.size() == 2 ? n : nullptr;
}

BlockLink linkOf(const BlockProgram& program, int id) {
    for (const auto& [parent, node] : program.nodes) {
        if (node.next == id) return {parent, -1};
        for (size_t i = 0; i < node.args.size(); ++i)
            if (node.args[i].block == id) return {parent, static_cast<int>(i)};
    }
    return {};
}

int rootOf(const BlockProgram& program, int id) {
    int current = id;
    for (size_t guard = 0; guard <= program.nodes.size(); ++guard) {
        const BlockLink link = linkOf(program, current);
        if (link.parent < 0) return current;
        current = link.parent;
    }
    return current;
}

int lastOf(const BlockProgram& program, int id) {
    int current = id;
    for (size_t guard = 0; guard <= program.nodes.size(); ++guard) {
        const BlockNode* node = program.node(current);
        if (!node || node->next < 0) return current;
        current = node->next;
    }
    return current;
}

std::vector<int> chainOf(const BlockProgram& program, int first) {
    std::vector<int> chain;
    for (int current = first; current >= 0 && chain.size() <= program.nodes.size();) {
        const BlockNode* node = program.node(current);
        if (!node) break;
        chain.push_back(current);
        current = node->next;
    }
    return chain;
}

void detach(BlockProgram& program, int id) {
    const BlockLink link = linkOf(program, id);
    if (link.parent >= 0) {
        BlockNode& parent = *program.node(link.parent);
        if (link.arg < 0) parent.next = -1;
        else parent.args[static_cast<size_t>(link.arg)].block = -1;
    }
    program.tops.erase(std::remove(program.tops.begin(), program.tops.end(), id), program.tops.end());
}

void placeTop(BlockProgram& program, int id, float x, float y) {
    BlockNode* node = program.node(id);
    if (!node) return;
    detach(program, id);
    node->x = x;
    node->y = y;
    program.tops.push_back(id);
}

void attachAfter(BlockProgram& program, int target, int id) {
    if (!program.node(target) || !program.node(id) || target == id) return;
    detach(program, id);
    BlockNode& before = *program.node(target);
    program.node(lastOf(program, id))->next = before.next;
    before.next = id;
}

void attachBody(BlockProgram& program, int target, int arg, int id) {
    BlockNode* parent = program.node(target);
    if (!parent || !program.node(id) || arg < 0 || arg >= static_cast<int>(parent->args.size())) return;
    detach(program, id);
    program.node(lastOf(program, id))->next = parent->args[static_cast<size_t>(arg)].block;
    parent->args[static_cast<size_t>(arg)].block = id;
}

void attachValue(BlockProgram& program, int target, int arg, int id, float bumpX, float bumpY) {
    BlockNode* parent = program.node(target);
    if (!parent || !program.node(id) || arg < 0 || arg >= static_cast<int>(parent->args.size())) return;
    detach(program, id);
    const int old = parent->args[static_cast<size_t>(arg)].block;
    parent->args[static_cast<size_t>(arg)].block = id;
    if (old >= 0) {
        BlockNode* displaced = program.node(old);
        displaced->x = bumpX;
        displaced->y = bumpY;
        program.tops.push_back(old);
    }
}

void attachAbove(BlockProgram& program, int top, int id, float x, float y) {
    if (!program.node(top) || !program.node(id) || top == id) return;
    detach(program, id);
    program.tops.erase(std::remove(program.tops.begin(), program.tops.end(), top), program.tops.end());
    program.node(lastOf(program, id))->next = top;
    BlockNode& head = *program.node(id);
    head.x = x;
    head.y = y;
    program.tops.push_back(id);
}

namespace {

void eraseChain(BlockProgram& program, int first);

// El bloque y lo que lleva dentro (valores y cuerpos), sin los de debajo.
void eraseTree(BlockProgram& program, int id) {
    BlockNode* node = program.node(id);
    if (!node) return;
    const std::vector<BlockArg> args = node->args;
    program.nodes.erase(id);
    for (const BlockArg& arg : args)
        if (arg.block >= 0) eraseChain(program, arg.block);
}

void eraseChain(BlockProgram& program, int first) {
    for (int current = first; current >= 0;) {
        const BlockNode* node = program.node(current);
        if (!node) break;
        const int next = node->next;
        eraseTree(program, current);
        current = next;
    }
}

int copyChain(BlockProgram& to, const BlockProgram& from, int first);

int copyTree(BlockProgram& to, const BlockProgram& from, int id) {
    const BlockNode* source = from.node(id);
    if (!source) return -1;
    BlockNode copy = *source;
    copy.next = -1;
    for (BlockArg& arg : copy.args)
        if (arg.block >= 0) arg.block = copyChain(to, from, arg.block);
    const int fresh = to.nextId++;
    to.nodes[fresh] = std::move(copy);
    return fresh;
}

int copyChain(BlockProgram& to, const BlockProgram& from, int first) {
    int head = -1, previous = -1;
    for (int current : chainOf(from, first)) {
        const int fresh = copyTree(to, from, current);
        if (fresh < 0) break;
        if (previous >= 0) to.node(previous)->next = fresh;
        else head = fresh;
        previous = fresh;
    }
    return head;
}

}  // namespace

void removeStack(BlockProgram& program, int id) {
    if (!program.node(id)) return;
    detach(program, id);
    eraseChain(program, id);
}

void removeBlock(BlockProgram& program, int id) {
    BlockNode* node = program.node(id);
    if (!node) return;
    const BlockLink link = linkOf(program, id);
    const int next = node->next;
    const float x = node->x, y = node->y;
    node->next = -1;
    if (link.parent >= 0) {
        BlockNode& parent = *program.node(link.parent);
        if (link.arg < 0) parent.next = next;
        else parent.args[static_cast<size_t>(link.arg)].block = next;
    } else {
        const auto at = std::find(program.tops.begin(), program.tops.end(), id);
        if (at != program.tops.end()) {
            if (next >= 0) {
                *at = next;
                program.node(next)->x = x;
                program.node(next)->y = y;
            } else {
                program.tops.erase(at);
            }
        }
    }
    eraseTree(program, id);
}

int duplicateStack(BlockProgram& program, int id) { return copyChain(program, program, id); }
int duplicateBlock(BlockProgram& program, int id) { return copyTree(program, program, id); }
int copyStackFrom(BlockProgram& program, const BlockProgram& from, int id) {
    if (&program == &from) return duplicateStack(program, id);
    std::set<int> before;
    for (const auto& entry : program.nodes) before.insert(entry.first);
    const int copy = copyChain(program, from, id);
    std::map<std::string, std::string> references;
    std::vector<int> made;
    for (const auto& entry : program.nodes) if (!before.count(entry.first)) made.push_back(entry.first);
    if (from.node(id) && from.node(id)->key == "var.define") references[std::to_string(id)] = std::to_string(copy);
    for (int fresh : made) {
        BlockNode& node = *program.node(fresh);
        const BlockDef* def = blockDef(node.key);
        if (!def) continue;
        for (size_t i = 0; i < node.args.size(); ++i) if (def->args[i].kind == ArgKind::Variable) {
            const std::string original = node.args[i].value;
            if (!referencedVariable(from, original)) { node.args[i].value.clear(); continue; }
            auto found = references.find(original);
            if (found == references.end()) {
                const int declaration = copyTree(program, from, std::stoi(original));
                placeTop(program, declaration, 24.0f, 24.0f + static_cast<float>(program.tops.size()) * 64.0f);
                found = references.emplace(original, std::to_string(declaration)).first;
            }
            node.args[i].value = found->second;
        }
    }
    return copy;
}

void repairProgram(BlockProgram& program) {
    auto statementLike = [](const BlockDef* def) {
        return def && (def->shape == BlockShape::Statement || def->shape == BlockShape::If || def->shape == BlockShape::IfElse);
    };
    auto valueLike = [](const BlockDef* def) { return def && (def->shape == BlockShape::Number || def->shape == BlockShape::Boolean); };
    // Bloques conocidos, con sus argumentos completos.
    for (auto it = program.nodes.begin(); it != program.nodes.end();) {
        const BlockDef* def = blockDef(it->second.key);
        if (!def) {
            it = program.nodes.erase(it);
            continue;
        }
        std::vector<BlockArg>& args = it->second.args;
        args.resize(def->args.size());
        for (size_t i = 0; i < args.size(); ++i)
            if (args[i].value.empty() && def->args[i].kind != ArgKind::Text && def->args[i].kind != ArgKind::Sound) args[i].value = def->args[i].def;
        // Los eventos y las instrucciones tienen bloques debajo; los valores no.
        if (!statementLike(def) && def->shape != BlockShape::Hat) it->second.next = -1;
        if (it->second.key == "var.define") it->second.next = -1;
        ++it;
    }
    // Enlaces: a bloques que existen, del tipo que cabe, y un solo padre por bloque.
    std::set<int> owned;
    for (auto& [id, node] : program.nodes) {
        const BlockDef* def = blockDef(node.key);
        auto keep = [&](int& target, bool wantStatement) {
            if (target < 0) return;
            const BlockNode* child = program.node(target);
            const BlockDef* childDef = child ? blockDef(child->key) : nullptr;
            const bool ok = child && target != id && !owned.count(target) && (wantStatement ? statementLike(childDef) : valueLike(childDef));
            if (!ok) target = -1;
            else owned.insert(target);
        };
        keep(node.next, true);
        for (size_t i = 0; i < node.args.size(); ++i) {
            const ArgDef& arg = def->args[i];
            if (arg.kind == ArgKind::Body) keep(node.args[i].block, true);
            else if (arg.plug) {
                const BlockNode* child = program.node(node.args[i].block);
                const BlockDef* childDef = child ? blockDef(child->key) : nullptr;
                if (childDef && ((arg.kind == ArgKind::Boolean && childDef->shape != BlockShape::Boolean) ||
                    (arg.kind == ArgKind::Number && childDef->shape != BlockShape::Number))) node.args[i].block = -1;
                else keep(node.args[i].block, false);
            }
            else node.args[i].block = -1;
        }
    }
    // Las pilas sueltas: bloques sin padre, una vez cada una.
    std::vector<int> tops;
    for (int top : program.tops)
        if (program.node(top) && !owned.count(top) && std::find(tops.begin(), tops.end(), top) == tops.end()) tops.push_back(top);
    program.tops = std::move(tops);
    // Lo que no se alcanza desde una pila suelta (sin padre, o en un ciclo) sale a la vista.
    for (size_t guard = 0; guard <= program.nodes.size(); ++guard) {
        std::set<int> seen;
        std::vector<int> pending(program.tops.begin(), program.tops.end());
        while (!pending.empty()) {
            const int id = pending.back();
            pending.pop_back();
            if (!seen.insert(id).second) continue;
            const BlockNode* node = program.node(id);
            if (!node) continue;
            if (node->next >= 0) pending.push_back(node->next);
            for (const BlockArg& arg : node->args)
                if (arg.block >= 0) pending.push_back(arg.block);
        }
        int lost = -1;
        for (const auto& entry : program.nodes)
            if (!seen.count(entry.first)) {
                lost = entry.first;
                break;
            }
        if (lost < 0) break;
        const BlockLink link = linkOf(program, lost);
        if (link.parent >= 0) {
            BlockNode& parent = *program.node(link.parent);
            if (link.arg < 0) parent.next = -1;
            else parent.args[static_cast<size_t>(link.arg)].block = -1;
        }
        BlockNode& node = *program.node(lost);
        node.x = 24.0f;
        node.y = 24.0f + 60.0f * static_cast<float>(program.tops.size());
        program.tops.push_back(lost);
    }
    int highest = 0;
    for (const auto& entry : program.nodes) highest = std::max(highest, entry.first);
    program.nextId = std::max(program.nextId, highest + 1);
}

unsigned placeOf(const BlockProgram& program, int id) {
    bool direct = true;   // cuelga de la cadena del evento, no de un «si» ni de una ranura
    int current = id;
    for (size_t guard = 0; guard <= program.nodes.size(); ++guard) {
        const BlockLink link = linkOf(program, current);
        if (link.parent < 0) break;
        if (link.arg >= 0) direct = false;
        current = link.parent;
    }
    if (current == id) {
        const BlockDef* def = program.node(id) ? blockDef(program.node(id)->key) : nullptr;
        return def && def->shape == BlockShape::Hat ? kPlaceTop : 0u;
    }
    const BlockNode* root = program.node(current);
    if (!root) return 0;
    if (root->key == kCreate) return direct ? kPlaceCreate : 0u;
    if (root->key == kHit) return kPlaceHit;
    if (root->key == kMiss) return kPlaceMiss;
    return 0;
}

unsigned slotPlace(const BlockProgram& program, int target, int arg) {
    const BlockNode* node = program.node(target);
    if (!node) return 0;
    const int root = rootOf(program, target);
    const BlockNode* head = program.node(root);
    if (!head) return 0;
    if (head->key == kCreate) {
        // Solo la cadena directa de «al crear»: tras el sombrero o tras una propiedad suya.
        if (arg >= 0) return 0;
        if (target == root) return kPlaceCreate;
        return placeOf(program, target) == kPlaceCreate ? kPlaceCreate : 0u;
    }
    if (head->key == kHit) return kPlaceHit;
    if (head->key == kMiss) return kPlaceMiss;
    return 0;
}

bool fits(const BlockDef& def, unsigned place) { return (def.places & place) != 0; }

// ------------------------------------------------------ como lo hace cada --

const char* realizationKey(Realization how) {
    switch (how) {
        case Realization::Data: return "dato";
        case Realization::Script: return "script";
        case Realization::Approx: return "aprox.";
        case Realization::None: return "\xE2\x80\x94";
    }
    return "?";
}

BlockSupport blockSupport(Engine engine, const std::string& key) {
    if (key == "code.line") return {Realization::Script, "NoteCode.cpp (a line of the original script, verbatim)",
        "Only in the engine it was written for; not executed in the preview.",
        "Solo en el motor para el que se escribió; no se ejecuta en la vista previa."};
    if (key == "code.file") return {Realization::Script, "NoteCode.cpp (explicit engine-file override)",
        "User code is preserved, not executed in the preview or checked by the engine compiler.",
        "El código propio se conserva; no se ejecuta en la preview ni se comprueba con el compilador del motor."};
    struct Row { const char* key; BlockSupport codename, psych, vslice; };
    constexpr Realization D = Realization::Data, S = Realization::Script, A = Realization::Approx, N = Realization::None;
    if (key.rfind("var.", 0) == 0 || key == "op.clamp" || key == "op.remap" || key == "ctl.repeat")
        return {S, engine == Engine::Psych ? "Lua + Note Lab numeric/budget helpers" : "HScript + Note Lab numeric/budget helpers", "", ""};
    if (key == "ctl.after" || key == "ctl.cancel") return {S,
        engine == Engine::Codename ? "PlayState.hx:1411 (update), Conductor.songPosition" : engine == Engine::Psych ? "PlayState.hx (onUpdate), getSongPosition" : "NoteKind.hx:103 (onUpdate), Conductor.songPosition",
        "", ""};
    if (key == "get.bpm") return {D, engine == Engine::Psych ? "FunkinLua.hx:91 (curBpm)" : "Conductor.hx (bpm)", "", ""};
    if (key == "get.beat") return {D, engine == Engine::Codename ? "Conductor.hx:161 (curBeat)" : engine == Engine::Psych ? "FunkinLua.hx:127 (curBeat)" : "Conductor.hx:226 (currentBeat)", "", ""};
    if (key == "get.strumlineIndex") return engine == Engine::Codename ? BlockSupport{D, "Note.strumLine, PlayState.strumLines.members", "", ""}
        : BlockSupport{A, "goodNoteHit/opponentNoteHit; NoteData.getMustHitNote()", "Two sides only: 0 opponent, 1 player. Extra-line forks are not inferred.", "Sólo dos lados: 0 rival y 1 jugador. No se infieren líneas extra de forks."};
    if (key == "get.hitOffset") return engine == Engine::VSlice ? BlockSupport{D, "ScriptEvent.hx:153 (hitDiff), PlayState.hx:3115", "", ""}
        : BlockSupport{A, "Conductor.songPosition - Note.strumTime", "Signed callback-clock offset, not precise input latency or rating-offset compensation.", "Desfase firmado del reloj del callback, sin compensación precisa de entrada ni rating offset."};
    if (key == "is.sustain" || key == "get.noteTime" || key == "get.sustainLength" || key == "get.noteLane")
        return {D, engine == Engine::VSlice ? "SongData.hx:1053,1075,1129 (time,length,getDirection)" : "Note.hx (strumTime,sustainLength); event direction", "", ""};
    if (key == "look.alpha") return {S, engine == Engine::Codename ? "Note.hx:217 (onPostNoteCreation, alpha)" : engine == Engine::Psych ? "PlayState.onSpawnNote, Note.hx:522 (multAlpha)" : "NoteKind.hx:107 (onNoteIncoming), NoteSprite.alpha", "", ""};
    if (key == "look.scale" || key == "look.angle") return {A,
        engine == Engine::Codename ? "Note.hx:217; Strum.hx:185, PlayState.postUpdate; Note.scale" : engine == Engine::Psych ? "PlayState.onSpawnNote; Note.offsetAngle, scale" : "NoteKind.onNoteIncoming; NoteSprite.scale, angle",
        "Relative head transform; sustain duration remains native. Later modchart/strumline transforms may override it.",
        "Transformación relativa de cabeza; la duración del sostenido sigue nativa. Otro modchart puede sobrescribirla."};
    static const char* vsliceBeforeEn = "V-Slice tells the note type before applying the hit: it doesn't include this hit yet (PlayState.hx:3142-3160).";
    static const char* vsliceBeforeEs = "V-Slice avisa al tipo antes de aplicar el acierto: todavía no incluye este (PlayState.hx:3142-3160).";
    static const Row rows[] = {
        {kCreate, {S, "PlayState.hx:797-806 (onNoteCreation)", "", ""}, {D, "NoteTypesConfig.hx (custom_notetypes/<type>.txt)", "", ""},
         {D, "NoteKind.hx:48", "", ""}},
        {kHit, {S, "PlayState.hx:2055 (onPostNoteHit)", "", ""}, {S, "PlayState.hx:3120 (goodNoteHit), :3017 (opponentNoteHit)", "", ""},
         {S, "PlayState.hx:1513-1525, :3142 (onNoteHit)", "", ""}},
        {kMiss, {S, "PlayState.hx:1942 (onPostPlayerMiss)", "", ""}, {S, "PlayState.hx:2880 (noteMiss)", "", ""},
         {S, "PlayState.hx:2903 (onNoteMiss)", "", ""}},
        {"ctl.if", {S, "HScript", "", ""}, {S, "Lua", "", ""}, {S, "HScript", "", ""}},
        {"ctl.ifElse", {S, "HScript", "", ""}, {S, "Lua", "", ""}, {S, "HScript", "", ""}},
        {"do.log", {S, "trace", "", ""}, {S, "FunkinLua.hx:1550 (debugPrint)", "", ""}, {S, "trace", "", ""}},
        {"op.compare", {S, "HScript", "", ""}, {S, "Lua", "", ""}, {S, "HScript", "", ""}},
        {"op.and", {S, "HScript", "", ""}, {S, "Lua", "", ""}, {S, "HScript", "", ""}},
        {"op.or", {S, "HScript", "", ""}, {S, "Lua", "", ""}, {S, "HScript", "", ""}},
        {"op.not", {S, "HScript", "", ""}, {S, "Lua", "", ""}, {S, "HScript", "", ""}},
        {"op.chance", {S, "FlxG.random.bool (Script.hx:58)", "", ""}, {S, "ExtraFunctions.hx:246 (getRandomBool)", "", ""},
         {S, "FlxG.random.bool", "", ""}},
        {"op.random", {S, "FlxG.random.int (Script.hx:58)", "", ""}, {S, "ExtraFunctions.hx:226 (getRandomInt)", "", ""},
         {S, "FlxG.random.int", "", ""}},
        {"op.math", {S, "HScript", "", ""}, {S, "Lua", "", ""}, {S, "HScript", "", ""}},
        {"is.direction", {D, "NoteHitEvent.hx:77, NoteMissEvent.hx:59 (direction)", "", ""},
         {D, "PlayState.hx:3120, :2880 (direction)", "", ""}, {D, "NoteData.getDirection()", "", ""}},
        {"is.rating", {D, "NoteHitEvent.hx:93 (rating), RatingManager.hx:56-59", "", ""}, {D, "Note.hx:126 (rating), Rating.hx:31-45", "", ""},
         {D, "ScriptEvent.hx:138 (judgement), PlayState.hx:3125-3138", "", ""}},
        {"is.player", {D, "NoteHitEvent.hx:57 (player)", "", ""}, {S, "goodNoteHit / opponentNoteHit", "", ""},
         {D, "NoteData.getMustHitNote()", "", ""}},
        {"is.downscroll", {D, "PlayState.hx:150 (downscroll)", "", ""}, {D, "FunkinLua.hx:184 (downscroll)", "", ""},
         {D, "Preferences.hx:99 (downscroll)", "", ""}},
        {"get.health", {D, "PlayState.hx:266 (health)", "", ""}, {D, "FunkinLua.hx:698 (getHealth)", "", ""},
         {A, "PlayState.hx:237 (health)", vsliceBeforeEn, vsliceBeforeEs}},
        {"get.combo", {D, "PlayState.hx:275 (combo)", "", ""}, {D, "PlayState.hx:177 (combo)", "", ""},
         {A, "Highscore.hx:13, :73 (tallies.combo)", vsliceBeforeEn, vsliceBeforeEs}},
        {"get.misses", {D, "PlayState.hx:328 (misses)", "", ""}, {D, "PlayState.hx:214 (songMisses)", "", ""},
         {A, "Highscore.hx:78 (tallies.missed)", vsliceBeforeEn, vsliceBeforeEs}},
        {"get.score", {D, "PlayState.hx:324 (songScore)", "", ""}, {D, "PlayState.hx:212 (songScore)", "", ""},
         {A, "PlayState.hx:244 (songScore)", vsliceBeforeEn, vsliceBeforeEs}},
        {"get.songTime", {D, "Conductor.songPosition (Script.hx:103)", "", ""}, {D, "FunkinLua.hx:769 (getSongPosition)", "", ""},
         {D, "Conductor.hx:107 (songPosition)", "", ""}},
        {kAvoid, {S, "Note.hx:42, StrumLine.hx:231, PlayState.hx:1900-1905", "", ""}, {D, "Note.hx:64 (ignoreNote), PlayState.hx:3026", "", ""},
         {S, "PlayState.hx:2812, :2880, :2903-2918", "", ""}},
        {kCausesMiss, {S, "NoteHitEvent.hx:19, PlayState.hx:2008-2036", "", ""}, {D, "Note.hx:134 (hitCausesMiss), PlayState.hx:3100-3117", "", ""},
         {S, "PlayState.hx:3142-3162, :3171-3195", "", ""}},
        {kHitHealth, {D, "NoteHitEvent.hx:89 (healthGain)", "", ""}, {D, "Note.hx:124 (hitHealth), PlayState.hx:3097", "", ""},
         {D, "ScriptEvent.hx:116 (healthChange)", "", ""}},
        {kMissHealth, {D, "NoteMissEvent.hx:23 (healthGain)", "", ""}, {D, "Note.hx:125 (missHealth), PlayState.hx:2898", "", ""},
         {D, "ScriptEvent.hx:116 (healthChange), PlayState.hx:2903", "", ""}},
        {kNoSplash, {D, "NoteHitEvent.hx:97 (showSplash), PlayState.hx:2036", "", ""},
         {D, "Note.hx:22-23 (noteSplashData.disabled), PlayState.hx:2577", "", ""},
         {D, "ScriptEvent.hx:159 (doesNotesplash), PlayState.hx:3149", "", ""}},
        {kNoAnim, {D, "NoteHitEvent.hx:126 (preventAnim)", "", ""}, {D, "Note.hx:132 (noAnimation), PlayState.hx:2991, :3044", "", ""},
         {D, "NoteKind.hx:29 (noanim), BaseCharacter.hx:561", "", ""}},
        {kAnimSuffix, {D, "NoteHitEvent.hx:65 (animSuffix)", "", ""}, {D, "Note.hx:90 (animSuffix), PlayState.hx:2994, :3046", "", ""},
         {D, "NoteKind.hx:34 (suffix), BaseCharacter.hx:563", "", ""}},
        {kSinger, {D, "NoteHitEvent.hx:53 (characters)", "", ""}, {D, "Note.hx:91 (gfNote), PlayState.hx:2995, :3050", "", ""},
         {S, "BaseCharacter.hx:714 (playSingAnimation)", "", ""}},
        {kNoMissAnim, {D, "NoteMissEvent.hx:98 (preventAnim), PlayState.hx:1930", "", ""},
         {D, "Note.hx:133 (noMissAnimation), PlayState.hx:2958", "", ""},
         {N, "BaseCharacter.hx:605-626",
          "V-Slice plays the miss animation in the character itself, and cancelling the event also skips the scoring (PlayState.hx:2903-2918).",
          "V-Slice pone la animación de fallo en el propio personaje, y cancelar el evento también se salta la puntuación "
          "(PlayState.hx:2903-2918)."}},
        {"do.health", {S, "PlayState.hx:266 (health)", "", ""}, {S, "FunkinLua.hx:697 (addHealth)", "", ""}, {S, "PlayState.hx:237 (health)", "", ""}},
        {"do.setHealth", {S, "PlayState.hx:266 (health)", "", ""}, {S, "FunkinLua.hx:696 (setHealth)", "", ""},
         {S, "PlayState.hx:237 (health)", "", ""}},
        {"do.drain", {S, "FlxTimer (Script.hx:72), PlayState.hx:266", "", ""}, {S, "FunkinLua.hx:655, :664 (runTimer, onTimerCompleted)", "", ""},
         {S, "FlxTimer, PlayState.hx:237", "", ""}},
        {"do.score", {S, "PlayState.hx:324 (songScore)", "", ""}, {S, "FunkinLua.hx:672 (addScore)", "", ""}, {S, "PlayState.hx:244 (songScore)", "", ""}},
        {"do.setScore", {S, "PlayState.hx:324 (songScore)", "", ""}, {S, "FunkinLua.hx:684 (setScore)", "", ""},
         {S, "PlayState.hx:244 (songScore)", "", ""}},
        {"do.misses", {S, "PlayState.hx:328 (misses)", "", ""}, {S, "FunkinLua.hx:676 (addMisses)", "", ""},
         {S, "Highscore.hx:78 (tallies.missed)", "", ""}},
        {"do.combo", {S, "PlayState.hx:275 (combo)", "", ""}, {S, "PlayState.hx:177 (combo)", "", ""}, {S, "Highscore.hx:73 (tallies.combo)", "", ""}},
        {"do.hey", {S, "Character.hx:297 (playAnim), PlayAnimContext.hx:11", "", ""}, {S, "PlayState.hx:3069-3076", "", ""},
         {S, "Bopper.hx:259 (playAnimation)", "", ""}},
        {"do.anim", {S, "Character.hx:297 (playAnim), PlayAnimContext.hx:11", "", ""}, {S, "FunkinLua.hx:988 (playAnim)", "", ""},
         {S, "Bopper.hx:259 (playAnimation)", "", ""}},
        {"do.flash", {S, "PlayState.hx:319 (camGame)", "", ""}, {S, "FunkinLua.hx:841 (cameraFlash)", "", ""}, {S, "PlayState.hx:600 (camGame)", "", ""}},
        {"do.shake", {S, "PlayState.hx:319 (camGame)", "", ""}, {S, "FunkinLua.hx:837 (cameraShake)", "", ""}, {S, "PlayState.hx:600 (camGame)", "", ""}},
        {"do.zoom", {S, "PlayState.hx:1475-1483 (camZooming)", "", ""}, {S, "PlayState.hx:1756-1759 (camZooming)", "", ""},
         {S, "PlayState.hx:1222-1227 (cameraBopMultiplier)", "", ""}},
        {"do.hideHud", {S, "PlayState.hx:315 (camHUD), FlxTimer", "", ""}, {S, "PlayState.hx:207 (camHUD), FunkinLua.hx:655", "", ""},
         {S, "PlayState.hx:595 (camHUD), FlxTimer", "", ""}},
        {"do.sound", {S, "Paths.hx:84 (sound)", "", ""}, {S, "FunkinLua.hx:1317 (playSound)", "", ""},
         {S, "FunkinSound.hx:570 (playOnce), Paths.hx:115", "", ""}},
        {"do.image", {S, "FlxSprite, Paths.image, camHUD, FlxTimer", "", ""}, {S, "FunkinLua: makeLuaSprite, setObjectCamera, runTimer", "", ""}, {S, "FlxSprite, Paths.image, PlayState.camHUD, FlxTimer", "", ""}},
        {"do.screamer", {S, "FlxSprite, Paths.image/sound, camHUD, FlxTimer", "", ""}, {S, "FunkinLua: makeLuaSprite, playSound, runTimer", "", ""}, {S, "FlxSprite, FunkinSound.playOnce, PlayState.camHUD", "", ""}},
        {"do.video", {A, "CodenameCrew/codename-website: wiki/modding/scripting/hxvlc",
          "Requires a video-enabled build with hxvlc.", "Requiere una build con video y hxvlc."},
         {A, "FunkinLua.runHaxeCode; hxvlc/hxcodec FlxVideoSprite",
          "Requires runHaxeCode and a video sprite backend; Lua-only builds cannot execute this overlay.", "Requiere runHaxeCode y backend de video; las builds sólo Lua no pueden ejecutar este overlay."},
         {A, "funkin/graphics/video/FunkinVideoSprite.hx",
          "Requires the scriptable FunkinVideoSprite class and video support (modern V-Slice).", "Requiere FunkinVideoSprite scriptable y soporte de video (V-Slice moderno)."}},
    };
    for (const Row& row : rows)
        if (key == row.key) return engine == Engine::Codename ? row.codename : engine == Engine::Psych ? row.psych : row.vslice;
    return {};
}

// ---------------------------------------------------------------- frases --

namespace {

std::string argText(const BlockProgram& program, const BlockNode& node, const ArgDef& def, size_t index, bool spanish) {
    const BlockArg& arg = node.args[index];
    if (def.kind == ArgKind::Body) {
        std::string out = "[";
        bool first = true;
        for (int id : chainOf(program, arg.block)) {
            out += (first ? "" : "; ") + blockText(program, id, spanish);
            first = false;
        }
        return out + "]";
    }
    if (arg.block >= 0) {
        // Un valor sin argumentos («vida», «combo») se lee sin parentesis.
        const BlockNode* plugged = program.node(arg.block);
        const bool bare = plugged && plugged->args.empty();
        return bare ? blockText(program, arg.block, spanish) : "(" + blockText(program, arg.block, spanish) + ")";
    }
    switch (def.kind) {
        case ArgKind::Choice: return choiceLabel(def, arg.value, spanish);
        case ArgKind::Text: return "\"" + arg.value + "\"";
        case ArgKind::Color: return "#" + hexColor(arg.value);
        case ArgKind::Sound:
        case ArgKind::Image:
        case ArgKind::Video: return arg.value.empty() ? std::string("?") : arg.value;
        case ArgKind::Boolean: return spanish ? "<vacío>" : "<empty>";
        case ArgKind::Variable: {
            const auto* v = referencedVariable(program, arg.value);
            return v ? v->args[0].value : (spanish ? "<elegir variable>" : "<choose variable>");
        }
        default: return arg.value;
    }
}

}  // namespace

std::string blockText(const BlockProgram& program, int id, bool spanish) {
    const BlockNode* node = program.node(id);
    if (!node) return {};
    const BlockDef* def = blockDef(node->key);
    if (!def) return node->key;
    if (node->key == kCreate) return spanish ? "al crear" : "when created";
    if (node->key == kMiss) return spanish ? "cuando el jugador la falla" : "when the player misses it";
    if (node->key == kHit) {
        const std::string who = node->args.empty() ? "player" : node->args[0].value;
        return std::string(spanish ? "cuando " : "when ") + choiceLabel(def->args[0], who, spanish) + (spanish ? " la toca" : " hits it");
    }
    std::string out;
    const std::string pattern = spanish ? def->textEs : def->textEn;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const size_t close = pattern.find('}', i);
            if (close != std::string::npos) {
                const std::string name = pattern.substr(i + 1, close - i - 1);
                if (name != "type") {
                    const size_t index = static_cast<size_t>(std::atoi(name.c_str()));
                    if (index < def->args.size() && index < node->args.size()) out += argText(program, *node, def->args[index], index, spanish);
                }
                i = close;
                continue;
            }
        }
        out += pattern[i];
    }
    if (def->shape == BlockShape::If || def->shape == BlockShape::IfElse) {
        out += " " + argText(program, *node, def->args[1], 1, spanish);
        if (def->shape == BlockShape::IfElse) out += std::string(" ") + (spanish ? def->elseEs : def->elseEn) + " " + argText(program, *node, def->args[2], 2, spanish);
    }
    return out;
}

// ---------------------------------------------------------------- codigo --

namespace {

// Una propiedad leida del programa: la clave, su valor y el bloque.
struct Prop {
    std::string key;
    float amount = 0.0f;
    float amount2 = 1.0f;
    std::string text;
    int node = -1;
};

// Un evento con acciones: el sombrero, si es un acierto (o un fallo) y quien.
struct Event {
    int hat = -1;
    bool hit = true;
    std::string who = "player";
    int first = -1;
};

std::vector<Prop> propsOf(const BlockProgram& program) {
    std::vector<Prop> props;
    for (int top : program.tops) {
        const BlockNode* hat = program.node(top);
        if (!hat || hat->key != kCreate) continue;
        for (int id : chainOf(program, hat->next)) {
            const BlockNode& node = *program.node(id);
            const BlockDef* def = blockDef(node.key);
            if (!def || def->category != BlockCategory::Properties) continue;
            Prop prop;
            prop.key = node.key;
            prop.node = id;
            if (!def->args.empty() && !node.args.empty()) {
                if (def->args[0].kind == ArgKind::Number)
                    prop.amount = std::clamp(parseNumber(node.args[0].value, parseNumber(def->args[0].def, 0.0f)), def->args[0].min, def->args[0].max);
                else
                    prop.text = node.args[0].value;
                if (node.args.size() > 1 && def->args[1].kind == ArgKind::Number)
                    prop.amount2 = std::clamp(parseNumber(node.args[1].value, 1.0f), def->args[1].min, def->args[1].max);
            }
            props.push_back(std::move(prop));
        }
    }
    return props;
}

// La ultima con esa clave: si se repite, manda la ultima.
const Prop* findProp(const std::vector<Prop>& props, const char* key) {
    for (auto it = props.rbegin(); it != props.rend(); ++it)
        if (it->key == key) return &*it;
    return nullptr;
}

std::vector<Event> eventsOf(const BlockProgram& program) {
    std::vector<Event> events;
    for (int top : program.tops) {
        const BlockNode* hat = program.node(top);
        if (!hat || hat->next < 0 || (hat->key != kHit && hat->key != kMiss)) continue;
        Event event;
        event.hat = top;
        event.hit = hat->key == kHit;
        event.who = event.hit && !hat->args.empty() ? hat->args[0].value : std::string("player");
        if (event.who != "opponent" && event.who != "any") event.who = "player";
        event.first = hat->next;
        events.push_back(event);
    }
    return events;
}

// El estado al escribir el codigo de un evento.
struct Emit {
    Engine engine = Engine::Codename;
    const BlockProgram* program = nullptr;
    BlockCode* code = nullptr;
    std::string tag;               // base de las etiquetas de temporizador de Psych
    bool hit = true;
    int side = 0;                  // Psych: 1 goodNoteHit, 2 opponentNoteHit, 3 noteMiss
    int depth = 1;
    std::string out;
    std::map<int, std::string>* globals = nullptr;   // variables del script o campos del NoteKind
    std::map<int, std::string>* timers = nullptr;    // Psych: rama de onTimerCompleted por bloque
    std::set<std::string>* imports = nullptr;        // V-Slice
    bool bounded = false;
    bool captured = false;
};

bool luaLang(const Emit& e) { return e.engine == Engine::Psych; }
void line(Emit& e, const std::string& text);
void use(Emit& e, const char* import);

bool activeKey(const BlockProgram& program, const std::string& key) {
    for (const auto& entry : program.nodes)
        if (entry.second.key == key && (placeOf(program, entry.first) != 0 ||
            (key == "var.define" && std::find(program.tops.begin(), program.tops.end(), entry.first) != program.tops.end()))) return true;
    return false;
}

bool needsContext(const BlockProgram& program) {
    for (const char* key : {"ctl.after", "is.sustain", "get.noteTime", "get.sustainLength", "get.noteLane", "get.strumlineIndex", "get.hitOffset"})
        if (activeKey(program, key)) return true;
    return false;
}

bool needsBudget(const BlockProgram& program) { return activeKey(program, "ctl.repeat") || activeKey(program, "ctl.after"); }
bool needsClock(const BlockProgram& program) {
    return !blockVariables(program).empty() || activeKey(program, "ctl.after") || activeKey(program, "ctl.cancel");
}
bool needsMath(const BlockProgram& program) {
    return needsBudget(program) || needsClock(program) || activeKey(program, "op.clamp") || activeKey(program, "op.remap");
}

std::string songClock(Engine engine) {
    return engine == Engine::Psych ? "getSongPosition()" : engine == Engine::Codename ? "Conductor.songPosition" : "Conductor.instance.songPosition";
}

std::string runtimeHelpers(const BlockProgram& program, Engine engine) {
    if (!needsMath(program)) return {};
    const bool luaCode = engine == Engine::Psych;
    const std::string pad = engine == Engine::VSlice ? "  " : "";
    std::string out;
    auto row = [&](const std::string& text) { out += pad + text + "\n"; };
    for (const auto& variable : blockVariables(program))
        row((luaCode ? "local " : "var ") + std::string("notelabVar") + std::to_string(variable.id) + (luaCode ? " = " : ":Float = ") + number(static_cast<float>(variable.initial)) + (luaCode ? "" : ";"));
    if (luaCode) {
        out += R"NL(local function notelabFinite(value)
    value = tonumber(value) or 0
    if value ~= value or value == math.huge or value == -math.huge then return 0 end
    return math.max(-1000000, math.min(1000000, value))
end
local function notelabClamp(value, low, high)
    value, low, high = notelabFinite(value), notelabFinite(low), notelabFinite(high)
    if low > high then low, high = high, low end
    return math.max(low, math.min(high, value))
end
local function notelabRemap(value, a, b, c, d)
    value, a, b, c, d = notelabFinite(value), notelabFinite(a), notelabFinite(b), notelabFinite(c), notelabFinite(d)
    if a == b then return c end
    return notelabFinite(c + (value - a) * (d - c) / (b - a))
end
)NL";
    } else {
        row("function notelabFinite(value:Float):Float {");
        row("  if (!Math.isFinite(value)) return 0;");
        row("  return Math.max(-1000000, Math.min(1000000, value));");
        row("}");
        row("function notelabClamp(value:Float, low:Float, high:Float):Float {");
        row("  value = notelabFinite(value); low = notelabFinite(low); high = notelabFinite(high);");
        row("  if (low > high) { var swap = low; low = high; high = swap; }");
        row("  return Math.max(low, Math.min(high, value));");
        row("}");
        row("function notelabRemap(value:Float, a:Float, b:Float, c:Float, d:Float):Float {");
        row("  value = notelabFinite(value); a = notelabFinite(a); b = notelabFinite(b); c = notelabFinite(c); d = notelabFinite(d);");
        row("  if (a == b) return c;");
        row("  return notelabFinite(c + (value - a) * (d - c) / (b - a));");
        row("}");
    }
    if (!needsClock(program)) return out;
    if (luaCode) {
        out += "local notelabPending, notelabTokens = {}, {}\nlocal notelabSerial, notelabClock = 0, nil\nlocal function notelabReset()\n";
        out += "    notelabPending, notelabTokens, notelabSerial, notelabClock = {}, {}, 0, nil\n";
        for (const auto& v : blockVariables(program)) out += "    notelabVar" + std::to_string(v.id) + " = " + number(static_cast<float>(v.initial)) + "\n";
        out += R"NL(end
local function notelabSync(now)
    if now ~= now or now == math.huge or now == -math.huge then return false end
    if notelabClock ~= nil and (now < notelabClock - 1 or now - notelabClock > 1000) then notelabReset() end
    notelabClock = now
    return true
end
local function notelabCancel(key)
    notelabTokens[key] = nil
    for i = #notelabPending, 1, -1 do
        if notelabPending[i].key == key then table.remove(notelabPending, i) end
    end
end
local function notelabSchedule(key, seconds, action)
    if not notelabSync(getSongPosition()) then return end
    notelabCancel(key)
    if #notelabPending >= 32 then return end
    if notelabSerial >= 1000000000 then notelabReset(); notelabSync(getSongPosition()) end
    notelabSerial = notelabSerial + 1
    notelabTokens[key] = notelabSerial
    table.insert(notelabPending, {key = key, token = notelabSerial, due = notelabClock + notelabClamp(seconds, 0, 60) * 1000, run = action})
end
local function notelabStep(now)
    local previous = notelabClock
    if not notelabSync(now) or previous == nil or now <= previous then return end
    local ready, waiting = {}, {}
    for _, task in ipairs(notelabPending) do
        if task.due <= now and #ready < 4 then table.insert(ready, task) else table.insert(waiting, task) end
    end
    notelabPending = waiting
    for _, task in ipairs(ready) do
        if notelabTokens[task.key] == task.token then
            notelabTokens[task.key] = nil
            task.run()
        end
    end
end
)NL";
    } else {
        row("var notelabPending:Array<Dynamic> = [];");
        row("var notelabTokens:Map<String, Int> = new haxe.ds.StringMap();");
        row("var notelabSerial:Int = 0;");
        row("var notelabClock:Null<Float> = null;");
        row("function notelabReset():Void {");
        row("  notelabPending = []; notelabTokens = new haxe.ds.StringMap(); notelabSerial = 0; notelabClock = null;");
        for (const auto& v : blockVariables(program)) row("  notelabVar" + std::to_string(v.id) + " = " + number(static_cast<float>(v.initial)) + ";");
        row("}");
        row("function notelabSync(now:Float):Bool {");
        row("  if (!Math.isFinite(now)) return false;");
        row("  if (notelabClock != null && (now < notelabClock - 1 || now - notelabClock > 1000)) notelabReset();");
        row("  notelabClock = now; return true;");
        row("}");
        row("function notelabCancel(key:String):Void {");
        row("  notelabTokens.remove(key);");
        row("  for (task in notelabPending.copy()) if (task.key == key) notelabPending.remove(task);");
        row("}");
        row("function notelabSchedule(key:String, seconds:Float, action:Void->Void):Void {");
        row("  if (!notelabSync(" + songClock(engine) + ")) return;");
        row("  notelabCancel(key); if (notelabPending.length >= 32) return;");
        row("  if (notelabSerial >= 1000000000) { notelabReset(); notelabSync(" + songClock(engine) + "); }");
        row("  notelabSerial++; notelabTokens.set(key, notelabSerial);");
        row("  notelabPending.push({key: key, token: notelabSerial, due: notelabClock + notelabClamp(seconds, 0, 60) * 1000, run: action});");
        row("}");
        row("function notelabStep(now:Float):Void {");
        row("  var previous = notelabClock;");
        row("  if (!notelabSync(now) || previous == null || now <= previous) return;");
        row("  var ready:Array<Dynamic> = []; var waiting:Array<Dynamic> = [];");
        row("  for (task in notelabPending) { if (task.due <= now && ready.length < 4) ready.push(task); else waiting.push(task); }");
        row("  notelabPending = waiting;");
        row("  for (task in ready) if (notelabTokens.get(task.key) == task.token) { notelabTokens.remove(task.key); task.run(); }");
        row("}");
    }
    return out;
}

void eventPrelude(Emit& e) {
    const bool luaCode = luaLang(e);
    if (needsClock(*e.program)) line(e, "notelabSync(" + songClock(e.engine) + ")" + (luaCode ? "" : ";"));
    e.bounded = needsBudget(*e.program);
    if (e.bounded) line(e, luaCode ? "local notelabBudget = 512" : "var notelabBudget = 512;");
    if (!needsContext(*e.program)) return;
    if (e.engine == Engine::Codename) {
        line(e, "var notelabContext = {time: event.note.strumTime / 1000, sustain: event.note.sustainLength / 1000, lane: event.direction, line: strumLines.members.indexOf(event.note.strumLine), offset: " +
             std::string(e.hit ? "Conductor.songPosition - event.note.strumTime" : "0") + ", player: " + (e.hit ? "event.player" : "true") + ", rating: " + (e.hit ? "event.rating" : "\"\"") + "};");
    } else if (luaCode) {
        line(e, "local notelabContext = {time = (getPropertyFromGroup('notes', id, 'strumTime') or 0) / 1000, sustain = (getPropertyFromGroup('notes', id, 'sustainLength') or 0) / 1000, lane = direction, line = " +
             std::string(e.side == 2 ? "0" : "1") + ", offset = " + (e.hit ? "getSongPosition() - (getPropertyFromGroup('notes', id, 'strumTime') or getSongPosition())" : "0") +
             ", player = " + (e.side == 2 ? "false" : "true") + ", gf = getPropertyFromGroup('notes', id, 'gfNote') == true, rating = " +
             (e.hit ? "getPropertyFromGroup('notes', id, 'rating') or ''" : "''") + "}");
    } else {
        use(e, "funkin.Conductor");
        line(e, "var notelabContext = {time: event.note.noteData.time / 1000, sustain: event.note.noteData.length / 1000, lane: event.note.noteData.getDirection(), line: event.note.noteData.getMustHitNote() ? 1 : 0, offset: " +
             std::string(e.hit ? "event.hitDiff" : "0") + ", player: event.note.noteData.getMustHitNote(), rating: " + (e.hit ? "event.judgement" : "\"\"") + "};");
    }
}

void line(Emit& e, const std::string& text) {
    e.out += (e.engine == Engine::VSlice ? std::string(static_cast<size_t>(e.depth) * 2, ' ') : std::string(static_cast<size_t>(e.depth), '\t'));
    e.out += text + "\n";
}

void use(Emit& e, const char* import) {
    if (e.imports) e.imports->insert(import);
}

std::string expression(Emit& e, int id);

// El valor de un argumento: el bloque enchufado o lo escrito.
std::string value(Emit& e, const BlockNode& node, size_t index) {
    const BlockDef* def = blockDef(node.key);
    if (!def || index >= def->args.size() || index >= node.args.size()) return "0";
    const ArgDef& arg = def->args[index];
    const BlockArg& slot = node.args[index];
    if (slot.block >= 0 && arg.plug) return expression(e, slot.block);
    switch (arg.kind) {
        case ArgKind::Number: return number(std::clamp(parseNumber(slot.value, parseNumber(arg.def, 0.0f)), arg.min, arg.max));
        case ArgKind::Boolean: return "false";
        case ArgKind::Text:
        case ArgKind::Sound:
        case ArgKind::Image:
        case ArgKind::Video: return luaLang(e) ? lua(slot.value) : hx(slot.value);
        case ArgKind::Color: return hexColor(slot.value);
        case ArgKind::Variable: return referencedVariable(*e.program, slot.value) ? "notelabVar" + slot.value : "0";
        default: return slot.value;
    }
}

// Un argumento que tiene que ser entero (puntos, fallos, combo, azar).
std::string wholeValue(Emit& e, const BlockNode& node, size_t index) {
    const BlockDef* def = blockDef(node.key);
    if (def && index < def->args.size() && index < node.args.size() && node.args[index].block < 0) {
        const ArgDef& arg = def->args[index];
        return integer(std::clamp(parseNumber(node.args[index].value, parseNumber(arg.def, 0.0f)), arg.min, arg.max));
    }
    const std::string inner = value(e, node, index);
    return luaLang(e) ? "math.floor(" + inner + ")" : "Std.int(" + inner + ")";
}

std::string expression(Emit& e, int id) {
    const BlockNode* found = e.program->node(id);
    if (!found) return "false";
    const BlockNode& n = *found;
    const std::string& k = n.key;
    const bool lua = luaLang(e);
    const Engine engine = e.engine;
    if (k == "op.compare") {
        const std::string op = n.args.size() > 1 ? n.args[1].value : "gt";
        const char* symbol = op == "eq" ? "==" : op == "ne" ? (lua ? "~=" : "!=") : op == "lt" ? "<" : op == "le" ? "<=" : op == "ge" ? ">=" : ">";
        return "(" + value(e, n, 0) + " " + symbol + " " + value(e, n, 2) + ")";
    }
    if (k == "op.and") return "(" + value(e, n, 0) + (lua ? " and " : " && ") + value(e, n, 1) + ")";
    if (k == "op.or") return "(" + value(e, n, 0) + (lua ? " or " : " || ") + value(e, n, 1) + ")";
    if (k == "op.not") return lua ? "(not " + value(e, n, 0) + ")" : "!" + value(e, n, 0);
    if (k == "op.chance") {
        if (lua) return "getRandomBool(" + value(e, n, 0) + ")";
        use(e, "flixel.FlxG");
        return "FlxG.random.bool(" + value(e, n, 0) + ")";
    }
    if (k == "op.random") {
        if (lua) return "getRandomInt(" + wholeValue(e, n, 0) + ", " + wholeValue(e, n, 1) + ")";
        use(e, "flixel.FlxG");
        return "FlxG.random.int(" + wholeValue(e, n, 0) + ", " + wholeValue(e, n, 1) + ")";
    }
    if (k == "op.math") {
        const std::string op = n.args.size() > 1 ? n.args[1].value : "add";
        const char* symbol = op == "sub" ? "-" : op == "mul" ? "*" : op == "div" ? "/" : "+";
        return "(" + value(e, n, 0) + " " + symbol + " " + value(e, n, 2) + ")";
    }
    if (k == "op.clamp") return "notelabClamp(" + value(e, n, 0) + ", " + value(e, n, 1) + ", " + value(e, n, 2) + ")";
    if (k == "op.remap") return "notelabRemap(" + value(e, n, 0) + ", " + value(e, n, 1) + ", " + value(e, n, 2) + ", " + value(e, n, 3) + ", " + value(e, n, 4) + ")";
    if (k == "var.get") return value(e, n, 0);
    if (k == "is.sustain") return "(notelabContext.sustain > 0)";
    if (k == "get.noteTime") return "notelabContext.time";
    if (k == "get.sustainLength") return "notelabContext.sustain";
    if (k == "get.noteLane") return "notelabContext.lane";
    if (k == "get.strumlineIndex") return "notelabContext.line";
    if (k == "get.hitOffset") return e.hit ? "notelabContext.offset" : "0";
    if (k == "get.bpm" || k == "get.beat") {
        if (engine == Engine::Psych) return k == "get.bpm" ? "curBpm" : "curBeat";
        if (engine == Engine::Codename) return k == "get.bpm" ? "Conductor.bpm" : "Conductor.curBeat";
        use(e, "funkin.Conductor");
        return k == "get.bpm" ? "Conductor.instance.bpm" : "Conductor.instance.currentBeat";
    }
    if (k == "is.direction") {
        const std::string lane = std::to_string(directionIndex(n.args.empty() ? std::string() : n.args[0].value));
        if (e.captured) return "(notelabContext.lane == " + lane + ")";
        if (engine == Engine::Codename) return "(event.direction == " + lane + ")";
        if (engine == Engine::Psych) return "(direction == " + lane + ")";
        return "(event.note.noteData.getDirection() == " + lane + ")";
    }
    if (k == "is.rating") {
        if (!e.hit) return "false";
        const std::string rating = n.args.empty() ? std::string("sick") : n.args[0].value;
        if (e.captured) return "(notelabContext.rating == " + (lua ? ::fml::notelab::lua(rating) : hx(rating)) + ")";
        if (engine == Engine::Codename) return "(event.rating == " + hx(rating) + ")";
        if (engine == Engine::Psych) return "(getPropertyFromGroup('notes', id, 'rating') == " + ::fml::notelab::lua(rating) + ")";
        return "(event.judgement == " + hx(rating) + ")";
    }
    if (k == "is.player") {
        if (e.captured) return "notelabContext.player";
        if (!e.hit) return "true";
        if (engine == Engine::Codename) return "event.player";
        if (engine == Engine::Psych) return e.side == 2 ? "false" : "true";
        return "event.note.noteData.getMustHitNote()";
    }
    if (k == "is.downscroll") {
        if (engine == Engine::VSlice) {
            use(e, "funkin.Preferences");
            return "Preferences.downscroll";
        }
        return "downscroll";
    }
    if (k == "get.health") {
        if (engine == Engine::Codename) return "health";
        if (engine == Engine::Psych) return "getHealth()";
        use(e, "funkin.play.PlayState");
        return "PlayState.instance.health";
    }
    if (k == "get.combo") {
        if (engine == Engine::Codename) return "combo";
        if (engine == Engine::Psych) return "getProperty('combo')";
        use(e, "funkin.Highscore");
        return "Highscore.tallies.combo";
    }
    if (k == "get.misses") {
        if (engine == Engine::Codename) return "misses";
        if (engine == Engine::Psych) return "getProperty('songMisses')";
        use(e, "funkin.Highscore");
        return "Highscore.tallies.missed";
    }
    if (k == "get.score") {
        if (engine == Engine::Codename) return "songScore";
        if (engine == Engine::Psych) return "getProperty('songScore')";
        use(e, "funkin.play.PlayState");
        return "PlayState.instance.songScore";
    }
    if (k == "get.songTime") {
        if (engine == Engine::Codename) return "(Conductor.songPosition / 1000)";
        if (engine == Engine::Psych) return "(getSongPosition() / 1000)";
        use(e, "funkin.Conductor");
        return "(Conductor.instance.songPosition / 1000)";
    }
    return lua ? "nil" : "null";
}

void emitChain(Emit& e, int first);

// Si toda la expresion va entre un par de parentesis que se cierran al final.
bool wrapped(const std::string& text) {
    if (text.size() < 2 || text.front() != '(' || text.back() != ')') return false;
    int depth = 0;
    bool quote = false;
    char mark = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == mark) quote = false;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = true;
            mark = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')' && --depth == 0 && i + 1 < text.size()) {
            return false;
        }
    }
    return depth == 0;
}

// Un bloque de varias lineas en su propio ambito, para que sus variables no choquen.
void open(Emit& e) {
    line(e, luaLang(e) ? "do" : "{");
    ++e.depth;
}

void close(Emit& e) {
    --e.depth;
    line(e, luaLang(e) ? "end" : "}");
}

std::string character(Emit& e, const std::string& who) {
    if (e.engine == Engine::Codename) return who == "dad" ? "dad" : who == "gf" ? "gf" : "boyfriend";
    if (e.engine == Engine::Psych) return who == "dad" ? "dad" : who == "gf" ? "gf" : "boyfriend";
    use(e, "funkin.play.PlayState");
    return who == "dad" ? "PlayState.instance.currentStage.getDad()" : who == "gf" ? "PlayState.instance.currentStage.getGirlfriend()"
                                                                          : "PlayState.instance.currentStage.getBoyfriend()";
}

// Un temporizador por bloque: si vuelve a ejecutarse, el anterior se cancela.
std::string timerName(int id) { return "notelabTimer" + std::to_string(id); }

#include "NoteMediaEmit.hpp"

void emitStatementBody(Emit& e, int id) {
    const BlockNode* found = e.program->node(id);
    if (!found) return;
    const BlockNode& n = *found;
    const BlockDef* def = blockDef(n.key);
    if (!def || def->shape == BlockShape::Hat || def->category == BlockCategory::Properties) return;
    const std::string& k = n.key;
    const bool lua = luaLang(e);
    const Engine engine = e.engine;
    if (k == "do.image" || k == "do.screamer" || k == "do.video") {
        emitMedia(e, n, id);
        return;
    }
    if (k == "var.set" || k == "var.change") {
        const std::string target = value(e, n, 0);
        if (target == "0") return;
        line(e, target + " = notelabFinite(" + (k == "var.change" ? target + " + " : "") + value(e, n, 1) + ")" + (lua ? "" : ";"));
        return;
    }
    if (k == "ctl.repeat") {
        open(e);
        const std::string count = "notelabCount" + std::to_string(id), index = "notelabIndex" + std::to_string(id);
        line(e, lua ? "local " + count + " = math.floor(notelabClamp(" + value(e, n, 0) + ", 0, 64))"
                    : "var " + count + " = Std.int(notelabClamp(" + value(e, n, 0) + ", 0, 64));");
        line(e, lua ? "local " + index + " = 0" : "var " + index + " = 0;");
        line(e, lua ? "while " + index + " < " + count + " and notelabBudget > 0 do"
                    : "while (" + index + " < " + count + " && notelabBudget > 0) {");
        ++e.depth;
        line(e, index + " = " + index + " + 1" + (lua ? "" : ";"));
        emitChain(e, n.args[1].block);
        --e.depth;
        line(e, lua ? "end" : "}");
        close(e);
        return;
    }
    if (k == "ctl.after") {
        line(e, "notelabSchedule(" + value(e, n, 2) + ", " + value(e, n, 0) + (lua ? ", function()" : ", function() {"));
        ++e.depth;
        line(e, lua ? "local notelabBudget = 512" : "var notelabBudget = 512;");
        const bool wasCaptured = e.captured;
        e.captured = true;
        emitChain(e, n.args[1].block);
        e.captured = wasCaptured;
        --e.depth;
        line(e, lua ? "end)" : "});");
        return;
    }
    if (k == "ctl.cancel") {
        line(e, "notelabCancel(" + value(e, n, 0) + ")" + (lua ? "" : ";"));
        return;
    }
    if (k == "ctl.if" || k == "ctl.ifElse") {
        std::string cond = value(e, n, 0);
        // Sin parentesis dobles: «if (combo > 10)», no «if ((combo > 10))».
        if (!lua && wrapped(cond)) cond = cond.substr(1, cond.size() - 2);
        line(e, lua ? "if " + cond + " then" : "if (" + cond + ") {");
        ++e.depth;
        emitChain(e, n.args[1].block);
        --e.depth;
        if (k == "ctl.ifElse") {
            line(e, lua ? "else" : "} else {");
            ++e.depth;
            emitChain(e, n.args[2].block);
            --e.depth;
        }
        line(e, lua ? "end" : "}");
        return;
    }
    if (k == "do.log") {
        line(e, lua ? "debugPrint(" + value(e, n, 0) + ")" : "trace(" + value(e, n, 0) + ");");
        return;
    }
    if (k == "code.line") {
        // Tal cual en su motor; en otro, como comentario (no compilaria).
        const std::string from = n.args.empty() ? std::string() : n.args[0].value;
        const std::string text = n.args.size() > 1 ? n.args[1].value : std::string();
        if (text.empty()) return;
        line(e, from == engineKey(engine) ? text : std::string(lua ? "-- " : "// ") + "(" + from + ") " + text);
        return;
    }
    if (k == "do.health" || k == "do.setHealth") {
        const bool set = k == "do.setHealth";
        const std::string v = value(e, n, 0);
        if (engine == Engine::Codename) line(e, std::string("health ") + (set ? "= " : "+= ") + v + ";");
        else if (engine == Engine::Psych) line(e, std::string(set ? "setHealth(" : "addHealth(") + v + ")");
        else {
            use(e, "funkin.play.PlayState");
            line(e, std::string("PlayState.instance.health ") + (set ? "= " : "+= ") + v + ";");
        }
        return;
    }
    if (k == "do.drain") {
        const std::string amount = value(e, n, 0), every = value(e, n, 1), times = wholeValue(e, n, 2);
        if (lua) {
            const std::string tag = e.tag + "_" + std::to_string(id);
            const std::string capturedAmount = "notelabDrainAmount" + std::to_string(id);
            if (n.args[0].block >= 0) {
                (*e.globals)[id] = "local " + capturedAmount + " = 0\n";
                line(e, capturedAmount + " = " + amount);
            }
            line(e, "runTimer(" + ::fml::notelab::lua(tag) + ", " + every + ", " + times + ")");
            (*e.timers)[id] = "\t\taddHealth(" + (n.args[0].block >= 0 ? capturedAmount : amount) + ")\n";
            return;
        }
        const std::string timer = timerName(id);
        const std::string target = engine == Engine::Codename ? "health" : "PlayState.instance.health";
        if (engine == Engine::VSlice) {
            use(e, "funkin.play.PlayState");
            use(e, "flixel.util.FlxTimer");
            (*e.globals)[id] = "  var " + timer + ":FlxTimer = null;\n";
        } else {
            (*e.globals)[id] = "var " + timer + " = null;\n";
        }
        line(e, "if (" + timer + " != null) " + timer + ".cancel();");
        line(e, timer + " = new FlxTimer().start(" + every + ", function(_) { " + target + " += " + amount + "; }, " + times + ");");
        return;
    }
    if (k == "do.score" || k == "do.setScore") {
        const bool set = k == "do.setScore";
        if (engine == Engine::Codename) line(e, std::string("songScore ") + (set ? "= " : "+= ") + wholeValue(e, n, 0) + ";");
        else if (engine == Engine::Psych) line(e, std::string(set ? "setScore(" : "addScore(") + wholeValue(e, n, 0) + ")");
        else {
            use(e, "funkin.play.PlayState");
            line(e, std::string("PlayState.instance.songScore ") + (set ? "= " : "+= ") + wholeValue(e, n, 0) + ";");
        }
        return;
    }
    if (k == "do.misses") {
        if (engine == Engine::Codename) line(e, "misses += " + wholeValue(e, n, 0) + ";");
        else if (engine == Engine::Psych) line(e, "addMisses(" + wholeValue(e, n, 0) + ")");
        else {
            use(e, "funkin.Highscore");
            line(e, "Highscore.tallies.missed += " + wholeValue(e, n, 0) + ";");
        }
        return;
    }
    if (k == "do.combo") {
        if (engine == Engine::Codename) line(e, "combo = " + wholeValue(e, n, 0) + ";");
        else if (engine == Engine::Psych) line(e, "setProperty('combo', " + wholeValue(e, n, 0) + ")");
        else {
            use(e, "funkin.Highscore");
            line(e, "Highscore.tallies.combo = " + wholeValue(e, n, 0) + ";");
        }
        return;
    }
    if (k == "do.hey") {
        // «Hey!»: el personaje de su lado y GF que anima, como el tipo de serie de Psych.
        if (engine == Engine::Codename) {
            if (e.captured) {
                line(e, "if (notelabContext.line >= 0 && notelabContext.line < strumLines.members.length && strumLines.members[notelabContext.line] != null) {");
                ++e.depth;
            }
            line(e, e.captured ? "for (character in strumLines.members[notelabContext.line].characters)" : "for (character in event.characters)");
            line(e, "\tif (character != null && character.hasAnimation(\"hey\")) character.playAnim(\"hey\", true);");
            line(e, e.captured ? "if (notelabContext.player && gf != null && gf.hasAnimation(\"cheer\")) gf.playAnim(\"cheer\", true);"
                              : std::string("if (") + (e.hit ? "event.player && " : "") +
                                "gf != null && !event.characters.contains(gf) && gf.hasAnimation(\"cheer\")) gf.playAnim(\"cheer\", true);");
            if (e.captured) { --e.depth; line(e, "}"); }
        } else if (engine == Engine::Psych) {
            open(e);
            if (e.side == 2) {
                line(e, "playAnim('dad', 'hey', true)");
                line(e, "setProperty('dad.specialAnim', true)");
                line(e, "setProperty('dad.heyTimer', 0.6)");
            } else {
                line(e, "local character, anim = 'boyfriend', 'hey'");
                line(e, e.captured ? "if notelabContext.gf then character, anim = 'gf', 'cheer' end"
                                  : "if getPropertyFromGroup('notes', id, 'gfNote') then character, anim = 'gf', 'cheer' end");
                line(e, "playAnim(character, anim, true)");
                line(e, "setProperty(character .. '.specialAnim', true)");
                line(e, "setProperty(character .. '.heyTimer', 0.6)");
            }
            close(e);
        } else {
            use(e, "funkin.play.PlayState");
            open(e);
            line(e, "var stage = PlayState.instance.currentStage;");
            line(e, std::string("var side = ") + (e.captured ? "notelabContext.player" : "event.note.noteData.getMustHitNote()") + " ? stage.getBoyfriend() : stage.getDad();");
            line(e, "if (side != null && side.hasAnimation('hey')) side.playAnimation('hey', true, true);");
            line(e, "var cheer = stage.getGirlfriend();");
            line(e, std::string("if (") + (e.captured ? "notelabContext.player" : "event.note.noteData.getMustHitNote()") + " && cheer != null && cheer.hasAnimation('cheer')) cheer.playAnimation('cheer', true, true);");
            close(e);
        }
        return;
    }
    if (k == "do.anim") {
        const std::string who = n.args.empty() ? std::string("bf") : n.args[0].value;
        const std::string anim = n.args.size() > 1 ? n.args[1].value : std::string("hey");
        if (anim.empty()) return;
        if (engine == Engine::Codename) {
            const std::string c = character(e, who);
            line(e, "if (" + c + " != null) " + c + ".playAnim(" + hx(anim) + ", true);");
        } else if (engine == Engine::Psych) {
            const std::string c = character(e, who);
            const std::string body = "playAnim('" + c + "', " + ::fml::notelab::lua(anim) + ", true); setProperty('" + c + ".specialAnim', true)";
            if (who == "gf") line(e, "if getProperty('gf.alpha') ~= nil then " + body + " end");
            else line(e, body);
        } else {
            open(e);
            line(e, "var character = " + character(e, who) + ";");
            line(e, "if (character != null && character.hasAnimation(" + hx(anim) + ")) character.playAnimation(" + hx(anim) + ", true, true);");
            close(e);
        }
        return;
    }
    if (k == "do.flash") {
        const std::string rgb = value(e, n, 0), seconds = value(e, n, 1);
        if (engine == Engine::Psych) line(e, "cameraFlash('camGame', '" + rgb + "', " + seconds + ", true)");
        else if (engine == Engine::Codename) line(e, "camGame.flash(0xFF" + rgb + ", " + seconds + ", null, true);");
        else {
            use(e, "funkin.play.PlayState");
            line(e, "PlayState.instance.camGame.flash(0xFF" + rgb + ", " + seconds + ", null, true);");
        }
        return;
    }
    if (k == "do.shake") {
        const std::string power = value(e, n, 0), seconds = value(e, n, 1);
        if (engine == Engine::Psych) line(e, "cameraShake('camGame', " + power + ", " + seconds + ")");
        else if (engine == Engine::Codename) line(e, "camGame.shake(" + power + ", " + seconds + ", null, true);");
        else {
            use(e, "funkin.play.PlayState");
            line(e, "PlayState.instance.camGame.shake(" + power + ", " + seconds + ", null, true);");
        }
        return;
    }
    if (k == "do.zoom") {
        const std::string amount = value(e, n, 0);
        if (engine == Engine::Codename) {
            line(e, "camGame.zoom += " + amount + ";");
            line(e, "camHUD.zoom += " + amount + " * 2;");
            line(e, "camZooming = true;");
        } else if (engine == Engine::Psych) {
            line(e, "setProperty('camGame.zoom', getProperty('camGame.zoom') + " + amount + ")");
            line(e, "setProperty('camHUD.zoom', getProperty('camHUD.zoom') + " + amount + " * 2)");
            line(e, "setProperty('camZooming', true)");
        } else {
            use(e, "funkin.play.PlayState");
            line(e, "PlayState.instance.cameraBopMultiplier += " + amount + ";");
            line(e, "PlayState.instance.camHUD.zoom += " + amount + " * 2 * PlayState.instance.defaultHUDCameraZoom;");
        }
        return;
    }
    if (k == "do.hideHud") {
        const std::string seconds = value(e, n, 0);
        if (lua) {
            const std::string tag = e.tag + "_" + std::to_string(id);
            line(e, "setProperty('camHUD.visible', false)");
            line(e, "runTimer(" + ::fml::notelab::lua(tag) + ", " + seconds + ")");
            (*e.timers)[id] = "\t\tsetProperty('camHUD.visible', true)\n";
            return;
        }
        const std::string timer = timerName(id);
        const std::string hud = engine == Engine::Codename ? "camHUD" : "PlayState.instance.camHUD";
        if (engine == Engine::VSlice) {
            use(e, "funkin.play.PlayState");
            use(e, "flixel.util.FlxTimer");
            (*e.globals)[id] = "  var " + timer + ":FlxTimer = null;\n";
        } else {
            (*e.globals)[id] = "var " + timer + " = null;\n";
        }
        line(e, hud + ".visible = false;");
        line(e, "if (" + timer + " != null) " + timer + ".cancel();");
        line(e, timer + " = new FlxTimer().start(" + seconds + ", function(_) { " + hud + ".visible = true; });");
        return;
    }
    if (k == "do.sound") {
        const std::string name = n.args.empty() ? std::string() : resourceNativeKey(n.args[0].value, ResourceKind::Sound);
        if (name.empty()) return;
        const std::string volume = value(e, n, 1);
        if (engine == Engine::Psych) line(e, "playSound(" + ::fml::notelab::lua(name) + ", " + volume + ")");
        else if (engine == Engine::Codename) line(e, "FlxG.sound.play(Paths.sound(" + hx(name) + "), " + volume + ");");
        else {
            use(e, "funkin.audio.FunkinSound");
            use(e, "funkin.Paths");
            line(e, "FunkinSound.playOnce(Paths.sound(" + hx(name) + "), " + volume + ");");
        }
        return;
    }
}

void emitStatement(Emit& e, int id) {
    if (!e.bounded) { emitStatementBody(e, id); return; }
    line(e, luaLang(e) ? "if notelabBudget > 0 then" : "if (notelabBudget > 0) {");
    ++e.depth;
    line(e, luaLang(e) ? "notelabBudget = notelabBudget - 1" : "notelabBudget--;");
    emitStatementBody(e, id);
    --e.depth;
    line(e, luaLang(e) ? "end" : "}");
}

void emitChain(Emit& e, int first) {
    for (int id : chainOf(*e.program, first)) emitStatement(e, id);
}

const char* whoComment(const std::string& who, bool hit) {
    if (!hit) return "When the player misses it.";
    if (who == "opponent") return "When the opponent hits it.";
    if (who == "any") return "When anyone hits it.";
    return "When the player hits it.";
}

// La cabecera: de donde sale y el programa en frases, en ingles.
std::string header(const char* comment, const std::string& type, const BlockProgram& program) {
    auto inlineText = [](const std::string& text) {
        std::string out;
        for (char c : text) {
            if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else out += c;
        }
        return out;
    };
    std::string out = std::string(comment) + " Note Lab (Funkin Mod Lab): note type \"" + inlineText(type) + "\".\n";
    for (const std::string& text : programSummary(program, false)) out += std::string(comment) + " - " + inlineText(text) + "\n";
    auto userLines = [&](const std::string& text, const std::string& marker) {
        size_t at = 0;
        while (at < text.size()) {
            const size_t end = text.find('\n', at);
            std::string row = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            row.erase(std::remove(row.begin(), row.end(), '\r'), row.end());
            out += std::string(comment) + " " + marker + ": " + row + "\n";
            if (end == std::string::npos) break;
            at = end + 1;
        }
    };
    userLines(program.comment, "@user");
    for (const auto& entry : program.nodes) userLines(entry.second.comment, "@user#" + std::to_string(entry.first));
    return out;
}

// Lo que es comun a los tres motores: el programa bien armado y los choques
// entre propiedades.
void checks(BlockCode& code, const BlockProgram& program, Engine engine) {
    const std::vector<Prop> props = propsOf(program);
    for (size_t i = 0; i < props.size(); ++i)
        for (size_t j = i + 1; j < props.size(); ++j)
            if (props[i].key == props[j].key)
                conflict(code, Severity::Info, props[i].key, props[i].node, "Repeated block: the last one counts.", "Bloque repetido: vale el último.");
    const Prop* avoid = findProp(props, kAvoid);
    if (avoid) {
        if (const Prop* missHealth = findProp(props, kMissHealth))
            conflict(code, Severity::Warning, kMissHealth, missHealth->node,
                     "With «must be avoided», letting it pass is not a miss: the health when missed never applies.",
                     "Con «hay que evitarla», dejarla pasar no es un fallo: la vida al fallarla no se aplica nunca.");
        for (const Event& event : eventsOf(program))
            if (!event.hit)
                conflict(code, Severity::Warning, kMiss, event.hat,
                         "With «must be avoided», letting it pass is not a miss: these blocks never run.",
                         "Con «hay que evitarla», dejarla pasar no es un fallo: estos bloques no se ejecutan nunca.");
    }
    if (const Prop* causesMiss = findProp(props, kCausesMiss))
        if (findProp(props, kNoAnim) || findProp(props, kAnimSuffix) || findProp(props, kSinger))
            conflict(code, Severity::Info, kCausesMiss, causesMiss->node,
                     "Hitting it is a miss: the character plays the miss animation, so the singing blocks only count on the opponent's side.",
                     "Tocarla es un fallo: el personaje hace la animación de fallo, así que los bloques de canto solo cuentan en el lado del rival.");
    // Cada bloque, en su sitio.
    std::function<void(int)> visit;
    visit = [&](int id) {
        const BlockNode* node = program.node(id);
        if (!node) return;
        const BlockDef* def = blockDef(node->key);
        const unsigned place = placeOf(program, id);
        if (def && place != 0) {
            for (size_t i = 0; i < node->args.size(); ++i)
                if (def->args[i].kind == ArgKind::Variable && !referencedVariable(program, node->args[i].value))
                    conflict(code, Severity::Error, node->key, id, "Select a declared numeric variable. The declaration may have been deleted.", "Elige una variable numérica declarada. Puede que su declaración se haya borrado.");
            if (node->key == "get.hitOffset" && place != kPlaceHit)
                conflict(code, Severity::Error, node->key, id, "Hit offset only exists in a hit event.", "El desfase del acierto sólo existe en un evento de acierto.");
            if ((node->key == "ctl.after" || node->key == "ctl.cancel") && node->args[node->key == "ctl.after" ? 2 : 0].value.empty())
                conflict(code, Severity::Error, node->key, id, "Give the deferred action a non-empty key.", "Pon una clave no vacía a la acción diferida.");
        }
        if (!def) {
            conflict(code, Severity::Warning, node->key, id, "Unknown block: not exported.", "Bloque desconocido: no se exporta.");
        } else if (place != kPlaceTop && place != 0 && !fits(*def, place)) {
            if (def->category == BlockCategory::Properties)
                conflict(code, Severity::Warning, node->key, id, "Properties go right under «when created»: this one does nothing here.",
                         "Las propiedades van justo debajo de «al crear»: aquí no hace nada.");
            else if (place == kPlaceCreate)
                conflict(code, Severity::Warning, node->key, id, "Under «when created» only properties go: this one does nothing here.",
                         "Bajo «al crear» solo van propiedades: aquí no hace nada.");
            else if (node->key == "is.rating")
                conflict(code, Severity::Warning, node->key, id, "The rating only exists when it's hit: here it's always false.",
                         "El juicio solo existe al tocarla: aquí nunca se cumple.");
            else if (node->key == "is.player")
                conflict(code, Severity::Info, node->key, id, "Only the player misses: here it's always true.",
                         "Solo falla el jugador: aquí siempre se cumple.");
            else
                conflict(code, Severity::Warning, node->key, id, "This block doesn't fit here.", "Este bloque no encaja aquí.");
        } else if (place != 0 && def) {
            const BlockSupport support = blockSupport(engine, node->key);
            if (support.how == Realization::None)
                conflict(code, Severity::Warning, node->key, id, support.noteEn, support.noteEs);
            else if (support.how == Realization::Approx)
                conflict(code, Severity::Info, node->key, id, support.noteEn, support.noteEs);
            if (node->key == "do.sound" && (node->args.empty() || node->args[0].value.empty()))
                conflict(code, Severity::Warning, node->key, id, "Pick a sound: without one, it plays nothing.",
                         "Elige un sonido: sin él, no suena nada.");
            if (node->key == "code.line" && (node->args.size() < 2 || node->args[1].value.empty()))
                conflict(code, Severity::Warning, node->key, id, "Empty code line: it does not export an action.",
                         "Línea de código vacía: no exporta una acción.");
            else if (node->key == "code.line" && node->args[0].value != engineKey(engine))
                conflict(code, Severity::Warning, node->key, id, "This code belongs to another engine: it exports as a comment, not an action.",
                         "Este código pertenece a otro motor: se exporta como comentario, no como acción.");
            if ((node->key == "ctl.if" || node->key == "ctl.ifElse") && !node->args.empty() && node->args[0].block < 0)
                conflict(code, Severity::Info, node->key, id, "Empty condition: it never holds.", "Condición vacía: no se cumple nunca.");
        }
        for (const BlockArg& arg : node->args)
            if (arg.block >= 0)
                for (int child : chainOf(program, arg.block)) visit(child);
    };
    for (int top : program.tops) {
        const BlockNode* head = program.node(top);
        if (!head) continue;
        const BlockDef* def = blockDef(head->key);
        if (!def || def->shape != BlockShape::Hat) {
            conflict(code, Severity::Info, head->key, top, "Loose blocks: they do nothing until they hang from an event.",
                     "Bloques sueltos: no hacen nada hasta que cuelguen de un evento.");
            continue;
        }
        for (int id : chainOf(program, top)) visit(id);
    }
}

bool safeGraph(const BlockProgram& program, BlockCode& code) {
    auto fail = [&](const std::string& key, int id, const char* en, const char* es) {
        conflict(code, Severity::Error, key, id, en, es); return false;
    };
    if (program.nodes.size() > 1024) return fail("", -1, "This program exceeds the 1024-block limit.", "El programa supera el límite de 1024 bloques.");
    std::map<int, int> owners, colors;
    for (const auto& entry : program.nodes) {
        const auto& n = entry.second; const auto* def = blockDef(n.key);
        if (!def || n.args.size() != def->args.size()) return fail(n.key, entry.first, "Invalid block definition or arguments: repair the program before export.", "Definición o argumentos inválidos: repara el programa antes de exportar.");
        auto validLink = [&](int child, ArgKind kind) {
            if (child < 0) return true;
            const auto* target = program.node(child); const auto* childDef = target ? blockDef(target->key) : nullptr;
            if (!childDef || ++owners[child] > 1) return false;
            if (kind == ArgKind::Body) return childDef->shape == BlockShape::Statement || childDef->shape == BlockShape::If || childDef->shape == BlockShape::IfElse;
            return childDef->shape == (kind == ArgKind::Boolean ? BlockShape::Boolean : BlockShape::Number);
        };
        if ((def->shape == BlockShape::Number || def->shape == BlockShape::Boolean || n.key == "var.define") && n.next >= 0)
            return fail(n.key, entry.first, "This block cannot own an action stack.", "Este bloque no puede tener una pila de acciones.");
        if (!validLink(n.next, ArgKind::Body)) return fail(n.key, entry.first, "Broken or multiply-owned action link.", "Enlace de acción roto o con varios propietarios.");
        for (size_t i = 0; i < n.args.size(); ++i)
            if (n.args[i].block >= 0 && (!(def->args[i].plug || def->args[i].kind == ArgKind::Body) || !validLink(n.args[i].block, def->args[i].kind)))
                return fail(n.key, entry.first, "Wrong value type or broken block link.", "Tipo de valor incorrecto o enlace roto.");
    }
    std::set<int> roots;
    for (int top : program.tops) if (!program.node(top) || owners[top] || !roots.insert(top).second)
        return fail("", top, "Invalid program root.", "Raíz del programa inválida.");
    std::function<bool(int, int)> visit = [&](int id, int depth) {
        if (id < 0 || colors[id] == 2) return true;
        if (colors[id] == 1) return fail(program.node(id)->key, id, "Cyclic blocks cannot be exported.", "No se puede exportar un ciclo de bloques.");
        if (depth > 48) return fail(program.node(id)->key, id, "Nested blocks exceed the 48-level limit.", "La anidación supera el límite de 48 niveles.");
        colors[id] = 1;
        const auto& n = *program.node(id);
        for (const auto& arg : n.args) if (!visit(arg.block, depth + 1)) return false;
        if (!visit(n.next, depth)) return false;
        colors[id] = 2; return true;
    };
    for (const auto& entry : program.nodes) if (!visit(entry.first, 0)) return false;
    return true;
}

// ---------------------------------------------------------------- Codename --

BlockCode codename(const std::string& type, const std::string& file, const BlockProgram& program, const TypeLook& look) {
    BlockCode code;
    const std::vector<Prop> props = propsOf(program);
    const Prop* avoid = findProp(props, kAvoid);
    const Prop* causesMiss = findProp(props, kCausesMiss);
    const Prop* hitHealth = findProp(props, kHitHealth);
    const Prop* missHealth = findProp(props, kMissHealth);
    const Prop* noSplash = findProp(props, kNoSplash);
    const Prop* noAnim = findProp(props, kNoAnim);
    const Prop* suffix = findProp(props, kAnimSuffix);
    const Prop* singer = findProp(props, kSinger);
    const Prop* noMissAnim = findProp(props, kNoMissAnim);
    std::string creation, playerHit, noteHit, playerMiss, postCreation;
    if (const Prop* p = findProp(props, "look.alpha")) postCreation += "\tevent.note.alpha *= " + number(p->amount) + ";\n";
    if (const Prop* p = findProp(props, "look.scale")) postCreation += "\tif (!event.note.isSustainNote) { event.note.scale.x *= " + number(p->amount) + "; event.note.scale.y *= " + number(p->amount2) + "; event.note.updateHitbox(); }\n";
    if (const Prop* p = findProp(props, "look.angle")) postCreation += "\tif (!event.note.isSustainNote) event.note.angle += " + number(p->amount) + ";\n";
    if (avoid) creation += "\tevent.note.avoid = true; // the CPU doesn't hit it (Note.hx:42, StrumLine.hx:231)\n";
    if (!look.codenameSplash.empty())
        creation += "\tevent.note.splash = " + hx(look.codenameSplash) + "; // preloaded when it's created (Note.hx:216)\n";
    if (causesMiss) {
        // Los campos que PlayState lee tras el evento (PlayState.hx:2008-2036):
        // el acierto se cuenta como un fallo de noteMiss (:1900-1935).
        playerHit += "\t// Hitting it is a miss (NoteHitEvent.hx:19; PlayState.hx:2008-2036).\n"
                     "\tevent.misses = true;\n"
                     "\tevent.score = -10;\n"
                     "\tevent.accuracy = 0;\n"
                     "\tevent.showRating = false;\n"
                     "\tevent.showSplash = false;\n"
                     "\tevent.preventStrumGlow();\n"
                     "\tevent.preventVocalsUnmute();\n"
                     "\tevent.preventAnim();\n"
                     "\tfor (character in event.characters)\n"
                     "\t\tif (character != null) character.playSingAnim(event.direction, event.animSuffix, \"MISS\");\n"
                     "\tFlxG.sound.play(Paths.soundRandom(\"missnote\", 1, 3), FlxG.random.float(0.1, 0.2));\n";
        playerHit += "\tevent.healthGain = " + number(hitHealth ? hitHealth->amount : -0.0475f) + "; // NoteHitEvent.hx:89\n";
    } else {
        if (hitHealth) playerHit += "\tevent.healthGain = " + number(hitHealth->amount) + "; // NoteHitEvent.hx:89\n";
        if (noSplash) playerHit += "\tevent.showSplash = false; // NoteHitEvent.hx:97\n";
    }
    if (noAnim || suffix || singer) {
        if (causesMiss) noteHit += "\tif (event.player) return; // hitting it is a miss\n";
        if (noAnim) noteHit += "\tevent.preventAnim(); // NoteHitEvent.hx:126\n";
        if (suffix) noteHit += "\tevent.animSuffix = " + hx(suffix->text) + "; // NoteHitEvent.hx:65\n";
        if (singer) noteHit += "\tif (gf != null) event.characters = [gf]; // NoteHitEvent.hx:53\n";
    }
    if (avoid) {
        playerMiss += "\t// Letting it pass is not a miss: cancel it and remove the note (PlayState.hx:1900-1905).\n"
                      "\tevent.cancel();\n"
                      "\tif (event.note != null) event.note.strumLine.deleteNote(event.note);\n";
    } else {
        if (missHealth) playerMiss += "\tevent.healthGain = " + number(missHealth->amount) + "; // NoteMissEvent.hx:23\n";
        if (noMissAnim) playerMiss += "\tevent.preventAnim(); // NoteMissEvent.hx:98\n";
    }

    // Las acciones van en onPostNoteHit / onPostPlayerMiss: despues de que el
    // motor aplique el acierto, como el goodNoteHit de Psych.
    std::map<int, std::string> globals, timers;
    std::string postHit, postMiss;
    for (const Event& event : eventsOf(program)) {
        if (!event.hit && avoid) continue;
        Emit e;
        e.engine = Engine::Codename;
        e.program = &program;
        e.code = &code;
        e.hit = event.hit;
        e.globals = &globals;
        e.timers = &timers;
        e.bounded = needsBudget(program);
        if (event.hit && event.who != "any") {
            e.depth = 2;
            emitChain(e, event.first);
            if (e.out.empty()) continue;
            postHit += std::string("\t// ") + whoComment(event.who, true) + "\n\tif (" + (event.who == "player" ? "event.player" : "!event.player") +
                       ") {\n" + e.out + "\t}\n";
        } else {
            emitChain(e, event.first);
            if (e.out.empty()) continue;
            (event.hit ? postHit : postMiss) += std::string("\t// ") + whoComment(event.who, event.hit) + "\n" + e.out;
        }
    }

    const std::string guard = "\tif (event.noteType != " + hx(type) + ") return;\n";
    std::string body;
    auto function = [&](const char* name, const std::string& lines, const std::string& check) {
        if (lines.empty()) return;
        body += std::string("\nfunction ") + name + "(event) {\n" + check + lines + "}\n";
    };
    function("onNoteCreation", creation, guard);
    function("onPostNoteCreation", postCreation, guard);
    if (const Prop* p = findProp(props, "look.angle")) body += "\nfunction postUpdate(elapsed) {\n\tfor (line in strumLines.members) if (line != null) line.notes.forEach(function(note) {\n\t\tif (note != null && note.noteType == " + hx(type) + " && !note.isSustainNote) {\n\t\t\tvar strum = line.members[note.strumID];\n\t\t\tif (strum != null) note.angle = strum.angle + " + number(p->amount) + ";\n\t\t}\n\t});\n}\n";
    function("onPlayerHit", playerHit, guard);
    function("onNoteHit", noteHit, guard);
    function("onPlayerMiss", playerMiss, guard);
    auto prelude = [&](bool hit) {
        Emit e; e.engine = Engine::Codename; e.program = &program; e.hit = hit;
        eventPrelude(e); return e.out;
    };
    if (!postHit.empty()) postHit = prelude(true) + postHit;
    if (!postMiss.empty()) postMiss = prelude(false) + postMiss;
    function("onPostNoteHit", postHit, "\tif (event.cancelled || event.noteType != " + hx(type) + " || event.note == null || event.note.isSustainNote) return;\n");
    function("onPostPlayerMiss", postMiss,
             "\tif (event.cancelled || event.noteType != " + hx(type) + " || event.note == null || event.note.isSustainNote) return;\n");
    std::string vars;
    for (const auto& entry : globals) vars += entry.second;
    vars += runtimeHelpers(program, Engine::Codename);
    if (needsClock(program)) body += "\nfunction create() { notelabReset(); }\nfunction update(elapsed) { notelabStep(Conductor.songPosition); }\nfunction destroy() { notelabReset(); }\n";
    std::string imports = activeKey(program, "do.video") ? "import hxvlc.flixel.FlxVideoSprite;\n" : "";
    if (activeKey(program, "do.screamer")) imports += "import flixel.sound.FlxSound;\n";
    if (!body.empty()) code.files.push_back({"data/notes/" + file + ".hx", header("//", type, program) + imports + (vars.empty() ? "" : "\n" + vars) + body});
    return code;
}

// ------------------------------------------------------------------- Psych --

BlockCode psych(const std::string& type, const std::string& file, const BlockProgram& program, const TypeLook& look) {
    BlockCode code;
    const std::vector<Prop> props = propsOf(program);
    const Prop* avoid = findProp(props, kAvoid);
    const Prop* causesMiss = findProp(props, kCausesMiss);
    const Prop* hitHealth = findProp(props, kHitHealth);
    const Prop* missHealth = findProp(props, kMissHealth);
    const Prop* noSplash = findProp(props, kNoSplash);
    const Prop* noAnim = findProp(props, kNoAnim);
    const Prop* suffix = findProp(props, kAnimSuffix);
    const Prop* singer = findProp(props, kSinger);
    const Prop* noMissAnim = findProp(props, kNoMissAnim);
    // NoteTypesConfig.hx: `clave: valor` sobre la nota al darle el tipo
    // (Note.hx:229); las cadenas entre comillas.
    std::string txt;
    if (!look.psychTexture.empty()) txt += "texture: " + lua(look.psychTexture) + "\n";
    if (!look.psychSplash.empty()) {
        txt += "noteSplashData.texture: " + lua(look.psychSplash) + "\n";
        if (!look.psychSplashRgb) txt += "noteSplashData.useRGBShader: false\n";
    }
    if (avoid) txt += "ignoreNote: true\n";
    if (causesMiss) txt += "hitCausesMiss: true\n";
    // Con hitCausesMiss el acierto pasa por noteMiss y resta missHealth
    // (PlayState.hx:3115, :2898); si no, suma hitHealth (:3097).
    if (hitHealth) txt += causesMiss ? "missHealth: " + number(-hitHealth->amount) + "\n" : "hitHealth: " + number(hitHealth->amount) + "\n";
    if (missHealth) {
        if (causesMiss && hitHealth)
            conflict(code, Severity::Warning, kMissHealth, missHealth->node,
                     "Psych uses missHealth for both when hitting it is a miss: the health when hit counts.",
                     "Psych usa missHealth para las dos cosas cuando tocarla es un fallo: vale la vida al tocarla.");
        else
            txt += "missHealth: " + number(-missHealth->amount) + "\n";
    }
    if (noSplash) txt += "noteSplashData.disabled: true\n";
    if (noAnim) txt += "noAnimation: true\n";
    if (suffix) txt += "animSuffix: " + lua(suffix->text) + "\n";
    if (singer) txt += "gfNote: true\n";
    if (noMissAnim) txt += "noMissAnimation: true\n";
    if (!txt.empty()) code.files.push_back({"custom_notetypes/" + file + ".txt", txt});

    // Las acciones, en Lua: los scripts de custom_notetypes reciben las
    // llamadas de todas las notas, asi que cada funcion mira el tipo.
    std::map<int, std::string> globals, timers;
    std::string good, opponent, miss;
    const std::string tag = "notelab_" + identifier(file);
    for (const Event& event : eventsOf(program)) {
        if (!event.hit && avoid) continue;
        auto emit = [&](int side, std::string& into) {
            Emit e;
            e.engine = Engine::Psych;
            e.program = &program;
            e.code = &code;
            e.tag = tag;
            e.hit = event.hit;
            e.side = side;
            e.globals = &globals;
            e.timers = &timers;
            e.bounded = needsBudget(program);
            emitChain(e, event.first);
            if (!e.out.empty()) into += std::string("\t-- ") + whoComment(event.who, event.hit) + "\n" + e.out;
        };
        if (!event.hit) emit(3, miss);
        else {
            if (event.who != "opponent") emit(1, good);
            if (event.who != "player") emit(2, opponent);
        }
    }
    std::string script;
    const std::string check = "\tif noteType ~= " + lua(type) + " or isSustainNote then return end\n";
    auto function = [&](const char* name, const std::string& lines) {
        if (lines.empty()) return;
        Emit e; e.engine = Engine::Psych; e.program = &program; e.hit = std::string(name) != "noteMiss"; e.side = std::string(name) == "opponentNoteHit" ? 2 : e.hit ? 1 : 3;
        eventPrelude(e);
        script += std::string("\nfunction ") + name + "(id, direction, noteType, isSustainNote)\n" + check + e.out + lines + "end\n";
    };
    function("goodNoteHit", good);
    function("opponentNoteHit", opponent);
    function("noteMiss", miss);
    std::string spawn;
    if (const Prop* p = findProp(props, "look.alpha")) spawn += "\tsetPropertyFromGroup('notes', id, 'multAlpha', (getPropertyFromGroup('notes', id, 'multAlpha') or 1) * " + number(p->amount) + ")\n";
    if (const Prop* p = findProp(props, "look.scale")) spawn += "\tif not isSustainNote then\n\t\tsetPropertyFromGroup('notes', id, 'scale.x', getPropertyFromGroup('notes', id, 'scale.x') * " + number(p->amount) + ")\n\t\tsetPropertyFromGroup('notes', id, 'scale.y', getPropertyFromGroup('notes', id, 'scale.y') * " + number(p->amount2) + ")\n\tend\n";
    if (const Prop* p = findProp(props, "look.angle")) spawn += "\tif not isSustainNote then setPropertyFromGroup('notes', id, 'offsetAngle', (getPropertyFromGroup('notes', id, 'offsetAngle') or 0) + " + number(p->amount) + ") end\n";
    if (!spawn.empty()) script += "\nfunction onSpawnNote(id, direction, noteType, isSustainNote)\n\tif noteType ~= " + lua(type) + " then return end\n" + spawn + "end\n";
    if (needsClock(program)) script += "\nfunction onCreatePost() notelabReset() end\nfunction onUpdate(elapsed) notelabStep(getSongPosition()) end\nfunction onDestroy() notelabReset() end\n";
    if (!timers.empty()) {
        script += "\nfunction onTimerCompleted(tag, loops, loopsLeft)\n";
        bool first = true;
        for (const auto& [id, lines] : timers) {
            script += std::string(first ? "\tif" : "\telseif") + " tag == " + lua(tag + "_" + std::to_string(id)) + " then\n" + lines;
            first = false;
        }
        script += "\tend\nend\n";
    }
    std::string vars = runtimeHelpers(program, Engine::Psych);
    for (const auto& entry : globals) vars += entry.second;
    if (!script.empty()) code.files.push_back({"custom_notetypes/" + file + ".lua", header("--", type, program) + vars + script});
    return code;
}

// ----------------------------------------------------------------- V-Slice --

BlockCode vslice(const std::string& type, const std::string& file, const BlockProgram& program, const TypeLook& look) {
    BlockCode code;
    const std::vector<Prop> props = propsOf(program);
    const Prop* avoid = findProp(props, kAvoid);
    const Prop* causesMiss = findProp(props, kCausesMiss);
    const Prop* hitHealth = findProp(props, kHitHealth);
    const Prop* missHealth = findProp(props, kMissHealth);
    const Prop* noSplash = findProp(props, kNoSplash);
    const Prop* noAnim = findProp(props, kNoAnim);
    const Prop* suffix = findProp(props, kAnimSuffix);
    const Prop* singer = findProp(props, kSinger);
    // El personaje no canta por su cuenta cuando otro bloque decide lo que
    // hace (BaseCharacter.hx:561, :577).
    const bool noanim = noAnim || singer || causesMiss;
    const std::string suffixText = suffix ? suffix->text : std::string();
    std::set<std::string> imports;
    std::map<int, std::string> globals, timers;
    imports.insert("funkin.play.notes.notekind.NoteKind");
    if (needsContext(program) || needsClock(program)) imports.insert("funkin.Conductor");
    if (needsClock(program)) {
        imports.insert("funkin.modding.events.ScriptEvent");
        imports.insert("funkin.modding.events.ScriptEvent.UpdateScriptEvent");
    }

    // Las acciones de cada evento, antes de que el acierto se convierta en
    // fallo (que termina con return).
    std::string hitActions, missActions;
    for (const Event& event : eventsOf(program)) {
        if (!event.hit && avoid) continue;
        Emit e;
        e.engine = Engine::VSlice;
        e.program = &program;
        e.code = &code;
        e.hit = event.hit;
        e.globals = &globals;
        e.timers = &timers;
        e.imports = &imports;
        e.bounded = needsBudget(program);
        if (event.hit && event.who != "any") {
            e.depth = 3;
            emitChain(e, event.first);
            if (e.out.empty()) continue;
            hitActions += std::string("    // ") + whoComment(event.who, true) + "\n    if (" +
                          (event.who == "player" ? "event.note.noteData.getMustHitNote()" : "!event.note.noteData.getMustHitNote()") + ")\n    {\n" +
                          e.out + "    }\n";
        } else {
            e.depth = 2;
            emitChain(e, event.first);
            if (e.out.empty()) continue;
            (event.hit ? hitActions : missActions) += std::string("    // ") + whoComment(event.who, event.hit) + "\n" + e.out;
        }
    }

    std::string hit, miss;
    if (avoid)
        hit += "    // The machine (the opponent or botplay) hits with 'perfect' (PlayState.hx:2812, :2880): it doesn't hit this one.\n"
               "    // The NoteKind gets the event before the characters (:1513-1525).\n"
               "    if (event.judgement == 'perfect') {\n"
               "      event.cancel();\n"
               "      return;\n"
               "    }\n";
    auto prelude = [&](bool isHit) {
        Emit e; e.engine = Engine::VSlice; e.program = &program; e.hit = isHit; e.depth = 2; e.imports = &imports;
        eventPrelude(e); return e.out;
    };
    if (!hitActions.empty()) hitActions = prelude(true) + hitActions;
    if (!missActions.empty()) missActions = prelude(false) + missActions;
    hit += hitActions;
    if (noSplash) hit += "    event.doesNotesplash = false; // ScriptEvent.hx:159\n";
    if (causesMiss) {
        imports.insert("funkin.play.PlayState");
        imports.insert("funkin.play.scoring.Scoring");
        imports.insert("funkin.audio.FunkinSound");
        imports.insert("funkin.Paths");
        imports.insert("flixel.FlxG");
        if (!hitHealth) imports.insert("funkin.util.Constants");
        hit += "    // Hitting it is a miss, like onNoteMiss (PlayState.hx:3142-3162, :3171-3195).\n"
               "    if (event.judgement != 'perfect') {\n"
               "      event.judgement = 'miss';\n"
               "      event.isComboBreak = true;\n"
               "      event.doesNotesplash = false;\n"
               "      event.score = Scoring.getMissScore();\n"
               "      event.healthChange = " + (hitHealth ? number(hitHealth->amount) : std::string("Constants.HEALTH_MISS_PENALTY")) + ";\n"
               "      var player = PlayState.instance.currentStage.getBoyfriend();\n"
               "      if (player != null) player.playSingAnimation(event.note.noteData.getDirection(), true);\n"
               "      if (PlayState.instance.vocals != null) PlayState.instance.vocals.playerVolume = 0;\n"
               "      FunkinSound.playOnce(Paths.soundRandom('missnote', 1, 3), FlxG.random.float(0.5, 0.6));\n"
               "      return;\n"
               "    }\n";
    } else if (hitHealth) {
        hit += "    if (event.judgement != 'perfect') event.healthChange = " + number(hitHealth->amount) + "; // ScriptEvent.hx:116\n";
    }
    if (singer) {
        imports.insert("funkin.play.PlayState");
        hit += "    // GF sings it (BaseCharacter.hx:714).\n"
               "    var gf = PlayState.instance.currentStage.getGirlfriend();\n"
               "    if (gf != null) {\n"
               "      gf.playSingAnimation(event.note.noteData.getDirection(), false, " + hx(suffixText) + ");\n"
               "      gf.holdTimer = 0;\n"
               "    }\n";
    }
    if (avoid)
        miss += "    // Letting it pass is not a miss (PlayState.hx:2903-2918).\n"
                "    event.note.handledMiss = true;\n"
                "    event.cancel();\n";
    else {
        if (missHealth) miss += "    event.healthChange = " + number(missHealth->amount) + "; // ScriptEvent.hx:116\n";
        miss += missActions;
    }
    const std::string klass = "NoteLab" + identifier(file);
    std::string script = header("//", type, program);
    for (const std::string& import : imports) script += "import " + import + ";\n";
    // NoteKind(id, descripcion, noteStyleId, params, noanim, suffix) (NoteKind.hx:48).
    script += "\nclass " + klass + "NoteKind extends NoteKind\n{\n";
    script += runtimeHelpers(program, Engine::VSlice);
    for (const auto& entry : globals) script += entry.second;
    if (!globals.empty()) script += "\n";
    script += "  public function new()\n  {\n"
              "    super(" + hx(type) + ", " + hx(type + " (Note Lab)") + ", " +
              (look.vsliceStyle.empty() ? std::string("null") : hx(look.vsliceStyle)) + ", null, " + (noanim ? "true" : "false") + ", " +
              (suffix && !noanim ? hx(suffixText) : std::string("null")) + ");\n"
              "  }\n";
    if (!hit.empty())
        script += "\n  public override function onNoteHit(event:HitNoteScriptEvent):Void\n  {\n"
                  "    if (event.eventCanceled) return;\n" + hit + "  }\n";
    if (!miss.empty())
        script += "\n  public override function onNoteMiss(event:NoteScriptEvent):Void\n  {\n"
                  "    if (event.eventCanceled) return;\n" + miss + "  }\n";
    std::string incoming;
    if (const Prop* p = findProp(props, "look.alpha")) incoming += "    event.note.alpha *= " + number(p->amount) + ";\n    if (event.note.holdNoteSprite != null) event.note.holdNoteSprite.alpha *= " + number(p->amount) + ";\n";
    if (const Prop* p = findProp(props, "look.scale")) incoming += "    event.note.scale.x *= " + number(p->amount) + "; event.note.scale.y *= " + number(p->amount2) + "; event.note.updateHitbox();\n";
    if (const Prop* p = findProp(props, "look.angle")) incoming += "    event.note.angle += " + number(p->amount) + ";\n";
    if (!incoming.empty()) script += "\n  public override function onNoteIncoming(event:NoteScriptEvent):Void\n  {\n    if (event.eventCanceled || event.note == null) return;\n" + incoming + "  }\n";
    if (needsClock(program)) script += "\n  public override function onCreate(event:ScriptEvent):Void { notelabReset(); }\n  public override function onUpdate(event:UpdateScriptEvent):Void { notelabStep(Conductor.instance.songPosition); }\n  public override function onDestroy(event:ScriptEvent):Void { notelabReset(); }\n";
    script += "}\n";
    code.files.push_back({"scripts/notekinds/" + file + ".hxc", script});
    return code;
}

}  // namespace

BlockCode generateBlocks(Engine engine, const std::string& type, const std::string& fileName, const BlockProgram& program,
                         const TypeLook& look) {
    BlockCode code;
    if (!safeGraph(program, code)) return code;
    switch (engine) {
        case Engine::Codename: code = codename(type, fileName, program, look); break;
        case Engine::Psych: code = psych(type, fileName, program, look); break;
        case Engine::VSlice: code = vslice(type, fileName, program, look); break;
    }
    checks(code, program, engine);
    for (const auto& item : program.nodes) {
        const BlockNode& node = item.second;
        if (node.key != "code.file" || node.args.size() != 3 || node.args[0].value != engineKey(engine) ||
            std::find(program.tops.begin(), program.tops.end(), item.first) == program.tops.end()) continue;
        const std::string& path = node.args[1].value;
        const bool safeName = !fileName.empty() && fileName != "." && fileName != ".." && fileName.find_first_of("/\\:") == std::string::npos;
        const bool allowed = safeName && (engine == Engine::Codename ? path == "data/notes/" + fileName + ".hx"
            : engine == Engine::Psych ? (path == "custom_notetypes/" + fileName + ".lua" || path == "custom_notetypes/" + fileName + ".txt")
            : path == "scripts/notekinds/" + fileName + ".hxc");
        if (!allowed) {
            conflict(code, Severity::Error, node.key, item.first, "The custom file path no longer matches this type.", "La ruta del archivo propio ya no corresponde a este tipo.");
            continue;
        }
        auto found = std::find_if(code.files.begin(), code.files.end(), [&](const BlockFile& f) { return f.path == path; });
        if (found == code.files.end()) code.files.push_back({path, node.args[2].value});
        else found->text = node.args[2].value;
        conflict(code, Severity::Warning, node.key, item.first, "Custom file overrides generated code. Its behavior is not simulated or compiler-verified.",
                 "El archivo propio reemplaza al generado. Su comportamiento no se simula ni se ha comprobado con el compilador del motor.");
    }
    for (const auto& item : program.drafts)
        if (item.second.engine == engine)
            conflict(code, Severity::Error, "", -1, "Unapplied code draft: apply or discard it before exporting " + item.second.path,
                     "Borrador de código pendiente: aplícalo o descártalo antes de exportar " + item.second.path);
    return code;
}

bool blocksAvoid(const BlockProgram& program) { return findProp(propsOf(program), kAvoid) != nullptr; }
bool blocksHitMisses(const BlockProgram& program) { return findProp(propsOf(program), kCausesMiss) != nullptr; }

BlockAppearance blocksAppearance(const BlockProgram& program) {
    const auto props = propsOf(program);
    BlockAppearance look;
    if (const Prop* p = findProp(props, "look.alpha")) look.alpha = p->amount;
    if (const Prop* p = findProp(props, "look.scale")) { look.scaleX = p->amount; look.scaleY = p->amount2; }
    if (const Prop* p = findProp(props, "look.angle")) look.angle = p->amount;
    return look;
}

int activeBlocks(const BlockProgram& program) {
    int count = 0;
    for (const auto& entry : program.nodes) {
        const unsigned place = placeOf(program, entry.first);
        if (place == kPlaceCreate || place == kPlaceHit || place == kPlaceMiss || (place == kPlaceTop && entry.second.key == "var.define")) ++count;
    }
    return count;
}

std::vector<std::string> programSummary(const BlockProgram& program, bool spanish) {
    std::vector<std::string> lines;
    for (int top : program.tops) {
        const BlockNode* hat = program.node(top);
        if (!hat) continue;
        if (hat->key == "var.define") { lines.push_back(blockText(program, top, spanish)); continue; }
        if (hat->next < 0) continue;
        const BlockDef* def = blockDef(hat->key);
        if (!def || def->shape != BlockShape::Hat) continue;
        std::string text = blockText(program, top, spanish) + ": ";
        bool first = true;
        for (int id : chainOf(program, hat->next)) {
            text += (first ? "" : "; ") + blockText(program, id, spanish);
            first = false;
        }
        lines.push_back(std::move(text));
    }
    return lines;
}

// ---------------------------------------------------------------- presets --

namespace {

int hatOf(BlockProgram& program, const char* key, const char* who = nullptr) {
    const int id = newBlock(program, key);
    if (who) program.node(id)->args[0].value = who;
    placeTop(program, id, 24.0f, 24.0f + 160.0f * static_cast<float>(program.tops.size()));
    return id;
}

int append(BlockProgram& program, int after, const char* key, std::vector<const char*> values = {}) {
    const int id = newBlock(program, key);
    BlockNode& node = *program.node(id);
    for (size_t i = 0; i < values.size() && i < node.args.size(); ++i) node.args[i].value = values[i];
    attachAfter(program, lastOf(program, after), id);
    return id;
}

int inside(BlockProgram& program, int parent, int arg, const char* key, std::vector<const char*> values = {}) {
    const int id = newBlock(program, key);
    BlockNode& node = *program.node(id);
    for (size_t i = 0; i < values.size() && i < node.args.size(); ++i) node.args[i].value = values[i];
    const int first = program.node(parent)->args[static_cast<size_t>(arg)].block;
    if (first < 0) attachBody(program, parent, arg, id);
    else attachAfter(program, lastOf(program, first), id);
    return id;
}

// Un valor enchufado en la ranura `arg` de `parent`.
int plug(BlockProgram& program, int parent, int arg, const char* key, std::vector<const char*> values = {}) {
    const int id = newBlock(program, key);
    BlockNode& node = *program.node(id);
    for (size_t i = 0; i < values.size() && i < node.args.size(); ++i) node.args[i].value = values[i];
    attachValue(program, parent, arg, id, 0.0f, 0.0f);
    return id;
}

int condition(BlockProgram& program, int parent, const char* key, std::vector<const char*> values = {}) {
    return plug(program, parent, 0, key, std::move(values));
}

}  // namespace

const std::vector<BlockPreset>& blockPresets() {
    static const std::vector<BlockPreset> presets = {
        {"hurt", "Hurt", "Daño", "Must be avoided; hitting it is a miss and takes 10 % of the bar (like Psych's Hurt Note).",
         "Hay que evitarla; tocarla es un fallo y quita el 10 % de la barra (como la Hurt Note de Psych).",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kCreate);
             append(p, hat, kAvoid);
             append(p, hat, kCausesMiss);
             append(p, hat, kHitHealth, {"-0.2"});
         }},
        {"instakill", "Instakill", "Muerte instantánea", "Must be avoided; hitting it empties the bar.",
         "Hay que evitarla; tocarla vacía la barra.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kCreate);
             append(p, hat, kAvoid);
             append(p, hat, kCausesMiss);
             append(p, hat, kHitHealth, {"-2"});
         }},
        {"missKills", "Missing kills", "Fallarla mata", "Letting it pass empties the bar.", "Dejarla pasar vacía la barra.",
         [](BlockProgram& p) { append(p, hatOf(p, kCreate), kMissHealth, {"-2"}); }},
        {"heal", "Heals", "Da vida", "Hitting it gives 15 % of the bar.", "Acertarla da el 15 % de la barra.",
         [](BlockProgram& p) { append(p, hatOf(p, kCreate), kHitHealth, {"0.3"}); }},
        {"mine", "Mine", "Mina", "Must be avoided; hitting it takes 15 % without counting as a miss (a V-Slice mine, Constants.hx:522).",
         "Hay que evitarla; tocarla quita el 15 % sin contar como fallo (una mina de V-Slice, Constants.hx:522).",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kCreate);
             append(p, hat, kAvoid);
             append(p, hat, kHitHealth, {"-0.3"});
         }},
        {"poison", "Poison", "Veneno", "Must be avoided; hitting it takes a little health every half second for 4 seconds.",
         "Hay que evitarla; tocarla quita un poco de vida cada medio segundo durante 4 segundos.",
         [](BlockProgram& p) {
             append(p, hatOf(p, kCreate), kAvoid);
             append(p, hatOf(p, kHit, "player"), "do.drain", {"-0.05", "0.5", "8"});
         }},
        {"noAnim", "No animation", "Sin animación", "The character doesn't sing it.", "El personaje no la canta.",
         [](BlockProgram& p) { append(p, hatOf(p, kCreate), kNoAnim); }},
        {"altAnim", "Alternate animation", "Animación alternativa", "Sung with the -alt animations.", "Se canta con las animaciones -alt.",
         [](BlockProgram& p) { append(p, hatOf(p, kCreate), kAnimSuffix, {"-alt"}); }},
        {"gfSing", "GF sings", "Canta GF", "GF sings it.", "La canta GF.", [](BlockProgram& p) { append(p, hatOf(p, kCreate), kSinger); }},
        {"hey", "«Hey!»", "«Hey!»", "«Hey!» from the character on its side and a cheer from GF.", "«Hey!» del personaje de su lado y GF que anima.",
         [](BlockProgram& p) { append(p, hatOf(p, kHit, "any"), "do.hey"); }},
        {"flashShake", "Flash and shake", "Destello y temblor", "Hitting it flashes and shakes the camera.",
         "Tocarla hace un destello y un temblor de cámara.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kHit, "player");
             append(p, hat, "do.flash", {"FFFFFF", "0.3"});
             append(p, hat, "do.shake", {"0.02", "0.2"});
         }},
        {"blackout", "Blackout", "Apagón", "Must be avoided; hitting it hides the arrows for a second.",
         "Hay que evitarla; tocarla oculta las flechas un segundo.",
         [](BlockProgram& p) {
             append(p, hatOf(p, kCreate), kAvoid);
             append(p, hatOf(p, kHit, "player"), "do.hideHud", {"1"});
         }},
        {"sickBonus", "Sick bonus", "Premio por sick", "A «sick» on it gives 500 extra points.", "Un «sick» en ella da 500 puntos de más.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kHit, "player");
             const int check = append(p, hat, "ctl.if");
             condition(p, check, "is.rating", {"sick"});
             inside(p, check, 1, "do.score", {"500"});
         }},
        {"coin", "Coin toss", "Moneda al aire", "Half the times it gives health and the other half it takes it.",
         "La mitad de las veces da vida y la otra mitad la quita.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kHit, "player");
             const int check = append(p, hat, "ctl.ifElse");
             condition(p, check, "op.chance", {"50"});
             inside(p, check, 1, "do.health", {"0.3"});
             inside(p, check, 2, "do.health", {"-0.3"});
         }},
        // Avanzados: condiciones, valores y varios eventos a la vez.
        {"vampire", "Vampire", "Vampiro", "The player steals health by hitting it; when the opponent hits it, it steals from the player.",
         "El jugador roba vida al tocarla; cuando la toca el rival, se la roba al jugador.",
         [](BlockProgram& p) {
             append(p, hatOf(p, kHit, "player"), "do.health", {"0.08"});
             const int theirs = hatOf(p, kHit, "opponent");
             append(p, theirs, "do.health", {"-0.08"});
             append(p, theirs, "do.flash", {"8B0000", "0.2"});
         },
         true},
        {"roulette", "Roulette", "Ruleta", "10 %: full health; 20 %: loses a quarter of the bar; otherwise, 100 points.",
         "10 %: vida llena; 20 %: pierde un cuarto de la barra; si no, 100 puntos.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kHit, "player");
             const int jackpot = append(p, hat, "ctl.ifElse");
             condition(p, jackpot, "op.chance", {"10"});
             inside(p, jackpot, 1, "do.setHealth", {"2"});
             inside(p, jackpot, 1, "do.flash", {"12FA05", "0.4"});
             const int bad = inside(p, jackpot, 2, "ctl.ifElse");
             condition(p, bad, "op.chance", {"20"});
             inside(p, bad, 1, "do.health", {"-0.5"});
             inside(p, bad, 1, "do.flash", {"F9393F", "0.4"});
             inside(p, bad, 2, "do.score", {"100"});
         },
         true},
        {"jumpscare", "Jumpscare", "Susto", "Must be avoided; hitting it flashes, shakes, zooms and hides the arrows for half a second.",
         "Hay que evitarla; tocarla da un destello, tiembla, hace zoom y oculta las flechas medio segundo.",
         [](BlockProgram& p) {
             append(p, hatOf(p, kCreate), kAvoid);
             const int hat = hatOf(p, kHit, "player");
             append(p, hat, "do.flash", {"FFFFFF", "0.5"});
             append(p, hat, "do.shake", {"0.05", "0.5"});
             append(p, hat, "do.zoom", {"0.1"});
             append(p, hat, "do.hideHud", {"0.5"});
         },
         true},
        {"perfectRhythm", "Perfect rhythm", "Ritmo perfecto", "A «sick» gives health and bumps the camera; anything else takes health.",
         "Un «sick» da vida y mueve la cámara; cualquier otro juicio quita vida.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kHit, "player");
             const int check = append(p, hat, "ctl.ifElse");
             condition(p, check, "is.rating", {"sick"});
             inside(p, check, 1, "do.health", {"0.1"});
             inside(p, check, 1, "do.zoom", {"0.03"});
             inside(p, check, 2, "do.health", {"-0.1"});
         },
         true},
        {"streak", "Streak", "Racha", "With a combo over 10 and a «sick», it gives the combo times 10 in points.",
         "Con un combo de más de 10 y un «sick», da el combo por 10 en puntos.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kHit, "player");
             const int check = append(p, hat, "ctl.if");
             const int both = condition(p, check, "op.and");
             const int bigCombo = plug(p, both, 0, "op.compare", {"0", "gt", "10"});
             plug(p, bigCombo, 0, "get.combo");
             plug(p, both, 1, "is.rating", {"sick"});
             const int points = inside(p, check, 1, "do.score");
             const int times = plug(p, points, 0, "op.math", {"0", "mul", "10"});
             plug(p, times, 0, "get.combo");
         },
         true},
        {"doubleOrNothing", "Double or nothing", "Doble o nada", "Half the times it doubles the score and the other half it leaves it at 0.",
         "La mitad de las veces duplica los puntos y la otra mitad los deja en 0.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kHit, "player");
             const int check = append(p, hat, "ctl.ifElse");
             condition(p, check, "op.chance", {"50"});
             const int doubled = inside(p, check, 1, "do.setScore");
             const int times = plug(p, doubled, 0, "op.math", {"0", "mul", "2"});
             plug(p, times, 0, "get.score");
             inside(p, check, 2, "do.setScore", {"0"});
         },
         true},
        {"laneColors", "Lane colors", "Colores por carril", "A flash with the color of the lane it's in.",
         "Un destello con el color del carril en el que está.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kHit, "player");
             const char* lanes[4][2] = {{"left", "C24B99"}, {"down", "00FFFF"}, {"up", "12FA05"}, {"right", "F9393F"}};
             for (const auto& lane : lanes) {
                 const int check = append(p, hat, "ctl.if");
                 condition(p, check, "is.direction", {lane[0]});
                 inside(p, check, 1, "do.flash", {lane[1], "0.25"});
             }
         },
         true},
        {"lastChance", "Last chance", "Última oportunidad", "Missing it with little health left leaves you at a quarter of the bar instead of dying.",
         "Fallarla con poca vida te deja en un cuarto de la barra en vez de morir.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kMiss);
             const int check = append(p, hat, "ctl.if");
             const int low = condition(p, check, "op.compare", {"0", "lt", "0.5"});
             plug(p, low, 0, "get.health");
             inside(p, check, 1, "do.setHealth", {"0.5"});
             inside(p, check, 1, "do.flash", {"F9393F", "0.3"});
         },
         true},
        {"missPenalty", "Harsh miss", "Fallo castigado", "Missing it takes 500 points, breaks the combo and shakes the camera.",
         "Fallarla quita 500 puntos, rompe el combo y hace temblar la cámara.",
         [](BlockProgram& p) {
             const int hat = hatOf(p, kMiss);
             append(p, hat, "do.score", {"-500"});
             append(p, hat, "do.combo", {"0"});
             append(p, hat, "do.shake", {"0.03", "0.3"});
         },
         true},
    };
    return presets;
}

void addPreset(BlockProgram& program, const BlockPreset& preset) {
    BlockProgram made;
    preset.build(made);
    float bottom = 24.0f;
    for (int top : program.tops)
        if (const BlockNode* node = program.node(top))
            bottom = std::max(bottom, node->y + 48.0f * static_cast<float>(chainOf(program, top).size()) + 40.0f);
    for (int top : made.tops) {
        const BlockNode& hat = *made.node(top);
        const std::string who = hat.args.empty() ? std::string() : hat.args[0].value;
        // El mismo evento que ya este en el lienzo.
        int target = -1;
        for (int existing : program.tops) {
            const BlockNode* node = program.node(existing);
            if (node && node->key == hat.key && (hat.key != kHit || (!node->args.empty() && node->args[0].value == who))) {
                target = existing;
                break;
            }
        }
        if (target < 0) {
            const int copy = copyStackFrom(program, made, top);
            placeTop(program, copy, 24.0f, bottom);
            bottom += 48.0f * static_cast<float>(chainOf(made, top).size()) + 40.0f;
            continue;
        }
        for (int id : chainOf(made, hat.next)) {
            const BlockNode& node = *made.node(id);
            // Una propiedad sustituye a la de la misma clave.
            if (hat.key == kCreate)
                for (int old : chainOf(program, program.node(target)->next))
                    if (program.node(old)->key == node.key) {
                        removeBlock(program, old);
                        break;
                    }
            attachAfter(program, lastOf(program, target), copyTree(program, made, id));
        }
    }
}

}  // namespace fml::notelab

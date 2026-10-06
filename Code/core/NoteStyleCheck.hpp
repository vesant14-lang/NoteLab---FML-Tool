// fml_notelab — Validador de estilos de notas.
//
// Comprueba cada pieza contra su atlas como lo haria el motor: Flixel busca los
// fotogramas cuyo nombre EMPIEZA por el prefijo (`findByPrefix`), sin recortar
// espacios. Una pieza sin fotogramas no da error en ningun motor: simplemente no
// se dibuja, y por eso aqui es un problema (DESIGN_PLUGIN_NOTE_LAB §7.1).
#pragma once

#include "NoteStyleRead.hpp"

#include <string>
#include <vector>

namespace fml { class Vfs; }

namespace fml::notelab {

struct PartResult {
    int frames = 0;          // fotogramas que el motor encontraria
    std::string matched;     // prefijo que los dio: el principal o una alternativa
};

struct StyleReport {
    std::string style;                // id del estilo
    std::vector<PartResult> parts;    // mismo orden que NoteStyle::parts
    int partsResolved = 0;
    int partsInherited = 0;
    int hudExpected = 0, hudFound = 0;
    std::vector<Finding> findings;
};

StyleReport checkNoteStyle(const Vfs& vfs, const NoteStyle& style, AtlasCache& atlases);
std::vector<StyleReport> checkCatalog(const Vfs& vfs, const Catalog& catalog);

// Lo de otro motor dentro de una instalacion (los scripts de Psych que FML
// guarda en una de Codename, el `NOTE_assets` que aun trae V-Slice) no lo carga
// el motor de esa instalacion: sus fallos pasan a informacion, con el motivo
// (FML-NOTE-020). `engine` es el de la instalacion, detectado o elegido.
void settleOtherEngines(const Catalog& catalog, std::vector<StyleReport>& reports, Engine engine);

// «note/left», «splash/up#2», «countdown/two»...
std::string partSubject(const PartBinding& binding);

// Texto del hallazgo, en ingles o en espanol.
std::string describe(const Finding& finding, bool spanish);

// Nombres para personas de lo que el nucleo llama con claves: `note/left` es
// «Nota ← izquierda» y `strumStatic/left` «Receptor en reposo ← izquierda».
const char* partLabel(Part part, bool spanish);
const char* partHelp(Part part, bool spanish);           // que es, en una frase
std::string directionLabel(int direction, bool spanish);  // "← izquierda"
const char* judgementLabel(int index, bool spanish);      // "Sick!", "Bien"...
const char* countdownLabel(int index, bool spanish);      // "3", "2", "1", "¡Ya!"
// El sujeto de un hallazgo en palabras: «Nota ← izquierda», «sonido de la
// cuenta atras «3»», «hoja «shared:notes»»...
std::string readableSubject(const std::string& subject, bool spanish);

}  // namespace fml::notelab

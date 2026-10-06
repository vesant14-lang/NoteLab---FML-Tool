// fml_notelab — El juego base de cada motor, buscado solo (DESIGN_PLUGIN_NOTE_LAB §30).
//
// Un mod trae lo que cambia; lo demas (el skin por defecto, las salpicaduras,
// la cuenta atras, el estilo `funkin` en el que caen los de V-Slice) lo pone
// la instalacion del motor en la que se juega. Sin ella, un mod de Codename sin
// skin propio no tiene nada que dibujar y uno de V-Slice pierde lo que hereda.
// Aqui solo se mira el disco: nunca se escribe.
#pragma once

#include "NoteStyleRead.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fml::notelab {

// Lo leido ya trae lo que el motor carga por defecto: su skin (Codename y
// Psych) o el notestyle `funkin`, en el que caen los de V-Slice. Entonces no
// hace falta juego base; la build de un mod suele traer el motor entero, a
// veces una carpeta mas abajo de la elegida.
bool hasEngineDefault(const Catalog& catalog, Engine engine);

// La carpeta tiene lo que el motor carga por defecto, su skin de notas:
//  - Codename: assets/images/game/notes/default.xml (Note.hx:156-158);
//  - Psych: noteSkins/NOTE_assets.xml (0.7 y 1.0, Note.hx:77) o NOTE_assets.xml
//    (0.6 y anteriores), en assets/shared/images/ o en assets/images/;
//  - V-Slice: el notestyle `funkin` (NoteStyleRegistry.hx:23) en
//    assets/data/notestyles/ (la build), assets/preload/data/notestyles/ (el
//    codigo fuente) o assets/gameplay/notestyles/funkin/ (la 0.9 de prueba).
bool isEngineInstall(const std::filesystem::path& folder, Engine engine);

// Y es el motor tal cual, no un mod que se reparte con el motor dentro y sus
// archivos cambiados: lleva el ejecutable oficial (CodenameEngine.exe,
// PsychEngine.exe, Funkin.exe) o es el codigo fuente del motor.
bool isOfficialInstall(const std::filesystem::path& folder, Engine engine);

// Como se encontro el juego base de un mod.
enum class BaseFound {
    None,
    User,        // lo eligio la persona
    Contains,    // el mod esta dentro de esa instalacion (`<motor>/mods/<mod>`)
    Remembered,  // una instalacion oficial ya usada antes
    Nearby,      // una instalacion oficial junto al mod
};
const char* baseFoundKey(BaseFound how);   // "user", "contains", "remembered", "nearby"

struct BaseSearch {
    std::filesystem::path folder;
    BaseFound how = BaseFound::None;
};

// Busca, en este orden, la instalacion que contiene al mod, las ya usadas
// (`remembered`, la mas reciente primero) y las oficiales que hay junto al mod:
// en la carpeta que lo contiene y en la de encima, hasta dos niveles por
// debajo de cada una. Entre varias de junto al mod gana una build (con el
// ejecutable) antes que el codigo fuente y, entre iguales, la de skin mas
// reciente. `modRoot` puede ser un ZIP.
BaseSearch findEngineBase(const std::filesystem::path& modRoot, Engine engine,
                          const std::vector<std::filesystem::path>& remembered);

// Lo que se monta del juego base: su carpeta `assets/`, como la pone el motor
// debajo de un mod (FML monta igual `assets/` y `mods/<mod>`, Vfs.hpp). Asi no
// se cuelan los otros mods de su `mods/` y lo que el mod sobrescribe tapa lo
// del juego base. Sin `assets/`, la carpeta entera.
std::filesystem::path baseMountOf(const std::filesystem::path& base);

// Una carpeta de mod trae al menos una de estas cosas (como FML,
// CodenameAdapter.cpp:84-93): no todos traen canciones.
bool looksLikeModFolder(const std::filesystem::path& folder);

// Lo que se abre de verdad al elegir una carpeta, como lo monta FML
// (CodenameAdapter.cpp:103-140 y :780-905) y como lo carga el motor:
//  - una instalacion (`assets/` y `mods/`) con mods dentro abre uno de ellos
//    (el pedido o el primero) con su `assets/` debajo;
//  - un mod dentro de `<instalacion>/mods/` lleva debajo esa instalacion;
//  - lo demas se abre tal cual.
// Antes, una carpeta que solo contiene otra con un mod o una instalacion (lo
// que deja descomprimir un ZIP en una carpeta con su nombre) se abre por dentro.
// `packs` son, para Codename, las carpetas que el mod suma encima de si mismo
// como bibliotecas: sus `content/<paquete>` en el orden de
// `content/order.txt` y despues los demas por orden alfabetico (las registra
// `Paths.assetsTree.addLibrary`, que pone delante la ultima,
// AssetsLibraryList.hx:272), y los addons `[HIGH]` y normales de la
// instalacion y del mod. `lowPacks`, los addons `[LOW]`, van debajo del mod.
struct OpenLayout {
    std::filesystem::path mod;                    // lo que se abre como mod
    std::filesystem::path install;                // la instalacion que lo contiene; vacia = ninguna
    std::vector<std::string> mods;                // los mods de esa instalacion, por nombre
    std::vector<std::filesystem::path> packs;     // encima del mod (Codename)
    std::vector<std::filesystem::path> lowPacks;  // debajo del mod (Codename)
};
OpenLayout openLayoutOf(const std::filesystem::path& root, const std::string& preferredMod = {});

}  // namespace fml::notelab

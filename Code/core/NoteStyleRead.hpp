// fml_notelab — Lectores de estilos de notas de Codename, Psych y V-Slice.
//
// Buscan en TODO lo montado y a cualquier profundidad: la raiz elegida puede ser
// un mod suelto, el juego entero con assets/ y mods/, o un ZIP. Cada estilo
// guarda su `scope`, la carpeta que hace de raiz de su contenido (`assets/`,
// `assets/shared/`, `mods/Foo/` o vacia), y sus rutas se resuelven primero ahi.
// Nunca escriben.
#pragma once

#include "NoteStyle.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace fml { class Vfs; }

namespace fml::notelab {

// Rutas del motor -> rutas virtuales con el caso real del disco. Compara sin
// mayusculas, como Windows, que es donde corren los tres motores.
class AssetIndex {
public:
    explicit AssetIndex(const Vfs& vfs);

    // La ruta tal cual, o vacia.
    std::string exact(const std::string& virtualPath) const;
    // `rel` empieza por `images/`, `sounds/` o `<lib>/images/`...: primero en el
    // scope pedido y, si no esta, en cualquier otro (el juego base, otro mod).
    std::string find(const std::string& scope, const std::string& rel) const;
    // Como `find`, probando extensiones de audio.
    std::string findSound(const std::string& scope, const std::string& relNoExt) const;
    // Otro archivo con el mismo nombre en la misma instalacion (la primera
    // carpeta del scope), para decir donde lo tiene un motor modificado.
    std::string sameName(const std::string& scope, const std::string& name) const;

    const std::vector<std::string>& files() const { return m_files; }

private:
    std::vector<std::string> m_files;
    std::unordered_map<std::string, std::string> m_byLower;
    // "images/..." y "sounds/..." en minusculas -> rutas completas que acaban asi.
    std::unordered_map<std::string, std::vector<std::string>> m_bySegment;
    std::unordered_map<std::string, std::vector<std::string>> m_byName;
};

// V-Slice: `lib:ruta` -> rutas candidatas relativas al scope (NoteStyle.hx:103-107,
// :216-220). `default:` es `images/` de la raiz; sin biblioteca, Paths busca en
// `shared` y despues en la de precarga.
std::vector<std::string> vsliceCandidates(const std::string& declared, const char* folder,
                                          const char* extension);

// Nombres de fotograma de un atlas Sparrow (.xml) o Packer (.txt), leidos una vez.
class AtlasCache {
public:
    const std::vector<std::string>* names(const Vfs& vfs, const std::string& atlasPath);
private:
    std::map<std::string, std::vector<std::string>> m_names;
    std::map<std::string, bool> m_failed;
};

// Fotogramas cuyo nombre EMPIEZA por el prefijo, como `findByPrefix` de Flixel.
int countPrefix(const std::vector<std::string>& names, const std::string& prefix);

struct Catalog {
    std::vector<NoteStyle> styles;
    std::vector<Finding> findings;   // de lectura: JSON roto, atributo ausente...
    // Codename: los tipos de nota que piden los charts (`noteTypes`,
    // ChartData.hx:12), como los escriben, sin repetir.
    std::vector<std::string> chartTypes;
};

Catalog scanNoteStyles(const Vfs& vfs);

// Motor de lo abierto, por lo que contiene: como Atlas, que lo deduce del
// formato de cada definicion y deja cambiarlo. Cada marca cuenta una vez, no
// por archivo, para que un mod con mil charts no pese mas que su estructura.
struct EngineGuess {
    bool found = false;
    Engine engine = Engine::Codename;
    std::array<int, 3> score{};              // Codename, Psych, V-Slice
    std::vector<std::string> evidence;       // marcas del motor elegido
    // Otro motor, ninguno de los tres ("Leather Engine", "Kade Engine"), cuando
    // sus marcas pesan mas. Note Lab es solo para los tres: un mod asi no se
    // abre y se dice por que (DESIGN_PLUGIN_NOTE_LAB §30).
    std::string other;
    std::vector<std::string> otherEvidence;
};
EngineGuess guessEngine(const Vfs& vfs, const Catalog& catalog);

// La expresion que un script asigna a una propiedad, leida como texto y sin
// ejecutar nada: Lua `setPropertyFromGroup(..., 'clave', <valor>)` o HScript
// `x.clave = <valor>`. La primera que encuentra, sin espacios a los lados;
// vacia si el script no la asigna (leerla con un getter no cuenta).
std::string scriptAssignment(const std::string& text, const std::string& key);
// El texto entre comillas de una expresion como 'BULLET_assets'; vacio si no lo es.
std::string quotedValue(const std::string& expression);
// Psych: el valor de una propiedad en el .txt de un tipo de nota, una por
// linea como `propiedad: valor` o `propiedad = valor` (NoteTypesConfig.hx:24-44).
std::string psychConfigValue(const std::string& text, const std::string& key);

// La ruta pasa por una carpeta oculta (`.git`, `.fml` con copias de
// seguridad): ningun motor carga de ahi.
bool insideHiddenFolder(const std::string& path);

}  // namespace fml::notelab

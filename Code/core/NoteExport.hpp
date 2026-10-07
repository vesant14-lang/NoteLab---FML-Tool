// fml_notelab — Exportar un estilo de notas a Codename, Psych o V-Slice
// (DESIGN_PLUGIN_NOTE_LAB §20, §21 y §25.5).
//
// Del modelo neutro sale un paquete con la forma de un mod del motor de
// destino: atlas reempaquetados con los nombres que ese motor busca, los
// archivos que los declaran y el HUD. Lo que el destino no puede decir (un FPS
// fijo, una pieza que no tiene, la paleta RGB de Psych) queda anotado, y la
// guia de instalacion se genera del paquete en ingles y espanol.
//
// Nunca escribe dentro de un mod: el paquete va a una carpeta nueva o a un ZIP,
// y los dos se arman en un temporal al lado antes de publicarse enteros.
#pragma once

#include "NoteBlocks.hpp"
#include "NoteStyle.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fml::notelab {

// Para que lo usa el motor de destino.
enum class ExportRole {
    ModSkin,     // el skin por defecto del mod
    Selectable,  // Psych: uno mas en Opciones > Visuales (list.txt)
    SongSkin,    // lo pide una cancion: Psych `arrowSkin`, V-Slice `playData.noteStyle`
    NoteType,    // el aspecto de un tipo de nota
};
const char* exportRoleKey(ExportRole role);
bool exportRoleAvailable(Engine target, ExportRole role);
// El motor guarda los receptores en el mismo atlas que las notas: van siempre.
bool exportStrumsShareAtlas(Engine target, ExportRole role);

struct ExportOptions {
    Engine target = Engine::Codename;
    ExportRole role = ExportRole::ModSkin;
    std::string name;       // se limpia con exportName: carpeta, archivos, id y list.txt
    std::string title;      // como se ve en el motor y en la guia
    std::string author;
    std::string noteType;   // ExportRole::NoteType: el tipo tal como lo escribe el chart
    bool notes = true;      // notas y sostenidos (y los receptores si comparten atlas)
    bool strums = true;     // V-Slice: los receptores van en su propio atlas
    bool splashes = true;
    bool holdCovers = true; // solo V-Slice los tiene
    bool hud = true;
    // ExportRole::NoteType: su programa de bloques (NoteBlocks.hpp) va en los
    // mismos archivos que el aspecto, y los archivos que ya tiene el mod para
    // ese tipo (rutas acabadas como `custom_notetypes/<tipo>.lua`) se avisan.
    BlockProgram blocks;
    std::vector<std::string> existingFiles;
};

struct ExportFile {
    std::string path;                  // relativa a la raiz del paquete, con '/'
    std::vector<unsigned char> bytes;
};

// Algo que el paquete cambia, deja fuera o no puede decir en el destino. El
// texto se compone con describeExportNote, en ingles o en espanol.
struct ExportNote {
    Severity severity = Severity::Info;
    std::string code;      // FML-EXPORT-###
    std::string subject;   // "note/left", "splash", "judgement/sick" (como los hallazgos)
    std::string detail;
    std::string textEn, textEs;   // un choque de bloques trae su texto hecho
};

struct ExportPackage {
    Engine source = Engine::Codename;
    ExportOptions options;
    std::string folder;                // nombre de la carpeta del paquete
    std::string styleName;             // el estilo de origen, para la guia
    std::vector<ExportFile> files;
    std::vector<ExportNote> notes;
    int frames = 0, atlases = 0, images = 0, sounds = 0;
    // Lo que Note Lab lee al abrir el paquete con sus propios lectores
    // (verifyExport): «estructura verificada» del §13.
    bool verified = false;
    int verifyErrors = -1;
    std::vector<std::string> verifiedStyles;
    bool hasErrors() const;
};

// Como leer los archivos del estilo: la VFS del mod, con lo importado montado.
struct ExportIo {
    std::function<std::optional<std::vector<unsigned char>>(const std::string&)> readBytes;
    std::function<std::optional<std::string>(const std::string&)> readText;
};

// Minusculas, cifras, `-` y `_`; lo demas pasa a `-`. Asi el nombre de list.txt
// y el sufijo del archivo coinciden en Psych 0.7 y 1.0 (Note.hx:429,
// NoteSplash.hx:366).
std::string exportName(const std::string& text);
bool noteExportNamesCollide(const std::string& first, const std::string& second);

ExportPackage buildExport(const NoteStyle& style, const ExportOptions& options, const ExportIo& io);

// Escribe el paquete en `scratch`, lo lee con scanNoteStyles/checkCatalog y
// comprueba que salen los estilos esperados sin errores. Borra lo que escribio.
bool verifyExport(ExportPackage& package, const std::filesystem::path& scratch);

// Anade LEEME_INSTALAR.txt, INSTALL.txt y notelab-export.json (la marca que
// permite reemplazar una exportacion anterior y nada mas).
void addInstallGuides(ExportPackage& package);
std::string installGuide(const ExportPackage& package, bool spanish);
std::string describeExportNote(const ExportNote& note, bool spanish);

// `folder` es la carpeta final del paquete. Si ya existe solo se reemplaza si
// es una exportacion de Note Lab (tiene notelab-export.json).
bool writeExportFolder(const ExportPackage& package, const std::filesystem::path& folder,
                       std::string& error, bool spanish);
bool writeExportZip(const ExportPackage& package, const std::filesystem::path& zip,
                    std::string& error, bool spanish);

}  // namespace fml::notelab

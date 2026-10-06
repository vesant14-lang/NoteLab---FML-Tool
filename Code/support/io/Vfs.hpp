// fml_io — VFS de pila de raices.
//
// Tres requisitos que salieron de medir el corpus, no de suponer:
//
//   CORPUS §3  — `mods/<Mod>/` y `assets/` COEXISTEN con prioridad. No es una
//                carpeta raiz, es una pila: la primera que resuelve, gana.
//   CORPUS §20 — Hay espacios en nombres de carpeta, de archivo y DENTRO de
//                atributos XML que apuntan a assets. La ruta es un dato opaco:
//                jamas se construye un comando de shell con ella.
//   DESIGN §1.3 — Resolucion case-insensitive, reportando el desajuste como
//                warning (los modders trabajan en Windows, el motor a veces no).
#pragma once

#include "../core/Model.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fml {

class Vfs {
public:
    struct Root {
        std::filesystem::path path;
        std::string           label;   // "mod", "assets", ...
        size_t                files = 0;
        // Las capas del mod (incluidos content/<paquete>) son editables. La
        // raiz base tambien puede ser el destino cuando el usuario abre SOLO
        // el juego base; en ese caso cada guardado sigue pasando por
        // SafeWriter y conserva una copia de seguridad. Con un mod montado,
        // la base vuelve a ser fallback de solo lectura y el mod recibe los
        // overrides.
        bool                  writable = false;
        MountProviderKind     kind = MountProviderKind::Directory;
        std::filesystem::path cachePath; // solo ZIP; nunca es proveedor de escritura
        std::string           fingerprint;
    };

    struct Conflict {
        std::string          virtualPath;
        size_t                winnerRootIndex = 0;
        size_t                shadowedRootIndex = 0;
        std::filesystem::path winner;
        std::filesystem::path shadowed;
    };

    struct Entry {
        std::filesystem::path real;
        std::string           virtualPath;  // con el caso REAL del disco, separador '/'
        size_t                rootIndex = 0;
        bool                  isDir = false;
        std::uintmax_t        size = 0;
        std::uintmax_t        compressedSize = 0;
        std::uint32_t         archiveFileIndex = UINT32_MAX;
        bool                  fromArchive = false;
        size_t                discoveryOrder = 0;
    };

    struct MountIssue {
        std::string code;
        std::string message;
        std::string path;
    };

    struct ZipLimits {
        size_t maxEntries = 100000;
        std::uint64_t maxFileBytes = 512ull * 1024ull * 1024ull;
        std::uint64_t maxTotalBytes = 16ull * 1024ull * 1024ull * 1024ull;
        double maxCompressionRatio = 1000.0;
    };

    // Indexa la raiz entera de una pasada. Las raices empujadas antes tienen
    // MAS prioridad. Devuelve cuantos archivos indexo.
    size_t pushRoot(const std::filesystem::path& path, std::string label,
                    MountProviderKind kind = MountProviderKind::Directory);
    bool pushFile(const std::filesystem::path& path, const std::string& virtualPath,
                  std::string label);
    size_t pushZip(const std::filesystem::path& path, std::string label,
                   const ZipLimits& limits = ZipLimits{});

    const std::vector<Root>& roots() const { return m_roots; }

    // La raiz de MAS prioridad no es necesariamente donde hay que escribir.
    // Un mod con paquetes de contenido (`content/<pack>/`) los registra por
    // encima de su propia carpeta, pero el contenido nuevo va al mod, no a un
    // paquete. Quien monta la pila decide cual es la raiz de escritura.
    void   setWriteRoot(size_t index) { if (index < m_roots.size()) m_writeRoot = index; }
    size_t writeRootIndex() const { return m_writeRoot; }

    void setRootWritable(size_t index, bool writable = true) {
        if (index < m_roots.size()) m_roots[index].writable = writable;
    }
    bool isRootWritable(size_t index) const {
        return index < m_roots.size() && m_roots[index].writable;
    }

    // Si el archivo viene de una capa editable se escribe ahi. Si solo existe
    // en una capa de lectura, se crea un override en la raiz de escritura
    // activa (normalmente un mod; en modo solo juego base, `assets/`).
    std::optional<std::filesystem::path> resolveForWrite(const std::string& virtualPath) const;

    // Notifica una escritura que ya fue confirmada de forma atomica. Una VFS
    // congelada no acepta proveedores nuevos, pero si debe empezar a resolver
    // inmediatamente el override que acaba de aparecer en su raiz editable.
    // Sin esto, la siguiente recarga de stage/chart seguia leyendo la copia de
    // assets hasta reabrir todo el proyecto y daba la impresion de que Guardar
    // no habia hecho nada.
    bool notifyFileWritten(const std::string& virtualPath);

    std::optional<Entry>     find(const std::string& virtualPath) const;
    std::vector<Entry>       findAll(const std::string& virtualPath) const;
    std::vector<Entry>       allEntries(bool includeDirectories = false) const;
    std::optional<std::filesystem::path> resolve(const std::string& virtualPath) const;
    std::optional<std::filesystem::path> resolve(const Entry& entry) const;
    bool                     exists(const std::string& virtualPath) const;

    // Lista entradas dentro de un directorio virtual.
    std::vector<Entry> listDir(const std::string& virtualDir, bool recursive = false) const;
    // Igual, filtrando por extension (sin punto, case-insensitive: "xml").
    std::vector<Entry> listFiles(const std::string& virtualDir,
                                 const std::string& ext,
                                 bool recursive = false) const;

    std::optional<std::string> readText(const std::string& virtualPath) const;
    std::optional<std::string> readText(const Entry& entry) const;
    std::optional<std::vector<unsigned char>> readBytes(const std::string& virtualPath,
                                                        size_t maxBytes = SIZE_MAX) const;
    std::optional<std::vector<unsigned char>> readBytes(const Entry& entry,
                                                        size_t maxBytes = SIZE_MAX) const;

    size_t fileCount() const { return m_fileCount; }
    size_t dirCount()  const { return m_index.size() - m_fileCount; }

    // Rutas pedidas con un caso distinto al del disco. Material del Mod Doctor.
    const std::set<std::string>& caseMismatches() const { return m_caseMismatches; }
    const std::vector<Conflict>& conflicts() const { return m_conflicts; }
    const std::vector<MountIssue>& issues() const { return m_issues; }

    // Texto en UTF-8 siempre: un archivo que no lo es (un XML o JSON de un mod
    // guardado en ANSI, una "ñ" en un nombre) se lee como Windows-1252. Sin
    // esto esos bytes llegaban hasta `json::dump`, que lanza. Opcional: quien
    // compara o reescribe los bytes originales (FML) lo deja apagado.
    void setUtf8Text(bool enabled) { m_utf8Text = enabled; }
    bool utf8Text() const { return m_utf8Text; }

    // Tope de entradas al indexar una carpeta (0 = sin tope). Elegir por error
    // C:\ o toda la carpeta de Descargas dejaba la ventana congelada minutos
    // indexando cientos de miles de archivos. Al pasarse, el indice se detiene
    // y queda el aviso FML-VFS-006. La profundidad tiene su propio tope fijo
    // (una union de carpetas que apunta a su padre no termina nunca).
    void setScanLimit(size_t maxEntries) { m_scanLimit = maxEntries; }
    bool scanLimitReached() const { return m_scanLimitReached; }

    // Tras congelar, ninguna capa puede aparecer a mitad de un frame. Para
    // remontar se construye otra VFS y la app intercambia la instantanea.
    void freeze() { m_frozen = true; }
    bool frozen() const { return m_frozen; }
    std::string fingerprint() const;

    static std::string normalize(std::string p);   // '\' -> '/', sin '/' inicial ni final
    static std::string toLower(std::string s);

private:
    std::vector<Root>            m_roots;
    size_t                       m_writeRoot = 0;
    std::map<std::string, Entry> m_index;   // clave = ruta virtual en minusculas
    // Incluye ganadores y ocultos, en orden de proveedor y descubrimiento.
    // Sin esto dos `data/global.hx` se colapsan en uno y ScriptPack queda roto.
    std::vector<Entry>            m_entries;
    size_t                       m_fileCount = 0;
    mutable std::set<std::string> m_caseMismatches;
    std::vector<Conflict>         m_conflicts;
    mutable std::vector<MountIssue> m_issues;
    bool m_frozen = false;
    bool m_utf8Text = false;
    size_t m_scanLimit = 0;
    bool m_scanLimitReached = false;

    std::optional<std::filesystem::path> materializeArchiveEntry(const Entry& entry) const;
};

}  // namespace fml

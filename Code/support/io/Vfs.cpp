#include "Vfs.hpp"

#include "../core/Hash.hpp"
#include "../core/TextEncoding.hpp"
#include "../../third_party/miniz/miniz.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace fml {
namespace {

bool hasUnsafeSegments(const std::string& input) {
    if (input.empty()) return true;
    if (input.front() == '/' || input.front() == '\\') return true;
    if (input.size() >= 2 && std::isalpha(static_cast<unsigned char>(input[0])) &&
        input[1] == ':') return true;
    std::string normalized = input;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    size_t begin = 0;
    while (begin <= normalized.size()) {
        const size_t end = normalized.find('/', begin);
        const std::string part = normalized.substr(begin,
            end == std::string::npos ? std::string::npos : end - begin);
        if (part == "..") return true;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return false;
}

std::string fileFingerprint(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::is_regular_file(path, ec) ? fs::file_size(path, ec) : 0;
    ec.clear();
    const auto stamp = fs::last_write_time(path, ec);
    const auto ticks = ec ? 0 : stamp.time_since_epoch().count();
    return sha256Hex(path.lexically_normal().u8string() + "\n" +
                     std::to_string(size) + "\n" + std::to_string(ticks));
}

std::filesystem::path conflictPath(const Vfs::Root& root, const Vfs::Entry& entry) {
    if (!entry.real.empty()) return entry.real;
    return root.path / fs::u8path(entry.virtualPath);
}

size_t zipRead(void* opaque, mz_uint64 offset, void* buffer, size_t bytes) {
    auto* stream = static_cast<std::ifstream*>(opaque);
    stream->clear();
    stream->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!*stream) return 0;
    stream->read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
    return static_cast<size_t>(stream->gcount());
}

struct ZipReader {
    std::ifstream stream;
    mz_zip_archive archive{};
    bool initialized = false;

    bool open(const fs::path& path) {
        stream.open(path, std::ios::binary);
        if (!stream) return false;
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec) return false;
        archive.m_pRead = zipRead;
        archive.m_pIO_opaque = &stream;
        initialized = mz_zip_reader_init(&archive, static_cast<mz_uint64>(size), 0) != 0;
        return initialized;
    }

    ~ZipReader() {
        if (initialized) mz_zip_reader_end(&archive);
    }
};

struct ExtractTarget {
    std::ofstream stream;
};

size_t zipWrite(void* opaque, mz_uint64 offset, const void* buffer, size_t bytes) {
    auto* target = static_cast<ExtractTarget*>(opaque);
    target->stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!target->stream) return 0;
    target->stream.write(static_cast<const char*>(buffer),
                         static_cast<std::streamsize>(bytes));
    return target->stream ? bytes : 0;
}

bool pathStaysInside(const fs::path& root, const fs::path& candidate) {
    const fs::path relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

}  // namespace

std::string Vfs::toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Vfs::normalize(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    std::string out;
    out.reserve(p.size());
    bool prevSlash = false;
    for (char c : p) {
        if (c == '/') {
            if (prevSlash) continue;
            prevSlash = true;
        } else {
            prevSlash = false;
        }
        out.push_back(c);
    }
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

size_t Vfs::pushRoot(const fs::path& path, std::string label, MountProviderKind kind) {
    if (m_frozen) {
        m_issues.push_back({"FML-VFS-001", "se intento modificar una instantanea VFS congelada",
                            path.u8string()});
        return 0;
    }
    std::error_code ec;
    if (fs::is_regular_file(path, ec) && toLower(path.extension().u8string()) == ".zip")
        return pushZip(path, std::move(label));
    ec.clear();
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) return 0;

    const size_t rootIndex = m_roots.size();
    Root root;
    root.path = path;
    root.label = std::move(label);
    root.kind = kind;
    root.fingerprint = fileFingerprint(path);
    m_roots.push_back(std::move(root));

    size_t added = 0;
    // Ningun mod real pasa de unas decenas de niveles; una union de carpetas
    // que apunta a un antecesor si, y sin tope el recorrido no terminaba.
    constexpr int kMaxScanDepth = 64;
    const size_t firstEntry = m_entries.size();
    fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it.depth() >= kMaxScanDepth) it.disable_recursion_pending();
        if (m_scanLimit > 0 && m_entries.size() - firstEntry >= m_scanLimit) {
            m_scanLimitReached = true;
            m_issues.push_back({"FML-VFS-006", "demasiados archivos: el indice se detuvo en " +
                                std::to_string(m_scanLimit) + " entradas", path.u8string()});
            break;
        }
        const fs::path& real = it->path();
        std::string virt = normalize(real.lexically_relative(path).u8string());
        if (virt.empty()) continue;

        Entry entry;
        entry.real = real;
        entry.virtualPath = virt;
        entry.rootIndex = rootIndex;
        entry.isDir = it->is_directory(ec);
        if (ec) { ec.clear(); entry.isDir = false; }
        if (!entry.isDir) {
            entry.size = it->file_size(ec);
            if (ec) { ec.clear(); entry.size = 0; }
            ++added;
        }
        entry.discoveryOrder = m_entries.size();
        m_entries.push_back(entry);

        const std::string key = toLower(virt);
        if (const auto previous = m_index.find(key); previous != m_index.end()) {
            if (!previous->second.isDir && !entry.isDir) {
                m_conflicts.push_back({virt, previous->second.rootIndex, rootIndex,
                    conflictPath(m_roots[previous->second.rootIndex], previous->second), real});
            }
            continue;
        }
        if (!entry.isDir) ++m_fileCount;
        m_index.emplace(key, std::move(entry));
    }
    m_roots[rootIndex].files = added;
    return added;
}

bool Vfs::pushFile(const fs::path& path, const std::string& virtualPath,
                   std::string label) {
    if (m_frozen) return false;
    const std::string virt = normalize(virtualPath);
    if (virt.empty() || hasUnsafeSegments(virt) || m_index.count(toLower(virt)))
        return false;
    std::error_code ec;
    const fs::path physical = fs::weakly_canonical(path, ec);
    if (ec || !fs::is_regular_file(physical, ec)) return false;
    Root root;
    root.path = physical.parent_path();
    root.label = std::move(label);
    root.files = 1;
    root.fingerprint = fileFingerprint(physical);
    const size_t rootIndex = m_roots.size();
    m_roots.push_back(std::move(root));
    Entry entry;
    entry.real = physical;
    entry.virtualPath = virt;
    entry.rootIndex = rootIndex;
    entry.size = fs::file_size(physical, ec);
    if (ec) entry.size = 0;
    entry.discoveryOrder = m_entries.size();
    m_entries.push_back(entry);
    m_index.emplace(toLower(virt), std::move(entry));
    ++m_fileCount;
    return true;
}

size_t Vfs::pushZip(const fs::path& path, std::string label, const ZipLimits& limits) {
    if (m_frozen) {
        m_issues.push_back({"FML-VFS-001", "se intento modificar una instantanea VFS congelada",
                            path.u8string()});
        return 0;
    }
    ZipReader reader;
    if (!reader.open(path)) {
        m_issues.push_back({"FML-VFS-200", "ZIP ilegible o truncado", path.u8string()});
        return 0;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&reader.archive);
    if (count > limits.maxEntries) {
        m_issues.push_back({"FML-VFS-203", "ZIP rechazado: demasiadas entradas (" +
                            std::to_string(count) + ")", path.u8string()});
        return 0;
    }

    struct Pending {
        std::string raw;
        std::string path;
        mz_uint index = 0;
        std::uint64_t size = 0;
        std::uint64_t compressed = 0;
        bool directory = false;
    };
    std::vector<Pending> pending;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&reader.archive, i, &stat)) {
            m_issues.push_back({"FML-VFS-204", "entrada ZIP sin metadata",
                                path.u8string() + "::#" + std::to_string(i)});
            continue;
        }
        // El nombre en UTF-8: un ZIP de la compresion clasica de Windows lo
        // guarda en CP437, y esos bytes acababan en `u8path` (que lanza) al
        // extraer la entrada. La entrada se sigue leyendo por su indice.
        const std::string raw = zipEntryNameToUtf8(stat.m_filename ? stat.m_filename : "");
        if (hasUnsafeSegments(raw)) {
            m_issues.push_back({"FML-VFS-201", "entrada ZIP bloqueada por ruta insegura",
                                path.u8string() + "::" + raw});
            continue;
        }
        if (stat.m_is_encrypted || !mz_zip_reader_is_file_supported(&reader.archive, i)) {
            m_issues.push_back({"FML-VFS-205", "entrada ZIP cifrada o no soportada",
                                path.u8string() + "::" + raw});
            continue;
        }
        if (!stat.m_is_directory) {
            if (stat.m_uncomp_size > limits.maxFileBytes) {
                m_issues.push_back({"FML-VFS-202", "entrada ZIP demasiado grande",
                                    path.u8string() + "::" + raw});
                continue;
            }
            const double ratio = stat.m_comp_size == 0
                ? (stat.m_uncomp_size == 0 ? 1.0 : limits.maxCompressionRatio + 1.0)
                : static_cast<double>(stat.m_uncomp_size) /
                  static_cast<double>(stat.m_comp_size);
            if (stat.m_uncomp_size >= 1024u * 1024u && ratio > limits.maxCompressionRatio) {
                m_issues.push_back({"FML-VFS-202", "entrada ZIP con ratio de compresion peligroso",
                                    path.u8string() + "::" + raw});
                continue;
            }
            if (total > limits.maxTotalBytes - stat.m_uncomp_size) {
                m_issues.push_back({"FML-VFS-203", "ZIP rechazado: contenido expandido excesivo",
                                    path.u8string()});
                return 0;
            }
            total += stat.m_uncomp_size;
        }
        pending.push_back({raw, normalize(raw), i, stat.m_uncomp_size,
                           stat.m_comp_size, stat.m_is_directory != 0});
    }

    // Los ZIP distribuidos suelen envolver el mod en una unica carpeta. Solo
    // se quita cuando TODOS los archivos comparten ese primer segmento.
    std::string wrapper;
    bool commonWrapper = true;
    for (const auto& item : pending) {
        if (item.directory) continue;
        const size_t slash = item.path.find('/');
        if (slash == std::string::npos) { commonWrapper = false; break; }
        const std::string first = item.path.substr(0, slash);
        if (wrapper.empty()) wrapper = first;
        else if (toLower(wrapper) != toLower(first)) { commonWrapper = false; break; }
    }
    if (!commonWrapper) wrapper.clear();

    const size_t rootIndex = m_roots.size();
    Root root;
    root.path = path;
    root.label = std::move(label);
    root.kind = MountProviderKind::Zip;
    root.writable = false;
    root.fingerprint = fileFingerprint(path);
    std::error_code tempError;
    root.cachePath = fs::temp_directory_path(tempError) / "FunkinModLab" / "zip-cache" /
                     root.fingerprint.substr(0, 24);
    m_roots.push_back(std::move(root));

    size_t added = 0;
    auto addDirectory = [&](const std::string& directory) {
        if (directory.empty()) return;
        const std::string key = toLower(directory);
        if (m_index.count(key)) return;
        Entry entry;
        entry.virtualPath = directory;
        entry.rootIndex = rootIndex;
        entry.isDir = true;
        entry.fromArchive = true;
        entry.discoveryOrder = m_entries.size();
        m_entries.push_back(entry);
        m_index.emplace(key, std::move(entry));
    };

    for (auto item : pending) {
        if (!wrapper.empty()) {
            if (toLower(item.path) == toLower(wrapper)) continue;
            const std::string prefix = wrapper + "/";
            if (toLower(item.path).rfind(toLower(prefix), 0) == 0)
                item.path.erase(0, prefix.size());
        }
        item.path = normalize(item.path);
        if (item.path.empty()) continue;

        size_t slash = item.path.find('/');
        while (slash != std::string::npos) {
            addDirectory(item.path.substr(0, slash));
            slash = item.path.find('/', slash + 1);
        }
        if (item.directory) { addDirectory(item.path); continue; }

        Entry entry;
        entry.virtualPath = item.path;
        entry.rootIndex = rootIndex;
        entry.isDir = false;
        entry.size = item.size;
        entry.compressedSize = item.compressed;
        entry.archiveFileIndex = item.index;
        entry.fromArchive = true;
        entry.discoveryOrder = m_entries.size();
        m_entries.push_back(entry);
        ++added;

        const std::string key = toLower(item.path);
        if (const auto previous = m_index.find(key); previous != m_index.end()) {
            if (!previous->second.isDir) {
                m_conflicts.push_back({item.path, previous->second.rootIndex, rootIndex,
                    conflictPath(m_roots[previous->second.rootIndex], previous->second),
                    path / fs::u8path(item.raw)});
            }
            continue;
        }
        m_index.emplace(key, std::move(entry));
        ++m_fileCount;
    }
    m_roots[rootIndex].files = added;
    return added;
}

std::optional<fs::path> Vfs::resolveForWrite(const std::string& virtualPath) const {
    const std::string norm = normalize(virtualPath);
    if (hasUnsafeSegments(norm)) return std::nullopt;
    if (m_roots.empty() || m_writeRoot >= m_roots.size() || !isRootWritable(m_writeRoot))
        return std::nullopt;

    if (const auto entry = find(norm)) {
        if (isRootWritable(entry->rootIndex) && !entry->fromArchive) return entry->real;
        return m_roots[m_writeRoot].path / fs::u8path(entry->virtualPath);
    }

    std::string parent = norm;
    while (true) {
        const size_t slash = parent.find_last_of('/');
        if (slash == std::string::npos) break;
        parent.resize(slash);
        if (const auto directory = find(parent)) {
            if (isRootWritable(directory->rootIndex) && !directory->fromArchive)
                return m_roots[directory->rootIndex].path / fs::u8path(norm);
            break;
        }
    }
    return m_roots[m_writeRoot].path / fs::u8path(norm);
}

bool Vfs::notifyFileWritten(const std::string& virtualPath) {
    const std::string norm = normalize(virtualPath);
    if (norm.empty() || hasUnsafeSegments(norm)) return false;

    const auto target = resolveForWrite(norm);
    if (!target) return false;

    // Sigue exactamente la misma regla que resolveForWrite(): un recurso que
    // ya venia de una capa editable se actualiza ahi; uno heredado se crea en
    // la raiz de escritura del mod. No se desbloquea ni se anade un provider
    // nuevo a la instantanea congelada.
    const auto previous = find(norm);
    size_t rootIndex = m_writeRoot;
    if (previous && isRootWritable(previous->rootIndex) && !previous->fromArchive)
        rootIndex = previous->rootIndex;
    if (rootIndex >= m_roots.size() || !isRootWritable(rootIndex)) return false;

    std::error_code ec;
    if (!fs::is_regular_file(*target, ec) || ec) return false;
    const std::uintmax_t size = fs::file_size(*target, ec);
    if (ec) return false;

    const std::string key = toLower(norm);
    Entry entry;
    entry.real = *target;
    entry.virtualPath = previous ? previous->virtualPath : norm;
    entry.rootIndex = rootIndex;
    entry.isDir = false;
    entry.size = size;
    entry.fromArchive = false;

    // Actualizar la entrada de ESTA raiz evita que findAll()/ScriptPack vean
    // varias copias artificiales del mismo archivo despues de varios guardados.
    bool knownInRoot = false;
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        if (it->rootIndex != rootIndex || toLower(it->virtualPath) != key) continue;
        it->real = entry.real;
        it->virtualPath = entry.virtualPath;
        it->isDir = false;
        it->size = entry.size;
        it->compressedSize = 0;
        it->archiveFileIndex = UINT32_MAX;
        it->fromArchive = false;
        entry.discoveryOrder = it->discoveryOrder;
        knownInRoot = true;
        break;
    }
    if (!knownInRoot) {
        entry.discoveryOrder = m_entries.size();
        m_entries.push_back(entry);
        ++m_roots[rootIndex].files;
    }

    auto winner = m_index.find(key);
    if (winner == m_index.end()) {
        m_index.emplace(key, entry);
        ++m_fileCount;
        return true;
    }

    // Las raices se montan de mayor a menor prioridad. Solo reemplazamos el
    // ganador si la escritura pertenece a la misma capa o a una de mayor
    // prioridad que el proveedor visible actual.
    if (winner->second.isDir || rootIndex <= winner->second.rootIndex) {
        if (winner->second.isDir) ++m_fileCount;
        winner->second = entry;
    }
    return true;
}

std::optional<Vfs::Entry> Vfs::find(const std::string& virtualPath) const {
    const std::string norm = normalize(virtualPath);
    const auto it = m_index.find(toLower(norm));
    if (it == m_index.end()) return std::nullopt;
    if (it->second.virtualPath != norm)
        m_caseMismatches.insert(norm + " -> " + it->second.virtualPath);
    return it->second;
}

std::vector<Vfs::Entry> Vfs::findAll(const std::string& virtualPath) const {
    const std::string key = toLower(normalize(virtualPath));
    std::vector<Entry> result;
    for (const auto& entry : m_entries)
        if (toLower(entry.virtualPath) == key) result.push_back(entry);
    return result;
}

std::vector<Vfs::Entry> Vfs::allEntries(bool includeDirectories) const {
    std::vector<Entry> result;
    result.reserve(m_entries.size());
    for (const auto& entry : m_entries)
        if (includeDirectories || !entry.isDir) result.push_back(entry);
    return result;
}

std::optional<fs::path> Vfs::materializeArchiveEntry(const Entry& entry) const {
    if (!entry.fromArchive || entry.rootIndex >= m_roots.size() || entry.isDir)
        return std::nullopt;
    const Root& root = m_roots[entry.rootIndex];
    const fs::path target = root.cachePath / fs::u8path(entry.virtualPath);
    if (!pathStaysInside(root.cachePath, target)) {
        m_issues.push_back({"FML-VFS-206", "la ruta de cache escaparia del directorio aislado",
                            entry.virtualPath});
        return std::nullopt;
    }
    std::error_code ec;
    if (fs::is_regular_file(target, ec) && fs::file_size(target, ec) == entry.size)
        return target;
    ec.clear();
    fs::create_directories(target.parent_path(), ec);
    if (ec) return std::nullopt;

    ZipReader reader;
    if (!reader.open(root.path)) return std::nullopt;
    fs::path temporary = target;
    temporary += ".fml-partial-" + std::to_string(entry.archiveFileIndex);
    if (!pathStaysInside(root.cachePath, temporary)) return std::nullopt;
    ExtractTarget writer;
    writer.stream.open(temporary, std::ios::binary | std::ios::trunc);
    if (!writer.stream) return std::nullopt;
    const bool extracted = mz_zip_reader_extract_to_callback(&reader.archive,
        entry.archiveFileIndex, zipWrite, &writer, 0) != 0;
    writer.stream.close();
    if (!extracted || fs::file_size(temporary, ec) != entry.size) {
        fs::remove(temporary, ec);
        m_issues.push_back({"FML-VFS-207", "fallo al extraer una entrada ZIP",
                            root.path.u8string() + "::" + entry.virtualPath});
        return std::nullopt;
    }
    fs::rename(temporary, target, ec);
    if (ec) {
        // Otra lectura simultanea puede haber terminado primero.
        if (fs::is_regular_file(target, ec) && fs::file_size(target, ec) == entry.size) {
            ec.clear();
            fs::remove(temporary, ec);
            return target;
        }
        return std::nullopt;
    }
    return target;
}

std::optional<fs::path> Vfs::resolve(const std::string& virtualPath) const {
    if (auto entry = find(virtualPath)) {
        return resolve(*entry);
    }
    return std::nullopt;
}

std::optional<fs::path> Vfs::resolve(const Entry& entry) const {
    if (entry.fromArchive) return materializeArchiveEntry(entry);
    if (entry.real.empty()) return std::nullopt;
    return entry.real;
}

bool Vfs::exists(const std::string& virtualPath) const {
    return m_index.count(toLower(normalize(virtualPath))) > 0;
}

std::vector<Vfs::Entry> Vfs::listDir(const std::string& virtualDir, bool recursive) const {
    std::vector<Entry> out;
    const std::string dir = normalize(virtualDir);
    const std::string prefix = dir.empty() ? std::string() : toLower(dir) + "/";
    for (const auto& entry : m_entries) {
        const std::string key = toLower(entry.virtualPath);
        const auto winner = m_index.find(key);
        if (winner == m_index.end() ||
            winner->second.discoveryOrder != entry.discoveryOrder) continue;
        if (!prefix.empty()) {
            if (key.size() <= prefix.size()) continue;
            if (key.compare(0, prefix.size(), prefix) != 0) continue;
        }
        if (!recursive && key.find('/', prefix.size()) != std::string::npos) continue;
        out.push_back(entry);
    }
    return out;
}

std::vector<Vfs::Entry> Vfs::listFiles(const std::string& virtualDir,
                                       const std::string& ext,
                                       bool recursive) const {
    const std::string wanted = toLower(ext);
    std::vector<Entry> out;
    for (auto entry : listDir(virtualDir, recursive)) {
        if (entry.isDir) continue;
        const std::string path = toLower(entry.virtualPath);
        const size_t dot = path.rfind('.');
        if (dot != std::string::npos && path.compare(dot + 1, std::string::npos, wanted) == 0)
            out.push_back(std::move(entry));
    }
    return out;
}

std::optional<std::vector<unsigned char>> Vfs::readBytes(const std::string& virtualPath,
                                                          size_t maxBytes) const {
    const auto entry = find(virtualPath);
    if (!entry) return std::nullopt;
    return readBytes(*entry, maxBytes);
}

std::optional<std::vector<unsigned char>> Vfs::readBytes(const Entry& entry,
                                                          size_t maxBytes) const {
    if (entry.isDir || entry.rootIndex >= m_roots.size())
        return std::nullopt;
    if (entry.fromArchive) {
        if (entry.size > maxBytes) return std::nullopt;
        std::vector<unsigned char> bytes(static_cast<size_t>(entry.size));
        const Root& root = m_roots[entry.rootIndex];
        ZipReader reader;
        if (!reader.open(root.path)) return std::nullopt;
        if (!bytes.empty() && !mz_zip_reader_extract_to_mem(&reader.archive,
                entry.archiveFileIndex, bytes.data(), bytes.size(), 0)) return std::nullopt;
        return bytes;
    }

    // La lista de rutas de una VFS congelada es estable, pero una raiz editable
    // puede cambiar de contenido mediante SafeWriter/hot reload. `entry.size`
    // pertenece al instante de indexacion: usarlo despues de guardar truncaba
    // archivos que crecieron y rechazaba los que se hicieron mas pequenos.
    std::error_code ec;
    const std::uintmax_t currentSize = fs::file_size(entry.real, ec);
    if (ec || currentSize > maxBytes) return std::nullopt;
    std::vector<unsigned char> bytes(static_cast<size_t>(currentSize));
    std::ifstream stream(entry.real, std::ios::binary);
    if (!stream) return std::nullopt;
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (stream.gcount() != static_cast<std::streamsize>(bytes.size()))
            return std::nullopt;
    }
    return bytes;
}

std::optional<std::string> Vfs::readText(const std::string& virtualPath) const {
    constexpr size_t kMaxTextBytes = 64u * 1024u * 1024u;
    const auto bytes = readBytes(virtualPath, kMaxTextBytes);
    if (!bytes) return std::nullopt;
    std::string text(bytes->begin(), bytes->end());
    if (m_utf8Text) text = ensureUtf8(std::move(text));
    return text;
}

std::optional<std::string> Vfs::readText(const Entry& entry) const {
    constexpr size_t kMaxTextBytes = 64u * 1024u * 1024u;
    const auto bytes = readBytes(entry, kMaxTextBytes);
    if (!bytes) return std::nullopt;
    std::string text(bytes->begin(), bytes->end());
    if (m_utf8Text) text = ensureUtf8(std::move(text));
    return text;
}

std::string Vfs::fingerprint() const {
    std::string material;
    for (const auto& root : m_roots) {
        material += std::to_string(static_cast<int>(root.kind)) + "|" + root.label + "|" +
                    root.path.lexically_normal().u8string() + "|" + root.fingerprint + "\n";
    }
    for (const auto& entry : m_entries)
        if (!entry.isDir)
            material += toLower(entry.virtualPath) + "|" + std::to_string(entry.rootIndex) + "|" +
                        std::to_string(entry.size) + "\n";
    return sha256Hex(material);
}

}  // namespace fml

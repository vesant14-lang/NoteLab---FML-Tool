// fml_core — Diagnosticos y Result.
//
// Regla de la arquitectura (DESIGN §1.3): NINGUN parser lanza excepciones.
// Todo devuelve Result<T> y acumula Diagnostic. Un archivo roto degrada, no
// tumba la app. Y como efecto lateral, estos diagnosticos SON los hallazgos
// del Mod Doctor: no hay que escribirlo dos veces.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fml {

enum class Severity { Info, Warning, Error };

inline const char* toString(Severity s) {
    switch (s) {
        case Severity::Info:    return "INFO";
        case Severity::Warning: return "WARN";
        case Severity::Error:   return "ERROR";
    }
    return "?";
}

// Un hallazgo del inventario no significa que la cancion abierta este rota.
// Mantener esa diferencia en el modelo evita que errores de un stage que no se
// usa contaminen el estado de la preview activa.
enum class DiagnosticScope { ActiveContent, WorkspaceInventory };

inline const char* toString(DiagnosticScope scope) {
    switch (scope) {
        case DiagnosticScope::ActiveContent:      return "active-content";
        case DiagnosticScope::WorkspaceInventory: return "workspace-inventory";
    }
    return "unknown";
}

struct Diagnostic {
    Severity    severity = Severity::Warning;
    std::string code;      // "FML-1001" — estable, para poder silenciar por codigo
    std::string message;
    std::string path;      // ruta virtual o real, si aplica
    int         line = -1;
    DiagnosticScope scope = DiagnosticScope::ActiveContent;
};

class DiagnosticSink {
public:
    explicit DiagnosticSink(DiagnosticScope defaultScope = DiagnosticScope::ActiveContent)
        : m_defaultScope(defaultScope) {}

    void add(Severity sev, std::string code, std::string message,
             std::string path = {}, int line = -1) {
        m_items.push_back({sev, std::move(code), std::move(message), std::move(path), line,
                           m_defaultScope});
    }
    void addScoped(DiagnosticScope scope, Severity sev, std::string code,
                   std::string message, std::string path = {}, int line = -1) {
        m_items.push_back({sev, std::move(code), std::move(message), std::move(path), line,
                           scope});
    }
    void info (std::string c, std::string m, std::string p = {}, int l = -1) { add(Severity::Info,    std::move(c), std::move(m), std::move(p), l); }
    void warn (std::string c, std::string m, std::string p = {}, int l = -1) { add(Severity::Warning, std::move(c), std::move(m), std::move(p), l); }
    void error(std::string c, std::string m, std::string p = {}, int l = -1) { add(Severity::Error,   std::move(c), std::move(m), std::move(p), l); }

    const std::vector<Diagnostic>& all() const { return m_items; }

    size_t count(Severity sev) const {
        size_t n = 0;
        for (const auto& d : m_items) if (d.severity == sev) ++n;
        return n;
    }

    size_t count(Severity sev, DiagnosticScope scope) const {
        size_t n = 0;
        for (const auto& d : m_items)
            if (d.severity == sev && d.scope == scope) ++n;
        return n;
    }

    size_t count(DiagnosticScope scope) const {
        size_t n = 0;
        for (const auto& d : m_items) if (d.scope == scope) ++n;
        return n;
    }

    void append(const DiagnosticSink& other) {
        m_items.insert(m_items.end(), other.m_items.begin(), other.m_items.end());
    }

private:
    DiagnosticScope m_defaultScope = DiagnosticScope::ActiveContent;
    std::vector<Diagnostic> m_items;
};

// Result minimo. VS2019 (MSVC 14.29) no tiene std::expected, y no vale la pena
// arrastrar tl::expected para esto.
template <typename T>
class Result {
public:
    static Result ok(T value)          { Result r; r.m_value = std::move(value); return r; }
    static Result fail(std::string err){ Result r; r.m_error = std::move(err);   return r; }

    bool               hasValue() const { return m_value.has_value(); }
    explicit operator  bool()     const { return hasValue(); }
    T&                 value()          { return *m_value; }
    const T&           value()    const { return *m_value; }
    const std::string& error()    const { return m_error; }

private:
    std::optional<T> m_value;
    std::string      m_error;
};

}  // namespace fml

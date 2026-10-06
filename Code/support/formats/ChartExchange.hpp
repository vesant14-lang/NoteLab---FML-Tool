// fml_formats — Intercambio explicito de charts con otros motores.
//
// Esta capa nunca escribe el proyecto ni sustituye el documento activo. Solo
// transforma archivos externos a/desde UniversalChart; la UI muestra el
// resultado y el usuario decide si lo aplica al borrador.
#pragma once

#include "../core/Chart.hpp"
#include "../core/Diagnostics.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fml {

// Los eventos de Psych, traducidos a los que Codename ejecuta de verdad.
//
// Los 14 eventos integrados de Codename estan declarados como datos
// (`EventsData.hx:13-122`) y los de Psych viven en el `switch` de
// `PlayState.hx:2061+`. Cinco tienen equivalente exacto y el resto no tiene
// ninguno: esos NO se tiran -se conservan con su nombre y sus valores- pero
// tampoco se disfrazan de algo que el motor no va a ejecutar.
struct PsychEventPort {
    std::string psych;      // como se llama en Psych
    std::string codename;   // vacio = no hay equivalente
    std::string hint;       // que hacer entonces
};

const std::vector<PsychEventPort>& psychEventPorts();

struct PsychEventReport {
    // nombre de Psych -> cuantos, para poder decirlo sin repetir la lista.
    std::vector<std::pair<std::string, int>> translated;
    std::vector<std::pair<std::string, int>> unresolved;
    int total = 0;
};

// Traduce en el sitio los que tienen equivalente y cuenta los que no.
// `bpm` hace falta porque Psych mide los tweens en SEGUNDOS y Codename en
// PASOS: sin convertirlo, un tween de 4 s entra como 4 pasos.
PsychEventReport translatePsychEvents(UniversalChart& chart, float bpm);

struct ChartImportCandidate {
    std::string    difficulty;
    UniversalChart chart;
};

struct ChartImportBundle {
    std::string sourcePath;
    std::string formatLabel;
    std::string songName;
    std::string artist;
    std::string charter;
    std::vector<ChartImportCandidate> candidates;
    std::vector<std::string> warnings;
    // Que paso con los eventos de Psych, para poder ensenarlo en vez de
    // resumirlo en un aviso.
    PsychEventReport events;
    // Tipos de nota que el chart usa. Codename acepta cualquiera -son cadenas-,
    // pero el que no exista como script en el mod no hace nada, asi que se
    // dicen por su nombre.
    std::vector<std::string> noteTypes;
};

struct PsychExportPackage {
    std::string text;
    std::vector<std::string> warnings;
};

Result<ChartImportBundle> importPsychEngineChart(
    const std::vector<std::filesystem::path>& paths, DiagnosticSink& sink);

Result<ChartImportBundle> importFunkinBaseChart(
    const std::vector<std::filesystem::path>& paths, DiagnosticSink& sink);

Result<ChartImportBundle> importFunkinBaseChartText(
    const std::string& chartText, const std::string& metadataText,
    const std::string& sourcePath, DiagnosticSink& sink);

Result<PsychExportPackage> exportPsychEngineChart(
    const UniversalChart& chart, const std::string& displayName,
    DiagnosticSink& sink);

// Escritura atomica para una ruta que el usuario eligio expresamente en el
// dialogo de exportacion. No crea .fml ni toca el proyecto.
bool writeExternalTextAtomic(const std::filesystem::path& target,
                             const std::string& text, std::string& error);

}  // namespace fml

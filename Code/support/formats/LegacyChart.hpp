// fml_formats — Chart legacy de FNF.
//
// El mismo formato en Codename y en Psych (CORPUS §11); solo cambia donde vive
// el archivo. Por eso este parser no es "de Codename": es del FORMATO, y lo
// reusaran los dos adaptadores.
#pragma once

#include "../core/Chart.hpp"
#include "../core/Diagnostics.hpp"

#include <string>

namespace fml {

Result<UniversalChart> parseLegacyChart(const std::string& text,
                                        const std::string& sourcePath,
                                        DiagnosticSink&    sink);


// Un mod portado desde otro motor puede arrastrar sus eventos en un archivo
// que el chart de Codename no referencia y que carga un script suyo. Esto NO
// reconoce archivos por su nombre: mira la forma del JSON. El dialecto
// posicional de Leather/Psych es `{"song":{"events":[[nombre, ms, v1, v2]]}}`.
// Devuelve vacio -no error- cuando el archivo no encaja en ningun dialecto
// conocido: no encajar es lo normal, y adivinar seria peor que callarse.
struct ForeignEventFile {
    std::string             dialect;
    std::vector<ChartEvent> events;
    bool                    noteModchart = false;
};
std::optional<ForeignEventFile> parseForeignEventFile(const std::string& text);
// XML counterpart. It recognizes event-like nodes by structure (identity plus
// ms/beat/step time, or an initialization container), not by filename or mod.
// Returned ChartEvent times are already converted through the song TimeMap.
std::optional<ForeignEventFile> parseForeignXmlEventFile(
    const std::string& text, const TimeMap& map);
}  // namespace fml

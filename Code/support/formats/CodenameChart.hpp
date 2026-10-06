// Lector del formato nativo de Codename Engine (codenameChart + strumLines).
#pragma once

#include "../core/Chart.hpp"
#include "../core/Diagnostics.hpp"

#include <string>
#include <vector>

namespace fml {

struct CodenameChartDefaults {
    std::string songId;
    std::string displayName;
    float       bpm = 100.0f;
    int         beatsPerMeasure = 4;
    int         stepsPerBeat = 4;
    bool        needsVoices = true;
};

bool isCodenameChartDocument(const std::string& text);

Result<UniversalChart> parseCodenameChart(const std::string& text,
                                          const std::string& sourcePath,
                                          const CodenameChartDefaults& defaults,
                                          DiagnosticSink& sink);

// events.json usa la misma estructura de eventos que el chart, envuelta en
// {"events": [...]}. `global` queda marcado para poder distinguir su origen.
Result<std::vector<ChartEvent>> parseCodenameEventsFile(const std::string& text,
                                                        const std::string& sourcePath,
                                                        DiagnosticSink& sink);

// Reconstruye bpmChanges despues de combinar eventos locales y globales.
void rebuildChartTiming(UniversalChart& chart, DiagnosticSink& sink);

}  // namespace fml

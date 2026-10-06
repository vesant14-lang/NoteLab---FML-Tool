#pragma once

#include "NoteBlocks.hpp"

namespace fml::notelab {

struct CodeImportResult {
    bool applied = false;
    bool syntaxValid = false;
    int line = 0;
    std::string error;
    BlockProgram program;
};

enum class CodeColorKind { Text, Comment, String, Number, Keyword, Type, Function, Literal };
struct CodeColorSpan {
    size_t begin = 0, end = 0;
    CodeColorKind kind = CodeColorKind::Text;
};
std::vector<CodeColorSpan> colorBlockSource(Engine engine, const std::string& text, bool config = false);

std::string draftKey(Engine engine, const std::string& path);
CodeImportResult applyBlockSource(Engine engine, const std::string& type, const std::string& fileName,
    const BlockProgram& original, const std::string& path, const std::string& text, const TypeLook& look = {});
CodeImportResult keepCustomSource(Engine engine, const BlockProgram& original, const std::string& path, const std::string& text);
const char* previewCoverage(const std::string& key, bool spanish);

// Pasar el script de un tipo de nota a bloques, lo que se pueda (pedido del
// autor, 5 oct 2026). Se buscan las funciones de evento del motor (Psych
// goodNoteHit, opponentNoteHit, noteMiss y el bucle de onCreate; Codename
// onPlayerHit, onDadHit, onNoteHit, onPlayerMiss; V-Slice onNoteHit,
// onNoteMiss) y, dentro (sin la comprobacion del tipo), lo que los bloques
// saben hacer: vida, puntos, fallos, combo, sonido, temblor, destello, «Hey!»,
// mensajes y las propiedades del tipo. Lo demas se queda como bloques
// «codigo» en su sitio, tal cual; lo que no cuelga de un evento se avisa.
struct ScriptImport {
    BlockProgram program;
    int blocks = 0;     // sentencias que se hicieron bloques
    int kept = 0;       // lineas que se quedan como bloques «codigo»
    std::vector<std::pair<std::string, std::string>> notes;   // avisos: ingles, espanol
};
ScriptImport importNoteScript(Engine engine, const std::string& type, const std::string& text);

}

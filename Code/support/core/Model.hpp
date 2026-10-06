// fml_core — Modelo universal. Structs puros, CERO IO, CERO dependencias.
//
// Este archivo no sabe que existen Codename ni Psych. Los adaptadores producen
// esto; el runtime y el editor solo consumen esto. Es lo que permite reutilizar
// todo el editor entre motores (DESIGN §3.2).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace fml {

struct Vec2 { float x = 0.0f, y = 0.0f; };

// -----------------------------------------------------------------------------
// Procedencia comun a datos, scripts y recursos resueltos. Una ruta virtual no
// basta en un workspace con overlays: tambien hay que saber QUE capa gano y si
// el origen se puede editar. Los rangos se rellenan solo cuando un parser puede
// demostrar de que literal procede un valor.
// -----------------------------------------------------------------------------
enum class SourceEditability { Editable, ReadOnlyComputed, ReadOnlyProvider };

struct SourceOrigin {
    std::string path;
    std::string provider;
    size_t      providerIndex = static_cast<size_t>(-1);
    std::string contentHash;
    size_t      byteStart = static_cast<size_t>(-1);
    size_t      byteEnd   = static_cast<size_t>(-1);
    int         line      = -1;
    int         column    = -1;
    SourceEditability editability = SourceEditability::ReadOnlyComputed;
};

// Perfil versionado del motor. No contiene excepciones por mod: describe las
// reglas de una familia/build de Codename y permite que un adaptador futuro
// publique otro perfil sin contaminar el runtime universal.
struct EngineProfile {
    std::string id;
    std::string engine;
    std::string version;
    int         build = 0;
    int         commit = 0;
    // Defines con los que se parsea el HScript del mod. `Parser.evalPreproCond`
    // trata un define ausente como false SIN diagnostico, asi que lo que falte
    // aqui no da error: borra el bloque `#if` y deja el script "parseado bien".
    std::map<std::string, std::string> preprocessors;
    // Defines que FML sabe POSITIVAMENTE que la build emulada no tiene: otros
    // targets, `debug` en release, interruptores comentados en Project.xml.
    // Sin esta lista no se puede distinguir "se que es falso" de "no tengo
    // opinion", y el aviso de defines ausentes seria ruido: un `#if html5` en
    // una preview de Windows es correcto, no un hueco.
    std::vector<std::string> absentDefines;
    // De donde salieron: "baseline" o la ruta del Project.xml del fork. La UI y
    // los diagnosticos tienen que poder decir en que se basan.
    std::string definesSource;
    // Perfil de fuentes de la build. No afirma que las clases esten enlazadas
    // en Safe Preview: conserva de donde salen los classpaths, haxelibs y
    // defines para que el diagnostico pueda distinguir fuente inspeccionable de
    // codigo realmente ejecutable. El fingerprint cambia cuando cambia alguno
    // de los archivos de configuracion observados.
    std::vector<std::string> buildFiles;
    std::vector<std::string> sourceRoots;
    std::vector<std::string> haxelibs;
    std::string sourceFingerprint;
    bool        sourceProfileDetected = false;
    std::vector<std::string> scriptExtensions;
    std::vector<std::string> audioExtensions;
};

enum class MountProviderKind { Directory, Zip, BaseGame, Addon, ContentPack };

inline const char* toString(MountProviderKind kind) {
    switch (kind) {
        case MountProviderKind::Directory:   return "directory";
        case MountProviderKind::Zip:         return "zip";
        case MountProviderKind::BaseGame:    return "base-game";
        case MountProviderKind::Addon:       return "addon";
        case MountProviderKind::ContentPack: return "content-pack";
    }
    return "unknown";
}

struct MountProviderInfo {
    std::string id;
    std::string label;
    std::string source;
    MountProviderKind kind = MountProviderKind::Directory;
    size_t priority = 0;
    bool writable = false;
};

struct ModWorkspace {
    std::string baseInstallation;
    std::string primaryMod;
    std::vector<std::string> addons;
    std::vector<std::string> contentPacks;
    std::vector<MountProviderInfo> providers;
    size_t writeProvider = static_cast<size_t>(-1);
    EngineProfile profile;
    std::string fingerprint;
};

enum class ScriptScope {
    Global,
    Stage,
    Character,
    Song,
    SongDifficulty,
    LegacyChart,
    Event,
    NoteType,
    XmlExtension,
    Shader,
    Cutscene,
    Dialogue,
    State,
    LibrarySource,
    Unknown,
};

inline const char* toString(ScriptScope value) {
    switch (value) {
        case ScriptScope::Global:         return "global";
        case ScriptScope::Stage:          return "stage";
        case ScriptScope::Character:      return "character";
        case ScriptScope::Song:           return "song";
        case ScriptScope::SongDifficulty: return "song-difficulty";
        case ScriptScope::LegacyChart:    return "legacy-chart";
        case ScriptScope::Event:          return "event";
        case ScriptScope::NoteType:       return "note-type";
        case ScriptScope::XmlExtension:   return "xml-extension";
        case ScriptScope::Shader:         return "shader";
        case ScriptScope::Cutscene:       return "cutscene";
        case ScriptScope::Dialogue:       return "dialogue";
        case ScriptScope::State:          return "state";
        case ScriptScope::LibrarySource:  return "library-source";
        case ScriptScope::Unknown:        return "unknown";
    }
    return "unknown";
}

enum class ScriptApplicability { Required, Optional, NotApplicable };
enum class ScriptParseState { NotAttempted, StaticScanned, Parsed, Error };
enum class ScriptLoadState { NotLoaded, Loaded, Blocked, Error };

// Papel de un .hx dentro de Codename. La extension no basta: `data/*.hx` es
// HScript, `assets/source/*.hx` se importa dentro del mismo Interp y el
// `source/*.hx` de un checkout del motor es Haxe compilado.
enum class HxSourceRole {
    HScriptAsset,
    ImportedHScriptModule,
    CompiledHaxeSource,
    UnsupportedLanguage,
    Unknown,
};

inline const char* toString(HxSourceRole value) {
    switch (value) {
        case HxSourceRole::HScriptAsset:          return "hscript-asset";
        case HxSourceRole::ImportedHScriptModule: return "imported-hscript-module";
        case HxSourceRole::CompiledHaxeSource:    return "compiled-haxe-source";
        case HxSourceRole::UnsupportedLanguage:   return "unsupported-language";
        case HxSourceRole::Unknown:               return "unknown";
    }
    return "unknown";
}

struct HxImportFact {
    std::string name;
    std::string alias;
    int         line = -1;
    bool        usingDirective = false;
    bool        wildcard = false;
};

struct HxDeclarationFact {
    std::string kind;  // class/interface/enum/abstract/typedef
    std::string name;
    int         line = -1;
};

struct HxPreprocessorFact {
    std::string directive;   // if / elseif / error / ...
    std::string expression;
    std::vector<std::string> identifiers;
    int line = -1;
};

struct HxScriptImportFact {
    std::string path;
    int line = -1;
};

struct HxApiFact {
    std::string name;
    int line = -1;
};

struct HxCompileFeatureFact {
    std::string name;
    int line = -1;
};

struct HxTypeHintFact {
    std::string name;
    std::string typeName;
    std::string evidence;  // annotation / constructor / imported-symbol
    int line = -1;
    // `<module>` para variables del script/clase, o un id estable de funcion.
    // Evita que un parametro llamado `event` en una funcion contamine otra.
    std::string scope;
};

struct HxStructuralFieldFact {
    std::string name;
    std::string typeName;
    int line = -1;
    bool optional = false;
    bool function = false;
};

// A named typedef or a synthetic anonymous-object shape. `targetType` is set
// for aliases (`typedef Id<T> = Array<T>`); structural aliases retain fields.
struct HxTypeAliasFact {
    std::string name;
    std::string kind;       // typedef / class / interface / abstract / anonymous
    std::string owner;
    std::string scope;
    int line = -1;
    std::vector<std::string> typeParameters;
    std::string targetType;
    std::vector<HxStructuralFieldFact> fields;
    bool structural = false;
    bool anonymous = false;
};

struct HxReturnFact {
    std::string functionScope;
    std::string expression;
    int line = -1;
    std::vector<std::string> inferredTypes;
    bool conditional = false;
};

struct HxMemberAccessFact {
    std::string receiver;
    std::string member;
    std::string inferredType;
    int line = -1;
    bool call = false;
    bool write = false;
    bool optional = false;  // `receiver?.member`
    int argumentCount = -1; // -1 si no es llamada o la aridad es condicional
    bool arityKnown = false;
    std::string scope;
    // More than one entry is a conservative flow union. `inferredType` is
    // populated only when the receiver has one proven type.
    std::vector<std::string> inferredTypes;
    std::string inferredMemberType;
};

struct HxFunctionParameterFact {
    std::string name;
    std::string typeName;
    std::string defaultExpression;
    int line = -1;
    bool optional = false;   // `?value`
    bool hasDefault = false; // `value = ...`; tambien reduce la aridad minima
    bool rest = false;       // `...values`
};

// Firma observada directamente en fuente. No afirma que el simbolo este
// enlazado en SafeInterp ni que Haxe elegiria este overload: conserva hechos
// para que el runtime o un proveedor exacto puedan tomar esa decision.
struct HxFunctionFact {
    std::string owner;       // tipo contenedor; vacio para HScript/module-level
    std::string name;        // vacio para closure anonima
    // El nombre vuelve a aparecer en el propio fuente, aparte de su
    // declaracion. Distingue una funcion que el script SE LLAMA a si mismo —o
    // que registra como callback, `signal.add(miFuncion)`— de una que solo
    // existe para que la llame el motor. Sin esto, las dos parecen iguales.
    bool referencedInSource = false;
    std::string scope;       // id estable owner/name/line/offset
    int line = -1;
    std::vector<std::string> typeParameters;
    std::vector<HxFunctionParameterFact> parameters;
    std::string returnType;
    int minArity = 0;
    int maxArity = 0;        // -1 cuando existe parametro rest
    bool arityKnown = true;  // false si #if/spread hace condicional la firma
    bool anonymous = false;
    bool isStatic = false;
    bool isInline = false;
    bool isOverride = false;
    bool isMacro = false;
    bool isExtern = false;
    bool overload = false;   // `@:overload` sobre esta declaracion
    // Used only when no explicit return annotation is available. Multiple
    // entries are retained as a union instead of selecting a branch.
    std::vector<std::string> inferredReturnTypes;
    bool returnConditional = false;
};

struct HxCallFact {
    std::string receiver;    // vacio para llamada local; `new` para constructor
    std::string callee;
    std::string scope;
    int line = -1;
    int argumentCount = -1;
    bool arityKnown = false;
    bool optional = false;
    bool constructor = false;
    // resolved-local / ambiguous / arity-mismatch / runtime-probe-required /
    // conditional. Nunca se usa este string para habilitar una API del host.
    std::string resolution;
    std::string inferredReturnType;
    std::vector<std::string> inferredReturnTypes;
    std::vector<std::string> argumentTypes;
    std::vector<int> candidateLines;
};

struct HxStaticFacts {
    HxSourceRole role = HxSourceRole::Unknown;
    std::string packageName;
    std::vector<HxImportFact> imports;
    std::vector<HxDeclarationFact> declarations;
    std::vector<HxPreprocessorFact> preprocessors;
    std::vector<std::string> importedScripts;
    std::vector<HxScriptImportFact> importedScriptFacts;
    std::vector<std::string> apiSymbols;
    std::vector<HxApiFact> apiFacts;
    std::vector<std::string> handledEvents;
    std::vector<std::string> compileOnlyFeatures;
    std::vector<HxCompileFeatureFact> compileFeatureFacts;
    std::vector<HxTypeHintFact> typeHints;
    std::vector<HxTypeAliasFact> typeAliases;
    std::vector<HxReturnFact> returns;
    std::vector<HxMemberAccessFact> memberAccesses;
    std::vector<HxFunctionFact> functions;
    std::vector<HxCallFact> calls;
    bool containsNul = false;
    bool lexicalError = false;
    std::string lexicalErrorMessage;
    int lexicalErrorLine = -1;
};

// Cadena de evidencia para una necesidad HX. `resolved` solo afirma que hay
// una ruta de proveedor para ESE simbolo; los miembros usados sobre el objeto
// se siguen verificando en runtime y pueden producir una causa mas especifica.
enum class HxRequirementKind {
    Import,
    Using,
    ImportScript,
    Preprocessor,
    CompileFeature,
    RuntimeApi,
    MemberAccess,
    Language,
};

inline const char* toString(HxRequirementKind value) {
    switch (value) {
        case HxRequirementKind::Import:         return "import";
        case HxRequirementKind::Using:          return "using";
        case HxRequirementKind::ImportScript:   return "import-script";
        case HxRequirementKind::Preprocessor:   return "preprocessor";
        case HxRequirementKind::CompileFeature: return "compile-feature";
        case HxRequirementKind::RuntimeApi:      return "runtime-api";
        case HxRequirementKind::MemberAccess:    return "member-access";
        case HxRequirementKind::Language:        return "language";
    }
    return "unknown";
}

enum class HxProviderKind {
    BuildProfile,
    MountedHScript,
    SafeHost,
    HaxeCore,
    EngineSource,
    Haxelib,
    ExactEngine,
    RuntimeObservation,
    Unknown,
};

inline const char* toString(HxProviderKind value) {
    switch (value) {
        case HxProviderKind::BuildProfile:       return "build-profile";
        case HxProviderKind::MountedHScript:     return "mounted-hscript";
        case HxProviderKind::SafeHost:           return "safe-host";
        case HxProviderKind::HaxeCore:           return "haxe-core";
        case HxProviderKind::EngineSource:       return "engine-source";
        case HxProviderKind::Haxelib:            return "haxelib";
        case HxProviderKind::ExactEngine:        return "exact-engine";
        case HxProviderKind::RuntimeObservation: return "runtime-observation";
        case HxProviderKind::Unknown:            return "unknown";
    }
    return "unknown";
}

enum class HxRequirementState {
    Resolved,
    RuntimeProbeRequired,
    Partial,
    InspectionOnly,
    ExactBuildRequired,
    KnownAbsent,
    Unresolved,
    Unsupported,
    Unknown,
};

inline const char* toString(HxRequirementState value) {
    switch (value) {
        case HxRequirementState::Resolved:             return "resolved";
        case HxRequirementState::RuntimeProbeRequired: return "runtime-probe-required";
        case HxRequirementState::Partial:              return "partial";
        case HxRequirementState::InspectionOnly:       return "inspection-only";
        case HxRequirementState::ExactBuildRequired:   return "exact-build-required";
        case HxRequirementState::KnownAbsent:          return "known-absent";
        case HxRequirementState::Unresolved:           return "unresolved";
        case HxRequirementState::Unsupported:          return "unsupported";
        case HxRequirementState::Unknown:              return "unknown";
    }
    return "unknown";
}

struct HxRequirementFact {
    HxRequirementKind kind = HxRequirementKind::Import;
    HxRequirementState state = HxRequirementState::Unknown;
    HxProviderKind providerKind = HxProviderKind::Unknown;
    std::string symbol;
    int line = -1;
    std::string provider;
    std::string capability;
    std::string backend;
    std::string evidencePath;
    int evidenceLine = -1;
    std::string reason;
    bool runtimeObserved = false;
};

inline const char* toString(ScriptApplicability value) {
    switch (value) {
        case ScriptApplicability::Required:      return "required";
        case ScriptApplicability::Optional:      return "optional";
        case ScriptApplicability::NotApplicable: return "not-applicable";
    }
    return "unknown";
}

inline const char* toString(ScriptParseState value) {
    switch (value) {
        case ScriptParseState::NotAttempted: return "not-attempted";
        case ScriptParseState::StaticScanned:return "static-scanned";
        case ScriptParseState::Parsed:       return "parsed";
        case ScriptParseState::Error:        return "error";
    }
    return "unknown";
}

inline const char* toString(ScriptLoadState value) {
    switch (value) {
        case ScriptLoadState::NotLoaded: return "not-loaded";
        case ScriptLoadState::Loaded:    return "loaded";
        case ScriptLoadState::Blocked:   return "blocked";
        case ScriptLoadState::Error:     return "error";
    }
    return "unknown";
}

enum class ScriptRuntimeState {
    NotStarted,
    WaitingForHook,
    Active,
    Partial,
    Disabled,
    Blocked,
    Error,
    TimedOut,
    Unavailable,
};

inline const char* toString(ScriptRuntimeState value) {
    switch (value) {
        case ScriptRuntimeState::NotStarted:     return "not-started";
        case ScriptRuntimeState::WaitingForHook: return "waiting-for-hook";
        case ScriptRuntimeState::Active:         return "active";
        case ScriptRuntimeState::Partial:        return "partial";
        case ScriptRuntimeState::Disabled:       return "disabled";
        case ScriptRuntimeState::Blocked:        return "blocked";
        case ScriptRuntimeState::Error:          return "error";
        case ScriptRuntimeState::TimedOut:       return "timed-out";
        case ScriptRuntimeState::Unavailable:    return "runtime-unavailable";
    }
    return "unknown";
}

// Un mismo script puede tener mas de una clase de problema a la vez. Guardar
// solamente `missingApis` hacia que un import no resuelto, una capa visual
// parcial y una API que detuvo un callback aparecieran todos como
// "bloqueantes". La categoria viene del diagnostico emitido por el host, no
// del nombre del mod ni de una lista especial de scripts.
enum class ScriptIssueKind {
    Error,
    BlockedApi,
    UnresolvedImport,
    PartialCapability,
    CompatibilityFallback,
    CommandRejected,
    UnknownPreprocessor,
    CompileOnlyFeature,
};

inline const char* toString(ScriptIssueKind value) {
    switch (value) {
        case ScriptIssueKind::Error:                 return "error";
        case ScriptIssueKind::BlockedApi:            return "blocked-api";
        case ScriptIssueKind::UnresolvedImport:      return "unresolved-import";
        case ScriptIssueKind::PartialCapability:     return "partial-capability";
        case ScriptIssueKind::CompatibilityFallback: return "compatibility-fallback";
        case ScriptIssueKind::CommandRejected:       return "command-rejected";
        case ScriptIssueKind::UnknownPreprocessor:   return "unknown-preprocessor";
        case ScriptIssueKind::CompileOnlyFeature:    return "compile-only-feature";
    }
    return "error";
}

struct ScriptIssue {
    ScriptIssueKind kind = ScriptIssueKind::Error;
    std::string code;
    std::string api;
    std::string message;
    std::string callback;
    std::string impact;
    int line = -1;
};

struct ScriptDescriptor {
    std::string id;                 // estable: scope + owner + path
    std::string path;               // ruta virtual
    std::string logicalFileName;    // para .pack puede diferir de path
    std::string extension;
    ScriptScope scope = ScriptScope::Unknown;
    std::string owner;              // stage/personaje/cancion/evento/note type
    std::string difficulty;
    std::string pack;
    int discoveryOrder = -1;
    int loadOrder = -1;
    bool packed = false;
    // Forma parte del ScriptPack inicial de PlayState. Los .hx ubicados en
    // subcarpetas no estandar de songs/<song>/scripts quedan indexados para
    // addScript/importScript, pero Codename no los carga hasta que otro script
    // los agrega expresamente.
    bool autoLoad = true;
    // Si el motor ejecuta este lenguaje. `Script.create` (`Script.hx:235-251`)
    // manda `hx|hscript|hsc|hxs` a HScript y desempaqueta `pack`; `lua` y
    // cualquier otra extension devuelven un DummyScript, o sea que no corren.
    // Se decide UNA vez al indexar y viaja con el descriptor, para que nadie
    // tenga que volver a deducirlo por la extension mas adelante.
    bool executable = true;
    bool shadowed = false;
    std::string shadowedBy;
    SourceOrigin source;
    std::vector<std::string> imports;
    std::vector<std::string> importedScripts;
    std::vector<std::string> hooks;
    std::vector<std::string> apiSymbols;
    // Nombres de evento comparados en el codigo (`case "Punch"`, `.name == "X"`).
    // No exige hook `onEvent`: `punchMechanic.hx` de Voiid recorre `events` en
    // `create()` y despacha por nombre sin usar el hook.
    std::vector<std::string> handledEvents;
    std::vector<std::string> loadedBy;    // XML/import/owner que lo solicita
    // Indice estructural siempre disponible, incluso antes de arrancar el host.
    // No sustituye al parser HScript: evita falsos imports por comentarios y
    // aporta tipos/preprocessors/lineas para explicar la cadena de causa.
    HxStaticFacts hx;
    // Derivado de `hx`: une cada requisito con el proveedor, capacidad y
    // backend que lo pueden satisfacer. Se enriquece cuando el host real
    // anuncia su superficie o emite un diagnostico de runtime.
    std::vector<HxRequirementFact> hxRequirements;
    // Otras rutas con EXACTAMENTE este contenido (mismo `source.contentHash`).
    //
    // Un proyecto que reune material de varias versiones replica el mismo
    // script dentro de cada cancion que lo usa. El motor los carga por separado
    // -y debe seguir haciendolo, porque cada copia pertenece a su cancion-,
    // pero para leer un informe son UNO: si falla, falla una vez en 27 sitios,
    // no 27 veces. Vacio en el caso normal, que es un script sin copias.
    std::vector<std::string> duplicatePaths;
};

struct ScriptReport {
    std::string scriptId;
    ScriptApplicability applicability = ScriptApplicability::Optional;
    ScriptParseState parseState = ScriptParseState::NotAttempted;
    ScriptLoadState loadState = ScriptLoadState::NotLoaded;
    ScriptRuntimeState runtimeState = ScriptRuntimeState::NotStarted;
    std::vector<std::string> hooksCalled;
    // Union legado para la UI. `issues` conserva la clasificacion y la cadena
    // completa; las herramientas de cobertura no deben volver a asumir que
    // todo lo listado aqui bloqueo el script.
    std::vector<std::string> missingApis;
    std::vector<ScriptIssue> issues;
    std::vector<std::string> requestedCapabilities;
    std::string lastHook;
    // `error` conserva la causa primaria para los consumidores existentes.
    // first/latest permiten mostrar despues los sintomas sin reemplazarla.
    std::string error;
    std::string firstError;
    std::string latestError;
    int firstErrorLine = -1;
    std::string stack;
    int line = -1;
    int column = -1;
    double totalMs = 0.0;
    size_t commandCount = 0;
};

struct ScriptDependency {
    std::string from;
    std::string to;
    bool dynamic = false;
};

struct ScriptGraph {
    std::vector<ScriptDescriptor> scripts;
    std::vector<ScriptReport> reports;
    std::vector<ScriptDependency> dependencies;

    const ScriptDescriptor* find(const std::string& id) const {
        for (const auto& script : scripts) if (script.id == id) return &script;
        return nullptr;
    }
    ScriptDescriptor* find(const std::string& id) {
        for (auto& script : scripts) if (script.id == id) return &script;
        return nullptr;
    }
    const ScriptReport* report(const std::string& id) const {
        for (const auto& item : reports) if (item.scriptId == id) return &item;
        return nullptr;
    }
    ScriptReport* report(const std::string& id) {
        for (auto& item : reports) if (item.scriptId == id) return &item;
        return nullptr;
    }
};

// -----------------------------------------------------------------------------
// Property bag tipado.
// CORPUS §16: los <sprite> de Codename llevan hijos <property name= type= value=>
// arbitrarios. Un struct de campos fijos perderia eso. Ademas guardamos SIEMPRE
// el literal original (`raw`) para poder reescribirlo sin perdida (DESIGN §1.4).
// -----------------------------------------------------------------------------
struct PropertyValue {
    enum class Type { Bool, Int, Float, String };
    Type        type = Type::String;
    std::string raw;

    bool operator==(const PropertyValue& o) const { return type == o.type && raw == o.raw; }
    bool operator!=(const PropertyValue& o) const { return !(*this == o); }

    bool  asBool()  const { return raw == "true" || raw == "1"; }
    float asFloat() const { try { return std::stof(raw); } catch (...) { return 0.0f; } }
    int   asInt()   const { try { return std::stoi(raw); } catch (...) { return 0; } }
};

// De donde sale un stage. Nunca un bool (DESIGN §1.3b).
enum class StageSource { Declarative, BuiltinDescriptor, ScriptExecuted, Unknown };

inline const char* toString(StageSource s) {
    switch (s) {
        case StageSource::Declarative:       return "Declarative";
        case StageSource::BuiltinDescriptor: return "BuiltinDescriptor";
        case StageSource::ScriptExecuted:    return "ScriptExecuted";
        case StageSource::Unknown:           return "Unknown";
    }
    return "?";
}

// La editabilidad es propiedad del CAMPO, no del motor.
enum class PropertyEditability { Editable, ReadOnly_Computed, ReadOnly_Builtin };

// -----------------------------------------------------------------------------
struct AnimationDef {
    std::string      name;         // nombre logico: "idle", "singLEFT"
    std::string      atlasPrefix;  // prefijo en el Sparrow: "Dad idle dance"
    std::vector<int> indices;      // vacio = todos los frames del prefijo
    int              fps  = 24;
    bool             loop = false;
    // FunkinSprite crea automaticamente una animacion "idle" con TODOS los
    // frames cuando el XML apunta a un atlas pero no declara ningun <anim>.
    // Un prefijo vacio no sirve para expresarlo porque framesFor("") es vacio.
    bool             allAtlasFrames = false;
    Vec2             offset;
    bool             flipX = false, flipY = false;

    // Rellenado al validar contra el atlas. -1 = todavia sin comprobar,
    // 0 = el prefijo NO existe en el atlas (animacion rota).
    int              resolvedFrames = -1;

    // Offset del tag <anim> en el archivo, para escribir sin tocar nada mas.
    size_t           tagOffset = static_cast<size_t>(-1);
};

struct StageObject {
    enum class Kind { Sprite, Box, Player, Opponent, Girlfriend, Character, Ratings, Unknown };

    Kind        kind = Kind::Sprite;
    std::string name;
    // Identidad efimera de la instancia que produjo el runtime HScript. No se
    // serializa en XML: solo conecta el objeto observado con recursos visuales
    // del frame, como un shader o una captura de camara.
    std::string runtimeObjectId;
    std::string spritePath;      // ruta del asset, relativa a assetFolder

    // Grupo contenedor del que cuelga, si lo hay. En Codename <high-memory>
    // envuelve sprites que solo se cargan si el modo de baja calidad esta off.
    // Vacio = cuelga directamente del <stage>.
    std::string group;

    // Solo para <box>/<solid>: rectangulos de color generados, sin asset.
    float       width = 0.0f, height = 0.0f;
    std::string color;
    bool        updateHitbox = false;

    Vec2  position;
    // Offset puramente visual de FlxSprite/FunkinSprite. No cambia el hitbox ni
    // la posicion autoral; el renderer lo resta despues de aplicar scale.
    Vec2  frameOffset;
    Vec2  scroll{1.0f, 1.0f};
    Vec2  scale {1.0f, 1.0f};
    float alpha        = 1.0f;
    float angle        = 0.0f;
    float zoomFactor   = 1.0f;
    // Camaras de Flixel en las que se dibuja el objeto. Vacio conserva la
    // camara por defecto (`camGame`). HScript suele escribir `[camHUD]`, pero
    // el modelo admite varias para no perder la semantica de FlxSprite.cameras.
    std::vector<std::string> cameraIds;
    bool  antialiasing = true;
    bool  flipX        = false;
    // Separacion de StageCharPos para varios personajes en el mismo marcador.
    Vec2  characterSpacing{20.0f, 0.0f};
    std::string type;            // "onbeat"/"beat"/"loop" — comportamiento en datos
    // Ciclado por beat. Codename: por defecto 2, pero los personajes usan 1.
    int         beatInterval = 2;
    int         beatOffset   = 0;

    // true = el XML no lo declaraba; lo anade el motor por defecto.
    bool  implicit = false;

    // Solo para marcadores de personaje: llevan offsets de camara inline.
    bool  hasCamOffset = false;
    Vec2  camOffset;

    std::vector<AnimationDef>            anims;
    std::map<std::string, PropertyValue> properties;
    // Offset del tag <property> de cada una, para editarla sin reescribir nada
    // mas. Si falta, es que no existe en el archivo y hay que crearla.
    std::map<std::string, size_t>        propertyTagOffsets;

    // Rellenado al resolver contra la VFS. Vacio = no encontrado.
    std::string resolvedImage;   // ruta virtual del .png
    std::string resolvedAtlas;   // ruta virtual del .xml, si lo hay

    // Atributos que el parser no reconocio. NO se descartan: se conservan para
    // poder reescribir el archivo sin perder nada.
    std::map<std::string, std::string> unknownAttributes;
};

struct UniversalStage {
    std::string  id;            // nombre de archivo: es la identidad real
    std::string  displayName;   // atributo `name`, si difiere. No es unico.
    std::string  sourcePath;
    std::string  assetFolder;
    float        zoom = 1.0f;
    Vec2         startCamPos;
    // El motor comprueba CADA EJE por separado:
    //     if (xml.has.startCamPosX) camFollow.x = startCam.x;
    //     if (xml.has.startCamPosY) camFollow.y = startCam.y;
    // `limo.xml` declara solo Y — es el unico del juego base que lo hace, y
    // tratar los dos ejes como uno lo descuadraba entero.
    bool         hasStartCamPosX = false;
    bool         hasStartCamPosY = false;
    StageSource  source = StageSource::Declarative;

    std::vector<StageObject> objects;          // EN ORDEN DE RENDER
    std::vector<std::string> behaviorScripts;  // .hx hermanos (solo comportamiento)

    // Para la escritura no destructiva (DESIGN §1.4): los bytes originales y,
    // por cada objeto, el offset de su tag. SIZE_MAX = marcador implicito, no
    // existe en el archivo.
    std::string         sourceText;
    std::string         rootName = "stage";   // nombre del elemento raiz
    std::vector<size_t> objectTagOffsets;

    std::map<std::string, std::string> unknownAttributes;
};

struct UniversalCharacter {
    std::string  id;
    std::string  sourcePath;
    std::string  spriteAtlas;
    std::vector<std::string> atlasSources;
    std::string  icon;
    std::string  healthColor;
    float        holdTime = 4.0f;   // -> BehaviorProfile
    int          beatInterval = 2;
    bool         hasBeatInterval = false;
    // OJO al nombre: el atributo isPlayer="true" del XML NO significa "es el
    // jugador". Significa que sus offsets y animaciones estan AUTORIZADOS para
    // el lado del jugador (Character.hx:357 -> playerOffsets). Quien es el
    // jugador lo decide el SLOT del stage que ocupa.
    bool         playerOffsets = false;
    // Codename usa el centro para Sparrow/Packer y la esquina superior para
    // Animate, salvo que `centercam` lo sobrescriba expresamente.
    bool         centeredCamera = true;
    bool         hasCenteredCamera = false;
    Vec2         position;
    Vec2         camOffset;
    float        frameWidth = 0.0f;
    float        frameHeight = 0.0f;
    float        scale = 1.0f;
    bool         antialiasing = true;
    bool         flipX = false;

    // Estado transitorio producido por el runtime HScript. Nunca se serializa
    // en el XML: representa la instancia Character ya colocada por el Stage.
    bool         hasRuntimeStagePosition = false;
    Vec2         runtimeStagePosition;
    // Keep role/strumline indices stable when a script hides an actor. These
    // instance properties affect both Sparrow and Animate drawing, not XML.
    bool         runtimeVisible = true;
    float        runtimeAlpha = 1.0f;
    std::string  runtimeObjectId;

    std::vector<AnimationDef> anims;
    std::vector<std::string>  behaviorScripts;

    std::string resolvedImage;
    std::string resolvedAtlas;
    std::vector<std::string> resolvedImages;
    std::vector<std::string> resolvedAtlases;
    int         atlasPrefixCount = -1;   // cuantas animaciones ofrece el atlas

    // Escritura no destructiva, igual que en los stages.
    std::string sourceText;
    size_t      rootTagOffset = static_cast<size_t>(-1);

    std::map<std::string, std::string> unknownAttributes;
    // `Character.xml` del motor es un `haxe.xml.Access` sobre el nodo raiz, y
    // los mods leen atributos por nombre -`xml.get("camx")`- incluidos los que
    // FML ya modela. `unknownAttributes` solo guarda los que no consumio el
    // parser, asi que no sirve para responder esas lecturas. Esto conserva el
    // nodo tal cual venia, que es lo unico que reproduce la semantica.
    std::map<std::string, std::string> xmlAttributes;
};

struct SongEntry {
    std::string id;
    std::string sourcePath;
    std::string displayName;
    std::string icon;
    std::string color;
    float       bpm = 0.0f;
    int         beatsPerMeasure = 4;
    int         stepsPerBeat    = 4;
    bool        needsVoices = true;
    std::string instSuffix;
    std::string vocalsSuffix;

    // Cadenas libres. CORPUS §19: nunca asumir easy/normal/hard.
    // El ORDEN importa y no es alfabetico: si meta.json las declara, ese es el
    // orden; si no, es el de charts/ con la correccion easy/normal/hard que
    // aplica el motor (Chart.hx:145). Ordenarlas por nombre las descoloca.
    std::vector<std::string> difficulties;
    bool difficultiesInferred = false;   // deducidas de charts/, no declaradas
    std::vector<std::string> variants;
    // Dificultades de cada variante. La clave vacia representa la seleccion
    // normal y duplica `difficulties` solo para consumidores nuevos.
    std::map<std::string, std::vector<std::string>> variantDifficulties;
    // Todos los meta candidatos encontrados. Resolver una seleccion decide
    // cual gana; indexar no debe perder los demas.
    std::map<std::string, std::string> metadataFiles;

    // CORPUS §18: Codename tambien parte las voces. N pistas, no 2.
    std::vector<std::string> audioTracks;
    std::vector<std::string> scripts;

    // meta.json en crudo. Se conserva para poder reescribirlo sin perder
    // campos que no entendemos (DESIGN §1.4). Vacio = la cancion no tiene meta.
    std::string metaPath;
    std::string metaText;
};

// Quien ejecuta de verdad el comportamiento de un evento. Codename NO exige un
// script hermano: `EventsData.reloadEvents` solo registra el esquema, y
// `PlayState.executeEvent` entrega el evento a todos los ScriptPack. Un mod
// puede resolverlo desde un script de cancion o global, o no tener script
// ninguno porque el motor ya lo implementa. Tratar "JSON sin .hx" como error
// producia 6 avisos falsos de 7 eventos en Voiid y 0 en DUSTIN: no medimos si
// el evento funciona, medimos el estilo de autoria del mod.
enum class EventHandlerKind {
    Builtin,        // lo implementa el motor (`EventsData.defaultEventsList`)
    SiblingScript,  // data/events/<nombre>.hx junto al esquema
    ScriptPack,     // un script del mod compara este nombre
    Unresolved,     // hay esquema y nadie que lo reclame
};

inline const char* toString(EventHandlerKind kind) {
    switch (kind) {
        case EventHandlerKind::Builtin:       return "builtin";
        case EventHandlerKind::SiblingScript: return "sibling-script";
        case EventHandlerKind::ScriptPack:    return "handled-by-pack";
        case EventHandlerKind::Unresolved:    return "unresolved";
    }
    return "unresolved";
}

struct EventDef {
    struct Param {
        std::string name;
        std::string type;          // "Bool", "Float(0.1, 10, 0.01, 2)" — DSL del motor
        std::string defaultValue;
        // `EventParamInfo.saveIfDefault` (EventsData.hx:181-186). Si es false el
        // motor omite el parametro al guardar cuando vale lo de por defecto.
        // Sin este campo, un evento del mod que lo declare se reescribiria con
        // parametros que Codename nunca habria escrito.
        bool        saveIfDefault = true;
    };
    std::string        name;
    std::string        schemaPath;  // .json o .pack — esquema de UI (CORPUS §17)
    std::string        scriptPath;  // .hx  — comportamiento hermano, si lo hay
    std::vector<Param> params;
    EventHandlerKind   handler = EventHandlerKind::Unresolved;
    // Rutas de los scripts que comparan este nombre. Vacio salvo en ScriptPack.
    std::vector<std::string> handlerScripts;
    // El nombre venia dentro de un .pack y no del nombre de archivo.
    bool               packed = false;
};

struct ProjectIndex {
    std::string projectName;
    std::string rootPath;
    std::string engineName;
    std::string engineVersionHint;
    EngineProfile engineProfile;
    ModWorkspace workspace;

    std::vector<UniversalCharacter> characters;
    std::vector<UniversalStage>     stages;
    std::vector<SongEntry>          songs;
    std::vector<EventDef>           events;
    // Grafo autoritativo. `scripts` se conserva durante la migracion de la UI
    // vieja, pero nunca vuelve a ser la fuente de verdad.
    ScriptGraph                     scriptGraph;
    std::vector<std::string>        scripts;
    std::vector<std::string>        customStates;  // overrides de estado (no convertibles)
};

// Resultado de la deteccion: nunca un enum pelado (DESIGN §1.3).
struct DetectResult {
    std::string              engine;
    std::string              versionHint;
    float                    confidence = 0.0f;   // 0..1
    std::vector<std::string> evidence;
};

}  // namespace fml

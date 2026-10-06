#include "ShaderLibrary.hpp"

#include <SDL3/SDL.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>

typedef char GLchar;

#define GL_FRAGMENT_SHADER   0x8B30
#define GL_VERTEX_SHADER     0x8B31
#define GL_COMPILE_STATUS    0x8B81
#define GL_LINK_STATUS       0x8B82
#define GL_ACTIVE_UNIFORMS   0x8B86
#define GL_FLOAT_VEC2        0x8B50
#define GL_FLOAT_VEC3        0x8B51
#define GL_FLOAT_VEC4        0x8B52
#define GL_INT_VEC2          0x8B53
#define GL_INT_VEC3          0x8B54
#define GL_INT_VEC4          0x8B55
#define GL_BOOL              0x8B56
#define GL_BOOL_VEC2         0x8B57
#define GL_BOOL_VEC3         0x8B58
#define GL_BOOL_VEC4         0x8B59
#define GL_FLOAT_MAT2        0x8B5A
#define GL_FLOAT_MAT3        0x8B5B
#define GL_FLOAT_MAT4        0x8B5C
#define GL_SAMPLER_2D        0x8B5E

namespace {

typedef GLuint (APIENTRY* PFN_CreateShader)(GLenum);
typedef void   (APIENTRY* PFN_ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (APIENTRY* PFN_CompileShader)(GLuint);
typedef void   (APIENTRY* PFN_GetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY* PFN_GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (APIENTRY* PFN_CreateProgram)(void);
typedef void   (APIENTRY* PFN_AttachShader)(GLuint, GLuint);
typedef void   (APIENTRY* PFN_LinkProgram)(GLuint);
typedef void   (APIENTRY* PFN_GetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY* PFN_GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (APIENTRY* PFN_DeleteShader)(GLuint);
typedef void   (APIENTRY* PFN_DeleteProgram)(GLuint);
typedef void   (APIENTRY* PFN_GetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*);
typedef GLint  (APIENTRY* PFN_GetUniformLocation)(GLuint, const GLchar*);

struct GLX {
    PFN_CreateShader        CreateShader = nullptr;
    PFN_ShaderSource        ShaderSource = nullptr;
    PFN_CompileShader       CompileShader = nullptr;
    PFN_GetShaderiv         GetShaderiv = nullptr;
    PFN_GetShaderInfoLog    GetShaderInfoLog = nullptr;
    PFN_CreateProgram       CreateProgram = nullptr;
    PFN_AttachShader        AttachShader = nullptr;
    PFN_LinkProgram         LinkProgram = nullptr;
    PFN_GetProgramiv        GetProgramiv = nullptr;
    PFN_GetProgramInfoLog   GetProgramInfoLog = nullptr;
    PFN_DeleteShader        DeleteShader = nullptr;
    PFN_DeleteProgram       DeleteProgram = nullptr;
    PFN_GetActiveUniform    GetActiveUniform = nullptr;
    PFN_GetUniformLocation  GetUniformLocation = nullptr;
    bool loaded = false;
};

GLX g;

void ensureLoaded() {
    if (g.loaded) return;
    auto L = [](auto& fn, const char* n) {
        fn = reinterpret_cast<std::decay_t<decltype(fn)>>(SDL_GL_GetProcAddress(n));
    };
    L(g.CreateShader, "glCreateShader");       L(g.ShaderSource, "glShaderSource");
    L(g.CompileShader, "glCompileShader");     L(g.GetShaderiv, "glGetShaderiv");
    L(g.GetShaderInfoLog, "glGetShaderInfoLog");
    L(g.CreateProgram, "glCreateProgram");     L(g.AttachShader, "glAttachShader");
    L(g.LinkProgram, "glLinkProgram");         L(g.GetProgramiv, "glGetProgramiv");
    L(g.GetProgramInfoLog, "glGetProgramInfoLog");
    L(g.DeleteShader, "glDeleteShader");       L(g.DeleteProgram, "glDeleteProgram");
    L(g.GetActiveUniform, "glGetActiveUniform");
    L(g.GetUniformLocation, "glGetUniformLocation");
    g.loaded = g.CreateShader && g.CompileShader && g.CreateProgram;
}

bool isWordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Sustitucion de palabra completa: `varying` no debe tocar `myvarying`.
std::string replaceWord(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        const bool leftOk  = (pos == 0) || !isWordChar(s[pos - 1]);
        const size_t after = pos + from.size();
        const bool rightOk = (after >= s.size()) || !isWordChar(s[after]);
        if (leftOk && rightOk) { s.replace(pos, from.size(), to); pos += to.size(); }
        else pos = after;
    }
    return s;
}

// Copiado literal de ShaderTemplates.fragHeader (FunkinShader.hx).
const char* kFragHeader = R"GLSL(
varying float openfl_Alphav;
varying vec4  openfl_ColorMultiplierv;
varying vec4  openfl_ColorOffsetv;
varying vec2  openfl_TextureCoordv;

uniform bool  openfl_HasColorTransform;
uniform vec2  openfl_TextureSize;
uniform sampler2D bitmap;
// El backing store puede ser mayor que el bitmap logico (pool de FlxText).
// Los shaders siguen recibiendo UV 0..1 y medidas del objeto real; solo la
// lectura del sampler se recorta a la region residente.
uniform vec2 fml_TextureUvScale;

uniform bool hasTransform;
uniform bool hasColorTransform;

vec4 applyFlixelEffects(vec4 color) {
    if (!hasTransform) return color;
    if (color.a == 0.0) return vec4(0.0, 0.0, 0.0, 0.0);
    if (!hasColorTransform) return color * openfl_Alphav;
    color.rgb = color.rgb / color.a;
    color = clamp(openfl_ColorOffsetv + (color * openfl_ColorMultiplierv), 0.0, 1.0);
    if (color.a > 0.0) return vec4(color.rgb * color.a * openfl_Alphav, color.a * openfl_Alphav);
    return vec4(0.0, 0.0, 0.0, 0.0);
}

vec4 flixel_texture2D(sampler2D bitmap, vec2 coord) {
    return applyFlixelEffects(texture2D(bitmap, coord * fml_TextureUvScale));
}

uniform vec4 _camSize;

float map(float value, float min1, float max1, float min2, float max2) {
    return min2 + (value - min1) * (max2 - min2) / (max1 - min1);
}
vec2 getCamPos(vec2 pos) {
    vec4 size = _camSize / vec4(openfl_TextureSize, openfl_TextureSize);
    return vec2(map(pos.x, size.x, size.x + size.z, 0.0, 1.0),
                map(pos.y, size.y, size.y + size.w, 0.0, 1.0));
}
vec2 camToOg(vec2 pos) {
    vec4 size = _camSize / vec4(openfl_TextureSize, openfl_TextureSize);
    return vec2(map(pos.x, 0.0, 1.0, size.x, size.x + size.z),
                map(pos.y, 0.0, 1.0, size.y, size.y + size.w));
}
vec4 textureCam(sampler2D bitmap, vec2 pos) {
    return flixel_texture2D(bitmap, camToOg(pos));
}
)GLSL";

const char* kFragBody = "gl_FragColor = flixel_texture2D(bitmap, openfl_TextureCoordv);";

// Vertex shader propio que produce exactamente los varyings que espera el
// preambulo de OpenFL.
const char* kVertexSource = R"GLSL(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aUV;
layout (location = 2) in vec4 aColor;
uniform mat4 uProjection;
out float openfl_Alphav;
out vec4  openfl_ColorMultiplierv;
out vec4  openfl_ColorOffsetv;
out vec2  openfl_TextureCoordv;
void main() {
    openfl_TextureCoordv     = aUV.xy / aUV.z;
    openfl_Alphav            = aColor.a;
    openfl_ColorMultiplierv  = vec4(aColor.rgb, 1.0);
    openfl_ColorOffsetv      = vec4(0.0);
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)GLSL";

// OpenFL vertex contract projected onto the renderer's fixed VBO. Runtime
// shaders may add arbitrary per-corner attributes; expandVertex rewrites those
// declarations to uniform arrays and indexes them with fmlCornerIndex(). This
// preserves the public Shader.data contract without allowing a script to own
// native buffers or pointers.
const char* kRawVertHeader = R"GLSL(
layout (location = 0) in vec2 fml_aPos;
layout (location = 1) in vec3 fml_aUVQ;
layout (location = 2) in vec4 fml_aColor;

out float openfl_Alphav;
out vec4  openfl_ColorMultiplierv;
out vec4  openfl_ColorOffsetv;
out vec2  openfl_TextureCoordv;

uniform mat4 openfl_Matrix;
uniform bool openfl_HasColorTransform;
uniform vec2 openfl_TextureSize;
uniform bool hasColorTransform;

int fmlCornerIndex() {
    int vertex = gl_VertexID % 6;
    if (vertex == 0 || vertex == 3) return 0;
    if (vertex == 1) return 1;
    if (vertex == 2 || vertex == 4) return 2;
    return 3;
}

#define openfl_Alpha (fml_aColor.a)
#define openfl_ColorMultiplier (vec4(1.0))
#define openfl_ColorOffset (vec4(0.0))
#define openfl_Position (vec4(fml_aPos, 0.0, 1.0))
#define openfl_TextureCoord (fml_aUVQ.xy / fml_aUVQ.z)
#define alpha (1.0)
#define colorMultiplier (vec4(fml_aColor.rgb, 1.0))
#define colorOffset (vec4(0.0))
)GLSL";

const char* kRawVertBody = R"GLSL(
openfl_Alphav = openfl_Alpha;
openfl_TextureCoordv = openfl_TextureCoord;
openfl_ColorMultiplierv = vec4(1.0);
openfl_ColorOffsetv = vec4(0.0);
if (openfl_HasColorTransform) {
    openfl_ColorMultiplierv = openfl_ColorMultiplier;
    openfl_ColorOffsetv = openfl_ColorOffset / 255.0;
}
openfl_Alphav = openfl_Alpha * alpha;
if (hasColorTransform) {
    openfl_ColorOffsetv = colorOffset / 255.0;
    openfl_ColorMultiplierv = colorMultiplier;
}
gl_Position = openfl_Matrix * openfl_Position;
)GLSL";

}  // namespace

namespace fml {

// GLSL ES 1.0 -> 3.3 core, por texto y no con #define: redefinir nombres
// reservados (gl_FragColor) lo rechazan algunos drivers.
static std::string translateDialect(std::string line) {
    line = replaceWord(std::move(line), "varying", "in");
    line = replaceWord(std::move(line), "attribute", "in");
    line = replaceWord(std::move(line), "texture2D", "texture");
    line = replaceWord(std::move(line), "textureCube", "texture");
    line = replaceWord(std::move(line), "gl_FragColor", "fmlFragColor");
    return line;
}

static std::string translateVertexDialect(std::string line) {
    line = replaceWord(std::move(line), "varying", "out");
    line = replaceWord(std::move(line), "attribute", "in");
    line = replaceWord(std::move(line), "texture2D", "texture");
    line = replaceWord(std::move(line), "textureCube", "texture");
    return line;
}

bool ShaderLibrary::isModOwnedUniform(const std::string& name) {
    if (name.rfind("openfl_", 0) == 0) return false;
    if (name.rfind("fml_", 0) == 0 && name.rfind("fml_attr_", 0) != 0)
        return false;
    return name != "bitmap" && name != "uTexture" &&
           name != "hasTransform" && name != "hasColorTransform" &&
           name != "_camSize" && name != "uProjection";
}

std::string ShaderLibrary::expand(const std::string& source) {
    return expand(source, nullptr);
}

// La expansion se hace LINEA A LINEA, y no sobre el texto entero, para poder
// llevar la cuenta de que linea del resultado sale de que linea del archivo del
// usuario. Sin esa cuenta, el error que devuelve el driver apunta a una linea
// que no existe en su .frag y no hay forma de depurarlo (DESIGN §14.4.2).
std::string ShaderLibrary::expand(const std::string& source, std::vector<int>* lineMap) {
    std::string out;
    if (lineMap) lineMap->clear();

    auto emit = [&](const std::string& text, int sourceLine) {
        out += text;
        out += '\n';
        if (lineMap) lineMap->push_back(sourceLine);
    };
    // El preambulo del motor son ~55 lineas: es justo el desfase que hace
    // ilegible el log, y por eso se marca entero como linea 0.
    auto emitBlock = [&](const char* block) {
        std::string line;
        for (const char* c = block; *c; ++c) {
            if (*c == '\n') { emit(translateDialect(line), 0); line.clear(); }
            else if (*c != '\r') line += *c;
        }
        if (!line.empty()) emit(translateDialect(line), 0);
    };

    emit("#version 330 core", 0);
    emit("precision highp float;", 0);
    emit("out vec4 fmlFragColor;", 0);

    size_t start = 0;
    int    sourceLine = 0;
    while (start <= source.size()) {
        size_t end = source.find('\n', start);
        if (end == std::string::npos) end = source.size();
        std::string line = source.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ++sourceLine;

        // The safe renderer owns the desktop GLSL version. A source-provided
        // version may describe GLSL ES and cannot legally appear after ours.
        std::string trimmed = line;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(),
            [](unsigned char c) { return !std::isspace(c); }));
        if (trimmed.rfind("#version", 0) == 0) {
            if (end == source.size()) break;
            start = end + 1;
            continue;
        }

        // En todo el corpus los pragmas van solos en su linea, pero si alguien
        // los mete en medio se conserva lo que haya alrededor: no se descarta
        // codigo ajeno por no encajar en el caso comun.
        const size_t h = line.find("#pragma header");
        const size_t b = line.find("#pragma body");
        if (h != std::string::npos) {
            const std::string before = line.substr(0, h);
            const std::string after  = line.substr(h + strlen("#pragma header"));
            if (!before.empty()) emit(translateDialect(before), sourceLine);
            emitBlock(kFragHeader);
            if (!after.empty()) emit(translateDialect(after), sourceLine);
        } else if (b != std::string::npos) {
            emit(translateDialect(line.substr(0, b) + kFragBody +
                                  line.substr(b + strlen("#pragma body"))), sourceLine);
        } else {
            emit(translateDialect(line), sourceLine);
        }

        if (end == source.size()) break;
        start = end + 1;
    }
    return out;
}

std::string ShaderLibrary::expandVertex(const std::string& input,
                                        std::vector<int>* lineMap) {
    const std::string source = input.empty()
        ? std::string("#pragma header\nvoid main(void) {\n#pragma body\n}\n")
        : input;
    std::string out;
    if (lineMap) lineMap->clear();

    auto emit = [&](const std::string& text, int sourceLine) {
        out += text;
        out += '\n';
        if (lineMap) lineMap->push_back(sourceLine);
    };
    auto emitBlock = [&](const char* block) {
        std::string line;
        for (const char* c = block; *c; ++c) {
            if (*c == '\n') { emit(translateVertexDialect(line), 0); line.clear(); }
            else if (*c != '\r') line += *c;
        }
        if (!line.empty()) emit(translateVertexDialect(line), 0);
    };

    emit("#version 330 core", 0);
    emit("precision highp float;", 0);
    const bool hasHeaderPragma = source.find("#pragma header") != std::string::npos;
    if (!hasHeaderPragma) emitBlock(kRawVertHeader);

    const std::regex attributeDecl(
        R"(^\s*(?:attribute|in)\s+(?:(?:lowp|mediump|highp)\s+)?([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;\s*(?://.*)?$)");
    const std::regex generatedUniformDecl(
        R"(^\s*uniform\s+(?:mat4\s+openfl_Matrix|bool\s+openfl_HasColorTransform|vec2\s+openfl_TextureSize|bool\s+hasColorTransform)\s*;\s*(?://.*)?$)");
    const std::regex generatedVaryingDecl(
        R"(^\s*(?:varying|out)\s+(?:float\s+openfl_Alphav|vec4\s+openfl_ColorMultiplierv|vec4\s+openfl_ColorOffsetv|vec2\s+openfl_TextureCoordv)\s*;\s*(?://.*)?$)");
    const std::set<std::string> builtinAttributes = {
        "openfl_Alpha", "openfl_ColorMultiplier", "openfl_ColorOffset",
        "openfl_Position", "openfl_TextureCoord", "alpha",
        "colorMultiplier", "colorOffset"
    };
    std::set<std::string> emittedAttributes;

    size_t start = 0;
    int sourceLine = 0;
    while (start <= source.size()) {
        size_t end = source.find('\n', start);
        if (end == std::string::npos) end = source.size();
        std::string line = source.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ++sourceLine;

        std::string trimmed = line;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(),
            [](unsigned char c) { return !std::isspace(c); }));
        if (trimmed.rfind("#version", 0) == 0) {
            if (end == source.size()) break;
            start = end + 1;
            continue;
        }

        const size_t h = line.find("#pragma header");
        const size_t b = line.find("#pragma body");
        if (h != std::string::npos) {
            const std::string before = line.substr(0, h);
            const std::string after = line.substr(h + strlen("#pragma header"));
            if (!before.empty()) emit(translateVertexDialect(before), sourceLine);
            emitBlock(kRawVertHeader);
            if (!after.empty()) emit(translateVertexDialect(after), sourceLine);
        } else if (b != std::string::npos) {
            const std::string combined = line.substr(0, b) + kRawVertBody +
                line.substr(b + strlen("#pragma body"));
            std::string part;
            std::istringstream body(combined);
            while (std::getline(body, part))
                emit(translateVertexDialect(part), sourceLine);
        } else {
            std::smatch match;
            if (std::regex_match(line, match, attributeDecl)) {
                const std::string type = match[1].str();
                const std::string name = match[2].str();
                if (builtinAttributes.find(name) == builtinAttributes.end() &&
                    emittedAttributes.insert(name).second) {
                    emit("uniform " + type + " fml_attr_" + name + "[4];", sourceLine);
                    emit("#define " + name + " fml_attr_" + name +
                         "[fmlCornerIndex()]", sourceLine);
                }
            } else if (!std::regex_match(line, generatedUniformDecl) &&
                       !std::regex_match(line, generatedVaryingDecl)) {
                emit(translateVertexDialect(line), sourceLine);
            }
        }

        if (end == source.size()) break;
        start = end + 1;
    }
    return out;
}

// Los drivers emiten dos formas distintas, las mismas que el motor distingue en
// `FunkinShader.hx:79-80`: `0(47) : error C1503: ...` (NVIDIA) y
// `ERROR: 0:47: ...` (Mesa/AMD). Se reescribe el numero de linea; el resto del
// mensaje del driver se deja intacto, que es la parte que dice que pasa.
std::string ShaderLibrary::remapErrorLog(const std::string& log,
                                         const std::vector<int>& lineMap) {
    if (lineMap.empty()) return log;

    auto mapped = [&](long expanded) -> std::string {
        if (expanded >= 1 && (size_t)expanded <= lineMap.size()) {
            const int src = lineMap[(size_t)expanded - 1];
            return src > 0 ? std::to_string(src) : std::string("preambulo");
        }
        return std::string("?");
    };

    std::string out;
    size_t i = 0;
    while (i < log.size()) {
        if (!std::isdigit((unsigned char)log[i]) ||
            (i > 0 && isWordChar(log[i - 1]))) { out += log[i++]; continue; }

        size_t a = i;
        while (a < log.size() && std::isdigit((unsigned char)log[a])) ++a;

        if (a < log.size() && log[a] == '(') {
            size_t d = a + 1;
            while (d < log.size() && std::isdigit((unsigned char)log[d])) ++d;
            if (d > a + 1 && d < log.size() && log[d] == ')') {
                out += log.substr(i, a - i);
                out += "(linea " + mapped(std::strtol(log.c_str() + a + 1, nullptr, 10)) + ")";
                i = d + 1;
                continue;
            }
        }
        if (a < log.size() && log[a] == ':') {
            size_t d = a + 1;
            while (d < log.size() && std::isdigit((unsigned char)log[d])) ++d;
            if (d > a + 1) {
                out += log.substr(i, a - i);
                out += ":linea " + mapped(std::strtol(log.c_str() + a + 1, nullptr, 10));
                i = d;
                continue;
            }
        }
        out += log.substr(i, a - i);
        i = a;
    }
    return out;
}

LoadedShader* ShaderLibrary::find(const std::string& virtualPath) {
    auto it = m_shaders.find(virtualPath);
    return it == m_shaders.end() ? nullptr : &it->second;
}

ShaderLibrary::~ShaderLibrary() { clear(); }

void ShaderLibrary::clear() {
    ensureLoaded();
    if (g.loaded && g.DeleteProgram)
        for (auto& kv : m_shaders) if (kv.second.program) g.DeleteProgram(kv.second.program);
    m_shaders.clear();
}

void ShaderLibrary::invalidate(const std::string& virtualPath) {
    auto it = m_shaders.find(virtualPath);
    if (it == m_shaders.end()) return;
    ensureLoaded();
    if (g.loaded && it->second.program && g.DeleteProgram) g.DeleteProgram(it->second.program);
    m_shaders.erase(it);
}

const LoadedShader* ShaderLibrary::load(const std::string& virtualPath,
                                        const std::string& source,
                                        DiagnosticSink& sink) {
    if (const auto it = m_shaders.find(virtualPath); it != m_shaders.end())
        return &it->second;
    std::vector<int> fragmentMap;
    const std::string fragment = expand(source, &fragmentMap);
    return loadExpanded(virtualPath, fragment, kVertexSource,
                        std::move(fragmentMap), {}, false, sink);
}

const LoadedShader* ShaderLibrary::loadRaw(const std::string& cacheKey,
                                           const std::string& fragmentSource,
                                           const std::string& vertexSource,
                                           DiagnosticSink& sink) {
    if (const auto it = m_shaders.find(cacheKey); it != m_shaders.end())
        return &it->second;
    const std::string fragmentInput = fragmentSource.empty()
        ? std::string("#pragma header\nvoid main(void) {\n#pragma body\n}\n")
        : fragmentSource;
    std::vector<int> fragmentMap, vertexMap;
    const std::string fragment = expand(fragmentInput, &fragmentMap);
    const std::string vertex = expandVertex(vertexSource, &vertexMap);
    return loadExpanded(cacheKey, fragment, vertex, std::move(fragmentMap),
                        std::move(vertexMap), true, sink);
}

const LoadedShader* ShaderLibrary::loadExpanded(
        const std::string& cacheKey,
        const std::string& fragmentSource,
        const std::string& vertexSource,
        std::vector<int> fragmentLineMap,
        std::vector<int> vertexLineMap,
        bool customVertex,
        DiagnosticSink& sink) {
    if (const auto it = m_shaders.find(cacheKey); it != m_shaders.end())
        return &it->second;

    ensureLoaded();
    LoadedShader out;
    out.virtualPath = cacheKey;
    out.lineMap = std::move(fragmentLineMap);
    out.vertexLineMap = std::move(vertexLineMap);
    out.customVertex = customVertex;
    if (!g.loaded) {
        out.error = "OpenGL no disponible";
        return &m_shaders.emplace(cacheKey, std::move(out)).first->second;
    }

    auto compile = [&](GLenum type, const char* src, std::string& err) -> GLuint {
        const GLuint sh = g.CreateShader(type);
        g.ShaderSource(sh, 1, &src, nullptr);
        g.CompileShader(sh);
        GLint ok = 0;
        g.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048] = {0};
            g.GetShaderInfoLog(sh, sizeof log, nullptr, log);
            err = log;
            g.DeleteShader(sh);
            return 0;
        }
        return sh;
    };

    const GLuint vs = compile(GL_VERTEX_SHADER, vertexSource.c_str(), out.error);
    if (!vs) {
        out.errorHuman = customVertex
            ? remapErrorLog(out.error, out.vertexLineMap) : out.error;
        sink.error("FML-2500",
                   std::string(customVertex ? "vertex shader del mod no compila: "
                                            : "el vertex shader interno no compila: ") +
                       out.errorHuman,
                   cacheKey);
        return &m_shaders.emplace(cacheKey, std::move(out)).first->second;
    }
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSource.c_str(), out.error);
    if (!fs) {
        g.DeleteShader(vs);
        // Lo que se ensena y lo que va al diagnostico es la version trasladada:
        // el log crudo queda en `error` para quien quiera el texto del driver.
        out.errorHuman = remapErrorLog(out.error, out.lineMap);
        sink.error("FML-2501", "shader del mod no compila: " + out.errorHuman, cacheKey);
        return &m_shaders.emplace(cacheKey, std::move(out)).first->second;
    }

    out.program = g.CreateProgram();
    g.AttachShader(out.program, vs);
    g.AttachShader(out.program, fs);
    g.LinkProgram(out.program);
    GLint linked = 0;
    g.GetProgramiv(out.program, GL_LINK_STATUS, &linked);
    g.DeleteShader(vs);
    g.DeleteShader(fs);
    if (!linked) {
        char log[2048] = {0};
        g.GetProgramInfoLog(out.program, sizeof log, nullptr, log);
        out.error = log;
        g.DeleteProgram(out.program);
        out.program = 0;
        sink.error("FML-2502", "el shader no enlaza: " + out.error, cacheKey);
        return &m_shaders.emplace(cacheKey, std::move(out)).first->second;
    }

    // Uniforms del MOD: se filtran los de OpenFL, que rellena el editor.
    GLint n = 0;
    g.GetProgramiv(out.program, GL_ACTIVE_UNIFORMS, &n);
    for (GLint i = 0; i < n; ++i) {
        char name[128] = {0};
        GLsizei len = 0; GLint size = 0; GLenum type = 0;
        g.GetActiveUniform(out.program, (GLuint)i, sizeof name, &len, &size, &type, name);
        const std::string activeName(name, len > 0 ? (size_t)len : strlen(name));
        std::string nm = activeName;
        if (const size_t bracket = nm.find('['); bracket != std::string::npos)
            nm.erase(bracket);
        if (!isModOwnedUniform(nm)) continue;

        ShaderUniform u;
        u.vertexAttribute = nm.rfind("fml_attr_", 0) == 0;
        u.name = u.vertexAttribute ? nm.substr(strlen("fml_attr_")) : nm;
        u.location = g.GetUniformLocation(out.program, activeName.c_str());
        u.type     = (int)type;
        u.isSampler = (type == GL_SAMPLER_2D);
        u.components = (type == GL_FLOAT_VEC2 || type == GL_INT_VEC2 ||
                        type == GL_BOOL_VEC2) ? 2
                     : (type == GL_FLOAT_VEC3 || type == GL_INT_VEC3 ||
                        type == GL_BOOL_VEC3) ? 3
                     : (type == GL_FLOAT_VEC4 || type == GL_INT_VEC4 ||
                        type == GL_BOOL_VEC4) ? 4 : 1;
        u.matrixColumns = type == GL_FLOAT_MAT2 ? 2 : type == GL_FLOAT_MAT3 ? 3
                        : type == GL_FLOAT_MAT4 ? 4 : 0;
        u.arraySize = std::max(1, static_cast<int>(size));
        const int valuesPerElement = u.matrixColumns > 0
            ? u.matrixColumns * u.matrixColumns : u.components;
        u.valueCount = std::min(ShaderUniform::MaxValues,
                                u.arraySize * valuesPerElement);
        out.uniforms.push_back(std::move(u));
    }

    out.ok = true;
    sink.info("FML-2503", "shader compilado (" + std::to_string(out.uniforms.size()) +
                          " uniforms del mod)", cacheKey);
    return &m_shaders.emplace(cacheKey, std::move(out)).first->second;
}

}  // namespace fml

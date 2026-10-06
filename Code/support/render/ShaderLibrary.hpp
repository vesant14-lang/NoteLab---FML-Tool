// fml_render — Shaders del mod.
//
// DESIGN §«shaders»: es la unica parte "tipo script" de un mod que se puede
// reproducir con fidelidad casi exacta, porque no es codigo especifico del
// motor: es GLSL, un lenguaje portatil, y el contrato esta completamente
// especificado en `FunkinShader.hx`.
//
// Un .frag de Codename se ve asi:
//
//     #pragma header
//     uniform vec2 redOff;
//     void main() {
//         vec2 uv = getCamPos(openfl_TextureCoordv);
//         col.r = textureCam(bitmap, uv + redOff).r;
//     }
//
// `#pragma header` se sustituye por un preambulo fijo que declara los varyings
// de OpenFL y las funciones `flixel_texture2D`, `getCamPos` y `textureCam`.
// Ese preambulo se copia literal del motor.
//
// El unico trabajo real es el salto de dialecto: los mods estan escritos en
// GLSL ES 1.0 (`varying`, `attribute`, `texture2D`, `gl_FragColor`) y aqui se
// compila contra 3.3 core. Se traduce por texto, no con #define, porque
// redefinir nombres reservados como `gl_FragColor` lo rechazan algunos drivers.
#pragma once

#include "../core/Diagnostics.hpp"

#include <map>
#include <string>
#include <vector>

namespace fml {

struct ShaderUniform {
    std::string name;
    int         location = -1;
    int         type = 0;        // GL_FLOAT, GL_FLOAT_VEC2, ...
    int         components = 1;  // 1..4
    // Matrices and vertex attributes are transported through the same safe
    // JSON bridge as scalar uniforms. Four values were enough for camera
    // filters, but a raw FunkinShader commonly sends mat4 values (16) and four
    // per-corner vec4 values (also 16). Keep a bounded payload so an untrusted
    // shader cannot make the renderer allocate every frame.
    static constexpr int MaxValues = 64;
    float       value[MaxValues] = {};
    int         valueCount = 1;
    int         arraySize = 1;
    int         matrixColumns = 0; // 0, 2, 3 or 4
    bool        vertexAttribute = false;
    bool        isSampler = false;
    // Cero NO es un valor neutro: `res` a (0,0) es una division por cero que
    // devuelve NaN y pinta el fragmento negro, y un `threshold` a 0 deja pasar
    // todo y lo pinta blanco. Los dos llenan el marco de camara entero. Saber
    // si ALGUIEN le dio valor —el .txt del autor, el script del mod o la linea
    // de comandos— es lo que separa "sin efecto" de "basura".
    bool        assigned = false;
};

struct LoadedShader {
    std::string   virtualPath;
    unsigned int  program = 0;
    bool          ok = false;
    std::string   error;          // log crudo del driver
    // El mismo log con las lineas trasladadas al .frag del usuario. El motor NO
    // hace esto: reporta contra el fuente ya expandido: `source.split("\n")` en
    // `FunkinShader.hx:134`. Quien escribe un shader de 20 lineas ve "line 47"
    // y no tiene forma de saber a que se refiere.
    std::string   errorHuman;
    // linea del fuente expandido (1-based) -> linea del archivo del usuario.
    // 0 = linea del preambulo generado, que no existe en su archivo.
    std::vector<int> lineMap;
    std::vector<int> vertexLineMap;
    std::vector<ShaderUniform> uniforms;   // solo las del mod, sin las de OpenFL
    // True when the program contains the mod-provided vertex shader. Sprite
    // geometry then stays in the logical 1280x720 game space until that shader
    // applies openfl_Matrix.
    bool customVertex = false;
};

class ShaderLibrary {
public:
    ~ShaderLibrary();

    // Compila un .frag del mod. Cachea por ruta; devuelve el resultado aunque
    // haya fallado, para poder enseñar el error.
    const LoadedShader* load(const std::string& virtualPath, const std::string& source,
                             DiagnosticSink& sink);

    // Compiles a FunkinShader constructed from source at runtime. The cache key
    // must identify the source pair; callers normally use a content hash so
    // hundreds of note instances share one GL program.
    const LoadedShader* loadRaw(const std::string& cacheKey,
                                const std::string& fragmentSource,
                                const std::string& vertexSource,
                                DiagnosticSink& sink);

    void invalidate(const std::string& virtualPath);
    void clear();

    size_t count() const { return m_shaders.size(); }

    // Devuelve el shader ya compilado si existe, para que el inspector pueda
    // leer y escribir los valores de sus uniforms sin recompilar.
    LoadedShader* find(const std::string& virtualPath);

    // El texto final que se le pasa a OpenGL, para poder enseñarlo cuando algo
    // no compila. Sin esto, depurar un shader ajeno es a ciegas.
    static std::string expand(const std::string& source);

    // Igual, pero devolviendo tambien el mapa linea-expandida -> linea-del-.frag.
    static std::string expand(const std::string& source, std::vector<int>* lineMap);

    // OpenFL vertex counterpart of expand(). Custom attributes become bounded
    // four-corner uniform arrays, indexed from gl_VertexID, because the safe
    // renderer owns a fixed vertex layout rather than accepting arbitrary VBOs.
    static std::string expandVertex(const std::string& source,
                                    std::vector<int>* lineMap = nullptr);

    // Uniforms injected by OpenFL/FML are renderer-owned state. They must not
    // be exposed as mod values: the default zero payload would be uploaded
    // after the renderer's value and silently overwrite it. Translated custom
    // vertex attributes are the only `fml_` names that remain mod-owned.
    static bool isModOwnedUniform(const std::string& name);

    // Reescribe las lineas del log del driver usando ese mapa. Reconoce las dos
    // formas que emiten los drivers: `0(47) : error ...` (NVIDIA) y
    // `ERROR: 0:47: ...` (Mesa/AMD), que son las mismas que el motor distingue
    // en `FunkinShader.hx:79-80`.
    static std::string remapErrorLog(const std::string& log,
                                     const std::vector<int>& lineMap);

private:
    const LoadedShader* loadExpanded(const std::string& cacheKey,
                                     const std::string& fragmentSource,
                                     const std::string& vertexSource,
                                     std::vector<int> fragmentLineMap,
                                     std::vector<int> vertexLineMap,
                                     bool customVertex,
                                     DiagnosticSink& sink);
    std::map<std::string, LoadedShader> m_shaders;
};

}  // namespace fml

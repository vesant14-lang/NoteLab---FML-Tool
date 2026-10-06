#include "GlRenderer.hpp"
#include "FxShaders.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../../third_party/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
// Las rutas que reciben stbi_write_* son UTF-8 en todo el proyecto (argv ya
// convertido, `path.u8string()`). Sin esto stb abria con fopen ANSI y un PNG
// hacia una carpeta como C:\Users\José\... no se escribia.
#define STBIW_WINDOWS_UTF8
#include "../../third_party/stb_image_write.h"

#include <SDL3/SDL.h>

// windows.h define macros min/max que rompen std::min/std::max.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// -----------------------------------------------------------------------------
// Cargador minimo de OpenGL 3.3.
// opengl32.lib en Windows solo exporta GL 1.1; todo lo demas hay que pedirlo por
// SDL_GL_GetProcAddress. Son ~25 funciones: no compensa arrastrar glad/glew.
// -----------------------------------------------------------------------------
#ifndef APIENTRY
#define APIENTRY
#endif
typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_ARRAY_BUFFER          0x8892
#define GL_DYNAMIC_DRAW          0x88E8
#define GL_FRAGMENT_SHADER       0x8B30
#define GL_VERTEX_SHADER         0x8B31
#define GL_COMPILE_STATUS        0x8B81
#define GL_LINK_STATUS           0x8B82
#define GL_TEXTURE0              0x84C0
#define GL_CLAMP_TO_EDGE         0x812F
#define GL_FRAMEBUFFER           0x8D40
#define GL_COLOR_ATTACHMENT0     0x8CE0
#define GL_FRAMEBUFFER_COMPLETE  0x8CD5
#define GL_READ_FRAMEBUFFER      0x8CA8
#define GL_DRAW_FRAMEBUFFER      0x8CA9
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#ifndef GL_BGRA
#define GL_BGRA                  0x80E1
// Tipos de uniform. Las cabeceras de GL de Windows son de OpenGL 1.1 y no los
// traen; `ShaderLibrary.cpp` define los suyos por lo mismo. `GL_INT` si viene,
// asi que solo se anaden los que faltan.
#define GL_INT_VEC2              0x8B53
#define GL_INT_VEC3              0x8B54
#define GL_INT_VEC4              0x8B55
#define GL_BOOL                  0x8B56
#define GL_BOOL_VEC2             0x8B57
#define GL_BOOL_VEC3             0x8B58
#define GL_BOOL_VEC4             0x8B59
#endif
#ifndef GL_FLOAT_MAT2
#define GL_FLOAT_MAT2            0x8B5A
#define GL_FLOAT_MAT3            0x8B5B
#define GL_FLOAT_MAT4            0x8B5C
#endif

namespace {

typedef GLuint (APIENTRY* PFN_glCreateShader)(GLenum);
typedef void   (APIENTRY* PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (APIENTRY* PFN_glCompileShader)(GLuint);
typedef void   (APIENTRY* PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY* PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (APIENTRY* PFN_glCreateProgram)(void);
typedef void   (APIENTRY* PFN_glAttachShader)(GLuint, GLuint);
typedef void   (APIENTRY* PFN_glLinkProgram)(GLuint);
typedef void   (APIENTRY* PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY* PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (APIENTRY* PFN_glUseProgram)(GLuint);
typedef void   (APIENTRY* PFN_glDeleteShader)(GLuint);
typedef void   (APIENTRY* PFN_glDeleteProgram)(GLuint);
typedef void   (APIENTRY* PFN_glGenVertexArrays)(GLsizei, GLuint*);
typedef void   (APIENTRY* PFN_glBindVertexArray)(GLuint);
typedef void   (APIENTRY* PFN_glDeleteVertexArrays)(GLsizei, const GLuint*);
typedef void   (APIENTRY* PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void   (APIENTRY* PFN_glBindBuffer)(GLenum, GLuint);
typedef void   (APIENTRY* PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void   (APIENTRY* PFN_glDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRY* PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (APIENTRY* PFN_glEnableVertexAttribArray)(GLuint);
typedef GLint  (APIENTRY* PFN_glGetUniformLocation)(GLuint, const GLchar*);
// Solo para la vigilancia: leer del PROGRAMA lo que quedo tras subir el uniform.
// Es la unica forma de separar "FML calculo bien el valor" de "el valor llego a
// la GPU"; el trace de la app mide lo primero y da 1.0 para un greyscale que no
// se ve.
typedef void   (APIENTRY* PFN_glGetUniformfv)(GLuint, GLint, GLfloat*);
typedef void   (APIENTRY* PFN_glUniformMatrix2fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniform1i)(GLint, GLint);
typedef void   (APIENTRY* PFN_glUniform1iv)(GLint, GLsizei, const GLint*);
typedef void   (APIENTRY* PFN_glActiveTexture)(GLenum);
typedef void   (APIENTRY* PFN_glUniform1f)(GLint, GLfloat);
typedef void   (APIENTRY* PFN_glUniform1fv)(GLint, GLsizei, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniform2fv)(GLint, GLsizei, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniform3fv)(GLint, GLsizei, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniform4fv)(GLint, GLsizei, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniform2iv)(GLint, GLsizei, const GLint*);
typedef void   (APIENTRY* PFN_glUniform3iv)(GLint, GLsizei, const GLint*);
typedef void   (APIENTRY* PFN_glUniform4iv)(GLint, GLsizei, const GLint*);
typedef void   (APIENTRY* PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void   (APIENTRY* PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void   (APIENTRY* PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRY* PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void   (APIENTRY* PFN_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint,
                                                 GLint, GLint, GLbitfield, GLenum);
typedef GLenum (APIENTRY* PFN_glCheckFramebufferStatus)(GLenum);

struct GL {
    PFN_glCreateShader            CreateShader = nullptr;
    PFN_glShaderSource            ShaderSource = nullptr;
    PFN_glCompileShader           CompileShader = nullptr;
    PFN_glGetShaderiv             GetShaderiv = nullptr;
    PFN_glGetShaderInfoLog        GetShaderInfoLog = nullptr;
    PFN_glCreateProgram           CreateProgram = nullptr;
    PFN_glAttachShader            AttachShader = nullptr;
    PFN_glLinkProgram             LinkProgram = nullptr;
    PFN_glGetProgramiv            GetProgramiv = nullptr;
    PFN_glGetProgramInfoLog       GetProgramInfoLog = nullptr;
    PFN_glUseProgram              UseProgram = nullptr;
    PFN_glDeleteShader            DeleteShader = nullptr;
    PFN_glDeleteProgram           DeleteProgram = nullptr;
    PFN_glGenVertexArrays         GenVertexArrays = nullptr;
    PFN_glBindVertexArray         BindVertexArray = nullptr;
    PFN_glDeleteVertexArrays      DeleteVertexArrays = nullptr;
    PFN_glGenBuffers              GenBuffers = nullptr;
    PFN_glBindBuffer              BindBuffer = nullptr;
    PFN_glBufferData              BufferData = nullptr;
    PFN_glDeleteBuffers           DeleteBuffers = nullptr;
    PFN_glVertexAttribPointer     VertexAttribPointer = nullptr;
    PFN_glEnableVertexAttribArray EnableVertexAttribArray = nullptr;
    PFN_glGetUniformLocation      GetUniformLocation = nullptr;
    PFN_glGetUniformfv            GetUniformfv = nullptr;
    PFN_glUniformMatrix2fv        UniformMatrix2fv = nullptr;
    PFN_glUniformMatrix3fv        UniformMatrix3fv = nullptr;
    PFN_glUniformMatrix4fv        UniformMatrix4fv = nullptr;
    PFN_glUniform1i               Uniform1i = nullptr;
    PFN_glUniform1iv              Uniform1iv = nullptr;
    PFN_glActiveTexture           ActiveTexture = nullptr;
    PFN_glUniform1f               Uniform1f = nullptr;
    PFN_glUniform1fv              Uniform1fv = nullptr;
    PFN_glUniform2fv              Uniform2fv = nullptr;
    PFN_glUniform3fv              Uniform3fv = nullptr;
    PFN_glUniform4fv              Uniform4fv = nullptr;
    PFN_glUniform2iv              Uniform2iv = nullptr;
    PFN_glUniform3iv              Uniform3iv = nullptr;
    PFN_glUniform4iv              Uniform4iv = nullptr;
    PFN_glGenFramebuffers         GenFramebuffers = nullptr;
    PFN_glBindFramebuffer         BindFramebuffer = nullptr;
    PFN_glDeleteFramebuffers      DeleteFramebuffers = nullptr;
    PFN_glFramebufferTexture2D    FramebufferTexture2D = nullptr;
    PFN_glBlitFramebuffer         BlitFramebuffer = nullptr;
    PFN_glCheckFramebufferStatus  CheckFramebufferStatus = nullptr;
    bool ok = false;
};

GL g_gl;

template <typename T>
bool load(T& fn, const char* name) {
    fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return fn != nullptr;
}

bool loadGl(std::string* err) {
    bool ok = true;
    ok &= load(g_gl.CreateShader, "glCreateShader");
    ok &= load(g_gl.ShaderSource, "glShaderSource");
    ok &= load(g_gl.CompileShader, "glCompileShader");
    ok &= load(g_gl.GetShaderiv, "glGetShaderiv");
    ok &= load(g_gl.GetShaderInfoLog, "glGetShaderInfoLog");
    ok &= load(g_gl.CreateProgram, "glCreateProgram");
    ok &= load(g_gl.AttachShader, "glAttachShader");
    ok &= load(g_gl.LinkProgram, "glLinkProgram");
    ok &= load(g_gl.GetProgramiv, "glGetProgramiv");
    ok &= load(g_gl.GetProgramInfoLog, "glGetProgramInfoLog");
    ok &= load(g_gl.UseProgram, "glUseProgram");
    ok &= load(g_gl.DeleteShader, "glDeleteShader");
    ok &= load(g_gl.DeleteProgram, "glDeleteProgram");
    ok &= load(g_gl.GenVertexArrays, "glGenVertexArrays");
    ok &= load(g_gl.BindVertexArray, "glBindVertexArray");
    ok &= load(g_gl.DeleteVertexArrays, "glDeleteVertexArrays");
    ok &= load(g_gl.GenBuffers, "glGenBuffers");
    ok &= load(g_gl.BindBuffer, "glBindBuffer");
    ok &= load(g_gl.BufferData, "glBufferData");
    ok &= load(g_gl.DeleteBuffers, "glDeleteBuffers");
    ok &= load(g_gl.VertexAttribPointer, "glVertexAttribPointer");
    ok &= load(g_gl.EnableVertexAttribArray, "glEnableVertexAttribArray");
    ok &= load(g_gl.GetUniformLocation, "glGetUniformLocation");
    ok &= load(g_gl.GetUniformfv, "glGetUniformfv");
    ok &= load(g_gl.UniformMatrix2fv, "glUniformMatrix2fv");
    ok &= load(g_gl.UniformMatrix3fv, "glUniformMatrix3fv");
    ok &= load(g_gl.UniformMatrix4fv, "glUniformMatrix4fv");
    ok &= load(g_gl.Uniform1i, "glUniform1i");
    ok &= load(g_gl.Uniform1iv, "glUniform1iv");
    ok &= load(g_gl.Uniform2iv, "glUniform2iv");
    ok &= load(g_gl.Uniform3iv, "glUniform3iv");
    ok &= load(g_gl.Uniform4iv, "glUniform4iv");
    ok &= load(g_gl.ActiveTexture, "glActiveTexture");
    ok &= load(g_gl.Uniform1f, "glUniform1f");
    ok &= load(g_gl.Uniform1fv, "glUniform1fv");
    ok &= load(g_gl.Uniform2fv, "glUniform2fv");
    ok &= load(g_gl.Uniform3fv, "glUniform3fv");
    ok &= load(g_gl.Uniform4fv, "glUniform4fv");
    ok &= load(g_gl.GenFramebuffers, "glGenFramebuffers");
    ok &= load(g_gl.BindFramebuffer, "glBindFramebuffer");
    ok &= load(g_gl.DeleteFramebuffers, "glDeleteFramebuffers");
    ok &= load(g_gl.FramebufferTexture2D, "glFramebufferTexture2D");
    ok &= load(g_gl.BlitFramebuffer, "glBlitFramebuffer");
    ok &= load(g_gl.CheckFramebufferStatus, "glCheckFramebufferStatus");
    if (!ok && err) *err = "no se pudieron cargar todas las funciones de OpenGL 3.3";
    g_gl.ok = ok;
    return ok;
}

template <typename Uniform>
void uploadRuntimeUniform(const Uniform& uniform) {
    if (uniform.location < 0 || uniform.isSampler) return;
    const int valuesPerElement = uniform.matrixColumns > 0
        ? uniform.matrixColumns * uniform.matrixColumns
        : std::max(1, uniform.components);
    const int availableElements = std::max(1, uniform.valueCount / valuesPerElement);
    const int count = std::max(1, std::min(uniform.arraySize, availableElements));

    if (uniform.matrixColumns == 2) {
        g_gl.UniformMatrix2fv(uniform.location, count, GL_FALSE, uniform.value);
        return;
    }
    if (uniform.matrixColumns == 3) {
        g_gl.UniformMatrix3fv(uniform.location, count, GL_FALSE, uniform.value);
        return;
    }
    if (uniform.matrixColumns == 4) {
        g_gl.UniformMatrix4fv(uniform.location, count, GL_FALSE, uniform.value);
        return;
    }

    const bool integer = uniform.type == GL_INT      || uniform.type == GL_BOOL ||
                         uniform.type == GL_INT_VEC2 || uniform.type == GL_BOOL_VEC2 ||
                         uniform.type == GL_INT_VEC3 || uniform.type == GL_BOOL_VEC3 ||
                         uniform.type == GL_INT_VEC4 || uniform.type == GL_BOOL_VEC4;
    if (integer) {
        GLint values[64]{};
        const int total = std::min(64, count * std::max(1, uniform.components));
        for (int i = 0; i < total; ++i) values[i] = static_cast<GLint>(uniform.value[i]);
        switch (uniform.components) {
            case 2:  g_gl.Uniform2iv(uniform.location, count, values); break;
            case 3:  g_gl.Uniform3iv(uniform.location, count, values); break;
            case 4:  g_gl.Uniform4iv(uniform.location, count, values); break;
            default: g_gl.Uniform1iv(uniform.location, count, values); break;
        }
        return;
    }
    switch (uniform.components) {
        case 2:  g_gl.Uniform2fv(uniform.location, count, uniform.value); break;
        case 3:  g_gl.Uniform3fv(uniform.location, count, uniform.value); break;
        case 4:  g_gl.Uniform4fv(uniform.location, count, uniform.value); break;
        default: g_gl.Uniform1fv(uniform.location, count, uniform.value); break;
    }
}

// pos2 + uv3 + color4. La UV lleva tres componentes para poder corregir la
// perspectiva de un quad; con la tercera a 1 el resultado es el de siempre.
constexpr int kFloatsPerVertex = 9;

const char* kVertexShader = R"(#version 330 core
layout (location = 0) in vec2 aPos;
// (u*q, v*q, q). Con q=1 es la UV de siempre; con perspectiva corrige la
// interpolacion sobre un trapecio, que si no deforma la textura.
layout (location = 1) in vec3 aUV;
layout (location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec3 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec3 vUV;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 FragColor;
void main() {
    // Con q=1 la division es por 1.0 exacto: no cambia un solo pixel de lo que
    // ya se dibujaba, y el caso con perspectiva sale correcto sin otra pasada.
    vec4 t = texture(uTexture, vUV.xy / vUV.z);
    if (t.a < 0.003) discard;
    FragColor = t * vColor;
}
)";

GLuint compile(GLenum type, const char* src, std::string* err) {
    const GLuint s = g_gl.CreateShader(type);
    g_gl.ShaderSource(s, 1, &src, nullptr);
    g_gl.CompileShader(s);
    GLint ok = 0;
    g_gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        g_gl.GetShaderInfoLog(s, sizeof log, nullptr, log);
        if (err) *err = std::string("shader: ") + log;
        g_gl.DeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

namespace fml {

GlRenderer::~GlRenderer() { shutdown(); }

const GlRenderer::BuiltinUniformLocations&
GlRenderer::builtinUniforms(unsigned int program) {
    const auto found = m_builtinUniforms.find(program);
    if (found != m_builtinUniforms.end()) return found->second;

    BuiltinUniformLocations locations;
    locations.uProjection = g_gl.GetUniformLocation(program, "uProjection");
    locations.openflMatrix = g_gl.GetUniformLocation(program, "openfl_Matrix");
    locations.bitmap = g_gl.GetUniformLocation(program, "bitmap");
    locations.uTexture = g_gl.GetUniformLocation(program, "uTexture");
    locations.textureSize = g_gl.GetUniformLocation(program, "openfl_TextureSize");
    locations.textureUvScale = g_gl.GetUniformLocation(program, "fml_TextureUvScale");
    locations.camSize = g_gl.GetUniformLocation(program, "_camSize");
    locations.hasTransform = g_gl.GetUniformLocation(program, "hasTransform");
    locations.hasColorTransform =
        g_gl.GetUniformLocation(program, "hasColorTransform");
    locations.openflHasColorTransform =
        g_gl.GetUniformLocation(program, "openfl_HasColorTransform");
    return m_builtinUniforms.emplace(program, locations).first->second;
}

void GlRenderer::forgetShaderProgram(unsigned int program) {
    if (program) m_builtinUniforms.erase(program);
}

void GlRenderer::clearShaderProgramCache() { m_builtinUniforms.clear(); }

bool GlRenderer::init(Resolver resolve, std::string* error) {
    m_resolve = std::move(resolve);
    const char* traceDraws = std::getenv("FML_TRACE_GL_DRAWS");
    m_traceDrawTiming = traceDraws && traceDraws[0] != '\0' &&
                        std::strcmp(traceDraws, "0") != 0;
    const char* traceSpriteBindings =
        std::getenv("FML_TRACE_SPRITE_SHADER_BINDINGS");
    m_traceSpriteShaderBindings = traceSpriteBindings &&
        traceSpriteBindings[0] != '\0' &&
        std::strcmp(traceSpriteBindings, "0") != 0;
    if (!loadGl(error)) return false;

    const GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader, error);
    if (!vs) return false;
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader, error);
    if (!fs) { g_gl.DeleteShader(vs); return false; }

    m_program = g_gl.CreateProgram();
    g_gl.AttachShader(m_program, vs);
    g_gl.AttachShader(m_program, fs);
    g_gl.LinkProgram(m_program);
    GLint linked = 0;
    g_gl.GetProgramiv(m_program, GL_LINK_STATUS, &linked);
    g_gl.DeleteShader(vs);
    g_gl.DeleteShader(fs);
    if (!linked) {
        char log[1024] = {0};
        g_gl.GetProgramInfoLog(m_program, sizeof log, nullptr, log);
        if (error) *error = std::string("link: ") + log;
        return false;
    }

    m_uProjection = g_gl.GetUniformLocation(m_program, "uProjection");
    m_uTexture    = g_gl.GetUniformLocation(m_program, "uTexture");

    g_gl.GenVertexArrays(1, &m_vao);
    g_gl.GenBuffers(1, &m_vbo);
    g_gl.BindVertexArray(m_vao);
    g_gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    g_gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    g_gl.EnableVertexAttribArray(0);
    g_gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    g_gl.EnableVertexAttribArray(1);
    g_gl.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    g_gl.EnableVertexAttribArray(2);
    g_gl.BindVertexArray(0);

    // Textura 1x1 blanca para dibujar rectangulos solidos con el mismo shader.
    {
        const unsigned char white[4] = {255, 255, 255, 255};
        glGenTextures(1, &m_whiteTex);
        glBindTexture(GL_TEXTURE_2D, m_whiteTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }

    // Mascara radial para el perfil de mall-evil. No es un asset del mod: solo
    // aporta el alpha; el comando decide color e intensidad cada frame.
    {
        constexpr int size = 128;
        std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 4), 255);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float nx = (x + 0.5f) / (size * 0.5f) - 1.0f;
                const float ny = (y + 0.5f) / (size * 0.5f) - 1.0f;
                float edge = std::clamp((std::sqrt(nx * nx + ny * ny) - 0.35f) / 0.65f,
                                        0.0f, 1.0f);
                edge *= edge;
                pixels[(static_cast<size_t>(y * size + x) * 4) + 3] =
                    static_cast<unsigned char>(std::lround(edge * 255.0f));
            }
        }
        glGenTextures(1, &m_vignetteTex);
        glBindTexture(GL_TEXTURE_2D, m_vignetteTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels.data());
    }

    // FlxText se materializa como una textura dinamica, igual que en OpenFL.
    // En ciertos controladores la primera reserva hecha durante gameplay tarda
    // 80-90 ms aunque el bitmap mida menos de 1 MiB. Comprometemos unas pocas
    // superficies comunes al inicializar el renderer: la cancion solo hace
    // glTexSubImage2D y nunca paga una reserva de backing store inesperada.
    {
        constexpr int poolCount = 2;
        constexpr int poolWidth = 1024;
        constexpr int poolHeight = 256;
        const std::vector<unsigned char> clear(
            static_cast<std::size_t>(poolWidth) * poolHeight * 4u, 0u);
        m_textTexturePool.reserve(poolCount);
        for (int index = 0; index < poolCount; ++index) {
            Texture texture;
            glGenTextures(1, &texture.id);
            glBindTexture(GL_TEXTURE_2D, texture.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, poolWidth, poolHeight, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, clear.data());
            texture.texW = poolWidth;
            texture.texH = poolHeight;
            texture.dynamic = true;
            texture.ok = true;
            m_textTexturePool.push_back(std::move(texture));
            m_vramBytes += static_cast<std::size_t>(poolWidth) * poolHeight * 4u;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    m_scratch = new std::vector<float>();
    return true;
}

void GlRenderer::shutdown() {
    // Antes que nada: el worker no puede seguir vivo con el renderer a medio
    // desmontar, y `join` es lo unico que garantiza que no queda decodificando.
    stopDecodeWorker();
    clearShaderProgramCache();
    m_asyncDecode = false;
    if (!m_textures.empty()) {
        std::vector<GLuint> ids;
        for (auto& kv : m_textures) if (kv.second.id) ids.push_back(kv.second.id);
        if (!ids.empty()) glDeleteTextures((GLsizei)ids.size(), ids.data());
        m_textures.clear();
    }
    if (!m_textTexturePool.empty()) {
        std::vector<GLuint> ids;
        for (const Texture& texture : m_textTexturePool)
            if (texture.id) ids.push_back(texture.id);
        if (!ids.empty()) glDeleteTextures((GLsizei)ids.size(), ids.data());
        m_textTexturePool.clear();
    }
    if (m_whiteTex) { glDeleteTextures(1, &m_whiteTex); m_whiteTex = 0; }
    if (m_vignetteTex) { glDeleteTextures(1, &m_vignetteTex); m_vignetteTex = 0; }
    if (m_previewTex) { glDeleteTextures(1, &m_previewTex); m_previewTex = 0; }
    if (m_exactTex) { glDeleteTextures(1, &m_exactTex); m_exactTex = 0; }
    if (m_sceneTex) { glDeleteTextures(1, &m_sceneTex); m_sceneTex = 0; }
    m_sceneW = m_sceneH = 0;
    m_sceneCaptured = false;
    if (m_cameraSamplerTex) {
        glDeleteTextures(1, &m_cameraSamplerTex);
        m_cameraSamplerTex = 0;
    }
    m_cameraSamplerW = m_cameraSamplerH = 0;
    m_cameraSamplerReady = m_cameraSamplerCapturing = false;
    for (int i = 0; i < 2; ++i)
        if (m_passTex[i]) { glDeleteTextures(1, &m_passTex[i]); m_passTex[i] = 0; }
    m_previewW = m_previewH = 0;
    m_exactW = m_exactH = 0;
    m_passW = m_passH = 0;
    for(auto& entry:m_feedbackHistory) {
        if(entry.second.previous.texture) glDeleteTextures(1,&entry.second.previous.texture);
        if(entry.second.output.texture) glDeleteTextures(1,&entry.second.output.texture);
    }
    m_feedbackHistory.clear();
    if (g_gl.ok) {
        if (m_vbo)     { g_gl.DeleteBuffers(1, &m_vbo);       m_vbo = 0; }
        if (m_vao)     { g_gl.DeleteVertexArrays(1, &m_vao); m_vao = 0; }
        if (m_program) { g_gl.DeleteProgram(m_program);      m_program = 0; }
        if (m_blitProgram) { g_gl.DeleteProgram(m_blitProgram); m_blitProgram = 0; }
        for (const auto& entry : m_visualizerPostPrograms)
            if (entry.second) g_gl.DeleteProgram(entry.second);
        m_visualizerPostPrograms.clear();
        {
        }
        if (m_visualizerLayerTex) { glDeleteTextures(1,&m_visualizerLayerTex); m_visualizerLayerTex=0; }
        if (m_visualizerLayerFbo) { g_gl.DeleteFramebuffers(1,&m_visualizerLayerFbo); m_visualizerLayerFbo=0; }
        m_visualizerLayerActive=false;
        m_visualizerLayerW=m_visualizerLayerH=0;
        if (m_passFbo) { g_gl.DeleteFramebuffers(1, &m_passFbo); m_passFbo = 0; }
        if (m_sceneFbo) { g_gl.DeleteFramebuffers(1, &m_sceneFbo); m_sceneFbo = 0; }
        if (m_cameraSamplerFbo) {
            g_gl.DeleteFramebuffers(1, &m_cameraSamplerFbo);
            m_cameraSamplerFbo = 0;
        }
    }
    delete static_cast<std::vector<float>*>(m_scratch);
    m_scratch = nullptr;
    m_vramBytes = 0;
    m_texturesEvicted = m_texturesDownscaled = 0;
    m_textureResidencyMisses.clear();
    m_textureResidencyMissSeen.clear();
    m_retainedTexturePaths.clear();
}

// Reduce por un factor entero. El promedio de RGB se pondera por alfa: los
// sprites de FNF llevan alfa recto y bordes transparentes en negro, y promediar
// el color con ellos deja un halo oscuro alrededor de cada figura.
static void downscaleRgba(const unsigned char* src, int sw, int sh, int factor,
                          std::vector<unsigned char>& dst, int& dw, int& dh) {
    dw = (sw + factor - 1) / factor;
    dh = (sh + factor - 1) / factor;
    dst.assign((size_t)dw * dh * 4u, 0);
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            unsigned long long cr = 0, cg = 0, cb = 0, ca = 0;
            int n = 0;
            for (int dy = 0; dy < factor; ++dy) {
                const int sy = y * factor + dy;
                if (sy >= sh) break;
                for (int dx = 0; dx < factor; ++dx) {
                    const int sx = x * factor + dx;
                    if (sx >= sw) break;
                    const unsigned char* q = src + ((size_t)sy * sw + sx) * 4u;
                    cr += (unsigned long long)q[0] * q[3];
                    cg += (unsigned long long)q[1] * q[3];
                    cb += (unsigned long long)q[2] * q[3];
                    ca += q[3];
                    ++n;
                }
            }
            unsigned char* o = dst.data() + ((size_t)y * dw + x) * 4u;
            o[0] = ca ? (unsigned char)(cr / ca) : 0;
            o[1] = ca ? (unsigned char)(cg / ca) : 0;
            o[2] = ca ? (unsigned char)(cb / ca) : 0;
            o[3] = n ? (unsigned char)(ca / n) : 0;
        }
    }
}

void GlRenderer::evictUntilFits(size_t needed) {
    if (m_vramBudget == 0) return;
    while (m_vramBytes + needed > m_vramBudget) {
        auto victim = m_textures.end();
        for (auto it = m_textures.begin(); it != m_textures.end(); ++it) {
            // Lo usado en ESTE frame esta cogido por el bucle de dibujo.
            if (!it->second.id || it->second.lastUsedFrame >= m_frameIndex) continue;
            if (victim == m_textures.end() ||
                it->second.lastUsedFrame < victim->second.lastUsedFrame)
                victim = it;
        }
        // Si lo unico que queda es el conjunto vivo del frame, se pasa del
        // techo y se dibuja igual: el presupuesto acota la CACHE, no lo que la
        // escena necesita ahora. Recortar aqui seria dibujar mal.
        if (victim == m_textures.end()) return;
        const size_t freed = (size_t)victim->second.texW * victim->second.texH * 4u;
        glDeleteTextures(1, &victim->second.id);
        m_vramBytes = m_vramBytes > freed ? m_vramBytes - freed : 0;
        m_textures.erase(victim);
        ++m_texturesEvicted;
    }
}

// Todo lo que no toca OpenGL. Corre en el hilo del frame cuando la carga es
// sincrona y en el worker cuando no; por eso no consulta la VFS ni el estado del
// renderer: recibe la ruta ya resuelta y los dos limites que necesita.
GlRenderer::DecodedTexture GlRenderer::decodeTexture(
        const std::string& virtualPath, const std::string& realPath,
        int maxTextureSize, std::size_t vramBudget) {
    DecodedTexture out;
    out.path = virtualPath;
    if (realPath.empty()) return out;
    const auto started = std::chrono::steady_clock::now();
    constexpr size_t kEncodedLimit = 128u * 1024u * 1024u;
    constexpr size_t kDecodedLimit = 256u * 1024u * 1024u;
    constexpr size_t kResidentLimit = 128u * 1024u * 1024u;
    try {

    // stbi no acepta rutas UTF-16; abrimos nosotros y decodificamos de memoria
    // para no romper con nombres no-ASCII ni con espacios.
    std::unique_ptr<FILE, decltype(&fclose)> f(
        _wfopen(std::filesystem::u8path(realPath).wstring().c_str(), L"rb"), &fclose);
    if (!f) return out;
    if (_fseeki64(f.get(), 0, SEEK_END) != 0) return out;
    const auto size = _ftelli64(f.get());
    if (size <= 0 || static_cast<unsigned long long>(size) > kEncodedLimit ||
        _fseeki64(f.get(), 0, SEEK_SET) != 0) return out;
    std::vector<unsigned char> buf(static_cast<size_t>(size));
    const bool read = fread(buf.data(), 1, buf.size(), f.get()) == buf.size();
    f.reset();
    if (!read) return out;

    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(buf.data(), static_cast<int>(buf.size()), &w, &h, &comp) ||
        w <= 0 || h <= 0 || w > 16384 || h > 16384 ||
        static_cast<size_t>(w) * static_cast<size_t>(h) > kDecodedLimit / 4u)
        return out;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> px(
        stbi_load_from_memory(buf.data(), static_cast<int>(buf.size()), &w, &h, &comp, 4),
        &stbi_image_free);
    if (!px) return out;
    buf.clear();
    buf.shrink_to_fit();

    // Solo se reduce cuando la alternativa es fallar, nunca por norma: FML
    // compara sus capturas pixel a pixel contra el motor (§1.5) y reducir
    // siempre invalidaria esa medida. Dos razones para hacerlo, ambas
    // ineludibles:
    //   1. el limite del hardware, que el driver rechaza en silencio dejando
    //      una textura negra;
    //   2. una sola imagen mas grande que TODO el presupuesto, donde desalojar
    //      no puede ayudar.
    int texW = w, texH = h, factor = 1;
    const int cap = maxTextureSize > 0 ? maxTextureSize : 8192;
    while (texW > cap || texH > cap) {
        ++factor; texW = (w + factor - 1) / factor; texH = (h + factor - 1) / factor;
    }
    if (vramBudget > 0)
        while ((size_t)texW * texH * 4u > vramBudget) {
            ++factor; texW = (w + factor - 1) / factor; texH = (h + factor - 1) / factor;
        }
    while (static_cast<size_t>(texW) * texH * 4u > kResidentLimit) {
        ++factor; texW = (w + factor - 1) / factor; texH = (h + factor - 1) / factor;
    }

    if (factor > 1) {
        downscaleRgba(px.get(), w, h, factor, out.pixels, texW, texH);
        out.downscaled = true;
    } else {
        out.pixels.assign(px.get(), px.get() + (size_t)w * (size_t)h * 4u);
    }

    // Mascara de alfa submuestreada 1/4 para seleccionar por pixel sin guardar
    // la textura entera en RAM.
    const int step = 4;
    out.maskW = (w + step - 1) / step;
    out.maskH = (h + step - 1) / step;
    out.alpha.resize((size_t)out.maskW * out.maskH);
    for (int my = 0; my < out.maskH; ++my) {
        for (int mx = 0; mx < out.maskW; ++mx) {
            const int sxp = mx * step, syp = my * step;
            unsigned char best = 0;
            for (int dy = 0; dy < step && syp + dy < h; ++dy)
                for (int dx = 0; dx < step && sxp + dx < w; ++dx) {
                    const unsigned char a = px.get()[(((size_t)(syp + dy) * w) + (sxp + dx)) * 4 + 3];
                    if (a > best) best = a;
                }
            out.alpha[(size_t)my * out.maskW + mx] = best;
        }
    }
    // `w`/`h` sigue siendo el tamano del archivo: las UV se normalizan con el,
    // asi que la geometria no cambia.
    out.w = w; out.h = h; out.texW = texW; out.texH = texH;
    out.ok = true;
    out.decodeMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    } catch (const std::bad_alloc&) {
        out = DecodedTexture{};
        out.path = virtualPath;
    }
    return out;
}

// La unica parte que necesita OpenGL. Siempre en el hilo del frame.
const GlRenderer::Texture* GlRenderer::residentFromDecode(DecodedTexture&& decoded) {
    Texture t;
    if (decoded.ok && !decoded.pixels.empty()) {
        if (decoded.downscaled) ++m_texturesDownscaled;
        evictUntilFits((size_t)decoded.texW * decoded.texH * 4u);

        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, decoded.texW, decoded.texH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, decoded.pixels.data());

        t.maskW = decoded.maskW; t.maskH = decoded.maskH;
        t.alpha = std::move(decoded.alpha);
        t.w = decoded.w; t.h = decoded.h;
        t.texW = decoded.texW; t.texH = decoded.texH;
        t.ok = true;
        t.lastUsedFrame = m_frameIndex;
        m_vramBytes += (size_t)decoded.texW * (size_t)decoded.texH * 4u;
    }
    auto& slot = m_textures[decoded.path] = t;
    return slot.ok ? &slot : nullptr;
}

void GlRenderer::startDecodeWorker() {
    if (m_decodeThread.joinable()) return;
    m_decodeStop = false;
    m_decodeThread = std::thread([this] {
        for (;;) {
            std::pair<std::string, std::string> job;
            {
                std::unique_lock<std::mutex> lock(m_decodeMutex);
                m_decodeWake.wait(lock, [this] {
                    return m_decodeStop || !m_decodeQueue.empty();
                });
                if (m_decodeStop) return;
                job = std::move(m_decodeQueue.front());
                m_decodeQueue.pop_front();
            }
            // Los dos limites se leen fuera del lock: no cambian durante la
            // sesion y el worker no debe tocar el resto del estado.
            DecodedTexture decoded = decodeTexture(job.first, job.second,
                                                   m_maxTextureSize, m_vramBudget);
            std::unique_lock<std::mutex> lock(m_decodeMutex);
            m_decodeWake.wait(lock, [this] {
                return m_decodeStop || m_decodeReady.empty();
            });
            if (m_decodeStop) return;
            m_decodeReady.push_back(std::move(decoded));
        }
    });
}

void GlRenderer::stopDecodeWorker() {
    if (m_decodeThread.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_decodeMutex);
            m_decodeStop = true;
        }
        m_decodeWake.notify_all();
        m_decodeThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        m_decodeQueue.clear();
        m_decodeReady.clear();
        m_decodeInFlight.clear();
    }
    // La subida parcial pertenece al hilo principal, no al worker. Al apagar
    // el modo asincrono tambien hay que soltar su nombre OpenGL: aun no esta en
    // m_textures y, sin esto, quedaria fuera del barrido normal de shutdown.
    if (m_pendingUpload.id) {
        glDeleteTextures(1, &m_pendingUpload.id);
        const size_t held = static_cast<size_t>(m_pendingUpload.decoded.texW) *
                            m_pendingUpload.decoded.texH * 4u;
        m_vramBytes = m_vramBytes > held ? m_vramBytes - held : 0;
    }
    m_pendingUpload = PendingTextureUpload{};
}

void GlRenderer::setAsyncTextureDecode(bool enabled) {
    if (m_asyncDecode == enabled) return;
    m_asyncDecode = enabled;
    if (enabled) {
        // El limite del hardware se consulta ahora: el worker no puede llamar a
        // OpenGL para averiguarlo.
        if (m_maxTextureSize == 0) glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);
        startDecodeWorker();
    } else {
        stopDecodeWorker();
    }
}

int GlRenderer::pumpTextureUploads() {
    // Cuatro MiB por frame mantienen una transferencia individual por debajo
    // del presupuesto de 16.6 ms incluso con atlases enormes. Antes se hacia
    // `swap` de TODA la cola y cuatro texturas listas podian bloquear juntas
    // 20-40 ms. Una imagen puede aparecer unos cuadros despues, pero el audio,
    // los scripts y la camara no se congelan mientras tanto.
    constexpr size_t kUploadBytesPerFrame = 4u * 1024u * 1024u;

    if (!m_pendingUpload.active()) {
        DecodedTexture decoded;
        {
            std::lock_guard<std::mutex> lock(m_decodeMutex);
            if (m_decodeReady.empty()) return 0;
            decoded = std::move(m_decodeReady.front());
            m_decodeReady.erase(m_decodeReady.begin());
        }
        m_decodeWake.notify_one();

        const std::string path = decoded.path;
        // Si un warm sincrono la hizo residente mientras el worker estaba
        // decodificando, este resultado ya no tiene trabajo que hacer.
        if (m_textures.find(path) != m_textures.end()) {
            std::lock_guard<std::mutex> lock(m_decodeMutex);
            m_decodeInFlight.erase(path);
            return 0;
        }
        if (!decoded.ok || decoded.pixels.empty()) {
            residentFromDecode(std::move(decoded)); // conserva el fallo en cache
            std::lock_guard<std::mutex> lock(m_decodeMutex);
            m_decodeInFlight.erase(path);
            return 0;
        }

        m_pendingUpload.decoded = std::move(decoded);
        DecodedTexture& source = m_pendingUpload.decoded;
        if (source.downscaled) ++m_texturesDownscaled;
        const size_t held = static_cast<size_t>(source.texW) * source.texH * 4u;
        evictUntilFits(held);

        const auto allocateStart = std::chrono::steady_clock::now();
        glGenTextures(1, &m_pendingUpload.id);
        glBindTexture(GL_TEXTURE_2D, m_pendingUpload.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Solo reserva almacenamiento. Los pixeles llegan por franjas abajo.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source.texW, source.texH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_vramBytes += held;
        const float allocateMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - allocateStart).count();
        m_pendingUpload.uploadMs += allocateMs;
        m_pendingUpload.allocateMs = allocateMs;
        m_pendingUpload.maxSliceMs = std::max(
            m_pendingUpload.maxSliceMs, allocateMs);
    }

    PendingTextureUpload& pending = m_pendingUpload;
    DecodedTexture& source = pending.decoded;
    const size_t bytesPerRow = static_cast<size_t>(source.texW) * 4u;
    const int remaining = source.texH - pending.nextRow;
    const int rows = std::min(remaining, std::max(1, static_cast<int>(
        kUploadBytesPerFrame / std::max<size_t>(1u, bytesPerRow))));

    const auto uploadStart = std::chrono::steady_clock::now();
    glBindTexture(GL_TEXTURE_2D, pending.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, pending.nextRow, source.texW, rows,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    source.pixels.data() + static_cast<size_t>(pending.nextRow) * bytesPerRow);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    pending.nextRow += rows;
    const float sliceMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - uploadStart).count();
    pending.uploadMs += sliceMs;
    pending.maxSliceMs = std::max(pending.maxSliceMs, sliceMs);
    if (pending.nextRow < source.texH) return 0;

    const std::string path = source.path;
    const float decodeMs = source.decodeMs;
    const float uploadMs = pending.uploadMs;
    const float allocateMs = pending.allocateMs;
    const float maxSliceMs = pending.maxSliceMs;
    const int logicalW = source.w;
    const int logicalH = source.h;
    Texture texture;
    texture.id = pending.id;
    texture.maskW = source.maskW; texture.maskH = source.maskH;
    texture.alpha = std::move(source.alpha);
    texture.w = source.w; texture.h = source.h;
    texture.texW = source.texW; texture.texH = source.texH;
    texture.ok = true;
    texture.lastUsedFrame = m_frameIndex;
    m_textures[path] = std::move(texture);
    pending.id = 0; // ya pertenece a m_textures
    pending = PendingTextureUpload{};
    {
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        m_decodeInFlight.erase(path);
    }
    if (decodeMs + uploadMs > 2.0f && m_textureLoads.size() < 64)
        // `ms` siempre ha significado bloqueo de UN frame. Con una subida por
        // franjas, sumar los veinte cuadros vuelve a inventar un tiron que ya
        // no existe; se informa la peor franja individual.
        m_textureLoads.push_back(
            {path + " [subida]", logicalW, logicalH, maxSliceMs,
             allocateMs >= maxSliceMs ? "reserva" : "transferencia"});
    return 1;
}

const GlRenderer::Texture* GlRenderer::acquire(const std::string& virtualPath) {
    auto it = m_textures.find(virtualPath);
    if (it != m_textures.end()) {
        it->second.lastUsedFrame = m_frameIndex;
        return it->second.ok ? &it->second : nullptr;
    }

    if (m_maxTextureSize == 0)
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);

    const std::string real = m_resolve ? m_resolve(virtualPath) : std::string();

    if (m_asyncDecode) {
        // Se encola y el sprite se salta este frame. Aparecer un par de frames
        // tarde es lo que compra no congelar el frame que la pidio.
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        if (m_decodeInFlight.insert(virtualPath).second) {
            m_decodeQueue.emplace_back(virtualPath, real);
            m_decodeWake.notify_one();
        }
        return nullptr;
    }

    // El techo se mira ANTES de empezar: una vez abierto el archivo no hay
    // forma de partir la decodificacion, asi que lo que se evita es que varias
    // texturas nuevas caigan en el mismo frame. No se cachea el fallo -no se
    // toca m_textures-, de modo que el siguiente frame vuelve a intentarlo.
    if (m_textureBudgetMs > 0.0f && m_textureSpentMs >= m_textureBudgetMs)
        return nullptr;

    const auto loadStart = std::chrono::steady_clock::now();
    const Texture* result = residentFromDecode(
        decodeTexture(virtualPath, real, m_maxTextureSize, m_vramBudget));
    const float loadMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - loadStart).count();
    m_textureSpentMs += loadMs;
    // Solo lo que puede notarse. Una textura pequena no explica un tiron y
    // llenaria el log de ruido.
    if (loadMs > 2.0f && m_textureLoads.size() < 64)
        m_textureLoads.push_back({virtualPath, result ? result->w : 0,
                                  result ? result->h : 0, loadMs, "sincrona"});
    return result;
}

void GlRenderer::prefetchTextures(const std::vector<std::string>& paths) {
    if (!m_asyncDecode) return;
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    for (const std::string& path : paths) {
        if (path.empty() || m_textures.count(path) != 0) continue;
        if (!m_decodeInFlight.insert(path).second) continue;
        m_decodeQueue.emplace_back(path, m_resolve ? m_resolve(path) : std::string());
    }
    m_decodeWake.notify_all();
}

void GlRenderer::prefetchPriorityTextures(const std::vector<std::string>& paths) {
    if (!m_asyncDecode) return;
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    // push_front invierte el orden; recorrer al reves conserva la prioridad
    // declarada (marco, icono jugador, icono rival).
    for (auto path = paths.rbegin(); path != paths.rend(); ++path) {
        if (path->empty() || m_textures.count(*path) != 0) continue;
        if (m_decodeInFlight.insert(*path).second) {
            m_decodeQueue.emplace_front(*path,
                m_resolve ? m_resolve(*path) : std::string());
            continue;
        }
        // Si ya esperaba al final de la cola, promoverla. Una decodificacion
        // que el worker ya tomo no aparece aqui y se deja terminar.
        const auto queued = std::find_if(m_decodeQueue.begin(), m_decodeQueue.end(),
            [&](const auto& item) { return item.first == *path; });
        if (queued == m_decodeQueue.end()) continue;
        auto item = std::move(*queued);
        m_decodeQueue.erase(queued);
        m_decodeQueue.push_front(std::move(item));
    }
    m_decodeWake.notify_all();
}

void GlRenderer::warmTextures(const std::vector<std::string>& paths) {
    // Sincrono y sin techo a proposito: es justo el momento en el que si se
    // puede esperar, y hacerlo aqui es lo que evita que el sprite aparezca dos
    // frames tarde en el primer compas.
    const float budget = m_textureBudgetMs;
    const bool  async  = m_asyncDecode;
    m_textureBudgetMs = 0.0f;
    m_asyncDecode = false;
    for (const std::string& path : paths)
        if (!path.empty()) acquire(path);
    m_asyncDecode = async;
    m_textureBudgetMs = budget;
}

bool GlRenderer::invalidateTexture(const std::string& virtualPath) {
    auto it = m_textures.find(virtualPath);
    if (it == m_textures.end()) return false;
    if (it->second.id) {
        glDeleteTextures(1, &it->second.id);
        const size_t held = (size_t)it->second.texW * it->second.texH * 4u;
        m_vramBytes = m_vramBytes > held ? m_vramBytes - held : 0;
    }
    m_textures.erase(it);
    return true;
}

ImageInfo GlRenderer::imageInfo(const std::string& virtualPath) {
    ImageInfo info;
    if (const Texture* t = acquire(virtualPath)) {
        info.w = t->w; info.h = t->h; info.ok = true;
    }
    return info;
}

GlRenderer::PreviewImage GlRenderer::previewImage(const std::string& virtualPath) {
    PreviewImage out;
    if (const Texture* texture = acquire(virtualPath)) {
        out.texture = texture->id;
        out.width = texture->w;
        out.height = texture->h;
        out.ok = texture->ok && texture->id != 0;
    }
    return out;
}

bool GlRenderer::setPreviewFiltering(const std::string& virtualPath, bool linear) {
    const Texture* texture = acquire(virtualPath);
    if (!texture || !texture->id) return false;
    glBindTexture(GL_TEXTURE_2D, texture->id);
    const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool GlRenderer::uploadDynamicFrame(const std::string& virtualPath,
                                    const unsigned char* bgra,
                                    int width, int height) {
    if (virtualPath.empty() || !bgra || width <= 0 || height <= 0) return false;
    while (glGetError() != GL_NO_ERROR) {}
    Texture& texture = m_textures[virtualPath];
    // La condicion era `texW < width`, o sea: se reutilizaba la textura cuando
    // la imagen nueva CABIA en la anterior. Entonces `glTexSubImage2D` solo
    // pisa la esquina y el resto se queda con lo de antes, mientras el quad se
    // dibuja muestreando la textura ENTERA. Con imagenes de tamano fijo -video-
    // no se nota nunca; con un texto que se acorta al borrar una letra salen
    // las dos cosas a la vez que se veian: las letras viejas que no desaparecen
    // y el texto nuevo aplastado dentro del ancho antiguo.
    //
    // Ahora se rehace en cuanto el tamano CAMBIA, no solo cuando crece, para
    // que lo asignado y lo dibujado coincidan y muestrear 0..1 sea correcto.
    // Un video conserva su tamano cuadro a cuadro, asi que sigue sin reasignar.
    //
    // El fondo de texturas de texto de runtime reparte A PROPOSITO texturas mas
    // grandes que la imagen, y sus llamantes responden de sus propias UV. Ese
    // camino se deja exactamente como estaba: cambiarlo aqui vaciaria el fondo
    // -una textura borrada no vuelve a el- para arreglar algo que alli no esta
    // roto.
    const bool pooledText = virtualPath.rfind("runtime:text-slot/", 0) == 0;
    const bool sizeChanged = texture.texW != width || texture.texH != height;
    const bool tooSmall = texture.texW < width || texture.texH < height;
    if (texture.id && (!texture.dynamic || tooSmall ||
                       (!pooledText && sizeChanged))) {
        glDeleteTextures(1, &texture.id);
        const size_t held = static_cast<size_t>(texture.texW) * texture.texH * 4u;
        m_vramBytes = m_vramBytes > held ? m_vramBytes - held : 0;
        texture = Texture{};
    }
    bool pixelsUploaded = false;
    if (!texture.id) {
        const bool runtimeText = virtualPath.rfind("runtime:text-slot/", 0) == 0;
        if (runtimeText) {
            auto best = m_textTexturePool.end();
            for (auto it = m_textTexturePool.begin();
                 it != m_textTexturePool.end(); ++it) {
                if (it->texW < width || it->texH < height) continue;
                if (best == m_textTexturePool.end() ||
                    static_cast<std::size_t>(it->texW) * it->texH <
                    static_cast<std::size_t>(best->texW) * best->texH)
                    best = it;
            }
            if (best != m_textTexturePool.end()) {
                texture = std::move(*best);
                m_textTexturePool.erase(best);
            }
        }
        if (!texture.id) {
            evictUntilFits(static_cast<size_t>(width) * height * 4u);
            glGenTextures(1, &texture.id);
            glBindTexture(GL_TEXTURE_2D, texture.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, bgra);
            texture.texW = width;
            texture.texH = height;
            m_vramBytes += static_cast<size_t>(width) * height * 4u;
            pixelsUploaded = true;
        }
    }
    if (!pixelsUploaded) {
        glBindTexture(GL_TEXTURE_2D, texture.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    texture.w = width;
    texture.h = height;
    texture.ok = true;
    texture.dynamic = true;
    texture.lastUsedFrame = m_frameIndex;
    // El video es opaco y se selecciona por su quad. No se guarda una segunda
    // copia de alfa de un frame que cambia 30/60 veces por segundo.
    texture.alpha.clear(); texture.maskW = texture.maskH = 0;
    return glGetError() == GL_NO_ERROR;
}

bool GlRenderer::beginOffscreenFrame(int width, int height) {
    if (!g_gl.ok || width <= 0 || height <= 0) return false;
    while (glGetError() != GL_NO_ERROR) {}
    if (!m_offscreenFbo) g_gl.GenFramebuffers(1, &m_offscreenFbo);
    if (!m_offscreenTex) {
        glGenTextures(1, &m_offscreenTex);
        glBindTexture(GL_TEXTURE_2D, m_offscreenTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_offscreenW = m_offscreenH = 0;
    }
    if (m_offscreenW != width || m_offscreenH != height) {
        glBindTexture(GL_TEXTURE_2D, m_offscreenTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_offscreenW = width;
        m_offscreenH = height;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, m_offscreenFbo);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_offscreenTex, 0);
    if (g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    // Se guarda el viewport para devolverlo: si se quedara con el tamano del
    // fotograma exportado, la interfaz se dibujaria del tamano equivocado en
    // cuanto terminara la exportacion.
    glGetIntegerv(GL_VIEWPORT, m_offscreenViewport);
    glViewport(0, 0, width, height);
    // Fondo transparente y no negro: el lienzo pinta el suyo encima si lo
    // tiene, y si no lo tiene un PNG con alfa es mas util que uno negro.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

bool GlRenderer::endOffscreenFrame(std::vector<unsigned char>& rgba) {
    if (!g_gl.ok || !m_offscreenFbo || m_offscreenW <= 0 || m_offscreenH <= 0)
        return false;
    const std::size_t rowBytes = static_cast<std::size_t>(m_offscreenW) * 4u;
    rgba.assign(rowBytes * static_cast<std::size_t>(m_offscreenH), 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_offscreenW, m_offscreenH, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(m_offscreenViewport[0], m_offscreenViewport[1],
               m_offscreenViewport[2], m_offscreenViewport[3]);
    // Alfa RECTA, no premultiplicada. ImGui mezcla con
    // `glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE,
    // ONE_MINUS_SRC_ALPHA)`, y sobre un destino transparente eso deja el color
    // ya multiplicado por su alfa: un blanco al 50 % sale (128,128,128,128).
    // Un PNG -y cualquier programa que componga despues- espera (255,255,255,
    // 128). Sin deshacerlo, TODO lo semitransparente -bordes suavizados,
    // fundidos, blur- sale oscuro al componerlo fuera, y eso solo se descubre
    // cuando ya has metido la secuencia en otro programa.
    //
    // Con el fondo opaco el alfa es 255 en todas partes y esto no cambia ni un
    // pixel, asi que no hace falta saber si la exportacion lleva alfa o no.
    for (std::size_t at = 0; at + 3 < rgba.size(); at += 4) {
        const unsigned int alpha = rgba[at + 3];
        if (alpha == 0 || alpha == 255) continue;
        for (int channel = 0; channel < 3; ++channel) {
            const unsigned int value =
                (static_cast<unsigned int>(rgba[at + channel]) * 255u + alpha / 2u) / alpha;
            rgba[at + channel] = static_cast<unsigned char>(std::min(255u, value));
        }
    }

    // GL entrega la imagen con el origen abajo a la izquierda; stb y el
    // escritor de GIF la quieren de arriba abajo. Se le da la vuelta aqui, una
    // sola vez, para que ningun llamante tenga que acordarse.
    std::vector<unsigned char> row(rowBytes);
    for (int y = 0; y < m_offscreenH / 2; ++y) {
        unsigned char* top = rgba.data() + rowBytes * static_cast<std::size_t>(y);
        unsigned char* bottom = rgba.data() +
            rowBytes * static_cast<std::size_t>(m_offscreenH - 1 - y);
        std::memcpy(row.data(), top, rowBytes);
        std::memcpy(top, bottom, rowBytes);
        std::memcpy(bottom, row.data(), rowBytes);
    }
    return glGetError() == GL_NO_ERROR;
}

GlRenderer::PreviewImage GlRenderer::finishOffscreenPreview() {
    if (!g_gl.ok || !m_offscreenFbo || !m_offscreenTex ||
        m_offscreenW <= 0 || m_offscreenH <= 0) return {};
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(m_offscreenViewport[0], m_offscreenViewport[1],
               m_offscreenViewport[2], m_offscreenViewport[3]);
    return {m_offscreenTex, m_offscreenW, m_offscreenH, true};
}

void GlRenderer::releaseOffscreenFrame() {
    if (m_offscreenTex) { glDeleteTextures(1, &m_offscreenTex); m_offscreenTex = 0; }
    if (m_offscreenFbo && g_gl.ok) {
        g_gl.DeleteFramebuffers(1, &m_offscreenFbo);
        m_offscreenFbo = 0;
    }
    m_offscreenW = m_offscreenH = 0;
}

bool GlRenderer::dynamicFrameReady(const std::string& virtualPath) const {
    const auto found = m_textures.find(virtualPath);
    return found != m_textures.end() && found->second.dynamic &&
           found->second.ok && found->second.id != 0;
}

void GlRenderer::removeDynamicFrame(const std::string& virtualPath) {
    const auto found = m_textures.find(virtualPath);
    if (found == m_textures.end() || !found->second.dynamic) return;
    const bool runtimeText = virtualPath.rfind("runtime:text-slot/", 0) == 0;
    if (runtimeText && found->second.id && m_textTexturePool.size() < 2u &&
        found->second.texW == 1024 && found->second.texH == 256) {
        Texture reusable = std::move(found->second);
        reusable.w = reusable.h = 0;
        reusable.lastUsedFrame = 0;
        reusable.alpha.clear();
        reusable.maskW = reusable.maskH = 0;
        m_textures.erase(found);
        m_textTexturePool.push_back(std::move(reusable));
        return;
    }
    invalidateTexture(virtualPath);
}

void GlRenderer::flush() {
    auto* verts = static_cast<std::vector<float>*>(m_scratch);
    if (!verts || verts->empty() || !m_batchTexture) { if (verts) verts->clear(); return; }

    glBindTexture(GL_TEXTURE_2D, m_batchTexture);
    const auto uploadStart = m_traceDrawTiming
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    g_gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
    g_gl.BufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(verts->size() * sizeof(float)),
                    verts->data(), GL_DYNAMIC_DRAW);
    if (m_traceDrawTiming) {
        const auto now = std::chrono::steady_clock::now();
        m_drawTiming.batchUploadMs +=
            std::chrono::duration<double, std::milli>(now - uploadStart).count();
        ++m_drawTiming.batchFlushes;
    }
    // Floats por vertice: pos2 + uv3 + color4. Estaba escrito a mano como 8 y
    // al pasar la UV a tres componentes dibujaba un 12,5% de vertices de mas,
    // leyendo mas alla del buffer. Se deriva de la constante para que el
    // formato y el conteo no puedan volver a separarse.
    const auto issueStart = m_traceDrawTiming
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    glDrawArrays(GL_TRIANGLES, 0,
                 (GLsizei)(verts->size() / kFloatsPerVertex));
    if (m_traceDrawTiming)
        m_drawTiming.batchIssueMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - issueStart).count();
    ++m_drawCalls;
    verts->clear();
}

void GlRenderer::drawGameBounds(const ViewportTransform& view, int viewportW, int viewportH) {
    const float fit = std::min(viewportW / kGameWidth, viewportH / kGameHeight) * view.zoom;
    const float x0 = viewportW * 0.5f - kGameWidth  * 0.5f * fit + view.panX;
    const float y0 = viewportH * 0.5f - kGameHeight * 0.5f * fit + view.panY;
    drawOutline(x0, y0, x0 + kGameWidth * fit, y0 + kGameHeight * fit,
                1.0f, 1.0f, 1.0f, 0.35f, 2.0f);
}

bool GlRenderer::readFramebuffer(std::vector<unsigned char>& rgba,
                                 int viewportW, int viewportH) {
    if (viewportW <= 0 || viewportH <= 0) return false;
    rgba.assign((size_t)viewportW * viewportH * 4, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, viewportW, viewportH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    // OpenGL entrega las filas de abajo a arriba; hay que darles la vuelta.
    const size_t stride = (size_t)viewportW * 4;
    std::vector<unsigned char> row(stride);
    for (int y = 0; y < viewportH / 2; ++y) {
        unsigned char* a = rgba.data() + (size_t)y * stride;
        unsigned char* b = rgba.data() + (size_t)(viewportH - 1 - y) * stride;
        memcpy(row.data(), a, stride);
        memcpy(a, b, stride);
        memcpy(b, row.data(), stride);
    }
    return true;
}

bool GlRenderer::screenshot(const std::string& path, int viewportW, int viewportH) {
    std::vector<unsigned char> px;
    if (!readFramebuffer(px, viewportW, viewportH)) return false;
    return stbi_write_png(path.c_str(), viewportW, viewportH, 4, px.data(),
                          viewportW * 4) != 0;
}

void GlRenderer::beginFrame(int viewportW, int viewportH, float r, float g, float b) {
    // Marca el frame en curso. `evictUntilFits` no toca nada con este numero,
    // que es justo lo que impide liberar una textura que el bucle de dibujo
    // tiene cogida.
    ++m_frameIndex;
    if (m_traceDrawTiming) m_drawTiming = DrawTiming{};
    m_textureSpentMs = 0.0f;
    m_cameraSamplerReady = false;
    m_cameraSamplerCapturing = false;
    glViewport(0, 0, viewportW, viewportH);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}


GlRenderer::ScreenQuad GlRenderer::quadOf(const DrawCmd& c, const Camera& cam,
                                          const ViewportTransform& view,
                                          int viewportW, int viewportH) const {
    const float fit  = std::min(viewportW / kGameWidth, viewportH / kGameHeight) * view.zoom;
    const float offX = viewportW * 0.5f - kGameWidth  * 0.5f * fit + view.panX;
    const float offY = viewportH * 0.5f - kGameHeight * 0.5f * fit + view.panY;
    const float SX = kGameWidth * 0.5f, SY = kGameHeight * 0.5f;

    auto toScreen = [&](float wx, float wy, float& ox, float& oy) {
        const float rawX = wx - (cam.x - SX) * c.scrollX;
        const float rawY = wy - (cam.y - SY) * c.scrollY;
        float gx = (rawX - SX) * cam.zoom + SX;
        float gy = (rawY - SY) * cam.zoom + SY;
        if (c.zoomFactor != 1.0f && cam.zoom != 0.0f) {
            const float requested = std::max(1.0f + (cam.zoom - 1.0f) * c.zoomFactor, 0.0f);
            const float diff = requested / cam.zoom;
            gx = (gx - SX) * diff + SX;
            gy = (gy - SY) * diff + SY;
        }
        // Debe coincidir con `draw`: de otro modo una cámara girada pinta el
        // sprite en un lugar y el inspector intenta seleccionarlo en otro.
        if (cam.angle != 0.0f) {
            const float radians = cam.angle * 3.14159265358979323846f / 180.0f;
            const float cs = std::cos(radians), sn = std::sin(radians);
            const float dx = gx - SX, dy = gy - SY;
            gx = dx * cs - dy * sn + SX;
            gy = dx * sn + dy * cs + SY;
        }
        ox = gx * fit + offX;
        oy = gy * fit + offY;
    };

    // AABB de las cuatro esquinas: con matriz arbitraria —o con un quad de
    // perspectiva— el rectangulo local ya no es un rectangulo en pantalla.
    // Comparte `drawCmdCorners` con el dibujo a proposito: derivar las esquinas
    // por separado dejaria quads visibles donde no se pueden seleccionar.
    float wx[4], wy[4], qw[4];
    drawCmdCorners(c, wx, wy, qw);
    ScreenQuad q;
    for (int k = 0; k < 4; ++k) {
        float sx = 0, sy = 0;
        toScreen(wx[k], wy[k], sx, sy);
        if (k == 0) { q.x0 = q.x1 = sx; q.y0 = q.y1 = sy; }
        else {
            q.x0 = std::min(q.x0, sx); q.x1 = std::max(q.x1, sx);
            q.y0 = std::min(q.y0, sy); q.y1 = std::max(q.y1, sy);
        }
    }
    return q;
}

float GlRenderer::worldToScreenScale(const DrawCmd& c, const Camera& cam,
                                     const ViewportTransform& view,
                                     int viewportW, int viewportH) const {
    const float fit = std::min(viewportW / kGameWidth, viewportH / kGameHeight) * view.zoom;
    float diff = 1.0f;
    if (c.zoomFactor != 1.0f && cam.zoom != 0.0f)
        diff = std::max(1.0f + (cam.zoom - 1.0f) * c.zoomFactor, 0.0f) / cam.zoom;
    return fit * cam.zoom * diff;
}

int GlRenderer::pick(const RenderList& list, const ViewportTransform& view,
                     int viewportW, int viewportH, float mouseX, float mouseY) {
    // De arriba abajo: el ultimo dibujado es el que esta delante.
    for (int i = (int)list.cmds.size() - 1; i >= 0; --i) {
        const DrawCmd& c = list.cmds[i];
        if (!c.visible || c.alpha <= 0.01f) continue;
        if (!c.pickable) continue;                           // HUD: nunca roba clicks
        const bool solid = c.texture == DrawCmd::SolidTexture;
        if (c.texture == DrawCmd::VignetteTexture) continue; // nunca roba clicks
        if (!solid && (c.texture < 0 || c.texture >= (int)list.textures.size())) continue;

        const ScreenQuad q = quadOf(c, list.camera, view, viewportW, viewportH);
        if (mouseX < q.x0 || mouseX > q.x1 || mouseY < q.y0 || mouseY > q.y1) continue;

        if (solid) return i;

        const Texture* tex = acquire(list.textures[c.texture]);
        if (!tex || tex->alpha.empty()) return i;   // sin mascara: vale la caja

        float u = (mouseX - q.x0) / std::max(1.0f, q.x1 - q.x0);
        float v = (mouseY - q.y0) / std::max(1.0f, q.y1 - q.y0);
        if (c.flipX) u = 1.0f - u;
        if (c.flipY) v = 1.0f - v;

        const float px = c.sx + u * c.sw;
        const float py = c.sy + v * c.sh;
        const int mx = std::min(tex->maskW - 1, std::max(0, (int)(px / 4.0f)));
        const int my = std::min(tex->maskH - 1, std::max(0, (int)(py / 4.0f)));
        if (tex->alpha[(size_t)my * tex->maskW + mx] > 12) return i;
    }
    return -1;
}

void GlRenderer::drawOutline(float x0, float y0, float x1, float y1,
                             float r, float g, float b, float a, float t) {
    if (!g_gl.ok || !m_whiteTex) return;
    auto* verts = static_cast<std::vector<float>*>(m_scratch);
    verts->clear();
    auto rect = [&](float ax, float ay, float bx, float by) {
        // pos2 + uv3 + color4: la tercera componente de UV es 1 porque un
        // contorno no lleva perspectiva. Emitir 8 floats aqui desalineaba el
        // buffer que comparte con el batcher y el resto del frame salia como
        // triangulos de basura.
        const float q[6][9] = {
            {ax, ay, 0, 0, 1, r, g, b, a}, {bx, ay, 1, 0, 1, r, g, b, a},
            {bx, by, 1, 1, 1, r, g, b, a}, {ax, ay, 0, 0, 1, r, g, b, a},
            {bx, by, 1, 1, 1, r, g, b, a}, {ax, by, 0, 1, 1, r, g, b, a},
        };
        verts->insert(verts->end(), &q[0][0], &q[0][0] + 6 * kFloatsPerVertex);
    };
    rect(x0, y0, x1, y0 + t);
    rect(x0, y1 - t, x1, y1);
    rect(x0, y0, x0 + t, y1);
    rect(x1 - t, y0, x1, y1);

    m_batchTexture = m_whiteTex;
    g_gl.UseProgram(m_program);
    g_gl.BindVertexArray(m_vao);
    flush();
    g_gl.BindVertexArray(0);
}

void GlRenderer::draw(const RenderList& list, const ViewportTransform& view,
                     int viewportW, int viewportH, bool resetDrawCalls,
                     bool clipToGameFrame, const float* clipRectGame) {
    if (!g_gl.ok || viewportW <= 0 || viewportH <= 0) return;
    const auto drawStarted = m_traceDrawTiming
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (m_traceDrawTiming) ++m_drawTiming.drawInvocations;
    if (resetDrawCalls) m_drawCalls = 0;

    auto* verts = static_cast<std::vector<float>*>(m_scratch);
    verts->clear();
    m_batchTexture = 0;
    m_batchAntialiasing = true;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Proyeccion ortografica en pixeles de pantalla, origen arriba-izquierda.
    const float L = 0.0f, R = (float)viewportW, T = 0.0f, B = (float)viewportH;
    // Textures loaded from disk store their first (top) image row at V=0.
    // While rendering a camera sampler we mirror the framebuffer projection so
    // logical y=0 is also stored at V=0; its consumer can then use the exact
    // OpenFL UV convention without a mod-specific flip.
    const float projectionY = m_cameraSamplerCapturing
        ? 2.0f / (B - T) : 2.0f / (T - B);
    const float projectionTranslateY = m_cameraSamplerCapturing ? -1.0f : 1.0f;
    const float proj[16] = {
        2.0f / (R - L), 0.0f,            0.0f, 0.0f,
        0.0f,           projectionY,      0.0f, 0.0f,
        0.0f,           0.0f,           -1.0f, 0.0f,
        (R + L) / (L - R), projectionTranslateY, 0.0f, 1.0f,
    };
    g_gl.UseProgram(m_program);
    g_gl.UniformMatrix4fv(m_uProjection, 1, GL_FALSE, proj);
    g_gl.Uniform1i(m_uTexture, 0);
    g_gl.ActiveTexture(GL_TEXTURE0);
    g_gl.BindVertexArray(m_vao);

    // Espacio de JUEGO (1280x720) -> ventana. La navegacion del editor solo
    // afecta a esta ultima etapa.
    const float fit = std::min(viewportW / kGameWidth, viewportH / kGameHeight) * view.zoom;
    const float offX = viewportW * 0.5f - kGameWidth  * 0.5f * fit + view.panX;
    const float offY = viewportH * 0.5f - kGameHeight * 0.5f * fit + view.panY;

    // Cada FlxCamera recorta contra su viewport. En el editor camGame se deja
    // abierta a proposito para poder inspeccionar objetos fuera del encuadre,
    // pero las capas superpuestas (camHUD/camOther/custom) deben respetar el
    // rectangulo de 1280x720. Sin este scissor, barras de cine legitimas que un
    // mod coloca justo fuera de pantalla se convertian en franjas negras
    // infinitas sobre todo el lienzo del editor.
    if (clipToGameFrame) {
        // Cada FlxCamera tiene su PROPIO viewport, no el marco entero. Una
        // camara que un script crea con `new FlxCamera(x, y, w, h)` y coloca
        // encima de un objeto del escenario -la pantalla de television de
        // `bonetravaganza`, por ejemplo- solo debe pintar dentro de ese
        // rectangulo. Recortarlas todas al marco de juego hacia que una camara
        // pequena con fondo opaco tapara la pantalla entera.
        const float cx = clipRectGame ? clipRectGame[0] : 0.0f;
        const float cy = clipRectGame ? clipRectGame[1] : 0.0f;
        const float cw = clipRectGame ? clipRectGame[2] : kGameWidth;
        const float ch = clipRectGame ? clipRectGame[3] : kGameHeight;
        const int x0 = std::clamp(static_cast<int>(std::floor(
            offX + cx * fit)), 0, viewportW);
        const int y0 = std::clamp(static_cast<int>(std::floor(
            offY + cy * fit)), 0, viewportH);
        const int x1 = std::clamp(static_cast<int>(std::ceil(
            offX + (cx + cw) * fit)), 0, viewportW);
        const int y1 = std::clamp(static_cast<int>(std::ceil(
            offY + (cy + ch) * fit)), 0, viewportH);
        glEnable(GL_SCISSOR_TEST);
        glScissor(x0, viewportH - y1, std::max(0, x1 - x0),
                  std::max(0, y1 - y0));
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    const float SX = kGameWidth  * 0.5f;   // centro del viewport de juego
    const float SY = kGameHeight * 0.5f;
    const Camera& cam = list.camera;

    auto setSpriteUniform = [&](const SpriteShaderUniform& uniform) {
        uploadRuntimeUniform(uniform);
    };

    auto traceSpriteSampler = [&](const DrawCmd& command,
                                  const SpriteShaderUniform& uniform,
                                  bool drawn) {
        if (!m_traceSpriteShaderBindings || !command.spriteShader ||
            !uniform.isSampler) return;
        const std::string key = command.runtimeObjectId + "|" +
            command.debugName + "|" + command.spriteShader->sourceKey + "|" +
            command.renderCameraId + "|" + uniform.name + "|" +
            uniform.samplerObjectId + "|" +
            (m_cameraSamplerCapturing ? "capture" : "compose") + "|" +
            (m_cameraSamplerReady ? "ready" : "missing") + "|" +
            (drawn ? "drawn" : "skipped");
        if (!m_spriteShaderTraceSeen.insert(key).second) return;
        m_spriteShaderTraces.push_back({
            command.runtimeObjectId,
            command.debugName,
            command.spriteShader->sourceKey,
            command.renderCameraId,
            uniform.name,
            uniform.samplerObjectId,
            m_cameraSamplerCapturing,
            m_cameraSamplerReady,
            drawn
        });
    };

    for (const DrawCmd& c : list.cmds) {
        if (!c.visible || c.alpha <= 0.001f) continue;
        const bool solid = c.texture == DrawCmd::SolidTexture;
        const bool vignette = c.texture == DrawCmd::VignetteTexture;
        const bool procedural = solid || vignette;
        if (!procedural && (c.texture < 0 || c.texture >= (int)list.textures.size())) continue;
        const auto acquireStarted = m_traceDrawTiming && !procedural
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const std::string requestedPath = procedural
            ? std::string() : list.textures[c.texture];
        const std::string retainKey = !c.retainTextureWhileLoading
            ? std::string()
            // El rol de HUD es mas estable que la identidad HScript. Un mod
            // suele destruir iconP1 y crear otro objeto para sustituirlo; la
            // textura anterior debe seguir cubriendo ESE hueco mientras llega
            // la nueva, aunque haya cambiado el runtimeObjectId.
            : (!c.debugName.empty() ? c.debugName + "|" + c.renderCameraId
                                    : c.runtimeObjectId + "|" + c.renderCameraId);
        const Texture* tex = procedural ? nullptr : acquire(requestedPath);
        bool retainedPrevious = false;
        if (!procedural && !tex && !retainKey.empty()) {
            const auto previous = m_retainedTexturePaths.find(retainKey);
            if (previous != m_retainedTexturePaths.end() &&
                previous->second != requestedPath) {
                tex = acquire(previous->second);
                retainedPrevious = tex != nullptr;
            }
        }
        if (m_traceDrawTiming && !procedural) {
            m_drawTiming.textureAcquireMs +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - acquireStarted).count();
            ++m_drawTiming.textureAcquires;
        }
        if (!procedural && !tex) {
            if (c.retainTextureWhileLoading &&
                m_textureResidencyMissSeen.insert(requestedPath).second)
                m_textureResidencyMisses.push_back({
                    requestedPath, c.runtimeObjectId, c.debugName,
                    c.renderCameraId
                });
            continue;
        }
        if (!procedural && !retainedPrevious && !retainKey.empty())
            m_retainedTexturePaths[retainKey] = requestedPath;
        const unsigned int textureId = solid ? m_whiteTex
                                             : (vignette ? m_vignetteTex : tex->id);
        const float logicalTextureW = procedural ? 1.0f : static_cast<float>(tex->w);
        const float logicalTextureH = procedural ? 1.0f : static_cast<float>(tex->h);
        const float textureW = procedural ? 1.0f : static_cast<float>(
            tex->dynamic && tex->texW > tex->w ? tex->texW : tex->w);
        const float textureH = procedural ? 1.0f : static_cast<float>(
            tex->dynamic && tex->texH > tex->h ? tex->texH : tex->h);
        if (!textureId) continue;

        if (textureId != m_batchTexture || c.antialiasing != m_batchAntialiasing) {
            flush();
            const auto bindStarted = m_traceDrawTiming
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            m_batchTexture = textureId;
            m_batchAntialiasing = c.antialiasing;
            glBindTexture(GL_TEXTURE_2D, m_batchTexture);
            const GLint filter = c.antialiasing ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            if (m_traceDrawTiming)
                m_drawTiming.batchBindMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - bindStarted).count();
        }

        // Transformacion de camara de Flixel, aplicada a CADA esquina para
        // soportar matrices arbitrarias (Animate rota y escala sus simbolos).
        auto toScreen = [&](float wx, float wy, float& outX, float& outY,
                            float& logicalX, float& logicalY) {
            const float rawX = wx - (cam.x - SX) * c.scrollX;
            const float rawY = wy - (cam.y - SY) * c.scrollY;
            float gx = (rawX - SX) * cam.zoom + SX;
            float gy = (rawY - SY) * cam.zoom + SY;
            if (c.zoomFactor != 1.0f && cam.zoom != 0.0f) {
                const float requested = std::max(1.0f + (cam.zoom - 1.0f) * c.zoomFactor, 0.0f);
                const float diff = requested / cam.zoom;
                gx = (gx - SX) * diff + SX;
                gy = (gy - SY) * diff + SY;
            }
            // FlxCamera rota la escena alrededor del centro de su viewport,
            // despues de zoom y scroll. Con angle=0 esta rama es identidad.
            if (cam.angle != 0.0f) {
                const float radians = cam.angle * 3.14159265358979323846f / 180.0f;
                const float cs = std::cos(radians), sn = std::sin(radians);
                const float dx = gx - SX, dy = gy - SY;
                gx = dx * cs - dy * sn + SX;
                gy = dx * sn + dy * cs + SY;
            }
            logicalX = gx;
            logicalY = gy;
            outX = logicalX * fit + offX;
            outY = logicalY * fit + offY;
        };

        // Las esquinas salen de UNA sola definicion compartida con el picking.
        // En modo quad vienen dadas; si no, se derivan del rect y la matriz.
        float wx[4], wy[4], qw[4];
        drawCmdCorners(c, wx, wy, qw);
        float qx[4], qy[4], logicalX[4], logicalY[4];
        for (int k = 0; k < 4; ++k)
            toScreen(wx[k], wy[k], qx[k], qy[k], logicalX[k], logicalY[k]);

        float u0 = c.sx / textureW,             v0 = c.sy / textureH;
        float u1 = (c.sx + c.sw) / textureW,    v1 = (c.sy + c.sh) / textureH;
        float shaderU0 = c.sx / logicalTextureW;
        float shaderV0 = c.sy / logicalTextureH;
        float shaderU1 = (c.sx + c.sw) / logicalTextureW;
        float shaderV1 = (c.sy + c.sh) / logicalTextureH;
        if (c.flipX) { const float t = u0; u0 = u1; u1 = t; }
        if (c.flipY) { const float t = v0; v0 = v1; v1 = t; }
        if (c.flipX) { const float t = shaderU0; shaderU0 = shaderU1; shaderU1 = t; }
        if (c.flipY) { const float t = shaderV0; shaderV0 = shaderV1; shaderV1 = t; }

        const float a = c.alpha;
        // (u*q, v*q, q) por vertice: el shader divide y recupera la UV. Con
        // qw=1 el producto y la division son exactos y el resultado es
        // identico bit a bit al de la ruta afin anterior.
        const float uq[4] = {u0 * qw[0], u1 * qw[1], u1 * qw[2], u0 * qw[3]};
        const float vq[4] = {v0 * qw[0], v0 * qw[1], v1 * qw[2], v1 * qw[3]};
        const float shaderUq[4] = {
            shaderU0 * qw[0], shaderU1 * qw[1],
            shaderU1 * qw[2], shaderU0 * qw[3]};
        const float shaderVq[4] = {
            shaderV0 * qw[0], shaderV0 * qw[1],
            shaderV1 * qw[2], shaderV1 * qw[3]};
        float quad[6][9] = {
            {qx[0], qy[0], uq[0], vq[0], qw[0], c.colorR, c.colorG, c.colorB, a},
            {qx[1], qy[1], uq[1], vq[1], qw[1], c.colorR, c.colorG, c.colorB, a},
            {qx[2], qy[2], uq[2], vq[2], qw[2], c.colorR, c.colorG, c.colorB, a},
            {qx[0], qy[0], uq[0], vq[0], qw[0], c.colorR, c.colorG, c.colorB, a},
            {qx[2], qy[2], uq[2], vq[2], qw[2], c.colorR, c.colorG, c.colorB, a},
            {qx[3], qy[3], uq[3], vq[3], qw[3], c.colorR, c.colorG, c.colorB, a},
        };

        // Los sprites con CustomShader no pasan por el batch normal: cada uno
        // necesita su propio programa y, opcionalmente, samplers extras. La
        // textura del sprite sigue siendo `bitmap` en la unidad 0; una captura
        // de camGame llega como otra unidad de textura, nunca como BitmapData.
        bool spriteShaderReady = c.spriteShader && c.spriteShader->program != 0;
        if (spriteShaderReady) {
            for (const SpriteShaderUniform& uniform : c.spriteShader->uniforms) {
                if (!uniform.isSampler) continue;
                const bool wrongIdentity = uniform.samplerObjectId != "camera:game";
                const bool noCapture = !m_cameraSamplerReady || !m_cameraSamplerTex;
                if (!wrongIdentity && !noCapture) continue;
                // Solo una vez por sampler y frame: un sprite por nota llenaria
                // el registro con la misma linea.
                const bool known = std::any_of(
                    m_spriteShaderSkips.begin(), m_spriteShaderSkips.end(),
                    [&](const SpriteShaderSkip& seen) {
                        return seen.sampler == uniform.name &&
                               seen.wanted == uniform.samplerObjectId;
                    });
                if (!known)
                    m_spriteShaderSkips.push_back({uniform.name,
                                                   uniform.samplerObjectId,
                                                   !wrongIdentity && noCapture});
                traceSpriteSampler(c, uniform, false);
                spriteShaderReady = false;
                break;
            }
        }
        if (spriteShaderReady) {
            flush();
            const auto customSetupStarted = m_traceDrawTiming
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            const bool customVertex = c.spriteShader->customVertex;
            static constexpr int corners[6] = {0, 1, 2, 0, 2, 3};
            // El fragment shader ve la geometria UV del bitmap logico. El
            // helper de muestreo aplica despues la escala hacia el backing
            // store compartido; asi gradientes y kernels conservan 0..1.
            for (int vertex = 0; vertex < 6; ++vertex) {
                quad[vertex][2] = shaderUq[corners[vertex]];
                quad[vertex][3] = shaderVq[corners[vertex]];
            }
            if (customVertex) {
                for (int vertex = 0; vertex < 6; ++vertex) {
                    quad[vertex][0] = logicalX[corners[vertex]];
                    quad[vertex][1] = logicalY[corners[vertex]];
                }
            }
            g_gl.UseProgram(c.spriteShader->program);
            const BuiltinUniformLocations& uniform =
                builtinUniforms(c.spriteShader->program);
            // A raw OpenFL vertex shader receives logical 1280x720 positions.
            // Its openfl_Matrix performs the final editor fit/pan exactly once;
            // fragment-only shaders keep the existing screen-space path.
            const float logicalProjection[16] = {
                proj[0] * fit, 0.0f,          0.0f, 0.0f,
                0.0f,          proj[5] * fit, 0.0f, 0.0f,
                0.0f,          0.0f,          proj[10], 0.0f,
                proj[12] + proj[0] * offX,
                proj[13] + proj[5] * offY, 0.0f, 1.0f,
            };
            const float* shaderProjection = customVertex ? logicalProjection : proj;
            g_gl.UniformMatrix4fv(uniform.uProjection, 1, GL_FALSE,
                                  shaderProjection);
            g_gl.UniformMatrix4fv(uniform.openflMatrix, 1, GL_FALSE,
                                  shaderProjection);
            g_gl.Uniform1i(uniform.bitmap, 0);
            const float textureSize[2] = {logicalTextureW, logicalTextureH};
            g_gl.Uniform2fv(uniform.textureSize, 1, textureSize);
            const float textureUvScale[2] = {
                logicalTextureW / textureW,
                logicalTextureH / textureH};
            g_gl.Uniform2fv(uniform.textureUvScale, 1, textureUvScale);
            const float camSize[4] = {
                0.0f, 0.0f, logicalTextureW, logicalTextureH};
            g_gl.Uniform4fv(uniform.camSize, 1, camSize);
            const bool tinted = std::abs(c.colorR - 1.0f) > 0.0001f ||
                                std::abs(c.colorG - 1.0f) > 0.0001f ||
                                std::abs(c.colorB - 1.0f) > 0.0001f;
            g_gl.Uniform1i(uniform.hasTransform, 1);
            g_gl.Uniform1i(uniform.hasColorTransform, tinted ? 1 : 0);
            g_gl.Uniform1i(uniform.openflHasColorTransform, 0);

            g_gl.ActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textureId);
            int textureUnit = 1;
            for (const SpriteShaderUniform& uniform : c.spriteShader->uniforms) {
                if (uniform.isSampler) {
                    g_gl.ActiveTexture(GL_TEXTURE0 + textureUnit);
                    glBindTexture(GL_TEXTURE_2D, m_cameraSamplerTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    g_gl.Uniform1i(uniform.location, textureUnit++);
                } else {
                    setSpriteUniform(uniform);
                }
            }
            if (m_traceDrawTiming)
                m_drawTiming.customSetupMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - customSetupStarted).count();
            const auto customUploadStarted = m_traceDrawTiming
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            g_gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
            g_gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof quad),
                            quad, GL_DYNAMIC_DRAW);
            if (m_traceDrawTiming)
                m_drawTiming.customUploadMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - customUploadStarted).count();
            const auto customIssueStarted = m_traceDrawTiming
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            glDrawArrays(GL_TRIANGLES, 0, 6);
            for (const SpriteShaderUniform& uniform : c.spriteShader->uniforms)
                traceSpriteSampler(c, uniform, true);
            if (m_traceDrawTiming) {
                m_drawTiming.customIssueMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - customIssueStarted).count();
                ++m_drawTiming.customSprites;
            }
            ++m_drawCalls;

            // El siguiente comando normal vuelve al batch que ya estaba listo.
            g_gl.ActiveTexture(GL_TEXTURE0);
            g_gl.UseProgram(m_program);
            g_gl.UniformMatrix4fv(m_uProjection, 1, GL_FALSE, proj);
            g_gl.Uniform1i(m_uTexture, 0);
            m_batchTexture = 0;
            continue;
        }
        if (c.runtimeShaderOnlyGenerated) continue;
        verts->insert(verts->end(), &quad[0][0], &quad[0][0] + 6 * kFloatsPerVertex);
    }
    flush();
    g_gl.BindVertexArray(0);
    if (clipToGameFrame) glDisable(GL_SCISSOR_TEST);
    if (m_traceDrawTiming)
        m_drawTiming.drawTotalMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - drawStarted).count();
}

void GlRenderer::drawExactFrame(const ViewportTransform& view,
                                int viewportW, int viewportH) {
    if (!g_gl.ok || !m_exactTex || m_exactW <= 0 || m_exactH <= 0 ||
        viewportW <= 0 || viewportH <= 0) return;
    m_drawCalls = 0;
    auto* verts = static_cast<std::vector<float>*>(m_scratch);
    verts->clear();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    const float L = 0.0f, R = static_cast<float>(viewportW);
    const float T = 0.0f, B = static_cast<float>(viewportH);
    const float projection[16] = {
        2.0f / (R - L), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (T - B), 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        (R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f,
    };
    g_gl.UseProgram(m_program);
    g_gl.UniformMatrix4fv(m_uProjection, 1, GL_FALSE, projection);
    g_gl.Uniform1i(m_uTexture, 0);
    g_gl.ActiveTexture(GL_TEXTURE0);
    g_gl.BindVertexArray(m_vao);

    const float fit = std::min(viewportW / static_cast<float>(m_exactW),
                               viewportH / static_cast<float>(m_exactH)) * view.zoom;
    const float width = m_exactW * fit;
    const float height = m_exactH * fit;
    const float x0 = viewportW * 0.5f - width * 0.5f + view.panX;
    const float y0 = viewportH * 0.5f - height * 0.5f + view.panY;
    const float x1 = x0 + width, y1 = y0 + height;
    const float quad[6][9] = {
        {x0, y0, 0, 0, 1, 1, 1, 1, 1}, {x1, y0, 1, 0, 1, 1, 1, 1, 1},
        {x1, y1, 1, 1, 1, 1, 1, 1, 1}, {x0, y0, 0, 0, 1, 1, 1, 1, 1},
        {x1, y1, 1, 1, 1, 1, 1, 1, 1}, {x0, y1, 0, 1, 1, 1, 1, 1, 1},
    };
    verts->insert(verts->end(), &quad[0][0], &quad[0][0] + 6 * kFloatsPerVertex);
    m_batchTexture = m_exactTex;
    m_batchAntialiasing = true;
    glBindTexture(GL_TEXTURE_2D, m_exactTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    flush();
    g_gl.BindVertexArray(0);
}

namespace {

// El blit de la cadena NO puede usar el shader del batcher: ese descarta alfa
// baja (`if (t.a < 0.003) discard;`), y un shader de camara que devuelve alfa
// pequena dejaria agujeros con el contenido anterior del destino.
const char* kBlitFragment = R"(#version 330 core
in vec3 vUV;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 FragColor;
void main() { FragColor = texture(uTexture, vUV.xy / vUV.z) * vColor; }
)";

// El postproceso del visualizador se ARMA con las piezas que la escena usa de
// verdad (`FxShaders.hpp`). Antes era un `main()` escrito a mano con los seis
// efectos entrelazados, y anadir uno era editarlo en el punto correcto sin
// equivocarse de orden.

// Quad de pantalla completa en coordenadas de recorte: pos2 + uv3 + color4, el
// mismo formato de vertice que el batcher. La tercera componente de UV vale 1:
// un blit no tiene perspectiva.
//
// Hay dos convenciones en juego y no coinciden: OpenGL guarda la fila 0 de una
// textura ABAJO, y OpenFL entrega `openfl_TextureCoordv.y = 0` ARRIBA. Los
// shaders del corpus estan escritos contra la de OpenFL —RainEffect,
// SpeedEffect y BarsEffect tienen direccion—, asi que la cadena trabaja con
// buffers en orden de imagen (fila 0 = arriba) y se voltea al entrar y al
// salir. Sin eso compilan igual y la escena sale del reves; se comprobo con
// `chromaticAberration` a offset cero, que es la identidad exacta: cualquier
// error de V se ve como un espejo perfecto y no como un artefacto sutil.
//
// `topIsV0` dice que V le toca al vertice de ARRIBA (ndc.y = +1):
//   true  -> V = 0 arriba: convierte entre pantalla (OpenGL) e imagen (OpenFL).
//   false -> V = 1 arriba: identidad entre dos buffers en orden de imagen.
void fillScreenQuad(float* v, bool topIsV0) {
    const float t = topIsV0 ? 0.0f : 1.0f;
    const float b = topIsV0 ? 1.0f : 0.0f;
    const float q[6][9] = {
        {-1.0f, -1.0f, 0.0f, b, 1, 1, 1, 1, 1},
        { 1.0f, -1.0f, 1.0f, b, 1, 1, 1, 1, 1},
        { 1.0f,  1.0f, 1.0f, t, 1, 1, 1, 1, 1},
        {-1.0f, -1.0f, 0.0f, b, 1, 1, 1, 1, 1},
        { 1.0f,  1.0f, 1.0f, t, 1, 1, 1, 1, 1},
        {-1.0f,  1.0f, 0.0f, t, 1, 1, 1, 1, 1},
    };
    memcpy(v, &q[0][0], sizeof q);
}

const float kIdentityMatrix[16] = {
    1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1,
};

}  // namespace

bool GlRenderer::beginCameraSamplerCapture(int width, int height,
                                           float r, float g, float b) {
    m_cameraSamplerReady = false;
    m_cameraSamplerCapturing = false;
    if (!g_gl.ok || width <= 0 || height <= 0) return false;
    const auto allocateStarted = m_traceDrawTiming
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (!m_cameraSamplerFbo) g_gl.GenFramebuffers(1, &m_cameraSamplerFbo);
    if (!m_cameraSamplerTex) {
        glGenTextures(1, &m_cameraSamplerTex);
        glBindTexture(GL_TEXTURE_2D, m_cameraSamplerTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_cameraSamplerW = m_cameraSamplerH = 0;
    }
    if (m_cameraSamplerW != width || m_cameraSamplerH != height) {
        glBindTexture(GL_TEXTURE_2D, m_cameraSamplerTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_cameraSamplerW = width;
        m_cameraSamplerH = height;
    }
    if (m_traceDrawTiming)
        m_drawTiming.samplerAllocateMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - allocateStarted).count();
    const auto attachStarted = m_traceDrawTiming
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, m_cameraSamplerFbo);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_cameraSamplerTex, 0);
    if (g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    if (m_traceDrawTiming)
        m_drawTiming.samplerAttachMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - attachStarted).count();
    const auto clearStarted = m_traceDrawTiming
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (m_traceDrawTiming)
        m_drawTiming.samplerClearMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - clearStarted).count();
    m_cameraSamplerCapturing = true;
    return true;
}

void GlRenderer::endCameraSamplerCapture(int restoreViewportW,
                                         int restoreViewportH) {
    if (!m_cameraSamplerCapturing) return;
    // Una sola muestra pequena en modo de diagnostico basta para separar
    // "la captura no existio" de "existio pero ya venia plana". Hacerlo cada
    // frame introduciria precisamente el tiron que se intenta medir.
    if (m_traceSpriteShaderBindings && m_cameraSamplerTraceLuma < 0.0f &&
        m_cameraSamplerW >= 16 && m_cameraSamplerH >= 16) {
        constexpr int side = 16;
        std::vector<unsigned char> pixels(
            static_cast<size_t>(side) * side * 4u, 0);
        glReadPixels(m_cameraSamplerW / 2 - side / 2,
                     m_cameraSamplerH / 2 - side / 2,
                     side, side, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        double luma = 0.0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            luma += 0.2126 * pixels[i] + 0.7152 * pixels[i + 1] +
                    0.0722 * pixels[i + 2];
        m_cameraSamplerTraceLuma = static_cast<float>(
            luma / static_cast<double>(side * side));
    }
    m_cameraSamplerCapturing = false;
    m_cameraSamplerReady = true;
    glDisable(GL_SCISSOR_TEST);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, restoreViewportW, restoreViewportH);
}

bool GlRenderer::beginSceneCapture(int viewportW, int viewportH,
                                   float r, float g, float b, float alpha) {
    m_sceneCaptured = false;
    if (!g_gl.ok || viewportW <= 0 || viewportH <= 0) return false;
    if (!m_sceneFbo) g_gl.GenFramebuffers(1, &m_sceneFbo);
    if (!m_sceneTex) {
        glGenTextures(1, &m_sceneTex);
        glBindTexture(GL_TEXTURE_2D, m_sceneTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_sceneW = m_sceneH = 0;
    }
    if (m_sceneW != viewportW || m_sceneH != viewportH) {
        glBindTexture(GL_TEXTURE_2D, m_sceneTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewportW, viewportH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_sceneW = viewportW;
        m_sceneH = viewportH;
    }
    // `beginFrame()` ya marco este frame antes de abrir el FBO. No se incrementa
    // aqui: las dos rutas deben compartir el mismo identificador para que el
    // LRU no considere que las texturas de camGame pertenecen a otro frame.
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_sceneTex, 0);
    if (g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        // Sin FBO valido se sigue por el camino de siempre: peor, pero visible.
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, viewportW, viewportH);
    glClearColor(r, g, b, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
    m_sceneCaptured = true;
    return true;
}

void GlRenderer::presentSceneCapture(int viewportW, int viewportH,
                                     bool alphaComposite) {
    if (!m_sceneCaptured) return;
    glDisable(GL_SCISSOR_TEST);
    m_sceneCaptured = false;
    if (!g_gl.ok || !m_sceneFbo) { g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0); return; }
    // Una camara opaca sustituye el rectangulo que ocupa, igual que antes. Las
    // camaras transparentes (HUD, overlays y camOther por defecto) no pueden
    // usar glBlitFramebuffer: un blit copiaria tambien sus pixeles transparentes
    // y borraria las camaras ya compuestas. Se presentan como una textura con
    // mezcla alfa, que es la misma frontera que usa FlxCamera al llegar al
    // framebuffer principal.
    if (alphaComposite && ensurePassTargets(viewportW, viewportH)) {
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, viewportW, viewportH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        g_gl.UseProgram(m_blitProgram);
        const BuiltinUniformLocations& blitUniforms =
            builtinUniforms(m_blitProgram);
        g_gl.UniformMatrix4fv(blitUniforms.uProjection, 1, GL_FALSE,
                              kIdentityMatrix);
        g_gl.Uniform1i(blitUniforms.uTexture, 0);
        g_gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneTex);
        float quad[6 * kFloatsPerVertex];
        fillScreenQuad(quad, false);
        g_gl.BindVertexArray(m_vao);
        g_gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        g_gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof quad),
                        quad, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCalls;
        g_gl.BindVertexArray(0);
        g_gl.UseProgram(m_program);
        return;
    }
    // Sin voltear: los dos tienen el origen abajo a la izquierda.
    g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFbo);
    g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    g_gl.BlitFramebuffer(0, 0, viewportW, viewportH,
                         0, 0, viewportW, viewportH,
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportW, viewportH);
}

bool GlRenderer::ensurePassTargets(int viewportW, int viewportH) {
    if (!g_gl.ok) return false;
    if (!m_passFbo) g_gl.GenFramebuffers(1, &m_passFbo);
    for (int i = 0; i < 2; ++i) {
        if (!m_passTex[i]) {
            glGenTextures(1, &m_passTex[i]);
            glBindTexture(GL_TEXTURE_2D, m_passTex[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            // CLAMP_TO_EDGE y no REPEAT: muchos shaders del corpus muestrean
            // fuera del rango (blur, aberracion, mirror) y con REPEAT el borde
            // de arriba aparece pegado abajo.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            m_passW = m_passH = 0;
        }
    }
    if (m_passW != viewportW || m_passH != viewportH) {
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, m_passTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewportW, viewportH, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        m_passW = viewportW;
        m_passH = viewportH;
    }
    if (!m_blitProgram) {
        const GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader, nullptr);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, kBlitFragment, nullptr);
        if (!vs || !fs) return false;
        m_blitProgram = g_gl.CreateProgram();
        g_gl.AttachShader(m_blitProgram, vs);
        g_gl.AttachShader(m_blitProgram, fs);
        g_gl.LinkProgram(m_blitProgram);
        g_gl.DeleteShader(vs);
        g_gl.DeleteShader(fs);
        GLint linked = 0;
        g_gl.GetProgramiv(m_blitProgram, GL_LINK_STATUS, &linked);
        if (!linked) { g_gl.DeleteProgram(m_blitProgram); m_blitProgram = 0; return false; }
    }
    return m_passFbo != 0 && m_passTex[0] != 0 && m_passTex[1] != 0;
}

bool GlRenderer::ensureFeedbackTarget(FeedbackTexture& target,int width,int height,bool reset) {
    if (!g_gl.ok || width <= 0 || height <= 0) return false;
    bool fresh = false;
    if (!target.texture) {
        glGenTextures(1, &target.texture);
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // CLAMP_TO_EDGE: la estela se muestrea AMPLIADA, asi que sale del
        // rango por los cuatro lados. Con REPEAT el borde de arriba
        // reaparecería abajo y el tunel se llenaria de costuras.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        target.width = target.height = 0;
    }
    if (target.width != width || target.height != height) {
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        target.width = width;
        target.height = height;
        fresh = true;
    }
    // Una textura recien creada trae basura, no negro. Y al saltar el cabezal
    // hay que borrarla o la estela arrastra un manchon del sitio del que
    // vienes: en un juego no pasa porque el tiempo solo avanza; en un editor,
    // que es lo que esto es, pasa todo el rato.
    if (!fresh && !reset) return true;
    GLint previousDraw = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_passFbo);
    g_gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, target.texture, 0);
    const bool complete=g_gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,
                         static_cast<GLuint>(previousDraw));
    return complete;
}

bool GlRenderer::beginVisualizerIsolation(int viewportW,int viewportH) {
    if (!g_gl.ok || m_visualizerLayerActive || viewportW<1 || viewportH<1) return false;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&m_visualizerLayerDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&m_visualizerLayerRead);
    glGetIntegerv(GL_VIEWPORT,m_visualizerLayerViewport);
    if (!m_visualizerLayerFbo) g_gl.GenFramebuffers(1,&m_visualizerLayerFbo);
    if (!m_visualizerLayerTex) glGenTextures(1,&m_visualizerLayerTex);
    g_gl.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,m_visualizerLayerTex);
    if (viewportW!=m_visualizerLayerW || viewportH!=m_visualizerLayerH) {
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,viewportW,viewportH,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        m_visualizerLayerW=viewportW; m_visualizerLayerH=viewportH;
    }
    g_gl.BindFramebuffer(GL_FRAMEBUFFER,m_visualizerLayerFbo);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m_visualizerLayerTex,0);
    if (g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,m_visualizerLayerDraw);
        g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER,m_visualizerLayerRead);
        return false;
    }
    glViewport(0,0,viewportW,viewportH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    m_visualizerLayerActive=true;
    return true;
}

bool GlRenderer::endVisualizerIsolation(int blendMode) {
    if (!m_visualizerLayerActive) return false;
    g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,m_visualizerLayerDraw);
    g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER,m_visualizerLayerRead);
    glViewport(0,0,m_visualizerLayerW,m_visualizerLayerH);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    if (blendMode==1) glBlendFunc(GL_ONE,GL_ONE);
    else if (blendMode==2) glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_COLOR);
    else if (blendMode==3) glBlendFunc(GL_DST_COLOR,GL_ONE_MINUS_SRC_ALPHA);
    g_gl.UseProgram(m_blitProgram);
    const auto& blit=builtinUniforms(m_blitProgram);
    g_gl.UniformMatrix4fv(blit.uProjection,1,GL_FALSE,kIdentityMatrix);
    g_gl.Uniform1i(blit.uTexture,0);
    g_gl.ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,m_visualizerLayerTex);
    float quad[6*kFloatsPerVertex]; fillScreenQuad(quad,false);
    g_gl.BindVertexArray(m_vao); g_gl.BindBuffer(GL_ARRAY_BUFFER,m_vbo);
    g_gl.BufferData(GL_ARRAY_BUFFER,sizeof quad,quad,GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,6); ++m_drawCalls;
    g_gl.BindVertexArray(0);
    glViewport(m_visualizerLayerViewport[0],m_visualizerLayerViewport[1],m_visualizerLayerViewport[2],m_visualizerLayerViewport[3]);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    m_visualizerLayerActive=false;
    return true;
}

bool GlRenderer::applyVisualizerPostProcess(
        const VisualizerPostProcess& settings,
        int x, int y, int width, int height,
        int viewportW, int viewportH) {
    if (!g_gl.ok || !settings.active() || width <= 1 || height <= 1 ||
        viewportW <= 1 || viewportH <= 1) return false;

    // El rectangulo viene de ImGui y puede quedar cortado por el borde de una
    // ventana. Solo se copia la parte que de verdad existe en el framebuffer.
    int rx = std::clamp(x, 0, viewportW);
    int ry = std::clamp(y, 0, viewportH);
    int rw = std::min(width - std::max(0, -x), viewportW - rx);
    int rh = std::min(height - std::max(0, -y), viewportH - ry);
    if (rw <= 1 || rh <= 1) return false;
    const int glY = viewportH - (ry + rh);
    if (glY < 0) return false;
    if (!ensurePassTargets(rw, rh)) return false;

    // Que piezas hay que compilar. Los umbrales son los mismos que usa
    // `VisualizerPostProcess::active()`: un efecto que no llega a notarse
    // tampoco entra en el shader.
    unsigned int mask = 0u;
    if (settings.chromaticPx > 0.01f) mask |= fx::PostFxChromatic;
    if (settings.blurPx > 0.01f) mask |= fx::PostFxBlur;
    if (settings.brightness < -0.001f || settings.brightness > 0.001f ||
        settings.contrast < 0.999f || settings.contrast > 1.001f ||
        settings.saturation < 0.999f || settings.saturation > 1.001f ||
        settings.tintMix > 0.001f) mask |= fx::PostFxColorFilter;
    if (settings.grain > 0.001f) mask |= fx::PostFxGrain;
    if (settings.scanlines > 0.001f) mask |= fx::PostFxScanlines;
    if (settings.vignette > 0.001f) mask |= fx::PostFxVignette;
    if (settings.glitch > 0.001f) mask |= fx::PostFxGlitch;
    if (settings.pixelSize > 1.01f) mask |= fx::PostFxPixelate;
    if (settings.mirrorAmount > 0.001f) mask |= fx::PostFxMirror;
    if (settings.feedbackAmount > 0.001f) mask |= fx::PostFxFeedback;
    // Creative passes are deliberately not fused with the legacy fixed-order
    // shader: repeated filters keep their own settings and order.
    if (settings.creative.active())
        mask = 0x80000000u | static_cast<unsigned int>(settings.creative.type);

    GLuint program = 0;
    const auto cached = m_visualizerPostPrograms.find(mask);
    if (cached != m_visualizerPostPrograms.end()) {
        program = static_cast<GLuint>(cached->second);
    } else {
        std::string ignored;
        const std::string source = settings.creative.active()
            ? fx::buildCreativeFragment(settings.creative.type)
            : fx::buildPostFragment(mask);
        const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader, &ignored);
        const GLuint fragment = compile(GL_FRAGMENT_SHADER, source.c_str(),
                                        &ignored);
        if (!vertex || !fragment) {
            if (vertex) g_gl.DeleteShader(vertex);
            if (fragment) g_gl.DeleteShader(fragment);
            return false;
        }
        program = g_gl.CreateProgram();
        g_gl.AttachShader(program, vertex);
        g_gl.AttachShader(program, fragment);
        g_gl.LinkProgram(program);
        g_gl.DeleteShader(vertex);
        g_gl.DeleteShader(fragment);
        GLint linked = 0;
        g_gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            g_gl.DeleteProgram(program);
            return false;
        }
        // Se guarda tambien la mascara vacia: no recompilar el pase identidad
        // vale igual que no recompilar los demas.
        m_visualizerPostPrograms[mask] = program;
    }

    FeedbackHistory* history=nullptr;
    bool resetHistory=settings.feedbackReset;
    if(settings.feedbackAmount>.001f) {
        constexpr uint64_t budget=256ull*1024*1024;
        const uint64_t required=static_cast<uint64_t>(rw)*rh*4*(settings.feedbackKey ? 2:1);
        if(required>budget) return false;
        const auto memory=[&] {
            uint64_t bytes=required;
            for(const auto& entry:m_feedbackHistory) if(entry.first!=settings.feedbackKey)
                for(const auto* t:{&entry.second.previous,&entry.second.output}) bytes+=static_cast<uint64_t>(t->width)*t->height*4;
            return bytes;
        };
        while((memory()>budget || m_feedbackHistory.size()>=32) && !m_feedbackHistory.empty()) {
            auto oldest=m_feedbackHistory.end();
            for(auto it=m_feedbackHistory.begin();it!=m_feedbackHistory.end();++it)
                if(it->first!=settings.feedbackKey && (oldest==m_feedbackHistory.end() || it->second.lastUse<oldest->second.lastUse)) oldest=it;
            if(oldest==m_feedbackHistory.end()) break;
            if(oldest->second.previous.texture) glDeleteTextures(1,&oldest->second.previous.texture);
            if(oldest->second.output.texture) glDeleteTextures(1,&oldest->second.output.texture);
            m_feedbackHistory.erase(oldest);
        }
        history=&m_feedbackHistory[settings.feedbackKey];history->lastUse=++m_feedbackClock;
        if(settings.feedbackKey) {
            resetHistory|=history->atMs<0 || settings.feedbackAtMs<history->atMs ||
                settings.feedbackAtMs-history->atMs>250 || history->revision!=settings.feedbackRevision;
            if(settings.feedbackAtMs!=history->atMs) {
                history->deltaMs=resetHistory ? 1000.f/60 : std::clamp(settings.feedbackAtMs-history->atMs,1.f,250.f);
                std::swap(history->previous,history->output);
            }
            history->atMs=settings.feedbackAtMs;history->revision=settings.feedbackRevision;
        }
    }

    GLint previousDraw = 0, previousRead = 0;
    GLint previousViewport[4] = {0, 0, viewportW, viewportH};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    g_gl.ActiveTexture(GL_TEXTURE0);

    // Copia el canvas desde el framebuffer que ImGui esta pintando. En una
    // exportacion ese framebuffer es el offscreen; en la preview es la ventana.
    g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER,
                         static_cast<GLuint>(previousRead));
    glBindTexture(GL_TEXTURE_2D, m_passTex[0]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rx, glY, rw, rh);

    g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_passFbo);
    g_gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, m_passTex[1], 0);
    if (g_gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,
                             static_cast<GLuint>(previousDraw));
        g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER,
                             static_cast<GLuint>(previousRead));
        glViewport(previousViewport[0], previousViewport[1],
                   previousViewport[2], previousViewport[3]);
        return false;
    }
    glViewport(0, 0, rw, rh);

    g_gl.UseProgram(program);
    const BuiltinUniformLocations& builtin =
        builtinUniforms(program);
    g_gl.UniformMatrix4fv(builtin.uProjection, 1, GL_FALSE, kIdentityMatrix);
    g_gl.Uniform1i(builtin.uTexture, 0);
    const float resolution[2] = {static_cast<float>(rw),
                                 static_cast<float>(rh)};
    const float tint[3] = {settings.tintR, settings.tintG, settings.tintB};
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,"uPreserveAlpha"),settings.preserveAlpha ? 1.f : 0.f);
    g_gl.Uniform2fv(g_gl.GetUniformLocation(program,
                                             "uResolution"), 1, resolution);
    if (settings.creative.active()) {
        const auto& pass=settings.creative;
        g_gl.Uniform1f(g_gl.GetUniformLocation(program,"uCreativeMix"),pass.mix);
        g_gl.Uniform1f(g_gl.GetUniformLocation(program,"uCreativeTime"),pass.time);
        g_gl.Uniform1f(g_gl.GetUniformLocation(program,"uCreativeScale"),pass.pixelScale);
        g_gl.Uniform3fv(g_gl.GetUniformLocation(program,"uCreativeColor"),1,pass.color.data());
        const char* names[]={"uCP0","uCP1","uCP2","uCP3","uCP4","uCP5"};
        for (int i=0;i<6;++i)
            g_gl.Uniform1f(g_gl.GetUniformLocation(program,names[i]),pass.params[i]);
        const char* details[]={"uCD0","uCD1","uCD2","uCD3","uCD4","uCD5"};
        for(int i=0;i<6;++i) g_gl.Uniform1f(g_gl.GetUniformLocation(program,details[i]),pass.detail[i]);
        g_gl.Uniform1f(g_gl.GetUniformLocation(program,"uCreativeVariant"),pass.variant);
    }
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uBlurPx"), settings.blurPx);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uGrain"), settings.grain);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uGrainSizePx"), settings.grainSizePx);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uTime"), settings.timeSeconds);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uBrightness"), settings.brightness);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uContrast"), settings.contrast);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uSaturation"), settings.saturation);
    g_gl.Uniform3fv(g_gl.GetUniformLocation(program,
                                             "uTint"), 1, tint);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uTintMix"), settings.tintMix);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uVignette"), settings.vignette);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uVignetteSoftness"),
                   settings.vignetteSoftness);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uChromaticPx"), settings.chromaticPx);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uScanlines"), settings.scanlines);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uGlitch"), settings.glitch);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uGlitchBands"), settings.glitchBands);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uGlitchTime"), settings.glitchTime);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uPixelSize"), settings.pixelSize);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uMirrorAmount"), settings.mirrorAmount);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uMirrorAxis"), settings.mirrorAxis);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uMirrorCenter"), settings.mirrorCenter);
    const float feedbackDelta=history && settings.feedbackHalfLife>0 ? history->deltaMs/1000 : 1.f/60;
    const float trail=settings.feedbackHalfLife>0 ? std::exp2(-feedbackDelta/std::max(.02f,settings.feedbackHalfLife))*std::pow(std::clamp(settings.feedbackAmount,0.f,.999f),feedbackDelta*60) : settings.feedbackAmount;
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uFeedbackAmount"), trail);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uFeedbackZoom"), settings.feedbackHalfLife>0 ? std::pow(settings.feedbackZoom,feedbackDelta*60) : settings.feedbackZoom);
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uFeedbackRotate"), settings.feedbackRotate*(settings.feedbackHalfLife>0 ? feedbackDelta*60 : 1));
    const float drift[]={settings.feedbackDriftX*feedbackDelta/std::max(1,rw),-settings.feedbackDriftY*feedbackDelta/std::max(1,rh)};
    g_gl.Uniform2fv(g_gl.GetUniformLocation(program,"uFeedbackDrift"),1,drift);
    {
        const float pair[2] = {settings.blurX, settings.blurY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uBlurDir"), 1, pair);
    }
    {
        const float pair[2] = {settings.chromaticX, settings.chromaticY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uChromaticDir"), 1, pair);
    }
    {
        const float pair[2] = {settings.glitchX, settings.glitchY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uGlitchDir"), 1, pair);
    }
    {
        const float pair[2] = {settings.pixelX, settings.pixelY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uPixelDir"), 1, pair);
    }
    {
        const float pair[2] = {settings.vignetteX, settings.vignetteY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uVignetteDir"), 1, pair);
    }
    {
        const float pair[2] = {settings.scanlineH, settings.scanlineV};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uScanlineDir"), 1, pair);
    }
    {
        const float pair[2] = {settings.feedbackX, settings.feedbackY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uFeedbackDir"), 1, pair);
    }
    {
        const float pair[2] = {settings.pixelOffsetX, settings.pixelOffsetY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uPixelOffset"), 1, pair);
    }
    {
        const float pair[2] = {settings.scanlineOffsetX, settings.scanlineOffsetY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uScanlineOffset"), 1, pair);
    }
    {
        const float pair[2] = {settings.vignetteCenterX, settings.vignetteCenterY};
        g_gl.Uniform2fv(g_gl.GetUniformLocation(program, "uVignetteCenter"), 1, pair);
    }
    if (settings.feedbackAmount > 0.001f) {
        // La estela vive en su propia textura, en la unidad 1. La pareja de
        // ida y vuelta no sirve: se reescribe entera en cada pase y lo que hace
        // falta es un fotograma que sobreviva al siguiente.
        const bool ready=history && ensureFeedbackTarget(history->previous,rw,rh,resetHistory) &&
            (!settings.feedbackKey || ensureFeedbackTarget(history->output,rw,rh,false));
        g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,m_passFbo);
        g_gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m_passTex[1],0);
        if(!ready) {
            g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(previousDraw));
            g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(previousRead));
            glViewport(previousViewport[0],previousViewport[1],previousViewport[2],previousViewport[3]);return false;
        }
        g_gl.Uniform1i(g_gl.GetUniformLocation(program, "uFeedback"), 1);
        g_gl.ActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, history->previous.texture);
        g_gl.ActiveTexture(GL_TEXTURE0);
    }
    g_gl.Uniform1f(g_gl.GetUniformLocation(program,
                                            "uScanlineSizePx"),
                   settings.scanlineSizePx);

    glBindTexture(GL_TEXTURE_2D, m_passTex[0]);
    float quad[6 * kFloatsPerVertex];
    fillScreenQuad(quad, false);
    g_gl.BindVertexArray(m_vao);
    g_gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
    g_gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof quad),
                    quad, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ++m_drawCalls;

    // La estela se queda con el RESULTADO, no con la entrada: por eso arrastra
    // tambien su propia estela y el rastro se hunde en si mismo. Se copia aqui,
    // con `m_passFbo` todavia leyendo `m_passTex[1]`.
    if (settings.feedbackAmount > 0.001f && history) {
        g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER, m_passFbo);
        g_gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, m_passTex[1], 0);
        if (g_gl.CheckFramebufferStatus(GL_READ_FRAMEBUFFER) ==
            GL_FRAMEBUFFER_COMPLETE) {
            glBindTexture(GL_TEXTURE_2D,settings.feedbackKey ? history->output.texture : history->previous.texture);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, rw, rh);
        }
    }

    // Sustituye exactamente el mismo rectangulo en el framebuffer original.
    // No mezcla alfa: el postproceso ya conserva el alfa del fotograma.
    g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,
                         static_cast<GLuint>(previousDraw));
    g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER,
                         static_cast<GLuint>(previousRead));
    glViewport(rx, glY, rw, rh);
    g_gl.UseProgram(m_blitProgram);
    const BuiltinUniformLocations& blit = builtinUniforms(m_blitProgram);
    g_gl.UniformMatrix4fv(blit.uProjection, 1, GL_FALSE, kIdentityMatrix);
    g_gl.Uniform1i(blit.uTexture, 0);
    glBindTexture(GL_TEXTURE_2D, m_passTex[1]);
    fillScreenQuad(quad, false);
    g_gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof quad),
                    quad, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ++m_drawCalls;

    g_gl.BindVertexArray(0);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
    g_gl.UseProgram(m_program);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

// Luminancia media (0-255) de un parche del centro del framebuffer activo. Es
// una sincronizacion con la GPU y solo se llama con la vigilancia encendida.
static float centerPatchLuma(int w, int h, float* alphaOut = nullptr) {
    constexpr int kSide = 8;
    if (w < kSide || h < kSide) return -1.0f;
    unsigned char pixels[kSide * kSide * 4] = {0};
    glReadPixels(w / 2 - kSide / 2, h / 2 - kSide / 2, kSide, kSide,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    double sum = 0.0, alpha = 0.0;
    for (int i = 0; i < kSide * kSide; ++i) {
        sum += 0.2126 * pixels[i * 4] + 0.7152 * pixels[i * 4 + 1] +
               0.0722 * pixels[i * 4 + 2];
        alpha += pixels[i * 4 + 3];
    }
    if (alphaOut) *alphaOut = static_cast<float>(alpha / (kSide * kSide));
    return static_cast<float>(sum / (kSide * kSide));
}

bool GlRenderer::applyShaderChain(const std::vector<ShaderPass>& passes,
                                  const ViewportTransform& view,
                                  int viewportW, int viewportH,
                                  bool alphaComposite) {
    if (!g_gl.ok || viewportW <= 0 || viewportH <= 0) return false;

    // Un shader que no compilo ya dio su diagnostico al cargarse. Tumbar la
    // cadena entera por uno malo dejaria la cancion sin ninguno de los otros,
    // que es peor que ensenar el efecto incompleto y decirlo.
    std::vector<const ShaderPass*> live;
    for (const ShaderPass& pass : passes)
        if (pass.program) live.push_back(&pass);
    if (live.empty()) return false;

    // El marco de juego en pixeles de pantalla: misma cuenta que drawGameBounds.
    const float fit = std::min(viewportW / kGameWidth, viewportH / kGameHeight) * view.zoom;
    const float fx0 = viewportW * 0.5f - kGameWidth  * 0.5f * fit + view.panX;
    const float fy0 = viewportH * 0.5f - kGameHeight * 0.5f * fit + view.panY;

    // Recortado a lo que de verdad hay en el framebuffer: con zoom o arrastre
    // el marco se sale, y leer fuera devuelve basura.
    int rx = (int)std::floor(fx0);
    int ry = (int)std::floor(fy0);
    int rw = (int)std::ceil(kGameWidth  * fit);
    int rh = (int)std::ceil(kGameHeight * fit);
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    rw = std::min(rw, viewportW - rx);
    rh = std::min(rh, viewportH - ry);
    if (rw <= 1 || rh <= 1) return false;
    // OpenGL cuenta las filas desde abajo; el marco viene con el origen arriba.
    const int glY = viewportH - (ry + rh);

    if (!ensurePassTargets(rw, rh)) return false;

    // Una capa compuesta con alfa tambien puede "ponerse negra": si su alfa
    // colapsa, lo que se ve es el fondo del editor, indistinguible de un fondo
    // negro. Excluirla dejaba ciega la vigilancia justo donde mas falta hace
    // -camGame transparente es lo que usan varios stages de DUSTIN-, asi que se
    // mide siempre y es el UMBRAL el que cambia: luminancia para una capa
    // opaca, alfa para una transparente.
    const bool watchBlack = m_watchBlackFrames;
    if (watchBlack) {
        m_blackFrame = BlackFrameReport{};
        m_blackFrame.sceneCaptured = m_sceneCaptured;
        m_blackFrame.x = rx; m_blackFrame.y = ry;
        m_blackFrame.w = rw; m_blackFrame.h = rh;
        m_blackFrame.passes = static_cast<int>(live.size());
        m_blackFrame.evictedSoFar = m_texturesEvicted;
        m_blackFrame.frame = m_frameIndex;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    // Sin mezcla: el shader SUSTITUYE el contenido, no lo compone encima. Con
    // blend activo una vineta que suma alfa se aplicaria dos veces.
    glDisable(GL_BLEND);

    g_gl.ActiveTexture(GL_TEXTURE0);

    g_gl.BindVertexArray(m_vao);
    g_gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);

    auto drawQuad = [&](bool topIsV0) {
        float quad[6 * kFloatsPerVertex];
        fillScreenQuad(quad, topIsV0);
        g_gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof quad),
                        quad, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        ++m_drawCalls;
    };
    auto bindTarget = [&](unsigned int texture) -> bool {
        if (!texture) { g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0); return true; }
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, m_passFbo);
        g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, texture, 0);
        return g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };
    auto blit = [&](unsigned int texture, bool topIsV0) {
        g_gl.UseProgram(m_blitProgram);
        const BuiltinUniformLocations& uniforms = builtinUniforms(m_blitProgram);
        g_gl.UniformMatrix4fv(uniforms.uProjection, 1, GL_FALSE,
                              kIdentityMatrix);
        g_gl.Uniform1i(uniforms.uTexture, 0);
        glBindTexture(GL_TEXTURE_2D, texture);
        drawQuad(topIsV0);
    };

    // 1. El marco de juego ya dibujado -> textura (orden de OpenGL), y de ahi al
    //    buffer de trabajo en orden de imagen, que es lo que espera el shader.
    if (m_sceneCaptured) {
        // glBlitFramebuffer SI obedece al scissor test, al contrario que
        // glCopyTexSubImage2D. ImGui lo deja activo con su ultimo rectangulo, y
        // con el puesto el blit se recorta a nada y el buffer de trabajo se
        // queda negro: exactamente el sintoma que se venia a arreglar.
        glDisable(GL_SCISSOR_TEST);
        // La escena esta en una textura NUESTRA: no se lee la ventana, asi que
        // el efecto ya no depende de que sea legible en este instante (§23.3).
        // El blit hace el recorte y el volteo de una vez; la Y del destino va
        // invertida porque el shader espera la imagen derecha.
        g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFbo);
        g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_passFbo);
        g_gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, m_passTex[0], 0);
        if (g_gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            presentSceneCapture(viewportW, viewportH, alphaComposite);
            return false;
        }
        g_gl.BlitFramebuffer(rx, glY, rx + rw, glY + rh,
                             0, rh, rw, 0,
                             GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (watchBlack) {
            m_blackFrame.blitOk = true;
            // Se lee del propio FBO de pases, que sigue enganchado a passTex[0].
            g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER, m_passFbo);
            m_blackFrame.sceneLuma = centerPatchLuma(rw, rh, &m_blackFrame.sceneAlpha);
        }
        // La escena capturada puede no ser la primera cámara de la lista. Las
        // capas que venían antes ya están en la ventana; copiar el FBO completo
        // las borraría. Se compone únicamente el rectángulo de camGame y el
        // shader sustituirá ese mismo rectángulo unos pasos más abajo.
        if (!alphaComposite) {
            g_gl.BindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFbo);
            g_gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            g_gl.BlitFramebuffer(rx, glY, rx + rw, glY + rh,
                                 rx, glY, rx + rw, glY + rh,
                                 GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, viewportW, viewportH);
        m_sceneCaptured = false;
    } else {
        // Camino antiguo, solo si no se pudo capturar la escena en un FBO.
        glBindTexture(GL_TEXTURE_2D, m_passTex[1]);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rx, glY, rw, rh);

        if (!bindTarget(m_passTex[0])) { g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0); return false; }
        glViewport(0, 0, rw, rh);
        blit(m_passTex[1], true);
    }

    // 2. La cadena, en ping-pong sobre la imagen ya derecha.
    int src = 0;
    for (size_t i = 0; i < live.size(); ++i) {
        const int dst = 1 - src;
        if (!bindTarget(m_passTex[dst])) { g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0); return false; }
        glViewport(0, 0, rw, rh);

        const GLuint program = live[i]->program;
        // Con la vigilancia puesta se vacia la cola de errores antes del pase:
        // `glGetError` devuelve el PRIMERO acumulado, y sin vaciarla no se sabe
        // si el error es de aqui o de cualquier cosa anterior del frame.
        const bool watchThisPass = watchBlack && i == 0;
        if (watchThisPass) { while (glGetError() != GL_NO_ERROR) {} }
        auto noteError = [&](int step) {
            if (!watchThisPass) return;
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR && m_blackFrame.glErrorStep == 0) {
                m_blackFrame.glError = error;
                m_blackFrame.glErrorStep = step;
            }
        };
        g_gl.UseProgram(program);
        noteError(1);
        const BuiltinUniformLocations& uniforms = builtinUniforms(program);

        g_gl.UniformMatrix4fv(uniforms.uProjection, 1, GL_FALSE, kIdentityMatrix);
        g_gl.Uniform1i(uniforms.bitmap, 0);
        // El tamano que ve el shader es el del marco en pantalla, no 1280x720:
        // los efectos que miden en pixeles (mosaico, scanlines) cambian de
        // grano con el zoom del editor. Es una diferencia conocida y solo
        // desaparece renderizando el pase a resolucion de juego.
        const float textureSize[2] = {(float)rw, (float)rh};
        g_gl.Uniform2fv(uniforms.textureSize, 1, textureSize);
        const float textureUvScale[2] = {1.0f, 1.0f};
        g_gl.Uniform2fv(uniforms.textureUvScale, 1, textureUvScale);
        // El destino ES la camara y sin relleno, asi que el rect es el total y
        // `getCamPos` queda como la identidad. En el motor el filtro puede
        // agrandar el bitmap, y por eso el uniform existe (FunkinShader.hx:643).
        const float camSize[4] = {0.0f, 0.0f, (float)rw, (float)rh};
        g_gl.Uniform4fv(uniforms.camSize, 1, camSize);
        // En la ruta de filtros de camara estos quedan sin asignar, o sea
        // false, y `applyFlixelEffects` devuelve el color tal cual.
        g_gl.Uniform1i(uniforms.hasTransform, 0);
        g_gl.Uniform1i(uniforms.hasColorTransform, 0);
        g_gl.Uniform1i(uniforms.openflHasColorTransform, 0);
        noteError(2);

        if (live[i]->uniforms) {
            for (const ShaderUniform& u : *live[i]->uniforms) {
                if (u.location < 0 || u.isSampler) continue;
                // Type, array length and matrix shape come from linked GLSL;
                // the runtime never chooses an OpenGL upload function itself.
                uploadRuntimeUniform(u);
            }
            // Releer del programa lo que quedo. Solo con la vigilancia puesta:
            // glGetUniformfv sincroniza con la GPU.
            // TODOS los pases, no solo el primero: el que se investiga puede
            // estar en mitad de la cadena y con uno solo no se distingue.
            if (watchBlack && g_gl.GetUniformfv) {
                for (const ShaderUniform& u : *live[i]->uniforms) {
                    if (u.location < 0 || u.isSampler) continue;
                    GLfloat back[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    g_gl.GetUniformfv(program, u.location, back);
                    if (!m_blackFrame.firstPassUniforms.empty())
                        m_blackFrame.firstPassUniforms += " ";
                    m_blackFrame.firstPassUniforms +=
                        "p" + std::to_string(i) + ":" + u.name +
                        "[cpu=" + std::to_string(u.value[0]) +
                        " gpu=" + std::to_string(back[0]) + "]";
                }
            }
        }

        noteError(3);
        glBindTexture(GL_TEXTURE_2D, m_passTex[src]);
        noteError(4);
        drawQuad(false);
        noteError(5);
        src = dst;
        if (watchBlack) {
            const float luma = centerPatchLuma(rw, rh);
            if (i == 0) m_blackFrame.firstLuma = luma;
            m_blackFrame.finalLuma = luma;
        }
    }

    // 3. De vuelta a su sitio en pantalla, deshaciendo la conversion. El quad
    //    va en coordenadas de recorte, asi que el viewport lo coloca solo.
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(rx, glY, rw, rh);
    if (alphaComposite) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    blit(m_passTex[src], true);
    glViewport(0, 0, viewportW, viewportH);

    if (watchBlack) {
        // El umbral sale de las medidas de §23.2-23.3: un frame normal daba
        // luminancia 24-31 y los negros 10.4 y 11. Seis deja sitio de sobra para
        // una escena legitimamente oscura sin tragarse el fallo.
        // Umbral por modo. En una capa opaca el negro es luminancia baja; en
        // una transparente el sintoma es que no queda alfa que componer.
        m_blackFrame.alphaComposite = alphaComposite;
        m_blackFrame.fired = alphaComposite
            ? (m_blackFrame.sceneAlpha >= 0.0f && m_blackFrame.sceneAlpha < 4.0f)
            : (m_blackFrame.finalLuma >= 0.0f && m_blackFrame.finalLuma < 6.0f);
        if (m_blackFrame.fired) ++m_blackFrames;
    }

    g_gl.BindVertexArray(0);
    g_gl.UseProgram(m_program);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

unsigned int GlRenderer::capturePreviewFrame(int viewportW, int viewportH) {
    if (viewportW <= 0 || viewportH <= 0) return 0;
    if (!m_previewTex) {
        glGenTextures(1, &m_previewTex);
        glBindTexture(GL_TEXTURE_2D, m_previewTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_previewTex);
    }
    if (m_previewW != viewportW || m_previewH != viewportH) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewportW, viewportH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_previewW = viewportW;
        m_previewH = viewportH;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, viewportW, viewportH);
    return m_previewTex;
}

bool GlRenderer::uploadExactFrame(const unsigned char* bgra, int width, int height) {
    if (!bgra || width <= 0 || height <= 0) return false;
    // Do not attribute an unrelated declarative-preview GL error to this upload.
    while (glGetError() != GL_NO_ERROR) {}
    if (!m_exactTex) {
        glGenTextures(1, &m_exactTex);
        glBindTexture(GL_TEXTURE_2D, m_exactTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_exactTex);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (m_exactW != width || m_exactH != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, bgra);
        m_exactW = width;
        m_exactH = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}

void GlRenderer::clearExactFrame() {
    if (m_exactTex) glDeleteTextures(1, &m_exactTex);
    m_exactTex = 0;
    m_exactW = m_exactH = 0;
}

}  // namespace fml

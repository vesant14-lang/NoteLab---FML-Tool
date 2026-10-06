#include "FxShaders.hpp"
#include "CreativeFxShader.hpp"

namespace fml {
namespace fx {

std::string buildCreativeFragment(CreativeType type) {
    return "#version 330 core\n#define FX_TYPE " +
        std::to_string(static_cast<int>(type)) + "\n" + creativeShaderBody();
}

namespace {

// --- Muestreo ---------------------------------------------------------------
// Las piezas de muestreo se encadenan: cada una envuelve a la anterior. Asi el
// blur muestrea A TRAVES de la aberracion cuando las dos estan, y directamente
// de la textura cuando la aberracion no esta, sin que ninguna de las dos tenga
// que saber si la otra existe.

const char* kMirrorUniforms =
    "uniform float uMirrorAmount;\n"
    "uniform float uMirrorAxis;\n"
    "uniform float uMirrorCenter;\n";
// El pliegue, que es TODO el efecto: `centro - abs(v - centro)`. La mitad que
// contiene el pliegue se ve tal cual y la otra ve su reflejo, asi que la
// costura no tiene salto: las dos mitades comparten el pixel del pliegue.
//
// `uMirrorAxis`: 0 dobla en horizontal, 1 en vertical, 2 en las dos -que es el
// caleidoscopio de cuatro-. Es un float porque el uniform es un float; el
// rotulo del panel dice cual es cual.
//
// Lo que NO hace, y conviene saberlo antes de buscarlo: no repite. Un
// caleidoscopio de N sectores es este mismo pliegue aplicado N veces sobre una
// tira, y necesita decidir que pasa con lo que queda fuera de la tira. Aqui el
// pliegue es uno y el resto de la imagen se lee tal cual.
//
// Con el pliegue descentrado la coordenada puede salirse de [0,1]; la sujeta
// `fxSample0`, asi que lo que se ve fuera es el borde estirado. Es predecible
// y es lo que hace cualquier espejo que no llega a la pared.
const char* kMirrorSample = R"(
vec4 FX_THIS(vec2 uv) {
    if (uMirrorAmount <= 0.001) return FX_PREV(uv);
    float center = clamp(uMirrorCenter, 0.0, 1.0);
    vec2 folded = uv;
    if (uMirrorAxis < 0.5 || uMirrorAxis > 1.5)
        folded.x = center - abs(uv.x - center);
    if (uMirrorAxis > 0.5)
        folded.y = center - abs(uv.y - center);
    return mix(FX_PREV(uv), FX_PREV(folded), clamp(uMirrorAmount, 0.0, 1.0));
}
)";

const char* kChromaticUniforms =
    "uniform float uChromaticPx;\n"
    "uniform vec2 uChromaticDir;\n";
// Cuanto de la separacion va a cada eje. (1,0) es la horizontal de siempre.
const char* kChromaticSample = R"(
vec4 FX_THIS(vec2 uv) {
    vec2 px = 1.0 / max(uResolution, vec2(1.0));
    vec2 offset = uChromaticPx * px * uChromaticDir;
    vec4 center = FX_PREV(uv);
    if (uChromaticPx <= 0.001) return center;
    float red = FX_PREV(uv + offset).r;
    float blue = FX_PREV(uv - offset).b;
    return vec4(red, center.g, blue, center.a);
}
)";

const char* kBlurUniforms =
    "uniform float uBlurPx;\n"
    "uniform vec2 uBlurDir;\n";
// Nueve muestras del MISMO fotograma ya compuesto. No es un gaussiano: al
// subir el radio se ve la estrella de ocho puntas. Es el techo de un solo
// pase, y esta dicho aqui para que nadie lo confunda con un desenfoque real.
const char* kBlurSample = R"(
vec4 FX_THIS(vec2 uv) {
    if (uBlurPx <= 0.01) return FX_PREV(uv);
    vec2 px = 1.0 / max(uResolution, vec2(1.0));
    vec2 reach = px * max(0.0, uBlurPx) * max(uBlurDir, vec2(0.0));
    if (reach.x < 1e-6 || reach.y < 1e-6) {
        // Un eje apagado: las nueve muestras EN LINEA por el otro. Aplastar
        // la estrella dejaria muestras repetidas en el centro; en linea se
        // lee como barrido, que es lo que se pide cuando se apaga un eje.
        vec2 dir = reach.x < 1e-6 ? vec2(0.0, reach.y) : vec2(reach.x, 0.0);
        vec4 c = FX_PREV(uv) * 0.20;
        c += (FX_PREV(uv + dir * 0.25) + FX_PREV(uv - dir * 0.25)) * 0.16;
        c += (FX_PREV(uv + dir * 0.50) + FX_PREV(uv - dir * 0.50)) * 0.12;
        c += (FX_PREV(uv + dir * 0.75) + FX_PREV(uv - dir * 0.75)) * 0.08;
        c += (FX_PREV(uv + dir) + FX_PREV(uv - dir)) * 0.04;
        return c;
    }
    vec4 c = FX_PREV(uv) * 0.24;
    c += FX_PREV(uv + vec2( reach.x, 0.0)) * 0.12;
    c += FX_PREV(uv + vec2(-reach.x, 0.0)) * 0.12;
    c += FX_PREV(uv + vec2(0.0,  reach.y)) * 0.12;
    c += FX_PREV(uv + vec2(0.0, -reach.y)) * 0.12;
    c += FX_PREV(uv + reach * 0.7071) * 0.07;
    c += FX_PREV(uv - reach * 0.7071) * 0.07;
    c += FX_PREV(uv + vec2(reach.x, -reach.y) * 0.7071) * 0.07;
    c += FX_PREV(uv + vec2(-reach.x, reach.y) * 0.7071) * 0.07;
    return c;
}
)";

const char* kGlitchUniforms =
    "uniform float uGlitch;\n"
    "uniform float uGlitchBands;\n"
    "uniform float uGlitchTime;\n"
    "uniform vec2 uGlitchDir;\n";
// Desgarro por filas. La semilla sale de la FILA y del paso de tiempo, asi
// que el mismo instante da el mismo desgarro en la preview y en la
// exportacion: sin eso las dos no se pueden comparar.
//
// Solo se mueve una fila de cada tres. Moverlas todas no parece un glitch,
// parece ruido: lo que se lee como fallo es el contraste entre la banda
// desplazada y la que sigue en su sitio.
const char* kGlitchSample = R"(
vec4 FX_THIS(vec2 uv) {
    if (uGlitch <= 0.001) return FX_PREV(uv);
    float bands = max(1.0, uGlitchBands);
    float t = floor(uGlitchTime);
    float g = clamp(uGlitch, 0.0, 1.0) * 0.25;
    // Cuanto desgarro va a cada eje: filas que se corren en X -lo de
    // siempre-, columnas que se corren en Y. Cada eje tiene su propia semilla,
    // para que el desgarro vertical no sea el horizontal girado.
    vec2 shift = vec2(0.0);
    if (uGlitchDir.x > 0.001) {
        float row = floor(uv.y * bands);
        if (fxHash21(vec2(row, t)) > 0.66)
            shift.x = (fxHash21(vec2(row + 7.0, t)) - 0.5) * g * uGlitchDir.x;
    }
    if (uGlitchDir.y > 0.001) {
        float col = floor(uv.x * bands);
        if (fxHash21(vec2(col + 13.0, t)) > 0.66)
            shift.y = (fxHash21(vec2(col + 19.0, t)) - 0.5) * g * uGlitchDir.y;
    }
    if (shift.x == 0.0 && shift.y == 0.0) return FX_PREV(uv);
    vec4 moved = FX_PREV(uv + shift);
    // Y la banda desplazada se separa en color: un desgarro limpio no existe
    // en ninguna senal real.
    float red = FX_PREV(uv + shift * 1.15).r;
    float blue = FX_PREV(uv + shift * 0.85).b;
    return vec4(red, moved.g, blue, moved.a);
}
)";

const char* kPixelateUniforms =
    "uniform float uPixelSize;\n"
    "uniform vec2 uPixelDir;\n"
    "uniform vec2 uPixelOffset;\n";
// Cuantiza la UV, asi que va el ULTIMO del muestreo: todo lo de dentro se
// evalua en el centro del bloque y el resultado sale entero en bloques,
// desenfoque incluido. Ponerlo antes daria bloques con los bordes suaves,
// que es justo lo contrario de lo que se busca.
const char* kPixelateSample = R"(
vec4 FX_THIS(vec2 uv) {
    if (uPixelSize <= 1.0) return FX_PREV(uv);
    // Cuanto del bloque va a cada eje: (1,1) cuadrados, (1,0) tiras a lo
    // ancho. Un eje a cero es un pixel, o sea sin cuantizar por ahi.
    vec2 block = max(vec2(1.0), uPixelSize * uPixelDir);
    // En pixeles, con la rejilla corrida `uPixelOffset`: asi el bloque se
    // puede cuadrar con lo que cubre. Con cero es la rejilla de siempre.
    vec2 p = uv * uResolution + uPixelOffset;
    vec2 q = (floor(p / block) + 0.5) * block - uPixelOffset;
    return FX_PREV(q / uResolution);
}
)";

// --- Color ------------------------------------------------------------------
const char* kFeedbackUniforms =
    "uniform sampler2D uFeedback;\n"
    "uniform float uFeedbackAmount;\n"
    "uniform float uFeedbackZoom;\n"
    "uniform float uFeedbackRotate;\n"
    "uniform vec2 uFeedbackDir;\n"
    "uniform vec2 uFeedbackDrift;\n";
// Drift is zero for old documents and all non-visualizer callers.
// Se mezcla con `max` y no con `mix`. Con `mix` el fotograma actual se
// DESVANECE detras de su propia estela y la imagen entera pierde contraste;
// con `max` la estela solo puede ANADIR luz, asi que lo oscuro sigue oscuro y
// lo brillante deja rastro. Es lo que hace que se lea como un tunel y no como
// una imagen sucia.
const char* kFeedbackCode = R"(
    {
        vec2 centered = uv - 0.5;
        float sn = sin(uFeedbackRotate), cs = cos(uFeedbackRotate);
        vec2 turned = vec2(centered.x * cs - centered.y * sn,
                           centered.x * sn + centered.y * cs);
        // Cuanto del tunel va a cada eje: (1,1) el de siempre, (1,0) solo
        // se abre a lo ancho.
        vec2 zoom = vec2(1.0) + (uFeedbackZoom - 1.0) * uFeedbackDir;
        vec2 past = turned / max(vec2(0.05), zoom) + 0.5-uFeedbackDrift;
        vec4 trail = texture(uFeedback, clamp(past, 0.0, 1.0));
        rgb = max(rgb, trail.rgb * clamp(uFeedbackAmount, 0.0, 0.995));
        if(uPreserveAlpha>.5) color.a=max(color.a,trail.a*clamp(uFeedbackAmount,0.,.995));
    }
)";

const char* kColorUniforms =
    "uniform float uBrightness;\n"
    "uniform float uContrast;\n"
    "uniform float uSaturation;\n"
    "uniform vec3 uTint;\n"
    "uniform float uTintMix;\n";
const char* kColorCode = R"(
    {
        float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        rgb = mix(vec3(luma), rgb, max(0.0, uSaturation));
        rgb = (rgb - 0.5) * max(0.0, uContrast) + 0.5 + uBrightness;
        rgb = mix(rgb, rgb * uTint, clamp(uTintMix, 0.0, 1.0));
    }
)";

// --- Pelicula ---------------------------------------------------------------
const char* kGrainUniforms =
    "uniform float uGrain;\n"
    "uniform float uGrainSizePx;\n"
    "uniform float uTime;\n";
const char* kGrainHelper = R"(
float fxHash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
)";
// `uTime * 24.0` cuantiza el grano a 24 pasos por segundo: el mismo instante
// da el mismo grano en la preview y en la exportacion. Sin eso las dos no se
// pueden comparar.
const char* kGrainCode = R"(
    {
        float grainSize = max(1.0, uGrainSizePx);
        vec2 cell = floor(gl_FragCoord.xy / grainSize) + floor(uTime * 24.0);
        rgb += (fxHash21(cell) - 0.5) * clamp(uGrain, 0.0, 1.0);
    }
)";

const char* kScanlineUniforms =
    "uniform float uScanlines;\n"
    "uniform float uScanlineSizePx;\n"
    "uniform vec2 uScanlineDir;\n"
    "uniform vec2 uScanlineOffset;\n";
// Cuanto de las lineas va a cada sentido: horizontales -las de un tubo- y
// verticales -la rejilla de apertura-. Las dos a la vez hacen cuadricula.
const char* kScanlineCode = R"(
    {
        float lineSize = max(1.0, uScanlineSizePx);
        // Corridas `uScanlineOffset` pixeles, para cuadrarlas con lo que
        // cubren. Con cero, donde estaban.
        float lineY = 0.5 + 0.5 * sin((gl_FragCoord.y + uScanlineOffset.y) * 3.14159265 / lineSize);
        float lineX = 0.5 + 0.5 * sin((gl_FragCoord.x + uScanlineOffset.x) * 3.14159265 / lineSize);
        float line = max(lineY * uScanlineDir.x, lineX * uScanlineDir.y);
        rgb *= 1.0 - clamp(uScanlines, 0.0, 1.0) * (0.08 + 0.22 * line);
    }
)";

const char* kVignetteUniforms =
    "uniform float uVignette;\n"
    "uniform float uVignetteSoftness;\n"
    "uniform vec2 uVignetteDir;\n"
    "uniform vec2 uVignetteCenter;\n";
// Cuanto de la vineta va a cada eje: (1,1) redonda, (1,0) solo los lados,
// (0,1) solo arriba y abajo.
const char* kVignetteCode = R"(
    {
        // El centro, en pixeles desde el del cuadro. Con cero, el de siempre.
        vec2 centered = uv * 2.0 - 1.0 - uVignetteCenter * 2.0 / max(uResolution, vec2(1.0));
        centered.x *= uResolution.x / max(1.0, uResolution.y);
        float reach = length(centered * uVignetteDir);
        float edge = smoothstep(clamp(uVignetteSoftness, 0.05, 0.95), 1.35,
                                reach);
        rgb *= 1.0 - edge * clamp(uVignette, 0.0, 1.0);
    }
)";

void replaceAll(std::string& text, const std::string& from,
                const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
}

}  // namespace

int postFxCount(unsigned int mask) {
    int count = 0;
    // El tope sale de `PostFxAll`, no del nombre de la ultima pieza. Escribirlo
    // a mano es el mismo fallo que ya costo un efecto mudo en el catalogo del
    // visualizador: se anade una pieza, nadie toca este bucle, y la cuenta
    // miente sin que falle nada.
    for (unsigned int bit = 1u; bit <= PostFxAll; bit <<= 1)
        if (mask & bit) ++count;
    return count;
}

std::string buildPostFragment(unsigned int mask) {
    std::string uniforms;
    std::string helpers;
    std::string samplers;
    std::string body;

    // El eslabon de partida: la textura tal cual. Los muestreadores se van
    // encadenando encima y `last` siempre nombra al ultimo.
    samplers +=
        "vec4 fxSample0(vec2 uv) {\n"
        "    return texture(uTexture, clamp(uv, 0.0, 1.0));\n"
        "}\n";
    std::string last = "fxSample0";
    int link = 0;

    auto chain = [&](const char* code) {
        const std::string self = "fxSample" + std::to_string(++link);
        std::string piece = code;
        replaceAll(piece, "FX_THIS", self);
        replaceAll(piece, "FX_PREV", last);
        samplers += piece;
        last = self;
    };

    // `fxHash21` lo usan el grano y el glitch: se emite si lo pide cualquiera
    // de los dos, y ANTES de los muestreadores porque el glitch lo llama.
    if (mask & (PostFxGrain | PostFxGlitch)) helpers += kGrainHelper;

    // El orden del muestreo, del prisma a la pantalla. Ver `FxShaders.hpp`.
    // El espejo va el PRIMERO: dobla la escena antes de que la vea la lente.
    if (mask & PostFxMirror)    { uniforms += kMirrorUniforms;    chain(kMirrorSample); }
    if (mask & PostFxChromatic) { uniforms += kChromaticUniforms; chain(kChromaticSample); }
    if (mask & PostFxBlur)      { uniforms += kBlurUniforms;      chain(kBlurSample); }
    if (mask & PostFxGlitch)    { uniforms += kGlitchUniforms;    chain(kGlitchSample); }
    if (mask & PostFxPixelate)  { uniforms += kPixelateUniforms;  chain(kPixelateSample); }

    // El feedback abre la etapa de color: la estela es parte de la imagen, y
    // el grado, el grano y la vineta tienen que caer SOBRE ella.
    if (mask & PostFxFeedback) { uniforms += kFeedbackUniforms; body += kFeedbackCode; }
    if (mask & PostFxColorFilter) { uniforms += kColorUniforms; body += kColorCode; }
    // El orden de la etapa de pelicula tambien esta fijado: grano, lineas y
    // vineta. La vineta va la ultima porque apaga bordes, y apagar despues de
    // haber anadido grano es lo que hace que el borde no hierva.
    if (mask & PostFxGrain) {
        uniforms += kGrainUniforms;
        body += kGrainCode;
    }
    if (mask & PostFxScanlines) { uniforms += kScanlineUniforms; body += kScanlineCode; }
    if (mask & PostFxVignette)  { uniforms += kVignetteUniforms; body += kVignetteCode; }

    std::string out =
        "#version 330 core\n"
        "in vec3 vUV;\n"
        "in vec4 vColor;\n"
        "uniform sampler2D uTexture;\n"
        "uniform vec2 uResolution;\n"
        "uniform float uPreserveAlpha;\n";
    out += uniforms;
    out += "out vec4 FragColor;\n";
    out += helpers;
    out += samplers;
    out +=
        "\nvoid main() {\n"
        "    vec2 uv = vUV.xy / vUV.z;\n"
        "    vec4 color = " + last + "(uv);\n"
        "    vec3 rgb = color.rgb;\n";
    if(!(mask & PostFxFeedback)) out+="    if(uPreserveAlpha>.5) rgb=color.a>.00001 ? rgb/color.a : vec3(0);\n";
    out += body;
    if(!(mask & PostFxFeedback)) out+="    if(uPreserveAlpha>.5) rgb=clamp(rgb,0.,1.)*color.a;\n";
    out += "    FragColor = vec4(clamp(rgb, 0.0, 1.0), color.a) * vColor;\n"
           "}\n";
    return out;
}

}  // namespace fx
}  // namespace fml

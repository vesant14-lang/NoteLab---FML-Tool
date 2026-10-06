// fml_render — El shader de postproceso, ensamblado a partir de piezas.
//
// QUE PROBLEMA RESUELVE. El postproceso del visualizador era un `main()`
// escrito a mano con seis efectos entrelazados: para anadir uno habia que
// editar ese `main()`, colocar el codigo en el punto correcto y acordarse de
// que el blur muestrea a traves de la aberracion. Eso convierte cada efecto
// nuevo en una edicion delicada de un archivo que ya nadie quiere tocar.
//
// Aqui cada efecto es una PIEZA con su etapa, sus uniforms, sus ayudantes y su
// codigo. El shader se arma con las piezas que la escena usa de verdad, en un
// orden fijo y declarado. Anadir un efecto de pantalla completa pasa a ser
// anadir una pieza.
//
// EL ORDEN NO ES UN DETALLE, es la mitad del resultado:
//
//   Muestreo -> Color -> Pelicula
//
// El grano antes del blur se difumina y desaparece. La vineta antes del color
// se destine. Por eso la etapa es un dato de la pieza y no una decision de
// quien escribe el `main()`.
//
// LO QUE ESTO NO HACE. Sigue siendo UN pase sobre el fotograma ya compuesto.
// Los efectos que necesitan el fotograma ANTERIOR o varias pasadas -estela,
// tunel, bloom real- necesitan ademas targets de ida y vuelta, y eso es otra
// pieza de fontaneria distinta de esta.
#pragma once

#include <string>
#include "../core/CreativeFx.hpp"

namespace fml {
namespace fx {

// Cada efecto de pantalla completa es un bit. El conjunto de bits activos es
// lo que identifica un programa compilado: dos escenas con los mismos efectos
// comparten shader y no se recompila nada.
enum PostFxBit : unsigned int {
    PostFxChromatic   = 1u << 0,
    PostFxBlur        = 1u << 1,
    PostFxColorFilter = 1u << 2,
    PostFxGrain       = 1u << 3,
    PostFxScanlines   = 1u << 4,
    PostFxVignette    = 1u << 5,
    PostFxGlitch      = 1u << 6,
    PostFxPixelate    = 1u << 7,
    PostFxMirror      = 1u << 8,
    PostFxFeedback    = 1u << 9,
    PostFxAll         = (1u << 10) - 1u,
};

// El fuente del fragment shader para ese conjunto. Con `mask == 0` devuelve el
// pase identidad, que es lo correcto: sin efectos, la copia no debe teñir.
//
// Es determinista: la misma mascara da exactamente el mismo texto. De eso
// depende que el cache por mascara sirva de algo y que una comprobacion de
// pixeles pueda compararse consigo misma.
std::string buildPostFragment(unsigned int mask);
// Each creative effect is a separate pass. Compiles only its own sampling code.
std::string buildCreativeFragment(CreativeType type);

// Cuantas piezas trae esa mascara. Solo para informes y pruebas.
int postFxCount(unsigned int mask);

// El orden del MUESTREO no es arbitrario y conviene tenerlo escrito. Se lee
// como el camino de la luz, de la escena a quien mira:
//
//   escena -> prisma -> lente -> optica -> senal -> pantalla
//
// El espejo es el prisma y va PEGADO a la escena: dobla el cuadro antes de que
// lo vea ningun cristal. La aberracion es de la lente; el desenfoque es optico
// y va encima; el glitch es un fallo de transmision y va sobre lo ya formado;
// el pixelado es la pantalla y por eso va el ultimo, cuantizando todo lo
// anterior. Cambiar ese orden cambia el resultado, no solo el rendimiento.
//
// EL FEEDBACK NO ES UNA PIEZA DE MUESTREO: es la unica que mira un fotograma
// que YA NO ESTA. Necesita una textura que sobreviva de un cuadro al siguiente
// -no vale la pareja de ida y vuelta, que se reescribe entera en cada pase-, y
// por eso el renderer guarda una aparte y la vuelve a llenar con el resultado.
//
// Va la PRIMERA de la etapa de color, antes del grado y de la pelicula: la
// estela es parte de la imagen compuesta, y el grano o la vineta tienen que
// caer sobre ella y no por debajo.
//
// CONSECUENCIA VISIBLE de que el espejo sea el primero: las franjas de color y
// el desenfoque se pliegan CON la imagen, asi que en la mitad reflejada la
// aberracion apunta al otro lado. Es lo que hace un espejo de verdad, y es la
// razon de que el desenfoque cruce la costura sin cortarse. Ponerlo el ultimo
// daria un cuadro doblado con la optica pintada encima, plana e igual en las
// dos mitades: mas limpio y menos creible.

}  // namespace fx
}  // namespace fml

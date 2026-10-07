# Note Lab 1.0.4a — inicio rápido

Extrae el ZIP completo y ejecuta `NoteLab.exe`, con `SDL3.dll` al lado.
Necesitas Windows x64 y un controlador compatible con OpenGL 3.3. EN/ES cambia
el idioma. No necesitas instalar FML, Haxe ni herramientas de compilación.

## 1. Aprende con tus recursos

Abre **Juego base**, una carpeta de mod o un ZIP. Puedes mantener cuatro
fuentes abiertas. **Primeros pasos** guía el trabajo real; cada misión se marca
cuando la completas. El menú **Tutorial** permite empezar, continuar, repetir
o saltar las siete guías. **Muéstrame** señala el control de la misión; clic
o Esc retiran la sombra. Los tutoriales no sobrescriben tus bloques. Lo que ya
tenías hecho se salta solo; en **Todas las misiones**, o en las casillas de
progreso, un clic en una misión pasada te devuelve a ella y te lleva a donde se
hace, sin tocar tu trabajo (menos **Abre un mod** si ya hay uno abierto).

![Tutorial del editor con recursos del juego base](images/sprite-tutorial-es.png)

## 2. Mira y juega antes de cambiar

Elige un estilo a la izquierda y una canción en Preview. Puedes mostrar ambos
lados, cambiar downscroll, velocidad y posición, o activar **Jugar**.
Las canciones y la audición de archivos tienen audio; los sonidos de interacción
del editor de bloques no, y el tutorial tampoco suena.

Sin canción se reproduce un **patrón de prueba**: arriba de la lista de canciones hay
siete (Básico, Escalera, Repeticiones, Acordes, Sostenidos largos, Ráfaga y
Aleatorio) y **Editar patrón…** abre una cuadrícula para hacer los tuyos: un clic
pone o quita una nota y arrastrar a la derecha la hace sostenida. Se guardan con
su nombre en tus preferencias.

**Receptores:** un estilo con receptores propios los muestra al elegirlo. El
selector enseña temporalmente los de otro estilo; para incluirlos en un export,
usa **Combinar estilos / HUD**: el selector de preview no los copia.

## 3. Crea un HUD o una nota

**Crear HUD** va por pasos — Base, Colores, Detalles y Piezas — y en **Piezas →
Dibujadas por mí** muestra una tarjeta por pieza: un clic abre el editor en esa
pieza. Dibuja notas, receptores, sostenidos y splashes. Lo que dejas vacío
conserva su base. No confundirlo con **Crear nota custom**: ese asistente crea
un tipo con nombre, comportamiento, aspecto opcional, sonido de acierto y bot.

Antes de dibujar, el editor pregunta **¿Cómo quieres empezar?**: la plantilla
con un hueco por pieza, un **lienzo libre** del tamaño que quieras (por ejemplo
32×32 en pixel art) o el lienzo de una pieza a su tamaño del juego. El lienzo
libre se pasa luego con **Pasar a la hoja general**, ajustado sin deformarse.

![Editor por capas y filas de piezas](images/sprite-sheet-es.png)

El editor incluye pincel, goma, figuras, relleno, cuentagotas, selección,
capas, fotogramas, espejo y pixel art. Rueda: zoom; botón central o Espacio:
desplazar; clic derecho pinta con el segundo color; Alt + clic toma un color;
`[` `]` cambian el pincel; `0` / `1` / `F` encuadran. Selecciona
la fila y el frame, dibuja en una capa y revisa el resultado en la línea de
tiempo. **Guardar dibujo** conserva todo en `.nlsprite`; **PNG final** ensambla
las piezas y **Guardar PNG + XML** guarda una hoja plana.

**Figura** cambia la barra superior: el selector muestra 12 figuras con
miniaturas. Elige una y arrastra en el lienzo, o pulsa **Colocar figura** para
insertarla en la selección o el hueco elegido, en una capa nueva. Ajusta su
grosor de contorno y **Pixel art**; la flecha conserva su contorno habitual.

El creador avanzado importa PNG/XML/TXT, imágenes, secuencias, GIF y audio.
Inspecciona antes de asignar; define cuadrícula o recortes si faltan metadatos.
Asigna función, dirección, orden, FPS y offsets. El ranking se hace con tus
imágenes desde **Archivo → Crear HUD de ranking · Imágenes**.

## 4. Combina recursos de varias hojas

En Inspector, **Combinar estilos / HUD** copia solo los grupos marcados desde
otra fuente: notas, receptores, splashes, covers, ranking, cuenta atrás o
sonidos. Repite con distintas fuentes. Las otras piezas no se borran y puedes
deshacer. Las hojas parciales conservan receptores base en preview, pero ese
respaldo visual no constituye una composición exportada.

## 5. Programa una nota custom

Ve a **Notas custom → Catálogo → Crear nota custom** o **+** en Bloques.
Elige bloques o solo código y pulsa **Crear nota**: queda guardada en **Tus
notas** y la ventana pasa a **Lista**, con los siguientes pasos (sus bloques, su
aspecto, su bot). El tipo puede usar las notas normales sin un sprite propio.
Conecta acciones a eventos, configura valores y revisa avisos. En Bloques, la
fila de la nota tiene **Guardar**, **Guardar como**, **Pasar a código**,
**Aspecto** y **Bot**. La preview no ejecuta cualquier script del mod.

**Guardar** la escribe en el mod abierto, donde la busca su motor, con sus
bloques dentro de su propio script: al abrir el mod otra vez, sin proyecto, la
nota vuelve con sus bloques. La primera vez enseña los archivos que escribe y
solo reemplaza algo que no escribió Note Lab si lo aceptas. Un clic en una de
**Tus notas** enseña su vista general: aspecto, comportamiento, si está en el
mod y su código para el motor del mod. Al salir con notas sin guardar en el mod,
Note Lab ofrece **Guardarlas en el mod**.

Los nombres que generarían el mismo archivo de otra nota se rechazan: usa un
nombre diferente, no solo espacios, guiones o mayúsculas. Cambiar el aspecto
también marca la nota como pendiente de guardar. Una recuperación anterior se
conserva aparte de la sesión actual; **Archivo → Recuperar sesión anterior…**
permite abrirla aunque ya tengas un mod abierto, con confirmación si hay trabajo
sin guardar.

Desde **…** puedes **Pasar su script a bloques**, guardar/abrir `.nlblocks` y
guardar su código en una carpeta. Solo se convierten sentencias reconocidas;
las demás quedan como código del motor original, y las funciones no admitidas
se enumeran. Revisa ese informe antes de exportar a otro motor.

## 6. Usa imágenes, vídeos y sonidos

La pestaña **Recursos**, o **Mods → Recursos** en el lateral de Bloques,
permite buscar, inspeccionar y asignar archivos. **Biblioteca / En uso** muestra
dependencias y conexiones. Sonidos separa **Efectos / Canciones / Música**.
No se reproduce nada al seleccionar; pulsa reproducir explícitamente.
La audición puede recorrer el archivo completo y tiene Loop, velocidad,
balance y rango A–B. Estos ajustes no modifican el archivo ni tu canción.

Importar permite escoger una carpeta relativa y crearla. Empieza en modo solo
proyecto; copiar al mod requiere confirmación y nunca sobrescribe archivos.
Los recursos conectados a bloques se incluyen en el export. PNG/OGG Vorbis/MP4
son los formatos de salida de esos bloques; faltantes, colisiones o formatos
incompatibles bloquean la publicación. Los vídeos requieren APIs compatibles.

## 7. Reparte los tipos en un chart

**Distribuir** permite seleccionar canción, tipos, lados, cantidad y filtros.
También funciona en el patrón de prueba, para probar las notas sin canción (un
patrón no se guarda como chart). Comprueba el resultado con el bot o jugando.
**Guardar distribución como chart** guarda una copia; reemplazar el original es una operación aparte con backup y
comprobación de cambios externos. ZIP y charts del juego base están protegidos.

## 8. Guarda y exporta

Ctrl+S guarda `.fmlnote`; Ctrl+E prepara un paquete para Codename, Psych o
V-Slice. Revisa avisos y guías de instalación; prueba en un mod de prueba.
Un tipo de nota no reemplaza todos los receptores: para eso exporta un skin.
La validación de estructura no certifica cualquier script o versión del motor.

| Archivo | Conserva |
| --- | --- |
| `.fmlnote` | El proyecto y referencias a fuentes/cache. |
| `.nlsprite` | El dibujo editable: capas, pestañas, frames y ajustes. |
| `.nlblocks` | El programa de bloques para reutilizar. |
| PNG + XML | La hoja ensamblada, sin capas de edición. |
| Export de motor | Los archivos y rutas preparados para el destino elegido. |

Los proyectos aún dependen de sus fuentes y de la cache en
`%LOCALAPPDATA%/FunkinNoteLab`; no son bundles portables. Conserva ambas.
Si falta una fuente, abrir el proyecto se bloquea sin descartar tu trabajo.
Redimensiona los paneles por sus separadores; en ventanas estrechas, Inspector
pasa a una pestaña del centro.

Consulta la [guía avanzada ilustrada](GUIDE_ADVANCED_ES.md) y el
[changelog](../CHANGELOG.md). El informe de pruebas (`docs/RELEASE_CHECKS.md`)
va en el ZIP de Developer.

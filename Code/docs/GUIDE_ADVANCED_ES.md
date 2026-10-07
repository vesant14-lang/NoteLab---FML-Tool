# Note Lab · FML Tool — guía avanzada

Edición para la build 1.0.4a, revisada el 7 de octubre de 2026.

Esta guía explica el flujo completo: abrir fuentes, inspeccionar recursos,
crear notas y HUD, combinar hojas, distribuir tipos en charts, definir
comportamientos con bloques, editar código y exportar paquetes para un motor.
No requiere conexión a internet. Conserva este Markdown junto a la carpeta
`images/` para ver las capturas.

Las capturas muestran **Note Lab con el juego base de V-Slice o Psych cargado**.
Los archivos del juego no se incluyen en la distribución; debes usar tu
instalación.
Las etiquetas pueden aparecer en inglés o español según el idioma elegido.

También está disponible la [guía avanzada en inglés](GUIDE_ADVANCED_EN.md).

## Índice

1. [Qué hace Note Lab y qué no](#1-qué-hace-note-lab-y-qué-no)
2. [Instalación y primer inicio](#2-instalación-y-primer-inicio)
3. [Conceptos para no mezclar archivos](#3-conceptos-para-no-mezclar-archivos)
4. [Abrir el juego base o un mod](#4-abrir-el-juego-base-o-un-mod)
5. [Entender la interfaz](#5-entender-la-interfaz)
6. [Inspeccionar un estilo](#6-inspeccionar-un-estilo)
7. [Preview y canciones](#7-preview-y-canciones)
8. [Notas sostenidas, receptores y efectos](#8-notas-sostenidas-receptores-y-efectos)
9. [Combinar varias hojas y HUD](#9-combinar-varias-hojas-y-hud)
10. [Creador básico](#10-creador-básico)
11. [Creador avanzado e importación](#11-creador-avanzado-e-importación)
12. [Cuadrículas, recortes y orden de frames](#12-cuadrículas-recortes-y-orden-de-frames)
13. [Asignar piezas, direcciones y animaciones](#13-asignar-piezas-direcciones-y-animaciones)
14. [Ranking, cuenta atrás y sonidos](#14-ranking-cuenta-atrás-y-sonidos)
15. [Biblioteca general de recursos](#15-biblioteca-general-de-recursos)
16. [Importar recursos custom y elegir destino](#16-importar-recursos-custom-y-elegir-destino)
17. [Crear tipos de nota custom](#17-crear-tipos-de-nota-custom)
18. [Trabajar con bloques](#18-trabajar-con-bloques)
19. [Imagen, sonido, vídeo y screamer](#19-imagen-sonido-vídeo-y-screamer)
20. [Recursos en uso y resolución del export](#20-recursos-en-uso-y-resolución-del-export)
21. [Variables, condiciones y temporizadores](#21-variables-condiciones-y-temporizadores)
22. [Editar código y sincronizarlo](#22-editar-código-y-sincronizarlo)
23. [Comentarios y borradores](#23-comentarios-y-borradores)
24. [Distribuir notas en una canción](#24-distribuir-notas-en-una-canción)
25. [Bot de preview](#25-bot-de-preview)
26. [Guardar el chart sin perder el original](#26-guardar-el-chart-sin-perder-el-original)
27. [Proyectos, fuentes y cache](#27-proyectos-fuentes-y-cache)
28. [Exportar a los tres motores](#28-exportar-a-los-tres-motores)
29. [Validación dentro del juego](#29-validación-dentro-del-juego)
30. [Ejercicios completos](#30-ejercicios-completos)
31. [Solución de problemas](#31-solución-de-problemas)
32. [Atajos y buenas prácticas](#32-atajos-y-buenas-prácticas)
33. [Desarrollo y límites actuales](#33-desarrollo-y-límites-actuales)
34. [Tutoriales y modo foco](#34-tutoriales-y-modo-foco)
35. [Dibujar las piezas de tu HUD](#35-dibujar-las-piezas-de-tu-hud)
36. [Capas, selección, pestañas y fotogramas](#36-capas-selección-pestañas-y-fotogramas)
37. [Dibujo editable y PNG final](#37-dibujo-editable-y-png-final)
38. [Scripts a bloques y programas reutilizables](#38-scripts-a-bloques-y-programas-reutilizables)
39. [Efectos, canciones y música](#39-efectos-canciones-y-música)
40. [Comprobaciones específicas por formato](#40-comprobaciones-específicas-por-formato)
41. [Empezar a dibujar y lienzo libre](#41-empezar-a-dibujar-y-lienzo-libre)
42. [Tus notas en el mod, patrones y volver a misiones](#42-tus-notas-en-el-mod-patrones-y-volver-a-misiones)

Si vienes de la 1.0.4, empieza por los capítulos 36 y 42. Si vienes de la
1.0.3, empieza por el capítulo 42; también cambian los
capítulos 7, 17, 19, 24 y 34. Si vienes de la 1.0.2, lee además el capítulo 41
(y cambian el 18 y el 35). Si vienes de la 1.0.1, lee los capítulos 34–42.
Para crear sin una plantilla impuesta, combina la importación avanzada (11–13)
con el editor por capas (35–37) o un lienzo libre (41). Para crear una nota
completa, empieza por el capítulo 17.

La 1.0.4a añade las doce figuras y sus controles (capítulo 36), además de
corregir el guardado y la recuperación de sesiones. El capítulo 42
explica la protección de nombres de nota, los cambios visuales pendientes y
cómo recuperar una sesión sin sobrescribir la actual. El selector de figuras
tiene una captura nueva; se conservan las anteriores donde la pantalla no cambió.

## 1. Qué hace Note Lab y qué no

Note Lab trabaja sobre los sistemas de notas de Codename, Psych y V-Slice.
Un mismo proyecto puede conservar un aspecto, recursos propios y programas
de bloques. El export se construye para el motor elegido; no consiste en
copiar un JSON idéntico a los tres.

Puedes crear o modificar notas, sostenidos, receptores, splashes, recursos de
ranking y cuenta atrás. También puedes probar un chart y asignarle tipos de
nota. El sistema de bloques genera comportamientos de tipos custom.

No es un editor completo de canciones, escenarios o personajes. No ejecuta
cualquier script del mod en la preview ni convierte automáticamente todos
los modcharts entre motores. Tampoco reconstruye un proyecto FLA a partir de
un atlas.

**Regla importante:** ver algo en la preview, guardar un proyecto, generar
un paquete y comprobarlo jugando son cuatro operaciones distintas.

## 2. Instalación y primer inicio

Extrae el ZIP público completo. Mantén `SDL3.dll` junto a `NoteLab.exe`.
Se necesita Windows de 64 bits y un controlador gráfico compatible con
OpenGL 3.3. No necesitas Python, Haxe ni Visual Studio para ejecutar la app.

![Inicio de Note Lab](images/welcome-en.png)

El idioma inicial es inglés. EN / ES cambia los textos de la interfaz.
Puedes empezar con tus propios recursos o abrir una instalación existente.

Para aprender, abre primero el juego base. Una vez entendido el formato,
trabaja sobre una copia de tu mod. No hace falta copiar todos sus archivos
dentro de la carpeta del programa.

Si Windows bloquea el ejecutable, revisa su procedencia y las políticas de tu
equipo. No desactives las protecciones del sistema como paso rutinario.

## 3. Conceptos para no mezclar archivos

| Concepto | Significado |
| --- | --- |
| Fuente | Carpeta, ZIP o instalación que proporciona los archivos. |
| Estilo | Conjunto de piezas visuales de notas, receptores y HUD. |
| Hoja / atlas | Imagen y metadata que describen regiones o animaciones. |
| Pieza | Función concreta: nota izquierda, receptor pulsado, cuerpo de sostenido, etc. |
| Tipo custom | Nombre utilizado en el chart para activar aspecto o reglas especiales. |
| Programa de bloques | Comportamiento asociado a un tipo, no a todos los recursos del mod. |
| Proyecto `.fmlnote` | Estado editable de Note Lab y referencias a sus fuentes. |
| Export | Archivos generados para instalar en un motor determinado. |

Un estilo no equivale a un tipo custom. Puedes tener un estilo completo para
las notas normales y otro aspecto utilizado solo por una nota especial.
Igualmente, una imagen importada no se activa por aparecer en la biblioteca:
debe asignarse a una pieza o referenciarse desde un bloque.

## 4. Abrir el juego base o un mod

Usa **Open mod / Abrir mod**, **ZIP** o **Base game / Juego base**.
La aplicación detecta estructuras compatibles e intenta montar los recursos
base necesarios debajo del mod.

Puedes abrir hasta cuatro fuentes. Selecciona el estilo concreto de la
fuente que quieres editar; no asumas que el último mod abierto es el activo.
La elección de motor puede ser automática o explícita.

Si un mod necesita imágenes que no trae, comprueba la instalación base
detectada. Una carpeta de código fuente sin sus assets no equivale a una
instalación jugable. Tampoco es recomendable abrir todo Descargas cuando
conoces la carpeta del mod.

Los ZIP se leen mediante la biblioteca de archivos virtuales. Para cambiar
un chart o copiar recursos al mod necesitas un destino de carpeta editable:
Note Lab no reescribe el ZIP original.

## 5. Entender la interfaz

![Interfaz con Bopeebo y el estilo base cargados](images/base-preview-en.png)

La lista izquierda contiene las fuentes y sus estilos. El centro dispone de
Preview, Custom notes, Assets y Resources. El Inspector muestra piezas,
diagnósticos, composición y exportación del estilo seleccionado.

En una ventana estrecha, el Inspector pasa a una pestaña del centro. Arrastra
los separadores para ajustar el espacio. No confundas ocultar un panel con
borrar un recurso.

El área Preview muestra una simulación de notas y HUD. Assets inspecciona
las piezas del estilo. Resources contiene la biblioteca de imágenes,
vídeos y sonidos, incluso cuando no son parte del estilo de notas.

## 6. Inspeccionar un estilo

Selecciona un estilo y revisa el motor, el origen de su definición y los
contadores de piezas. Una pieza incompleta puede existir en el catálogo sin
tener frames utilizables.

![Spritesheet real del juego base en Assets](images/base-assets-en.png)

En Assets, inspecciona la hoja, la animación y sus frames. Busca:

- La imagen correcta y su XML, TXT o metadata compatible.
- Regiones dentro de los límites del PNG.
- Animaciones que resuelvan frames y no solo un nombre vacío.
- Orden, FPS, loop, escala y offsets.
- El origen de una pieza heredada del juego base.

Haz una variante antes de experimentar si quieres conservar una comparación
clara con el estilo original. Deshacer recupera operaciones del editor;
no es un backup de las carpetas externas.

## 7. Preview y canciones

Sin una canción, la preview utiliza un patrón de prueba. Sirve para comprobar
legibilidad y piezas, pero no representa la densidad de tu chart. Hay siete de
serie y puedes hacer los tuyos en una cuadrícula (capítulo 42).

Selecciona canción y dificultad para probar sus notas. Revisa ambos lados,
jugador o rival; alterna downscroll y prueba sostenidos y acordes. La
velocidad visual del chart no debe confundirse con FPS de una animación.

Las canciones conservan su audio general: reproducción, pausa, posición y
volumen. La decodificación puede requerir un momento al cargar la pista.
Los sonidos de interacción del **editor de bloques** están silenciados en
esta build, sin interruptor para activarlos. Eso no elimina los sonidos
exportados por tus bloques.

El modo de jugar permite comprobar la respuesta del patrón/chart y su
marcador. El comportamiento arbitrario de scripts no se ejecuta en este
entorno. Por eso una nota con un bloque de vídeo puede verse como una nota
normal aquí y reproducir el vídeo únicamente en el motor destino.

## 8. Notas sostenidas, receptores y efectos

Una hoja puede separar notas, receptores y splashes en archivos distintos.
Necesitas revisar cada grupo, no solo las cuatro flechas normales.

| Grupo | Qué revisar |
| --- | --- |
| Notas | Cuatro direcciones, escala, frames y offsets. |
| Sostenidos | Cuerpo y final, continuidad y recorte. |
| Receptores | Estado normal, pulsado y confirmación. |
| Splashes | Dirección, variante, FPS y alineación. |
| Hold covers | Disponibilidad real en el motor destino. |
| HUD | Juicios, combo, cifras y cuenta atrás. |

Las hojas parciales pueden mostrar receptores base como respaldo de preview.
Eso evita una pantalla vacía, pero **no añade automáticamente esos receptores
al export**. Usa composición si quieres incorporarlos al estilo editado.

Al elegir un estilo que tiene receptores propios, la preview pasa a mostrar
los suyos aunque antes estuvieras viendo los de otro estilo. El selector
**Receptores** sigue sirviendo para probar otros solo en la preview.

Exportar un tipo custom tampoco equivale a reemplazar todos los receptores
del juego. Elige un skin completo cuando ese sea tu objetivo.

## 9. Combinar varias hojas y HUD

![Composición de componentes del juego base](images/combine-styles-en.png)

En **Combine styles / HUD**, elige la fuente donante y marca únicamente los
grupos que necesitas. Las piezas no seleccionadas se conservan.

Ejemplo: toma notas de una hoja, receptores de otra y ranking de una tercera.
Aplica una composición por vez y revisa el resultado después de cada paso.
No hace falta reunir artificialmente todas las imágenes en un único PNG.

Este flujo ayuda con mods que separan notas y receptores en distintos
archivos. Si importas solo una hoja de notas, comprueba que los receptores
procedan de la hoja correcta antes de exportar.

Los recursos de otras fuentes o ZIPs utilizados en la composición se
conservan mediante la cache del proyecto. Mantén esa cache junto a las
fuentes originales mientras necesites editar.

## 10. Creador básico

El creador básico sirve para obtener rápidamente un aspecto usando los
controles de la app. Es adecuado para probar paletas, formas y proporciones.
No sustituye la importación de arte propio.

Elige la base, ajusta el aspecto y revisa todas las direcciones y estados.
Confirma para añadir el resultado al proyecto. Si cambias de motor al
exportar, lee las advertencias de escala o componentes no disponibles.

Desde la 1.0.2 también puedes dibujar cada pieza: consulta los capítulos 35–37 y 41.
El ranking se hace con tus propias imágenes (capítulo 14).

## 11. Creador avanzado e importación

![Creador avanzado con una imagen de ranking del juego base](images/creator-base-es.png)

El modo avanzado utiliza tus recursos: imágenes, hojas con metadata,
secuencias, regiones manuales y audio. Primero importa e inspecciona; después
asigna una función.

Flujo recomendado:

1. Abre el creador avanzado e importa archivos o una carpeta/secuencia.
2. Selecciona el recurso exacto en la lista.
3. Comprueba la imagen, sus animaciones y posibles avisos.
4. Define recortes si no hay metadata.
5. Elige función, dirección, variante y orden de frames.
6. Ajusta FPS, loop, escala, pixel art y offsets donde corresponda.
7. Añade o actualiza la asignación.
8. Revisa las funciones pendientes antes de confirmar el estilo.

PNG/XML y PNG/TXT necesitan referencias coherentes. GIF es un formato de
entrada del creador; no significa que el motor vaya a cargar un GIF como
notestyle. El export produce el formato admitido por el destino.

Para un recurso del HUD normalmente necesitas exactamente un frame. Para
una pieza animada, define la secuencia real de frames.

## 12. Cuadrículas, recortes y orden de frames

Si tienes una hoja regular, indica el tamaño de celda. Una cuadrícula 32×32
solo es correcta si los frames siguen realmente ese tamaño y separación.
No elijas una celda por el tamaño aproximado del personaje o flecha.

Para una hoja irregular, traza regiones manuales. Revisa que cada rectángulo
incluya el arte y no invada al frame vecino. El botón central desplaza la
vista; la rueda modifica el zoom donde el panel lo indica.

El orden se expresa con índices visibles de frame. Por ejemplo,
`1, 3, 2, 2` reproduce cuatro pasos y repite el segundo frame al final.
Una selección vacía puede significar todos los frames; lee la indicación
del control antes de asumir que significa ninguno.

**Borrar selección** limpia el orden seleccionado, no borra el PNG original.
Revisa y guarda la asignación después de cambiar el orden. Cambiar la
selección visual sin actualizar la asignación no necesariamente cambia el
estilo ya confirmado.

## 13. Asignar piezas, direcciones y animaciones

La función determina cómo se usa un recurso. Una nota izquierda no es un
receptor izquierdo, aunque los dos utilicen la misma flecha.

Asigna primero las cuatro notas, luego cuerpo/final de sostenido y los
estados de receptor. Añade splashes y covers después. Así identificas más
rápido si un error procede de la base o de un efecto.

FPS controla la animación, no la velocidad de desplazamiento del chart.
Loop define si repite; algunas piezas tienen reglas de tiempo fijas en
Codename/Psych. La exportación informa cuando debe adaptar tus valores.

Los offsets alinean el dibujo con su referencia. Evita compensar un atlas
mal recortado aumentando arbitrariamente todos los offsets. Pixel art
cambia el filtrado; no reconstruye píxeles perdidos por una imagen borrosa.

## 14. Ranking, cuenta atrás y sonidos

Para ranking con arte propio, asigna los juicios Sick, Good, Bad
y Shit, el rótulo de combo y las cifras del 0 al 9 según los espacios del
creador. No combines todas las cifras en un único frame si cada cifra debe
tener su propio recurso.

La cuenta atrás puede tener imágenes y sonidos distintos. El audio se
asigna como sonido de cuenta atrás o efecto con nombre, no como una pieza
visual. Escúchalo manualmente antes de asignarlo.

Un efecto con nombre puede servir a un bloque de sonido. Importarlo no
significa que se dispare automáticamente. Comprueba que el nombre y la ruta
del bloque correspondan a un archivo exportable.

Las posiciones ajustadas solo en la preview no constituyen un HUD designer
universal con layout exportable. Comprueba el alcance del export del motor.

## 15. Biblioteca general de recursos

![Pestaña Recursos con archivos reales del juego base](images/resources-base-es.png)

La pestaña **Resources / Recursos** organiza imágenes, vídeos y sonidos.
Las categorías se pueden expandir o comprimir. Usa la búsqueda para
filtrar por ruta, y **Include base game** para incluir recursos heredados.

En Bloques, cambia **Mods → Recursos** en el lateral izquierdo; también puedes
abrirlo con el botón de imagen situado junto a Presets. **Biblioteca** muestra
los archivos del mod que contiene el tipo en edición y **En uso** las referencias
de sus bloques. La fuente y el tipo aparecen arriba para no mezclar mods.

![Biblioteca lateral sin tapar los bloques](images/blocks-resources-es.png)

Las tarjetas distinguen imágenes, vídeos y sonidos, muestran nombre, carpeta
y origen. Pulsa una categoría para filtrarla y vuelve a pulsarla para ver todas.
La búsqueda funciona en ambas pestañas. **En uso** señala recursos encontrados
y conectados, faltantes/ambiguos y bloques desconectados; su clic derecho permite
seleccionar el bloque que los usa o cambiar el archivo. Encontrado/conectado
describe la referencia, no certifica su codec ni el export; el preflight sigue
comprobando esos requisitos. No se analiza código
manual en esta lista. El catálogo no crea bloques ni ejecuta archivos por
aparecer en él. Puedes volver a Mods sin perder la pestaña de recursos.

El clic derecho permite inspeccionar, mostrar en carpeta, copiar la ruta,
abrir en un visor/reproductor y asignar un recurso a una ranura compatible
del bloque seleccionado. Elige primero el bloque si quieres usar esa
asignación directa.

![Inspección de una imagen del juego base](images/resource-image-base-es.png)

La preview integrada muestra PNG, con pan y zoom. Los formatos de imagen que
no resuelva se pueden abrir en el visor del sistema. La preview de vídeo
utiliza el reproductor asociado de Windows; no es un decodificador de vídeo
integrado en Note Lab.

![Inspección de sonido sin reproducción automática](images/resource-sound-base-es.png)

El audio solo empieza al pedir **Listen / Escuchar**. Hay pausa, continuación,
posición y volumen, sin corte automático a los 30 segundos. Cerrar o cambiar
el recurso detiene esa audición. Es independiente de la canción del chart.

**Loop / Repetir** está apagado por defecto y repite el archivo completo.
También hay tres ajustes de prueba, disponibles antes de confirmar una importación:

- **Velocidad:** de 0,25× a 2×; también cambia el tono, no conserva el pitch.
- **Balance estéreo:** izquierda, centro o derecha; funciona también con sonido mono.
- **Rango A–B:** activa el rango custom, ajusta sus extremos o usa Marcar A/B
  en la posición actual. Con Repetir encendido se reproduce sólo ese tramo.

Pausar conserva la posición. Detener vuelve al inicio, o a A si el rango está
activo. Restablecer ajustes desactiva Loop/rango y devuelve velocidad/balance
a 1×/centro, conservando el volumen. Cambiar de archivo reinicia su rango A–B.
Estos ajustes no modifican los archivos ni el código exportado: las propiedades
del bloque se configuran aparte. El límite de tamaño de audición sigue en 32 MB.

**Refresh / Actualizar** vuelve a consultar el catálogo montado. Si añadiste
archivos externamente que aún no están indexados, vuelve a cargar la fuente:
actualizar la lista no equivale a reindexar todo el disco.

## 16. Importar recursos custom y elegir destino

Usa **Import custom resources** en Recursos o en su selector de Bloques.
También puedes arrastrar archivos a la pestaña o al navegador de recursos.
El diálogo conserva una selección de hasta 128 archivos compatibles.

Antes de confirmar, selecciona un archivo y pide inspección. Puedes revisar
la imagen, escuchar el sonido o abrir el vídeo, además de consultar el
origen. La categoría se deduce de la extensión; inspeccionar ayuda a detectar
un archivo incorrecto o dañado.

Hay dos formas de importar:

| Modo | Dónde guarda | Qué modifica |
| --- | --- | --- |
| Solo proyecto | Cache propia de Note Lab, con ruta virtual elegida. | No cambia el mod, el juego base ni el ZIP. |
| Copiar a este mod | Raíz de escritura de una fuente de carpeta editable. | Crea archivos nuevos tras confirmar. |

El modo inicial es solo proyecto. Copiar al mod debe activarse explícitamente
y no está disponible para ZIPs ni para una fuente abierta solo como juego base.

La carpeta de destino es **relativa a la raíz montada**, por ejemplo:

```text
images/custom/screamer/
sounds/custom/screamer/
videos/custom/
```

No pongas `C:\...`, `assets/` por costumbre o `../`. En una instalación
con paquetes, la raíz editable puede estar dentro del mod, no en la carpeta
del ejecutable. Revisa la ruta real mostrada antes de copiar.

Activa **Create destination folder if missing** si quieres crearla. No se
sobrescribe un archivo existente ni se adivina un nombre alternativo. Si hay
dos archivos con el mismo nombre, cambia el destino o prepara nombres únicos.
Las rutas con traversal, nombres reservados o carpetas que atraviesan
junctions/enlaces se rechazan.

La importación limita imágenes/sonidos a 32 MB por archivo, vídeos a 128 MB y
el conjunto a 256 MB. Conserva el resultado si una copia falla parcialmente:
el mensaje lo indica; los archivos anteriores no se borran ni sobrescriben.

Después de importar, guarda el proyecto. La duración, volumen, opacidad y
ajuste de pantalla se definen en el bloque que utiliza el recurso, no se
deducen de su nombre. El selector permite aplicar esos valores al asignarlo.
Para animaciones de notas con FPS y frames, usa el creador avanzado.

## 17. Crear tipos de nota custom

En **Custom notes → Catalog**, pulsa **Create custom note**; en Bloques, usa
**+**. El asistente reúne nombre, comportamiento, preset inicial, aspecto,
sonido de acierto y bot. Primero abre una fuente a la que asociar el tipo.
Elige **Bloques** o **Solo código** en sus tarjetas; **Solo código** comienza
con una plantilla de eventos del motor. El aspecto también se elige con
tarjetas: como las normales, pintarla, tus imágenes o dibujarla.
Crearlo no escribe dentro del mod: el tipo vive en el proyecto hasta que
pulses **Guardar en el mod** (capítulo 42) o exportes su definición, y lo uses
en un chart.

![Asistente de nota custom con el juego base cargado](images/new-note-base-es.png)

Un solo botón, **Crear nota**, la guarda en **Catálogo → Tus notas** y la
ventana pasa a **Lista**: no se abre ningún editor por sí solo. Desde ahí
eliges el siguiente paso — **Editar sus bloques** (o su código), **Darle
aspecto**, **Configurar su bot** — u **Otra nota**; **Hecho** cierra y la nota
queda guardada. Si la creas desde **+** en Bloques, vas directo a sus bloques.

![La nota creada y sus siguientes pasos](images/new-note-ready-es.png)

Un clic en una nota de **Tus notas** enseña su vista general (aspecto,
comportamiento, si está en el mod y su código). Cada fila tiene además botones
para editarla, darle aspecto y configurar su bot sin pasar otra vez por el
asistente.

Define por separado:

- Nombre del tipo que leerá el chart.
- Aspecto, si necesita uno distinto al de las notas normales.
- Reglas o comportamiento mediante bloques.
- Distribución: qué notas del chart llevarán ese tipo.

Cambiar el nombre puede exigir actualizar el chart. No presupongas que
`Scare`, `scare` y el nombre de archivo generado sean intercambiables en
todos los motores. Sigue las instrucciones del paquete para su ID nativo.

Una nota custom sin aspecto propio puede conservar el aspecto normal y
usar únicamente reglas o scripts.

**Mis notas / Your notes** separa los tipos creados en Note Lab. El aspecto
puede pintarse, importarse o dibujarse. Su editor de dibujo solo muestra la
nota, el cuerpo y el final de sostenido: no cambia los receptores del HUD.
**Guardar** la escribe en el mod con sus bloques dentro de su script
(capítulo 42); no basta con copiar el código al portapapeles. Para convertir un
script existente, consulta el capítulo 38.

## 18. Trabajar con bloques

![Bloques y código coloreado con el juego base cargado](images/code-editor-en.png)

El programa tiene eventos, propiedades, controles, operadores, sensores,
acciones de juego, animaciones, cámara, sonido y variables.

Los eventos encabezan pilas. Las propiedades de creación van en su contexto
de creación. Las acciones se conectan a aciertos o fallos; los valores y
condiciones se enchufan en ranuras compatibles.

Una pila suelta puede verse en el lienzo sin participar en el código
ejecutable. Conectar un bloque no es solo acercarlo visualmente:
comprueba que encaje y que desaparezca su estado desconectado.

Arrastra desde la paleta, busca por nombre, expande categorías y utiliza
ordenar/encuadrar cuando el programa quede disperso. Deshacer y rehacer
recuperan cambios de conexiones y valores.

Los presets son puntos de partida. Inspecciónalos antes de exportar.
Una nota de daño o que debe evitarse afecta al bot y a la jugabilidad de
forma diferente a una nota que premia el acierto.

Los diagnósticos del motor distinguen datos nativos, scripts, aproximaciones
y funciones no disponibles. Que el programa genere código no significa que
Note Lab ejecute todos sus efectos en Preview.

Debajo del selector de tipo, una segunda fila trabaja con la nota abierta:

![Fila de la nota: Guardar, Guardar como y Pasar a código](images/blocks-note-row-es.png)

| Botón | Qué hace |
| --- | --- |
| **Guardar** | Guarda la nota en la carpeta del mod abierto, con sus bloques y aspecto. El primer guardado muestra los archivos; ZIP y juego base no son destinos editables. Ctrl+S guarda el proyecto por separado. |
| **Guardar como** ▾ | Otra nota con otro nombre, un programa `.nlblocks`, su código en una carpeta o el proyecto en otro archivo. |
| **Pasar a código** ▾ | Convierte esta nota en solo código (Ctrl+Z lo deshace) o crea una copia en código y deja esta en bloques. |
| **Pasar a bloques** | En una nota de solo código, convierte lo reconocido (capítulo 38). |
| **Aspecto** ▾ / **Bot** | Su aspecto y su bot, sin salir de Bloques. |

El indicador distingue **guardada en el mod**, **cambios sin guardar en el mod**,
**aún sin guardar en el mod** y **script editado a mano**. Guardar el proyecto
no equivale a guardar esa nota en el mod. El selector **Bloques / Código / Los
dos** tiene iconos.

## 19. Imagen, sonido, vídeo y screamer

Los bloques multimedia utilizan ranuras de recursos tipadas. Una ranura
de imagen no acepta un sonido, ni una de vídeo acepta un PNG.

| Bloque | Recursos | Valores principales |
| --- | --- | --- |
| Mostrar imagen | PNG | Duración, opacidad, ajuste y espera entre activaciones. |
| Tocar sonido | OGG Vorbis | Recurso y volumen. |
| Screamer de imagen + sonido | PNG y OGG Vorbis | Duración, volumen, espera y ajuste. |
| Reproducir vídeo | MP4 | Duración máxima, volumen, ajuste y espera. |

**Contain / Contener** mantiene toda la imagen con posibles márgenes.
**Cover / Cubrir** llena la pantalla y puede recortar bordes.
**Stretch / Estirar** llena el área deformando las proporciones.

Para un screamer, encaja el bloque **screamer imagen … sonido …** (categoría
Cámara) en una pila de acierto. Selecciona la imagen; después abre la ranura de
sonido y asigna el OGG. No hay un screamer completo mientras una de esas dos
ranuras esté vacía. (Desde la 1.0.4 ya no está el botón **Añadir screamer…**
del lateral de Recursos; el bloque sigue igual.)

Empieza con una duración corta, volumen moderado y una espera suficiente.
Cada bloque reutiliza su overlay y su temporizador en lugar de acumular
uno por acierto. El sonido del screamer se detiene al limpiar el efecto.
Las notas del sostenido no disparan el evento como nuevos taps.

Las duraciones multimedia se acotan entre 0,05 y 30 segundos. Volumen y
opacidad van de 0 a 1. La espera evita disparos repetidos demasiado próximos.

Mostrar una imagen **no anima automáticamente un spritesheet**: muestra la
imagen completa. Para crear piezas animadas, usa el creador de notas;
para una secuencia cinematográfica, prepara un vídeo compatible.

El vídeo necesita una build con soporte de vídeo y las APIs de script
correspondientes. Psych puede requerir Haxe habilitado y su biblioteca de
vídeo; Codename necesita la implementación correspondiente; V-Slice requiere
su clase de vídeo scriptable. Una extensión MP4 no garantiza un codec válido.
Note Lab informa estas restricciones, no instala bibliotecas en el motor.

Usa un aviso de contenido cuando publiques efectos bruscos, flashes o
sonidos fuertes. Probar un archivo no debe sorprender a quien abre el editor:
la audición siempre es manual.

## 20. Recursos en uso y resolución del export

La pestaña lateral **In use / En uso** agrupa las referencias de los bloques del tipo
actual. Muestra qué bloques las usan, si están conectados y si el recurso
falta o tiene una referencia ambigua.

![Referencias conectadas junto al editor](images/blocks-in-use-base-en.png)

No analiza dependencias ocultas en código escrito a mano. Un script propio
puede necesitar archivos adicionales que no aparezcan en esa lista.

Al exportar:

1. Se toma la referencia guardada por la ranura del bloque.
2. Se lee el archivo a través de la fuente montada, también desde ZIP.
3. Se obtiene una ruta relativa de motor bajo images, sounds o videos.
4. Se incluye el archivo y se genera el código que utiliza su clave nativa.
5. Se comprueban formatos, límites y colisiones antes de publicar.

Ejemplo:

```text
Origen virtual: shared/images/custom/cara.png
Archivo del export: images/custom/cara.png
Clave del código: custom/cara
```

El paquete no necesita una ruta absoluta de tu PC. Los recursos importados
solo al proyecto también pueden viajar con el export.

Para estos bloques, el contrato de salida es PNG, OGG **Vorbis** y MP4.
Puedes inspeccionar otros formatos, pero conviértelos antes de exportar.
No se renombra un MP3 como OGG ni un JPG como PNG para fingir compatibilidad.

Si falta un recurso, dos archivos diferentes acaban en la misma ruta o el
formato es incompatible, la publicación se bloquea. Dos referencias al mismo
contenido no necesitan dos copias idénticas. Los bloques desconectados no
aportan recursos al paquete.

## 21. Variables, condiciones y temporizadores

Usa una variable cuando un valor deba sobrevivir entre aciertos del mismo
tipo. Define su identidad y elígela desde los desplegables; el nombre visible
no debe crear accidentalmente otra variable.

Una condición puede consultar dirección, jugador, juicio, vida, combo,
tiempo u otros datos disponibles. Revisa la ayuda del sensor: no todos tienen
sentido en un fallo o al crear una nota.

Las condiciones vacías no equivalen a verdadero. Una probabilidad debe
probarse con suficientes notas, no con un único acierto.

Repeticiones y tareas diferidas tienen límites de ejecución para evitar
bucles y acumulación descontrolada. No intentes implementar un evento por
frame multiplicando temporizadores en cada tap.

Ejemplo de lógica de screamer controlado:

```text
cuando el jugador toca el tipo Scare
  si dirección es arriba
    screamer imagen + sonido, 0,8 s, volumen 0,4, espera 2 s
```

Conecta la condición y el efecto dentro de su cuerpo. La espera pertenece
al bloque del efecto; no es la separación entre notas del chart.

## 22. Editar código y sincronizarlo

Las vistas **Blocks / Code / Both** muestran el mismo tipo. En Both puedes
redimensionar el espacio entre lienzo y editor.

Selecciona el motor cuyo archivo estás editando. El código tiene colores
de sintaxis y números de línea; los colores ayudan a leer, pero no sustituyen
la validación del motor.

Tras editar, usa **Apply to blocks**. La sincronización reconoce el código
soportado por el generador. No convierte cualquier archivo Haxe/Lua en un
programa visual equivalente.

Si el cambio no se puede representar, conserva un archivo custom para ese
motor o descarta el borrador. El archivo custom sustituye el comportamiento
generado correspondiente; los bloques anteriores dejan de describirlo por
completo. La UI lo señala.

La exportación se bloquea mientras haya un borrador pendiente de decisión.
Guarda el proyecto para conservarlo, pero no confundas guardar un borrador
con aplicarlo al programa.

## 23. Comentarios y borradores

El generador actualiza las descripciones de acuerdo con el programa.
Los comentarios propios se conservan aparte de esa descripción automática.

Selecciona un bloque para editar su comentario en **Comments and preview
coverage**. Sin un bloque seleccionado, editas el comentario del tipo.
Puedes utilizar el editor de código para los comentarios que excedan el
límite del pequeño panel.

Anota intención y condiciones, no solo lo que ya dice el bloque:
«Solo en taps del jugador; evitar repetir durante dos segundos» ayuda más
que «reproduce un sonido».

Cuando haya archivo custom, documenta sus recursos manuales y las APIs que
requiere. La lista de recursos de bloques no descubrirá esas dependencias
automáticamente.

## 24. Distribuir notas en una canción

Abre la canción/dificultad y ve a **Distribute / Distribuir**.
Añade tipos y elige porcentaje o cantidad exacta. El reparto utiliza una
semilla para obtener resultados reproducibles. También funciona en el patrón de
prueba, para probar tus notas sin canción; un patrón no se guarda como chart
(capítulo 42).

Revisa el número de candidatas y de notas realmente colocadas. Un porcentaje
se calcula sobre las candidatas que pasan los filtros, no siempre sobre
todas las notas del chart.

Filtros útiles:

- Jugador, rival o ambos.
- Carriles elegidos.
- Excluir sostenidos.
- Excluir acordes.
- Separación mínima entre notas del mismo tipo.
- Respetar o reemplazar los tipos custom que ya existían.

Si aparecen 0 o muy pocas notas, primero comprueba filtros y tipos
protegidos. No aumentes el porcentaje a ciegas. Un chart con muchos tipos
propios puede dejar pocas notas normales disponibles.

Usa una cantidad exacta para solicitar, por ejemplo, veinte notas candidatas.
Eso no crea candidatas donde los filtros las excluyen. Guarda la semilla y
anota la dificultad para poder repetir el reparto.

## 25. Bot de preview

El bot custom se abre desde su botón o desde el clic derecho del tipo en el
catálogo. Configura jugador y rival de manera independiente: heredar,
tocar o evitar.

Sirve para comprobar una distribución con distintas reglas de preview.
Un tipo dañino marcado como evitable no debe evaluarse como una nota normal
solo porque utilice la misma imagen.

El bot no ejecuta cada script del motor ni certifica que el mod sea
superable. Prueba también manualmente y, después, dentro del juego destino.
Su configuración queda en el proyecto.

## 26. Guardar el chart sin perder el original

La distribución empieza como una edición dentro de Note Lab.
**Save distribution as chart / Guardar distribución como chart** permite
publicarla como chart nativo.

Para la primera prueba, guarda una copia. Reemplazar el original requiere
una elección explícita y genera un backup único. Si el archivo cambió
externamente desde que lo cargaste, se bloquea la sustitución.

No se reemplazan charts dentro del ZIP ni los de una fuente de juego base.
Tampoco debes dar a una copia JSON el nombre del ZIP de origen.

Comprueba qué canción, dificultad y archivo se van a guardar. El backup del
chart no incluye imágenes, programas de bloques ni una copia de todo el mod.
Conserva también el proyecto y un backup de tu mod.

## 27. Proyectos, fuentes y cache

Ctrl+S guarda `.fmlnote`. Incluye configuraciones, estilos editados, recetas,
programas, comentarios, borradores y referencias de importación.

Actualmente no es un bundle portable de todas las fuentes. Mantén:

- El proyecto.
- Las carpetas o ZIP de origen.
- La cache de recursos importados/generados.

La cache está bajo `%LOCALAPPDATA%/FunkinNoteLab`. No la borres como
«temporales sin importancia» si el proyecto utiliza archivos que solo están
allí. Cambiar el mod de carpeta puede romper sus referencias.

Abrir un proyecto con fuentes faltantes se bloquea sin sustituir el trabajo
actual. No se promete un asistente completo de reconexión en esta build.
Para entregar a otra persona un resultado jugable, entrega el export; para
entregar un proyecto editable, conserva sus dependencias.

## 28. Exportar a los tres motores

![Revisión del export con el juego base cargado](images/export-base-es.png)

Ctrl+E prepara el export del estilo; desde Bloques también puedes exportar
el tipo actual. Elige motor, función del paquete y grupos incluidos.
Lee advertencias antes de seleccionar carpeta o ZIP.

| Motor | Organización habitual del comportamiento de tipos |
| --- | --- |
| Codename | Definiciones/configuración compatibles y scripts HX bajo data/notes. |
| Psych | Configuración y scripts Lua bajo custom_notetypes; recursos de skin según el formato admitido. |
| V-Slice | Datos de estilos y NoteKind HXC bajo scripts/notekinds cuando corresponda. |

Estas rutas explican el destino del comportamiento; la guía de instalación
generada por cada paquete es la referencia para **ese export concreto**.
Skin completo, tipo custom y HUD no tienen necesariamente la misma
instalación ni sustituyen los mismos archivos.

El exporter puede adaptar escalas, tiempos o formatos visuales que el motor
no permite declarar directamente. Algunas funciones son aproximadas o no
están disponibles. No ocultes esos avisos al entregar el mod.

Antes de escribir, Note Lab construye el paquete y lo vuelve a leer con sus
lectores. Si faltan recursos necesarios o quedan errores, la publicación se
bloquea. Esa relectura comprueba estructura, no ejecuta todos los scripts.

Un destino existente solo se reemplaza cuando es una exportación reconocida
y cumple las comprobaciones de protección. No utilices una carpeta de mod
con trabajo ajeno como carpeta de export por comodidad.

Conserva INSTALL.txt, LEEME_INSTALAR.txt y el manifiesto. Prueba en una copia
del motor antes de incorporar los archivos a tu mod principal.

## 29. Validación dentro del juego

La prueba completa requiere el motor real:

1. Crea una copia o instalación de prueba.
2. Instala el paquete según su guía.
3. Comprueba el nombre/ID del tipo en el chart.
4. Reproduce notas normales, custom, sostenidos, aciertos y fallos.
5. Observa receptores, splashes, ranking y cuenta atrás.
6. Prueba pausa, reinicio y final de canción con efectos activos.
7. Revisa logs por recursos ausentes o APIs no disponibles.
8. Prueba los vídeos con el codec y la versión que vas a distribuir.

Las pruebas automatizadas de esta entrega se describen por separado de las
partidas registradas el 5 de octubre de 2026 en Psych 1.0.4, Codename 1.0.1 y
V-Slice 0.8.6. Esas partidas comprobaron ejemplos de HUD, notas custom,
sostenidos y algunos bloques; no se repitieron para la 1.0.4a.
**No certifican todos los bloques multimedia ni cualquier versión de los
motores.** El informe de la entrega
(`docs/RELEASE_CHECKS.md`, en el ZIP de Developer) recoge el alcance y las
evidencias.

## 30. Ejercicios completos

### A. Aprender sin modificar el juego base

Abre el juego base, selecciona Funkin' y Bopeebo en dificultad normal.
Inspecciona una nota en Assets y revisa sus frames. Cambia solo opciones de
preview. Crea una variante, guarda un proyecto y exporta a una carpeta de
prueba. No copies encima de assets del juego base.

### B. Notas y receptores separados

Abre una hoja de notas y un estilo con los receptores que quieres.
Combina únicamente receptores. Revisa reposo, pulsado y confirmación en
cuatro direcciones. Exporta un skin completo y comprueba que se incluyan
ambos orígenes. No te conformes con el respaldo visual de preview.

### C. Nota custom visual y reglas

Crea el tipo TestNote, asigna un aspecto y añade un preset sencillo.
Distribuye una cantidad pequeña en el jugador, con semilla fija y sin
reemplazar tipos existentes. Guarda una copia del chart, exporta el tipo y
comprueba que el motor reconozca su nombre.

### D. Imagen y sonido desde el mod

Crea Scare, añade el bloque de screamer y asigna una imagen PNG y un OGG
Vorbis desde la biblioteca. Usa 0,8 s, volumen 0,4 y espera 2 s.
Comprueba la lista de recursos en uso y que ninguno falte.
Exporta e inspecciona que images y sounds contengan los archivos.
La prueba final del efecto se realiza dentro del motor.

### E. Recursos propios sin escribir en el mod

Importa un PNG propio como solo proyecto, elige images/custom y permite
crear la carpeta. Inspecciónalo, asígnalo a un bloque y guarda el proyecto.
El export debe incluir el PNG; tu mod original no debe haber cambiado.

### F. Vídeo opcional

Importa un MP4 compatible, pruébalo en el reproductor del sistema y asígnalo
a una nota de prueba poco frecuente. Elige volumen moderado y duración
máxima. Lee la advertencia de la API del motor. Instala y prueba el paquete
antes de anunciar soporte para esa versión.

## 31. Solución de problemas

| Síntoma | Comprobaciones |
| --- | --- |
| No aparece ningún estilo | Carpeta correcta, assets presentes, motor y fuentes base. |
| Receptores invisibles | Hoja separada, prefijos, composición; no confundir fallback con export. |
| Animación vacía | Hoja y metadata correctas, región dentro de imagen, orden no inválido. |
| Solo se ve un frame | HUD estático, FPS, selección de frames y asignación actualizada. |
| Muy pocas notas custom | Filtros, tipos protegidos, lado, separación y candidatas. |
| Bot toca una nota dañina | Reglas del tipo y perfil tocar/evitar por lado. |
| El sonido no se oye | Volumen, carga terminada, archivo decodificable y botón Escuchar. |
| No suena al mover bloques | Normal: los sonidos de interacción están silenciados. |
| Screamer sin sonido | Ranura OGG asignada, recurso existente y export del tipo instalado. |
| Vídeo no aparece en juego | API/script de vídeo, codec, motor/versión y ruta del paquete. |
| Import no crea carpeta | Confirmar Create destination folder y una raíz editable válida. |
| Import rechaza un archivo existente | Cambiar nombre/destino; no hay sobrescritura automática. |
| Export bloqueado | Leer los errores; aplicar o descartar borradores, resolver media faltante. |
| Proyecto no abre | Fuente o cache ausente; conserva el trabajo abierto y restaura dependencias. |
| Funciona en Preview, no en juego | Preview parcial; ID de tipo, formato y APIs del motor. |

Al reportar un problema, incluye build de Note Lab, motor y versión, recurso,
tipo/dificultad, pasos mínimos y mensaje/log. Si puedes, aporta una muestra
pequeña propia. No publiques una instalación completa sin permiso.

## 32. Atajos y buenas prácticas

Ctrl+S guarda el proyecto; Ctrl+E prepara exportación. Ctrl+Z/Y permiten
deshacer/rehacer donde el editor los admite. El botón central desplaza
los lienzos y la rueda hace zoom en los paneles que lo indican.

Mantén los nombres de tipos y recursos estables. Usa carpetas específicas
para imágenes, sonidos y vídeos propios. Evita carpetas enormes, nombres
duplicados y dependencias implícitas que solo funcionan en tu instalación.

Prueba primero una pieza, un tipo y una canción. Después combina.
Usa copias de charts y motores para validar. Guarda antes de cerrar o
recargar fuentes. No dependas de que deshacer sobreviva a una sesión nueva.

## 33. Desarrollo y límites actuales

La entrega Developer contiene las fuentes de Note Lab, su núcleo de edición y
las dependencias necesarias. No es la suite completa de FML.

`src/` contiene UI; `core/` contiene modelos, lectores, creación, bloques,
persistencia, recursos y export; `support/` contiene las dependencias
reutilizadas. `docs/MODULES.md` (en el ZIP de Developer) documenta archivos
y scripts. FML_SYNC_MAP.json
permite revisar y trasladar cambios a sus rutas compartidas.

Para compilar se necesitan herramientas C++ x64 de Visual Studio y Windows
SDK. build.bat produce la app; test.bat prueba el núcleo;
test-workflow.bat comprueba flujos; test-ui.ps1 prueba la UI nativa.
Los motores originales no deben utilizarse como destinos de fixtures.

La licencia MIT del código no relicencia los recursos de juegos/mods.
Las capturas de esta guía son demostrativas; conserva las licencias y
permisos de cualquier artwork que redistribuyas.

Límites que no se deben anunciar como resueltos:

- Proyecto portable que incluya automáticamente todas las fuentes.
- Ejecución completa de scripts arbitrarios en Preview.
- Conversión universal de código a bloques.
- Layout del HUD exportable de forma idéntica a todos los motores.
- Soporte multikey universal más allá de los contratos actuales.
- Vídeo integrado en el editor y compatibilidad con cualquier codec/build.
- Análisis automático de todas las dependencias de archivos custom.

Los resultados de pruebas y los pendientes concretos están en
`docs/RELEASE_CHECKS.md`, y las propuestas futuras en
`docs/NEXT_IMPROVEMENTS_ES.md`; los dos van en el ZIP de Developer.

## 34. Tutoriales y modo foco

Los tutoriales trabajan sobre la interfaz real. **Primeros pasos** tiene
12 misiones: abrir, elegir, cargar una canción, jugar, crear y guardar tu
nota en el mod.
Las guías de zona cubren bloques, Crear HUD, creador custom/ranking, recursos,
exportación y editor de sprites. No es necesario terminar una para usar la app.

![Guía del editor integrada en su ventana](images/sprite-tutorial-es.png)

1. Acepta la tarjeta inicial o abre **Tutorial → Primeros pasos**.
2. Sigue el objetivo y realiza la tarea. Se marca por el estado real del editor.
3. Pulsa **Muéstrame** si no encuentras el control: lo encuadra y desplaza el
   panel para hacerlo visible.
4. Clic o Esc retiran la sombra; el contorno y la indicación pueden permanecer
   hasta completar el objetivo. Arrastrar oculta temporalmente la indicación.
5. Puedes saltar una misión o la guía. El menú permite continuar, repetir
   desde el principio o volver a habilitar las ofertas de cada zona.
6. Una misión pasada, hecha o saltada, se puede volver a hacer desde **Todas
   las misiones** o sus casillas de progreso (capítulo 42).

Primeros pasos tiene prioridad sobre las guías de zona para evitar varias
indicaciones a la vez. Lo que ya tenías hecho al empezar se salta solo. Al
repetir, una tarea ya resuelta no se cuenta como nueva: vuelve a hacerla o usa
**Siguiente misión**.
El tutorial de jugar pide tres aciertos y devuelve después la preview a auto.

La guía de bloques monta tres notas básicas: daño, curación y suerte.
Su mini vídeo muestra bloques reales encajándose. La paleta del tutorial
presenta los necesarios para la misión; **Ver todos** devuelve la paleta
completa. Si la nota actual contiene tu trabajo, la guía pide una nueva.

Desde la 1.0.3 los tutoriales no suenan: sus avisos de misión, final y oferta van
silenciados, su interruptor no aparece y los paquetes no traen archivos de
sonido. Los sonidos de interacción del editor de bloques siguen apagados; las
canciones y la audición de archivos conservan su audio.

## 35. Dibujar las piezas de tu HUD

Abre un estilo base y **Crear HUD**. La ventana va por pasos: **Base** (de qué
estilo partes), **Colores**, **Detalles** y **Piezas**, con **Atrás** y
**Siguiente**; también puedes saltar a un paso con su número. En **Piezas**
elige **Dibujadas por mí**: aparece una tarjeta por pieza con su miniatura (la
que dibujaste o la del estilo). Elegirlo no abre el editor por sí solo; un clic
en una tarjeta lo abre en esa pieza, tras preguntar cómo quieres empezar
(capítulo 41). No modifiques el PNG original a mano.

![Crear HUD en el paso Piezas, con una tarjeta por pieza](images/hud-steps-es.png)

Cada fila tiene una función:

| Fila | Uso |
| --- | --- |
| Nota | Cabeza de la nota que se desplaza. |
| Receptor | Estado en reposo de la flecha fija. |
| Receptor pulsado | Estado al pulsar sin un acierto. |
| Receptor al acertar | Confirmación del acierto. |
| Tramo del sostenido | Cuerpo repetido o estirado de la nota larga. |
| Final del sostenido | Remate de su cola. |
| Salpicadura | Efecto de splash. |

Elige una fila y un hueco antes de dibujar. La hoja general ofrece seis
huecos por fila para los fotogramas. Una fila que no dibujes conserva la pieza
del estilo base; no significa que estés borrando ese componente del HUD.

![Hoja general, herramientas y capas](images/sprite-sheet-es.png)

Dibuja la dirección izquierda; nota y receptor pueden rotarse a las otras
direcciones. Ajusta la rotación y comprueba las miniaturas, especialmente si
tu dibujo no es una flecha. El tinte usa la luminosidad para aplicar la paleta
de cada dirección manteniendo contorno y luces; comprueba el resultado en las
cuatro, no solo en el dibujo sin teñir.

Puedes usar **Receptores con la forma de la nota** o dibujar sus estados
independientemente. La app puede derivar estados pulsado/acierto y animar un
splash de un frame. Para controlar una secuencia propia, dibuja sus frames.

![Receptor de acierto editable por separado](images/sprite-hit-receptor-es.png)

**Usar en el HUD** devuelve las piezas al creador, pero aún no crea el estilo.
Revisa la preview y confirma **Crear HUD**. El estilo resultante conserva su
receta: abrir Crear HUD sobre él permite recuperar lo dibujado.

Para una única nota especial, usa **Cambiar su aspecto → Dibujarla** en el
catálogo o el menú de Bloques. Ese editor solo tiene filas de nota/sostenido;
no es un creador de receptores o de todo el HUD.

![Aspecto de una nota, limitado a sus piezas](images/sprite-note-look-es.png)

## 36. Capas, selección, pestañas y fotogramas

Empieza en una capa de dibujo. Pincel y goma tienen tamaño, dureza y opacidad.
Con figuras arrastra sus extremos; puedes configurar relleno y contorno.
El bote admite tolerancia; el cuentagotas copia un color. Los presets se crean
en otra capa, dentro de la selección o del hueco elegido, sin sustituir todo
lo que habías pintado.

Al elegir **Figura** (U), la barra superior muestra el selector con miniaturas:
rectángulo, rectángulo redondeado, elipse/círculo, triángulo, rombo, estrella,
flecha, corazón, pentágono, hexágono, trapecio y cruz. No cambia las plantillas
de notas ni los presets de HUD.

![Selector de doce figuras, con el juego base cargado](images/shape-picker-en.png)

Elige una figura y arrastra para definir su tamaño. **Colocar figura** la
inserta centrada en la selección o el hueco elegido, en una capa nueva que
puedes mover o deshacer. **Rellena** activa el interior; **Grosor** ajusta el
contorno entre 0 y 32 píxeles. La flecha conserva su contorno habitual y su
control de grosor queda desactivado.

**Pixel art** dibuja la figura sin suavizado en los bordes; si el lienzo ya es
pixel art, se aplica automáticamente. Cada figura recuerda sus ajustes de
grosor y pixel art mientras trabajas en el editor. Los dibujos guardados
conservan los píxeles, no figuras vectoriales que puedan reeditarse después.

| Atajo del editor | Acción |
| --- | --- |
| B / E | Pincel / goma. |
| U / G / I | Figura / relleno / cuentagotas. |
| M / V | Seleccionar rango / mover. |
| X | Intercambiar colores principal y secundario. |
| Ctrl+C / X / V | Copiar / cortar / pegar la selección. |
| Ctrl+A / D | Seleccionar todo / quitar selección. |
| Ctrl+Z / Y | Deshacer / rehacer. |
| Supr | Vaciar el rango seleccionado de la capa activa. |
| Rueda | Zoom en el lienzo. |
| Botón central o Espacio + arrastrar | Desplazar la hoja. |
| Shift + clic de pincel/goma | Línea desde el último punto. |
| Clic derecho | Pintar con el color secundario. |
| Alt + clic | Tomar un color con cualquier herramienta. |
| [ / ] | Pincel más pequeño / más grande (con Shift: dureza). |
| 0 / 1 / F | Ver toda la hoja / 100 % / encuadrar el hueco elegido. |

El botón **?** de la barra recuerda estos atajos. **Espejo arriba-abajo** y
**Espejo izq.-der.** repiten cada trazo reflejado; **Pixel art** pinta sin
suavizado y, de cerca, enseña la rejilla de píxeles. Al acercarte (o en un
lienzo de pixel art) los píxeles se ven nítidos. El menú **Archivo** reúne
**Empezar de nuevo…**, **Cargar del estilo**, **Abrir hoja…** y **Guardar
dibujo**; en el aspecto de una nota también **Ver el PNG final**. Dibujando
todo el HUD, **PNG final** tiene su propio botón.

Los atajos no deben usarse mientras escribes en un campo. Para verificar una
acción, revisa el icono activo y su ayuda. Cortar o borrar trabaja en la capa
activa; contenido de otra capa puede seguir viéndose.

![Seleccionar y trabajar sobre un hueco ampliado](images/sprite-selection-es.png)

El panel Capas permite añadir, duplicar, renombrar, ocultar, ajustar opacidad,
reordenar y unir abajo. Unir aplana esas capas: guarda un `.nlsprite` antes si
quieres conservarlas separadas. El límite actual es 16 capas.

**Mis colores → +** guarda el color principal en tu paleta; clic derecho lo
quita. La paleta queda en preferencias, no se distribuye como parte del mod.

![Paleta personal y capas](images/sprite-palette-es.png)

La línea de tiempo corresponde a la fila seleccionada. Elige un frame,
reproduce, cambia FPS, activa papel cebolla, duplica, elimina o mueve el
fotograma. Los huecos usados se reproducen en su orden. Un recorte copiado puede
pegarse en la selección o con **En un hueco libre** como siguiente frame.
No confundas borrar píxeles de una capa con **Borrar fotograma**.

Las pestañas sirven para dibujos sueltos, un hueco editado aparte o referencias.
**Pasar a la hoja general** traslada una pieza suelta a su fila (capítulo 41).
En el menú **Archivo**, **Abrir hoja** acepta PNG con XML/TXT, imágenes, GIF o
`.nlsprite`, y **Cargar del estilo** extrae sus piezas para usarlas como base;
los archivos del mod siguen intactos.

![Dibujo suelto en su propia pestaña](images/sprite-tabs-es.png)

En hojas grandes usa presets de zoom y el mapa: clic/arrastre en él mueve la
vista. Si pierdes el área de trabajo, encuadra antes de aumentar la escala.
El editor recompone la región modificada al pintar; evita duplicar capas
enormes cuando solo necesitas otro frame.

## 37. Dibujo editable y PNG final

Guarda dos productos distintos si vas a continuar editando:

- **Guardar dibujo → `.nlsprite`**: capas, nombres, visibilidad, opacidad,
  pestañas, fotogramas y ajustes. Es el original editable de Note Lab.
- **PNG final → Guardar PNG + XML**: imagen ensamblada y metadata Sparrow,
  sin capas. Sirve para inspección o uso en otra herramienta.

Usar el dibujo en una nota/HUD también conserva su archivo editable con la
receta del proyecto. Sigue siendo una referencia a la cache: no borres esa
carpeta esperando que el `.fmlnote` sea un bundle autosuficiente. Guardar tu
propio `.nlsprite` aparte te da una copia explícita reutilizable.
Abrir un dibujo con trabajo existente añade una pestaña, no lo pisa.

![PNG final de todo el HUD](images/sprite-final-es.png)

En **Todo el HUD**, el resultado combina piezas base y dibujadas.
**Solo lo dibujado** limita la hoja a esas piezas. Pasa el cursor sobre cada
frame para revisar nombre y límites; comprueba sostenidos, estados y splash,
no solo la nota principal.

![PNG final limitado a las piezas dibujadas](images/sprite-final-drawn-es.png)

**Archivo → Guardar hoja de notas (PNG + XML)** también ensambla el estilo
seleccionado aunque no se haya creado dibujando. Es útil para inspeccionar una
composición de varias hojas en una sola, no para reconstruir capas originales.

PNG/XML no crea por sí solo scripts, registros de skin ni definiciones
NoteKind. Para instalar en un motor, usa el export de ese motor y su guía.
Conserva una copia de dibujo, proyecto y export como productos diferentes.

## 38. Scripts a bloques y programas reutilizables

Selecciona el tipo y abre **Bloques → … → Pasar su script a bloques**.
Lee el informe: cuántas sentencias se reconocieron, cuántas quedaron como
código y qué funciones no entraron. El script del mod no se modifica.

Se reconocen patrones concretos de eventos y acciones: vida, puntuación,
fallos, combo, sonido, sacudida, flash, «Hey!», mensajes y ciertas propiedades
de notas de Psych. No es un intérprete general de Lua/Haxe ni un descompilador.

![Conversión de un ejemplo de código propio, con V-Slice base cargado](images/script-to-blocks-base-en.png)

![Código y bloques sobre una fuente del juego base](images/code-editor-en.png)

| Resultado | Qué hacer |
| --- | --- |
| Bloque reconocido | Revisar valor, conexión y evento. |
| Bloque «código» | Revisar la línea original y el motor indicado. |
| Función no admitida, como ciertos `onUpdate` | Conservar su script y resolverla aparte; no se ejecuta mágicamente como evento de nota. |
| Textura declarada por el script | Asignar el aspecto y sus recursos; un nombre de textura no dibuja una imagen por sí solo. |

Las líneas no reconocidas dentro de un evento permanecen en su posición.
Solo se emiten como código para su motor original; en otro quedan como
comentarios y generan un aviso. Una línea vacía también avisa que no exporta
ninguna acción. Revisa bloques de apertura/cierre y dependencias: preservar
texto no prueba que compile ni reproduce su comportamiento en Preview.

Si el tipo ya tiene bloques o es solo código, la conversión crea otro tipo
«Nombre (bloques)»; no borra el anterior. Si decides usar el nuevo, distribúyelo
en el chart y exporta el nombre/ID correspondiente.

**Guardar programa de bloques (.nlblocks)** permite reutilizar el programa.
Abrirlo en un tipo vacío lo carga; en uno con trabajo añade pilas nuevas a su
lado. Comprueba conexiones y nombres de recursos después de importarlo.
El `.nlblocks` no incluye automáticamente tus imágenes o sonidos.

**Guardar su código en una carpeta** respeta las rutas nativas: Codename
`data/notes/*.hx`, Psych `custom_notetypes/*.lua`, V-Slice
`scripts/notekinds/*.hxc`. No sustituye el export de sus dependencias.
Para sincronizar una edición de código generado con sus bloques utiliza
**Aplicar a bloques**; una conversión de script externo y una edición de un
borrador generado son flujos distintos.

## 39. Efectos, canciones y música

En Recursos elige Sonidos. **Efectos** es el filtro inicial; **Canciones**
agrupa Inst y Voices por canción; **Música** organiza lo que está bajo `music/`.
La clasificación aplica a los tres motores. **Incluir juego base** añade sus
proveedores cuando trabajas con un mod que hereda recursos.

![Canciones e instrumental agrupados en la biblioteca](images/sounds-songs-base-es.png)

![Inspector de sonido del juego base](images/resource-sound-base-es.png)

Elegir una fila no la reproduce. Inspección y reproducción son explícitas;
puedes abrir la carpeta origen con sus acciones contextuales. Busca la ruta
si dos recursos tienen nombres parecidos y comprueba qué proveedor ganó.

La audición permite recorrer el archivo completo. **Loop** repite el archivo; al
activar A–B repite solo ese rango. Velocidad, balance y volumen afectan esa
escucha, no el archivo original ni los valores de un bloque de sonido.
**Stop** vuelve al inicio o al punto A y cancela un arranque pendiente.

Una voz o instrumental de la biblioteca no pasa a ser automáticamente la
canción activa de Preview; cárgala desde el selector de canciones. El audio
de la canción y la audición del recurso son transportes distintos.

## 40. Comprobaciones específicas por formato

Antes de entregar tu mod:

1. En Psych 1.0, comprueba ambos lados del chart: `psych_v1` usa carriles
   absolutos. `mustHitSection` dirige la cámara, no intercambia al jugador.
   Los exports de Psych usan la convención clásica y el motor moderno la convierte.
2. En plantillas RGB de Psych, verifica los cuatro sostenidos. Al pintar
   regiones Sparrow compartidas, cada aspecto recibe su copia; las piezas
   intactas conservan la suya. Una rejilla/tira no permite esa reubicación y
   mantiene el límite de primera pieza: no se anuncia como separación universal.
3. En V-Slice, un receptor de acierto de un frame se exporta con dos frames
   sobre la misma región para que pueda volver al reposo. Compruébalo con el
   bot y el rival.
4. Haz varias vueltas de reproducción automática. El inicio nuevo debe volver
   a mostrar taps y cabezas de sostenidos, sin conservar sus aciertos anteriores.
5. Guarda, cierra y reabre el dibujo y el proyecto. Revisa capas, frames,
   recursos y roles; prueba también una exportación en una carpeta con acentos.
6. Prueba el paquete final en la versión del motor que distribuirás y guarda
   las advertencias junto al resultado. Un PNG correcto no valida el script.

El informe de la entrega (`docs/RELEASE_CHECKS.md`, en el ZIP de Developer)
enumera los resultados actuales, capturas revisadas, prueba del ZIP público y
compilación del Developer extraído.

## 41. Empezar a dibujar y lienzo libre

Desde la 1.0.3, el editor de sprites no te deja directamente en la hoja: antes
pregunta **¿Cómo quieres empezar?**. Elijas lo que elijas, el dibujo acaba en la
hoja de la nota o del HUD, cada hueco un fotograma.

![¿Cómo quieres empezar? Plantilla, lienzo libre o lienzo de una pieza](images/sprite-start-es.png)

| Opción | Para qué |
| --- | --- |
| **Plantilla con huecos** | La hoja de siempre: una fila por pieza y sus huecos. Puedes empezar con una flecha, mina, corazón, vacía o la del estilo. |
| **Lienzo libre** | Un lienzo del tamaño que elijas (16×16 a 256×256, o cualquiera hasta 512), con opción **Pixel art**. |
| **Lienzo de una pieza** | El tamaño exacto de una pieza en el juego, con su forma para empezar. |
| **Seguir** | Vuelve a lo que tenías (capas, fotogramas y pestañas), si ya habías dibujado. |

Un lienzo libre se abre en su propia pestaña. Al terminar, **Pasar a la hoja
general** lo pone como Nota en el siguiente hueco libre; la flecha de al lado
elige otra pieza o un hueco concreto (y lo sustituye). Si el tamaño no coincide
con el hueco, se ajusta sin deformarse; en pixel art cada píxel crece un número
entero de veces, nítido. El pie del editor dice a qué tamaño quedará.

![Lienzo libre de 32×32 en pixel art](images/sprite-free-pixel-es.png)

Ejemplo: dibuja tu nota en 32×32 con **Pixel art**, pásala a la hoja general y
revisa en **En el juego** cómo queda con su sostenido. Puedes abrir otro lienzo
libre desde **+ Abrir… → Lienzo libre (tú eliges el tamaño)…** sin perder la hoja.

En el editor del aspecto de una nota, la columna derecha enseña la nota con su
tramo y final de sostenido **En el juego** y las cuatro direcciones, sin
desplazarte. El mapa de la hoja solo aparece al dibujar todo el HUD.

## 42. Tus notas en el mod, patrones y volver a misiones

Novedades de la 1.0.4.

### Guardar una nota en el mod

Una nota que creas en Note Lab vive en el proyecto hasta que la guardas en su
mod. **Guardar en el mod**, en su vista general (un clic en **Tus notas**), o
**Guardar** en Bloques la escribe en la carpeta del mod abierto, donde la busca su motor:
Psych `custom_notetypes/`, Codename `data/notes/`, V-Slice `scripts/notekinds/`,
más su aspecto si tiene uno propio. La primera vez enseña los archivos y dónde
van; después guarda sin preguntar.

![Guardar en el mod: lo que se escribe y dónde](images/save-in-mod-es.png)

Sus bloques van dentro de su propio script, en un comentario que el juego
ignora. Al abrir el mod otra vez —sin proyecto, incluso en otro PC— la nota
vuelve a **Tus notas** con sus bloques y la marca **en el mod**.

![El mod abierto otra vez, sin proyecto: la nota vuelve con sus bloques](images/own-note-in-mod-es.png)

- Nunca escribe en un ZIP ni en la instalación del juego base: descomprime el
  mod o abre uno de la carpeta `mods` del juego.
- Un archivo que ya estaba en el mod y no escribió Note Lab solo se reemplaza
  si marcas **Reemplazarlos**.
- Si el script se edita a mano después (en cualquier parte, también debajo del
  comentario), la nota sale como **script editado a mano**: elige **Pasar su
  script a bloques** o **Usar los bloques guardados**. Guardarla otra vez pide
  permiso para reemplazar el script editado.
- Al salir con notas que aún no están en su mod, Note Lab lo dice y ofrece
  **Guardarlas en el mod**. Si sales sin guardar el proyecto, guarda una copia:
  al abrir otra vez, **Tu última sesión no se guardó** ofrece **Recuperarla**.

La vista general de una nota tuya enseña su aspecto, lo que hace, si está en el
mod y su código para el motor del mod abierto, con **Copiar código**, **Pasar a
bloques** (si es solo código) e **Ir a la primera** cuando la usa la canción o el
patrón cargado: la vista previa se abre ya, un poco antes de la nota.

### Protección de guardado y recuperación en la 1.0.4a

- Dos nombres distintos no pueden generar los mismos archivos de nota. Por
  ejemplo, `Fire Note` y `Fire-Note`, o cambios solo de mayúsculas, pueden
  coincidir al exportarse: elige un nombre realmente diferente. Esta protección
  se aplica también al guardar notas de proyectos anteriores.
- El comentario de bloques guarda la identidad de la nota. Si un archivo
  existente pertenece a otra nota, no se considera propio ni se sobrescribe
  automáticamente. Los comentarios antiguos siguen siendo compatibles; un
  archivo sin identidad reconocida requiere permiso explícito para reemplazarse.
- Cambiar escala, animaciones, recortes u otras propiedades del aspecto marca
  la nota como **con cambios**, incluso si conserva el mismo ID de aspecto. Al
  guardar, se incluye también el aspecto de una nota importada.
- Una sesión pendiente de recuperación se conserva separada de la copia de la
  sesión actual. Abrir otro mod o guardar otro proyecto no la elimina.
  **Archivo → Recuperar sesión anterior…** permite recuperarla con un mod ya
  abierto y pide confirmación si hay cambios sin guardar. Recuperar una anterior
  conserva también la sesión desplazada; descartar una recuperación no borra
  la copia de la sesión activa.

### Patrones de prueba

Sin canción, la vista previa toca un patrón de prueba. Arriba de la lista de
canciones hay siete: Básico, Escalera, Repeticiones, Acordes, Sostenidos
largos, Ráfaga y Aleatorio. **Editar patrón…** abre una cuadrícula: arriba el
rival, abajo el jugador, una columna por semicorchea, de 1 a 16 compases.

![El editor de patrones partiendo de «Sostenidos largos»](images/pattern-editor-es.png)

Un clic pone una nota y otro clic la quita; arrastrar a la derecha la hace
sostenida (hasta la siguiente nota de su carril); el clic derecho también la
quita. **Partir de…** copia uno de serie y **Vaciar** deja la cuadrícula en
blanco. **Guardar y ponerlo** lo guarda con su nombre en tus preferencias
(hasta 32) y lo pone en la vista previa, al BPM de la vista previa. Abrir uno
tuyo lo edita; **Borrar patrón** lo quita.

**Distribuir** también funciona en un patrón de prueba, con los tipos del mod
elegido: sirve para ver y jugar tus notas sin canción. Un patrón no es un
chart: no hay **Guardar distribución como chart…**. Cambiar el BPM mantiene el
reparto; cambiar de patrón lo quita, como cambiar de canción.

### Volver a una misión del tutorial

Al empezar o continuar un tutorial, lo que ya tenías hecho se salta solo (y se
dice). En **Todas las misiones**, las que se pasaron sin hacerlas salen como
**saltadas**. Un clic en cualquier misión pasada —hecha o saltada— vuelve a
ella: deja de contar, el tutorial te lleva a donde se hace (la pestaña, la vista
o el paso de **Crear HUD**) y nada de tu trabajo cambia. En las guías de zona,
lo mismo con las casillas de progreso.

![Todas las misiones: las saltadas se pueden hacer](images/tutorial-back-es.png)

Si lo que pide la misión ya se cumple (por ejemplo, ya hay una canción
cargada), cuenta al hacerlo otra vez —otra canción, otro estilo— o con
**Siguiente misión**. **Abre un mod** es la única que no se repite mientras haya
un mod abierto. Las misiones que piden crear una nota te guían dentro de la
ventana **Nueva nota custom**: el nombre, **Crear nota** y luego sus bloques.

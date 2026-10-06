# Bitácora de Note Lab

## 2026-10-06 — cierre de la 1.0.3, sin sonidos

El autor pidió «silencia los sonidos de los logros para esta 1ra build» y
lanzar el paquete público y el de desarrollo de la v1 pública; luego, «sin los
sonidos la build pública ni dev, los archivos».

**Sonidos.** `nlbuild::tutorialAudioPlayback` pasa a `false`: no suena ningún
aviso del tutorial (misión, final ni oferta) y el interruptor «Sonidos» se
oculta en la ventana del tutorial y en su menú (comprobado en capturas). Ni
`assets/sounds` ni `tools/` entran en los paquetes; `package-release.ps1` los
salta y rechaza cualquier `.wav`, `sounds/` o `tools/`. `build-project.bat` ya
no copia los `.wav` junto al exe. El arpa y su script siguen en la fuente del
autor: para devolverlos basta poner la política a `true`, volver a copiar
`assets/sounds` en el build y quitar la exclusión del empaquetador.

**Encontrado al cerrar.** Una captura del lienzo libre de 32×32 en pixel art
salía suavizada aunque `setPreviewFiltering` pedía filtro cercano: el backend
OpenGL3 de ImGui de esta versión enlaza su propio sampler lineal y pisa el
filtro de la textura. El lienzo pide ahora
`GetPlatformIO().DrawCallback_SetSamplerNearest` alrededor de la imagen y
vuelve al lineal después; la captura nueva sale con píxeles nítidos. También:
«1 statement became a block» en singular, y dos ayudas y una misión que aún
decían «Meter en la hoja general» dicen «Pasar a la hoja general».

**Scripts de prueba.** En esta máquina `NoDefaultCurrentDirectoryInExePath`
está activo: `test.bat` no encontraba `NoteLabCoreTests.exe` y ahora lo llama
por su ruta. En Windows PowerShell 5.1 `ExitCode` llega vacío si no se abrió el
`Handle` del proceso mientras corría: `test-ui.ps1` daba por fallida una prueba
que pasaba, y `capture-demo.ps1` no habría visto un fallo. Los dos lo abren ya.
El total de la interfaz se cuenta en los registros.

**Pruebas sobre el exe final** (tras el último cambio de código): núcleo 714,
flujo con copias aisladas de Codename 1.0.1, Psych 1.0.4 y V-Slice 0.8.6 2.148,
interfaz 135 más la guarda; 2.997 sin fallos. La copia de V-Slice da 169 charts
(la 1.0.2 contó 173); se anota lo que salió. `core/`, `support/` y
`third_party/` son idénticos a la 1.0.2.

**Carpeta con acentos.** El Developer compilado en una carpeta «José» dio 65
fallos en el núcleo y la prueba abortó (0xC0000409). La causa estaba en la
prueba, no en la app: `writeSheet` hacía `fs::path(x.u8string() + ".png")`, que
en Windows lee el UTF-8 como ANSI, y las hojas acababan en una carpeta
«JosÃ©». Con `fs::u8path` pasan las 714 con «José» en la ruta temporal. La app
abre el mismo fixture desde «José» y «ñandú» igual que desde una ruta ASCII.
La 1.0.2 no lo vio porque compiló su Developer en una ruta sin acentos.

**Ranking con texto oculto.** Después de cerrar, el autor pidió «oculta el
ranking with text». `kTextRankingVisible` (ya existía y solo cubría la
importación de fuentes) cubre ahora también Archivo → Crear HUD de ranking ·
Texto, los dos botones «Ranking con texto» del inspector y su tema de la
ayuda; el ranking con imágenes no cambia. El código sigue, apagado. Se rehízo
la entrega 1.0.3 con este exe; la anterior quedó en `.delivery-backups`.

**Pendiente.** Con todo el kit dentro de una carpeta «José» aún fallan 13
comprobaciones de exportación (releer y verificar el export en esa ruta). Es
código sin cambios desde la 1.0.2; el autor pidió cerrar la 1.0.3 sin mirarlo.

**Documentación.** Versión 1.0.3 en política, recursos del exe, empaquetador,
mapa de sincronización, README, inicio rápido y guía. La guía llega a 41
capítulos («Empezar a dibujar y lienzo libre») y actualiza los de notas
custom, Bloques, tutoriales y Crear HUD. 32 capturas nuevas con el exe final:
las 18 de `capture-demo.ps1` con el juego base de V-Slice y 14 del editor,
la nota «Lista», Bloques y Crear HUD con el juego base de Psych.

## 2026-10-05 — comodidad y flujo (1.0.3)

Publicada la 1.0.2, el autor siguió probando «para ver qué tan cómodo es» y
pidió, en ráfaga:

- un paint más cómodo para una sola nota;
- un botón visible para pasar a código;
- guardar la nota en el catálogo y no solo abrir Bloques;
- «Guardar» y «Guardar como» en Bloques;
- sonidos mejores y que suenen solo en lo del tutorial;
- una creación que no mande directo al paint;
- mejores controles en el paint;
- receptores automáticos según el estilo;
- mejores botones en el creador de notas y mejor flujo en Crear HUD;
- una pantalla de inicio en el paint (plantilla con huecos, lienzo libre de la
  medida que se quiera y luego «pasarlo al general», lienzos de cada pieza);
- otros sonidos, «que se notan muy generados», entregados en `.wav`;
- capturas para el visto bueno;
- de los cuatro juegos de sonidos que se le mandaron, «deja solo el arpa».

**Paint.** `SheetRow::width` permite huecos no cuadrados: `freeLayout` es un
lienzo libre (8–512 px). `SpriteDoc` guarda `freeW/freeH/pixel` (también en el
`.nlsprite`). `drawSpriteStart`/`spriteBegin` hacen la pantalla de inicio.
`spriteSendToSheet` sustituye a `spriteInsertSingle`: lleva lo dibujado a
cualquier pieza y hueco con `fitIntoCell`, que escala sin deformar y, en pixel
art, por enteros y sin suavizar.

Controles nuevos:

- clic derecho pinta con el color de contorno;
- Alt + clic coge un color con cualquier herramienta;
- `[ ]` cambian el tamaño del pincel (con Shift, la dureza);
- `0`, `1` y `F` para la vista;
- espejo arriba-abajo o izquierda-derecha;
- `stampPixelBrush` pinta sin suavizado y hay rejilla de píxeles de cerca;
- `spritePixelize` quita el suavizado de presets y figuras;
- la textura del lienzo se ve nítida de cerca (`setPreviewFiltering`).

Archivo, cargar y guardar van a un menú. En el aspecto de una nota, el panel
derecho enseña «En el juego» (la nota con su tramo y su final) sin
desplazarse.

**Creador de notas.**

- Tarjetas con icono para elegir qué hace la nota y su aspecto.
- «Crear nota» la deja en el Catálogo y pasa a «Lista», con los siguientes
  pasos: sus bloques o código, su aspecto y su bot.
- Abierto desde Bloques, va directo a los bloques (`fromBlocks`).
- «Tus notas» lleva botones por fila.

**Bloques.** Segunda fila (`drawNoteActions`) con:

- Guardar (`saveProject`);
- Guardar como: otra nota (`copyNoteAs`), `.nlblocks`, código o proyecto;
- Pasar a código: convertir con `keepCustomSource` y Ctrl+Z, o hacer una copia;
- Pasar a bloques, en las notas de solo código;
- Aspecto y Bot.

**Crear HUD.**

- Va por pasos (`createHud.step`): Base, Colores, Detalles y Piezas. Las
  marcas del tutorial de un paso oculto van en su botón.
- Las piezas son tarjetas con su miniatura: lo dibujado se sube como textura
  y lo del estilo sale de `partThumb`.
- «Dibujadas por mí» no abre nada solo; la tarjeta abre el editor, que
  pregunta cómo empezar si la pieza está vacía.

**Receptores.** `selectStyle` vuelve a los del estilo si el elegido los trae.

**Sonidos.** Por qué sonaban fuera de lugar:

- `updateTutorial` tocaba la campana con cualquier misión cumplida de una ruta
  abierta;
- las franjas de las zonas sonaban al abrir su ventana.

Ahora suena solo la misión que se está enseñando, y las franjas no suenan.

Los sonidos se sintetizan con numpy en `tools/make_tutorial_sounds.py`, con
semilla fija. La primera versión fue kalimba y marimba, con un acorde de piano
eléctrico y una sala por convolución. Al pedir «otros sonidos» se probaron
cuatro juegos: arpa, caja de música, gotas con glockenspiel y kalimba. El
autor dejó el arpa: cuerdas punteadas con Karplus-Strong ampliado (retardo de
un periodo, pérdidas y paso todo de afinación; medido: 391,8 / 587,4 Hz para
sol y re). El script solo hace el arpa y reproduce byte a byte los archivos
que se aprobaron (el ruido se saca en el mismo orden).

Luego pidió «cambia ligeramente el de completado». El glissando recto de siete
notas, con el bajo desde el principio, pasó a ser una subida de cinco notas
que se acelera un poco y cae en sol y do agudos casi juntos, con el bajo al
llegar. El final es algo menos brillante. Ese sonido saca ahora otro ruido, así
que el script fija el estado del generador al empezar la oferta (`OFFER_RNG`):
misión y oferta siguen saliendo idénticos. El respaldo sintetizado del exe
cambió igual.

En la primera versión los espectrogramas enseñaron chasquidos al final de
cada nota; se arreglaron con un final en coseno. `loadTutorialCue` carga
`sounds/*.wav` con `SDL_LoadWAV` y `SDL_ConvertAudioSamples`. Sin el archivo
suena el respaldo sintetizado, que ahora también es el arpa (`pluck` en
`BlockCanvas.cpp`). `build-project.bat` copia la carpeta junto al exe.

Pruebas: las 7 de interfaz pasan; `notes` llega a 28 comprobaciones. Capturas
52–63 en `capturas-tutorial-notelab`, pendientes del visto bueno del autor.

## 2026-10-05 — cierre de la 1.0.2 y repetición de canciones

Se reprodujo el fallo reportado: si el audio terminaba antes del margen final
del chart, volvía al inicio sin limpiar los aciertos. Desaparecían las notas
y las cabezas de sostenidos, pero seguía dibujándose su cuerpo. La reproducción
automática reinicia ahora audio y estado del chart juntos; el modo manual
conserva sus resultados y una búsqueda explícita no se confunde con una vuelta.
Las regresiones cubren tres vueltas en ambos sentidos de scroll, taps,
sostenidos, pausa/reanudación al final, búsqueda y final manual.

También se avisa al exportar una línea de código vacía o perteneciente a otro
motor: no se presenta un comentario como si fuera una acción ejecutable.
`test.bat` aísla TEMP/TMP dentro del kit. La primera matriz restringida falló
por acceso a archivos temporales; los resultados finales proceden de las
repeticiones con permisos de proceso normales y datos aislados en `.qa`.

Resultados nuevos: núcleo 714, flujo con las tres instalaciones 2.148,
interfaz 129, todos sin fallos; además pasa la guarda de programa ausente.
Total: 2.991 comprobaciones más esa guarda. La matriz antigua de 2.275 no se
reutiliza como resultado nuevo: las instalaciones actuales contienen otros
estilos y charts. El ensayo de bucles está incluido en el total, no se duplica.

Versión, recursos del exe, empaquetador, README y guía actualizados a 1.0.2.
La guía MD llega a 40 capítulos, con editor de sprites, piezas dibujadas,
PNG final, `.nlsprite`, asistente de nota, tutoriales, script→bloques y filtros
de sonido. Se revisaron las capturas 29–44 y se generaron 18 capturas actuales
con el juego base. No se atribuye esa revisión al visto bueno individual del
autor; no se incluyen muestras IA ni los mods privados de prueba.

El kit sigue siendo solo Note Lab: 14 módulos propios y 87 fuentes/cabeceras,
con el soporte que necesita. Las pruebas nativas anteriores de Psych 1.0.4,
Codename 1.0.1 y V-Slice 0.8.6 se documentan por separado de la nueva matriz;
no se afirma haber repetido el gameplay durante el empaquetado.

El Developer extraído en una carpeta reubicada compila de cero y conserva la
versión 1.0.2, sin necesitar otro checkout de FML. El ZIP público extraído
arranca y carga el inspector del juego base. Se comprobaron 347 hashes de
archivos y los dos ZIP de validación. La entrega final conserva el mismo
código y exe probado; sus firmas y última comprobación quedan junto a los
ZIP en `RELEASE_VERIFICATION.json`. Se conserva la entrega 1.0.1.

## 2026-10-05 — sostenidos de un color arreglados y prueba en los tres motores

El autor pidió arreglar el fallo conocido (Crear HUD pintaba los cuatro tramos
de la plantilla RGB de Psych del color de la izquierda) y dio permiso para
probar en cada motor «que el hud funcione, las notas funcionen y todo eso».

Causa: en `NOTE_assets.xml` de Psych 0.7 y 1.0 los cuatro `hold piece` y los
cuatro `hold end` son la misma región (x=1102/1051, y=444), y `chip` comparte
también los aciertos 0002-0003 de las cuatro direcciones.
`paintStyleImages` pintaba cada región una vez, para la primera pieza. Ahora
cada «aspecto» (Paint + paleta horneada, `lookKey`) que pide una región ya
pintada distinto recibe una copia en estantes debajo de la hoja, y sus
fotogramas se mueven en el atlas (`movedSparrow`, con pugixml).
Reglas:

- Las piezas que no cambian también reclaman su región, para que nadie les
  pinte encima.
- En rejilla o tira no se puede mover una celda, así que manda la primera
  pieza.

`PaintedImage`/`PaintedSheet` llevan `atlas` + `atlasText`. La vista en vivo
mete el atlas nuevo en `AtlasStore::putSparrow`, y `commitPainted` lo guarda y
lo monta junto al PNG. Las pruebas nuevas cubren:

- los cuatro tramos con su copia y el color de su carril;
- que lo que no cambia conserve su región.

La exportación ya separaba por paleta (`AtlasOut`, clave con la paleta); el
fallo estaba solo al pintar.

Prueba en el juego (computer-use sobre la ventana del juego, con permiso; Note
Lab sigue sin tocarse así). Para cada motor, un mod de prueba con:

- un HUD de Crear HUD: neón sobre la plantilla RGB en Psych, piezas dibujadas
  («HUD completo») en Codename y V-Slice;
- un tipo «NL Prueba» con bloques («Hey!», flash y sacudida) y aspecto
  dibujado;
- Bopeebo Hard con el 30 % de las notas de ese tipo;
- un script de bot solo de prueba.

Resultados:

- **Psych 1.0.4:** como skin de canción (`arrowSkin` + `disableNoteRGB`; como
  skin del mod, Psych lo recolorearía, y el export ya lo avisa). Cada sostenido
  sale de su color, con salpicaduras y la nota custom (el autor vio el flash y
  la sacudida).
- **Codename 1.0.1:** HUD dibujado, sostenidos, salpicadura de estrella y flash.
  Con `playerStrums.cpu = true`, Codename trata esos aciertos como del rival
  (`PlayState.hx:1991`: sin salpicadura ni `event.player`). La prueba lo
  corrige en `onDadHit`; no es un fallo de Note Lab.
- **V-Slice 0.8.6:** salió un fallo real. El receptor de arriba se quedaba
  encendido en los dos lados. `playConfirm` pone `active = isAnimationDynamic
  ('confirm')`, que es `numFrames > 1` (`StrumlineNote.hx:143`,
  `FunkinSprite.hx:398`); con un acierto dibujado de un fotograma el receptor
  queda inactivo y su temporizador no lo devuelve a reposo. Ahora
  `addVSlicePart` repite el fotograma hasta dos para el acierto (comparten
  región), con su prueba. Vuelto a jugar: arreglado.

Para preparar estos paquetes sin tocar la interfaz:

- `--save-chart=<archivo>`;
- con `--commit-create`, `--save-project`/`--export-to` esperan a la
  confirmación.

Los mods de prueba se quitaron de los tres juegos y quedan en
`capturas-tutorial-notelab/mods-de-prueba`. Capturas 45-51. Núcleo: 708 ok.

## 2026-10-05 — piezas dibujadas en Crear HUD, la hoja completa y código a bloques

Pedidos del autor en una tanda: meter el editor de imágenes en Crear HUD y, con
él, «todo lo relacionado a la nota, splash, hold», con línea de tiempo, el
archivo con todos los assets, la hoja completa con huecos, selección por rango,
presets en otra capa, copiar a un hueco libre, pestañas y dibujos sueltos,
línea con Shift, controles para hojas grandes, botones con iconos, receptores
editables, ver el PNG final, paleta propia, archivos propios del paint;
receptores de otro estilo en la vista previa; separar canciones de efectos en
Recursos; pasar el código de una nota a bloques y guardar programa y código.

Núcleo (`core/NoteCreate.*`): `DrawnPiece` (nota, receptor en reposo, pulsado y
al acertar, tramo, final, salpicadura) con fotogramas; `drawnPieceSheets` para
la vista en vivo (rejillas), `drawnPieceAtlas` + `bindDrawnAtlas` para crear
(un atlas Sparrow con los nombres del juego base, `basePieceName`). El tinte de
lo dibujado va por degradado de luz (oscuro, color, casi blanco), no por tono:
por tono, un dibujo de colores puros (saturación 1) no tomaba el pastel. El
receptor gris también va por luz. `styleSheetAtlas` saca cualquier estilo a un
solo PNG + XML; `stylePieceFrames` carga piezas de un estilo en el editor. La
receta guarda cada pieza (PNG en fila, sin teñir), sus FPS y `sprite` (el
`.nlsprite`); `NoteProject` lo lee y escribe. Exportado y releído sin errores
en los tres motores (prueba nueva).

Editor (`src/SpriteShapes.hpp`, `src/SpriteEditor.hpp`): la hoja con una fila
por pieza y 6 huecos; el de un tipo solo lleva nota y sostenido. Recompone solo
el rango que cambia; deshacer guarda solo la capa del trazo. Pestañas con
intercambio de estado (la de delante vive en los campos del editor). El
`.nlsprite` es un ZIP con `sprite.json` y un PNG por capa (`writeSpriteFile` /
`readSpriteFile` en `core/NoteExport.cpp`, que ya tenía miniz); abrir uno nunca
pisa una hoja con trabajo: va como otra pestaña.

Código a bloques (`core/NoteCode.cpp`, `importNoteScript`): por líneas, con las
funciones de evento de cada motor (también los métodos de una clase NoteKind
con llaves en otra línea) y sin las guardas del tipo, que ya pone el evento. Lo
que no se reconoce va como el bloque nuevo `code.line` (tal cual en su motor,
comentado en los otros), en su sitio, así el código generado conserva el
orden. Probado con un script real (Bullet Note de un mod de Psych): 1 bloque,
19 líneas como código, y avisa de `onUpdate` y de la textura.

Recursos (`soundUseOf`): canción = todo bajo `songs/` o un Inst/Voices; música
= bajo `music/`; el resto, efectos. En el mod de prueba: 83 efectos, 105 de
canciones y 14 de música, antes mezclados.

La guía de bloques montaba sus pasos sobre la nota abierta aunque tuviera
bloques del autor; ahora no se mete en ella (`tutorialForeignWork`) y pide una
nueva, sin borrar nada.

Problema conocido, anterior: en una plantilla RGB de Psych 0.7 los cuatro
tramos de sostenido comparten región del atlas y Crear HUD los pinta todos con
el color de ←. Dibujar el sostenido lo evita; arreglarlo pide duplicar regiones
al pintar (`paintStyleImages`).

Pruebas: núcleo 705 ok; interfaz, las 7 (`notes` con el flujo nuevo, `tutorial`
con la zona nueva).

## 2026-10-04 — flujo de notas custom, editor de sprites y sonidos del tutorial

Pedidos del autor, en ráfaga: crear la nota desde el Catálogo con todo en un
sitio (nombre, código, bot, sprites, sonido) y de ahí a los bloques; que
Bloques no deje hacer nada sin una nota elegida o sin elegir «solo código»
(escribirlo, guardarlo y usarlo después); el tutorial con ese flujo y sin
obligar a un sprite propio; sonidos para los tutoriales «sencillos pero sin
notar que es IA»; y un editor para dibujar el sprite «con capas, figuras,
pinceles, tamaños... como Krita».

Asistente (`src/NewNote.hpp`): crea el tipo en el proyecto con lo elegido. «Solo
código» reutiliza el archivo propio que ya existía (`keepCustomSource`, bloque
`code.file`) con una plantilla que usa los mismos eventos que escriben los
bloques en cada motor; su vista de código cambia «Aplicar a bloques» por
«Guardar código». Las plantillas pasan la revisión de sintaxis en los tres
motores (probado con Psych 0.7, el juego base de Codename y V-Slice). El «+»
antiguo de Bloques abría un desplegable solo con el nombre: ahora abre el
asistente. Bloques ya no escoge sola la primera nota.

Editor de sprites (`src/SpriteShapes.hpp`, núcleo sin interfaz, y
`src/SpriteEditor.hpp`): capas de imagen de 160 px, pincel y goma con dureza,
figuras con relleno y contorno (3 × 3 muestras por píxel), bote de pintura,
cuentagotas y deshacer. Lo dibujado se guarda con lo creado y va como el
aspecto del tipo por el mismo camino que unas imágenes propias
(`commitTypeLookFrames`, sacado de `typeLookFromImport`). Lo que no se guarda
aún: las capas para seguir editándolas tras cerrar el proyecto (sí durante la
sesión). Al revisar las capturas, el corazón se salía del lienzo por abajo: el
centrado de su curva estaba mal; corregido.

Sonidos del tutorial: sintetizados con las mismas funciones que los del editor
(`composeTutorial`), por un permiso aparte (`tutorialAudioPlayback`); los del
editor siguen apagados. `--write-sounds` los escribe en WAV: misión 0,26 de
pico, final 0,32 y aviso 0,18, por debajo de los del editor.

Repetir un tutorial ya no da por hechas solas las misiones cuyo estado ya se
cumplía (`preMet`). «Distribuir» sin chart deja elegir la canción ahí.

Pruebas: núcleo 689 sin fallos; las siete de interfaz pasan, `notes` (10) y
`tutorial` (24) también con el fixture de `.qa`. Build en
`FunkinNoteLab-1.0\NoteLab-1.0.2-dev-b` (la otra carpeta estaba en uso).

## 2026-10-04 — tutorial: lo que vio el autor al probarlo

El autor probó la build y apuntó: la guía general se cortaba al llegar a
Bloques porque saltaba la de bloques; un clic quitaba el foco y no volvía;
se pedía «arrastrar la categoría» y no un bloque; el vídeo enseñaba bloques que
no existen («quitar vida»); la paleta soltaba todos los bloques de golpe; en
«Juega tú» había que seguir jugando; el panel derecho no iba a la acción; las
preguntas salían en mitad de la pantalla; una guía terminada no se iba.

Cambios: con la ruta general en marcha, las demás esperan (`tutorialGeneralActive`)
y la general sigue dentro de Bloques con una misión nueva y, en Exportar, con su
misión en la franja. La paleta acepta un bundle (`CanvasState::onlyBlocks`): el
tutorial pone ahí los bloques de la misión y la paleta quita la cuadrícula de
categorías; con ella, en 880 px de alto solo quedaban unos 70 px de lista y el
bloque pedido no se veía. Lo señalado sale del estado del tipo abierto: «+» si
falta un tipo, el evento, la acción o el hueco de su número; la paleta se
desplaza sola al bloque (`jumpCategory`). El foco tiene sombra solo hasta el
primer clic; el borde y el mensaje se quedan. Avisos: tarjeta abajo a la derecha
y franjas de una línea. Cada ruta terminada se cierra a los 3,5 s.

`--ui-test=tutorial` pasa de 15 a 22 comprobaciones (también con el fixture de
`.qa`). Núcleo 689 sin fallos. La prueba `resources` falló una vez en 17
ejecuciones justo tras la build completa y no se repitió en 16 más, ni con esta
build ni con la 1.0.1 original: queda anotada como intermitente.

## 2026-10-04 — bugs de la lista de la v1

Esc: once ventanas se cerraban con Esc sin mirar nada más. Escribiendo en un
campo, Esc cancela el campo y en el mismo fotograma cerraba la ventana entera
(en el Creador custom, con todo lo importado). Exportar lo intentaba evitar con
`!IsAnyItemActive()`, que no sirve: el campo se suelta antes de esa comprobación.
`escapeClosesWindow()` mira también el fotograma anterior y que la ventana sea la
de arriba del todo (sin un desplegable, selector de color ni pregunta encima).

El Creador custom pregunta antes de descartar lo que no se aplicó; mientras
haya trabajo sin aplicar no tiene aspa, porque ImGui cierra un modal por el aspa
sin dejar preguntar. Los mensajes del núcleo (compartido con FML) llegan en
inglés: la interfaz los traduce por su texto sin tocar el núcleo.

El ranking con texto solo se abría soltando una fuente; la ayuda ya decía
«Ranking con texto… bajo el HUD del inspector» y ese botón no existía. Vuelve
al inspector y al menú Archivo.

Las pruebas de interfaz leían las preferencias: con `blocksCodeRatio` en 0,15 el
panel de código queda en su mínimo (300 px, legible) y la prueba `code` dejaba
de ver comentarios fuera del ancho. No era un fallo del editor sino de la
prueba. Ahora `--ui-test` no lee ni guarda preferencias y `--capture` no las
guarda. Nueva `--ui-test=tutorial` (15 comprobaciones). Resultado: 689 del núcleo
y las seis pruebas de interfaz sin fallos, con la build completa de
`build-project.bat`. Build de prueba en `FunkinNoteLab-1.0\NoteLab-1.0.2-dev`.

## 2026-10-04 — tutorial de juego, guía de bloques y tutoriales por zona

Pedido del autor: un tutorial como el de un juego, que se pueda saltar sin
perderlo, con el mensaje junto a lo que se usa y el resto oscurecido; una guía
integrada en los bloques con misiones, un mini vídeo de ratón arrastrando y
encajando, y un bundle de tres notas básicas; después, un tutorial por cada
zona importante (no por todas), con una ventana que pregunta antes si hacerlo
o saltarlo y no vuelve a preguntar si se salta, y todos en el menú Tutorial.

Todo está en `src/Tutorial.hpp`; `main.cpp`, `CustomCreator.hpp` y
`ModResources.hpp` solo marcan dónde están los controles y avisan de lo que
solo se ve al hacerlo. Las misiones se detectan con el estado real de la app
(hay un mod, se creó un HUD, se exportó y se verificó…), no con clics simulados.
Zonas con tutorial: Crear HUD de notas, Creador custom y ranking (el ranking con
imágenes es el mismo creador), Recursos del mod y Exportar. Combinar estilos,
el aspecto de un tipo y el ranking con texto quedan fuera, por pedido.

La franja de una zona va dentro de su modal y el foco se pinta en la capa de
delante: una ventana aparte quedaría tapada o sin poder pulsarse. Esc con el
foco puesto lo consume la franja (`SetKeyOwner`) para que no cierre la ventana.
Una ventana ImGui con NoBringToFrontOnFocus nace al fondo de la pila: la capa
del foco general se trae al frente cada fotograma.

Las pruebas de interfaz desactivan preguntas y franjas. Con preferencias
limpias pasan las cinco (blocks, score, code, resources, audition). La prueba
`code` fallaba antes solo porque leía las preferencias reales: con
`blocksCodeRatio` en 0,15 (el mínimo) falla también la 1.0.1 original; con
preferencias limpias pasa. Las pruebas no son herméticas.
Capturas de revisión en `Documents\fnf\capturas-tutorial-notelab`.

## 2026-10-04 — charts de Psych 1.0: cada nota en su lado

Psych 1.0 guarda los charts con `format: "psych_v1"` y carriles absolutos: 0..3
del jugador y 4..7 del rival, sin mirar `mustHitSection`, que solo mueve la
cámara (`PlayState.hx:1355`; `Song.hx:174-180` solo convierte lo que no empieza
por "psych_v1"). Codename hace lo mismo (`Chart.hx:68` → `PsychParser.hx:27-29`).
El lector compartido (`support/formats/LegacyChart.cpp`) aplicaba la regla
clásica a todos: con los 97 charts del juego base que trae Psych 1.0, 15.864 de
33.185 notas quedaban en el lado contrario (comprobado con el lector real); ahora
0. Afectaba a la vista previa, a «Jugar», al filtro de lado del reparto y al bot.

Guardar el reparto sobre un chart `psych_v1` (`core/NoteSongs.cpp`) casaba mal
las notas y se bloqueaba; ahora usa la misma regla y no reescribe un 8+ como si
fuera un tipo empacado. El export a Psych (`support/formats/ChartExchange.cpp`)
escribía carriles clásicos con la etiqueta "psych_v1", que Psych 1.0 y Codename
no convierten; ahora sale sin ella: Psych 0.7 lo lee tal cual y los otros dos lo
convierten solos.

La ida y vuelta del propio lector no lo veía porque lector y escritor compartían
el error: las pruebas nuevas comparan con la regla de cada motor leída en su
fuente. `tests/CoreRegression.cpp`: 689 comprobaciones, 0 fallos. El mismo
arreglo está en FML (`Downloads\fnf`, commit e30b053), del que sale este núcleo.

## 2026-10-04 — audición completa y controles de recursos

Se retira el temporizador que pausaba a los 30 segundos en el visor y en
importación. Ambos usan el mismo controlador y los ajustes del AudioEngine
compartido: Loop apagado por defecto, velocidad, balance estéreo y rango A–B.
La velocidad también afecta al tono. El rango se reinicia al cambiar de recurso;
la audición se libera al cerrar y el tamaño máximo sigue acotado a 32 MB.
Los ajustes son de preview, sin modificar archivos, canción ni scripts exportados.
Los sonidos de interacción del editor de bloques siguen desactivados.
El cierre se detecta por el estado previo de cada diálogo: un popup de recursos
o importación inactivo no libera la audición o imagen del otro. El contenido de
importación tiene scroll propio y deja Confirmar/Cancelar siempre accesibles.

## 2026-10-04 — recursos multimedia, guía y nuevo lateral

Biblioteca de imágenes, vídeos y sonidos disponible fuera y dentro de Bloques.
El usuario señaló que los desplegables sobre el editor estorbaban: se trasladan
al lateral izquierdo con Mods/Recursos y Biblioteca/En uso, búsqueda, filtros
por tipo, tarjetas de archivo y estados encontrado/faltante/desconectado. Importar
y añadir screamer quedan al pie. El cambio no reemplaza la selección ni el
programa; se conservan las acciones de clic derecho. La toolbar se adapta a
ventanas estrechas. Al cerrar una fuente se remapea o invalida el navegador.

Importación propia con inspección y carpeta relativa; modo sólo proyecto por
defecto, copia al mod y creación de carpeta explícitas. No sobrescribe. PNG,
OGG Vorbis y MP4 conectados se incluyen en el paquete con claves de motor y
preflight de ausencia, colisión, ruta y tamaño. Los bloques multimedia controlan
duración, volumen/opacidad, ajuste y cooldown. Vídeo externo en preview y
soporte de script condicionado al motor; no se certifican todas las APIs.

Guía avanzada Markdown con capturas reales del juego base y documentación
de módulos/scripts. Sin ilustraciones IA ni archivos del juego en las entregas.
Las reglas de recursos y export van en el núcleo reutilizable; la disposición
del panel es propia del host. Resultados finales en docs/RELEASE_CHECKS.md.

## 2026-10-04 — build 1.0.1, silencio, identidad y matriz de QA

`BuildPolicy.hpp` separa reproducción general y cues del editor de bloques.
La revisión inicial silenció todo por una interpretación demasiado amplia;
tras la aclaración del usuario se restauran canciones y audición de imports.
Sólo los cues quedan bloqueados, sin checkbox. El volumen se guarda y el cero
heredado de la build silenciada se migra una vez a 80%; después se respeta el
mute elegido. El audio compartido de FML y los exports OGG no se modifican.

El PNG original se conserva byte a byte y queda embebido en el ejecutable,
welcome e icono de ventana. Un script produce el ICO multirresolución sin
generar arte nuevo. README con badges, guía, límites y capturas nativas EN/ES
del juego base; no se distribuyen sus archivos ni fixtures sintéticas.

Cancelar creación desde welcome quita solo la fuente vacía recién creada.
El creador explica recursos incompatibles/ilegibles y sonidos sin nombre.
Abrir fallido/cancelado después de No guardar conserva el indicador de trabajo
pendiente hasta el reemplazo real. Los parsers no se replican en la interfaz.

La matriz detectó carpeta ZIP destino inexistente y reemplazo repetido fallido
en Windows. `NoteExport` crea los padres y publica con sustitución atómica
Unicode. Se repitieron los tests; los supuestos iniciales sobre valores
desconectados y preflight eran del harness, no bugs del programa.

Resultado: 684 checks core + 2.143 workflow/matriz con las tres instalaciones
+ 37 interacciones UI = 2.864, más el guard de programa ausente; cero fallos.
Incluye creación, roles, atlas, palette/presets, comentarios/código, targets,
ZIP, rendering y timing con audio bloqueado.

Las copias aisladas reciben skin, tipo y chart privado. Computer Use resultó
lento/inconsistente y el usuario pidió abandonarlo; la prueba posterior no
lo utiliza. Codename reprodujo Bopeebo hasta 18 s, resolvió textura/36 frames,
21 instancias custom y el bloque log. V-Slice inició NoteKind, reprodujo Bopeebo
con su LoadingState/bot, confirmó cuatro hits custom y capturó el framebuffer.
El observer inicial de Code comparaba el ID incorrecto y usaba Sys sin importar;
se corrigió solo la prueba. El cargador normal de V-Slice funcionó donde el
switch directo no estableció gameplay. Psych tiene observer preparado, pero
no se certifica como jugado. Detalles en `docs/RELEASE_CHECKS.md`.

La entrega 1.0.1 usa una carpeta nueva, conservando 1.0.0. Fixtures, juegos,
capturas sintéticas y registros privados no entran en Public/Developer.
El changelog consolidado de Atlas se entrega aparte.

## 2026-10-03 — preparación de la primera build pública 1.0.0

Continúa la base de creación avanzada, bloques/código, distribución, bot y
guardado de charts desarrollada el 2 de octubre. No se reescriben los parsers
ni exporters en la interfaz. MIT fue confirmada expresamente para Note Lab.

Hecho en esta revisión: inicio sin nota musical, entrada directa a creación,
disposición compacta, composición por grupos, respaldo de receptores, nombres
multikey explícitos, identidad completa de hojas y protección de trabajo al
cerrar/recargar/abrir. Guardado atómico y lectura acotada permanecen en el
núcleo compartido, con regresiones para que FML pueda aprovecharlo.

Las salidas se preparan aparte; no se modifican mods ni se sustituyen builds
anteriores. La entrega dev conserva la estructura del núcleo y una lista
verificable de archivos, sin el proyecto entero de FML ni material de mods.

La primera repetición de las nuevas pruebas de preview tuvo tres fallos:
el test reutilizaba una lista de dibujo que la API deliberadamente amplía.
Se corrigió el test para iniciar una lista por fotograma, como hace la app;
no se cambió el contrato del renderer para ocultar el fallo.

Resultados finales y límites de verificación: `docs/RELEASE_CHECKS.md`.
La primera prueba de código fue lanzada sin sus presets requeridos y el
harness intentó leer un buffer aún inexistente. Se añadió un rechazo explícito
de ese caso y un script que prepara fixtures/presets reproducibles. La prueba
de bloques también necesitaba su preset inicial `hey`; preparada correctamente
pasó los 21 pasos. La herramienta de captura de Windows no pudo leer el cuadro
del error por un timeout de aprobación; se continuó con las pruebas nativas.
La revisión de capturas detectó botones del Inspector fuera del ancho de
su panel. Se aplicó el ajuste de líneas existente también a creación, variantes,
restauración y ranking, sin introducir un segundo sistema de layout.
El kit reducido se recompiló fuera del checkout original: 684 checks del
núcleo, 25 de workflow y 37 de UI, más el guard de programa ausente. Una primera
compilación del ajuste de botones detectó una declaración adelantada necesaria;
se corrigió y se repitió la build independiente completa antes de entregar.
No se debe presentar relectura de paquetes como prueba de ejecución dentro
de los tres motores. Los proyectos aún dependen de sus fuentes/cache; no se
anuncia portabilidad ni un compositor completo de toda la HUD exportada.

## Base previa · 2026-10-02

Creación avanzada, recetas reeditables, código con colores/comentarios,
65 bloques, reparto de cuotas, bot por tipo/lado y guardado nativo con backups.
Última revisión previa documentó 655 checks del núcleo, 39 del flujo custom
y 41 del flujo anterior, sin fallos. Son antecedentes, no resultados de esta
compilación. Las pruebas públicas se vuelven a ejecutar contra el kit actual.

## 2026-10-04 — Developer exclusivo de Note Lab

La entrega dev se reorganiza como repositorio independiente: interfaz en
`src/`, 13 módulos de notas en `core/` y únicamente dependencias necesarias
en `support/`. No incluye las otras herramientas de FML. Se conserva el mapa
de rutas y hashes originales en `FML_SYNC_MAP.json` para revisar y trasladar
mejoras al núcleo compartido sin sustituir la estructura de la suite.
El binario público no cambia con esta reorganización.

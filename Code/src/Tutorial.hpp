#pragma once

// Tutorial de Note Lab, como el de un juego (pedidos del autor, 4 oct 2026).
// Rutas de misiones que se marcan solas cuando la persona hace la tarea en la
// app de verdad; no es una demostracion aparte:
//   - la general, en una ventana flotante: abrir, mirar, jugar, crear y publicar;
//   - la de bloques, integrada en el editor de bloques: pide un bundle de tres
//     notas basicas (dano, curacion y suerte) y exportarlo, con un mini video
//     de un raton arrastrando y encajando bloques;
//   - una por zona importante (Crear HUD de notas, Creador custom y ranking,
//     Recursos del mod y Exportar), en una franja arriba de su ventana.
// Antes de cada una, una ventana pregunta si hacerla o saltarla; saltarla no
// vuelve a preguntar y todas siguen en el menu «Tutorial» de arriba.
// Modo foco: oscurece todo menos el control que usa la mision y pone el
// mensaje a su lado; un clic o Esc lo quitan y «Muestrame» lo vuelve a poner.
// Nada de aqui escribe en el mod ni en el proyecto: solo mira el estado de
// NoteLabApp y guarda el progreso en las preferencias, por la clave de cada
// mision y de cada zona.

struct TutorialMission {
    const char* key;          // estable: es lo que se guarda en las preferencias
    int track;                // 0 general, 1 bloques, 2.. una zona (kTutorialAreas)
    int chapter;
    const char* glyph;
    const char* titleEn;
    const char* titleEs;
    const char* goalEn;
    const char* goalEs;
    const char* stepsEn;      // pasos separados por '\n'
    const char* stepsEs;
    const char* tipEn;
    const char* tipEs;
    const char* calloutEn;    // el mensaje junto al control, en modo foco
    const char* calloutEs;
    const char* targets;      // controles que se iluminan, separados por '|': el primero que este en pantalla
    int panel;                // sin control: -1 nada, 0 mods, 1 centro, 2 inspector
    int demo;                 // mini video: -1 ninguno, 0 evento, 1 quitar vida, 2 «si», 3 valor en el «si», 4 dar vida
};

constexpr int kTutorialTracks = 7;
constexpr int kTrackGeneral = 0, kTrackBlocks = 1, kTrackHud = 2, kTrackCustom = 3, kTrackResources = 4, kTrackExport = 5, kTrackSprite = 6;
constexpr int kTutorialChapters = 4;
const char* const kTutorialChapterEn[kTutorialTracks][kTutorialChapters] = {
    {"First steps", "Let's play", "Create", "Ship it"},
    {"Note 1 · Damage", "Note 2 · Healing", "Note 3 · Luck", "Ship the bundle"},
    {"Create note HUD", "", "", ""}, {"Custom creator", "", "", ""}, {"Mod resources", "", "", ""}, {"Export", "", "", ""},
    {"Sprite editor", "", "", ""}};
const char* const kTutorialChapterEs[kTutorialTracks][kTutorialChapters] = {
    {"Primeros pasos", "A jugar", "Crea", "Publícalo"},
    {"Nota 1 · Daño", "Nota 2 · Curación", "Nota 3 · Suerte", "Publica el bundle"},
    {"Crear HUD de notas", "", "", ""}, {"Creador custom", "", "", ""}, {"Recursos del mod", "", "", ""}, {"Exportar", "", "", ""},
    {"Editor de sprites", "", "", ""}};

// Cada tutorial del menu: la ruta general, la guia de bloques y las zonas
// importantes. Lo que se pregunta antes de empezarlo sale de aqui.
struct TutorialArea {
    const char* key;          // estable: es lo que se guarda en las preferencias
    int track;
    const char* glyph;
    const char* nameEn;
    const char* nameEs;
    const char* introEn;
    const char* introEs;
};

const TutorialArea kTutorialAreas[kTutorialTracks] = {
    {"general", kTrackGeneral, ui::icon::Star, "First steps", "Primeros pasos",
     "Open a mod, play its notes, create your own and export them. Each mission ticks itself while you use the program.",
     "Abre un mod, juega sus notas, crea las tuyas y expórtalas. Cada misión se marca sola mientras usas el programa."},
    {"blocks", kTrackBlocks, ui::icon::Puzzle, "Blocks guide", "Guía de bloques",
     "Build a bundle of three basic notes by snapping blocks, like in Scratch: damage, healing and luck.",
     "Monta un bundle de tres notas básicas encajando bloques, como en Scratch: daño, curación y suerte."},
    {"hud", kTrackHud, ui::icon::Brush, "Create note HUD", "Crear HUD de notas",
     "Make your own arrows from a style of the mod: its shapes, your colors. The mod is never touched.",
     "Haz tus propias flechas a partir de un estilo del mod: sus formas, tus colores. El mod no se toca."},
    {"custom", kTrackCustom, ui::icon::Layers, "Custom creator & ranking", "Creador custom y ranking",
     "Bring your own images, sheets and sounds and tell Note Lab what each one is: notes, receptors, ranking or countdown.",
     "Trae tus imágenes, hojas y sonidos y dile a Note Lab qué es cada uno: notas, receptores, ranking o cuenta atrás."},
    {"resources", kTrackResources, ui::icon::FolderOpen, "Mod resources", "Recursos del mod",
     "Find the mod's images, videos and sounds, look at them, listen to them and bring your own.",
     "Encuentra las imágenes, vídeos y sonidos del mod, míralos, escúchalos y trae los tuyos."},
    {"export", kTrackExport, ui::icon::Zip, "Export", "Exportar",
     "Get your work out for Codename, Psych or V-Slice, as a folder or a ZIP, with its install guide.",
     "Saca tu trabajo para Codename, Psych o V-Slice, en carpeta o ZIP, con su guía de instalación."},
    {"sprite", kTrackSprite, ui::icon::Brush, "Sprite editor", "Editor de sprites",
     "Draw your notes, receptors, holds and splashes like in a paint program: tools, presets, frames and loose drawings.",
     "Dibuja tus notas, receptores, sostenidos y salpicaduras como en un programa de dibujo: herramientas, presets, fotogramas y dibujos sueltos."},
};

const TutorialMission kTutorialMissions[] = {
    // ------------------------------------------------------------ general --
    {"open-mod", 0, 0, ui::icon::FolderOpen, "Open a mod", "Abre un mod",
     "Open the folder or ZIP of a Codename, Psych or V-Slice mod.",
     "Abre la carpeta o el ZIP de un mod de Codename, Psych o V-Slice.",
     "Press «Open mod folder…» or «Open ZIP…».\nOr drop the folder or the ZIP on the window.\nThe engine and the base game are detected by themselves.",
     "Pulsa «Abrir carpeta de mod…» o «Abrir ZIP…».\nO suelta la carpeta o el ZIP sobre la ventana.\nEl motor y el juego base se detectan solos.",
     "You can keep up to four mods open at once.", "Puedes tener hasta cuatro mods abiertos a la vez.",
     "Start here: open a mod's folder.", "Empieza aquí: abre la carpeta de un mod.", "welcome-open", 1, -1},
    {"pick-style", 0, 0, ui::icon::Palette, "Pick a note style", "Elige un estilo de notas",
     "Choose the arrows you are going to look at.", "Elige las flechas que vas a mirar.",
     "In the left panel, expand your mod.\nClick a note style.\nRed and amber numbers are errors and warnings: hover them.",
     "En el panel de la izquierda, despliega tu mod.\nHaz clic en un estilo de notas.\nLas cifras rojas y ámbar son errores y avisos: pasa el ratón por encima.",
     "Right-click a style for more actions.", "Clic derecho en un estilo para ver más acciones.",
     "Choose one of your mod's note styles.", "Elige uno de los estilos de notas de tu mod.", "", 0, -1},
    {"song", 0, 0, ui::icon::Music, "Load a song", "Pon una canción",
     "Watch the notes with a chart from the mod and its music.", "Mira las notas con un chart del mod y su música.",
     "Go to the «Preview» tab.\nOpen the song list and pick one with its difficulty.\nIts speed, BPM and music are loaded with it.",
     "Ve a la pestaña «Vista previa».\nAbre la lista de canciones y elige una con su dificultad.\nSe cargan su velocidad, su BPM y su música.",
     "Without a song a test pattern plays: it also works for looking.", "Sin canción suena un patrón de prueba: también sirve para mirar.",
     "Open this list and pick a song.", "Abre esta lista y elige una canción.", "song-combo", 1, -1},
    {"play", 0, 1, ui::icon::Game, "Play it yourself", "Juega tú",
     "Hit three notes with the keyboard, like in the game.", "Acierta tres notas con el teclado, como en el juego.",
     "Turn on «Play» in the preview bar.\nHit three notes with D F J K or the arrow keys.\nThen the preview plays by itself again and the tutorial goes on.",
     "Activa «Jugar» en la barra de la vista previa.\nAcierta tres notas con D F J K o con las flechas.\nLuego la vista previa vuelve a jugar sola y el tutorial sigue.",
     "Change the keys in Preview → Keys….", "Las teclas se cambian en Vista previa → Teclas….",
     "Turn on «Play» and hit with D F J K.", "Activa «Jugar» y toca con D F J K.", "play-toggle", 1, -1},
    {"tweak", 0, 1, ui::icon::Edit, "Tweak a piece", "Retoca una pieza",
     "Change something on an arrow and watch it live.", "Cambia algo de una flecha y míralo en vivo.",
     "In the inspector (right), click a piece: note, hold, receptor…\nChange its scale, its FPS or its offsets.\nCtrl+Z undoes it. The original mod is never touched.",
     "En el inspector (a la derecha), haz clic en una pieza: nota, sostenido, receptor…\nCambia su escala, sus FPS o sus offsets.\nCtrl+Z lo deshace. El mod original nunca se toca.",
     "Ctrl+click picks several HUD images to change them together.", "Ctrl+clic elige varias imágenes del HUD para cambiarlas a la vez.",
     "Click a piece and change its scale or its FPS.", "Haz clic en una pieza y cambia su escala o sus FPS.", "piece-edit|parts-grid", 2, -1},
    {"variant", 0, 2, ui::icon::Add, "Make your variant", "Crea tu variante",
     "Make your own version of a style without touching the mod's.", "Haz tu propia versión de un estilo sin tocar la del mod.",
     "With a style chosen, press «New variant» in the inspector.\nName it and change whatever you like.\nTo paint brand-new arrows, use «Create note HUD».",
     "Con un estilo elegido, pulsa «Nueva variante» en el inspector.\nPonle nombre y cámbiale lo que quieras.\nPara pintar flechas nuevas, usa «Crear HUD de notas».",
     "Your variant travels in the project and exports like any style.", "Tu variante viaja en el proyecto y se exporta como cualquier estilo.",
     "Your own copy of this style, without touching the mod.", "Tu propia copia de este estilo, sin tocar el mod.", "new-variant", 2, -1},
    {"custom-notes", 0, 2, ui::icon::Puzzle, "Discover custom notes", "Descubre las notas custom",
     "See which special notes the mod brings.", "Mira qué notas especiales trae el mod.",
     "Open the «Custom notes» tab.\nPick a type from the list.\nYou will see its script, its look and what the bot does with it.",
     "Abre la pestaña «Notas custom».\nElige un tipo de la lista.\nVerás su script, su aspecto y lo que hace el bot con ella.",
     "The ones the chart already uses show up in place in the preview.", "Las que ya usa el chart salen en su sitio en la vista previa.",
     "The mod's special notes are here.", "Aquí están las notas especiales del mod.", "tab-types", 1, -1},
    {"blocks", 0, 2, ui::icon::Add, "Create your custom note", "Crea tu nota custom",
     "A note of your own: its name, what it does and how it looks, in one place.",
     "Una nota tuya: su nombre, lo que hace y cómo se ve, en un sitio.",
     "In «Custom notes» → «Catalog», press «Create custom note…».\nName it; its look can stay like the normal notes (no sprite needed).\nPress «Create and edit blocks»: it takes you to its blocks.",
     "En «Notas custom» → «Catálogo», pulsa «Crear nota custom…».\nPonle nombre; el aspecto puede quedarse como las notas normales (no hace falta sprite).\nPulsa «Crear y editar bloques»: te lleva a sus bloques.",
     "Prefer writing the script yourself? Choose «Code only» there.",
     "¿Prefieres escribir el script tú? Elige «Solo código» ahí.",
     "Create your note here.", "Crea aquí tu nota.", "catalog-create|types-views|tab-types", 1, -1},
    {"blocks-snap", 0, 2, ui::icon::Puzzle, "Snap your first blocks", "Encaja tus primeros bloques",
     "An event and an action under it: your first mechanic.", "Un evento y una acción debajo: tu primera mecánica.",
     "Drag «when the player hits…» onto the canvas (or click it).\nSnap «change health by» under it.\nWatch the code on the right change with it.",
     "Arrastra «cuando el jugador toca…» al lienzo (o haz clic en él).\nEncaja «cambiar vida por» debajo.\nMira cómo cambia el código a la derecha.",
     "Clicking a block in the palette also puts it in its place.", "Hacer clic en un bloque de la paleta también lo pone en su sitio.",
     "Drag this block onto the canvas.", "Arrastra este bloque al lienzo.", "blocks-newtype", -1, -1},
    {"distribute", 0, 2, ui::icon::Grid, "Scatter custom notes", "Reparte notas custom",
     "Place custom notes in the song by percentage, without doing it by hand.", "Pon notas custom en la canción por porcentaje, sin hacerlo a mano.",
     "In «Custom notes», open «Distribute».\nOn the test pattern? Pick a song of the mod right there (or load the one from before).\nPress «Populate with this mod's custom notes»: nothing needs saving, the mod's chart does not change.",
     "En «Notas custom», abre «Distribuir».\n¿En el patrón de prueba? Elige ahí mismo una canción del mod (o vuelve a la de antes).\nPulsa «Poner las notas custom de este mod»: no hace falta guardar nada, el chart del mod no cambia.",
     "To keep it, «Save chart distribution» writes it as a separate chart.", "Si quieres conservarlo, «Guardar distribución» lo escribe como un chart aparte.",
     "Open «Distribute» to scatter custom notes.", "Abre «Distribuir» para repartir notas custom.", "distribute-song|distribute-fill|types-views|tab-types", 1, -1},
    {"save", 0, 3, ui::icon::Document, "Save your project", "Guarda tu proyecto",
     "Keep all your work in a .fmlnote file.", "Guarda todo tu trabajo en un archivo .fmlnote.",
     "File → Save (Ctrl+S).\nChoose where and give it a name.\nWhen you reopen it, Note Lab tells you if something changed in the mod.",
     "Archivo → Guardar (Ctrl+S).\nElige dónde y ponle nombre.\nAl reabrirlo, Note Lab avisa si algo cambió en el mod.",
     "The project remembers the open mods by their path on this PC.", "El proyecto recuerda los mods abiertos por su ruta en este PC.",
     "File → Save (Ctrl+S).", "Archivo → Guardar (Ctrl+S).", "menu-file", -1, -1},
    {"export", 0, 3, ui::icon::Zip, "Export to an engine", "Exporta a un motor",
     "Get your work out, ready to put in a mod.", "Saca tu trabajo listo para meterlo en un mod.",
     "Press «Export…» or File → Export note style….\nChoose the engine (Codename, Psych or V-Slice) and a folder or ZIP.\nNote Lab rereads the package before writing it and adds an install guide.",
     "Pulsa «Exportar…» o Archivo → Exportar estilo de notas….\nElige el motor (Codename, Psych o V-Slice) y carpeta o ZIP.\nNote Lab relee el paquete antes de escribirlo y le añade una guía de instalación.",
     "Then try it in the game: copy the folder into mods/ and open a song.", "Después pruébalo en el juego: copia la carpeta a mods/ y abre una canción.",
     "Export the style for Codename, Psych or V-Slice.", "Exporta el estilo para Codename, Psych o V-Slice.", "ex-write|export-style|menu-file", 2, -1},
    // ------------------------------------------- bloques: bundle de 3 notas --
    {"bk-new", 1, 0, ui::icon::Add, "Start your bundle", "Empieza tu bundle",
     "You will make three basic notes: damage, healing and luck. First, a new note type.",
     "Vas a crear tres notas básicas: daño, curación y suerte. Primero, un tipo de nota nuevo.",
     "Press «+» next to «Type» (or «Create custom note…» in the Catalog).\nName it «Damage»: its look can stay like the normal notes.\nPress «Create and edit blocks».",
     "Pulsa «+» junto a «Tipo» (o «Crear nota custom…» en el Catálogo).\nLlámala «Daño»: el aspecto puede quedarse como las normales.\nPulsa «Crear y editar bloques».",
     "New notes only exist in Note Lab until you export them.", "Las notas nuevas solo existen en Note Lab hasta que las exportas.",
     "Create your first note here.", "Crea aquí tu primera nota.", "blocks-choose|blocks-newtype", -1, -1},
    {"bk-event", 1, 0, ui::icon::Puzzle, "Damage · the event", "Daño · pon el evento",
     "Every mechanic starts with an event: when the note is hit.", "Toda mecánica empieza con un evento: cuando tocan la nota.",
     "The palette only shows the blocks the guide uses: they add up step by step.\nDrag «when the player hits…» onto the canvas (or click it).\nLet go anywhere on the canvas.",
     "La paleta solo enseña los bloques que usa la guía: se suman paso a paso.\nArrastra «cuando el jugador toca…» al lienzo (o haz clic en él).\nSuéltalo en cualquier sitio del lienzo.",
     "Yellow hat blocks are events: everything else hangs from them.", "Los bloques amarillos con sombrero son eventos: todo lo demás cuelga de ellos.",
     "Drag the yellow event onto the canvas.", "Arrastra el evento amarillo al lienzo.", "palette-events|blocks-palette", -1, 0},
    {"bk-hurt", 1, 0, ui::icon::Heart, "Damage · take health", "Daño · quita vida",
     "Make the note hurt when it is hit.", "Que la nota haga daño al tocarla.",
     "Drag «change health by» under the event until it snaps.\nClick its 0.1 and write -0.2.\nClicking it in the palette snaps it under the event too.",
     "Arrastra «cambiar vida por» bajo el evento hasta que encaje.\nHaz clic en su 0.1 y escribe -0.2.\nUn clic en la paleta también lo encaja bajo el evento.",
     "A negative number takes health; the whole bar is 2.", "Un número negativo quita vida; la barra entera es 2.",
     "Snap «change health» under the event and make it negative.", "Encaja «cambiar vida» bajo el evento y ponlo en negativo.",
     "palette-game|blocks-palette", -1, 1},
    {"bk-heal", 1, 1, ui::icon::Heart, "A healing note", "Una nota de curación",
     "A second note that gives health back.", "Una segunda nota que devuelve vida.",
     "Create another note with «+» and name it «Healing».\nPut the event «when the player hits…».\nUnder it, «change health by» with a positive number.",
     "Crea otra nota con «+» y llámala «Curación».\nPon el evento «cuando el jugador toca…».\nDebajo, «cambiar vida por» con un número positivo.",
     "Same blocks, another number: that is how mechanics are reused.", "Los mismos bloques con otro número: así se reutilizan las mecánicas.",
     "A new type for the healing note.", "Un tipo nuevo para la nota de curación.", "blocks-newtype", -1, 4},
    {"bk-if", 1, 2, ui::icon::Puzzle, "Luck · a condition", "Suerte · una condición",
     "The third note does something only sometimes.", "La tercera nota hace algo solo a veces.",
     "Create a third note, «Luck», with its event.\nDrag «if … then» under the event.\nPut «change health by» inside the «if».",
     "Crea una tercera nota, «Suerte», con su evento.\nArrastra «si … entonces» bajo el evento.\nMete «cambiar vida por» dentro del «si».",
     "What is inside an «if» only runs when its condition is true.", "Lo que hay dentro de un «si» solo pasa cuando su condición se cumple.",
     "Drag «if … then» under the event.", "Arrastra «si … entonces» bajo el evento.", "palette-control|blocks-palette", -1, 2},
    {"bk-chance", 1, 2, ui::icon::Pulse, "Luck · a chance", "Suerte · la probabilidad",
     "Give the «if» its condition: half of the time.", "Dale al «si» su condición: la mitad de las veces.",
     "Drag «chance of 50 %» into the hexagon slot of the «if».\nCheck that there is an action inside the «if».\nHalf of the times the note is hit, it happens.",
     "Arrastra «probabilidad de 50 %» al hueco hexagonal del «si».\nComprueba que dentro del «si» hay una acción.\nLa mitad de las veces que toquen la nota, pasará.",
     "Hexagons are yes/no values; rounded blocks are numbers.", "Los hexágonos son valores de sí o no; los redondeados, números.",
     "Drop «chance of %» into the «if» slot.", "Suelta «probabilidad de %» en el hueco del «si».", "palette-operators|blocks-palette", -1, 3},
    {"bk-code", 1, 3, ui::icon::Code, "Look at the code", "Mira el código",
     "See what each block becomes in every engine.", "Mira en qué se convierte cada bloque en cada motor.",
     "Switch the view to «Code» or «Both».\nChoose the engine: Codename, Psych or V-Slice.\nClashes are marked where an engine cannot do something.",
     "Cambia la vista a «Código» o «Los dos».\nElige el motor: Codename, Psych o V-Slice.\nLos choques se marcan donde un motor no puede hacer algo.",
     "You can edit the code and apply it back to the blocks.", "Puedes editar el código y aplicarlo a los bloques.",
     "Switch to «Code» to see what gets exported.", "Cambia a «Código» para ver lo que se exporta.", "blocks-view", -1, -1},
    {"bk-export", 1, 3, ui::icon::Zip, "Export your bundle", "Exporta tu bundle",
     "Take your three notes to the game.", "Lleva tus tres notas al juego.",
     "Choose one of your three types.\nPress «Export this type…» and pick the engine.\nRepeat with the other two into the same folder.",
     "Elige uno de tus tres tipos.\nPulsa «Exportar este tipo…» y elige el motor.\nRepite con los otros dos en la misma carpeta.",
     "Use «Distribute» to put your notes in a song.", "Usa «Distribuir» para poner tus notas en una canción.",
     "Export the type for your engine.", "Exporta el tipo para tu motor.", "blocks-export", -1, -1},
    // ------------------------------------------------- zona: Crear HUD de notas --
    {"hud-base", kTrackHud, 0, ui::icon::Layers, "Pick a starting point", "Elige el punto de partida",
     "Your HUD keeps the shapes of a style of the mod; you put the colors.",
     "Tu HUD toma las formas de un estilo del mod; los colores los pones tú.",
     "Open the «Starting point» list.\nChoose the style whose shapes you like.\nThe preview on the right changes with it.",
     "Abre la lista «Punto de partida».\nElige el estilo cuyas formas te gusten.\nLa vista de la derecha cambia con él.",
     "What you don't change comes out as in that style.", "Lo que no cambies sale como en ese estilo.",
     "Open this list and choose the shapes.", "Abre esta lista y elige las formas.", "hud-base", -1, -1},
    {"hud-colors", kTrackHud, 0, ui::icon::Palette, "Give it your colors", "Ponle tus colores",
     "One color per direction, like the base game, or a ready-made palette.",
     "Un color por dirección, como en el juego base, o una paleta ya hecha.",
     "Press a preset: Classic, Pastel, Neon…\nOr click the color of each arrow ← ↓ ↑ →.\n«Strength» is how strongly the new color is painted.",
     "Pulsa un preset: Clásico, Pastel, Neón…\nO haz clic en el color de cada flecha ← ↓ ↑ →.\n«Fuerza» es cuánto se pinta el color nuevo.",
     "The colors are painted into the images: the three engines show the same.",
     "Los colores se pintan en las imágenes: los tres motores enseñan lo mismo.",
     "Press a preset or an arrow's color.", "Pulsa un preset o el color de una flecha.", "hud-colors", -1, -1},
    {"hud-details", kTrackHud, 0, ui::icon::Settings, "The details", "Los detalles",
     "Decide how the receptors, the hits and the holds look.", "Decide cómo se ven los receptores, los aciertos y los sostenidos.",
     "«Receptors»: gray, their color or see-through.\n«On hit»: glow with its color or white.\nCheck «Paint the splashes too» to have them match.",
     "«Receptores»: grises, de su color o transparentes.\n«Al acertar»: brilla con su color o blanco.\nMarca «Pintar también las salpicaduras» para que vayan a juego.",
     "The preview moves like in the game: watch a hit before deciding.", "La vista se mueve como en el juego: mira un acierto antes de decidir.",
     "Change how receptors and hits look.", "Cambia cómo se ven receptores y aciertos.", "hud-details", -1, -1},
    {"hud-shape", kTrackHud, 0, ui::icon::Brush, "Draw your pieces (optional)", "Dibuja tus piezas (opcional)",
     "Keep the style's shapes or draw your own: note, receptor, hold and splash.",
     "Quédate con las formas del estilo o dibuja las tuyas: nota, receptor, sostenido y salpicadura.",
     "Go to step 4, «Pieces», and choose «Drawn by me».\nClick a piece's card: you choose how to start (template, free canvas…) and draw it.\nPress «Use in the HUD»: what you don't draw stays the style's.",
     "Ve al paso 4, «Piezas», y elige «Dibujadas por mí».\nPulsa la tarjeta de una pieza: eliges cómo empezar (plantilla, lienzo libre…) y la dibujas.\nPulsa «Usar en el HUD»: lo que no dibujes se queda del estilo.",
     "They are tinted with your colors; «Tint them» off keeps the drawing's.",
     "Se tiñen con tus colores; sin «Teñirlas» se quedan los del dibujo.",
     "Optional: draw your own pieces here.", "Opcional: dibuja aquí tus propias piezas.", "hud-shape", -1, -1},
    {"hud-create", kTrackHud, 0, ui::icon::Check, "Create your HUD", "Crea tu HUD",
     "Keep it as a new style of the project.", "Guárdalo como un estilo nuevo del proyecto.",
     "Give it a name at the top left.\nPress «Create HUD».\nIt shows up in your mod's list, ready to edit or export.",
     "Ponle nombre arriba a la izquierda.\nPulsa «Crear HUD».\nAparece en la lista de tu mod, listo para editar o exportar.",
     "To change it later, «Create HUD…» on that style: its recipe is kept.",
     "Para cambiarlo después, «Crear HUD…» sobre ese estilo: guarda su receta.",
     "Press here to create it.", "Pulsa aquí para crearlo.", "hud-create", -1, -1},
    // --------------------------------------------- zona: Creador custom y ranking --
    {"cc-import", kTrackCustom, 0, ui::icon::FolderOpen, "Bring your files", "Trae tus archivos",
     "Images, sheets (PNG + XML), frame sequences or OGG sounds.", "Imágenes, hojas (PNG + XML), secuencias de frames o sonidos OGG.",
     "Press «Import files…» or «Import folder / sequence…».\nOr drop them on this window.\nEach one shows up under «Resources», on the left.",
     "Pulsa «Importar archivos…» o «Importar carpeta / secuencia…».\nO suéltalos sobre esta ventana.\nCada uno aparece en «Recursos», a la izquierda.",
     "Nothing is copied into the mod: your files are kept with the project.", "No se copia nada al mod: tus archivos se guardan con el proyecto.",
     "Start here: import your files.", "Empieza aquí: importa tus archivos.", "cc-import", -1, -1},
    {"cc-role", kTrackCustom, 0, ui::icon::Grid, "Tell it what it is", "Dile qué es",
     "Each file needs a role: a piece of the notes, a HUD or ranking image, or a sound.",
     "Cada archivo necesita una función: pieza de las notas, imagen del HUD o del ranking, o un sonido.",
     "Choose the role in «Define its role» (for the ranking, «HUD image / ranking»).\nPick which piece or image it is: Sick!, Good, combo…\nPress «Assign / replace role».",
     "Elige la función en «Definir su función» (para el ranking, «Imagen HUD / ranking»).\nElige qué pieza o qué imagen es: Sick!, Good, combo…\nPulsa «Asignar / reemplazar función».",
     "Named like the base game (sick.png, combo.png…)? «Suggest roles from names» does it for you.",
     "¿Con nombres del juego base (sick.png, combo.png…)? «Sugerir funciones por nombres» lo hace por ti.",
     "Choose its role here and assign it.", "Elige aquí su función y asígnala.", "cc-roles", -1, -1},
    {"cc-validate", kTrackCustom, 0, ui::icon::Search, "Validate it", "Valídalo",
     "Note Lab checks every assignment before building anything.", "Note Lab revisa cada asignación antes de construir nada.",
     "Press «Validate resources».\nRead the errors, if there are any, and fix them.\nWhen it says validated, it's ready.",
     "Pulsa «Validar recursos».\nLee los errores, si hay, y corrígelos.\nCuando diga «validados», está listo.",
     "Each engine's limits are reported again when you export.", "Los límites de cada motor se avisan otra vez al exportar.",
     "Check everything before applying.", "Revísalo todo antes de aplicar.", "cc-validate", -1, -1},
    {"cc-apply", kTrackCustom, 0, ui::icon::Star, "Apply it", "Aplícalo",
     "Your files become a style of the project, or the ranking of your HUD.",
     "Tus archivos se convierten en un estilo del proyecto, o en el ranking de tu HUD.",
     "Give it a name under «Project».\nPress «Apply custom resources».\nSave the project to keep editing it later.",
     "Ponle nombre en «Proyecto».\nPulsa «Aplicar recursos propios».\nGuarda el proyecto para poder seguir editándolo.",
     "Then export it like any other style.", "Después expórtalo como cualquier otro estilo.",
     "Apply your resources.", "Aplica tus recursos.", "cc-apply", -1, -1},
    // ------------------------------------------------- zona: Recursos del mod --
    {"rs-kind", kTrackResources, 0, ui::icon::Search, "What are you looking for?", "¿Qué buscas?",
     "The mod's images, videos and sounds, each in its tab.", "Las imágenes, vídeos y sonidos del mod, cada uno en su pestaña.",
     "Choose Images, Videos or Sounds.\nType part of a path in the search box: «menu», «hit»…\nCheck «Include base game» to see the engine's too.",
     "Elige Imágenes, Videos o Sonidos.\nEscribe parte de una ruta en el buscador: «menu», «hit»…\nMarca «Incluir juego base» para ver también los del motor.",
     "A • after a path means a block already uses it.", "Un • tras la ruta quiere decir que ya lo usa un bloque.",
     "Pick a kind or search by path.", "Elige un tipo o busca por ruta.", "rs-kinds", -1, -1},
    {"rs-pick", kTrackResources, 0, ui::icon::Photo, "Pick one", "Elige uno",
     "Click a file to see where it comes from and how big it is.", "Haz clic en un archivo para ver de dónde viene y cuánto ocupa.",
     "Click a path in the list.\nOn the right: where it comes from, its size and whether it is inside a ZIP.\n«Show in folder» and «Copy path» take it with you.",
     "Haz clic en una ruta de la lista.\nA la derecha: de dónde viene, su tamaño y si va dentro de un ZIP.\n«Mostrar en la carpeta» y «Copiar ruta» te lo llevan.",
     "Right-click a file for more actions.", "Clic derecho en un archivo para más acciones.",
     "Click a file in the list.", "Haz clic en un archivo de la lista.", "rs-list", -1, -1},
    {"rs-listen", kTrackResources, 0, ui::icon::Volume, "Listen to a sound", "Escucha un sonido",
     "Hear it before using it; nothing plays by itself.", "Óyelo antes de usarlo; nada suena solo.",
     "Go to Sounds and pick one.\nPress «Listen».\nLoop, speed, balance and the A–B range only change what you hear.",
     "Ve a Sonidos y elige uno.\nPulsa «Escuchar».\nLoop, velocidad, balance y el rango A–B solo cambian lo que oyes.",
     "For images, the wheel zooms and the middle button drags.", "En las imágenes, la rueda hace zoom y el botón central arrastra.",
     "Pick a sound and press «Listen».", "Elige un sonido y pulsa «Escuchar».", "rs-listen|rs-kinds", -1, -1},
    {"rs-import", kTrackResources, 0, ui::icon::Add, "Bring your own", "Trae los tuyos",
     "Add your own PNG, OGG or MP4 to use them in blocks.", "Añade tus PNG, OGG o MP4 para usarlos en los bloques.",
     "Press «Import custom resources…».\nChoose the files: Note Lab inspects them first.\nBy default they stay in the project; copying them into the mod is up to you.",
     "Pulsa «Importar recursos custom…».\nElige los archivos: Note Lab los revisa antes.\nPor defecto se quedan en el proyecto; copiarlos al mod lo decides tú.",
     "Nothing is overwritten: if the name exists, you are told.", "No se sobrescribe nada: si el nombre ya existe, te avisa.",
     "Import your own files here.", "Importa aquí tus propios archivos.", "rs-import", -1, -1},
    // ---------------------------------------------------------- zona: Exportar --
    {"ex-engine", kTrackExport, 0, ui::icon::Game, "Engine and purpose", "Motor y para qué",
     "Where it goes and what it is for in that engine.", "Adónde va y para qué sirve en ese motor.",
     "Choose the engine: Codename, Psych or V-Slice.\nChoose «What it's for»: mod skin, a song, a note type…\nIf it isn't the source engine, it tells you what changes.",
     "Elige el motor: Codename, Psych o V-Slice.\nElige «Para qué»: skin del mod, una canción, un tipo de nota…\nSi no es el motor de origen, te dice lo que cambia.",
     "The text under each option says what the engine does with it.", "El texto bajo cada opción dice qué hace el motor con ello.",
     "Choose the target engine and what it's for.", "Elige el motor de destino y para qué.", "ex-engine", -1, -1},
    {"ex-name", kTrackExport, 0, ui::icon::Edit, "Name it", "Ponle nombre",
     "The files and the id come out of that name.", "De ese nombre salen los archivos y el id.",
     "Write the name.\nBelow it you see the clean name the files will use.\nOn the right, the list of what will be created.",
     "Escribe el nombre.\nDebajo ves el nombre limpio que usarán los archivos.\nA la derecha, la lista de lo que se crea.",
     "Errors in «Conflicts and warnings» block the export; warnings don't.",
     "Los errores de «Choques y avisos» bloquean la exportación; los avisos no.",
     "Name your package.", "Ponle nombre a tu paquete.", "ex-name", -1, -1},
    {"ex-output", kTrackExport, 0, ui::icon::FolderOpen, "Folder or ZIP", "¿Carpeta o ZIP?",
     "Choose the format and where it is written.", "Elige el formato y dónde se escribe.",
     "Choose Folder or ZIP.\nPress «Choose…» and pick where.\nNever inside an open mod: Note Lab doesn't write there.",
     "Elige Carpeta o ZIP.\nPulsa «Elegir…» y escoge dónde.\nNunca dentro de un mod abierto: Note Lab no escribe ahí.",
     "A ZIP is the easiest to share; a folder, to try it right away.", "El ZIP es lo más fácil de compartir; la carpeta, para probarlo enseguida.",
     "Folder or ZIP, and where.", "Carpeta o ZIP, y dónde.", "ex-output", -1, -1},
    {"ex-write", kTrackExport, 0, ui::icon::Check, "Export it", "Expórtalo",
     "Write the package and check it.", "Escribe el paquete y compruébalo.",
     "Press «Export».\nNote Lab rereads it: «structure verified» means it found the whole style.\n«Show in folder» takes you there; the install guide goes inside.",
     "Pulsa «Exportar».\nNote Lab lo relee: «estructura verificada» quiere decir que encontró el estilo entero.\n«Mostrar en la carpeta» te lleva; la guía de instalación va dentro.",
     "Then copy it into the game's mods/ folder and play a song.", "Después cópialo a la carpeta mods/ del juego y juega una canción.",
     "Press here to export.", "Pulsa aquí para exportar.", "ex-write", -1, -1},
    // ---------------------------------------------------- zona: Editor de sprites --
    {"sp-tools", kTrackSprite, 0, ui::icon::Brush, "Pick a tool", "Elige una herramienta",
     "Brush, eraser, shape, fill, picker, select and move, on the left, like in a paint program.",
     "Pincel, goma, figura, relleno, cuentagotas, selección y mover, a la izquierda, como en un programa de dibujo.",
     "Click a tool on the left (or press its key: B, E, U, G, I, M, V).\nIts options show above the sheet: size, hardness, opacity…\nThe two colors under the tools: main and outline (X swaps them).",
     "Haz clic en una herramienta a la izquierda (o pulsa su tecla: B, E, U, G, I, M, V).\nSus opciones salen encima de la hoja: tamaño, dureza, opacidad…\nLos dos colores bajo las herramientas: principal y contorno (X los cambia).",
     "The mouse wheel zooms and the middle button (or Space) drags the sheet.", "La rueda acerca y el botón central (o Espacio) arrastra la hoja.",
     "Pick a tool here.", "Elige aquí una herramienta.", "sprite-tool-0", -1, -1},
    {"sp-draw", kTrackSprite, 0, ui::icon::Edit, "Draw in a box", "Dibuja en un hueco",
     "Each row of the sheet is a piece and each box a frame.", "Cada fila de la hoja es una pieza y cada hueco un fotograma.",
     "Draw in the chosen box (outlined in purple).\nWith the brush, hold Shift and click: a straight line from the last point.\nThe fill stays inside its box; a selection limits everything.",
     "Dibuja en el hueco elegido (recuadrado en morado).\nCon el pincel, mantén Shift y haz clic: una línea recta desde el último punto.\nEl relleno no sale de su hueco; una selección lo limita todo.",
     "Draw the note looking left (←): the other directions are rotated.", "Dibuja la nota mirando a la izquierda (←): las demás direcciones salen giradas.",
     "Draw here.", "Dibuja aquí.", "sprite-cell", -1, -1},
    {"sp-preset", kTrackSprite, 0, ui::icon::Star, "Use a preset", "Usa un preset",
     "Ready-made pieces in their own layer, so they don't mix with your drawing.",
     "Piezas ya hechas en su propia capa, para no mezclarse con tu dibujo.",
     "Select a range (Select tool) or choose a box.\nPress a preset: arrow, receptor, hold, splash…\n«Whole HUD» puts the five pieces at once.",
     "Selecciona un rango (herramienta Selección) o elige un hueco.\nPulsa un preset: flecha, receptor, sostenido, salpicadura…\n«HUD completo» pone las cinco piezas de golpe.",
     "Hide or delete its layer to undo it without touching the rest.", "Oculta o borra su capa para quitarlo sin tocar lo demás.",
     "Press a preset.", "Pulsa un preset.", "sprite-preset-0", -1, -1},
    {"sp-copy", kTrackSprite, 0, ui::icon::Copy, "Copy to a free box", "Copia a un hueco libre",
     "Reuse a box: a new frame or the same piece somewhere else.", "Reutiliza un hueco: un fotograma nuevo o la misma pieza en otro sitio.",
     "Choose a box and copy it (Ctrl+C).\nPress «In a free box»: it goes to the next free one of its row.\nOr select a range and paste (Ctrl+V): it goes in a new layer.",
     "Elige un hueco y cópialo (Ctrl+C).\nPulsa «En un hueco libre»: va al siguiente libre de su fila.\nO selecciona un rango y pega (Ctrl+V): va en una capa nueva.",
     "Copy and paste also work between tabs.", "Copiar y pegar también funciona entre pestañas.",
     "Paste in a free box.", "Pega en un hueco libre.", "sprite-paste-free", -1, -1},
    {"sp-frames", kTrackSprite, 0, ui::icon::Play, "Animate it", "Anímalo",
     "The boxes with something of a row are its animation, in order.", "Los huecos con algo de una fila son su animación, en orden.",
     "Below, choose the piece and look at its frames.\n«Duplicate frame» copies the chosen one to the next free box.\nPlay it and set its FPS; onion skin shows the previous one.",
     "Abajo, elige la pieza y mira sus fotogramas.\n«Duplicar fotograma» copia el elegido al siguiente hueco libre.\nReprodúcelo y ajusta sus FPS; el papel cebolla enseña el anterior.",
     "A splash with a single frame grows and fades by itself.", "Una salpicadura de un solo fotograma crece y se desvanece sola.",
     "Use the timeline.", "Usa la línea de tiempo.", "sprite-timeline", -1, -1},
    {"sp-tabs", kTrackSprite, 0, ui::icon::Layers, "Loose drawings and other sheets", "Dibujos sueltos y otras hojas",
     "Draw a piece apart and put it into the general sheet, or open another sheet to copy from it.",
     "Dibuja una pieza aparte y métela en la hoja general, o abre otra hoja para copiar de ella.",
     "Press «Open…»: a loose drawing, the chosen box apart or another sheet.\nDraw in its tab.\n«To the general sheet» puts it in its row (or back in its box).",
     "Pulsa «Abrir…»: un dibujo suelto, el hueco elegido aparte u otra hoja.\nDibuja en su pestaña.\n«Pasar a la hoja general» lo pone en su fila (o lo devuelve a su hueco).",
     "Only the general sheet is used; the others are for working.", "Solo se usa la hoja general; las demás son para trabajar.",
     "Open a tab here.", "Abre aquí una pestaña.", "sprite-insert|sprite-newtab", -1, -1},
    {"sp-use", kTrackSprite, 0, ui::icon::Check, "Use it", "Úsalo",
     "Each row with something replaces that piece.", "Cada fila con algo sustituye esa pieza.",
     "Press «Use in the HUD» (or «Use as its look» for a note type).\nThe preview shows it right away.\nNothing is written into the mod.",
     "Pulsa «Usar en el HUD» (o «Usar como su aspecto» en un tipo de nota).\nLa vista previa lo enseña enseguida.\nNo se escribe nada en el mod.",
     "File → Save note sheet (PNG + XML) gives you the whole style in one file.",
     "Archivo → Guardar hoja de notas (PNG + XML) te da el estilo entero en un archivo.",
     "Press here to use it.", "Pulsa aquí para usarlo.", "sprite-use", -1, -1},
};
constexpr int kTutorialCount = static_cast<int>(sizeof(kTutorialMissions) / sizeof(kTutorialMissions[0]));

struct TutorialRuntime {
    TutorialRuntime() {
        focus.fill(-1);
        focusDismissed.fill(false);
        focusSince.fill(0.0);
        pointUntil.fill(0.0);
        finishedAt.fill(-1.0);
    }
    std::array<int, kTutorialTracks> focus{};         // mision elegida en la lista; -1 = la primera sin hacer
    std::string toastKey;                             // la ultima mision cumplida, para celebrarla
    double toastAt = -100.0;
    std::array<ImRect, 3> panels{};                   // mods, centro e inspector, de este fotograma
    std::array<bool, 3> panelValid{};
    std::map<std::string, ImRect> marks;              // los controles de las misiones, de este fotograma
    std::set<std::string> signals;                    // cosas que solo se ven al hacerlas
    // Al repetir una ruta: lo que ya se cumplia entonces no cuenta solo (se
    // ensena igual, con «Siguiente mision»); cuenta cuando deja de cumplirse y
    // se vuelve a hacer.
    std::set<std::string> preMet;
    bool listOpen = false;
    bool anchored = true;                             // la ventana sigue su esquina hasta que la arrastran
    // Modo foco, por ruta: cambiar de zona no lo reinicia en las demas.
    std::array<std::string, kTutorialTracks> focusKey;
    std::array<bool, kTutorialTracks> focusDismissed{};
    std::array<double, kTutorialTracks> focusSince{};
    std::array<double, kTutorialTracks> pointUntil{}; // sin modo foco: hasta cuando late el borde
    ImRect windowRect, cardRect;
    bool windowVisible = false, cardVisible = false;
    bool cardCollapsed = false;
    double demoClock = -1.0;                          // --tutorial-demo-time: el mini video quieto en ese segundo
    // Zonas: la que tiene su ventana abierta en este fotograma y su franja (y de
    // que ruta es: dentro de Exportar puede ir la mision de la ruta general).
    int area = -1;
    int stripTrack = -1;
    ImRect stripRect;
    bool stripVisible = false;
    bool areaFocusShown = false;                      // el foco de la zona se dibujo en el fotograma anterior
    // El foco: la sombra solo hasta el primer clic; el borde y el mensaje se
    // quedan hasta cumplir la mision (lo dibujado en este fotograma, para las pruebas).
    bool dimShown = false, hintShown = false;
    // Llevar la accion a la vista: desplazar el panel o la paleta una vez por mision.
    std::string scrollTargets;
    std::string jumpedFor;
    // Una ruta terminada se cierra sola un momento despues de celebrarlo.
    std::array<double, kTutorialTracks> finishedAt{};
    bool askGeneral = false;                          // la pregunta del primer arranque
    std::string forceAsk;                             // --tutorial-ask=<zona>: la pregunta tambien en capturas
    bool quiet = false;                               // --ui-test: ni preguntas ni franjas
    bool openMenu = false;                            // --tutorial-menu: el menu desplegado (capturas)
    // Para --ui-test=tutorial: pregunta aunque no haya ventana de verdad, y la
    // pregunta y los botones del ultimo fotograma.
    bool testing = false;
    std::string lastAsk;
    ImRect askAcceptRect, askSkipRect, stripNextRect, stripLeaveRect;
    std::set<std::string> offered;                    // avisos que ya sonaron en esta sesion
};
TutorialRuntime g_tutorial;

void startTutorial(NoteLabApp& app, int track, bool fromStart);
bool tutorialAlreadyDone(const char* key) { return g_tutorial.preMet.count(key) > 0; }

const char* tutorialText(const NoteLabApp& app, const char* en, const char* es) { return app.spanish ? es : en; }

// Un sonido del tutorial, si estan encendidos (nunca en capturas ni pruebas).
void tutorialSound(const NoteLabApp& app, nlblocks::TutorialCue cue) { nlblocks::playTutorialCue(cue, app.tutorialSounds && !app.headless); }

// ------------------------------------------------------------- marcas --

void tutorialBeginFrame() {
    g_tutorial.panelValid = {};
    g_tutorial.marks.clear();
    g_tutorial.windowVisible = g_tutorial.cardVisible = false;
    g_tutorial.area = -1;
    g_tutorial.stripTrack = -1;
    g_tutorial.stripVisible = false;
    g_tutorial.lastAsk.clear();
}

void tutorialMarkRect(const std::string& key, ImVec2 min, ImVec2 max) {
    if (max.x - min.x < 2.0f || max.y - min.y < 2.0f) return;
    g_tutorial.marks[key] = ImRect(min, max);
}

bool tutorialListHas(const std::string& list, const std::string& key) {
    size_t begin = 0;
    while (begin <= list.size()) {
        const size_t end = list.find('|', begin);
        if (list.compare(begin, end == std::string::npos ? std::string::npos : end - begin, key) == 0) return true;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return false;
}

// El control recien dibujado (el ultimo item). Si es lo que pide la mision que
// acaba de empezar y quedo fuera de su panel, el panel se desplaza hasta el.
void tutorialMark(const char* key) {
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    tutorialMarkRect(key, min, max);
    if (g_tutorial.scrollTargets.empty() || !tutorialListHas(g_tutorial.scrollTargets, key)) return;
    g_tutorial.scrollTargets.clear();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImRect visible = window->InnerClipRect;
    if (min.y >= visible.Min.y && max.y <= visible.Max.y) return;
    ImGui::SetScrollFromPosY(min.y - window->Pos.y - 12.0f, 0.0f);
}

// El panel recien dibujado (el ultimo EndChild), para poder senalarlo.
void tutorialPanel(int index) {
    if (index < 0 || index >= static_cast<int>(g_tutorial.panels.size())) return;
    g_tutorial.panels[static_cast<size_t>(index)] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    g_tutorial.panelValid[static_cast<size_t>(index)] = true;
}

// La paleta, el lienzo y la cabecera de cada categoria que se usa, tal como
// los midio el editor de bloques en este fotograma; en el lienzo, el hueco del
// numero del primer «cambiar vida» y el de la condicion del primer «si».
void tutorialMarkCanvas(const nlblocks::CanvasState& canvas, const BlockProgram& program) {
    for (const char* key : {"do.health", "ctl.if"})
        for (const auto& [id, node] : program.nodes) {
            if (node.key != key) continue;
            const auto slot = canvas.slotRects.find(static_cast<long long>(id) * 16);
            if (slot != canvas.slotRects.end()) {
                const ImVec4 r = slot->second;
                tutorialMarkRect(std::string("slot:") + key, ImVec2(r.x, r.y), ImVec2(r.x + r.z, r.y + r.w));
            }
            break;
        }
    tutorialMarkRect("blocks-canvas", canvas.canvasMin, canvas.canvasMax);
    tutorialMarkRect("blocks-palette", canvas.paletteMin, canvas.paletteMax);
    const ImRect palette(canvas.paletteMin, canvas.paletteMax);
    const std::pair<BlockCategory, const char*> wanted[] = {
        {BlockCategory::Events, "palette-events"}, {BlockCategory::Game, "palette-game"},
        {BlockCategory::Control, "palette-control"}, {BlockCategory::Operators, "palette-operators"}};
    for (const auto& [category, key] : wanted) {
        // El boton redondo de la categoria (arriba de la paleta, siempre a la
        // vista); si no, su cabecera en la lista, solo si se ve.
        const ImVec4 button = canvas.categoryRects[static_cast<size_t>(category)];
        if (button.z > 0.0f && button.w > 0.0f) {
            tutorialMarkRect(key, ImVec2(button.x, button.y), ImVec2(button.x + button.z, button.y + button.w));
            continue;
        }
        const ImVec4 r = canvas.categoryHeaderRects[static_cast<size_t>(category)];
        const ImRect header(ImVec2(r.x, r.y), ImVec2(r.x + r.z, r.y + r.w));
        if (r.z <= 0.0f || r.w <= 0.0f || !palette.Contains(header)) continue;
        tutorialMarkRect(key, header.Min, header.Max);
    }
    // Cada categoria por su numero y cada bloque de la paleta que se ve entero:
    // las misiones senalan el bloque concreto, no solo su categoria.
    float listTop = canvas.paletteMin.y;
    for (size_t c = 0; c < canvas.categoryRects.size(); ++c) {
        const ImVec4 b = canvas.categoryRects[c];
        if (b.z <= 0.0f || b.w <= 0.0f) continue;
        tutorialMarkRect("palcat:" + std::to_string(c), ImVec2(b.x, b.y), ImVec2(b.x + b.z, b.y + b.w));
        listTop = std::max(listTop, b.y + b.w + 6.0f);
    }
    for (const auto& [block, r] : canvas.paletteRects) {
        const float middle = r.y + r.w * 0.5f;   // a la vista al menos hasta la mitad
        if (middle >= listTop && middle <= canvas.paletteMax.y)
            tutorialMarkRect("pal:" + block, ImVec2(r.x, std::max(r.y, listTop)), ImVec2(r.x + r.z, std::min(r.y + r.w, canvas.paletteMax.y)));
    }
}

// Algo que solo se ve en el momento de hacerlo.
void tutorialSignal(const char* key) { g_tutorial.signals.insert(key); }

// ----------------------------------------------------------- deteccion --

// Lo que llevan los tipos creados en Note Lab (no los del mod), para el bundle.
struct TutorialRecipe {
    bool hit = false, hurt = false, heal = false, luckIf = false, luck = false;
};

void tutorialWalk(const BlockProgram& program, int id, TutorialRecipe& recipe, int depth) {
    for (int guard = 0; id >= 0 && guard < 256 && depth < 8; ++guard) {
        const BlockNode* node = program.node(id);
        if (!node) return;
        // Dos maneras validas: la accion «cambiar vida» al tocarla, o la
        // propiedad «vida al tocarla» bajo «al crear» (la de los presets).
        if ((node->key == "do.health" || node->key == "hit.health") && !node->args.empty()) {
            const double value = std::atof(node->args[0].value.c_str());
            if (value < 0.0) recipe.hurt = true;
            if (value > 0.0) recipe.heal = true;
        }
        if (node->key == "ctl.if" || node->key == "ctl.ifElse") {
            recipe.luckIf = true;
            const BlockNode* condition = node->args.empty() ? nullptr : program.node(node->args[0].block);
            const bool inside = node->args.size() > 1 && node->args[1].block >= 0;
            if (condition && condition->key == "op.chance" && inside) recipe.luck = true;
            for (size_t arg = 1; arg < node->args.size(); ++arg)
                if (node->args[arg].block >= 0) tutorialWalk(program, node->args[arg].block, recipe, depth + 1);
        }
        id = node->next;
    }
}

struct TutorialBundle {
    int created = 0;
    bool event = false, hurt = false, heal = false, luckIf = false, luck = false;
};

std::string tutorialLowered(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Un tipo creado en Note Lab (no uno que ya trae el mod).
bool tutorialCreatedType(const Source& source, const std::string& name) {
    const std::string key = tutorialLowered(name);
    return std::none_of(source.noteTypes.begin(), source.noteTypes.end(),
                        [&](const NoteTypeEntry& entry) { return tutorialLowered(entry.name) == key; });
}

// Lo que lleva un programa: «cuando … toca» con acciones, o «al crear» con
// propiedades (los presets); y si tiene algun evento puesto.
TutorialRecipe tutorialRecipeOf(const BlockProgram& program, bool* hasHat = nullptr) {
    TutorialRecipe recipe;
    for (int top : program.tops) {
        const BlockNode* hat = program.node(top);
        if (!hat || hat->key.rfind("event.", 0) != 0) continue;
        if (hasHat) *hasHat = true;
        if (hat->key != "event.hit" && hat->key != "event.create") continue;
        recipe.hit = true;
        tutorialWalk(program, hat->next, recipe, 0);
    }
    return recipe;
}

// El tipo abierto en el editor de bloques, si lo hay.
const BlockProgram* tutorialOpenProgram(const NoteLabApp& app, bool* created = nullptr) {
    if (app.blocksSource < 0 || app.blocksSource >= static_cast<int>(app.sources.size()) || app.blocksType.empty()) return nullptr;
    const Source& source = *app.sources[static_cast<size_t>(app.blocksSource)];
    if (created) *created = tutorialCreatedType(source, app.blocksType);
    const auto found = source.typeBlocks.find(app.blocksType);
    return found == source.typeBlocks.end() ? nullptr : &found->second;
}

// Lo que monta la guia de bloques: evento, cambiar vida, «si» y probabilidad.
// Un tipo con otros bloques ya tiene trabajo del autor: la guia no se mete en
// el (ni lo borra) y pide una nota nueva.
bool tutorialForeignWork(const BlockProgram& program) {
    static const std::set<std::string> guide = {"event.hit", "do.health", "ctl.if", "op.chance"};
    return std::any_of(program.nodes.begin(), program.nodes.end(), [](const auto& entry) { return !guide.count(entry.second.key); });
}

bool tutorialGuideBlocked(const NoteLabApp& app) {
    const BlockProgram* program = tutorialOpenProgram(app);
    return program && tutorialForeignWork(*program);
}

bool tutorialInBlocks(const NoteLabApp& app) { return !app.sources.empty() && app.centerTab == 1 && app.typesView == 1; }

int tutorialHits(const NoteLabApp& app) {
    int hits = 0;
    for (Judgement j : {Judgement::Sick, Judgement::Good, Judgement::Bad, Judgement::Shit}) hits += app.counts[static_cast<size_t>(j)];
    return hits;
}

TutorialBundle tutorialBundle(const NoteLabApp& app) {
    std::vector<TutorialRecipe> recipes;
    for (const auto& source : app.sources)
        for (const auto& [name, program] : source->typeBlocks) {
            if (!tutorialCreatedType(*source, name)) continue;
            recipes.push_back(tutorialRecipeOf(program));
        }
    TutorialBundle bundle;
    bundle.created = static_cast<int>(recipes.size());
    for (const TutorialRecipe& recipe : recipes) bundle.event = bundle.event || recipe.hit;
    // Tres tipos distintos: el de dano, uno de curacion que no sea ese (mejor si
    // no lleva un «si») y uno de suerte que no sea ninguno de los dos.
    int hurt = -1, heal = -1;
    for (size_t i = 0; i < recipes.size() && hurt < 0; ++i) if (recipes[i].hurt) hurt = static_cast<int>(i);
    for (int pass = 0; pass < 2 && heal < 0; ++pass)
        for (size_t i = 0; i < recipes.size() && heal < 0; ++i)
            if (static_cast<int>(i) != hurt && recipes[i].heal && (pass == 1 || !recipes[i].luckIf)) heal = static_cast<int>(i);
    bundle.hurt = hurt >= 0;
    bundle.heal = heal >= 0;
    for (size_t i = 0; i < recipes.size(); ++i) {
        if (static_cast<int>(i) == hurt || static_cast<int>(i) == heal) continue;
        bundle.luckIf = bundle.luckIf || recipes[i].luckIf;
        bundle.luck = bundle.luck || recipes[i].luck;
    }
    return bundle;
}

bool tutorialMet(const NoteLabApp& app, const std::string& key, const TutorialBundle& bundle) {
    if (key == "open-mod") return !app.sources.empty();
    if (key == "pick-style") return app.selSource >= 0 && app.selStyle >= 0;
    if (key == "song") return app.songSource >= 0 && app.songIndex >= 0;
    if (key == "play") return app.manual && tutorialHits(app) >= 3;
    if (key == "tweak") {
        for (const auto& source : app.sources)
            for (std::uint8_t edited : source->edited) if (edited) return true;
        return false;
    }
    if (key == "variant") {
        for (const auto& source : app.sources)
            for (const NoteStyle& style : source->catalog.styles) if (isVariant(style)) return true;
        return false;
    }
    if (key == "custom-notes") return app.centerTab == 1 && app.selType >= 0;
    if (key == "blocks") {
        bool created = false;
        return g_tutorial.signals.count("newnote") > 0 || (tutorialInBlocks(app) && tutorialOpenProgram(app, &created) && created);
    }
    if (key == "blocks-snap") {
        // Un evento con algo encajado debajo, en cualquier tipo.
        for (const auto& source : app.sources)
            for (const auto& entry : source->typeBlocks)
                for (int top : entry.second.tops) {
                    const BlockNode* hat = entry.second.node(top);
                    if (hat && hat->key.rfind("event.", 0) == 0 && hat->next >= 0) return true;
                }
        return false;
    }
    if (key == "distribute") return app.distributed;
    if (key == "save") return !app.projectPath.empty() && !app.dirty;
    if (key == "export") return !app.exporting.written.empty() && app.exporting.writtenVerified;
    if (key == "bk-new") return bundle.created > 0;
    if (key == "bk-event") return bundle.event;
    if (key == "bk-hurt") return bundle.hurt;
    if (key == "bk-heal") return bundle.heal;
    if (key == "bk-if") return bundle.luckIf;
    if (key == "bk-chance") return bundle.luck;
    // Ya con el bundle hecho: una vista «Código» guardada de antes no la cumple sola.
    if (key == "bk-code") return bundle.luck && app.centerTab == 1 && app.typesView == 1 && app.blocksView != 0;
    if (key == "bk-export")
        return !app.exporting.written.empty() && app.exporting.writtenVerified && app.exporting.noteType[0] != '\0';
    // Las zonas: lo que deja hecho su ventana o lo que se toco en ella (senales).
    auto anyRecipe = [&](const char* prefix) {
        for (const auto& source : app.sources)
            for (const auto& entry : source->recipes)
                if (entry.first.rfind(prefix, 0) == 0) return true;
        return false;
    };
    if (key == "hud-create") return anyRecipe("arrows:");
    if (key == "cc-import") return !app.custom.recipe.resources.empty();
    if (key == "cc-role") return !app.custom.recipe.assignments.empty();
    if (key == "cc-apply") return anyRecipe("custom:");
    if (key == "rs-pick") return !app.media.selected.empty();
    if (key == "rs-import") return app.media.importIsOpen || g_tutorial.signals.count(key) > 0;
    if (key == "ex-write") return !app.exporting.written.empty() && app.exporting.writtenVerified;
    const TutorialMission* mission = nullptr;
    for (const TutorialMission& m : kTutorialMissions) if (key == m.key) mission = &m;
    if (mission && mission->track >= kTrackHud) return g_tutorial.signals.count(key) > 0;
    return false;
}

int tutorialTrackCount(int track) {
    int count = 0;
    for (const TutorialMission& mission : kTutorialMissions) count += mission.track == track ? 1 : 0;
    return count;
}

int tutorialDoneCount(const NoteLabApp& app, int track) {
    int done = 0;
    for (const TutorialMission& mission : kTutorialMissions)
        if (mission.track == track && app.tutorialDone.count(mission.key)) ++done;
    return done;
}

// La mision que se ensena en una ruta: la elegida en la lista o la primera
// sin hacer; -1 cuando estan todas.
int tutorialCurrent(const NoteLabApp& app, int track) {
    const int chosen = g_tutorial.focus[static_cast<size_t>(track)];
    if (chosen >= 0 && chosen < kTutorialCount && kTutorialMissions[chosen].track == track) return chosen;
    for (int i = 0; i < kTutorialCount; ++i)
        if (kTutorialMissions[i].track == track && !app.tutorialDone.count(kTutorialMissions[i].key)) return i;
    return -1;
}

// La posicion de una mision dentro de su ruta (1, 2, 3...).
int tutorialNumber(int index) {
    int number = 0;
    for (int i = 0; i <= index && i < kTutorialCount; ++i)
        if (kTutorialMissions[i].track == kTutorialMissions[index].track) ++number;
    return number;
}

// Lo contestado en la pregunta de cada zona: 0 sin preguntar, 1 haciendolo,
// 2 saltado o cerrado (no se vuelve a preguntar).
int tutorialAreaState(const NoteLabApp& app, int track) {
    if (track == kTrackGeneral) return app.tutorialSeen ? (app.tutorialOpen ? 1 : 2) : 0;
    const auto found = app.tutorialAreas.find(kTutorialAreas[track].key);
    return found == app.tutorialAreas.end() ? 0 : found->second;
}

// La ruta general en marcha (abierta y con misiones por hacer): manda ella. La
// guia de bloques y las zonas esperan, para no cortarle el recorrido.
bool tutorialGeneralActive(const NoteLabApp& app) { return app.tutorialOpen && tutorialCurrent(app, kTrackGeneral) >= 0; }

// Con el editor de bloques a la vista (pregunte o no la guia).
bool tutorialBlocksViewOpen(const NoteLabApp& app) { return tutorialInBlocks(app); }

bool tutorialBlocksGuideVisible(const NoteLabApp& app) {
    return tutorialInBlocks(app) && !app.tutorialBlocksHidden && tutorialAreaState(app, kTrackBlocks) == 1 && !tutorialGeneralActive(app);
}

std::string tutorialSummary(const NoteLabApp& app) {
    std::string text;
    for (int track = 0; track < kTutorialTracks; ++track) {
        const int current = tutorialCurrent(app, track);
        text += std::string(track == 0 ? "" : " | ") + kTutorialAreas[track].key + " " + std::to_string(tutorialDoneCount(app, track)) + "/" +
                std::to_string(tutorialTrackCount(track)) + " current=" + (current >= 0 ? kTutorialMissions[current].key : "done");
    }
    const TutorialBundle bundle = tutorialBundle(app);
    text += " | bundle hurt=" + std::to_string(bundle.hurt) + " heal=" + std::to_string(bundle.heal) + " luck=" + std::to_string(bundle.luck);
    return text;
}

// Una ruta abierta: su ventana, su guia o su franja estan a la vista.
bool tutorialTrackOpen(const NoteLabApp& app, int track) {
    if (track == kTrackGeneral) return app.tutorialOpen;
    if (track == kTrackBlocks) return !app.tutorialBlocksHidden && tutorialAreaState(app, track) == 1;
    return tutorialAreaState(app, track) == 1;
}

// Una ruta terminada se cierra sola: lo que ocupaba se va y se repite desde el menu.
void tutorialCloseFinished(NoteLabApp& app, int track) {
    if (track == kTrackGeneral) app.tutorialOpen = false;
    else if (track == kTrackBlocks) app.tutorialBlocksHidden = true;
    else app.tutorialAreas[kTutorialAreas[track].key] = 2;
    const TutorialArea& area = kTutorialAreas[track];
    setStatus(app, std::string("Tutorial «") + area.nameEn + "» complete. Repeat it whenever you want from the Tutorial menu.",
                   std::string("Tutorial «") + area.nameEs + "» completado. Repítelo cuando quieras desde el menú Tutorial.");
}

// Cada fotograma, con el tutorial abierto o no: lo que la persona ya hizo cuenta igual.
void updateTutorial(NoteLabApp& app) {
    const TutorialBundle bundle = tutorialBundle(app);
    const double now = ImGui::GetTime();
    bool changed = false, missionSound = false, finishedSound = false;
    for (int i = 0; i < kTutorialCount; ++i) {
        const TutorialMission& mission = kTutorialMissions[i];
        if (app.tutorialDone.count(mission.key)) continue;
        const bool met = tutorialMet(app, mission.key, bundle);
        const auto before = g_tutorial.preMet.find(mission.key);
        if (before != g_tutorial.preMet.end()) {
            if (!met) g_tutorial.preMet.erase(before);
            continue;
        }
        if (!met) continue;
        // Solo suena la mision que se esta ensenando: lo que se cumple de paso
        // (otra mision, una ruta que no se ve) se marca en silencio, para que
        // un boton cualquiera no suene a tutorial.
        const bool shown = tutorialTrackOpen(app, mission.track) && tutorialCurrent(app, mission.track) == i &&
                           (mission.track == kTrackGeneral || !tutorialGeneralActive(app));
        app.tutorialDone.insert(mission.key);
        changed = true;
        g_tutorial.toastKey = mission.key;
        g_tutorial.toastAt = now;
        if (g_tutorial.focus[static_cast<size_t>(mission.track)] == i) g_tutorial.focus[static_cast<size_t>(mission.track)] = -1;
        missionSound = missionSound || shown;
        // «Juega tu» en el tutorial: con tres aciertos la vista previa vuelve a
        // jugar sola y se sigue (fuera del tutorial, jugar no cambia nada).
        if (std::string(mission.key) == "play" && app.tutorialOpen) {
            app.manual = false;
            setStatus(app, "Three hits! The preview plays by itself again; the tutorial goes on.",
                           "¡Tres aciertos! La vista previa vuelve a jugar sola y el tutorial sigue.");
        }
    }
    // Terminada una ruta, se celebra un momento y se cierra sola.
    for (int track = 0; track < kTutorialTracks; ++track) {
        double& finished = g_tutorial.finishedAt[static_cast<size_t>(track)];
        if (!tutorialTrackOpen(app, track) || tutorialCurrent(app, track) >= 0) {
            finished = -1.0;
            continue;
        }
        if (finished < 0.0) {
            finished = now;
            finishedSound = true;
        } else if (now - finished > 3.5) {
            finished = -1.0;
            tutorialCloseFinished(app, track);
            changed = true;
        }
    }
    if (finishedSound) tutorialSound(app, nlblocks::TutorialCue::Finished);
    else if (missionSound) tutorialSound(app, nlblocks::TutorialCue::Mission);
    if (changed && !app.headless) saveSettings(app);
}

// El bloque de la paleta y, si no se ve, el boton de su categoria.
std::string tutorialPalette(const char* block) {
    const BlockDef* def = blockDef(block);
    return std::string("pal:") + block + (def ? "|palcat:" + std::to_string(static_cast<int>(def->category)) : std::string());
}

// Lo que se senala. En las misiones de montar bloques, el paso que falta en el
// tipo abierto: un tipo nuevo, el evento, la accion o su numero. En «Juega tu»,
// nada mientras se juega (taparia las flechas). En el resto, sus controles.
std::string tutorialTargets(const NoteLabApp& app, const TutorialMission& mission) {
    const std::string key = mission.key;
    if (key == "play" && app.manual) return "";
    const bool building = key == "blocks-snap" || key == "bk-event" || key == "bk-hurt" || key == "bk-heal" || key == "bk-if" || key == "bk-chance";
    if (!building) return mission.targets;
    if (!tutorialInBlocks(app)) return "types-views|tab-types";
    bool created = false;
    const BlockProgram* program = tutorialOpenProgram(app, &created);
    bool hasHat = false, health = false;
    TutorialRecipe recipe;
    if (program) {
        recipe = tutorialRecipeOf(*program, &hasHat);
        for (const auto& entry : program->nodes) health = health || entry.second.key == "do.health";
    }
    // Cada nota del bundle va en su propio tipo nuevo, nunca en uno con
    // trabajo del autor.
    const bool foreign = program && key != "blocks-snap" && tutorialForeignWork(*program);
    const bool needNew = !program || foreign || (key != "blocks-snap" && !created) || (key == "bk-heal" && recipe.hurt) ||
                         (key == "bk-if" && (recipe.hurt || recipe.heal));
    if (needNew) return program ? "blocks-newtype" : "blocks-choose|blocks-newtype";
    if (key == "bk-chance") return tutorialPalette("op.chance");
    if (!hasHat) return tutorialPalette("event.hit");
    if (key == "bk-if") return tutorialPalette("ctl.if");
    if ((key == "bk-hurt" && health && !recipe.hurt) || (key == "bk-heal" && health && !recipe.heal)) return "slot:do.health|" + tutorialPalette("do.health");
    return tutorialPalette("do.health");
}

// El control que se ilumina: la primera marca que este en pantalla, o el panel.
bool tutorialTarget(const NoteLabApp& app, const TutorialMission& mission, ImRect& target, std::string* resolved = nullptr) {
    const std::string targets = tutorialTargets(app, mission);
    if (targets.empty() && std::string(mission.key) == "play") return false;
    size_t begin = 0;
    while (begin < targets.size()) {
        const size_t end = targets.find('|', begin);
        const std::string key = targets.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        const auto found = g_tutorial.marks.find(key);
        if (found != g_tutorial.marks.end()) {
            target = found->second;
            if (resolved) *resolved = key;
            return true;
        }
        if (key == "menu-file") break;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (mission.panel >= 0 && mission.panel < 3 && g_tutorial.panelValid[static_cast<size_t>(mission.panel)]) {
        target = g_tutorial.panels[static_cast<size_t>(mission.panel)];
        if (resolved) *resolved = "panel";
        return true;
    }
    return false;
}

// Llevar la accion a la vista: el bloque pedido, a la paleta (una vez por
// bloque: si la persona se desplaza, se respeta).
void tutorialBringIntoView(NoteLabApp& app, const TutorialMission& mission) {
    const std::string targets = tutorialTargets(app, mission);
    if (targets.rfind("pal:", 0) != 0) return;
    const std::string block = targets.substr(4, targets.find('|') - 4);
    if (g_tutorial.marks.count("pal:" + block) || g_tutorial.jumpedFor == block) return;
    const BlockDef* def = blockDef(block);
    if (!def || !tutorialInBlocks(app)) return;
    const size_t category = static_cast<size_t>(def->category);
    app.canvas.jumpCategory = static_cast<int>(category);
    if (category < app.canvas.collapsed.size()) app.canvas.collapsed[category] = false;
    app.canvas.search.fill('\0');
    g_tutorial.jumpedFor = block;
}

// La mision de una ruta empieza (o «Muestrame»): vuelve el foco entero y su
// control se trae a la vista.
void tutorialFocusMission(NoteLabApp& app, int track, const TutorialMission& mission, double now) {
    const size_t t = static_cast<size_t>(track);
    g_tutorial.focusKey[t] = mission.key;
    g_tutorial.focusDismissed[t] = false;
    g_tutorial.focusSince[t] = now;
    g_tutorial.pointUntil[t] = now + 5.0;
    g_tutorial.scrollTargets = tutorialTargets(app, mission);
    g_tutorial.jumpedFor.clear();
}

// «Muestrame»: a la pestana donde se hace, y vuelve el modo foco.
void tutorialShow(NoteLabApp& app, const TutorialMission& mission) {
    const std::string key = mission.key;
    if (key == "song" || key == "play") app.requestedTab = 0;
    else if (key == "custom-notes") { app.requestedTab = 1; app.typesView = 0; }
    else if (key == "blocks") { app.requestedTab = 1; app.typesView = 0; }
    else if (key == "blocks-snap") { app.requestedTab = 1; app.typesView = 1; }
    else if (key == "distribute") { app.requestedTab = 1; app.typesView = 2; }
    else if (mission.track == kTrackBlocks) { app.requestedTab = 1; app.typesView = 1; }
    tutorialFocusMission(app, mission.track, mission, ImGui::GetTime());
    g_tutorial.pointUntil[static_cast<size_t>(mission.track)] = ImGui::GetTime() + 6.0;
}

// ------------------------------------------------------------ dibujo --

// Casillas de progreso de una ruta, con un hueco mayor entre capitulos.
void drawTutorialProgress(const NoteLabApp& app, int track, int current, float width) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const int count = tutorialTrackCount(track);
    const float height = 8.0f, gap = 3.0f, chapterGap = 9.0f;
    int chapters = 0, last = -1;
    for (const TutorialMission& mission : kTutorialMissions)
        if (mission.track == track && mission.chapter != last) { ++chapters; last = mission.chapter; }
    const float cell = (width - gap * static_cast<float>(count - chapters) - chapterGap * static_cast<float>(chapters - 1)) /
                       static_cast<float>(std::max(1, count));
    const double pulse = 0.55 + 0.45 * std::sin(ImGui::GetTime() * 5.0);
    float x = at.x;
    int previous = -1;
    for (int i = 0; i < kTutorialCount; ++i) {
        const TutorialMission& mission = kTutorialMissions[i];
        if (mission.track != track) continue;
        if (previous >= 0) x += mission.chapter != kTutorialMissions[previous].chapter ? chapterGap : gap;
        const bool done = app.tutorialDone.count(mission.key) > 0;
        const ImU32 tint = done ? ui::color::Success
            : i == current ? ui::withAlpha(ui::color::Accent, static_cast<int>(255.0 * pulse))
            : ui::color::Border;
        draw->AddRectFilled(ImVec2(x, at.y), ImVec2(x + cell, at.y + height), tint, 3.0f);
        x += cell;
        previous = i;
    }
    ImGui::Dummy(ImVec2(width, height));
}

// Pasos numerados, con su circulo.
void drawTutorialSteps(const char* steps, float dot = 20.0f) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const std::string text = steps;
    size_t begin = 0;
    int number = 1;
    while (begin <= text.size()) {
        const size_t end = text.find('\n', begin);
        const std::string step = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 center(at.x + dot * 0.5f, at.y + dot * 0.5f);
        draw->AddCircleFilled(center, dot * 0.5f, ui::color::Raised);
        draw->AddCircle(center, dot * 0.5f, ui::withAlpha(ui::color::Accent, 180), 0, 1.5f);
        const std::string digit = std::to_string(number++);
        const float digitSize = dot * 0.62f;
        const ImVec2 size = ui::fonts().semibold->CalcTextSizeA(digitSize, FLT_MAX, 0.0f, digit.c_str());
        draw->AddText(ui::fonts().semibold, digitSize, ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), ui::color::Text, digit.c_str());
        ImGui::SetCursorScreenPos(ImVec2(at.x + dot + 10.0f, at.y + (dot - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(step.c_str());
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(at.x, std::max(ImGui::GetCursorScreenPos().y, at.y + dot + 4.0f)));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
}

// «¡Mision cumplida!» de una ruta: tres segundos y medio, desvaneciendose.
void drawTutorialToast(const NoteLabApp& app, int track, float width) {
    const double since = ImGui::GetTime() - g_tutorial.toastAt;
    if (g_tutorial.toastKey.empty() || since < 0.0 || since >= 3.5) return;
    const TutorialMission* finished = nullptr;
    for (const TutorialMission& mission : kTutorialMissions)
        if (g_tutorial.toastKey == mission.key && mission.track == track) finished = &mission;
    if (!finished) return;
    const bool es = app.spanish;
    const float alpha = static_cast<float>(since < 3.0 ? 1.0 : (3.5 - since) / 0.5);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = 52.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), ui::withAlpha(ui::color::Success, static_cast<int>(48.0f * alpha)), 8.0f);
    draw->AddRect(at, ImVec2(at.x + width, at.y + height), ui::withAlpha(ui::color::Success, static_cast<int>(200.0f * alpha)), 8.0f, 0, 1.5f);
    const ImU32 ink = ui::withAlpha(ui::color::Success, static_cast<int>(255.0f * alpha));
    draw->AddText(ui::fonts().ui, 20.0f, ImVec2(at.x + 12.0f, at.y + 15.0f), ink, ui::icon::Check);
    draw->AddText(ui::fonts().semibold, 15.0f, ImVec2(at.x + 42.0f, at.y + 8.0f), ui::withAlpha(ui::color::Text, static_cast<int>(255.0f * alpha)),
                  es ? "¡Misión cumplida!" : "Mission complete!");
    draw->AddText(ui::fonts().ui, 14.0f, ImVec2(at.x + 42.0f, at.y + 28.0f), ui::withAlpha(ui::color::Muted, static_cast<int>(255.0f * alpha)),
                  es ? finished->titleEs : finished->titleEn);
    const char* star = "+1";
    const ImVec2 starSize = ui::fonts().semibold->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, star);
    draw->AddText(ui::fonts().semibold, 15.0f, ImVec2(at.x + width - starSize.x - 32.0f, at.y + 17.0f), ink, star);
    draw->AddText(ui::fonts().ui, 15.0f, ImVec2(at.x + width - 27.0f, at.y + 17.0f), ink, ui::icon::Star);
    ImGui::Dummy(ImVec2(width, height + 4.0f));
}

// Los textos de los bloques del mini video: los mismos que dice la paleta
// (NoteBlocks.cpp: «cuando {0} toca», «cambiar vida por {0}», «si {0}
// entonces», «probabilidad de {0} %»), con los valores que pide la guia.
struct DemoLabels {
    const char* hat;
    const char* health;
    const char* heal;
    const char* ifWord;
    const char* thenWord;
    const char* chance;
};

DemoLabels demoLabels(bool es) {
    return es ? DemoLabels{"cuando el jugador toca", "cambiar vida por -0.2", "cambiar vida por 0.2", "si", "entonces", "probabilidad de 50 %"}
              : DemoLabels{"when the player hits", "change health by -0.2", "change health by 0.2", "if", "then", "chance of 50 %"};
}

constexpr float kDemoFont = 11.0f;

float demoTextW(const char* text) { return ui::fonts().semibold->CalcTextSizeA(kDemoFont, FLT_MAX, 0.0f, text).x; }

// Los anchos del mini video, medidos con sus textos: nada se sale del bloque.
struct DemoWidths {
    float hat, health, chance, slot, ifBlock, palette, canvas;
};

DemoWidths demoWidths(bool es) {
    const DemoLabels l = demoLabels(es);
    DemoWidths w{};
    w.hat = demoTextW(l.hat) + 16.0f;
    w.health = demoTextW(l.health) + 14.0f;
    w.chance = demoTextW(l.chance) + 20.0f;
    w.slot = w.chance + 4.0f;
    w.ifBlock = 6.0f + demoTextW(l.ifWord) + 6.0f + w.slot + 6.0f + demoTextW(l.thenWord) + 8.0f;
    w.palette = std::max({w.hat, w.health, w.ifBlock, w.chance}) + 16.0f;
    w.canvas = std::max({w.hat, w.health, w.ifBlock}) + 26.0f;
    return w;
}

ImVec2 blocksDemoSize(const NoteLabApp& app) {
    const DemoWidths w = demoWidths(app.spanish);
    return ImVec2(w.palette + 14.0f + w.canvas, 124.0f);
}

// Un bloque del mini video: sombrero (evento), instruccion, «si» o hexagono.
// El «si» lleva su palabra, el hueco de la condicion (slotW) y «entonces».
void drawDemoBlock(ImDrawList* draw, ImVec2 at, float width, int shape, ImU32 fill, ImU32 stroke, const char* label, float alpha,
                   float slotW = 0.0f, const char* after = nullptr) {
    const ImU32 f = ui::withAlpha(fill, static_cast<int>(255.0f * alpha));
    const ImU32 s = ui::withAlpha(stroke, static_cast<int>(255.0f * alpha));
    const ImU32 ink = IM_COL32(255, 255, 255, static_cast<int>(255.0f * alpha));
    const float h = 18.0f;
    if (shape == 0) {           // sombrero
        draw->AddEllipseFilled(ImVec2(at.x + 18.0f, at.y + 2.0f), ImVec2(18.0f, 7.0f), f);
        draw->AddRectFilled(ImVec2(at.x, at.y), ImVec2(at.x + width, at.y + h), f, 4.0f);
        draw->AddRect(ImVec2(at.x, at.y), ImVec2(at.x + width, at.y + h), s, 4.0f);
    } else if (shape == 1) {    // instruccion, con su muesca
        draw->AddRectFilled(ImVec2(at.x, at.y), ImVec2(at.x + width, at.y + h), f, 4.0f);
        draw->AddRectFilled(ImVec2(at.x + 10.0f, at.y - 3.0f), ImVec2(at.x + 22.0f, at.y + 1.0f), f, 1.5f);
        draw->AddRect(ImVec2(at.x, at.y), ImVec2(at.x + width, at.y + h), s, 4.0f);
    } else if (shape == 2) {    // «si … entonces»: brazo, hueco y pie
        draw->AddRectFilled(ImVec2(at.x, at.y), ImVec2(at.x + width, at.y + h), f, 4.0f);
        draw->AddRectFilled(ImVec2(at.x + 10.0f, at.y - 3.0f), ImVec2(at.x + 22.0f, at.y + 1.0f), f, 1.5f);
        draw->AddRectFilled(ImVec2(at.x, at.y + h - 2.0f), ImVec2(at.x + 9.0f, at.y + h + 18.0f), f);
        draw->AddRectFilled(ImVec2(at.x, at.y + h + 16.0f), ImVec2(at.x + width * 0.6f, at.y + h + 25.0f), f, 3.0f);
        draw->AddRect(ImVec2(at.x, at.y), ImVec2(at.x + width, at.y + h), s, 4.0f);
        // El hueco hexagonal de la condicion, entre «si» y «entonces».
        const float sx = at.x + 6.0f + demoTextW(label ? label : "") + 6.0f, sw = slotW;
        const ImVec2 slot[6] = {{sx + 5.0f, at.y + 2.0f}, {sx + sw - 5.0f, at.y + 2.0f}, {sx + sw, at.y + 9.0f},
                                {sx + sw - 5.0f, at.y + 16.0f}, {sx + 5.0f, at.y + 16.0f}, {sx, at.y + 9.0f}};
        draw->AddConvexPolyFilled(slot, 6, ui::withAlpha(stroke, static_cast<int>(200.0f * alpha)));
        if (after && *after) draw->AddText(ui::fonts().semibold, kDemoFont, ImVec2(sx + sw + 6.0f, at.y + 3.0f), ink, after);
    } else {                    // valor de si o no
        const ImVec2 hex[6] = {{at.x + 6.0f, at.y}, {at.x + width - 6.0f, at.y}, {at.x + width, at.y + h * 0.5f},
                               {at.x + width - 6.0f, at.y + h}, {at.x + 6.0f, at.y + h}, {at.x, at.y + h * 0.5f}};
        draw->AddConvexPolyFilled(hex, 6, f);
        draw->AddPolyline(hex, 6, s, ImDrawFlags_Closed, 1.0f);
    }
    if (label && *label)
        draw->AddText(ui::fonts().semibold, kDemoFont, ImVec2(at.x + (shape == 3 ? 10.0f : 6.0f), at.y + 3.0f), ink, label);
}

// El cursor del mini video: una flecha blanca con borde; al pulsar, un anillo.
void drawDemoCursor(ImDrawList* draw, ImVec2 tip, bool pressed, float alpha) {
    const ImU32 white = IM_COL32(255, 255, 255, static_cast<int>(255.0f * alpha));
    const ImU32 black = IM_COL32(10, 10, 14, static_cast<int>(255.0f * alpha));
    if (pressed) draw->AddCircle(tip, 10.0f, ui::withAlpha(ui::color::Accent, static_cast<int>(220.0f * alpha)), 0, 2.0f);
    const float k = pressed ? 0.88f : 1.0f;
    const ImVec2 arrow[7] = {{tip.x, tip.y}, {tip.x, tip.y + 16.0f * k}, {tip.x + 4.0f * k, tip.y + 12.0f * k},
                             {tip.x + 7.0f * k, tip.y + 18.0f * k}, {tip.x + 9.5f * k, tip.y + 17.0f * k},
                             {tip.x + 6.5f * k, tip.y + 11.0f * k}, {tip.x + 11.5f * k, tip.y + 11.0f * k}};
    draw->AddConcavePolyFilled(arrow, 7, white);
    draw->AddPolyline(arrow, 7, black, ImDrawFlags_Closed, 1.2f);
}

// El mini video de la guia de bloques: un raton coge un bloque de la paleta,
// lo arrastra y lo encaja. Los bloques son los de verdad, con sus textos y sus
// colores. Se dibuja en vivo, sin archivos, y repite cada 4,2 s.
void drawBlocksDemo(const NoteLabApp& app, ImVec2 at, ImVec2 size, int variant) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool es = app.spanish;
    const DemoLabels L = demoLabels(es);
    const DemoWidths W = demoWidths(es);
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), ui::color::Window, 8.0f);
    draw->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), ui::color::Border, 8.0f);
    const float paletteW = W.palette;
    draw->AddRectFilled(ImVec2(at.x + 1.0f, at.y + 1.0f), ImVec2(at.x + paletteW, at.y + size.y - 1.0f), ui::color::Panel, 8.0f,
                        ImDrawFlags_RoundCornersLeft);
    draw->AddText(ui::fonts().semibold, 10.5f, ImVec2(at.x + 8.0f, at.y + 6.0f), ui::color::Faint, es ? "PALETA" : "PALETTE");
    draw->AddText(ui::fonts().semibold, 10.5f, ImVec2(at.x + paletteW + 8.0f, at.y + 6.0f), ui::color::Faint, es ? "LIENZO" : "CANVAS");
    // Los colores de Scratch 3 de cada categoria (los del editor).
    const ImU32 eventFill = IM_COL32(255, 191, 0, 255), eventStroke = IM_COL32(204, 153, 0, 255);
    const ImU32 gameFill = IM_COL32(255, 102, 128, 255), gameStroke = IM_COL32(230, 51, 85, 255);
    const ImU32 controlFill = IM_COL32(255, 171, 25, 255), controlStroke = IM_COL32(207, 139, 23, 255);
    const ImU32 opFill = IM_COL32(89, 192, 89, 255), opStroke = IM_COL32(56, 148, 56, 255);
    const ImVec2 hatAt(at.x + paletteW + 14.0f, at.y + 30.0f);
    const ImVec2 underHat(hatAt.x, hatAt.y + 18.0f);
    // Lo que ya esta en el lienzo, lo que se arrastra y adonde va.
    int shape = 1;
    ImU32 fill = gameFill, stroke = gameStroke;
    const char* label = L.health;
    const char* after = nullptr;
    float width = W.health, slotW = 0.0f;
    ImVec2 to = underHat;
    switch (variant) {
        case 0: shape = 0; fill = eventFill; stroke = eventStroke; label = L.hat; width = W.hat; to = hatAt; break;
        case 4: label = L.heal; break;
        case 2: shape = 2; fill = controlFill; stroke = controlStroke; label = L.ifWord; width = W.ifBlock; slotW = W.slot; after = L.thenWord; break;
        case 3:
            shape = 3; fill = opFill; stroke = opStroke; label = L.chance; width = W.chance;
            to = ImVec2(underHat.x + 6.0f + demoTextW(L.ifWord) + 8.0f, underHat.y);
            break;
        default: break;
    }
    const ImVec2 from(at.x + 8.0f, at.y + 40.0f);
    // Reloj del video.
    const double period = 4.2;
    const double clock = g_tutorial.demoClock >= 0.0 ? g_tutorial.demoClock : ImGui::GetTime();
    const double t = std::fmod(clock, period);
    const float fade = static_cast<float>(t < 3.6 ? 1.0 : std::max(0.0, (period - t) / 0.6));
    auto ease = [](double x) { x = std::clamp(x, 0.0, 1.0); return static_cast<float>(x * x * (3.0 - 2.0 * x)); };
    // Lo fijo: el evento (salvo cuando el video es ponerlo) y el «si» del valor.
    if (variant != 0) drawDemoBlock(draw, hatAt, W.hat, 0, eventFill, eventStroke, L.hat, fade);
    if (variant == 3) drawDemoBlock(draw, underHat, W.ifBlock, 2, controlFill, controlStroke, L.ifWord, fade, W.slot, L.thenWord);
    // El de la paleta, siempre ahi.
    drawDemoBlock(draw, from, width, shape, fill, stroke, label, 0.55f * fade, slotW, after);
    // El que se arrastra.
    const ImVec2 grab(14.0f, 9.0f);
    const ImVec2 rest(at.x + size.x - 30.0f, at.y + size.y - 26.0f);
    ImVec2 cursor = rest;
    ImVec2 block = from;
    bool pressed = false, snapped = false, carrying = false;
    if (t < 0.6) {
        const float k = ease(t / 0.6);
        cursor = ImVec2(rest.x + (from.x + grab.x - rest.x) * k, rest.y + (from.y + grab.y - rest.y) * k);
    } else if (t < 0.8) {
        cursor = ImVec2(from.x + grab.x, from.y + grab.y);
        pressed = true;
    } else if (t < 2.2) {
        const float k = ease((t - 0.8) / 1.4);
        block = ImVec2(from.x + (to.x - from.x) * k, from.y + (to.y - from.y) * k - std::sin(k * 3.14159f) * 10.0f);
        cursor = ImVec2(block.x + grab.x, block.y + grab.y);
        pressed = true;
        carrying = true;
        if (k > 0.75f) {        // la sombra de donde encajara
            draw->AddRect(ImVec2(to.x - 1.0f, to.y - 1.0f), ImVec2(to.x + width + 1.0f, to.y + 19.0f),
                          IM_COL32(255, 255, 255, 120), 4.0f, 0, 1.5f);
        }
    } else {
        block = to;
        snapped = true;
        const float k = ease((t - 2.2) / 0.8);
        cursor = ImVec2(to.x + grab.x + 26.0f * k, to.y + grab.y + 18.0f * k);
    }
    if (carrying) draw->AddRectFilled(ImVec2(block.x + 3.0f, block.y + 4.0f), ImVec2(block.x + width + 3.0f, block.y + 22.0f),
                                      IM_COL32(0, 0, 0, 70), 4.0f);
    drawDemoBlock(draw, block, width, shape, fill, stroke, label, fade, slotW, after);
    if (snapped && t < 2.6) {   // el destello del encaje
        const float flash = static_cast<float>(1.0 - (t - 2.2) / 0.4);
        draw->AddRect(ImVec2(block.x - 3.0f, block.y - 3.0f), ImVec2(block.x + width + 3.0f, block.y + 21.0f),
                      ui::withAlpha(ui::color::Success, static_cast<int>(255.0f * flash)), 5.0f, 0, 2.5f);
    }
    drawDemoCursor(draw, cursor, pressed, fade);
    // El rotulo de lo que se esta viendo.
    const char* caption = variant == 0 ? (es ? "Arrastra el evento al lienzo" : "Drag the event onto the canvas")
                        : variant == 3 ? (es ? "Suéltalo en el hueco" : "Drop it into the slot")
                                       : (es ? "Arrástralo hasta que encaje" : "Drag it until it snaps");
    draw->AddText(ui::fonts().ui, 12.0f, ImVec2(at.x + paletteW + 8.0f, at.y + size.y - 20.0f), ui::color::Muted, caption);
}

// Rellena `area` menos los huecos: bandas horizontales por cada borde de hueco.
void fillOutside(ImDrawList* draw, const ImRect& area, const std::vector<ImRect>& holes, ImU32 color) {
    std::vector<float> ys{area.Min.y, area.Max.y};
    for (const ImRect& hole : holes) {
        ys.push_back(std::clamp(hole.Min.y, area.Min.y, area.Max.y));
        ys.push_back(std::clamp(hole.Max.y, area.Min.y, area.Max.y));
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    for (size_t band = 0; band + 1 < ys.size(); ++band) {
        const float y0 = ys[band], y1 = ys[band + 1];
        if (y1 - y0 < 0.5f) continue;
        std::vector<std::pair<float, float>> spans;
        for (const ImRect& hole : holes)
            if (hole.Min.y <= y0 && hole.Max.y >= y1)
                spans.push_back({std::max(hole.Min.x, area.Min.x), std::min(hole.Max.x, area.Max.x)});
        std::sort(spans.begin(), spans.end());
        float x = area.Min.x;
        for (const auto& span : spans) {
            if (span.first > x) draw->AddRectFilled(ImVec2(x, y0), ImVec2(span.first, y1), color);
            x = std::max(x, span.second);
        }
        if (x < area.Max.x) draw->AddRectFilled(ImVec2(x, y0), ImVec2(area.Max.x, y1), color);
    }
}

// El mensaje junto al control: el de la mision o, si lo senalado es un paso
// concreto (un bloque de la paleta, su categoria, un hueco, un tipo nuevo), ese paso.
std::string tutorialCalloutText(const NoteLabApp& app, const TutorialMission& mission, const std::string& resolved) {
    const bool es = app.spanish;
    const std::string key = mission.key;
    auto say = [&](const char* en, const char* esText) { return std::string(es ? esText : en); };
    if (resolved == "pal:event.hit") return say("Drag this event onto the canvas (or click it).", "Arrastra este evento al lienzo (o haz clic en él).");
    if (resolved == "pal:do.health") return say("Drag «change health» under the event (or click it).", "Arrastra «cambiar vida» bajo el evento (o haz clic en él).");
    if (resolved == "pal:ctl.if") return say("Drag «if … then» under the event.", "Arrastra «si … entonces» bajo el evento.");
    if (resolved == "pal:op.chance") return say("Drop «chance of %» into the «if» slot.", "Suelta «probabilidad de %» en el hueco del «si».");
    if (resolved.rfind("palcat:", 0) == 0) return say("Open this category: the block is there.", "Abre esta categoría: ahí está el bloque.");
    if (resolved == "slot:do.health")
        return key == "bk-hurt" ? say("Click here and type a negative number, like -0.2.", "Haz clic aquí y escribe un número negativo, como -0.2.")
                                : say("Click here and type a positive number, like 0.2.", "Haz clic aquí y escribe un número positivo, como 0.2.");
    if (resolved == "catalog-create")
        return say("Create your note here: name, look (it can stay like the normal ones), sound and bot.",
                   "Crea aquí tu nota: nombre, aspecto (puede quedarse como las normales), sonido y bot.");
    if (resolved == "blocks-newtype" || resolved == "blocks-choose") {
        if (key.rfind("bk-", 0) == 0 && key != "bk-new" && tutorialGuideBlocked(app))
            return say("This note has your own blocks: the guide won't touch them. Create a new note here.",
                       "Esta nota ya tiene tus bloques: la guía no los toca. Crea aquí una nota nueva.");
        if (key == "bk-heal") return say("Create the «Healing» type here.", "Crea aquí el tipo «Curación».");
        if (key == "bk-if") return say("Create the «Luck» type here.", "Crea aquí el tipo «Suerte».");
        if (key != "bk-new") return say("First create a note here (or pick one above).", "Primero crea aquí una nota (o elige una arriba).");
    }
    if ((resolved == "types-views" || resolved == "tab-types") && key == "blocks")
        return say("Go to «Custom notes» → «Catalog»: notes are created there.", "Ve a «Notas custom» → «Catálogo»: ahí se crea la nota.");
    if ((resolved == "types-views" || resolved == "tab-types") && key != "blocks" && key != "custom-notes" && key != "distribute")
        return say("Go back to «Blocks» to continue.", "Vuelve a «Bloques» para seguir.");
    if (resolved == "distribute-song")
        return say("First pick a song: «Distribute» works on it.", "Primero elige una canción: «Distribuir» trabaja sobre ella.");
    if (resolved == "distribute-fill")
        return say("Press here: it scatters the mod's custom notes in the song.", "Pulsa aquí: reparte las notas custom del mod en la canción.");
    if (resolved == "parts-grid") return say("Click a piece.", "Haz clic en una pieza.");
    if (resolved == "piece-edit") return say("Change its scale or its FPS.", "Cambia su escala o sus FPS.");
    if (resolved == "ex-write" && key == "export") return say("Press here to export.", "Pulsa aquí para exportar.");
    return es ? mission.calloutEs : mission.calloutEn;
}

// El globo del modo foco: el mensaje de la mision junto al control, con su
// flecha. Ligero (sin la sombra): mas pequeno y sin la pista de abajo.
void drawTutorialCallout(const NoteLabApp& app, ImDrawList* draw, const ImRect& target, const ImRect& screen, int index,
                         const std::string& resolved, bool light) {
    const TutorialMission& mission = kTutorialMissions[index];
    const bool es = app.spanish;
    const std::string heading = std::string(mission.track == kTrackBlocks ? (es ? "GUÍA DE BLOQUES · " : "BLOCKS GUIDE · ")
                                            : mission.track >= kTrackHud ? "TUTORIAL · " : "") +
                                (es ? "MISIÓN " : "MISSION ") + std::to_string(tutorialNumber(index)) +
                                (mission.track >= kTrackHud ? " / " + std::to_string(tutorialTrackCount(mission.track)) : std::string());
    const std::string message = tutorialCalloutText(app, mission, resolved);
    const char* hint = light ? "" : (es ? "Un clic o Esc quita la sombra; el aviso se queda." : "A click or Esc removes the shade; the hint stays.");
    ImFont* bold = ui::fonts().semibold;
    ImFont* regular = ui::fonts().ui;
    const float wrap = light ? 230.0f : 260.0f;
    const float messageFont = light ? 14.0f : 16.0f;
    const ImVec2 headingSize = bold->CalcTextSizeA(11.5f, FLT_MAX, 0.0f, heading.c_str());
    const ImVec2 messageSize = bold->CalcTextSizeA(messageFont, FLT_MAX, wrap, message.c_str());
    const ImVec2 hintSize = *hint ? regular->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, hint) : ImVec2(0.0f, -6.0f);
    const float w = std::max({headingSize.x, messageSize.x, hintSize.x}) + 28.0f;
    const float h = headingSize.y + messageSize.y + hintSize.y + 34.0f;
    const float gap = 16.0f;
    bool below = target.Max.y + gap + h < screen.Max.y - 8.0f;
    float y = below ? target.Max.y + gap : target.Min.y - gap - h;
    if (!below && y < screen.Min.y + 8.0f) {   // ni arriba ni abajo: dentro, abajo del todo
        y = std::min(target.Max.y, screen.Max.y) - h - 12.0f;
        below = true;
    }
    const float centerX = (target.Min.x + target.Max.x) * 0.5f;
    const float x = std::clamp(centerX - w * 0.5f, screen.Min.x + 8.0f, screen.Max.x - w - 8.0f);
    const ImU32 bubble = light ? ui::withAlpha(ui::color::Accent, 235) : ui::color::Accent;
    draw->AddRectFilled(ImVec2(x + 2.0f, y + 4.0f), ImVec2(x + w + 2.0f, y + h + 4.0f), IM_COL32(0, 0, 0, light ? 60 : 90), 12.0f);
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bubble, 12.0f);
    const float arrowX = std::clamp(centerX, x + 18.0f, x + w - 18.0f);
    if (below && y >= target.Max.y) {
        const ImVec2 tri[3] = {{arrowX - 9.0f, y + 1.0f}, {arrowX + 9.0f, y + 1.0f}, {arrowX, y - 9.0f}};
        draw->AddTriangleFilled(tri[0], tri[1], tri[2], bubble);
    } else if (!below) {
        const ImVec2 tri[3] = {{arrowX - 9.0f, y + h - 1.0f}, {arrowX + 9.0f, y + h - 1.0f}, {arrowX, y + h + 9.0f}};
        draw->AddTriangleFilled(tri[0], tri[1], tri[2], bubble);
    }
    float line = y + 12.0f;
    draw->AddText(bold, 11.5f, ImVec2(x + 14.0f, line), IM_COL32(255, 255, 255, 190), heading.c_str());
    line += headingSize.y + 4.0f;
    draw->AddText(bold, messageFont, ImVec2(x + 14.0f, line), IM_COL32(255, 255, 255, 255), message.c_str(), nullptr, wrap);
    line += messageSize.y + 6.0f;
    if (*hint) draw->AddText(regular, 12.0f, ImVec2(x + 14.0f, line), IM_COL32(255, 255, 255, 170), hint);
}

int tutorialActiveTrack(const NoteLabApp& app) {
    if (tutorialBlocksGuideVisible(app)) return kTrackBlocks;
    return app.tutorialOpen ? kTrackGeneral : -1;
}

// Lo que se dibuja del foco de una mision, en `draw`: la sombra hasta el primer
// clic (o Esc), y despues el borde y el mensaje, que se quedan hasta cumplirla.
// Con el boton pulsado (arrastrando un bloque) solo el borde, para no tapar.
// Devuelve si se dibujo la sombra.
bool drawMissionFocus(NoteLabApp& app, int track, int current, ImDrawList* draw, const std::vector<ImRect>& guides, bool escape) {
    const TutorialMission& mission = kTutorialMissions[current];
    const size_t t = static_cast<size_t>(track);
    const double now = ImGui::GetTime();
    if (g_tutorial.focusKey[t] != mission.key) tutorialFocusMission(app, track, mission, now);
    tutorialBringIntoView(app, mission);
    ImRect target;
    std::string resolved;
    if (!tutorialTarget(app, mission, target, &resolved)) return false;
    const bool focusMode = app.tutorialFocus;
    bool dim = focusMode && !g_tutorial.focusDismissed[t];
    if (dim && now - g_tutorial.focusSince[t] > 0.6) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool overGuide = std::any_of(guides.begin(), guides.end(), [&](const ImRect& r) { return r.Contains(mouse); });
        const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (escape || (clicked && !overGuide)) {
            g_tutorial.focusDismissed[t] = true;
            dim = false;
        }
    }
    // Sin modo foco: solo el borde un momento, como antes.
    if (!focusMode && now > g_tutorial.pointUntil[t]) return false;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImRect screen(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y));
    const float pad = 6.0f;
    const ImRect hole(ImVec2(target.Min.x - pad, target.Min.y - pad), ImVec2(target.Max.x + pad, target.Max.y + pad));
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(now * 6.0));
    if (dim) {
        std::vector<ImRect> holes{hole};
        holes.insert(holes.end(), guides.begin(), guides.end());
        fillOutside(draw, screen, holes, IM_COL32(4, 5, 9, 170));
    }
    draw->AddRect(hole.Min, hole.Max, ui::withAlpha(ui::color::Accent, 150 + static_cast<int>(105.0f * pulse)), 8.0f, 0, 2.0f + 2.0f * pulse);
    const bool holding = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !dim;
    if (focusMode && !holding) {
        drawTutorialCallout(app, draw, hole, screen, current, resolved, !dim);
        g_tutorial.hintShown = true;
    }
    g_tutorial.dimShown = g_tutorial.dimShown || dim;
    return dim;
}

// El foco de una zona: dentro de su ventana (un modal), dibujado en la capa de
// delante, con la franja del tutorial y el control a la vista. Solo con la
// ventana de la zona arriba del todo: un desplegable o una pregunta lo apagan.
// Esc ya se atendio en la franja (antes de que la ventana lo use para cerrarse).
void drawAreaFocus(NoteLabApp& app) {
    const int track = g_tutorial.stripTrack;
    if (track < 0 || !g_tutorial.stripVisible || GImGui->OpenPopupStack.Size != 1) return;
    const int current = tutorialCurrent(app, track);
    if (current < 0) return;
    g_tutorial.areaFocusShown = drawMissionFocus(app, track, current, ImGui::GetForegroundDrawList(), {g_tutorial.stripRect}, false);
}

// Modo foco en la ventana principal: todo oscuro menos el control de la
// mision, el tutorial y la guia. Una ventana propia sin entrada (los clics la
// atraviesan), por encima de la principal.
void drawTutorialFocus(NoteLabApp& app) {
    g_tutorial.areaFocusShown = false;
    g_tutorial.dimShown = g_tutorial.hintShown = false;
    if (g_tutorial.area >= kTrackHud) {
        drawAreaFocus(app);
        return;
    }
    const int track = tutorialActiveTrack(app);
    if (track < 0) return;
    const int current = tutorialCurrent(app, track);
    if (current < 0) return;
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    // Sin NoBringToFrontOnFocus: ImGui crea esas ventanas al fondo de la pila y
    // la capa quedaria detras de la principal. Se trae al frente cada fotograma;
    // el tutorial y la guia se ven por sus huecos y los clics la atraviesan.
    ImGui::Begin("##tutorialfocus", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    std::vector<ImRect> guides;
    if (g_tutorial.windowVisible) guides.push_back(g_tutorial.windowRect);
    if (g_tutorial.cardVisible) guides.push_back(g_tutorial.cardRect);
    drawMissionFocus(app, track, current, ImGui::GetWindowDrawList(), guides, ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    ImGui::End();
}

// La tarjeta de una mision de la ruta general, dentro de su ventana.
void drawTutorialCard(NoteLabApp& app, int current, float width) {
    const bool es = app.spanish;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 cardAt = ImGui::GetCursorScreenPos();
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    ImGui::Dummy(ImVec2(width, 2.0f));
    ImGui::Indent(14.0f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 28.0f);
    if (current >= 0) {
        const TutorialMission& mission = kTutorialMissions[current];
        const bool isDone = app.tutorialDone.count(mission.key) > 0;
        const ImVec2 iconAt = ImGui::GetCursorScreenPos();
        const float bubble = 40.0f;
        draw->AddRectFilled(iconAt, ImVec2(iconAt.x + bubble, iconAt.y + bubble),
                            isDone ? ui::withAlpha(ui::color::Success, 60) : ui::color::AccentSoft, 12.0f);
        const char* glyph = isDone ? ui::icon::Check : mission.glyph;
        const ImVec2 glyphSize = ui::fonts().ui->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, glyph);
        draw->AddText(ui::fonts().ui, 20.0f, ImVec2(iconAt.x + (bubble - glyphSize.x) * 0.5f, iconAt.y + (bubble - glyphSize.y) * 0.5f),
                      isDone ? ui::color::Success : ui::color::Accent, glyph);
        ImGui::SetCursorScreenPos(ImVec2(iconAt.x + bubble + 12.0f, iconAt.y));
        ImGui::BeginGroup();
        ImGui::PushFont(ui::fonts().semibold, 11.5f);
        ImGui::TextColored(ui::vec(isDone ? ui::color::Success : ui::color::Muted), "%s",
                           ((es ? "MISIÓN " : "MISSION ") + std::to_string(tutorialNumber(current)) + (isDone ? (es ? "  ·  HECHA" : "  ·  DONE") : "")).c_str());
        ImGui::PopFont();
        ImGui::PushFont(ui::fonts().semibold, 19.0f);
        ImGui::TextUnformatted(es ? mission.titleEs : mission.titleEn);
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(iconAt.x, std::max(ImGui::GetCursorScreenPos().y, iconAt.y + bubble + 8.0f)));
        ImGui::TextUnformatted(es ? mission.goalEs : mission.goalEn);
        ImGui::Dummy(ImVec2(1.0f, 2.0f));
        drawTutorialSteps(es ? mission.stepsEs : mission.stepsEn);
        ImGui::Dummy(ImVec2(1.0f, 2.0f));
        // «Juega tu»: cuantos aciertos lleva (al tercero vuelve el modo automatico).
        if (std::string(mission.key) == "play" && app.manual && !isDone) {
            const std::string hits = std::string(es ? "Aciertos: " : "Hits: ") + std::to_string(std::min(3, tutorialHits(app))) + " / 3";
            ImGui::PushFont(ui::fonts().semibold, 15.0f);
            ImGui::TextColored(ui::vec(ui::color::Accent), "%s", ui::label(ui::icon::Game, hits).c_str());
            ImGui::PopFont();
        }
        if (!isDone && tutorialAlreadyDone(mission.key))
            ImGui::TextColored(ui::vec(ui::color::Success), "%s", ui::label(ui::icon::Check, tutorialText(app,
                "You already have this done: look at it and press «Next mission».", "Ya lo tienes hecho: repásalo y pulsa «Siguiente misión».")).c_str());
        const ImVec2 tipAt = ImGui::GetCursorScreenPos();
        draw->AddText(ui::fonts().ui, 15.0f, ImVec2(tipAt.x, tipAt.y + 1.0f), ui::color::Warning, ui::icon::Info);
        ImGui::SetCursorScreenPos(ImVec2(tipAt.x + 22.0f, tipAt.y));
        ImGui::BeginGroup();
        ImGui::TextColored(ui::vec(ui::color::Muted), "%s", es ? mission.tipEs : mission.tipEn);
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(1.0f, 4.0f));
        if (ui::primaryButton(ui::label(ui::icon::Eye, tutorialText(app, "Show me", "Muéstrame")), ImVec2(130.0f, 32.0f)))
            tutorialShow(app, mission);
        ImGui::SameLine(0.0f, 8.0f);
        if (!isDone) {
            if (ui::flatButton(tutorialAlreadyDone(mission.key) ? tutorialText(app, "Next mission", "Siguiente misión")
                                                                : tutorialText(app, "Skip mission", "Saltar misión"),
                               tutorialText(app, "Mark it as done and go to the next one.", "Márcala como hecha y pasa a la siguiente."),
                               ImVec2(130.0f, 32.0f))) {
                app.tutorialDone.insert(mission.key);
                g_tutorial.focus[0] = -1;
                if (!app.headless) saveSettings(app);
            }
        } else if (ui::flatButton(tutorialText(app, "Next mission", "Siguiente misión"), nullptr, ImVec2(150.0f, 32.0f))) {
            g_tutorial.focus[0] = -1;
        }
    } else {
        const ImVec2 iconAt = ImGui::GetCursorScreenPos();
        const float bubble = 48.0f;
        draw->AddRectFilled(iconAt, ImVec2(iconAt.x + bubble, iconAt.y + bubble), ui::withAlpha(ui::color::Warning, 60), 14.0f);
        const ImVec2 glyphSize = ui::fonts().ui->CalcTextSizeA(24.0f, FLT_MAX, 0.0f, ui::icon::Star);
        draw->AddText(ui::fonts().ui, 24.0f, ImVec2(iconAt.x + (bubble - glyphSize.x) * 0.5f, iconAt.y + (bubble - glyphSize.y) * 0.5f),
                      ui::color::Warning, ui::icon::Star);
        ImGui::SetCursorScreenPos(ImVec2(iconAt.x + bubble + 12.0f, iconAt.y + 2.0f));
        ImGui::BeginGroup();
        ImGui::PushFont(ui::fonts().semibold, 11.5f);
        ImGui::TextColored(ui::vec(ui::color::Warning), "%s", tutorialText(app, "TUTORIAL COMPLETE", "TUTORIAL COMPLETADO"));
        ImGui::PopFont();
        ImGui::PushFont(ui::fonts().semibold, 19.0f);
        ImGui::TextUnformatted(tutorialText(app, "Note master!", "¡Maestro de las notas!"));
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(iconAt.x, iconAt.y + bubble + 8.0f));
        ImGui::TextUnformatted(tutorialText(app,
            "You opened, played, created and exported. Next step: copy your export into the game's mods/ folder and play a song with it.",
            "Abriste, jugaste, creaste y exportaste. Siguiente paso: copia tu export a la carpeta mods/ del juego y juega una canción con él."));
        ImGui::Dummy(ImVec2(1.0f, 4.0f));
        if (ui::flatButton(ui::label(ui::icon::Help, tutorialText(app, "Open the help", "Abrir la ayuda")), nullptr, ImVec2(160.0f, 32.0f)))
            openHelp(app, 0);
    }
    ImGui::PopTextWrapPos();
    ImGui::Unindent(14.0f);
    ImGui::Dummy(ImVec2(width, 8.0f));
    const ImVec2 cardEnd(cardAt.x + width, ImGui::GetCursorScreenPos().y);
    draw->ChannelsSetCurrent(0);
    draw->AddRectFilled(cardAt, cardEnd, ui::color::Raised, 10.0f);
    draw->AddRectFilled(cardAt, ImVec2(cardAt.x + 4.0f, cardEnd.y), current >= 0 ? ui::color::Accent : ui::color::Warning, 10.0f,
                        ImDrawFlags_RoundCornersLeft);
    draw->ChannelsMerge();
}

void skipTutorial(NoteLabApp& app) {
    app.tutorialOpen = false;
    app.tutorialSeen = true;
    g_tutorial.focusDismissed[kTrackGeneral] = true;
    if (!app.headless) saveSettings(app);
    setStatus(app, "Tutorial skipped. Continue or repeat it from the Tutorial menu.",
                   "Tutorial saltado. Lo continúas o repites desde el menú Tutorial.");
}

// La ruta general, en su ventana flotante. Con el editor de bloques a la vista
// manda la guia de bloques y esta ventana espera; con una zona abierta, la
// franja de la zona.
void drawTutorialWindow(NoteLabApp& app) {
    if (!app.tutorialOpen || tutorialBlocksGuideVisible(app) || g_tutorial.area >= kTrackHud) return;
    const int current = tutorialCurrent(app, 0);
    const bool es = app.spanish;
    const int done = tutorialDoneCount(app, 0), total = tutorialTrackCount(0);
    // De entrada abajo, sobre el inspector: no tapa la vista previa (la
    // strumline del jugador) ni los botones de la bienvenida. Si ahi taparia lo
    // que pide la mision, se va a otra esquina (mientras no la hayan arrastrado).
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 work = viewport->WorkPos, workSize = viewport->WorkSize;
    struct Spot { ImVec2 at, pivot; };
    std::vector<Spot> spots;
    if (g_tutorial.panelValid[2]) spots.push_back({ImVec2(g_tutorial.panels[2].Max.x - 4.0f, g_tutorial.panels[2].Max.y - 4.0f), ImVec2(1.0f, 1.0f)});
    spots.push_back({ImVec2(work.x + workSize.x - 12.0f, work.y + workSize.y - 40.0f), ImVec2(1.0f, 1.0f)});
    if (g_tutorial.panelValid[1]) spots.push_back({ImVec2(g_tutorial.panels[1].Min.x + 6.0f, g_tutorial.panels[1].Max.y - 6.0f), ImVec2(0.0f, 1.0f)});
    spots.push_back({ImVec2(work.x + workSize.x - 12.0f, work.y + 84.0f), ImVec2(1.0f, 0.0f)});
    spots.push_back({ImVec2(work.x + 12.0f, work.y + workSize.y - 40.0f), ImVec2(0.0f, 1.0f)});
    Spot spot = spots.front();
    ImRect target;
    if (g_tutorial.anchored && current >= 0 && tutorialTarget(app, kTutorialMissions[current], target)) {
        const ImVec2 size = g_tutorial.windowRect.GetWidth() > 0.0f ? g_tutorial.windowRect.GetSize() : ImVec2(372.0f, 420.0f);
        target.Expand(8.0f);
        for (const Spot& candidate : spots) {
            const ImVec2 min(candidate.at.x - size.x * candidate.pivot.x, candidate.at.y - size.y * candidate.pivot.y);
            if (!ImRect(min, ImVec2(min.x + size.x, min.y + size.y)).Overlaps(target)) {
                spot = candidate;
                break;
            }
        }
    }
    const float room = spot.pivot.y > 0.5f ? spot.at.y - work.y - 8.0f : work.y + workSize.y - spot.at.y - 8.0f;
    ImGui::SetNextWindowPos(spot.at, g_tutorial.anchored ? ImGuiCond_Always : ImGuiCond_Appearing, spot.pivot);
    ImGui::SetNextWindowSizeConstraints(ImVec2(372.0f, 0.0f), ImVec2(372.0f, std::max(240.0f, room)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::vec(ui::color::Panel));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::vec(ui::withAlpha(ui::color::Accent, 140)));
    const std::string title = ui::label(ui::icon::Star, std::string("Tutorial  ·  ") + std::to_string(done) + " / " + std::to_string(total)) + "###tutorial";
    bool open = true;
    const bool visible = ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    // Arrastrarla la suelta del ancla y se queda donde la dejen.
    if (GImGui->MovingWindow == ImGui::GetCurrentWindow()) g_tutorial.anchored = false;
    g_tutorial.windowRect = ImRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                                                ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
    g_tutorial.windowVisible = true;
    if (visible) {
        const float width = 340.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        const int chapter = current >= 0 ? kTutorialMissions[current].chapter : kTutorialChapters - 1;
        ImGui::PushFont(ui::fonts().semibold, 12.5f);
        ImGui::TextColored(ui::vec(ui::color::Accent), "%s", ((es ? "NIVEL " : "LEVEL ") + std::to_string(chapter + 1) + "  ·  " +
            (es ? kTutorialChapterEs[0][chapter] : kTutorialChapterEn[0][chapter])).c_str());
        ImGui::PopFont();
        // Saltar el tutorial entero, siempre a la vista.
        const char* skipText = tutorialText(app, "Skip tutorial", "Saltar tutorial");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(skipText).x - 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
        if (ImGui::SmallButton(skipText)) skipTutorial(app);
        ImGui::PopStyleColor();
        ui::tooltip(tutorialText(app, "Close it and keep your progress; continue or repeat it from the Tutorial menu.",
                                      "Lo cierra y guarda tu progreso; lo continúas o repites desde el menú Tutorial."));
        drawTutorialProgress(app, 0, current, width);
        ImGui::Dummy(ImVec2(width, 4.0f));
        drawTutorialToast(app, 0, width);
        drawTutorialCard(app, current, width);
        ImGui::Dummy(ImVec2(width, 4.0f));
        if (ImGui::Checkbox(tutorialText(app, "Focus mode", "Modo foco"), &app.tutorialFocus) && !app.headless) saveSettings(app);
        ui::tooltip(tutorialText(app, "Darkens everything except what the mission uses, with its message next to it.",
                                      "Oscurece todo menos lo que usa la misión, con su mensaje al lado."));
        if (nlbuild::tutorialAudioPlayback) {
            ImGui::SameLine(0.0f, 18.0f);
            if (ImGui::Checkbox(tutorialText(app, "Sounds", "Sonidos"), &app.tutorialSounds) && !app.headless) saveSettings(app);
            ui::tooltip(tutorialText(app, "A soft chime for each mission and when a tutorial ends.",
                                          "Una campana suave por cada misión y al terminar un tutorial."));
        }
        // Todas las misiones, por capitulo; un clic ensena esa.
        ImGui::SetNextItemOpen(g_tutorial.listOpen, ImGuiCond_Always);
        g_tutorial.listOpen = ImGui::CollapsingHeader(tutorialText(app, "All missions", "Todas las misiones"));
        if (g_tutorial.listOpen) {
            // Altura fija con su propio desplazamiento: la ventana no se sale de la pantalla.
            ImGui::BeginChild("##missions", ImVec2(width, 230.0f), ImGuiChildFlags_None);
            int lastChapter = -1;
            for (int i = 0; i < kTutorialCount; ++i) {
                const TutorialMission& mission = kTutorialMissions[i];
                if (mission.track != 0) continue;
                if (mission.chapter != lastChapter) {
                    lastChapter = mission.chapter;
                    ImGui::PushFont(ui::fonts().semibold, 12.0f);
                    ImGui::TextColored(ui::vec(ui::color::Faint), "%s", ((es ? "NIVEL " : "LEVEL ") + std::to_string(mission.chapter + 1) + "  ·  " +
                        (es ? kTutorialChapterEs[0][mission.chapter] : kTutorialChapterEn[0][mission.chapter])).c_str());
                    ImGui::PopFont();
                }
                const bool isDone = app.tutorialDone.count(mission.key) > 0;
                ImGui::PushID(mission.key);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##mission", i == current, 0, ImVec2(ImGui::GetContentRegionAvail().x, 24.0f))) g_tutorial.focus[0] = i;
                if (i == current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY(0.5f);
                ImDrawList* rows = ImGui::GetWindowDrawList();
                rows->AddText(ui::fonts().ui, 14.0f, ImVec2(at.x + 6.0f, at.y + 4.0f),
                              isDone ? ui::color::Success : i == current ? ui::color::Accent : ui::color::Faint,
                              isDone ? ui::icon::Check : ui::icon::Circle);
                rows->AddText(ui::fonts().ui, 15.0f, ImVec2(at.x + 30.0f, at.y + 3.0f), isDone ? ui::color::Muted : ui::color::Text,
                              es ? mission.titleEs : mission.titleEn);
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::Dummy(ImVec2(1.0f, 4.0f));
            if (ui::flatButton(ui::label(ui::icon::Restart, tutorialText(app, "Repeat the tutorial", "Repetir el tutorial"))))
                startTutorial(app, kTrackGeneral, true);
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    if (!open) skipTutorial(app);
}

// El bundle de bloques del tutorial: la paleta solo ensena los bloques que usa
// la mision, y se van sumando paso a paso (la paleta tiene «Ver todos» para
// salir). Sin tutorial de bloques en marcha, la paleta entera.
void tutorialPaletteBundle(NoteLabApp& app) {
    std::vector<std::string> keys;
    if (tutorialGeneralActive(app)) {
        const std::string key = kTutorialMissions[tutorialCurrent(app, kTrackGeneral)].key;
        if (key == "blocks" || key == "blocks-snap") keys = {"event.hit", "do.health"};
    } else if (tutorialBlocksGuideVisible(app)) {
        const int current = tutorialCurrent(app, kTrackBlocks);
        const std::string key = current >= 0 ? kTutorialMissions[current].key : "";
        static const char* const steps[] = {"bk-new", "bk-event", "bk-hurt", "bk-heal", "bk-if", "bk-chance"};
        int step = -1;
        for (int i = 0; i < 6; ++i)
            if (key == steps[i]) step = i;
        if (step >= 0) keys.push_back("event.hit");
        if (step >= 2) keys.push_back("do.health");
        if (step >= 4) keys.push_back("ctl.if");
        if (step >= 5) keys.push_back("op.chance");
    }
    app.canvas.onlyBlocks = std::move(keys);
}

// Si una ruta puede ofrecerse ahora: nunca en las pruebas de interfaz, ni en
// las capturas salvo que se pida (--tutorial-ask=<zona>).
bool tutorialMayAsk(const NoteLabApp& app, int track) {
    if (g_tutorial.quiet) return false;
    if (g_tutorial.testing) return true;
    if (!g_tutorial.forceAsk.empty()) return g_tutorial.forceAsk == kTutorialAreas[track].key;
    return !app.headless;
}

// Lo contestado al aviso. Saltar no vuelve a preguntar: queda en el menu.
void tutorialAnswer(NoteLabApp& app, int track, bool accept) {
    const size_t t = static_cast<size_t>(track);
    if (track == kTrackGeneral) {
        app.tutorialSeen = true;
        app.tutorialOpen = accept;
        g_tutorial.askGeneral = false;
        g_tutorial.anchored = true;
    } else {
        app.tutorialAreas[kTutorialAreas[track].key] = accept ? 1 : 2;
        if (track == kTrackBlocks) {
            app.tutorialBlocksHidden = !accept;
            g_tutorial.cardCollapsed = false;
        }
    }
    if (g_tutorial.forceAsk == kTutorialAreas[track].key) g_tutorial.forceAsk.clear();
    g_tutorial.focusKey[t].clear();
    if (!app.headless) saveSettings(app);
    if (!accept)
        setStatus(app, "Skipped: you won't be asked again. It's in the Tutorial menu whenever you want it.",
                       "Saltado: no te lo volveremos a preguntar. Lo tienes en el menú Tutorial cuando quieras.");
}

// El aviso discreto de un tutorial: una franja de una linea arriba de su sitio
// (la ventana de la zona o el editor de bloques), sin tapar nada ni pararlo.
void drawTutorialOfferStrip(NoteLabApp& app, int track) {
    const TutorialArea& area = kTutorialAreas[track];
    const bool es = app.spanish;
    g_tutorial.lastAsk = area.key;
    // Sin sonido: la franja aparece al abrir una ventana y sonaria como el
    // boton que la abrio. Solo suena el aviso del primer arranque.
    g_tutorial.offered.insert(area.key);
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() + 12.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), ui::color::Raised, 8.0f);
    draw->AddRectFilled(at, ImVec2(at.x + 3.0f, at.y + height), ui::withAlpha(ui::color::Accent, 200), 8.0f, ImDrawFlags_RoundCornersLeft);
    ImGui::SetCursorScreenPos(ImVec2(at.x + 12.0f, at.y + 6.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ui::vec(ui::color::Accent), "%s", area.glyph);
    ImGui::SameLine(0.0f, 8.0f);
    const std::string text = std::string(es ? "¿Primera vez aquí? Tutorial «" : "First time here? Tutorial «") + (es ? area.nameEs : area.nameEn) +
                             "» · " + std::to_string(tutorialTrackCount(track)) + (es ? " misiones" : " missions");
    ImGui::TextColored(ui::vec(ui::color::Muted), "%s", text.c_str());
    ui::tooltip(es ? area.introEs : area.introEn);
    const char* startText = tutorialText(app, "Start", "Empezar");
    const char* skipText = tutorialText(app, "Skip", "Saltar");
    const float frame = ImGui::GetStyle().FramePadding.x * 2.0f;
    const float startW = ImGui::CalcTextSize(ui::label(ui::icon::Play, startText).c_str()).x + frame + 8.0f;
    const float skipW = ImGui::CalcTextSize(skipText).x + frame + 8.0f;
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x + 8.0f, at.x + width - 10.0f - startW - 6.0f - skipW), at.y + 6.0f));
    bool accept = false, skip = false;
    if (ui::primaryButton(ui::label(ui::icon::Play, startText), ImVec2(startW, 0.0f))) accept = true;
    g_tutorial.askAcceptRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SameLine(0.0f, 6.0f);
    if (ui::flatButton(skipText, tutorialText(app, "You won't be asked again; it's always in the Tutorial menu.",
                                                   "No te lo volveremos a preguntar; lo tienes siempre en el menú Tutorial."),
                       ImVec2(skipW, 0.0f)))
        skip = true;
    g_tutorial.askSkipRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + height));
    ImGui::Dummy(ImVec2(width, 6.0f));
    if (accept || skip) tutorialAnswer(app, track, accept);
}

// El aviso del primer arranque: una tarjeta pequena abajo a la derecha, no una
// ventana en mitad de la pantalla. Se puede seguir usando el programa con ella.
void drawGeneralOffer(NoteLabApp& app) {
    if (!g_tutorial.askGeneral) return;
    const TutorialArea& area = kTutorialAreas[kTrackGeneral];
    const bool es = app.spanish;
    g_tutorial.lastAsk = area.key;
    if (g_tutorial.offered.insert(area.key).second) tutorialSound(app, nlblocks::TutorialCue::Offer);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 14.0f, viewport->WorkPos.y + viewport->WorkSize.y - 42.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::vec(ui::color::Panel));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::vec(ui::withAlpha(ui::color::Accent, 120)));
    ImGui::Begin("##tutorialoffer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    const float inner = 300.0f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
    ImGui::PushFont(ui::fonts().semibold, 15.0f);
    ImGui::TextColored(ui::vec(ui::color::Accent), "%s", ui::label(ui::icon::Star, tutorialText(app, "First time in Note Lab?", "¿Primera vez en Note Lab?")).c_str());
    ImGui::PopFont();
    ImGui::TextColored(ui::vec(ui::color::Muted), "%s", es ? area.introEs : area.introEn);
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(1.0f, 2.0f));
    bool accept = false, skip = false;
    if (ui::primaryButton(ui::label(ui::icon::Play, tutorialText(app, "Start the tutorial", "Empezar el tutorial")), ImVec2(180.0f, 0.0f))) accept = true;
    g_tutorial.askAcceptRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SameLine(0.0f, 6.0f);
    if (ui::flatButton(tutorialText(app, "Skip", "Saltar"),
                       tutorialText(app, "You won't be asked again; it's always in the Tutorial menu.",
                                         "No te lo volveremos a preguntar; lo tienes siempre en el menú Tutorial."),
                       ImVec2(100.0f, 0.0f)))
        skip = true;
    g_tutorial.askSkipRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::End();
    if (accept || skip) tutorialAnswer(app, kTrackGeneral, accept);
}

// La guia de bloques, integrada arriba del editor de bloques: el bundle de
// tres notas basicas, con su mini video. Con la ruta general en marcha espera
// (la general sigue guiando aqui dentro); sin contestar, solo su aviso.
void drawBlocksGuide(NoteLabApp& app) {
    tutorialPaletteBundle(app);
    if (tutorialGeneralActive(app)) return;
    if (tutorialAreaState(app, kTrackBlocks) == 0) {
        if (tutorialMayAsk(app, kTrackBlocks)) drawTutorialOfferStrip(app, kTrackBlocks);
        return;
    }
    if (app.tutorialBlocksHidden || tutorialAreaState(app, kTrackBlocks) != 1) return;
    const bool es = app.spanish;
    const int current = tutorialCurrent(app, 1);
    const int done = tutorialDoneCount(app, 1), total = tutorialTrackCount(1);
    const TutorialBundle bundle = tutorialBundle(app);
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const char* skipText = tutorialText(app, "Skip guide", "Saltar guía");
    auto skipGuide = [&]() {
        app.tutorialBlocksHidden = true;
        g_tutorial.focusDismissed[kTrackBlocks] = true;
        if (!app.headless) saveSettings(app);
        setStatus(app, "Blocks guide hidden. Bring it back with «Guide» in the blocks bar.",
                       "Guía de bloques oculta. Vuelve con «Guía» en la barra de bloques.");
    };
    if (g_tutorial.cardCollapsed) {
        // Una linea: la mision que toca y volver a abrirla.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::vec(ui::color::Raised));
        ImGui::BeginChild("##blocksguide", ImVec2(width, ImGui::GetFrameHeight() + 14.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ui::vec(ui::color::Accent), "%s", ui::label(ui::icon::Star, tutorialText(app, "Blocks guide", "Guía de bloques")).c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::vec(ui::color::Muted), "%d / %d  ·  %s", done, total,
                           current >= 0 ? (es ? kTutorialMissions[current].titleEs : kTutorialMissions[current].titleEn)
                                        : tutorialText(app, "bundle complete", "bundle completo"));
        ImGui::SameLine();
        if (ImGui::SmallButton(tutorialText(app, "Expand", "Desplegar"))) g_tutorial.cardCollapsed = false;
        ImGui::SameLine();
        if (ImGui::SmallButton(skipText)) skipGuide();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        g_tutorial.cardRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        g_tutorial.cardVisible = true;
        return;
    }
    const ImVec2 demoSize = blocksDemoSize(app);
    const float pad = 12.0f;
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y + pad));
    // Izquierda: el mini video, o el icono de la mision.
    const ImVec2 left = ImGui::GetCursorScreenPos();
    const TutorialMission* mission = current >= 0 ? &kTutorialMissions[current] : nullptr;
    if (mission && mission->demo >= 0) {
        drawBlocksDemo(app, left, demoSize, mission->demo);
    } else {
        draw->AddRectFilled(left, ImVec2(left.x + demoSize.x, left.y + demoSize.y), ui::color::Window, 8.0f);
        const char* glyph = mission ? mission->glyph : ui::icon::Star;
        const ImVec2 glyphSize = ui::fonts().ui->CalcTextSizeA(44.0f, FLT_MAX, 0.0f, glyph);
        draw->AddText(ui::fonts().ui, 44.0f, ImVec2(left.x + (demoSize.x - glyphSize.x) * 0.5f, left.y + (demoSize.y - glyphSize.y) * 0.5f),
                      mission ? ui::color::Accent : ui::color::Warning, glyph);
    }
    // Derecha: nivel, progreso, bundle, la mision y sus botones.
    const float textX = left.x + demoSize.x + 18.0f;
    const float textW = at.x + width - pad - textX;
    ImGui::SetCursorScreenPos(ImVec2(textX, left.y));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
    const int chapter = mission ? mission->chapter : kTutorialChapters - 1;
    ImGui::PushFont(ui::fonts().semibold, 12.5f);
    ImGui::TextColored(ui::vec(ui::color::Accent), "%s", ((es ? "GUÍA DE BLOQUES  ·  " : "BLOCKS GUIDE  ·  ") +
        std::string(es ? kTutorialChapterEs[1][chapter] : kTutorialChapterEn[1][chapter])).c_str());
    ImGui::PopFont();
    // El bundle: las tres notas, hechas o por hacer.
    ImGui::SameLine(0.0f, 16.0f);
    const std::pair<bool, const char*> notes[3] = {
        {bundle.hurt, tutorialText(app, "Damage", "Daño")}, {bundle.heal, tutorialText(app, "Healing", "Curación")},
        {bundle.luck, tutorialText(app, "Luck", "Suerte")}};
    for (const auto& [ready, name] : notes) {
        const std::string chip = ui::label(ready ? ui::icon::Check : ui::icon::Circle, name);
        ui::pill(chip.c_str(), ready ? ui::color::Success : ui::color::Faint, ready);
        ImGui::SameLine(0.0f, 6.0f);
    }
    // Minimizar y saltar la guia, pegados a la derecha (medidos, no a ojo).
    const char* minimizeText = tutorialText(app, "Minimize", "Minimizar");
    const float pad2 = ImGui::GetStyle().FramePadding.x * 2.0f;
    const float buttonsW = ImGui::CalcTextSize(minimizeText).x + ImGui::CalcTextSize(skipText).x + pad2 * 2.0f + 4.0f;
    ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, textX + textW - buttonsW), ImGui::GetCursorScreenPos().y));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    if (ImGui::SmallButton(minimizeText)) g_tutorial.cardCollapsed = true;
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::SmallButton(skipText)) skipGuide();
    ImGui::PopStyleColor();
    drawTutorialProgress(app, 1, current, textW);
    ImGui::Dummy(ImVec2(1.0f, 2.0f));
    drawTutorialToast(app, 1, std::min(textW, 420.0f));
    if (mission) {
        ImGui::PushFont(ui::fonts().semibold, 17.0f);
        ImGui::TextUnformatted(((es ? "Misión " : "Mission ") + std::to_string(tutorialNumber(current)) + "  ·  " +
                                (es ? mission->titleEs : mission->titleEn)).c_str());
        ImGui::PopFont();
        ImGui::TextColored(ui::vec(ui::color::Muted), "%s", es ? mission->goalEs : mission->goalEn);
        drawTutorialSteps(es ? mission->stepsEs : mission->stepsEn, 18.0f);
        if (tutorialAlreadyDone(mission->key))
            ImGui::TextColored(ui::vec(ui::color::Success), "%s", ui::label(ui::icon::Check, tutorialText(app,
                "You already have this done: look at it and press «Next mission».", "Ya lo tienes hecho: repásalo y pulsa «Siguiente misión».")).c_str());
        if (ui::primaryButton(ui::label(ui::icon::Eye, tutorialText(app, "Show me", "Muéstrame")), ImVec2(130.0f, 30.0f)))
            tutorialShow(app, *mission);
        ImGui::SameLine(0.0f, 8.0f);
        if (ui::flatButton(tutorialAlreadyDone(mission->key) ? tutorialText(app, "Next mission", "Siguiente misión")
                                                             : tutorialText(app, "Skip mission", "Saltar misión"),
                           tutorialText(app, "Mark it as done and go to the next one.", "Márcala como hecha y pasa a la siguiente."),
                           ImVec2(130.0f, 30.0f))) {
            app.tutorialDone.insert(mission->key);
            g_tutorial.focus[1] = -1;
            if (!app.headless) saveSettings(app);
        }
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::AlignTextToFramePadding();
        if (std::string(mission->key) != "bk-new" && tutorialGuideBlocked(app))
            ImGui::TextColored(ui::vec(ui::color::Warning), "%s",
                               ui::label(ui::icon::Warning, tutorialText(app, "This note has your own blocks: the guide won't touch them. Create a new one with «+».",
                                                                         "Esta nota ya tiene tus bloques: la guía no los toca. Crea una nueva con «+».")).c_str());
        else
            ImGui::TextColored(ui::vec(ui::color::Faint), "%s", ui::label(ui::icon::Info, es ? mission->tipEs : mission->tipEn).c_str());
    } else {
        ImGui::PushFont(ui::fonts().semibold, 17.0f);
        ImGui::TextUnformatted(tutorialText(app, "Bundle complete!", "¡Bundle completo!"));
        ImGui::PopFont();
        ImGui::TextColored(ui::vec(ui::color::Muted), "%s", tutorialText(app,
            "Damage, healing and luck: your three basic notes. Put them in a song with «Distribute» and play them.",
            "Daño, curación y suerte: tus tres notas básicas. Ponlas en una canción con «Distribuir» y juégalas."));
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    const float bottom = std::max(left.y + demoSize.y, ImGui::GetItemRectMax().y) + pad;
    draw->ChannelsSetCurrent(0);
    draw->AddRectFilled(at, ImVec2(at.x + width, bottom), ui::color::Raised, 10.0f);
    draw->AddRectFilled(at, ImVec2(at.x + 4.0f, bottom), mission ? ui::color::Accent : ui::color::Warning, 10.0f, ImDrawFlags_RoundCornersLeft);
    draw->ChannelsMerge();
    ImGui::SetCursorScreenPos(ImVec2(at.x, bottom));
    ImGui::Dummy(ImVec2(width, 8.0f));
    g_tutorial.cardRect = ImRect(at, ImVec2(at.x + width, bottom));
    g_tutorial.cardVisible = true;
}

// En la barra de bloques, con la guia oculta: «Guia» la vuelve a poner (no
// mientras manda la ruta general ni mientras su aviso esta a la vista).
void drawBlocksGuideButton(NoteLabApp& app) {
    if (!app.tutorialBlocksHidden || tutorialGeneralActive(app) || tutorialAreaState(app, kTrackBlocks) == 0) return;
    ImGui::SameLine(0.0f, 6.0f);
    if (ui::flatButton(ui::label(ui::icon::Star, tutorialText(app, "Guide", "Guía")),
                       tutorialText(app, "The blocks guide: a bundle of three basic notes, step by step.",
                                         "La guía de bloques: un bundle de tres notas básicas, paso a paso."))) {
        app.tutorialBlocksHidden = false;
        app.tutorialAreas[kTutorialAreas[kTrackBlocks].key] = 1;
        g_tutorial.cardCollapsed = false;
        if (!app.headless) saveSettings(app);
    }
}

// ------------------------------------------------------------- zonas --

// La franja del tutorial arriba de la ventana de una zona: la mision, sus pasos
// y sus botones. Esc con la sombra puesta solo la quita, no cierra la ventana.
// Sirve tambien para la mision de la ruta general que se hace dentro de la zona.
void drawTutorialStrip(NoteLabApp& app, int track) {
    const size_t t = static_cast<size_t>(track);
    if (g_tutorial.areaFocusShown && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        g_tutorial.focusDismissed[t] = true;
        ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##tutorialesc"), ImGuiInputFlags_LockThisFrame);
    }
    g_tutorial.stripTrack = track;
    const TutorialArea& area = kTutorialAreas[track];
    const bool es = app.spanish;
    const int current = tutorialCurrent(app, track);
    const int done = tutorialDoneCount(app, track), total = tutorialTrackCount(track);
    const TutorialMission* mission = current >= 0 ? &kTutorialMissions[current] : nullptr;
    const float width = ImGui::GetContentRegionAvail().x;
    const float pad = 10.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    // Cabecera: la zona, el progreso y los botones a la derecha.
    const float rowY = at.y + pad;
    ImGui::SetCursorScreenPos(ImVec2(at.x + pad + 6.0f, rowY + 3.0f));
    ImGui::PushFont(ui::fonts().semibold, 12.5f);
    ImGui::TextColored(ui::vec(ui::color::Accent), "%s", ui::label(area.glyph, std::string("TUTORIAL  ·  ") + (es ? area.nameEs : area.nameEn)).c_str());
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, rowY + 8.0f));
    drawTutorialProgress(app, track, current, 120.0f);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, rowY + 3.0f));
    ImGui::TextColored(ui::vec(ui::color::Muted), "%d / %d", done, total);
    const float headerEnd = ImGui::GetItemRectMax().x;
    const char* skipText = tutorialText(app, "Skip tutorial", "Saltar tutorial");
    const char* showText = tutorialText(app, "Show me", "Muéstrame");
    const char* nextText = mission && tutorialAlreadyDone(mission->key) ? tutorialText(app, "Next mission", "Siguiente misión")
                                                                         : tutorialText(app, "Skip mission", "Saltar misión");
    const char* closeText = tutorialText(app, "Close tutorial", "Cerrar tutorial");
    const float frame = ImGui::GetStyle().FramePadding.x * 2.0f;
    const float showW = 116.0f, nextW = 116.0f;
    const float skipW = ImGui::CalcTextSize(mission ? skipText : closeText).x + frame;
    const float buttonsW = (mission ? showW + nextW + 16.0f : 0.0f) + skipW;
    float buttonsX = at.x + width - pad - buttonsW;
    // «¡Mision cumplida!» de esta ruta, junto a los botones.
    const double since = ImGui::GetTime() - g_tutorial.toastAt;
    if (!g_tutorial.toastKey.empty() && since >= 0.0 && since < 3.5)
        for (const TutorialMission& finished : kTutorialMissions)
            if (g_tutorial.toastKey == finished.key && finished.track == track) {
                const std::string pill = ui::label(ui::icon::Check, std::string(tutorialText(app, "Mission complete! +1", "¡Misión cumplida! +1")));
                const float pillW = ImGui::CalcTextSize(pill.c_str()).x + 20.0f;
                if (buttonsX - pillW - 10.0f > headerEnd + 10.0f) {
                    ImGui::SetCursorScreenPos(ImVec2(buttonsX - pillW - 10.0f, rowY + 2.0f));
                    ui::pill(pill.c_str(), ui::color::Success, true);
                }
            }
    buttonsX = std::max(buttonsX, headerEnd + 12.0f);
    ImGui::SetCursorScreenPos(ImVec2(buttonsX, rowY - 2.0f));
    if (mission) {
        if (ui::primaryButton(ui::label(ui::icon::Eye, showText), ImVec2(showW, 0.0f))) tutorialShow(app, *mission);
        ImGui::SameLine(0.0f, 8.0f);
        if (ui::flatButton(nextText, tutorialText(app, "Mark it as done and go to the next one.", "Márcala como hecha y pasa a la siguiente."),
                           ImVec2(nextW, 0.0f))) {
            app.tutorialDone.insert(mission->key);
            g_tutorial.focus[t] = -1;
            if (!app.headless) saveSettings(app);
        }
        g_tutorial.stripNextRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::SameLine(0.0f, 8.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::color::Muted));
    const bool leave = ImGui::Button(mission ? skipText : closeText, ImVec2(skipW, 0.0f));
    g_tutorial.stripLeaveRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::PopStyleColor();
    if (mission) ui::tooltip(tutorialText(app, "Hide it; you won't be asked again. Bring it back from the Tutorial menu.",
                                               "Lo oculta y no vuelve a preguntar. Lo recuperas desde el menú Tutorial."));
    const float headerBottom = ImGui::GetItemRectMax().y;
    // Cuerpo: la mision a la izquierda y sus pasos a la derecha.
    const float bodyY = headerBottom + 8.0f;
    const float leftX = at.x + pad + 6.0f;
    const float leftW = std::min(380.0f, (width - pad * 2.0f) * 0.42f);
    float bottom = bodyY;
    if (mission) {
        ImGui::SetCursorScreenPos(ImVec2(leftX, bodyY));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + leftW);
        ImGui::PushFont(ui::fonts().semibold, 16.5f);
        ImGui::TextUnformatted(((es ? "Misión " : "Mission ") + std::to_string(tutorialNumber(current)) + "  ·  " +
                                (es ? mission->titleEs : mission->titleEn)).c_str());
        ImGui::PopFont();
        ImGui::TextColored(ui::vec(ui::color::Muted), "%s", es ? mission->goalEs : mission->goalEn);
        if (tutorialAlreadyDone(mission->key))
            ImGui::TextColored(ui::vec(ui::color::Success), "%s", ui::label(ui::icon::Check, tutorialText(app,
                "Already done: press «Next mission».", "Ya lo tienes hecho: pulsa «Siguiente misión».")).c_str());
        ImGui::TextColored(ui::vec(ui::color::Faint), "%s", ui::label(ui::icon::Info, es ? mission->tipEs : mission->tipEn).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        bottom = std::max(bottom, ImGui::GetItemRectMax().y);
        const float stepsX = leftX + leftW + 18.0f;
        draw->AddLine(ImVec2(stepsX - 9.0f, bodyY + 2.0f), ImVec2(stepsX - 9.0f, bodyY + 60.0f), ui::color::Border);
        ImGui::SetCursorScreenPos(ImVec2(stepsX, bodyY));
        ImGui::BeginGroup();
        const float right = at.x + width - pad;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (right - stepsX));
        drawTutorialSteps(es ? mission->stepsEs : mission->stepsEn, 18.0f);
        ImGui::Dummy(ImVec2(1.0f, 1.0f));   // los pasos acaban moviendo el cursor: el grupo cierra sobre un elemento
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        bottom = std::max(bottom, ImGui::GetItemRectMax().y);
    } else {
        ImGui::SetCursorScreenPos(ImVec2(leftX, bodyY));
        ImGui::BeginGroup();
        ImGui::PushFont(ui::fonts().semibold, 16.5f);
        ImGui::TextColored(ui::vec(ui::color::Warning), "%s", ui::label(ui::icon::Star, tutorialText(app, "Zone complete!", "¡Zona completada!")).c_str());
        ImGui::PopFont();
        ImGui::TextColored(ui::vec(ui::color::Muted), "%s", tutorialText(app,
            "You know this window now. This strip closes by itself; repeat it from the Tutorial menu.",
            "Ya conoces esta ventana. Esta franja se cierra sola; la repites desde el menú Tutorial."));
        ImGui::EndGroup();
        bottom = std::max(bottom, ImGui::GetItemRectMax().y);
    }
    bottom += pad;
    draw->ChannelsSetCurrent(0);
    draw->AddRectFilled(at, ImVec2(at.x + width, bottom), ui::color::Raised, 10.0f);
    draw->AddRectFilled(at, ImVec2(at.x + 4.0f, bottom), mission ? ui::color::Accent : ui::color::Warning, 10.0f, ImDrawFlags_RoundCornersLeft);
    draw->ChannelsMerge();
    ImGui::SetCursorScreenPos(ImVec2(at.x, bottom));
    ImGui::Dummy(ImVec2(width, 6.0f));
    g_tutorial.stripRect = ImRect(at, ImVec2(at.x + width, bottom));
    g_tutorial.stripVisible = true;
    if (leave) {
        if (track == kTrackGeneral) {
            skipTutorial(app);
            return;
        }
        app.tutorialAreas[area.key] = 2;
        g_tutorial.focusDismissed[t] = true;
        if (!app.headless) saveSettings(app);
        if (mission)
            setStatus(app, "Tutorial skipped: you won't be asked again. It's in the Tutorial menu.",
                           "Tutorial saltado: no te lo volveremos a preguntar. Lo tienes en el menú Tutorial.");
    }
}

// Al principio de la ventana de una zona: su aviso la primera vez y, si se
// hace, su franja. Con la ruta general en marcha la zona espera, y dentro de
// Exportar va la mision de exportar de la general. Tambien deja dicho que la
// zona esta abierta (para el foco).
void tutorialZone(NoteLabApp& app, int track) {
    g_tutorial.area = track;
    if (g_tutorial.quiet) return;
    if (tutorialGeneralActive(app)) {
        const int current = tutorialCurrent(app, kTrackGeneral);
        if (track == kTrackExport && std::string(kTutorialMissions[current].key) == "export") drawTutorialStrip(app, kTrackGeneral);
        return;
    }
    const int state = tutorialAreaState(app, track);
    if (state == 0) {
        if (tutorialMayAsk(app, track)) drawTutorialOfferStrip(app, track);
        return;
    }
    if (state == 1) drawTutorialStrip(app, track);
}

// ------------------------------------------------------------- menu --

// Empezar o seguir un tutorial desde el menu; «desde el principio» borra lo
// hecho de esa ruta. Elegir otro con la ruta general en marcha la deja en
// pausa (se sigue desde el menu). Las zonas abren su ventana si se puede.
void startTutorial(NoteLabApp& app, int track, bool fromStart) {
    const size_t t = static_cast<size_t>(track);
    if (fromStart) {
        for (const TutorialMission& mission : kTutorialMissions)
            if (mission.track == track) {
                app.tutorialDone.erase(mission.key);
                g_tutorial.signals.erase(mission.key);
            }
        g_tutorial.focus[t] = -1;
        g_tutorial.toastKey.clear();
        const TutorialBundle bundle = tutorialBundle(app);
        for (const TutorialMission& mission : kTutorialMissions)
            if (mission.track == track && tutorialMet(app, mission.key, bundle)) g_tutorial.preMet.insert(mission.key);
    }
    g_tutorial.focusKey[t].clear();
    g_tutorial.focusDismissed[t] = false;
    g_tutorial.finishedAt[t] = -1.0;
    if (track == kTrackGeneral) {
        app.tutorialOpen = true;
        app.tutorialSeen = true;
        g_tutorial.askGeneral = false;
        g_tutorial.anchored = true;
    } else {
        if (tutorialGeneralActive(app)) {
            app.tutorialOpen = false;
            setStatus(app, "«First steps» paused: continue it from the Tutorial menu.", "«Primeros pasos» en pausa: lo sigues desde el menú Tutorial.");
        }
        app.tutorialAreas[kTutorialAreas[track].key] = 1;
    }
    if (track == kTrackBlocks) {
        app.tutorialBlocksHidden = false;
        g_tutorial.cardCollapsed = false;
        if (!app.sources.empty()) {
            app.requestedTab = 1;
            app.typesView = 1;
        } else {
            setStatus(app, "Open a mod: the blocks guide lives in Custom notes → Blocks.",
                           "Abre un mod: la guía de bloques está en Notas custom → Bloques.");
        }
    }
    if (track == kTrackHud) openCreateHud(app);
    if (track == kTrackCustom) openCustomCreator(app);
    if (track == kTrackResources) {
        int source = app.selSource;
        if (source < 0 || source >= static_cast<int>(app.sources.size())) source = app.sources.empty() ? -1 : 0;
        if (source >= 0) openModResources(app, source, app.blocksSource == source ? app.blocksType : std::string(), -1, -1, ResourceKind::Image);
        else setStatus(app, "Open a mod first: its images, videos and sounds are listed there.",
                            "Abre un mod antes: ahí salen sus imágenes, vídeos y sonidos.");
    }
    if (track == kTrackExport) openExport(app);
    if (track == kTrackSprite) {
        if (!app.sources.empty()) {
            openCreateHud(app);
            app.createHud.useDrawn = true;
            app.createHud.step = 3;
            openHudSpriteEditor(app, DrawnPiece::Note, false);
            // El tutorial empieza en la hoja con huecos: sus misiones van ahi.
            app.spriteEditor.choosing = false;
        } else {
            setStatus(app, "Open a mod: the sprite editor opens from Create HUD or a note's look.",
                           "Abre un mod: el editor de sprites se abre desde Crear HUD o el aspecto de una nota.");
        }
    }
    if (!app.headless) saveSettings(app);
}

// El menu «Tutorial» de la barra de arriba: cada tutorial, con su progreso;
// repetir cualquiera desde el principio; volver a preguntar y saltar. Siempre
// esta, se haya saltado o no.
void drawTutorialMenu(NoteLabApp& app) {
    const char* title = tutorialText(app, "Tutorial", "Tutorial");
    if (g_tutorial.openMenu && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    if (!ImGui::BeginMenu(title)) return;
    const bool es = app.spanish;
    auto label = [&](int track) {
        const TutorialArea& area = kTutorialAreas[track];
        const int done = tutorialDoneCount(app, track), total = tutorialTrackCount(track);
        return ui::label(area.glyph, std::string(es ? area.nameEs : area.nameEn) + "  (" + std::to_string(done) + "/" + std::to_string(total) + ")" +
                                         (done == total ? "  ✓" : ""));
    };
    for (int track = 0; track < kTutorialTracks; ++track) {
        if (track == kTrackHud) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", tutorialText(app, "Zones of the program", "Zonas del programa"));
        }
        const bool active = track == kTrackGeneral ? app.tutorialOpen
                          : track == kTrackBlocks ? !app.tutorialBlocksHidden && tutorialAreaState(app, track) == 1
                                                  : tutorialAreaState(app, track) == 1;
        ImGui::PushID(track);
        const bool finished = tutorialDoneCount(app, track) == tutorialTrackCount(track);
        if (ImGui::MenuItem(label(track).c_str(), nullptr, active)) startTutorial(app, track, finished);
        ui::tooltip(es ? kTutorialAreas[track].introEs : kTutorialAreas[track].introEn);
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::BeginMenu(ui::label(ui::icon::Restart, tutorialText(app, "Repeat from the start", "Repetir desde el principio")).c_str())) {
        for (int track = 0; track < kTutorialTracks; ++track) {
            ImGui::PushID(track);
            if (ImGui::MenuItem(ui::label(kTutorialAreas[track].glyph, es ? kTutorialAreas[track].nameEs : kTutorialAreas[track].nameEn).c_str()))
                startTutorial(app, track, true);
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(tutorialText(app, "Ask again when entering each zone", "Volver a preguntar al entrar en cada zona"), nullptr, false,
                        !app.tutorialAreas.empty())) {
        app.tutorialAreas.clear();
        if (!app.headless) saveSettings(app);
        setStatus(app, "Each zone will ask again the next time you enter it.", "Cada zona volverá a preguntar la próxima vez que entres.");
    }
    if (nlbuild::tutorialAudioPlayback && ImGui::MenuItem(tutorialText(app, "Tutorial sounds", "Sonidos del tutorial"), nullptr, app.tutorialSounds)) {
        app.tutorialSounds = !app.tutorialSounds;
        if (!app.headless) saveSettings(app);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tutorialText(app, "Skip the tutorial", "Saltar el tutorial"), nullptr, false, app.tutorialOpen)) skipTutorial(app);
    ImGui::EndMenu();
}

// Todo el tutorial, al final de cada fotograma.
void drawTutorial(NoteLabApp& app) {
    updateTutorial(app);
    drawTutorialWindow(app);
    drawTutorialFocus(app);
    drawGeneralOffer(app);
}

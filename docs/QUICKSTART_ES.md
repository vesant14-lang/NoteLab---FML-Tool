# Note Lab 1.0 — inicio rápido

Extrae el ZIP completo. Ejecuta `NoteLab.exe` con `SDL3.dll` al lado.
Requiere Windows x64 y un controlador con OpenGL 3.3. EN/ES cambia el idioma.

Esta build está silenciada al 100 %, sin interruptor para activar sonido.
La canción avanza visualmente y las animaciones funcionan; también puedes
importar y exportar OGG. Ni la preview, ni el editor, ni la audición de archivos
producen audio. Cambiar preferencias antiguas no lo reactiva.

## Crear desde cero

En inicio, pulsa **Crear notas custom**. Importa tus PNG, XML/TXT, secuencias
o audio. Inspecciona el archivo antes de asignarlo. Para una hoja sin metadata,
define celdas o recortes. Asigna cada dirección, sostenido, receptor, splash
y recurso del HUD; revisa FPS, orden, escala y offsets. Confirma para añadir
el estilo al proyecto. Ranking visible: imágenes, no el antiguo creador de texto.

## Combinar varias hojas o HUD

Abre el mod o varias fuentes y elige el estilo que quieres editar.
En Inspector, **Combinar estilos / HUD** permite elegir otra fuente y copiar
solo los grupos marcados. Por ejemplo, notas de una hoja, receptores de otra
y juicios/cifras de una tercera. Repite la combinación para cada fuente.
Las demás piezas no se borran. Deshacer recupera la composición anterior.
Si la fuente viene de otro mod o ZIP, se conservan copias en la cache propia.

Las hojas parciales usan los receptores base en la preview. Para que una mezcla
forme parte del export, aplícala con **Combinar estilos / HUD**: ese respaldo
visual por sí solo no modifica el estilo. Exportar un tipo de nota no sustituye
los receptores del motor; elige un skin completo para hacerlo.

## Notas custom y charts

En Notas custom puedes editar bloques/código, configurar el bot de preview
y repartir los tipos en una canción. Usa **Guardar distribución como chart**
para guardar una copia o reemplazar explícitamente el original con backup.
No se reemplazan charts dentro de ZIP ni del juego base. Un cambio externo
del original bloquea la escritura.

## Guardar y exportar

Ctrl+S guarda `.fmlnote`; Ctrl+E prepara un paquete para Codename, Psych o
V-Slice. Lee los avisos y su guía antes de copiarlo a un mod de prueba.
La verificación de estructura no certifica ejecución de todos los scripts.
La preview no ejecuta código arbitrario, ni exporta toda su composición de HUD.

Los proyectos todavía dependen de las carpetas originales y de la cache en
`%LOCALAPPDATA%/FunkinNoteLab`. Conserva ambas. Si falta una fuente, abrir el
proyecto se bloquea y se conserva el trabajo actual. No es un paquete portable.

Si la ventana es estrecha, el Inspector pasa a una pestaña del centro.
Los paneles se redimensionan arrastrando sus separadores. La preview y el
inspector avanzado admiten pan con botón central y zoom donde está indicado.

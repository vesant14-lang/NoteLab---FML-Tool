# Note Lab change log

## 1.0.4a — Paint and save protection · 2026-10-07

- A pending recovery is kept separately from the current autosave. Opening
  another mod, saving a different project or restarting does not overwrite it.
  Recovering an older session also preserves the displaced current session.
  Recovery remains available from File after opening a mod and asks before
  replacing unsaved work.
- Custom-note creation and Save in mod reject names that map to another
  note's export files, including spaces versus hyphens, case-only aliases and
  Windows filename sanitization. Saved block comments now carry their note's
  identity, so an old or stale catalog cannot silently replace another note's
  script. Existing block comments remain readable; an unidentified existing
  file still requires explicit replacement approval.
- A note's saved-state comparison includes its complete visual definition,
  not just its look ID. Editing scale, animation settings, frame definitions
  or other look properties marks the note as changed and includes it in the
  save prompt. Looks imported from a mod are also included when saving.
- Regression coverage includes the three target formats, legacy saved notes,
  owner mismatches, visual changes and multiple pending recovery copies.
- Paint's Shape tool starts with a rectangle and offers 12 labeled thumbnails:
  rectangle, rounded rectangle, ellipse/circle, triangle, diamond, star, arrow,
  heart, pentagon, hexagon, trapezoid and cross. Draw by dragging or use Place
  shape to insert one in the selection/chosen box on a new, undoable layer.
  Outline thickness and pixel-art rendering are configurable per shape; the
  note arrow keeps its standard outline. Existing note and HUD presets remain.
- Documentation corrects Save in Blocks versus project saving, the new-note
  Ready flow, explicit resource copying and recovery. README, guides and
  developer notes remove outdated workflow claims and internal process text.
- The full advanced guide is included in English and Spanish, with 42 chapters,
  offline screenshots and instructions for the new shape controls.

## 1.0.4 — public release · 2026-10-07

This release adds in-mod note saving, editable demo patterns and tutorial
navigation. Export formats are unchanged from 1.0.3.

- Your notes live in your mod. «Save» on a note writes it into the open mod
  folder, where its engine looks for it (Psych `custom_notetypes/`, Codename
  `data/notes/`, V-Slice `scripts/notekinds/`, plus its own look), and its
  blocks travel inside the same script as a comment the game ignores: open the
  mod again — without a project, even on another PC — and the note comes back
  with its blocks. Before writing, the files are listed; an existing file is
  only replaced if you say so. Never into a ZIP or the base game install. If
  the script was edited by hand afterwards — anywhere in the file, also below
  Note Lab's comment — Note Lab says so and offers to turn the script into
  blocks or to use the saved blocks again, instead of using stale ones silently;
  saving over the edited script asks first. Its overview still lists what the
  saved blocks did.
- Closing with a note not saved in the mod offers
  «Save in the mod», and a copy of the session is kept so the next start can
  offer «Recover the last session».
- Clicking one of your own custom notes (in «Your notes») shows its overview —
  look, behavior, whether it is saved in the mod, and its script in the code of
  the open mod's engine — instead of jumping to Blocks, the same as the mod's
  own notes already did. «To blocks» sits in the script section, next to «Copy
  code».
- «Go to the first» switches to the Preview at once, a little before the note,
  so you see it arrive.
- «Add screamer…» is gone from the resources panel (the screamer block stays).
- Tutorial: starting or continuing a tutorial jumps over the missions you have
  already done (a mod open, a song loaded…) and starts at the first one left;
  «Open a mod» cannot be skipped; missions that ask for a new note guide you
  inside the «New custom note» window (its name, then «Create note», then its
  blocks) — before, any open window switched the guide off. «Save your note in
  the mod» replaces the export mission of First steps and of the blocks guide,
  and points inside the «Save in the mod» window. «Repeat from the start» asks
  for everything again except what it needs to begin (a mod open and a style
  picked). «Scatter custom notes» now points straight at «Populate with this
  mod's custom notes»: it works on the demo pattern too.
- Tutorial: any mission already passed — done, skipped by hand or jumped over
  at the start — can be done again: click it in «All missions», or click its box
  in the progress bar of any tutorial. Note Lab takes you to where it is done
  (the tab, the view or the step of «Create HUD») and nothing of your work
  changes. Missions passed without doing them show as «skipped». If what a
  mission asks is already true, it counts when you do it again (another style,
  another song…) or with «Next mission». «Open a mod» is the only one that is
  not repeated while a mod is open.
- Preview: seven demo patterns instead of one — Basic, Stairs, Jacks, Chords,
  Long holds, Stream and Random — picked from the song list, which now reads
  «Pattern: <name>». «Edit pattern…» opens a grid (opponent above, player
  below, one column per sixteenth, 1 to 16 bars): click a box to put a note,
  click it again to take it away, drag right to make it a hold; «Start from…»
  copies a built-in pattern. Your patterns (up to 32) are saved by name in your
  preferences, appear in the same list, play at the preview's BPM and can be
  edited or deleted later.
- The demo pattern counts as a chart for «Distribute»: the mod's custom notes
  can be scattered in it with the same package, seed and filters, the preview
  and «Play» use them, and changing the BPM keeps what was scattered (another
  pattern clears it, like another song). It is only for trying them, so there
  is no «Save distribution as chart…» on a demo pattern; a song of the mod can
  still be picked right there. A note's overview says «In 6 notes of the demo
  pattern «Basic»» and «Go to the first» works there too.
- Packages (already applied to the 1.0.3 delivery, same executable): the
  development log stays out of both ZIPs, and the public ZIP keeps the guide,
  the quickstart and their screenshots but not the developer notes (core sync,
  module map, future ideas, release checks), which stay in the Developer ZIP.
  The README, the quickstart and the guide no longer link to files a package
  does not carry: they name the developer notes as part of the Developer ZIP.

## 1.0.3 — public release · 2026-10-06

UI and workflow improvements. The note-editing core and export formats are
unchanged from 1.0.2.

- Sprite editor: before drawing, «How do you want to start?» offers the
  template with a box for each piece, a free canvas of the size you choose
  (16×16 to 256×256 or any size up to 512, with a pixel-art option) or the
  canvas of one piece at its game size (and «Continue» with what you had). A
  free canvas goes to the general sheet with «To the general sheet»; the arrow
  next to it picks the piece and the box. Another size is fitted to the box
  without distortion; pixel art grows by whole pixels, crisp.
- Sprite editor controls: right click paints with the outline color, Alt +
  click picks a color with any tool, `[` `]` brush size (Shift: hardness), `0`
  whole sheet, `1` 100 %, `F` the chosen box, mirror top-bottom or left-right
  while painting, a pixel-art brush with no smoothing and a pixel grid up
  close, crisp pixels when zoomed, a shortcuts help button. Presets and shapes
  are crisp on a pixel-art canvas.
- The note look editor is quieter: «File» groups start again, load from the
  style, open and save drawings and the final PNG; the right panel shows the
  note with its hold «In the game» and the four directions without scrolling.
- New custom note: options as cards with icons; one «Create note» button saves
  it in the Catalog (Your notes) and the window turns into «Ready» with the next
  steps — edit its blocks or code, give it a look, set up its bot — instead of
  opening the sprite editor by itself. From Blocks it goes straight to its
  blocks. «Your notes» rows have buttons to edit, give a look and set up the bot.
- Blocks: a second row for the open note — Save (the project), Save as (another
  note, a block program, its code or the project), To code (turn this note into
  code, Ctrl+Z undoes it, or make a code copy), To blocks for code-only notes,
  Look and Bot — with «Unsaved changes» / «Saved». The view switch has icons.
- Create note HUD goes step by step: Base, Colors, Details and Pieces, with
  back and next; the tutorial points at the step that holds its control.
  Pieces are cards with their thumbnail (drawn or the style's) and a click
  opens the editor on that piece; choosing «Drawn by me» no longer opens the
  editor by itself.
- Preview receptors are automatic: a style that has its own shows them when
  you pick it, even if another's were shown before.
- The text ranking creator («Ranking with text», File → Create ranking HUD ·
  Text) is hidden in this build, with its help topic; ranking from images is
  unchanged.
- Tutorial sounds are silenced in this build: no
  mission, ending or offer sound plays, their «Sounds» switch is hidden (in the
  tutorial window and the Tutorial menu) and neither package contains sound
  files. Block editor sounds stay off as before; songs and file audition keep
  their audio. For when they return, a mission only chimes if it is the one
  the tutorial is showing, and zone offers no longer chime when a window opens.
- New capture flags: `--sprite-start[=0..3]`, `--sprite-free=WxH[p]`,
  `--new-note-done`, `--hud-step=N`, `--catalog`. `--ui-test=notes` grows to 28
  checks (start screen, free canvas into the sheet, «Ready», HUD steps and
  piece cards).
- Fixed: zoomed-in and pixel-art canvases in the sprite editor were drawn
  smoothed. The bundled ImGui OpenGL3 backend binds its own linear sampler and
  ignored the texture's filter; the canvas now asks for its nearest sampler.
- Fixed labels: «1 statement became a block» in the script-to-blocks report,
  and «To the general sheet» in the hints that still used the old name.
- Core tests pass when the kit lives in a folder with accents (for example a
  Windows user named «José»): the fixture writer turned UTF-8 paths into ANSI
  ones and 65 checks failed. Only the test helper was wrong; the app already
  opened mods from such folders.
- Test scripts: `test.bat` runs the core tests by full path (it failed where
  Windows does not search the current folder); `test-ui.ps1` and
  `capture-demo.ps1` read exit codes reliably in Windows PowerShell 5.1, and the
  UI total is counted from the logs instead of being fixed in the script.
- Release closure: 2,997 passing core/workflow/UI checks (714 + 2,148 + 135)
  plus the missing-program guard, all on the final executable; 32 fresh native
  captures for the README and guides (now 41 chapters); a relocated Developer
  build and an extracted-public startup check; per-file and archive SHA-256.

## 1.0.2 — public release · 2026-10-05

- Fixed automatic song repeats hiding tap notes and the heads of long notes.
  When audio ends before the chart's trailing padding, playback now resets
  chart hit/miss state with the audio. Multiple laps, both scroll directions,
  pause/resume at the end and manual-run results have regression coverage.
- Script-to-block exports warn about empty code lines and original code that
  belongs to another engine, instead of silently implying an executable action.
- Public-release metadata is consistently 1.0.2. The source manifest includes
  the new editor/assistant/tutorial files; the package builder refuses a
  mismatched binary. Documentation covers the new workflows with native captures.
- Release closure: 2,991 passing core/workflow/UI checks plus the missing-program
  guard, fresh documentation and screenshots, a clean relocated Developer build,
  extracted-public startup checks and per-file/archive SHA256 manifests.
- Psych Engine 1.0 charts (`format: "psych_v1"`, also `psych_v1_convert`) now
  put every note on its real side. They store absolute lanes — 0–3 player,
  4–7 opponent — and `mustHitSection` only moves the camera (Psych
  `PlayState.hx:1355`; Codename reads them the same way, `Chart.hx:68`). The
  reader applied the classic rule, so about half of the notes of every Psych
  1.0 base-game chart played on the wrong strumline in the preview, «Play»,
  distribution side filters and the bot (15,864 of 33,185 notes across the 97
  charts Psych 1.0 ships; now 0).
- Saving a distribution back into a `psych_v1` chart now matches its notes
  instead of refusing, and never rewrites an 8+ lane as if it were a packed
  note type.
- Charts exported for Psych keep classic lanes and no longer claim
  `psych_v1`, so Psych 0.7 reads them natively and Psych 1.0 / Codename convert
  them themselves. Regression tests compare against each engine's own rule, not
  only against Note Lab's reader.
- In-app tutorial, like a game's: missions that tick themselves when you really
  do the task (nothing is a separate demo, nothing writes into the mod).
  «First steps» (12 missions in a floating window: open, play, create, ship),
  the blocks guide built into the blocks editor (a bundle of three basic notes —
  damage, healing and luck — with a live mini video of a mouse dragging and
  snapping blocks) and one tutorial per main zone, as a strip at the top of its
  window: Create note HUD, Custom creator & ranking, Mod resources and Export
  (4 missions each).
- Before each tutorial a discreet card or strip offers starting or skipping; skipping never
  asks again. Every tutorial stays in the new «Tutorial» menu with its progress,
  «Repeat from the start» for any of them and «Ask again when entering each zone».
- Focus mode: darkens everything except the control the mission uses, with the
  mission's message next to it; a click or Esc removes it (Esc inside a zone
  does not close the window) and «Show me» brings it back.
- New custom note flow. «Create custom note…» in the Catalog (and «+» in
  Blocks) opens one window with everything a note needs: name, what it does
  (blocks, or code only), what it starts with, its look (like the normal notes
  by default: no sprite of its own required; paint it, your images, or draw
  it), a sound when hit and its bot; then it takes you to its blocks or its
  code. Notes created in Note Lab are listed in the Catalog under «Your notes».
- Blocks does nothing until a note is chosen: it no longer picks one by itself
  and offers «Create a new note…» or «Code only…».
- «Code only» notes: their script starts with the basics for the mod's engine
  (the same events the blocks use) and «Save code» keeps it with the project,
  ready for charts and exports.
- Sprite editor: a small drawing editor for a note's look, like a paint
  program — layers (visibility, opacity, reorder, merge down), brush and
  eraser with size, hardness and opacity, shapes by dragging (rectangle,
  rounded, circle, triangle, diamond, star, arrow, heart) with fill and
  outline, paint bucket with tolerance, picker, palette with the base game's
  arrow colors, undo/redo and templates. The left note is drawn; the other
  three are rotated from it. From the new-note window, «Change its look» and
  the blocks «…» menu.
- Tutorial sounds: a soft chime per mission, a short arpeggio when a tutorial
  ends and a light tap when one is offered. Synthesized in the program, with
  their own switch (window and Tutorial menu); block editor sounds stay off.
- Tutorials can be repeated for real: on «Repeat from the start» what is
  already true (a mod open, a song loaded…) no longer ticks by itself — the
  mission says so and «Next mission» moves on. Clicking a finished tutorial
  in the menu repeats it.
- «Distribute» without a chart lets you pick the song right there, or load
  the one from before after going back to the test pattern; its mission points
  at that and says nothing needs saving.
- New `--ui-test=notes` (22 checks); `--ui-test=tutorial` grows to 24.
- Create note HUD: «Pieces — Drawn by me». Note, receptor (at rest, pressed
  and on hit), hold piece, hold end and splash can each be drawn; what isn't
  drawn stays the style's. Drawings are tinted with each direction's color
  (whatever colors the drawing has), note and receptor are rotated from the
  left one, pressed/hit receptors and a one-frame splash animate by themselves,
  and receptors can take the note's shape. On creation everything drawn goes
  into one Sparrow atlas with the base game's names; the recipe keeps each
  piece and its frames, so «Create HUD…» on that style reopens them.
- The sprite editor works on the whole sheet: a row per piece and boxes for its
  frames (the timeline: play, FPS, onion skin, duplicate/delete/move frames).
  Tool palette with drawn icons and shortcuts (brush, eraser, shape, fill,
  picker, range selection, move), Shift + click draws a straight line from the
  last point, zoom presets and wheel, a map of the sheet for big ones, region
  recomposition while painting, copy/cut/paste and «paste in a free box»,
  presets (one piece or the whole HUD) in a new layer, inside the selection or
  the chosen box, «Load from the style», «Open sheet…» (PNG + XML/TXT, images,
  GIF) and tabs: loose drawings of a piece that go into the general sheet, the
  chosen box apart, and other sheets (with previews) to copy from.
- «Final PNG»: the file with everything together as it will be saved — the
  whole HUD or only the drawn pieces — with each frame's box and name on hover,
  and «Save PNG + XML…». File → «Save note sheet (PNG + XML)…» writes any style
  in one sheet with the base game's names.
- Note drawings (.nlsprite): a file with every tab, layer (name, visibility,
  opacity), frame and setting. «Save drawing…» / «Open sheet…»; using a drawing
  also keeps it with the project, so its layers come back after closing Note
  Lab (this removes the old «layers only last the session» limit).
- «My colors» in the editor's palette: add the main color with «+», remove it
  with a right click; kept in the preferences.
- The note look editor (from «Create custom note…», «Change its look» or the
  blocks «…» menu) is only for that note: note and hold rows, no HUD sheets or
  HUD presets.
- Blocks: «Turn its script into blocks…» reads the type's script (the mod's or
  a code-only note's) and turns what it can into blocks — health, score,
  misses, combo, sound, shake, flash, «Hey!», messages and Psych note
  properties (hitHealth, missHealth, ignoreNote…) — under the right event
  (Psych goodNoteHit/opponentNoteHit/noteMiss/onCreate, Codename onPlayerHit/
  onDadHit/onNoteHit/onPlayerMiss, V-Slice NoteKind methods). The rest stays,
  verbatim and in place, as new «code» blocks (only in that engine), and what
  hangs from no event is listed. Never over existing work: a type that already
  has blocks gets a new «<type> (blocks)» note. The mod is not touched.
- Blocks «…» menu: «Save block program (.nlblocks)…», «Open block program…»
  (into an empty type, or as new stacks beside the existing ones) and «Save its
  code in a folder…» (with the paths the engine expects).
- The blocks guide never builds on a note with the author's own blocks: it
  asks for a new note instead, and says so.
- Preview: «Receptors:» shows the notes with another style's receptors, only in
  the preview.
- Mod resources: sounds are split into Effects, Songs (Inst/Voices under
  songs/, grouped by song) and Music, the same in the three engines; effects
  first.
- New tutorial zone «Sprite editor» (7 missions) and «Draw your pieces
  (optional)» in Create note HUD.
- Fixed (older than this version): when several pieces share one atlas region
  and are painted differently — the Psych 0.7/1.0 RGB template gives the same
  region to the four hold pieces and the four hold ends, `chip` to the four
  directions' hit frames — Create HUD and «Change its look» painted all of them
  with the first one's color. Each look now gets its own copy under the sheet
  and the atlas is rewritten (only Sparrow; a grid or strip cell stays with the
  first piece). Live preview, the created style, the project and every export
  follow it.
- V-Slice export: a receptor hit drawn as one frame is written with two (same
  region). With one, V-Slice turns the receptor off after the hit
  (`active = isAnimationDynamic`, `StrumlineNote.hx:143`) and the opponent's
  and the bot's receptors never went back to rest.
- Tested in the three engines with real play (Psych 1.0.4, Codename 1.0.1,
  V-Slice 0.8.6): a Create HUD style (Psych RGB template painted; drawn pieces
  in Codename and V-Slice), a custom note with blocks («Hey!», flash and shake)
  and a drawn look, and Bopeebo with that note distributed. Test mods are not
  included in the release packages.
- New command-line flags for these tests: `--save-chart=<file>` saves the
  distribution as a separate chart (like «Save as a separate chart…»), and with
  `--commit-create` the requested `--save-project` and `--export-to` wait until
  the creation is confirmed.
- Tutorial improvements:
  - «First steps» now leads the way: while it runs, the blocks guide and the
    zone tutorials wait instead of interrupting it, and it keeps guiding inside
    the blocks editor (new mission «Snap your first blocks»).
  - The palette shows a tutorial bundle — only the blocks the mission uses,
    added step by step, without the category grid — with «Show all» to leave
    it. Missions point at the exact block (scrolling the palette to it), the
    value slot to edit or «+» for a new type, not at a category.
  - The mini video uses the real blocks, with their real texts and values.
  - Focus: the first click (or Esc) removes the shade, but the outline and
    the hint stay until the mission is done; holding the mouse (dragging) hides
    the hint. The inspector scrolls to the control the mission needs, and the
    tutorial window moves to another corner if it would cover it.
  - «Play it yourself» asks for three hits, shows the count, then puts the
    preview back on auto and goes on.
  - Offers are discreet: a small card at the first start and a one-line strip
    in each zone and in the blocks editor, instead of a window in the middle.
  - A finished tutorial closes itself after a short celebration.
- Esc no longer closes a whole window while you are typing in one of its
  fields (it cancels the field, as expected) or while a dropdown, color picker or
  question is open on top. Affects Create HUD, a type's look, the text ranking,
  the custom creator, export, import, help and the other dialogs.
- The custom creator asks before discarding imported resources and assigned
  roles that were not applied (Cancel or Esc); reopening it starts from scratch.
- The text ranking HUD is reachable again: File → Create ranking HUD · Text…
  and «Ranking with text…» under the HUD in the inspector (it was only reachable
  by dropping a font). The help already pointed there.
- Spanish UI: the custom creator's validation and import messages, its resource
  kinds and «Splashes» in Combine styles are translated; chart-save errors show
  in the interface language.
- UI tests (`--ui-test`) start from default preferences and never save them,
  and captures (`--capture`) never save them: results no longer depend on what
  was stored (a code panel saved at its minimum width failed `--ui-test=code`),
  and their flags no longer leak into the user's preferences.
- `test-ui.ps1` includes the expanded 24-check tutorial flow and the 22-check
  new-note/editor flow alongside the earlier editing and resource checks.

## 1.0.1 — public-build polish · 2026-10-04

- Removed the 30-second resource/import audition cutoff. Shared audio controls
  add opt-in full-file looping, playback speed, stereo balance and an A–B loop
  range. Preview-only settings do not alter files, song playback or exported
  block properties; stop, pause/resume and reset remain explicit.
- Media libraries moved from above the block canvas into the left sidebar:
  Mods / Resources, Library / In use, categorized counters, search, readable
  filename cards and explicit found/missing/disconnected states. Context actions
  and the screamer shortcut remain available; switching panels preserves editing.
- General image/video/sound library, explicit image/audio inspection, system
  video preview and assignment to typed resource slots. Nothing autoplays.
- Custom resource import with destination selection and confirmed folder
  creation: project-only by default, optional writable-mod copy, no overwrite.
- Image overlay, image-plus-sound screamer and conditional video blocks with
  duration, fit, opacity/volume and cooldown. Referenced PNG/OGG Vorbis/MP4 files
  accompany connected note-type exports; missing, unsafe, unsupported or colliding
  dependencies stop publication. Manual-code dependencies are not scanned.
- Guarded image decoding rejects excessive dimensions before allocating pixels.
- Illustrated advanced Markdown guide covering creation, HUD, charts, blocks,
  resources, code and engine-specific export with real base-game screenshots.
- Block-editor interaction cues stay disabled, without an enable switch;
  legacy preferences cannot reactivate them. The earlier blanket mute was
  corrected: song playback, volume, pause/seeking and imported-file audition
  are restored. OGG import/export is unchanged. Legacy zero volume is migrated
  once; new user-selected volume and deliberate mute persist independently.
- Original supplied Note Lab logo in welcome, window icon, executable icon and
  README. Multi-size Windows icon generation uses that image, not new artwork.
- README refreshed with badges, navigation, real base-game screenshots and
  clearer creation, composition, code, installation and limitations guidance.
- Cancelling creation from an empty welcome returns to welcome without leaving
  an empty source. Existing projects are preserved.
- Failed/cancelled opening after choosing Don't save no longer clears the
  current workspace's unsaved marker prematurely.
- Advanced creation explains incompatible image/audio roles and prevents
  assignment of unreadable resources or unnamed sound effects.
- ZIP export now creates missing destination directories and atomically
  replaces existing ZIPs on Windows, including accented paths.
- Expanded creation/export/code tests and isolated native-engine test tooling;
  game installations and private fixtures are excluded from both deliveries.

For exact test results and in-engine limits, see `docs/RELEASE_CHECKS.md`.

## 1.0.0 — first public build · 2026-10-03

### Creation and editing

- Basic painting and advanced file-based creation: images, XML/TXT sheets,
  PNG sequences, GIF input, grid/manual crops, ordered frames and OGG assignments.
- Image ranking/countdown, splashes, holds and named audio effects.
- Custom-note block editor, editable syntax-colored code, bounded
  code-to-block synchronization, saved comments, presets and undo/redo.
- Deterministic custom-note distribution, exact quantities and preview bot profiles.
- Protected native-chart copy/replacement with backups and external-change checks.

### First-public-build polish

- Removed the musical-note tile from welcome; added direct custom-note and
  image-ranking creation and a full-width first-use workspace.
- Compact windows keep editing controls in an Inspector tab instead of
  crushing the preview between fixed side panels.
- Inspector action buttons wrap to the available width rather than getting clipped.
- Combine notes/receptors/splashes/covers/ranking/countdown/sounds independently
  from several styles, including other mods and ZIPs.
- Partial note sheets use base receptors in preview; Codename receptors can
  resolve explicit multikey names. No Voiid-only exception is used.
- Sheet deduplication now preserves distinct scale, offsets, alpha, grid and palette metadata.
- Closing edited sources asks before discarding; closing an unrelated source
  preserves the selected style and its undo/redo history.
- Failed reloads preserve the previous source and edits; reload-all restores
  the selected style when it still exists.
- Project source preparation happens before replacing the current workspace.
  Missing/unreadable sources and malformed/oversized projects preserve current work.
- Unique private temporary project files; atomic replacement, repeat saves
  and Unicode paths. Existing unrelated `.tmp` files are not reused.
- Bounded project/preferences parsing and safe numeric defaults.
- Self-contained developer kit, MIT/dependency notices, clean packaging,
  module map, test scripts and a standalone development log.

### Scope

Exports are structurally verified, not certified in-game. Preview does not
execute arbitrary mod scripts or all block actions. Full HUD layout export,
portable project bundles and assisted source/cache reconnection remain future work.

## Developer package layout — 2026-10-04

- Reorganized the developer delivery into a standalone Note Lab repository:
  `src/` for the UI, `core/` for the 14 note-editing modules and `support/` for
  required shared helpers only. Other FML applications and labs are not included.
- Adapted relative includes and build/test/release scripts; kept behavior and
  original namespaces compatible with the shared implementation.
- Added `test-layout.ps1` and `FML_SYNC_MAP.json` for self-contained source checks
  and reviewed transfer of improvements to the suite.
- The revised public binary and developer kit are packaged together from this
  same standalone source snapshot; earlier deliveries remain recoverable.

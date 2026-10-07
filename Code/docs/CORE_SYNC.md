# Keeping Note Lab and FML in sync

This standalone repository derives its note-editing core from FML. Its folder
layout and relative includes are adapted for Note Lab; release-specific changes
are listed below. Synchronization requires reviewing those changes, not copying
the standalone over the suite.

| Standalone path | Original FML path |
| --- | --- |
| `core/` | `src/fml_notelab/` |
| `src/` | `NoteLab/src/` |
| `support/core/`, `support/io/` | `src/fml_core/`, `src/fml_io/` |
| `support/formats/` | `src/fml_formats/` |
| `support/runtime/`, `support/render/` | `src/fml_runtime/`, `src/fml_render/` |
| `support/audio/` | `src/fml_audio/` and the Vorbis implementation helper |
| `tests/CoreRegression.cpp` | `src/tools/fml_notelabcheck.cpp` |

`FML_SYNC_MAP.json` gives the exact per-file mapping and original baseline hashes.
Paths and namespaces are not a claim that the other FML tools are included.
When returning an improvement, review the behavior diff, translate its local
include paths using the mapping, and preserve the suite's original layout.
Do not copy the whole standalone repository over FML.

1. Change model/read/export/persistence rules in the shared core.
2. Add a core regression. Add a workflow test when rendering or UI orchestration matters.
3. Verify Note Lab and the suite against the same source revision.
4. Create a release snapshot through the package script, not a forked copy of a parser/generator.

First-public-build core changes:

- `NoteStyle`: exact sheet identity includes visual metadata; component
  composition preserves unselected groups and remaps every copied sheet binding.
- `NotePreview`: opt-in receptor fallback preserves separate note/receptor
  sheets and respects intentionally hidden valid receptors.
- `NoteStyleRead`: missing Codename receptor prefixes can use names explicitly
  declared in the mod's four-key `multikeyData.xml`, without a mod-name special case.
- `NoteProject`: bounded decoding, safe numeric defaults and unique private
  temporary files for atomic Unicode-aware project writes.
- `NoteExport`: create missing ZIP destination parents and use atomic Unicode
  replacement on Windows, including repeat exports to the same file.
- `NoteResources`: winner-provider image/video/sound discovery, typed safe paths,
  connected-block usage, canonical engine keys and bounded non-overwriting
  imports. These rules are independent of the standalone panels.
- `NoteBlocks` / `NoteMediaEmit.hpp`: typed image/video arguments and bounded
  image, screamer and conditional-video code generation for the selected engine.
- `NoteExport`: package active media dependencies with format/path/size/collision
  preflight. Hand-written scripts are not statically scanned for dependencies.
- `NoteImage`: check compressed input size and decoded pixel limits before allocation.

Host-level changes live only in `src`: welcome/compact layout,
composition mounting, safer source closing/reloading and transactional project
source preparation. FML can reuse these core changes without importing
the independent window implementation. The revised 1.0.1 audio policy is host-only:
`src/BuildPolicy.hpp` enables general playback and disables only block-editor
cues, without changing the shared `fml_audio` implementation or muting the suite.
The resource sidebar, cards and explicit preview/import dialogs are host UI;
FML can consume the resource/export modules without adopting their positioning.

`SOURCE_MANIFEST.json` in the developer delivery records the exact shipped
files and SHA-256 values. For moving improvements back to a different FML
checkout, compare its baseline first, merge only related changes and rebuild
the actual consumers. A packaged snapshot is not an automatic suite release.

## 1.0.4a

The Paint host now provides twelve rasterized figures, remembered outline/pixel
options per figure, a thumbnail picker and placement on a new layer. The
geometry belongs to `SpriteShapes.hpp`; picker, placement and undo orchestration
belong to `SpriteEditor.hpp`. `PublicSpriteShapeCases.hpp` and the native notes
UI suite cover these flows. These are standalone UI changes, not new engine
export formats.

- `NoteExport`: `noteExportNamesCollide` shares case-insensitive export-name
  and Windows filename collision checks with creation and Save in mod.
- `NoteProject`: embedded block metadata optionally stores `noteType`; readers
  still accept older metadata and re-embedding retains a known owner.
- Host changes in `main.cpp`, `NewNote.hpp` and `ModSave.hpp` preserve queued
  session recovery, verify script ownership and compare complete visual
  definitions when deciding whether a note needs saving. These are separate
  from the reusable core helpers.
- `tests/SaveSafetyCases.hpp` adds native workflow regressions for all three
  engine formats and recovery; `CoreRegression.cpp` covers the shared helpers.

Original baseline hashes in `FML_SYNC_MAP.json` remain unchanged: they describe
the original FML snapshot, not the current release inventory.

## 1.0.4

Shared-core additions to review for FML:

- `NoteProject`: `embedProgramInScript` / `readEmbeddedProgram` keep a note's
  blocks inside its own script, in a comment the engine ignores (`--[[ ... ]]`
  in Lua, `/* ... */` in HScript): a `notelab-blocks 1` marker, the SHA-256 of
  the code outside the comment (CRLF and trailing blank lines do not count) and
  the program plus the written files as base64 JSON. Code added before *or
  after* the comment marks the script as edited by hand. Re-embedding replaces
  the old comment instead of stacking one more.
- `NotePreview`: `demoPatternOf` (seven built-in demo patterns),
  `CustomPattern` on a sixteenth-note grid, `customPatternNotes` and
  `patternFromNotes`.
- Core regressions: «Bloques dentro del script» and «Patrones de prueba» in
  `tests/CoreRegression.cpp` (FML: `fml_notelabcheck.cpp`).

`src/ModSave.hpp` is a new interface file (saving a note into the open mod
folder, the overview of one's own notes); it is mapped with a null baseline.

## 1.0.3

No shared-core change: `core/` and `support/` are identical to 1.0.2. The core
test's fixture writer now builds UTF-8 paths with `fs::u8path` (it failed in an
accented folder); check the same helper in FML's `fml_notelabcheck.cpp`. The
release changes the standalone UI only (`main.cpp`, `NewNote.hpp`,
`SpriteEditor.hpp`, `SpriteShapes.hpp`, `Tutorial.hpp`, `BlockCanvas`,
`BuildPolicy.hpp`). One host detail is worth carrying to FML: the bundled
ImGui OpenGL3 backend binds its own linear sampler, so a nearest-neighbour
`glTexParameter` on an ImGui image is ignored; request
`ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest` around the draw.

## 1.0.2 additions to review for FML

- `NoteSongs`: Psych 1.0 absolute-lane chart reading and matching when saving
  distributions. Keep the classic-lane export convention explicit.
- `NoteCreate`: drawn-piece models, independent receptor states, style frame
  extraction, whole-sheet Sparrow assembly and palette-specific copies of
  shared Sparrow regions. Grids/strips retain their documented first-owner limit.
- `NoteProject`: drawn recipes and editable drawing references in projects.
- `NoteExport`: bounded `.nlsprite` layer/tab archives and two-frame adaptation
  of a one-frame V-Slice hit receptor.
- `NoteCode` / `NoteBlocks`: bounded native-script-to-block conversion and
  engine-specific `code.line` preservation. Unrecognized top-level code is
  reported, not converted into a false cross-engine equivalent.
- `NoteResources`: Effects / Songs / Music classification and song grouping.

`Tutorial.hpp`, `NewNote.hpp`, `SpriteEditor.hpp` and `SpriteShapes.hpp` are
mapped in `FML_SYNC_MAP.json` too. Their interface integration is separate from
the reusable core changes. Preserve the original baseline hashes; a null hash
means a new file with no snapshot baseline, not an automatic merge approval.

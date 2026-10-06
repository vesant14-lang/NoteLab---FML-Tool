# Keeping Note Lab and FML in sync

This is a standalone Note Lab repository, **not a copy of the FML suite or a
second engine implementation**. Behavior is preserved from the shared snapshot;
the folder layout and relative includes are adapted for this smaller repository.

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

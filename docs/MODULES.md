# Architecture and file map

## Shared logic: `src/fml_notelab`

| Module | Responsibility |
| --- | --- |
| `NoteStyle` | Engine-neutral style model, sheet identity, independent component composition. |
| `NoteStyleRead` | Native engine readers, scope/path resolution, statically readable skin registrations and multikey names. |
| `NoteStyleCheck` | Required resources, resolved frames, findings and supported-engine differences. |
| `NotePreview` | Pure render-list builder, notes, receptors, holds, splashes, judgement/combo preview and explicit receptor fallback. |
| `NoteSongs` | Song/chart/audio catalog, native chart parsing, type patching and protected chart saves/backups. |
| `NoteTypes` | Custom-note catalog, deterministic distribution and preview bot rules. |
| `NoteProject` | Versioned `.fmlnote`, styles, recipes, programs, import references and bounded atomic project writes. |
| `NoteImage` | PNG decode/encode, crops, transforms, recoloring and packing. |
| `NoteExport` | Engine packages, coverage notes, structural re-reading and guarded folder/ZIP publication. |
| `NoteBlocks` | Block graph, definitions, validation, repair, presets and engine code generation. |
| `NoteCode` | Code synchronization, drafts/comments and syntax-color classification. |
| `NoteInstall` | Mod/base-game layouts, packs, addons and safe base discovery. |
| `NoteCreate` | Basic/advanced recipes, import inspection, manual crops/order and custom style construction. |

The core does not depend on an ImGui context or OpenGL. Its image/font helpers
use vendored decoding/rasterization libraries through explicit inputs.

## Independent UI: `NoteLab/src`

| File | Responsibility |
| --- | --- |
| `main.cpp` | Application state, sources, native dialogs, preview/audio, menus, editor, persistence orchestration and native CLI QA. |
| `Theme.hpp` | Shared palette, system fonts/icons and consistently styled controls. |
| `BlockCanvas.hpp/.cpp` | Block canvas interaction, layout, connections and visual editing. |
| `CodeEditor.hpp` | Native text editing with a syntax-colored overlay and line numbers. |
| `CustomCreator.hpp` | Advanced resource inspector, roles, grid/crops/order and creation UI. |
| `SongTools.hpp` | Distribution saving and preview-bot dialogs. |
| `StyleComposer.hpp` | Source/group picker, owned cross-source media mounting and composition UI. The composition rules themselves stay in `NoteStyle`. |
| `BuildPolicy.hpp` | Build version and hard-off playback policy. Preferences cannot enable audio in this build. |
| `Branding.hpp` | Load the original embedded PNG, set the SDL window icon and render the welcome logo. |
| `NoteLab/resources.rc` | Windows executable icon, embedded logo and version metadata. |

## Other shared dependencies

`fml_core`: models, diagnostics, hashes and UTF-8 helpers.
`fml_io/Vfs`: overlay folders/ZIPs and explicit imported files.
`fml_formats`: Sparrow/Animate atlases and native chart/song formats.
`fml_runtime/Scene`: engine-neutral atlas storage, frame geometry and draw lists.
`fml_render`: OpenGL rendering, text and effect support used by preview.
`fml_audio/AudioEngine`: independent song/inspection playback.
`third_party`: vendored dependencies with their original licenses.

## Scripts and tests

| File | Purpose |
| --- | --- |
| `setup_msvc.bat` | Locate/configure an installed MSVC x64 toolchain. |
| `build.bat` | Build the independent application into `build/app`. |
| `test.bat` | Rebuild and run the shared synthetic regression suite into `build/core`. |
| `test-workflow.bat` | Compile the native workflow harness against the application objects; generate owned fixtures under `.qa/` and run it. Run `build.bat` first after source changes. |
| `test-ui.ps1` | Run code, block and score interactions on generated fixtures with a bounded child-process timeout and no private mods. |
| `NoteLab/build.bat` | Build all units explicitly for `app` or `check`, with an optional output directory. It works both inside FML and in this kit. |
| `package-release.ps1` | Make clean public/developer folders and ZIPs from an explicit source dependency closure; never delete an existing release. |
| `src/tools/fml_notelabcheck.cpp` | Engine-neutral regression executable with optional read-only mod scans. |
| `tests/PublicCoreCases.hpp` | Composition, receptor fallback/multikey and project safety regressions. |
| `tests/PublicWorkflow.cpp` | Actual app-level create/compose/undo/reopen/export/protection flow using generated images and a hidden native render context. |
| `tests/PublicMatrix.hpp` | Image-input modes, all component roles, native-target package matrix, block/code roundtrips and optional real-installation display checks. |
| `test-engines.ps1` | Copy explicitly selected native engines into private `.qa/`, run workflow tests and prepare their QA mod. Originals remain unchanged. |
| `prepare-game-mods.ps1` | Install generated skin/type exports and a base-song QA chart into those isolated copies only. Game media never enters releases. |
| `capture-demo.ps1` | Capture the native UI with an explicitly selected base-game installation and isolated preferences for README images. |
| `prepare-assets.ps1` | Convert the supplied logo to a multi-resolution Windows icon, retaining the original PNG. |

The developer ZIP has source, not your preferences, local mods, private QA
paths, old executables or compiler objects. Dependency DLL/LIB files remain
because they are part of the vendored SDL build dependency.

Optional creation samples use `FML_NOTE_SAMPLE_CODENAME`, `FML_NOTE_SAMPLE_PSYCH`
and `FML_NOTE_SAMPLE_VSLICE` for engine installations. They are unset by default;
the source kit has no developer's personal mod paths.

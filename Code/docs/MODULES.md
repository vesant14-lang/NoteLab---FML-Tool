# Architecture and file map

## Shared logic: `core`

| Module | Responsibility |
| --- | --- |
| `NoteStyle` | Engine-neutral style model, sheet identity, independent component composition. |
| `NoteStyleRead` | Native engine readers, scope/path resolution, statically readable skin registrations and multikey names. |
| `NoteStyleCheck` | Required resources, resolved frames, findings and supported-engine differences. |
| `NotePreview` | Pure render-list builder, notes, receptors, holds, splashes, judgement/combo preview and explicit receptor fallback. |
| `NoteSongs` | Song/chart/audio catalog, classic and absolute-lane Psych parsing, native type patching and protected chart saves/backups. |
| `NoteTypes` | Custom-note catalog, deterministic distribution and preview bot rules. |
| `NoteProject` | Versioned `.fmlnote`, styles, drawn-piece recipes and drawing-file references, programs, imports and bounded atomic writes. |
| `NoteImage` | PNG decode/encode, crops, transforms, recoloring and packing. |
| `NoteExport` | Engine packages, coverage notes, structural re-reading, guarded folder/ZIP publication and bounded layered `.nlsprite` archives. One-frame V-Slice hit-receptor adaptation belongs here. |
| `NoteBlocks` | Block graph, definitions, validation, repair, presets and engine code generation. |
| `NoteCode` | Code synchronization, drafts/comments, syntax-color classification and bounded native note-script import. Recognized event statements become blocks; engine-specific lines and unsupported functions are explicit. |
| `NoteInstall` | Mod/base-game layouts, packs, addons and safe base discovery. |
| `NoteCreate` | Basic/advanced recipes, import inspection, manual crops/order, drawn-piece construction, style-to-sheet packing and palette-specific Sparrow region remapping. |
| `NoteResources` | Categorized mod media catalog, safe references, connected-block usage and bounded, non-overwriting media imports. |
| `NoteMediaEmit.hpp` | Shared image, sound-screamer and conditional video code emitters with engine keys and bounded overlay cleanup. |

The core does not depend on an ImGui context or OpenGL. Its image/font helpers
use vendored decoding/rasterization libraries through explicit inputs.

## Independent UI: `src`

| File | Responsibility |
| --- | --- |
| `main.cpp` | Application state, sources, native dialogs, preview/audio, menus, editor, persistence orchestration and native CLI QA. |
| `Theme.hpp` | Shared palette, system fonts/icons and consistently styled controls. |
| `BlockCanvas.hpp/.cpp` | Block canvas interaction, layout, connections and visual editing. |
| `CodeEditor.hpp` | Native text editing with a syntax-colored overlay and line numbers. |
| `CustomCreator.hpp` | Advanced resource inspector, roles, grid/crops/order and creation UI. |
| `NewNote.hpp` | Single-note assistant (cards, one «Create note» and the «Ready» next-steps page), behavior/look/sound/bot setup and engine-specific code-only templates. |
| `SpriteEditor.hpp` | Layered drawing UI, «How do you want to start?» screen, free canvases sent to the sheet, tools (mirror, pixel art, shortcuts), timeline, selection/clipboard, tabs, final PNG and drawing-file orchestration. |
| `SpriteShapes.hpp` | Drawing geometry, sheet/cell and free-canvas layouts, distortion-free fitting into a cell, pixel brush, layer compositing, shapes and code-rendered presets used by the sprite editor. |
| `Tutorial.hpp` | Seven in-workspace guides, task detection, focus hints, block demonstration and tutorial cues (silenced in 1.0.3 by `BuildPolicy`). |
| `ModResources.hpp` | General library and docked Library/In use sidebar, categorized cards/status, contextual actions, explicit media preview, import/destination dialog and typed-slot assignment. Shared resource/import audio controls reuse AudioEngine for full-length audition, opt-in Loop, speed, stereo balance and A–B ranges. |
| `SongTools.hpp` | Distribution saving and preview-bot dialogs. |
| `StyleComposer.hpp` | Source/group picker, owned cross-source media mounting and composition UI. The composition rules themselves stay in `NoteStyle`. |
| `BuildPolicy.hpp` | Build version and independent audio policies: general playback on, block-editor cues off, tutorial cues off in 1.0.3 (their switch is hidden). |
| `Branding.hpp` | Load the original embedded PNG, set the SDL window icon and render the welcome logo. |
| `resources.rc` | Windows executable icon, embedded logo and version metadata. |

## Necessary support only: `support/`

`support/core`: models, diagnostics, hashes and UTF-8 helpers.
`support/io/Vfs`: overlay folders/ZIPs and explicit imported files.
`support/formats`: Sparrow/Animate atlases and native chart/song formats.
`support/runtime/Scene`: engine-neutral atlas storage, frame geometry and draw lists.
`support/render`: OpenGL rendering, text and effect support used by preview.
`support/audio/AudioEngine`: song playback, decoding, volume/seeking and imported-file audition.
`third_party`: vendored dependencies with their original licenses.

There is no FML application, script host, runtime session, character/stage
viewer, importer, Atlas or other lab in this repository. These source helpers
are the direct/transitive dependencies of Note Lab. Their original C++ names
stay compatible with the shared implementation.

## Scripts and tests

| File | Purpose |
| --- | --- |
| `setup_msvc.bat` | Locate/configure an installed MSVC x64 toolchain. |
| `build.bat` | Build the independent application into `build/app`. |
| `test.bat` | Rebuild and run the shared synthetic regression suite into `build/core`. |
| `test-workflow.bat` | Compile the native workflow harness against the application objects; generate owned fixtures under `.qa/` and run it. Run `build.bat` first after source changes. |
| `test-ui.ps1` | Run code, blocks, resource/audio controls, score, tutorial and new-note/sprite flows on generated fixtures, with a bounded child timeout and isolated unsaved preferences. |
| `build-project.bat` | Build all Note Lab units explicitly for `app` or `check`, with an optional output directory, from this repository alone. |
| `test-layout.ps1` | Verify the standalone source folders, required modules and quoted include paths; reject other FML tools. |
| `FML_SYNC_MAP.json` | Map standalone source paths to their original FML paths and baseline hashes for reviewed synchronization. |
| `package-release.ps1` | Make clean folders, ZIPs and fresh SHA256 manifests from the dependency closure. Require matching source/binary/release versions; never overwrite an existing release. `-DeveloperOnly` publishes only the source kit. |
| `tests/CoreRegression.cpp` | Engine-neutral regression executable with optional read-only mod scans. |
| `tests/PublicCoreCases.hpp` | Composition, receptor fallback/multikey and project safety regressions. |
| `tests/PublicWorkflow.cpp` | Actual app-level create/compose/undo/reopen/export/protection flow using generated images and a hidden native render context. |
| `tests/PublicMatrix.hpp` | Image-input modes, all component roles, native-target package matrix, block/code roundtrips and optional real-installation display checks. |
| `tests/PublicMediaCases.hpp` | Media catalog/provider/Unicode checks, import protection, typed assignment, usage persistence and packaged-resource preflight/ZIP regressions. A private 65-second WAV fixture exercises full-length audition, loop boundaries, A–B, speed/balance, asynchronous stop and song independence. |
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

The same media test file includes native song-repeat regressions: three laps
in both scroll directions, tap and sustain-head render commands, hit/miss
reset, end-of-audio pause/resume, manual results and explicit seeking.
`build/app/PublicWorkflow.exe --playback-only` reruns audition and repeat
checks without the broader export matrix, after compiling the workflow harness.
The core test's temporary directory is local to `.qa/core-temp`.

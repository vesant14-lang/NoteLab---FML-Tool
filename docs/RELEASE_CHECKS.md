# Note Lab 1.0.1 verification

Prepared on 2026-10-04. Tests use isolated copies of the installations selected
by the user. Original game/mod files and previous releases are not replaced.
Synthetic pixel fixtures remain private QA material, not content in either
delivery. README images are native captures with base-game art.

## Automated checks

| Suite | Passed | Coverage |
| --- | ---: | --- |
| Shared core | 684 | Styles, composition, receptors, types, blocks, code, projects and export regressions. |
| Native workflow/matrix | 2,143 | Creation/export/code matrix plus display/chart/project checks using the three installations. |
| Native UI | 37 | Syntax-colored editing, blocks/connections, controls and score/reset interactions. |

Total: **2,864 passing checks**, plus clean rejection of a UI test without its
required program. Zero reported failures in final runs. The separate 1,660-check
matrix is included in the 2,143 workflow run, not counted twice.

Creation covers six image-input modes, 16/32 grids, manual regions, XML/TXT,
sequences, every part/direction, HUD roles, frame order, FPS/loop and invalid
references. Exports cover source/target/role combinations, rereading, missing
resources and repeat ZIP writes under accented paths.

Code checks cover every preset and engine, editable comments, exact supported
code/block roundtrips, syntax-color spans and all 65 palette definitions in EN/ES.
Dormant values are not expected to emit statements by themselves. These checks
do not execute every block inside the engines.

Each real-installation style is drawn at three sizes, both scroll directions
and three side modes. Native charts load, their clock advances without an audio
device, and projects save/reopen under Unicode paths.

Silence checks include legacy sound-enabled preferences, imported audio,
song-clock progression and forced editor cue requests. The build policy stays
off, no SDL playback device starts, and there is no audio-enable UI.

The matrix found a real Windows ZIP defect: destination parents were not
created, and repeat publication could not atomically replace an existing ZIP.
Both cases were fixed in the shared exporter and the matrix was repeated.

## Actual engine execution — without Computer Use

Private packages contain a generated skin, `NoteLabQA` custom note and a logging
block, plus a Bopeebo test chart. Bootstrap/observer scripts are separate from
the exported block. Captures come from the engine framebuffer, not from Note Lab
pretending to be the game.

| Engine | Observed result |
| --- | --- |
| Codename 1.0.1 | Automatic Bopeebo playback to an 18-second checkpoint. 77 note instances processed, including 21 custom instances/sustain segments; zero misses at that checkpoint. The exported texture key and 36 atlas frames resolved. Logging block ran; PNG captured natively. |
| V-Slice 0.8.6 | Mod and NoteKind initialized. Native LoadingState started Bopeebo in bot/practice mode; 13 player tap hits, including 4 custom hits, by the 18-second checkpoint. Logging block ran; built-in screenshot plugin saved a PNG. |
| Psych | Packages reread and all loaded styles passed the Note Lab render/chart matrix. A game observer is prepared, but the song was not started in the supplied executable. **In-game playback remains unverified.** |

Codename first exposed observer mistakes: numeric ID versus string note type
and an unavailable unimported Sys exit. Only the observer was corrected; the
final capture/record has no such script error. V-Slice needed normal process
permissions for native save initialization and its native LoadingState entry;
direct state switching did not establish playback. The final run has a
screenshot-related conductor lag warning, not a fatal export error. The missing
optional V-Slice mod icon warning is not concealed.

This verifies these specific packages and a short song segment, not every mod,
engine version, HUD role, block or full-song gameplay. Native test processes are
bounded and only processes created by these tests are stopped.

## Delivery and images

The original logo PNG is unchanged and embedded in the app. The multi-size ICO
is a mechanical conversion. Welcome, preview, asset, composer, code and compact
EN/ES captures were inspected. Code syntax colors are visible.

Only the Note Lab dependency closure, documentation, logo and vendored libraries
are packaged: no engine copies, fixtures, preferences, compiler objects or private
logs. ZIP hashes and per-file manifests accompany the release. See
`RELEASE_VERIFICATION.json` beside the ZIPs for archive, extracted-app smoke and
relocated-source build results.

## Limits

Preview does not execute arbitrary mod scripts or every block; it reports its
simulation coverage. HUD layout offsets remain preview-only. Projects reference
source mods and their imported/generated-media cache; portable bundles and
assisted cache reconnection are future work.

Shared modules are reusable by FML, but this release does not silently merge or
rebuild the suite. Audio is disabled in this host, not removed from shared audio.
OGG import/export remains available.

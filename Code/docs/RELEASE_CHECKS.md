# Note Lab 1.0.4a release verification

Date: **2026-10-07**. This report distinguishes current automated checks from
gameplay recorded for earlier builds. Tests use private outputs, preferences
and fixtures; the three installed engines are read-only inputs. Previous
deliveries are preserved.

## Changes in this build

- Recovery copies are reserved separately from the active autosave. Opening
  another source or saving a project does not consume a pending recovery.
  Restoring a session preserves the displaced session and prompts for unsaved work.
- Note creation and in-mod saving reject names that produce colliding export
  filenames. Embedded block metadata identifies its owning note; older metadata
  remains readable, without silently granting ownership to an unrelated file.
- Saved-state comparison includes the complete look definition. Visual edits
  and imported looks are included when saving a custom note.
- Paint offers twelve distinct figures, a thumbnail picker, drag drawing and
  placement on an undoable new layer. Thickness/pixel settings are remembered
  per figure; the note arrow retains its standard outline.
- The advanced guide is available in English and Spanish, with 42 chapters.
  Instructions distinguish project saving, in-mod saving and exporting.

## Current automated results

| Suite | Passed | Main coverage |
| --- | ---: | --- |
| Shared core | 768 | Styles, charts, projects, creation, code/block conversion, embedded ownership, Unicode saves, patterns, media and export regressions. |
| Native workflow | 2,320 | Creation, rendering, composition, imports, distribution, saving/reopening, save protection, twelve figures, block presets, audio and export/reread checks. |
| Native UI | 187 | Actual controls for code, blocks, resources, audition, tutorials, note creation, painting, figure selection/settings/placement, in-mod saving and pattern editing. |

**3,275 checks passed with zero failures**, plus the separate missing-program
guard. The application was rebuilt after its last code change; the workflow
harness links the same application modules. The core suite builds separately.

The three-engine matrix read these installations:

| Engine | Discovered styles | Charts |
| --- | ---: | ---: |
| Codename | 4 | 67 |
| Psych | 11 | 78 |
| V-Slice | 2 | 173 |

The 59 save-safety checks cover name aliases, legacy ownership, conflicting
owners, complete look changes, imported looks, several pending recoveries and
preservation of the active session. The 113 figure checks cover all twelve
distinct raster outputs, geometry, EN/ES labels, clipping, crisp pixel edges,
thickness, layers, settings and undo/redo.

UI cases include standard 1480×900 and compact 1024×768 layouts. The new
selector is also captured in both sizes. Input is driven inside the native
application; these checks do not automate the desktop.

Logs are retained privately as `release-1.0.4a-core.log`,
`release-1.0.4a-workflow.log` and `release-1.0.4a-ui.log`.

## Coverage boundaries

Creation covers images, sequences, sheets, XML/TXT, grids, regions, frame order,
FPS/loop, note/HUD roles, drawn pieces, final sheets and editable drawings.
Every discovered style is rendered at three sizes, both scroll directions
and each side mode. Project/chart tests include Unicode paths.

Exports cover target/role combinations, resource preflight, rereading, missing
files, destination creation, repeat publication and accented paths. Save in mod
is tested on an owned fixture, never a user's mod or base-game installation.

Code tests cover presets, comments, syntax spans, supported roundtrips and
script-to-block conversion. They do not execute every block in every game.
Real song audio is decoded and checked for non-silent samples, clock
progression, pause and seeking. Separate audition/repeat regressions exercise
full-length files, looping, A–B, speed/balance and returning tap/hold heads.
Synthetic media-header checks verify validation, not native video decoding.

General song playback and resource audition remain enabled. Block-editor and
tutorial interaction cues stay muted without an enable control in this build.

## Historical native gameplay — 2026-10-05

These are recorded observations from earlier development, **not new gameplay
runs for 1.0.4a**. The screenshots below are game captures, not simulated Preview.
The packages and song segments were specific examples, not a universal test.

| Engine/version | Recorded result |
| --- | --- |
| Psych 1.0.4 | Bopeebo Hard with a custom-note distribution, RGB-based HUD, colored holds, splashes and note actions. Song-skin settings included `arrowSkin` and `disableNoteRGB`. |
| Codename 1.0.1 | Drawn HUD, holds, star splash, custom notes and flash. A private observer required a callback adjustment; it is not shipped. |
| V-Slice 0.8.6 | Drawn HUD, custom notes and blocks; the corrected one-frame confirm receptor was replayed with two references to its region. |

![Psych native gameplay](images/game-psych.jpg)

![Codename native gameplay](images/game-codename.jpg)

![V-Slice native gameplay](images/game-vslice.jpg)

In-mod ownership metadata and recovery were not gameplay features in those
runs. Their protection is checked by the current automated suites.

## Source closure, guides and screenshots

The kit contains only Note Lab: **14 core modules and 90 source/header files**,
plus required format, file, render, audio and vendored dependencies. Quoted
includes are self-contained. The source manifest is regenerated during packaging.

Build policy, executable resources, package version, sync map, README and
current guides use **1.0.4a**. Packaging rejects a source/binary version mismatch.

Both deliveries contain the advanced guides in EN and ES and their offline
images. The new figure screenshot was captured from the final executable with
the V-Slice base mounted read-only. Older screenshots remain where the
corresponding screen is unchanged. Maintainer logs, private fixtures, gameplay
assets and test audio/video are excluded.

## Delivery checks

Archive verification is repeated after final packaging. Its exact results, final
archive hashes, extracted-public smoke tests and relocated Developer build
are recorded in `RELEASE_VERIFICATION.json` beside the two release ZIPs.
`SHA256SUMS.txt` identifies those archives; each also contains its file manifest.

The release checks require both guides and the new figure/save-safety sources,
validate every listed file/hash and local document link, reject private outputs,
and compare the public executable with the tested build.

| Check | Closing result |
| --- | --- |
| Developer dependency closure | Clean application build and 768 core checks passed from the extracted candidate in an accented folder, without the FML checkout. The 247 non-document files match the final source tree. |
| Relocated application | Version 1.0.4a; startup and native screenshot completed successfully. |
| Extracted Public | Version 1.0.4a; startup, V-Slice base assets and 35 note-creation/painting UI steps passed. Final archive verification repeats these checks. |
| Guides | EN and ES each have 42 chapters; all table-of-contents anchors resolve. Local links and image paths are checked in both extracted packages. |
| Integrity | Archive checksums and per-file manifests are validated; private fixtures, logs, test media and other FML applications are rejected. |

## Known limits

Preview simulates supported properties, not arbitrary mod scripts. HUD layout
offsets are preview-only. Projects still reference source mods and the
imported/generated-media cache; portable bundles and complete reconnection
remain future work.

Multimedia scripts depend on the selected engine/build and codec support.
Video inspection uses the external player. Not every multimedia block has
been exercised natively in all three games. Structural validation is not
certification of every engine version or third-party script.

Earlier export failures from an accented build directory did not reproduce
in the 1.0.4 closing checks. The cause was not identified; this remains a
non-reproduced report, not a confirmed fix.

# Keeping Note Lab and FML in sync

This kit is a release snapshot of the shared FML modules, **not a second engine
implementation**. Inside the suite, keep the same `src/fml_notelab` and shared
dependency paths. The Note Lab UI remains under `NoteLab/src`.

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

Host-level changes live only in `NoteLab/src`: welcome/compact layout,
composition mounting, safer source closing/reloading and transactional project
source preparation. FML can reuse these core changes without importing
the independent window implementation. The 1.0.1 silence policy is host-only:
`NoteLab/src/BuildPolicy.hpp` disables playback in Note Lab without changing
the shared `fml_audio` implementation or muting the suite.

`SOURCE_MANIFEST.json` in the developer delivery records the exact shipped
files and SHA-256 values. For moving improvements back to a different FML
checkout, compare its baseline first, merge only related changes and rebuild
the actual consumers. A packaged snapshot is not an automatic suite release.

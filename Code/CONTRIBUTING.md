# Contributing

Use the existing dark/violet UI and English/Spanish labels. Keep core rules
out of UI headers. Prefer scoped improvements and reproducible synthetic
fixtures over mod-name exceptions. Document module contracts and known
limitations, and add regressions for changes to saving or export.

Build with `build.bat`, then run `test.bat`, `test-workflow.bat` and
`powershell -File test-ui.ps1`. Native
workflow/UI checks need an OpenGL 3.3 context. Core checks do not need game mods.
Test a changed exporter by re-reading its package, then test it in a separate
destination-engine mod before claiming in-game support.

Never commit mods, game assets, fonts, user preferences, cache, local paths,
compiler objects, generated private projects or QA fixture directories.
Preserve dependency notices. Update the changelog for user-visible changes;
private development logs and fixtures do not belong in release packages.

Report bugs with app version, engine/build, file format, steps and the relevant
warning. Attach only assets you have permission to share. Do not send a whole
commercial/private mod or personal project if a small fixture reproduces it.

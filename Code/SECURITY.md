# Security and data safety

Treat mods, images, archives, metadata and projects as untrusted input.
The preview does not run arbitrary mod code. Exporting generated/custom
scripts is not equivalent to validating every possible behavior in the game.

Limits apply to file scanning, project structure, custom images/frames and
exports. Save operations must preserve the last good project on failure.
Native chart replacement requires explicit approval, a backup and a check
that the loaded original has not changed. Base-game/ZIP inputs are protected.

Do not publish vulnerability-triggering projects in public comments. Contact
the maintainer privately using the distribution page's available contact
channel, with version, steps and a minimal reproducer. No private address or
response-time guarantee is supplied by this kit.

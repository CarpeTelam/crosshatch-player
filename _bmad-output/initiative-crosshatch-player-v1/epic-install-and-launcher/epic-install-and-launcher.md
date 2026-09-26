---
type: epic
title: "Players install a game by dropping one file and start it in three taps"
parent: initiative-crosshatch-player-v1
covers: [CAP-3, CAP-4, CAP-7, CAP-8]
after: []
assignee: ""
risk: high
---

# Players install a game by dropping one file and start it in three taps

## Description

Adds the one installer and registry, the `.cpgame` packer, and the paged games launcher with Continue, remove, and a mode picker. Solo matches save and resume. The launcher replaces the minimal Games list from epic-script-runtime.

## Outcome

A player installs a game by putting one file on the SD card and starts it without help; the CAP-3 and CAP-4 success checks are the signal.

## Requirements

Completed at inception. This epic owns CAP-3, CAP-4, solo resume through Continue (CAP-7), and the launcher part of CAP-8.

## Done when

1. A valid `.cpgame` in `/games/` is installed and listed when the launcher opens, with no reboot; an invalid one is renamed `.bad` and its reason shown once; zip bombs, path traversal, binary chunks, over-limit members, and an EOCD count mismatch are rejected.
2. Reinstalling a game, and removing it from the launcher, both keep `/.games-data/<id>/`; `scripts/pack_game.py` and the device compute the same package hash on a shared vector.
3. From Home a solo game starts in at most 3 taps; the list pages past one screen; each row shows the package `icon.bmp` or its manifest library icon; a package whose `api` exceeds the host's shows as unavailable; no step needs text entry.
4. "Continue" is listed first when a save exists; sleeping during a solo match and choosing Continue restores it; a save from a changed package is discarded.
5. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

`GamePackageInstaller`, `GameRegistry`, `.pkg`, `Manifest::check`, `GamesLauncherActivity`, `GameModeActivity`, `resume.bin` in GameSaveStore, resume in the match, `scripts/pack_game.py`, and the SHA-256 helper. Consumes `lib/ZipFile`, `lib/miniz`, `lib/PngToBmpConverter`, `Storage`, and the web file manager unchanged. Not a web Games page (deferred).

Handoffs: epic-pass-and-play extends the mode picker with the seat choice and resume with the hand-off.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-15, AD-16, AD-17, AD-19, AD-21, AD-22
- constraint — docs/contributing/touch-and-ui.md

## Notes

- Risk high: the installer deletes and renames files on the SD card; a person confirms on a device that saves survive reinstall and remove.
- Waits on epic-script-runtime because: `Manifest::parse`, GameSaveStore, `GameMatchActivity`, and the minimal Games list this replaces.
- Waits on epic-icon-library because: the icon library for launcher rows and the `.bmp` layout images are read from.

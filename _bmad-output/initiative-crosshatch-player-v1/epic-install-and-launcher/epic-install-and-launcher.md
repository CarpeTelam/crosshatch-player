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

`GamePackageInstaller`, `GameRegistry`, `.pkg`, `Manifest::check`, `GamesLauncherActivity`, `GameModeActivity`, `resume.bin` in GameSaveStore, resume in the match, `scripts/pack_game.py`, and the SHA-256 helper. Consumes `lib/ZipFile`, `lib/miniz`, `lib/PngToBmpConverter`, `Storage`, and the web file manager unchanged. `scripts/pack_game.py` keeps the calling contract the fork release workflow (epic-platform-baseline entry 7) relies on: a `games/<id>/` directory in, `<id>.cpgame` out, the package hash printed, and a nonzero exit on an invalid package. Not a web Games page (deferred).

Handoffs: epic-pass-and-play extends the mode picker with the seat choice and resume with the hand-off.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-15, AD-16, AD-17, AD-19, AD-21, AD-22
- constraint — docs/contributing/touch-and-ui.md

## Notes

- Risk high: the installer deletes and renames files on the SD card; a person confirms on a device that saves survive reinstall and remove.
- Waits on epic-script-runtime because: `Manifest::parse`, GameSaveStore, `GameMatchActivity`, and the minimal Games list this replaces.
- Waits on epic-icon-library because: the icon library for launcher rows and the `.bmp` layout images are read from.
- Handoff from epic-icon-library (entries 1 and 2, 2026-09-28): launcher rows draw library icons through the `src/games` `drawGameIcon` helper (Screens have no `lib/GameIcons` edge), read `icon.bmp` through its `.bmp` reader, and reuse its 128 KB converted-image constant in the installer and `pack_game.py`; `GameCore` cannot include `lib/GameIcons`, so checking a manifest `icon` name against the library belongs outside `GameCore`.
- Handoffs from epic-icon-library (owner's entry-8 review, 2026-09-28; details in `_bmad-output/implementation-artifacts/deferred-work.md` under `## 3.1`, `## 3.2`, `## 3.6`, `## 3.7`):
  - Retro AI-2, the `src/games` screen harness (a Storage stub and a renderer double, or a scripted simulator screenshot diff), closes the device-side gaps it lists: `FrameReplay`'s `Op::Icon` and `Op::Image` wiring, `GameAssets::load`'s image spans, the `BadImage` → `STR_GAMES_BAD_IMAGE` mapping, the cover-grid tab order against `HomeActivity`'s index mapping, and entry 7's R3 render gate and R3 residual (Pause then Resume inside the Play-again gap redraws the old frame); also the `vmFailure` call sites in `GameVM` and the match (`## 3.10`), `GameAssets`'s image-header read-error path (the cross-story fix f27dcefd), and `FrameReplay`'s fill-weight replay (`## 3.9`) (added by the epic-icon-library retrospective, AI-12).
  - The installer and `pack_game.py` count the converted-image budget as `GameCore::IMAGES_BYTES` does: whole `.bmp` bytes, 62 + ceil(w / 32) * 4 * h each, headers included and `icon.bmp` excluded, at most `MAX_IMAGES` (32) images; otherwise a package installs and then fails to start with `BadImage`. They also pass *every* package image through `PngToBmpConverter`, a supplied `.bmp` included, because the loader accepts only that layout (top-down rows, two palette entries, `imageSize` filled in; `GameImages.cpp` `checkImageHeader`) (retrospective R7).
  - Retro A3: split the Lua-literal vector notation out of `scripts/game_codec.py` when `pack_game.py` first imports the codec.
  - Retro R10: while an overlay such as the light panel is open over a match, its watchdog, timer poll and store flush pause; the fix needs a ledger row for `FrontlightPanelActivity.cpp` or more of `ActivityManager.cpp` (an owner decision), documented in `docs/crosshatch/game-canvas.md` "Overlays".
  - Launcher rows draw library icons through `drawGameIcon` by their Phosphor names and weights (entry 9), and a manifest `icon` name check against the library stays outside `GameCore`.
  - Device run: time a worst-case dithered 480x800 `ch.gfx.image` on the render task on an X4 Pro (deferred from entry 2; no device run in epic-icon-library), and a worst-case frame after the per-frame drawing bound the epic-icon-library retrospective's AI-10 adds (a frame could hold 2,048 image or large-icon commands; retrospective R1).
- Flash and static-RAM budget (epic-icon-library retrospective AI-9, 2026-09-28): 28,288 B of the 250 KiB flash gate and 248 B of the 1,024 B static-RAM gate remain, measured at `f27dcefd` with `check_flash_budget.py` (games on minus off, IRAM counted), for this epic, epic-pass-and-play, and epic-play-nearby. This epic's inception splits that headroom between them; its tracer measures its own delta per `docs/crosshatch/orchestrated-epics.md` (base measured before the first story), and no size is quoted before it. Icon compression is the owner's reserved, API-neutral lever: on the 220 committed bitmaps (70,400 B of data) PackBits measures 51,776 B and zlib 21,315 B (data only; the firmware saving and whether an inflater is already linked are unmeasured). The inception names the headroom below which the compression story runs.
- Carried from the epic-script-runtime retrospective (AI-4, via the epic-icon-library retrospective's AI-13): bound sleep's time under `RenderLock` (`GameMatchActivity::onExit`'s join and abandon waits count real elapsed time); this epic's Done when 4 (sleep during a solo match, then Continue) changes the same exit path. AI-3 (the device record of the `loop` bands and the abandon path) stays the owner's check.

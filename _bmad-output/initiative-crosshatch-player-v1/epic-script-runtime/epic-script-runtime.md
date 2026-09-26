---
type: epic
title: "A sandboxed Lua game runs solo on the device"
parent: initiative-crosshatch-player-v1
covers: [CAP-1, CAP-2, CAP-7]
after: []
assignee: ""
risk: high
---

# A sandboxed Lua game runs solo on the device

## Description

Builds the script runtime: the sandboxed Lua VM on its own task, the one codec and its limits, the `ch.*` bindings except icons and images, the display list and refresh policy, per-game storage, and the match activity for solo play with its pause, error, and game-over views. A minimal Games list on Home reaches games placed on the SD card by hand.

## Outcome

A developer runs a solo script game on an X4 Pro and no script can crash or hang the device; the CAP-1 and CAP-2 success checks are the signal.

## Requirements

Completed at inception. This epic owns CAP-1 (the host API), CAP-2 (fault isolation), and the solo part of CAP-7 (pause, leave, play again). CAP-1's "no game-specific firmware code" check is closed by epic-first-party-games.

## Done when

1. A solo fixture game, placed by hand as `/.games/<id>/` with no `.pkg`, is reached from Home → Games in the list theme and played to game over on an X4 Pro, using drawing, text, tap, long press, swipe, refresh hints, and `ch.timer`, with `ch.store` surviving a restart.
2. A Lua error, an infinite loop, heap exhaustion of the 256 KB arena, an oversized state (over 1,400 B), move (over 256 B), or `ch.store` (over 4 KB), a binary chunk at load, frame-buffer overflow, and an invalid `status` each end the session in the error view with Back, in host tests and on the device, and the device stays responsive.
3. The C codec and `scripts/game_codec.py` pass the same golden vectors, and the byte formats are recorded in `docs/crosshatch/formats.md`.
4. Back and Home open the pause menu, Leave returns to Games, Play again restarts a solo round, and sleep takes the forced exit without deadlock; the canvas exception to `touch-and-ui.md` is recorded in `docs/crosshatch/`.
5. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

`lib/GameScript`, the `GameCore` ports, `Session` for one seat, `Manifest::parse`, and in `src/games` the GameVM task, FrameReplay, GameViewport, the arena backend, the GameAssets loader, and GameSaveStore for `store.bin`. `GameMatchActivity` with the Playing, Paused, Over, Error, and Leaving states. Owns ledger rows 4 to 7 (the Home menu item and `goToGames()`) and the first `STR_GAMES_*` keys (row 2). Not the installer, `.pkg`, paging, Continue, or remove (epic-install-and-launcher); not `ch.gfx.icon` or `ch.gfx.image` (epic-icon-library); not pass or nearby.

Handoffs: the minimal Games list is replaced by the full launcher in epic-install-and-launcher; `Manifest::parse` is reused by the installer and the Nearby lobby; `Session` is extended to N seats in epic-pass-and-play; GameSaveStore gains `resume.bin` in epic-install-and-launcher; the `ch.gfx` binding registry and runtime views are extended by epic-icon-library.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-1, AD-4 to AD-10, AD-14, AD-17, AD-19 to AD-21, AD-23
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md
- constraint — docs/contributing/touch-and-ui.md
- constraint — docs/activity-manager.md

## Notes

- Decision: E2 reaches games through a minimal Home → Games list of installed directories; epic-install-and-launcher replaces it (user's decision, 2026-09-26).
- Waits on epic-platform-baseline because: build guard, vendored Lua, test subdirectories, and the ledger.

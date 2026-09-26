---
id: SPEC-crosshatch-player
companions:
  - glossary.md
  - first-party-games.md
  - ../../planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md
  - ../../planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md
  - ../../../AGENTS.md
sources:
  - ../../planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/brief.md
  - ../../planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/addendum.md
---

> **Canonical contract.** This SPEC and the files in `companions:` are the complete, preservation-validated contract for what to build, test, and validate. Source documents listed in frontmatter are for traceability — consult them only if you need narrative rationale or prose color this contract intentionally omits.

# crosshatch-player v1 game platform

## Why

A vision and a pain. The author, a developer who writes games with an AI assistant, wants an X4 Pro or reTerminal Sticky to work as a pocket game console that friends and family can use, above all kids handed a device at a restaurant instead of a phone. On these boards today, adding a game means changing the firmware. CrossPlay's games are each more than 1,200 lines of C++ across 169 changed upstream files, and the author's fork of it became a merge nightmare in which automated merges silently deleted fork code. SUMI proved sandboxed Lua works on this hardware class but has no touch, no multiplayer, and no scripted games. CrossMux compiles its games in. Upstream CrossPoint closed the games request and has no plugin system. v1 makes games content rather than firmware: script packages on the SD card, run by an additive fork runtime that gives every game multiplayer for free. It is a passion project; demand beyond the author is unknown, and that is accepted.

## Capabilities

- **CAP-1**
  - **intent:** A developer can write a turn-based game as a script against a documented, versioned host API that covers touch input, drawing, refresh hints, a timer, and per-game storage.
  - **success:** Each first-party game (CAP-10) is only a script package; no game-specific firmware code exists.
- **CAP-2**
  - **intent:** A faulty or runaway script cannot crash, hang, or corrupt the device, and its failure ends the session in an error view the player can back out of.
  - **success:** A Lua error, an infinite loop, heap exhaustion, and an oversized state or move each end the session in the error view with Back, both in host tests and on a device, and the device stays responsive.
- **CAP-3**
  - **intent:** A player or author installs a game by putting one `.cpgame` file on the SD card with the existing web file manager or USB, without a reboot or a firmware build.
  - **success:** When the launcher opens, a valid package in `/games/` is listed. An invalid one is renamed `.bad` and its reason is shown once. Reinstalling a game keeps its saved data.
- **CAP-4**
  - **intent:** A player reaches a paged games launcher from Home, starts a game, continues a saved one, or removes one, with no text entry.
  - **success:** From Home, a solo or pass-and-play game starts in at most 3 taps. "Continue" is listed first when a save exists. The list pages past one screen.
- **CAP-5**
  - **intent:** One game script plays solo, pass-and-play, and Play Nearby (one player per device, two devices) with no radio code in the script.
  - **success:** A two-player game finishes a round in both pass-and-play and Play Nearby from the same unmodified package.
- **CAP-6**
  - **intent:** In pass-and-play games marked hidden, one player never sees another player's private view, including through e-ink ghosting, sleep, or resume.
  - **success:** Every change of turn seat, every new or resumed round, and every sleep shows the blank hand-off screen with a full or half refresh before the next seat is drawn, and a script cannot suppress it.
- **CAP-7**
  - **intent:** Players can pause, leave, play again, and resume an interrupted solo or pass-and-play match. A Play Nearby match ends cleanly when the other player leaves or goes silent.
  - **success:** Putting the device to sleep during a solo or pass match and then choosing "Continue" restores the match. In Play Nearby, a peer that leaves or is silent for 10 s brings up the "player left" view on the other device.
- **CAP-8**
  - **intent:** Games and runtime screens share a curated icon library as a common design language, and a game can also ship its own images.
  - **success:** The launcher, the runtime views, the Home Games tile, and the first-party games draw from the library. A package image is shown at its native size in 1-bit.
- **CAP-9**
  - **intent:** An AI assistant can write a working game from the API docs alone, and the docs can move to a future starter repo unchanged.
  - **success:** Given only `docs/crosshatch/game-api.md`, its LuaLS stub, and the icon catalog, an AI writes a game that installs and plays a round on a device.
- **CAP-10**
  - **intent:** At least three first-party games ship with each fork release, covering a solo puzzle, an open-information two-player game, and a hidden-information two-player game. Together they exercise every mode.
  - **success:** Sudoku, Ultimate tic-tac-toe, and Battleship ship as `.cpgame` release assets, install through the inbox, and each plays a round in every mode it declares (`first-party-games.md`).
- **CAP-11**
  - **intent:** Upstream `develop` merges stay clean. Game code sits outside upstream files, and changes to upstream files are capped by a ledger and enforced in CI.
  - **success:** A fork-only CI job fails any PR that changes an upstream path missing from the ledger or the baseline allowlist, and merging upstream `develop` touches no game code.

## Constraints

- Game code is active only in the x4pro and sticky envs, their release variants, and the simulator envs, under `FREEINK_CAP_GAMES`. The game libraries still compile, unreferenced, for `default`, `x4c`, and `papermono`, at no cost to the C3.
- Upstream changes are limited to the 9-file ledger in AD-3 plus 1 reserve file. Going past that requires an architecture update first. The `freeink-sdk` pointer never moves.
- Game rules live only in scripts. Roster, turn, sync, and protocol logic lives only in the host-testable `GameCore`. There is no game-specific C++.
- Scripts reach the host only through `ch.*`: no file, radio, or framebuffer access, and text chunks only. Each callback has a budget of 2 M instructions, which answers a move in about 1 s, and each VM has a 256 KB PSRAM heap.
- Every mode enforces the same codec limits: a snapshot of at most 1,400 B (one ESP-NOW v2 payload), a move of at most 256 B, and a `ch.store` of at most 4 KB.
- Only simple turn-structured games are in scope, and every API addition must serve one. The API has no frame loop, drag input, sprites, sound, or timers under 1 s.
- The API is versioned by an integer level and is additive only within a level. Icon names are part of the level.
- Play Nearby never runs while the web server or any other Wi-Fi activity is up. The radio is unencrypted, so the design assumes everyone in the room cooperates.
- The runtime adds at most 250 KB of flash to the x4pro image. Its internal-RAM costs are the 16 KB VM stack, the 4 KB link stack, and the Wi-Fi driver; everything else lives in PSRAM. The Nearby lobby refuses to open below 100 KB of free internal heap.
- A child can finish every v1 flow unaided: no text entry, large touch targets, and short `tr(STR_GAMES_*)` text.

## Non-goals

- More than two players. The seat model allows N; the lobby and UI ship with 2.
- Simultaneous turns, reconnecting or rejoining after a drop, and saving Nearby matches.
- A web Games page for upload and delete; `/api/games*` is reserved for it.
- Dirty-region refresh, true grayscale, and image scaling.
- A computer opponent provided by the runtime. A script may implement its own inside `apply`.
- Real-time, action, physics, or drag-heavy games.
- Playing against CrossPlay devices, and porting CrossPlay's C++ games.
- A game starter repo, a gallery, sharing between players, a browser playground, and a simulated radio link for Play Nearby.
- Migrating saves across package versions; a changed package hash discards the save.
- Aligning with upstream "web plugins" before upstream has code for them.
- Encryption, anti-cheat, and telemetry.

## Success signal

- The author writes a new game with an AI in an evening, drops the `.cpgame` on the SD card, and a child picks up an X4 Pro, launches the game, and finishes a round alone, passing the device, or against a second device, without adult help and with no firmware build, as observed on a real device. The next upstream `develop` merge touches no game code.

## Open Questions

- How reliable is ESP-NOW, what does a match cost in battery, and how do the Sticky and a mixed X4 Pro/Sticky pair behave? This waits for the second device and blocks nothing else: radio logic is built and tested against `FakeLink` until then. Measure before tuning the 400 ms / 10 s / 100 KB values.
- Does internal heap recover after ESP-NOW teardown, or does leaving a Nearby match need `silentRestart()`? This can be measured on one device by bringing the radio up and down without a peer.
- Which icons make up the v1 set? The icon library epic picks them from Phosphor's fill set, and the API docs epic catalogs them (architecture AD-24).

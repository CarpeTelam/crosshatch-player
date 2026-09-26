---
type: initiative
title: "Games as content: the crosshatch-player v1 game platform"
parent: none
covers: [CAP-1, CAP-2, CAP-3, CAP-4, CAP-5, CAP-6, CAP-7, CAP-8, CAP-9, CAP-10, CAP-11]
after: []
assignee: ""
risk: high
---

# Games as content: the crosshatch-player v1 game platform

## Description

An X4 Pro or reTerminal Sticky becomes a pocket game console that a child can use alone. Games are script packages on the SD card, not firmware changes. An additive fork runtime runs them sandboxed and gives every game solo, pass-and-play, and two-device Play Nearby for free. Upstream CrossPoint merges stay clean. The spec owns the capabilities, constraints, and non-goals; the architecture spine owns the decisions every epic adopts.

## Outcome

The author writes a new game with an AI in an evening and a child plays it on a real X4 Pro with no firmware build; the spec's success signal is the measure.

## Requirements

The spec is the requirement source: `CAP-1` to `CAP-11`, with its Constraints and Non-goals. Epics cite those ids in `covers`.

## Done when

1. The spec's success signal is observed on a real X4 Pro: an AI-written game, dropped on the SD card as one `.cpgame`, is launched by a child and a round is finished alone, passing the device, and against a second device, without adult help.
2. Every CAP-1 to CAP-11 success check passes on a fork release build.
3. The next merge of upstream `develop` touches no game code, and the upstream-touch ledger job passes on it.
4. `default`, `x4c`, and `papermono` build with the game libraries compiled and unreferenced, and the x4pro image grows by at most 250 KB.
5. Sudoku, Ultimate tic-tac-toe, and Battleship are attached to a fork release as `.cpgame` assets.

## Boundaries

Follows the spec's capabilities, cut along the spine's layers: build and merge guardrails, the script runtime, the icon library, install and launcher, pass-and-play, Play Nearby, author docs, and first-party games. Not in scope: everything in the spec's Non-goals and the spine's Deferred table. Tracer path: Home, Games, and a solo Lua game drawn and played on an X4 Pro (baseline, then script runtime), before packages install through the inbox.

- Touch point: `platformio.ini`, `test/CMakeLists.txt`, the simulator's `simulator.ini` — build flag, test subdirectories, lint suppress (ledger rows 1 and 3); owner: epic-platform-baseline
- Touch point: `ActivityManager`, `HomeActivity` — the Games menu item and `goToGames()` (ledger rows 4 to 7); owner: epic-script-runtime
- Touch point: `CoverGridHomeUi` — the Games tile drawn from a `GameIcons` bitmap (ledger rows 8 and 9); owner: epic-icon-library
- Touch point: `lib/I18n/translations/english.yaml` — `STR_GAMES_*` keys appended (ledger row 2); owner: each epic appends its own keys
- Touch point: `lib/ZipFile`, `lib/miniz`, `lib/PngToBmpConverter`, `Storage` / `HalFile` — consumed unchanged by the installer; owner: epic-install-and-launcher
- Touch point: `GfxRenderer`, `RenderLock`, the sleep path, `UiAppHost`, `UiListActivity` — consumed unchanged; owner: epic-script-runtime
- Touch point: the existing web file manager and USB — consumed unchanged to put files in `/games/`; owner: epic-install-and-launcher
- Touch point: the fork release — first-party `.cpgame` assets attached; owner: epic-first-party-games

## References

- spec — _bmad-output/specs/spec-crosshatch-player/SPEC.md, sections Capabilities, Constraints, Non-goals, Success signal
- spec — _bmad-output/specs/spec-crosshatch-player/first-party-games.md
- spec — _bmad-output/specs/spec-crosshatch-player/glossary.md
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-1 to AD-24, Consistency Conventions, Deferred
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md
- constraint — AGENTS.md, sections Policy and Conventions that differ from defaults
- brief — _bmad-output/planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/brief.md, for history only

## Notes

- Open question: ESP-NOW reliability, battery cost, and Sticky or mixed-pair behaviour wait for a second device (spec Open Questions); epic-play-nearby owns the answer and builds against `FakeLink` until then.
- Open question: whether internal heap recovers after ESP-NOW teardown (spec Open Questions); epic-play-nearby owns the answer.

---
type: epic
title: "Game code builds everywhere and upstream merges stay clean"
parent: initiative-crosshatch-player-v1
covers: [CAP-11]
after: []
assignee: ""
risk: medium
---

# Game code builds everywhere and upstream merges stay clean

## Description

Lays the ground every other epic builds on: the `FREEINK_CAP_GAMES` build guard, the vendored Lua 5.5.1 engine, empty game library skeletons that compile on all five boards, host test suites wired into the test build, and the fork-only CI checks that cap upstream changes and flash growth. No game behaviour ships here.

## Outcome

Later epics add game code without breaking any board's build or an upstream merge; CAP-11's success check is the signal.

## Requirements

Completed at inception. This epic owns CAP-11 and the spec's Constraints on the build guard, C3 cost, flash cap, and upstream-change cap.

## Done when

1. A PR that changes an upstream path missing from `docs/crosshatch/upstream-touches.md` and the baseline allowlist fails the fork-only ledger job; one within the ledger passes; a `freeink-sdk` pointer change fails.
2. The ledger lists all 9 rows of AD-3 up front, and a trial merge of current `upstream/develop` has no conflict in a game path.
3. `x4pro`, `sticky`, `default`, `x4c`, and `papermono` build in CI with `lib/lua` (5.5.1, unmodified, srcFilter) and empty `lib/GameCore`, `lib/GameScript`, and `lib/GameIcons`; `FREEINK_CAP_GAMES=1` is set only in the six x4pro and sticky envs and the simulator envs.
4. `test/game_core` and `test/game_script` run in the host GoogleTest build in CI, including a Lua-on-host smoke test; the format check and `pio check` pass with `lib/lua` excluded by its own `.clang-format` and the `check_flags` suppress.
5. A fork-only workflow (a new file, not `ci.yml`) records the pre-games x4pro image size and fails a PR above that baseline plus 250 KB.
6. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

Build configuration, CI, the upstream-touch ledger, the vendored engine, and empty library skeletons. Owns ledger rows 1 (`platformio.ini`) and 3 (`test/CMakeLists.txt`) and the simulator's fork-owned `simulator.ini`. Not any game behaviour.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-2, AD-3, AD-4, Stack, Structural Seed
- constraint — _bmad-output/specs/spec-crosshatch-player/SPEC.md, Constraints
- constraint — AGENTS.md, Policy and Running and verifying

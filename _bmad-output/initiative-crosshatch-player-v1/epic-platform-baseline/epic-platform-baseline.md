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

This epic owns CAP-11 and the spec's Constraints on the build guard, C3 cost, flash cap, and upstream-change cap. Lines without a CAP id cite their source section.

- R1 (CAP-11): `docs/crosshatch/upstream-touches.md` lists the 9 AD-3 rows, the 1 reserve slot, and the baseline allowlist. (spine AD-3)
- R2 (CAP-11): A fork-only CI job fails a PR that changes a path present in `upstream/develop`, compared from the merge-base with full upstream history, that is in neither the ledger nor the allowlist, a `freeink-sdk` pointer move included; a trial merge of `upstream/develop` shows no conflict in a game path. (SPEC CAP-11; spine AD-3; AGENTS.md Policy)
- R3 (SPEC Constraints, build guard): `FREEINK_CAP_GAMES=1` is set only in `x4pro`, `sticky`, their `-gh_release` and `-gh_release_rc` variants, `simulator_x4pro`, and `simulator_sticky`; the game libraries compile, unreferenced, for `default`, `x4c`, and `papermono`. (spine AD-2)
- R4 (SPEC Constraints, engine pin): Lua 5.5.1 is vendored byte-for-byte in `lib/lua/`, compiled as C with every `LUA_COMPAT_*` off, with a fork-owned `library.json` srcFilter excluding the eight AD-4 files, kept out of the format check by `lib/lua/.clang-format` and out of `pio check` by the `--suppress=*:*/lib/lua/*` line. (spine AD-4, AD-3 row 1)
- R5 (SPEC Constraints, flash cap): a fork-only CI job builds x4pro with `FREEINK_CAP_GAMES` on and off on the same commit and fails a PR whose difference exceeds 250 KB. (spine Operational envelope)
- R6 (spine AD-1): `test/game_core` and `test/game_script` host GoogleTest suites run in the existing CI unit-test job. (spine AD-1, AD-3 row 3)

## Done when

1. A PR that changes an upstream path missing from `docs/crosshatch/upstream-touches.md` and the baseline allowlist fails the fork-only ledger job; one within the ledger passes; a `freeink-sdk` pointer change fails.
2. The ledger lists all 9 rows of AD-3 up front, and a trial merge of current `upstream/develop` has no conflict in a game path.
3. `x4pro`, `sticky`, `default`, `x4c`, and `papermono` build in CI with `lib/lua` (5.5.1, unmodified, srcFilter) and empty `lib/GameCore`, `lib/GameScript`, and `lib/GameIcons`; `FREEINK_CAP_GAMES=1` is set only in the six x4pro and sticky envs and the simulator envs.
4. `test/game_core` and `test/game_script` run in the host GoogleTest build in CI, including a Lua-on-host smoke test; the format check and `pio check` pass with `lib/lua` excluded by its own `.clang-format` and the `check_flags` suppress.
5. A fork-only workflow (a new file, not `ci.yml`) builds x4pro with `FREEINK_CAP_GAMES` on and off on the same commit and fails a PR whose image-size difference exceeds 250 KB.
6. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

Build configuration, CI, the upstream-touch ledger, the vendored engine, and empty library skeletons. Owns ledger rows 1 (`platformio.ini`) and 3 (`test/CMakeLists.txt`) and the simulator's fork-owned `simulator.ini`. Not any game behaviour.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-2, AD-3, AD-4, Stack, Structural Seed
- constraint — _bmad-output/specs/spec-crosshatch-player/SPEC.md, Constraints
- constraint — AGENTS.md, Policy and Running and verifying

## Notes

- Decision: entry 1 is the tracer bullet: the build flag, the three libraries, and the host suites wired through CI (2026-09-26).
- Decision: entries 2 and 3 add only new files and run beside entry 1; land entry 2 first so later PRs show the ledger job passing; entry 4 waits on entries 1 and 3 (2026-09-26).
- Decision: the game libraries and `lua` compile on every env through the fork-only, whole-file-guarded `src/games/GamesBuildAnchor.cpp`, not a `[base]` `lib_deps` entry, so `platformio.ini` gets no extra shared line; AD-2 records the mechanism (user's decision, 2026-09-26, replacing the same day's `lib_deps` choice).
- Decision: `.gitignore` and `.github/PULL_REQUEST_TEMPLATE.md`, already changed by the fork, join the AD-3 allowlist instead of being reverted (user's decision, 2026-09-26).
- Decision: the flash budget is measured as x4pro with games on minus the same commit with games off, not against a fixed baseline, so upstream merges never consume it; Done when 5 and the spine's Operational envelope were reworded (user's decision, 2026-09-26).
- Decision: the owner adds the ledger and size jobs as required checks in branch protection; entries 2 and 3 are hitl for that step (user's decision, 2026-09-26).
- Decision: the default refactor sweep is kept as entry 5 (2026-09-26).

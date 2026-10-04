---
title: 'e5-r2: stale docs and the input double drift guard (epic-pass-and-play retro F1, F3, F5, A2, V1, V2)'
type: 'chore'
ticket: ''
created: '2026-10-03'
status: 'done'
baseline_revision: '0264a1f4'
route: 'full'
review: 'thorough'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/epic-pass-and-play-retrospective.md'
---

## Intent

Bring docs that still describe ticket 5.13's removed hand-off time guard into line with the plain-tap behaviour (epic Notes line 138), record the owner's F5 decision, reword the i18n ledger row to what the fork does, and pin the screen input double's two copied constants to the device sources.

## Design Notes

- F1: the lifecycle table's HandOff and Result rows now say plain tap or Confirm and point to Taps. No other leftover wording of the rule was found by grep (`begun after`, `after it was pushed`, `time guard`, `slack`, `first push completed` in `docs/`); the one `time guard` hit is the Taps section's own "superseding the 2026-10-02 time guard".
- F5: one "What remains" item. The brief names the log line `dropped a tap or Confirm ...`; `GameMatchActivity.cpp` (loopPlaying, ~line 594) has no such line after ticket 13: the drop is logged `dropped a touch before the frame was on the panel`. The doc and the deferred entry use that actual text.
- F3: B6 rewritten as plain-tap checks (one tap, Confirm, home key, power click, accepted overlap), with the intro saying ticket 13 and epic Notes line 138 superseded the guard, the 2026-10-02 Decision and N1. T5 is "record only". The answer-table rows P18 to P20 already say superseded and are left as history.
- A2: ledger row 2 reads "`STR_GAMES_*` keys added, changed, or removed (the prefix only; no key outside it changes)", Guarded "prefix-scoped". The script is unchanged.
- V1/V2: both constants can be read robustly: `TOUCH_DOWN_SELECT_DELAY_MS` from `src/MappedInputManager.cpp` (file-local `constexpr` in an anonymous namespace) and `TOUCH_LONG_PRESS_MS` from freeink-sdk `InputManager.h` (private `static constexpr`), by the existing `constexprValue(readCode(...))` reader. `match.cmake`'s `MAIN_CPP_PATH` (nothing in the suite reads `main.cpp` any more) became `MAPPED_INPUT_MANAGER_CPP_PATH`; `INPUT_MANAGER_HEADER_PATH` stays. Two tests compare each with the double's constant; the double's comment names the device behaviour each stands in for. The double's other model gaps are unchanged. No function was moved or rewritten, so no guard needed a `git log -L`.

## Verification

- Host tests: `cmake -S test -B build/test -G Ninja ... && cmake --build build/test` under `/tmp/crosshatch-hosttest.lock`, then `ctest --test-dir build/test --output-on-failure -j`: 1622 of 1622 passed, including the four `CopiedConstantsTest` tests.
- Guard bites: with the double's constants temporarily 91 and 501, `TheDoublesTouchDownDelayIsTheDevicesSelectDelay` and `TheDoublesLongPressIsTheDevicesTouchLongPress` both failed (2 of 4); reverted, all passed.
- `python3 scripts/check_upstream_touches.py`: PASS; `python3 scripts/check_upstream_touches_test.py`: 18 tests OK.
- `./bin/clang-format-fix` twice, `git status` unchanged by the second run.
- Firmware builds and `pio check` were not run: the diff changes no firmware source (no `src/`, `lib/`), only docs, a host test, its CMake definitions and a test double.

## Review Triage Log

The lenses ran as context-free subagents over the diff.

All three lenses (adversarial, edge-case, verification-gap) ran as subagents and returned.

- Fixed: the log line also covers other awaiting-display drops (game-canvas "What remains" reworded); B6 step 1 says a tap before the frame is published may be dropped and is counted, not a failure; the deferred entry notes it resolves the earlier `CopiedConstantsTest` entry.
- Rejected: line-number citations (the brief names line 138; kept alongside the date); numeric thresholds, timings and recording places for B6/T5 and the trigger (the owner's wording is "record only"); the ledger claim (the script enforces paths only, as the ledger intro says); explain-hint and skip for uninitialised submodules (fails closed, TheSourcesAreRead names the path); semantic pinning of the double (out of scope).

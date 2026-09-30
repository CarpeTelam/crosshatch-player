---
title: 'The mode picker (GameModeActivity)'
type: 'feature'
ticket: '9'
created: '2026-09-29'
status: done
baseline_revision: '000cafe613404964b25c100715a64eb813929ee8'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/docs/contributing/touch-and-ui.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The launcher starts only solo and shows "None of its modes work here" for a game that only another mode can start; nothing lets a person choose among the modes a host offers (R8).

**Approach:** Add `GameModeActivity` on `UiListActivity`. The launcher opens a game whose `Manifest::check` is Ok: straight to the match when `check.modes` has one mode, otherwise it pushes the picker, whose row starts the match and whose Back pops to the launcher. `goHome` maps `GameMode` to Home's Games row.

## Boundaries & Constraints

**Always:** Start the match with `GameMatchActivity(renderer, mappedInput, manifest)` (leave `Start` defaulted); a pick of `pass` or `nearby` starts that same solo match today (the only mode it can run), logged, and epic-pass-and-play changes it. `Manifest::check` stays the only place a mode disappears. Allocate with `makeUniqueNoThrow`; locals under 256 B; `LOG_*`; text through `tr(STR_GAMES_*)`, keys in `english.yaml` only. Row 5 of `docs/crosshatch/upstream-touches.md` and `ActivityManager.cpp` change in the same commit, inside `#if FREEINK_CAP_GAMES`, with `GameModeActivity::NAME` shared by constructor and mapping. Keep every guard of `activateIndex` (bounds check, `app.clearTapFlash()`, OOM null check, `requestUpdate()` on a refused row).

**Never:** Touch `GameMatchActivity.*`, the installer, `lib/GameCore/**`, `GameHostCaps.*`, or entry 1's and entry 4's shared harness files (add files instead). No seat choice, no Continue.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| One mode | game Ok with modes = solo (or only pass, `pass` on) | tap replaces launcher with the match; no picker | none |
| Two modes | `pass` on, game solo+pass | tap pushes `GameModeActivity`; rows Solo, Pass and play, in that order | none |
| Pick | tap or Confirm on a row | picker replaced by the match; pass row logs that it plays solo | OOM: logged, stays |
| Back | Back on the picker | pops to the launcher (`finish()`), not Home | none |
| Home gesture | current activity `GameMode` | `goHome` selects Games | none |
| Unavailable | `check` not Ok | listed with its reason, tap refused (unchanged) | logged |

</frozen-after-approval>

## Code Map

- `src/activities/games/GamesLauncherActivity.{h,cpp}` -- `startable()` (solo-only) and its `check.ok()` branch in `unavailableText` go; `activateIndex` starts the match or pushes the picker. `startable`, `unavailableText`, `activateIndex` were all added in 80b68a92 and have no earlier history.
- `src/activities/games/GameModeActivity.{h,cpp}` -- new; model on `NetworkModeSelectionActivity` (rows built once in the constructor) and the launcher's `buildScreen` (safe-area margins).
- `src/activities/ActivityManager.cpp` -- `goHome` Games mapping, line `activityName == GamesLauncherActivity::NAME || ...`; add `GameModeActivity::NAME` and its include in the guarded block.
- `docs/crosshatch/upstream-touches.md` -- row 5 text.
- `lib/I18n/translations/english.yaml` -- after `STR_GAMES_UNAVAILABLE_INVALID`.
- `test/game_script/harness/` -- new `mode_picker.cmake` (suite), new `mode_picker.sources.cmake` (excludes `GameModeActivity.cpp` from the shared globbed libraries), `ModePickerTest.cpp`; `games_launcher.cmake` gains `GameModeActivity.cpp` in `game_launcher_src` (the launcher now references it); `GamesLauncherTest.cpp` test `AGameOnlyAnotherModeCanStartIsListedButDoesNotOpenYet` changes. Fixture pattern: `GamesLauncherTest.cpp` `ListTest`; `screen_stubs/ActivityManager.h` records `pushedActivities`, `replacements`, `asks.popped`.
- Stack semantics: `pushActivity` keeps the launcher (no onExit) and a pop only re-renders it; `replaceActivity` clears the stack.
- `_bmad-output/implementation-artifacts/deferred-work.md` -- mark the `## 4.8` mode item resolved by entry 9 (append under `## 4.9`).

## Tasks & Acceptance

**Execution:**
- [ ] `lib/I18n/translations/english.yaml` -- add `STR_GAMES_MODE_TITLE` "Choose a mode", `STR_GAMES_MODE_SOLO` "Solo" / `_DESC` "Play on your own", `STR_GAMES_MODE_PASS` "Pass and play" / `_DESC` "Take turns on this device", `STR_GAMES_MODE_NEARBY` "Play nearby" / `_DESC` "Play with another device"
- [ ] `src/activities/games/GameModeActivity.{h,cpp}` -- `NAME = "GameMode"`; static `modeCount(bits)` and `needed(bits)` (two or more of the three Mode bits); ctor takes renderer, input, `const Manifest&` (copied), mode bits; rows in solo, pass, nearby order; Back is `finish()`; pick starts the match
- [ ] `src/activities/games/GamesLauncherActivity.{h,cpp}` -- drop `startable`; refuse only `!check.ok()`; one mode: match as today; two or more: `pushActivity` the picker (OOM logged)
- [ ] `src/activities/ActivityManager.cpp`, `docs/crosshatch/upstream-touches.md` -- mapping and row 5 text
- [ ] harness files above -- suite `ModePickerHarnessTest`: one mode skips the picker; two modes push it with the right rows; a pick starts the match (pass row logs solo); Back pops; Confirm/next keys; three modes (picker built directly); `NAME`; `modeCount`; change the interim launcher test to the new behaviour
- [ ] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 4.9` heading with an entry recording the 4.8 item resolved

**Acceptance Criteria:**
- Given a game with one startable mode, when its row is tapped, then the match replaces the launcher and no picker exists.
- Given `pass` on and a solo+pass game, when its row is tapped, then `GameModeActivity` is pushed, lists Solo then Pass and play, and Back pops without going Home.
- Given the picker, when a row is tapped, then `GameMatchActivity` replaces it.
- Given the simulator, then Home, Games, and `counter` is 2 taps to a playing counter (at most 3).
- `check_upstream_touches.py` and `check_layers.py` pass; `default`, `x4pro`, `sticky`, `x4c`, `papermono` build.

## Implementation Notes

Implemented directly by the build agent from this plan (no separate implementation subagent). `GameModeActivity.{h,cpp}` are new; `mode_picker.cmake` builds `ModePickerHarnessTest` on `game_launcher_src`, which `games_launcher.cmake` now builds with `GameModeActivity.cpp` (one added line, in the lane's own suite file). `mode_picker.sources.cmake` keeps `GameModeActivity.cpp` out of the shared globbed libraries. The rendered skill files under `_bmad/render/` are untracked and not committed.

## Plan Change Log

## Review Triage Log

Pass 1. The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as context-free subagents over the staged diff and all returned. Counts: high 0, medium 0, low 8, false 2, deferred 2. Rows:

| # | Lens | Finding | Verdict, route | Evidence / action |
|---|------|---------|----------------|-------------------|
| 1 | blind, edge | A pick of pass or nearby, or a pass-only game's direct start, plays a solo match (a 2-seat game with one seat) | low, defer | Required by the ticket ("starts the solo match, the only mode the match can run"); unreachable on the real host (`pass` and `nearby` off, so `Manifest::check` leaves solo). Recorded for epic-pass-and-play in `## 4.9`, naming both paths. |
| 2 | edge | The launcher's direct one-mode start of a game with no solo mode is unlogged | low, patch | Added the `has no solo mode` log in `GamesLauncherActivity::activateIndex`; asserted in `AGameWhoseOnlyStartableModeIsPassAlsoGoesStraightToItsMatch`. |
| 3 | blind | Tests that pin the solo start read as the spec | low, patch | Comments on both tests say they are placeholders until epic-pass-and-play. |
| 4 | blind, gap | No test runs the `goHome` mapping | low, defer | `ActivityManager` is a double in the harness; the mapping shares `NAME` constants and the gap already holds for the launcher and match. Recorded in `## 4.9`. |
| 5 | blind | Untested: picker allocation failure, the launcher's state after Back, a one-mode picker | low, partly patch | Added `AOneModePickerIsOneRowAndStartsTheMatch`. The allocation failure needs an OOM hook for a non-array `new` the shared doubles lack; the launcher's state after Back is `ActivityManager`'s stack (verified in the simulator: `back-to-launcher.png`). |
| 6 | blind | `activateIndex` looks a row's mode up in a loop with no break | low, patch | `rowKind` holds the index into the mode table; no loop. |
| 7 | blind | `MAX_MODES` with no tie to the mode bits could overflow `rowMode[]` | false | The constructor's loop runs over `MODE_TEXTS[MAX_MODES]`, so it cannot make more than `MAX_MODES` rows. |
| 8 | blind | `GameModeActivity` holds a `Manifest` by value | false | The activity is heap-allocated by `makeUniqueNoThrow`; the launcher's listing already holds up to 64 entries, and only one picker exists. Unmeasured beyond that. |
| 9 | blind | `buildScreen` repeats the launcher's margin code | low, reject | A shared helper belongs on `UiListActivity`, an upstream file outside the ledger; the copy is 7 lines. |
| 10 | blind | Deferred-work wording and stale docs | low, patch | Reworded the evidence line; `formats.md` is the `## 4.8` item, outside `touches`. |
| 11 | edge | The picker's allocation failure does not request a redraw; a picker with no mode bits is empty | low, reject | The launcher's own match branch does the same; `needed()` keeps a picker from being built with fewer than two modes. |
| 12 | blind | `lineAfter(...) == "SoloAndPass"` is order dependent | low, reject | Rows are in the registry's name order by design, tested elsewhere. |
| 13 | intent | Readings: the diff implements the routing and screen (A), not an effect of the pick (B) or device reachability (C) | no finding | B is epic-pass-and-play's by the ticket; C follows `HostCaps.pass` being false until that epic. |

Pass 2 (source: orchestrator's independent review of 000cafe6..60b59a4b, one context-free reviewer; it found no high findings). Also recorded: the one-line edit to entry 8's `games_launcher.cmake` (`GameModeActivity.cpp` in `game_launcher_src`) is approved, same lane.

| # | Lens | Finding | Verdict, route | Evidence / action |
|---|------|---------|----------------|-------------------|
| 14 | verification-gap | Nothing pins that the picker's rows come from `check.modes`, not `manifest.modes` (mutation survived) | medium, patch | `AGameWithTwoModesOpensThePickerAboveTheLauncher` now has a solo, pass, nearby manifest with `pass` on and nearby off, and asserts Solo, Pass only. Rerun with the mutation: that test fails. |
| 15 | verification-gap | The row-to-mode lookup `MODE_TEXTS[rowKind[index]]` is unpinned | low, patch | Added `TheFirstRowOfAPassAndNearbyPickerStartsPass` and `TheSecondRowOfAPassAndNearbyPickerStartsNearby`; with the lookup mutated to `MODE_TEXTS[index]` both fail. |
| 16 | verification-gap | `ThePickerIsNamedByTheConstantGoHomeMapsToTheGamesRow` overclaims | low, patch | Renamed `ThePickerIsNamedGameModeAndNotTheLaunchersName`; the `goHome` deferral stands. |
| 17 | adversarial | The solo fallback is handed only to epic-pass-and-play, but `NEARBY_BUILT` can turn nearby on first | low, patch | The `## 4.9` deferral names both epics; both logs name the mode ("until it can run <mode>", "offers only <mode>"); tests match. |
| 18 | edge | Match allocation failure returns without `requestUpdate()` (picker and launcher) | low, patch | `requestUpdate()` before returning in both match branches, and in the launcher's picker branch (same cleared tap flash). |

## Design Notes

- The picker is pushed, not a replacement: Back returns to the same launcher (selection kept, inbox not reinstalled), and the match's `replaceActivity` clears the stack. A replacement picker would need `goToGames()` on Back, which re-runs the inbox install and loses the selection.
- Guards kept from `activateIndex` (no history before 80b68a92): the bounds check protects the listing array; `clearTapFlash()` stops a lingering flash graying the next screen; the null check is the `-fno-exceptions` OOM rule; `requestUpdate()` on a refused row shows the selection the tap moved.
- `startable()` and the `check.ok()` branch of `unavailableText` are removed: an Ok check has at least one startable mode (`CheckResult::modes` is 0 unless Ok), so the "Ok but not solo" reason is now false everywhere; `STR_GAMES_UNAVAILABLE_MODE` stays for `NoHostMode`.
- `GameModeActivity.cpp` needs the screen doubles, so like the launcher it is excluded from the shared globbed libraries (`mode_picker.sources.cmake`) and built by its suite.
- Layer table: the picker reaches `src/games`, `GameCore`, `UiListActivity` only, the same edges as the launcher; no table change.

## Verification

Results (worktree at baseline 000cafe6 plus this change, uncommitted when run; the commit hash is in the report):

- Host suites: `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- 1204 of 1204 passed after pass 2, including `ModePickerHarnessTest` and `GamesLauncherHarnessTest` (each run 20 times with `--gtest_repeat=20`, no failure) and `GamesLauncherHarnessTest`.
- `python3 scripts/check_upstream_touches.py` -- PASS (run again on the commit); `python3 scripts/check_layers.py` -- PASS, 461 edges.
- Firmware, each under the lock: pass 1 built `x4pro`, `default`, `sticky`, `x4c`, `papermono` (all SUCCESS); after pass 2, `x4pro` and `default` (both SUCCESS).
- Simulator (`sim.sh build x4pro`, `start`, `tap`, `ss`): Home, Games, Counter is 2 taps to a playing counter (`counter-playing.png`). The picker cannot open on the real host (`pass` is false), so `mode-picker.png` and `back-to-launcher.png` come from a temporary local build with `PASS = true` in `GameHostCaps.cpp` and a two-mode `pair` package, reverted and not committed: Games, Pair opens the picker (Solo, Pass and play), Back returns to Games with the same scroll and selection, and Pass and play logs `the match plays solo until pass and play exists` and starts the match. With the real caps that path is Home, Games, game, mode: 3 taps.

Screenshots (`story-modepicker-screenshots/`):
- `home.png` -- Home with the Games row.
- `games.png` -- the Games launcher.
- `counter-playing.png` -- Counter playing after two taps (Games, Counter).
- `mode-picker.png` -- the mode picker for a solo and pass game (temporary caps).
- `back-to-launcher.png` -- Back from the picker: the launcher as it was.

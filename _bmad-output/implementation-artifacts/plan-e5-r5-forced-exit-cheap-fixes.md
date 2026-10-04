---
title: 'e5-r5: forced-exit cheap fixes (skip the needless blank, flush on the TurnChanged pass)'
type: 'bugfix'
ticket: ''
created: '2026-10-03'
status: 'done'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '9829a1270809dddc62d20bd9878e85c3b82ec3a0'
context: ['/docs/crosshatch/game-canvas.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A hidden pass match's forced exit (sleep, Home, any Replace) pushes the 1.7 s half-refresh blank whenever it has a VM, whatever the panel shows, and the blank spends the 1,500 ms window the SD steps share (retrospective F6/F10, deferral `## 5.6`). Also `loopPlaying` returns after `handle(TurnChanged)` before `flushResume()`, so a move made one pass before an exit is written, if at all, after the blank.

**Approach:** The owner's "cheap fixes only" decision of 2026-10-03: (1) the blank is pushed only when `panel == Panel::Seat`, as the no-VM path and `leave()` already require; (2) the TurnChanged pass flushes the resume write. The 1,500 ms window and the order of the exit steps do not change (reordering is AD-20, the owner's open choice).

## Boundaries & Constraints

**Always:** never skip the blank when a seat's private frame could be on the panel; where `panel` cannot tell, keep the blank. Existing tests stay as they are unless they assert a blank where nothing private shows.

**Never:** change `FORCED_EXIT_DEADLINE_MS`, reorder the exit's steps, touch GameVM, SoloRounds, Session, Manifest, GameModeActivity, GameSaveStore, the epic file or the spine.

## I/O & Edge-Case Matrix

| Scenario | State at the forced exit | Expected | Error Handling |
|----------|--------------------------|----------|----------------|
| Seat's frame | Playing, Result, pause menu over a seat's frame, a new state not yet drawn, error view not drawn | blank pushed | none |
| Nothing private | HandOff, Paused-from-HandOff, Over, drawn error view, after Leave's blank | no push; resume write, delete retry, store flush run in the window | none |
| Move then exit | TurnChanged pass, then sleep | snapshot on the card before the blank | a failed write retries as before |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.cpp` -- `pushForcedExitBlank` (VM path; add `panel != Panel::Seat`), `loopPlaying` (TurnChanged branch), `onExit` comment; `panel` is set in `render` after a frame push (`Seat`), `renderHandOff` (`Blank`), `renderView` (`Seat` only for a canvas under the view and not Over, else `Other`), `pushBlank` (`Blank`); initial `Other`.
- `src/activities/games/GameMatchActivity.h` -- comment of `pushForcedExitBlank`.
- `test/game_script/harness/GameMatchTest.cpp` -- `HiddenPassTest` forced-exit cases; `ResumeMatchTest.cpp` -- `PassResumeTest`.
- `docs/crosshatch/game-canvas.md` -- The forced exit (step 2, the bound paragraph), Hidden pass states, Saves, "What remains".

## Tasks & Acceptance

**Execution:**
- [x] `GameMatchActivity.cpp` -- `pushForcedExitBlank` returns unless `panel == Panel::Seat`; `flushResume()` after `handle(TurnChanged)` -- the two cheap fixes
- [x] `GameMatchTest.cpp`, `ResumeMatchTest.cpp` -- failing-first tests for HandOff, Paused-from-HandOff, Over, a drawn and an undrawn error view, and the TurnChanged-pass write
- [x] `game-canvas.md`, `deferred-work.md` -- new rule, the What-remains entry, the `## e5-r5` entry

**Acceptance Criteria:**
- Given a hidden pass match in HandOff, Paused-from-HandOff, Over, or a drawn error view, when the device sleeps, then no blank is pushed and the SD steps run.
- Given a seat's frame on the panel (Playing, Result, a pause menu over it, an undrawn error view), when the device sleeps, then the blank is pushed as before.
- Given a move that passes the turn, when the loop pass that handles it ends, then the snapshot is on the card.

## Implementation Notes

Implemented directly by the build agent (the tests were written and run red against the base first; then the two code changes), not through an implementation subagent.

## Plan Change Log

## Review Triage Log

All four lenses ran as context-free subagents and returned (pass 1). Verdicts:

- Edge: Over's frame might be private -- false: `leave()` and the owner's decision already treat it as seat 0's public frame; the test pins the "Everyone: ..." text; before Over is drawn `panel` is stale `Seat`, so the blank is kept (new test `TheForcedExitBeforeOverIsDrawnPushesTheBlank`).
- Edge: fast refresh ghosting over Paused-from-HandOff and Over -- low, defer (deferred-work `## e5-r5`); the owner named those states.
- Edge: held-back HandOff keeps the prior `panel` -- false: prior is `Seat` (from Result) or Over/`Other` (public), both right.
- Edge: failed write throttle blocks the TurnChanged-pass write -- false for this change: `GameSaveStore` backoff is unchanged; tests advance past it as before.
- Edge: blocking SD write in the TurnChanged pass -- low, accepted: the same write already runs every pass in Playing and Result.
- Edge/Blind: loop-in-a-test fragility -- low, patched in part: same teardown pattern as `LeavingFromOverOrTheBlanksPauseMenuPushesNothing`; kept.
- Blind: doc says unmeasured while What remains says 1,654 ms -- medium, patched (paragraph now cites the owner's log); overlong doc lines -- low, rewrapped only my paragraphs.
- Blind: stale-`panel` states untested -- patched for Over; the rest fall on the safe side by construction (Design Notes), low.
- Blind: plan unfinished, no measurements -- patched below; red-run evidence: the new tests failed against the base before the fix (13 failures listed in the first run).
- Blind: deferred entry wording -- patched (`## 5.6` stays open).
- Verification-gap: no gaps. Intent-alignment: reading A implemented; the device-surface limits (1,654 ms in states that still blank, ghosting) are in game-canvas.md and the deferred entries.

## Design Notes

**What `panel` tells.** `Seat` = the last push was a seat's frame (`render`), or a view drawn over a seat's frame (Result, a pause menu over the canvas, not Over). `Blank` = the hand-off screen or the blank. `Other` = anything else (the screen before the match, Over, a drawn error view, a pause menu on no frame). Because `panel` is written only by a push, a state entered but not yet drawn keeps the previous value: Playing -> Result, Paused, or Error before the view is drawn all still read `Seat`, which is the safe side; HandOff held back for the round's turn seat keeps `Seat` too, so the blank is kept there. Epic Notes lines 101-102 and 113: a stuck VM stopped on the way to Error (no-VM path, unchanged), Paused over a seat's frame (`Seat`), Result's mover frame (`Seat`). No state was found where `panel` cannot tell. A resumed hidden match starts on `Other` (the title screen) until its blank is drawn, with no seat's frame yet: no blank, correct.

**Guards kept.** `pushForcedExitBlank` had one guard, `!forcedExit || !hiddenPass` (a user Leave pushes its own in `leave`; solo and open pass show nothing private): kept, one condition added. `loopPlaying`'s TurnChanged branch returns after the handle (the RoundOver and TurnChanged branches take the pass); the return stays, `flushResume()` goes before it. Why `loopPlaying` and not `handle()`'s Result case: `handle` is also called by `onExit` and tests, and a write inside a state transition would run for any caller; the loop pass is the one that already writes in every other state (`flushResume()` follows `store.flushIfDue` in `loopPlaying`, `loopView`, `loopHandOff`), so one line there is the smaller and more local change. RoundOver (to Over) needs none: Over never writes.

**Existing tests changed deliberately.** `TheForcedExitOnTheBlankPushesTheBlankAgain` asserted a blank over the hand-off screen: now `...PushesNothingAndLeavesTheWindowToTheSdSteps`. `TheForcedExitFromAScriptErrorWithItsVmStillPushesTheBlank` asserted a blank over a drawn error view: now one test that checks both the drawn view (no push) and the undrawn one (blank). Nothing else changed.

## Verification

Results (tree = this commit's working tree; the tests were red on the base before the fix):
- Host tests, `ctest` on `build/test` (Ninja): 1,632/1,632 pass (the base had 1,631 before the new cases; 10 cases are new or reworked).
- `scripts/check_upstream_touches.py`: PASS. `./bin/clang-format-fix` twice, then `git status` shows only the touched files.
- `pio run -e x4pro` (inside `check_flash_budget.py build on`) and `pio run -e default`: SUCCESS. `pio check` for `default` and `-e x4pro` at low, medium and high: no defects, rc 0.
- Flash and static RAM, `check_flash_budget.py`'s four steps (build on, build off, compare, objects) run the same way on base `9829a127` (a separate detached worktree under the scratchpad, removed afterwards) and on this tree: x4pro `firmware.bin` games on 5,934,416 B (base) and 5,934,432 B (tree), +16 B; games off 5,679,936 B on both; games-on minus games-off +254,480 B (base) and +254,496 B (tree), 21,984 B under the gate; static internal RAM +784 B on both (+0 B); `objects` clean on both (46 objects, largest mutable static 4 B). Not measured: the C3 `default` image's size.
- Review: the four lenses ran as context-free subagents (see the Triage Log). Not run: a simulator screenshot (no UI changed), the device (B7.6).

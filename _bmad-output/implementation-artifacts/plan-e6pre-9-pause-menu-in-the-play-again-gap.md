---
title: 'e6pre-9 the pause menu in the Play-again gap shows no previous-round canvas'
type: 'bugfix'
ticket: ''
created: '2026-10-04'
baseline_revision: 'e418a7f11076318d9bdf36b1150e322e60b085f7'
status: 'done'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick']
review_loop_iteration: 0
context:
  - '{project-root}/docs/crosshatch/game-canvas.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A pause menu opened in the Play-again gap (`vm->roundsStarted() < roundsStartedAwaited`) is drawn over the canvas's newest frame, which is the last round's, in solo and open pass, though `game-canvas.md` says the last round's board is never shown (deferred-work `## 5.4`, last item; `## 5.6`).

**Approach:** Owner decision 2026-10-04: apply the fix. `GameMatchActivity::canvasUnderView` is also false in the gap, in every mode, accepting solo's visible change. The new round's first frame still draws on a cleared screen in full, and Resume in the gap keeps the menu.

</frozen-after-approval>

## Implementation Notes

Oneshot (about 10 lines of source). Files: `src/activities/games/GameMatchActivity.{cpp,h}` (`canvasUnderView` and its header comment), `test/game_script/harness/GameMatchTest.cpp` (PlayAgainGapTest gains `roster()` and `lateStep()` hooks and two shared bodies; new `PlayAgainGapPassTest`, an open pass match, whose gap has no late step B because pass drops a tap once the round is over), `docs/crosshatch/game-canvas.md`, `deferred-work.md` (`## e6pre-9`, appended; `## 5.4` and `## 5.6` entries are append-only history). Spine: this tree's spine has no "target, not built until e6pre-9" note.

## Review Triage Log

The quick lens ran as a context-free subagent and returned.
- Guard covered the first round, whose menu no one redraws (medium, real): patched; the guard needs `awaited > 1`, as `pauseInGap` does.
- `game-canvas.md` Playing -> Paused row said "over the frame" (low, real): patched.
- Pass variant's pixel check vacuous without late step B (low, real): patched; it checks A's square at (100, 200) there.
- "Canvas is back" tests pass with or without the fix (low): kept as guards of the redraw; the "no canvas" tests are the ones that fail without it.
- `panel` is `Other` for a gap menu; a hidden match has no Playing gap (low): reasoned, left.
- Status/bookkeeping and firmware builds not yet run (false): done after the review.

## Design Notes

What each guard in `canvasUnderView` protects (history: introduced in 17d02483 for pass-and-play; before that `renderView` always drew the canvas under a view, acf780ea; e3r-x bd176acd added the gap skip in `renderCanvas`, not here):
- `state == Error || !vm`: the error view stands alone; no VM, no frame.
- Paused with `resumesTo == HandOff`: the device is between players, so no seat's frame may show.
- `seatShownRequest() >= seatAwaited`: before the VM serves the hand-off's request the front frame may be the last seat's.
- New: `roundsStarted() >= roundsStartedAwaited`: in the gap every frame is the last round's. `roundsStartedAwaited` starts at 1 and `roundsStarted()` at 0, so before a match's first frame the menu also sits on no canvas; there is none to draw (the first frame draws on a cleared screen anyway).

Other guards that stay: `renderCanvas`'s gap skip (sets `viewOnScreen`, changes no replay state) keeps a Resume in the gap on the menu; `viewOnScreen` plus `replay.forceFull()` make the round's first frame a cleared, full-refresh draw; `loopView`'s `gapWhenPaused` redraws the menu when the round starts, now over the new frame (`renderView` takes the canvas branch again). `panel` is `Other` for the gap menu (`canvas` false), which only a hidden match's forced-exit/Leave blank reads; a hidden match has no Playing gap (its Play again goes to HandOff).

## Verification

**Commands:**
- `pio run -e x4pro`, `pio run -e default`: SUCCESS. `pio check` (default) and `pio check -e x4pro` with all fail-on-defect levels: PASSED. `sim.sh build x4pro`: SUCCESS. No simulator screenshot (the gap needs a 2 s slow-restart staging).
- `./bin/clang-format-fix` twice: clean; `check_upstream_touches.py`: PASS.
- Host tests (`cmake` + `ctest` per AGENTS.md): 1666 of 1666 pass; the two "no canvas" tests fail with the `canvasUnderView` edit stashed (solo and pass), the "canvas is back" ones pass either way.

---
title: 'e6pre-2 no timer is delivered after the round is over'
type: 'bugfix'
ticket: ''
created: '2026-10-04'
baseline_revision: 'eed14f07'
status: 'done'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
context:
  - '{project-root}/docs/crosshatch/game-canvas.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A timer due after a round is over reaches the game's `input` in solo (and would in nearby): `lateTimer` tests seat 0, which only a pass round shows once over. It also reads "over" from the seat, so a roster with no local seat (`firstLocalSeat()` defaults to 0) would drop every timer and mislog it as late (deferred-work `## e5-r6`).

**Approach:** Owner decision, option (a): a timer due after the round is over is never delivered, in any mode, and the test is the explicit `status.over`. `SoloRounds::lateTimer(event, roundOver)` takes the flag; its callers (`GameVM::run`, `GameVM::stepHandOff`, `SoloRounds::play`, `SoloRounds::step`) pass `status().over`.

## Boundaries & Constraints

**Always:** Keep every existing guard (Design Notes). The drop log line and level are unchanged. Timers due before over still deliver.

**Never:** Edit `api-level-1.txt` (frozen; it states no old behaviour), the architecture spine, `.skills/`, or the SDK pointer.

</frozen-after-approval>

## Code Map

- `lib/GameScript/SoloRounds.{h,cpp}` -- `lateTimer`, `play`, `step`.
- `src/games/GameVM.cpp` -- `run` (solo and open loop, logs the drop) and `stepHandOff` (hidden pass and the replay of a held timer).
- `test/game_script/SoloRoundsTest.cpp`, `test/game_script/harness/GameVmTest.cpp` -- tests.
- `docs/crosshatch/game-canvas.md` -- the behaviour, in "Timers" and the touch-tag paragraph.

## Tasks & Acceptance

- [x] `SoloRounds::lateTimer` takes `roundOver`; `play` and `step` pass `session->status().over`; `step` asks before the NO_SEAT return.
- [x] `GameVM::run` passes `session->status().over` (the local; `playing` is null in the open path); `stepHandOff` asks before the NO_SEAT return, so a roster with no local seat still drops and logs.
- [x] Tests: solo drop (SoloRounds and GameVm), open pass drop (existing, kept), hidden pass drop (existing, kept), no-local-seat drop (both levels), timer before over delivers (solo and open pass, GameVm; solo, SoloRounds). The old `ASoloTimerDueAfterOverStillReachesTheLocalSeat` is inverted.
- [x] game-canvas.md; deferred-work `## e6pre-2`.
- Fixtures: none relied on the game cancelling its timer in `over` (pass-open and solo cancel in their `over` handler; harmless and kept).

**Acceptance:** every host test passes; the solo drop tests (SoloRounds and GameVm) fail on `eed14f07`'s `lateTimer(event, seat)` (reasoned, and the predicate assertions); the no-local-seat drop tests pass on it too (seat 0 was true there) and are regression guards; builds and checks clean.

## Design Notes

Guards in `lateTimer`/its callers, read with `git log -L` (one commit, c2954f7f, introduced `lateTimer`; its callers' order came from the e5-r6 plan):

- `event.kind == Timer`: only a timer is ever late; taps, `over`, `rejected` are not.
- The explicit over flag replaces `seat == 0`: seat 0 was the pass proxy for over; it also named a no-local-seat roster.
- `game.timer().accepts(event)` before the log in `run`, and as `stepHandOff`'s and `play`'s first line: a stale (re-armed or cancelled) timer is dropped silently, never logged as late.
- `madeUnderAnotherSeat` before the drop in `run`: kept in order (touch only).
- `stepHandOff`: the Result/HandOff hold of a timer is unchanged (never over there); the drop now precedes the `NO_SEAT` fail-closed return so the no-local-seat roster logs it; the log still comes after the round's `over` events and before any replay; `step` drops with no draw (no frame published for nothing).
- `SoloRounds::step`'s `NO_SEAT` early return is kept, after the late drop.

Spine sentences stating the old behaviour or silent on it (not edited here): ARCHITECTURE-SPINE.md line 387 (AD-23: "It fires a `timer` event into `input` for the local seat, or the current turn seat in `pass`", with the 2026-10-01 amendment on Result/hand-off, no after-over rule) and line 180 (the events bullet listing `timer`); `game-api-seed.md` line 191 (`ch.timer.after` row). `api-level-1.txt` states nothing about it; unchanged.

## Review Triage Log

Lenses (blind, edge-case, intent/verification-gap) ran as context-free subagents and all returned. No high code finding (the `playing` null in the open path was checked: `run` uses the local `session`).

| Finding | Verdict |
|---|---|
| No-local-seat tests pass on old code (blind, gap) | true, low: both old and new drop an over round there; relabelled as regression guards. The distinguishing case (no-local-seat, still playing: old dropped, new delivers to seat 0) is unreachable today; deferred. |
| No hidden-pass no-local-seat / before-over / nearby test (edge, gap) | deferred (below); the existing hidden-pass drop test and solo/open tests cover the code paths. |
| Held timer dropped silently on Play again / not logged if no seat is asked (edge) | accepted; doc reworded. |
| "Due before over" means dequeued before over (edge, blind) | existing behaviour, pinned by the queued-behind-winning-move tests. |
| Stale `seat 0` wording in a test message, misleading `play` comment (gap, blind) | patched. |
| Double check in run/step and stepHandOff/play (all) | false: redundant by design, each layer is called alone by other callers. |

## Verification

(filled in after the builds)
**Results (worktree, incremental, after review patches):** host ctest 1662/1662 (1656 baseline + 6 new, 1 inverted); `check_layers.py`, `check_upstream_touches.py` pass; `./bin/clang-format-fix` twice, nothing outside my files; `pio run -e x4pro` and `-e default` succeed; `pio check` (default and `-e x4pro`, fail on low/medium/high) exit 0; `sim.sh build x4pro` succeeds. No screenshots (no UI change).

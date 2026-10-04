---
title: 'e5-r6: drop a timer that falls due after the round is over'
type: 'bugfix'
ticket: ''
created: '2026-10-03'
status: 'done'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick', 'edge-case-hunter', 'verification-gap', 'blind-hunter', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: 'a4142f98a35e57e17d1fcbe24e80b9824dd6247c'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A Timer that falls due after a pass round is over reaches `rules.input(seat, ...)` with seat 0 (`seatShown` returns 0 once the round is over), though `game-canvas.md` calls seat 0 "never an input seat"; a script indexing per-seat tables on `ctx.seat` can error out of a finished match (retrospective F9).

**Approach:** The owner decided (2026-10-03) to drop such a timer, with a `LOG_DBG`, in the solo-round step and the hand-off step; a timer held in Result or HandOff still reaches the next turn seat once it is shown. No API-level change.

</frozen-after-approval>

## Implementation Notes

Oneshot: about 40 lines of code and tests across three source files.

- `lib/GameScript/SoloRounds.h/.cpp`: pure `static lateTimer(event, seat)` (Timer and seat 0); `step` (no draw, as a stale timer) and `play` (direct callers) drop it. lib/GameScript has no logging, so the LOG_DBG is in GameVM.
- `src/games/GameVM.cpp`: `run` (open pass, solo-shaped path: `lateTimer(event, rounds.shownSeat()) && timer.accepts(event)`, so a stale timer stays silent) and `stepHandOff` (after the NO_SEAT check, so a Result mover and the HandOff view are unchanged) log "Dropped a timer due after the round was over" and drop. Session.cpp, SeatShown.h need no change: the decision is "no input seat for a timer", not "which seat is shown".
- Guards read before editing (`git log -L` pitfall): `stepHandOff`'s stale-timer return (kept first), the HandOff/Result hold of a timer (kept, runs before the new check), `madeUnderAnotherSeat` (a timer is never touched by it; the new check sits before it only in `run`, after it in `stepHandOff`, which is order-neutral for timers); `SoloRounds::step`'s stale-timer and NO_SEAT returns (kept, new check after both).
- Other timer paths: a timer held for the next seat is delivered by `showSeatNow` through `stepHandOff` with the turn seat (tests `ATimerDueInResultIsHeldForTheNextSeat`, ModePicker/GameMatch suites pass); `showSeatNow` replays a held timer through `stepHandOff`, so one that were still held when the round is over would reach seat 0 and be dropped (a guard; a held timer exists only while the view is Result or HandOff, where no move can end the round, so no test reaches it). Solo and nearby rosters (one local seat) show their seat after `over`, never 0, so their timers are unchanged (see deferred-work `## e5-r6`).
- Existing assertions changed (the one place): `GameVmTest.cpp` `OpenPassATimerQueuedBehindTheWinningMoveReachesSeatZero` (pinned "timer for seat 0" delivered, count 1) became `...IsDroppedNotDeliveredToSeatZero` (count 0 and the drop line logged); its comment and the `TIMER_AFTER_OVER_GAME` comment changed with it. Added: `SoloRoundsTest.ASoloTimerDueAfterOverStillReachesTheLocalSeat` (solo timers after over unchanged, review finding 1), hidden-pass twin in GameVmTest, `PassRoundsTest.ATimerDueAfterThePassRoundEndsIsDroppedNotDeliveredToSeatZero` in SoloRoundsTest. The host tests live in `test/game_script/harness/GameVmTest.cpp` (the brief's `test/game_script/GameVmTest.cpp` does not exist).
- `docs/crosshatch/game-canvas.md` (~line 251): "after the round to seat 0 (R7)" now says the timer is dropped. `api-level-1.txt` only says `event timer` and states no seat, so it is unchanged: no API-level change.
- The three failing tests were run first and failed on the old code (3 failed of 4 selected; the hidden twin and open twin failed on the missing drop line, the SoloRounds test on a delivered "timer for seat 0").

## Review Triage Log

Lenses ran as context-free subagents and all returned: quick, edge-case-hunter, verification-gap (blind-hunter and intent-alignment not run; oneshot route is a quick review plus the two lenses AGENTS.md's firmware risk asks for).

1. Verification-gap: no test pins solo/nearby timers unchanged after over. Verdict medium; patch: added `ASoloTimerDueAfterOverStillReachesTheLocalSeat`.
2. Quick: held-timer replay through `showSeatNow` untested. Verdict low: the path is unreachable (a held timer exists only in Result/HandOff, where no move can end the round); the plan note now says so. Rejected, no code.
3. Quick: `run`'s `continue` skips `publishCommitted`/`logRound`, unlike a stale timer. Verdict low: nothing changed so nothing to publish; `madeUnderAnotherSeat`'s drop does the same. Rejected.
4. Quick and edge-case: the log check duplicates `step`'s decision. Verdict low: both use the one `lateTimer` predicate; `accepts` is the only other input. Rejected.
5. Quick and edge-case: a roster with no local seat has `firstLocalSeat()` 0 and would drop timers. Verdict false in practice: such a roster plays no round (it reads no input at all), nothing to drop. Rejected.
6. Edge-case: a dropped held timer leaves a game that re-arms only from its timer handler without ticks. Verdict false as a defect: it is the owner's decision (drop after over); a game over has nothing left to tick.
7. Edge-case: solo/nearby after over still deliver; partial fix. Verdict low, defer: recorded in deferred-work `## e5-r6`.
8. Edge-case: LOG_DBG may be compiled out so the drop-line assertions are vacuous. Verdict false: `test/game_script/harness/stubs/Logging.h` captures LOG_DBG; the tests failed before the fix.
9. Quick/verification: doc line wrapping in game-canvas.md. Verdict low, patched (reflowed).
10. Quick: Verification lists only the host run. Patched below.
11. Quick: `plan-e5-xr-cross-story-fixes.md` still says timers reach seat 0. Verdict low: historical plan, not edited (stays_out).

Follow-up (orchestrator's two extra lenses, run as context-free subagents and returned; `blind-hunter` and `intent-alignment` added to `lenses_ran`):
12. Blind hunter, low: the hidden-pass twin would pass if the drop moved to the held-timer path, and it did not assert the round-end events. Patched: it now asserts `over for seat 1` and `over for seat 2` once each, that the drop line follows `over for seat 2`, and that no further seat was requested (a held timer is dropped only after `showSeatNow`).
13. Blind hunter, low: the `game-canvas.md` paragraph was left with a short ragged line. Patched: reflowed.
14. Intent alignment: constraints met; one doc nit (same as 13) fixed. Also recorded in deferred-work: `lateTimer` infers "round over" from seat 0 (no-local-seat roster).

## Verification

**Commands:**
- host tests: `cmake --build build/test` then `ctest --test-dir build/test -j 4` -- 1628 of 1628 passed
- host tests after the review patch: `ctest --test-dir build/test -j 4` -- 1629 of 1629 passed; fork scripts: `scripts/check_upstream_touches.py` -- PASS; `./bin/clang-format-fix` twice -- no change, `git status` as expected
- `pio run -e x4pro`, `pio run -e default` -- both exit 0
- `pio check` (default) and `pio check -e x4pro`, each with `--fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- both PASSED
- `check_flash_budget.py` `build on`, `build off`, `compare`, `objects` -- exit 0 on both trees. Measurement (method: the script's four steps run on the base commit `a4142f98` in a separate `git worktree` checkout under the scratchpad, `freeink-sdk` copied from this tree because the submodule clone is blocked, and the same four steps on this tree before the commit; no figure from an earlier run): games-on `firmware.bin` base 5,933,408 B, this tree 5,933,568 B, so **+160 B flash** (the games-on minus games-off difference +253,584 B at the base, +253,744 B here); static internal RAM games-on minus games-off +784 B on both, so **+0 B**. `objects` found no static initializer.

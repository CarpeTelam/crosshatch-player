---
title: 'Partly-remote rosters show a local seat'
type: 'bugfix'
ticket: '1'
created: '2026-10-12'
baseline_revision: 'c28120a7b440fb4d3e67e2669cd149f187c03f9d'
status: done
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
warnings: ['oversized']
deferred:
  - summary: >-
      The fail-closed branches for a turn that is no seat (MatchRounds::step and drawShown, GameVM::showSeatNow and stepHandOff) have no test that reaches them.
    evidence: |-
      The two tests that reached them (a turn on a seat another device plays) were rewritten to the new rule, as the entry's verify asked; LuaGame::popSeat rejects a turn outside 1..n, so only a non-Lua IGameRules stub reaches them; SeatShownTest pins the pure function. A stub test in MatchRoundsTest (turn 0: no draw, no input, roundsStarted 0) and a hidden-VM one (request unserved, line logged) would close it.
    location: >-
      lib/GameScript/MatchRounds.cpp:step, src/games/GameVM.cpp:showSeatNow
    severity: low
  - summary: >-
      docs/crosshatch/game-canvas.md does not say what a device with several local seats shows while the turn is a remote seat's.
    evidence: |-
      The Hidden pass states section says the VM draws "the turn seat after showTurnSeat"; with this change a remote turn shows the local seat that last showed (else the first), reads taps as that seat, and discards their moves. The file is outside entry 1's touches; a later entry that touches the doc can state it.
    location: >-
      docs/crosshatch/game-canvas.md (Hidden pass states)
    severity: low
---

<intent-contract>

## Intent

**Problem:** A roster with some seats local and some not (R10) stalls on four paths: a local move that passes the turn to a remote seat is never drawn, a hidden hand-off to a remote turn seat is never served (the blank stays), a timer due while the turn seat is remote is dropped, and a round whose first turn is remote publishes no frame. A roster with no local seat also delivers a still-playing round's timer to seat 0.

**Approach:** When the turn seat is not local, `seatShown` names the local seat that last showed, else the first local seat; the open match (`MatchRounds`) and the hidden VM (`GameVM`) draw and read input as that seat, so each path is served. A roster with no local seat drops a timer and logs it.

## Boundaries & Constraints

**Always:** One local seat (every v1 nearby device) and every all-local pass roster behave exactly as today. A turn outside `1..n` still shows `NO_SEAT`. Every guard in `MatchRounds::step`, `GameVM::showSeatNow`, and `GameVM::stepHandOff` stays (see Design Notes). Fork-only files; nothing upstream changes.

**Never:** Touch `Session.*`, `games/**`, upstream files, or the CMake lists. No new setting, string, or wire field. No change to what the match draws for a remote seat beyond this rule (owner Decision 2026-10-11: nothing is added to the canvas, no spinner).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Path 1: move passes the turn away | open, 3 seats, local 1 and 2; seat 1 moves, turn becomes 3 | seat 1's frame is drawn after the move (a frame published) | none |
| Path 3: timer while turn is remote | same roster, turn 3 after seat 2 showed last | timer reaches seat 2 (last shown), one draw | none |
| Path 4: first turn remote | setup gives turn 3; start, then Play again | start draws the first local seat; `roundsStarted` moves; Play again draws the seat that last showed | none |
| Path 2: hidden hand-off to remote | hidden, `pass(3)`, local 1 and 2, turn 3; `showTurnSeat` | request served with the last-shown local seat (else the first); frame published, no blank; a held timer and a tap reach that seat; the tap is not a turn change | none |
| Taps while the turn is remote | same | read by the shown seat, its move discarded off turn | none |
| No local seat | `localSeats = 0`, round still playing, timer due | not delivered (never as seat 0); one log line | dropped |
| Turn not a seat | turn 0 or past `n` | `NO_SEAT`, nothing drawn or read | fails closed |
| One local seat, all-local pass | any | unchanged | none |

</intent-contract>

## Code Map

- `lib/GameCore/SeatShown.h` -- `seatShown(state, roster, status, mover)`: add `lastShown` (default `NO_SEAT`); the Playing/Paused tail returns the turn seat when local, else (turn is a seat) `lastShown` if local, else `firstLocalSeat()`. Early returns (HandOff, 0 or 1 local seat, Over, Result) unchanged.
- `lib/GameScript/MatchRounds.{h,cpp}` -- `draw(seat)` records a local seat drawn in `lastLocal`; `shownSeat()` passes it; getter `lastLocalSeat()` for GameVM; `noSeatTimer(event)` (Timer on a roster with no local seat) dropped in `step` and `play` after the late check.
- `src/games/GameVM.cpp` -- hidden path: pass `rounds.lastLocalSeat()` to its three `seatShown` calls; `stepHandOff`'s `passed` needs the shown seat to have been the turn seat before the move; log the no-seat timer drop in `run` (open) and `stepHandOff`.
- `src/games/CanvasGate.h`, `src/activities/games/GameMatchActivity.*` -- read-only: the gate waits on `seatShownRequest()` alone, which `showSeatNow` now serves.
- Tests: `test/game_core/SeatShownTest.cpp`, `test/game_script/MatchRoundsTest.cpp` (`PassRoundsTest`), `test/game_script/harness/GameVmTest.cpp`.

## Tasks & Acceptance

**Environment:** work only in the worktree `/home/user/wt/epic-play-nearby-lane-a` (never `/home/user/crosshatch-player`); run every command from it with absolute paths. Host tests build in its `build/test` (a baseline build of the unchanged tree may still be running or finished there) under `flock /tmp/crosshatch-hosttest.lock sh -c '<commands>'`; scratch goes under `/tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/6.1/`. Do not commit, push, mark the ticket, or edit anything outside this plan's Code Map files and Implementation Notes. Follow AGENTS.md: allocation, `LOG_*`, locals under 256 bytes, comments in the codebase's style.

**Execution:**
- [ ] Tests first, run red against the unchanged code (record the failing names in Implementation Notes): `SeatShownTest` (rewrite `ATurnSeatThisDeviceDoesNotPlayShowsNoSeat`; one-local-seat rows ignore `lastShown`); `PassRoundsTest` (rewrite `ATurnSeatThisDeviceDoesNotPlayGetsNoInputAndNoDraw`; add paths 1, 3, 4 and Play again on a three-seat roster with two local seats, and a `localSeats = 0` timer-while-playing test); `GameVmTest` (rewrite `AHiddenVmLeavesTheHandOffUnservedWhenTheTurnSeatIsNotThisDevices`: served with the first local seat, a held timer and a tap reach it, `turnsPassed()` stays 0; add the last-shown case with seat 2 moving then turn 3; add the no-local timer drop and log; one open partly-remote VM test)
- [ ] `lib/GameCore/SeatShown.h` -- the `lastShown` rule and its comment
- [ ] `lib/GameScript/MatchRounds.{h,cpp}` -- `lastLocal`, `lastLocalSeat()`, `noSeatTimer`
- [ ] `src/games/GameVM.cpp` -- the three calls, the `passed` guard, the no-seat timer log
- [ ] `./bin/clang-format-fix`; host tests; `scripts/check_upstream_touches.py`

**Acceptance Criteria:**
- Given a three-seat roster with two local seats, when a local move passes the turn to seat 3, then the mover's frame is drawn; when a timer is due while seat 3 has the turn, then it is delivered to the local seat that last showed; when a round starts or restarts with seat 3 to move, then a local seat's frame is published and `roundsStarted` moves.
- Given a hidden roster of the same shape with seat 3 to move, when the match asks `showTurnSeat`, then the request is served with a local seat's frame, and a tap on it is not counted as a turn change.
- Given `localSeats = 0` and a round still playing, when a timer falls due, then `input` never sees it and one line is logged.
- Given one local seat or an all-local pass roster, then every existing solo, pass, and one-local-seat test passes unchanged.

## Implementation Notes

- Red run (before any code change; for `SeatShownTest` only the unused `lastShown` parameter was stubbed in so the file compiles): failed `SeatShownTest.ATurnSeatThisDeviceDoesNotPlayShowsTheLocalSeatThatLastShowedElseTheFirst`, `SeatShownTest.TheLocalSeatsNeedNotBeTheFirstOnes`; `PassRoundsTest.ALocalMoveThatPassesTheTurnToARemoteSeatIsDrawnAsTheMover`, `ATimerDueWhileTheTurnIsRemoteReachesTheSeatThatLastShowed`, `ARoundWhoseFirstTurnIsRemoteDrawsTheFirstLocalSeatAndCountsAsStarted`, `APlayAgainWhoseFirstTurnIsRemoteDrawsTheSeatThatLastShowed`, `ARosterWithNoLocalSeatDropsATimerOfARoundStillPlaying`; `GameVmTest.AHiddenVmServesTheHandOffWithTheFirstLocalSeatWhenTheTurnSeatIsNotThisDevices`, `AHiddenVmServesTheLocalSeatThatLastShowedWhenTheTurnPassesToARemoteSeat`, `AnOpenVmOfAPartlyRemoteRosterShowsAndReadsAsALocalSeatWhileTheTurnIsRemote`, `ARosterWithNoLocalSeatDropsAndLogsATimerOfARoundStillPlaying`, `AHiddenRosterWithNoLocalSeatDropsAndLogsATimerOfARoundStillPlaying`. Green on `SeatShownTest.OneLocalSeatIgnoresTheLastShown` and `PassRoundsTest.ARosterWithALocalSeatStillDeliversATimerOfARoundStillPlaying` (regression guards, they pass before and after).
- Green: the full host suite (2070 tests) after the change; `GameVmTest.*` 5 runs in a row; `scripts/check_upstream_touches.py` PASS; `scripts/check_layers.py` and the layers and api-freeze script tests; `./bin/clang-format-fix` twice, no change.
- `GameVM::showSeatNow`'s NO_SEAT guard is kept for a turn that is no seat; its log line is now "Turn %u is no seat of the roster; no seat shown" (the old "not this device's" no longer holds for a remote seat).
- `MatchRounds.h` now includes `<SeatShown.h>` (for `NO_SEAT` as the `lastLocal` default).
- Not run (after review): `pio run -e x4pro`, `pio run -e default`, `pio check`, `sim.sh build x4pro`, the flash budget gate.

## Plan Change Log

## Review Triage Log

### 2026-10-12 — Review pass
Four lenses ran as context-free foreground subagents (blind hunter, edge-case hunter, verification-gap, intent-alignment auditor). The implementation subagent was re-engaged for the patches.
- verdicts: 21 findings — high 0, medium 0, low 15, false 6, maybe-false 0
- findings:
  - `[low]` `[reject]` Blind 1: the plan's Environment paragraph holds machine-local paths and build-agent orders — the fix edits this build's plan, which triage rejects; the paragraph is the implementer's only environment source.
  - `[false]` `[reject]` Blind 2: plan metadata contradicts its empty Triage Log — this entry fills the log; `oversized` is recorded in `warnings` and the story is one agreed scope (the Intent's four paths share one rule).
  - `[low]` `[patch]` Blind 3: stale comments after the rule — `MatchRounds.h`'s "When seatShown names NO_SEAT (a turn seat this device does not play)" and `SeatShown.h`'s "two roster shapes" were stale; fixed in both (comment only). `GameMatchActivity.h` still describes a pass match correctly. `docs/crosshatch/game-canvas.md` lacks the remote-turn behaviour but is outside the entry's `touches`: deferred (item 2).
  - `[low]` `[defer]` Blind 4: the fail-closed paths for a turn that is no seat lost their tests when the two stalling tests were rewritten — only `SeatShownTest` pins turn 0 and past n; `LuaGame::popSeat` rejects such a turn, so a Lua game cannot reach them; deferred (item 1).
  - `[low]` `[reject]` Blind 5: a hidden roster with a remote seat re-shows a local seat's private frame — the 2026-10-11 Decision (epic Notes, Q3) says a hidden hand-off to a remote turn seat "is served with that seat, with no blank"; no mode builds a mixed hidden roster (AD-12: hidden is pass, all local); a guard would add a branch for an unreachable roster.
  - `[low]` `[reject]` Blind 6: the no-local timer drop sits in four places with uneven logging — `play`/`step` are the host layer and GameVM logs once per reachable path (stale events return first in `run` via `accepts` and in `stepHandOff`'s first line); `step`'s check is needed so it does not go on to draw. Unreachable roster; unifying is a refactor.
  - `[false]` `[reject]` Blind 7: firmware checks not run — they are the plan's Verification and run after this review, under the build lock.
  - `[low]` `[reject]` Blind 8: `lastLocal` is lost across save and resume — a nearby match saves nothing (R4) and pass rosters are all local, so no resumed roster has a remote turn; saving it would change `resume.bin` (`docs/file-formats.md`).
  - `[low]` `[reject]` Blind 9: partly-remote fixtures duplicated across test files — `GameVmTest.cpp` opens an anonymous namespace (no linkage clash); the duplicates are a few lines, and a shared header would add a file to the tests' CMake lists, which `stays_out` names.
  - `[low]` `[patch]` Blind 10: the no-local timer drop was `LOG_DBG`, compiled out at the release `LOG_LEVEL=1` while R10 and the acceptance criterion say it is logged — both lines in `GameVM.cpp` are now `LOG_INF`.
  - `[false]` `[reject]` Edge 1: a tap by the sole local seat of a one-local-seat hidden roster no longer counts as a pass — hidden is pass-only and all-local, and a one-local-seat nearby match is an open match that never reaches `stepHandOff`; the old count was a spurious turn change.
  - `[low]` `[reject]` Edge 2: the `localSeatCount() <= 1` early return gives a one-local-seat or zero-local roster a seat for a bad turn — unchanged from today (that is "behave exactly as today"); the matrix row on a turn that is no seat is about rosters with two or more local seats, which `SeatShownTest` pins.
  - `[low]` `[reject]` Edge 3: a timer held in HandOff/Result is cleared silently by Play again — pre-existing behaviour (`timerHeld = false`), only reachable for a hidden roster, which is never zero-local.
  - `[low]` `[reject]` Edge 4: a game that re-arms its timer from `input` loses its chain on a zero-local roster — R10 says that roster drops a timer; no such roster is built.
  - `[low]` `[defer]` Edge 5: no test drives `MatchRounds::start/step`, `showSeatNow`, or `stepHandOff` with a turn that is no seat — same root as Blind 4 (deferred item 1).
  - `[low]` `[reject]` Edge 6: hidden privacy — same as Blind 5.
  - `[low]` `[defer]` Gap 1: the `NO_SEAT` early returns in `step`, `drawShown`, `showSeatNow`, `stepHandOff` have no remaining test; removing them fails nothing — same root as Blind 4 (deferred item 1; the lens's filed disposition is defer).
  - `[false]` `[reject]` Gap, other findings: `stepHandOff` logs the no-seat drop for every non-stale timer, consistent with `run` — the lens itself reports no defect.
  - `[false]` `[reject]` Intent 1: path 2's no-blank claim is untested at `GameMatchActivity` / `CanvasGate` — `CanvasGate::wait` and `open` and `renderCanvas` read only `roundsStarted()` and `seatShownRequest()`, which `showSeatNow` now serves (tests assert both); the match cannot build a partly-remote hidden roster (the Unknown the entry named, answered in Design Notes).
  - `[false]` `[reject]` Intent 2: paths 1, 3, and 4 are verified at the host seam only, firmware checks not yet run — no mode builds a partly-remote match before entry 10; firmware checks are the plan's Verification (this pass).
  - `[low]` `[reject]` Intent 3: `MatchRounds::step`/`play` drop the no-local timer silently while GameVM logs — the host layer has no logger by design; the VM is the one caller that logs.

## Design Notes

- Intent: R10 and the 2026-10-11 Decision (epic Notes, inception Q3) set the rule: "the device shows the local seat that last showed, else the first local seat". "Last showed" is kept for the VM's life, Play again included (the literal reading; a Play again whose first turn is remote shows the seat that last showed). Only a local seat counts: seat 0 and `NO_SEAT` frames never update it.
- Path 2 fits `GameVM::showSeatNow` alone, no `CanvasGate` change (the entry's Unknown): the gate waits on `seatShownRequest()` (`CanvasGate::wait`, `open`), which `showSeatNow` stores after the fallback seat's frame is published. Its `NO_SEAT` guard stays for a turn that is not a seat.
- Guards kept (AGENTS.md `git log -L`; `MatchRounds::step` e418a7f1 and c2954f7f): a stale timer drops before anything with no draw; the late-timer drop is asked before the seat, so a roster with no local seat drops it too; `NO_SEAT` reads no input. `showSeatNow`: drain the queue under the old view, fail closed on `NO_SEAT`, serve after the publish, then deliver the held timer. `stepHandOff`: stale drop, hold timers outside Playing, late drop before the seat, `madeUnderAnotherSeat`.
- `stepHandOff`'s `passed` compared `status.turn != seat`, true for any off-turn tap once the shown seat can differ from the turn seat; it now also requires the turn to have been `seat` before the move.
- A zero-local roster still draws seat 0 as today (only its timer is dropped, as R10 says); non-timer input there is untouched.
- Hidden rosters are all-local in v1 (AD-12), so the fallback is unreachable on a device; showing a local seat after a hand-off to a remote seat follows the Decision as written and is a privacy question only for a mixed hidden roster no mode builds.

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` (under the host-test lock) -- expected: all pass; the new tests failed on the unchanged code
- `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: clean
- After review, under the build lock: `pio run -e x4pro`, `pio run -e default`, `pio check` for `default` and `-e x4pro` (the three `--fail-on-defect` flags), `sim.sh build x4pro`; `scripts/check_flash_budget.py build on`, `build off`, `compare` -- expected: pass; growth over the epic base (+258,048 B flash, +784 B static RAM) within 9,504 B and 160 B

**Results (2026-10-12, worktree tree = the commit's code, base `c28120a7`):**
- Host: `cmake --build build/test` and `ctest --test-dir build/test -j` after the review patches: 100% of 2070 tests passed. The new tests were red on the unchanged code (names in Implementation Notes). No test double was added or extended.
- Fast checks: `scripts/check_upstream_touches.py` PASS (trial merge of upstream/develop clean; no upstream file touched); `./bin/clang-format-fix` twice, no change (no formatting-only change outside this entry's files).
- Firmware, under the build lock with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, after the review patches: `scripts/check_flash_budget.py build on` (= `pio run -e x4pro`) exit 0, `build off` exit 0, `compare` exit 0; `pio run -e default` SUCCESS; `pio check` for `default` and `-e x4pro` with the three `--fail-on-defect` flags PASSED; `sim.sh build x4pro` SUCCESS.
- Budget (R15; incremental tree with the shared cache, not a fresh clone, so not CI's figures): x4pro `firmware.bin` 6,213,808 B games on, 5,955,312 B off, +258,496 B; static internal RAM 190,248 B on, 189,464 B off, +784 B. The epic base Measurement (`7bfd8991`, same code as `c28120a7`) is +258,048 B and +784 B, with the games-off size identical (5,955,312 B), so this entry's growth over the base is **+448 B flash and +0 B static RAM**, within 9,504 B and 160 B. The base figure is the epic Notes' recorded one, not re-measured at `c28120a7` in this tree.
- Screenshots: none (the verify names none; no match builds a partly-remote roster to show on the simulator).

## Auto Run Result

**Summary.** When the turn seat is not local on a roster with several local seats but not all, the device now shows the local seat that last showed, else the first local seat: `seatShown` takes `lastShown`, `MatchRounds` records the last local seat it drew and passes it, and `GameVM`'s hidden path does the same. That serves paths 1 (a move passing the turn away is drawn), 3 (a timer reaches that seat), 4 (a round with a remote first turn publishes a frame and starts) in the open match and path 2 (a hidden hand-off to a remote turn seat is served with no blank) in `GameVM::showSeatNow` alone, with no `CanvasGate` change. `stepHandOff` no longer counts an off-turn tap by the shown seat as a turn change. A roster with no local seat drops a still-playing round's timer, logs it (`LOG_INF`), and never delivers it as seat 0.

**Files.**
- `lib/GameCore/SeatShown.h`: the `lastShown` rule and comment.
- `lib/GameScript/MatchRounds.{h,cpp}`: `lastLocal`, `lastLocalSeat()`, `noSeatTimer`.
- `src/games/GameVM.cpp`: `lastLocalSeat()` in three calls, the `wasTurnSeat` guard, the no-seat timer drop and log.
- `test/game_core/SeatShownTest.cpp`, `test/game_script/MatchRoundsTest.cpp`, `test/game_script/harness/GameVmTest.cpp`: the two stalling tests rewritten, a test per path, the no-local timer, one-local-seat guards.
- This plan.

**Review.** Four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), 21 findings: high 0, medium 0, low 15, false 6. Patched 2 (low): the no-seat timer log level (`LOG_DBG` to `LOG_INF`), and the stale `MatchRounds.h` and `SeatShown.h` comments. Deferred 2 (low, in `deferred`): the fail-closed no-seat branches have no test that reaches them, and `docs/crosshatch/game-canvas.md` does not state the remote-turn behaviour. Rejected, with reasons in the Review Triage Log: the plan's environment text, hidden-roster privacy (the Decision and AD-12 settle it; no mode builds one), `lastLocal` across resume, fixture duplication, and the four-place timer-drop logging, among others. `followup_review_recommended: false` (no patched entry above low).

**Verification.** As in Verification Results above: 2070 of 2070 host tests, upstream-touch ledger PASS, format clean, both builds, `pio check` on `default` and `x4pro`, `sim.sh build x4pro`, flash budget compare exit 0 with +448 B flash and +0 B static RAM over the epic base.

**Residual risks.**
- A mixed hidden roster (some seats remote) would show a local seat's frame after a hand-off to a remote seat, as the Decision says; no mode builds one in v1.
- A zero-local roster still draws seat 0 and reads non-timer input as seat 0, as today; only its timer is dropped.
- "Last showed" lives in the VM and is not saved: a resumed VM starts from the first local seat (no partly-remote match is saved).
- The budget delta subtracts the epic's recorded base from a tree measured here; the games-off size matches the base exactly.
- Device behaviour is unmeasured: no partly-remote match exists until entry 10.

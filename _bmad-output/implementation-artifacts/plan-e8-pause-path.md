---
title: 'e8-pause-path: a match-level test that a pause before the first frame is not taken out of ch.time.ms'
type: 'chore'
ticket: ''
created: '2026-10-11'
baseline_revision: '26933a33a20b349a8a935bbccf8de0e1e463e8ff'
status: 'built'
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: []
deferred: []
---

<intent-contract>

## Intent

**Problem:** The A-2 fix (a pause before `LuaGame::load` is no longer subtracted from play time, `plan-e8-ai8-sweep.md`) is pinned at the ledger (`PauseClockTest`) and `LuaGame` (`PlayTimeTest`) levels, but no test drives the device path: Back through `GameMatchActivity`, `GameVM::matchPaused()` on the loop task while the VM task has not yet taken its baseline, then Resume. `GameVM` hands its own ledger in as `HostPorts::paused` (`src/games/GameVM.cpp:83`), which nothing tests. `_bmad-output/implementation-artifacts/deferred-work.md` `## e8-ai8` records it.

**Approach:** A `MatchTest` in `test/game_script/harness/GameMatchTest.cpp`, modelled on `TheGameClockLeavesThePauseOut`, holds the VM task with `fakertos::arm`, presses Back, advances the clock, resumes, and reads `ch.time.ms` through a tap. It must fail on the pre-fix code, proved once by a scratch mutant. Test-only: no change to `src/`, `lib/`, `games/` or any upstream file.

</intent-contract>

## Implementation Notes

Oneshot: one fixture class and three tests in one existing file (`test/game_script/harness/GameMatchTest.cpp`, after `TheGameClockLeavesOutTwoPausesInOneMatch`); no production change, no new game fixture (`CLOCK_GAME` fits), no change to `src/`, `lib/`, `games/` or an upstream file.

- `PauseBeforeTheLoadTest` (derived from `MatchTest`) holds the VM task with `fakertos::arm()` before `enter()`, so it stops at its first clock read; Back (a real `input->click`, through `GameMatchActivity::loopPlaying` to `handle(MatchEvent::Back)` to `vm->matchPaused()`) then runs on the loop task; the clock advances; `fakertos::release()` lets the VM load. `playTimeAtATap()` returns the text the game logged after `clock ` (exactly one such line), so a test compares the whole value.
- `APauseOpenWhenTheGameLoadsLeavesOutOnlyItsTimeAfterTheLoad`: pause, +30000 ms, load, +4000 ms still paused, Resume, +2000 ms, tap: `ch.time.ms` logs 2000.
- `APauseClosedBeforeTheGameLoadsIsNotLeftOutOfWhatFollows`: pause, +30000 ms, Resume with the VM still held, +1500 ms, load, +2000 ms, tap: 2000.
- `APauseAfterTheLoadIsLeftOutBesideOneBeforeIt` (added after review): pause, +30000, Resume, load, +1000, Back, +5000, Resume, +2000, tap: 3000.

## Plan Change Log

## Review Triage Log

### 2026-10-11 — Review pass 1 (blind-hunter, edge-case-hunter, verification-gap, intent-alignment)

- verdicts: 14 findings — high 0, medium 0, low 11, false 3, maybe-false 0 (blind 7, edge-case 7; the verification-gap lens found no gaps and the intent-alignment audit reported no divergence).
- findings:
  - `[low]` `reject` blind: `deferred-work.md` `## e8-ai8` is not updated — the brief reserves that file (and the retrospective) to the orchestrator, who carries it when marking; the plan names the residual for it.
  - `[low]` `patch` blind: the only mutant was in `LuaGame::load`, not the `GameVM` wiring the Problem names — ran the second mutant (`HostPorts` ledger pointer `nullptr`) once; the open-pause and after-load tests fail on it, the closed-pause test alone does not (Design Notes).
  - `[low]` `patch` blind, edge: the closed-pause test cannot tell a ledger that subtracts correctly from one that never subtracts — added `APauseAfterTheLoadIsLeftOutBesideOneBeforeIt` (a second pause after the load; 3000 vs 8000 for a ledger that never subtracts) and a comment in the closed test saying it pairs with the other two.
  - `[low]` `patch` blind: the hold point's only evidence is a reverted scratch `fprintf`, and a line reference is brittle — the plan now cites `LuaGame::load`'s baseline read by symbol, and the fixture comment names the dependency (a clock read or log line added to the VM task before it moves the hold, and `pauseBeforeTheLoad` then fails with "the game loaded before the pause").
  - `[low]` `reject` blind: the seqlock-retry coverage is unasserted — the Design Notes already say the test does not assert it; adding a retry counter to `PauseClock` would be a production change the intent excludes.
  - `[low]` `patch` blind: evidence lives only in the scratchpad — the mutant readings and the repeat count are copied into the Design Notes and Verification.
  - `[false]` `reject` blind: the plan's front matter and logs are unfinished — they are written here, in finalize.
  - `[low]` `patch` edge: a failed `ASSERT_*` in a void helper lets the test go on — the test bodies wrap each helper in `ASSERT_NO_FATAL_FAILURE`.
  - `[low]` `patch` edge: `logHas("clock 2000")` is a substring match (would pass 20000) — `playTimeAtATap()` returns the whole logged value and the tests `EXPECT_EQ` it (a failure now prints the reading, `"0"`, `"6000"`, `"8000"` in the mutant runs).
  - `[false]` `reject` edge: `waitParked` may hold another task's thread — the test creates no task but the VM's, and the order `load entered`, `parked`, `baseline taken` in the scratch run placed the hold inside `LuaGame::load`.
  - `[low]` `reject` edge: no Home-gesture variant — `handleHomeGesture` and Back both reach `handle()`'s one `to == MatchState::Paused` line that calls `vm->matchPaused()`; `TheGameClockLeavesOutAPauseMadeByTheHomeGesture` pins the Home event after the first frame, and a third pre-load test would repeat the arithmetic.
  - `[low]` `patch` edge: no test with two pauses straddling the load — the same new test as above.
  - `[low]` `reject` edge: no test of a pause between the baseline read and the first frame — such a pause is an ordinary pause after the load, which the ledger arithmetic treats as one after the baseline (`PlayTimeTest`, `TheGameClockLeavesThePauseOut` and the new after-load test); the intent allowed "before load or between load and first frame" and asks for one, named in Design Notes. Recorded as the residual there.
  - `[false]` `reject` edge: the `e8-ai8` entry is not updated — same as the first row; duplicate.
  - verification-gap: no gaps found. intent-alignment: reading R1 (device path, pause before the baseline) with open and closed pauses implemented; the surface is the intent's own (real `GameMatchActivity` and `GameVM`); the one noted limit is that `fakertos` holds the thread exactly at a clock read, which a device only approximates by timing (Design Notes, Doubles).

## Design Notes

**Which gap the test exercises.** Between the VM task starting and `LuaGame::load` taking its baseline: the task is inside `load()` (entered, `lua_newstate` done) and held at its first clock read, which is the `ports.paused->read(ports.clock)` baseline read (`LuaGame::load`'s `ports.paused->read(ports.clock)`; `GameRandom::next32` and the arena read no clock). Evidence, from a scratch `fprintf` in `load()` and in the test, reverted: the lines came in the order `load entered`, `parked` (after `waitParked()` returned), `baseline taken start=31000 paused=30000` (open case) and `start=32500 paused=30000` (closed case). So the pause is on the loop task strictly before the baseline, with the VM task already in `load()`. It is not earlier than `load()` entry: the gate holds only at a clock read or a log line, and the VM task makes neither before `load()` (`GameVM::run` calls `setStackFloor` and `heap.create<Session>`, no clock). A pause before `load()` entry but after task creation would lead to the same baseline read, with no observable difference; no harness hook places Back there, and none is needed. The residual: the first-frame gap after the load (baseline taken, first frame not yet published) is not driven; a pause there is an ordinary pause after the load, which `TheGameClockLeavesThePauseOut` and its neighbours cover.

**What the hold does to `read()`.** The held `read()` has already loaded the ledger word (running, 0) before it blocks on the clock; Back then writes a paused word, so on release the reload differs and `read()` retries. The logged baseline (`paused=30000` with `start=31000`) is the retried pair. The test therefore also drives the seqlock's retry through the device path; it is not a separate claim the test asserts.

**Mutants (each run once, reverted; `git status` clean of `src/` and `lib/` after).** Logs in `/tmp/claude-0/-home-user-crosshatch-player/3dc27d62-67b9-55f1-ba2a-c9849c6890c8/scratchpad/e8-pause-path/mutant1.log` and `mutant2.log`; the readings below are copied from them.
- `LuaGame::load` with `bindings.pausedAtStartMs = 0;` in place of `start.pausedTotalMs` (the pre-fix behaviour): all three tests fail, each reading `"0"` where 2000, 2000 and 3000 are expected.
- `src/games/GameVM.cpp` `HostPorts{random, clock, log, store, nullptr}` in place of `&paused` (the wiring the deferral names, no ledger): the open-pause test reads `"6000"` (expected 2000) and the after-load test reads `"8000"` (expected 3000) and fail; the closed-pause test reads 2000 and passes, so it alone cannot see a missing ledger, which is why the other two exist.
Unmutated: the three pass, 300 repeats with no failure (`--gtest_repeat=300`), and the full host suite passes, 2,059 of 2,059.

**Doubles.** No double is added or extended. `fakertos`'s gate stands in for a VM task slow to reach its first clock read (a device task is created and scheduled on its own core, so Back can land first); the host gate is more permissive than the device in that it holds the thread exactly at a clock read, which the device only approximates by timing.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake --build build/test && ctest --test-dir build/test --output-on-failure -j8'` -- expected: every test passes, the three new ones included. Result: 2,059 of 2,059 pass, `PauseBeforeTheLoad` x3 registered; `GameMatchHarnessTest --gtest_filter='*PauseBeforeTheLoad*' --gtest_repeat=300` has no failure.
- `./bin/clang-format-fix` twice, then `git status` -- expected: nothing new. Result: no change beyond the test file. `python3 scripts/check_upstream_touches.py` -- passes (the diff touches only a fork test file and this plan).
- Scratch mutants (each once, reverted): `LuaGame::load` sets `bindings.pausedAtStartMs = 0`; `GameVM`'s `HostPorts` takes `nullptr` for `&paused` -- expected: the tests fail (Design Notes: all three, and two of three). Result as the Design Notes record.

No firmware checks: the diff is one host test file and the plan, so `pio run`, `pio check` and `sim.sh build` do not apply.

## Auto Run Result

**Summary:** `GameMatchTest.cpp` gains `PauseBeforeTheLoadTest` and three tests that drive the A-2 fix through the real device path: the VM task is held at its first clock read inside `LuaGame::load` (before the baseline), Back reaches `vm->matchPaused()` on the loop task, time passes, Resume, and a tap logs `ch.time.ms`. They cover a pause open at the load, a pause closed before it, and a pause before plus one after. Test-only; no firmware checks (the diff is one host test file and this plan, so `pio run`, `pio check` and `sim.sh build` do not apply).

**Files:** `test/game_script/harness/GameMatchTest.cpp` (the fixture and three tests); this plan.

**Review:** four lenses ran as context-free subagents. Patches applied: 7 rows (all low; 6 changes, two rows share the after-load test): the `GameVM` wiring mutant, the after-load test, the hold-point comment, the evidence copied into the plan, `ASSERT_NO_FATAL_FAILURE`, the whole-value comparison. Rejected: 7 rows (see the log): the `deferred-work.md` update (the orchestrator's), the unasserted retry, the Home variant, the between-baseline-and-first-frame variant, and three false ones (unfinished front matter, the parked task, the duplicate `deferred-work.md` row). Deferred: none.

**Verification:** as above. The pre-fix mutant fails all three tests (reading 0); the wiring mutant fails two (6000 and 8000).

**Residual risks:** (1) the hold is the host gate's, exactly at a clock read; on the device, the VM task and Back race by timing, so `## e5-run4`'s device check stays the proof of the real ordering. (2) A pause between the baseline and the first frame is not driven separately; it takes the ordinary after-load path. (3) A clock read or log line added to the VM task before `LuaGame::load`'s baseline moves the hold; `pauseBeforeTheLoad` fails with "the game loaded before the pause" instead of passing silently. The `## e8-ai8` deferral is settled for the host path and is the orchestrator's to carry into `deferred-work.md`.

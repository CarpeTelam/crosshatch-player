---
title: "The forced exit's blank hand-off"
type: 'feature'
ticket: '6'
created: '2026-10-01'
status: 'built'
route: 'full'
route_source: 'auto'
baseline_revision: 'dbc964f7f5283988f64947afb25851c95d137645'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.skills/scope-discipline/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** When a hidden pass match is left by sleep or any Replace, `onExit()` stops the VM and writes to the card but leaves whatever the panel shows, often one seat's private frame, on screen while the device sleeps (R6; spine AD-12, AD-20 as amended 2026-10-01).

**Approach:** Inside `stopVm()`, only in a forced exit of a hidden pass match, right after `vm->stop()` returns (joined or timed out) and before `flushResume()` and any abandon, draw entry 4's blank (`FrameReplay::drawBlank`) and push it with `HalDisplay::HALF_REFRESH` under the `RenderLock` `ActivityManager` already holds, logging one line. The SD steps keep their 1,500 ms deadline from the start of `onExit()`, so the push's time counts against it. `game-canvas.md` records the order, the bound (the 502 ms device figure), and the resume-before-store order.

## Boundaries & Constraints

**Always:** Never take `RenderLock` in `onExit()` or anything it calls (12cc816); `fakelock::selfDeadlocks` stays 0. The blank push needs `forcedExit`, a hidden pass lifecycle (`lifecycle.hiddenPass()`), and a VM (`stopVm`'s existing `if (!vm) return;`). Every existing guard in `onExit()` and `stopVm()` stays, in order (Design Notes). Solo and open pass matches, and a user Leave of any match, push nothing new. The double's `displayBuffer` moves no clock; tests say so where they rely on it. AGENTS.md memory rules (no new statics, locals under 256 B), `LOG_*` only.

**Never:** Edit `src/games/FrameReplay.*` (calls only), `GameSaveStore.*`, `GameModeActivity.*`, `GamesLauncherActivity.*`, `lib/GameCore/**`, `english.yaml` (no new string), fixtures, the epic file, or the spine. No pass `resume.bin` (entry 9). No change to `FORCED_EXIT_DEADLINE_MS`, `STOP_TIMEOUT_MS`, or the deadline rule; the blank push itself is not an SD step and is not gated by it.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Sleep on a seat's frame | hidden pass, Playing, VM idle | one push: blank, `HALF_REFRESH`, no text; after the join, before any SD op; log line; then the store flush | — |
| Sleep in Result / HandOff | the banner over the mover's frame / the blank | the same push (the blank again from HandOff) | — |
| Stuck VM | hidden pass, VM held in `ch.log` | push about 500 ms after `onExit()` began, before the abandon's "did not stop within" line; whole exit within about 1,030 ms | VM abandoned and leaked as today |
| Push past the window | the push takes the clock past 1,500 ms | the `ch.store` flush is skipped and logged ("skipped the ch.store flush") | as every late SD step |
| No VM | hidden pass in Error, or after Leave | no push (no seat's frame is on the panel) | — |
| User Leave | hidden pass, Leave from the pause menu | no `HALF_REFRESH` push | — |
| Solo, open pass | forced exit | no push at all during `onExit()`, no log line | — |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.cpp` -- `onExit()` (:197) notes `forcedExitBeganMs`, sets `forcedExit`, `handle(ForcedExit)` (Leaving), `stopVm()`, `retryResumeDelete(true)`, `flushStore()`; update its order comment. `stopVm()` (:317): `if (!vm) return;` → `vm->stop(STOP_TIMEOUT_MS)` → **new: `pushForcedExitBlank()`** → `flushResume()` → reset or `abandonVm()`. New private `pushForcedExitBlank()`: return unless `forcedExit && lifecycle.hiddenPass()`; `replay.drawBlank(renderer, viewport)`; `renderer.displayBuffer(HalDisplay::HALF_REFRESH)`; `LOG_INF("GAME", "%s: forced exit: blank hand-off screen pushed (half refresh)", manifest.id)` (log after the push, so its line follows the panel write). Do not touch `renderHandOff()` (:673, entry 4's render-task push), `viewOnScreen`, routing, or `passScreenShown`: the match is Leaving and render draws nothing in Leaving. `leave()` calls `stopVm()` with `forcedExit` false: unchanged.
- `src/activities/games/GameMatchActivity.h` -- declare `pushForcedExitBlank()` beside `stopVm()` with a comment (AD-12; render task idle because the caller holds `RenderLock`, so `replay` and the framebuffer are safe from the loop task here); update the `onExit()` and `stopVm()` comments and the class comment's hidden paragraph (the forced exit's blank).
- `src/games/FrameReplay.{h,cpp}` -- `drawBlank` reused as is (entry 4 triage row 21 kept its icon lookup); no edit.
- `test/game_script/harness/GameMatchTest.cpp` -- `HiddenPassTest`: generalize `enterHidden(id = "pass-hidden")`; add a `sleep()` helper (`exited = true; activityManager.exitHolding(*activity)`) and a local hidden game with `ch.store.set` on each tap (`installGame`) for the store cases. `renderer->onDisplay` records, at the push, the clock since the exit began, `fakesd::sim().ops.size()`, and whether "did not stop within" is logged. `PassMatchTest`/`MatchTest` for open pass and solo. `ScreenTest`'s TearDown already checks `selfDeadlocks`; assert it in each new test too.
- `docs/crosshatch/game-canvas.md` -- `### The forced exit` (:86): new step after the join, the blank in a hidden pass match; step numbering and the deadline sentence follow. `**The bound.**` (:105): the push is after the join and before the abandon, its half refresh counts against the 1,500 ms window and is unmeasured (entry 11; `lib/hal/HalDisplay.h` names 1,720 ms for a half refresh, source unstated), the host double's push costs no clock; append "Measured on an X4 Pro: a forced exit with a stuck VM held `RenderLock` for 502 ms (epic-install-and-launcher entry 14)." Add the resume-before-store order: a slow exit can skip the store flush after the resume write ran, so Continue may resume a board newer than `ch.store` (retrospective rev-5, deferred; trigger: a device log line `forced exit past 1500 ms; skipped the ch.store flush`). `## Hidden pass states` (:116): one line pointing to the forced exit's blank.

## Tasks & Acceptance

**Execution:**
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- `pushForcedExitBlank()` called in `stopVm()` as in Code Map; comments.
- [x] `test/game_script/harness/GameMatchTest.cpp` -- tests (names indicative): `TheForcedExitOnASeatsFramePushesTheBlankAfterTheJoinAndBeforeTheStore` (store game, Playing: one push during the exit, HALF, no texts, made with no SD op yet and before the store rename; log line; `selfDeadlocks` 0; a comment says the double's push moves no clock and the device's half refresh is measured by entry 11); `TheForcedExitInResultAndOnTheBlankPushesTheBlank` (pass-hidden, two activities or two tests: Result, HandOff; the push holds no "apple"); `TheForcedExitWithAStuckVmPushesTheBlankBetweenTheJoinAndTheAbandon` (`fakertos::arm(At::Log)`, a canvas tap parks seat 1's input in `ch.log`; at the push the clock is at least 500 ms past the exit's start and under 1,000 ms, "did not stop within" not yet logged; whole exit ≤ 1,030 ms; `expectCleanPsram = false`; release and `waitNoTasks`); `TheBlanksRefreshCountsAgainstTheSdStepsDeadline` (store game in Result, `onDisplay` advances the clock by `FORCED_EXIT_DEADLINE_MS`: "skipped the ch.store flush" logged, no store.bin); `LeavingAHiddenMatchPushesNoHalfRefresh`; in `PassMatchTest` and `MatchTest`, a forced exit of open pass and of solo (`tracer`) adds no push and no log line; extend `ACallStuckInResultIsStoppedIntoTheErrorView` to sleep from Error and assert no push.
- [x] `docs/crosshatch/game-canvas.md` -- as in Code Map.
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 5.6` at the end only if something is deferred.

**Acceptance Criteria:**
- Given the host suites (CMake/Ninja under the host-test lock), when they run, then every test passes, the new ones included, and the existing forced-exit tests (`ResumeMatchTest`, `MatchTest`) are unchanged.
- Given `pio run -e x4pro`, `pio run -e default`, and `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` under the build lock, then each succeeds; `sim.sh build x4pro` builds.

## Implementation Notes

- The implementation subagent built the Code Map as written: `pushForcedExitBlank()` (guard `forcedExit && lifecycle.hiddenPass()`, `drawBlank`, `displayBuffer(HALF_REFRESH)`, then `LOG_INF "<id>: forced exit: blank hand-off screen pushed (half refresh)"`) is called in `stopVm()` between `vm->stop()` and `flushResume()`; no guard moved. Its hand-back reached this session; the orchestrator could not relay it, and this session re-ran the host suites itself (1,463 of 1,463 before the review patches).
- Tests: `HIDDEN_STORE_GAME` (two moves a turn, `ch.store.set` on each) gives a dirty store in Playing or Result; `HIDDEN_ERROR_GAME` (a script error on the first tap) an error view that keeps its VM. `HiddenPassTest::sleepRecordingPushes` records each exit push from inside `renderer->onDisplay`: clock since the exit began, the fake card's op count, whether the abandon had logged, whether `store.bin` existed, the renderer calls before it.
- The double (R13): no double was added or extended. `screen_stubs/GfxRenderer.h`'s `displayBuffer` stands in for the device's blocking push but moves no clock, so it is more permissive than the device in time; the tests that need the push to cost time move the clock inside `onDisplay`, and the comments name entry 11 as the measurement.
- Mutation checks (review pass 1): pushing after the abandon fails `TheForcedExitWithAStuckVmPushesTheBlankBetweenTheJoinAndTheAbandon`; pushing after `flushResume()` passes every test, since pass writes no `resume.bin` yet (deferred to entry 9, `## 5.6`).
- Assumption for entry 11: the X4 Pro's `displayBuffer(HALF_REFRESH)` returns early enough that the SD steps after it still start within 1,500 ms of `onExit()`'s start; `lib/hal/HalDisplay.h` gives 1,720 ms for a half refresh (unmeasured here; if it holds, a hidden pass match's forced exit skips its store flush, and after entry 9 its resume write). Entry 11 times the push in a hidden pass match with a dirty store and checks the log has no `skipped the ch.store flush`; a skipped resume write on the device reopens AD-20's order with the owner (the entry's unknown).
- Assumption for entry 11: the half refresh leaves no readable ghost of the seat's frame under the blank (AD-12 chose HALF here, where the hand-off between seats uses FULL); R14's device check of the blank before sleep confirms it.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-10-01). The four lenses ran as context-free subagents, launched together in one message in the foreground, each with the worktree read-only, and all four returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment (one ran in the background despite the flag; this session waited for it before triage). Verdicts: medium 0, low 11, maybe-false 1, false 1, plus the intent auditor's descriptive items. No intent_gap or bad_plan. The patches were applied by this session, not by re-engaging the implementer, since its hand-backs route through the orchestrator unreliably in this run; host suites re-run after them.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | edge (1), verification-gap (other 1) | A script error keeps the VM (`fail()` never resets it; only `stopStuckVm` does), so a forced exit from that error view pushes the blank, while the doc said Error pushes nothing | low | patch | Real. The behaviour is the rule (any forced exit with a VM; nothing private is lost); the doc now says so, and `TheForcedExitFromAScriptErrorWithItsVmStillPushesTheBlank` pins it. |
| 2 | edge (2), blind (1), verification-gap, intent (div. 2) | `HALF_REFRESH` may block ~1,720 ms (`HalDisplay.h`), past the 1,500 ms deadline, so every hidden-pass forced exit skips its store flush | maybe-false (medium if true) | defer | Unmeasured; AD-12/AD-20 (amended 2026-10-01) place the push in the window on purpose, and the entry's unknown names it. `## 5.6` with the owner's options (`displayBufferAsync`); `Assumption for entry 11` line; the doc's bound now says it applies even when the VM joins at once. |
| 3 | edge (3), blind (5) | `expectOneBlankPush`'s `ASSERT_EQ` returns only from the helper; callers then read `pushes[0]` out of bounds | low | patch | Real. Every caller wraps it in `ASSERT_NO_FATAL_FAILURE`. |
| 4 | blind (2), verification-gap (other 2), intent (div. 3) | The 502 ms X4 Pro figure predates the push but reads as its bound | low | patch | Real. The doc says it was measured before the blank existed. |
| 5 | blind (3) | The blank is re-pushed where nothing private shows (HandOff, Paused from HandOff, Over), costing a refresh against the window | low | reject | R6 and the verify line ask for every forced exit of a hidden pass match, HandOff named; a state gate adds branches against the spine's rule. |
| 6 | blind (4) | A half refresh may leave a ghost of the seat's frame; the trade-off is unrecorded | low | patch | AD-12 names HALF. The doc and the plan record the choice and its device check (R14, entry 11). |
| 7 | blind (6), intent (div. 4) | Paused, Over, and Starting untested | low | patch (Paused) / reject (Over) / false (Starting) | `TheForcedExitFromThePauseMenuOverASeatsFramePushesTheBlank` added. Over shows seat 0's frame, everyone's: no privacy at stake, same code path. Starting is unreachable: `onEnter()` leaves it synchronously (`handle(Started)` or Error). |
| 8 | blind (7) | No test that the deadline does not gate the push, and no below-boundary case | low | patch | `TheBlankIsPushedEvenPastTheDeadline` and `ABlankPushThatEndsInsideTheDeadlineLeavesTheStoreFlushInTime` added beside the at-deadline case. |
| 9 | blind (8) | `EXPECT_LT(sinceExitMs, deadline)` cannot fail | low | patch | Deleted. |
| 10 | blind (9) | The HALF-mode loop in the Leave test never runs | low | patch | Deleted (the size check covers it). |
| 11 | blind (10) | The blank is recognised by "no text" alone; a textless seat frame would pass; the "apple" loop is dead | low | patch | `expectOneBlankPush` now compares every drawing call since the last `clearScreen` before the push with `drawBlank`'s eye-closed fills; dead loop deleted. |
| 12 | intent (div. 1, 5, 7) | The outcome lives on the panel and the lock on the real `ActivityManager`; sleep and Replace look the same to the tests | false | reject | Descriptive. The real `ActivityManager::exitActivity` holds the render mutex around `onExit()` (as `abandonVm` already relies on); `onExit()` does not tell sleep from Replace; the panel is entry 11's (R14). |
| 13 | intent (div. 6) | The push's place before `flushResume()` is unobservable until pass saves | low | defer | Confirmed by mutation (push after `flushResume()` passes). `## 5.6`, trigger entry 9. |

## Design Notes

**Where the push goes.** In `stopVm()`, between `vm->stop()` and `flushResume()`: the spine places it after the stop wait (joined or not), before the first SD step and before an abandon's wait. Putting it in `stopVm()` rather than `onExit()` is what puts it before `flushResume()`, which must stay right after the wait (the snapshot lives in the VM's memory). `stopVm()` also serves `leave()`, so the push needs `forcedExit`.

**Guards kept** (`git log -L` of `stopVm` and `onExit`: 97dcf52f, df9b09b5, acf780ea, 68ec417b, d45dd71d): `onExit()` notes `millis()` first (the deadline counts from there) and sets `forcedExit` before anything else; `handle(ForcedExit)` before `stopVm()` (Leaving: render draws nothing, routing closed); never `RenderLock` (12cc816). `stopVm()`: `if (!vm) return;` (a user exit or Error already stopped it; no push then, since no seat's frame is on the panel); `flushResume()` after the wait and before `vm.reset()` or `abandonVm()` (the snapshot is in the VM's memory, d45dd71d); a joined VM is reset, otherwise `abandonVm()`.

**Why render-task state is safe here.** `drawBlank` touches `replay`'s refresh policy and the framebuffer, render-task state; `onExit()` runs with `RenderLock` held, so the render task is not inside `render()`, as `abandonVm()` already relies on when it frees the frames.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j4'` -- expected: all pass; the new tests also with `--gtest_repeat=30`.
- `for t in scripts/*_test.py; do python3 $t; done`; `python3 scripts/check_layers.py`; `python3 scripts/check_upstream_touches.py`; `./bin/clang-format-fix` twice -- expected: pass, nothing new.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`, `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- expected: success, no defects. No flash measurement (entry 6 records none); no CI gate changes, so no fresh tree.

**Results (2026-10-01, after the review patches, on the tree this entry commits):**
- Host suites: CMake/Ninja build and `ctest -j4` under `/tmp/crosshatch-hosttest.lock`: 1,467 of 1,467 pass. The new and changed tests (`HiddenPassTest.*`, `MatchTest.AForcedExitOfASoloMatchPushesNothing`, `PassMatchTest.TheForcedExitOfAnOpenPassMatchPushesNothing`; 23 tests): `ctest --repeat until-fail:30`, no failure.
- Every `scripts/*_test.py` (12) passes; `check_layers.py`: 483 include edges pass, no new edge; `check_upstream_touches.py`: PASS (fork files only); `./bin/clang-format-fix` twice: nothing new, nothing outside these paths.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` SUCCESS, `pio run -e default` SUCCESS, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` PASSED, `sim.sh build x4pro` SUCCESS.
- No flash measurement (entry 6 records none); no CI gate changed, so no fresh tree; no screenshots (the verify names none, and the blank on sleep is a host-checked push, confirmed on a device by entry 11).

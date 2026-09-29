---
title: 'The match on the host'
type: 'chore'
ticket: '4'
created: '2026-09-29'
status: built
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '34fae3ff7b29a0ea12050626b02803e6efaea957'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GameVM.cpp` (FreeRTOS, `Arduino.h`) and `GameMatchActivity.cpp` (the activity framework, FreeInkUI, `UITheme`) build on no host suite, so the device-side items in `deferred-work.md` (`## 3.2`, `## 3.7`, `## 3.10`, `## e3r-2`, `## e3r-x`, and epic-script-runtime's own harness items) are checked only in the simulator, and a dropped guard passes every host test.

**Approach:** A new suite in entry 1's harness (`match.cmake`) builds the real `GameVM`, `GameClock`, `GameRandom`, `FrameReplay`, and `GameMatchActivity` (and the real `Activity`, `MatchStore`, `GameSaveStore`, `GameAssets`) unchanged, against doubles kept in `test/game_script/harness/screen_stubs/`: a FreeRTOS on threads with a controllable clock, an `ActivityManager` and `RenderLock`, scripted input, a recording FreeInkUI target, and the real `I18n` generated into the build folder. Each pinned item gets a test that fails under a mutation of the guarded line, recorded in Verification.

## Boundaries & Constraints

**Always:** Change only `test/game_script/harness/` (new files; no edit to `CMakeLists.txt`, `harness_base.sources.cmake`, `installer.*`, `game_assets_and_replay.cmake`, `stubs/`) and `deferred-work.md` (appended under `## 4.4`, plus the `Resolved by entry 4` marks on closed items). Every double says what real behaviour it models. The doubles are named and laid out so entry 5 (the list and Home) and entry 11 (resume) build on them.

**Never:** edit `src/**`, `lib/**`, or an upstream file; take `RenderLock` in a double's way that hides a real self-deadlock; read `fakelog` from a test while a VM task may still write it.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| VM lifecycle | create, start, tap, stop | frames published, input reaches Lua, `stop` joins, no notify to an ended task | a failed task create returns false and clears `taskAlive` |
| Timer | `ch.timer` armed, clock advanced | `pollTimer` posts one Timer event; a re-armed or cancelled timer's event is dropped | n/a |
| Stuck VM | task blocked in a host call | watchdog stops it; abandon frees its PSRAM, or leaks it when it will not stop and the match keeps the slot | error view, `STR_GAMES_ERROR` |
| Load failures | every `LoadResult` | the view names the matching `STR_GAMES_*` text | `BadImage` names `STR_GAMES_BAD_IMAGE` |
| Play again gap | Play again, then a tap, a render, Pause and Resume before the new round's first frame | gesture dropped, nothing drawn, pause menu stays; the first frame draws on a cleared screen | n/a |
| Store | `store.bin` on the card, then a set | the game reads it; flushed at `FLUSH_INTERVAL_MS`, at round end, on Leave, and in `onExit` | n/a |

</frozen-after-approval>

## Code Map

- `src/games/GameVM.cpp`, `GameClock.cpp`, `GameRandom.cpp` -- built unchanged. Not `SIMULATOR`, so the device branches run (`esp_timer`, `pxTaskGetStackStart`, and `abandon`'s suspend-and-delete path included).
- `src/activities/games/GameMatchActivity.cpp` -- built unchanged, with `GameAssets`, `GameSaveStore`, `MatchStore`, `FrameReplay`, `GameViewport`, `GameIconDraw`, the SDK's `FreeInkUI.cpp`, and `lib/GameCore`'s `Session`, `Roster`, `MatchLifecycle`. `Activity` is the exception: a copy of its surface (Design Notes).
- `test/game_script/harness/match.cmake` -- the suite (`GameMatchHarnessTest`): its own `game_match_src` library from `harness_game_sources`, less the shared exclusions but for the four files above, and I18n generated into the build folder by `scripts/gen_i18n.py`.
- `test/game_script/harness/screen_stubs/` -- the doubles (Design Notes). `MatchSupport.h` -- the rig; `GameVmTest.cpp`, `GameMatchTest.cpp` -- the 58 tests.
- Not changed: `CMakeLists.txt`, `harness_base.sources.cmake`, `installer.*`, `game_assets_and_replay.cmake`, `stubs/`, every `src/**` and `lib/**` file.

## Tasks & Acceptance

**Execution:**
- [x] `test/game_script/harness/screen_stubs/**` -- the doubles
- [x] `test/game_script/harness/match.cmake` -- the suite and its libraries
- [x] `test/game_script/harness/{MatchSupport.h,GameVmTest.cpp,GameMatchTest.cpp}` -- the rig and the pinned items
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- closed items marked, the rest under `## 4.4`

**Acceptance Criteria:**
- Given the host suites, when they run, then every test passes and each pinned item's recorded mutation fails the test named beside it (Verification).
- Given `pio run -e x4pro` and `-e default` and `check_layers.py`, then each passes.

## Implementation Notes

Implemented directly in the build session, not through a subagent: the doubles' design (which real header each one shadows, and why a whole source set of its own instead of the shared `game_harness_src`) came out of the investigation, and a fresh implementer given only this plan would have redone it.

**Departure from the frozen Approach:** it says the suite builds "the real `Activity`". It builds a copy of `Activity`'s surface (`screen_stubs/activities/Activity.h`, its members in `ScreenDoubles.cpp`): the real `src/activities/Activity.h` includes the real `src/activities/ActivityManager.h` by a quoted path from its own folder, which no include order can shadow, and that header needs FreeRTOS semaphores and the render task. Every other named source is built unchanged. `## 4.4` records the copy.

The first full run of the suite under `--gtest_repeat` hung: `fakertos::pass()` waited for the held-task count to reach zero, and the next hold (round 2's setup) could raise it again before the waiting thread looked. `pass()` now waits for the tasks held on the older generation to leave.

The review's patches (Review Triage Log) added five tests (`ALongPressOnTheCanvas...`, `ASwipeOnTheCanvas...`, `ASystemEdgeSwipe...`, and `WatchdogStopTest`'s two), a log hook for the second pair, and the gate's release in every `TearDown`.

## Plan Change Log

## Review Triage Log

Review pass 1. The four lenses (blind hunter, edge-case hunter, verification gap, intent alignment) ran as context-free subagents over the staged diff (the plan excluded from it, given to the edge-case hunter as the claims file), and all four returned. None of them built or ran the suite. Counts over the 22 rows below (findings of several lenses grouped by root cause): 0 high, 7 medium, 13 low, 2 false, 0 maybe-false. The intent-alignment audit is descriptive; row 22 is this build's reading of its four divergences.

| # | Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- | --- |
| 1 | blind, edge | A test that fails between `arm()` and `release()` leaves the task held, and its `TearDown` then blocks or frees memory the task still uses | medium | patch. `ScreenTest`, `MatchTest`, and `GameVmTest` `TearDown` release the gate first, and `GameVmTest` leaks a VM that still will not stop instead of freeing it. `reset()` also wakes waiters. |
| 2 | blind | `ulTaskNotifyTake` dereferences a null task after an `if (task)` | low | patch: returns 0 when there is no task (only a task waits). Edge-case hunter's twin finding is the same row. |
| 3 | blind, edge | `Task::counted` is read at thread end without the lock while `vTaskDelete(handle)` writes it under it; `vTaskDelete` of an ended task could double-decrement `liveTasks` | low | patch: the thread end decrements under the lock and clears `counted`, so the count moves once. (`stackBase` is written and read by the task itself: false.) |
| 4 | blind | The registry never frees a `Task`, which a leak checker would report | low | patch: tasks are kept in `State::tasks`, which lives for the process. |
| 5 | blind, edge | Negative checks wait in real time (a 50 ms sleep in `PollTimerPostsTheTimerEventOnceItIsDue`, 150 ms in the gap tap test, a 20 ms sleep after the abandon) and pass on a loaded runner when the bug is present | medium | patch: each is now an event queued behind the one under test (a tap whose frame or log line must come after) or the game's own per-tap `ch.log`; the sleeps are gone. (The abandon test's wall-clock lower bound is not one: `abandon` sleeps at least 5 ms a poll for 100 polls: false.) |
| 6 | blind, edge | `state()` takes the last log line with " -> " and " on ", which a game's `ch.log` could spoof | low | patch: only lines that start with the match's own `INF GAME: <id>: ` count. |
| 7 | blind, edge | `ui()` dereferences a null target after a non-fatal `EXPECT_NE` | low | patch: aborts with a message. |
| 8 | blind, edge | The drift ledger names four copies; the diff adds more (`ActivityManager`, `MappedInputManager`, `RenderLock`, `UITheme`, the renderer's two calls, `Activity.cpp`'s bodies) | medium | patch: the third `## 4.4` entry names each. |
| 9 | blind | The input double has no button remapping and `isPressed` is `wasPressed` | low | patch: recorded in the same entry (AGENTS.md says users remap the front buttons; the real mapper is upstream's and not exercised). |
| 10 | blind, edge | `RenderLock(Try)` by its holder counts as a self-deadlock | low | patch: only a blocking second lock is counted. |
| 11 | blind | The suite needs the SDK submodule and the documented command does not init it | false | `test/game_script/CMakeLists.txt` already builds `FreeInkUI.cpp` from it for `GameScriptTest`, and AGENTS.md and CI's recursive checkout supply it; the same failure mode as before. |
| 12 | blind | The generated I18n could drift from the real build's, and `lib/I18n` could leak onto the include path | false | The suite runs the real generator and copies the real `I18n.h` and `I18n.cpp`; `lib/I18n` is not on any include path of the suite, and the firmware build compiles the real ones. |
| 13 | blind | Resolved deferred-work items keep contradictory old evidence, and two carry a nested "It read:" | low | patch for the nesting (both now read once). The evidence lines stay: they are the history of the item, as entry 1's marks left them. |
| 14 | blind, edge | A comment lists `slow-restart` as a fixture the suite plays (it does not); `installFixture` throws on a missing folder; missing direct includes | low | patch: `match.cmake`'s comment and `installFixture` (a test failure, not an exception) and the includes. |
| 15 | blind | `uxTaskGetStackHighWaterMark` is a constant, so a headroom guard cannot trigger on the host | low | patch (wording): `GameVM` only logs it (the headroom check is `CallGuard`'s, on addresses); `FakeRtos.h` and `## 4.4` now say the value is a constant. |
| 16 | gap | `readGesture`'s long press and swipe paths (and GameTouch's edge rule) are driven by no test, and a stub comment says the suite pins them | medium | patch: `ALongPressOnTheCanvasReachesTheGameAsALongPressAtTheCanvasPoint`, `ASwipeOnTheCanvasReachesTheGameAtItsStartWithItsDirection`, `ASystemEdgeSwipeNeverReachesTheGame`, with mutations L1 to L4. |
| 17 | gap | `stopStuckVm`'s clean-stop branch and its "ended on its own error" message are unpinned (the reviewer filed it as defer) | medium | patch, since it is cheap: `WatchdogStopTest` releases the held call at the "stopping the VM" log line (a hook on the log), with mutations W1 and W2. |
| 18 | gap | `TheKeysMoveAndChooseInAMenuAsTouchDoes` drives keys, never Previous or the wrap | low | patch: renamed, and it wraps both ways (K1, K2; with two options Previous and Next choose alike, so a swap of the two is not detectable). |
| 19 | edge | `GameArenaDouble`'s OOM line differs from the real file's when the reserve shrinks | low | patch: the comment says so. |
| 20 | edge | The plan says the suite builds "the real Activity"; it builds a copy | medium | patch: `GameMatchTest`'s banner and the plan's Design Notes say so, the third `## 4.4` entry names it, and the report to the orchestrator flags the departure from the frozen Approach (it cannot be the real one: `Activity.h` includes `ActivityManager.h` by a quoted path from its own folder). |
| 21 | edge | The `## 3.10` entry points at "the first entry under `## 4.4`" for `notLoaded`; it is the second | low | patch. |
| 22 | intent | Four divergences between the intent's surface (real sources) and the diff's (the doubles' boundary): the `Activity` copy, the other copies, threads for cores, text for layout | medium | Each is rows 8, 20, 15, and the fifth `## 4.4` entry (views pinned by text, not layout); nothing further to fix. |

## Design Notes

**No existing function was moved or rewritten** (test files and doubles only), so there is no `git log -L` guard to carry. The doubles copy a few real definitions; each header says which, and `## 4.4` records that a change to the real one is not seen by the host.

**Whether `GameVM` and the match link on the host without upstream edits: yes.** Each double, and why it exists:

| Double (`screen_stubs/`) | Stands in for | Why |
| --- | --- | --- |
| `FakeRtos.h`, `freertos/FreeRTOS.h`, `freertos/task.h`, `Arduino.h`, `esp_timer.h`, `esp_random.h` | FreeRTOS tasks and notifies, `millis`, `esp_timer`, `esp_random` | `GameVM` creates a task; CI's unit-tests job has no `.pio/`, so the simulator's shim is not reachable. A task is a detached thread; the clock is the test's (`fakertos::advance`); a gate holds the task at a clock read or a log line (`arm`, `pass`, `release`), so a stuck call, a step in flight, and the Play-again gap are staged, not raced. `vTaskDelete` of another task parks it for good. |
| `Logging.h` | `LOG_*` (the shared capture) | One mutex over the capture: the VM task logs while the test reads. A log line is also a gate point (inside `ch.log`, a locked binding), and a `hook` lets a test act when the code says something. |
| `GameArenaDouble.cpp`, `ArenaSize.h` | `GameArena.cpp` | The same code with the reserve a test can shrink, the only way to make the Session or the codec scratch not fit (`NoSession`, `OutOfMemory`). |
| `GfxRenderer.h` | the shared renderer double, plus `displayBuffer` and `tapToLogical` | The match calls both; the shared double has neither. Derived from it, the shared file untouched. |
| `activities/Activity.h`, `ActivityManager.h`, `ScreenDoubles.cpp` | `Activity`, `ActivityManager`, `RenderLock`, global `gpio` | See the departure above. The manager double records what a screen asks (updates, `goToGames`, push, pop) and models the lock: `exitHolding` calls `onExit` with `RenderLock` held, and a second blocking lock by its holder is counted (`selfDeadlocks`), not waited on (12cc816). |
| `MappedInputManager.h`, `HalGPIO.h` | the input a screen reads | Scripted per frame (`tap`, `click`, `press`, `longPress`, `swipe`; `clear()` ends the frame). |
| `components/UiAppHost.h`, `UiAppHelpers.h`, `UITheme.h` | `UiAppHost`, `touchSnapshotFrom`, the popup metrics and `drawButtonHints` | The real ones build `GfxRendererTarget` and the settings-driven theme. The real `FreeInkApp` and dialogs run on a recording target: every line a dialog draws, wrapped, with its rectangle. |
| `RenderLockProbe.h` | counters of the `RenderLock` double | |
| I18n (generated, not a double) | `lib/I18n` | `gen_i18n.py` output and the real `I18n.cpp`, in the build folder, so a test asserts the real English of `STR_GAMES_*`. |

**Why the match's own source library.** `screen_stubs/GfxRenderer.h` gives `GfxRenderer` a different layout from the shared double's, so every source that takes a `GfxRenderer` (`FrameReplay`, `GameViewport`, `GameIconDraw`, the match) is built here, and `game_harness_src` is not linked.

**Reading the match's state.** The state is private, so a test reads it from what the match does: the transition it logs (`"<id>: A -> B on E"`), the lines its views draw (`RecordingTarget::newest()`), the displays and clears on the renderer, the asks on the manager, and the card.

**Pinned items, and what stays open.**
- `## 3.2` `BadImage` mapping: `ADamagedImageSaysSoAndNamesTheImageReason`, one test per other `LoadResult`, and f27dcefd's read error at the match (M1, M1b, M1c).
- `## 3.7` R3 gate, R3 residual: `PlayAgainGapTest` (M2, M3, M5, M6, M7).
- `## 3.10` call sites: `GameVmTest`'s three failure tests and `GameMatchTest`'s three (V15, V16, V18, M13, M14). `notLoaded` stays open (M13b).
- `## e3r-2` gesture drop: M4. `## e3r-x` skip: M5, M6, M7; the store order stays open (M7b).
- epic-script-runtime's harness items: the VM task lifecycle (V1 to V4), `pollTimer` and the stale drop (V5, V6), `abandonVm` (V8 to V12, M18, M25), the forced redraw, the loop's re-request guard, and the identical-frame skip (V14, M19 to M21), the `store.bin` restore and `flushIfDue` (M15, M16, M26), the match's transitions (M8 to M12, M23, M24), `GameVM::run`'s composition and the watchdog (V7, V7b, M17, M28, W1, W2), the Canvas (V17, M22, M22b), and, from the review, `readGesture` (L1 to L4) and the menu keys (K1, K2).
- **Not reached, for the sweep:** the tracer item's `GamesListActivity` dir/id filter, and the hostcaps item's `Manifest::check` filter (both entry 5, the list screen); `## 3.6`'s cover-grid tab order (entry 5, Home); the sandbox item's device run of the `loop` fixture (a device; the host stages two of its abandon cases, and `## 4.4` records what it does not); `## e3r-2`'s first item (the gesture gate opens when the frame is published, not displayed: a design gap, not a test gap); `## e3r-x`'s second item (the inert menu: a UX decision); `## e3r-1`'s timing items and `## 3.2`'s replay timing (a device).

**For entry 5 and entry 11, the doubles to build on:** `screen_stubs/ActivityManager.h` (the manager double: `asks`, `onGoToGames`, `exitHolding`, `destroyHolding`; entry 5 adds its own asks in a file of its own, since this one is not to be edited), `MappedInputManager.h` (scripted input), `ScreenDoubles.cpp` (`RenderLock`, `gpio`, `activityManager`), `components/UiAppHost.h` (`RecordingTarget::newest()`), `MatchSupport.h` (`ScreenTest`, `waitFor`), and `match.cmake`'s `game_match_src` (a suite that needs more of `src/games` or `src/activities/games` copies its shape).

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`, under the build lock -- 921 of 921 passed (863 before this entry, 58 added), total test time 7.77 s; the configure ran `gen_i18n.py` and built the suite from a tree with no `lib/I18n/I18nKeys.h` of its own on the include path.
- `pio run -e x4pro` -- SUCCESS in 5 min 24 s, on the tree before the review patches; nothing under `src/`, `lib/`, or `platformio.ini` differs between that tree and the commit (`git diff --stat -- src lib platformio.ini` is empty), and no test file is part of a firmware build.
- `pio run -e default` -- SUCCESS in 6 min 36 s, same tree and same reasoning.
- The suite from a fresh tree (an archive tree: `git archive` of the committed tree plus each submodule's archive, nested ones included; no git history, no `.pio/`, no generated `lib/I18n` files): `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release`, `cmake --build build/test --target GameMatchHarnessTest`, `ctest -R 'MatchTest|PlayAgainGapTest|WatchdogStopTest|GameVmTest'` -- 58 of 58 passed. (The gate is CI's unit-tests job, unchanged; this is the check that the suite's configure-time I18n generation and the SDK sources work from a clean tree.)
- `python3 scripts/check_layers.py` -- passed: 406 include edges in 99 game files follow the layer table.
- `python3 scripts/check_upstream_touches.py` -- PASS (every changed path is new, or in `deferred-work.md`).
- `./bin/clang-format-fix`, twice -- run twice as the last step: no file changed on the second run, and `git status` shows nothing new beyond this commit's own files (the first run of the session only reflowed comments in this entry's own new files; no upstream or fork file outside them was touched).

**Mutations.** Each row applies one mutation to the guarded line (the source is restored after each), rebuilds the suite, and runs the tests that pin it, with a scratch runner (not committed). "Caught" means at least one of them failed (a crash or timeout counts). The table is from the last run, on the final tests. In earlier passes V14 first reported "survives" (the runner had not touched the file's mtime; the stale build is unexplained, and the rerun with the mtime touched caught it), M22 survived while the double's 10 px advance equalled the stand-in's (the fonts now advance 9, 11, 17), and V7b needed a second idle poll in its test; each was fixed and rerun.

| Id | Pinned item | Mutation | Result |
| --- | --- | --- | --- |
| V1 | VM task lifecycle: join of a never-started VM | delete `if (!task) return true;` in `join` | caught by JoinIsTrueForAVmThatNeverStartedAndFalseWhileTheTaskRuns |
| V2 | VM task lifecycle: postInput wakes the task | delete `notifyTask();` in `postInput` | caught by AnInputEventReachesLuaAndItsFrameIsTheNextOne |
| V3 | VM task lifecycle: no notify to an ended task | drop the `if (taskAlive)` guard in `notifyTask` | caught by ATaskThatCannotBeCreatedLeavesNoTaskToNotifyOrJoin, NothingIsNotifiedToATaskThatHasEnded |
| V4 | VM task lifecycle: a failed create clears taskAlive | delete `taskAlive = false;` in `start()`'s failure path | caught by ATaskThatCannotBeCreatedLeavesNoTaskToNotifyOrJoin |
| V5 | pollTimer posts a due timer event | empty the `postInput(event)` in `pollTimer` | caught by PollTimerPostsTheTimerEventOnceItIsDue |
| V6 | the stale-timer drop | delete `if (!timer.accepts(event)) return Outcome::Ok;` (`lib/GameScript/SoloRounds.cpp`) | caught by ATimerEventPolledBeforeTheGameRearmsItIsDropped |
| V7 | watchdog: a call is timed from the first poll that saw it | delete `watchedSinceMs = nowMs;` in `runningForMs` | caught by RunningForTimesEachCallFromTheFirstPollThatSawIt |
| V7b | watchdog: idle is 0 | delete `if (!busy()) return 0;` in `runningForMs` | caught by RunningForTimesEachCallFromTheFirstPollThatSawIt |
| V8 | abandonVm: a deletable stuck VM is deleted | `if (false && stuck->deleteIfStuckInLua())` in `abandon` | caught by AbandonFreesAVmStuckOutsideALockedBinding, ACallRunningPastThreeSecondsIsStoppedAndAbandonedIntoTheErrorView |
| V9 | abandon frees the frame storage | delete `stuck->frameStorage.reset();` | caught by AbandonFreesAVmStuckOutsideALockedBinding |
| V10 | abandon frees the arena | delete `stuck->arena.release();` | caught by AbandonFreesAVmStuckOutsideALockedBinding |
| V10b | abandon frees the sources | delete `stuck->assets.release();` | caught by AbandonFreesAVmStuckOutsideALockedBinding |
| V11 | abandon never deletes inside a locked binding | drop `!game.inLockedBinding()` from `deleteIfStuckInLua` | caught by AbandonLeavesAVmStuckInsideALockedBindingAndSaysSo, AVmThatWillNotStopKeepsTheStoreSlotForTheTaskThatMayStillPostToIt |
| V12 | abandon of an ended VM deletes it | delete `delete stuck;` in the finished branch of `abandon` | caught by AbandonOfAVmThatEndedAfterAllDeletesItAndFreesEverything |
| V13 | drawFront: nothing before the first frame | delete `if (frameBuffers.frameGen() == 0) return false;` in `drawFront` | caught by TheTaskRunsSetupAndTheFirstDrawThenStopsWhenCancelled |
| V14 | drawFront: identical frame skipped | `drawn = true;` after `replay.draw` in `drawFront` | caught by DrawFrontSkipsAFrameIdenticalToTheOneOnScreenUntilForced |
| V15 | ## 3.10: failure() passes (failed, sessionOutOfMemory) in order | swap the first two arguments of `vmFailure(...)` in `failure()` (`GameVM.h`) | caught by AScriptErrorIsAScriptFailureThatStartedTheGame, AScriptErrorSaysTheGameStoppedWithLuasMessage |
| V16 | failure() reads nothing before the task has ended | delete `if (!failed()) return Failure::None;` in `failure()` (`GameVM.h`) | caught by TheTaskRunsSetupAndTheFirstDrawThenStopsWhenCancelled |
| V17 | Canvas: width and height | swap width and height in `GameVM::create`'s `Canvas` | caught by TheGameSeesTheViewportAsItsScreenAndMeasuresTextWithTheFonts, TheGameSeesTheScreenAndTheFontsTheMatchGaveIt |
| V18 | NoSession is recorded by the task | delete `sessionOutOfMemory = true;` in `run()` | caught by ASessionThatDoesNotFitIsNoSessionAndCouldNotStart |
| M1 | ## 3.2: BadImage -> STR_GAMES_BAD_IMAGE | `BadImage` returns `STR_GAMES_CANNOT_READ` | caught by ADamagedImageSaysSoAndNamesTheImageReason |
| M1b | NoSources -> STR_GAMES_NO_SOURCES | `NoSources` returns `STR_GAMES_FOLDER_MISSING` | caught by AFolderWithNoLuaSaysSo |
| M1c | CannotRead -> STR_GAMES_CANNOT_READ (f27dcefd) | `CannotRead` returns `STR_GAMES_BAD_IMAGE` | caught by AFolderTheCardWillNotOpenSaysItCannotBeRead, AnImageHeaderTheCardCannotReadIsNotADamagedImage |
| M2 | ## 3.7: the + 1 of the R3 render gate | drop the `+ 1` of `roundsStartedAwaited.store(...)` | caught by ATapInTheGapIsDroppedAndNeverReachesTheNewRound, TheLoopAsksForNoRenderWhileTheLastRoundsLateFrameIsTheNewest |
| M3 | ## 3.7: loopPlaying's early return in the gap | delete `if (awaitingRound) return;` in `loopPlaying` | caught by TheLoopAsksForNoRenderWhileTheLastRoundsLateFrameIsTheNewest |
| M4 | ## e3r-2: the gesture drop in loopPlaying | drop `!awaitingRound &&` from the gesture test | caught by ATapInTheGapIsDroppedAndNeverReachesTheNewRound |
| M5 | ## e3r-x: renderCanvas draws nothing in the gap | `if (false)` for the Play-again-gap skip in `renderCanvas` | caught by ARenderInTheGapDrawsNothingAndTheNewRoundsFirstFrameIsDrawnInFullOnAClearedScreen, PauseThenResumeInTheGapKeepsThePauseMenuUntilTheNewRoundsFirstFrame |
| M6 | ## e3r-x: the gap skip sets viewOnScreen | delete `viewOnScreen = true;` in the gap skip | caught by AGapRenderMarksTheScreenAsNotHoldingTheCanvasEvenWhenNoMenuWasDrawn |
| M7 | ## e3r-x: the PlayAgain store of roundsStartedAwaited (present) | delete the PlayAgain store of `roundsStartedAwaited` | caught by ATapInTheGapIsDroppedAndNeverReachesTheNewRound, TheLoopAsksForNoRenderWhileTheLastRoundsLateFrameIsTheNewest |
| M7b | ## e3r-x: the store order (before shown) | store `roundsStartedAwaited` after `shown.store(to)` | survives |
| M8 | the flush on entering Over | delete `flushStore();` in `case MatchState::Over` | caught by TheEndOfARoundOpensTheOverMenuAndWritesTheStoreAtOnce |
| M9 | Leave stops the VM before goToGames | delete `stopVm();` in `leave()` | caught by LeavingStopsTheVmAndWritesTheStoreBeforeItGoesToGames |
| M10 | Leave flushes before goToGames | swap `flushStore();` and `goToGames();` in `leave()` | caught by LeavingStopsTheVmAndWritesTheStoreBeforeItGoesToGames |
| M11 | 12cc816: onExit never takes RenderLock | add `RenderLock relock(*this);` to `onExit` | caught by AForcedExitStopsTheVmAndWritesTheStoreWithoutTakingTheRenderLock |
| M12 | handleHomeGesture lets Home go once Leaving | `handleHomeGesture` returns `true` | caught by HomePausesARoundInPlayAndIsIgnoredWhileAMenuIsUpUntilTheMatchLetsGo |
| M13 | ## 3.10: vmFailureText fills outOfMemory | `texts.outOfMemory = tr(STR_GAMES_NOT_LOADED)` | caught by ASessionThatDoesNotFitSaysTheGameCouldNotStartInTheHostsWords, ScratchThatDoesNotFitSaysTheSameInTheHostsWords |
| M13b | ## 3.10: vmFailureText fills notLoaded (unreachable on the host) | `texts.notLoaded = tr(STR_GAMES_OUT_OF_MEMORY)` | survives |
| M14 | ## 3.10: vmHealthy's headline | invert the headline choice in `vmHealthy` | caught by AScriptErrorSaysTheGameStoppedWithLuasMessage, ASessionThatDoesNotFitSaysTheGameCouldNotStartInTheHostsWords |
| M15 | flushIfDue in loopPlaying | delete `store.flushIfDue(millis());` in `loopPlaying` | caught by ADirtyStoreIsWrittenOnceTheFlushIntervalPassesWhilePlaying |
| M16 | flushIfDue in loopView | delete `store.flushIfDue(millis());` in `loopView` | caught by ADirtyStoreIsWrittenOnceTheFlushIntervalPassesWhilePaused |
| M17 | the watchdog's limit | `> WATCHDOG_MS` becomes `>= WATCHDOG_MS` | caught by ACallRunningPastThreeSecondsIsStoppedAndAbandonedIntoTheErrorView |
| M18 | abandonVm keeps the store slot | delete `slotLeaked = true;` in `abandonVm` | caught by AVmThatWillNotStopKeepsTheStoreSlotForTheTaskThatMayStillPostToIt |
| M19 | a render with no new frame redraws in full | delete `replay.forceFull();` for a render with no new frame | caught by ARenderWithNoNewFrameRedrawsInFull |
| M20 | the loop does not re-request a frame a render took | drop `frame != renderedFrame` from the loop's render request | caught by TheLoopDoesNotAskAgainForAFrameARenderAlreadyTook |
| M21 | an identical frame is not refreshed | drop the `return` after a `drawFront` that drew nothing | caught by AFrameIdenticalToTheOneOnScreenIsNotRefreshed |
| M22 | loadFonts before the VM is created | delete `replay.loadFonts(renderer);` in `onEnter` | caught by TheGameSeesTheScreenAndTheFontsTheMatchGaveIt |
| M22b | the viewport of onEnter | delete `viewport = GameViewport::forRenderer(renderer);` in `onEnter` | caught by TheGameSeesTheScreenAndTheFontsTheMatchGaveIt |
| M23 | flushStore before the store exists | delete `if (!store.ready()) return;` in `flushStore` | caught by crash/timeout |
| M24 | a forced exit does not leave() | `leave();` for every event but ForcedExit in `handle` | caught by AForcedExitStopsTheVmAndWritesTheStoreWithoutTakingTheRenderLock |
| M25 | the destructor leaks the slot only after a leaked VM | delete `if (!slotLeaked) return;` in the destructor | caught by EnteringStartsTheGameAndItsFirstFrameIsDrawnInFull |
| M26 | GameAssets::load restores store.bin | delete `saves.restoreInto(store);` in `GameAssets::load` | caught by AStoreOnTheCardIsRestoredIntoTheGamesStoreWhenTheMatchStarts |
| M28 | the stuck view's headline | headline `STR_GAMES_START_FAILED` in `stopStuckVm` | caught by ACallRunningPastThreeSecondsIsStoppedAndAbandonedIntoTheErrorView |
| L1 | readGesture: the long press is read | `touchSnapshotFrom(mappedInput, false)` in `readGesture` | caught by ALongPressOnTheCanvasReachesTheGameAsALongPressAtTheCanvasPoint |
| L2 | readGesture: a long press is a LongPress | a long press becomes `Kind::Tap` in `readGesture` | caught by ALongPressOnTheCanvasReachesTheGameAsALongPressAtTheCanvasPoint |
| L3 | readGesture: the swipe is read | `if (false && gpio.wasSwipe(...))` in `readGesture` | caught by ASwipeOnTheCanvasReachesTheGameAtItsStartWithItsDirection |
| L4 | readGesture: the swipe's end point | the swipe's end point maps `startX, startY` | caught by ASwipeOnTheCanvasReachesTheGameAtItsStartWithItsDirection |
| W1 | stopStuckVm: a Lua error that ended the call is shown | invert `if (vm->failed())` in `stopStuckVm` | caught by ACallThatEndsInALuaErrorMeanwhileShowsThatErrorNotTheWatchdogs |
| W2 | stopStuckVm: a VM that stops is not abandoned | `if (false && vm->stop(...))` in `stopStuckVm` | caught by ACallThatReturnsWhenTheStopIsAskedIsStoppedWithoutAnAbandon |
| K1 | loopView: Previous wraps | `(current - 1) % menu.count` for Previous in `loopView` | caught by TheKeysMoveAroundAMenuAndConfirmChoosesTheFocusedOption |
| K2 | loopView: Next moves | `selected.store(current)` for Next in `loopView` | caught by TheKeysMoveAroundAMenuAndConfirmChoosesTheFocusedOption |

Rows that survive, by design (`## 4.4`): M7b, M13b (M7b, the order of two atomic stores on one thread; M13b, a text no host game reaches).

**Repeat and load.** `GameMatchHarnessTest --gtest_repeat=40` alone (all 58 tests each time), and 6 concurrent processes of 10 repeats on 4 cores, all pass.

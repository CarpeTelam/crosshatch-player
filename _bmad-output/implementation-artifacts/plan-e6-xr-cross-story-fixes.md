---
title: 'e6-xr: epic-play-nearby cross-story review fixes'
type: 'bugfix'
ticket: ''
created: '2026-10-14'
status: 'built'
baseline_revision: 'c58f28aa9fc3c655fc9cc6b6fa32b961d1085cfa'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-play-nearby/cross-story-review.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
warnings: ['multiple-goals', 'oversized']
deferred:
  - summary: 'Cross-story review row 14 (A3, low): with the light panel open over a nearby match or the lobby, ActivityManager::preventAutoSleep() asks only the current activity (the panel), so the device can auto-sleep; that forced exit ends the match for the peer as "left".'
    evidence: 'Deferred by the cross-story review: the fix is in upstream ActivityManager (row 5''s file, a new change there) or a fork-side flag read by main.cpp, for a case that needs the panel left open past the sleep timeout mid-match; the peer sees a correct "left". For the owner to confirm at entry 14.'
    location: 'src/activities/ActivityManager.cpp (preventAutoSleep), src/activities/games/GameMatchActivity.h, src/activities/games/GameLobbyActivity.h'
    severity: 'low'
  - summary: 'Cross-story review row 15 (A6, low): when the title screen could not read a game''s settings, New game in Nearby is refused, and so is a player who only means to Join; a guest runs no setup and the match exempts the follower.'
    evidence: 'Deferred by the cross-story review: it needs an unreadable settings file, and the fix moves the check into the lobby''s Host row, which changes 6.10''s lobby flow. For the owner to confirm at entry 14.'
    location: 'src/activities/games/GameModeActivity.cpp (New game in Nearby), src/activities/games/GameLobbyActivity.cpp (Host row)'
    severity: 'low'
  - summary: >-
      R6 ("The lobby and the match set `preventAutoSleep`") and R8 ("The lobby sets `preventAutoSleep` while open") in the epic file, and spine AD-21's line, no longer match the code after cross-story review row 3 (the match sleeps in PeerGone and Error, the lobby in Choose and Ended).
    evidence: |-
      e6-xr review (blind hunter; intent-alignment divergence 1). Row 3 prescribes code and tests only, and the epic-file currency paragraph covers lines a Notes Decision superseded, so the edit is outside this build's intent. For the orchestrator to amend the two Requirements (and the spine line) with row 3's exception.
    location: >-
      _bmad-output/initiative-crosshatch-player-v1/epic-play-nearby/epic-play-nearby.md R6, R8; ARCHITECTURE-SPINE.md AD-21 (line 315)
    severity: low
  - summary: >-
      No CI build compiles src/main.cpp with -DCROSSHATCH_SD_LOG=1, so the [MEM] battery block (and the rest of row 14's define-on branch) is pinned only by source text.
    evidence: |-
      e6-xr review (verification gap): a search of .github/, platformio.ini, scripts/ and the test CMake files finds no build setting the define. It is cross-story review row 17 (owner, a CI job in its own follow-up). This build ran the define-on x4pro and default builds and pio check locally.
    location: >-
      .github/workflows/crosshatch-ci.yml; src/main.cpp [MEM] block
    severity: medium
---

<intent-contract>

## Intent

**Problem:** The cross-story review of epic-play-nearby (`cross-story-review.md`) accepted nine fixes: a tap queued before an adopt can be played against a board the player never saw (row 1); an `ABORT` behind a queued round-ending `STATE` skips Over (row 2); the player-left, error, Choose and Ended screens never auto-sleep (row 3); a leaked VM's session keeps the radio up for the boot (row 4); the epic file, `game-canvas.md`, and ledger row 14 say things the code no longer does (rows 5, 6, "Epic file currency"); no log line carries a battery reading (row 8); no test plays a first-party game in a nearby match (row 9); and a lobby tap during the push after a list change can join the wrong host (row 16).

**Approach:** Follow each accepted row's "Reason / action" to the letter, in fork files, plus one `#if CROSSHATCH_SD_LOG` change in upstream `src/main.cpp`'s `[MEM]` branch (ledger row 14, amended). Each code fix gets host tests that fail without it.

## Boundaries & Constraints

**Always:** AGENTS.md rules (nothrow allocation, locals under 256 B, `LOG_*`, no `RenderLock` in a destructor or `onExit`). Every existing guard in a function this touches stays (Design Notes). Fork-only code; the only upstream file is `src/main.cpp`, and only inside its `#if CROSSHATCH_SD_LOG` `[MEM]` branch. The epic's remaining flash share is 1,984 B and static RAM 80 B; this build stays within it, measured like for like. Define-off images of `x4pro`, `default`, and `sticky` are unchanged by the `main.cpp` change.

**Never:** Edit anything under `games/`, `.agents/`, `freeink-sdk`, `deferred-work.md`, or `ci.yml`; change `noteSeatDrawn`'s seat rule for solo and pass; change Paused's end handling; free anything a leaked VM may touch; allocate for the battery line; rows marked packet, owner, defer, or reject (7, 10 to 15, 17, 18); the spine.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|---|---|---|---|
| R1 turn-passing adopt | guest taps (tag F) while host's `STATE` is queued; VM adopts, then pops the tap | tap dropped ("Dropped a touch made under frame ..."); no `input`, no `apply`, nothing ships | none |
| R1 RoundStarted adopt (A8) | tap queued behind host's over and next-round `STATE`s | tap dropped; no "tap for seat 2" | none |
| R1 tap after the adopt | tap made once the adopted frame is on the panel | played as today | none |
| R2 end behind queued over | over `STATE` queued, then the peer's `ABORT`, loop resumes in Playing | Over entered (over event once), then PeerGone | none |
| R2 empty queue | end latched, queue empty, VM not adopting | PeerGone on that pass, as today | none |
| R2 bound | VM holds a taken `STATE` and never counts it | end honoured `PEER_END_HOLD_MS` (1,000 ms) after the hold began, not before; one log line | VM keeps running; PeerGone as today |
| R3 sleep | match PeerGone or Error; lobby Choose or Ended | `preventAutoSleep()` false; true in every other nearby state | none |
| R4 leaked VM | Leave or forced exit with a VM that will not stop | goodbye as today; the destructor stops the `GameLink` task and ends the link once, frees nothing | task that will not stop: link left up, logged as the destructor's |
| R16 tap during push | host list changes; tap on row 0 during the push of the new rows | dropped; joins nothing | redraw requested |

</intent-contract>

## Code Map

- `src/games/GameVM.cpp:264-286` `nearbyStep` -- row 1 floor move and row 2 `adopting` flag. `:405-424` `noteSeatDrawn` / `madeUnderAnotherSeat` (unchanged). `:186` call site, `:214` `queue.pop`. `GameVM.h:253`, `:322-326` (`drawnSeat`, `seatFrame`), `pollNearby` (`:288-291` of .cpp).
- `lib/GameScript/MatchRounds.cpp:66-84` `adopted` (Adopted draws via `drawShown`, RoundStarted via `beginAdopted`; a NO_SEAT draw draws nothing). Read only.
- `src/games/NearbySession.{h,cpp}` -- `adoptNext` (`.cpp:324-336`: takes under the lock, adopts outside it, logs `stateAdopted` after `releaseState`), `endLatched`, `counters().queueSize`, destructor (`.cpp:173-183`), `Core` (link, task). Row 4 adds the call here.
- `src/activities/games/GameMatchActivity.cpp:513-560` `loop()` (end read, `roundEndPending`), `:562-664` `loopPlaying` (its `pollNearby` at `:642`), `:118-124` destructor (`slotLeaked`), `:393-412` `leave`. `.h:121` `preventAutoSleep`, `:255` `slotLeaked`.
- `src/activities/games/GameLobbyActivity.{h,cpp}` -- `.h:116` `preventAutoSleep`; `.cpp:189-209` `buildScreen` (stores `builtSerial` before the push), `:325-333` `activateIndex`'s serial check, `.h:225-230` comment. `UiListActivity::render` (`src/activities/UiListActivity.cpp:153-169`, upstream: read only) calls `renderUi()` (publishes the touch table) then `displayBuffer()`; touch routes against the published table while the push runs.
- `src/main.cpp:725-743` `[MEM]` block: the `#if CROSSHATCH_SD_LOG` branch is the comment and `if (millis() - lastMemPrint >= 10000) {`. `powerManager.getBatteryPercentage()` (`lib/hal/HalPowerManager.cpp:154`, the status bar's: `BaseTheme.cpp:116`); `BatteryMonitor::readMillivolts()` (SDK; HalPowerManager and HalGPIO each hold a function-local `static const BatteryMonitor`).
- `docs/crosshatch/upstream-touches.md:46` row 14, `:48` the paragraph under the table.
- `scripts/device_log.py` -- `MEM_LINE` (`:346`), `add_line` (`:497-555`; non-matching `[MEM]` lines go to `other_mem`), `CARD_EVENT` boots (`:367`, `:489`), raw card logs carry `# boot N` header lines (`SD_HEADER`), `summarize` (`:704-742`), module docstring (`:1-45`). `scripts/device_log_test.py`.
- `test/diag_log/DiagLogWriterTest.cpp:684-709` `DiagLogMain.TheHeapLinesNeedNoUsbHostOnlyWhenTheDefineIsOn` (row 14's text pin).
- `test/game_script/harness/NearbyMatchTest.cpp` -- rig (`startMatch`, `frame`, `step`, `run`, `idle`, `move`, `playRound`, `holdHostVmInACall`, `endTheLeakedTasks`), tests at `:583` and `:705` (held loop), `:800` (sleep), `:1087-1112` (leak; pin `endCalls == 0`), `:1186` (round end before end). `nearby_match.cmake`. `NearbySessionSupport.h` (`ObservedLink`, `LinkProbe`, `linkLines`). `screen_stubs/FakeRtos.h` (`arm(At::Log, skip, "GameVM")`, `waitParked`, `release`). `MatchSupport.h:49` `installFixtureAs`.
- `test/game_script/harness/games_check/RoundFile.h` (`loadRound`, `Step`), `ScriptVm.h` (`OwnedVm::create`), `games_check.cmake:66-71` (`games_check_core`). Companion rounds: `test/game_script/first_party/{battleship,ultimate-tic-tac-toe}/rounds/won-by-seat-1.lua` and their `taps.lua`.
- `test/game_script/harness/NearbyLobbyTest.cpp` -- `:116` (Choose sleep pin), `:338` rebuilt-rows test, `:604` `onDisplay` hook; `screen_stubs/GfxRenderer.h:35-41` (`onDisplay` runs inside `displayBuffer`); `ScreenDoubles.cpp:27-45` (a blocking `RenderLock` by its own holder is counted in `fakelock::selfDeadlocks`, never waited).
- `docs/crosshatch/game-canvas.md:150-161` (end read, "A `STATE` still queued ... is not adopted"), `:164-178` (Leaving: three copies from player-left, the abandoned VM's session).
- `docs/crosshatch/fork-scripts.md:142-147` (Heap lines), `:195-` (`summary`).
- `_bmad-output/initiative-crosshatch-player-v1/epic-play-nearby/epic-play-nearby.md` -- R9 (`:33`), R12 (`:36`), R13 (`:37`), R14 (`:38`), Boundaries (`:54`), Decision 2026-10-13 flush before sleep (`:170`).

## Tasks & Acceptance

**Execution:**
- [x] `src/games/GameVM.{h,cpp}` -- In `nearbyStep`: set a new `std::atomic<bool>` (e.g. `adopting`, release) before `adoptNext` and clear it (release) after `rounds.adopted`, the publish, and `logRound`, also when nothing was taken; add a loop-task reader `adopting()` (acquire). After an adopt with `Adopted` or `RoundStarted` whose draw published a frame (`frameBuffers.frameGen()` moved during the step), keep `noteSeatDrawn(rounds.shownSeat())` and then set `seatFrame` to `frameBuffers.frameGen()`, with a comment citing R4 and inception Q3 -- rows 1 and 2.
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- Row 2: in `loop()`, before `roundEndPending` is read, compute a hold for Playing only: when `nearby->endLatched()` and (`nearby->counters().queueSize > 0` or `vm->adopting()`), start the hold clock on its first pass (`millis()`), call `vm->pollNearby()`, and skip `takeEnd` until `PEER_END_HOLD_MS` (1,000 ms, a named constant) has passed since the hold began; then honour the end and log one `LOG_INF` naming the wait. Read `roundEndPending` after the hold check. Paused, Over, and every other state keep today's rule. Row 3: `preventAutoSleep()` returns `nearby != nullptr` and the state is neither `PeerGone` nor `Error`. Row 4: in the destructor's `slotLeaked` branch, call the new `NearbySession` call before `(void)nearby.release()`; `leave()` unchanged. Update the header comments that say otherwise.
- [x] `src/games/NearbySession.{h,cpp}` -- Row 4: add a call (e.g. `bool shutDownLink()`) that stops the `GameLink` task (`core->task.stop(joinMs)`) and, when it stopped, ends the link (`core->link->end()`), freeing nothing; when the task does not stop, it logs as the destructor does (`GameLinkLog::taskLeaked`) and leaves the link up. A second call ends the link no second time. Its comment lists what a leaked VM still calls (`ship`, `adoptNext` → `takeState`/`releaseState`, `session()`, `takeEnd`, `endLatched`) and that these touch only the lock, the queue, the pool, the outbound slot, the end slot, and `sessionNow`, never the link (Design Notes D4).
- [x] `src/activities/games/GameLobbyActivity.{h,cpp}` -- Row 3: `preventAutoSleep()` returns `session != nullptr && current != Screen::Ended`, comment updated. Row 16: override `render(RenderLock&&)`: read `rowSerial` (under the held lock), call `UiListActivity::render`, then store `builtSerial` after the push; remove the store from `buildScreen`; update the `builtSerial` comment.
- [x] `src/main.cpp` -- Row 8: inside the `#if CROSSHATCH_SD_LOG` branch, after `if (millis() - lastMemPrint >= 10000) {`, log one line `LOG_INF("MEM", "Battery: %u mV, %u%%", ...)`: millivolts from a function-local `static const BatteryMonitor` (`readMillivolts()`, 0 when the read fails) and percent from `powerManager.getBatteryPercentage()`, both read under a blocking `RenderLock` scoped to the read; extend the branch's comment. Nothing else in the file.
- [x] `docs/crosshatch/upstream-touches.md` -- Row 14's text and the paragraph under the table name the battery line, its reads, and its lock.
- [x] `scripts/device_log.py` -- Parse the battery line before the `[MEM]` fallback (it is not an "other" `[MEM]` line). Track the current boot per device (a `card file ...: boot N` event in a merged log, a `# boot N` header line in a raw card log; `-` when neither). `summarize` prints, per device, `battery:` with the line count, the first and last reading (mV, %, time), and the same per boot. Docstring's `summary` paragraph names it.
- [x] `scripts/device_log_test.py` -- Tests: a raw card log over two boots and a merged log of two devices give the first and last reading per device and per boot with their times; the battery line is not counted as an other `[MEM]` line; a log with no battery line prints `battery: none`.
- [x] `test/diag_log/DiagLogWriterTest.cpp` -- Update row 14's pin: the define-on branch is the comment, the `if` on the period alone, and the battery read and line; the `#else` branch is upstream's, unchanged.
- [x] `test/game_script/harness/NearbyMatchTest.cpp` -- Row 1: (a) guest's loop held, host moves (STATE queued), one guest pass with a tap on a free cell: "Dropped a touch made under frame" logged, no new "tap for seat 2", no "apply seat 2", nothing shipped by the guest (`state sent` unchanged after idle); (b) the same with the host's over and Play-again STATEs queued (A8); (c) after the adopted frame is on the panel, a tap is played. Row 2: (d) host wins while the guest's loop is held (over STATE queued, acked), host leaves; guest's loop resumes: `Playing -> Over on RoundOver`, "over for seat 2" once, then PeerGone, in that order; (e) the bound: with the guest's VM parked at its `state adopt` log line (after the take, before the draw and count), the end is held until `PEER_END_HOLD_MS` of fake time and honoured then, not before; the existing empty-queue tests still pass unchanged. Row 3: pin `preventAutoSleep()` in Playing, Paused, Over (true), PeerGone and Error (false); solo false. Row 4: rename the two leak tests and change their pins to the new rule with the reason: after `destroyHost()` the host link's `endCalls` is 1 and its `GameLink` task is not alive; adjust `endTheLeakedTasks` (no GameLink task left to delete). Row 9: one test per first-party game (Design Notes D6).
- [x] `test/game_script/harness/NearbyLobbyTest.cpp` -- Row 3: Choose false, Host, Browse, Joined true, Ended false (`:116` pin changes, with the reason). Row 16: a guest lists two hosts; the first leaves and expires; inside `onDisplay` of the push that draws the new rows, the test taps row 0's rect and runs one guest loop pass: nothing is joined (the remaining host's seats stay 1 of 2, the guest stays Browsing) and `fakelock::selfDeadlocks` does not move.
- [x] `test/game_script/harness/nearby_match.cmake`, `MatchSupport.h`, `NearbySessionSupport.h` -- Row 9 support: compile definitions for the games folder and the companion folder; a generic install helper taking a package directory (`installFixtureAs` becomes a call of it); `games_check_core` linked if it builds (D6); `ObservedLink` records each `STATE` frame sent and received (ver, snapshot bytes), with a comment naming the device behaviour it stands in for and a test pinning that a recorded sent frame equals the peer's received one.
- [x] `docs/crosshatch/game-canvas.md` -- Row 6: the player-left Back sends no `ABORT` to a seat whose `ABORT` was heard (F8; `copies=0` when that was every peer), the error view's Back sends three. Rows 2 and 4 make two sentences false: the queued-`STATE` sentence (`:160-161`) states the hold and its bound; the Leaving paragraph states that the destructor stops the leaked session's task and link.
- [x] `docs/crosshatch/fork-scripts.md` -- Row 8: a **Battery** bullet beside **Heap lines** and the `summary` paragraph's battery block.
- [x] `_bmad-output/initiative-crosshatch-player-v1/epic-play-nearby/epic-play-nearby.md` -- Row 5: append to the 2026-10-13 flush-before-sleep Decision, keeping its words, "As built (entry 19; cross-story review row 5, 2026-10-14): ..." (lines logged up to the flush are kept; "Entering deep sleep" is logged after it in `enterDeepSleep` and never reaches the card; the packet looks for the power-button line). Epic file currency (D7): R9, R12, R13, R14, and Boundaries.

**Acceptance Criteria:**
- Given the fix tree, when the host suites, the script tests, `check_upstream_touches.py`, and `check_layers.py` run, then all pass, and each new test above fails with its fix reverted (mutation recorded in Implementation Notes).
- Given `check_flash_budget.py` at `c58f28aa` and at the fix commit in this worktree, when compared, then the fix adds at most 1,984 B flash and 80 B static RAM, and `compare` and `objects` exit 0.
- Given define-off `x4pro`, `default`, and `sticky` builds of the fix tree with and without its `main.cpp` change, when compared with `cmpimg.py`, then only the app descriptor's stamps and digests and the trailer differ.
- Given the epic file, when R9, R12, R13, R14, and Boundaries are read beside the Notes' Decisions, then none says what a Decision superseded, and each edit cites its Decision.

## Implementation Notes

- **Where each row landed.** Rows 1 and 2: `GameVM::nearbyStep` (floor move after an adopt whose draw moved `frameGen()`; `adoptingState` set before `adoptNext`, cleared after `logRound` and on the not-taken path; `GameVM::adopting()` acquire) and `GameMatchActivity::holdEndForQueuedState()` (an `enum class EndHold { Idle, Holding, Spent }`, `PEER_END_HOLD_MS = 1000`, one `LOG_INF` "the peer's end waited N ms behind a STATE the VM did not adopt; honouring it"; `Spent` keeps a run-out hold from starting again, since the end latches once). Row 3: both `preventAutoSleep()`s. Row 4: `NearbySession::shutDownLink()` (a `linkDown` flag on `Core`, owner's task only, so a second call ends nothing); the destructor now calls it and releases `core` when it answers false, which is the old destructor's behaviour line for line (the `!core` guard and the leak on a task that will not stop are kept). Row 16: `GameLobbyActivity::render` override reads `rowSerial` under the held lock, calls `UiListActivity::render`, stores `builtSerial` after it; the store left `buildScreen`. Row 8: `src/main.cpp`'s define-on `[MEM]` branch only (`git diff` shows the hunk inside `#if CROSSHATCH_SD_LOG` alone); `device_log.py` gains `BATTERY_LINE`, `CARD_BOOT_HEADER`, `BatterySeries`, `battery_report`.
- **D6: the RoundFile path, not the fallback.** `games_check_core` links into `NearbyMatchHarnessTest` beside `game_match_src` (as `GamesCheckFlowTest` already does), so each game's steps come from `games_check::loadRound` over its companion's `rounds/won-by-seat-1.lua` on `CANVAS_474`, with the host's installed copy's sources and the companion's top-level modules. Waits are on each device's `render()` count, not its pushes: Battleship's waiting seat draws a frame identical to the panel's while the other seat places, and `FrameReplay` skips that push (the frame still counts as displayed, `CanvasGate::markDisplayed`). `git diff c58f28aa -- 'test/game_script/harness/*.cmake' scripts .github` adds no line naming either game.
- **Test-rig changes.** `endTheLeakedTasks` now waits for no task at all and frees the Duo as usual; when a GameLink task is still running (the fix reverted) it reports the failure, deletes that task, and leaks the Duo, so the mutation fails cleanly instead of crashing. `ObservedLink` records STATE frames into `LinkProbe` under a mutex (`statesSent()`, `statesReceived()` return copies).
- **Mutations (each new test fails with its fix reverted; host build of the named target, then that test binary with a filter; every file restored after):**
  - M1, the `seatFrame = frameBuffers.frameGen()` line removed: `ATapQueuedBehindAStateThatPassesTheTurnIsDroppedAndOneMadeAfterTheAdoptIsPlayed` and `ATapQueuedBehindTheOverAndTheNextRoundsStatesIsDropped` fail.
  - M2, `endHeld` forced false: `AnEndLatchedBehindAQueuedOverStateWaitsForOverAndThenEntersPeerGone` and `AnEndHeldBehindAStateTheVmNeverAdoptsIsHonouredAfterTheBound` fail. M2b, `|| vm->adopting()` dropped: the bound test fails (the end is honoured on the pass after the take).
  - M3, the match's `preventAutoSleep` back to `nearby != nullptr`: `ANearbyMatchHoldsTheDeviceAwakeWhileLiveButNotInThePlayerLeftViewAndASoloOneNever` and `AScriptErrorOnTheMoverSendsThreeAbortsAtOnceAndThePeersViewHasTheGameErrorLine` fail. M3b, the lobby's back to `true`: `TheLobbyHoldsTheDeviceAwakeOnlyWhileItsSessionIsLive` and `TheChooseScreenOffersHostAndJoinUnderTheGamesNameWithCancelInTheFooter` fail.
  - M4, the destructor's `shutDownLink()` call removed: both `...WithAStuckVmEndsTheLeakedSessionsLinkInTheDestructorAndFreesNothing` tests fail.
  - M16, `builtSerial.store(rowSerial)` put back in `buildScreen` (before the push): `ATapDuringThePushOfRebuiltRowsJoinsNothing` fails.
  - M8, the battery block cut from `main.cpp`: `DiagLogMain.TheHeapLinesNeedNoUsbHostOnlyWhenTheDefineIsOn` fails; `BATTERY_LINE`'s match disabled in `device_log.py`: the three new battery summary tests fail; the `# boot N` header read removed: the raw-card-log test fails.
  - Row 9's two tests and `ARecordedStateSentIsTheStateThePeerReceived` pin no fix of their own (row 9 is test-only); they pass on the fix tree.
- **Host results.** `ctest` all pass (2,607 tests, two dump tests skipped as usual); `ctest -R 'NearbyMatchHarnessTest|NearbyLobby|NearbyMatchTest' --repeat until-fail:20 -j8`: no failure. Fork script tests all pass (`device_log_test.py` 79 tests); `check_upstream_touches.py` passes at HEAD and at a `git stash create` commit of the working tree; `check_layers.py` passes; `./bin/clang-format-fix` run twice, the second changing nothing.
- **Not run here (after the review, per the plan's Verification):** `check_flash_budget.py` (base recorded above), `pio run -e default`, the define-on `x4pro` and `default` builds, the define-off `x4pro` / `default` / `sticky` image comparisons with and without the `main.cpp` change, `pio check` (four runs), and `sim.sh build x4pro`.
- **Left as written (D7):** R6's and R8's "set `preventAutoSleep`" and spine AD-21's line: row 3 is a triage fix for the owner to confirm at entry 14, not a Notes Decision.

## Plan Change Log

## Review Triage Log

### 2026-10-14 — Review pass
- lenses: blind-hunter (14 findings), edge-case-hunter (3), verification-gap (1 gap, 1 other), intent-alignment (7 divergences, each logged as a finding). Each ran as a context-free subagent.
- verdicts: 26 findings — high 0, medium 2, low 20, false 4, maybe-false 0
- findings:
  - `[medium]` `[patch]` (blind) The battery read takes a blocking `RenderLock` every 10 s, so in the diagnostics builds that device runs use, `loop()` can stall for a whole e-ink push. — Patched: `RenderLock::Mode::Try`, the same mode the gauge-capacity step uses; a period that finds a render under way logs no battery line, and the heap lines still go. Comment, ledger row 14, `fork-scripts.md`, and the text pin follow.
  - `[low]` `[reject]` (blind) The battery line carries no charging or external-power flag, so a drain comparison taken with a cable attached is meaningless. — The run it serves is cardless, with no cable. Adding fields means new reads and new summary logic, more than the row asked for.
  - `[low]` `[reject]` (blind) A failed read (0 mV) can be picked as the first or last reading. — A gauge read rarely fails, and the docs say 0 mV means a failed read, so a reader sees it plainly. Skipping such readings adds branches.
  - `[low]` `[reject]` (blind) The summary prints no drain arithmetic, and a raw multi-boot log's device-wide first and last mix two `[millis]` clocks. — The row asks for the first and last reading per device and per boot, with times, which is what is printed. The per-boot lines carry each boot's own clock.
  - `[false]` `[reject]` (blind) Per-boot attribution in a merged log depends on line order, so a boot-7 line could fall under boot 8. — `merge` gives each device's lines monotone clamped times and sorts by (ms, device, sequence) (`device_log.py` `entries.sort`, the "monotone clamp"). A device's lines therefore keep file order, with each card file's event ahead of its lines.
  - `[low]` `[reject]` (blind) The epic Notes carry no dated Scope note for this build's widening of ledger row 14 (or for rows 1 to 16). — The authority is the cross-story review's row 8 and the orchestrator's dispatch. The epic Notes' Decisions and Scope notes are the orchestrator's to write (`orchestrated-epics.md`, Decisions), and it records `## e6-xr`.
  - `[low]` `[defer]` (blind; the same as intent divergence 1) R6's and R8's "set `preventAutoSleep`" no longer match the code after row 3. — Real, but the intent's currency paragraph covers only lines that a Notes Decision superseded, and row 3 prescribes code and tests only. The intent leaves this edit out. Carried in `deferred` for the orchestrator.
  - `[low]` `[reject]` (blind) Ended, PeerGone, and Error now auto-sleep but keep the radio up until the timeout, and no test drives an auto-sleep forced exit from PeerGone or Error. — The radio staying up there is the existing `## 6.15` deferral, as the review's row 3 says. A forced exit from those states was already reachable by a power-button sleep, and `leave()` applies F8 whatever its caller (`NearbyLeaveTest`).
  - `[low]` `[reject]` (blind) The hold's expiry line is `LOG_INF` with no `device_log.py` pattern, and a tap dropped after an adopt reuses the existing line. — A hold that expires means a VM stuck for over 1 s, which the watchdog line then reports anyway. The row asked for neither a summary field nor a new line.
  - `[low]` `[reject]` (blind) `holdEndForQueuedState` returns to Idle and can hold again; `adoptingState` is set on every VM wake. — The comment's "never begun again" is about `Spent` only, which is the code. A momentary `adopting` read holds the end for one pass and clears on the next (Idle). It is harmless and bounded.
  - `[false]` `[reject]` (blind) `shutDownLink()` has no tests of its failure path and no null check on `core`. — The destructor now runs through it, and `NearbySessionTest.ATaskThatWillNotStopLeaksItsStateAndIsLoggedRatherThanFreed` pins the failure path. `core` is null only after the destructor has released it, when no caller can reach the object.
  - `[low]` `[reject]` (blind) The first-party nearby tests play only a host-wins round. — Row 9 asks for one round to Over and through Play again per game. The fixture suite pins the host adopting a guest's over (`SeatTwosMoveEndsTheRoundAndSeatOneReachesOverOnce`).
  - `[low]` `[patch]` (blind; the same as intent divergence 8) Two doc paragraphs are not wrapped to 120 columns (`game-canvas.md` Leaving, `fork-scripts.md` `summary`). — Patched: rewrapped, text unchanged.
  - `[low]` `[reject]` (blind) The row-14 text pin is exact to the byte and breaks on reformatting. — An exact pin is the established form for the row-14 branch (entry 21's pin), and it is deliberate: the define-on branch of an upstream file is meant to change only through a ledgered edit.
  - `[low]` `[reject]` (edge) An overlay opened during a hold longer than the bound lets the first pass after it honour the end with the STATE unadopted. — A hold lasts milliseconds while the loop runs, since each pass wakes the VM. An end latched behind an overlay starts its hold only when the loop returns. Counting only the time the loop runs adds state for a race that needs an overlay opened inside a few-millisecond window.
  - `[low]` `[patch]` (edge) The text pin's comment-line loop wraps `npos + 1` to 0 and loops forever when the `if` is found mid-line. — Patched: the loop ends and fails on `npos`.
  - `[low]` `[patch]` (edge) The epic's As-built sentence says the power-button line is the last line before sleep. In fact `enterDeepSleep` logs activity lines before the flush, and an auto-sleep logs no power-button line. — Patched to the packet's wording: the power-button line or a line just after it, for a power-button sleep.
  - `[medium]` `[defer]` (verification-gap) No CI build compiles `main.cpp` with `-DCROSSHATCH_SD_LOG=1`, so the battery block is pinned only by its source text. — This is cross-story review row 17, an owner item (a CI job in its own follow-up). This build runs the define-on `x4pro` and `default` builds and `pio check` locally (Verification). Carried in `deferred`.
  - `[false]` `[reject]` (verification-gap, other) The plan records the firmware checks as not run and its status as blocked, so the diff may not be the tree the host results describe. — The plan was mid-run when the lens read it: the host suites were re-run on this exact tree (2,607 pass), and the firmware checks run after this pass, as the Verification orders.
  - `[low]` `[reject]` (intent 2) No new test pins "queue empty → honoured at once". — The end-to-end behaviour stays pinned by the existing `ALatchedEndWaitsBehindAnOverlayAndTheNextLoopPassEntersPeerGone` and the PeerGone suite, all unchanged and passing.
  - `[low]` `[reject]` (intent 3) The A8 test does not isolate the RoundStarted adopt's own floor move. — In a nearby match `loopPlaying` enters Over before it reads a gesture, so a tap tagged with the over frame cannot reach the queue. The case A8 names (a tap queued before Over, both adopts queued) is pinned and fails with the floor move removed (mutation M1).
  - `[low]` `[reject]` (intent 4) The radio stays up from `leave()` until the destructor. — A named choice (D4): the row says "before the release", and the destructor runs as the manager takes the screen down after `goToGames()`.
  - `[low]` `[reject]` (intent 5) "The same snapshot" is checked at the link level only. — A named reading (D6): the harness has no seam into a VM's snapshot, and the guest's later frames and its Over count are asserted beside it.
  - `[low]` `[reject]` (intent 6) The battery line comes before the heap lines, and millivolts are read from a second `BatteryMonitor`. — "Beside" holds. HalPowerManager's monitor is a private function-local static, and a second function-local static is the pattern HalPowerManager and HalGPIO already use. The percent is the status bar's own.
  - `[false]` `[reject]` (intent 7) The lobby's "no session" may differ from Choose. — Choose is the only screen with no session: `failToChoose` and the initial screen both have none, and every other screen is entered from a session.
  - `[low]` `[reject]` (intent 1, 8) Counted under the blind rows above (R6 and R8 deferred, the wraps patched); listed here so every reported divergence has a row.

## Design Notes

**Settled by the sources.** Every fix's design is its row's "Reason / action" (`cross-story-review.md`, Triage); the orchestrator's prompt confines upstream code to `main.cpp`'s `[MEM]` branch with row 14 amended, forbids `games/`, and says "allocate nothing" for row 8. Rows 14 and 15 are carried as `deferred` with the review's reasons.

**Guards kept** (`git log -L` read for `loop`, `nearbyStep`, the destructor, `buildScreen`). `loop()`: `allows(PeerGone)` first, so the reason stays latched in Error, PeerGone, Leaving; `roundEndPending` Playing only, so Paused never holds the end; one read per pass. `nearbyStep`: `!step.taken` returns false (the pass goes on to Play again and the queue); the draw is only for an Ok outcome and an adopting result (a refusal changed nothing). Destructor: `!slotLeaked` returns (a normal session is freed by its unique_ptr), `store.leak()` before the release (the leaked VM may call `ch.store.set`). `activateIndex`'s serial check stays as is; only where `builtSerial` is stored moves.

**Choices named (not settled by the row's text).**
- D1 (row 1): "an adopt that draws" is read as the step's draw moved `frameGen()`; a no-local-seat adopt that draws nothing leaves the floor. `drawnSeat` still comes from `noteSeatDrawn`.
- D2 (row 2): the bound is `PEER_END_HOLD_MS` = 1,000 ms from the first pass that holds (the row's "about 1 s"), on `millis()` (the harness's fake clock). "A `STATE` the VM has not adopted" includes one the VM has taken but not yet drawn and counted: the queue empties at the take, and the over event runs before the round end is counted, so the queue alone would let `takeEnd` win in that gap. Hence the VM's `adopting` flag. Order: VM stores `adopting=true`, then takes under the session lock, and stores false (release) only after `rounds.adopted` counted the end; the loop reads `queueSize` under the same lock, then `adopting` (acquire), then `roundsEnded` — so a loop that sees false also sees the count.
- D3 (row 3): the lobby's "holds no session (Choose)" is `session == nullptr`; Ended keeps its session (Back to the list) and still returns false.
- D4 (row 4): the call is made in the destructor only, "before the release" as the row says; `leave()` keeps the session up until the destructor, which runs as the manager takes the screen down after `goToGames()`. What a leaked VM calls never reaches the link: `ship()` touches `core->out`, `store->outbound()`, `sessionId()` (lobby objects); `adoptNext()` takes and releases a queue entry under the lock and logs with `session()` (`sessionNow`); `shipStep`, the only sender, runs on the stopped task. `ILink::end()` is idempotent (`EspNowLink::end`).
- D5 (row 8): the status bar's percent is `powerManager.getBatteryPercentage()`; the millivolts are `BatteryMonitor::readMillivolts()` (gauge on the X4 Pro, ADC elsewhere). `RenderLock` because the render task reads the same gauge in the themes and HalPowerManager's cache is unsynchronized (main.cpp's gauge-capacity step takes it for the same reason); blocking, at most once per 10 s, in diagnostics builds only. The function-local static is HalPowerManager's and HalGPIO's own pattern and allocates nothing.
- D6 (row 9): each game's steps come from its companion's `rounds/won-by-seat-1.lua` (a proven legal playthrough), read with `games_check::loadRound` on the harness canvas (474 x 788), seat 1's steps tapped on the host and seat 2's on the guest, offset by the canvas origin. If `games_check_core` cannot link into the suite, the fallback is a C++ step list written from the companion's moves through the game's `layout.lua` / `board.lua`, with a comment naming the source; Implementation Notes say which. Each tap waits until its device shows the previous move (the adopted frame pushed, or the tap is dropped by row 1's floor); a `move = false` step ships nothing, so its wait is on the device's push, not on an adopt. The match is entered with the game's own manifest parsed from its `manifest.json`. Asserted: every `state adopt` line of both devices says `Adopted` or `RoundStarted`; the over `STATE` the winner sent equals, byte for byte and by `ver`, the one the other device received and adopted (the link-level reading of "the same snapshot", since the harness has no seam into a VM's snapshot); each step's `shows` is on the mover's next push; both reach Over, the guest once, and through the host's Play again both reach the new round. "No tooling names a game": the cmake adds folder paths only; `git grep -n -e battleship -e ultimate-tic-tac-toe -- test/game_script/harness/*.cmake scripts .github` shows no new hit.
- D7 (epic file currency): R9 (`NEARBY_BUILT` on the X4 Pro and the simulator only: Decision 2026-10-13, Nearby on the X4 Pro only); R12 (entry 14 compares idle drain after a match with a fresh boot: Decision 2026-10-13, no restart after nearby); R13 (the device-run path is the card log merged by `device_log.py merge`, the serial capture the other path whose device checks wait for a USB run, `## 6.3`: Decision 2026-10-13, the on-device diagnostics log); R14 (firmware also built with `-DCROSSHATCH_SD_LOG=1`: same Decision); Boundaries (the diagnostics log: `lib/DiagLog`, `src/diagnostics`, ledger rows 13 and 14). Each edit cites its Decision and keeps the Decision's wording. R6 and R8's "set `preventAutoSleep`" and spine AD-21's line are left as written: row 3 is a triage fix the owner confirms at entry 14, not a Notes Decision (named in the result for the orchestrator).
- Doc sentences rows 2 and 4 make false are corrected in `game-canvas.md` with row 6's paragraph, so the doc matches the code.

## Verification

**Commands (during implementation):**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass.
- `ctest --test-dir build/test -R 'NearbyMatchHarnessTest|NearbyLobby' --repeat until-fail:20 -j8` -- expected: no failure (threaded suites).
- `for t in scripts/*_test.py; do python3 "$t" || exit 1; done`; `python3 scripts/check_upstream_touches.py`; `python3 scripts/check_layers.py`; `./bin/clang-format-fix` twice -- expected: pass; second run changes nothing.

**Commands (once, after review; `flock /tmp/crosshatch-build.lock`, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, `df -h /` ≥ 5 GB before each):**
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare`, `objects` -- expected: exit 0; delta over the base run at `c58f28aa` ≤ 1,984 B flash, ≤ 80 B static RAM. Base, measured 2026-10-14 in this worktree before any change (same four commands, build lock, shared cache, incremental tree; scratch `e6-xr/budget-base.log`): `firmware.bin` 6,255,376 B on, 5,957,312 B off, +298,064 B flash; static internal RAM 190,328 B on, 189,464 B off, +864 B; `objects` no problems. That is +40,016 B / +80 B over the epic base, as entry 13 recorded.
- `pio run -e default` -- expected: builds.
- `PLATFORMIO_BUILD_FLAGS="-DCROSSHATCH_TOUCH_LOG=1 -DCROSSHATCH_SD_LOG=1" pio run -e x4pro` and `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1 pio run -e default` -- expected: build.
- Define-off `pio run -e x4pro`, `-e default`, `-e sticky` at the fix tree and again with `src/main.cpp` from `c58f28aa`, each `firmware.bin` pair compared with `cmpimg.py` (scratch `6.22/`) -- expected: only descriptor stamps, digests, trailer differ; main.cpp restored after.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and with `-e x4pro`, each also with `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1` -- expected: no defect.
- `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- expected: builds.

**Manual checks:**
- The epic file edits read beside the Notes' Decisions (AC 4).

**Results (2026-10-14).**
- **Host (after the review patches):** `ctest` 2,607 of 2,607 pass (the two dump tests skipped, as usual). Every `scripts/*_test.py` passes; so do `check_upstream_touches.py` (PASS) and `check_layers.py`. `./bin/clang-format-fix` changed nothing on its second run. The nearby suites' 20 repeats passed on the pre-patch tree, and the patches did not touch those suites. The mutation runs are under Implementation Notes.
- **Firmware:** one script under the build lock with the shared cache (scratch `e6-xr/fw.sh`, log `fw.log`), 8.4 to 10.1 GB free before each build. All of these pass:
  - `check_flash_budget.py` `build on`, `build off`, `compare`, and `objects`: exit 0, 63 game objects, no problems;
  - `pio run -e default`;
  - the define-on `x4pro` build (`-DCROSSHATCH_TOUCH_LOG=1 -DCROSSHATCH_SD_LOG=1`) and `default` build (`-DCROSSHATCH_SD_LOG=1`), which are the first compile of the battery block;
  - `pio check` (low, medium, high) for `default` and `x4pro`, each with and without `-DCROSSHATCH_SD_LOG=1`: four PASSED, no defects;
  - `sim.sh build x4pro`.
- **Define off, row 8:** I built `x4pro`, `default`, and `sticky` on the fix tree, then again with `src/main.cpp` from `c58f28aa`, and restored the file. `cmpimg.py` compared each pair (scratch `e6-xr/cmpimg.log`), and all three exit 0:
  - x4pro: 6,255,904 B against 6,255,904 B;
  - default: 5,927,792 B against 5,927,792 B;
  - sticky: 6,138,368 B against 6,138,368 B.
  - In each pair only the descriptor's `app_elf_sha256` (32 B) and the trailer (33 B) differ, so every loaded byte is the same.
- **Flash budget:** +528 B flash, +0 B static RAM over the base. The method is in `## Auto Run Result`.
- **Manual:** the epic file edits were read beside the Notes' Decisions. R9, R12, R13, R14, and Boundaries each cite their Decision. R6 and R8 are deferred, as the Review Triage Log says.

## Auto Run Result

**Status: built.** This run first handed back as `blocked` while its implementation subagent ran detached. The orchestrator resumed it: the subagent had finished, the run went on through review and verification, and that earlier blocked note is superseded.

**Summary.** The build fixes cross-story review rows 1, 2, 3, 4, 5, 6 (the doc), 8, 9, and 16, and brings the epic file current.
- **Row 1:** an adopt that draws now moves the VM's touch-tag floor to the frame it just published. A tap queued behind the peer's move, or behind its over and next-round `STATE`s, is dropped.
- **Row 2:** in Playing, a latched end waits behind a `STATE` the VM has not adopted. That covers one still queued and one taken but not yet drawn and counted (`GameVM::adopting()`). The wait is bounded at `PEER_END_HOLD_MS` (1,000 ms), so a peer that wins and leaves still takes the other device to Over first.
- **Row 3:** the match lets the device sleep in PeerGone and Error, and the lobby in Choose and Ended.
- **Row 4:** `NearbySession::shutDownLink()` stops the GameLink task and ends the link, freeing nothing. The match's destructor calls it on the leaked-VM path.
- **Row 16:** the lobby stores `builtSerial` after the push, so a tap during the push of rebuilt rows joins nothing.
- **Row 8:** a diagnostics build logs `[MEM] Battery: <mV> mV, <percent>%` in `main.cpp`'s define-on `[MEM]` branch, under a render lock it only tries (ledger row 14 amended). `device_log.py summary` lists the first and last reading per device and per boot.
- **Row 9:** Battleship and Ultimate tic-tac-toe each play their companion's `won-by-seat-1` round to Over and through Play again in a two-device nearby match.
- **Docs:** `game-canvas.md` (F8, the hold, the leaked session's link) and `fork-scripts.md`.
- **Epic file:** row 5's As-built sentence, plus R9, R12, R13, R14, and Boundaries brought current with their Decisions.

**Files.**
- `src/games/GameVM.{h,cpp}`: the floor move and `adopting()`.
- `src/activities/games/GameMatchActivity.{h,cpp}`: the end hold, `preventAutoSleep`, and the destructor's `shutDownLink`.
- `src/games/NearbySession.{h,cpp}`: `shutDownLink()`, which the destructor now uses.
- `src/activities/games/GameLobbyActivity.{h,cpp}`: `preventAutoSleep` and the `render` override.
- `src/main.cpp`: the battery line, upstream file, ledger row 14.
- `docs/crosshatch/upstream-touches.md`: row 14 and the paragraph under the table.
- `scripts/device_log.py` and `device_log_test.py`: the battery summary.
- `test/diag_log/DiagLogWriterTest.cpp`: row 14's pin.
- `test/game_script/harness/NearbyMatchTest.cpp`, `NearbyLobbyTest.cpp`, `NearbySessionSupport.h`, `MatchSupport.h`, `nearby_match.cmake`: tests for rows 1 to 4, 9, and 16, `installPackageAs`, and the STATE record.
- `docs/crosshatch/game-canvas.md`, `docs/crosshatch/fork-scripts.md`.
- `epic-play-nearby.md`.
- This plan.

**Review (four lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment; 26 findings).**
- **Patched:** 4 findings (1 medium, 3 low).
  - The battery read's `RenderLock` is now tried, not blocking.
  - The text pin's comment loop no longer loops forever.
  - The As-built sentence matches the packet.
  - Two doc paragraphs are rewrapped.
- **Deferred:** 2, added to `deferred` beside rows 14 and 15.
  - R6, R8, and spine AD-21 still say the lobby and match set `preventAutoSleep` (low).
  - No CI build compiles the define-on `main.cpp` (medium; cross-story review row 17).
- **Rejected:** 20, each with its reason in the Review Triage Log: 4 false, 16 low (two of them named plan choices).
- `followup_review_recommended: false`: no high was patched, and only one medium.

**Verification.** Under `## Verification`, Results.

**Flash.** Measured in this worktree with `check_flash_budget.py` `build on`, `build off`, `compare`, and `objects`, under the build lock with the shared cache, on an incremental tree; it was not a fresh clone. The tree was this commit's, clean, before an amend that changed only this plan (scratch `e6-xr/budget-commit.log`).
- `firmware.bin`: 6,255,904 B games on, 5,957,312 B off, +298,592 B flash.
- Static internal RAM: 190,328 B on, 189,464 B off, +864 B.
- `objects`: no problems.
- The base at `c58f28aa`, measured the same way in the same worktree, was +298,064 B and +864 B. This build therefore adds **+528 B flash and +0 B static RAM**.
- The epic now stands at +40,544 B and +80 B over its base (+258,048 B and +784 B). That leaves 1,456 B and 80 B of its 42,000 B and 160 B share, and 8,608 B under the 300 KiB gate.
- The uncommitted tree measured +298,592 B before the commit, the same figure.

**Residual risks.**
- Rows 1 to 4 and 16 are tested only on the host harness. Their device behaviour waits for entry 14's run, as do the hold's 1,000 ms bound (an estimate) and the battery line's gauge reads.
- Row 9 checks "the same snapshot" at the link, as D6 says.
- The formatter changed nothing outside this build's files.
- Spine AD-21's `preventAutoSleep` line is left for the orchestrator.

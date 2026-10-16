---
title: 'dlab-c2: the transition test sweep on its own FreeRTOS task'
type: 'feature'
ticket: 'story-dlab-c2-sweep-task'
created: '2026-10-16'
baseline_revision: '8874e2ce977ce631c4cb5a2b1ab0206ee5692eb7'
status: 'built'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-c2-sweep-task.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
warnings: ['oversized']
deferred:
  - summary: >-
      serve()'s decision table (clean once, clock to 0 on any replay, run state from the latest press, pause before clean) has no host test.
    evidence: |-
      It lives in device-only code (TransitionTestActivity::serve, compiled only under CROSSHATCH_DISPLAY_LAB); test_sweep_control.cpp pins the request word's bit protocol only. Extracting the decision into a pure helper in lib/GameDisplayLab would let the host suite cover replay+clean, clean+resume and pause-during-replay.
    location: >-
      src/activities/games/display_lab/TransitionTestActivity.cpp (serve)
    severity: medium
  - summary: >-
      stopSweep() and endSweepLocked() wait for the task with no time limit, the latter while holding the render mutex.
    evidence: |-
      A scan whose BUSY wait never ends (the SDK's UC8179/UC8279 wait is unbounded, dlab-b2's deferral) wedges the loop task or onExit/sleep for good. GameVM has a time-limited join with an abandon fallback and a LOG_ERR; the same here would make a wedged scan diagnosable. Settle with the SDK BUSY bound.
    location: >-
      src/activities/games/display_lab/TransitionTestActivity.cpp (stopSweep, endSweepLocked)
    severity: medium
  - summary: >-
      Cover detection is a heartbeat timing: scans continue for up to COVERED_GAP_US plus the scan in flight under a covering screen, and a main-loop pass stalled over 300 ms ends the run as covered.
    evidence: |-
      ActivityManager::loop knows when it pushes the light panel over this screen but nothing tells the screen; an onCovered hook in the push path, or an atomic top-activity generation checked in enterPanel, would make it deterministic. Design change outside this story; the doc states both effects.
    location: >-
      src/activities/games/display_lab/TransitionTestActivity.cpp (loop, the enterPanel gate); src/activities/ActivityManager.cpp
    severity: medium
  - summary: >-
      The delivered-scan count and busy sum the screen passes to SlotScheduler::record have no test.
    evidence: |-
      Reverting to record(slot.scans, slot.busyUs) passes every host test; the screen is device-only. Closes with serve()'s deferred extraction (a SlotResult delivered-scans accessor pinned in test_gray_model.cpp).
    location: >-
      src/activities/games/display_lab/TransitionTestActivity.cpp (runSlot)
    severity: medium
  - summary: >-
      endSweepLocked can still delete a task blocked in DisplayLabActivity::scan's own inverted-display LOG_ERR when the display is inverted between the lambda's pre-check and scan's check.
    evidence: |-
      Closing it needs a flag set inside DisplayLabActivity::scan right before its RenderLock, which the story forbids changing; the window is one register read wide and the lab is dev-only.
    location: >-
      src/activities/games/display_lab/TransitionTestActivity.cpp (endSweepLocked); DisplayLabActivity.cpp (scan)
    severity: low
---

<intent-contract>

## Intent

**Problem:** The transition test runs its slots inline in `TransitionTestActivity::loop()`, and input is polled only from the main loop, so on an UltraChip panel (one scan = a whole SDK session, hundreds of ms) presses shorter than a slot are lost (the owner's first UC8179 run, epic D11).

**Approach:** Move the slot runner (slot clock, scheduler, stats, scene, model) onto a dedicated FreeRTOS "sweep task" built with GameVM's idioms. `loop()` only forwards controls as requests through a pure, host-tested request/state word (`GameDisplayLab::SweepControl`) and applies the visible result when the task ends. Every exit path stops the task and joins it (or, under the render lock, deletes it while it waits for that lock) before anything is freed.

## Boundaries & Constraints

**Always:** `GrayModel`, `GraySlot.h`, `SlotScheduler.h`, `TransitionScene`, `GrayPreset` unchanged (their tests pass as they are); every scan through `DisplayLabActivity::scan`, every clean through `lab.cleanNow()` under `RenderLock`; D4 (nothing drawn while the view is up) and D5 unchanged; task stack sized from a measurement, HWM logged once per run at `LOG_INF`, no task-side local over 256 B; task created in `startRun`, gone before `onExit` returns; `preventAutoSleep()` and `skipLoopDelay()` true while the sweep runs; covered detection that the task cannot defeat; task-creation failure: `LOG_ERR`, Run row "Stopped: not enough memory" (`tr`), nothing sent; no `RenderLock` in `onExit` (12cc816); `default` and `sticky` still build.

**Never:** changes to `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, `freeink-sdk/`, the stock bench, the frame-period or polarity screens; a second scan path; `Serial`; bare `new`; a new ledger row; generated I18n files edited; UI calls (rows, `requestUpdate`, `promoteNextRefresh`, `clearTapFlash`) on the task.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Run | Run row | task created; it cleans to white and sweeps from 0; `loop()` never waits on a scan | creation fails: `LOG_ERR` with the stack size, Run row "Stopped: not enough memory", view not entered |
| Pause / resume | Confirm, or a tap, while the view is up | served at the end of the slot in flight; clock stops; resume continues at the paused clock | two presses before the task serves them cancel out |
| Replay / Clean | NavNext / NavPrevious | at the slot boundary: clean, sweep from 0 and running / clean and paused | combined requests: clean once, clock 0 if any replay, run state = the latest press |
| Stop | Back while the view is up | the task ends after the scan in flight (the follow-up is skipped); join; rows with one FULL refresh; "Stopped" | — |
| Home | home gesture while the view is up | `handleHomeGesture` stops and joins, returns false; `onExit` promotes FULL | — |
| Sleep | power hold, timeout | `prepareForSleep` (render lock held) ends the task without waiting on it | task waiting for the lock: deleted |
| Covered | screen pushed over the running view | task refuses its next scan once `loop()` has not run for `COVERED_GAP_US` and ends; when the cover closes the view ends with FULL refresh, "Stopped" | exit while covered: `onExit` ends it as Sleep does |
| Scan failure / session cap / stats | as c1 | as c1, from the task; the Stats row is written under `RenderLock` when the view ends | — |

</intent-contract>

## Code Map

- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- the screen to change. The loop task keeps rows, `status`, `statsText`, `shownOnPanel`, input, `leaveView`. The task gets `runSlot`, `advanceClock`, `cleanToWhite`, `logStats`/`reportStats`, the pause, replay and clean bodies, `scheduler`, `scene`, `model`, `follow`, `elapsedUs`/`clockUs`/`sceneMs`/`sceneStale`/`statsSinceUs`.
- `src/activities/games/display_lab/DisplayLabActivity.h` -- `scan` (takes `RenderLock` itself, blocking), `cleanNow` (caller holds the lock), `scansBeforeClean`, `preset()`, `masks()`, `outcomeText`; read-only here.
- `src/games/GameVM.{h,cpp}` -- idioms: `TASK_PRIORITY 1`, `TASK_CORE 1`, `xTaskCreatePinnedToCore`, null-handle `LOG_ERR` with the stack size, `pxTaskGetStackStart` floor, `uxTaskGetStackHighWaterMark` log at run end, `STOP_POLL_MS` join poll, `abandon` (suspend, inspect, delete).
- `src/activities/ActivityManager.cpp` -- `onExit` is always called with the render mutex held (Pop, Replace and its stack drain); `prepareForSleep` takes it too; `handleHomeGesture` runs unlocked before `goHome`; a push gives the covered screen no call. The mutex is non-recursive.
- `lib/GameDisplayLab/SlotScheduler.h`, `GraySlot.h` -- reuse unchanged. A scan callback returning false makes `runGraySlot` skip the follow-up and commit only what was delivered.
- `test/game_display_lab/CMakeLists.txt` -- add the new test file to `test_scan_lut`.
- `lib/I18n/translations/english.yaml` -- `STR_GAMES_*` keys (ledger row 2).
- `docs/crosshatch/display-lab.md` -- "Transition test" section, memory paragraph, code map.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/SweepControl.h` -- new header-only, std-only class. One `std::atomic<uint32_t>` word with `Stop`, `Run`, `Replay`, `Clean`, `InPanel` bits, plus an atomic heartbeat (µs). Owner side: `reset(nowUs)` sets Run|Replay and the heartbeat; `heartbeat(nowUs)`; `heartbeatAgeUs(nowUs)`, wrap-safe; `requestRun(bool)`; `requestReplay()` sets Replay|Run; `requestClean()` sets Clean and clears Run, as one CAS; `requestStop()`; `runRequested()`; `insidePanel()`. Task side: `take()` returns `{stop, run, replay, clean}` and clears Replay|Clean with one `fetch_and`. `enterPanel(nowUs, gapUs)` returns `Gate::{Enter, Stop, Covered}`: Stop when the Stop bit is set, Covered when the heartbeat age is over `gapUs`, otherwise Enter, setting InPanel by CAS so a concurrent Stop is never missed. `leavePanel()`. -- the pure request/state protocol (D11's request word).
- [x] `test/game_display_lab/test_sweep_control.cpp` + `CMakeLists.txt` -- pin: reset state; take clears only Replay and Clean; coalescing (pause twice is a no-op; replay then clean leaves run off; clean then replay leaves it on); Stop is sticky and gates `enterPanel`; Enter sets and `leavePanel` clears InPanel; a Stop while inside keeps InPanel until leave; Covered at age > gap, Enter at age == gap; heartbeat wrap across `UINT32_MAX`; a two-thread run where a looping enter/leave thread sees Stop and ends outside the panel.
- [x] `src/activities/games/display_lab/TransitionTestActivity.h` -- constants `SWEEP_STACK_BYTES` (measured, see Verification), `SWEEP_PRIORITY = 1`, `SWEEP_CORE = 1`, `STOP_POLL_MS = 5`, `SWEEP_STACK_MARGIN_BYTES = 1024`. Members `TaskHandle_t sweepTask`, `std::atomic<bool> sweepDone`, `SweepControl control`, `enum class SweepEnd : uint8_t { Stopped, Covered, ScanFailed }` with the failing `Scan`, the last reported figures (tenths, ms per scan, `bool`). Overrides `handleHomeGesture` and `prepareForSleep`; `skipLoopDelay`/`preventAutoSleep` return `sweepTask && control.runRequested()`. Rewrite the class comment.
- [x] `src/activities/games/display_lab/TransitionTestActivity.cpp` -- the loop and task code as Design Notes say: `startRun` creates the task; `sweepEntry`/`sweepRun` run on it; `stopSweep()` is the unlocked join; `endSweepLocked()` is the locked stop; `leaveView` writes the Run and Stats rows after the join.
- [x] `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_NO_TASK: "Stopped: not enough memory"` beside `STR_GAMES_DLAB_STOPPED`.
- [x] `docs/crosshatch/display-lab.md` -- the sweep runs on its own task: controls answer while scans run; pause, replay and clean take effect at the end of the slot in flight, Back after the scan in flight; covered behaviour; the task stack in internal RAM while a run is up; `SweepControl.h` in the code map.

**Acceptance Criteria:**
- Given the host suite, when it runs, then every existing test passes unchanged and `test_sweep_control.cpp` passes.
- Given a run in progress, when any exit path runs (Back, Home, sleep, cover then Home or sleep, cover then close), then no buffer is freed while the task can still touch it, and no main-task wait under the render lock depends on the task taking that lock.
- Given the x4pro build, when a run ends, then the log has one `LOG_INF` with the task's stack size and its high-water mark, and `LOG_ERR` when the free bytes fall under `SWEEP_STACK_MARGIN_BYTES`.
- Given `default` and `sticky`, when built, then they compile with the lab out, and the flash gate's games delta is unchanged from 8874e2ce, measured the same way at both commits.

## Implementation Notes

Built 2026-10-16 in the worktree `/home/user/crosshatch-lanec2` (branch `dlab-lane-c2`, base 8874e2ce), staged, not
committed (the orchestrator runs the review lenses on the staged diff).

**Protocol between `loop()` and the sweep task** (`lib/GameDisplayLab/SweepControl.h`): one atomic word (Stop, Run,
Replay, Clean, InPanel) and an atomic heartbeat. Controls write requests and `xTaskNotifyGive`; the task `take()`s
them at each slot boundary (Replay and Clean one-shot, Run a state, Stop sticky). The task brackets every scan and
clean with `enterPanel` (Stop, Covered when the heartbeat is older than `COVERED_GAP_US`, else InPanel by CAS) and
`leavePanel`. The task publishes `sweepDone` (release) and parks in `vTaskSuspend`; only the screen deletes it.
`stopSweep` (from `loop()`: Back, cover, the task's own end, the task gone after sleep; and `handleHomeGesture`) joins
by polling `sweepDone` every 5 ms. `endSweepLocked` (from `onExit`, `prepareForSleep`) polls each tick and deletes the
task at once when `insidePanel() && eTaskGetState == eBlocked`. The task keeps the slot runner; `leaveView` writes
the Run and Stats rows under `RenderLock` from the task's last figures, after the join.

Deviations, all small:
- Every word change is one `compare_exchange_weak` loop (`change()`), not `fetch_or`/`fetch_and`: on the S3 those
  two are libcalls the linker puts in IRAM (+206 B `.iram0.text`, +48 B `.iram0.text_end` in the plain x4pro
  image, measured), while `__atomic_compare_exchange_4` is already linked. Same semantics.
- `heartbeatAgeUs` returns 0 when the heartbeat is newer than the given clock (the task reads `micros()` before the
  owner's latest heartbeat), so a race never reads as a huge age and a false Covered; pinned by a test.
- The stack-floor idiom: `pxTaskGetStackStart` is kept, and the scan lambda records the least room above it before
  a scan; the run's `LOG_INF` gives stack size, high-water free bytes and that figure, `LOG_ERR` under the margin.
- Back from the view joins and then shows `endText()` (a scan failure that landed just before Back keeps its text).
- Resume after a clean, a pause or a replay restarts the scheduler and the speed window, as c1.

**Stack measurement** (`SWEEP_STACK_BYTES`). The chain's TUs (the screen, `DisplayLabActivity`,
`Ssd1677ScanBackend`, `GfxRenderer`, `HalDisplay`, `Logging`, the SDK's `FreeInkDisplay`, `EpdBus`, the SSD1677,
UC8179 and UC8279 drivers, `GrayModel`, `TransitionScene`, `MaskPlanes`) recompiled from `compile_commands.json`
with `-fstack-usage -fcallgraph-info=su`, virtual calls joined by hand; frames of prebuilt code (newlib, Arduino
core, FreeRTOS) read from the x4pro ELF's `entry a1, N`. Frames: `sweepEntry` 32, `sweepRun` 48, `runSlot` 80, the
scan lambda 48, `DisplayLabActivity::scan` 48, `Ssd1677ScanBackend::scan` 160 (416 to here); scan into the SDK:
`FreeInkDisplay` 32-64, `Ssd1677Driver::displayGray` 32, `refresh` 48, `EpdBus::waitRefreshComplete`/`waitBusy` x2
96 (656), then its `Serial.printf`: `Print::printf` 80, `Print::vprintf` 144, `vsnprintf` 64, `_vsnprintf_r` 144,
`_svfprintf_r` 800, `_dtoa_r` chain 256, `_calloc_r`/heap chain about 320: **about 2,464 B**. Other chains are
shallower: `logPrintf` (336) from the backend about 2,316 B, the clean path through `GfxRenderer::displayBuffer`'s log
about 2,204 B, the SPI writes (`copyGrayscaleMsb` 320 with `EpdBus`) about 1,000 B. Plus 512 B allowed for an
interrupt's exception frame and the coprocessor save area: 2,976 B. +2 KiB = 5,024 B, rounded up to 5 KiB; set to
**8,192 (6,144 before the first review round) B** (8 KiB) because the heap and SPI-lock externals are read by hand, not measured. The realistic peak is
lower (no float formats reach `_dtoa_r`). The UltraChip lab backends on the experimental branch run deeper SDK
sessions on this task: their first hardware run's HWM line decides whether 8 KiB holds there.

**Results** (all with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache` and the locks):
- Host suite: 2,193 tests, all pass (12 new, then 14 after the two review rounds `SweepControl.*` in `test_scan_lut`).
- `scripts/check_layers.py`: passed. `scripts/check_upstream_touches.py`: PASS (it checks HEAD; the staged diff
  touches only `english.yaml` among upstream files, ledger row 2).
- `./bin/clang-format-fix`: no changes; second run no changes.
- `pio run -e x4pro`, `-e default`, `-e sticky`: success. `pio check -e x4pro` and `-e default` with the three
  `--fail-on-defect`: no defects.
- Plain x4pro image against 8874e2ce (both built in this worktree, same way): Flash 6,249,106 -> 6,250,962 B
  (+1,856); `firmware.bin` 6,254,176 -> 6,256,032 B (+1,856); RAM 104,232 -> 104,232 B (+0); `size -A`
  `.dram0.data`, `.dram0.bss`, `.noinit`, `.iram0.*` all unchanged. The task's 8 KiB stack is heap, only during a run.
- Flash gate (`build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`): on 6,218,576 B,
  off 5,960,384 B, games delta +258,192 B (18,288 B to spare), static RAM +784 B (240 B to spare), objects clean:
  identical to the 8874e2ce base measured the same way (+258,192 B, +784 B), so the games delta moved by +0.
- Incident: the second x4pro build found PlatformIO reinstalling `framework-arduinoespressif32` 3.3.11 and its
  `-libs` (cause not found; another session shares `~/.platformio`), and the disk filled (about 2 GB free of the
  quota): `-libs/esp32c6` was left with 3,948 of 5,972 files and one truncated header. Repaired from the cached
  archive (`~/.platformio/.cache/downloads/aa51c55d...`), compared file by file: identical. The S3 and C3 trees
  matched the archive's counts throughout; no env here builds a C6.

## Plan Change Log

## Review Triage Log

### 2026-10-16 — Review pass (orchestrator-run lenses: the session's subagents could not nest, so the orchestrator launched the four lenses itself and triaged, per orchestrated-epics.md § 3's fallback)
- verdicts: 29 findings — high 0, medium 5, low 13, false 0, maybe-false 0, rejected-by-rule 11
- findings:
  - `[medium]` `[patch]` (edge E1) loop() kept handling input in the pass where the sweep ended, so a Back/Confirm release popped the screen or restarted Run — `return` after `leaveView`.
  - `[low]` `[reject]` (edge E2) heartbeat age wraps to 0 after a 2^31 µs covered pause — needs a 36-minute cover of a paused view; the fix adds state; the user presses Back.
  - `[medium]` `[patch]` (edge E3, verification-gap other, edge claim C3, blind 2: one root cause) endSweepLocked could delete a task blocked in DisplayLabActivity::scan's pre-lock LOG_ERR (HWCDC tx_lock), not on the render mutex — the scan lambda pre-checks the three refusals before enterPanel and logs outside the panel; the comment names the window left (an inversion between that check and scan's own) and the SWEEP_CORE reliance.
  - `[low]` `[patch]` (edge E4) a gate-refused scan was counted in the speed stats — only delivered scans are recorded.
  - `[low]` `[patch]` (edge claim C1, blind 3) the delete-while-waiting path logged no stack line — it logs uxTaskGetStackHighWaterMark before vTaskDelete.
  - `[low]` `[reject]` (edge claim C2) the plan says take() uses one fetch_and; the code uses the CAS loop, equivalent — a plan-wording fix, rejected by rule.
  - `[medium]` `[patch]` (verification-gap) requestRun(false), the pause, was observed by no test (mutation passed all 11) — RequestRunFalsePausesAndTrueResumes added.
  - `[medium]` `[patch]` (blind 1) COVERED_GAP_US 1 s was sized for the inline loop; up to 1 s of scans landed under a covering screen — 300 ms, doc updated.
  - `[low]` `[patch]` (blind 4) the stack comment's arithmetic (5 KiB) did not match 6144, and the UltraChip session was unmeasured although the owner's unit is a UC8179 — 8192 with a comment that adds up.
  - `[low]` `[patch]` (blind 5) skipLoopDelay true spun the main loop against the task on the same core and priority — returns false; preventAutoSleep keeps the clock up.
  - `[medium]` `[defer]` (blind 6) serve()'s decision table (clean once, clock to 0 on replay, run from the latest press, pause before clean) has no host test; extracting it into a pure helper is a design change — deferred.
  - `[low]` `[patch]` (blind 7) Back's latency doc omitted the clean in flight — doc, class comment and stopSweep comment updated.
  - `[low]` `[patch]` (blind 8) the new log lines were undocumented — listed in display-lab.md.
  - `[medium]` `[defer]` (blind 9) neither join has a time limit, and endSweepLocked waits under the render mutex — a wedged BUSY wait freezes the device; a timed join with abandon rides the SDK's unbounded-BUSY deferral (dlab-b2) — deferred.
  - `[low]` `[patch]` (blind 10) the Speed row could show the previous run's figures when a run ended before its first report — figures reset per run.
  - `[low]` `[reject]` (intent 1) pause takes effect at the slot, not the scan — intended: a slot's follow-up belongs with its drive (InkBoy's hold pulse); the doc says so.
  - `[low]` `[reject]` (intent 2) the Stats row is written once at view end — the row is hidden while the view is up (D4); the log keeps its cadence.
  - `[low]` `[reject]` (intent 3) "joined" on onExit/sleep is a delete-in-place when blocked on the mutex — the plan's Design Notes chose it over a deadlock; the pre-lock window is the E3 patch above.
  - `[low]` `[reject]` (intent 4) covered-screen scans may land during the detection window — the heartbeat detector is the equivalent the story allows; the window is now 300 ms (blind 1).
  - `[low]` `[reject]` (intent 5) Back shows endText() rather than always "Stopped", and waits for the scan in flight — a failed scan's outcome is the truer text; the wait is the inline design's and is documented.
  - `[low]` `[reject]` (intent 6) the stack floor is logged, not enforced while running — GameVM's enforcement serves scripts of unknown depth; here the chain is fixed and the LOG_ERR threshold reports a shortfall.
  - `[low]` `[reject]` (intent 7) the UC8179 backend is not on this branch — true by design (experimental branch); the stack headroom patch (blind 4) covers its session until the first hardware line.
  - `[low]` `[reject]` (intent 8) responsiveness is device-level and host tests cover SweepControl only — the story says so; the owner's run verifies it.
  - `[low]` `[reject]` (intent 9) the device-only statement lives in the plan and the doc code map — where the story asks for it.
  - `[low]` `[reject]` (intent 10) a new STR_GAMES_DLAB_NO_TASK rather than reusing STR_GAMES_OUT_OF_MEMORY — the story asks for "Stopped" with the reason in one row line; english.yaml is ledger row 2.
  - `[low]` `[reject]` (intent, build claims) "default and sticky still build" and "no local over 256 B" are claimed only in the plan — the orchestrator's verification run below is the evidence.

### 2026-10-16 — Follow-up review pass (same four lenses, orchestrator-run, over commit 1d873a0c)
- verdicts: 36 findings — high 0, medium 6, low 15, false 0, maybe-false 0, rejected-by-rule 15
- findings:
  - `[medium]` `[patch]` (blind 1, edge claim) the round-1 stack LOG_INF sat between endSweepLocked's eBlocked check and vTaskDelete; a log can block on the serial lock and yield, so the task could be deleted holding it and the caller's next log hang under the render mutex (graded medium: dev-only bench, needs the pre-lock race and a blocked serial write at once) — HWM into a local, vTaskSuspend then vTaskDelete with no call between, logging after; one `logStackEnd` helper serves both ends.
  - `[low]` `[patch]` (blind 2, edge claim C4) the delete path had no stack-margin LOG_ERR — the shared helper has it.
  - `[medium]` `[patch]` (blind 3, edge F5) busy time of TimedOut/Dropped scans was spread over the Done count — busy summed for Done scans only.
  - `[medium]` `[patch]` (blind 4, edge F4) a final report over an empty window overwrote a real speed; the cap path could report twice — figures kept only when the window had a delivered scan; `sweeping = false` before a refused cap clean returns.
  - `[low]` `[patch]` (blind 9) statsReported was set true in startRun and never cleared — reset per run, set by a non-empty report.
  - `[low]` `[patch]` (blind 5) the lambda's refusal message hid which check failed — one reason per check, with rateCount().
  - `[medium]` `[defer]` (blind 6, edge F2, intent I4/I5) cover detection is a heartbeat timing: scans continue for up to 300 ms plus the scan in flight under a cover, and a main-loop stall over 300 ms reads as a cover — a deterministic signal from ActivityManager's push path is a design change outside this story; the doc states both; deferred.
  - `[low]` `[patch]` (blind 6, part) a covered run showed "Stopped" — STR_GAMES_DLAB_COVERED, also when the loop sees the cover first.
  - `[low]` `[reject]` (blind 7, intent I12) the stack comment cites plan notes that said 8,192 (6,144 before the first review round) — the orchestrator corrected the plan's Implementation Notes and Results (a plan edit, outside the code review).
  - `[low]` `[patch]` (blind 8) the stopSweep header comment broke mid-sentence — reflowed.
  - `[medium]` `[patch]` (blind 10, verification-gap 1) the threaded handshake test caught a non-atomic enterPanel about 1 run in 6 and spun without a deadline — 200 rounds with a 10 s deadline that fails instead of hanging; "Previous then Confirm" and stopRequested tests added.
  - `[low]` `[patch]` (blind 11) the loop task's cover line was undocumented — the log list is grouped by task.
  - `[low]` `[defer]` (edge F1, edge claim C3, intent I9) the pre-lock race left (an inversion between the lambda's pre-check and scan's own) could only be closed by a flag set inside DisplayLabActivity::scan, which the story forbids changing — deferred (low).
  - `[low]` `[reject]` (edge F3) heartbeat wrap after a 36-minute covered pause — carried from round 1; rejected for the same reason.
  - `[medium]` `[patch]` (edge F6) Stop requested while inverted ran the refusal pre-check before the Stop gate, so Back showed "Refused" — SweepControl::stopRequested() checked first, pinned by a test.
  - `[low]` `[reject]` (edge claims C1, C2) take() uses a CAS loop not fetch_and; skipLoopDelay returns false unlike the plan's task text — plan wording; the triage log records both deviations.
  - `[medium]` `[defer]` (verification-gap 2) the delivered-scan count the screen passes to record() has no test — device-only, closes with serve()'s deferred extraction; deferred.
  - `[low]` `[reject]` (intent I1, I2, I3, I6, I7, I8, I10, I11) the UC8179 backend is on the experimental branch; the UltraChip stack figure is a provision until the first hardware line; the stack floor is log-only and the joins unbounded (deferred in round 1); the refusal pre-check duplicates scan's three checks by design (no log inside the panel); slot-boundary pause, the clean-in-flight wait on Back, preventAutoSleep false while paused and the Stats row written at view end are the settled readings — all recorded; no code change.

## Design Notes

**Sources.** D11 (epic, 2026-10-16) names the task and supersedes, for this screen only, the Boundaries line "never a background FreeRTOS task". The story's Approach and I/O rows set the behaviour. c1's Design Notes and its review patch (light-panel cover) give the c1 behaviour that is kept.

**The deadlock this design avoids.** `DisplayLabActivity::scan` takes the non-recursive render mutex itself, and `onExit` and `prepareForSleep` are called with the main task already holding it. A plain join there deadlocks when the task is waiting for that mutex, so:
- *Unlocked join* (`stopSweep`): from `loop()` (Back, cover detected, the task ended on its own, the task gone after `prepareForSleep`) and from `handleHomeGesture`. It calls `requestStop()` and notifies, polls `sweepDone` every `STOP_POLL_MS`, then `vTaskDelete`. Its wait is bounded by the scan in flight, as c1's inline scans were.
- *Locked stop* (`endSweepLocked`): from `onExit` and `prepareForSleep`. It calls `requestStop()` and notifies, then loops: done → delete; `insidePanel() && eTaskGetState(task) == eBlocked` → the task waits for the mutex the caller holds (the only blocking step between `enterPanel` and `RenderLock`), holds nothing, and is deleted (GameVM::abandon's precedent), with a `LOG_INF` and main's own `logStats`; else `vTaskDelay(1)`. Outside the panel the task sees Stop before its next panel entry and ends without the lock.
- The task never deletes itself. After publishing `sweepDone` (release) it parks in `vTaskSuspend(nullptr)`, and only the owner deletes it, so notifications never reach a freed task without GameVM's `taskMutex`.
- Residual: a refusal `LOG_ERR` inside `scan` before its lock, blocked in the USB-CDC write while holding its `tx_lock`, would leak that lock and silence logging. Never a hang.

**Task loop (`sweepRun`).**
1. Floor = `pxTaskGetStackStart(nullptr)`.
2. Loop:
   - `take()`. On Stop, end with Stopped.
   - On a pause transition: advance the clock, `reportStats`, stop the scheduler, log "paused at".
   - On Replay: gated clean, then elapsed 0, `applyAll(0)`, and c1's start log line. Else on Clean: gated clean, scene stale.
   - When running after a clean, or on a resume: `clockUs = now`, `scheduler.start(now)`, restart the stats window, log "resumed" on the transition. A clean is a pause of the clock, as in c1.
   - Paused: `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`. Not due: wait with a timeout of the ticks to `nextUs()`, at least 1. Otherwise `runSlot`.
3. Before returning, report the stats if running, log the HWM, and `LOG_ERR` under the margin.

`runSlot` is c1's body: the cap clean runs through the gate, and the scan lambda calls `enterPanel(micros(), COVERED_GAP_US)` and `leavePanel()` around `lab.scan`. A refused gate returns false, recording Stop or Covered, which ends the run after `runGraySlot` commits what was delivered. A gated clean is `enterPanel`, then `{RenderLock; clearScreen(0xFF); lab.cleanNow();}`, then `leavePanel`, then `model.reset(W)`.

Pause, replay and clean are served at slot boundaries, not between a slot's two scans. c1 keeps a clean out of a slot's pair (cap-clean comment), and `GraySlot.h` makes the follow-up the HOLD-ending scan. "Pauses within a slot" (story, Hardware) is met. Stop and Covered cut the slot.

**Loop task.**
- `loop()`: a cover gap (`heartbeatAgeUs > COVERED_GAP_US` while shown) → log, `stopSweep`, `leaveView(Stopped)`. Shown with no task, or `sweepDone` → `stopSweep`, `leaveView` with the end text (`SweepEnd` switch, no default: Stopped/Covered → "Stopped", ScanFailed → `outcomeText`). Then `UiListActivity::loop()`, then `control.heartbeat(micros())`, which replaces `lastLoopUs`.
- `startRun` (guard: no task): `control.reset(now)`; `shownOnPanel = true` under `RenderLock` (no task alive yet, as c1); create the task. On failure, revert the flag under the lock, set `status` to NO_TASK, rebuild the rows and `requestUpdate`.
- Controls write requests and `xTaskNotifyGive`.
- `leaveView` runs only after the join. It formats `statsText` from the task's last figures under `RenderLock`. The rows are not drawn while the view is up (`render()` returns early), so this is when the Stats row is "updated under RenderLock".

**Guards kept** from c1:
- `render`, `navigateButtons`, `handleCustomInput`: draw nothing and move nothing while shown. Swipes are consumed.
- `activateIndex`: a tap while shown toggles the pause; Run only from the Run row and with no notice.
- `onExit`: FULL promoted when shown (its first step is now `endSweepLocked`).
- `leaveView`: FULL promoted before the flag clears.
- `runSlot`: cap clean when `scansBeforeClean() < SCANS_PER_SLOT`; a non-Done scan ends the run.
- `handleForcedRefresh`: swallowed while shown.

**Device-only:** task create, notify and delete, `eTaskGetState`, the HWM log, `RenderLock`, input, the panel. Host-tested: `SweepControl` only. The screen compiles only under `CROSSHATCH_DISPLAY_LAB` (x4pro), and the simulator compiles it out.

## Verification

**Commands:** (each with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache` and AGENTS.md's locks)
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` under the host-test lock -- expected: all pass, the existing count plus the new tests.
- `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py`, `./bin/clang-format-fix` -- expected: clean; `git status` shows nothing new on a second run.
- After review: `pio run -e x4pro`, `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro` and `pio check -e default` with `--fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: success, no defects.
- Flash gate (`build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`) at 8874e2ce and at the commit -- expected: same games delta (+0) and RAM delta.
- Plain x4pro image: `firmware.bin` size and `size -A` static RAM at 8874e2ce (built before any edit) and at the commit -- record the delta.
- Stack: recompile the chain's TUs from `compile_commands.json` with `-fstack-usage`. Sum the deepest frames: `sweepEntry` → `sweepRun` → `runSlot`/lambda → `DisplayLabActivity::scan` → `Ssd1677ScanBackend::scan` → SDK facade, and → `cleanNow` → backend `clean` → `GfxRenderer::displayBuffer` → SDK, plus the `LOG_*` path. Set `SWEEP_STACK_BYTES` to that sum plus at least 2 KiB, rounded to 1 KiB, minimum 4 KiB. Record the figures.

**Manual checks (if no CLI):**
- Simulator: the lab is compiled out (no `HalDisplay::eink()`), so no screenshot.
- Hardware (owner): a short Confirm press pauses within a slot; Back stops at once; `ms_per_scan` unchanged from c1; the per-run HWM line gives the measured high-water mark.

## Auto Run Result

**Summary.** The transition test's sweep runs on its own FreeRTOS task (`DlabSweep`, priority 1, core 1, 8 KiB
stack); the main loop keeps polling input and forwards the controls through `GameDisplayLab::SweepControl` (an
atomic request word plus a heartbeat), which the task reads at slot boundaries and before every scan or clean. The
screen's loop applies the visible result when the task reports it. The locked exits (`onExit`, `prepareForSleep`)
stop the task without a join and delete it when it is blocked waiting for the render mutex the caller holds; the
unlocked exits join it.

**Files.**
- `lib/GameDisplayLab/SweepControl.h` (new): the request word (Stop, Run, Replay, Clean, InPanel), the heartbeat and
  the panel gate; header-only, host-tested.
- `test/game_display_lab/test_sweep_control.cpp` (new, 12 tests), `test/game_display_lab/CMakeLists.txt`.
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}`: the task, its entry and run loop, the gate around
  each scan and clean, `stopSweep` / `endSweepLocked`, `handleHomeGesture` and `prepareForSleep` overrides, the stack
  measurement line.
- `lib/I18n/translations/english.yaml`: `STR_GAMES_DLAB_NO_TASK`.
- `docs/crosshatch/display-lab.md`: the task, its controls' timing, the new log lines, the code map.

**Review.** Four lenses, launched by the orchestrator (the session's subagents could not nest): 29 findings, 10
patched (5 medium at entry verdict: the end-of-sweep input pass, the pre-lock delete window, the untested pause, the
covered gap, and the counted refused scan grouped with the stats reset), 2 deferred (serve()'s decision table
untested; unbounded joins), 11 rejected with reasons in the triage log, 0 false. Follow-up review recommended:
`true`: two medium entries were patched (the pre-lock delete window and the covered gap are the unverified risks,
both device-only).

**Verification (orchestrator, on the patched tree).** Host suite 2,194/2,194; `check_layers.py` and
`check_upstream_touches.py` pass; `clang-format-fix` twice, clean. `pio run -e x4pro` 6,251,318 B flash
(+2,212 B over 8874e2ce's 6,249,106 B), 104,232 B RAM (+0); `default` and `sticky` build; `pio check -e x4pro` no
defects; flash gate (D6): games delta +258,192 B (+0 against the dlab-c1 base, 18,288 B to spare), static RAM
+784 B (240 B to spare), `objects` clean. The simulator compiles the lab out, so no screenshot.

**Follow-up pass (same four lenses over 1d873a0c).** 36 findings, 9 patched (the delete path's log ordering,
the Stop-before-refusal order, the busy sum of undelivered scans, the empty-window report and the stats flag, the
refusal log's reason, the Covered row text, a comment, the handshake test's 200 rounds and deadline plus two new
tests, the doc's log list), 3 more deferred (a deterministic cover signal, the delivered-scan count's test, the
pre-lock race), the rest rejected with reasons. No high finding was patched, so `followup_review_recommended` is
false. Verification repeated on the follow-up tree: host suite 2,196/2,196; layer and ledger checks pass; x4pro
6,251,426 B flash (+2,320 B over 8874e2ce), 104,232 B RAM (+0); default and sticky build; pio check x4pro clean;
flash gate games delta +258,192 B (+0), static RAM +784 B (240 B to spare), objects clean.

**Residual risks.** The stack's UltraChip figure is a provision, not a measurement, until the first UC8179 run's
`gray sweep task ended` line; the delete-while-waiting path's remaining window (an inversion between the lambda's
pre-check and `scan()`'s own) is device-only; input responsiveness itself is verified only on hardware.

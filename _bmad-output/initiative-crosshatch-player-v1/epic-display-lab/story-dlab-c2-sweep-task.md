# Story dlab-c2: the transition test's sweep on its own task (lane C, epic branch)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R3, R4,
Boundaries, Decisions D4, D5, D8 and D11; the Story results for dlab-c1, dlab-a2 and the hardware report that
motivates this story). Ref: `dlab-c2`. Plan: `_bmad-output/implementation-artifacts/plan-dlab-c2-sweep-task.md`.
Builds on `dlab-c1` (`TransitionTestActivity`, `GraySlot.h`, `SlotScheduler.h`) and on the orchestrator's
`fix:` commit that precedes this story (the follow-up scan is skipped when its masks are empty; the entry warning
fits the dialog).

## Intent

**Problem:** The owner ran the transition test on a UC8179 and found its controls laggy to non-responsive. The
firmware reads buttons and touch only from the main loop (`main.cpp` → `mappedInputManager.update()` →
`UiListActivity::loop()`), and `TransitionTestActivity::loop()` runs each slot's scans inline. On an UltraChip
controller one scan is a whole session over the SDK seam (D8): two full-screen planes streamed over SPI (about 96 KB),
the activation and its BUSY wait, then the register restore, all blocking inside the SDK. A slot of one or two such
scans blocks the loop for hundreds of milliseconds, so input is sampled once per slot, and a press shorter than that
is never seen (`wasPressed`/`wasReleased` need the edge across two `gpio.update()` samples).

**Approach:** Run the sweep's slots on a dedicated FreeRTOS task (the "sweep task"), as `GameVM` runs scripts
(`src/games/GameVM.{h,cpp}`: `xTaskCreatePinnedToCore`, `TASK_PRIORITY 1`, `TASK_CORE 1`, `taskEntry`, a joined
stop in the destructor path; copy its idioms, including the stack-floor check). The main loop keeps polling input;
`loop()` only reads the task's state and forwards the controls to it. The panel is the sweep task's alone while the
test view is up: `shownOnPanel` already makes `render()` draw nothing, and every scan already runs under
`RenderLock` inside `DisplayLabActivity::scan`, so the task takes that lock per scan as the loop did. The slot
clock, the scheduler and the stats move with the slot runner onto the task. Controls (pause/resume, replay, clean,
stop) become requests the task honours between scans (an atomic request word, or a FreeRTOS notification), and the
screen's loop applies the visible result (row text, the FULL-refresh promotion, `requestUpdate`) when the task
reports it, so no UI call happens on the task. Leaving the screen (Back, Home, sleep, a covering screen) stops and
joins the task before `onExit` frees the model and masks; `onExit` still takes no `RenderLock` (12cc816).

## Boundaries & Constraints

**Always:** the engine's pure parts (`GrayModel`, `GraySlot.h`, `SlotScheduler`, `TransitionScene`, `GrayPreset`)
unchanged in behaviour (their tests still pass as they are, plus whatever the new seam needs); every scan still goes
through `DisplayLabActivity::scan` (timeout, cap, dropped, inverted) under `RenderLock`; D4 (no drawing during a
sweep) and D5 (the slot period) unchanged; the task's stack sized from a measurement (log the high-water mark once
per run at `LOG_INF`, as GameVM does) and no local over 256 B on it; the task is created in `startRun` (or the first
run) and deleted before `onExit` returns, never left running across screens; `preventAutoSleep()` stays true while
running; the covered-screen detection (`COVERED_GAP_US`) keeps working or is replaced by something equivalent that
the task cannot defeat; a failed task creation logs and shows the Run row as "Stopped" with the reason (`tr`), never
a half-started sweep; C3 `default` and `sticky` still build (the whole screen is behind `CROSSHATCH_DISPLAY_LAB`);
the host suite gains a test for the request/state protocol's pure part (the request word's transitions) if one is
extracted, and the plan says what the device-only part is.

**Never:** no change to `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam or `freeink-sdk/`;
no second scan path around `DisplayLabActivity::scan`; no `Serial`, no bare `new`; no new ledger row; no edit of
generated I18n files; no change to the stock bench or the frame-period and polarity screens.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Run | Run row (or NavNext) | task created, clean to white, sweep starts; `loop()` returns within one input poll period while scans run | task creation fails: logged, Run row "Stopped" with the OOM text, no scan sent |
| Pause / resume | Confirm or a tap while running | the task finishes the scan in flight, then idles with the clock stopped; resume continues at the paused clock; both register from a single short press | — |
| Replay | NavNext while the view is up | clean to white, sweep from 0, on the task | — |
| Clean | NavPrevious | clean to white, paused | — |
| Stop | Back while the view is up | the task finishes the scan in flight and exits; the rows return with one FULL refresh; "Stopped" | — |
| Leave | Back from the rows, Home, sleep | the task is joined before `onExit` frees anything; one FULL refresh | — |
| Scan failure | a scan returns TimedOut/Dropped/Refused on the task | the sweep ends, the rows show the outcome text, as today | — |
| Session cap | `scansBeforeClean() < SCANS_PER_SLOT` on the task | cleans first, clock and windows restart, as today | — |
| Covered | another screen pushed over the running test | the sweep stops (task joined) and the view ends with a FULL refresh | — |
| Stats | every `STATS_EVERY_US` | the `gray scans_s=... ms_per_scan=...` log line from the task; the Stats row text updated under `RenderLock` | — |

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`,
  `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro`; the flash-budget gate with D6 (the lab is excluded,
  so expect +0 against the dlab-c1 base; record the plain x4pro image's delta too).
- Simulator: the lab is absent there (no `HalDisplay::eink()`), so no screenshot; say so in the plan.
- Hardware (owner): a short press of Confirm pauses within a slot; Back stops at once; the Stats line's
  `ms_per_scan` is unchanged from before this story (the task adds no per-scan cost).

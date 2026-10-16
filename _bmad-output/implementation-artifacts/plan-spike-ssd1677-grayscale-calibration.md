---
title: 'Spike: SSD1677 scan primitive and frame-period lab on the X4 Pro'
type: 'feature'
ticket: ''
created: '2026-10-16'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '61623734ca4093590bb256eae294ae94d1d525c4'
context:
  - '{project-root}/docs/contributing/touch-and-ui.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The research run recommends building InkBoy's per-pixel grayscale engine for the X4 Pro's SSD1677 batch, but the firmware has no way to drive a caller-supplied one-frame waveform with per-pixel hold/black/white masks, and two numbers only a panel can give are unknown: the SSD1677 frame period (one "dose" is one one-frame activation) and the drive polarity of the two LUT rows.

**Approach:** A fork-only, X4 Pro-only, SSD1677-only "Display lab" reachable from the Games launcher, with a scan primitive (caller LUT via 0x32, raw masks into both RAM planes, activation with 0x22 = 0xCC, timed BUSY wait) built on the SDK facade's existing overlay-grayscale path, a frame-period screen that runs nine timed all-hold scans, and a polarity screen that drives the whole panel with one LUT row so the user can see which row darkens. Safety first: stock voltages, never the OTP command, entry warning, session cap, cleaning refresh on exit. The transition-test instrument is Part B, deferred.

**Decisions:**
- HAL seam, option (a): one `#if FREEINK_CAP_GAMES`-guarded accessor `EInkDisplay& eink()` in `lib/hal/HalDisplay.h`, with a new ledger row 13 and an architecture spine note under AD-3. Nothing else in `lib/hal` changes.
- Split: the per-pixel model, transition test, tuning list and SD preset are deferred (deferred-work.md) until this spike's measurements exist.
- Scan timeout 15,000 ms (human decision, review pass 1, 2026-10-16), not 5,000 ms.
- Dev environment only (human decision, review pass 1, 2026-10-16): the lab compiles only when a dedicated `FREEINK_CAP_DISPLAY_LAB` build flag is set, and that flag is set in the `x4pro` development env alone; the `x4pro-gh_release` and `x4pro-gh_release_rc` envs never carry the bench. Ledger row 1's Change text grows by that one flag, under the same spine amendment.

## Boundaries & Constraints

**Always:** All new code under `src/activities/games/display_lab/` and `lib/GameDisplayLab/` (ledger fork-only globs), compiled only under `FREEINK_CAP_GAMES && FREEINK_DEVICE_X4PRO && FREEINK_CAP_DISPLAY_LAB` (the last set in the `x4pro` dev env only), and gated at runtime on `BoardConfig::ACTIVE.displayController == SSD1677` on an X4 Pro; the C3 `default` env must still build. LUT bytes 105-109 (VGH, VSH1, VSH2, VSL, VCOM) stay the panel's stock values (0x17 0x41 0xA8 0x32 0x30). Every scan goes through the facade's `copyGrayscaleBuffers(black, white)` then `displayGrayBuffer(turnOff=false, lut, factoryMode=true)`; the lab exits with `displayBuffer(FULL_REFRESH)`. Mask planes live in PSRAM via `HalMemory::allocatePsram`, allocated in `onEnter` and freed in `onExit`; locals under 256 bytes; `makeUniqueNoThrow` only; `LOG_INF("DLAB", ...)` for every measurement; `tr(STR_GAMES_DLAB_*)` for text. The LUT builder (`lib/GameDisplayLab`) is pure C++ with no Arduino includes and has a host GoogleTest suite.

**Never:** No edit to `freeink-sdk/`, `.agents/`, `CrossPointSettings.h`, `SettingsList.h`, or any upstream file other than the HAL accessor, the ledger, the yaml keys and the test CMake the ledger already allows; no `#define private`; no command 0x30; no voltage changes; no windowed activation; no background FreeRTOS task; no per-frame heap allocation; no change to the stock refresh paths or the reader; no per-pixel model or dose table (Part B).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Launcher row | X4 Pro, controller SSD1677 | "Display lab" row shown | No error expected |
| Launcher row | UC8179/UC8279 unit, or not X4 Pro | Row absent | No scan is ever sent |
| Enter lab | Confirm on the warning screen | Lab menu: Frame period, Polarity, Exit | Back on the warning returns without touching the panel |
| Frame period | TP[0A] in {1,10,100} x frame-rate bytes {0x22, 0x11, 0x44}, all-hold masks | Nine scans; per scan `LOG_INF("DLAB","tp=%u fr=0x%02x busy_us=%lu")`; table on screen | A scan over 15,000 ms (human decision, review pass 1: 5,000 ms could reject a legitimate tp=100 scan at a slow frame period) ends the run, logs `timeout`, and runs FULL_REFRESH |
| Polarity | row choice LUT1 or LUT2, TP[0A]=10 | Whole panel driven by the chosen row only (all pixels' bits select it); user sees darker or lighter; log line with row and ms | Same timeout rule |
| Session cap | 2,400 scans since last FULL_REFRESH | Lab runs FULL_REFRESH and resets the count | No error expected |
| Exit (Back, Home, sleep) | any state | PSRAM freed, one FULL_REFRESH, scan count logged | No error expected |
| PSRAM allocation fails | `allocatePsram` returns null | Lab shows a one-line notice and returns to the launcher | No scan is sent |

</frozen-after-approval>

## Code Map

- `freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h:268,291` and `src/FreeInkDisplay.cpp:869,906` -- `displayGrayBuffer(turnOff, lut, factoryMode)` and `copyGrayscaleBuffers(lsb, msb)`: raw bytes to 0x24 (lsb) and 0x26 (msb); with `factoryMode=true` the SSD1677 driver (`src/driver/Ssd1677Driver.cpp:815-847`) uploads bytes 0-104 to 0x32, 105 to 0x03, 106-108 to 0x04, 109 to 0x2C, writes 0x21=0x00, 0x22=0xCC, 0x20, blocks on BUSY, leaves analog on, sets `_needsGrayClear` (the next stock FAST becomes a HALF; the lab never relies on stock FAST). The scan primitive; do not change.
- `lib/hal/HalDisplay.h:108,130`, `HalDisplay.cpp:13,146` -- `displayGrayBuffer(bool)` drops the LUT; `EInkDisplay einkDisplay` private; `getController()`. Add only the guarded accessor.
- `docs/crosshatch/upstream-touches.md:28-44,87` -- ledger table (rows 1-12, "No reserve row remains"); `scripts/check_upstream_touches.py` reads it. Add row 13.
- `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/` -- the spine; AD-3 is the ledger decision. Add the seam note.
- `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1748,2190` -- `ACTIVE.displayController` (promoted by `applyXteinkDisplayController()` in `src/main.cpp:392`), `isX4Pro()`; `display.getController()` is the HAL mirror. Runtime gate.
- `freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h:11-42` -- 112-byte layout: bytes 0-49 five LUT rows x 10 (each byte four 2-bit phases A-D of one group), 50-99 ten groups x (TP A,B,C,D, RP), 100-104 frame rate, 105-109 VGH VSH1 VSH2 VSL VCOM, 110-111 reserved. Row index = (RED bit, BW bit): 00 LUT0, 01 LUT1, 10 LUT2, 11 LUT3, LUT4 = VCOM. VS codes: 00 VSS, 01 VSH1, 10 VSL, 11 VSH2.
- `src/activities/games/GameOptionsActivity.{h,cpp}` -- smallest `UiListActivity` to copy (NAME, `onEnter` under `RenderLock`, `listCount`, `buildScreen`, `activateIndex`, `onBackButton` with `app.clearTapFlash(); finish();`).
- `src/activities/games/GameMatchActivity.cpp:455-880` -- `UiAppHost` screen pattern: `loop()`, `mappedInput.wasReleased(MappedInputManager::Button::Back/Confirm/NavNext/NavPrevious)`, `skipLoopDelay()`, `millis()` around `renderer.displayBuffer`, `LOG_INF`.
- `src/activities/games/GamesLauncherActivity.{h,cpp}` -- fork-only launcher; add the row and its `startActivityForResult`.
- `src/activities/util/ConfirmationActivity.h` -- `ConfirmationActivity(renderer, input, heading, body)` for the entry warning.
- `lib/hal/HalMemory.h:16-21` -- `HalMemory::allocatePsram(bytes)` returns `PsramBuffer`, null on failure (pattern in `src/games/GameArena.cpp:8`). Two planes of 48,000 B.
- `lib/GfxRenderer/GfxRenderer.h:217-334` -- `clearScreen`, `fillRect`, `drawText`, `drawCenteredText`, `displayBuffer` for the lab's own screens.
- `lib/I18n/translations/english.yaml:568` -- `STR_GAMES_*` keys (ledger row 2); generated headers never hand-edited.
- `test/game_core/CMakeLists.txt`, `test/absolute_grayscale/CMakeLists.txt` -- suite pattern (3 lines: add_executable, link `crosspoint_test_common GTest::gtest_main`, `gtest_discover_tests`); `test/CMakeLists.txt` row 3 allows only the two game `add_subdirectory` lines, so the new suite is added from `test/game_core/CMakeLists.txt`.
- `scripts/check_flash_budget.py` -- the lab compiles under `FREEINK_CAP_GAMES`: counts toward the 270 KiB games flash delta and 1,024 B static RAM delta; no mutable static over 64 B in `src/activities/games` or `lib/Game*`.
- `.claude/skills/run-crosshatch-player/SKILL.md:168` -- simulator has no e-ink physics and no `Ssd1677Driver`; it can screenshot the launcher row, warning and menu only.

## Tasks & Acceptance

**Execution:**
- [x] `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/*.md` -- under AD-3, note the display-lab seam: one guarded facade accessor in `HalDisplay.h`, ledger row 13 -- the ledger requires the spine update first
- [x] `docs/crosshatch/upstream-touches.md` -- row 13: `lib/hal/HalDisplay.h`, "`EInkDisplay& eink()` accessor for the display lab", Guarded yes; replace "No reserve row remains" wording accordingly -- CI gate
- [x] `lib/hal/HalDisplay.h` -- `#if FREEINK_CAP_GAMES` / `EInkDisplay& eink() { return einkDisplay; }` / `#endif`, nothing else -- the seam
- [x] `lib/GameDisplayLab/Ssd1677ScanLut.{h,cpp}` -- `buildOneFrameLut(uint8_t out[112], uint8_t tp, uint8_t frameRateByte, VsCode row1, VsCode row2)`: LUT0 and LUT3 and LUT4 all 00, LUT1 and LUT2 phase A of group 0 per args, TP[0A]=tp, every other TP and RP 0, five frame-rate bytes = frameRateByte, stock voltage tail -- pure C++, host-tested
- [x] `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- `UiListActivity` menu (Frame period, Polarity, Exit); `ConfirmationActivity` warning on entry; SSD1677 and X4 Pro gate; PSRAM planes; scan counter with forced FULL_REFRESH at 2,400; FULL_REFRESH on exit -- the lab shell, owner of the scan primitive (`scan(lut)` = `copyGrayscaleBuffers` + timed `displayGrayBuffer(false, lut, true)` via `display.eink()`)
- [x] `src/activities/games/display_lab/FramePeriodActivity.{h,cpp}` -- runs the nine all-hold scans with `micros()` timing, the 5 s abort, log lines, and a results table -- the first unknown
- [x] `src/activities/games/display_lab/PolarityActivity.{h,cpp}` -- sets every pixel's bits to select LUT1 (then LUT2 on the next press) with TP[0A]=10, scans, logs, shows which row was driven -- the second unknown
- [x] `src/activities/games/GamesLauncherActivity.cpp` -- "Display lab" row shown only when `BoardConfig::isX4Pro()` and `display.getController()` is SSD1677 -- entry point
- [x] `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_*` keys (title, warning heading and body, menu rows, result labels) -- text
- [x] `test/game_display_lab/{CMakeLists.txt,test_scan_lut.cpp}` and one `add_subdirectory` line in `test/game_core/CMakeLists.txt` -- byte-exact LUT layout, tp placement, frame-rate bytes, voltage tail, row codes -- the matrix's LUT rows
- [x] `docs/crosshatch/display-lab.md` -- the hardware checklist: flash x4pro, open Games, Display lab, run Frame period, copy the nine `DLAB` lines, run Polarity and note which row darkens, photograph, expected ranges, where Part B picks up -- the user's validation script

**Acceptance Criteria:**
- Given an X4 Pro whose probe says SSD1677, when the Games launcher opens, then "Display lab" is present; on a UC8179 or UC8279 unit, or on any other board, it is absent.
- Given the warning screen, when Back is pressed, then no command reaches the panel.
- Given the frame-period screen completes, then nine `DLAB` lines report tp, frame-rate byte and BUSY microseconds and the screen lists them.
- Given the polarity screen runs both rows, then two `DLAB` lines name the row and its milliseconds.
- Given any exit path, then one FULL_REFRESH runs and the PSRAM planes are freed.
- Given `python3 scripts/check_upstream_touches.py` on the branch, then it passes with row 13.
- Given the host suite, when `ctest` runs, then `test_scan_lut` passes.

## Implementation Notes

- Compile guard: the lab is `#if FREEINK_CAP_GAMES && FREEINK_DEVICE_X4PRO && FREEINK_CAP_DISPLAY_LAB && !defined(SIMULATOR)` (`FREEINK_CAP_DISPLAY_LAB=1` in the `x4pro` development env only, never a release env, per the owner's review decision; one macro,
  `CROSSHATCH_DISPLAY_LAB`, defined in `display_lab/DisplayLabActivity.h`), not only `FREEINK_CAP_GAMES && FREEINK_DEVICE_X4PRO`:
  `simulator_x4pro` defines `FREEINK_DEVICE_X4PRO=1` with `lib_ignore = hal`, and the simulator's HAL stand-in has no
  `HalDisplay::eink()`. The simulator therefore has no row at compile time (the plan's "absent in the simulator" holds).
- New game library `lib/GameDisplayLab` needed a layer-table row (spine, `scripts/check_layers.py` `TABLE`/`SPINE_TERMS`/
  `COMPONENTS`, `check_layers_test.py` BASE tree) and a `GamesBuildAnchor.cpp` include (AD-2, `FILE_EDGES`); the Screens row
  gained `GameDisplayLab`. `scripts/check_upstream_touches_test.py` pins the ledger row count (now 13).
- The launcher's `activities/util/ConfirmationActivity.h` include is under `CROSSHATCH_DISPLAY_LAB`: the host harness
  (`test/game_script/harness`) compiles `GamesLauncherActivity.cpp` and cannot compile `OptionPopup.h`.
- The experiment screens are `UiListActivity` tables (Run row over result rows; two rows for polarity), not custom
  `UiAppHost` layouts. The polarity screen holds its repaint after a drive until the next press or tap, so the user can
  see which row darkened; that press repaints with a promoted FULL refresh. The frame-period run repaints at once with
  a promoted FULL refresh.
- Timeout: the facade's BUSY wait returns on its own (30 s SDK limit), so the 15,000 ms rule (owner's review decision; 5,000 ms in the plan) is applied to the measured
  call time after it returns; it then aborts the run, logs `timeout`, and runs FULL_REFRESH.
- An inverted display is refused with a notice row (the facade drops grayscale calls while inverted), as is a PSRAM
  failure; the notice row's activation or Back returns to the launcher.
- Activity names `DisplayLab`, `DlabFramePeriod`, `DlabPolarity` are not in `ActivityManager::goHome`'s Games mapping
  (ledger row 5 is untouched), so a Home gesture from the lab opens Home on its default row.

### Lead's diff read and matrix audit (2026-10-16)

- Fixed before review: the compile guard as implemented tested `defined(FREEINK_DEVICE_X4PRO)`, which is true on every board once BoardConfig.h defaults the macro to 0, so the lab would have compiled into the `sticky` build; the guard now tests the macro's value (`FREEINK_CAP_GAMES && FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)`). The frame-period result cells were 16 bytes and "Not enough memory" is 17; widened to 24.
- Matrix audit: the session-cap, timeout and nine-scan-order rows had no host test because their logic sat in the activity. Extracted `lib/GameDisplayLab/ScanBudget.h` (cap, cleaned, record with the timeout verdict, counts) and `framePeriodScan(i)` in `Ssd1677ScanLut.h`; `DisplayLabActivity` and `FramePeriodActivity` use them; `test/game_display_lab/test_scan_budget.cpp` covers those three rows. The frame-period and polarity rows' LUT content and plane fills are covered by `test_scan_lut.cpp`. The remaining rows (launcher row presence, warning, PSRAM failure notice, exit refresh) are device behaviours the host harness cannot exercise (it compiles the launcher with the lab compiled out, and the simulator has no SSD1677 driver); they are on the hardware checklist in `docs/crosshatch/display-lab.md`.
- Host suite after the changes: all tests pass; `check_upstream_touches.py`, `check_layers.py` and the three fork script tests pass; `clang-format-fix` is stable.

### Verification after the review patches (2026-10-16, the one firmware run)

- Host suite: 100% of 2,078 tests pass (includes `test_scan_lut`, `test_scan_budget`, and the five new launcher harness cases).
- `scripts/check_upstream_touches.py` PASS (rows 1 and 13 as amended); `scripts/check_layers.py` passed (647 edges); every `scripts/*_test.py` OK; `./bin/clang-format-fix` stable.
- `pio run -e x4pro` SUCCESS; `pio run -e default` (C3) SUCCESS; `pio run -e sticky` SUCCESS (the lab compiles out there).
- `pio check -e x4pro --fail-on-defect low/medium/high`: no defects.
- Flash budget (`check_flash_budget.py build on/off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`): firmware delta within budget with 9,712 B to spare; static RAM delta +784 B of 1,024; game objects clean (largest mutable static 4 B). The lab counts toward the games delta because the games-off build also drops `FREEINK_CAP_GAMES`; headroom is now about 9.5 KiB.
- Simulator (`sim.sh setup`, `build x4pro`, `start`): Home and Games launcher screenshots in `spike-dlab-screenshots/`; the launcher shows no "Display lab" row (compiled out) and "No games found" renders as before.
- Human decisions applied: `FREEINK_CAP_DISPLAY_LAB` set in the `x4pro` env only; `SCAN_TIMEOUT_MS` 15,000.
- Not verifiable here: the frame period and polarity themselves (hardware checklist in `docs/crosshatch/display-lab.md`).

## Plan Change Log

## Review Triage Log

### Pass 1 (2026-10-16): blind-hunter 12, edge-case-hunter 9, verification-gap 2 + 2 other, intent-alignment descriptive

Verdicts: high 3, medium 3, low 9, false 4, maybe-false 1; intent_gap 2 (held for the human, code kept in place, see below), patch 9 entries, defer 1, rejected 6.

| # | lens | location | verdict | route | evidence |
|---|---|---|---|---|---|
| 1 | blind, edge | PolarityActivity.cpp activateIndex/drive | high | patch | Verified: `UiListActivity::routeListTouch` calls `requestUpdate()` when `app.invalidated()` after a routed tap, and `FreeInkApp::route` invalidates after dispatch, so a tapped row drives the panel and repaints at once with the promoted FULL; only Confirm holds. Fix: skip `render` while `shownOnPanel`, clear the tap flash before the drive. |
| 2 | blind, edge | PolarityActivity.cpp shownOnPanel on Up/Down | low | patch (doc) | With the render skip, Up/Down repaints are swallowed and the next Confirm or tap repaints; the checklist's "press any key" is wrong and becomes "Confirm or tap". |
| 3 | blind | DisplayLabActivity.h guard vs release envs | high | intent_gap | Verified: `x4pro-gh_release` and `x4pro-gh_release_rc` set both flags, so the bench ships in release firmware; the frozen Boundaries say "compiled only under FREEINK_CAP_GAMES && FREEINK_DEVICE_X4PRO", which includes them. Human decides; code kept in place since the change is one guard. |
| 4 | blind, edge | DisplayLabActivity.cpp scan, dropped facade call | medium | patch | Verified: `displayGrayBuffer` returns at once when `_inverted` or `_grayPassFailed`; the control-center inversion toggle can be reached over the lab. Fix: re-check `isInverted()` per scan (Refused) and classify a busy time under a floor as Dropped in `ScanBudget`, with a test. |
| 5 | edge | FramePeriodActivity.cpp 5 s timeout vs tp=100 | high | intent_gap | Verified reasoning: at a frame period over 50 ms (the SSD1675 range reaches 67 ms) a legitimate tp=100 scan exceeds the frozen matrix's 5,000 ms and aborts the run as a timeout. The limit is frozen text; human decides (recommend 15,000 ms). |
| 6 | blind, edge | DisplayLabActivity.cpp timeout applied after return; worst case 30 s | low | patch (doc) | True as described (the SDK BUSY cap is 30 s; nothing can interrupt a wait). Harm is a stalled bench, not data; the checklist states the bound. The blind hunter's "Plan Change Log empty" part is rejected: the log records loopbacks, deviations live in Implementation Notes. |
| 7 | edge | GamesLauncherActivity.cpp buildScreen with labRow and no games | low | patch | Verified: `rowCount()==0` hides "No games found" and "Not enough memory" whenever the lab row exists. Keep the lab row with no games, but show the out-of-memory text when `listFailed`. |
| 8 | edge | Ssd1677ScanLut.h framePeriodScan(i >= 9) | false | reject | Every caller iterates `i < FRAME_PERIOD_SCANS`; no reachable call passes a larger index. |
| 9 | edge | Ssd1677ScanLut.cpp buildOneFrameLut(nullptr) | false | reject | Both callers pass a stack array; no reachable null. |
| 10 | edge | PolarityActivity press sequence vs plan wording | low | patch (doc) | The checklist already describes the extra repaint press; the plan's Code Map phrase "then LUT2 on the next press" is loose. Checklist wording tightened. |
| 11 | blind | ledger row 13 / spine "three-line hunk", "112-byte upload" | low | patch (doc) | The hunk is six lines with its comment; `setCustomLut` sends 105 bytes to 0x32 plus the voltage registers. Text corrected to match the code. |
| 12 | blind | ScanBudget undocumented in plan, spine, doc | low | patch (doc) | True; the lead added it after the implementer's pass. Named in the spine row, the doc code map, and these notes. |
| 13 | blind, verification-gap | GamesLauncherActivity lab-row logic untested on host | medium | patch | Pre-verified by the verification-gap lens: the harness compiles the launcher with the lab compiled out, so `labRow` is always false; the demonstrations (restoring `rowCount()` in openRemoveDialog, swapping the provideRow checks) pass every test. Fix: a protected test seam for `labRow` and harness cases for count, label, position, no remove dialog, padding, and zero games. |
| 14 | blind | dead lab code in non-X4 Pro envs incl. C3 | false | reject | The launcher is a games file compiled only under `FREEINK_CAP_GAMES`, which the C3 `default` env does not set; on `sticky` the unreachable branch costs a few bytes, and wrapping it would defeat the test seam of row 13. |
| 15 | blind | DisplayLabActivity onExit FULL refresh with nothing scanned | low | patch | True on the notice paths and when no scan ran; `cleanPanel()` now runs only when `budget.sinceClean() > 0`. |
| 16 | blind | STR_GAMES_DLAB_ROW1/2 hardcode VSL/VSH1 | low | patch | True; the labels become format strings filled from `vsName(POLARITY_ROWn)` like the log line. |
| 17 | blind | no static_assert tying LUT_BYTES to the SDK's LUT size | maybe-false | reject | The SDK's `lut_grayscale` lives in `src/lut/`, not on the facade's public include path, so fork code cannot name it; a submodule bump that changes the layout would show in the datasheet-pinned host test only by hand. Would need an SDK-exported constant; noted for Part B. |
| 18 | verification-gap | DisplayLabActivity scan cap/timeout wiring device-only | medium (unverified on host) | defer | Pre-verified as filed: `ScanBudget` arithmetic is tested, the wiring in `scan()` is not compilable on the host (no display facade double); the checklist cannot reach 2,400 scans. Settled when Part B gives the primitive a host-testable shape. |
| 19 | verification-gap (other) | plan Implementation Notes misstate the guard | low | fixed in notes | The lead's note described the wrong guard; corrected above. |
| 20 | intent-alignment (d) | plan Verification's simulator step no longer checks the runtime gate | low | fixed in notes | True: the lab is compiled out of the simulator, so the row's absence is a preprocessor fact. The simulator step now only screenshots the launcher without the row; the gate is on the hardware checklist. |

Deviation from the workflow's intent_gap rule: rows 3 and 5 are each a one-value change (a compile guard, a timeout constant); the code was not reverted while the human decides, and the patch round for the other entries proceeds in parallel.


## Design Notes

- Plane bits are raw in overlay mode: a 1 in the BW plane (0x24) with 0 in RED (0x26) selects LUT1; 1 in RED with 0 in BW selects LUT2; both 0 selects LUT0 (hold). The lab calls them "row 1" and "row 2" and lets the panel say which is black; InkBoy's convention on the SSD1681 was white = VSL (10) on the BW-plane row and black = VSH1 (01) on the RED-plane row, which is the default the polarity screen tries first.
- Frame-rate bytes: 0x22 is what Waveshare's 4-gray LUT uses on this panel; 0x11 and 0x44 bracket it. The encoding is undocumented, so the three values are an experiment, not a claim.
- Timing wraps the whole facade call (LUT upload of 112 CS-framed bytes plus activation plus BUSY). The frame period is the slope of busy time against tp across 1, 10 and 100, which cancels the fixed overhead.
- Session cap and exit refresh mirror InkBoy's 2,400 and 2,402 limits; the lab is a bench, not a product mode.
- Flash budget: if CI's `x4pro flash budget` fails on the games delta, the fallback is a `FREEINK_CAP_DISPLAY_LAB` env flag with ledger row 1's Change text extended in the same spine update.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all suites pass including `game_display_lab`
- `python3 scripts/check_upstream_touches.py` -- expected: pass with row 13
- `./bin/clang-format-fix` (after `git add` of new files), then again, then `git status` -- expected: nothing new
- `flock /tmp/crosshatch-build.lock sh -c 'pio run -e x4pro && pio run -e default'` -- expected: both build
- `flock /tmp/crosshatch-build.lock sh -c 'pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high'` -- expected: no defects
- `.claude/skills/run-crosshatch-player/sim.sh setup && sim.sh build x4pro && sim.sh start x4pro`, navigate to Games, screenshot -- expected: the lab row is absent in the simulator (its HAL reports no SSD1677), which itself checks the gate

**Manual checks (if no CLI):**
- Hardware (user): follow `docs/crosshatch/display-lab.md`; the nine-line frame-period table and the polarity answer are the spike's deliverables and the inputs to Part B.

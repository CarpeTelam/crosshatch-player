---
title: 'Spike: SSD1677 scan primitive and frame-period lab on the X4 Pro'
type: 'feature'
ticket: ''
created: '2026-10-16'
status: 'draft'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
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

## Boundaries & Constraints

**Always:** All new code under `src/activities/games/display_lab/` and `lib/GameDisplayLab/` (ledger fork-only globs), compiled only under `FREEINK_CAP_GAMES && FREEINK_DEVICE_X4PRO`, and gated at runtime on `BoardConfig::ACTIVE.displayController == SSD1677` on an X4 Pro; the C3 `default` env must still build. LUT bytes 105-109 (VGH, VSH1, VSH2, VSL, VCOM) stay the panel's stock values (0x17 0x41 0xA8 0x32 0x30). Every scan goes through the facade's `copyGrayscaleBuffers(black, white)` then `displayGrayBuffer(turnOff=false, lut, factoryMode=true)`; the lab exits with `displayBuffer(FULL_REFRESH)`. Mask planes live in PSRAM via `HalMemory::allocatePsram`, allocated in `onEnter` and freed in `onExit`; locals under 256 bytes; `makeUniqueNoThrow` only; `LOG_INF("DLAB", ...)` for every measurement; `tr(STR_GAMES_DLAB_*)` for text. The LUT builder (`lib/GameDisplayLab`) is pure C++ with no Arduino includes and has a host GoogleTest suite.

**Never:** No edit to `freeink-sdk/`, `.agents/`, `CrossPointSettings.h`, `SettingsList.h`, or any upstream file other than the HAL accessor, the ledger, the yaml keys and the test CMake the ledger already allows; no `#define private`; no command 0x30; no voltage changes; no windowed activation; no background FreeRTOS task; no per-frame heap allocation; no change to the stock refresh paths or the reader; no per-pixel model or dose table (Part B).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Launcher row | X4 Pro, controller SSD1677 | "Display lab" row shown | No error expected |
| Launcher row | UC8179/UC8279 unit, or not X4 Pro | Row absent | No scan is ever sent |
| Enter lab | Confirm on the warning screen | Lab menu: Frame period, Polarity, Exit | Back on the warning returns without touching the panel |
| Frame period | TP[0A] in {1,10,100} x frame-rate bytes {0x22, 0x11, 0x44}, all-hold masks | Nine scans; per scan `LOG_INF("DLAB","tp=%u fr=0x%02x busy_us=%lu")`; table on screen | A scan over 5,000 ms aborts the run, logs `timeout`, and runs FULL_REFRESH |
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
- [ ] `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/*.md` -- under AD-3, note the display-lab seam: one guarded facade accessor in `HalDisplay.h`, ledger row 13 -- the ledger requires the spine update first
- [ ] `docs/crosshatch/upstream-touches.md` -- row 13: `lib/hal/HalDisplay.h`, "`EInkDisplay& eink()` accessor for the display lab", Guarded yes; replace "No reserve row remains" wording accordingly -- CI gate
- [ ] `lib/hal/HalDisplay.h` -- `#if FREEINK_CAP_GAMES` / `EInkDisplay& eink() { return einkDisplay; }` / `#endif`, nothing else -- the seam
- [ ] `lib/GameDisplayLab/Ssd1677ScanLut.{h,cpp}` -- `buildOneFrameLut(uint8_t out[112], uint8_t tp, uint8_t frameRateByte, VsCode row1, VsCode row2)`: LUT0 and LUT3 and LUT4 all 00, LUT1 and LUT2 phase A of group 0 per args, TP[0A]=tp, every other TP and RP 0, five frame-rate bytes = frameRateByte, stock voltage tail -- pure C++, host-tested
- [ ] `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- `UiListActivity` menu (Frame period, Polarity, Exit); `ConfirmationActivity` warning on entry; SSD1677 and X4 Pro gate; PSRAM planes; scan counter with forced FULL_REFRESH at 2,400; FULL_REFRESH on exit -- the lab shell, owner of the scan primitive (`scan(lut)` = `copyGrayscaleBuffers` + timed `displayGrayBuffer(false, lut, true)` via `display.eink()`)
- [ ] `src/activities/games/display_lab/FramePeriodActivity.{h,cpp}` -- runs the nine all-hold scans with `micros()` timing, the 5 s abort, log lines, and a results table -- the first unknown
- [ ] `src/activities/games/display_lab/PolarityActivity.{h,cpp}` -- sets every pixel's bits to select LUT1 (then LUT2 on the next press) with TP[0A]=10, scans, logs, shows which row was driven -- the second unknown
- [ ] `src/activities/games/GamesLauncherActivity.cpp` -- "Display lab" row shown only when `BoardConfig::isX4Pro()` and `display.getController()` is SSD1677 -- entry point
- [ ] `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_*` keys (title, warning heading and body, menu rows, result labels) -- text
- [ ] `test/game_display_lab/{CMakeLists.txt,test_scan_lut.cpp}` and one `add_subdirectory` line in `test/game_core/CMakeLists.txt` -- byte-exact LUT layout, tp placement, frame-rate bytes, voltage tail, row codes -- the matrix's LUT rows
- [ ] `docs/crosshatch/display-lab.md` -- the hardware checklist: flash x4pro, open Games, Display lab, run Frame period, copy the nine `DLAB` lines, run Polarity and note which row darkens, photograph, expected ranges, where Part B picks up -- the user's validation script

**Acceptance Criteria:**
- Given an X4 Pro whose probe says SSD1677, when the Games launcher opens, then "Display lab" is present; on a UC8179 or UC8279 unit, or on any other board, it is absent.
- Given the warning screen, when Back is pressed, then no command reaches the panel.
- Given the frame-period screen completes, then nine `DLAB` lines report tp, frame-rate byte and BUSY microseconds and the screen lists them.
- Given the polarity screen runs both rows, then two `DLAB` lines name the row and its milliseconds.
- Given any exit path, then one FULL_REFRESH runs and the PSRAM planes are freed.
- Given `python3 scripts/check_upstream_touches.py` on the branch, then it passes with row 13.
- Given the host suite, when `ctest` runs, then `test_scan_lut` passes.

## Implementation Notes

## Plan Change Log

## Review Triage Log

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

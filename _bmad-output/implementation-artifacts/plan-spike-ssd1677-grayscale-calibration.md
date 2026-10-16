---
title: 'Spike: SSD1677 grayscale calibration lab on the X4 Pro'
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
  - '{project-root}/_bmad-output/planning-artifacts/research/technical-fast-e-paper-display-driving-for-x4-pro-2026-10-16/digests/D2-r2-1.md'
  - '{project-root}/docs/contributing/touch-and-ui.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The research run recommends building InkBoy's per-pixel grayscale engine for the X4 Pro's SSD1677 batch, but two numbers it needs are unknown and only a panel can give them: the SSD1677 frame period (one "dose" is one one-frame activation) and a first 12-entry dose table for this panel. Nothing in the firmware can drive a caller-supplied one-frame waveform with per-pixel hold/black/white masks today.

**Approach:** A fork-only, X4 Pro-only, SSD1677-only "Display lab" reachable from the Games launcher: a scan primitive (caller LUT via 0x32, raw masks into both RAM planes, activation with 0x22 = 0xCC, timed BUSY wait) built on the SDK facade's existing overlay-grayscale path, a frame-period measurement screen, and InkBoy's transition-test screen driven by a per-pixel level and dose model with a live-editable table and an SD preset. Safety first: stock voltages, never the OTP command, entry warning, session cap, cleaning refresh on exit.

## Boundaries & Constraints

**Always:** All new code under `src/activities/games/display_lab/` and `lib/GameDisplayLab/` (ledger fork-only globs), compiled only under `FREEINK_CAP_GAMES && FREEINK_DEVICE_X4PRO`, and gated at runtime on `BoardConfig::ACTIVE.displayController == SSD1677`; the C3 `default` env must still build. LUT bytes 105-109 (VGH, VSH1, VSH2, VSL, VCOM) stay the panel's stock values (0x17 0x41 0xA8 0x32 0x30). Every scan goes through the facade's `displayGrayBuffer(turnOff=false, lut, factoryMode=true)` after `copyGrayscaleBuffers(black, white)`; exit always runs `displayBuffer(FULL_REFRESH)`. Per-pixel state lives in PSRAM via `HalMemory::allocatePsram`, allocated in `onEnter` and freed in `onExit`; locals under 256 bytes; `makeUniqueNoThrow` only; `LOG_INF` for every measurement; `tr(STR_GAMES_*)` for text. The model (`lib/GameDisplayLab`) is pure C++ with no Arduino includes and has a host GoogleTest suite.

**Never:** No edit to `freeink-sdk/`, `.agents/`, `CrossPointSettings.h`, `SettingsList.h`, or any upstream file outside the ledger (the one HAL seam is decided in Open Questions); no `#define private`; no command 0x30; no voltage changes; no windowed activation (the facade writes full planes); no background FreeRTOS task; no per-frame heap allocation; no change to the stock refresh paths or to the reader.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Enter lab on SSD1677 X4 Pro | Games launcher, "Display lab" row | Warning screen (lifespan, experimental); Confirm enters, Back returns | No error expected |
| Enter lab on UC8179/UC8279 unit or non-X4 Pro | controller != SSD1677 | Row hidden; if reached, a one-line notice and Back | No scan is ever sent |
| Frame-period measurement | TP[0A] in {1,10,100}, frame-rate bytes in {0x22, A, B}, all-hold masks | One scan per combination; `LOG_INF("DLAB", "tp=%u fr=0x%02x busy=%lu us")`; table shown on screen | A scan over 5 s aborts the run and logs a timeout |
| Transition test | preset doses, hold mask, polarity | Four bands K/D/L/W, three 12 px rectangles per band sweeping 5 s, 1 s pause, repeat; each slot: compute masks from the model, scan, commit doses | Scan failure stops the sweep and shows the last log line |
| Live edit | tuning list, one value changed | Next sweep uses the value; "Save" writes `/.crosshatch/display-lab.json` | Write failure keeps the in-memory value and logs |
| Preset load | file missing or malformed | InkBoy defaults (D2a table, hold mask 0xc667, retarget 100, white=VSL, black=VSH1) | Malformed file ignored with LOG_ERR |
| Session cap | 2,400 scans since last full refresh | Lab forces `displayBuffer(FULL_REFRESH)` and resets the count | No error expected |
| Exit (Back, Home, sleep) | any state | PSRAM freed, `displayBuffer(FULL_REFRESH)` once, counters logged | No error expected |

</frozen-after-approval>

## Code Map

- `freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h:268,291` and `src/FreeInkDisplay.cpp:869,906` -- `displayGrayBuffer(turnOff, lut, factoryMode)` and `copyGrayscaleBuffers(lsb, msb)`: raw bytes to 0x24 (lsb) and 0x26 (msb); with `factoryMode=true` the SSD1677 driver (`src/driver/Ssd1677Driver.cpp:815-847`) uploads bytes 0-104 to 0x32, 105 to 0x03, 106-108 to 0x04, 109 to 0x2C, writes 0x21=0x00, 0x22=0xCC, 0x20, blocks on BUSY, leaves analog on, sets `_needsGrayClear` (next FAST becomes a HALF). The scan primitive. Do not change.
- `lib/hal/HalDisplay.h:108,130` and `HalDisplay.cpp:146` -- `displayGrayBuffer(bool)` drops the `lut`/`factoryMode` arguments; `EInkDisplay einkDisplay` is private; `getController()` at cpp:13. Upstream file, not in the ledger: see Open Questions.
- `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1748,2190` -- `displayController` (promoted by `applyXteinkDisplayController()` in `src/main.cpp:392`), `isX4Pro()`; the runtime gate.
- `freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h:11-42` -- 112-byte layout (5 LUT rows x 10 bytes VS, 10 x 5 TP/RP, 5 frame-rate, VGH VSH1 VSH2 VSL VCOM, 2 reserved); LUT row index = (RED bit, BW bit): 00 LUT0, 01 LUT1, 10 LUT2, 11 LUT3, LUT4 = VCOM. VS codes 00 VSS, 01 VSH1, 10 VSL, 11 VSH2. Build the one-frame LUT from this: LUT0 all VSS; white row and black row phase A set per preset polarity; TP[0A] = n, everything else 0; RP 0; frame-rate bytes from the preset; stock voltage tail.
- `src/activities/games/GameOptionsActivity.{h,cpp}` -- smallest `UiListActivity` (menu and tuning list to copy); `src/activities/games/GameMatchActivity.{h,cpp}:455-880` -- `UiAppHost` custom screen with `loop()`, buttons via `mappedInput.wasReleased(Button::Back/Confirm/NavNext/NavPrevious)`, `skipLoopDelay()`, `millis()` timing and `LOG_INF`; `docs/activity-manager.md:51-80,375-437` -- `RenderLock`, `startActivityForResult`, `finish()`; never `rowTouch`.
- `src/activities/games/GamesLauncherActivity.*` -- where the "Display lab" row goes (fork-only; find its row list and `activateIndex`).
- `src/activities/util/ConfirmationActivity.h` -- `ConfirmationActivity(renderer, input, heading, body)` for the entry warning.
- `lib/hal/HalMemory.h:16-21` -- `HalMemory::allocatePsram(bytes)` returns `PsramBuffer`, null on failure (used in `src/games/GameArena.cpp:8`). Planes: 2 x 48,000 B; model: 384,000 x 2 B; history optional (not in the spike).
- `lib/Serialization/PersistableStore.h:50-54` and `src/util/BookmarkFile.cpp:17,76` -- `writeDocToFile`/`readDocFromFile` with ArduinoJson 7 for the preset; `lib/hal/HalStorage.h:39-72` for `Storage`; never SdFat.
- `lib/I18n/translations/english.yaml:568` -- `STR_GAMES_*` keys (ledger row 2); generated headers never hand-edited.
- `test/absolute_grayscale/CMakeLists.txt` -- 3-line host suite to copy for `test/game_display_lab/`; `test/CMakeLists.txt:41-85` lists suites by hand (ledger row 3 allows only the two game subdirectories: put the new suite's `add_subdirectory` inside `test/game_core/CMakeLists.txt` or `test/game_script/CMakeLists.txt` instead, see Design Notes).
- `scripts/check_flash_budget.py` -- the lab compiles under `FREEINK_CAP_GAMES`, so its flash counts toward the 270 KiB games budget and its statics toward the 1,024 B RAM budget; no mutable static over 64 B in `src/activities/games` or `lib/Game*`.
- `.claude/skills/run-crosshatch-player/SKILL.md:168` -- the simulator has no e-ink physics and no `Ssd1677Driver`; it can only screenshot the menus and the warning screen.

## Tasks & Acceptance

**Execution:**
- [ ] `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/*.md` -- add the display-lab seam decision (per Open Question 1) under AD-3 -- the ledger requires a spine update before a row
- [ ] `docs/crosshatch/upstream-touches.md` -- add ledger row 13 for the HAL seam (per Open Question 1) -- CI gate
- [ ] `lib/hal/HalDisplay.h`, `lib/hal/HalDisplay.cpp` -- the seam from Open Question 1, `#if FREEINK_CAP_GAMES`-guarded, nothing else -- the only upstream code touch
- [ ] `lib/GameDisplayLab/Ssd1677ScanLut.{h,cpp}` -- build the 112-byte one-frame LUT from (tp, frameRateByte, whiteVs, blackVs) with the stock voltage tail; pure C++ -- host-testable
- [ ] `lib/GameDisplayLab/GrayModel.{h,cpp}` -- per-pixel `{level u8, remaining:4, target:2, source:2}`, dose table and hold mask, interpolated budget by level, retarget scaling, mask building (lighten to white mask else black mask; follow-up only when hold off and remaining > 1), commit after delivery; InkBoy defaults -- the engine, pure C++
- [ ] `lib/GameDisplayLab/LabPreset.{h,cpp}` -- preset struct, defaults, ArduinoJson read/write of `/.crosshatch/display-lab.json` -- persistence
- [ ] `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- `UiListActivity` menu: Frame period, Transition test, Tuning, Save preset; entry warning via `ConfirmationActivity`; SSD1677 gate; session scan counter and forced full refresh; full refresh on exit -- the lab shell
- [ ] `src/activities/games/display_lab/FramePeriodActivity.{h,cpp}` -- runs the 9 all-hold scans, times each with `micros()` around `displayGrayBuffer`, shows and logs the table -- answers the first unknown
- [ ] `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- `UiAppHost` screen: band and rectangle geometry, 5 s sweep at a fixed slot period chosen from the measured scan time, mask planes in PSRAM, pause/replay/clean/polarity-swap controls, scan count and ms per scan on screen -- the calibration instrument
- [ ] `src/activities/games/display_lab/TuningActivity.{h,cpp}` -- `UiListActivity` of the 12 doses, 12 hold bits, retarget percent, frame-rate byte, polarity; one value per row, NavNext/NavPrevious adjust -- live editing
- [ ] `src/activities/games/GamesLauncherActivity.cpp` -- "Display lab" row, shown only when `isX4Pro()` and controller is SSD1677 -- entry point
- [ ] `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_*` keys -- text
- [ ] `test/game_display_lab/{CMakeLists.txt,test_gray_model.cpp,test_scan_lut.cpp}` -- LUT byte layout, dose interpolation, retarget, mask building, commit, cap; wired per Design Notes -- the I/O matrix's model rows
- [ ] `docs/crosshatch/display-lab.md` -- hardware checklist: what to run, what to photograph, how to read the transition test, where the log lines are -- the user's validation script

**Acceptance Criteria:**
- Given an X4 Pro whose controller probe says SSD1677, when the Games launcher opens, then a "Display lab" row is present; on a UC8179 or UC8279 unit it is absent.
- Given the lab is entered, when the warning is declined, then no command reaches the panel and the launcher returns.
- Given the frame-period screen runs, when it completes, then nine `DLAB` log lines report tp, frame-rate byte and BUSY microseconds, and the screen shows them.
- Given the transition test runs with the default preset, when 2,400 scans have been sent since the last full refresh, then a full refresh runs and the count resets.
- Given any exit path, when the lab is left, then one `FULL_REFRESH` runs and the PSRAM buffers are freed.
- Given the host suite, when `ctest` runs, then the LUT builder, dose interpolation, retarget scaling, mask building and commit tests pass.

## Implementation Notes

## Plan Change Log

## Review Triage Log

## Design Notes

- Scan = `copyGrayscaleBuffers(blackMask, whiteMask)` then `displayGrayBuffer(false, lut, true)`. Plane bits are raw: a 1 in the BW plane with 0 in RED selects LUT1, a 1 in RED with 0 in BW selects LUT2; both 0 selects LUT0 (hold). Polarity (which of LUT1/LUT2 drives white) is a preset field with a swap control because the SSD1677 datasheet's row naming is a convention, not a guarantee; InkBoy's default is white = VSL (10), black = VSH1 (01).
- The transition test's slot period is `max(measured scan time + 5 ms, 100 ms)`: a board game needs a few scans per second, not 33 ms, and the plane upload alone is about 19 ms at 40 MHz.
- `_needsGrayClear` makes the next stock FAST a HALF; the lab never relies on stock FAST while active and always exits with FULL.
- Host suite wiring: ledger row 3 allows only `add_subdirectory(game_core)` and `add_subdirectory(game_script)` in `test/CMakeLists.txt`, so `test/game_display_lab` is added from `test/game_core/CMakeLists.txt` (fork-only) with `add_subdirectory(../game_display_lab game_display_lab)`.
- Flash budget risk: the lab adds to the games delta; if CI's `x4pro flash budget` fails, the fallback is a `FREEINK_CAP_DISPLAY_LAB` env flag (ledger row 1 would need its Change text extended in the same spine update).

## Open Questions

1. **The HAL seam.** Fork code cannot pass a LUT through `HalDisplay::displayGrayBuffer(bool)`, and `lib/hal/HalDisplay.{h,cpp}` are upstream files with no ledger row ("No reserve row remains"; a row needs an architecture spine update first). Options: (a) add ledger row 13 and a spine note, exposing one guarded accessor `EInkDisplay& eink()` in `HalDisplay.h` (one line, future lab work needs no further rows); (b) add ledger row 13 for a guarded overload `displayGrayBuffer(bool, const uint8_t* lut, bool factoryMode)` (two lines, narrower surface); (c) stop here and propose the accessor to CrossPoint upstream, resuming when it lands. (a) and (b) both touch an upstream file and carry the same merge-conflict risk; (c) costs weeks.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all suites pass including `game_display_lab`
- `python3 scripts/check_upstream_touches.py` -- expected: pass with the new row
- `./bin/clang-format-fix` (after `git add` of new files), then again, then `git status` -- expected: nothing new
- `flock /tmp/crosshatch-build.lock sh -c 'pio run -e x4pro && pio run -e default'` -- expected: both build
- `flock /tmp/crosshatch-build.lock sh -c 'pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high'` -- expected: no defects
- `flock /tmp/crosshatch-build.lock sh -c 'python3 scripts/check_flash_budget.py ...'` (per its docstring) -- expected: within 270 KiB and 1,024 B

**Manual checks (if no CLI):**
- Hardware (user): follow `docs/crosshatch/display-lab.md`; the frame-period table and the first dose table are the spike's deliverables.

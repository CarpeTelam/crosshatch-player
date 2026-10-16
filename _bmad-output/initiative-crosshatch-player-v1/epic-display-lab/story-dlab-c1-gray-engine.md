# Story dlab-c1: the grayscale engine, transition test and tuning (lane C, Part B)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R3, R4, R5,
R6, Boundaries, Decisions D1, D4, D5, the owner Decision on InkBoy defaults, Measurement). Ref: `dlab-c1`. Plan:
`_bmad-output/implementation-artifacts/plan-dlab-c1-gray-engine.md`. Design reference, read in full before planning:
`_bmad-output/planning-artifacts/research/technical-fast-e-paper-display-driving-for-x4-pro-2026-10-16/digests/D2-r2-1.md`
(InkBoy's model, dose table, scheduler, calibration screen, and its re-implementation checklist) and section D2a of
the research report. Builds on `dlab-0`'s `ScanBackend`/`ScanRequest`/`MaskPlanes` (read `src/activities/games/display_lab/ScanBackend.h` and `lib/GameDisplayLab/MaskPlanes.h` as they are in the tree; they are fixed).

## Intent

**Problem:** The bench can drive the whole panel one way, but the product goal is stable four-shade grayscale without
dithering and without a flashing refresh. InkBoy's engine does that on an SSD1681 by moving each pixel to its target
shade with a counted number of one-frame pulses and then holding it; nothing in the firmware models pixels or counts
doses, and no screen lets the owner find this panel's dose table.

**Approach:** Port InkBoy's design over `ScanBackend`: a pure `GrayModel` in `lib/GameDisplayLab/` (per-pixel 2-byte
state `{level u8, remaining:4, target:2, source:2}` over a caller-sized pixel count, a 12-entry dose table with a
16-bit hold mask, the interpolated budget by current level, retarget scaling for interrupted pixels, commit of one or
two doses per delivered scan, and the black/white mask build for a scan plus the follow-up mask), a pure `GrayPreset`
(the tunable values, InkBoy's defaults, JSON encode/decode via ArduinoJson for `/.crosshatch/display-lab.json`), a pure
`SlotScheduler` (fixed slot period per epic D5, skip-not-queue), and two screens: `TransitionTestActivity` (four
shade bands K/D/L/W top to bottom, three 12-px rectangles of the other shades per band sweeping across in 5 s, 1 s
pause, repeat; pause, replay, clean, and a live "scans/s, ms per scan" line; the panel shows the drive, the screen is
not repainted during the sweep per D4) and `TuningActivity` (a list of the 12 doses, 12 hold bits, retarget percent,
frame-rate index, polarity swap, one value per row adjusted with NavNext/NavPrevious or tap, Save writes the preset).
The lab menu gains "Transition test", "Tuning" and "Save preset" rows. History routes and the weak row are optional
in this story: implement the model fields and the budget rules for them, with the weak row used only when
`backend->supportsWeakRow()` is true, and leave both off in the default preset.

## Boundaries & Constraints

**Always:** pixel state and the two mask planes in PSRAM (`HalMemory::allocatePsram`), allocated in `onEnter`, freed
in `onExit`; the model, preset and scheduler are pure C++ with host tests that pin InkBoy's dose table and the
level arithmetic from the digest (`level += (target*64 - level) / remaining`); every scan goes through
`ScanBackend::scan` with the lab's budget wrapper (timeout, cap, dropped, inverted); the sweep stops on any non-Done
outcome and the screen says why; a full refresh before the first sweep and on exit; `STR_GAMES_DLAB_*` keys; no
change to `ScanBackend`'s shape; flash: the engine must stay within the epic's measured headroom (9,712 B at base);
report the measured delta, and HALT as blocked if it does not fit rather than trimming the safety code.

**Never:** no dithering path in this story; no per-frame heap allocation; no background task; no change to the
SSD1677/UltraChip encodings; no edit to `freeink-sdk/`; no new ledger row.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Dose lookup | preset table, level exactly on a shade | the table's dose for (source shade, target) | — |
| Dose lookup | level between shades | linear interpolation between the two neighbouring shades' doses, rounded up | — |
| Retarget mid-transition | remaining > 0, new target | budget = base dose scaled by retarget percent, clamped 1..12; source frozen to nearest shade | — |
| Mask build | pixel with remaining > 0 | in the white mask if target*64 > level else the black mask; follow-up mask only if hold bit off and remaining > 1 | — |
| Commit | scan delivered (Done) | each driven pixel's level advances by (target*64 - level)/remaining and remaining decrements; a Dropped/Refused/TimedOut scan commits nothing | — |
| Transition sweep | default preset, backend Done | rectangles sweep 5 s, pause 1 s, repeat until Pause/Back; "scans/s" updates | sweep stops on a non-Done outcome, result row shows the outcome |
| Preset load | missing or malformed file | InkBoy defaults; malformed logged with LOG_ERR | — |
| Preset save | Save row | `/.crosshatch/display-lab.json` written via Storage; failure keeps in-memory values and logs | — |
| Tuning edit | NavNext on a dose row | dose +1 (max 12); NavPrevious -1 (min 1); next sweep uses it | — |
| Memory | PSRAM allocation fails | notice row, no sweep | — |

## Code Map (verify in the tree after dlab-0)

- `src/activities/games/display_lab/ScanBackend.h`, `DisplayLabActivity.{h,cpp}` (the backend owner, budget wrapper, menu rows), `PolarityActivity.{h,cpp}` (the no-repaint-during-drive pattern to copy: `render()` override).
- `lib/GameDisplayLab/MaskPlanes.h`, `ScanBudget.h`, `Ssd1677ScanLut.h` (the frame-rate list lives in the backend now; use `backend->rateCount()/rateLabel()`).
- `lib/Serialization/PersistableStore.h` (`writeDocToFile`/`readDocFromFile`, ArduinoJson 7) and `src/util/BookmarkFile.cpp` as the call-site example; `lib/hal/HalStorage.h` (`Storage`), never SdFat.
- `src/activities/games/GameOptionsActivity.{h,cpp}` (list screen with adjustable rows pattern), `lib/GfxRenderer/GfxRenderer.h` (`fillRect`, `drawText`), `src/MappedInputManager.h` (`Button::NavNext/NavPrevious/Confirm/Back`).
- `test/game_display_lab/CMakeLists.txt` (add `test_gray_model.cpp`, `test_gray_preset.cpp`, `test_slot_scheduler.cpp`); the preset codec test needs ArduinoJson on the host: check how other host suites include it (grep `ArduinoJson` under `test/`) and, if none does, test the preset's value layer and leave the JSON layer to a device note.
- `docs/crosshatch/display-lab.md` — add a "Transition test and tuning" section: how to read the sweep, the tuning order (doses and hold first, then retarget, then history), and what to photograph.

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`, `pio run -e default`; `pio check -e x4pro` as AGENTS.md gives it; the flash-budget gate (`build on`, `build off`, `compare`, `objects`), recording the delta against the epic base; `sim.sh build x4pro` and a launcher screenshot (the lab is compiled out of the simulator, so the row is absent; that is the expected image).
- Hardware (owner, not the agent): the transition test on the UC8179 unit after lane A lands; the dose table it yields is the deliverable.

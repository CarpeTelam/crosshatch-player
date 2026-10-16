---
title: 'dlab-c1: the grayscale engine, transition test and tuning'
type: 'feature'
ticket: 'story-dlab-c1-gray-engine'
created: '2026-10-16'
baseline_revision: '9b2317f3106ec2da8b057bf4175a8d423d5afaa3'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-c1-gray-engine.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/_bmad-output/planning-artifacts/research/technical-fast-e-paper-display-driving-for-x4-pro-2026-10-16/digests/D2-r2-1.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
warnings: ['oversized']
deferred: []
---

<intent-contract>

## Intent

**Problem:** The display lab can drive the whole panel one way, but nothing models pixels or counts doses, so there is no four-shade, dither-free, flash-free grayscale and no screen on which the owner can find this panel's dose table.

**Approach:** Port InkBoy's engine over `ScanBackend`: pure `GrayModel`, `GrayPreset` (+ codec), `SlotScheduler`, `TransitionScene` and a pure slot runner in `lib/GameDisplayLab/`, host-tested; two screens (`TransitionTestActivity`, `TuningActivity`) and three new lab menu rows ("Transition test", "Tuning", "Save preset"). First, epic D6: the flash-budget gate builds both images with `FREEINK_CAP_DISPLAY_LAB` unset, its sidecar test pins that, and its docstring and docs say so.

## Boundaries & Constraints

**Always:** pixel state (2 B/pixel), the follow-up mask pair, and the optional history (3 B/pixel) and weak (1 bit/pixel) arrays in PSRAM via `HalMemory::allocatePsram`, allocated in the transition screen's `onEnter`, freed in its `onExit`; every scan through `DisplayLabActivity::scan` (timeout, cap, dropped, inverted); a non-Done outcome stops the sweep and the screen shows it; a FULL refresh before the first sweep and on leaving the test; host tests pin InkBoy's dose table, hold mask `0xc667`, and `level += (target*64 - level) / remaining`; `STR_GAMES_DLAB_*` keys only; history and weak mode implemented but off in the default preset, weak used only when `backend->supportsWeakRow()`; locals < 256 B; `makeUniqueNoThrow`/null checks; no `RenderLock` in `onExit`/destructors; report the engine's measured size.

**Never:** a change to `ScanBackend`/`ScanRequest`/`MaskPlanes` shape; dithering; per-slot heap allocation; a background task; a change to the SSD1677 encodings; an edit to `freeink-sdk/`, `.agents/`, the upstream ledger, or `scripts/check_layers.py`; ArduinoJson or `lib/Serialization` from the lab (Design Notes).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Dose lookup | preset table, level exactly on a shade | the table's dose for (source shade, target) | — |
| Dose lookup | level between shades | linear interpolation of the two neighbouring shades' doses, rounded up | — |
| Retarget mid-transition | remaining > 0, new target | budget = base scaled by retarget percent (rounded up), clamped 1..12; source = nearest shade | — |
| Mask build | pixel with remaining > 0 | white mask if target*64 > level, else black; follow-up mask only if hold bit off and remaining > 1 | — |
| Commit | drive Done (follow Done or not) | each driven pixel's level += (target*64 - level)/remaining, remaining-- (twice for follow-up pixels when the follow-up was Done) | drive Dropped/Refused/TimedOut: nothing committed |
| Transition sweep | default preset, backend Done | rectangles sweep 5 s, pause 1 s, repeat until stopped; stats update | non-Done outcome stops the sweep; the Run row shows it |
| Preset load | missing or malformed file | InkBoy defaults; malformed logged with LOG_ERR | — |
| Preset save | Save row | `/.crosshatch/display-lab.json` written via Storage | failure keeps in-memory values, logs, row says so |
| Tuning edit | NavNext on a dose row in edit | dose +1 (max 12); NavPrevious -1 (min 1); next sweep uses it | — |
| Memory | a PSRAM allocation fails | notice row, no sweep | — |

</intent-contract>

## Code Map

- `scripts/check_flash_budget.py` -- `build_environment(state)` (on: plain env; off: `PLATFORMIO_BUILD_UNFLAGS=UNFLAG`, `PLATFORMIO_BUILD_DIR=OFF_BUILD_DIR`), `check_flag(state, defines)` (used by `compare` and `objects`), module docstring; `fork_common.GAMES_MACRO/GAMES_BUILD_FLAG` style for constants.
- `scripts/check_flash_budget_test.py` -- `BuildEnvironmentTest` (line ~889: `test_on_is_the_plain_env...` changes), `CompareTest` flag tests (~395-418) for the metadata `defines` shape.
- `.github/workflows/crosshatch-ci.yml` lines 7-13 (comment describing the gate; comment only); `docs/crosshatch/game-icons.md` ~261 and `docs/crosshatch/display-lab.md` (gate descriptions).
- `src/activities/games/display_lab/ScanBackend.h` -- fixed. `ScanRequest{blackMask, whiteMask, frames, rate, swapPolarity}`, `Scan{Done, TimedOut, Dropped, Refused}`, `supportsWeakRow()`, `rateCount()/rateLabel()`.
- `lib/GameDisplayLab/MaskPlanes.h` -- fixed. `clear()`, `setPixel(mask,x,y)`, `test()`, `black()/white()` const; both-mask pixel = third (weak) row per D1.
- `DisplayLabActivity.{h,cpp}` -- session owner: `ROW_COUNT`, `Row` enum, `buildRows()`, `activateIndex()`, `openScreen<T>()`, `scan()` (cleans first when `budget.cleanDue()`), private `cleanPanel()`, `budget` (`ScanBudget::sinceClean()`), `masks()`, `backend()`.
- `PolarityActivity.{h,cpp}` -- the pattern to copy: `std::atomic<bool> shownOnPanel`, `render()` override drawing nothing while shown, `renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH)` then `requestUpdate()` to come back, `RenderLock` around row-text writes.
- `FramePeriodActivity.cpp` -- `outcomeText(Scan)` mapping to existing `STR_GAMES_DLAB_DONE/TIMED_OUT/DROPPED/REFUSED`.
- `src/activities/UiListActivity.{h,cpp}` -- `handleCustomInput()` (first hook in `loop()`), `handleButtons()` (Back/Confirm), `activateIndex` (tap or Confirm), `navigateButtons()`, `skipLoopDelay()`/`preventAutoSleep()` on `Activity`.
- `src/activities/games/GameOptionsActivity.{h,cpp}` -- adjustable-row list (tap steps the value in place).
- `lib/GfxRenderer/GfxRenderer.h` -- `clearScreen(0xFF)`, `getOrientation()` (`Portrait`, `LandscapeClockwise`, `PortraitInverted`, `LandscapeCounterClockwise`); `GfxRenderer.cpp:360 rotateCoordinates` is the logical-to-panel map `TransitionScene` must equal.
- `lib/hal/HalStorage.h` -- `Storage.mkdir`, `readFileToString(module, path, cap, std::string&)`, `openFileForWrite` + `HalFile::write`, `exists`. `lib/hal/HalMemory.h` -- `PsramBuffer allocatePsram(bytes)`.
- `test/game_display_lab/CMakeLists.txt` -- target `test_scan_lut`; links `crosspoint_test_common GTest::gtest_main`; no ArduinoJson on the host.
- `scripts/check_layers.py` -- `lib/GameDisplayLab` may include STD only; Screens may include `lib/GameDisplayLab`, `lib/hal`, `lib/I18n`, `lib/Logging`, `lib/Memory`, `lib/GfxRenderer`, upstream `src/` -- not `lib/Serialization` or `ArduinoJson.h`.
- `lib/I18n/translations/english.yaml` ~635-655 -- the `STR_GAMES_DLAB_*` block (ledger row 2); `STR_STATE_ON` exists, add DLAB-prefixed on/off text instead of reusing non-games keys only if needed.

## Tasks & Acceptance

**Execution:**
- [x] `scripts/check_flash_budget.py` -- add `LAB_MACRO = 'FREEINK_CAP_DISPLAY_LAB'`, `LAB_UNFLAG = '-DFREEINK_CAP_DISPLAY_LAB=1'`; `build_environment('on')` sets `PLATFORMIO_BUILD_UNFLAGS=LAB_UNFLAG` (still `.pio/build`), `'off'` sets `f'{UNFLAG} {LAB_UNFLAG}'`; `check_flag` also raises SetupError when either build defines the lab macro; docstring says both builds leave the dev-only lab out (epic D6) -- the gate measures shipped firmware.
- [x] `scripts/check_flash_budget_test.py` -- pin: on-build env is exactly `{PATH, PLATFORMIO_BUILD_UNFLAGS: LAB_UNFLAG}` even with overrides set; off-build unflags both; `LAB_UNFLAG` appears in `[env:x4pro]` of platformio.ini; compare/objects SetupError when the lab define is in either build's metadata; fixtures' happy-path metadata stay lab-free.
- [x] `.github/workflows/crosshatch-ci.yml` (comment lines 7-13 only), `docs/crosshatch/game-icons.md` (the "games on minus games off" sentence), `docs/crosshatch/display-lab.md` -- say both builds unset the lab (D6).
- [x] `lib/GameDisplayLab/GrayPreset.{h,cpp}` -- `Shade {K,D,L,W}` (0..3), transition index 0..11 in the table order K>D,K>L,K>W,D>K,D>L,D>W,L>K,L>D,L>W,W>K,W>D,W>L with `transitionFrom/To/Index`; `GrayPreset {dose[4][4], holdMask, retargetPercent, rateIndex, swapPolarity, historyMode (bit0 return, bit1 reverse), returnDelta[6], reversePercent[6], weakWhiteToLight}`; `inkBoyDefaults()` (table as digest, hold 0xc667, 100 %, rate 0, no swap, history 0, routes LKL -1/150 and 0/0 for DKD,DWD,LWL,DLD,LDL, weak off); `valid()`; tuning steps: dose ±1 clamp 1..12 and tap-cycle 12→1, hold set/toggle, retarget ±25 clamp 50..150 and cycle, rate ±1 clamp `< count` and cycle, swap toggle; codec `encodeGrayPreset(preset, char*, cap)` → length (0 when too small) and `loadGrayPreset(text or nullptr, len, out)` → `Missing | Loaded | Malformed` (out = defaults unless Loaded), std only.
- [x] `lib/GameDisplayLab/GrayModel.{h,cpp}` -- non-owning view: pixels (2 B: level, then remaining | target<<4 | source<<6), width, height, optional history (3 B: origin 0..3 or 4 = none, blackSteps, whiteSteps) and weak bitmap; `bytesFor`, `historyBytesFor`, `weakBytesFor`; `reset(shade)`; `baseDose(level, target, preset)`; `retarget(x, y, target, preset)` (no-op when unchanged) and clipped `retargetRect`; `buildMasks(drive, follow, preset, weakRow)` → driven count; `commit(driveDone, followDone, preset)`; accessors for level/remaining/target/source and `activeCount()`. Rules in Design Notes.
- [x] `lib/GameDisplayLab/GraySlot.h` -- header-only `template <typename ScanFn> SlotResult runGraySlot(model, drive, follow, preset, weakRow, scan)`: build masks; none driven → no scan; else drive scan, then (only when it was Done) the follow-up scan, then `commit(driveDone, followDone)`; `SlotResult {driven, scans, busyUs, driveDone, followDone}`. `scan(const MaskPlanes&, uint32_t& busyUs) -> bool` (Done).
- [x] `lib/GameDisplayLab/SlotScheduler.h` -- `MIN_PERIOD_US 100000`, `MARGIN_US 5000`; `start(nowUs)`, `due(nowUs)` (wrap-safe signed difference), `slotDone(slotStartUs, nowUs, busyUs)` → period = max(busy + margin, min), next = slotStart + period, skip whole periods already past (counted, never queued); `periodUs()`, `skipped()`; window stats (`record(scans, busyUs)`, scans/s in tenths and ms per scan over the window, `resetWindow(nowUs)`).
- [x] `lib/GameDisplayLab/TransitionScene.{h,cpp}` -- `PanelTurn` (the four `GfxRenderer` orientations) and `toPanel(turn, logical rect, panelW, panelH)` equal to `rotateCoordinates`; scene over the logical screen: four bands K,D,L,W top to bottom (height/4 each), each with three lanes (band/3) holding a 12-px-wide rectangle of the other three shades in K,D,L,W order, inset 8 px top and bottom in its lane; `rectX(elapsedMs, logicalW)`: -12 → logicalW over `SWEEP_MS 5000` (linear), hidden during `PAUSE_MS 1000`, cycle 6000; `targetAt(lx, ly, ms)`; `applyAll(model, ms, preset)` (each pixel retargeted once); `update(model, prevMs, nowMs, preset)` (new rect area to its shade, old-minus-new area to the band shade).
- [x] `test/game_display_lab/test_gray_model.cpp`, `test_gray_preset.cpp`, `test_slot_scheduler.cpp`, `test_transition_scene.cpp`, `CMakeLists.txt` -- cover every matrix row that has a pure part (Design Notes, Tests), plus a fake `scan` in `runGraySlot` tests naming what it stands in for (`DisplayLabActivity::scan`: Done = delivered; any other outcome = not delivered).
- [x] `src/activities/games/display_lab/GrayPresetFile.{h,cpp}` -- `PATH "/.crosshatch/display-lab.json"`; `load(GrayPreset&)` (Storage read, cap 4 KiB; Missing → `LOG_INF`, Malformed → `LOG_ERR`, both defaults) and `save(const GrayPreset&)` (`Storage.mkdir("/.crosshatch")`, write via `HalFile`, `LOG_ERR` + false on any failure); encode buffer on the heap (`makeUniqueNoThrow`), never per slot.
- [x] `DisplayLabActivity.{h,cpp}` -- rows FramePeriod, Polarity, TransitionTest, Tuning, SavePreset, Exit (+ descriptions); session `GrayPreset` loaded in `openSession()` (rate index ≥ `rateCount()` → 0 with `LOG_ERR`); public `preset()`, `savePreset()` (Save row subtitle "Saved" / "Save failed, values kept"), `scansBeforeClean()` (cap − sinceClean), `cleanNow()` (caller holds the render lock; existing `cleanPanel()`).
- [x] `TransitionTestActivity.{h,cpp}` -- UiListActivity: rows Run (subtitle status), Stats ("%u.%u scans/s, %u ms per scan"), Controls (help text). Behaviour in Design Notes.
- [x] `TuningActivity.{h,cpp}` -- rows: 12 doses, 12 holds, Retarget, Frame rate (backend `rateLabel`), Swap polarity, Save; label + current value subtitle; edits `lab.preset()` in place. Behaviour in Design Notes.
- [x] `lib/I18n/translations/english.yaml` -- new `STR_GAMES_DLAB_*` keys only.
- [x] `docs/crosshatch/display-lab.md` -- "Transition test and tuning" section: controls, reading the sweep (rectangle shade vs band = dose; track behind = hold/history; build-up over passes; endpoints), tuning order (doses and hold first, then retarget, then history), what to photograph, the preset file and its keys; code map entries.

**Acceptance Criteria:**
- Given the x4pro env, when the gate runs `build on`/`build off`, then neither build defines `FREEINK_CAP_DISPLAY_LAB` and `compare` refuses metadata that does.
- Given an X4 Pro with a scan backend, when the lab opens, then its menu shows Frame period, Polarity, Transition test, Tuning, Save preset, Exit, and the preset comes from `/.crosshatch/display-lab.json` or InkBoy's defaults.
- Given Run on the transition screen, when the sweep starts, then the panel is FULL-refreshed white, the bands are driven from white, and no screen repaint reaches the panel until the test view is left (Back, a non-Done outcome).
- Given `default`, `sticky` and the simulator, when built, then no display_lab screen code compiles and the launcher shows no lab row.

## Implementation Notes

- D6 is in `scripts/check_flash_budget.py` (`LAB_MACRO`, `LAB_UNFLAG`, both builds unflag the lab, `check_flag` refuses
  the lab define in either build, which `compare` and `objects` both call), its sidecar test, the CI comment,
  `game-icons.md` and the opening of `display-lab.md`. Not committed separately (the build session was told not to
  commit): those five files are the D6 commit if it is split out.
- Codec: `version` is required (absent counts as "version != 1", Malformed), since it is the only key without a
  default; unknown string, `null` and fractional values are skipped too; nesting deeper than 16 is Malformed. The
  file has no trailing newline (the documented text exactly).
- A weak-eligible pixel is set in both masks in the follow-up scan too (the same third row), not only the drive scan.
- The follow-up scan is always sent after a delivered drive scan, even when no pixel follows up: it is the
  HOLD-ending scan (Design Notes, checklist 6).
- `outcomeText(Scan)` moved from `FramePeriodActivity.cpp`'s anonymous namespace to a public static of
  `DisplayLabActivity`, shared by the frame-period and transition screens.
- Transition screen: NavPrevious's clean leaves the model reset and marks the scene stale, so the first slot after a
  resume retargets the whole scene at the paused clock (not only the rectangles' delta). `onExit` logs the speed
  without taking the render lock and promotes a FULL refresh when the test view was up.
- `tr()` is a macro over `StrId::id`, so every on/off and saved/failed choice is `cond ? tr(A) : tr(B)`.
- A direct run of the packaged cppcheck on the new files (not `pio check`) found only `shadowFunction` (locals named
  like the model's accessors, renamed) and a `constVariable`; both fixed.

- Matrix test audit (2026-10-16, after the patch round; 2,159 of 2,159 host tests passed): Dose lookup (both rows) by
  `GrayModel.BaseDoseOnAShadeIsTheTablesDose`/`BaseDoseBetweenShadesInterpolatesRoundedUp`; Retarget by
  `RetargetMidTransitionKeepsTheLevelAndReplacesRemaining`; Mask build by the three `MaskBuild*` tests and the weak
  tests; Commit by `GraySlot.*` (fake scan: Done = delivered); Transition sweep by `TransitionScene.RectangleSweepsThenPauses`
  and the `Update*` tests, `SlotScheduler.*`, and `GraySlot.FailedDriveCommitsNothingAndSendsNoFollowUp` for the stop
  decision; Preset load by `GrayPresetCodec.MissingFileGivesDefaults`/`MalformedTextGivesDefaults`; Preset save by
  `RoundTripKeepsEveryField` (the write itself, the `.tmp` replace and the failure path are device glue); Tuning edit
  by `GrayPresetTuning.*`. The Memory row and the screens' input handling are device-only (`CROSSHATCH_DISPLAY_LAB`,
  x4pro only, no host harness), as dlab-0's backend-choice rows: their evidence is the x4pro, default and sticky builds
  and review.
- Follow-up pass (2026-10-16): `TransitionTestActivity::loop()` stamps `lastLoopUs` at the end of each call; a gap
  over `COVERED_GAP_US` (1 s) while the test view is up means a screen was pushed over it (no `onExit` on a push), so
  the view is left as Back leaves it. `handleForcedRefresh()` returns true while the view is up. `turnOf` is a cast
  pinned by four `static_assert`s. The help strings say Next/Previous, since the nav axis flips with the orientation
  on a touch board.

## Plan Change Log

## Review Triage Log

### 2026-10-16 — Review pass
- lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment, each a context-free subagent over `diff-review1.patch` (183,401 B, the plan excluded).
- verdicts: 32 findings — high 0, medium 4, low 19, false 9, maybe-false 0
- findings:
  - `[false]` `[reject]` (intent) The preset codec is a std-only JSON subset, not ArduinoJson/PersistableStore. — The spine's layer table (ARCHITECTURE-SPINE.md:34, :38) and `scripts/check_layers.py` forbid `lib/Serialization`/`ArduinoJson.h` from `lib/GameDisplayLab` and Screens alike (the auditor confirmed), so ArduinoJson would fail CI; R6 asks for a host-tested preset codec, which this gives. The story's outcome (a JSON file at the path, defaults on missing/malformed, LOG_ERR) holds. Recorded as a deviation for the orchestrator.
  - `[false]` `[reject]` (intent) Tuning's NavNext/NavPrevious change a value only in Confirm's edit mode. — On the X4 Pro Up/Down are NavPrevious/NavNext, so a mode-less reading leaves no button to move between 28 rows; in edit mode NavNext +1 clamped and NavPrevious -1 hold as the matrix says (Design Notes).
  - `[false]` `[reject]` (intent) The "live" speed line is in the log during the sweep and on the row afterwards. — D4 and the story forbid repainting during the sweep; the row cannot update on the panel without breaking it.
  - `[false]` `[reject]` (intent, blind) D6 is not its own commit and the base is unrecorded. — Both commits are made at Finalize, D6 first; the base was measured on a fresh archive tree of `9b2317f3` + the D6 script and is recorded in Verification.
  - `[false]` `[reject]` (intent) D5's period uses the slot's summed busy time, not one scan's. — A slot is two scans (digest Q5 pair); a period below the slot's busy time would skip every slot. Design Notes record the reading.
  - `[false]` `[reject]` (intent) Extras: Save row in Tuning, outcomeText moved, CI comment, scene/slot units. — The story asks "Save writes the preset" in Tuning; the move removes a duplicate switch; the CI comment describes the changed gate (D6 asks the gate's description updated); the units are pure seams the plan lists.
  - `[low]` `[reject]` (intent) A tap wraps 12→1 while buttons clamp. — Wrapping is the only way to lower a value by tap (GameOptions pattern); the matrix's clamp is the button path, which holds.
  - `[medium]` `[patch]` (verification) Since D6 `objects` no longer reads the lab's screen objects (they compile empty in the games-on build). — Inherent to D6 (both builds unflag the lab); patched the docstring and display-lab.md to say the lab's screen sources are outside the scan. The coverage loss itself stays (residual risk).
  - `[low]` `[reject]` (verification) The ScanRequest a slot sends (frames 1, preset rate, swap) and the rate fallback are untested. — Device-only glue (x4pro only, no host harness), as dlab-0's deferred items 1-2; ScanRequest lives in src, so a pure helper would add public surface; checked by inspection.
  - `[low]` `[reject]` (verification) The session-cap pre-clean guard is untested. — Same device-glue boundary; two lines read in review; its timing fix is patched below.
  - `[low]` `[patch]` (verification) A weak pixel's follow-up bits are untested. — Added a test with W>L hold off and weakRow on.
  - `[medium]` `[patch]` (verification other) The `objects` docstring paragraph still says it reads all of src/activities/games. — Grouped with the `objects` entry; patched.
  - `[false]` `[reject]` (verification other) The D6 base figures are not recorded. — Recorded (Verification) from the fresh-tree run.
  - `[low]` `[patch]` (blind) The pre-slot cap clean skews the sweep clock, skipped count and scans/s. — Stats reported before the clean, fresh micros() after it as clock, slot start and window start.
  - `[low]` `[reject]` (blind) "Saved" stays on after later edits; unsaved edits are dropped on leaving. — The row reports the last save's outcome; tracking unsaved changes is new state and a new lab API the story does not ask for.
  - `[medium]` `[patch]` (blind) The preset save truncates the file before writing. — Writes `.tmp`, then `Storage.replaceFile` only after a full write and close.
  - `[low]` `[patch]` (blind) The preset is read from SD under the render lock. — Loaded before the lock in onEnter, assigned under it.
  - `[low]` `[patch]` (blind) A tap outside the old row areas does nothing; a swipe scrolls the hidden list. — Swipes consumed while the test shows; the doc names the row area and Confirm.
  - `[low]` `[reject]` (blind) History, routes and weak have no Tuning rows despite "every number editable". — The story names Tuning's rows (doses, holds, retarget, rate, swap) and makes history and weak optional and off; they stay editable in the preset file.
  - `[low]` `[patch]` (blind) Docs call history off and weak off "InkBoy's defaults". — Comment and doc now say those two are this story's choice.
  - `[low]` `[reject]` (blind) The gate's "on" build now rewrites the developer's `.pio/build`. — The plan keeps CI's layout and `objects`' build dir; the rebuild back hits the shared cache. Developer cost only.
  - `[false]` `[reject]` (blind) Grouped with the D6-commit row above (same claim). — See that row.
  - `[low]` `[patch]` (blind) Missing tests: a sweep-to-sweep jump without the pause, a jump wider than the rectangle, and GrayPresetFile's Storage paths. — Both scene cases added (the code was right on inspection: both branches return the whole old rectangle); the Storage layer is device-only glue with no host Storage, left to the hardware checklist.
  - `[low]` `[patch]` (blind) `skipped=` is neither per window nor per run. — Reset in resetWindow (per window), doc says so.
  - `[low]` `[patch]` (blind) GrayPresetFile hand-writes the directory check and mentions SdFat. — `Storage.ensureDirectoryExists`.
  - `[medium]` `[patch]` (edge) Non-atomic save. — Grouped with the blind save row.
  - `[low]` `[patch]` (edge) Cap clean skews the clock. — Grouped with the blind clock row.
  - `[false]` `[reject]` (edge) A screen whose area matches but whose dimensions do not match the orientation. — `GfxRenderer::getScreenWidth/Height` derive from the same panel size by orientation (GfxRenderer.cpp:1926), so the swap always matches.
  - `[low]` `[patch]` (edge) A tap outside the row hit areas does nothing. — Grouped with the blind tap/swipe row.
  - `[low]` `[patch]` (edge) Swipes scroll the hidden list. — Grouped with the blind tap/swipe row.
  - `[low]` `[reject]` (edge) skipNumber accepts malformed number tokens inside unknown keys' values. — Unknown keys are skipped by design; tightening the grammar adds code for hand-edited files nobody is likely to write that way.
  - `[low]` `[patch]` (edge) SD read under the render lock. — Grouped with the blind lock row.

### 2026-10-16 — Review pass (follow-up)
- lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment, each a context-free subagent over `diff-followup.patch` (`git diff 9b2317f3..fe2f6cef`, 191,929 B, the plan excluded); the intent auditor read the story file as the verbatim intent.
- verdicts: 29 findings — high 0, medium 5, low 15, false 9, maybe-false 0
- findings:
  - `[medium]` `[reject]` (blind) At D5's 100 ms slot floor the 5 s sweep moves a 12-px rectangle about 16 px a slot at 800 px (9.8 px at 480), so a pixel is under it for one slot or so (1-2 doses against W>K's 7) and never settles on the rectangle's shade. — Real by arithmetic, but the intent fixes the numbers: the story and R4 say 12-px rectangles crossing in 5 s (InkBoy's tuning.md, digest line 35, also reads "motion blur" and "build-up over passes"). A different speed needs an owner Decision; raised for the orchestrator under Residual risks.
  - `[false]` `[reject]` (blind) The period ignores the slot's CPU time, so nearly every slot counts as skipped. — `SlotScheduler::slotDone` counts `whole = (now - (start + period)) / period`, which is 0 until the CPU time reaches period + 5 ms (>= 105 ms); below that the late slot runs at once and nothing is skipped. The unmeasured walk time is already a residual risk.
  - `[low]` `[patch]` (blind) A weak pixel set in both masks cites "MaskPlanes, D1", but `ScanRequest` says D1 leaves a both-masks pixel undefined (the SSD1677 holds it). — The comments in `GrayModel.cpp`/`GrayModel.h` now say both masks are the weak row only of a backend whose `supportsWeakRow()` is true, and that D1 itself leaves it undefined. `ScanBackend.h` is fixed by the story; every backend returns false today.
  - `[low]` `[reject]` (blind) "Saved" stays on after later edits; unsaved edits are dropped without a prompt. — carried: the row reports the last save's outcome; tracking unsaved changes is new state and a lab API the story does not ask for.
  - `[false]` `[reject]` (blind) The gate's measurement changed but no base was re-measured, and the engine's size is reported nowhere. — Both are in this plan's Verification (the D6 base from a fresh archive tree, the engine's +18,576 B), where the story asks for them; the `game-icons.md` table columns predate the display lab, so none of them includes it.
  - `[low]` `[reject]` (blind) The gate's "on" build overwrites the developer's `.pio/build`; `LAB_MACRO`/`LAB_UNFLAG` are not in `fork_common`. — carried for the build dir (developer cost only; the shared cache makes the rebuild back cheap). The constants are used by the gate alone, so `fork_common` has no second caller to share them with.
  - `[medium]` `[patch]` (blind) Since D6, `objects` no longer reads the lab's screen sources. — carried: patched in the first pass (the docstring and display-lab.md say so); the coverage loss stays a residual risk. Not patched again.
  - `[low]` `[reject]` (blind) The JSON reader takes non-JSON: loose number tokens in unknown keys, leading zeros, raw control characters, `\u` read literally. — carried for `skipNumber`; the rest are the same leniency on a hand-edited dev file: each yields a defined value or an unknown (skipped) key, never a wrong known value, and rejecting them adds parser branches.
  - `[low]` `[reject]` (blind) A failed save leaves `/.crosshatch/display-lab.json.tmp` on the card. — Real but harmless: nothing reads the `.tmp`, and the next save truncates it on open; a failed save is rare on a dev bench.
  - `[false]` `[reject]` (blind) The preset adds a new top-level `/.crosshatch` directory, unlisted in the formats docs. — The epic names `/.crosshatch/display-lab.json` (epic-display-lab.md:32); display-lab.md documents the file and its keys; `docs/crosshatch/formats.md` covers game byte formats only.
  - `[low]` `[patch]` (blind) The help text names Up and Down, which are not always NavNext/NavPrevious. — On a touch board the nav axis flips in inverted portrait and counter-clockwise landscape (`MappedInputManager::isNavDirectionSwapped`), so "Down replays" was wrong there. `STR_GAMES_DLAB_TRANSITION_CONTROLS` and `STR_GAMES_DLAB_EDITING` now say Next and Previous; display-lab.md names the keys behind each and the flip.
  - `[low]` `[patch]` (blind) `allocate` checks only the screen's area; `ToPanelEqualsTheRenderersRotation` compares against a hand copy of `rotateCoordinates`. — The area half is carried `false` (`getScreenWidth/Height` derive from the panel size by orientation, GfxRenderer.cpp:1926). The copy half is grouped with the verification-gap `turnOf` entry below.
  - `[low]` `[reject]` (blind) The session-cap guard and `runSlot`'s clock and stats handling have no tests. — carried: device-only glue (x4pro dev env only, no host harness), as dlab-0's deferred items; read again in this pass with no defect found.
  - `[low]` `[patch]` (blind) display-lab.md puts the budget's range "(1..12)" where the percent's belongs; `GrayModel.h` calls `source` "the shade its dose was read from". — The doc now says "(clamped to 1..12)"; the header says `source` is the shade nearest the level at the last retarget, which picks the hold bit (`followsUp`, GrayModel.cpp:162).
  - `[medium]` `[patch]` (edge) The top-edge light-panel swipe pushes `FrontlightPanelActivity` over a running test view without `onExit`. — Traced: `ActivityManager::loop` (ActivityManager.cpp:132-135) pushes it before the activity's loop on any screen; the overlay draws over the drive, and after the pop `render()` returns early while `shownOnPanel`, so its remnants stay and the sweep resumes with a jumped clock over a desynced panel. Patched: `loop()` notes the end of each call, and a gap over `COVERED_GAP_US` (1 s) while the view is up leaves the view (the rows' FULL refresh cleans the overlay). The same root cause has a second path, the forced refresh (main.cpp `renderer.displayBuffer(HALF_REFRESH)` unless `handleForcedRefresh()`), which would send the white framebuffer over the drive: `handleForcedRefresh()` now returns true while the view is up.
  - `[medium]` `[reject]` (edge) A rectangle step wider than `RECT_W` leaves gap columns that never get the lane's shade. — Same arithmetic as the blind sweep-speed row; the intent fixes 5 s and 12 px, so grouped with it and rejected there.
  - `[low]` `[reject]` (edge) A UTF-8 BOM at the start of the preset makes it Malformed. — Loud, not silent: LOG_ERR "malformed" and InkBoy's defaults; editors rarely add a BOM to UTF-8 today, and stripping one adds a branch to the codec.
  - `[medium]` `[patch]` (edge) Acceptance "no screen repaint reaches the panel until the test view is left" fails when the light panel is pushed. — Grouped with the light-panel row; the same patch.
  - `[low]` `[patch]` (verification) `turnOf` is untested, and the scene's rotation is pinned against a frozen copy of `rotateCoordinates`. — `turnOf` is now a cast whose four `static_assert`s pin each `GfxRenderer::Orientation` to the same-named `PanelTurn`, so the x4pro build fails on a mismatch. The copy half stays: pinning the real function means exposing upstream's file-static `rotateCoordinates`, an upstream touch; residual risk on an upstream merge.
  - `[low]` `[reject]` (verification other) The first pass's reason "no host Storage" is not quite right: `test/game_script/harness/stubs/HalStorage.h` is a fake card. — True, but the fake lacks `readFileToString` and `replaceFile`, and `GrayPresetFile` compiles only with the lab on, so the conclusion holds (the lens agrees); the fix would be an edit to this plan.
  - `[low]` `[reject]` (intent) The screens, the Tuning buttons, the Memory row and `GrayPresetFile` are exercised by no test; the matrix rows live at the device surface. — carried: device-only glue with no host harness; the evidence is the builds and the owner's hardware run.
  - `[false]` `[reject]` (intent) Tuning's NavNext moves the selection unless Confirm was pressed; a tap wraps 12 → 1. — carried, both rows (the modal reading and the tap wrap) with their recorded reasons.
  - `[false]` `[reject]` (intent) The live speed line shows in the log during the sweep and on the row afterwards. — carried: D4 forbids repainting during the sweep.
  - `[false]` `[reject]` (intent) The preset codec is a std-only JSON subset, not ArduinoJson/PersistableStore. — carried; now also the orchestrator's decision (Design Notes).
  - `[low]` `[patch]` (intent) The weak row is encoded as both masks, which `ScanBackend.h` says D1 leaves undefined. — Grouped with the blind weak-contract row; the same comment patch.
  - `[false]` `[reject]` (intent) A failed follow-up after a delivered drive scan still commits one dose. — That is the matrix's per-scan rule ("commit: scan delivered → each driven pixel advances; a Dropped/Refused/TimedOut scan commits nothing"), the auditor's reading E1; the sweep still stops on that outcome.
  - `[false]` `[reject]` (intent) Behaviour not in the intent: the pre-emptive cap clean, the `outcomeText` move, `TransitionScene`, the launcher PNG. — The cap clean keeps the lab's own cap clean from landing between a slot's two scans (Design Notes); the move and the scene are carried from the first pass's extras row; the PNG is the screenshot the build brief requires the story to commit.
  - `[low]` `[reject]` (intent) The owner's "every number editable on the device" against history, routes and weak being file-only. — carried: the story names Tuning's rows and makes history and weak optional and off.
  - `[false]` `[reject]` (intent) Whether the D6 base and the engine's size were recorded is not established by the diff. — carried: both are in Verification.

## Design Notes

- **Settled by sources:** D1 (shape), D4 (no drawing during a drive), D5 (slot = max(busy + 5 ms, 100 ms)), D6 (gate), the owner's 2026-10-16 defaults decision (InkBoy's numbers, all editable), digest Q1-Q6 (model, follow-up rule, commit, retarget, routes, weak eligibility, transition test). One slot = one drive scan plus one follow-up scan (InkBoy's default pair hold, digest Q5 "drive scan, follow scan, complete_pair"; checklist 6 "HOLD-ending scan after the last drive"); its busy time is the sum of both, which D5's period uses. Each scan has `frames = 1` (a dose is one one-frame activation).
- **Orchestrator decision (2026-10-16, follow-up dispatch; not to revisit):** the preset codec stays std-only, not ArduinoJson as the story's Approach says, because the layer check (`scripts/check_layers.py`, the spine's layer table) forbids ArduinoJson in `lib/GameDisplayLab`.
- **No ArduinoJson (deviation from the story's wording):** the spine's layer table (ARCHITECTURE-SPINE.md:34, :38, owner-amended) lets `lib/GameDisplayLab` include the standard library only and Screens nothing in `lib/Serialization`; `scripts/check_layers.py` enforces it in CI and is outside the epic's paths. R6 asks for a host test of "the preset codec", which ArduinoJson (absent from the host suite) would prevent. So the codec is a small std-only JSON subset (objects, arrays, integers, booleans; unknown keys skipped; nested values skipped) in `GrayPreset.cpp`, fully host-tested, and the file is still JSON at the story's path. Format (absent keys keep defaults; any present key of the wrong type or out of range, a parse error, or `version` ≠ 1 → Malformed):
  `{"version":1,"doses":[2,4,7,4,1,6,4,1,5,7,3,2],"hold":[true,true,false,false,true,false,false,true,false,false,false,true],"retarget_percent":100,"rate":0,"swap_polarity":false,"history":0,"return_delta":[-1,0,0,0,0,0],"reverse_percent":[150,0,0,0,0,0],"weak_white_to_light":false}`. Routes in order LKL, DKD, DWD, LWL, DLD, LDL; ranges: dose 1..12, retarget 50..150, rate 0..255 (checked against the backend at load), history 0..3, return delta -11..11, reverse 0..250 (0 = follow retarget).
- **Model rules** (digest excerpts): `base = ceil((dose[low][t]*(64-f) + dose[high][t]*f) / 64)`, `low = level/64`, `f = level%64`, `high = low<3 ? low+1 : low`; base 0 only when level == t*64. Retarget: `budget = old.remaining && base ? clamp(ceil(base*pct/100), 1, 12) : base`; `source = min(3, (level+32)/64)`. History (when allocated and `historyMode` ≠ 0): route exists when origin ≠ none and origin == new target, via = old target; `steps` = blackSteps when via is darker than origin, else whiteSteps; completed return (`!old.remaining`, bit0, level == via*64, steps == dose[origin][via]) → `clamp(base + returnDelta, 1, 12)` when delta ≠ 0; reversal (`old.remaining && base`, bit1, reversePercent ≠ 0) → `clamp(ceil(base*rev/100), 1, 12)`; then origin = level/64 when (new target is K or W, or DLD/LDL is non-neutral) and the pixel was settled on a shade, else none; steps reset. Commit counts each dose into black/white steps (saturating). Weak: eligible when weak bitmap present, new target L, old target W, settled at 192; cleared by any other retarget; `buildMasks(weakRow=true)` sets an eligible darkening pixel in both masks. The ceil of the scaled budget is our choice (the digest does not show InkBoy's `scaled` table); at 100 % it is the identity.
- **Transition screen behaviour:** Run (rows view) → `renderer.clearScreen(0xFF)` and `lab.cleanNow()` under `RenderLock`, `model.reset(W)`, `scene.applyAll(0)`, shownOnPanel = running = true, scheduler started. While shown, `render()` draws nothing; `handleCustomInput()` takes Confirm (pause/resume), NavNext (replay: clean + elapsed 0 + run), NavPrevious (clean: white panel, model reset to W, paused), Back (leave the test view); any routed tap pauses/resumes (activateIndex while shown). `loop()` runs at most one slot when `scheduler.due(now)`: if `lab.scansBeforeClean() < 2`, clean first (white framebuffer, `cleanNow`, reset, `applyAll(elapsed)`); `scene.update(prev, now)`; `runGraySlot` with a lambda building `ScanRequest{masks.black(), masks.white(), 1, preset.rateIndex, preset.swapPolarity}` through `lab.scan` and remembering the last non-Done `Scan`; a non-Done outcome stops the sweep and leaves the view with its outcome text. Leaving the view: running = shownOnPanel = false, `promoteNextRefresh(FULL_REFRESH)`, `requestUpdate()`. `skipLoopDelay()` and `preventAutoSleep()` are true while running. The elapsed clock pauses while paused. Log `DLAB gray scans_s=%u.%u ms_per_scan=%u skipped=%lu` about every 2 s while running and on stop. `onExit` frees every buffer (no RenderLock).
- **Tuning behaviour:** a tap on a value row steps it in place (+1 wrapping, holds and swap toggle) as `GameOptionsActivity` does; Confirm on a value row enters edit mode for that row (its subtitle marks it), where NavNext/NavPrevious change the value (+1/-1, clamped, the matrix row) and Confirm or Back leaves edit mode; outside edit mode NavNext/NavPrevious move the selection (UiListActivity's base contract: the X4 Pro's Up/Down are NavPrevious/NavNext, so without a mode no button could move between 28 rows). Save (tap or Confirm) calls `lab.savePreset()`. Changes apply to the session preset, which the next sweep reads.
- **Memory (X4 Pro 800×480):** model 768,000 B + follow masks 96,000 B PSRAM per transition screen; history 1,152,000 B only when `historyMode` ≠ 0, weak 48,000 B only when weak is on and the backend supports a weak row; plus dlab-0's 192,000 B. Mask build and commit walk every pixel each slot (no active bitmap); unmeasured on device.
- **Tests:** dose table and 0xc667 against the digest; interpolation at 0, 32, 63, 64, 160, 192; `level` sequence of a W→L pixel (192, 160, 128) and a retarget at 160 keeping level and replacing remaining; mask build per rule incl. follow-up and weak both-bits; commit 1 vs 2 doses and nothing on a failed drive (through `runGraySlot` with the fake scan); scheduler period, wrap, skip; scene `toPanel` vs a copy of `rotateCoordinates` for all four turns, rect x at 0/2500/4999/5000/5999/6000 ms, update leaves unchanged pixels untouched; codec round trip, missing, malformed (parse error, range, type, version), unknown keys skipped, too-small buffer; tuning steps and clamps.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass, new suites included.
- `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py`, each `python3 scripts/*_test.py` -- expected: exit 0.
- `./bin/clang-format-fix` twice -- expected: the second run leaves `git status` unchanged.
- After review, with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, under `flock /tmp/crosshatch-build.lock`: `pio run -e x4pro` (record firmware.bin and RAM against the base below), `pio run -e default`, `pio run -e sticky`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` with and without `-e x4pro`; `scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`; `.claude/skills/run-crosshatch-player/sim.sh build x4pro` and a launcher screenshot (lab compiled out: no row expected).
- D6 base (before the engine is measured): the gate's `build on`/`build off`/`compare` on a fresh archive tree of the baseline commit plus the D6 script, recorded here; the gate once more on a fresh archive tree of the final commit (CI-gate rule).
- Base plain `pio run -e x4pro` at `9b2317f3` (worktree, 2026-10-16, before any change): firmware.bin 6,226,176 B; RAM 104,232 B (31.8 %).
- Hardware: none (owner: the transition test on the UC8179 unit after lane A lands).

**Results (2026-10-16):**
- D6 base, re-taken with the D6 gate (epic Measurement; AGENTS.md: a changed gate re-measures its base): a fresh `git archive` tree of `9b2317f3` plus every submodule (recursive), with the D6 `check_flash_budget.py` copied in (the D6 commit's script before the review, which changed only its docstring), `build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`: games on 6,215,440 B, games off 5,957,248 B, **+258,192 B, 18,288 B to spare**; static internal RAM +784 B, **240 B to spare**; objects: 57 checked, no problems. Under the old counting dlab-0 left 7,552 B: the lab no longer counts.
- Host: 2,159 of 2,159 pass after the patch round (`test_scan_lut` 95). All `scripts/*_test.py` exit 0; `check_layers.py` passes; `check_upstream_touches.py` PASS; `./bin/clang-format-fix` twice, the second run changed nothing.
- `pio run -e x4pro` (worktree, after review): rc 0; firmware.bin **6,244,752 B, +18,576 B over the base's 6,226,176 B** (the engine and its screens, lab on, measured the same way at both commits); RAM 104,232 B, +0. Rebuilt from an empty build dir after the package repair below: the same 6,244,752 B.
- `pio run -e default`: rc 0 (lab compiled out; `lib/GameDisplayLab` compiles on the C3 through the build anchor).
- `pio check -e x4pro --fail-on-defect low/medium/high`: PASSED, no defects.
- Gate in the worktree after review: games on 6,217,232 B, off 5,959,040 B, **+258,192 B, 18,288 B to spare (+0 against the D6 base)**; RAM +784 B, 240 B to spare (+0); objects: 63 checked, no problems. Both images grew 1,792 B (the 20 new `STR_GAMES_DLAB_*` strings are in every build).
- `sim.sh build x4pro`: SUCCESS. Screenshot `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-gray-screenshots/launcher-no-lab-row.png`: Home → Games in the simulator, no Display lab row (the lab is compiled out there), as expected.
- Fresh tree (CI-gate rule): a `git archive` tree of `f8a89c01` plus every submodule (recursive), the gate's four commands only: games on 6,217,232 B, off 5,959,040 B, **+258,192 B, 18,288 B to spare**; RAM +784 B, 240 B to spare; objects 63, no problems; rc 0. Equal to the worktree run. (The amend that recorded this changed only this plan, which the gate does not read.)
- **Not run in the first pass (run in the follow-up below):** `pio run -e sticky` and `pio check` on `default`. The first sticky attempt died on a full disk (other lanes' builds share it), and the `pio check -e x4pro` that followed hit the disk-full mid-way through the platform's framework reinstall, leaving the shared `~/.platformio/packages/framework-arduinoespressif32-libs` half-installed (a 0-byte `package.json`; every env failed with `MissingPackageManifestError`). I repaired it under the build lock (removed the broken package and stale `~/.platformio/.cache/tmp/pkg-installing-*`, reinstalled through `pio run -e x4pro`: manifest valid, 2.1 GB, all chips). `sticky` and `default` use `custom_sdkconfig`, whose framework reinstall-and-compile needs about twice that package's size free; with the disk at 2-3 GB I did not start it again. CI builds all seven envs and runs cppcheck on `default` on the epic PR; the `default` build above already compiled every changed shared file.

**Results, follow-up pass (2026-10-16, after its patches; worktree, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, each step under `flock /tmp/crosshatch-build.lock`; logs in the orchestrator scratchpad `dlab-c1/fu-*.log`):**
- Host: 2,159 of 2,159 pass. All `scripts/*_test.py` exit 0; `check_layers.py` passes; `check_upstream_touches.py` PASS; `./bin/clang-format-fix` twice, the second run changed nothing.
- `pio run -e x4pro`: rc 0; firmware.bin **6,244,928 B** (+176 B over the first pass's 6,244,752 B, the follow-up's patches; +18,752 B over the base's 6,226,176 B, both plain builds); RAM 104,232 B, +0.
- `pio run -e default`: rc 0.
- `pio run -e sticky`: **rc 0** (the check the first pass could not run).
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (`default`): **PASSED, no defects** (the check the first pass could not run).
- `pio check -e x4pro` with the same flags: PASSED, no defects.
- Gate (`build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`) in the worktree: games on 6,217,232 B, off 5,959,056 B, **+258,176 B, 18,304 B to spare** (-16 B against the first pass's +258,192 B: the shorter help strings are in both images and the off image's padding moved); RAM +784 B, 240 B to spare; objects 63, no problems. The follow-up changes no gate, so no fresh tree.
- `sim.sh build x4pro`: SUCCESS (the lab is compiled out there; the committed launcher screenshot stands).

## Auto Run Result

**Summary:** InkBoy's per-pixel grayscale engine now runs over the D1 `ScanBackend` seam, and epic D6 takes the dev-only lab out of the flash gate.
- Pure parts in `lib/GameDisplayLab`, all host-tested:
  - `GrayModel`: 2 B per pixel, the dose interpolation, retargets, history routes, the weak row, and the mask build and commit.
  - `GrayPreset`: InkBoy's table, hold mask `0xc667` and retarget percent, the tuning steps, and a std-only JSON codec.
  - `SlotScheduler` (D5), `TransitionScene`, and `runGraySlot`.
- Two new screens: `TransitionTestActivity` (the sweep, which never repaints while running; pause, replay and clean) and `TuningActivity` (28 rows, tap to step, Confirm to edit).
- The lab menu gains Transition test, Tuning and Save preset.
- The preset lives at `/.crosshatch/display-lab.json`, written atomically through `Storage`.

**Deviations from the story's wording (Design Notes):**
- The preset codec is not ArduinoJson. The spine's layer table and `check_layers.py` forbid ArduinoJson in both places the codec could live.
- Tuning's NavNext/NavPrevious change a value inside Confirm's edit mode. On the X4 Pro those keys are Up/Down, so without a mode they could not also move between rows.
- The "live" speed line is live in the log. The screen shows it after the sweep stops, because D4 forbids repainting during it.

**Files:**
- `scripts/check_flash_budget.py`, `check_flash_budget_test.py`, `.github/workflows/crosshatch-ci.yml` (comment), `docs/crosshatch/game-icons.md`: D6 (both builds unflag the lab; `compare`/`objects` refuse a lab define; `objects` no longer sees the lab's screens).
- `lib/GameDisplayLab/GrayModel.{h,cpp}`, `GrayPreset.{h,cpp}`, `GraySlot.h`, `SlotScheduler.h`, `TransitionScene.{h,cpp}`: the engine.
- `test/game_display_lab/test_gray_model.cpp`, `test_gray_preset.cpp`, `test_slot_scheduler.cpp`, `test_transition_scene.cpp`, `CMakeLists.txt`: 53 new tests.
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}`, `TuningActivity.{h,cpp}`, `GrayPresetFile.{h,cpp}`: screens and the preset file.
- `DisplayLabActivity.{h,cpp}`, `FramePeriodActivity.cpp`: new rows, session preset, `cleanNow`/`scansBeforeClean`, shared `outcomeText`.
- `lib/I18n/translations/english.yaml`: 20 `STR_GAMES_DLAB_*` keys (ledger row 2).
- `docs/crosshatch/display-lab.md`: "Transition test and tuning", the preset file, the gate note.
- `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-gray-screenshots/launcher-no-lab-row.png`: the simulator's Games list with no lab row.

**Review (thorough, four lenses):** 32 findings: high 0, medium 4, low 19, false 9.
- **Patched (9 entries):** 2 medium and 7 low.
  - Medium: `objects` no longer reads the lab's screens (documented); the preset save is atomic (`.tmp` plus `replaceFile`).
  - Low: the cap clean no longer skews the sweep clock and stats; the preset is read outside the render lock; swipes are consumed in the test view; the defaults wording is corrected; new scene and weak-follow-up tests; `skipped` is per window; `ensureDirectoryExists`.
- **Deferred:** none.
- **Rejected:** the rest, each with its reason in the Review Triage Log:
  - the intent auditor's divergences, each recorded in Design Notes;
  - device-glue verification gaps, which have no host harness, as in dlab-0;
  - "Saved" staying on after later edits;
  - no Tuning rows for history and weak (the story names Tuning's rows);
  - the gate's "on" build sharing `.pio/build`;
  - `skipNumber` accepting loose number tokens inside unknown keys.

**Follow-up review:** recommended (`true`). Two medium entries were patched after the lenses ran. The atomic save path (`GrayPresetFile::save` with `replaceFile`) and the cap-clean clock handling in `TransitionTestActivity::runSlot` have had no independent review.

**Verification:** see Verification → Results.
- **D6 base:** +258,192 B of games delta, 18,288 B to spare; RAM 240 B to spare.
- **Engine:** outside the gate (+0 there). The plain x4pro image grows +18,576 B, static RAM +0.
- **Not run:** `sticky` and `pio check default`, because of disk-full and the shared-package repair; CI covers both.

**Residual risks:**
- **Untested on any panel.** The device glue (`ScanRequest` from the preset, the rate fallback, the cap pre-clean, the input mapping, PSRAM failure) is checked by inspection and the build only.
- **Unmeasured slot time.** Each slot walks all 384,000 pixels twice in PSRAM.
- **Choices to confirm on hardware:** the follow-up scan is always sent after a delivered drive scan, and the slot period uses the slot's summed busy time.
- **Dev-only code outside AD-2's scan.** Since D6 the lab's screen code is outside `objects`' static scan; `lib/GameDisplayLab` is still read.
- **Shared package was repaired.** The framework-libs package was reinstalled by this lane; any other lane whose build failed around 07:52 with `MissingPackageManifestError` should retry.

### Follow-up review pass (2026-10-16)

**Summary:** a fresh four-lens review of `9b2317f3..fe2f6cef` with the plan as intent: 29 findings (high 0, medium 5, low 15, false 9). It found one real gap in the transition screen's "no repaint during the sweep" contract (D4). It also found smaller comment, doc and help-text errors. All are patched in one follow-up commit. It also ran the two checks the first pass lost to the full disk: `sticky` builds and `pio check` on `default` passes.

**Files:**
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}`:
  - a screen pushed over the running view (the light panel's top-edge swipe) now ends the view, whose FULL refresh cleans the overlay;
  - a forced refresh is swallowed while the view is up;
  - `turnOf` is a cast pinned by `static_assert`s.
- `lib/GameDisplayLab/GrayModel.{h,cpp}`: comments (`source`'s meaning; the weak row is a convention only for a backend whose `supportsWeakRow()` is true, which D1 leaves undefined).
- `lib/I18n/translations/english.yaml`: the transition controls and Tuning edit help say Next/Previous, not Up/Down.
- `docs/crosshatch/display-lab.md`: the keys behind Next/Previous and their flip, the covered-view and forced-refresh behaviour, and "clamped to 1..12".

**Review breakdown:**
- **Patched (5 entries):** 1 medium (the light-panel overlay and forced refresh), 4 low (the weak-row comments, the help strings, the doc range and `source` comment, `turnOf`).
- **Deferred:** none.
- **Rejected:** the rest, each with its reason in the Review Triage Log:
  - the sweep-speed arithmetic (the intent fixes 5 s and 12 px);
  - `skipped` miscounting (false);
  - the carried rows (Saved status, the gate's build dir, `objects` coverage, `skipNumber`, device glue, Tuning's mode and wrap, the live line, ArduinoJson, editability, D6 figures);
  - the `.tmp` leftover;
  - `/.crosshatch` (the epic names it);
  - the BOM;
  - the per-scan commit rule;
  - the extras;
  - the first pass's "no host Storage" wording.

**Follow-up review recommendation:** `false`. No high was patched in this pass (1 medium, 4 low), so the work has converged.

**Verification:** see Verification, "Results, follow-up pass". `sticky` rc 0; `pio check` on `default` PASSED with no defects; x4pro, default, `pio check -e x4pro`, the gate (+258,176 B, 18,304 B to spare; RAM 240 B to spare) and the sim build all pass.

**Residual risks (added by this pass):**
- **Sweep speed on this panel.** At D5's 100 ms slot floor, a 12-px rectangle crossing in 5 s moves about 9.8 px a slot in portrait and 16 px in landscape. A pixel under it gets one or two doses, against up to 7 for W>K. So the rectangles will likely never reach their shade on the X4 Pro, and gap columns appear in landscape. The story and R4 fix these numbers. If the owner's hardware run confirms the problem, a slower sweep or wider rectangles needs an epic Decision.
- **Covered-view detection is a timing heuristic.** A screen pushed over the view and closed within 1 s goes unnoticed. The light panel cannot realistically open and close that fast.
- **The scene's rotation is pinned against a copy of `rotateCoordinates`.** An upstream change to it would not fail the test.


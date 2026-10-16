---
title: 'dlab-u1: the UltraChip stock-path bench'
type: 'feature'
ticket: 'story-dlab-u1-ultrachip-stock-bench'
created: '2026-10-16'
status: 'built'
baseline_revision: '9b2317f3106ec2da8b057bf4175a8d423d5afaa3'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-u1-ultrachip-stock-bench.md'
  - '{project-root}/docs/contributing/touch-and-ui.md'
warnings: ['oversized']
deferred:
  - summary: >-
      The transition screen's fell-back factor (2 x the stock FAST reference) is uncalibrated; if the armed transition itself takes 2 x a FAST DU or more, every armed flip is flagged.
    evidence: |-
      The transition is a DTM2 stream, a 25-frame XTF_PRE_BW_MID activation and a DTM1 stream; the reference is an OTP FAST DU whose length is unknown. The owner's UC8179 timings (reference, flips, and the FULL repaint) settle it.
    location: >-
      lib/GameDisplayLab/StockBench.h (FELL_BACK_FACTOR)
    severity: medium (unverified)
  - summary: >-
      The flash-budget gate (`check_flash_budget.py compare --limit-kib 270`) is over budget by 416 B with this story: games delta +276,896 B against the 276,480 B limit.
    evidence: |-
      The overage is the display lab's dev-env-only code, which the gate still counts. The epic's D6 (2026-10-16) has the gate build both images with FREEINK_CAP_DISPLAY_LAB unset, and until then tracks the lab's cost in the epic file. Once D6 lands, the gate should pass, since only the launcher's subtitle helper ships. The epic PR must not merge while the gate is red.
    location: >-
      scripts/check_flash_budget.py (epic D6)
    severity: high
  - summary: >-
      No test covers which bench a panel gets: DisplayLabActivity::available(), usesStockBench (a backend wins), labRowSubtitle and the stock menu dispatch.
    evidence: |-
      No host harness compiles src/activities/games/display_lab (CROSSHATCH_DISPLAY_LAB is 0 off device), as with dlab-0's deferred guard-order item; dropping `|| usesStockBench(c)` or `!hasScanBackend` passes every test. Settled by the owner's launcher check on a UC8179 and an SSD1677, or a pure gate function with a test.
    location: >-
      src/activities/games/display_lab/StockBenchGate.cpp, DisplayLabActivity.cpp
    severity: medium (unverified)
---

<intent-contract>

## Intent

**Problem:** On the X4 Pro's UC8179 and UC8279 batches the one-frame scan bench cannot run (no facade path to the waveform registers yet), so the display lab is hidden there; but the facade already exposes two UltraChip paths worth measuring on the owner's UC8179 now: the stock non-flashing previous-to-current transition (`displayGrayscaleBase(FAST_REFRESH)`) and the OEM four-level overlay grayscale.

**Approach:** On an X4 Pro whose controller is UC8179 or UC8279 and has no scan backend, show the Display lab row (subtitle "UltraChip stock-path bench") and a lab menu of Transition timing, Gray ramp, Exit instead of the scan screens. Transition timing times one stock FAST refresh (the reference), arms the transition with one gray pass, then times 10 checker flips through the transition, flags fall-backs, and shows min/median/max. Gray ramp paints four bands K/D/L/W through the overlay-mask planes, times base and gray pass, and holds the panel until the next activation. Pure parts in `lib/GameDisplayLab/StockBench`, host-tested.

## Boundaries & Constraints

**Always:** public `HalDisplay` calls only (`display.*`; no `eink()`); keep the launcher's entry warning, the inverted refusal (lab `onEnter` and per run), the 15,000 ms timeout (`DisplayLabActivity::SCAN_TIMEOUT_MS`), and one FULL refresh on every way out of a screen that drove the panel; log every timing with `LOG_INF("DLAB", …)`; new strings as `STR_GAMES_DLAB_*` in `english.yaml` only; PSRAM planes via `HalMemory::allocatePsram` in the screen's `onEnter`, freed in `onExit`; no per-run heap allocation; locals under 256 B; no static initializer or mutable static over 64 B (the flash-budget `objects` check); every new device file behind `CROSSHATCH_DISPLAY_LAB`; C3 `default` and `sticky` still build.

**Never:** no custom LUT, no register write, no `freeink-sdk/` or `.agents/` change; no change to `ScanBackend.h`, the SSD1677 backend, `FramePeriodActivity`, `PolarityActivity`, `ScanBackends.cpp`; no new ledger row; no edit of generated I18n files.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Gate | X4 Pro, UC8179 or UC8279, no scan backend | launcher row shown, subtitle "UltraChip stock-path bench"; menu Transition timing, Gray ramp, Exit | — |
| Gate | SSD1677 | unchanged (scan screens, subtitle "Panel waveform bench") | — |
| Transition run | Run | ref FAST timed, arm gray pass timed, 10 flips timed; `DLAB transition ref ms=`, `DLAB arm ms=`, 10 × `DLAB transition n=<i> ms=<ms> flashed=<yes/no>`; rows show each, summary min/median/max | a call over 15,000 ms ends the run, logs `DLAB timeout: …`, the next repaint is FULL |
| Transition, no overlay grayscale | caps unsupported | arm skipped (logged), every flip `flashed=yes` | — |
| Gray ramp | Run, overlay supported | base then gray pass; `DLAB gray base_ms=<> gray_ms=<>`; panel held; next activation repaints with FULL | over the timeout: log, FULL now |
| Gray ramp | overlay unsupported | notice row text, `DLAB gray unsupported`, no pass | — |
| Inverted at run | `display.isInverted()` | nothing sent, status "Turn off the inverted display first" | — |
| Exit | Back, Home, sleep after a drive in this visit | exactly one FULL refresh (Back: promoted onto the menu repaint; other routes: in `onExit`) | — |

</intent-contract>

## Code Map

- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- `available()` (gate), `onEnter` (notice, `openSession`), `listCount`, `buildRows` (the `set` lambda + three `set(Row::…)` lines), `activateIndex` switch, `openScreen<T>` (constructs `T(renderer, mappedInput, *this)`), `rowItems[ROW_COUNT]`. Lane dlab-c1 edits the same menu rows in parallel: touch only what is listed in the tasks.
- `src/activities/games/display_lab/PolarityActivity.{h,cpp}` -- the pattern to copy for a held panel: `std::atomic<bool> shownOnPanel`, `render()` override that draws nothing while set, next activation clears it, `renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH)`, `requestUpdate()`; `buildScreen` boilerplate. `FramePeriodActivity.cpp` -- the Run-row + result-rows table pattern and a blocking `run()`.
- `src/activities/games/display_lab/ScanBackends.h` -- `hasScanBackend(controller)`; read-only.
- `src/activities/games/GamesLauncherActivity.cpp:430` -- `item.subtitle = tr(STR_GAMES_DLAB_ROW_DESC);` compiled in every build (host harness `test/game_script/harness/GamesLauncherTest.cpp:345` expects that text with the lab compiled out). Include block under `#if CROSSHATCH_DISPLAY_LAB` at line 19.
- `lib/hal/HalDisplay.h` -- `displayBuffer(mode, false)`, `displayGrayscaleBase(RefreshMode, bool)`, `copyGrayscaleBuffers(lsb, msb)`, `displayGrayBuffer(bool)`, `cleanupGrayscaleBuffers(bw)`, `grayscaleCapabilities(GrayscaleMode::Overlay).supported()`, `getFrameBuffer()`, `getBufferSize()`, `getDisplayWidth/Height()`, `isInverted()`, `getController()`. X4 Pro builds `EINK_DISPLAY_SINGLE_BUFFER_MODE`: `getFrameBuffer()` is `renderer.getFrameBuffer()`.
- Driver facts (read-only, `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8179Driver.cpp`, `Uc8279X4Driver.cpp`): `displayGrayscaleBase(Fast)` runs `transitionGrayscaleBase` only when `_grayRefreshedOnce && _oldPlaneValid && !_needFullClear`; only `displayGray` sets `_grayRefreshedOnce`, `initController` (begin, wake, UC8179 direct-gray setup/exit) clears it; otherwise it falls back to `display(Fast)`, which is a FAST DU when `_oldPlaneValid && !_needFullClear` and a full GC (flashing) otherwise. After `displayGray`, `_redriveAfterGray` turns the next plain `displayBuffer(FAST)` into a transition. UC8279 variant 0x67 reports no grayscale; on UC8279 a gray pass with MSB coverage over 25 % uses the four-tone quality bank.
- Overlay encoding (`freeink-sdk/docs/grayscale-capabilities.md`): (LSB, MSB) dark (1,1), light (0,1), B/W (0,0); base frame bit set = white, and the base is 0 for every non-white pixel (K, D, L bands black, W white).
- `GfxRenderer::Orientation` / `getOrientation()` -- logical x vs. physical: Portrait `lx = H-1-py`, PortraitInverted `lx = py`, LandscapeClockwise `lx = W-1-px`, LandscapeCounterClockwise `lx = px`.
- `test/game_display_lab/CMakeLists.txt` -- target `test_scan_lut`; add the new test and source there. `scripts/check_layers.py` -- `lib/GameDisplayLab` may include the C++ standard library only.
- `docs/crosshatch/display-lab.md` -- gate sentence, hardware checklist step 3, code map.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/StockBench.{h,cpp}` -- new, pure C++ (std only), namespace `GameDisplayLab`: `fillChecker(uint8_t* frame, uint16_t width, uint16_t height, bool inverted)` (framebuffer byte order as `MaskPlanes`, bit set = white; 4 × 2 blocks of `width/4` × `height/2`, block (c, r) white when `(c + r) % 2 == 0` xor `inverted`; pad bits clear); `enum class Shade {Black, DarkGray, LightGray, White}`; `enum class BandAxis {X, Y}`; `rampShadeAt(x, y, width, height, axis, reversed)` (four equal bands K, D, L, W along the axis from its origin, mirrored when `reversed`; the last band takes the remainder); `fillGrayRamp(uint8_t* base, uint8_t* lsb, uint8_t* msb, width, height, axis, reversed)` with the overlay encoding; `struct TimingSummary {uint32_t min, median, max; size_t count;}` and `summarizeTimings(const uint32_t* values, size_t count)` (count capped at `MAX_TIMINGS` = 16; median of an even count is the lower-rounded mean of the middle two; count 0 gives zeros); `transitionFellBack(uint32_t us, uint32_t referenceUs, bool armed)` = `!armed || (referenceUs > 0 && us >= FELL_BACK_FACTOR * referenceUs)` with `FELL_BACK_FACTOR` = 2, overflow-safe -- the host-testable heart of the bench.
- [x] `test/game_display_lab/test_stock_bench.cpp` + `CMakeLists.txt` -- tests for every function above: checker bytes and inversion at 800 × 480 and a non-multiple-of-8 width, each ramp band's (base, lsb, msb) bits on both axes and reversed, odd/even/empty/over-cap medians, fell-back cases (unarmed, under, at, over the factor, zero reference, values near `UINT32_MAX`).
- [x] `src/activities/games/display_lab/StockBenchGate.{h,cpp}` -- new: `bool usesStockBench(HalDisplay::Controller)` = (UC8179 or UC8279) and `!hasScanBackend(controller)` (a backend always wins); `const char* labRowSubtitle()` = stock-bench text when `BoardConfig::isX4Pro() && usesStockBench(display.getController())`, else `STR_GAMES_DLAB_ROW_DESC`.
- [x] `src/activities/games/display_lab/TransitionTimingActivity.{h,cpp}` -- new `UiListActivity` (`NAME` "DlabTransition"), constructor `(GfxRenderer&, MappedInputManager&, DisplayLabActivity&)` (lab unused). Rows: Run (status), Reference, Arm, Transition 1..10, Summary. `onEnter`: one PSRAM zero plane of `display.getBufferSize()` (OOM → status "Not enough memory", Run refused). Run (blocking, one `RenderLock` held for the whole run, `app.clearTapFlash()` first): refuse when inverted or the framebuffer size ≠ `bytesFor(w, h)`; checker A → `display.displayBuffer(FAST_REFRESH, false)` timed = reference; if overlay supported: `displayGrayscaleBase(FAST_REFRESH, false)` + `copyGrayscaleBuffers(zero, zero)` + `displayGrayBuffer(false)` + `cleanupGrayscaleBuffers(fb)` timed = arm, else log `DLAB arm skipped: no overlay grayscale`; then i = 1..10: checker (`inverted = i odd`) into the framebuffer, `displayGrayscaleBase(FAST_REFRESH, false)` timed with `micros()`, flashed = `transitionFellBack`, log line. Any call over the timeout: log, stop, promote FULL. After the run: summary, `promoteNextRefresh(FULL)`, `requestUpdate()`. Back: if the screen drove the panel this visit, promote FULL, clear the flag, `finish()`; `onExit`: if still set, `renderer.displayBuffer(FULL_REFRESH)` (no RenderLock, 12cc816); free the plane.
- [x] `src/activities/games/display_lab/GrayRampActivity.{h,cpp}` -- new `UiListActivity` (`NAME` "DlabGrayRamp"), same constructor. One row: Gray ramp (status). `onEnter`: two PSRAM planes (LSB, MSB). Activation while held: clear held, `promoteNextRefresh(FULL)`, `requestUpdate()` only. Otherwise: refuse when inverted; overlay unsupported → status `STR_GAMES_DLAB_GRAY_UNSUPPORTED`, log, no pass; else under `RenderLock`: `fillGrayRamp` (axis/reversed from `renderer.getOrientation()` so bands run left to right K, D, L, W as the user holds it) into the framebuffer and planes, time `displayGrayscaleBase(FAST_REFRESH, false)` = base, time `copyGrayscaleBuffers` + `displayGrayBuffer(false)` = gray, `cleanupGrayscaleBuffers(fb)`, log `DLAB gray base_ms=%u gray_ms=%u`, set held (render draws nothing). Over the timeout: log, promote FULL, request repaint. Back and `onExit` as Transition timing.
- [x] `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- `available()` adds `|| usesStockBench(c)`; a `stockBench` member set in `onEnter` (log `DLAB UltraChip stock-path bench`); `onEnter` skips `openSession()` for it (no backend, no masks); `listCount` gives `STOCK_ROW_COUNT` (3) for it (`static_assert` ≤ `ROW_COUNT`); `buildRows` and `activateIndex` each get one early `if (stockBench)` branch into new private `buildStockRows()` / `activateStock(index)` defined at the end of the .cpp. No other change to the existing rows or the scan path.
- [x] `src/activities/games/GamesLauncherActivity.cpp` -- the lab row's subtitle from a file-local helper that returns `labRowSubtitle()` under `CROSSHATCH_DISPLAY_LAB` and `tr(STR_GAMES_DLAB_ROW_DESC)` otherwise; include `display_lab/StockBenchGate.h` in the existing `#if` block.
- [x] `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_*` keys: stock row desc, Transition timing (+ desc), Gray ramp (+ desc), gray unsupported, reference, arm, transition n, ms, ms fell back, summary, gray result.
- [x] `docs/crosshatch/display-lab.md` -- gate text, a "UltraChip stock-path bench" section (what each screen does, the arming sequence, the fell-back heuristic and its unmeasured factor, the hardware steps and expected log lines), code map entries.

**Acceptance Criteria:**
- Given an x4pro build on a UC8179 X4 Pro, when Games opens, then the Display lab row reads "UltraChip stock-path bench" and, past the warning, the lab lists Transition timing, Gray ramp, Exit.
- Given the host suite, when it runs, then every `StockBench` function is pinned, and every existing test still passes.
- Given `default` and `sticky` builds, when built, then they compile none of the new device files.

## Implementation Notes

- Strings: the summary row needs a label and a value, so "summary" is two keys, `STR_GAMES_DLAB_SUMMARY` ("Summary")
  and `STR_GAMES_DLAB_SUMMARY_MS` ("Min %u, median %u, max %u ms"). The arm row of an unarmed run reuses
  `STR_GAMES_DLAB_GRAY_UNSUPPORTED`; inverted, out-of-memory and refused statuses reuse the lab's existing keys.
- Gray ramp: a base call over the timeout skips the gray pass (as a timed-out call ends the transition run);
  `cleanupGrayscaleBuffers(fb)` runs after either path, and the `DLAB gray base_ms= gray_ms=` line is logged only when
  both calls completed (the timeout line carries the time otherwise).
- Review fixes: the arm times its base alone (checked before any plane upload) and the gray step separately, with
  the cleanup outside both spans and one `DLAB arm ms=` line of their sum; a flip over the timeout stays out of the
  summary; a refused run keeps the previous table; dismissing the held ramp clears the tap flash;
  `STR_GAMES_DLAB_GRAY_RESULT` is "Base %u ms, gray %u ms"; the lab's not-available log and `GamesLauncherActivity.h`'s
  class comment name the stock bench.
- Short axes: `rampShadeAt` and `fillChecker` give one pixel per band or block when the axis is shorter than the
  band count, the rest to the last; a coordinate past the axis counts as its last pixel.
- Host suite: 2,128 tests (2,106 + 22 `StockBench*` in `test_scan_lut`).

## Plan Change Log

## Review Triage Log

### 2026-10-16 — Review pass
- verdicts: 33 findings — high 0, medium 2, low 19, false 6, maybe-false 6
- findings:
  - `[maybe-false]` `[defer]` (blind) the fell-back factor 2 is uncalibrated; if the transition takes 2x a DU or more, every armed flip is flagged — settled only by the owner's UC8179 timings (transition vs. reference vs. a FULL); if true, medium. Deferred.
  - `[low]` `[patch]` (blind) `flashed=yes` on an unarmed run flags non-flashing DU refreshes while the doc told the owner to compare the flag with the eye — doc now says the flag means "fell back to a plain refresh" (the story's definition, log key kept) and only an armed flag predicts a visible flash.
  - `[low]` `[patch]` (blind) arm ms / base_ms differ by driver path (first run after wake arms through a plain FAST base, later ones through the transition) and nothing says so — documented in display-lab.md.
  - `[low]` `[patch]` (blind) the bench calls HalDisplay with turnOffScreen=false while the reader passes the fading fix through GfxRenderer, yet the doc called it "the call the reader makes" — doc corrected.
  - `[medium]` `[patch]` (blind) the arm checked one timeout over base + upload + gray pass + cleanup, so a hung base was followed by three more panel calls; the arm span included the cleanup unlike Gray ramp's — base timed and checked alone before any upload, gray step timed separately, cleanup outside both spans.
  - `[false]` `[reject]` (blind) nothing keeps the reference a plain FAST after a pending clear or redrive — the screen is repainted with an ordinary refresh after onEnter and a FULL after every run, and each passes through displayStart, which clears `_needFullClear` and `_redriveAfterGray` before Run can be pressed.
  - `[low]` `[reject]` (blind) `bandsFor` and the backend-wins rule are pure logic left untested in device code — both verified correct by hand (bandsFor against GfxRenderer.cpp:1159-1174 by two lenses and the planner); a test needs a new pure function in lib (new surface) for a defect nobody meets today.
  - `[low]` `[reject]` (blind) degenerate sizes (width < 4, 0) untested and `NullFrameIsIgnored` asserts nothing — the device framebuffer is always 800x480; additional tests for unreachable sizes add nothing a user meets.
  - `[low]` `[patch]` (blind) the press that dismisses the held ramp did not clear the tap flash — `app.clearTapFlash()` added on that path.
  - `[low]` `[patch]` (blind) the gray result text's "Look at the panel, then press a key." is only visible after the key press — string is now "Base %u ms, gray %u ms".
  - `[low]` `[patch]` (blind) a refused Run wiped the previous table — `resetResults()` moved after the refusal checks.
  - `[maybe-false]` `[reject]` (blind) the arm row's unsupported text may truncate at one line — would need the rendered row at 480 px; if true only low (cosmetic, row still readable in the log).
  - `[low]` `[patch]` (blind) onEnter's not-available log still named only a scan backend and called available() twice — log names the stock bench; available() computed once.
  - `[low]` `[patch]` (blind) StockBench.h cited a nonexistent `rowBytes(width)` — comments point at `MaskPlanes::bytesFor`.
  - `[low]` `[reject]` (blind) per-pixel fills under the render lock between flips — outside every timed span, so no measurement is affected; a template-row rewrite adds code for a few milliseconds between flips.
  - `[low]` `[reject]` (edge) a top-edge swipe opening the control center while the ramp is held leaves render suppressed after it pops, so the closed panel stays until a press — recoverable with one press (which repaints FULL), the same as PolarityActivity's existing hold, and a fix needs a resume hook the Activity API lacks.
  - `[medium]` `[patch]` (edge) arm base hang followed by more panel calls — same root as the blind arm-timeout finding; same fix.
  - `[low]` `[patch]` (edge) a flip over the timeout was counted in the summary — recorded into flipUs/flipCount only after the timeout check.
  - `[low]` `[patch]` (edge) unarmed `flashed=yes` contradicts what the eye sees — same root as the blind flashed finding; doc fix.
  - `[low]` `[patch]` (edge) rampShadeAt's comment said the Black band takes the remainder when reversed; the code gives it to White — comment corrected.
  - `[low]` `[patch]` (edge) the doc's "15,000 ms timeout per call" was untrue for the arm — same root as the arm-timeout finding; the code now checks each timed step and the doc says "per timed step" with the list.
  - `[maybe-false]` `[defer]` (verification-gap, pre-verified) no test covers which bench a panel gets (available(), usesStockBench, labRowSubtitle, the menu dispatch) — no host harness compiles display_lab (pre-existing, as dlab-0's deferred guard-order item); if a regression ships, medium. Deferred.
  - `[maybe-false]` `[reject]` (verification-gap, pre-verified) `bandsFor` has no test — grouped with the blind pure-logic finding: mapping verified correct; a test needs new lib surface. Low.
  - `[low]` `[patch]` (verification-gap other) the onEnter not-available log is stale — same root as the blind onEnter finding; same fix.
  - `[false]` `[reject]` (intent) the arming sequence departs from the Code Map's "or one ordinary refresh cycle" — the story asked to document what makes the path active; the drivers set `_grayRefreshedOnce` only in `displayGray` (Design Notes cite it).
  - `[false]` `[reject]` (intent) 12 lines instead of 11 — the run logs exactly 11 `DLAB transition` lines (reference + 10); the arm line is a separate `DLAB arm` timing the Code Map's arming note and "log every timing" require (Design Notes).
  - `[low]` `[patch]` (intent) `flashed` D1/D2 reading — the diff follows the story's definition (D1); same root as the blind flashed finding, doc fix.
  - `[false]` `[reject]` (intent) exit FULL lives per screen and a lab visit that ran nothing gets none — the same rule dlab-0's lab applies (a FULL when something reached the panel since the last clean; display-lab.md), and every route out after a drive runs one.
  - `[maybe-false]` `[defer]` (intent) gate and subtitle untested on host — same root as the verification-gap gate finding; deferred with it.
  - `[maybe-false]` `[reject]` (intent) band orientation untested — same root as the bandsFor finding.
  - `[low]` `[patch]` (intent) base_ms path not reported — same root as the blind arm/base path finding; documented.
  - `[false]` `[reject]` (intent) no session cap — the story's Always list governs this bench and names the warning, exit FULL, inverted refusal and timeout; the 2,400 cap counts one-frame scans, which this bench never sends.
  - `[false]` `[reject]` (intent) flip count fixed at 10 — "a chosen number of times (10)": the story chose it; no user control is asked for.

### 2026-10-16 — Firmware verification
- verdicts: 1 finding — high 1, medium 0, low 0, false 0, maybe-false 0
- findings:
  - `[high]` `[defer]` (verification) `check_flash_budget.py compare --limit-kib 270` fails: over budget by 416 B (games delta +276,896 B). The lab is dev-env only, and the epic's D6 changes the gate to build both images with FREEINK_CAP_DISPLAY_LAB unset, tracking the lab's cost in the epic until then. The gate change is the orchestrator's D6 item, not this story's intent. Deferred; it blocks the epic PR until D6 lands.

## Design Notes

- Gate precedence (story: "SSD1677 keeps the scan screens; UltraChip gets these two"; epic Lanes: dlab-a2/b2 add UltraChip scan backends later on the experimental branch only): a controller with a scan backend gets the scan screens; an UltraChip without one gets the stock bench. Which one an UltraChip shows once it has a backend is left to dlab-a2/b2.
- Arming (story Code Map: "document what sequence makes the transition path active"): per the drivers, an ordinary refresh never sets `_grayRefreshedOnce`; only a gray pass does, and wake resets it. So the run takes the reference first (after a gray pass, `_redriveAfterGray` would turn that FAST into a transition) and arms with one overlay gray pass of all-B/W masks over the same checker. Its time is logged as `DLAB arm ms=` so the run's `DLAB transition` lines stay the story's 11 (reference + 10).
- Fell-back heuristic (story: "from the measured time against the stock FAST time … and the capability flags"): after a successful arm, every fallback the drivers can take is a full GC (pending clear or no valid old plane), which takes much longer than a FAST; a FAST-DU fallback happens only unarmed, which the overlay capability says. Hence `!armed || t >= 2 × ref`; the factor 2 is unmeasured and the table shows raw ms so the owner can judge.
- Gray base uses `FAST_REFRESH` as the reader's per-page call does; `gray_ms` covers the plane upload and the activation (what a caller pays for the gray pass).
- Exit rule: once a screen drove the panel in this visit, every way out runs exactly one FULL (Back promotes it onto the menu's repaint so it shows the menu; Home/sleep run it in `onExit`, as the SSD1677 lab's `cleanPanel`). The transition table's repaint after a run is also FULL (FramePeriod's rule), so a run then Back costs two FULLs; the story asks for both.
- DisplayLabActivity hunk for the dlab-c1 merge: the gate line, the `stockBench` member and `STOCK_ROW_COUNT`, one `onEnter` condition, `listCount`, one early branch each in `buildRows`/`activateIndex`, and two functions appended at the end of the file.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass, `test_stock_bench` included.
- `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py`, each `python3 scripts/*_test.py` -- expected: exit 0.
- `./bin/clang-format-fix` twice -- expected: the second run leaves `git status` unchanged.
- After review, with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, under `flock /tmp/crosshatch-build.lock`: `pio run -e x4pro`, `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`; `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects` -- expected: all succeed; record flash and static-RAM against the epic base (9,712 B / 240 B spare at `0cb1fc8e`).

**Manual checks (if no CLI):**
- Hardware (owner, after merge): the transition table and log lines and a gray-ramp photo on the UC8179 unit.

## Auto Run Result

Status: built. **Flash-budget gate red: over by 416 B; see Verification and the `deferred` list (epic D6).**

**Summary**

The UltraChip stock-path bench, reviewed and patched. It applies on an X4 Pro whose UC8179 or UC8279 has no scan backend.

- **Launcher:** the row shows Display lab with the subtitle "UltraChip stock-path bench".
- **Menu:** Transition timing, Gray ramp and Exit. No scan session is opened.
- **Transition timing:**
  - A 4x2 checker and a timed stock FAST reference.
  - One arming all-B/W gray pass, with its base timed and checked on its own.
  - Ten timed `displayGrayscaleBase(FAST)` flips, each with the fell-back flag.
  - A min/median/max summary.
- **Gray ramp:**
  - K/D/L/W overlay bands, oriented to the way the device is held.
  - The base and the gray pass are timed.
  - The panel holds the ramp until the next press, which repaints with a FULL refresh.
- **Both screens:**
  - Refuse to run while the display is inverted.
  - Apply the 15,000 ms timeout to each timed step.
  - Run one FULL refresh on every way out after a drive.

**Files**

- `lib/GameDisplayLab/StockBench.{h,cpp}`: the pure checker, ramp, summary and heuristic.
- `test/game_display_lab/test_stock_bench.cpp` and `CMakeLists.txt`: 22 tests.
- `src/activities/games/display_lab/StockBenchGate.{h,cpp}`: the gate and the subtitle.
- `TransitionTimingActivity.{h,cpp}` and `GrayRampActivity.{h,cpp}`: the two screens.
- `DisplayLabActivity.{h,cpp}`:
  - The gate, the `stockBench` member and `STOCK_ROW_COUNT`.
  - One early branch each in `buildRows` and `activateIndex`.
  - The UltraChip row list in `buildStockRows`/`activateStock`, added at the end of the .cpp.
  - The existing rows and the scan path are untouched, for the dlab-c1 merge.
- `GamesLauncherActivity.{h,cpp}`: the subtitle helper and the comment.
- `english.yaml`: 14 `STR_GAMES_DLAB_*` keys.
- `docs/crosshatch/display-lab.md`: the gate, the checklist, the bench section and the code map.

**Review**

- Four lenses ran: blind-hunter, edge-case-hunter, verification-gap and intent-alignment.
- They raised 33 findings. 17 rows were patched (11 distinct fixes), 3 rows were deferred as 2 items, and 13 were rejected with reasons in the triage log.
- Firmware verification added 1 finding: the flash gate, deferred to D6.
- Follow-up review recommended: false. The two medium rows patched share one root cause and no high was patched.

**Verification**

- **Host:** 2,128/2,128 tests pass after the patches.
- **Fast checks:**
  - `check_layers.py` passes.
  - All 13 `scripts/*_test.py` pass.
  - `check_upstream_touches.py` passes.
  - `clang-format-fix` was run twice with no change.
- **Firmware** (08:16–09:27 under the build lock, cold shared cache): `pio run -e x4pro`, `-e default` and `-e sticky` succeed.
  - x4pro: RAM 104,232 B, flash 6,230,358 B (95.1 %).
- **`pio check`:** `default` and `-e x4pro` both pass with no defects.
- **Flash budget:**
  - `build on` and `build off` succeed.
  - `compare` fails: games delta +276,896 B against the 276,480 B limit, over by 416 B. Against the epic base (9,712 B spare at `0cb1fc8e`), the epic now has a total cost of +10,128 B.
  - This story alone, like for like: the same gate on the same machine at the story's base `9b2317f3` gave a games delta of +268,928 B (7,552 B to spare). So this story costs **+7,968 B** of games-delta flash. The games-off image also grew by 1,280 B (5,957,248 to 5,958,528 B), from the 14 new strings, which every build carries.
  - Static RAM: +784 B, 240 B to spare, +0 B against both the base and `9b2317f3`.
  - `objects`: no problems.
- **Hardware:** for the owner on the UC8179.

**Residual risks**

- The flash gate stays red until D6.
- Arming assumes an all-zero overlay gray pass reaches the driver's `displayGray`.
- The fell-back factor of 2 is a guess.
- Every behaviour of the device screens is unverified until the owner's run.

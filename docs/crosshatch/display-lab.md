# Display lab: the panel scan bench (Part A) and the gray engine (Part B)

The display lab is a bench, not a product mode. It lives in `src/activities/games/display_lab/` and
`lib/GameDisplayLab/`, compiles only under `FREEINK_CAP_DISPLAY_LAB`, which the `x4pro` development env alone sets, so
no release build (`x4pro-gh_release*`) ever carries it (the simulator's HAL stand-in has no `HalDisplay::eink()`, so the
row is absent there too), and shows its row only on an X4 Pro whose panel probe found a controller with a scan
backend (SSD1677 and UC8279 so far). For the same reason the flash-budget gate (`scripts/check_flash_budget.py`)
builds both of its images with `FREEINK_CAP_DISPLAY_LAB` unset (the display-lab epic, D6): it measures shipped firmware, so the lab's
cost never counts against the games budget, and the gate refuses a build whose metadata still defines the lab. Its
static-object scan (`objects`) therefore no longer reads the lab's screen code, which compiles empty without the flag;
it still reads `lib/GameDisplayLab`.
Its menu has six rows: Frame period, Polarity, Transition test, Tuning, Save preset, Exit.
Part A answers the two numbers only a panel can give before the per-pixel grayscale engine (Part B, the transition
test and tuning below) can be tuned:

1. the **frame period**: how long one frame of a one-frame activation takes, from the slope of BUSY time against
   TP[0A];
2. the **drive polarity** of the two LUT rows a pixel can select: which of LUT1 (the BW-plane row) and LUT2 (the
   RED-plane row) darkens the panel under the codes the lab tries first (VSL on row 1, VSH1 on row 2, InkBoy's SSD1681
   convention).

Safety: on an SSD1677 every LUT the lab sends carries the panel's stock voltages (bytes 105-109 = `0x17 0x41 0xA8
0x32 0x30`), the OTP command is never sent, and each scan goes through the SDK facade's overlay-grayscale path with
`factoryMode=true` (the upload of the 105 LUT bytes plus the voltage registers, command 0x22 = 0xCC, a timed BUSY
wait); on a UC8279 the lab writes no voltage register and sends no OTP or MTP command (see its section below). On
either, a scan whose timed wait took over 15,000 ms ends its run and the panel is cleaned. A wait cannot be
interrupted. On an SSD1677 the SDK caps each BUSY wait at 30 s, so a hung panel stalls the lab up to 30 s per scan. On
a UC8279 the SDK's BUSY waits have no time limit (`EpdBus::waitBusy`'s `UcIdleHigh` path, by design, the same wait
every ordinary UC8279 refresh uses), so a panel whose BUSY never releases stalls the lab without bound, and the 15 s
timeout fires only for a DRF wait that does end after more than 15 s. 2,400 scans since the last clean force
a FULL refresh, and leaving the lab by any route (Back, Home, sleep) runs one FULL refresh when a scan reached the
panel since the last clean, and frees the backend's two PSRAM planes and the lab's two PSRAM masks.

## The UC8279 backend (X4 Pro UC8279 batch)

On a UC8279 X4 Pro (`Uc8279X4Driver`) the lab drives the SDK facade's lab API (`freeink-sdk/docs/display-lab.md`)
instead of the overlay-grayscale path, with the same screens, safety wrapper and experiments:

- **Tables.** Each scan uploads the five register-prefixed 43-byte tables (0x20 VCOM, 0x21 WW, 0x22 KW, 0x23 WK,
  0x24 KK). Each holds one group `[0x01, P1, P2, P3, P4, 0x01, 0x01]` and zeros, a phase byte being `level << 6 |
  frames`. A phase holds at most 63 frames, so the scan's frames fill the four phases 63 at a time (100 = 63 + 37) up
  to 252; a request over 252 frames is refused. Every table runs the same frames: KW at VSL and WK at VSH (exchanged
  when the polarity is swapped), VCOM, WW and KK at GND. No voltage register is written and no OTP or MTP command is
  sent.
- **Planes.** DTM1 (old) is the inverted white mask and DTM2 (new) the inverted black mask: a pixel in neither mask
  selects WW and holds, the white mask selects KW (drive to white), the black mask WK (drive to black), and a pixel in
  both KK, which holds too. This rests on the lab's CDI equalling the B/W refresh's. The driver's quality bank
  (`kQualityLutReg`, 0x22 and 0x23 exchanged) streams inverted gray planes, not B/W old/new planes, so it is no
  direct evidence for this selection; the polarity screen is what confirms on hardware which table darkens.
- **Rows.** The polarity screen labels Row 1 "KW VSL" and Row 2 "WK VSH"; swapped, "KW VSH" and "WK VSL". The names
  follow the UltraChip level-select convention (01 VDH/VSH, 10 VDL/VSL; `makeQualityBank` treats levels 1 and 2 as
  opposite rails).
- **Rates.** The frame-rate list is the PLL (0x30) codes {0x0E, 0x0D, 0x0F}: the driver's stock 0x0E first (so the
  polarity scan runs at the stock rate) and one code either side. No UC8279 PLL code table is documented, so read the
  three slopes as relative, not as known frequencies.
- **Session per scan.** Every scan is a self-contained lab session: upload, PLL, one timed `labScan`, then
  `labEnd()`, which restores PSR, CDI, PLL and TSSET, because the screens repaint between scans and an ordinary refresh
  needs `labEnd()` first and drops the waveform. After `labEnd()` the driver treats the panel as unknown, so the
  screen's next repaint after a scan is a full refresh. The cleaning refresh calls `labEnd()` before its FULL refresh.
- **Busy time.** `busy_us` is the driver's DRF-to-BUSY-release time only: not the table upload, the plane streaming,
  PON or the PSR rewrite, where the SSD1677's covers its LUT upload, activation and BUSY wait. The SDK's UC8279 BUSY
  wait starts with a one-tick delay (1 ms), which `busy_us` includes, so it never falls under the lab's 500 us
  dropped-call floor. Frame-period slopes compare across controllers; `tp=1` intercepts and the polarity screen's
  `ms=` do not.
- **Refusals.** A refused scan reports Refused and logs `scan refused: UC8279 <reason> (rate index N, M frames)`, the
  reason being `request without masks`, `rate index out of range` or `frames over 252` (before any lab call), or
  `lab unsupported`, `upload refused`, `frame rate refused` or `scan refused` (a facade call returned false; the session
  still ends with `labEnd()`). The facade refuses every lab call while the display is inverted, so on a UC8279 an
  inversion the lab did not catch first shows as `upload refused`, never as "Dropped". `scan refused: no UC8279 planes`
  is a defensive guard: when the planes do not fit, the lab shows its out-of-memory notice before any scan.

## Hardware checklist (the user's validation script)

1. Build and flash: `flock /tmp/crosshatch-build.lock sh -c 'pio run -e x4pro -t upload'`; open a serial monitor at
   the env's baud with `LOG_LEVEL` 1 or more (the `x4pro` development env prints `DLAB` lines at `LOG_INF`).
2. If the display is inverted (Settings), turn that off first: the facade drops grayscale calls (on a UC8279, refuses
   lab calls) while inverted, and the lab shows "Turn off the inverted display first" instead of its menu.
3. Home → Games. On an SSD1677 or UC8279 X4 Pro the last row is **Display lab**; on a UC8179 unit it is absent, which
   is itself a check of the gate (`DisplayLabActivity::available()`).
4. Confirm the entry warning. Back or Cancel there returns to the launcher and sends nothing to the panel.
5. **Frame period**: select it, then Run. The lab shows "Running" and performs nine all-hold scans (no pixel is
   driven, so the screen stays): TP[0A] in {1, 10, 100} against the frame-rate bytes {0x22, 0x11, 0x44}, tp outer
   (on a UC8279, 1, 10 and 100 frames against the PLL codes {0x0E, 0x0D, 0x0F}, labelled `tp=… fr=0x0E` and so on).
   Copy the nine lines from the log, for example:

   ```
   [DLAB] tp=1 fr=0x22 busy_us=...
   [DLAB] tp=1 fr=0x11 busy_us=...
   [DLAB] tp=1 fr=0x44 busy_us=...
   [DLAB] tp=10 fr=0x22 busy_us=...
   ...
   [DLAB] tp=100 fr=0x44 busy_us=...
   ```

   The screen lists the same nine busy times. A line reading `timeout` (and a "Timed out" row) means a scan ran over
   15 s: the run stopped there and the panel was cleaned; report it with the lines before it. A "Dropped" row means the
   facade refused the call (see Expected ranges).
6. **Polarity**: select it, then Row 1, labelled "Row 1 (LUT1 VSL)" on an SSD1677 and "Row 1 (KW VSL)" on a UC8279
   (the backend names each row). The whole panel is driven with VSL for 10 frames (on a UC8279, through the KW table)
   and the screen is *not* repainted: look at the panel and note whether it went darker or lighter all over (the
   change is uniform and may be subtle, since 10 frames is a short drive). Only Confirm or a tap brings the screen
   back, with a FULL refresh; Up and Down move the selection but do not repaint while the drive is shown. After that
   repaint the next activation runs the row it lands on: select Row 2 and do the same with VSH1 (on a UC8279, "Row 2
   (WK VSH)": the WK table at VSH). Two log lines name each row and its time:

   ```
   [DLAB] polarity row=1 vs=LUT1 VSL ms=...
   [DLAB] polarity row=2 vs=LUT2 VSH1 ms=...
   ```

   On a UC8279 they read `vs=KW VSL` and `vs=WK VSH`.

7. Photograph the panel after each polarity drive (before pressing a key) and the frame-period table.
8. Leave with Back. The log's `exit: N scans this session` line and one FULL refresh end the session.

## Expected ranges

- Busy time grows with TP for a fixed frame-rate byte; the frame period is `(busy(100) - busy(1)) / 99` microseconds
  per frame. SSD1677-class panels at their usual frame rates give a period in the low tens of milliseconds per frame
  (roughly 10-25 ms), so expect `tp=100` scans of about 1-2.5 s and `tp=1` scans dominated by the fixed overhead of the
  LUT upload (105 bytes to 0x32 plus the voltage registers) and activation (tens of milliseconds). If the three frame-rate bytes give the same slope, the
  byte's encoding does not reach this activation; if they differ, the ratio between them is the data Part B needs.
- Exactly one of the two polarity rows darkens the panel. If Row 1 (VSL) lightens and Row 2 (VSH1) darkens, InkBoy's
  convention holds on this panel: white = VSL on the BW-plane row, black = VSH1 on the RED-plane row. If it is the other
  way round, turn on Swap polarity in Tuning (the preset's `swap_polarity`), so the gray engine's scans swap the two
  rows.
- On a UC8279 the slope reads the same way, but `busy_us` there is the DRF activation and BUSY wait only, so the `tp=1`
  intercept is the activation overhead alone (with the wait's 1 ms start tick), with no table upload in it. For
  polarity, Row 1 (KW at VSL) should lighten and Row 2 (WK at VSH) darken; if Row 1 darkens instead, the levels or the
  table roles are the other way round on this panel, and Swap polarity in Tuning (the preset's `swap_polarity`, sent
  as `ScanRequest::swapPolarity`) swaps the levels. The "Dropped" status cannot occur there (see Busy time and
  Refusals in its section).
- On an SSD1677, a "Dropped" status (log line `dropped: scan N came back in M us, under the 500 us floor`) means the
  facade refused the call and the panel was not driven, usually because the display is inverted (the control-center
  panel can invert it while the lab is open; the lab also refuses outright, logged as `scan refused: display inverted`,
  when it sees the inversion first). Turn inversion off and run again; a dropped time is not a measurement.

## The scan seam

Part B takes the measured frame period as the length of one dose and the polarity answer as the row-to-color mapping
(a preset's polarity swap flips it), and builds on the scan seam (the display-lab epic's D1): a `ScanRequest` of two
full-screen drive masks (`GameDisplayLab::MaskPlanes`: black, white, a pixel in neither holds), a frame count, a rate
index and a polarity swap, sent through `DisplayLabActivity::scan`, which wraps the controller's `ScanBackend` with the
session cap, the timeout and the dropped-call floor. A new controller is one backend class plus its case in the one
switch of `ScanBackends.cpp`, and the screens and the safety rules stay as they are: the backend names its own drive
rows (`ScanBackend::rowLabel`, the epic's Amendment D1a; on the SSD1677 "LUT1 VSL" and "LUT2 VSH1"), which the
polarity screen shows and logs as `vs=`, and the frame-period screen runs the backend's own rate list. The
frame-period TPs and the polarity TP are frame counts every backend takes. Nothing in the lab is a product feature;
the launcher row and the lab can be removed once the engine has its own entry.

## Transition test and tuning (Part B)

The gray engine is InkBoy's dither-free four-shade model (the research digest D2 round 2) over the scan seam. Each
pixel has a modelled level (0 = black K, 64 = dark gray D, 128 = light gray L, 192 = white W), a target shade, and a
count of doses left; a **dose** is one one-frame scan of that pixel toward its target (`frames = 1`). A pixel moving
from shade A to shade B gets the dose-table entry for A>B; one between shades gets the two neighbouring shades' doses
interpolated, rounded up; one interrupted mid-transition gets that scaled by the retarget percent (clamped to 1..12). The model
advances only when a scan was delivered: `level += (target*64 - level) / remaining`, so the last dose lands on the
target. One **slot** is a drive scan of every pixel with doses left, then a follow-up scan that drives again only the
pixels whose transition's hold bit is off and that have two or more doses left (every other pixel holds, which ends
the pulse); a slot repeats every max(the slot's busy time + 5 ms, 100 ms) (the epic's D5). Nothing is dithered.

The preset starts as InkBoy's defaults, the author's numbers for an SSD1681, not this panel's: finding this panel's
values is the point of the two screens.

| From > To | K>D | K>L | K>W | D>K | D>L | D>W | L>K | L>D | L>W | W>K | W>D | W>L |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Dose | 2 | 4 | 7 | 4 | 1 | 6 | 4 | 1 | 5 | 7 | 3 | 2 |
| Hold | on | on | off | off | on | off | off | on | off | off | off | on |

Retarget 100 %, rate index 0, no polarity swap, and InkBoy's routes (L>K>L return -1 and reverse 150 %, the rest
neutral) are InkBoy's too. History and weak mode are off: this story's choice (InkBoy ships history "both" and weak
"normal"); the preset file turns them on.

### Transition test

Display lab → Transition test → Run. The panel is cleaned to white with a FULL refresh, then the screen stops
drawing: the panel shows only what the scans drive. Four shade bands, K, D, L, W from the top, each with three 12-px
rectangles of the other three shades (in K, D, L, W order), sweep from left to right in 5 s, pause 1 s, and repeat,
following the screen's orientation.

Controls while it runs (the Controls row says the same):

- **Confirm**, or a tap over the screen's row area (where the Run, Speed and Controls rows were): pause; again to
  resume (the sweep clock stands still while paused). Swipes do nothing while the test view is up.
- **Next** (NavNext: side Down or front Right; Up or Left in inverted portrait and counter-clockwise landscape,
  where the axis follows the screen): replay: clean to white and start the sweep from 0.
- **Previous** (NavPrevious, the opposite keys): clean: a white panel, paused; resume redraws the bands.
- **Back**: stop and return to the rows with a FULL refresh. The Run row says "Stopped", or how the sweep ended when a
  scan was not delivered ("Timed out", "Dropped", "Refused (see log)"), which stops it at once. The Speed row shows the
  last "N.N scans/s, N ms per scan".
- Another screen over the test (the light panel's top-edge swipe) ends the view as Back does, once it closes: the rows
  come back with a FULL refresh. A forced refresh (the power or home button's Refresh) does nothing while it runs.

Every 2,400 scans the panel needs a cleaning refresh (the lab's session cap): the sweep cleans to white just before
it and carries on. The log has `DLAB gray scans_s=N.N ms_per_scan=N skipped=N` about every 2 s and on every stop
(`skipped` counts the slot periods lost to slow slots in that same window, about 2 s; they are dropped, never
queued).

How to read it:

- **Rectangle shade against its band** is the dose: a rectangle meant to be L that looks like D on a W band has too
  many W>L doses (or W>L should not hold); one that never gets dark enough has too few.
- **The track behind a rectangle** is the return to the band shade (hold and history): a dark or light trail after the
  rectangle passes means the return transition's dose or hold bit is off, or needs the history routes.
- **Build-up over passes**: a band that drifts darker or lighter over several sweeps means a pair of opposite doses
  (A>B and B>A) does not cancel.
- **Endpoints**: the K and W bands and rectangles must reach full black and white; if they do not, raise K>W, W>K, or
  the rows into K and W.

Tuning order, one change per run: the **doses and hold bits** first, judged by the rectangles and their tracks; then
the **retarget** percent, judged by rectangles that change direction mid-sweep (after a replay or a pause); then, only
if trails remain, the **history** routes and the weak row, which the preset file turns on (no row on the device).

What to photograph: the panel paused mid-sweep (Confirm) with the rectangles over each band, the panel during the 1 s
pause (the tracks), and the panel after five or more full sweeps (build-up), each with the Tuning values and the
log's `gray` line beside it.

### Tuning

Display lab → Tuning lists every value of the session's preset, one row each: Dose K>D through Dose W>L (1..12), Hold
K>D through Hold W>L (on or off), Retarget (50..150 %, steps of 25), Frame rate (the backend's rate labels), Swap
polarity, and Save. A **tap** on a row steps it in place (+1, wrapping; a hold or the swap toggles). **Confirm** on a
row enters edit mode for it ("editing" on its line): Next and Previous (NavNext and NavPrevious, as above) then change
it by one step (+1 / -1, clamped; a hold or the swap on / off), and Confirm or Back leaves edit mode. Outside edit
mode they move the selection and Back closes the screen. Changes apply at once to the session (the next transition-test run uses them) and are logged as
`DLAB tune <row>=<value>`; nothing is written until **Save** (on this screen or the lab menu), so leaving the lab
without saving discards them.

### The preset file

`/.crosshatch/display-lab.json` on the SD card, read when the lab opens and written by Save. Missing: InkBoy's
defaults (logged `no preset`). Unreadable or malformed: InkBoy's defaults, logged with `LOG_ERR`. A failed save keeps
the values in memory and says "Save failed, values kept". Its text, InkBoy's defaults shown:

```json
{"version":1,"doses":[2,4,7,4,1,6,4,1,5,7,3,2],"hold":[true,true,false,false,true,false,false,true,false,false,false,true],"retarget_percent":100,"rate":0,"swap_polarity":false,"history":0,"return_delta":[-1,0,0,0,0,0],"reverse_percent":[150,0,0,0,0,0],"weak_white_to_light":false}
```

| Key | Meaning | Range |
| --- | --- | --- |
| `version` | the file format; required | 1 |
| `doses` | the 12 doses in the table order K>D, K>L, K>W, D>K, D>L, D>W, L>K, L>D, L>W, W>K, W>D, W>L | 1..12 each |
| `hold` | the 12 hold bits in the same order (on: the follow-up scan holds that transition) | `true` / `false` |
| `retarget_percent` | the budget of a pixel retargeted mid-transition, percent of its base dose | 50..150 |
| `rate` | the frame-rate index into the backend's list; past its end, rate 0 (logged) | 0..255 |
| `swap_polarity` | swap the backend's two drive rows | `true` / `false` |
| `history` | 1: completed returns, 2: reversals, 3: both (allocates 3 B a pixel of PSRAM) | 0..3 |
| `return_delta` | per route LKL, DKD, DWD, LWL, DLD, LDL: doses added to a completed return (0 = none) | -11..11 |
| `reverse_percent` | per route, the same order: a reversal's budget percent (0 = follow retarget) | 0..250 |
| `weak_white_to_light` | W>L pixels settled on white darken through the backend's weak row, when it has one | `true` / `false` |

Absent keys keep the defaults; unknown keys are skipped; a key of the wrong type or out of range, an array of the
wrong length, a parse error, or a missing or different `version` makes the whole file malformed. The codec is a small
standard-library JSON subset in `lib/GameDisplayLab/GrayPreset.cpp`, not ArduinoJson: the lab's library may use the
C++ standard library only (the spine's layer table, `scripts/check_layers.py`), and the host suite tests it.

Memory, PSRAM only, while the transition test is open: the model (2 B a pixel, 768,000 B at 800x480) and the
follow-up masks (2 x 48,000 B), plus the history (1,152,000 B) when `history` is on and the weak bitmap (48,000 B)
when weak mode is on and the backend has a weak row; the lab's own masks and the backend's planes stay as in Part A.
If any does not fit the screen shows "Not enough memory" and runs nothing.

## Code map

- `lib/GameDisplayLab/Ssd1677ScanLut.{h,cpp}`: the 112-byte scan LUT builder (105 LUT bytes, five voltage bytes, two
  reserved) and the experiment tables; pure C++, pinned byte for byte by `test/game_display_lab/test_scan_lut.cpp`.
  Its `planeFillFor` and `framePeriodScan` are no longer used by the screens: they are Part A's reference, which
  `test_ssd1677_encoding.cpp` pins the seam against.
- `lib/GameDisplayLab/LabExperiments.h`: the experiments' request layout, controller-neutral: `FRAME_PERIOD_TPS`,
  `POLARITY_TP`, the frame-period scan order (`framePeriodTp`, `framePeriodRate`: tp outer, rate inner) and the
  polarity row to mask table (`POLARITY_ROW_MASKS`: row 1 white, row 2 black); pinned against Part A by
  `test_ssd1677_encoding.cpp`.
- `lib/GameDisplayLab/Ssd1677Encoding.{h,cpp}`: the SSD1677 backend's pure parts: its rate list (the three frame-rate
  bytes, in order), the LUT for one request (`buildOneFrameLut` with the rate's byte and the row codes, swapped on
  request), the row labels (`ssd1677RowLabel`), and the planes from the masks (white to BW, black to RED); pinned
  against Part A's bytes by
  `test/game_display_lab/test_ssd1677_encoding.cpp`.
- `lib/GameDisplayLab/MaskPlanes.{h,cpp}`: the two drive masks in the framebuffer's byte order (MSB first, row-major,
  100 bytes per row at 800 px): clear, fill, clipped rectangles and pixels; pinned by
  `test/game_display_lab/test_mask_planes.cpp`.
- `lib/GameDisplayLab/ScanBudget.h`: the session cap, the timeout, and the dropped-call floor, pinned by
  `test/game_display_lab/test_scan_budget.cpp`.
- `src/activities/games/display_lab/ScanBackend.h`: the gate macro, `ScanRequest`, and the abstract `ScanBackend`
  (the epic's D1).
- `src/activities/games/display_lab/ScanBackends.{h,cpp}`: `hasScanBackend` and `makeScanBackend`, one case per
  controller; SSD1677 and UC8279 have a backend so far.
- `src/activities/games/display_lab/Ssd1677ScanBackend.{h,cpp}`: the SSD1677 facade calls (its two PSRAM planes,
  `copyGrayscaleBuffers`, the timed `displayGrayBuffer(false, lut, true)`) and the cleaning FULL refresh.
- `lib/GameDisplayLab/UC8279ScanLut.{h,cpp}`: the UC8279 backend's pure parts: the five 43-byte tables of one scan,
  the PLL code list and its labels, the row labels (`uc8279RowLabel`), and the two DTM planes from the masks; pinned
  byte for byte by `test/game_display_lab/test_uc8279_scan_lut.cpp`.
- `lib/GameDisplayLab/UC8279ScanSession.h`: `runUc8279Request` (the request's checks, the tables and the planes,
  then `runUc8279Scan`) and `runUc8279Scan`, the one lab session of a UC8279 scan (labSupported, upload, PLL,
  labScan, labEnd); templates over the lab so `test_uc8279_scan_lut.cpp` drives them with a recording fake that
  refuses as the SDK seam does.
- `src/activities/games/display_lab/UC8279ScanBackend.{h,cpp}`: the UC8279 facade calls (its two PSRAM planes, its
  table buffer, `runUc8279Request` over `display.eink()`) and the cleaning `labEnd()` then FULL refresh.
- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}`: the menu, the gate, the session (the backend and the
  two PSRAM masks), the controller-neutral `scan()` wrapper (the inversion and rate checks, the session cap, the
  timeout rule), and the exit refresh.
- `src/activities/games/display_lab/FramePeriodActivity.{h,cpp}` and `PolarityActivity.{h,cpp}`: the two experiments.
- `lib/GameDisplayLab/GrayPreset.{h,cpp}`: the gray engine's tunable values (`Shade`, the transition order, InkBoy's
  defaults, the tuning steps) and the preset file's codec; pinned by `test/game_display_lab/test_gray_preset.cpp`.
- `lib/GameDisplayLab/GrayModel.{h,cpp}`: the per-pixel model (level, doses left, target, source; optional history and
  weak bitmap): the dose lookup, retargets, the mask build, and the commit; `GraySlot.h`'s `runGraySlot` runs one slot
  (drive scan, follow-up scan, commit) through a caller's scan function; both pinned by `test_gray_model.cpp`.
- `lib/GameDisplayLab/SlotScheduler.h`: the slot period (D5), skipped slots, and the speed window; pinned by
  `test_slot_scheduler.cpp`.
- `lib/GameDisplayLab/TransitionScene.{h,cpp}`: the bands and sweeping rectangles over the logical screen, mapped to
  the panel as `GfxRenderer::rotateCoordinates` maps a pixel; pinned by `test_transition_scene.cpp`.
- `src/activities/games/display_lab/GrayPresetFile.{h,cpp}`: the preset file's path, its load and save through
  `Storage`.
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` and `TuningActivity.{h,cpp}`: the two Part B
  screens; `DisplayLabActivity` owns the session's preset and the Save preset row.
- `lib/hal/HalDisplay.h`: the one upstream touch, `EInkDisplay& eink()` under `FREEINK_CAP_GAMES` (ledger row 13).

# Display lab: the panel scan bench (Part A) and the gray engine (Part B)

The display lab is a bench, not a product mode. It lives in `src/activities/games/display_lab/` and
`lib/GameDisplayLab/`, compiles only under `FREEINK_CAP_DISPLAY_LAB`, which the `x4pro` development env alone sets, so
no release build (`x4pro-gh_release*`) ever carries it (the simulator's HAL stand-in has no `HalDisplay::eink()`, so the
row is absent there too), and shows its row only on an X4 Pro whose panel probe found a controller with a scan
backend (SSD1677 so far) or an UltraChip controller (UC8179, UC8279) without one, which gets the
[UltraChip stock-path bench](#ultrachip-stock-path-bench) instead of the scan screens. The row's subtitle says which:
"Panel waveform bench" for the scan screens, "UltraChip stock-path bench" for the stock bench.
For the same reason the flash-budget gate (`scripts/check_flash_budget.py`) builds both of its images with
`FREEINK_CAP_DISPLAY_LAB` unset (the display-lab epic, D6): it measures shipped firmware, so the lab's cost never
counts against the games budget, and the gate refuses a build whose metadata still defines the lab. Its
static-object scan (`objects`) therefore no longer reads the lab's screen code, which compiles empty without the flag;
it still reads `lib/GameDisplayLab`.
The scan menu has seven rows: Frame period, Polarity, Transition test, Gray viewer, Tuning, Save preset, Exit; the
stock bench's has three: Transition timing, Gray ramp, Exit.
Part A answers the two numbers only a panel can give before the per-pixel grayscale engine (Part B, the transition
test and tuning below) can be tuned:

1. the **frame period**: how long one frame of a one-frame activation takes, from the slope of BUSY time against
   TP[0A];
2. the **drive polarity** of the two LUT rows a pixel can select: which of LUT1 (the BW-plane row) and LUT2 (the
   RED-plane row) darkens the panel under the codes the lab tries first (VSL on row 1, VSH1 on row 2, InkBoy's SSD1681
   convention).

Safety: every LUT the lab sends carries the panel's stock voltages (bytes 105-109 = `0x17 0x41 0xA8 0x32 0x30`), the
OTP command is never sent, each scan goes through the SDK facade's overlay-grayscale path with `factoryMode=true`
(the upload of the 105 LUT bytes plus the voltage registers, command 0x22 = 0xCC, a timed BUSY wait), a scan whose
call took over 15,000 ms ends its run and the panel is cleaned (a wait cannot be interrupted, and the SDK's own BUSY cap
is 30 s, so a hung panel can stall the lab up to 30 s per scan), 2,400 scans since the last clean force a FULL refresh,
and leaving the lab by any route (Back, Home, sleep) runs one FULL refresh when a scan reached the panel since the last
clean, and frees the backend's two PSRAM planes and the lab's two PSRAM masks.

## Hardware checklist (the user's validation script)

1. Build and flash: `flock /tmp/crosshatch-build.lock sh -c 'pio run -e x4pro -t upload'`; open a serial monitor at
   the env's baud with `LOG_LEVEL` 1 or more (the `x4pro` development env prints `DLAB` lines at `LOG_INF`).
2. If the display is inverted (Settings), turn that off first: the facade drops grayscale calls while inverted, and the
   lab shows "Turn off the inverted display first" instead of its menu.
3. Home → Games. On an SSD1677 X4 Pro the last row is **Display lab** with the subtitle "Panel waveform bench"; on a
   UC8179 or UC8279 unit it reads "UltraChip stock-path bench" and opens the stock bench's menu instead (Transition
   timing, Gray ramp, Exit; follow [its own steps](#ultrachip-stock-path-bench) after the warning). Either subtitle is
   itself a check of the gate (`DisplayLabActivity::available()`, `usesStockBench`).
4. Confirm the entry warning. Back or Cancel there returns to the launcher and sends nothing to the panel.
5. **Frame period**: select it, then Run. The lab shows "Running" and performs nine all-hold scans (no pixel is
   driven, so the screen stays): TP[0A] in {1, 10, 100} against the frame-rate bytes {0x22, 0x11, 0x44}, tp outer.
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
6. **Polarity**: select it, then Row 1, labelled "Row 1 (LUT1 VSL)" on an SSD1677 (the backend names each row). The
   whole panel is driven with VSL for 10 frames and the screen is *not*
   repainted: look at the panel and note whether it went darker or lighter all over (the change is uniform and may be
   subtle, since 10 frames is a short drive). Only Confirm or a tap brings the screen back, with a FULL refresh; Up and
   Down move the selection but do not repaint while the drive is shown. After that repaint the next activation runs
   the row it lands on: select Row 2 and do the same with VSH1. Two log lines name each row and its time:

   ```
   [DLAB] polarity row=1 vs=LUT1 VSL ms=...
   [DLAB] polarity row=2 vs=LUT2 VSH1 ms=...
   ```

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
- A "Dropped" status (log line `dropped: scan N came back in M us, under the 500 us floor`) means the facade refused the
  call and the panel was not driven, usually because the display is inverted (the control-center panel can invert it
  while the lab is open; the lab also refuses outright, logged as `scan refused: display inverted`, when it sees the
  inversion first). Turn inversion off and run again; a dropped time is not a measurement.

## UltraChip stock-path bench

On the UC8179 and UC8279 batches no facade call reaches the waveform registers yet (the SDK lab seam is the epic's
option 1), so the one-frame scan bench cannot run there. The stock-path bench (story dlab-u1) measures what the public
facade already offers on those panels, with `HalDisplay` calls only: no custom LUT, no register write, no `eink()`. The
gate (`usesStockBench`, `StockBenchGate.h`): an UltraChip controller with no scan backend; a controller with a scan
backend always gets the scan screens. The lab opens no scan session for it (no backend, no masks), and keeps the entry
warning, the inverted refusal (on entry and again per run), the 15,000 ms timeout per timed step (the reference, the arm's base and gray pass, each flip, the ramp's base and
gray pass; a step over it ends the run before the next panel call), and one FULL refresh on
every way out of a screen that drove the panel (Back promotes it onto the menu's repaint; Home and sleep run it in the
screen's `onExit`).

**Transition timing** times the stock non-flashing previous-to-current transition, the facade call behind a grayscale
page's base: `displayGrayscaleBase(FAST_REFRESH)`. The bench calls `HalDisplay` with `turnOffScreen=false`; the reader
goes through `GfxRenderer`, which passes the sunlight fading fix as `turnOffScreen`, so with that setting on the
reader's transition also pays a power-off that these timings leave out. Run, blocking:

1. A checker of 4 x 2 blocks into the framebuffer, then one plain `displayBuffer(FAST_REFRESH)`, timed: the reference
   (`DLAB transition ref ms=`). It runs first because after a gray pass the drivers turn a plain FAST into the
   transition (`_redriveAfterGray`).
2. Arming. Per the drivers (`Uc8179Driver.cpp`, `Uc8279X4Driver.cpp`), `displayGrayscaleBase(FAST)` takes the
   transition only when a gray pass has run since the controller was last initialized (`_grayRefreshedOnce`; begin,
   wake and the UC8179 direct-gray setup clear it), the previous plane is valid, and no full clear is pending;
   otherwise it falls back to `display(FAST)`, a FAST DU when the previous plane is valid and no clear is pending, a
   flashing full GC otherwise. An ordinary refresh never arms it. So the run arms it with one overlay gray pass of
   all-B/W masks (both planes zero) over the same checker: `displayGrayscaleBase(FAST)` timed as the base, then
   `copyGrayscaleBuffers` + `displayGrayBuffer` timed as the gray pass, each checked against the timeout, then
   `cleanupGrayscaleBuffers` (untimed); one line logs their sum (`DLAB arm ms=`). A panel that reports no overlay
   grayscale (UC8279 variant 0x67) skips it (`DLAB arm skipped: no overlay grayscale`). The arm's base is a plain FAST
   on the first run after boot or wake, but the transition itself on later runs and after a Gray ramp pass or a reader
   AA page (a gray pass has already run), so `arm ms` is not comparable across those cases.
3. Ten flips: the checker, inverted on odd flips, through `displayGrayscaleBase(FAST_REFRESH)`, each timed
   (`DLAB transition n=<i> ms=<ms> flashed=<yes/no>`).

The table shows the reference, the arm, each flip (marked "fell back" when flagged), and the flips' min, median and
max (a flip over the timeout is left out of them); it is redrawn with a FULL refresh after every run. **The
fell-back heuristic** (`transitionFellBack`): `flashed=yes` means the flip fell back to a plain refresh instead of the
transition (the story's definition), not that the eye saw a flash. Unarmed, every flip is a plain refresh, so every
flip is flagged, though a FAST DU (the fallback when the old plane is valid) does not visibly flash; armed, every fallback the drivers can still take is a full GC
(a pending clear or no valid old plane), much slower than a FAST, so a flip taking 2 x the reference or more is
flagged. The factor 2 (`FELL_BACK_FACTOR`) is a guess, not a measurement: the table shows raw milliseconds so the owner
can judge, and the photo or eye tells a flash apart.

**Gray ramp** paints the OEM four-level grayscale: four bands, black, dark gray, light gray, white, left to right as the
device is held, through the overlay masks (`freeink-sdk/docs/grayscale-capabilities.md`: (LSB, MSB) dark (1, 1), light
(0, 1), B/W (0, 0); the base is black under the three darker bands). Activation: `displayGrayscaleBase(FAST_REFRESH)`
timed as the base, `copyGrayscaleBuffers` + `displayGrayBuffer` timed as the gray pass (what a caller pays for it),
then `cleanupGrayscaleBuffers` (untimed); logged as `DLAB gray base_ms=<> gray_ms=<>`. As with the arm, the first
activation's base after boot or wake is a plain FAST and later ones (or any after a Transition run or a reader AA page)
take the transition, so `base_ms` is not comparable across those cases. The panel keeps the ramp until the next
Confirm or tap, which only repaints the screen with a FULL refresh. A panel without overlay grayscale shows "This panel
reports no overlay grayscale" and logs `DLAB gray unsupported`; no pass is sent. On a UC8279, a gray pass whose MSB
plane covers over 25 % of the panel (this ramp's two gray bands cover half) runs the driver's four-tone quality bank.

Hardware steps (after the checklist's steps 1-4, on a UC8179 or UC8279 unit; the log shows
`DLAB UltraChip stock-path bench` on entry):

1. **Transition timing** → Run. The panel shows the checker, a gray pass over it, then ten flips. Copy the lines:

   ```
   [DLAB] transition ref ms=...
   [DLAB] arm ms=...
   [DLAB] transition n=1 ms=... flashed=no
   ...
   [DLAB] transition n=10 ms=... flashed=no
   ```

   and photograph the table. Note by eye whether any flip flashed (black-white-black). An armed flip flagged
   `flashed=yes` is the heuristic's guess at a full-GC fallback, which does flash; on an unarmed run every flip is
   flagged and none need flash, so there the flag says nothing about what the eye sees.
2. **Gray ramp** → activate the row. Photograph the four bands before pressing a key, and copy
   `[DLAB] gray base_ms=... gray_ms=...`. The next Confirm or tap brings the screen back with a FULL refresh.
3. Leave with Back: one FULL refresh.

Nothing here is calibrated yet: an armed flip that took the transition should not flash and should log
`flashed=no`; a flagged flip is the heuristic's guess at a fall-back, so report it with its time and what the eye saw.
A `timeout: ...` line means a timed step ran over 15 s: the run stopped and the next repaint is a FULL refresh.

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
pixel has a modelled level (0 black, 64 dark gray, 128 light gray, 192 white), a target shade, and a
count of doses left; a **dose** is one one-frame scan of that pixel toward its target (`frames = 1`). A pixel moving
from shade A to shade B gets the dose-table entry for A>B; one between shades gets the two neighbouring shades' doses
interpolated, rounded up; one interrupted mid-transition gets that scaled by the retarget percent of its direction
(toward dark when its new target lies below its level, toward light when above; clamped to 1..24). The model
advances only when a scan was delivered: `level += (target*64 - level) / remaining`, so the last dose lands on the
target. One **slot** is a drive scan of every pixel with doses left, then, when any pixel needs it, a follow-up
scan that drives again only the pixels whose transition's hold bit is off and that have two or more doses left (every other pixel holds, which ends
the pulse); a slot repeats every max(the slot's busy time + 5 ms, 100 ms) (the epic's D5). Nothing is dithered.

The preset starts as InkBoy's defaults on the SSD1677, the author's numbers for an SSD1681, not this panel's: finding
this panel's values is the point of the two screens. On a UC8179 or UC8279 panel it starts as the UltraChip preset
instead (the epic's D12, the owner's first UC8179 calibration run, where the full white-to-black swing took 12
one-frame doses at PLL 0x06, then calibrated on Static runs): the doses below with InkBoy's holds and everything else,
logged as `no preset at ...: UltraChip defaults`. A dose is 1..24. Its rate index 0 is the rate the table was measured at: PLL 0x06 on the UC8179
backend (the experimental branch's list {0x06, 0x05, 0x07}). Its retarget, 100 % both ways, is an uncalibrated
placeholder: on that run 150 % overshot toward white, and the shortfall toward black came through the old 12-dose
clamp, which the 24 cap lifts, so the range stays 50..150 % for both directions until a run measures them.

Shades are named by their seven-level number wherever the lab shows them (the epic's D14): **0** black, **2** dark
gray, **4** light gray, **6** white; levels 1, 3 and 5 arrive with dlab-g2. A transition such as 0>2 is black to dark
gray, and a shade's model level is its number x 32. The stock bench keeps its own labels.

| From > To | 0>2 | 0>4 | 0>6 | 2>0 | 2>4 | 2>6 | 4>0 | 4>2 | 4>6 | 6>0 | 6>2 | 6>4 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| UltraChip dose | 2 | 7 | 12 | 7 | 2 | 10 | 7 | 6 | 9 | 12 | 5 | 3 |

InkBoy's:

| From > To | 0>2 | 0>4 | 0>6 | 2>0 | 2>4 | 2>6 | 4>0 | 4>2 | 4>6 | 6>0 | 6>2 | 6>4 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Dose | 2 | 4 | 7 | 4 | 1 | 6 | 4 | 1 | 5 | 7 | 3 | 2 |
| Hold | on | on | off | off | on | off | off | on | off | off | off | on |

Retarget 100 % both ways, rate index 0, no polarity swap, and InkBoy's routes (4>0>4 return -1 and reverse 150 %, the
rest neutral) are InkBoy's too. History and weak mode are off: this story's choice (InkBoy ships history "both" and weak
"normal"); the preset file turns them on.

### Transition test

Display lab → Transition test. Six rows: **Run** (starts a run; its line says how the last one ended), **Mode** (a tap
or Confirm steps Static, Slow, Fast, Ladder; it opens on Static on a UC8179 or UC8279 and on Fast on the SSD1677),
**Hold for photos** (On or Off, a tap or Confirm toggles it; On every time the screen opens, never saved with the
preset; it reads "Always in Ladder" while Mode is Ladder, which holds either way; see [Holds for
photos](#holds-for-photos)), **Clean** (one cleaning FULL refresh to white and the rows back, no run, the Run row
unchanged), **Speed** (the last "N.N scans/s, N ms per scan") and **Controls**. Run is also the replay: every run cleans
to white first and starts from 0.

A run is on its own FreeRTOS task (the epic's D11), so the buttons answer while a scan is in flight. The panel is
cleaned to white with a FULL refresh, then the screen stops drawing: the panel shows only what the scans drive. Four
shade bands, 0, 2, 4, 6 from the top, each with three rectangles of the other three shades (in 0, 2, 4, 6 order),
following the screen's orientation. The mode sets the rectangles (story dlab-c3, after the owner's UC8179 run, D12):

- **Fast**: 12-px rectangles sweep from left to right in 5 s, pause 1 s, and repeat (the SSD1677's sweep). Like Slow's,
  the rectangles wait just off the left edge while the bands paint from white, so none lands on a band still filling;
  the slot after the paint has settled (a slot with nothing left to drive, `gray fast: paint settled after N slots; the
  sweep starts`) the sweep starts from its 0, and the run's start line gives the crossing as `5000 ms after the
  paint`. On a panel
  whose slot is several hundred milliseconds the rectangle moves about 60 px a slot (0.4 s) and retargets only its
  12-px footprint at each, so it leaves spaced stripes of partial transitions rather than a solid track. It is the one
  mode whose rectangles retarget pixels still mid-transition, so it is the one that exercises the retarget split.
- **Slow**: 48-px rectangles, the same sweep and pause, over a crossing sized so that a pixel stays under a rectangle
  for the preset's longest dose run in slots: per transition its dose when its hold bit is on (one dose a slot), half
  of it rounded up when off (the follow-up scan gives a second), the largest of the twelve (4 for InkBoy's preset, 7
  for the UltraChip one). Crossing = those slots x slot period x (screen width + 48) / 48, at most 120 s. The
  rectangles wait just off the left edge while the bands paint from white; each painting slot is measured (its real
  length, the scans and the work over every pixel, at least the scheduler's period), and once the paint has settled (a
  slot with nothing left to drive) the crossing is sized from the longest of them and starts, so no rectangle lands on
  a band still painting. The run's start line says the crossing is pending; then `gray slow: slot period N ms,
  crossing N ms, dwell N slots` (with `(capped at 120 s, ...)` when the cap left the dwell under the longest run; at
  380 ms the UltraChip preset crosses 800 px in 47 s).
- **Static**: rectangles a third of the width wide in the middle, standing still. Every pixel runs its whole dose. Once
  a slot finds no pixel left to drive the pattern has settled, logged as `gray static: flip N settled after M slots
  (offset +K, next +J)` (flip 0 is the first paint, +K the offset that flip moved the shades by, M the slots that drove
  pixels), and it flips (with Hold for photos On, after the hold: see [Holds for photos](#holds-for-photos)): every
  pixel takes one full transition, with its table dose, to the shade the next offset along (0, 2, 4, 6, 0, ...). The
  offsets run +1, +2, +3, +1, ..., so flips 1 to 3 take the pixels through all twelve transitions between them, and
  every later three again. The residue after a flip, per band, is the per-transition answer. The session cap's clean
  (every 2,400 scans) restarts the sequence at flip 0 from white (`gray static: sequence restarted at flip 0 after the
  session-cap clean`). With `history` on in the preset file, a flip back to a pixel's origin is a completed return or a
  reversal, budgeted by that route's `return_delta` or `reverse_percent` instead of the table dose, so those flips'
  residues include the history adjustments; a Static run's start logs `gray static: history N is on, so the flips'
  residues include its route adjustments`.

- **Ladder** (story dlab-g1, the epic's D14): no bands or rectangles, but the panel's response to single one-frame
  pulses, from which the seven levels and their doses are read:
  - *Layout*: The rows above the marker's 16 (in Ladder the marker's box is 32 x 16 px, with no rectangles to keep clear
    of) split into a top half that starts from white and a bottom half that starts from black; the marker's rows below
    them belong to the bottom half. Each half holds 24 columns, column n (1 to 24, left to right, following the
    orientation), each followed by a 1 px gutter of the half's own shade, so the columns can be counted and an untouched
    strip of the origin sits beside each: 19 px columns on a portrait screen (480 wide), 32 px on a landscape one (800
    wide, the 8 px left over on the right holding the half's shade). The rule: the pitch is the width / 24 and a column
    is the pitch less its 1 px gutter; under 48 px wide, 1 px columns without gutters, as many as fit (`gray ladder:
    narrow screen: ...`).
  - *Base*: The clean leaves the panel white, so the bottom half is first driven to black with the preset's 6>0 dose and
    settles (`gray ladder: base painted after N slots`). The ladder reads only the preset's rate index and polarity, and
    that 6>0 dose for the base; the other Tuning values do not change the pulses.
  - *Stage 1, the ladder*: Column n gets exactly n one-frame pulses toward the opposite shade (the top half's toward
    black, the bottom half's toward white), one a slot, each pulse one scan (the preset with every hold bit set, so no
    follow-up scan), set directly rather than from the dose table. A stage is 24 slots and its columns are end-aligned:
    column 24 pulses in all of them, column n in the last n, so every column's last pulse lands in the same slot and all
    rest equally long before the photo. The run then holds (`ladder 1`).
  - *Stage 2, the return*: Next drives each column back toward its origin with the same n pulses, end-aligned again; it
    settles and holds (`ladder 2`). Next again ends the run, as Back does but with "Finished" on the Run row (`gray
    ladder: done`); Run replays it from the clean.
  - *Holds and pauses*: The Ladder holds at both photos whatever the Hold for photos row says (it is nothing but its
    photos): the row reads "Always in Ladder" while Mode is Ladder, and the run's start line says so too. A pause with
    Next between two of a stage's pulse slots puts a gap in the one-pulse-a-slot rhythm; the stage's line counts such
    pauses and names the stage slots the first four fell after (a pause before the first pulse or after the last is
    harmless and not counted).
  - *Session-cap clean*: It restarts the ladder from the base (`gray ladder: restarted from the base after the
    session-cap clean`), which holds at `ladder 1` first again, whichever stage it cut; except when it cuts off the
    marker's way back from `ladder 2`: both photos are taken, so the run ends as a finished one (`gray ladder: done (the
    session-cap clean came after both photos)`). In practice a Ladder run never meets the cap: the run's own clean
    resets the count, and a run is some 150 scans against the cap's 2,400.
  - *Log*: Each stage's end logs what its photo needs: `gray ladder: stage N settled after M slots: rate R (label), swap
    S, one pulse per slot[, paused P times after stage slots a, b, ...], pulse interval up to I ms, slots up to T ms,
    base 6>0 dose D, column n has had (has been driven back) n pulses (end-aligned), W px columns, gutter G px`. I is
    the longest start-to-start interval between two of the stage's pulse slots and T its longest slot.

#### Holds for photos

With **Hold for photos** On (story dlab-c4, the epic's D13) the run holds itself at each mode's photo spots, so a photo
does not depend on pausing within half a slot:

- **Static**: every settle (`static settle N`, N the flip that settled); the flip waits for the resume.
- **Ladder**: the end of stage 1 (`ladder 1`) and of stage 2 (`ladder 2`), with Hold for photos Off too; the next
  stage, or the run's end, waits for the resume and the marker's way back. The base is no photo spot. The marker sits
  on the black rows below the bottom half and goes to 4, never over a column.
- **Slow**: the bands once their paint has settled, before the rectangles enter (`slow painted 0`, the slot that sizes
  the crossing); the rectangles once they are wholly on-screen and the preset's longest dose run in slots has passed
  since their left (trailing) edge came on (`slow dwelt N`, N the crossing). The crossing is sized so that the trailing
  column has had the full run by the first slot with the rectangles wholly on-screen, so the wait does not change the
  ramp under the rectangle (its full shade at the trailing edge back to the band at the leading edge, which has just
  arrived); what it adds is a rectangle-wide trail behind it whose oldest column has finished its return. A crossing
  whose rectangles reach the right edge first skips the spot (`gray hold: slow dwelt N skipped (the rectangles reached
  the right edge first)`). Then the end of the crossing, the rectangles off the right edge and their trails behind them
  (`slow crossed N`, the first slot of the pause).
- **Fast**: the bands once their paint has settled, before the first sweep (`fast painted 0`); then the end of each
  sweep (`fast pause N`, the first slot of its 1 s pause). A slot longer than the pause can
  leave no slot starting in it; that pause is logged as `gray hold: fast pause N skipped (no slot started in the pause)`
  (Slow's crossing end likewise).

At a spot the run shows the hold marker, a pause symbol: two bars in the bottom-right corner of the logical screen, each
12 px wide with an 8 px gap between them in a 32 px box (the gap keeps the band's shade and is never driven). The box
sits in the bottom band's margin below its last row of rectangles (8 rows on a landscape screen, 10 in portrait,
following the orientation; a 16 px square, bars of 4, on a screen too small for rectangles; 32 x 16 px under the
Ladder's columns), so it never shares a row with a rectangle or its trail. The bars are driven by the gray engine's own
scans to the band's opposite shade (two steps along 0, 2, 4, 6), since nothing can be drawn through the renderer while
the view is up. On a preset whose dose from the band's shade to that one is weak the bars can be faint; the `gray hold:`
log line is the reliable signal that the run holds. The sweep clock stands still: the task runs slots until the marker
has settled, then holds (nothing is driven). Next (or Confirm) resumes: the marker is driven back to the band's shade
and settles, still with the clock standing, and only then does the run go on (Static's flip, Slow's crossing, the rest
of the pause). The marker's slots never count toward Static's settle or Slow's measurement.

Two effects of those slots, out and back: pixels still in flight elsewhere (Slow's rectangles, Fast's trails) keep
receiving their doses with the scene standing still, so the photo shows them a few slots further on than the spot
itself; and every Fast pause and every Slow crossing's end lasts those slots longer, so the trails settle further than
they would without a hold. The build-up photo (five or more full sweeps) is therefore taken with Hold Off.

Previous or Back while held stops as always, with the FULL refresh. Auto-sleep waits while the run holds (it would end
the hold with its FULL refresh), for at most 10 minutes from the hold's start; after that, as during a pause asked for
with Next, the normal idle timeout applies and auto-sleep ends the run as any leave does, so a forgotten hold does not
keep the device awake for good.

When the session cap's clean falls in a hold's marker slots the hold is dropped and the panel repaints from white; the
log says which case it was and how many marker slots the hold had run: `gray hold: <spot> dropped by the session-cap
clean after N marker slots: holds again at flip 0` (Static, its sequence restarting at flip 0 and holding at its
settle), `...: the ladder restarts from the base` (Ladder, holding at `ladder 1` first again, whichever stage's hold was
dropped), `...: skipped for this crossing` (Slow or Fast: the next spot holds, a dropped painted spot starts the
crossing without its photo, and the bands repaint under the crossing without waiting or measuring again), or `...: photo
taken, the marker's way back cut off` (any mode, dropped on the way back; the Ladder restarts from the base after
`ladder 1`'s and ends after `ladder 2`'s). With Hold Off nothing holds but the Ladder, no other marker is drawn, and
Static flips as soon as it settles.

Controls while it runs (the Controls row says the same, by action: "While it runs: Next or Confirm pauses and resumes,
and ends a hold for photos; Previous or Back stops"). The X4 Pro has no front buttons and its touch routing is dead
while the view is up (D12), so the side buttons are the controls there. Pause and resume take effect at the end of the
slot in flight (a slot's drive scan and its follow-up stay together); a stop takes effect after the scan in flight, or
after the clean in flight (the session cap's clean is one FULL refresh).

- **Next** (NavNext: side Down or front Right; Up or Left in inverted portrait and counter-clockwise landscape,
  where the axis follows the screen), **Confirm** where there is one, or a tap over the screen's row area on a device
  whose touch routes while the view is up (not the X4 Pro): pause; again to resume (the sweep clock stands still while
  paused). While the run holds for a photo, one press resumes it. A press while the marker is still being driven out
  or back pauses as usual, and the next press resumes where it was: the marker's slots, then the hold (which the
  press after that ends) or the run.
- **Previous** (NavPrevious, the opposite keys; on its release) or **Back** (a front button where there is one, or
  the left-edge swipe, which is checked before the other swipes are consumed): stop and return to the rows with a
  FULL refresh. The Run row says "Stopped" ("Finished" when a Ladder run ended on its own), or how the run ended when
  a scan was not delivered ("Timed out", "Dropped", "Refused (see log)"), which stops it at once. The Speed row shows
  the last "N.N scans/s, N ms per scan". Any other swipe does nothing while the test view is up.
- Another screen over the test (the light panel's top-edge swipe) stops the sweep: once the screen's loop has not run
  for 0.3 s the task sends no further scan, and when the other screen closes the view ends as Back does, with the rows'
  FULL refresh and "Covered by another screen" on the Run row. The check cannot tell a cover from a stall: a main-loop
  pass that takes over 0.3 s while the view is up ends the run the same way. A forced refresh (the power or home
  button's Refresh) does nothing while it runs.
- Home and sleep end the sweep task before the screen frees anything; a task that was waiting for the render lock
  when sleep or a screen change took it is deleted where it waits.

Every 2,400 scans the panel needs a cleaning refresh (the lab's session cap): the sweep cleans to white just before
it and carries on. The log has `DLAB gray scans_s=N.N ms_per_scan=N skipped=N` about every 2 s and on every stop
(`skipped` counts the slot periods lost to slow slots in that same window, about 2 s; they are dropped, never
queued), and at the end of every run `gray sweep task ended: stack N bytes, high-water N bytes free, least before a
scan N bytes` (an error line follows when under 1,024 bytes were left; the same line is logged when sleep or a
screen change deleted the task, by the screen's loop task). Other lines, by the task that logs them:

- sweep task: `gray sweep: loop silent for over N ms, a screen covers the view; ending` (its cover check, before a scan
  or a clean); `gray sweep: scan refused: no lab session (backend or masks)`, `...: display inverted`, or `...: rate
  index N of N` (the run ends "Refused (see log)"); `gray static: flip N settled after M slots (offset +K, next +J)`;
  `gray static: history N is on, ...`; `gray static: sequence restarted at flip 0 after the session-cap clean`; `gray
  slow: slot period N ms, crossing N ms, dwell N slots`; `gray fast: paint settled after N slots; the sweep starts`;
  `gray hold: <spot> N, marker X over Y` (a photo spot: the marker's shade over its band's, by level number), `gray
  hold: marker settled after N slots; holding, Next resumes`, `gray hold: resumed; marker back to Y`, `gray hold: marker
  back after N slots; the run goes on` (`...; the ladder is done` at a Ladder run's end), `gray hold: <spot> N skipped
  (<reason>)`, `gray hold: <spot> dropped by the session-cap clean after N marker slots: <case>`; `gray ladder: N
  columns of W px, gutter G px; rows A-B from white, C-D from black (`none` for a half without rows), the marker's rows
  below black; painting the base` (a Ladder run's start, followed by `gray ladder: narrow screen: N of 24 columns, W px
  each, no gutter` under 48 px wide), `gray ladder: base painted after N slots; ...`, `gray ladder: stage N settled
  after M slots: rate R (label), ...`, `gray ladder: restarted from the base after the session-cap clean`, `gray ladder:
  done` (`... (the session-cap clean came after both photos)`); the stats, pause, resume, clean and start lines (the
  start line names the mode, the rectangle width ("no rectangles" for the Ladder, whose layout line follows, crossing
  "none"), the crossing, the hold (for the Ladder: that it always holds) and both retarget percents).
- loop task: `gray sweep: covered by another screen` (its own cover check, when the screen's loop runs again after the
  gap; a cover usually logs this line and the sweep task's); `gray sweep: task deleted while it waited for the render
  lock` (sleep or a screen change ended the task inside a scan or a clean); `Cannot create the sweep task (N byte
  stack)` (no internal RAM for its stack; the Run row says "Stopped: not enough memory"); `gray sweep stopped:
  <outcome>` when the view ends; `transition test mode: static|slow|fast|ladder` (the Mode row); `transition test: panel
  cleaned to white` (the Clean row); `transition test hold: on|off` (the Hold row).

How to read it:

- **Rectangle shade against its band** is the dose: a rectangle meant to be 4 (light gray) that looks like 2 on a 6
  (white) band has too many 6>4 doses (or 6>4 should not hold); one that never gets dark enough has too few.
- **The track behind a rectangle** is the return to the band shade (hold and history): a dark or light trail after the
  rectangle passes means the return transition's dose or hold bit is off, or needs the history routes.
- **Build-up over passes**: a band that drifts darker or lighter over several sweeps means a pair of opposite doses
  (A>B and B>A) does not cancel.
- **Endpoints**: the 0 and 6 bands and rectangles must reach full black and white; if they do not, raise 0>6, 6>0, or
  the rows into 0 and 6.

Tuning order, one change per run: the **doses and hold bits** first, judged by Static's settled pattern and its flips
and by the rectangles and their tracks; then the two **retarget** percents, in **Fast** mode only (Static and Slow retarget settled pixels,
which take their table dose; Fast's rectangles move on before their pixels settle, which is what a retarget scales):
toward dark for pixels turned toward a darker shade than their level, toward light for the others; then, only
if trails remain, the **history** routes and the weak row, which the preset file turns on (no row on the device).

What to photograph, with Hold for photos On (each hold has its `gray hold:` line): Static's pattern after each settle,
Slow's painted bands, its rectangles after a full dwell, and its crossing's end with the tracks, Fast's painted bands
and its pause (the tracks); the pause symbol in the bottom-right says the run is holding, and the photo is the rest of
the panel. The panel after five or more full sweeps (build-up) is taken with Hold for photos Off, since each hold's
marker slots let the trails settle further. Keep the Tuning values and the log's `gray` lines beside each.

The Ladder's two photos (D14: the seven levels and their pulse counts are read off them), at rate index 0 first (the
rate the table was measured at; index 2 is the fallback if it separates fewer levels), each held with its `gray
ladder: stage N settled` line beside it:

- **`ladder 1`**: the top half is the response to n pulses from white, the bottom half to n pulses from black, column
  n with n pulses, n = 1 at the left, every column's last pulse in the same slot; the 1 px gutters show the untouched
  origin beside each. Read each column's darkness against black and white: the first column that looks
  fully black in the top half is the full 6>0 swing in pulses (12 expected on the UC8179 at PLL 0x06, D12), and the
  levels between are where the curve is steep or flat; the bottom half is the same from black, which is coarser (D14).
  A level for the seven-level table is a column whose shade sits where that gray should, and n is its dose from white
  (top) or black (bottom).
- **`ladder 2`**: every column has had n pulses back toward its origin. A column that is back at the origin shade
  means n pulses each way cancel; one that is not shows the asymmetry of that direction (top half: n pulses toward
  white after n toward black; bottom half the reverse), which is the residue a return transition needs a different
  dose for.

Before reading either photo, check its line:

- **Pulse interval**: the figure to read for the rhythm is the stage's pulse interval, the longest start-to-start
  time between two pulse slots; a skipped slot period or a pause shows there, where the slot's own length (`slots up
  to`) does not. An interval well over the slot length means some pulses rested longer than the rest.
- **Pauses**: a pause after stage slot s puts a gap between the pulses of every column n > 24 - s (the columns already
  pulsing then); distrust those columns, or take the photo again without pausing.
- **The base**: the bottom half's black is the preset's 6>0 dose (in the line); if that dose is under the panel's full
  swing the bottom half starts gray, and its columns measure from that gray, not from black.
- **Temperature**: note the room temperature with each ladder photo; the pulse response depends on it (the panel's own
  sensor is a backend read, outside this story).
- **Rest**: end alignment gives every column the same rest after its last pulse, but not before its first: column 1
  rests longer than column 24 between the base (the top half: the clean) and its first pulse.

### Gray viewer

Display lab → Gray viewer (story dlab-g3, the epic's D16) paints a picture through the gray engine, to see how its
shades look on real artwork, line art first. Six rows:

- **Image**: the entry the next run paints, as "name (n of N)". A tap or Confirm steps it: the test chart first, then
  the PNG and JPEG files (`.png`, `.jpg`, `.jpeg`, any case) of `/.crosshatch/display-lab/images/` sorted by name
  (case-insensitive), then the chart again.
  - Hidden files (a leading dot, as macOS's `._` files), folders and other files are ignored.
  - At most 32 files are listed: the first 32 by name, whatever order the card's directory holds them in (the rest
    are logged as not listed). A name of 64 bytes or more is skipped (logged).
  - Without the folder there is only the chart.
  - The repo's `docs/crosshatch/display-lab-images/` holds four 480 x 800 test PNGs to copy there (its README says
    what each shows).
- **Dither**: Plain or Ordered; a tap or Confirm toggles it.
  - Plain is the nearest level.
  - Ordered is the 4x4 Bayer matrix scaled to the level count: line art and text keep their edges with Plain, and
    gradients are Ordered's case.
- **Run**: clean, paint, settle, hold.
- **Levels**: the engine's level count and names ("4 levels: 0, 2, 4, 6"). They come from the engine, so dlab-g2's
  seven levels appear here and in every picture with no change to the viewer.
- **Controls**: help text.
- **Status**: how the last run ended: "Not run", "Stopped", a scan's outcome, or why the picture could not be read.

A run runs on the transition test's sweep runner: the same task, slots, stops, cover and session cap.

1. It cleans the panel to white with a FULL refresh, then paints the entry into the model:
   - The **test chart**, the same pixels on every device, top to bottom in fifths: the engine's levels as equal steps;
     a continuous gradient; hatching at pitches 3, 6 and 12 px in thirds; then concentric 2 px rings every 16 px in
     the left half and 45-degree strokes 1, 2, 4 and 8 px wide in the right.
   - A **file**: read by the viewer's own reader (`GrayImageReader`). The firmware's image converters cut gray to two
     bits before they write the 1 bpp framebuffer, so the reader drives PNGdec and JPEGDEC directly (D16's amendment).
     - Rows are converted to 8-bit gray as the converters convert them (every PNG pixel type, indexed and 1/2/4-bit
       gray included, alpha composited on white; a JPEG as 8-bit gray at JPEGDEC's 1/2, 1/4 or 1/8 scale when the
       picture shrinks that much, and a progressive one at 1/8).
     - The picture is scaled to fit the screen, up or down, with its aspect kept, centred on a white letterbox:
       bilinear along an axis it enlarges (centre-aligned taps per axis; only the 16.16 blend arithmetic is the JPEG
       converter's), with the previous PNG row, the previous JPEG strip's last row and the previous block's last
       column kept (`SeamKeeper`, the same for both decoders) so a blend crosses those seams (a 1/8-scale progressive JPEG enlarges smoothly, not in 8 px
       bands), nearest at pixel centres along one it shrinks.
     - The converters' limits apply: each side up to 32,767 px, 8 MP, and a PNG row within the firmware's PNG buffer
       (about 2,000 px of colour with alpha; the expanded gray row is capped at 8,208 px).
     - Not decoded ("format or size not supported"): interlaced or 16-bit PNGs, a PNG colour type that does not
       exist, extended or lossless JPEGs, and a file whose layout is cut or malformed. Against a decoder that never
       returns (PNGdec 1.1.6 spins without end on a truncated file) or writes past its object, the reader has four
       layers:
       - **The walk.** Before a decoder reads anything, the file's layout is walked against its size, and the walked
         size, all the decoder is ever given, ends where the walk ends.
         - A PNG's chunks through IEND: every chunk up to it inside the file, a PLTE of at most 768 bytes in whole
           triples, a tRNS of at most 256, at most 65,536 chunks. The walked size ends at IEND: PNGdec has no IEND
           case, so a chunk after it (a PLTE over 1,024 bytes would be copied past its palette) is never given to it,
           and its reads there are filler, which reads as an invalid chunk length.
         - A JPEG's header from SOI to SOS, at the offsets JPEGDEC's own header parse visits (it steps over a fill
           byte, or anything under 0xFFC0, three bytes at a time, and reads a length after every other marker, SOI and
           RSTn included), and with its 2 KiB parse buffer where it has it (it moves it on once the parse is 1 KiB
           in, at most 256 times): every marker and segment inside the file, and the segments it reads out of that
           buffer wholly inside it, checked as it copies them: a SOF0 or SOF2 of 1 to 4 components; a DQT of whole
           65- or 129-byte tables; a DHT of whole tables, each of class 0 or 1 and index 0 or 1 (JPEGDEC keeps two
           DC and two AC tables, and builds a DC table of index 2 or 3 past its two), with at most 256
           codes whose lengths are not over-subscribed (the Kraft bound: an over-subscribed table makes JPEGDEC write
           past its tables as it builds them), and values the standard allows for 8-bit samples: a DC category up to
           11 and an AC size up to 10 (larger ones would make a block longer than the decode keeps in its buffer, 209
           bytes at most with these); an APP1 it takes for Exif with its IFD offset, each IFD's tag count and
           entries, and the next IFD's offset inside the segment and the buffer; and the SOS segment, after the frame,
           as JPEGDEC takes it whole (its length checked before its component count is read, so a cut one is
           malformed): 1 to 4 of the frame's components, each named once, with DC and AC tables of index 0 or 1
           defined by an earlier DHT (an AC index of 3 would have it read about 20 KB past its object); a baseline
           scan covers every frame component with a DC and an AC table, over the spectral range 0 to 63 with no
           successive approximation (Ss 0, Se 63, Ah 0, Al 0); a progressive file's first scan is DC only (Ss and Se
           0, Ah 0) with its DC tables defined. Then
           the picture must end: a baseline file whose last two bytes are FF D9, with no MPF APP2 saying more
           pictures follow, ends there (one read); otherwise its first FF D9 after SOS, found by reading forward
           from it in 4 KB reads (a motion photo's video, a second picture such as an MPF preview, or padding after
           the picture is not given to the decoder); a progressive file ends at the first marker after its first
           scan (JPEGDEC decodes that scan alone), or its first 2 KiB if that is further (JPEGDEC refuses a first
           read under 256 bytes), so a progressive file cut after that scan paints. With none, a scan cut short would
           leave JPEGDEC decoding past the data it has. The log says how long the walk took (`<kind> walked in N
           ms`).
         - A file without IEND (a PNG), SOS or an EOI after it (a JPEG) is refused as "The picture file has no end",
           one under a signature's worth of bytes as "too short", a read that fails as "could not open".
       - **The reads.** The decoder is told the walked size, and no read gives it a byte of the file past that size,
         after a seek the file refused, or once a stop is asked. PNGdec always gets the length it asked for, the rest
         0xFF (its IDAT loop moves on and the data ends in an error). JPEGDEC gets zeros for the rest, never 0xFF
         (its header parse would keep reading) and never the buffer as it was (after a seek past a big APP1, an empty
         read leaves SOI and the APP1 marker, which send it round for ever). While open() runs, a read that starts
         before the walked header's end (the SOS segment's) is the header parse's; every other read is the decode's,
         the header parse's last top-up included. The reads keep the file's position themselves (from 0 at the
         open, a seek's target and each read's bytes), so a read that can give nothing (past the walked size, after
         a refused seek or a failed read, once stopped, past the cap) makes no file call at all, and JPEGDEC is told
         that position. A read that fails is tried once more, from a seek back to its position (an SD card's
         transient error), and the walk's reads likewise, before the picture fails as "could not open"; a second
         failure is final, never a loop. The callbacks' bookkeeping is one header the host tests run too
         (`DecoderReads.h`):
         - the header parse keeps a read whole or not at all: a stop, a short or failed read, a refused seek, or more
           header reads than the walk's buffer moves and four, and the read is dropped: the whole 2 KiB buffer is
           zeroed, the bytes already in it too, so no segment it began meets zeros (a DQT whose length or tables did
           would wrap its count and read past its object), and every later header read gives nothing; the parse
           steps over the zeros to an error, and the picture is "could not open" whatever open() says;
         - the decode's reads are never dropped that way: told the whole span, so its buffer is always topped up and
           its bit pointer stays in it, and when a refill would leave it starved (a run of 0xFF, an erased sector,
           filters to nothing) each 0xFF followed by another 0xFF becomes 0; a stuffed 0xFF 00 is never touched;
           restart markers (0xFF D0 to D7) are taken out of each refill (JPEGDEC counts restarts by its interval,
           and its filter drops them anyway) and the room filled with more of the file, so a picture with a restart
           marker after every block over flat areas, two thirds markers, never starves it; after 16 top-ups with the
           file going on, the bytes it has are told (fewer than asked: the patch below refills again before the next
           block), and with none yet it reads on until some come or the file ends, so a decode read is never answered
           with nothing while the scan has data (JPEGDEC's filter, given nothing, reads the byte after a 0xFF it
           holds); a valid picture decodes
           exactly as it is; it gets the file's bytes for 2 reads per 2 KiB of the file and 64 more (each with up to
           16 top-ups), then nothing. open() gets four such reads at most (it makes one), then nothing, so a parse
           that ran past the walked header still ends.
       - **The decoders' patches.** Both libraries carry the lab's build-time patches, only where the lab is built:
         `scripts/lab_decoder_patches/pngdec` and `.../jpegdec` (fork-only folders), applied by
         `scripts/patch_lab_decoders.py` from the x4pro env's `extra_scripts` to that env's library copies, JPEGDEC's
         after upstream's `scripts/jpegdec_patches`; the host decoder test's copies get the same. PlatformIO keeps one
         library copy per env, so the development X4 Pro build's own converters (covers, chapter images) share the
         patched libraries. The default, sticky and release envs (`x4pro-gh_release*` included) run no such script
         and build stock PNGdec and JPEGDEC (with upstream's own patches): their converters keep the over-reads below,
         JPEGDEC's MCU overrun on the cover and chapter-image path included, as upstream has them today; the lab adds
         no exposure there and removes none. The patch files are written as suggestions for upstream. A patch the lab
         no longer carries is kept in `scripts/lab_decoder_patches/retired/<library>`: the script reverses it from a
         library copy that still has it (an older build's), and fails naming the library folder to delete when it
         neither reverses nor the current patch applies; it does nothing on a clean or before the libraries are
         installed. The fork CI checks each lab patch is in the x4pro build's copies and in the test's.
         - PNGdec (`crosshatch-0001-refill-before-palette-copy`): a PLTE or tRNS is copied out of its 2 KiB file
           buffer; one that runs past the bytes in it (an indexed PNG with 1.3 to 2 KB of iCCP or text before its
           palette, or about 1 KB before a palette and its alpha table) is read again from its start first, and one
           larger than the palette is refused (as released it read past the decoder object, a wrong palette or a
           crash). Other envs keep that over-read; the fix is a suggestion for upstream.
         - JPEGDEC (`crosshatch-0001-refill-between-blocks-stop-an-overrun`): as released it refills only after a
           whole MCU that ends past its refill mark, which keeps 512 bytes, and a colour MCU can be longer (a 4:2:0
           picture of noise-like detail saved at quality 100 runs 510 to 600 bytes; a crafted one of the longest
           codes about 1.24 KB): its bit pointer then ran past the data for good. The patch refills before every block
           whose data left is under 256 bytes (a block of the longest codes the walk lets through is 209 bytes, 213
           with the 4 bytes its bit window reads ahead), and loads the bit window again from the refilled bytes, so
           such pictures decode whole; a
           pointer that still passes the data (a reader that gives less than the file has, as the converters' do
           at a cut) ends the decode with an error, the pointer back at the data's end; and an error from one
           decode no longer carries into the next decode of the same open.
       - **The exit.** A stop waits for the task at most 20 s, in all (below).
       - **The watchdog.** For the whole read of a picture, its walk included, the sweep task joins the task watchdog,
         which the x4pro build configures to reset the device after 5 s without a feed (its prebuilt `dio_opi`
         sdkconfig: CONFIG_ESP_TASK_WDT_INIT, CONFIG_ESP_TASK_WDT_PANIC, a 5 s timeout for every watched task), and
         feeds it on progress: every row a decoder hands over and every read that gives file bytes. Nothing in the SD
         card feeds it: around each blocking file call (open, read, seek, close and a file handle's end, each waiting on
         the card's mutex, which another task may hold for longer than 5 s) the task leaves the watchdog and rejoins
         after, until it has gone 5 s outside file calls, or 10 s in all less the time in calls that succeeded, without
         progress, from when it stays watched through them too: a long wait for the card before a read that gives bytes
         is never a reset, while slow failing reads with a spin between them are. A decoder that spins where no read or
         row reaches it, or reads on without progress, then resets the device within about 10 to 15 s instead of holding
         the runner. A task the watchdog already watches is fed but never left; a join or rejoin that fails is tried
         once more. A watchdog that is not configured, a join that fails and a rejoin that fails are each logged once,
         with the error (`gray viewer: ... the rest of the read runs unwatched`). The decisions are `DecoderReads.h`'s
         `TaskWatchdog`, host-tested over a fake watchdog. The restart status stays for the case it cannot reach, a task
         stuck in the SD card behind its mutex.
     - A JPEG's EXIF orientation is ignored, as the firmware's converters ignore it: a picture is shown as its pixels
       are stored, so a camera's portrait photo may appear on its side.
   - Each row is quantised with the Dither row, and each run of equal shades in a row is one retarget.
2. The renderer's framebuffer is never drawn while the view is up (the clean's white fill is the lab's own clean).
3. Slots run until one drives no pixel: the paint has settled.
4. The hold marker (a pause symbol, 32 x 16 px at the bottom-right) is driven to 0 or 6, whichever is farther from the
   picture under it. Once it has settled the run holds; the device stays awake for ten minutes of a hold, as the
   transition test's.
5. The marker is never driven back: the next picture cleans first.

Controls while the view is up:

- **Next** (side Down) or **Confirm**, or a tap where touch routes:
  - while the run's first picture, or a next one, is being cleaned and read: ignored, so a double press cannot skip a
    picture or pause the one being painted;
  - while it paints its slots (or drives the marker out), pause and resume, as in the transition test;
  - while it holds, paint the next entry (clean first; after the last file, the chart). The Image row then shows it.
  Each press is one step on the sweep's request word, which decides by what the word holds at that moment whether
  it is a next picture or a pause or resume, so a press racing the hold is never lost; a hold ends only when the task
  serves the resume or the next picture, so a resume and a pause before it does leave the hold (and its ten-minute
  bound) in place. A resume that ever reaches a held picture holds again (logged `gray viewer: still held; Next paints
  the next picture`).
- **Previous** (side Up) or **Back**: stop; the rows come back with a FULL refresh.
- Another screen covering it, Home and sleep end it as they end the transition test.

A file that cannot be read ends the run with the reason on the Status row: it could not be opened (a missing or
unreadable file, a read that failed, or a header the library rejects as corrupt), its format or size is not supported
(the limits above, a cut or malformed layout), it has no end (no IEND, SOS or EOI), it is too short to be a picture, there
is not enough memory, or it did not decode. The log names the file. The Image row still steps
and the chart still works. A stop during the read is no error (logged `gray viewer: <name>: read stopped`, the Status
row says "Stopped"). The session cap's clean drops a hold in progress and paints the entry again (a file is decoded
again), before the slot clock and the speed window restart, so a repaint's seconds are neither skipped periods nor
part of a slot's length.

The log, besides the runner's lines (speed, pause and resume, the stack line, the cover):

- `gray viewer: N pictures in /.crosshatch/display-lab/images` when the screen opens;
- `gray viewer: run, <name>, dither plain|ordered`;
- `gray viewer: <name>, dither ..., N pixels to drive` for the chart, or for a file `gray viewer: <name> WxH at X,Y
  WxH[ (jpeg scale 1/N[, progressive])], read and painted in N ms, dither ..., N pixels to drive` (the scale for a
  JPEG only);
- `gray viewer: <name> painted after N slots, pixels per level [0:N 2:N 4:N 6:N]; marker 0|6`;
- `gray hold: marker settled after N slots; holding, Next paints the next picture`;
- `gray viewer: next, <name>`, and `gray viewer: still held; Next paints the next picture` (above);
- `gray viewer: session-cap clean, the marker dropped; painting <name> again`;
- `gray viewer: <name>: cannot open|unsupported format|file too short|file has no end|out of memory|decode failed`
  (after the reader's own error line, such as `PNG truncated (a chunk or segment runs past the file's N bytes)`, `JPEG
  has no end (no SOS marker, or no EOI after it)`, `JPEG malformed (SOI, a segment length, SOF, DQT, DHT (class, index,
  codes, over-subscribed, values), SOS (components, tables, spectral range, approximation), an Exif IFD, or a table or
  SOS past the 2 KiB parse buffer, or too many buffer moves)` or `PNG read failed during the layout walk`), or `gray
  viewer: <name>: read stopped`;
- `PNG|JPEG: the N bytes after its end (IEND|EOI at N) are not read: <path>` when a file goes on past its end;
- `gray viewer: name of 64 bytes or more, skipped: <name>` when the screen opens;
- `gray viewer stopped: <outcome>`.

Memory, beside the runner's working set (as the transition test's):

- two logical rows of scratch and the file list (32 names of 64 B), both in internal RAM;
- while a file decodes, the decoder (PNGdec about 59 KB with the firmware's 16 KB row buffer, JPEGDEC about 20 KB,
  heap) and its row buffers: one output row; for a PNG one gray source row, and two more when the fit enlarges it
  vertically (the row above for the blend); for a JPEG two strip rows of the scaled width when the fit enlarges it
  vertically (the previous strip's last row and the one being built), and a 16-byte column. The free-heap floor before each is the
  decoder's own size plus 16 KB, as the converters' rule, but with the real size (the PNG converter's 44 KiB figure
  predates the firmware's larger PNG buffer);
- a stop (Back, Home, sleep) during a decode normally waits for one output row's decode and SD read (the reader
  checks it before every row, and the decode's reads give nothing once it is asked). The stops wait at most 20 s for
  the task in all, from the first one asked (Home's stop and the screen's exit after it share the 20 s), so no exit
  hangs (the SDK's own BUSY wait can run to 30 s, so a scan stuck there outlasts it too):
  - Back (the rows view's stop) then logs `<screen>: the sweep task did not end within 20000 ms of the stop; left
    running, not deleted` once (deleting it could leave the SD card's mutex held) and keeps the task, which can scan
    nothing more; the Status row (the transition test's Run row) says "Still stopping: the last run has not ended
    yet" and Run shows the same instead of starting, until the screen joins the task once it ends; only then does the
    row show the run's end (a cover seen meanwhile reads as one) and the transition test's Speed row the run's last
    speed. Auto-sleep is not held off by a stopped run.
  - Leaving the screen and sleep, which free the screen next, detach the task instead, at once when a stop already
    gave up: `<screen>: the sweep task did not end within 20000 ms of the stop; detached. It frees its runner's
    buffers when it ends; if it never does they are leaked by design until a restart, and no run starts before one`.
    The inert client is set and the task counted in the registry before the one exchange that hands the runner
    (the model, the follow-up masks, every buffer the task can touch) to the task; nothing of the runner is touched
    after it. Every client hook is handed the runner, never the screen's pointer to it (null once detached), the
    inert client answers each one, and the task checks the sticky stop after any hook that can take long and before
    any slot, so it touches neither the lab's masks nor the screen; it frees the runner when it returns (`<screen>: the
    detached sweep task ended; its buffers freed`) and deletes itself. While one is detached both screens show
    "Restart the device to run the lab again: an earlier run has not ended" on opening and on Run, and leave their
    own working set unallocated (the detached runner still holds its model, about 1.2 MB of PSRAM); the notice clears
    to "Not run" once the task ends, and Run then allocates. A task that never returns keeps its buffers and its
    registry entry for good, and only a restart clears them. A detached task stuck inside an SD read holds the
    Storage mutex: whatever reads the card next waits behind it for as long as that read lasts, so the screen Home
    goes to, reopening the lab (the gray viewer lists its folder on opening) or the Image row can freeze, and sleep's
    own steps wait without a bound. A timed Storage lock on the sleep path would bound that; it is a follow-up outside
    this story;
- the sweep task's stack, 12 KiB, provisional: the decode chain and the scan chain (the transition test's 8 KiB
  provision) never nest, so the need is the larger of the two; the run's `gray sweep task ended` line measures it,
  and the story's Verification asks for the marks of a PNG, a baseline and a progressive JPEG.

### Tuning

Display lab → Tuning lists every value of the session's preset, one row each: Dose 0>2 through Dose 6>4 (1..24), Hold
0>2 through Hold 6>4 (on or off), Retarget to dark and Retarget to light (50..150 %, steps of 25: the budget of a pixel
turned mid-transition toward a darker, or a lighter, shade than its level), Frame rate (the backend's rate labels),
Swap polarity, Reset to defaults (the panel's starting values, the UltraChip preset or InkBoy's, into the session at
once, logged as `DLAB tune reset: ... defaults`; the only way back to them on the device, and Save then writes them),
and Save. A **tap** on a row steps it in place (+1, wrapping; a hold or the swap toggles). **Confirm** on a
row enters edit mode for it ("editing" on its line): Next and Previous (NavNext and NavPrevious, as above) then change
it by one step (+1 / -1, clamped; a hold or the swap on / off), and Confirm or Back leaves edit mode. Outside edit
mode they move the selection and Back closes the screen. Changes apply at once to the session (the next transition-test run uses them) and are logged as
`DLAB tune <row>=<value>`; nothing is written until **Save** (on this screen or the lab menu), so leaving the lab
without saving discards them.

### The preset file

`/.crosshatch/display-lab.json` on the SD card, read when the lab opens and written by Save. The defaults are the
controller's: the UltraChip preset on a UC8179 or UC8279, InkBoy's on the SSD1677 (chosen in the lab's `onEnter` from
the probed controller, before the file is read). Missing: those defaults (logged `no preset at ...: UltraChip
defaults` or `...: InkBoy's defaults`). Unreadable or malformed: the same defaults, logged with `LOG_ERR`. A failed
save keeps the values in memory and says "Save failed, values kept". Its text, InkBoy's defaults shown:

```json
{"version":1,"base":"inkboy","doses":[2,4,7,4,1,6,4,1,5,7,3,2],"hold":[true,true,false,false,true,false,false,true,false,false,false,true],"retarget_dark_percent":100,"retarget_light_percent":100,"rate":0,"swap_polarity":false,"history":0,"return_delta":[-1,0,0,0,0,0],"reverse_percent":[150,0,0,0,0,0],"weak_white_to_light":false}
```

| Key | Meaning | Range |
| --- | --- | --- |
| `version` | the file format; required | 1 |
| `base` | the defaults the saved values carry: `inkboy` (SSD1677) or `ultrachip` (UC8179, UC8279). Informational and optional: files saved before dlab-c3 have none, and a value naming neither is logged and ignored. When it names other defaults than this panel's, the lab logs an error naming both (`preset ... was saved over InkBoy's defaults, this panel's are UltraChip: loaded anyway`), loads the file all the same, and keeps the file's base, so Save writes it back and the error stays until Tuning's Reset to defaults; Save's log names the base it wrote. On an UltraChip panel a file without the key logs an error that it predates the key and its doses override the UltraChip defaults | `"inkboy"` / `"ultrachip"` |
| `doses` | the 12 doses in the table order 0>2, 0>4, 0>6, 2>0, 2>4, 2>6, 4>0, 4>2, 4>6, 6>0, 6>2, 6>4 | 1..24 each |
| `hold` | the 12 hold bits in the same order (on: the follow-up scan holds that transition) | `true` / `false` |
| `retarget_dark_percent` | the budget of a pixel retargeted mid-transition toward a darker shade, percent of its base dose | 50..150 |
| `retarget_light_percent` | the same toward a lighter shade | 50..150 |
| `retarget_percent` | read only, from files written before the split (dlab-c1, dlab-c2): the value of each direction the file does not set itself, whatever the keys' order; each direction it filled is logged, as `tune retarget_percent read as retarget to dark=N%` and `... to light=N%`; Save writes the two keys above instead | 50..150 |
| `rate` | the frame-rate index into the backend's list; past its end, rate 0 (logged) | 0..255 |
| `swap_polarity` | swap the backend's two drive rows | `true` / `false` |
| `history` | 1: completed returns, 2: reversals, 3: both (allocates 3 B a pixel of PSRAM) | 0..3 |
| `return_delta` | per route LKL, DKD, DWD, LWL, DLD, LDL: doses added to a completed return (0 = none) | -11..11 |
| `reverse_percent` | per route, the same order: a reversal's budget percent (0 = the retarget percent of the new target's direction, as for any other retarget: toward dark when it lies below the pixel's level, toward light when above) | 0..250 |
| `weak_white_to_light` | 6>4 pixels settled on white darken through the backend's weak row, when it has one | `true` / `false` |

The format stays `version` 1: an older firmware (before dlab-c3) reading a newer file skips the `base` and the two
new retarget keys and keeps its retarget default, but a file holding any dose above 12 is out of its range, so it
rejects the whole file as malformed and runs on its defaults. Absent keys keep the defaults (the controller's); unknown keys are skipped; a key of the wrong type or out of range, an array of the
wrong length, a parse error, or a missing or different `version` makes the whole file malformed. The codec is a small
standard-library JSON subset in `lib/GameDisplayLab/GrayPreset.cpp`, not ArduinoJson: the lab's library may use the
C++ standard library only (the spine's layer table, `scripts/check_layers.py`), and the host suite tests it.

Memory, PSRAM only, while the transition test is open: the model (3 B a pixel, 1,152,000 B at 800x480: the level,
the doses left, which need a byte since the cap rose to 24, and the target and source shades) and the
follow-up masks (2 x 48,000 B), plus the history (1,152,000 B) when `history` is on and the weak bitmap (48,000 B)
when weak mode is on and the backend has a weak row; the lab's own masks and the backend's planes stay as in Part A.
If any does not fit the screen shows "Not enough memory" and runs nothing. While a run is up the sweep task's stack
(8 KiB; the gray viewer's 12 KiB) is in internal RAM; when it cannot be created the Run row says "Stopped: not enough
memory" and nothing is sent to the panel.

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
  controller; only SSD1677 has a backend so far.
- `src/activities/games/display_lab/Ssd1677ScanBackend.{h,cpp}`: the SSD1677 facade calls (its two PSRAM planes,
  `copyGrayscaleBuffers`, the timed `displayGrayBuffer(false, lut, true)`) and the cleaning FULL refresh.
- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}`: the menu, the gate, the session (the backend and the
  two PSRAM masks), the controller-neutral `scan()` wrapper (the inversion and rate checks, the session cap, the
  timeout rule), and the exit refresh.
- `src/activities/games/display_lab/FramePeriodActivity.{h,cpp}` and `PolarityActivity.{h,cpp}`: the two experiments.
- `lib/GameDisplayLab/GrayPreset.{h,cpp}`: the gray engine's tunable values (`Shade`, the transition order, InkBoy's
  and the UltraChip defaults, `PresetBase`, the tuning steps, the two retarget percents) and the preset file's codec
  (with the older `retarget_percent` key); pinned by `test/game_display_lab/test_gray_preset.cpp`.
- `lib/GameDisplayLab/GrayModel.{h,cpp}`: the per-pixel model (level, doses left, target, source; optional history and
  weak bitmap): the dose lookup, retargets, the Ladder's pulses (`driveRect`: doses set directly), the mask build, and
  the commit; `GraySlot.h`'s `runGraySlot` runs one slot (drive scan, follow-up scan, commit) through a caller's scan
  function; both pinned by `test_gray_model.cpp`.
- `lib/GameDisplayLab/SlotScheduler.h`: the slot period (D5), skipped slots, and the speed window; pinned by
  `test_slot_scheduler.cpp`.
- `lib/GameDisplayLab/TaskHandoff.h`: the sweep task's end against its owner's give-up (`finishTask`, `detachTask`:
  one exchange, the inert client and the registry first, undone when the task's end wins; `InertAnswers`, what the
  inert client answers, which leave the run's end to the stop) and the owner's stop
  bookkeeping (`StopTracker`: the shared 20 s deadline, the give-up logged once, a cover seen while stopping);
  pinned, the races included, by `test_task_handoff.cpp`.
- `lib/GameDisplayLab/SweepControl.h`: the sweep runner's request word between a screen's loop and its sweep task
  (Stop, Run, Replay, Clean, InPanel, and since dlab-g3 Held and Replaying: the hold, the screens' presses (`press`:
  a pause or resume, or the gray viewer's next picture, decided in one compare-exchange), the task ending the hold
  when it serves a resume), the loop's heartbeat for the covered check, and whether the run keeps the device awake
  (`keepsAwake`: Run, or a hold within its ten minutes, never once a stop is asked); pinned by
  `test_sweep_control.cpp`.
- `lib/GameDisplayLab/PanelGeometry.{h,cpp}`: the logical screen over the panel model, shared by the transition test
  and the gray viewer: the four orientations (`PanelTurn`), a logical rectangle mapped to the panel as
  `GfxRenderer::rotateCoordinates` maps a pixel (`toPanel`) and retargeted (`retargetLogicalRect`), and the hold
  marker's pause symbol (`pauseBars`) and its settle check (`barsSettled`); pinned by `test_transition_scene.cpp` and
  `test_gray_picture.cpp`.
- `lib/GameDisplayLab/TransitionScene.{h,cpp}`: the bands and rectangles over the logical screen, mapped to the panel
  through `PanelGeometry`, and the modes (`SweepMode`): Fast's and Slow's sweeps (both
  waiting for the bands' paint), Slow's crossing from the slot period and the preset's longest dose run
  (`slowSweepMs`), Static's still rectangles and its settle-and-flip sequence, and the Ladder (its column layout and
  rectangles, the base and the two stages driven through `GrayModel::driveRect`, `slotPreset`'s held rhythm for one
  pulse a slot); `slotDone` makes the per-slot decision for all of them (reapply after a flip, start Slow's crossing
  once measured and Fast's once painted, the Ladder's next stage) and for the holds for photos (each mode's photo
  spots, the hold marker's box, its pause-symbol bars and shade, its slots out and back, the resume, the session cap's
  drop); the shades' names (`shadeName`, D14's level numbers); pinned by `test_transition_scene.cpp`.
- `src/activities/games/display_lab/GrayPresetFile.{h,cpp}`: the preset file's path, its load and save through
  `Storage`.
- `src/activities/games/display_lab/GrayRunner.{h,cpp}`: the sweep runner the transition test and the gray viewer share
  (extracted from the transition test by dlab-g3): the PSRAM working set, the sweep task (its creation, its joined stop
  from the loop and its stop under the render lock), the request word, the cover check, the slot loop (the session
  cap's clean, the scan with its refusal checks, `runGraySlot`, the scheduler), the stats, the hold for a photo; each
  screen is its `Client` (what it paints and decides per slot). Device-only; the simulator compiles the lab out.
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` and `TuningActivity.{h,cpp}`: the two Part B
  screens; `DisplayLabActivity` owns the session's preset and the Save preset row.
- `lib/GameDisplayLab/GrayPicture.{h,cpp}`: the gray viewer's picture: the quantiser (`quantise`, Plain and Ordered at
  the engine's level count), the test chart (`testChartGray`), the run splitting (`forEachRun`), the painted rows
  (`paintGrayRow`, `paintChart`), the per-level counts, and the hold (paint settled, marker out to the farther
  extreme, held); pinned by `test_gray_picture.cpp`.
- `lib/GameDisplayLab/ImageRows.{h,cpp}`: the gray viewer's decode arithmetic: the files it lists (`imageKindOf`,
  `nameLess`, `ImageList`: the first 32 by name), the converters' size limits and PNG checks, a PNG row to gray as the
  converter converts it (`pngRowToGray`), JPEGDEC's coarse scale and option (`jpegScaleFor`), the fit (`fitToScreen`,
  `FitAxis` and its taps), the layout walks against a file's size (`walkPngChunks`, `walkJpegSegments`, `FileLayout`),
  `WalkExtent`, the answers to a decoder's reads (`readAllowance`, `decoderReadCap`, `jpegHeaderReadCap`, `fillPngRead`,
  `answerJpegHeaderRead`, `answerJpegDecodeRead`, `jpegFilteredLength`, `jpegDropRestartMarkers`,
  `jpegPositionAfterRead`), the output pixels a decoded row or block makes (`rowsReadyAt`, `outputsEndingIn`), the seams
  between blocks (`SeamKeeper`) and the sampling across them (`SourceBlock`, `sampleBlock`: bilinear enlarging, nearest
  shrinking); pinned by `test_image_rows.cpp`, and the reads against the real PNGdec and JPEGDEC (every cut and a few
  appends of the sample pictures, among them a 3 KB PNG whose IDAT crosses PNGdec's buffer, an indexed PNG with a text
  chunk before its 768-byte palette, 128 x 128 noise JPEGs whose scans cross JPEGDEC's, colour noise at quality 100
  (4:2:0 and 4:4:4), a 512 x 512 progressive JPEG whose DC scan refills inside decode(), a progressive JPEG with restart
  markers in a first scan past 2 KiB decoded from its walked extent as from the whole file, restart-marker line art
  decoded pixel-identical with and without the tame, scans with 15, 16 and 40,000 restart markers in a row decoded as
  without them, a crafted colour JPEG of the longest codes, an erased sector, and a 66 KB camera-shaped JPEG whose APP1
  crosses JPEGDEC's header buffer; a stop at every read; every synthetic header the walk's tests pass, which
  `image_fixtures.h` shares between both test files; single-byte changes to a real header; each decode bounded, under
  AddressSanitizer and UndefinedBehaviorSanitizer, with guards inside the decoders' objects: 64 poisoned bytes after
  JPEGDEC's file buffer (a test-only field), its bit offset within the data in its buffer and its Huffman tables
  untouched by the decode, PNGdec's row buffer past its palette untouched) by `test_image_decoders.cpp`, through the
  reader's own callback logic. JPEGDEC runs there as the device builds it (a 32-bit bit reader, no SSE:
  `jpegdec_as_device.h`). That test is built where PlatformIO's library folder has the decoders' sources, and by the
  fork CI's Display lab decoders job, which fetches them at the pinned versions; both decoders get the firmware's
  patches in a copy in the build tree, and the PNG buffer size is `platformio.ini`'s, both by
  `test/game_display_lab/CMakeLists.txt`.
- `lib/GameDisplayLab/DecoderReads.h`: the gray viewer's read callbacks as pure functions over a small file interface
  (`answerPngRead`, `answerSeek`, `answerJpegRead`, `answerJpegSeek`: which answer a read gets, the caps' counts, the
  header latch, a refused seek, the position, restart markers dropped and the span topped up; `DecoderReads`, one
  image's state) and the task watchdog's decisions (`TaskWatchdog`: join, the gaps around file calls, progress,
  rejoin; `WatchdogQuiet`), which `GrayImageReader.cpp` runs over `HalFile` and ESP-IDF's watchdog and the tests
  over fakes; pinned by `test_image_rows.cpp`.
- `scripts/lab_decoder_patches/` and `scripts/patch_lab_decoders.py`: the lab's decoder patches (above), applied at
  build time in the x4pro env only.
- `src/activities/games/display_lab/GrayImageReader.{h,cpp}`: the gray viewer's reader, PNGdec and JPEGDEC driven
  directly into 8-bit gray rows (the one file the layer check lets include them); and `GrayViewerActivity.{h,cpp}`,
  the screen.
- `lib/GameDisplayLab/StockBench.{h,cpp}`: the stock-path bench's pure parts: the checker frame (`fillChecker`), the
  gray ramp in the overlay encoding (`rampShadeAt`, `fillGrayRamp`), the timing summary (`summarizeTimings`) and the
  fell-back heuristic (`transitionFellBack`); pinned by `test/game_display_lab/test_stock_bench.cpp`.
- `src/activities/games/display_lab/StockBenchGate.{h,cpp}`: `isUltraChip` (which also picks the UltraChip preset),
  `usesStockBench` (an UltraChip without a scan backend) and the launcher row's subtitle (`labRowSubtitle`).
- `src/activities/games/display_lab/TransitionTimingActivity.{h,cpp}` and `GrayRampActivity.{h,cpp}`: the stock
  bench's two screens; the lab menu (`DisplayLabActivity`, `buildStockRows`, `activateStock`) opens them in place of
  the scan screens.
- `lib/hal/HalDisplay.h`: the one upstream touch, `EInkDisplay& eink()` under `FREEINK_CAP_GAMES` (ledger row 13).

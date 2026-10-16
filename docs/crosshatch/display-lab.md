# Display lab: the panel scan bench (spike, Part A)

The display lab is a bench, not a product mode. It lives in `src/activities/games/display_lab/` and
`lib/GameDisplayLab/`, compiles only under `FREEINK_CAP_DISPLAY_LAB`, which the `x4pro` development env alone sets, so
no release build (`x4pro-gh_release*`) ever carries it (the simulator's HAL stand-in has no `HalDisplay::eink()`, so the
row is absent there too), and shows its row only on an X4 Pro whose panel probe found a controller with a scan
backend (SSD1677 and UC8279 so far).
It answers the two numbers only a panel can give before Part B (the per-pixel grayscale engine, deferred in
`_bmad-output/implementation-artifacts/deferred-work.md`) can be built:

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
  way round, Part B swaps the codes (`GameDisplayLab::POLARITY_ROW1/2`).
- On a UC8279 the slope reads the same way, but `busy_us` there is the DRF activation and BUSY wait only, so the `tp=1`
  intercept is the activation overhead alone (with the wait's 1 ms start tick), with no table upload in it. For
  polarity, Row 1 (KW at VSL) should lighten and Row 2 (WK at VSH) darken; if Row 1 darkens instead, the levels or the
  table roles are the other way round on this panel, and Part B swaps the polarity (`ScanRequest::swapPolarity`). The
  "Dropped" status cannot occur there (see Busy time and Refusals in its section).
- On an SSD1677, a "Dropped" status (log line `dropped: scan N came back in M us, under the 500 us floor`) means the
  facade refused the call and the panel was not driven, usually because the display is inverted (the control-center
  panel can invert it while the lab is open; the lab also refuses outright, logged as `scan refused: display inverted`,
  when it sees the inversion first). Turn inversion off and run again; a dropped time is not a measurement.

## Where Part B picks up

Part B (deferred-work.md, "Display lab Part B") takes the measured frame period as the length of one dose and the
polarity answer as the row-to-color mapping, and builds on the scan seam (the display-lab epic's D1): a `ScanRequest`
of two full-screen drive masks (`GameDisplayLab::MaskPlanes`: black, white, a pixel in neither holds), a frame count, a
rate index and a polarity swap, sent through `DisplayLabActivity::scan`, which wraps the controller's `ScanBackend`
with the session cap, the timeout and the dropped-call floor. A new controller is one backend class plus its case in
the one switch of `ScanBackends.cpp`, and the screens and the safety rules stay as they are: the backend names its
own drive rows (`ScanBackend::rowLabel`, the epic's Amendment D1a; on the SSD1677 "LUT1 VSL" and "LUT2 VSH1"), which
the polarity screen shows and logs as `vs=`, and the frame-period screen runs the backend's own rate list. The
frame-period TPs and the polarity TP are frame counts every backend takes. On top of it come the
per-pixel level and dose model, the transition-test screen with sweeping rectangles, the live tuning list, and the SD
preset. Nothing in Part A is a product feature; the launcher row and the lab can be removed once Part B has its own
entry.

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
- `lib/hal/HalDisplay.h`: the one upstream touch, `EInkDisplay& eink()` under `FREEINK_CAP_GAMES` (ledger row 13).

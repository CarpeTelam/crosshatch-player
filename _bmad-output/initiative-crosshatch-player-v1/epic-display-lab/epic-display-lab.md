# Epic: display lab for all three X4 Pro panel batches, and the grayscale engine (Part B)

Orchestrated epic (docs/crosshatch/orchestrated-epics.md). Owner: huey. Orchestrator: this session. Base commit:
`0cb1fc8e` (the SSD1677 display lab spike, Part A, on branch `claude/wizardly-carson-9ke6px`). Epic branch: that
branch; lanes are worktrees `dlab-lane-*` merged back into it. Stories are intent files in this folder, not
`tickets.toml` entries: a build agent is dispatched with `/bmad-build-auto <story file>` and its plan goes to
`_bmad-output/implementation-artifacts/plan-<ref>-<slug>.md`.

## Why

The research run (`_bmad-output/planning-artifacts/research/technical-fast-e-paper-display-driving-for-x4-pro-2026-10-16/research.md`)
recommends InkBoy's per-pixel grayscale engine for a pen-and-paper game system, with stable four-shade grayscale and
flash-free updates as the priority and frame rate as a bonus. Part A built the scan bench for the SSD1677 batch
(`docs/crosshatch/display-lab.md`). The owner's unit is a UC8179, so the bench must cover the two UltraChip batches
too, and the engine (Part B, deferred in `_bmad-output/implementation-artifacts/deferred-work.md`) is built now with
every panel number a live-tunable parameter, so the device itself is the calibration rig.

## Requirements

- R1. One scan primitive interface, `ScanBackend`, chosen at runtime by the detected controller, with three
  implementations: SSD1677 (the existing path), UC8179, UC8279 (the X4 Pro UltraChip batch, `Uc8279X4Driver`).
  A scan takes two full-screen 1-bpp masks (drive-to-black, drive-to-white; a pixel in neither holds), a frame count,
  a frame-rate index, and returns the measured busy time; every backend keeps the panel's stock voltages and never
  sends an OTP-programming command.
- R2. The frame-period and polarity screens run unchanged on every backend; the launcher's "Display lab" row shows on
  any X4 Pro whose controller has a backend.
- R3. The grayscale engine: InkBoy's per-pixel model (level 0..192, remaining doses, target, source), a 12-entry dose
  table with a hold mask, interpolated retargets, optional history routes and a weak mode where the backend supports
  a third drive row, driven by a slot scheduler over `ScanBackend`.
- R4. The transition-test screen (four shade bands, three sweeping rectangles per band, 5 s sweep, pause, replay,
  clean) and a tuning screen that changes one value at a time, with the preset saved to and loaded from
  `/.crosshatch/display-lab.json`.
- R5. Safety on every backend: entry warning, 15,000 ms scan timeout, 2,400-scan session cap with a forced full
  refresh, full refresh on every exit path, dev-env build only (`FREEINK_CAP_DISPLAY_LAB`).
- R6. Host tests for every pure part (mask planes, each backend's table builder and plane encoding, the gray model,
  the scheduler's slot arithmetic, the preset codec); the C3 `default` and the `sticky` env still build with the lab
  compiled out.

## Boundaries

- Fork-only paths only (`src/activities/games/display_lab/`, `lib/GameDisplayLab/`, `test/game_display_lab/`,
  `docs/crosshatch/`), plus the upstream files the ledger already allows for this work: row 1 (`platformio.ini`,
  the `FREEINK_CAP_DISPLAY_LAB` flag), row 2 (`STR_GAMES_*` keys), row 13 (`HalDisplay::eink()`). No new ledger row
  without an owner decision; no edit to `freeink-sdk/`, `.agents/`, `CrossPointSettings.h`, `SettingsList.h`.
- Never command 0x30 (SSD1677 OTP) or any UltraChip OTP/MTP programming command; never change the voltage registers
  from the panel's values; never a background FreeRTOS task; no per-frame heap allocation; locals under 256 bytes;
  PSRAM via `HalMemory::allocatePsram`, allocated in `onEnter` and freed in `onExit`.
- The simulator has no e-ink physics and compiles the lab out; it only screenshots the launcher and menus.
- AGENTS.md verification order and locks apply to every lane; the shared build cache is `{main}/.cache`.

## Lanes

- Lane 0 (serial, first): `dlab-0` the backend seam. Everything else branches from its merge.
- Lane C: `dlab-c1` the grayscale engine and its two screens (after lane 0).
- Lane C, second story: `dlab-c2` the transition test's sweep on its own FreeRTOS task (after dlab-c1 and the owner's
  first hardware run; D11).
- Lane A, first story: `dlab-u1` the UltraChip stock-path bench for both UC8179 and UC8279 (owner option 3, after
  lane 0): times the stock non-flashing previous-to-current transition and exercises the OEM four-level grayscale
  through the facade's public paths; needs no SDK change.
- SDK seam (owner option 1): `sdk-seam` in the fork `CarpeTelam/freeink-sdk`, branch `display-lab-seam`, kept in the
  CarpeTelam fork until the owner confirms it on hardware (no upstream pull request before that, owner decision
  2026-10-16); the experimental superproject branch points the submodule at it.
- Lane A, second story: `dlab-a2` the UC8179 lab backend over the seam; Lane B: `dlab-b2` the UC8279 lab backend over
  the seam. Both run in parallel on the experimental branch after the seam lands; they touch disjoint files (each
  backend is its own pair of files and its own test file).

## Decisions (owner, dated)

- 2026-10-16: HAL seam option (a): `HalDisplay::eink()` behind `FREEINK_CAP_GAMES`, ledger row 13 (spike plan).
- 2026-10-16: the bench is dev-env only: `FREEINK_CAP_DISPLAY_LAB=1` in the `x4pro` env alone (spike review).
- 2026-10-16: scan timeout 15,000 ms (spike review).
- 2026-10-16: build UltraChip benches for both UC8179 and UC8279, and Part B now, as parallel lanes (this epic).
- 2026-10-16: the owner forked `Free-Ink/freeink-sdk` to `CarpeTelam/freeink-sdk`; the UltraChip one-frame benches
  are built over a lab API added there (option 1) on an experimental branch that cannot merge into `develop` until
  upstream accepts the seam, and the stock-path UltraChip bench (option 3) is built now on the epic branch.
- 2026-10-16: the experimental superproject branch is `claude/display-experimental-sdk-do-not-merge`, branched from the
  epic branch; it alone moves the submodule pointer to the fork's `display-lab-seam` and edits `.gitmodules`; it is
  never merged into `develop` or the epic branch; the seam-backed stories (`dlab-a2`, `dlab-b2`) land there.
- 2026-10-16: no upstream pull request to Free-Ink until the owner has confirmed the seam on hardware; everything
  stays in the CarpeTelam repositories (`CarpeTelam/freeink-sdk` branch `display-lab-seam`, the experimental
  superproject branch) until then.
- 2026-10-16: Part B ships with InkBoy's defaults (dose table, hold mask 0xc667, retarget 100 percent, routes) and
  every number editable on the device; no number is claimed calibrated until the owner measures it.

## Orchestrator decisions (design, dated 2026-10-16)

- D1. `ScanBackend` lives device-side in `src/activities/games/display_lab/ScanBackend.h` as an abstract class:
  `bool available()`, `const char* name()`, `uint8_t rateCount()`, `const char* rateLabel(uint8_t)`,
  `Scan scan(const ScanRequest&, uint32_t& busyUs)`, `void clean()` (a FULL refresh through the renderer), plus
  `bool supportsWeakRow()`. `ScanRequest` is `{const uint8_t* blackMask; const uint8_t* whiteMask; uint8_t frames;
  uint8_t rate; bool swapPolarity;}` with masks as full-screen 1-bpp bitmaps in the framebuffer's byte order
  (set bit = drive that way). The pure parts of each backend (table bytes, plane encoding from the two masks) live in
  `lib/GameDisplayLab/` and are host-tested; only the facade calls live in `src/`.
- D1a (2026-10-16, after dlab-0's review): `ScanBackend` also exposes `const char* rowLabel(uint8_t row, bool swapped) const` (the `swapped` argument added on the combined tree after dlab-0, 2026-10-16) (row 1
  and row 2, short text for the polarity screen and its `vs=` log token, e.g. "LUT1 VSL" on the SSD1677, "KW table"
  on UltraChip). The polarity screen and its log line use it; no backend-specific strings outside the backends.
- D6 (2026-10-16): the flash-budget gate measures shipped firmware. The display lab is dev-env only, so
  `scripts/check_flash_budget.py` builds both of its images with `FREEINK_CAP_DISPLAY_LAB` unset (an unflag on the
  "on" build as well as the "off" build), its sidecar test pins that, and the epic's base measurement is re-taken
  with the gate so changed (AGENTS.md: a change to how a gate measures means re-measuring the base). Until then
  the lab's cost is tracked here: dlab-0 +2,016 B (7,696 B left under the old counting).
- D2. SSD1677 encoding: black mask to the RED plane, white mask to the BW plane (a set bit selects LUT2 or LUT1);
  hold = both clear. UltraChip encoding (UC8179, UC8279): the "old" plane (DTM1) and "new" plane (DTM2) are derived
  from the masks: hold = old == new (white), drive-to-white = old black, new white (the KW table), drive-to-black =
  old white, new black (the WK table); WW and KK tables are all zero. Which of the two UltraChip tables darkens is
  what the polarity screen answers, as on the SSD1677.
- D3. One-frame tables: SSD1677 as Part A (`Ssd1677ScanLut`); UltraChip as five 42-byte register tables with one
  group of one phase (`T1 = frames`, repeat 1), level codes per `swapPolarity`, uploaded through whatever path the
  driver investigation names in the story file; the frame rate is the controller's PLL register, whose documented
  codes are the rate list.
- D4. The lab's own screens never draw during a sweep: the panel shows what the scans drive; the next activation
  repaints with a FULL refresh (Part A's polarity rule).
- D5. The gray engine's slot period is `max(measured scan time + 5 ms, 100 ms)` from the backend's last busy time;
  a board game needs a few scans per second, so whole-screen correctness beats windowed speed.
- D7 (2026-10-16, dlab-b2 halt G1): a UC8279 phase byte holds at most 63 frames (6-bit count). A lab table spreads
  `frames` over the single group's four phases at the same level (100 = 63 + 37; 252 is the ceiling) so the drive is
  one continuous run of `frames` frames; a request above 252 is `Scan::Refused`. The UC8179's counts are 8-bit, so
  dlab-a2 keeps D3's single phase.
- D8 (2026-10-16, dlab-b2 halt G2, binds dlab-a2 too): the SDK seam forbids any other display call between the
  waveform upload and `labEnd()`, and the lab's screens repaint through the renderer between runs, which would leave
  a cached upload stale. An UltraChip backend's `scan()` is a self-contained session: upload the tables, set the
  PLL, `labScan`, then `labEnd` every call (the upload's ~260 SPI bytes sit outside the timed DRF window). The
  story's "upload once, then not again until a change" is withdrawn; D1 gains no end-of-run hook.
- D9 (2026-10-16, dlab-b2 halt G3): D2's "WW and KK tables are all zero" meant their level, not their length. VCOM,
  WW and KK carry the same frame counts as KW and WK (group 0, T1 = `frames`, split per D7 on the UC8279) at the
  GND level, matching every stock UltraChip table in the driver, so the activation length follows TP and the
  all-hold row of the matrix drives nothing.
- D10 (2026-10-16, before dlab-a2): dlab-u1's gate gives the stock bench only to an UltraChip controller without a
  scan backend, and a backend wins. So that the owner's UC8179 unit keeps the stock transition and OEM ramp to compare
  against the lab, an UltraChip controller with a scan backend gets the stock bench's two rows (Transition timing,
  Gray ramp) appended to the scan menu before Exit; an SSD1677 does not. The stock-bench-only menu stays for an
  UltraChip controller with no backend.
- D11 (2026-10-16, after the owner's first UC8179 run): the transition test's controls were laggy to non-responsive
  because its slots run inline in the main loop, which is the only place input is polled, and an UltraChip scan
  blocks for hundreds of milliseconds. The sweep moves to a dedicated FreeRTOS task (story dlab-c2), as GameVM runs
  scripts; the main loop keeps polling input and forwards the controls to the task between scans; the panel stays the
  task's alone while the view is up (D4), every scan still goes through DisplayLabActivity::scan under RenderLock,
  and the task is joined before onExit frees anything. Two smaller fixes landed first as an orchestrator `fix:`
  commit (8598d3ad): the follow-up scan is skipped when its masks are empty (MaskPlanes::any), and the entry warning
  was shortened to fit the dialog's three headline lines.

## Story results

- dlab-0 (lane 0): built, commit `40a71c4d` on `dlab-lane-0`; 33 review findings, 8 patched, 3 deferred; 2,104 host
  tests; x4pro, default, sticky build; pio check clean; flash +2,016 B, static RAM unchanged. Follow-up pass `03c0a06d`
  (D1a, 8 lows patched, 2 more deferrals, 2,106 tests, flash +2,160 B in total); merged as `9ee36396` plus the
  orchestrator's `rowLabel(row, swapped)` signature commit; 5 deferrals carried to deferred-work.md.
- dlab-b2 (lane B, experimental branch): halted once on three intent gaps (plan `536a61b4`; D7-D9 decided them),
  then built, commit `4bc3054e` on `dlab-lane-b`; follow-up pass `eef111b7` (29 findings, 0 high, 5 medium; the doc's
  "about 150 s" stall claim corrected: the SDK's UC8279 BUSY wait has no bound); 2,129 host tests; x4pro, default,
  sticky build; pio check -e x4pro clean; flash +1,472 B (gate 6,016 B to spare before D6), static RAM unchanged.
  Merged into `claude/display-experimental-sdk-do-not-merge` as `7ea790b2`; 2 deferrals carried to deferred-work.md.
- dlab-c1 (lane C): built, D6 as `1ca33864` and the engine as `fe2f6cef` on `dlab-lane-c`; follow-up pass `ef6e15a2`
  (29 findings, 0 high, 5 medium; 1 medium patched: a light-panel swipe could cover the running transition test and
  draw over the drive). 2,159 host tests on the merged tree; x4pro, default, sticky build; pio check clean on default
  and x4pro; the preset codec is std-only (the layer check forbids ArduinoJson there). Flash: with D6 the gate measures
  shipped firmware, base re-taken at `9b2317f3` + D6 as +258,192 B games delta (18,288 B to spare), the engine +0
  against it; the plain x4pro image with the lab on grows +18,576 B; static RAM +0. Merged as `7e430902`; no
  deferrals. Open for the hardware run: at D5's 100 ms slots a 12 px rectangle crossing in 5 s gives each pixel only
  1-2 doses, so it may never reach its shade; the sweep speed or slot period is an epic Decision after the owner's
  transition-test run, not a lane fix.
- dlab-u1 (lane A): built, commit `9f8a8f6d` on `dlab-lane-a` (33 findings, 11 patched); halted once on a full disk
  and a half-installed PlatformIO package (orchestrator fixed both); 2,128 host tests; x4pro, default, sticky build;
  pio check clean on default and x4pro; its own flash gate ran before D6 and was 416 B over (the lab's cost), which
  D6 resolves. Merged as `7e46ec7b` with the lab menu, strings, test list and doc combined with dlab-c1's; the merged
  tree's lab build failed on two `GameDisplayLab::Shade` enums (fixed on the epic branch, the stock bench takes the
  engine's). Merged tree: 2,181 host tests; x4pro lab build 6,249,086 B flash, 104,232 B RAM; pio check -e x4pro
  clean; flash gate with D6: games delta within budget, static RAM +784 B (240 B to spare). 2 deferrals carried.
- dlab-a2 (lane B, experimental branch): built, commit `9a6f22a7` on `dlab-lane-b` after one stalled hand-back (the
  implement subagent was still writing; resumed from `in-progress`); 4 lenses, 1 medium and 6 low patched, no
  follow-up pass; 2,225 host tests; x4pro, default, sticky build; pio check -e x4pro clean. PLL list {0x06, 0x05,
  0x07} (0x07 undocumented, brackets stock); rows "KW VDL" / "WK VDH"; D10 through a pure `labMenuFor` gate with a
  host test. Plain x4pro image +1,168 B over `b8f86fa7`, RAM +0; flash gate with D6 +0 against the 9b2317f3 + D6
  base (18,288 B to spare), static RAM 240 B to spare. Merged into the experimental branch; 1 deferral carried.
  Hardware questions for the owner: which table darkens, whether a repeat-1 group runs once or twice, whether the
  panel accepts PLL 0x07.

## Measurement (base)

- 2026-10-16, commit `0cb1fc8e`, `scripts/check_flash_budget.py build on/off` + `compare --limit-kib 270
  --ram-limit-bytes 1024`: x4pro games delta within budget with 9,712 B to spare; static RAM delta +784 B of 1,024
  (240 B to spare). Every delta in this epic subtracts this measurement. The lab counts toward the games delta
  because the games-off build also clears `FREEINK_CAP_GAMES`; Part B's engine must fit in the remaining 9.5 KiB or
  the epic needs an owner decision on the budget.

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
- Lane A, first story: `dlab-u1` the UltraChip stock-path bench for both UC8179 and UC8279 (owner option 3, after
  lane 0): times the stock non-flashing previous-to-current transition and exercises the OEM four-level grayscale
  through the facade's public paths; needs no SDK change.
- SDK seam (owner option 1): `sdk-seam` in the fork `CarpeTelam/freeink-sdk`, branch `display-lab-seam`, proposed
  upstream as a pull request; the experimental superproject branch points the submodule at it.
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

## Measurement (base)

- 2026-10-16, commit `0cb1fc8e`, `scripts/check_flash_budget.py build on/off` + `compare --limit-kib 270
  --ram-limit-bytes 1024`: x4pro games delta within budget with 9,712 B to spare; static RAM delta +784 B of 1,024
  (240 B to spare). Every delta in this epic subtracts this measurement. The lab counts toward the games delta
  because the games-off build also clears `FREEINK_CAP_GAMES`; Part B's engine must fit in the remaining 9.5 KiB or
  the epic needs an owner decision on the budget.

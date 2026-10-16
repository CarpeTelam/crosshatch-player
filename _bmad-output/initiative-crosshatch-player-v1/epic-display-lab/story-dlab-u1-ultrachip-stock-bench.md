# Story dlab-u1: the UltraChip stock-path bench (lane A, after dlab-0)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R2, R5,
R6, Boundaries, Decisions, the owner Decision on options 1 and 3, D4). Ref: `dlab-u1`. Plan:
`_bmad-output/implementation-artifacts/plan-dlab-u1-ultrachip-stock-bench.md`. Builds on `dlab-0` (read
`src/activities/games/display_lab/ScanBackend.h` and `DisplayLabActivity.{h,cpp}` as they are in the tree).

## Intent

**Problem:** On the X4 Pro's UC8179 and UC8279 batches no caller can reach the controller's waveform registers
through the SDK facade (the lab API that will is being added to the SDK separately), so the one-frame scan bench
cannot run there yet. But the facade already exposes two UltraChip paths worth measuring and demonstrating on the
owner's UC8179 unit now: the stock non-flashing previous-to-current transition (`HalDisplay::displayGrayscaleBase`
with a Fast fallback, which the driver runs as the vendor "pre-BW" differential pass when a valid previous frame
exists), and the OEM four-level grayscale (`displayGrayscaleBase` + `copyGrayscaleBuffers` + `displayGrayBuffer`
with the driver's own banks).

**Approach:** On an X4 Pro whose controller is UC8179 or UC8279, show the "Display lab" row and a lab menu with two
UltraChip screens instead of the scan screens: (1) **Transition timing**: draws a test frame (a large checker of
8 blocks), then flips it a chosen number of times (10) through the non-flashing transition, timing each call with
`micros()` and logging `DLAB transition n=<i> ms=<ms> flashed=<yes/no>` where `flashed` is whether the driver fell
back to a plain refresh (detect from the measured time against the stock FAST time measured first, and from the
facade's capability flags; document the heuristic); shows min/median/max; a Back exits with a FULL refresh.
(2) **Gray ramp**: paints the OEM four-level grayscale (four vertical bands K/D/L/W via the overlay-mask planes the
facade documents in `freeink-sdk/docs/grayscale-capabilities.md`), times the base and the gray pass, logs
`DLAB gray base_ms=<> gray_ms=<>`, holds the panel until the next activation (D4), then cleans with a FULL refresh.
`ScanBackend` is not involved: these screens use the public facade through `HalDisplay`, and the lab's gate becomes
"any X4 Pro controller with a backend or an UltraChip stock bench" (SSD1677 keeps the scan screens; UltraChip gets
these two; the row's subtitle says which).

## Boundaries & Constraints

**Always:** public `HalDisplay` calls only (no `eink()` use is needed; if a call turns out to require it, say so in
Design Notes and use it, since ledger row 13 allows it); keep the warning, the exit FULL refresh, the inverted
refusal; log every timing; `STR_GAMES_DLAB_*` keys; host-test the pure parts (the checker frame generator, the
median, the overlay-mask band generator) in `test/game_display_lab/`; C3 `default` and `sticky` still build.

**Never:** no custom LUT, no register writes, no SDK change; no change to `ScanBackend` or the SSD1677 screens; no
new ledger row.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Gate | controller UC8179 or UC8279 on an X4 Pro | lab row shown, subtitle "UltraChip stock-path bench"; menu: Transition timing, Gray ramp, Exit | — |
| Gate | SSD1677 | unchanged from dlab-0 (scan screens) | — |
| Transition timing | Run | one stock FAST refresh timed first (the reference), then 10 transitions; 11 log lines; table of ms and a flashed flag per transition | a call over 15,000 ms ends the run, logs timeout, FULL refresh |
| Gray ramp | Run | base pass then gray pass; two timings logged; panel held until the next activation; FULL refresh on that activation | facade reports grayscale unsupported: notice, no pass |
| Exit | any route | one FULL refresh | — |

## Code Map (verify in the tree after dlab-0)

- `lib/hal/HalDisplay.h`: `displayGrayscaleBase(RefreshMode fallback, bool)`, `displayGrayscaleBase(GrayscaleMode, …)`, `copyGrayscaleBuffers`, `cleanupGrayscaleBuffers`, `displayGrayBuffer(bool)`, `supportsAsyncGrayscaleBase`/capability getters, `getController()`.
- `freeink-sdk/docs/grayscale-capabilities.md` (the overlay-mask encoding: LSB/MSB planes, dark gray in both) and `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8179Driver.cpp` (`displayGrayscaleBase` runs `transitionGrayscaleBase` only when fallback is Fast, `_grayRefreshedOnce`, `_oldPlaneValid`, and no full clear is pending; so the screen must do one grayscale pass or one ordinary refresh cycle first, then transitions; document what sequence makes the transition path active) and `Uc8279X4Driver.cpp` (same gate; planes inverted on this driver, handled by the facade).
- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` (menu, gate, budget wrapper, exit refresh), `PolarityActivity.cpp` (the render-override pattern for holding the panel).
- `src/activities/reader/EpubReaderActivity.cpp` near line 1914 (an existing UC8279-gated call site to learn the facade's expected call order for a grayscale page).

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`, `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro` as AGENTS.md gives it; the flash-budget gate with figures against the epic base.
- Hardware (owner): the transition-timing table and the gray-ramp photo on the UC8179 unit; these are the story's real deliverables and go in the plan's Verification when the owner reports them.

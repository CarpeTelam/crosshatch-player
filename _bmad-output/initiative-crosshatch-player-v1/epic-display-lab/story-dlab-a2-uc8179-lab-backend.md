# Story dlab-a2: the UC8179 lab backend over the SDK seam (lane A, experimental branch)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R1, R2,
R5, R6, Boundaries, Decisions D1-D3 and D7-D10, the owner Decision on options 1 and 3; D8 and D9 amend D2/D3 for
UltraChip and this story follows them; D7 is UC8279-only). Ref: `dlab-a2`. Plan:
`_bmad-output/implementation-artifacts/plan-dlab-a2-uc8179-lab-backend.md`. Builds on `dlab-0`
(`ScanBackend`, `MaskPlanes`) and on the SDK seam (`FREEINK_CAP_DISPLAY_LAB` lab API in the submodule:
`labSupported`, `labUploadWaveform`, `labSetFrameRate`, `labScan`, `labEnd` on `FreeInkDisplay`, reached
through `HalDisplay::eink()`; read `freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h` and
`src/driver/Uc8179Driver.cpp` in the tree, where the seam's own comments state the register sequences and plane geometry).

## Intent

**Problem:** The scan bench and the grayscale engine run on the SSD1677 batch only. The owner's unit is a UC8179.

**Approach:** Add `UC8179ScanBackend` (device side, `src/activities/games/display_lab/`) with its pure parts in
`lib/GameDisplayLab/UC8179ScanLut.{h,cpp}` (host-tested): the five 43-byte register-prefixed one-frame tables
(every table one group of one phase, `T1 = frames`, repeat 1, 8-bit counts so no D7 split; VCOM, WW and KK at the
GND level for those frames so an unchanged pixel holds and the activation length follows TP (D9); KW and WK with
level codes per `swapPolarity`, D3; `UC8279ScanLut` on this branch is the worked example), the PLL code list for the frame-rate index (the controller's
documented PLL register values; cite the source in Design Notes; if none is documented, three values bracketing the
driver's config value), and the plane encoding from the two masks per D2 (old plane and new plane: hold = equal and
white, drive-to-white = old black / new white, drive-to-black = old white / new black). `scan()` is a
self-contained session every call (D8): `labUploadWaveform`, `labSetFrameRate`, `labScan(old, new, busyUs)`, then
`labEnd`, as `UC8279ScanBackend` does; `clean()` is a FULL refresh through the renderer. The lab's gate adds this
controller (`factoryFor`); a backend wins over the stock bench (`usesStockBench`), so on it the menu offers the scan
screens (frame period, polarity), `dlab-c1`'s transition test, tuning and save preset, and, per D10, the stock
bench's two rows (Transition timing, Gray ramp) before Exit, so the owner can compare the stock transition and the
OEM ramp against the lab on the same unit.

## Boundaries & Constraints

**Always:** D1's `ScanBackend` shape unchanged; tables byte-pinned by host tests; voltages never written; the
seam's `labEnd` on every exit; the budget wrapper (timeout, cap, dropped, inverted) unchanged; C3 `default` and
`sticky` still build (the seam is compiled out there and the backend file is under the lab guard).

**Never:** no OTP/MTP command; no edit to `freeink-sdk/` in this story (the seam is fixed; a gap in it is an intent
gap: HALT and name it); no change to the SSD1677 backend.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Backend choice | controller UC8179 | `UC8179ScanBackend`; `labSupported()` true | `labSupported()` false (seam not compiled): no backend, row shows the stock bench only |
| All-hold scan | both masks clear, frames n, rate i | old == new == white for every pixel; all five tables with T1 = n (D9); PLL set to code i | — |
| Polarity scan | whiteMask all set | old black, new white for every pixel (KW table drives) ; swapPolarity swaps the KW/WK level codes | — |
| Table upload | every scan (D8) | `labUploadWaveform`, PLL, `labScan`, `labEnd` in that order, each call | upload refused: scan returns Refused, logged, `labEnd` still runs |
| Clean / exit | any | FULL refresh through the renderer (`labEnd` already ran inside the scan) | — |
| Stock rows (D10) | controller UC8179 with the backend | the scan menu shows Transition timing and Gray ramp before Exit; on an SSD1677 it does not | — |

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro` (the experimental branch's x4pro env sets `FREEINK_CAP_DISPLAY_LAB=1`, which the submodule's seam also reads), `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro`; the flash-budget gate against the epic base.
- Hardware (owner): the nine-line frame-period table and the polarity answer on the UC8179 unit.

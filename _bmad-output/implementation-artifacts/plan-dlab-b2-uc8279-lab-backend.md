---
title: 'dlab-b2: the UC8279 lab backend over the SDK seam'
type: 'feature'
ticket: 'story-dlab-b2-uc8279-lab-backend'
created: '2026-10-16'
status: 'blocked'
blocked_reason: 'intent gap: G1 frames over 63 vs the UC8279 6-bit phase count; G2 lab session lifetime vs the screens repainting between runs; G3 VCOM/WW/KK tables zero bytes vs GND level with T1 frames'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/freeink-sdk/docs/display-lab.md'
warnings: []
deferred: []
---

<intent-contract>

## Intent

**Problem:** The scan bench and the grayscale engine run on the SSD1677 batch only; the X4 Pro's UC8279 batch has no `ScanBackend`.

**Approach:** Add `UC8279ScanBackend` (`src/activities/games/display_lab/`) over the seam's `FreeInkDisplay` lab API (`display.eink()`), with its pure parts in `lib/GameDisplayLab/UC8279ScanLut.{h,cpp}` (host-tested): the five 43-byte register-prefixed one-frame tables, the PLL code list, and the old/new plane encoding from the two masks (D2). `scan()` uploads the tables, sets the PLL code, then `labScan(old, new, busyUs)`; `clean()` is `labEnd` then a FULL refresh; `factoryFor` maps `UC8279` to it.

## Boundaries & Constraints

**Always:** D1's `ScanBackend` shape unchanged; tables byte-pinned by host tests; voltages never written; the seam's `labEnd` on every exit; the lab wrapper (timeout, cap, dropped, inverted) unchanged; C3 `default` and `sticky` build with the lab compiled out.

**Never:** an OTP/MTP command; an edit under `freeink-sdk/`; a change to the SSD1677 backend or its bytes; the UC8179 case of `factoryFor` (lane A).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Backend choice | controller UC8279, X4 Pro | `UC8279ScanBackend`; `labSupported()` true | `labSupported()` false: not available, lab shows its notice |
| All-hold scan | both masks clear, frames n, rate i | old == new == 0xFF (white) everywhere; tables with T1 = n; PLL = code i | rate out of range: Refused, nothing sent |
| Polarity scan | whiteMask all set | old 0x00, new 0xFF (KW table); swapPolarity swaps KW/WK level codes | — |
| Table upload | first scan after a table change | `labUploadWaveform` once, then not again until a change | refused: Refused, logged |
| Clean / exit | any | `labEnd` then FULL refresh | — |

</intent-contract>

## Code Map

- `src/activities/games/display_lab/ScanBackend.h` -- D1 interface and `CROSSHATCH_DISPLAY_LAB` gate; unchanged.
- `src/activities/games/display_lab/ScanBackends.cpp` -- `factoryFor`: the `Controller::UC8279` case (`return nullptr;`) becomes `&make<UC8279ScanBackend>`; leave the UC8179 line to lane A. dlab-0 deferral: no test pins this table.
- `src/activities/games/display_lab/Ssd1677ScanBackend.{h,cpp}` -- golden example: PSRAM planes sized `display.getBufferSize()` in the ctor, `available()`, refusal log lines, `clean()` = `renderer.displayBuffer(HalDisplay::FULL_REFRESH)`.
- `src/activities/games/display_lab/DisplayLabActivity.cpp` -- wrapper `scan()` (rate check before the lock, cap clean, budget); `onExit` cleans only when `budget.sinceClean() > 0`.
- `src/activities/games/display_lab/FramePeriodActivity.cpp` (`run()` ends with `promoteNextRefresh(FULL_REFRESH)` + `requestUpdate()`) and `PolarityActivity.cpp` (`drive()` / `activateIndex`) -- repaint through the renderer between runs, not through the backend (evidence for G2).
- `lib/GameDisplayLab/Ssd1677Encoding.{h,cpp}`, `LabExperiments.h` (`FRAME_PERIOD_TPS = {1, 10, 100}`, `POLARITY_TP = 10`) -- pattern for the pure parts; shared, unchanged.
- `test/game_display_lab/CMakeLists.txt` -- add the new test file.
- `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp` (read only) -- `labTablesValid` (len 215, prefix 0x20+t), 42 caller bytes padded to the 49-byte register, `labScan` refuses until an upload in this session, `streamPlane(invert=false)`, `labEnd` restores PSR/CDI/PLL/TSSET; `makeQualityBank` and `kXtfPreBwMid` give the group layout.
- `docs/crosshatch/display-lab.md` -- add the UC8279 backend.

## Tasks & Acceptance

Pending the G1-G3 answers; the shape is settled:

**Execution:**
- [ ] `lib/GameDisplayLab/UC8279ScanLut.{h,cpp}` -- `UC8279_RATE_COUNT`, `uc8279RateLabel`, `uc8279RowLabel(row, swap)`, `buildUc8279ScanTables(out[215], frames, swap)`, `uc8279PllCode(index)`, `encodeUc8279Planes(black, white, old, new, bytes)`.
- [ ] `test/game_display_lab/test_uc8279_scan_lut.cpp` + CMake entry -- pin every table byte, rates, labels, the matrix's plane cases.
- [ ] `src/activities/games/display_lab/UC8279ScanBackend.{h,cpp}` + `ScanBackends.cpp` case.
- [ ] `docs/crosshatch/display-lab.md` -- UC8279 section.

**Acceptance Criteria:**
- Given a UC8279 X4 Pro, when the launcher opens, then the Display lab row shows and both scan screens run through `UC8279ScanBackend`.

## Implementation Notes

## Plan Change Log

## Review Triage Log

## Design Notes

Settled by the sources (cited):

- Group layout: 7-byte groups `[0x01, P1, P2, P3, P4, 0x01, 0x01]`, `Pn = level << 6 | frames` (6-bit count), as `makeQualityBank` writes them (hardware-validated per its comment) and every stock group in `kXtfPreBwMid` and `kUc8279X3_BwGc` reads; level 1 = VSH, 2 = VSL (`makeQualityBank`'s net-charge sign). The 42 caller bytes are six groups; the driver zero-pads the seventh.
- Level codes: unswapped KW (0x22, drive-to-white, row 1) = VSL (0x80), WK (0x23, row 2) = VSH (0x40), the direction of the stock GC and pre-BW tables (`kUc8279X3_BwGc` KW `0x9A`, WK `0x5A`); swapped exchanges them. Row labels "KW VSL" / "WK VSH" (D1a's "KW table" example), swapped "KW VSH" / "WK VSL".
- PLL list: no UC8279 PLL table is documented in the tree, the SDK, or the research run; the story's fallback, three codes bracketing the driver's `pll = 0x0E`, ordered as the SSD1677 list (the known value first, so the polarity screen's rate 0 is stock): {0x0E, 0x0D, 0x0F}. 0x0F is also Ink-boy's and the X3 init's code (research [7]).
- Planes (D2, framebuffer convention 1 = white; the lab's CDI `cdiAa` equals the B/W refresh's `cdiBwFull`, 0x97): per byte `old = ~white`, `new = ~black`, so hold = (1, 1), drive-to-white = (0, 1), drive-to-black = (1, 0); a pixel in both masks (undefined by D1) gives (0, 0), the KK table, which also holds.
- `.gitmodules` and the `freeink-sdk` pointer differ from `develop` on this branch: the orchestrator's touch (epic Decision 2026-10-16, experimental branch), not this story's; the ledger check's flag on it is expected.

## Verification

**Commands:** (after the answers) host suite under the hosttest lock; `scripts/check_upstream_touches.py`; `./bin/clang-format-fix`; then under the build lock with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `-e default`, `-e sticky`; `pio check -e x4pro`; the flash-budget gate against the epic base.

## Auto Run Result

**Status: blocked (intent gap).** Planning stopped at step 2; no code changed. Three questions, each with readings that differ on hardware:

**G1. `frames` over 63.** The UC8279 phase byte holds a 6-bit frame count (`makeQualityBank`: `frames & 0x3F`, cap 63; level in bits 7:6), but the frame-period experiment sends `frames = 100` (`LabExperiments.h` `FRAME_PERIOD_TPS = {1, 10, 100}`), and the story asks for "one group of one phase, T1 = frames, repeat 1" and a nine-line table on hardware. Options: (A) refuse `frames > 63` (Scan::Refused, logged): six timed lines, the TP=100 lines show Refused; (B) one group, frames split over its four phases at the same level (100 = 63 + 37; up to 252, above that Refused): nine timed lines, a continuous drive of `frames` frames; (C) repeat count > 1: not exact for every n, and the repeat byte is undocumented. **Recommend B.** (UC8179's tables carry 8-bit counts, so lane A does not have this.)

**G2. Session lifetime.** The seam requires `labEnd()` before any other display call, and an ordinary refresh drops the uploaded waveform without clearing the driver's `_labWaveformLoaded` (`freeink-sdk/docs/display-lab.md` "Call labEnd() before any other display call"; `Uc8279X4Driver.cpp` clears it only in `labEnd`). The screens repaint through the renderer between runs (`FramePeriodActivity::run` end, `PolarityActivity` after each look), not through `clean()`, and D1 has no end-of-run hook. With the story's upload cache ("once, then not again until a change"), the second run after a repaint would skip the upload and scan whatever waveform the OTP refresh left in the LUT registers (a hold scan could drive every pixel), and the repaint itself runs at the lab's PLL code. Options: (A) per-scan session inside `scan()`: upload, PLL, `labScan`, `labEnd` every scan (about 260 extra SPI bytes per scan, outside the timed DRF window; no shared-file or D1 change; the matrix's upload row becomes "before every scan"); (B) a D1 addition (`endScans()`) the lab calls before each repaint, keeping the cache (edits D1, both screens, lane A); (C) the screens call the lab's clean instead of promote + repaint (edits both dlab-0 screens, shared with lane A). **Recommend A.** Lane A's UC8179 backend faces the same rule.

**G3. The hold tables' timing.** The story says "VCOM all zero; WW and KK all zero", but D3 says all five tables carry "one group of one phase (T1 = frames)" and the matrix's all-hold row expects "tables with T1 = n". Every stock UC8279 table in the driver gives all five tables the same frame counts and differs only in level bits, and `makeQualityBank` pads tables "so all five LUTs finish on the same frame". The frame-period experiment's hold pixels all select WW; with WW and VCOM at zero frames the activation length may not follow TP. Options: (A) VCOM, WW, KK all zero bytes; (B) VCOM, WW, KK with group 0 phase 1 = GND level (0x00) for T1 frames, framing bytes as KW/WK. **Recommend B.**

Evidence read: the story, the epic (R1-R6, D1-D6), `story-sdk-seam-display-lab.md`, `freeink-sdk/docs/display-lab.md`, `FreeInkDisplay.h`/`.cpp` lab section, `Uc8279X4Driver.{h,cpp}`, `Uc8279X3Luts.h`, `Uc8179Driver.cpp` (table format), dlab-0's backend, screens and plan, the research doc. Files changed: this plan only. Formatting: no source touched.

---
title: 'dlab-a2: the UC8179 lab backend over the SDK seam'
type: 'feature'
ticket: 'story-dlab-a2-uc8179-lab-backend'
created: '2026-10-16'
status: 'built'
baseline_revision: 'b8f86fa7b241fc427fea3d8c1560553b24743556'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/freeink-sdk/docs/display-lab.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
warnings: [oversized]
deferred:
  - summary: >-
      The UC8179 backend's device wiring (factoryFor's labSupported() gate, scan()'s plane argument order, clean()'s labEnd before the FULL refresh) is pinned by no test.
    evidence: |-
      ScanBackends.cpp and UC8179ScanBackend.cpp compile only in x4pro with no host harness for display.eink(); the tests drive runUc8179Request with their own buffers. Inverting the labSupported() ternary, swapping oldPlane/newPlane in scan(), or dropping labEnd() from clean() passes every test. Same gap as dlab-0's and dlab-b2's carried factoryFor items; a facade fake or the owner's hardware checklist (steps 3 and 6) settles it.
    location: >-
      src/activities/games/display_lab/ScanBackends.cpp, src/activities/games/display_lab/UC8179ScanBackend.cpp
    severity: medium
---

<intent-contract>

## Intent

**Problem:** The scan bench and the gray engine run on the SSD1677 and UC8279 batches only; the owner's unit is a UC8179, which gets only the stock-path bench.

**Approach:** Add `UC8179ScanBackend` over the SDK seam's `FreeInkDisplay` lab API (`display.eink()`), its pure parts in `lib/GameDisplayLab/UC8179ScanLut.{h,cpp}` (five 43-byte one-group tables, one phase, `T1 = frames`, repeat 1, D9 levels; the PLL code list; the old/new planes per D2) and its request-to-session template in `UC8179ScanSession.h` (D8: upload, PLL, `labScan`, `labEnd` every call). `factoryFor` maps `UC8179` to it. Per D10, an UltraChip controller with a scan backend gets the stock bench's Transition timing and Gray ramp rows before Exit on the scan menu.

## Boundaries & Constraints

**Always:** D1's `ScanBackend` shape unchanged; table bytes pinned by host tests; voltages never written; `labEnd` ends every scan that made a lab call and `clean()` calls `labEnd` before its FULL refresh; the lab wrapper (`DisplayLabActivity::scan`: inverted, rate, cap, timeout, dropped) unchanged; locals under 256 B (the 215-byte tables are a backend member); planes in PSRAM with the backend; C3 `default` and `sticky` build with the lab compiled out; the stock-bench-only menu stays for an UltraChip controller with no backend (D10).

**Never:** an OTP/MTP command; an edit under `freeink-sdk/`, `.agents/`, the epic or story files; a change to the SSD1677 or UC8279 backends' bytes or sequences; new i18n keys; an upload cache across scans (D8).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Backend choice | controller UC8179, `display.eink().labSupported()` true | `UC8179ScanBackend`; launcher row "Panel waveform bench" | `labSupported()` false: `factoryFor` gives no backend, so the row shows the stock bench only (the story's row); a scan whose session finds it false is Unsupported, Refused, no other lab call |
| All-hold scan | both masks clear, frames n (0-255), rate i | old == new == 0xFF; five tables `[prefix, L<<6, n, 0, 0, 0, 0x01, 0 x36]`, VCOM/WW/KK L=0; calls upload, PLL `{0x06,0x05,0x07}[i]`, labScan, labEnd; Done with labScan's busyUs | — |
| Polarity scan | whiteMask all set / blackMask all set | row 1: old 0x00, new 0xFF (KW); row 2: old 0xFF, new 0x00 (WK); KW VDL (0x80) / WK VDH (0x40), exchanged when swapPolarity | — |
| Bad request | null mask, or rate ≥ 3 | Refused, busyUs 0 | no lab call, nothing written |
| Lab call fails | upload, PLL or labScan false | Refused, busyUs 0, logged | labEnd still called, nothing after it |
| Clean / exit | any | `labEnd` (no-op outside a session), then FULL refresh through the renderer | — |
| Menu (D10) | X4 Pro, controller | UC8179/UC8279 (backend): Frame period, Polarity, Transition test, Tuning, Save preset, Transition timing, Gray ramp, Exit; SSD1677: the six without the two stock rows; UltraChip without backend: Transition timing, Gray ramp, Exit; other: no lab | notice row (unsupported, inverted, OOM) as today |

</intent-contract>

## Code Map

- `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8179Driver.cpp` (read only) -- lab section ~794-890: `labTablesValid` (len 215, `bytes[t*43] == 0x20+t`), upload = PSR psr0, five tables 42 B each, CDI `cdiActive`; `labSetFrameRate` writes PLL verbatim, never refuses; `labScan` false until an upload; `labEnd` restores PSR/CDI/PLL 0x06/TSSET. Lines 43-72: group layout `[rail selectors, TA, TB, TC, TD, repeat]` (R23h comment), `kDarkGrayLut` "VDL phase" = level bits `10`; plane select (old=DTM1, new=DTM2): (0,0) KK, (0,1) KW, (1,0) WK, (1,1) WW. `PLL_40_HZ 0x05`, `PLL_50_HZ 0x06`. `kGrayPreBwMid` KW 0xAA, WK 0x55.
- `lib/GameDisplayLab/UC8279ScanLut.{h,cpp}`, `UC8279ScanSession.h` -- worked example: constants, `RATE_LABELS`, `uc8279RowLabel`, `buildUc8279ScanTables`, `encodeUc8279Planes`; `ScanOutcome`, `runUc8279Scan(lab, tables, pll, old, new, busyUs)` (generic UltraChip sequence, uploads `UC8279_SCAN_TABLE_BYTES` = 215), `runUc8279Request`, `scanOutcomeName`. Reuse `ScanOutcome`, `scanOutcomeName`, `runUc8279Scan`; do not edit.
- `src/activities/games/display_lab/UC8279ScanBackend.{h,cpp}` -- mirror for the UC8179 backend (ctor PSRAM planes, `available()`, one `LOG_ERR("DLAB", "scan refused: UC8179 %s …")`, `clean()` = `display.eink().labEnd()` then `renderer.displayBuffer(HalDisplay::FULL_REFRESH)`).
- `src/activities/games/display_lab/ScanBackends.cpp` -- `case Controller::UC8179: return nullptr;` becomes `&make<UC8179ScanBackend>` plus include.
- `src/activities/games/display_lab/StockBenchGate.{h,cpp}` -- `usesStockBench`, `labRowSubtitle`; add the D10 gate.
- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- `available()`, `Row` enum, `ROW_COUNT`, `stockBench`, `listCount`, `buildRows`, `buildStockRows`, `activateIndex`, `activateStock`, `StockRow`, `savePreset` (writes `rowItems[Row::SavePreset]`). Guards to keep: `activateIndex`'s range check and notice → `finish()`, `app.clearTapFlash()`; `savePreset`'s `if (!notice)`.
- `TransitionTimingActivity.h`, `GrayRampActivity.h` -- comment "`lab` is unused: the stock bench has no scan session" needs D10 wording; code unchanged.
- `lib/GameDisplayLab/StockBench.h`, `test/game_display_lab/test_stock_bench.cpp` -- home of the pure menu gate and its test.
- `test/game_display_lab/test_uc8279_scan_lut.cpp`, `CMakeLists.txt` -- test style and fake (`RecordingLab`); add the new test file and `UC8179ScanLut.cpp`.
- `docs/crosshatch/display-lab.md` -- intro (lines 5-16), Safety (29-35), UC8279 section (38-75) as template, checklist steps 3, frame period, polarity labels and `vs=` tokens, Expected ranges, stock bench section (144-200), Code map (347-407).

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/UC8179ScanLut.{h,cpp}` -- namespace `GameDisplayLab`: `UC8179_TABLES 5`, `UC8179_TABLE_BYTES 43`, `UC8179_SCAN_TABLE_BYTES 215`; `UC8179_PLL_CODES[] = {0x06, 0x05, 0x07}`, `UC8179_RATE_COUNT`; `uc8179RateLabel(i)` ("0x06", "0x05", "0x07", nullptr out of range); `bool uc8179PllCode(i, uint8_t&)`; `uc8179RowLabel(row, swap)` ("KW VDL"/"WK VDH", swapped "KW VDH"/"WK VDL", else nullptr); `void buildUc8179ScanTables(uint8_t* out, uint8_t frames, bool swap)`; `encodeUc8179Planes(black, white, old, new, bytes)` (old = ~white, new = ~black). Pure C++; comments cite the sources in Design Notes.
- [x] `lib/GameDisplayLab/UC8179ScanSession.h` -- `template <Lab, Request> ScanOutcome runUc8179Request(lab, request, tables, old, new, bytes, busyUs)`: busyUs 0; null mask → NoMasks, bad rate → BadRate (no lab call, nothing written); build tables, encode, `runUc8279Scan`. `static_assert(UC8179_SCAN_TABLE_BYTES == UC8279_SCAN_TABLE_BYTES)` with a comment that the seam's UltraChip session is one sequence for both.
- [x] `test/game_display_lab/test_uc8179_scan_lut.cpp` + `CMakeLists.txt` -- every table byte for frames 0, 1, 10, 100, 255, both swaps, against an independently written expectation; swap exchanges only KW/WK level bytes; prefixes; rate list, labels, row labels against the tables' level bits; planes for the matrix cases and a both-masks pixel; `runUc8179Request` with a recording fake (UC8179 refusals: length/prefix upload, labScan before upload; never refuses PLL): all-hold for every rate × FRAME_PERIOD_TPS, polarity both swaps, bad requests, upload/PLL/scan refusals end with labEnd, Unsupported makes no other call, every scan its own session.
- [x] `src/activities/games/display_lab/UC8179ScanBackend.{h,cpp}` -- `final : ScanBackend`, as `UC8279ScanBackend` with UC8179 names, `runUc8179Request`, `supportsWeakRow()` false.
- [x] `src/activities/games/display_lab/ScanBackends.cpp` -- UC8179 case (`display.eink().labSupported() ? &make<UC8179ScanBackend> : nullptr`, so `hasScanBackend` and the gate follow the seam) and include; the UC8279 case untouched.
- [x] `lib/GameDisplayLab/StockBench.h` + `test_stock_bench.cpp` -- `enum class LabMenu : uint8_t { None, Scan, ScanWithStockRows, StockBench }` and `constexpr LabMenu labMenuFor(bool ultraChip, bool hasScanBackend)`; test pins all four cases.
- [x] `StockBenchGate.{h,cpp}` -- `GameDisplayLab::LabMenu labMenuFor(HalDisplay::Controller)` (UltraChip = UC8179 or UC8279, `hasScanBackend`); `usesStockBench` reads it; header comment states D10.
- [x] `DisplayLabActivity.{h,cpp}` -- `available()` = X4 Pro and `labMenuFor(c) != None`; member `LabMenu menu` replaces `bool stockBench`; one `Row` enum `{FramePeriod, Polarity, TransitionTest, Tuning, SavePreset, TransitionTiming, GrayRamp, Exit}` (`ROW_COUNT` 8), rows appended per menu with `rowCount`, dispatch on the row's `actionValue`; `StockRow`, `buildStockRows`, `activateStock`, `STOCK_ROW_COUNT` go; class comment updated (UC8179 has a backend; the eight-row menu on UltraChip).
- [x] `TransitionTimingActivity.h`, `GrayRampActivity.h` -- constructor comment: `lab` unused; on the scan menu every scan has ended its own lab session.
- [x] `docs/crosshatch/display-lab.md` -- UC8179 backend section, menus per D10, safety (UC8179 BUSY wait also `UcIdleHigh`), checklist labels and rates, Expected ranges, stock bench intro, code map.

**Acceptance Criteria:**
- Given a UC8179 X4 Pro, when Games opens, then the Display lab row reads "Panel waveform bench"; the frame-period screen lists nine scans `tp=… fr=0x06/0x05/0x07`; the polarity screen labels "Row 1 (KW VDL)" and "Row 2 (WK VDH)"; the menu ends Transition timing, Gray ramp, Exit.
- Given an SSD1677 X4 Pro, when the lab opens, then its menu is the six rows ending Save preset, Exit.
- Given `default` and `sticky`, when built, then no display_lab or UC8179 lab code compiles in.

## Implementation Notes

- 2026-10-16, build: every Execution item done by the implementation subagent (its run outlived the parent's first hand-back; the orchestrator resumed this run from `in-progress`, and nothing was redone). New: `lib/GameDisplayLab/UC8179ScanLut.{h,cpp}`, `UC8179ScanSession.h`, `src/activities/games/display_lab/UC8179ScanBackend.{h,cpp}`, `test/game_display_lab/test_uc8179_scan_lut.cpp`; edited `StockBench.h` (`LabMenu`, `labMenuFor`), `StockBenchGate.{h,cpp}`, `DisplayLabActivity.{h,cpp}` (one `Row` enum, rows appended, dispatch on `actionValue`; `savePreset` writes the row only on a scan menu), `ScanBackends.cpp`, the two stock screens' constructor comments, `test_stock_bench.cpp`, `CMakeLists.txt`, `docs/crosshatch/display-lab.md`.
- Verify (parent): host suite 2,224 of 2,224 pass (the 19 `Uc8179*` tests and `StockBenchLabMenu.EveryCase` among them); `check_layers.py` passes; every `scripts/*_test.py` passes; `check_upstream_touches.py` flags only `.gitmodules` and the `freeink-sdk` pointer (the orchestrator's, at HEAD); `./bin/clang-format-fix` changes nothing.
- Matrix audit: all-hold, polarity, bad request, lab-call failure, and the in-session `labSupported()` false row are pinned by `Uc8179Request.*` / `Uc8179ScanLut.*` / `Uc8179Planes.*` through the recording fake; the menu row and the "no backend → stock bench" half of Backend choice by `StockBenchLabMenu.EveryCase` (the pure gate). Device glue with no host harness, as for the SSD1677 and UC8279 backends: `factoryFor`'s `labSupported()` check, `clean()`'s labEnd-then-FULL, and the menu's row order in `buildRows`.
- Base measurement before any change: plain `pio run -e x4pro` at `b8f86fa7` (this worktree, shared cache, build lock): firmware.bin 6,257,504 B; RAM 104,256 B (31.8 %).

- Built uncommitted on `dlab-lane-b` (staged). Host suite 2,224 tests pass (new: 18 `Uc8179*` tests, 1 `StockBenchLabMenu`); ledger check against the staged tree flags only `.gitmodules` and the `freeink-sdk` pointer; `check_layers.py` and all 13 `scripts/*_test.py` pass; `./bin/clang-format-fix` twice, second run clean. Firmware builds, `pio check` and the flash gate not yet run (after review).
- The UC8179 fake has `refuseUpload` / `refuseRate` knobs besides its seam rules: the request builds valid tables, so an upload refusal is forced, and the UC8179 never refuses PLL, so the RateRefused path (shared `runUc8279Scan`) is exercised only through the knob.
- `savePreset` writes the Save preset row only when the menu is not the stock bench alone (the row exists only on the scan menus).
- Doc's UC8179 Expected-ranges bullet states two derived expectations (0x05 slope ~1.25x 0x06's if they are 40/50 Hz; a slope about twice the rate's period would mean RP + 1 runs); both are inference, flagged as such in the text's conditionals.

## Plan Change Log

## Review Triage Log

### 2026-10-16 — Review pass
- lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment, each a context-free subagent over `git diff --cached b8f86fa7` (82,526 B, the plan excluded); the intent auditor read the story file as the verbatim intent. Patches went back to the step-03 implementation subagent.
- verdicts: 26 findings — high 0, medium 4, low 15, false 6, maybe-false 1
- findings:
  - `[low]` `[patch]` BH: the `labSupported()` check in `factoryFor` cannot produce the "no seam → stock bench" fallback the doc describes (the lab compiles only with `FREEINK_CAP_DISPLAY_LAB`, which compiles the seam; `Uc8179Driver::labSupported()` is true) — kept the check (the story's Backend-choice row), reworded the `ScanBackends.cpp` comment, the doc intro, UC8179 section, checklist step 3, stock-bench intro and code map: every UC8179 X4 Pro the lab builds for gets the scan menu.
  - `[low]` `[reject]` BH: `UC8179ScanBackend` copies `UC8279ScanBackend`; template it on traits — the epic's Lanes give each backend its own pair of files; templating would rewrite lane B's merged backend for a ~60-line class.
  - `[low]` `[patch]` BH: `runUc8279Scan`, `ScanOutcome` and the code map still read UC8279-only though the UC8179 shares them — `UC8279ScanSession.h` comments and the code-map entry now name the shared UltraChip session and TooManyFrames as UC8279-only; no code change.
  - `[medium]` `[patch]` BH: each menu's row layout is untested and `savePreset` relies on SavePreset's fixed index — grouped with VG's menu-row gap; `LabRow`, `LAB_ROW_COUNT` and constexpr `labMenuRows(menu, out)` moved into `StockBench.h`, `buildRows` walks it, `savePreset` finds the row by `actionValue`, `StockBenchLabMenu.EachMenusRowsInOrder` pins all four lists.
  - `[low]` `[patch]` BH: `ROW_COUNT` is a hand-written 8 and `add` writes `rowItems[rowCount++]` unbounded — grouped with EC's deletion finding; `rowItems` is now sized `LAB_ROW_COUNT = LabRow::Exit + 1` and filled only from `labMenuRows`, which writes at most one of each row.
  - `[low]` `[reject]` BH: an OOM session on a `ScanWithStockRows` unit shows the notice and loses the stock rows — two 48,000 B PSRAM masks plus two backend planes failing on an 8 MB-PSRAM X4 Pro is not everyday use, and the fallback adds a branch on a path that already shows a clear notice.
  - `[low]` `[reject]` BH: the stock rows' FULL refreshes do not reset the lab's scan budget, so leaving can run a redundant clean — the budget counts lab scans only; an extra FULL on exit is harmless, and a hook would add surface to the dlab-u1 screens for no defect.
  - `[low]` `[patch]` BH: VCOM level 00 is VCOM DC, not GND, yet comments, the test and the doc said "VCOM … at GND" — wording now "level 00 (GND on WW and KK, VCOM DC on the VCOM table)"; no byte change.
  - `[low]` `[patch]` BH: doc reflow left orphan lines, lines over 120 columns and an unparseable "have a backend" sentence — reflowed the named paragraphs, fixed the sentence (six pre-existing long lines at HEAD left untouched).
  - `[low]` `[patch]` BH: the UC8179 Expected-ranges bullet lacks the DRF-only `busy_us` caveat and any note on undocumented PLL 0x07 — both added.
  - `[low]` `[reject]` BH: the test file copies the UC8279 suite's scaffolding — each backend keeps its own test file (epic Lanes); a shared fake header would edit lane B's test for no defect.
  - `[low]` `[reject]` BH: two `labMenuFor` overloads, `LabMenu` in `StockBench.h`, `usesStockBench` with one caller — the overloads differ by parameter type and namespace; `StockBench.h` is the plan's chosen home for the bench gate; `usesStockBench` kept so `labRowSubtitle` reads unchanged.
  - `[false]` `[reject]` BH: the plan is untracked and firmware verification has not run — the plan is committed at Finalize, and the firmware checks run after review by the workflow's order (recorded under Verification).
  - `[low]` `[reject]` EC: OOM on `ScanWithStockRows` hides the stock rows — same claim as BH's OOM row; rejected there.
  - `[low]` `[patch]` EC: the removed `STOCK_ROW_COUNT <= ROW_COUNT` static_assert leaves `rowItems` unbounded — grouped with BH's bound row; patched there.
  - `[low]` `[reject]` EC: `UC8179ScanLut.cpp` has no lab guard, so the pure lib may compile in `default` and `sticky` — the same as `UC8279ScanLut.cpp`; unreferenced, it is dropped at link (`--gc-sections`), and the device backend that references it sits under `CROSSHATCH_DISPLAY_LAB`.
  - `[maybe-false]` `[reject]` EC: rate index 0 (0x06) may not be the B/W path's stock PLL, since the B/W init never writes 0x30 — the driver's own grayscale path and `labEnd` both write 0x06 ("the 50 Hz rate the driver's grayscale path restores"); settled only by reading PLL's power-on value on the unit; if true it changes one comment's word "stock" (low), so rejected.
  - `[medium]` `[patch]` VG: no test checks which rows each menu shows or what each dispatches; three demonstrated mutations pass every test — the menu-row group patched under BH's row above.
  - `[medium]` `[defer]` VG: the UC8179 wiring (`factoryFor`'s `labSupported()` gate, the plane argument order in `scan()`, `labEnd` before the clean) has no automated check — no host harness exists for `display.eink()` or any backend class (the SSD1677 and UC8279 backends are the same); deferred, joining dlab-0's and dlab-b2's carried `factoryFor` item; hardware checklist steps 3 and 6 are its check.
  - `[false]` `[reject]` IA: the PLL list may be read as "the tree's documented codes" (two) — the story's own hardware check is a nine-line frame-period table (three TPs by three rates), which two codes cannot give; Design Notes cite the two documented codes and the bracket.
  - `[false]` `[reject]` IA: the session makes five calls (labSupported first) where the matrix names four — `labSupported()` is a const query with no bus traffic; the four lab calls run in the matrix's order every scan.
  - `[false]` `[reject]` IA: `clean()` calls `labEnd` though the story says it already ran — the story's Always keeps "the seam's labEnd on every exit"; `labEnd` is a no-op outside a session, matching the UC8279 backend.
  - `[false]` `[reject]` IA: D10 here also changes the UC8279 menu — D10 reads "an UltraChip controller with a scan backend gets the stock bench's two rows", which includes the UC8279.
  - `[low]` `[patch]` IA: the "labSupported() false" fallback is unreachable on a lab build and its test exercises the per-scan Unsupported path — same root cause as BH's first row; documented there.
  - `[medium]` `[patch]` IA: the intent's device surfaces (row order, `factoryFor`, `scan()`, `clean()`) are untested; tests reach only pure surfaces — the row order part is the menu-row group (patched); the backend wiring part is VG's deferred row.
  - `[false]` `[reject]` IA: no evidence of firmware builds, `pio check`, the flash gate or the hardware run — firmware checks run after review (Verification); the hardware run is the owner's.

## Design Notes

- **Group layout** (the story; D3; `Uc8179Driver.cpp` "UC8179 datasheet R23h: each group is [rail selectors, four frame counts, repeat count]"): data byte 0 = level select, phase A in bits 7-6; bytes 1-4 = TA-TD, 8-bit; byte 5 = repeat 0x01 (every stock group's value); 36 zero bytes. One phase: `[L<<6, frames, 0, 0, 0, 0x01]`. Whether the controller runs a group RP or RP+1 times is not documented in the tree; the frame-period slope shows it.
- **Levels**: 00 GND (VCOM table: VCOM DC), 01 VDH, 10 VDL (`kDarkGrayLut`'s "VDL phase" is bits `10`). Unswapped KW (drive to white, row 1) = VDL, WK (row 2) = VDH: the direction of `kGrayPreBwMid` (KW 0xAA, WK 0x55) and of the UC8279 backend; the polarity screen confirms on hardware. D9: VCOM, WW, KK carry `frames` at level 00.
- **Example, frames 10 unswapped**: VCOM `20 00 0A 00 00 00 01`, WW `21 00 0A …`, KW `22 80 0A 00 00 00 01`, WK `23 40 0A 00 00 00 01`, KK `24 00 0A …`, zeros to 43. Frames 0 gives TA 0 (no caller sends it; as the UC8279 builder accepts it). No refusal: 255 fits the 8-bit count (D7 is UC8279-only).
- **PLL list**: the tree documents two UC8179 codes, `PLL_40_HZ 0x05` and `PLL_50_HZ 0x06` (`Uc8179Driver.cpp:40-41`; `freeink-sdk/docs/display-lab.md` "UC8179 PLL 0x06 is the 50 Hz rate"; the gray_full header writes `0x67 >> 4 = 0x06`); no full table, so the story's fallback: three codes bracketing the driver's 0x06, stock first as the UC8279 list (rate 0 is the polarity screen's): {0x06, 0x05, 0x07}; 0x05 is the documented 40 Hz, 0x07 undocumented. Labels hex, as on the UC8279.
- **Planes** (D2): the lab's CDI is `cdiActive`, the AA activation's, under which the driver states (old, new) = (0,1) KW, (1,0) WK, (1,1) WW, (0,0) KK: old = ~white, new = ~black; a both-masks pixel is KK, held at GND.
- **Session reuse**: the seam's UltraChip calls and table layout are one API for both controllers, so `runUc8179Request` reuses `runUc8279Scan` and `ScanOutcome` (TooManyFrames never occurs) rather than a second copy; its own test pins the UC8179 sequence through its fake.
- **D10 menu**: one `Row` enum and one dispatch for both menus (the stock rows now appear in two menus); `SavePreset` keeps index 4 because the rows before it are always present on the scan menu. `labMenuFor(ultraChip, hasScanBackend)`: backend → Scan or ScanWithStockRows; none → StockBench (UltraChip) or None.
- `RecordingLab` (UC8179 test) stands in for `FreeInkDisplay`'s lab API as `Uc8179Driver` defines it; more permissive than the device: never refuses for inversion (the wrapper and facade do) and never times BUSY.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass.
- `python3 scripts/check_upstream_touches.py` (expected: only `.gitmodules` and the `freeink-sdk` pointer, the orchestrator's), `python3 scripts/check_layers.py`, every `python3 scripts/*_test.py`, `./bin/clang-format-fix` twice -- expected: pass, second run clean.
- After review, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, under `flock /tmp/crosshatch-build.lock`: `pio run -e x4pro`, `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`; `scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects` -- expected: all succeed; record x4pro firmware.bin and RAM against the same build at `b8f86fa7`, and the gate against the D6 base (+258,192 B, 18,288 B to spare; RAM +784 B).

**Manual checks:**
- Hardware (owner): the nine-line frame-period table and the polarity answer on the UC8179 unit.

**Results (2026-10-16):**
- Host, after the review patches: 2,225 of 2,225 pass (`StockBenchLabMenu.EachMenusRowsInOrder` added); `check_layers.py` passes; all 13 `scripts/*_test.py` pass; `check_upstream_touches.py` flags only `.gitmodules` and the `freeink-sdk` pointer (the orchestrator's; this story touches fork-only paths); `./bin/clang-format-fix` twice, second run clean.
- Firmware, once after review, worktree, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, under `flock /tmp/crosshatch-build.lock`: `pio run -e x4pro` rc 0, firmware.bin **6,258,672 B, +1,168 B** over the same build at `b8f86fa7` (6,257,504 B, taken first in this run); RAM 104,256 B, **+0**. `pio run -e default` rc 0, `pio run -e sticky` rc 0. `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`: no defects.
- Flash gate (D6: both images leave the lab out): `build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`: games on 6,218,528 B, off 5,960,336 B, **+258,192 B, 18,288 B to spare: +0 against the D6 base** (`9b2317f3` + D6, `plan-dlab-c1-gray-engine.md`); static RAM +784 B, 240 B to spare (+0); objects: no problems. No CI gate changed, so no fresh tree.

## Auto Run Result

**Status: built.** Summary: the UC8179 batch gets a scan backend over the SDK seam's lab API (`UC8179ScanBackend`, pure `UC8179ScanLut` and `UC8179ScanSession`): five one-group, one-phase 43-byte tables (KW VDL / WK VDH unswapped, VCOM/WW/KK at level 00 per D9), PLL rates {0x06, 0x05, 0x07}, old = ~white / new = ~black planes, one self-contained lab session per scan (D8), `labEnd` then FULL in `clean()`. `factoryFor` maps the UC8179 to it when the facade reports lab support. D10: a pure `labMenuFor` / `labMenuRows` gate gives an UltraChip controller with a backend (UC8179, UC8279) the scan menu with Transition timing and Gray ramp before Exit; the SSD1677 keeps six rows; the stock-bench-alone menu stays for an UltraChip controller without a backend.

**Files:**
- `lib/GameDisplayLab/UC8179ScanLut.{h,cpp}` -- tables, PLL list, rate and row labels, planes.
- `lib/GameDisplayLab/UC8179ScanSession.h` -- `runUc8179Request` (checks, build, encode, the shared `runUc8279Scan`).
- `lib/GameDisplayLab/UC8279ScanSession.h` -- comments only: the session and `ScanOutcome` are shared.
- `lib/GameDisplayLab/StockBench.h` -- `LabMenu`, `labMenuFor`, `LabRow`, `LAB_ROW_COUNT`, `labMenuRows` (D10).
- `src/activities/games/display_lab/UC8179ScanBackend.{h,cpp}` -- the device backend.
- `src/activities/games/display_lab/ScanBackends.cpp` -- the UC8179 case.
- `src/activities/games/display_lab/StockBenchGate.{h,cpp}` -- `labMenuFor(Controller)`; `usesStockBench` reads it.
- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- one row list per menu, dispatch by row, Save preset found by row.
- `src/activities/games/display_lab/{TransitionTiming,GrayRamp}Activity.h` -- constructor comments for D10.
- `test/game_display_lab/test_uc8179_scan_lut.cpp`, `test_stock_bench.cpp`, `CMakeLists.txt` -- byte-pinned tables, planes, session via a recording fake; every menu's rows.
- `docs/crosshatch/display-lab.md` -- the UC8179 backend section, menus, safety, checklist, expected ranges, code map.

**Review:** 26 findings (high 0, medium 4, low 15, false 6, maybe-false 1). Patched: 1 medium entry (menu rows untested and `savePreset`'s fixed index: BH, VG, IA) and 6 low entries (unreachable no-seam wording, shared-session comments, row bound, VCOM level-00 wording, doc reflow, UC8179 expected ranges). Deferred: 1 medium (UC8179 device wiring has no host harness). Rejected, with reasons in the Review Triage Log: backend and test duplication of the UC8279 pair, OOM fallback to the stock rows, stock rows and the scan budget, overload naming, unguarded pure lib (link-time GC), PLL 0x06 power-on value (maybe-false), and the six false findings.

**Follow-up review recommended: false** -- one medium and no high entry patched on a first pass.

**Verification:** see `## Verification` → Results: host 2,225/2,225; x4pro, default, sticky build; `pio check -e x4pro` clean; x4pro firmware.bin +1,168 B and RAM +0 against `b8f86fa7`; flash gate +258,192 B (18,288 B to spare, +0 against the D6 base), static RAM +784 B (240 B to spare). Formatting changed nothing outside this story's paths.

**Residual risks:** hardware unverified (the owner's run): which table darkens (KW at VDL expected to lighten), whether a group with repeat 0x01 runs once or twice (the frame-period slope shows it), whether the controller takes PLL 0x07 (an unbounded UltraChip BUSY wait means a stall needs a power cycle), and the power-on PLL value the "stock" rate 0 assumes. The `labSupported()` check in `factoryFor` is always true on a lab build, so the stock-bench-alone menu is unreachable on this branch.

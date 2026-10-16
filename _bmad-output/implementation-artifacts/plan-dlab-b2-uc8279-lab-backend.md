---
title: 'dlab-b2: the UC8279 lab backend over the SDK seam'
type: 'feature'
ticket: 'story-dlab-b2-uc8279-lab-backend'
created: '2026-10-16'
status: 'built'
baseline_revision: '536a61b4829cd37d8bef6ed2c764aca8c25e62fc'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/freeink-sdk/docs/display-lab.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
warnings: [oversized]
deferred:
  - summary: >-
      factoryFor's UC8279 case (and the whole controller-to-backend table) is pinned by no test.
    evidence: |-
      ScanBackends.cpp compiles only in x4pro; nothing under test/ references hasScanBackend or makeScanBackend. Leaving
      the UC8279 case at nullptr or moving it to UC8279C passes every check. Same item as dlab-0's carried deferral in
      deferred-work.md; a pure controller-to-backend-kind mapping in lib/GameDisplayLab would let a host test pin it,
      best done once lanes A and B have both merged their cases.
    location: >-
      src/activities/games/display_lab/ScanBackends.cpp:36
    severity: medium
  - summary: >-
      A UC8279 BUSY that never releases hangs a lab scan with no bound, so the lab's 15 s timeout and clean never run.
    evidence: |-
      Uc8279X4Driver::busyPolarity() is UcIdleHigh; EpdBus::waitBusy's UcIdleHigh branch and waitRefreshComplete loop
      while BUSY_N is LOW with no millisecond cap ("deliberately no fixed millisecond timeout on this controller path").
      The same wait governs every ordinary UC8279 refresh, so the lab adds no new exposure; a bound (or a timed DRF wait
      for the lab) belongs in freeink-sdk and should be proposed upstream. docs/crosshatch/display-lab.md Safety states it.
    location: >-
      freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp:342
    severity: low
---

<intent-contract>

## Intent

**Problem:** The display lab's scan screens run on the SSD1677 batch only; the X4 Pro's UC8279 batch (`Uc8279X4Driver`) has no `ScanBackend`.

**Approach:** Add `UC8279ScanBackend` over the SDK seam's `FreeInkDisplay` lab API (`display.eink()`), with its pure parts in `lib/GameDisplayLab/UC8279ScanLut.{h,cpp}` (the five 43-byte one-group tables, split per D7, levels per D9; the PLL code list; the old/new planes per D2) and its call sequence in a pure template (`UC8279ScanSession.h`) that host tests drive with a recording fake. Each `scan()` is a self-contained lab session (D8); `factoryFor` maps `UC8279` to it, so the launcher row and both scan screens appear on UC8279 units.

## Boundaries & Constraints

**Always:** D1's `ScanBackend` shape unchanged; table bytes pinned by host tests; voltages never written; `labEnd` ends every scan that made a lab call, and `clean()` calls `labEnd` before its FULL refresh; the lab wrapper (`DisplayLabActivity::scan`: timeout, cap, dropped, inverted) unchanged; locals under 256 B (the 215-byte tables live in the backend object, not on the stack); PSRAM planes allocated with the backend; C3 `default` and `sticky` build with the lab compiled out.

**Never:** an OTP/MTP command; an edit under `freeink-sdk/`, `.agents/`, the epic file, or the dlab-0 screens (`FramePeriodActivity`, `PolarityActivity`, `DisplayLabActivity`); a change to the SSD1677 backend or its bytes; the UC8179 case in `factoryFor` (lane A); new i18n keys; an upload cache across scans (withdrawn by D8).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| All-hold scan | both masks clear, frames n ≤ 252, rate i | old == new == 0xFF everywhere; all five tables carry n frames split per D7, VCOM/WW/KK at GND, prefixes 0x20-0x24; calls: upload, PLL `{0x0E,0x0D,0x0F}[i]`, labScan, labEnd; Done with labScan's busyUs | — |
| Polarity scan | whiteMask all set (row 1) / blackMask all set (row 2) | row 1: old 0x00, new 0xFF; row 2: old 0xFF, new 0x00; KW = VSL, WK = VSH unswapped, exchanged when swapPolarity | — |
| Frame split (D7) | frames 1, 10, 63, 64, 100, 252 | phases fill 63 at a time in order (64 = 63+1, 100 = 63+37, 252 = 4×63), unused phases 0x00 | frames 253-255: builder false, scan Refused, no lab call |
| Bad request | rate ≥ 3, or a null mask | Refused, busyUs 0 | no lab call |
| Driver refuses | `labSupported()` false | Refused | no other lab call |
| Lab call fails | upload, PLL or labScan returns false | Refused, busyUs 0, logged | labEnd still called, nothing after it |

</intent-contract>

## Code Map

- `src/activities/games/display_lab/ScanBackend.h` -- D1 interface, `ScanRequest`, `CROSSHATCH_DISPLAY_LAB` gate; read only.
- `src/activities/games/display_lab/ScanBackends.cpp` -- `factoryFor`: the `case Controller::UC8279: return nullptr;` line becomes `return &make<UC8279ScanBackend>;` plus the include; leave the UC8179 line (lane A).
- `src/activities/games/display_lab/Ssd1677ScanBackend.{h,cpp}` -- golden example: ctor allocates two planes of `display.getBufferSize()` with `HalMemory::allocatePsram`, `available()`, `LOG_ERR("DLAB", "scan refused: …")` lines, `clean()` = `renderer.displayBuffer(HalDisplay::FULL_REFRESH)`, includes (`Arduino.h`, `GfxRenderer.h`, `HalDisplay.h`, `Logging.h`).
- `src/activities/games/display_lab/DisplayLabActivity.cpp` -- wrapper already refuses inverted / bad rate before the lock, counts only Done, cleans on timeout and exit; read only.
- `lib/GameDisplayLab/Ssd1677Encoding.{h,cpp}`, `LabExperiments.h` (`FRAME_PERIOD_TPS {1,10,100}`, `POLARITY_TP 10`, `POLARITY_ROW_MASKS`), `MaskPlanes.h` -- patterns and shared inputs; read only.
- `test/game_display_lab/CMakeLists.txt` -- add the new test file and `UC8279ScanLut.cpp` to `test_scan_lut`; `test_ssd1677_encoding.cpp` is the style example.
- SDK (read only): `freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h` lines 301-316 (`labSupported() const`, `labUploadWaveform(const uint8_t*, size_t)`, `labSetFrameRate(uint8_t)`, `labScan(const uint8_t*, const uint8_t*, uint32_t&)`, `labEnd()`); `src/driver/Uc8279X4Driver.cpp` ~828-925 (`labTablesValid`: len 215, `bytes[t*43] == 0x20+t`; 42 bytes padded to 49; `labScan` false until an upload this session; `labEnd` restores PSR/CDI/PLL/TSSET).
- `docs/crosshatch/display-lab.md` -- lines 7, 31, 50, 117-118 say SSD1677 only; add the UC8279 backend and its files.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/UC8279ScanLut.{h,cpp}` -- namespace `GameDisplayLab`: `UC8279_TABLES = 5`, `UC8279_TABLE_BYTES = 43`, `UC8279_SCAN_TABLE_BYTES = 215`, `UC8279_MAX_FRAMES = 252`, `UC8279_PLL_CODES[] = {0x0E, 0x0D, 0x0F}`, `UC8279_RATE_COUNT`; `uc8279RateLabel(i)` ("0x0E", "0x0D", "0x0F", nullptr out of range); `bool uc8279PllCode(i, uint8_t& code)`; `uc8279RowLabel(row, swap)` (row 1 "KW VSL" / swapped "KW VSH", row 2 "WK VSH" / "WK VSL", else nullptr); `bool buildUc8279ScanTables(uint8_t* out, uint8_t frames, bool swap)` (false, out untouched, when frames > 252); `encodeUc8279Planes(black, white, oldPlane, newPlane, bytes)` (`old = ~white`, `new = ~black`). Pure C++, no Arduino include; comments cite D7-D9 and the sources in Design Notes.
- [x] `lib/GameDisplayLab/UC8279ScanSession.h` -- header-only `template <typename Lab> ScanOutcome runUc8279Scan(Lab& lab, const uint8_t* tables, uint8_t pllCode, const uint8_t* oldPlane, const uint8_t* newPlane, uint32_t& busyUs)` returning a small enum (`Done`, `Unsupported`, `UploadRefused`, `RateRefused`, `ScanRefused`): `busyUs = 0`; `!lab.labSupported()` → Unsupported (no other call); upload, PLL, labScan in order, the first false → `lab.labEnd()` and that outcome; else `lab.labEnd()` and Done. The backend passes `display.eink()`.
- [x] `test/game_display_lab/test_uc8279_scan_lut.cpp` + CMake -- pin every table byte for frames 0, 1, 10, 63, 64, 100, 252 both swaps; 253/255 false with out untouched; rate list, labels, row labels; the matrix's plane cases on 800×480 masks plus a both-masks pixel (0,0); every FRAME_PERIOD_TPS and POLARITY_TP builds; the session template with a `RecordingLab` fake (records call order and bytes; refuses as the seam documents: wrong length/prefix upload, labScan before an upload) covering each matrix row.
- [x] `src/activities/games/display_lab/UC8279ScanBackend.{h,cpp}` -- `final : ScanBackend`; ctor `(GfxRenderer&)` allocates `oldPlane`/`newPlane` (PSRAM, `display.getBufferSize()`), logs OOM; member `uint8_t tables[UC8279_SCAN_TABLE_BYTES]`; `available()` = both planes; `name()` "UC8279"; rates via UC8279ScanLut; `supportsWeakRow()` false; `scan()`: refuse (log, busyUs 0) on no planes, null mask, bad rate, frames > 252, then encode, `runUc8279Scan(display.eink(), …)`, log any non-Done outcome, map to `Scan::Done` / `Scan::Refused`; `clean()` = `display.eink().labEnd()` then `renderer.displayBuffer(HalDisplay::FULL_REFRESH)`.
- [x] `src/activities/games/display_lab/ScanBackends.cpp` -- the UC8279 case and include.
- [x] `docs/crosshatch/display-lab.md` -- UC8279 alongside SSD1677: launcher row, row labels, rate list, per-scan session, the new files.

**Acceptance Criteria:**
- Given a UC8279 X4 Pro, when Games opens, then the Display lab row shows; the frame-period screen lists nine scans labelled `tp=… fr=0x0E/0x0D/0x0F`, and the polarity screen labels rows "Row 1 (KW VSL)" and "Row 2 (WK VSH)".
- Given any lab exit or cap/timeout clean on a UC8279, when `clean()` runs, then `labEnd` precedes the FULL refresh.
- Given the `default` and `sticky` envs, when built, then no display_lab or UC8279 lab code compiles in.

## Implementation Notes

- 2026-10-16, build: all six Execution items done. New: `lib/GameDisplayLab/UC8279ScanLut.{h,cpp}`,
  `lib/GameDisplayLab/UC8279ScanSession.h`, `src/activities/games/display_lab/UC8279ScanBackend.{h,cpp}`,
  `test/game_display_lab/test_uc8279_scan_lut.cpp` (20 tests); edited `ScanBackends.cpp` (UC8279 case + include;
  UC8179 line untouched), `test/game_display_lab/CMakeLists.txt`, `docs/crosshatch/display-lab.md` (new "The UC8279
  backend" section, launcher/rows/rates/code map, safety paragraph split per controller).
- Additions beyond the Code Map, kept small: `UC8279_PHASE_MAX_FRAMES` (63) and `UC8279_PHASES` (4), from which
  `UC8279_MAX_FRAMES` is derived; `scanOutcomeName(ScanOutcome)` in the session header for the backend's one
  `LOG_ERR("DLAB", "scan refused: UC8279 %s", …)` line; host-tested.
- `runUc8279Scan` zeroes `busyUs` again on a labScan refusal (the fake and the driver already do), so a refused scan
  can never carry a time; the backend also zeroes it on any non-Done outcome.
- The backend's `scan()` checks rate before frames (both before any lab call), matching the matrix's "Bad request"
  and "frames 253-255" rows; these refusals and `clean()`'s labEnd-then-FULL order live in firmware-only code and are
  not host-tested (the pure parts they call are: `uc8279PllCode` false at 3+, `buildUc8279ScanTables` false at 253+).
- `Arduino.h` is not included by the backend (no `micros()`: the driver times the scan); the other golden-example
  includes are.
- Verification run here: full host suite (2,126 tests, all pass, the 20 new ones included); every `scripts/*_test.py`
  passes; `scripts/check_layers.py` passes; `check_upstream_touches.py` on a dangling commit of the working tree
  flags only `.gitmodules` and the `freeink-sdk` pointer (the orchestrator's, as Design Notes expect);
  `./bin/clang-format-fix` reformatted the new test file once, second run clean. Not run (after review): the
  firmware builds, `pio check`, the flash budget gate.
- 2026-10-16, verify (parent session): the matrix audit found the "Bad request" row (null mask, rate 3+, frames
  253+: no lab call) covered only by firmware code. Moved the backend's request checks, the table build and the plane
  encoding into `runUc8279Request<Lab, Request>` in `UC8279ScanSession.h` (outcomes `NoMasks`, `BadRate`,
  `TooManyFrames`); `scan()` keeps only the planes check and one log line. Two tests added (`Uc8279Request.*`); host
  suite 2,128 of 2,128 pass; `check_upstream_touches.py` flags only `.gitmodules` and the `freeink-sdk` pointer.

## Plan Change Log

- 2026-10-16, first pass: halted at planning (intent gap G1 frames > 63, G2 session lifetime, G3 hold-table timing; commit `536a61b4`). The orchestrator answered with epic decisions D7, D8, D9 (below); the intent contract now carries them and drops the upload cache. KEEP: the settled layout, level, PLL and plane choices in Design Notes.

## Review Triage Log

### 2026-10-16 — Review pass
- verdicts: 23 findings — high 0, medium 4, low 18, false 1, maybe-false 0
- findings:
  - `[low]` `[reject]` BH: the gate never asks `labSupported()` (story's "Backend choice" error row) — unreachable: the lab needs `FREEINK_CAP_DISPLAY_LAB`, which compiles the seam, and an X4 Pro UC8279 resolves to `uc8279X4Driver()` (`FreeInkDisplay.cpp:210`), whose `labSupported()` is true; making `available()` false would show the out-of-memory notice, a worse message. Each scan still refuses (`Unsupported`) if it ever happens.
  - `[low]` `[patch]` BH: `Refused` after a successful upload breaks ScanBackend's "nothing sent" wording — both cases unreachable on an X4 Pro; documented in `UC8279ScanBackend.h`.
  - `[medium]` `[patch]` BH: busyUs window differs per controller and nothing says so — backend header and doc now state DRF-to-BUSY only, slopes comparable, intercepts not.
  - `[low]` `[patch]` BH: checklist step 6 SSD1677-only wording and log samples — UC8279 wording and `vs=KW VSL` / `vs=WK VSH` added.
  - `[low]` `[patch]` BH: "Expected ranges" has no UC8279 reading — UC8279 bullet added (intercept, polarity reading).
  - `[low]` `[patch]` BH: doc's refusal log list wrong after the `runUc8279Request` refactor — corrected to the real lines and reasons.
  - `[low]` `[patch]` BH: stale `runUc8279Scan` references in the backend header and code map — now name `runUc8279Request`.
  - `[low]` `[patch]` BH: plane-to-table mapping cites only the CDI equality — comments and doc now cite `kQualityLutReg` (same selection as D2) and the polarity screen as the hardware check.
  - `[low]` `[patch]` BH: request path tested at rate 0 and POLARITY_TP only — `Uc8279Request.FramePeriodRequestsSendEachRatesPllAndTables` covers 3 rates × FRAME_PERIOD_TPS.
  - `[low]` `[patch]` BH: `const_cast` in the fake, hard-coded "frames over 252", ragged prose — `mutable calls`, `static_assert(UC8279_MAX_FRAMES == 252)`, reflowed doc and CMake comment.
  - `[medium]` `[patch]` EC: a hung panel stalls through five untimed SDK BUSY waits — the 15 s timeout still fires (the DRF wait is timed, so a stuck BUSY yields busyUs ≈ 30 s); the doc's "up to 30 s per scan" was false for the UC8279 and now states about 150 s.
  - `[low]` `[patch]` EC: post-upload refusals reported as Refused — same root cause as BH's contract finding; documented.
  - `[medium]` `[patch]` EC: doc claims 30 s worst case — grouped with the stall finding; corrected.
  - `[medium]` `[defer]` VG: `factoryFor`'s UC8279 case is pinned by no test — pre-existing dlab-0 deferral already carried in deferred-work.md (controller-to-backend table); closing it restructures the switch lane A also edits; deferred.
  - `[low]` `[reject]` VG: the backend's outcome-to-`Scan` mapping is untested — a four-line `!= Done` mapping over the tested `runUc8279Request`; a harness for `display.eink()` and the backend class does not exist for the SSD1677 backend either.
  - `[low]` `[reject]` VG: `DisplayLabActivity.h:21` comment says "SSD1677 only so far" — a dlab-0 file this plan leaves untouched so the lanes merge cleanly; lane A changes the same line; named for the orchestrator under residual risks.
  - `[low]` `[reject]` IA: `labSupported()` checked per scan, not at backend choice — same as BH's first finding (unreachable).
  - `[low]` `[reject]` IA: the menu does not offer dlab-u1's stock bench or dlab-c1's transition test — neither exists on this branch; they arrive with their lanes, and D8 bars screen edits.
  - `[false]` `[reject]` IA: the host `Request` struct could drift from `ScanRequest` silently — `runUc8279Request` is instantiated with `ScanRequest` in the x4pro build, so a renamed or retyped field fails to compile.
  - `[low]` `[reject]` IA: post-upload Refused meaning — same as BH's contract finding (patched by documentation).
  - `[low]` `[reject]` IA: the story says the owner's unit is a UC8179 while hardware verification needs a UC8279 — an owner logistics question, not a code defect; the hardware step stays the owner's.
  - `[low]` `[reject]` IA: `supportsWeakRow()` false — D2 gives the UltraChip encoding two drive tables (KW, WK) and holds WW/KK; no third row exists to report.
  - `[low]` `[patch]` IA: "level 1 = VSH" is an inference — comment now cites the UltraChip level-select convention and the polarity screen as the check.

### 2026-10-16 — Review pass (follow-up)
- verdicts: 29 findings — high 0, medium 5, low 21, false 3, maybe-false 0
- findings:
  - `[medium]` `[patch]` BH: the Safety paragraph's UC8279 stall bound is false (SDK "caps each BUSY wait at 30 s", "about 150 s", "five BUSY waits", the DRF wait "enough for the 15 s timeout to fire"), and the first pass's EC-stall row rested on the same premise — `Uc8279X4Driver::busyPolarity()` is `UcIdleHigh`, whose `EpdBus::waitBusy` branch (`EpdBus.cpp:342-357`, "deliberately no fixed millisecond timeout") and `waitRefreshComplete` have no cap, and PON is waited only when the panel is off. Safety now states the SSD1677's 30 s cap and the UC8279's unbounded wait, with the 15 s timeout firing only for a DRF wait that ends after 15 s.
  - `[low]` `[patch]` BH: the "Dropped" guidance (checklist step 2, Expected ranges, Busy time) reads as covering the UC8279 — verified: `FreeInkDisplay::lab*` return false when `_inverted`, so an inversion shows as `upload refused` (Refused), and the `UcIdleHigh` wait's leading `delay(1)` keeps `busy_us` over the 500 us floor. Step 2 names the UC8279 refusal, the Dropped bullet is SSD1677-only, Busy time and the UC8279 Expected-ranges bullet name the 1 ms start tick, and Refusals says inversion shows as `upload refused`.
  - `[low]` `[reject]` BH: ticked Execution items and the first Implementation Note describe code the verify-note refactor replaced — the fix edits this build's plan; the later Implementation Note records the refactor.
  - `[low]` `[reject]` BH: the first pass's Auto Run Result tallies read "medium 2, low 11" where the log holds 3 medium and 10 low patches — the fix edits this build's plan; this pass's Auto Run Result states the corrected first-pass figures.
  - `[low]` `[reject]` BH: the post-upload-Refused root cause is `[patch]` for BH/EC and `[reject]` for IA in the first-pass log — the fix edits this build's plan; the IA row's own evidence says it was patched by documentation.
  - `[low]` `[patch]` BH: the `kQualityLutReg` evidence ("the same selection as this one") overreaches — the quality bank streams inverted gray planes (`Uc8279X4Driver.cpp:658-663`), not B/W old/new planes. The `UC8279ScanLut.h` comment and the doc's Planes bullet now say it is no direct evidence and leave the CDI equality and the polarity screen as the grounds.
  - `[low]` `[reject]` BH: the frontmatter `context:` lacks the epic file and `warnings: [oversized]` is unexplained — the fix edits this build's plan.
  - `[low]` `[reject]` BH: residual risks miss stale contracts in `ScanBackend.h`, `FramePeriodActivity.h:14`, `PolarityActivity.h:15`, `LabExperiments.h:26` — carried for `ScanBackend.h`'s "nothing sent" (the first pass's BH contract row, documented in `UC8279ScanBackend.h`) and `DisplayLabActivity.h:21`; the other three name the SSD1677 as qualified examples ("on the SSD1677 …") that stay true, and the screens are on the intent's Never list.
  - `[false]` `[reject]` BH: the Safety text omits that the lab leaves panel power on (no POF) — the driver keeps analog power on between ordinary refreshes too (`displayFinish` and the AA path send POF only on an explicit turn-off, `Uc8279X4Driver.cpp:523-527, 687-692`), so the lab adds no exposure the paragraph should name.
  - `[low]` `[reject]` BH: `runUc8279Request`/`runUc8279Scan`/`buildUc8279ScanTables` take `uint8_t*` with an implicit 215-byte length — both callers pass 215-byte buffers (the backend's member array, the tests' `std::array`); changing the signatures adds surface for no observed harm.
  - `[low]` `[reject]` BH: three host cases missing (rate 3 with frames 253, inversion refusal in the fake, labSupported-false through the request path) — the first only changes the logged reason (both Refused, no lab call, each refusal pinned alone); inversion is refused by the wrapper and the facade, so the fake would restate a boolean; Unsupported writes only backend-owned buffers.
  - `[low]` `[patch]` BH: the `UC8279` case in `ScanBackends.cpp` sits under the `// Not X4 Pro controllers.` group — a `// The X4 Pro's UC8279 batch.` comment now heads it; the case does not move, so lane A's UC8179 line stays untouched.
  - `[low]` `[patch]` BH: the Refusals bullet lists `scan refused: no UC8279 planes` as a line a tester can see — `DisplayLabActivity::openSession` rejects a backend whose `available()` is false (`DisplayLabActivity.cpp:74`), so the lab shows its out-of-memory notice first; the bullet now calls the line a defensive guard.
  - `[low]` `[defer]` EC: a UC8279 BUSY that never releases hangs `scan()` with no bound, so the wrapper's timeout and clean never run — real, but the unbounded `UcIdleHigh` wait is the SDK's by design and governs every ordinary UC8279 refresh; a bound belongs in `freeink-sdk` (barred here). Deferred; the doc now states it.
  - `[medium]` `[patch]` EC: the doc's "SDK caps each BUSY wait at 30 s … ~150 s … the 15 s timeout fires" is false for the UC8279 — same root cause as BH's Safety finding; patched there.
  - `[medium]` `[patch]` VG: the rewritten Safety paragraph is wrong for the UC8279 (no cap on `UcIdleHigh`; the first pass's EC row closed on a false premise) — same root cause as BH's Safety finding; patched there.
  - `[low]` `[patch]` VG: the "Dropped" explanation does not hold on a UC8279 (an inversion yields `upload refused`, Refused, never Dropped) — same root cause as BH's Dropped finding; patched there.
  - `[low]` `[reject]` IA: "Refused, busyUs 0, logged" and "Done with labScan's busyUs" live at the backend surface while the tests drive the templates — carried: the first pass's VG row "outcome-to-`Scan` mapping is untested" (a `!= Done` mapping over the tested `runUc8279Request`).
  - `[low]` `[reject]` IA: `clean()`'s labEnd-before-FULL order has no test — each scan ends its own session and `Uc8279X4Driver::labEnd` returns early when `!_labActive`, so the order is not observable today; a harness for the backend class does not exist for either controller.
  - `[medium]` `[defer]` IA: the launcher row on UC8279 units rests on an untested `factoryFor` case — carried: the first pass's VG `factoryFor` row, already in `deferred`.
  - `[low]` `[reject]` IA: `RecordingLab`'s fidelity is checked only by a test of the fake — the fake's refusals were read against `Uc8279X4Driver.cpp:834-904` (the VG lens confirmed they match); the SDK driver needs its Arduino bus, which the host suite does not build.
  - `[low]` `[patch]` IA: KW = VSL / WK = VSH are pinned as level bits, not rails — carried: the first pass's IA "level 1 = VSH is an inference" row (comment cites the UltraChip convention; the polarity screen is the check).
  - `[low]` `[reject]` IA: polarity row 2 is driven through `runUc8279Request` only by row 1's test — row 2 differs only in the mask passed; `Uc8279Planes.PolarityRowsSelectKwThenWk` pins both rows' planes and the request path forwards masks unchanged.
  - `[low]` `[reject]` IA: no backend-level check of the frames 253-255 refusal — `runUc8279Request` pins `TooManyFrames` with no lab call for 253 and 255, and the backend forwards the request unchanged.
  - `[medium]` `[patch]` IA: the doc adds hardware claims no intent row states, among them the "about 150 s" stall — the stall claim is BH's Safety finding, patched there; "Row 1 should lighten" is stated as the expected reading with the swap instruction if it fails, and "next repaint is a full refresh" matches `labEnd` setting `_needFullClear`.
  - `[false]` `[reject]` IA: frames 0 is accepted and would run a full session — no caller sends 0 (`FRAME_PERIOD_TPS` {1, 10, 100}, `POLARITY_TP` 10), and Design Notes record the choice (all phase bytes 0x00, as the SSD1677 builder accepts TP 0).
  - `[low]` `[reject]` IA: Refused after a successful upload breaks D1's "nothing sent" — carried: the first pass's IA post-upload row (documented in `UC8279ScanBackend.h`; unreachable on an X4 Pro).
  - `[low]` `[reject]` IA: `labSupported()` handled per scan, not at backend choice — carried: the first pass's IA row (unreachable on an X4 Pro UC8279).
  - `[false]` `[reject]` IA: `UC8279ScanSession.h` also holds the request checks, wider than "its call sequence" — the intent's purpose is a template host tests drive with a fake, and the checks there make the matrix's "Bad request" and "frames 253-255" rows host-testable; no bad outcome follows.

## Design Notes

- **D7** (orchestrator, 2026-10-16): "a UC8279 phase byte holds at most 63 frames. Spread `frames` over the single group's four phases at the same level (100 = 63 + 37; ceiling 252); above 252 return `Scan::Refused`. Host-test the split (1, 10, 63, 64, 100, 252, 253)."
- **D8**: "`scan()` is a self-contained session every call: upload tables, set PLL, `labScan`, then `labEnd`. The story's 'upload once, then not again until a change' is withdrawn; no D1 change, no end-of-run hook, no edits to the dlab-0 screens. The matrix's upload row becomes 'before every scan'." Why: the seam requires `labEnd()` before any other display call and an ordinary refresh drops the waveform (`freeink-sdk/docs/display-lab.md`), while the screens repaint between runs.
- **D9**: "VCOM, WW and KK carry the same frame counts as KW and WK (group 0, split per D7) at the GND level; D2's 'all zero' meant level, not length."
- Group layout: one 7-byte group `[0x01, P1, P2, P3, P4, 0x01, 0x01]`, `Pn = level << 6 | frames` (6-bit), then 35 zero bytes, as `makeQualityBank` writes groups (hardware-validated per its comment) and as every stock group in `kXtfPreBwMid` / `kUc8279X3_BwGc` reads; level 0 GND, 1 VSH, 2 VSL (`makeQualityBank`'s net-charge sign). "Repeat 1" is that framing.
- Example, frames 100, unswapped: KW `0x22, 0x01, 0xBF, 0xA5, 0x00, 0x00, 0x01, 0x01`, then zeros to 43 bytes; WK `0x23, 0x01, 0x7F, 0x65, 0x00, 0x00, 0x01, 0x01, …`; WW `0x21, 0x01, 0x3F, 0x25, 0x00, 0x00, 0x01, 0x01, …`. Frames 0: all four phase bytes 0x00 (as the SSD1677 builder accepts TP 0; no caller sends it).
- Levels: unswapped KW (0x22, drive-to-white, row 1, white mask) = VSL, WK (0x23, row 2, black mask) = VSH: the direction of the stock GC tables (`kUc8279X3_BwGc` KW `0x9A`, WK `0x5A`) and pre-BW tables; swapped exchanges them. Row labels follow D1a ("KW table" example).
- PLL list: no UC8279 PLL code table is documented in the tree, the SDK, or the research run, so the story's fallback applies: three codes bracketing the driver's `pll = 0x0E` (`uc8279X4DefaultConfig`), the stock value first as in the SSD1677 list, so the polarity screen's rate 0 is stock: {0x0E, 0x0D, 0x0F}. 0x0F is also Ink-boy's and the X3 init's code (research [7]).
- Planes (D2; framebuffer 1 = white; the lab's CDI `cdiAa` equals the B/W refresh's `cdiBwFull`, 0x97, so plane bits mean what they mean in a B/W refresh): hold (1,1) selects WW, drive-to-white (0,1), drive-to-black (1,0); a pixel in both masks (undefined by D1) gives (0,0), KK, which holds at GND too.
- X4 Classic refuses `labSetFrameRate`; unreachable, since `DisplayLabActivity::available()` requires `BoardConfig::isX4Pro()`; the session still maps it to Refused with `labEnd`.
- `RecordingLab` stands in for `FreeInkDisplay`'s lab API as `docs/display-lab.md` and `Uc8279X4Driver` define it: it refuses an upload whose length is not 215 or whose prefixes are not 0x20-0x24, and a `labScan` before an upload since the last `labEnd`. More permissive than the device: it never refuses for inversion (the wrapper checks that) and never times BUSY.
- `.gitmodules` and the `freeink-sdk` pointer differ from `develop` on this branch: the orchestrator's touch (epic Decision 2026-10-16, the experimental branch), not this story's; `check_upstream_touches.py` flagging them is expected.
- Flash gate: on this branch `check_flash_budget.py` still builds the lab into its "on" image (D6 not applied here), so the backend counts against the games budget; the delta is `pio run -e x4pro` firmware.bin at the baseline commit and at this story's commit, same cache, same lock.

## Verification

**Commands:** (host-test lock: `flock /tmp/crosshatch-hosttest.lock sh -c '…'`; build lock: `flock /tmp/crosshatch-build.lock sh -c 'export PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache; …'`)
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, the new suite included.
- `python3 scripts/check_upstream_touches.py` -- expected: only `.gitmodules` / `freeink-sdk` (orchestrator's).
- `./bin/clang-format-fix` twice -- expected: no change on the second run.
- After review: `pio run -e x4pro`, `pio run -e default`, `pio run -e sticky`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`; `scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects` -- expected: all succeed; record the x4pro firmware.bin delta against the baseline build.

**Manual checks:** hardware (owner): the nine-line frame-period table and the polarity answer on the UC8279 unit.

## Auto Run Result

**Status: built.** `UC8279ScanBackend` gives the X4 Pro's UC8279 batch the display lab's two scan screens over the SDK
seam: one self-contained lab session per scan (D8), five 43-byte tables with frames split over the group's four phases
(D7) and VCOM/WW/KK at GND for the same length (D9), PLL list {0x0E, 0x0D, 0x0F}, planes `old = ~white`,
`new = ~black` (D2). The first pass halted on G1-G3; D7-D9 answered them.

**Files**
- `lib/GameDisplayLab/UC8279ScanLut.{h,cpp}`: tables, PLL list and labels, row labels, plane encoding.
- `lib/GameDisplayLab/UC8279ScanSession.h`: `runUc8279Request` (request checks, tables, planes) and `runUc8279Scan`
  (labSupported, upload, PLL, labScan, labEnd on every path), templated for the host fake.
- `src/activities/games/display_lab/UC8279ScanBackend.{h,cpp}`: the backend (PSRAM planes, member table buffer,
  `clean()` = labEnd then FULL refresh).
- `src/activities/games/display_lab/ScanBackends.cpp`: `UC8279` → `UC8279ScanBackend` (UC8179 line untouched).
- `test/game_display_lab/test_uc8279_scan_lut.cpp` (23 tests) and `CMakeLists.txt`.
- `docs/crosshatch/display-lab.md`: the UC8279 backend, checklist, expected ranges, safety, code map.

**Review** (four lenses, 23 findings): 13 patched (documentation of the busy-time window, the UC8279 worst-case stall,
the post-upload Refused exception, checklist and log lines, plane/level evidence; a rate-by-TP request test; fake and
static_assert tidy-ups); 1 deferred (factoryFor untested, the carried dlab-0 item); 9 rejected with reasons in the
Review Triage Log (unreachable labSupported-false gate, dlab-u1/c1 screens not on this branch, `Request` drift caught
by the x4pro compile, outcome mapping too thin to harness, owner's unit, weak row, stale `DisplayLabActivity.h:21`
comment left for the merge). Patched entries by verdict: medium 2, low 11. Follow-up review recommended: the
rewritten doc text (Safety, Busy time, Expected ranges, including "Row 1 should lighten") was patched after the
lenses read the diff and no lens has read it.

**Verification**
- Host suite (Ninja, hosttest lock): 2,129 of 2,129 pass, 23 `Uc8279*` among them.
- `check_upstream_touches.py`: FAIL on `.gitmodules` and the `freeink-sdk` pointer only, the orchestrator's touch on
  this experimental branch; every file this story touches is fork-only.
- `./bin/clang-format-fix`: second run clean; no formatting change outside this story's paths.
- Build lock, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `-e default`,
  `-e sticky` succeed; `pio check -e x4pro` (low/medium/high) "No defects found" (the first attempt failed with
  `MissingPackageManifestError` while the shared framework package was being rewritten, 07:51; the retry passed).
- Flash gate (`build on`/`off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`): within budget, 6,016 B
  flash and 240 B static RAM to spare; objects "No problems". On this branch the gate still builds the lab into its
  "on" image (D6 not applied here).
- Size delta, same method at both ends (`pio run -e x4pro`, shared cache, build lock): firmware.bin 6,227,904 B at
  baseline `536a61b4` → 6,229,376 B with this story's sources = +1,472 B; static RAM 104,256 B both (pio's RAM line),
  gate RAM delta +784 B at both.
- Hardware: none here; the owner's nine-line frame-period table and polarity answer on a UC8279 unit remain.

**Residual risks**
- Group framing bytes (`0x01 … 0x01, 0x01`), level names and the KW/WK roles rest on the driver's stock tables, not a
  datasheet; the polarity screen is the check.
- The PLL codes 0x0D and 0x0F are a bracket, not documented frequencies.
- `DisplayLabActivity.h:21` still says "SSD1677 only so far"; lane A edits the same line, so the orchestrator fixes it
  at merge.
- `factoryFor` stays untested (deferred).

### Follow-up review pass (2026-10-16)

**Status: built.** A fresh four-lens pass (blind hunter, edge-case hunter, verification gap, intent alignment) over
`git diff 536a61b4..4bc3054e`, the plan as intent, as the orchestrator asked because the first pass patched doc text
no lens had read. It found that patched text wrong in two places, and fixed those plus three smaller ones; no code
behaviour changed (one comment line in `ScanBackends.cpp`, one header comment).

**Files**
- `docs/crosshatch/display-lab.md`: Safety now says the UC8279's BUSY waits have no time limit (SDK `UcIdleHigh`, by
  design), so a hung panel stalls without bound and the 15 s timeout fires only for a DRF wait that ends; the
  "about 150 s" figure and the five-wait list are gone. "Dropped" is SSD1677-only: on a UC8279 an inversion shows as
  `upload refused` and the 1 ms start tick keeps `busy_us` over the 500 us floor (checklist step 2, Busy time,
  Refusals, Expected ranges). The `kQualityLutReg` evidence is narrowed, and `no UC8279 planes` is called a defensive
  guard.
- `lib/GameDisplayLab/UC8279ScanLut.h`: the plane comment no longer claims the quality bank makes the same selection.
- `src/activities/games/display_lab/ScanBackends.cpp`: a `// The X4 Pro's UC8279 batch.` comment heads the UC8279
  case, which sat under `// Not X4 Pro controllers.`; the UC8179 line (lane A) is untouched.

**Review** (29 findings: high 0, medium 5, low 21, false 3): 5 entries patched (the Safety stall claim, medium,
raised by BH, EC, VG and IA; the UC8279 "Dropped" guidance, low, BH and VG; the `kQualityLutReg` overreach, low; the
`factoryFor` comment group, low; the unreachable planes refusal line, low). Patched entries by verdict: medium 1,
low 4. 1 deferred: the SDK's unbounded UC8279 BUSY wait (`EpdBus.cpp:342`, low; the fix belongs upstream in
freeink-sdk). Rejected, reasons in the Review Triage Log: five plan-only fixes (stale Execution items, first-pass
tallies, a duplicate's disposition, frontmatter `context`/`warnings`; for the record, the first pass patched 3 medium
and 10 low entries, not "medium 2, low 11"); stale contracts in other files (carried or qualified SSD1677 examples);
POF omission (false: ordinary refreshes keep power on too); implicit 215-byte buffer length; three extra host cases;
backend-surface glue untested (carried); `clean()` order unobservable; fake fidelity; row 2 and frames 253 at the
request level; frames 0 (false); the session header's scope (false); post-upload Refused and per-scan labSupported
(carried). Follow-up review recommended: false (no high patched on this follow-up pass).

**Verification** (on the patched tree before the commit)
- Host suite (Ninja, hosttest lock): 2,129 of 2,129 pass.
- `check_upstream_touches.py`: FAIL on `.gitmodules` and the `freeink-sdk` pointer only (the orchestrator's touch on
  this experimental branch); every file this pass touches is fork-only.
- `./bin/clang-format-fix`: two runs, nothing new in `git status`; no formatting change outside this pass's paths.
- Build lock, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, from an emptied `.pio`:
  `check_flash_budget.py build on` (the `x4pro` build) and `build off` succeed; `compare --limit-kib 270
  --ram-limit-bytes 1024` within budget (flash +270,464 B of 276,480; static RAM +784 B of 1,024); `objects` "No
  problems"; `pio run -e default` and `-e sticky` succeed; `pio check -e x4pro` (low/medium/high) "No defects found".
  x4pro `firmware.bin` 6,229,376 B; this pass changes only comments, so no size delta is claimed.

**Residual risks**
- A hung UC8279 panel hangs the lab, as it hangs any UC8279 refresh (deferred to the SDK).
- Unchanged from the first pass: group framing, level names and KW/WK roles rest on stock tables, not a datasheet
  (the polarity screen is the check); PLL codes 0x0D/0x0F are a bracket; `DisplayLabActivity.h:21` still says
  "SSD1677 only so far" (lane A edits the same line); `ScanBackend.h`'s "Refused, with nothing sent" has the UC8279
  post-upload exception documented only in `UC8279ScanBackend.h`; `factoryFor` stays untested (deferred).

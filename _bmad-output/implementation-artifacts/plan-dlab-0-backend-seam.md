---
title: 'dlab-0: the scan backend seam'
type: 'refactor'
ticket: 'story-dlab-0-backend-seam'
created: '2026-10-16'
status: 'built'
baseline_revision: '38dea8188f75dddae5e61c3cd939914d29929675'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-0-backend-seam.md'
warnings: ['oversized']
deferred:
  - summary: >-
      Nothing checks the argument order of what Ssd1677ScanBackend::scan sends (encodeSsd1677Planes(black, white, bw, red), copyGrayscaleBuffers(bw, red)).
    evidence: |-
      The host tests pin the pure encoder and LUT builder against Part A, not the device glue; swapping the two mask or plane arguments compiles and passes every test. Closing it needs a fake EInkDisplay host harness or a run on an SSD1677 unit (the owner has a UC8179).
    location: >-
      src/activities/games/display_lab/Ssd1677ScanBackend.cpp
    severity: medium
  - summary: >-
      The lab wrapper's guard order (rate refusal before the cap clean, count only a Done scan) has no automated check.
    evidence: |-
      DisplayLabActivity::scan compiles only in x4pro and has no host harness; ScanBudget alone is tested. Moving the rate check below cleanDue() or dropping the non-Done early return would pass every test.
    location: >-
      src/activities/games/display_lab/DisplayLabActivity.cpp
    severity: low
  - summary: >-
      D1 has no hook for a backend to name its drive rows, so the polarity labels and the vs= log name stay SSD1677 facts (POLARITY_ROW1/2, vsName) on every backend.
    evidence: |-
      PolarityActivity labels rows from Ssd1677ScanLut's codes; ScanBackend (D1, fixed shape) has no row-name member. Lanes dlab-a1 and dlab-b1 need an orchestrator decision (a D1 addition) before the polarity screen can label UltraChip rows truthfully.
    location: >-
      src/activities/games/display_lab/PolarityActivity.cpp
    severity: low
  - summary: >-
      The controller-to-backend table (factoryFor in ScanBackends.cpp) has no test; a lane mapping UC8179 or UC8279 to the wrong backend would pass every test.
    evidence: |-
      ScanBackends.cpp compiles only in x4pro with no host harness; nothing references hasScanBackend or makeScanBackend under test/. A wrong case would show the lab row on an UltraChip unit and send SSD1677 LUT and register writes to it. Lanes dlab-a1/b1 edit exactly these cases; a pure controller-to-backend-kind mapping in lib/GameDisplayLab would let a host test pin it.
    location: >-
      src/activities/games/display_lab/ScanBackends.cpp
    severity: medium
  - summary: >-
      ScanBackend::rowLabel(row) const (Amendment D1a) has no swap argument, so a caller sending swapPolarity = true would log the unswapped code.
    evidence: |-
      D1a's text mentions the swapped codes but fixes the signature without swap; Ssd1677ScanBackend::rowLabel returns ssd1677RowLabel(row, false). No caller swaps in this story (the polarity screen sends false); Part B's engine may, and needs an orchestrator decision (a swap argument, or labels documented as unswapped).
    location: >-
      src/activities/games/display_lab/ScanBackend.h
    severity: low
---

<intent-contract>

## Intent

**Problem:** Part A's scan primitive is `DisplayLabActivity::scan(lut, bwFill, redFill, busyUs)`, which knows only the SSD1677 facade path and a uniform fill byte per plane; the UC8179/UC8279 benches and the gray engine need one primitive with per-pixel masks and a controller-specific encoding, built by three parallel lanes.

**Approach:** Add the epic's D1 `ScanBackend`/`ScanRequest`, move the SSD1677 path into `Ssd1677ScanBackend` with its pure encoding in `lib/GameDisplayLab/Ssd1677Encoding`, add a pure `MaskPlanes` helper, and make the lab choose the backend by controller at `onEnter` and wrap `backend->scan()` with the existing controller-neutral `ScanBudget` rules. SSD1677 behaviour is byte-for-byte unchanged.

## Boundaries & Constraints

**Always:** `ScanBackend`/`ScanRequest` exactly as epic D1 (any change to their shape is an intent gap: HALT); every pure part host-tested; `test_scan_lut` and `test_scan_budget` keep passing; `STR_GAMES_DLAB_*` keys only; `default` and `sticky` build with the lab compiled out; PSRAM via `HalMemory::allocatePsram` in `onEnter`, freed in `onExit`; locals < 256 B; no per-scan heap allocation; `makeUniqueNoThrow` + null check; no `RenderLock` in `onExit`/destructors.

**Never:** a change to what reaches the SSD1677 panel (same 112 LUT bytes, same `displayGrayBuffer(false, lut, true)` so 0x22 = 0xCC, same plane bytes); UltraChip code; a new ledger row or any upstream-file edit; an edit to `freeink-sdk/`, `.agents/`, or `english.yaml` beyond existing `STR_GAMES_DLAB_*` keys (none needed).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Backend choice | controller SSD1677 | `Ssd1677ScanBackend`; launcher row shown | — |
| Backend choice | UC8179 / UC8279 | no backend; row hidden; lab shows `STR_GAMES_DLAB_NOT_SSD1677` notice | no scan sent |
| Frame-period scan | both masks clear, frames n, rate index i | planes 0x00/0x00; LUT == `buildOneFrameLut(n, FRAME_PERIOD_RATES[i], POLARITY_ROW1, POLARITY_ROW2)` | rate index ≥ rateCount → Refused, nothing sent |
| Polarity scan | whiteMask all set, blackMask clear | BW 0xFF, RED 0x00 (row 1); blackMask all set → BW 0x00, RED 0xFF (row 2); `swapPolarity` swaps the row codes, not planes | — |
| MaskPlanes rect | `setRect(mask, x, y, w, h)` on cleared masks | exactly those pixels set in that mask only; MSB-first, row-major, `(width+7)/8` bytes per row (100 at 800 px) | clipped to the mask; w/h ≤ 0 or fully outside: no-op |
| Timeout / cap / dropped / inverted | as Part A | identical outcomes and log lines | as Part A |

</intent-contract>

## Code Map

- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- today: gate macro `CROSSHATCH_DISPLAY_LAB`, `available()`, PSRAM `bwPlane`/`redPlane`, `scan()`, `cleanPanel()`, `onExit` clean. Guards to keep in the wrapper: no-masks Refused (`scan refused: no mask planes`), per-scan `display.isInverted()` Refused (control center can invert after onEnter), `RenderLock` around cap-clean + send, `budget.cleanDue()` clean first, `record()` → TimedOut (log + clean) / Dropped (log, no clean). `onExit` takes no lock (12cc816).
- `FramePeriodActivity.{h,cpp}` -- 9 scans via `framePeriodScan(i)`; labels `tp=%u fr=0x%02X`, log `tp=%u fr=0x%02x busy_us=%lu`.
- `PolarityActivity.{h,cpp}` -- row i drives `planeFillFor(Row1/Row2)` with TP=`POLARITY_TP`, rate `FRAME_PERIOD_RATES[0]`; labels/log name `vsName(POLARITY_ROW1/2)`.
- `lib/GameDisplayLab/Ssd1677ScanLut.{h,cpp}`, `ScanBudget.h` -- keep unchanged; reuse `buildOneFrameLut`, `FRAME_PERIOD_TPS/RATES`, `POLARITY_*`, `vsName`.
- `test/game_display_lab/CMakeLists.txt` -- target `test_scan_lut`; add sources there.
- `src/activities/games/GamesLauncherActivity.{h,cpp}` -- calls `DisplayLabActivity::available()`; includes `display_lab/DisplayLabActivity.h` for the macro. No edit needed.
- `lib/hal/HalDisplay.h` -- `display.getController()`, `isInverted()`, `getBufferSize()`, `getDisplayWidth/Height/WidthBytes()`, `eink()` (`copyGrayscaleBuffers(const uint8_t* lsb, const uint8_t* msb)`, `displayGrayBuffer(bool, const unsigned char*, bool)`).
- `docs/crosshatch/display-lab.md` -- "Code map" and "Where Part B picks up" name `DisplayLabActivity::scan`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/MaskPlanes.{h,cpp}` -- pure non-owning view over two caller buffers (`Mask::Black`, `Mask::White`): default ctor (`valid()` false), ctor `(black, white, width, height)`, accessors, `clear()` (both zero), `fill(mask)`, `setRect(mask, x, y, w, h)` (int args, clipped), `setPixel(mask, x, y)` (out of range ignored), `test(mask, x, y)` -- D1 mask format.
- [x] `lib/GameDisplayLab/Ssd1677Encoding.{h,cpp}` -- `SSD1677_RATE_COUNT`, `ssd1677RateLabel(i)` ("0x22","0x11","0x44"; nullptr out of range), `ssd1677RowCodes(swap)`, `bool buildSsd1677ScanLut(out, frames, rateIndex, swap)` (delegates to `buildOneFrameLut`; false and out untouched when out of range), `encodeSsd1677Planes(black, white, bw, red, bytes)` (white→BW, black→RED, D2) -- pure SSD1677 parts.
- [x] `src/activities/games/display_lab/ScanBackend.h` -- move the gate macro block here from `DisplayLabActivity.h`; D1 abstract class (virtual dtor, `enum class Scan {Done, TimedOut, Dropped, Refused}` nested) and `ScanRequest`.
- [x] `src/activities/games/display_lab/Ssd1677ScanBackend.{h,cpp}` -- ctor `(GfxRenderer&)` allocates two PSRAM planes of `display.getBufferSize()`; `available()` = both planes; `scan()` refuses on missing planes/masks/bad rate, else builds LUT (stack, 112 B), encodes, `copyGrayscaleBuffers(bw, red)`, times `displayGrayBuffer(false, lut, true)`, returns Done; `clean()` = `renderer.displayBuffer(FULL_REFRESH)`; `supportsWeakRow()` false.
- [x] `src/activities/games/display_lab/ScanBackends.{h,cpp}` -- `hasScanBackend(controller)` and `makeScanBackend(controller, renderer)` (one switch case per backend; later lanes add one line each).
- [x] `DisplayLabActivity.{h,cpp}` -- `available()` = X4 Pro && `hasScanBackend`; onEnter allocates two PSRAM mask buffers + `MaskPlanes` view + backend (OOM notice when any fails or `!backend->available()`); `scan(const ScanRequest&, uint32_t&)` wrapper keeping every guard above; `cleanPanel()` → `backend->clean()` + `budget.cleaned()`; `masks()`, `backend()` accessors; `using Scan = ScanBackend::Scan`; onExit frees backend and masks after the clean.
- [x] `FramePeriodActivity.{h,cpp}` -- scans = `FRAME_PERIOD_TPS` × `min(rateCount, MAX_RATES=16)` (tp outer, rate inner); arrays sized for the max; clears masks once per run; labels `tp=%u fr=%s` and log `tp=%u fr=%s busy_us=%lu` with `rateLabel` (identical text on SSD1677).
- [x] `PolarityActivity.{h,cpp}` -- row 1 fills White, row 2 fills Black (after `clear()`), request `{frames=POLARITY_TP, rate=0, swapPolarity=false}`; labels/log keep `vsName(POLARITY_ROW1/2)`.
- [x] `test/game_display_lab/test_mask_planes.cpp`, `test_ssd1677_encoding.cpp`, `CMakeLists.txt` -- add to `test_scan_lut`; cover the matrix rows (rect bytes incl. clipping and partial bytes, pixel bit order, Part A byte equality for all 9 frame-period scans and both polarity rows, swap, bad rate index).
- [x] `docs/crosshatch/display-lab.md` -- code map and "Where Part B picks up" name the seam.

- [x] Follow-up pass, Amendment D1a (story file, orchestrator 2026-10-16): `src/activities/games/display_lab/ScanBackend.h` -- add `virtual const char* rowLabel(uint8_t row) const = 0;` (rows 1 and 2; nullptr otherwise): short text naming what that row drives on this controller.
- [x] D1a: `lib/GameDisplayLab/Ssd1677Encoding.{h,cpp}` -- pure `ssd1677RowLabel(row, swap)` returning static strings "LUT1 VSL"/"LUT2 VSH1" (swap: "LUT1 VSH1"/"LUT2 VSL"), nullptr for other rows; `Ssd1677ScanBackend::rowLabel(row)` returns `ssd1677RowLabel(row, false)` (the signature has no swap: the label names the unswapped drive, a swapped request drives the other row's code).
- [x] D1a: `PolarityActivity.{h,cpp}` -- labels from `lab.backend()->rowLabel(i + 1)` ("?" when null), log `polarity row=%u vs=%s ms=%u` with the same label; drop `VS_CODES`/`vsName`.
- [x] D1a: `lib/I18n/translations/english.yaml` -- no SSD1677-specific user string outside the backend: `STR_GAMES_DLAB_ROW1/ROW2` become "Row 1 (%s)"/"Row 2 (%s)", `STR_GAMES_DLAB_ROW_DESC` "Panel waveform bench", and `STR_GAMES_DLAB_NOT_SSD1677` is renamed `STR_GAMES_DLAB_NOT_SUPPORTED` ("Needs an X4 Pro whose panel the lab supports", one use in `DisplayLabActivity.cpp`).
- [x] D1a: `test/game_display_lab/test_ssd1677_encoding.cpp` -- pin `ssd1677RowLabel` against `"LUT1 " + vsName(ssd1677RowCodes(swap).row1)` and `"LUT2 " + ...row2`, both swaps, and nullptr for rows 0 and 3; `docs/crosshatch/display-lab.md` -- the checklist's labels and `vs=` log lines, and the seam paragraph (rows are now named by the backend).

**Acceptance Criteria:**
- Given an SSD1677 X4 Pro, when either screen runs, then the LUT and plane bytes sent equal Part A's for the same scan (pinned by host tests) and the log lines are textually identical.
- Given a UC8179/UC8279 X4 Pro, when the launcher opens, then no Display lab row shows; and if the lab is reached, it shows the not-supported notice and sends nothing.
- Given `default` and `sticky`, when built, then no display_lab code compiles.

## Implementation Notes

- Built 2026-10-16 in `dlab-lane-0` (not committed). D1's signatures are taken literally: no `const` on the virtuals.
- The wrapper also refuses `request.rate >= backend->rateCount()` before it takes the lock, so a bad rate cannot set
  off a due cap clean ("nothing sent"); the backend keeps its own refusal (`buildSsd1677ScanLut` false).
- `openSession()` refuses (logged, out-of-memory notice) when `MaskPlanes::bytesFor(width, height)` differs from
  `display.getBufferSize()`; never on the X4 Pro (100 x 480 = 48,000), but it keeps the backend's plane copy in bounds.
- `MaskPlanes::fill` is `setRect` over the whole mask, so pad bits past a non-multiple-of-8 width stay clear (no
  difference at 800 px). Frame-period labels are 24 bytes (Part A: 16) for longer UltraChip rate labels.
- New refusal log lines: `scan refused: rate index %u of %u`, `scan refused: no SSD1677 planes`, `scan refused: request
  without masks`, `framebuffer of %u bytes is not %ux%u at 1 bpp`, `OOM: two %u byte PSRAM masks`, `OOM: scan backend`;
  the not-available line now reads `Not an X4 Pro with a scan backend for its controller; ...`. Every Part A line on
  the SSD1677 paths (scan, timeout, dropped, cap, exit, polarity, tp/fr) is textually unchanged.

- Review patch round (2026-10-16): the frame-period scan order (`framePeriodTp`/`framePeriodRate`) and the polarity
  row-to-mask table (`POLARITY_ROWS`, `POLARITY_ROW_MASKS`) moved into the header-only
  `lib/GameDisplayLab/LabExperiments.h`, which both screens call and the tests pin against Part A's `framePeriodScan`
  and `planeFillFor`; `ScanBackends.cpp` derives `hasScanBackend` and `makeScanBackend` from one file-local switch
  (`factoryFor`), with the UC8179 and UC8279 cases apart so lanes A and B edit different lines; the `ScanRequest`
  comment says D1 leaves a pixel in both masks undefined (SSD1677 holds it); the doc narrows the seam claim.
- Matrix test audit: Frame-period, Polarity (incl. swap), and MaskPlanes rows are covered by `test_ssd1677_encoding`
  and `test_mask_planes` (ran, passed); timeout/cap/dropped by `test_scan_budget`. The two Backend-choice rows and the
  inverted case are device-only code (`CROSSHATCH_DISPLAY_LAB`, x4pro only) with no host harness, as in Part A; their
  evidence is the x4pro, default and sticky builds plus inspection, and the story's Verification says the SSD1677
  behaviour is pinned by the byte-level tests (Hardware: none).

- Follow-up pass dispatched 2026-10-16 with Amendment D1a (story file, commit `1e21493f`): `ScanBackend::rowLabel(uint8_t row) const`, the polarity screen labelled and logged from it, and no SSD1677-specific user string outside the backend. Read: D1a's signature has no swap argument, so `rowLabel` names the unswapped drive (the polarity screen always sends `swapPolarity = false`), and the pure `ssd1677RowLabel(row, swap)` holds both; "string" is taken as user-visible text and log lines, so the launcher description and the not-supported notice are made neutral too (frame-period TPs are numbers and stay).

- D1a pass built 2026-10-16 on `40a71c4d` (not committed): `rowLabel(uint8_t) const` on `ScanBackend`; pure
  `ssd1677RowLabel` with static strings (a `switch` on the row number, `default` nullptr), pinned against
  `"LUT1 "/"LUT2 " + vsName(ssd1677RowCodes(swap))` for both swaps; the polarity screen labels and logs through a
  `rowLabelOf(index)` helper ("?" without a backend or label), so its log line now reads `polarity row=1 vs=LUT1 VSL
  ms=...` (a deliberate D1a change to Part A's text); `STR_GAMES_DLAB_NOT_SSD1677` renamed `STR_GAMES_DLAB_NOT_SUPPORTED`
  (grep finds no other use; no other translation has the DLAB keys). `STR_GAMES_DLAB_FRAME_PERIOD_DESC` ("Nine timed
  hold scans") is left as the plan lists it untouched, though nine is the SSD1677's count (3 TPs x 3 rates).

## Plan Change Log

## Review Triage Log

### 2026-10-16 — Review pass
- verdicts: 33 findings — high 0, medium 5, low 23, false 4, maybe-false 1
- findings:
  - `[low]` `[reject]` EC: a due cap clean runs before the backend refuses a null-mask request — unreachable today (every caller passes the lab's valid masks; rate checked before the lock); the fix adds a guard for a case no caller produces.
  - `[false]` `[reject]` EC: a backend returning TimedOut/Dropped would skip the count and clean — the `ScanBackend::scan` contract and the only backend return Done or Refused; the lab alone classifies time.
  - `[low]` `[reject]` EC: `ScanRequest` carries no mask size, so a short mask buffer would be over-read — no caller builds one (lab masks are `getBufferSize()` bytes, checked in `openSession`); the fix adds a field to D1's fixed shape (intent: a change to it is an intent gap). Recorded as a residual risk.
  - `[low]` `[reject]` EC: `frames == 0` reaches the LUT as TP 0 — no caller sends 0 (1/10/100); a guard for Part B's future use is not needed now.
  - `[false]` `[reject]` EC: an out-of-range `Mask` value makes `plane()` return null — only a cast of an invalid enum reaches it; no caller does.
  - `[low]` `[reject]` EC: a framebuffer/geometry mismatch shows "Not enough memory" — cannot happen on the X4 Pro (100 x 480 = 48,000); a distinct notice needs a new key and branch.
  - `[low]` `[reject]` EC: Run reports Done with zero scans when `rateCount` is 0 — unreachable: the lab opens the screen only with a backend, SSD1677 has 3 rates.
  - `[low]` `[reject]` EC: four 48,000 B PSRAM buffers (192,000 B) instead of two move the OOM threshold — accepted in Design Notes; the story asks for an encoding to plane bytes; X4 Pro PSRAM is megabytes; fix is an API redesign.
  - `[low]` `[reject]` BH: SSD1677 planes are identity copies of the masks (96 KB memcpy per scan, double PSRAM) — same as above; the copy is outside the timed window; the story names `Ssd1677Encoding` as two masks to plane bytes.
  - `[low]` `[patch]` BH: the doc claims a new controller needs only a backend and two cases, though polarity labels and TPs are SSD1677 facts — doc sentence narrowed (the D1 hook itself is deferred under the intent-alignment row below).
  - `[low]` `[reject]` BH: `ScanRequest` has no size/geometry — same as the EC size row; D1 shape is fixed.
  - `[low]` `[patch]` BH: the seam never defines a pixel set in both masks — `ScanRequest` comment now says D1 leaves it undefined, callers set at most one, SSD1677 holds it (LUT3).
  - `[low]` `[reject]` BH: geometry-mismatch notice and "no mask planes" log now also covers a missing backend — the notice is unreachable on the X4 Pro; the log line is Part A's text, kept identical by the matrix.
  - `[low]` `[patch]` BH: `planeFillFor`/`framePeriodScan` are firmware-dead but the doc describes them as live — doc entry now calls them Part A's reference the tests pin the seam against.
  - `[medium]` `[patch]` BH: the frame-period index mapping is untested; the test restates the formula — mapping moved to a pure lib/GameDisplayLab helper the screen calls, pinned against `framePeriodScan` (grouped with the VG row).
  - `[low]` `[reject]` BH: `FramePeriodActivity` sized for 16 rates (~4.5 KB heap vs ~0.8 KB) — deliberate (Design Notes) so lanes A/B need not edit the screen; X4 Pro only, freed when the screen closes.
  - `[medium]` `[patch]` BH: `hasScanBackend` and `makeScanBackend` are parallel switches nothing keeps in agreement; lanes A/B will each add a case — both now derive from one file-local switch; the UC cases stay separate with a comment.
  - `[low]` `[reject]` BH: `name()`/`supportsWeakRow()` unused and the queries are non-const — D1 lists all seven members; adding `const` changes D1's literal shape.
  - `[false]` `[reject]` BH: `cleanPanel()` dereferences `scanBackend` unchecked — reached only from `scan()` after the backend check and from `onExit` when `sinceClean() > 0`, which needs a scan recorded with a backend; `closeSession` runs after it.
  - `[false]` `[reject]` BH: Run reports Done with zero scans — unreachable, as the EC row above.
  - `[low]` `[patch]` BH: stale test target name and a dangling CMake comment line — comment reflowed; the name `test_scan_lut` is kept because the story names that target.
  - `[low]` `[reject]` BH: no on-device re-run recorded after the refactor — the intent states "Hardware: none" (the owner's unit is a UC8179).
  - `[medium]` `[patch]` VG: the frame-period scan order is restated in the test, not taken from the screen — fixed by the lib mapping helper (grouped with the BH mapping row).
  - `[medium]` `[patch]` VG: the polarity row-to-mask table is unpinned — table moved to the lib helper; a test pins that each row's mask encodes to `planeFillFor(Row1/Row2)`.
  - `[medium]` `[defer]` VG: nothing checks the argument order of the bytes `Ssd1677ScanBackend::scan` actually sends — device-only facade glue; needs a fake `EInkDisplay` harness or an SSD1677 unit; deferred.
  - `[low]` `[defer]` VG: the wrapper's guard order (rate refusal before the cap clean, count only on Done) has no automated check — no host harness for the activity; the backend-agreement part is closed by the single-switch patch; deferred.
  - `[low]` `[patch]` VG (other): Part A helpers now used only by tests, so the tests could not show the screens agree with Part A — closed by the two lib helpers the screens now call.
  - `[low]` `[defer]` IA: byte identity is pinned at the pure layer, not at the facade boundary — same gap as the VG facade row; deferred there.
  - `[low]` `[reject]` IA: the wrapper's guard order is untested on host — same as the VG wrapper row (deferred there).
  - `[low]` `[reject]` IA: PSRAM doubled and new log lines — PSRAM as the EC row; new lines appear only on new refusal/OOM paths; Part A's lines on SSD1677 paths are unchanged.
  - `[low]` `[patch]` IA: lanes A and B both edit `ScanBackends.cpp`, against the epic's disjoint-files lanes — the two UC cases are separated by an unchanged line so their one-line edits merge cleanly; the CMake source list is a trivial merge for the orchestrator.
  - `[maybe-false]` `[reject]` IA: D1's open points (`Scan` nested, `available()` = buffers allocated, `rate` an index) are now de-facto contract — each follows R1/D1's text (Design Notes); it would only be an issue if a later lane needs another reading, which would settle it; if-true grade low.
  - `[low]` `[defer]` IA: the polarity row labels and `vs=` log name are SSD1677 facts with no D1 hook — not caused by this change (D1 lacks the hook); the UC lanes need an orchestrator decision; deferred.

### 2026-10-16 — Review pass (follow-up, with Amendment D1a)
- verdicts: 30 findings — high 0, medium 2, low 23, false 5, maybe-false 0
- findings:
  - `[medium]` `[defer]` VG: the device glue (mask order into the request, `encodeSsd1677Planes` and `copyGrayscaleBuffers` arguments, `rowLabel` passing `false`) is not tested at any call site — carried: same claim as deferred item 1; the request side is now hardened by designated initializers (BH row below), the backend calls stay inspection-only.
  - `[medium]` `[defer]` VG: the controller-to-backend table (`factoryFor`) has no test; a lane mapping UC8179 to the SSD1677 backend would pass every test and drive SSD1677 waveforms on an UltraChip panel — device-only code with no host harness; deferred so lanes dlab-a1/b1 pin it (a pure controller-to-backend-kind mapping) when they add their cases.
  - `[low]` `[patch]` VG (other): `GamesLauncherActivity.h` still says the row shows where the probe said SSD1677 — comment reworded to "controller has a scan backend".
  - `[low]` `[reject]` VG (other): the plan's matrix and residual risks still name `STR_GAMES_DLAB_NOT_SSD1677` and the missing D1 hook — the fix is an edit to this build's plan; the Auto Run Result is rewritten at Finalize.
  - `[low]` `[defer]` IA: byte identity at the facade boundary is pinned only at the pure layer — carried (deferred item 1).
  - `[low]` `[reject]` IA: the wrapper is untested and the not-available/OOM log lines are reworded — carried (deferred item 2); the matrix's identical-log rows (timeout, cap, dropped, inverted) are unchanged.
  - `[low]` `[patch]` IA: launcher header comment describes SSD1677-only gating — same patch as the VG launcher-comment row.
  - `[low]` `[defer]` IA: `rowLabel(row) const` has no swap argument, so the backend names only the unswapped drive although D1a mentions swapped codes — D1a fixes the signature; no caller sends `swapPolarity = true` in this story; the pure `ssd1677RowLabel(row, swap)` covers both; deferred for the orchestrator before Part B swaps.
  - `[false]` `[reject]` IA: `MaskPlanes` byte order is tested against the documented convention, not the real framebuffer — `GfxRenderer.cpp:652` writes pixel x at bit `7 - (x % 8)` of byte `y * widthBytes + x / 8`, the same convention the tests pin.
  - `[low]` `[reject]` IA: PSRAM doubled and frame-period arrays sized for 48 rows — carried (Design Notes choices).
  - `[low]` `[patch]` IA: `LabExperiments.h` and the screens still include the SSD1677 header for `FRAME_PERIOD_TPS`/`POLARITY_TP` — constants moved into `LabExperiments.h`, `Ssd1677ScanLut.h` now includes it (grouped with the BH row).
  - `[low]` `[reject]` EC: a null-mask request can run a due cap clean before the backend refuses — carried.
  - `[false]` `[reject]` EC: a backend returning TimedOut/Dropped skips the count and clean — carried: the contract and the only backend return Done or Refused.
  - `[low]` `[reject]` EC: geometry mismatch shows "Not enough memory" — carried (unreachable on the X4 Pro).
  - `[low]` `[reject]` EC: Run reports Done with zero scans — carried (unreachable).
  - `[false]` `[reject]` EC: an invalid `Mask` value dereferences null — carried.
  - `[false]` `[reject]` EC: the polarity label and `vs=` log changed from Part A's text — Amendment D1a requires exactly this (`vs=<rowLabel>`, "LUT1 VSL"); the plan's older acceptance line is superseded (Implementation Notes).
  - `[low]` `[reject]` BH: the lab checks the rate but not the masks before the lock — carried.
  - `[false]` `[reject]` BH: a backend returning TimedOut skips the safety handling — carried.
  - `[low]` `[reject]` BH: geometry mismatch shows "Not enough memory" — carried.
  - `[low]` `[reject]` BH: identity encoding doubles PSRAM and copies 96 KB per scan — carried.
  - `[low]` `[reject]` BH: `ScanRequest` has no size — carried (D1 shape fixed; lab masks and backend planes both come from `getBufferSize()`).
  - `[low]` `[patch]` BH: `name()` is never called and the log never says which backend runs — `openSession` now logs `backend <name>, <n> rates` on success.
  - `[low]` `[patch]` BH: stale SSD1677-only comments and the doc title — launcher comment, doc title and `PolarityActivity::drive` comment reworded; the "no mask planes" log line is Part A's and stays.
  - `[low]` `[reject]` BH: `vs=LUT1 VSL` puts a space inside a key=value token — D1a fixes the label text ("LUT1 VSL"); the checklist reads logs by hand.
  - `[low]` `[reject]` BH: the frame-period screen reserves 16 rates; `rateCount() == 0` reports Done — carried.
  - `[low]` `[defer]` BH: the seam cannot label a swapped drive, and only `rowLabel` is const — swap part grouped with the IA rowLabel deferral; the const part is carried (D1's literal shape).
  - `[low]` `[patch]` BH: `LabExperiments.h` depends on the SSD1677 header — constants moved (see the IA row).
  - `[low]` `[patch]` BH: `framePeriodTp`/`framePeriodRate` tested only at rateCount 3; no backend-table test; stale target name — tests added at rateCount 1 and 16; the table test is deferred (VG row); the target name is carried (reject).
  - `[low]` `[patch]` BH: `ScanRequest` is built positionally, so swapped masks or frames/rate compile silently — both screens now use designated initializers.

## Design Notes

- Settled by the epic: D1 (interface shape, mask format), D2 (black→RED, white→BW, hold = both clear), D4 (screens do not draw during a drive), R5 (safety stays in the lab, controller-neutral). Both bits set selects LUT3, which this LUT leaves all VSS (holds); `supportsWeakRow()` is false because this backend drives no third row (lane C decides more).
- `ScanRequest.rate` is an index into the backend's rate list (R1 "frame-rate index"); SSD1677's list is `FRAME_PERIOD_RATES` in order, so index 0 is 0x22 as Part A's polarity scan used, and the 9 scans keep Part A's order.
- The frame-period screen sizes for 16 rates so lanes A/B need not edit it (UltraChip PLL lists are longer than three); a backend with more is capped with one `LOG_ERR`.
- Residual for the orchestrator: D1 has no hook for the polarity rows' drive-code names (`vs=VSL`), which are SSD1677 facts; this story keeps them from `Ssd1677ScanLut` (unchanged on SSD1677). Lanes A/B will need a decision (a D1 addition) to label rows on UltraChip.
- Masks live in the lab (one pair per session, shared by every screen); the backend owns its own encoded planes (SSD1677: +96,000 B PSRAM over Part A, 192,000 B total, while the lab is open).

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass, new suites included.
- `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py`, each `python3 scripts/*_test.py` -- expected: exit 0.
- `./bin/clang-format-fix` twice -- expected: second run leaves `git status` unchanged.
- After review, with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, under `flock /tmp/crosshatch-build.lock`: `pio run -e x4pro`, `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`; `scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects` -- expected: all succeed; record flash and static-RAM deltas against the epic base (9,712 B / 240 B spare at `0cb1fc8e`).
- Hardware: none (owner's unit is a UC8179; SSD1677 bytes are pinned by host tests).

## Auto Run Result

**Summary:** The display lab's scan primitive is now the epic's D1 seam. `ScanBackend`/`ScanRequest`
(`ScanBackend.h`, which also holds the lab's gate macro) take two full-screen drive masks; `Ssd1677ScanBackend` holds
Part A's facade path (own PSRAM planes, `copyGrayscaleBuffers`, timed `displayGrayBuffer(false, lut, true)`, FULL
refresh clean); its pure parts are `Ssd1677Encoding` (rate list as indices 0x22/0x11/0x44, LUT per request with row
codes swapped on request, white mask to BW and black mask to RED). `MaskPlanes` is a pure view over the lab's two
PSRAM masks (clear, fill, clipped rect, pixel, test). `DisplayLabActivity` picks the backend through `ScanBackends`
(one switch; SSD1677 only) and keeps every Part A guard in a controller-neutral `scan()` wrapper. Both screens build
`ScanRequest`s from `LabExperiments.h`'s pinned layouts; on an SSD1677 the LUT bytes, plane bytes, facade calls and
log lines equal Part A's.

**Files:**
- `lib/GameDisplayLab/MaskPlanes.{h,cpp}` -- the two drive masks in framebuffer byte order.
- `lib/GameDisplayLab/Ssd1677Encoding.{h,cpp}` -- SSD1677 rate list, request LUT, mask-to-plane encoding.
- `lib/GameDisplayLab/LabExperiments.h` -- frame-period scan order and polarity row masks, controller-neutral.
- `src/activities/games/display_lab/ScanBackend.h` -- gate macro, `ScanRequest`, abstract `ScanBackend` (D1).
- `src/activities/games/display_lab/ScanBackends.{h,cpp}` -- controller-to-backend table (`hasScanBackend`, `makeScanBackend`).
- `src/activities/games/display_lab/Ssd1677ScanBackend.{h,cpp}` -- Part A's facade path behind the seam.
- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- session (backend + masks), the safety wrapper.
- `src/activities/games/display_lab/FramePeriodActivity.{h,cpp}`, `PolarityActivity.{h,cpp}` -- requests through the seam.
- `test/game_display_lab/test_mask_planes.cpp`, `test_ssd1677_encoding.cpp`, `CMakeLists.txt` -- new suites in `test_scan_lut`.
- `docs/crosshatch/display-lab.md` -- the seam, its limits, and the code map.

**Review (thorough, four lenses):** 33 findings -- high 0, medium 5, low 23, false 4, maybe-false 1. Patched: 3
medium entries (frame-period order pinned via a lib helper; polarity row masks pinned; one controller switch) and 5 low
(doc seam claim, doc Part A helpers, both-masks comment, CMake comment, UC case spacing). Deferred: 3 (facade argument
order untested, medium; wrapper guard order untested, low; no D1 hook for row names, low). Rejected: 25, each with its
reason in the Review Triage Log (chiefly unreachable today, D1's fixed shape, or a deliberate Design Notes choice such
as the 16-rate frame-period capacity and the 192,000 B PSRAM session).

**Follow-up review:** recommended (`true`): three medium entries were patched after the lenses ran, so
`LabExperiments.h`, the `factoryFor` switch in `ScanBackends.cpp`, and the two screens' call sites have had no
independent review.

**Verification (2026-10-16, uncommitted tree, after the patch round):**
- Host: `flock /tmp/crosshatch-hosttest.lock ... cmake/ctest` -- 2,104 of 2,104 passed (`test_scan_lut` 40 tests).
- `scripts/check_layers.py` pass; all 13 `scripts/*_test.py` exit 0; `./bin/clang-format-fix` twice, second run no change, nothing outside this story's files; `scripts/check_upstream_touches.py` PASS (re-run after the commit).
- Under `flock /tmp/crosshatch-build.lock`, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` rc 0 (RAM 31.8%, flash 94.9%), `pio run -e default` rc 0, `pio run -e sticky` rc 0 (lab files compile empty under the gate), `pio check -e x4pro --fail-on-defect low/medium/high` PASSED (no defects).
- `scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`: games delta +268,784 B, within budget with 7,696 B to spare (base `0cb1fc8e`, same commands: 9,712 B spare, so this story costs +2,016 B of flash); static internal RAM delta +784 B, 240 B to spare (+0 B against the base). `objects`: no problems.
- Hardware: none (the owner's unit is a UC8179).

**Residual risks:**
- The device-side glue (argument order into `encodeSsd1677Planes` and `copyGrayscaleBuffers`, the wrapper's guard order) is checked only by inspection and the build; the first SSD1677 hardware run should repeat the display-lab checklist (deferred items 1-2).
- `ScanRequest` carries no mask size (D1's fixed shape): a later caller must pass masks of `display.getBufferSize()` bytes, as `MaskPlanes` from the lab's `masks()` are.
- Polarity row labels, `vs=` log names, and frame-period TPs are SSD1677 facts; lanes dlab-a1/b1 need an orchestrator decision on a D1 hook (deferred item 3). User-facing `STR_GAMES_DLAB_NOT_SSD1677` / `FRAME_PERIOD_DESC` wording is still SSD1677's.
- Lanes A and B both add a case to `ScanBackends.cpp` and a test file to `test/game_display_lab/CMakeLists.txt`; the cases sit on separate lines, the CMake list is a trivial merge.
- Flash headroom for Part B is now 7,696 B (epic Notes assumed 9.5 KiB).

### Follow-up pass (2026-10-16): Amendment D1a and the second review

**Summary:** Applied Amendment D1a. `ScanBackend::rowLabel(uint8_t row) const` is backed by the pure, tested
`ssd1677RowLabel(row, swap)` and returns "LUT1 VSL" / "LUT2 VSH1". The polarity screen now labels its rows ("Row 1
(LUT1 VSL)") and logs `vs=<rowLabel>` from it. The DLAB strings are controller-neutral:
- `STR_GAMES_DLAB_ROW1` and `ROW2` are "Row 1 (%s)" and "Row 2 (%s)".
- `ROW_DESC` is "Panel waveform bench".
- `STR_GAMES_DLAB_NOT_SSD1677` is renamed `STR_GAMES_DLAB_NOT_SUPPORTED`.

This resolves deferred item 3, the missing D1 hook for row names.

The second review's patches:
- `FRAME_PERIOD_TPS` and `POLARITY_TP` moved into the controller-neutral `LabExperiments.h`, which `Ssd1677ScanLut.h` now includes.
- Both screens build `ScanRequest` with designated initializers.
- `openSession` logs `backend <name>, <n> rates`.
- Neutral comments in the launcher header and `PolarityActivity`, and a neutral doc title.
- Order tests at rate counts 1 and 16.

**Files (this pass):**
- `ScanBackend.h`, `Ssd1677ScanBackend.{h,cpp}`, `Ssd1677Encoding.{h,cpp}`: `rowLabel` and `ssd1677RowLabel`.
- `PolarityActivity.{h,cpp}`: labels and log from `rowLabel`, designated initializers.
- `FramePeriodActivity.{h,cpp}`: designated initializers, includes `LabExperiments.h`.
- `LabExperiments.h`, `Ssd1677ScanLut.h`: the shared constants moved.
- `DisplayLabActivity.cpp`: the renamed notice key and the backend log line.
- `GamesLauncherActivity.h`: comment only.
- `english.yaml`: `STR_GAMES_DLAB_*` lines only (ledger row 2).
- `test_ssd1677_encoding.cpp`: row-label tests and the order tests at 1 and 16 rates.
- `docs/crosshatch/display-lab.md`: title, checklist labels and logs, seam paragraph, code map.

**Review (follow-up, four lenses over the combined diff since `38dea818`):** 30 findings: high 0, medium 2, low 23, false 5.
- **Patched (8, all low):** launcher comment, stale comments and doc title, backend log line, constants moved, order tests, designated initializers.
- **Deferred (2 new):** the `factoryFor` table is untested (medium); `rowLabel` has no swap argument (low).
- **Carried deferrals:** items 1 and 2.
- **Rejected:** the rest, each with its reason in the Review Triage Log. The polarity log change is required by D1a, and it supersedes the plan's older "log lines textually identical" acceptance line.

**Follow-up review:** not recommended (`false`). This follow-up pass patched no high finding, so it has converged; it patched 8 low findings and no medium.

**Verification (2026-10-16, after the patches, before the commit):**
- Host suite: 2,106 of 2,106 pass, of which 42 are in `test_scan_lut`.
- `check_layers` passes, and all 13 `scripts/*_test.py` exit 0.
- `./bin/clang-format-fix`, run twice, was stable.
- `check_upstream_touches` passes on the commit.
- Under the build lock, with the shared cache:
  - `pio run -e x4pro`, `-e default` and `-e sticky` all exit 0.
  - `pio check -e x4pro` at low, medium and high passes with no defects.
- Flash budget, measured with `build on` / `build off` / `compare`:
  - Games delta +268,928 B, within budget with 7,552 B to spare. The base `0cb1fc8e`, measured the same way, had 9,712 B spare, so the story costs +2,160 B in total.
  - Static internal RAM is +784 B with 240 B to spare, +0 B against the base.
  - `objects` reports no problems.
- Hardware: none.

**Residual risks:**
- **Device glue untested:** the order of the device-side arguments into `encodeSsd1677Planes` and `copyGrayscaleBuffers`, and the order of the wrapper's guards, are checked by inspection only (deferred items 1 and 2). The request side is now named fields.
- **Backend table:** lanes dlab-a1/b1 should pin the controller-to-backend table when they add their cases (deferred item 4).
- **Swapped labels:** `rowLabel` names the unswapped drive only (deferred item 5).
- **Frame-period subtitle:** `STR_GAMES_DLAB_FRAME_PERIOD_DESC` still reads "Nine timed hold scans". That is the SSD1677's count; another backend with a different number of rates would make it wrong.
- **Polarity log format:** the polarity log is now `vs=LUT1 VSL`, with a space inside the value.
- **Flash headroom for Part B:** 7,552 B.

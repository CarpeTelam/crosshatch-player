# Story dlab-g2: seven levels (lane G, epic branch)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R3, R4,
Boundaries, Decisions D4, D5, D12, D13 and D14; the Story results for dlab-c3, dlab-c4 and dlab-g1). Ref: `dlab-g2`.
Plan: `_bmad-output/implementation-artifacts/plan-dlab-g2-seven-levels.md`. Builds on `dlab-g1` (the ladder's
measurements) and on the whole of lane C.

## Intent

**Problem:** The engine models four shades (K, D, L, W; level 0..192 in steps of 64; a 4×4 dose table with a 16-bit
hold mask; the transition test's four bands; Tuning's twelve dose rows). The owner wants seven levels, black, five
grays and white, so a true 50 % gray sits in the middle (D14): the ladder (dlab-g1) shows how many pulses separate
them.

**Approach:** Generalise the engine's shade count to seven, keeping every mechanism. The level scale stays 0..192:
the step becomes 32, today's K, D, L, W sit exactly on levels 0, 2, 4, 6, the new grays are 1, 3 (50 %) and 5.
1. **Model.** `SHADE_COUNT` 7; `Shade` becomes the index 0 (black) .. 6 (white) with `K` = 0, `D` = 2, `L` = 4 and
   `W` = 6 kept as named constants; level step 32 (0..192, `TOP_LEVEL` unchanged); the pixel's target | source byte
   uses 3 bits each; `ORIGIN_NONE` 7; `baseDose` interpolates by level % 32. The 3 B/pixel layout and the history
   array's size are unchanged.
2. **Preset.** `dose[7][7]` (42 off-diagonal values, 1..24) and a 64-bit hold mask (diagonal bits always set). The
   history routes become directional: two routes (return toward dark, return toward light), each with its return
   delta and reverse percent, replacing the six per-pair routes (a pixel returning to its origin takes the route of
   the direction it returns in). The preset file's `version` becomes 2: `doses` and `holds` carry 56 entries in the
   order `from 0..6, to ≠ from ascending`; `history` keeps its mode; `return_delta` and `reverse_percent` carry two
   values. A version 1 file still loads: its 4×4 table lands exactly on levels 0, 2, 4, 6 and the odd levels'
   doses interpolate linearly between their neighbours' (a transition into or out of an odd level is the mean of the
   two anchor transitions, rounded up), its holds by the nearest anchor pair, its six routes collapse to the two
   directional ones (the L>K>L return gives toward-light), logged `preset: version 1 file expanded to seven
   levels`; Save writes version 2.
3. **Defaults.** `inkBoyDefaults()` is the expansion of InkBoy's table (pinned); `ultraChipDefaults()` is the seven-
   level table the orchestrator reads off the ladder photos, recorded in D14 with its pulse counts (until those exist,
   the expansion of the calibrated four-shade table, and the test pins whichever D14 holds).
4. **Transition test.** Seven bands, one rectangle lane each (lane shade `(band + 3) mod 7`, rectangle height the
   band minus two insets), Static's flips rotate the offset +1 .. +6 (six flips cover all 42 transitions, logged as
   today), Slow and Fast unchanged otherwise; the hold marker's dash sits in the bottom band's inset as in dlab-c4.
5. **Tuning.** The dose rows become 42, grouped by source level (`Dose 0>1` .. `Dose 6>5`), then 42 hold rows, then
   the retargets, history mode, the two return deltas, the two reverse percents, rate and the rest; row text names
   levels by index (0 black, 3 the 50 % gray, 6 white). The row list is long, so the tuning screen gets a **Section** row at the top that jumps between
   Doses, Holds and Other (a lab preference, not saved).
6. **Transparent.** The 3-bit level field's value 7 is reserved as `Shade::TRANSPARENT` (D14, the owner's ask for
   sprites): a level-coded bitmap pixel at 7 is not retargeted. `GrayModel::retargetBitmap(x, y, w, h, levels,
   preset)` (one byte per pixel, 0..6 a level, 7 transparent) retargets the others and skips it, pinned by a test; the
   preset codec rejects 7 anywhere in a table; the game engine's own sprite plumbing over the gray engine is a later
   story and not this one.
7. **Stock bench.** Keeps its four levels through the named constants `K`, `D`, `L`, `W` (0, 2, 4, 6); its
   behaviour and tests unchanged.

## Boundaries & Constraints

**Always:** the mask planes, slots, scheduler, scan path, backends and seam unchanged (each scan still drives a pixel
black, white or holds it); the model's 3 B/pixel and the history array's 3 B/pixel unchanged; every pure change
host-tested (the level arithmetic, the 7×7 codec and the version 1 expansion, the defaults, the directional routes,
the band and lane geometry, the six-flip coverage); a version 1 file loads and a version 2 file round-trips;
`DOSE_MAX` 24; locals under 256 B; `tr(STR_…)` keys in `english.yaml` only; C3 `default` and `sticky` still build;
the flash-budget gate (D6) unchanged; `docs/crosshatch/display-lab.md` rewritten where it says four shades.

**Never:** no change to `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam or `freeink-sdk/`;
no new ledger row; no edit of generated I18n files; no change to the frame-period or polarity screens; no change to
the stock bench's measurements; no change to the ladder's stages (dlab-g1).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Level arithmetic | shade s | level s × 32; `baseDose` at a level between two shades interpolates | — |
| Version 1 file | 12 doses, 16-bit holds, six routes | expands as described, logged; Save writes version 2 | malformed: defaults, LOG_ERR, as today |
| Version 2 file | 42 doses, 42 holds, two routes | loads; round-trips through Save | a dose outside 1..24: rejected whole, as today |
| Defaults | SSD1677 / UltraChip | the two pinned tables | — |
| Static flips | Run with Mode = Static | offsets +1 .. +6, every band through all six other levels | — |
| Tuning | Section row | Doses / Holds / Other; the rows below follow | — |
| Transparent | retargetBitmap with a 7 | that pixel keeps its level and target; the others retarget | a value above 7: clamped to transparent, LOG_ERR once |
| Stock bench | UltraChip without a lab backend | four levels, unchanged | — |

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`; the tests above.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`,
  `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro`; the flash-budget gate with D6 (expect +0).
- Hardware (owner, experimental branch): Static shows seven distinguishable bands after the first settle; the six
  flips show which of the 42 transitions need a dose change; Tuning edits them.

# Story dlab-g1: the ladder scene (lane G, epic branch)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R3, R4,
Boundaries, Decisions D4, D5, D12, D13 and D14; the Story results for dlab-c3 and dlab-c4). Ref: `dlab-g1`. Plan:
`_bmad-output/implementation-artifacts/plan-dlab-g1-ladder-scene.md`. Builds on `dlab-c4` (`TransitionScene`'s hold
phases and marker, `SweepControl::holdRun`, the sweep task) and `dlab-c1` (`GrayModel`, `GraySlot.h`).

## Intent

**Problem:** The owner wants eight shades (D14). The four-shade table was tuned by eye, twelve values, from
photographs of Static flips. An eight-shade table has 56 doses and cannot be tuned that way. What is missing is the
panel's response curve: how dark n one-frame pulses from white leave a pixel, how light n pulses from black, for
n = 1..24 at the preset's rate, and whether n pulses back restore the origin. With that curve the eight levels and
their doses are read off a photo instead of guessed.

**Approach:** A fourth mode on the transition test's Mode row, **Ladder** (Static → Slow → Fast → Ladder), that paints
a measurement pattern instead of bands and rectangles, in two stages, each ending in a hold (dlab-c4's mechanics,
marker included, Hold row honoured):
1. **Stage 1, the ladder.** The logical screen splits into a top half starting from white and a bottom half starting
   from black (the clean leaves the panel white, so the bottom half is first driven to black with the preset's full
   W>K dose and settled before the stage starts, logged `gray ladder: base painted`). Each half is 24 columns, column
   n (1..24, left to right) receives exactly n one-frame pulses toward the opposite shade, one per slot (the held
   rhythm, so each pulse is a scan), through a new pure entry point `GrayModel::driveRect(x, y, w, h, Toward, doses)`
   that sets the doses directly without the table (level and target bookkeeping as a retarget's, so settle detection
   and the masks work unchanged). A 1-column gutter of the half's origin shade separates columns where the width
   allows (480 wide: 20 px columns, no gutter; 800 wide: 32 px columns with a 1 px gutter). After the longest column
   settles (24 slots), the scene holds: `gray hold: ladder 1`.
2. **Stage 2, the return.** Next resumes: each column is driven back toward its origin with the same n pulses, settles,
   holds: `gray hold: ladder 2`. A column that is not back at its origin shows the asymmetry of that direction.
3. **End.** Next resumes: the run ends as a finished sweep does (`gray ladder: done`); Run replays it.
The log at each hold names the rate index, the pulse rhythm (one per slot) and the column width, so the photo can be
read without the device. The scene's decisions (column rects for both orientations, which slot holds, the two stages,
the end) are pure in `TransitionScene` and host-tested; the dose entry point is pure in `GrayModel` and host-tested.

## Boundaries & Constraints

**Always:** every scan still goes through `DisplayLabActivity::scan` on the sweep task; D4 and D5 unchanged; the hold
and marker mechanics of dlab-c4 reused, not duplicated; the Tuning screen, the preset file and `GrayPreset` unchanged
(the ladder reads the preset's rate index and polarity only); `driveRect` leaves `retarget` untouched; locals under
256 B; `tr(STR_…)` keys in `english.yaml` only; C3 `default` and `sticky` still build; the flash-budget gate (D6)
unchanged; the doc's "What to photograph" gains the two ladder photos and how to read them (column n, darkness).

**Never:** no change to `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam or `freeink-sdk/`;
no new ledger row; no edit of generated I18n files; no change to the stock bench, the frame-period or polarity
screens; no change to the Static, Slow and Fast modes' behaviour.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Ladder mode | Mode row stepped past Fast | "Ladder"; Run paints the base, then stage 1 | — |
| Stage 1 | 24 slots after the base | column n has had n pulses; settle; hold `ladder 1` | Hold off: stage 2 follows at once |
| Stage 2 | Next while holding | columns driven back n pulses; settle; hold `ladder 2` | Hold off: the run ends |
| End | Next while holding at stage 2 | the run ends, rows return with "Finished", Run replays | — |
| Narrow screen | logical width < 48 | columns at least 1 px, as many as fit, logged | — |
| Session cap mid-stage | cap reached | the cap clean runs; the ladder restarts from the base, logged | — |
| driveRect | rect, Toward::Dark, 5 | every pixel in the rect: target K (or W for Light), 5 doses left, level kept | out-of-model clipped |

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`; tests for `driveRect`, the column
  geometry in both orientations, the stage sequence with Hold on and off, and the cap restart.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`,
  `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro`; the flash-budget gate with D6 (expect +0).
- Hardware (owner, experimental branch): the two ladder photos at rate index 0; the orchestrator reads the eight
  levels and their pulse counts off them for dlab-g2's starting table.

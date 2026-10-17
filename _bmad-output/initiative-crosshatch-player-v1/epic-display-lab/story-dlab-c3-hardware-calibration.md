# Story dlab-c3: the transition test after the owner's first UC8179 calibration run (lane C, epic branch)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R3, R4,
Boundaries, Decisions D4, D5, D11 and D12; the Story results for dlab-c1 and dlab-c2). Ref: `dlab-c3`. Plan:
`_bmad-output/implementation-artifacts/plan-dlab-c3-hardware-calibration.md`. Builds on `dlab-c2`
(`TransitionTestActivity` on the sweep task, `SweepControl`), `dlab-c1` (`GrayModel`, `GrayPreset`,
`TransitionScene`, `TuningActivity`) and the owner's hardware findings in D12.

## Intent

**Problem:** The owner ran the transition test on a UC8179 X4 Pro (D12). Four flash-free shades form, but: (1) the
test's live controls were unreachable: Back is a left-edge swipe that the view's swipe guard swallows before its
Back check, pause is Confirm (a front button the X4 Pro lacks) or a tap, and touch routing is dead while the view
is up, so the only way out was Home or the light-panel swipe; (2) the sweep (12 px rectangles crossing in 5 s) is
far faster than this panel's slot (about 0.35 to 0.4 s), so every rectangle is a one-slot touch and the test only
ever exercises partial transitions, which the model handles worst; (3) one retarget percentage cannot serve both
directions: at 150 % returns toward white overshoot while returns toward black still fall short; (4) the panel needs
12 one-frame pulses for the full white-to-black swing, the dose cap, leaving no headroom.

**Approach:** Four changes to the transition test and its engine, all behind `CROSSHATCH_DISPLAY_LAB` or in
`lib/GameDisplayLab`:
1. **Side-button controls.** While the test view is up: NavNext (side Down) pauses and resumes, NavPrevious (side
   Up) stops and returns to the rows (what Back did). Back (the gesture or a front button where one exists) still
   stops: check Back before the swipe guard. Confirm and the tap still pause where they exist. The Controls row and
   `docs/crosshatch/display-lab.md` say so. Replay and clean leave the live view: Run on the rows replays (it cleans
   first), and a new **Clean** row cleans the panel to white without a run. The tuning screen gains nothing new.
2. **Sweep modes** on the Speed row, which becomes **Mode**, stepping Static → Slow → Fast (Fast is today's 5 s
   sweep, kept for the SSD1677). *Static*: paint the bands and rectangles once and let every pixel finish its dose
   run (the scene stops moving); after the pattern has settled (no pixel active for one slot), flip it: every pixel
   takes one full transition to the shade `(shade + 2) mod 4` (K↔L, D↔W), settle, flip back, settle, and repeat;
   the log says `gray static: flip N settled after M slots` each time. *Slow*: the sweep crossing time is computed
   from the backend's measured scan time so a rectangle dwells for at least a full dose run: crossing time =
   `RECT_W_SLOW / (RECT_W_SLOW / (slots per dose run × slot period))`, i.e. pick `RECT_W_SLOW = 48` px and a crossing
   time such that a pixel stays under a rectangle for `ceil(DOSE_MAX / 2)` slots at the measured slot period (the
   first run uses D5's 100 ms minimum until a slot has been measured, then re-derives); cap the crossing at 120 s.
   The scene keeps its lanes and shade order; only the rectangle width and the crossing time change by mode.
3. **Retarget split**: `retargetPercent` becomes two values, toward black (`retargetDarkPercent`) and toward white
   (`retargetLightPercent`), each 50..150 in steps of 25, two tuning rows ("Retarget to dark", "Retarget to light").
   A pixel retargeted to a darker shade scales by the dark value, to a lighter shade by the light value. The preset
   file gains the two keys and keeps reading the old `retarget_percent` as both (version stays 1; document the
   fallback).
4. **Dose range**: `DOSE_MAX` 24. The tuning rows' step stays 1 (Confirm edit mode: Next/Previous ±1; a tap +1
   wrapping). The preset file's range check follows.
5. **UltraChip starting preset** (D12): `GrayPreset::ultraChipDefaults()` = the owner's table
   `[3,7,12,7,2,10,7,2,9,12,5,3]` with InkBoy's holds; the lab uses it when the backend is UC8179 or UC8279 and no
   preset file exists (`GrayPresetFile` logs which defaults); InkBoy's stay for the SSD1677. Pinned by a test.

## Boundaries & Constraints

**Always:** every scan still goes through `DisplayLabActivity::scan` on the sweep task under `RenderLock`; D4 (no
drawing during a sweep) and D5 (the slot period) unchanged; the pure parts stay pure and host-tested (the mode's
geometry and the static flip's sequencing in `TransitionScene`, the split retarget in `GrayModel`/`GrayPreset`, the
preset codec's new keys and fallback, the defaults); the preset file's `version` stays 1 and an old file still loads;
`SweepControl`'s protocol unchanged except for whatever a new request needs (prefer none: Clean is a row, not a
live control); locals under 256 B; `tr(STR_…)` for new text, keys in `english.yaml` only; C3 `default` and `sticky`
still build; the flash-budget gate (D6) unchanged.

**Never:** no change to `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam or `freeink-sdk/`;
no new ledger row; no edit of generated I18n files; no change to the stock bench or the frame-period and polarity
screens; no change to how the sweep task starts, stops or is joined (dlab-c2's design stands).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Pause by side button | NavNext while the view is up | the task pauses at the slot boundary, as Confirm did; NavNext again resumes | — |
| Stop by side button | NavPrevious while the view is up | the view ends with a FULL refresh and the rows, as Back did | — |
| Back gesture | left-edge swipe while the view is up | stops the view (checked before the swipe guard); other swipes still consumed | — |
| Clean row | activated on the rows | one FULL refresh to white, no run, Run row unchanged | — |
| Static mode | Run with Mode = Static | pattern painted once; `settled` logged when no pixel has doses left for a slot; then flip, settle, flip back, repeat until stopped | a scan failure ends the run as today |
| Slow mode | Run with Mode = Slow, slot period measured p ms | rectangles `RECT_W_SLOW` wide; a pixel stays under a rectangle for ≥ ceil(DOSE_MAX/2) slots; crossing ≤ 120 s | no measurement yet: D5's 100 ms assumed, re-derived after the first slot |
| Fast mode | Run with Mode = Fast | today's sweep, unchanged | — |
| Retarget split | a pixel mid-transition retargeted darker / lighter | dose scaled by the dark / light percentage | — |
| Old preset file | `retarget_percent` only | both new values take it; `tune` log says so | malformed: defaults, LOG_ERR, as today |
| Dose 24 | a dose row at 24, tap | wraps to 1; edit mode clamps at 24 | — |
| Defaults by controller | UC8179 / UC8279, no file | `ultraChipDefaults()`; SSD1677: `inkBoyDefaults()` | — |

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`; new tests for every pure change.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`,
  `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro`; the flash-budget gate with D6 (expect +0).
- Hardware (owner, on the experimental branch's merge): side Down pauses within a slot, side Up stops; Static mode
  settles and flips without a flash; the residue after a full flip per band is the per-transition answer.

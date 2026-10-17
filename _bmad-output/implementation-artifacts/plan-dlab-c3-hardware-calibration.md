---
title: 'dlab-c3: the transition test after the first UC8179 calibration run'
type: 'feature'
ticket: 'story-dlab-c3-hardware-calibration'
created: '2026-10-17'
baseline_revision: '39903ffbc3e4684ff4f31a9c4e11d495ece663de'
status: 'built'
route: 'full'
route_source: 'pinned'
review: ''
review_source: ''
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: true
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-c3-hardware-calibration.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
warnings: ['oversized']
deferred:
  - summary: >-
      The UltraChip starting preset is unreachable on the epic branch: an UltraChip controller has no scan backend there and gets the stock bench, so the lab loads and logs a preset nothing uses.
    evidence: |-
      ScanBackends.cpp returns nullptr for UC8179 and UC8279 on this branch by design (the backends live on the experimental branch, claude/display-experimental-sdk-do-not-merge, where the merge makes the preset live). Settles when the backends come to the epic branch.
    location: >-
      src/activities/games/display_lab/DisplayLabActivity.cpp (onEnter)
    severity: low
  - summary: >-
      The controller-to-PresetBase choice in DisplayLabActivity::onEnter and the legacy-retarget log in GrayPresetFile::load have no test.
    evidence: |-
      Inverting the ternary, or passing a different base to load than to the reset, passes every host test; the code is lab-only and uses a HAL type the host build cannot compile. A pure presetBaseFor(bool ultraChip) helper would pin the first; the owner's run on each controller family is the only check today.
    location: >-
      src/activities/games/display_lab/DisplayLabActivity.cpp, GrayPresetFile.cpp
    severity: low
  - summary: >-
      The live-view input order (Back before the swipe guard, NavPrevious stops, NavNext pauses) has only the owner's hardware check.
    evidence: |-
      No harness compiles TransitionTestActivity (CROSSHATCH_DISPLAY_LAB is off on the host); putting the swipe guard back above the Back check would swallow the edge swipe again with no CI signal. Joins dlab-c2's deferred device-wiring family.
    location: >-
      src/activities/games/display_lab/TransitionTestActivity.cpp (handleCustomInput)
    severity: medium
---

<intent-contract>

## Intent

**Problem:** On the owner's UC8179 X4 Pro (epic D12) the transition test's live controls are unreachable (Back is an edge swipe the swipe guard swallows, Confirm is a front button the device lacks, touch routing is dead while the view is up), the 5 s / 12 px sweep is a one-slot touch per pixel at this panel's 0.35-0.4 s slot, one retarget percentage cannot serve both directions, and 12 doses is the cap the panel's full swing already needs.

**Approach:** Side buttons drive the live view (Next pauses, Previous stops, Back checked before the swipe guard); replay and clean become rows (Run, a new Clean); a Mode row picks Static (paint, settle, flip, repeat), Slow (48 px, crossing sized from the measured slot) or Fast (today's sweep); the retarget percentage splits into toward-dark and toward-light; the dose cap rises to 24 (the model's doses-left count gets its own byte); UltraChip panels start from the owner's table. Pure parts in `lib/GameDisplayLab`, host-tested.

## Boundaries & Constraints

**Always:** every scan through `DisplayLabActivity::scan` on the sweep task under `RenderLock`; D4 and D5 unchanged; `SweepControl.h` unchanged (Clean is a row run by the loop task while no task exists); the sweep task's start, stop and join unchanged (dlab-c2); the preset file's `version` stays 1, an old file loads; locals under 256 B; new text through `tr(STR_…)` with keys in `english.yaml` only; `default` and `sticky` build; the flash gate (D6) +0; no new deferred-work entry made worse (the dlab-c2 five).

**Never:** `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, `freeink-sdk/`, the stock bench, the frame-period and polarity screens; a new ledger row; generated I18n files; per-slot heap allocation; a change to `RETURN_DELTA_LIMIT` (11) or the history routes.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Pause by side button | NavNext pressed while the view is up | pause at the slot boundary as Confirm does; again resumes | — |
| Stop by side button | NavPrevious released while the view is up | join, rows back with a FULL refresh, Run row "Stopped" | — |
| Back gesture | left-edge swipe while the view is up | stops (checked before the swipe guard); other swipes consumed | — |
| Clean row | activated on the rows (no task) | white framebuffer, `lab.cleanNow()` (one FULL refresh) under `RenderLock`, rows repainted; Run row unchanged | — |
| Static mode | Run, Mode = Static | bands and centred third-width rectangles painted once; a slot that drives no pixel logs `gray static: flip N settled after M slots`, then every target becomes `(shade + 2) mod 4`, settle, flip back, repeat | scan failure ends the run as today |
| Slow mode | Run, Mode = Slow | 48 px rectangles; crossing = `ceil(12 × period × (W + 48) / 48)` ms ≤ 120,000; dwell under a rectangle ≥ 12 slots | no measurement yet: D5's 100 ms; the run's first slot with a delivered drive scan re-derives once from its period and reapplies the scene |
| Fast mode | Run, Mode = Fast | today's 12 px / 5 s / 1 s pause sweep, unchanged | — |
| Retarget split | pixel mid-transition retargeted | target level below current: base × dark %; above: base × light %; rounded up, clamped 1..24 | — |
| Old preset file | `retarget_percent` only | both new values take it; `tune retarget_percent=N read as ...` logged | new keys win in any order; malformed: defaults, LOG_ERR |
| Dose 24 | dose row at 24 | tap wraps to 1; edit mode clamps at 24; file accepts 1..24 | 25 malformed |
| Defaults by controller | UC8179 / UC8279, no file | `ultraChipDefaults()`, logged; SSD1677 `inkBoyDefaults()`; absent keys of a file keep the controller's defaults | — |

</intent-contract>

## Code Map

- `lib/GameDisplayLab/GrayPreset.{h,cpp}` -- `DOSE_MAX`, `retargetPercent` (one field today), `stepRetarget`/`cycleRetarget`, `inkBoyDefaults`, `valid`, codec `readValue`/`encodeGrayPreset`/`loadGrayPreset` (base preset `inkBoyDefaults()`, key buffer 24 B: `retarget_light_percent` is 22 chars, fits).
- `lib/GameDisplayLab/GrayModel.{h,cpp}` -- state byte packs remaining in 4 bits (`REMAINING_MASK 0x0F`): 24 does not fit. `scaledDose`/`clampDose` clamp to `DOSE_MAX`; `retarget` picks the percent.
- `lib/GameDisplayLab/TransitionScene.{h,cpp}` -- `rectX` is static (Fast geometry); `update` returns early when the rectangle did not move (Static relies on it).
- `lib/GameDisplayLab/SlotScheduler.h` -- `periodUs()` after `slotDone` is the measured period (`MIN_PERIOD_US` 100 ms). Reuse unchanged; `GraySlot.h`'s `SlotResult.driven == 0` is "no active pixel this slot".
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- `handleCustomInput` (swipe guard before Back), `activateIndex`, `buildRows` (Run, Speed, Controls), `startRun`, `serve` (replay branch, its start log line), `runSlot` (end: hooks for Static and Slow), `leaveView`. Keep every c2 guard (`return` after `leaveView` in `loop`, Stop before refusal in the lambda, `endSweepLocked`'s order).
- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- `onEnter` loads the preset before the render lock; `grayPreset` reset under it.
- `src/activities/games/display_lab/GrayPresetFile.{h,cpp}` -- `load` logs "InkBoy's defaults" on Missing/Malformed.
- `src/activities/games/display_lab/StockBenchGate.{h,cpp}` -- the UltraChip check inline in `usesStockBench`.
- `src/activities/games/display_lab/TuningActivity.{h,cpp}` -- `RETARGET_ROW`, `Kind::Retarget`, `values[ROW_COUNT][40]`.
- Tests: `test/game_display_lab/test_gray_preset.cpp`, `test_gray_model.cpp`, `test_transition_scene.cpp` (already in `CMakeLists.txt`).
- `lib/I18n/translations/english.yaml` (ledger row 2), `docs/crosshatch/display-lab.md`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/GrayPreset.{h,cpp}` -- `DOSE_MAX = 24`; `retargetDarkPercent`/`retargetLightPercent` replace `retargetPercent`; `enum class Toward : uint8_t { Dark, Light }` with `retargetPercent(Toward)`, `stepRetarget(Toward, int)`, `cycleRetarget(Toward)`; `ultraChipDefaults()` (InkBoy's with D12's dose table); `enum class PresetBase : uint8_t { InkBoy, UltraChip }`, `presetDefaults(PresetBase)`, `presetBaseName(PresetBase)`; codec writes the two new keys in place of `retarget_percent`, reads all three, `loadGrayPreset(text, length, defaults, out, bool* legacyRetarget = nullptr)` -- the split, the cap, the defaults.
- [x] `test/game_display_lab/test_gray_preset.cpp` -- the documented text, round trip, steps per direction, 24 cap and wrap, UltraChip table pinned, legacy key as both, new key wins before or after it, legacy flag, absent keys keep the passed defaults, 25 and 49/151 per key malformed.
- [x] `lib/GameDisplayLab/GrayModel.{h,cpp}` -- three bytes a pixel (level, doses left, target | source << 2), `PIXEL_BYTES = 3`; the retarget percent by direction; clamps 1..`DOSE_MAX`; header comment.
- [x] `test/game_display_lab/test_gray_model.cpp` -- sizes (1,152,000 B at 800x480), a 24-dose run lands on its target, the split scaling both ways, the clamp at 24.
- [x] `lib/GameDisplayLab/TransitionScene.{h,cpp}` -- `enum class SweepMode : uint8_t { Static, Slow, Fast }`, `nextMode`; `setMode(mode, slotPeriodUs)`; `slowSweepMs(slotPeriodUs, logicalW)`; instance `rectX`, `rectW()`, `sweepMs()`; flip (`flipped()`, `flipShade`), `staticSlotDone(bool drove) -> std::optional<StaticSettled>`, `restartFlips()`; `measureSlow(bool delivered, uint32_t periodUs) -> bool` -- the modes' geometry and sequencing, pure.
- [x] `test/game_display_lab/test_transition_scene.cpp` -- Fast unchanged (existing tests via instance `rectX`); Slow formula, cap and measured dwell ≥ 12 slots at 100 and 380 ms; Static fixed rectangle, no move on update; flip sequence (settle N, flip, counts, flip back); `measureSlow` once per `setMode`; `nextMode` cycle.
- [x] `src/activities/games/display_lab/StockBenchGate.{h,cpp}` -- `isUltraChip(controller)`, used by `usesStockBench`.
- [x] `src/activities/games/display_lab/GrayPresetFile.{h,cpp}` -- `load(PresetBase, GrayPreset&)`; logs which defaults; `tune retarget_percent=N read as retarget to dark and to light` when the legacy key was used.
- [x] `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- `onEnter`: `PresetBase` from `isUltraChip(display.getController())` before `GrayPresetFile::load`; comments.
- [x] `src/activities/games/display_lab/TuningActivity.{h,cpp}` -- `RETARGET_DARK_ROW`, `RETARGET_LIGHT_ROW`, `Kind::RetargetDark`, `Kind::RetargetLight`; doses 1..24 in comments.
- [x] `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- rows Run, Mode, Clean, Speed, Controls; input as Design Notes; `sweepMode` (loop task, read by the task only after creation), `measuredPeriodUs` (task-written, read after the join); `startRun` sets the scene's mode; `serve` replay restarts flips and logs mode and both retargets; `runSlot`'s end calls `afterSlot`; class comment.
- [x] `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_MODE`, `..._MODE_STATIC`, `..._MODE_SLOW`, `..._MODE_FAST`, `..._CLEAN`, `..._CLEAN_DESC`, `..._RETARGET_DARK`, `..._RETARGET_LIGHT`; `..._TRANSITION_CONTROLS` reworded; `STR_GAMES_DLAB_RETARGET` removed (no other user).
- [x] `docs/crosshatch/display-lab.md` -- controls, modes, Clean row, split retarget, 24 cap, file keys and fallback, UltraChip table, model 3 B a pixel, log lines, code map.

**Acceptance Criteria:**
- Given the host suite, when it runs, then every test passes, including the new ones above.
- Given a run with the test view up on an X4 Pro, when side Down is pressed and side Up released, then the first pauses at the next slot boundary and the second ends the view with the rows' FULL refresh.
- Given an old preset file with only `retarget_percent`, when the lab opens, then Tuning shows that value on both retarget rows and Save writes the two new keys.
- Given `default`, `sticky` and the flash gate, when built, then both compile and the games delta and static RAM equal 39903ffb's, measured the same way.

## Implementation Notes

Built 2026-10-17 in the worktree `/home/user/crosshatch-lanec3` (branch `dlab-lane-c3`, base 39903ffb), staged, not
committed (the orchestrator runs the review lenses on the staged diff). Every task as planned; the settled readings:

- **Doses left gets a byte.** `GrayModel` is 3 B a pixel (`PIXEL_BYTES`: level, doses left, target | source << 2);
  `pixelAt` replaces the `2 * i` indexing; `bytesFor(800, 480)` is 1,152,000 B (+384,000 B PSRAM). Semantics and the
  `buildMasks`/`commit`/`runGraySlot` signatures are unchanged.
- **Retarget split.** `Toward` (Dark, Light); `GrayModel::retarget` takes `retargetPercent(t * 64 < level ? Dark :
  Light)` for a pixel interrupted mid-transition; history reversal percents still override. Tuning has "Retarget to
  dark" and "Retarget to light"; `STR_GAMES_DLAB_RETARGET` is gone (no other user, no other translation had it).
- **Codec.** `retarget_dark_percent` / `retarget_light_percent` written; `retarget_percent` read into a side value
  and applied after the object to each direction the text did not set (order-independent), reported through
  `loadGrayPreset(..., bool* legacyRetarget)`; `GrayPresetFile::load` logs `tune retarget_percent read as retarget to
  dark=N%, to light=N%`. `loadGrayPreset` now takes the defaults (absent keys keep the controller's). Version 1.
- **Defaults.** `PresetBase`, `presetDefaults`, `presetBaseName`, `GrayPreset::ultraChipDefaults()`; the lab's
  `onEnter` picks the base with the new `isUltraChip(controller)` (StockBenchGate, also used by `usesStockBench`)
  before `GrayPresetFile::load`. On this branch an UltraChip controller has no scan backend (stock bench), so the
  UltraChip preset is live only on the experimental branch.
- **Modes** (`TransitionScene`): `SweepMode`, `nextMode`, `setMode(mode, slotPeriodUs)`, instance `rectX`,
  `slowSweepMs` (12 x period x (W + 48) / 48, rounded up, 1..120,000 ms), `measureSlow` (once per `setMode`),
  `staticSlotDone` / `flipped` / `flipShade` / `restartFlips`. Static rectangles: a third of the width, centred.
  `CYCLE_MS` and the static `rectX(ms, w)` are gone (the scene's tests were their only users).
- **Screen.** Rows Run, Mode (default Static, kept for the visit), Clean, Speed, Controls. The Speed row stays: the
  story's "Speed row becomes Mode" is read as the Mode selector taking the place under Run, since the speed line is
  what Slow's derivation and the c2 hardware check (`ms_per_scan`) read. View input: Back or NavPrevious (release)
  stop, checked before the swipe guard; NavNext (press) or Confirm (release) pause. `afterSlot` runs Static's settle
  and Slow's measurement on the task; `SweepControl.h` untouched (nothing sends Replay mid-run or Clean now; `serve`
  still serves both). The Clean row runs on the loop task with no sweep task alive (guarded).
- Not changed: `RETURN_DELTA_LIMIT` stays 11; a session-cap clean in Static mode keeps counting the settle's slots
  across the clean (one log line can include the repaint).

**Results** (`PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, AGENTS.md's locks):
- Host suite: 2,206 tests, all pass (2,196 before; 10 new, others updated for the split, the 24 cap and 3 B pixels).
- `scripts/check_layers.py`: passed. `scripts/check_upstream_touches.py`: PASS (`english.yaml` is ledger row 2).
- `./bin/clang-format-fix`: reflowed four of this change's own comments and two test lines; the second run changed
  nothing.
- `pio run -e x4pro`, `-e default`, `-e sticky`: success. `pio check -e x4pro` with the three `--fail-on-defect`: no
  defects.
- Plain x4pro image, both built in this worktree the same way: 39903ffb Flash 6,251,426 B, `firmware.bin` 6,256,496
  B, RAM 104,232 B; this change Flash 6,254,294 B (+2,868), `firmware.bin` 6,259,360 B (+2,864), RAM 104,232 B (+0);
  `size -A` `.dram0.*`, `.noinit`, `.iram0.*` identical.
- Flash gate (D6), at 39903ffb and on the change: games-on 6,218,672 -> 6,219,392 B, games-off 5,960,496 -> 5,961,200
  B (+704: the new English strings, which every image carries), games delta +258,176 -> +258,192 B (+16; 18,288 B to
  spare); static RAM +784 B both (240 B to spare); `objects` clean both. The +16 B is not isolated: no lab code links
  into either gate image, so it is likely a layout effect of the string table on the games-on image, not engine code.

## Plan Change Log

### 2026-10-17 — orchestrator review fixes (supersede the Tasks and Design Notes where they differ)
- Slow: the dwell is the preset's longest dose run in slots (`GrayPreset::longestRunSlots`: dose when held, ceil(dose
  / 2) when not), not a fixed 12; the period passed to the derivation is max(scheduler period, the slot's real length
  from before the scene update to after the commit); the rectangles wait off-screen until the first delivered slot
  measures the period, and the crossing starts at that slot's clock (no jump, no reapply); the log names the dwell and
  says when the 120 s cap cut it. Avoids: a crossing too short for held runs and for the CPU part of the slot, and
  rectangles jumping back and retargeting pixels mid-transition.
- Static: the flip offset rotates +1, +2, +3 (three flips cover all twelve transitions), logged with the offset; the
  session cap's clean restarts the sequence (logged). Avoids: six transitions never measured, a stale settle count.
- `TransitionScene::slotDone(driven, driveDone, periodUs, elapsedMs) -> SceneStep` replaces `staticSlotDone` and
  `measureSlow`; the activity only logs and applies `reapply`, and the flip test drives `slotDone`. Avoids: the reapply
  decision living only in device code.
- The preset file gains `"base"` (`inkboy` / `ultrachip`, optional; a mismatch is logged as an error and still
  loads); `loadGrayPreset` reports through `GrayPresetLoadInfo` (which directions the old key filled, the file's base);
  `encodeGrayPreset` takes the base. One retarget selector serves both accessors.
- Controls text by logical action; taps documented as pausing only where touch routes in the view (not the X4 Pro).
- The intent contract's matrix rows for Static ("(shade + 2) mod 4") and Slow ("12 slots") are superseded by the
  above; the contract itself is left as written.

## Review Triage Log

### 2026-10-17 — Review pass (orchestrator-run lenses, per orchestrated-epics.md § 3's fallback: the session's subagents cannot nest)
- verdicts: 36 findings — high 0, medium 9, low 14, false 0, maybe-false 0, rejected-by-rule 13
- findings:
  - `[low]` `[defer]` (blind 1) the UltraChip preset is unreachable on this branch (an UltraChip controller has no scan backend here and gets the stock bench); the load and log run for nothing — by design: the backends live on the experimental branch, where the merge makes it live; deferred as a note.
  - `[medium]` `[patch]` (blind 2, edge E1) Slow sized its crossing from scheduler.periodUs() (busy + 5 ms), leaving out the slot's CPU work over 384,000 pixels — the slot's real duration (scene update through commit) is measured and the larger of the two is used.
  - `[medium]` `[patch]` (blind 3, edge E2) the dwell assumed two doses every slot; a held transition gets one — `GrayPreset::longestRunSlots()` (held: dose; else half rounded up) sizes the dwell; tests and wording corrected.
  - `[medium]` `[patch]` (blind 4) Static's flip (shade + 2) exercised only K↔L and D↔W — the offset rotates +1, +2, +3 so three flips cover all twelve transitions; logged; pinned.
  - `[low]` `[patch]` (blind 5, edge E3, verification-gap other) the session-cap clean in Static reset the model but not the flip count — restartFlips() on the cap clean, logged as a restart.
  - `[low]` `[patch]` (blind 6, edge claim C3) the legacy-retarget log named both final values even when a new key set one; the header comment disagreed — the loader reports which directions the old key filled; only those are logged.
  - `[medium]` `[patch]` (blind 7, edge claim C2) a saved file records no controller base, so an SSD1677-tuned file would silently replace the UltraChip table — Save writes `base`; a differing base logs one LOG_ERR and still loads; codec pinned.
  - `[low]` `[patch]` (blind 8) comments and the doc still said a tap pauses while D12 says touch routing is dead in the view — reworded: taps pause only where touch routes, which the X4 Pro's view does not.
  - `[low]` `[patch]` (blind 9) the Controls string hard-coded the side keys (they swap with orientation) and omitted the edge swipe — worded by logical action; the doc keeps the key names.
  - `[low]` `[patch]` (blind 10) the 120 s cap applied silently — the `gray slow:` line gives the dwell in slots and says "capped".
  - `[low]` `[patch]` (blind 11) ultraChipDefaults keeps 100 % / 100 % retarget after D12 showed the directions differ — stated as an uncalibrated placeholder in the header and the doc.
  - `[low]` `[defer]` (blind 12, verification-gap 2) the controller → PresetBase choice in DisplayLabActivity::onEnter and the legacy-flag log have no test — device-only; joins dlab-c2's deferred device-wiring family.
  - `[low]` `[patch]` (blind 13) the doc's `reverse_percent` row was ambiguous with two percents — it follows the new target's direction.
  - `[low]` `[patch]` (blind 14) two duplicated Toward switches — one selector.
  - `[medium]` `[patch]` (blind 15) Slow's re-derivation 0.4 s into the crossing jumped the rectangles back and retargeted their first pixels mid-transition — the rectangles wait off-screen until the first delivered slot is measured; pinned.
  - `[medium]` `[patch]` (verification-gap 1) the per-slot decision (reapply after a Static settle or Slow's measurement) lived only in device code; dropping `sceneStale = true` broke the flip with every host test green — `TransitionScene::slotDone(driven, driveDone, periodUs, elapsedMs)` returns {reapply, settled, measured}; the activity applies it and the test drives it.
  - `[medium]` `[defer]` (verification-gap 3) the live-view input order (Back before the swipe guard, NavPrevious stops, NavNext pauses) has only the owner's hardware check — no harness compiles the lab; deferred with the device-wiring family.
  - `[low]` `[patch]` (edge E4) a file holding a dose above 12 stays version 1 and older firmware rejects it whole — documented in the file section (no bump: files at or under 12 stay readable everywhere).
  - `[low]` `[reject]` (edge claim C1) the plan named `retargetPercentFor`; the code has `retargetPercent(Toward)` — plan wording, corrected in place by the implementer at the orchestrator's request.
  - `[low]` `[reject]` (verification-gap, the +16 B) the gate's games delta moved +16 B with the lab excluded — the lens traced it: the shared I18n string table grows about 708 B in both images (gen_i18n keeps the lab's keys in every build) and firmware.bin is padded to 16 B, so the two images round differently; no `GameDisplayLab` symbol is in the games-off ELF. Settles with `size -A` on both ELFs at both commits; within budget (18,272 B spare).
  - `[low]` `[reject]` (intent 1) the Speed readout stays beside a new Mode row where the story said "becomes Mode" — the story misnamed the readout; the figures are what the owner reports and what the c2 hardware check reads.
  - `[low]` `[reject]` (intent 2, 3, 4, 5, 7, 8, 9) Slow per the story's "i.e." clause (the literal formula was wrong), Static's rectangle a third of the width, "settled" as a slot that drove nothing, retarget direction by current level, legacy key as defaults-fill, controls as specified, the Clean row repainting the rows, the 3 B/pixel model — settled readings, recorded; no change.
  - `[low]` `[reject]` (intent 6) UltraChip defaults keyed on the probed controller and used for partial and malformed files too — the better reading (a UC panel's fallback is its own table); the base key patch above makes the file's origin explicit.
  - `[low]` `[reject]` (intent 10) the device-only surfaces (input, Clean row, afterSlot glue, tuning rows, onEnter selector, preset log) are untested — the story says so; the pure per-slot step patch above moves the largest piece into tests.

## Design Notes

**Doses left needs a byte.** The model packs doses left (4 bits), target and source (2 each) into one byte; 24 needs 5 bits. Freezing the hold decision at retarget would keep 2 B a pixel but strip `source` and leave `preset` unused in `buildMasks`/`commit`; instead the pixel grows to 3 B (level, doses left, target | source << 2): +384,000 B PSRAM at 800x480 (8 MB on the X4 Pro), semantics and signatures unchanged.

**Retarget rule.** `budget = oldRemaining != 0 && base != 0 ? scaledDose(base, toDark ? dark : light) : base`, `toDark = t * 64 < current` (the darkening side of `buildMasks`'s `lighten`). History reversal percents still override.

**Codec fallback.** `retarget_percent` is held aside while parsing; after the object, each direction without its own key takes it (range-checked as before). `encodeGrayPreset` writes only the new keys, so a saved file drops the old one. Version stays 1; an older firmware reading a new file skips the unknown keys.

**Defaults.** `DisplayLabActivity::onEnter` picks `PresetBase::UltraChip` when `isUltraChip(display.getController())` and passes it to `GrayPresetFile::load`, before the render lock. On this branch an UltraChip controller has no scan backend and gets the stock bench, so the path is live on the experimental branch (dlab-a2/b2).

**Rows and input.** Story's "Speed row becomes Mode" read as: a Mode row (the selector the story asks for) under Run; the Speed row stays (it reports `ms_per_scan`, which Slow mode and the c2 hardware check use). Mode steps on tap or Confirm, default Static, kept for the screen's visit. `handleCustomInput` while shown:
```
if (released(Back) || released(NavPrevious)) { stopSweep(); leaveView(endText()); return true; }
if (wasSwipe() != None) return true;
if (released(Confirm) || pressed(NavNext)) { togglePause(); return true; }
```
NavPrevious stops on release (as Back) so its press and release both stay in the view. Replay and clean requests are no longer sent mid-run; `serve` keeps handling them (the protocol is unchanged; Replay starts every run).

**Static.** Rectangles a third of the width, centred, never move (`update` is then a no-op). After each slot `scene.staticSlotDone(slot.driven > 0)`: a slot that drove nothing returns `{flip, slots}` (flip 0 is the first paint; slots that drove since the pattern was applied), flips the scene, and the task logs and sets `sceneStale`, so the next slot's `applyAll` retargets every pixel to `(shade + 2) mod 4` with the full base dose. No `SweepControl` change: settling is a property of the model, read on the task.

**Slow.** `setMode(Slow, period)` sets `sweepMs = slowSweepMs(period, logicalW)`. `startRun` passes the screen's last measured period, or `MIN_PERIOD_US` before any. After a slot whose drive scan was delivered, `scene.measureSlow(true, scheduler.periodUs())` re-derives once per run; on true the task stores the period, logs `gray slow: slot period N ms, crossing N ms`, and sets `sceneStale` (the rectangle jumps to the new geometry's position at the same clock; `update` never mixes geometries).

**Clean row.** Loop task, no sweep task: `clearTapFlash`; `{RenderLock; clearScreen(0xFF); lab.cleanNow();}`; log; `requestUpdate` (the rows repaint over white).

**Device-only:** input, the Clean row, the rows, the task hooks. Host-tested: preset, codec, model, scene geometry and sequencing.

## Verification

**Commands:** (with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache` and AGENTS.md's locks)
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass.
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py` -- expected: pass.
- `./bin/clang-format-fix` twice -- expected: the second run changes nothing.
- `pio run -e x4pro`, `-e default`, `-e sticky`; `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: success, no defects.
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`, at 39903ffb and on the change -- expected: same games delta and static RAM.
- Plain x4pro image: `Flash:`/`RAM:` lines and `size -A` at 39903ffb (built before any edit) and on the change -- record the delta.

**Manual checks (if no CLI):**
- Hardware (owner): side Down pauses within a slot, side Up stops; Static settles and flips without a flash, logging `gray static: flip N settled after M slots`; Slow logs its crossing; Clean whitens the panel; an UltraChip unit without a file logs the UltraChip defaults.

## Auto Run Result

**Summary.** The transition test after the owner's first UC8179 calibration run (D12): side-button controls while the
view is up (Next or Confirm pauses and resumes, Previous or Back stops; Back checked before the swipe guard), a Clean
row, a Mode row (Static: paint, settle, flip through +1/+2/+3 so three flips cover all twelve transitions; Slow: a
crossing sized from the measured slot and the preset's longest run, rectangles waiting off-screen until the first
slot is measured; Fast: the 5 s sweep), retarget split into toward-dark and toward-light, dose cap 24 (the model at
3 B a pixel), the owner's table as the UltraChip starting preset, and a `base` key in the preset file.

**Review.** Four lenses, orchestrator-run: 36 findings, 15 patched (6 medium at entry verdict: the slot's real
duration, the dwell from the preset, the full-table flip rotation, the mid-crossing jump, the preset base key, the
pure per-slot step), 3 deferred, 13 rejected with reasons in the triage log, 0 false. Follow-up review recommended:
`true` (two or more medium entries patched).

**Verification (orchestrator, on the patched tree).** Host suite 2,208/2,208; `check_layers.py` and
`check_upstream_touches.py` pass; `clang-format-fix` clean. `pio run -e x4pro` 6,255,358 B flash (+6,252 B over
39903ffb's 6,249,106 B), 104,232 B RAM (+0); `default` and `sticky` build; `pio check -e x4pro` no defects; flash
gate (D6): within budget, static RAM 240 B to spare, `objects` clean; the gate's +16 B games delta is image padding
on the shared string table (triage log). The simulator compiles the lab out, so no screenshot.

**Residual risks.** Every live-control, Clean-row and mode behaviour is device-only and verified on the owner's
UC8179 through the experimental branch; the UltraChip retarget defaults are placeholders until a Static run gives
per-direction figures.

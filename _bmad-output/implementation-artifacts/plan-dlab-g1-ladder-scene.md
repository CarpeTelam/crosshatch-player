---
title: 'dlab-g1: the ladder scene (a Ladder mode measuring 1..24 pulses from white and from black, and the return)'
type: 'feature'
ticket: 'story-dlab-g1-ladder-scene'
created: '2026-10-18'
baseline_revision: '7ec81942ba350ce2bca5018755853d815be0d3cb'
status: 'in-review'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-g1-ladder-scene.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
warnings: ['oversized']
deferred: []
---

<intent-contract>

## Intent

**Problem:** D14 takes the engine to seven levels (black, five grays, white), whose 7×7 table (42 doses) cannot be tuned
by eye from flips. What is missing is the panel's response curve: how dark n one-frame pulses from white leave a pixel,
how light n pulses from black, n = 1..24 at the preset's rate, and whether n pulses back restore the origin.

**Approach:** A fourth Mode, **Ladder** (Static → Slow → Fast → Ladder), that paints a measurement pattern in two
stages, each ending in a dlab-c4 hold (marker, Hold row honoured): a base (bottom half driven to black with the preset's
W>K dose, `gray ladder: base painted`); stage 1, 24 columns per half, column n getting exactly n one-frame pulses
toward the opposite shade, one per slot, through a new pure `GrayModel::driveRect`, then `gray hold: ladder 1`; stage 2,
each column driven back n pulses, then `gray hold: ladder 2`; Next ends the run ("Finished", `gray ladder: done`), Run
replays. Decisions pure in `TransitionScene`, the dose entry point pure in `GrayModel`, both host-tested.

## Boundaries & Constraints

**Always:** every scan through `DisplayLabActivity::scan` on the sweep task; D4, D5 unchanged; dlab-c4's hold
mechanics reused (TransitionScene's phases, marker and `slotDone`; `SweepControl::holdRun`; `afterSlot`), not a second
hold path; `GrayModel::retarget` untouched (`driveRect` beside it); the Tuning screen, preset file and `GrayPreset`
unchanged (the ladder reads the preset's rate, polarity and, for the base, its W>K dose); locals under 256 B, no
per-slot allocation; `tr(STR_…)` keys in `english.yaml` only; C3 `default` and `sticky` build; flash gate (D6) +0; the
doc's "What to photograph" gains the two ladder photos and how to read them.

**Never:** `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam, `freeink-sdk/`; a ledger row;
generated I18n files; the stock bench, frame-period, polarity screens; Static, Slow and Fast behaviour.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|---|---|---|---|
| Ladder mode | Mode row stepped past Fast | "Ladder"; Run paints the base, then stage 1 | — |
| Stage 1 | 24 slots after the base | column n has had n pulses (one per slot, one scan per slot), end-aligned: its pulses are the stage's last n slots, every column's last pulse in the same slot; settle; hold `ladder 1`; the line names the pulse interval, the longest slot and the base's 6>0 dose | Hold off: holds too (the Ladder always holds; the Hold row reads "Always in Ladder", the start line says so); a manual pause between two pulse slots is counted and the first four stage slots it fell after are named (a pause before the first pulse or after the last is not) |
| Stage 2 | Next while holding | columns driven back n pulses, end-aligned; settle; hold `ladder 2` | Hold off: holds too |
| End | Next while holding at stage 2 | marker back, the run ends, rows return with "Finished", Run replays | — |
| Layout | logical width W ≥ 48 | 24 columns of W/24 − 1 px, each followed by a 1 px gutter of the half's origin (480: 19 px; 800: 32 px) | — |
| Narrow screen | logical width < 48 | columns at least 1 px, as many as fit (min(width, 24)), no gutter, logged; a stage is still 24 slots | — |
| Session cap mid-stage | cap reached | the cap clean runs; a hold in progress is dropped; the ladder restarts from the base and holds at `ladder 1` first again (during the base, mid stage 1 or 2, the marker going out at either hold, its way back from `ladder 1`), logged | the clean cuts off the marker's way back from `ladder 2`: both photos taken, the run ends as Finished |
| Hold marker (every mode) | a photo spot | a pause symbol: two bars inside markerRect (each (w − 8) / 2 wide, an 8 px gap, full height: 12, 8, 12 at 32 px), driven to the band's opposite shade and back; the gap is never driven nor counted for the settle | a box too narrow for both bars and the gap: one bar, the whole box |
| Fast paint (owner ask) | Fast run starts | the rectangles wait off-screen until a slot drives nothing (`gray fast: paint settled after N slots`); the sweep clock's crossing starts there; Hold on: a `fast painted 0` hold first | Hold off: the sweep starts the slot after the settle; a cap clean dropping the painted hold starts the crossing without its photo |
| Shade names (owner ask) | Tuning rows, `gray` log lines, the doc | shades by D14's level numbers: K "0", D "2", L "4", W "6" (Dose 0>2, marker 4 over 0) | the preset file (no names) and the stock bench's labels unchanged |
| driveRect | rect, Toward::Dark, 5 | every pixel in the rect: target K (W for Light), 5 doses left, level kept; each dose drives toward that end (black mask for K, white for W) even on a pixel already there | out-of-model clipped; doses 0 changes nothing, above DOSE_MAX clamps |

</intent-contract>

## Code Map

- `lib/GameDisplayLab/GrayModel.{h,cpp}` -- `retarget` (untouched: its source byte is the shade nearest the level,
  history origin/steps reset, weak bit set or cleared), `retargetRect` (the clipping pattern), `buildMasks`/`commit`
  (`followsUp`: the follow-up scan only for a pixel whose (source, target) hold bit is off), `deliverDose` (lands
  exactly on the target after `remaining` doses; a pixel already at the goal still takes each dose, level unchanged).
- `lib/GameDisplayLab/GrayPreset.h` -- `Toward`, `holdBit`, `holdMask` (uint16_t), `DOSE_MAX` 24; read-only.
- `lib/GameDisplayLab/TransitionScene.{h,cpp}` -- `SweepMode`/`nextMode`, `HoldSpot`/`holdSpotName`, `HoldDrop`/
  `holdDropName`, `SceneStep`, `setMode`, `setHold`/`dropHold`, `rectX`, `targetAt`, `markerRect`, `markerBandShade`
  (inline), `slotDone` (phase routing; MarkerBack runs `flipPending_`'s flip), `patternStep`, `cancelHold`,
  `restartFlips`, `retargetLogical`/`toPanel` (logical → panel).
- `lib/GameDisplayLab/SweepControl.h` -- `holdRun`; unchanged.
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- `modeText`/`modeName`, `SweepEnd`/`endText`,
  `serve` (replay: start line, `restartFlips`), `runSlot` (cap clean: `cancelHold`, Static's restart; `runGraySlot`
  call), `afterSlot` (logs, the held block). Keep every dlab-c2/c4 guard (Stop before refusals in the scan lambda,
  `endSweepLocked` order, heldForPhoto before `holdRun`).
- `test/game_display_lab/test_transition_scene.cpp` (`SceneBench`, `TaskSim` -- commits without masks) and
  `test_gray_model.cpp` (`Bench`, `FakeScan`); already in `test/game_display_lab/CMakeLists.txt`.
- `lib/I18n/translations/english.yaml` (ledger row 2, `STR_GAMES_*`); `docs/crosshatch/display-lab.md` (Transition
  test, Holds for photos, log lines, What to photograph, code map).

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/GrayModel.{h,cpp}` -- add `driveRect(x, y, w, h, Toward, doses)` per Design Notes; nothing
  else changes.
- [x] `lib/GameDisplayLab/TransitionScene.{h,cpp}` -- `SweepMode::Ladder` (cycle), `HoldSpot::Ladder` ("ladder"),
  `HoldDrop::LadderFromBase`, `LadderStage`, `LadderHalf`, `LadderLayout`/`ladderLayout`, `ladderHeight`,
  `ladderColumn`, `ladderStage`, `restartLadder`, `ladderPulsing`, `heldRhythm`; `SceneStep` gains `ladderSettled`
  and `finished`; the ladder branch of `setMode`, `rectX`, `targetAt`, `markerBandShade`, `patternStep` (takes the
  model), MarkerBack's pending advance, `cancelHold`, `dropHold`.
- [x] `test/game_display_lab/test_gray_model.cpp` -- driveRect: bookkeeping, clipping, 0 and clamp, history and weak
  cleared, held-rhythm slots deliver one scan a slot and land on the end.
- [x] `test/game_display_lab/test_transition_scene.cpp` -- mode cycle; layout at 480, 800, 47, 30, 20, 0; column
  rects and origins in all four turns at 800×480 (no column under the marker); stage sequence with Hold on and off
  (column n's doses per slot, 24 slots per stage, holds, finished); cap restart (running and in each hold phase);
  names; `heldRhythm`. `TaskSim` runs ladder pulses with `heldRhythm`.
- [x] `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- Ladder in `modeText`/`modeName`; the slot's
  preset from `scene.slotPreset(lab.preset())` (the held rhythm while a ladder stage pulses) passed to `runGraySlot`; start
  line ("crossing none") and a `gray ladder:` layout line (and the narrow-screen line); cap clean restarts the ladder;
  `afterSlot` returns false on `finished` (`SweepEnd::Finished`, "Finished", `gray ladder: done`) and logs each stage
  settle with rate index and label, one pulse per slot, column width and gutter; class comment.
- [x] `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_MODE_LADDER`, `STR_GAMES_DLAB_FINISHED`.
- [x] `docs/crosshatch/display-lab.md` -- Mode row, the Ladder mode, its holds, log lines, the two ladder photos and
  how to read them, code map.

**Acceptance Criteria:**
- Given the host suite, when it runs, then every test passes, the new driveRect and ladder tests included.
- Given a Ladder run with Hold on on the device, when stage 1 settles, then each half shows 24 columns, column n n
  pulses from its origin, the dash shows, and the log names rate, rhythm and column width; Next runs the return and
  holds again; Next ends the run with "Finished".
- Given `default`, `sticky` and the flash gate, when built, then both compile and the games delta and static RAM equal
  7ec81942's, measured the same way.

## Implementation Notes

Built 2026-10-18 in the worktree `/home/user/crosshatch-laneg1` (branch `dlab-lane-g1`, base 7ec81942) under the
orchestrator's fallback (orchestrated-epics.md § 3: no nested subagents): plan and implementation in the build agent's
own context, every change staged (`git add -A`), nothing committed; the four lenses are the orchestrator's. Every task
as planned. Resolution: `tickets.py find dlab-g1` exits 1 ("type None is not one of initiative, epic"): the epic's
stories are intent files, not `tickets.toml` entries (epic file, header), so the story file and the plan path the
dispatch named are the intent and the plan, as for dlab-c4 (`ticket` = the story's stem).

- `GrayModel::driveRect` beside `retarget` (untouched); its nearest-shade rule is a new anonymous helper
  (`nearestShadeOf`), which `retarget` does not use, so its code stays as it was.
- `TransitionScene`: `patternStep` now takes the model (the Ladder's stages drive it); `ladderStep`, `advanceLadder`,
  `driveLadder` are private; `cancelHold`'s if-ladder on the spot became `dropBeforeTheWayBack`, an exhaustive switch
  (its guard line kept). `markerBandShade()` is K in Ladder, so the marker goes K → L → K on the rows under the bottom
  half. `ladderLayout()` (member) next to the static `ladderLayout(int)`.
- The activity: `afterSlot` returns false on `finished` (`SweepEnd::Finished`, "Finished"); `runSlot` returns false
  after it, so `sweepRun` ends as for any other end and the loop's `leaveView(endText())` repaints the rows. The
  resumed line says `the ladder is done` instead of `the run goes on` on that slot. The layout line at a Ladder run's
  start, the narrow-screen line (columns under 24 or 1 px wide), the stage lines (rate index with
  `backend->rateLabel`, swap, one pulse per slot, column width, gutter) and the cap's restart line are `gray ladder:`.
- Strings: `STR_GAMES_DLAB_MODE_LADDER`, `STR_GAMES_DLAB_FINISHED` (ledger row 2).
- Tests: `TaskSim` commits ladder pulse slots with `heldRhythm` (as the task's `runGraySlot` does); the full-size
  geometry test runs 800×480 in all four turns (about 0.3 s on the host).
- Device-only code (`TransitionTestActivity`) is not compiled on the host: its firmware build is the orchestrator's.

Review batch (2026-10-18, the orchestrator's four lenses, applied on top of the WIP commit 44a2ba65, staged, not
committed): 1 `lightens()` changed the direction rule `buildMasks` and `commit` share (a W target lightens and a K target
darkens whatever the level; the same result for every state retarget leaves, since it never leaves doses on an
on-target pixel), pinned by a
Light-on-white pixel in `HeldRhythmSlotsSendOneScanAPulseAndLandOnTheEnd` (white mask, never black); 2 end-aligned
columns (`ladderStep` counts stage slots, `driveLadderColumn` per slot; `driveLadder` removed), tests check every
pixel's doses before every stage slot (`expectStageSlot`), in all four turns on a small panel
(`LadderStagesAreEndAlignedInEveryTurn`) and a narrow one (`NarrowLadderStillRunsTwentyFourSlotsAStage`); 3 a gutter at
every width with room; 4 `holdsAtSpots()`, the hold test runs with Hold on and off; 5 and 6 `ladderPauses`,
`ladderSlotUs`; 7 `ladderCleaned`, pinned in `SessionCapCleanRestartsTheLadderFromTheBaseOrEndsItAfterBothPhotos`; 8
`slotPreset` (the slot's preset for the task and the sim); 9 the header's Mode list and "four shade bands"; 10
`rectW_` 0 and "no rectangles" in the start line; 11 the Ladder's 32 x 16 marker and `ladderHeight() = H - 16`,
tested in every turn at 800 x 480; 12 `none` for an empty half. Design Notes and the matrix updated; the doc's Ladder
section, holds, cap and log lines rewritten.

Follow-up batch (2026-10-18, on top of the story commit dce6f20f, staged, not committed): the orchestrator's items 1-10
and the owner's asks 11-13. 1 comments say the Ladder always holds (`LadderStage::Done`, `afterSlot`, the activity's
header, `setHold`, `SceneStep::hold`); 2 `ladderMidStage()` and `ladderStageSlots()`, pinned by
`LadderMidStageIsBetweenAStagesFirstAndLastPulse`; 3 the settle line names the first four stage slots pauses fell
after; 4 `ladderIntervalUs`, the longest start-to-start interval between a stage's pulse slots, tracked in `runSlot`
from the slot start times; 5 the Hold row reads "Always in Ladder" (`STR_GAMES_DLAB_HOLD_LADDER`) while Mode is Ladder;
6 `LadderFromBase`'s comment and the doc: the restart holds at `ladder 1` first; 7 three more cap cases (the marker
going out at `ladder 2`, mid stage 2, during the base), and the mutation check: `ladderCleaned` reduced to `stage ==
Return` fails `SessionCapCleanRestartsTheLadderFromTheBaseOrEndsItAfterBothPhotos` (run, then restored); 8 the doc's
reading guide (the base's black, temperature, rest before the first pulse, the pulse interval, pauses); 9 the doc's
Ladder bullet in sub-bullets, two ragged lines fixed; 10 this plan's wording. 11 `markerBars()` (`MARKER_GAP` 8): the
marker is a pause symbol inside the unchanged `markerRect()`; `retargetMarker`, `markerSettled` and `targetAt` use the
bars; the marker tests check the bars and an untouched gap in every turn at both sizes (and the Ladder's). 12
`shadeName` replaces `shadeLetter` (K "0", D "2", L "4", W "6"): Tuning's rows, the `gray hold:` lines, the stage line's
`base 6>0 dose`; the doc's tables and text with a legend; `ShadeNamesAndLogNames` pins it. 13 Fast waits for the
paint as Slow does (`painted_` and `crossStartMs_` are Slow's renamed state, shared; `paintSlots_`;
`HoldSpot::FastPainted`, `SceneStep::fastPainted`, `paintPending()`; `gray fast: paint settled after N slots; the
sweep starts`); the Fast geometry tests settle the paint first (`sweepingFast`), and
`FastRectanglesWaitUntilThePaintSettles` and `FastHoldsAtThePaintThenOncePerSweepInItsPause` pin it.

## Plan Change Log

## Review Triage Log

### 2026-10-18 — Review pass (orchestrator-run lenses, per orchestrated-epics.md § 3's fallback: the session's subagents cannot nest)
- verdicts: 24 findings — high 0, medium 2, low 11, false 0, maybe-false 0, rejected-by-rule 11
- findings:
  - `[medium]` `[patch]` (blind 1, edge E1, verification-gap other) `driveRect` toward white on a pixel already white lands in the black mask (`buildMasks`/`commit` lighten only when `target * 64 > level`, and 192 > 192 is false) while the header promises an on-target pixel takes the new count; the ladder never hits it today, the API would — a pixel whose target is W lightens and whose target is K darkens whatever its level; pinned with a Light-on-white scan test.
  - `[medium]` `[patch]` (intent, column timing) the columns were start-aligned (column n pulsed in slots 1..n, so column 1 idled some ten seconds before the photo while column 24 had just been pulsed), an unequal rest the intent did not choose — end-aligned: column n's pulses land in the stage's last n slots, every column's last pulse in the same slot, both stages; pinned.
  - `[low]` `[patch]` (blind 2) at 480 wide the 24 columns touched (24 × 20) and left no untouched reference shade — every width keeps a 1 px gutter of the half's origin shade (columns pitch − 1 wide), so columns are countable against their origin.
  - `[low]` `[patch]` (blind 3) with Hold off the ladder ends with nothing to photograph (stage 1's end lasts one slot; stage 2's end wipes the panel) — the ladder holds regardless of the Hold row, logged at the run's start.
  - `[low]` `[patch]` (blind 4) a manual pause mid-stage breaks the one-pulse-per-slot rhythm silently — the activity counts pauses during a stage and the settled line says so.
  - `[low]` `[patch]` (blind 5) the settled line lacked the slot period and the W>K dose that painted the base — both logged.
  - `[low]` `[patch]` (blind 6, edge E2) a session-cap clean cutting off the marker's way back at `ladder 2` restarted the whole ladder though the photo was taken — the run finishes; the doc says the cap path is practically unreachable in a ladder run (the run's clean resets the budget; a run is some 150 scans against 2,400).
  - `[low]` `[patch]` (blind 8) the slot's preset choice (`ladderPulsing() ? pulsePreset : preset`) was copied into TaskSim — `TransitionScene::slotPreset(const GrayPreset&)` serves both the task and the sim; the cached copy goes.
  - `[low]` `[patch]` (blind 9) stale comments (the Mode row's list, the header's four bands) — updated.
  - `[low]` `[patch]` (blind 10) `rectW_` doubled as the column width for the start line — stays 0 for the Ladder; the layout line carries the width.
  - `[low]` `[patch]` (blind 11) the ladder's height came from the other modes' lane geometry through `markerRect()` (a lane margin of 0 would give a 0-high marker that settles at once) — in Ladder mode the marker strip is `MARKER_PX` tall and the halves split above it.
  - `[low]` `[patch]` (blind 12) the layout line printed "0--1" for a half with no rows — guarded.
  - `[low]` `[defer]` (blind 7) `serve`'s clean branch does not restart the ladder (latent: `requestClean` has no caller) — joins dlab-c4's note: whoever adds a caller drops the hold and restarts the scene in `cleanToWhite`.
  - `[low]` `[reject]` (intent 1–7) the pure layer tested through TaskSim with the activity glue device-only (the deferred family), the base painted through the preset's own W>K retarget (the story's stated exception), the details on a separate `gray ladder:` line, the halves split above the marker strip, the gutter rule by example, the model landing n pulses on the end shade (it models the stimulus, the photo measures the response) — settled readings, recorded; the story's stray "eight levels" is the orchestrator's wording, fixed at the merge.
  - `[low]` `[reject]` (verification-gap) no gaps filed; the activity's end path and log lines are the deferred device-wiring family.

### 2026-10-18 — Follow-up review pass (same four lenses, orchestrator-run, over commit dce6f20f; two mediums were patched) and the owner's three asks (D15)
- verdicts: 22 findings — high 0, medium 0, low 11, false 0, maybe-false 0, rejected-by-rule 11
- findings:
  - `[low]` `[patch]` (follow-up blind 1) comments still said the Ladder holds only with Hold on — the Ladder always holds; the activity header follows.
  - `[low]` `[patch]` (follow-up blind 2, edge 1) `ladderPauses` counted a pause before a stage's first pulse or after its last — `ladderMidStage()` gates it; pinned.
  - `[low]` `[patch]` (follow-up blind 3) the settled line did not say where a pause fell — the first four stage slots paused after, then "…".
  - `[low]` `[patch]` (follow-up blind 4) a slot the scheduler skipped stretched the pulse interval unreported — the longest start-to-start interval between pulse slots is logged as the interval.
  - `[low]` `[patch]` (follow-up blind 5) the Hold row misled in Ladder mode — it reads "Always in Ladder" there.
  - `[low]` `[patch]` (follow-up blind 6, edge 2) `LadderFromBase`'s comment and the doc promised a hold "at that stage's end" after a drop at `ladder 2`; the restart holds at `ladder 1` first — reworded.
  - `[low]` `[patch]` (follow-up blind 7, verification-gap 1, edge 6) `ladderCleaned`'s condition was pinned only by its true case (cutting it to `stage == Return` passed) — the marker going out at `ladder 2`, mid stage 2 and during the base are tested; the cut now fails.
  - `[low]` `[patch]` (follow-up blind 8, 9, 11) the reading guide lacked the base's dependence on the 6>0 dose, the rest before the first pulse under end alignment, the pulse interval and pauses, and room temperature (the panel's sensor is a backend read, outside this story) — written; the Ladder bullet split into sub-bullets; two ragged lines fixed.
  - `[low]` `[patch]` (follow-up edge 4, 5) the plan named a `pulsePreset` member that no longer exists and called the direction change "nothing else" — plan wording.
  - `[low]` `[patch]` (owner, D15) the marker is a pause symbol: `markerBars()`, two bars with an 8 px gap inside `markerRect()`, the gap never driven; shades are named by their seven-level number (`shadeName`: 0, 2, 4, 6) in Tuning, the logs and the doc; Fast waits for the paint to settle before its rectangles enter, with a `fast painted` hold — all pinned.
  - `[low]` `[reject]` (follow-up blind 10) `nearestShadeOf` duplicated in `retarget` — the story leaves `retarget` untouched; by rule.
  - `[low]` `[reject]` (follow-up edge 3, intent) the Ladder ignores Hold off — the first pass's decision (nothing to photograph otherwise); the row says so.
  - `[low]` `[reject]` (follow-up intent 1–6) the Ladder's departures from the story's wording (always holds, 19 + 1 px columns at 480, the halves above a 16-row strip, the second cap outcome, a stage of 24 pulse slots plus the settle slot, the direction rule shared by every mode) — review-pass decisions, recorded here; the story is amended at the merge.
  - `[low]` `[reject]` (orchestrator's item 13 note) "no mode exercises the retarget split" — wrong as stated: Fast's trails are still retargeted mid-transition; the doc and Design Notes keep Fast as the mode that exercises it.

## Design Notes

Sources: story dlab-g1 (Approach, Matrix), D13 (marker; amended to a dash in the bottom margin, never on a measured
row), D14 (why a ladder; rate 0, 0x07 the fallback), dlab-c4's result (phases, marker, `holdRun`), dlab-c3's (dose cap
24, the slot ~0.45 s).

- **driveRect** (story: "sets the doses directly without the table, level and target bookkeeping as a retarget's"):
  clipped like `retargetRect`; per pixel: level kept, remaining = min(doses, DOSE_MAX), target K (Dark) or W (Light),
  source = the shade nearest the level (retarget's rule, so `holdBit` and settle detection work unchanged); history
  origin ORIGIN_NONE and step counters 0 (a driven pixel has no route to adjust); weak bit cleared (a ladder pulse is
  the plain one-frame drive). doses 0: nothing. Applies even when the target equals the current target (unlike
  retarget's no-op): the pulses are the point. Direction (review batch 1): `buildMasks` and `commit` share `lightens`:
  toward white for a W target and toward black for a K target whatever the level, so a Light pulse on a pixel already
  at 192 goes to the white mask (the level comparison alone put it in the black one); a middle target keeps the level
  comparison, and retarget never leaves doses on an on-target pixel, so its pixels drive as before.
- **One pulse per slot** ("the held rhythm, so each pulse is a scan"): `buildMasks`/`commit` give a pixel a second dose
  when its hold bit is off. During a ladder stage the task passes `TransitionScene::slotPreset(preset)` (review batch
  8: `heldRhythm(preset)`, a copy with every hold bit set kept in the scene, while `ladderPulsing()`; the preset
  itself otherwise) to `runGraySlot`, so the follow-up mask is empty and `runGraySlot` sends one scan; `TaskSim` uses
  the same call. The base and
  the marker run on the preset as is (the base is "the preset's full W>K dose" (6>0), the transition Static paints; the
  marker is dlab-c4's). `ladderPulsing()` is true only in Running with stage Ladder or Return, and a stage starts only
  after the previous phase settled, so no pixel ever spans the two presets.
- **Layout** (story: a 1 px gutter where the width allows; < 48 → at least 1 px, as many as fit; review batch 3: a
  gutter at every width that has room, so the columns can be counted and an untouched strip of the origin sits beside
  each): `pitch = W / 24`; for a pitch of 2 or more, columns `pitch - 1` px with a 1 px gutter (480: 19 px; 800: 32
  px); a pitch of 1 (24..47 px): 1 px columns, no gutter; below 24 px, `W` columns of 1 px. Column n at logical x
  `(n - 1) * pitch`; leftover pixels on the right hold the origin.
- **Rows and the marker:** in Ladder `markerRect()` is `MARKER_W` x `MARKER_PX` at the bottom-right (review batch 11:
  no lanes to keep clear of, so the ladder does not depend on the lane geometry and the marker is never 0 rows high;
  the other modes keep dlab-c4's dash). The halves share the rows above it (`ladderHeight() = markerRect().y`, `H -
  16`): top half
  `[0, A/2)` from white, bottom `[A/2, A)` from black; the marker rows `[A, H)` belong to the bottom half of the
  logical screen, so they hold black, the marker sits on them (`markerBandShade()` K in Ladder, out shade L), and it
  never covers a measured column (D13's amendment: the dash shares no row with what is photographed).
- **Sequence** (pure, `ladderStep`): Base: a slot that drove pixels counts; a slot that drove none → settled (stage
  Base, slots) and `advanceLadder` (stage 1), no spot. A stage starts with no slot run and column 24 driven
  (`driveLadderColumn`); each stage slot counts, and after stage slot k (from 1) column 24 − k is driven, so column n
  is driven before stage slot 24 − n (from 0) and its n pulses are the stage's last n slots (review batch 2: every
  column's last pulse in the same slot, the same rest before the photo). After 24 stage slots the first slot that
  drove none settles the stage (`ladderSettled`, 24 slots) with spot `HoldStart{Ladder, 1|2}`; a narrow screen's
  missing columns leave early stage slots empty, which still count. The Ladder always holds (review batch 4:
  `holdsAtSpots()` is Hold on or Ladder; with Hold off there is nothing else to photograph): the advance waits
  (`ladderPending_`, as `flipPending_`) for MarkerBack's arrival. Advance: Ladder → Return (column 24 driven back
  toward the origin); Return → Done, `step.finished`. The end therefore drives the marker back first, as every
  dlab-c4 resume does, then ends (the rows' FULL refresh follows).
- **Cap** (story: "the ladder restarts from the base, logged"): `cancelHold` drops a ladder hold as
  `LadderFromBase` ("the ladder restarts from the base"; MarkerBack stays `WayBackCutOff`), `dropHold` clears the
  pending advance, and the task calls `ladderCleaned(dropped)` after the clean: it restarts the ladder from the base
  (as Static's `restartFlips`), logging `gray ladder: restarted from the base after the session-cap clean`, and the
  reapplied scene paints the base again; except that a clean cutting off the marker's way back from `ladder 2`
  (`WayBackCutOff` in the Return stage) ends the run as Finished, since both photos are taken (review batch 7). The cap
  path is practically unreachable in a ladder run: the run's own clean resets the budget, and a run is some 150 scans
  against 2,400.
- **The task's lines** (review batches 5, 6, 10, 12): the start line names "no rectangles" (rectW stays 0 in Ladder;
  the layout line carries the columns) and that the Ladder always holds; the layout line prints `none` for a half
  without rows; each stage-settled line adds `paused N times` (manual pauses during the stage's pulses, counted in
  `serve` while `ladderPulsing()`; 0 omitted), the stage's longest pulse slot in ms (tracked in `afterSlot` before
  `slotDone`, over the stage's slots that drove pixels: the settling slot drives none, so its period is the scheduler's
  floor) and the preset's W>K dose that painted the base; both counters clear at each stage's end, a run's start and
  the cap's restart.
- **Follow-up batch (2026-10-18):** a pause counts only between two pulse slots of a stage (`ladderMidStage()`:
  pulsing, 0 < stage slots < 24), since one before the first pulse or after the last leaves the rhythm whole; the
  stage slot it fell after, s, marks columns n > 24 − s as carrying the gap. The pulse interval is the longest
  start-to-start time between two pulse slots (the activity has the slot start times), which shows a skipped slot
  period or a pause where the slot's own length does not; the doc names it the figure to read.
- **Pause-symbol marker (owner ask 11):** `markerRect()` stays the marker's box in every mode (dlab-c4's dash, the
  Ladder's 32 x 16); `markerBars()` puts two full-height bars of (w − 8) / 2 at its edges, and `retargetMarker`,
  `markerSettled` and `targetAt` read only the bars, so the gap keeps the band's shade, is never driven and never
  delays the hold. A box narrower than 10 px would leave no bar: then one bar fills it.
- **Shade names (owner ask 12):** `shadeName` (was `shadeLetter`, smaller to rename than to add) returns D14's level
  numbers, the even levels 0, 2, 4, 6, so dlab-g2's odd levels slot in without renaming. Only what the user sees
  changes: Tuning's row labels (and so its `tune` log lines), the `gray hold:` lines, the stage line; code-level
  notation (`GrayPreset.h`'s comments, enum names) and the preset file keep K, D, L, W; the stock bench keeps its own
  labels.
- **Fast waits for the paint (owner ask 13):** Slow's paint-settled state is now shared (`painted_`, `crossStartMs_`):
  Fast's rectangles stay at −RECT_W until a slot drives nothing, which starts the crossing at that slot's clock (with
  Hold on after the `fast painted` hold, the clock standing still through it), so `crossingAt` counts from the settle
  in both modes; the 5 s sweep and 1 s pause are unchanged. A cap clean that drops the painted hold is
  `SkippedThisCrossing` (the crossing starts without its photo), as Slow's painted spot. The retarget split: the
  orchestrator's note says no mode exercises it now; as built, the paint's mid-transition retargets are gone (the
  rectangles no longer land on bands still painting), but Fast's rectangles still move on before the pixels they
  retargeted settle (about 60 px a slot against a 12 px rectangle on the UltraChip), so the trails behind them are
  retargeted mid-transition and still exercise the split; the doc's "Fast is the one mode that exercises the retarget
  split" stays. dlab-c3's deferral about a deliberate mid-transition flip stands either way.
- **Rewritten guards:** `cancelHold`'s first line (`Running` or no spot → nothing) stays; its if-ladder on the spot
  becomes a switch. `afterSlot` keeps its held block order (heldForPhoto before `holdRun`, dlab-c4).

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass.
- `python3 scripts/check_layers.py` and `python3 scripts/check_upstream_touches.py` -- expected: exit 0.
- `./bin/clang-format-fix` twice -- expected: the second run changes nothing.
- After review (orchestrator, build lock, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`):
  `pio run -e x4pro`, `-e default`, `-e sticky`; `pio check -e x4pro --fail-on-defect low --fail-on-defect medium
  --fail-on-defect high`; `scripts/check_flash_budget.py` with D6 -- expected: builds, clean, games delta +0.

**Manual checks (if no CLI):**
- Hardware (owner, experimental branch): the two ladder photos at rate index 0, with the `gray ladder:` lines.

## Auto Run Result

Status: `in-review`: implemented and staged; the review (the orchestrator's four lenses) and the firmware checks are
pending; no commit.

- Summary: Ladder mode (Static → Slow → Fast → Ladder): base, stage 1 (column n: n pulses toward the opposite shade, one
  scan a slot, end-aligned), hold `ladder 1`, stage 2 (n pulses back), hold `ladder 2`, end ("Finished"); the Ladder
  holds whatever the Hold row; the cap's clean restarts from the base (or ends the run after both photos);
  `GrayModel::driveRect`; doc and strings.
- Files (staged): `lib/GameDisplayLab/GrayModel.{h,cpp}`, `lib/GameDisplayLab/TransitionScene.{h,cpp}`,
  `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}`, `lib/I18n/translations/english.yaml`,
  `test/game_display_lab/test_gray_model.cpp`, `test/game_display_lab/test_transition_scene.cpp`,
  `docs/crosshatch/display-lab.md`, this plan.
- Review: not run here (orchestrator's fallback); `lenses_ran` stays empty until the orchestrator's batch is triaged.
- Verification on the host (2026-10-18, staged tree over 7ec81942): host suite 2,233 tests, all passed (2,223 at
  dlab-c4 + 10 new: 4 `GrayModelDriveRect`, 6 Ladder scene tests; the mode-cycle and name tests extended);
  `scripts/check_layers.py` passed; `scripts/check_upstream_touches.py` PASS; `./bin/clang-format-fix` reformatted
  only this story's files on the first run (comment rewraps, one log call) and nothing on the second. No new compiler
  warning in `test/game_display_lab` (the two `-Wdangling-else` at lines 745 and 750 are dlab-c4's).
- After the review batch (staged over 44a2ba65): host suite 2,234 tests, all passed (170 in `test_scan_lut`);
  `scripts/check_layers.py` passed; `scripts/check_upstream_touches.py` PASS; `./bin/clang-format-fix` twice, the
  second changing nothing; no new warning in `test/game_display_lab`. Matrix audit after it: Stage 1 and 2 with Hold
  on and off (`LadderHoldsAfterEachStageAndEndsAfterTheReturn`, `LadderStagesAreEndAlignedInEveryTurn`), Layout and
  Narrow (`LadderLayoutAtTheX4ProsWidthsAndOnNarrowScreens`, `NarrowLadderStillRunsTwentyFourSlotsAStage`), Session
  cap (`SessionCapCleanRestartsTheLadderFromTheBaseOrEndsItAfterBothPhotos`), driveRect's direction
  (`GrayModelDriveRect.HeldRhythmSlotsSendOneScanAPulseAndLandOnTheEnd`), the marker and columns in every turn
  (`LadderColumnsAndMarkerInEveryTurnAtFullSize`).
- After the follow-up batch (staged over dce6f20f): host suite 2,236 tests, all passed (172 in `test_scan_lut`);
  `scripts/check_layers.py` passed; `scripts/check_upstream_touches.py` PASS; `./bin/clang-format-fix` twice, the
  second changing nothing; no new warning in `test/game_display_lab` (the two `-Wdangling-else`, now at lines 792 and
  797, are dlab-c4's). Matrix audit for the new rows: Hold marker (`HoldMarkerSitsInTheBottomRightCornerInEveryTurn`,
  `MarkerIsADashBelowTheLastLaneAtFullSizeInEveryTurn`, `LadderColumnsAndMarkerInEveryTurnAtFullSize`), Fast paint
  (`FastRectanglesWaitUntilThePaintSettles`, `FastHoldsAtThePaintThenOncePerSweepInItsPause`,
  `SessionCapCleanInSlowOrFastSkipsTheDroppedSpot`, `SetModeRestartsTheCrossingSpots`), Shade names
  (`ShadeNamesAndLogNames`; Tuning's labels are device code), Session cap (the extended
  `SessionCapCleanRestartsTheLadderFromTheBaseOrEndsItAfterBothPhotos`), pauses (`LadderMidStageIsBetweenAStagesFirstAndLastPulse`;
  the line itself is device code).
- Matrix audit (first pass): Ladder mode (`ModeRowCyclesStaticSlowFastLadder`, `LadderColumnsInEveryTurnAtFullSize`), Stage 1 and
  Hold off (`LadderHoldsAfterEachStageAndEndsAfterTheReturn`, `LadderWithHoldOffRunsTheReturnAtOnceThenEnds`,
  `GrayModelDriveRect.HeldRhythmSlotsSendOneScanAPulseAndLandOnTheEnd`), Stage 2 and End (the same two scene tests;
  the "Finished" row text is device code), Narrow screen (`LadderLayoutAtTheX4ProsWidthsAndOnNarrowScreens`; its log
  line is device code), Session cap (`SessionCapCleanRestartsTheLadderFromTheBase`), driveRect (the three other
  `GrayModelDriveRect` tests).
- Firmware: unmeasured (orchestrator builds after review): `pio run -e x4pro`, `-e default`, `-e sticky`, `pio check
  -e x4pro`, the flash gate with D6.
- Residual risks: the activity's changes compile only in the firmware build; the ladder's photos are the owner's
  (hardware run at rate index 0).

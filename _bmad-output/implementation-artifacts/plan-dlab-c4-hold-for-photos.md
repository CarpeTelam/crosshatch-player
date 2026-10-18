---
title: 'dlab-c4: hold for photos (the Hold row, the per-mode photo spots, the hold marker)'
type: 'feature'
ticket: 'story-dlab-c4-hold-for-photos'
created: '2026-10-17'
baseline_revision: '3ba6049bdbd2147b0bc19d4119b3c18e4dd10b3d'
status: 'built'
route: 'full'
route_source: 'pinned'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-c4-hold-for-photos.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
warnings: ['oversized']
deferred: []
---

<intent-contract>

## Intent

**Problem:** The owner photographs the panel at set moments of a transition-test run (D13): Static's settled
patterns, Slow's painted bands, its rectangles after a full dwell and the crossing's end, Fast's end-of-sweep pause.
The baseline commit (ad2d44fe) holds Static at every settle, always; the other modes never hold, and nothing on the
panel says the test is holding rather than running or stuck.

**Approach:** A Hold row (On by default each visit, not saved) gates everything. `TransitionScene` decides, purely
and per mode, when a slot reaches a photo spot, and owns the hold's phases: at a spot it retargets a marker (a 32 px dash
in the bottom margin, at the logical bottom-right) to the opposite shade of its band and stops the sweep clock; the task runs slots until
the marker settles, then holds; Next resumes, the marker is driven back and settles, then the run continues (Static's
flip only then). With Hold off nothing holds and Static flips at once (dlab-c3).

## Boundaries & Constraints

**Always:** D4 (the marker is driven by the engine's own scans, nothing drawn through the renderer while the view is
up) and D5 unchanged; every scan through `DisplayLabActivity::scan` on the sweep task; `SweepControl` unchanged beyond
the baseline's `holdRun`; the hold decisions, the marker rect and shade, and the resume sequence pure in
`lib/GameDisplayLab/TransitionScene` and host-tested; the marker's slots never reach Static's settle detection or
Slow's measurement; locals under 256 B, no per-slot allocation; `tr(STR_…)` with new keys in `english.yaml` only;
`default` and `sticky` build; the flash gate (D6) +0; no dlab-c2 or dlab-c3 deferral made worse (no new untested
decision in `serve`).

**Never:** `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam, `freeink-sdk/`; a ledger row;
generated I18n files; the stock bench, frame-period, polarity and Tuning screens; the preset file format.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Hold row | tap / Confirm on it | toggles On/Off, the row shows it; On on every visit; read at Run | — |
| Static hold | Hold on, a settle | settle line; marker out; `gray hold: static settle N`; held once the marker settles; the flip waits | Hold off: flip at once, no marker |
| Slow holds | Hold on | three per run's crossing: `slow painted` (the measured slot), `slow dwelt` (the `longestRunSlots`-th slot with the rectangles' left edge on-screen, right edge still on), `slow crossed` (first slot of the pause) | Hold off: none; a dwell spot whose rectangles reach the right edge is skipped, logged `gray hold: slow dwelt N skipped (...)` |
| Fast hold | Hold on | `fast pause N`, the first slot of each pause | Hold off: none; a pause no slot starts in is logged `gray hold: fast pause N skipped (no slot started in the pause)` (Slow's crossing end likewise) |
| Resume | Next or Confirm while held | marker back to its band's shade, settles at the still clock, then the run continues (the flip, the crossing, the pause) | Next during the marker's slots pauses as usual (`resumeHold` does nothing there); the next press resumes where it was |
| Stop while holding | Previous or Back | the view ends with the FULL refresh, marker and all | — |
| Auto-sleep while holding | the sleep timeout passes while held | no sleep for `HOLD_AWAKE_MS` (10 min) from the hold's start; after that the normal idle timeout applies and auto-sleep ends the run as any leave does | a pause asked for with Next may sleep, as before |
| Marker | any orientation | a 32 px dash in the bottom margin: logical x W-32, rows from the last lane's rectangle's bottom to H, at most 16 (8 at 480 high, 10 at 800), so never on a rectangle's rows; a 16 px corner square when the bands have no lanes; mapped with `toPanel` | — |
| Cap during marker slots | session cap | the cap clean runs as today; the hold is dropped (marker hidden, a pending flip dropped), logged `gray hold: <spot> dropped by the session-cap clean after N marker slots: <case>`, the case `holds again at flip 0` (Static, marker out), `skipped for this crossing` (Slow or Fast, marker out: the next spot holds, Slow repainting its bands without measuring again) or `photo taken, the marker's way back cut off` (any mode, marker back) | — |

</intent-contract>

## Code Map

- `lib/GameDisplayLab/TransitionScene.{h,cpp}` -- `slotDone(driven, driveDone, periodUs, elapsedMs) -> SceneStep`
  (Static settle: `settled` + immediate flip + `reapply`; Slow: `measured` once); `rectX`, `targetAt`, `applyAll`,
  `retargetLogical` (clips, then `toPanel` + `GrayModel::retargetRect`), `bandOf`, `shown`, `restartFlips`, `setMode`.
- `lib/GameDisplayLab/GrayModel.h` -- `retargetRect`, `remaining(x, y)`, `activeCount()`; unchanged.
- `lib/GameDisplayLab/SweepControl.h` -- `holdRun()` (baseline); unchanged.
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- `Row`/`ROW_COUNT`, `buildRows`, `activateIndex`,
  `stepMode` (pattern for the Hold row), `startRun` (sets the scene's mode), `serve` (resume block), `advanceClock`,
  `runSlot` (cap clean: `restartFlips` in Static), `afterSlot` (the baseline's unconditional Static hold, replaced).
  Keep every dlab-c2 guard (Stop before refusals in the scan lambda, `endSweepLocked` order, `return` after `leaveView`).
- `test/game_display_lab/test_transition_scene.cpp` -- `SceneBench`, `paintAndSettle`, the Static flip loop; already
  in `test/CMakeLists.txt`.
- `lib/I18n/translations/english.yaml` (ledger row 2); `STR_STATE_ON`/`STR_STATE_OFF` exist.
- `docs/crosshatch/display-lab.md` -- Transition test section (rows, Static's baseline hold paragraph, log lines,
  what to photograph) and the code map's `TransitionScene` entry.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/TransitionScene.{h,cpp}` -- add `HoldSpot { StaticSettle, SlowPainted, SlowDwelt,
  SlowCrossed, FastPause }` + `holdSpotName`, `HoldPhase { Running, MarkerOut, Held, MarkerBack }`, `HoldStart {spot,
  count}`; `SceneStep` gains `hold`, `held`, `resumed`, `markerSlots`; `MARKER_PX = 16`, `setHold`, `holdOn`,
  `holdPhase`, `clockStill`, `markerRect`, `markerShade`, `markerSettled`, `resumeHold`, `cancelHold`;
  `slotDone(model, preset, driven, driveDone, periodUs, elapsedMs)` routes by phase (Design Notes); `targetAt` shows
  the marker while it is out; Static's flip deferred while a hold is up; `setMode` and `restartFlips` reset hold state
  as Design Notes say -- the pure decisions.
- [x] `test/game_display_lab/test_transition_scene.cpp` -- existing calls on the new signature; new: marker rect and
  shade in all four turns; Static hold sequence (spot, held only after the marker settles, flip only after resume and
  the marker back, settle slot counts unchanged by marker slots, `clockStill`); Slow's three spots and Fast's one per
  cycle with a simulated task; Hold off: no spot, Static flips at once; `cancelHold`; marker slots not measured.
- [x] `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- Hold row after Mode (`holdOn`, true in
  `onEnter`, `toggleHold`, log `transition test hold: on|off`); `startRun` calls `scene.setHold`; `advanceClock` stands
  still on `scene.clockStill()`; `serve` calls `scene.resumeHold` when the run restarts; `afterSlot` logs the spot,
  stops on `held` (the baseline's stop block), logs `resumed`; the cap clean calls `cancelHold`; class comment.
- [x] `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_HOLD_PHOTOS: "Hold for photos"`; the controls text says
  Next ends a hold.
- [x] `docs/crosshatch/display-lab.md` -- rows, the Hold row, the spots per mode, the marker, the log lines, what to
  photograph, the code map.

**Acceptance Criteria:**
- Given the host suite, when it runs, then every test passes, the new hold, marker and resume tests included.
- Given Hold on and a Static run on the device, when a pattern settles, then a 32 px dash in the bottom-right margin
  turns the opposite shade, the run holds, and Next turns it back before the flip starts.
- Given Hold off, when a Static run settles, then it flips at once and no marker appears.
- Given `default`, `sticky` and the flash gate, when built, then both compile and the games delta and static RAM equal
  3ba6049b's, measured the same way.

## Implementation Notes

Built 2026-10-17 in the worktree `/home/user/crosshatch-lanec4` (branch `dlab-lane-c4`, base 3ba6049b), staged, not
committed (the orchestrator runs the review lenses on the staged diff). Every task as planned; the settled readings:

- **The scene owns the hold.** `TransitionScene::slotDone(model, preset, driven, driveDone, periodUs, elapsedMs)` routes
  by `HoldPhase` (Running, MarkerOut, Held, MarkerBack); the mode's decision moved into `patternStep`, the crossing's
  spots into `crossingSpot`, the flip into `flip()`. Hold off (the scene's default) returns exactly dlab-c3's steps,
  so the existing tests only changed signature. `resumeHold` (no-op unless Held) is the one unconditional call `serve`
  gained, so its untested decision table (dlab-c2 deferral) grows no branch; `cancelHold` returns the dropped spot.
- **Marker.** First built as a 16 px corner square; after review a 32 px dash in the bottom margin (see Design Notes),
  shade `markerBandShade() + 2` while out; `targetAt` shows it, so a reapply keeps it; `markerSettled` reads `remaining`
  over its `toPanel` rect (at most 512 pixels). Log letters K/D/L/W from a local helper.
- **Clock.** `advanceClock` adds nothing while `scene.clockStill()`; the spot slot's own duration is not added and the
  last marker-back slot's is (net one slot, as an ordinary slot).
- **Spot counts.** Static: the settled flip; Slow's painted bands: 0; Slow's dwell and crossing end, Fast's pause: the
  crossing from 1. The dwell spot counts Running slots with `rectX >= 0` in the cycle and holds at the
  `longestRunSlots`-th when the right edge is still on-screen (else skipped).
- **Session cap.** The hold is dropped, logged `gray hold: <spot> restarted after the session-cap clean; the next photo
  spot holds`; `restartFlips` also clears a pending flip.
- **Rows.** Run, Mode, Hold for photos (`STR_GAMES_DLAB_HOLD_PHOTOS`, subtitle `STR_STATE_ON`/`STR_STATE_OFF`), Clean,
  Speed, Controls (text now says Next ends a hold). The start line names the hold.
- **Accepted.** The marker-out slots also drive pixels still in flight elsewhere at the still clock (documented).

**Results** (`PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, AGENTS.md's locks):
- Host suite: 2,216 tests, all pass (7 new in `test_transition_scene.cpp`). Mutation check: skipping the marker's
  settle check fails two of them.
- `scripts/check_layers.py`: passed. `scripts/check_upstream_touches.py`: PASS, trial merge clean (`english.yaml` is
  ledger row 2).
- `./bin/clang-format-fix`: reflowed one comment and two lines of this change; the second run changed nothing.
- `pio run -e x4pro`, `-e default`, `-e sticky`: success. `pio check -e x4pro` with the three `--fail-on-defect`: no
  defects.
- Plain x4pro image, both built in this worktree the same way: 3ba6049b Flash 6,256,578 B, `firmware.bin` 6,261,648 B,
  RAM 104,232 B; this change Flash 6,258,558 B (+1,980), `firmware.bin` 6,263,632 B (+1,984), RAM 104,232 B (+0).
- Flash gate (D6), at 3ba6049b and on the change: games-on 6,219,584 -> 6,219,696 B, games-off 5,961,392 -> 5,961,504
  B (+112 each: the English strings every image carries), games delta +258,192 B both (+0; 18,288 B to spare); static
  RAM +784 B both (240 B to spare); `objects` 67 objects, no problems, both.

## Plan Change Log

### 2026-10-17 — orchestrator review batch (14 items; supersedes the Tasks and Design Notes where they differ)
- The marker is a 32 px dash in the bottom band's margin below the last lane's rectangle (at most 16 rows; 8 at 480
  high, 10 at 800), a 16 px corner square only without lanes. Avoids: the square sharing rows with the bottom lane's
  rectangle and retargeting its in-flight trail at `fast pause` and `slow crossed`.
- Auto-sleep waits while the run holds (`heldForPhoto`). Avoids: a sleep's FULL refresh ending a hold.
- `crossingAt` shared by `rectX` and `crossingSpot`; `markerBandShade()` for the log. `SweepControl::holdRun`'s comment
  names every mode's spots.
- Tests: the dash at full size in every turn, setMode restarting the crossing spots, the dwell skip at the right edge,
  Hold off passing real spots, a press during the marker's slots, the cap clean in Slow and Fast skipping the dropped
  spot. Doc: the dwelt spot's ramp, the faint marker, the marker slots' two effects and the build-up photo with Hold off,
  the press during the marker's slots, the Controls string.

### 2026-10-17 — orchestrator follow-up batches 2 and 3 (on fc64cf24)
- `heldForPhoto` (and its start) stored before `holdRun`. Avoids: the loop reading Run cleared and no hold, and
  auto-sleep ending the hold.
- The hold keeps the device awake for `HOLD_AWAKE_MS` (10 min) from its start, then the idle timeout applies.
  Avoids: a forgotten hold keeping the CPU up indefinitely.
- Skipped spots are logged; dropped holds say which case and their marker slots; one `shadeLetter`; shared reset
  helpers (`cancelHold` now clears the spot).
- Tests: the marker's settle and slots in every turn (a mutant reading the logical rect as panel coordinates fails),
  Static's settle counts against a Hold-off twin, the skip reasons, the drop cases. Doc: the Static bullet points to
  the hold, the dwelt spot's wait (a finished trail, not the full run), the sleep bound, the drop cases, re-wrapped.

## Review Triage Log

### 2026-10-17 — Review pass (orchestrator-run lenses, per orchestrated-epics.md § 3's fallback: the session's subagents cannot nest)
- verdicts: 27 findings — high 0, medium 2, low 18, false 0, maybe-false 0, rejected-by-rule 7
- findings:
  - `[medium]` `[patch]` (blind 1, edge E1, E3, verification-gap other, intent) the 16 px marker square shares its top rows with the bottom lane's rectangle rows (480 high: lane 448–471, marker 464–479; 800 high: lane 740–789, marker 784–799), so at `fast pause` and `slow crossed` it retargets the newest trail pixels twice and on a narrow screen hides a dwelt rectangle; the corner test's 64×160 screen has no lanes and could not see it — the marker becomes a dash in the band's bottom inset: MARKER_W (32) px wide, as tall as the rows below the last lane (8 px at 480, 10 at 800, capped at 16), so it never touches a lane at any size with lanes; pinned at 800×480 and 480×800 in every turn.
  - `[medium]` `[patch]` (blind 7) auto-sleep could end a hold (Run is cleared while held, so `preventAutoSleep` fell false) and take the panel's FULL refresh mid-photo — the guard holds while the task is up and the scene holds, not only while Run is set.
  - `[low]` `[patch]` (verification-gap 1) `setMode`'s reset of the crossing-spot tracking was pinned by nothing (deleting it passed every test; a Fast run after a Back-and-Run would skip its first pause) — a test reuses the scene across `setMode` and `setHold` and expects the first spot again, Fast and Slow.
  - `[low]` `[patch]` (verification-gap 2, blind 11a) the dwelt skip at the right edge never ran in a test — a 60 px wide Slow case expects painted then crossed, no dwelt.
  - `[low]` `[patch]` (blind 11b) `HoldOffNeverHoldsAndStaticFlipsAtOnce` passed vacuously for Slow and Fast — it now checks each passed a spot's slot.
  - `[low]` `[patch]` (blind 8) a press during the marker's slots pauses as usual (`resumeHold` false in MarkerOut and MarkerBack) with neither doc nor test — pinned; the doc's Controls paragraph says so.
  - `[low]` `[patch]` (blind 3) the doc still quoted the c3 Controls string — follows `english.yaml`.
  - `[low]` `[patch]` (blind 2) Hold On lengthens every Fast pause and Slow crossing end (the marker's slots keep dosing the in-flight pixels, out and back) — the doc says the build-up photo is taken with Hold off, and names both effects.
  - `[low]` `[patch]` (blind 9) "rectangles after a full dwell" overstated the dwelt spot — the doc says the trailing-edge column has had the full run, the leading edge has just arrived.
  - `[low]` `[patch]` (blind 13) a weak band→band+2 dose makes the marker faint — the doc names the `gray hold:` line as the reliable signal.
  - `[low]` `[patch]` (blind 5) a session-cap clean in Slow or Fast skips the dropped spot for that crossing and Slow repaints without re-measuring; only Static holds again at the same spot — documented in the doc and the matrix; `cancelHold` pinned for Slow and Fast.
  - `[low]` `[patch]` (blind 4) `SweepControl::holdRun`'s comment still said Static only — reworded.
  - `[low]` `[patch]` (blind 10) `crossingSpot` repeated `rectX`'s clock arithmetic — one private helper gives the crossing number and its elapsed time.
  - `[low]` `[patch]` (blind 12) `afterSlot` derived the marker's band shade as `shifted(marker, 2)`, a fact private to the scene — the scene's hold step carries the band shade.
  - `[low]` `[defer]` (blind 6) only the cap clean drops a hold; `serve`'s replay and clean branches repaint from white without `cancelHold` — unreachable today (`requestReplay` only through `reset()` in Running, `requestClean` has no caller); deferred as a note for whoever adds a caller: drop the hold in `cleanToWhite`.
  - `[low]` `[defer]` (verification-gap 3, intent) the Hold row's wiring (`onEnter` On each visit, `toggleHold`, `scene.setHold(holdOn)`), `advanceClock`'s `clockStill` gate, `serve`'s `resumeHold`, `afterSlot`'s `holdRun` and the cap path's order have only the hardware check — the device-wiring family (dlab-c2, c3); the TaskSim mirror is the host-side record of the order.
  - `[low]` `[defer]` (edge E2) a logical height under 64 px puts the marker into band 2 — no supported screen is that small; the dash's height is clamped to the inset anyway.
  - `[low]` `[reject]` (edge claim C4) "opposite shade" is cyclic (W band → D, K band → L) — the story says `(band + 2) mod 4`; by design.
  - `[low]` `[reject]` (intent 1–6) marker exclusion temporal not spatial, the shown shade after Static's shift, the dwelt spot operational (the parenthetical), in-flight pixels advancing during the marker's slots, the static Controls text, Fast's miss when a slot outlasts the pause, Hold's default in the activity — settled readings, all recorded in the doc; no change.
  - `[low]` `[reject]` (intent 7) the resume sequence is tested at the scene (`resumeHold`) rather than through `SweepControl` and `serve` — the story asks for host tests of the pure decisions; the task path is the deferred device wiring above.

### 2026-10-17 — Follow-up review pass (same four lenses, orchestrator-run, over commit fc64cf24; two mediums were patched)
- verdicts: 17 findings — high 0, medium 1, low 10, false 0, maybe-false 0, rejected-by-rule 6
- findings:
  - `[medium]` `[patch]` (follow-up blind 2) with Hold on by default and every mode holding, a forgotten hold kept the device awake at full CPU indefinitely (`preventAutoSleep` true while held; `main.cpp` resets the idle clock and disables power saving on every pass) — a hold keeps the device awake for `HOLD_AWAKE_MS` (10 min) from its start, then the idle timeout applies; doc and header say so.
  - `[low]` `[patch]` (follow-up edge 1) `afterSlot` cleared Run before setting `heldForPhoto`, a window in which the loop reads both false and auto-sleep can end the hold — the hold is published first.
  - `[low]` `[patch]` (follow-up verification-gap 1) `markerSettled`'s logical-to-panel mapping ran only in the identity turn (a mutant reading the logical rect as panel coordinates passed every test; in Portrait it would report held after one slot) — the every-turn test runs to held and resumed and checks the rotated panel pixels; the mutant fails it.
  - `[low]` `[patch]` (follow-up blind 1) the doc credited the dwelt spot's extra wait with the full run, which the first fully on-screen slot already has — the wait adds a rectangle-wide trail whose oldest column has finished its return; reworded.
  - `[low]` `[patch]` (follow-up blind 3, 11) the session-cap line said "restarted" for every dropped hold — `cancelHold` returns the spot, the case and the marker slots so far; the line names holds again at flip 0, skipped for this crossing, or photo taken and the way back cut off; pinned.
  - `[low]` `[patch]` (follow-up blind 4) a missed spot left no log line while the doc calls `gray hold:` the reliable signal — the step carries a skipped spot and its reason (RightEdge, PauseOutrun), logged `gray hold: <spot> N skipped (<reason>)`; pinned, Hold off logs nothing.
  - `[low]` `[patch]` (follow-up blind 6) `shadeLetter` repeated Tuning's `SHADE_LETTERS` — one `GameDisplayLab::shadeLetter`, host-tested, used by both.
  - `[low]` `[patch]` (follow-up blind 7) four hand-written hold resets with differing field sets — `dropHold()` and `resetCrossingSpots()`; `spot_` is optional and cleared.
  - `[low]` `[patch]` (follow-up blind 8) the doc's Static bullet said the settled pattern flips at once; lines past 120 columns — points to Holds for photos; re-wrapped.
  - `[low]` `[patch]` (follow-up blind 10) the Static hold test asserted the preset's settle count (4) — compares against a Hold-off twin.
  - `[low]` `[reject]` (follow-up blind 5, intent) the epic's D13 and the story still described the 16 px square — the story was amended in fc64cf24 by the orchestrator; D13 and the Story results entry are the merge commit's.
  - `[low]` `[reject]` (follow-up blind 9) no simulator check of the six-row screen — the lab is absent in the simulator (no `HalDisplay::eink()`, dlab-c2); the row list scrolls.
  - `[low]` `[reject]` (follow-up intent 1–5) the scene-level tests through TaskSim, the painted spot once per run (the matrix's "three per crossing" reads per-run for painted), the shown shade after Static's shift, the clock standing still, the additions beyond the story (auto-sleep, the pause during the marker's slots, the start log's hold state, the 16 px fallback without lanes) — settled readings, recorded here; no change.

## Design Notes

**Phases (all in the scene).** `slotDone` by `phase_`: `MarkerOut` and `MarkerBack` count `markerSlots_` and, once
`markerSettled(model)` (no doses left under `markerRect()`'s panel rect), go to `Held` (`held`) or `Running`
(`resumed`; a pending Static flip runs now: `reapply`); `Held` returns an empty step; `Running` makes the mode's
decision and, at a spot with Hold on, retargets the marker (`markerShown_`, `retargetLogical` over `markerRect()`) and
enters `MarkerOut` (`hold`). The marker's slots never reach the mode's decision, so they cannot settle Static, count as
its slots, or feed Slow's period. `resumeHold` (Held → MarkerBack, marker retargeted back) is a no-op otherwise, so
`serve` calls it unconditionally when the run restarts. `cancelHold` (cap clean) returns the spot it dropped. `setMode` resets the phase and the crossing tracking and keeps `holdOn_`; `setHold` sets it and resets the
phase; `restartFlips` also drops a pending flip. The scene's default is Hold off, so dlab-c3's behaviour is the default.

**Spots.** Static: every settle (`settled.flip`); the flip (`++flips_`, shift) is deferred to the resumed slot.
Slow: the `measured` slot (count 0); then per crossing cycle `(elapsed - slowStart) / (sweepMs + PAUSE_MS)`: slots
with `rectX >= 0` counted, the `dwellSlots_`-th holds when `x + rectW <= logicalW` (else skipped), the first slot with
no `rectX` holds (count: the crossing, 1-based). Fast: the first slot with no `rectX` in each cycle. A slot longer
than the pause can skip a cycle's spot; accepted.

**Marker.** A 32 px (`MARKER_W`) dash at the logical bottom-right in the bottom band's margin: its rows run from the
last lane's rectangle's bottom (`rectTop(BANDS - 1, LANES - 1) + rectHeight()`) to the bottom, at most `MARKER_PX` (16),
so it never shares a row with a rectangle or its trail; without lanes (`rectHeight() <= 0`) a 16 px corner square;
clipped on a smaller screen. Its band is the last (`markerBandShade()`, which the log uses too); its shade while out is
that moved on by 2. The clock stands still while it is out, so `update` never moves anything over it. A weak
band-to-band+2 dose makes it faint; the `gray hold:` line is the reliable signal.

**Sleep.** While held, Run is clear, so the activity keeps `heldForPhoto` and `heldSinceMs` (set on `held` before
`holdRun`, so the loop never sees Run cleared without the hold; cleared on resume, at a run's start and at the task's
end). `preventAutoSleep`: the task up and Run requested, or held for under `HOLD_AWAKE_MS` (10 min) since the hold's
start; after that the idle timeout applies, so a forgotten hold cannot keep the CPU up for good.

**Clock.** `advanceClock` adds nothing while `scene.clockStill()`: the scene stays at the spot's clock through the
marker's slots and the hold, and runs again after the resumed slot.

**Cap clean in a marker phase.** Dropped (not re-driven): after a repaint from white the marker would settle before the
pattern and hold a half-painted panel. Static's sequence restarts (c3) and holds again at flip 0's settle; Slow and
Fast keep the crossing's passed flags, so the dropped spot is skipped for that crossing (Slow does not re-measure).

**Crossing time.** `crossingAt(elapsedMs)` (crossing number, time into it) serves both `rectX` and `crossingSpot`.

**Skips and drops.** With Hold on, `crossingSpot` puts a passed spot in `SceneStep::skipped` (`SpotSkip::RightEdge` for
the dwell, `SpotSkip::PauseOutrun` when a crossing ends with no slot in its pause); the activity logs `gray hold:
<spot> N skipped (<reason>)`. `cancelHold` returns `DroppedHold {spot, drop, markerSlots}` with `HoldDrop`
(`StaticAgainAtFlipZero`, `SkippedThisCrossing`, `WayBackCutOff`). `dropHold()` and `resetCrossingSpots(cycle)` are
the only resets of the hold and crossing state (`setMode`, `setHold`, `cancelHold`, a new crossing).
`GameDisplayLab::shadeLetter` replaces the two local K/D/L/W tables (the transition test's log and Tuning's row names,
whose text is unchanged).

**Marker slots' side effects.** They keep dosing pixels in flight at a still scene, so every Fast pause and Slow
crossing end runs longer and its trails settle further: the build-up photo is taken with Hold off (documented).

## Verification

**Commands:** (with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache` and AGENTS.md's locks)
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass.
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py` -- expected: pass.
- `./bin/clang-format-fix` twice -- expected: the second run changes nothing.
- `pio run -e x4pro`, `-e default`, `-e sticky`; `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: success, no defects.
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects` -- expected: within budget, games delta and static RAM as at 3ba6049b.
- Plain x4pro image `Flash:`/`RAM:` at 3ba6049b (built before any edit) and on the change -- record the delta.

**Manual checks (if no CLI):**
- Hardware (owner, experimental branch): the marker appears within about a second of a settle and is gone when the
  flip starts; the three Slow holds and the Fast hold land as described; Hold off runs without holding.

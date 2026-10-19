---
title: 'dlab-g2: seven levels (model, preset version 2, defaults, transition test, Tuning, via, double clean, transparent)'
type: 'feature'
ticket: 'story-dlab-g2-seven-levels'
created: '2026-10-19'
baseline_revision: '7d574d04419259c40b4eb915dc889b60b14cdf5a'
status: 'done'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']  # orchestrator-run, two passes
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-g2-seven-levels.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/scope-discipline/SKILL.md'
warnings: ['oversized']
deferred:
  - {summary: 'white-black-white fill order and stop checks: no automated check', location: 'src/activities/games/display_lab/GrayRunner.cpp', severity: low}
  - {summary: 'bitmap values above 7 counted, not logged: no caller until the sprite story', location: 'lib/GameDisplayLab/GrayModel.cpp', severity: low}
  - {summary: 'version 1 notice lifetime, cap clean, Tuning suffix: device-only', location: 'src/activities/games/display_lab/', severity: low}
  - {summary: 'nearest-anchor hold rule changes InkBoy defaults: SSD1677 Static/Slow re-check', location: 'lib/GameDisplayLab/GrayPreset.cpp', severity: low}
---

<intent-contract>

## Intent

**Problem:** The gray engine models four shades (K, D, L, W on levels 0..192 in steps of 64, a 4×4 dose table, a
16-bit hold mask, six per-pair history routes, four test bands, twelve Tuning dose rows). D14 takes it to seven
levels, black, five grays and white, so a true 50 % gray (level 3) exists; the ladders (dlab-g1) gave the table.

**Approach:** Generalise the shade count to seven and keep every mechanism, exactly the story's nine items bounded by
D14's table: level step 32 (K, D, L, W = 0, 2, 4, 6), 3-bit target/source, a 7×7 dose table with a 64-bit hold mask,
two directional history routes, an optional `via` level per transition run as two legs, preset file version 2 with a
version 1 expansion, InkBoy's expanded and D14's UltraChip defaults, seven bands with one lane each and six rotating
flips, a sectioned Tuning screen, a white-black-white clean before every runner paint and on the Clean row,
`Shade::TRANSPARENT` (7) with `GrayModel::retargetBitmap`, and the stock bench on the named constants.

## Boundaries & Constraints

**Always:** mask planes, slots, scheduler, scan path, backends and seam unchanged; the model's 3 B/pixel and the
history array's 3 B/pixel; every pure change host-tested; a version 1 file loads and a version 2 file round-trips;
`DOSE_MAX` 24; locals under 256 B; `tr(STR_…)` keys in `english.yaml` only; C3 `default` and `sticky` still build;
flash-budget gate (D6) unchanged; `docs/crosshatch/display-lab.md` rewritten where it says four shades.

**Never:** no change to `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam or `freeink-sdk/`; no
new ledger row; no generated I18n edit; no change to the frame-period or polarity screens, the stock bench's
measurements, or the ladder's stages; no game-engine sprite plumbing; nothing beyond the nine items (defer instead).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Level arithmetic | shade s | level s × 32; `baseDose` at a level between two shades interpolates | — |
| Version 1 file | 12 doses, 12 holds, six routes | expands as described (with its own doses, no vias), `expandedVersion1` reported (logged); when the expansion is not the panel's defaults, the lab menu's Save preset line says a version 1 preset is in use until a Save or a Reset; Save writes version 2 | malformed: defaults, LOG_ERR, as today |
| Version 2 file | 42 doses, 42 holds, 42 vias, two routes | loads; round-trips through Save | a dose outside 1..24, a via of 7 or past it, an array of the wrong length for the version: rejected whole |
| Defaults | SSD1677 / UltraChip | InkBoy's expansion / D14's table, both pinned | — |
| Static flips | Run with Mode = Static | offsets +1 .. +6, every band through all six other levels (42 transitions in six flips) | — |
| Tuning | Section row | Doses / Holds / Other; the rows below follow | — |
| Via | 0>1 with via 2, from settled black | the 0>2 leg (dose[0][2]) then the 2>1 leg (dose[2][1]) | — |
| Transparent | retargetBitmap with a 7 | that pixel keeps its level and target; the others retarget | a value above 7: treated as transparent and counted (the caller's one LOG_ERR) |
| Stock bench | UltraChip without a lab backend | four levels, unchanged | — |

</intent-contract>

## Code Map

- `lib/GameDisplayLab/GrayPreset.{h,cpp}` -- shades, transition order, hold bits, defaults, tuning steps, the std-only
  JSON codec (`Reader`, `readValue`, `ReadState`, `encodeGrayPreset`, `loadGrayPreset`). `loadGrayPreset` must stay
  under 256 B of locals: it already holds a `GrayPreset read`, which grows to about 120 B.
- `lib/GameDisplayLab/GrayModel.{h,cpp}` -- per-pixel bytes (level, doses left, target | source << 2), `retarget`
  (budget, retarget percent, history routes via `routeOf` and `midRoutesActive`, weak bit), `driveRect`, `buildMasks`,
  `commit`, `deliverDose`. Guards to keep: `retarget` returns when the target is unchanged or the pixel is outside;
  `deliverDose` returns at 0 doses left; `commit` does nothing unless `driveDone` and reads direction and follow-up
  from the state `buildMasks` read; `driveRect` ignores 0 doses and caps at `DOSE_MAX`.
- `lib/GameDisplayLab/TransitionScene.{h,cpp}` -- `BANDS`, `LANES`, `laneShade`, `bandShade`, `shifted`, `flipOffset`,
  `markerShade` (`shifted(band, 2)`), `shadeName`, `heldRhythm` (`holdMask = 0xFFFF`), margin/marker geometry (works
  unchanged with one lane), Ladder (unchanged).
- `lib/GameDisplayLab/StockBench.cpp` -- `rampShadeAt` casts the part 0..3 to `Shade`; must map to K, D, L, W.
- `lib/GameDisplayLab/GrayPicture.{h,cpp}` -- reads `SHADE_COUNT`; expected to need no change (check its tests).
- `src/activities/games/display_lab/GrayRunner.{h,cpp}` -- `serve` (replay path calls `cleanToWhite`), `cleanToWhite`
  (one FULL refresh under `RenderLock`), the cap clean in `runSlot`.
- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- `cleanFromRows` (Clean row), comments naming
  four bands and +1/+2/+3.
- `src/activities/games/display_lab/TuningActivity.{h,cpp}` -- 12 dose + 12 hold rows, retargets, rate, swap, reset,
  save; `labels[24][16]`, `values[ROW_COUNT][40]`.
- `src/activities/games/display_lab/GrayPresetFile.{h,cpp}` -- `ENCODE_BYTES` 512 (a v2 file is about 800 B), load's
  log lines.
- `lib/I18n/translations/english.yaml` -- `STR_GAMES_DLAB_*` (ledger row 2).
- `src/activities/games/display_lab/DisplayLabActivity.cpp` -- `onEnter` holds a loaded preset and assigns the
  defaults to the session's; at seven levels (`GrayPreset` 120 B) the two must not share its frame (256-byte rule).
- `lib/GameDisplayLab/TuningLayout.h` (new) -- Tuning's sections and rows as pure functions, so the Section row's
  matrix row is host-tested.
- `docs/crosshatch/display-lab.md` -- engine, table, transition test, Tuning, file format sections.
- Tests: `test/game_display_lab/test_gray_preset.cpp`, `test_gray_model.cpp`, `test_transition_scene.cpp`,
  `test_stock_bench.cpp`, `test_gray_picture.cpp` (one binary, `test/game_display_lab/CMakeLists.txt`).

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameDisplayLab/GrayPreset.h`, `GrayPreset.cpp` -- `Shade { K = 0, D = 2, L = 4, W = 6, TRANSPARENT = 7 }`
  (levels 1, 3, 5 by value), `SHADE_COUNT` 7, `TRANSITION_COUNT` 42, `ROUTE_COUNT` 2 indexed by `Toward`, `VIA_NONE`
  7, transition order `from 0..6, to ≠ from ascending`, `holdBit` = bit source × 7 + target of a `uint64_t` (first in
  the struct; `sizeof(GrayPreset)` ≤ 120, asserted), `HOLD_DIAGONAL`, `HOLD_ALL`, `holdRowsOf`; `via[7][7]`; `valid()`
  checks vias and no hold bit past 48; `longestRunSlots` counts a via's two legs; tuning steps for via, history, return
  deltas and reverse percents; the version 1 expansion (doses by mean, holds by the nearest anchor pair, routes to
  L>K>L and D>W>D, vias cleared when the file has doses, dropped routes reported); `inkBoyDefaults()` = InkBoy's
  expansion; `ultraChipDefaults()` = D14's table, every hold on, via 0>1 = 2; codec version 2 (`via` -1 for none),
  version 1 read; `samePresetValues`, `version1NoticeNeeded`.
- [x] `lib/GameDisplayLab/GrayModel.h`, `GrayModel.cpp` -- `LEVEL_STEP` 32, `ORIGIN_NONE` 7, 3-bit target and source;
  the doses-left byte keeps a via's second-leg target in its high three bits; `retarget` dispatches a via's first leg
  and keeps the final target; commit starts the second leg with the first leg's history and no weak bit; returns are
  judged on final targets only, against both legs' counts for a via; directional routes; `retargetBitmap` (on-model
  values above 7 counted); `nextTarget`; follow-up from per-call hold rows.
- [x] `lib/GameDisplayLab/PanelGeometry.h` -- `farEndOver`, the hold marker's rule shared by the scene and the viewer.
- [x] `lib/GameDisplayLab/TransitionScene.h`, `TransitionScene.cpp` -- `BANDS` 7, `LANES` 1, lane (band + 3) mod 7,
  `flipOffset` +1..+6, `holdLevelOver` (black or white, whichever is farther from the band), `shadeName` the digit,
  `heldRhythm` `HOLD_ALL`, `setScreen`.
- [x] `lib/GameDisplayLab/TuningLayout.h` -- Tuning's sections and rows.
- [x] `lib/GameDisplayLab/StockBench.cpp` -- the ramp's four parts on K, D, L, W.
- [x] `src/activities/games/display_lab/GrayRunner.{h,cpp}` -- a run's clean (start, replay, session cap) is white,
  black, white, one render lock per pass, a stop checked between passes and after the clean; the run's and the cap's
  own log lines; `cleanWhiteBlackWhite` for the Clean row.
- [x] `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- the Clean row's triple clean; the working
  set placed with `setScreen`; seven bands, one lane, six flips in its comments.
- [x] `src/activities/games/display_lab/TuningActivity.{h,cpp}` -- the Section row and three sections; via suffixes on
  Dose (" (via N)") and Hold (" (via N, unused)") rows; the Reset row's description while the version 1 notice is live;
  value lines of 64 characters.
- [x] `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` -- the version 1 notice on the Save preset line
  (until a Save or a Reset; a failed Save keeps it); `setDefaults` keeps `onEnter` under 256 B.
- [x] `src/activities/games/display_lab/GrayPresetFile.{h,cpp}` -- encode buffer 1,024 B; the version 1 flag out; the
  expansion and the dropped routes logged.
- [x] `lib/I18n/translations/english.yaml` -- the new `STR_GAMES_DLAB_*` keys; the transition test and Tuning
  descriptions.
- [x] `docs/crosshatch/display-lab.md` -- rewritten for seven levels.
- [x] `test/game_display_lab/*` -- every matrix row and the rules above.

**Acceptance Criteria:**
- Given a version 1 preset file on the SD card, when the lab opens, then it loads with its doses on levels 0, 2, 4, 6,
  the odd levels interpolated, the log says `preset: version 1 file expanded to seven levels`, and Save writes
  `"version":2`.
- Given Mode Static and Run, when six flips have settled, then every band has gone through all six other levels.
- Given a run starts (transition test or viewer), when the runner paints, then three FULL refreshes (white, black,
  white) came first and one log line says so.
- Given the Tuning screen, when the Section row is stepped, then the rows below are that section's.

## Implementation Notes

Implemented in the main session (orchestrated-epics § 3 fallback: no nested subagents), full route, then two review
batches from the orchestrator's lenses (the Review Triage Log). The final state:

- **Model** (`GrayModel`): `LEVEL_STEP` 32, `ORIGIN_NONE` 7, 3-bit target and source; the doses-left byte holds the
  count in five bits and a via's second-leg target (+1, 0 none) in three, so 3 B/pixel stays. `retarget` dispatches:
  no-op when already going to `target`; to the first leg's own target drops the second leg; otherwise the via of the
  level nearest the pixel's (unless the pixel is on the via level) over `retargetLeg`, the old body. Guards kept from
  the old `retarget`: outside/invalid and "same (final) target" return; `deliverDose` keeps its early return at 0
  doses and starts leg 2 when leg 1's last dose lands (the slot's follow-up dose can be that one). Leg 2 keeps leg 1's
  history (origin, step counters) and never takes the weak bit. A return to the origin is judged on a final target
  only (a first leg toward the origin level is no return) and, for a via, against both legs' counts (`cleanRun`).
  `followsUp` reads per-call hold rows (`holdRowsOf`, each `buildMasks` and `commit`), guarding a source past white.
  `retargetBitmap` counts on-model values above 7.
- **Preset** (`GrayPreset`): `holdMask` first, then 7x7 doses and vias, two routes indexed by `Toward`; `sizeof` 120
  (asserted). Version 1 expansion: doses by the mean of the anchor transitions; holds by the nearest anchor pair
  (fewest shades spanned, the same direction; a tie, both ends odd, to the pair whose source lies away from the other
  end); routes to L>K>L (toward light) and D>W>D (toward dark), the other four reported when non-zero; vias cleared
  when the file has its own doses. `loadGrayPreset` reads into `out` directly and streams lists (no buffers).
  `version1NoticeNeeded` says whether a loaded version 1 file differs from the panel's defaults.
- **Scene**: seven bands, one lane at (band + 3) mod 7, six flips, `holdLevelOver` on `farEndOver` (the viewer's rule,
  shared from `PanelGeometry.h`), `setScreen` (no temporary scene on `allocateWorkingSet`'s frame).
- **Runner**: a run's clean (start, replay, the session cap's) is white, black, white, one render lock per pass, the
  stop checked between passes (`Stopped`, logged) and after the clean (no paint, no `onCapClean` over a stop); logged
  once as the run's line or the cap's own; the Clean row uses `cleanWhiteBlackWhite` under its own lock.
- **Lab menu and Tuning**: the version 1 notice ("Version 1 preset in use: Reset in Tuning") on the Save preset line
  when `version1NoticeNeeded`, until a Save or a Reset (a failed Save keeps it), and Tuning's Reset row says so while
  it is live; Tuning's sections via `TuningLayout.h`; via suffixes on Dose and Hold rows; 64-character value lines.
- **File**: encode buffer 1,024 B; `preset: version 1 file expanded to seven levels` and the dropped-routes line (a
  noinline helper, off `load`'s frame).
- **Stock bench**: four parts on K, D, L, W; the unreachable `TRANSPARENT` case.
- **Viewer**: no code change but the shared marker rule (`farEndOver`) and three comments.
- **Stack** (xtensa `-Os -fstack-usage`, this tree, after the closing batch): `loadGrayPreset` 160 B (192 at
  baseline), `version1NoticeNeeded` 160 B, `GrayPresetFile::load` 240 B (144), `DisplayLabActivity::onEnter` 160 B
  (128), `TuningActivity::writeValue` 112 B, `edit` inlined into `tapStep` 112 B and `adjust` 128 B,
  `allocateWorkingSet` 32 B (176), `GrayModel::buildMasks` 80 B, `retargetLeg` 80 B, `GrayRunner::cleanToWhite` 64 B,
  `runSlot` 176 B; all under 256 (the `buildScreen` frames, 1,136 B, are the list framework's, unchanged).
- **Files**: changed `lib/GameDisplayLab/{GrayModel,GrayPreset,TransitionScene,StockBench,GrayPicture}.{h,cpp}`,
  `lib/GameDisplayLab/PanelGeometry.h`, `src/activities/games/display_lab/{GrayRunner,TransitionTestActivity,
  TuningActivity,GrayPresetFile,DisplayLabActivity}.{h,cpp}`, `GrayViewerActivity.{h,cpp}` (comments),
  `lib/I18n/translations/english.yaml`, `docs/crosshatch/display-lab.md`, `test/game_display_lab/{test_gray_model,
  test_gray_preset,test_transition_scene,test_gray_picture,test_stock_bench}.cpp`, `test/game_display_lab/CMakeLists.txt`;
  new `lib/GameDisplayLab/TuningLayout.h`, `test/game_display_lab/test_tuning_layout.cpp`.

## Plan Change Log

- Closing batch P13 (orchestrator, 2026-10-19): the matrix's Version 1 row names the no-via rule and the notice; Tasks,
  Implementation Notes, Design Notes and the readings list rewritten in the present tense for the final rules (batch 1
  and the closing batch changed the hold tie, the marker, the cap clean, the version 1 vias and the history across a
  via). The "every band through all six other levels" wording stays for the orchestrator's merge-time amendment.

## Review Triage Log

Orchestrator verdicts, follow-up pass (2026-10-19, four lenses over 3fa04c5c): 33 findings — high 0, medium 0, low 13 patched (the closing batch below), rejected-by-rule 20 (stale plan text the batch rewrote, duplicates across lenses, device-only test asks deferred). No high, so the review closes here per the owner's scope decision; the deferred items go to deferred-work.md at merge: host tests for the notice's lifetime, the cap's clean and stop checks and Tuning's suffix (no host harness for the display-lab activities); the InkBoy hardware re-check of Static and Slow after the hold rule changed its defaults.

### Batch 1 (orchestrator's four lenses over 09e9eb0b; applied in this session, staged, not committed)

Orchestrator verdicts (2026-10-19): 22 findings — high 0, medium 3, low 14, false 0, rejected-by-rule 5 (duplicates across lenses and merge-time items); every medium patched (P1–P3), so one follow-up pass follows this batch and, per the owner's scope decision for this story, only a high after that opens another.

The orchestrator triaged; 14 patches applied, 3 items deferred by the orchestrator (the white-black-white fill order
check, the bitmap's LOG_ERR once, the matrix's "every band through all six other levels"). The seven readings were
confirmed, R4 (hold tie) as changed by P4, R5 (marker) by P12, R6 (cap clean) by P9.

| Item | Severity | Action |
|---|---|---|
| P1 via breaks history routes | medium | `retargetLeg(.., secondLeg)` leaves the history alone when commit starts leg 2 (origin and step counters are leg 1's); a completed return's clean-run check counts both legs of a via (`cleanRun`). Tests `GrayModelVia.TheFirstLegsOriginAndStepsCarryAcrossTheVia` (0>1 via 2 then 1>0 takes the dark route's delta, 1>2 is no return, an unclean run is no return). |
| P2 version 1 keeps a via | medium | `expandVersion1` sets every via to none; the codec tests flipped (a version 1 file has no vias; a version 2 file without `via` keeps the defaults'). |
| P3 version 1 in use is silent | medium | `GrayPresetFile::load(.., bool* expandedVersion1)`; the lab menu's Save preset line reads `STR_GAMES_DLAB_PRESET_VERSION1` until a Save or a Reset (`version1Line`); the doc gains a migration paragraph. |
| P4 hold "nearest" | low | `expandVersion1Holds`: the same-direction anchor pair spanning the fewest shades, a tie (both ends odd) to the pair whose source lies away from the other end; header, doc and test (`Version1HoldsTakeTheNearestAnchorPair`) agree; the full 7x7 InkBoy hold table re-pinned (InkBoy's longest run is now 6 slots: 0>5 takes K>L's hold). |
| P5 dropped routes silent | low | `GrayPresetLoadInfo::droppedVersion1Routes` and `version1RouteName`; `GrayPresetFile` logs them once from a noinline helper (its buffer off `load`'s frame). Tested in the version 1 file test. |
| P6 sizeof(GrayPreset) 128 | low | `holdMask` first; `static_assert(sizeof(GrayPreset) <= 120)`. |
| P7 64-bit shift per pixel | low | `holdRowsOf(preset, rows)` derived once per `buildMasks` and `commit`; `followsUp` reads a byte. Test `GrayModel.HoldRowsAgreeWithTheHoldBits`. |
| P8 dose/hold rows of a via | low | Tuning's Dose and Hold values end " (via N)" (`STR_GAMES_DLAB_VIA_SUFFIX`); the doc's table note rewritten. |
| P9 cap clean single | low | The session cap's clean is `WhiteBlackWhite` too; doc and comments updated. |
| P10 no stop between passes | low | `cleanWhiteBlackWhite(.., control)` checks the stop between passes (false: `Stopped`, logged); `serve` checks `stopSeen()` after the replay's clean, before `onReplay`. Firmware-only path: syntax-checked, not host-tested. |
| P11 Tuning value truncation | low | `VALUE_CHARS` 40 for the value lines and both `plain` buffers. |
| P12 marker on the lane's level | low | `TransitionScene::holdLevelOver` (black from level 3 up, else white: the viewer's rule) replaces `MARKER_SHIFT`; the scene tests and the Ladder's marker (white over black) follow; the deliverDose comment corrected. |
| P13 clipped bitmap values counted | low | `retargetBitmap` counts on-model pixels only; its test updated. |
| P14 tests | low | (a) `AResetMidFirstLegClearsTheSecondLeg`; (b) `TheSecondLegIgnoresTheViaOfItsOwnTransition`; (c) `RetargetsDuringTheSecondLeg`; (d) `ASecondLegFromWhiteToFourIsWeakEligible` (pinned: weak-eligible); (e) `TransitionScene.StaticSixFlipsOnTheUltraChipPresetCoverEveryTransitionAndTheVia`. |

### Closing batch (orchestrator's follow-up lenses over 3fa04c5c; applied in this session, staged, not committed)

No highs: the last batch. 13 items applied; deferred by the orchestrator: host tests for the notice's lifetime, the
cap's triple clean and its stop checks and Tuning's suffixes (no host harness for the lab's activities), and the
InkBoy hardware re-check of Static and Slow after the hold rule (to the story result).

| Item | Severity | Action |
|---|---|---|
| P1 v1 without doses drops the via | low | `expandVersion1` clears vias only with `doseCount != 0`; the absent-keys test keeps the via for both versions. |
| P2 first leg as a return | low | `retargetLeg` judges routes only on final targets (`next >= SHADE_COUNT`). Tests `AFirstLegTowardTheOriginIsNoReturn`, `AnUncleanTwoLegRunIsNoReturn` (leg 1's count off; a count past the table). |
| P3 second leg weak bit | low | Off on a second leg; the pinned test flips to `ASecondLegFromWhiteToFourIsNotWeakEligible`, which also pins a plain 6>4 from rested white as eligible. |
| P4 lock and stops in the clean | low | `cleanToWhite` takes the render lock per pass; the cap path checks `stopSeen()` after its clean, before `onCapClean`; the `stop()`, `stop` declaration and `MarkerOut` comments updated. Firmware-only: syntax-checked. |
| P5 cap log line | low | `gray sweep: session cap: cleaned white, black, white` (`cleanToWhite(.., atCap)`); doc matched. |
| P6 notice rule | low | `version1NoticeNeeded` (and `samePresetValues`) in `GrayPreset`; test `TheVersion1NoticeIsForAnExpansionThatIsNotThePanelsDefaults`; the string "Version 1 preset in use: Reset in Tuning"; Tuning's Reset row `STR_GAMES_DLAB_RESET_PRESET_VERSION1_DESC` while live (`DisplayLabActivity::version1Notice`); a failed Save keeps the notice; doc "reads Saved". |
| P7 hold change on the same preset | low | Test `AHoldChangedOnTheSamePresetAppliesToTheNextSlot` (one dose held, then two once the hold is off). |
| P8 one marker rule | low | `farEndOver` in `PanelGeometry.h` used by `holdLevelOver` and `GrayPicture::markerShadeFor`; test `TheMarkerTakesTheEndFartherFromItsBand` (levels 0-6, literal); literal bands and markers per settle in the Static hold test; the Ladder marker's 0>6 / 6>0 cost in the doc. |
| P9 "once per slot" | low | Comments say once per `buildMasks` and per `commit` call. |
| P10 a via's own hold | low | Hold rows read " (via N, unused)" (`STR_GAMES_DLAB_VIA_UNUSED_SUFFIX`); the doc's table note and Tuning text say the own hold is unused and the dose read only by `baseDose`. |
| P11 template width, source guard | low | `VALUE_CHARS` 64; `followsUp` returns false for a source past white. |
| P12 frames | low | Re-measured (Implementation Notes, Stack): all under 256 B. |
| P13 plan | plan | Plan Change Log entry; sections rewritten. |

## Design Notes

- Level scale and names: D14 ("the level scale stays 0..192 with a step of 32, today's K, D, L, W sit exactly on levels
  0, 2, 4, 6") and D15 (2): shades are named by their digit, so `shadeName` returns `'0' + level`.
- Hold bit layout: bit source × 7 + target (InkBoy's source × 4 + target generalised), diagonal bits always set; the
  64-bit mask holds 49 bits, the rest must be clear.
- Directional routes (story item 2): the route is the direction the pixel returns in (its origin below its level:
  toward dark). Version 1 collapse: toward light takes L>K>L's values (the story); toward dark takes its mirror D>W>D
  (K↔W, D↔L), the only one of the five dark-side routes the story's "L>K>L gives toward-light" pairs with. D14: "the
  history routes keep the four-shade defaults' deltas and percents, collapsed": toward light -1 / 150 %, toward dark
  0 / 0. With two routes there is no per-pair "mid route", so the origin is recorded on every retarget of a settled
  pixel (the old record rule existed only to gate D>L>D and L>D>L).
- Version 1 expansion (story item 2): anchors land on 0, 2, 4, 6; a transition with one odd end is the mean of the two
  anchor transitions around it rounded up, the diagonal anchor counting 0 (0>1 = ⌈dose[0][2] / 2⌉); one with both ends
  odd is the mean of its four anchor transitions, rounded up once. Holds "by the nearest anchor pair": among the ends'
  anchors, the same-direction pair spanning the fewest shades; a tie (both ends odd) goes to the pair whose source lies
  away from the other end (batch 1, P4: 0>1, 1>2, 0>3 and 1>3 take 0>2; 3>1 takes 4>2). A version 1 file with its own
  doses has no vias (batch 1, P2: that table was never calibrated for one); one without doses keeps the defaults'
  table and its vias together (closing batch, P1).
- Version 1 notice (batch 1 P3, closing batch P6): a loaded version 1 file whose expansion differs from the panel's
  defaults (`version1NoticeNeeded`) puts "Version 1 preset in use: Reset in Tuning" on the lab menu's Save preset line
  until a Save or a Reset; a failed Save keeps it; Tuning's Reset row says so while it is live. InkBoy's own version 1
  file expands to InkBoy's defaults exactly, so it raises none.
- Via encoding (items 6 and 8): in memory `via[from][to]` is 7 for none (item 6); the file writes -1 for none and
  rejects 7 or above anywhere in the table, so TRANSPARENT never appears as a level in a preset (item 8: "the preset
  codec rejects 7 anywhere in a table"); doses and holds carry no levels. A via equal to `from` or `to` is invalid. The
  file's hold key stays `hold` (the story writes "`holds`", naming the existing array; renaming the key would make a
  version 1 file's holds unreadable for nothing).
- Via legs (item 6, D14: "0>1 goes to 2 and one pulse back toward black, using the engine's existing return
  mechanics"): leg 1 is an ordinary retarget to the via (its dose `dose[from][via]`, the retarget percent when the pixel
  was mid-transition, the routes and weak bit seen as a retarget to the via); when its last dose lands the model
  retargets to the stored final target from a settled via level, so leg 2's dose is `dose[via][to]`. `from` is the
  shade nearest the pixel's level; a pixel already on the via level goes straight; leg 2 never takes a via (no chains).
  The pending target sits in the doses-left byte's high three bits (`DOSE_MAX` 24 needs five), keeping 3 B/pixel.
  While a via is set, the transition's own dose (D14's "1 (via 2)") is read only by `baseDose` for a pixel between
  those levels, and its own hold bit not at all (Tuning marks both rows).
- Via and history (batch 1 P1, closing batch P2, P3): leg 2 keeps leg 1's origin and step counters, so a later return
  is judged against where the transition started; returns are judged on final targets only (a first leg toward the
  origin level is no return), and a two-leg run is clean when both legs' counts match the table; leg 2 never takes the
  weak bit (its pixel reached white in the same slot, and the weak row's W>L calibration assumes a rested white).
- Defaults: D14's table and "rate index 0, one pulse per slot, every hold on" for `ultraChipDefaults()`; retarget 100 %
  both ways as before.
- Transition test (item 4): `BANDS` = 7, one lane, lane shade (band + 3) mod 7; the marker margin formula already gives
  the bottom band's inset with one lane. The hold marker goes to black or white, whichever is farther from its band
  (batch 1 P12: the gray viewer's rule, `farEndOver`, shared): black over levels 3 to 6, white over 0 to 2; in the
  Ladder, white over black (a 0>6 and a 6>0 run).
- Double clean (item 7, D14's Slow run): every clean the shared runner makes before a paint is white, black, white:
  the run's start, the viewer's next picture, and the session cap's clean (batch 1 P9: the repaint follows it too); the
  Clean row the same. One render lock per pass, a stop checked between passes and after the clean (closing batch P4);
  logged once a run, the cap with its own line (P5). Each pass goes through `lab.cleanNow()`, restarting the cap count.
- Transparent (item 8): `GrayModel` is std-only (the layer check), so `retargetBitmap` returns how many values above 7
  it treated as transparent and the caller logs once; the caller is the later sprite story.
- Tuning (item 5): the Section row filters the list (the matrix's "the rows below follow"); the routes group (History,
  Return × 2, Reverse × 2, then the 42 Via rows) sits in Other before Frame rate, the story's order "the retargets,
  history mode, the two return deltas, the two reverse percents, rate and the rest".

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake --build build/test -j8' && ctest --test-dir build/test
  --output-on-failure -j8` -- expected: build clean, every test passes.
- `python3 scripts/check_layers.py` and `python3 scripts/check_upstream_touches.py` -- expected: exit 0.
- `./bin/clang-format-fix` twice -- expected: the second run changes nothing.
- x4pro toolchain `-fsyntax-only` over the changed screen files (as dlab-g3) -- expected: no errors.
- After review, the orchestrator: `pio run -e x4pro`, `-e default`, `-e sticky`, `pio check -e x4pro`, flash-budget
  gate (expect +0).

**Manual checks (if no CLI):**
- Hardware (owner, experimental branch): Static shows seven distinguishable bands after the first settle; the six flips
  show which of the 42 transitions need a dose change; Tuning edits them.

## Auto Run Result

Status: implemented and verified on the host; review not run (the orchestrator runs the four lenses over this diff and
sends the patch batch); nothing committed: every change is staged (`git add -A`) in `/home/user/crosshatch-laneg2`
on `dlab-lane-g2`, baseline `7d574d04419259c40b4eb915dc889b60b14cdf5a`.

Summary: the engine at seven levels with every mechanism kept (story items 1-9 as in Tasks), D14's table as the
UltraChip defaults, file version 2 with the version 1 expansion, seven bands and six flips, the sectioned Tuning
screen, the via legs, the white-black-white clean, the transparent value, the stock bench on the named shades.

Verification (host, this tree, the staged diff):
- Host suite: `cmake --build build/test` (Release, under the host-test lock), 0 warnings; `ctest -j8`: 2,405 of 2,405
  pass (baseline 2,386; +19: the display-lab binary 302 tests before the Tuning layout's 3, 305 with them).
- Matrix audit, each row with its passing test: level arithmetic (`GrayModelSevenLevels.*`); version 1 file
  (`GrayPresetCodec.Version1FileExpandsToSevenLevelsAndSavesAsVersion2`, `ArraysOfEitherLayoutMayComeBeforeTheVersion`,
  malformed cases in `MalformedTextGivesDefaults`); version 2 file (`RoundTripKeepsEveryField`, `DosesUpTo24Load`,
  via/length cases in `MalformedTextGivesDefaults`); defaults (`DefaultsAreInkBoysTableExpandedToSevenLevels`,
  `UltraChipDefaultsAreD14sSevenLevelTable`); Static flips (`StaticSettlesThenFlipsThroughAllFortyTwoTransitions`);
  Tuning (`TuningLayout.*`); via (`GrayModelVia.*`); transparent (`GrayModelBitmap.*`: the count above 7 is returned for
  the caller's one LOG_ERR); stock bench (`StockBench` ramp tests, unchanged).
- `python3 scripts/check_layers.py`: pass (890 edges, 185 files). `python3 scripts/check_upstream_touches.py`: PASS
  (the only upstream file touched is `english.yaml`, ledger row 2).
- `./bin/clang-format-fix` twice: the second run changed nothing; no formatting change outside these paths.
- Device compile: the six changed or affected screen files (`TuningActivity`, `GrayRunner`, `TransitionTestActivity`,
  `GrayPresetFile`, `GrayViewerActivity`, `DisplayLabActivity`) compile `-fsyntax-only` with the x4pro toolchain and
  flags (`FREEINK_CAP_DISPLAY_LAB=1`) and I18n headers regenerated into scratch, no error or warning (script
  `{scratch}/dlab-g2/syntax_raw.sh`, after dlab-g3's; a deliberate static_assert probe failed as it should).
- Stack frames: the figures in Implementation Notes (xtensa `-Os -fstack-usage`, base and new measured the same way on
  this tree).
- Firmware (`pio run` x4pro, default, sticky; `pio check -e x4pro`; the flash-budget gate): unmeasured (orchestrator
  builds after review).

Readings, confirmed by the orchestrator (final rules in Design Notes): the file's via uses -1 for none; the hold key
stays `hold`; the version 1 dark route takes D>W>D's values; holds take the nearest anchor pair, a tie to the source
away from the other end; the marker takes the end farther from its band; the session cap's clean is white, black,
white; the Section row resets to Doses on each visit.

Residual risks: the via's second leg runs from the settled via level through the ordinary retarget, so a pixel
retargeted while on leg 2 behaves as any mid-transition pixel; on a screen under 112 px high the no-lane marker square
can cross into the band above (no device has one). Hardware check for the owner (experimental branch): Static shows
seven distinguishable bands after the first settle, the six flips show which of the 42 transitions need a dose change,
Tuning edits them, and a run begins with the white, black, white clean.

### Review batch 1 result

Applied P1 to P14 as the Review Triage Log says; staged with `git add -A`, nothing committed, no `pio` (the
orchestrator's firmware verification was running in this worktree; the device syntax check shadowed `I18n.h` in scratch
so the pio-generated `lib/I18n` headers were not touched). Host suite: 2,412 of 2,412 pass (+7 over the first
pass's 2,405; the display-lab binary 312); its warnings are the pre-existing ones (a full rebuild). `check_layers.py`
pass (892 edges), `check_upstream_touches.py` PASS (upstream file touched: `english.yaml`, ledger row 2),
`./bin/clang-format-fix` twice with no change on the second run; the six screen files compile `-fsyntax-only` for x4pro
with no error or warning. Stack frames re-measured the same way (xtensa `-Os -fstack-usage`): `loadGrayPreset` 160 B,
`GrayPresetFile::load` 240 B (272 before the noinline log helper), `logDroppedRoutes` 112 B, `DisplayLabActivity::onEnter`
160 B, `GrayModel::buildMasks` 80 B, `retargetLeg` 80 B; all under 256. Firmware figures: unmeasured (orchestrator builds
after review).


### Closing batch result

Applied P1 to P13 as the Review Triage Log says; staged with `git add -A`, nothing committed, no `pio`. Host suite:
2,417 of 2,417 pass (+5 over batch 1's 2,412; the display-lab binary 317). `check_layers.py` pass (892 edges),
`check_upstream_touches.py` PASS. `./bin/clang-format-fix` twice, the second run changing nothing. The six screen
files compile `-fsyntax-only` for x4pro with no error or warning. Stack frames are recorded in Implementation Notes
(Stack), all under 256 B. Firmware figures: unmeasured (orchestrator builds).

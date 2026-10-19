---
title: 'dlab-g2: seven levels (model, preset version 2, defaults, transition test, Tuning, via, double clean, transparent)'
type: 'feature'
ticket: 'story-dlab-g2-seven-levels'
created: '2026-10-19'
baseline_revision: '7d574d04419259c40b4eb915dc889b60b14cdf5a'
status: 'in-progress'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-g2-seven-levels.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/scope-discipline/SKILL.md'
warnings: ['oversized']
deferred: []
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
| Version 1 file | 12 doses, 12 holds, six routes | expands as described, `expandedVersion1` reported (logged); Save writes version 2 | malformed: defaults, LOG_ERR, as today |
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
  (levels 1, 3, 5 by value), `SHADE_COUNT` 7, `TRANSITION_COUNT` 42, `ROUTE_COUNT` 2 indexed by `Toward` (return toward
  dark, toward light), `VIA_NONE` 7, transition order `from 0..6, to ≠ from ascending`, `holdBit` = bit source × 7 +
  target of a `uint64_t`, `HOLD_DIAGONAL`, `HOLD_ALL`; `GrayPreset` gains `via[7][7]`; `valid()` checks vias (none on
  the diagonal, else none or a level other than from and to) and no hold bit past 48; `longestRunSlots` counts a via
  transition's two legs; tuning steps for via, history mode, return deltas (±1, -11..11) and reverse percents (±25,
  0..250); version 1 expansion functions (doses, holds, routes) public for tests; `inkBoyDefaults()` = the expansion of
  InkBoy's table, hold 0xc667 and routes; `ultraChipDefaults()` = D14's table, every hold on, via 0>1 = 2, the same
  routes; codec version 2 (`doses`, `hold`, `via` 42 each, `via` -1 for none; `return_delta`, `reverse_percent` two
  each, toward dark then light), version 1 still read (array lengths checked against the version after the object,
  any key order), `GrayPresetLoadInfo::expandedVersion1`.
- [x] `lib/GameDisplayLab/GrayModel.h`, `GrayModel.cpp` -- `LEVEL_STEP` 32, `ORIGIN_NONE` 7, 3-bit target and source;
  the doses-left byte keeps the count in its low 5 bits and a pending second-leg target in its high 3 (0 none, else
  level + 1), so 3 B/pixel stays; `retarget` ignores a target past white (TRANSPARENT), looks up the via of the shade
  nearest the pixel's level, runs leg 1 to the via through the existing bookkeeping and keeps the final target; a
  retarget to the pending final target changes nothing, to the leg's own target drops the second leg; `deliverDose`
  starts leg 2 (the existing bookkeeping, no via of its own) when leg 1's last dose lands; routes directional, the
  origin recorded on every retarget of a settled pixel; `driveRect` clears a pending leg; `retargetBitmap(x, y, w, h,
  levels, preset)` returns the count of values above 7 (treated as transparent); `nextTarget(x, y)` accessor.
- [x] `lib/GameDisplayLab/TransitionScene.h`, `TransitionScene.cpp` -- `BANDS` = `SHADE_COUNT`, `LANES` 1,
  `laneShade` = (band + 3 + lane) mod 7, `flipOffset` +1..+6, marker shade shifted by `SHADE_COUNT / 2`, `shadeName`
  the level's digit ('?' past white), `heldRhythm` `HOLD_ALL`; comments say seven bands and six flips.
- [x] `lib/GameDisplayLab/StockBench.cpp` -- the ramp's part 0..3 to K, D, L, W through the named constants.
- [x] `src/activities/games/display_lab/GrayRunner.{h,cpp}` -- a public static `cleanWhiteBlackWhite(renderer, lab)`
  (three FULL refreshes through `lab.cleanNow()`: white, black, white; the caller holds the render lock); the replay's
  clean uses it and logs `gray sweep: cleaned white, black, white before the paint` once per replay; the cap clean
  stays one refresh.
- [x] `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}` -- the Clean row runs the same triple clean
  (its log line says so); comments and doc block say seven bands, one lane, six flips.
- [x] `src/activities/games/display_lab/TuningActivity.{h,cpp}` -- a Section row first (Doses, Holds, Other; tap or
  Confirm steps it, Doses on every visit, not saved); Doses: 42 rows `Dose 0>1` .. `Dose 6>5`; Holds: 42 rows; Other:
  retarget to dark, to light, History (0..3), Return to dark, Return to light, Reverse to dark, Reverse to light, 42
  `Via 0>1` rows (None or a level), Frame rate, Swap polarity, Reset, Save; arrays sized for the longest section.
- [x] `src/activities/games/display_lab/GrayPresetFile.cpp` -- encode buffer 1,024 B; `preset: version 1 file expanded
  to seven levels` logged on load.
- [x] `lib/I18n/translations/english.yaml` -- new `STR_GAMES_DLAB_*` keys for the Section row and its sections,
  History and its values, Return and Reverse rows, Via; the transition test and Tuning descriptions updated.
- [x] `docs/crosshatch/display-lab.md` -- the engine, defaults table, transition test, Tuning, clean and file-format
  sections rewritten for seven levels.
- [x] `test/game_display_lab/*` -- the matrix rows above, plus the pins already there regeneralised: level arithmetic,
  7×7 codec and version 1 expansion, both defaults, directional routes, via legs, retargetBitmap, band and lane
  geometry, six-flip coverage, stock bench unchanged, the viewer at seven levels.

**Acceptance Criteria:**
- Given a version 1 preset file on the SD card, when the lab opens, then it loads with its doses on levels 0, 2, 4, 6,
  the odd levels interpolated, the log says `preset: version 1 file expanded to seven levels`, and Save writes
  `"version":2`.
- Given Mode Static and Run, when six flips have settled, then every band has gone through all six other levels.
- Given a run starts (transition test or viewer), when the runner paints, then three FULL refreshes (white, black,
  white) came first and one log line says so.
- Given the Tuning screen, when the Section row is stepped, then the rows below are that section's.

## Implementation Notes

Implemented in the main session (orchestrated-epics § 3 fallback: no nested subagents), full route.

- **Model** (`GrayModel`): `LEVEL_STEP` 32, `ORIGIN_NONE` 7, 3-bit target and source; the doses-left byte keeps its
  count in five bits and a via's second-leg target (+1, 0 none) in three, so 3 B/pixel stays. `retarget` is the via
  dispatch (no-op when already going to `target`; to the leg's own target drops the second leg; otherwise the via of
  the level nearest the pixel's, unless it sits on the via) over `retargetLeg`, the old body. Guards kept from the old
  `retarget`: outside/invalid returns, and "same target" (now "same final target"); `retargetLeg` keeps a pixel's doses
  when the new leg is the one it already runs (a plain 0>2 asked for 1 via 2). `deliverDose` starts leg 2 when leg 1's
  last dose lands; its existing early return at 0 doses kept. Routes: the direction of the return
  (`routeIndex(toward)`), origin recorded for every settled pixel (`routeOf`, `midRoutesActive` gone). New
  `retargetBitmap` (returns the count above 7) and `nextTarget`.
- **Preset** (`GrayPreset`): 7x7 doses and vias, `uint64_t` hold mask, two routes indexed by `Toward`; the version 1
  expansion (`expandVersion1Doses`, `expandVersion1Holds`, `version1RouteFor`) also builds `inkBoyDefaults()`;
  `ultraChipDefaults()` is D14's table, `HOLD_ALL`, via 0>1 = 2. Codec version 2 with `via` (-1 none); arrays' lengths
  are checked against the version after the object, so keys may come in any order. To stay under 256 B,
  `loadGrayPreset` reads into `out` directly (restored to `defaults` on any failure; header says `out` and `defaults`
  must differ: both callers pass distinct presets) and lists stream into their destinations (no array buffers).
  Measured on the xtensa toolchain (`-Os -fstack-usage`): `loadGrayPreset` 192 B at baseline, 368 B on the first
  draft, 208 B now.
- **Stack** (same method, baseline from `git stash` of this tree): `DisplayLabActivity::onEnter` 128 -> 176 B (a
  first draft hit 304 with two presets in its frame: fixed by a `[[gnu::noinline]] setDefaults`),
  `TransitionTestActivity::allocateWorkingSet` 176 -> 32 B (a draft hit 272 through a temporary scene holding its
  held-rhythm preset: fixed by `TransitionScene::setScreen`), `GrayPresetFile::load` 144 -> 240 B, the viewer's
  `afterSlot` 224 B (its per-level text grows with `SHADE_COUNT`). All under 256.
- **Scene**: `BANDS` 7, `LANES` 1, lane (band + 3) mod 7, `flipOffset` +1..+6, marker `MARKER_SHIFT` 3, `shadeName` the
  digit, `heldRhythm` `HOLD_ALL`. Marker geometry unchanged; with one lane the bottom band's margin is its inset plus
  the leftover rows (12 rows at 480 high, 10 at 800).
- **Runner**: `GrayRunner::cleanWhiteBlackWhite` (static, the caller holds the render lock) for the replay clean (one
  `gray sweep: cleaned white, black, white before the paint` line) and the Clean row; the cap clean stays one.
- **Tuning**: Section row and three sections via `TuningLayout.h`; arrays sized 54 rows; the Save line kept in
  `saveLine` across section switches. New keys: Section, Doses, Holds, Other, History (+3 values), Return and Reverse
  to dark and light, As retarget, Via.
- **Stock bench**: the ramp maps its four parts to K, D, L, W; its switch gains the unreachable `TRANSPARENT` case.
- **Viewer**: no code change; three comments that said "four today" now say seven; its tests pass at seven, and the
  level readout test pins "0, 1, 2, 3, 4, 5, 6".
- **Doc**: `docs/crosshatch/display-lab.md` engine, defaults tables, transition test, holds, Tuning, preset file
  (version 2 and the version 1 expansion), memory, code map.
- **Files**: changed `lib/GameDisplayLab/{GrayModel,GrayPreset,TransitionScene,StockBench}.{h,cpp}`,
  `lib/GameDisplayLab/GrayPicture.h` (comment), `src/activities/games/display_lab/{GrayRunner,TransitionTestActivity,
  TuningActivity,GrayPresetFile}.{h,cpp}`, `DisplayLabActivity.cpp`, `GrayViewerActivity.{h,cpp}` (comments),
  `lib/I18n/translations/english.yaml`, `docs/crosshatch/display-lab.md`,
  `test/game_display_lab/{test_gray_model,test_gray_preset,test_transition_scene,test_gray_picture,
  test_stock_bench}.cpp`, `test/game_display_lab/CMakeLists.txt`; new `lib/GameDisplayLab/TuningLayout.h`,
  `test/game_display_lab/test_tuning_layout.cpp`.

## Plan Change Log

## Review Triage Log

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
  odd is the mean of its four anchor transitions, rounded up once (the same linear interpolation in both indices).
  Holds "by the nearest anchor pair": an odd level is equidistant from two anchors, and the tie goes away from the
  transition's other end (0>1 takes 0>2, 3>1 takes 4>0), the only tie rule that always yields an anchor transition in
  the same direction (rounding toward would make 1>2 the diagonal 2>2). A version 1 file has no `via`, so absent-key
  rules apply (the defaults' vias stay), as for every other absent key.
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
  `dose[0][1]` stays a valid 1..24 entry (D14's "1 (via 2)"), unused while the via is set.
- Defaults: D14's table and "rate index 0, one pulse per slot, every hold on" for `ultraChipDefaults()`; retarget 100 %
  both ways as before.
- Transition test (item 4): `BANDS` = 7, one lane, lane shade (band + 3) mod 7; the marker margin formula already gives
  the bottom band's inset with one lane. The marker's "opposite shade" (D13) generalises InkBoy's +2 of four to
  + `SHADE_COUNT / 2` (+3 of seven), so the pause symbol differs from its band by three levels or more on the
  wrap side.
- Double clean (item 7, D14's Slow run): in the shared runner's replay clean so the transition test, the Ladder base and
  the viewer get it; the session cap's clean is a clean during a run, not "a run's clean before the paint", so it stays
  one refresh. The cleans go through `lab.cleanNow()`, each restarting the cap count as today.
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

Readings for the orchestrator to confirm (each settled in Design Notes, none blocking): the file's via uses -1 for
none (item 6's in-memory 7 = none against item 8's "the codec rejects 7 anywhere in a table"); the hold key stays
`hold`; the version 1 dark route takes D>W>D's values; the hold tie rounds away from the other end; the marker's hold
level is three along; the session cap's clean stays one refresh; the Section row resets to Doses on each visit.

Residual risks: the via's second leg runs from the settled via level through the ordinary retarget, so a pixel
retargeted while on leg 2 behaves as any mid-transition pixel; on a screen under 112 px high the no-lane marker square
can cross into the band above (no device has one). Hardware check for the owner (experimental branch): Static shows
seven distinguishable bands after the first settle, the six flips show which of the 42 transitions need a dose change,
Tuning edits them, and a run begins with the white, black, white clean.


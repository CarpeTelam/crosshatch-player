---
title: 'Check every game at the X4 Pro''s 466-pixel canvas'
type: 'feature'
ticket: '11'
created: '2026-10-05'
status: done
baseline_revision: '7699d2e0705b8978fc5d4785a92fa02b88df99f1'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/api-level-1.txt'
warnings: [oversized]
deferred:
  - summary: >-
      The X4 Pro's inset values {9, 7, 3, 7} are typed in three places that nothing ties together: the SDK's XTEINK_X4_PRO profile, the games check's CANVAS_466 (pinned to GameViewport::forRenderer by CanvasSizesTest with the insets typed there), and the simulator shim.
    evidence: |-
      The SDK header (freeink-sdk BoardConfig.h:1697) is the only source and is not host-includable here; its pointer may not move for fork work. If an SDK bump changes the X4 Pro's insets (its X4 Pro note says "pending measurement" for a sibling profile), every host test and the simulator still say 466 while the device gives another size, and the games are not checked at it. A script check that reads the profile's inset line, or the SDK exposing the insets to host tests, would settle it.
    location: >-
      test/game_script/harness/games_check/GamesCheckRig.h (CANVAS_466), .claude/skills/run-crosshatch-player/shim/BoardConfig.h
    severity: medium
  - summary: >-
      Sudoku's checks VM (`TheGamesOwnChecksPass`) is still thin: about 3.5 KB below the 262,144 B Lua cap, so a future addition to `checks.lua`, `solver`, `counter`, or the bank can turn the required games-check red.
    evidence: |-
      Measured on this tree by lowering the cap in a scratch build (not committed): it passes at 258,600 B and fails at 258,300 B on each of 40 fixed arena addresses, so the margin is stable across runs (spread under 300 B) but small; the rounds VM, which was the flaky one, now has about 24 KB. UTTT's checks VM passes down to 250,000 B (it failed once at 245,000 B and passed at 240,000 B, so about 12 KB at worst) and Battleship's down to 240,000 B. Cutting it needs the bank, solver, and counter (about 100 KB of the live set) out of one VM, for example by verifying the bank from rounds.
    location: >-
      test/game_script/first_party/sudoku/checks.lua, test/game_script/first_party/README.md ("The checks VM heap")
    severity: medium
  - summary: >-
      Nothing in CI proves the simulator's X4 Pro shim is in effect: the simulator job only builds simulator_x4pro.
    evidence: |-
      The shim was proven here by the startup log line ("[SIM] X4 Pro bezel insets {9, 7, 3, 7}" in build/sim/sim.log) and by the shots (the Sudoku grid's border at screen x 14 to 466). A change of PlatformIO's include handling, or of the library's selectDevice, could silently return the simulated X4 Pro to 474 x 788 with every host test green. A CI step that starts the simulator and greps the log, or a check in sim.sh, would settle it.
    location: >-
      .claude/skills/run-crosshatch-player/shim/BoardConfig.h, .github/workflows/crosshatch-ci.yml (simulator-build)
    severity: low
---

<intent-contract>

## Intent

**Problem:** The X4 Pro gives a game a 466 x 788 canvas (bezel insets {9, 7, 3, 7}) and the Sticky 474 x 788, but every check and every simulator shot in this epic ran at 474, so no game was ever seen at 466; two comments call 474 the X4 Pro's; and each game lays out from `ch.screen`, so the two boards show different pixels (Sudoku's cells are 50 px on one and 51 px on the other).

**Approach:** Per the owner's two Decisions of 2026-10-05 (the epic Notes' last two lines): every game lays out one fixed 466 x 788 design box and centres it in `ch.screen` (the Sticky gets 4 px of white each side, the layout and tap targets are the X4 Pro's), the games check plays every round and check at 466 x 788 and at 474 x 788 and confirms the same layout at both, the simulator's X4 Pro runs the device's insets, and the two comments say 466 for the X4 Pro and 474 for the Sticky.

## Boundaries & Constraints

**Always:** API level 1 only; no engine, `src/`, or `lib/` change except the comment in `src/games/GameViewport.h`; `games/<id>/` stays self-contained (R2: no shared file or `require`, no repository path; each game's board module stays its own copy) and the games check names no game; a game still reads `ch.screen` (to centre the box and to say in one `ch.log` line, once per VM, when the canvas is under 466 x 788: unsupported, laid out from the canvas's corner, never adapted); every tap target keeps at least 44 px; Sudoku's cells are 50 px on every canvas; the note tiles' dither phase holds (the box's offset on 474 is 4, even; the canvas origin's x + y is even on both boards); snapshots, moves, and state untouched (R9); a frame stays within the engine's command limits; formatting as AGENTS.md says.

**Never:** Adapt a layout per canvas (no second set of numbers for 474); edit a file upstream also has beyond the ledger (`src/games/GameViewport.h` is a fork path, `docs/crosshatch` is too); move `freeink-sdk` or edit the simulator library under `.pio`; add a hard-coded game name or path to the games check; change `API_SURFACE_CRC` unless the comment edit moves the surface CRC (then follow the value the test prints).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| X4 Pro canvas | `ch.screen` 466 x 788 | box origin (0, 0); UTTT cells 50 px at x 8; Battleship big board x 13; Sudoku cells 50 px at x 8 | none |
| Sticky canvas | `ch.screen` 474 x 788 | every x of the 466 layout plus 4; same cells, targets, tiles; 4 px white each side | none |
| Taller canvas | 480 x 800 (not a device) | box centred: origin (7, 6) | none |
| Canvas under the box | 320 x 480 | box origin clamped to (0, 0); one `ch.log` line naming the canvas and the 466 x 788 the game needs; no per-canvas layout | line logged once, no fault |
| Break at 466 only | a game faults or mis-lays only at 466 | the games check fails at 466 and passes at 474 | check names the round or check |
| Simulator X4 Pro | `simulator_x4pro` | `GameViewport` 466 x 788 at (7, 9); a game shot shows it | none |

</intent-contract>

## Code Map

- `test/game_script/harness/games_check/GamesCheckRig.h` -- `CanvasSize`, `CANVAS_474`, `CANVAS_466`; `GamesCheckRig::create(seed, canvas)`. DONE by the planner (see Implementation Notes).
- `.../ScriptVm.{h,cpp}` (`OwnedVm::create(..., error, canvas)`), `RoundPlayer.{h,cpp}` (`GameUnderCheck::canvas`), `GameCheck.{h,cpp}` (`Roots::canvas`, passed to the checks VM, the clash probe, and each round's VM and player) -- DONE.
- `.../GamesCheckTest.cpp` -- every game's three tests run again as `Games466/GamesCheck466Test` (466) beside `Games/GamesCheckTest` (474) -- DONE. `GamesCheckEngineTest.cpp` (`TheRoundsAndTheGamesOwnChecksSeeTheCanvasTheRootsName`, `AGameThatBreaksOnlyAtTheX4ProCanvasFailsTheCheckThereAndOnlyThere`) and `GamesCheckFlowTest.cpp` (`CanvasSizesTest`: the two sizes equal `GameViewport::forRenderer` on the stub renderer with insets {9,3,3,3} and {9,7,3,7}) -- DONE.
- `.claude/skills/run-crosshatch-player/shim/BoardConfig.h` + `simulator.ini` (`-include` on `simulator_x4pro` only) -- DONE: the library's `BoardConfig.h` (pinned crosspoint-simulator) gives its X4 Pro profile the default insets, and `HalGPIO::begin` re-selects the profile at startup, so a static initializer would be overwritten; the shim is force-included, includes the library's header with `selectDevice` renamed by a macro, and defines a `selectDevice` that sets {9, 7, 3, 7} after selecting the X4 Pro and logs a `[SIM]` line. Its effect on the simulator (a 466 x 788 canvas) is checked in Verification.
- `games/ultimate-tic-tac-toe/board.lua` and `games/sudoku/board.lua` (identical copies, keep them identical): add `board.W, board.H = 466, 788`, `board.origin(w, h)` (`max(0, (w - W)//2)`, `max(0, (h - H)//2)`), `board.fits(w, h)`; `board.layout(w, h, opts)` computes `cell` from W and H (not w and h) and returns `x = ox + (W - 9*cell)//2`, `y = oy + top` plus `ox`, `oy` in L.
- `games/ultimate-tic-tac-toe/main.lua`: the header text y 40, the question icon at `ch.screen.w - 72, 12`, `tapIsHelp` (`HELP_X`/`HELP_Y` from the right edge and top), the HOW TO PLAY page (x 24, y 30 and `help_lines` from y 100, wrap width `ch.screen.w - 48`) all move into the box (`ox`/`oy` added, W - 48 wrap, help zone `ox + W - HELP_X <= x < ox + W`); `game.draw` logs the unsupported-canvas line once.
- `games/battleship/layout.lua` (`compute(w, h)`): design W, H; `ox`, `oy` added to every x and y it returns (`question`, `question_rect`, `big`, `small`, `column` (`w = ox + W - 8 - columnX`), `status`, `tray`, `buttons`, `message`, `over`, `over_label_y`, `over_counts_y`), `L.ox`, `L.oy`, `L.W`; `layout.fits`. `games/battleship/main.lua`: header text y 12 and `drawHelp` x 24, y 24, `help_lines` (y 90, wrap `ch.screen.w - 48`) take the box; `game.draw` logs once.
- `games/sudoku/layout.lua` (`get()`, `help_area`, the menu rows' `row_x/row_y/row_w`, `kh`, `row_h` from W and H and the box), `view.lua` (header y 13, MENU title (24, 13), HOW TO PLAY (24, 24), `draw_end`'s `cx` and y's), `help.lua` (wraps to `layout.help_area`): all into the box; `layout.note_tile`'s comment now says 50 px cells everywhere (its formula stays); the logged line goes in `view.draw`.
- Companion checks to update with the layout: `test/game_script/first_party/ultimate-tic-tac-toe/{checks,taps}.lua` (`SIZES` rows to the box: 474 -> cell 50, x 12; 466 -> x 8; 480 x 800 -> x 15, y 126; drop 320 x 480 from the exact-geometry check and test it as the unsupported-canvas case instead; `taps.help` and `taps.off_grid`), `battleship/{checks,draws,taps}.lua` (absolute numbers in `geometry`, the over screens, the column, the help tap), `sudoku/{checks,rules,taps}.lua` (`rules.layout`, `expect_tiles` now 50 px cells and pitch 15 on every canvas, the frame and worst-frame checks, taps), their `rounds/`, and `test/game_script/first_party/README.md`.
- Add, in each game's checks, the same-layout pin: the rects and targets the layout returns at 474 equal the 466 ones shifted by 4 in x (`ch.screen.w` swapped temporarily as `rules.expect_tiles` does, then restored), the unsupported-canvas case (a 320 x 480 canvas: one `ch.log` line from a draw, none from a second draw, no fault), and a draw at 474 whose commands equal the 466 draw's with x + 4 (via each game's `trace.lua` recorder where it has one).
- Comments: `docs/crosshatch/api-level-1.txt` around line 384 (frame-pixels note: the X4 Pro is 466 x 788 = 367,208 pixels, the Sticky 474 x 788 = 373,512; "About 2.8 canvases" stays true) and `src/games/GameViewport.h` line 11 ("on the X4 Pro 466 x 788, on the Sticky 474 x 788 of the 480 x 800 portrait screen"). Comment-only: run `ApiLevelTest`/`ApiSurfaceTest`; if the surface CRC counts the comment, set `API_SURFACE_CRC` (`lib/GameCore/ApiLevel.h`) to the value the test prints and say so.
- Harness comments naming 474 as the one device canvas (`RoundFile.h`, `RoundFile.cpp`/`RoundFileTest.cpp` line 135 text, `RoundPlayer.h`, `GamesCheckRig.h`, `first_party/README.md` line 26) say the check plays both.
- `.claude/skills/run-crosshatch-player/SKILL.md`: one line that `x4pro` runs the device's insets (466 x 788 canvas) and `sticky` the default (474 x 788).
- Do not change: `lib/`, `src/` (but the comment), `freeink-sdk`, the simulator library, `scripts/check_*`, upstream `ci.yml`, puzzles, the note PNGs, snapshot formats.

## Tasks & Acceptance

**Execution:**
- [x] games check: a canvas in the rig, VM, player, `Roots`, and a `Games466` suite beside the 474 one; engine tests for the canvas and a 466-only break; the `forRenderer` pin -- done by the planner before the second Decision.
- [x] simulator: the X4 Pro insets shim and its `-include` -- done; the first version used `-I`, which PlatformIO orders after the library folders, so the library's header won (found by the review's edge-case lens and by checking the built binary); now proven by the startup log line and the shots, see Verification.
- [x] `games/*/board.lua` (two identical copies), `games/ultimate-tic-tac-toe/main.lua`, `games/battleship/{layout,main}.lua`, `games/sudoku/{layout,view,help}.lua` -- lay out the 466 x 788 box centred in `ch.screen`, log once under it -- the second Decision.
- [x] the three games' companion `checks`, `rules`, `draws`, `taps`, and rounds -- follow the layout; add the same-layout pins and the unsupported-canvas case.
- [x] the two comments, `SKILL.md`, the harness comments and `first_party/README.md` -- say 466 for the X4 Pro and 474 for the Sticky and that every game lays out one 466 x 788 box.
- [x] a scratch mutant per game that breaks only at one canvas turns the games check red (not committed; recorded in Verification).

**Acceptance Criteria:**
- Given any of the three games, when the games check plays its rounds and checks at 466 x 788 and at 474 x 788, then both pass, and the game's cells, targets, and note tiles at 474 are the 466 ones shifted 4 px right.
- Given a canvas under 466 x 788, when a game draws, then it logs one line and does not fault.
- Given `simulator_x4pro`, when a game runs, then the canvas is 466 x 788 at (7, 9), and the Sticky's shows the same screen pixels inside the box.
- Given the two comments, then they say 466 for the X4 Pro and 474 for the Sticky.

## Implementation Notes

Written by the planner before the implementation subagent starts: the harness canvas, the engine tests, and the simulator shim were built first (the planner measured that every game's existing checks pass at 466 under the first reading of the Decision), then the second owner Decision (the fixed box) arrived and the rest of this plan was made to it.

## Plan Change Log

- Re-planned after the owner's Decision of 2026-10-05 (epic Notes, last line, relayed by the orchestrator with the entry's first plan half built): the first reading, "check each game at 466 and fix where it breaks", is replaced by the fixed 466 x 788 design box centred in `ch.screen`. The harness and simulator work already built is unchanged and still needed (the check still plays both canvases). KEEP: the `Games466` suite, the shim, the engine tests.
- Folded in after the owner's further Decision of 2026-10-05 (epic Notes, last line; spine AD-7 amended; relayed by the orchestrator, `7699d2e0`): 466 x 788 is the default canvas for every game, and entry 11's `touches` add `docs/crosshatch/game-canvas.md`. The planner wrote that paragraph itself (the box centred in `ch.screen`; X4 Pro and Paper Mono 466 x 788, Sticky 474 x 788; smaller unsupported and logged; the games check plays both) in the implementation pass, so the implementation subagent does not touch the file. Nothing else changes.
- Baseline moved from `40f6a3bf` to `7699d2e0` (HEAD when the implementation was reviewed): the commit between them is the orchestrator's docs-only Decision (spine AD-7, epic Notes, `tickets.toml` touches), not this entry's work, and would otherwise sit in the reviewed diff.

## Review Triage Log

### 2026-10-05 — Review pass
- verdicts: 32 findings (rows below) — high 2, medium 10, low 13, false 7, maybe-false 0; routes: patch 15, defer 5, reject 12; the two `high` rows share one root cause (the shim). Four lenses ran as context-free subagents: Blind Hunter 15 findings, Edge Case Hunter 6, Verification Gap 6 (2 filed and 4 `Other findings`), Intent Alignment 5 divergences (descriptive, each judged).
- findings:
  - Blind Hunter
    - `[medium]` `[patch]` docs say a game logs "instead of drawing off the canvas" but the games log and then draw from the corner — `game-canvas.md` reworded to what the games do (no adaptation, one log line, laid out from the corner, clipped); the Design Notes already record the reading.
    - `[low]` `[patch]` wrong cross-reference in `GamesCheckRig.h` (names a test that does not exist) — now names `CanvasSizesTest.AreTheViewportsOfTheTwoBoardsInsets` and says it types the insets itself.
    - `[medium]` `[defer]` the insets {9, 7, 3, 7} are typed in three places (SDK profile, `CANVAS_466`/`CanvasSizesTest`, the shim) and nothing ties them — the SDK header is not host-includable and its pointer may not move for fork work; deferred, see `deferred`.
    - `[low]` `[reject]` the 466 x 788 box is hard-coded in many places (two `board.lua` copies, Battleship's `layout.W/H`, checks) — by design: R2 keeps each game self-contained and its companion folder its own, with no shared file; a shared constant would break that.
    - `[medium]` `[patch]` `GameCheck.cpp`'s probe and `GamesCheck466Test.ThePackageInstallsAndLoads` ignored the canvas (a duplicate of the 474 test) — `checkPackage`'s module-loading probe now takes `roots.canvas`, so a module load that reads `ch.screen` runs at 466 in that test.
    - `[medium]` `[defer]` the Sudoku rounds-VM memory fix (generational GC, forced collections) hides rather than removes a thin heap margin — verified real: without the collector changes the Sudoku rounds fault "not enough memory" on 93 of 150 runs with ASLR on and 150 of 150 with ASLR off; with them 0 faults (see Verification for the bar); the margin stays a few KB; deferred, see `deferred`.
    - `[false]` `[reject]` `expect_tiles` is redundant (both entries 50 px / 15 px) — it is the same-layout pin for the two canvases' `layout.get()` (cache keyed by `w`), and it checks the parity on both.
    - `[low]` `[patch]` Sudoku's frame digest folded a string by length and first and last byte, so a middle-character difference passed — `fold` now folds every byte; a scratch mutant (header "Sudokx" at 474 only, same length and ends) turns the games check red now, which the old fold could not show.
    - `[low]` `[reject]` helpers duplicated across the games' checks (`withCanvas`, `logged`, `sameShifted`) — by design: R2, each game's companion folder is self-contained and may leave with it.
    - `[low]` `[patch]` literal `788 - 40` in `sudoku/rounds/how-to-play.lua` — now `require("board").H - 40`; the other literals in Battleship's checks pin the 466 and 788 the games are specified at, and stay.
    - `[false]` `[reject]` the Paper Mono 466 x 788 claim is unsupported — the owner's Decision says so and the SDK's `PAPER_MONO` profile ends `{9, 7, 3, 7}` (`BoardConfig.h:1062`).
    - `[low]` `[patch]` "About 2.8 canvases" in `api-level-1.txt` is 2.9 on the X4 Pro (1,048,576 / 367,208 = 2.86) — comment now says 2.9 and 2.8; `API_SURFACE_CRC` unchanged.
    - `[false]` `[reject]` the decision documents are not in the diff — the spine, epic Notes, and `tickets.toml` changes are the orchestrator's commit `7699d2e0` and `40f6a3bf`, before the baseline, so outside this diff by construction; the stale-474 grep was done (next row).
    - `[high]` `[patch]` the simulator shim's effect is unverified, and it is not in effect: the review found `-I` folders come after the library's in PlatformIO, so the library's `BoardConfig.h` won — verified on the rebuilt binary (no `[SIM]` string) and on the compile line (library `-I` 68th, project 71st); fixed by force-including the shim (`-include`) with the header guarded for C and libraries, a `[SIM]` startup log line, and the proof in Verification (log line; grid border at screen x 14 to 466).
    - `[low]` `[patch]` UTTT's `tapIsHelp` comment omits that the margins and outside-the-box are a miss, and `sayUnsupported`'s log line is over 120 columns — comment added, line wrapped; other new Lua lines over 120 columns wrapped.
  - Edge Case Hunter
    - `[high]` `[patch]` same as the shim finding above (the shim is not found first) — fixed and proven as above (counted once as `high`; this row is the same root cause).
    - `[medium]` `[patch]` three comments still call 474 the X4 Pro's/the boards' canvas (`pass-open/main.lua:10`, `stubs/GfxRenderer.h:55`, `FrameReplayTest.cpp:431`, and others the grep found: `GameMatchTest`, `GameVmTest`, `GfxBindingsTest`, `LuaGameFixture.h`, `GameTouchTest`, `fixtures/README.md`) — reworded to the Sticky's / the test canvas; no value changed.
    - `[medium]` `[patch]` the 466 package test never uses 466 — same fix as the probe row above.
    - `[low]` `[patch]` wrong test name in the rig comment — same fix as above.
    - `[low]` `[reject]` the note-tile dither phase is unguarded at odd box offsets (480 x 800 gives (7, 6)) — only canvases no device gives; the Decision names the even offset on 474 and the device (7, 9) origin; no named user is hurt, and the fix adds a guard.
    - `[low]` `[patch]` the digest folds only the ends of a string — same fix as the digest row.
  - Verification Gap
    - `[medium]` `[defer]` nothing runs the shim in CI (the simulator job only builds) — the checking was done here (startup log, shots); a CI log assertion is a new gate the intent does not ask for; deferred as low.
    - `[medium]` `[defer]` the canvas pin does not read the SDK profile — same as the typed-constants row; deferred once.
    - `[medium]` `[patch]` the 466 package test duplicates 474 — same as above.
    - `[low]` `[patch]` rig comment names a missing test — same as above.
    - `[medium]` `[defer]` the Sudoku rounds heap residual — same as the heap row above.
    - `[false]` `[reject]` the Paper Mono claim — same as above: the SDK profile and the Decision.
  - Intent Alignment (descriptive; each divergence judged as a finding)
    - `[low]` `[reject]` the check proves layout at the Lua level on a typed canvas, not through the real viewport and device fonts — the intent's check is the games check (`ch.screen` 466 wide for those runs); the real viewport is pinned once (`CanvasSizesTest`) and the simulator shots cover the device surface; the harness's stand-in metrics are a known, documented limit.
    - `[false]` `[reject]` Battleship's same-layout pin is layouts only — it also compares every `draw` command shifted 4 px (`draws.lua` `sameShifted`, `rounds/draw-commands.lua`), which the mutant run (cell 43 at 466 only, header x at 474 only) shows red.
    - `[low]` `[reject]` the harness default canvas stays 474 — the Decision's "default canvas" is for the games' design and `game-canvas.md`; changing the default would move older fixtures (taps typed at 474) with no observable gain, since the check plays both.
    - `[false]` `[reject]` the unsupported-canvas log is untested on a device — no device gives one; it is pinned at 320 x 480 in each game's checks.
    - `[false]` `[reject]` `SKILL.md` assumes the Sticky's insets — the Sticky shots (pixel-identical to the X4 Pro's inside the box) are the evidence.

### 2026-10-05 — Review pass (follow-up pass, `followup_pass`)
- verdicts: 28 findings (rows below; the lenses' 13 + 2 + 5 + 6 = 26, two rows split) — high 0, medium 8, low 17, false 3, maybe-false 0; routes: patch 10, defer 4 (all in `deferred`, carried), reject 14. Four lenses ran again as context-free subagents on the whole diff since the baseline (Blind Hunter 13 findings, Edge Case Hunter 2, Verification Gap 3 filed and 2 `Other findings`, Intent Alignment 6 divergences, judged as findings). The first pass's rows were carried where the claim and the code were unchanged.
- findings:
  - Blind Hunter
    - `[medium]` `[defer]` `carried` the insets {9, 7, 3, 7} are typed in four places and `CanvasSizesTest` types its own — same as the first pass's deferred item.
    - `[low]` `[defer]` `carried` the simulator shim fails silently when `<BoardConfig.h>` is not found and nothing automated checks it — same as the first pass's deferred item (low).
    - `[medium]` `[patch]` the timing fixture's pixel-budget test has no 466 x 788 size, and `GameTouchTest`/`FrameReplayTest` have no X4 Pro origin — `{466, 788}` added to `TheTimingFixturesBandsSitAtTheLimitsOnEveryCanvas` (passes: band 2 is exactly the budget at 466, which the fixture's owner runs on the X4 Pro); the touch and replay mapping is engine code this entry does not touch and `CanvasSizesTest` pins the viewport's origin, so no new touch test (reject for that part, low).
    - `[medium]` `[patch]` the README's Sudoku heap numbers contradicted each other ("5 to 8 KB" against "3.5 KB"), one sentence was unwrapped, and about 25 lines of incident narrative sat in the contributor README — the two measures are named as two measures in `README.md` and `sudoku/checks.lua`, the narrative is cut to the cause, the rule, and the measured margins (the full account is in Design Notes).
    - `[low]` `[reject]` the flake fix is bundled into the canvas change — it is this follow-up pass's own commit (the orchestrator asked for it here), separate from the first pass's commit; moved pins are listed in Design Notes and every original pin line is accounted for (a line-by-line count of the old file against the five new ones leaves only renames).
    - `[low]` `[reject]` `expect_tiles` is vacuous (both entries 50 px / 15 px) — same as the first pass's `false` row, carried: it pins `layout.get`'s cache keyed by width and the parity on both widths.
    - `[medium]` `[patch]` nothing checks that every command at 466 stays inside the canvas for Ultimate tic-tac-toe and Sudoku — UTTT's `frame()` and Sudoku's `watch` recorder now assert that every rect, line, text, and image starts on the canvas and every rect and line ends on it (UTTT's gated to canvases of at least 466 x 788, since the 320 x 480 case is laid out off the canvas by design); a text's width is the device's metrics, which the check does not have.
    - `[low]` `[reject]` the 466 pass repeats canvas-independent work (the package load, the bank verification) — about 1.4 s a game; the intent has the check play every round and check at both canvases, and splitting out the canvas-independent ones needs a tag per check for no named harm.
    - `[low]` `[reject]` `CANVAS_474` is a silent default argument — every production call site in `GameCheck.cpp` and `RoundPlayer.cpp` passes `roots.canvas` or `game.canvas` (read: lines 275, 339, 447, 460, `under.canvas`); only tests of the harness itself use the default.
    - `[low]` `[reject]` `carried` helpers are copied across games — by design, R2 (first pass).
    - `[low]` `[reject]` `carried` a canvas under 466 x 788 is "unsupported" only by a log line, and the doc's list of boards — owner Decision 2026-10-05 (the epic Notes); the doc's boards are the Decision's.
    - `[medium]` `[patch]` the Sticky's layout changed (cells 51 to 50 px) and no Sticky screenshot is in the packet, and an unchanged manifest `version` may not refresh an installed copy — six Sticky shots (all three games) copied into the packet (Verification); the installer replaces `/.games/<id>/` with the new folder whatever the version and writes the new package hash to `.pkg` (`GamePackageInstaller.cpp` `commit`), nothing compares versions, so an installed copy refreshes when the package is installed again.
    - `[low]` `[patch]` Battleship's dialog and banner bounds subtract `oy`, which weakens them on 480 x 800 — both bounds are now in canvas pixels and only for the 788-tall canvases the devices give (the dialog and banner are the host's, not the box's).
    - `[low]` `[patch]` UTTT `smallCanvas` says no cell is under 44 px and asserts nothing — an assertion added; the stray double blank line in UTTT's `checks.lua` removed.
    - `[low]` `[reject]` the digest's `fold` maps `nil` and `false` to one value, and `CanvasSizesTest` sits outside the anonymous namespace of `GamesCheckFlowTest.cpp` — a rect with `filled` omitted at one canvas only has no cause in the layout code, and the test's place is a tidy-up with no named harm.
  - Edge Case Hunter
    - `[low]` `[patch]` the UTTT help-tap loop tests a 470-wide canvas, not the Sticky's 474 — the loop now runs 466 and 474, the tap at `w - 1` hits at 466 and misses at 474.
    - `[low]` `[reject]` the shim sets the insets only in `selectDevice`, so a read before `HalGPIO::begin` sees 474 — nothing reads the insets before `begin` (the game viewport is made on a game's start), and the log line prints once per `selectDevice`, which runs once at startup.
  - Verification Gap
    - `[medium]` `[patch]` the timing fixture has no 466 size — same as above.
    - `[medium]` `[defer]` `carried` the 466 premise is typed twice and never read from the SDK profile — same as the first pass's deferred item.
    - `[low]` `[defer]` `carried` the simulator's bezel override has no automated check — same as the first pass's deferred item (low).
    - `[low]` `[patch]` the UTTT help-tap loop (a 470-wide canvas) — same as above.
    - `[low]` `[reject]` `tapIsHelp`'s lower y bound is tested only at `oy = 0` — no device has a taller canvas.
  - Intent Alignment (descriptive; each divergence judged)
    - `[low]` `[reject]` `carried` the check proves layout on a typed canvas, not through the real viewport and device fonts — first pass.
    - `[false]` `[reject]` the checks are self-consistent geometry, not pixels — the pixels are the 15 simulator shots in the packet (nine X4 Pro, six Sticky), each looked at; the geometry pins are the gate the intent names.
    - `[low]` `[reject]` `carried` the simulator surface is not tested — first pass; the shots are committed now.
    - `[false]` `[reject]` the games were redesigned rather than only fixed — the owner's Decisions of 2026-10-05 (the epic Notes) ask for the fixed 466 x 788 box.
    - `[medium]` `[patch]` Sudoku's memory stability is covered only by README prose and the checks VM is still thin — the rounds VM's margin went from under 3 KB to about 24 KB and the proof bar is recorded in Verification; the checks VM stays at about 3.5 KB and is a deferred item (stable across 40 arena addresses).
    - `[false]` `[reject]` the 466 pass doubles the Sudoku VMs under the required label — the run adds about 3 s; the cost is the intent's.

## Design Notes

- Sources that settle the choices: the epic Notes Decisions of 2026-10-05 (the 466 canvas; the fixed box; Sudoku's 50 px cells; "the centring offset is even on 474, so the note tiles' dither phase holds"; a smaller canvas is unsupported and logged, not adapted; whether level 1 promises a minimum canvas is epic-api-freeze's). `tickets.toml` entry 11's `unknown`: the simulator takes the insets from `.claude/skills/run-crosshatch-player/shim/BoardConfig.h` (a fork file, only `simulator_x4pro`'s `-I`), and the games check's second canvas needs no change outside the check's own files (`GamesCheckTest.cpp` and the files beside it).
- Why a shim: the pinned simulator library defines `ACTIVE`, `selectDevice`, and the X4 Pro profile in its own `BoardConfig.h`, and `HalGPIO::begin` calls `selectDevice(XteinkX4Pro)` at startup, which copies the profile over `ACTIVE`; so the override must run after that call, and wrapping `selectDevice` (a macro rename around the library's include) is the one place in fork files. It is force-included (`-include`), not an `-I` folder: PlatformIO puts a project's `-I` after the library folders (compile line of `src/main.cpp`: the simulator library's `-I` is 68th, the project's `-Isrc` 70th), so a shadowing `BoardConfig.h` is never found first. The header is guarded by `__cplusplus` and `__has_include(<BoardConfig.h>)`, since every translation unit (C files, libraries with their own include path) gets it. The Sticky env keeps the default, which is the Sticky's own.
- Smaller-than-box reading: "a canvas smaller than 466 x 788 is unsupported, and each game says so in a log line rather than laying out off the canvas" is built as: one `ch.log` line once per VM from `draw`, and the box origin clamped at (0, 0); no other adaptation exists, so no outcome differs between readings on any supported canvas.
- Tap targets: the box's targets are the 466 ones (a tap in the 4 px margins is a miss), the same on both boards.
- No function is moved: `board.layout`, `layout.compute`, `layout.get` keep their callers; each only gains `ox`/`oy`. `layout.get`'s cache key stays `w, h`.

**Follow-up pass: the Sudoku rounds' memory fault (the orchestrator's must-fix).**
- Cause, measured (scratch builds, not committed): with the arena at one fixed address (mmap), three processes made the same 688,832 `luaAlloc` calls, call for call; with ASLR the sequences differ from about the 1,330th call, where the order of a table's rehash (776 B) and a code-vector growth (64 B) swaps (read with gdb: `luaH_resize` from `llex` in one process, `luaM_growaux_` from `addk` in another). Lua 5.5's compiler keys a function's `nil` constant by its own constants table (`nilK`, `lcode.c`), and `hashpointer` takes the low 32 bits of an address, so the arena's ASLR base moves that key's node and the table's rehash. The string-hash seed is not the cause: `ScriptVm` and `LuaGame` pass `ports.random.next32()` to `lua_newstate`, seeded by the round. The cap was refused 14 to 28 times in most rounds (the heap lives at the cap and emergency collections do the work), and a stack growth cannot collect, so a few KB of margin left a rare ordering that failed.
- Not reproduced on demand: 600 fixed arena addresses (19,200 plays) at the real cap on the old tree did not fault, so the 1-in-1,600 rate is the first pass's figure, not measured again; what was measured is the margin: the old tree's worst round (`notes-digits`) failed at a cap of 259,000 B and passed at 259,500 B, the new tree's worst (`notes-dots`) fails at 236,000 B and passes at 238,000 B (60 fixed addresses all passed at 238,000 B).
- Not taken: pinning the arena address in the rig (mmap hint) would make the check repeat, but only where the kernel honours the address (not under ASAN's shadow range or on a host that refuses the hint, and a round holds two rigs at once), so the margin is the fix. A `luai_makeseed` constant would not have helped (the seed is passed in).
- The split: `rules.lua` (920 lines) into `pins.lua` (shared helpers, `digit_of`, `units_of`, `with_clock`, the generational collection), `rules.lua` (apply, undo_cell, undo_fill, ring, clash, hint, layout, clock), `interaction.lua` (taps, store, reset), `drawn.lua` (record, watch, expect_*, frame, look) and `marks.lua` (draw_marks). Each moved function is verbatim; the guards it carries: `rules.taps`, `frame`, and `look` start with `collectgarbage("collect")` and `frame` and `look` end with one (the heap), `record` puts the real `ch.gfx` back after an error, `expect_tiles` puts `ch.screen.w` back after an error, `with_clock` puts the real `ch.time` back after an error (all three unchanged), and `answer` caches solutions per clue string (unchanged). The one edit is the clock: `rules.clock` and `interaction.reset` shared a module-level `clock` local, which is now `pins.time.now`. Every original non-comment line is accounted for (a count of the old file's lines against the five new files leaves only the renamed function headers and the clock). The rounds that call them require the module that holds them (`notes-digits`, `solve-*`: `drawn`; `notes-dots`: `interaction` and `drawn`; `toggles`: `interaction` and `marks`). A module requires the game's modules before `pins`, so `pins`'s own requires are cache hits and nothing nests (the load-nesting rule is main's).
- The checks VM is not changed and stays at about 3.5 KB; it is a deferred item (stable on 40 fixed addresses: passes at 258,600 B, fails at 258,300 B).
- Orchestrator's other points: the manifest `version` is not compared (the installer replaces `/.games/<id>/` by id and writes the package hash to `.pkg`, `GamePackageInstaller.cpp` `commit`), so an unchanged `1.0.0` with new contents refreshes on install; the Sticky shots are in the packet.

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` (under the hosttest lock) -- expected: all pass, including `Games/GamesCheckTest`, `Games466/GamesCheck466Test`, `GamesCheckEngineTest`, `GamesCheckFlowTest`, `ApiLevelTest`, `ApiSurfaceTest`.
- `python3 scripts/*_test.py` each, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new.
- Scratch mutants (not committed): a game breaking only at 466 (and only at 474) turns the games check red.
- After the review, under the build lock: `.claude/skills/run-crosshatch-player/sim.sh build x4pro` (and `sticky` for the same-pixels comparison); `pio run -e x4pro` and `-e default` only if a file under `src/` other than the comment-only `GameViewport.h` changed (then `pio check`).

**Manual checks:**
- Simulator `x4pro` shots (466 x 788), each looked at for clipping and overlap: Ultimate tic-tac-toe in play and HOW TO PLAY; Battleship placement, firing, Over; Sudoku with digit notes in a SHADE PEERS row, the pad and rail, the menu. The Sticky simulator's same screens inside the box are pixel-identical.

**Results on the final tree (the commit's tree; `7699d2e0` plus this entry's changes):**
- Host suite: `ctest --test-dir build/test -j` 1,775 of 1,775 pass (after the review patches), including `Games/GamesCheckTest` and `Games466/GamesCheck466Test` (21 tests, all three games at 474 and at 466), `GamesCheckEngineTest` (the canvas reaches rounds and checks; a game breaking only at 466 is red there and green at 474), `GamesCheckFlowTest` (`CanvasSizesTest`), `ApiLevelTest`, `ApiSurfaceTest` (`API_SURFACE_CRC` unchanged: the comment is not in the surface).
- Fast checks: every `scripts/*_test.py` passes (13), `python3 scripts/check_upstream_touches.py` PASS, `./bin/clang-format-fix` run twice, the second run changing nothing; it reflowed a comment in this entry's own `shim/BoardConfig.h` and nothing outside this entry's paths.
- Mutants, scratch only, `games/` restored and `diff -r` clean after each, each run on the review-patched tree: a Sudoku header with a changed middle character at 474 only (same length and ends: the old digest could not see it) red in `Games/` and `Games466/` `EveryRoundPlays`; a UTTT header y +1 at 474 only red in `TheGamesOwnChecksPass` (both suites); a Battleship cell 43 at 466 only red in `EveryRoundPlays` and `TheGamesOwnChecksPass`. The implementation also ran one-canvas mutants per game before the review patches (list below).
- Flake bar: 20 full `ctest -j` runs, 0 failures; `ctest -R sudoku_1 --repeat until-fail:200 -j4` (the 7 Sudoku tests of both suites): the first run failed once (`Games466/GamesCheck466Test.EveryRoundPlaysAsItsFileSays/sudoku_1`, "stack overflow (string slice too long)", round `notes-digits`), the next three runs passed 7 of 7 each; 400 separate processes of the Sudoku rounds, 0 failures. See the deferred item for the cause and the rate; the bar is not met in one try, and the plan does not call the failure a flake: it is a heap-margin fault, measured.
- Simulator, under the build lock with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `sim.sh build x4pro` SUCCESS and `sim.sh build sticky` SUCCESS. The first shim (`-I`) was not in effect: the built binary had no `[SIM]` string and the compile line puts the library's `-I` 68th and the project's 71st; the shim is now force-included (`-include`), and the rebuilt `simulator_x4pro` logs `[SIM] X4 Pro bezel insets {9, 7, 3, 7}: game canvas 466 x 788 at (7, 9)` at startup (`build/sim/sim.log`; the Sticky run prints no such line). Measured in the Sudoku board shot (`sudoku-board-pad-rail.png`, 480 x 800): the grid's border runs from screen x 14 to 466, which is the box's x 8 less the 1 px border plus the origin 7; on the Sticky the same screens are pixel-identical for Ultimate tic-tac-toe in play and its HOW TO PLAY, Sudoku's menu and HOW TO PLAY, and Battleship's placement (0 differing pixels each, `compare -metric AE`); Battleship's firing shot differs by 1,076 pixels, all in the pass-to-next-player dialog (the runtime's own screen, not the game's, which is centred on the whole canvas: 4 px wider offset), with the boards and the column identical.
- Not run: `pio run -e x4pro`, `-e default`, `pio check`: the only `src/` change is the comment in `src/games/GameViewport.h` (the simulator builds compiled it).

**Screenshots (X4 Pro simulator at 466 x 788, `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-466-canvas-screenshots/`; each looked at for clipping and overlap, none found):**
- `sudoku-board-pad-rail.png` -- Sudoku's board, pad with SHOW REMAINING counts, and rail.
- `sudoku-notes-shade-peers.png` -- digit notes in a SHADE PEERS row (the selected cell framed), the focused digit 9 in white on black.
- `sudoku-menu.png` -- the MENU panel.
- `sudoku-how-to-play.png` -- Sudoku's HOW TO PLAY page.
- `ultimate-tic-tac-toe-play.png` -- a round in play, the highlighted board, the question button.
- `ultimate-tic-tac-toe-how-to-play.png` -- its HOW TO PLAY page.
- `battleship-placement.png` -- placement, two ships placed, the tray and four buttons.
- `battleship-firing.png` -- firing after a hit (the target board, the own fleet, the column; the hand-off dialog is the runtime's).
- `battleship-over.png` -- seat 0 at Over, both fleets, the Game over dialog, the shot counts.

**Recorded by the implementation (before the review patches; host tests and fast checks only):** scratch mutants, restored after each run, each turned the games check red: UTTT x +1 at 474 only and y +1 at 466 only (board layout, small-canvas and same-layout checks); Battleship cell 43 at 466 only (layout, same-layout, and `draw-commands` round) and a header x +1 at 474 only (`draw-commands`); Sudoku `rail_x` +1 at 466 only and the MENU title y +1 at 474 only (`rounds/canvases.lua`). The explicit-size pins run under both `Games/` and `Games466/`, so a one-canvas break is red in both suites. `ApiLevelTest` and `ApiSurfaceTest` pass unchanged: the comment edit does not move `API_SURFACE_CRC`.

**Follow-up pass results (the final tree: the first commit `3fe30c4f` plus this pass's commit):**
- Flake bar (the brief's): 20 full `ctest --test-dir build/test -j4` runs, each 1,775 of 1,775 passed, 0 failures; then `ctest -R sudoku --repeat until-fail:200 -j4` over the 7 Sudoku tests of both suites (`Games/` and `Games466/`, three tests each, and the companion test): 1,400 test runs, 0 failures (exit 0, 202 s). Both runs on the committed tree's files, after the review patches and the formatter.
- Margin, measured by lowering the Lua cap in a scratch build (uncommitted instrumentation in `ArenaAllocator.cpp`, reverted): the rounds VM, old tree: worst round passes at 259,500 B and fails at 259,000 B of 262,144 B; new tree: passes at 238,000 B and fails at 236,000 B (60 fixed arena addresses all passed at 238,000 B). The checks VM: passes at 258,600 B, fails at 258,300 B on each of 40 fixed addresses, unchanged. UTTT passes down to 250,000 B, Battleship down to 240,000 B. Method and cause: Design Notes.
- Host suites with the patches: `Games/` and `Games466/` for all three games, `TheTimingFixturesBandsSitAtTheLimitsOnEveryCanvas` now at 466 x 788 too, all pass; every `scripts/*_test.py` (13) passes, `python3 scripts/check_upstream_touches.py` PASS, `./bin/clang-format-fix` twice, nothing changed.
- `sim.sh build x4pro` SUCCESS under the build lock (`PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`); no `src/` or simulator file changed in this pass, so no `pio run`, `pio check`, or Sticky rebuild.
- Sticky screenshots (the Sticky's layout changed, 474 x 788 with 4 px of white each side; taken in the first pass on `simulator_sticky`, the games unchanged since, each looked at again), in the same folder `story-466-canvas-screenshots/`:
  - `sticky-uttt-play.png` -- Ultimate tic-tac-toe in play, the grid at 50 px cells, the margins white.
  - `sticky-uttt-help.png` -- its HOW TO PLAY page.
  - `sticky-sudoku-menu.png` -- Sudoku's MENU panel.
  - `sticky-sudoku-help.png` -- Sudoku's HOW TO PLAY page.
  - `sticky-battleship-placement.png` -- Battleship's placement.
  - `sticky-battleship-firing.png` -- Battleship firing after a hit, the hand-off dialog over the own fleet.

## Auto Run Result

**Summary.** The games check plays every round and check of all three games at 474 x 788 and at 466 x 788 (a second canvas in the check's own files, naming no game); each game lays out one fixed 466 x 788 box centred in `ch.screen` (the Sticky's 474 gets 4 px of white at each side), logs one line and lays out from the corner on a canvas under the box, and a companion check per game pins that the 474 layout, tap targets, note tiles, and frame are the 466 ones shifted 4 px; the simulator's X4 Pro runs the device's insets (466 x 788 at (7, 9)) through a force-included header; the two 474-for-the-X4-Pro comments, and the other comments that said so, now say 466 for the X4 Pro and 474 for the Sticky; `docs/crosshatch/game-canvas.md` has the paragraph for game authors.

**Files.**
- `games/{battleship,sudoku,ultimate-tic-tac-toe}/` -- the fixed box: `layout.lua`, `board.lua` (the two copies stay identical), `main.lua`, `view.lua`, `help.lua`.
- `test/game_script/harness/games_check/` -- `CanvasSize`, `Roots::canvas`, the `Games466` suite, engine tests, `CanvasSizesTest`; the module-loading probe takes the canvas.
- `test/game_script/first_party/` -- each game's checks, rounds, taps, `rules.lua`, a new `sudoku/canvases.lua` and `rounds/canvases.lua`, and `README.md`.
- `.claude/skills/run-crosshatch-player/{shim/BoardConfig.h,simulator.ini,SKILL.md}` -- the simulator's X4 Pro insets.
- `docs/crosshatch/{api-level-1.txt,game-canvas.md}`, `src/games/GameViewport.h`, stale comments in `test/game_script/` -- 466 for the X4 Pro, 474 for the Sticky.
- `_bmad-output/.../story-466-canvas-screenshots/` -- the nine shots listed in Verification.

**Review.** Four lenses ran as context-free subagents. 32 findings: high 2 (one root cause, the shim), medium 10, low 13, false 7. Patches applied: 15 rows (the shim force-include and proof, the module-loading probe at the canvas, the stale comments, two comment cross-references, the api-level note's 2.9 versus 2.8, the digest folding every byte, the UTTT comment and wrapped lines, one literal, the `game-canvas.md` wording). Deferred: 5 rows in 3 items (the typed inset values, the Sudoku rounds heap margin, nothing in CI proving the shim). Rejected with reasons in the triage log: 12 rows. Follow-up review recommended: true, by the first-pass rule (a `high` was patched: the shim). The unverified risks a second pass should look at: the force-included `shim/BoardConfig.h` is compiled into every translation unit of `simulator_x4pro` (one build, and the log line and shots show it works, but nothing in CI checks it), and the Sudoku rounds heap margin after the review patches (the `until-fail:200` bar failed once in four runs). Patched entries by verdict: high 1, medium 4, low 6 (grouped rows).

**Verification.** As in Verification above: host suite 1,775 of 1,775; the games check at both canvases; 13 script tests, the ledger check, and the formatter twice; three scratch mutants red on the final tree (and six before the patches); the shim proved by the startup log line and a measured grid edge; the Sticky shots identical inside the box; 20 full runs clean, and the `until-fail:200` bar failed once in four runs (see the deferred heap item).

**Residual risks.**
- The Sudoku rounds VM's heap margin: about 1 failure in 1,600 plays of the rounds in the games check, which can fail a PR's check; rerun the job once, and split `rules.lua` in a follow-up.
- The inset values are typed in three places (SDK, harness, shim).
- No device run: the games were seen on the simulator's bitmap font and the harness's stand-in metrics, not the device's panel.
- A canvas smaller than the box is laid out from its corner and clipped (no device gives one).
- Formatting: `./bin/clang-format-fix` changed nothing outside this entry's paths (it reflowed a comment in the new shim header only).

### Follow-up pass (2026-10-05)

**Summary.** The Sudoku rounds' memory fault is fixed by cutting the rounds' heap: the cause (the arena's ASLR base reorders a load's first allocations through Lua's pointer-keyed `nil` constant) is measured, the old margin of under 3 KB is about 24 KB, and the brief's bar is met on the final tree (20 full `ctest` runs, 1,775 of 1,775 each, and 1,400 Sudoku test runs, no failure). `rules.lua` is split into `pins`, `rules`, `interaction`, `drawn`, and `marks`, so a round loads only the pins it calls.

**Files.**
- `test/game_script/first_party/sudoku/{pins,interaction,drawn,marks}.lua` (new), `rules.lua` (the rules only), `rounds/{notes-digits,notes-dots,solve-expert,solve-hard,toggles}.lua` (require the module they call), comment updates in `canvases.lua`, `rounds/canvases.lua`, `checks.lua`, `battleship/draws.lua`, and `README.md` ("The checks VM heap").
- Review patches: `test/game_script/GfxBindingsTest.cpp` (a 466 x 788 size for the timing fixture), `ultimate-tic-tac-toe/checks.lua` (every command on the canvas; the help-tap loop at 466 and 474; a 44 px assertion), `sudoku/drawn.lua` (the same on-canvas assertion in `watch`), `battleship/checks.lua` (dialog and banner bounds in canvas pixels on 788-tall canvases).
- `_bmad-output/.../story-466-canvas-screenshots/sticky-*.png` (six Sticky shots).

**Review.** Four lenses ran again. 28 rows: medium 8, low 17, false 3; routes: patch 10, defer 4 (carried, in `deferred`), reject 14. Patched entries by verdict this pass: medium 4, low 6 (no `high`). Rejected rows carry their reasons in the triage log. Deferred: the inset values typed in several places, the simulator shim's missing automated check, and a new item for Sudoku's checks VM (about 3.5 KB margin, stable).

**Follow-up review recommended: false.** No `high` was patched this pass. The unverified risks: the 1-in-1,600 fault could not be reproduced on demand, so the fix is shown by margin and by the flake bar, not by watching the old fault disappear; and Sudoku's checks VM is still at about 3.5 KB.

**Residual risks.**
- Sudoku's checks VM: about 3.5 KB below the cap; a future addition to `checks.lua`, the solver, the counter, or the bank can turn the required `games-check` red (deferred, medium).
- No device run, no Sticky device run; shots are the simulator's.
- Formatting: `./bin/clang-format-fix` changed nothing in this pass.

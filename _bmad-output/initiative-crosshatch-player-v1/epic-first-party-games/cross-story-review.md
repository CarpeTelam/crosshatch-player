# Cross-story review: epic-first-party-games

The orchestrator ran this review on 2026-10-05, after entry 11 merged. The reviewed tree was `daedbeab`. `orchestrated-epics.md` asks for it under "Before the epic PR".

- **Diff.** `git diff eca7e6c7..daedbeab`, excluding:
  - `_bmad-output/**`
  - `*.png`
  - `games/sudoku/puzzles.lua`, the generated bank
  - `*.generated.h`
  - the generated I18n files

  What remained was 54.9 KB of diff text after those exclusions. It covers the engine, the harness, the three games, their companion folders, scripts and docs.
- **Reviewers.** Three context-free subagents ran `bmad-review`'s lenses: adversarial (A), edge-case-hunter (EC) and verification-gap (VG). Each was told to weight the boundaries between the story merges.
  - A and EC built a scratch copy and fuzzed the rules.
    - A ran 20,000 random UTTT games, 3,000 Battleship games and 400 Sudoku sequences. All held.
    - EC played every round at canvases 468–480 wide. All passed.
  - VG ran mutants against `ctest -L games-check` (97 tests).
  - The tracked tree was not touched.
- **Raw reports.** These are in the orchestrator's scratchpad (`xr/adversarial.md`, `xr/edge-case.md`, `xr/verification-gap.md`). The rows below carry their substance.
- **Stories that landed after the review.**
  - Entry 12 (`54c2a4ea`) changes only `scripts/fork_release.py` (`pack_one` passes absolute paths) and its test. It meets no other story's code: its boundary is the release workflow, which it does not edit. Its own four-lens review is its combined-diff pass.
  - Entry 5's packet changes only `_bmad-output/`.
- **Verdicts.**
  - fix: the build agent fixes it in this PR.
  - owner: it changes a game rule or a cross-version promise, so it goes to the owner.
  - defer: it goes to `deferred-work.md` with a reason.
  - reject: the reason is given.

## Triage

| # | Lens | Story or boundary | Finding | Verdict | Reason / action |
|---|---|---|---|---|---|
| 1 | A M1, VG-1 | 8.3 × 8.4 × 8.11 | Sudoku's end screen draws the difficulty name at canvas y 260. That is under the host's end-of-round dialog, whose top edge is at 259, so the name never shows. `story-sudoku-screenshots/solved.png` shows "Solved", "Time", "Best", then the dialog. Battleship pinned its boards against 259, but UTTT and Sudoku pin nothing. | fix | Move the name above the dialog, for example into the "Time" line or at y ≤ 230. Pin every game's end frame against one shared host-dialog bound in the harness, so no game's text sits under the dialog. Battleship's pin then reads that same bound. |
| 2 | A M2, EC7 | 8.11 × harness | Four places type the X4 Pro and Sticky insets (`{9, 7, 3, 7}`, `{9, 3, 3, 3}`) and the even-origin rule, and none reads them from the SDK: `CanvasSizesTest`, `GamesCheckRig.h`'s `CANVAS_466/474`, the simulator shim and `layout.lua`'s tile phase comment. `CanvasSizesTest` pins a literal against itself. | fix | Add a host test that includes `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h` and checks three things: `XTEINK_X4_PRO.viewableInsets` and the struct default; the 466 and 474 widths derived from them; and `(left + top) % 2 == 0` for both. Derive `CANVAS_466/474` from it, or `static_assert` that they agree. If the header cannot compile host-side, record why in the plan and pin the copies against each other in that one test. The shim keeps its literal and names the test. |
| 3 | A M3 | 8.11 × engine tests | The host renderer double's canvas is 474 × 788 at (3, 6), but comments in `GfxRenderer.h`, `GameMatchTest.cpp`, `FrameReplayTest.cpp` and `GameVmTest.cpp` call it the Sticky's. The Sticky's real origin is (3, 9), and `GameTouchTest` already uses (3, 9). | fix | Comments only: call it the double's own canvas, at an odd origin parity no device has. Do not move its geometry; those pixel expectations are not this epic's. |
| 4 | A M4, EC2, VG-5 | 8.8 × 8.3 × 8.9 × 8.11 | The check-only VMs share the device's 256 KB heap cap and 2 M instruction budget. Sudoku's checks VM faults at a cap only 3.8 KB lower. Its batches peak at 1.7–1.8 M of 2 M, the Battleship and Sudoku canvas rounds at 1.1–1.4 M, and the rounds VM has about 24 KB of margin. Small, benign growth crosses a cliff. A guard fault also ends that game's remaining checks. The README records the ASLR-dependent "not enough memory" flake this produced once. | fix | Give the VMs that run only check code, the round-file and `steps(state)` VM and the `checks.lua` VM, a host heap cap and instruction budget well above the device's, as named harness constants. The played `LuaGame` and the package-load probe keep the device's cap and budget. If any check measures the game's own cost through that VM's guard, keep that measurement on a count hook and record which in the plan. After the change, update the README's "The checks VM heap" section, and remove or simplify the split/batching workarounds only where the plan shows they are no longer needed. |
| 5 | A M5 | 8.8 × 8.3 | Nothing measures the played game's heap headroom. Sudoku's played VM reaches about 180–200 KB of 256 KB at its first HINT, and no round or check reports the high-water mark. | fix | `RoundPlayer` reads the played VM's arena peak after each round and fails a round whose margin under the device cap falls below 16 KB. Add one long Expert Sudoku round that mixes HINT, CHECK, FILL NOTES, digit notes and many writes. |
| 6 | A M6, EC1 | 8.7 × 8.8 × each game | The games check never restores a game from its own snapshot. On Continue, the device builds a new VM, calls `Session::restore` and starts each seat with an empty `ui`. No round draws or takes input in that state. EC confirmed in a scratch copy that every round passes with a restore before every step. | fix | Add a `RoundPlayer` mode that restores the snapshot into a fresh `LuaGame` before each step, as on Continue, then draws every local seat. Run every first-party round in it at both canvases. A fault or Lua error fails the round. |
| 7 | VG-1 | 8.3 × 8.9 × 8.11 | Sudoku's dot notes, the default NOTES AS mode, are pinned by nothing. Mutants all stay green: no dots drawn, a white focused dot, light rings, no white disc on a shaded peer. Also unpinned: the pad focus frame, the NOTES rail inversion, the MENU icons, and the end screen's level line (row 1). | fix | Pin the dot commands in `drawn.lua`'s frame check: per shown mark, the circle's radius and colour on each ground, including the white disc under a ring on a dark ground. Also pin the pad focus frame, the rail button's inverted fill and the MENU icon names. |
| 8 | VG-2 | 8.9 | The 27 note PNGs and `NOTE_W`/`NOTE_H` are unobserved. `make_note_images.py --check` catches a swapped PNG but runs in no job. | fix | Register `make_note_images.py --check` as a ctest under the `games-check` label in `games_check.cmake`, so the existing CI job runs it with no workflow edit. Add a games-check test that the installed images' sizes equal `NOTE_W`/`NOTE_H`. |
| 9 | VG-3, A L8 | 8.8 × 8.11 | The 466 suite does not pass the canvas to the round-file VM (`GameCheck.cpp:460`) or to the package-load probe (`:275`). `OwnedVm::create`, `GamesCheckRig::create`, `GameCheck` and `RoundPlayer` all default to `CANVAS_474`, so dropping the argument is silent. | fix | Make `canvas` a required parameter everywhere it defaults. Extend `CANVAS_GAME`'s test so a round's `steps(state)` and a module's load each log `ch.screen`, asserted at both sizes. |
| 10 | VG-4 | 8.1 × 8.2 × 8.3 | The draw recorders prove which commands were drawn but not their ink. Mutants stay green when a game clears its page black or draws its grid lines white. | fix | In each game's existing frame pin, check the cheap invariants. The first command is `clear("white")`. Grid and outline lines are black. Text is black except where the game inverts it on purpose; name those cases. |
| 11 | EC4 | 8.8 × 8.11 | `RoundPlayer` hands `input()` taps that the device would drop: off the canvas, negative, or in 466..473, which exists only on the Sticky. No current round does this. | fix | Fail a step whose tap lies outside the canvas under check, naming the canvas. |
| 12 | A L2, EC6 | 8.8 | A first-party game with no `checks.lua` passes, with only a note on stderr. A mis-named round (`rounds/x.LUA`, `rounds/sub/x.lua`, `rounds/x.lua.txt`) is silently skipped next to valid ones. | fix | Fail a first-party game without `checks.lua`. Fail any entry in `rounds/` that is not a regular `.lua` file. |
| 13 | A L3 | 8.8 × 8.7 | For a hidden game, `shows` can be met by one of the check's own every-local-seat draws, not by the frame the device would show next. | fix | For hidden rounds, take `shows` only from the frames the flow itself drew. Battleship's placement rounds must still pass, or their expectations are corrected with the reason. |
| 14 | A L4 | 8.1 × 8.2 × 8.3 | The recorders claim to count each call as one command, as the engine does. That is false for `refresh`, which the engine does not count. | fix | Count `refresh` and `clear` as `ChBindings.cpp` counts them, in every recorder, and fix the claim in the README and `trace.lua` headers. Merging the four recorders into one is deferred (row 24). |
| 15 | A L7 | 8.2 | Battleship's Result-banner bound, "about y 649", was not converted from panel to canvas pixels as the dialog bound was. The banner starts at about canvas y 641, so about 8 px of overlap would pass. | fix | Convert it to canvas pixels, with the measurement and method in a comment. Take it from the same shared host bounds as row 1. |
| 16 | A L1 | 8.10 | `ZipDirectoryTest` still uses 40 as an EOCD count "above the limit" and `32, 33` as the boundary list. 40 is now under the 64-member cap. | fix | Use `PACKAGE_MEMBERS + 1`, and 64 and 65 in the list. |
| 17 | A L5 | 8.11 | `first_party/README.md` says "three tests per game". There are now six (both canvas suites) plus one per companion. | fix | Doc only. |
| 18 | A L6 | 8.11 | `docs/crosshatch/game-canvas.md` says the Paper Mono gives games 466 × 788, but no Paper Mono env sets `FREEINK_CAP_GAMES`. | fix | Doc only: say the insets are the X4 Pro's and that the Paper Mono does not run games today. |
| 19 | A L10 | 8.7 × 8.2 | `fixtures/pass-keep/main.lua` and `fixtures/README.md` call `pass-keep` the engine's only kept-turn game. Battleship placement and `TestSupport.h`'s `HIDDEN_KEEPS_TURN_GAME` are kept-turn too. | fix | Comment and README only. |
| 20 | EC5 | 8.3 | Sudoku's clock counts the time the pause menu is open, because the VM and `ui.last` survive Pause and Resume. Sleep followed by Continue drops the same idle gap. A pause of about 100 minutes pins the clock at 99:59, and a pause can spoil a best time. | owner | Which idle time counts toward a solve is a game rule, so the owner decides. Recommendation: cap the time one gap between two inputs can add, for example 2 minutes. Any tap, a cell selection included, ends a gap. |
| 21 | EC3 | 8.10 | A package with 33–64 members, such as Sudoku's 36, fails on firmware from before entry 10 with a generic "too many members". API level 1 did not change, so the manifest cannot say it needs the larger cap. | owner | Raising the cap within level 1 was the owner's decision (Notes, entry 10), and level 1 is not frozen (`API_LEVEL_FROZEN` false). Recommendation: no change. No released fork firmware has installed a first-party game, and entry 6's release ships the new cap with the games. The owner confirms at sign-off. |
| 22 | VG-6 | 8.11 | No job runs the simulator's X4 Pro shim. A no-op shim builds green, and the 466 screenshots would then show 474. | defer | Low; no product code is affected. A headless smoke test that greps `[SIM] X4 Pro bezel insets` in the `Simulator build` job is a CI gate change with its own fresh-clone rule. Carried to `deferred-work.md`. |
| 23 | A L9 | 8.3 | The in-game credit names "sudokuexchange.com", while `make_bank.py` names the GitHub repository. | reject | Both are right. The pinned repository's README describes it as the puzzle bank of the Sudoku Exchange site (sudokuexchange.com), and its `LICENSE.txt` dedicates the data to the public domain. `puzzles.lua` names the repository and the commit. |
| 24 | A L4 | 8.1 × 8.2 × 8.3 | The four recorders are near-copies; the UTTT and Battleship `trace.lua` files are byte-identical, so they will drift. | defer | Merging them is a refactor across the companion folders, beyond a fix story. Row 14 fixes the false claim. Carried to `deferred-work.md`. |

## Result

- **Fix story:** rows 1–19 are accepted. Rows 20 and 21 go to the owner, rows 22 and 24 are deferred, and row 23 is rejected.
- **Who fixes them:** a build agent fixes rows 1–19 as `e8-xr` (plan `_bmad-output/implementation-artifacts/plan-e8-xr-cross-story-fixes.md`), on the epic head after entry 12.
- **What the fix may touch:**
  - the harness and companion folders under `test/game_script/`;
  - `games/sudoku` (row 1, and any pin row 7 or 10 shows a game must change for);
  - `games/battleship` and `games/ultimate-tic-tac-toe` only where a row's pin shows a real defect;
  - the README and docs named above.
  - It touches no `src/`, `lib/` or upstream file, apart from host test code that only reads the SDK header.
- **Review and flash:**
  - The fix commit gets the same three context-free lenses before the push, recorded below.
  - Firmware is unchanged unless row 1's game edit counts: a game file is packaged, not linked, so it needs no new flash measurement.
  - If the fix changes `games/sudoku`, the device-run packet's `sudoku.chgame` and its hash are rebuilt from the new head, and the owner gets the new file.

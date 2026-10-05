---
title: 'e8-xr: fix the epic-first-party-games cross-story review, rows 1 to 19'
type: 'bugfix'
ticket: ''
created: '2026-10-05'
status: 'built'
baseline_revision: 'ddd88cb777b8b3cc3e1a30e65ad95ae12e1f2c5a'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - AGENTS.md
  - _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/cross-story-review.md
  - test/game_script/first_party/README.md
  - docs/crosshatch/game-canvas.md
warnings: ['oversized']
deferred:
  - summary: >-
      The host end-of-round dialog and Result banner bounds (HostBounds.h: 259, 528, 640) are one hand measurement of three screenshots and no test ties them to the host's layout.
    evidence: |-
      A change to the dialog in src/activities/games/GameMatchView.cpp or GameMatchActivity.cpp moves the real edge while every consumer (RoundPlayer endFrameClear, Battleship, Sudoku and UTTT end-frame pins) keeps pinning the old numbers. Follow the GameViewIconsTest pattern (fui::optionDialog on a frame stub) to measure the real dialog.
    location: >-
      test/game_script/harness/games_check/HostBounds.h
    severity: medium
  - summary: >-
      The restore probe is pinned to GameVM's Continue only for a hidden pass roster.
    evidence: |-
      GamesCheckFlowTest TheRestoreProbeStartsAGameFromASnapshotAsGameVMsResumeDoes resumes a Roster::pass(2) hidden game; a solo or open-pass variant would pin rounds.start against begin.
    location: >-
      test/game_script/harness/games_check/GamesCheckFlowTest.cpp
    severity: low
  - summary: >-
      In an open pass round, `shows` can still be met by the check's every-seat sweep frame instead of the frame the device shows next.
    evidence: |-
      Row 13 scoped the flow-only rule to hidden rounds; the README now says so. In an open pass round the flow frame is the next turn seat's while step.seat's first frame is the sweep's.
    location: >-
      test/game_script/harness/games_check/RoundPlayer.cpp (shows)
    severity: low
  - summary: >-
      Text-box heights are re-derived by hand in three companion folders (layout.DY, UTTT TEXT_BOX, Battleship) and the harness end-frame pin tests only a text's top y.
    evidence: |-
      A shared host.text_box_h(size) table would let the harness pin boxes; the device's font metrics are what a real fix needs.
    location: >-
      test/game_script/first_party/*/checks.lua, test/game_script/harness/games_check/RoundPlayer.cpp
    severity: low
  - summary: >-
      No check bounds the heap of the HINT, CHECK, FILL NOTES and symmetry calls on each band's costliest puzzle in the played game.
    evidence: |-
      Those calls moved from a device-sized checks VM to a larger one; the played-game margin gate covers long-expert's first HINT only. Unverified: a per-band played round on puzzles.costly[band] would show it (low confidence).
    location: >-
      test/game_script/first_party/sudoku/checks.lua
    severity: low (unverified)
---

<intent-contract>

## Intent

**Problem:** The orchestrator's cross-story review of epic-first-party-games (`_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/cross-story-review.md`) found, in rows 1 to 19 (verdict "fix"), a Sudoku end screen whose difficulty name sits under the host's end-of-round dialog, a games check that cannot tell several real regressions from a pass (restore from a snapshot, played-VM heap margin, canvas not passed to two VMs, ink of draw commands, dot notes, off-canvas taps, missing `checks.lua`, mis-named rounds, hidden-round `shows`), check-only VMs that share the device's heap cap and instruction budget, and stale comments and docs.

**Approach:** Fix each row as its "Reason / action" cell says, in the harness and companion folders under `test/game_script/`, `games/sudoku` (row 1 only), and the named docs and comments, with one host-bounds constants header shared by the harness and exposed to the check VMs, and a pin for each row that a mutant of the regression turns red. Rows 20 to 24 are out of scope.

## Boundaries & Constraints

**Always:**
- Work only in the worktree `/home/user/epic-first-party-games-lane-c` (branch `epic-first-party-games-lane-c`); every path below is relative to it. Never touch `/home/user/crosshatch-player`.
- Touch only: `test/game_script/**` (harness, `first_party/`, `fixtures/` comments), `games/sudoku` (row 1, and only a pin that shows a game must change), `games/battleship` and `games/ultimate-tic-tac-toe` only where a new pin shows a real defect (record it), `docs/crosshatch/game-canvas.md`, `.claude/skills/run-crosshatch-player/shim/BoardConfig.h` (a comment only), and this plan. No `src/`, `lib/`, `freeink-sdk/`, `.skills/`, `ci.yml` or `crosshatch-ci.yml` edit; no `.github/` edit at all.
- Wrap every `cmake` configure/build and `ctest` of host tests in `flock /tmp/crosshatch-hosttest.lock sh -c '...'`, every `pio`/`sim.sh` command in `flock /tmp/crosshatch-build.lock sh -c '...'`, with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`. Host tests: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release` (already configured and built once in this worktree) then `cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest` and `ctest --test-dir build/test -L games-check --output-on-failure -j4`; before finishing, the whole host suite (`cmake --build build/test`, `ctest --test-dir build/test --output-on-failure -j`).
- AGENTS.md conventions hold in harness C++: `makeUniqueNoThrow`, locals under 256 bytes, `-fno-exceptions`-safe, no `Serial`. Never `git clean -fdX`. Format with `./bin/clang-format-fix` (no arguments) as the last step, twice, and `git add` any new file first.
- The played `LuaGame` (Player::prepare in `RoundPlayer.cpp`) and the package-load probe (`checkPackage` in `GameCheck.cpp`) keep the device's heap cap (`GameScript::LUA_HEAP_BYTES`) and instruction budget (`CallGuard::INSTRUCTION_BUDGET`).
- A test double this change adds or extends names, in a comment and in the plan, the device behaviour it stands in for, with a test that pins the two agree, and says where it is more permissive than the device.
- Mutant evidence: every new pin is shown red by a scratch mutant of the regression it guards (listed in Verification), each reverted, with `git status` clean of mutant edits afterwards.

**Never:** Rows 20 to 24 (Sudoku's clock, the member-cap level, the simulator shim smoke job, the provenance string, merging the four recorders). Do not move the host renderer double's geometry (row 3 is comments only). Do not edit `lib/`'s `CallGuard`/`ArenaAllocator` to raise a limit: the check VMs' larger budget lives in `ScriptVm`. Do not add a CI job or edit a workflow. Do not call a failing test a flake.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Played-VM heap margin | A round whose played game needs more than the device cap less 16 KiB | The round fails naming the margin and the cap tried | Failure text names the second play's fault |
| Restore probe | A game that faults or raises in `load`, `restore`, `start` or `draw` of a VM restored from the snapshot (ui empty) | The round fails at that step, naming it | Fault or Lua error text in the failure |
| Off-canvas tap | A step with x or y negative, x at or over the canvas width (466..473 on the 466 canvas), or y at or over the height | The step fails naming the tap and the canvas | The device drops such a tap, so the check refuses it |
| No `checks.lua` | A first-party game with no `<id>/checks.lua` | `runGameChecks` fails | Message names the file |
| Mis-named round | `rounds/x.LUA`, `rounds/sub/` (a directory), `rounds/x.lua.txt` beside valid rounds | `playRounds` fails naming the entry | The valid rounds still play |
| Hidden `shows` | A hidden pass step whose `shows` text is drawn only by the check's every-seat draw | The step fails | Only frames the flow itself drew count |
| Canvas not passed | A round file or load that reads `ch.screen` | It sees the canvas under check, at 474 and at 466 | A test logs `ch.screen` from `steps(state)` and from a module load |
| End frame under the dialog | A frame drawn while the round is over has a text starting inside the host dialog's band | The round fails naming the text and the band | Applies on 788-tall canvases |
| Check-VM limits | A check or `steps(state)` that spends 3 M instructions or 600 KB of Lua heap | Passes in a check VM; faults in the played game and the probe | The played game keeps the device limits |

</intent-contract>

## Code Map

All under the worktree. Baseline (this worktree at ddd88cb7, measured 2026-10-05): `ctest -L games-check` is 97 tests, all green, 10 proc-seconds, 3 s wall at `-j4`. A temporary edit that lowered the played VM's cap by 16, 32, 48 and 64 KiB left every `GamesCheckTest.*EveryRound*` green (6 tests), so a 16 KiB margin gate is not borderline today (edit reverted).

- `test/game_script/harness/games_check/GamesCheckRig.h` -- `CanvasSize`, `CANVAS_474/466` (typed insets, row 2), `GamesCheckRig::create(seed, canvas = CANVAS_474)` (row 9), arena split at `LUA_REGION_BYTES`; the check-VM region and cap go here.
- `.../ScriptVm.h`, `ScriptVm.cpp` -- the check VM: `ScriptVm`, `OwnedVm::create(..., canvas = CANVAS_474)` (row 9), `protect()` arms `CallGuard` then `lua_pcall`; `probeLoadNesting`. `CallGuard::INSTRUCTION_BUDGET` (2,000,000) is a lib constant: a larger budget needs a wrapper count hook in `ScriptVm` (see Design Notes).
- `.../GameCheck.h`, `GameCheck.cpp` -- `Roots{..., canvas = CANVAS_474}` (row 9), `checkPackage` (probe VM at :275, device limits), `runGameChecks` (checks VM; `hasChecks` false is a note today, row 12), `playRounds` (listing at ~:413 filters `.lua` silently, row 12; round-file VM at :460; clash VM at :447), `readModules`.
- `.../RoundPlayer.h`, `RoundPlayer.cpp` -- `GameUnderCheck.canvas = CANVAS_474` (row 9), `PlayOptions`, `Player` (prepare, begin, step, stepHidden, shows, finish), the tap built at `step()` (row 11), `shows()` over `report.frames` (row 13), `keepFrame` (text commands only), `resolveSteps` call in `begin()` (a second play must not resolve twice).
- `.../GamesCheckTest.cpp`, `GamesCheckEngineTest.cpp`, `GamesCheckFlowTest.cpp` (`CanvasSizesTest` at :222, row 2), `ScriptVmTest.cpp`, `RoundFileTest.cpp`, `TestSupport.h` (`HIDDEN_KEEPS_TURN_GAME`, row 19) -- tests; `GamesCheckEngineTest.cpp:1047` is `CANVAS_GAME`'s canvas test (row 9).
- `test/game_script/harness/games_check.cmake` -- targets and labels (`games-check`); CI builds only `GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest` and runs `ctest -L games-check`, so every new test lives in those three executables or is a plain `add_test` (no workflow edit).
- `test/game_script/first_party/{ultimate-tic-tac-toe,battleship,sudoku}/` -- `checks.lua`, `trace.lua` (UTTT, Battleship; byte-identical), `drawn.lua` (Sudoku's recorder `record`, `watch`, `expect_marks`, `frame`, `look`), `marks.lua`, `canvases.lua`, `draws.lua`, `taps.lua`, `rounds/*.lua`, `tools/make_note_images.py` (`--check` exits 0 today, standard library only).
- `games/sudoku/view.lua` -- `draw_end` (:68-77, the name at `oy + 260`), `draw_mark` (dots), pad focus `frame` (:180), rail inversion (:187), `MENU_ICONS` (:15); `games/sudoku/layout.lua` (`NOTE_W/H`, `DY`), `games/battleship/layout.lua` (comments at :56 and :72 say 649 and 259).
- Stale text: `test/game_script/harness/stubs/GfxRenderer.h:55-57,239-242`, `GameMatchTest.cpp:44-46`, `FrameReplayTest.cpp:44-47`, `GameVmTest.cpp` (the "Sticky's (3, 6)" comments, row 3); `ZipDirectoryTest.cpp:127,150-155` (row 16); `first_party/README.md` (:98 "three tests per game", the draw-recorder and "checks VM heap" sections, the `shows` paragraph); `docs/crosshatch/game-canvas.md:27` (row 18); `test/game_script/fixtures/pass-keep/main.lua:1` and `fixtures/README.md` (row 19); `.claude/skills/run-crosshatch-player/shim/BoardConfig.h` (comment names the test, row 2); `games/sudoku/layout.lua:105` tile-phase comment.
- Read-only: the SDK header `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h` (`ViewableInsets` default {9,3,3,3} at :708, `XTEINK_X4_PRO` {9,7,3,7} at ~:1697). It includes `Arduino.h`, `driver/gpio.h`, `esp_rom_sys.h` and uses `Serial`, `gpio_num_t`, `gpio_hold_dis`, `pinMode`, `digitalWrite`, `OUTPUT`, `HIGH`, `esp_rom_printf`: a scratch compile with a stub `Arduino.h` still needed `Serial`, `<initializer_list>`, the gpio names and `esp_rom_printf`.
- Do not change: the engine, `lib/`, `src/`, the host renderer double's geometry, rows 20-24.

## Tasks & Acceptance

**Execution:**
- [ ] `test/game_script/harness/games_check/HostBounds.h` (new) -- named constants shared by the harness and the check VMs: the host end-of-round dialog's top edge (259) and bottom edge (measure it: the dialog spans panel y 268 to about 536 in `story-sudoku-screenshots/solved.png` and `story-battleship-screenshots/over-menu.png`, canvas = panel less the 9 px top inset), Result's banner top (about 641 canvas, panel 650 in `result-hit.png`), the canvas height they hold for (788), and the check-VM limits (`CHECK_LUA_HEAP_BYTES`, `CHECK_LUA_REGION_BYTES`, `CHECK_INSTRUCTION_BUDGET`), each with its measurement and method in a comment -- one source for rows 1, 4, 15.
- [ ] `ScriptVm.h/.cpp`, `GamesCheckRig.h` -- a required `VmLimits` (device or check) for `GamesCheckRig::create`, `ScriptVm` and `OwnedVm::create`; a check VM gets a larger arena region and Lua cap and a wrapper count hook for the larger budget; registers a global `host` (`dialog_top`, `dialog_bottom`, `banner_top`, `canvas_h`, `image_size(name) -> w, h` over the installed `GameImages`) and `within_device_budget(f, ...)` (runs `f`, raises when it spent the device's 2,000,000 instructions or more, counted on the same hook) -- row 4 and the shared bounds of rows 1, 8, 15.
- [ ] `ScriptVm.h`, `GamesCheckRig.h`, `GameCheck.h`, `RoundPlayer.h` and every caller -- `canvas` becomes a required parameter wherever it defaulted -- row 9.
- [ ] `GameCheck.cpp` -- checks VM, round-file VM and clash VM take check limits; probe and played game take device limits; no `checks.lua` is a failure; any `rounds/` entry that is not a regular file ending exactly `.lua` is a failure naming it (the valid ones still play) -- rows 4, 12.
- [ ] `RoundPlayer.h/.cpp` -- (a) after a round plays clean at the device cap, play it again with the Lua cap lowered by `HEAP_MARGIN_BYTES` (16 KiB, a named constant); a fault there fails the round naming the margin (do not re-resolve `steps` on the second play) -- row 5; (b) `PlayOptions::restoreProbe`: before each step, and once after the last, restore the current snapshot (and ver) into a fresh `LuaGame` in a second rig, `start`, draw every local seat, discard it; a load, restore, start or draw fault or Lua error fails the round at that step -- row 6; (c) fail a step whose tap is off the canvas under check, naming the tap and the canvas -- row 11; (d) hidden rounds take `shows` only from frames the flow drew (`begin`'s and `stepHidden`'s), not `drawEveryLocalSeat`'s -- row 13; (e) once a round is over, the frame the device shows (seat 1 in solo, seat 0 in pass) must have no text command starting in `[dialog_top, dialog_bottom)` when the canvas is 788 tall -- row 1 -- each with its guard comment.
- [ ] `GamesCheckTest.cpp`, `games_check.cmake` -- two more tests per game and canvas (`EveryRoundRestoresFromItsSnapshot` at 474 and 466, running `playRounds` with the restore probe); `add_test` (label `games-check`, plain Python, no build target) for `first_party/sudoku/tools/make_note_images.py --check` when that file exists -- rows 6, 8.
- [ ] `GamesCheckEngineTest.cpp`, `ScriptVmTest.cpp`, `RoundFileTest.cpp`, `GamesCheckFlowTest.cpp` and any test the signature changes break -- the tests for every I/O row (scratch games: heap-hungry game, restore-faulting game, off-canvas tap, no `checks.lua`, mis-named rounds, hidden `shows` from a check-only draw, `CANVAS_GAME` logging `ch.screen` from `steps(state)` and from a module's load at both sizes, an over frame with text in the dialog band, check-VM limits against the played game's); the `ScriptVm` vs `LuaGame` equivalence tests run the `ScriptVm` with device limits, and the check limits get their own test; change the checks-without-`checks.lua` test from a note to a failure -- rows 4, 5, 6, 9, 11, 12, 13.
- [ ] A host test in `GamesCheckFlowTest.cpp` or `GamesCheckEngineTest.cpp` (the executable in which the real SDK header can be included without a `BoardConfig` clash; decide from `games_check.cmake`) that includes `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h` through a test-only stub dir for `Arduino.h`, `driver/gpio.h` and `esp_rom_sys.h`, and checks: `XTEINK_X4_PRO.viewableInsets` and the `ViewableInsets` default; 466 and 474 derived from them (`480 - left - right`, `800 - top - bottom`); `(left + top) % 2 == 0` for both; and `CANVAS_466/474` agree. If the header cannot compile host-side, record the compile errors in Implementation Notes and parse the two values from the header text in the test instead, or, last, pin the copies against each other; never leave the literal pinned to itself -- row 2.
- [ ] `test/game_script/first_party/ultimate-tic-tac-toe/{trace.lua,checks.lua}`, `battleship/{trace.lua,checks.lua,draws.lua}`, `sudoku/{drawn.lua,canvases.lua}` -- every recorder stops counting `refresh` (the engine's `gfxRefresh` appends no command) and counts `clear` as one; a pin with a refresh in the frame; headers and claims corrected; a pin of each game's frame invariants (first command `clear("white")`; grid and outline lines black; text black except named inversions) -- rows 10, 14.
- [ ] `test/game_script/first_party/battleship/checks.lua`, `games/battleship/layout.lua` comments -- the dialog and banner bounds read `host.dialog_top` and `host.banner_top` (banner as canvas 641, not panel 649; keep the `h == 788` guard, now `host.canvas_h`); the two comments say canvas pixels and cite `HostBounds.h` -- rows 1, 15.
- [ ] `games/sudoku/view.lua` (`draw_end`) and Sudoku's checks -- the difficulty name moves above the dialog (a small line between "Solved" and "Time", or folded into the Time line, so every end-screen text box ends at or above `host.dialog_top` and no two boxes overlap, box height 2 x `layout.DY[size]`); an end-frame pin of every text box and of the level line itself against `host.dialog_top`; UTTT gets the same end-frame text pin -- row 1.
- [ ] `test/game_script/first_party/sudoku/{drawn.lua,marks.lua,checks.lua}` and a new round or `steps` call if needed -- pin the dot commands per shown mark (circle radius, colour, per ground, the white disc under a ring on a `dark` or `black` ground, the focused solid black dot), the pad focus frame, the rail's inverted NOTES fill, the eight MENU icon names, and the end screen's level line; pin that the 27 installed images `note_[gbh][1-9]` are `NOTE_W x NOTE_H` through `host.image_size` -- rows 7, 8.
- [ ] `test/game_script/first_party/sudoku/rounds/` (new `long-expert.lua` or similar) -- one long Expert round mixing HINT, CHECK, FILL NOTES, digit notes and many writes, played at the device cap and checked by the margin gate -- row 5.
- [ ] `test/game_script/first_party/sudoku/checks.lua` -- the four `COSTLY` rows call through `within_device_budget`, so their cost stays measured against the device's 2,000,000; other batching and the module split stay unless a measurement in Implementation Notes shows one is dead -- row 4.
- [ ] `test/game_script/harness/ZipDirectoryTest.cpp` -- the over-limit EOCD count is `PACKAGE_MEMBERS + 1`, the boundary list holds 64 and 65 -- row 16.
- [ ] Comments and docs: the four "Sticky's (3, 6)" comments (the double's own canvas, 474 x 788 at (3, 6), an odd origin parity no device has; the Sticky's real origin is (3, 9)); `first_party/README.md` (test counts per game and companion, the recorder claim, the checks-VM-heap section for the new limits and the margin gate and restore probe, `shows` for hidden rounds, no `checks.lua` and mis-named rounds are failures, the note-images tool now runs in CI); `docs/crosshatch/game-canvas.md:27`; `fixtures/pass-keep/main.lua` and `fixtures/README.md`; `TestSupport.h` comment; the shim header; `layout.lua:105` -- rows 2, 3, 17, 18, 19.

**Acceptance Criteria:**
- Given the tree after this change, when `ctest -L games-check` runs, then every test passes at both canvases, including the new restore, margin and end-frame pins, and the games-check list names the note-images script.
- Given Sudoku's solved screen on an X4 Pro simulator, when screenshotted, then the level name is visible above the dialog and overlaps nothing.
- Given each mutant in Verification applied alone, when `ctest -L games-check` runs, then at least one test fails naming the regression.
- Given the played game and the package-load probe, when their caps are read, then they are the device's; given a check VM, then they are the named larger constants.
- Given `git status` after `./bin/clang-format-fix` twice, then nothing new, and `scripts/check_upstream_touches.py` passes.

## Implementation Notes

Built 2026-10-05 against `ddd88cb7`. Host tests and the fast checks only; no firmware build, no `pio check`, no simulator build ran (the plan's "after the review" steps are open: see the end).

**Measurements.**
- Dialog (`HostBounds.h`): on `story-sudoku-screenshots/solved.png` and `story-battleship-screenshots/over-menu.png` (480 x 800), scanning rows for a dark run of 360 px (x 60 to 420) finds the 2 px border at panel rows 268-269 (top) and 535-536 (bottom), so canvas rows 259 up to 527: `DIALOG_TOP` 259, `DIALOG_BOTTOM` 528 (one past).
- Banner: on `result-hit.png` the same scan finds its border at panel rows 649-650 (top) and 779-780 (bottom). The plan's "about 641 canvas, panel 650" took the border's second row: the first row is panel 649, canvas **640**, so `BANNER_TOP` is 640 (Battleship's column ends at exactly 640, so it still passes; the review's "about y 649" is the panel row).
- Heap margin: with `HEAP_MARGIN_BYTES` raised in a scratch edit, every first-party round passes with the cap 64 KiB lower and fails at 80 KiB lower (`check`, `hint-correct`, `hint-wrong`, `long-expert`, at the first HINT: the solver loading takes the played Sudoku VM to about 176 to 192 KB of its 256 KB). The 16 KiB gate is not borderline. The new `long-expert` round fails at the same HINT; the rest of its mix (FILL NOTES, digit notes, CHECK, ~50 writes) did not push past it.
- `BoardConfig.h` compiles host-side with `-DFREEINK_DEVICE_X4PRO=1` and stubs for `Serial`/`Serial0`, `LOW`/`HIGH`/`OUTPUT`, `pinMode`, `digitalWrite`, `gpio_num_t`, `gpio_hold_dis/en`, `esp_rom_printf` and `<initializer_list>` (`harness/games_check/board_stubs/`, given to `BoardInsetsTest.cpp` alone through source-file include directories in `games_check.cmake`; it lives in `GamesCheckEngineTest`, whose include path has no `Arduino.h`, where `GamesCheckFlowTest`'s `screen_stubs/Arduino.h` would clash). The profile is landscape 800 x 480; the insets are in its portrait frame, so the test compares `displayHeight` with 480 and `displayWidth` with 800. It also pins `STICKY.viewableInsets` to the default.

**Decisions.**
- Check VM limits: 1 MB Lua heap (region 1.75 x), 16,000,000 instructions (`HostBounds.h`), in `ScriptVm` (wrapper count hook; message `<chunk>:<line>: instruction budget exceeded`, result `Fault`), `lib/` untouched. One difference from a device VM, recorded in `ScriptVm.h` and the README: `luaport_memoryerror` records a memory error only while `CallGuard::hook` is installed, so in a check VM a heap-cap error ends the call as a `Fault` (mapped from `LUA_ERRMEM`) but a script's own `pcall` can catch it first.
- `within_device_budget(f, ...)` is a global of a check VM, not a `host` field; it restarts the hook interval first so the count starts on a boundary as `CallGuard::arm` does.
- Which checks measure the game's own cost through a check VM's guard: only Sudoku's four `COSTLY` rows (symmetry, HINT, CHECK, FILL NOTES), now through `within_device_budget`. The bank's `CHUNKS` batches, the `deep` stack, and the pin-module split stay: no measurement shows one dead (the batches are sized to the device's budget, and the heap cause of the split is gone but harmless).
- Hidden `shows`: only flow frames count. A hidden step that ends the round shows seat 0 (a move that ends a round is never a turn change), so its `shows` is read from seat 0's frame, not the mover's: Battleship's `won-by-seat-1/2` ("Player N wins!") pass on seat 0's headline; before, they were met by the sweep's seat-N frame, which the device never shows then. Mutant 11a (that `shows` made "Their ships:", seat 1's firing text) shows the difference.
- The restore probe's `load` phase can only fail from `main.lua`'s own load (restore itself calls no Lua), so engine tests exercise `start` (status) and `draw`; the phase names are `load`, `restore`, `start`, `draw for seat N`.
- `RoundReport::restoreLog` (the last restored game's `ch.log`) exists for the pin of the probe against `GameVM`'s resume.
- Sudoku end screen (`games/sudoku/view.lua`): "Solved" at y 60, the level (small) at 120, "Time" at 164, "Best" or the hint note at 214 (ends 244, 15 px above the dialog); the new harness pin found the defect on every Sudoku solve round before the edit.

**Test doubles this change adds or extends** (each says in its header what it stands in for and where it is more permissive):
- check VM (`ScriptVm.h`): the device sandbox with larger limits; `ScriptVmTest` pins the device-limits VM to a `LuaGame` snippet for snippet (budget, heap, globals) and the check limits to their constants; more permissive: bigger heap and budget, `host`, `within_device_budget`, a catchable memory error.
- `HostBounds.h`: a copy of the host's dialog and banner layout, measured once in English; pinned by being read by every first-party check and by the engine test that asserts the values a check VM sees (mutant 12); there is no host code in the harness to compare it with, so a change of the host's dialog is a change here.
- restore probe (`RoundPlayer.h`): GameVM's Continue; `GamesCheckFlowTest.TheRestoreProbeStartsAGameFromASnapshotAsGameVMsResumeDoes` pins the two to the same sequence (no `setup`, the restored snapshot's `status` first, the device's draw among the probe's); more permissive: it draws every local seat where the device draws the turn seat, and the live VM plays on instead of the restored one.
- end-frame text pin: tests where a text starts, not the box (a device font metric).
- `GamesCheckRig.h`'s typed insets and canvases: `BoardInsetsTest` pins them to the SDK's profiles.

**Mutants** (each applied alone, `ctest -L games-check` red, reverted, `git status` identical after): 1, 2a-2d, 3a-3c, 4a-4d, 5a (round-file VM canvas), 5b (probe canvas), 6a (`note_h5.png` over by `note_g3.png`: `GamesCheckNoteImages`), 6b (`NOTE_W` 10: the Sudoku checks and `GamesCheckNoteImages`), 7 (a UTTT `draw` that asserts `setup` ran: `EveryRoundRestoresFromItsSnapshot`), 8 (a UTTT `apply` that grows the heap to 245 KB: the margin gate), 9 (a round tap at x 470, scratch round file), 10a (no `checks.lua`), 10b (`rounds/x.LUA`), 11a and 11b (hidden `shows`), 12 (`BANNER_TOP` 649: the engine's host-bounds check).

**Verification run.** `ctest -L games-check -j4`: 124 tests, all green (97 before; `GamesCheckNoteImages`, 2 `EveryRoundRestoresFromItsSnapshot` per game, `BoardInsetsTest` x 2, and the new engine, `ScriptVmTest` and flow tests). The whole host suite: 1802 tests, all green. `scripts/*_test.py`: all pass. `scripts/check_upstream_touches.py`: pass; none of the changed paths exists upstream. `./bin/clang-format-fix` twice: nothing new.

**Packages.** `games/sudoku/view.lua` and `layout.lua` (a comment) and `games/battleship/layout.lua` (comments) changed, so `sudoku.chgame` and `battleship.chgame` and their hashes change: the device-run packet's `sudoku.chgame` is to be rebuilt from the new head (cross-story review, "Result").

**Still open.** `sim.sh build x4pro` and the Sudoku solved-screen screenshots (`story-xr-screenshots/`, copied and listed here and under `## Auto Run Result`), after the review; no `pio run` or `pio check`: `src/`, `lib/`, `freeink-sdk/` are untouched.

**Review patches (2026-10-05).** The memory error a check's own pcall catches is now a fault in a check VM: `protect()` counts the arena's cap and region refusals from arm, the count hook raises a sticky "not enough memory" on growth, and any growth after the pcall is a `Fault` (so a refusal Lua would have recovered from by collecting also fails a check, which is stricter than the device and documented in `ScriptVm.h` and the README). `within_device_budget` raises when the guard has a fault or the arena refused growth, before it resets the hook. `static_assert`s pin `VmLimits::device()` to `ARENA_BYTES`, `LUA_HEAP_BYTES` and `CallGuard::INSTRUCTION_BUDGET`. `games/battleship/layout.lua` and `games/sudoku/layout.lua` are reverted to the epic head and the comment above `draw_end` removed: `games/**` is now only the functional end-screen change in `games/sudoku/view.lua`, so the earlier "Packages" paragraph (a Battleship change) no longer holds: Battleship's package is byte-identical to the epic head. The explanations went to `first_party/README.md`. Cost, measured after the patches: `ctest -L games-check -j4` is 124 tests in 6.95 s wall (24 proc-seconds; 97 tests in 3 s wall before), the whole host suite 1802 tests.

## Plan Change Log

- Banner top is 640, not "about 641" (Implementation Notes: the border's first row is panel 649).
- A hidden step that ends the round reads `shows` from seat 0's frame (the only frame the device shows then); the plan's "frames the flow itself drew" read for the mover would have failed Battleship's win rounds, which "must still pass or be corrected with the reason".
- `RoundReport::restoreLog` and `GamesCheckFlowTest.TheRestoreProbeStartsAGameFromASnapshotAsGameVMsResumeDoes` were added to pin the restore probe to `GameVM`'s resume (the boundaries ask a test double to be pinned to the device behaviour).

## Review Triage Log

### 2026-10-05 — Review pass
- verdicts: 31 findings — high 0, medium 5, low 26, false 0, maybe-false 0
- findings:
  - `[medium]` `[patch]` blind-hunter: a heap-cap error inside a check VM can be swallowed by the check's own pcall and the call ends OK — verified: `ScriptVm::protect` mapped only an outer `LUA_ERRMEM`, and a check VM has no CallGuard memory hook. Fixed: `protect()` counts arena cap and region refusals from arm, the count hook raises a sticky "not enough memory" on growth, and the call is a `Fault` after the pcall; `ScriptVmTest` has the pcall-swallow case; the two false sentences in `ScriptVm.h` and the README are corrected (any refusal is a fault in a check VM).
  - `[low]` `[patch]` blind-hunter: `BoardInsetsTest` header says no literal is compared with itself, yet hard-codes the SDK's expected insets, and the shim and `layout.lua` copies are not read — header reworded (literals are the SDK's expected values; the two copies are named, not read); row 2 itself lets the shim keep its literal.
  - `[low]` `[patch]` blind-hunter: `GamesCheckRig.h` cites `GamesCheckFlowTest` for `BoardInsetsTest`, which is compiled into `GamesCheckEngineTest` — comment fixed.
  - `[low]` `[reject]` blind-hunter: `rounds/` is strict about stray files (`.DS_Store`, `notes.md`) — row 12 says "any entry in `rounds/` that is not a regular `.lua` file"; the strictness is the row's decision and the README states it.
  - `[low]` `[reject]` blind-hunter: the added second play and restore probe have no measured cost — measured: `ctest -L games-check -j4` is 124 tests in about 7 s wall (97 tests in 3 s before), recorded in Verification; no defect.
  - `[low]` `[reject]` blind-hunter: the restore probe does not compare the restored snapshot, ver or frames with the live ones — `Session::restore` copies the bytes, so a snapshot comparison would pin the engine, not the game; frames differ by design (empty `ui`, Design Notes row 6).
  - `[low]` `[defer]` blind-hunter: the end-frame pin tests only where a text starts, and three games re-derive text-box heights by hand (`layout.DY`, UTTT `TEXT_BOX`, Battleship) — the harness limit is by design (a device font metric the stand-in lacks, Design Notes row 1); one shared height table is a refactor beyond a fix story.
  - `[medium]` `[defer]` blind-hunter: the dialog and banner bounds come from one hand measurement of three PNGs and nothing ties them to the host's layout (the same finding as verification-gap 1) — pre-existing in kind (Battleship typed 259 before) and a host-layout test is its own story; recorded with the `GameViewIconsTest` pattern.
  - `[low]` `[reject]` blind-hunter: `within_device_budget` fails only after the call completes and its test has about 10K slack — a runaway call still ends at the check VM's 16M budget; the vendored Lua's opcode counts do not change; the caught-fault hook reset is the edge-case finding below (patched).
  - `[low]` `[patch]` blind-hunter: nothing ties `VmLimits::device()` to the old `ARENA_BYTES`, and `CHECK_LUA_REGION_BYTES` is a bare 4/7 ratio — `static_assert`s now pin the device limits to `ARENA_BYTES`, `LUA_HEAP_BYTES` and `CallGuard::INSTRUCTION_BUDGET`, and the check region is derived from the device's ratio.
  - `[low]` `[patch]` blind-hunter: heap figures disagree between docs (180 to 200 against 180 to 190 KB) — README and `long-expert.lua` now both say 176 to 192 KB of 256 KB (the measured worst, first HINT).
  - `[low]` `[patch]` blind-hunter: shipped game sources carry multi-line comments pointing at test files and the end-screen change is not isolated — reverted `games/battleship/layout.lua` and `games/sudoku/layout.lua` and removed the comment above `draw_end`; `games/**` is now only the functional end-screen change in `view.lua` (Battleship's package is byte-identical to the epic head); the explanations moved into the README (orchestrator's instruction).
  - `[low]` `[reject]` blind-hunter: README re-flow formatting (long lines, a short line) — cosmetic, no behaviour; no tool checks Markdown wrap and a rewrap is a larger diff than the harm.
  - `[low]` `[reject]` blind-hunter: the `GamesCheckNoteImages` ctest adds a Python dependency and drops silently if the tool is deleted; the ZipDirectory change is unmentioned — `find_package(Python3 REQUIRED)` is already in `games_check.cmake`, a deleted companion tool leaves with its companion by design (README, "When a game leaves"), and row 16 asks for the ZipDirectory change.
  - `[low]` `[defer]` edge-case-hunter: in an open pass round `shows` for the mover's seat can still be met by a sweep frame (and the README claimed otherwise) — row 13 scopes the change to hidden rounds; README corrected (patch), the code change deferred.
  - `[low]` `[patch]` edge-case-hunter: the README claim that open and solo rounds read `shows` the same way is false for open pass — corrected to say only hidden rounds read flow-only frames.
  - `[low]` `[defer]` edge-case-hunter: the COSTLY HINT, CHECK, FILL NOTES and symmetry calls of each band's costliest puzzle no longer run at the device's heap, so no check bounds their heap — played-game heap is gated by the margin round (first HINT of `long-expert`), and the device budget is kept by `within_device_budget`; a per-band played round is a new round, unverified (low confidence).
  - `[medium]` `[patch]` edge-case-hunter: a check VM's memory error caught by the check's own pcall is not a fault (same root cause as blind-hunter 1) — fixed with that finding.
  - `[low]` `[patch]` edge-case-hunter: with no steps the final restore probe is labelled `begin ... after the last step` — labelled "after begin".
  - `[low]` `[patch]` edge-case-hunter: the `GamesCheckRig.h` comment names the wrong executable (same as blind-hunter 3) — fixed with it.
  - `[low]` `[patch]` edge-case-hunter: "Every round also pays four more things" lists three — the count now reads three.
  - `[low]` `[patch]` edge-case-hunter: `within_device_budget` called after a caught sticky fault resets the interval-1 re-raise hook — it raises before `lua_sethook` when `guard.fault()` is set or the arena refused growth; the test's assertion does not isolate this guard (the sticky hook also re-raises), recorded as a limit.
  - `[medium]` `[defer]` verification-gap: the host dialog and banner bounds are not tied to the host's layout, so a host layout change leaves every consumer pinning the old numbers — as above; to be built as a host-side test beside `GameViewIconsTest`'s `fui::optionDialog` frame stub.
  - `[low]` `[defer]` verification-gap: the restore probe is pinned to `GameVM`'s Continue for a hidden pass roster only, not solo or open pass — `Session::restore` and `start` are shared, so the risk is the differing `rounds.start` against `begin`; a solo variant of `TheRestoreProbeStartsAGameFromASnapshotAsGameVMsResumeDoes` is a small follow-up.
  - `[medium]` `[patch]` verification-gap (other findings): the README and `ScriptVm.h` claim a caught memory error still ends the call as a fault, which is false — same root cause as blind-hunter 1; fixed with it.
  - `[low]` `[reject]` verification-gap (other findings): `ZipDirectoryTest` hard-codes 64 and 65 in one loop — row 16 asks for "64 and 65 in the list" and the other test uses `PACKAGE_MEMBERS + 1`.
  - `[low]` `[reject]` intent-alignment: reading C1 (VM-granular exemption: the larger-limit VMs still call game modules) against C2 (code-granular) — a documented reading; the intent's fixed rule (played game and load probe keep the device's limits) holds, and the only cost proofs through a check VM are Sudoku's COSTLY rows, kept on `within_device_budget` (Design Notes, row 4).
  - `[low]` `[reject]` intent-alignment: row 5 reads as an arena-peak read, implemented as a lowered-cap replay — a documented reading (`ArenaAllocator` has no `luaHeld` peak and `lib/` is out of bounds); the worst round passes 64 KiB lower, so the gate is not borderline.
  - `[low]` `[reject]` intent-alignment: row 6 restores and draws but takes no input on the restored game — row 6's fix text says restore, then draw every local seat; taps written against a live `ui` would not mean the same on an empty one (Design Notes, row 6).
  - `[low]` `[patch]` intent-alignment: the closing condition's screenshots are one image at one canvas and one variant — added the "No best time after a hint" Hard-band variant (the widest line) on the X4 Pro; the games check covers both canvases (466 and 474) on the harness surface.
  - `[low]` `[reject]` intent-alignment: the hidden win-step `shows` rule (seat 0's frame) and the Sudoku digest no longer counting `refresh` go a little beyond the rows' words — each is the device's behaviour (a move that ends a round is never a turn change; the engine's `gfxRefresh` appends no command) and is recorded in Implementation Notes.
  - Root-cause groups: the memory-error entry (blind-hunter 1, edge-case-hunter 4, verification-gap other 1; medium, patched) is one group; the dialog and banner bounds entry (blind-hunter 8, verification-gap 1; medium, deferred) is another. The review loop ran once (`review_loop_iteration` 0). Lenses ran as four context-free subagents on the staged diff (259,635 B).

## Design Notes

**Row 4, which VMs run check-only code (settled from the harness source).** `OwnedVm::create` is called at four places in `GameCheck.cpp`: `runGameChecks` (the `checks.lua` VM, :~354), `playRounds`'s clash probe (:~447) and each round file's VM (:~460, the VM that runs `rounds/<name>.lua` and, kept alive by `loadRound`, its `steps(state)` via `resolveSteps` in `RoundPlayer::begin`), and `checkPackage`'s module-loading probe (:~275). The first three run only the companion's code: check limits. The probe runs `main.lua`'s own load to count module nesting, and the played `LuaGame` (`Player::prepare`) is the game itself: both keep the device's cap and budget (the orchestrator's fixed rule). `ScriptVmTest` pins `ScriptVm` to `LuaGame` snippet for snippet, so it builds `ScriptVm` with device limits there, and adds one test for the check limits.

**Larger budget without touching `lib/`.** `CallGuard::hook` trips when its own `spent` reaches the lib constant. A check VM installs a wrapper hook after each `guard.arm(L)` (in `ScriptVm::protect`) that counts COUNT events in `HOOK_INTERVAL` steps itself, raises through `guard.raise(L, "instruction budget exceeded")` at `CHECK_INSTRUCTION_BUDGET`, and forwards every other event to `CallGuard::hook` (a recorded fault replaces the hook with `CallGuard::hook` at interval 1, so a memory or binding fault still wins). The VM result stays `Fault`. `within_device_budget` reads the same counter, so the Sudoku `COSTLY` entries (the only checks that prove a game call fits the device budget through the checks VM's guard: symmetry, HINT, CHECK, FILL NOTES of each band's costliest puzzle) keep that proof on a count hook.

**Row 5, the margin gate.** The cap counts garbage and a first-fit region: the only exact statement of "margin under the device cap" is that the game still plays with the cap lowered by the margin. `ArenaAllocator` exposes no peak of `luaHeld` (its `peakBytes()` counts block headers, 16 B on the host against 8 B on the device, so it is not comparable), and `lib/` is out of bounds, so the gate is a second play at `LUA_HEAP_BYTES - 16 KiB` after a clean play at the cap. The first play is the device's.

**Row 6, the restore probe is a side probe, not a replacement.** The device builds a new VM and starts every seat's `ui` empty (`GameVM::run`, `resumeLength != 0`), so a round's later taps, written against the `ui` they built (Sudoku's selection, Battleship's rotation), would not mean the same on a restored VM; the edge-case reviewer saw exactly those ui-loss mismatches when frames were compared. The probe therefore restores into a second VM, draws every local seat, and drops it, while the live VM plays on. It proves restore, start and draw do not fault or raise from a snapshot with an empty `ui`, which is the Continue seam; it does not prove the same taps work on it.

**Row 13.** Only hidden rounds change: an open or solo round's `shows` still reads `MatchRounds::step`'s frame first.

**Row 1.** The dialog's band is measured once in a simulator frame (an English-language host `tr()` dialog; a longer translation could move the top edge), so `HostBounds.h` states the measurement and that it is a double of the host's layout. The harness pin tests a text command's top y (the box height is a device font metric the stand-in lacks); each game's own check pins the full box with its own line steps.

**Guards in functions this change rewrites (AGENTS.md `git log -L`).** `runGameChecks`: the `hasChecks` early return protected "nothing to run" (now a failure, and the return stays so no VM is built); the `fault()` break stops a game's remaining checks after a guard fault, as the device ends a game on one, and keeps counting the unrun ones; the list read refuses a non-table or empty list. `playRounds`: the `files.empty()` failure, the clash probe, and the per-round `continue` after an unreadable round (so the others still play) all stay. `Player::step`: the over-round, wrong-shown-seat and `ver` guards stay, and the new tap guard runs before the clock advances. `Player::begin`: the `resolveSteps` call must run once per round object (a second play reuses the resolved steps). `shows()`: the first-frame-for-the-seat rule stays for solo and open rounds.

## Verification

**Commands:**
- `cd /home/user/epic-first-party-games-lane-c && export PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache && flock /tmp/crosshatch-hosttest.lock sh -c 'cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest && ctest --test-dir build/test -L games-check --output-on-failure -j4'` -- expected: all pass, the count above 97 by the new tests.
- The same under the lock with `cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: the whole host suite green (the other tests include the signature changes).
- Mutants, each applied alone to a scratch change (never committed), `ctest -L games-check` red, then reverted: (1) Sudoku `view.lua` end-screen name back at `oy + 260`; (2) `draw_mark` returning at the top of the dots branch; focused dot `"black"` to `"white"`; ring `"dark"` to `"light"`; the `if ground then ... circle(cx, cy, 6, "white", true)` line deleted; (3) the pad focus frame at `view.lua:180` deleted; the rail's inverted fill off; all MENU icons `"x"`; (4) `ch.gfx.clear("white")` to `"black"` in each of the three games; Battleship's grid lines `"white"` (`main.lua:231-232`); (5) `OwnedVm::create` at the round-file VM without the roots' canvas; the probe's canvas dropped; (6) `note_h5.png` overwritten with `note_g3.png`; `NOTE_W` 12 to 10; (7) a game whose `load` or `draw` after `restore` raises; (8) a game that grows the Lua heap to 245 KB in one round; (9) a round tap at x 470 on the 466 canvas; (10) a game with no `checks.lua`; `rounds/x.LUA`; (11) a Battleship placement `shows` met only by another seat's frame; (12) the first-party `host.banner_top` raised to 649.
- `python3 test/game_script/first_party/sudoku/tools/make_note_images.py --check` -- expected: exit 0, and the same command is a ctest in the `games-check` list.
- `python3 scripts/check_upstream_touches.py`, the fork script tests under `scripts/*_test.py` that exist, and `./bin/clang-format-fix` twice with nothing new in `git status` -- expected: pass.
- After the review: `sim.sh build x4pro` under `flock /tmp/crosshatch-build.lock`, then the Sudoku solved screen (how `story-sudoku-screenshots/solved.png` was made: see `story-sudoku-plan.md`) screenshotted; copy the shots to `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-xr-screenshots/` and list each with a line in this plan and under `## Auto Run Result`. No `pio run` or `pio check`: no firmware source changes (`src/`, `lib/`, `freeink-sdk/` untouched); say so in the result.

**Manual checks (if no CLI):**
- Look at each screenshot: the level name is visible above the dialog and overlaps no other text.
- Read the final diff for the stale words "Sticky's canvas (3, 6)", "three tests per game", "about y 649", "engine's only", "no checks.lua: none run", "same as the device", and for any default `canvas` left.

## Auto Run Result

**Summary.** Rows 1 to 19 of the cross-story review are implemented in the harness, the companion folders and the docs, with one shipped-game change: Sudoku's end screen now draws "Solved", the level, "Time" and "Best" so every box ends above the host's end-of-round dialog (y 244 against 259). New pins: a shared host-bounds header (`HostBounds.h`) read by the harness and exposed to check VMs as `host`; an end-frame text pin in `RoundPlayer`; check VMs with a 1 MB heap and 16 M instructions (device limits kept for the played game and the load probe); a played-heap margin gate (a second play 16 KiB below the cap) and a long Expert round; a restore probe of every round at both canvases; off-canvas tap, missing `checks.lua`, mis-named round and hidden `shows` rules; required `canvas` parameters; ink, dot-note, pad, rail, MENU and note-image pins; refresh counting; the SDK-insets test; and the doc fixes.

**Files changed.**
- `games/sudoku/view.lua`: the end-screen layout (the only shipped-game change).
- `test/game_script/harness/games_check/`: new `HostBounds.h`, `BoardInsetsTest.cpp`, `board_stubs/`; changed `GameCheck.*`, `GamesCheckRig.h`, `RoundPlayer.*`, `ScriptVm.*`, `TestSupport.h` and the games-check tests.
- `test/game_script/harness/`: `games_check.cmake` (note-images ctest, `BoardInsetsTest` stubs), `ZipDirectoryTest.cpp`, comments in `GameMatchTest.cpp`, `FrameReplayTest.cpp`, `GameVmTest.cpp`, `stubs/GfxRenderer.h`.
- `test/game_script/first_party/`: the three companions' `checks.lua`, recorders and pins, the new `sudoku/rounds/long-expert.lua`, `sudoku/tools/make_note_images.py` (header), the README.
- Docs and comments: `docs/crosshatch/game-canvas.md`, `test/game_script/fixtures/{README.md,pass-keep/main.lua}`, the simulator shim's comment.
- Screenshots: `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-xr-screenshots/`.

**Review breakdown.** 31 findings (high 0, medium 5, low 26). Patched: the check-VM memory error (one root cause, three lenses; medium), `within_device_budget` after a caught fault, the `GamesCheckRig.h` and `BoardInsetsTest` wording, the empty-steps probe label, the device-limits `static_assert`s, four README corrections, the removal of test-pointing comments from `games/**`, and a second screenshot. Deferred (five): the host dialog and banner bounds not tied to the host's layout (medium), the restore pin for solo and open pass, open-pass `shows`, duplicated text-box heights, the per-band costliest-puzzle heap. Rejected, each with its reason in the log: strict `rounds/` entries, the unmeasured cost (now measured), restore-vs-live comparison, README rewrap, the Python ctest dependency, the ZipDirectory literals, `within_device_budget` timing, and the intent-alignment readings C1, D2, E1.

**Follow-up review recommendation:** `false`. The first pass patched one medium entry and no high.

**Verification.**
- `ctest --test-dir build/test -L games-check -j4`: 124 of 124 pass (97 before), 6.95 s wall. Whole host suite: 1802 of 1802 pass.
- All `scripts/*_test.py` pass; `python3 scripts/check_upstream_touches.py`: PASS; `./bin/clang-format-fix` twice left nothing new.
- 24 scratch mutants of the regressions, each red alone and reverted (list in Implementation Notes); the memory-swallow case is a test.
- `sim.sh build x4pro` under the build lock: success. Screenshots (X4 Pro, 466 x 788) were taken from a scratch Sudoku package with one blank cell so the real `view.lua` draws the end screen; the scratch package is not committed.
- No `pio run` or `pio check`: `src/`, `lib/` and `freeink-sdk/` are untouched. Fresh-clone gate run: not applicable (no CI workflow or gate changed; the games-check job and its three targets are unedited).

**Screenshots** (look at each):
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-xr-screenshots/sudoku-solved-level-above-dialog.png`: Easy solved screen, "Solved", "Easy", "Time 1:04", "Best 1:04" all above the Game over dialog, no overlap.
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-xr-screenshots/sudoku-solved-no-best-after-hint.png`: Hard solved screen after a hint, "Solved", "Hard", "Time 62:09", "No best time after a hint" (the widest line), all above the dialog.

**Package hashes** (`scripts/pack_game.py` on `games/<id>/` at this commit; the device-run packet's copies are to be rebuilt by the orchestrator):
- sudoku `e35f23efd835450d` (changed)
- ultimate-tic-tac-toe `a7e63b542144938b`
- battleship `10a07de10cb14489` (byte-identical to the epic head, so the owner's Battleship file stands)

**Formatting.** `./bin/clang-format-fix` made no formatting-only change outside the paths of this change.

**Residual risks.** A check VM now fails on any refused allocation, even one Lua would recover from (a 1 MB cap makes that unlikely). The host dialog and banner bounds are a measurement, not derived from the host's layout (deferred, medium). The restore probe proves restore, start and draw from an empty `ui`, not input on the restored game. Text fit is still only proven on the device.

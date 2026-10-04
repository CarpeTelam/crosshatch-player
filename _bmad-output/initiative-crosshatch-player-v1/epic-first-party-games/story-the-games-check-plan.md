---
title: 'The games check'
type: 'feature'
ticket: '8'
created: '2026-10-04'
status: 'draft'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/test/game_script/fixtures/README.md'
  - '{project-root}/docs/crosshatch/api-level-1.txt'
warnings: [oversized]
deferred: []
---

<intent-contract>

## Intent

**Problem:** No PR check packs, installs, or plays `games/<id>/`, so a broken first-party game first fails at release (epic Notes, pre-inception audit, 2026-10-04), and there is no first-party game at all. Entries 2 and 3 (Battleship, Sudoku) build on this entry's three interfaces: the rounds file, the companion folder's `checks.lua`, and the 9x9 board module.

**Approach:** A generic harness target `test/game_script/harness/games_check.cmake` finds every `<games root>/<id>/` at configure time, packs it with the real `pack_game.py`, installs it with the real installer, runs the game's `checks.lua` and every round of its companion folder `test/game_script/first_party/<id>/` headlessly over `Session` and `MatchRounds`, and fails on the epic's R10 list; `games-check` in `crosshatch-ci.yml` runs it. The tracer game is `games/ultimate-tic-tac-toe/` (R1, R3, R6, R8) with the board module (`board.lua`) entry 3 copies once, a HOW TO PLAY page behind a `question` icon button, and its rounds and checks in its companion folder.

## Boundaries & Constraints

**Always:** Every new file is under `games/`, `test/game_script/`, `.github/workflows/crosshatch-ci.yml` (a Game path), or is this plan and its screenshots; no upstream file changes, so `check_upstream_touches.py` stays green with no ledger edit. The check names no game, and its own tests use fixtures (`test/game_script/fixtures/`) or text they write into the build directory, never `games/` (R2). `games/ultimate-tic-tac-toe/` is self-contained: its own text (not `tr()`), `main.lua` plus `board.lua`, no `icon.png`, no path outside itself named in it. Every drawing command, icon, and `ctx` field stays within `api-level-1.txt`. Every snapshot stays at or under 700 B (R9). Lua is 5.5 (`global` is reserved). Local variables in C++ stay under 256 B, buffers go on the heap, and allocation uses `makeUniqueNoThrow` or `new (std::nothrow)` where it can fail (AGENTS.md; the host tests follow it too, since this code is the template for later entries). No `Serial.print*`; the check logs through `std::cerr`/gtest, which is host-only.

**Never:** Change `src/`, `lib/`, `docs/crosshatch/api-level-1.txt`, `API_SURFACE_CRC`, `platformio.ini`, the freeink-sdk pointer, `.skills/`, upstream's `ci.yml`, or any existing harness file (the new suite is one more `harness/*.cmake`, which the harness globs; `test/CMakeLists.txt` is untouched). Add `labeled` or another event to `crosshatch-ci.yml`. Commit scratch trees or generated files. Put a game, round, or fixture-copy of a game in `games/` for a test. Pass `games/` to another harness target.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Green game | `games/<id>/` packs, installs, its checks and rounds pass | each `GamesCheck` test of the id passes; skipped declared modes are logged | none |
| Pack failure | the packer exits non-zero for the folder | the id's package test fails with the packer's stderr | the other ids still run |
| Load or Lua fault | `setup`, `apply`, `status`, `input`, or `draw` raises, exceeds 2,000,000 instructions, or a frame passes `frame_commands_count` | the round fails naming round, step, and `errorMessage()` | the round stops; later rounds run |
| Snapshot over 700 B | any snapshot after `begin` or a step is larger | failure naming round, step, and the size | none |
| Wrong outcome | final status differs from the round's `winners` (or an `unfinished` round is over) | failure naming expected and actual winners | none |
| Rejected tap | step with `move = false` | `ver` unchanged and, if `shows` is set, the seat's next frame holds that text | a step that moves anyway, or lacks the text, fails |
| Wrong seat | step names a seat other than the one shown | failure (the device delivers input to the shown seat only) | none |
| No rounds / companion without game | `rounds/` missing or empty; `first_party/<id>/` with no `games/<id>/` | failure | none |
| Round for another mode | round `mode` not in the manifest, or not `solo`/`pass` | failure | none |
| Declared `nearby` | manifest lists `nearby` | logged as skipped, no failure | none |
| Hidden manifest | `hidden` true, `pass` | the hidden flow plays (below); every local seat is still drawn after each step | none |
| `checks.lua` fails | a check raises, the file is missing a list, or a guard fault | failure naming the check; a guard fault stops that game's remaining checks, counted | none |
| Seed | same seed twice / another seed | identical / different `math.random` draws in `setup` | none |
| UTTT forced board | last move in cell c, board c open | next move must be in board c (else rejected "Play in the highlighted board"), and board c is highlighted | |
| UTTT finished target | board c won or full | any open board may be played | |
| UTTT big result | three won small boards in a line / no live line left | `winners` = that seat / `{}` | |

</intent-contract>

## Code Map

Read, never edit:
- `test/game_script/LuaGameFixture.h` -- `LuaGameTest::SessionGame`, the arena/frames/canvas/`HostPorts` wiring, `frontCommands()`; the model for the check's rig. The check cannot derive from it (it is gtest-bound): copy the wiring into `GamesCheckRig`.
- `lib/GameScript/LuaGame.cpp` 191-374 -- `load()` (state, `BindingContext` fields, `guard.install`) and `enter()` (`guard.arm`, `lua_pcall`, fault vs status): `ScriptVm` mirrors these with public pieces (`ArenaAllocator::luaAlloc`, `openSandbox`, `openChLibrary`, `setBindingContext`, `CallGuard`).
- `lib/GameScript/MatchRounds.h` -- `begin`, `beginAgain`, `play(event, seat)`, `draw(seat)`, `start`, `step`; `lib/GameCore/SeatShown.h`, `MatchLifecycle.h`; `src/games/GameVM.cpp` 295-340 (`stepHandOff`), 260-293 (`drawShown`, `showSeatNow`) -- the hidden flow the driver follows.
- `lib/GameCore/Session.h` (`ver()`, `status()`, `snapshot()`, `draw(seat)`), `Roster.h` (`pass`, `passSeats`), `Manifest.h` (`ManifestReader`, `ManifestSettings`, `SettingValues`), `src/games/GameRegistry.h` (`readGame`), `GameAssets.h`, `MatchStore.h`, `GamePackageInstaller.h`, `GameHostCaps.h`.
- `test/game_script/harness/packed_fixtures.cmake`, `pack_fixtures.py`, `PackedFixturesTest.cpp` -- the pack-then-install pattern, `HalDisplay display;`, `gtest_discover_tests`. `installer.cmake`, `match.cmake` -- `game_installer_src`, `game_harness_src`, `game_harness_core`, `game_match_src`. Linking `game_installer_src` with `game_harness_src` and `game_harness_core` in one executable links cleanly (probed during planning with a scratch executable that called `installAll()` and `GameAssets::load` with a `MatchStore`; removed afterwards).
- `test/game_script/fixtures/pass-open`, `pass-hidden`, `pass-art`, `tracer` -- engine scratch games (their `ch.log` lines name each seat's calls).
- `test/game_script/harness/GameVmTest.cpp` 215-300 and `MatchSupport.h` -- the GameVM hidden-flow tests and rig the pin test reuses.

New, all under `/home/user/epic-first-party-games-lane-a/`:
- `test/game_script/harness/games_check.cmake`, `harness/pack_games.py`, `harness/games_check/*` (core library, glue, tests), `test/game_script/first_party/README.md`, `.../ultimate-tic-tac-toe/{checks.lua, taps.lua, rounds/*.lua, tools/make_rounds.py}`.
- `games/ultimate-tic-tac-toe/{manifest.json, main.lua, board.lua}`.
- `.github/workflows/crosshatch-ci.yml` -- the `games-check` job and its `needs` line (also its header comment).

## Tasks & Acceptance

**Execution** (in this order; group 1 stands alone and can be a separate commit if the orchestrator splits the ticket):

Group 1, the check.
- [ ] `test/game_script/harness/games_check/ScriptVm.{h,cpp}` -- class over `lua_State`: `ScriptVm(arena, sources, ports, canvas, images)`, `load()` (state from `luaAlloc`, `openSandbox`, `openChLibrary`, installs a `CallGuard`), `call(chunkText, name, fn)` running a function under `guard.arm` with the 2,000,000-instruction budget and reporting `Ok | Error(message) | Fault(message)`. Extra sources are the companion folder's top-level `*.lua` modules (a name clash with a game module is a failure). Stands in for the device sandbox: pinned by `ScriptVmTest.cpp` (same snippets through `DirectGame` and `ScriptVm`: `os`, `load`, `package` are nil, `require` finds package modules, a loop faults at the budget, `ch.gfx` outside `draw` errors).
- [ ] `games_check/RoundFile.{h,cpp}` + `RoundFileTest.cpp` -- evaluates `rounds/<name>.lua` in a `ScriptVm` and converts the returned table into `Round` (C1 below); unknown keys, wrong types, empty `steps`, both or neither of `winners`/`unfinished`, `seed` not an integer, a setting id or value the manifest lacks are errors naming the key.
- [ ] `games_check/RoundPlayer.{h,cpp}` -- plays one `Round` over `LuaGame`, `Session`, `MatchRounds` (D1): fresh `LuaGame` per round with `SeededRandom(seed)` as `HostPorts::random`, a `FakeClock` the steps advance, the device canvas 474x788, `setSettings`; solo uses `start`/`step`, open pass the same, hidden follows `stepHandOff` (D2); after `begin` and every step it checks the snapshot size, draws every local seat (and seat 0 once over), reads each frame's text commands, and records failures. Option `drawEveryLocalSeat` (default true).
- [ ] `games_check/GameCheck.{h,cpp}` -- installer-bound glue: packed `.chgame` onto the fake card, `installAll()`, `GameRegistry::readGame`, `GameAssets::load` through a `MatchStore`, `ManifestReader` settings, then `runChecks` (C2) and `playRounds` (modes: play `solo`/`pass` the manifest declares and `gameHostCaps()` can start; log each other declared mode as skipped), returning a `Report` of failures and notes.
- [ ] `games_check/SeededRandom.h` -- `GameCore::IRandom` over a splitmix32 stream from the round's seed; no process-wide state.
- [ ] `harness/pack_games.py` -- for each id runs `pack_game.py <root>/<id> <out>`, writes `<id>.hash` on success or `<id>.packerror` with stderr on failure, then a `packed.stamp`; exit 0 after every id was tried, 2 for usage; never fails the build for a game's fault (the test does).
- [ ] `harness/games_check.cmake` -- cache variables `GAMES_CHECK_GAMES_ROOT` (default `games/`) and `GAMES_CHECK_COMPANION_ROOT` (default `test/game_script/first_party`), made absolute; ids = directories of each root, found with `CONFIGURE_DEPENDS` globs; one custom command (inputs: every file of the games root, `pack_games.py`, `pack_game.py`, `fork_common.py`, `ApiLevel.h`, `names.txt`) and a `packed_games` target; library `games_check_core` (ScriptVm, RoundFile, RoundPlayer; links `game_harness_core`, `lua_vendored`); executables `GamesCheckTest` (production), `GamesCheckEngineTest` (self-tests), each compiling `Session.cpp` and `MatchLifecycle.cpp` and linking `game_installer_src game_harness_src game_harness_core GTest::gtest_main`; `GamesCheckFlowTest` (the pin, D2) linking `game_match_src`, `games_check_core`, `GTest::gtest_main`. Definitions: roots, ids, `PACKED_GAMES_DIR`, scratch dir, `PACK_GAME_PY`, `PYTHON_EXECUTABLE`, `MATCH_FIXTURES_DIR`. `gtest_discover_tests(... PROPERTIES LABELS games-check)` for all three; `GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST` for an empty id list.
- [ ] `games_check/GamesCheckTest.cpp` -- per game id: `ThePackageInstallsAndLoads` (packerror absent, installer reports 1 installed, registry lists it `check.ok()`, the hash equals the packer's), `TheGamesOwnChecksPass`, `EveryRoundPlaysAsItsFileSays` (no rounds is a failure); per companion id: `HasAGame`.
- [ ] `games_check/GamesCheckEngineTest.cpp` -- the committed negative and positive cases of the I/O matrix over scratch trees the test writes into the build directory (copies and edits of `fixtures/pass-open`, `pass-hidden`, `pass-art`, a `math.random` scratch game, packed by the real packer through `std::system`): pack failure, Lua error, instruction-budget fault, frame-limit fault, snapshot 701 B red and 700 B green, wrong winners, no rounds, companion without game, undeclared-mode round, `nearby` skipped, failing and passing `checks.lua`, a check that exceeds the budget, the hidden flow over `pass-hidden`, settings over `pass-art`, seeds (same seed same draws, different seed different).
- [ ] `games_check/GamesCheckFlowTest.cpp` -- the pin (D2): over `pass-hidden`, the seats drawn and the input and apply lines of `RoundPlayer` with `drawEveryLocalSeat` off equal `GameVM`'s hidden flow's log lines for the same four taps.

Group 2, the game.
- [ ] `games/ultimate-tic-tac-toe/board.lua` -- C3. `manifest.json` -- id `ultimate-tic-tac-toe`, name "Ultimate Tic-Tac-Toe", version "1.0.0", api 1, seats 2/2, modes `["pass"]`, hidden false, no icon (the launcher's default mark). `main.lua` -- the rules and drawing (Design Notes).
- [ ] `test/game_script/first_party/ultimate-tic-tac-toe/rounds/{won-by-seat-1,won-by-seat-2,drawn,forced-board,rejected-move}.lua`, `taps.lua` (cell to canvas tap through `board`), `checks.lua` (board geometry at 474x788, 480x800, 320x480; every win line; apply rejections), `tools/make_rounds.py` (stdlib; an independent Python implementation of the rules that searches playouts and prints the move lists committed in the rounds; not run by CI).
- [ ] `test/game_script/first_party/README.md` -- the companion folder (layout, the rounds format, the `checks.lua` interface, the cache variables, how to run `GamesCheckTest`, how a game leaves with its folder).

Group 3, CI and evidence.
- [ ] `.github/workflows/crosshatch-ci.yml` -- job `games-check` (checkout with submodules, `apt-get install cmake ninja-build`, googletest cache as `ci.yml`, configure, `cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest`, `ctest --test-dir build/test -L games-check --output-on-failure`), added to `Crosshatch Test Status` `needs`, and one line in the header comment.
- [ ] `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-tracer-screenshots/` -- the five simulator shots of Verification.

**Acceptance Criteria:**
- Given `games/ultimate-tic-tac-toe/`, when `ctest -L games-check` runs, then its package, checks, and every round pass, and the five rounds include a win by each seat, a draw, a forced-board round, and a rejected move.
- Given a scratch game root where a game fails to pack, raises a Lua error, exceeds a budget or frame limit, grows a snapshot past 700 B, loses its rounds, or fails its `checks.lua`, when `GamesCheckTest` runs against it, then it fails, and it passes for scratch rounds over `fixtures/pass-hidden/` and `fixtures/pass-art/`.
- Given the finished tree, when `test/game_script/harness/` and `scripts/` are searched for the names of the three first-party games, then no harness code or script names one (fixture names excepted).
- Given the PR, when CI runs, then `games-check` runs the three executables and `Crosshatch Test Status` waits for it.
- Given the packed game in `fs_/games/`, when Games is opened in the simulator, then it installs and a pass round plays with the playable small boards highlighted.

## Implementation Notes

## Plan Change Log

- 2026-10-04 (orchestrator, after the owner's checkpoint on entry 1): this plan started as a copy of entry 1's approved plan (`story-tracer-the-games-check-and-ultimate-tic-tac-toe-plan.md`). The owner approved C1 (amended: `steps` may be a function `steps(state)` called once after `begin` with the initial state decoded from the snapshot, under its own budget), C2, C3, and D1 to D3, recorded as epic Notes Decisions of 2026-10-04, and split the ticket: this entry, 8, is groups 1 and 3 (the games check, its engine tests, the `games-check` job, `first_party/README.md`); group 2 (Ultimate tic-tac-toe) and the screenshots stay with entry 1, which merges after this one. `games/` does not exist yet, so the production target must pass with it missing or empty. Status draft: the next run narrows this plan to entry 8 and adds the amended C1.

## Review Triage Log

## Design Notes

**The three choices for the owner's checkpoint** (epic Notes, 2026-10-04: "Entries 1 and 3 get a plan checkpoint"; "entry 1's checkpoint is where the owner approves the rounds file format, the companion folder's `checks.lua` interface, and the 9x9 board module's interface"). Recorded as Decisions in the epic Notes once approved, before entries 2 and 3 are dispatched.

C1, the rounds file `rounds/<name>.lua` (a Lua chunk evaluated in the game's sandbox VM: `ch.screen` is the 474x788 device canvas, `require` finds the game's modules and the companion folder's top-level `.lua` files, so a round computes taps with the game's own `board`; it returns):

```lua
return {
  mode = "pass",                   -- "solo" | "pass"; must be a mode the manifest declares
  settings = { level = "Easy" },   -- optional: setting id -> value (else the manifest's default)
  seed = 1,                        -- optional integer (default 1): seeds the game's random source
  steps = {                        -- at least one, one tap each, in order
    { seat = 1, x = 120, y = 300 },
    { seat = 2, x = 60, y = 300, move = false, shows = "That cell is taken" },
    -- optional: wait = <ms> (the clock advances first); move = false (the tap must not change
    -- the state, default true = it must, ver + 1); shows = "text" (a text command containing it
    -- in `seat`'s frame after the step)
  },
  winners = { 1 },                 -- the round must be over with exactly these ({} = draw), or
  -- unfinished = true,            -- the round must still be on after the last step
}
```
Mode is per round, not a cross product, because the modes differ in play (solo: seat 1 plays every move) and the epic only has single-mode games (R1: UTTT and Battleship `pass`, Sudoku `solo`); R10's "in each declared mode it can play" is read as: each round runs in its `mode`, a declared mode with no round is not a failure, and a declared mode the host cannot play (`nearby`) is logged as skipped. Taps, not moves, because `Session` takes a move only from `input` (`Session::handle`), so only a tap exercises `input`, `apply`, `status`, and `draw` together. Timer events are not delivered; `wait` only moves `ch.time.ms()` (Sudoku's elapsed time). Alternatives rejected: JSON (raw pixel lists for 81 cells are unreadable), moves into `LuaGame::apply` (skips `input` and the Session).

C2, `checks.lua`, loaded as module `checks` in the same sandbox VM (same libraries, `ch`, canvas, `require`; `math.random` seeded 1, so a check calls `math.randomseed(n)` for its own seed and `require("main")` for the game table, then `setup(ctx)` with the settings it chooses):

```lua
return {
  { name = "every win line wins", run = function() ... end },  -- failing = raising (assert or error)
}
```
Each `run()` is its own guarded call with a fresh 2,000,000-instruction budget, so many checks may total far over it (Sudoku: one per bank puzzle); loading the module is one call too. A failure names the check and the Lua message and later checks still run; a guard fault (budget, stack, memory) fails that check and stops the game's remaining checks, which the report counts. No entries, or a non-list, is a failure. The VM is built from public `lib/GameScript` pieces, so no engine file changes.

C3, `board.lua` (copied once into `games/sudoku/` by entry 3, then owned by each):
`board.layout(w, h, opts?)` -> `L = {x, y, cell, size, block}` (`opts`: `top` default 120, `bottom` 40, `margin` 4; `cell = min((w - 2*margin)//9, (h - top - bottom)//9)`, `size = 9*cell`, `block = 3*cell`, `x = (w - size)//2`, `y = top`; 51 px on 474x788); `board.cell_rect(L, row, col)` and `board.block_rect(L, brow, bcol)` -> `x, y, w, h`; `board.cell_at(L, x, y)` -> `row, col` or `nil` outside the grid; `board.draw_grid(L)` draws 1 px lines on every cell edge and 3 px black lines on every block edge and the border (about 30 commands), nothing else. Pure except `draw_grid`, which needs `draw`'s context.

**Settled by existing text.** Driver at the `Session`/`MatchRounds` level, not `GameVM` (D1; the builder's pick the ticket allows): the check must draw every local seat after each step (R10, epic Notes 2026-10-04), which `GameVM` never does (it draws the shown seat only); a seat's frame in a non-turn seat is what `nearby` needs (epic Notes, Handoff to epic-play-nearby). It is deterministic (no threads or clock), so no flake bar applies, and a per-round seed needs only an `IRandom` (`LuaGame::load` and `openSandbox` take `HostPorts::random`), not the target stub `screen_stubs/esp_random.h` the ticket's unknown feared. Real pieces: the real packer, installer, registry, `GameAssets`, `LuaGame`, `Session`, `MatchRounds`, `seatShown`, and `gameHostCaps()`.

D2, the hidden flow (R10; spine AD-21; `GameVM::stepHandOff`): for a `hidden` pass manifest the driver draws nothing after `begin` (HandOff); shows the turn seat (`seatShown(Playing)`); delivers a step only to the shown seat (else the step fails); after a move with `!over && turn != seat` shows the mover (Result), then HandOff, then the new turn seat; once over it shows seat 0. Each guard in `stepHandOff` it follows: a seat other than the shown one gets no input; a move that ends the round is RoundOver, never a turn change; seat 0 is drawn when the status is over. It is a double of `GameVM`'s hand-off, more permissive in one way (no timer hold, no queued events) and stricter in none; `GamesCheckFlowTest` pins that its draw and input sequence on `pass-hidden` equals `GameVM`'s (retro AI-4). No existing function is moved or rewritten, so no `git log -L` reading applies.

D3, committed negative tests, plus one-off evidence: the engine tests are committed (cheap, they guard the check itself and need no nested build); the epic's "scratch trees turn the target red" is also shown once on the real target by reconfiguring with the two cache variables pointed at scratch roots in the build directory (recorded in Verification, nothing committed). The red cases there are copies of `games/ultimate-tic-tac-toe/` edited in the scratch tree.

D4, the check runs only `ScriptVm`-evaluated rounds against what the installer wrote (`GameAssets`), not the source folder, so it plays what ships.

UTTT (R6, R8; first-party-games.md): `state = {c = <81-char string 0/1/2>, w = <9-char: 0 open, 1 or 2 won, 3 full>, n = <forced board 0..9, 0 any>, m = <moves>}` (about 120 B); `status` turn = `m % 2 + 1` or over; the winner is a line of won boards (`winners {mark}`); a draw when every line holds both marks or a full board; `apply({b, c})` rejects "Play in the highlighted board", "That cell is taken"; the next forced board is `c` when open, else 0. `input` maps a tap to `(b, c)` via `board.cell_at`; a tap on the `question` button (medium, top right) sets `ui.help`, and a tap while it is open closes it and makes no move; `rejected` events set `ui.message`. `draw`: header ("X to move" or the result), the `question` icon, `light` fill under playable boards, `x`/`circle` icons at 32 px in cells, a won board filled white with a 128 px mark, a full board `light`, then `board.draw_grid`; HOW TO PLAY replaces the board with the rules and the controls in `"small"` text. Fewer than 100 draw commands plus 81 icons, far inside `frame_commands_count` and `frame_icon_image_pixels`.

No firmware source changes: `pio run`, `pio check`, and the sibling-env builds cover files this entry does not touch (the host code is not in any env), so they are not run; `sim.sh build x4pro` is, for the screenshots.

## Verification

**Commands** (host tests and fast checks first; locks as AGENTS.md says):
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, including `-L games-check`.
- `ctest --test-dir build/test -L games-check --repeat until-fail:20` -- expected: pass (determinism).
- One-off red/green on the real target, scratch roots in `build/test/games_check_scratch/` (never committed): reconfigure with `-DGAMES_CHECK_GAMES_ROOT=... -DGAMES_CHECK_COMPANION_ROOT=...`, run `ctest -L games-check` for a copy of the game that fails to pack, raises a Lua error, grows a snapshot past 700 B, loses its `rounds/`, fails a scratch `checks.lua` (each red), and for scratch rounds over `fixtures/pass-hidden/` and `fixtures/pass-art/` (green); then reset the variables. Record each result.
- `grep -rniE 'tic-tac|ultimate|sudoku|battleship' test/game_script/harness scripts` -- expected: no match in harness code or `scripts/` (fixture names excepted).
- `for t in scripts/*_test.py; do python3 $t; done`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, second run changes nothing.
- After review and patches, once: `sim.sh build x4pro` (under the build lock, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`), then the screenshots below.
- The `games-check` job's commands once from a fresh tree of the commit (`git clone` the worktree into the scratchpad `8.1/fresh`, `git submodule update --init --recursive`; or the `git archive` recipe): configure, build the three targets, `ctest -L games-check`; the plan says which tree it was. Delete it afterwards.

**Manual checks:** simulator screenshots in `story-tracer-screenshots/`, each looked at: `installed.png` (Games list with the package installed from `fs_/games/`), `pass-round.png` (small boards the next move may use highlighted), `won-board.png` (a won small board), `how-to-play.png` (the page behind the `question` button), `over-menu.png` (the end-of-round menu over the final frame).

## Auto Run Result

Status: ready-for-dev (halted after planning, as the orchestrator asked). Nothing is implemented, reviewed, or committed; the plan file is the only new file and is left uncommitted. `lenses_ran` is empty, `deferred` is empty, `followup_review_recommended` is false.

**For the owner's checkpoint** (Design Notes C1, C2, C3 hold the full text; approve or amend, then record as Notes Decisions before entries 2 and 3 run):
1. C1, the rounds file: a Lua chunk `rounds/<name>.lua` returning `{mode, settings?, seed?, steps = {{seat, x, y, wait?, move?, shows?}...}, winners | unfinished}`, taps only, one `mode` per round (not a cross product of rounds and modes).
2. C2, `checks.lua`: returns a list of `{name, run}`; each `run()` is its own guarded call with a fresh 2,000,000-instruction budget; a guard fault stops that game's remaining checks.
3. C3, `board.lua`: `layout(w, h, opts?)`, `cell_rect`, `block_rect`, `cell_at`, `draw_grid`; 51 px cells on 474x788.
Builder's picks the ticket allowed (D1, D2): a `Session`/`MatchRounds` driver, not `GameVM`, because the check must draw every local seat after each step and `GameVM` never does; the hidden flow follows `GameVM::stepHandOff`, pinned by a test against `GameVM`. D3: the negative cases are committed engine tests over scratch trees the tests write, plus a one-off run of the real target against scratch roots.

**Files** (all new): `test/game_script/harness/{games_check.cmake, pack_games.py, games_check/*}`, `test/game_script/first_party/{README.md, ultimate-tic-tac-toe/...}`, `games/ultimate-tic-tac-toe/{manifest.json, main.lua, board.lua}`, a `games-check` job in `.github/workflows/crosshatch-ci.yml`, five screenshots under `story-tracer-screenshots/`. No upstream file, no `src/`, `lib/`, or API change; no ledger edit needed (all are Game paths).

**Review:** not run (planning only). **Verification:** none yet. Planning evidence: configuring `test/` in this worktree works; a scratch executable linking `game_installer_src`, `game_harness_src`, `game_harness_core`, and `Session.cpp`/`MatchLifecycle.cpp` and calling `GamePackageInstaller::installAll()` and `GameAssets::load` through a `MatchStore` built and linked cleanly (scratch files removed, tree clean apart from this plan).

**Residual risks:** (1) Size: about 25 new files and two deliverables; the plan marks `oversized`. Recommendation: let the orchestrator split group 1 (the check, proven over fixture scratch trees and a minimal round over `fixtures/pass-open/`) from group 2 (the game, its rounds, screenshots), as the ticket's own Notes allow; group 1 is a clean first commit. (2) The `GameVM` pin test links `game_match_src`, a separate executable, because mixing it with `game_installer_src` was not probed. (3) Round move lists (win by each seat, draw) come from the Python oracle in `tools/make_rounds.py`; a draw may need a playout search. (4) Real device font metrics differ from the harness's stand-in metrics, so the check does not prove text fits; the screenshots and the device run (entry 5) do.

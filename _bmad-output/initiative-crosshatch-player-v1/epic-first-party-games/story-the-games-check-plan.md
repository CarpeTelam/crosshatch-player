---
title: 'The games check'
type: 'feature'
ticket: '8'
created: '2026-10-04'
status: done
baseline_revision: 'd6aa0d3cf3e16a0315529baaba3ffe4b9d438aec'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/test/game_script/fixtures/README.md'
  - '{project-root}/docs/crosshatch/api-level-1.txt'
warnings: [oversized]
deferred:
  - summary: >-
      The games check and `ctest -L games-check` are documented only in `test/game_script/first_party/README.md` and a CI comment, not in AGENTS.md or `docs/contributing/`.
    evidence: |-
      A contributor adding a game under `games/` reads AGENTS.md first and finds no mention of the check, its label, or the rule that a game leaving the repository takes its companion folder with it. The fix edits AGENTS.md (an agent-context file, the owner's), so it is deferred, not patched.
    location: >-
      AGENTS.md, docs/contributing/
    severity: low
---

<intent-contract>

## Intent

**Problem:** No PR check packs, installs, or plays `games/<id>/`, so a broken first-party game first fails at release (epic Notes, pre-inception audit, 2026-10-04). Entries 1 to 3 (Ultimate tic-tac-toe, Battleship, Sudoku) build on this entry's interfaces: the rounds file, the companion folder's `checks.lua`, and the driver, and entry 1 is the first to put a game in `games/`.

**Approach:** A generic harness target `test/game_script/harness/games_check.cmake`, which names no game, finds every `<games root>/<id>/` at configure time, packs each with the real `pack_game.py`, installs it with the real installer, runs the game's `checks.lua` and every round of its companion folder `test/game_script/first_party/<id>/` headlessly over `Session` and `MatchRounds`, and fails on the epic's R10 list; the `games-check` job of `crosshatch-ci.yml` runs it. Its own tests use fixtures and scratch trees in the build directory, never `games/` (R2). `games/` does not exist yet, so the production target passes with it missing or empty.

## Boundaries & Constraints

**Always:** Every new file is under `test/game_script/`, is `.github/workflows/crosshatch-ci.yml` (changed), or is this plan; all are Game paths or fork-only, so no upstream file changes and `check_upstream_touches.py` stays green with no ledger edit. The harness and `scripts/` name no first-party game (`ultimate-tic-tac-toe`, `sudoku`, `battleship`); fixture names (`pass-open`, `pass-hidden`, `pass-art`) are fine. Engine tests build their scratch trees in the build directory and never write to `games/` or `test/game_script/first_party/`. The check runs what the installer wrote (`GameAssets`), not the source folder. Every snapshot stays at or under 700 B (R9: half of `GameCore::SNAPSHOT_BYTES`). Local variables in C++ stay under 256 B, buffers go on the heap, fallible allocation uses `makeUniqueNoThrow` or `new (std::nothrow)` (AGENTS.md; this code is the template for later entries; host-only code logs through gtest and `std::cerr`, never `Serial.print*`). Lua is 5.5 (`global` is reserved). A test double names the device behaviour it stands in for, in a comment, and a test pins that the two agree.

**Never:** Change `src/`, `lib/`, `docs/crosshatch/api-level-1.txt`, `API_SURFACE_CRC`, `platformio.ini`, the freeink-sdk pointer, `.skills/`, upstream's `ci.yml`, `test/CMakeLists.txt`, or any existing harness file (the new suite is one more `harness/*.cmake`, which the harness globs). Add `labeled` or another event to `crosshatch-ci.yml`. Commit scratch trees or generated files. Put a game, round, or copy of a fixture in `games/` or `first_party/`. Pass `games/` to another harness target. Add a game, its rounds, or the board module (entry 1's).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Green game | `<root>/<id>/` packs, installs, its checks and rounds pass | each `GamesCheckTest` test of the id passes; skipped declared modes are logged | none |
| No games | root missing or empty | production target passes (no per-id tests) | none |
| Pack failure | the packer exits non-zero for the folder | the id's package test fails with the packer's stderr | the other ids still run |
| Load or Lua fault | `setup`, `apply`, `status`, `input`, or `draw` raises, exceeds 2,000,000 instructions, or a frame passes `frame_commands_count` | the round fails naming round, step, and `errorMessage()` | the round stops; later rounds run |
| Snapshot over 700 B | any snapshot after `begin` or a step is larger | failure naming round, step, and the size (701 B red, 700 B green) | none |
| Wrong outcome | final status differs from the round's `winners` (or an `unfinished` round is over) | failure naming expected and actual winners | none |
| Rejected tap | step with `move = false` | `ver` unchanged and, if `shows` is set, the seat's next frame holds that text | a step that moves anyway, or lacks the text, fails |
| Moving tap | step with `move` unset | `ver` is exactly one more | a step that does not move fails |
| Wrong seat | step names a seat other than the one shown | failure (the device delivers input to the shown seat only) | none |
| No rounds / companion without game | `rounds/` missing or empty; `first_party/<id>/` with no `<root>/<id>/` | failure | none |
| Round for another mode | round `mode` not in the manifest, or not `solo`/`pass` | failure | none |
| Declared `nearby` | manifest lists `nearby` | logged as skipped, no failure | none |
| Round file malformed | unknown key, wrong type, empty `steps`, both or neither of `winners`/`unfinished`, `seed` not an integer, a setting id or value the manifest lacks | failure naming the key | none |
| Hidden manifest | `hidden` true, `pass` | the hidden flow plays (D2); every local seat is still drawn after each step | none |
| Settings | round `settings` | `ctx.settings` carries them, else the manifest's defaults | none |
| Seed | same seed twice / another seed | identical / different `math.random` draws in `setup` | none |
| `steps` a function | `steps(state)` | called once after `begin`, in the round's VM under its own 2 M budget, with the initial state decoded from the snapshot; returns the list | an error, a fault, or a non-list fails the round |
| `checks.lua` | present / absent | its list runs, each `run()` guarded with its own 2 M budget (the sum may exceed it); absent: none run | a check that raises fails by name and the rest run; a missing or empty list fails; a guard fault fails that check and stops the game's remaining checks, counted |

</intent-contract>

## Code Map

Read, never edit (all paths under `/home/user/epic-first-party-games-lane-a/`):
- `test/game_script/LuaGameFixture.h` -- `LuaGameTest::SessionGame`, the arena/frames/canvas/`HostPorts` wiring, `frontCommands()`: the model for the check's rig. The check cannot derive from it (gtest-bound); copy the wiring into `GamesCheckRig`.
- `lib/GameScript/LuaGame.{h,cpp}` (`load()` ~191-215, `enter()`, `loadEntry()`, `setupEntry`): `ScriptVm` mirrors them with public pieces (`ArenaAllocator::luaAlloc`, `openSandbox`, `openChLibrary`, `setBindingContext`, `BindingContext` fields, `CallGuard::install/arm/fault/message`, `Codec::decode`). Note `LuaGame::load` seeds `lua_newstate` from `ports.random.next32()`.
- `lib/GameScript/MatchRounds.h` (`begin`, `beginAgain`, `play(event, seat)`, `draw(seat)`, `start`, `step`); `lib/GameCore/SeatShown.h`, `MatchLifecycle.h`; `src/games/GameVM.cpp` 240-345 (`drawShown`, `showSeatNow`, `stepHandOff`): the hidden flow D2 follows.
- `lib/GameCore/Session.h` (`ver()`, `status()`, `snapshot()`, `draw`), `Roster.h` (`pass`, `passSeats`), `Manifest.h` (`ManifestReader`, `ManifestSettings`, `SettingValues`), `src/games/GameRegistry.h` (`readGame`), `GameAssets.h`, `MatchStore.h`, `GamePackageInstaller.h`, `GameHostCaps.h` (`gameHostCaps()`: pass on, nearby off), `GameCore/HostCaps.h`.
- `test/game_script/harness/packed_fixtures.cmake`, `pack_fixtures.py`, `PackedFixturesTest.cpp`, `InstallerSupport.h` -- the pack-then-install pattern, `HalDisplay display;`, `fakesd`, `gtest_discover_tests`. `installer.cmake`, `match.cmake`, `CMakeLists.txt` -- `game_installer_src`, `game_harness_src`, `game_harness_core`, `game_match_src`, the globbed `*.cmake` suites (HARNESS_DIR, REPO_ROOT). Linking `game_installer_src` with `game_harness_src` and `game_harness_core` in one executable links cleanly (probed at entry 1's planning).
- `test/game_script/fixtures/{pass-open,pass-hidden,pass-art,tracer}` -- engine scratch games; `harness/GameVmTest.cpp` 215-300 and `MatchSupport.h` -- the GameVM hidden-flow tests and rig the pin test reuses.
- `.github/workflows/crosshatch-ci.yml` (jobs, `crosshatch-test-status` `needs`, header comment) and upstream's `ci.yml` `unit-tests` job (the configure, cache, and build commands to mirror); `docs/crosshatch/orchestrated-epics.md` (fresh-clone recipe).

New, all under the worktree:
- `test/game_script/harness/games_check.cmake`, `harness/pack_games.py`, `harness/games_check/*` (core library, glue, tests), `test/game_script/first_party/README.md`.
- `.github/workflows/crosshatch-ci.yml` -- the `games-check` job, its `needs` line and header-comment line (the one changed existing file; fork-only).

## Tasks & Acceptance

**Execution** (in this order):

- [ ] `test/game_script/harness/games_check/ScriptVm.{h,cpp}` -- class over `lua_State`: `ScriptVm(arena, sources, ports, canvas, images)`, `load()` (state from `luaAlloc`, `openSandbox`, `openChLibrary`, binding context, installs a `CallGuard`), `run(chunkText, chunkName)` and `call(function ref, args)` running under `guard.arm` with the 2,000,000-instruction budget, reporting `Ok | Error(message) | Fault(message)`, and keeping a returned value alive in the registry; a helper pushes a codec-decoded snapshot as a Lua table. Extra sources are the companion folder's top-level `*.lua` modules (a name clash with a game module is a failure). Stands in for the device sandbox (`LuaGame::loadEntry`): pinned by `ScriptVmTest.cpp` (same snippets through `DirectGame` and `ScriptVm`: `os`, `load`, `package` are nil, `require` finds package modules, a loop faults at the budget, `ch.gfx` outside `draw` errors).
- [ ] `games_check/RoundFile.{h,cpp}` + `RoundFileTest.cpp` -- evaluates `rounds/<name>.lua` in a `ScriptVm` and converts the returned table into `Round` (C1); `steps` is either a list or a function the `Round` keeps with its VM. Unknown keys, wrong types, empty `steps`, both or neither of `winners`/`unfinished`, `seed` not an integer, a setting id or value the manifest lacks are errors naming the key.
- [ ] `games_check/RoundPlayer.{h,cpp}` -- plays one `Round` over `LuaGame`, `Session`, `MatchRounds` (D1): fresh `LuaGame` per round with `SeededRandom(seed)` as `HostPorts::random`, a `FakeClock` the steps advance, the device canvas 474x788, `setSettings`; solo uses `start`/`step`, open pass the same, hidden follows `stepHandOff` (D2); after `begin`, calls a function `steps` once with the decoded initial state; after `begin` and every step checks the snapshot size, draws every local seat (and seat 0 once over), reads each frame's text commands, and records failures. Option `drawEveryLocalSeat` (default true).
- [ ] `games_check/GameCheck.{h,cpp}` -- installer-bound glue: packed `.chgame` onto the fake card, `installAll()`, `GameRegistry::readGame`, `GameAssets::load` through a `MatchStore`, `ManifestReader` settings, then `runChecks` (C2) and `playRounds` (play `solo`/`pass` the manifest declares and `gameHostCaps()` can start; log each other declared mode as skipped), returning a `Report` of failures and notes. Parametric in the two roots.
- [ ] `games_check/SeededRandom.h` -- `GameCore::IRandom` over a splitmix32 stream from the round's seed; no process-wide state.
- [ ] `harness/pack_games.py` -- for each id runs `pack_game.py <root>/<id> <out>`, writes `<id>.hash` on success or `<id>.packerror` with stderr on failure, then a `packed.stamp`; exit 0 after every id was tried, 2 for usage; never fails the build for a game's fault (the test does). Beside `pack_fixtures.py`, same docstring style (it is in `test/game_script`, a Game path, so `fork-scripts.md`'s `scripts/` rules do not apply).
- [ ] `harness/games_check.cmake` -- cache variables `GAMES_CHECK_GAMES_ROOT` (default `games/`) and `GAMES_CHECK_COMPANION_ROOT` (default `test/game_script/first_party`), made absolute; ids = directories of each root, found with `CONFIGURE_DEPENDS` globs, so a missing root is an empty list; one custom command (inputs: every file of the games root, `pack_games.py`, `pack_game.py`, `fork_common.py`, `ApiLevel.h`, `names.txt`) and a `packed_games` target; library `games_check_core` (ScriptVm, RoundFile, RoundPlayer; links `game_harness_core`, `lua_vendored`); executables `GamesCheckTest` (production), `GamesCheckEngineTest` (self-tests), each compiling `Session.cpp`, `Roster.cpp`, and `MatchLifecycle.cpp` as the linker needs and linking `game_installer_src game_harness_src game_harness_core GTest::gtest_main`; `GamesCheckFlowTest` (the pin, D2) linking `game_match_src`, `games_check_core`, `GTest::gtest_main`. Definitions: roots, ids, `PACKED_GAMES_DIR`, scratch dir (in the build directory), `PACK_GAME_PY`, `PACK_GAMES_PY`, `PYTHON_EXECUTABLE`, `MATCH_FIXTURES_DIR`. `gtest_discover_tests(... PROPERTIES LABELS games-check)` for all three; `GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST` for an empty id list.
- [ ] `games_check/GamesCheckTest.cpp` -- per game id: `ThePackageInstallsAndLoads` (packerror absent, installer reports 1 installed, registry lists it `check.ok()`, the hash equals the packer's), `TheGamesOwnChecksPass`, `EveryRoundPlaysAsItsFileSays` (no rounds is a failure); per companion id: `HasAGame`.
- [ ] `games_check/GamesCheckEngineTest.cpp` -- the committed negative and positive cases of the I/O matrix over scratch trees the test writes into the build directory (copies and edits of `fixtures/pass-open`, `pass-hidden`, `pass-art`, a `math.random` scratch game, packed by the real packer through `std::system`): pack failure, Lua error, instruction-budget fault, frame-limit fault, snapshot 701 B red and 700 B green, wrong winners, no rounds, companion without game, undeclared-mode round, round for a mode other than `solo`/`pass`, malformed round file, `nearby` skipped, failing and passing `checks.lua`, a passing `checks.lua` whose checks together exceed 2 M instructions, a check that itself exceeds the budget, the hidden flow over `pass-hidden`, settings over `pass-art`, a round whose `steps` is a function of the decoded initial state, a `steps` function that raises or exceeds its budget, seeds (same seed same draws, different seed different), wrong seat, rejected and moving taps.
- [ ] `games_check/GamesCheckFlowTest.cpp` -- the pin (D2): over `pass-hidden`, the seats drawn and the input and apply lines of `RoundPlayer` with `drawEveryLocalSeat` off equal `GameVM`'s hidden flow's log lines for the same four taps.
- [ ] `test/game_script/first_party/README.md` -- the companion folder: layout (`first_party/<id>/{rounds/*.lua, checks.lua, top-level modules, tools/}`), the rounds format (C1), the `checks.lua` interface (C2), the cache variables, how to run `GamesCheckTest` and the engine tests, what the check proves and does not (text metrics), how a game leaves with its folder. Names no game.
- [ ] `.github/workflows/crosshatch-ci.yml` -- job `games-check` (checkout with submodules recursive, `apt-get install cmake ninja-build`, googletest cache as `ci.yml`, configure, `cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest`, `ctest --test-dir build/test -L games-check --output-on-failure`), added to `Crosshatch Test Status` `needs`, and one line in the header comment.

**Acceptance Criteria:**
- Given a scratch games root where a game fails to pack, raises a Lua error, exceeds a budget or frame limit, grows a snapshot past 700 B, loses its rounds, has a companion folder with no game, plays a round in an undeclared mode, or fails its `checks.lua`, when the check runs against it, then it fails; and it passes for scratch rounds over `fixtures/pass-hidden/` and `fixtures/pass-art/`, a declared `nearby`, and a function `steps`.
- Given `games/` missing or empty, when `ctest -L games-check` runs, then the production target passes.
- Given both cache variables at scratch roots in the build directory, when the real target is reconfigured and run, then it is red and green as the engine tests are (recorded, not committed).
- Given the finished tree, when `test/game_script/harness/` and `scripts/` are searched for the names of the three first-party games, then none names one (fixture names excepted).
- Given the PR, when CI runs, then `games-check` builds and runs the three executables and `Crosshatch Test Status` waits for it, and its commands pass once from a fresh tree of the commit.

## Implementation Notes

Built by one implementation subagent from this plan (the plan's route: full), then patched by the same subagent after the review. Choices the builder made inside the plan, none changing an outcome the Notes settle: `shows` matches when a text command contains the text; seat 0 is drawn once over in pass rounds only (the device never draws it in solo); the first failure stops its round and later rounds still run; `seed` is an integer 0 to 4294967295 (default 1, also `checks.lua`'s); a companion `.lua` file whose name is no module name is a failure; a declared mode the host cannot start (other than the `nearby`-style skip) fails; the engine tests pack through `pack_games.py` with `std::system`, the production target through the CMake custom command. After the review a step delivered once the round is over fails in every mode (the README says so). A folder name with a comma or semicolon would still break the id list CMake passes as a definition; not guarded (no game id has one). Test doubles: `ScriptVm` (pinned by `ScriptVmTest`), `RoundPlayer`'s hidden flow (pinned by `GamesCheckFlowTest`, two scenarios); both name what they stand in for in their headers, and the README says the stand-in text metrics do not prove text fits. No `src/`, `lib/`, `ci.yml`, ledger, `.skills/`, or submodule change; the repository has no `games/` folder yet.

## Plan Change Log

- 2026-10-04 (orchestrator, after the owner's checkpoint on entry 1): this plan started as a copy of entry 1's approved plan. The owner approved C1 (amended: `steps` may be a function), C2, C3, and D1 to D3 (epic Notes Decisions of 2026-10-04) and split the ticket: entry 8 is groups 1 and 3 (the games check, its engine tests, the `games-check` job, `first_party/README.md`); the game (Ultimate tic-tac-toe, the board module C3, its companion folder) and the screenshots stay with entry 1.
- 2026-10-04 (this run, planning): narrowed to entry 8. Removed group 2, the UTTT rows of the matrix and its Design Notes, C3 (entry 1's), the screenshots and `sim.sh build x4pro` (no game or screen changes here), and the `story-tracer-screenshots` task; added C1's amendment (a function `steps(state)`), the empty-games case, and entry 8's verify list. The `<intent-contract>` block was entry 1's text (it named the tracer game); it is rewritten to entry 8's `tickets.toml` description, as the split Decision directs, not preserved verbatim.

## Review Triage Log

### 2026-10-04 - Review pass
- verdicts: 32 findings - high 0, medium 8, low 19, false 4, maybe-false 1
- findings:
  - `[low]` `[patch]` (blind-hunter) `idsIn()`, `Report::add()`, and an unused `<sstream>` are dead code in `GameCheck.{h,cpp}`; `idsIn` duplicates the CMake id glob - verified by grep: no caller anywhere. Patch: delete them (shares a root cause with the verification-gap lens's "other findings" row).
  - `[false]` `[reject]` (blind-hunter) a nearby-only game can never pass because every game needs a round - the epic's R10 and Decision C1 say a game with no rounds fails and a round's mode is `solo` or `pass`; every game of the epic declares solo or pass (R1), so no game can be nearby-only.
  - `[low]` `[patch]` (blind-hunter) `shows` is satisfied by the check's own every-seat draw of a seat the device would not show - real (RoundPlayer `shows` scans every frame kept after the step); C1 words it as "that seat's next frame", which this reading follows. Patch: README states that the check also draws unshown seats and `shows` matches those.
  - `[low]` `[patch]` (blind-hunter) the round file's VM is seeded 1, so a `steps(state)` using `math.random` ignores `seed` - real (the VM exists before the seed is read); a `steps` should be a function of the state. Patch: README says so.
  - `[medium]` `[patch]` (blind-hunter) `readWinners` accepts any table (`{a = 1}`, `{1, 1}`), unlike `readSteps` - verified in RoundFile.cpp: only values are read, so a malformed winners table is read as winners. Patch: require positional keys and no duplicates, with RoundFileTest cases. Grouped with the edge-case lens's winners row.
  - `[medium]` `[patch]` (blind-hunter) the hidden-flow pin covers one four-tap scenario on `pass-hidden`, so `RoundPlayer.h`'s "stricter in none" is unpinned for a move that keeps the turn or a rejected tap - real (the plan's pin is for the four taps; the engine tests have no such round either). Patch: engine tests over a scratch hidden game with a keep-turn move and a rejected tap. Grouped with the verification-gap lens's hidden-flow row.
  - `[medium]` `[patch]` (blind-hunter) `wait` and the clock are untested - real: `advance(step.waitMs)` can be deleted with every test green. Patch: an engine test with a scratch game that logs `ch.time.ms()`. Grouped with the verification-gap lens's clock row.
  - `[low]` `[patch]` (blind-hunter) heap discipline in the new host code: `std::make_unique` in `RoundFileTest.cpp` and `GamesCheckFlowTest.cpp` where AGENTS.md says `makeUniqueNoThrow` - real; the "large stack objects" part is false (`MatchStore` and `GameAssets` hold a buffer pointer and two pointers). Patch: `makeUniqueNoThrow`.
  - `[low]` `[patch]` (blind-hunter) an id with a `.` or two ids differing by `-` and `_` abort gtest at registration; `pack_games.py` takes the last stdout line as the hash with no check and has no timeout; the `DEPENDS` list is fixed - the id part is real (`testName` maps only `-`; shares a root cause with the edge-case lens's id row). Patch: map every non-alphanumeric to `_` and append the index. The hash is already compared with the installer's (a wrong line is a red test, not a pass), the timeout and the fixed `DEPENDS` copy `pack_fixtures.py` and `packed_fixtures.cmake`: not worth more code.
  - `[low]` `[patch]` (blind-hunter) the job passes with zero tests if the label wiring breaks, and has no `-j` - real for the first (ctest exits 0 on "No tests were found"); `-j` is cosmetic. Patch: `--no-tests=error` (shares a root cause with the edge-case and verification-gap rows).
  - `[low]` `[defer]` (blind-hunter) the gate is documented only in the first_party README and a CI comment, not in AGENTS.md or `docs/contributing/` - the fix edits AGENTS.md; deferred (see `deferred`).
  - `[low]` `[reject]` (blind-hunter) engine tests hard-code `pass-open` coordinates and strings and a `15` length - fixtures are fork-owned and stable, a fixture change that breaks them is a loud red test; the `'` in a build path does not occur.
  - `[low]` `[reject]` (blind-hunter) `GamesCheckRig` copies `LuaGameFixture`'s wiring with no drift test - the canvas differs on purpose (474x788); a drift test adds machinery for a rig that `ScriptVmTest` already pins against `LuaGame` on the behaviours that matter.
  - `[low]` `[patch]` (blind-hunter) readability: `GameCheck.h` comment reflowed by clang-format into a broken list, and `std::error_code error` shadowed by `std::string error` in `playRounds` - real, direct corrections. Patch.
  - `[low]` `[patch]` (edge-case-hunter) a directory name with `.`, a space, or `-`/`_` twins aborts gtest at registration - same root cause as the blind lens's id row; patched there.
  - `[medium]` `[patch]` (edge-case-hunter) a solo round with steps after it is over: `seatShown` stays 1, so a `move = false` step passes and a moving one fails with "did not move" - verified in RoundPlayer `step()` (the seat check catches only pass rounds); the README says a step after the round is over is a failure. Patch: fail a step delivered once the round is over, naming it.
  - `[medium]` `[patch]` (edge-case-hunter) the README claim above versus the code - same root cause as the previous row.
  - `[false]` `[reject]` (edge-case-hunter) a solo round never draws seat 0 once over, against the plan's "(and seat 0 once over)" - the device never draws seat 0 in solo (`seatShown` returns the one local seat), so drawing it would test a frame no device shows; the README states pass rounds only.
  - `[low]` `[patch]` (edge-case-hunter) the CI ctest step lacks `--no-tests=error` - same root cause as the blind lens's CI row.
  - `[medium]` `[patch]` (edge-case-hunter) `winners` with non-sequence keys is accepted - same root cause as the blind lens's winners row.
  - `[low]` `[reject]` (edge-case-hunter) a `shows` string with an embedded NUL passes the length check but reads as empty - a Lua source with a NUL in a round file's string is not met in practice, and the fix adds a check for it.
  - `[low]` `[reject]` (edge-case-hunter) `pack_games.py` has no packer timeout - a hung local packer is not met in practice and CI's job timeout bounds it.
  - `[low]` `[patch]` (verification-gap) the `games-check` job can go green with zero tests - same root cause as the blind lens's CI row; patched there.
  - `[low]` `[reject]` (verification-gap) the production wiring (`games_check.cmake` to `GamesCheckTest`) is exercised by no committed test - the intent itself says the scratch-root reconfigure is "recorded, not committed", and the one-off run is recorded in Verification; the first game's PR exercises it in CI.
  - `[medium]` `[patch]` (verification-gap) `wait` and the clock are not observed by any test - same root cause as the blind lens's clock row.
  - `[medium]` `[patch]` (verification-gap) hidden-pass steps that do not pass the turn are covered by no test - same root cause as the blind lens's pin row.
  - `[maybe-false]` `[reject]` (verification-gap) the "host cannot run this package" branch of `checkPackage` has no negative test - settles on whether the packer admits a manifest that `Manifest::check(gameHostCaps())` refuses (unverified); if true it is low (a defensive branch), so rejected.
  - `[low]` `[patch]` (verification-gap, other findings) `idsIn` is unused - same root cause as the first row; patched there.
  - `[low]` `[reject]` (intent-alignment) the engine tests exercise the functions, not the CMake wiring, and the red/green of the real target lives in a one-off run - the intent's verify states that one-off run is "recorded, not committed"; same row as the verification-gap wiring one.
  - `[low]` `[reject]` (intent-alignment) the ctest mapping from `Report.ok()` to a red test (`expectGreen`) is not exercised in a red case - three lines of glue; the engine tests assert the `Report`s it reads.
  - `[false]` `[reject]` (intent-alignment) reading C (every declared mode needs a round) is not enforced - the epic Notes Decision C1 settles it: a declared mode with no round is no failure.
  - `[false]` `[reject]` (intent-alignment) `ScriptVm`, `SeededRandom`, and `GamesCheckFlowTest` go beyond the intent's list - they are the approved plan's design (C2, D1, D2) that the intent names as its starting design.
- fixes applied for the `patch` rows (same implementation subagent; host tests of the edited files, then the whole verification again): dead code and the reflowed comment and shadowed variable removed in `GameCheck.{h,cpp}`; README states the `shows` and `steps`-VM-seed limits; `readWinners` requires positional keys 1..n, in-range seats, and no duplicates, with six `RoundFileTest` cases; `RoundPlayer::step` fails a step delivered after the round is over, in every mode, with an engine test; engine tests for a hidden game that keeps the turn and takes a rejected tap and for `wait` moving `ch.time.ms`; `GamesCheckFlowTest` pins that hidden scenario against `GameVM` too; `makeUniqueNoThrow` in `RoundFileTest` and `GamesCheckFlowTest`; `testNameOf(id, index)` makes valid, unique test names; `--no-tests=error` on the CI ctest step.

### 2026-10-04 - Review pass (follow-up pass on the built plan)
- verdicts: 30 findings - high 0, medium 0, low 24, false 6, maybe-false 0
- findings:
  - `[low]` `[patch]` (blind-hunter) `shows` reads only the first frame drawn for the step's seat, while the README says "a frame" - verified in `RoundPlayer::shows` (it fails at the first frame of `step.seat` that lacks the text); the code comment and C1 ("that seat's next frame") agree with the code. Patch: the README says "the first frame". Shares a root cause with the edge-case lens's `shows` row.
  - `[false]` `[reject]` `carried` (blind-hunter) a declared mode with no round passes - first pass's reading-C row: epic Notes Decision C1 settles that a declared mode with no round is no failure.
  - `[low]` `[reject]` (blind-hunter) timer-driven games cannot be tested, `wait` fires no timer - C1 settles it (`wait` only moves `ch.time.ms`, no timer events) and the README lists it under what the check does not prove; adding a timer step is a new feature of the check for a game that does not exist yet.
  - `[low]` `[reject]` (blind-hunter) tap coordinates are not bounded to the canvas - verified (`readStep` takes any int16), but a mistyped tap with `move = true` fails loudly with "did not move", and a `move = false` typo is not met in practice; the fix adds a guard.
  - `[low]` `[patch]` (blind-hunter) `pack_games.py` has no test on its own (stale-output cleanup, stamp, exit 2, empty stdout, hash shape) - the cleanup is real and shares a root cause with the verification-gap lens's cleanup row (patched there); the hash shape and the missing timeout are the first pass's rows (the hash is compared with the installer's, a wrong line is a red test) and stay rejected; exit 2 and the stamp are glue the build exercises on every run.
  - `[false]` `[reject]` (blind-hunter) the fresh-clone gate is unrecorded - Verification, Results records the fresh-tree run (this lens read the diff without the plan).
  - `[low]` `[defer]` `carried` (blind-hunter) no CI doc lists the `games-check` job, and no `restore-keys` - the doc half is the first pass's deferred row (the gate documented only in the first_party README and a CI comment); the `restore-keys` half is cosmetic.
  - `[low]` `[reject]` (blind-hunter) two checks with one name give an ambiguous failure line - real but a check author names their checks; the failure also carries the run's text; the fix adds a validation branch.
  - `[low]` `[reject]` (blind-hunter) the hidden-flow pin drives 2-seat games only and compares only the draw, tap, apply, and over lines - `RoundPlayer` takes the next seat from `status.turn`, not arithmetic over the seat count, so a wrap-around has no code of its own to pin; the dropped behaviours (timer hold, queued events) are the stated looseness of D2.
  - `[low]` `[patch]` (blind-hunter) the error for `unfinished` says "must be true" though a boolean `false` is accepted (with `winners`) - verified in `RoundFile.cpp`. Patch: the text says "true or false".
  - `[low]` `[patch]` (blind-hunter) `GamesCheckRig.h` uses `new (std::nothrow)` without `#include <new>` - real (it works only through `<memory>`). Patch: the include.
  - `[low]` `[reject]` (blind-hunter) `readRound` pass 2 calls `lua_tostring` on keys pass 1 vouched for - a comment-level coupling in one function; no caller can diverge.
  - `[false]` `[reject]` (blind-hunter) `Round`'s defaulted move assignment moves `vm` before `stepsRef` - nothing move-assigns a live `Round` (grep: only the move construction in `loadRound`'s callers), and the old VM's closing frees its references.
  - `[low]` `[reject]` (edge-case-hunter) a `checks.lua` list with a hole or an extra non-sequence key leaves a check unrun - verified (`readCheckList` reads `lua_rawlen`); needs a hand-built malformed list, and the fix adds a key-counting guard.
  - `[low]` `[reject]` (edge-case-hunter) a check's `run()` that mutates the checks list changes what a later check calls - verified, but needs a check that edits the list the harness holds; the fix adds a reference scheme.
  - `[low]` `[reject]` (edge-case-hunter) a directory name with a space, comma, semicolon, or quote breaks the id list or the configure - the Implementation Notes record it as unguarded; no game id has one; the fix adds a name filter.
  - `[low]` `[reject]` (edge-case-hunter) an empty `GAMES_CHECK_GAMES_ROOT` resolves to the repository root - only a deliberate `-D...=` does that; the fix adds a guard.
  - `[low]` `[reject]` `carried` (edge-case-hunter) no packer timeout and no hash-shape check - the first pass's rows.
  - `[low]` `[reject]` (edge-case-hunter) a packer that exits 0 with no `.chgame` blames "no package" - the check still fails, only the wording differs.
  - `[low]` `[patch]` (edge-case-hunter) the README's `shows` wording against `RoundPlayer::shows` - same root cause as the blind lens's `shows` row; patched there.
  - `[low]` `[patch]` (verification-gap, pre-verified) `checkPackage`'s "unavailable on this host" failure has no test - patch: `AGameTheHostCannotStartFailsThePackageTest` (a pass-only game with `seats` 9 and 9, which the packer admits and `Manifest::check` marks `TooManySeats`); mutation (the check disabled) fails exactly it.
  - `[low]` `[patch]` (verification-gap, pre-verified) `pack_games.py`'s clean start has no test - patch: `APackOverAnOldFailureStartsCleanSoAFixedGameIsGreenAndItsPackErrorIsGone` packs a broken game, fixes it, packs the same folder again; mutation (the cleanup disabled) fails exactly it.
  - `[low]` `[patch]` (verification-gap, pre-verified) a solo round never drawing seat 0 is asserted nowhere - patch: `ASoloRoundDrawsOnlyItsOneSeatNeverSeatZeroEvenOnceOver` (the device never asks a solo game to draw seat 0); mutation (`localSeatCount() > 1` dropped) fails exactly it.
  - `[low]` `[reject]` (verification-gap, other findings) installer-reports-other-than-one and the `shows` "no frame drawn" branches have no test - the lens does not claim them as gaps; both are defensive branches of loud failures.
  - `[low]` `[reject]` `carried` (verification-gap, other findings) the production wiring is exercised by no committed test - the first pass's wiring row (the intent says the scratch-root run is "recorded, not committed").
  - `[low]` `[reject]` `carried` (intent-alignment) the expectations live at the target, the tests exercise the library - the first pass's wiring and `expectGreen` rows.
  - `[low]` `[reject]` (intent-alignment) no committed test configures a missing or empty root, and `HasAGame` red is tested through `companionHasGame` - the missing-or-empty run is the intent's recorded one-off; the wrapper is three lines of glue (first pass's `expectGreen` row).
  - `[false]` `[reject]` (intent-alignment) the rounds run in `ScriptVm` and the hidden flow in `RoundPlayer`, second implementations of the sandbox and `GameVM`'s hand-off - D1 and D2 choose them; each is pinned (`ScriptVmTest`, `GamesCheckFlowTest`) and disclosed in the README.
  - `[false]` `[reject]` (intent-alignment) the fresh-clone evidence is not in the diff - it is in Verification, Results (the diff excludes the plan).
  - `[false]` `[reject]` `carried` (intent-alignment) `GamesCheckFlowTest`, `checks.lua` fault-stop, and the companion-module clash go beyond the intent's text - the first pass's row: they are C1, C2, D1, and D2.
- fixes applied for the `patch` rows (by me, in my own context: this follow-up pass had no implementation subagent to re-engage; each is a one-line text change or a test): README `shows` wording; `unfinished` error text; `#include <new>`; three engine tests, each with its mutation shown red (the three guards disabled one at a time, each failing exactly its test, then restored).

## Design Notes

**Settled by the epic Notes, 2026-10-04** (the owner's entry-1 checkpoint Decisions, C1 as amended, C2, the driver and the negative tests; read them there, not restated):

C1 (rounds file), amended: `rounds/<name>.lua` is a chunk evaluated in the game's sandbox VM (`ScriptVm`; `ch.screen` is the 474x788 device canvas; `require` finds the game's modules and the companion folder's top-level `.lua` files); it returns `{mode, settings?, seed?, steps, winners | unfinished}`, each step `{seat, x, y, wait?, move?, shows?}`. `steps` is a list, or a function `steps(state)` called once after `begin`, in the same VM under its own 2 M budget, with the initial state decoded from the session's snapshot (`lib/GameScript/Codec.h`, `Codec::decode` into that VM), returning the list. So the `Round` keeps its VM alive until `steps` ran, and the round's evaluation and `steps` call are two guarded calls.
```lua
return { mode = "pass", settings = { level = "Easy" }, seed = 1,
  steps = function(state) return { { seat = 1, x = 120, y = 300 } } end,  -- or a plain list
  winners = { 1 } }   -- or unfinished = true
```
Mode is per round, not a cross product (a declared mode with no round is no failure); a declared mode the host cannot play (`nearby`) is logged as skipped. Taps, because `Session` takes a move only from `input`; `wait` only moves `ch.time.ms` (no timer events); `move = false` means `ver` unchanged; `shows` is text in `seat`'s next frame.

C2 (`checks.lua`): module `checks` in the same sandbox (`math.random` seeded 1, so a check calls `math.randomseed`); returns a non-empty list of `{name, run}`; loading is one guarded call and each `run()` its own with a fresh 2 M budget; a raised error fails that check and the rest run; a guard fault fails it and stops the game's remaining checks, counted; a missing or empty list fails; no `checks.lua`, none run.

D1 (driver at `Session`/`MatchRounds`, not `GameVM`): the check draws every local seat after each step (R10), which `GameVM` never does; a per-round seed needs only an `IRandom` (`LuaGame::load` and `openSandbox` take `HostPorts::random`), not the target stub `esp_random`. Real pieces: the packer, installer, registry, `GameAssets`, `LuaGame`, `Session`, `MatchRounds`, `seatShown`, `gameHostCaps()`. Deterministic (no threads, no clock), so no flake bar applies.

D2 (the hidden flow; spine AD-21; `GameVM::stepHandOff`): for a `hidden` pass manifest the driver draws nothing after `begin` (HandOff); shows the turn seat (`seatShown(Playing)`); delivers a step only to the shown seat (else the step fails); after a move with `!over && turn != seat` shows the mover (Result), then HandOff, then the new turn seat; once over it shows seat 0. Each guard it follows from `stepHandOff`: a seat other than the shown one gets no input (`madeUnderAnotherSeat`, HandOff reads none); a move that ends the round is RoundOver, never a turn change (`!status.over`); seat 0 is drawn when the status is over. It is a double of `GameVM`'s hand-off, more permissive in one way (no timer hold, no queued events, no late-timer drop) and stricter in none; `GamesCheckFlowTest` pins that its draw and input sequence on `pass-hidden` equals `GameVM`'s. No existing function is moved or rewritten, so no `git log -L` reading applies.

D3: the engine tests are committed (they guard the check itself, need no nested build); the "scratch trees turn the target red" is also shown once on the real target by reconfiguring with the two cache variables at scratch roots in the build directory (recorded in Verification, nothing committed).

D4: the check plays what the installer wrote (`GameAssets`), not the source folder, so it plays what ships.

**Test doubles** (epic-install-and-launcher retro AI-4): `ScriptVm` stands in for `LuaGame::loadEntry`'s sandbox (same `openSandbox`, `openChLibrary`, `CallGuard`; it differs by no `main.lua` game table, no state codec round trip, no `ui` tables) and `ScriptVmTest` pins the shared behaviours; `RoundPlayer`'s hidden flow stands in for `GameVM` (D2); the host canvas uses the harness's stand-in text metrics, so the check does not prove text fits the real font (the device run, entry 5, does). Each is stated in a comment and in the README.

**Decisions the builder makes within these** (none changes an observable outcome the Notes settle): the file layout inside `games_check/`; whether the check's `Report` is a struct of strings or gtest failures; whether the production tests read the packed output in `PACKED_GAMES_DIR` and the engine tests invoke `pack_games.py` at run time through `std::system` (the plan's reading: yes, same script both ways).

## Verification

**Commands** (host tests and fast checks first; locks as AGENTS.md says; `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache` exported):
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, including `-L games-check`, with `games/` missing.
- `ctest --test-dir build/test -L games-check --repeat until-fail:20` -- expected: pass (determinism).
- One-off red/green on the real target, scratch roots in `build/test/games_check_scratch/` (never committed): reconfigure with `-DGAMES_CHECK_GAMES_ROOT=... -DGAMES_CHECK_COMPANION_ROOT=...`, run `ctest -L games-check` for a copy of a fixture game that fails to pack, raises a Lua error, grows a snapshot past 700 B, loses its `rounds/`, fails a scratch `checks.lua` (each red), and for scratch rounds over `fixtures/pass-hidden/` and `fixtures/pass-art/` (green); reset the variables and confirm the production target passes again with `games/` missing, then with an empty `games/` (made and removed in the scratch run, never committed). Record each result.
- `grep -rniE 'tic-tac|ultimate|sudoku|battleship' test/game_script/harness scripts` -- expected: no match in harness code or `scripts/` (fixture names excepted).
- `for t in scripts/*_test.py; do python3 $t; done`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, second run changes nothing.
- The `games-check` job's commands once from a fresh tree of the commit (`git clone` the worktree into the scratchpad `8.8/fresh`, `git submodule update --init --recursive`; or the `git archive` recipe): configure, build the three targets, `ctest -L games-check`; the plan says which tree it was. Delete it afterwards.
- No firmware source changes: `pio run`, `pio check`, and `sim.sh build x4pro` are not run (the host code is in no PlatformIO env); the plan says so.

**Manual checks:** the `games-check` YAML reads as the `unit-tests` job's commands narrowed to the three targets and the label, and the job is in `crosshatch-test-status`'s `needs`.

**Results** (host tests and fast checks, run after the review's patches, under the host-test lock; host code is in no PlatformIO env, so `pio run`, `pio check`, and `sim.sh build` were not run):
- Full host suite with `games/` missing: 1,740 of 1,740 pass, 64 of them under `-L games-check`. `ctest -L games-check --repeat until-fail:20`: all 64 pass on each of the 20 repeats.
- One-off real-target runs over scratch roots in `build/test/games_check_scratch/manual/` (not committed; the script and log are in the scratchpad `8.8/oneoff.sh`, `oneoff2.log`): red for a game that fails to pack (3 of its 4 tests fail), raises a Lua error, grows a snapshot past 700 B, loses its `rounds/`, and fails a scratch `checks.lua` (1 of 4 fail each, the matching test); green for scratch rounds over `pass-hidden` and `pass-art` (8 of 8). After resetting both variables the production target passes with `games/` missing (64 of 64) and with an empty `games/` made and removed in the run (64 of 64).
- `grep -rniE 'tic-tac|ultimate|sudoku|battleship' test/game_script/harness scripts test/game_script/first_party`: no match. Every `scripts/*_test.py` passes; `scripts/check_upstream_touches.py` prints PASS with no ledger edit; `./bin/clang-format-fix` twice, the second changed nothing, and it changed no file outside this story's paths.
- Matrix audit: each row has a covering test that ran and passed (the engine, RoundFile, and ScriptVm tests listed by `ctest -L games-check -N`); the hidden-flow pin `HiddenFlowPinTest` passes for two scenarios. A mutation (dropping the mover's draw in the hidden flow) failed both the pin and its engine test, run by the implementer before the review.
- Fresh-tree gate, run from a real `git clone` of the worktree checked out at the story commit (before this amend, which adds only these result lines to the plan) with `git submodule update --init --recursive`: the `games-check` job's commands, configure with Ninja and Release, `cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest` (145 steps, exit 0), then `ctest -L games-check --no-tests=error`: 100% tests passed, 0 failed out of 64. The tree was deleted afterwards. The job itself has not run on GitHub; it runs on the epic PR.
- Follow-up review pass, after its patches (host-test lock; host code is in no PlatformIO env, so `pio run`, `pio check`, and `sim.sh build` were not run): full host suite 1,743 of 1,743 pass (67 under `-L games-check`); `ctest -L games-check --no-tests=error --repeat until-fail:20` ran all 20 repeats without a failure; every `scripts/*_test.py` passes; `scripts/check_upstream_touches.py` PASS with no ledger edit; the `grep` for first-party names over the harness, `scripts/`, and `first_party/` finds nothing; `./bin/clang-format-fix` twice, the second changed nothing, and it changed no file outside this story's paths. Each new test was shown red by disabling the guard it pins (the unavailable-on-host check in `GameCheck.cpp`, the clean start in `pack_games.py`, the solo seat-0 condition in `RoundPlayer.cpp`, one at a time, each failing exactly its test), then restored. The one-off real-target runs are not repeated: the patches touch no `games_check.cmake`, `pack_games.py`, or production test code.
- Fresh-tree gate for the follow-up commit's tests (run on the commit tree before this amend, which adds only this line to the plan): a `git archive` tree of it plus every submodule's archive, nested ones included (no git history; the gate reads none), the `games-check` job's commands: configure with Ninja and Release (exit 0), `cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest` (145 steps), `ctest -L games-check --no-tests=error`: 100% tests passed, 0 failed out of 67. The tree was deleted afterwards.

## Auto Run Result

Status: built. Branch `epic-first-party-games-lane-a`: the story commit `a2602884` and one follow-up commit (its hash is in the final report) from the follow-up review pass below.

**Summary:** the games check, generic and naming no game: `games_check.cmake` finds every `<games root>/<id>/` and companion `<id>/` at configure time (two cache variables), packs with the real `pack_game.py`, installs with the real installer, and `GamesCheckTest` runs each game's `checks.lua` (C2) and plays every round of its `rounds/` (C1 as amended, `steps` a list or `steps(state)`) over `Session` and `MatchRounds`, hidden flow included (D2), drawing every local seat after each step, failing on every R10 case. `GamesCheckEngineTest` (committed negative and positive cases over scratch trees in the build directory), `ScriptVmTest`, `RoundFileTest`, and `GamesCheckFlowTest` (the `GameVM` pin) guard the check itself; the `games-check` job runs the three executables and `Crosshatch Test Status` waits for it. `games/` does not exist yet, so the production target has no per-game tests and passes.

**Files** (all new unless noted; no upstream file, `src/`, `lib/`, ledger, `ci.yml`, `.skills/`, or submodule change):
- `test/game_script/harness/games_check.cmake` -- targets, cache variables, `packed_games`, labels.
- `test/game_script/harness/pack_games.py` -- packs each id, writes `<id>.hash` or `<id>.packerror`, then the stamp.
- `test/game_script/harness/games_check/ScriptVm.{h,cpp}` -- the sandbox VM for round files, `checks.lua`, and `steps(state)`, plus `OwnedVm`.
- `.../RoundFile.{h,cpp}` -- reads and validates `rounds/<name>.lua`.
- `.../RoundPlayer.{h,cpp}` -- plays one round over `LuaGame`, `Session`, `MatchRounds`.
- `.../GameCheck.{h,cpp}` -- installer-bound glue: package check, `checks.lua` runner, round player over the installed game, test names.
- `.../SeededRandom.h`, `GamesCheckRig.h`, `TestSupport.h` -- the seeded `IRandom`, the VM rig, scratch-game support.
- `.../GamesCheckTest.cpp`, `GamesCheckEngineTest.cpp`, `ScriptVmTest.cpp`, `RoundFileTest.cpp`, `GamesCheckFlowTest.cpp` -- the production suite and the check's own tests.
- `test/game_script/first_party/README.md` -- the companion folder, the rounds format, the `checks.lua` interface, how to run it.
- `.github/workflows/crosshatch-ci.yml` (changed, fork-only) -- the `games-check` job, its `needs` line, a header-comment bullet.
- This plan.

**Review:** four lenses ran (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), 32 findings: high 0, medium 8, low 19, false 4, maybe-false 1. Patched (grouped into 8 patches, 4 of them medium groups): dead code and two readability slips; README limits for `shows` and the `steps` VM seed; `winners` validation; a step after the round is over fails in every mode; engine tests for a hidden turn-keeping move plus rejected tap (also pinned against `GameVM`) and for `wait`; `makeUniqueNoThrow`; valid unique test names; `--no-tests=error`. Deferred (1): the gate is documented only in the first_party README and a CI comment, not in AGENTS.md or `docs/contributing/` (low; the fix edits AGENTS.md). Rejected with reasons in the Review Triage Log: a nearby-only game (false), solo seat-0 draw (false, the device never draws it), mode coverage and scope extras (false), the wiring-not-tested and `expectGreen` rows (the intent excludes committing the scratch-root run), a fixture-coupling row, the rig-drift row, NUL in `shows`, the packer timeout, and the "host cannot run this package" branch (maybe-false, low if true).

**Verification:** see Verification, Results.

**Follow-up review pass (2026-10-04, second pass on the built plan):** the four lenses ran again over the story's diff (30 findings: high 0, medium 0, low 24, false 6, maybe-false 0; the Review Triage Log has every row, six marked `carried` from the first pass). Patched, all low (8 rows, 4 root causes): the README says `shows` reads the first frame drawn for the seat; the `unfinished` error text says "true or false"; `#include <new>` in `GamesCheckRig.h`; and three new engine tests, each shown red with its guard disabled: a game the host marks unavailable fails the package test, a second pack over an old failure starts clean, and a solo round never draws seat 0. Deferred: none new (the first pass's one item stands). Rejected with reasons in the log: timers, tap bounds, duplicate check names, 3-seat hidden pin, malformed `checks.lua` lists, a `run()` that edits the list, odd folder names, an empty root variable, the wiring-not-tested rows (carried), and others. No `src/`, `lib/`, `ci.yml`, ledger, `.skills/`, or submodule file changed in the pass. No implementation subagent existed to re-engage in this pass, so I applied the patches myself (the patches are one-line text edits and three tests).

**followup_review_recommended: false.** This pass patched no high finding, so the work has converged on the workflow's rule; patch volume is not grounds.

**Residual risks:** (1) The stand-in text metrics mean the check cannot prove text fits the device's fonts; entry 5's device run does. (2) The production wiring (id globs, the `packed_games` command, the compile definitions) has no committed test over a non-empty root; the one-off run above covers it today and entry 1's first game exercises it in CI. (3) A game directory name with a comma or semicolon would break the CMake id list. (4) The `games-check` job has run only from a local fresh tree, not on GitHub, until the epic PR. (5) Measurements: none for memory, flash, or timing; the host check's cost is unmeasured on a device (the games-check suite takes about 3.6 s of process time on the host).

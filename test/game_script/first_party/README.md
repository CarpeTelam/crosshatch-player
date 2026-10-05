# First-party game companion folders

One folder per first-party game, `first_party/<id>/`, where `<id>` is the folder name of the game in `games/<id>/`. The
games check (`test/game_script/harness/games_check.cmake`) finds the games by looking at `games/` and the companions by
looking here, packs every game with the real `scripts/pack_game.py`, installs the package with the real installer, and plays
what the installer wrote headlessly: the game's `checks.lua` and every round of its `rounds/`. The check names no game, so a
game that appears in `games/` is checked, and one that leaves is not, with no edit to the harness. Fixtures for the check's
own tests live in `test/game_script/fixtures/` and never here or in `games/`.

## Layout

```
first_party/<id>/
  rounds/<name>.lua   one scripted playthrough each (the rounds format below); at least one is required
  checks.lua          optional: the game's own checks (the interface below)
  <module>.lua        optional top-level modules, which rounds and checks.lua may require
  tools/              anything else a game's maintainers keep beside it (generators, notes); the check never reads it
```

A top-level module is named `[a-z0-9_]{1,32}.lua`, as the game's own are, and may not have the name of one of the game's own
modules (a clash is a failure). `checks.lua` is such a module, named `checks`. A rounds file is not a module.

## Rounds

`rounds/<name>.lua` is a chunk run in the game's sandbox (`ScriptVm`: the same libraries, `ch`, and instruction budget a
game gets; `ch.screen` is a device canvas, the Sticky's 474 x 788 or the X4 Pro's 466 x 788: the check plays every round and
every check on both, and a game lays out one 466 x 788 box centred in either; `require` finds the game's modules and the
companion's top-level modules). It returns

```lua
return { mode = "pass", settings = { level = "Easy" }, seed = 1,
  steps = {
    { seat = 1, x = 120, y = 300 },
    { seat = 2, x = 120, y = 300, move = false, shows = "That square is taken" },
  },
  winners = { 1 } }      -- or: unfinished = true
```

| Key | Meaning |
| --- | --- |
| `mode` | `"solo"` or `"pass"`, one the manifest declares. A declared mode with no round is no failure. |
| `settings` | Optional: setting id to value, each one the manifest declares. `ctx.settings` carries them; a setting left out has the manifest's default. |
| `seed` | Optional integer 0 to 4294967295 (default 1): seeds `math.random` and the VM's string hash, so a round repeats and another seed differs. |
| `steps` | A non-empty list of taps, or a function `steps(state)` (below). |
| `winners` | The seats that win, as a list; `{}` is a draw. The round must be over after its last step with exactly these. |
| `unfinished` | `true` instead of `winners`: the round must not be over. Exactly one of `winners` and `unfinished`. |

A step is `{seat, x, y, wait?, move?, shows?}`, a tap in canvas pixels:

- `seat` is the seat the device shows (it reads input for that seat only), so a step for another seat is a failure. A solo
  round's seat is 1; an open pass round's is the turn seat; a hidden pass round follows the hand-off (below). A step after the
  round is over is a failure in every mode, whatever seat or `move` it names (the device reads no input once the round ends).
- `wait` milliseconds pass on the clock (`ch.time.ms`) before the tap. No timer event is ever delivered.
- `move = false` says the tap changes nothing: `ver` stays and, when `shows` is set, the seat's next frame holds that text. Left
  out, the tap must move: `ver` is exactly one more.
- `shows` is text that one of the text commands in the first frame the check drew for `seat` after the step contains. That includes
  the check's own every-local-seat draws (below), so in a hidden pass round it can be met by a frame of a seat the device would
  not show at that moment; it does not prove which frame the device shows next.

`steps` may be a function, called once after the round begins, in the round's VM with its own 2,000,000-instruction budget,
with the initial state (the first snapshot, decoded into a table) and returning the list. An error, a fault, or a result that
is not a valid list fails the round. The round file's own VM, the one that runs the file and `steps`, is seeded 1 whatever
`seed` says (`seed` is the played game's), so a `math.random` inside `steps(state)` always gives the same draws: `steps` must
be a function of the state alone.

Every key is checked, and a problem names the key: an unknown key, a wrong type, an empty `steps`, both or neither of
`winners` and `unfinished`, a `seed` that is not an integer, a mode the manifest does not declare (or `nearby`), a setting id or
value the manifest lacks.

A hidden pass game (`"hidden": true`) plays as the device plays it: nothing is drawn after the round begins (the hand-off
screen), the turn seat is shown, a step goes to the shown seat, after a move that passes the turn the mover's frame is shown,
then the hand-off, then the next seat, and once the round is over seat 0. The check also draws every seat the device plays after
the round begins and after every step (and seat 0 once a pass round is over), which the device never does, so a frame that
faults, passes the 2,048-command limit, or follows a state only another seat would show is found whichever seat is on screen.

After the round begins and after every step the snapshot must be at most 700 bytes (half the Session's 1,400, so a game keeps
room to grow).

## `checks.lua`

A module named `checks` in the same sandbox (`math.random` is seeded 1, so a check that needs other draws calls
`math.randomseed`). It returns a non-empty list of `{name = "...", run = function() ... end}`. Loading it is one guarded call and
each `run()` another, each with a fresh 2,000,000-instruction budget (the checks together may use more). A check passes by
returning and fails by raising; the failure names the check and the rest still run. A guard fault (the instruction budget, the
heap cap, a frame limit) fails that check and stops the game's remaining checks, which the failure counts. A missing or empty
list fails. No `checks.lua`: no check runs, and the log says so.

## Running it

The check is three executables, all labelled `games-check`:

```sh
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest
ctest --test-dir build/test -L games-check --output-on-failure
```

- `GamesCheckTest` is the check itself: three tests per game in `games/` (`ThePackageInstallsAndLoads`,
  `TheGamesOwnChecksPass`, `EveryRoundPlaysAsItsFileSays`) and one per companion folder (`HasAGame`: a companion with no game is a
  failure). With `games/` missing or empty it has no test and passes. Skipped declared modes (`nearby`, which this host cannot
  start) are logged, not failed.
- `GamesCheckEngineTest` tests the check over scratch trees it writes in the build folder: every way a game can fail it
  (does not pack, raises, passes a budget or the frame limit, grows a snapshot past 700 bytes, ends with other winners, has no
  rounds, loads a module inside a module's load) and the rounds it must pass.
- `GamesCheckFlowTest` pins the hidden flow of the player to `GameVM`'s.

Both roots are cache variables, for a one-off run over scratch trees (made absolute against the repository root):

```sh
cmake -S test -B build/test -DGAMES_CHECK_GAMES_ROOT=/scratch/games -DGAMES_CHECK_COMPANION_ROOT=/scratch/first_party
```

Reset them with `-UGAMES_CHECK_GAMES_ROOT -UGAMES_CHECK_COMPANION_ROOT` (or `-D` with the defaults, `games` and
`test/game_script/first_party`).

## Module loading

A module another module's own load requires needs the C stack twice over, and the device refuses a third level: the sandbox
will not parse a module with under 10 KiB of its 16 KiB VM stack free, and fails the game with `require 'x': script
recursion too deep to load a module` (`Sandbox.cpp`, `CallGuard::PARSE_HEADROOM_BYTES`). The simulator refused Sudoku's
`main > layout > board` that way while every host check passed, because this host sets no stack headroom. So the games
check carries the rule for every game, in `ThePackageInstallsAndLoads`: **while `main.lua` loads, at most main and one module
loading inside it** (`MAX_LOAD_NESTING` 2). A module another one needs is required from main first, or from a function body
(`setup`, `draw`, `input`), never by the other module's own load; every later `require` finds it loaded.

The probe (`probeLoadNesting` in `games_check/ScriptVm.cpp`) wraps `require` to count a name the first time it is required
(a cache hit loads nothing, so it never nests), runs `pcall(require, "main")`, and reports the deepest chain, as `main > a >
b`. Its result for the cases that matter:

| `main.lua` | Result |
| --- | --- |
| requires `a` and `b` | green |
| requires `b`, then `a`, and `a` requires `b` | green: `b` is already loaded when `a` loads |
| requires `a`, and `a` requires `b` (not loaded) | red, names `main > a > b`, says to require `b` from main first or from a function body |
| a function body requires `a`, and `a` requires `b` | green: **not probed**, the author's to keep shallow |
| raises while loading | no nesting finding: the rounds name the error (a chain read before it raised still counts) |

It is a double of the sandbox's refusal and says where it differs: stricter in one way (it counts depth, not bytes, so it
can fail a load the device takes) and more permissive in another (only main's own load is probed, so a module required later,
in a function body, is not). It is a depth rule because this host's frames are about a third of the simulator's, so no byte
margin is a usable bound here. `ScriptVmTest` pins it to the sandbox: at a modelled stack margin where a real `LuaGame` loads
the flat game, it refuses the nested one with the message above, and the probe flags exactly the nested game.

## Observing draw commands

A round sees only the text commands of a frame (`shows`), and `ch.gfx` outside the engine's draw call is an error, so a check
cannot call a game's `draw` on the real table. What a draw puts on the canvas beyond text (a highlight fill, an icon, a won
board's mark, which cells a frame fills, whether it asks for a full refresh) is read through a **recording `ch.gfx`**, swapped
in while a function runs. The rounds format does not carry it (that would change the format); a check, or a round's
`steps(state)`, calls `game.draw(state, seat, ui)` under the recorder and reads the commands.

Each game keeps its own recorder (`ultimate-tic-tac-toe/trace.lua`, `battleship/trace.lua`, Sudoku's `record` in `rules.lua`),
and each takes its function names from the real `ch.gfx`'s keys, so a misspelled call raises on the recorder too. `trace.record(f,
on_call)` runs `f`, returns the commands as text, one `name(args)` a line in drawing order, and their number, calls
`on_call(name, ...)` for each as it is drawn, and puts the real `ch.gfx` back, also when `f` raises (the error is raised again as
it was); Sudoku's `record(f, on_image, on_call)` returns the count only and hands the calls to its hooks. A check turns the
commands into whatever it pins: the boards a `light` fill covers, the icons and where they sit, the lines of a stroke, the
refreshes.

- *Secrecy for a hidden game.* Draw two states that differ only in what a seat must not see (the other seat's intact ships)
  and compare that seat's frames command by command; the failure names the first command that differs. Run it for every frame
  a seat can show (firing, waiting, placing) with a positive control (a difference a frame may show makes the comparison
  fail) and a least command count, so a comparison of empty frames cannot pass. Battleship's `draws.lua` does this.
- *Kinds and places.* Pin which commands may fall in a region (the target board holds grid lines, outlines, and shot icons,
  and nothing filled), which icons a frame may hold, and the order of fills (a highlight under the grid, a won board's white
  fill before its mark).
- *The double's limits.* The recorder is a double of the engine's `ch.gfx`. It counts each call as one command, as the engine
  does; a check pins that against a known count (UTTT: `board.draw_grid`'s documented 28 commands; Battleship: `draws.recorder`,
  a function with three calls, an unknown name that raises, and the real `ch.gfx` back after an error; Sudoku: `rules.frame`'s
  `draw_grid` count, and `rules.draw_marks` raising on a misspelled call). It is more permissive
  than the device: no argument is checked (a bad colour or size passes), nothing is clipped, no frame limit (2,048 commands)
  or icon and image budget applies, and an icon or image name is not looked up. The rounds, which draw the real `ch.gfx` in
  every frame, are what find those.

## The checks VM heap

The sandbox's Lua heap is 256 KB (`lua_heap_bytes`) and counts garbage as well as live data. The game's modules, `checks.lua`
(compiled, about 3.5 times its source), and everything it requires share it, and the allocator is first-fit in a region that
fills with small holes, so a heap that is nearly full fails with "not enough memory" before the cap. Two games are there:

- Sudoku's checks VM holds the solver, the counter, and the bank, with a headroom of 5 to 8 KB (6,500 B passed and 6,600 B
  failed on this tree: a global string of n bytes added before the last collection). Its rules, and what its draw marks on the
  board, are pinned in `rules.lua`, called from the rounds' `steps(state)`, where the VM has room.
- Sudoku's rounds VM is within a few KB of the heap too (about 170 KB live at the start of `steps`: the game's modules, the
  solver, and `rules.lua` compiled; `rules.frame`'s worst frame peaks near 227 KB of the 256 KB): `rules.lua` runs
  generational collection with a full one at the start of its heavy pins. Measured on separate processes: without them the
  Sudoku rounds fault "not enough memory" in 93 of 150 runs (150 of 150 under `setarch -R`); with them 0 in 400 processes and,
  in `ctest -R sudoku_1 --repeat until-fail:200`, 1 failure in about 1,600 plays of the rounds (a heap fault again, once as
  "stack overflow (string slice too long)", which is a stack that could not grow). Which process faults varies run to run, so
  the free space is fragmented by something address-dependent that was not pinned; two cheaper-looking changes made it
  worse (requiring the solver lazily: about 3% of plays; a full collection before every recorded draw: 17%), so do not add
  collections or move loads here without re-measuring with separate processes. The one-layout-on-every-canvas pins
  (`canvases.lua`, called from `rounds/canvases.lua`) load neither `rules.lua` nor `taps.lua` and keep a digest of a frame, not
  its commands. The real fix is a smaller live set (splitting `rules.lua`, which is most of it).
- Battleship's checks VM holds about 190 KB once `checks.lua` is compiled, and one frame's draw leaves about 47 KB of garbage,
  so the draw-level pins are in `draws.lua`, all called from one round, `rounds/draw-commands.lua`, whose `steps(state)` runs in a
  VM with the game's modules and none of the checks. UTTT's are in its `checks.lua`, which has room.

When a check faults "not enough memory", measure with `collectgarbage("count")` after a full collection at its start, and move
what draws frames (the heaviest thing a check does) into a module a round's `steps` function calls. A round's VM has the
game's modules and the round's own, and a 2,000,000-instruction budget for `steps(state)`: loop over a frame's commands once,
not once per cell.

## What the check proves, and what it does not

It proves that the package the packer makes installs and loads, that every round of every mode the host can start plays as its
file says over the same Lua sandbox, `Session`, and round loop the match uses, that no frame of any seat faults, that the
game's own checks pass, and that main's load nests no module loads deeper than the device takes (above). `ScriptVm` stands in for
the device sandbox (its load-nesting probe for the sandbox's parser-headroom refusal) and the hidden flow for `GameVM`'s hand-off;
each is pinned by a test (`ScriptVmTest`, `GamesCheckFlowTest`). A round sees text commands only; what a draw puts on the canvas beyond text is read by
a game's checks and rounds through its recording `ch.gfx` (above), which proves the commands and not how they look, so a
screenshot still shows the look. It does not prove that text fits: the host canvas measures with the harness's
stand-in text metrics, not the device's fonts, so only a run on the device shows that a line fits its box. It plays no timer
events, no swipes, no long presses, and no nearby match.

## When a game leaves

A game that leaves `games/` takes its folder with it: delete `first_party/<id>/` in the same change. A companion left behind
fails `HasAGame`.

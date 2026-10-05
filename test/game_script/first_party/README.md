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
  rounds/<name>.lua   one scripted playthrough each (the rounds format below); at least one is required, and every entry
                      of rounds/ must be a regular file whose name ends exactly `.lua`
  checks.lua          required: the game's own checks (the interface below)
  <module>.lua        optional top-level modules, which rounds and checks.lua may require
  tools/              anything else a game's maintainers keep beside it (generators, notes); the check never reads it
```

A top-level module is named `[a-z0-9_]{1,32}.lua`, as the game's own are, and may not have the name of one of the game's own
modules (a clash is a failure). `checks.lua` is such a module, named `checks`. A rounds file is not a module. A game with
no `checks.lua` fails `TheGamesOwnChecksPass` naming the file, and an entry of `rounds/` that is not a regular `.lua` file
(`x.LUA`, `x.lua.txt`, a directory) fails `EveryRoundPlaysAsItsFileSays` naming it; the valid rounds beside it still play.

## Rounds

`rounds/<name>.lua` is a chunk run in the game's sandbox (`ScriptVm`: the same libraries and `ch` a game gets, in a check VM
(below); `ch.screen` is the canvas under check, the Sticky's 474 x 788 or the X4 Pro's 466 x 788: the check plays every round
and every check on both, and a game lays out one 466 x 788 box centred in either; `require` finds the game's modules and the
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

- `x` and `y` are on the canvas under check: a tap that is negative, or at or over its width or height (x 466 to 473 exists
  only on the Sticky's 474 canvas), fails the step naming the tap and the canvas, as the device drops such a tap.
- `seat` is the seat the device shows (it reads input for that seat only), so a step for another seat is a failure. A solo
  round's seat is 1; an open pass round's is the turn seat; a hidden pass round follows the hand-off (below). A step after the
  round is over is a failure in every mode, whatever seat or `move` it names (the device reads no input once the round ends).
- `wait` milliseconds pass on the clock (`ch.time.ms`) before the tap. No timer event is ever delivered.
- `move = false` says the tap changes nothing: `ver` stays and, when `shows` is set, the seat's next frame holds that text. Left
  out, the tap must move: `ver` is exactly one more.
- `shows` is text that one of the text commands in the first frame the check drew for `seat` after the step contains. In a hidden
  pass round only frames the flow itself drew count, never the check's own every-local-seat draws (below): the mover's frame
  after a move that passes the turn, or the turn seat's after one that keeps it, and, for a step that ends the round, seat 0's
  frame, the end screen the device shows (no other seat is shown then). In an open or solo round the first frame drawn after
  the step is MatchRounds' own, but only hidden rounds read flow-only frames: in an open pass round the first frame drawn
  for `step.seat` can be the check's own every-seat sweep (for a step that ends the round, seat 0 is the flow's frame and the
  mover's own is the sweep's), which is out of scope here.

`steps` may be a function, called once after the round begins, in the round's check VM with its own instruction budget,
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

Every round also pays three more things, each pinned by a test of the check's own (`GamesCheckEngineTest`):

- *The end frame.* Once the round is over, the host draws its end-of-round dialog over the frame the device shows (seat 1 in
  solo, seat 0 in pass). On a 788-tall canvas no text of that frame may start inside the dialog's band, `host.dialog_top` up to
  `host.dialog_bottom` (259 up to 528, measured in `harness/games_check/HostBounds.h`), where the player would never see it;
  the check names the text and the band. Frames the check draws for other seats are not on the device and are not read.
- *The heap margin.* A round that plays clean at the device's Lua heap cap (256 KB) is played again with the cap 16 KiB lower
  (`HEAP_MARGIN_BYTES`), and a fault there fails the round naming the margin and the cap tried. The cap counts garbage and a
  first-fit region, so "this much room" is exactly "still plays with the cap this much lower"; the arena's own peak counts block
  headers that differ between this host and the device and is not read. Sudoku's `rounds/long-expert.lua` is the long round
  (HINT, CHECK, FILL NOTES, digit notes, many writes) the gate is aimed at; the played game's VM peaks at about 176 to 192 KB of its 256 KB at
  the first HINT.
- *Continue.* With `PlayOptions::restoreProbe` (the `EveryRoundRestoresFromItsSnapshot` test of each game, on both canvases),
  before each step and once after the last the current snapshot is restored into a new game in a second VM, started, and drawn
  for every local seat, as the device does on Continue (a new VM, `Session::restore`, every seat's `ui` empty). A fault or Lua
  error at `load`, `restore`, `start` or `draw` fails the round at that step. It proves restore, start and draw do not fault from a
  snapshot with an empty `ui`, not that the round's later taps mean the same there (they were written against the `ui` the live VM
  built), so the live VM plays on and the restored one is dropped.

After the round begins and after every step the snapshot must be at most 700 bytes (half the Session's 1,400, so a game keeps
room to grow).

## `checks.lua`

A module named `checks` in the same sandbox, in a check VM (`math.random` is seeded 1, so a check that needs other draws calls
`math.randomseed`). It returns a non-empty list of `{name = "...", run = function() ... end}`. Loading it is one guarded call and
each `run()` another, each with a fresh instruction budget (the checks together may use more). A check passes by returning and
fails by raising; the failure names the check and the rest still run. A guard fault (the instruction budget, the heap cap, a
frame limit) fails that check and stops the game's remaining checks, which the failure counts. A missing or empty list fails, and
so does no `checks.lua` at all: every game this check plays is a first-party game with its own checks.

A check VM has two globals a game's sandbox lacks: `host`, the host's numbers (`host.dialog_top`, `host.dialog_bottom`,
`host.banner_top`, `host.canvas_h`, in canvas pixels, from `HostBounds.h`) and `host.image_size(name) -> w, h` over the
installed images, and `within_device_budget(f, ...)`, which runs `f` and returns its results, and raises when it spent the
device's 2,000,000 instructions or more. A check that measures what a game call costs calls it through that: a check VM's own
budget is far above the device's.

## Running it

The check is three executables, all labelled `games-check`:

```sh
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest
ctest --test-dir build/test -L games-check --output-on-failure
```

- `GamesCheckTest` is the check itself: eight tests per game in `games/`, four on the Sticky's 474 x 788 canvas and the same four on
  the X4 Pro's 466 x 788 (`ThePackageInstallsAndLoads`, `TheGamesOwnChecksPass`, `EveryRoundPlaysAsItsFileSays`,
  `EveryRoundRestoresFromItsSnapshot`) and one per companion folder (`HasAGame`: a companion with no game is a failure). With
  `games/` missing or empty it has no test and passes. Skipped declared modes (`nearby`, which this host cannot start) are logged,
  not failed.
- `GamesCheckEngineTest` tests the check over scratch trees it writes in the build folder: every way a game can fail it
  (does not pack, raises, passes a budget or the frame limit, grows a snapshot past 700 bytes, ends with other winners, has no
  rounds or `checks.lua`, a mis-named round, a tap off the canvas, a text under the end-of-round dialog, a played game past the
  heap margin or faulting from a restored snapshot, loads a module inside a module's load) and the rounds it must pass, and
  `BoardInsetsTest`, which compiles the SDK's `BoardConfig.h` (through `harness/games_check/board_stubs/`, which stand in for
  the Arduino core) and pins the two canvases' bezel insets, and that both canvas origins have an even x + y, to the SDK's own
  profiles.
- `GamesCheckFlowTest` pins the hidden flow of the player to `GameVM`'s.
- `GamesCheckNoteImages` is a plain ctest, no executable: Sudoku's `tools/make_note_images.py --check`, which compares the 27
  committed note PNGs with what the tool generates (and `layout.lua`'s `NOTE_W` and `NOTE_H`). It runs in the same job, with no
  workflow edit, and exists while the tool does; `sudoku/checks.lua` pins the installed images' sizes through `host.image_size`.

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
b`. It runs in a VM with the device's heap cap and instruction budget, not a check VM's: it is `main.lua`'s own load, which
is the game's, and a load that faults there is no nesting finding. Its result for the cases that matter:

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

Each game keeps its own recorder (`ultimate-tic-tac-toe/trace.lua`, `battleship/trace.lua`, Sudoku's `record` in `drawn.lua`,
and the digest's own in `sudoku/canvases.lua`), and each takes its function names from the real `ch.gfx`'s keys, so a
misspelled call raises on the recorder too. `trace.record(f, on_call)` runs `f`, returns the commands as text, one `name(args)`
a line in drawing order, and their number, calls `on_call(name, ...)` for each call as it is made, and puts the real `ch.gfx`
back, also when `f` raises (the error is raised again as it was); Sudoku's `record(f, on_image, on_call)` returns the count only
and hands the calls to its hooks. A check turns the
commands into whatever it pins: the boards a `light` fill covers, the icons and where they sit, the lines of a stroke, the
refreshes.

- *Secrecy for a hidden game.* Draw two states that differ only in what a seat must not see (the other seat's intact ships)
  and compare that seat's frames command by command; the failure names the first command that differs. Run it for every frame
  a seat can show (firing, waiting, placing) with a positive control (a difference a frame may show makes the comparison
  fail) and a least command count, so a comparison of empty frames cannot pass. Battleship's `draws.lua` does this.
- *Kinds and places.* Pin which commands may fall in a region (the target board holds grid lines, outlines, and shot icons,
  and nothing filled), which icons a frame may hold, and the order of fills (a highlight under the grid, a won board's white
  fill before its mark).
- *The ink.* A frame's commands can be the right ones in the wrong ink, so each game pins the cheap invariants of every frame it
  draws: the first command is `clear("white")` and nothing clears again, the grid's lines (and a board's outlines) are black, and
  text is black except the inversions the game makes on purpose, which each pin names: Sudoku's focused-digit numerals (white on
  their black ground) and its NOTES button while NOTES is on (white label and icon on a black fill), Battleship's Ready button
  once the fleet is placed (white label and icon on a black fill) and the white cross inside a hit's black bar; Ultimate Tic Tac
  Toe inverts nothing.
- *The double's limits.* The recorder is a double of the engine's `ch.gfx`. It counts as the engine does (`ChBindings.cpp`): each
  call is one command, a `clear` included, except `refresh`, which asks for a refresh of the frame and appends no command, so it
  is neither counted nor in the text (`on_call` still sees it); a check pins that against a known count (UTTT: `board.draw_grid`'s
  documented 28 commands and a frame with a clear and a refresh in it; Battleship: `draws.recorder`, a function with three calls,
  a frame with a refresh, an unknown name that raises, and the real `ch.gfx` back after an error; Sudoku: `drawn.frame`'s
  `draw_grid` count and its calls against its commands, and `marks.draw_marks`'s refresh frame and a misspelled call). It is more permissive
  than the device: no argument is checked (a bad colour or size passes), nothing is clipped, no frame limit (2,048 commands)
  or icon and image budget applies, and an icon or image name is not looked up. The rounds, which draw the real `ch.gfx` in
  every frame, are what find those.

## The check VMs' limits

The device's sandbox has a 256 KB Lua heap (`lua_heap_bytes`, which counts garbage as well as live data) and a 2,000,000
instruction budget per call into Lua. The games check gives those, to the letter, to the VMs that run the game itself: the
played `LuaGame` (`VmLimits::device()`, in `RoundPlayer`) and the package-load probe (`checkPackage`). The VMs that run only the
check's own code get larger ones (`VmLimits::check()`, the constants in `harness/games_check/HostBounds.h`): the round file's VM
and its `steps(state)`, the `checks.lua` VM, and the module-clash probe get a 1 MB heap (`CHECK_LUA_HEAP_BYTES`, in a region 1.75
times that) and a 16,000,000 instruction budget (`CHECK_INSTRUCTION_BUDGET`), counted by a wrapper count hook in `ScriptVm` (the
lib's `CallGuard::INSTRUCTION_BUDGET` is a constant and `lib/` is not this check's to change). They share nothing with a game, so a
check that is heavier than a game may be does not sit a few KB or a few percent from a cliff: before this, Sudoku's checks VM
faulted at a heap cap 3.8 KB lower and its batches peaked at 1.7 to 1.8 M of 2 M instructions, and a round once faulted "not
enough memory" about 1 play in 1,600 (below).

A check VM is a double of the device's sandbox and says where it is more permissive: the larger heap and budget; the globals `host`
and `within_device_budget` (above), which the device does not have; and `CallGuard`'s memory hook is not installed in it, so the VM
watches the arena itself: any refused allocation (the cap or the region) since the call began makes the call a fault ("not
enough memory"), sticky through the count hook and checked again after the call, even if a check's own `pcall` caught the
error or Lua would have recovered by collecting and retrying (the cap is 4 times the device's, so a refusal means the check is
out of room). `ScriptVmTest` pins a device-limits `ScriptVm` to a real `LuaGame` snippet for snippet (the budget, the heap, the
sandbox's globals) and the check limits to their own constants. Anything that measures **the game's own cost** through a
check VM's guard keeps that measurement on `within_device_budget`, which counts the device's 2,000,000 on the same hook from a fresh
interval: Sudoku's four `COSTLY` calls (symmetry, HINT, CHECK, FILL NOTES of each band's costliest puzzle) are the only such
checks. The played game's heap is measured by the margin gate (above): the round plays again with the cap 16 KiB lower.

The split of Sudoku's checks and pins stays as it was built, and none of it is known dead: a check VM's room makes it
unnecessary for the heap, not harmful, and the batches (`CHUNKS`) still keep each batch under the device's budget, which is
the measure of what a game call may cost. What it was for, kept as the record:

- Sudoku's checks VM holds the solver, the counter, and the bank. Its rules, and what its draw marks on the board, are pinned in
  `rules.lua` and the modules beside it, called from the rounds' `steps(state)`: `pins.lua` (the shared helpers), `rules.lua`,
  `interaction.lua` (taps, the toggles' store, the reset), `drawn.lua` (`frame`, `look`, and the recorder), and `marks.lua`
  (`draw_marks`). The one-layout-on-every-canvas pins (`canvases.lua`, called from `rounds/canvases.lua`) load none of the pin
  modules nor `taps.lua` and keep a digest of a frame, not its commands.
- At the device's 256 KB a round faulted "not enough memory" about once in 1,600 plays, varying from process to process because
  Lua's compiler keys a chunk's `nil` constant by the constants table's own address (`nilK` in `lcode.c`) and a table hashes a
  pointer by its low 32 bits, so where ASLR put the arena changed when a table rehashed and so the order of a load's first
  allocations; the string-hash seed is not it, since the rig passes the round's own seed. A margin of a few KB cannot absorb that.
  With the check VM's heap it cannot happen; the played game and the probe, which keep the device's, have 64 KiB or more of room
  on every first-party round (measured: the worst, Sudoku's first HINT, passes with the cap 64 KiB lower and fails 80 KiB lower).
- Battleship's draw-level pins are in `draws.lua`, all called from one round, `rounds/draw-commands.lua`, whose `steps(state)`
  runs in a VM with the game's modules and none of the checks. UTTT's are in its `checks.lua`.

When a check faults "not enough memory" or an instruction budget at the check VM's own limits, measure with
`collectgarbage("count")` after a full collection at its start, and move what draws frames (the heaviest thing a check does) into a
module a round's `steps` function calls. Loop over a frame's commands once, not once per cell.

Two pins the games' own checks make from these bounds, and one the harness makes from the SDK: Sudoku's end screen lays its
four lines (the level, "Solved", the time, the best) out so that every text box ends above the host's end-of-round dialog
(`HostBounds.h` measures where it starts), with no two boxes overlapping; and `BoardInsetsTest` pins the typed bezel insets of
both canvases, and the even x + y of both canvas origins that `layout.lua`'s note-tile phase relies on, to the SDK's board
profiles.

## What the check proves, and what it does not

It proves that the package the packer makes installs and loads, that every round of every mode the host can start plays as its
file says over the same Lua sandbox, `Session`, and round loop the match uses, that no frame of any seat faults, that the
game's own checks pass, that main's load nests no module loads deeper than the device takes (above), that the played game keeps
16 KiB of its Lua heap under the device's cap, that it draws and starts from a restored snapshot with an empty `ui` as Continue
does, and that no end-screen text sits under the host's dialog. `ScriptVm` stands in for the device sandbox (its load-nesting
probe for the sandbox's parser-headroom refusal) and the hidden flow for `GameVM`'s hand-off; each is pinned by a test
(`ScriptVmTest`, `GamesCheckFlowTest`). The host's dialog and banner bounds are a double of its layout, measured once on the
simulator in English (`HostBounds.h`), and so is the end frame's text pin: it tests where a text starts, not the box. A round sees text commands only; what a draw puts on the canvas beyond text is read by
a game's checks and rounds through its recording `ch.gfx` (above), which proves the commands and not how they look, so a
screenshot still shows the look. It does not prove that text fits: the host canvas measures with the harness's
stand-in text metrics, not the device's fonts, so only a run on the device shows that a line fits its box. It plays no timer
events, no swipes, no long presses, and no nearby match.

## When a game leaves

A game that leaves `games/` takes its folder with it: delete `first_party/<id>/` in the same change. A companion left behind
fails `HasAGame`.

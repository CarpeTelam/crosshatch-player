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
game gets; `ch.screen` is the 474 x 788 device canvas; `require` finds the game's modules and the companion's top-level
modules). It returns

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
- `shows` is text that one of the text commands in a frame the check drew for `seat` after the step contains. That includes
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
  rounds) and the rounds it must pass.
- `GamesCheckFlowTest` pins the hidden flow of the player to `GameVM`'s.

Both roots are cache variables, for a one-off run over scratch trees (made absolute against the repository root):

```sh
cmake -S test -B build/test -DGAMES_CHECK_GAMES_ROOT=/scratch/games -DGAMES_CHECK_COMPANION_ROOT=/scratch/first_party
```

Reset them with `-UGAMES_CHECK_GAMES_ROOT -UGAMES_CHECK_COMPANION_ROOT` (or `-D` with the defaults, `games` and
`test/game_script/first_party`).

## What the check proves, and what it does not

It proves that the package the packer makes installs and loads, that every round of every mode the host can start plays as its
file says over the same Lua sandbox, `Session`, and round loop the match uses, that no frame of any seat faults, and that the
game's own checks pass. `ScriptVm` stands in for the device sandbox and the hidden flow for `GameVM`'s hand-off; each is pinned by
a test (`ScriptVmTest`, `GamesCheckFlowTest`). It does not prove that text fits: the host canvas measures with the harness's
stand-in text metrics, not the device's fonts, so only a run on the device shows that a line fits its box. It plays no timer
events, no swipes, no long presses, and no nearby match.

## When a game leaves

A game that leaves `games/` takes its folder with it: delete `first_party/<id>/` in the same change. A companion left behind
fails `HasAGame`.

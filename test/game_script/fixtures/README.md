# Game fixtures

Lua games and scripts the host suites (`test/game_script`) load, and that the simulator and an X4 Pro run by hand.
They live here, never in `games/`, which the release workflow packs.

## Placing a fixture on the SD card

A game folder (it has a `manifest.json`) is copied whole: `cp -r test/game_script/fixtures/tracer <sd>/.games/`
(`fs_/.games/` in the simulator). The folder name must equal the manifest's `id`.

A single fault script under `faults/` becomes a game of its own: make `<sd>/.games/f-<name>/`, copy the script there as
`main.lua`, and add this `manifest.json`, with `<name>`'s underscores written as hyphens in the id (ids allow only
`a-z`, `0-9`, and `-`):

```json
{"id": "f-<name>", "name": "Fault <name>", "version": "1.0.0", "api": 1, "seats": {"min": 1, "max": 1}, "modes": ["solo"]}
```

Open it from Home, Games. Every fault must end in the error view ("The game stopped with an error" with the text below
in small type), Back must return to Games, and the device must stay responsive.

## Games

| Folder | What it shows |
| --- | --- |
| `solo/` | The closing device run's game (Done-when 1): eight tap, long-press, and swipe prompts against a 60 s `ch.timer` countdown, with a checklist of the inputs seen this round and `ch.store`'s rounds finished and best score at the bottom. `screenshots/` holds its simulator frames; the loader ignores them. |
| `tracer/` | A move-driven round: the fifth tap below the banner ends it, the end-of-round menu opens, Play again starts a new round. |
| `counter/` | `ch.store`: the count survives Leave, reopening, sleep, and a restart. |
| `timer/` | `ch.timer`: three ticks 3 s apart with no input. |
| `gallery/` | Every drawing command and color; prints the last touch event. |
| `icons/` | Every library icon at 32, 64, and 128 px: black icons on white, and a tap turns the page to white icons on black. The medium row sits on a light band, which shows through around each icon's ink. |
| `loop/` | Runaway scripts, one band each (tap it); see below. |
| `limits/` | The codec, status, and display-list limits, one band each (tap it); see below. |

`surface/` and `modules/` are host-suite scripts, not games.

## Closing device run

Epic-script-runtime's closing run on an X4 Pro, in this order:

1. Place `solo/` and every fault: `loop/`, `limits/`, and each script under `faults/` as `f-<name>/` (above). Start
   from no `/.games-data/solo/`, so the round count starts at 0.
2. Play `solo` to game over from Home, Games (swipe inside the box: a right swipe from the left quarter is Back, an up
   swipe from the bottom is Home; the round also ends when the 60 s run out). Confirm each checklist box fills as you tap, hold, and swipe and after
   the first 5 s tick; "Last swipe" names each swipe's direction; the first frame of a round is a full refresh and the
   rest are fast; the end-of-round menu shows with "Over event received" below it and "Rounds finished 1" at the
   bottom. Choose Play again: a fresh round (Round 2, empty checklist, 60 s) with the store kept.
3. In that round: Back opens the pause menu, and Resume returns to the round; the Home gesture opens it too, and
   Leave returns to Games. Open `solo` again, put the device to sleep mid-round, and wake it: it sleeps and wakes
   without a hang (the forced exit), and Games opens `solo` again at Round 2.
4. Restart the device and open `solo`: it shows Round 2, "Rounds finished 1", and the first round's best. Play it to
   game over: "Rounds finished 2".
5. Open each fault in turn: each ends in the error view with the text in the tables above, Back returns to Games,
   and the device stays responsive.

This checks Done-when 1 (steps 2 and 4), 2 (step 5), and 4 (steps 2 and 3).

## Fault bands

`loop/` ("Runaway scripts"):

| Band | Error text |
| --- | --- |
| Loop forever | `main.lua:7: instruction budget exceeded` |
| Loop inside pcall | `main.lua:7: instruction budget exceeded` |
| Recurse through pcall | `script recursion too deep (C stack nearly full)` |
| Slow C calls forever | `It stopped responding: one step ran over 3 seconds` (after about 3 s) |
| Stuck in one C call | `It stopped responding: one step ran over 3 seconds` (after about 3.5 s; the VM is abandoned) |

`limits/` ("Limit faults"):

| Band | Error text |
| --- | --- |
| State over 1,400 bytes | `apply: state is too large (over 1400 bytes)` |
| Move over 256 bytes | `input: move is too large (over 256 bytes)` |
| ch.store over 4 KB | `main.lua:51: ch.store.set: the store is too large (over 4096 bytes)` |
| Invalid status | `status.turn is 2, not a seat in 1..1` |
| Frame over 2,048 commands | `main.lua:36: frame is full (at most 2048 drawing calls or 32768 bytes)` |
| Lua error in input | `main.lua:52: boom` |

## Fault scripts (`faults/`)

Each ends in the error view as soon as the game opens, except `loop_input`, which needs one tap on the canvas.

| Script | Where it fails | Error text |
| --- | --- | --- |
| `binary_chunk.lua` | setup | `main.lua:2: attempt to call a nil value (global 'load')` |
| `deep_parens.lua` | parsing `main.lua` | `C stack overflow` |
| `deep_pattern.lua` | setup | `main.lua:5: pattern too complex` |
| `gc_loop.lua` | setup | `main.lua:5: setmetatable: __gc metamethods are not supported` |
| `gc_recursive.lua` | setup | `main.lua:6: setmetatable: __gc metamethods are not supported` |
| `heap.lua` | setup | `not enough memory` |
| `io.lua` | setup | `main.lua:2: attempt to index a nil value (global 'io')` |
| `load.lua` | setup | `main.lua:2: attempt to call a nil value (global 'load')` |
| `loop_draw.lua` | the first draw | `main.lua:7: instruction budget exceeded` |
| `loop_in_pcall.lua` | the first draw, inside `pcall` | `main.lua:10: instruction budget exceeded` |
| `loop_in_xpcall_handler.lua` | setup | `main.lua:5: instruction budget exceeded` |
| `loop_input.lua` | input, on the first tap | `main.lua:8: instruction budget exceeded` |
| `loop_load.lua` | running `main.lua` | `main.lua:2: instruction budget exceeded` |
| `loop_setup.lua` | setup | `main.lua:2: instruction budget exceeded` |
| `lua_error.lua` | setup | `main.lua:2: boom` |
| `missing_require.lua` | running `main.lua` | `main.lua:2: module 'nothere' not found` |
| `nested_pcall.lua` | running `main.lua` | `script recursion too deep (C stack nearly full)` |
| `os.lua` | setup | `main.lua:2: attempt to index a nil value (global 'os')` |
| `recurse_in_xpcall_handler.lua` | setup | `main.lua:5: instruction budget exceeded` |
| `recursive_index.lua` | setup | `script recursion too deep (C stack nearly full)` in the simulator; on the device Lua's own `C stack overflow` may come first |
| `table_insert_len.lua` | setup | `main.lua:6: table.insert: more than 65536 elements` |
| `table_move.lua` | setup | `main.lua:3: table.move: more than 65536 elements` |
| `unknown_icon.lua` | the first draw, inside `pcall` | `main.lua:10: ch.gfx.icon: unknown icon "no_such_icon"` |

The texts are those of the x4pro simulator (entry 13's run). Line numbers are the scripts' own; the stack-depth faults
(`deep_parens`, `nested_pcall`, `recursive_index`, and `loop`'s recursion band) depend on frame sizes, so the device may
stop them with the other of the two stack messages.

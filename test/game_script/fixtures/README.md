# Game fixtures

Lua games and scripts the host suites (`test/game_script`) load, and that the simulator and an X4 Pro run by hand.
They live here, never in `games/`, which the release workflow packs.

## Placing a fixture on the SD card

Games install from packages: opening Games installs every `/games/*.chgame` (`/games/` on the card, `fs_/games/` in
the simulator), and lists only what was installed (`/.games/<id>/` holds a `.pkg`, which only the installer writes).
Copying a fixture folder into `/.games/` no longer lists it. Pack a game folder (it has a `manifest.json`; the folder
name must equal the manifest's `id`) with the release packer and drop the package in the inbox:

```sh
python3 scripts/pack_game.py test/game_script/fixtures/counter /tmp/packs   # writes /tmp/packs/counter.chgame
cp /tmp/packs/counter.chgame <sd>/games/                                    # fs_/games/ in the simulator
```

`pack_game.py` prints the package hash on its last line and refuses a folder the installer would refuse. A fixture
packs when it holds only `manifest.json`, `main.lua`, `<name>.lua` files, and `<name>.png` images: `counter/`,
`gallery/`, `icons/`, `limits/`, `loop/`, `pack-images/`, `pass-art/`, `pass-hidden/`, `pass-open/`, `slow-restart/`, `timer/`, `timing/`, and `tracer/` do. Three fixtures are **host-only**
and cannot be packed as they are: `images/` and `bad-image/` hold `.bmp` files (a package carries `.png`, which the
installer converts) and `solo/` holds a `screenshots/` folder (a package is flat). The host suites load those from
here directly.

A single fault script under `faults/` becomes a game of its own: make a folder `f-<name>/`, copy the script there as
`main.lua`, add this `manifest.json`, with `<name>`'s underscores written as hyphens in the id (ids allow only
`a-z`, `0-9`, and `-`), and pack the folder as above:

```json
{"id": "f-<name>", "name": "Fault <name>", "version": "1.0.0", "api": 1, "seats": {"min": 1, "max": 1}, "modes": ["solo"]}
```

Open it from Home, Games. Every fault must end in the error view ("The game stopped with an error" with the text below
in small type), Back must return to Games, and the device must stay responsive.

## Games

| Folder | What it shows |
| --- | --- |
| `solo/` (host-only) | The closing device run's game (Done-when 1): eight tap, long-press, and swipe prompts against a 60 s `ch.timer` countdown, with a checklist of the inputs seen this round and `ch.store`'s rounds finished and best score at the bottom. `screenshots/` holds its simulator frames; the loader ignores them. |
| `tracer/` | A move-driven round: the fifth tap below the banner ends it, the end-of-round menu opens, Play again starts a new round. |
| `slow-restart/` | The tracer's round (three taps) with a slow Play again: every later round's `setup` spins about 2 s on `ch.time.ms()`, so the end-of-round menu stays on screen meanwhile. A tap on the canvas in that gap is dropped: the new round starts at "taps: 0" with no square. Play again, then Back and Resume within the gap: the pause menu stays on screen (inert) until round 2's first frame, never the round-1 board. |
| `counter/` | `ch.store`: the count survives Leave, reopening, sleep, and a restart. |
| `pass-open/` | Pass and Play's open match (epic-pass-and-play entry 1): noughts and crosses on a 3 x 3 board, solo or pass. From Games, `pass-open`, Pass: seat 1 (X) sees "Player 1 (X) to move", a tap on a free square places its mark and seat 2's board shows "Player 2 (O) to move"; a tap on a taken square says "That square is taken" to that seat only. A line of three or a full board ends the round: each seat's `input` gets `over` once (logged `over for seat S`; its answer, a move on the centre square, is discarded), and the end-of-round menu sits over the "Everyone: Player N wins" (or "Everyone: a draw") frame, seat 0's. A 10 s `ch.timer` nudges the seat whose turn it is (logged `timer for seat S`); each tap is logged `tap for seat S`, and one after the round's end goes to seat 0. Solo places X and O in turn from seat 1. A pass match writes `resume.bin` as solo does (epic-pass-and-play entry 9), so the title screen offers Continue after Leave. |
| `pass-hidden/` | Pass and Play's hidden hand-off (epic-pass-and-play entries 4 and 12): a two-seat, pass-only game whose manifest says `hidden`, so no seat sees another's frame. It ships no `icon.png`, `handoff.png`, or `title.png`, so it shows the runtime's defaults. From Games, `pass-hidden`, New game: the hand-off screen (a full refresh): the game's icon (`game-controller`, as on its launcher row) at 128 px in the upper half, "Player 1's turn" under it, and the "I'm ready" button in the middle; only that button, or Confirm, shows seat 1's frame ("Player 1", "Player 1's secret: apple", "Moves: 0"), and a tap elsewhere does nothing. A tap on the frame is a move: seat 1's frame stays, with the banner "Tap to pass to player 2" at the bottom; only a tap on the banner, or Confirm, shows the hand-off screen again ("Player 2's turn"), and its button shows seat 2's frame ("Player 2's secret: river"). The round ends after four moves with no winner: the end-of-round menu sits over "Everyone: the secrets were apple and river" (seat 0's frame), and Play again starts on the hand-off screen ("Player 1's turn"). Back or Home in the banner or on the hand-off screen pauses, and Resume returns there; the pause menu from the hand-off screen sits on no frame. A 5 s `ch.timer`, re-armed by each tap, falls due on its own: one that does during a hand-off is held and reaches the next seat after its first frame (logged `timer for seat S`). Each call is logged with its seat (`tap for seat S`, `apply seat S`, `draw for seat S`, `over for seat S`). Only the timer depends on device timing (whether a hand-off takes more than 5 s), so a device run with no `timer for seat` line is no failure; the rest needs no calibration. A pass match writes `resume.bin` as solo does (epic-pass-and-play entry 9), so the title screen offers Continue after Leave, and Continue resumes on the hand-off screen naming the saved turn seat. |
| `pass-art/` | What a developer controls (epic-pass-and-play entry 12): `title.png` (480 x 480, two rings and a cross in a frame), `handoff.png` (480 x 800, a ring and a checkerboard in a frame), `default_mode` `pass`, the library icon `spade` (fill), and two settings: Level (`Easy`, `Hard`; default `Hard`) and Board (`Small`, `Medium`, `Large`; no default, so `Small`). A hidden game that also plays solo, seats 1 to 2. `setup` reads `ctx.settings` and every frame prints the mode and both values ("Mode: pass", "Level: Hard", "Board: Small"), so the screen shows what the match was started with. Each seat has a secret only its own frame shows (seat 1 `lantern`, seat 2 `harbour`); a tap is a move, and the round ends after four moves with no winner. The log lines name the seat each call was for (`setup <mode> level <v> board <v>`, `tap for seat S`, `apply seat S`, `draw for seat S`, `over for seat S`). The device run below uses it. |
| `changed/counter/` | `counter/` at version 1.0.1 with the title "Counter v2": the same id and a different package hash, for installing over `counter` on a device (its save is discarded, its `ch.store` kept). `changed/` only holds such variants, named by the game id they change because the packer needs the folder name to equal the manifest's `id`; pack one with `python3 scripts/pack_game.py test/game_script/fixtures/changed/counter <out>` into an `<out>` other than `counter`'s, or it overwrites `counter.chgame`. `scripts/pack_device_run.py` packs it as `counter-changed.chgame` (see Device-run packages below). |
| `timer/` | `ch.timer`: three ticks 3 s apart with no input. |
| `gallery/` | Every drawing command and color; prints the last touch event. |
| `icons/` | Every library icon at 32, 64, and 128 px in both weights, up to three icons of one category a page (`docs/crosshatch/game-icons.md`'s order; the title shows the ink and page `n/N`, the heading the category, part, and "regular, fill"). The small and medium rows show each name regular then fill; the large area shows the regular icons above the fill ones, each name under its fill icon. Each tap turns to the next page: the 21 black pages (black icons on white) first, then the 21 white pages (white icons on black), then back to the first. The medium row sits on a light band, which shows through around each icon's ink. |
| `images/` (host-only) | `ch.gfx.image`: the game's own `badge.bmp` (100 x 60) and `dot.bmp` (37 x 37) at their own size, in black on one light band and in white on the next; each covers the band whole (opaque), and a last badge is clipped at the right edge. `icon.bmp` is the launcher's and is not loaded as an image. |
| `bad-image/` (host-only) | `broken.bmp` claims 8 bits per pixel: the game does not start, and the load-failure view says "An image is damaged or too large". |
| `pack-images/` | The packable twin of `images/`: `icon.png` (96 x 96, the launcher's row icon), `badge.png` (100 x 60), and `dot.png` (37 x 37), drawn through `ch.gfx.image`. The host build packs it, `counter/`, and `timing/` with `scripts/pack_game.py` and installs them with the real installer (`PackedFixturesTest`). |
| `loop/` | Runaway scripts, one band each (tap it); see below. |
| `limits/` | The codec, status, and display-list limits, one band each (tap it); see below. |
| `timing/` | Three frames at the top of what a frame may ask of the replay, one band each (tap it; tap the frame to go back): the game's own mid-gray `gray.png` (480 x 800), a frame at exactly 1,048,576 icon and image pixels (the whole `frame_icon_image_pixels` budget), and 2,048 filled rects that each cover the whole canvas (the whole command limit); see Timing run below. |

`surface/` and `modules/` are host-suite scripts, not games.

## Closing device run

Epic-script-runtime's closing run on an X4 Pro, in this order:

1. Place `solo/` and every fault: `loop/`, `limits/`, and each script under `faults/` as `f-<name>/` (above; `solo/`
   is packed from a copy without its `screenshots/` folder). Start from no `/.games-data/solo/`, so the round count
   starts at 0.
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

## Timing run

`timing/` is the device run of `_bmad-output/implementation-artifacts/deferred-work.md`'s `## e3r-1` (the replay of a
frame at the icon and image budget, under `RenderLock`, has been timed only on the host) and of the 2,048 full-canvas
fills that entry left unbounded. It packs as above (`gray.png` becomes a 1-bit dithered `gray.bmp` at install:
about half the pixels white, runs of one or two pixels, the worst case for the replay's fills). Run it on an X4 Pro:

0. Note the firmware commit the device runs (`git rev-parse HEAD` of the build, or the version line the device shows in
   Settings) and write it beside the results: a timing means nothing without the build it came from. Start the serial
   log before opening the game and keep it for the whole run: `pio device monitor -e x4pro` (115200 baud; add
   `--filter log2file` or redirect with `| tee timing.log`), or any terminal on the board's USB serial port. It carries
   the `band 2 charges ...` line, a watchdog or reset banner if there is one, and the `GAME` line `VM stopped; arena
   peak ...` that a normal Leave prints.
1. Install `timing.chgame`, open it from Home, Games. The menu is a cheap frame: the baseline.
2. For each band in turn, tap it and record the time from the tap to the finished picture (a 60 fps phone video of the
   screen, counted in frames, is enough; the serial log has no replay time), whether the device stays responsive
   during it (the touch panel and the buttons answer once it is drawn), and the serial log for a watchdog or reset
   line. Then tap the frame to go back and record the time to the menu again.
3. Band 2's serial log line is `band 2 charges 1048576 pixels`. A refusal instead (`the frame's icons and images cover
   over 1048576 pixels`) or a `frame is full` error is a finding: report it, do not change the limit.

| Band | Frame | Commands |
| --- | --- | --- |
| 1 | `gray.png` once, at the canvas's top-left (373,512 canvas pixels on the 474 x 788 canvas) | 1 |
| 2 | Two gray images, eighteen 128 px `circle` fill icons in white, and 15 one-row strips of the image at the bottom edge, 1,048,576 pixels in all on the 474 x 788 canvas (worked out from `ch.screen` on any other) | 36 (with the clear) |
| 3 | 2,048 filled rects of the whole canvas, in `light`, `black`, `white`, `dark` in turn, the last `dark`; the screen ends dark gray | 2,048 |

The simulator's frames for the three bands are in
`_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/story-timing-screenshots/`; their few
milliseconds of replay say nothing about the device.

What to record for entry 14: the firmware commit, each band's tap-to-picture time and the menu's, a reset or watchdog line if any, the `VM stopped` line after Leave, and
whether a Back press during a band's replay is answered once the picture is drawn. `GfxBindingsTest`'s
`TheTimingFixturesBandsSitAtTheLimitsOnEveryCanvas` pins the frames on the host, and `GameHashTest` and the installer suite
install the package.

`GameHash`'s mbedTLS branch (the device build) has never run against `package_vectors.json`: the host tests use OpenSSL.
While the installer is on the device, also install `test/game_core/package_vector.chgame` and read
`/.games/package-vector/.pkg` back: it must read `v1` on one line and `0530a15766e91bf1` on the next.

## Device-run packages

The packages entry 14 puts on a device are never committed. `python3 scripts/pack_device_run.py <dir>` packs them:
`counter`, `loop`, `timing`, and `pack-images` from here, `changed/counter` as `counter-changed`, `pass-open`, `pass-hidden`, `pass-art`, the hardening
generator's `binary-lua-stored` case as `invalid-binary-lua.chgame` (it must install as `.bad`), and
`test/game_core/package_vector.chgame`, with a `HASHES.txt` of the hash `pack_game.py` printed for each. On a pull
request, add the label `package-games`: the `Game packages` job of `.github/workflows/crosshatch-game-packages.yml` runs the
script and uploads `<dir>` as the artifact `game-packages` (kept 30 days). The job is not a required check and runs
only with the label; adding another label does nothing there and reruns nothing in `crosshatch-ci.yml`.

## Title screen, Options, and hand-off run

Epic-pass-and-play's device run (entry 11) checks entry 12's screens on an X4 Pro with `pass-art.chgame`,
`pass-hidden.chgame`, and `counter.chgame` from the packet, and `slow-restart` (packed by hand, above), starting from
no `/.games-data/pass-art/`. Each step is done once by touch and once with the front buttons alone (Up,
Down, Confirm, Back); a step that names a tap names the button press too.

1. Games: `pass-art`'s row reads "Solo · Pass and play", `pass-hidden`'s "Pass and play", `counter`'s "Solo".
2. Open `pass-art`: the header names "Pass art"; the 480 x 480 band under it shows `title.png` (the frame, the two
   rings, the cross), centred; the rows are New game, "Pass and play · Hard · Small" (selected), and Options. Open
   `counter`: its icon at 128 px centred in the band, Continue only with a save, New game "Solo", and no Options.
3. In `pass-art`, Options: Mode "Pass and play", Level "Hard", Board "Small". A tap (or Confirm) on each cycles it:
   Mode to "Solo" and back, Level to "Easy", Board to "Medium", "Large", and back to "Small". Leave Level "Easy" and
   Board "Large", then Back: the title screen's Options row is selected and New game reads "Pass and play · Easy ·
   Large". Back to Games and open `pass-art` again: the same line (the choices are remembered).
4. New game: the hand-off screen shows `handoff.png` (the frame, the ring, the checkerboard), with only the "I'm
   ready" button over it in the middle and no "Player N's turn". A tap on the page outside the button does nothing.
   The button (or Confirm) shows seat 1's frame with a full refresh: "Mode: pass", "Level: Easy", "Board: Large".
5. Tap the frame: seat 1's frame stays, with "Tap to pass to player 2" in a framed banner at the bottom. A tap above
   the banner does nothing; a tap on it shows the hand-off page again. Tap the banner twice quickly: the second tap
   does not press "I'm ready" (they are far apart).
6. Press "I'm ready": seat 2's frame. On it, put the device to sleep, once with each Sleep Screen mode (Settings, Display, Sleep Screen:
   Dark, Light, Custom, Cover, Cover + Custom, None, Quick Resume, Transparent): the screen first goes plain white (no
   icon, no text, not the page), then shows that sleep screen; the transparent and Quick Resume modes draw over white,
   never over the seat's frame. Wake: Home. Games, `pass-art`, Continue resumes on the hand-off page.
7. Play `pass-art` to the end of the round and choose Play again: the hand-off page again, and seat 1's new frame
   still reads "Level: Easy", "Board: Large" (Play again keeps the settings).
8. Open `pass-hidden`, New game: the default hand-off screen, the `game-controller` icon at 128 px in the upper half,
   "Player 1's turn" under it, and "I'm ready" in the middle; after seat 1's move and the banner, "Player 2's turn".
   Sleep on that screen: plain white first, as in step 6.
9. In `slow-restart`, play to the end of the round, choose Play again, then Back at once: the pause menu says
   "Starting the next round", centred under "Paused".
10. In `pass-art`, set Options' Mode to "Solo" and Back: New game reads "Solo · Easy · Large"; with the new round's
    save of step 7 on the card (Leave it from its pause menu), New game asks "Start a new game?", "Solo", "This replaces the saved game.", Cancel focused.

## Fault bands

`loop/` ("Runaway scripts"):

| Band | Error text |
| --- | --- |
| Loop forever | `main.lua:7: instruction budget exceeded` |
| Loop inside pcall | `main.lua:7: instruction budget exceeded` |
| Recurse through pcall | `script recursion too deep (C stack nearly full)` |
| Slow C calls forever | `It stopped responding: one step ran over 3 seconds` (after about 3 s; the watchdog cancels the VM, and the log reads `VM stopped; ...`, not `abandoning`) |
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
| `icon_image_pixels.lua` | the first draw, inside `pcall` | `main.lua:11: ch.gfx.icon: the frame's icons and images cover over 1048576 pixels` |
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
| `unknown_image.lua` | the first draw, inside `pcall` | `main.lua:10: ch.gfx.image: unknown image "no_such_image"` |

The texts are those of the x4pro simulator (entry 13's run). Line numbers are the scripts' own; the stack-depth faults
(`deep_parens`, `nested_pcall`, `recursive_index`, and `loop`'s recursion band) depend on frame sizes, so the device may
stop them with the other of the two stack messages.

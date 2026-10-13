---
name: run-crosshatch-player
description: Build, run, and drive the crosshatch-player firmware in the desktop simulator (SDL2, headless under Xvfb) to see a UI change working. Use to run the simulator, launch the app, tap/swipe/press buttons, take a screenshot of a screen, or check an X4 Pro or Sticky UI change without hardware.
---

# Run crosshatch-player in the simulator

The firmware compiles as a native Linux binary against
[crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator),
which replaces `lib/hal/` and draws the e-ink framebuffer in an SDL2 window.
Agents drive it with `.claude/skills/run-crosshatch-player/sim.sh`, which runs
the window on Xvfb, sends taps, swipes, and keys with `xdotool`, and saves PNG
screenshots to `build/sim/shots/`. **Always look at the screenshot.**

All paths are relative to the repo root. Set the driver as a variable:

```bash
S=.claude/skills/run-crosshatch-player/sim.sh
```

## Prerequisites (once per container)

```bash
apt-get install -y libsdl2-dev libssl-dev xvfb xdotool imagemagick
git submodule update --init --recursive
```

PlatformIO: the proxy returns 403 for GitHub archive `.zip` URLs, so install
pioarduino core from PyPI:

```bash
uv tool install pioarduino==6.1.19
```

## Setup and build

```bash
$S setup          # writes the sim envs into platformio.local.ini, seeds fs_/books/
$S check          # exit 0 if that block is current, else say why and exit 1
$S build x4pro    # or: sticky | x4.  ~80 s cold, ~12 s incremental or with a warm .cache/
```

`setup` copies `simulator.ini` (from this skill directory) into the gitignored
`platformio.local.ini`, which `platformio.ini` already loads through
`extra_configs`. No tracked file changes. `check` exits 1 when
`platformio.local.ini` lacks the current `simulator.ini` (never set up, malformed
markers, or `simulator.ini` edited since), and `build` runs `setup` first when
`check` fails; `setup` replaces its own marked block and leaves the rest alone.
The markers must be absent, or exactly one begin marker followed by one end
marker. Any other shape (a begin or an end marker alone, an end marker first,
two begin or two end markers, two blocks) is the one case `build` does not
repair: `setup` exits 1 without changing the file rather than drop the settings
around the markers, and `check` exits 1 saying so, so restore them by hand.
`scripts/sim_sh_test.py` tests both commands on scratch repositories.
The envs are `simulator_x4pro`, `simulator_sticky`, and `simulator` (X4).
`x4pro` runs the device's bezel insets (a 466 x 788 game canvas, from `shim/BoardConfig.h`); `sticky` keeps the default
(474 x 788).

## Run: live session (agent path)

```bash
$S start x4pro           # Xvfb :77 + simulator in background; prints window size
$S ss home               # -> build/sim/shots/home.png
$S tap 240 345           # Browse Files
$S tap 240 138           # first row ("books")
$S tap 240 138           # first book; first open indexes the EPUB
$S key next              # page forward (Down)
$S swipe 400 400 80 400  # swipe left = page forward on touch devices
$S ss page
$S log 20                # tail firmware log (activity changes, errors)
$S stop
```

Commands: `tap X Y`, `hold X Y [MS]`, `swipe X1 Y1 X2 Y2`,
`key back|enter|left|right|up|down|power|home|sleep [HOLD_MS]` (`next`/`prev`
alias `down`/`up`), `ss [NAME]`, `geom`, `log [N]`. Each input waits
`SIM_SETTLE` seconds afterwards (default 1); use `SIM_SETTLE=3` for slow screens.
`SIM_DISPLAY` overrides `:77`. `SIM_INSTANCE=N` runs a numbered instance (see Two instances).

Coordinates are window pixels, which equal the firmware's logical pixels
(480x800 in portrait). Portrait X4 Pro and Sticky landmarks, Lyra theme:

| Screen | Target | X,Y |
|---|---|---|
| Home | Browse Files / Library / File Transfer / Games / Settings | 240,345 / 240,418 / 240,490 / 240,562 / 240,634 |
| List screens | first row; header back arrow | 240,138; 25,63 |
| File Transfer | Join / Calibre / Create Hotspot | 240,145 / 240,220 / 240,285 |

Opening Games installs every `fs_/games/*.chgame` (an "Installing games..." popup shows
while it works, and a `.chgame` that fails is renamed `.chgame.bad` with its reason in a
popup), then lists the installed games, `fs_/.games/<id>/` folders with a `.pkg`. To try a
fixture, pack it and drop the package in the inbox, then open Games:
`python3 scripts/pack_game.py test/game_script/fixtures/tracer /tmp/packs && mkdir -p fs_/games && cp /tmp/packs/tracer.chgame fs_/games/`.
Never copy a fixture folder into `fs_/.games/`: without the installer's `.pkg` it is not listed.
`images/`, `bad-image/`, and `solo/` are host-only (they hold `.bmp` files or a
`screenshots/` folder, so `pack_game.py` refuses them); `test/game_script/fixtures/README.md`
says how to place the rest, faults included.
Games run in both `x4pro` and `sticky` (both envs set `BOARD_HAS_PSRAM`). The
`gallery` fixture draws every command and color and prints the last touch event:
`hold X Y 800` is a long press, `swipe` a swipe; a swipe from the left quarter
rightwards is Back. Back and Home open the game's pause menu (Resume, Leave);
Leave, or Back from the error view, returns to Games
(`docs/crosshatch/game-canvas.md`).

## Run: scripted (deterministic, one-shot)

The simulator's own timed input schedule. Good for reproducible before/after
shots. Actions use the simulator's syntax (`TAP:x,y`, `SWIPE:x1,y1,x2,y2`,
`BACK`, `ENTER`, `UP`, `DOWN`, `HOME`, `SLEEP`, `QUIT`), timed in ms from launch:

```bash
$S script x4pro '1500:TAP:240,560;3000:HOME;4500:QUIT' '2500:settings;4000:home-key'
```

This prints `build/sim/shots/settings.png` and `home-key.png`. End every script
with `QUIT`, or the run hits `SIM_TIMEOUT` (60 s) and fails.

## Two instances: Play Nearby

Under the simulator Play Nearby is on: the lobby's radio is `UdpLink` (`src/games/UdpLink.cpp`), UDP on loopback, so
two simulator windows of one worktree can play one round. `SIM_INSTANCE=N` (`1`..`9`; unset or empty is the single
instance above, unchanged) gives each window its own Xvfb display (`:77+N`), state directory (`build/sim/inst-N/`: pids,
log, `shots/`), SD root (`build/sim/inst-N/fs_`, passed to the binary as an absolute `CROSSPOINT_SIM_SD`), web server
port (`8080+10N`), and inbox (`build/sim/inst-N/fs_/games`). Any other value exits 1 naming `SIM_INSTANCE`.

```bash
$S where                                  # the single instance's display, state, SD root, inbox, ports, UDP base
SIM_INSTANCE=1 $S where                   # instance 1's, without creating anything
SIM_INSTANCE=1 $S inbox test/game_script/fixtures/nearby   # packs the game into instance 1's inbox
SIM_INSTANCE=2 $S inbox test/game_script/fixtures/nearby
SIM_INSTANCE=1 $S start x4pro             # a window on :78
SIM_INSTANCE=2 $S start x4pro             # a window on :79
SIM_INSTANCE=1 $S ss host-choose          # -> build/sim/inst-1/shots/host-choose.png
SIM_INSTANCE=2 $S tap 240 300             # drives instance 2 only
SIM_INSTANCE=1 $S stop; SIM_INSTANCE=2 $S stop
```

Every command takes the variable (`start`, `tap`, `hold`, `swipe`, `key`, `ss`, `geom`, `log`, `stop`, `where`,
`inbox`). `setup`, `check`, and `build` do nothing different with it, but an invalid value (not `1`..`9`) makes every
command exit 1, those included. `script` with a valid instance uses the instance's state directory and `shots/`, but
the shared `./fs_` and no instance environment (no `CROSSPOINT_SIM_SD`, web server port, or UDP base). The simulators of
one worktree must agree on the UDP port base, the default instance included: every `start` exports
`CROSSHATCH_UDP_PORT_BASE` to the binary, the caller's value when it is set, else `21000 + (cksum of the repo root % 1000) * 8`,
so simulators of two worktrees running at once do not hear each other. Without the variable `UdpLink` binds the first
free port of `45700..45707`. A link takes the first free port of its range of eight, and the port is the host's label,
`Simulator <port>`; a ninth window finds no port and the lobby shows "Nearby could not start".

The recipe, with the `nearby` fixture (two seats, `pass` and `nearby` modes) in both inboxes, as run for
`_bmad-output/initiative-crosshatch-player-v1/epic-play-nearby/story-udp-link-screenshots/`. Each instance has its own
`prefs.bin`, so each sets the mode once. Coordinates are window pixels, X4 Pro, Lyra theme:

1. Pack the fixture into each inbox, start both instances, and on each tap Games (`240,562`; that installs the
   package), the game (`240,148`), Options (`80,687`), the Mode row (`240,150`, which cycles to "Play nearby"), the
   header back arrow (`25,63`).
2. New game (`240,615`) opens the lobby's Choose screen: Host a game (`240,148`), Join a game (`240,210`).
3. Instance 1 hosts: label `Simulator <port>`, "Waiting for a player", "1 of 2 players", then Start (`240,241`) and
   Cancel (`240,303`) ("Player 2 joined" once the guest has joined). Instance 2 joins: the list shows the host row
   (`240,148`; a host holding the fixture at another version is greyed with "Different version"), and a tap shows
   "Waiting for the host to start" with Cancel (`240,208`). A guest who cancels shows the host "Player 2 left"; a host
   who cancels shows the guest "The host left" with "Back to the list" (`240,180`).
4. Instance 1 taps Start. The board cells' centres are `105/245/385, 280/420/560` (the host is X and moves first); a tap
   on a taken square shows "That square is taken" on the guest only. A won round shows the Game over dialog: on the host
   Play again (`240,452`) and Leave (`240,503`), on the guest "Waiting for the host" and Leave. Back (`key back`) opens
   the pause menu (Resume, Leave `240,503`); Leave on the host leaves the guest on "The other player left" with Back
   (`240,477`), which returns to the Games list.

Look at each screenshot before using it. The lobby polls every 100 ms and a changed list of hosts redraws within a
second; `SIM_SETTLE=3` or `4` between taps gives the other instance time to hear a frame.

`.claude/skills/run-crosshatch-player/sim_instance_test.py` tests `where`, `inbox`, and the invalid values on scratch
repositories; run it by hand (`python3 .claude/skills/run-crosshatch-player/sim_instance_test.py`).

## File-transfer web server

In a live session, tap File Transfer, then Create Hotspot (`240,285`). The
firmware's web server is then served on the host:

```bash
curl -sS http://127.0.0.1:8080/api/status
```

## Run: human path (desktop with a display)

```bash
pio run -e simulator_x4pro -t run_simulator
```

This opens an SDL window. Keys: arrows, Return, Esc, P (power), S (sleep),
H (Home); mouse = touch. It blocks until the window closes, so agents use the
driver instead.

## Test

The simulator is for seeing behavior. Unit tests are still host GoogleTest:

```bash
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j4
```

## Gotchas

- **Run from the repo root.** The simulated SD card is `./fs_/` relative to the
  working directory (`/books/` = `fs_/books/`). The driver `cd`s there itself.
  All device envs share `fs_/`, including settings, reading progress, and the
  last-open book.
- **Stale caches.** After a change to layout, line breaking, or cache formats,
  run `rm -rf fs_/.crosspoint/`, or old cached pages will hide the fix.
- **Wake resumes the last book.** `key sleep` shows the sleep screen, and any
  key wakes it. The simulator re-execs itself (same PID, log clock restarts
  at 0), and the firmware reopens the last-open book, not Home.
- **The simulator library is pinned** in `simulator.ini` (`#1915ee0…`). After
  merging upstream `develop`, a link error naming a `Hal*` method means the HAL
  moved ahead of the pin. Bump the commit to the newest upstream simulator
  commit, re-run `$S setup`, and rebuild. If the fork's own HAL diverges, fork
  the simulator (see its `FORKING.md`) and point `lib_deps` at the fork.
- **Upstream's Linux sample pulls in `AnimatedGIF`**, which fails with
  `'memcpy_P' was not declared`. This firmware never uses it, so
  `simulator.ini` omits it.
- **Fake heap.** `freeHeap` reads 1 MiB and nothing enforces the C3's
  ~380 KB. The simulator shows behavior, not memory safety. Set
  `CROSSPOINT_SIM_FREE_HEAP` and `CROSSPOINT_SIM_MAX_ALLOC_HEAP` to exercise
  low-memory branches, and still build `default` for the real budget.
- **No e-ink physics.** Refresh modes, ghosting, and waveform timing are not
  modeled. Grays render as dithered patterns.
- **After an orientation change,** the window reshapes. Check `$S geom` before
  tapping.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `uv pip install https://github.com/.../v6.1.19.zip`: 403 Forbidden | Install from PyPI (see Prerequisites). |
| `gif.inl: 'memcpy_P' was not declared` | Remove `AnimatedGIF` from `lib_deps`, then `$S setup`. |
| `sim: .pio/build/…/program missing` | Run `$S build <device>` first. |
| `sim: no simulator window on :77` | The sim exited or never started. Check `$S log`, then `$S start` again. |
| `pkill -f …program` kills your own shell (exit 144) | The pattern matches your own command line. Use `$S stop`, which uses the pid files in `build/sim/`. |
| `[Xvfb] <defunct>` in `pgrep` after `stop` | This is a harmless zombie that is reaped when the shell exits. `start` still works. |

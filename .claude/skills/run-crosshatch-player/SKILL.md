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
pioarduino core from a clone:

```bash
git clone -q --depth 1 --branch v6.1.19 https://github.com/pioarduino/platformio-core.git /tmp/pio-core
uv pip install --system /tmp/pio-core
```

## Setup and build

```bash
$S setup          # writes the sim envs into platformio.local.ini, seeds fs_/books/
$S build x4pro    # or: sticky | x4.  ~80 s cold, ~12 s incremental or with a warm .cache/
```

`setup` copies `simulator.ini` (from this skill directory) into the gitignored
`platformio.local.ini`, which `platformio.ini` already loads through
`extra_configs`. No tracked file changes. Re-run it after editing
`simulator.ini`; it replaces its own marked block and leaves the rest alone.
The envs are `simulator_x4pro`, `simulator_sticky`, and `simulator` (X4).

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
`SIM_DISPLAY` overrides `:77`.

Coordinates are window pixels, which equal the firmware's logical pixels
(480x800 in portrait). Portrait X4 Pro and Sticky landmarks, Lyra theme:

| Screen | Target | X,Y |
|---|---|---|
| Home | Browse Files / Library / File Transfer / Settings | 240,345 / 240,418 / 240,490 / 240,562 |
| List screens | first row; header back arrow | 240,138; 25,63 |
| File Transfer | Join / Calibre / Create Hotspot | 240,145 / 240,220 / 240,285 |

## Run: scripted (deterministic, one-shot)

The simulator's own timed input schedule. Good for reproducible before/after
shots. Actions use the simulator's syntax (`TAP:x,y`, `SWIPE:x1,y1,x2,y2`,
`BACK`, `ENTER`, `UP`, `DOWN`, `HOME`, `SLEEP`, `QUIT`), timed in ms from launch:

```bash
$S script x4pro '1500:TAP:240,560;3000:HOME;4500:QUIT' '2500:settings;4000:home-key'
```

This prints `build/sim/shots/settings.png` and `home-key.png`. End every script
with `QUIT`, or the run hits `SIM_TIMEOUT` (60 s) and fails.

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
- **The simulator library is pinned** in `simulator.ini` (`#8699595…`). After
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
| `uv pip install https://github.com/.../v6.1.19.zip`: 403 Forbidden | Install from the `git clone` (see Prerequisites). |
| `gif.inl: 'memcpy_P' was not declared` | Remove `AnimatedGIF` from `lib_deps`, then `$S setup`. |
| `sim: .pio/build/…/program missing` | Run `$S build <device>` first. |
| `sim: no simulator window on :77` | The sim exited or never started. Check `$S log`, then `$S start` again. |
| `pkill -f …program` kills your own shell (exit 144) | The pattern matches your own command line. Use `$S stop`, which uses the pid files in `build/sim/`. |
| `[Xvfb] <defunct>` in `pgrep` after `stop` | This is a harmless zombie that is reaped when the shell exits. `start` still works. |

---
title: 'Technical research: where the game runtime spends its time, and what would make full-page graphics fast'
type: 'technical'
topic: 'Game runtime performance on the X4 Pro: frame replay, framebuffer placement, Lua, SD writes'
decision: 'Nothing now. First-party games are light on graphics (owner, 2026-10-04). Reopen this if a game shows slow frames or wants full-page art.'
source: 'native run: simulator measurements and a callgrind profile at firmware commit 4cecba478f, read against the owner device runs recorded in the ticket plans'
status: 'complete: shelved, no action'
firmware_commit: '4cecba478f'
created: '2026-10-04'
updated: '2026-10-04'
---

# Technical research: game runtime performance

**Decision this research serves:** none today. The owner decided on 2026-10-04 that first-party games will be light on
graphics, so no optimization ticket is opened. This note is the reference for the day a game shows slow frames, or a
full-page graphic game is planned. It records what was measured, the evidence chain, the options with their expected
gains, and the diagnostics that produced the numbers, so the next person starts from measurements, not guesses.

## Executive summary

**Finding.** The game runtime's time goes into the frame replay's pixel writes, not into Lua. Two things make the
pixel writes expensive on the X4 Pro: the e-ink framebuffer is almost certainly in PSRAM (B1), and images and icons are
replayed as one `fillRect` call per horizontal run of one colour, about 190,000 calls for a dithered full-canvas image
(B2). Typical frames (text, rects, a few icons) replay in an estimated 10 to 30 ms on the device, which is invisible
under the 559 ms fast panel refresh. Only images and large dithered fills are outliers: 0.94 s, 1.7 s, and 3.7 s from
tap to picture for the `timing` fixture's three bands.

**Expected gains if ever needed.** Replaying into an internal-RAM scratch and blitting images in one call per band
(options O1 and O2 below) would bring every frame to the refresh floor of about 0.6 s and cap the pathological fill
frame at about 1 s. For graphics-light games they change nothing a player can see.

**Cheapest next step when reopened.** One `LOG_DBG` of the framebuffer address at boot on a device build: an address
at or above `0x3C000000` is PSRAM on the ESP32-S3. The largest estimate below depends on it.

## Measurements

Every device figure is the owner's, from the tethered serial log of epic-install-and-launcher's device run (entry 14,
`_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/story-device-run-and-owner-sign-off-plan.md`,
rows 8 and 8a, 2026-09-30) and the pass-and-play device run (`.../epic-pass-and-play/story-device-run-and-owner-sign-off-plan.md`,
rows T1 to T6, 2026-10-03). The simulator figures are from this research at firmware commit `4cecba478f`
(`simulator_x4pro` env, x86, `sim.sh` on Xvfb), with a temporary `millis()` probe around `GameVM::drawFront` in
`GameMatchActivity::renderCanvas` that was not committed. A simulator time says nothing about the device in absolute
terms; the device-to-simulator ratio is the useful column.

| Frame | Device replay under `RenderLock` | Device, tap to picture (video) | Simulator replay | Ratio |
|---|---|---|---|---|
| `timing` band 1: one dithered 480 x 800 image | 567 ms | 0.94 s | 22 ms | 26x |
| `timing` band 2: 1,048,576 icon and image pixels (the whole `frame_icon_image_pixels` budget) | 1,338 ms | 1.69 s | 45 ms | 30x |
| `timing` band 3: 2,048 full-canvas filled rects (light, black, white, dark) | 3,142 ms | 3.70 s | 4 ms | 785x |
| `timing` menu, `gallery` page, `icons` page (typical frames) | not recorded; estimated 10 to 30 ms from the ratios | about 0.6 s | 0 to 1 ms | |

Panel refresh times on the X4 Pro (SSD1677), from the same logs: fast 559 ms, half 1,654 to 1,720 ms, full 1,489 to
1,722 ms. Every frame pays one of these after its replay.

Loop-task costs per move (pass-and-play run): `resume.bin` write 91 to 92 ms (one 50 ms); `ch.store` write 42 to
656 ms (the long ones overlapped a panel push).

## Evidence chain

### B1. The X4 Pro's framebuffer is in PSRAM (strongly indicated, not yet confirmed on a device)

- The SDK allocates the framebuffer with plain `malloc(bufferSize)` unless `FREEINK_FB_PSRAM` is set
  (`freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp`, `allocFrameBufferStorage`). `FREEINK_FB_PSRAM`
  defaults to M5Paper and Paper Mono only (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`). The SDK's
  comment says the framebuffer is internal on every other board.
- The x4pro env extends `base` and uses the prebuilt `dio_opi` Arduino variant (no `custom_sdkconfig`). That variant's
  sdkconfig (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig`) has
  `CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, `CONFIG_SPIRAM_MODE_QUAD=y`. So any
  `malloc` over 4 KB is served from quad-SPI PSRAM first, and the 48,000-byte framebuffer is one.
- Band 3 writes 2,048 x 48 KB = 96 MB in 3,142 ms: about 31 MB/s. That is quad PSRAM bandwidth, not internal SRAM
  (which would do the same in a few hundred milliseconds). Band 1's 190,000 scattered single-byte read-modify-writes
  at about 3 µs each is PSRAM cache-miss latency.
- The simulator cannot show this (its "heap" is host memory), which is why band 3's ratio is 785x while the others
  are about 30x.

Confirming it is one log line on a device build: `LOG_DBG("GFX", "framebuffer at %p", renderer.getFrameBuffer())`;
the S3 maps PSRAM at `0x3C000000` and up.

### B2. Images and icons are replayed one run at a time

- `FrameReplay::draw` (`src/games/FrameReplay.cpp`) turns an `Image` command into `GameImageBlit::runs`, which
  walks every visible pixel and calls `renderer.fillRect(x, y, w, 1, black)` once per run of one colour; `Icon` goes
  through `GameIconBlit::inkRuns` and `drawGameIconAt` the same way. The installer's converter Atkinson-dithers
  images to 1 bit, so a photo-like or gray image has runs of one or two pixels: a 480 x 800 image is about 190,000
  calls.
- The callgrind profile of the simulator (the `timing` bands) puts 90 percent of the image replay inside
  `GfxRenderer::fillRect` (804 M of 894 M instructions in `GameImageBlit::runs`); the pixel walk (`blackAt`, the run
  loop) is under 10 percent. Each call pays clipping, two `rotateCoordinates`, strip-mode checks, and the masks.
- On every portrait device the panel is rotated: Portrait maps logical `(x, y)` to physical `(y, 479 - x)`
  (`rotateCoordinates` in `lib/GfxRenderer/GfxRenderer.cpp`). A logical horizontal run therefore lands on a different
  physical row per pixel, which is the worst axis for the cache; a logical vertical run is contiguous bits in one
  physical row.
- e3r-1's budget (`frame_icon_image_pixels` = 1,048,576) bounds the pixel count, so the worst case is bounded, but
  it is bounded at 1.3 s of replay plus the refresh.

### B3. Lua is not the bottleneck for drawing

- No Lua function (`luaV_execute`, `luaD_hook`, the `ch.gfx` bindings) appears in the profile's top entries for these
  frames; the `gallery` and `icons` draws, which make a few hundred binding calls each, replay in about a millisecond on
  x86.
- What remains unmeasured is the interpreter's speed from the PSRAM arena (AD-6 puts the whole Lua heap there). The
  `CallGuard` budget is 2,000,000 instructions per call and the watchdog 3 s; a device that ran under about 670,000
  Lua instructions per second would hit the watchdog before the budget. No device run has timed it; the `slow-restart`
  fixture spins on `ch.time.ms()`, not on an instruction count.

### B4. Loop-task stalls per move

- `MatchPersistence::flushResumeOf` writes `resume.bin` for every committed snapshot: `replaceFile` does
  `ensureDirectoryExists`, `exists`, open-write-close, `remove`, `rename`, all under the Storage lock on the loop
  task, which is also the touch poller. Measured at 91 ms per move. `ch.store` writes land on the same task
  (`FLUSH_INTERVAL_MS` = 5 s after a change).
- Not a frame cost; it shows as touch pickup, and only in rapid-tap games.

### B5. Things checked and found fine

- The render task holds a `HalPowerManager::Lock` while rendering (`ActivityManager.cpp`), so a replay never runs on
  the 80 MHz idle clock. A timer-driven Lua call can run its first ~10 ms on the idle clock before the loop restores
  it (the loop restores full speed only when it sees `GameVM::busy()`); minor.
- The per-frame FNV-1a hash (up to 32 KB), the codec's per-call decode (state up to 1,400 B) and encode, the call
  hook, the 10 ms loop cadence, the frame mutex: each costs tens of microseconds to a few milliseconds against a
  559 ms refresh.
- `renderer.drawText` per glyph (the replay draws one code point per call so widths match `ch.text_width`) allocates
  nothing for Latin text (`resolveVisualText` scans bytes before building a string) and costs a map lookup per glyph.
- `FrameBuffers::takeFront` holds the frame mutex for the whole replay, so the VM's next `publish` waits for it. Only
  matters when a replay is long, which is exactly the image case; a third list (32 KB of PSRAM) would decouple them.

## Options, with expected gains

Estimates, not measurements. Each row is the device's tap-to-picture time; the 0.6 s floor is the fast refresh.

| Frame | Today | O1 scratch replay | O2 batched blits | O1 + O2 |
|---|---|---|---|---|
| 2,048 full-canvas fills (worst case) | 3.7 s | ~1.0 to 1.2 s | no change | ~1.0 to 1.2 s |
| One dithered full-canvas image | 0.94 s | ~0.7 to 0.8 s | ~0.7 s | ~0.6 s |
| Full icon and image budget frame | 1.7 s | ~1.1 to 1.3 s | ~0.8 s | ~0.6 to 0.7 s |
| Typical frame (text, rects, a few icons) | ~0.6 s | ~0.6 s | ~0.6 s | ~0.6 s |

### O1. Replay into an internal-RAM scratch, then copy once (fork-only)

`GfxRenderer::beginStripTarget(scratch, 0, panelHeight)` already redirects `fillRectImpl`, `drawPixel` (lines),
`drawGlyphBitmap` (text) and `clearScreen` to a caller scratch holding physical rows; `getFrameBuffer()` and
`getBufferSize()` let `FrameReplay` memcpy the finished frame into the real buffer before `displayBuffer`. All four
replay paths honour the strip. Cost: 48,000 B of internal RAM for the match (the device run showed about 188 KB free
internal at idle), allocated with `MALLOC_CAP_INTERNAL` and null-checked, falling back to today's path; or 24 KB and
two replay passes, since fills and glyphs clip to the band. Gain: a full-canvas fill from ~1.5 ms to ~0.2 ms; images
by a few x (the per-call cost stays). Changes only `src/games/FrameReplay.cpp` and `GameMatchActivity`; the harness's
recording renderer (`test/game_script/harness/stubs/GfxRenderer.h`) needs the strip calls added.

### O2. Blit images and icons in one call per band (fork-only)

`GfxRenderer::drawGlyphBitmap(bitmap, width, height, frame, twoBit=false, mode, state)` is public and resolves
rotation, clipping and the strip once per bitmap, then paints "1 = ink" pixels in `state`'s colour. Image rows are
byte-padded (`rowBytes`), which maps onto it by declaring the glyph width as `rowBytes * 8` and clipping right to the
real width; an opaque image is one `fillRect` of the background ink plus one blit of the other (no bitmap inversion:
for "black" fill the rect black and paint white where the bit is 1; for "white" the reverse). Icons are ink-only
already; their PackBits rows decode in bands small enough for the 256 B stack rule, and the 128 px size (the 64 px
bitmap at 2x) needs a 2x row expansion per band. Gain: about 190,000 calls per image become a handful; replay of a
full-canvas image from 567 ms to tens of milliseconds if the framebuffer is internal, somewhat more in PSRAM. The
harness's recording renderer needs `drawGlyphBitmap` implemented, since `FrameReplayTest` checks pixels through it.
Keeps every guard of e3r-1 (budget, name checks, `frameFull`) untouched: this is replay-side only.

### O3. Propose upstream: allocate the framebuffer internally when `FREEINK_FB_PSRAM` is off

One `heap_caps_malloc(bufferSize, MALLOC_CAP_INTERNAL)` in `allocFrameBufferStorage`. Fixes the reader's page renders
too. By policy it goes through the SDK (never move the submodule pointer), so it is a proposal, not a fork change.
Confirm B1 on a device first.

### O4. Coalesce `resume.bin` writes

Write at most once every few seconds after the last commit, plus on Leave, round end and the forced exit (which already
flush). Removes ~90 ms of loop-task stall per move. Trade: a hard crash loses the last seconds of moves. Only worth
it for rapid-tap games.

### Not worth doing

The hash, the codec, the call hook, the loop cadence, Lua GC tuning (default incremental, pause 250, stepmul 200):
none reaches a millisecond that the refresh does not hide.

## Diagnostics

Existing, on the device:

- `[GFX] Time = N ms from clearScreen to displayBuffer` (`LOG_DBG`): the replay under `RenderLock` without the
  panel refresh. The x4pro env builds with `LOG_LEVEL=2`, so it is on; `pio device monitor -e x4pro` captures it.
  This is the line entry 14 read for the band times.
- `[GAME] <id>: frame N pushed in M ms` (`LOG_INF`): the panel push alone. Adding `replayed in` beside it (a
  `millis()` before `vm->drawFront` in `renderCanvas`) is the probe this research used.
- `[LOOP] New max loop duration` (`LOG_DBG`): loop-task stalls over 50 ms (SD writes show here).
- `VM stopped; arena peak N bytes, stack high-water N bytes free, least at a hook N bytes` on Leave: Lua heap and
  task stack.
- The `timing` fixture (`test/game_script/fixtures/timing/`) is the replay stress test; `fixtures/README.md`,
  "Timing run", says how to run and record it.

In the simulator (real `GfxRenderer` code on x86; `lib/hal` is the simulator's):

- Build and drive with `.claude/skills/run-crosshatch-player/sim.sh` (`setup`, `build x4pro`, `start`, `tap`, `ss`,
  `log`). `xdotool` had to be installed (`apt-get install -y xdotool`); the setup script does not install it. Pack
  fixtures with `scripts/pack_game.py` into `fs_/games/`.
- Function-level profile: start `Xvfb :78`, then
  `DISPLAY=:78 valgrind --tool=callgrind --callgrind-out-file=<out> .pio/build/simulator_x4pro/program` from the
  repo root, drive it with `xdotool` (a 0.3 s mouse down registers as a tap; settle 40 to 90 s per step, since boot
  takes about 25 s and a band replay 1 to 3 s under callgrind), then `callgrind_control -d <pid>` dumps without
  quitting. `callgrind_annotate --inclusive=yes <out>` gives the split between the pixel walk and the renderer.
  Absolute numbers mean nothing for the device; ratios between functions do.
- Running `sim.sh setup` changes PlatformIO's project checksum and empties `.pio/build`, so the next firmware build
  is cold.

Worth adding when this is reopened:

- A `bench` fixture that logs milliseconds per million Lua instructions (a counted loop timed with `ch.time.ms()`),
  to settle B3's PSRAM-arena question and calibrate the 2,000,000 budget against the 3 s watchdog.
- A per-call Lua duration log over a threshold (the pass-and-play run noted that a call's own time is not logged).
- The framebuffer address log of B1.

## Claims

| # | Claim | Status | Evidence |
|---|---|---|---|
| 1 | Band replay and refresh times on the X4 Pro | verified | owner's serial log and video, entry 14 rows 8 and 8a |
| 2 | Simulator replay times at `4cecba478f` | verified | this research, temporary probe, `sim.log` |
| 3 | 90 percent of the image replay is inside `fillRect` | verified (x86) | callgrind profile, inclusive costs |
| 4 | The x4pro framebuffer is in PSRAM | unverified, strongly indicated | SDK `malloc` path, sdkconfig `SPIRAM_USE_MALLOC`, band 3's 31 MB/s |
| 5 | Typical frames replay in 10 to 30 ms on the device | unverified | simulator 0 to 1 ms times the 26 to 30x ratio |
| 6 | O1 and O2 gains | unverified estimates | cycle costs in B1 and B2 |
| 7 | `resume.bin` write per move costs ~90 ms on the loop task | verified | pass-and-play run row T1 |
| 8 | Lua speed from PSRAM | unmeasured | no device benchmark exists |

# Smoke-run packet: epic-play-nearby entry 11

Everything the owner needs for the first run of Play Nearby on two X4 Pros: the firmware, the package, the capture, the steps in run order, and what to record. The record is the "Owner's smoke run: fields to fill" table at the end of `story-nearby-on-two-x4-pros-wiring-and-the-smoke-run-plan.md` (beside this file); the orchestrator brings the owner's results back into that plan. A failure on the device becomes a new entry of the epic (ticket 11's description).

Needed: two X4 Pros, one computer with two USB ports, and about an hour. Sticky and a mixed pair are not in this run (spec Open Questions).

## Firmware

- **One build on both devices.** The orchestrator builds it from the merged epic head with the touch log on, under the build lock:
  `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_TOUCH_LOG=1 pio run -e x4pro` (the recipe is `docs/crosshatch/game-canvas.md`, "The canvas exception").
  - Firmware commit: `b9f5aceefbe93a1caff2e84e61694f581bc03f5e` (the epic branch after entry 11's wiring merged), built 2026-10-13 in the main checkout under the build lock with `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_TOUCH_LOG=1 pio run -e x4pro`; `scripts/check_flash_budget.py touch-line` on its ELF reports the per-tap line present, so the define took.
  - File: `crosshatch-x4pro-b9f5acee-touchlog.bin`, 6,256,480 B, SHA-256 `a3c0083064e9b557c860d155599b215a613711bdd2fddb44a188a313e3acba00`.
  - **Check the SHA-256 on the computer before flashing (`sha256sum <name>.bin`), and flash only a file that matches, the same file on both devices.** A level-1 surface mismatch ends a match (AD-13), and a different build of the lobby would not join.
- Flash with CrossPoint Reader Flash Tools, or upload the `.bin` with the device's Wi-Fi file transfer and use Settings' firmware update from the SD card. Close the file transfer afterwards: the lobby refuses to open while Wi-Fi is up ("Wi-Fi is in use. Close it and try again."). If the refusal shows after you closed it, record it as a finding (what you closed, and how) and restart the reader to carry on.
- The x4pro build logs at `LOG_LEVEL=2`. A `[MEM]` line comes every 10 s while a port is open.
- This is a diagnostics build (`-DCROSSHATCH_TOUCH_LOG=1` prints a line per tap). When the run is over, flash the normal release firmware back on both devices.
- Sticky: `NEARBY_BUILT` is on for every board that builds the games, Sticky included, and no Sticky has run this. Use two X4 Pros only; a Sticky offering nearby is expected and untested.

## The package

`python3 scripts/pack_device_run.py <out-dir>` packs it (with the other device-run games) and lists the hashes in `HASHES.txt`. For this run only `nearby.chgame` matters.

- `nearby.chgame`: 1,999 B, hash `2bef3752d7af24a2` (packed 2026-10-13 from the same commit by `scripts/pack_device_run.py`).
- Put the same file on both cards' game inbox (upload with the device's file manager into the games folder; check the file's size on the computer with `ls -l` first and on the card afterwards, since an upload once delivered 0-byte files). Open Games on each device so it installs, and check the list shows "Nearby" with the same hash line in the log on both.
- Nearby is a noughts-and-crosses fixture for two seats. A tap on a free square is a move; a tap on a taken square is rejected ("That square is taken", on the device that tapped, and nothing is sent). The square at the bottom left under the board (60 px) raises a script error, for the optional step.

## Capture

On the computer, with both devices plugged in and awake:

```sh
python3 scripts/device_log.py capture \
  --port A=/dev/serial/by-id/<device one> \
  --port B=/dev/serial/by-id/<device two> \
  --out nearby-run-logs
```

`docs/crosshatch/fork-scripts.md` ("Capturing a device run") has the port-path notes. Name the host `A` and the guest `B`; keep the capture running for the whole run, including each sleep. Stop it with Ctrl-C at the end, then:

```sh
python3 scripts/device_log.py summary nearby-run-logs/merged.log
python3 scripts/device_log.py summary --mem-all nearby-run-logs/A.raw.log
```

Note whether the run steps in `fork-scripts.md` worked as written, and any correction (the last row of the table).

## Steps, in run order

**1. The capture itself (`deferred-work.md` `## 6.3`).** Do this first, before any nearby step.
- Connecting must not reset the board. Start the capture on a board that was already running and look at the `[<millis>]` prefix of the first line after `connected`: it should be the board's uptime, not near 0 (near 0 means connecting reset it). Do this first, with nothing to lose, and record any reset.
- Then, with no `pio device monitor` open yet, capture for a minute and open `pio device monitor -e x4pro` on one port beside it: note whether bytes go missing from `A.raw.log` (holes between `[MEM]` lines, which come every 10 s) while both are open. A second reader can pulse DTR and RTS, so record a reset here too. Close the monitor.
- Let one device sleep (power button) and wake it: `merged.log` must give a `lost` line at the sleep and a `connected` line at the wake. Note each time.

**2. Host and join (the lobby, the join).** Both devices on the Games list, nearby on both.
- On both: open Nearby, Options, set Mode to "Play nearby" (the game's New game row then says "Play nearby"), back.
- Device A: New game, Host a game. The screen shows its label large (`X4 Pro` and four hex digits, from its MAC), "Waiting for a player", "1 of 2 players". Note the label as shown.
- Device B: New game, Join a game. The list shows A's label and "1 of 2 players". Note how long the host takes to appear (the host list redraws at most once a second, entry 10's D2: write down what you see on the panel, a late or flickering list included).
- Device B: tap A's row. B shows "Waiting for the host to start" and A's label; A shows "Player 2 joined" and Start enabled.
- Look at the `LINK` lines: `radio up on channel 1 as <MAC>` on each, `task started; stack 4096 bytes`, and `session=` with the same id on both once B has joined.

**3. A round with a rejected move.** Device A taps Start.
- A (X) moves first: tap a square. B must show it within a second or two. B (O) taps the same, taken square: B shows "That square is taken" and nothing is sent (no `state sent` line for it on B). Then B taps a free square, and A and B alternate until one wins or the board fills.
- The host's round start is the first state (`state sent ver=1` on A): B adopts it as `result=RoundStarted`, and that is expected. Each accepted move after it should give a `state sent ver=N` on the mover and a `state recv ver=N` then `state adopt ... result=Adopted` on the other (N rises by one each move). A `resend` line is a finding to record, not a failure. `queue full` is the match's 4-deep session queue overflowing (the frame is left unacked and resent); the radio's receive ring has its own overflow counter that no line logs.
- On the end of the round, A's menu offers Play again and Leave; B's offers "Waiting for the host" and Leave. Play again once, play a second round, then continue.

**4. Leave and "player left".**
- On A press Back (the pause menu), then Leave. Within a second B shows "The other player left" with Back; the `LINK` lines give `leave reason=left ... copies=3` on A and, on B, one `abort recv reason=left` line per copy it hears (up to three), then `Playing -> PeerGone`, and `the other player is gone: left`.
- If B waits on the "player left" view, a `silence n=1` line follows about 10 s after the `PeerGone` line (the link keeps counting while the view is up). That is expected after a clean leave and is not a finding.
- On B press Back: `PeerGone -> Leaving`, `task stopped; stack high-water N bytes free`. Both devices return to the Games list.
- Record each device's `task stopped` figure. The `GameLink` task has a 4,096 B stack and no margin was stated by entry 8; read the figure against the VM's rule (the owner kept 2,048 B headroom on its 16 KB stack, `deferred-work.md` `## owner-e4-entry14`). A figure under 512 B free is a finding.
- Optional: start another round and tap the bottom-left square on A (the script error): B must show the game-error line and `abort recv reason=script_error`. Also optional: power A off mid-round and watch B reach "went silent" about 10 s later (`silence n=1` then `PeerGone`).

**5. The radio is off afterwards.** After Leave, on the device that left, open Settings, Wi-Fi (or the file transfer) or start New game for nearby again: it must not refuse with "Wi-Fi is in use". Note it.

**6. Internal heap across ten bring-ups, one device, no peer (spec Open Question 2).** Use device A alone, with B asleep or off.
- Start from the Games list, idle. Wait for two `[MEM]` lines (20 s) and note the first `Free` and `Min Free` (that is the "before").
- Ten times: New game, Host a game (wait until the host screen shows its label: the radio is up, a `radio up` line), then Cancel. Each is one bring-up and one teardown with no peer. Do not Start.
- Back on the Games list, wait 20 s and note the next two `[MEM]` lines (the "after"). `device_log.py summary` shows `nearby: radio up 10` and, with `--mem-all`, every `[MEM]` line with its time.
- Record `Free`, `Min Free`, and `MaxAlloc` for the before and the after (the largest free block matters as much as the total: fragmentation is the usual failure). Take the spread between the idle `[MEM]` lines before the cycles as the noise. An after that is lower than the before by more than that spread, in `Free` or `MaxAlloc`, is a leak: record the figures and say whether the teardown after a bring-up needs `silentRestart()` (AD-18; this measures bring-up and teardown with no peer, not a match). One that recovers to within the noise needs none. The tolerance is the owner's call; write the figures either way. Also note any bring-up that failed (`[ERR] [LINK] bring-up failed at ...`, or "Nearby could not start").

**7. Device checks carried from earlier entries.**
- `## 6.5`: the receive callback writes frames into PSRAM from the Wi-Fi task and the ring's order matters on this dual-core chip. A clean round (steps 3 and 4) with no torn or reordered frame (no `BadSnapshot` adopt result, and no `Stale` one on a move that was not a resend) is the pass; the ring's overflow count is not logged, so a dropped frame shows only as a `resend`; a crash or a `Guru Meditation` is the failure, with its log.
- Entry 10's D2: the host list's 1,000 ms refresh. In step 2, with a second host nearby if you have one (or by A cancelling and hosting again), watch the guest's list: does it follow within about a second without flicker, and does the panel look right (no ghosting from repeated partial refreshes)?

## What to record

Fill the table at the end of the plan: the firmware commit and SHA-256; the merged log's lines for the lobby (the label as shown on A, B's list row, "Player 2 joined"), the join, a round with the rejected move, and Leave with "player left" (paste the lines of `nearby-run-logs/merged.log`); the stack high-water of each stop; the ten-cycle heap before and after and whether `silentRestart()` is needed; the `## 6.3` checks, `## 6.5`, and D2; and whether `fork-scripts.md`'s run steps worked as written. Keep `nearby-run-logs/` and send it with the results.

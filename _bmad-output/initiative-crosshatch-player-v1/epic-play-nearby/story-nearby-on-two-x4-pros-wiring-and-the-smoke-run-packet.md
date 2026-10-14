# Smoke-run packet: epic-play-nearby entry 11

This packet covers the first run of Play Nearby on two X4 Pros: the firmware, the package, the card log, the steps in run order, and what to record. The record is the "Owner's smoke run: fields to fill" table at the end of `story-nearby-on-two-x4-pros-wiring-and-the-smoke-run-plan.md` (beside this file); the orchestrator brings the owner's results back into that plan. A failure on the device becomes a new entry of the epic (ticket 11's description).

This is the cardless run (owner Decisions of 2026-10-13: the on-device diagnostics log, the hook version, with a flush before sleep). Each device writes its own log to its SD card, so no computer is attached during the run. A computer is needed only afterwards, to read the two cards and merge their logs. This packet replaces the earlier one (firmware `b9f5acee`, captured over USB), which should not be used.

Needed: two X4 Pros, each with its SD card, about an hour, and afterwards a computer with a card reader and this repository. Sticky and a mixed pair are not in this run: the owner has no Sticky yet, and Nearby is offered on the X4 Pro only (entry 20).

## Firmware

- **One build on both devices.** The orchestrator built it from the epic head with both diagnostics defines (the touch log and the SD-card log), under the build lock:
  `PLATFORMIO_BUILD_FLAGS="-DCROSSHATCH_TOUCH_LOG=1 -DCROSSHATCH_SD_LOG=1 -DCROSSHATCH_BUILD_ID=<commit>" pio run -e x4pro`. The recipe is in `docs/crosshatch/game-canvas.md` ("The canvas exception") and `docs/crosshatch/fork-scripts.md` ("The run without a computer").
  - Firmware commit: `7d5b59d35140fb93f3668f4dc2478ec55f5039e1`, the epic branch after entry 21 merged. It was built on 2026-10-13 in the main checkout, under the build lock, with `CROSSHATCH_BUILD_ID=7d5b59d3`, so each card file's `firmware` header line reads `7d5b59d3`. Its ELF has the card log's hook and flush (`DiagLog::append`, `DiagLog::flushBeforeSleep`), and `scripts/check_flash_budget.py touch-line` reports the per-tap line present, so both defines took. A define-on `pio run -e default` also built (entry 21's deferred close-out).
  - File: `crosshatch-x4pro-7d5b59d3-diag.bin`, 6,269,456 B, SHA-256 `bfdfc8f527ca7f24a310384e65815c48194708686be2b49f7f3fc44b7159cf58`.
- **Check the SHA-256 before flashing (`sha256sum <name>.bin`), and flash only a file that matches.** Put the same file on both devices. A level-1 surface mismatch ends a match (AD-13), and a lobby from a different build would not join.
- Flash it either way:
  - Use CrossPoint Reader Flash Tools.
  - Or copy the `.bin` to the card and use Settings' firmware update from the SD card. Copy it with a card reader, or upload it with the device's Wi-Fi file transfer.
- Close the file transfer afterwards. The lobby refuses to open while Wi-Fi is up ("Wi-Fi is in use. Close it and try again."). If the refusal still shows after you closed it, record it as a finding (what you closed, and how), then restart the reader to carry on.
- This is a diagnostics build:
  - It logs at `LOG_LEVEL=2`.
  - It writes one line per tap (the touch log).
  - It writes a `[MEM]` heap line pair every 10 s, which also reaches the card with no computer attached (entry 21).
  - Every line also goes to the card.
- When the run is over, flash the normal release firmware back on both devices. A normal build writes no card log.

## The package

`python3 scripts/pack_device_run.py <out-dir>` packs it with the other device-run games, and lists the hashes in `HASHES.txt`. For this run only `nearby.chgame` matters.

- `nearby.chgame`: 1,999 B, hash `2bef3752d7af24a2`, packed on 2026-10-13 from the same commit by `scripts/pack_device_run.py`. It is unchanged from the earlier packet.
- Put the same file in both cards' game inbox:
  - Use a card reader, or upload it into the games folder with the device's file manager.
  - Check the file's size before copying (`ls -l`) and on the card afterwards, since an upload once delivered 0-byte files.
- Open Games on each device so it installs. Check that the list shows "Nearby" on both. The card log's install line gives the hash.
- Nearby is a noughts-and-crosses fixture for two seats:
  - A tap on a free square is a move.
  - A tap on a taken square is rejected: the device that tapped shows "That square is taken", and nothing is sent.
  - The square at the bottom left under the board (60 px) raises a script error, for the optional step.

## The card log

- **Before the run, on both cards:** delete the `crosshatch-log` folder at the card's root, if one is there. `merge` reads every boot in the folder, so older runs' files would be merged in. Do this while the card is in the reader, when you copy the firmware and the package.
- **During the run:** nothing to do. Each device writes `/crosshatch-log/b<boot>-s<segment>-<device>.log`.
  - `<device>` is the last four hex digits of its label (`X4 Pro 3F2A` writes `...-3F2A.log`).
  - Every restart or wake from sleep starts a new boot number.
  - Write down which label is device A (the host) and which is B (the guest).
- **After the run:** turn both devices off, take out both cards, and on the computer, in the repository:

  ```sh
  python3 scripts/device_log.py merge --out nearby-run-logs A=/path/to/cardA B=/path/to/cardB
  python3 scripts/device_log.py summary nearby-run-logs/merged.log
  python3 scripts/device_log.py summary --mem-all nearby-run-logs/A.raw.log
  ```

  - Give each card's root, or its `crosshatch-log` folder; `merge` finds the folder.
  - `merge` writes `merged.log` (both devices on one run clock, lined up by the `LINK` lines' `session` and `STATE` `ver`), `A.raw.log`, `B.raw.log`, and `summary.txt`.
  - Copy each card's `crosshatch-log` folder into `nearby-run-logs/card-A/` and `card-B/` too, so the originals travel with the results.
- **What it cannot capture:**
  - A crash or panic. If one happens, write down what the screen showed and when.
  - The last lines before a deep sleep after the flush, "Entering deep sleep" among them.
- **Dropped lines:** a full buffer drops whole lines and says so in the next file as `[ERR] [DIAG] dropped N lines (buffer full)`. A failed card write is `[ERR] [DIAG] lost N bytes (card write failed)`. `summary` counts both. Any drop is a finding: record the count and where in the run it fell.
- The `## 6.3` checks of the earlier packet test the serial capture over USB, which this run does not use. They wait for a later run over USB, so skip them.

## Steps, in run order

**1. The card log itself.** Both devices on, from the Games list.
- Let device B sleep (power button), wait ten seconds, and wake it.
- After the run, B's card should hold:
  - two boot numbers;
  - the first boot's last file ending with `[DBG] [MAIN] Power button held <N>ms, sleeping` or a line just after it, which shows the flush before sleep worked ("Entering deep sleep" comes after the flush and is never on the card);
  - the second boot starting at the wake.
- Note the sleep and wake, and whether the files show them as described.

**2. Host and join (the lobby, the join).** Both devices on the Games list, with Nearby installed on both.
- On both devices: open Nearby, then Options, set Mode to "Play nearby", and go back. The game's New game row then says "Play nearby".
- Device A: New game, then Host a game. The screen shows:
  - its label, large (`X4 Pro` and four hex digits, from its MAC);
  - "Waiting for a player";
  - "1 of 2 players".

  Note the label as shown.
- Device B: New game, then Join a game. The list shows A's label and "1 of 2 players". Note roughly how long the host takes to appear. The list redraws at most once a second (entry 10's D2), so write down what you see on the panel, a late or flickering list included.
- Device B: tap A's row.
  - B shows "Waiting for the host to start" and A's label.
  - A shows "Player 2 joined", with Start enabled.
- In the merged log afterwards, look for these `LINK` lines:
  - `radio up on channel 1 as <MAC>` on each device;
  - `task started; stack 4096 bytes`;
  - `session=` with the same id on both, once B has joined.

**3. A round with a rejected move.** Device A taps Start.
- A (X) moves first: tap a square. B must show the move within a second or two.
- B (O) taps the same, taken square. B shows "That square is taken", and nothing is sent: no `state sent` line for it on B.
- B taps a free square. A and B then alternate until one wins or the board fills.
- What the merged log should show:
  - The host's round start is the first state (`state sent ver=1` on A). B adopts it as `result=RoundStarted`, which is expected.
  - Each accepted move after it gives `state sent ver=N` on the mover. On the other device it gives `state recv ver=N`, then `state adopt ... result=Adopted`. N rises by one each move.
  - A `resend` line is a finding to record, not a failure.
  - `queue full` means the match's 4-deep session queue overflowed; the frame is left unacked and resent. The radio's receive ring has its own overflow counter, which no line logs.
- When the round ends:
  - A's menu offers Play again and Leave.
  - B's menu offers "Waiting for the host" and Leave.

  Tap Play again once, play a second round, then continue.

**4. Leave and "player left".**
- On A, press Back (the pause menu), then Leave. Within a second, B shows "The other player left", with Back.
- The `LINK` lines:
  - on A, `leave reason=left ... copies=3`;
  - on B, one `abort recv reason=left` line per copy it hears (up to three), then `Playing -> PeerGone`, and `the other player is gone: left`.
- If B waits on the "player left" view, a `silence n=1` line follows about 10 s after the `PeerGone` line; the link keeps counting while the view is up. That is expected after a clean leave and is not a finding.
- On B, press Back. The log gives `PeerGone -> Leaving`, then `task stopped; stack high-water N bytes free`. Both devices return to the Games list.
- Record each device's `task stopped` figure; `summary` lists them.
  - The `GameLink` task has a 4,096 B stack, and entry 8 stated no margin.
  - Read the figure against the VM's rule: the owner kept 2,048 B of headroom on its 16 KB stack (`deferred-work.md` `## owner-e4-entry14`).
  - A figure under 512 B free is a finding.
- Optional:
  - Start another round and tap the bottom-left square on A (the script error). B must show the game-error line and `abort recv reason=script_error`.
  - Power A off mid-round and watch B reach "went silent" about 10 s later (`silence n=1`, then `PeerGone`). Write down the time on B's screen; A's log stops at the power-off with no flush.

**5. The radio is off afterwards.** After Leave, on the device that left, open Settings, then Wi-Fi (or the file transfer), or start New game for nearby again. It must not refuse with "Wi-Fi is in use". Note it.

**6. Internal heap across ten bring-ups, one device, no peer (spec Open Question 2).** Use device A alone, with B asleep or off.
- Start from the Games list, idle, and wait 30 s. The card gets a `[MEM]` pair every 10 s.
- Ten times: New game, Host a game, wait until the host screen shows its label (the radio is up, a `radio up` line), then Cancel.
  - Each is one bring-up and one teardown with no peer. Do not Start.
- Back on the Games list, wait 30 s, idle.
- Read it afterwards from `summary --mem-all` on `A.raw.log`, which lists every `[MEM]` line with its device time. The `radio up` lines mark the ten cycles; the summary shows `nearby: radio up 10`.
  - The "before" is the idle lines before the first `radio up`.
  - The "after" is the idle lines after the last Cancel.
- Record `Free`, `Min Free`, and `MaxAlloc` for the before and the after. The largest free block matters as much as the total, since fragmentation is the usual failure.
- Take the spread between the idle `[MEM]` lines before the cycles as the noise.
  - An after lower than the before by more than that spread, in `Free` or `MaxAlloc`, is a leak. Record the figures, and say whether the teardown after a bring-up needs `silentRestart()`. This is AD-18; it measures bring-up and teardown with no peer, not a match.
  - An after that recovers to within the noise needs none.
  - The tolerance is the owner's call. Write down the figures either way.
- Also note any bring-up that failed: an `[ERR] [LINK] bring-up failed at ...` line, or "Nearby could not start" on the screen.

**7. Device checks carried from earlier entries.**
- `## 6.5`: the receive callback writes frames into PSRAM from the Wi-Fi task, and the ring's order matters on this dual-core chip.
  - The pass is a clean round (steps 3 and 4) with no torn or reordered frame. That means no `BadSnapshot` adopt result, and no `Stale` one on a move that was not a resend.
  - The ring's overflow count is not logged, so a dropped frame shows only as a `resend`.
  - The failure is a crash or a `Guru Meditation`. The card cannot capture a crash, so note what the screen showed.
- Entry 10's D2: the host list's 1,000 ms refresh. In step 2, watch the guest's list while a second host appears nearby, if you have one, or while A cancels and hosts again.
  - Does the list follow within about a second, without flicker?
  - Does the panel look right, with no ghosting from repeated partial refreshes?

## What to record

Fill in the table at the end of the plan:
- The firmware commit and its SHA-256.
- The merged log's lines, pasted from `nearby-run-logs/merged.log`, for:
  - the lobby: the label as shown on A, B's list row, and "Player 2 joined";
  - the join;
  - a round with the rejected move;
  - Leave, with "player left".
- The stack high-water at each stop.
- The ten-cycle heap before and after, and whether `silentRestart()` is needed.
- The card log's check (step 1), and any `[DIAG]` dropped or lost lines.
- `## 6.5` and D2.
- Whether the run steps in `fork-scripts.md`'s "The run without a computer" worked as written, or the corrections.

Keep `nearby-run-logs/` with both cards' folders, and send it with the results.

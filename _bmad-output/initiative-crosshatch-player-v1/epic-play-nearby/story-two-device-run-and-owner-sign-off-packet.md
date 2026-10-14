# Closing-run packet: epic-play-nearby entry 14

This packet is the closing device run of Play Nearby: the firmware, the packages, the steps in run order, the owner's questions, and, at the end, the fields to fill. It is meant to be run with no other file open. Fill the "Fields to record" table at the end as you go and send it back with the logs. The orchestrator copies the results into `story-two-device-run-and-owner-sign-off-plan.md` (beside this file). A failure on a device becomes a new entry of the epic.

The run is cardless (owner Decisions of 2026-10-13). Each device writes its own log to its SD card, and the cards are read at the end through the device's Wi-Fi File Transfer. No computer is attached during the run. It uses two X4 Pros (epic Scope note of 2026-10-14); Sticky is not in it.

- **Device A** is `X4 Pro 2C8C` and **device B** is `X4 Pro 6E68`. The host changes from game to game, and each step names it.
- **You need:**
  - both devices with their SD cards, fully charged;
  - a phone or computer with a browser, for File Transfer before and after the run only;
  - a closed metal tin, or a switched-off, unplugged microwave oven (step 9);
  - a clock;
  - about four hours. Steps 2 and 10 are 30-minute waits.
- **Battery:** keep both devices off the charger for the whole run, since the battery figures need it. Note each device's battery percent at the start.
- **Sleep:** hold the power button until the device sleeps, about half a second. A short press does nothing, and a double press toggles the light.
- **If something goes wrong mid-run:**
  - A crash, a restart, or a "Not enough memory for Nearby. Restart the reader and try again." dialog is a finding. Note the step, the clock time and what the screen showed. A crash never reaches the card.
  - Then restart the device if it did not restart itself, and carry on from the step you were in.
  - A restart cuts that device's heap and battery windows, so name the restart in the fields.

## Firmware

The orchestrator fills these fields when it builds.

- One build from the epic head, under the build lock, with the touch log, the SD-card log and the build id:
  `PLATFORMIO_BUILD_FLAGS="-DCROSSHATCH_TOUCH_LOG=1 -DCROSSHATCH_SD_LOG=1 -DCROSSHATCH_BUILD_ID=<commit>" pio run -e x4pro`.
  - Firmware commit: `471eda60fc32e646b0e5fafb7638f6d218e4da68`, the PR's head when it was built (2026-10-14, the main checkout, under the build lock). Every later commit on the epic branch is documentation only. Each card file's `firmware` header line reads `471eda60`, its `CROSSHATCH_BUILD_ID`. The build contains the card log's hook and flush, the battery line, and the per-tap touch line (`check_flash_budget.py touch-line`).
  - File name: `crosshatch-x4pro-471eda60-diag.bin`.
  - Size: 6,270,032 bytes.
  - SHA-256: `77f9c19255c769ab7d48d26597ef147a0936f44c0e6396867ebc7c462ac30e4e`.
- **Check the SHA-256 before flashing, and flash only a file that matches.** Both devices get the same file, because a level-1 surface mismatch ends a match (AD-13).
- Flash it with CrossPoint Reader Flash Tools, or upload the `.bin` with File Transfer and use Settings' firmware update from the SD card.
- **What this diagnostics build adds:**
  - It logs at `LOG_LEVEL=2`.
  - It writes one `touch` line per tap.
  - Every 10 s it writes `[MEM] Battery: <mV> mV, <pct>%` and the heap lines: internal heap as `[MEM] Free: N bytes, Total: N bytes, Min Free: N bytes, MaxAlloc: N bytes`, and PSRAM as `[MEM] PSRAM: ...`.
  - Every line also goes to the card.
- **Release firmware for step 13** (the orchestrator fills these):
  - File name: `crosshatch-x4pro-471eda60-release.bin`. This is the same commit built with no diagnostics define, the normal `pio run -e x4pro`: 6,255,904 bytes, with no card log and no touch line.
  - SHA-256: `bafeb4cc57872a80f4d6d4e0af124b5cbf5f584ded363ccbf415f0c618a00e77`.

## Packages

`python3 scripts/pack_device_run.py <out>` packs `nearby.chgame` (its hash is in `HASHES.txt`). `python3 scripts/pack_game.py games/<id> <out>` packs each first-party game. The orchestrator fills the table.

| File | Goes on | Package hash | Bytes | KB as the file manager shows it |
| --- | --- | --- | --- | --- |
| `nearby.chgame` | A and B | `2bef3752d7af24a2` | 1,999 | 1.95 KB |
| `ultimate-tic-tac-toe.chgame` | A and B | `3aeb7cf5a0ffe519` | 5,489 | 5.36 KB |
| `battleship.chgame` | A and B | `c7a3a73bea899174` | 11,038 | 10.78 KB |
| `sudoku.chgame` | A only | `411a1048bae48675` | 42,958 | 41.95 KB |

- **Nearby** is a noughts-and-crosses fixture for two seats.
  - A tap on a free square is a move.
  - A tap on a taken square is rejected: that device shows "That square is taken" and sends nothing.
  - The black square under the board, at the bottom left, raises a script error when tapped on your own turn (step 5).
- **Uploads** arrive as `<name>.chgame.zip`. Rename each to `<name>.chgame` in the file manager; the inbox ignores `.chgame.zip`.

## Steps, in run order

**0. Before the run, on each device.**
- Flash the firmware (above).
- Settings, System, Time to Sleep: note the value you start with, then set it to "Never".
- Open File Transfer and the file manager in the browser.
  - Open `/crosshatch-log/`, select every file in it and delete them, including the running boot's file. A new file may appear at once, because the log starts its next file with a header. That is expected; leave it.
  - Upload the packages into `/games/`: A gets all four, B all but Sudoku. Rename each `.chgame.zip` to `.chgame`.
  - Note each file's size in `/games/` as the file manager shows it, and compare it with the table's last column. Any difference is a finding, not only a size of 0.
- Close File Transfer. The device restarts.

**1. Install, and the Wi-Fi refusal check.** Both devices, straight after the restart.
- **Install.** Home, Games. The device installs the packages. Check the list: Nearby, Ultimate Tic-Tac-Toe and Battleship on both, and Sudoku on A. A note popup or a `.bad` file is a finding.
- **Modes.** In Nearby, Ultimate Tic-Tac-Toe and Battleship: Options, set Mode to "Play nearby", and go back. On A, set Sudoku's Difficulty to Easy.
- **No refusal after the restart.** In Nearby, tap New game. Record that the lobby opens (Host a game, Join a game) and does not say "Wi-Fi is in use. Close it and try again.". Tap Cancel without hosting or joining, and go back to the Games list.
- **Optional, A only.** This needs a borrowed book the device cannot date: opening it shows "This book is on loan. Connect to Wi-Fi once so the device can check the date." with "Sync Clock Now".
  - Choose Sync Clock Now, and cancel the Wi-Fi list it opens.
  - Home, Games, Nearby, New game. Expected: the dialog "Wi-Fi is in use. Close it and try again.", and the log line `Nearby for nearby refused: Wi-Fi in use`.
  - Sleep A and wake it, which restarts it, then go to the Games list.
  - With no such book, record "not reachable from the UI". Every other Wi-Fi feature restarts the device when it closes.

**2. Fresh-boot idle, 30 minutes, both devices.** Leave both on the Games list, untouched, for 30 minutes. Note the start and end times. Nothing in this boot has brought the radio up.

**3. Leaving the lobby before Start.** Nearby on both.
- A: New game, Host a game. A shows its label large, "Waiting for a player" and "1 of 2 players".
- B: New game, Join a game, then tap A's row. B shows "Waiting for the host to start", and A shows "Player 2 joined".
- B: Cancel. A shows "Player 2 left".
- B: Join a game again and tap A's row. A shows "Player 2 joined".
- A: Cancel. B shows "The host left" and "Back to the list".
- Cancel back to Nearby's title screen on both. The lobby logs nothing for a leave before Start, so record what the screens showed.

**4. A fixture round, Play again, and a Leave.** Nearby; A hosts.
- **Start.** A: New game, Host a game. B: New game, Join a game, tap A's row. A: Start.
- **Moves.** A (X) moves first. B must show the move within a second or two.
- **The rejected move.** On B's turn, B taps the square A just took. B shows "That square is taken". The log has `rejected for seat 2` on B, and no `state sent` for it.
- **Play again.** Play to the end. A taps Play again; B's menu shows "Waiting for the host" until the round starts. Make a few moves.
- **The Leave.** B: Back (the pause menu), then Leave. A shows "The other player left". A: Back.
- **Idle.** Leave both on the Games list, untouched, for 30 seconds.
- **Radio off.** On B, the device that left: Nearby, New game. The lobby opens with no "Wi-Fi is in use", so the radio is off. Cancel.
- **What the log should show:**
  - On A: `abort recv reason=left seat=2`, then `nearby: Playing -> PeerGone on PeerGone` and `nearby: the other player is gone: left`.
  - On A, Back from "The other player left" logs `leave reason=left waited=0ms unacked=... copies=0`. On B, the Leave logs `copies=3`.
  - Each accepted move is `state sent ver=N` on the mover, then `state recv ver=N` and `state adopt ... result=Adopted` on the other device. A's round start is adopted as `result=RoundStarted`.
  - A clean round has no `result=BadSnapshot`, and no `result=Stale` on a move that was not resent (`## 6.5`).
  - A `resend` or `queue full` line, or `the peer's end waited N ms behind a STATE the VM did not adopt`, is a finding to note, not a failure.

**5. The script error.** Nearby; B hosts.
- B: New game, Host a game. A: Join, tap B's row. B: Start.
- B (X, first to move) taps the black square under the board, at the bottom left.
- B shows "The game stopped with an error". Its log has `[LUA] Script error: ...` (with `boom`) and `nearby stopped: ...`.
- A shows "The other game stopped with an error". Its log has `abort recv reason=script_error seat=1`.
- Back on both.

**6. Ultimate tic-tac-toe.** B hosts, so B is Player 1 (X) and A is Player 2 (O). Make every rejection tap on the tapping device's own turn: a tap out of turn shows nothing, by design.
- **Start.** B hosts, A joins, B starts.
- **Headers.** On B's turn, B reads "Player 1 (X): your move" and A reads "Player 2 (O): waiting". Note them.
- **Rejections.** On A's turn:
  - A taps a taken cell: "That cell is taken".
  - Once only some small boards are highlighted, A taps a free cell outside them: "Play in the highlighted board".
- **`## 6.12`, first observation.** While A waits, note whether A shows the playable small boards highlighted. Open HOW TO PLAY on A (the question button, top right) and read paragraph 6, "The highlighted small boards are the ones you may play in." Note whether it reads right to a waiting player, then close it.
- **Right-edge taps (AI-7).** On A's turns, tap at the very right edge of the screen, level with the right column of cells, ten times in all, over as many turns as it takes. For each tap, note whether a mark was placed, a message shown, or nothing happened.
- **`## 6.12`, second observation.** Just before the round's last move, a win or a draw, open HOW TO PLAY on the device that is waiting. Make the last move on the other device. Note what the waiting device shows: the help page over the end dialog, or the end dialog.
- **Play again, and the Leave.** B: Play again. A few moves, then A: Back, Leave. B shows "The other player left"; B: Back.

**7. Battleship.** A hosts, so A is Player 1. Note the clock time when A opens the lobby; this match is the battery cost.
- **Start.** A hosts, B joins, A starts.
- **Placement**, one seat at a time. While A places, B shows "Waiting for Player 1".
  - On A: Rotate, Random, Clear, then place the fleet and Ready.
  - On B: the same.
  - Check that neither screen ever shows the other device's ships.
- **Firing.** A shot at a square already shot at is rejected with "You already fired there", and the turn stays.
- **The end.** Play the round to its end.
- **Play again.** A: Play again. Placement starts on both devices.
- **The Leave.** A: Back, Leave. B shows "The other player left"; B: Back. Note the clock time.

**8. A sleep mid-match.** Nearby; B hosts.
- B hosts, A joins, B starts. Make two or three moves.
- Sleep B (hold the power button). A sleep is a forced exit, which sends three `ABORT(left)`, so A shows "The other player left", not "went silent".
- A: Back. Wake B and go to the Games list.

**9. Silence, and row 3.** Nearby; A hosts.
- **Set A's sleep timer.** On A: Settings, System, Time to Sleep, "1 min". Then go straight on. If A sleeps before the match starts, wake it (it restarts), go to Games, Nearby, and host again.
- **Start.** A hosts, B joins, A starts. Make a few moves.
- **Shield B.** Right after a move, note the clock time and shield B: close it in the metal tin or the switched-off, unplugged microwave oven. If neither stops the link, carry B out of range instead (another floor, or 30 m or more). Note which way you used.
- **The silence.** A should show "The other player went silent" about 10 s later; note the time on A's screen. After 30 s, take B out. B should show "The other player went silent" too.
- **Row 3.** Leave A on "went silent", untouched. It should sleep after about a minute; note whether it did, and when.
  - If it slept, wake it.
  - If it has not slept by about 2 minutes, note that and press Back to the Games list.
  - Either way, set A's Time to Sleep back to "Never".
- **End.** B: Back, to the Games list. Step 10 starts now.

**10. B's after-match idle, 30 minutes.**
- Leave B on the Games list, untouched, for 30 minutes. Note the start and end times. Steps 11 and 12 run on A meanwhile.
- At the end, on B: Nearby, New game. The lobby opens with no "Wi-Fi is in use", so the radio was off through the window. Cancel.

**11. Ten bring-ups with no peer, A only.**
- On the Games list, idle, wait 30 seconds.
- Ten times: Nearby, New game, Host a game; wait until the screen shows A's label; then Cancel, and Cancel again to the title screen. Do not Start.
- Back on the Games list, idle, wait 30 seconds.
- "Nearby could not start. Try again." on the screen is a finding; note which cycle.

**12. One-device checks, A only.**
- **Sudoku**, Solo, Easy (`## e5-run4`, AI-7):
  - **Back before the first frame.** Tap New game, and press Back at once, before the grid shows. Wait about a minute on the pause menu, then Resume. With no tap, note how long "1 min" takes to appear in the header. About a minute after Resume is right; later is a lag.
  - **The header across a pause.** Wait with no tap until the header reads "1 min". Note the minutes, press Back, wait about 2 minutes, then Resume. The header must show the same minutes as before the pause.
  - **TIMER.** MENU shows "TIMER: ON". Tap it to OFF, close the menu, and check the header shows no time. Turn it back on.
  - **NOTES AS.** MENU: note what the "NOTES AS" row reads.
  - **A sleep mid-puzzle.** Note the header's minutes, sleep A, wake, then Games, Sudoku, Continue. Note the minutes. They may go back (finding A-1, which the owner accepted).
  - **Leave.** The log gets a `VM stopped` line.
- **Battleship**, in Pass and play (`## e5-close`; Options, Mode "Pass and play"):
  - **Known fleets.** Place both fleets the same way so every square is known. Ship 1 (5 long) starts at row 1, column 1, ship 2 (4) at row 2, column 1, ship 3 (3) at row 3, ship 4 (3) at row 4, and ship 5 (2) at row 5, all across.
  - **B5a**, three tries. After a shot, tap the banner, then tap the same spot again as fast as you can. One tap should pass the banner. Record whether the second one also passed the hand-off.
  - **B5b**, three tries. Tap "I'm ready", then at once tap an unshot square. Record whether the shot was taken or dropped.
  - **B5c**, three tries. On the hand-off screen, rest a finger on the band, press Confirm with the other hand, and lift once the seat's frame is up. Record whether a shot was taken; none should be.
  - **B6.** Play the round to its end. On the end-of-round menu, look for a ghost of the last shooter's frame, then sleep A. Wake, then Games, Battleship. Note any ghost, and confirm the title screen offers no Continue.
- Set Battleship's Mode back to "Play nearby" if you like; nothing later needs it.

**13. The end, both devices.**
- Settings, System, Time to Sleep: back to the value noted in step 0.
- Open File Transfer. In the file manager, open `/crosshatch-log/` and download every file: A's into a folder `card-A`, and B's into `card-B`. Close File Transfer; the device restarts.
- Flash the release firmware (the Firmware section: check its SHA-256 first). A release build writes no card log.
- Send both folders, with the fields table below, to the orchestrator.

**14. Optional, only if you answer yes to Q2: one USB capture.** One device, on a computer with this repository.
- `python3 scripts/device_log.py capture --port A=/dev/serial/by-id/<the device's port> --out usb-capture`.
- Connect and wait for a few `[MEM]` lines. Sleep the device, wait ten seconds, and wake it. Wait for a few more lines, then stop with Ctrl-C.
- Record whether connecting reset the device, and whether the merged log has a `lost ...` and a `connected ...` line across the sleep.

## After the run: the orchestrator

```sh
python3 scripts/device_log.py merge --out nearby-close-logs A=card-A B=card-B
python3 scripts/device_log.py summary nearby-close-logs/merged.log
python3 scripts/device_log.py summary --mem-all nearby-close-logs/A.raw.log
python3 scripts/device_log.py summary --mem-all nearby-close-logs/B.raw.log
```

From these, the orchestrator fills the figures the owner cannot see on the screen. Heap figures are internal heap (the `[MEM] Free:` line, which `NearbyProbe` checks), not the `[MEM] PSRAM:` line. `Min Free` is a low-water mark for the whole boot.

- **The card log after the mid-run delete (step 0):** the first file after the delete starts with `# crosshatch-diag-log 1` and has an `[ERR] [DIAG] lost N bytes (card write failed)` notice. Any `[ERR] [DIAG] dropped N lines (buffer full)` line is also recorded.
- **Reliability:** per `LINK` session, from the `nearby:` block.
  - It covers STATEs sent and received, adopt results, and the `resend`, `silence`, `queue full`, `link refused` and ABORT totals.
  - The `GameLink` stack figure is the least `task stopped; stack high-water N bytes free` over all stops. A figure under 512 B free is a finding.
- **Silence (step 9):** on each device, the time from the last `state sent`/`state recv` line before the cut to `silence n=1`, and to `nearby: Playing -> PeerGone on PeerGone`. Acks and keepalives are not logged, so the last move is the reference.
- **Heap around the first match (row 10), both devices:**
  - "Before" is the last `[MEM] Free:` line of step 2, before any bring-up.
  - "Lowest" is the `Min Free` of the first `[MEM] Free:` line after step 4's Leave. Being a low-water mark for the boot, it also covers step 3's lobby bring-ups and step 1's install, and the drop from step 2's `Min Free` is the peak use.
  - "After" is the `[MEM] Free:` lines of step 4's 30 seconds on the Games list.
- **Heap after teardown (step 11):** `Free`, `Min Free` and `MaxAlloc` from the `[MEM] Free:` line just before step 11's first of ten `radio up` lines, and from the one after its last Cancel. Anchor on step 11's own window, not on the boot's first `radio up`.
- **Bring-up failures (step 11):** `[ERR] [LINK] bring-up failed at <step>` (the radio's steps), `[ERR] [LINK] session=... bring-up failed: <step>` (the session's), and `[ERR] [GAME] Nearby: the radio did not start for nearby`.
- **Battery:** from the `battery:` block (per boot) and `--mem-all`.
  - The Battleship match's cost runs from `Nearby: hosting battleship` / `Nearby: looking for battleship` to its Leave, on each device.
  - The idle drain compares step 2's window (A and B) with step 10's (B), in mV and % over 30 minutes.
- **`VM stopped`:** per game, for the fixture, Ultimate tic-tac-toe, Battleship and Sudoku. A stack under 512 B free is flagged for epic-api-freeze.
- **The right-edge taps:** from step 6, the count of `not sent, off the canvas` touch lines, and the taps whose line has a canvas point in the right column (entry 15's margin).
- **B5a to B5c:** the `Dropped a touch made under frame N, before seat N's first frame N` and `battleship: dropped a touch ...` lines in step 12, set against the owner's per-try notes.
- **The 400 ms, 10 s and 100 KB values** (`ReliableLink`'s resend and silence limits, and `NearbyProbe`'s threshold):
  - step 9's timing confirms the 10 s or not;
  - the `resend` lines across the run confirm the 400 ms or not;
  - Q1's answer, with the heap figures, settles the 100 KB.
- **`## e8-touch-log`:** this define-on build ran, and the card holds per-tap `touch` lines (`<id>: touch tap screen (x,y) ...`).
- **`fork-scripts.md`'s "The run without a computer":** whether it held for a run read through File Transfer, from the owner's notes on step 13 and the merge.

## The label test (`## e6pre-5`, on PR #40)

This runs on GitHub, before or after the device run. Each step needs a run that is still in progress; check that before acting.

1. The owner adds the `package-games` label to PR #40, and a "Game packages" run starts.
2. While that run is in progress, the owner adds an unrelated label. That makes a second, skipped run.
   - The pack must end `success`, not `cancelled`.
   - The skipped run must have started before the pack finished; compare their start and end times.
   - If the pack finished first, remove and re-add `package-games` and try again.
3. Later, the owner removes and re-adds `package-games`. While that pack is in progress, the orchestrator pushes a commit. The push's run must cancel that pack: it ends `cancelled`.
4. Record the three run URLs, with their start times, end times and conclusions: the pack, the unrelated label's run, and the push's run (with the pack it cancelled).

## The owner's questions

Each question shows the orchestrator's recommendation from `cross-story-review.md`. The owner answers; nothing here is decided until then.

**Q1 (review row 10): the Nearby heap threshold, 100 KB (102,400 B).** `NearbyProbe` refuses Nearby when the free internal heap is below 100 KB.
- At the smoke run, a bring-up and a match on device A took `Min Free` from 171,144 B to 97,316 B, a peak use of 73,828 B. A reader admitted at about 102 KB would then peak at about 28 KB free.
- **Recommendation:** set the threshold to this run's measured peak use plus a 30 KB margin, rounded up to a whole KiB. At the smoke run's figure that is about 103 KiB, a small raise from 100 KiB.
  - A changed value is a new entry after this run, not a change inside it.
  - If this run's peak use is under about 70 KiB, 100 KiB stands.
- Answer:

**Q2 (review row 12): the serial capture over USB (R13).** No serial capture has run against a device; every run has been cardless (`## 6.3`).
- **Recommendation:** one short capture of one device across a sleep and wake (step 14), since the tool exists and costs a cable. The other choice is to retire R13's serial path in favour of the card log.
- Answer:

**Q3 (review row 13): the rule "the host heard advertising again ends a started match".** It cannot see a host that hosts again, because the new session's `ADVERT` is dropped before the lobby sees it. It fires only on a delayed same-session copy, which a started host never sends.
- **Recommendation:** drop the rule and its grace window, and leave the end to `ABORT` and the 10 s silence. Do it in a follow-up after this epic.
- Answer:

**Q4 (review row 17): a CI build of the packet's diagnostics firmware.** No CI job compiles `x4pro` with the packet's defines, so an upstream merge into `Logging.cpp` or `main.cpp` could break only the packet build. This also answers `## 6.19` item 2, `## 6.21`, and `## e6-xr` item 4.
- **Recommendation:** yes, a CI job building `x4pro` with the packet's defines, in its own follow-up.
- Answer:

**Q5 (review row 3): auto-sleep on the "player left" and error views.** Since the fix build, a match on "The other player left", "went silent" or the error view lets the device auto-sleep, and so does the lobby on its first screen and after its wait has ended. R6, R8 and AD-21 still say "set `preventAutoSleep`".
- **Recommendation:** keep the auto-sleep, and amend the R6, R8 and AD-21 lines with the exception (`## e6-xr` item 3). Step 9's row 3 is the device check.
- Answer:

**Q6: the deferrals to confirm.** Each row confirmed stays open in `deferred-work.md` with the reason given. `## 6.3` goes with Q2.

| Item | What stays open | Recommendation | Answer |
| --- | --- | --- | --- |
| `## 6.1` (2) | The GameVM half of the no-seat-turn tests (needs a rules seam in `GameVM`); `game-canvas.md` saying what a device with several local seats shows on a remote turn | confirm | |
| `## 6.2` (forget) | A handle reused after a forget cannot be exercised until an adapter evicts peers | confirm | |
| `## 6.4` (2) | A `formats.md` section on the link (the api-docs epic); `ReliableLink`'s allocation-failure paths untested (no seam in `lib/Memory`) | confirm | |
| `## 6.5` (4) | Peer-table eviction against foreign senders; a guard on a second `create()`; a refused-send counter; the `FrameRing` orderings not pinned by a host test | confirm | |
| `## 6.6` (6; the `ADVERT` rule is Q3) | The frozen API level on the wire; a ghost seat until the silence limit; `LinkRefused`/`LinkFailed` untested; the three-seat hand-off latch; a delayed duplicate `ABORT` after a rejoin; the API level in `ACCEPT` for three seats or more | confirm | |
| `## 6.8` | With three seats or more, one refusing peer holds the staged shipment | confirm | |
| `## 6.10` (3) | Three-seat lobby screens untested; the three-seat resync overwriting "Player N left"; a lobby whose link dies shows its wait with no radio | confirm | |
| `## 6.11` (3) | A host's Cancel sends no frame, so a guest's list keeps it 3 to 4 s; no "Not your turn" message; the inbox ignores `.chgame.zip` (the serial checks are Q2) | confirm | |
| `## 6.12` (2) | Ultimate tic-tac-toe's help page over the end dialog; the waiting seat sees the playable boards highlighted, with help paragraph 6 | confirm if step 6's observations read right to you; otherwise each becomes a new entry | |
| `## 6.13` (items 1, 3, 4) | The USB drive taking the card mid-batch; `renderView`'s redundant redraw; `merge` not checking walked boots against their device's boots | confirm | |
| `## 6.13` (item 2) | Resolved by the fix build: `game-canvas.md:175-177` now says Back from "player left" sends no `ABORT` to a peer that left | none (resolved) | |
| `## 6.15` (2) | A stuck VM after a script error or watchdog stop, then Back, runs in no test; the radio stays up on the "player left" and error views until Back or sleep | confirm | |
| `## 6.16` (3) | `sim_instance_test.py` outside CI's glob; `sim.sh start`'s per-instance environment untested; the simulator branches of `NearbyRadio::open()` and `NearbyProbe::read()` untested | confirm | |
| `## 6.19` (items 1, 3, 5, 6) | The card writer's stall and card wear, unmeasured; its folder listing allocates with throwing `new` and stops at 200 files; the simulator cannot build the card log; an unmount between the writer's open and write (documented) | confirm | |
| `## e6-xr` (items 1, 2) | The light panel over a nearby match lets the device auto-sleep (the peer sees "left"); an unreadable settings file refuses Join as well as Host | confirm | |

## Fields to record

Fill in what you see as you go; the orchestrator fills the log figures from the cards.

| Step | Field | Value |
| --- | --- | --- |
| Start | Battery % on A and on B; both unplugged all run | |
| 0 | File sizes in `/games/` on each device, against the table | |
| 1 | The lobby opened after the restart (no Wi-Fi dialog); the loaned-book path's dialog, or "not reachable from the UI" | |
| 1 | Install: the four games listed on A, three on B; any note or `.bad` | |
| 2 | Start and end times | |
| 3 | "Player 2 left" on A; the rejoin; "The host left" on B | |
| 4 | The rejected move on B; Play again; "The other player left" on A; B's lobby opened after the Leave | |
| 5 | The error view on B; "The other game stopped with an error" on A | |
| 6 | The headers; both rejections; the right-edge taps (each tap's result); `## 6.12`'s two observations; Play again; the Leave | |
| 7 | Start and end times; Rotate, Random and Clear on each device; ships hidden; the rejected repeat shot; Play again; the Leave | |
| 8 | "The other player left" on A after B slept | |
| 9 | The cut time and the way used; "went silent" on A (time) and on B; row 3: did A sleep, and when | |
| 10 | Start and end times; B's lobby opened at the end | |
| 11 | Any "Nearby could not start" (which cycle) | |
| 12 | Sudoku: Back before the first frame, the header across a pause, TIMER, NOTES AS, the minutes across a sleep | |
| 12 | B5a, B5b and B5c: each of the three tries; B6: any ghost, no Continue | |
| 13 | Release firmware flashed on both | |
| 14 | Only if Q2 is yes: a reset on connect; `lost` and `connected` lines | |
| Any | Crashes, restarts, or "Not enough memory for Nearby" dialogs (step, time, screen) | |
| End | Anything in reading the cards that did not work as this packet says | |
| Questions | Q1 to Q5, and Q6's rows | |

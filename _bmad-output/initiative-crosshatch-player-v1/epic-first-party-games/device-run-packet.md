# Device-run packet: epic-first-party-games entry 5

This packet is everything the owner needs for entry 5, "Device run and owner sign-off", on an X4 Pro. It names the firmware and the packages, gives the steps in run order (install and heap first, then one part per game, then the Sudoku timing, then B7.6), the serial lines that carry each figure, which fixtures are calibrated, the owner's design judgements, and the answer tables: the three `Assumption for entry 5:` lines and this epic's deferrals. Built on 2026-10-05 from `daedbeabf8a9cf7da90ed976401a0fbb73498c8d` (`daedbeab`), the epic head when the firmware and packages were built (branch `epic-first-party-games-lane-a`).

**This packet is the single home of the results:** the owner's answers go in its tables (J1 to J6, A1 to A3, the deferral rows) and the step results, figures and log excerpts go in the "Step results" table under "What to record" at the end. Entry 5's plan, `story-device-run-and-owner-sign-off-plan.md`, only points here and records the sign-off. A device failure becomes a new story in this epic's PR, which merges only after this entry and a green CI.

Files beside this one are in `device-run-packet/`: the five packages, `HASHES.txt`, `make_sudoku_costly.py` (packs `sudoku-costly`) and `count_sudoku_costly.lua` (the host counts behind Part T's figures).

## Firmware

- **Use the entry 13 firmware for the whole run:** `firmware-x4pro-0c149819.bin`.
  - 5,936,048 B, SHA-256 `bdfd44e28b52e8f45ccd08a1c21467ee96bef015637e9af46a1ff47fb6bde800`.
  - Built on 2026-10-10 from entry 13's lane at `e14dd42f`, which the epic branch merged as `0c149819`. The merge adds no firmware source beyond entry 13's.
  - It makes `ch.time.ms()` leave out time spent in the pause menu, so Sudoku's elapsed time no longer counts a pause (owner Decision of 2026-10-09; entry 13).
  - Flash: games on against off is +256,032 B, which is +416 B over the epic base Measurement; 20,448 B remain under the limit. Static RAM is +784 B, unchanged.
  - The orchestrator sends it to the owner as a session file. **Check the received file's SHA-256 against the one above before flashing, and flash only a file that matches.**
- **The earlier firmware.** `firmware-x4pro-daedbeab.bin` is 5,935,632 B, SHA-256 `823141498d4f0493b3c104288639dfcd340bc8364332a54e04eec38062971c9e`. Parts I, U and B already run on it stand, since none of them reads the clock. Re-flash before Part S, and record which firmware each part ran on.
- **Flashing.**
  - Use CrossPoint Reader Flash Tools, or with no computer, the card. Upload the `.bin` with the device's Wi-Fi file transfer, then use Settings' firmware update from the SD card.
  - A rebuild from a checkout is not promised to be byte-identical (two builds of one source gave different bytes). Use one only if the received file is lost, and write down that you did.
  - It must be a build from entry 10 on: entry 10 raised the installer's package cap to 64 members, and Sudoku has 36.
  - The firmware prints no commit at boot. So the checks are the file's hash before flashing, that Sudoku installs (Part I step 2), and in Part S that a pause leaves the time unchanged.
- **Serial log (needed for every figure).**
  - With a computer: `pio device monitor -e x4pro` at 115200 baud, run in a loop that appends to one file: `while true; do pio device monitor -e x4pro | tee -a entry5.log; sleep 1; done`.
  - With an Android phone: a USB serial terminal (for example "Serial USB Terminal") over a USB-C cable, logging to a file. Reconnect after each wake, and keep DTR and RTS off if connecting resets the reader.
  - Each line starts `[<millis>] [<level>] [<origin>] `, so a gap between two lines is a time in ms.
  - **The port goes away at every sleep** (deep sleep resets the chip). Note each sleep's time, so a line lost to the power-down is known to be lost. The lines the forced exit logs just before the port closes (`VM stopped`, `saved ch.store`, `blank screen pushed`) may not arrive. A sleep whose lines matter (Part P) is repeated until they do.
  - The x4pro build logs at `LOG_LEVEL=2`. While the serial port is open, the firmware prints a `[MEM] Free: N bytes, Total: N bytes, Min Free: N bytes, MaxAlloc: N bytes` line (and a PSRAM line) every 10 s (`src/main.cpp`).

## Packages

All are in `device-run-packet/`. The hashes are what `python3 scripts/pack_game.py games/<id> <dir>` printed on 2026-10-05 for the tree at `daedbeab` (a second packing gave the same bytes). `sudoku.chgame` and `sudoku-costly.chgame` were packed again from `14428a56`, the merge of the cross-story fixes (`e8-xr`), whose only change to a shipped game is the Sudoku end screen in `games/sudoku/view.lua` (the level name moved above the host's end-of-round dialog). They replace the copies sent first (`3c657cfcb2e461dc` and `d964af5a1278ea7f`); the other three files are unchanged, and nothing under `src/`, `lib/` or `freeink-sdk` changed, so the firmware is still the one above. The three games are the three that the release workflow packs; `pass-store` and `sudoku-costly` exist only for this run.

| File | Packed from | Package hash | Bytes | SHA-256 of the file |
| --- | --- | --- | --- | --- |
| `sudoku.chgame` | `games/sudoku` (36 members) | `e35f23efd835450d` | 40,638 | `3e72bd57f74772d472220b7a5e32d404129be593fee515ffcdd6deed62ed435d` |
| `ultimate-tic-tac-toe.chgame` | `games/ultimate-tic-tac-toe` | `a7e63b542144938b` | 4,824 | `1d65629a50a69ae18e56d15728edcb047d6d037b44e62ac71e07d8a0a22b35aa` |
| `battleship.chgame` | `games/battleship` | `10a07de10cb14489` | 9,494 | `45c461bde92cebcb68d8b0bcf9601665312bd2c7fd3d9e7598e9d67756ffdb03` |
| `pass-store.chgame` | `python3 scripts/pack_device_run.py <dir>` (B7.6) | `5a6bbba63ef264ee` | 1,218 | `ec4e1c656ca8bc62d5d9b3b631e51cfb72cfbcd55df388f44d026963192684e4` |
| `sudoku-costly.chgame` | `python3 device-run-packet/make_sudoku_costly.py <dir>` (Part T) | `340f6c3dbb0c277b` | 40,642 | `4c0999bbfb897c4890d47ae3f785ed053c298471fd965f5274c800fc58065d34` |

- `pass-store` is the script's derived game: `pass-hidden` plus one `ch.store.set` per move, so a store flush can land in a sleep (its hash equals the one the epic-pass-and-play packet printed). The script's other twelve files are not needed here.
- `sudoku-costly` is Sudoku with one change, so HINT and CHECK can be timed on the costliest Expert grid: its `setup` returns a fixed Expert grid instead of a random deal, and its id, name and Difficulty default (`Expert`) differ so it cannot be mistaken for the shipped game. The grid is bank puzzle `0034ee8363e5` (Expert, index 59, the costliest Expert call the bank tool recorded) under the one symmetry out of 3,000 random ones that cost most (below). `make_sudoku_costly.py` documents the edits; it is a packet file, not a repository script, and nothing under `games/` changes.
- **Install through the inbox:** the inbox is the card's `/games/` folder (`INBOX_DIR`, `src/games/GamePaths.h`; the orchestrator's brief wrote `/games/inbox`, which the code does not read). Put the `.chgame` files there from a computer, then open Games on the device: it installs them ("Installing games...") and deletes each inbox file. A refused package is renamed `<name>.chgame.bad` and the launcher shows a note naming the reason.
- After an install, `/.games/<id>/.pkg` holds `v1`, then the package hash on its second line: compare it with the table.

## Fixture calibration (AI-5)

`docs/crosshatch/orchestrated-epics.md` ("Owner hand-offs") requires a fixture whose outcome depends on device timing to be calibrated on the device first, or listed as uncalibrated.

| Item | Status | Why |
| --- | --- | --- |
| `sudoku-costly`, HINT and CHECK against the 3 s watchdog (Part T) | uncalibrated: expected outcome estimated from the host ratio | Host instruction counts, measured on 2026-10-05 with a host Lua 5.5.1 built from `lib/lua/src` and a count hook of 1 over the game's own `input` (`device-run-packet/count_sudoku_costly.lua` reproduces them): the first HINT 302,708, the first CHECK 258,030, a HINT after a CHECK 45,186. They include loading the solver module and the menu handling, so they sit a few hundred instructions above the solver-only counts of Part T (302,052 = 11,023 table building + 246,397 `answer` + 44,632 `hint`). At the epic's two device rates (2.17 M instructions a second with the VM alone, the spike's figure in the D2 Decision; 670,000 a second, the research's B3 floor) the first HINT takes about 0.14 s to 0.45 s, the first CHECK 0.12 s to 0.39 s, and the later HINT 0.02 s to 0.07 s. No device time is measured yet; Part T is the calibration run. |
| `pass-store`, B7.6's forced-exit timings | uncalibrated: expected outcome estimated from logged figures, not a host ratio | `docs/crosshatch/game-canvas.md` ("The forced exit, the worst case") estimates the blank to start about 100 ms in with a joined VM, from a 91 ms resume write measured on an X4 Pro, and says the whole is unmeasured. Part P is the first run. |
| Part B step 5 (a) and (b), a second tap before or after the seat's frame is published | uncalibrated: no expected outcome estimated | The expected result of each try depends on tap timing against a 1.7 s refresh; the counts are what is recorded, and no figure is predicted. |
| the three games' rounds (Parts U, B, S) | no timing band | Nothing in a round's outcome depends on time. The dropped-touch lines are counted as a rate, with no expected figure; the owner decides at e5-r2. |
| `loop`, `slow-restart`, `timing`, the epic-pass-and-play fixtures | not in this packet | Their calibration is the earlier packets'. |

## Order and card state

1. Start from a card with no `/games/`, `/.games/`, `/.games-tmp/` and none of this run's `/.games-data/<id>/` folders (`sudoku`, `ultimate-tic-tac-toe`, `battleship`, `pass-store`, `sudoku-costly`). Remove a game left from an earlier run from the Games list (long-press its row, Remove) and delete its `/.games-data/<id>/` from a computer, so each round starts without a save and the hash check is clean.
2. Flash the firmware once, start the serial log, and keep both for the whole run.
3. Settings, Display, Sleep Screen "None" (so a ghost shows); Controls, Short Power Button Click "Sleep". Note the values you start with.
4. Parts I, U, B, S, T, P in that order. T installs a fourth game (`sudoku-costly`, removed at its end) and P a fourth again (`pass-store`), both after Part I, so the heap there is read with exactly three games installed.

## Part I: install and the heap (R12; `owner-e4-games-cap`; deferral 8.10)

1. At Home, with no game installed and no inbox file, wait for two `[MEM]` lines. Record `Free`, `Min Free` and `MaxAlloc`, and the PSRAM line. Then open Games with nothing installed ("No games found"), wait for two more `[MEM]` lines and record them: that is the Games list's own cost, so step 3's delta below is what three games cost. Back to Home.
2. Put `sudoku.chgame`, `ultimate-tic-tac-toe.chgame` and `battleship.chgame` in `/games/`. Home, Games. Watch for "Installing games..." and the three rows (Sudoku "Solo", Ultimate Tic-Tac-Toe "Pass and play", Battleship "Pass and play"). Any note popup, a `.bad` file, or a log line `OOM: installer` is a device failure: the installer's 5,888 B `Job` is one contiguous internal-heap block (entry 10; deferral 8.10), and these lines are how it would show.
3. On the Games list with exactly the three games installed, wait for three `[MEM]` lines and record all three (`Free`, `Min Free`, `MaxAlloc`, and the PSRAM line). That is the launcher's free heap for `owner-e4-games-cap`; its delta against the empty Games list is what three games cost. For deferral 8.10, `MaxAlloc` at Home before the install is the largest internal block just before `installAll` allocated its 5,888 B `Job`: a number the install fitted in, not how close it came (nothing logs the block at `installAll`).
4. From a computer, read `/.games/<id>/.pkg` for the three games (and later for the others): the second line must equal the hash in the table.
5. Return to this list after each game in Parts U, B and S and note one `[MEM]` line there each time, so a leak across games shows.

## Part U: Ultimate tic-tac-toe, pass (open)

The first-party games are laid out in a fixed 466 × 788 box. Judge the look at J5.

1. **3 taps:** Home, Games, Ultimate Tic-Tac-Toe, New game (the title screen offers New game "Pass and play"; there is no Options row, the game has no setting and one mode). Count: Games, the row, New game. The header reads "Player 1 (X) to move", the small boards the first player may play in are highlighted (the first move may go anywhere).
2. Play the round to its end (a win or a draw), tapping a cell directly, with no hand-off screen (the game is open). Check: a tap on a taken cell shows "That cell is taken"; a tap outside the highlighted boards shows "Play in the highlighted board"; the question button (top right) opens HOW TO PLAY and a tap closes it, and the page's text fits with nothing clipped (deferrals 8.1 #2 and 8.4: the real font fit).
3. **Sleep mid-round:** about ten moves in, press the power button. Wake (Home), Games, Ultimate Tic-Tac-Toe, Continue: the same board and turn. Log: a `VM stopped` line for the sleep, then `Resuming at ver N` after Continue. An open pass match shows nothing private, so no `blank screen pushed` line is expected; with Sleep Screen "None", note any ghost of the board on the panel.
4. Finish the round. At the end the header names the winner and the end-of-round menu offers Play again and Leave. Play again once: an empty board, seat 1 to move.
5. Leave. Record the `VM stopped; arena peak N bytes, stack high-water N bytes free, least at a hook N bytes` line that follows (it carries no game id: it follows the `ultimate-tic-tac-toe: ... -> Leaving` line). Note a `[MEM]` line on the Games list.

## Part B: Battleship, hidden pass

A hidden pass game: after each move that passes the turn, the mover's frame stays under a "Tap to pass" banner, then a hand-off screen ("Player N's turn", "I'm ready"), then the next seat's frame, as in epic-pass-and-play's B3. Placement is one seat at a time; each ship placed is a move that keeps the turn.

**A known fleet.** The round has two seats, both you. Place both fleets the same way, so every shot is known: ship 1 (5 long) by tapping the first square of row 1, ship 2 (4) row 2, ship 3 (3) row 3, ship 4 (3) row 4, ship 5 (2) row 5, all across from column 1 (the first tap of a ship is its first square; rotation stays "across"). The 17 squares are then rows 1 to 5, columns 1 to 5, 4, 3, 3 and 2. Each seat's first shot is a deliberate miss at row 10, column 10 (step 3), so seat 1, who fires first, wins on its 18th shot: the round is 35 shots. Keep a tally of the shots as you go (each seat's count and its hits): the deliberate repeat shot is rejected and so uses no shot, and step 5's tries fire at known squares and count. If the order drifts so that seat 2 reaches its 17th hit first, seat 2 wins and step 6 is the same with seat 2 as the winner.

1. **3 taps:** Home, Games, Battleship, New game ("Pass and play"). The hand-off screen comes first ("Player 1's turn"); "I'm ready" shows seat 1's frame ("Player 1: place ships", the 10 × 10 board, a tray of five ships, and the buttons Rotate, Random, Clear, Ready).
2. **Sleep mid-placement (R11):** place two ships, then press the power button at once. Wake, Games, Battleship, Continue: the hand-off screen names the saved turn seat (Player 1). "I'm ready": the two ships are still on the board and the prompt names the third. Place the other three, then Ready. The hand-off screen names Player 2. Place seat 2's fleet; sleep once more mid-placement, after the first ship, and Continue (seat 2's hand-off, one ship kept); finish and Ready. Log: a `VM stopped` line at each sleep and `Resuming at ver N` after each Continue; with a seat's frame on the panel (placement is one) the forced exit also logs `battleship: forced exit: blank screen pushed (half refresh)`.
3. Fire. The mover sees "Miss", "Hit!" or "You sank the <ship>!" beside the boards, the target board large and the seat's own fleet small. Make each seat's first shot at the empty square row 10, column 10 to see a miss, and on a later turn fire at a square that seat has already shot at (seat 1 again at row 10, column 10): the shot is rejected with a reason and the turn stays.
4. **Count the dropped touches (e5-r2):** from the first "I'm ready" of the firing phase until the first shots are taken, tap as you naturally would, with no attempt to be fast (about ten hand-offs), then copy the log up to that point before starting step 5, and count the lines `battleship: dropped a touch before the frame was on the panel`. Count, separately, the lines `battleship: dropped a touch that began N ms before the hand-off passed` and `Dropped a touch made under frame F, before seat S's first frame G`. Count the `battleship: hand-off screen pushed in N ms` lines (one per hand-off) from the same span. Record the three counts and the number of hand-offs. The deliberate fast taps of step 5 are counted on their own and never added to these.
5. **5.13's tap targets (e5-close; the owner's decision is that the banner and "I'm ready" are plain tap targets that act on release, even mid-refresh):** with the fleets known, make each check three times and note the result.
   - (a) Result banner: after a shot, tap the banner once, then tap once more at the same spot as fast as you can. Expect one tap to pass the banner. The second tap may pass the hand-off too: that overlap (the banner and "I'm ready" overlap on the X4 Pro) is accepted; record whether it did.
   - (b) First move after "I'm ready": tap "I'm ready", then at once tap a known square. Expect the shot to be taken if the tap came after the seat's frame was published, and dropped (with the line above) if before; record how often each.
   - (c) A contact held across the transition: on the hand-off screen, rest a finger on the band, press Confirm with the other hand, and lift once the seat's frame is up. Expect no shot: the target board is unchanged when the frame comes up.
6. **Sleep from Over after a seat's move (e5-r5):** fire at the known squares until seat 1's winning shot (its 18th, with each seat's first shot a miss). Tap through whatever the match shows until the end-of-round menu is up over the page that shows both fleets and the shot counts. Before sleeping, look for a faint ghost of the last shooter's frame under the Over page (note it, photo). Press the power button there. Expect a `VM stopped` line and no `blank screen pushed` line: the forced exit pushes the blank only while a seat's frame is on the panel (`pushForcedExitBlank`), so on Over its absence is right and the ghost check is the point. Wake and look at the panel for a readable ghost of a seat's frame (photo), then Home, Games, Battleship, and note what the title screen offers (the finished match's resume file is deleted at Over and the forced exit retries that delete, so expect no Continue row).
7. Play again from a fresh match once if time allows, then Leave. Record the `VM stopped` line that follows the `battleship: ... -> Leaving` line, and a `[MEM]` line on the Games list. Open HOW TO PLAY once (the question button, a tap closes it; it shows no board).

## Part S: Sudoku, solo

1. **3 taps:** Home, Games, Sudoku, New game ("Solo · Easy"; the title screen also offers Options, whose Difficulty cycles Easy, Medium, Hard, Expert). Count: Games, the row, New game. The page shows "Sudoku - Easy", the 9 × 9 grid, a 3 × 3 digit pad, and to its right NOTES, ERASE, UNDO and MENU.
2. Play the round with the controls: tap a cell, then a digit. Along the way:
   - NOTES on, then marks in a few cells (dots first, then MENU, "NOTES AS: DIGITS"), and a focused digit (tap a pad key with no cell selected);
   - MENU: SHOW REMAINING and SHADE PEERS are on by default; turn each off and on again (each choice is remembered on this device);
   - a digit that clashes with a peer is marked; ERASE and UNDO work;
   - MENU, HOW TO PLAY opens a page, and a tap closes it (this loads the `help` module in the middle of play: any error view naming "script recursion too deep to load a module" is a failure: deferral 8.4 #1);
   - HINT once and CHECK once (the timing is Part T's).
   Look at the panel for J1 to J5 now, while the notes and the shading are up (J6 is the Sticky, which this run cannot answer).
3. **Sleep mid-round:** with some digits and notes placed, press the power button. Wake, Games, Sudoku, Continue: the grid, digits, notes and elapsed time are back; the selection, the focused digit and a HINT or CHECK mark are gone (a resume is not an edit; the epic's D5). Log: a `VM stopped` line for the sleep (no `blank screen pushed`: a solo match shows nothing private) and `Resuming at ver N` after Continue.
4. Solve the grid. "Solved", "Time m:ss" and the band appear, and, since you used HINT, "No best time after a hint". The "Best m:ss" line shows only for a solve with no HINT: it is not checked on the device unless you also play one more Easy round with no HINT (Play again), which is optional.
5. Leave. Record the `VM stopped` line that follows `sudoku: ... -> Leaving`, and a `[MEM]` line on the Games list.

## Part T: HINT and CHECK on the costliest Expert puzzle (R12, B3)

**Why this puzzle:** `games/sudoku/puzzles.lua` files each puzzle under the hardest technique the solver needs and keeps one only if its worst single call, the first HINT or CHECK on it, costs at most 1,000,000 instructions. The costliest kept Expert puzzle in the orientation the bank tool graded is `0034ee8363e5` (index 59 of 100): 224,973 instructions. The game deals each puzzle under a random symmetry (digit relabelling, band, stack, row and column swaps, transposition), and the symmetry changes the cost; measured on 2026-10-05 over 3,000 random symmetries of this puzzle, the costliest found was 302,052 (11,023 table building, 246,397 `answer`, 44,632 `hint`), 34% above the bank's figure (in a smaller sample, all 100 Expert puzzles at 40 symmetries each, the costliest call was also this puzzle's, 280,243). So 302,052 is the worst found, not shown to be the worst possible: the other 99 Expert puzzles got 40 symmetries each, and the Easy, Medium and Hard puzzles none. Both figures are far under the 1 M cap and the 2 M budget. **The shipped Sudoku cannot be forced** to this puzzle: `setup` picks the puzzle and the symmetry at random, and nothing logs which puzzle was dealt. So Part T uses `sudoku-costly`, which deals that costliest symmetry found every time; and step 6 times the shipped game on a random Expert deal as a cross-check, with the grid recorded.

The grid `sudoku-costly` deals (0 is empty; 26 clues, 55 empty cells), and its one solution (the game's solver and a separate backtracking search agree):

```
grid           solution
4 0 0 0 0 7 0 0 0     4 6 3 9 2 7 8 1 5
0 1 5 0 0 0 7 2 0     9 1 5 3 4 8 7 2 6
0 0 0 0 1 0 0 0 3     8 7 2 5 1 6 4 9 3
0 0 6 7 0 0 0 8 0     1 9 6 7 3 2 5 8 4
0 5 0 0 0 1 0 0 9     3 5 8 4 6 1 2 7 9
2 0 0 0 0 5 0 6 0     2 4 7 8 9 5 3 6 1
0 0 4 0 5 0 0 0 8     7 2 4 1 5 9 6 3 8
6 0 0 0 8 0 9 0 0     6 3 1 2 8 4 9 5 7
0 0 9 6 0 0 0 4 0     5 8 9 6 7 3 1 4 2
```

On the host this grid's first HINT selects the cell at row 5, column 5 and reads "Only cell (row)"; a CHECK on the untouched grid reads "All correct".

**How the time is read.** The firmware logs no tap and no call time (a call over 3 s logs `a call ran over 3000 ms; stopping the VM` and shows the error view). So time each call from a phone video of the panel with a finger visibly on the key: from the finger touching the key to the first visible change. That time holds the touch, the call and the start of the fast refresh (about 0.67 s on this panel in the epic-pass-and-play log), so it is an upper bound on the call. To take the touch and the refresh out, first time a **control** the same way, three times: a tap on MENU (the panel opens with no solver call), and subtract its median from each HINT or CHECK time; call the difference the call's time. The pass bar is the one the epic set (D2): no `a call ran over 3000 ms` line, no error view, and a call time under 1.5 s (half the watchdog); over 1.5 s the bank's cost cap is lowered (D2). Time each of steps 2 to 4 three times where it can be repeated (a resume repeats it by sleeping again), at 60 frames a second if the phone has it (30 otherwise), and record the raw video times, the control and the difference. Be plain about what this can show: a pass or fail against the 3 s watchdog and the 1.5 s bar. Touch latency and the refresh are as large as the 0.02 to 0.45 s the estimates predict, so it cannot tell the 2.17 M from the 670,000 instructions a second, and the rate stays uncalibrated unless the numbers separate clearly. Cross-check with the matching `sudoku-costly: frame N pushed in M ms` line.

1. Install `sudoku-costly.chgame` (Part I's heap readings are already taken). **3 taps:** Home, Games, Sudoku costly, New game (its Options row offers Difficulty, which does nothing here: the deal is fixed). It deals the grid above at once. Check the grid against the picture, then time the control: tap MENU and close it three times.
2. **First CHECK on a fresh VM:** tap MENU, then CHECK. The message reads "All correct". Time it (host cost 258,030: it pays the answer).
3. **HINT after the CHECK:** MENU, HINT. Row 5, column 5 is selected, with "Only cell (row)". Time it (host cost 45,186: the answer is cached).
4. **First HINT on a fresh VM:** press the power button, wake, Games, Sudoku costly, Continue (a resume starts a new VM and drops the cached answer), then MENU, HINT. Time it (host cost 302,708: it pays the answer and the hint). This is the worst call found.
5. Heap and a solve at Expert: tap MENU, make sure SHADE PEERS is on and set "NOTES AS: DIGITS", then FILL NOTES (every candidate in every empty cell, the heaviest frame; look at J2 to J4 here); tap a cell and a pad key to focus a digit; UNDO once (it restores the notes the fill replaced). Then enter the solution above, cell then digit, for the 55 empty cells. After about 20 digits, CHECK reads "All correct"; then enter one deliberate wrong digit: CHECK reads "1 wrong digit", and HINT reads "Wrong digit" and selects it; fix it and finish. The grid ends on "Solved". Any out-of-memory fault (an error view naming memory, a `Script error:` line) is a device failure (Assumption A2). Leave and record the `VM stopped` line that follows `sudoku-costly: ... -> Leaving`.
6. **The shipped Sudoku at Expert:** Games, Sudoku, Options, Difficulty to Expert, Back, New game. Time the first CHECK, then the first HINT after a sleep and Continue, as in steps 2 to 4, and photograph the dealt grid (which puzzle it was cannot be read off a photograph: the orchestrator can match it against the bank under every symmetry offline, if the cross-check matters). Leave and record the `VM stopped` line. Then set Difficulty back to Easy, and Remove "Sudoku costly".

## Part P: B7.6, a sleep with a dirty store (`pass-store`; e6pre-13)

The forced exit now writes the resume file, retries the delete and flushes `ch.store` before it pushes the blank (e6pre-10); B7.6 is the first device run of that order. Put `pass-store.chgame` in `/games/`, open Games (it installs), and read its `.pkg` hash.

1. Pass store, New game ("Pass and play"). "I'm ready": seat 1's frame ("Player 1", "Player 1's secret: apple", "Moves: 0").
2. **The dirty store.** Each move calls `ch.store.set`, and `GameSaveStore::flushIfDue` writes a dirty store only when 5 s have passed since the store's last write, which is the match's start until the first `pass-store: saved ch.store` line. A move made more than 5 s after that is flushed on the next loop pass; one made within 5 s of it stays dirty until the 5 s are up. So press the power button within 5 s of the match's start with a move made in that time (the first move's own flush, due at 5 s, may otherwise log `saved ch.store` normally, which is not an error), by one of two routes:
   - **From the start:** tap New game, then "I'm ready" at once (the hand-off screen takes about 1.7 s to push), tap the frame (a move) and press power with seat 1's frame under its banner, all inside 5 s of New game. If a `pass-store: saved ch.store` line appears before you press power, the 5 s passed first: try the second route.
   - **After a flush:** once a `saved ch.store` line has appeared, make a move within 5 s of it and press power at once.
   Try each route up to five times and write down each attempt's gap from the log. If neither can be hit by hand (the hand-off pushes are about 1.7 s each), write "not reachable by hand" with the attempts: B7.6 then stays unanswered, and the owner chooses between a longer store interval for a test build and accepting the host-only evidence.
3. Expect, in this order: `Exiting activity: GameMatch`, `VM stopped; arena peak ...`, `pass-store: saved ch.store (N bytes)` (the dirty store), then `pass-store: forced exit: blank screen pushed (half refresh)`. A second `saved resume.bin` line need not appear: the last move's resume file is written when the turn passes. A line `pass-store: forced exit past 1500 ms; skipped the resume write` (or `the resume.bin delete`, or `the ch.store flush`) is a finding for the owner (deferral 5.6, resolved by e6pre-10 in its order, reopened by such a line): the alternatives are a start-time bound for the blank on its own thread, or an async push.
4. Compute the gaps from the `[millis]` prefixes: `Exiting activity: GameMatch` to `saved ch.store`; `saved ch.store` to `blank screen pushed` (the half refresh, 1,654 ms in the epic-pass-and-play log); and `Exiting activity: GameMatch` to `blank screen pushed`. The estimate (unmeasured as a whole) is about 100 ms from the exit to the blank's start with a joined VM, plus the store write (42 to 96 ms in the epic-pass-and-play log), then the half refresh (1,654 ms there): about 1.8 s in all. Any gap or stall far above that, a card step that took over 1,000 ms, or a missing `saved` line is the "card stall" the ticket asks about.
5. Wake, Games, Pass store, Continue: the hand-off screen names the seat whose turn it was after the last move, and "I'm ready" shows its frame with the move count of the moves made. Leave. Then Remove "Pass store".

## Part Z: end of the run

Put back what the run changed: Settings, Display, Sleep Screen and Controls, Short Power Button Click to the values you noted in "Order and card state"; the games' remembered toggles (SHOW REMAINING, SHADE PEERS, NOTES AS) and Difficulty to their defaults if you changed them; Remove any game still installed that you do not want (`pass-store` and `sudoku-costly` were removed in Parts P and T); and delete the `/.games-data/` folders of this run's games from a computer if the card goes back to normal use. Send the photos and videos with the results and name each by its step id; save the serial log as `entry5.log` whole.

## Serial lines that carry each figure

| ID | Measure | From line | To line | What decides |
| --- | --- | --- | --- | --- |
| L1 | Each game's heap and stack | `VM stopped; arena peak N bytes, stack high-water N bytes free, least at a hook N bytes` after the game's `-> Leaving` line | n/a | Record three numbers per game. A fault or an error view naming memory is a failure. The arena is 475,136 B (a 448 KiB Lua region and a 16 KiB reserve); the Lua heap cap is 256 KiB of requested bytes. The simulator's headers are 16 B and the device's 8 B (`ArenaAllocator.h`), so the device peak should read lower than the simulator's 318,944 B for Sudoku (unmeasured). **A stack figure under 512 B free reopens the Lua C-stack decision** (epic-api-freeze's pre-freeze checklist; the entry-14 run read 1,412 B at the high-water and 1,652 B at a hook). |
| L2 | Launcher free heap | `[MEM] Free: N bytes, Total: N bytes, Min Free: N bytes, MaxAlloc: N bytes` | n/a | Record `Free`, `Min Free`, `MaxAlloc` and the PSRAM line, at Home (no game), on the Games list with three games, and after each game. The figure goes to `owner-e4-games-cap`. |
| L3 | Dropped touches | `<id>: dropped a touch before the frame was on the panel` | n/a | A count over a span with the number of `<id>: hand-off screen pushed in N ms` lines. No pass or fail bar (e5-r2); the owner decides whether to accept the touch once the VM has the seat's frame. |
| L4 | B7.6's forced exit | `Exiting activity: GameMatch` | `saved resume.bin`, `saved ch.store`, `forced exit: blank screen pushed (half refresh)` | Part P step 4. |
| L5 | HINT and CHECK | the video | the video | Part T; the log's `frame N pushed in M ms` and any `a call ran over 3000 ms` line. |
| L6 | Install | `Installing games...` on the panel, `OOM: installer` in the log | n/a | Part I step 2. |

## Owner judgements on the panel (the Decisions of 2026-10-05)

Look at each on the X4 Pro's panel, with Sudoku's SHADE PEERS on and digit notes on (Part S step 2), and answer in the last column. The simulator shots to compare with are in `story-sudoku-screenshots/` and `story-466-canvas-screenshots/` (the latter at 466 × 788, the X4 Pro's canvas), and the mock `notes-look-mock/schemes-now-vs-swap.png`. A poor result becomes a new story in this epic's PR.

| ID | Decision | What to look at | Owner's answer |
| --- | --- | --- | --- |
| J1 | Clue legibility (Assumption A1): a clue is a black numeral on the 25% grey ground, no longer white on 50% | Do all nine digits, 1 2 4 included, read at arm's length in normal light, in a normal cell and in a cell SHADE PEERS shades (a clue keeps its light ground there)? | |
| J2 | Digit notes in three bold sets of 12 × 16 px: G grey on white (a normal cell), B black on white (the focused digit's note in a normal cell), H white on a 50% checker (a shaded cell), the focused digit's note there inverted | Does each set read, and does the focused digit stand out, in a normal cell and in a shaded one? | |
| J3 | The selected cell with digit notes gets a 3 px black frame (the third pixel lies only in the notes' blank margin) | Is the frame visible, and is no glyph clipped by it (the bottom row 7, 8, 9 especially)? | |
| J4 | The swapped greys: the player's own digits in a shaded cell are black on 50%; the focused digit's copies on the board are white on solid black | Do they read, and does the 50% ground hide the H note tiles' edges (no 1-pixel checker seam around a tile: deferral 8.9 #2, the dither phase assumes the origin's x plus y is even, (7, 9) on the X4 Pro)? | |
| J5 | The fixed 466 × 788 layout on the X4 Pro's 466-pixel canvas, 50 px Sudoku cells | In all three games (Ultimate tic-tac-toe's board and HOW TO PLAY, Battleship's boards, buttons and Over page, Sudoku's grid, pad, rail, notes, menu and pages): is anything clipped at an edge, overlapped, or too small to tap? | |
| J6 | The Sticky, if the owner has one (the same layout centred in 474 px, 4 px of white each side; cells, tap targets and note tiles the same) | Same look, and no seam around an H tile (origin (3, 9))? **Not answerable this run:** it needs a `sticky` firmware, which this packet does not build (the orchestrator decided not to). The packages are the same; the owner-facing item is whether to run a later Sticky pass. | Not answerable this run |

## Owner answers: the `Assumption for entry 5:` lines

Every such line in `epic-first-party-games.md`, Notes; "line" is its line number there at `daedbeab`.

| ID | Line | Summary | Checked by | Owner's answer |
| --- | --- | --- | --- | --- |
| A1 | 112 | The owner judges Sudoku's clue legibility on the X4 Pro's panel; if the clues read poorly, the fix is a new story in this epic's PR. The Decision of 2026-10-05 (line 116) moved it to the swapped greys: black on 25%, no longer white on 50%. | J1 to J4 | |
| A2 | 113 | Each game's heap fits the device: Sudoku's live Lua heap about 117 KB against the 256 KB cap and a simulator arena peak of 318,944 B (at `e84e5fa3`); Sudoku's `solve-easy` round once ran out of memory for a reason never explained. The run records each game's `VM stopped` line and plays Sudoku through a HINT, a CHECK, a FILL NOTES and a solve at Expert; an out-of-memory fault there is a device failure. | L1; Part T step 5; Part S | |
| A3 | 120 | The installer's 5,888 B `Job` allocates on the device's heap with the three games installed; a failed install of any of the three packages is a device failure. | Part I steps 2 to 4; L2, L6 | |

Also from the ticket, recorded for their owners, not fixed here: whether a stack high-water under 512 B free reopens the Lua C-stack decision (epic-api-freeze's checklist; L1), and whether B7.6 shows a card stall (Part P step 4).

## Owner answers: this epic's deferrals

The open items in `_bmad-output/implementation-artifacts/deferred-work.md` under `## 8.1` to `## 8.4`, `## 8.8` to `## 8.11`, `## e5-close`, `## e6pre-13`, `## e5-r5`, `## e5-r2` and `## owner-e4-games-cap`. Items marked "Resolved" are left out: 8.1 #3, 8.2 #1, 8.3 #1 and #3's text (its residue is row 8.3 #3 below), and `## e5-close`'s V3 (resolved by entry 7: `PassResumeTest`, host). "Checked here" says what this run shows; "Confirm" asks the owner to keep the item deferred as written, or reassign it.

| Section, item | Summary | Status, trigger | Checked here | Owner's answer |
| --- | --- | --- | --- | --- |
| 8.1 #1 | A rejected move after the round is over, or a malformed one, gets "Play in the highlighted board" in Ultimate tic-tac-toe | Deferred (low): the device delivers no input once the round is over; a "Round is over" reason is the owner's wording call | Confirm | |
| 8.1 #2, 8.4 #4 | Ultimate tic-tac-toe's HOW TO PLAY fit was checked at stand-in metrics and has no paging for a narrow canvas | Deferred (low): the 466 and 474 box is now checked in the harness; real font fit shows on a device | Part U step 2, J5; Confirm | |
| 8.3 #2, 8.4 #5 | No committed check draws Sudoku's bank-wide worst frame (1,293 commands by an uncommitted count against the 2,048 limit) | Deferred (low); a bank or notes-drawing change reopens it | Part T step 5 (FILL NOTES with SHADE PEERS on at Expert draws without a fault); Confirm | |
| 8.3 #3 residue | Still proven by screenshots alone: the colours and coordinates of strokes and fills, the clue and focus grounds, the selection frame, the pad key's focus frame, the NOTES inversion, the MENU, HOW TO PLAY and end pages | Superseded in part by the greys swap; an eye check | J1 to J5 | |
| 8.4 #1 | The load-nesting probe sees only main's own load; a module required in a function body that itself requires another at load is not probed | Deferred (low); "entry 5's device run is where a lazy chain would show" | Sudoku loads `solver`, `view`, `help` and `puzzles` lazily: no "script recursion too deep" error through Parts S and T | |
| 8.4 #2 | The instruction margin of Sudoku's `toggles` round is unmeasured (host harness only) | Deferred (low): the sandbox has no counting hook | Confirm | |
| 8.4 #3 | Same as 8.1 #1 | Deferred (low); "the owner confirms at entry 5" | Confirm | |
| 8.4 #6, 8.8 #1 | The games check and `ctest -L games-check` are documented only in `test/game_script/first_party/README.md` and a CI comment, not in AGENTS.md or `docs/contributing/` | Deferred (low): AGENTS.md is an agent-context file, the owner's | Confirm | |
| 8.4 #7, 8.11 #2 | Sudoku's `checks.lua` VM has little heap headroom: about 6.5 KB (8.4) and, re-measured at 8.11, about 3.5 KB under the 262,144 B cap | Deferred (medium): a larger bank or more checks may turn the required games check red | Confirm | |
| 8.4 #8 | The instruction margin of Battleship's `draw-commands` round is unmeasured | Deferred (low) | Confirm | |
| 8.9 #1 | Nothing in CI runs `make_note_images.py`; the baked phase of the 27 PNGs is pinned only by the tool's own `--check` and a manual pixel check | Deferred (low) | J4 (a seam would show as a checker break on a shaded notes cell); Confirm | |
| 8.9 #2 | The notes tiles' dither phase assumes the origin's x plus y is even, which a game cannot read: (7, 9) on the X4 Pro, (3, 9) on the Sticky; confirmed on the simulator only | Deferred (low); "settle it in entry 5's device run" | J4 for the X4 Pro; the Sticky half is not answerable this run (J6) | |
| 8.9 #3 | The X4 Pro's 466 × 788 canvas was exercised by no simulator run or host canvas | Built since by entry 11 (the games check at both canvases, a simulator run with the X4 Pro's insets); the device is the last check | J5 | |
| 8.9 #4 | On the Sticky's 51 px cells the 3 px frame overpainted a note glyph row | Settled by the fixed 466 box (50 px cells everywhere, the owner's Decision of 2026-10-05) | J3 on the X4 Pro (50 px cells there too); the Sticky is not answerable this run (J6) | |
| 8.10 | The installer's `Job` is one contiguous internal-heap block, now 5,888 B (was 4,096 B); whether a fragmented S3 heap can still allocate it is unmeasured | Deferred (medium, unverified): `installAll` logs `OOM: installer` on failure | Part I steps 2 to 4 (A3) | |
| 8.11 #1 | The X4 Pro's inset values {9, 7, 3, 7} are typed in three places nothing ties together (the SDK profile, the games check's `CANVAS_466`, the simulator shim) | Deferred (medium): an SDK bump could change them | J5 shows the device's real canvas; Confirm | |
| 8.11 #3 | Nothing in CI proves the simulator's X4 Pro shim is in effect | Deferred (low) | Confirm | |
| `## e5-close` 5.13 | Device check of ticket 5.13's plain tap targets: a fast double tap on the Result banner, the first move after "I'm ready", a contact held across the transition | Owed since epic-pass-and-play | Part B step 5 | |
| `## e5-close` leak | The abandoned VM leaks 1,240 B on the device, not the 1,032 B the earlier records say | Deferred; trigger: the next change to `GameVM::abandon` or the arena, or a device report | Not run in this packet: it needs `loop.chgame`'s "Stuck in one C call" (`pack_device_run.py` writes it). The trigger names the next device run, which is this one: the owner chooses to run it now or keep it deferred | |
| `## e5-close` F13 | The launcher row shows no sign of a save, and New game over a save relies on a `peek` cached when the title screen opened | Deferred (unverified) | Not run in this packet (it needs a save to appear after the title screen's `peek`). The trigger names the next change to that path or a report: the owner chooses to run it now or keep it deferred | |
| `## e5-close` D2, D3, D4, AI-10 | Duplicated mode mappings and style blocks; the same package hash and builders hard-coded across test suites | Deferred to epic-play-nearby's refactor sweep | Confirm | |
| `## e6pre-13` | B7.6: a sleep with a dirty store on an X4 Pro; record when the blank starts and whether the resume write and the `ch.store` flush land | Owed since e6pre-10 | Part P | |
| `## e5-r5` (1) | The same B7.6, still open after e6pre-10's reordering | Owed | Part P | |
| `## e5-r5` (2) | A fast refresh of the Over menu, or of a pause menu opened from the hand-off, over a panel that held a seat's frame could leave a faint ghost | Open; no device run of a sleep from Over after a seat's move | Part B step 6 | |
| `## e5-r2` | A tap after "I'm ready" but before the VM publishes the next seat's frame is dropped (the owner decided on 2026-10-03 to keep dropping it); the line is `dropped a touch before the frame was on the panel` | Trigger: a device log showing the line often after "I'm ready" | Part B step 4, L3 | |
| `## owner-e4-games-cap` | Replace the launcher's in-RAM listing and its 64-game cap with an on-card index | Stays deferred (its trigger is a games epic after epic-play-nearby; three games are far from the cap) | Part I (L2): the launcher's free heap with three games installed is the figure added to its entry | |

## Release dry run (R13): the orchestrator fills this in

**Passed, 2026-10-05, on the head that carries the final packages.** The run is: trigger `crosshatch-release.yml` (the "Fork release" workflow) from GitHub with `dry_run` on, on the epic head. The workflow lists the packages and uploads them as a run artifact; it creates no tag and no release. A dry run may use any branch (the workflow's header says so), so the epic branch qualifies.

History:
- Run 37291529964 on `daedbeab` failed at "Pack the games" on a bug in the release script's packer path, which entry 8.12 fixed (`54c2a4ea`).
- Run 37294652408 on `42b084bb` passed, and its packages matched the first `sudoku.chgame` (`3c657cfcb2e461dc`) byte for byte.
- The cross-story fixes (`e8-xr`, merged as `14428a56`) then changed `games/sudoku/view.lua` (the end screen), so the dry run ran again on the head below.

- Run link: https://github.com/CarpeTelam/crosshatch-player/actions/runs/37302020967 (artifact `fork-release`, kept 7 days)
- Commit the run built: `305fd677`, the epic head after `e8-xr` and the repack. It is valid while `games/**`, `scripts/pack_game.py`, `src/**`, `lib/**` and the `freeink-sdk` pointer stay as they are there; the later merge of `develop` (`715e67c7`) changed only `_bmad-output/`.
- Date and conclusion: 2026-10-05, 11:17 to 11:31 UTC, success.
  - "Build and check the release envs" built and checked `crosspoint-1.6.5-ch.4-x4pro.bin` (5,893,584 B) and `crosspoint-1.6.5-ch.4-sticky.bin` (5,782,704 B); both "passed".
  - "Pack the games" packed the three games.
  - "Tag and publish" was skipped, as a dry run does.
  - Those release `.bin` files are the `gh_release` envs, not the `x4pro` build this packet flashes. Their size and hash differ from the firmware above by design, and their hash differs between runs because the build embeds its run.

The release's package table (`| Package | Package hash | SHA-256 |`) against this packet's. The package hash must be equal; the SHA-256 of the file is equal only when the runner's zlib deflates the same bytes as this machine's.

| Package | This packet's package hash | The dry run's package hash | This packet's SHA-256 | The dry run's SHA-256 |
| --- | --- | --- | --- | --- |
| `sudoku.chgame` | `e35f23efd835450d` | `e35f23efd835450d` | `3e72bd57f74772d472220b7a5e32d404129be593fee515ffcdd6deed62ed435d` | `3e72bd57f74772d472220b7a5e32d404129be593fee515ffcdd6deed62ed435d` |
| `ultimate-tic-tac-toe.chgame` | `a7e63b542144938b` | `a7e63b542144938b` | `1d65629a50a69ae18e56d15728edcb047d6d037b44e62ac71e07d8a0a22b35aa` | `1d65629a50a69ae18e56d15728edcb047d6d037b44e62ac71e07d8a0a22b35aa` |
| `battleship.chgame` | `10a07de10cb14489` | `10a07de10cb14489` | `45c461bde92cebcb68d8b0bcf9601665312bd2c7fd3d9e7598e9d67756ffdb03` | `45c461bde92cebcb68d8b0bcf9601665312bd2c7fd3d9e7598e9d67756ffdb03` |

All three package hashes and all three file SHA-256 values are equal: the runner packed byte-identical files.

## What to record

In this packet (the single home of the results). The owner's answers go in the J, A and deferral tables above; everything else goes in the "Value recorded" column of this table and in the "Step results" table after it (R12's list):

| Item | What to write | Value recorded |
| --- | --- | --- |
| Firmware | The commit and `firmware.bin` SHA-256 flashed (this packet's are under "Firmware"; the file arrives as a session file and its hash is checked), and the hashes `/.games/<id>/.pkg` shows for the three games, `pass-store` and `sudoku-costly` | |
| Steps | Pass or fail for each step in the Step results table below | |
| Taps from Home | The tap count for each game's first round (at most 3) | |
| `VM stopped` | The line for each of Ultimate tic-tac-toe, Battleship, Sudoku, and `sudoku-costly` (arena peak, stack high-water, least at a hook), and whether any stack figure is under 512 B | |
| Launcher free heap | The `[MEM]` lines at Home with no game, on the Games list with three games, and after each game (`Free`, `Min Free`, `MaxAlloc`, PSRAM) | |
| Dropped touches | The count of `dropped a touch before the frame was on the panel` after "I'm ready", with the hand-off count over the same span, and the other two dropped-touch counts | |
| 5.13's checks | The result of B5 a, b and c, three tries each | |
| Sleep from Over | Whether a ghost of a seat's frame showed before or after the sleep (photos) | |
| HINT and CHECK | The three times on `sudoku-costly` (first CHECK, HINT after it, first HINT after a resume), each as the raw video time, the MENU control and the difference, the same on the shipped Sudoku's random Expert deal with its first grid row, and whether any run hit the 3 s watchdog | |
| B7.6 | The log lines of Part P step 3 with their `[millis]`, the three computed gaps, and any `skipped` or `forced exit past 1500 ms` line | |
| Install | Whether all five packages installed with no note and no `OOM: installer` line | |
| Faults | Any reset, watchdog banner, `abandoning it`, `script recursion too deep`, `OOM`, or `Script error:` line | |
| Judgements | The owner's answers to J1 to J6, A1 to A3, and each deferral row | |
| Dry run | The release dry run's link and its package table, matching this packet's hashes | |
| Failures | Each device failure as a new story in this epic's PR | |

### Step results

One row per step: pass or fail, the log excerpt that shows it (with its `[millis]` prefix), and notes. A step not run says why.

| Step | Result | Log excerpt | Notes |
| --- | --- | --- | --- |
| I1 | | | |
| I2 | | | |
| I3 | | | |
| I4 | | | |
| I5 | | | |
| U1 | | | |
| U2 | | | |
| U3 | | | |
| U4 | | | |
| U5 | | | |
| B1 | | | |
| B2 | | | |
| B3 | | | |
| B4 | | | |
| B5a | | | |
| B5b | | | |
| B5c | | | |
| B6 | | | |
| B7 | | | |
| S1 | | | |
| S2 | | | |
| S3 | | | |
| S4 | | | |
| S5 | | | |
| T1 | | | |
| T2 | | | |
| T3 | | | |
| T4 | | | |
| T5 | | | |
| T6 | | | |
| P1 | | | |
| P2 | | | |
| P3 | | | |
| P4 | | | |
| P5 | | | |
| Z | | | |

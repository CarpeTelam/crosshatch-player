# Device-run packet: epic-pass-and-play entry 11

This packet is everything the owner needs for entry 11, "Device run and owner sign-off", on an X4 Pro. It names the firmware and packages, gives the steps in run order (epic 4's open recheck first, then pass-and-play), the serial lines that carry the timings, which fixtures are calibrated, and two answer tables: the `Assumption for entry 11:` lines and this epic's deferrals. Built on 2026-10-02 from `6cf7f65b`.

Record the results in entry 11's plan, `story-device-run-and-owner-sign-off-plan.md` in this folder (the owner or the next session creates it), as "What to record" at the end says. A device failure becomes a new story in this epic's PR (CarpeTelam/crosshatch-player#24), which merges only after entry 11 and a green CI.

## Firmware

- **Commit:** `6cf7f65b`, the head of CarpeTelam/crosshatch-player#24 (branch `claude/tender-rubin-vhrc0p`). Its firmware sources are identical to the merge `92af3173`; the last commit changes two docs only.
- **Download it:** `firmware-x4pro.bin` from run 37036393728, built at `6cf7f65b`: https://github.com/CarpeTelam/crosshatch-player/actions/runs/37036393728/artifacts/11240760847. #24's "Firmware builds" comment is rewritten for each new head, so it links a later build once the branch moves. A build of a later head whose commits since `6cf7f65b` change only `_bmad-output/` is the same firmware (`79e7683d`, this packet's commit, is one). Extract the `.bin` from the ZIP and flash it with CrossPoint Reader Flash Tools. The artifact's expiry date: unknown.
- **Or build and flash it:** from a checkout at `6cf7f65b`, run `git submodule update --init --recursive`, then `pio run -e x4pro -t upload`.
- **Serial log (needed for every timing):** `pio device monitor -e x4pro` (115200 baud), with `--filter log2file` or piped through `tee entry11.log`. Start it before the first step and keep it running. The x4pro build logs at `LOG_LEVEL=2`, so `DBG` lines are on. Every line starts `[<millis>] [<level>] [<origin>] `, so a gap between two lines is a time in ms. A game's own `ch.log` lines carry the game id as origin, for example `[51234] [INF] [pass-hidden] tap for seat 1`.
- **Size at this commit** (`check_flash_budget.py`'s four steps, x4pro, games on minus off, 2026-10-02): +253,424 B flash (5,933,264 − 5,679,840) and +784 B static internal RAM. Against the epic base `c1902721` that is +19,984 B flash and +0 B static RAM, within this epic's 20,000 B / 32 B share. The flash gate is 270 KiB (276,480 B), 23,056 B above this build; the static-RAM gate is 1,024 B.

## Packages

Not committed. `python3 scripts/pack_device_run.py <out-dir>` writes the thirteen files below and `HASHES.txt`. The `Crosshatch game packages` workflow runs it on a pull request labelled `package-games` (artifact `game-packages`, kept 30 days): #24 carries the label, so download the artifact from the latest `Crosshatch game packages` run on the PR's Checks tab. Locally, any checkout with Python 3 works. The hashes below were printed on 2026-10-02 (rerun: same); `slow-restart`, `pass-title` and `pass-store` were first hand-packed and their hashes match the script's.

| File | Game (name on the row) | Hash | Used in |
| --- | --- | --- | --- |
| `counter.chgame` | `counter` (Counter) 1.0.0 | `e8c3ac8dfb646d3b` | A (R1, R2), B1 |
| `loop.chgame` | `loop` (Runaway scripts) | `c74273851f270beb` | A (R1) |
| `timing.chgame` | `timing` (Frame timing) | `ea2741992f220fd8` | A (R1, R2's marker) |
| `pack-images.chgame` | `pack-images` (Packed images) | `5f3b4f61e48de51b` | A (R1) |
| `counter-changed.chgame` | `counter` 1.0.1 | `fa0d541ee5b21f13` | not used |
| `pass-open.chgame` | `pass-open` (Pass open) | `432492ea015af1e5` | B2 |
| `pass-hidden.chgame` | `pass-hidden` (Pass hidden) | `5e20548ec727951d` | B3, B6, B7 |
| `pass-art.chgame` | `pass-art` (Pass art) | `2dd475c09a003d94` | B1, B4, B5, B6 |
| `slow-restart.chgame` | `slow-restart` (Slow restart) | `db614d9ff9aa1a35` | B8 |
| `pass-title.chgame` | `pass-title` (Pass title) | `71c89e65c08971f1` | B5 |
| `pass-store.chgame` | `pass-store` (Pass store) | `5a6bbba63ef264ee` | B7 |
| `invalid-binary-lua.chgame` | `hardening` | (invalid) | A (R3) |
| `package_vector.chgame` | `package-vector` (Package Vector) | `0530a15766e91bf1` | A (R1, R4) |

The script derives the last two new games, so no fixture is added to the tree:

- `pass-title`: `pass-art` without `handoff.png`, for the `title.png` fallback (B5).
- `pass-store`: `pass-hidden` plus one `ch.store.set` per move. No other packet fixture in a pass match writes `ch.store`, so without it no store flush can land in a hand-off (B7).

R4's eight "Aa extra" games are still packed by hand, with the loop in epic 4's packet.

After install, `/.games/<id>/.pkg` holds `v1` and the hash on its second line.

## Fixture calibration (AI-5)

`orchestrated-epics.md` ("Owner hand-offs") requires a fixture whose outcome depends on device timing to be calibrated on the device first, or listed here as uncalibrated.

| Fixture | Status | Why |
| --- | --- | --- |
| `loop`, "Slow C calls forever" | uncalibrated: expected outcome estimated from the host ratio | e4-z1 resized it to `backtrackArgs(6, 10)` from the host time and a device/host ratio of about 95 (`plan-e4-z1-loop-fixture-calibration.md`, "Device estimate (unmeasured on the device)"). R1 is its calibration run. |
| `loop`, "Stuck in one C call" | calibrated | Entry 14's device run: abandoned after about 3.5 s, 1,032 B leaked (`deferred-work.md` `## owner-e4-entry14`); `backtrack(12, 40)` unchanged since. |
| `slow-restart` | uncalibrated: expected outcome estimated from the host ratio | `setup` spins 2 s on `ch.time.ms()`, plus up to one `slowCall`, whose device time is unmeasured. It must stay under the 3 s watchdog. No device run of it is recorded. |
| `pass-open`, `pass-hidden`, `pass-store` | no timing band | Their `ch.timer` (10 s, 5 s, 5 s) fires only if a player waits that long; a run with no `timer for seat` line is no failure (fixtures README). |
| `counter`, `pass-art`, `pass-title`, `pack-images`, `package_vector`, `invalid-binary-lua`, the eight `extra` games | no timing band | Nothing in their outcome depends on time. |
| `timing` | not run here | R1 only installs it; its own bands are the Timing run in the fixtures README, outside this packet. |

## Order and card state

1. **Part A, epic 4's recheck (R1–R4), first, on a clean card.** R1 needs a card with no `/games/`, `/.games/`, or `/.games-data/`, so it has to run before Part B fills them.
2. **Part B, pass-and-play.** It reuses Part A's card after B0 tidies it.
3. **Part C, the unreadable `.pkg` (AI-11), last.** It pulls the card while the firmware runs.

Flash the firmware once, before Part A. Keep the serial log running for all three parts.

## Part A: epic 4's recheck R1–R4

Epic 4's recheck is still open: its retrospective's Behavior verification reads "Device, X4 Pro: pending the owner". Run `../epic-install-and-launcher/device-run-packet.md` R1–R4 as written there, with these changes:

- **Firmware:** this packet's `6cf7f65b`, not `b83dfbe2`. Since `b83dfbe2` the game fixtures used by R1–R4 have not changed, and their hashes are unchanged (repacked 2026-10-02, including R4's eight extras `457203b4c567adc8` … `cc566037021bbf1c`).
- **The launcher has one row per game** (entry 8) and a row opens the game's title screen (entry 7). Each row also shows a second line of modes (`Solo`).
  - R1 steps 3 and 5: tap Runaway scripts' row, then New game (or Continue, if the title screen offers one; both show the band menu), then the band. Back from the error view returns to Games with Runaway scripts selected.
  - R2 step 1: Counter, New game, a few taps, then Back and Leave. Step 2: long-press Counter's row or hold Confirm on it. There is no Continue row any more. The dialog reads "Remove this game?", "Counter", "Its saved data is kept.", with Cancel and Remove. Step 3 expects Counter's row gone.
  - R4 steps 3–5: tap Package Vector's row (on page 2, eight rows a page), then New game. It stops in the error view at once. Back returns to Games on page 2 with Package Vector selected. Step 6 (a Continue row) no longer applies.
  - New game on a game with no `prefs.bin` now writes one (the mode it started, entry 12). So R2's `/.games-data/counter/` also holds `prefs.bin`, and R4's Package Vector now gets a `/.games-data/package-vector/` holding only `prefs.bin`. Neither changes what R2 or R4 checks.
- **Also, after R1 step 1 (deferral 5.10 #16, `GameHash`'s mbedTLS branch):** from a computer, `/.games/package-vector/.pkg` reads `v1`, then `0530a15766e91bf1`.
- **Record** under epic 4's retrospective, `epic-install-and-launcher-retrospective.md`, "Behavior verification", as its packet says, and copy the result lines into entry 11's plan. An R1 band that is still abandoned is an epic-4 item, as that packet's step 6 says, not a story here.

## Part B: pass-and-play

The steps come from the fixtures README ("Title screen, Options, and hand-off run"), `game-canvas.md`, and the story plans (5.12 above all). Do each title-screen and Options step once by touch, then once with the front buttons alone (Up, Down, Confirm, Back).

### B0. Set up

1. Remove R4's eight "Aa extra" games (long-press each row, Remove). This keeps the pass games on page 1.
2. Put in `/games/`: `counter.chgame` (R2 removed it), `pass-open.chgame`, `pass-hidden.chgame`, `pass-art.chgame`, `slow-restart.chgame`, `pass-title.chgame`, `pass-store.chgame`. No `/.games-data/pass-*/` folder may exist yet.
3. Settings: Display, Sleep Screen "None"; Controls, Short Power Button Click "Sleep". Note the Home Button Gestures actions you start with.

### B1. Launcher, title screen, Options, `prefs.bin`

1. Home, Games. The rows read Counter "Solo", Pass art "Solo · Pass and play", Pass hidden "Pass and play", Pass open "Solo · Pass and play", and Pass store "Pass and play" on page 1. Pass title "Solo · Pass and play" and Slow restart "Solo" are on page 2.
2. Open Pass art. The header reads "Pass art", and the 480 × 480 band under it shows `title.png`, centred (a frame, two rings, a cross). The rows are New game "Pass and play · Hard · Small" (selected) and Options. Serial: `Entering activity: GameMode`, then `Title screen of pass-art: save none`. The gap is `peek`'s time with no save (T2).
3. Back. Open Counter. It shows its icon at 128 px centred in the band, Continue "Load the previous game" (selected: R2 kept its `resume.bin`), New game "Solo", and no Options. Serial: `Title screen of counter: save valid` (T2 with a save).
4. Pass art, Options. The header reads "Options", with rows Mode "Pass and play", Level "Hard", Board "Small". A tap (or Confirm) cycles a row: Mode goes to "Solo" and back, Level to "Easy", Board to "Medium", "Large", and back to "Small". Leave Level "Easy" and Board "Large", then Back. The Options row is selected, and New game reads "Pass and play · Easy · Large". Serial: `pass-art: saved prefs.bin (30 bytes)`.
5. Remembered: go Back to Games and reopen Pass art, then sleep, wake, and reopen it. Both times the line reads "Pass and play · Easy · Large". Afterwards, from a computer, `/.games-data/pass-art/prefs.bin` is 30 bytes and starts `CHPF`.
6. For deferrals 5.12 #1 and #4: open Options, cycle Board once, then swipe up from the bottom (Home). Note whether Home opens on its Games row. Reopen Pass art and note whether the Board change was kept. As built: Home's default row, and not kept.

### B2. `pass-open`: an open round, 3 taps from Home

1. Pass open, Options, Mode "Pass and play", Back (New game now reads "Pass and play"). Back to Home.
2. **3 taps:** Games, Pass open, New game. Seat 1's board reads "Player 1 (X) to move". Count the taps (pass: 3).
3. Play to the end. Each tap on a free square places the mark, and the next board reads "Player 2 (O) to move". A taken square shows "That square is taken" to that seat only. There is no banner and no hand-off screen. Serial: `tap for seat S`, `apply seat S cell N`.
4. Round end: the end-of-round menu (Play again, Leave) sits over "Everyone: Player N wins" or "Everyone: a draw". Serial: `over for seat 1` and `over for seat 2`, once each. Play again: an empty board, seat 1 to move.
5. **Fast double taps (P11):** ten times, tap a free square, then at once a second free square. Count how often the second tap placed the other seat's mark (`tap for seat 1` then `tap for seat 2` within the double tap) and how often it was dropped (`Dropped a touch made under frame F, before seat S's first frame G`, or no line).
6. **First tap (P13):** on ten turns, tap the moment the next board appears. Count the first taps that placed nothing.
7. **Long press (P15):** hold a finger about 1 s on a free square: no mark, and no `tap for seat` line. A quick tap places one. The 90 ms touch-down threshold cannot be seen by hand: unmeasured.
8. Mid-round, Back, Leave. Pass open's title screen offers Continue (selected). Continue: the same board and turn. Serial: `Continue pass-open: a solo roster of 1 seat(s) unless the save says otherwise` (the fallback roster), then `pass-open: resuming the save's roster: pass, 2 seat(s)`.

### B3. `pass-hidden`: the hand-off, 3 taps from Home

1. **3 taps:** Games, Pass hidden, New game ("Pass and play"). The hand-off screen comes up with a full refresh, laid out as the title screen with no header: the `game-controller` icon at 128 px centred in the band, "Player 1's turn" in the first row's place, and the "I'm ready" button in the second's. A tap on the band or on the text does nothing.
2. "I'm ready" (or Confirm) shows seat 1's frame with a full refresh: "Player 1", "Player 1's secret: apple", "Moves: 0". Check for **no ghost** of the hand-off screen.
3. Tap the frame (a move). Seat 1's frame stays, with "Tap to pass to player 2" in a framed banner at the bottom. A tap above the banner does nothing. A tap on the banner (or Confirm) shows the hand-off screen, "Player 2's turn", with a full refresh: **no trace of "apple"** may remain. "I'm ready" shows "Player 2's secret: river", with no trace of seat 1's frame.
4. Play to four moves. The end-of-round menu sits over "Everyone: the secrets were apple and river", with `over for seat 1` and `over for seat 2` once each. **Play again** shows the hand-off screen, "Player 1's turn".
5. **Resting finger (P14):** on a hand-off screen, rest a finger on the band. Press Confirm with the other hand, then lift the finger once the seat's frame is up. The lift is no move: no `tap for seat` line, and Moves is unchanged.
6. Back on the banner or on the hand-off screen opens the pause menu (over no frame from the hand-off screen), and Resume returns to the same screen.

### B4. `pass-art`: `handoff.png`, settings in the match, Play again

1. New game (Level Easy, Board Large from B1). The band on the hand-off screen shows `handoff.png` (a frame, a ring above a checkerboard), not `title.png`'s two rings. "Player 1's turn" and "I'm ready" sit as in B3. Serial: `Page /.games/pass-art/handoff.bmp: 480x480`.
2. "I'm ready": seat 1's frame reads "Mode: pass", "Level: Easy", "Board: Large". Serial: `setup pass level Easy board Large`.
3. Play to the end, then Play again: the hand-off screen again, and seat 1's new frame still reads "Level: Easy", "Board: Large".

### B5. Top-image fallback

The hand-off screen's band falls back `handoff.png`, then `title.png`, then the icon.

1. `handoff.png`: B4 step 1 (Pass art).
2. `title.png`: Pass title (page 2), New game. The band shows `title.png`'s two rings and cross. Serial: `Page /.games/pass-title/title.bmp: 480x480`.
3. Icon: Pass hidden (B3 step 1). The `game-controller` icon.

### B6. Plain taps on the banner and the hand-off screen

Ticket 13 removed the time guard that this step first checked (owner Decision 2026-10-03, epic Notes line 138, superseding the 2026-10-02 Decision, the home-key 2,000 ms and power-click 1,051 ms dating, and review row N1): the banner, "I'm ready" and Confirm are plain tap targets that act on release, even mid-refresh. The old steps 1 to 4 (a fast double tap never presses "I'm ready", a held Confirm does not pass, the home-key and power-click dating) described that guard and are replaced by the checks below. Use Pass art or Pass hidden.

1. **One tap passes:** on the banner, tap once, including during the Result screen's refresh: the hand-off screen comes after that one tap. On the hand-off screen tap "I'm ready" once, also during its refresh: the seat's frame comes. A tap made after "I'm ready" but before the next seat's frame is published is dropped by the owner's Decision of 2026-10-03 (serial: `dropped a touch before the frame was on the panel`); count those, they are not a failure.
2. **Confirm:** one press and release of Confirm passes the banner, then the hand-off screen, with no wait.
3. **Home key (the X4 Pro has one):** Settings, Controls, Home Button Gestures, Double Tap "Confirm". A double tap on the home key passes the banner, and again passes the hand-off screen, with no time bound. Afterwards, set Double Tap back.
4. **Power button:** Settings, Controls, Short Power Button Click "Confirm", with Double-Click Power for Light on. A click passes the banner or the hand-off screen about half a second later. Afterwards, set Short Power Button Click back to what it was (the firmware default is "Ignore").
5. **Accepted overlap:** a stray second tap on the banner's spot can pass the hand-off, and a stray second tap on "I'm ready" can become a move. Record it if it happens; it is the owner's accepted behaviour, not a failure.

### B7. Sleep in a hidden match: the blank, the timings, Continue

1. **Blank before sleep, once per Sleep Screen mode** (Settings, Display, Sleep Screen): Dark, Light, Custom, Cover, Cover + Custom, None, Quick Resume, Transparent. On a seat's frame in Pass art, sleep (between modes, wake and come back through Continue as in step 2). The screen first goes plain white (no icon, no text, not the page), then shows that sleep screen. Transparent and Quick Resume draw over white, never over the seat's frame. Under None, check the white for a **readable ghost** of the seat's frame (P5). Serial, each time: `pass-art: forced exit: blank screen pushed (half refresh)` (T3).
2. Wake: Home. Then Games, Pass art, **Continue**: the hand-off screen naming the saved turn seat. "I'm ready" shows that seat's frame with its Moves count.
3. In Pass hidden, sleep once on the hand-off screen. Plain white comes first here too. Continue resumes on the hand-off screen.
4. **States (P10):** in Pass hidden, sleep once each from Result (the banner), the hand-off screen, and the end-of-round menu. Note any `forced exit past 1500 ms; skipped ...` line.
5. **Store flush in the window (P4, P20), Pass store:** sleep and wake (to reset the loop bar), then note the last `New max loop duration` line before the first hand-off. Play two rounds, waiting more than 5 s on each seat's frame before its move, so the store write falls due at the move. Note each `pass-store: saved ch.store (N bytes)` line and whether it comes after `pass-store: Playing -> Result on TurnChanged` (it landed in the window). Note every `New max loop duration: N ms (activity: N ms)` line while Result or the hand-off screen is up.
6. **Forced exit with a dirty store (P4), Pass store:** make two moves less than 5 s apart (the second set is held for the 5 s flush), then sleep at once. Expect `forced exit: blank screen pushed (half refresh)`, then `saved ch.store (N bytes)`. A line `pass-store: forced exit past 1500 ms; skipped the ch.store flush` (or `skipped the resume write`) is a finding. It goes to the owner with deferral 5.6's options: accept the skip, `displayBufferAsync`, or widen the window.

### B8. Play-again gap and New over a save

1. Slow restart (page 2): play the round (three taps below the banner), Play again, then Back at once. The pause menu shows "Starting the next round", centred under "Paused". Round 2 reads "This round's setup took about 2 s". An error view ("It stopped responding: one step ran over 3 seconds") is a calibration finding (see the table).
2. Pass art: start a match (New game or Continue), then Back, Leave, so a save is there. Options, Mode "Solo", Back: New game reads "Solo · Easy · Large". New game asks "Start a new game?", "Solo", "This replaces the saved game.", Cancel focused. Cancel keeps the save; New game starts a solo match.

## Part C: a Continue whose `.pkg` will not read (AI-11)

The launcher lists only games whose `.pkg` reads. So the `.pkg` must fail between the title screen opening and the Continue tap, and pulling the card is the only way found. Whether the X4 Pro firmware keeps running with its card pulled is unknown. Back up the card first. If you would rather not pull it, record "not staged" (the host test `ResumeMatchTest.AContinueWhosePkgWillNotReadStopsInTheErrorViewAndKeepsTheSave` covers it).

1. Leave a Counter match after a few taps so `/.games-data/counter/resume.bin` exists. From a computer, copy that file aside as `resume.before`.
2. Games, Counter: the title screen offers Continue. Pull the card, then tap Continue.
3. **Expect:** the error view, "The game could not start", with "The saved match could not be resumed. It is unchanged: try Continue again, or open the game for a new match." Serial: `counter: cannot read .pkg; not starting a new match over a save`, then `counter stopped: The saved match could not be resumed. ...`. No match starts (no `Started counter` line).
4. Back, put the card in, and restart the device. From a computer, `resume.bin` is byte-identical to `resume.before` (`cmp`). Games, Counter, Continue resumes the count.

## Timings: the serial lines

| ID | Measure | From line | To line | What decides |
| --- | --- | --- | --- | --- |
| T1 | One `resume.bin` write (R14) | `pass-hidden: Playing -> Result on TurnChanged` | `pass-hidden: saved resume.bin (N bytes, ver V)` | Record. An upper bound: the gap includes one loop pass. In open pass, time it from the game's `apply seat S cell N` line, which also counts the draw. |
| T2 | Time `peek` adds when the title screen opens (R14) | `Entering activity: GameMode` | `Title screen of <id>: save <none/valid/unreadable/unstartable>` | Record, with and without a save. |
| T3 | Half refresh inside the forced exit (R14, P4) | `VM stopped; arena peak N bytes, stack high-water N bytes free, least at a hook N bytes` | `<id>: forced exit: blank screen pushed (half refresh)` | Record. `lib/hal/HalDisplay.h` says 1,720 ms (source unstated). |
| T4 | The forced exit's window (4.13) | `Exiting activity: GameMatch` (then `<id>: <State> -> Leaving on ForcedExit`) | the last of `saved resume.bin`, `saved ch.store`, or a `forced exit past 1500 ms; skipped the resume write` / `the resume.bin delete` / `the ch.store flush` line | Any `skipped` line goes to the owner (B7 step 6). |
| T5 | Loop stalls in Result and the hand-off (P19, P20, N1) | `New max loop duration: N ms (activity: N ms)` | n/a | Record only, with no pass or fail threshold: the guard whose 250 ms slack T5 once compared stalls with is gone (ticket 13, epic Notes line 138), and N1 no longer exists. The line prints only for a new maximum since boot (over 50 ms). Stalls below the bar noted before the first hand-off are unmeasured. |
| T6 | A `ch.store` write | the line before it | `pass-store: saved ch.store (N bytes)` | Record. Upper bound, as T1. |

## Owner answers: the `Assumption for entry 11:` lines

Every such line in `epic-pass-and-play.md`, Notes. "Line" is its line number there at `6cf7f65b`; dates are 2026.

| ID | Line | Date, source | Summary | Checked by | Owner's answer |
| --- | --- | --- | --- | --- | --- |
| P1 | 97 | 10-01, orchestrator | Entry 2 rewrote `PickerTest.APassOnlyGameWithOneSeatStartsNothingFromTheLauncher` (outside its touches) for R10's rule | none (host) | Accept (owner, 2026-10-03) |
| P2 | 98 | 10-01, orchestrator | Entry 2 corrected the `NearbyNeedsTwoSeats` (`Manifest.h`) and `pass` (`HostCaps.h`) comments, outside its touches | none | Accept (owner, 2026-10-03) |
| P3 | 99 | 10-01, orchestrator | Entry 4 changed `ManifestTest.EveryFixtureManifestIsListed` for `pass-hidden` | none (host) | Accept (owner, 2026-10-03) |
| P4 | 101 | 10-01, entry 6's plan | The half refresh returns in time for the SD steps to start within 1,500 ms; no `skipped the ch.store flush` | B7.1, B7.6, T3, T4 | Accept the skip, fix deferred under 5.6: the blank takes 1,654 ms against the 1,500 ms window, so a pending store flush or resume write after it is skipped; B7.6 was not run (owner, 2026-10-03) |
| P5 | 102 | 10-01, entry 6's plan | The half-refresh blank leaves no readable ghost of the seat's frame | B7.1 | Accept: no ghost reported in Part B (owner, 2026-10-03) |
| P6 | 103 | 10-01, orchestrator | Entry 7 updated the launcher suites (29 cases) outside its touches | none (host) | Accept (owner, 2026-10-03) |
| P7 | 105 | 10-01, entry 7's plan | New over a save asks a second time, Cancel focused (now with the mode's name, entry 12) | B8.2 | Accept (owner, 2026-10-03) |
| P8 | 106 | 10-01, entry 7's plan | Until entry 9, Continue on a pass save started a new pass match. Superseded: entry 9 resumes the save's roster | B2.8, B7.2 | Accept: superseded by entry 9 (owner, 2026-10-03) |
| P9 | 109 | 10-01, orchestrator | Entry 9 changed five `ModePickerTest` cases that entry 7 marked "until entry 9" | none (host) | Accept (owner, 2026-10-03) |
| P10 | 113 | 10-01, cross-story row 15 | A hidden match's forced exit pushes the blank in every state (Over, HandOff, Error too); if that skips an SD step, the owner may limit it to Playing/Result/Paused | B7.4, T4 | Accept, with P4: the same skip, the same deferral (owner, 2026-10-03) |
| P11 | 115 | 10-01, e5-xr's plan | Open pass: a touch made during an SD write after a move can count as the next player's move. Closing it needs touch read off the loop task (upstream input code) | B2.5, T1 | Accept as a known limit: the SD write after a move takes about 91 ms (T1) (owner, 2026-10-03) |
| P12 | 116 | 10-01, e5-xr's plan | Open pass: a swipe or long press landing just before the next screen can count as the next player's | none: no packet fixture logs a swipe or long press; unmeasured | Accept: unmeasured (owner, 2026-10-03) |
| P13 | 117 | 10-01, e5-xr's plan | A touch after the panel shows the next screen but before its push returns is dropped without feedback | B2.6 (the gap itself: unmeasured) | Accept as a known limit (owner, 2026-10-03) |
| P14 | 118 | 10-01, e5-xr's plan | A finger already resting when a new turn begins is ignored for that contact | B3.5 | Accept (owner, 2026-10-03) |
| P15 | 119 | 10-01, e5-xr's plan | The input double copies the 90 ms touch-down and 500 ms long press | B2.7 (500 ms roughly; 90 ms unmeasured) | Accept: the 90 ms threshold stays unmeasured (owner, 2026-10-03) |
| P16 | 122 | 10-02, orchestrator | AI-12 (installer split) stays deferred although entry 12 changed `GamePackageInstaller.cpp` | none | Accept (owner, 2026-10-03) |
| P17 | 123 | 10-02, orchestrator | Entry 12 changed `GameImages.{h,cpp}` and `GameAssets.cpp` (outside touches) to reserve `title` and `handoff` | none | Accept (owner, 2026-10-03) |
| P18 | 129 | 10-02, entry 12's plan | Banner and "I'm ready" overlap; time, not position, guards a double tap. Answered by the owner's Decision of 10-02 (line 130): keep both, keep the time guard. Superseded by the owner's Decision of 10-03 (ticket 13): the guard is removed, the layout stays, a stray second tap on the banner's spot can pass the hand-off | B6.1 | Answered 10-03 |
| P19 | 132 | 10-02, entry 12's fix round | Home-key Confirm dated back 2,000 ms and power click 1,051 ms; a loop stall over 250 ms is not covered | B6.3, B6.4, T5 | Answered 10-03: the dating is removed with the guard (ticket 13); a home-key or power-click Confirm passes at once |
| P20 | 135 | 10-02, orchestrator | N1 open: a press read late after a stall over 250 ms can pass the hand-off; one over 250 ms reopens N1 | B7.5, T5 | Answered 10-03: N1 no longer exists, the guard it covered is removed (ticket 13) |

Also for the owner at entry 11 (Decision of 10-02, line 121): the approved design's departures from R4, R5, R6/R14, R9 and R10 (buttons or Confirm only, the hand-off screen's picture, the plain white sleep push, New game plus Options, the level-1 preview keys). Answer: accept (owner, 2026-10-03).

## Owner answers: this epic's deferrals

The open items in `deferred-work.md` under `## e5-inception`, `## 5.1`–`## 5.10` (5.10 is the sweep), `## e5-xr` and `## 5.12`. Items marked "Resolved" or "Built, no longer deferred" are left out: 5.1 #1–3, #5; 5.2 #1–4, #6–7; 5.4 #1–3; 5.5 #1–2; 5.6 #2; 5.7 #1–12; 5.8 #1–4, #7–8; 5.9 #1–5; 5.10 #1–6; e5-xr #1–4; 5.12 #5, #7. An item superseded by a later one is listed under the later one. "Upstream" says whether the fix needs an upstream file.

| Section, item | Summary | Status, trigger | Upstream | Owner's answer |
| --- | --- | --- | --- | --- |
| e5-inception #1 | Title-screen seat-choice rows | Deferred; a host with `seats_max` above 2 | no | Accept as listed (owner, 2026-10-03) |
| 5.8 #5 | A registry load out of memory shows "Not enough memory" and is not retried in that visit | Documented; a device report | no | Accept as listed (owner, 2026-10-03) |
| 5.8 #6 | `ContinueLauncherTest`'s cases moved to `OneRowPerGameTest` | Documented | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #7 | The launcher-entry `peek` timing (`## 4.13`, `## owner-e4-homes`) is replaced by R14's title-screen `peek` | Documented; this run (T2) | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #8 (from 5.1 #4, 5.2 #5) | `SoloRounds` rename and the AD-21 note | Deferred; the next spine edit | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #9 (from 5.4 #4) | Solo/open pass pause menu in the Play-again gap drawn over the last round's frame | Deferred; the owner's decision | no | Leave deferred (owner, 2026-10-03) |
| 5.10 #10 (from 5.6 #1) | The forced exit's half refresh may use up the SD steps' window | Deferred; this run (T3, T4) | no | Accept the skip, fix deferred under 5.6 (see P4) (owner, 2026-10-03) |
| 5.10 #11 | Drift guards for the doubles, `roundsStartedAwaited`/`shown` interleaving, unreachable `notLoaded`, host task vs core | Deferred; the next feature in those files, this run | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #12 | `## e4-y`'s two items (the `counter/main.lua` comment, the vector hash check) | Deferred; unchanged (`## e4-y`) | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #13 | The older `GameSaveStore` forms `peek(id, pkgHash)` and `loadResume(ver, unreadable)` have no firmware caller | Deferred; the next `GameSaveStore` API change | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #14 | Retro R10 (pauses under the light panel) and AI-12 (installer split) | Deferred; next ledger change (R10), an installer epic (AI-12) | R10: ledger row (`FrontlightPanelActivity.cpp`) | Accept as listed (owner, 2026-10-03) |
| 5.10 #15 | Move `GameConfirmDialog` to its own file | Deferred; next story adding a file there | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #16 (a) | `## 4.1`'s per-pixel tests of circle, outline, line, text | Unchanged | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #16 (b) | `## 4.13`: `resume.bin` write cost, forced-exit total, the panel refresh behind `displayBuffer` | This run (T1, T3, T4) | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #16 (c) | `GameHash`'s mbedTLS branch | This run (Part A, `.pkg` check) | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #16 (d) | Unbounded fills | The `timing` fixture's device run (not in this packet) | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #16 (e) | `## 4.11`'s rejected resume kept (`LUA_ERRMEM`) | Unchanged | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #16 (f) | The `goHome` mapping test | The next row-5 touch | ledger row 5 | Accept as listed (owner, 2026-10-03) |
| 5.10 #16 (g) | Launcher fixed row height; a second tap on Remove | A taller-row theme, or a device report | no | Accept as listed (owner, 2026-10-03) |
| 5.10 #16 (h) | Installer extracted-sum bound; `ZipFile` and `PngToBmpConverter` reason codes | Extracted sum: epic-first-party-games' "before the freeze" Notes (`## owner-e4-homes` A19); reason codes: the next ledger change (A21) | reason codes: ledger row (upstream files) | Accept as listed (owner, 2026-10-03) |
| e5-xr #5 | `renderCanvas` stores `renderedFrame` from the read before `drawFront` | Deferred; next `renderCanvas` change or overlay pixels seen | no | Accept as listed (owner, 2026-10-03) |
| e5-xr #6 | A partly-local roster stalls on four paths; blocks epic-play-nearby | Deferred; epic-play-nearby's first story | no | Accept as listed (owner, 2026-10-03) |
| e5-xr #7, #8 (same item, recorded twice) | The input double's 90 ms and 500 ms thresholds are copies of private upstream constants | Deferred; an upstream change to them; B2.7 meanwhile | a check would read upstream files | Accept as listed (owner, 2026-10-03) |
| e5-xr #9 | Back-date the touch latch itself (long-press half of the same-pass race) | Deferred; epic-play-nearby's input work, or a device report | possibly (a `MappedInputManager` accessor is one route) | Accept as listed (owner, 2026-10-03) |
| e5-xr #10 | Three gaps in the screen input double | Deferred; next change to the double | no | Accept as listed (owner, 2026-10-03) |
| 5.12 #1 | Home gesture from Options lands on Home's default row | Deferred; next row-5 touch (B1.6 shows it) | ledger row 5 | Accept as listed (owner, 2026-10-03) |
| 5.12 #2 | game-api-seed §3 still calls the hand-off "the blank screen" | Deferred; next edit of the companion | no | Accept as listed (owner, 2026-10-03) |
| 5.12 #3 | Hidden-pass test names still say "the blank" | Documented; next rewrite of those suites | no | Accept as listed (owner, 2026-10-03) |
| 5.12 #4 | An Options change left by a Replace (Home, sleep) is not remembered; options: allow the `onExit()` write (spine change) or write on each cycle | Owner decides at entry 11 (B1.6) | no | Accept as built: remembered on Back only (owner, 2026-10-03) |
| 5.12 #6 (1) | N2: a power-click Confirm on a pass with another button's release is dated by `getHeldTime()` | Deferred (+80 B, minimal +32 B); when the share has room | no | Superseded by entry 13 (owner Decision, 2026-10-03): N2 went with the time guard (owner, 2026-10-03) |
| 5.12 #6 (2) | N1: a pass more than 250 ms late is not covered | Deferred (+80 B); this run (T5) | no | Superseded by entry 13 (owner Decision, 2026-10-03): N1 went with the time guard (owner, 2026-10-03) |
| 5.12 #8 | After a failed settings read, New game and Continue do nothing until reopened; no retry (M3, +80 B), no on-screen reason | Deferred; 80 B of room | no | Accept as listed (owner, 2026-10-03) |
| 5.12 #9 | M4: a remembered pass overwritten by an Options change on a host without pass seats; a garbage mode byte never rewritten | Deferred; such a host, or a prefs.bin format change | no | Accept as listed (owner, 2026-10-03) |
| 5.12 #10 | M5: after Unreadable-then-readable, a choice cycled back takes the file's value | Deferred; a report | no | Accept as listed (owner, 2026-10-03) |
| 5.12 #11 | N6: `exists` answering false reads as None, so an Options change overwrites good prefs | Deferred; a report | no | Accept as listed (owner, 2026-10-03) |
| 5.12 #12 | N7: 2^31–2^32 ms after the push every tap is dropped (Back, Resume clears it) | Deferred; a report | no | Superseded by entry 13 (owner Decision, 2026-10-03): N7 went with the time guard (owner, 2026-10-03) |
| 5.12 #13 | N8: on the Sticky the SDK overwrites the confirm/power key's press start | Deferred; file the proposal against freeink-sdk `InputManager.cpp` (`applyStateChange`) | **upstream proposal (SDK)** | Superseded by entry 13 (owner Decision, 2026-10-03): N8 existed only because loopHandOff dated Confirm; no SDK proposal to file (owner, 2026-10-03) |

## What to record

In entry 11's plan, `story-device-run-and-owner-sign-off-plan.md` in this folder, per entry 11's verify:

- The firmware flashed (the CI artifact, or a local build of `6cf7f65b`) and the package hashes from `HASHES.txt`, or the hashes the device's `.pkg` files show.
- Each step's result (A R1–R4 and the `.pkg` check, B0–B8, C), pass or fail, with photos for the hand-off screen, the banner, a ghost if any, and B5's three bands.
- The serial lines for T1–T6, each with its `[millis]` prefix and the computed gap, plus B2.5 and B2.6's counts and the loop bar before the first hand-off.
- Any reset, watchdog banner, `abandoning it`, or `forced exit past 1500 ms` line.
- The owner's answers to P1–P20, the Decision-121 line, and each deferral row.
- A device failure becomes a new story in this epic's PR (CarpeTelam/crosshatch-player#24), not a later fix. An R1–R4 failure goes to epic 4's retrospective as its packet says.

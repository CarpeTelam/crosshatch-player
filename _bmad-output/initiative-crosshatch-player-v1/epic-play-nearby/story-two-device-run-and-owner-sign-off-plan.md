---
title: 'Two-device run and owner sign-off'
type: 'chore'
ticket: '14'
created: '2026-10-14'
baseline_revision: '471eda60fc32e646b0e5fafb7638f6d218e4da68'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
deferred: []
context:
  - '{project-root}/AGENTS.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Done when 2, 3 and 5 have no closing device result. The smoke run (entry 11) played only the `nearby` fixture, on firmware from before the sweep (entry 13) and the cross-story fix build. Nothing on a device has yet covered the silence, the script error, the two first-party nearby rounds, the battery cost and idle drain, the 100 KB threshold, the Wi-Fi refusal, the checks carried from epic-first-party-games, or the `package-games` label test.

**Approach:** This pass writes the closing run's packet beside this plan, and this plan's fields-to-fill table. The run is cardless, in run order, on two X4 Pros with one firmware built from the epic head. The pass stops before the owner's run. The orchestrator builds the firmware, packs the packages, and fills the packet's `FILL` fields. A later pass of this entry brings back the owner's results, writes the measured values into `docs/crosshatch/` and the spine's Deferred rows, and records the owner's answers.

## Boundaries & Constraints

**Always:**
- Entry 14's `touches`: this plan and the packet beside it, in this pass. `docs/crosshatch/**`, the spine, `deferred-work.md` outcomes, and new entries in `tickets.toml` come after the run.
- Cardless and X4 Pro only (owner Decisions of 2026-10-13; the epic's Scope note of 2026-10-14). The firmware is built from the epic head with `-DCROSSHATCH_TOUCH_LOG=1 -DCROSSHATCH_SD_LOG=1 -DCROSSHATCH_BUILD_ID=<commit>`. The cards are read through the device's Wi-Fi file manager and joined with `device_log.py merge` and `summary`.
- The packet follows entry 11's packet in form, in run order and as short as the steps allow. It carries the cross-story review's packet rows 6, 7, 10, 11 and 18. The owner's questions sit in one section at its end: rows 3, 10, 12, 13 and 17, then the deferrals to confirm, each with the orchestrator's recommendation from `cross-story-review.md`.
- Every figure and answer that entry 14's verify asks for has a row in this plan's fields table.
- The firmware commit, size, SHA-256 and package hashes are `FILL`, for the orchestrator.

**Never:**
- Build firmware, flash a device, or claim a figure that only the device gives.
- Change `src/`, `lib/`, `test/`, `games/`, or `deferred-work.md`. Run `tickets.py mark` or `pull`. Push.
- Decide one of the owner's questions. Each gets the review's recommendation, and the owner answers.

</frozen-after-approval>

## Code Map

These are the sources the packet quotes. Every quoted log line must match the source text.

- `src/games/GameLinkLog.h:61-138`: the `LINK` lines, each led by `session=%08x`. They are `state sent ver=`, `state recv ver= seat=`, `state adopt ver= seat= result=` (Adopted, RoundStarted, NotPeer, Busy, BadSnapshot, Stale, OutOfTurn, BadStatus), `queue full`, `abort recv reason= seat=` (left, script_error, timeout, full, version_mismatch), `leave reason= waited=ms unacked= copies=`, `resend n= total=`, `silence n= total=`, `abort sent`, `link refused`, `task started; stack N bytes`, and `task stopped; stack high-water N bytes free`. `EspNowLink.h:375` is `radio up on channel 1 as <MAC>`.
- `src/activities/games/GameMatchActivity.cpp`:
  - `:297`: `<id>: <from> -> <to> on <event>`.
  - `:356`: `<id>: the other player is gone: <reason>`.
  - `:389`: `<id> stopped: <error>` (ERR).
  - `:586`: the peer's end, held behind an unadopted STATE (cross-story row 2).
  - `:341-345`: a forced exit (a sleep) sends three `ABORT(left)`, so a sleeping device gives its peer "left", not silence (R6).
- `GameMatchView.cpp:59,76-85`: the view texts. They are "The game stopped with an error", "The other player left", "The other player went silent", and "The other game stopped with an error".
- `src/games/GameVM.cpp`:
  - `:254`: `VM stopped; arena peak N bytes, stack high-water N bytes free, least at a hook N bytes`.
  - `:243`: `[LUA] Script error: ...`.
  - `:436`: `Dropped a touch made under frame ...`.
- `src/games/GameTouchLog.h:71`: `not sent, off the canvas`.
- `src/main.cpp:737,745`: `[MEM] Battery: <mV> mV, <pct>%`, logged before `[MEM] Free: ..., Min Free: ..., MaxAlloc: ...`. Both come every 10 s, and the battery line is skipped when the render-lock try fails.
- `src/games/NearbyProbe.{h,cpp}`: it refuses when `WiFi.getMode() != WIFI_MODE_NULL` or the free internal heap is below `100 * 1024`. `GameModeActivity.cpp:364` logs `Nearby for <id> refused: Wi-Fi in use|low memory`. The dialog says "Wi-Fi is in use. Close it and try again." `ReliableLink.h:39-41` sets 400 ms resend, 10,000 ms silence and 1,000 ms keepalive.
- Lobby (`english.yaml:640-654`): the screens show "Waiting for a player", "Player 2 joined", "Player 2 left", "Waiting for the host to start", "The host left", and "Back to the list". The lobby logs `Nearby: hosting <id>`, `looking for <id>` and `starting the match of <id>`. It logs nothing for a leave before Start, so the screens are the record.
- The fixture (`test/game_script/fixtures/nearby/main.lua`):
  - A taken square shows "That square is taken" and logs `rejected for seat S`.
  - The black square under the board at the bottom left (x 27-86, y 700-759) runs `error("boom")` when tapped on the tapper's turn.
- Ultimate tic-tac-toe rejects with "That cell is taken" and "Play in the highlighted board". Its header reads "Player 1 (X): your move" or "…: waiting".
- Battleship:
  - Its buttons are Rotate, Random, Clear and Ready. Rotate is local and is not a move.
  - A repeated shot is rejected with "You already fired there".
  - While the other seat places, the screen says "Waiting for Player N".
- Wi-Fi states:
  - Every upstream Wi-Fi feature ends in `silentRestart` or `ESP.restart`: File Transfer (`CrossPointWebServerActivity.cpp:125-133`), Calibre, OPDS, the KOReader auth and sync, OTA, font download, Settings > Wi-Fi Networks, and Sync Clock.
  - The one exception is the loaned-book "Sync Clock Now" path (`EpubReaderActivity.cpp:1384-1425`). After a cancel, a failed join or a failed sync, it leaves the radio in station mode with no restart.
- Settings > Time to Sleep: 1 to 30 min, or "Never"; the default is 10. Home, the Games list and a game's title screen allow auto-sleep.
- Packing: `scripts/pack_device_run.py <out>` packs `nearby.chgame` (with `HASHES.txt`), and `scripts/pack_game.py games/<id> <out>` packs each first-party game.
- `device_log.py summary` gives these blocks:
  - a `battery:` block with the count, the first and last readings, and the same per boot;
  - a `nearby:` block with each session's STATE counts, adopt results, running totals, ABORTs, leaves, and the `GameLink` high-water;
  - the `VM stopped` lines, and the `[MEM]` lines (all of them with `--mem-all`).
- Models: [the smoke-run packet](story-nearby-on-two-x4-pros-wiring-and-the-smoke-run-packet.md), and the [epic-first-party-games packet](../epic-first-party-games/device-run-packet.md), Part B step 5 (B5a to B5c) and "Fourth run".

## Tasks & Acceptance

**Execution:**
- [x] `story-two-device-run-and-owner-sign-off-packet.md` (beside this plan): write the packet as Design Notes 1 to 9 describe, in the step order of Design Note 10, with the questions of Design Note 11 at its end. This lets the owner run it with no other file open.
- [x] Check that every log line and screen text the packet quotes appears in the source cited in the Code Map.

**After the owner's run (a later pass of this entry, not this one):**
- [ ] Fill the fields table below from the owner's "Fields to record" table at the end of the packet, and from `merge` and `summary` on both cards.
- [ ] Record the 400 ms, 10 s and 100 KB values, confirmed or changed, in `docs/crosshatch/game-canvas.md` (the Overlays paragraph on the `GameLink` task) and in the spine's Deferred rows (`ARCHITECTURE-SPINE.md:608-609`), with the Internal RAM row (`:570`) if the threshold changes. A changed value is a new entry (the ticket's `unknown`).
- [ ] Give the orchestrator the outcomes for `deferred-work.md` `## e5-run4`, `## e5-close`, `## e5-run2`, `## e6pre-5` and `## e8-touch-log`, and the owner's deferral answers. A device failure becomes a new entry in `tickets.toml`.

**Acceptance Criteria:**
- Given the packet, when the owner follows it top to bottom, then every row of the fields table has a step or a summary line that produces it, and nothing in it needs a computer during the run.
- Given the packet's questions section, when the owner reads it, then each of rows 3, 10, 12, 13 and 17, and each deferral, shows the orchestrator's recommendation and space for the answer.

## Implementation Notes

- **The packet** is `story-two-device-run-and-owner-sign-off-packet.md`, beside this plan. It has the steps 0 to 14 of Design Note 10, the orchestrator's after-run figures, the label test, and Q1 to Q6 at its end.
- **Roles.** Design Note 10 fixes the host for steps 6 (B) and 7 (A); the packet alternates the rest. A hosts steps 3, 4 and 9, and B hosts steps 5 and 8. In step 9 B is the shielded device and A carries row 3.
- **Deleting the log folder (Design Note 5).** The file manager deletes only an empty folder (`CrossPointWebServer::handleDelete`, "folder not empty"). The packet therefore selects and deletes every file in `/crosshatch-log/`, the running boot's file among them, and leaves any new file the writer then starts. The delete comes before the uploads, so the install's upload and rename lines land in the files the run keeps.
- **The loaned-book path (Design Note 6).** "loan date unverified" is `openProtectedBook`'s internal error. The screen shows `STR_LOAN_TIME_UNVERIFIED`: "This book is on loan. Connect to Wi-Fi once so the device can check the date.", with "Sync Clock Now". The packet quotes the screen.
  - After the refusal, the radio stays in station mode, so the packet restarts A with a sleep and wake before step 2.
  - A's fresh-boot window then runs in the boot after that wake, which has no bring-up either.
- **Step 1's lobby check** stops at the lobby's first screen (Host a game, Join a game). The radio comes up only on Host or Join (`GameLobbyActivity::openHost`/`openGuest`), so the fresh boot still has no bring-up.
- **The bring-up failure lines** (corrected at review): both forms exist. The radio's steps log `[ERR] [LINK] bring-up failed at <step>` (`EspNowLink.h:352`), and the session's log `session=... bring-up failed: <step>` (`GameLinkLog.h:134`). The lobby adds `[ERR] [GAME] Nearby: the radio did not start for <id>`. The packet's after-run list names all three.
- **Battleship's B5b** uses "an unshot square", not the first-party packet's "known square". Its fleet layout is given, so any unshot square shows whether the shot was taken.
- **The label test's second run** re-adds `package-games` before the push. Design Note 9 says "after re-labelling", which the packet spells out as remove and re-add.
- **Review patches** were applied by the build agent (the implementer had ended detached). The packet gained a top-of-file battery, sleep and failure block, the release firmware, the label test's overlap proof, and its own "Fields to record" table (the orchestrator's ruling). Row 22 of the triage log moves the `fork-scripts.md` judgement to the orchestrator's after-run list.
- **Quote check.** Every quoted log format and screen text was grepped against `src`, `lib`, `games`, the `nearby` fixture, `scripts/device_log.py` and `.github/workflows`. All were found. The one composed string, Ultimate tic-tac-toe's "Player 1 (X): your move", is built in `header()` (`games/ultimate-tic-tac-toe/main.lua:193`). Help paragraph 6 is quoted with its capital and full stop.

## Plan Change Log

## Review Triage Log

**Pass 1 (2026-10-14).** Four lenses ran as context-free subagents: the blind hunter (BH), the edge-case hunter (EC), the verification-gap lens (VG) and the intent-alignment auditor (IA).
- The VG and IA lenses ran in the foreground.
- The BH and EC lenses came back detached; the orchestrator forwarded their reports and I triaged them on arrival.
- The orchestrator ruled on four findings (Q1, the label test, the release firmware, and the packet's own fields table).
- The implementation subagent had also ended detached, so I applied every patch myself and checked each against the source.

Counts: 0 high, 6 medium, 16 low, 3 false (IA's descriptive rows not counted). 22 patched, 3 rejected, 0 deferred.

| # | Lens | Finding | Verdict | Route and evidence |
|---|------|---------|---------|--------------------|
| 1 | BH1, EC12 | Q1's "keeps 100 KB as about the floor" contradicts its rule: 73,828 B plus 30 KB is about 102.1 KiB, over the code's 102,400 B | medium | patch: Q1 reworded by the orchestrator's ruling, peak use plus 30 KB rounded up to a whole KiB, about 103 KiB at the smoke-run figure, a small raise done as a new entry; Design Note 11 too |
| 2 | BH2, EC1, EC2 | Step 11 and the Implementation Note named only `bring-up failed: <step>`; the radio logs `bring-up failed at %s` (`EspNowLink.h:352`, confirmed) | low | patch: the after-run list names both forms and `Nearby: the radio did not start for`; the note corrected |
| 3 | BH3 | The `[MEM]` quote skipped `Total`; x4pro also logs a PSRAM line (`BOARD_HAS_PSRAM`, `platformio.ini:369`); `Min Free` is a since-boot mark, so "lowest through step 4" was vague | low | patch: exact line quoted; internal heap named; "Lowest" defined as the first `Min Free` after step 4's Leave, covering the boot so far |
| 4 | BH4 | Nothing fixes the charge state for the battery figures | medium | patch: fully charged, off the charger all run, starting % noted |
| 5 | BH5 | B's radio is never confirmed off during its after-match idle | low | patch: step 10 ends with a Nearby New game on B that must open the lobby |
| 6 | BH6, EC11 | The label test can pass with no overlap | medium | patch, by the orchestrator's ruling: check the run is in progress, prove overlap by times and conclusions, retry if not, record three run URLs |
| 7 | BH7 | Step 13 does not restore the release firmware | low | patch, by the orchestrator's ruling: step 13 flashes it; Firmware section and fields table have its `FILL`s |
| 8 | BH8 | The firmware fields were garbled ("File: FILL, FILL B") | low | patch: labelled commit, file name, size, SHA-256 |
| 9 | BH9, EC13, VG | `## 6.13` item 2 is "listed as resolved" in Design Note 11 but left out of the packet; the table was out of order | low | patch: a resolved row added; rows in numeric order |
| 10 | BH10 | `## 6.12` was recommended "confirm" before step 6 shows it | low | patch: confirm if the observations read right, otherwise each becomes a new entry |
| 11 | BH11 | The Code Map cites `Dropped a touch made under frame` but nothing reads it | low | patch: the after-run list sets it against B5a to B5c |
| 12 | BH12, EC8 | B's `copies=3` had no field; Ultimate tic-tac-toe's rejections lacked who, when, and the constrained-board condition; an out-of-turn tap shows nothing (no "Not your turn", `## 6.11`) | medium | patch: each rejection on the tapper's own turn, the wrong-board tap only once some boards are unhighlighted; `copies=3` already in step 4's log list, read from the cards |
| 13 | BH13 | No guidance for a failure mid-run | low | patch: note step, time and screen, restart, carry on, name the restart |
| 14 | BH14 | Frontmatter `in-review` with `lenses_ran: []` | false | It was the review in progress; `lenses_ran` is set at this triage |
| 15 | EC3 | No recovery if A does not auto-sleep on "went silent" | low | patch: after about 2 min, note it and press Back; Never restored either way |
| 16 | EC4 | The step-11 heap anchor "before the first `radio up`" picks step 3's in the same boot | medium | patch: anchored on step 11's own first of ten `radio up` lines and its last Cancel |
| 17 | EC5 | A may auto-sleep before step 9's match with Time to Sleep at 1 min | low | patch: if so, wake it and host again |
| 18 | EC6 | A short power press does nothing (`shortPwrBtn` IGNORE, 400 ms hold, `CrossPointSettings.h:272,395`), and a double press toggles the light | medium | patch: one line at the top saying to hold until it sleeps; each sleep step says "sleep" |
| 19 | EC7 | The file manager shows KB (`formatFileSize`, `FilesPage.html:2061`), so bytes cannot be compared, and only 0 was a finding | low | patch: a "KB as the file manager shows it" column (`FILL`); any difference is a finding |
| 20 | EC9 | A drawn round has no "winning move" for the second `## 6.12` observation | low | patch: "the round's last move, a win or a draw" |
| 21 | EC10 | The low-memory dialog is never mentioned | low | patch: the failure guidance names it as a finding |
| 22 | EC14 | "Run with no other file open", yet the record lives in the plan and needs `fork-scripts.md` | low | patch, by the orchestrator's ruling: a compact "Fields to record" table at the packet's end; the `fork-scripts.md` judgement moves to the orchestrator |
| 23 | IA | Under reading R-B (device results), this pass closes no Done when item | false | Descriptive. The frozen Approach stages it, and the post-run tasks stay unchecked |
| 24 | IA | No step drives the device below 100 KB, so Done when 4's refusal gets no device result; and the Wi-Fi dialog is optional | low | reject: the low-memory state cannot be reached from the UI without new code (`src/**` is out of `touches`). The ticket's Wi-Fi wording allows "no such state is reachable", and the threshold is settled through R12's figures and Q1 |
| 25 | IA | The silence view reads "went silent", where Done when 3 says "player left" | false | "player left" is the PeerGone view, whose reason line for silence is "The other player went silent" (R6, `GameMatchView.cpp:59`) |
| 26 | IA | `FILL` appears in the plan's table too; the label test and step 14 are outside the run order | low | reject: descriptive. The orchestrator fills both, the label test is on GitHub, and step 14 is conditional on Q2 by design |

## Design Notes

1. **Firmware and packages.**
   - The orchestrator fills the firmware fields: the commit, file name, size and SHA-256.
   - Flash with Flash Tools, or with Settings' firmware update from the card. Check the SHA-256 first. Both devices get the same file (AD-13).
   - The release firmware is flashed back in step 13, with its own `FILL` fields (orchestrator's ruling at review).
   - The packages are `nearby.chgame` (`pack_device_run.py`), and `ultimate-tic-tac-toe.chgame`, `battleship.chgame` and `sudoku.chgame` (`pack_game.py`). For each, the orchestrator fills the bytes and the hash. Sudoku goes on A only.
   - Uploads arrive as `<name>.chgame.zip`. Rename each in the file manager. Note each file's size on the card before opening Games (`## e5-run2`).

2. **Silence is not a sleep (R6, Done when 3).**
   - The ticket's "one device off" cannot be done with the power button, because a sleep is the forced exit and sends three `ABORT(left)`.
   - The packet therefore runs a sleep mid-match as its own step: the peer should show "The other player left", which is R6's forced exit.
   - It makes true silence by shielding one device: a closed metal tin, or a switched-off, unplugged microwave oven, or carrying it out of range. Both sides should reach "The other player went silent".
   - Row 7's timing runs from the last `state sent`/`state recv` line before the cut to `silence n=1` and to the `-> PeerGone` line. Acks and keepalives are not logged, so the reference is the last move before the cut.

3. **Idle drain (Decision 2026-10-13, no restart after nearby).**
   - Set Time to Sleep to "Never" before the run, and restore it at the end.
   - The fresh-boot window comes first: both devices, 30 min, on the Games list, untouched, right after the File Transfer restart and the install, with no bring-up in that boot.
   - The after-match window is B's: 30 min on the Games list after the silence step's match. B woke from the sleep step's deep sleep and then hosted or joined that match, so the boot holds the first bring-up's memory. A meanwhile runs its one-device steps.
   - The orchestrator compares the battery lines over the two windows (mV/min and %). `summary --mem-all` and the per-boot `battery:` block give them. A match's battery cost is the readings from the start of the Battleship lobby to its Leave, on both devices.

4. **Heap (R12; row 10; row 6).**
   - Row 10 uses the first bring-up of the fresh boot (the lobby step). "Before" is the idle window's last `[MEM]` line. "Lowest" is the smallest `Min Free` through the first match. "After" is the `[MEM]` lines on the Games list 30 s after the first match's Leave. Both devices.
   - Then A repeats the ten Host/Cancel cycles with no peer, with 30 s idle before and after, as smoke-run step 6 did.
   - "Heap after teardown" is the after-figures.

5. **The card log's mid-run delete (row 6, entry 22).** Delete `/crosshatch-log/` from the file manager while File Transfer is open before the run; that deletes the running boot's file. The check afterwards: the next file starts with its `# crosshatch-diag-log 1` header and an `[ERR] [DIAG] lost N bytes` notice. Downloading the logs at the end through File Transfer restarts each device, and since entry 23 `merge` places the earlier and later boots.

6. **The Wi-Fi refusal (row 11).** Upstream ends every Wi-Fi feature with a restart, the File Transfer included (Code Map).
   - The packet tries New game in Nearby straight after the File Transfer restart, and records that the lobby opens.
   - The loaned-book path is the only one that leaves Wi-Fi up, so it is optional and needs a borrowed book showing "loan date unverified". Cancel the Wi-Fi list it opens, then Home, the game, New game: the dialog is expected.
   - With no such book, record "not reachable from the UI".

7. **Row 3 on the device.** Before the silence step, set A's Time to Sleep to 1 min, and leave A on "went silent". It should sleep after about 1 min; before the fix it held off sleep. Set it back to Never.

8. **The one-device checks, on A** (Part B step 5 and "Fourth run" of the first-party packet, and `## e5-run4` with AI-7):
   - Sudoku (all with Mode Solo, Easy):
     - Back before the first frame, then Resume: the header minutes must not lag.
     - Wait for "1 min", pause 2 min, Resume: the minutes are the same as before the pause.
     - TIMER off and back on.
     - Note what NOTES AS reads.
     - A sleep and Continue mid-puzzle: the minutes may go back, which A-1 accepts.
     - Leave, which logs `VM stopped`.
   - Battleship in Pass and play: B5a, B5b and B5c, three tries each, recorded per try; then B6, a sleep from Over (no Continue on the title screen).
   - AI-7's right-edge taps go in the nearby Ultimate tic-tac-toe round on A. Make ten taps at the screen's right edge beside the right column, on A's turn, then count the `not sent, off the canvas` lines, and the taps that count as the right column (entry 15's margin).

9. **The label test (row 18, `## e6pre-5`).** These steps are the orchestrator's and the owner's, on PR #40:
   - The owner adds `package-games`, and the "Game packages" run starts.
   - While it runs, the owner adds an unrelated label. The pack must not be cancelled.
   - Later, while a pack runs after re-labelling, the orchestrator pushes a commit. The push cancels the pack.
   - Each action needs the run still in progress. Overlap is shown by start and end times, and the pack ends `success` (the label) or `cancelled` (the push). Record three run URLs (the pack, the unrelated label's run, the push's run) with their times and conclusions (orchestrator's ruling at review). The step can run before or after the device run.

10. **Run order in the packet** (A `2C8C` and B `6E68`; the roles swap between games):
    - (0) Before the run: flash; File Transfer for the uploads, the renames, the sizes, and deleting the log folder; Time to Sleep Never; close, which restarts.
    - (1) Install and the Wi-Fi refusal check.
    - (2) The fresh-boot idle, 30 min, both devices.
    - (3) Leaving the lobby before Start: the guest cancels, so A shows "Player 2 left"; the guest rejoins; the host cancels, so B shows "The host left".
    - (4) A fixture round with a rejected move and Play again. The guest Leaves, and the host shows "player left"; Back there logs `copies=0`. The radio is off afterwards: New game in Nearby opens the lobby.
    - (5) The script error: the mover sees the error view and `Script error`, and the peer gets `abort recv reason=script_error` and "The other game stopped with an error".
    - (6) Ultimate tic-tac-toe, B hosting: both rejections, the headers, `## 6.12`'s two observations, the right-edge taps, Play again, a Leave.
    - (7) Battleship, A hosting: Rotate, Random and Clear on each device, each screen never showing the other's ships, the rejected repeat shot, a full round, Play again (placement starts on both), and the host's Leave.
    - (8) A sleep mid-match on B: A shows "left". Wake B.
    - (9) Silence by shielding, with row 3 on A.
    - (10) B's after-match idle, 30 min, while A runs (11) the ten cycles and (12) the one-device checks.
    - (13) Restore Time to Sleep; download `/crosshatch-log/` from both through File Transfer and send it.
    - (14) Optional, only on a "yes" to row 12: one short USB capture of one device across a sleep and wake (`device_log.py capture`, from `fork-scripts.md`).
    - The packet's "What to record" points at this plan's fields table.

11. **Questions, at the packet's end, each with the recommendation:**
    - Q1, row 10: 100 KB. Recommended (worded at review, orchestrator's ruling): this run's measured peak use plus a 30 KB margin, rounded up to a whole KiB. At the smoke run's 73,828 B that is about 103 KiB, a small raise, done as a new entry after the run.
    - Q2, row 12: one USB capture now, or retire R13's serial path. Recommended: one short capture.
    - Q3, row 13: drop the "host heard advertising again ends the match" rule and its grace window. Recommended: drop it, in a follow-up after this epic.
    - Q4, row 17: a CI job that builds x4pro with the packet's defines. Recommended: yes, in its own follow-up. This also answers `## 6.19` item 2, `## 6.21` and `## e6-xr` item 4.
    - Q5, row 3: auto-sleep on the "player left" and error views. Recommended: keep it, and amend R6, R8 and AD-21's "set `preventAutoSleep`" lines with the exception (`## e6-xr` item 3).
    - Q6: confirm each deferral, one table row each with the recommendation "confirm". The rows are:
      - `## 6.1` (2), `## 6.2` (forget), `## 6.4` (2), `## 6.5` (4), `## 6.6` (7; the ADVERT item is Q3), `## 6.8`, `## 6.10` (3), `## 6.11` (Cancel frame, "Not your turn", `.chgame.zip`; the serial checks are Q2), `## 6.12` (2, judged on the device in step 6), `## 6.15` (2), `## 6.16` (3), `## 6.19` (items 1, 3, 5, 6), `## 6.13` (items 1, 3, 4), `## e6-xr` (items 1, 2).
      - `## 6.3` goes with Q2.
      - `## 6.13` item 2 is already resolved by the fix build (`game-canvas.md:175-177`), so it is listed as resolved, not to confirm (a row marked "none (resolved)").

## Verification

**Commands:**
- `python3 scripts/check_upstream_touches.py`: expected PASS (no upstream file).
- `./bin/clang-format-fix`, twice: expected no change.
- `git status --short`: expected only this plan and the packet.

**Manual checks:**
- Grep each log line and screen text the packet quotes against the files in the Code Map. Every one must be found.

### Owner's closing run: fields to fill

Each field is filled after the run, by the owner or by the orchestrator, from `merge` and `summary` on both cards. The orchestrator fills the `FILL` rows when it builds.

| Field | Value |
|-------|-------|
| Firmware commit, file, size, SHA-256 (the orchestrator) | `471eda60fc32e646b0e5fafb7638f6d218e4da68`, `crosshatch-x4pro-471eda60-diag.bin`, 6,270,032 B, SHA-256 `77f9c19255c769ab7d48d26597ef147a0936f44c0e6396867ebc7c462ac30e4e` (packages: nearby `2bef3752d7af24a2`, Ultimate tic-tac-toe `3aeb7cf5a0ffe519`, Battleship `c7a3a73bea899174`, Sudoku `411a1048bae48675`) |
| Release firmware flashed back in step 13: file and SHA-256 (the orchestrator), done on both (the owner) | `crosshatch-x4pro-471eda60-release.bin`, SHA-256 `bafeb4cc57872a80f4d6d4e0af124b5cbf5f584ded363ccbf415f0c618a00e77`; done on both: |
| Package hashes and sizes: `nearby`, `ultimate-tic-tac-toe`, `battleship`, `sudoku` (the orchestrator) | FILL |
| Inbox file sizes on each card before install (`## e5-run2`) | |
| The card log: header and `lost N bytes` notice after the mid-run delete (row 6); `[DIAG]` dropped or lost lines per device | |
| Wi-Fi refusal: after the File Transfer restart; the loaned-book path, or "not reachable" (row 11) | |
| Lobby before Start: "Player 2 left" on A, rejoin, "The host left" on B | |
| Fixture round: the rejected move (`rejected for seat`, no `state sent`), Play again, the Leave and "player left", `copies=0` on Back | |
| Radio off after leaving (New game in Nearby opens the lobby) | |
| Script error: the error view and `Script error` on the mover; `abort recv reason=script_error` and the view on the peer | |
| Ultimate tic-tac-toe round: both rejections, the headers, Play again, the Leave; `## 6.12`'s two observations | |
| Battleship round: Rotate, Random and Clear on each device, boards hidden, the rejected repeat shot, Play again, the Leave | |
| Sleep mid-match: "The other player left" on the peer (the forced exit's ABORTs) | |
| Silence: last `state` line, then `silence n=1`, then `-> PeerGone`, in ms; the reason shown on each device (row 7) | |
| Row 3: A asleep on "went silent" after Time to Sleep (yes/no, about when) | |
| Reliability per session: STATEs sent and received, adopt results, resends, silences, queue full, link refused, ABORTs | |
| Battery cost of the Battleship match, each device (first and last mV and %, times) | |
| Idle drain: the fresh-boot window (A, B) against B's after-match window (mV and % over 30 min) | |
| Heap around the first match, each device: `Free` and `Min Free` before, the lowest, after (row 10) | |
| Heap after teardown: ten cycles on A, `Free`, `Min Free` and `MaxAlloc` before and after (row 6) | |
| `GameLink` stack high-water: the least over all stops, each device | |
| `VM stopped` per game (fixture, Ultimate tic-tac-toe, Battleship, Sudoku); any stack under 512 B free flagged for epic-api-freeze | |
| `## e5-run4`: header across a pause, TIMER, NOTES AS, B6 | |
| AI-7: Sudoku sleep and Continue, Back before the first frame then Resume, the right-edge taps (`off the canvas` count) | |
| B5a, B5b, B5c: each try's result (`## e5-close`) | |
| `## e8-touch-log`: the define-on build ran, and the per-tap `touch` lines are on the card | |
| Label test (`## e6pre-5`): three run URLs with times and conclusions; the pack not cancelled by an overlapping unrelated label, the pack cancelled by an overlapping push | |
| USB capture (only if Q2 is yes): no reset on connect; lost and connected lines across a sleep | |
| The 400 ms, 10 s and 100 KB values: confirmed or changed, and where recorded | |
| Owner answers: Q1 to Q5, and the Q6 deferrals | |
| `fork-scripts.md`'s "The run without a computer" as written, or the corrections | |
| Device failures (each a new entry) | |

## Auto Run Result

**Summary:** This pass wrote the closing run's packet, `story-two-device-run-and-owner-sign-off-packet.md` (beside this plan), and this plan's fields-to-fill table. It stops before the owner's run, as the ticket is `hitl`.
- **The run:** cardless, in run order (steps 0 to 14), on A `2C8C` and B `6E68`. It covers:
  - the lobby leaves before Start;
  - a fixture round with a rejected move, Play again and Leave;
  - the script error;
  - Ultimate tic-tac-toe and Battleship rounds in Play Nearby;
  - a sleep (the forced exit's "left");
  - true silence by shielding, with row 3;
  - both idle-drain windows;
  - the ten-cycle heap;
  - the one-device checks: `## e5-run4`, AI-7, and B5a to B5c with B6.
- **Also in the packet:** the orchestrator's after-run figures (`merge` and `summary`), the label test with its overlap proof, the owner's questions Q1 to Q6 with recommendations, and its own "Fields to record" table.
- **Left for the orchestrator:** the firmware, release-firmware and package fields stay `FILL`.

**Files:** this plan, and the packet beside it.

**Review breakdown:** four context-free lenses (blind hunter, edge-case hunter, verification gap, intent alignment) gave 26 rows: 0 high, 6 medium, 16 low and 3 false, plus IA's descriptive rows.
- **Patched (22):** listed in the Review Triage Log. Four of them follow the orchestrator's rulings: Q1's wording, the label test's overlap and three URLs, the release firmware, and the packet's own fields table.
- **Rejected:** 3. **Deferred:** none.
- I applied the patches myself, because the implementation subagent had ended detached.

**Verification:** at the tree that is the commit:
- `scripts/check_upstream_touches.py`: PASS.
- `./bin/clang-format-fix`, twice: no change.
- `git status`: only the two files.
- Every quoted log line and screen text was grepped against `src`, `lib`, `games` and the fixture, and each was found.
- No firmware builds or host tests ran: the change is docs only, and the orchestrator builds the packet's firmware.

**Residual risks:**
- The silence step depends on the shielding working; the packet falls back to carrying the device out of range.
- The Wi-Fi dialog is reachable only through a loaned book.
- The low-memory refusal is not reachable from the UI.
- The post-run tasks (fields, `docs/crosshatch/`, the spine's Deferred rows, `deferred-work.md` outcomes) wait for the owner's results.

**Owner's steps:** the packet. The orchestrator first fills its `FILL` fields, then runs the label test with the owner on PR #40.

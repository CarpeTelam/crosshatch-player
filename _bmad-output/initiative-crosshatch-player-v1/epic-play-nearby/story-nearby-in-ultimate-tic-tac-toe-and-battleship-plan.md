---
title: '6.12 Nearby in Ultimate tic-tac-toe and Battleship'
type: 'feature'
ticket: '12'
created: '2026-10-12'
status: 'ready-for-dev'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: ['oversized']
deferred: []
---

<intent-contract>

## Intent

**Problem:** Both first-party two-player games declare only `pass`, and three things read wrong when each device draws its own seat (R11): Battleship's end-of-round reveal of both fleets is seat 0's alone, so a nearby device keeps its firing screen under the end dialog; Ultimate tic-tac-toe's header ignores the viewing seat; Battleship's help says the device is handed on.

**Approach:** Add `nearby` to both manifests and make the three fixes in `games/`, each also right in pass. The two companion folders gain checks of a seat's own Over screen and of a tap out of turn, plus the text and round checks the new wording changes. Neither game reads `ctx` or branches on mode.

## Boundaries & Constraints

**Always:** `games/<id>/` and the two companion folders only (the entry's `touches`). Both games keep drawing one seat per call and placing one seat at a time. Snapshots stay within 700 B (no state field changes). Every existing round and check passes unchanged, or is updated with its reason in Design Notes. Fixtures stay out of `games/`.

**Never:** read `ctx` outside the `setup` signature, or branch on mode; touch `src/**`, `lib/**`, `test/game_script/harness/**`, `test/game_script/fixtures/**`, `test/game_script/first_party/README.md`, or any upstream file (a need for one is a HALT); change `state` layouts, `fleet.lua`, or `board.lua`; add a nearby round to the games check (it plays `solo` and `pass` only and logs `nearby` skipped, R11, epic-first-party-games R10); play device rounds (entry 14's).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Battleship, own Over screen | round over, `draw(state, 1 or 2, ui)`, with or without `ui.help` | the frame seat 0 draws: winner headline, both fleets with 17 ship cells each and the misses, both shot counts; no question button; no text under the dialog band | none |
| Battleship, pass | round over, `draw(state, 0, ui)` | unchanged, command for command | none |
| Battleship, Over input | round over, a tap by seat 1 or 2 on the old question rect | no move, `ui.help` stays nil | none |
| Out of turn, either game | the seat that is not `status.turn` taps anywhere (cells, buttons, question mark) | `state` and `ui.message` unchanged (silent); the question mark still opens and any tap closes the page; a returned move is the Session's to discard | none |
| UTTT header in play | viewing seat s, turn seat t | s == t: `Player s (M): your move`; else `Player s (M): waiting` | none |
| UTTT header over | any seat | unchanged: `Player 1 (X) wins`, `Player 2 (O) wins`, `Draw` | none |

</intent-contract>

## Code Map

- `games/battleship/manifest.json`, `games/ultimate-tic-tac-toe/manifest.json` -- `"modes": ["pass"]`; the spec's modes are `pass`, `nearby` (`first-party-games.md` rows 8, 9). `Manifest` and the seat rule (`ManifestCheckTest`: nearby needs `seats.max` 2) already take it.
- `games/battleship/main.lua` -- `mode()` (l.136) returns `"over"` for seat 0 only; `input` (l.143) opens help for `seat ~= 0` before the over test; `views(state, 0)` is the reveal; `HELP_PARAGRAPHS` (l.28-37) holds both "hands the device on" (para 2, l.32) and "When the device is passed" (para 5, l.36); the file comment l.2 says "the device is passed between them".
- `games/battleship/view.lua` -- `view.draw` (l.192) branches on `seat == 0`; `drawBoth` calls `game.views(state, 0)`; `ui.help` is tested first.
- `games/ultimate-tic-tac-toe/main.lua` -- `header(state)` (l.184) and `game.draw` call; file comment l.2-3 says the seats "pass the device".
- `test/game_script/first_party/battleship/{checks,draws,taps}.lua`, `rounds/draw-commands.lua` (calls the `draws` pins), `rounds/won-by-seat-*.lua` (end `shows`).
- `test/game_script/first_party/ultimate-tic-tac-toe/{checks,taps,trace}.lua`, `rounds/`.
- Read only: `first_party/README.md` (end frame: no text starts in `host.dialog_top`..`dialog_bottom`, 259..528; stand-in metrics 8/10/14 px a char), `lib/GameCore/Session.h` (a move from any seat but the turn seat is discarded; `SessionTest.AMoveOffTurnIsDiscarded` pins it).

## Tasks & Acceptance

**Execution:**
- [ ] both `manifest.json` -- `"modes": ["pass", "nearby"]`
- [ ] `games/battleship/main.lua` -- `mode()` returns `"over"` for seat 0 or a finished round; `input` skips the question rect and returns nil when `m == "over"`; new help paragraphs 2 and 5 (Design Notes); the header comment and the `views` comment say any seat sees both fleets at Over
- [ ] `games/battleship/view.lua` -- `view.draw`: `m == "over"` takes the seat-0 branch (headline, then `drawBoth` over `game.views(state, 0)`), tested before `ui.help`, so an open page never hides the reveal
- [ ] `games/ultimate-tic-tac-toe/main.lua` -- `header(state, seat)`; `game.draw` passes `seat`; comment l.2-3 no longer says the device is passed
- [ ] `battleship/draws.lua` + `rounds/draw-commands.lua` -- new `draws.ownOver()`: for each winner and seats 1 and 2, with `ui` empty and with `help = true`, the frame equals seat 0's command for command (positive control: it differs from the same seat's frame one shot before the last); no text box starts inside the dialog band; also the `ink` list gains seat 1 and 2 over screens
- [ ] `battleship/checks.lua` -- `outOfTurn` (every phase and non-turn seat, a sweep of taps: state, `ui.message` unchanged, help still opens), `overInput` (no question rect, no move), `helpAvoidsHandOver` (no "hand", "pass", "device" in the page; the new wording present), `noCtx` (`setup` with a ctx that raises on any read gives the same state as `{mode="pass"}` and `{mode="nearby"}`), `helpPageFits` words: "look away" becomes "secret"
- [ ] `ultimate-tic-tac-toe/checks.lua` -- `headerNamesTheViewingSeat` (both turns, both seats, mid-round states; seat 0 keeps the old text; fits `L.x`..question icon under the stand-in), `ownOverScreen` (seats 0, 1, 2 over frames equal, winner header, nothing under the dialog), `outOfTurn`, `helpAvoidsHandOver`, `noCtx`
- [ ] Packaging check -- pack both games, record the new hashes (base: Battleship `e04ad7646a807f0d`, UTTT `cd11a6224228acd9`, packed at `c28120a7`)

**Acceptance Criteria:**
- Given a finished Battleship round, when seat 1 or seat 2 is drawn, then it shows both fleets and counts, clear of the dialog, with or without the help page open.
- Given Ultimate tic-tac-toe in play, when a seat is drawn, then the header names its mark and whether it is its move.
- Given the games check on both canvases, then it is green and logs `nearby` skipped for both games.
- Given the new checks and the games as at `c28120a7`, then exactly the Battleship own-Over pin and the UTTT header check are red.

## Implementation Notes

## Plan Change Log

## Review Triage Log

## Design Notes

**Owner checkpoint: the new text, verbatim.** Current text first, new second; everything else in both games is unchanged.

- Battleship help, paragraph 2 (`main.lua:30-32`). Current: "Placing: tap a square to put the next ship there, longest first. Rotate turns it. Random places the ships you have not placed yet, Clear lifts them all, and Ready hands the device on. Ships may touch but not overlap." New: "Placing: tap a square to put the next ship there, longest first. Rotate turns it. Random places the ships you have not placed yet, Clear lifts them all, and Ready finishes your placement. Ships may touch but not overlap."
- Battleship help, paragraph 5 (`main.lua:36`). Current: "When the device is passed, look away. The first to sink all five ships wins." New: "Keep your ships secret: the other player must not see your screen. The first to sink all five ships wins." Reads right in pass (the one at the device is the player whose turn it is) and in nearby; it drops "look away".
- Ultimate tic-tac-toe header in play (`main.lua:184-191`, drawn at l.201). Current: "Player 1 (X) to move" / "Player 2 (O) to move", for whichever seat is on turn, whoever views. New for the viewing seat s with mark M: "Player 1 (X): your move" / "Player 2 (O): your move" when it is s's turn, else "Player 1 (X): waiting" / "Player 2 (O): waiting" (the pattern of Battleship's "Player 2: waiting"). Seat 0, which no device shows in play, keeps "Player 1 (X) to move". The Over header stays "Player 1 (X) wins", "Player 2 (O) wins", "Draw", so `rounds/won-by-seat-*.lua`'s and `drawn.lua`'s `shows` stay as they are.
- Ultimate tic-tac-toe help: no change. Its current text already says no such thing ("Seat 1 plays X and seat 2 plays O. Take turns: ...", its first of 7 paragraphs); the check pins that none of its paragraphs says hand, pass, or device. Say if the owner wants "Seat" turned to "Player" there to match the header.

**Sources.** Fixes and scope: epic R11 and the Decision (owner, 2026-10-11, inception Q6). Out of turn: the Decision (owner, 2026-10-11, inception Q3), "Taps out of turn reach `input` and any move is discarded silently (AD-8); the games say whose turn it is (Q6)". So the games stay seat-agnostic in `apply` and `input`; the checks pin that such a tap changes nothing and says nothing, and the header and `last_shot_line` say whose turn it is. No `ctx` branch and `nearby` skipped by the games check: R11 and (owner, 2026-10-12) in R11. Verify's two reds are the Battleship own-Over pin and the UTTT header; the out-of-turn and UTTT own-Over checks are green today and pin existing behaviour.

**Why `views(state, 0)` and not a changed `views(state, seat)`.** Secrecy lives in `views` and its checks (`secrecy`, `viewsPerSeat`, `maskOverAWholeGame`) pin that seats 1 and 2 never see a fleet they should not; Over is "the frame for everyone" (`Session::draw`, seat 0), so `view.draw` takes the seat-0 branch for any seat once over. No existing check changes. The help page loses to Over only for seats 1 and 2; seat 0 never has `ui.help`.

**Guards in code that moves.** `input` keeps its order: help closes on any tap first; `ui.message` clears; a rejected event sets the message; the question rect (now not at Over) before the over test, which still returns nil with no move for a finished round (`inputFiring` pins it). `mode()` gains the over case only; seat 0 in play still maps to `"over"` and still gives no move. No `git log -L` guard is dropped: `view.draw`'s `seat == 0` branch is kept as `m == "over"`, with seat 0's in-play frame (title only) unchanged.

**Known edge, not fixed.** UTTT shows its help page under the end dialog if a round ends while a nearby device has the page open; Battleship's reveal wins over its page, so it has no such edge. R11 names no UTTT fix for it; the review step records it in `deferred`, `low`.

**Doubles.** The new checks add no double: they call the games, `trace.record` (existing, documented), and `host.dialog_*`. The stand-in text metrics (8/10/14 px a char) prove the header and help fit only under those metrics, so fit on the device is unmeasured until entry 14's runs.

**Budget.** No `src/` or `lib/` file changes, so the epic's 9,504 B flash and 160 B static RAM share is untouched by construction; unmeasured (nothing to measure). The packages' hashes change, so pass saves of `1.6.5-ch.4` are discarded (AD-17, epic Decision 2026-10-11).

## Verification

**Commands** (host tests under `flock /tmp/crosshatch-hosttest.lock`; no firmware build, since no firmware file changes):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test --target GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest && ctest --test-dir build/test -L games-check --output-on-failure` -- expected: green at both canvases; the log notes `battleship: declared mode nearby skipped` and the same for `ultimate-tic-tac-toe`.
- Red against today's scripts: copy `git show c28120a7:games` into a scratch tree, run with `-DGAMES_CHECK_GAMES_ROOT=<scratch>/games`, then `-UGAMES_CHECK_GAMES_ROOT` -- expected: red only in `TheGamesOwnChecksPass` and `EveryRoundPlaysAsItsFileSays` naming Battleship's own-Over pin (`draw-commands`) and UTTT's header check; everything else green. Record the failure text.
- Full host suite: `cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass.
- `python3 scripts/pack_game.py games/battleship <scratch>` and `... games/ultimate-tic-tac-toe <scratch>` -- expected: both pack; hashes recorded and different from the bases above.
- Every `python3 scripts/<name>_test.py`; `python3 scripts/check_upstream_touches.py`; `./bin/clang-format-fix` twice, `git status` clean after the second -- expected: pass.

**Manual checks:**
- Snapshot size: the harness fails a round over 700 B; none does.
- Optional, if `sim.sh` installs the games cheaply: a pass-mode simulator screenshot of the UTTT header and of Battleship's Over screen (real fonts, so it shows fit); else say unmeasured.

## Auto Run Result

Status: ready-for-dev. Halted after planning at the owner plan checkpoint (entry 12); no code is changed, nothing is committed, the tree holds only this plan.

- **Owner checkpoint:** the three changed strings (Battleship help paragraphs 2 and 5, Ultimate tic-tac-toe's in-play header) are in Design Notes verbatim with the current text beside each; UTTT's help is proposed unchanged. Alternatives the owner may want: "Player" for "Seat" in UTTT's help; "look away" kept in Battleship's paragraph 5.
- **Intent gaps:** none. The out-of-turn reading is settled by the epic's Decision (owner, 2026-10-11, inception Q3): the game stays seat-agnostic and the Session discards the move; the checks pin that such a tap changes nothing and says nothing.
- **Files (planned):** both `games/*/manifest.json`; `games/battleship/{main,view}.lua`; `games/ultimate-tic-tac-toe/main.lua`; `test/game_script/first_party/battleship/{checks,draws}.lua` and `rounds/draw-commands.lua`; `test/game_script/first_party/ultimate-tic-tac-toe/checks.lua`. All inside the entry's `touches`; none needs `stays_out`.
- **Budget:** no firmware file changes; the epic share is untouched by construction (unmeasured). Base package hashes at `c28120a7`: Battleship `e04ad7646a807f0d`, UTTT `cd11a6224228acd9`.
- **Residual risks:** header and help fit are proven under the harness's stand-in metrics only; the device run (entry 14) shows real fit. The plan exceeds 1,600 tokens (`oversized`) because it carries the verbatim text.

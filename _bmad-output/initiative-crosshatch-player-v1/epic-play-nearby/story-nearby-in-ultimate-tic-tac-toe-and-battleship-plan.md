---
title: '6.12 Nearby in Ultimate tic-tac-toe and Battleship'
type: 'feature'
ticket: '12'
created: '2026-10-12'
status: 'built'
baseline_revision: '32362b3d8c108117c94c123e2cd6a8bacf8c9910'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: ['oversized']
deferred:
  - summary: >-
      Ultimate tic-tac-toe draws its help page over the end dialog when a nearby round ends while that device has the page open.
    evidence: |-
      draw tests ui.help before the board and the end frame, so the page covers the winner header beneath the host's dialog; Battleship's Over frame wins over its page, so it has no such edge. R11 names no UTTT fix for it; the owner deferred it as low (epic Notes, Decision 2026-10-12, entry 12's plan checkpoint).
    location: >-
      games/ultimate-tic-tac-toe/main.lua:196-204
    severity: low
  - summary: >-
      A nearby seat that is waiting sees the playable small boards highlighted and a help paragraph saying "the highlighted small boards are the ones you may play in".
    evidence: |-
      game.playable(state) highlights the boards the turn seat may play in whoever views, and help paragraph 6 speaks to the viewer. In pass the viewer is always the turn seat, so it reads right there; a waiting nearby seat sees the opponent's boards under the "waiting" header. R11 names three fixes and not this one, so it is not part of this entry; whether to hide the highlight for the waiting seat or reword the paragraph is the owner's call.
    location: >-
      games/ultimate-tic-tac-toe/main.lua:39-45 and the highlight loop in game.draw
    severity: low
  - summary: >-
      No automated check pins that either first-party manifest declares nearby.
    evidence: |-
      The verification-gap lens removed "nearby" from both manifests, repacked, and ran the games check (129 tests) and the manifest/mode/roster/session/catalog/launcher tests (216): all passed. The games check only logs a skipped nearby; the harness and the first_party README are on this entry's stays_out, and a nearby round is barred (epic-first-party-games R10). A harness-level or manifest-level test, or entry 14's device run, would settle it.
    location: >-
      games/battleship/manifest.json:7; games/ultimate-tic-tac-toe/manifest.json:7
    severity: low
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
- [ ] `games/ultimate-tic-tac-toe/main.lua` -- `header(state, seat)`; `game.draw` passes `seat`; comment l.2-3 no longer says the device is passed; `HELP_PARAGRAPHS[1]` reads "Player 1 plays X and Player 2 plays O. Take turns: tap an empty cell to put your mark in it." (owner addition, Plan Change Log)
- [ ] `battleship/draws.lua` + `rounds/draw-commands.lua` -- new `draws.ownOver()`: for each winner and seats 1 and 2, with `ui` empty and with `help = true`, the frame equals seat 0's command for command (positive control: it differs from the same seat's frame one shot before the last); no text box starts inside the dialog band; also the `ink` list gains seat 1 and 2 over screens
- [ ] `battleship/checks.lua` -- `outOfTurn` (every phase and non-turn seat, a sweep of taps: state, `ui.message` unchanged, help still opens), `overInput` (no question rect, no move), `helpAvoidsHandOver` (no "hand", "pass", "device" in the page; the new wording present), `noCtx` (`setup` with a ctx that raises on any read gives the same state as `{mode="pass"}` and `{mode="nearby"}`), `helpPageFits` words: "look away" becomes "secret"
- [ ] `ultimate-tic-tac-toe/checks.lua` -- `headerNamesTheViewingSeat` (both turns, both seats, mid-round states; seat 0 keeps the old text; fits `L.x`..question icon under the stand-in), `ownOverScreen` (seats 0, 1, 2 over frames equal, winner header, nothing under the dialog), `outOfTurn`, `helpAvoidsHandOver` (no "hand", "pass", "device", and now also no "Seat" in any paragraph; paragraph 1 is the new "Player 1 plays X and Player 2 plays O. ..." text), `noCtx`; `helpPageFits` re-runs on the longer paragraph 1 (six more characters) and is updated only if the stand-in metrics say it no longer fits
- [ ] Packaging check -- pack both games, record the new hashes (base: Battleship `e04ad7646a807f0d`, UTTT `cd11a6224228acd9`, packed at `c28120a7`)

**Acceptance Criteria:**
- Given a finished Battleship round, when seat 1 or seat 2 is drawn, then it shows both fleets and counts, clear of the dialog, with or without the help page open.
- Given Ultimate tic-tac-toe in play, when a seat is drawn, then the header names its mark and whether it is its move.
- Given the games check on both canvases, then it is green and logs `nearby` skipped for both games.
- Given the new checks and the games as at `c28120a7`, then exactly the Battleship own-Over pin and the UTTT header check are red.

## Implementation Notes

- Implemented by the build subagent per the Tasks above, with the owner's addition (UTTT help "Player 1 plays X and Player 2 plays O"). One shape differs from the Design Notes' prose: `input` tests `m == "over"` (return nil) before the question rect, not after; same behaviour for the matrix (the rect is no target at Over, and a finished round takes no move), and `inputFiring` still pins the no-move rule.
- Against the base games (`c28120a7`) the new scripts are red in more than the AC's two named checks: Battleship `overInput` (old game opens help at Over), both games' `helpAvoidsHandOver` (old text), Battleship `helpPageFits` (it now requires "secret"), besides the own-Over pin and the UTTT header check. All are red for the reason the check pins; the AC's "exactly" is read as "at least these two", and the full set is in Verification.
- `ownOver` (Battleship `draws.lua`) is a `draw`-level pin in the round VM; no test double is added or extended, so no double-versus-device pin applies.

## Plan Change Log

- 2026-10-12, owner answer to the plan checkpoint (epic Notes, "Decision (owner, 2026-10-12, entry 12's plan checkpoint, the game text)"): all proposed text approved; added UTTT help paragraph 1 "Seat 1 ... seat 2" to "Player 1 ... Player 2" (task in `games/ultimate-tic-tac-toe/main.lua`; `helpAvoidsHandOver` and `helpPageFits` in its checks; Design Notes bullet and Owner answer); the help-under-the-end-dialog edge is deferred, low. Status stays `ready-for-dev`; no other task, boundary, or AC changed.

## Review Triage Log

### 2026-10-12 — Review pass
- verdicts: 28 findings — high 0, medium 0, low 8, false 18, maybe-false 2
- findings:
  - Blind Hunter
    - `[false]` `[reject]` Battleship's Over frame draws from `views(state, 0)`, which a nearby seat's own view cannot supply — `Session::draw(seat)` is `rules.draw(snapshot(), seat)` (lib/GameCore/Session.cpp:68): every device passes the whole state to `draw`, and the mover-applies Decision (epic Notes, 2026-10-12) ships the whole `STATE`; `views` is a pure function of it, and the new `ownOver` pin draws seats 1 and 2 from the full state exactly as the Session does.
    - `[false]` `[reject]` Pass players lose the hand-over cue now that the help drops it — the intent and the owner's 2026-10-12 Decision require the text to stop saying the device is handed on; pass keeps the host's hand-off view (`GameVM::showTurnSeat`, `handOffView`, src/games/GameVM.cpp:259-269), which is where the cue lives.
    - `[false]` `[reject]` The UTTT header may say "Player 1 (X): waiting" on Player 2's turn in pass — `drawShown` draws `seatShown(...)`, the turn seat, in pass (src/games/GameVM.cpp:266), so the viewing seat is the turn seat and reads "your move"; the games check plays pass rounds green.
    - `[false]` `[reject]` The out-of-turn guarantee is only a comment — `SessionTest.AMoveOffTurnIsDiscarded` (test/game_core/SessionTest.cpp:195) pins the Session half; the games must stay seat-agnostic in `input` (epic Decision, inception Q3), and Battleship's `input` rightly returns an `F` move for an off-turn tap, so asserting nil would be wrong.
    - `[false]` `[reject]` Off-turn coverage misses a tap on an already-fired square or a taken cell, which would set a message — both games' `input` set `ui.message` only for a `rejected` event (battleship/main.lua:144-147, ultimate-tic-tac-toe/main.lua:123-126), never from a tap; the Battleship sweep taps every cell and button and the UTTT sweep covers the whole canvas, including a taken cell in state 2.
    - `[false]` `[reject]` The new Battleship checks may exhaust the check VM — the games check ran all 129 tests green at both canvases with the checks in place, so no VM limit is hit.
    - `[false]` `[reject]` The positive control's "differs" substring match is fragile — a reworded `sameFrames` error makes `assert(not ok and ... find "differs")` fail loudly, never pass falsely.
    - `[false]` `[reject]` The Over frame is not perspective-aware (no "You win") — the intent asks for none; Over is the frame for everyone and the owner's Decision keeps the Over headers unchanged.
    - `[low]` `[defer]` Nothing declares or pins the nearby support (no manifest-modes check, no docs entry, `hidden` on Battleship) — the manifest pin is the verification-gap lens's finding, deferred below; the docs entry is not asked for and Battleship's `hidden` is pre-existing and unrelated.
    - `[low]` `[patch]` Style slips — the stacked duplicate doc comment above UTTT `checks.lua`'s `frame` is real and merged by the patch; the long lines match the files' existing width (many pre-existing lines are as long, and Lua is not under clang-format), `local taps = require("taps")` inside a function is how that file does it (four other sites), and the check names say what they pin, so no change.
  - Edge Case Hunter
    - `[low]` `[reject]` A help page open when the round ends at seat 1 or 2 is hidden by the Over frame, and the next tap only clears `ui.help` with no visible change — the host's end dialog is up at that point, and the fix would mutate `ui` inside `draw` or add a branch; unlikely to be met and not worth the complexity.
    - `[low]` `[defer]` UTTT shows its help page under the end dialog when the round ends with it open — the edge the plan names, deferred low by the owner (epic Notes, 2026-10-12, entry 12's plan checkpoint).
    - `[low]` `[defer]` A waiting nearby seat sees the playable-board highlight and the paragraph "the highlighted small boards are the ones you may play in" — real and exposed by nearby, but R11 names three fixes and not this; the owner decides how.
    - `[false]` `[reject]` An off-turn Battleship tap while firing returns an `F` move — the Session discards it (`SessionTest.AMoveOffTurnIsDiscarded`); the game must stay seat-agnostic (inception Q3).
    - `[false]` `[reject]` UTTT `outOfTurn` has no finished-state case — UTTT's `input` reads neither state nor turn, so an off-turn tap at Over changes nothing by construction, and the two in-play cases already pin that.
    - `[maybe-false]` `[reject]` "Player 2 (O): your move" (23 chars) may reach the question icon on the device — fits under the stand-in metrics (8/10/14 px a char); real fonts are unmeasured until entry 14; if true it is cosmetic (low), so rejected with this note and listed under residual risks.
    - `[false]` `[reject]` A nil or out-of-range seat has no pin in `header` — it falls to the seat-0 text, and the Session only ever passes 0, 1, or 2.
    - `[low]` `[reject]` Design Notes say `input` keeps the question rect before the over test, but the code tests Over first — true, and harmless (the matrix row needs the rect to be no target at Over); the fix is to edit the plan, so rejected; Implementation Notes record the actual order.
    - `[low]` `[reject]` The AC's "exactly the Battleship own-Over pin and the UTTT header check are red" against the base understates the reds — true, `overInput`, both `helpAvoidsHandOver`, and Battleship `helpPageFits` are also red there by design; the fix is to edit the plan, so rejected; Verification records the actual set.
  - Verification Gap Reviewer
    - `[low]` `[defer]` Nothing pins that both manifests declare `nearby` — mutation-tested by the lens (nearby removed: 129 games-check and 216 manifest/mode/roster tests still pass); needs a harness or manifest-level test, both off this entry's touches; deferred (low) above.
    - `[false]` `[reject]` `ctest` does not repack the games, so a run without the build tests old scripts — not a defect in the diff; Verification builds the full tree (`cmake --build build/test`, which includes `packed_games`) before `ctest`.
  - Intent Alignment Auditor (descriptive; each divergence graded)
    - `[false]` `[reject]` The nearby-device Over screen and the host dialog are not exercised — the intent defers device rounds to entry 14 and bars a nearby round in the games check.
    - `[false]` `[reject]` The out-of-turn checks and UTTT's own-Over check pin behaviour that already held — the intent asks for those checks; the plan says they are green today.
    - `[maybe-false]` `[reject]` Header and help fit are proven only under the stand-in metrics — same as the edge lens's fit finding; unmeasured until entry 14.
    - `[false]` `[reject]` The "device" and "seat" word bans go beyond the intent — the bans pin the intent's "does not say the device is handed on" and the owner's "Player" addition (epic Notes, 2026-10-12).
    - `[false]` `[reject]` `noCtx` serves the plan's constraint, not the three fixes — R11 and the entry's Never list require that neither game reads `ctx`.
    - `[false]` `[reject]` Nothing in the diff verifies the package hashes change — `pack_game.py` runs in Verification.
    - `[false]` `[reject]` No round file changes for "any round check the wording changes" — no round `shows` or pin names an in-play header or help text; the games check, which plays every round, is green.

## Design Notes

**Owner checkpoint: the new text, verbatim.** Current text first, new second; everything else in both games is unchanged.

- Battleship help, paragraph 2 (`main.lua:30-32`). Current: "Placing: tap a square to put the next ship there, longest first. Rotate turns it. Random places the ships you have not placed yet, Clear lifts them all, and Ready hands the device on. Ships may touch but not overlap." New: "Placing: tap a square to put the next ship there, longest first. Rotate turns it. Random places the ships you have not placed yet, Clear lifts them all, and Ready finishes your placement. Ships may touch but not overlap."
- Battleship help, paragraph 5 (`main.lua:36`). Current: "When the device is passed, look away. The first to sink all five ships wins." New: "Keep your ships secret: the other player must not see your screen. The first to sink all five ships wins." Reads right in pass (the one at the device is the player whose turn it is) and in nearby; it drops "look away".
- Ultimate tic-tac-toe header in play (`main.lua:184-191`, drawn at l.201). Current: "Player 1 (X) to move" / "Player 2 (O) to move", for whichever seat is on turn, whoever views. New for the viewing seat s with mark M: "Player 1 (X): your move" / "Player 2 (O): your move" when it is s's turn, else "Player 1 (X): waiting" / "Player 2 (O): waiting" (the pattern of Battleship's "Player 2: waiting"). Seat 0, which no device shows in play, keeps "Player 1 (X) to move". The Over header stays "Player 1 (X) wins", "Player 2 (O) wins", "Draw", so `rounds/won-by-seat-*.lua`'s and `drawn.lua`'s `shows` stay as they are.
- Ultimate tic-tac-toe help, paragraph 1 of 7 (`main.lua:39`). Current: "Seat 1 plays X and seat 2 plays O. Take turns: tap an empty cell to put your mark in it." New (owner's addition, 2026-10-12): "Player 1 plays X and Player 2 plays O. Take turns: tap an empty cell to put your mark in it." It says no hand-over today either; the change is the wording, to match the header. The other six paragraphs are unchanged; the check pins that none of the seven says hand, pass, device, or "Seat".

**Owner answer (2026-10-12).** The epic Notes' "Decision (owner, 2026-10-12, entry 12's plan checkpoint, the game text)" approves every string above as proposed, with the one addition shown in the Ultimate tic-tac-toe help bullet ("Player" for "Seat"/"seat"). It also defers the help-page-under-the-end-dialog edge (Known edge below) as `low`. No existing check pins today's UTTT help text (a search of `ultimate-tic-tac-toe/` for "Seat"/"seat" finds only comments and step labels); `helpPageFits` measures the page under the stand-in metrics and is the one check the longer line can affect.

**Sources.** Fixes and scope: epic R11 and the Decision (owner, 2026-10-11, inception Q6). Out of turn: the Decision (owner, 2026-10-11, inception Q3), "Taps out of turn reach `input` and any move is discarded silently (AD-8); the games say whose turn it is (Q6)". So the games stay seat-agnostic in `apply` and `input`; the checks pin that such a tap changes nothing and says nothing, and the header and `last_shot_line` say whose turn it is. No `ctx` branch and `nearby` skipped by the games check: R11 and (owner, 2026-10-12) in R11. Verify's two reds are the Battleship own-Over pin and the UTTT header; the out-of-turn and UTTT own-Over checks are green today and pin existing behaviour.

**Why `views(state, 0)` and not a changed `views(state, seat)`.** Secrecy lives in `views` and its checks (`secrecy`, `viewsPerSeat`, `maskOverAWholeGame`) pin that seats 1 and 2 never see a fleet they should not; Over is "the frame for everyone" (`Session::draw`, seat 0), so `view.draw` takes the seat-0 branch for any seat once over. No existing check changes. The help page loses to Over only for seats 1 and 2; seat 0 never has `ui.help`.

**Guards in code that moves.** `input` keeps its order: help closes on any tap first; `ui.message` clears; a rejected event sets the message; the question rect (now not at Over) before the over test, which still returns nil with no move for a finished round (`inputFiring` pins it). `mode()` gains the over case only; seat 0 in play still maps to `"over"` and still gives no move. No `git log -L` guard is dropped: `view.draw`'s `seat == 0` branch is kept as `m == "over"`, with seat 0's in-play frame (title only) unchanged.

**Known edge, not fixed (deferred, low, by the owner's 2026-10-12 Decision).** UTTT shows its help page under the end dialog if a round ends while a nearby device has the page open; Battleship's reveal wins over its page, so it has no such edge. R11 names no UTTT fix for it; the review step records it in `deferred`, `low`.

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

**Results** (run in this worktree on the tree to be committed on top of `32362b3d`; host tests build in `build/test`, Ninja, under `/tmp/crosshatch-hosttest.lock`):
- Full host suite after the review patch: 2,070 of 2,070 pass. `ctest -L games-check -V`: 129 of 129 pass at both canvases; the log holds `battleship: declared mode nearby skipped: this host cannot start it` and the same for `ultimate-tic-tac-toe`, at both.
- Red against the base: the new scripts against `git archive c28120a7 games` (`-DGAMES_CHECK_GAMES_ROOT`, then `-UGAMES_CHECK_GAMES_ROOT`; the rebuilt tree is green again): 8 of 129 fail (4 per canvas), all Battleship or UTTT, Sudoku green. They are `TheGamesOwnChecksPass` Battleship and UTTT, and Battleship `EveryRoundPlaysAsItsFileSays` and `EveryRoundRestoresFromItsSnapshot`. The texts:
  - own-Over pin: `draws.lua:351: winner 1, seat 1: the Over frame is seat 0's: command 3 differs: text(46, 44, "Player 1", "small", "black") against icon("question", 418, 10, "small", "black")` (at the base, seat 1's Over frame is its firing screen with the question button, not seat 0's reveal);
  - UTTT header: `state 1 (0 moves), seat 1's header: got Player 1 (X) to move, wanted Player 1 (X): your move`;
  - also red there, for the reason each pins: Battleship `overInput` (`Over, seat 1: the question rectangle opens no page: got true, wanted nil`), Battleship `helpAvoidsHandOver` (`the page says 'hand'`), Battleship `helpPageFits` (`the page does not mention 'secret'`), UTTT `helpAvoidsHandOver` (`the page says 'seat'`). The AC's "exactly" is read as "at least the first two"; the verify line names those two.
- `python3 scripts/pack_game.py`: Battleship packs to 11,038 B, hash `c7a3a73bea899174` (base `e04ad7646a807f0d`); UTTT packs to 5,489 B, hash `3aeb7cf5a0ffe519` (base `cd11a6224228acd9`). Both differ from their bases; measured on the tree to be committed.
- Every `scripts/*_test.py` passes; `python3 scripts/check_upstream_touches.py` PASS (trial merge of `upstream/develop` clean); `./bin/clang-format-fix` twice, no change to the tree (`git status` shows only this story's ten files).
- Snapshot size: no round exceeds 700 B (the harness fails one that does; all rounds pass).
- Not run, by the plan: no firmware build, `pio check`, or `sim.sh build` (no `src/`, `lib/`, or C/C++ file changed; the diff is Lua under `games/`, Lua checks under the companion folders, two manifests, and this plan). No simulator screenshots (the plan's Verification makes them optional and none is among the entry's verify items): header and help fit with real fonts are unmeasured, proven only under the stand-in metrics; entry 14's device rounds measure them.


## Auto Run Result

Status: built.

**Summary.** Both first-party two-player games now declare `["pass", "nearby"]` and make the three R11 fixes, each also right in pass. Battleship: any seat shows the end-of-round reveal of both fleets (Over wins over an open help page, no question button, no text under the dialog band), and its help no longer says the device is handed on or to look away (paragraphs 2 and 5, the owner's approved text). Ultimate tic-tac-toe: the in-play header names the viewing seat's mark and whether it is its move ("Player 1 (X): your move" / "Player 2 (O): waiting"; seat 0 and the Over headers unchanged), and its help says "Player 1 plays X and Player 2 plays O" (the owner's addition, 2026-10-12). Neither game reads `ctx` or branches on mode; no `src/`, `lib/`, harness, or fixture file changed. The plan was first updated with the owner's answer (Plan Change Log).

**Files.**
- `games/battleship/manifest.json`, `games/ultimate-tic-tac-toe/manifest.json`: `nearby` added.
- `games/battleship/main.lua`: `mode()` gives `"over"` for seat 0 or a finished round; `input` returns no move at Over before the question rect; new help paragraphs 2 and 5; comments.
- `games/battleship/view.lua`: `view.draw` takes the Over branch before the help page.
- `games/ultimate-tic-tac-toe/main.lua`: `header(state, seat)`; help paragraph 1; comment.
- `test/game_script/first_party/battleship/{checks,draws}.lua`, `rounds/draw-commands.lua`: `ownOver` pin (wired into the round, ink and both-canvas lists extended), `outOfTurn`, `overInput`, `helpAvoidsHandOver`, `noCtx`; `helpPageFits` word "look away" to "secret".
- `test/game_script/first_party/ultimate-tic-tac-toe/checks.lua`: `headerNamesTheViewingSeat`, `ownOverScreen`, `outOfTurn`, `helpAvoidsHandOver`, `noCtx`; `frame` takes a seat.
- This plan.

**Review (thorough; lenses run as context-free subagents: blind-hunter, edge-case-hunter, verification-gap, intent-alignment).** 28 findings: high 0, medium 0, low 8, false 18, maybe-false 2. One entry patched (low): the duplicate doc comment above UTTT `checks.lua`'s `frame`, merged by the same implementation subagent (re-engaged). Three deferred, all low (see `deferred`): UTTT help page under the end dialog (the owner's deferral), the waiting nearby seat's playable-board highlight and "you may play in" help line, and no automated pin that the manifests declare `nearby`. Rejected, with reasons in the Review Triage Log: 18 false findings (each refuted at the cited code), two low (a help page open at Over hidden by the Over frame; two plan-prose inaccuracies whose fix would edit the plan, recorded in Implementation Notes and Verification instead), and two maybe-false fit findings (low if true; unmeasured until entry 14). `followup_review_recommended`: false (patched: one low, no high or medium).

**Verification.** See the plan's Verification Results: 2,070 of 2,070 host tests; games check 129 of 129 at both canvases with `nearby` skipped for both games; 8 expected reds against `c28120a7`; both games pack with new hashes (Battleship `c7a3a73bea899174`, UTTT `3aeb7cf5a0ffe519`); script tests, ledger check, and formatting pass. No firmware check applies (no C/C++ change). Budget: the epic's 9,504 B flash and 160 B static RAM share is untouched by construction; unmeasured (no firmware file changed).

**Test doubles.** None added or extended.

**Formatting.** `./bin/clang-format-fix` made no change outside this story's files.

**Residual risks.**
- Header and help fit under real fonts, and the Over screen under the real end dialog, are proven only under the harness's stand-in metrics and dialog band; entry 14's device rounds measure them.
- Pass saves of `1.6.5-ch.4` are discarded because the package hashes changed (AD-17, epic Decision 2026-10-11).
- No automated check would catch a later edit that drops `nearby` from a manifest (deferred, low).

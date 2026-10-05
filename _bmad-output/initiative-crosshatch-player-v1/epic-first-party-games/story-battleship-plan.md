---
title: 'Battleship'
type: 'feature'
ticket: '2'
created: '2026-10-05'
baseline_revision: 'd0d488a2fa1a71556924f1dce497f00c5d16a864'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/test/game_script/first_party/README.md'
  - '{project-root}/games/ultimate-tic-tac-toe/main.lua'
  - '{project-root}/test/game_script/first_party/ultimate-tic-tac-toe/checks.lua'
  - '{project-root}/test/game_script/first_party/ultimate-tic-tac-toe/taps.lua'
  - '{project-root}/docs/crosshatch/api-level-1.txt'
warnings: [oversized]
deferred:
  - summary: >-
      Nothing observes the non-text commands a seat's draw emits, so a draw that read the other seat's fleet directly (not
      through `views`) would leak ship positions on the target board and stay green.
    evidence: |-
      Checks cannot call `draw` (`ch.gfx` outside draw is an error, `ScriptVm.h:9`), and a round's `shows` matches only
      text commands of the step seat's frame (`RoundPlayer.cpp` `shows`). Secrecy is pinned on `game.views` and
      `fleet.mask` (checks `secrecy`, `viewsPerSeat`, `maskOverAWholeGame`; mutant (e)); by reading, `drawFiring` draws
      `views(...).target` only. Closing it needs a harness hook (a frame-command inspection in rounds or a recording
      `ch.gfx` for checks), and `test/game_script/harness/**` is in this ticket's Never list.
    location: >-
      test/game_script/harness/games_check/RoundPlayer.cpp (shows); games/battleship/main.lua drawFiring
    severity: medium
---

<intent-contract>

## Intent

**Problem:** Battleship, the hidden-information game of the epic, does not exist: nothing in `games/` exercises a `hidden` pass manifest, a move that keeps the turn, or per-seat drawing, and the games check (entry 8) has no hidden first-party game to play.

**Approach:** Add `games/battleship/` (R1, R7, R8, R9) as the entry's description says: placement one seat at a time (ships touching but not overlapping; each ship a move that keeps the turn; rotate in the seat's `ui`; Random, Clear, and Ready as moves), then one shot per turn with hit, miss, or sunk shown to the mover; the target board drawn large with library icons and the seat's own fleet small with `ch.gfx` shapes; seat 0 at Over both fleets; a HOW TO PLAY page behind a `question` icon button. Add its companion folder `test/game_script/first_party/battleship/` (rounds in the Decision C1 format, a `checks.lua` in the C2 interface), play it through entry 8's check, and show it running in the simulator.

## Boundaries & Constraints

**Always:** Every new file is under `games/battleship/`, `test/game_script/first_party/battleship/`, or is this plan or its screenshots; no upstream file changes, so `check_upstream_touches.py` stays green with no ledger edit. `games/battleship/` is self-contained (R2): its own text (not `tr()`), `manifest.json` (id `battleship`, `api` 1, `modes` `["pass"]`, `seats` 2/2, `hidden` true, no `icon`), `main.lua`, and its own modules; it shares no file or `require` with another game (it does not use `board.lua`, a 9×9 module; Battleship is 10 × 10) and names no repository path. Every drawing command, icon, and `ctx` field stays within `api-level-1.txt`; every snapshot at or under 700 B (R9); each `draw` shows only its seat's fleet and its own shots (seat 0, at Over, both fleets); Lua is 5.5 (`global` is reserved); no call nears the 2,000,000-instruction budget or `frame_commands_count`. A move is at most 256 B and a reject reason at most 64 B. Rounds and checks name no path outside their companion folder.

**Never:** Change `src/`, `lib/`, `docs/crosshatch/api-level-1.txt`, `API_SURFACE_CRC`, `platformio.ini`, the freeink-sdk pointer, `.skills/`, `.github/workflows/**`, `test/game_script/harness/**`, `test/game_script/fixtures/**`, `first_party/README.md`, `games/ultimate-tic-tac-toe/`, `games/sudoku/`, or any upstream file; add `nearby` to the manifest (epic-play-nearby does), a toggle between boards, or an API entry; name a game in `test/game_script/harness/` or `scripts/`; commit scratch trees or generated files; put a fixture or engine test in `games/`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Place a ship | placing seat taps a board cell; ships still to place | the next ship (5, 4, 3, 3, 2 in that order) is placed at the cell, across (down after Rotate), shifted back so it fits the board; the turn stays | an overlap is rejected "Ships cannot overlap", state unchanged |
| Ships touch | a ship placed edge to edge with another | accepted | none |
| Rotate | tap Rotate | the seat's `ui` flips across/down; no move | none |
| Random | some ships placed | every remaining ship placed at random, legal, earlier ones kept; turn stays | all five placed: "All five ships are placed" |
| Clear | at least one ship placed | every ship lifted; turn stays | none placed: "No ships to clear" |
| Ready | five placed | the turn passes (seat 1 to seat 2; seat 2 starts firing, seat 1 fires first); the runtime's Result and hand-off hide the fleet | fewer than five: "Place all five ships first" |
| Fire | firing turn, an unshot cell of the other fleet | miss, hit, or sunk (with the ship's name) shown to the mover in Result; the turn passes | a cell already shot: "You already fired there" |
| Last ship sunk | the shot sinks the fifth ship | round over, `winners = {shooter}`, seat 0's frame shows both fleets | a move after the round is over: "The game is over" |
| Secrecy | any frame of seat 1 or 2 | holds that seat's own fleet and its own shots; intact enemy ships never appear | none |
| How to play | tap `question`; then any tap | the page replaces the boards, no move; the next tap closes it | none |
| Wrong phase / malformed | a shot during placement, a placement move while firing, a non-table, out-of-range, or non-integer cell | rejected with a reason within 64 B, state unchanged | the reason names what to do |
| Snapshot | every step of every round, every seat drawn | at most 700 B; no fault; frame within limits | the check fails the round |

</intent-contract>

## Code Map

Read, never edit (all under `/home/user/epic-first-party-games-lane-a/`):
- `games/ultimate-tic-tac-toe/main.lua` -- the shape of a finished first-party game: `setup`/`status`/`apply`/`input`/`draw`, `rejected` events into `ui.message`, the HOW TO PLAY page (`wrap` with `ch.text_width`, `help_lines`, a `question` icon button, any tap closes), extra game-table fields (`playable`, `help_lines`) that `checks.lua` calls; copy the patterns, share no file (R2).
- `test/game_script/first_party/ultimate-tic-tac-toe/{checks.lua, taps.lua, rounds/*.lua}` -- the same for the companion side: a `taps` helper turning moves into canvas pixels through the game's layout module; checks as `{name, run}` with `eq` helpers, `rejects` helper, a `helpPageFits` check; rounds as chunks returning `{mode, steps, winners | unfinished}` with `shows` and `move = false`.
- `test/game_script/first_party/README.md` -- C1 (rounds: for a hidden pass round the step `seat` follows the hand-off: the placing or turn seat; a step after the round is over fails; `shows` is text one of the seat's frame's text commands contains) and C2 (`checks.lua`). `docs/crosshatch/game-canvas.md`, Hidden pass states -- after a turn-passing move the mover's own frame is shown in Result under a bottom banner (y about 649 to 780 on the X4 Pro) and then the hand-off; the end-of-round menu is a dialog centred on the canvas (about y 268 to 540, x 50 to 430) over seat 0's frame.
- `test/game_script/fixtures/pass-hidden/main.lua` -- a hidden two-seat game; `docs/crosshatch/api-level-1.txt` (icons `fire`, `waves`, `dot-outline`, `boat`, `arrow-clockwise`, `shuffle`, `trash`, `check`, `question`; `ch.gfx` functions, `align`; limits); `docs/crosshatch/game-icons.md`; `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md`, sections 2, 3, 4; `scripts/pack_game.py` (a package holds `manifest.json`, `main.lua`, and `[a-z0-9_]{1,32}.lua` modules).
- `.claude/skills/run-crosshatch-player/SKILL.md` -- the simulator driver (screenshots, after the review, by the build agent, not the implementer).

New:
- `games/battleship/{manifest.json, main.lua, fleet.lua, layout.lua}`.
- `test/game_script/first_party/battleship/{checks.lua, taps.lua, rounds/{won-by-seat-1,won-by-seat-2,random-placement,clear,rejected-moves,how-to-play}.lua}`.
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-battleship-screenshots/*.png` (the build agent, after the review).

## Tasks & Acceptance

**Execution:**
- [ ] `games/battleship/manifest.json` -- id `battleship`, name "Battleship", version "1.0.0", api 1, seats 2/2, modes `["pass"]`, hidden true, no icon -- R1.
- [ ] `games/battleship/fleet.lua` -- the pure rules of Design Notes (ship table, `place`, `random_fill`, `shot`, `mask`, counts); no `ch` use -- R7, R9.
- [ ] `games/battleship/layout.lua` -- the pure geometry of Design Notes (`compute(w, h)`, `cell_rect`, `cell_at`, `inside`); no `ch` use -- R7.
- [ ] `games/battleship/main.lua` -- `setup`, `status`, `apply`, `input`, `draw`, plus `views(state, seat)`, `help_lines()`, and `headline(state, seat)` as extra fields for `checks.lua`; the HOW TO PLAY page -- R6-style page per R7, R8, R9.
- [ ] `test/game_script/first_party/battleship/taps.lua` -- module `taps`: canvas centres of a board cell, the four placement buttons, the question button, a point off every target, through the game's `layout` module and `ch.screen`; helpers that turn a list of placements or shots into steps -- so rounds hold no pixel.
- [ ] `.../rounds/won-by-seat-1.lua`, `won-by-seat-2.lua` -- hand-written fleets (literal ship cells, both across and down, touching ships), seat 1 or 2 hits all 17 cells while the other seat fires only misses; `shows` for Hit, Miss, and the sunk text on the way and the winner's header on the last step; `winners = {1}` / `{2}`.
- [ ] `.../rounds/random-placement.lua` -- seat 1 places two ships, taps Random and Ready; seat 2 taps Random and Ready; seat 1 fires one shot; `unfinished = true`.
- [ ] `.../rounds/clear.lua` -- places ships, taps Clear (`shows` the first ship's prompt again), places five, Ready, hand-off, the other seat's placement; `unfinished = true`.
- [ ] `.../rounds/rejected-moves.lua` -- an overlapping ship, Ready before five ships, a tap after five are placed, Clear with nothing placed, then in firing a repeated shot, each `move = false` with the reason in `shows`.
- [ ] `.../rounds/how-to-play.lua` -- the question button in placement and in firing opens the page (`shows` "HOW TO PLAY", `move = false`), a tap closes it, play goes on.
- [ ] `.../checks.lua` -- the checks listed in Design Notes (geometry, tap targets, fleet rules against an independent validator, `apply`'s phases and reasons, secrecy of `views`, win and status, text fit, reason lengths).
- [ ] `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-battleship-screenshots/` -- the simulator shots of Verification -- the build agent, after the review.

**Acceptance Criteria:**
- Given the finished tree, when `ctest -L games-check` runs, then `GamesCheckTest` passes the package, the game's checks, and all six rounds (a win by each seat, Random placement, Clear, rejected moves, HOW TO PLAY), and no snapshot passes 700 B.
- Given scratch copies of the game with one rule removed each (the overlap rejection, the Ready gate, the repeated-shot rejection, the win test, `mask` revealing intact ships), when the real target runs over each, then the checks or rounds fail (they prove the rules).
- Given the packed game in `fs_/games/`, when Games is opened in the simulator, then it installs and plays: placement shows the rotate, Random, Clear, Ready, and How to play controls; Ready then shows Result's banner and the hand-off; firing shows the target and the fleet boards; a hit shows its text in Result under the banner; the question button opens the HOW TO PLAY page; and the end-of-round menu sits over seat 0's frame with both fleets visible.
- Given `test/game_script/harness/` and `scripts/`, when searched for the three first-party games' names, then no harness code or script names one (fixture names excepted), and `check_upstream_touches.py` passes with no ledger edit.

## Implementation Notes

Implemented (lane A, not committed): `games/battleship/{manifest.json, fleet.lua, layout.lua, main.lua}` and `test/game_script/first_party/battleship/{taps.lua, checks.lua, rounds/{won-by-seat-1, won-by-seat-2, random-placement, clear, rejected-moves, how-to-play}.lua}`. Host verification: full `ctest -j` 1753 of 1753 pass; `ctest -L games-check` 75 of 75, 20 repeats clean; `checks.lua` runs 31 checks.

Deviations and additions (none changes an outcome the plan settles):
- `game.views(state, seat)` gives seat 1 and 2 one extra field, `left`: the number of the other fleet's ships not yet sunk. The plan counted the target's ships from its `s` cells, which is ambiguous (five `s` cells are a Carrier or a Cruiser and a Destroyer); the count is public (every sinking is announced) and `views` stays the one place `draw` reads the other seat's data from.
- Extra fields of the game table for `checks.lua`: `prompt(own, vertical)` (the placement status line) and `last_shot_line(state, seat)`, beside `views`, `help_lines`, `headline`.
- Wording the plan left open: the placement prompt reads "Place the Carrier (5), across" (or "down"); the placed-and-waiting frame draws the big own fleet and the tray with "Your fleet is ready" and no buttons; seat 0 drawn before Over shows only "Battleship" (never a fleet); shot counts read "Player N fired K shots".
- A wrapped line is one text command, so `shows` in the rounds uses the part that stays on one line under the harness metrics: "Carrier!", "Cruiser!", "Battleship!" for a sunk text, "You already fired" for the repeated-shot reason.
- The state is about 345 B at setup (measured by padding it until a round failed: 480 B of padding gave 831 B); a game adds under 20 B for `l`. Limit 700 B.
- `fleet.random_fill` reads the board once as rows and once as columns (a transposed string) so each candidate anchor is one `string.sub` compare; the 68 seeds of the Random checks run in five checks well inside their budgets.

Mutants (scratch roots in `build/test/games_check_scratch/`, since deleted; each run with `-DGAMES_CHECK_GAMES_ROOT=... -DGAMES_CHECK_COMPANION_ROOT=...` after a rebuild, then reset with `-U...`): (a) overlap rejection removed: red (2 checks, a round); (b) Ready gate removed: red (a check, the rejected-moves round); (c) repeated-shot rejection removed: red (2 checks, a round); (d) win test removed: red (checks and the won-by rounds); (e) `mask` revealing intact ships: red (3 checks; the rounds do not see it, the checks do); (f) state padded past 700 B: red (every round). The real target is green again.

Still to run after the review: `pio run -e x4pro` and `-e default`/other envs as the build agent decides (no firmware source changed), `sim.sh build x4pro`, packing with `scripts/pack_game.py` into `fs_/games/`, and the six screenshots. Not verified on a display: real font widths (the help page and the right-hand column were fitted under the harness's 8/10/14 px stand-in metrics), the look of the grid and ship bars, and the end-of-round dialog over seat 0's frame.

- Container restart (orchestrator, 2026-10-05): the build agent handed back while its implementation subagent ran; the container then restarted. The implementation subagent had finished before the restart (its last act wrote the notes above and handed back: every host test and fast check passing, nothing committed, firmware checks, `sim.sh`, and screenshots still to do). Recovered as it stands: the tree's uncommitted `games/battleship/`, `test/game_script/first_party/battleship/`, and this plan at `in-progress`; nothing was redone. The orchestrator re-dispatches `/bmad-build-auto ticket 8.2`, which resumes from `in-progress`.

- Review patches and the simulator fix (build agent, 2026-10-05): `headline` and `drawWaiting` for a seat that has not placed (`seat < state.p`); no question tap target for seat 0; `game.count_line`; checks `shotLines` and `waitingAndSeat0`; a comment fix in `randomNeedsRoom`. The simulator showed seat 0's boards ending at y 275 of the panel, 7 px under the dialog's top edge (268 on the panel, canvas starting 9 px down), so the Over cell is 18 px (boards end at 246 canvas, 255 panel); `layout.lua` and the geometry check changed to match.

## Plan Change Log

## Review Triage Log

### 2026-10-05 — Review pass
- verdicts: 25 findings — high 0, medium 3, low 17, false 5, maybe-false 0
- findings:
  - Blind Hunter
  - `[false]` `[reject]` `apply` ignores `seat` — the runtime passes only the turn seat and the plan settled it (as tic-tac-toe does); a wrong-seat move cannot reach the game.
  - `[low]` `[reject]` the firing column is not bounded by `column.h` — a message and a 3-line shot text coexist only on a normal firing frame, which has no Result banner (a Result frame has no message); everything ends by y 692 of 788.
  - `[low]` `[defer]` secrecy checked on `views`, not on `draw`'s output; `last_shot_line` reads `state.l` — no leak today (a hit line never names a ship; only a sinking does, and it is public); the unverified draw is the deferred item (grouped with the verification-gap finding).
  - `[low]` `[patch]` seat 0 and Over input paths dead; seat 0 has an invisible question tap target — fixed with a `seat ~= 0` guard in `input` (check `waitingAndSeat0`); the dead `over` mode branch is harmless and kept.
  - `[medium]` `[patch]` no check of seat 0's Over screen (shot counts) — fixed by `game.count_line(state, i)`, used by `drawBoth` and pinned in check `shotLines` (a `3 - i` mutant is red).
  - `[low]` `[reject]` `fleet.shot` returns nothing for a foreign character — only a corrupted snapshot reaches it; `apply` validates cells and the alphabet is closed.
  - `[low]` `[reject]` `random_fill` greedy, no retry — after at most four manual ships on a 10 x 10 board a later ship always has room (68 seeds in the checks); the reachable case is not shown.
  - `[low]` `[patch]` `randomNeedsRoom` comment does not match its fixture — comment corrected (fixture is a no-room board, not a reachable fleet).
  - `[low]` `[reject]` `OFF_BOARD` unreachable by a tap and the edge shift undocumented — the shift is exercised by `inputPlacing`; a help sentence would cost page lines for no harm.
  - `[low]` `[reject]` help says a sunk ship is named — true at the shot; the page is not wrong.
  - `[low]` `[reject]` magic numbers, no small-canvas guard — both devices give 474 x 788 (`api-level-1.txt` line 384) and the checks run two canvas sizes.
  - `[false]` `[reject]` no undo in placement — R7 specifies Clear, not undo; a scope cut the epic settles.
  - Edge Case Hunter
  - `[low]` `[patch]` seat 2 reads "fleet ready" while seat 1 places — `headline` and `drawWaiting` now key on `seat < state.p` ("waiting" otherwise); check `waitingAndSeat0` (mutated: red).
  - `[false]` `[reject]` `apply` ignores `seat` — same as above.
  - `[low]` `[patch]` seat 0 question tap with no icon — same guard as above.
  - `[false]` `[reject]` `fleet.shot` out of range raises — `apply` checks `isCell` first; only a direct call from a check reaches it.
  - `[low]` `[reject]` canvas too small for `cell` — same as the Blind Hunter row.
  - `[false]` `[reject]` a rejection while help is open leaves a stale message — a rejection follows a move tap synchronously and help opens only on the non-move question tap, so the two never overlap.
  - Verification Gap
  - `[medium]` `[patch]` the opponent's last-shot lines, ships-left counts and seat 0's shot counts are never asserted — check `shotLines` pins `last_shot_line` for both seats and every result and `count_line`; rounds cannot pin a non-mover's frame (`shows` matches only the step seat's), so a check does.
  - `[medium]` `[defer]` draw-level secrecy is not observed — deferred (see `deferred`; needs harness support the ticket may not touch).
  - Intent Alignment (divergences it reported)
  - `[low]` `[defer]` "each draw shows only its seat's fleet" lives in draw output, tests pin `views` — grouped with the deferred draw-level item.
  - `[low]` `[defer]` icons on the target and shapes on the own fleet are not asserted as command kinds — same deferred item; the simulator screenshots show both.
  - `[low]` `[reject]` the mover's Result frame is pinned by `shows`, which a check's own draws could also meet — the hidden flow is `GamesCheckFlowTest`/`HiddenFlowPinTest`'s; the simulator's Result screenshot shows the banner over the mover's frame.
  - `[low]` `[patch]` seat 0's "Player N wins!" is not seat 0's frame — covered with the `count_line` check (seat 0's own lines).
  - `[low]` `[reject]` fit proved under stand-in metrics only — the simulator screenshots measured the real fonts: every line fits (and showed the Over boards 7 px into the dialog; fixed, see Implementation Notes).

## Design Notes

**Interfaces fixed by the owner (epic Notes, 2026-10-04):** C1 the rounds file, C2 `checks.lua` (`first_party/README.md` states both as built); the hidden flow of the games check (Decision D1 to D3, `GamesCheckFlowTest`): a hidden pass round gives each step to the shown seat, plays the mover's frame then the hand-off after a turn-passing move, and draws every local seat after each step and seat 0 once over. R7: 10 × 10, fleet 5, 4, 3, 3, 2, placement one seat at a time with ships touching but never overlapping, each ship a move that keeps the turn, rotation in `ui`, Random places the remaining ships, Clear lifts every placed ship and keeps the turn, Ready passes the turn, a repeated shot rejected with a reason, hit, miss, or sunk shown in Result, first to sink the fleet wins; target board about 44 px cells with library icons, own fleet about 28 px cells in `ch.gfx` shapes below it, seat 0 at Over both fleets; `question` icon button, HOW TO PLAY page with the rules, placement, and firing, no board. Notes Decision of 2026-10-04 (readiness check): five placement controls. R9: every snapshot at most 700 B. R2: a self-contained package.

**State** (about 250 B; measure it): `state = { f = { <100 chars>, <100 chars> }, p = 1, t = 1, l = nil }`.
- `f[s]` is seat s's own fleet, row by row (cell `(row - 1) * 10 + col`): `.` water, `o` a miss the other seat fired here, `a` to `e` an intact ship cell (`a` Carrier 5, `b` Battleship 4, `c` Cruiser 3, `d` Submarine 3, `e` Destroyer 2), `A` to `E` a hit cell of that ship. A ship is sunk when none of its letters is lowercase; the fleet is beaten when `f:find("[a-e]")` is nil.
- `p` is the phase: 1 or 2 is that seat placing, 3 is firing. `t` is the seat that fires (1 first, set when seat 2's Ready makes `p` 3; Design pick below). `l` is the last shot `{ seat, row, col, result, ship }` with `result` 0 miss, 1 hit, 2 sunk and `ship` the ship's number 1 to 5 (0 for a miss), absent until a shot.
- `status(state)`: `p < 3` is `{ turn = p }`; `p == 3`: `f[2]` beaten is `{ over = true, winners = { 1 } }`, `f[1]` beaten `{ over = true, winners = { 2 } }`, else `{ turn = t }`. Depends on `state` only.

**Moves** (each at most 256 B): `{ "P", row, col, "H" | "V" }` place the next ship; `{ "R" }` Random; `{ "C" }` Clear; `{ "Y" }` Ready; `{ "F", row, col }` fire. `apply(state, seat, move)` ignores `seat` (the runtime passes only the turn seat; placement uses `p`, firing `t`; the tracer's plan settled the same for tic-tac-toe), validates every field (`math.type(v) == "integer"`, 1 to 10; direction `"H"` or `"V"`; `move` a table), and returns the reasons below, each at most 64 B, leaving `state` unchanged on a rejection (validate before mutating): "The game is over" (any move once over); "Place your ships first" (a shot while `p < 3`); "The ships are placed. Fire at the other board" (a placement move while `p == 3`); "That ship would go off the board"; "Ships cannot overlap"; "All five ships are placed" (a placement or Random with none left); "No room for the other ships; tap Clear" (Random finds no legal spot); "No ships to clear"; "Place all five ships first" (Ready); "You already fired there"; "Tap a square on the board" (a malformed move or cell).
- Place: the next ship is the first of the five whose letter is not in `f` (so the order is 5, 4, 3, 3, 2); `fleet.place(f, row, col, vertical)` returns the new string or `nil, reason`. `input` builds the move from the tap shifted so the ship fits (`col = min(col, 11 - len)` across, `row = min(row, 11 - len)` down); `apply` still rejects an off-board anchor.
- Random: for each remaining ship in order, list every legal anchor in both directions and pick `math.random(#list)` (at most 200 candidates per ship, so about 5,000 steps in all); earlier ships stay. Terminates by construction.
- Ready: needs no ship left to place; `p = p + 1`; when that makes `p` 3, `t = 1`.
- Fire: `target = 3 - t`; `fleet.shot(f[target], row, col)` returns the new string, the result, and the ship number; `l = { t, row, col, result, ship }`; the turn passes (`t = target`) unless the shot ends the round (`t` stays the winner).

**`game.views(state, seat)`** (pure, used by `draw` and by `checks.lua`): for seat 1 or 2, `{ own = f[seat], target = <nil while p < 3, else fleet.mask(f[3 - seat])> }`; for seat 0, `{ fleets = { f[1], f[2] } }`. `fleet.mask(f)` maps `.` and intact `a`-`e` to `.`, `o` to `o`, a hit cell to `x`, and a hit cell of a sunk ship to `s`: it hides every intact ship, and a hit never names its ship. `draw` takes everything it draws about the other seat from `views`, so secrecy is one function the checks pin.

**Layout** (`layout.compute(w, h)`, pure; numbers at 474 × 788, formulas in terms of `w`, `h`; the unknown's "480 × 800" is the spec's round figure, the canvas is 474 × 788): header band 52 px (title text left; `question` icon, 32 px, at `w - 48, 10`; its tap rectangle `w - 72, 0, 72, 52`). Big board (placement's own fleet, firing's target): `cell = min(44, (w - 8) // 10, (h - 52 - 8 - 280) // 10)` = 44, so 440 px, `x = (w - 10 * cell) // 2` = 17, `y = 52`. Small board (firing's own fleet): `cell = 28`, `x` the big board's, `y = 52 + 440 + 8` = 500, ending at 780. Right column of the firing view: `x = 17 + 280 + 12` = 309 to `w - 8` (157 px), `y` 500 to 640 (above Result's banner, which starts at about 649): the last shot's line and the ships-left counts. Placement view below the big board (`y` 500 on): status line (medium) at 500; the ship tray at 536 (five horizontal bars, 16 px a square: placed filled, the next outlined thick, the rest outlined thin); four buttons at `y` 576, 84 px high, `(440 - 3 * 8) // 4` = 104 px wide, x from 17 with 8 px gaps: Rotate (`arrow-clockwise`), Random (`shuffle`), Clear (`trash`), Ready (`check`), each the icon at 32 px 12 px below the top and a small centred label 50 px below it; Ready is drawn inverted (black fill, white icon and label) while five ships are placed; `ui.message` at 672 in small text. Seat 0's Over view: two boards side by side, `cell = min(20, (w - 24) // 20)`, 14 px apart, centred, `y = 66`, a label ("Player 1", "Player 2") above each, ending at about 266, above the end-of-round dialog (about y 268 to 540), with each seat's shot count below it at about y 570 ("Player 1 fired N shots", N counted from the other fleet's `o` and uppercase cells). Every rectangle inside the canvas; every tap target at least 44 px each way (cells 44, buttons 104 × 84, question 72 × 52).

**Drawing.** All text is the game's own and in `"small"`, `"medium"`, or `"large"`. Header (`headline(state, seat)`, medium): placing "Player N: place ships"; placed and waiting (`p` not the seat's and below 3) "Player N: fleet ready"; firing "Player N: fire!" when `t == seat`, else "Player N: waiting"; once over "Player W wins!" for every seat (the seat's own boards stay as they are). A fleet cell: a ship is a black filled rectangle inset `max(1, cell // 12)` from its cell and stretched across the inset to a neighbour of the same ship (so a ship reads as one bar, two touching ships stay apart); a hit gets two white lines as a cross; a miss a small ring (`ch.gfx.circle`, radius `cell // 8`, unfilled). The target board: 1 px lines on every cell edge (22), `waves` (miss), `fire` (hit), `fire` in `"fill"` weight (sunk), each `ch.gfx.icon(name, x + 6, y + 6, "small", "black")` in a 44 px cell; the last shot, when it is the seat's own, outlined by a 3 px frame. On the small fleet board the opponent's last shot is outlined. Firing right column (wrapped with `ch.text_width` to 157 px): the last shot's line in medium, for the mover "Hit!", "Miss", "You sank the Cruiser!", for the other seat "Player 1 hit your ship", "Player 1 missed", "Player 1 sank your Cruiser!"; then a `boat` icon (32 px) with "Their ships: N" and "Your ships: M" in small text (N, M the ships not yet sunk, counted from `views`' target `s` cells and `own`'s lowercase letters); `ui.message` in small text below. In Result the mover's frame is drawn after the turn passes, so `l` is what the mover sees. The HOW TO PLAY page replaces the boards, as tic-tac-toe's does: title "HOW TO PLAY" and wrapped small text; the paragraphs cover the goal and the fleet (five ships of 5, 4, 3, 3, 2 squares on a 10 by 10 board), placement (tap a square for the next ship, longest first; Rotate; Random places the ships not placed yet; Clear lifts them all; ships may touch but not overlap; Ready hands the device on), firing (tap a square on the big board; a wave is a miss, a flame a hit; sunk ships are named; no square twice; the first to sink all five wins), the small board (the other player's misses show as rings and hits as crossed ships), the hand-off (look away while the device is passed), and the question button (a tap closes the page). It shows no board, so it reveals nothing.

**`input`.** `rejected` sets `ui.message = ev.reason`. A `tap` while `ui.help` closes it (no move); else `ui.message = nil`; a tap in the question rectangle sets `ui.help`; else by mode (the same four modes `draw` has: seat 0 or a round that is over: nothing; placing: the Rotate rectangle flips `ui.vertical` and returns nil, Random, Clear, and Ready return their moves, a board cell returns `{ "P", ... }` with the direction from `ui.vertical`; firing: a target cell returns `{ "F", row, col }`, a tap outside it nil; placed and waiting: nothing). Other events return nil.

**`checks.lua`** (each its own `{name, run}`): geometry (rectangles inside the canvas and apart; cell and button sizes; `cell_at` inverts `cell_rect` for the 100 cells of each of the four boards and is nil one pixel outside); fleet rules against a validator the check writes itself (ship lengths 5, 4, 3, 3, 2; straight and contiguous; no overlap; touching accepted; order of placement; off-board and overlap rejected); Random over 60 `math.randomseed` values, from empty and from one to four manual ships; `apply` per phase (every move of the matrix, reasons within 64 B, state unchanged after a rejection, `p` and `t` transitions, `status`); firing (miss, hit, sunk for each ship, repeat, win by each seat, a move after the round is over); `views` (for two states that differ only in where the other seat's intact ships lie, seat 1's `target` is identical; `target` holds only `.`, `o`, `x`, `s`; seat 0 sees both fleets); help and header text fit the canvas under the harness's stand-in metrics (an estimate; the simulator shows the real fonts).

**Rounds.** All `mode = "pass"`, taps from `taps.lua`; a hidden round's step seat is the shown seat (placing seat 1, then 2 after seat 1's Ready, then seat 1 fires and the seats alternate). `won-by-seat-1`: fleets written as literal ship lists, seat 1 hits all 17 cells of seat 2's fleet, seat 2 fires 16 distinct water cells, so seat 1 wins on its 17th shot with `winners = { 1 }`; `won-by-seat-2` mirrors it with seat 1 firing 17 misses first (seat 2 wins on its 17th, `winners = { 2 }`). A step after the round is over fails, so the exact lists prove the 17th hit ends it.

**Builder's picks the Notes leave open** (none changes an outcome they settle): seat 1 fires first; ships are placed in the fixed order 5, 4, 3, 3, 2 and the tapped cell is the ship's first square, shifted back at the board's edge; a ship that would overlap is rejected rather than shifted; Clear and Random with nothing to do are rejected rather than accepted as no-ops; the miss mark is `waves` (`dot-outline` stays for the small boards' rings, which are shapes), the hit `fire`, the sunk ship `fire` in `"fill"` weight, `boat` marks the ships-left counts, `trash` is Clear; header and message wording; seat 1 and seat 2 draw their own boards once over with the winner in the header (the device shows seat 0 then), so seat 0's view alone shows both fleets (R7); `help_lines`, `views`, and `headline` are extra fields of the game table, which the runtime ignores and `checks.lua` runs; no `icon`: the entry names none (the launcher and hand-off screen show their default mark, as for tic-tac-toe). The layout fits both boards with the controls (`unknown`), so no toggle is built and nothing goes back to the owner.

**Settled by existing text:** manifest and modes (R1, epic-play-nearby adds `nearby`); 700 B (R9); text is the game's own (R2); five controls (Notes, readiness check); no `board.lua` (R2: entry 3's copy is of tic-tac-toe's module; Battleship shares no file); no `git log -L` reading applies: no existing function is moved or rewritten; no test double is added or extended (the hidden flow is entry 8's, pinned by `GamesCheckFlowTest`). No firmware source changes: `pio run`, `pio check`, and the sibling-env builds cover files this entry does not touch, so they are not run; `sim.sh build x4pro` is, for the screenshots.

## Verification

**Commands** (host tests and fast checks first; locks as AGENTS.md says; `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`; work only in `/home/user/epic-first-party-games-lane-a`):
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'`, then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, including `-L games-check` (Battleship's three tests and `HasAGame`).
- `ctest --test-dir build/test -L games-check --repeat until-fail:20` -- expected: pass.
- One-off mutants, scratch roots in `build/test/games_check_scratch/` (never committed): reconfigure with `-DGAMES_CHECK_GAMES_ROOT=... -DGAMES_CHECK_COMPANION_ROOT=...` over copies of the game with (a) the overlap rejection removed, (b) the Ready gate removed, (c) the repeated-shot rejection removed, (d) the win test removed, (e) `mask` revealing intact ships, (f) the state padded past 700 B; run `ctest -L games-check` -- expected: each red; then reset with `-UGAMES_CHECK_GAMES_ROOT -UGAMES_CHECK_COMPANION_ROOT` and the real target green. Record each result.
- `grep -rniE 'tic-tac|ultimate|sudoku|battleship' test/game_script/harness scripts` -- expected: no match in harness code or `scripts/` (fixture names excepted).
- `for t in scripts/*_test.py; do python3 $t; done`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, the second run changes nothing.
- After review and patches, once: `sim.sh build x4pro` under the build lock, pack the game with `scripts/pack_game.py`, copy it to `fs_/games/`, and take the screenshots below.

**Manual checks:** simulator screenshots in `story-battleship-screenshots/`, each looked at: `placement.png` (placement with a ship or two placed and the Rotate, Random, Clear, Ready, and question controls), `hand-off.png` (the hand-off between the placements), `firing.png` (the firing view, target board large and own fleet small), `result-hit.png` (Result with its banner after a hit, the hit's text readable above the banner), `how-to-play.png` (the HOW TO PLAY page), `over-menu.png` (seat 0's frame with both fleets under the end-of-round menu).

**Results** (build agent, worktree `/home/user/epic-first-party-games-lane-a`; the host tests and fast checks ran on the patched tree, then the simulator):
- Host: full `ctest -j` 1753 of 1753 pass (`build/test`, Ninja, rebuilt after the last edit); `ctest -L games-check --repeat until-fail:20` 75 of 75 each time (a repeat check on the new checks, not a flake fix). `checks.lua` runs 33 checks. Two new-check mutants were red and then reset: `count_shots(state.f[i])` in `count_line`, and `fleet ready` for every seat; the implementer's mutants (a) to (f) of Implementation Notes were red as recorded there.
- Fast checks: every `scripts/*_test.py` passes; `check_upstream_touches.py` PASS with no ledger edit; the harness and `scripts/` grep for the games' names has no match; `./bin/clang-format-fix` twice, the second run changed nothing and nothing outside the battleship paths and this plan.
- Firmware: `sim.sh build x4pro` under the build lock, SUCCESS (about 18 s with the shared cache). `pio run`, `pio check` and the other envs were not run: no firmware source, `lib/`, or build file changed (see Design Notes, Settled by existing text). Not measured: flash, RAM, device timing, real e-ink look. The state is about 345 B at setup (measured by the implementer by padding, method in Implementation Notes).
- Simulator (x4pro, the packed `battleship.chgame` from `scripts/pack_game.py` into `fs_/games/`, played from Games through two placements and a 17-shot win by seat 1; each screenshot looked at):
  - `story-battleship-screenshots/placement.png`: placement with three ships down (one placed after Rotate, "down"), the tray, and the Rotate, Random, Clear, Ready, and question controls.
  - `story-battleship-screenshots/hand-off.png`: the hand-off screen between the placements ("Player 2's turn", I'm ready).
  - `story-battleship-screenshots/firing.png`: the firing view, the empty target large and the own fleet small with the right-hand column.
  - `story-battleship-screenshots/result-hit.png`: Result after a hit, the flame outlined on the target, "Hit!" above the "Tap to pass to player 2" banner.
  - `story-battleship-screenshots/how-to-play.png`: the HOW TO PLAY page.
  - `story-battleship-screenshots/over-menu.png`: seat 0's frame under the Game over menu with both fleets and the shot counts.
- Not seen in a screenshot: the placed-ready frame of seat 2 and a sunk ship's text (both pinned by checks and rounds only).

## Auto Run Result

**Summary.** `games/battleship/` is a hidden two-seat pass game: placement one seat at a time (ships touching, never overlapping; each ship a move that keeps the turn; Rotate in `ui`; Random, Clear, Ready as moves), then one shot per turn with hit, miss, or sunk shown to the mover in Result; the target board large with library icons, the own fleet small with `ch.gfx` shapes; seat 0 at Over shows both fleets; a HOW TO PLAY page behind the `question` button. Its companion folder holds `checks.lua` (33 checks) and six rounds (a win for each seat, Random placement, Clear, rejected moves, HOW TO PLAY), all played by entry 8's games check.

**Files.** All new:
- `games/battleship/manifest.json`, `fleet.lua`, `layout.lua`, `main.lua`: the package, its pure rules, its pure geometry, and the entry points.
- `test/game_script/first_party/battleship/checks.lua`, `taps.lua`, `rounds/{won-by-seat-1,won-by-seat-2,random-placement,clear,rejected-moves,how-to-play}.lua`: the companion.
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-battleship-screenshots/*.png` (6) and this plan.

**Review.** 25 findings from the four lenses (`lenses_ran`: blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Patches applied: 7 rows (2 medium: the missing assertions on the other seat's lines and seat 0's counts; 5 low: seat 2's waiting headline, seat 0's question target twice, a test comment, seat 0's wording check). Deferred: 1 item (draw-level secrecy is unobserved; needs harness support this ticket may not touch), from 4 grouped rows. Rejected with reasons, as logged in the Review Triage Log: 14 (5 `false` and 9 `low` unreachable or cosmetic). `followup_review_recommended: false`: one `medium` entry class patched (two rows, one root: unasserted texts) and no `high`.

**Verification.** See the Verification Results above: host 1753 of 1753, games-check 75 of 75 over 20 repeats, fast checks green, `sim.sh build x4pro` SUCCESS, six screenshots looked at.

**Residual risks.** Text fit and the grid look were checked in the simulator on the X4 Pro only (the Sticky has the same canvas size per `api-level-1.txt`); no device run. The Result frame reads "Player 1: waiting" for the mover (the turn has passed); left as the header rule says. The deferred draw-level secrecy gap. Formatting: `./bin/clang-format-fix` changed nothing outside this story's paths.

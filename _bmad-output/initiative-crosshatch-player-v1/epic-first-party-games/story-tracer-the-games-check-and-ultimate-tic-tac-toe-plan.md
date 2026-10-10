---
title: 'Tracer: the games check and Ultimate tic-tac-toe'
type: 'feature'
ticket: '1'
created: '2026-10-04'
status: done
baseline_revision: 'd3795e885a93a86f324d19fdccb873289f54dbe0'
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
  - '{project-root}/test/game_script/fixtures/README.md'
  - '{project-root}/docs/crosshatch/api-level-1.txt'
warnings: [oversized]
deferred:
  - summary: >-
      A rejected move after the round is over, or a malformed move, gets "Play in the highlighted board", though no board is highlighted then.
    evidence: |-
      `game.apply` uses one reason for every unplayable move (blind-hunter and edge-case-hunter). The device delivers no input once the round is over and `input` only builds well-formed moves, so a player never meets it; `checks.lua` pins the current wording, so a "Round is over" reason is a rule change for the owner's wording.
    location: >-
      games/ultimate-tic-tac-toe/main.lua (apply)
    severity: low
  - summary: >-
      The HOW TO PLAY page is checked for fit only at the harness canvas (474x788) under stand-in text metrics, and has no scroll or paging for a canvas as narrow as 320 px.
    evidence: |-
      `helpPageFits` in checks.lua reads `ch.screen`, which a check cannot change, so the 320x480 and 480x800 canvases are not covered; seven wrapped paragraphs at a 28 px step may pass the bottom on a narrow canvas. No v1 device has such a canvas (X4 Pro and Sticky: 474 x 788), and only the simulator or a device shows real font fit.
    location: >-
      games/ultimate-tic-tac-toe/main.lua (help_lines), test/game_script/first_party/ultimate-tic-tac-toe/checks.lua (helpPageFits)
    severity: low
  - summary: >-
      The games check sees only text commands, so no round or check can assert a highlight fill, an icon, or a won board's big mark.
    evidence: |-
      `RoundPlayer.cpp` keeps each frame's text commands only (verification-gap lens): removing the `light` fill loop or the won-board mark in `main.lua` leaves every round and check green. Only the simulator screenshots show them. Fixing it changes the games check (entry 8's files), which this entry's intent excludes.
    location: >-
      test/game_script/harness/games_check/RoundPlayer.cpp:126-138
    severity: low
---

<intent-contract>

## Intent

**Problem:** There is no first-party game yet: the games check (entry 8, merged) has nothing to play, and entries 2 and 3 (Battleship, Sudoku) need a finished game, a proven 9x9 board module, and a companion folder to copy the pattern from.

**Approach:** Add `games/ultimate-tic-tac-toe/` (R1, R6, R8) with the board module `board.lua` (R3, interface fixed by epic Notes Decision C3, which entry 3 copies once) and a HOW TO PLAY page behind a `question` icon button, and its companion folder `test/game_script/first_party/ultimate-tic-tac-toe/` (rounds in the Decision C1 format, a `checks.lua` in the Decision C2 interface, a dev tool under `tools/`), then show it running in the simulator.

## Boundaries & Constraints

**Always:** Every new file is under `games/ultimate-tic-tac-toe/`, `test/game_script/first_party/ultimate-tic-tac-toe/`, or is this plan or its screenshots; no upstream file changes, so `check_upstream_touches.py` stays green with no ledger edit. `games/ultimate-tic-tac-toe/` is self-contained (R2): its own text (not `tr()`), `manifest.json`, `main.lua`, `board.lua`, no `icon.png`, no path outside itself named in it. Every drawing command, icon, and `ctx` field stays within `api-level-1.txt`; every snapshot at or under 700 B (R9); Lua is 5.5 (`global` is reserved); no call nears the 2,000,000-instruction budget or `frame_commands_count`. Rounds and checks name no path outside their companion folder.

**Never:** Change `src/`, `lib/`, `docs/crosshatch/api-level-1.txt`, `API_SURFACE_CRC`, `platformio.ini`, the freeink-sdk pointer, `.skills/`, upstream's `ci.yml`, `crosshatch-ci.yml`, `test/CMakeLists.txt`, or any file of the games check (`test/game_script/harness/`, `first_party/README.md`); add a `nearby` mode, a zoomed view, or an API entry; name a game in `test/game_script/harness/` or `scripts/`; commit scratch trees or generated files; put a fixture or engine test in `games/`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Package | `games/ultimate-tic-tac-toe/` | `GamesCheckTest` packs, installs, and loads it; the manifest is `pass`, seats 2/2, not hidden | none |
| Forced board | last move in cell c, board c open | the next move must be in board c; board c alone is highlighted | a move elsewhere is rejected "Play in the highlighted board" |
| Finished target | board c won or full | any open board may be played; every open board is highlighted | a tap in a won or full board is rejected the same way |
| Taken cell | tap on an occupied cell of a playable board | no move | rejected "That cell is taken" |
| Small board result | three in a line in a small board / its 9 cells full | board closed, drawn as a big mark / a `light` fill | none |
| Big result | three won boards in a line / no live line left (every line holds both marks or a full board) | `winners` = that seat / `{}` | none |
| HOW TO PLAY | tap the `question` button; then any tap | the rules page replaces the board, no move; the next tap closes it, no move | none |
| Tap off the grid | tap outside the 9x9 grid | no move | none |
| Board geometry | `layout` at 474x788, 480x800, 320x480 | `L = {x, y, cell, size, block}` as Decision C3; `cell_at` inverts `cell_rect` for all 81 cells and is `nil` outside | a failing check names itself |
| Snapshot and frames | every step of every round, every seat drawn | snapshot at most 700 B; no fault, frame within limits | the check fails the round |

</intent-contract>

## Code Map

Read, never edit (all under `/home/user/epic-first-party-games-lane-a/`):
- `test/game_script/first_party/README.md` -- the rounds format (C1: `{mode, settings?, seed?, steps, winners | unfinished}`, step `{seat, x, y, wait?, move?, shows?}`, `steps` may be a function of the decoded state), the `checks.lua` interface (C2), what the check proves; an open pass round's step seat is the turn seat; a step after the round ends fails.
- `test/game_script/harness/games_check/{GameCheck,RoundPlayer,RoundFile,ScriptVm}.cpp` -- how rounds and checks run (read to resolve a doubt, do not edit); `ScriptVm` stands in for the device sandbox (`os`, `load`, `package` nil; `require` finds the game's modules and the companion's top-level `.lua` files; `ch.gfx` only inside `draw`; text metrics are the harness's stand-ins).
- `test/game_script/fixtures/pass-open/main.lua` -- the shape of a two-seat open pass game (`status`, `apply`, `input` with `rejected` and `over` events, seat 0's frame); `docs/crosshatch/api-level-1.txt` (icons `x`, `circle`, `question`; limits); `docs/crosshatch/game-icons.md`; `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md` sections 1 to 4, 7; `scripts/pack_game.py` (what a package may hold).
- `.claude/skills/run-crosshatch-player/SKILL.md`, `docs/crosshatch/game-canvas.md` -- the simulator driver and the end-of-round menu (screenshots, after the review).

New:
- `games/ultimate-tic-tac-toe/{manifest.json, main.lua, board.lua}`.
- `test/game_script/first_party/ultimate-tic-tac-toe/{checks.lua, taps.lua, rounds/{won-by-seat-1,won-by-seat-2,drawn,forced-board,rejected-move}.lua, tools/make_rounds.py}`.
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-tracer-screenshots/*.png` (five shots; the build agent takes them after the review, not the implementer).

## Tasks & Acceptance

**Execution:**
- [ ] `games/ultimate-tic-tac-toe/board.lua` -- the C3 module exactly: `layout(w, h, opts?)`, `cell_rect(L, row, col)`, `block_rect(L, brow, bcol)`, `cell_at(L, x, y)`, `draw_grid(L)`; no game rule in it, nothing named for tic-tac-toe, so entry 3 copies it unchanged -- R3, Decision C3.
- [ ] `games/ultimate-tic-tac-toe/manifest.json` -- id `ultimate-tic-tac-toe`, name "Ultimate Tic-Tac-Toe", version "1.0.0", api 1, seats 2/2, modes `["pass"]`, hidden false, no icon -- R1.
- [ ] `games/ultimate-tic-tac-toe/main.lua` -- the rules, `input`, and `draw` as Design Notes' UTTT section says -- R6, R8, R9.
- [ ] `test/game_script/first_party/ultimate-tic-tac-toe/taps.lua` -- module `taps`: `taps.tap(b, c)` returns the canvas centre of cell c of small board b through `board` and `ch.screen`; `taps.steps(moves)` turns a move list `{{b, c}, ...}` into steps alternating seats 1, 2; the `question` button's centre; so rounds read as move lists.
- [ ] `.../rounds/{won-by-seat-1,won-by-seat-2,drawn,forced-board,rejected-move}.lua` -- the five rounds (Design Notes) -- Verify's round kinds.
- [ ] `.../checks.lua` -- list of `{name, run}`: board geometry at the three sizes (values of Decision C3), `cell_at` and `cell_rect` agree for all 81 cells at each size and `cell_at` is `nil` just outside, `block_rect` covers its nine cells, every win line wins (small boards and big, both marks), the draw rule (a state with no live line is a draw, one live line is not), apply rejections (taken cell, wrong board, closed board, out-of-range or non-integer `b`/`c`), the forced-board hand-over (to c when open, to any when closed).
- [ ] `.../tools/make_rounds.py` -- stdlib only, an independent Python implementation of the rules (it shares no code or constants with `main.lua`); searches seeded random playouts for a win by each seat and a draw, and builds the forced-board and rejected-move move lists; prints the move lists in the Lua syntax the rounds hold. Not run by CI; its docstring says how to run it.
- [ ] `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-tracer-screenshots/` -- five simulator shots (Verification) -- the build agent, after the review.

**Acceptance Criteria:**
- Given the finished tree, when `ctest -L games-check` runs, then `GamesCheckTest` passes the package, the game's checks, and all five rounds, which include a win by each seat, a draw, a forced-board round, and a rejected move, and no snapshot passes 700 B.
- Given a scratch copy of the game with the forced-board rule or the draw rule removed, when the real target runs over it, then the rounds fail (the rounds prove the rules).
- Given the packed game in `fs_/games/`, when Games is opened in the simulator, then it installs and a pass round plays with the playable small boards highlighted, a won small board shows as a big mark, the `question` button opens the HOW TO PLAY page, and the end-of-round menu opens over the final frame.
- Given `test/game_script/harness/` and `scripts/`, when searched for the three first-party games' names, then no harness code or script names one (fixture names excepted), and `check_upstream_touches.py` passes with no ledger edit.

## Implementation Notes

Built by one implementation subagent from this plan (route: full). Choices inside the plan, none changing an outcome the Notes settle: a move is `{b, c}` (positional); every unplayable tap (out of range, closed board, outside the forced board, after the round is over) gets "Play in the highlighted board", an occupied cell "That cell is taken"; the `question` button's tap area is 80 x 100 px at the top right (icon 64 px); the game table carries two extra fields, `playable(state)` and `help_lines()`, which `checks.lua` uses (the runtime ignores extra fields; `checks.lua` runs them); grid lines are 1 px `black` lines (`light` and `dark` are fills only); the HOW TO PLAY text is seven paragraphs wrapped with `ch.text_width` at a 28 px line step. `tools/make_rounds.py` found seeds 2423 (seat 1 wins in 29 moves), 2664 (seat 2 wins in 32), 2733 (draw in 44), 1828 (the forced-board list); `rejected-move` is hand-composed and checked by the tool. Risks it named: text fit is unproven under the host's stand-in metrics (the simulator screenshots show it); a 128 px won-board mark would not fit a block on a 320 px canvas (fine at 474 and 480). Build agent's verification of the diff: the full host suite 1749/1749 and `-L games-check` 71/71 pass, the game's three tests and `HasAGame` among them.

After the review (patches applied by the build agent, the implementation subagent having finished): the won mark takes the largest icon size that fits its block; the cells of won boards are not drawn; `checks.lua` gained the ninth-cell-line case and a draw with a small board still open (`121201212`); the finished rounds' last step has `shows` for the header; `make_rounds.py` now finds a `forced-board` list whose target was won before the move (seed 3497, which also holds the own-cell case) and a `drawn` list that leaves a small board open (seed 703, 51 moves); `forced-board.lua` and `drawn.lua` hold those lists. Found by the build agent's mutant run, not by a lens: with the draw rule replaced by "draw only when no small board is open" every check and round still passed (the first `drawn` list ended with all boards closed); the new check and list turn that mutant red.

## Plan Change Log

- 2026-10-04 (orchestrator, after the owner's checkpoint): approved C1 (amended: `steps` may be a function of the decoded initial state), C2, C3, and D1 to D3, recorded as epic Notes Decisions of 2026-10-04. The owner split this ticket: groups 1 and 3 (the games check, its tests, the CI job, `first_party/README.md`) are now entry 8 (`story-the-games-check-plan.md`), which merges first; this entry keeps group 2 (Ultimate tic-tac-toe, `board.lua`, its companion folder, its rounds and checks) and the screenshots, and plays through entry 8's check. Status reset to draft so the next run narrows this plan to group 2 and the screenshots against the narrowed entry in `tickets.toml`.
- 2026-10-04 (build agent, planning, entry 8 merged into the tree): narrowed to group 2 and the screenshots. Removed the games check's tasks, its I/O rows, its code map, the three engine executables, the `games-check` job and the fresh-tree run (no CI gate or workflow changes here), and the planning run's `## Auto Run Result`. The `<intent-contract>` was the combined ticket's text; it is rewritten to the narrowed entry's `tickets.toml` description, as entry 8's plan did, not preserved verbatim. UTTT's Design Notes below follow the approved plan's group 2, with the choices the Notes leave to the builder listed under "Builder's picks".

## Review Triage Log

### 2026-10-05 - Review pass
- verdicts: 18 findings - high 0, medium 2, low 14, false 2, maybe-false 0
- findings:
  - `[false]` `reject` blind: `apply` ignores `seat` and takes the mark from `state.m` -- game-api-seed section 2: "The runtime passes moves to `apply` only from the seat that `status` names", so a wrong-seat move never reaches `apply`; the fixtures do the same.
  - `[low]` `defer` blind: one reason ("Play in the highlighted board") for malformed and after-over moves -- real wording gap, not reachable by a player; deferred (edge-case lens finding E2 is the same root).
  - `[low]` `patch` blind: hard-coded 32 and 128 px icon sizes, a 128 px mark spills on a 320 px canvas -- patched: the won mark takes the largest icon size that fits its block (128, 64, or 32 px); the small cell icon stays 32 (a 34 px cell holds it). Edge-case lens E1 is the same root.
  - `[low]` `defer` blind: HOW TO PLAY fit is checked only at the harness canvas -- deferred (no narrow device in v1).
  - `[low]` `patch` blind: forced-board round's comment says "already won" but its won target is the move's own cell -- patched: `make_rounds.py` now requires the target to be won before the move (seed 3497 rows), the round and its comment say what they cover, and the own-cell case stays in the same round. Edge-case lens E3 and E4 are the same root.
  - `[low]` `reject` blind: nothing ties the committed rounds to `make_rounds.py` -- by the plan, the tool is not run by CI; the rounds' `winners` and `move` flags are what prove the lists against `main.lua`, and a changed rule fails a round.
  - `[medium]` `patch` blind: line-vs-full precedence in a small board is untested -- patched: `checks.lua` forcedBoardHandOver plays a ninth cell that completes a line and asserts the board is won, not "3" (verification-gap lens VG1 is the same root).
  - `[low]` `patch` blind: a won board's nine cell icons are drawn and then covered -- patched: the cells of won boards are skipped.
  - `[low]` `reject` blind: UI cues (winning line, last move, header collision at 320 px) -- new features the approved design does not name, not defects; no v1 device has a 320 px canvas.
  - `[false]` `reject` blind: integral floats from the host would be refused -- the codec keeps integers as integers and tap coordinates are integers (the fixtures use `//` on them); no float path is shown.
  - `[low]` `reject` blind: duplicated `LINES` and reason strings in `checks.lua`, public `help_lines`, `setup` ignores `ctx` -- the check's constants are an independent pin on purpose, `help_lines` is in the plan, and a fix adds code for no named harm.
  - `[low]` `patch` edge E1: large icon overflows a block under 128 px -- see the hard-coded icon sizes row; patched.
  - `[low]` `defer` edge E2: after-over and malformed moves get the board reason -- see the reason-wording row; deferred.
  - `[low]` `patch` edge E3: `forced_board` accepts a target the move itself won -- see the forced-board comment row; patched in the tool.
  - `[low]` `patch` edge E4: `forced-board.lua` claim -- see the forced-board comment row; patched.
  - `[medium]` `patch` verification-gap VG1: the ninth cell completing a line is not pinned -- see the precedence row; patched.
  - `[low]` `patch` verification-gap VG2: the header text is never asserted -- patched: the last step of each finished round has `shows` "Player 1 (X) wins", "Player 2 (O) wins", or "Draw"; the fills, icons, and big marks cannot be seen by the harness, deferred (entry 8's files).
  - `[low]` `reject` intent-alignment: expectations live at the simulator surface while the diff's tests are headless -- true and planned for: the screenshots under Verification are the simulator evidence; nothing to change in the diff.

## Design Notes

**Interfaces fixed by the owner (epic Notes, 2026-10-04):** C1 the rounds file, C2 `checks.lua`, C3 `board.lua` (`layout` -> `{x, y, cell, size, block}` with `opts.top` 120, `bottom` 40, `margin` 4, `cell = min((w - 2*margin)//9, (h - top - bottom)//9)`, `size = 9*cell`, `block = 3*cell`, `x = (w - size)//2`, `y = top`: 51 px cells at x = 7, y = 120 on 474x788, 52 on 480x800, 34 on 320x480; `cell_rect` and `block_rect` return `x, y, w, h`, rows and columns 1-based; `cell_at` returns `row, col` or `nil`; `draw_grid` draws 1 px lines on every cell edge and 3 px black lines on every block edge and the border, about 30 commands, nothing else; all pure except `draw_grid`). The games check as built: README's rounds and `checks.lua` text; the driver is `Session`/`MatchRounds`, so `input`, `apply`, `status`, and `draw` for every local seat run on every step.

**UTTT (R6, R8; `first-party-games.md`, Ultimate tic-tac-toe; the approved plan's group 2):**
- `state = {c = <81 chars, "0" empty, "1" x, "2" circle; cell c of small board b is char (b-1)*9+c>, w = <9 chars, one per small board: "0" open, "1" or "2" won by that seat, "3" full with no line>, n = <forced small board 0..9, 0 = any open one>, m = <moves played>}` (about 120 B). `status`: `{over = true, winners = {mark}}` when `w` holds three of a mark in a line; `{over = true, winners = {}}` when no line is live for either mark (a line is live for a mark when each of its three `w` chars is "0" or that mark); else `{turn = m % 2 + 1}`. Seat 1 plays x, seat 2 circle.
- `apply(state, seat, move)` with `move = {b, c}`: reject a non-integer or out-of-range `b` or `c`, or a board that is not playable (closed, or not the forced one), with "Play in the highlighted board"; an occupied cell with "That cell is taken". Otherwise place the mark, close the small board (won by a line of its nine cells, or full), set `n = c` when board c is open else 0, `m = m + 1`. Every reason is within `reject_reason_bytes` (64).
- `input`: a `tap` on the `question` button (medium icon, top right, tap area at least 64 px) sets `ui.help` and makes no move; any tap while `ui.help` closes it, no move; otherwise clear `ui.message`, map the tap through `board.cell_at` to a global (row, col), then to `(b, c)`, return `{b, c}` or `nil` off the grid; a `rejected` event sets `ui.message`; `over` and others return `nil`.
- `draw`: clear white; header text "Player N (X) to move" / "Player N (O) to move" (N the turn seat; at seat 0 or over, the result: "Player N (X) wins" or "Draw"); the `question` icon; `light` fill under every playable small board (the forced one, else every open one; none when over); `light` fill under a full board; `board.draw_grid`; `x` and `circle` icons at 32 px centred in each cell; a won board drawn over its cells and lines as a white fill inside the block lines with a 128 px mark centred; `ui.message` in "small" text under the board. The HOW TO PLAY page replaces all of it: a title and the rules and controls in "small" text, wrapped by `ch.text_width` so it fits whatever the font metrics are. Fewer than 100 drawing commands plus at most 81 icons, far inside `frame_commands_count` and `frame_icon_image_pixels`.
- HOW TO PLAY text covers: seat 1 is X and seat 2 is O; tap an empty cell; the cell's place in its small board is the small board the other player must play in next; when that board is won or full they play in any open board; three in a row in a small board wins it; three won boards in a row win the game; no line left to make is a draw; the highlighted boards are the playable ones; the question button opens this page, a tap closes it.
- Snapshot and frames: under 700 B throughout (R9); the check's every-seat draw covers the frame limits.

**Rounds (C1):** all `mode = "pass"`, move lists `{b, c}` through `taps.steps`; `won-by-seat-1`, `won-by-seat-2` (`winners = {1}` and `{2}`, each ends with three won boards in a line), `drawn` (`winners = {}` by the no-live-line rule), `forced-board` (`unfinished = true`: a move sends the opponent to board c, a tap in another board is `move = false` with `shows = "Play in the highlighted board"`, the legal reply follows, and later a move sends the opponent to a won board so a free choice is played), `rejected-move` (`unfinished = true`: a tap on an occupied cell is `move = false` with `shows = "That cell is taken"`; the `question` button is `move = false` with `shows = "HOW TO PLAY"`; a tap closes it, `move = false`; a tap off the grid is `move = false`; then a legal move). The lists come from `tools/make_rounds.py`'s independent rules: its agreement with `main.lua` is what the round `winners` prove, so it must not share code with the game.

**Builder's picks the Notes leave open** (none changes an outcome they settle): the header and message wording; a closed board's tap uses the forced-board reason (the highlighted boards are the ones that can be played); the won mark is `"regular"` weight; a full board is `light` as the approved design says (a full board is never playable, so the two uses of `light` never meet); extra fields on the game table (for example a `playable(state)` helper for `checks.lua`) are allowed only if the runtime ignores them, which `checks.lua` or a round must confirm by running.

**Settled by existing text:** no `icon.png` and no manifest icon (`first-party-games.md`: the launcher's default mark; entry 1's `tickets.toml` description); text is the game's own, not `tr()` (R2); modes `["pass"]` only (R1; epic-play-nearby adds `nearby`); no zoomed view (R6); the check, not this entry, checks the package and the frames (R10).

No existing function is moved or rewritten, so no `git log -L` reading applies; no test double is added or extended (the check's doubles are entry 8's). No firmware source changes: `pio run`, `pio check`, and the sibling-env builds cover files this entry does not touch, so they are not run; `sim.sh build x4pro` is, for the screenshots.

## Verification

**Commands** (host tests and fast checks first; locks as AGENTS.md says; `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`):
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'`, then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, including `-L games-check` (the game's three tests and `HasAGame`).
- `ctest --test-dir build/test -L games-check --repeat until-fail:20` -- expected: pass.
- One-off mutants, scratch roots in `build/test/games_check_scratch/` (never committed): reconfigure with `-DGAMES_CHECK_GAMES_ROOT=... -DGAMES_CHECK_COMPANION_ROOT=...` over copies of the game with (a) the forced-board rejection removed, (b) the no-live-line draw removed, (c) a snapshot padded past 700 B, run `ctest -L games-check` -- expected: each red; then reset with `-UGAMES_CHECK_GAMES_ROOT -UGAMES_CHECK_COMPANION_ROOT` and the real target green. Record each result.
- `grep -rniE 'tic-tac|ultimate|sudoku|battleship' test/game_script/harness scripts` -- expected: no match in harness code or `scripts/` (fixture names excepted).
- `python3 tools/make_rounds.py` in the companion folder -- expected: runs and prints move lists the rounds hold (not run by CI).
- `for t in scripts/*_test.py; do python3 $t; done`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, the second run changes nothing.
- After review and patches, once: `sim.sh build x4pro` under the build lock, then the screenshots below.

**Manual checks:** simulator screenshots in `story-tracer-screenshots/`, each looked at: `installed.png` (Games list with the package installed from `fs_/games/`), `pass-round.png` (small boards the next move may use highlighted), `won-board.png` (a won small board), `how-to-play.png` (the page behind the `question` button), `over-menu.png` (the end-of-round menu over the final frame).

## Auto Run Result

**Summary.** `games/ultimate-tic-tac-toe/` is a pass-mode, two-seat package (manifest, `main.lua` with the rules, input, drawing and the HOW TO PLAY page behind the `question` button, and `board.lua` exactly as Decision C3), with its companion folder `test/game_script/first_party/ultimate-tic-tac-toe/`: five rounds (a win by each seat, a draw with a small board still open, a forced-board round, a rejected-move round), `taps.lua`, an 11-check `checks.lua`, and `tools/make_rounds.py`, an independent Python rules oracle that finds the move lists. The games check of entry 8 packs, installs and plays all of it. No upstream, `src/`, `lib/`, API, CI, or harness file changed.

**Files** (all new): `games/ultimate-tic-tac-toe/{manifest.json, main.lua, board.lua}`; `test/game_script/first_party/ultimate-tic-tac-toe/{checks.lua, taps.lua, rounds/{won-by-seat-1,won-by-seat-2,drawn,forced-board,rejected-move}.lua, tools/make_rounds.py}`; five screenshots below; this plan.

**Review** (thorough; four lenses): 18 findings, high 0, medium 2, low 14, false 2. Patched (8 rows, 5 entries): the ninth cell that completes a line (medium, pinned by a check), the forced-board round covering a target won before the move, the won mark's icon size by block, the redundant icons in won boards, the header text asserted by the finished rounds. Deferred (3, all low): the reject reason for after-over and malformed moves, HOW TO PLAY fit on a narrow canvas, and the harness seeing text commands only (fills, icons, big marks unasserted). Rejected with reasons in the triage log: seat unchecked (the runtime passes the turn seat only), rounds not tied to the tool by CI (by the plan), UI cues (new features), floats (none reachable), duplicated constants, and the simulator-surface note (answered by the screenshots). Follow-up review recommended: false (one entry of medium patched, no high).

**Verification** (on the tree committed here):
- Host build and `ctest --test-dir build/test -j`: 1749/1749 pass; `ctest -L games-check`: 71/71, among them `ThePackageInstallsAndLoads`, `TheGamesOwnChecksPass` (11 checks), `EveryRoundPlaysAsItsFileSays` (five rounds, no snapshot over 700 B, every seat drawn each step) for the game and `HasAGame`; `--repeat until-fail:20` over the label passes.
- Mutants over scratch roots in `build/test/games_check_scratch/` (deleted; both cache variables reset and the real target green after): (a) forced-board rule removed: `TheGamesOwnChecksPass` and `EveryRoundPlaysAsItsFileSays` red; (b) draw only when no small board is open: red (`drawRule` check and `drawn` step 51) after the post-review patch, green before it (see Implementation Notes); (c) 700 B of padding in the state: `EveryRoundPlaysAsItsFileSays` red.
- `grep -rniE 'tic-tac|ultimate|sudoku|battleship' test/game_script/harness scripts`: no match. `scripts/*_test.py`: all pass. `python3 scripts/check_upstream_touches.py`: PASS. `./bin/clang-format-fix` run twice: no change (no formatting-only change outside this entry's paths). `python3 .../tools/make_rounds.py` runs in about 7 s and prints the committed lists.
- `sim.sh build x4pro` (shared build cache, build lock): SUCCESS, 1 m 40 s. The package packed with `scripts/pack_game.py` (4,241 bytes), copied to `fs_/games/`, installed by opening Games, and played in the simulator. No firmware source changed, so `pio run`, `pio check`, and the other envs were not run.
- Screenshots in `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-tracer-screenshots/`, each looked at:
  - `installed.png`: the Games list with "Ultimate Tic-Tac-Toe / Pass and play" installed from `fs_/games/`, with the launcher's default mark.
  - `pass-round.png`: a round after three moves, "Player 2 (O) to move", with only the forced small board (top right) highlighted.
  - `won-board.png`: seat 1 has won small board 4 (a big X over it) and the next move is forced into the highlighted board 1.
  - `how-to-play.png`: the HOW TO PLAY page opened by the `question` button, wrapped to fit.
  - `over-menu.png`: "Player 1 (X) wins" with the end-of-round menu (Play again, Leave) over the final frame.

**Residual risks.** Text fit is shown only by these simulator frames and the host's stand-in metrics, not a device run (entry 5). The big mark and highlight are unobserved by any test (deferred). HOW TO PLAY has no scroll for a canvas narrower than the X4 Pro's. `make_rounds.py` is not run by CI; the rounds' own `winners` and `move` flags are what prove its lists against `main.lua`. Memory, flash and timing: unmeasured (no firmware change).

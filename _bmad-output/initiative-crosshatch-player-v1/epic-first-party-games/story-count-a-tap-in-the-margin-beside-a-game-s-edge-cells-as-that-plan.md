---
title: "Count a tap in the margin beside a game's edge cells as that cell"
type: 'bugfix'
ticket: '15'
created: '2026-10-10'
status: done
baseline_revision: '9c9fd3dfa088068c7944a5d607604d82a3835d9a'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: ['{project-root}/test/game_script/first_party/README.md', '{project-root}/docs/crosshatch/game-canvas.md']
warnings: ['oversized']
deferred:
  - summary: >-
      Sudoku's MENU panel rows and the HOW TO PLAY pages (and Ultimate tic-tac-toe's and Battleship's help pages) do not snap a tap in the box's side margins.
    evidence: |-
      The owner's entry-15 Decision names the grid, pad and rail only and says taps on headers stay dead. By the size rule the MENU rows (24 px inset, 80 px rows) would snap sideways, and their 72 px title band and 76 px bottom gap would snap too, which would make header taps press HINT or CLOSE; the owner has not been asked. No test pins that the panel stays unsnapped.
    location: >-
      games/sudoku/main.lua (game.input, menu_tap branch), games/sudoku/layout.lua (menu_at)
    severity: low
  - summary: >-
      docs/crosshatch/game-canvas.md does not tell game authors that the first-party games snap a tap in the box's edge margin (a gap smaller than the target's size) to the edge target.
    evidence: |-
      Blind hunter finding. The rule lives in three games' snap helpers and the owner's Decision in the epic Notes; a fourth game's author has nothing pointing at it. Fork-only doc, no upstream ledger row needed.
    location: >-
      docs/crosshatch/game-canvas.md
    severity: low
---

<intent-contract>

## Intent

**Problem:** On the owner's X4 Pro a finger on Ultimate tic-tac-toe's right column reports canvas x 450 to 460, but the grid ends at x 457, so a plain tap at x 458 to 460 hits no cell and is ignored (device run, entry 5; two of 50 taps, at (460,418) and (458,557)). Sudoku's grid, pad and rail and Battleship's board and buttons end at the same kind of edge.

**Approach:** In each first-party game, a tap inside the 466 x 788 box that falls in the margin between an edge target and the box's edge, on the same row or column as that target, counts as the nearest target, when the gap is smaller than the target's own size (the owner's Decision, in Design Notes). The games' visuals do not change; the pure hit-tests (`cell_at`, `key_at`, `rail_at`, Battleship's `cell_at`) stay exact.

## Boundaries & Constraints

**Always:** The box is 466 x 788 centred in `ch.screen`; margins are measured in the box (`L.ox`, `L.oy`, `board.W`, `board.H` / `layout.W`, `layout.H`), so on the Sticky's 474 x 788 the 4 px outside the box still miss. A gap is a margin only when it is smaller than the edge target's own size along the gap's axis: a cell side for a grid, a key's `kw` x `kh` for the pad, a rail button's `rail_w` x `bh` for the rail, a button's `bw` x `h` for Battleship's button row. A point snaps only when it lies on the target's own rows (left and right margins) or columns (top and bottom), so a corner never snaps. An exact hit on any target wins over a snap. Each game stays self-contained (R2): no shared file and no `require` across games; Ultimate tic-tac-toe's and Sudoku's `board.lua` copies get the same new text.

**Never:** No change to any drawing, to `cell_at` / `key_at` / `rail_at`, to a manifest, to `src/`, `lib/`, `docs/crosshatch/api-level-1.txt`, `device-run-packet.md` or the `.chgame` files in `device-run-packet/`. No snap into Sudoku's MENU panel rows or any HOW TO PLAY page (the Decision names the grid, pad and rail only; the overlay panels are deferred). No snap of a header, title band or message line.

## I/O & Edge-Case Matrix

Box coordinates (add `ox` on a wider canvas); the owner's Decision fixes which gaps snap.

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| UTTT right margin | tap x 458..465, any y 120..569 | the move of the column 9 cell on that row | none |
| UTTT left margin | tap x 0..7, y 120..569 | the column 1 cell of that row | none |
| UTTT top, bottom | tap y 0..119 (header), y 570..787 (message area), x inside the grid | no cell (120 and 218 px are not smaller than a 50 px cell) | tap ignored |
| UTTT help button | tap x >= 386, y < 100 | HOW TO PLAY opens, as today | none |
| Corner | tap x 460, y 119 or y 570 | nothing | ignored |
| Sudoku grid | tap x 0..7 or 458..465 with y 56..505 | the column 1 or 9 cell of that row; the grid's top (56 px) and the 8 px gap to the pad do not snap | none |
| Sudoku pad | tap x 0..7, y 514..753; or x 8..307, y 754..787 | the key in column 1 on that row; the key in row 3 of that column | none |
| Sudoku rail | tap x 458..465, y 514..753; or x 316..457, y 754..787 | the rail button on that row; MENU | none |
| Sudoku gaps | x 308..315 at y 754..787; y 506..513 | nothing (between targets; x 308..315 is on no target's column) | ignored |
| Battleship big board | tap x 0..12 or 453..465, y 52..491 (placing or firing) | the column 1 or 10 cell of that row; a placement tap takes the same shift-back as an in-board tap | none |
| Battleship buttons | placing, tap x 0..12 or 453..465, y 576..659 | Rotate, or Ready | none |
| Battleship no snap | y 0..51 outside the question rect, y 492..575, y 660..787 | nothing | ignored |
| Sticky | box offset 4: x ox-1 and ox+466 | miss | ignored |

</intent-contract>

## Code Map

Geometry in the 466 x 788 box (from the code): UTTT `board.layout(w, h)` has cell 50, grid x 8..457, y 120..569 (`top` 120). Sudoku `layout.get()` has the grid at x 8..457, y 56..505; `pad_y` 514, `kw` 100, `kh` 80, pad x 8..307, y 514..753; `rail_x` 316, `rail_w` 142 (to x 457), `bh` 60, rail y 514..753. Battleship `layout.compute`: cell 44, `big` x 13..452, y 52..491; `buttons` row y 576..659, `bw` 104, x 13..452; `question_rect` x 394..465, y 0..51.

- `games/ultimate-tic-tac-toe/board.lua` -- `cell_at(L, x, y)` stays exact and pinned (checks.lua `cellAtInvertsCellRect` expects nil one pixel outside). Add `board.snap(L, x, y, bx, by, bw, bh, tw, th)` (the generic rule, returning the point moved onto the block's edge when it lies in a margin, else unchanged) and `board.tap_cell(L, x, y)` (the grid's row, col after snap).
- `games/ultimate-tic-tac-toe/main.lua` -- `game.input` ~line 138 calls `board.cell_at`; the help test `tapIsHelp` runs first and stays first.
- `games/sudoku/board.lua` -- a copy of UTTT's `board.lua` (R3): the same two functions, the same text.
- `games/sudoku/layout.lua` -- `cell_at`, `key_at`, `rail_at` stay exact (`rules.lua` `rules.layout` pins them). Add `layout.snap_cell`, `layout.snap_key`, `layout.snap_rail` (x, y -> x, y) over `board.snap` with the grid, pad and rail blocks and their element sizes.
- `games/sudoku/main.lua` -- `game.input` ~lines 321-331: after the MENU branch, `layout.cell_at(layout.snap_cell(ev.x, ev.y))`, then keys, then rail, in the same order. The `ui.panel == "menu"` and help branches stay as they are.
- `games/battleship/layout.lua` -- `cell_at(board, px, py)` stays exact. Add `layout.snap(L, px, py, rect, tw, th)` (the same rule over a `{x, y, w, h}` block; the box from `L.ox`, `L.oy`, `L.W`, `layout.H`), `layout.board_rect(B)`, and `L.button_row = { x, y, w, h }` plus `L.button_w` in `compute`.
- `games/battleship/main.lua` -- `game.input` ~lines 157-185: the question rect test first (exact), then the placing buttons and board, and the firing board, each on the snapped point. A snapped placement tap then takes the existing shift-back (`math.min(row, ...)`).
- `test/game_script/first_party/ultimate-tic-tac-toe/checks.lua` -- add the pins as new entries in the returned list (~line 600); `SIZES` (474, 466, 480 x 800) and `withCanvas` are there. `taps.lua` gets `taps.margin(b, c)` (a tap in the left or right margin on the cell's row, for a cell in column 1 or 9). `rounds/margin-taps.lua` (new) plays moves by margin taps; `rounds/rejected-move.lua` and `taps.off_grid()` (40 px below the grid) must keep their outcome.
- `test/game_script/first_party/sudoku/rules.lua` -- add `rules.margins()` beside `rules.layout()` (~line 188), called from `rounds/how-to-play.lua` where `rules.layout()` is called (it runs at both canvases); helpers `on_cell`, `on_key`, `tap` exist. Keep `rules.layout` as it is.
- `test/game_script/first_party/battleship/checks.lua` -- add the pins near `inputPlacement` / `inputFiring` (~lines 739-860: `tapEvent`, `tapCell`, `tapButton`, `sameMove`); `SIZES` is at ~line 129.
- Do not touch: `device-run-packet.md`, `device-run-packet/`, `src/`, `lib/`, `freeink-sdk`, `.skills/`, generated files, `test/game_script/*.cpp`.

## Tasks & Acceptance

**Execution:**
- [ ] `games/ultimate-tic-tac-toe/board.lua`, `main.lua` -- add `board.snap` and `board.tap_cell`; `game.input` uses `tap_cell` -- the right-column fix
- [ ] `games/sudoku/board.lua`, `layout.lua`, `main.lua` -- the same copy; `layout.snap_cell/key/rail`; `game.input` snaps grid, pad and rail
- [ ] `games/battleship/layout.lua`, `main.lua` -- `layout.snap`, `board_rect`, `button_row`; `game.input` snaps the board and (placing) the button row
- [ ] `test/game_script/first_party/ultimate-tic-tac-toe/checks.lua`, `taps.lua`, `rounds/margin-taps.lua` -- pins at the 466, 474 and 480 x 800 canvases: every x from the grid's right edge to the box's right edge (458..465 on 466) on every row places the column 9 move, and the left side likewise; x ox-1 and ox+466, the corners, the header (y 0..119 outside the help area), 40 px below the grid, and the help button are unchanged; a layout made with `{top = 30, bottom = 30}` exercises the top and bottom branches of `board.snap` (a 30 px gap snaps, a 60 px one does not); a round places moves by margin taps, with a corner tap that is `move = false`
- [ ] `test/game_script/first_party/sudoku/rules.lua`, `rounds/how-to-play.lua` -- `rules.margins()`: through `game.input`, the grid's left and right, the pad's left and bottom, and the rail's right and bottom margins act as their targets; brute force: over every point of the box at most one of cell/key/rail answers, and the points of the gaps listed in the matrix answer none
- [ ] `test/game_script/first_party/battleship/checks.lua` -- pins at the three canvases: board margins in placing and firing (placement shift-back at column 10), Rotate and Ready by margin, no snap at the listed rows, the question button still wins at x 458..465, y 0..51, and over every point of the box in placing at most one target answers

**Acceptance Criteria:**
- Given an X4 Pro canvas, when a tap lands at canvas x 460 on Ultimate tic-tac-toe's right column (and at every x from 458 to 465), then the move of the column 9 cell on that row is placed, as with a tap at its centre.
- Given any game and canvas, when a tap lands in a header, a message area, a corner, between targets, or outside the box, then nothing happens, as before.
- Given the games check run unchanged on the new tree, when it plays the existing rounds and checks, then each has the same outcome as before.

## Implementation Notes

Where to work: this plan's repository is the git worktree `/home/user/epic-first-party-games-lane-c` (branch `epic-first-party-games-lane-c`). Every path in this plan is relative to it. Begin every Bash command with `cd /home/user/epic-first-party-games-lane-c &&`, use absolute paths under it for Read, Edit and Write, and never touch `/home/user/crosshatch-player` (the main checkout). The shell resets its directory between commands.

Rules from AGENTS.md that bear on this change: run the host build and `ctest` only through `flock /tmp/crosshatch-hosttest.lock sh -c '...'`; run `./bin/clang-format-fix` (no arguments) last, after `git add` of any new file, and again to confirm nothing changes; never run `git clean -fdX`; do not commit (the build agent commits); Lua lines in this repo wrap at 120 columns; a test double or screen double that the checks add must be no more permissive than the device (a tap at or over the canvas width never reaches `input`). Record in this section, as you go, files touched and surprises.

Build notes: files touched are the games' `board.lua` (UTTT, Sudoku: `board.snap`, `board.tap_cell`), UTTT `main.lua`, Sudoku `layout.lua` / `main.lua`, Battleship `layout.lua` / `main.lua`, and the companions named in the Code Map (UTTT `taps.lua`, `checks.lua`, `rounds/margin-taps.lua`; Sudoku `rules.lua`, `rounds/how-to-play.lua`; Battleship `checks.lua`). No surprises. Battleship's checks gained their own `withCanvas` (the harness plays a check at one canvas, the pins need three). The brute-force sweeps use a stride away from the margins to stay inside the check VM's instruction budget. New pack hashes (old at b86fe20e): ultimate-tic-tac-toe `cd11a6224228acd9` (was `a7e63b542144938b`), sudoku `799b0b36c86bed2d` (was `0d3f38fe297ddb6e`), battleship `d3fecc1e5fc245f6` (was `a0de5525493b90da`).

## Plan Change Log

## Review Triage Log

### 2026-10-10 — Review pass
- verdicts: 21 findings — high 0, medium 1, low 18, false 2, maybe-false 0
- findings:
  - `[low]` `[patch]` (blind hunter) Sudoku `snap_rail` hardcodes `4 * L.bh` while `rail_at` uses `layout.RAIL` — a fifth rail button would snap the bottom margin onto the wrong edge; fixed to `layout.RAIL * L.bh`.
  - `[low]` `[reject]` (blind hunter) `board.snap` is copied in UTTT's and Sudoku's `board.lua` and in Battleship's `layout.lua` with a different signature — each game must stay self-contained (epic R2, R3: nothing shared, `require` loads only a package's own root); the two `board.lua` copies are byte-identical by the plan's rule, and every game's pins test its own copy.
  - `[low]` `[reject]` (blind hunter) `layout.board_rect` builds a table per tap — one small table per tap on a 788 px screen is not a cost anyone meets; caching it needs a new field and buys nothing.
  - `[low]` `[reject]` (blind hunter) Battleship's board snap runs before the mode is known — the call is pure and its result is used only in placing and firing; moving it adds a branch and changes nothing observable.
  - `[false]` `[reject]` (blind hunter) `onlyOneTarget` runs at one canvas only — `test/game_script/harness/games_check.cmake` plays every check on the 474 and the 466 canvases (`GamesCheckTest` and `GamesCheck466Test`), so the sweep runs at both; `marginTaps` additionally loops three sizes.
  - `[low]` `[reject]` (blind hunter) magic numbers and a weak closing assert in `onlyOneTarget` — the sweep's edges now come from the layout, the patch below made it call `game.input` and assert agreement per point, and the mutants in the next rows fail it; a rewrite for style adds nothing.
  - `[low]` `[reject]` (blind hunter) pixel figures in comments and test names go stale — they are the owner's Decision's own figures, each a measured geometry at the box; the pins compute from the layout and fail when it moves.
  - `[low]` `[reject]` (blind hunter) `rules.margins` hangs off the how-to-play round — `rules.layout` hangs there too and it is the harness's way to run a pin at both canvases in Sudoku's small-heap VMs; the call is in the round and the games check fails if it fails.
  - `[low]` `[reject]` (blind hunter) `rules.margins` sweeps a large area with dense filters — it runs in the check VM's budget (the games check passes) and its edge and gap pixels are all swept.
  - `[low]` `[defer]` (blind hunter) no test pins that Sudoku's MENU panel and HOW TO PLAY page stay unsnapped, nor that a margin tap on a selected edge cell toggles it like an exact tap — the first is the deferred owner question (deferred item 1); the toggle is the same code path as an exact tap and not a defect.
  - `[low]` `[defer]` (blind hunter) no note in `docs/crosshatch/game-canvas.md` — deferred item 2; the Decision is in the epic Notes.
  - `[low]` `[patch]` (edge-case hunter) `taps.margin` returns `L.x + L.size + 3` (x 461) while its comment and the round's say "3rd px right" and "x 460" — now `+ 2` (x 460) and the comments say what the code does; the round still plays.
  - `[low]` `[reject]` (edge-case hunter) the sweeps in Sudoku and Battleship sample interior points — every margin and gap pixel along x is swept, and the pixels within 4 px of each target edge along y; the sampled interior strips lie inside targets or in the gaps the other rows pin, and Battleship's stride was widened to 12 only for the `game.input` calls' budget.
  - `[low]` `[reject]` (edge-case hunter) `L.button_row.w = 4 * bw + 3 * gap` truncates when `10 * cell - 24` is not divisible by 4 — `cell` is 44 on every supported canvas (a smaller canvas is unsupported), and 416 divides by 4.
  - `[medium]` `[patch]` (verification gap) Battleship's header, strip and under-buttons rows were never tapped through `game.input` at board or button columns, and `onlyOneTarget` re-called `layout.snap` itself; two mutants (board snap `th` 60; button snap `th` 200) stayed green — verified by reading `marginTaps`' `no` list (box-edge columns only) and `answers()`. Fixed in `battleship/checks.lua`: `marginTaps` taps those rows through `game.input` in placing and firing and asserts nothing happens, `onlyOneTarget` classifies each point through `game.input`; both mutants now fail (`marginTaps` at 17,0 and 17,660; `onlyOneTarget` at 28,0 and 28,660), `main.lua` restored byte-identical.
  - `[low]` `[reject]` (verification gap, other finding) `board.tap_cell` in Sudoku's `board.lua` has no caller there — kept: the plan makes the two `board.lua` copies identical text (R3), deleting it forks them.
  - `[low]` `[reject]` (intent alignment) the Sudoku pins run through `game.input` and no Sudoku round places a move from a margin tap, and UTTT's round does not sweep each x — the intent asks for pins "in the game's companion checks" and for UTTT a move placed at x 460; `marginTaps` sweeps every x through `game.input` and the round places six moves by margin tap.
  - `[low]` `[reject]` (intent alignment) the checks use box-relative offsets, so "canvas x 458 to 465" is exercised as box-relative on the Sticky — the plan fixes the frame (margins are the box's, and the Sticky's 4 px outside it miss); at 466 box and canvas coincide.
  - `[false]` `[reject]` (intent alignment) the diff carries no hashes or run evidence — they belong to the plan's Verification and `## Auto Run Result`, not the diff.
  - `[low]` `[reject]` (intent alignment) test helpers (`moveAt`, `taps.margin`) agree with the code by construction — the cells they name come from the layout's rectangles, and the pins also assert the nil cases, the corners, the headers and the one-target sweeps they cannot reach by construction.
  - `[low]` `[reject]` (intent alignment) `snapRule` tests top and bottom branches on synthetic layouts — no shipped geometry reaches the top branch; the shipped consequence (UTTT's 120 and 218 px, Sudoku's grid top, Battleship's top stay dead) is pinned through `game.input`.

## Design Notes

**The rule is the owner's Decision (owner, 2026-10-10, entry 15; epic Notes, the line "Decision (owner, 2026-10-10, entry 15)"):** reading D of the earlier blocked run. A gap counts as a target's margin only when it is smaller than the target's own size. Left and right margins snap on the targets' own rows (8 px on Ultimate tic-tac-toe and Sudoku, 13 px on Battleship); Sudoku's pad and rail bottom snaps (34 px against 80 px keys); not snapped: UTTT's top (120) and bottom (218), Sudoku's grid top (56), Battleship's top (52) and button-row bottom (128); headers and message lines stay dead; corners off every target's row and column do not snap; a snapped Battleship placement tap goes through the same shift-back rule as an in-board tap; the button-row targets snap sideways like Sudoku's pad. The cause and the cells to cover come from tickets.toml entry 15 and the epic Notes' Finding of 2026-10-10 (second device run).

**Why the snap is a new function and not a change to `cell_at`:** `cell_at` is pinned to nil one pixel outside (UTTT `cellAtInvertsCellRect`, Battleship `cellAtInvertsCellRect`, Sudoku `rules.layout`), and its other users (drawing, tiles) want the exact cell. No existing function is moved or rewritten, so no guard is dropped; `game.input` keeps its order (help or question first, exact hits before snapped ones).

**Why snapping after the exact test is safe:** every margin strip lies outside every exact target, and two targets' strips never overlap (grid rows 56..505 against pad rows 514..753; pad columns 8..307 against rail columns 316..457; Battleship's board rows 52..491 against its button rows 576..659 and the question rect y 0..51); the brute-force pins assert it.

**Not covered, on purpose:** Sudoku's MENU rows have a 24 px inset and 80 px rows; by the size rule they would snap, including from the panel's title band, but the Decision lists "grid, pad and rail" only and says header taps stay dead. A deferred item for the owner.

**Test doubles:** the checks call `game.input` with `{kind = "tap", x, y}` events as the engine does after its own canvas check; the rounds go through the real engine, which drops a tap at or over the canvas width, so rounds pin margins inside the box only.

## Verification

**Commands:**
- Host, under the lock, from the worktree root: `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- all pass, the games check at both canvases included
- `python3 scripts/pack_game_test.py` and the other fork script tests (`scripts/*_test.py`), `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py` -- pass
- `./bin/clang-format-fix` twice -- the second run changes nothing (`git add` new files first)
- `python3 scripts/pack_game.py games/<id> <scratch dir>` for ultimate-tic-tac-toe, sudoku and battleship -- record each new hash beside the old ones at `b86fe20e` (`a7e63b542144938b`, `0d3f38fe297ddb6e`, `a0de5525493b90da`)
- No firmware build (only `games/**` and companion checks change); no screenshots (the look is unchanged)

**Manual checks (if no CLI):**
- Each game's new pin fails when its `game.input` goes back to the exact hit-test (mutate in scratch, see the pin fail, restore).

## Auto Run Result

**Summary.** A tap in the 466 x 788 box's margin beside an edge target now counts as that target in all three first-party games, under the owner's entry-15 Decision (a gap smaller than the target's own size, on the target's own rows or columns). Ultimate tic-tac-toe's grid snaps on its sides (8 px: a tap at canvas x 458 to 465 places the column 9 move); Sudoku's grid snaps on its sides, its pad on the left and at the bottom (34 px), its rail on the right and at the bottom; Battleship's board snaps on its sides in placing and firing (13 px, with the placement shift-back) and the button row snaps sideways. Headers, message lines, corners, the gaps between targets and the box's outside never snap. `cell_at`, `key_at` and `rail_at` are unchanged and no drawing changed.

**Files.**
- `games/ultimate-tic-tac-toe/board.lua`, `games/sudoku/board.lua` -- identical new `board.snap` and `board.tap_cell`.
- `games/ultimate-tic-tac-toe/main.lua` -- `game.input` hit-tests through `board.tap_cell`.
- `games/sudoku/layout.lua`, `games/sudoku/main.lua` -- `layout.snap_cell`, `snap_key`, `snap_rail`; `game.input` hit-tests on the snapped points.
- `games/battleship/layout.lua`, `games/battleship/main.lua` -- `layout.snap`, `board_rect`, `L.button_row`, `L.button_w`; `game.input` hit-tests on snapped points (the question button stays exact and first).
- `test/game_script/first_party/ultimate-tic-tac-toe/{checks.lua,taps.lua,rounds/margin-taps.lua}` -- `marginTaps` and `snapRule` checks, margin-tap helpers, a round with six moves by margin tap and a corner tap that changes nothing.
- `test/game_script/first_party/sudoku/{rules.lua,rounds/how-to-play.lua}` -- `rules.margins`, run at both canvases from the help round.
- `test/game_script/first_party/battleship/checks.lua` -- `marginTaps` and `onlyOneTarget` through `game.input`, at three canvases.
- The plan, the only file outside `games/**` and the companions.

**Review.** Four lenses ran as context-free subagents (blind hunter, edge-case hunter, verification-gap, intent-alignment). 21 findings: high 0, medium 1, low 18, false 2. Patched: the medium (Battleship's vertical non-snap rows and `onlyOneTarget` through `game.input`; two mutants that stayed green now fail) and two lows (`snap_rail` uses `layout.RAIL`; `taps.margin` pixel and comments). Deferred: two lows, in `deferred` (the unsnapped Sudoku MENU and help pages, which the Decision does not name; a game-canvas.md note). Every reject carries its reason in the Review Triage Log.

**Follow-up review recommended: false.** One medium was patched and no high; the patch was proven by the two mutants failing and the full suite passing.

**Verification (on the tree this commit holds, after the last patch).**
- Host: `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake ... && cmake --build build/test && ctest --test-dir build/test -j'` -- 1853 of 1853 passed, games-check 129 of 129 at the 474 and 466 canvases; the new Ultimate tic-tac-toe round and the new checks are among them (`ctest -N` lists the per-game tests).
- Fork script tests (`scripts/*_test.py`, 13) pass; `check_upstream_touches.py` PASS (no upstream-listed file changed); `check_layers.py` passed.
- `./bin/clang-format-fix` twice: the second run changed nothing; no formatting-only change outside my paths.
- Pin mutations (by the implementation subagent, restored byte-identical): each game's `input` reverted to the exact hit-test fails its new pin; Battleship's two `th` mutants fail after the patch.
- No firmware build and no simulator run: only `games/**` and companion checks changed, and the look is unchanged. No screenshots.

**Package hashes** (`scripts/pack_game.py games/<id>`; before at `b86fe20e` in brackets):
- ultimate-tic-tac-toe `cd11a6224228acd9` (`a7e63b542144938b`)
- sudoku `e1d2bd0eb8c1c750` (`0d3f38fe297ddb6e`)
- battleship `d3fecc1e5fc245f6` (`a0de5525493b90da`)

**Residual risks.** The margin rule is pinned on the host; no device has tapped x 458 to 465 since the change (the next device run should repeat the Ultimate tic-tac-toe right column with the touch lines). Sudoku's MENU rows and the help pages do not snap (deferred, owner's call). Battleship's `onlyOneTarget` samples every 12 px in the interior along y (every margin and gap pixel is swept).

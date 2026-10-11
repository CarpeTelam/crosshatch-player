---
title: 'e8-menu-snap: snap a tap in Sudoku MENU panel side insets and bottom gap'
type: 'feature'
ticket: ''
created: '2026-10-11'
status: 'built'
review: 'thorough'
review_source: 'pinned'
baseline_revision: 'f47948352cf0d96f91908c93fdd14416f61826de'
route: 'oneshot'
route_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: []
deferred: []
---

<intent-contract>

## Intent

**Problem:** Sudoku's MENU panel (`menu_at`: nine 77 px rows from box y 72 to 765, 24 px side insets) takes a tap only exactly on a row, while the grid, pad and rail snap a tap in their edge margins (entry 15). Per the owner's Decision of 2026-10-11 (epic Notes, retrospective A-10; `deferred-work.md` `## 8.15`), a tap in the side insets counts as the row beside it, a tap in the 23 px gap under CLOSE counts as CLOSE, and the 72 px title band above HINT stays dead (HINT forfeits the best time). Taps outside the 466 x 788 box never snap.

**Approach:** Add `layout.snap_menu` (a guard for the title band, then `board.snap` over the rows' block), have `game.input`'s menu branch go through it, and pin each edge in `rules.lua`, called from the how-to-play round, which the games check plays on the 474 and 466 canvases.

</intent-contract>

## Implementation Notes

Oneshot: about 90 lines across four files.

- `games/sudoku/layout.lua`: new `layout.snap_menu(x, y)`: a tap above `row_y` (the 72 px title band) returns unchanged, else `board.snap` over the rows' block (`row_x`, `row_y`, `row_w` x 9 rows, targets `row_w` x `row_h`). `board.snap` alone would also snap the band (72 px < the 77 px row), so the guard is the one new rule. The header comment that said the MENU panel has no snap now lists four snaps; HOW TO PLAY needs none (any tap closes it).
- `games/sudoku/main.lua`: `game.input`'s menu branch reads `layout.menu_at(layout.snap_menu(ev.x, ev.y))`; `menu_at` and `menu_tap` are unchanged, so the exact lookup stays exact (`rules.layout` still pins it).
- `test/game_script/first_party/sudoku/rules.lua`: new `rules.menu_margins`, called from `rounds/how-to-play.lua` beside `rules.margins`, in the VM that already has the room. The games check plays that round on the 474 and 466 canvases. Layout level, every pixel: side insets give their row (three rows' heights each), the bottom gap gives row 9; the title band, the corners off the rows' columns and the Sticky's 4 px outside the box give nil. Through `game.input`: each row's inset taps (both sides) equal its centre tap's move and ui, the bottom gap equals CLOSE's, and the band's equal a tap that does nothing, which differs from HINT's own tap.
- `README.md` and `docs/crosshatch/game-canvas.md` say nothing about the menu's margins (grep for dead, margin, snap, MENU), so they stay.

## Design Notes (sources)

- Side insets and bottom gap snap, band stays dead: epic Notes, Decision (owner, 2026-10-11, retrospective A-10); `deferred-work.md` `## 8.15`. Geometry: 77 px rows (`row_h = min(80, 700 // 9)`), 24 px insets, 72 px band, 23 px gap.
- Corners off the rows' columns do not snap, and the Sticky's 4 px outside never snap: entry 15's Decision (epic Notes, 2026-10-10) "Corners off every target's row and column do not snap", `board.snap`'s box bound; the bottom gap therefore snaps only over the rows' columns (x 24 to 441), as the pad's and rail's 34 px do. The 8.15 text says a tap "in the 23 px gap below row 9" counts as CLOSE; the corners of that strip are the same case as the pad's, and a stray corner tap closes nothing worse than a missed one.
- `snap_menu` changes no existing function: nothing is moved or rewritten, so no `git log -L` guard is at risk.

## Plan Change Log

## Review Triage Log

### 2026-10-11 — Review pass
- verdicts: 13 findings — high 0, medium 0, low 6, false 7, maybe-false 0 (Verification Gap filed none; the intent audit is descriptive)
- findings:
  - Blind Hunter
    - `[false]` `[reject]` Shared `s` mutated by row taps — `game.input` never applies a move (`game.apply` does), and `seen` rebuilds `ui` and the store each call, so `s` is read-only through these taps.
    - `[low]` `[reject]` `seen` compares only the move and a few ui fields — the per-pixel layout loop pins row identity; the input half pins that main.lua calls `snap_menu` (its mutation fails); adjacent rows have distinct signatures. A wider signature adds complexity for no named harm.
    - `[low]` `[reject]` Box edges hard-coded (`+ 465`, `+ 787`) — same as `rules.margins` beside it; the box is the fixed 466 x 788 (`board.W`, `board.H`).
    - `[low]` `[patch]` "Every pixel" wider than the coverage — the comment now says what is sampled (every x, three pixel rows per row; an every-pixel loop exceeded the check VM's instruction budget, tried and reverted). The outside-the-box part is `ox > 0` only because the 466 canvas has no pixel outside the box (a tap off the canvas fails its step).
    - `[false]` `[reject]` Test attached to the how-to-play round — the brief and entry 15 put the snap pins in `rules.lua` beside `rules.margins`, which that round already calls; it names `rules.lua:<line>` and the edge in a failure.
    - `[false]` `[reject]` Narrower bottom gap (corners dead) is an unconfirmed deviation — entry 15's Decision says corners off every target's row and column do not snap; the epic Decision says the panel follows that rule. One reading; cited in Design Notes. The layout.lua comment now says the corners stay dead (patch to the comment).
    - `[false]` `[reject]` Tracking documents not updated — the brief reserves `deferred-work.md` and the retrospective to the orchestrator.
    - `[false]` `[reject]` `board.snap` may return nil — it returns `x, y` on every path (board.lua:55-66).
  - Edge Case Hunter
    - `[low]` `[patch]` Insets sampled at three y per row — as above.
    - `[false]` `[reject]` Taps above or below the box not pinned, no 466 outside check — the canvas is the box on 466 (no such pixel), and a tap off the canvas is not delivered.
    - `[low]` `[reject]` `ch.store.set({})` at the end wipes a seeded store — `interaction.lua` resets it the same way; the round's later steps read none of it.
    - `[false]` `[reject]` A side-inset slip beside HINT forfeits the best time — the owner's Decision says the side insets count as the row beside them, row 1 included; only the title band above is dead.
    - `[low]` `[reject]` "Every pixel" claim — same as the first.
  - Verification Gap: no findings (it ran the suite and three mutations: all fail as pinned).
  - Intent Alignment (descriptive): it names one open reading, R3, the bottom corners counting as CLOSE; the Decision's own reference to entry 15's corner rule settles R2 (cited above). The test placement and the unit-level `game.input` calls are the plan's design.

## Verification

**Commands:**
- `cmake --build build/test` (it packs the games; ctest alone reads the last pack) then `ctest --test-dir build/test -R sudoku` -- expected: pass on both canvases
- mutations (each rebuilt): `snap_menu` the identity; no title guard; sides only; bottom only; left only; right only; `main.lua` not calling it -- expected: the how-to-play round fails on both canvases, naming the edge
- `./bin/clang-format-fix` twice; `python3 scripts/check_upstream_touches.py`

## Auto Run Result

**Summary:** Sudoku's MENU panel snaps a tap in the 24 px side insets to the row beside it and a tap in the 23 px gap under CLOSE (over the rows' columns) to CLOSE; the 72 px title band above HINT, the bottom corners and everything outside the 466 x 788 box stay dead. `menu_at` is unchanged. Sudoku's package hash changes.

**Files:**
- `games/sudoku/layout.lua`: `layout.snap_menu`; the header comment on snaps updated (it said the MENU panel had none).
- `games/sudoku/main.lua`: the menu branch of `game.input` goes through `snap_menu`.
- `test/game_script/first_party/sudoku/rules.lua`: `rules.menu_margins`.
- `test/game_script/first_party/sudoku/rounds/how-to-play.lua`: calls it (the games check plays it on the 474 and 466 canvases).
- `_bmad-output/implementation-artifacts/plan-e8-menu-snap.md`: this plan. `README.md` and `game-canvas.md` do not describe the menu's margins and are unchanged. `drawn.lua`, `canvases.lua`, `checks.lua`, `pins.lua` untouched.

**Review:** 4 lenses ran as context-free subagents. Patches: 2 comment rewordings (the sampling claim; the corners). Deferred: none. Rejected: every other finding, with its reason in the Review Triage Log. followup_review_recommended: false (no high, no medium).

**Verification:**
- `cmake --build build/test` then the full `ctest` (2052 tests): all pass, `-R sudoku` 11 of 11 on both canvases.
- Mutations, each rebuilt (the games are packed at build time, so ctest alone reads the old pack): `snap_menu` the identity; no title guard; sides only; bottom only; left only; right only; `main.lua` not calling `snap_menu`. Each fails the how-to-play round on both canvases naming the edge, e.g. `the left inset, row 1 at 4,72 got nil, wanted 1` (474), `the title band at 28,0 got 1, wanted nil`, `the bottom gap at 28,765 got nil, wanted 9`.
- `./bin/clang-format-fix` twice: no change. `python3 scripts/check_upstream_touches.py`: PASS.
- No firmware code changed: no `pio run`/`pio check`. No simulator shot taken (optional).

**Residual risks:** the bottom corners (outside x 24 to 441) stay dead, by entry 15's corner rule; the owner's 8.15 wording ("the 23 px gap") does not name them. The input half of the pin samples a few taps per row, not every pixel. The device is unrun.

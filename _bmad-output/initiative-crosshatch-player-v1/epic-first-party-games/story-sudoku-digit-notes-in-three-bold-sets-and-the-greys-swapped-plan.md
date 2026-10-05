---
title: 'Sudoku digit notes in three bold sets, and the greys swapped'
type: 'feature'
ticket: '9'
created: '2026-10-05'
status: 'built'
baseline_revision: 'af31d7655f1117c5c20eb333f1c287f5f71ce495'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: true
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/api-level-1.txt'
warnings: [oversized]
deferred:
  - summary: >-
      Nothing in CI runs make_note_images.py, so the baked phase of the 27 PNGs (and their 12 x 16 size) is pinned only by the tool's own --check, which compares the files with the tool's output, and by the manual simulator pixel check.
    evidence: |-
      The verification-gap lens showed that flipping the H rule's parity in the tool and regenerating the PNGs passes every committed test: rules.frame pins the tile origin's x + y, not the pixels, and --check compares the files with what the tool writes. A companion tool's test cannot go in scripts/ (R2: the repository's tooling names no game) and the games check cannot read PNGs. This entry strengthened --check (a stray note_*.png, a glyph that is not 10 x 14, and layout.lua's NOTE_W and NOTE_H against IMAGE_W and IMAGE_H now fail it), and the pixel check on the simulator shot found 0 violations in 4,680 ring pixels. A seam would show as a visible checker break on a shaded notes cell.
    location: >-
      test/game_script/first_party/sudoku/tools/make_note_images.py
    severity: low
  - summary: >-
      The tiles' dither phase assumes the viewport origin's x + y is even (insets left 3 and top 9, or 3 and 3 inverted), which a game cannot read; it is confirmed on the x4pro simulator only.
    evidence: |-
      BoardConfig.h has one ViewableInsets default (top 9, right 3, bottom 3, left 3) and no profile overrides it, so reading says every board agrees, and the simulator pixel check (the same viewport code as the device) found 0 violations; an origin of odd x + y would put a one-pixel checker seam around every H tile (the same check with the origin moved one row gave 135 violations). The Sticky and the device itself have not been run. Settle it in entry 5's device run by looking at a SHADE PEERS row with digit notes on the X4 Pro and the Sticky.
    location: >-
      games/sudoku/layout.lua (note_tile)
    severity: medium (unverified)
---

<intent-contract>

## Intent

**Problem:** Sudoku's digit notes (two sets of 10 x 14 images) and its greys (a clue white on `"dark"`, SHADE PEERS cells `"light"`) read poorly; the owner's two Decisions of 2026-10-05 replace them with 27 bold 12 x 16 images in three sets and a swapped ground scheme, approved from the mocks `notes-look-mock/schemes-now-vs-swap.png` (right block) and `block-bold-12x16-aligned.png`.

**Approach:** The companion tool `make_note_images.py` writes G (grey numeral on white), B (black numeral on white), and H (white numeral on a 50% checker) with its own bold glyph bitmaps; `layout.lua` gives each note tile a slot inside the cell's grid lines whose screen parity matches the dither it lands on; `view.lua` draws G and B in a normal cell, H in a SHADE PEERS cell (the focused digit's note H drawn `"white"`), a clue black on `"light"`, and a shaded cell `"dark"`; the companion pins follow.

## Boundaries & Constraints

**Always:** API level 1 only (27 images against `images_count` 32 and `images_bytes`); `games/sudoku/` stays self-contained (R2); the tool is standard library only, deterministic, and its `--check` passes on the committed PNGs; every tile lies inside offsets 2..49 of its 51 px cell on both axes, so no tile touches a 1 px cell line or a 3 px block line (`board.draw_grid` stays whole); the checker baked in G and H is in phase with the screen's dither where the image lands (`"dark"` is black where canvas x + y is even); snapshots, moves, and state are untouched (R9); a frame stays within 2,048 commands.

**Never:** any change under `src/`, `lib/`, `scripts/`, `docs/crosshatch/api-level-1.txt`, `test/game_script/harness/`, `.github/`, `games/ultimate-tic-tac-toe/`, `games/battleship/`, the epic file, `tickets.toml`, `deferred-work.md`; a font file or non-standard-library import in the tool; a new API; a change to dot notes, the pad, the rail, the menu, HOW TO PLAY, or any other screen beyond the grounds, numerals, tiles, and selection frame this entry names; moving `board.lua` or the solver. Edits to the companion folder stay to this entry's needs: entry 4 edits `rules.lua`, `taps.lua`, and `checks.lua` beside it and the orchestrator resolves the overlap.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Normal cell, digit notes | empty cell outside the SHADE PEERS area (or SHADE PEERS off), marks visible | each mark is `note_g<d>`, except the focused digit's, `note_b<d>`, all drawn `"black"` | none |
| Shaded cell, digit notes | empty cell in the selected cell's row, column, or box, SHADE PEERS on | cell ground `"dark"`; each mark is `note_h<d>` drawn `"black"`, except the focused digit's, `note_h<d>` drawn `"white"` | none |
| Clue | clue cell, shaded or not | black numeral on `"light"`; stays `"light"` inside the shaded area | none |
| Player digit in a shaded cell | own digit, SHADE PEERS on, cell a peer of the selection | black numeral on `"dark"` | none |
| Focused digit's copies | any filled cell holding the focused digit | white numeral on solid black (unchanged), outranking clue and shade | none |
| Every slot | any cell, any k in 1..9 | tile inside offsets 2..49 of the cell on both axes; tile origin x + y even (odd for an inverted H) | the companion pin fails otherwise |
| Selected cell with visible marks | selected cell holding marks | a 2 px black frame at insets 0..1 (the tiles start at inset 2) | none |
| Dot notes | NOTES AS: DOTS | as built (grey rings, solid black focused dot, white paper behind a ring on a grounded cell) | none |

</intent-contract>

## Code Map

New or changed, all under `/home/user/epic-first-party-games-lane-b/`. Work only in that worktree.

- `test/game_script/first_party/sudoku/tools/make_note_images.py` -- today writes 18 images (10 x 14, glyphs 8 x 12 with 2 px strokes, `pixels(digit, grey)`, `png_bytes`, `images()`, `--check`, `--out`); rewrite for 27 images of `IMAGE_W` 12 x `IMAGE_H` 16: a bold glyph 10 x 14 (strokes about 3 px, hand-made ASCII bitmaps in the tool) inside a one pixel margin. Keep `png_chunk`, `png_bytes`, `main`, and the exit contract.
- `games/sudoku/note_g1..9.png`, `note_b1..9.png` (rewritten) and `note_h1..9.png` (new), generated by the tool, committed.
- `games/sudoku/layout.lua` -- pure geometry; add `layout.NOTE_W = 12`, `layout.NOTE_H = 16`, and `layout.note_tile(x, y, k, inverted) -> X0, Y0` for the cell rectangle's corner (x, y) and mark k (1..9).
- `games/sudoku/view.lua` -- `draw_board` grounds (loop around lines 100-115), the numeral colour (line 124), the selection frame (lines 141-148), and `draw_mark` (lines 77-89, which takes slot centres and a `row` nudge for the old 10 x 14 images); dots stay as built.
- `test/game_script/first_party/sudoku/rules.lua` -- `rules.frame` (its `on_image` pin and the `record` double, lines 413-486) follows the three sets; add `rules.look`.
- `test/game_script/first_party/sudoku/rounds/notes-digits.lua` and `rounds/notes-dots.lua` -- call `rules.look(state)` next to `rules.frame`.
- `docs/crosshatch/api-level-1.txt` (`draw ch.gfx.image opaque`, "white" swaps), `lib/GfxRenderer/GfxRenderer.cpp` (`drawPixelDither`: `"light"` black where x and y are both even, `"dark"` where x + y is even, in screen coordinates), `src/games/FrameReplay.cpp` and `src/games/GameViewport.cpp` (a canvas point is drawn at the screen point offset by the viewport origin, which is the board's bezel insets, default left 3 and top 9 in Portrait and 3 and 3 in PortraitInverted: x + y of the origin is even, and the origin is (odd, odd), so `"light"` is black at canvas (odd, odd)), `lib/GameScript/ChBindings.cpp` -- read only.
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-sudoku-screenshots/` -- re-taken frames and one new one (Verification).
- Reference for taking the shots: scratchpad `8.3/drv.py` (a driver kept in scratch by the Sudoku build: packs `games/sudoku`, installs it, reads the dealt grid from `fs_/.games-data/sudoku/resume.bin`, solves it, and taps cells, keys, rail, and menu); never committed.

## Tasks & Acceptance

**Execution:**
- [ ] `test/game_script/first_party/sudoku/tools/make_note_images.py` -- 27 images: tile 12 x 16 px; the glyph (10 x 14, bold) at tile offset (1, 1); with tile pixel (i, j) 0-based, G is black where the glyph is set and (i + j) is even, else white; B is black where the glyph is set, else white; H is black where (i + j) is even outside the glyph and white where the glyph is set (so a bare H is in phase with `"dark"` when the tile origin x + y is even, and inverted by `"white"` it is in phase when the origin x + y is odd); update the docstring (the phase rule, the 27 names, the sizes); keep stdlib only, deterministic, `--check`, `--out` -- R4, R8.
- [ ] `games/sudoku/note_{g,b,h}{1..9}.png` -- run the tool; commit the 27 PNGs (the 18 old names are overwritten) -- R4.
- [ ] `games/sudoku/layout.lua` -- `note_tile(x, y, k, inverted)`: column `(k-1)%3`, row `(k-1)//3`; `X0 = x + 4 + 16*col + a`, `Y0 = y + 2 + 16*row`, `a = (x + y + (inverted and 1 or 0)) % 2`, so X0 + Y0 is even (odd when inverted) in every cell and slot, a tile spans at most offsets 4..48 across and exactly 2..49 down, and the 16 px pitch leaves the tiles abutting vertically and 4 px apart across -- R4.
- [ ] `games/sudoku/view.lua` -- grounds: focused copy `"black"`, else clue `"light"`, else (SHADE PEERS on and a peer of the selection) `"dark"`; numeral `"white"` only on `"black"`; marks as images: set `h` in a `"dark"` cell (the focused digit's drawn `"white"` at `note_tile(..., true)`), else `b` for the focused digit and `g` for the rest (all `"black"`), at `layout.note_tile`; dots unchanged (`paper` when the cell has a ground); the selected cell with visible marks gets `frame(black, 0, 1)` only, any other selected cell keeps its frame; update the file's comments -- R4.
- [ ] `test/game_script/first_party/sudoku/rules.lua` -- `rules.frame`: the image pin accepts `note_[gbh][1-9]`, derives each tile's expected set and colour from the cell (a peer of `ui.sel` takes `h`, `"white"` for the focused digit; otherwise `b` for the focused digit and `g`), and asserts the position equals `layout.note_tile` and lies in offsets 2..49 both ways with the parity rule; extend the `record` double with an optional second callback for `rect` and `text` calls (name the device behaviour it stands in for, and that it is more permissive); add `rules.look(state)`: a state with a player's digit in a peer of the selection and a focused digit with copies, asserting each cell's ground and numeral colour (clue `"light"` with black, shaded `"dark"` with black, focus copies `"black"` with white, others no ground) -- R4.
- [ ] `test/game_script/first_party/sudoku/rounds/notes-digits.lua`, `rounds/notes-dots.lua` -- call `rules.look(state)` beside `rules.frame` -- R4.
- [ ] `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-sudoku-screenshots/` -- re-take `grid-clues`, `grid-digits`, `notes-dots`, `notes-digits`, `shade-remaining`, `clash`, `check-strikes`, `hint` and add `notes-digits-shade.png` (digit notes with a focused digit in normal cells and in a SHADE PEERS row), after the review; look at each against the mocks and at device scale -- R4.

**Acceptance Criteria:**
- Given the finished tree, when `ctest --test-dir build/test -L games-check` runs, then the three games pass, Sudoku's package within `images_count` 32 and `images_bytes`, and `rules.frame` and `rules.look` are green on the four rounds that call them.
- Given the committed PNGs, when `make_note_images.py --check` runs, then it exits 0; given a changed glyph or phase, then it exits 1.
- Given a frame with digit notes, SHADE PEERS on, a selection and a focused digit, when drawn, then each shaded cell's marks are `note_h` (the focused digit's drawn `"white"`), every other cell's are `note_g` or `note_b`, every tile is at `layout.note_tile`, and no tile covers a cell line or block line.
- Given the simulator running the packed game, when the shots listed in Verification are taken, then the checker inside each H tile continues the ground's checker without a visible seam (a pixel check of one shaded row: every non-glyph pixel of an H tile follows the dither phase), the clues read black on 25% grey, and the focused digit's note reads black on grey in a shaded cell.
- Given `check_upstream_touches.py`, `scripts/*_test.py`, and `./bin/clang-format-fix` twice, then all pass with no change.

## Implementation Notes

- The implementation subagent found that 27 images plus `manifest.json` and Sudoku's 8 `.lua` modules (board, grid, help, layout, main, puzzles, solver, view) are 36 package members; `scripts/pack_game.py` (`MAX_MEMBERS`) and the installer (`GameCore::PACKAGE_MEMBERS`) allow 32. The plan counted `images_count` 32 only. The three `GamesCheckTest` cases for `sudoku` fail with "36 members; at most 32". Everything else was verified with both limits raised to 40 temporarily (reverted; nothing under `scripts/` or `lib/` is changed): `games-check` green 20 times, the seven mutants all killed. It needed a decision, which the owner made (next note).
- Resolved by the owner (epic Notes, 2026-10-05, the file-cap Decision): `package_members_count` rises to 64 in entry 10 (lane C), and entry 9 now follows entries 3 and 10; no module is merged and no image dropped. Until entry 10 merges into this lane, the three `GamesCheckTest.*/sudoku_1` tests fail on the member count (expected).
- Epic head `3765a159` (entry 4) was merged into the lane by the orchestrator; `rules.lua` conflicted in four hunks and was resolved so both survive: entry 4's `record` (real `ch.gfx` keys, `on_call`), `watch`, `expect_marks`, and `rules.draw_marks`, with my `rules.frame` tile pins, `peer`, and `rules.look` (its callback is entry 4's `on_call`). Where they disagreed about a ground the greys Decision won: `watch` and `expect_marks` now pin clues `light` and SHADE PEERS cells `dark`. Checked with both member limits raised to 64 temporarily (reverted): `games-check` 85 of 85 green.
- Glyphs: ASCII 10 x 14 bitmaps in `make_note_images.py`, 3 px strokes; looked at as ASCII only, not yet in the simulator.
- A selected cell with visible marks gets `frame(black, 0, 1)` in dots mode too (the plan's frame rule names visible marks, not digit notes).

- Review patches (applied by the build agent; the implementation subagent had finished, so it was not re-engaged): the selection frame of black outlines at insets 0 and 1 is gated on digit notes (`marked[sel] and not ui.dots`), so dot notes keep the frame of their ground as built; `rules.frame` pins the selection frame (new `expect_frame`, reading the outlines `watch` now gathers by cell and inset: black at 0 and 1 and nothing at 2 to 4 for digit notes, the white halo at 2 to 4 for dots and for the unmarked selected cell of `rules.look`), derives "shaded" from `ui.shade` and `ui.sel`, and draws the frame again with SHADE PEERS off and with nothing selected (every mark is then B or G); `layout.note_tile` names its pitch (`NOTE_PITCH`) and its comment is rewritten; `make_note_images.py` checks each glyph is 10 x 14, and `--check` also fails on a stray `note_*.png` and on `layout.lua`'s `NOTE_W, NOTE_H` differing from its image size; comment wraps fixed.
- Shots taken on x4pro after the review: the simulator driver is `scratchpad/8.9/drv.py` (not committed), the pixel check `scratchpad/8.9/phase.py`.

## Plan Change Log

## Review Triage Log

### 2026-10-05 — Review pass
- verdicts: 26 findings — high 0, medium 3, low 21, false 1, maybe-false 1 (the intent-alignment lens's items are descriptive, logged as rows with `reject`)
- findings:
  - blind-hunter 1 `[low]` `[reject]` the package-member limit is missing from the acceptance criteria and the BLOCKED note still reads as open — the fix is an edit of this plan; the cap was raised by entry 10 and the note now says it was resolved.
  - blind-hunter 2 `[low]` `[reject]` verification deliverables (shots, pixel check, results) are absent from the diff and status is in-review with empty logs — they are produced after the review by this workflow's order; present in the final tree.
  - blind-hunter 3 `[medium]` `[patch]` the frame of black outlines at 0 and 1 also applies to dot notes, which the intent says stay as built — real: `marked[sel]` was set for dots too; fixed by `marked[sel] and not ui.dots`, pinned by `expect_frame` (halo expected for dots), mutant `marked[sel]` alone is red.
  - blind-hunter 4 `[low]` `[reject]` a selected cell's frame differs with and without notes — inherent: a halo at insets 2 to 4 would cover the tiles' first rows (the intent puts the tiles inside the grid lines); the shots show both frames readable.
  - blind-hunter 5 `[low]` `[defer]` the phase assumes the viewport origin's x + y is even and nothing guards it — a design assumption of level 1 (a game cannot read the origin); BoardConfig has one default inset set and the pixel check found 0 violations on x4pro; deferred with the device run as the settling check.
  - blind-hunter 6 `[low]` `[patch]` `rules.frame`'s pin restates `layout.note_tile`, ignores `ui.shade` and fails on a nil `ui.sel`, and SHADE PEERS off or no selection is never drawn with notes — fixed: `shaded` reads `ui.shade` and `ui.sel`, and two more draws (shade off, no selection) pin B and G only.
  - blind-hunter 7 `[low]` `[reject]` the acceptance criterion says `rules.look` runs on four rounds while two call it — wording of the plan; `rules.frame` runs in four rounds and `rules.look` in the two notes rounds, which cover both note styles.
  - blind-hunter 8 `[low]` `[patch]` nothing ties the PNG dimensions to `NOTE_W, NOTE_H` and `note_tile` hardcodes its pitch — fixed: `--check` compares `layout.lua`'s constants with `IMAGE_W, IMAGE_H`; `note_tile` uses a named `NOTE_PITCH`.
  - blind-hunter 9 `[low]` `[reject]` the one-pixel nudge also moves G and B tiles — the Decision asks G in phase too, and one rule per cell keeps a cell's tiles aligned; only the focused H deviates, by design.
  - blind-hunter 10 `[low]` `[patch]` the tool does not validate glyph shapes and `--check` ignores stray images — fixed: an assertion on every glyph's 10 x 14 and `--check` fails on a `note_*.png` that is not one of the 27 (checked with a stray file).
  - blind-hunter 11 `[low]` `[patch]` dangling docstring sentence, garbled `layout.lua` comment, ragged `rules.lua` comment, and `draw_mark`'s eight arguments — comments fixed; the argument list is kept (a refactor with no named harm).
  - edge-case-hunter 1 `[low]` `[reject]` the frame's inset 1 overpaints row 49, the bottom tiles' paper margin — true and harmless: the margin is blank paper (G, B) or in-phase ground (H) and the frame is meant to sit on it; glyph rows end at 48.
  - edge-case-hunter 2 `[low]` `[reject]` `note_tile` hardcodes the 51 px cell — the canvas is 474 x 788 on both boards by the API and `board.layout` yields 51; `rules.frame` asserts the bounds against `layout.get().cell`, and the comment now says 51.
  - edge-case-hunter 3 `[low]` `[defer]` same claim as blind-hunter 5 (origin parity unguarded) — one deferred entry.
  - edge-case-hunter 4 `[medium]` `[patch]` no test pins the selection frame; no mutant touches it — fixed with `expect_frame`; mutants with the frame branch removed and with it reaching dots are red.
  - edge-case-hunter 5 `[low]` `[reject]` same claim as blind-hunter 7 (four rounds against two) — wording of the plan.
  - edge-case-hunter 6 `[low]` `[patch]` `on_image` decides "shaded" from `peer` alone (ignores `ui.shade`, nil `ui.sel`) — same root cause as blind-hunter 6, fixed.
  - verification-gap 1 `[low]` `[defer]` no automated test pins the PNGs' baked phase or size — no CI job can run a companion tool (R2); `--check` strengthened (blind-hunter 8, 10), the manual pixel check done; deferred.
  - verification-gap 2 `[medium]` `[patch]` the selected-cell frame is unverified (grep for a frame assertion finds none) — same root cause as edge-case-hunter 4, fixed.
  - verification-gap other `[low]` `[patch]` a comment line far over the file's wrap — wrapped.
  - verification-gap other `[low]` `[reject]` no test draws digit notes with SHADE PEERS off — judged speculative by the lens; done anyway under blind-hunter 6.
  - intent-alignment 1 `[false]` `[reject]` descriptive: phase is pinned in canvas coordinates, never screen — the screen-coordinate check is the simulator pixel check (4,680 ring pixels, 0 violations; moving the origin one row gives 135).
  - intent-alignment 2 `[maybe-false]` `[reject]` descriptive: the simulator verification has not happened — it was not yet run at review; done after the patches (Verification below), so the divergence is closed.
  - intent-alignment 3 `[low]` `[reject]` descriptive: the tests run on a permissive double and nothing tests that an H tile's checker matches the ground — the double's gap is named in the plan; the pixel check covers the match.
  - intent-alignment 4 `[low]` `[reject]` descriptive: the geometry (4 + 16c + a, 2 + 16r) departs from the mock's 3 + 17c, 1 + 16r — a recorded choice in Design Notes (the unknown the ticket leaves to the builder); the shots were looked at against the mocks.
  - intent-alignment 5 `[low]` `[reject]` descriptive: dots' paper logic now fires on a dark ground and that shot was not retaken — retaken (`notes-dots.png`, `shade-remaining.png`); see the residual risk on the focused black dot.

## Design Notes

Sources. The epic Notes Decisions of 2026-10-05 (the three-sets Decision, the greys Decision that amends it, the lane Decision) settle the sets, the grounds, the slot rule, and which grounds each set goes on; the mocks `schemes-now-vs-swap.png` and `block-bold-12x16-aligned.png` show them. The three choices the Decisions leave to the builder, settled here:

1. **Where a tile sits (the unknown: the mock's 3 + 17c, 1 + 16r).** Cells are 51 px; a cell's grid line is its offset 0, and a block line is 3 px centred on a block edge, so it covers offsets 0..1 of a block's left and top edge cells and offset 50 of its right and bottom edge cells, and offsets 2..49 are clear in every cell (the Sudoku build's own pin used the same bound, `rules.frame`: `dx >= 2`, `dx + w <= cell - 1`). Three 16 px rows of tiles exactly fill 2..49, so rows sit at 2 + 16r, not the mock's 1 + 16r (the mock's top row covers the block line's inner pixel). Across there are 12 px to spare, which pays for the phase below.
2. **The phase (the unknown: "matches the screen's dither phase where each image lands").** An image is static; `"dark"` is black where screen x + y is even (`drawPixelDither`), the canvas origin on both boards in Portrait is (3, 9) (viewable insets left 3, top 9; `GameViewport::forRenderer`) and (3, 3) in PortraitInverted, so canvas x + y even is screen x + y even. A 17 px slot pitch would alternate the parity between neighbours, and the 51 px cell pitch alternates it between cells, so a tile's x is nudged by one pixel (`a`) to keep X0 + Y0 even in every cell and slot: one baked phase fits every shaded tile. The focused digit's H is drawn `"white"`, which inverts the checker (bg black where i + j odd), so its tile is nudged the other way (X0 + Y0 odd) and is in phase too; the cost is that this one tile sits one pixel to the side of its neighbours. A game cannot read the origin: the parity rule assumes it (an assumption, recorded, pinned only as the property of the slots, and confirmed in the simulator by a pixel check, which draws through the same viewport code as the device). G needs no phase for legibility (it sits on white) but gets the same one, as the Decision says.
3. **The glyphs (the unknown: bold bitmaps that read at 12 x 16).** The tool draws its own 10 x 14 glyphs with about 3 px strokes in an ASCII table (the mock's DejaVu Sans Bold shows only the weight); the implementer looks at each set at 1x and zoomed in the simulator, G especially (a 3 px stroke carries a 50% checker), and records the shapes that read. A 2 px glyph read faintly in the white-on-grey mock (`white-on-grey-10x14.png`).

The selected-cell frame. Old frames (white halo at insets 2..4 on a dark or black ground, black 1..2 and white 3 otherwise) would cover the top and left edges of the tiles, which now start at inset 2; a cell holding visible marks gets black insets 0..1 only. A selected cell without marks keeps its old frame: its ground is `"dark"` in a shaded area, so it keeps the white halo that read on the 50% ground, as before on `"dark"` clues.

Guards of the rewritten code, each kept: `draw_mark`'s `paper` (a white disc behind a dot ring on a grounded cell, so the ring's inner white shows; the ground is `"dark"` now, not `"light"`, and the Decision keeps dots as built, so the shot is the check), the focused dot's solid black, and the old `row` nudge (replaced by the tile geometry); `draw_board`'s ground priority (focus copy, then clue, then shade, as built with the two greys swapped) and the hidden-marks mask `notes[c] & cand[c]` (untouched); `ground[c]` still feeds the frame test and `paper`. `git log -L` on `draw_mark` shows one commit (`e84e5fa3`, the Sudoku build), with no earlier fix to preserve.

Test double. `rules.frame`'s `record` stands in for the engine's `ch.gfx` (counts one command per call; faults outside draw, an unknown image name, and frame limits are the engine's, not the double's); the new callback hands `rect` and `text` calls to `rules.look`. It is more permissive than the device: it clips nothing, does not look an image up, and does not lay out text. A test pins the image names and placement against the same `layout.note_tile` the game calls, so a drawing of an image that does not exist is caught only by the games check's rounds, which draw the real `ch.gfx`.

R4's text in the epic file ("18 package images of about 10 x 14 px") is superseded by the Decisions; the orchestrator keeps the epic file.

## Verification

**Environment:** `export PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`; work only in `/home/user/epic-first-party-games-lane-b`; AGENTS.md's locks.

**Commands** (host tests and fast checks while implementing; then the review; then the firmware-side step once):
- `python3 test/game_script/first_party/sudoku/tools/make_note_images.py` then `... --check` -- expected: exit 0, 27 PNGs of 12 x 16.
- `cd /home/user/epic-first-party-games-lane-b && flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'`, then `ctest --test-dir build/test --output-on-failure -j` and `ctest --test-dir build/test -L games-check --repeat until-fail:20` -- expected: all pass (the games check over all three games, Sudoku packing within `images_count` 32 and `images_bytes`).
- Mutants, each applied, run, and reverted: clue ground `"dark"` again; shade ground `"light"` again; a shaded cell's marks drawn `note_g`; the focused H drawn `"black"`; the parity nudge `a` removed; a numeral white on `"dark"`; a tile at `Y0 = y + 1` -- expected: each red (`rules.frame` or `rules.look`).
- `python3 scripts/pack_game.py games/sudoku <scratch>` -- expected: packs; record the package size.
- `for t in scripts/*_test.py; do python3 $t; done`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass; nothing changes.
- After the review and its patches, once: `flock /tmp/crosshatch-build.lock sh -c './.claude/skills/run-crosshatch-player/sim.sh build x4pro'`, then the shots. No firmware source changes, so `pio run`, `pio check`, and the other envs are not run (as entries 1 and 3).

**Manual checks:** simulator shots in `story-sudoku-screenshots/`, each looked at against `notes-look-mock/schemes-now-vs-swap.png` (right block) and `block-bold-12x16-aligned.png` and at device scale: `grid-clues.png` (clues black on 25%), `grid-digits.png`, `notes-dots.png`, `notes-digits.png` (G and B in normal cells), `shade-remaining.png`, `clash.png`, `check-strikes.png`, `hint.png`, and the new `notes-digits-shade.png` (digit notes with a focused digit in normal cells and in a SHADE PEERS row). A pixel check on a shaded row of `notes-digits-shade.png` (a scratch script) shows every non-glyph pixel of every H tile follows `"dark"`'s phase. Results are written here after the run.

**Results (build agent, on the final tree after the review patches; method as each line says).**
- Host: `ctest --test-dir build/test -j`: 1,763 of 1,763 pass (the epic head `af31d765`, package member cap 64); `-L games-check --repeat until-fail:20`: passes, 85 tests, 138.6 s for the 20 repeats. No flake is claimed.
- Tool: `make_note_images.py --check` exits 0 (27 PNGs of 12 x 16, `layout.lua`'s `NOTE_W, NOTE_H` agree); with a stray `games/sudoku/note_x1.png` it exits 1 (removed). Glyph preview (the 27 images at 6x, scratch) looked at: bold B numerals, G a checker inside the same glyph, H white on checker.
- Mutants (each applied, rebuilt, run `ctest -R sudoku`, reverted; every one red, 2 failing sudoku tests): clue ground `"dark"`; shade ground `"light"`; the digit-notes frame branch removed (halo kept); the focused H drawn `"black"`; a numeral white on `"dark"`; the frame branch reaching dots; a tile at `Y0 = y + 1`; the parity nudge removed. The implementation subagent's seven earlier mutants (shaded marks as G among them) were all red on the pre-patch tree.
- Package: `pack_game.py games/sudoku <scratch>`: packs, 39,876 B, 36 members (under the cap of 64), hash `e795de05bf033e8f`; the 27 images at 12 x 16 are well under `images_count` 32 and `images_bytes`. `scripts/*_test.py` all pass; `check_upstream_touches.py` PASS (no ledger edit); `./bin/clang-format-fix` twice after `git add`: nothing changed; no formatting-only change outside the paths.
- Simulator: `flock /tmp/crosshatch-build.lock sh -c './.claude/skills/run-crosshatch-player/sim.sh build x4pro'` succeeded (30.9 s, warm cache); the game was packed into `fs_/games/`, installed, and played from a cold start with the driver in scratch; no fault in the run (the only log line with "error" is the simulator's own X shutdown at `stop`).
- H-tile pixel check (scratch `8.9/phase.py`, on `notes-digits-shade.png`, screen coordinates, canvas origin (3, 9) from the viewport): the outer one-pixel ring of every note tile slot (nine per cell, the focused digit's slot at its inverted position) of the ten empty shaded cells but the selected one, 4,680 ring pixels, follows `"dark"`'s phase (black where screen x + y is even): 0 violations. Sensitivity: with the focus slot at the normal alignment 3 violations (one visible 7), with the origin moved one row 135.
- Screenshots (`story-sudoku-screenshots/`, x4pro, Easy, the device's fonts; each looked at, the notes at 3x and 5x crops against `notes-look-mock/schemes-now-vs-swap.png` (right block) and `block-bold-12x16-aligned.png`): `grid-clues.png` (clues black on the 25% ground, the pad's counts); `grid-digits.png` (the player's digits black on white, the focused 7's copies white on black, clues black on 25% grey); `clash.png` (a repeated 8 struck on each cell, the focused 8 black); `check-strikes.png` ("1 wrong digit", the struck 6 among the focused 6's copies); `hint.png` ("Only digit": the selected cell's row, column, and box on the 50% ground, clues keeping their 25% ground, the player's digits black on 50%, a white halo round the selected cell); `notes-dots.png` (dot notes after FILL NOTES, focused 7's dots solid black, as built); `shade-remaining.png` (dot notes with a selected cell: its peers on the 50% ground, rings on white paper, the pad's counts); `notes-digits.png` (digit notes after FILL NOTES, G grey numerals and the focused 7 in B black, tiles inside the grid lines); `notes-digits-shade.png` (the new one: digit notes with a selected cell, H tiles in its row, column, and box blending into the 50% ground with white numerals and the focused 7 black, G and B elsewhere). The notes read at device scale: G is faint (a checker inside the glyph, as the Decision asks) and B and H read clearly.


## Auto Run Result

**Status:** built. One commit on `epic-first-party-games-lane-b`. `review: thorough`, `lenses_ran` all four, `followup_review_recommended: true` (two medium entries were patched: the dots-mode selection frame and the missing frame pin; the patches were checked by mutants and the shots, not by a second lens pass).

**Summary.** Sudoku's digit notes are 27 bold 12 x 16 images in three sets (G grey numeral on white, B black numeral on white, H white numeral on a 50% checker) from `make_note_images.py`'s own glyph bitmaps. A normal cell draws G, and B for the focused digit; a SHADE PEERS cell draws H, the focused digit's H `"white"` (inverted). Tiles sit in offsets 2..49 of the cell, clear of every grid and block line, and `layout.note_tile` nudges each tile one pixel so its baked checker continues the screen's `"dark"` dither (confirmed on the simulator: 0 violations in 4,680 ring pixels). The greys are swapped: a clue is a black numeral on `"light"`, a SHADE PEERS cell `"dark"`, a player's digit in it black, the focused digit's copies white on black. A selected cell holding digit notes gets a two-outline black frame (a halo would cover the tiles' first rows); dot notes keep their frame.

**Files.**
- `test/game_script/first_party/sudoku/tools/make_note_images.py`: 27 images, bold 10 x 14 glyphs, phase rule, stricter `--check`.
- `games/sudoku/note_{g,b,h}{1..9}.png`: 18 rewritten, 9 new.
- `games/sudoku/layout.lua`: `NOTE_W`, `NOTE_H`, `note_tile`.
- `games/sudoku/view.lua`: grounds, numeral colours, `draw_mark`, selection frame.
- `test/game_script/first_party/sudoku/rules.lua`: `rules.frame` tile and frame pins, `rules.look`, `watch` and `expect_marks` for `light` clues and `dark` peers.
- `test/game_script/first_party/sudoku/rounds/notes-digits.lua`, `notes-dots.lua`: call `rules.look`.
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-sudoku-screenshots/`: eight re-taken shots and `notes-digits-shade.png`.
- This plan.

**Review.** 26 findings: patched 2 medium entries and 4 low (the dots-frame gate; the frame pin with the shade-off and no-selection draws; the tool's checks and the named pitch; comment fixes), deferred 2 entries (CI does not run the tool; the origin-parity assumption, to be settled by the device run), rejected the rest with the reasons in the log (plan-text edits, descriptive items, unreachable canvas sizes, harmless margin overpaint, the G and B nudge the Decision asks for).

**Verification.** As in the Verification results above: host suite 1,763 of 1,763 and the games check 20 times; eight mutants red; `sim.sh build x4pro` and the shots; the pixel check; script tests, ledger check, and `./bin/clang-format-fix` twice clean. No `pio run`, `pio check`, or other env was run: no firmware source changed. Formatting: nothing outside this entry's paths changed.

**Residual risks.**
- The origin-parity assumption holds on the x4pro simulator and by reading `BoardConfig` for every board, but the Sticky and the device are not run; entry 5 should look at a SHADE PEERS row with digit notes on both.
- G (a checker inside a 3 px glyph) is faint at device scale, as in the mock; the owner judges it on the panel.
- The focused solid black dot (dot notes) sits on a 50% ground in a SHADE PEERS cell and is low contrast there; dot notes are unchanged as the Decision says, so it is left for the owner to judge (`shade-remaining.png`).
- The focused digit's inverted H tile sits one pixel to the side of its neighbours in a cell (to keep its phase).
- R4's text in the epic file still says 18 images of 10 x 14; the orchestrator keeps the epic file.
- `rules.lua` also has entry 4's lines in the same regions; the merge of the lanes was resolved at the epic head (above).

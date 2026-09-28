---
title: 'The v1 icon set'
type: 'feature'
ticket: '4'
created: '2026-09-28'
status: 'built'
baseline_revision: '068a9ad06cfd46c5af1db81ee576a9542da93c21'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The library holds only the tracer's four icons, so games, the runtime views (entry 5), and the Home tile (entry 6) have no v1 set to draw from, and API level 1 names no full icon list.

**Approach:** Pick 62 names across the six categories (below, each with its reason), vendor each chosen Phosphor 2.1.1 fill or regular SVG byte for byte, draw the two chess pieces Phosphor lacks (pawn, bishop) as originals in the fill style, regenerate the header with the unchanged generator, list the names and attribution in `docs/crosshatch/game-icons.md`, append every `icon` line with `API_SURFACE_CRC`, and page the `icons` fixture through every icon at 32, 64, and 128 px in black and in white.

## Boundaries & Constraints

**Always:**
- AGENTS.md rules; every build, simulator build, and host-test CMake step under `flock /tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/build.lock sh -c '...'`.
- `scripts/gen_game_icons.py` and its test stay byte-unchanged; the header is only ever written by running it.
- Phosphor files are copied byte for byte from the 2.1.1 tarball (sha256 in `names.txt`); originals go under `assets/game-icons/original/fill/`, are `<svg xmlns viewBox="0 0 256 256" fill="currentColor">` with `<path d>` only, and every contour winds clockwise (y down) except holes.
- Generated icon data (every `*_32`, `*_64` array plus `ICONS` and its name strings) stays within 48 KiB (49,152 B), measured with `nm -S` on the x4pro ELF.
- Fill weight for game tokens (suits, dice, pieces, player markers, the hit/miss/dot marks); regular for `mark_x`, `mark_o` (owner's decision), controls, and status icons, which sit beside the device's line-style UI icons.

**Never:**
- No change to the generator, `freeink-sdk`, `.skills/`, the spine, the epic file, `ci.yml`, `GameMatchActivity` (entry 5), `CoverGridHomeUi` (entry 6), or any upstream file (this story touches none).
- No icon from another library; no hand-edited header; no commit of `_bmad/render/`.

## The v1 list (62 names)

| Category | Name | Source (weight) | Reason |
| --- | --- | --- | --- |
| Marks | `mark_x`, `mark_o` | `x`, `circle` (regular) | Noughts and crosses, Ultimate tic-tac-toe (owner's decision, tracer) |
| | `mark_dot` | `dot-outline` (fill) | A small solid dot: last move, a legal-move hint, a board point (Phosphor's fill `dot` is a ring) |
| | `mark_hit`, `mark_miss` | `fire`, `waves` (fill) | Battleship's hit (flame) and miss (a water tile); a drop was too like the flame at 32 px |
| Card suits | `suit_club`, `suit_diamond`, `suit_heart`, `suit_spade` | `club`, `diamond`, `heart`, `spade` (fill) | The four suits (seed section 5 names `suit_spade`) |
| Dice faces | `die_1` … `die_6` | `dice-one` … `dice-six` (fill) | Every face, for dice games and random choices |
| Board pieces | `piece_king` | `crown-cross` (fill) | The chess king's cross-topped crown (seed names `piece_king`) |
| | `piece_queen` | `crown` (fill) | A ball-tipped coronet, like the Staunton queen's |
| | `piece_rook`, `piece_knight` | `castle-turret`, `horse` (fill) | The rook's turret and the knight's horse head |
| | `piece_bishop`, `piece_pawn` | original (fill style) | Phosphor has neither; silhouettes on the rook's base bar |
| | `piece_ship` | `boat` (fill) | Battleship's ship |
| Player markers | `marker_circle`, `marker_square`, `marker_triangle`, `marker_star`, `marker_hexagon` | same names (fill) | Distinct solid tokens for seats and counters; shapes, not shades, since the screen is 1-bit |
| | `player`, `players` | `user`, `users` (fill) | One seat (whose turn); the pass-the-device and nearby prompts |
| Controls | `arrow_left`, `arrow_right`, `arrow_up`, `arrow_down` | `arrow-*` (regular) | Paging and direction (seed names `arrow_left`) |
| | `undo`, `redo` | `arrow-u-up-left`, `arrow-u-up-right` (regular) | Take back and replay a move |
| | `restart` | `arrows-clockwise` (regular) | Play again, new round |
| | `rotate` | `arrow-clockwise` (regular) | Battleship's rotate control (spec fit notes) |
| | `shuffle` | `shuffle` (regular) | Random placement, shuffle a deck |
| | `play`, `pause` | same names (regular) | Resume and pause |
| | `check`, `plus`, `minus` | same names (regular) | Confirm; add and remove, steppers |
| | `info`, `help`, `hint` | `info`, `question`, `lightbulb` (regular) | Rules, help, a hint |
| | `pencil`, `eraser` | `pencil-simple`, `eraser` (regular) | Sudoku's pencil marks and clearing a cell |
| | `show`, `hide` | `eye`, `eye-closed` (regular) | Hidden information: show a hand or board, look away before a pass |
| | `home`, `settings`, `leave`, `trash` | `house`, `gear-six`, `sign-out`, `trash` (regular) | Launcher and menu actions; `leave` matches the runtime's Leave |
| | `timer`, `hourglass` | same names (regular) | `ch.timer` countdowns; waiting for the other player |
| | `game_controller` | `game-controller` (regular) | Entry 6's Home Games tile; line style like the Home tiles' icons |
| Status | `pause` (above), `warning`, `flag_checkered` | `warning`, `flag-checkered` (regular) | Entry 5: pause view `pause`, error view `warning`, game-over view `flag_checkered` (a finish, neutral for a win or a loss) |
| | `trophy`, `smiley`, `smiley_sad` | same names (regular) | A win; a result face either way |

Entry 5 also has `play` (Resume), `leave` (Leave), and `restart` (Play again) for option rows, should it draw them.

Cut to fit 48 KiB: none (62 icons ≈ 42 KB estimated; the measurement is recorded below). Considered and left out: `handshake` (a draw; illegible at 32 px in both weights), `stop`, `lock`, `cards`, a duplicate `close` (`mark_x` is the same glyph).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Regenerate | `python3 scripts/gen_game_icons.py` twice | 62 icons; both runs equal the committed header | none |
| Surface | `api-level-1.txt` icon lines vs `GameIcons::ICONS` | same 62 names both ways; every name draws at each size | `IconsMatchTheList` fails on a mismatch |
| Fixture paging | each tap on the `icons` fixture | next page; after the last black page the white pages; after the last white page back to the first; every page draws only its ink | none |
| Unknown name | `ch.gfx.icon('no_such_icon', …)` | unchanged: `unknown_icon` fault | `CallGuard::raise` |

</frozen-after-approval>

## Code Map

- Never commit (the build agent commits), never run two builds at once, and never commit `_bmad/render/`. Work only in `/home/user/wt-runtime` (branch `epic3/runtime`); scratch under `/tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/3.4/`. Phosphor 2.1.1 is extracted at `<scratch>/3.4/phosphor/package/` (`assets/<weight>/<name>[-fill].svg`, `LICENSE`). Draft originals and a preview tool (`preview.py`, montage of the generator's own raster) are in `<scratch>/3.4/`.
- `assets/game-icons/names.txt` -- the map (`<name> <weight> <path>`); header comments stay, one `#` comment line per category added. `phosphor/{fill,regular}/` hold the tracer's four files.
- `scripts/gen_game_icons.py` -- `SOURCES` labels `original/` paths "original, in Phosphor's <weight> style"; `read_map` sorts by name; unchanged.
- `lib/GameIcons/GameIcons.generated.h` -- regenerated; `GameIcons.h` (`find`, sortedness `static_assert`) unchanged.
- `docs/crosshatch/api-level-1.txt` `# Icons (epic-icon-library)` block (lines 223-227); `lib/GameCore/ApiLevel.h:20` `API_SURFACE_CRC`, whose new value `ApiSurfaceTest.ListLoadsAndMatchesItsCrc` prints.
- `test/game_script/fixtures/icons/main.lua` + `fixtures/README.md` `icons/` row; `test/game_script/LuaGameTest.cpp:125` `TheIconsFixtureDrawsEveryIconAtEachSizeInBothColors` assumes two pages.
- `test/game_script/ApiSurfaceTest.cpp:361` `IconsMatchTheList` -- needs no change.
- `docs/crosshatch/` is a fork path (ledger Game paths); `assets/game-icons` too.
- Baseline already measured at `068a9ad0` (`check_flash_budget.py --metadata-dir <scratch>/3.4/base-meta build on/off/compare`, log `<scratch>/3.4/base-budget.log`): firmware.bin games on 5,833,504 B, off 5,675,280 B, difference +158,224 B; static internal RAM on 101,824 B, off 101,816 B, +8 B. Icon data by `<scratch>/3.4/icon_size.sh` (sums `nm -S -C` sizes of `GameIcons::` symbols in `.pio/build/x4pro/firmware.elf`): 8 arrays 2,560 B + `ICONS` 48 B = 2,608 B. At the commit, run the same three steps with `--metadata-dir <scratch>/3.4/new-meta`, then `icon_size.sh`; the name strings are not separate symbols, so add their bytes (sum of `len(name)+1`) as a computed figure, labelled so.
- `scripts/check_flash_budget.py` -- `build on`, `build off`, `compare` (hidden `--metadata-dir`); x4pro ELF at `.pio/build/x4pro/firmware.elf`; `nm` from the toolchain beside `size`.

## Tasks & Acceptance

**Execution:**
- [ ] `assets/game-icons/phosphor/{fill,regular}/` -- copy each Phosphor file in the list byte for byte.
- [ ] `assets/game-icons/original/fill/{pawn,bishop}.svg` -- the originals from scratch (Design Notes).
- [ ] `assets/game-icons/names.txt` -- 62 lines, grouped by category with a `#` line each; header note that `original/` files are this project's work under its MIT licence.
- [ ] `lib/GameIcons/GameIcons.generated.h` -- run the generator.
- [ ] `docs/crosshatch/game-icons.md` (new) -- attribution (Phosphor 2.1.1, MIT, copyright line, tarball and sha256, `phosphor/LICENSE`; originals), the naming rules, the list table above with reasons, entry 5's and entry 6's names, the cut list, and the measured data size.
- [ ] `docs/crosshatch/api-level-1.txt` -- 62 `icon` lines by category, sorted within each; `lib/GameCore/ApiLevel.h` -- new CRC.
- [ ] `test/game_script/fixtures/icons/main.lua` -- `NAMES` by category; pages of 6: title with ink and page `n/N`, the category, a small row, a medium row on a `light` band, large icons 3 across with names; tap = next page, black pages then white pages, then wrap.
- [ ] `test/game_script/LuaGameTest.cpp` -- the fixture test taps through pages until both inks have drawn every icon at each size (bounded), checking each page's clear color and ink.
- [ ] `test/game_script/fixtures/README.md` -- `icons/` row describes the paging.

**Acceptance Criteria:**
- Given the committed assets, when the generator runs twice, then both outputs equal the committed header (`cmp`).
- Given the `icons` fixture on `simulator_x4pro`, when tapped through every page, then screenshots show every library icon at small, medium, and large in black and in white, and every original and Phosphor icon reads at 32 px.
- Given the x4pro builds, then icon data ≤ 49,152 B by `nm -S`, and the flash and static-RAM deltas (games on minus off) at the baseline and at this commit are recorded.

## Implementation Notes

- The originals are the drafts from `<scratch>/3.4/orig/` unchanged; the generator's contours measure every one clockwise (positive shoelace area, y down) except the bishop's slit (counter-clockwise).
- Fixture: 13 pages per ink (a category split into pages of up to 6: Board pieces, Player markers 2 each, Controls 5), `ui.page` 0..25, black pages first. The test records each ink's pages in order, checks the white pages repeat the black ones, that the tap after the last white page redraws the first black page, one `Clear` per page, and every text and icon in the page's ink; bounded at `4 * ICON_COUNT` pages. Mutations checked by hand: dropping a name from `NAMES` fails it (`smiley_sad size 0 black`), a modulus of `2N + 1` and alternating inks fail it ("the white pages repeat the black pages").
- `api-level-1.txt`: the icon block has a `#` sub-heading per category (comments, outside the CRC). New `API_SURFACE_CRC` `0xBC4A21FF` (printed by `ListLoadsAndMatchesItsCrc`).
- Measured (method in Code Map): icon symbols 124 arrays 39,680 B + `ICONS` 744 B = 40,424 B by `nm -S`; name strings 540 B computed; total 40,964 B of 49,152 B. `check_flash_budget.py` at this tree: games on 5,871,760 B, off 5,675,280 B, +196,480 B (baseline +158,224 B; the set adds 38,256 B); static internal RAM +8 B (baseline +8 B); `objects`: no problems. Log `<scratch>/3.4/new-budget.log`.
- Review pass 1 fixes: the originals' base bar widened to the rook's (`M40,216H216…`, x 32..224), the rest of each file unchanged; `mark_miss` = `waves-fill.svg`, `mark_dot` = `dot-outline-fill.svg` (copied byte for byte; `drop-fill.svg`, `dot-fill.svg` deleted); header regenerated (twice, `cmp`-identical). New `GameIconBlitTest.TheIconDataFitsIn48KiB` (device layout: bitmaps + 12 B per `ICONS` entry + names = 40,964 B <= 49,152). Re-measured (`build on`, `compare`, `icon_size.sh`; log `<scratch>/3.4/new-budget2.log`): unchanged, 5,871,760 B games on, +196,480 B, RAM +8 B, 40,424 B of icon symbols.

## Plan Change Log

- 2026-09-28, review pass 1 (intent_gap, settled by the orchestrator's standing pre-answer: the builder's recommended option when reversible inside the epic PR). Findings: `mark_hit` (`fire`) and `mark_miss` (`drop`) are two solid teardrops that are hard to tell apart at 32 px (`black-01-marks.png`); `mark_dot` (fill `dot`) is a ring, not a dot. Amended in the frozen list: `mark_miss` = fill `waves`, `mark_dot` = fill `dot-outline` (a solid r 28 disc), both still fill weight as the Boundaries require. Known-bad state avoided: a Battleship hit and miss that look alike, and a dot name for a ring. KEEP: everything else in the implementation (the other 60 names, the originals' drawings, the fixture's paging, the test's paging checks, the measurements' method); the originals' base bar is widened as a patch, not redrawn. Listed under Assumed for entry 8.

## Review Triage Log

Pass 1 (2026-09-28). The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as
context-free subagents over `git diff 068a9ad0` (the vendored Phosphor SVGs, the generated header, and BMAD output
listed by `--stat` only). Verdicts: 0 high, 1 medium, 12 low, 5 false, 0 maybe-false. Routes: 2 intent_gap settled by
the orchestrator's standing pre-answer (Plan Change Log), 7 patch, 0 defer, the rest rejected.

| # | Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- | --- |
| 1 | blind, edge | The originals' base bar (x 40..216) is 16 units narrower than `castle-turret`'s (x 32..224), though the docs say they stand on the rook's bar | low | patch: bar is `M40,216H216a8,8,0,0,1,0,16H40a8,8,0,0,1,0-16Z` in both; `white-04-pieces-1.png` retaken |
| 2 | blind | `mark_hit` (`fire`) and `mark_miss` (`drop`) are near-identical teardrops at 32 px | medium | intent_gap, settled reversibly: `mark_miss` = fill `waves`; `black-01-marks.png` retaken; Assumed for entry 8 |
| 3 | blind | `mark_dot` (fill `dot`) is a ring, not a dot | low | intent_gap, settled reversibly: fill `dot-outline` (a solid r 28 disc); Assumed for entry 8 |
| 4 | blind | Pieces are fill only, so chess cannot show an outlined second side; the pieces' baselines differ | low | rejected: the frozen list fixes the pieces as fill; the doc now says so, and outlined pieces go to entry 8 as a question; the baselines are Phosphor's own drawings, vendored byte for byte |
| 5 | blind | The naming rule ("for what they do, not for the picture") contradicts `trash`, `timer`, `trophy` and others | low | patch: rule reworded (action where the picture could mean several, otherwise the picture) |
| 6 | blind | "Names the runtime screens use" is in the present tense, but no screen draws one; suggests `static_assert`s on reserved names | low | patch: the section says entries 5 and 6 will use them; the `static_assert` is rejected: entry 6 names `GAME_CONTROLLER_32` directly (a rename fails its build), and entry 5 owns its call sites |
| 7 | verification-gap, blind | Nothing enforces the 48 KiB icon-data limit | low | patch: `GameIconBlitTest.TheIconDataFitsIn48KiB` (device layout, 40,964 B <= 49,152 B) |
| 8 | blind | No steps for adding an icon; `names.txt`'s header omits "not ending in _" and does not point to the list | low | patch: "Adding or changing an icon" in `game-icons.md`; header line fixed |
| 9 | blind | The fixture was screenshotted on x4pro only; Sticky's canvas could clip the last row | false | the edge-case lens measured both devices' game canvas as 474 x 788; the layout's last text sits at y 526 |
| 10 | blind | The fixture test does not check the per-page split (at most six, one category) | low | rejected: the split is a presentation detail the screenshots show; coverage, ink, order, and wrap are tested |
| 11 | edge | A size byte outside 0..2 would index past `seen[icon]` in the test | low | rejected: `Reader::next` yields only the `TextSize` values the binding wrote (3.1's `static_assert` ties them); a guard for unreachable input |
| 12 | edge, intent | The intent says six categories, the list has seven groups (Status) | low | patch: the doc says Controls and Status together are the epic's common-controls category |
| 13 | intent | Supply side only: no test reaches `drawGameIcon` or `renderer.drawIcon` for the chosen names | false | the frozen Never excludes entries 5 and 6; `IconsMatchTheList` draws every name at each size through `ch.gfx.icon` |
| 14 | intent | The icon block in `api-level-1.txt` is rewritten, not appended | false | no name is removed, the list is compared as a set both ways, and the CRC is recomputed over entry lines only |
| 15 | intent | Nothing committed checks the SVGs against the Phosphor tarball | low | rejected: checked with `cmp` against the 2.1.1 tarball (sha256 in `names.txt`) in Verification; a CI check would need the network and is not in the intent |
| 16 | intent | 128 px is checked only as a size enum | false | 3.1's `GameIconBlitTest` covers the doubled blit; the screenshots show 128 px |
| 17 | intent | The fixture's order is not tested against the doc's order | low | rejected: cosmetic; coverage is tested |
| 18 | intent | Reasons such as `rotate` and `mark_hit` cite games not in this diff | false | the reasons cite `first-party-games.md` (Battleship, Sudoku), the spec the ticket names |

## Design Notes

**Originals** (fill style: solid silhouettes, 8-unit corner radii, the rook's base bar, x 32..224: `M40,216H216a8,8,0,0,1,0,16H40a8,8,0,0,1,0-16Z`): pawn = a 44-radius head, a collar, and a flared body; bishop = an 18-radius ball, a mitre with a diagonal slit (a counter-clockwise contour, so nonzero winding cuts it), a collar, and a body. Both previewed at 32 and 64 px beside `castle-turret` and `horse`.

**Assumed (reversible, for entry 8):** `piece_king` = `crown-cross` and `piece_queen` = `crown` rather than an original queen (fewer originals; the ball-tipped coronet reads as a queen); controls and status in the regular weight; `show`/`hide`, `hint`, `restart`, `rotate`, `leave` named by action; `mark_hit`/`mark_miss` as flame and water tile (`waves`), `mark_dot` as fill `dot-outline`; pages of 6 icons in the fixture.

## Verification

**Commands:**
- `python3 scripts/gen_game_icons.py` then twice more to scratch; `cmp` -- expected: identical, 62 icons.
- `for t in scripts/*_test.py; do python3 "$t"; done` -- all pass.
- Host suite under the lock (AGENTS.md commands) -- expected: all pass, `IconsMatchTheList`, `ListLoadsAndMatchesItsCrc`, and the fixture test included.
- `check_flash_budget.py build on`, `build off`, `compare` at the baseline (already run into `<scratch>/3.4/base-meta`) and at the commit, under the lock; `xtensa-esp32s3-elf-nm -S` over the icon symbols; `pio run -e default` under the lock.
- `python3 scripts/check_layers.py`; `python3 scripts/check_upstream_touches.py`.
- `sim.sh build x4pro` under the lock; place `icons` in `fs_/.games/`; screenshot every page; look at each; copy to `story-icon-set-screenshots/`.
- `./bin/clang-format-fix` twice; `git status` unchanged by the second run.

**Results (implementation, uncommitted tree on `068a9ad0`):**
- Generator: written plus two more runs to scratch, all `cmp`-identical, 62 icons; `gen_game_icons.py`, its test, and `GameIcons.h` unchanged against `068a9ad0`.
- `scripts/*_test.py`: all pass. Host suite: 682/682 pass (`IconsMatchTheList`, `ListLoadsAndMatchesItsCrc`, `SurfaceCrcMatchesTheLists`, `TheIconsFixtureDrawsEveryIconAtEachSizeInBothColors` included).
- `check_flash_budget.py build on`, `build off`, `compare`, `objects` into `<scratch>/3.4/new-meta`: pass (figures in Implementation Notes); `pio run -e default`: SUCCESS; `sim.sh build x4pro`: SUCCESS.
- `check_layers.py`: pass. `check_upstream_touches.py`: PASS (it checks HEAD; no changed path exists in `upstream/develop`).
- Simulator: `icons` in `fs_/.games/`, 27 screenshots (26 pages and the wrap, which is byte-identical to page 1), each looked at; every icon reads at 32 px. Copied to `story-icon-set-screenshots/`: `black-NN-<category>.png` and `white-NN-<category>.png`, NN 01 to 13, one per page (marks, suits, dice, pieces-1, pieces-2, markers-1, markers-2, controls-1 to controls-5, status).
- `./bin/clang-format-fix` twice: the first reflowed one comment in `LuaGameTest.cpp`; the second changed nothing.

**Results after review pass 1 (build agent, 2026-09-28):**
- Generator: two runs to scratch `cmp`-identical to each other and to the committed header, 62 icons; generator, its test, and `GameIcons.h` unchanged against `068a9ad0`.
- Every vendored file under `assets/game-icons/phosphor/` (60 SVGs and `LICENSE`) `cmp`-identical to the 2.1.1 tarball's.
- `scripts/*_test.py`: all pass. `check_layers.py`: passed (351 edges in 88 game files).
- Host suite under the lock: `100% tests passed, 0 tests failed out of 683` (the new `TheIconDataFitsIn48KiB` included).
- `pio run -e default` under the lock: SUCCESS, Flash 5,624,021 B (85.8%), RAM 57,912 B. `x4pro` (games on and off) and `sim.sh build x4pro`: SUCCESS (implementation runs above).
- Flash and static RAM, x4pro, games on minus off by `check_flash_budget.py`: baseline `068a9ad0` +158,224 B flash, +8 B RAM; this set +196,480 B flash, +8 B RAM; the set adds 38,256 B to the games-on image and 0 B of static internal RAM. Epic 2 closed at +150,448 B. Icon data by `nm -S`: 40,424 B of symbols + 540 B of name strings (computed) = 40,964 B of 49,152 B (baseline 2,608 B of symbols). No name cut to fit.
- Screenshots (`story-icon-set-screenshots/`), each looked at: `black-01-marks.png` … `black-13-status.png` show every icon at 32, 64 (on a light band) and 128 px in black on white, a page per category part; `white-01-marks.png` … `white-13-status.png` the same in white on black.

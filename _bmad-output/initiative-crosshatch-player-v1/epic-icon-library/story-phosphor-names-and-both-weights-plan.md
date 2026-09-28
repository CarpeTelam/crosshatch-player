---
title: 'Phosphor names and both weights'
type: 'feature'
ticket: '9'
created: '2026-09-28'
status: done
baseline_revision: '87dae8eabb1812a0e7c0a29c8e6365e536abf41f'
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

**Problem:** The owner's entry-8 answers (epic Notes, `Decision (entry 8...)` lines) replace the 62 category-prefixed,
one-weight names with Phosphor's own names in both weights and drop the chess pieces, so the library, its API lines,
the fixture, the screens that name icons, and the docs all carry names and a signature the owner has rejected.

**Approach:** Rename every icon to its exact Phosphor 2.1.1 name (55 names: the 62 minus the six chess pieces, with
regular and fill `circle` merged into one), vendor each in regular and fill, teach the generator two weights per name
and hyphenated names, add `ch.gfx.icon`'s optional sixth `weight` argument through the display list to the blit, raise
the icon-data cap to 96 KiB, and update the API list, fixture, views, docs, and planning references.

## Boundaries & Constraints

**Always:**
- AGENTS.md rules; every `pio run`, `sim.sh setup`/`build`, host-test CMake configure and build, and
  `check_flash_budget.py build` under `flock <lock> sh -c '...'`.
- Names are exactly Phosphor 2.1.1's SVG stems without `-fill`, hyphens included; the library holds Phosphor icons
  only (`assets/game-icons/original/` goes). Every name ships in `regular` and `fill`, copied byte for byte from the
  tarball (sha256 in `names.txt`).
- The 55 names, by category (docs and fixture order):
  - Marks: `x`, `circle`, `dot-outline`, `fire`, `waves`
  - Card suits: `club`, `diamond`, `heart`, `spade`
  - Dice faces: `dice-one`, `dice-two`, `dice-three`, `dice-four`, `dice-five`, `dice-six`
  - Board pieces: `boat`
  - Player markers: `square`, `triangle`, `star`, `hexagon`, `user`, `users`
  - Controls: `arrow-left`, `arrow-right`, `arrow-up`, `arrow-down`, `arrow-u-up-left`, `arrow-u-up-right`,
    `arrows-clockwise`, `arrow-clockwise`, `shuffle`, `play`, `pause`, `check`, `plus`, `minus`, `info`, `question`,
    `lightbulb`, `pencil-simple`, `eraser`, `eye`, `eye-closed`, `house`, `gear-six`, `sign-out`, `trash`, `timer`,
    `hourglass`, `game-controller`
  - Status: `warning`, `flag-checkered`, `trophy`, `smiley`, `smiley-sad`
- `ch.gfx.icon(name, x, y, size, color, weight?)`: `weight` is `"regular"` (default when absent or nil) or `"fill"`;
  any other value is an ordinary Lua argument error, as a bad size is. The weight rides in the `Icon` op.
- Generated icon data (bitmaps, `ICONS`, name strings) stays within 96 KiB (98,304 B), measured with `nm -S`.
- The views keep the regular weight; `CoverGridHomeUi.cpp` changes only inside its existing `#if FREEINK_CAP_GAMES`
  block, and only if the constant's name must change.

**Never:** no change to `freeink-sdk`, `.skills/`, `ci.yml`, `scripts/check_layers.py`,
`.claude/skills/run-crosshatch-player/`, `API_LEVEL_FROZEN`, another story's `deferred-work.md` section, the epic file,
`tickets.toml`, earlier plans, or the review packet; no icon from outside Phosphor; no hand-edited header; no commit of
`_bmad/render/`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Default weight | `ch.gfx.icon('dice-six', 1, 2, 'small', 'black')` or a nil 6th argument | Icon op with the regular weight | none |
| Fill | `..., 'black', 'fill')` | Icon op with the fill weight; replay draws the fill bitmap | none |
| Bad weight | `'bold'`, `1`, a table | `bad argument #6 to 'icon' (invalid option 'bold')` / `string expected` | Lua argument error (catchable) |
| Old name | `ch.gfx.icon('mark_x', ...)` | unknown icon fault, as any unknown name | `CallGuard::raise` |
| Byte limit | 9 bytes left in the list; then exactly 10 | icon refused, list unchanged; then accepted, list full | `frameFull` |
| Generator: a weight missing, a weight twice | `x regular ...` only; `x fill` twice | exit 1 naming the line or name | `Failure` |
| Generator: not Phosphor's path or name | `cross regular phosphor/regular/x.svg`, `x_y`, `x--y`, `x-`, a name ending `-fill`, an `original/` path, weight `bold` | exit 1 | `Failure` |

</frozen-after-approval>

## Code Map

- Work only in `/home/user/wt-weights` (branch `epic3/weights`, submodules initialised). Lock:
  `/tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/build.lock`; scratch
  `<scratchpad>/3.9/`; Phosphor 2.1.1 extracted at `<scratchpad>/3.9/phosphor/package/assets/{regular,fill}/`
  (`<name>.svg`, `<name>-fill.svg`; all 110 files exist). Toolchain installed; do not reinstall or touch certificates.
  Do not commit (the build agent commits).
- `assets/game-icons/names.txt` -- `<name> <weight> <path>`; header comments name the tarball, licence, grammar.
- `scripts/gen_game_icons.py` -- `SOURCES` (drop `original`), `WEIGHTS` (becomes `('regular', 'fill')`, index order),
  `NAME`, `read_map` (keyed by name today), `header_text` (struct `Icon {name, small, medium}`, per-name
  `<UPPER>_32/_64`, `ICONS`), docstring. `scripts/gen_game_icons_test.py` -- `GOOD_MAP`, `MainTest` (copies `x.svg`,
  `heart-fill.svg`; `test_an_original_icon_is_labelled_as_one`; rule cases; bad-SVG cases write `phosphor/bad.svg`).
- `lib/GameIcons/GameIcons.h` -- hand-written `find`, sortedness `static_assert`, comment naming `game_controller`.
- `lib/GameScript/DisplayList.{h,cpp}` -- `Op::Icon` layout `op, color, size, x, y, icon` (`ICON_BYTES` 9),
  `appendIcon`, `Reader::next`, `DrawCommand`. `ChBindings.h` `SIZE_NAMES` pattern; `ChBindings.cpp` `gfxIcon`,
  `SIZE_VALUES` + `static_assert` pattern.
- `src/games/GameIconBlit.h` `sourceFor`; `GameIconDraw.{h,cpp}` `drawGameIcon`, `drawGameIconAt`;
  `FrameReplay.cpp:154` `case Op::Icon`; `GameViewIcons.h` `forView`/`forOption` names;
  `GameMatchActivity.cpp:537,551` call `drawGameIcon` (no change needed).
- `src/components/CoverGridHomeUi.cpp:277` names `GameIcons::GAME_CONTROLLER_32`; ledger row 9 names it too.
- `lib/GameCore/Manifest.{h,cpp}` `validIcon` (`[a-z0-9_]{1,32}`, "a library icon name"); `ManifestTest.cpp:200`.
- `test/game_core/ApiLevelList.h:60` icon grammar `[a-z0-9_]{1,32}`; `lib/GameCore/ApiLevel.h:20` `API_SURFACE_CRC`
  (`ApiSurfaceTest.ListLoadsAndMatchesItsCrc` prints the new value).
- Tests: `test/game_script/GfxBindingsTest.cpp` (`describe` :70, decode test :112-149, outside-draw :192, bad
  arguments :350-358, unknown/sanitizing :391-455), `DisplayListTest.cpp` (`RoundTripsIcons` :91,
  `AnImagePastTheByteLimitIsRefused` :184 as the byte-limit pattern), `GameIconBlitTest.cpp` (names :25-110,
  `SourceFor...` :136, `TheMarkOIsARing...` :217, `TheIconDataFitsIn48KiB` :240), `ApiSurfaceTest.cpp`
  (`GfxOptionsMatchTheList` :320, `IconsMatchTheList` :362), `LuaGameTest.cpp:131` fixture test,
  `GameViewIconsTest.cpp:83-89`.
- `test/game_script/fixtures/icons/main.lua` (pages of 6, one weight), `fixtures/README.md:31` `icons/` row.
- Docs: `docs/crosshatch/api-level-1.txt` (:34 grammar comment, :68 `fn`, enums :190-209, icon block :237-306),
  `game-icons.md` (whole), `game-canvas.md:90-92`. Planning: `game-api-seed.md` :44, :151, :161-163, :256;
  `ARCHITECTURE-SPINE.md` AD-24 :370-373, naming row :394, tree comment :497; `architecture-walkthrough.html:440-470`
  (six example icons labelled `mark_x` … `piece_pawn`). `first-party-games.md` names no icon (nothing to change).
- Measurement: entry 7 measured +196,336 B flash and +776 B static RAM (games on minus off, x4pro, fresh tree of
  `965c55d7`); HEAD only added docs since. `<scratchpad>/3.4/icon_size.sh` shows the `nm -S -C` sum over
  `GameIcons::` symbols (point it at this worktree's ELF).

## Tasks & Acceptance

**Execution:**
- [ ] `assets/game-icons/` -- delete `original/` and the four chess SVGs; copy every missing regular and fill SVG of
  the 55 names byte for byte; rewrite `names.txt`: two lines per name (`regular` then `fill`), grouped by category
  with `#` lines; header says the names are Phosphor's own, both weights, Phosphor only.
- [ ] `scripts/gen_game_icons.py` -- names `[a-z][a-z0-9-]{0,31}`, no `--`, no final `-`, not ending in `-fill`;
  weights `regular`, `fill`; each path must be `phosphor/regular/<name>.svg` or `phosphor/fill/<name>-fill.svg`; each
  name exactly once per weight and in both; header per Design Notes; docstring updated.
- [ ] `scripts/gen_game_icons_test.py` -- good map of two names in both weights (header shape, sort, identifiers);
  every Matrix generator row; drop the original-label test; keep ring, byte-identical, and exit-code tests.
- [ ] `lib/GameIcons/GameIcons.generated.h` -- run the generator (never typed). `GameIcons.h` -- comment names
  `game-controller` and the weight arrays; `find` unchanged.
- [ ] `lib/GameScript/DisplayList.{h,cpp}` -- `enum class IconWeight : uint8_t { Regular, Fill }`,
  `DrawCommand::weight`, `appendIcon(x, y, icon, size, color, weight)`, layout `... size u8, weight u8, x y i16,
  icon u16` (10 B), decode. `ChBindings.{h,cpp}` -- `WEIGHT_NAMES = {"regular", "fill", nullptr}` with values and
  `static_assert`; `gfxIcon` reads arg 6 with `luaL_checkoption(L, 6, "regular", WEIGHT_NAMES)`.
- [ ] `src/games/GameIconBlit.h` -- `sourceFor(index, drawnPixels, GameIcons::Weight, Source&)` (false for a weight
  past `WEIGHT_COUNT`). `GameIconDraw.{h,cpp}` -- `drawGameIconAt(..., bool black, bool fill)`,
  `drawGameIcon(..., bool black, bool fill = false)`. `FrameReplay.cpp` -- pass `command.weight == IconWeight::Fill`;
  `static_assert` that `IconWeight::Fill` + 1 == `GameIcons::WEIGHT_COUNT` and the order matches.
- [ ] `src/games/GameViewIcons.h` -- `pause`, `flag-checkered`, `warning`; `play`, `sign-out`, `arrows-clockwise`.
- [ ] `lib/GameCore/Manifest.{h,cpp}` -- `validIcon` also accepts `-`; comment; `ManifestTest` parses
  `"icon": "game-controller"`.
- [ ] `test/game_core/ApiLevelList.h` -- icon grammar `[a-z][a-z0-9-]{0,31}`; `ApiLevelTest` rejects `icon a_b`.
- [ ] `docs/crosshatch/api-level-1.txt` -- grammar comment; `fn ch.gfx.icon(name, x, y, size, color, weight?)`;
  `enum weight regular`, `enum weight fill` after the refresh enums; 55 `icon` lines by category, sorted within each.
  `lib/GameCore/ApiLevel.h` -- new CRC.
- [ ] Tests: `GfxBindingsTest` -- new names; `describe` adds the weight; decode test covers default, nil,
  `'regular'`, `'fill'`; bad-argument rows for `'bold'`, `1`, a table at #6; `mark_x` is now unknown.
  `DisplayListTest` -- round trip both weights, 10 B, and `AnIconPastTheByteLimitIsRefused` (9 left refused, 10 fits
  and fills). `GameIconBlitTest` -- new names in `find`'s hits and misses (`dice_six`, `dice-sixx`, `x-`, `mark_x`);
  every bitmap of both weights through `inkAt`; `sourceFor` both weights and a bad weight; regular `circle` a ring,
  fill `circle` inked at the centre; `TheIconDataFitsIn96KiB` (device layout: 4 bitmaps a name, a 20 B `ICONS`
  entry, names; limit 98,304). `ApiSurfaceTest` -- weight in `GfxOptionsMatchTheList`; `IconsMatchTheList` draws
  every listed name at each size in each weight and checks both sets. `GameViewIconsTest` -- new names.
- [ ] `test/game_script/fixtures/icons/main.lua` -- Design Notes layout; `LuaGameTest` fixture test records
  (icon, size, weight) and requires every icon at every size in both weights in both inks, other checks kept.
  `fixtures/README.md` -- `icons/` row.
- [ ] `docs/crosshatch/game-icons.md` -- rewrite: attribution (Phosphor 2.1.1, MIT, copyright, tarball and sha256,
  `phosphor/LICENSE`, `https://phosphoricons.com`), both weights and the `weight` argument, naming rules (Phosphor's
  name, look it up on the site; identifiers `-`→`_`, fill adds `_FILL`), one table row per name (category, name,
  why it is in the set), screen uses, adding an icon, Size (96 KiB cap, measured figures, flash and RAM table with a
  column for this story). `game-canvas.md` -- view icon names.
- [ ] Planning references -- `game-api-seed.md` section 5 (`fn` row, Icons paragraph with Phosphor names and weight,
  example `x`/`circle`, manifest example `"icon": "x"`); spine AD-24 (Phosphor names, both weights, `weight`
  argument, Phosphor only, a manifest icon may use `-`) with an "amended 2026-09-28 (owner, entry 8)" marker, naming
  row :394, tree comment :497; walkthrough's six example labels and aria-label (`x`, `circle`, `dice-five`, `heart`,
  `star` replacing the pawn glyph with a star, `arrow-left`).

**Acceptance Criteria:**
- Given the committed assets, when the generator runs twice to scratch, then both equal the committed header (`cmp`),
  55 names, 110 bitmaps per size.
- Given the `icons` fixture on `simulator_x4pro`, when tapped through every page, then screenshots show every icon at
  32, 64 and 128 px in regular and fill, in black and in white.
- Given the tracer game and `f-lua-error` on `simulator_x4pro` and `simulator_sticky`, then the pause, game-over and
  error views show `pause`, `flag-checkered`, `warning` and the row icons; the cover-grid Home shows the Games tab.
- Given the x4pro builds, then icon data ≤ 98,304 B by `nm -S`, and `check_flash_budget.py` games on minus off is
  recorded beside entry 7's +196,336 B / +776 B.

## Implementation Notes

- The implementation ran as a subagent from this plan; the build agent triaged the review and sent the patches back to it.
- Generator: `read_map` keys entries by weight, then name; a path must equal `phosphor_path(name, weight)`, so the
  `original/` source and its label are gone; `identifier()` maps `-` to `_` and adds `_FILL` for the fill weight.
  `generate` prints `55 icons, 2 weights each`. The sidecar gained a check of the committed map (55 names, Phosphor
  paths only, both weights, no `original/`).
- All 110 SVGs and `LICENSE` under `assets/game-icons/phosphor/` are `cmp`-identical to the 2.1.1 tarball's files
  (build agent's check, 0 of 110 differ).
- `CoverGridHomeUi.cpp` is unchanged: `GAME_CONTROLLER_32` is still the regular 32 px `game-controller` bitmap.
- `GameMatchActivity.cpp` is unchanged: it draws the names `GameViewIcons.h` returns (`pause`, `flag-checkered`,
  `warning`; `play`, `sign-out`, `arrows-clockwise`) in the regular weight.
- Beyond the plan: `ManifestCheckTest` uses `"x"`; a `'Fill'` bad-argument row and a catchable bad-weight case under
  `pcall`. Review patches: the Phosphor-site wording (only listed names exist), the manifest comment, no default
  weight on `appendIcon`, a stray `printf` removed, `GameIcons.h`'s comment reflowed.
- `API_SURFACE_CRC` is `0xCF6FE64E`.
- Screenshots were renamed by the build agent to say what they show (see Verification).

## Plan Change Log

## Review Triage Log

Pass 1 (2026-09-28). The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as
context-free subagents, launched together, over `git diff 87dae8ea` with the generated header, vendored SVGs,
screenshots and BMAD plan files listed by `--stat` only (`scratchpad/3.9/review-diff-1.patch`). Verdicts: 0 high,
1 medium, 11 low, 2 false, 0 maybe-false; intent-alignment is descriptive. Routes: 5 patch (sent to the implementation
subagent), 1 defer, the rest rejected.

| # | Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- | --- |
| 1 | blind | Docs send authors to phosphoricons.com, which lists ~1,500 icons; only 55 exist, and an unknown name stops the game | medium | patch: `game-api-seed.md` section 5, `game-icons.md` naming rules and step 1, and the `names.txt` header say only the listed names exist; the site is for previewing one |
| 2 | blind, edge, intent | The manifest `icon` check accepts `_`, a leading, trailing or doubled `-`, and a leading digit, none of which a library name has; its comment says library names use `_` | low | patch: comment only (why `-` is accepted, why `_` stays: nothing that parsed before fails, and the name is not checked against the library here). The widen-only grammar is the plan's reversible choice; a syntax check cannot prove a name exists, which the launcher's handoff covers |
| 3 | blind, intent | `ApiLevelList.h`'s icon regex allows `--`, a trailing `-` and `-fill`, looser than the generator | low | rejected: unlikely to be met, since `IconsMatchTheList` requires every listed name to be a library name, which the generator gates; a tighter regex is more than a direct correction |
| 4 | blind | Weight passed as a `bool` beside `bool black`, round-tripped from `IconWeight` | low | rejected: the plan's design keeps `GameIconDraw.h` free of `lib/GameIcons` for screens; no wrong call exists |
| 5 | blind | `appendIcon`'s default weight can hide a dropped argument; the plan gives an explicit parameter | low | patch: default removed, the one test call passes `IconWeight::Regular` |
| 6 | blind | The cap doubled to 96 KiB with no recorded decision | false | the epic Notes' `Decision (entry 8, flash option A)` sets 96 KiB (98,304 B) |
| 7 | blind | Entry 9's flash figures come from the working tree, not an archive of a commit | low | rejected: this story changes no CI gate, so the fresh-tree rule does not apply; the doc says how it was measured, and the review patches changed no generated code |
| 8 | blind | CI never runs `gen_game_icons_test.py` | false | the `Fork script tests` job runs every `scripts/*_test.py` (entry 7's F7) |
| 9 | blind | No policy for a Phosphor bump that renames a vendored icon | low | rejected: the pin is 2.1.1 and a frozen level never renames; a bump is a later decision with its own rules |
| 10 | blind | `names.txt` repeats the weight and path the generator can derive | low | rejected: R1 asks for a map recording each name's file and weight; the generator checks them |
| 11 | blind | A `std::printf` was added to the size test | low | patch: deleted, with its include |
| 12 | blind | `GameIcons.h`'s comment left badly wrapped | low | patch: reflowed by hand |
| 13 | edge | `ApiSurfaceTest` indexes `WEIGHT_NAMES` by a decoded weight without a bound check | low | rejected: the reader yields only weights the binding wrote (as 3.4's size finding); a guard for unreachable input |
| 14 | verification-gap | The replay step (`FrameReplay` → `drawGameIconAt`'s `fill`) is untested; an inverted flag passes every host test | low | defer: no host test builds `FrameReplay.cpp` or `GameIconDraw.cpp` (3.1's replay gap, deferred to retro AI-2's harness); the `icons` screenshots show both weights drawn through the replay; `deferred-work.md` `## 3.9` |
| 15 | intent | The epic's R1, R3, R4, Done when 1 and `tickets.toml` still say `snake_case`, original additions and the five-argument call | -- | rejected here: the orchestrator owns the epic file and `tickets.toml` (build brief), and the plan's Never lists them; raised in the final report |
| 16 | intent | Byte-for-byte provenance of the vendored SVGs is not tested | -- | checked: all 110 SVGs and `LICENSE` `cmp`-identical to the 2.1.1 tarball (sha256 in `names.txt`); a CI check would need the network |

Also seen by the build agent in the screenshots, not patched: on Controls page 3 the labels `arrows-clockwise` and
`arrow-clockwise` touch in the large area (cosmetic; the icons are clear).

## Design Notes

**Header.** The generator fixes the weight order (regular 0, fill 1) and writes, after the sizes:

```cpp
enum class Weight : uint8_t { Regular, Fill };  // indexes Icon::small and Icon::medium
inline constexpr size_t WEIGHT_COUNT = 2;
struct Icon { const char* name; const uint8_t* small[WEIGHT_COUNT]; const uint8_t* medium[WEIGHT_COUNT]; };
// dice-six: Phosphor 2.1.1 regular, phosphor/regular/dice-six.svg
inline constexpr uint8_t DICE_SIX_32[SMALL_BYTES] = {...};   // and DICE_SIX_64
// dice-six: Phosphor 2.1.1 fill, phosphor/fill/dice-six-fill.svg
inline constexpr uint8_t DICE_SIX_FILL_32[SMALL_BYTES] = {...};  // and DICE_SIX_FILL_64
inline constexpr Icon ICONS[] = {{"dice-six", {DICE_SIX_32, DICE_SIX_FILL_32}, {DICE_SIX_64, DICE_SIX_FILL_64}}, ...};
```

Identifiers mirror Phosphor's file stems (`game-controller.svg` → `GAME_CONTROLLER_32`, `-fill.svg` → `_FILL_`), so
the Home tab's `GAME_CONTROLLER_32` still resolves to the same regular bitmap and `CoverGridHomeUi.cpp` is unchanged.
Names hold no `_` and never end in `-fill`, so the mapping is one to one.

**Fixture.** Three names a page (a category split into parts), each shown in both weights: the small row and the
medium row (on the light band) hold six cells, each name's regular then fill; the large area holds two rows of three
at the current positions, regular above and fill below, the name under the fill icon; the heading names the category,
part, and "regular, fill". Black pages, then white pages, then wrap, as today (21 pages an ink).

**Assumed (reversible, for entry 8):** fill identifiers take `_FILL` and regular ones none, so the Home tile is
untouched; `names.txt` keeps three columns with two lines per name and the generator checks each path is Phosphor's
own for the name; `enum weight regular|fill` lines join `api-level-1.txt` like `size`; a manifest `icon` accepts `-`
(AD-24 lets a manifest name a library icon); `circle` is listed once, under Marks, and `boat` alone is Board pieces;
`drawGameIcon` gains `fill = false` and the views stay regular; the fixture shows three names a page, both weights
side by side; the walkthrough's pawn example becomes `star`.

## Verification

**Commands:**
- `python3 scripts/gen_game_icons.py`, then twice to scratch and `cmp` -- identical, 55 icons.
- `for t in scripts/*_test.py; do python3 "$t" || echo FAILED $t; done` -- all pass.
- Host suite under the lock (AGENTS.md commands) -- all pass, `IconsMatchTheList`, the weight tests,
  `ListLoadsAndMatchesItsCrc`, the fixture test, `TheIconDataFitsIn96KiB` included.
- The `Icons up to date` job's commands (generate to a scratch path, `cmp` with the committed header).
- `pio run -e x4pro`, `-e sticky`, `-e default` under the lock -- SUCCESS.
- `check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` under
  the lock; the `nm -S -C` sum of `GameIcons::` symbols plus the computed name strings.
- `python3 scripts/check_layers.py`; `python3 scripts/check_upstream_touches.py` (on a `git stash create` commit
  before committing) -- pass.
- Simulator under the lock: `sim.sh build x4pro` and `sticky`; `icons`, `tracer`, `f-lua-error` in `fs_/.games/`;
  screenshot every `icons` page, the pause, game-over and error views on both, and the cover-grid Home on both; look
  at each; copy to `story-names-weights-screenshots/`.
- `./bin/clang-format-fix` twice; the second run changes nothing.

**Results (2026-09-28, the tree of this story's commit):**
- Generator: written, then two runs to scratch, all `cmp`-identical; `55 icons, 2 weights each`. This is the
  `Icons up to date` job's command pair (generate to a scratch path, `cmp` with the committed header).
- Every `scripts/*_test.py` passes (the generator's sidecar included, with its committed-map check).
- Host suite under the lock: `100% tests passed, 0 tests failed out of 699` after the review patches
  (`IconsMatchTheList` in both weights, the weight rows of `GfxBindingsTest` and `GfxOptionsMatchTheList`,
  `AnIconPastTheByteLimitIsRefused`, `ListLoadsAndMatchesItsCrc` with `0xCF6FE64E`, the fixture test,
  `TheIconDataFitsIn96KiB`, `TheFillCircleIsInkedAtTheCentre`).
- `pio run` under the lock after the review patches: `x4pro` SUCCESS (Flash 5,897,774 B, RAM 101,824 B, the same as
  before the patches), `sticky` SUCCESS (Flash 5,785,515 B), `default` SUCCESS (Flash 5,624,181 B, RAM 57,912 B).
- Icon data by `xtensa-esp32s3-elf-nm -S -C` on the x4pro ELF: 220 bitmaps 70,400 B + `ICONS` 1,100 B = 71,500 B of
  symbols, plus 475 B of name strings (computed, `len(name) + 1`) = 71,975 B of the 98,304 B cap (26,329 B spare);
  entry 7's set was 40,964 B.
- Flash and static RAM, x4pro games on minus off, `check_flash_budget.py build on`, `build off`,
  `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` under the lock, built from an empty `.pio` in this
  worktree (the review patches after it changed only comments, a default argument and tests; x4pro's size is
  byte-identical): on 5,902,784 B, off 5,675,376 B, **+227,408 B** (entry 7: +196,336 B; this story +31,072 B:
  +31,011 B of icon data and 61 B of code), 28,592 B under the 250 KiB gate. Static internal RAM **+776 B**
  (IRAM +684 B, `.iram0.text_end` +84 B, `.dram0.bss` +8 B), unchanged from entry 7, 248 B under the gate.
  `objects`: no problems.
- `check_layers.py`: 358 edges in 90 game files, passed. `check_upstream_touches.py` on the commit: PASS (trial merge of `upstream/develop` clean).
- All 110 SVGs and `LICENSE` `cmp`-identical to the 2.1.1 tarball.
- Simulator (`sim.sh build x4pro` and `sticky`, `icons`, `tracer`, `f-lua-error` in `fs_/.games/`), each shot looked
  at, in `story-names-weights-screenshots/`:
  - `x4pro-icons/black-01-marks-1.png` … `black-21-status-2.png`: every icon, three names a page, regular and fill
    side by side at 32 px, 64 px (on a light band) and 128 px (regular above, fill below), black on white.
  - `x4pro-icons/white-01-…` … `white-21-status-2.png`: the same pages in white on black;
    `wrap-back-to-black-01.png`: the tap after the last white page.
  - `x4pro-sheet-black-a.png`, `-black-b.png`, `-white-a.png`, `-white-b.png`: contact sheets of those 42 pages.
  - `{x4pro,sticky}-pause.png`, `-game-over.png`, `-error.png`: `pause` with `play`/`sign-out`, `flag-checkered`
    with `arrows-clockwise`/`sign-out`, `warning` with `sign-out`; `x4pro-sheet-tracer.png` the tracer before,
    paused and over; `{x4pro,sticky}-pause-over-icons.png` the pause view over the white fixture page.
  - `{x4pro,sticky}-cover-grid-home.png`: the cover-grid Home with the `game-controller` Games tab before Settings.
  - `sticky-icons-black-01.png`, `sticky-icons-white-01.png`, `sticky-sheet.png`: the fixture's first page in both
    inks and the views on sticky (sticky shot only page 1 of each ink; its canvas matches x4pro's).

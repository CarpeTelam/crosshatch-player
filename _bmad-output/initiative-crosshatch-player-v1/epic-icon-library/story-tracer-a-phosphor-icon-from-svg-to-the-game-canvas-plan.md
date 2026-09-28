---
title: 'Tracer: a Phosphor icon from SVG to the game canvas'
type: 'feature'
ticket: '1'
created: '2026-09-28'
status: 'built'
baseline_revision: '1eacdc7739605047280dfd25d1956789ddfce204'
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

**Problem:** `lib/GameIcons` is a stub (`libraryName()`), so games have no icon library and no `ch.gfx.icon`; nothing proves a Phosphor SVG can become a 1-bit bitmap on the game canvas with the standard library alone.

**Approach:** Vendor four Phosphor 2.1.1 SVGs (`mark_x` = regular `x`, `mark_o` = regular `circle`, `suit_heart` = fill `heart`, `die_6` = fill `dice-six`) with the licence and a name map; a standard-library generator rasterizes them at 32 and 64 px into a committed `GameIcons.generated.h` in `GfxRenderer::drawIcon`'s layout; `ch.gfx.icon` appends an `Icon` display-list command that `FrameReplay` draws through a pure, host-tested blit in `src/games`; an unknown name stops the game through `CallGuard::raise`.

## Boundaries & Constraints

**Always:**
- AGENTS.md rules (C3-safe, `LOG_*`, no bare `new`, locals < 256 B, whole-file `#if FREEINK_CAP_GAMES` on every `src/games/*.cpp`); AD-2: icon data is `inline constexpr` in a header, never `static`; every build, simulator build, and host-test CMake step runs under `flock /tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/build.lock sh -c '...'`.
- Generator: Python 3.11 standard library only, `fork_common` exit contract (0 written, 1 a rule broken: bad map line, bad name, duplicate, unsupported SVG content, an icon that renders empty; 2 could not run: missing map or SVG), single quotes, byte-identical output on every run.
- Bitmap layout (epic Notes): square, 1 bpp, MSB-first, rows padded to whole bytes, bit 0 = ink, stored rotated 90° counter-clockwise, so stored `(row, col)` is drawn at `(px-1-row, col)` exactly as `GfxRenderer::drawIcon` maps it.
- `ch.gfx.icon(name, x, y, size, color)`: top-left at `x, y`; `size` from `SIZE_NAMES` (small 32, medium 64, large 128 = the 64 px bitmap doubled); `color` white or black only; one command within 2,048 commands / 32 KB; ink pixels only, background left as drawn; clipped to the canvas.
- Screens reach icons only through `drawGameIcon` in `src/games`, whose signature has no `GameScript` type; `lib/GameIcons` includes only the standard library.

**Never:**
- No change to `freeink-sdk`, `.skills/`, the spine, `ci.yml`, or any upstream file outside the ledger/allowlist (`.gitignore` and `AGENTS.md` are allowlisted); no CI job (entry 3); no docs attribution or name list in `docs/crosshatch/` (entry 4); no views or Home tile (entries 5, 6); no `rsvg-convert`, Pillow, or other non-stdlib tool.
- Never hand-edit `GameIcons.generated.h`; never commit (the build agent commits).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Draw each size and color | `ch.gfx.icon('die_6', x, y, s, c)` in draw, each s, c ∈ {white, black} | one Icon command with the icon's index, size, color, x, y; replay fills only ink pixels at 32/64/128 px | none |
| Large | size `large` | each 64 px bitmap pixel becomes a 2×2 block | none |
| Clipped | icon partly or wholly off the canvas | only the on-canvas part is drawn; nothing when wholly off; rows and columns walked are bounded by the canvas | none |
| Unknown name | `'no_such_icon'`, also inside `pcall` | game stops (`Fault::Binding`), frame not published, message `<chunk>:<line>: ch.gfx.icon: unknown icon "no_such_icon"` | `CallGuard::raise` |
| Odd unknown name | over 32 bytes, or with control bytes or `"` | message shows at most the first 32 bytes (cut at a UTF-8 boundary), each control byte or `"` as `?` | `CallGuard::raise` |
| Bad arguments | non-string name, `'light'`/`'dark'`, `'huge'` size, non-integer x | ordinary Lua argument error (catchable) | `luaL_arg*` |
| Outside draw / full frame | icon in setup, input, status; 2,049th command | `ch.gfx.icon called outside draw`; `frame is full (...)` | existing guard faults |
| Generator: ring | outer contour one way, inner the other (Phosphor `circle`) | centre empty, ring inked (nonzero winding); same-direction inner contour stays filled | none |
| Generator: bad input | `<rect>`, `transform=`, `fill-rule=`, unknown path command, bad name | exit 1 naming the file or line | `Failure` |
| Generator: missing input | no map file, map names a missing SVG | exit 2 | `SetupError` |

</frozen-after-approval>

## Code Map

- The project root is the git worktree `/home/user/wt-runtime` (branch `epic3/runtime`, submodules initialised): work, build, and run the simulator only there, never in `/home/user/crosshatch-player` or another worktree. Scratch files go under `/tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/3.1/`. The toolchain (`pio`, clang-format 21) is installed; do not reinstall it or touch certificates.
- `lib/GameIcons/GameIcons.{h,cpp}` -- stub; delete `GameIcons.cpp`, rewrite `GameIcons.h`. `lib/lua/.clang-format` (`DisableFormat: true`) is the precedent for the new `lib/GameIcons/.clang-format`, which also leaves hand-written `GameIcons.h` unformatted: write it in repo style by hand.
- `test/game_script/GameScriptTest.cpp` -- only `LinksLibraries`; delete the file and its entry in `test/game_script/CMakeLists.txt` (also drop `lib/GameIcons/GameIcons.cpp` from the source list; `lib/GameIcons` stays an include dir; `src/games` is already one, so header-only `src/games` code is host-testable, as `GameTouch.h` is).
- `lib/GameScript/DisplayList.{h,cpp}` -- `Op`, `DrawCommand`, packed layout comment, `append*`, `Reader::next` switch; `ChBindings.cpp` -- `drawTarget`, `frameFull`, `checkInkColor`, `checkSize`, `utf8Cut`, `GFX_FUNCTIONS`; `CallGuard::raise` prefixes chunk:line from `lua_getstack(L, 1)` and caps text at 128 B.
- `src/games/FrameReplay.cpp` -- `draw()` switch over ops with viewport origin `ox, oy`, canvas `width, height`, `renderer.fillRect(x, y, w, h, bool black)`; `lib/GfxRenderer/GfxRenderer.cpp:1377` -- `drawIcon`'s mapping (reference only, unchanged).
- `scripts/fork_common.py` -- `Failure`, `SetupError`, `exit_code`; `freeink-sdk/libs/assets/Icons/tools/gen_icons.py` and `scripts/convert_icon.py` -- packing reference (the latter rotates CCW like `drawIcon` expects).
- `docs/crosshatch/api-level-1.txt` -- add after `fn ch.gfx.text(...)`: `fn ch.gfx.icon(name, x, y, size, color)`; new `# Icons (epic-icon-library)` section before `# Limits` with `icon die_6`, `icon mark_o`, `icon mark_x`, `icon suit_heart`; add "an unknown icon name" to the pcall comment's fault list. `lib/GameCore/ApiLevel.h` `API_SURFACE_CRC` -- `ApiSurfaceTest.ListLoadsAndMatchesItsCrc` prints the new value. Grammar already has `icon <name>` (`[a-z0-9_]{1,32}`, `test/game_core/ApiLevelList.h`).
- `test/game_script/ApiSurfaceTest.cpp` -- `listed("icon", true)`, `expectSameNames`; top comment says the icon table is checked elsewhere; `ChTableMatchesTheList` picks up `ch.gfx.icon` from `fixtures/surface` automatically.
- `test/game_script/SessionGameTest.cpp:232` `EveryFaultScriptEndsWithTheReadmesText` -- reads `fixtures/README.md` `## Fault scripts` rows (`script`, step, text) and requires a row per `faults/*.lua`.
- `test/game_script/GfxBindingsTest.cpp` -- `describe()` switch, `EveryCallAndArgumentFormDecodes`, `EveryGfxCallOutsideDrawIsAScriptError`, `BadArgumentsAreScriptErrors`, `AFullFrameUnderPcallStillStopsTheGameUnpublished` patterns; `DisplayListTest.cpp`; `LuaGameTest.cpp` + `LuaGameFixture.h` (`readFixture`, `DirectGame`, canvas 480×800).
- `test/game_script/fixtures/gallery/` -- fixture shape (manifest, `ui` table persists between draws, `input` returns nil).
- `docs/crosshatch/upstream-touches.md` Game paths -- has `scripts/gen_game_icons.py`, lacks the test; `.gitignore:12` `*.generated.h`; `AGENTS.md:17` generated-files line.
- Phosphor 2.1.1: `https://registry.npmjs.org/@phosphor-icons/core/-/core-2.1.1.tgz` (sha256 `313332be6190b724da24107addd781799b48bf76b13963f24501112ffe1baadd`), already extracted at `/tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/3.1/phosphor/package/` (`LICENSE`, `assets/regular/{x,circle}.svg`, `assets/fill/{heart-fill,dice-six-fill}.svg`). All 3,024 fill/regular SVGs: one `<svg viewBox="0 0 256 256" fill="currentColor">` of `<path d>` only; commands M L H V C S Q T A Z in both cases (T twice), arc args comma-separated.

## Tasks & Acceptance

**Execution:**
- [ ] `assets/game-icons/` -- copy byte-for-byte `phosphor/LICENSE`, `phosphor/regular/x.svg`, `phosphor/regular/circle.svg`, `phosphor/fill/heart-fill.svg`, `phosphor/fill/dice-six-fill.svg`; write `names.txt`: `#` comments (source package, version, tarball URL and sha256, MIT licence file, "names are part of the game API level"), then one line per icon `<name> <weight> <path relative to assets/game-icons>`, whitespace-separated -- R1's name map.
- [ ] `scripts/gen_game_icons.py` -- see Design Notes; `--assets DIR` (default `<repo>/assets/game-icons`) and `--out PATH` (default `<repo>/lib/GameIcons/GameIcons.generated.h`); module docstring gives usage and exit codes.
- [ ] `scripts/gen_game_icons_test.py` -- unittest, temp dirs only: square path pixel-exact; relative forms equal absolute ones; H/V, C/S, Q/T, A parsed (a circle from two arcs is symmetric); ring hole vs same-direction fill; one displayed ink pixel lands at stored row `px-1-x`, col `y`, bit 0; two `main()` runs byte-identical; exit 0/1/2 for each matrix generator row, and 1 for an icon rendering empty.
- [ ] `lib/GameIcons/GameIcons.generated.h` -- generated by running the script (never typed).
- [ ] `lib/GameIcons/.clang-format` (`DisableFormat: true`); `lib/GameIcons/GameIcons.h` -- includes the generated header; `constexpr int find(const char* name, size_t length)` (binary search over sorted `ICONS`, index or -1, no NUL needed); `static_assert` that `ICONS` is non-empty and strictly sorted; delete `GameIcons.cpp`.
- [ ] `.gitignore` -- `!lib/GameIcons/GameIcons.generated.h` after `*.generated.h`. `AGENTS.md` -- line 17: `*.generated.h` from `src/**/*.html` and `.js` are regenerated by every `pio run`; `lib/GameIcons/GameIcons.generated.h` is committed and regenerated with `python3 scripts/gen_game_icons.py`. `docs/crosshatch/upstream-touches.md` -- add `scripts/gen_game_icons_test.py` under Game paths.
- [ ] `lib/GameScript/DisplayList.{h,cpp}` -- `Op::Icon` (append last); `DrawCommand::icon` (`uint16_t`); layout `Icon op u8, color u8, size u8, x y i16, icon u16`; `bool appendIcon(int64_t x, int64_t y, uint16_t icon, TextSize size, Color color)` with clamp16 and `reserve`; decode in `Reader::next`.
- [ ] `lib/GameScript/ChBindings.cpp` -- `gfxIcon` registered as `icon`: `drawTarget(L, "icon")`, `luaL_checklstring(1)`, x, y integers, `checkSize(4)`, `checkInkColor(5)`, then `GameIcons::find`; unknown → sanitized name (matrix rows 4-5) into a local buffer, `guard->raise(L, message)`; full → `frameFull`; `static_assert(GameIcons::ICON_COUNT <= UINT16_MAX)`.
- [ ] `src/games/GameIconBlit.h` (pure, header-only, includes only `<GameIcons.h>` and std) -- `DRAWN_PIXELS[] = {32, 64, 128}` (small, medium, large); `struct Source { const uint8_t* bitmap; int pixels; int scale; }`; `bool sourceFor(size_t index, int drawnPixels, Source&)` (false for an index ≥ `ICON_COUNT` or another size); `bool inkAt(bitmap, pixels, x, y)` (drawIcon's mapping); `template <typename Fn> void inkRuns(const Source&, int32_t left, int32_t top, int32_t width, int32_t height, Fn&& fn)` -- `fn(y, x, w)` per horizontal ink run, clipped to `[0,width)×[0,height)`, walking only visible rows and columns.
- [ ] `src/games/GameIconDraw.{h,cpp}` -- `bool drawGameIcon(const GfxRenderer&, const char* name, int x, int y, int pixels, bool black)` (screen coordinates, clipped to `getScreenWidth/Height`; unknown name or size → `LOG_ERR`, false) and `void drawGameIconAt(const GfxRenderer&, size_t index, int pixels, int originX, int originY, int width, int height, int x, int y, bool black)` (the canvas form both share: runs from `inkRuns` filled with `renderer.fillRect(originX + x, originY + y, w, 1, black)`); `.cpp` whole-file guarded.
- [ ] `src/games/FrameReplay.cpp` -- `case Op::Icon`: `drawGameIconAt(renderer, command.icon, DRAWN_PIXELS[size], ox, oy, width, height, command.x, command.y, command.color == Color::Black)`.
- [ ] Tests: `test/game_script/GameIconBlitTest.cpp` (new, in CMake) -- `find` hits every name, misses unknown, prefix, longer, and embedded-NUL names; `inkAt` equals drawIcon's `(size-1-row, col)` mapping on a one-bit bitmap; scale 2 gives 2×2 blocks; runs merge adjacent ink; clipping at each edge and wholly off; real `mark_o` 64 px has an empty centre and inked ring; `sourceFor` rejects bad index and 48 px. `DisplayListTest` -- Icon round trip, 9 bytes, counts one command. `GfxBindingsTest` -- icon in the decode test (each size, both colors, index of the name), outside-draw list, bad arguments, unknown name direct and under `pcall` (Binding fault, unpublished frame, exact message), long/odd name sanitizing. `ApiSurfaceTest.IconsMatchTheList` -- `GameIcons::ICONS` names vs `listed("icon", true)` both ways, and every listed name draws Ok through `ch.gfx.icon` at each size; update the top comment. `LuaGameTest` -- the `icons` fixture's black page (first draw) and white page (after a tap) draw every `ICONS` index at each size in that color.
- [ ] `test/game_script/fixtures/faults/unknown_icon.lua` -- complete game (setup/status/apply) whose draw calls `ch.gfx.icon('no_such_icon', 0, 0, 'small', 'black')` inside `pcall(function() ... end)`; `fixtures/README.md` -- its `## Fault scripts` row (`the first draw, inside pcall`, the exact text), and an `icons/` row in `## Games`.
- [ ] `test/game_script/fixtures/icons/` -- `manifest.json` (id `icons`, solo, 1 seat); `main.lua`: a `NAMES` list of the four; page black (white canvas, black icons) or white (black canvas, white icons), a tap toggles it (`ui`); per page a title, a small row, a medium row over a `light` band (shows the background left as drawn), and the large icons in a 2×2 grid, laid out from `ch.screen`.
- [ ] `docs/crosshatch/api-level-1.txt`, `lib/GameCore/ApiLevel.h` -- as the Code Map says.

**Acceptance Criteria:**
- Given the four vendored SVGs, when `python3 scripts/gen_game_icons.py --out X` runs twice, then both outputs equal the committed header byte for byte.
- Given the `icons` fixture in `fs_/.games/icons/` on `simulator_x4pro`, when it opens and after one tap, then screenshots show all four icons at 32, 64, and 128 px in black and in white, every die pip and heart point and the 2 px regular lines visible at 32 px.
- Given `faults/unknown_icon.lua` as `f-unknown-icon`, when opened in the simulator, then the error view shows the README's text.

## Implementation Notes

- `THRESHOLD` stays 0.5: the 32 px render keeps every die pip (3 x 3 px holes), the heart's point, and the 2-3 px
  lines of `mark_x` and `mark_o` (ASCII dump of the generator's grids; screenshots below confirm).
- Arc segment count is `ceil(|dtheta| / (pi/32) - 1e-9)`: the small allowance keeps a half turn at exactly 32
  segments however `atan2` rounds pi, so a last-bit libm difference is very unlikely to change the vertex count.
- The source label in the header's per-icon comments comes from the map path's top folder (`SOURCES`):
  `phosphor/...` is `Phosphor 2.1.1 <weight>`, `original/...` is `original, in Phosphor's <weight> style` (entry 4's
  originals), and any other folder is exit 1. The header carries one line with Phosphor's licence notice. `names.txt`
  comments carry the package, tarball URL, and sha256 for people.
- Names are `[a-z][a-z0-9_]{0,31}` without `__` and not ending in `_` (a subset of the API grammar), so each
  uppercased name plus `_32`/`_64` is a valid, unreserved C++ identifier. Weights are Phosphor's six; a map path must be relative, inside the assets
  folder, and end in `.svg`.
- `ch.gfx.icon` takes its name with `luaL_checklstring`, so a number is converted to a string and then fails as an
  unknown name (Binding fault); a table or nil is the ordinary argument error. Tested both ways.
- Samples use integer vertex coordinates (1/4096 px) and Python's correctly rounded int/int division for crossings,
  so the fill involves no libm call. Arc flattening does use `math.sin`, `cos`, and `atan2`, which are not correctly
  rounded; rounding vertices to 1/4096 px makes a platform's last-bit difference very unlikely (not impossible) to
  move a pixel. A non-finite number in path data is exit 1; an arc whose radius squared underflows, or whose
  endpoints differ by too little to place a centre, is drawn as a straight line.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-09-28). The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as
context-free subagents over `git diff 1eacdc77` (BMAD output excluded). Verdicts: 0 high, 1 medium, 10 low, 9 false or
rejected, 0 maybe-false. Routes: 11 patch, 1 defer, the rest rejected.

| # | Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- | --- |
| 1 | blind, edge | A name ending in `_` yields `NAME__32`, a reserved identifier | low | patch: `read_map` rejects a trailing `_`; test case added |
| 2 | edge | `1e999` in a path is inf, then an `OverflowError` traceback | low | patch: non-finite numbers are a `Failure` naming the file |
| 3 | edge | A subnormal arc endpoint gives `ZeroDivisionError` | low | patch: zero denominator draws a straight line |
| 4 | blind | Every header comment says "Phosphor 2.1.1", so entry 4's originals (added with the unchanged generator) would be mislabelled | medium | patch: label from the map path (`phosphor/` or `original/`, anything else exit 1) |
| 5 | blind | The generated header carries no MIT notice | low | patch: one generated comment line naming the copyright and `phosphor/LICENSE` |
| 6 | blind | "libm differences cannot move a pixel" overstates the guarantee; arcs use sin/cos/atan2 | low | patch: comment, docstring, and Implementation Notes reworded |
| 7 | blind | The sidecar misses exit codes for a non-UTF-8 map, absolute and backslash paths, and an unwritable `--out` | low | patch: cases added (fork-scripts.md asks for every outcome) |
| 8 | blind | Comment above `drawTarget` still says "Both gfx faults" | low | patch: names the three guard-raised gfx faults |
| 9 | blind, edge | `DRAWN_PIXELS[command.size]` has no tie to `TextSize` | low | patch: `static_assert` in `FrameReplay.cpp`; the reader only yields sizes the binding wrote, as for `TEXT_FONT_IDS` |
| 10 | blind | `drawGameIcon` checks the size twice with two log wordings | low | patch: `drawGameIconAt` returns bool; one check |
| 11 | blind | `GamesBuildAnchor.cpp` says every env "compiles lib/GameIcons", now header-only | low | patch: comment reworded |
| 12 | verification-gap, edge | Nothing checks the committed header against a fresh generator run | rejected | out of scope by the frozen intent (Never: "no CI job (entry 3)"); entry 3's `Icons up to date` job is that check; today's header matches a fresh run (`cmp`) |
| 13 | verification-gap | `FrameReplay`'s `Op::Icon` wiring and `drawGameIconAt` are checked only by simulator screenshots | low | defer: `FrameReplay` has no host harness for any op (retro AI-2 stays with epic-install-and-launcher); the screenshots cover it; `deferred-work.md` `## 3.1` |
| 14 | edge | Invalid UTF-8 or C1 bytes in an unknown name reach the error view | rejected | low and unlikely (a game passing binary garbage as an icon name); replacing every byte >= 0x7F would also mangle valid UTF-8 names |
| 15 | edge | `drawGameIcon(nullptr, ...)` crashes in `strlen` | rejected | callers are screens passing literals; no caller can pass null today; a guard adds a branch for an unreachable input |
| 16 | blind | `lib/GameIcons/.clang-format` also exempts hand-written `GameIcons.h` | rejected | the ticket requires `lib/GameIcons/.clang-format` (`DisableFormat`); the plan's Code Map records the trade-off |
| 17 | blind | `detail::compare` could be `std::string_view::compare` | rejected | style only; the hand-written compare is pinned by tests and has no defect |
| 18 | blind | No compile-time-checked overload for screen names and sizes | rejected | new public surface for entries 5 and 6 to add if they want it |
| 19 | blind | `generate()` writes non-atomically and rewrites identical bytes | rejected | low, not met in everyday use; the fix adds temp-file handling |
| 20 | blind, intent | `drawGameIcon` has no caller | false | the ticket asks for it now for entry 5 and the launcher rows |
| 21 | intent | Proof "on the game canvas" is not exercised end to end; large (128 px) and `drawGameIcon` exceed the intent | false | the simulator screenshots in Verification show SVG to canvas pixels; 128 px and `drawGameIcon` are in the ticket's description and the frozen Boundaries |

## Design Notes

**Rasterizer** (the unknown: legibility at 32 px). Parse with `xml.etree.ElementTree`: root `svg` in the SVG namespace, `viewBox` `0 0 N N`, `fill` absent or `currentColor`; children only `path` with only `d`; anything else is `Failure`. Tokenize numbers with `[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?`, arc flags as one `0`/`1` char; implicit command repeats, `M`→`L` after the first pair; `S`/`T` reflect the previous control point per the SVG spec; `Z` returns to the subpath start. Flatten cubics and quadratics to 32 segments, arcs by SVG's endpoint-to-centre conversion (radii scaled up when too small, zero radius → line) at `ceil(|Δθ| / (π/32))` segments. Scale to `px / N`, then round every vertex to a multiple of 1/4096 px so later float math is exact-input and libm differences cannot move a pixel. Coverage: 16 sample lines per pixel row at `y + (k + 0.5)/16`; per line collect edge crossings (half-open in y, direction ±1), sort by x, and add each nonzero-winding interval's overlap with pixel columns `/16`. Ink when coverage ≥ `THRESHOLD`, a named constant starting at 0.5; lower it only if the 32 px screenshot drops a pip, a heart point, or a 2 px line, and record the value and why in Implementation Notes.

**Header shape** (generated text is fixed in the script):

```cpp
// Generated by scripts/gen_game_icons.py from assets/game-icons/names.txt; never edit by hand.
#pragma once
#include <cstddef>
#include <cstdint>
namespace GameIcons {
inline constexpr int SMALL_PIXELS = 32;  // + MEDIUM_PIXELS 64, SMALL_BYTES 128, MEDIUM_BYTES 512
struct Icon { const char* name; const uint8_t* small; const uint8_t* medium; };
// die_6: Phosphor 2.1.1 fill, phosphor/fill/dice-six-fill.svg
inline constexpr uint8_t DIE_6_32[SMALL_BYTES] = {0x.., ... 16 per line};
inline constexpr Icon ICONS[] = {{"die_6", DIE_6_32, DIE_6_64}, ...};  // sorted by name, bytewise
inline constexpr size_t ICON_COUNT = sizeof(ICONS) / sizeof(ICONS[0]);
}
```

Per-name arrays (`GAME_CONTROLLER_32` later) are what entry 6 passes to `renderer.drawIcon`. The display list stores the index, not the name: it is transient and built by the same firmware.

**Assumed (reversible, for entry 8):** the name map is `assets/game-icons/names.txt` (whitespace columns) rather than JSON/CSV; the lookup lives in hand-written `GameIcons.h` over generated data; the unknown-name text is `ch.gfx.icon: unknown icon "<name>"`; the fixture toggles black/white pages on tap because four 128 px icons do not fit one 480 px row; the threshold value is recorded after the screenshot check.

## Verification

**Commands:**
- `python3 scripts/gen_game_icons_test.py`; `for t in scripts/*_test.py; do python3 "$t" || echo FAILED $t; done` -- expected: all pass.
- `python3 scripts/gen_game_icons.py --out /tmp/…/a.h && python3 scripts/gen_game_icons.py --out /tmp/…/b.h && cmp a.h b.h && cmp a.h lib/GameIcons/GameIcons.generated.h` -- expected: identical.
- `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass, including the new tests and `EveryFaultScriptEndsWithTheReadmesText`.
- `flock <lock> sh -c 'pio run -e x4pro && pio run -e sticky && pio run -e default'` -- expected: success; record the icon data's flash from `xtensa-esp32s3-elf-nm -S --size-sort` on the x4pro ELF (symbols in `GameIcons`), total flash delta "unmeasured" (entry 4 measures).
- `python3 scripts/check_layers.py`; `python3 scripts/check_upstream_touches.py` -- expected: exit 0.
- `flock <lock> sh -c '.claude/skills/run-crosshatch-player/sim.sh build x4pro'`, then place `icons` and `f-unknown-icon` in `fs_/.games/` and screenshot: icons black page, icons white page, unknown-icon error view; look at each; copy to `_bmad-output/initiative-crosshatch-player-v1/epic-icon-library/story-icon-tracer-screenshots/` (`icons-black.png`, `icons-white.png`, `unknown-icon-error.png`).
- `./bin/clang-format-fix` twice -- expected: `git status` unchanged by the second run.

**Results (2026-09-28, after the review patches):**
- Every `scripts/*_test.py` passes; `gen_game_icons_test.py` 23/23 (ring hole, same-direction fill, both source labels, two `main()` runs byte-identical, every exit code).
- Two generator runs to scratch files are identical to each other and to the committed header (`cmp`).
- Host suite under the lock: `100% tests passed, 0 tests failed out of 654`, including `GameIconBlitTest.*`, `DisplayListTest.RoundTripsIcons`, the new `GfxBindingsTest` cases (unknown name direct and under `pcall`: `Fault::Binding`, frame unpublished), `ApiSurfaceTest.IconsMatchTheList`, `ListLoadsAndMatchesItsCrc` (`API_SURFACE_CRC 0xEEE4AEDF`), `LuaGameTest.TheIconsFixtureDrawsEveryIconAtEachSizeInBothColors`, and `SessionGameTest.EveryFaultScriptEndsWithTheReadmesText` (`unknown_icon`).
- `check_layers.py`: passed (339 edges in 85 game files). `check_upstream_touches.py`: passed on the commit.
- `pio run -e x4pro`: SUCCESS, Flash 5,825,166 B (88.9%), RAM 101,824 B (31.1%). `sticky` and `default`: not built; both stop before compiling, when the ESP-IDF venv setup's `uv` fails on pypi.org with `invalid peer certificate: UnknownIssuer` while `SSL_CERT_FILE` points at the penv certifi bundle (`uv` with `/root/.ccr/ca-bundle.crt` resolves; environment, not this change; the brief forbids touching certificates).
- Icon data flash, measured with `xtensa-esp32s3-elf-nm -S` on the x4pro ELF: 8 bitmaps (4 x 128 B + 4 x 512 B) plus `ICONS` (48 B) = 2,608 B in flash rodata; code `gfxIcon` 322 B, `appendIcon` 158 B, `drawGameIconAt` 539 B. The x4pro games-on minus games-off delta is unmeasured (entry 4 measures it).
- Simulator (`sim.sh build x4pro`, fixtures placed in `fs_/.games/icons/` and `fs_/.games/f-unknown-icon/`), each screenshot looked at:
  - `story-icon-tracer-screenshots/icons-black.png`: black page, the four icons at 32 px, 64 px (over a `light` band, background left as drawn), and 128 px; every pip, the heart's point, and the 2 px lines show at 32 px.
  - `story-icon-tracer-screenshots/icons-white.png`: after one tap, the same in white on black.
  - `story-icon-tracer-screenshots/unknown-icon-error.png`: the error view with `main.lua:10: ch.gfx.icon: unknown icon "no_such_icon"`, the README's text.
- `./bin/clang-format-fix` twice: the second run changed nothing; it re-laid-out only `GFX_FUNCTIONS` in `ChBindings.cpp` (a file this story edits).

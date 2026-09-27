---
title: 'The full ch.gfx and display-list limits'
type: 'feature'
ticket: '9'
created: '2026-09-27'
status: done
baseline_revision: 'cda0f3ccec509dacbac9ec2590ec0fc01e991936'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `ch.gfx` has only `clear`, `rect`, and `text` in white or black; `line`, `circle`, `refresh`, `align`, the `light`/`dark` fills, `ch.screen`, and `ch.text_width` are missing, and no host test decodes each argument form or hits each limit.

**Approach:** Extend the `DisplayList` with line and circle commands, a text `align` byte, and a per-frame refresh hint; make the bindings check colors (four for fills, two otherwise), sizes, alignments, and modes; expose `ch.screen` and `ch.text_width` from a `Canvas` port (canvas size plus per-size advance tables) passed in at VM start, which entry 11 fills with real font tables.

## Boundaries & Constraints

**Always:**
- `api-level-1.txt` is the contract: signatures, enums (`color`, `size`, `align`, `refresh`), `frame_commands_count 2048`, `frame_bytes 32768`; no entry changes, so `API_SURFACE_CRC` stays.
- Every `ch.gfx` error, including outside `draw`, is a Lua error raised inside the trampoline (a `ScriptError`), never a crash; `ch.text_width` works in every callback and at load.
- `GameScript` stays free of HAL/Arduino/`GfxRenderer`/`Logging`; the port carries plain tables, and the stand-in is deterministic. Game statics `constexpr`; locals under 256 B.
- Fixtures only in `test/game_script/fixtures/`.

**Never:** drawing lines, circles, dithered fills, alignment, or size-to-font mapping in `FrameReplay`, refresh escalation, or the hint fold in `FrameBuffers::publish` (entry 11); `ch.timer`/`store`/`time`/`log`/`api` (entry 10); icons/images; editing `lib/lua`, `ci.yml`, `.skills/`, or the submodule pointer.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Fills | `clear`, filled `rect`/`circle` with any of the four colors | command decoded with that color | — |
| Ink | `line`, outline `rect`/`circle`, `text` in `light`/`dark` | — | `ScriptError` "bad argument #n ... (\"light\" and \"dark\" are only for fills)" |
| Text | 3 sizes; `align` omitted, `left`, `center`, `right` | size and align decoded (omitted = left) | bad size/align: "invalid option" |
| Refresh | none, `refresh()`, `"half"`, `"full"`, several calls | frame hint fast / fast / half / full, the max of the frame's calls; not a command | bad mode: "invalid option" |
| Outside draw | any `ch.gfx` call at load, in `setup`, `status`, `apply`, `input` | — | "ch.gfx.<fn> called outside draw" |
| Limits | 2,049th command; commands past 32,768 B | nothing appended | "frame is full (at most 2048 drawing calls or 32768 bytes)"; frame not published |
| Screen | `ch.screen.w`, `.h` | the canvas size passed at VM start | — |
| Width | `ch.text_width(s, size)` in `input` (and anywhere) | Σ per-glyph pixel advances; code points outside the table use its fallback; stops at a NUL; bad UTF-8 counts as U+FFFD | bad size: "invalid option" |

</frozen-after-approval>

## Code Map

- `lib/GameScript/DisplayList.{h,cpp}` -- packed commands, `Op`, `Color`, `TextSize`, `DrawCommand`, `Reader`; add ops, `Align`, `Refresh`, hint.
- `lib/GameScript/ChBindings.{h,cpp}` -- `BindingContext`, `GFX_FUNCTIONS`, `outsideDraw`, `frameFull`, `openChLibrary` (builds `ch`).
- `lib/GameScript/LuaGame.{h,cpp}` -- constructor, `load()` sets `bindings.*`; `abandon()` resets `bindings`.
- `lib/Utf8/Utf8.h` `utf8NextCodepoint` -- the renderer's decoder (bad bytes → `REPLACEMENT_GLYPH`); reuse so width and drawing decode alike.
- `lib/EpdFont/EpdFontData.h` -- `EpdGlyph::advanceX` (12.4, stride `sizeof(EpdGlyph)`), `EpdUnicodeInterval{first,last,offset}`, `fp4::toPixel`: the shape entry 11 hands over.
- `src/games/FrameReplay.{h,cpp}` -- switch over `Op` (must stay exhaustive); `src/games/GameVM.{h,cpp}` `create`, constructor; `src/activities/games/GameMatchActivity.cpp` `onEnter` (viewport, then `GameVM::create`); `src/games/GameViewport.h` `width()`/`height()`.
- `test/game_script/{LuaGameFixture.h,DisplayListTest.cpp,CMakeLists.txt}`, and 44 `LuaGame`/`DirectGame` construction sites in the suites.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameScript/TextMetrics.{h,cpp}` -- `AdvanceRange`, `AdvanceTable`, `TextMetrics{tables[3]}` with `width(text, size)` and constexpr `standIn()`.
- [x] `lib/GameScript/DisplayList.{h,cpp}` -- `Op::Line`, `Op::Circle`, `Align`, `Refresh`; `appendLine`, `appendCircle`, `appendText(..., align)`; `requestRefresh`/`refresh()` (max; `clear()` resets).
- [x] `lib/GameScript/ChBindings.{h,cpp}` -- `Canvas{width, height, text}`, `BindingContext::canvas`; `line`, `circle`, `refresh`, color/size/align checks, `ch.screen`, `ch.text_width`.
- [x] `lib/GameScript/LuaGame.{h,cpp}` -- constructor takes a `Canvas` (copied); `load()` points the context at it.
- [x] `src/games/FrameReplay.{h,cpp}`, `GameVM.{h,cpp}`, `src/activities/games/GameMatchActivity.cpp` -- `FrameReplay::textMetrics()` (stand-in until entry 11), `GameVM::create(assets, canvas)`, the match builds the canvas from the viewport; replay skips line and circle.
- [x] `test/game_script/{TextMetricsTest,GfxBindingsTest}.cpp`, `DisplayListTest.cpp`, `LuaGameFixture.h`, `CMakeLists.txt` (plus `lib/Utf8/Utf8.cpp`), construction sites -- every matrix row.

**Acceptance Criteria:**
- Given `ctest`, then every suite passes; given `pio run -e x4pro` and `-e default`, then both build; `pio check` finds no defects.
- Given `sim.sh build x4pro` with the tracer in `fs_/.games/tracer/`, when it is opened and tapped, then it draws and counts as before.
- `./bin/clang-format-fix` leaves no diff; `check_upstream_touches.py` passes.

## Implementation Notes

- Implemented directly (no coding subagent in this session). New: `lib/GameScript/TextMetrics.{h,cpp}`, `test/game_script/{GfxBindingsTest,TextMetricsTest}.cpp`. Changed: `DisplayList`, `ChBindings`, `LuaGame` (constructor takes a `Canvas`), `FrameReplay` (`textMetrics()`, skips line and circle), `GameVM::create`, `GameMatchActivity::onEnter`, the test fixture and `CMakeLists.txt` (adds `lib/Utf8/Utf8.cpp`), and the 34 construction sites in the existing suites (mechanical `canvas` argument).
- `TextMetrics::width` uses the renderer's `utf8NextCodepoint` (lib/Utf8), so malformed bytes and the stop at NUL match drawing; firmware flash grew 1,528 B on x4pro (5,791,386 to 5,792,914 B).
- Lua's `luaL_error` in a C function called from Lua prefixes the caller's chunk and line, so the frame-full message reads "main.lua:3: frame is full (...)"; argument errors read "bad argument #5 to 'line' (...)" (Lua names the field, not `ch.gfx.line`).
- `api-level-1.txt` already listed every signature, enum, and limit this entry ships; nothing changed there, so `API_SURFACE_CRC` stays 0xCAF107E5.
- Until entry 11, replay draws `light` fills white and `dark` black (the existing `inked`), ignores `align`, and skips lines and circles; the tracer uses none of them.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time against `review.diff`, 80 kB: blind-hunter with a floor of 9, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 0, medium 0, low 10, false 2, maybe-false 1.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | verification-gap | `LuaGame` must copy the `Canvas` (the match passes a local of `onEnter`), but every test's canvas outlived its game, so storing a reference would pass | low | Patched: `ScreenIsTheCanvasPassedAtStart` overwrites its canvas after constructing the game and still expects 123x45. |
| 2 | blind | `TheCommandLimitIs2048...` reads `ui.n or 2048`; `ui.n` is never set | low | Patched: plain `2048`. |
| 3 | verification-gap | `GameMatchActivity::onEnter`'s canvas (viewport size, replay metrics) has no automated test | low | Defer (`deferred-work.md`): the device-only harness gap of entries 1 and 8; the simulator probe showed "screen 474x788" and a width measured in `input`. |
| 4 | blind | `ch.text_width` ignores kerning and ligatures that `drawText` applies, so hit-tests and `align` can drift a few pixels once real tables arrive | low | Defer as a handoff to entry 11 (`deferred-work.md`): AD-7 fixes the port as advance tables; entry 11 owns how replay draws text. |
| 5 | blind | `light`/`dark` are now accepted but replay draws them white/black; lines, circles, and `align` do not appear on screen | low | Rejected: the intent's Never gives replay of these to entry 11 (Implementation Notes). |
| 6 | blind | The stand-in's 8/10/14 px widths disagree with the UI font replay draws in | low | Rejected: the intent names the stand-in for this build; entry 11 supplies the tables. |
| 7 | blind | Argument errors name `'line'`, not `ch.gfx.line` | low | Rejected: Lua's own `luaL_argerror` text; a custom formatter adds code for a cosmetic gain. |
| 8 | blind | Text after an embedded NUL still costs frame bytes though nothing after it draws or measures | low | Rejected: rare, bounded by the 32 KB limit, and consistent with the renderer. |
| 9 | blind | Tests pin exact Lua message text ("main.lua:3:"), which a Lua update could change | low | Rejected: pinning the message is the point of those tests. |
| 10 | blind | `ch.screen` is a plain table a script can overwrite | low | Rejected: it changes only that script's own view. |
| 11 | blind | `AdvanceTable::advanceOf` trusts `index`, `stride`, and `advances`; a malformed table reads out of bounds | false | Not reachable from a script: tables come from the host (flash font data in entry 11), like any other port data; the header states the shape. |
| 12 | blind | The `Align align = Align::Left` default on `appendText` could hide a forgotten argument | false | Every binding call passes `align`; the default only serves callers with nothing to align, and the reader decodes the stored byte. |
| 13 | edge (claim) | A combining mark adds its glyph's advance, while `getTextAdvanceX` gives it none | maybe-false | Would be low: settled by whether the built-in fonts store zero `advanceX` for marks; noted in finding 4's handoff entry. Rejected. |

Edge-case claims check: the Intent and Tasks claims held (line/circle/align/hint in the list, colors four for fills and two otherwise, sizes/aligns/modes checked, `ch.screen` and `ch.text_width` from the port passed at start, replay skips line and circle). Intent-alignment: readings are (a) the script surface and display list with its limits and a port for text metrics, verified by host decode tests, and (b) the whole `ch.gfx` visibly drawn on the device. The diff implements (a); (b)'s drawing is the intent's Never (entry 11). Expectations live at the Lua API and the command stream; the tests exercise both on the host, and the device wiring (canvas from the viewport) only in the simulator.

After the patches: `ctest` 522/522 (only test files changed).

## Design Notes

**Port (entry 11 supplies only tables).**

```cpp
struct AdvanceRange { uint32_t first, last, index; };   // EpdUnicodeInterval's fields
struct AdvanceTable {
  const AdvanceRange* ranges; uint32_t rangeCount;      // sorted, disjoint
  const uint8_t* advances; uint16_t stride;             // 12.4 u16 of glyph i at advances + i * stride
  uint16_t fallback;                                    // 12.4, for a code point in no range (U+FFFD's)
};
```

`stride` lets entry 11 point at a built-in font's `EpdGlyph` array (`advanceX` offset, `sizeof(EpdGlyph)`) without copying; it builds the ranges from the font's intervals. Tables must outlive the VM (flash, or storage the `GameVM` owns). `width` sums `fp4::toPixel`-style snapped advances per code point (no kerning or ligatures; entry 11 makes replay agree, including for `align`). Stand-in: no ranges, fallback 8/10/14 px for small/medium/large.

**Refresh hint.** A frame field, not a command: `refresh` never counts toward the 2,048; several calls keep the maximum (`full` > `half` > `fast`); entry 11 reads `DisplayList::refresh()` of each published frame and folds coalesced frames in `publish`.

**Layout.** Line: op, color, x1 y1 x2 y2 (10 B). Circle: op, color, filled, x y r (9 B). Text gains an align byte (header 10 B). Coordinates clamp to int16; a negative radius, like a non-positive rect size, draws nothing. The binding checks the fill flag before the color, so an outline in `light` fails on its color argument.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `pio run -e default`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (default and `-e x4pro`) -- SUCCESS, no defects.
- `sim.sh build x4pro`, `start x4pro`, tap into the tracer, `ss` -- screenshot in `story-gfx-screenshots/`.
- `./bin/clang-format-fix`, `python3 scripts/check_upstream_touches.py` -- no diff, PASS.

**Verification record** (2026-09-27, working tree before commit, on cda0f3cc):
- Host: `ctest` 522/522 passed (`GameScriptTest` 92: new 11 `GfxBindingsTest`, 5 `TextMetricsTest`/`TableTest`, 2 `DisplayListTest`).
- `pio run -e x4pro` SUCCESS (RAM 31.1 %, Flash 88.4 %, 5,792,914 B); `pio run -e default` SUCCESS (RAM 17.7 %, Flash 85.6 %); `pio check` (default) and `pio check -e x4pro` with the three `--fail-on-defect` flags: no defects.
- `./bin/clang-format-fix`: exit 0, no other files touched; `check_upstream_touches.py`: PASS (fork-only paths).
- Simulator x4pro (`sim.sh build x4pro` SUCCESS), screenshots in [story-gfx-screenshots/](story-gfx-screenshots/): `games.png`; `tracer-frame.png` and `tracer-tap.png` (the tracer draws, a tap counts 1 of 5 with its square); `probe-frame.png` and `probe-tap.png` (a scratch game, not committed, drawing every new call without error: "screen 474x788" from `ch.screen`, and after a tap "width in input: 140", `ch.text_width("tap at 234", "large")` with the stand-in's 14 px).

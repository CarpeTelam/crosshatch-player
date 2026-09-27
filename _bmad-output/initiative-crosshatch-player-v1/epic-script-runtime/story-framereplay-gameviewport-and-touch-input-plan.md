---
title: 'FrameReplay, GameViewport, and touch input'
type: 'feature'
ticket: '11'
created: '2026-09-27'
status: 'built'
baseline_revision: '2b65f1efb2752b2c4d779bca2344f4d9caedd9b7'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
  - '{project-root}/docs/contributing/touch-and-ui.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Replay draws only clears, rects, and one-font text (light/dark as white/black, no lines, circles, or `align`), `ch.text_width` measures a stand-in, every frame is a full refresh even when nothing changed, the Sticky simulator cannot run a game (no PSRAM), and only taps reach `input`.

**Approach:** `FrameReplay` draws every command with dithered fills and glyph-by-glyph text in three built-in fonts whose advance tables feed `ch.text_width`; a pure `RefreshPolicy` (hint folded across coalesced frames in `FrameBuffers`, `forceFull`, a fast-refresh counter, a shown-frame hash) picks the refresh or skips it; a pure touch classifier turns long presses and swipes into events, dropping the system's edge gestures; the sticky simulator env gains `BOARD_HAS_PSRAM`.

## Boundaries & Constraints

**Always:**
- Game text is drawn so its width equals `ch.text_width` for the same string and size: pen positions come from the same `TextMetrics` walk that measures (plain advances, no kerning, ligatures, or BiDi reordering); a code point the font lacks draws as its replacement glyph (or nothing when it has none) at the table's fallback advance. `align` shifts the start by the width (center: `w / 2`, floor).
- AD-7: `FrameBuffers::publish` folds the max hint of every frame published since the render last took the front, under the frame mutex; a new frame whose hash equals the frame on screen is not drawn or refreshed unless `forceFull` is set; a render with no new frame (an overlay popped, a system repaint) redraws with `forceFull`.
- Drawing cost stays bounded by the canvas: lines clipped before drawing, circles walked only over canvas rows, fills clipped (the render task holds `RenderLock` and the frame mutex).
- Edge gestures never reach `input`: a right swipe from the left 25%, an up swipe from the bottom 14%, a down swipe from the top 14%, classified by the SDK's `fui::edgeSwipe` on the logical screen as `MappedInputManager` does. Tap, long press, and swipe start must lie on the canvas; coordinates are canvas pixels.
- `GameScript`/`GameCore` stay free of HAL/Arduino/`GfxRenderer`/Logging; `src/games` `.cpp` whole-file `#if FREEINK_CAP_GAMES`; statics `constexpr`; locals under 256 B; fixtures only in `test/game_script/fixtures/`.

**Never:** pause/over/error views or `handleHomeGesture` (entry 13); store persistence (entry 12); icons/images; changing `api-level-1.txt` (it already lists `long_press`, `swipe`, `dir`); editing upstream files (`platformio.ini` included), `lib/lua`, `ci.yml`, `.skills/`, or the submodule pointer.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Fills | `clear`, filled `rect`/`circle` in white/light/dark/black | solid white, dithered light, dithered dark, solid black | — |
| Ink | `line`, outline `rect`/`circle`, `text` in white/black | drawn; off-canvas parts clipped | huge coordinates cost at most the canvas |
| Text | 3 sizes × left/center/right | small/medium/large fonts; drawn width == `ch.text_width` | unknown code point: replacement glyph's advance |
| Refresh | hints across coalesced frames; first frame; 11 fast frames in a row | max hint; full; the 11th is half, counter resets on half/full | — |
| Same frame | a new frame identical to the shown one | nothing drawn or refreshed | — |
| Repaint | render with no new frame | redrawn, full refresh | — |
| Touch | tap / long press / swipe on canvas | `{kind, x, y}` / with `dir` for swipe, canvas coords | start off canvas: dropped |
| Edge | Back, Home, or Menu edge swipe | never reaches `input` (Back/Home/light panel as today) | — |

</frozen-after-approval>

## Code Map

- `src/games/FrameReplay.{h,cpp}` -- `draw` switch over `Op` (keep exhaustive), `inked`, stand-in `textMetrics()`; becomes stateful (fonts, policy).
- `src/games/GameViewport.{h,cpp}` -- `forRenderer` (portrait minus `getOrientedViewableTRBL`; Sticky and X4 Pro both default insets, 474x788); `toCanvas` moves inline and a `(x, y, w, h)` constructor is added for tests.
- `src/games/GameVM.{h,cpp}` -- `drawFront` (use `takeFront`), `postTap` (replace with public `postInput`).
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `onEnter` builds the canvas (`replay.loadFonts(renderer)` first), `loop` touch (tap via `touchSnapshotFrom`), `render`.
- `lib/GameScript/FrameBuffers.{h,cpp}` -- `publish`, `readFront` (keep for tests); add `takeFront(fn(list, hint))`.
- `lib/GameScript/DisplayList.{h,cpp}` -- add `hash()` (FNV-1a 64 over the used bytes).
- `lib/GameScript/TextMetrics.{h,cpp}` -- `AdvanceTable` (ranges become bytes plus `rangeStride` read by `memcpy`, so a table points straight at a font's `EpdUnicodeInterval` array in flash), `advanceOf`, `width`; add `find`, `forEachGlyph`, `alignedStart`. Sites: `TextMetricsTest.cpp:41`, `GfxBindingsTest.cpp:346`.
- `lib/GameScript/CanvasClip.h` -- add `clipLine`, `circleRows`.
- `lib/GameCore/GameEvent.h`, `lib/GameScript/LuaGame.cpp` `pushEvent` -- `LongPress`, `Swipe`, `SwipeDir dir`; `Session::handle` already treats them as player events.
- `GfxRenderer`: `fillRectDither(x, y, w, h, Color)` (White/LightGray/DarkGray/Black, pattern on absolute pixels, clipped), `fillRect`, `drawLine`, `drawText` (y is the line top), `getFontMap()` → `EpdFontFamily::getData()` (`glyph[].advanceX`, `intervals`, `intervalCount`), `tapToLogical`. Built-ins always present: `UI_10_FONT_ID` (Ubuntu 10), `UI_12_FONT_ID` (Ubuntu 12), `NOTOSANS_18_FONT_ID`; Ubuntu has no U+FFFD.
- Touch: `touchSnapshotFrom(mappedInput, true)` (tap, long press; the long press suppresses the lift), `gpio.wasSwipe` + `renderer.tapToLogical` (per-frame, not consumed; `MappedInputManager::decodeSwipe` is private and upstream), `fui::swipeDirection`, `fui::edgeSwipe` (`FreeInkUICore.h`, header-only). `Button::Back` already folds the Back swipe; `ActivityManager::loop` handles Home and the light panel before `loop()`; a pop only calls `requestUpdate()`.
- `.claude/skills/run-crosshatch-player/{simulator.ini,SKILL.md}` -- sticky env flags; `hold X Y MS`, `swipe` exist.
- Already resolved, no work: the 2.7-review arena item (`c25a2ff6`, `LUA_REGION_BYTES` + `SCRATCH_RESERVE_BYTES`) and the codec stack at depth 16 (entry 8: at most about 1.8 KB on the S3).

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/GameEvent.h`, `lib/GameScript/{GameInput.h,LuaGame.cpp}` -- `EventKind::LongPress`, `Swipe`, `SwipeDir`; push `long_press {x, y}`, `swipe {x, y, dir}`.
- [x] `lib/GameScript/TextMetrics.{h,cpp}` -- byte ranges, `find`, `forEachGlyph(text, size, fn(cp, penX, known))` (width is its end), `alignedStart`.
- [x] `lib/GameScript/{DisplayList,FrameBuffers}.{h,cpp}`, new `RefreshPolicy.{h,cpp}` -- `hash()`, folded hint and `takeFront`; `decide(hash, hint, mode)` per Design Notes.
- [x] `lib/GameScript/CanvasClip.h` -- `clipLine`, `circleRows`.
- [x] `src/games/GameTouch.h` (header-only, pure) -- `Gesture` and `toEvent(gesture, screenW, screenH, viewport, event)`.
- [x] `src/games/{FrameReplay,GameViewport,GameVM}.{h,cpp}`, `src/activities/games/GameMatchActivity.{h,cpp}` -- replay of every op, font tables, policy; touch through `GameTouch`.
- [x] `test/game_script/{RefreshPolicyTest,GameTouchTest}.cpp`, `DisplayListTest.cpp`, `TextMetricsTest.cpp`, `CanvasClipTest.cpp`, `LuaGameTest.cpp`, `CMakeLists.txt` (FreeInkUI and `src/games` include dirs) -- every matrix row that is pure.
- [x] `test/game_script/fixtures/gallery/{manifest.json,main.lua}` -- every command and color, aligned text over guides with a `text_width` underline, the last event printed and marked.
- [x] `.claude/skills/run-crosshatch-player/{simulator.ini,SKILL.md}` -- `-DBOARD_HAS_PSRAM` in `simulator_sticky`; the landmark note.

**Acceptance Criteria:**
- Given the simulator (x4pro and sticky) with `fixtures/gallery` in `fs_/.games/gallery/`, when it opens, then the screenshot shows every command and color with visible light/dark dithers and underlines exactly as wide as their text; when a long press and a swipe land on the canvas, then the frame reports them in canvas coordinates; when an edge Back swipe is made, then the match leaves (today's Back) and the frame shows no swipe.
- Given `ctest`, then every suite passes; given `pio run -e x4pro`, `-e sticky`, `-e default`, then all build; `pio check` (x4pro, default) finds no defects; `check_upstream_touches.py` passes; `./bin/clang-format-fix` leaves no diff.

## Implementation Notes

- Implemented directly (no coding subagent in this session). New: `lib/GameScript/RefreshPolicy.{h,cpp}`, `src/games/GameTouch.h`, `test/game_script/{RefreshPolicyTest,GameTouchTest}.cpp`, `fixtures/gallery/`. Changed: `GameEvent.h` (`LongPress`, `Swipe`, `SwipeDir`, `dir`), `GameInput.h` (comment), `LuaGame.cpp` (`pushEvent`), `TextMetrics` (byte ranges with `rangeStride`, `find`, `forEachGlyph`, `alignedStart`, `toPixel`; `width` is `forEachGlyph`'s end), `DisplayList` (`hash`), `FrameBuffers` (folded hint, `takeFront`), `CanvasClip.h` (`clipLine`, `floorSqrt`, `circleRows`), `FrameReplay` (stateful: fonts, policy), `GameViewport` (inline `toCanvas`, test constructor), `GameVM` (`postInput` public, `postTap` gone, `drawFront` via `takeFront`), `GameMatchActivity`, the two table sites in the tests, `test/game_script/CMakeLists.txt`, and the simulator skill's `simulator.ini` (sticky gains `-DBOARD_HAS_PSRAM`) and `SKILL.md`.
- The repaint rule needed one more piece: the loop no longer asks for a frame a render already took (`renderedFrame` is atomic, written by render). Without it, a frame published between a request and its render made the next request a same-frame render, which the repaint rule would have forced to a full refresh.
- Gestures: taps and long presses still come from `touchSnapshotFrom` (AD-20); the swipe's start comes from `gpio.wasSwipe` + `renderer.tapToLogical`, the same calls `MappedInputManager::decodeSwipe` (private, upstream, not ledgered) makes. On the X4 Pro (a Home key) the bottom-edge up swipe reaches the match and `GameTouch` drops it; on the Sticky it is Home, handled by `ActivityManager`.
- Text is drawn in logical order with no BiDi reordering (one code point per `drawText`); a combining mark the font has is drawn at its own pen position. Built-in Ubuntu fonts have no U+FFFD, so an unknown code point in small or medium text draws nothing and advances 0, as `drawText` already did.
- Resolved elsewhere, no work here: the handoff's arena item (`c25a2ff6`) and the codec stack at depth 16 (entry 8).

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time against `review.diff`, 83 kB: blind-hunter with a floor of 10, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 0, medium 0, low 8, false 6, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind | If a frame is published after the render task wakes but before it reads `frameGen`, and a loop pass runs in that gap, the loop's second request makes a same-frame render that forces a full refresh of an unchanged screen | low | Rejected: needs a publish and a loop pass inside the render task's wake-to-read window; costs one extra full refresh, never a wrong screen; closing it needs a request flag, which can go stale across a pushed overlay and leave its pixels on screen (the worse failure). |
| 2 | blind | Any system `requestUpdate` (USB plugged, battery icon) during play forces a full refresh | low | Rejected: AD-7 makes a repaint the match did not ask for a `forceFull`; such repaints are rare. |
| 3 | blind | A 64-bit hash collision would skip a changed frame | false | Probability 2^-64 per frame pair; FNV-1a over every command byte. |
| 4 | blind | Text is measured twice per command (`width`, then `forEachGlyph`) | low | Rejected: at most 32 KB of text per frame, a table lookup per code point. |
| 5 | blind | Right-to-left text is drawn in logical order | low | Rejected: `ch.text_width` measures logically too, and the width rule requires one code point per draw (Implementation Notes). |
| 6 | blind | A missing built-in font would log "Font not found" per glyph | false | `setupDisplayAndFonts` inserts `UI_10`, `UI_12`, and `NOTOSANS_18` unconditionally (`OMIT_FONTS` is defined nowhere); `loadFonts` logs the impossible case once. |
| 7 | blind, edge | `clipLine` gives up after 8 passes and could drop a visible line | false | Each pass puts one endpoint on an edge line, leaving it at most the other axis's bits; two passes per endpoint settle it, so four passes suffice; the far-away-endpoint tests (`CrossingLinesEndOnTheCanvasOnTheSameLine`) pass. |
| 8 | blind | A long press starting in the 3 px bezel strip is consumed (its lift suppressed) and then dropped | low | Rejected: the strip is covered by the bezel; nothing else would have used that touch. |
| 9 | blind | The top-edge down swipe is dropped even on boards without a frontlight | false | The Input events convention makes it the device's Menu gesture on every board. |
| 10 | blind | `DisplayList::hash` runs under the frame mutex, delaying a VM publish | low | Rejected: one pass over at most 32 KB, beside a replay of the same bytes under the same mutex. |
| 11 | edge | `swipeDirName(None)` leaves `dir` out of a swipe event | false | `GameTouch::toEvent` never posts a swipe without a direction (`SwipeDir::None` returns false). |
| 12 | edge | `AdvanceTable` with `rangeStride` 0 reads one record for every range | false | Tables come from the host (flash fonts, `loadFonts` sets 12); port data, as entry 9 settled for `advances`. |
| 13 | verification-gap | `GameMatchActivity::render`'s repaint rule and the loop's no-re-request rule have no automated test | low | Defer (`deferred-work.md`): the device-only harness gap of entries 1, 8, and 10; the simulator run shows the frame repainted after the light panel closed (`after-panel-x4pro.png`), and the policy half is `RefreshPolicyTest.ForceFullRedrawsAnIdenticalFrame`. |
| 14 | verification-gap | `FrameReplay::loadFonts` building tables from real font data has no host test; a wrong offset would break the width rule | low | Defer with 13: the layout is pinned by four `static_assert`s against `EpdUnicodeInterval`, and the simulator shows each bar exactly under its text in every size and alignment. |

Edge-case claims check: the Intent and Tasks claims held (hint folded under the mutex, identical frame skipped unless forced, repaint forced, 11th fast frame half, edge swipes dropped by the SDK's classification on the logical screen, canvas coordinates, dithered light/dark, width equal to `ch.text_width` by construction). Deletion check: `postTap` (replaced by `postInput`, its only caller moved) and `FrameReplay::textMetrics()`'s stand-in (replaced by the font tables) carried no other contract. Intent-alignment: readings are (a) replay, policy, touch, and the Sticky simulator's PSRAM as the Approach states, and (b) Sticky-specific canvas geometry; the Sticky has the default bezel insets and the same 480 x 800 portrait screen, so (a) and (b) coincide (474 x 788, `gallery-sticky.png`). Expectations live on the device screen and at `input`; the tests exercise the pure policy, geometry, text walk, and touch classification on the host and the wiring in the simulator.

## Design Notes

**Fonts.** small → `UI_10_FONT_ID`, medium → `UI_12_FONT_ID` (the runtime's own UI fonts), large → `NOTOSANS_18_FONT_ID` (the largest built-in bitmap font; the Ubuntu UI family stops at 12). `FrameReplay::loadFonts` reads each family's regular `EpdFontData`: ranges = `intervals` (stride 12; a `static_assert` pins the layout to `AdvanceRange`), advances = `&glyph[0].advanceX` (stride `sizeof(EpdGlyph)`), fallback = `getGlyph(0xFFFD)`'s advance or 0. Flash data, so the copies in `Canvas` never dangle, even in a leaked VM. A missing font logs and keeps the stand-in for that size.

**Text.** Per glyph: `drawText(fontId, penX, y, <one code point>)`; a single code point gets no kerning or ligature and cannot be routed to the SD CJK fallback, since unknown code points are replaced first.

**Policy** (`GameScript::RefreshPolicy`, `FAST_REFRESH_LIMIT` 10):

```cpp
bool decide(uint64_t hash, Refresh hint, Refresh& mode) {
  if (!force && shown && hash == shownHash) return false;   // identical: skip
  mode = force ? Refresh::Full : hint;  force = false;
  if (mode == Refresh::Fast && ++fastCount > FAST_REFRESH_LIMIT) mode = Refresh::Half;
  if (mode != Refresh::Fast) fastCount = 0;
  shown = true;  shownHash = hash;  return true;
}
```

`force` starts true (the screen shows the Games list). The match's `render` calls `replay.forceFull()` when `frameGen` has not moved since its last render.

**Touch.** `toEvent`: tap/long press map through `viewport.toCanvas`; a swipe is dropped if `edgeSwipe(Left|Top|Bottom)` holds on the logical screen or its start is off the canvas, else `{Swipe, start, swipeDirection}`.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `-e sticky`, `-e default`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (`-e x4pro`, `-e default`) -- SUCCESS, no defects.
- `sim.sh build x4pro` and `build sticky`; `start`, `hold`, `swipe`, `ss` -- screenshots in `story-replay-screenshots/`, looked at.
- `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` -- PASS, no diff.

**Verification record** (2026-09-27, working tree before commit, on 2b65f1ef):
- Host: `ctest` 577/577 passed (`GameScriptTest` adds 8 `RefreshPolicyTest`, 6 `GameTouchTest`, 6 `CanvasClipTest`, 4 `TextMetricsTest`/`TableTest`, 2 `DisplayListTest`/`FrameBuffersTest`, 2 `LuaGameTest`). Every pure matrix row has a passing test: fills and white ink in the gallery's commands, clipping of far-off lines and huge circles, drawn width equal to `ch.text_width` for each alignment, hint folding across coalesced frames, first frame full, the 11th fast frame half, `forceFull`, identical-frame skip, canvas coordinates, and every edge swipe dropped.
- `pio run -e x4pro` SUCCESS (RAM 31.1 %, Flash 88.5 %, 5,800,042 B, +3,728 B over entry 10); `-e sticky` SUCCESS (Flash 86.8 %); `-e default` SUCCESS (Flash 85.6 %). `pio check` with the three `--fail-on-defect` flags: x4pro and default PASSED, no defects. `check_flash_budget.py build on` + `objects`: 34 game objects, no mutable static, no static initializer.
- `./bin/clang-format-fix`: exit 0, only this story's files touched. `check_upstream_touches.py`: PASS (fork-only paths; run again on the commit).
- Simulator x4pro and sticky (`sim.sh build x4pro`, `build sticky`, both SUCCESS; the sticky env now runs games), `fixtures/gallery` in `fs_/.games/gallery/`, screenshots in [story-replay-screenshots/](story-replay-screenshots/), each looked at: `gallery-final-x4pro.png` and `gallery-sticky.png` (every command; light and dark dithered; white ink on black; each bar exactly under its text, left, centred on the guide, and right against it, in all three sizes; the far-off line clipped to the canvas edges); `longpress-x4pro.png`/`longpress-sticky.png` (a hold at screen 200,700 / 100,450 reached `input` as `long_press` at canvas 197,691 / 97,441, no tap after it); `swipe-x4pro.png` (swipe 300,400 to 100,420: `swipe` at 297,391 `left`; sticky: 197,591 `right`); `top-edge-x4pro.png` (the light panel opened, no event) and `after-panel-x4pro.png` (after Back the frame is repainted whole, not left under the panel's pixels); `top-edge-sticky.png` (no frontlight: no event); `back-swipe-x4pro.png`/`back-swipe-sticky.png` (a swipe 20,400 to 300,410 left the match for Games, no event logged); `home-swipe-sticky.png` (the bottom-edge up swipe went Home); `tracer-tap-x4pro.png` (the tracer still counts taps). The log held only the two expected `[gallery] event` lines per run. After the loop's no-re-request change the x4pro run was repeated (long press, swipe, light panel, Back) with the same results; the sticky shots predate that change, which touches only shared match code.

---
title: 'ch.gfx.image and package images'
type: 'feature'
ticket: '2'
created: '2026-09-28'
status: done
baseline_revision: 'bc6adc548587ec84c1530e72f1fb6908dc13895c'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A game cannot draw its own graphics: `GameAssets` loads only Lua sources and the store, and `ch.gfx` has no `image`. The installer (epic-install-and-launcher) will write each package PNG as a 1-bit `.bmp` in `PngToBmpConverter`'s layout, and nothing reads that layout into a game.

**Approach:** A pure, host-tested `GameCore` unit owns the `.bmp` header check, the image-name rule, the one 128 KiB converted-image constant, and the loaded-image table. `GameAssets` loads every `<name>.bmp` except `icon.bmp` into its PSRAM block through it. `LuaGame` passes the table to the bindings in `BindingContext`, and `ch.gfx.image` appends an `Image` display-list command. `FrameReplay` draws that command opaque through a pure blit in `src/games`. An unknown name stops the game through `CallGuard::raise`, and a malformed or over-budget image is a load failure with one new `STR_GAMES_*` reason.

## Boundaries & Constraints

**Always:**
- AGENTS.md rules: C3-safe shared code, `LOG_*`, no bare `new`, locals under 256 B (the header buffer goes in a helper, never in `GameAssets::load`'s frame), every `src/games/*.cpp` whole-file `#if FREEINK_CAP_GAMES`, AD-2 (`inline constexpr` in headers, no mutable statics). Every build, simulator build, and host-test CMake step runs under `flock /tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/build.lock sh -c '...'`.
- The accepted layout is exactly `writeBmpHeader1bit`'s: `BM`, file size, pixel offset 62, a 40-byte DIB, width > 0, negative height (top-down), 1 plane, 1 bpp, no compression, image size = row bytes × height, 2 colours, palette `00 00 00 00 FF FF FF 00` (index 0 black). Rows are padded to 4 bytes, MSB first, and bit 1 is white. The file is exactly 62 + row bytes × height bytes long.
- Image names are `[a-z0-9_]{1,32}` + `.bmp`; `icon.bmp` is never an image. The budget counts the `.bmp` files' bytes, headers included (`GameCore::IMAGES_BYTES` = 131,072), across at most `GameCore::MAX_IMAGES` = 32 images.
- No new include edge: `lib/GameScript` and `src/games` already reach `lib/GameCore`, and `python3 scripts/check_layers.py` still passes. The spine is unchanged.
- `ch.gfx.image(name, x, y, color)` draws top-left at `x, y` at native size, opaque. `black` draws as converted; `white` swaps black and white. Colour is `white` or `black` only. The image is clipped to the canvas and is one command within 2,048 commands / 32 KB.

**Never:**
- No change to `PngToBmpConverter`, the installer, `pack_game.py`, `freeink-sdk`, `.skills/`, the spine, `ci.yml`, or any upstream file except the one `english.yaml` key (ledger row 2, append-only). No image scaling or grayscale.
- Never commit (the build agent commits).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Draw | `ch.gfx.image('badge', x, y, c)` in draw, c ∈ {black, white} | one Image command (index, x, y, color); replay fills every visible pixel: black bits black, white bits white; `white` swaps them | none |
| Clipped | image partly or wholly off the canvas | only the on-canvas part is drawn, nothing when wholly off; only visible rows and columns are walked; padding bits never drawn | none |
| Unknown name | `'no_such_image'`, also inside `pcall`, also with no images loaded | game stops (`Fault::Binding`), frame unpublished, `<chunk>:<line>: ch.gfx.image: unknown image "no_such_image"`; long or odd names are shortened and sanitized as for icons | `CallGuard::raise` |
| Bad arguments | non-string name, `'light'`, non-integer x | ordinary Lua argument error | `luaL_arg*` |
| Header check | valid; under 62 bytes or shorter than its layout; not `BM`; 2/4/8/24 bpp; bottom-up, zero width, other palette, compression, longer than its layout; file larger than the budget left | Ok; `Truncated`; `NotBmp`; `WrongDepth`; `WrongLayout`; `OverBudget` (exactly the budget left is Ok) | result enum |
| Load | a `.bmp` fails the check, the total passes 131,072 B, or more than 32 images | `LoadResult::BadImage`; the match shows "The game could not start" and the new reason | logged with file and cause |
| Load | `icon.bmp`, `Icon.BMP`, `bad-name.bmp` | not in the table (a misnamed `.bmp` is logged) | none |

</frozen-after-approval>

## Code Map

- The project root is the worktree `/home/user/wt-runtime` (branch `epic3/runtime`). Scratch goes under `/tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/3.2/`. The toolchain is installed; do not reinstall it or touch certificates.
- `lib/PngToBmpConverter/PngToBmpConverter.cpp:125` `writeBmpHeader1bit`, and the 1-bit row packing at :693 and :759 (`bitOffset = 7 - x % 8`), are reference only. Resolved unknown: the converter does **not** build on the host. It needs `HalStorage`/`HalFile`, Arduino `Print`, FreeRTOS `task.h`, `HalDisplay`, and `InflateStream`, and no host suite links it or stubs them. So the fixture `.bmp` files are committed bytes, and a test checks them against the converter's layout.
- `lib/GameCore/` holds pure std code, host-tested in `test/game_core` (`CMakeLists.txt` lists sources explicitly). `test/game_script/CMakeLists.txt` also lists `lib/GameCore/*.cpp` it links (`Roster.cpp`, `Session.cpp`).
- `src/games/GameAssets.{h,cpp}` has `load()`'s two directory passes (count, then read), one `HalMemory::allocatePsram` block, `moduleNameOf`, `LoadResult`, and `release()`. `GameVM.cpp:55-62` constructs `LuaGame(..., assets.sources(), ...)`, and `GameVM::drawFront` calls `replay.draw(renderer, viewport, frame, hint)`. The frame and the images share the VM's lifetime: `abandon` runs under RenderLock.
- `src/activities/games/GameMatchActivity.cpp:29` holds `loadFailureReason`. `lib/I18n/translations/english.yaml` ends with `STR_GAMES_BAD_SOURCE_NAME`, so append after it.
- `lib/GameScript/ChBindings.{h,cpp}` has `BindingContext`, `drawTarget`, `checkInkColor`, `frameFull`, `unknownIcon` (the sanitizer to share), `gfxIcon`, and `GFX_FUNCTIONS`. `LuaGame.{h,cpp}` has the constructor, `load()` filling `bindings.*`, and `sources` stored by reference. `DisplayList.{h,cpp}` has `Op`, `DrawCommand`, the layout comment, `appendIcon`, and `Reader::next`.
- `src/games/FrameReplay.{h,cpp}` has the `draw()` op switch. `src/games/GameIconBlit.h` is the pure-blit pattern (int64 clip bounds, visible-only walk, runs).
- Tests:
  - `test/game_script/LuaGameFixture.h`: `LuaGameTest`, `DirectGame`, `readFixture`, `GAME_SCRIPT_FIXTURES_DIR`.
  - `GfxBindingsTest.cpp`: `describe()` switch (`-Wswitch`), `EveryCallAndArgumentFormDecodes`, `EveryGfxCallOutsideDrawIsAScriptError`, `BadArgumentsAreScriptErrors`, and the unknown-icon tests to mirror.
  - `DisplayListTest.cpp`; `GameIconBlitTest.cpp`.
  - `ApiSurfaceTest.cpp:397` (the `live` limit map must list every `limit` line).
  - `SessionGameTest.EveryFaultScriptEndsWithTheReadmesText`, which needs a README row per `faults/*.lua`.
- `docs/crosshatch/api-level-1.txt` and `lib/GameCore/ApiLevel.h` `API_SURFACE_CRC`: `ApiSurfaceTest.ListLoadsAndMatchesItsCrc` / `ApiLevelTest` print the new value.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/GameImages.{h,cpp}` (new) holds these, each with a one-line comment:
  - `IMAGES_BYTES`, `MAX_IMAGES`, `IMAGE_HEADER_BYTES` (62), `IMAGE_NAME_BYTES` (32);
  - `enum class ImageCheck {Ok, Truncated, NotBmp, WrongDepth, WrongLayout, OverBudget}` and `imageCheckName()`;
  - `struct ImageHeader {uint32_t width, height, rowBytes; size_t pixelBytes() const;}`;
  - `ImageCheck checkImageHeader(const uint8_t* header, size_t headerBytes, size_t fileBytes, size_t budgetLeft, ImageHeader&)`, in the matrix's order, with 64-bit size math;
  - `bool imageNameOf(const char* fileName, size_t length, char (&name)[IMAGE_NAME_BYTES + 1])`, false for `icon.bmp`;
  - `bool looksLikeImage(fileName, length)` (any-case `.bmp`);
  - `struct ImageSpan {char name[33]; uint32_t width, height, rowBytes, offset;}`;
  - `struct GameImages {const ImageSpan* spans; size_t count; const uint8_t* pixels; int find(const char*, size_t) const; const uint8_t* pixelsOf(const ImageSpan&) const;}`;
  - `inline constexpr GameImages NO_IMAGES{}`.
- [x] `test/game_core/ConverterBmpLayout.h` (new) mirrors `writeBmpHeader1bit` field by field as `std::vector<uint8_t> converterHeader1bit(int w, int h)` and packs a pixel grid in the converter's row layout.
- [x] `test/game_core/GameImagesTest.cpp` (new) plus its CMake entry test every header-check row (widths 1, 32, and 33 for padding; at budget Ok, one over `OverBudget`), the names (`icon.bmp`, 33-char, uppercase, `-`, no stem), and `find` (hit, miss, prefix, longer, embedded NUL).
- [x] `src/games/GameAssets.{h,cpp}`: in pass 1, validate each image's header through a helper that owns the 62-byte buffer, and total its bytes. Allocate one block, `[SourceSpan×n][ImageSpan×m][pixels][text]`. In pass 2, re-check each header and read its pixels inside what pass 1 sized. A changed folder is `CannotRead`. Append `LoadResult::BadImage`; add `images()`; `release()` clears both views.
- [x] `src/activities/games/GameMatchActivity.cpp`: `BadImage` → `STR_GAMES_BAD_IMAGE`. `english.yaml`: append `STR_GAMES_BAD_IMAGE: "An image is damaged or too large"`.
- [x] `lib/GameScript/ChBindings.{h,cpp}`:
  - `BindingContext::images` (`const GameCore::GameImages*`) is the port.
  - `unknownIcon` becomes `unknownName(L, function, kind, name, length)`, and the icon text stays unchanged.
  - `gfxImage` is registered as `image`.
  - `static_assert(MAX_IMAGES <= UINT16_MAX)`.
- [x] `lib/GameScript/LuaGame.{h,cpp}`: the constructor gains a trailing `const GameCore::GameImages& images = GameCore::NO_IMAGES`, stored by reference, and `load()` sets `bindings.images`.
- [x] `lib/GameScript/DisplayList.{h,cpp}`: `Op::Image` goes last. `DrawCommand::image` is a `uint16_t`, and the layout is `Image op u8, color u8, x y i16, image u16`. Add `appendImage` and decode it.
- [x] `src/games/GameImageBlit.h` (new, pure, header-only): `bool blackAt(pixels, rowBytes, x, y)`, and `template <typename Fn> void runs(const ImageSpan&, const uint8_t* pixels, int32_t left, int32_t top, int32_t width, int32_t height, Fn&& fn)`, which calls `fn(y, x, w, black)` for each clipped run of one colour.
- [x] `src/games/FrameReplay.{h,cpp}`: `draw()` takes `const GameCore::GameImages&`. `case Op::Image` fills each run with `black == (command.color == Black)`, and an index past the table is logged and skipped. In `src/games/GameVM.cpp`, pass `assets.images()` to `LuaGame` and `replay.draw`.
- [x] Tests:
  - `test/game_script/GameImageBlitTest.cpp` (new, in CMake) covers exact opaque coverage, bit order, padding ignored, each clipped edge, and wholly off.
  - `DisplayListTest` covers the Image round trip (8 bytes, one command).
  - `GfxBindingsTest` covers image decode (both colours, clamp), outside draw, bad args, and an unknown name direct, under `pcall`, and with no images (Binding fault, unpublished, exact text).
  - `ApiSurfaceTest`: the `images_bytes` and `images_count` limits.
  - `LuaGameFixture.h`: an `images` member plus `useImages(folder)`, built with `checkImageHeader`/`imageNameOf` over `fixtures/<folder>/*.bmp`.
  - `LuaGameTest`: the fixture's `.bmp` headers equal `converterHeader1bit`, they check Ok, `bad-image`'s file fails, and the `images` fixture draws every image in black and in white.
- [x] Fixtures:
  - `test/game_script/fixtures/images/`: manifest; `main.lua`; `badge.bmp` (100×60, border and crosshatch, a width that is not a multiple of 32); `dot.bmp` (37×37 disc); and an `icon.bmp` (64×64) the loader must skip. Write the bytes with a one-off Python snippet in scratch, recorded in Implementation Notes. `main.lua` draws a black row and a white row over `light` bands, showing opacity, plus one image clipped at the right edge.
  - `fixtures/bad-image/`: manifest, `main.lua`, and `broken.bmp` (a converter header claiming 8 bpp).
  - `faults/unknown_image.lua`: mirrors `unknown_icon.lua`.
  - `README.md`: the `## Games` rows and the fault row.
- [x] `docs/crosshatch/api-level-1.txt`:
  - `fn ch.gfx.image(name, x, y, color)` after `ch.gfx.icon`;
  - the pcall comment names an unknown icon or image name;
  - `limit images_bytes 131072` and `limit images_count 32`, with a comment line (the `.bmp` bytes, headers included; `icon.bmp` excluded).
  - Update `API_SURFACE_CRC`.

**Acceptance Criteria:**
- Given the `images` fixture in `fs_/.games/images/` on `simulator_x4pro`, when it opens, then a screenshot shows both images at native size in black and in white, opaque over the light band, and one clipped at the edge.
- Given `bad-image` in `fs_/.games/`, when it opens, then the load-failure view shows "The game could not start" and "An image is damaged or too large".
- Given `faults/unknown_image.lua` as `f-unknown-image`, when it opens, then the error view shows the README's text.

## Implementation Notes

- Fixture `.bmp` bytes were written by this one-off snippet (run from `test/game_script/fixtures/`), which mirrors
  `writeBmpHeader1bit` and the converter's `oneBit` row packing; `LuaGameTest.TheFixtureImagesAreInTheConvertersLayout`
  checks every committed header against `ConverterBmpLayout::converterHeader1bit`:

  ```python
  import struct, pathlib
  def header(w, h, bpp=1):
      row = (w + 31) // 32 * 4; size = row * h
      return (b'BM' + struct.pack('<IIIIiiHHIIIIII', 62 + size, 0, 62, 40, w, -h, 1, bpp, 0, size, 2835, 2835, 2, 2)
              + bytes([0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0]))
  def rows(w, h, white):  # bit 1 = white, MSB first, rows padded to 4 bytes (padding left 0)
      row = (w + 31) // 32 * 4; out = bytearray(row * h)
      for y in range(h):
          for x in range(w):
              if white(x, y): out[y * row + x // 8] |= 1 << (7 - x % 8)
      return bytes(out)
  bmp = lambda w, h, white, bpp=1: header(w, h, bpp) + rows(w, h, white)
  badge = lambda x, y: not (x < 3 or y < 3 or x >= 97 or y >= 57) and (x + y) % 10 != 0 and (x - y) % 10 != 0
  dot = lambda x, y: (x - 18) ** 2 + (y - 18) ** 2 > 18 * 18
  icon = lambda x, y: abs(x - y) <= 3 or abs(x + y - 63) <= 3
  pathlib.Path('images/badge.bmp').write_bytes(bmp(100, 60, badge))    # 1,022 B
  pathlib.Path('images/dot.bmp').write_bytes(bmp(37, 37, dot))         # 358 B
  pathlib.Path('images/icon.bmp').write_bytes(bmp(64, 64, icon))       # 574 B, skipped by the loader
  pathlib.Path('bad-image/broken.bmp').write_bytes(bmp(16, 16, lambda x, y: (x + y) % 2 == 0, bpp=8))  # WrongDepth
  ```
- `checkImageHeader` order: under 62 bytes → `Truncated`; no `BM` → `NotBmp`; bpp ≠ 1 → `WrongDepth`; pixel offset,
  DIB size, width ≤ 0, height ≥ 0 (bottom-up or zero), planes, compression, colours used ≠ 2, palette, then image-size
  and file-size fields → `WrongLayout`; file shorter than its layout → `Truncated`, longer → `WrongLayout`; then
  `OverBudget`. The resolution fields and colours-important are not checked (the converter writes 2835 and 2).
- `GameAssets::load` pass 1 records a bad image and returns `BadImage` only after the source checks, so a folder with
  no loadable Lua still says so first; more than `MAX_IMAGES` images is counted, then `BadImage`. Pass 2 re-checks
  each header against the budget pass 1 sized (its file bytes, not the full 128 KiB) and the pixel bytes left; any
  mismatch is `CannotRead`. One `Stem` buffer serves module and image names (a `static_assert` ties the two 32s);
  `readImageHeader` is `[[gnu::noinline]]` so its 62-byte buffer stays out of `load()`'s frame. A misnamed `.bmp`
  is logged unless it is `icon.bmp` in any case.
- `FrameReplay::draw` takes the images as its last parameter; `GameVM::drawFront` passes `assets.images()`.
  `LuaGameFixture`'s `SessionGame` passes the fixture's `images` too (empty unless `useImages`).
- x4pro: flash 5,828,302 B (+3,136 B against 5,825,166 B at `b562ad3c`; `bc6adc54` differs from it only in
  `_bmad-output`, so the baseline stands), RAM 101,824 B (unchanged). `default` (C3) builds: flash 5,624,021 B,
  RAM 57,912 B.
- `API_SURFACE_CRC` is `0xAEB225B0`.
- Screenshots (simulator_x4pro): `story-image-screenshots/images.png`, `bad-image-load-failure.png`,
  `unknown-image-error.png`.
- Review patches (pass 1):
  - Pass 1's accounting is now the pure `GameCore::ImageBudget::add`. It refuses a 33rd image with a new verdict, `ImageCheck::TooMany`, and then checks against the budget left. `GameAssets::load` (through the noinline `addImage`, which replaces `readImageHeader`) and `LuaGameFixture::useImages` share it. Pass 2 re-adds each image to a second budget and must stay within pass 1's.
  - `GameImageBlit::runs` takes `drawBlack` and hands `fn` the ink to fill, so `FrameReplay` no longer decides the ink inline.
  - `unknownName(L, kind, ...)` now takes one parameter.
  - `GameImages::find` compares without `strlen`, and refuses a query that contains a NUL.
  - `DisplayListTest.AnImagePastTheByteLimitIsRefused` is new.
  - `api-level-1.txt` has a comment giving the size rule: 62 + ceil(w / 32) * 4 * h.
  - A converter file is 62 + 4k bytes, so no two images can sum to exactly `IMAGES_BYTES + 1`. The over-budget case is `IMAGES_BYTES + 4`.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-09-28). The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as
context-free subagents over `git diff bc6adc54` (BMAD output excluded).

| Verdict | Count |
| --- | --- |
| high | 0 |
| medium | 2 |
| low | 11 |
| false or rejected | 6 |
| maybe-false | 1 |

Nine findings were patched (1-9), three deferred (10, 11, 19), and eight rejected (12-18, 20).

| # | Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- | --- |
| 1 | verification-gap, blind, intent | `GameAssets::load`'s budget, count cap, and bad-image verdict run in no test, and `useImages` mirrors them | medium | patch: pure `ImageBudget::add` shared by the loader and the fixture. Tests added: `TheBudgetCountsEveryImageBeforeIt`, `TheThirtyThirdImageIsRefused`, `ABadImageInTheMiddleIsRefusedAlone`. The remaining loader wiring (span offsets, block layout) needs a Storage harness: see #19 |
| 2 | verification-gap, blind, intent | `FrameReplay`'s ink rule (`white` inverts) is inline and untested | medium | patch: `runs(..., drawBlack, fn)` yields the ink; `GameImageBlitTest.BlackDrawsAsConvertedAndWhiteSwapsTheInk` |
| 3 | blind | `unknownName` takes `function` and `kind`, and both call sites pass the same string | low | patch: one parameter; the icon text is unchanged (its exact-text tests pass) |
| 4 | blind | The `WrongLayout` comment says "any other field" | low | patch: the comment names the fields left unchecked (reserved bytes 6-9, resolution, colours-important) |
| 5 | blind | Every pass-1 failure log names the budget | low | patch: only `OverBudget` does; `TooMany` has its own line |
| 6 | blind | The misnamed-file comment sits on the `.lua` branch | low | patch: each branch has its own comment; the `.bmp` skip is marked deliberate |
| 7 | blind | `find` runs `strlen` per span per call | low | patch: `memcmp` plus a terminator check; a query with an embedded NUL is refused |
| 8 | blind | No test hits the 32 KB byte limit with an Image command | low | patch: `DisplayListTest.AnImagePastTheByteLimitIsRefused` |
| 9 | blind | The API list gives no size formula for the budget | low | patch: comment line in `api-level-1.txt` (outside the CRC) |
| 10 | blind | Replay cost of a dithered full-canvas image (per-run `fillRect`) is unmeasured | maybe-false, medium if true | defer: needs a device timing of a worst-case dithered image on the render task; `deferred-work.md` `## 3.2` |
| 11 | blind | The installer must count the budget the same way (headers included, `icon.bmp` excluded) | low | defer: a handoff to epic-install-and-launcher; `deferred-work.md` `## 3.2`, and an Assumed line below |
| 12 | edge | A new `.bmp` added between passes, earlier in directory order, can displace a counted image | low | rejected: the loop task owns Storage while the match loads, so nothing writes the folder between the passes in everyday use; the fix adds a guard |
| 13 | edge | A new `.lua` added between passes can displace a counted module | low | rejected: pre-existing sources behaviour (epic-script-runtime), same reasoning as #12 |
| 14 | edge | The plan says "a changed folder is CannotRead", but a same-count change that fits passes | rejected | the fix would edit this plan's claim; the race is #12's |
| 15 | blind | The launcher icon's file name appears twice (`imageNameOf`, `GameAssets`) | rejected | style only, no named harm; the installer and launcher epic can name a constant when it adds the third use |
| 16 | blind | "An image is damaged or too large" also covers more than 32 images | rejected | "too large" reads naturally for too many images, and the log names the exact cause |
| 17 | intent | The converter mirror is not tied to the real converter | rejected | intended by the ticket's unknown: the converter does not build on the host, so the fixtures are committed bytes checked against its layout; `ConverterBmpLayout.h` names the file to keep in step |
| 18 | intent | Colour parameter, `images_count`, and a new CRC go beyond the intent | false | the ticket names `ch.gfx.image(name, x, y, color)` and "the image limits" |
| 19 | intent, verification-gap | `LoadResult::BadImage` → `STR_GAMES_BAD_IMAGE` and the loader's span wiring have no host test | low | defer: `src/games` and the match have no Storage harness (retro AI-2 stays with epic-install-and-launcher); the `bad-image-load-failure.png` and `images.png` screenshots cover them; `deferred-work.md` `## 3.2` |
| 20 | intent | A misnamed `.bmp` does not fail the load, unlike a folder of only misnamed `.lua` | rejected | the frozen matrix says a misnamed `.bmp` is logged and not in the table |

## Design Notes

**Assumed (reversible, for entry 8):**
- The budget counts `.bmp` bytes with headers, because the installer and `pack_game.py` can compute them from file sizes or PNG dimensions.
- Images are capped at 32, mirroring AD-15's member cap as `MAX_SOURCES` does.
- `api-level-1.txt` carries both limits, as the vector for `pack_game.py`.
- The unknown-name text is `ch.gfx.image: unknown image "<name>"`.
- The strict layout check accepts only the converter's header.
- One PSRAM block holds both sources and images.
- A misnamed `.bmp` is skipped with a log line, like a misnamed `.lua`, not a failure.
- The fixture names and shapes.
- `GameCore` owns the format and the table, because the launcher (Screens) must read `icon.bmp` later and Screens may not include `lib/GameScript`.
- `icon.bmp` is outside the 128 KiB budget, and the installer must count the budget the same way (review #11).
- The load-failure text is "An image is damaged or too large", which also covers more than 32 images.

## Verification

**Commands:**
- `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass.
- `flock <lock> sh -c 'pio run -e x4pro && pio run -e default'` -- expected: SUCCESS. Record the x4pro flash and RAM against the baseline 5,825,166 B / 101,824 B (3.1's measurement at `b562ad3c`), or rebuild the baseline if `bc6adc54` differs.
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, and every `scripts/*_test.py` -- expected: exit 0.
- `flock <lock> sh -c '.claude/skills/run-crosshatch-player/sim.sh build x4pro'`, place `images`, `bad-image`, and `f-unknown-image` in `fs_/.games/`, and screenshot each. Look at each, then copy them to `_bmad-output/initiative-crosshatch-player-v1/epic-icon-library/story-image-screenshots/` as `images.png`, `bad-image-load-failure.png`, and `unknown-image-error.png`.
- `./bin/clang-format-fix` twice -- expected: the second run changes nothing.

**Results (2026-09-28, after the review patches):**
- **Host suites** (run under the lock): `100% tests passed, 0 tests failed out of 682`. These include:
  - `GameImagesTest.*`: every header-check row, names, `find`, and `ImageBudget`'s cumulative budget, 33rd image, and a bad image in the middle;
  - `GameImageBlitTest.*`, including `BlackDrawsAsConvertedAndWhiteSwapsTheInk`;
  - `DisplayListTest.RoundTripsImages` and `AnImagePastTheByteLimitIsRefused`;
  - the `GfxBindingsTest` image cases: an unknown name direct, under `pcall`, and with no images;
  - `ApiSurfaceTest` limits and CRC (`0xAEB225B0`);
  - `LuaGameTest.TheFixtureImagesAreInTheConvertersLayout` and the `images` fixture draw;
  - `SessionGameTest.EveryFaultScriptEndsWithTheReadmesText` (`unknown_image`).
- **`pio run -e x4pro`**: SUCCESS. Flash 5,828,482 B, RAM 101,824 B. The flash delta is +3,316 B against 3.1's 5,825,166 B at `b562ad3c` (`bc6adc54` changes only `_bmad-output`), from `pio run` size output on the same toolchain. Static RAM is +0 B.
- **`pio run -e default` (C3)**: SUCCESS. Flash 5,624,021 B, RAM 57,912 B.
- **Checks and scripts**:
  - `check_layers.py`: passed, with 351 edges in 88 game files and no new edge kind.
  - Every `scripts/*_test.py` passes.
  - `check_upstream_touches.py`: PASS on the commit. The only upstream file touched is `english.yaml`, ledger row 2, append-only.
- **Simulator** (`sim.sh build x4pro`): the fixtures were placed in `fs_/.games/` and removed afterwards. I looked at each screenshot after the review patches:
  - `story-image-screenshots/images.png`: `badge` (100×60) and `dot` (37×37) at native size in black (as converted) and in white (inverted), each opaque over a `light` band, plus a `badge` clipped at the right edge.
  - `story-image-screenshots/bad-image-load-failure.png`: "The game could not start" with "An image is damaged or too large".
  - `story-image-screenshots/unknown-image-error.png`: the error view with `main.lua:10: ch.gfx.image: unknown image "no_such_image"`, which is the README's text.
- **`./bin/clang-format-fix`**, run twice: it reformatted only this story's new files, and the second run changed nothing.

---
title: 'e3r-1: bound the per-frame replay cost of ch.gfx.icon and ch.gfx.image, and list the icon, ink, and image-name rules'
type: 'feature'
ticket: ''
created: '2026-09-28'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '8bd18e86609d0e41b7bf22d95f9e95319449f228'
context:
  - '{project-root}/docs/crosshatch/api-level-1.txt'
  - '{project-root}/test/game_script/fixtures/README.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** (epic-icon-library retrospective R1, AI-10) A frame may hold 2,048 `Image` or `Icon` commands, and `FrameReplay` walks every visible pixel of each and fills each one-colour run with a 1-px `fillRect`; a legal 480x800 checkerboard image gives 384,000 runs a command, so one frame can cost ~786 M fills on the render task under `RenderLock`, which no watchdog covers. (R9 (d), AI-2) Level 1 also leaves three rules games rely on outside its entries: the drawn icon sizes (32/64/128 px), "icons draw ink only; images draw opaque", and the image-name grammar `[a-z0-9_]{1,32}`.

**Approach:** A per-frame budget of canvas pixels covered by `ch.gfx.icon` and `ch.gfx.image` commands, counted at append time from each command's rectangle (at the list's saturated coordinates) clipped to the canvas, held by the `DisplayList` and reset by `clear()`, and raised by the bindings through `CallGuard::raise` (sticky under pcall, frame unpublished) as a full frame is. New level-1 entries state the budget, the three icon sizes, the ink/opaque rule, and the image-name grammar; `ApiSurfaceTest` checks each against the code, with one `API_SURFACE_CRC` update.

## Boundaries & Constraints

**Always:** the budget is `limit frame_icon_image_pixels 1048576` (owner-confirmable assumption; level 1 is a preview). A command's charge is exactly the pixels its replay walks: the intersection of `[x, x+w) x [y, y+h)` (x, y saturated to int16 as recorded; w, h the image's size or the icon's drawn side) with `[0, canvas.w) x [0, canvas.h)`, so an off-canvas command is free. Charge before appending; a refused charge appends nothing. Keep every guard in `gfxIcon`/`gfxImage` in its order (outside-draw, argument checks, unknown name, then budget, then frame-full). Memory rules of AGENTS.md; no new allocation.

**Never:** no change to `FrameReplay.cpp`'s drawing, the blits' output, or the other ops' costs (rect/clear/circle fills are out of scope: deferred). No `API_LEVEL_FROZEN` change. No edit to upstream files, generated headers, or `ci.yml`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| At the budget | commands whose clipped areas sum to exactly 1,048,576 | draw Ok, frame published | none |
| One over | the same plus one on-canvas pixel | `ScriptError`, "main.lua:N: ch.gfx.<kind>: the frame's icons and images cover over 1048576 pixels" | guard `Fault::Binding`, frame not published |
| Under pcall | overflow inside `pcall`, then more drawing | same error, fault sticky | as above |
| Off canvas | icon/image wholly off canvas, or at clamped ±70000 | charges 0 | none |
| Partly on | image overlapping an edge | charges the visible part only | none |
| Next frame | a frame after a full budget frame | counter restarts at 0 | none |

</frozen-after-approval>

## Code Map

- `lib/GameScript/DisplayList.h/.cpp` -- add `MAX_BLIT_PIXELS` (1,048,576), a `uint32_t blitted` counter reset in `clear()`, `bool chargeBlit(int64_t x, int64_t y, uint32_t w, uint32_t h, int32_t canvasW, int32_t canvasH)` (clamps x, y with the file's `clamp16`, int64 intersection, false and no change when it would pass the limit), and `uint32_t blitPixels() const`. Do not change `appendIcon`/`appendImage` signatures (DisplayListTest calls them).
- `lib/GameScript/ChBindings.cpp` `gfxIcon` (:161) / `gfxImage` (:180) -- after the name lookup, `chargeBlit` with the icon's drawn side (`GameIcons::DRAWN_PIXELS[size]`) or `images->spans[image].width/height`, and the context's `canvas->width/height`; on false raise a new `blitBudgetFull(L, kind)` through `guard->raise` (message buffer ≤ 96 B). Then the existing append/frameFull.
- `lib/GameIcons/GameIcons.h` -- move `DRAWN_PIXELS` here (`{SMALL_PIXELS, MEDIUM_PIXELS, 2 * MEDIUM_PIXELS}`, same comment); `src/games/GameIconBlit.h` keeps the name with `using GameIcons::DRAWN_PIXELS;` so FrameReplay, GameIconBlitTest, and `sourceFor` are unchanged. lib/GameScript cannot include src/games (layer check).
- `docs/crosshatch/api-level-1.txt` -- header grammar gains `draw <fn path> ink|opaque` and `name <what> <pattern>`; entries `limit frame_icon_image_pixels 1048576` (comment: counted how, why the value, the error), `limit icon_small_pixels 32`, `limit icon_medium_pixels 64`, `limit icon_large_pixels 128`, `draw ch.gfx.icon ink`, `draw ch.gfx.image opaque`, `name image [a-z0-9_]{1,32}` (comment: `icon` is never an image). Update the pcall-fault list comment. `lib/GameCore/ApiLevel.h` -- new `API_SURFACE_CRC` (ApiSurfaceTest prints it).
- `test/game_core/ApiLevelList.h` `parseEntry` -- body regexes for `draw` (`^(ch\.gfx\.[a-z_]+) (ink|opaque)$`) and `name` (`^([a-z_]+) ([!-~]+)$`). `test/game_core/ApiLevelTest.cpp` -- add malformed `draw`/`name` probes to its rejected list.
- `test/game_script/ApiSurfaceTest.cpp` -- `LimitsMatchTheCode` map gains the four limits (`MAX_BLIT_PIXELS`, `GameIconBlit::DRAWN_PIXELS[0..2]`; include `GameIconBlit.h`); new `DrawRulesMatchTheBlits` (draw entries == {icon: ink, image: opaque}, each names a listed fn; `inkRuns` of `circle` regular covers only ink and fewer than side²; `GameImageBlit::runs` of a fixture image covers w·h with both inks); new `ImageNameMatchesTheLoader` (`name image` pattern as std::regex vs `GameCore::imageNameOf(name + ".bmp")` for every single byte 0x00-0xFF, lengths 0, 1, `IMAGE_NAME_BYTES`, `IMAGE_NAME_BYTES + 1`; `icon` the one listed exception). `IconsMatchTheList` draws at x = -200 so its 330 icons cost no budget (comment why). Header comment lists the new kinds.
- `test/game_script/GfxBindingsTest.cpp` -- tests: exact budget and one over with a 480x800 checkerboard image (in-memory span via the fixture's `imageSpans`/`imagePixels`) and with 64/65 large icons; under pcall (sticky, unpublished, `Fault::Binding`); off-canvas and partly-on charges; counter resets next frame; worst-case frame at the budget replayed through `GameImageBlit::runs` and `GameIconBlit::inkRuns` emits at most N = 1,048,576 fills (checkerboard: exactly N).
- `test/game_script/fixtures/faults/icon_image_pixels.lua` + README "Fault scripts" row (the every-fault test needs both): 65 large icons at (0,0) inside pcall in draw, then text; the error text from the host run.
- `docs/crosshatch/game-icons.md` :3-8, :52 -- one sentence each pointing at the new entries.
- Do not touch `FrameReplay.cpp`, `GameImageBlit.h`, `GameAssets.cpp`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameIcons/GameIcons.h`, `src/games/GameIconBlit.h` -- move DRAWN_PIXELS -- one source for bindings and replay
- [x] `lib/GameScript/DisplayList.h/.cpp` -- budget counter and `chargeBlit` -- the limit lives with the frame
- [x] `lib/GameScript/ChBindings.cpp` -- charge in gfxIcon/gfxImage, raise through the guard -- R1's fix
- [x] `test/game_script/DisplayListTest.cpp` -- chargeBlit unit cases (exact, over, clamp, off/partly on, clear resets)
- [x] `test/game_script/GfxBindingsTest.cpp` -- the matrix above and the N-fills test
- [x] `docs/crosshatch/api-level-1.txt`, `lib/GameCore/ApiLevel.h`, `test/game_core/ApiLevelList.h`, `test/game_core/ApiLevelTest.cpp`, `test/game_script/ApiSurfaceTest.cpp` -- entries, grammar, checks, one CRC
- [x] `test/game_script/fixtures/faults/icon_image_pixels.lua`, `fixtures/README.md` -- the fault and its row
- [x] `docs/crosshatch/game-icons.md` -- pointers to the entries

**Acceptance Criteria:**
- Given the host suites, when run, then all pass, including every existing fixture test (icons pages, images, gallery, tracer, solo).
- Given x4pro and default builds and `sim.sh build x4pro`, when built, then they succeed, and the icons and images fixtures draw in the simulator.
- Given api-level-1.txt, when an entry's value differs from the code, then ApiSurfaceTest fails.

## Implementation Notes

- The fault fixture's README text (`main.lua:11: ch.gfx.icon: the frame's icons and images cover over 1048576 pixels`) is the host run's (SessionGameTest); the README says its texts are the x4pro simulator's, so the owner's simulator run should confirm it.
- `ApiLevelTest.GrammarRejectsMalformedEntries`: the new `draw`/`name` probes are a second array, so clang-format keeps the existing list's packing.
- `./bin/clang-format-fix` ran with the cached clang-format 21.1.8 (`/root/.cache/uv/archive-v0/NI2mFgP98NgibHTeMuBf7/bin`) first on PATH; the system one is 18. It changed nothing outside this ticket's paths.
- Mutation check: changing the list's `frame_icon_image_pixels`, `icon_large_pixels`, `draw ch.gfx.icon`, and `name image` values fails ApiSurfaceTest (LimitsMatchTheCode, DrawRulesMatchTheBlits, ImageNameMatchesTheLoader, and the CRC).

## Plan Change Log

## Review Triage Log

Pass 1 (2026-09-28). The four lenses ran as context-free subagents over `git diff 8bd18e86..` (worktree, `_bmad-output` excluded), all four returned before triage: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. Counts: high 0, medium 0, low 11, false 0, maybe-false 0; rejected 5 (low, as below), patched 7 groups, deferred 4.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | edge-case | `inkRuns` still walks each row of an icon wholly left or right of the canvas (empty inner loop) while `chargeBlit` charges 0; its comment says such an icon "costs nothing" | low | patch | Confirmed: x = -200, side 32 gives firstX 200 > endX 32, 32 row iterations. Add `if (endX <= firstX \|\| endY <= firstY) return;` as `GameImageBlit::runs` has (`git log -L`: inkRuns since b562ad3c, no guard ever) |
| 2 | edge-case (claim) | api-level comment says an off-canvas call is free while replay does residual row work | low | patch | Same root as #1; fixed by #1 |
| 3 | blind, edge-case, verification-gap | `ImageNameMatchesTheLoader` prints `\x` + decimal | low | patch | Confirmed at ApiSurfaceTest.cpp:552; print two hex digits |
| 4 | blind | budget never timed on a device | low | defer | AI-10's device run is the owner's (no device here); host time measured instead (Verification). Deferred with the device run |
| 5 | blind | game-api-seed section 6 and spine AD-7 do not list the budget fault | low | defer | Planning artifacts the owner updates (bmad-architecture); the level list is the contract. Deferred |
| 6 | blind | the budget comment quotes 474 x 788 as if every host's canvas | low | patch | Say the count is inside this host's canvas and name the X4 Pro and Sticky for the figure |
| 7 | blind | games cannot query image size or pixels left | low | defer | `ch.gfx.image_size` is already a freeze decision (retro AI-2); the budget makes it more relevant. Deferred to that decision |
| 8 | blind | `limit icon_*_pixels` uses `_pixels` for a side while `frame_icon_image_pixels` is an area | low | patch | Real in a contract whose names carry units; rename to `icon_small_side_pixels`, `icon_medium_side_pixels`, `icon_large_side_pixels` (one CRC) |
| 9 | blind, intent-alignment | `icon` reserved only in a comment; the pattern is not the loader's exact set | low | patch | Pattern `(?!icon$)[a-z0-9_]{1,32}` (ECMAScript lookahead); drop the test's exception |
| 10 | blind | `.BMP` in upper case not stated | low | reject | The entry's comment already says a file named otherwise is skipped; `looksLikeImage` logs it |
| 11 | blind | header grammar for `draw`/`name` looser than the parser | low | patch | State `<fn path>` is a ch.gfx function, `<what>` is `[a-z_]+`, the pattern has no spaces, and which work appended the kinds |
| 12 | blind, intent-alignment | the fill bound is tested on a copy of the replay loop, not `FrameReplay::draw` | low | defer | Pre-existing: `test/` builds no `FrameReplay.cpp` (retro AI-1/AI-2 harness). Deferred with it |
| 13 | blind | which error wins when both limits break; a charge left when append then fails | low | reject | Either way the game stops (sticky guard fault) and the list is cleared next frame; each message is true for the call that raised it |
| 14 | blind | no image-kind fault fixture | low | reject | A fault script is one main.lua with no images; the image message is pinned exactly in GfxBindingsTest. README text re-checked in the simulator (Verification) |
| 15 | blind | `DRAWN_PIXELS` indexed by `TextSize`; only its count is asserted | low | patch | Assert each index (Small 32, Medium 64, Large 128 by `TextSize`) beside the count assert |
| 16 | intent-alignment | `DrawRulesMatchTheBlits` does not assert the white swap | low | reject | `GameImageBlitTest.BlackDrawsAsConvertedAndWhiteSwapsTheInk` pins it |
| 17 | intent-alignment | `IconsMatchTheList` now draws off canvas | low | reject | Deliberate (plan); it checks names, sizes, weights as decoded commands, which draw position does not affect |

## Design Notes

- Area, not runs: a command's replay walks every visible pixel (`blackAt`/`inkAt`) and emits at most one fill per pixel, so an area budget bounds both pixel tests and fills; counting runs would need a walk at append time or per-image precomputation. Worst case under the bound: ≤ 1,048,576 pixel tests and ≤ 1,048,576 fills a frame (was ~786 M).
- Value: 1,048,576 ≈ 2.8 canvases (474x788 = 373,512 on X4 Pro and Sticky). The icons fixture's heaviest page is about 129,024 px (6 each at 32, 64, 128 px); the images fixture a few thousand; a full-canvas background image plus 64 medium icons is 635,656.
- `git log -L`: `gfxIcon` (b562ad3c, 890c1a69, 9197d046), `gfxImage` (890c1a69), `DisplayList::clear` (97dcf52f, fcae82b3). Guards: `drawTarget` raises outside draw (first, so the context message wins); `luaL_check*` are ordinary argument errors pcall catches; the unknown name raises through the guard; `frameFull` last. The budget goes between the name and the append: a name error is reported before any charge, and nothing over budget is appended. `clear()` resets `used`, `commands`, `hint`; it now resets the counter too.

- Review patch (triage #1): `git log -L` on `GameIconBlit::inkRuns` shows one commit, b562ad3c; its only guard is the null-bitmap / non-positive-size early return, which protects the bitmap reads, and it is kept. The new `endX <= firstX || endY <= firstY` return comes after the visible-range lines, as `GameImageBlit::runs` has it, so an icon wholly off the canvas walks no rows, matching `chargeBlit`'s zero charge.
- Review patches renamed the size limits to `icon_*_side_pixels` (a side, where `frame_icon_image_pixels` is an area) and made the image-name entry `(?!icon$)[a-z0-9_]{1,32}`, the loader's exact set; `API_SURFACE_CRC` is 0x52E8D03D after both.

## Verification

Host-test and firmware builds share one lock with other agents: wrap each whole chain as `flock /tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/build.lock sh -c '<commands>'` (never two builds at once). `pio` is `/root/.local/bin/pio`; never reinstall packages or touch certificates (a TLS or missing-package failure is a stop). The implementer runs only the host suites, the layer and upstream checks, and clang-format; the firmware and simulator builds and the flash measurement are run by the plan's owner after the commit is staged. Scratch files go under `/tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/e3r-1/`.

**Commands:**
- `flock /tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/build.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- all pass
- `flock /tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/build.lock sh -c 'pio run -e x4pro && pio run -e default'` -- success; x4pro firmware.bin size vs base 8bd18e86
- `flock /tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/build.lock sh -c '.claude/skills/run-crosshatch-player/sim.sh setup && ... build x4pro'` -- success; screenshots of icons and images fixtures
- `python3 scripts/check_upstream_touches.py`; `python3 scripts/check_layers.py` -- pass
- `./bin/clang-format-fix` twice -- nothing new

**Results (2026-09-28, worktree, after the review patches; lock held for each chain):**
- Host suites: 716/716 pass (`ctest --test-dir build/test`), including `SessionGameTest.EveryFaultScriptEndsWithTheReadmesText` with `icon_image_pixels.lua`, `ApiSurfaceTest.*`, `ApiLevelTest.*`, and the icons and images fixture tests.
- `pio run -e x4pro`: SUCCESS (RAM 101,824 B, Flash 5,898,682 B). `pio run -e default` (C3): SUCCESS (RAM 57,912 B, Flash 5,624,153 B).
- `sim.sh setup` and `sim.sh build x4pro`: SUCCESS. Simulator run (x4pro): the icons fixture draws black pages 1-2 and white page 1 unchanged; the images fixture draws both images in both inks and the clipped badge; `f-icon-image-pixels` ends in the error view with `main.lua:11: ch.gfx.icon: the frame's icons and images cover over 1048576 pixels`, the README row's text, and Back returns to Games.
- Screenshots (`_bmad-output/implementation-artifacts/e3r-1-screenshots/`): `x4pro-icons-black-1.png` and `x4pro-icons-black-2.png` (marks, every size, both weights, black on white, medium row on the light band), `x4pro-icons-white-1.png` (the same page white on black), `x4pro-images.png` (badge and dot, black and white, opaque over the bands, badge clipped at the right edge), `x4pro-fault-icon-image-pixels.png` (the budget fault's error view).
- `python3 scripts/check_layers.py`: passed. `python3 scripts/check_upstream_touches.py`: PASS; no changed or new path exists in `upstream/develop`.
- Host time (not device time): a 1,048,320-pixel checkerboard frame through `GameImageBlit::runs` with a 1-bit stand-in fill, 1,048,320 fills, 3.33 ms best of 5 (scratch benchmark, g++ -O2, Xeon 2.8 GHz; `GfxRenderer::fillRect` not included).
- Flash (measured): x4pro games-on `firmware.bin` 5,903,088 B at 8bd18e86 and 5,903,696 B at this work's commit (its code tree, which the amend recording this line leaves unchanged; built as 86a71cf4), +608 B. Method, the same for both: `git archive <commit>` plus every nested submodule's archive into an empty `scratchpad/e3r-1/flash`, then `pio run -e x4pro` under the lock.
- `./bin/clang-format-fix` twice with clang-format 21.1.8 first on PATH: no change the second time.


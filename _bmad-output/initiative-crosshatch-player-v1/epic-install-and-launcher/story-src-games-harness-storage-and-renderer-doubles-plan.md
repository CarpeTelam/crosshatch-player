---
title: 'src/games harness: storage and renderer doubles'
type: 'chore'
ticket: '1'
created: '2026-09-28'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '8f389a52e74a00fb26591e4c94067f6a7c2d6b47'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `test/` builds none of `GameAssets.cpp`, `FrameReplay.cpp`, or `GameIconDraw.cpp`, because they need `HalStorage`, `HalMemory::allocatePsram`, and `GfxRenderer`. Deferred-work `## 3.1`, `## 3.2` (third item), `## 3.9`, `## e3r-1` (second item), `## e3r-2` (loader half), and the image-header read-error path of f27dcefd are pinned only by simulator screenshots, and `GfxBindingsTest`'s `frontBlitFills` tests a copy of the replay's dispatch.

**Approach:** A host harness under `test/game_script/harness/` (R12): a directory-listing `HalStorage` fake with per-path failure injection, a PSRAM stub, and a recording `GfxRenderer` double, with the three sources built unchanged. Each pinned item gets a test that fails under a mutation of the guarded line, and each closed item is marked `Resolved by entry 1 (<commit>)`.

## Boundaries & Constraints

**Always:** `harness/CMakeLists.txt` includes every `*.cmake` in its folder, so each later entry adds a suite file and edits no shared list; each suite globs `src/games` and `src/activities/games` sources less a named exclusion list; only `touches` files change (`test/game_script/CMakeLists.txt` by one `add_subdirectory` line, `test/game_script/harness/**`, `GfxBindingsTest.cpp`'s `frontBlitFills`, `deferred-work.md`); a source seam is added only if a source will not compile against a double, and is named here.

**Never:** edit `lib/**`, `src/**`, or an upstream file; move the SDK pointer; reformat existing code.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Full load | 2 modules and 2 images in a game folder, listed in mixed order | One PSRAM block `[SourceSpan][ImageSpan][rows][text]`, spans in listing order, each offset the running sum | Ok |
| Folder result | Missing folder, a file at the path, unopenable, no Lua, only misnamed Lua, over the caps, a bad image, PSRAM refusal | `FolderMissing`, `FolderMissing`, `CannotRead`, `NoSources`, `BadSourceName`, `TooLarge`, `BadImage`, `OutOfMemory`, each logged, no block held | Result and log line |
| Header read | Read error, short read, file under 62 bytes | `CannotRead`, `CannotRead`, `BadImage` (truncated) | Result and log line |
| Pass 2 | Rows fail to read, a file lost, a file grew, a file swapped | `CannotRead`, block freed, views empty, no write past the block | Log line with counts |
| Long name | A name SdFat cannot fit, one the simulator cuts, one that fills the buffer | Said in the log, never loaded, never counted as misnamed | Log line |
| Icon replay | Each size, black and white, regular and fill, partly off the canvas | Every screen pixel matches the generated bitmap at canvas origin; no fill asked outside the canvas | Unknown icon or size: nothing drawn, logged |
| Image replay | Two images, partly off the canvas, an index past the table | Each image's own rows at canvas origin, opaque; past the table nothing drawn | Logged |
| Fill cost | Solid image, checkerboard at the budget | One fill per run; at most `MAX_BLIT_PIXELS` fills | n/a |

</frozen-after-approval>

## Code Map

- `src/games/GameAssets.cpp` -- `load`, `scanFolder`, `readFolder`, `addImage`: built unchanged against `HalStorage` and `HalMemory`.
- `src/games/FrameReplay.cpp`, `GameIconDraw.cpp`, `GameViewport.cpp` -- built unchanged against the renderer double (`getFontMap`, `getClipRect`, `setClipRect`, `clearScreen`, `fillRect`, `fillRectDither`, `drawLine`, `drawText`, `getLineHeight`, `getOrientedViewableTRBL`).
- `test/game_script/save_store_stubs/HalStorage.h` -- the seed for the fake; left as is (`GameSaveStoreTest` still uses it).
- `test/game_script/GfxBindingsTest.cpp` -- `frontBlitFills` becomes a call to `harness::replayFills`.
- `test/game_core/ConverterBmpLayout.h` -- the converter's 1-bit files, reused to build image files.

## Tasks & Acceptance

**Execution:**
- [x] `test/game_script/harness/stubs/` -- `HalStorage.h` (fake SD), `HalMemoryStub.{h,cpp}` (PSRAM with guard bands), `GfxRenderer.h` (recording double with a clip-honouring screen model), `Logging.h`, `HalDisplay.h`, `HalMemory.h` -- the doubles
- [x] `test/game_script/harness/CMakeLists.txt`, `game_assets_and_replay.cmake` -- shared libraries and the suite file; the globbed source set with a named exclusion list
- [x] `test/game_script/harness/{HarnessDoublesTest,GameAssetsLoadTest,FrameReplayTest}.cpp`, `HarnessSupport.h` -- the doubles' own tests and the pinned items
- [x] `test/game_script/harness/ReplayFills.{h,cpp}`, `test/game_script/GfxBindingsTest.cpp` -- `frontBlitFills` over the real replay
- [x] `test/game_script/CMakeLists.txt` -- one `add_subdirectory(harness)` line
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- mark the closed items; one `## 4.1` entry

**Acceptance Criteria:**
- Given the host suites, when built and run, then all pass, `GameHarnessTest` among them.
- Given a mutation of a guarded line (Verification below), when the suite runs, then the named test fails.
- Given `pio run -e x4pro` and `-e default`, then both build (no firmware source changed).

## Implementation Notes

Implemented directly in the planning session rather than by an implementation subagent: the investigation was already loaded, and a fresh subagent would have re-derived it.

No source seam was needed (the plan's unknown): `FrameReplay.cpp`, `GameIconDraw.cpp`, `GameViewport.cpp`, `GameAssets.cpp`, `GameSaveStore.cpp` and the other host-buildable `src/games` sources compile unchanged against the doubles.

## Plan Change Log

## Review Triage Log

Pass 1. All four lenses (blind hunter, edge-case hunter, verification gap, intent alignment) ran as context-free subagents over the staged diff and returned before triage. Verdict counts: 0 high, 10 medium, 10 low, 7 false, 0 maybe-false; every finding has a row. Patches were applied by the build agent (no implementation subagent was used).

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | blind, edge, gap | `(<commit>)` left literal in the closed entries | low | defer | A commit cannot name itself. The ticket's own format is `Resolved by entry 1 (<commit>)`; the orchestrator, or a follow-up commit, fills the hash. Reported at the top of the final report. |
| 2 | blind | Resolved entries keep the old text after "It read:" | false | reject | The same shape as the entries entry 7 and entry 10 closed (`## 3.6`, `## 3.7`); the old text is the record of what was closed. |
| 3 | blind, edge | `## 3.10`'s `sim.sh check` item marked resolved by 6973ec8, outside the harness | false | reject | The ticket's description says to mark it; `scripts/sim_sh_test.py` runs (4 tests, OK). |
| 4 | blind, gap | Plan unfinished: Verification empty, no mutation run recorded | false | reject | True when the lenses read it; Verification is filled in below with every mutation and its failing test. |
| 5 | blind | Plan cites a `touches` list that has no field | false | reject | `touches` is the ticket entry's key in `tickets.toml`; a fix would edit the plan. |
| 6 | blind, edge, gap, intent | The expected-pixel oracle shares `GameIconBlit::inkAt` and `GameImageBlit::blackAt` with the replay, and the comment overstates the independence | medium | patch | Both are pinned by `GameIconBlitTest` and `GameImageBlitTest`, and origin, scale, weight, bitmap choice, clip, and ink are the test's own; the comment now says exactly that. |
| 7 | blind | `GameScriptTest` gets the doubles' include directories through `game_harness_src` | medium | patch | It builds, but a later `GameScriptTest` source would silently compile against the doubles. `game_harness_replay_probe` now links `game_harness_src` PRIVATE, so only `ReplayFills.h`'s folder is on the path. |
| 8 | blind, intent | No drift guard between the doubles and the real `GfxRenderer`, `HalFile`, `HalDisplay` | medium | defer | A signature change breaks the firmware build, not the host suite. A guard needs `src/` or `lib/`, outside `touches`; recorded under `## 4.1`. |
| 9 | blind, intent | The clip model and SdFat rules are the double's own model | low | defer | Same entry under `## 4.1`. |
| 10 | blind, edge | The fake's `write` ignores `seek`; a handle is bound by path | low | defer | No `src/games` code seeks then writes; recorded under `## 4.1`. |
| 11 | blind | `fillsOutside` answers 0 when `keepCalls` is off | medium | patch | It now aborts with a message when `keepCalls` is false. |
| 12 | blind | `AnIconsInkRowsFillOneRunAtATime` asserts only `<= 256` | medium | patch | It now counts the ink runs from the bitmap and asserts exactly that many fills, for three icons. |
| 13 | blind | The doubles' own tests skip `failOpenWrite`, `failClose`, `rmdir`, `ensureDirectoryExists`, `openFileForRead`, a read on a folder | low | patch | `TheRemainingCardCallsBehaveAndFailPerPath` covers them. |
| 14 | blind, edge | PSRAM guards are checked only on free, so a leak or an overrun on an Ok path is not seen | medium | patch | `HarnessTest::TearDown` asserts no live block and no overrun (a test that damages a block on purpose opts out); `GameAssetsLoadTest::TearDown` releases first. |
| 15 | blind, edge | Image caps tested one past only; exact 32 images, exact 131,072 bytes, 61- and 62-byte files untested | medium | patch | `TheImageCapsAreInclusive` and `TheHeaderSizeIsTheEdgeBetweenTruncatedAndCheckedForItsFields`. |
| 16 | blind | The swapped-layout test asserts no log line | low | patch | It asserts the "changed since it was checked" line. |
| 17 | blind | No README for the harness | false | reject | The header of `harness/CMakeLists.txt` says how a suite is added and what is excluded; a new `.md` is not requested. |
| 18 | blind | Codec bytes and constants (47, 262152, 131072) hard-coded | low | reject | They mirror `GameSaveStoreTest`'s own vector and the log text the code prints; deriving them would restate the source under test. |
| 19 | edge | An image index equal to the table's count, or an empty table, never replayed | medium | patch | Both added to `AnImageIndexPastTheTableDrawsNothingAndIsLogged`. |
| 20 | edge | An unfit or filling name checked only beside a valid `main.lua` | medium | patch | The SdFat case is repeated alone and expects `NoSources`. |
| 21 | edge | An image that shrinks between the passes | low | patch | `AnImageThatShrankBetweenThePassesStillLoadsWithItsNewSize`. |
| 22 | edge | A refused icon size logs nothing asserted | low | patch | The `at 48 px` line is asserted. |
| 23 | edge | `GameViewport::forRenderer` and `setInsets` never exercised | medium | patch | `TheCanvasIsTheScreenLessTheBezelInsets`. |
| 24 | edge | A new firmware-only source in `src/games` breaks the harness build | low | reject | The ticket asks for a glob less an exclusion list; the `FATAL_ERROR` and the comment above the list say what to do. |
| 25 | edge | `replayFills` reports only `LOG_ERR` text, where the old helper failed on a missing bitmap | low | reject | Each such path logs with `LOG_ERR` (`FrameReplay.cpp`, `GameIconDraw.cpp`), and the exact fill counts would also miss. |
| 26 | intent | `## 3.2`'s third item and `## e3r-2` are only partly closed | false | reject | Intended: the `BadImage` mapping and the gesture drop need `GameMatchActivity` (entry 4); the entries say so. |
| 27 | gap | Whether `GameScriptTest` links with the harness archives | false | reject | It builds and links; the full host suite passed (see Verification). |

## Design Notes

- **Doubles shadow, never replace.** The stubs directory is the first include path, so `<HalStorage.h>`, `<GfxRenderer.h>`, `<HalDisplay.h>`, and `<Logging.h>` resolve to doubles; `<HalMemory.h>` forwards to the real header, and the stub defines only `allocatePsram` and the deleter.
- **Two views of the renderer.** The double records each call with the clip in force (what the code asked for) and keeps a screen model that honours the clip (what would show). Clip tests assert on the first (`fillsOutside`), so a replay that stops clipping is caught even though the renderer's own clip would hide it.
- **The PSRAM stub guards the block.** 4 KiB guard bands and poison, checked on free, so a write past what pass 1 sized fails a test cleanly instead of corrupting the heap.
- **Fake folder semantics.** Children list in creation order; `getName` returns 0 for a name that does not fit (SdFat), or cuts it with `getNameCuts` (the simulator); `onRewind` lets a test change the card between the loader's two passes; `failReadAt` (byte offset) fails only the row read of a two-pass reader; `shortReadAt` gives a short read.
- **`frontBlitFills` history.** `git log -L` shows one commit, 131fe505; its one guard was `ADD_FAILURE` for an icon with no bitmap. `harness::replayFills` keeps it as the `errors` list (`LOG_ERR` lines from an icon or image the replay could not draw), which `frontBlitFills` reports as failures.
- **Excluded from the shared source set:** `GameClock.cpp`, `GameRandom.cpp` (esp_timer, esp_random), `GameVM.cpp` (FreeRTOS, Arduino), `ForkReleaseProbe.cpp` (HTTPS client), `GamesBuildAnchor.cpp` (link anchor), `GameMatchActivity.cpp`, `GamesListActivity.cpp` (activity framework: the next harness entries).

## Verification

**Commands (all under `flock <lock>`, on the final tree):**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- 780 of 780 passed (`GameHarnessTest` 61 tests; `GfxBindingsTest` unchanged in count, its fill counts now from the real replay; 708 before this entry).
- `pio run -e x4pro` -- SUCCESS (3 min 53 s); `pio run -e default` -- SUCCESS (6 min 17 s). No file under `src/` or `lib/` changed (`git diff 8f389a52 -- src lib` is empty), so both build what the baseline built; run before the review patches, which touch `test/` only.
- `python3 scripts/sim_sh_test.py` -- 4 tests OK: `## 3.10`'s `sim.sh check` item is resolved by 6973ec8 (a doc mark, no mutation).
- `python3 scripts/check_upstream_touches.py` -- PASS (no upstream file changed).
- `./bin/clang-format-fix` twice -- second run and `git status` show nothing new; it changed only this entry's files.
- No CI gate or workflow changed, so no fresh-tree run applies.

**Mutations (one guarded line changed at a time in the source under test, host suites rebuilt and run, source restored).** Every mutation below made the named test fail; each run is a separate build of `GameHarnessTest` and `GameScriptTest`, failing names taken from the run.

**`## 3.1` (icon origin, size, ink, clip)**

| Mutation | Guarded line changed | Test that fails |
|---|---|---|
| 3.1a | `GameIconDraw.cpp`: icon origin dropped | `FrameReplayTest.AnIconIsDrawnFromTheCanvasOriginInEachSize`, `FrameReplayTest.AnIconPartlyOffTheCanvasIsClippedToTheCanvasBeforeItIsDrawn` (+2 more) |
| 3.1b | `FrameReplay.cpp`: icon size index fixed | `FrameReplayTest.AnIconIsDrawnFromTheCanvasOriginInEachSize`, `FrameReplayTest.AnIconPartlyOffTheCanvasIsClippedToTheCanvasBeforeItIsDrawn` (+3 more) |
| 3.1c | `FrameReplay.cpp`: icon ink inverted | `FrameReplayTest.AnIconIsDrawnFromTheCanvasOriginInEachSize`, `FrameReplayTest.AnIconPartlyOffTheCanvasIsClippedToTheCanvasBeforeItIsDrawn` (+2 more) |
| 3.1d | `FrameReplay.cpp`: icon clip widened | `FrameReplayTest.AnIconPartlyOffTheCanvasIsClippedToTheCanvasBeforeItIsDrawn`, `FrameReplayTest.AnIconWhollyOffTheCanvasCostsNothing` (+1 more) |

**`## 3.2` third item (spans, offsets, pass-2 re-read, image offset)**

| Mutation | Guarded line changed | Test that fails |
|---|---|---|
| 3.2a | `GameAssets.cpp`: image span offset = 0 | `GameAssetsLoadTest.ModulesAndImagesLandInOneBlockAtTheirOwnOffsets` |
| 3.2b | `GameAssets.cpp`: source span offset = 0 | `GameAssetsLoadTest.ModulesAndImagesLandInOneBlockAtTheirOwnOffsets` |
| 3.2c | `GameAssets.cpp`: pass-2 budget re-check dropped | `GameAssetsLoadTest.AnImageThatGrewBetweenThePassesIsRefusedWithoutOverrunningTheBlock` |
| 3.2d | `GameAssets.cpp`: image spans placed at block start | `GameAssetsLoadTest.AModuleThatAppearsInPassTwoIsNotReadPastThePassOneCount`, `GameAssetsLoadTest.ModulesAndImagesLandInOneBlockAtTheirOwnOffsets` |
| 3.2e | `FrameReplay.cpp`: replay image origin dropped | `FrameReplayTest.AnImageIsDrawnFromItsOwnRowsAtTheCanvasOrigin`, `FrameReplayTest.AnImagePartlyOffTheCanvasIsClippedBeforeItIsDrawn` (+1 more) |
| 3.2f | `FrameReplay.cpp`: replay ignores the span offset | `FrameReplayTest.AnImageIsDrawnFromItsOwnRowsAtTheCanvasOrigin` |

**f27dcefd (image-header read error)**

| Mutation | Guarded line changed | Test that fails |
|---|---|---|
| f27a | `GameAssets.cpp`: short header read no longer an error | `GameAssetsLoadTest.AShortHeaderReadOfALongEnoughFileIsCannotRead` |
| f27b | `GameAssets.cpp`: header read error ignored | `GameAssetsLoadTest.AFailedHeaderReadIsCannotReadNotBadImage`, `GameAssetsLoadTest.AShortHeaderReadOfALongEnoughFileIsCannotRead` |
| f27c | `GameAssets.cpp`: file under the header wanted a full header | `GameAssetsLoadTest.AFileUnderTheHeaderIsATruncatedImageNotAReadError`, `GameAssetsLoadTest.TheHeaderSizeIsTheEdgeBetweenTruncatedAndCheckedForItsFields` |

**`## 3.9` (fill weight)**

| Mutation | Guarded line changed | Test that fails |
|---|---|---|
| 3.9a | `FrameReplay.cpp`: weight flag dropped in replay | `FrameReplayTest.AnIconsInkRowsFillOneRunAtATime`, `FrameReplayTest.TheFillWeightDrawsTheFillBitmapAndTheRegularWeightTheRegularOne` |
| 3.9b | `FrameReplay.cpp`: weight flag inverted in replay | `FrameReplayTest.AnIconIsDrawnFromTheCanvasOriginInEachSize`, `FrameReplayTest.AnIconPartlyOffTheCanvasIsClippedToTheCanvasBeforeItIsDrawn` (+3 more) |
| 3.9c | `GameIconDraw.cpp`: fill ignored in drawGameIconAt | `FrameReplayTest.AnIconsInkRowsFillOneRunAtATime`, `FrameReplayTest.TheFillWeightDrawsTheFillBitmapAndTheRegularWeightTheRegularOne` |

**`## e3r-1` second item (real replay fills)**

| Mutation | Guarded line changed | Test that fails |
|---|---|---|
| e1a | `FrameReplay.cpp`: replay fills an image run per pixel | `FrameReplayTest.EachRunOfOneColourIsOneFillNotOnePerPixel` |
| e1b | `GameIconDraw.cpp`: replay fills an icon run per pixel | `FrameReplayTest.AnIconsInkRowsFillOneRunAtATime` |
| e1c | `FrameReplay.cpp`: replay image clip widened | `FrameReplayTest.AnImagePartlyOffTheCanvasIsClippedBeforeItIsDrawn`, `GfxBindingsTest.AFrameAtTheBudgetReplaysInAtMostThatManyFills` |
| e1d | `FrameReplay.cpp`: replay icon clip widened (GfxBindingsTest sees it) | `FrameReplayTest.AnIconPartlyOffTheCanvasIsClippedToTheCanvasBeforeItIsDrawn`, `FrameReplayTest.AnIconWhollyOffTheCanvasCostsNothing` (+1 more) |

**`## e3r-2` loader half (results, release, long names)**

| Mutation | Guarded line changed | Test that fails |
|---|---|---|
| e2a | `GameAssets.cpp`: BadSourceName never returned | `GameAssetsLoadTest.ANameThatFitsButIsTooLongForAModuleIsMisnamedNotLong`, `GameAssetsLoadTest.NoLuaFilesIsNoSourcesAndOnlyMisnamedOnesIsBadSourceName` |
| e2b | `GameAssets.cpp`: release() after a pass-2 failure dropped | `GameAssetsLoadTest.AFileTheFolderLostBetweenThePassesIsCannotReadWithTheCounts`, `GameAssetsLoadTest.AModuleReadThatFailsOrComesUpShortReleasesTheBlock` (+4 more) |
| e2c | `GameAssets.cpp`: long-name line dropped | `GameAssetsLoadTest.ALongNameTheSimulatorCutsIsSaidWithItsPrefixAndNeverLoaded`, `GameAssetsLoadTest.ANameThatFillsTheBufferIsNeverClassified` |
| e2d | `GameAssets.cpp`: unreadable-name line dropped | `GameAssetsLoadTest.ANameSdFatCannotFitIsSaidAndNeverLoaded` |
| e2e | `GameAssets.cpp`: pass-2 module past the count is read | `GameAssetsLoadTest.AModuleThatAppearsInPassTwoIsNotReadPastThePassOneCount` |
| e2f | `GameAssets.cpp`: pass-2 text bound dropped | `GameAssetsLoadTest.ModuleTextThatGrewBetweenThePassesIsRefusedWithoutOverrunningTheBlock` |
| e2g | `GameAssets.cpp`: BadImage never returned | `GameAssetsLoadTest.ADamagedImageIsBadImageWithItsReasonLogged`, `GameAssetsLoadTest.AFileUnderTheHeaderIsATruncatedImageNotAReadError` (+3 more) |
| e2h | `GameAssets.cpp`: module count cap exclusive | `GameAssetsLoadTest.TheModuleCapsAreInclusive` |
| e2i | `GameAssets.cpp`: text cap exclusive | `GameAssetsLoadTest.OneByteOverTheTextLimitIsTooLarge`, `GameAssetsLoadTest.TheModuleCapsAreInclusive` |
| e2j | `GameAssets.cpp`: store restored even on failure | `GameAssetsLoadTest.TheSavedStoreIsRestoredOnlyOnceTheLoadSucceeds` |
| e2k | `GameAssets.cpp`: name that fills the buffer classified | `GameAssetsLoadTest.ALongNameTheSimulatorCutsIsSaidWithItsPrefixAndNeverLoaded`, `GameAssetsLoadTest.ANameThatFillsTheBufferIsNeverClassified` |
| e2l | `GameAssets.cpp`: folder not-a-folder passes | `GameAssetsLoadTest.AMissingFolderOrANonFolderIsFolderMissing` |

Two more: dropping the replay's image-index bound to `>` (`FrameReplay.cpp`, `command.image > images.count`) makes `AnImageIndexPastTheTableDrawsNothingAndIsLogged` read past the table and crash the suite (a detected failure, not a clean one); and making a failed header read return `WrongLayout` instead of `Truncated` changes nothing (the caller ignores the value once `readFailed` is set), so it is an equivalent mutation and pins nothing.

For `## e3r-1`'s second item, mutations e1c and e1d are the ones the old `frontBlitFills` copy could not see: widening the real replay's image clip fails `GfxBindingsTest.AFrameAtTheBudgetReplaysInAtMostThatManyFills`, and widening its icon clip fails `GfxBindingsTest.OnlyTheVisiblePixelsCountTowardTheBudget`.

**Unknown, settled:** `FrameReplay.cpp` and `GameIconDraw.cpp` compile against the renderer double unchanged; no source seam was added.

**Screenshots:** none (a host harness, no screen change).

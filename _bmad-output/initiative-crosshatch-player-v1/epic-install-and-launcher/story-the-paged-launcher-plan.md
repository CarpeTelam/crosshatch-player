---
title: 'The paged launcher (GamesLauncherActivity)'
type: 'feature'
ticket: '8'
created: '2026-09-29'
status: 'built'
baseline_revision: '0880c7463b77ac7d4b9a59d6ef8211a80143e357'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/docs/contributing/touch-and-ui.md'
  - '{project-root}/src/games/GameIconDraw.h'
  - '{project-root}/lib/GameCore/GameImages.h'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Home, Games opens a minimal, text-only list that hides every game not startable solo, so a package whose `api` the host lacks vanishes without a reason, and no row shows the package's icon.

**Approach:** Rename `GamesListActivity` to `GamesLauncherActivity` and make it R7's paged launcher: every registry game is a row of icon and name; the icon is the package `icon.bmp`, else the manifest `icon` by name and `icon_weight`, else `game-controller`; a game `Manifest::check` finds not Ok shows a short `tr()` reason and does not start.

## Boundaries & Constraints

**Always:** Keep the install-on-open, the failure popup, Back going straight Home, and the match started with today's `GameMatchActivity(renderer, mappedInput, manifest)`. Screens reach `lib/GameIcons` only through `src/games`. Allocate with `makeUniqueNoThrow`; no growing container; locals under 256 B; log with `LOG_*`; text through `tr(STR_GAMES_*)` with keys added to `english.yaml` only. Ledger row 5's text and its `ActivityManager.cpp` edits change in this commit, guarded by `FREEINK_CAP_GAMES`. Assumption for entry 14: a game with neither `icon.png` nor a manifest `icon` shows `game-controller` (inception, 2026-09-28).

**Never:** Touch `ActivityManager.h`, `src/activities/home/**`, `GameMatchActivity.*`, the installer, `GameSaveStore.*`, or entry 1's and entry 4's shared harness files (add a new file instead). No mode picker, Continue, or remove (entries 9 to 12). No per-row, per-frame heap allocation or SD read.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Row with `icon.bmp` | valid 64x64 `icon.bmp` and a manifest `icon` | the bmp's rows drawn; source `icon.bmp` | none |
| Manifest icon | no `icon.bmp`; `icon` known, weight regular or fill | that library icon in that weight | none |
| Fallback | neither, or `icon.bmp` damaged and no `icon`, or `icon` the library lacks | `game-controller` | logged, row still listed |
| Unavailable | `api` above the host's | row lists with a short reason under the name; a tap or Confirm starts nothing | none |
| Paging | more games than fit | first page ends before the last game; a swipe or page key shows the next rows | none |
| No games | empty registry | "No games found" | none |
| Row memory | icon cache allocation fails | rows fall back to library icons, all still listed | `LOG_ERR` |

</frozen-after-approval>

## Code Map

- `src/activities/games/GamesListActivity.{h,cpp}` -- `git mv` to `GamesLauncherActivity.*`, name `"GamesLauncher"`. Keep `installInbox`, `reasonText`, `handleCustomInput`, `onBackButton`, the note popup. Replace `rows`/`rebuildRows` with a `ListProps::rowProvider` (precedent: `src/activities/home/FileBrowserActivity.cpp` `provideRow`); drop the solo filter in `loadGames`.
- `src/games/GameRegistry.h` -- `Entry{manifest, check, pkgHash}`, sorted by name; `check` is the row's availability.
- `lib/GameCore/GameImages.h` -- `checkImageHeader` and `ImageHeader`; `lib/GameCore/PackageLimits.h` `ICON_PIXELS` (64); `Manifest::{icon, iconWeight}` and `CheckReason`.
- `src/games/GameIconBlit.h`, `lib/GameIcons/GameIcons.h` -- `find`, `PackedReader`; a decoded library row is bit 0 = ink, MSB first: the SDK's `BitmapFormat::Mask1` exactly, as is `icon.bmp` (bit 1 = white) top-down.
- `freeink-sdk/.../lists/list.h` -- `ListItem::icon` (a `BitmapRef` the caller owns), `ListProps::iconSize`, `subtitle`, `rowProvider` (item pointers valid until the next provider call; measure and draw of a row share one call).
- `src/activities/ActivityManager.cpp` (row 5) -- include, `"GamesList"` in `goHome`, `goToGames()`; `docs/crosshatch/upstream-touches.md` row 5 text.
- `test/game_script/harness/{games_list.cmake,GamesListTest.cpp}` -- `git mv` to `games_launcher.cmake`, `GamesLauncherTest.cpp`; also rename in `harness_base.sources.cmake`, `match.cmake`; `list_stubs/` stays. Doubles: `screen_stubs/` (`RecordingTarget::bitmap` only counts), `harness::bmpFile`, `installerscript`, `hostcaps`.
- `test/game_core/ManifestTest.cpp` comment before `EveryFixtureManifestIsListed` names `GamesListActivity::readManifest`.

## Tasks & Acceptance

**Execution:**
- [ ] `src/games/GameRowIcon.{h,cpp}` -- new, whole-file `#if FREEINK_CAP_GAMES`, namespace `GameRowIcon`: `SIDE`=`ICON_PIXELS`, `BYTES`=512; `enum class Source {PackageBmp, Library, Fallback}`; `hasPackageIcon(id)`; `readPackageIcon(id, uint8_t* bits)` (open `/.games/<id>/icon.bmp`, header through `checkImageHeader` with budget = file size, exactly 64x64, then the rows; false and logged otherwise); `hasLibraryIcon(name)`; `renderLibraryIcon(name, fill, bits)` (decode the 64 px medium bitmap through `PackedReader` with `GameIconBlit::sourceFor`, the engine `drawGameIcon` uses); `chooseSource(packageRead, manifestIcon)` -- the source order in one place; `FALLBACK_NAME` "game-controller".
- [ ] `src/activities/games/GamesLauncherActivity.{h,cpp}` -- after `loadGames` (no filter), `loadIcons()` sizes a dense `packageIcons` cache (BYTES per game that has an `icon.bmp`) and `packageSlot[]`, both with fallbacks on OOM, and logs each row's source at `LOG_DBG` ("Icon for <id>: icon.bmp | library <name> regular|fill | fallback game-controller"). `provideRow` sets label, `actionValue`, a `Mask1` 64x64 `icon` (cache slot, or the activity's 512 B scratch filled by `renderLibraryIcon`), and, when `!check.ok()`, the reason as `subtitle`. `props.iconSize = SIDE`. `activateIndex` returns (logged) unless `check.ok()` and the check offers `MODE_SOLO`, and starts the match as today.
- [ ] `lib/I18n/translations/english.yaml` -- append `STR_GAMES_UNAVAILABLE_NEWER` "Needs newer firmware", `_OLDER` "Too old for this firmware", `_SEATS` "Needs more players", `_MODE` "Its modes are not offered here", `_INVALID` "Its manifest is not valid".
- [ ] `src/activities/ActivityManager.cpp`, `docs/crosshatch/upstream-touches.md` -- row 5: include, `"GamesLauncher"` in the `goHome` mapping, `makeUniqueNoThrow<GamesLauncherActivity>`; row text names the launcher.
- [ ] `test/game_script/harness/` -- renames above; `GamesLauncherTest.cpp` keeps entry 5's tests except the solo-only ones (rewritten: every registry game is listed, `pass` on lists a pass-only game that does not start), adds paging (about 25 games, first page short of the last, `swipeDirection(Up)` shows later rows), the icon source order via logs and one bitmap per drawn row (`RecordingTarget::bitmaps`), an unknown manifest `icon` and a damaged `icon.bmp` falling back, an unavailable row that shows `tr()` reason and starts nothing by tap and Confirm; `GameRowIconTest.cpp` in the same suite checks pixels: `readPackageIcon` equals `harness::rowsOf` of the file, `renderLibraryIcon` equals `GameIconBlit::inkAt` for all 4,096 pixels in both weights, truncated, non-64, and non-1-bit files refused. Keep `-Werror=switch`.
- [ ] `test/game_core/ManifestTest.cpp` -- comment only.

**Acceptance Criteria:**
- Given 25 installed games, when Games opens, then page 1 does not show the last name and paging shows it, every row drawing one icon.
- Given a game whose `api` is 2, when its row is tapped or selected and Confirmed, then no match starts and the reason is on its row.
- Given `check_layers.py` and `check_upstream_touches.py`, then both pass; the C3 `default` env builds.

## Implementation Notes

Route full: 100+ lines across many files. The SDK list draws a caller-owned `BitmapRef` (`ListItem::icon`), so no icon is drawn beside the row through the renderer. Implemented directly in the build session rather than by a subagent, because the design's rationale (the Mask1 finding, the provider contract) sat in this session and a cold handoff would have lost it.

- `hasPackageIcon` (an existence check) runs before `readPackageIcon` so the cache is sized by the games that have a file; a slot is marked `0` in the first pass and set to its real index, or `NO_SLOT`, in the second.
- The screen double's `RecordingTarget::bitmap` only counts, and its file is entry 4's, so the suite pins the icon source by the launcher's `LOG_DBG` line per row (`Icon for <id>: ...`), one bitmap per drawn row by the count, and the pixels in `GameRowIconTest.cpp`; the screenshots show the drawn pixels.
- A new `english.yaml` key is used in the launcher only; the five `STR_GAMES_UNAVAILABLE_*` keys are English-only (other languages fall back).

## Plan Change Log

## Review Triage Log

Pass 1. The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) each ran as a context-free subagent over the staged diff and all four returned. Verdict counts: high 0, medium 3, low 9, false 2, maybe-false 0 (the intent-alignment report is descriptive and files no defect).

| # | Finding (lens) | Verdict, route | Evidence and action |
|---|---|---|---|
| 1 | Row draws its icon by `provideRow`'s own choice while the logged and tested choice is a second copy in `loadIcons` (blind, verification-gap) | medium, patch | One `choiceOf(index)` now serves the log and the draw; `GameRowIcon::choose` (source, name, weight) is unit-tested for every branch. |
| 2 | The screen double's `bitmap()` only counts, so the drawn pixels are unobserved (verification-gap, intent-alignment) | medium, patch + defer | Pixels are pinned where they are made (`GameRowIconTest`: `readPackageIcon` = file rows, `renderLibraryIcon` = independent PackBits decode for all icons in both weights, and `GameIconBlit::inkAt` on three icons) and the simulator screenshots show them drawn. Capturing the `BitmapRef` at draw time needs a change to `screen_stubs/components/UiAppHost.h`, entry 4's file (`stays_out`): deferred under `## 4.8`. |
| 3 | Each reason is asserted only somewhere in the text, and `ApiTooOld` never (verification-gap, blind) | medium, patch | `lineAfter` ties each reason to its row; `hostcaps::script().minApi` (a field added to entry 5's `list_stubs`) makes api 1 too old and the test expects `STR_GAMES_UNAVAILABLE_OLDER`. |
| 4 | A game Ok but not startable in solo shows no reason and ignores a tap (blind, edge-case) | low, patch | `startable()` decides both the row and `activateIndex`; the row shows `STR_GAMES_UNAVAILABLE_MODE`. Unreachable on today's host (`pass` and `nearby` are off), pinned with `pass` on. |
| 5 | A refused start returns before any repaint (edge-case, blind) | low, patch | `requestUpdate()` so the selection the tap moved is drawn; `clearTapFlash` stays out, since the row does not leave. |
| 6 | A library icon that fails to render leaves the row with no icon (blind, edge-case) | low, patch | `provideRow` renders `game-controller` then; the header's comment says what a partial render keeps. Only a generator bug reaches it. |
| 7 | `goHome`'s `"GamesLauncher"` literal must equal the constructor's (verification-gap) | low, patch | `GamesLauncherActivity::NAME` is used by both; the simulator shows Back returning to Home with Games selected (`back-reselects-games.png`). The real `ActivityManager` stays unbuilt on the host (deferred item 4.5, unchanged). |
| 8 | Stale comments: `match.cmake`, a test comment naming `games_list.cmake`, `ManifestTest` (blind, edge-case) | low, patch | Reworded. `harness_base.sources.cmake`'s "next harness entries" comment predates this change: not touched. |
| 9 | Brittle assertions: "Needs"/"Its " prefix count, `substr(5)`, `EXPECT_GE` (blind) | low, patch | Replaced by row-tied `lineAfter` and index lookup. |
| 10 | Wording of the reason strings (blind) | low, patch | Reworded to "Too old to run on this firmware", "Needs more players than fit here", "None of its modes work here", "The game's manifest is not valid". |
| 11 | `static_assert` hard-codes 8 and its comment names manifest.json; duplicate `reset()` (blind) | low, patch | Uses `GAMES_DIR`'s length and `MAX_ID_BYTES`; duplicate removed. |
| 12 | Plan says `renderLibraryIcon` is checked against `inkAt` (edge-case claim) | low, patch | Added `TheRowBitmapHoldsTheInkGameIconBlitDraws`. |
| 13 | Allocation-failure paths untested; worst-case heap unstated (blind, edge-case) | low, defer | `makeUniqueNoThrow` cannot be made to fail on the host; the fallback is one branch. The ceiling is in Design Notes; the untested paths are deferred under `## 4.8`. |
| 14 | No `game-icons.md` section on `GameRowIcon` (blind) | low, defer | `docs/crosshatch/game-icons.md` is outside `touches`; deferred with the stale `formats.md` line. |
| 15 | Library icon decoded per visible row per repaint (blind) | false | A PackBits decode of at most 512 B and one binary search per row: no heap, no SD; the sim repaints without delay. |
| 16 | The shared 512 B scratch relies on unchecked SDK ordering (blind) | false | `list.h` lays out and draws a row inside one loop iteration after one provider call (lines 580 to 700), and `FileBrowserActivity::provideRow` relies on the same contract. |
| 17 | `hasPackageIcon` and `readPackageIcon` can disagree if the card changes (edge-case) | low, reject | Harmless: an unread slot becomes `NO_SLOT`; the fix adds a header-check pass for no user-visible gain. |
| 19 | (Found by the simulator, not a lens.) `StateDisabled` on an unavailable row hid the selection when the row was selected | low, patch | Dropped; the reason under the name is the signal. Screenshot `unavailable-row-selected.png` shows the selection on the row. |
| 18 | Deleted guards of `loadGames`/`rebuildRows` (edge-case deletion) | false | Solo filter is the requirement removed; the rows-OOM path has no successor because no row array exists; `activateIndex` keeps the OOM check. |

## Design Notes

- **Unknown resolved:** yes, `ListItem::icon` takes the caller's bitmap. Library icons are rasterised into it by `GameRowIcon::renderLibraryIcon`, which reuses `GameIconBlit` (the decoder `drawGameIcon` draws from), because `drawGameIcon` paints on a `GfxRenderer` and a list row is painted by the SDK. Pixels are identical to `drawGameIcon`'s; the row inverts with selection for free. Flag this in the report.
- Package `icon.bmp` bytes are cached once per list load (dense, 512 B per game that has one, at most `GameRegistry::MAX_GAMES` = 64: a ceiling of 64 x 512 = 32,768 B plus 128 B of slots, one block, with a fallback to library icons when the block cannot be had); library icons decode into one 512 B scratch per provider call (render task only, PackBits decode, no heap, no SD).
- Guards in the code this replaces, kept or gone: the solo filter in `loadGames` goes (R7), but `activateIndex` keeps a solo-mode guard because the mode picker (entry 9) is not built and today's constructor starts solo; `rebuildRows`' OOM path (`count = 0`, nothing openable without rows) has no successor because no row array exists; `activateIndex` keeps the OOM check and `clearTapFlash` before leaving; `onBackButton` keeps `goHome` (not `finish`) so Home reselects Games, matched by the new activity name in `ActivityManager.cpp`.

## Verification

All host, firmware, and simulator builds ran under the shared build lock, on the tree of the commit (the same sources; the plan and screenshots are the only later edits).

**Commands and results:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- 1,103 of 1,103 pass (1,090 at the base: `GamesLauncherHarnessTest` has 31 tests, 25 launcher and 6 row-icon, in place of entry 5's 18 list tests, two of them rewritten and the rest kept).
- `pio run -e x4pro` (through `check_flash_budget.py build on`), `pio run -e sticky`, `pio run -e default` -- all SUCCESS at the final tree. `sim.sh build x4pro` -- SUCCESS.
- `python3 scripts/check_layers.py` -- 434 include edges follow the layer table; `python3 scripts/check_layers_test.py` -- 51 tests OK. `python3 scripts/check_upstream_touches.py` -- see the report (run on the commit).
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`, all exit 0. x4pro `firmware.bin`: 5,896,208 B games on (`.pio/build`), 5,677,472 B off (`.pio/build-games-off`), difference +218,736 B (37,264 B under the 256,000 B gate); static internal RAM +776 B (`.dram0.bss` +8, `.iram0.text` +684, `.iram0.text_end` +84; 248 B spare); 42 game objects, no static initializer, no mutable static over 64 B. Over the epic base at `962ae61` (+228,496 B, +776 B): -9,760 B flash, +0 B static RAM. Over the combined measurement at `40ec2e01` (+217,040 B): +1,696 B flash, +0 B static RAM. Pass bar (+240,496 B, +808 B): met, 21,760 B of flash and 32 B of RAM to spare for entries 9 to 13. Method: every figure is the tool's own two builds of the same tree; the two earlier figures are the orchestrator's recorded measurements, quoted, not re-measured here.

**Simulator (x4pro, 12 packages installed from the inbox by the installer; the packages were made by a scratch script: `scripts/pack_game.py` for the eleven that pack, and a hand-built stored zip, the test-side writer, for the `api` 2 game `pack_game.py` refuses).** Screenshots in `story-launcher-screenshots/`, each looked at:
- `_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/story-launcher-screenshots/page1.png` -- page 1 of the list: Battleship (`boat`), Card Sharks (`spade`, fill), Counter (no icon: `game-controller`), Dice Duel (`dice-six` regular) beside Dice Duel Deluxe (`dice-six` fill), Finish Line (`flag-checkered`, fill), Hearts (`heart`), and Moon Puzzle, whose icon is its `icon.png` converted to `icon.bmp`; the first row is selected.
- `.../page2.png` -- page 2 after one swipe: the rows continue from Dice Duel Deluxe to Trophy Room, with Timer and Tracer on `game-controller`, Moon Puzzle again, and Zeta Future, the `api` 2 game, with "Needs newer firmware" under its name.
- `.../unavailable-row-selected.png` -- a tap on Zeta Future starts nothing (log: `Not starting zeta-future: api newer than this host supports`) and the selection moves to the row.
- `.../back-reselects-games.png` -- Back from the launcher lands on Home with the Games row selected (`GamesLauncherActivity::NAME` matches the `goHome` mapping).
- Also run: a tap on Counter starts it (`Started counter`).

**Manual checks:**
- The `Icon for <id>: ...` log lines in the simulator name the source of every row, matching the pictures.

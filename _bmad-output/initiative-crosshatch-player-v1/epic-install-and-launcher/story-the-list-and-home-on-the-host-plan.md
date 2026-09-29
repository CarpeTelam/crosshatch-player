---
title: 'The list and Home on the host'
type: 'chore'
ticket: '5'
created: '2026-09-29'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: 'ba0c67be46a6ec9ba8ccd0ae2417514d4a40b1af'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GamesListActivity` and the cover-grid Home have no host test: the list's installer reasons, its `Manifest::check` and folder filters, its open and Back, and the Home tab order against `HomeActivity`'s index mapping (`deferred-work.md` `## 3.6`, `## 4.6`, and two `## e2r` items) are checked only by hand on a device or in the simulator.

**Approach:** Two host suites over entry 4's screen doubles. `games_list.cmake` builds the real `GamesListActivity` (installer scripted) and tests it; `home.cmake` builds the real `HomeActivity.cpp` and `CoverGridHomeUi.cpp` unchanged, as configure-time copies beside small header doubles, and taps every tab. Each pinned item gets a mutation of its guarded line and the test that fails under it.

## Boundaries & Constraints

**Always:** New files under `test/game_script/harness/` only, plus the plan, screenshots folder if any, and `deferred-work.md` (resolve closed items; new items under `## 4.5`). Home's two source files are built byte for byte (`configure_file COPYONLY`). Entry 4's tests must keep passing.

**Never:** Edit `src/**`, `lib/**`, an upstream file, or entry 1's or entry 4's shared harness files (`CMakeLists.txt`, `harness_base.sources.cmake`, `stubs/`, `screen_stubs/`, `match.cmake`, `MatchSupport.h`, `HarnessSupport.h`); a suite may add its own doubles and relocate headers at configure time. No `Serial`, no bare `new`.

</frozen-after-approval>

## Code Map

- `src/activities/games/GamesListActivity.{h,cpp}` -- the list under test: `onEnter` (installInbox, loadGames, rebuildRows), `reasonText`, the `Manifest::check` filter in `loadGames`, `activateIndex` (replaces itself with `GameMatchActivity`), `onBackButton` (`goHome()`), `handleCustomInput` (dismisses the note).
- `src/games/GameRegistry.cpp` -- the folder filter (`.pkg`, manifest id equals folder name, dot folders), built in `game_match_src`.
- `src/activities/home/HomeActivity.{h,cpp}`, `src/components/CoverGridHomeUi.{h,cpp}` -- Home under test; `indexToMenuItem` / `menuItemToIndex` (private statics in the header) against `drawTabs`' `ICONS` and OPDS skip.
- `test/game_script/harness/match.cmake` -- `game_match_src` (real Activity, UiListActivity, ButtonNavigator, FreeInkUI, GameRegistry, GameMatchActivity) that both new suites link. `screen_stubs/` (ActivityManager, MappedInputManager, UITheme, UiAppHost, GfxRenderer) are entry 4's doubles.
- New: `games_list.cmake`, `GamesListTest.cpp`, `list_stubs/` (`InstallerScript.h`, `GamePackageInstallerDouble.cpp`); `home.cmake`, `HomeTabsTest.cpp`, `home_stubs/` (header doubles and `HomeStubs.cpp`).

## Tasks & Acceptance

**Execution:**
- [x] `test/game_script/harness/list_stubs/` -- scripted `GamePackageInstaller` (real header; `hasInbox`, `installAll`) -- the list needs the Report, not a package.
- [x] `test/game_script/harness/games_list.cmake`, `GamesListTest.cpp` -- 17 tests: install first and once, the popup, header and hints, empty list, ordering, the folder filter, the `Manifest::check` filter, tap and Confirm open the match, Back goes Home, the note (dismiss by Back, Confirm, or tap), and every `Error` to its own text.
- [x] `test/game_script/harness/home.cmake`, `home_stubs/`, `HomeTabsTest.cpp` -- 8 tests: tab order on touch and button boards, with and without an OPDS server and recent books, tapping each tab, the side-button walk (with and without books), Home opened on an item, the two mappings as inverses, and the list-mode rows.
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- five items marked resolved; three new items under `## 4.5`.

**Acceptance Criteria:**
- Given the fake card holds games, when the list opens and a row is tapped, then the manager is asked to replace the list with that game's match and Back asks for Home.
- Given `HomeActivity.cpp` and `CoverGridHomeUi.cpp` as they are in `src/`, when the tabs are drawn and tapped, then each tab's icon opens the screen it stands for.
- Given a mutation of a pinned line, when the suite runs, then a named test fails (Verification).

## Implementation Notes

Implemented directly by the build agent in one pass rather than through an implementation subagent: the investigation and the code were one loop (each double was written against a compile error), and the plan was written after it. The plan's Code Map and Design Notes are the record of that investigation.

## Plan Change Log

## Review Triage Log

Pass 1. The four lenses (Blind Hunter, Edge Case Hunter, Verification Gap, Intent Alignment) ran as context-free subagents over the staged diff (the plan excluded from it, given to the edge-case lens as its claims file) and all returned. Each row carries its verdict (grouped findings are one row); none was high, maybe-false, intent_gap, or bad_plan.

| Lens | Finding | Verdict, route | Evidence and action |
|---|---|---|---|
| blind | Suites not wired into the build | false | `harness/CMakeLists.txt` globs `*.cmake`; both binaries built and ran (1080 tests). The plan was left out of the staged diff on purpose. |
| blind | Resolved entries keep `evidence:` lines that read the other way | low, rejected | The file's own convention is "Resolved by ...  It read: ..." with the old evidence kept (see the entries above `## 3.1`); a fix is an edit to every entry. |
| blind, edge, gap | The `.dotted` case does not test the dot rule (its id differs from its folder) | medium, patch | Case and its comment removed; the test's header says the clause is unobservable. |
| blind, edge | `no-manifest` and `broken` folders never asserted absent | medium, patch | The test asserts the registry's own "Skipping ..." log for each folder and "Found 1 games"; added a `.pkg` with a bad body (`bad-pkg`) as well. |
| blind, edge | `static_assert(UnknownIcon == 18)` misses an enumerator appended after it | medium, patch | Replaced by `listed()`, a switch over every `Error` with no default, and `-Werror=switch` on the target: any new value fails to compile until it is added. |
| blind | Report-count paths (several failures, long file name, no-failure draws exactly one line) | low, rejected | `Report` keeps only the first failure by design (`## 4.3`); the file name is bounded by the installer to 63 bytes and `note` truncates safely; the double's fixed 10 px glyph makes a wrapped-length check meaningless (`## 4.4`, layout item). |
| blind, edge | `touchPanel()` never turned off; walk only tested without books or OPDS | low, patch | The tab-order test runs on touch and button boards; the walk has a test with books and an OPDS server; books-without-OPDS and Back-opens-the-latest-book added to the recent-books test. |
| blind | `press` versus `click` unexplained; redundant `activityManager.reset()` | low, patch (reset) / false (press) | `press` sets the pressed edge `ButtonNavigator` reads; Confirm reads the release. Redundant resets removed. |
| blind | Tests depend on one renderer size and a left-to-right order; orphan comment | low, patch (comment) / rejected | The harness models one panel by design (`## 4.4`); the comment is reworded. |
| blind | The GfxRenderer copy is a second definition of one class (ODR); the include rewrite is unguarded; `file(GENERATE)` evaluates `$<` | medium (ODR: real, reported, recommendation in `## 4.5`) / low (guards): patch | Both guards added to `home.cmake` (FATAL_ERROR with the reason). The ODR itself stays: it is the workaround the brief's rule leaves, and the report asks the orchestrator to approve the shared-double edit. |
| blind | `FREEINK_CAP_GAMES` never defined for the new targets | false | `game_match_src` sets it PUBLIC, which `game_list_src` and `game_home_src` inherit; the tests run and would not link otherwise. |
| blind | Missing `<cstdio>`, `<cstdlib>`, `<cstddef>`, `<cstdint>`; `namespace library` opened twice | low, patch | Fixed. |
| blind | Home doubles have no drift guard | low, deferred already | `## 4.5`, third item. |
| blind | `reach::Open` is brittle to upstream signature changes; magic `opds ? 6 : 5`; OPDS fallback unasserted | low, patch (magic number and fallback) | The row count is derived from `menuItemToIndex(SETTINGS_MENU)`; the fallback to 0 is asserted; a changed upstream signature is a loud compile error, which is the intended failure. |
| edge | `.pkg` with a bad body untested | low, patch | `bad-pkg` case added. |
| edge | Tap at (240, 400) while the note is up may not be on the row | low, patch | The test taps the drawn "Alpha" row. |
| edge | Tap on an empty list cannot fail | low, rejected | It exercises the routing path; Confirm is the half that could. |
| edge | Test name says any key dismisses the note | low, patch | Renamed `...AndBackDismissesIt`. |
| edge | `Error::None` and a 63-character name with the longest reason | low, rejected | `None` is never reported with `failed > 0`; the wrap check needs a real font (`## 4.4`, layout item). |
| edge | OPDS request without a server and `NONE` untested | low, patch | Cases added to `HomeOpenedOnAnItemSelectsThatItemsTab`. |
| edge | The stubbed `hasEpubExtension` is case-sensitive | low, patch | Made case-insensitive. |
| edge | `dropMatch` exits every replacement when one was entered | low, patch | Only the activity `enterReplacement()` started is exited. |
| edge | Claims: books-without-OPDS untested; two mutants killed by a crash | low, patch (test) / recorded | Test added; the crashes are noted in the table as "no named test". |
| gap | No gaps; the `.dotted` case and the two surviving list terms | see above | Survivors are in `## 4.5`'s first list item and the Verification table. |
| intent | Readings A (source under doubles), B (as-shipped surface), C (guarded lines); the diff takes A with C | recorded | Divergences at the as-shipped surface are all in `## 4.5`: the `goHome` name mapping, the tab bar drawn by the test's `renderUi()`, the scripted installer, and the doubles' drift. The mutation record is this plan (the lens was not shown it). |

## Design Notes

**Home builds unchanged; the answer to the ticket's unknown.** `HomeActivity.cpp` and `CoverGridHomeUi.cpp` compile and run on the host without an upstream edit. What it takes: copies of `HomeActivity.{h,cpp}`, `CoverGridHomeUi.{h,cpp}` and six icon headers, made at configure time into `build/test/.../real_home/` (a change to the real files is what the suite builds); a folder `home_stubs/` with a double for each header the host lacks (`BoardConfig.h`, `FsHelpers.h`, `RecentBooksStore.h`, `Epub.h`, `Xtc.h`, `CrossPointSettings.h`, `CrossPointState.h`, `OpdsServerStore.h`, `Bitmap.h`, `HomeCoverCache.h`, `FileBrowserActivity.h`) and two forwarders for the sibling includes `UiAppHost.h` and `UITheme.h`; `HomeStubs.cpp` for the few functions behind them; and the real `LibraryIndexFile.h` and `LibraryBuilder.h` headers with their called members defined there (the real `LibraryIndexFile.cpp` uses `HalFile::fileSize64`, which the shared fake card lacks). That is more than a handful of headers, but every one is a few lines and the chain ended: no further header appeared after them.

**The GfxRenderer copy.** `screen_stubs/GfxRenderer.h` has no `drawIcon`, `getRegionByteSize`, `copyRegionToBuffer`, or `copyBufferToRegion`; Home calls all four, the tabs being icons. The brief says to stop and report when a shared double lacks something; the four members are also small enough that the orchestrator may prefer to add them. To keep both open, `home.cmake` generates a copy of the double with the four added (`file(GENERATE)`, inserted before a named line; the configure stops with a message if that line moves), placed first on the include path. The additions are member functions only and the icons drawn are kept in a function-local static, so the class is laid out as the original is and the objects of `game_match_src` (built against the original) agree with it. It is still two definitions of one class in one executable, which the standard calls ill-formed with no diagnostic; the recommendation is to add the members to the shared double and delete the copy (`## 4.5`). The patch for that edit is in the scratchpad (`gfxrenderer-icons.patch`).

**Private members.** `HomeActivity`'s `coverGridUi`, `selectorIndex`, `indexToMenuItem`, and `menuItemToIndex` are private; the test reaches them by the explicit-instantiation idiom (`reach::Open`), which the standard permits and which needs no `#define private public`. The theme double records `drawCoverGridHome` without rendering (the real `UITheme::drawCoverGridHome` calls `renderUi()`), so the test calls `coverGridUi->renderUi()` after `HomeActivity::render`.

**What a tab test can see.** A tab is an icon: the renderer double records each `drawIcon` bitmap and position; the test matches the bytes against the real icon arrays (`FolderIcon` and the rest, `GameIcons::GAME_CONTROLLER_32`) and taps the middle of the icon. The screen the manager is asked for is the other file's business (`indexToMenuItem`), so a reorder on either side fails.

**No function was moved or rewritten** (`git log -L` is not needed): the suites only read `src/`.

**Equivalent mutants recorded, not chased** (Verification): `if (i == 2 && !hasOpds) continue;` in `drawTabs` is a counting device (any one skipped slot gives the same count; the painter re-derives each icon from its index), so only `!hasOpds` inverted fails; a dot folder can never pass the registry's id check whichever line rejects it; and with pass and nearby off, the list's `!check.ok()` and solo-mode terms each give the same list alone.

## Verification

**Commands:**
- `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass. Result: 1081 of 1081 (1056 before, plus 17 `GamesListHarnessTest` and 8 `HomeHarnessTest`), run after the review fixes and after the mutation runs restored every file (`git status` shows no change under `src/` or `lib/`).
- `pio run -e x4pro` and `pio run -e default` -- expected: build. Result: `x4pro` SUCCESS in 4:25 and `default` (ESP32-C3) SUCCESS in 3:28, each `pio run` under the shared lock at the baseline tree (no `src/` or `lib/` change is in the commit; both ran before the mutation runs, which restore the files).
- `./bin/clang-format-fix` twice, then `git status` -- expected: nothing new after the second. Result: see below.

**Mutations** (each applied to the line named, built, the suite run, the file restored; script `scratchpad/4.5/mutate.py`; run twice, before and after the review fixes, with the same result for every row):

| Pinned item | Mutation | Fails |
|---|---|---|
| `## 4.6` reasonText | `GamesListActivity.cpp`: `BadSize` and `BadCrc` return each other's text | `EveryInstallErrorMapsToItsOwnReason`, `ConfirmAndATapAlsoDismissTheNoteWithoutOpeningARow` |
| host-caps filter | `loadGames`: `if (false)` for the whole filter | `AGameThisHostCannotStartSoloIsNotListed` |
| host-caps filter, one term | drop `!game.check.ok()` only, or the solo term only | none (equivalent while pass and nearby are off; `## 4.5`) |
| install before list | `onEnter`: `loadGames()` before `installInbox()` | `AGameTheInstallerJustInstalledIsInTheSameVisitsList` |
| Installing popup | `installInbox`: no `drawPopup` | `TheInstallingPopupIsShownBeforeTheInstallOnlyWhenTheInboxHoldsAFile` |
| Back goes Home | `onBackButton`: `finish()` for `goHome()` | `BackGoesStraightHomeAndDoesNotPopTheStack`, `AFailedFileIsExplainedOnceInAPopupOverTheListAndBackDismissesIt` |
| note dismissal | `handleCustomInput`: Confirm no longer dismisses | `ConfirmAndATapAlsoDismissTheNoteWithoutOpeningARow` |
| a row opens its own game | `activateIndex`: row 0 whatever the row | `ATapOnARowReplacesTheListWithThatGamesMatch`, `ConfirmOpensTheSelectedRow...` |
| `## 4.4` tracer, id filter | `GameRegistry.cpp`: `std::strcmp(id, dirName) != 0` becomes `false` | `OnlyAFolderWithAValidPkgAndAManifestOfItsOwnIdIsListed` |
| `.pkg` filter | `GameRegistry.cpp`: no `.pkg` needed (`if (false)` for the `readPackageHash` test) | same |
| dot-folder filter | `GameRegistry.cpp`: drop `dirName[0] != '.'` | none (equivalent: no manifest id begins with a dot; the test has no dot case) |
| `## 3.6` tab order | `CoverGridHomeUi.cpp`: `GAME_CONTROLLER_32` and `Settings2Icon` swapped | `TheTabsAreInHomesOrder...`, `WithAnOpdsServer...`, `RecentBooksCome...`, `TheSideButtonsWalk...` |
| OPDS skip | `drawTabs`: skip when `hasOpds` instead of `!hasOpds` | no named test: `HomeHarnessTest` crashes in the mutated icon lookup (exit 139) |
| OPDS skip slot | `drawTabs`: `i == 3` instead of `i == 2` | none (equivalent, above) |
| icon shift | `iconPainter`: `index >= 3` instead of `index >= 2` | `TheTabsAreInHomesOrder...`, `TheSideButtonsWalk...` |
| index mapping | `HomeActivity.h` `indexToMenuItem`: Transfer and Games swapped | seven tests, among them `TheTabsAreInHomesOrder...` |
| index mapping | `menuItemToIndex`: Games one row late | `HomeOpenedOnAnItemSelectsThatItemsTab`, `TheTwoIndexMappingsAreInverses...` |
| index mapping | `indexToMenuItem`: OPDS taken without a server | `HomeOpenedOnAnItem...`, `TheTabsAreInHomesOrder...`, `TheSideButtonsWalk...`, `TheTwoIndexMappings...` |
| menu count | `getMenuItemCount`: Games not counted | five tests, among them `TheTabsAreInHomesOrder...` |
| tab value | `drawTabs`: `tab.value = count` (books ignored) | no named test: crash in the mutated icon lookup (exit 139) |
| list-mode row | `HomeActivity.cpp`: Games row after Settings | `TheListHomeShowsGamesJustAboveSettings...` |

**Manual checks:** none; no simulator screenshot is named by the verify.

---
title: 'The list and Home on the host'
type: 'chore'
ticket: '5'
created: '2026-09-29'
status: done
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
- New: `games_list.cmake`, `GamesListTest.cpp`, `list_stubs/` (`InstallerScript.h`, `GamePackageInstallerDouble.cpp`, `HostCapsScript.h`, `GameHostCapsDouble.cpp`); `home.cmake`, `HomeTabsTest.cpp`, `home_stubs/` (header doubles and `HomeStubs.cpp`).

## Tasks & Acceptance

**Execution:**
- [x] `test/game_script/harness/list_stubs/` -- scripted `GamePackageInstaller` (real header; `hasInbox`, `installAll`) -- the list needs the Report, not a package.
- [x] `test/game_script/harness/games_list.cmake`, `GamesListTest.cpp` -- 18 tests: install first and once, the popup, header and hints, empty list, ordering, the folder filter, the `Manifest::check` filter, tap and Confirm open the match, Back goes Home, the note (dismiss by Back, Confirm, or tap), and every `Error` to its own text.
- [x] `test/game_script/harness/home.cmake`, `home_stubs/`, `HomeTabsTest.cpp` -- 8 tests, run on the games build and again, without `FREEINK_CAP_GAMES`, as the boards without games ship it (`HomeGamesOffHarnessTest`): tab order on touch and button boards, with and without an OPDS server and recent books, tapping each tab, the side-button walk (with and without books), Home opened on an item, the two mappings as inverses, and the list-mode rows.
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- five items marked resolved; three new items under `## 4.5`.

**Acceptance Criteria:**
- Given the fake card holds games, when the list opens and a row is tapped, then the manager is asked to replace the list with that game's match and Back asks for Home.
- Given `HomeActivity.cpp` and `CoverGridHomeUi.cpp` as they are in `src/`, when the tabs are drawn and tapped, then each tab's icon opens the screen it stands for.
- Given a mutation of a pinned line, when the suite runs, then a named test fails (Verification).

## Implementation Notes

Implemented directly by the build agent in one pass rather than through an implementation subagent: the investigation and the code were one loop (each double was written against a compile error), and the plan was written after it. The plan's Code Map and Design Notes are the record of that investigation.

Follow-up (orchestrator's independent review): the orchestrator approved one change outside `touches`, to entry 4's `test/game_script/harness/screen_stubs/GfxRenderer.h`: `drawIcon`, `getRegionByteSize`, `copyRegionToBuffer`, and `copyBufferToRegion` added to the shared double (17 lines, the icons drawn in a member `icons`), in place of the generated copy `home.cmake` had. The note is repeated in entry 4's plan (Verification).

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
| blind | (Superseded in pass 2: the copy is gone.) The GfxRenderer copy is a second definition of one class (ODR); the include rewrite is unguarded; `file(GENERATE)` evaluates `$<` | medium (ODR: real, reported, recommendation in `## 4.5`) / low (guards): patch | Both guards added to `home.cmake` (FATAL_ERROR with the reason). The ODR itself stays: it is the workaround the brief's rule leaves, and the report asks the orchestrator to approve the shared-double edit. |
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

Pass 2 (orchestrator's independent review, three lenses run by one context-free reviewer; confirmed the `## 3.6` pin by mutation, the configure-time copies, no flakes over 20 shuffled repeats, no stub leakage, and that the test copies easily after entry 8's rename).

| Lens | Finding | Verdict, route | Evidence and action |
|---|---|---|---|
| orchestrator: n/a (my question 0) | Approved: add the four members to entry 4's `GfxRenderer.h`, drop the generated copy | approved, done | The four members are in `screen_stubs/GfxRenderer.h`, the copy and its `file(GENERATE)` are out of `home.cmake`, the test reads `renderer->icons`, the `## 4.5` item is gone, and entry 4's plan carries a line. |
| verification-gap | `-Werror=switch` forces a case in `listed()` only; a new `Error` with a case in the test but none in `reasonText` still passed | medium, patch | `expectedText()` is now the table (a switch, no default, `-Werror=switch` kept and noted in `games_list.cmake` for copies); the test runs every value 0 to 255 that has a text and asserts the screen shows that text and no other. Mutation L8 (delete a `reasonText` case) fails it. |
| verification-gap, edge | The solo-mode term of the filter is unpinned | medium, patch | `list_stubs/GameHostCapsDouble.cpp` (linked into the test executable, winning over the archive member) turns `pass` on; `AGameThatCannotStartSoloIsNotListedEvenWhenAnotherModeCanStart` fails under mutation L2c. |
| edge (claims) | `deferred-work.md` says either filter term alone survives; `verdict()` makes `!check.ok()` equivalent for good; the reasonText item is now falsified | low, patch | Three entries reworded (`Manifest.cpp:220`). |
| verification-gap | The Home suite builds only with `FREEINK_CAP_GAMES=1`; the games-off mapping that `default`, `x4c`, `papermono` ship never runs | low, patch (cheap) | `game_home_off_src` and `HomeGamesOffHarnessTest` build the same copies and the same test file with `-UFREEINK_CAP_GAMES`; mutation G1 (its icons) and G2 fail it. |

## Design Notes

**Home builds unchanged; the answer to the ticket's unknown.** `HomeActivity.cpp` and `CoverGridHomeUi.cpp` compile and run on the host without an upstream edit. What it takes: copies of `HomeActivity.{h,cpp}`, `CoverGridHomeUi.{h,cpp}` and six icon headers, made at configure time into `build/test/.../real_home/` (a change to the real files is what the suite builds); a folder `home_stubs/` with a double for each header the host lacks (`BoardConfig.h`, `FsHelpers.h`, `RecentBooksStore.h`, `Epub.h`, `Xtc.h`, `CrossPointSettings.h`, `CrossPointState.h`, `OpdsServerStore.h`, `Bitmap.h`, `HomeCoverCache.h`, `FileBrowserActivity.h`) and two forwarders for the sibling includes `UiAppHost.h` and `UITheme.h`; `HomeStubs.cpp` for the few functions behind them; and the real `LibraryIndexFile.h` and `LibraryBuilder.h` headers with their called members defined there (the real `LibraryIndexFile.cpp` uses `HalFile::fileSize64`, which the shared fake card lacks). That is more than a handful of headers, but every one is a few lines and the chain ended: no further header appeared after them.

**The GfxRenderer members.** `screen_stubs/GfxRenderer.h` had no `drawIcon` (the cover grid's tabs are icons), `getRegionByteSize`, `copyRegionToBuffer`, or `copyBufferToRegion` (HomeActivity's cover snapshot). The first pass kept the file untouched and generated a patched copy in `home.cmake`; the orchestrator's review approved editing the shared double instead, and the copy is gone. `drawIcon` records the bitmap's bytes, position, and size in `renderer->icons`, which `forgetAll()` clears.

**The games-off build.** The boards `default`, `x4c`, and `papermono` ship Home without `FREEINK_CAP_GAMES`: five icons, no Games row. `home.cmake` builds `game_home_off_src` from the same copied files with `-UFREEINK_CAP_GAMES` (a compile option follows the definitions `game_match_src` passes on), and `HomeTabsTest.cpp` again as `HomeGamesOffHarnessTest` (ctest prefix `GamesOff.`), its expected tabs from `tabNames()` and its Games cases under `#if FREEINK_CAP_GAMES`.

**Scripted host caps.** `list_stubs/GameHostCapsDouble.cpp` is linked into the list test executable, where its `gameHostCaps()` wins over the archive member in `game_match_src`; it returns the real host's values (two seats, no radio) with `pass` scriptable, which the solo-mode test needs.

**Private members.** `HomeActivity`'s `coverGridUi`, `selectorIndex`, `indexToMenuItem`, and `menuItemToIndex` are private; the test reaches them by the explicit-instantiation idiom (`reach::Open`), which the standard permits and which needs no `#define private public`. The theme double records `drawCoverGridHome` without rendering (the real `UITheme::drawCoverGridHome` calls `renderUi()`), so the test calls `coverGridUi->renderUi()` after `HomeActivity::render`.

**What a tab test can see.** A tab is an icon: the renderer double records each `drawIcon` bitmap and position; the test matches the bytes against the real icon arrays (`FolderIcon` and the rest, `GameIcons::GAME_CONTROLLER_32`) and taps the middle of the icon. The screen the manager is asked for is the other file's business (`indexToMenuItem`), so a reorder on either side fails.

**No function was moved or rewritten** (`git log -L` is not needed): the suites only read `src/`.

**Equivalent mutants recorded, not chased** (Verification): `if (i == 2 && !hasOpds) continue;` in `drawTabs` is a counting device (any one skipped slot gives the same count; the painter re-derives each icon from its index), so only `!hasOpds` inverted fails; a dot folder can never pass the registry's id check whichever line rejects it; and with pass and nearby off, the list's `!check.ok()` and solo-mode terms each give the same list alone.

## Verification

**Commands:**
- `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass. Result (follow-up commit, after the orchestrator's review): 1090 of 1090 (1056 before, plus 18 `GamesListHarnessTest`, 8 `HomeHarnessTest`, 8 `HomeGamesOffHarnessTest`); `GamesListHarnessTest`, `HomeHarnessTest`, `HomeGamesOffHarnessTest`, and entry 4's `GameMatchHarnessTest` each pass `--gtest_repeat=20 --gtest_shuffle` (20 of 20). First commit: 1081 of 1081 (1056 before, plus 17 `GamesListHarnessTest` and 8 `HomeHarnessTest`), run after the review fixes and after the mutation runs restored every file (`git status` shows no change under `src/` or `lib/`).
- `pio run -e x4pro` and `pio run -e default` -- expected: build. Result: `x4pro` SUCCESS in 4:25 and `default` (ESP32-C3) SUCCESS in 3:28, each `pio run` under the shared lock at the baseline tree (no `src/` or `lib/` change is in the commit; both ran before the mutation runs, which restore the files).
- `./bin/clang-format-fix` twice, then `git status` -- expected: nothing new after the second. Result: see below.

**Mutations** (each applied to the line named, built, the suite run, the file restored; script `scratchpad/4.5/mutate.py`; run three times, before the review, after its fixes, and after the orchestrator's; the same result for every row each time, and the rows added last are the ones marked here):

| Pinned item | Mutation | Fails |
|---|---|---|
| `## 4.6` reasonText | `GamesListActivity.cpp`: `BadSize` and `BadCrc` return each other's text | `EveryInstallErrorMapsToItsOwnReason`, `ConfirmAndATapAlsoDismissTheNoteWithoutOpeningARow` |
| host-caps filter | `loadGames`: `if (false)` for the whole filter | `AGameThisHostCannotStartSoloIsNotListed` |
| host-caps filter, solo term | `loadGames`: drop the solo-mode term only | `AGameThatCannotStartSoloIsNotListedEvenWhenAnotherModeCanStart` (the `gameHostCaps()` double turns `pass` on) |
| host-caps filter, `!check.ok()` | drop `!game.check.ok()` only | none, permanently: `verdict()` returns `modes = 0` for every non-Ok result (`Manifest.cpp:220`), so the solo term already leaves those out |
| reasonText, a value with no case | `GamesListActivity.cpp`: the `UnknownIcon` case deleted | `EveryInstallErrorMapsToItsOwnReason` (the compiler only warns there) |
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
| index mapping | `HomeActivity.h` `indexToMenuItem`: Transfer and Games swapped | seven tests, among them `TheTabsAreInHomesOrder...` (`HomeHarnessTest`; the mutated text does not compile without games, which is a build error there and not a survivor) |
| index mapping, games off | `indexToMenuItem`: Settings one row late (both builds) | `HomeOpenedOnAnItemSelectsThatItemsTab`, `TheListHomeRowsAre...`, others, in `HomeHarnessTest` and `HomeGamesOffHarnessTest` |
| games-off icons | `CoverGridHomeUi.cpp`'s `#else` `ICONS`: Transfer and Settings swapped | `HomeGamesOffHarnessTest`: `TheTabsAreInHomesOrder...`, `TheSideButtonsWalk...`, `RecentBooksCome...` |
| index mapping | `menuItemToIndex`: Games one row late | `HomeOpenedOnAnItemSelectsThatItemsTab`, `TheTwoIndexMappingsAreInverses...` |
| index mapping | `indexToMenuItem`: OPDS taken without a server | `HomeOpenedOnAnItem...`, `TheTabsAreInHomesOrder...`, `TheSideButtonsWalk...`, `TheTwoIndexMappings...` |
| menu count | `getMenuItemCount`: Games not counted | five tests, among them `TheTabsAreInHomesOrder...` |
| tab value | `drawTabs`: `tab.value = count` (books ignored) | no named test: crash in the mutated icon lookup (exit 139) |
| list-mode row | `HomeActivity.cpp`: Games row after Settings | `TheListHomeShowsGamesJustAboveSettings...` |

**Manual checks:** none; no simulator screenshot is named by the verify.

---
title: 'Remove a game from the launcher (with whole-page paging and the return to the game''s page)'
type: 'feature'
ticket: '10'
created: '2026-09-29'
status: 'built'
baseline_revision: '764e49088660d9a946f5e37beba5480c63b01d25'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/docs/contributing/touch-and-ui.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A person cannot remove an installed game (R5, R7). The launcher's last page repeats rows of the page before it (it aligns to the end of the list), and leaving a game drops the launcher back on page 1.

**Approach:** (1) A long-press on a launcher row (or a hold of Confirm) opens a confirmation built into the launcher; confirming calls `GamePackageInstaller::remove(id)`, which deletes `/.games/<id>/` (the `.pkg` first) and never touches `/.games-data/<id>/`; the listing and icon cache are rebuilt, and a failure is reported in the launcher's note popup. (2) The launcher pages by whole pages: the list is padded to a whole number of pages with blank, disabled rows. (3) Opening a game stores a 4-byte fingerprint of its id in a `constinit` static; the next launcher selects that game and shows the whole page holding it, else the top.

## Boundaries & Constraints

**Always:** Change only `src/games/GamePackageInstaller.*`, `src/activities/games/GamesLauncherActivity.*`, `lib/I18n/translations/english.yaml`, and new files under `test/game_script/harness/` (plus `games_launcher.cmake`, `GamesLauncherTest.cpp`, where a launcher change makes them wrong). New strings are `tr(STR_GAMES_*)` in `english.yaml` only. SD access through `Storage`. `remove` deletes `/.games/<id>/.pkg` first and returns `Error::SdCard` if that fails; then `removeDir`; it never touches `/.games-data` or `/.games-tmp`. `ActivityManager.*`, `GameMatchActivity.*`, `GameSaveStore.*`, `UiListActivity.*`, and the SDK stay unchanged. The fingerprint is at most 8 B of static RAM.

**Never:** Move the `freeink-sdk` pointer; edit entry 1's or entry 4's shared harness files; give `goToGames()` a parameter; let a blank padding row be selected, tapped, or long-pressed; delete a game without asking.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Remove | `/.games/g/` (with `.pkg`) and `/.games-data/g/{store.bin,resume.bin}` | `/.games/g` gone; both data files byte-identical; a reinstall of `g` afterwards leaves them byte-identical too | No error |
| Remove, no such folder | id not on the card | `Error::None` (already gone) | none |
| `.pkg` will not delete | `failRemove` on `.pkg` | `Error::SdCard`; folder untouched, game still listed | Launcher shows the note; listing rebuilt |
| Folder delete stops partway | `failRemove` on `manifest.json` | `Error::SdCard`; `.pkg` gone so the registry does not list it | Launcher shows the note; row gone after rebuild |
| Bad id | empty, over 32 B, or not `[a-z0-9-]` | `Error::BadManifest`, no card call | none |
| Long-press, confirm | long-press row; Remove tapped, or Confirm on Remove | confirmation names the game; `remove(id)` called once; list refreshed; fingerprint cleared if it named the game | as above on failure |
| Long-press, cancel | Cancel tapped, Back, or Confirm on Cancel (the default) | nothing removed, list unchanged | none |
| Padding row | 9, 16, 25 games; tap, long-press, or keys on the blank rows | never selected or opened; no dialog | none |
| Paging | 9, 16, 25 games | every page (last included) starts at the row after the previous page's last; no row repeats; swipes and page keys agree | none |
| Return | open game 20 of 25 (straight or through the picker), leave | new launcher shows the page holding 20, selected; removed or unknown game: top; empty fingerprint: top | none |

</frozen-after-approval>

## Code Map

- `src/games/GamePackageInstaller.h/.cpp` -- add `Error remove(const char* id)`; reuse `GamePaths::GAMES_DIR`, `PKG_NAME`, and `commit()`'s marker-first order (`Storage.remove(.pkg)` then `removeDir`). `GamePaths.h` unchanged.
- `src/activities/games/GamesLauncherActivity.h/.cpp` -- the launcher (a `UiListActivity`): `listCount()`, `buildScreen`, `onEnter`, `loadGames`, `loadIcons`, `activateIndex`, `handleCustomInput` (note popup), `handleButtons`, `navigateButtons`. Reference: `WifiSelectionActivity::buildPromptDialog` (`fui::optionDialog` in the screen, `routeTouch`, Up/Down move focus), `LibraryListActivity::onRowLongPress` and its `wasLongPressed(Confirm, LONG_PRESS_MS)` hold.
- `freeink-sdk/.../lists/list.h` -- read only: `ListNav::scrollBy` clamps `top` to `count - pageSize`, which is the repeat; `ListItem::enabled=false` registers no hit; a nav-managed list draws `props.count` rows.
- `lib/I18n/translations/english.yaml` -- new keys after `STR_GAMES_MODE_NEARBY_DESC`.
- `test/game_script/harness/games_launcher.cmake` -- add the remove double to `game_launcher_src` (the launcher now references `remove`); `list_stubs/GamePackageInstallerDouble.cpp` is not edited.
- `test/game_script/harness/{installer.cmake,GamePackageInstallerTest.cpp,InstallerSupport.h}` -- patterns for the installer suite; `GamesLauncherTest.cpp` -- the launcher fixture to copy (`ListTest`, `addGame`, `rows`, `tapRow`, `open`, `reopen`).

## Tasks & Acceptance

**Execution:**
- [ ] `src/games/GamePackageInstaller.h/.cpp` -- add `remove(id)` (doc comment: order, what it keeps, why a listed game cannot be cross-linked) -- R5
- [ ] `lib/I18n/translations/english.yaml` -- `STR_GAMES_REMOVE_TITLE` "Remove this game?", `STR_GAMES_REMOVE_KEPT` "Its saved data is kept.", `STR_GAMES_REMOVE` "Remove", `STR_GAMES_REMOVING` "Removing...", `STR_GAMES_REMOVE_FAILED` "Could not remove it. Check the SD card."
- [ ] `src/activities/games/GamesLauncherActivity.h/.cpp` -- (a) pass `wantsTouchLongPress=true` and `InputTouch | InputLongPress`; `onRowLongPress` and a Confirm hold open the confirmation (`removeIndex`, `choice` focus 0 = Cancel); `buildScreen` draws only the dialog while it is open; `handleCustomInput` owns the dialog (Back cancels, Up/Down/NavPrevious/NavNext move focus, Confirm applies, touch through `UiAppHost::routeTouch` and an `ACTION_USER` handler registered in `onEnter`); confirm takes `RenderLock`, draws "Removing...", calls `remove`, rebuilds listing and icons, keeps the selection near the old row, and on failure sets `note`. (b) whole pages: `pageRows` (atomic, from `nav.visibleRows` after `syncListViewport`); `listCount()` returns the listing count rounded up to a multiple of `pageRows` when it exceeds one page; `provideRow` gives an index past the listing a blank `""` label, `enabled=false`; `buildScreen` sets `props.count` to the padded count after the sync and, when `followPending`, snaps `top` to `selected / pageRows * pageRows`; `navigateButtons` is overridden to walk the real count; `activateIndex` and `onRowLongPress` ignore an index past the listing. (c) return: `constinit` `uint32_t` FNV-1a of the id (0 = none, never produced) set in `activateIndex` before the picker push or the match replace, cleared by a successful remove of that game; `loadGames` looks it up and `requestSelection`s the game and marks the first build to align its page.
- [ ] `test/game_script/harness/` -- new `remove_game.cmake` with two executables: `GameRemoveTest.cpp` (real installer over the fake card: remove and reinstall each keep every byte of `store.bin` and `resume.bin`, and any other file in the data folder; missing folder; `.pkg` refuses; partial delete leaves the game unlisted and a retry or reinstall finishes; bad ids; `/.games-tmp` untouched) and `GameRemoveLauncherTest.cpp` (launcher over a scripted `remove`, in new `list_stubs/RemoveScript.h` and `list_stubs/GamePackageInstallerRemoveDouble.cpp`; long-press, confirm, cancel, Back, focus keys, Confirm hold, failure note and refreshed list, unavailable game removable, remove during a tap flash; paging with 9, 16, 25 games; padding rows inert; swipe and page keys agree; return to page for a straight start and through the picker, removed game, empty fingerprint). Add the double to `games_launcher.cmake`. Fix `GamesLauncherTest.cpp` only where it turns wrong.

**Acceptance Criteria:**
- Given the verify line of ticket 10, when the host suites run under the build lock, then all pass and `pio run -e x4pro` and `-e default` build.
- Given a simulator run, when a game is long-pressed, then a screenshot shows the confirmation; after Remove, the refreshed list; a 25-game list's last page shows no repeated row; after leaving a game, the launcher is on its page.
- Given the four `check_flash_budget.py` steps at the final commit, then static RAM is at most +808 B and flash at most +240,496 B (games on minus off), each figure with its method.

## Implementation Notes

The implementation subagent ended without changing any file (its session was cut off), so the plan's author implemented it directly, from the plan.

Working rules for the implementer: work only in the worktree `/tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/wt/launcher` (its submodules are initialised). Every host-test CMake configure or build runs as `flock /tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/build.lock sh -c '<commands>'` in one call; run no `pio` or simulator build (the orchestrator of this plan does). Do not commit, push, or run `./bin/clang-format-fix` (it runs at the end). Do not use `git clean`. Follow AGENTS.md: `makeUniqueNoThrow` for fallible allocations, locals under 256 B, `LOG_*` only, no model names. Keep every guard listed in Design Notes. Scratch goes under `/tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/4.10/`.

Added while building: the snap to the selection's page runs only on the build that consumes `followOnBuild` (`followsSelection`), because the render loop's correction pass (`consumeRebuildNeeded`) rebuilds with `followPending` still set and would otherwise undo the top that `onListRendered` advanced. `GamesLauncherActivity::forgetOpenedGame()` is public so a test can start from a fresh boot; nothing in the firmware calls it. The launcher change is `selectRemembered()` (called from `onEnter`) and not in `loadGames`. `GamesLauncherTest.cpp` calls `forgetOpenedGame()` in `SetUp` and before its one `reopen()` that expects the top. The installer's `remove` id check is inline (`validId` is private to `Manifest.cpp`, outside this entry's files).

## Plan Change Log

## Review Triage Log

Pass 1. The four lenses (blind hunter, edge-case hunter, verification gap, intent alignment) ran as context-free subagents, in the foreground, in one message, over `diff.patch` (the diff since the baseline, new files included), and all returned. Verdict counts: high 0, medium 2, low 7, false 2, maybe-false 0 (the intent-alignment report is descriptive and has no findings).

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | blind, edge | `buildRemoveDialog` reads `removeIndex` twice; the input task can set it to -1 between the check and `entries[removeIndex]` | medium | patch | Real: bounds check and use are separate reads across two tasks. Fixed: one local `index` read once. |
| 2 | blind | Non-SD errors (`BadManifest`) show "Check the SD card" | low | reject | The launcher passes only a listed game's id, which `Manifest::check` vetted, so `BadManifest` is unreachable from the UI; a second string is complexity for no reachable case. |
| 3 | blind | A `.pkg` deleted but `removeDir` failing leaves unlisted leftovers no sweep reclaims | medium | defer | Real, and the AD-16 rule the plan accepts (unlisted, never half-listed); a reinstall of that id removes them (`commit()`, tested). A sweep of unlisted `/.games/*` needs its own design. Deferred. |
| 4 | blind | Id grammar duplicated; no `<cstring>` include | low | reject | `validId` is in an anonymous namespace of `lib/GameCore/Manifest.cpp`, outside this entry's touches; the inline check is four lines. `<cstring>` is included by the file already. False for the include. |
| 5 | blind | 32-bit fingerprint can collide | low | reject | The owner's decision fixes a small fingerprint (at most 8 B). A collision selects another game's page, never opens or deletes anything. |
| 6 | blind, edge | Padding rests on last frame's measured page size; a stale size after a layout change | low | reject | `pageRows` is set in the same build, and `props.count` is padded with the fresh value; only the nav's scroll clamp of that one build uses the older size, and only when the size changed (no such change in a session). |
| 7 | blind | `removeIndex` / `removeFocus` are plain fields | low | reject | The same pattern as `noteVisible`; after fix 1 the render task reads `removeIndex` once. |
| 8 | blind | Hold-Confirm and long-press are invisible, magic constant | low | reject | A hold cannot be hinted in the footer without a label the base does not have; the constant is named and commented. |
| 9 | blind | No way to delete saved data | false | reject | R5 says removing keeps `/.games-data/<id>/`; the dialog says so. |
| 10 | blind | Test gaps (exact multiple, one page, shrinking page count, double-open) | medium | patch | Added `AListThatFillsWholePagesHasNoBlankRow...`, `RemovingTheLastGameOfTheLastPageDropsThatPage`, `TheConfirmationOpenedAgainOnAnotherRow...`. |
| 11 | blind | Test fixture copy, `80 + 8` pitch | low | reject | The fixture is the shared harness's pattern; the pitch fallback is used only when the last page holds one game. |
| 12 | edge | Pad and snap use `visibleRows`; a page whose rows grow (a wrapped reason) draws fewer | low | defer | Rows are fixed at `SIDE + 16`; a subtitle stays one line and inside that height at the shipped fonts. If a theme made a row taller, pages would hold fewer rows and the last page could repeat, as before this entry. Deferred with the variable-height note. |
| 13 | edge | A second tap on Remove during the blocking delete lands on the refreshed list | low | defer | Real only if the input manager queues taps across a blocking call, which the harness cannot show; the library's delete has the same shape. Deferred. |
| 14 | edge | `remove` for an id with no `.pkg` sharing clusters with `/.games-tmp/<id>` | low | reject | Only a listed game (with a `.pkg`) reaches `remove`, and a `.pkg` is written after the rename, so a listed folder cannot be cross-linked (header comment, Design Notes). |
| 15 | edge | Stale fingerprint after a failed partial delete of the remembered game | low | reject | The game is unlisted, so the launcher finds nothing and shows the top; a reinstall of that id returning to its old page is harmless. |
| 16 | edge | One `exists()` false negative on the `.pkg` | low | reject | A transient read fault on a present file is not shown to occur; the guard would add a branch the code cannot justify. |
| 17 | edge | Plan wording: `loadGames` does the lookup; the plan omits `followsSelection`; a remove during a tap flash is not tested | low | reject | Findings about the plan text; the code has `selectRemembered()` (called from `onEnter`) and the `followsSelection` guard (Implementation Notes). `clearTapFlash()` is called on every path that leaves the dialog. |
| 18 | gap | The hold callbacks (`onNextContinuous`, `onPreviousContinuous`) in `navigateButtons()` are never run: the screen double's `getHeldTime()` is 0 | medium | defer | Real, and a fix needs a settable held time in the shared `MappedInputManager` double (entry 4's file, not this entry's). The lambdas are the base's, with the real count. Deferred; the orchestrator can approve the small addition. |
| 19 | gap | The Confirm-hold threshold and the release suppression after it are not modelled | low | defer | The double's `wasLongPressed` ignores the threshold and `holdLong` sends no release; the suppression is the manager's. Deferred with 18. |
| 20 | intent | Tests exercise the harness, not the device; the launcher and the real `remove` are tested apart | maybe-false as a defect | reject | Descriptive. The join (launcher, real `remove`, real card) is shown in the simulator run recorded in Verification. |

Pass 2 (independent review by the orchestrator, sent after the first commit; the lenses that produced it were the orchestrator's own reviewers, not the build agent's). Verdict counts: high 0, medium 3, low 7, false 0.

| # | Finding | Verdict | Route | Evidence / action |
|---|---------|---------|-------|-------------------|
| ADV1 | After the confirmation opens, the last render's row hit rects still route a tap to `activateIndex` (a game starts, or the picker is pushed, under the pending dialog) | medium | patch | Reproduced by the new test `ATapOnARowBeforeTheConfirmationIsDrawnOpensNothing` (it failed with the guard off). Fixed: `onRowAction` is overridden and returns while `removeIndex >= 0` or the note is up, so neither `activateIndex` nor `onRowLongPress` runs and the selection does not move. |
| ADV2 | `fui::OptionDialogProps` (about 700 B) is a local in `buildRemoveDialog` | low | patch | Real, against the 256 B rule. Now the member `dialogProps`, filled on each build, as `GameMatchActivity` does. |
| ADV3 | `remove` on a folder without a `.pkg` beside `/.games-tmp/<id>` can free shared clusters | low | patch | Real for a caller with an unlisted id. `remove` now runs the installer's own `foldersShareClusters` when the `.pkg` is absent and the scratch folder exists, and returns `SdCard` (nothing deleted) when the probe shows the chains shared, cannot make its file, or cannot remove it; an independent pair is deleted. A marked folder needs no probe. Tests: three (`AFolderWithoutAMarker...`); the probe removal failed the test when off. Header comment rewritten to the exact results. |
| EDGE4 | Confirm hold checked under the note; row actions under the note move the selection | low | patch | The hold check is now `!noteVisible &&`; row actions under the note are dropped by the `onRowAction` override. Test `UnderTheNoteAHoldOrALongPressOpensNothingAndMovesNothing` (the selection stayed on Game 01). The hold guard alone is not observable in the harness (`openRemoveDialog` also refuses under the note), so its mutant survives; it protects the release on the device. |
| EDGE5 | `remove` answers `None` for a missing folder even when the card cannot be read | low | patch | `remove` opens `/.games` first and answers `SdCard` when it cannot (a card that answers with a `/.games` that lacks `<id>` is still `None`). Tests `ACardThatCannotOpenTheGamesFolderIsNotAGameThatIsGone` and the reworked not-there test; the check failed the test when off. |
| VG1 | `REMOVE_HOLD_MS` is unpinned: 0 survives every test | medium | patch (no shared-double edit) | The harness input double ignores the threshold and entry 4's file stays as it is (deferral stands, 4.13's sweep). Pinned by a `static_assert(REMOVE_HOLD_MS >= 500 && <= 3000)`, no shared constant exists (`LibraryListActivity`'s 1000 ms is private to its file), and a simulator check: `story-remove-screenshots/short-confirm-opens-game.png` shows a short Enter on the selected row opening the game (`Started game-01` in the log) and not the confirmation. |
| VG2 | `RemovingTheRememberedGame...` passes when the fingerprint is never cleared | low | patch | The test re-adds `game-20` before `reopen()` and expects the top; the never-clear mutant now fails it. |
| VG3 | The held page-key lambdas never run on the host | low | record | Equivalent mutant on the host; the deferral in `deferred-work.md` (`## 4.10`) stands. The PR must not claim host coverage of held page keys. |
| VG4 | The promised "remove during a tap flash" test is missing | low | patch, partly | Added `ALongPressThatFollowsATouchDownOpensAndRemovesCleanly` (touch down, then long-press, Remove, refreshed list, selection). The recording target discards paint, so a pressed row cannot be seen from the harness; what is pinned is the sequence and that `clearTapFlash()` is on every path that leaves the confirmation. The pressed look is not shown by any screenshot either. |
| VG5 | The `/.games-data` checks in the launcher suite are vacuous (`remove` is scripted there) | low | record | The launcher suite proves the launcher's reaction; the data guarantee is `GameRemoveTest` (real `remove`, byte-compared) and the simulator run in Verification (`store.bin` and `resume.bin` present after the real remove). |

## Design Notes

- Guards kept, from `git log -L` on the launcher's functions (80b68a92, 60b59a4b, b5566c1c): `loadGames` resets the listing first, so a failed `GameRegistry::load` leaves an empty list, never a stale one; `activateIndex` returns on a bad index, keeps an unavailable game unstarted (logs and repaints because the tap moved the selection), calls `app.clearTapFlash()` before leaving, and repaints after a failed allocation because the flash was cleared; the picker is pushed (Back returns here) while the match is a replace; `buildScreen` shows the empty message for no games and the note popup over the list.
- Why `remove` takes the `.pkg` first: `removeDir` deletes in directory order, so a stop partway would leave a listed game with files missing (the same reason `commit()` does it). A game the registry lists has a `.pkg`, which the installer writes only after the folder rename completes, so a listed folder cannot share clusters with `/.games-tmp/<id>` (that state exists only between the rename and the `.pkg`); `remove` therefore needs no `.xlink` probe and never touches `/.games-tmp`.
- A failed folder delete after the `.pkg` went leaves the game unlisted with leftovers; the note says so and the next install of that id removes them (`commit()`). Recorded as the accepted AD-16 behaviour, not a half-listed game.
- Padding is the launcher's row provider, not an SDK change: the list draws `props.count` rows and `ListNav` clamps to it, so a padded count makes `maxTop` a multiple of the page. `listCount()` is padded because `UiListActivity::syncListViewport` passes it to the nav; the only base paths that would walk onto padding (`navigateButtons`) are overridden, and Confirm or a tap past the listing is ignored. The first build measures the page size, so it lays out unpadded and the snap fixes the top.
- The confirmation is built into the screen (`fui::optionDialog`, as `WifiSelectionActivity` does) and not a pushed `ConfirmationActivity`, so the listing and icon cache stay in place and the harness can drive it. Cancel is the default focus.
- Assumption for entry 14: opening Games from Home also lands on the page of the last game opened, until that game is removed or the device restarts (the fingerprint is not cleared by Back to Home; the owner confirms it on the device). (entry 4.10, 2026-09-29)
- Detail of that assumption: a later visit to Games from Home also lands on the last opened game's page (the decision names only a remove and a boot as clearing it).

## Verification

Run in this worktree at the tree that became the fix commit (no code differs from it), every build under the shared build lock. Figures are from the fix commit; the first commit (`21e3c7dc`) measured +228,544 B flash and +784 B RAM the same way.

**Commands and results:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- 1,247 of 1,247 pass (the epic branch had 1,204; +13 in `GameRemoveTest`, +30 in `GameRemoveLauncherTest`).
- Tests bite (each change failed the named tests and was reverted): padding off (four `PagingTest`s); the page snap off (three); `navigateButtons` walking `listCount()` (one); padding rows enabled (one); no fingerprint set (six `ReturnTest`s); `onRowAction` guard off (two); fingerprint never cleared (one); the cluster probe off (one); the `/.games` open check off (one). The `!noteVisible &&` on the Confirm hold survives (see Review Triage Log, EDGE4).
- `pio run -e x4pro` (inside `check_flash_budget.py build on`) and `pio run -e default` -- SUCCESS both.
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-bytes 240496 --ram-limit-bytes 808`, `objects`: flash on 5,907,552 B, off 5,678,512 B, **+229,040 B** (limit 240,496; 11,456 B to spare); static internal RAM (`size -A`: .dram0 data and bss, .noinit, every .iram0.* section) 187,848 B on, 187,064 B off, **+784 B** (limit 808; 24 B to spare); `objects`: 43 game objects, largest mutable static 4 B (`lastOpened`), no static initializer. The base measured +228,496 B and +776 B at `962ae61`; that is two commits, not one method at one commit.
- `python3 scripts/check_upstream_touches.py` -- PASS (`english.yaml` is ledger row 2; no other upstream file changed). `./bin/clang-format-fix` twice; the second run changed nothing.

**What the launcher suite does not prove:** `remove` is scripted there, so its `/.games-data` checks only show the launcher never touches the card itself. The data guarantee is `GameRemoveTest` (the real `remove`, byte-compared, and no card call to `/.games-data`) and the simulator run below. The held page keys (`onNextContinuous`) and the Confirm hold's threshold never run on the host (the input double has no held time); see `deferred-work.md`, `## 4.10`.

**Simulator** (`sim.sh build x4pro`, `start x4pro`; 25 games in `fs_/.games`, game-20 running the counter fixture; the real installer `remove` on the real simulated card): after Remove on Game 02, `fs_/.games` held 24 folders and no `game-02`; after opening Game 20 from its page and choosing Leave, the launcher came back on Game 18 to 25 with Game 20 selected; after Remove on Game 20, `fs_/.games/game-20` was gone and `fs_/.games-data/game-20/store.bin` and `resume.bin` were still there. After the fix, a short Enter on a selected row opened the game (log: `Started game-01`), not the confirmation. The page holds 8 rows.
- `story-remove-screenshots/page1.png` -- the first page, 25 games (Game 01 to 08).
- `story-remove-screenshots/page2-starts-at-game-09.png` -- page 2 starts at the row after page 1's last.
- `story-remove-screenshots/last-page-no-repeats.png` -- the last page shows only Game 25, with blank padding and no repeated rows.
- `story-remove-screenshots/confirmation.png` -- the confirmation after a long-press on Game 02 (Cancel focused).
- `story-remove-screenshots/refreshed-list.png` -- the list after Remove: Game 02 gone, Game 03 selected.
- `story-remove-screenshots/back-on-game-20-page.png` -- the launcher after leaving Game 20: its whole page, Game 20 selected.
- `story-remove-screenshots/short-confirm-opens-game.png` -- a short Confirm on the selected row starts the game (Counter), not the remove confirmation.

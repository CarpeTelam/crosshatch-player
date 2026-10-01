---
title: 'One launcher row per game'
type: 'feature'
ticket: '8'
created: '2026-10-01'
status: done
route: 'full'
route_source: 'auto'
baseline_revision: '8b7b8f68d34d1453e5b45798f0724dea17c38b3d'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/docs/contributing/touch-and-ui.md'
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.skills/scope-discipline/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The launcher still lists a Continue row per saved game above the games (A1, A12, A14), peeks every startable game's save when it is built, and resumes a Continue row with a solo roster; a registry load that runs out of memory reads as "No games found" (rev-6). Since entry 7 every game row opens the game's title screen, which offers Continue itself, so the Continue rows are a second, solo-only way in.

**Approach:** `GamesLauncherActivity` lists one row per game (icon, name, and only an unavailable game's reason under it); a startable row pushes the title screen; after Leave the next launcher selects the game's own row on the page holding it; the launcher reads no save; a failed load shows "Not enough memory". `ContinueLauncherTest` goes: each case moves to `TitleScreenTest` or retires (named below). The launcher suites share one helper for "which title screen did the row open".

## Boundaries & Constraints

**Always:** remove the code the Continue rows leave dead (`continueOf`, `continueCount`, `gameOfRow`, `loadContinue`, the Continue branches, the `GameMatchActivity.h`, `GameSaveStore.h`, `GameHostCaps.h` includes); keep every other guard (bounds, unavailable, title-screen OOM, padding rows, the note and dialog tap guards, `lastOpened` and its clear on remove); reuse `STR_GAMES_OUT_OF_MEMORY` (no new string); new cases appended at the end of `ModePickerTest.cpp` (lane A may touch the file's middle); a test double change names the device behaviour it stands in for.

**Never:** change `GameModeActivity.*`, `GameMatchActivity.*`, `src/games/**`, `MatchSupport.h`, `formats.md`, or any upstream file; add a save marker on a launcher row (it would need `peek`); move or rewrite `TitleScreenTest` cases lane A's entry 9 may change.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Saves on the card | 3 games, 2 valid saves | rows Alpha, Bravo, Charlie; no "Continue" line; no `/.games-data/` exists or open on entry, scroll, or remove | — |
| Row with a save | tap or Confirm on its row | title screen pushed, Continue first and selected; a second Confirm resumes | — |
| After Leave | 25 games, Continue on Game 20, Leave | next launcher shows the whole page holding Game 20's row, selected; Confirm opens Game 20's title screen | game gone: top |
| Under the note | install note up, save on Game 01 | Confirm dismisses; Confirm pushes the title screen; no match until a choice there | — |
| OOM load | `GameRegistry::load` false (entry or reload after remove) | "Not enough memory", not "No games found"; Confirm and tap open nothing | the registry logs the OOM |

</frozen-after-approval>

## Code Map

- `src/activities/games/GamesLauncherActivity.h` -- rewrite the class comment (one row per game; a row pushes the title screen; no save read; the selection after Leave; the OOM message), drop `loadContinue`, `gameOfRow`, `continueOf`, `continueCount`; `rowCount()` = `listing.count`; add `bool listFailed` (written in `loadGames`, read by `buildScreen`, like `noteVisible`).
- `src/activities/games/GamesLauncherActivity.cpp` -- `onEnter`/`confirmRemove` drop `loadContinue()`; `selectRemembered` selects row `i`; `openRemoveDialog` drops the Continue branch (`removeIndex = row`); `confirmRemove` selection `min(index, count - 1)`, comments lose "Continue rows"; `loadGames` sets `listFailed`; `buildScreen` draws `STR_GAMES_OUT_OF_MEMORY` when `listFailed`; `provideRow` indexes the listing directly; `activateIndex` keeps only the title-screen push. Includes as above.
- `test/game_script/harness/LauncherSupport.h` (new, header-only) -- `launcher::openedTitle()`: enters and draws the title screen the launcher pushed, returns its header (the game's name, or a `<...>` diagnostic), lets it go as Back does, and restores `RecordingTarget::newest()` to the launcher's; `launcher::startNewOnTitle(MappedInputManager&)`: the three copies, moved, for a case that must Leave a real match.
- `GamesLauncherTest.cpp` -- the 11 which-game cases read `openedTitle()`; renames `AGameOnlyAnotherModeCanStartOpensItsMatchAndAGameWithTwoModesOpensThePicker` → `AGameOnlyAnotherModeCanStartHasNoReasonAndEveryStartableRowOpensItsTitleScreen`, `ATapOnARowReplacesTheListWithThatGamesMatch` → `ATapOnARowPushesThatGamesTitleScreen`; the stale `## 4.8` comment; add `ARegistryLoadThatRunsOutOfMemoryIsReportedNotShownAsNoGames` and `AReloadAfterARemoveThatRunsOutOfMemoryIsReported` (`oom::failSize` on the registry's `Entry[]`; `RemoveScript.h` for the remove).
- `GameRemoveLauncherTest.cpp` -- which-game cases read `openedTitle()`; ReturnTest's first case keeps a real match (`startNewOnTitle`); rename `AGameOpenedThroughTheModePickerIsRememberedToo` → `AGameWhoseTitleScreenWasOpenedIsRememberedWithNoMatch` (its row, Back, and Games again).
- `ModePickerTest.cpp` -- `TheLaunchersContinueRowStillResumesDirectly` → `AGameWithASaveHasOneRowThatOpensItsTitleScreenWithContinueFirst`; `openTitleThroughLauncher`'s stale comment; append the moved cases (below).
- `ContinueLauncherTest.cpp`, `continue_launcher.cmake` -- deleted (suites are globbed); `games_launcher.cmake` -- "mode picker" → "title screen".
- `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 4.12` peek-at-entry item marked resolved; `## 5.8` appended.

## Tasks & Acceptance

**Execution:**
- [x] `GamesLauncherActivity.{h,cpp}` -- the change above
- [x] `LauncherSupport.h` -- the two helpers; the three fixture copies removed
- [x] `GamesLauncherTest.cpp`, `GameRemoveLauncherTest.cpp` -- the edits above
- [x] `ModePickerTest.cpp` -- appended cases: `WithSavesOnTheCardTheLauncherIsOneRowPerGameAndReadsNoSave` (3 games, 2 saves, entry, swipe, remove: no `/.games-data/` op), `TwentyFiveGamesWithSavesPageAsOneRowPerGame`, `ConfirmOnTheGamesRowThenConfirmResumesItsSave` (from #9), `ASaveOfAChangedPackageOffersNoContinueAndIsLeftOnTheCard` (#3), `ASaveThatIsNotAValidResumeOffersNoContinue` (#5), `ASaveWhoseRenameWasInterruptedStillOffersContinue` (#7), `ASaveThatWentBadBetweenTheScreenAndTheTapStartsANewMatch` (#11), `LeavingAMatchStartedFromContinueSelectsTheGamesRowOnThePageHoldingIt` (#18), `TwoConfirmsAfterLeavingAContinueMatchResumeAgainAndLeaveTheSaveAlone` (#19), `ANewMatchLeftAfterAMoveIsResumedByConfirmOnItsRowAndConfirmOnContinue` (#21), `UnderTheInstallNoteConfirmsOpenTheTitleScreenAndNeverANewMatch` (rev-11)
- [x] `ContinueLauncherTest.cpp`, `continue_launcher.cmake`, `games_launcher.cmake` -- delete; comment
- [x] `deferred-work.md` -- as above

**ContinueLauncherTest dispositions (29):** moved: #3, #5, #7, #9, #11, #18, #19, #21 (names above). Retired, no Continue rows: #1 `OneContinueRowForEachValidSave...` and #15 `ContinueRowsPage...` (replaced by the one-row cases), #2 `WithNoSaveTheListIsTheGamesAlone`, #10 `TheGamesOwnRowStillStartsANewMatch...` (covered by `ANewRowOverASaveAsksFirst...`), #12 `ALongPressOrAHoldOnAContinueRow...`, #13 `ATapOnAContinueRowUnderTheConfirmationOrTheNote...` (game-row guards: `ATapOnARowBeforeTheConfirmationIsDrawnOpensNothing`, `ConfirmAndATapAlsoDismissTheNote...`), #14 `ALongPressOnAContinueRowUnderTheNote...` (`UnderTheNoteAHoldOrALongPress...`, and the rev-11 case), #16 `TheKeysWalkContinueRowsThenGamesAndWrap`, #17 `AStepBackFromTheFirstRow...` (`APreviousKeyHeldFromTheFirstGameWraps...`, `TheKeysNeverLandOnABlankRow...`), #20 `AfterLeavingAGameWithNoSave...` (ReturnTest's first case), #22 `RemovingAGameDropsItsContinueRow...`, #23 `ACardWithTwentyFiveGamesCostsOneReadPerSave...` (a peek count; replaced by the no-read case), #24 `NoGamesAndNoSavesAreAsBefore`, #25 `RemovingAGameWithNoSaveKeeps...`, #26 `ARemoveThatFailsKeepsTheContinueRows...` (`ADeleteThatFailsIsReported...`), #27 `RemovingTheOnlyGameLeaves...` (`RemovingTheOnlyGameShowsTheEmptyList`), #28 `TheContinueListHoldsASaveForEveryGame...` (the array goes), #29 `AContinueRowDrawsItsOwnGamesIcon` (icon cases); covered already: #4 `AGameThisHostCannotStartHasNoRow...` (`AGameThisHostCannotStartOpensNeither...` plus the no-read case), #6 `ASaveTheCardWillNotRead...` (`ASoloGamesUnreadableSave...`, `ATwoModeGamesUnreadableSave...`), #8 `ATapOnContinueResumes...` (`ConfirmWithNothingMovedResumesTheSoloSave`).

**Acceptance Criteria:**
- Given every host suite, when run, then all pass, and no launcher suite expects a `peek` (the one launcher count asserts zero `/.games-data/` ops).
- Given the x4pro build, when `check_flash_budget.py` runs, then the epic stays within +244,592 B flash and +816 B static RAM over `c1902721`.

## Implementation Notes

- Implemented by a context-free implementation subagent from this plan (its hand-back reached the orchestrator, which relayed it): host suites 1,481 of 1,481, fast checks pass.
- The review patches were applied by the build agent, not the implementer: in this run the implementer's hand-backs are delivered to the orchestrator, not to the build agent.
- `launcher::openedTitle` takes the launcher (`openedTitle(GamesLauncherActivity&)`; each suite's fixture wraps it as a no-argument `openedTitle()`): the title screen's recording target is built last and clears `RecordingTarget::newest()` when it goes, and the launcher's own target is reachable only through its protected `UiAppHost` base (`launcher::TargetOf`), so the helper needs the launcher to restore it.
- The moved `ContinueLauncherTest` cases and the new one-row cases run under `OneRowPerGameTest`, a fixture derived from `TitleScreenTest` and appended after `HostCapsDouble.DefaultsAreTheDevicesValues`, so the helpers they need (25 games, a remove, the launcher rebuilt after Leave) do not touch `TitleScreenTest`'s fixture in the file's middle. `WithSavesOnTheCard...` ends with a control: the title screen a row opens does peek, and the op count sees it.
- `GameRemoveLauncherTest.ATapOnARowBeforeTheConfirmationIsDrawnOpensNothing`: its Gamma check read `asks.replaced`, which no row asks for any more; it reads `asks.pushed`.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-10-01). The four lenses ran as context-free subagents, launched together in one message (each told the worktree is read-only), and all four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG: no gaps, one other finding), intent-alignment (IA, descriptive). Verdicts: low 10, false 5, plus IA's report and its divergences (rows 13 to 15). No intent_gap or bad_plan. The build agent applied the patches; host suites and fast checks re-ran on the patched tree (Verification).

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | BH1, IA R4 | `LauncherSupport.h` says `ModePickerTest` shares it; that file does not include it | low | patch | Comment names the two suites that use it, and says `ModePickerTest`'s fixture opens the pushed screen itself, since its cases go on into the title screen |
| 2 | BH3, EC2 | `startNewOnTitle` leaves `newest()` to the screens that went, and skips New game when a replacement was already there | low | patch | Comment: the case builds the launcher again before reading it (its one caller does, `reopen`); it compares the replacement count before the tap |
| 3 | BH4 | `openedTitle` reads only the header: a row that pushed and also started a match would pass | low | patch | Returns `<a match started>` when a replacement exists |
| 4 | EC1, BH4 | On its diagnostic paths `openedTitle` leaves the pushed screen, so later calls say `<2 screens pushed>` | low | reject | The first failure names the cause exactly (`<not a title screen>`, the push count); the follow-on is test output only, and TearDown's `activityManager.reset()` frees it |
| 5 | BH5, VG other | The `## 5.8` stale-text item misses `MatchSupport.h`'s `Saves` comments, `GameSaveStoreTest.cpp`'s, more of `formats.md`, and `mode_picker.cmake`'s "unchanged" | low | patch | Item rewritten to name them all (outside touches, for the sweep); `mode_picker.cmake`'s comment fixed here |
| 6 | BH8 | `ModePickerTest.cpp`'s two comments naming the deleted suite were left "for lane A", though the file's middle was edited | low | patch | Lane A's entry 9 does not touch the file (orchestrator, 2026-10-01): both comments reworded, the deferral's reason dropped |
| 7 | BH7 | Two `## 5.7` items whose trigger was entry 8 are unanswered: `pass-hidden` in the installer test; the launcher's solo-only `peek` part | low | patch | `## 5.8` carries `pass-hidden` to the sweep (`GamePackageInstallerTest.cpp` is not a launcher suite: outside touches) and resolves the `peek` part |
| 8 | BH11, BH12 | The 3-game no-read test's "scroll" scrolls nothing; #18's saves of Games 1 to 10 have no stated purpose | low | patch | Message says a redraw (the 25-game case scrolls); a comment says those saves once put Continue rows ahead and now shift no row |
| 9 | BH2 | `idOf`, `nameOf`, `takeGameOffTheCard`, `key`, `shown` are copied across suites | low | reject | Small per-suite fixture steps in anonymous namespaces, the harness's existing pattern; moving them is a wider refactor, and no copy has drifted |
| 10 | BH6 | `## 5.7` items are resolved by new `## 5.8` entries, not in place as `## 4.12`'s was | false | reject | The file's convention for one epic's items (entry 7 resolved `## 5.1` and `## 5.2` items under `## 5.7`; `merge=union` makes in-place edits by two lanes risky); `## 4.12` in place is the ticket's own wording |
| 11 | BH9 | The plan records no evidence, checkboxes open, flash bar versus the 250 KiB gate | false | reject | The review runs before firmware builds, measurement, and screenshots (AGENTS.md's order); both limits apply (the CI gate and the epic's share), and Verification records both |
| 12 | BH10, BH13, EC3 | OOM tests: the log line is shared by both allocations; the tap on an empty list hits nothing; `ListTest::dropMatch` unused; #6's solo read fault not covered | false | reject | `failSize` matches only the `Entry[]` array allocation (the reader is a non-array `new`), so the path is fixed by the size, not the log; the tap and Confirm show an empty list opens nothing, as `AConfirmOrATapOnAnEmptyListDoesNothing` does; `dropMatch` still runs in `reopen` and TearDown, a cleanup; a solo read fault is the same `Unreadable` answer (`GameSaveStore` tests) and the match's error view on a read fault is `ResumeMatchTest` (failReadAt, line 784) |
| 13 | IA | Leave is modelled by rebuilding the launcher (`reopen`), not `ActivityManager::goToGames()` | false | reject | The harness's manager is a double since epic 4 (`## 4.13` item); `lastOpened` is a file static the real `goToGames()` launcher reads the same way |
| 14 | IA | Only the `Entry[]` OOM is tested, not the manifest reader's | low | reject | Both take the same `return false`; the launcher sees only that `bool` |
| 15 | IA | No screenshots in the reviewed diff | false | reject | They are taken after the review (Verification) |

## Design Notes

**Layout (entry 3 reviews it):** a row is the game's icon and name; a second line only for a game this host cannot start (its reason), as today. No save marker: it would need `peek` per game at entry (unmeasured on a device, `## 4.12`), and the title screen says it on its first row. The selection after Leave is the game's own row on the whole page holding it: a Confirm there opens the title screen, whose first row is Continue when there is a save, so two Confirms resume and none starts New (A12's reason, ee26fa2f, now holds through the title screen).

**Guards in the rewritten functions:** `selectRemembered`: `lastOpened == 0` (fresh boot or removed game: top), not found (gone: top). `openRemoveDialog`: bounds, padding row, note. `activateIndex`: bounds, unavailable (logs, repaints), title-screen OOM (logs, repaints). `confirmRemove`: index bounds; the reload under `RenderLock`. `loadGames`: a false load leaves an empty listing and now `listFailed`.

## Verification

**Commands:**
- Host suites (AGENTS.md CMake/Ninja/ctest, hosttest lock) -- all pass.
- `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py`, `./bin/clang-format-fix` twice -- pass, nothing new.
- Build lock, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`, `sim.sh build x4pro`; `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`.
- Simulator screenshots into `story-one-row-screenshots/`: the launcher with saves on the card (one row per game); a game's title screen opened from it with Continue first; the launcher after Leave with the game's row selected on a later page.

**Results (2026-10-01, after the review patches, on the tree this entry commits):**
- Host suites: CMake/Ninja build and `ctest -j4` under `/tmp/crosshatch-hosttest.lock`: 1,481 of 1,481 pass (`ContinueLauncherTest`'s 29 gone; `GamesLauncherHarnessTest` 60, `GameRemoveLauncherTest` 40, `ModePickerHarnessTest` 52, with `OneRowPerGameTest`'s 11).
- Matrix audit: saves on the card: `OneRowPerGameTest.WithSavesOnTheCardTheLauncherIsOneRowPerGameAndReadsNoSave`, `TwentyFiveGamesWithSavesPageAsOneRowPerGame`; row with a save: `ConfirmOnTheGamesRowThenConfirmResumesItsSave`, `TitleScreenTest.AGameWithASaveHasOneRowThatOpensItsTitleScreenWithContinueFirst`; after Leave: `LeavingAMatchStartedFromContinueSelectsTheGamesRowOnThePageHoldingIt`, `ReturnTest.*`; under the note: `UnderTheInstallNoteConfirmsOpenTheTitleScreenAndNeverANewMatch`; OOM load: `ListTest.ARegistryLoadThatRunsOutOfMemoryIsReportedNotShownAsNoGames`, `AReloadAfterARemoveThatRunsOutOfMemoryIsReported`. Each ran and passed.
- `check_upstream_touches.py`: PASS (no upstream file touched). `check_layers.py`: 484 include edges pass. `./bin/clang-format-fix` twice: nothing new, nothing outside these paths.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` SUCCESS, `pio run -e default` SUCCESS, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` PASSED (no defects), the same for `default` PASSED, `sim.sh build x4pro` SUCCESS.
- Measurement (entry 8, 2026-10-01): on this entry's tree (lane-b on `8b7b8f68`, firmware sources unchanged between the measurement and the commit, which adds only tests and docs beside them), `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`: x4pro `firmware.bin` 5,921,376 B games on, 5,679,632 B off, +241,744 B (14,256 B under the gate); static internal RAM +784 B (`.dram0.bss` +16, `.iram0.text` +684, `.iram0.text_end` +84; 240 B under); `objects` clean (43 objects, largest mutable static 4 B, `lastOpened`). Over the base at `c1902721` (+233,440 B, +784 B): +8,304 B flash and +0 B static RAM, within the 11,152 B and 32 B bar, leaving 2,848 B and 32 B for entries 9 and 10. Against entry 7's measurement, taken the same way at `f9f08a39` (+242,096 B): this entry is -352 B flash, +0 B static RAM.
- No CI gate or workflow changed, so no fresh-tree run.
- Simulator (x4pro, 12 packed fixtures: two pages of 8 rows), in `story-one-row-screenshots/`:
  - `launcher-saves-one-row-each.png`: after Counter was played one tap and left (its `resume.bin` on the card), the launcher lists one row per game, no Continue row, Counter's row selected on page 1.
  - `launcher-after-leave-page2-row-selected.png`: after Tracer (page 2) was played one tap and left, the next launcher shows page 2 with Tracer's own row selected.
  - `confirm-opens-title-continue-first.png`: Confirm on that row opens Tracer's title screen, Continue first and selected, then Solo (log `Title screen of tracer: save valid`).
  - `second-confirm-resumes.png`: a second Confirm resumes the saved move ("Taps: 1 of 5"; log `Continue tracer: a solo match`, `Resuming at ver 2`).

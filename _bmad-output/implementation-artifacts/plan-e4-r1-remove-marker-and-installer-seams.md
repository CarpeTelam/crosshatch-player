---
title: 'A remove reported as failed leaves the game kept, and the installer tests pin the probe order and OOM (e4-r1, AI-14, AI-15)'
type: 'bugfix'
ticket: ''
created: '2026-09-30'
status: 'built'
baseline_revision: '40a1409ab116d476130fb899234668185f03bc69'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** (rev-1) `remove` writes `.removing`, then fails to delete the `.pkg` and returns `SdCard` with the marker left; the launcher says "Could not remove it", and the next visit's `finishRemovals` deletes the game silently. (rev-2) Nothing pins the `.xlink` probe's create → exists → remove order: the fake's `exists` logs nothing. (rev-4) No installer suite fails a nothrow allocation, so dropping `OutOfMemory` from `packageIsInvalid` passes every test.

**Approach:** When the `.pkg` delete fails, `removeFolder` deletes the marker, best effort, only when this call wrote it and the `.pkg` is still there. The fake logs `exists <path>`; tests pin the probe order on the install and remove paths. A new installer suite overrides nothrow `operator new`/`new[]` and fails each allocation of an install in turn.

## Boundaries & Constraints

**Always:** Keep every guard of `removeFolder`/`remove` (id check, `/.games` open, absent folder is done, the probe before the marker, the marker before the `.pkg`, the `.pkg` before the rest, marker last, no rewrite of an existing marker). No static RAM added. Frames in `GamePackageInstaller.cpp` stay at 256 B or less. Each new test fails with its fix or seam mutated. Files: `src/games/GamePackageInstaller.{cpp,h}` (remove comment only in `.h`), `test/game_script/harness/stubs/HalStorage.h`, `GameRemoveTest.cpp`, `GamePackageInstallerTest.cpp`, new `GameInstallerOomTest.cpp` and `installer_oom.cmake`, `docs/crosshatch/formats.md`, this plan, `deferred-work.md` `## e4-r1`.

**Never:** `src/activities/games/*`, `lib/I18n/*`, `Report`, `installAll`'s failure counting (lane e4-r2). No edit of an existing harness `.cmake` file (the harness README's rule). No upstream file.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| `.pkg` will not go, marker written by this call | listed game, `failRemove` `.pkg` | `SdCard`; marker gone; game listed and whole; a visit with the card back keeps it | logged |
| `.pkg` delete reports failure but the entry went | fake `.pkg` removed with `false` | `SdCard`; marker kept; next visit finishes the folder | logged |
| Marker there before the call (earlier stopped remove) | marked listed game, `.pkg` refuses, `remove` or a visit | `SdCard`; marker kept; next visit with the card back finishes | logged |
| Marker delete also fails | `.pkg` and `.removing` refuse | `SdCard`; marker stays; next visit finishes (as before) | logged |
| Probe order | `/.games/g` without `.pkg` beside `/.games-tmp/g`, visit or `remove` | ops: open, close of the tmp probe, `exists /.games/g/.xlink`, remove of the tmp probe, in that order | none |
| Failed allocation during install | valid package, k-th nothrow allocation fails, for every k | `OutOfMemory`; `.chgame` stays in the inbox, never `.chgame.bad`; nothing listed; next visit installs | logged |

</frozen-after-approval>

## Code Map

- `src/games/GamePackageInstaller.cpp` -- `removeFolder` (~L322-362): `hasRemovingMarker(id)` result becomes a local `markerWasThere`; the `.pkg`-delete failure branch reuses `path` for the exists check and the marker delete. `finishRemovals` only calls it on marked folders, so it never drops a marker. `foldersShareClusters` (~L195), `mayRemoveTmp` (~L212), `packageIsInvalid` (~L835) unchanged.
- `src/games/GamePackageInstaller.h` -- `remove`'s `SdCard` line: a `.pkg` that will not go leaves the game listed, whole, and unmarked (unless a marker was there).
- `test/game_script/harness/stubs/HalStorage.h` -- `exists` pushes `"exists <p>"`; new `failRemoveDone` set (remove deletes the entry, returns false); ops comment lists `exists`.
- `test/game_script/harness/GameRemoveTest.cpp` -- rename `AMarkerThatWillNotGoLeavesTheGameListedAndWhole`; rework `AStopAfterTheMarkerIsFinishedOnTheNextVisit` (it started from the old behavior: now a marker placed on an installed game); `ARemoveOfAGameThatIsNotThere…` allows `exists `. Helpers: `opIndex`, `visit`, `listed`, `placeMarkedFolder`.
- `test/game_script/harness/GamePackageInstallerTest.cpp` -- probe-order test beside `ScratchBesideAnIndependentHalfRemovedGame…` (~L750).
- `test/game_script/harness/installer_oom.cmake` + `GameInstallerOomTest.cpp` -- own executable over `game_installer_src` (as `remove_game.cmake`), so the override reaches no other suite; `GamesLauncherTest.cpp:36` shows the override pattern; allocations counted: `names[]`, `Job` (installAll), `ZipScratch`, `GameHash`.
- `docs/crosshatch/formats.md` ~L337-347 -- the remove paragraph says a failed step-1/`.pkg` remove is finished next visit; rewrite.

## Tasks & Acceptance

**Execution:**
- [x] `src/games/GamePackageInstaller.cpp` -- drop own marker on `.pkg` delete failure, with comment -- rev-1
- [x] `src/games/GamePackageInstaller.h` -- `remove` contract -- rev-1
- [x] `test/game_script/harness/stubs/HalStorage.h` -- log `exists`; `failRemoveDone` -- rev-2 seam, rev-1 guard test
- [x] `test/game_script/harness/GameRemoveTest.cpp` -- the first four matrix rows plus the remove-path probe order -- rev-1, rev-2
- [x] `test/game_script/harness/GamePackageInstallerTest.cpp` -- install-path (`mayRemoveTmp`) probe order -- rev-2
- [x] `test/game_script/harness/installer_oom.cmake`, `GameInstallerOomTest.cpp` -- allocation sweep -- rev-4
- [x] `docs/crosshatch/formats.md` -- remove paragraph -- contract text
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## e4-r1` at the end -- record

**Acceptance Criteria:**
- Given the fix reverted, then the renamed rev-1 test fails; given `!markerWasThere` or the `.pkg` exists check dropped, then its test fails; given `exists` moved after the probe's remove, then both order tests fail; given `OutOfMemory` dropped from `packageIsInvalid`, then the sweep fails.
- Given the whole host suite, then every test passes with `exists` logged.
- Given the flash budget gate at the final commit, then static RAM is unchanged from the base and flash within the epic's bar.

## Implementation Notes

- `removeFolder`: `markerWasThere` holds `hasRemovingMarker(id)`; on a failed `.pkg` delete, `!markerWasThere && Storage.exists(path)` gates a best-effort delete of `.removing` through the same `path` buffer (no new buffer). Every earlier guard is unchanged.
- Fake card: `exists` logs `exists <p>`; `failRemoveDone` applies to `remove` of a file only (not `rmdir`). Only `ARemoveOfAGameThatIsNotThere…` needed the new op allowed; the whole host suite passed with it (1366 tests, including the new ones).
- `GameRemoveTest`: the renamed test is `APkgThatWillNotGoLeavesTheGameListedWholeAndUnmarked` (row 1); new `APkgDeleteThatFailsAfterTheEntryWentKeepsTheMarkerForTheNextVisit` (row 2), `APkgAndAMarkerThatWillNotGoLeaveTheMarkerForTheNextVisit` (row 4), `TheProbeLooksForItsFileBeforeItRemovesIt` (row 5, remove path); `AStopAfterTheMarkerIsFinishedOnTheNextVisit` reworked to row 3 and renamed `AMarkerFromAnEarlierRemoveStaysWhenThePkgWillNotGoAndTheNextVisitFinishes` (a marker placed on an installed game, kept by both `remove` and a visit). `GameInstallerTest` gains `TheProbeLooksForItsFileBeforeItRemovesIt` (install path, `mayRemoveTmp`).
- `GameInstallerOomTest`: counts nothrow `new`/`new[]` only while armed around `installAll`, measures the total on a clean install (asserts at least 4), then fails each k in turn; a package without images (the converter maps its own OOM to `BadImage`, recorded in `deferred-work.md`).
- Mutations run (each reverted): fix reverted -> `APkgThatWillNotGo…` and `APkgAndAMarkerThatWillNotGo…` fail; `!markerWasThere` dropped -> `AMarkerFromAnEarlierRemoveStaysWhenThePkgWillNotGoAndTheNextVisitFinishes` fails; `.pkg` exists check dropped -> `APkgDeleteThatFailsAfterTheEntryWent…` fails; `exists` moved after the probe's remove -> both `TheProbeLooksForItsFileBeforeItRemovesIt` fail; `OutOfMemory` dropped from `packageIsInvalid` -> the sweep fails.

## Plan Change Log

## Review Triage Log

The four lenses ran as context-free subagents, launched in one message, and all four returned: `blind-hunter` (10 findings), `edge-case-hunter` (2, plus a deletion-check note), `verification-gap` (no gaps; 2 other findings), `intent-alignment` (descriptive). Verdicts (high/medium/low/false/maybe-false) over 13 rows: 0 / 1 / 10 / 2 / 0. Nothing routed to intent_gap or bad_plan; the patches went to the implementation subagent in one message and it fixed all of them.

| # | Lens | Finding | Verdict | Route and evidence |
|---|------|---------|---------|--------------------|
| 1 | blind, vg | `ZipFile::readFileToStream`'s and `InflateStream`'s `malloc` failures become `BadSize`, so a valid package on a low heap is renamed `.chgame.bad` (vg reproduced it on a scratch copy) | medium | defer: pre-existing (predates e4-r1), and the fix is in upstream `lib/ZipFile` or in how the installer reads a failed stream; `deferred-work.md` `## e4-r1` third entry, trigger the AI-12 split or a device log. |
| 2 | blind, vg | the OOM test header and the deferred entry overstate what the sweep covers (only the converter named as unswept; deflate branch never run) | low | patch: header and entry now name every unswept allocation and the stored-only packages. |
| 3 | blind, edge | `Storage.exists(.pkg)` reads false on a card fault, so the marker is kept on a listed game and the next visit finishes it | low | reject the code change (needs a card that fails the delete and then the stat; the guard edge proposed adds a branch and a stat); patch the record: the `.cpp` comment, `formats.md`, and Design Notes say a card that cannot answer counts as "gone", the side that never strands an unlisted folder. |
| 4 | edge | `finishRemovals` saw the marker but `removeFolder`'s re-read says none: the marker is rewritten, then dropped when the `.pkg` will not go | low | reject: needs a flaky stat between two reads; the outcome is a game kept whole that the person can remove again; the fix adds a parameter. |
| 5 | edge | no test checks that the marker `writeRemovingMarker` writes is empty any more | low | patch: `APkgDeleteThatFailsAfterTheEntryWent…` asserts it. |
| 6 | blind | `ASSERT_GE(total, 4u)` lets the sweep shrink silently | low | patch: `ASSERT_EQ(total, 4u)`. |
| 7 | blind | the sweep checks neither stray inbox files nor `firstFile` | low | patch: inbox holds one file; `firstFile` is `g.chgame` for allocations 3 and 4, empty for 1 and 2 (installAll's own, before any file is judged; `Report` is lane e4-r2's). |
| 8 | blind | the probe-order test is copied into two suites | low | reject: two suites pin two callers; a shared helper adds surface in `InstallerSupport.h`, outside this plan's files, for two copies. |
| 9 | blind | Verification names only `x4pro` and `default`; no build figures recorded yet | false | the brief asks for those two (the orchestrator builds all five); figures are recorded in Verification below. |
| 10 | blind | `formats.md` step 2 not updated; a sentence repeated | low | patch: step 2 says what a failed `.pkg` delete does; repeat removed. |
| 11 | blind | nothing records that this reverses e4-z2's ae 2 | low | patch: Design Notes and the first `## e4-r1` entry say AI-14 supersedes it. |
| 12 | blind | the reworked test's name says "a stop after the marker" but it places the marker by hand | low | patch: renamed `AMarkerFromAnEarlierRemoveStaysWhenThePkgWillNotGoAndTheNextVisitFinishes`. |
| 13 | intent | probe order pinned through two of four callers; the rev-1 symptom remains for a retry over an earlier marker and a marker that will not go | false | descriptive: all four callers run the one `foldersShareClusters`, and swapping its lines fails both tests (vg confirmed); the two residuals are the plan's decisions, recorded in `deferred-work.md`. |

## Design Notes

History: `removeFolder` came out of `remove` in 1d13ad18 (e4-z2); `git log -L` on it and on `foldersShareClusters` shows 5c49754c, d731afff, b976e765, c47cceaa, 1d13ad18, c2aa2b4e. Guards kept, and what each protects: id check (a path out of `/.games`); `/.games` must open (a mute card is not "gone"); absent folder is `None`; the probe before the marker (a file written into a folder that shares clusters shows in the other); marker before `.pkg` (a stop leaves a folder the next visit can identify); `.pkg` before the rest (never a listed game with files missing); marker last (FAT slot reuse); no rewrite of an existing marker (a rewrite whose close failed would strand a folder whose `.pkg` is gone, vg F2).

Existing marker: kept when the `.pkg` will not go. AI-14 says "its own `.removing`"; a marker already there is an earlier remove's that stopped partway, and `finishRemovals` always runs over such a marker, so dropping it would undo A18 (a stop after step 1 is finished next visit). A person's retry over it gets "Could not remove", and the earlier request still finishes next visit: rare (it needs a stop or a double failure first), recorded in `deferred-work.md`.

`.pkg` exists check: SdFat's `remove` can return false after the entry went (a failed sync); dropping the marker then would leave the unmarked, unlisted folder e4-z2 closed. Only a `.pkg` still there makes the game "kept".

This supersedes e4-z2's ae 2 decision (a failed `.pkg` delete is finished at the next visit, "the person chose Remove") for the case where this call wrote the marker. `Storage.exists` reading false on a card fault is treated as "gone", so the marker is kept: accepted because the opposite choice risks the unmarked orphan e4-z2 closed, while keeping it finishes a remove the person asked for.

Probe order: the fake cannot alias two folders, so the order is pinned through the op log, first occurrences, on a card where only one probe runs.

## Verification

**Commands:** run in the git worktree that holds this plan (never another checkout), `git submodule update --init --recursive` first; every host CMake configure/build and `pio` command under `flock /tmp/claude-0/-home-user-crosshatch-player/4faa8743-945f-5626-850b-328812382a3a/scratchpad/build.lock sh -c '…'` (other agents share the lock and `~/.platformio`; wait on it, never reinstall the toolchain). The implementer runs the host suite and the mutations; the build agent runs the rest.
- host: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j8` -- all pass
- mutations above, each reverted -- each named test fails
- `python3 scripts/<name>_test.py` (all), `check_layers.py`, `check_upstream_touches.py` -- pass
- `pio run -e x4pro`, `pio run -e default` -- SUCCESS
- `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- rc 0, figures recorded
- `./bin/clang-format-fix` twice -- `git status` shows nothing new

**Results** (at the final code, the tree this commit holds; logs in the scratchpad's `e4-r1/`; every build under the shared lock):
- Host suites: `cmake --build build/test` rc 0, `ctest -j8`: `100% tests passed, 0 tests failed out of 1366` (base 1361; +5 new cases: three in `GameRemoveTest`, one in `GameInstallerTest`, `GameInstallerOomTest`'s one). The implementer and the verification-gap lens each ran the five mutations of the Acceptance Criteria on the code (fix reverted, `!markerWasThere` dropped, the `.pkg` exists check dropped, `exists` after the probe's remove, `OutOfMemory` dropped from `packageIsInvalid`); each failed its named test.
- `scripts/*_test.py`: all 11 rc 0. `check_layers.py`: passed (469 include edges in 107 game files). `check_upstream_touches.py` on the commit: `Result: PASS`.
- `check_flash_budget.py`: `build on` rc 0 (this is the `pio run -e x4pro` build: SUCCESS), `build off` rc 0, `compare --limit-kib 250 --ram-limit-bytes 1024` rc 0: flash on 5,911,984 B, off 5,678,992 B, **+232,992 B**; static internal RAM on 187,848 B, off 187,064 B, **+784 B**; `objects` rc 0 (43 game objects, largest mutable static 4 B). The orchestrator's base at `40a1409a`, the same four steps: +232,912 B and +784 B. So e4-r1 adds **+80 B flash and 0 B static RAM**; against the pass bar (+240,496 B, +808 B) 7,504 B and 24 B remain. The `removeFolder` frame is unmeasured (one `bool` added, no buffer; e4-z2 measured it at 160 B).
- `pio run -e default`: SUCCESS. `sim.sh build x4pro`: SUCCESS.
- `./bin/clang-format-fix` (clang-format 21.1.8) twice as the last step: no change, `git status` clean but for the staged work.


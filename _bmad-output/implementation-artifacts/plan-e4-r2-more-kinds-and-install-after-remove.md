---
title: 'Name the kind in "and N more", and install the inbox after a remove (e4-r2, AI-2)'
type: 'feature'
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
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/epic-install-and-launcher-retrospective.md'
  - '{project-root}/_bmad-output/implementation-artifacts/plan-e4-z3-and-n-more.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** At the 64-game limit every package judged after its manifest read fails `TooManyGames` and stays in `/games`, so e4-z3's note "…: Too many games are installed; remove one first / and 2 more" reads the same whether the others wait for room or are broken (retro Q2, `d-03`/`d-05`/`d-06`). And following "remove one first" does nothing until Games is reopened: `confirmRemove` reloads the listing but never installs (rev-3).

**Approach:** The owner's answer to Q2 (2026-09-30), AI-2. The installer's `Report` counts the waiting packages; the launcher's note names the kind of the other failures: the waiting ones on a line "N more waiting for room", the rest on "and N more not installed"; with no waiting package among the others, e4-z3's "and N more" stays. After a successful remove, when the inbox holds a file, the launcher runs the same install it runs on entry ("Installing" popup and note), then reloads the listing.

## Boundaries & Constraints

**Always:** the installer's order is unchanged (a package is judged only when there is room); at most two new `STR_GAMES_*` keys, appended to `english.yaml`; the note stays one panel, each line its own `text()` call with no `\n`; e4-z3's tests keep passing; `confirmRemove` keeps every guard (index range check, id/name copied before the reload, the listing replaced under `RenderLock`, `lastOpened` cleared only on success, the more-lines cleared under a remove-failed note); no static RAM.

**Never:** install while holding `RenderLock`; take `RenderLock` recursively; touch the installer's remove/`finishRemovals`/`foldersShareClusters` code, `stubs/HalStorage.h`, `GameRemoveTest.cpp`, `GamePackageInstallerTest.cpp` (lane e4-r1); anything in `GamePackageInstaller.cpp` beyond the one counting site.

## I/O & Edge-Case Matrix

| Scenario | Report (failed, waiting, first) | Note lines under the reason |
|---|---|---|
| one failure | 1, any, any | none |
| others, none waiting | 3, 0, BadCrc / 3, 1, TooManyGames | "and 2 more" (unchanged) |
| only waiting others | 3, 3, TooManyGames / 2, 1, BadImage | "2 more waiting for room" / "1 more waiting for room" |
| both | 4, 3, TooManyGames | "2 more waiting for room", then "and 1 more not installed" |
| N = 1 each | 3, 2, TooManyGames plus one other | "1 more waiting for room", "and 1 more not installed" |
| saturated | 255, 255, TooManyGames | "254 more waiting for room" |
| remove ok, inbox holds a file | — | "Installing" popup, installAll outside the lock, note if it fails, listing reloaded with the new game |
| remove ok, inbox empty / remove fails | — | no installAll; remove-failed note as before |

</frozen-after-approval>

## Code Map

- `src/games/GamePackageInstaller.h:54-64` -- `Report`: add `uint8_t waiting` (every judged failure that is `TooManyGames`, the first included; stops at 255); fix `failed`'s comment (the launcher line).
- `src/games/GamePackageInstaller.cpp:945-950` -- the `++report.failed` site in `installAll`: add the `waiting` increment beside it. Nothing else in this file.
- `src/activities/games/GamesLauncherActivity.cpp:157-176` `installInbox` -- split into the install (popup when `hasInbox()`, `installAll`) and `showInstallNote(const Report&)` (resets `noteVisible`, `noteMore`, the new `noteWaiting`; formats the reason; the kind lines). History: 80b68a92 (created), 90ced5df (more-line), dac5e94c (comment).
- same file `:283-316` `confirmRemove` -- first `RenderLock` scope: "Removing" popup, `remove`, `removeIndex = -1`, `lastOpened`, decide `install = ok && hasInbox()`, draw "Installing" under the lock; release; `installAll()`; second `RenderLock` scope: `showInstallNote`, reload, selection, remove-failed note, `requestUpdate`. History: 21e3c7dc, 253bff56, 90ced5df.
- same file `:480-515` `buildScreen`'s note branch -- generalize e4-z3's one hand-laid more-line to up to two (`noteWaiting` then `noteMore`), each one line high, drawn bottom-up in the panel's extra room; with none, `screen.popup` exactly as before.
- `src/activities/games/GamesLauncherActivity.h` -- `char noteWaiting[48]` beside `noteMore[48]` (heap, in the activity); declare `showInstallNote`.
- `lib/I18n/translations/english.yaml` -- append `STR_GAMES_INSTALL_MORE_WAITING: "%u more waiting for room"`, `STR_GAMES_INSTALL_AND_MORE_NOT_INSTALLED: "and %u more not installed"`.
- `test/game_script/harness/list_stubs/InstallerScript.h`, `GamePackageInstallerDouble.cpp` -- record `lockWhenInstalling` (`fakelock::held()`, `screen_stubs/RenderLockProbe.h`), as `GamePackageInstallerRemoveDouble.cpp` does.
- `test/game_script/harness/GamesLauncherTest.cpp` (note forms, layout) and `GameRemoveLauncherTest.cpp` (install after remove); a new suite `installer_waiting.cmake` + `InstallerWaitingTest.cpp` over `game_installer_src` and `InstallerSupport.h` for the real counter (GamePackageInstallerTest is e4-r1's).

## Tasks & Acceptance

**Execution:**
- [x] `GamePackageInstaller.{h,cpp}` -- `waiting` field and its increment -- the launcher cannot tell the kinds apart otherwise.
- [x] `english.yaml` -- the two keys -- N = 1 grammar with no is/are.
- [x] `GamesLauncherActivity.{h,cpp}` -- `showInstallNote`, the kind lines, the two-line panel, install after remove -- Q2 and rev-3.
- [x] tests -- each matrix row, the panel with two lines (height +2 lines, positions, centered, no `\n`, the longest reason drawn whole above both lines), install after remove (installed game listed, `installAll` without the lock, popup up, note when it fails), none when the inbox is empty or the remove failed, and the real counter at the limit (3 valid; non-zip + valid; bad + valid + waiting at 63).

**Acceptance Criteria:**
- Given 64 games and 1 valid + 2 invalid packages dropped, when Games opens, then the note reads the first file's "Too many games…" and "2 more waiting for room".
- Given 64 games and a waiting package, when a game is removed, then "Installing" shows, the package installs, and it is listed before the person leaves the screen.
- Each change has a host test that fails with it taken out.

## Implementation Notes

- The implementation subagent built the plan as written: `installInbox` = popup when `hasInbox()` + `showInstallNote(installAll())`; `confirmRemove` in two `RenderLock` scopes with `installAll` between them; the note's up to two more-lines drawn bottom-up in the panel's extra room (`noteWaiting` above `noteMore`, one `text()` call each); `Report::waiting` counted at the one site; new suite `installer_waiting.cmake` / `InstallerWaitingTest.cpp`; `InstallerScript` records `lockWhenInstalling`. It ran 14 mutants against the new tests (13 caught; the clamp mutant survived and got a test in review, below).
- `git log -L` on `confirmRemove` (21e3c7dc, 253bff56, 90ced5df) and `installInbox` (80b68a92, 90ced5df, dac5e94c). Guards kept: the removeIndex range check (a listing that changed under an open dialog closes it quietly); id and name copied before the reload replaces `listing`; `lastOpened` cleared only on a successful remove; the more-lines cleared under a remove-failed note (now `noteWaiting` too); the install popup only when `hasInbox()`; no note when `failed == 0`; the reason alone when there is no file name.
- Review patches, applied by the build agent (resuming the implementation subagent runs it in the background here, which ends the build agent's turn before it returns): the counter counts a waiting package only when `failed` counted it; comments on `Report::waiting`, `installInbox`, and the selection after a remove; four tests (below). Three mutants of the patches (the old counter form, the popup unguarded, the clamp removed) each fail one new test.

## Plan Change Log

## Review Triage Log

Pass 1. All four lenses ran as context-free subagents and returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. Verdicts: 0 high, 0 medium, 9 low, 2 false (duplicates across lenses grouped below; every lens's finding has a row).

- false -- blind-hunter 1, edge-case 7, verification-gap other-1, intent-alignment (top): "review.diff lacks the `GamePackageInstaller.cpp` increment". The implementation subagent's mutant run was restoring that file while the diff was written (its `mut/run.py` rewrites and restores each file; the file's mtime is 23:25:28, the diff's 23:25:17). The worktree always had the line after the run; the diff was rewritten at 23:27 and holds it. The worktree matched the reviewed diff again when checked (verification-gap other-2, same cause: noted, no change).
- low, patch -- blind-hunter 2, edge-case 1: after `failed` saturates at 255, `waiting` kept counting, so ≥ 255 failures with some invalid first could hide the "not installed" line. The increment now sits inside `failed`'s own guard; test `AWaitingPackagePastASaturatedFailedCountIsNotCounted` (fails with the old form).
- low, patch -- verification-gap 2, edge-case 6: the `std::min(waiting, others)` clamp was untested. Kept (it guards a report the note does not trust); test `TheWaitingLineNeverCountsMoreThanTheOthers` (fails without the clamp).
- low, patch -- verification-gap 1: nothing asserted that a remove with an empty inbox draws no "Installing". `ARemoveWithAnEmptyInboxInstallsNothing` now reads the theme's calls before the render clears them (fails with the guard dropped).
- low, patch -- blind-hunter 6 (test part), edge-case 5: the d-05 case (64 games, 1 valid + 2 invalid past their manifest) was not run on the real installer. Test `InvalidPackagesPastTheirManifestWaitAtTheLimitLikeAValidOne` (waiting 3, all three stay `.chgame`). The AC holds for invalid packages that fail after the manifest read; one that fails at the zip directory or manifest (non-zip, a bad manifest) fails for its own reason, as `ANonZipFailsForItsOwnReasonAndTheValidOneWaits` shows.
- false -- blind-hunter 6 (defect part), intent-alignment divergence 1: "broken packages at the limit read as waiting". This is the owner's chosen reading (Q2's answer keeps the installer's order, A29); the line is true of them (not yet judged), and d-06's non-zip is now told apart.
- low, patch (comment) -- blind-hunter 4: the test message said "Installing is up while the installer works" but checks only that it was drawn first; reworded.
- low, rejected -- blind-hunter 3, 4, edge-case 3: a render already queued when the first lock scope ends can draw the old listing (still whole memory, and still holding the removed game) over "Installing" during the install. Nothing requests a render between the scopes (routeTouch's requestUpdate comes after `confirmRemove` returns; a queued one needs a request from the previous frame that the render task has not started), so it is rare, and the fix (a render-visible "installing" state or a reload in both scopes) adds state or a second registry read. Recorded in Design Notes.
- low, rejected (comment patched) -- blind-hunter 5, edge-case 2: when the install after a remove adds a game that sorts before the removed row, the kept index selects that row's neighbour. Only on the remove-with-a-waiting-package path; the fix (select by the next game's id) adds a branch and a copy. The comment now says so.
- low, rejected -- edge-case 4: a remove that fails after the `.pkg` went frees a place but installs nothing until the next visit. The intent says "after a successful remove"; the failed remove shows its own note.
- low, rejected -- blind-hunter 7: `hasInbox()` lists `/games` under the lock and `installAll` lists it again. One directory listing up to the first `.chgame`, as `onEnter` pays; moving it out of the lock needs a third scope for the popup.
- low, deferred -- blind-hunter 8: the two new keys also go through `snprintf` into 48-byte buffers, so a long translation could be cut mid UTF-8; e4-z3's deferred entry names only `STR_GAMES_INSTALL_AND_MORE`. Appended to `deferred-work.md` `## e4-r2`.
- low, patch -- blind-hunter 9: `installInbox`'s header comment updated; the `waiting` comment rewritten with the counter fix. Rejected: the `"damaged"` anchor in the new stacking test copies e4-z3's `TheMoreLineIsDrawnUnderTheReasonAndCenteredWithIt`, and a string change fails it loudly.
- intent-alignment divergences 2-4 (the split applies whatever the first failure is; two keys and an installer counter; the install after a remove is `installInbox`'s steps split around the lock): each is the plan's intent as written, not a defect.

## Design Notes

Strings. The owner's "N more are waiting" needs "is" for N = 1, which with "and N more not installed" would take three keys; the brief allows two. "%u more waiting for room" drops the verb and reads right for 1 and for many, and "for room" says why when the first failure is another reason. It has no "and", so it sits between the reason and the closing "and N more not installed". Assumption for the retro's device recheck: the note's extra lines read "N more waiting for room" and "and N more not installed" (e4-z3's "and N more" when none of the others waits).

Counting. `waiting` counts the first failure too, so the installer states one fact; the launcher takes one off when `firstError == TooManyGames`. `waiting <= failed` holds under saturation (both stop at 255); `others = failed - 1 - waitingOthers`, floored at 0.

The lock (rev-3). `confirmRemove` holds `RenderLock` for the remove and the reload because the render task reads the listing, Continue rows, icon cache, and note. `installAll` reads none of those, and `hasInbox`/`installAll` take no `RenderLock` (grep of `src/games`), so the install runs between two lock scopes: the render task is never blocked for an install, and no lock is taken twice. The "Installing" popup is drawn inside the first scope, as "Removing" is. If a render was already queued it may run during the install and redraw the old listing (still valid memory) over the popup; nothing is requested during the install, so normally the popup stays. The note fields and the listing are written only in the second scope.

## Verification

The implementer works only in the git worktree that holds this plan (never `/home/user/crosshatch-player`), runs the host suites there (`cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j8`), each chain wrapped whole in `flock /tmp/claude-0/-home-user-crosshatch-player/4faa8743-945f-5626-850b-328812382a3a/scratchpad/build.lock sh -c '...'`, and leaves firmware builds, the simulator, formatting, and the commit to the build agent.

**Commands:**
- host suites under the lock (full `ctest`), `scripts/*_test.py`, `check_layers.py`, `check_upstream_touches.py` -- pass.
- `pio run -e x4pro`, `pio run -e default`, `sim.sh build x4pro`, `check_flash_budget.py` four steps -- exit 0, figures recorded.

**Results** (this worktree, final code, every build under the shared lock; logs in the scratchpad's `e4-r2/`):
- Host: full `ctest -j8` 1385/1385 passed (`final-ctest.log`). Mutants: the implementer's 14 (each fails at least one new test; the clamp mutant got its test in review) and the review's 3 (the counter's old form fails `AWaitingPackagePastASaturatedFailedCountIsNotCounted`, the popup unguarded fails `ARemoveWithAnEmptyInboxInstallsNothing`, no clamp fails `TheWaitingLineNeverCountsMoreThanTheOthers`).
- `scripts/*_test.py` 11/11 exit 0; `check_layers.py` passed; `check_upstream_touches.py` PASS (`english.yaml` is ledger row 2; the other touched files are fork files).
- `check_flash_budget.py` `build on` (the `pio run -e x4pro` build), `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`: exit 0 each. Games on 5,912,512 B, off 5,679,168 B: +233,344 B flash; static internal RAM +784 B (187,848 on, 187,064 off); 43 game objects, largest mutable static 4 B (`lastOpened`), no static initializer. The epic's base, measured the same way at 40a1409a by the orchestrator, is +232,912 B and +784 B, so this change costs +432 B flash and 0 B static RAM (bars: 7,584 B and 24 B).
- `pio run -e default` (C3) SUCCESS; `sim.sh build x4pro` SUCCESS.
- The builds above were run twice; the first set overlapped the implementation subagent's mutant runs, which rewrite and restore source files, so every figure here is from the second set, after it returned and after `./bin/clang-format-fix` (clang-format 21.1.8 from PyPI, in the scratchpad; the container's is 18). The figures matched the first set.
- Simulator, x4pro (64 games packed from a copy of `fixtures/counter` with new ids, installed over two visits; invalid packages from `gen_hardening_packages.py`), in `_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/story-more-kinds-screenshots/`:
  - `note-64-games-1valid-2invalid.png`: `broken-lua.chgame` (binary Lua), `broken-crc.chgame` (CRC), and `extra-a.chgame` at 64 games: "broken-lua.chgame: Too many games are installed; remove one first" / "2 more waiting for room". All three stay `.chgame` (d-05: the invalid ones are judged past their manifest, so they wait).
  - `note-64-games-3valid.png`: three valid packages: "extra-c.chgame: Too many…" / "2 more waiting for room" (d-03).
  - `remove-at-limit-installs-waiting.png`: Remove on Game 02 at 64: "Extra C" installed and listed at once, Game 02 gone, and the note for the two still waiting: "extra-a.chgame: Too many…" / "1 more waiting for room" (N = 1).
  - `remove-at-limit-list-after.png`: the list after the note is dismissed, Extra C on top. The selection is on Game 01, the removed row's index shifted by the new game (review row, deferred).
  - `note-waiting-and-not-installed.png`: three non-zip files and the two waiting packages at 64: "not-a-game.chgame: Not a game package" / "2 more waiting for room" / "and 2 more not installed"; the non-zip files became `.chgame.bad` (d-06 told apart).
- Assumption for the retro's device recheck: with packages over the 64-game limit, the note's extra lines read "N more waiting for room" and, for any other failures, "and N more not installed" (e4-z3's "and N more" when none of the others waits); and after a Remove the waiting package installs at once, behind an "Installing" popup.

**Manual checks:**
- simulator screenshots: 64 games + 1 valid + 2 invalid; 64 games + 3 valid; remove at the limit, then the waiting package listed.

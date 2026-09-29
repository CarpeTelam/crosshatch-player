---
title: 'Continue in the launcher'
type: 'feature'
ticket: '12'
created: '2026-09-29'
status: done
baseline_revision: 'b698fbe6a3ff3e05d12f0183d9683c88ff70a358'
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

**Problem:** A solo match that was left or slept has a `resume.bin` (entry 11), but nothing in the launcher offers it (R10, R8): the person must open the game, and a New match then replaces the save.

**Approach:** `GamesLauncherActivity` lists one "Continue" row for each installed, startable game for which `GameSaveStore::peek(id, pkgHash)` is true, above the game rows and sorted by name (the registry's order). A tap starts `GameMatchActivity(..., Start::Resume)` with no mode step. A save from a changed package makes `peek` false, so it shows no row. The Continue list is built when the listing is built (`onEnter`, and after a remove), never in `buildScreen`.

## Boundaries & Constraints

**Always:** Change only `src/activities/games/GamesLauncherActivity.*`, `lib/I18n/translations/english.yaml` (one new key), and a new harness suite (`continue_launcher.cmake`, `ContinueLauncherTest.cpp`). Continue rows count in the launcher's whole-page padding (no row repeats, blank rows stay disabled), are covered by the stale-tap and note guards, and leaving a match started from Continue returns the launcher to the page holding that game (the 4 B fingerprint of entry 10, no new static). The Continue list lives in the activity's heap (`makeUniqueNoThrow`); add no mutable static. Selection and buttons walk games and Continue rows only, never padding.

**Never:** Edit `GameSaveStore.*`, `GameMatchActivity.*`, the installer, `ActivityManager.*`, or entry 1's and entry 4's shared harness files; call `peek` from `buildScreen`/`provideRow`; move the SDK pointer.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Saves | games B, A, C; valid saves for C and A | rows: Continue A, Continue C, then games A, B, C | none |
| Changed package | save's hash differs from `.pkg` | no Continue row; the file stays | `peek` logs why |
| Tap Continue | Continue row | `GameMatchActivity` with `Start::Resume` replaces the launcher; "Resuming at ver N"; no setup | falls back to New if the save became unreadable (match logs it) |
| Game not startable | save exists, `check` not Ok | no Continue row | none |
| No saves / no games | none | as before | none |
| Paging | 5 Continue + 7 games | 12 rows in whole pages, no repeat, padding disabled | none |
| Return | leave a match started from Continue | launcher selects the game's row and shows its page | game gone: top |
| Long-press | Continue row | nothing (Assumption for entry 14) | none |
| Remove a game | game with a save | its Continue row goes; other games' rows stay with no new `peek` | as entry 10 |

</frozen-after-approval>

## Code Map

- `src/activities/games/GamesLauncherActivity.h/.cpp` -- rows are `[Continue rows][game rows][padding]`. `listCount()`/`paddedCount()`, `navigateButtons`, `provideRow`, `activateIndex`, `openRemoveDialog`, `confirmRemove`, `selectRemembered` walk `listing.count` today and become row-aware. `loadGames()` is followed by a new `loadContinue()`.
- `src/games/GameSaveStore.h` -- read only: `peek(id, pkgHash)` is static and true only for a solo `resume.bin` the package accepts (1 to 2 `Storage.exists` calls when there is no file; one 1,400 B transient buffer and one file read when there is).
- `src/activities/games/GameMatchActivity.h` -- read only: `Start::Resume`.
- `lib/I18n/translations/english.yaml` -- `STR_GAMES_CONTINUE: "Continue"` after `STR_GAMES_REMOVING`.
- `test/game_script/harness/games_launcher.cmake`, `remove_game.cmake`, `GameRemoveLauncherTest.cpp`, `list_stubs/`, `ResumeMatchTest.cpp` -- patterns to copy for a suite of its own (`game_launcher_src` library, scripted host caps, save placement).

## Tasks & Acceptance

**Execution:**
- [ ] `lib/I18n/translations/english.yaml` -- add `STR_GAMES_CONTINUE` -- the row's second line.
- [ ] `src/activities/games/GamesLauncherActivity.h/.cpp` -- `loadContinue()` (skips non-startable games), row mapping helpers, Continue start in `activateIndex`, row-aware guards -- see Design Notes.
- [ ] `test/game_script/harness/continue_launcher.cmake` + `ContinueLauncherTest.cpp` -- own suite (I/O matrix rows), registered by the harness CMake's glob if that is how suites are found.
- [ ] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 4.12` with "Resolved by entry 12:" for the `## 4.11` item.

**Acceptance Criteria:**
- Given saves, when the launcher opens, then Continue rows come first, by name, one per valid save.
- Given a Continue tap, when the match enters, then it resumes from the save (no setup) and no mode picker opens.
- Given a reinstalled changed package, when the launcher opens, then no Continue row shows for it.

## Implementation Notes

Implemented directly in the build agent's session (the brief says the builder implements; no implementation subagent). Files: `GamesLauncherActivity.h/.cpp`, `english.yaml` (`STR_GAMES_CONTINUE`), `continue_launcher.cmake`, `ContinueLauncherTest.cpp` (27 tests after review pass 2; 24 in the first commit). `GameSaveStore.h` gained a launcher include (`games/GameSaveStore.h`), an edge `scripts/check_layers.py` already allows for screens (passes; the spine's layer table needs no change). The harness needed no edit to `games_launcher.cmake`: `game_match_src` already links `GameSaveStore.o`.

## Plan Change Log

- Review pass 1 (findings 1 and 3): the first tree carried the Continue answers of the old listing over a remove, and the frozen I/O matrix's "Remove a game" row says "no new `peek`". The carry-over was dropped in review (two listings alive at once on the C3; answers that could outlive a failed `peek`), so the built behaviour differs from that frozen row: the list is rebuilt, with a `peek` per startable game, on entry and after every remove. The frozen text is left as approved (pass 2, ADV3, restored the row the builder had edited); the difference is the Assumption for entry 14 in Design Notes. The Code Map/Task lines that named the carry-over were edited. KEEP: `loadContinue()` runs when the listing is built, never in `buildScreen`; the Continue rows are counted in the padded paging.

## Review Triage Log

Pass 1. The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as context-free subagents, in the foreground, over the first tree's diff, and all four returned. Verdict counts: 0 high, 5 medium, 5 low, 3 false, 0 maybe-false; the intent-alignment lens is descriptive and filed no findings (its divergences are rows below). No loopback: every accepted finding was a patch.

| # | Lens | Finding | Verdict, route | Evidence and action |
|---|------|---------|----------------|---------------------|
| 1 | blind, edge | `confirmRemove` keeps the old listing beside the new one (two `Entry[]` alive on the C3); carry-over `previousContinue` re-uses an answer from a failed OOM or transient `peek` (edge x3, verification x1) | medium, patch | Real: up to 64 x ~200 B twice, and a `continueOf` that was null after an OOM would drop every row after a remove. Fixed by deleting the carry-over: `loadContinue()` peeks every startable game each time, so the remove rebuild costs what entry costs (the plan's cost note is updated). |
| 2 | blind | Stale comment in `GameSaveStore::peek` ("once per row it draws") | low, defer | Real; the file is `stays_out`. Deferred (`## 4.12`). |
| 3 | blind, verification | Carry-over branches (`return 0`, hash mismatch, unlisted id) untested | medium, patch | Moot after #1; a remove with a mix of saved and unsaved games is now a test (`RemovingAGameWithNoSaveKeepsTheOtherGamesContinueRowsAndTheSelectionOnTheNextGame`). |
| 4 | blind | Cost of `onEnter` unmeasured on a device | low, defer | Real but device-only (AI-3); the suite pins the card calls. Deferred. |
| 5 | blind, verification | Failure paths untested: `loadContinue` OOM, failed remove with Continue rows, removing the last game, removing a game with no row | medium, patch | Added `WhenTheContinueListCannotBeAllocated...`, `ARemoveThatFailsKeepsTheContinueRows...`, `RemovingTheOnlyGame...`, and the mixed remove test. |
| 6 | blind, verification | `rows()` helper's 60 px bound is fragile | low, patch | Direct fix: a Continue line belongs to the nearest name above it. |
| 7 | blind | `peek` does not check the mode byte, so a pass or nearby save would resume as solo | false | `peek` is true only for a mode-0, one-seat save (its header comment and `readResume`); a non-solo save gives no row. Noted for epic-pass-and-play (deferred). |
| 8 | blind | The tap-under-confirmation test never taps after the dialog is drawn | low, patch | The first tap was after the render, the second before it; the test now says which, and does both. |
| 9 | blind | `gameOfRow` reads `continueOf` unchecked when it is null | false | `row < continueCount` and `continueCount` is 0 whenever `continueOf` is null (`loadContinue` resets both first); the header comment now says so. |
| 10 | blind | Return selects the game row, not the Continue row; header comment silent | low, patch | Deliberate (owner's "page holding that game"); header comment now says "that game's own row (also after a match started from its Continue row)". |
| 11 | blind | A shared "Continue" i18n key; the ledger | false | `STR_CONTINUE_READING` / `STR_EOB_CONTINUE_WITH` are other texts; `english.yaml` is ledger row 2 (`STR_GAMES_*`), and `check_upstream_touches.py` passes. |
| 12 | blind | `openRemoveDialog(int index)` / `activateIndex(int index)` naming vs rows | low, patch | Parameters renamed `row` in the header and the `.cpp`. |
| 13 | verification | No test that a Continue row draws its own game's icon | medium, patch | Added `AContinueRowDrawsItsOwnGamesIcon` (icon.bmp, library icon, fallback). |
| 14 | verification | The end of the long-press test asserts `drewLine("Game 01")`, which the list also draws | false | The list is not built under the confirmation (`buildScreen` returns after `buildRemoveDialog`; `GameRemoveLauncherTest` asserts it), so only the dialog draws it. |
| 15 | edge | A long-press on a Continue row returns before `clearTapFlash()` / `requestUpdate()` | medium, patch | A finger down arms the flash and the long-press ends without a repaint. `openRemoveDialog` now clears the flash and requests an update before returning for a Continue row. |
| 16 | edge, intent | A tap on a Continue row whose save went bad starts a New match and the row stays | low, defer | Only a card swap changes a save while the launcher is open; the match's fallback is entry 11's. Deferred. |
| 17 | edge | `deferred-work.md` still lists the `## 4.11` item | low, patch | Resolved through the `## 4.12` "Resolved by entry 12:" entry the brief asks for; `## 4.11` left as it is. |
| 18 | intent | Readings: separate rows (implemented), a folded affordance, one aggregated row; startable = `check.ok()`; no long-press remove; sim not run | low, no action | Descriptive. The simulator screenshots below cover the on-device surface. |

Pass 2 (after the patches): the host suite and the builds below ran on the patched tree.

Independent review of `253bff56` (the orchestrator's adversarial, edge-case, and verification-gap reviewers; findings in the orchestrator's `review-all.md`). Their lenses ran outside this session; triaged here:

| # | Finding | Verdict, route | Evidence and action |
|---|---------|----------------|---------------------|
| ADV1 | After Continue then Leave, `selectRemembered()` preselected the game's own row, so one Confirm started a New match and replaced `resume.bin` (taps=4 ver=7 became taps=0 ver=1) | high, patch | True: the own row of a game with a save starts New. `selectRemembered()` now selects the remembered game's Continue row when it has one, else its own row, and shows the whole page holding it. Tests: `LeavingAMatchStartedFromContinueReturnsToItsContinueRowOnThePageHoldingIt` (replaces the test that pinned the old behaviour), `OneConfirmAfterLeavingAContinueMatchResumesAgainAndLeavesTheSaveAlone` (the reviewer's), `AfterLeavingAGameWithNoSaveItsOwnRowIsSelectedOnItsPage`. A mutant that skips the Continue-row search fails the first two. Screenshot 6 re-shot. |
| ADV2 | A transient `peek` failure hides the row and a game-row tap then replaces a valid save | medium, defer | Real, but the fix needs `peek` to answer valid, none, or unknown (`GameSaveStore.*` is `stays_out`). Deferred with a trigger (`## 4.12`). |
| ADV3 | A row of the frozen I/O matrix was edited | low, patch | The frozen row is restored as approved; the Plan Change Log records the difference and the Design Notes carry an Assumption for entry 14 (the list is rebuilt, with its peeks, on entry and after every remove). |
| ADV4 | `peek` logs a save of another package at ERR and re-reads it on every build | low, defer | `GameSaveStore.cpp` is `stays_out`. Deferred with a trigger. |
| ADV5 | Stale "once per row it draws" comment | low, defer | Already deferred (pass 1, row 2); the item now carries the review id and a trigger. |
| ADV6 | A save `peek` accepts can be refused by `Session::restore`; the match then starts New over it | low, defer | `GameVM.cpp` and `GameMatchActivity.*` are `stays_out`. Deferred with a trigger. |
| VG1 | The note-guard test never reaches `onRowAction` (its tap is consumed dismissing the note) | medium, patch | Added `ALongPressOnAContinueRowUnderTheNoteMovesNoSelectionAndOpensNothing`; removing `|| noteVisible` from `onRowAction` fails it. |
| VG2 | `5-board-restored.png` is byte-identical to `1-counter-played.png`, and `counter`'s setup reads `taps` from `ch.store`, so a New match would look the same | medium, patch | Re-shot with a scratch fixture (`tally`, packed under the scratchpad, not committed) whose count lives only in the snapshot: a New match shows Taps: 0 (screenshot 7), the resumed one Taps: 3 (5). The identity of 1 and 5 is now the evidence. The old `counter` screenshots are removed. Log excerpt in Verification. |
| VG3 | Implementation Notes say 19 tests; the plan does not name the measured commit | low, patch | Count corrected (27); the measured tree is named in Verification. |


## Design Notes

- Guards kept (from `git log -L` on the changed functions, all in this epic's history: 80b68a92, 60b59a4b, 21e3c7dc, a941ee1d). `activateIndex`: the bounds check (now `rowCount()`), the not-startable return with `requestUpdate()` (a tap moved the selection there), `app.clearTapFlash()` before leaving, `lastOpened` set before the picker or match, and both OOM null-checks with `requestUpdate()` (the tap flash was cleared; repaint rather than leave a stale frame). `openRemoveDialog`: no padding row, no empty list, no note over the list (a stale row action must not ask), and now no Continue row. `confirmRemove`: the re-check of `removeIndex` against the listing, the `RenderLock` around the reload, `lastOpened` cleared only on success, the reload after a failure too (the registry is the truth), and the note on failure. `paddedCount`: no padding when `page <= 1` or the list fits one page. `navigateButtons`: the selection walks real rows only. `onRowAction` (unchanged): drops row actions under the dialog or note, which covers Continue rows.
- Cost of `peek`, measured by op counts on the fake card (`ACardWithTwentyFiveGamesCosts...`, `RemovingAGameDrops...`): on entry, one `peek` per startable game; a game with no save costs the `exists` checks for `resume.bin` and `resume.bin.tmp` (no read), a game with a save one open and read of the whole file into a 1,400 B transient buffer. So 25 games with 3 saves is 3 whole-file reads (opens of `resume.bin`) and 22 existence checks. After a remove the list is built the same way: one `peek` per startable game again (25 games with 3 saves: 3 whole-file reads and 44 existence checks), under the `RenderLock` `confirmRemove` already holds for its own rescan of every manifest and `.pkg`. A carry-over from the old listing was tried and dropped in review (two listings alive at once on the C3, and answers that could outlive a failed `peek`). `buildScreen` and `provideRow` never call `peek` (a swipe and a re-render add no `open`).
- `selectRemembered()` (pass 2, ADV1) selects the remembered game's Continue row when it has one, else its own row below the Continue rows, and shows the whole page holding it. The first version always chose the own row, where one Confirm after leaving a Continue match started a New match over the save. The `constinit` fingerprint is unchanged: no static added.
- Assumption for entry 14: a long-press (or a Confirm hold) on a Continue row does nothing; removing a game is on the game's own row, so a person is never asked to remove a game from the row that resumes it (2026-09-29).
- Assumption for entry 14: a Continue row shows the game's name with "Continue" as the second line, in place of an unavailable reason (a game that cannot start has no Continue row); it uses the game's icon (2026-09-29).
- Assumption for entry 14: no Continue row for a game whose `check` is not Ok; its save is not read (2026-09-29).
- Assumption for entry 14: after leaving a match of a game that still has a save, the launcher selects that game's Continue row, on the page holding it (2026-09-29).
- Assumption for entry 14: a tap on a game's own row while it has a save starts a New match, which replaces the save (one save slot per game, entry 11); there is no confirmation (2026-09-29).
- Assumption for entry 14: the Continue list is rebuilt, with its `peek`s, on entry and after every remove; it is not carried over from the previous listing (2026-09-29).

- Row `r < C` is Continue for game `continueOf[r]`; `C <= r < C + N` is game `r - C`; the rest is padding. `removeIndex` stays a game (listing) index.
- Continue row: label = the game's name, subtitle = "Continue", the game's icon. No per-row buffer.
- A long-press or Confirm hold on a Continue row does nothing; removal is on the game row.
- `selectRemembered()` selects the game row (the fingerprint names a game, not a row), so the page shown holds that game.

## Verification

**Commands** (each build under the shared lock). Figures marked "first commit" were taken on the source tree of `253bff56`; those marked "fix commit" on the source tree of the commit that adds this text (its parent is `253bff56`; only the plan, `deferred-work.md`, and the screenshots changed after the builds).
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- fix commit: passed, 1,274 of 1,274 tests, 27 of them `ContinueLauncherTest` (first commit: 1,271 of 1,271 with 24). They cover: Continue listed first and by name; none after a changed package, for an invalid save, or for a game that cannot start; a tap and a Confirm resuming with no mode step and no setup; the game's own row still starting a New match; paging with no repeated row; long-press, also under the install note, doing nothing; remove with mixed saved and unsaved games, a failed remove, and the last game; OOM of the Continue list; each Continue row's icon; the return to the Continue row on the page holding it, and one Confirm after it resuming with the save unchanged; the card-call counts. Mutants checked: no note guard in `onRowAction`, and no Continue-row search in `selectRemembered()`, each fail their tests.
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py` -- passed (`english.yaml` is ledger row 2).
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- all exit 0. Fix commit, x4pro `firmware.bin`: 5,908,368 B games on, 5,678,576 B off, **+229,792 B** (26,208 B under the 256,000 B gate). Static internal RAM (`.dram0.data` + `.dram0.bss` + `.noinit` + `.iram0.*`, `size -A`): 187,848 B on, 187,064 B off, **+784 B** (240 B under the 1,024 B gate). Objects: 43 game objects, no static initializer, largest mutable static 4 B (`lastOpened`, entry 10's). First commit, measured the same way: +229,760 B and +784 B.
- Against the base measured at `962ae61` (+228,496 B flash, +776 B static RAM): **+1,296 B flash and +8 B static RAM**, so 10,704 B and 24 B under this epic's pass bar (+240,496 B and +808 B).
- Builds, fix commit: `pio run -e x4pro` (through `build on`), `-e default`, and `sim.sh build x4pro` (`simulator_x4pro`) pass. First commit: all five envs (`default`, `sticky`, `x4c`, `papermono`, `x4pro`); the fix changes `GamesLauncherActivity.cpp` by a loop only, and the orchestrator builds all five before the PR.
- `./bin/clang-format-fix` twice; `git status` shows nothing new the second time.

**Simulator** (fix commit's `sim.sh build x4pro`; a scratch fixture `tally`, packed with `scripts/pack_game.py` under the scratchpad and dropped into `fs_/games/`; nothing under `games/`, nothing committed). `tally`'s count lives only in the snapshot, so a New match shows Taps: 0. Screenshots in `_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/story-continue-screenshots/`:
- `1-tally-played.png` -- three taps: "Taps: 3".
- `2-sleep-screen.png` -- `sim.sh key sleep` during the match (the log shows the forced exit).
- `3-wake-on-home.png` -- a key press wakes the simulator, which re-executes the firmware: it lands on Home, not on the game.
- `4-games-continue-first.png` -- Home, Games: "Tally / Continue" first (selected), the game's own row below.
- `5-board-restored-taps-3.png` -- a tap on the Continue row: "Taps: 3", byte-identical to screenshot 1; the log shows `Resuming at ver 4` then `Round started at ver 4`, with no `setup` (a New match is screenshot 7).
- `6-after-leave-continue-selected.png` -- Back, then Leave: the launcher returns with the Continue row selected (a Confirm resumes again). The test `OneConfirmAfterLeavingAContinueMatch...` pins the difference this shot cannot show, since one game's Continue row is also the first row.
- `7-new-match-from-game-row-taps-0.png` -- a tap on the game's own row: a New match, "Taps: 0" (it replaces the save with its first snapshot).
- `8-changed-package-no-continue.png` -- a changed `tally` (version 1.1.0, a `main.lua` edit) reinstalled on entering Games: one row, no Continue row (log: `tally: discarded /.games-data/tally/resume.bin: other package`, `0 Continue rows of 1 games`).

Log excerpt of the resume (screenshot 5): `[28550] [INF] [GAME] Resuming at ver 4` / `[28550] [INF] [GAME] Round started at ver 4`.

**Not measured:** the time `loadContinue` adds to the launcher's entry on a device; the fake card has no latency (deferred, AI-3).

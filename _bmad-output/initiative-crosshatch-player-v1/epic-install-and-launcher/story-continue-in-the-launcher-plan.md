---
title: 'Continue in the launcher'
type: 'feature'
ticket: '12'
created: '2026-09-29'
status: built
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
| Remove a game | game with a save | its Continue row goes; other games' rows stay (the list is rebuilt, `peek` per startable game again) | as entry 10 |

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

Implemented directly in the build agent's session (the brief says the builder implements; no implementation subagent). Files: `GamesLauncherActivity.h/.cpp`, `english.yaml` (`STR_GAMES_CONTINUE`), `continue_launcher.cmake`, `ContinueLauncherTest.cpp` (19 tests). `GameSaveStore.h` gained a launcher include (`games/GameSaveStore.h`), an edge `scripts/check_layers.py` already allows for screens (passes; the spine's layer table needs no change). The harness needed no edit to `games_launcher.cmake`: `game_match_src` already links `GameSaveStore.o`.

## Plan Change Log

- Review pass 1 (findings 1 and 3): the first tree carried the Continue answers of the old listing over a remove, and the I/O matrix's "Remove a game" row said "no new `peek`". That was dropped in review (two listings alive at once on the C3; answers that could outlive a failed `peek`), so the row now says the list is rebuilt with a `peek` per startable game. The builder edited this one matrix row and the Code Map/Task lines that named the carry-over; the Intent and Boundaries are as approved. KEEP: `loadContinue()` runs when the listing is built, never in `buildScreen`; the Continue rows are counted in the padded paging.

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

Pass 2 (after the patches): the host suite (all 1,271 tests) and the builds below ran on the patched tree.

## Design Notes

- Guards kept (from `git log -L` on the changed functions, all in this epic's history: 80b68a92, 60b59a4b, 21e3c7dc, a941ee1d). `activateIndex`: the bounds check (now `rowCount()`), the not-startable return with `requestUpdate()` (a tap moved the selection there), `app.clearTapFlash()` before leaving, `lastOpened` set before the picker or match, and both OOM null-checks with `requestUpdate()` (the tap flash was cleared; repaint rather than leave a stale frame). `openRemoveDialog`: no padding row, no empty list, no note over the list (a stale row action must not ask), and now no Continue row. `confirmRemove`: the re-check of `removeIndex` against the listing, the `RenderLock` around the reload, `lastOpened` cleared only on success, the reload after a failure too (the registry is the truth), and the note on failure. `paddedCount`: no padding when `page <= 1` or the list fits one page. `navigateButtons`: the selection walks real rows only. `onRowAction` (unchanged): drops row actions under the dialog or note, which covers Continue rows.
- Cost of `peek`, measured by op counts on the fake card (`ACardWithTwentyFiveGamesCosts...`, `RemovingAGameDrops...`): on entry, one `peek` per startable game; a game with no save costs the `exists` checks for `resume.bin` and `resume.bin.tmp` (no read), a game with a save one open and read of the whole file into a 1,400 B transient buffer. So 25 games with 3 saves is 3 whole-file reads (opens of `resume.bin`) and 22 existence checks. After a remove the list is built the same way: one `peek` per startable game again (25 games with 3 saves: 3 whole-file reads and 44 existence checks), under the `RenderLock` `confirmRemove` already holds for its own rescan of every manifest and `.pkg`. A carry-over from the old listing was tried and dropped in review (two listings alive at once on the C3, and answers that could outlive a failed `peek`). `buildScreen` and `provideRow` never call `peek` (a swipe and a re-render add no `open`).
- `selectRemembered()` selects the game's own row (the fingerprint names a game, not a row), so leaving a match started from Continue lands on the page holding that game. The `constinit` fingerprint is unchanged: no static added.
- Assumption for entry 14: a long-press (or a Confirm hold) on a Continue row does nothing; removing a game is on the game's own row, so a person is never asked to remove a game from the row that resumes it (2026-09-29).
- Assumption for entry 14: a Continue row shows the game's name with "Continue" as the second line, in place of an unavailable reason (a game that cannot start has no Continue row); it uses the game's icon (2026-09-29).
- Assumption for entry 14: no Continue row for a game whose `check` is not Ok; its save is not read (2026-09-29).

- Row `r < C` is Continue for game `continueOf[r]`; `C <= r < C + N` is game `r - C`; the rest is padding. `removeIndex` stays a game (listing) index.
- Continue row: label = the game's name, subtitle = "Continue", the game's icon. No per-row buffer.
- A long-press or Confirm hold on a Continue row does nothing; removal is on the game row.
- `selectRemembered()` selects the game row (the fingerprint names a game, not a row), so the page shown holds that game.

## Verification

**Commands** (each build under the shared lock; the tree was `b698fbe6` plus this story's working tree, committed unchanged in its code as the story's commit):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- passed, 1,271 of 1,271 tests, including the 24 of `ContinueLauncherTest` (Continue listed first and by name; none after a changed package, for an invalid save, or for a game that cannot start; a tap and a Confirm resuming with no mode step and no setup; the game's own row still starting a New match; paging with no repeated row; long-press doing nothing; remove with mixed saved and unsaved games, a failed remove, and the last game; OOM of the Continue list; each Continue row's icon; the return to the game's page; the card-call counts).
- `python3 scripts/check_layers.py` -- passed (466 include edges; the launcher's `games/GameSaveStore.h` include is a screen-to-adapter edge the layer table already allows). `python3 scripts/check_upstream_touches.py` -- PASS (`english.yaml` is ledger row 2).
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- all exit 0. x4pro `firmware.bin`: 5,908,336 B games on, 5,678,576 B off, **+229,760 B** (26,240 B under the 256,000 B gate). Static internal RAM (`.dram0.data` + `.dram0.bss` + `.noinit` + `.iram0.*`, from `size -A`): 187,848 B on, 187,064 B off, **+784 B** (`.dram0.bss` +16, `.iram0.text` +684, `.iram0.text_end` +84; 240 B under the 1,024 B gate). Objects: 43 game objects, no static initializer, largest mutable static 4 B (`lastOpened`, entry 10's; this story adds none). Method: both builds of the working tree above, games on minus games off, the same commands the base used.
- Against the base measured at `962ae61` (+228,496 B flash, +776 B static RAM): **+1,264 B flash and +8 B static RAM**, so 10,736 B and 24 B under this epic's pass bar (12,000 B and 32 B over the base: +240,496 B and +808 B). Against entry 10's final tree `a941ee1d` (+229,040 B, +784 B, measured the same way): +720 B flash, +0 B static RAM.
- `pio run -e default`, `-e sticky`, `-e x4c`, `-e papermono` (and x4pro through `build on`) -- all five envs built (rc 0).
- `./bin/clang-format-fix` twice; `git status` shows nothing new the second time.

**Simulator** (`sim.sh setup`, `build x4pro`, `start x4pro`; `counter` packed with `scripts/pack_game.py` into `fs_/games/`; nothing under `games/`). Screenshots in `_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/story-continue-screenshots/`:
- `1-counter-played.png` -- `counter` after three taps: "Taps: 3".
- `2-sleep-screen.png` -- `sim.sh key sleep` during the match: the sleep screen (the log shows the forced exit, `Playing -> Leaving on ForcedExit`).
- `3-wake-on-home.png` -- a key press wakes the simulator, which re-executes the firmware: it lands on Home ("No open book"), not on the game.
- `4-games-continue-first.png` -- Home, Games: "Counter / Continue" is the first row (selected), the game's own row "Counter" below it.
- `5-board-restored.png` -- a tap on the Continue row: "Taps: 3" is back (log: "Resuming at ver 4", no setup).
- `6-after-leave-on-games-row.png` -- Leave from the pause menu: the launcher returns with the game's own row selected, the Continue row still above (the save is kept).
- `7-changed-package-no-continue.png` -- a changed `counter` (version 1.1.0, a `main.lua` edit) dropped in `fs_/games/` and reinstalled on entering Games: one row, no Continue row (log: "counter: discarded /.games-data/counter/resume.bin: other package", "0 Continue rows of 1 games"); the old `resume.bin` stays on the card.

**Not measured:** the time `loadContinue` adds to the launcher's entry on a device; the fake card has no latency (deferred, AI-3).

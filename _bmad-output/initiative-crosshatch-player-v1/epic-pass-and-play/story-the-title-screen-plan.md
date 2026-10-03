---
title: 'The title screen'
type: 'feature'
ticket: '7'
created: '2026-10-01'
status: done
route: 'full'
route_source: 'auto'
baseline_revision: 'fb315fd8ffd410079554834619552f06a10de431'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/docs/contributing/touch-and-ui.md'
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.skills/heap-discipline/SKILL.md'
  - '{project-root}/.skills/scope-discipline/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A game's launcher row starts a match at once (one mode) or opens a bare mode picker, and a New match from the game's row silently replaces its save (A13); a pass save is never offered, since only the launcher's solo-only `peek` looks (R9, R16, `## 5.5`).

**Approach:** `GameModeActivity`, under its name, becomes the game's title screen: headed by the game's name, it offers Continue when the new `peek(game, hash, host)` finds a save (Valid or Unreadable), one New row per mode `check` leaves, and a Cancel / New game confirmation before a New over a save; it starts the match with the mode and `passSeats`' only `n`, or the resume. Every launcher game row opens it; the launcher's Continue rows stay until entry 8.

## Boundaries & Constraints

**Always:** `NAME` stays `"GameMode"` (ledger row 5); rows in fixed member arrays (no allocation per render); the dialog's props a member (launcher pattern, too big for the render stack); one `peek` per `onEnter`, none while drawing; New and Continue each `makeUniqueNoThrow` the match and repaint on OOM; strings appended to `english.yaml` only; the seat-count guard (`passSeats == 0`) kept; Back pops to the launcher.

**Never:** change `GameMatchActivity`, `GameSaveStore`, `ActivityManager`, or any upstream file but `english.yaml`; the launcher beyond `activateIndex`; seat-choice rows; a game icon on the title screen (entry 3 decides).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| No save | solo game, no resume.bin | header = name; rows Solo; tap starts New | — |
| Solo save | Valid solo save | Continue first (selected); tap/Confirm resumes the saved snapshot | — |
| Pass save | solo+pass game, Valid pass save (mode 1, n 2) | Continue offered; tap starts `Start::Resume` with `Roster::pass(2)`; file untouched through play, Over, and exit | until entry 9 the match starts a new pass match, which reads, writes, and deletes no resume.bin (no package hash) |
| Unreadable | open or read fault | Continue offered; a solo-only game's tap ends in the error view, file untouched | a game that starts pass gets a pass roster (never a solo write over a pass save); until entry 9 that starts a new pass match, file untouched |
| New over a save | save present, tap a mode | Cancel / New game dialog, Cancel focused; Cancel or Back keeps all; New game starts New in that mode | — |
| Pass New | pass row, seats 1..2, host 2 | `Roster::pass(2)` match | host `maxSeats` 1: nothing starts, logged, repaint |
| Launcher | any startable game row, tap or Confirm | pushes the title screen; Continue rows still resume directly | unavailable row opens nothing |

**Decisions (orchestrator, 2026-10-01):** Open Question 1 answered (a): entry 7 edits `GamesLauncherTest.cpp`, `GameRemoveLauncherTest.cpp`, and `ContinueLauncherTest.cpp` only as far as its row change forces (a row pushes the title screen; a case that needs a match taps New; which-game checks read the pushed screen), changing nothing else; these are orchestrator-approved out-of-touches edits, and entry 8 still owns their rewrite. New over a save is a second confirm; Continue on a pass save starts a resume with the pass roster; nothing in the interim tree may delete or overwrite a pass save, and the guard relied on is tested.

</frozen-after-approval>

## Code Map

- `src/activities/games/GameModeActivity.h` -- constructor takes `const GameRegistry::Entry&` (copies manifest, `check.modes`, `pkgHash`); drop `modeCount`/`needed` (only the launcher used `needed`); rows `ListItem rowItems[MAX_ROWS]`, `rowKind[MAX_ROWS]` (`MAX_ROWS = 1 + MAX_MODES`, a Continue kind beside the three modes); `SaveState save`; dialog state (`int8_t confirmRow`, `uint8_t confirmFocus`, `OptionDialogProps dialogProps`); `onEnter`, `handleCustomInput`, `onRowAction` overrides. Rewrite the class comment (`## 5.1`).
- `src/activities/games/GameModeActivity.cpp` -- `onEnter`: base, register `ACTION_USER` choice handler, `peek(manifest, pkgHash, gameHostCaps())`, build rows. `headerTitle` = `manifest.name`. `activateIndex`: Continue → `startResume()`; a mode with a save → open the dialog; else `startNew(kind)`. `startNew` keeps today's switch and logs (solo; pass via `passSeats`, guard; nearby plays solo). `startResume`: roster = solo for a game that cannot start pass; `pass(passSeats)` for one that cannot start solo; for one that starts both, solo only when the solo-only `peek(id, hash)` says Valid, else pass (a None or Unreadable there may be a pass save, which a solo match would replace); log `Continue <id>: a <solo|n-seat pass> match`; `Start::Resume`. Dialog build/input copied from `GamesLauncherActivity::buildRemoveDialog`/`handleRemoveInput`/`onRemoveChoice` (Cancel 0 focused, New game 1); `buildScreen` draws the dialog instead of the list while open; `onRowAction` ignores row taps while it is open.
- `src/activities/games/GamesLauncherActivity.cpp` `activateIndex` -- a non-Continue row of a startable game pushes `GameModeActivity(renderer, mappedInput, game)`; Continue rows as today (`Roster::solo()`, `Start::Resume`); remove the pass-only direct start (moved). Keep the unavailable guard and `lastOpened`.
- `lib/I18n/translations/english.yaml` -- append `STR_GAMES_CONTINUE_DESC` "Go on with the saved game", `STR_GAMES_NEW_OVER_SAVE_TITLE` "Start a new game?", `STR_GAMES_NEW_OVER_SAVE` "This replaces the saved game.", `STR_GAMES_NEW_GAME` "New game".
- `test/game_script/harness/list_stubs/HostCapsScript.h`, `GameHostCapsDouble.cpp` -- scriptable `maxSeats` (default `HostCapsValues::MAX_SEATS`; a one-seat host is not a device host, said so).
- `test/game_script/harness/ModePickerTest.cpp` -- fixture renamed `TitleScreenTest`; save helper (`resumeBytes` with mode and n, from `ContinueLauncherTest`) and the counting game; cases per matrix row, the carried Unreadable guard, New over a save (cancel, Back, confirm), the 2-tap launcher → match count for solo and pass, `APassThatCannotFitStartsNothing` on `maxSeats = 1` (`## 5.2`), `installCounter` default `seatsMax = 2` (`## 5.2`), `HostCapsDouble` pins `maxSeats`' default; drop `ModeCount` cases.
- `test/game_script/harness/mode_picker.cmake` -- comment only, if it names the picker.
- `test/game_script/harness/GamesLauncherTest.cpp`, `GameRemoveLauncherTest.cpp`, `ContinueLauncherTest.cpp` -- orchestrator-approved out-of-touches edits (Decisions): only the 29 cases the row change fails (11, 11, 7), each through a fixture helper that reads the pushed `GameModeActivity` (its game) or taps its New row; nothing else in them changes.

## Tasks & Acceptance

**Execution:**
- [x] `english.yaml` -- the four keys
- [x] `GameModeActivity.{h,cpp}` -- the title screen above
- [x] `GamesLauncherActivity.cpp` -- row activation
- [x] `list_stubs/*` -- `maxSeats`
- [x] `ModePickerTest.cpp` -- the suite above, plus the pass-save guard: Continue on a pass save played to Over and left, and a two-mode game's Unreadable Continue, each leaving the file's bytes unchanged
- [x] the three launcher suites -- the forced follow-through only
- [x] `deferred-work.md` `## 5.7` -- the launcher's stale `.h` comments (entry 8); `STR_GAMES_MODE_TITLE` unused (sweep); the tap-time solo-only `peek` redundant once entry 9 builds the roster from the save (sweep)

**Acceptance Criteria:**
- Given Home, when a solo game and a pass game are started, then each takes Games, the row, and a start: 3 taps.
- Given every host suite, when run, then all pass.

## Implementation Notes

- Implemented by a context-free implementation subagent from this plan; host suites 1,493 of 1,493, fast checks pass (its report, relayed by the orchestrator).
- `onEnter` peeks outside the lock, then assigns `save` and builds the rows under `RenderLock`: `UiListActivity::onEnter` has already asked for a render, and `ActivityManager` calls `onEnter` after unlocking ("onEnter may acquire its own lock"), so the lock cannot self-deadlock (12cc816 is about destructors).
- `RowKind` is an `enum class` (Solo, Pass, Nearby index `MODE_TEXTS`; Continue last). The dialog's headline is the tapped row's mode name ("Pass and play"), beside the plan's title and message: entry 3 reviews it.
- A pass-only game's Continue checks `passSeats` as New does (`Cannot continue ... in pass`, repaint).
- The three launcher suites (orchestrator-approved out-of-touches edits): each fixture gains `startNewOnTitle()` (enter the pushed title screen, tap its first New row, then New game if it asks, let the screen go as the manager does); only the 29 failing cases call it. `GamesLauncherTest.cpp` gains the `GameModeActivity.h` include, and `AGameOnlyAnotherModeCanStart...`'s `pushed == 0` became the helper call.
- `GamesLauncherActivity.cpp` keeps a now-unused `games/GameHostCaps.h` include (outside "row activation only"); deferred to entry 8 under `## 5.7`.
- The pass-save guard tests were mutation-checked: forcing a solo roster in `startResume` fails three of them.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-10-01). The four lenses ran as context-free subagents, launched together in one message (each told the worktree is read-only), and all four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG), intent-alignment (IA). Verdicts: medium 1, low 15, false 4, maybe-false 0, plus IA's descriptive report. No intent_gap or bad_plan; the patches went back to the step-03 implementer (the same agent, re-engaged), and verification re-ran on the patched tree.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | BH1 | In the three launcher suites, cases meaning "opens nothing" check only `replaced == 0`; a row now pushes, so a regression that opens the title screen passes | medium | patch | Real: forced by the row change, so inside Decision (a). `asks.pushed == 0` added beside each such check, nothing else changed |
| 2 | BH6 | `GameModeActivity.h` included `GameSaveStore.h`; upstream `ActivityManager.cpp` includes the header for `NAME`, so it reached GameScript headers (`check_layers.py` reads direct includes only) | low | patch | Header keeps `bool hasSave` and `GamePkg::HASH_BYTES`; `GameSaveStore.h` only in the .cpp |
| 3 | VG1, BH8 | `startResume`'s `Cannot continue ... in pass` branch never runs | low | patch | Case on a one-seat host: nothing starts, log, repaint, bytes unchanged |
| 4 | VG2, BH8 | The launcher's title-screen OOM branch, now reached by every row, never runs; Continue's match OOM untested | low | patch | Both cases added (`oom::failSize`) |
| 5 | BH2, IA §3.3 | `startNewOnTitle()` taps New game when no match started, so no launcher-path test shows the question over a save | low | patch | A `TitleScreenTest` case through the launcher asserts the question; the helper stays (entry 8 rewrites it) |
| 6 | BH8 | Pass-save Continue cases did not show `Start::Resume` was passed; the peek-once test used `EXPECT_GT`; the stale-tap test did not check the selection | low | patch | Assert the match's resume log ("no usable resume.bin" or "Resuming at ver"); exactly one open; selection checked where the fixture reads it |
| 7 | BH7 | `RowKind` tied to `MODE_TEXTS` by comment only; `startNew` would index past the table for Continue | low | patch | `static_assert`s and an early `LOG_ERR` return |
| 8 | BH9 | The Continue log does not say how the tap-time solo-only peek came out | low | patch | Result added to the `Continue <id>` line |
| 9 | IA §3.2 | Class comment "peeked once" is untrue for a two-mode Continue | low | patch | Comment names the tap-time read |
| 10 | BH12 | `onEnter`'s lock comment gives the wrong reason (the base's request is delivered after `onEnter` returns) | low | patch | Comment: a render notification left from the previous screen can draw this one during `onEnter` |
| 11 | BH3, BH4, IA §3.8 | `startNewOnTitle()` copied in three fixtures; stale names and messages in the launcher suites | low | defer | Their rewrite is entry 8's (Decision (a): forced edits only); `## 5.7` |
| 12 | BH5 | The confirm dialog copies the launcher's remove dialog (~90 lines) | low | defer | A shared two-option confirm is cleanup across files: sweep, `## 5.7` |
| 13 | BH10 | `## 5.4`'s pass-hidden installer item fired (the suite lists pass-only games) | low | defer | `GamePackageInstallerTest` is outside touches; carried to entry 8 in `## 5.7` |
| 14 | BH11 | `formats.md` still names the old callers of the two `peek` forms | low | defer | Outside touches; sweep, `## 5.7` |
| 15 | EC1 | The launcher's unchanged Continue row offers an Unreadable save of a solo+pass game with a solo roster; a pass save there, read at the match, would be refused and replaced | low | defer | Pre-existing path; before entry 9 no match writes a pass save, so nothing is at risk; entry 9 must build every `Start::Resume` roster from the save, the launcher's rows included until entry 8 (`## 5.7`) |
| 16 | EC2 | The question says New replaces the save, but before entry 9 a pass New writes no resume.bin | low | reject | True only in the interim tree; entry 9 (same PR) makes it true, and the owner's entry 3 reviews the wording |
| 17 | EC3, IA §3.1 | Continue's "Go on with the saved game" starts a new pass match for a pass save | false | reject | The approved interim (Decisions): entry 9 builds the roster from the save; the save is untouched meanwhile, pinned by tests |
| 18 | IA §3.4 | The 2-tap test counts its own calls | false | reject | The taps the test makes are the person's taps; `replaced == 1` after them is the product's evidence, and Home's Games row is one tap (HomeTabsTest) |
| 19 | IA §3.5, §3.6, §3.7 | Rows keep mode names; nearby plays solo; the launcher's Continue rows use the solo-only peek | false | reject | Design choices of the plan (layout; R9's seat handling) and entry 8's scope, as the intent says |
| 20 | BH13 | Firmware builds, `pio check`, flash budget, screenshots not run | false | reject | They run after the review, per AGENTS.md's order; results in Verification |

## Design Notes

**Layout (entry 3 reviews it):** a `UiListActivity`, header = the game's name (the screen is that game; "Choose a mode" goes), rows top to bottom: Continue (subtitle "Go on with the saved game") when a save is there, then the modes in solo, pass, nearby order with today's names and descriptions. The selection starts on the first row, so Confirm resumes when there is a save: a stray Confirm never starts New over it. No icon: the row that opened it showed one, and drawing it here costs a 512 B bitmap and `GameRowIcon` code against the 7,040 B left.

**New over a save:** a second confirm, Cancel focused. One save per game, and New replaces it for good; the restaurant test prefers one extra tap (New over a save is 4 taps from Home, a Continue or a New with no save 3) to a lost game. Recorded as the `Assumption for entry 11` below.

**Continue's roster:** `peek` gives no mode, and `GameMatchActivity` (out of touches) resumes only through its solo path until entry 9. A solo roster on a pass save would refuse it and start a solo match whose first snapshot replaces it; a pass roster leaves it untouched, because until entry 9 a pass match sets no package hash, and without one `GameSaveStore` (`resumeOn()`) reads, writes, and deletes no resume.bin, Over's delete included. So only a two-mode game asks the solo-only `peek` at the tap, and only its Valid gives solo. A solo-only game cannot hold a pass save of its installed package (the hash ties the save to this manifest), so its Unreadable stays solo and ends in the error view. Entry 9 builds the roster from the save. Interim (until entry 9), not wired: a pass save's Continue starts a new pass match, and a two-mode game's Unreadable Continue does too, instead of the error view; the save stays untouched in both, which `TitleScreenTest` pins.

**Assumption for entry 11 (entry 7's plan, 2026-10-01):** New over a save (Valid or Unreadable) asks a second time, Cancel focused, "Start a new game?" / "This replaces the saved game.", Cancel or New game; a New with no save, and Continue, start at once. Reason: one save per game, which New replaces for good; one extra tap only when a save is there keeps Continue and a fresh New at 3 taps from Home. Entry 3's owner session may change it to side-by-side rows with no confirm.

**Guards kept from today's `activateIndex`** (both files): bounds check; unavailable check (logs, repaints); `passSeats == 0` (logs, repaints, starts nothing); OOM on the match (logs, repaints after `clearTapFlash`).

## Verification

**Commands:**
- Host suites (AGENTS.md CMake/Ninja/ctest, hosttest lock) -- all pass.
- `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py`, `./bin/clang-format-fix` twice -- pass, nothing new.
- Build lock, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`, `sim.sh build x4pro`; `scripts/check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- delta over `c1902721` within +244,592 B flash, +816 B static RAM.
- Simulator screenshots into `story-title-screen-screenshots/`: no save; solo save; pass save; Unreadable save; the New-over-a-save dialog; a solo and a pass start.

**Results (2026-10-01, after the review patches, on the tree this entry commits):**
- Host suites: CMake/Ninja build and `ctest -j4` under `/tmp/crosshatch-hosttest.lock`: 1,497 of 1,497 pass (`ModePickerHarnessTest` 41: `TitleScreenTest` and `HostCapsDouble`; `GamesLauncherHarnessTest` 58, `GameRemoveLauncherTest` 40, `ContinueLauncherTest` 29). Mutation check after the patches: `startResume` passing `Start::New` fails six `TitleScreenTest` cases (the solo, pass, pass-only, and both Unreadable Continues); forcing a solo roster failed three pass-save guard cases before the patches (implementer's check).
- Matrix audit: every row has a passing case (no save: `WithNoSaveTheHeaderIsTheGamesName...`; solo save: `ConfirmWithNothingMovedResumesTheSoloSave`; pass save: `ContinueOnAPassSaveStartsAPassMatchThatLeavesTheSaveUntouchedThroughOverAndExit`; Unreadable: `ASoloGamesUnreadableSave...ErrorView`, `ATwoModeGamesUnreadableSave...LeftAlone`; New over a save: `ANewRowOverASaveAsksFirst...`, `BackAndAConfirmOnTheFocusedCancel...`, `NewGameStartsANewMatchInTheModeTapped`, `ANewRowReachedFromTheLauncherAsksFirstOverASave`; pass New: `ATapOnPassStartsATwoSeatPassMatch...`, `APassThatCannotFitStartsNothing`; launcher: `ASoloGameRowPushesItsTitleScreen`, `TheLaunchersContinueRowStillResumesDirectly`). 3 taps from Home: `ASoloGameAndAPassGameEachStartInTwoTapsFromTheLauncher` plus Home's Games row, and in the simulator (Games, Pass open, Pass and play).
- `check_layers.py`: 487 include edges pass. `check_upstream_touches.py`: PASS (`english.yaml`, ledger row 2, the only upstream file). Every `scripts/*_test.py` passes. `./bin/clang-format-fix` twice: nothing new, nothing outside these paths.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` SUCCESS, `pio run -e default` SUCCESS, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` PASSED, the same for `default` PASSED, `sim.sh build x4pro` SUCCESS.
- Measurement (entry 7, 2026-10-01): on this entry's committed tree (lane-b on `fb315fd8`, entries 1, 2, 4, 5, 6 merged; firmware sources unchanged between the measurement and the commit), `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`: x4pro `firmware.bin` 5,921,728 B games on, 5,679,632 B off, +242,096 B (13,904 B under the gate); static internal RAM +784 B (`.dram0.bss` +16, `.iram0.text` +684, `.iram0.text_end` +84; 240 B under); `objects` clean (43 objects, largest mutable static 4 B). Over the base at `c1902721` (+233,440 B, +784 B): +8,656 B flash and +0 B static RAM, within the 11,152 B and 32 B bar, leaving 2,496 B and 32 B for entries 8, 9, and 10. Entry 7's own share is unmeasured: entries 5 and 6 recorded no measurement, so the +4,544 B since entry 4's (+237,552 B) is theirs and this entry's together.
- No CI gate or workflow changed, so no fresh-tree run.
- Simulator (x4pro), in `story-title-screen-screenshots/`:
  - `title-counter-no-save.png`: a solo-only game, no save: header "Counter", one Solo row.
  - `title-pass-open-no-save.png`: a solo and pass game: Solo, Pass and play.
  - `title-pass-hidden-no-save.png`: a pass-only game: one Pass and play row.
  - `start-counter-solo.png`: Solo started (Home, Games, Counter, Solo).
  - `start-pass-open-pass.png`: Pass and play started, "Player 1 (X) to move" (log: `Mode pass picked for pass-open: 2 seats`).
  - `title-counter-solo-save.png`: a solo save: Continue first and selected, then Solo.
  - `new-over-save-question.png`: Solo over the save asks "Start a new game?", Solo, "This replaces the saved game.", Cancel focused, New game.
  - `after-cancel.png`: Cancel returns to the rows, the save kept.
  - `continue-counter-resumed.png`: Continue resumed the save ("Taps: 3"; log `Resuming at ver 4`).
  - `title-pass-open-pass-save.png`: a pass save (mode 1, n 2, made from a solo save's bytes) offers Continue; the launcher's own list has no Continue row for it.
  - `continue-pass-save-interim.png`: its Continue, before entry 9: `Continue pass-open: a 2-seat pass match (solo-only peek: none)`, a new pass match; the file's md5 the same before and after a move and Leave.
  - `title-counter-unreadable-save.png`: a save that cannot open (a socket at `resume.bin`, open fails ENXIO) still offers Continue.
  - `continue-unreadable-error-view.png`: its Continue ends in the error view, the file kept.

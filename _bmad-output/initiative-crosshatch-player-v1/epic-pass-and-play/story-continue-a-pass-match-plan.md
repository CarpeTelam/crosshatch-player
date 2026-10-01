---
title: 'Continue a pass match'
type: 'feature'
ticket: '9'
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
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.skills/heap-discipline/SKILL.md'
  - '{project-root}/.skills/scope-discipline/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A pass match skips the package hash, so it never writes, reads, or deletes `resume.bin`; Continue on a pass save starts a new pass match (entry 7's interim), and every `Start::Resume` plays the roster its caller passed, so the launcher's solo Continue row could replace a pass save (R8, AD-17, `## 5.5`, `## 5.6`, `## 5.7`).

**Approach:** `GameMatchActivity` sets the hash and `setRoster(roster)` for every match, so pass saves after every committed snapshot in any state Result and HandOff included, and Over's delete and its retry work as in solo. `Start::Resume` reads the save before the VM exists, through `loadResume(ver, unreadable, manifest, gameHostCaps(), saved)`, and plays `saved`: a hidden pass save starts at HandOff, an open one at Playing on its saved turn seat. A save only another host could start stops in the error view with the file kept.

## Boundaries & Constraints

**Always:** every `Start::Resume` plays the save's roster, whatever roster its caller passed; the lifecycle's hidden flag follows the roster played; solo resume tests unchanged in their assertions; every guard in `onEnter` and `seedResume` kept (Design Notes); the hidden forced exit's blank still comes before the resume write, now pinned by a test; locals under 256 B, no new statics, `LOG_*` only, no new string.

**Never:** change `src/games/GameSaveStore.*`, `GamesLauncherActivity.*`, `GameModeActivity.*`, `lib/**`, `english.yaml`, `formats.md`, the launcher suites, or the epic file; take `RenderLock` in `onExit()`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| New pass match | pass(2), `.pkg` | each committed snapshot written, mode 1 n 2, in Playing, Result, and HandOff; Over deletes; Leave keeps | a refused Over delete retried as in solo |
| Resume hidden | pass save of `pass-hidden`, turn 2; caller's roster solo | HandOff (blank) first, its tap draws seat 2 at the saved moves; no setup | — |
| Resume open | pass save of `pass-open`, turn 2; caller's roster solo | Playing, seat 2's frame at the saved board; no setup | — |
| Sleep mid-match, Continue | hidden and open | the match resumes at the snapshot sleep wrote | — |
| Host cannot start | pass save n 3 of a 2..3 game on a 2-seat host | error view "The saved match could not be resumed"; no game runs | file bytes unchanged through play attempts and exit |
| Unreadable pass save | read fault | error view, file unchanged | as solo |
| No usable save | none, other package, malformed | a new match with the caller's roster (logged), as today | — |
| Hidden forced exit, snapshot pending | Playing, a write that failed earlier | blank pushed before any resume op; then the snapshot written | — |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.cpp` -- ctor :112 builds `lifecycle(roster.mode == Pass && manifest.hidden)`: move that rule into an anonymous-namespace `isHiddenPass(roster, manifest)` used twice. `onEnter` :129: drop the pass branch (`"pass match; no resume.bin until pass saves"`); with a hash, `setPackageHash` then `store.saves().setRoster(roster)`. After `assets.load` and `replay.loadFonts`, for `Start::Resume` call a reworked `seedResume` before `GameVM::create` (the snapshot span lives in the store's buffer, which `assets.load` reuses through `restoreInto`, so the read must follow it; `create` uses only `store.slot()`); then `create(..., roster, lifecycle.hiddenPass())`; then `setResume(snapshot, ver)` when a snapshot came back (its refusal: `LOG_ERR` as today, `fail(STR_GAMES_START_FAILED, tr(STR_GAMES_RESUME_FAILED))`). `seedResume(std::span<const uint8_t>& snapshot, uint16_t& ver)` returns false (caller fails as today) for: empty and `unreadable` (today's log), or empty and `savedForAnotherHost()` (`LOG_ERR "%s: resume.bin is a save this host cannot start; not starting a new match over it"`); true and logs "no usable resume.bin; starting a new match" for any other empty; on a load sets `roster = saved`, `lifecycle = MatchLifecycle(isHiddenPass(saved, manifest))`, `LOG_INF "%s: resuming a %s save of %u seats"` (`GameCore::modeName`). New private `savedForAnotherHost()`: `HostCaps any = gameHostCaps(); any.pass = true; any.maxSeats = Roster::MAX_SEATS;` and true when `loadResume(ver, unreadable, manifest, any, saved)` loads a snapshot (the store's buffer and roster are then spent; the match goes to Error). Include `games/GameHostCaps.h`.
- `src/activities/games/GameMatchActivity.h` -- comments: class (pass saves; Resume plays the save's roster; hidden resume at HandOff), `Start`, the ctor's `roster` (New's; Resume replaces it), `flushResume` (Playing, Paused, Result, HandOff), `seedResume`, `savedForAnotherHost`.
- `src/games/GameSaveStore.h` -- read only: the five-argument `loadResume` adopts `saved` as the store's roster; `setRoster` before the load is overridden by it.
- `test/game_script/harness/ResumeMatchTest.cpp` -- new `PassResumeTest : ResumeMatchTest`: `enterPass(id, hidden, start, roster = pass(2))` builds the fixture's manifest (pass-open: seats 1..2, solo and pass; pass-hidden: 2..2, pass, hidden), `installFixture` + `installPkg`; helpers reading the file's mode (byte 14), n (15), ver (16..17).
- `test/game_script/harness/GameMatchTest.cpp` -- `PassMatchTest.TwoSeatsAlternate...` (:1070): drop the skip-line expectation; Leave now keeps a pass save (mode 1, n 2). Its other pass tests (`HiddenPassTest` passes no `.pkg`) need no change.
- `test/game_script/harness/ModePickerTest.cpp` -- only the cases entry 7 wrote "until entry 9": :472, :572 (the skip line), `ContinueOnAPassSave...` (:660) and `ContinueOnAPassOnlyGamesSave...` (:686) now resume the save ("Resuming at ver", no `setup ran`); `ATwoModeGamesUnreadableSave...` (:722) now ends in the error view, bytes unchanged. Rename these three to say so.
- `docs/crosshatch/game-canvas.md` -- Resume (:50): solo or pass; Continue plays the save's roster; a hidden pass save resumes at HandOff; a save only another host can start stops in the error view. Solo states' Starting rows; the forced exit's step 3 ("Playing or Paused only" → plus Result and HandOff); Hidden pass states: Starting → HandOff row (also a resumed match) and replace its last line ("keeps no resume.bin").

## Tasks & Acceptance

**Execution:**
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- as in Code Map
- [x] `test/game_script/harness/ResumeMatchTest.cpp` -- `PassResumeTest`, one or more cases per matrix row; the hidden ones use `Roster::solo()` as the caller's roster to prove the save's roster wins; the forced-exit case records at the blank's push (`renderer->onDisplay`) that no `resume.bin` op has run yet, then that the file holds the pending snapshot after the exit (deferred-work `## 5.6`)
- [x] `test/game_script/harness/GameMatchTest.cpp`, `ModePickerTest.cpp` -- the forced follow-through above, nothing else
- [x] `docs/crosshatch/game-canvas.md` -- as in Code Map
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 5.9` at the end: `formats.md`'s "A save that is `None` then starts a new match" now has the another-host exception, and its callers paragraph names the match's new form (sweep); `## 5.6`'s order item and `## 5.5`'s `setRoster` item and `## 5.7`'s every-Resume-roster item resolved

**Acceptance Criteria:**
- Given every host suite, when run, then all pass, and the solo `ResumeMatchTest` cases are unchanged.
- Given x4pro games on and off, when `check_flash_budget.py` compares them, then the delta over the base (+233,440 B, +784 B at `c1902721`) is within 11,152 B and 32 B.

## Implementation Notes

- Implemented by a context-free implementation subagent from this plan, as the Code Map has it; host suites 1,510 of 1,510, the new and changed cases (17) also `--repeat until-fail:30`; fast checks pass. Its mutation checks: pushing the blank after `flushResume()` fails the forced-exit order case, `savedForAnotherHost()` always false fails the another-host case, and dropping the lifecycle rebuild fails the hidden resume case.
- `PassResumeTest` (13 cases, `ResumeMatchTest.cpp`) has `enterPass` (fixture, `.pkg`, the fixture's manifest) over `enterWith(manifest, roster, start)`; the another-host case builds its own 2..3 pass manifest over the counting game. The hidden and open resume cases pass `Roster::solo()` as the caller's roster.
- `ModePickerTest.cpp` (the title screen's suite) is not named in `touches`; its five edits are the ones entry 7 wrote "until entry 9" for, which the orchestrator's notes say must end with this entry (the skip line twice; three Continue cases renamed: `ContinueOnAPassSaveResumesItAsAPassMatch`, `ContinueOnAPassOnlyGamesSaveResumesIt`, `ATwoModeGamesUnreadableSaveEndsInTheErrorViewAndIsLeftAlone`). Nothing else in it changed.
- No test double was added or extended.
- Review patches (pass 1): the implementer applied rows 1 to 9 of the triage log; this session braced one `if` in the new no-usable-save case (`-Wdangling-else`). `PassResumeTest` now has 16 cases.
- The worktree's `.pio/build/x4pro/src` and `.pio/build/simulator_x4pro/src` held `GameSaveStore` objects older than entry 5's source (dated 07:03, the source 09:28), which PlatformIO did not rebuild, so the first `pio run -e x4pro` and `sim.sh build x4pro` failed to link (`GameSaveStore::loadResume`/`setRoster`/the new `peek` undefined, also from `GameModeActivity`). Deleting those two `src` object folders fixed both; the code is unchanged. A stale tree, not this change; CI builds fresh.
- Deferred under `## 5.9`: `formats.md`'s None rule and callers paragraph, and `GameModeActivity`'s two interim comments (both files outside touches; the sweep).

## Plan Change Log

## Review Triage Log

Pass 1 (2026-10-01). The four lenses ran as context-free subagents, launched together in one message in the foreground (each told the worktree is read-only); three ran in the background despite the flag, and this session waited for all four before triage. All four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG), intent-alignment (IA). Verdicts: medium 1, low 10, false 3, plus IA's descriptive report. No intent_gap or bad_plan; the patches went back to the step-03 implementer (the same agent, re-engaged), and verification re-ran on the patched tree.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | VG1, BH7 | No test resumes a valid solo save with a pass caller roster (or a hidden manifest that also offers solo), so narrowing the adoption to pass saves passes every test; `GameModeActivity::startResume` passes pass(n) on a two-mode game whenever its solo-only peek is not Valid | medium | patch | Real gap on a reachable path (a transient fault at the title screen's tap-time peek). A case resumes a solo save under a pass(2) caller, open and hidden-with-solo, expecting Playing and mode 0, n 1 writes |
| 2 | BH1, EC1, VG other, IA §3.4 | `savedForAnotherHost` ignores `unreadable` on its second read, so a card fault there starts a new match over a save another host could resume | low | patch | Real (one line). Returns true when that read loads or reports unreadable |
| 3 | EC3 | `AHiddenMatchSleptInResult...` resumes with a pass(2) caller, though the plan says the hidden resume cases pass `Roster::solo()` | low | patch | Real; the caller roster is now `Roster::solo()` |
| 4 | BH8, IA §3.6 | No test plays a resumed pass match to Over and Play again, and no hidden match's Over delete is tested | low | patch | One case: a resumed hidden save played to Over (file gone), Play again to HandOff writing a mode 1, n 2 save |
| 5 | BH9 | The another-game's-seats case matches the log suffix ": "; no `bad seat count` case | low | patch | Expects ": seats not startable"; a pass n 1 case expects ": bad seat count" |
| 6 | BH5 | Docs and comments name only "more seats", but the widened host also turns Pass and Play on | low | patch | Wording now names both |
| 7 | BH3, EC2 | The another-host guard is reachable only by Continue (an Unreadable peek, then a clean read), and a New match from the title screen replaces such a save without a question | low | patch (doc) / reject (code) | `formats.md` already says such a save "survives until the game's next match replaces it", and New is the title screen's explicit choice (`GameModeActivity`, stays_out); game-canvas.md now says New replaces it |
| 8 | BH12 | `lifecycle` is reassigned though `MatchLifecycle` says its flag is fixed for the match's life | low | patch | Comment at the call site: still Starting, before the VM exists, nothing applied |
| 9 | BH13 | The two-tap test lost its only match-side proof of the pass roster; the solo log reads "1 seats" | low | patch | A match-side check replaces it; the log line is reworded |
| 10 | BH2 | A refused save is read twice and logged twice, the first line saying "discarded" for a file that is kept | low | reject | Only on the refusal path (rare); telling the reasons apart without the second read needs `GameSaveStore` to report them, which is stays_out |
| 11 | BH4 | The another-host refusal shows the generic "The saved match could not be resumed" | low | reject | The epic's Notes: entry 9 adds no string |
| 12 | BH6 | No test reaches `mode not startable` (`any.pass = true` unpinned) | low | reject | `HostCapsValues::PASS` is a constant true on every v1 host and in the simulator, so the branch needs a new host-caps seam in the match suites to test; the pass-off widening is for a later firmware |
| 13 | BH10 | Tests hard-code resume.bin's byte offsets | false | reject | The layout is a documented, versioned format (`formats.md`), and the solo tests' `resumeBytes` already pins it on purpose |
| 14 | BH11 | `GameModeActivity::startResume`'s comment is now wrong | false | reject | Already deferred under `## 5.9` (file stays_out); the match logs the roster it takes |
| 15 | BH13 | The plan file is not in the diff | false | reject | It is added with the commit |
| 16 | IA §3.1, §3.2, §3.3, §3.5 | The launcher's Continue row has no end-to-end pass-save test; the title-screen tests pass the pass roster already; a save the game itself cannot start is replaced; formats.md lags | -- | descriptive | The launcher suites are lane B's (entry 8 removes those rows); the callee cases use a solo caller; the game-cannot-start reading is the Design Notes' choice (no firmware writes such a save); formats.md is deferred under `## 5.9` |

## Design Notes

**Why read before `create`:** the VM's `Session` and the lifecycle are built from the roster, so the save must be read first; the snapshot span stays valid because nothing between the read and `setResume` calls the store.

**Host-cannot-start vs nothing to lose:** `loadResume` answers empty for no file, another package, a malformed file, and a save this game or host cannot start alike. Only the last is a save another host (a firmware with more seats) can resume, so starting new over it would lose it (R8: "kept"). A save of this package that the game itself cannot start cannot be written by any firmware (the hash ties it to this manifest), so only the host is widened. `GameSaveStore` is `stays_out`, hence the second read through its public API, only on that refusal path.

**Guards kept** (`git log -L`: 68ec417b, 46774c8b, b964078a, 51f0fe9a, 65c921a5): store OOM fails first; an unreadable `.pkg` on Resume fails (a match without the hash cannot tell the save from any file); asset failures fail; a save that would not read fails, leaving the file; `setResume`'s refusal fails (unreachable today, kept); null `create`/`start` fails as OOM; `vm` set under `RenderLock`; `handle(Started)` last.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j4'` -- expected: all pass; the new cases also `--repeat until-fail:30`.
- `for t in scripts/*_test.py; do python3 $t; done`, `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new.
- After the review, under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`, `sim.sh build x4pro`; `scripts/check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`.
- Simulator screenshots into `story-continue-pass-screenshots/`: Continue on the title screen into the hand-off (pass-hidden), and into the restored seat (pass-open and pass-hidden's next seat).

**Results (2026-10-01, after the review patches, on the tree this entry commits):**
- Host suites: CMake/Ninja build and `ctest -j8` under `/tmp/crosshatch-hosttest.lock`: 1,513 of 1,513 pass; no warning from the changed files. `ctest -R "PassResumeTest|TitleScreenTest|PassMatchTest|HiddenPassTest|ResumeMatchTest" --repeat until-fail:30`: 110 tests, no failure. Mutation checks (implementer): the blank pushed after `flushResume()`, `savedForAnotherHost()` always false, no lifecycle rebuild, and roster adoption narrowed to pass saves each fail a case.
- Matrix audit: new pass match (`ANewOpenPassMatchWritesEachSnapshotAsAPassSaveAndOverDeletesIt`, `AHiddenPassMatchWritesItsSnapshotsInHandOffAndResult`, `LeavingAPassMatchKeepsItsPassSave`, `ADeleteTheCardRefusesAtAPassMatchsOverIsRetriedAsInSolo`); resume hidden (`AHiddenPassSaveResumesOnTheBlankAndItsTapShowsTheSavedTurnSeat`); resume open (`AnOpenPassSaveResumesInPlayOnTheSavedTurnSeatsFrame`); sleep then Continue (`AHiddenMatchSleptInResult...`, `AnOpenMatchSleptMidMove...`); host cannot start (`ASaveOnlyAnotherHostCanStartStopsInTheErrorViewAndIsKept`); unreadable (`AnUnreadablePassSaveStopsInTheErrorViewAndIsKept`); no usable save (`WithNoUsableSaveContinueStartsANewMatchWithTheCallersRoster`); hidden forced exit (`AHiddenForcedExitPushesTheBlankBeforeAnyResumeOpAndThenWritesThePendingSnapshot`). All ran and passed.
- Every `scripts/*_test.py` passes; `check_layers.py`: 489 include edges pass; `check_upstream_touches.py`: PASS (fork files only); `./bin/clang-format-fix` twice: nothing new, nothing outside these paths.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` SUCCESS (after the stale-object note above), `pio run -e default` SUCCESS, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` PASSED, `sim.sh build x4pro` SUCCESS.
- Measurement (entry 9, 2026-10-01): on this entry's tree (lane-a on `8b7b8f68`, entries 1, 2, 4, 5, 6, 7 merged; firmware sources unchanged between the measurement and the commit), `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`: x4pro `firmware.bin` 5,922,032 B games on, 5,679,632 B off, +242,400 B (13,600 B under the gate); static internal RAM +784 B (`.dram0.bss` +16, `.iram0.text` +684, `.iram0.text_end` +84; 240 B under); `objects` clean (43 objects, largest mutable static 4 B). Over the base at `c1902721` (+233,440 B, +784 B): +8,960 B flash and +0 B static RAM, within the 11,152 B and 32 B bar, leaving 2,192 B and 32 B. Against entry 7's measurement (+242,096 B, the same four steps on firmware sources equal to `8b7b8f68`'s): +304 B for this entry.
- No CI gate or workflow changed, so no fresh-tree run.
- Simulator (x4pro; `pass-hidden` and `pass-open` packed with `pack_game.py` and installed from `fs_/games`), in `story-continue-pass-screenshots/`:
  - `hidden-result-before-sleep.png`: seat 1's move in a new hidden match, Result ("Tap to pass to player 2"); log: resume.bin saved at ver 1 in HandOff and ver 2 in Result; sleep then pushed the blank ("forced exit: blank hand-off screen pushed") and woke on Home.
  - `hidden-title-continue.png`: Games, Pass hidden: Continue first, then Pass and play.
  - `hidden-continue-handoff.png`: Continue opens on the blank hand-off screen (log: "resuming the save's roster: pass, 2 seat(s)", "Starting -> HandOff", "Resuming at ver 2").
  - `hidden-continue-seat2.png`: its tap shows seat 2, the saved turn seat: "Player 2's secret: river", "Moves: 1".
  - `open-before-sleep.png`: an open pass match after X took the corner, "Player 2 (O) to move", before sleep.
  - `open-title-continue.png`: after waking, Pass open's title screen: Continue, Solo, Pass and play.
  - `open-continue-seat2.png`: Continue resumes in Playing on seat 2's frame with the saved board (log: "Starting -> Playing", "Resuming at ver 2").

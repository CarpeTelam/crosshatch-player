---
title: 'Cross-story review fixes for epic-install-and-launcher (e4-x)'
type: 'bugfix'
ticket: ''
created: '2026-09-29'
status: 'built'
baseline_revision: '1faaa432099fd4b7777d3b38a83c5daa5f28ccde'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/cross-story-review.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The epic's cross-story review (`cross-story-review.md`, rows 1 to 9) found: (1) `pack_game.py` accepts an `icon.png` side that the converter scales to 63 px, which the installer rejects as `BadImage`; (2) `letStartedMatchesGo` deletes the save a Leave writes, so no test covers New, move, Leave, Continue; (3) no test installs the packer's live output; (4) an installed inbox file that will not delete reinstalls on every visit; (5) an OOM in the Continue list's allocation replaces every Continue row with the game's own row; (6) Play again forgets an Over delete that failed before the rematch has written; (7) the installer installs a 65th game the registry will not list; (8) `GamePackageInstaller::remove` has 288 B of locals; (9) the nesting limit is a hand copy with no shared vector.

**Approach:** One minimal fix per row, as the orchestrator specified, each with a test that fails without it.

## Boundaries & Constraints

**Always:** Only the files the orchestrator named (Code Map). Keep every guard of a function that is rewritten (Design Notes). Allocate nothing new; add no mutable static. `tr(STR_…)` keys in `english.yaml` only. `Error` gains one value, appended, so no number moves. Host-test flake stays cured (20 full `ctest -j8` runs, and each changed test under `--repeat until-fail:200`).

**Never:** No change to `PngToBmpConverter` or any upstream file outside the ledger. No `API_LEVEL_FROZEN` change, no CI-gate loosening, no `ci.yml` edit. No `tickets.py mark`/`pull`. Rows 10 to 13 are not built.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Icon side scales to 63 | square `icon.png` of 41 | packer exit 1: "which the installer's converter scales to 63x63, not 64x64; use a side that scales to exactly 64, such as 40, 42, 64, 128" | nothing written |
| Icon side scales to 64 | 1, 64, 100, 2048 | packs | none |
| Installed, inbox file will not delete | `failRemove` on `/games/g.cpgame` | installed; file renamed `g.cpgame.installed`; not re-installed | rename fails too: `SdCard`, file kept |
| 65th game | 64 folders with `.pkg`, new id | `Error::TooManyGames`, file stays, not `.bad`, its reason shown | same id: installs |
| New, move, Leave | forced exit wrote `resume.bin` | new Continue row selected on its page; Confirm resumes the move | none |
| Over delete fails, Play again, rematch write fails, Leave | `failRemove`, `failOpenWrite` | finished round's `resume.bin` removed at Leave | delete keeps retrying until the first write |
| Nesting | manifest at 32 levels, at 33 | parse/pack accept, refuse | `Syntax` / "nests deeper" |

</frozen-after-approval>

## Code Map

- `scripts/pack_game.py` -- `check_members` (icon rule), new `f32`, `icon_scaled_side`, `icon_side_hint`; `MAX_NESTING` (:90) comment. `lib/PngToBmpConverter/PngToBmpConverter.cpp:578-590` -- the arithmetic mirrored; unchanged (upstream, no ledger row).
- `src/games/GamePackageInstaller.{h,cpp}` -- `install` (delete at the end, 65th check after `readManifest`), `markBad` -> `moveAside`, `Job::badPath` -> `asidePath`, `packageIsInvalid`, `describe`, `remove`; `forEachInboxFile` skips `.installed` because it keeps only `*.cpgame`. `src/games/GameRegistry.h` `MAX_GAMES` (64).
- `src/activities/games/GamesLauncherActivity.{h,cpp}` -- `continueOf` (member array), `loadContinue`, `reasonText`. `lib/I18n/translations/english.yaml` -- `STR_GAMES_INSTALL_TOO_MANY_GAMES`.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `handle` (Playing), `flushResumeOf`, `loopPlaying`; `src/games/GameSaveStore.{h,cpp}` -- `resumeWrites()`.
- `test/game_script/harness/MatchSupport.h` (`Saves`, `letStartedMatchesGo`), `ContinueLauncherTest.cpp`, `GamesLauncherTest.cpp`, `GameRemoveLauncherTest.cpp`, `GamePackageInstallerTest.cpp`, `PackageHardeningTest.cpp`, `ResumeMatchTest.cpp`; new `PackedFixturesTest.cpp`, `packed_fixtures.cmake`, `pack_fixtures.py`, fixture `pack-images/`.
- `test/game_core/package_vectors.json` (`nesting`), `PackageLimitsTest.cpp`, `scripts/pack_game_test.py`. `docs/crosshatch/formats.md`, `game-icons.md`.

## Tasks & Acceptance

**Execution:**
- [x] `scripts/pack_game.py`, `pack_game_test.py` -- refuse an icon side the converter scales off 64, by its float32 arithmetic -- row 1
- [x] `GamePackageInstallerTest.cpp` -- side 41 (and seven more) expects `BadImage`, beside `AnIconOfAnySquareSizeComesOutAt64x64` -- row 1
- [x] `MatchSupport.h`, the three launcher tests -- `Saves` flag and return value; New, move, Leave, Continue test; three tests state their save state -- row 2
- [x] `packed_fixtures.cmake`, `pack_fixtures.py`, `PackedFixturesTest.cpp`, `pack-images/` -- pack fixtures at build time and install them -- row 3
- [x] `GamePackageInstaller.cpp` -- rename an installed inbox file that will not delete -- row 4
- [x] `GamesLauncherActivity.{h,cpp}` -- fixed `continueOf`; the OOM test is removed -- row 5
- [x] `GameMatchActivity.cpp`, `GameSaveStore.{h,cpp}`, `ResumeMatchTest.cpp` -- keep the Over delete pending until the rematch's first write -- row 6
- [x] `GamePackageInstaller.{h,cpp}`, `english.yaml`, `GamesLauncherActivity.cpp`, tests -- `TooManyGames` -- row 7
- [x] `GamePackageInstaller.cpp` -- `remove` with one path buffer -- row 8
- [x] `package_vectors.json`, `pack_game_test.py`, `PackageLimitsTest.cpp` -- `nesting` vector -- row 9

**Acceptance Criteria:**
- Given the host suites, when `ctest -j8` runs 20 times, then all pass every time; each changed test passes `--repeat until-fail:200`.
- Given the final tree, when the flash-budget steps run, then the delta stays under +240,496 B flash and +808 B static RAM.

## Implementation Notes

- Implemented directly in the foreground, no implementation subagent. The review lenses ran as foreground subagents (Review Triage Log).
- Row 5: `WhenTheContinueListCannotBeAllocatedTheGamesAreListedAndRemoveStillWorks` is **removed**, with the `operator new[]` hook in `ContinueLauncherTest.cpp` that only it used, because the allocation it made fail is gone. `TheContinueListHoldsASaveForEveryGameTheRegistryLists` (64 games, 64 saves, the last Continue row resumes) replaces it.
- Row 2: `Saves::Discard` / `Saves::Keep` is a required argument of `ContinueLauncherTest`'s `reopen`; `GamesLauncherTest` and `GameRemoveLauncherTest` pass `Discard`, the helper's old behaviour. `TheKeysWalk...` and `AfterLeavingAGameWithNoSave...` reach "no save" by making the card refuse the save (`failOpenWrite` on its tmp file) and reopening with `Keep`, instead of taking the file away.
- Row 6: `GameSaveStore::resumeWrites()` (a `uint32_t` member, no static) tells `flushResumeOf` that a snapshot was written.
- Row 9: the vectors' `nesting` case holds two whole manifests (the limit is 32 open containers; the second is one deeper). The `icon_scaling` case, added after review, holds the 280 sides of row 1.
- New files: `test/game_script/harness/{PackedFixturesTest.cpp,packed_fixtures.cmake,pack_fixtures.py}` and `test/game_script/fixtures/pack-images/` (with its README row).
- Assumption for entry 14: a valid package that would be the 65th game stays in `/games` with its reason shown on every visit until a game is removed; it is not renamed `.bad`, since it is not invalid.
- Assumption for entry 14: an installed package whose inbox file will not delete is moved to `<name>.cpgame.installed` and counted as installed; the leftover file is harmless and a person may delete it from a computer.

## Plan Change Log

None: no bad_plan or intent_gap loopback.

## Review Triage Log

Pass 1, on the diff from the baseline `1faaa432` to the first WIP tree (the plan excluded, so the blind lens saw code only): the four lenses ran as context-free foreground subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), and all four returned before triage. Verdicts: high 0, medium 2, low 18, false 6. Routes: patch 10, defer 2, rejected 8 (real but unlikely, and the fix adds guards or branches; one row is the intent-alignment lens's descriptive report, which found no divergence to route), false 6. No intent_gap or bad_plan. The patches were made after the pass and are part of the one commit.

| Lens | Finding | Verdict | Route | Evidence |
|---|---|---|---|---|
| verification-gap | The 280-side refusal list is pinned only against its own Python model; the converter is run on 27 sides | medium | patch | Real: `pack_game_test.py` asserted values that `icon_scaled_side` computed itself. Fix: `package_vectors.json` `icon_scaling` list; `GamePackageInstallerTest.TheConverterScalesEverySquareIconToExactly64ExceptTheSidesInTheVectors` runs the real converter on a header-only PNG of every side 1 to 2,048 (its BMP header is written before a pixel is decoded) and the Python test compares `icon_scaled_side` with the same list. |
| blind | Float32 mirror checked against the converter for only a handful of sides | medium | patch | Same as the row above. |
| verification-gap (other) | `wouldBeOverTheLimit` counts a `.pkg` by existence, the registry lists only valid ones | low | patch | Real but conservative. Now `GameRegistry::readPackageHash` (the registry's own first test); `OnlyFoldersWithAMarkerCount` gains a folder with a `.pkg` that does not parse. |
| edge 1, blind 3 | Same count can hold folders the registry skips for their manifest | low | defer | Real; the count can only be high, never let a hidden game through. Recorded under `## e4-x`; the manifest read of 64 folders on every install is the cost of exactness. |
| edge 12 | formats.md claims a folder counts when it holds a `.pkg` | low | patch | Wording now says valid `.pkg` and that the count can be high. |
| verification-gap (other) | Card-cannot-list branch of `wouldBeOverTheLimit` untested | low | rejected | A card that cannot open `/.games` also fails the commit's `ensureDirectoryExists`/rename, so the install reports its own SdCard; a guard or test adds complexity for a state with no demonstrated harm. |
| edge 2 | `!dir` returns "not over the limit" on a card fault | low | rejected | Same: the install fails on its own card fault (commit step); `/.games` missing is the normal first install. |
| edge 3 | Limit check runs before extraction, so an otherwise invalid 65th package says TooManyGames | low | rejected | By design: the file waits and gets its real verdict (`.bad` and reason) once a game is removed; checking after extraction would spend the install's work first. |
| edge 4 | 32+ refused files could starve a later same-id replacement (`MAX_PER_RUN`) | low | rejected | Needs 32 stuck 65th-game files; the same batch limit applies to any stuck file today. |
| edge 5, blind 5 | `.installed` files accumulate and are never cleaned or announced | low | defer | By design (the brief: rename, log, report only a failed rename); documented in formats.md with how to reinstall (rename back). Recorded under `## e4-x`. |
| edge 6 | `moveAside` renames onto a `.installed` that would not delete | low | patch | Behaviour is the old `markBad`'s and the rename fails on the name, reporting SdCard; the finding's early return would give the same result. Added `AnEarlierInstalledCopyThatWillNotBeReplacedLeavesTheFileReported` to pin it. |
| edge 7 | A future write path could bypass `resumeWrites()` | low | false | Speculative: `flushResume` is the only writer of `resume.bin`; `loadResume` reading the tmp file is a read. |
| edge 8 | Pause/Resume/Error before the first snapshot leaves the pending delete | low | false | `loopView` retries in Paused (state is not Error) and `leave()`/the forced exit retry in every state; before this change the pending flag was already cleared and the save never deleted. |
| edge 9, blind 7 | `continueOf` writes past 64 if the registry's cap broke | low | patch | `GameRegistry::load` caps `count` at `MAX_GAMES`, but the heap array used to be sized to the count; the loop bound is now `min(count, MAX_GAMES)`, one expression. |
| edge 10 | The removed OOM test's coverage | low | false | The allocation is gone; `TheContinueListHoldsASaveForEveryGameTheRegistryLists` (64 saves, the last Continue row resumes) replaces it. |
| edge 11 | `remove()` guards after the buffer reuse | low | false | Reviewer traced every guard as kept (Design Notes lists them). |
| edge 13 | Case-insensitive FAT names vs `strcmp` of the id | low | rejected | The registry lists a folder only when its manifest id equals the name exactly, so a differently cased folder is not a listed game anyway. |
| blind 2 | The 64-game scan repeats per inbox file | low | rejected | About 64 opens against an install that extracts a zip; not the loop it was compared to. |
| blind 4 | `remove()` rewritten, changes a log line | low | false | Row 8 asks for it; the "Keeping ..." line prints the same text (`/.games/g` ... `/.games-tmp/g`). |
| blind 6 | `moveAside` buffer sized for one suffix; truncation unchecked | low | rejected | `.bad` (4 B) is shorter than `.installed` (10 B) and the inbox path is bounded by `INBOX_PATH_BYTES`; a guard for a caller that does not exist adds branches. |
| blind 8 | `resumeWrites()` counter where a `bool` return would do | low | false | `flushResume` returns "true unless a write was due and failed", true also when nothing was written; a new meaning would touch every caller and test. A stale-VM flush that writes also replaced the file, so clearing is right. |
| blind 9 | `ASSERT_GE(…, 19u)`; copied `CANVAS_X`; unused return set; token buffer bound; `subTest` cleanup | low | patch (return set), rest rejected | The unused return value of `letStartedMatchesGo` is dropped. `GE` is the test's own convention; `CANVAS_X` is copied with a comment because `ResumeMatchTest` keeps its constants in an anonymous namespace; the token bound is on the string, which is what the parser drops; `subTest` cleanup is the file's pattern. |
| blind 10 | Missing tests: stale `.installed`, UI rendering of the new reason, pack-then-install of a refused side, fixtures not covered, pending delete at Leave and forced exit | low | patch (first, third), rest rejected | Stale `.installed` and the all-sides sweep added. `EveryInstallErrorMapsToItsOwnReason` draws each reason; the other six packable fixtures add no image or framing case the three miss; `ADeleteTheCardRefusedAtOverIsRetriedAtTheForcedExit` covers the forced exit. |
| blind 11 | Packed-fixtures wiring: `.cpgame` not a declared output, no `mkdir`, fresh-clone run | low | patch | Outputs now include the `.cpgame` files and `pack_fixtures.py` makes the folder. The gate is a host test, not a CI gate or workflow; its build tree here was built from scratch by `cmake -S test -B build/test`. |
| blind 12 | Review labels in comments; over-long doc lines; upstream ledger | low | patch | Comments reworded to durable reasons; doc paragraphs rewrapped to 120; `check_upstream_touches.py` passes (`english.yaml` is ledgered). |
| blind 1 (perf), edge 5 (notice), intent-alignment | Notice repeated each visit; readings B/C over A; row 5 removes rather than corrects the fallback; row 8 has no test; `MAX_NESTING` stays a copy | n/a | rejected | The rows are as the orchestrator specified (row 5: fixed array, remove the failure path; row 9: a shared vector); a stack-size rule has no unit test, so its measurement is in Verification. Not a defect of the diff. |

## Design Notes

Guards kept, from `git log -L` on each rewritten function:

- **`markBad` (now `moveAside(job, suffix)`; `5c49754c`, `d731afff`).** An earlier `<name><suffix>` is removed first and a failed remove is logged but the rename is still tried; a rename that fails returns false, so the caller reports `SdCard` and the file stays. `markBad` is the same call with `.bad`. The `install` tail keeps its rule that a file which stays is a failure to report: it now takes that path only when both the delete and the rename fail.
- **`remove` (`21e3c7dc`, `a941ee1d`).** The id vet (`BadManifest`, no path into `/.games` for a bad id); the openability check of `/.games` (a card that does not answer is `SdCard`, not "gone"); an absent folder is `None`; a folder without a `.pkg` beside `/.games-tmp/<id>` goes through `foldersShareClusters` and, if shared, deletes nothing (`SdCard`); the `.pkg` is removed first; a failed `.pkg` or `removeDir` is `SdCard`. One 96 B buffer serves the folder, its `.pkg`, and the scratch path in turn; each path is built again where the old code read it, and the "Keeping" line prints the same text from its parts.
- **`GameMatchActivity::handle`, Playing (`68ec417b`, `d45dd71d`, `46774c8b`).** Only `resumeDeletePending = false` goes. Kept: `resumeWritable`, `clearResumeBackoff()` and `playAgain()` on Play again (the failed delete armed the backoff), `shownFrame`, and the early return for every event but Resume and Back. The pending delete is cleared in `flushResumeOf` by the write counter, on the loop task with every other reader of the flag. The delete keeps its rules: at most every `FLUSH_INTERVAL_MS` unless forced, skipped past the forced exit's deadline, and never in the Error view's loop (Leave retries).
- **`loadContinue` (`253bff56`).** Every game that cannot start is skipped and every `peek` other than `None` gets its row; only the allocation and its OOM branch go. The array is the activity's own, so nothing the render task reads is replaced.

Row 7: the limit is checked after `readManifest` (the id is known, the package is well formed so far) and before the scratch folder is made, so a refused install touches nothing on the card. It counts folders whose `.pkg` parses (`GameRegistry::readPackageHash`, the registry's first test), other than the package's own id.

Row 3: `pack_fixtures.py` shells out to `scripts/pack_game.py` the way the release does, then writes the hash it printed beside the package, so the suite compares the packer's hash with the installer's `.pkg`. It lives beside `gen_hardening_packages.py` (a test helper, not a fork script).

Row 1: `icon_scaled_side` rounds to a single after each float operation. A float division or product of two exact singles rounded once in double and once to single equals the single result (a double has more than twice a single's mantissa), so the emulation is exact. The sweep in `GamePackageInstallerTest` runs the real converter on all 2,048 sides.


## Verification

All figures are from this worktree. The flash-budget, stack, and 20-run figures were measured on the WIP tree that carries every code change and was squashed into the one commit; the only changes after it are this plan, `deferred-work.md`, and the final `clang-format` runs, which changed nothing, so the commit's code is identical to what was measured.

**Commands:**
- Host suites, under the lock: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test`, then `ctest --test-dir build/test --output-on-failure -j8` -- build exit 0; **1322 of 1322 passed**.
- The flake proof, `ctest --test-dir build/test -j8` **20 times** -- 0 failed runs of 20.
- `ctest --test-dir build/test -j8 --repeat until-fail:200 -R 'ContinueTest|GameLimitTest|PackedFixture|PackedImages|InstallerTest\.(AnIconThat|AnInstalled|AnEarlier|TheConverter)|ResumeMatchTest\.(ALeave|ARematch|TheOverDelete)|PackageLimitsTest|ListTest|RemoveLauncher|GamesLauncher|GameRemoveLauncher'` -- 114 tests (every new or changed test, and every test of the two launcher suites whose `reopen` calls the helper) x 200 = **22,800 passes, 0 failures**, 601.6 s.
- Mutation check for row 6: with `resumeDeletePending = false` put back into the Playing case, `ALeaveBeforeTheRematchsFirstWriteStillRemovesTheFinishedRoundsSave`, `ARematchWhoseSetupErrorsStillRemovesTheFinishedRoundsSaveAtLeave`, and `TheOverDeleteKeepsRetryingInTheNewRoundUntilItsFirstWrite` fail; restored.
- `python3 scripts/<name>_test.py` for each of the ten sidecars -- all OK (`pack_game_test.py` 86 tests, `check_flash_budget_test.py` 78, `check_layers_test.py` 52, `gen_game_icons_test.py` 49, `fork_release_test.py` 76, `check_upstream_touches_test.py` 18, `fork_common_test.py` 27, `game_codec_test.py` 22, `check_api_freeze_test.py` 9, `sim_sh_test.py` 5).
- `python3 scripts/check_layers.py` -- passed (469 include edges in 107 game files). `python3 scripts/check_upstream_touches.py` -- PASS; only ledgered upstream paths changed (`lib/I18n/translations/english.yaml`).
- `pio run -e x4pro` (the flash budget's `build on`, on the final code) -- SUCCESS. `pio run -e default` (the C3) -- SUCCESS, 2 min 54 s. `sim.sh build x4pro` -- SUCCESS. The orchestrator builds the other three envs.
- Flash budget, four steps on that tree, under the lock: `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- all exit 0. Games on 5,910,000 B, off 5,678,912 B: **+231,088 B flash** (the epic's final measurement at `9f6e015c` was +230,560 B, so e4-x adds +528 B; the pass bar is +240,496 B, 9,408 B to spare) and **+784 B static internal RAM** (unchanged from the epic's +784 B; the bar is +808 B, 24 B to spare; the `compare` limit of 1,024 B leaves 240 B). `objects`: 43 game objects, largest mutable static 4 B (`lastOpened`), no static initializer. The recorded epic figures were measured the same way (the four steps); this delta is games on minus off within one commit.
- Stack frames of `GamePackageInstaller.cpp`, `-fstack-usage` on x4pro, single-object builds (`PLATFORMIO_BUILD_FLAGS=-fstack-usage pio run -e x4pro -t <dir>/x4pro/src/games/GamePackageInstaller.cpp.o`, never committed), baseline file (`1faaa432`) then final: **`remove` 336 B to 160 B** (whole frames, so under 256); `installAll` 528 B both times (inlines `install`; not e4-x's, deferred); `removeTmp` and `foldersShareClusters` 240 B both times. `wouldBeOverTheLimit` has no frame of its own (inlined; `installAll` stays 528 B).
- `./bin/clang-format-fix` twice -- see the last lines below.

**Matrix audit.** Icon 41: `pack_game_test.py` `test_a_square_icon_that_scales_to_63_is_refused_with_a_side_that_works` and `InstallerTest.AnIconThatTheConverterScalesTo63IsBadImage`; every side: `test_icon_scaled_side_is_the_shared_vector_for_every_side` and `TheConverterScalesEverySquareIconToExactly64ExceptTheSidesInTheVectors`. Icon 64: `test_icon_png_must_be_square`, `AnIconOfAnySquareSizeComesOutAt64x64`. Delete failure: `AnInstalledInboxFileThatWillNotDeleteIsRenamedOutOfTheInbox` and its two siblings. 65th game: `GameLimitTest.*`, `EveryInstallErrorMapsToItsOwnReason`. New, move, Leave: `ContinueTest.ANewMatchLeftAfterAMoveHasAContinueRowSelectedAndConfirmResumesItsMove`. Over delete: the three `ResumeMatchTest` cases. Nesting: `test_the_nesting_limit_is_the_shared_vectors` and `PackageLimitsTest.TheManifestNestingLimitIsTheParsersAndTheVectorsAtAndOverCases`. Every one ran in the 1322 and the repeat run.

No simulator screenshots: no verify names one.

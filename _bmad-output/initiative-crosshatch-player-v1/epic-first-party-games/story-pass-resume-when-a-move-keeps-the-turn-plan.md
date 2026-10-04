---
title: 'Pass resume when a move keeps the turn'
type: 'chore'
ticket: '7'
created: '2026-10-04'
status: done
review: 'thorough'
review_source: 'pinned'
baseline_revision: 'eca7e6c7147dc664a8189d097b134831cd573a21'
route: 'full'
route_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: ['{project-root}/test/game_script/fixtures/README.md']
warnings: []
deferred: []
---

<intent-contract>

## Intent

**Problem:** Pass resume has no test for a hidden pass match saved in `Paused`, or for a snapshot written mid-turn in `Playing`: `pass-hidden` passes the turn on every move, so every snapshot it writes is one `Result` or `HandOff` writes (`deferred-work.md` `## e5-close` V3).

**Approach:** Add an engine fixture `test/game_script/fixtures/pass-keep/` (a hidden two-seat pass game with a move that keeps the turn and one that passes it) and two `PassResumeTest` cases over it, each resuming into the hand-off with the kept move's change in its state; mark V3 resolved.

## Boundaries & Constraints

**Always:** Engine tests use fixtures, never a game in `games/` (R2). The fixture holds only `manifest.json` and `main.lua`, so it packs. The two cases use the real match, VM, and store over the existing doubles, and add no double. Each case fails when the resume path it covers is reverted.

**Never:** Change `src/`, `lib/`, `games/`, `test/game_script/first_party/`, or any upstream file; change what an existing `pass-hidden` test asserts; edit `deferred-work.md` beyond V3's entry. If a new case fails against the current `src/`, HALT: the fix is a new entry.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Saved in Paused | Seat 1's kept move whose write the card refused, then Back (Paused), card healthy, back-off passed | While still `Paused`, `resume.bin` is a two-seat pass save at ver 2; sleep keeps those bytes; Continue (solo caller) resumes in `HandOff` naming "Player 1's turn", its tap shows "Kept: 1" | A write that never comes fails the case at the pump |
| Mid-turn in Playing | Seat 1's kept move, no pause | While still `Playing`, ver 2 is on the card before any sleep; Continue resumes as above | Same |

</intent-contract>

## Code Map

- `test/game_script/fixtures/pass-hidden/main.lua`, `manifest.json` -- the model for the new fixture (hidden, seats 2..2, `modes: ["pass"]`, `game.apply`/`input`/`draw` log lines `apply seat S`, `tap for seat S`, `draw for seat S`); its tests stay unchanged.
- `test/game_script/harness/ResumeMatchTest.cpp`, `PassResumeTest` (line ~1133): `enterPass(id, hidden, start, roster)` (installs the fixture by folder name, sets manifest to hidden pass), `passTheBlank(seat)` (hand-off screen drawn, names `seat`, taps "I'm ready", shows the seat's frame), `moveToResult()` (pass-hidden's tap at canvas (100, 300)), `savedPassAt(ver)`, `pumpToPassSave(ver)`, `holds(push, text)`, `lastPush()`. Add the cases after `AHiddenMatchSleptInResultContinuesAtTheSnapshotTheSleepWrote` (~1444). Models: `AHiddenPassSaveResumesOnTheBlankAndItsTapShowsTheSavedTurnSeat` (~1333, resume asserts: no `draw for seat` before the tap) and `AHiddenForcedExitWritesThePendingSnapshotAndThenPushesTheBlank` (failed-write then backoff pattern: `fakesd::sim().failOpenWrite`, `fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS)`).
- `src/activities/games/GameMatchActivity.cpp`: `loopPlaying` ends with `flushResume()` (the mid-turn path); `loopView` calls `flushResume()` in Paused (the Paused path); `handle()` sets `persistence.setWritable(true)` for Paused. Read-only here; used only to mutation-check.
- `test/game_script/fixtures/README.md` -- line 20's packable list and the Games table: add `pass-keep/`.
- `_bmad-output/implementation-artifacts/deferred-work.md` `## e5-close`, the V3 entry (line ~1048) -- rewrite its `summary` as `Resolved by ...` the way the file's other resolved entries read (`Resolved by entry N (...). It read: <old summary>`).
- `MatchSupport.h` `installFixture` copies every regular file of `test/game_script/fixtures/<name>/`; `resume.cmake` passes the whole fixtures dir, so it needs no line.

## Tasks & Acceptance

**Execution:**
- [ ] `test/game_script/fixtures/pass-keep/manifest.json`, `main.lua` -- new fixture, id `pass-keep`, name "Pass keep", api 1, seats 2..2, `modes ["pass"]`, `hidden true`. State `{seats, turn = 1, kept = 0, passes = 0}`; `status` returns `{turn = state.turn}` (no round end); `input` on a tap returns `{kind = "keep"}` when `ev.y < 200`, else `{kind = "pass"}`; `apply` on "keep" adds 1 to `kept` and keeps `turn`, on "pass" sets `turn = state.turn % state.seats + 1`, `kept = 0`, adds 1 to `passes`; `draw` logs `draw for seat S`, shows "Player S", "Kept: K", "Passes: P". Log `apply seat S <kind>` and `tap for seat S`. Header comment says what it shows -- the engine's one hidden game whose move keeps the turn.
- [ ] `test/game_core/ManifestTest.cpp` -- `EveryFixtureManifestIsListed` asserts every fixture except `pass-hidden` offers solo; `pass-keep` is pass-only and hidden too, so `folder == "pass-hidden"` becomes `|| folder == "pass-keep"` -- found at implementation (a plan gap; a fork-only file in neither `touches` nor `stays_out`).
- [ ] `test/game_script/fixtures/README.md` -- add `pass-keep/` to the packable list and a table row (what each tap does, the frames, no round end, resume behavior) -- the README lists every fixture.
- [ ] `test/game_script/harness/ResumeMatchTest.cpp` -- two cases and a small `keepMove()` helper in `PassResumeTest` (tap at canvas (100, 100), then `frame()`, then wait for the log `apply seat 1 keep`). Case 1 `AHiddenMatchSavedInPausedAfterAMoveThatKeptTheTurnResumesOnTheBlankWithThatMove`: enter `pass-keep` New, `pumpToPassSave(1)`, `passTheBlank(1)`, refuse the tmp write, `keepMove()`, pump to `cannot write`, Back -> `Paused`, `EXPECT_TRUE(savedPassAt(1))`, clear the refusal, advance the back-off, `pumpToPassSave(2)` with `state() == "Paused"` (the pump runs no other state), record the bytes, `sleep()`, bytes unchanged, then Continue with a solo roster: log "resuming the save's roster: pass, 2 seat(s)", state `HandOff`, no `draw for seat ` before the tap, `passTheBlank(1)`, `holds(lastPush(), "Kept: 1")`. Case 2 `AHiddenMatchSnapshotWrittenMidTurnInPlayingAfterAMoveThatKeptTheTurnResumesOnTheBlankWithThatMove`: same with no pause or refusal: after `keepMove()`, `pumpToPassSave(2)` while `Playing`, then the same sleep and resume asserts. Each case states in a comment which path it covers and why the pump before the sleep is the check (the forced exit would write the snapshot too).
- [ ] `_bmad-output/implementation-artifacts/deferred-work.md` -- mark the `## e5-close` V3 entry resolved, naming entry 8.7, the fixture, and the two case names.

**Acceptance Criteria:**
- Given a hidden pass match on `pass-keep` that paused after a kept move, when the card takes the write only in `Paused`, then the save is on the card before any sleep, and Continue resumes in `HandOff` for seat 1 with "Kept: 1".
- Given the same match not paused, when the kept move commits in `Playing`, then the save is on the card before any sleep, and Continue resumes the same way.
- Given `loopView`'s `flushResume()` removed, then case 1 fails and case 2 does not; given `loopPlaying`'s closing `flushResume()` removed, then case 2 fails (case 1 fails too, because its refused write is only attempted from `Playing`'s flush, as do four existing tests that rely on that flush). Each run on a scratch edit of `src/`, reverted.
- Given every existing resume and match suite, then they pass unchanged.

## Implementation Notes

- Added `test/game_script/fixtures/pass-keep/` (`manifest.json`, `main.lua`) as the plan describes; `README.md` lists it in the packable list and the Games table. `pack_game.py` packs it (964 bytes).
- `ResumeMatchTest.cpp`: `keepMove()` and `resumeOnTheKeptTurn()` helpers in `PassResumeTest` (the second holds the resume asserts both cases share), and the two cases after `AHiddenMatchSleptInResultContinuesAtTheSnapshotTheSleepWrote`.
- `deferred-work.md`: `## e5-close` V3 now reads `Resolved by entry 8.7 (...). It read: ...`.
- Plan gap, fixed minimally: `ManifestTest.EveryFixtureManifestIsListed` (`test/game_core/ManifestTest.cpp`, a fork-only file) asserts every fixture except `pass-hidden` offers solo, so `pass-keep` failed it. Changed its `folder == "pass-hidden"` to `folder == "pass-hidden" || folder == "pass-keep"`; no other assertion changed.
- Verification run: full host suite 1678/1678 after that change; every `scripts/*_test.py`, `check_upstream_touches.py` (PASS), `./bin/clang-format-fix` twice (nothing new). Mutation A (`loopView`'s `flushResume();` deleted): only case 1 fails (at `pumpToPassSave(2)` in Paused). Mutation B (`loopPlaying`'s closing `flushResume();` deleted): case 2 fails, but so does case 1 (its pump to `cannot write` never sees an attempt, since no write is tried in Playing) and four existing pass-open and hidden-turn tests, which also rely on that flush; the acceptance line's "only case 2 fails" does not hold for B. `src/` was reverted with `git checkout -- src` (no change under `src/`).
- Firmware builds, `pio check`, and `sim.sh build` not run, per the brief.
- Review patches: `ManifestTest.cpp`'s comment names `pass-keep` beside `pass-hidden` as having no solo form; `GamePackageInstallerTest.cpp`'s `fixtures[]` gained `{"pass-keep", Manifest::MODE_PASS}`; the README row and the V3 entry say "entry 7" (ticket 8.7) instead of "entry 8.7".

## Plan Change Log

## Review Triage Log

### 2026-10-04 — Review pass
- verdicts: 21 findings — high 0, medium 0, low 17, false 4, maybe-false 0 (four low findings patched, thirteen rejected)
- findings:
  - Blind hunter
    - `[low]` `patch` stale comment above `ManifestTest.EveryFixtureManifestIsListed` still names `pass-hidden` as the only solo-less fixture — real, a direct correction; fix: comment names both.
    - `[low]` `reject` fixture's pass branch is never run by a test — real but the intent asks for two cases; the pass branch is pass-hidden's already-tested mechanism, and a third case is added scope, not a correction.
    - `[low]` `reject` only one keep is tested ("Kept: 2" not shown) — the intent's cases each need one kept move; a second adds no path.
    - `[low]` `reject` y = 200 split and the 20-frame loop are magic numbers — the split is documented in the fixture header and README; `keepMove` taps well inside it (existing `moveToResult` uses the same canvas convention).
    - `[false]` `reject` unknown move kinds fall through to "pass" — the fixture's own `input` is the only move source; no path sends another kind.
    - `[low]` `reject` README says it packs but nothing enforces it / does not say host-only — the enforcement half is the verification-gap finding below (patched); the host-only wording is not wrong, the row says what each tap does.
    - `[low]` `reject` V3's evidence and Trigger lines still read as pending, no commit cited — every other resolved entry in the file keeps its evidence line; the commit does not exist until Finalize.
    - `[low]` `reject` case 2 is not isolated from `loopPlaying`'s flush (mutation B fails case 1 too) — a failure that is wider than its own case, not a vacuous pass; the plan's acceptance criteria record it.
  - Edge-case hunter
    - `[low]` `reject` pass branch never driven — same as the blind hunter's row above.
    - `[low]` `patch` installer test's `fixtures[]` omits `pass-keep` — same root cause as the verification-gap row below.
    - `[low]` `reject` `keepMove` does not assert Playing — case 1 asserts `state() == "Playing"` after it and case 2 asserts it after `pumpToPassSave(2)`.
    - `[low]` `reject` resumed frame checks only "Kept: 1" — a restore mixing two snapshots' fields is unlikely; the `ver 2` log and bytes already pin the snapshot.
    - `[false]` `reject` case 1 could pass without exercising `loopView`'s flush if the back-off elapsed or the pause flushed — refuted: `savedPassAt(1)` is asserted after Back, so nothing wrote at the pause, and mutation A (flush deleted) fails the case.
    - `[false]` `reject` claim: mutation sensitivity unverified — refuted by the run recorded under Verification (mutation A fails case 1 only; B fails case 2).
  - Verification gap
    - `[low]` `patch` `InstallerTest.TheFixtureGamesTheReadmeListsInstallAndCanStart` hard-codes the README's fixture list and lacks `pass-keep`; the README's packs-and-installs claim has no check — add the row `{"pass-keep", MODE_PASS}`.
  - Intent alignment
    - `[low]` `patch` "entry 8.7" in the README row and V3 text — the epic's own numbering is entry 7 (8.7 is the ticket ref); fix: say "entry 7" / "entry 7 of epic-first-party-games (ticket 8.7)".
    - `[false]` `reject` `ManifestTest.cpp` change is outside the intent's list — it is the minimal fix the new fixture forces (recorded in Implementation Notes and the task list); not a defect.
    - `[low]` `reject` the kept move's "state" is checked as rendered text — the snapshot's codec bytes are pinned by the file comparison and `ver 2`; the frame shows the resumed state, which is the intent's "in its state" at the surface a player sees.
    - `[low]` `reject` V3 marked resolved by rewriting `summary` only — the file's convention for resolved entries.
    - `[low]` `reject` the two paths are not isolated from each other — same as the blind hunter's row on mutation B.
    - `[low]` `reject` verify's script and format checks not visible in the diff — not a defect; results recorded under Verification.

## Design Notes

- **New fixture, not a `pass-hidden` change.** The Unknown asks whether `pass-hidden` can gain a keeping move with every test unchanged. It cannot: its moves are one-tap-one-pass, `HiddenPassTest` and `PassResumeTest` count `Moves: N` and assert four-move rounds and ver numbers, and a second move kind would change the tap a test makes. Epic Notes (Decision 2026-10-04) and R11 settle the fixture: "V3's resume cases use entry 7's engine fixture".
- **Why the pump before the sleep is the check.** A forced exit writes the pending snapshot (`MatchPersistence::flushResumeOf` in `onExit`), so a case that only slept and resumed would pass with either loop path removed. The save must be seen on the card while the state is still `Paused` (case 1) or `Playing` (case 2).
- **Why case 1 refuses the first write.** After a kept move in `Playing`, `loopPlaying` writes the snapshot on its next pass, before Back can run. Refusing the write leaves it pending (and backed off), so only `Paused`'s own `flushResume` can write it.
- **Kept turn resumes on the same seat.** `status` still names seat 1, so Continue's hand-off says "Player 1's turn" and the tap shows seat 1's frame with the kept change; `pass-hidden` always resumes on the next seat.
- **No double added.** The cases use the existing screen, SD, and RTOS doubles; nothing new stands in for device behavior. `fakesd`'s `failOpenWrite` is more permissive than a card in that it refuses only the tmp open, as the existing cases already rely on.
- **No function is moved or rewritten.** No guard to preserve.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cd /home/user/epic-first-party-games-lane-b && cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, the two new `PassResumeTest` cases included.
- Mutation A, `src/activities/games/GameMatchActivity.cpp` `loopView`: delete `flushResume();`, rebuild `ResumeHarnessTest`, run `--gtest_filter='PassResumeTest.*'` -- expected: case 1 fails (and no other case does); mutation B, `loopPlaying`'s final `flushResume();` -- expected: case 2 fails (others may too). Revert each with `git checkout -- src`.
- `python3 scripts/<name>_test.py` for every `scripts/*_test.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new in `git status`.
- `python3 scripts/pack_game.py test/game_script/fixtures/pass-keep <scratch>` -- expected: packs.
- No firmware build or `pio check`: the diff changes no file under `src/`, `lib/`, or `platformio.ini`.

**Results (2026-10-04, after the review patches; host machine, local evidence only):**
- Host suites: `cmake --build build/test` and `ctest --test-dir build/test -j` -- 1678 of 1678 passed, the two new `PassResumeTest` cases, `ManifestTest.EveryFixtureManifestIsListed`, and `InstallerTest.TheFixtureGamesTheReadmeListsInstallAndCanStart` (with its new `pass-keep` row) included.
- Mutation A (line 566 of `src/activities/games/GameMatchActivity.cpp`, `loopView`'s `flushResume();` replaced by `(void)0;`): only `AHiddenMatchSavedInPausedAfterAMoveThatKeptTheTurnResumesOnTheBlankWithThatMove` failed. Mutation B (line 546, `loopPlaying`'s closing `flushResume();`): `AHiddenMatchSnapshotWrittenMidTurnInPlayingAfterAMoveThatKeptTheTurnResumesOnTheBlankWithThatMove` failed, with the Paused case and four existing tests (`ANewOpenPass...`, `LeavingAPassMatchKeepsItsPassSave`, `ADeleteTheCardRefuses...`, `AnOpenPassSaveResumesInPlay...`) and others that rely on that flush. Both reverted with `git checkout -- src`; `git status --short src` empty, host suite green again.
- Every `scripts/*_test.py` passed; `python3 scripts/check_upstream_touches.py` PASS; `./bin/clang-format-fix` twice, nothing new in `git status`; `python3 scripts/pack_game.py test/game_script/fixtures/pass-keep <scratch>` packed (964 bytes, hash 0edc6c9bf3d3f4f5).
- No firmware build, `pio check`, or `sim.sh build`: the diff changes nothing under `src/`, `lib/`, or `platformio.ini`. No screenshots (no screen changed).

## Auto Run Result

**Status:** built.

**Summary:** Added the engine fixture `test/game_script/fixtures/pass-keep/` (hidden, two seats, pass only; a tap above y = 200 keeps the turn and adds to `kept`, any other tap passes it) and two `PassResumeTest` cases over it: a hidden pass match saved in `Paused` after a kept move whose write the card refused, and one whose kept-move snapshot was written mid-turn in `Playing`. Each case checks the save on the card while still in that state (before any sleep), then Continue as a solo caller resumes in `HandOff` naming "Player 1's turn" with "Kept: 1" on the seat's frame. `deferred-work.md` `## e5-close` V3 is marked resolved. The new cases pass against the current `src/`; no engine fix was needed.

**Files:**
- `test/game_script/fixtures/pass-keep/main.lua`, `manifest.json` -- the new fixture.
- `test/game_script/fixtures/README.md` -- lists `pass-keep/` as packable and in the Games table.
- `test/game_script/harness/ResumeMatchTest.cpp` -- `keepMove()`, `resumeOnTheKeptTurn()`, and the two cases.
- `test/game_core/ManifestTest.cpp` -- the solo-form rule and its comment name `pass-keep` beside `pass-hidden` (a plan gap found at implementation).
- `test/game_script/harness/GamePackageInstallerTest.cpp` -- `pass-keep` row in the README-fixtures install test (review patch).
- `_bmad-output/implementation-artifacts/deferred-work.md` -- V3 marked resolved.
- This plan.

**Review (thorough, four lenses):** 21 findings, none high or medium, 17 low, 4 false. Patched (4 low): the stale `ManifestTest` comment, the missing installer-test row, and the "entry 8.7" numbering in the README and V3 text (two findings, one root). Deferred: none. Rejected: 13 low and 4 false, each with its reason in the Review Triage Log.

**Formatting:** `./bin/clang-format-fix` changed nothing outside this story's files.

**Verification:** as under Verification, Results.

**Residual risks:** the fixture's pass branch ("Passes: P", `kept` reset) is not driven by any test (rejected as outside the two cases the intent names). The kept move's change is observed as rendered text ("Kept: 1") plus the saved bytes, not as a VM-state read. Removing `loopPlaying`'s flush fails case 1 and four older tests as well as case 2, since all rely on that flush.

**Follow-up review recommended:** false (only low findings were patched).

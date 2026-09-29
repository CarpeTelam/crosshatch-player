---
title: 'resume.bin and resuming a solo match'
type: 'feature'
ticket: '11'
created: '2026-09-29'
baseline_revision: 'ba0c67be46a6ec9ba8ccd0ae2417514d4a40b1af'
status: done
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
context:
  - '/tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/wt/resume/AGENTS.md'
  - '/tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/wt/resume/.skills/heap-discipline/SKILL.md'
  - '/tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/wt/resume/.skills/control-flow-clarity/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A solo match is lost when the device sleeps, and the forced exit's VM join and abandon waits count polls, not time, so sleep's time under `RenderLock` has no bound (epic-script-runtime retro AI-4).

**Approach:** The VM task hands each committed snapshot to the loop task through a latest-wins mailbox; `GameSaveStore` writes `resume.bin` (tmp, then rename) after each one, in the forced exit, and in Leave, and deletes it on entering Over. `peek(id, pkgHash)` says whether a save is usable; `GameMatchActivity(..., Start::Resume)` restores it through `Session::restore`. The waits count `millis()`, and the bound is recorded in `docs/crosshatch/game-canvas.md`.

## Boundaries & Constraints

**Always:** All SD access through `Storage`/`HalFile`, on the loop task only; the VM task never touches `Storage` (AD-5). Allocate with `makeUniqueNoThrow` or `std::nothrow`; locals under 256 B; log with `LOG_*`. Every existing `GameMatchActivity` call site compiles unchanged (a defaulted trailing parameter). No string added, no launcher file changed. Keep every guard in a moved or rewritten function (see Design Notes). `GameVmTest` and `GameMatchTest` (64 tests) pass unedited.

**Never:** Edit `GamesLauncherActivity.*`, `GameModeActivity.*`, the installer, `english.yaml`, `ActivityManager.*`, `GamePaths.h`, `MatchStore.*`, `SoloRounds.*`, `LuaGame.*`, `ISnapshotStore.h`, or entry 1's and entry 4's harness files (`match.cmake`, `CMakeLists.txt`, `stubs/`, `screen_stubs/`, `MatchSupport.h`, `GameVmTest.cpp`, `GameMatchTest.cpp`, the installer suites). Save `pass` or `nearby` matches. Hand-edit a generated file. Take `RenderLock` in `onExit` (12cc816).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Write | solo match, `.pkg` present, a snapshot commits | `resume.bin` = 6 B blob header (`CHRS`, file 1, codec 1), pkgHash[8], mode 0, n 1, ver u16 LE, snapshot; written on the next loop pass through `.tmp` and rename | A failed write keeps the snapshot pending; the loop retries after `FLUSH_INTERVAL_MS`, the forced exit at once |
| Peek | valid save, same hash and codec | true | none |
| Discard | other hash, codec version, file version, magic, mode or n, empty or oversize snapshot, truncated | false, one log line, file kept | none |
| Over | round ends (also while Paused) | `resume.bin` and its tmp deleted; no snapshot with `over` set is ever written | delete failure logged |
| Forced exit | sleep in Playing or Paused, snapshot pending | stop, then the pending snapshot is written, then the store flushed; none of it takes `RenderLock` | write failure logged; exit continues |
| Waits | VM stuck | join and abandon each end 500 ms of `millis()` after they began, plus at most one poll | none |
| Resume | `Start::Resume`, valid save | game continues from the snapshot at its ver; `setup` does not run; no redundant rewrite | invalid or unreadable save, or no `.pkg`: logged, a new match starts |
| Play again | after Over | the new round's snapshots are written again | none |

</frozen-after-approval>

## Code Map

- `src/games/GameSaveStore.{h,cpp}` -- add `RESUME_MAGIC` "CHRS", `RESUME_FILE_VERSION` 1, `PACKAGE_HASH_BYTES` 8, static `peek`, `setPackageHash`, `loadResume`, `saveResume`, `deleteResume`, `flushResume`. Model on `saveStore`/`readValid`; leave those untouched. Must build in `GameSaveStoreTest` (no `GameHash.h`, no `Memory.h`).
- `src/games/SnapshotMailbox.h` (new, header-only) -- mutex-guarded latest-wins slot over caller-owned storage: `publish`, `take`, `pending`, `markPending`, `storage()`.
- `lib/GameCore/Session.{h,cpp}` -- add `restore(snapshot, ver)`; the next `start()` keeps it, skips `setup`, and runs `afterSnapshot`. `SoloRounds` stays unchanged: its `restart()` calls `start()` again, which runs `setup`.
- `src/games/GameVM.{h,cpp}` -- `join` and `abandon` count `millis()`; `create` allocates `SNAPSHOT_BYTES` more in the frame block for the mailbox; `setResume`; `committed()`; `run()` calls `restore` before `rounds.start` and publishes after each `Ok` or `Cancelled` `start`/`restart`/`step` whose ver changed (before `logRound`).
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `enum class Start { New, Resume }` as a defaulted fourth constructor parameter; read the hash with `GameRegistry::readPackageHash`; `flushResume(force)` from `loopPlaying`, `loopView` (Paused), and `stopVm` (after the wait, before the VM is freed); delete at Over; `resumeWritable` set on Playing/Paused, cleared on Over/Error.
- `docs/crosshatch/formats.md` -- `## resume.bin`. `docs/crosshatch/game-canvas.md` -- Solo states, Leaving, and The forced exit (the bound).
- Tests: `test/game_script/GameSaveStoreTest.cpp`; new `harness/resume.cmake`, `harness/resume.sources.cmake` (adds `Manifest.cpp` and `StreamingJsonParser.cpp` to `HARNESS_EXTRA_CORE_SOURCES` so `GameMatchHarnessTest` still links `GameRegistry.o`), `harness/ResumeSessionTest.cpp`, `harness/ResumeMatchTest.cpp` (its own fixture; the doubles' surface is in entry 4's plan handoff; fake clock moves with `vTaskDelay`, `delay`, timed-out notify).
- Reuse: `writeBlobHeader`/`checkBlobHeader`, `Codec::check`, `GamePkg::formatPkg` (test `.pkg`), `fakertos::arm/advance`, `match::ONE_TAP_GAME`, `installFixture`.

## Tasks & Acceptance

**Execution:**
- [ ] `src/games/SnapshotMailbox.h`, `lib/GameCore/Session.*` -- add the mailbox and `restore` -- the VM-to-loop hand-off and the restore point
- [ ] `src/games/GameSaveStore.*` -- resume API -- format, peek, atomic write, delete, flush
- [ ] `src/games/GameVM.*` -- waits by `millis()`, `setResume`, `committed()`, publish, restore -- R11 and the VM half of resume
- [ ] `src/activities/games/GameMatchActivity.*` -- `Start`, hash, flush points, Over delete, `stopVm` write, comments on the bound -- the match half
- [ ] `docs/crosshatch/formats.md`, `game-canvas.md` -- the format and the bound
- [ ] Tests -- GameSaveStoreTest (round trip, exact bytes, each discard, tmp-only read, failure keeps the old save, delete, flush and retry throttle, `over` deletes, ver truncation); ResumeSessionTest (restore, no setup, ver kept, rematch runs setup, bad sizes, mailbox latest-wins and `markPending`); ResumeMatchTest (waits under a ticker thread that advances the fake clock: elapsed-time counting; bound arithmetic for a stuck-in-locked-binding VM; write on loop pass; no `.pkg` writes nothing; Over deletes; forced-exit write proven by a loop write failed and throttled beforehand; Leave writes; sleep then `Start::Resume` shows the same snapshot, `setup` not run, file unchanged, next tap advances ver; mismatch starts a new match)

**Acceptance Criteria:**
- Given a solo match with a `.pkg`, when a move commits and the device sleeps, then `resume.bin` holds that snapshot and `peek` is true, and `Start::Resume` continues from it.
- Given a stuck VM, when the forced exit runs, then its waits end within 1,000 ms of `millis()` plus at most one poll each.
- Given the round ended, when Over is entered, then no `resume.bin` remains and `peek` is false.

## Implementation Notes

Full route (about 700 lines across 12 files).

Work only in the git worktree `/tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/wt/resume` (its own branch; never the main checkout `/home/user/crosshatch-player`). Every shell call starts in another directory: `cd` there, or use absolute paths. `git submodule update --init --recursive` is done. Every `pio`, `sim.sh`, and host-test CMake configure or build runs inside `flock /tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/build.lock sh -c '<whole chain>'`. Do not commit, push, or run `tickets.py mark`. Run `./bin/clang-format-fix` (no arguments) after your last edit. Scratch files go under `/tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/4.11/`. No model names in code or docs. If a double lacks something a test needs, stop and report it rather than editing the double.

## Plan Change Log

- 2026-09-29, orchestrator's independent review (#24): the frozen matrix says the forced exit writes a failed resume write "at once". Changed by the orchestrator's decision: a write that failed less than `FLUSH_INTERVAL_MS` before is not retried at Leave or the forced exit, and the forced exit's SD steps start only inside a 1,500 ms deadline from the start of `onExit()`. Avoids: a card that just failed getting a full retry, and the store flush and delete retry, under `RenderLock` during sleep. KEEP: the resume write comes first among the SD steps, before an abandon frees the mailbox's memory.
- Orchestrator-approved (reversible within the PR): `GameVM.*` beyond the waits (the snapshot hand-off, `setResume`, `committed()`, the `abandon` hook) and the new `src/games/SnapshotMailbox.h`.
- Assumption for entry 14: the forced exit's SD work is bounded by a deadline of 1,500 ms from the start of `onExit()` (`GameMatchActivity::FORCED_EXIT_DEADLINE_MS`). The resume write, the `resume.bin` delete retry, and the `ch.store` flush each start only inside it; past it each is skipped and logged (`forced exit past 1500 ms; skipped ...`), so a slow card can lose the last move or the last `ch.store` write rather than hold `RenderLock`. Inside it the total is the VM waits (about 1,030 ms on a device whose polls run on time) plus the SD steps that started before the deadline, each one tmp write and rename at the card's speed. The owner confirms the figure on the device (AI-3).

## Review Triage Log

Pass 1. All four lenses (blind hunter, edge-case hunter, verification gap, intent alignment) ran as context-free subagents over `git diff ba0c67be` and all returned; the implementation subagent was re-engaged for the patches. Counts: 0 high, 3 medium, 12 low, 6 false, 0 maybe-false. Nothing routed to intent_gap or bad_plan.

| # | Finding (lens) | Verdict | Route | Evidence / action |
|---|---|---|---|---|
| 1 | `handle(Over)` ignores a failed `deleteResume()`; `resumeWritable=false` makes the retry unreachable (BH, EC) | medium | patch | Real: a remove failure leaves the pre-final-move file and Continue resumes a finished round. `resumeDeletePending`, retried in loopView and stopVm; match test with an injected failure. |
| 2 | `peek` reads the header only, `loadResume` also runs `Codec::check`; Continue can start a new match (BH, EC, IA) | medium | patch | Real, rare (atomic writes make a bad payload need bit rot). `peek` now runs the same validation; the test that pinned the mismatch flips. |
| 3 | Cancelled-step publish untested (VG) | medium | patch | Verified: dropping `\|\| Cancelled` passes every test. New VM test. |
| 4 | Error never writes untested (VG) | low | patch | Verified by mutation. New match test. |
| 5 | Docs say no `.pkg` means no read or write, but Over deletes unconditionally (BH) | low | patch | Doc reworded: writes nothing, Over still removes a stale file. |
| 6 | `SNAPSHOT_BYTES` vs `Codec::SNAPSHOT_LIMIT`, store buffer vs snapshot: no assert (BH, EC) | low | patch | `LuaGame.cpp` already ties the first pair; added the second pair and the buffer bound in `GameSaveStore.cpp`. Silent `take` failure is unreachable after the asserts. |
| 7 | Ticker tests assert `elapsed < 1500`, can flake under load (BH) | low | patch | Widened to 6,000; a poll-counting wait measures about 12,500 there. |
| 8 | Forced-exit doc order (join, write, abandon) inconsistent with the code (EC) | low | patch | The write sits before the abandon because the abandon frees the mailbox's memory; doc states it. |
| 9 | Doc reason list omits `cannot open`; 1,030 ms assumes a 1 kHz tick; misplaced comment in `onEnter`; uneven `GameVM.h` comment (BH, EC) | low | patch | Direct corrections. |
| 10 | Docs overclaim: nothing calls `peek` or `Start::Resume` (BH, VG, IA) | low | patch | Intended: entry 12 wires them (`tickets.toml`, `stays_out`). Docs now say so; deferred entry for entry 12. |
| 11 | A save the game itself refuses traps the user in Error (BH, EC) | low | reject | Needs a game that rejects a state it committed under the same package hash and codec. The fix adds a failure-kind branch, and deleting on a transient out-of-memory would lose a good save. Noted under `## 4.11` for entry 12. |
| 12 | No throttle after a successful write; SD wear and loop stall (BH) | low | reject | R10 asks for a write after every committed snapshot; coalescing to one per loop pass is already the mitigation. Device cost unmeasured; noted under `## 4.11`. |
| 13 | Watchdog stop (`stopStuckVm`) drops the pending snapshot (EC) | low | reject | By design: Error writes nothing, the older loop-written file remains valid. |
| 14 | A New match over a valid save replaces it with its first snapshot (EC) | low | reject | One save slot per game; R10 writes after every committed snapshot. |
| 15 | Sleep's total `RenderLock` time still has unbounded SD writes (IA) | low | reject | Descriptive; the ticket's R11 scopes the bound to the VM waits, and the docs say the writes are not bounded. |
| 16 | `deleteResume` removes the tmp before the file, so a crash between leaves a stale file (BH) | false | none | A crash before the delete completes always leaves the file; the delete has no earlier state to protect. |
| 17 | Unvalidated tmp promoted to `resume.bin` on a first-ever torn write (EC) | false | none | The new write replaces it at once; a failed one leaves a file both `peek` (after #2) and load refuse. |
| 18 | `restore()` refused branch overwrites a save; publish/take size refusals silent (EC) | false | none | Unreachable: `setResume` checks the same limit and the asserts in #6 tie the sizes. |
| 19 | `formats.md` says "loop task only" but `peek` is static (BH) | false | none | The launcher runs on the loop task. |
| 20 | `docs/file-formats.md` lacks the `CHRS` entry (BH) | false | none | That file is the upstream cache-format history; the fork's game formats live in `docs/crosshatch/formats.md`, which the ticket names. |
| 21 | `JoinEnds...` asserts `<= 510` on a shared clock (BH) | false | none | That test runs no ticker and no other thread that moves the fake clock. |
| 22 | Intent alignment: the diff is the engine layer (R-A with R-D); the product path needs entry 12 (IA) | n/a | none | Descriptive, matches the ticket: "adds no string and changes no launcher file". |


Pass 2, source: the orchestrator's independent review (adversarial, edge-case, and verification-gap lenses, run by the orchestrator over `ba0c67be..68ec417b`; file `scratchpad/4.11/review-all.md`). Counts: 0 high, 3 medium, 6 low, 0 false. All accepted as patches in one follow-up commit; the orchestrator also approved the two out-of-`touches` changes (`GameVM.*` beyond the waits, and the new `src/games/SnapshotMailbox.h`) and the change to the matrix's forced-write row (see the Plan Change Log).

| # | Finding (lens) | Verdict | Route | Evidence / action |
|---|---|---|---|---|
| 23 | A Cancelled step is published though its status or draw never finished; `over` is the previous snapshot's (adversarial + edge 1) | medium | patch | Verified: `LuaGame` writes the status only on success. `Session::settledVer()`; Ok outcomes publish, Cancelled ones only when `settledVer == ver`. Tests A (cancel in status), B (cancel in draw), C (restart cancelled after setup). The deferred entry's claim is corrected. |
| 24 | The forced exit's SD work is unbounded, and `force` retries a write that just failed (adversarial + edge 2) | medium | patch | Reverses #15 (rejected on R11 alone; AD-17's amendment and the epic decision say "within the forced exit's bounded time"). `FORCED_EXIT_DEADLINE_MS` 1,500; resume write first; no forced retry inside `FLUSH_INTERVAL_MS`. |
| 25 | "Late by at most one poll" cannot fail: every tested timeout is a multiple of the poll (verification gap 1) | medium | patch | Mutation (poll 20, 100, 250) survived. `STOP_POLL_MS` public and `static_assert`ed; join tests with 503 and 507. |
| 26 | A VM that finishes within abandon's wait is deleted with its last publish unflushed (edge 3) | low | patch | Defaulted `beforeDelete` hook on `GameVM::abandon`; test. |
| 27 | A failed Over delete is never retried after a stuck VM takes the match to Error, on Leave or the forced exit (edge 4) | low | patch | Retry moved out of `stopVm` into `onExit` and `leave()`. |
| 28 | The delete retry has no throttle (edge 5) | low | patch | Throttled to `FLUSH_INTERVAL_MS` in `loopView`; test counts removes. |
| 29 | The documented bound reads "however slowly the polls run" (adversarial 6) | low | patch | Restated: 500 + 500 ms, each late by its last iteration, teardown uncounted. |
| 30 | `peek`'s out-of-memory branch never runs (verification gap 2) | low | patch | A replaceable nothrow `operator new[]` in `GameSaveStoreTest`; test. |
| 31 | No recorded evidence for `check_upstream_touches.py` and `clang-format-fix` (verification gap 3) | low | patch | Recorded under Verification. |

## Design Notes

- **Scope beyond the ticket's `(the waits)`.** The ticket's verify needs the VM to publish committed snapshots and start from one, and no file in `touches` can do that without `GameVM.*`. The edit is confined to that hand-off; `SoloRounds`, `LuaGame`, and `MatchStore` stay untouched. `SnapshotMailbox.h` is a new fork-only file.
- **`Session::restore` answers the ticket's unknown:** a `GameCore` change inside `Session` only. `restore` copies the snapshot into `state`, sets `version`, and marks the next `start()` to skip `setup`. `start()` clears the mark, so Play again runs `setup`.
- **Guards kept** (`git log -L`): `join`'s `if (!task) return true` (never started, nothing to wait for) and its finished-before-timeout order; `abandon`'s `if (!vm) return true`, the `finished()` check before any delete, `deleteIfStuckInLua`'s conditions, the SIMULATOR branch that waits nothing, and the leak; `stopVm`'s `if (!vm) return` (the VM is gone after a user exit). The waits keep `STOP_POLL_MS` polls and change only what ends them.
- **The bound** (record in game-canvas.md): join ends `STOP_TIMEOUT_MS` (500) of `millis()` after it began, abandon `ABANDON_WAIT_MS` (500) after it began, each late by at most one iteration (5 ms delay, plus abandon's settle of up to 10 ticks). About 1,030 ms worst case for the VM, plus the store and resume writes, each one tmp write and rename on the card, whose time the code does not bound.
- **Save policy.** The snapshot is written coalesced (latest wins) once per loop pass, so "after every committed snapshot" means every one a pass sees. Leave keeps the save (only Over deletes, per R10). A save is never written for a status-over snapshot; if one is the latest pending, the file is deleted instead, so a round that ended while Paused or at sleep does not resurrect. A failed resume start leaves the file (a transient out of memory must not delete a save). Mode is 0 and n is 1; `peek` and load refuse any other, so a Continue row always resumes. `ver` is the low 16 bits (the spine's `u16`).
- **Order in `run()`**: publish before `logRound`, so a test waiting for "Round started" knows the snapshot is in the mailbox. A Cancelled step publishes only when the status was computed for its ver (`Session::settledVer`); one cancelled during status is not published (review pass 2). The resume seed shares the mailbox storage; it is read by `restore` before the first publish.
- **Tests shadow** `GameVmTest`/`GameMatchTest`: their fixtures sit in anonymous namespaces, so `ResumeMatchTest.cpp` carries its own small copy.

## Verification

**Commands:** (each under `flock /tmp/claude-0/-home-user-crosshatch-player/471327d7-87f0-5189-8dae-dd3795e54a6e/scratchpad/build.lock sh -c '...'`)
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, `GameVmTest`/`GameMatchTest` unedited
- `pio run -e x4pro` and `pio run -e sticky`; `pio run -e default` -- expected: build
- `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- expected: passes
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py` -- expected: pass
- `./bin/clang-format-fix` twice -- expected: nothing new in `git status`

**Results** (worktree of branch `epic4/resume`, base ba0c67be, each run under the shared build lock, after the review patches):
- Host suites: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- 1119 of 1119 passed, including `GameSaveStoreTest` (41), `GameMatchHarnessTest` (64, unedited) and `ResumeHarnessTest` (41: Session restore, the mailbox, the waits, the match flows).
- Mutation checks by the implementer (each failed the intended tests, then reverted): waits back to poll counts, the `stopVm` flush removed, the Over delete removed, `over` snapshots written, `restore` not skipping `setup`, no published-ver seed after a restore, the retry throttle removed, `force` ignored, `markPending` removed, publishing removed, `Cancelled` dropped from the step publish, Error made writable, the delete retry removed. `ResumeHarnessTest` also passed 25 `--gtest_repeat` runs.
- `pio run -e x4pro`, `-e sticky`, `-e default` -- all succeeded (x4pro: RAM 31.1 %, flash 89.9 %; unmeasured as a delta: no base build was made).
- `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- passed.
- `python3 scripts/check_layers.py` -- passed. `python3 scripts/check_upstream_touches.py` -- see the commit.
- The lenses ran as context-free subagents (Review Triage Log). No screenshots: the story changes no view.

**Results after the orchestrator's review (follow-up commit)**, each run under the shared build lock, `.cache/` deleted under it afterwards:
- Host suites, the same command as above -- 1133 of 1133 passed (`ResumeHarnessTest` 55, `GameSaveStoreTest` 41, `GameMatchHarnessTest` 64 unedited; `GameVmTest.cpp` and `GameMatchTest.cpp` not edited).
- Mutation checks by the implementer, each failed the intended tests, then reverted: unsettled publish, no abandon hook, no deadline, no delete throttle, store flush before the resume write, `settled` always set.
- `pio run -e x4pro`, `-e sticky`, `-e default` -- succeeded. `sim.sh build x4pro` -- passed.
- `python3 scripts/check_layers.py` -- passed.
- `./bin/clang-format-fix` twice -- exit 0 both times; `git status` after the second run showed only the intended changes (nothing new).
- `python3 scripts/check_upstream_touches.py` -- run on the follow-up commit; PASS (the upstream-changed paths list is unchanged from the first commit; no file of this story is in `upstream/develop`).

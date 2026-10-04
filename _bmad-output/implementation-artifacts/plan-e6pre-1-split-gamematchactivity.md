---
title: 'e6pre-1 split GameMatchActivity into persistence, resume-seed, and view units'
type: 'refactor'
ticket: ''
created: '2026-10-04'
baseline_revision: '1a094c94f4ccad7f35f80515ddfc02e63fc20a17'
status: 'done'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
context:
  - '{project-root}/docs/crosshatch/game-canvas.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GameMatchActivity` (1,125 lines `.cpp`, 360 `.h`) carries match load/resume, lifecycle, the playing loop, canvas render, view building, snapshot persistence, and exit/abandon (epic 5 retro G1, AI-10). Epic play-nearby adds a second local-seat path, and four builds that follow (late timer after over, Play-again-gap pause menu, forced-exit order, SoloRounds rename) each land in one of these concerns.

**Approach:** Behaviour-preserving extraction of three cohesive units into new fork-only files: snapshot persistence (`MatchPersistence`), the resume-save seed (`MatchResume`), and view building (`GameMatchView`). The activity keeps the lifecycle, loops, render orchestration, and VM stop/abandon, delegating to them. Every existing host test stays green and unedited.

## Boundaries & Constraints

**Always:** Move code verbatim (comments included); keep every guard and early return, with its reason comment. Same call order, same `millis()` read points (via an injected clock, not a parameter captured earlier). New files inside `#if FREEINK_CAP_GAMES`. Layer rules (`scripts/check_layers.py`): `src/games` files include no screen/UI code; `GameMatchView` is in `src/activities/games`. `GameMatchActivity::FORCED_EXIT_DEADLINE_MS` stays (tests use it). Allocation, locals under 256 bytes, `LOG_*` rules as AGENTS.md.

**Never:** Change behaviour, log text, or timing. Fix any of the following builds' behaviours (late timer, gap pause menu, forced-exit order, SoloRounds). Edit an upstream file beyond ledger rows, a test's expectations, `.skills/`, or the SDK pointer. Touch `docs/file-formats.md` or `SECTION_FILE_VERSION` (no layout change).

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.{h,cpp}` -- the class being split; harness builds `.cpp` unchanged over doubles (`test/game_script/harness/match.cmake`).
- `src/games/MatchStore.{h,cpp}`, `src/games/GameSaveStore.h` (`flushResume(SnapshotMailbox&, nowMs)`, `deleteResume`, `resumeReplacements`, `clearResumeBackoff`, `loadResume`, `peekResume`, `FLUSH_INTERVAL_MS`) -- what persistence and seed call; `src/games/SnapshotMailbox.h` (`pending()`).
- `lib/GameCore/Manifest.h`, `HostCaps.h`, `Roster.h`, `MatchLifecycle.h` -- seed inputs/outputs; `src/games/GameHostCaps.h` `gameHostCaps()`.
- `test/game_script/harness/harness_base.sources.cmake` and `match.cmake` line 51 -- globbed source lists; a screen-doubles-only file is excluded from the shared libs and re-added in `match.cmake`, as `GameSplashLayout.cpp` is.
- `test/game_script/harness/resume.cmake` (`ResumeHarnessTest`, links `game_match_src`) -- where a new unit test joins.
- Tests that must stay green unedited: `GameMatchTest.cpp`, `ResumeMatchTest.cpp`, `PassMatchTest` (in ResumeMatchTest), `CopiedConstantsTest.cpp`, `CallGuardTest.cpp`.

## Tasks & Acceptance

**Execution:**
- [ ] `src/games/MatchPersistence.{h,cpp}` -- NEW. `class MatchPersistence` owns `resumeWritable`, `resumeDeletePending`, `resumeDeleteTriedMs`, `forcedExit`, `forcedExitBeganMs`, and `FORCED_EXIT_DEADLINE_MS = 1500`. `using Clock = uint32_t (*)()`; `bind(MatchStore&, const char* gameId, Clock)`; `setWritable/writable`, `beginForcedExit()` (reads the clock), `forcedExit()`, `deletePending()`, `sdStepAllowed(what)`, `flushResumeOf(SnapshotMailbox&)`, `retryResumeDelete(forced)`, `onOver()` (`pending = !deleteResume(); tried = clock()` read after the delete, as now), `flushStore()`. Bodies are today's `sdStepAllowed`, `flushResumeOf`, `retryResumeDelete`, `flushStore`, moved verbatim, `millis()` becoming `clock()`.
- [ ] `src/games/MatchResume.{h,cpp}` -- NEW. `ResumeSeed seedResume(GameSaveStore&, const Manifest&, const HostCaps&)` returning `{Outcome New|Resume|Refused, Refusal CannotRead|NotHere, snapshot, version, roster}`: today's `seedResume` body, minus the `roster`/`lifecycle` assignment (the caller does it) and with its log lines unchanged (the "resuming the save's roster" log stays in the function, after `Outcome::Resume` is decided).
- [ ] `src/activities/games/GameMatchView.{h,cpp}` -- NEW. `class GameMatchView` owns `dialogProps`, `bannerProps`, `bannerText`, `menuProps`, `ACTION_OPTION`, `ACTION_PASS`, `MAX_OPTIONS`, and today's `buildView`, `buildHandOffView`, `drawViewIcons`, `viewHeadline`, `optionLabel`, `labelIsBlack`; `void build(UiScreen&, GfxRenderer&, const Input&)` with `Input{state, title, focused, errorHeadline, errorDetail, pauseInGap, passTo, handOffSeat}`, filled by `viewScreen` at the callback time (the reads of `viewState`, `selected`, `passTo`, `handOffSeat`, and `state == Paused && pauseInGap()` happen where they do now).
- [ ] `src/activities/games/GameMatchActivity.{h,cpp}` -- delegate: members `MatchPersistence persistence`, `GameMatchView views`; `flushResume`, `flushResumeOf`, `retryResumeDelete`, `flushStore` become one-line forwarders (the `abandonVm` hook and every call site keep their names); `onEnter` calls `seedResume` and maps `Refusal` to `STR_GAMES_RESUME_FAILED` / `STR_GAMES_RESUME_NOT_HERE`; the file's static_asserts and `FORCED_EXIT_DEADLINE_MS` alias stay; update header comments that name moved members.
- [ ] `test/game_script/harness/harness_base.sources.cmake`, `match.cmake` -- exclude `GameMatchView.cpp` from the shared libs and re-add it to `game_match_src`.
- [ ] `test/game_script/harness/MatchPersistenceTest.cpp` + `resume.cmake` -- NEW host test, fake clock: `sdStepAllowed` is true outside a forced exit, true at 1,499 ms, false at 1,500 ms and logs once per refusal; `flushResumeOf` and `flushStore` do nothing before `bind`/when the store is not ready or not writable. Plus `seedResume` tests only if reachable with the existing `GameSaveStore` fixtures in `ResumeMatchTest.cpp` without new doubles (else defer).
- [ ] `docs/crosshatch/upstream-touches.md` -- ledger row only if `check_upstream_touches.py` demands one.

**Acceptance Criteria:**
- Given the unchanged test sources, when the host suite runs, then every test that passed at baseline `1a094c94` passes.
- Given the new `MatchPersistence` deadline test, when it runs, then it fails if the `>=` comparison or the clock read is altered.
- Given the three new units, when `check_layers.py` runs, then it passes; `GameMatchActivity.cpp` is shorter by roughly the moved bodies.

## Implementation Notes

Deviations: persistence returns early when unbound or with a null clock (needed for the unbound test; the activity always binds). `viewScreen` reads selected/passTo for every state. No `seedResume` unit test (needs new doubles): deferred. `GameMatchView::build` takes `const GfxRenderer&`.

## Plan Change Log

## Review Triage Log

Lenses ran as context-free subagents and all returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. Verdicts: 0 high, 0 medium, 5 low, rest false/deferred. Patches applied: 2 rows.

| Finding | Verdict / route | Evidence |
|---|---|---|
| Null Clock: retry/onOver/flushResumeOf/flushStore call `clock()` unguarded (blind, edge) | low / patch | Unreachable (the activity always binds a non-null clock) but inconsistent with `sdStepAllowed`; added `!clock` to each guard. |
| Stale `buildView` name in GameMatchView.cpp comment (edge) | low / patch | Comment now says `build()`. |
| `ResumeSeed::roster` comment says solo (blind) | false | `Roster` defaults to `Mode::Solo`; comment reworded only. |
| Tests miss back-off, onOver failure, replacement-clears-pending, successful write, writable=false with pending snapshot (blind, edge) | low / defer | Those paths are covered through the real activity (ResumeMatchTest 650, 671, 833-885; verification-gap lens confirmed); a direct unit needs a ready MatchStore with an SD double. |
| `seedResume` and `GameMatchView` have no direct tests (blind, edge, gap, intent) | low / defer | Covered end to end by ResumeMatchTest 1559-1610, ModePickerTest, and the render/tap tests now compiled with the view; direct tests need GameSaveStore doubles. |
| `viewScreen` reads selected/passTo/handOffSeat for every state (edge) | false | Atomic loads, no side effects, still at callback time. |
| `bind` stores `manifest.id` pointer (blind) | false | `manifest` is a by-value member of a non-copyable Activity, never moved. |
| Unused accessors `writable()`/`deletePending()`; forwarders; alias `FORCED_EXIT_DEADLINE_MS` (blind) | false | Alias is used by tests (spec'd); forwarders keep names for the follow-on builds; accessors are the seam for them. |
| `handle()` sets writable in seven places (blind) | false | Verbatim from the original; a state-to-flag mapping would be a behaviour-neutral rewrite beyond this refactor. |
| game-canvas.md not updated (blind) | false | No documented behaviour changed. |
| Log order of "resuming the save's roster" (blind) | false | Cosmetic, noted in Design Notes; no failure path between. |
| Gap/verification: none | - | verification-gap lens: no gaps. |

## Design Notes

Guards carried over (`git log -L` read for `flushResumeOf` 68ec417b, d45dd71d, b976e765, c147fb1c; `retryResumeDelete`/`sdStepAllowed` 68ec417b, d45dd71d; `flushStore` d45dd71d; `seedResume` 68ec417b, b964078a, 17d02483; view code 17d02483, 491486bf). Each stays with its comment; what each protects:

- `flushResumeOf`: `!resumeWritable` (Over and Error never write resume.bin; a finished round must not be saved), `!store.ready()` (match never allocated), `!committed().pending()` (nothing new), `sdStepAllowed` (forced-exit deadline), and the `resumeReplacements()` before/after comparison that clears `resumeDeletePending` only when a snapshot actually replaced or removed the old file (otherwise a retry would delete the new round's save).
- `retryResumeDelete`: `!resumeDeletePending || !store.ready()`; the `FLUSH_INTERVAL_MS` back-off unless forced; the deadline; the tried-time update only on failure.
- `sdStepAllowed`: true outside a forced exit; compares `millis() - began` unsigned (wrap-safe).
- `flushStore`: `!store.ready()`, `!slot.dirty()`, deadline; safe with a leaked VM task (slot mutex held only for a copy).
- `seedResume`: unreadable save keeps the file (no new match over it); `peekResume` `Unstartable` keeps it (`NotHere`); `Unreadable`/`Valid` on the second look keeps it; only `None` starts new. The lifecycle swap stays in the caller, before any event is applied.
- View building: the `shown`/`viewState` re-read at callback time, the `pauseInGap` message only in Paused, and `MAX_OPTIONS` clamp.
- The thin forwarders keep `flushResume()` null-VM check (`if (vm)`).

Seams for the following builds: late timer after over -> `loopPlaying`/`GameVM` (untouched); gap pause menu -> `GameMatchView::Input::pauseInGap` + `pauseInGap()`; forced-exit order -> `onExit`/`stopVm` + `MatchPersistence` (`beginForcedExit`, `sdStepAllowed`); SoloRounds -> `lib/GameScript` (untouched).

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, test count = baseline + new tests.
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` (twice) -- expected: clean.
- After review (once): `pio run -e x4pro`, `pio run -e default`, `sim.sh build x4pro`, `pio check` (default, and `-e x4pro`) under the build lock -- expected: success, no new defects.

**Results (worktree tree, incremental, after the review patches):** host ctest 1656/1656 (baseline 1649 at 1a094c94 plus 7 new); `check_layers.py` and `check_upstream_touches.py` pass (no ledger row needed); `./bin/clang-format-fix` twice, nothing outside my files; `pio run -e x4pro` and `-e default` succeed; `pio check` (default and `-e x4pro`, fail-on low/medium/high) exit 0; `sim.sh build x4pro` succeeds. No screenshots (behaviour-preserving; no CI gate changed). `GameMatchActivity.cpp` 1,125 -> 873 lines, `.h` 360 -> 314.

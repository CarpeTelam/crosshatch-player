---
title: 'e6pre-11 title screen writes prefs on exit'
type: 'fix'
ticket: ''
created: '2026-10-04'
baseline_revision: '71cacd39'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter','edge-case-hunter','verification-gap','intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/docs/crosshatch/formats.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A change made on a game's Options screen is not remembered when Options is left by a Replace (the Home gesture, sleep) rather than Back: `ActivityManager` runs the result handler only on a Pop, and AD-17 forbade writing `prefs.bin` from `onExit()` (deferred-work `## 5.12`).

**Approach:** Owner decision 2026-10-04: the title screen (`GameModeActivity`) writes `prefs.bin` from its own `onExit()` when an Options change is still unwritten (AD-17 amended on branch claude/pre-e6-lane-b, d8feb90c, as a target until this build).

## Boundaries & Constraints

**Always:** Take no `RenderLock` in `onExit()` (the manager holds it). All SD access through `GameSaveStore::savePrefs` (`Storage`/`HalFile`). Write once: not when Back already wrote it, not when nothing changed. A failed write is logged (`LOG_ERR`) and never blocks the exit.

**Never:** Edit the spine, `.skills/`, upstream files, or make the match write `prefs.bin`.

</frozen-after-approval>

## Code Map

- `src/activities/games/GameModeActivity.{h,cpp}` -- new `onExit()` override; `rememberChoices()` (existing: settings-unread guard, Unreadable re-read, `savePrefs`) is reused; `optionsChanged` (set by Options, cleared by `onOptionsClosed`) is the dirty flag.
- `src/activities/ActivityManager.cpp` :188-197, :225-231 (read only) -- Replace exits the current screen, then each stacked screen's `onExit()`, RenderLock held, loop task.
- `src/games/GameSaveStore.{h,cpp}` -- `savePrefs` writes `.tmp` then renames; comments about "never onExit()" reworded.
- `test/game_script/harness/ModePickerTest.cpp` -- tests; `docs/crosshatch/formats.md` -- prefs.bin text; `deferred-work.md` -- entry marked decided.

## Tasks & Acceptance

**Execution:**
- [x] `GameModeActivity.cpp/.h` -- `onExit()`: if `optionsChanged`, clear it, `rememberChoices()`, then base `onExit()`.
- [x] `ModePickerTest.cpp` -- Options change then Replace persists; Back writes once (no second write at exit); no change writes nothing; no Options opened writes nothing; failed write logged, exit goes on.
- [x] docs, comments, deferred-work.

**Acceptance Criteria:**
- Given an Options change, when a Replace exits Options and the title screen, then prefs.bin holds the change and the next title screen shows it.
- Given Back wrote the change, when the screen later exits, then no further write happens.
- Given no change, when the screen exits, then nothing is written.
- Given a failing write, when the screen exits, then `LOG_ERR` names it and the exit completes.

## Implementation Notes

Design: no new dirty flag; `optionsChanged` already means "an edit the handler has not seen" (Back's handler clears it, even on a failed write, so `onExit()` does not retry a write Back already tried). `onExit()` clears it before writing, so it runs once. Storage serialization: `savePrefs` goes through `Storage`, which has its own mutex distinct from the render mutex (the match's `onExit()` already writes `resume.bin` under RenderLock). A write costs a few ms (one small file) against the forced exit of a title screen, which has no deadline. Unchanged guards of `rememberChoices` (settings unread: skip; Unreadable: re-read and merge) apply to the `onExit()` path too.

## Verification

Host: `ctest --test-dir build/test -j` 1676/1676 after the change (+2 review tests for the Unreadable re-read and unread-settings guards; tests: `OptionsChangedThenLeftByAReplacePersistsTheChange`, `ABackThatWroteTheChangeIsNotWrittenAgainWhenTheScreenExits`, `OptionsLeftByAReplaceWithNoChangeWritesNothing`, `ATitleScreenLeftByAReplaceWithNoOptionsOpenedWritesNothing`, `AFailedPrefsWriteInOnExitIsLoggedAndTheExitGoesOn`; the old `OptionsLeftByAReplaceWritesNothing` is replaced). Firmware (all exit 0): `pio run -e x4pro`, `pio run -e default`, `pio check` default and x4pro with --fail-on-defect low/medium/high, `sim.sh build x4pro` PASS; check_upstream_touches PASS. Device Replace-over-Options not run.

## Review Triage Log

Lenses ran as context-free subagents and all four returned. No high; no intent_gap or bad_plan.

| Finding | Verdict / route | Evidence |
|---|---|---|
| `GameSaveStore.h:35` and `loadPrefs` comments still say never onExit() | low / patch | Reworded (loadPrefs can run from `onExit()` via the Unreadable re-read). |
| Unreadable re-read and settings-unread guard untested on the onExit path | medium / patch | Added `AnUnreadablePrefsBinThatReadsAgainIsMergedWhenOptionsIsLeftByAReplace` and `AnOptionsChangeLeftByAReplaceWhileTheSettingsCouldNotBeReadWritesNothing`. |
| `optionsChanged` may stay set on `onOptionsClosed` early paths, doubling the write | false | `onOptionsClosed` clears it unconditionally after `rememberChoices` (GameModeActivity.cpp); the Back test counts renames. |
| Unlocked merge in `onExit()` vs render task | false | The manager holds the render mutex for the whole Replace; `onOptionsClosed` takes the lock only because it runs without it. |
| No forced-exit deadline for the SD write | low / reject | One small file on a title screen with no timing contract; a fix adds a gate and state. |
| A failed write on Back is not retried at exit | low / reject | By design (Back already tried; matches "write once"); the fix adds retry state. |
| device-run-packet row 5.12 #4 says "not kept" | low / defer-as-history | The packet records the 2026-10-03 as-built answers; left as history. Re-run B5.12 #4 on the device to confirm the new behaviour. |
| Replace is simulated by `replaceFromOptions()`, not the real manager | low / reject | Same model as the old test; the manager source (:188-197) was read; sim/device run not done. |
| Latency of the write in sleep/Home | low / reject | Unmeasured: one tmp-write-plus-rename, same call as Back's. |
| Ticket ids in comments; formats.md line lengths | low / reject | Repo comments already cite amendments and ids; clang-format passes. |

## Plan Change Log

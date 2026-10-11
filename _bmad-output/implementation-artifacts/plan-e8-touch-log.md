---
title: 'Put the match touch log line behind CROSSHATCH_TOUCH_LOG'
type: 'chore'
ticket: ''
created: '2026-10-11'
status: 'built'
baseline_revision: '579c7674bd0dd594f6ce2fe6ee89c29ffd570c80'
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: []
deferred:
  - summary: >-
      Nothing automated asserts the default build lacks the touch line; the host harness forces CROSSHATCH_TOUCH_LOG on.
    evidence: |-
      Only match.cmake builds GameMatchActivity.cpp for the host, with the define. Deleting the #ifdef would keep every host test and CI env green; the flash gate's size ceiling would not notice either. This run proved absence once, by the ELF strings (Verification). An ongoing check needs a second match-harness variant without the define, or an ELF-strings assertion in the flash-budget job.
    location: >-
      test/game_script/harness/match.cmake:79
    severity: low
  - summary: >-
      CI never builds a firmware env with CROSSHATCH_TOUCH_LOG=1, so the documented device-run recipe can rot.
    evidence: |-
      The block is one LOG_DBG over a host-tested formatter. This run built x4pro with the flag (Verification). A CI build with the flag costs a full extra firmware build.
    location: >-
      docs/crosshatch/game-canvas.md:21
    severity: low
---

<intent-contract>

## Intent

**Problem:** In `LOG_LEVEL=2` dev builds, `GameMatchActivity::loopPlaying` logs a DBG line for every gesture (entry 14). Each tap enters the 16-line crash ring buffer, pushing the lifecycle lines out of a panic report, and hidden Battleship taps are recorded (epic-first-party-games retrospective A-4).

**Approach:** Per the owner's Decision of 2026-10-11 (epic Notes, retrospective A-4), compile the touch line, its `GameTouchLog.h` include and its `GameTouchLog::Line` buffer in only when the fork-only define `CROSSHATCH_TOUCH_LOG` is set, in every env and at every `LOG_LEVEL`. Default off; no `platformio.ini`, `platformio.local.ini`, CI or `lib/Logging` change. The host match harness defines it so its touch-log tests keep reaching the line. `game-canvas.md` says how to turn it on.

</intent-contract>

## Implementation Notes

Oneshot: one `#ifdef` around one include and one `if` block, a harness define, and a doc paragraph.

Decision, other touch lines (settled by entry 14's commits 8a0b72bf and 1acce4bf, `git log -L` of the block): entry 14 added exactly one log call, the `LOG_DBG("GAME", "%s: touch %s", ...)` in `loopPlaying`. Its other touch changes are not log lines but data collection (`TouchLatch` first/last sample, `Gesture::heldMs`, `Kind::Ended`, `Outcome`); `Outcome` is the routing decision itself (`aimed`), and the rest is a few stores per gesture, host-tested and read by `GameTouchLog`. They stay unconditional, so `readGesture` and `GameTouch` have one shape in every build. The drop lines ("dropped a touch ...", LOG_INF) predate entry 14 (17d02483), are about hand-off/frame timing, fire only on a dropped tap, and stay. Non-touch DBG lines are untouched.

## Review Triage Log

### 2026-10-11 — Review pass
- verdicts: 12 findings — high 0, medium 0, low 9, false 3, maybe-false 0 (plus the intent-alignment lens's descriptive report)
- findings:
  - `[low]` `patch` Blind Hunter 1: unwrapped doc paragraph and ragged `GameTouchLog.h` comment — doc paragraph rewrapped to 120 columns; the header comment reflowed by clang-format.
  - `[low]` `patch` Blind Hunter 2: stale "The touch log line above runs before the decision." — now "(when compiled in)". "Changes nothing below" holds: `outcome` and `aimed` are computed outside the `#ifdef`.
  - `[low]` `patch` Blind Hunter 3 / Edge Case 3: simulator enablement undocumented — doc says `sim.sh build` runs `pio run` (sim.sh line 112), so the variable reaches it, and the command runs under the build lock. Rejected: the `flock` wrapper itself in the doc (AGENTS.md's rule; one doc sentence points to it).
  - `[false]` `reject` Blind Hunter 4: dead `GameTouchLog` code ships in every build — `GameTouchLog.h` is header-only (no .cpp) and its functions are `inline`, so an unused one is not emitted; the ELF-strings and flash gate runs below measure it.
  - `[low]` `defer` Blind Hunter 5 / Verification Gap 1: only the define-on case is tested — deferred above. The claim that another harness target loses the log is `false`: `GameMatchActivity.cpp` is built only in `match.cmake` (`harness_base.sources.cmake:18` excludes it elsewhere).
  - `[false]` `reject` Blind Hunter 6: the name departs from the `FREEINK_*` convention and release envs may set it — the owner's Decision names the define; `scripts/fork_release.py` refuses `PLATFORMIO_BUILD_FLAGS` overrides (`BUILD_OVERRIDES`), so a release build cannot pick it up from the environment, and no env in `platformio.ini` sets it.
  - `[low]` `reject` Edge Case 1: `-DCROSSHATCH_TOUCH_LOG=0` still enables it — the Decision says "set"; the doc says `=1`; unlikely, and the fix adds a branch to a guard.
  - `[false]` `reject` Edge Case 2: define set at `LOG_LEVEL` below 2 leaves an include and an empty `if` — the Decision says the define gates the line at every `LOG_LEVEL`; the empty block is what the line compiled to at level 1 before this change, and costs nothing.
  - `[low]` `defer` Verification Gap 2: no CI build with the define on — deferred above; built locally here.
  - `[descriptive]` Intent Alignment: readings R-A (compile surface, implemented), R-B (runtime silence), R-C (scope: the two `LOG_INF` "dropped a touch" lines stay, settled in Implementation Notes: they predate entry 14, 17d02483, log no point, fire only on a dropped tap), R-D, R-E; divergence: the off state is verified at the ELF, not by a host test (same as the deferred item). No change.

## Verification

**Commands:**
- host suites (`cmake -S test -B build/test ...`, `ctest`) -- expected: all pass, TouchLogTest included
- `./bin/clang-format-fix` twice, `python3 scripts/check_upstream_touches.py` -- expected: clean
- `pio run -e x4pro` without the define, and with `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_TOUCH_LOG=1` -- expected: both build; the ELF's strings lack `: touch %s` without it and have it with it
- `pio run -e default`; `pio check` for `default` and `-e x4pro` at low/medium/high; flash gate `build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`

**Results (2026-10-11, tree at the commit this plan lands in; builds under the build lock, shared cache):**
- Host suites: `cmake --build build/test` and `ctest`: 2052 of 2052 passed, TouchLogTest's 23 included (run after the review patches).
- `./bin/clang-format-fix` twice: no change outside this story's paths; `check_upstream_touches.py`: PASS.
- `pio run -e x4pro` without the define (the flash gate's `build on`): builds. `strings -a` of its ELF: 0 lines with `: touch %s`, 2 with `dropped a touch` (the LOG_INF lines that stay).
- `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_TOUCH_LOG=1 pio run -e x4pro`: builds. Its ELF has 1 line with `: touch %s` (and the same 2 `dropped a touch`). The ELF is 3,856 B larger than the without-define ELF (measured the same way, same tree, debug info included); no flash-gate figure is quoted for it because the gate measures the default build.
- `pio run -e default`: builds.
- `pio check` (low, medium, high) for `default` and for `-e x4pro`: both PASSED.
- Flash gate: `build on`, `build off` ok; `compare --limit-kib 270 --ram-limit-bytes 1024`: +258,048 B (+252.0 KiB) flash, 18,432 B to spare; +784 B static internal RAM, 240 B to spare (games on versus off at this commit, the gate's own method, not a delta against an earlier figure); `objects`: 49 game objects, no problems.
- No CI gate or workflow changed, so no fresh-tree run.

## Auto Run Result

**Summary.** The match's per-tap `LOG_DBG("GAME", "%s: touch %s", ...)` line, its `GameTouchLog.h` include and the `GameTouchLog::Line` temporary compile in only when the fork-only define `CROSSHATCH_TOUCH_LOG` is set (any env, any `LOG_LEVEL`; it prints only at level 2). Default off. The other touch additions of entry 14 (first/last sample, `heldMs`, `Ended`, `Outcome`) are data and routing, not log lines, and the two "dropped a touch" `LOG_INF` lines predate it, so they stay (Implementation Notes). The host match harness defines it so TouchLogTest reaches the line. `game-canvas.md` says how to turn it on.

**Files.**
- `src/activities/games/GameMatchActivity.cpp` -- `#ifdef` around the include and the log block; comment updated.
- `src/games/GameTouchLog.h` -- header comment names the define (formatter unchanged).
- `test/game_script/harness/match.cmake` -- `CROSSHATCH_TOUCH_LOG=1` PRIVATE on `game_match_src`.
- `docs/crosshatch/game-canvas.md` -- the touch-log bullet replaces "there is no setting" with the opt-in and the build command.
- `_bmad-output/implementation-artifacts/plan-e8-touch-log.md` -- this plan.

**Review.** Four lenses as context-free subagents: 12 findings, 0 high, 0 medium, 9 low, 3 false. Patched: stale comment, doc wrapping, simulator/build-lock sentence. Deferred (2, low): no automated check of the off state, no CI build with the define. Rejected with reasons: dead-code claim (header-only inline), naming, `=0` define, empty block at low LOG_LEVEL. Formatting: `clang-format-fix` changed no file outside this story's paths.

**Residual risks.** The off state is proven once by ELF strings, not by a gate (deferred). `sim.sh build` honoring `PLATFORMIO_BUILD_FLAGS` is read from `sim.sh` (it runs `pio run`), not run.

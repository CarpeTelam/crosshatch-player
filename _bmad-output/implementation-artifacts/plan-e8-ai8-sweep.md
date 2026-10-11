---
title: 'Epic 8 retrospective AI-8: the post-epic sweep'
type: 'refactor'
ticket: ''
created: '2026-10-11'
baseline_revision: 'deecad1860134c0cfc6736faed057f4db61d0877'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
context: ['{project-root}/.agents/skills/refactor-for-review/SKILL.md', '{project-root}/.agents/skills/control-flow-clarity/SKILL.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Epic 8's retrospective (`epic-first-party-games-retrospective.md`, AI-8) left one sweep of items before epic-play-nearby changes Battleship:

- **G1:** Battleship's `main.lua` mixes rules with about 210 lines of drawing.
- **A-8:** `layout.snap` adds `L.W` where it means `layout.W`.
- **A-2:** a pause entered before `LuaGame::load` sets `startMs` is subtracted from play time that had not started.
- **A-10:** the 8.15 MENU-snap deferral predates entry 16's ninth row.
- **P3, A-3, A-7:** three stale comments or docs.

**Approach:** Move Battleship's drawing into `games/battleship/view.lua` without changing a single draw command, and fix `snap`'s width in the same package change. Capture the paused total at load, and subtract only the pause time after it. Restate the deferral with today's geometry. Correct the three texts.

## Boundaries & Constraints

**Always:**
- Battleship's frames stay identical, command for command. The companion checks (`draws.lua`, `checks.lua`, rounds) pass unchanged.
- `main.lua` keeps exporting `views`, `headline`, `prompt`, `last_shot_line`, `count_line` and `help_lines`, which the checks call.
- `view.lua` gets `game` passed in. It never requires `main`, and its own load requires nothing that is not already loaded (the load-nesting rule).
- `ch.time.ms` stays play time, frozen while paused. Every existing `PauseClockTest`, `PlayTimeTest` and `MatchTest` play-time case passes as it is.
- Locals stay under 256 B and nothing allocates.

**Never:**
- No change to Ultimate tic-tac-toe or Sudoku code, to `api-level-1.txt` entry lines or `API_SURFACE_CRC`, or to any upstream file.
- No new behavior in Battleship; the package hash may change.
- The MENU-snap question is not decided here: it is only restated.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Pause open before load | `enter(T0)`; load at T0+4000; `leave` at T0+10000; read at T0+12000 | `ch.time.ms` = 2000 (today it is 0) | none |
| Pause closed before load | `enter(T0)`, `leave(T0+3000)`; load at T0+5000; read 2000 later | 2000 (today it is 0) | none |
| No pause before load | as today's `PlayTimeTest` cases | unchanged | none |
| No ledger (`paused` null) | host with no pauses | raw clock since load | none |

</frozen-after-approval>

## Code Map

- `games/battleship/main.lua`:
  - Lines 191–205 (`wrap`) and 221–412 (`drawHelp`, `drawHeader`, `drawGrid`, `outline`, `drawFleet`, `drawTarget`, `drawMessage`, `drawTray`, `drawButtons`, `drawPlacing`, `drawWaiting`, `drawFiring`, `drawBoth`), plus `ICONS`/`LABELS` (38–39): move all of these to `view.lua`.
  - `game.draw` (414–437) becomes `sayUnsupported()`, then `view.draw(game, ...)`.
  - Keep the rules, `mode`, the text helpers, `HELP_PARAGRAPHS` and `RESULT_*` in `main`.
  - `help_lines` calls `view.wrap`.
- `games/battleship/layout.lua:41` (`snap`): `L.W` becomes `layout.W`.
- `lib/GameCore/PauseClock.h`:
  - Add a seqlock read that returns the clock's now and the paused total together.
  - `playMs` takes the total at load and subtracts only the growth since then.
- `lib/GameScript/ChBindings.h:79-84` and `.cpp:313-319` (`timeMs`): a new context field, `pausedAtStartMs`.
- `lib/GameScript/LuaGame.cpp:206-207`: when `ports.paused` is set, take `startMs` and `pausedAtStartMs` from one read.
- `test/game_core/PauseClockTest.cpp` and `test/game_script/HostBindingsTest.cpp` (`PlayTimeTest`): add the two matrix cases at both levels.
- `_bmad-output/implementation-artifacts/deferred-work.md` `## 8.15`, first entry (A-10).
- `test/game_script/first_party/sudoku/rules.lua:1-7` (P3): check VMs have their own limits since `e8-xr` (`HostBounds.h`; README "The check VMs' limits").
- `test/game_script/fixtures/slow-restart/main.lua:5-7` (A-3): follow fixtures README step 9.
- `docs/crosshatch/api-level-1.txt:86-96` (A-7, and A-2's wording):
  - A timer that falls due while paused is delivered on Resume, before the next input, and no game callback runs while paused.
  - Play time leaves out only pauses after the load.

## Tasks & Acceptance

**Execution:**
- [x] `games/battleship/view.lua`, `main.lua` -- move the drawing code verbatim, keeping its comments and order; `view.draw(game, L, state, seat, ui, m)` replaces `game.draw`'s body below the `help` and `seat 0` branches -- G1.
- [x] `games/battleship/layout.lua` -- `layout.W` in `snap` -- A-8.
- [x] `lib/GameCore/PauseClock.h`, `lib/GameScript/ChBindings.{h,cpp}`, `lib/GameScript/LuaGame.cpp` -- the baseline at load -- A-2.
- [x] `test/game_core/PauseClockTest.cpp`, `test/game_script/HostBindingsTest.cpp` -- the matrix cases. They must fail on today's code.
- [x] `deferred-work.md`, `rules.lua`, `slow-restart/main.lua`, `api-level-1.txt` -- the text fixes -- A-10, P3, A-3, A-7.
- [x] Retrospective AI-8 row -- mark it done with this plan's commit.

**Acceptance Criteria:**
- Given today's companion checks, when the games check runs at both canvases, then every Battleship round, frame pin and check passes unchanged.
- Given the matrix's pause-before-load cases, when `ch.time.ms` is read, then it is 2000 in each case, and the new tests fail if the baseline is dropped.
- Given `main.lua` after the split, when it is measured, then it holds no `ch.gfx` call.

## Implementation Notes

- G1: `view.lua` holds the moved code in its old order, bodies unchanged. The only edits are a `game` (and, for `drawHelp`, an `L`) parameter on the functions that call `game.*`, and `view.draw` in place of `game.draw`'s old body, which still clears first and keeps the `help` and `seat 0` branches; `main.lua` is `sayUnsupported()`, then `view.draw(game, canvas(), state, seat, ui, mode(state, seat))` (`canvas` and `mode` are pure, so computing them before the `help` branch emits nothing). `wrap` stays a local in `view.lua` and is exported as `view.wrap`. After review, the local `view` in `drawPlacing`, `drawWaiting`, `drawFiring` and `drawBoth` is renamed `seen`, so it no longer shadows the module table. `main.lua` requires `view` after `fleet` and `layout`, so `view.lua`'s own requires are cache hits (load nesting stays `main > x`). `main.lua` has no `ch.gfx` call. `layout.lua`'s header said "main.lua draws ... through it"; it now names `view.lua` (the paragraph was rewrapped).
- Frame identity, beyond the companion checks: a scratch Lua 5.5 build of `lib/lua` with a recording `ch` stub drew seats 0, 1 and 2 with four `ui` variants at every step of six seeded random games, with the baseline's three files and with the new four; the command streams (1,463,245 lines) were byte-identical at 466 x 788, 474 x 788, and 400 x 600 (the unsupported-canvas log included).
- A-2: `PauseClock::read(clock)` returns `Reading { nowMs, pausedTotalMs }` from the seqlock loop that `playMs` used to inline; `playMs(clock, startMs, pausedAtStartMs = 0)` subtracts `pausedTotalMs - pausedAtStartMs` (clamped at 0). `LuaGame::load` sets `startMs` and `pausedAtStartMs` from one `read()` when `ports.paused` is set, and `pausedAtStartMs = 0` otherwise (the context is reused across loads).
- A third `PauseClockTest` case, added after review, pins a pause open at the load and still open at the read: play time reads 0 and stays 0.
- The four new tests (`PauseClockTest` and `PlayTimeTest` `APauseOpenAtTheLoadLeavesOutOnlyItsTimeAfterTheLoad`, `APauseClosedBeforeTheLoadIsNotLeftOut`) failed, all four, with `playMs` made to ignore `pausedAtStartMs`, and pass with it.
- A-7 wording, after review: only what the host does: no timer is polled while the match is paused, so one that falls due then is delivered once the match is back in play. No ordering or callback claim: GameVM's run loop does not check the pause (a Play again's restart, a first start, and queued steps run while Paused), and `loopPlaying` posts a gesture before `pollTimer()`.
- After review: `fleet.lua`'s header and `game.views`' comment name `view.lua` as the drawer; `fixtures/README.md`'s `slow-restart/` row matches step 9; `## 8.15`'s first entry is narrowed to Sudoku's MENU panel, since every HOW TO PLAY page closes on any tap.
- Also touched: `deferred-work.md` `## e5-run4`'s device check for A-2 said pause time before the load "is subtracted"; it now says that was fixed here and what to check.
- The retrospective's AI-8 row reads "commit pending": replace it with this plan's commit hash at commit time.

### Verification after the review patches (orchestrator, 2026-10-11, on the patched tree)

- **Host suites:** 2,052 of 2,052 pass (`cmake --build build/test && ctest -j8 --output-on-failure`, under the host-test lock).
- **Fast checks:** every `scripts/*_test.py` passes, `check_upstream_touches.py` passes, and `./bin/clang-format-fix` makes no change.
- **Firmware,** in one run under the build lock:
  - The flash gate's four steps pass. `build on` (the `pio run -e x4pro` build), `build off`, and `compare --limit-kib 270 --ram-limit-bytes 1024` exit 0: flash is within the 276,480 B gate with 17,616 B to spare, and static internal RAM is +784 B against the 1,024 B limit, 240 B to spare. `objects` reports no problems.
  - This is one measurement at this tree, not a delta against the base.
  - `pio run -e default` succeeds.
  - `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` passes for `default` and for `-e x4pro`.
- **Simulator:** `sim.sh build x4pro` succeeds. Battleship was packed from the tree (package hash `e04ad7646a807f0d`) and installed through the inbox. The title, the hand-off, the placement frame and a Random fleet all draw as before, with no script error. The shots are `e8-ai8-screenshots/bs-placing.png` and `bs-random.png`.

## Plan Change Log

## Review Triage Log

### 2026-10-11 — Review pass 1 (blind-hunter, edge-case-hunter, verification-gap, intent-alignment)

- verdicts: 20 findings — high 0, medium 1, low 9, false 8, maybe-false 2. The verification-gap lens reported no gaps.
- findings:
  - `[medium]` `patch` edge, intent, blind: the new A-7 sentence "no game callback starts while the match is paused" is false. `GameVM`'s run loop never checks the pause, so a Play again's `rounds.restart()` (`GameVM.cpp:188`), a first `rounds.start()` (`:160`), and queued `rounds.step()` (`:218`) all run while Paused. Fix: drop the claim and say only what the host does (no timer is polled while paused).
  - `[low]` `patch` edge, blind: "delivered on Resume, before the next input" overstates the order. `loopPlaying` posts the gesture it read before it calls `pollTimer()`, so a tap in the first pass after Resume goes first. Same sentence as the finding above, so one rewrite fixes both.
  - `[low]` `patch` edge: `games/battleship/fleet.lua:2` still says "main.lua draws from their results".
  - `[low]` `patch` blind: A-3 is half done. `test/game_script/fixtures/README.md:42` (the `slow-restart/` row) still says "Back and Resume within the gap", the same wording the fixture header lost.
  - `[low]` `patch` blind: in `view.lua`, `drawPlacing`, `drawWaiting`, `drawFiring` and `drawBoth` declare `local view`, which shadows the module table; a later `view.*` call inside them would index the seat's view. Fix: rename the local.
  - `[low]` `patch` blind: `game.views`' comment in `main.lua` ("draw takes everything it draws about the other seat from here") should name `view.lua`.
  - `[low]` `patch` blind: the restated `## 8.15` entry keeps "Ultimate tic-tac-toe's and Battleship's help pages do not snap". Both close their help page on any tap (`games/battleship/main.lua` input, `games/ultimate-tic-tac-toe/main.lua:129`), so snapping cannot matter there. Narrow the entry to Sudoku's panel and pages.
  - `[low]` `patch` blind: no test for a pause open at the load and still open at the read, which should read 0 and stay frozen. Add it to `PauseClockTest`.
  - `[low]` `patch` blind: the retrospective's Findings rows (G1, P3, A-2, A-3, A-7, A-8, A-10), the verdict's deferred list, the A-10 open question and AI-10 (A-2 changes `ch.time.ms`'s meaning within level 1 a second time) are not updated, and the AI-8 row's "commit pending" needs the hash. The orchestrator updates these at commit.
  - `[maybe-false]` `defer` intent, blind: the device path itself (Back before the first frame through `GameMatchActivity`, then `GameVM::matchPaused` while the VM task runs `load()`) has no test. The ledger and `LuaGame` levels are pinned; `GameVM` hands its own ledger in as `HostPorts::paused` (`GameVM.cpp:83`). What would settle it: a `MatchTest` that pauses before `showFrame()` with a clock-reading fixture. `## e5-run4` already holds the device check.
  - `[maybe-false]` `reject` blind: four comments (`LuaGame.h:35-36`, `GameVM.h:126-127`, `GameMatchActivity.cpp:261`, `HostBindingsTest.cpp:111`) say `ch.time.ms` leaves out "the" Paused interval(s). They describe pauses made while the game runs, which are still left out; only a pause before the load changed. Not wrong enough to edit.
  - `[low]` `reject` blind: `playMs`'s defaulted third argument keeps the old two-argument form. The only production caller (`ChBindings.cpp` `timeMs`) passes it. Making it required would touch about 19 test calls for a hazard no caller meets.
  - `[low]` `reject` blind, intent: the command-stream identity run is not committed. The committed companion checks pin Battleship's frames, and moving the target board made 4 of 9 games-check tests fail (Implementation Notes). A committed comparer would be new tooling for a one-off move.
  - `[low]` `reject` blind: no automated check keeps `ch.gfx` out of `main.lua`. That is a style boundary, and a regression there harms nothing; adding a source-grep check is new complexity.
  - `[false]` `reject` blind: plan frontmatter lists four lenses with an empty triage log. The log is written here, in this pass.
  - `[false]` `reject` blind: the plan's Verification runs `sim.sh` without `flock` and ctest without `--output-on-failure`. Fixing it means editing the plan, and the run itself takes the lock.
  - `[false]` `reject` intent: A-10's restatement adds the snap-rule inference. The intent's "restate … not decided" holds: the entry ends "Still open for the owner".
  - `[false]` `reject` intent: A-8 has no test. The bug is latent, since `compute` sets `L.W = layout.W`, so no public-surface test can see it; the fix is a one-token correction.
  - `[false]` `reject` intent: A-8 "package change" read as a version bump. Nothing in the repo asks for a manifest version bump; only the hash changes.
  - `[false]` `reject` intent: G1 computes `canvas()` and `mode()` on the help and seat-0 paths too. Both are pure, so no command differs; the stream comparison confirms it.

## Design Notes

The baseline is exact. The paused total read at load counts every pause up to then, open or closed, so `total(now) - total(load)` is precisely the pause time after the load. Reading both values in one seqlock pass keeps the load-time read consistent with a concurrent `leave()` on the loop task. `playMs(clock, startMs, pausedAtStartMs = 0)` keeps the old two-argument form for the existing tests.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake --build build/test && ctest --test-dir build/test -j8'` -- expected: all pass, with the new cases included.
- `python3 scripts/check_upstream_touches.py`, every `scripts/*_test.py`, and `./bin/clang-format-fix` twice -- expected: pass, with nothing new.
- After the review, under the build lock: `pio run -e x4pro` and `pio run -e default`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`, for `default` and with `-e x4pro`; `python3 scripts/check_flash_budget.py` with the gate's steps -- expected: success, and the games-on delta within the gate.
- `.claude/skills/run-crosshatch-player/sim.sh build x4pro` and one Battleship placement screenshot -- expected: the same frame as before.

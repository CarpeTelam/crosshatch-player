---
title: 'Stop the game clock while the match is paused'
type: 'feature'
ticket: '13'
created: '2026-10-09'
status: 'built'
baseline_revision: '9fa5e0c255449ebbafd6e6d49fbb1d445cf29a7d'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: ['{project-root}/AGENTS.md', '{project-root}/.skills/heap-discipline/SKILL.md']
warnings: []
deferred:
  - summary: >-
      A pause entered from Result or HandOff (a hidden pass match), and a Leave, ScriptError, or ForcedExit from Paused, have no match-level test with the clock game.
    evidence: |-
      handle() takes the Paused edge from `from`/`to` for every state, and MatchLifecycle covers those transitions, so the ledger toggles the same way (a matchResumed() on a match that is leaving is a no-op); but no GameMatchTest drives a pass match through Paused or those exits while reading ch.time.ms. Review rated it low in both passes; a pass-hidden fixture test would settle it.
    location: >-
      test/game_script/harness/GameMatchTest.cpp
    severity: low
  - summary: >-
      A script that busy-waits on ch.time.ms() while the match is paused stays in its loop until Resume, and the raw 3 s watchdog can end it in the error view.
    evidence: |-
      The watchdog counts raw millis (GameMatchActivity.h WATCHDOG_MS) and CallGuard counts instructions (CallGuard.h INSTRUCTION_BUDGET); neither is play time. Only the slow-restart fixture busy-waits on the clock (a 2 s spin in setup); Sudoku does not. Documented in api-level-1.txt and the fixture README (step 9); the owner Decision keeps the timer and watchdog on the raw clock. Excluding paused time from the watchdog would settle it.
    location: >-
      src/activities/games/GameMatchActivity.cpp:564 (vmHealthy), test/game_script/fixtures/slow-restart/main.lua
    severity: low
  - summary: >-
      The PauseClock stress test does not show that the retry path of playMs ran, and the x86 host cannot show a reordering of the clock read.
    evidence: |-
      cleanReads is met by the one read before the writer starts and no counter records a retry; the acquire fence is by the seqlock pattern, not by a test. A ThreadSanitizer run or a retry counter in a test build would settle it.
    location: >-
      test/game_core/PauseClockTest.cpp
    severity: low
  - summary: >-
      The cost of the spinlocked 8-byte atomic loads in ch.time.ms() on the S3 (two per call, interrupts masked) is unmeasured.
    evidence: |-
      ESP-IDF stdatomic.c takes one global portMUX for each 8-byte load or store; playMs does two loads and the writer one store per pause edge. No timing was taken on a device; a tight `while true do ch.time.ms() end` run on the device would settle it. Flash and IRAM are measured (+416 B flash, section totals equal to the base).
    location: >-
      lib/GameCore/PauseClock.h
    severity: low
---

<intent-contract>

## Intent

**Problem:** `ch.time.ms()` counts the time the pause menu is open (the VM and `ui.last` survive Pause and Resume), so Sudoku's solve time includes pauses (cross-story row 20; owner Decision 2026-10-09, epic Notes, "settled").

**Approach:** `ch.time.ms()` returns play time: milliseconds since the game loaded, less every interval the match spent in `MatchState::Paused`. A small wait-free ledger the match writes and the binding reads carries the pauses; `ch.timer` and `GameTimer` keep the raw clock. Sudoku's sources do not change.

## Boundaries & Constraints

**Always:** Every state but Paused keeps counting (Over, Result, HandOff, Error included); the pause of Home counts as Paused. The VM task reads while the loop task enters and leaves Paused: atomics only, no lock in the binding. A host with no ledger (null) behaves as before. Shared code builds for the C3.

**Never:** Change `ch.timer.after`, `GameTimer`, `IClock`, the device/simulator `GameClock`, `games/sudoku`, API level 1's entry lines (so `API_SURFACE_CRC` stays), or add a `check_layers.py` edge. No 64-bit-lock of our own, no mutex in the binding.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Pause then resume | play 5 s, pause 30 s, resume, play 2 s | `ch.time.ms()` = 7000 | none |
| Read while paused | VM reads the clock mid-pause | frozen at the value when the pause began | none |
| Two pauses | pause, resume, pause, resume | both intervals left out | none |
| Timer across a pause | `ch.timer.after(3000)` set before a 30 s pause | fires on the raw clock exactly as today | none |
| enter twice / leave unpaused | repeated calls | no-op | none |
| No ledger | `HostPorts::paused` null (host doubles) | `now - startMs` as today | none |

</intent-contract>

## Code Map

- `lib/GameCore/PauseClock.h` (new) -- the ledger. One `std::atomic<uint64_t>` word: bit 63 = paused; running: low bits = paused total so far; paused: low bits = (pause start - paused total so far). `enter(now)` and `leave(now)` both store `now - low` (flipping the bit), so a reader needs no second variable. `pausedMs(word, now)` (pure); `playMs(clock, startMs)` is a seqlock reader: load the word (acquire), read the clock, acquire fence, reload the word (relaxed), retry while it changed.
- `lib/GameScript/ChBindings.h/.cpp` -- `BindingContext::paused` (const PauseClock*), `timeMs` reports `paused ? paused->playMs(*clock, startMs) : now - startMs`; fix the comments. `timerAfter` untouched.
- `lib/GameScript/LuaGame.h/.cpp` -- `HostPorts::paused = nullptr` (trailing, default), `bindings.paused = ports.paused` in `load()`.
- `src/games/GameVM.h/.cpp` -- member `GameCore::PauseClock paused` declared before `game`, passed in `HostPorts`; `void matchPaused()` / `matchResumed()` call `enter/leave(clock.nowMs())`.
- `src/activities/games/GameMatchActivity.cpp` `handle()` -- after `lifecycle.apply` succeeds: `to == Paused` enters, `from == Paused` leaves, guarded by `if (vm)`.
- `test/game_script/harness/games_check/ScriptVm.cpp` -- the check VM's own `BindingContext`: `bindings.paused = ports.paused`.
- `docs/crosshatch/api-level-1.txt` -- comment above `fn ch.time.ms()`; comments are not in the CRC.
- Tests: `test/game_core/PauseClockTest.cpp` (+ CMakeLists), `test/game_script/HostBindingsTest.cpp`, `test/game_script/harness/GameMatchTest.cpp`.
- Read-only: `src/games/GameClock.*`, `lib/GameScript/GameTimer.*`, `lib/GameCore/IClock.h`, `games/sudoku/main.lua`.

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameCore/PauseClock.h` -- add the ledger as above, header-only, comments naming the single writer (loop task), any reader, and the clock-after-load ordering -- a consistent, lock-free read (seqlock reader)
- [ ] `lib/GameScript/ChBindings.h`, `ChBindings.cpp`, `LuaGame.h`, `LuaGame.cpp` -- plumb `paused` and change `timeMs` -- the binding reports play time
- [ ] `src/games/GameVM.h`, `GameVM.cpp`, `src/activities/games/GameMatchActivity.cpp` -- own the ledger and toggle it on the Paused edge
- [ ] `test/game_script/harness/games_check/ScriptVm.cpp` -- pass the port through -- host double agrees
- [ ] `docs/crosshatch/api-level-1.txt` -- describe play time above the entry -- doc
- [ ] tests -- PauseClockTest (matrix rows, wrap-free mask, a two-thread stress that no read goes past the raw elapsed time and none goes backwards among reads no write overlapped); HostBindingsTest (paused interval left out of `ch.time.ms()`, a timer set before the pause fires on the raw clock after it, frozen read while paused, null port unchanged); GameMatchTest (Playing, Paused, Playing at the match level with the real GameVM: a game logging `ch.time.ms()` on tap shows the pause left out) -- pins the behaviour

**Acceptance Criteria:**
- Given a game that loaded 5 s ago and sat in Paused for 30 s, when it reads `ch.time.ms()` after resuming and 2 s more, then it gets 7000.
- Given a timer set before a pause, when the raw clock passes its due time, then the loop delivers it exactly as without the change.
- Given the match goes Playing, Paused, Playing, when the game reads the clock before and after, then the difference leaves the pause out.
- Given Sudoku in the simulator, when the player pauses about 30 s, resumes, and solves a one-blank scratch puzzle, then the shown time leaves the pause out.

## Implementation Notes

Implemented by one subagent from this plan (report: scratchpad 8.13/implementation-report.md). The run's first hand-back came early (a harness demand while the subagent was still running); the orchestrator resumed it after the subagent finished. Review patches (same subagent, re-engaged): playMs re-validation loop, header comment, stress test, two match tests (Home, two pauses), rig ledger, docs. Verification then caught one `pio check` finding (shadowFunction), fixed by the build agent (`pausedTotal`). Host verification before review, re-run by the build agent: full ctest 1818/1818, the 16 new tests (PauseClockTest 9, PlayTimeTest 6, MatchTest.TheGameClockLeavesThePauseOut) pass and every matrix row has a covering test.

## Plan Change Log

## Review Triage Log

### 2026-10-09 — Review pass
- lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment, each a context-free subagent over the staged diff (24.7 kB); reports saved in the build scratchpad (8.13/lenses/).
- verdicts: 19 findings — high 0, medium 4, low 11, false 4, maybe-false 0 (the last row is the `pio check` finding, which the verification step found after the lenses; the intent-alignment lens reports readings, so its rows are its divergences)
- findings:
  - `[medium]` `[patch]` BH1 stress test can fail with a starved reader (`cleanReads > 0` after `while (!done)`) — real: on one loaded core the writer can finish first. Fix: the reader reads at least once per pass.
  - `[low]` `[patch]` BH2 header describes only the overstating straddle, not the understating one (paused word, leave, later clock) — real; the comment is rewritten with the re-validation (EC1).
  - `[false]` `[reject]` BH3 comment line over 120 columns would make clang-format diff — `./bin/clang-format-fix` ran twice clean; the paragraph is rewritten anyway.
  - `[medium]` `[patch]` BH4 / IA single writer unchecked — verified: `handle()` is reached from `loop()`, `handleHomeGesture()` (`ActivityManager::loop`, `ActivityManager.cpp:110`), and `onExit()`/`fail()` through the same task (`docs/activity-manager.md`, FreeRTOS Task Model; the render task never calls it). All on the main task, so no CAS needed; the citation goes into the header.
  - `[medium]` `[patch]` BH5 / IA2 match-level coverage is Back only — real: the ticket and Decision name Home's pause. Added tests for Home's pause and for two pauses. Result/HandOff pauses take the same `from`/`to` edge in `handle()` and are not separately tested (the pass machine's states are MatchLifecycle's, covered there).
  - `[low]` `[patch]` BH6 `game-api-seed.md` still says "since the game started" — real, a one-row edit.
  - `[low]` `[patch]` BH7 `api-level-1.txt` note omits display-only, other states, sleep — real; comment lines only, CRC unchanged.
  - `[low]` `[reject]` BH8a `ChBindings.cpp` null-ledger branch has no `now < start` clamp — that line is the binding's code from before this change, unchanged.
  - `[low]` `[patch]` BH8b CMake order — trivial, fixed.
  - `[low]` `[reject]` BH8c some PauseClockTest cases repeat HostBindings ones; BH8d bit-63 limit unstated — duplicated coverage costs nothing, and 2^63 ms is not reachable.
  - `[medium]` `[patch]` EC1 reader preempted between word load and clock read overstates, then steps backwards (also EC2 for the on-device preemption window) — real: the VM task can be preempted there. Fix: `playMs` re-loads the word after the clock read and repeats on change (lock-free: a retry follows a completed write). The writer's own gap between its clock read and its store stays, a few instructions, and is documented.
  - `[low]` `[patch]` EC3 the 64-bit atomic: `components/newlib/src/stdatomic.c` emulates it with one global `portMUX` spinlock, a critical section held for the 8-byte access only, never across blocking work. The comment said "libatomic emulates it"; it now says this. The link and cost are measured in Verification.
  - `[low]` `[patch]` EC4 the plan promised "never backwards" and the test checks it only on non-overlapped reads — the claim narrows to what holds (see EC1), and the test comments say why.
  - `[low]` `[patch]` VG1 a call that busy-waits on `ch.time.ms()` while the match is Paused (loopView polls timers and the watchdog in Paused, `GameMatchActivity.cpp:549,564`) stays in its loop for the pause; the 3 s watchdog counts raw `millis()`, and CallGuard's budget is an instruction count (`CallGuard.h:40`), so only the watchdog ends it. Only the `slow-restart` fixture does this (a 2 s spin in `setup`, README step 9 pauses in that gap): a pause past about 1 s there trips the watchdog. Not a product game (Sudoku never waits on the clock); documented in the API note and the fixture README, the Lua unchanged.
  - `[low]` `[patch]` IA1 the games-check rig left `paused` null, so the check VM never ran the device's code path — the rig now owns a `PauseClock` and passes it.
  - `[false]` `[reject]` IA3 no test of a Sudoku move's `dt` across a pause — Sudoku's `move()` only subtracts two `ch.time.ms()` reads (`games/sudoku/main.lua:164`), covered by the binding tests; the simulator check in Verification shows it end to end.
  - `[low]` `[patch]` EC1 (edge-case hunter's row for BH2) the public `pausedMs(now)` documents that `now` must be read after its own load — same root as BH2, same fix.
  - `[low]` `[defer]` EC3 (edge-case hunter's row for BH7) a pause from Result or HandOff has no match-level test — same root as BH7; the first-pass deferred item, extended.
  - `[low]` `[patch]` EC8 (edge-case hunter's row for BH4) README step 9 can now end in the error view — same root as BH4, same fix.
  - `[low]` `[patch]` VG1 (verification-gap lens, other findings) `GamesCheckRig::pauses()` has no caller — same root as BH6, same fix.
  - `[low]` `[patch]` VG2 (verification-gap lens, other findings) the README step 9 line is indented three spaces — same root as BH4, same fix.
  - `[false]` `[reject]` IA edge notes: `ForcedExit` from Paused leaves once, harmlessly (a repeat is a no-op); `if (vm)` skips a pause before load, which cannot occur (Paused follows Started).
  - `[low]` `[patch]` PC (verification, `pio check -e x4pro`) `PauseClock.h:79` local `paused` shadows the member function `paused()` (cppcheck shadowFunction, a low defect that fails the gate) — renamed the local `pausedTotal`; re-run `pio check -e x4pro` passes.
  - `[false]` `[reject]` VG/IA "no gaps" summaries and the readings table (A implemented, B plumbing, C every-host default): reading C's "default" is met by the rig change above; D readings are excluded by the Decision.

### 2026-10-10 — Review pass 2 (follow-up pass over `9fa5e0c2..38b9104c`)
- lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment, four fresh context-free subagents over the 36.7 kB diff; reports in the scratchpad (8.13/lenses2/).
- verdicts: 23 findings — high 0, medium 1, low 18, false 4, maybe-false 0
- findings:
  - `[medium]` `[patch]` BH1 `playMs` is unsound under the C++ memory model: two acquire loads around a plain clock read do not stop the clock read moving after the second load — real (the orchestrator confirmed it). Fix: the standard seqlock reader (acquire load, clock read, `atomic_thread_fence(acquire)`, relaxed reload, retry); the header says which. The x86 host cannot show the bug, so the stress test cannot prove it; the fix is by the pattern.
  - `[low]` `[patch]` BH2 / EC1 `paused()` and the instance `pausedMs(now)` are test-only and the latter's comment contradicts its signature — real. Both deleted; `PauseClockTest` reads the paused total through `playMs`; the pure static `pausedMs(word, now)` stays.
  - `[low]` `[defer]` BH3 a game that busy-waits on `ch.time.ms()` while paused stays in its loop and the raw 3 s watchdog ends it — real, documented, owner-accepted by the Decision's scope (the timer and watchdog keep the raw clock); deferred, no watchdog change built.
  - `[low]` `[patch]` BH4 / EC8 / VG2 `slow-restart` step 9 conflicts with the new behaviour, the sentence was in two places and mis-indented — real. Now one instruction in step 9 (resume within about 1 s, expect the error view otherwise), the table row restored.
  - `[low]` `[patch]` BH5 stale wording ("counts from load()") and an `api-level-1.txt` note that says nothing of the CRC — real. `GamesCheckRig.h` and `LuaGameFixture.h` comments, the plan's Code Map, Design Notes and Execution text reworded, the seed row mentions the timer, `api-level-1.txt` says `API_SURFACE_CRC` and the level stay as they are. The epic Notes line 97 is the orchestrator's (fixed there). The contract paragraph's "wait-free" in the Approach is read-only and left.
  - `[low]` `[patch]` BH6 / VG1 `GamesCheckRig::pauses()` has no caller and no step can drive a pause — real. Accessor deleted, member and wiring kept (the check VM runs the device's path); no step kind added (new scope).
  - `[low]` `[defer]` BH7 / EC3 match-level lifecycle paths untested (Result and HandOff pauses, Leave, ScriptError or ForcedExit from Paused, a null `vm`) — all share `handle()`'s one edge, and `matchResumed()` on those exits is a no-op on a leaving match; added to the existing deferred item.
  - `[low]` `[defer]` BH8 the stress test does not show the retry path ran (`cleanReads` is met by the first read) — real; no instrumentation was built; the x86 host would not show the reorder anyway.
  - `[low]` `[defer]` BH9 the cost of the spinlocked 8-byte loads on the S3 (two loads per `ch.time.ms()`) is unmeasured — the section is a few instructions with interrupts masked; no timing was measured. The C3 side of the finding is false (EC6).
  - `[low]` `[patch]` BH10 the header comment repeats itself — trimmed to the bit layout once, the single-writer rule next to `enter()`/`leave()`, the stdatomic note once. The suggested debug assert of the single writer is not built (no cheap way to name the loop task; the citation and the verified call graph stand).
  - `[low]` `[reject]` EC2 a timer delivered while Paused is on the raw clock, so a handler comparing it with `ch.time.ms()` sees less play time — the Decision keeps `ch.timer.after` and `GameTimer` on the clock they have; not this change's to alter.
  - `[low]` `[reject]` EC4 `BindingContext::paused` defaults to null, so a host builder that forgets it counts paused time — the device builds it in one place and three harness builders set it; making it mandatory would change every `HostPorts` site for nothing a test shows.
  - `[false]` `[reject]` EC5 `enter()`/`leave()` have only a comment for a single writer — carried from pass 1 (BH4): the call graph is verified (`ActivityManager::loop`, main task).
  - `[false]` `[reject]` EC6 the 64-bit atomic was never linked for the C3 — `FREEINK_CAP_GAMES` is off in `default`, so no game library is built there, and `pio run -e default` succeeds; on the S3 the link is confirmed (`nm`).
  - `[low]` `[patch]` EC7 the plan's Code Map and Design Notes still say "wait-free, no retry loop" — reworded as BH5.
  - `[false]` `[reject]` IA1 no test runs Sudoku's sources across a pause — the simulator run on a scratch copy does (Verification, Results, pass 1); the binding and match tests pin the clock Sudoku reads.
  - `[low]` `[reject]` IA2 the seed doc and fixture README edits are outside the ticket's listed files — one row and two sentences that keep the documents true; they follow the Decision's "update where it describes ch.time.ms".
  - `[false]` `[reject]` IA3 `LuaGameFixture`'s ports leave the ledger null, so tests built on it count raw time — readings A2/A3: the doubles that stand for the device (`GamesCheckRig`, `GameVM`) carry the ledger, and the fixture's comment now says so.

## Design Notes

- Intent settled by epic Notes, owner Decision 2026-10-09 "cross-story row 20, settled" (build as entry 13; Paused only; Sudoku unchanged; `ch.timer`/GameTimer unchanged; comment in the binding and `api-level-1.txt`). The ticket's `unknown` (reaching the clock "without a new layer edge") is answered by the existing edges: Screens (`GameMatchActivity`) call the adapter (`GameVM`, an existing include), the adapter hands `lib/GameScript` a `lib/GameCore` type through `HostPorts` (GameScript already includes GameCore's `IClock.h`). No new include edge; `check_layers.py` runs in Verification.
- One packed word, not two variables: a reader that loaded a paused total and a pause start separately could double count an interval a concurrent `leave` just closed. `closed' = closed + now - since = now - (since - closed)`, so entering stores `now - closed`, leaving stores `now - (since - closed)`: the same subtraction, and `pausedMs = running ? v : now - v`. No lock of ours. The reader is a seqlock-style retry (below), lock-free: a retry follows only a completed write, and writes are one per pause edge.
- 64-bit atomic: no `uint64_t` atomic is in the tree yet. ESP-IDF's `newlib/src/stdatomic.c` emulates it with a spinlock on Xtensa and RISC-V without 64-bit atomics, so the source has no lock and the firmware links. The build measures what it costs (`__atomic_*_8` in IRAM counts toward the flash gate's IRAM).
- Order: the reader loads the word, reads the clock, fences (acquire) and reloads the word relaxed, so the clock read cannot move after the reload (two acquire loads around a plain clock read would not stop that, review pass 2). The `now` it keeps is then never before the `now` the writer stored (clock monotone), and `now - v` cannot go negative; the code still clamps at 0.
- `timeMs` is a one-line change; its only guard-free body has no early return. `timerAfter` keeps `context.clock->nowMs()`: the timer is due on raw time.
- Doubles: `FakeClock` and `RigClock` stand in for `GameClock`; they are unchanged and a null `HostPorts::paused` is the "nothing pauses" host, which equals today's device behaviour for a match never paused. The match-level test uses the real `GameVM` over `fakertos::nowMs`, which `GameClock` reads via `esp_timer`.
- Single writer, checked (review BH4): `GameMatchActivity::handle()` is reached from `loop()` and `handleHomeGesture()` through `ActivityManager::loop()` (`src/activities/ActivityManager.cpp:94-113`, the Arduino main task), and from `onExit()` and `fail()` in that task's action processing and the activity's own loop (`docs/activity-manager.md`, FreeRTOS Task Model: the render task never calls it). So `enter()`/`leave()` need no CAS; `GameVM::matchPaused()`/`matchResumed()` are loop-task calls.
- Can a VM call run while Paused (review VG1)? Yes. `loopView()` (Paused, Over, Error) polls the timer and the watchdog (`GameMatchActivity.cpp:549,564`), and a call in flight at the pause keeps running on the VM task. `CallGuard` limits calls by an instruction count (`CallGuard.h:40`, 2,000,000), not by time; the only wall-clock stop is the match's watchdog on raw `millis()` (`GameMatchActivity.h:115`, 3 s). So a script that busy-waits on `ch.time.ms()` while paused stays in its loop until Resume and only the watchdog ends it. Sudoku never waits on the clock; only the `slow-restart` fixture does (a 2 s spin in `setup`), so it is documented there (README row and step 9) and in `api-level-1.txt`, with the Lua unchanged.
- 64-bit atomic on the S3 (review EC3): ESP-IDF `components/newlib/src/stdatomic.c` emulates `std::atomic<uint64_t>` with one global `portMUX` spinlock, a critical section (interrupts masked) held only for the 8-byte load, store, or memcpy and never across blocking work. The binding takes no lock of its own. The x4pro link resolves `__atomic_load_8` (35 B) and `__atomic_store_8` (39 B), both in IRAM, and only the games-on ELF has them (`nm`, Verification).
- Reading: `playMs` loads the word, reads the clock, loads the word again and starts over when it changed, so neither way of straddling a pause edge survives; a retry follows only a completed write. The writer's own gap between reading its clock and its store (a few instructions) can still make a read straddling an edge differ by that gap, which the header and the stress test say.
- Sleep + Continue starts a new VM (new ledger, start at 0), so nothing persists across a sleep (Notes).

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` (host-test lock) -- expected: all pass, the new tests included
- `python3 scripts/check_layers.py`, fork script tests (`python3 scripts/<name>_test.py` for each), `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: clean
- Games check at both canvases (as the existing suites run it) -- expected: green
- `pio run -e x4pro`, `pio run -e default`, `pio check` (default and `-e x4pro`), `scripts/check_flash_budget.py` build on, build off, compare, all under the build lock -- expected: pass; record the x4pro `firmware.bin` size and SHA-256, and the flash and RAM delta against the epic base Measurement, measured on both sides on this commit
- `sim.sh build x4pro`, open Sudoku, note the clock, pause about 30 s, resume, move, solve a one-blank scratch puzzle -- expected: the time leaves the pause out

**Results (build agent, after the review patches; tree = this story's source, base `9fa5e0c2`):**
- Host: full `ctest` 1820 of 1820 (build/test, Ninja), the 18 new tests among them (PauseClockTest 9, PlayTimeTest 6, MatchTest clock tests 3); the games-check label 124 of 124 runs at both canvases; the implementation agent ran the stress test and the three match tests 30 times each with no failure. Fast checks: `check_layers.py` passes (573 edges), `check_upstream_touches.py` PASS (no ledger edit; every touched file is fork-only), all `scripts/*_test.py` pass, `./bin/clang-format-fix` twice left no change (outside the story's paths: none).
- Firmware, under the build lock with the shared cache: `pio run -e default` SUCCESS (3 m 31 s); `pio check` default PASSED; `pio check -e x4pro` first FAILED on one low `shadowFunction` (`PauseClock.h:79`, fixed by renaming the local), then PASSED. `check_flash_budget.py build on`, `build off`, `compare` (exit 0) and `objects` (exit 0: 49 game objects, largest mutable static 4 B, no static initializer), then `build on` and `compare` again after the rename:
  - x4pro `firmware.bin` games on 5,936,048 B, off 5,680,016 B, difference +256,032 B against the 276,480 B limit (20,448 B to spare). Against the epic base Measurement (on 5,935,632 B, off 5,680,016 B, +255,616 B, `eca7e6c7`): this story adds +416 B of flash, measured the same way on both sides.
  - Static internal RAM: on 187,848 B, off 187,064 B, +784 B against the 1,024 B limit (240 B to spare), equal to the base Measurement (+784 B). By section: `.dram0.bss` +16, `.iram0.text` +760, `.iram0.text_end` +8.
  - The x4pro link resolves the 64-bit atomics: `nm` on the games-on ELF shows `__atomic_load_8` (0x23 = 35 B) and `__atomic_store_8` (0x27 = 39 B) at 0x403863b0 and 0x403863d4, in IRAM; the games-off ELF has neither. The section totals did not grow past the base's (the 74 B fit the totals the base already had; why, an alignment or an earlier symbol in the same section, was not isolated: unmeasured). The 8-byte load and store take ESP-IDF's global spinlock (Design Notes).
  - `sim.sh build x4pro` SUCCESS (24.6 s).
- Simulator (x4pro, a scratch copy of `games/sudoku` packed from the scratchpad whose `setup` returns a solved grid with the top-left cell blank; `games/sudoku` itself is unchanged): opened Sudoku, the first tap selected the blank, Back paused it (log `Playing -> Paused` at 55,600 ms), 35.1 s in the pause menu (log `Paused -> Playing` at 90,715 ms), then the digit 1 was tapped (`Playing -> Over` at 92,907 ms). The end screen shows "Time 0:25" and "Best 0:25". The raw time from the game's first frame to the solve is about 60 s; the pause left out is 35.1 s. Screenshots:
  - `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-pause-clock-screenshots/one-blank-board.png`: the scratch puzzle, one blank at the top left.
  - `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-pause-clock-screenshots/paused-menu.png`: the pause menu over the board, taken about 30 s into the pause.
  - `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-pause-clock-screenshots/solved-time-0-25.png`: the solved screen, Time 0:25 after a 35 s pause.
- x4pro firmware for the device run: `/tmp/claude-0/-home-user-crosshatch-player/0bdf8073-ad95-5670-8b31-e69743e4a448/scratchpad/8.13/firmware-x4pro.bin` (a copy of `.pio/build/x4pro/firmware.bin` as built, which a later build overwrites), 5,936,048 bytes, SHA-256 `0b50111e0e616155d88a2a64d33cda114c9832f32d64e4f97570fa924b58b42b`. Two builds of the same source gave the same size and different bytes (the first 4d7227a8..., before the rename), so the SHA is of this file; the source differs from the first only by the local's name.
- A fresh-tree gate run: not needed (no CI gate or workflow changed).

**Results, follow-up pass (after the memory-order fix; tree = this commit's source, base `9fa5e0c2`):**
- Host: full `ctest` 1820 of 1820; the implementation agent ran the covering tests (133 of 133), the games-check label and 50 repeats of the stress test; `check_layers.py` and `check_upstream_touches.py` pass, all `scripts/*_test.py` pass, `./bin/clang-format-fix` twice left no change.
- Firmware, under the build lock: `check_flash_budget.py build on`, `compare` and `objects` exit 0; `pio run -e default` SUCCESS; `pio check` default and `-e x4pro` PASSED. x4pro `firmware.bin` 5,936,048 B on, 5,680,016 B off, +256,032 B (20,448 B to spare), +416 B against the epic base Measurement (+255,616 B); static internal RAM +784 B, equal to the base. `__atomic_load_8` and `__atomic_store_8` are still 35 B and 39 B in IRAM.
- x4pro firmware for the device run (supersedes the first-pass file above, which was built before the seqlock fence): `/tmp/claude-0/-home-user-crosshatch-player/0bdf8073-ad95-5670-8b31-e69743e4a448/scratchpad/8.13/firmware-x4pro-pass2.bin` (a copy of `.pio/build/x4pro/firmware.bin`, which a later build overwrites), 5,936,048 bytes, SHA-256 `bdfd44e28b52e8f45ccd08a1c21467ee96bef015637e9af46a1ff47fb6bde800`. The simulator run of the first pass was not repeated: the pass-2 source differs from it only in the reader's memory ordering and in tests, comments, and docs, and the match-level tests re-ran.

## Auto Run Result

**Summary.** `ch.time.ms()` now reports play time: milliseconds since the game loaded, less every interval the match spent in `MatchState::Paused` (Back's and Home's pause menus), for every game. A new header-only ledger, `GameCore::PauseClock` (one `std::atomic<uint64_t>`, no lock in the binding), is written by the loop task on the Paused edge in `GameMatchActivity::handle()` through `GameVM::matchPaused()/matchResumed()` and read by the binding through `HostPorts::paused`; `ch.timer` and `GameTimer` keep the raw clock, Sudoku's sources are unchanged. The simulator run shows a 35 s pause left out of the solve time.

**Files.**
- `lib/GameCore/PauseClock.h` (new): the ledger.
- `lib/GameScript/ChBindings.h`, `ChBindings.cpp`: `BindingContext::paused`, `timeMs` reports play time, comments.
- `lib/GameScript/LuaGame.h`, `LuaGame.cpp`: `HostPorts::paused` (trailing, default null), copied into the bindings on load.
- `src/games/GameVM.h`, `GameVM.cpp`: owns the ledger, passes it, `matchPaused()/matchResumed()`.
- `src/activities/games/GameMatchActivity.cpp`: toggles it on the Paused edge.
- `test/game_core/PauseClockTest.cpp` (new), `CMakeLists.txt`; `test/game_script/HostBindingsTest.cpp` (PlayTimeTest); `test/game_script/harness/GameMatchTest.cpp` (three match-level tests); `games_check/GamesCheckRig.h`, `ScriptVm.cpp` (the check VM runs the device's path).
- `docs/crosshatch/api-level-1.txt` (comment only, CRC unchanged), `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md` (one row), `test/game_script/fixtures/README.md` (two sentences).
- This plan and `story-pause-clock-screenshots/` (three images).

**Review.** Pass 1 (commit 38b9104c): four context-free lenses, 19 findings: patches 13 (medium 4: stress-test starvation, single-writer citation, Home and two-pause match tests, the `playMs` re-validation loop; low 9, the `pio check` shadowFunction among them), rejected 6, deferred 1. Follow-up pass 2 (this commit, four fresh lenses over `9fa5e0c2..38b9104c`): 23 findings, high 0, medium 1, low 18, false 4; patched 11 (the medium: `playMs` becomes a standard seqlock reader with an acquire fence, since two acquire loads around a plain clock read do not hold the clock read before the reload; ten lows: the test-only public `paused()`/`pausedMs(now)` and the dead `GamesCheckRig::pauses()` removed, stale "since load" comments and the plan's "wait-free" wording fixed, slow-restart step 9 made one instruction, the API note and seed row say what stays raw, the header trimmed), deferred 4 (the busy-wait and watchdog interplay, the stress test not showing the retry ran, the unmeasured spinlock cost, and the lifecycle-path tests, extending the first-pass item), rejected 8 with reasons in the log. `lenses_ran`: blind-hunter, edge-case-hunter, verification-gap, intent-alignment (both passes). Follow-up review recommended: false (the pass patched no high).

**Verification.** See Verification, Results: host tests 1820 of 1820, layer and ledger checks, formatting twice clean, `pio run` x4pro and default, `pio check` default and x4pro, flash budget +256,032 B (+416 B against the epic base) and RAM +784 B (equal to base), `sim.sh build x4pro` and the Sudoku pause run.

**x4pro firmware for the device run.** Path `/tmp/claude-0/-home-user-crosshatch-player/0bdf8073-ad95-5670-8b31-e69743e4a448/scratchpad/8.13/firmware-x4pro-pass2.bin`, 5,936,048 bytes, SHA-256 `bdfd44e28b52e8f45ccd08a1c21467ee96bef015637e9af46a1ff47fb6bde800` (built from this commit's source, after the seqlock fence; the first-pass file `firmware-x4pro.bin`, SHA 0b50111e..., is superseded). Flash +256,032 B on against off, +416 B against the epic base Measurement; static RAM +784 B, equal to the base.

**Screenshots.** The three paths under Verification, Results.

**Formatting-only changes outside my paths:** none. **Residual risks:** the 8-byte atomics take ESP-IDF's global spinlock briefly on every `ch.time.ms()` and every pause edge (never across blocking work); a game that busy-waits on `ch.time.ms()` stalls while paused (only the 3 s raw watchdog ends it; documented, only a fixture does it); the writer's clock-to-store gap can skew a read straddling an edge by a few instructions' worth of time; the on-device pause check (a real Sudoku run on the S3) is the owner's; the first-pass simulator run was not repeated after the pass-2 memory-order fix (tests and the build were).

---
title: 'ch.timer, ch.store, ch.time, ch.log, and ch.api'
type: 'feature'
ticket: '10'
created: '2026-09-27'
status: done
baseline_revision: 'aebea6f4d9cb77a5737eac949f39b57775aae2a9'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `ch` lacks `api`, `timer`, `store`, `time`, and `log`; `print` is a no-op; a no-argument `math.randomseed()` calls `time()` (newlib lock) from game code; nothing delivers `timer` events or holds the store blob entry 12 persists.

**Approach:** Add three ports (`GameCore::IClock`, `GameCore::IGameLog`, and a latest-wins `GameScript::StoreSlot` the match owns) and a cross-task `GameTimer`, passed to `LuaGame` as one `HostPorts` bundle; bind `ch.api`, `ch.timer`, `ch.store`, `ch.time.ms`, `ch.log`, and `print` over them; the match polls the timer and posts `timer` through the GameVM input queue.

## Boundaries & Constraints

**Always:**
- `api-level-1.txt` is the contract (`timer_min_ms 1000`, `timers_pending_count 1`, `store_bytes 4096`); no entry changes, so `API_SURFACE_CRC` stays.
- `print` and `ch.log`: Lua `print`'s formatting (`tostring`, tab-separated), one line of at most `LOG_LINE_BYTES` = 160 B cut at a UTF-8 boundary, control bytes shown as spaces, written as `LOG_INF(<game id>, "%s", line)`.
- Every binding that takes a lock or writes the log (timer, store, log) brackets it with `enterLockedSection`/`leaveLockedSection`; nothing inside raises or calls Lua.
- `math.randomseed()` with no argument reseeds from `IRandom`; with arguments it is Lua's (returns the two seeds). `luaopen_math`'s own `luaL_makeseed` (`time()`) at VM creation stays: it runs in `load()` before any game code.
- `ch.store.set` encodes through the 2.6 codec into the existing codec scratch (arena reserve); a breach is a sticky `ScriptError` (a script `pcall` cannot keep the game going, AD-10). The slot is dirty only when the bytes differ.
- `GameScript`/`GameCore` stay free of HAL/Arduino/Logging/FreeRTOS; locals under 256 B; fixtures only in `test/game_script/fixtures/`.

**Never:** `GameSaveStore`, `store.bin`, `GameAssets` store loading (entry 12); pass-mode timer seats; long press/swipe (entry 11); editing `lib/lua`, `ci.yml`, `.skills/`, the ledger, or the submodule pointer.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Timer | `ch.timer.after(1500)` at t | one `{kind = "timer"}` to `input` at t + 1500; a move it returns is applied | — |
| Replace / cancel | `after(5000)` then `after(1500)`; `after` then `cancel()`; re-armed after firing, before delivery | only the newest fires; nothing; the stale event is dropped | — |
| Short timer | `after(999)`, `after(-1)`, non-integer | — | `ScriptError` "bad argument #1 to 'after' (at least 1000 ms)" |
| Store | `set(t)` then `get()`; nothing set | an equal fresh table; `{}` | — |
| Store dirty | `set` same bytes twice; different bytes | dirty once; dirty again | — |
| Store limit | `set` > 4096 B, also under `pcall`; non-table; function inside | — | sticky `ScriptError` "...ch.store.set: the store is too large (over 4096 bytes)"; argument / codec error |
| Time | `ch.time.ms()` | clock ms since `load()` | — |
| Log | `print('a', 1, nil)`, `ch.log(t)` with `__tostring`, a 300 B string | "a\t1\tnil"; its text; 160 B | — |
| Deep binding | log/store call with < 4 KiB stack left | — | Lua error "... script recursion too deep" |
| API | `ch.api` | 1 (`API_LEVEL`) | — |
| randomseed | `math.randomseed()`; `math.randomseed(42)` | no `time()` call; seeds 42, 0 as in Lua | — |

</frozen-after-approval>

## Code Map

- `lib/GameScript/LuaGame.{h,cpp}` -- constructor (5 args, 45 test sites), `load()` sets `bindings.*`, `close()`/`abandon()` reset them; `scratch` is `SCRATCH_BYTES` = `scratchBytes(STORE_LIMIT)` (13,720 B) from the reserve, free whenever Lua runs (every encode's output is copied by `Session` before the next call); `pushEvent` switch.
- `lib/GameScript/ChBindings.{h,cpp}` -- `BindingContext` (add clock, start ms, timer, store, scratch, log), `openChLibrary`, `enterLockedSection`.
- `lib/GameScript/Sandbox.cpp` -- `silentPrint` (replace with `ch.log`), `randomseed` seeding at open; `lib/lua/src/lmathlib.c:631` `math_randomseed` (no-arg path calls `luaL_makeseed` → `time()`).
- `lib/GameScript/CallGuard.{h,cpp}` -- `Fault`, `trip`, sticky re-raise via hook count 1, `hasHeadroom`.
- `lib/GameScript/GameInput.h`, `lib/GameCore/GameEvent.h` -- `EventKind` (add `Timer`), event fields; `Session::handle` keeps moves from any non-runtime event already.
- `src/games/GameVM.{h,cpp}` -- `create`, `run` loop (`queue.pop`), `postInput`, static `abandon`; `GameRandom` as the provider pattern.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `onEnter` (create VM), `loop`, `onExit`/`stopStuckVm` abandon paths.
- Simulator shim: `ulTaskNotifyTake` ignores its timeout, so the VM task cannot time its own wait; the loop task polls instead.
- `lib/Logging/Logging.cpp` -- `logPrintf` origin is a runtime string; 256 B entry buffer (prefix about 50 B).
- `test/game_script/{LuaGameFixture.h,SandboxTest.cpp (PrintWritesNothing, globals),CMakeLists.txt}`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/{IClock.h,IGameLog.h}`, `GameEvent.h` -- ports; `EventKind::Timer` and a `serial` field.
- [x] `lib/GameScript/{GameTimer,StoreSlot}.{h,cpp}` -- per Design Notes.
- [x] `lib/GameScript/CallGuard.{h,cpp}` -- `Fault::Codec`, `raise(L, message)` (sticky, chunk:line prefix), `BINDING_HEADROOM_BYTES` 4 KiB.
- [x] `lib/GameScript/ChBindings.{h,cpp}` -- context fields; `ch.api`, `ch.timer`, `ch.store`, `ch.time`, `ch.log` (exported for `print`), `LOG_LINE_BYTES`.
- [x] `lib/GameScript/Sandbox.{h,cpp}` -- `print` = `ch.log`; wrapped `math.randomseed`.
- [x] `lib/GameScript/LuaGame.{h,cpp}` -- `HostPorts`, owned `GameTimer`, `timer()`, start ms; `Timer` event; timer cleared on close.
- [x] `src/games/{GameClock,GameLog}.{h,cpp}`, `GameVM.{h,cpp}` -- providers; `create(assets, canvas, id, store)`, `pollTimer()`, stale timer events dropped, `abandon` returns whether the task is gone.
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- PSRAM slot before the VM, `pollTimer()` each Playing pass, slot leaked with a VM leaked alive.
- [x] `test/game_script/{HostBindingsTest,StoreSlotTest,GameTimerTest}.cpp`, fixture, `SandboxTest.cpp`, `CMakeLists.txt`, construction sites -- every matrix row; `time()` counted by an interposer.
- [x] `test/game_script/fixtures/timer/{manifest.json,main.lua}` -- the simulator game.

**Acceptance Criteria:**
- Given `ctest`, then every suite passes; given `pio run -e x4pro` and `-e default`, then both build; `pio check` (both) finds no defects.
- Given the simulator with `fixtures/timer` in `fs_/.games/timer/`, when it opens, then it redraws after its delay without input (screenshots beside this plan) and the log shows its `print` line tagged `timer`.
- `./bin/clang-format-fix` leaves no diff; `check_upstream_touches.py` passes.

## Implementation Notes

- Implemented directly (no coding subagent in this session). New: `lib/GameCore/{IClock,IGameLog}.h`, `lib/GameScript/{GameTimer,StoreSlot}.{h,cpp}`, `src/games/{GameClock,GameLog}.{h,cpp}`, `test/game_script/{HostBindingsTest,GameTimerTest,StoreSlotTest}.cpp`, `fixtures/timer/`. Changed: `GameEvent.h` (`Timer`, `serial`), `GameInput.h` (comment), `CallGuard` (`Fault::Codec`, `raise`, `BINDING_HEADROOM_BYTES`), `ChBindings`, `Sandbox`, `LuaGame` (`HostPorts` replaces the `IRandom&` parameter), `GameVM`, `GameMatchActivity`, the fixture header, `CMakeLists.txt`, and the 45 construction sites (mechanical `random` to `ports`).
- Tabs stay in log lines (the matrix's "a\t1\tnil"); other control bytes, embedded NULs included, become spaces. Arguments after the 160th byte are not converted, so their `__tostring` does not run.
- `ch.store.set`'s codec errors (too large, and any other, e.g. `bad_type`) all raise the sticky `Fault::Codec`; a non-table argument is an ordinary argument error. The message carries the calling chunk and line when the caller is Lua (`main.lua:4: ch.store.set: ...`), and none when `pcall` calls the binding directly.
- The API list already had every entry this story ships (`ch.api`, `ch.timer.*`, `ch.store.*`, `ch.time.ms`, `ch.log`, `print`, `event timer`, `timer_min_ms`, `timers_pending_count`, `store_bytes`), so `API_SURFACE_CRC` stays 0xCAF107E5. `LOG_LINE_BYTES` is not listed: a game cannot observe it.
- Reserve: unchanged. Store encode and `get`'s copy reuse the 13,720 B scratch (`scratchBytes(STORE_LIMIT)`) already sized for the store in entry 8; the existing `static_assert` still holds.
- `time()`: the interposer in `HostBindingsTest` counts exactly one call per `load()` (luaopen_math's `luaL_makeseed`) and none in a no-argument `math.randomseed()`.
- Handoff to entry 12: `StoreSlot::restore` takes the bytes as given, so `GameSaveStore` must restore only a blob whose header checks and whose value is a table (`ch.store.get` returns whatever the bytes decode to).
- x4pro flash 5,796,314 B (entry 9: 5,792,914 B, the heap-region fix in between).

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time against `review.diff`, 98 kB: blind-hunter with a floor of 10, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 0, medium 0, low 7, false 5, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | verification-gap | `GameVM::pollTimer`, the stale-event drop in `GameVM::run`, and `abandonVm`'s slot leak run only in the simulator/device; the host test mirrors them | low | Defer (`deferred-work.md`): entry 1's GameVM harness gap; the simulator run and `GameTimerTest`/`AnEventFiredBeforeACancelIsDropped` cover the boundary. |
| 2 | blind, verification-gap | The store and timer bindings' locked sections are not observed by a test (only the log's is, `TheLogIsWrittenInsideALockedSection`) | low | Rejected: observing them needs a test seam (a virtual slot or timer); three reviewed lines each. |
| 3 | blind, edge | `ch.store.get` returns a non-table if the slot holds a valid non-table encoding | low | Rejected: only `restore` (entry 12, not yet called) can put one there; handed to entry 12 in Implementation Notes. |
| 4 | blind | `StoreSlot::restore` accepts bytes without decoding them | low | Rejected: entry 12 owns the blob header and validation (same note). |
| 5 | blind | Arguments past the 160th byte are never converted, so their `__tostring` does not run | low | Rejected: deliberate bound (Implementation Notes). |
| 6 | blind | `print`'s headroom error names `ch.log` | low | Rejected: cosmetic; `print` is `ch.log`. |
| 7 | blind | A timer keeps firing after the round is over | low | Rejected: AD-23 fires it regardless; Session discards moves after over, and a game can cancel. |
| 8 | blind | Timer latency depends on the loop cadence | false | The idle loop sleeps in 10 ms slices up to 50 ms (`main.cpp`), against a 1,000 ms minimum; the simulator ticks landed 1 to 53 ms late. |
| 9 | blind | A timer event into a full queue drops the oldest event | false | AD-5's documented queue policy, logged by `postInput`; unchanged. |
| 10 | blind | `GameEvent::serial` wraps | false | A stale event is accepted only if exactly 2^32 arms happen between its firing and its delivery. |
| 11 | blind | The store slot allocated in `onEnter` leaks when `GameVM::create` fails | false | It is a member (`storeStorage`, `store`), freed with the activity. |
| 12 | edge (claim) | "control bytes shown as spaces", yet tabs stay | false | The same rule says tab-separated and the matrix pins "a\t1\tnil". |

Edge-case claims check: the Intent and Tasks claims held (locked sections around every lock and log write, the store in the existing scratch, dirty only on change, `LOG_INF` with the id, `luaopen_math`'s one `time()` at load, the poll through the input queue, the slot leaked with a VM leaked alive). Deletion check: `silentPrint`'s contract (never stdout) is kept and still pinned (`PrintIsChLogNeverStdout` captures stdout). Intent-alignment: readings are (a) the ports, bindings, and a loop-task poll feeding the input queue, as the Approach states, and (b) the VM task timing its own wait; the diff implements (a) (the simulator's `ulTaskNotifyTake` ignores timeouts, so (b) would not run there). The intent's expectations live at the Lua API and the GameVM queue; the tests exercise the API through `LuaGame`, `Session`, and a mirrored poll, with the queue path itself in the simulator.

## Design Notes

**Timer.** `GameTimer` (std::mutex): `arm(now, ms)` and `cancel()` bump a serial; `takeDue(now, serial&)` (loop task, `GameVM::pollTimer`) fires once; `accepts(event)` (VM task, before `handle`) drops a timer event whose serial is no longer current. Loop-task polling works in the simulator (whose wait ignores timeouts) and keeps the VM task blocked forever between events.

**Store.** `StoreSlot` over caller storage (`STORE_LIMIT` B of PSRAM), std::mutex: `restore(bytes)` (entry 12, not dirty), `post(bytes)` (copy and dirty only when different; an empty slot differs from everything), `read(out)`, `takeIfDirty(out)` (entry 12's flush). `get` copies the slot into the scratch under the lock, then decodes outside it. Reserve: store encode and get reuse the one 13,720 B scratch, so the 16 KiB reserve (scratch + Session about 1.8 KB, already `static_assert`ed) is unchanged. The match owns the slot so it outlives the VM for entry 12's `onExit()` flush; if the VM is leaked alive, the slot is leaked too.

**Stack.** A binding call gets at least 2 KiB (the call hook's check). The codec at depth 16 needs about 1.8 KB on the S3 (2.8) plus Lua allocations; `logPrintf` has a 256 B buffer plus `vsnprintf` and the serial write (about 1.5 KB); so log and store bindings first require `BINDING_HEADROOM_BYTES`, as `require` does for the parser.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `pio run -e default`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (default and `-e x4pro`) -- SUCCESS, no defects.
- `sim.sh build x4pro`, `start x4pro`, open the timer fixture, `ss` before and after the delay, `log` -- screenshots in `story-host-bindings-screenshots/`.
- `./bin/clang-format-fix`, `python3 scripts/check_upstream_touches.py` -- no diff, PASS.

**Verification record** (2026-09-27, working tree before commit, on aebea6f4):
- Host: `ctest` 549/549 passed (`GameScriptTest` 119: new 16 `HostBindingsTest`, 4 `GameTimerTest`, 4 `StoreSlotTest`; `SandboxTest.PrintIsChLogNeverStdout` replaces `PrintWritesNothing`). Every matrix row has a passing test: timer through the queue, replace, cancel, stale drop, delays 999/0/-1/1.5, store round trip and empty, dirty tracking, 4,096 B fits and 4,097 B (and 5,000 B under two `pcall` forms) end in the sticky error, non-table and `bad_type`, `ch.time.ms` 0 then 1234 on the fake clock, log lines (tabs, `__tostring`, 160 B, UTF-8 cut, control bytes), headroom for `print`/`get`/`set`, `ch.api` 1 integer, `randomseed()` with no `time()` call (one per `load()`), `randomseed(42)` returning 42, 0.
- `pio run -e x4pro` SUCCESS (RAM 31.1 %, Flash 88.4 %, 5,796,314 B); `pio run -e default` SUCCESS (RAM 17.7 %, Flash 85.6 %); `pio check -e x4pro` and `-e default` with the three `--fail-on-defect` flags: no defects.
- `./bin/clang-format-fix`: exit 0, no files outside this story touched; `check_upstream_touches.py`: PASS (trial merge clean; fork-only paths).
- Simulator x4pro (`sim.sh build x4pro` SUCCESS), `fixtures/timer` in `fs_/.games/timer/`, scripted run with no input after opening the game, screenshots in [story-host-bindings-screenshots/](story-host-bindings-screenshots/): `timer-waiting.png` (Ticks 0 of 3, drawn at 1 ms), `timer-tick1.png` (Ticks 1, last tick at 3001 ms, saved ticks 1), `timer-tick2.png` (6053 ms), `timer-done.png` (Ticks 3, Done, 9053 ms). Log: `[INF] [timer] armed\t3000\tms; api\t1`, then `[INF] [timer] tick at\t3052\tms`, `6083`, `9120` in a live run, "Round over at ver 4", and Back logged "VM stopped" and returned to Games.

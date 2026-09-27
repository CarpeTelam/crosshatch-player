---
title: 'Sandbox, budgets, and the heap cap'
type: 'feature'
ticket: '7'
created: '2026-09-27'
status: 'built'
baseline_revision: '6c6f32f0fa8f6b4c656f6dd406a4fe822d13594f'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 2
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Entry 1's VM has no instruction budget, cancel, heap cap policy, or sandbox: `load` is open, `require` is missing, `math.random` is unseeded from `IRandom`, a runaway script hangs the `GameVM` task, deep C recursion can overflow its 16 KB stack, and a stuck VM is leaked whole.

**Approach:** A `CallGuard` hook (count every 1,000 instructions plus call events) armed in the one trampoline enforces the sticky 2 M budget, the cancel flag, and a C-stack headroom check (owner decision, 2026-09-27); `luaAlloc` caps Lua's bytes at 256 KiB; a sandbox opener builds the AD-6 globals with a source-table `require`; `GameVM` gains cancel, join, and abandon, and the match keeps the loop fast while a callback runs.

## Boundaries & Constraints

**Always:**
- AD-5/AD-6/AD-14 as written; Lua unmodified, `library.json` unchanged; the stack stays 16 KB in internal RAM.
- `GameScript` stays free of HAL/Arduino/`Logging`/FreeRTOS: the stack floor is passed in as an address. `lua.h` users include `<climits>` first and `static_assert(sizeof(lua_Integer) == 8)`. No allocation outside the arena on the VM task; locals under 256 B.
- A fault raised by the guard is sticky: the script's own `pcall`/`xpcall` catches it at most once per level and the callback still ends with it.
- Abandon frees only what the stuck task cannot be holding; the rest (the `GameVM` object with its mutexes) is leaked on purpose. Handoff 2.7: `LuaGame::abandon()` drops `L` without `lua_close`; suspend, then check `inSwap`, then delete.
- Fixtures only in `test/game_script/fixtures/`.

**Never:** `print`/`ch.log` (2.10), `apply`/`status`/`Session` (2.8), the match's Paused/Leaving/Error states (2.13); `LUAI_MAXCCALLS`/`MAXCCALLS` defines or a `setmetatable` wrapper (owner questions, Design Notes); editing `scripts/`, `.github/`, the ledger, `ci.yml`, `.skills/`, or the submodule pointer.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Runaway | `while true do end` in load, `setup`, `draw`, or `input`; also inside `pcall` | ends after 2 M instructions | `ScriptError` "main.lua:N: instruction budget exceeded" |
| Heap bomb | table/string growth past 256 KiB of Lua bytes | allocation refused, GC retried | `ScriptError` "not enough memory" |
| C recursion | `local function f() pcall(f) end f()`; recursive `__index` function | stops with at least 2 KB of stack left | `ScriptError` "script recursion too deep" |
| Sandbox | `io`, `os`, `debug`, `load`, `loadfile`, `dofile`, `load(string.dump(f))` | nil globals | `ScriptError` "attempt to ... nil value" |
| `require` | module present / missing / binary / circular / erroring | returns and caches its value (nil → true) / fails | `ScriptError` "module 'x' not found", "attempt to load a binary chunk", "circular require of 'x'" |
| Cancel | cancel flag set during a callback, even under `pcall` | callback ends at the next hook event | `Cancelled`, no error view |
| Stop | Back while looping or idle | task joins within 500 ms | join timeout → abandon |
| Stuck in C | pattern backtracking past 500 ms | device: task suspended once in Lua and `inSwap` clear, deleted, PSRAM freed, object leaked; simulator: all leaked | `LOG_ERR` |

</frozen-after-approval>

## Code Map

- `lib/GameScript/LuaGame.{h,cpp}` -- `enter()` is the trampoline to arm; `Outcome` gains `Cancelled`; `close()`; `loadEntry` opens libraries (move to the sandbox opener).
- `lib/GameScript/ChBindings.{h,cpp}` -- `BindingContext` (one pointer in `lua_getextraspace`): add `sources` and `guard`.
- `lib/GameScript/ArenaAllocator.{h,cpp}` -- `luaAlloc` (osize is the old size when `ptr` is set); add the Lua byte counter and limit.
- `lib/GameScript/GameSources.h` `find` -- `require` resolves here.
- `src/games/GameVM.{h,cpp}` -- `stop`, `run`, `taskMutex`/`taskAlive`, stack high-water log line; `GameArena::release`, `GameAssets` (add `release`).
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `onExit` leak path becomes abandon; add `skipLoopDelay()` (virtual in `Activity.h:44`, read in `main.cpp:805`).
- Simulator shim (`crosspoint-simulator@8699595` `src/freertos/task.h`): no `vTaskSuspend`, `eTaskGetState`, or `pxTaskGetStackStart`; `vTaskDelete(other)` only detaches. Device: `pxTaskGetStackStart` (`freertos/idf_additions.h`, lowest address), `INCLUDE_vTaskSuspend 1`.
- `lib/lua/src/lgc.c:980` -- `GCTM` sets `allowhook = 0`: no hook runs inside `__gc`. `ldo.h:61` / `lstrlib.c:380` -- `LUAI_MAXCCALLS` and `MAXCCALLS` (200) are `#if !defined` knobs.
- `test/game_script/LuaGameTest.cpp` -- fixture class to extend (multi-module sources); `CMakeLists.txt` globs `lib/GameScript/*.cpp`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameScript/CallGuard.{h,cpp}` -- `VM_STACK_BYTES`, budget, interval, headroom constants; `install`, `arm`, `requestCancel`, `setStackFloor`, `fault`, `message`, `minHeadroom`, the hook.
- [x] `lib/GameScript/Sandbox.{h,cpp}` -- open base/table/string/math/utf8, drop `load`/`loadfile`/`dofile`, `require`, seed `math.random` from `IRandom`; wrap `xpcall`'s handler so a guard fault skips it (Plan Change Log 1).
- [x] `lib/GameScript/ArenaAllocator.{h,cpp}` -- `LUA_HEAP_BYTES`, `luaBytes`, `luaLimit`/`setLuaLimit`; `luaAlloc` enforces it on growth.
- [x] `lib/GameScript/ChBindings.h`, `LuaGame.{h,cpp}` -- context fields; arm per entry; fault precedence; `Cancelled`; `requestCancel`, `setStackFloor`, `inLua`, `abandon`.
- [x] `src/games/GameVM.{h,cpp}`, `GameAssets.h` -- stack floor at task entry; `cancel`, `join`, `stop`, `busy`, static `abandon`; `Cancelled` is not a failure.
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- abandon on join timeout; `skipLoopDelay()`.
- [x] `test/game_script/fixtures/faults/*.lua`, `fixtures/loop/{manifest.json,main.lua}` -- one file per matrix fault; `loop` is the simulator game.
- [x] `test/game_script/{CallGuardTest,SandboxTest}.cpp`, `LuaGameTest.cpp`, `ArenaAllocatorTest.cpp`, `CMakeLists.txt` -- every matrix row; globals equal the api-level-1 base set plus `ch` and `require`; stack cost per level measured and printed.
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- the hook-blind recursion paths (Design Notes).

**Acceptance Criteria:**
- Given `ctest`, then every suite passes; given `pio run -e x4pro` and `-e default`, then both build.
- Given the simulator with `fixtures/loop` in `fs_/.games/loop/`, when the canvas is tapped, then the error view shows the budget message within about a second, Back returns to Games, and Home still responds; when the loop is running and Back is pressed, then the log shows the VM cancelled and joined.
- `./bin/clang-format-fix` leaves no diff; `check_upstream_touches.py` passes.

## Implementation Notes

- Implemented directly (no coding subagent in this session). New: `lib/GameScript/{CallGuard,Sandbox}.{h,cpp}`, `test/game_script/{CallGuardTest,SandboxTest}.cpp`, `LuaGameFixture.h` (the fixture class moved out of `LuaGameTest.cpp` so three suites share it), fixtures `faults/` (16 files), `modules/` (6), and `loop/`. Changed: `ArenaAllocator`, `ChBindings.h`, `LuaGame`, `GameVM`, `GameAssets.h`, `GameMatchActivity`, the test `CMakeLists.txt`, and two entries in `deferred-work.md`.
- `CallGuard::MESSAGE_CAPACITY` is 128, not 96: `short_src` (up to 59 chars) plus the line and the text overflowed 96 (`-Wformat-truncation`).
- Without the guard, the owner's `nested_pcall` fixture does not fail at all on the host: Lua's own "C stack overflow" is caught by the script's `pcall` and the script carries on; the test pins this, which is why the stack fault is sticky.
- The `loop` fixture grew two bands beyond the three matrix loops: "Slow C calls forever" (a backtracking `string.find` in a loop, so the budget does not end it within seconds) shows Back cancelling a running callback, and "Stuck in one C call" (2^40 steps in one `string.find`) shows the join timing out and abandon; the budget otherwise ends every loop before a Back can land.
- The simulator builds at `-O0` (the native platform sets no `-O`), where one `pcall` level costs more than the 2 KB headroom, so its modelled floor was passed by one level (hook low-water 0 B) before the guard raised; the thread's real stack is 8 MB, so nothing overflowed. The device builds at `-Os`, where a level is about 592 B.
- Abandon in the simulator returns at once (no 500 ms wait) since no wait can help there.
- `./bin/clang-format-fix` also reformats `test/game_core/ApiLevelTest.cpp` and `ManifestCheckTest.cpp` (entry 4's files); those edits were reverted to keep this diff to its story.
- Change 2 in `Sandbox.cpp`: `setmetatable` is written out (lbaselib's lines plus the `__gc` check) rather than wrapped, so its argument errors still name it; the table wrappers call the originals, so their own argument errors name `'?'` (cosmetic). The `__gc` check is a raw get, as Lua uses to decide finalization. `luaopen_math` and a no-argument `math.randomseed()` still call `luaL_makeseed`, which calls `time()`: the only remaining newlib-lock window for abandon inside Lua.
- The `loop` fixture's backtracking pattern changed from `('a?'):rep(n)` (now refused by `MAXCCALLS`) to `('.-'):rep(k) .. 'b'` with k within 16, and its last two bands now show the watchdog.
- `pio check -e default` first stopped with `MissingPackageManifestError` while another story's build had half-installed the shared Arduino libraries; after their reinstall it passes, and x4pro and default were rebuilt.
- Review loop 1: `xpcall` in `_G` is a C closure over the base `xpcall` whose handler is wrapped (`guardedHandler`); the rest of the code was kept as reviewed. Fixtures `loop_in_xpcall_handler.lua` and `recurse_in_xpcall_handler.lua` were added; before the wrapper, `AnXpcallHandlerCannotOutliveAGuardFault` hung until `timeout 20` killed it (exit 124). `loop_in_pcall.lua` now loops in `draw`, so the cancel test cancels a running loop under `pcall` instead of possibly cancelling before setup.

## Plan Change Log

1. Review pass 1 (edge-case lens, verified high): an error the CallGuard raises from its hook reaches an `xpcall` message handler while hooks are still off (`luaG_errormsg` calls the handler inside `luaD_hook`, which set `allowhook = 0`), so `xpcall(loop, function() while true do end end)` hung the VM past the budget and a handler recursing through `pcall` ran without the headroom check. Amended: Tasks (Sandbox wraps `xpcall`) and Design Notes (Guard). Known-bad state avoided: a script handler running unguarded after a guard fault. KEEP: all code and tests as reviewed in pass 1 (CallGuard, sticky re-raise, fault precedence in `enter()`, `luaAlloc` cap, `require`, seeding, GameVM cancel/join/abandon, `skipLoopDelay`, fixtures, stack measurements); the loopback adds only the `xpcall` wrapper, its two fixtures and test, and the `loop_in_pcall` fixture move to `draw`.

2. Independent review of the commit and owner decisions (2026-09-27; spine AD-4, AD-5, AD-6 "Amended 2026-09-27", the epic's last Note). Triggering findings: H1, C loops that run no Lua instructions (`table.move` over a huge range, `table.insert` with a huge `__len`, pattern backtracking) escape budget and cancel; the hook-blind parser, pattern-matcher, and `__gc` recursion (this plan's owner questions); M1, the base `print` writes to stdout under newlib's lock, abandon had no way to respect a binding that holds a lock, and `LuaGame.h` overstated what a call touches; L1, `trip()` formatted the stack and cancel messages with about 2 KB left. Amended, as one commit on top of the first: a 3 s wall-clock watchdog in `GameMatchActivity::loop` (cancel, abandon after 500 ms, error view "stopped responding: one call ran over 3 s") on `GameVM::runningForMs`; `table.move`/`insert`/`remove` refuse more than `TABLE_ELEMENTS_LIMIT` (65,536) elements; `lib/lua/library.json` passes `LUAI_MAXCCALLS=30`, `MAXCCALLS=16`, and `l_randomizePivot=sizeof` (Design Notes, "Lua build defines"); `setmetatable` refuses a metatable with a raw `__gc` field (`api-level-1.txt` moves it to `fn setmetatable(t, mt) -> table` and adds `limit table_elements_count 65536`; `API_SURFACE_CRC` 0x93ECD968); `print` is a no-op until entry 10; `enterLockedSection`/`leaveLockedSection` count locked bindings and abandon skips a task inside one; `require` refuses to compile with under 10 KB of stack free; `trip()` points at static literals for the stack and cancel faults. Known-bad state avoided: a script hanging the task in C with the game frozen until Leave, or crashing it through parser, matcher, or finalizer recursion. KEEP: everything in change 1. Not in this round: L2 (arena = Lua cap plus scratch reserve) moves to entry 8.

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time against `review.diff`, 67 kB: blind-hunter with a floor of 9, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 1, medium 1, low 6, false 5, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | edge | An error raised from the hook reaches an `xpcall` message handler with hooks off, so the handler escapes budget, cancel, and headroom | high | bad_plan, loopback 1 (Plan Change Log 1). Reproduced: `AnXpcallHandlerCannotOutliveAGuardFault` hung (exit 124 under `timeout 20`) before the wrapper, passes after. |
| 2 | blind | A looping `__gc` finalizer hangs `close()` (and any GC step), since hooks are off in `GCTM` | medium | defer: not caused by this change (Lua's `lgc.c:980`); recorded in this story's owner-question entry in `deferred-work.md`; on the device `close()` runs with `inLua` set, so abandon ends it after Leave. |
| 3 | blind, verification-gap | The cancel test's `loop_in_pcall` case could cancel during load, before the `pcall` loop runs | low | patch with loopback 1: the fixture loops in `draw`, and both cases start first, then cancel during `draw`. |
| 4 | verification-gap | `GameVM::cancel`, `join`, `abandon`, `deleteIfStuckInLua`, the device stack floor, and `Cancelled` handling in `run()` have no host test | low | defer: device code outside the host suites by design (entry 1); simulator evidence covers cancel, join, and the simulator's leak path; the device run is recorded in `deferred-work.md`. |
| 5 | verification-gap | `GameMatchActivity::skipLoopDelay` is not observed by any test | low | Rejected: a one-line override; no activity harness exists, and adding one is more than a direct fix. |
| 6 | blind | Between callbacks `skipLoopDelay` is false, so power saving may slow the first loop pass of a callback | low | Rejected: AD-5 specifies exactly "while a callback runs". |
| 7 | blind | A cancel request is permanent, so a cancelled `LuaGame` cannot start again | low | Rejected: documented in `CallGuard.h`; a cancelled VM always ends (AD-5), and a new match makes a new `GameVM`. |
| 8 | edge (claim) | Tasks list `minHeadroom`; the code exposes `deepestAddress()` and `stackFloor()` instead | low | Rejected: the only fix is a plan wording edit or an unneeded accessor; the log line computes the headroom from both. |
| 9 | blind | `std::stoi(frontText())` in the measurement test throws on non-numeric text | low | Rejected: test-only; a failure there already shows as a test error. |
| 10 | blind | The hook dereferences `bindingContext(L)->guard` without a null check | false | The hook is installed only by `LuaGame::start()` after the context is set; `close()` clears the context after `lua_close`, during which no hook runs. |
| 11 | blind | The sticky re-raise allocates the message string, which can fail in a full arena | false | A memory error still unwinds, and `enter()` reads the recorded fault first, so the outcome and message are the guard's. |
| 12 | blind | `deleteIfStuckInLua` sleeps holding `taskMutex` | false | Only `notifyTask` (loop task, the caller itself) and the VM task (suspended) take it. |
| 13 | blind | The budget message may name the wrong line after a re-raise | false | The message is formatted once, at the first trip, from the hook's `lua_Debug`; re-raises reuse it (`main.lua:7` in the simulator is the loop line). |
| 14 | blind | `vTaskResume` after a failed check could wake a task blocked in `ulTaskNotifyTake` | false | The run loop treats a wake as a spurious notification and re-checks `quitRequested`, which cancel has set. |

Intent-alignment: readings are (a) the approach as written (guard, cap, sandbox, cancel/join/abandon, loop speed) and (b) the Problem's stronger "deep C recursion can overflow" closed entirely. The diff implements (a); the tests exercise `LuaGame` and the arena on the host and `GameVM` only in the simulator. (b) diverges at the parser, pattern matcher, and `__gc`, which the hook cannot see; those are the owner questions in Design Notes. The "stuck VM is leaked whole" problem is fixed on the device only; the simulator still leaks, as documented.

Pass 2 (after loopback 1, against the rewritten diff; delta: `guardedXpcall`, `guardedHandler`, two fixtures, one test, `loop_in_pcall` moved to `draw`). Verdicts: none. Checked: base `xpcall`'s own `luaL_checktype` on argument 2 is kept, so its error text is unchanged; a C function gets `LUA_MINSTACK` free slots for the two pushes; a normal error still runs the script's handler at the error point (`handled main.lua:2: x`); `CallGuardTest` and `SandboxTest` pass, 476/476 overall.

## Design Notes

**Guard.** One hook, `LUA_MASKCOUNT | LUA_MASKCALL`, count 1,000, installed once. Each event checks, in order: stack headroom (`__builtin_frame_address(0)` minus the floor below 2,048 B), cancel, then (count events) budget. A trip records the fault and message, sets the hook count to 1 so the next instruction re-raises after any script `pcall`, and raises a preformatted string (the stack fault skips `lua_getinfo`). `enter()` arms (count 0, fault none), and after `lua_pcall` a recorded fault wins over the status. The call hook is what sees C recursion: a Lua call does not grow the C stack, but each `pcall`, metamethod, or C-to-Lua call does. Lua runs a message handler at the error point, still inside the hook when the error came from it, so the sandbox's `xpcall` wraps the script's handler in a C function that passes a guard fault through without calling it; any other error reaches the handler exactly as with the base `xpcall`.

**Measured host cost per level** (x86-64 gcc 13, painted 1 MiB pthread stack, depths 20 and 120): nested `pcall` 784 B at -O3 (816 B at -Os); recursive `__index` function 254 B (270 B). At Lua's 200-level limit these need 157 KB and 51 KB, so the guard, not Lua, must stop them; with a 16 KB floor it trips near 17 and 50 levels. On the ESP32-S3 (`xtensa-esp32s3-elf-gcc -Os -fstack-usage` over the Lua sources) the frames on the `pcall` path sum to about 592 B a level (`luaV_execute` 160, `luaD_rawrunprotected` 128, `lua_pcallk` 64, `luaD_precall` 48, six more at 32) and the `__index` path to about 304 B, both under the 2 KB headroom, so one level cannot jump the guard. The host suite prints both measurements and the depth reached (17 and 51); the owner can capture the device's `uxTaskGetStackHighWaterMark` line after a fixture run. The call hook costs 1 to 11 % over the count hook alone on the host (fib(30) +11 %, a sort with a Lua comparator +8 %, a call-free loop +1 %).

**Beyond the hook (owner questions, deferred).** Measured the same way, C recursion that runs no hook: parser nesting of 199 functions 95 KB (448 B a level), `do` blocks 63 KB, parentheses 45 KB; pattern matching `('a?'):rep(199)` about 19 KB (96 B a level); and `__gc` finalizers run with hooks off (`lgc.c:980`), so a looping or recursing `__gc` escapes budget and guard. Options: `-DLUAI_MAXCCALLS=<n>` and `-DMAXCCALLS=<m>` in `library.json` (an AD-4 spine change; Lua stays byte-for-byte), a `setmetatable` that refuses `__gc` (changes `lib setmetatable` semantics), or accept and document. On the device a looping `__gc` still ends in abandon after Leave.

**Abandon (the entry's unknown).** Not safe to free everything: the task may hold the input-queue or task mutex, and deleting a task never releases what it holds. Safe once it is suspended while `inLua` (inside `lua_pcall` or `lua_close`, touching only the arena, sources, and back list) and `inSwap` is clear: then the arena, frame storage, and sources are freed and `L` is dropped; the `GameVM` object stays leaked. Residual: a newlib lock taken inside a Lua library call (`table.sort`'s pivot calls `clock`/`time`) could stay held. `close()` needs no budget: with the stack empty, `lua_close` runs only `__gc`, which hooks cannot reach.

**Lua build defines (change 2).** On the ESP32-S3 (`-Os -fstack-usage`) one `LUAI_MAXCCALLS` unit of parser recursion costs at most about 304 B (`statement` 128 + `body` 144 + `statlist` 32, or `subexpr` 64 + `constructor` 96 + `recfield` 112 + `expr` 32; taken as 320). 30 units are 9.6 KB; `main.lua` parses on top of about 3.5 KB of task stack (the spike's whole-run high-water was 3.3 to 3.7 KB), so at most about 13.1 KB of 16 KB, and `require` first checks 10 KB free (`CallGuard::PARSE_HEADROOM_BYTES`), which a top-level require has (host: about 14 KB). 30, not 32: Lua raises "error in error handling" at `LUAI_MAXCCALLS / 10 * 11`, which for 32 left the message handler one level and replaced "C stack overflow" with that message; 30 leaves three. The sticky guard still ends nested `pcall` first (about 20 levels at 592 B on the S3, 17 on the host), which Lua's own error could not, since the script's `pcall` catches it; recursive `__index` (304 B) now stops at Lua's limit (27 levels on the host) as an ordinary uncaught error. `MAXCCALLS=16`: `match` is 48 B a level on the S3 with its helpers inlined, and its costliest caller `str_gsub` is 912 B (with a `luaL_Buffer`), so 912 + 16 x 48 + 32 = 1.7 KB fits in the 2 KB the guard leaves at the call event (the hook's own frames, about 200 B, are popped before the call); patterns may nest at most 16 recursive items (captures, `?`, `*`, `+`, `-`). `l_randomizePivot=sizeof` is object-like so the flag passes PlatformIO and CMake unquoted: `l_randomizePivot(L)` becomes `sizeof(L)`, a constant, as Lua's comment allows ("~0 is a good choice"). Each is read only in `ldo.h`, `lstate.c`, `ldo.c` (`LUAI_MAXCCALLS`), `lstrlib.c` (`MAXCCALLS`), and `ltablib.c` (`l_randomizePivot`), never in `lua.h`, `luaconf.h`, or `lauxlib.h`; the host suite reads the same `library.json`, and the device object shows `movi 16` for the match depth in `str_find_aux`, `str_gsub`, and `gmatch_aux`.

**Watchdog (change 2).** `GameVM` stores `millis()` before each call (start, draw, input, close) and `runningForMs(now)` is non-zero only while `inLua()`. `GameMatchActivity::loop` runs every pass while a callback runs (`skipLoopDelay`), and past 3,000 ms it takes `RenderLock`, `stop(500)`s the VM, abandons it if it did not join, and shows the error view; if the VM ended on its own error meanwhile, that message is shown instead.

**Simulator.** Floor = frame address at task entry minus 16 KB; abandon leaks all at once, since the shim cannot stop a thread.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `pio run -e default` -- SUCCESS; `pio check -e x4pro` -- no defects.
- `sim.sh setup`, `build x4pro`, `start x4pro`, `tap`, `key back`, `ss`, `log` -- screenshots beside this plan.
- `./bin/clang-format-fix`, `python3 scripts/check_upstream_touches.py` -- no diff, PASS.

**Manual checks:**
- X4 Pro: the loop and nested-`pcall` fixtures end in the error view; the "VM stopped" log line's high-water mark is the owner's evidence.

**Verification record** (2026-09-27, working tree before commit):
- Host: `ctest` 476/476 passed after review loop 1; `GameScriptTest` 57 (new: 10 `CallGuardTest`, 5 `SandboxTest`, 2 `LuaGameTest`, 1 `ArenaAllocatorTest`). `MeasuresStackCostPerLevel` printed nested `pcall` 784 B a level, stopped at depth 17, and recursive `__index` 256 B, depth 51; the tracer's least headroom at a hook was 15,104 of 16,384 B.
- After review loop 1: `pio run -e x4pro` SUCCESS (RAM 31.1 %, Flash 88.2 %, 5,782,514 B); `pio run -e default` SUCCESS (RAM 17.7 %, Flash 85.6 %); `pio check -e x4pro` and `pio check -e default`: no defects; `sim.sh build x4pro` SUCCESS, and the pcall-loop and cancel bands re-run with the same results.
- Simulator x4pro, `fixtures/loop` and `tracer` in `fs_/.games/`, screenshots in [story-sandbox-screenshots/](story-sandbox-screenshots/): `loop-frame.png`; `loop-budget-error.png` and `loop-pcall-error.png` ("main.lua:N: instruction budget exceeded", about 0.1 s after the tap); `recurse-error.png` ("script recursion too deep (C stack nearly full)"); `slow-cancelled.png` (Back during slow C calls: log "VM cancelled", joined 47 ms after the activity exit); `stuck-abandoned.png` (Back while stuck in one `string.find`: "did not stop within 500 ms of cancel; abandoning it", then "the simulator cannot stop its thread", Games list 512 ms after Back); `home-after.png` and `tracer-after.png` (Home and another game still respond); an idle Back from the tracer logged "VM stopped" in 5 ms.
- `./bin/clang-format-fix`: no diff in this story's files; `check_upstream_touches.py`: PASS (fork-only paths).

**Verification record, change 2** (2026-09-27, working tree before the second commit, on 601c0718):
- Host: `ctest` 483/483 passed; `GameScriptTest` 63 (new: `ParserAndPatternRecursionEndInScriptErrors` for `deep_parens` "C stack overflow" and `deep_pattern` "pattern too complex" in the modelled 16 KB stack; `TableLoopsPastTheLimitAreRefused` for `table_move` and `table_insert_len`, and 65,536 still moving; `SetmetatableRefusesGcFinalizers` for `gc_recursive` and `gc_loop`, and unchanged argument and protection errors; `PrintWritesNothing` with captured stdout; `RequireNeedsParserHeadroom`; `LockedSectionsAreCountedForAbandon`); `ApiLevelTest` recomputes `API_SURFACE_CRC` 0x93ECD968. The stack test printed nested `pcall` 784 B a level, stopped at depth 17 by the guard, and `__index` 256 B, stopped at depth 27 by `LUAI_MAXCCALLS`.
- `pio run -e x4pro` SUCCESS (RAM 31.1 %, Flash 88.3 %, 5,783,722 B); `pio run -e default` SUCCESS (RAM 17.7 %, Flash 85.6 %); `pio check -e x4pro` and `-e default`: no defects; `sim.sh build x4pro` SUCCESS.
- Simulator x4pro, [story-sandbox-screenshots/](story-sandbox-screenshots/): `watchdog-cancelled.png` (slow C calls: "a call ran over 3000 ms" 4.2 s after the match started (the tap came about 1.1 s in), then "VM cancelled" 246 ms later, error view "stopped responding: one call ran over 3 s"); `watchdog-abandoned.png` (one stuck `string.find`: cancel, "did not stop within 500 ms", the simulator's leak, the same error view); `home-after-watchdog.png` (Back, Back: Home responds); `loop-frame.png` refreshed.

---
title: 'Sticky-fault follow-ups: __close, the string metatable, and the README oracle (AI-13b)'
type: 'bugfix'
ticket: ''
created: '2026-09-28'
status: 'built'
baseline_revision: '93b1590f3f8f4d8b1b47982c2ef0a3312f5efd23'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/implementation-artifacts/plan-e2r-ai-13-runtime-boundary-fixes.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The independent review of da88f053 (triage continued from [plan-e2r-ai-13-runtime-boundary-fixes.md](plan-e2r-ai-13-runtime-boundary-fixes.md)) found that the heap cap can still be survived under `pcall`: a to-be-closed value whose `__close` raises while the memory error unwinds turns `LUA_ERRMEM` into `LUA_ERRRUN` (`luaD_closeprotected`), and scripts can make a closable value through `setmetatable` or the shared string metatable. It also found six low items: `error("not enough memory", 0)` is uncatchable, require's ERRMEM branch looks redundant, `requireHeadroom` formats on a short stack, the band tests do not tie fixture bands to README rows and skip watchdog bands by text, a heap test lost its cap check silently, and the contract does not state these rules.

**Approach (orchestrator decisions):** `setmetatable` refuses a metatable with a raw `__close` field (same style as `__gc`); the sandbox seals the string metatable (its `__metatable` field, so `getmetatable('')` returns a non-table); `requireHeadroom` raises a static literal with no formatting; the band tests parse each fixture's band labels and skip watchdog bands by name; docs state the rules (api-level-1.txt, game-api-seed.md section 6, spine AD-6). Accept item 2 (document it) and item 6 (comment). Remove require's ERRMEM branch unless a test shows it matters.

## Boundaries & Constraints

**Always:** Fork-only files. Existing error texts unchanged except the headroom refusals, which lose their `chunk:line:` prefix (no README row carries it). Tests for each fix, including pcall and xpcall cases for both repros and the `__close` refusal. `API_SURFACE_CRC` changes only if an entry line changes.

**Never:** No change to Lua's sources or `lib/lua/library.json`. Do not touch `src/activities/games` or `src/games` (a parallel agent owns them). Do not model the watchdog bands on the host.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| `__close` metatable | `pcall(setmetatable, {}, {__close = f})`, and the same under `xpcall` | false, "setmetatable: __close metamethods are not supported" | plain Lua error, like `__gc` |
| String metatable | `getmetatable('')` | `false`; `('ab'):upper()` and `'1' + 1` still work | `getmetatable('').__close = f` is an index error |
| Closable string | `local c <close> = 'x'` | "variable 'c' got a non-closable value" | plain Lua error |
| Heap cap via require inside a tbc scope | the closable value's metatable gained `__close` after `setmetatable`, then `pcall(require, 'bomb')` | ScriptError "not enough memory", Fault::Memory | require's raiseMemory records the fault before the close runs |
| Headroom refusal | ch.log / ch.store under 4 KiB of stack | ScriptError "ch.log: script recursion too deep to call it" | guard fault, no formatting |
| README drift | a fixture band without a README row, or a row without a band | the band test fails | |

</frozen-after-approval>

## Code Map

- `lib/GameScript/Sandbox.cpp:156-175` `guardedSetmetatable` -- loop the raw-field refusal over `__gc` and `__close`. `openSandbox` :238 -- after `luaopen_string`, set the string metatable's `__metatable` to `false`. :50-57 `forgetAndRaise` -- keep its ERRMEM branch (Design Notes). Header comment `Sandbox.h:18-26`.
- `lib/GameScript/CallGuard.{h,cpp}` -- add `raiseStatic(L, literal)`: Binding fault, `shown = literal`, no `lua_getstack`/`lua_getinfo`/`snprintf`; `raise()` formats into `text` and ends in it.
- `lib/GameScript/ChBindings.cpp:151-154` `requireHeadroom` -- use `raiseStatic`.
- `test/game_script/fixtures/surface/main.lua:57-61` and `ApiSurfaceTest.cpp:181-185` -- the fixture reports `S <type of getmetatable('')>` and method/arithmetic probes; the test checks the raw keys of the string metatable from C++ (`openSandbox` on a `luaL_newstate`): lstrlib's set plus `__metatable`.
- `test/game_script/LuaGameTest.cpp:212` next to `TheHeapCapStopsTheGameEvenUnderPcall` -- new tests.
- `test/game_script/SandboxTest.cpp:127` `SetmetatableRefusesGcFinalizers` -- add `__close`.
- `test/game_script/SessionGameTest.cpp:204-250` `expectBandsMatchTheReadme` -- parse `label = "..."` from the fixture's `main.lua`, compare with the README's labels as sets, skip names in an explicit list; :383 add the comment pointing at `ArenaAllocatorRegionTest.TheReserveStaysIntactWhenLuaExhaustsItsRegion` (`ArenaAllocatorTest.cpp:287`).
- `docs/crosshatch/api-level-1.txt:62-73` -- comments on setmetatable, the string metatable, and `error("not enough memory")`.
- `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md` section 6 Errors; `ARCHITECTURE-SPINE.md:133` (AD-6 lists `__gc`) -- amendments dated 2026-09-28.

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameScript/CallGuard.{h,cpp}`, `ChBindings.cpp` -- `raiseStatic`; `requireHeadroom` uses it.
- [ ] `lib/GameScript/Sandbox.{h,cpp}` -- `__close` refusal; string metatable seal; comments.
- [ ] `test/game_script/LuaGameTest.cpp` -- both repros under pcall and xpcall end at the refusal / seal with the game still running (draw Ok, text shows the error); `<close>` on a string is refused; the require-in-tbc heap case stops with Fault::Memory.
- [ ] `test/game_script/SandboxTest.cpp` -- `__close` refusal text, directly and under pcall.
- [ ] `test/game_script/HostBindingsTest.cpp` -- `LogAndStoreNeedStackHeadroom` asserts the message has no `main.lua:` prefix and the fault is Binding.
- [ ] `test/game_script/fixtures/surface/main.lua`, `ApiSurfaceTest.cpp` -- the seal.
- [ ] `test/game_script/SessionGameTest.cpp` -- band cross-check, named watchdog skips, comment.
- [ ] docs -- api-level-1.txt comments, game-api-seed.md section 6, spine AD-6 amendment.

**Acceptance Criteria:**
- Given a script, when it tries to give a table or a string a `__close` metamethod through `setmetatable` or the string metatable, then it gets a Lua error and no closable value.
- Given a fixture band without a README row (or the reverse), when the host suite runs, then `SessionGameTest` fails.
- Given the whole host suite, when run, then all tests pass; `pio run -e x4pro` and `-e default` succeed.

## Implementation Notes

- No subagent tool in this session: implemented directly, and each review lens run in turn by the build agent.
- `CallGuard::raiseStatic` records a Binding fault whose message is the literal itself; `raise()` formats into the guard's `text` and ends in `raiseStatic(L, text)`. `requireHeadroom` calls it, so the three headroom refusals read `ch.log: script recursion too deep to call it` (no `main.lua:N:` prefix; no README row carries these texts).
- `guardedSetmetatable` refuses a raw `__gc` or `__close` field (any non-nil value, `false` included); the message is `setmetatable: __close metamethods are not supported`. `openSandbox` sets the string metatable's `__metatable` to `false` right after the libraries open.
- The surface fixture can no longer list the string metatable's keys, so it reports `getmetatable("")` and a method and arithmetic probe; `ApiSurfaceTest.LibraryMembersMatchTheList` reads the metatable raw from a `luaL_newstate` + `openSandbox` state and expects lstrlib's nine keys plus `__metatable`.
- require's ERRMEM branch stays (item 3): `LuaGameTest.TheHeapCapInARequireSurvivesARaisingClose` fails without it. The test adds `__close` to a metatable after `setmetatable`, which the refusal cannot see.
- Residual (reported to the orchestrator, not fixed here): the same post-hoc `__close` survives a heap bomb raised in Lua code, not in require. A scratch probe (`local mt = {} local t = setmetatable({}, mt) mt.__close = function() error('recovered', 0) end local c <close> = t local s = string.rep('x', 1 << 20)` under `pcall`) drew `false recovered` with the draw Ok and no fault. No fork-code-only change closes it; the options are in the final report.
- Item 2: `TheHeapCapStopsTheGameEvenUnderPcall` gained a `pcall(error, 'not enough memory', 0)` case, which ends in Fault::Memory as api-level-1.txt now says.
- `API_SURFACE_CRC` is unchanged: only comments of api-level-1.txt changed.
- Mutation check (each alone, then restored): dropping require's ERRMEM branch fails `TheHeapCapInARequireSurvivesARaisingClose`; dropping `__close` from the refusal fails `AScriptCannotMakeAClosableValue` and `SetmetatableRefusesGcFinalizers`; dropping the seal fails `AScriptCannotMakeAClosableValue` and `LibraryMembersMatchTheList`; deleting the README's "Invalid status" row fails `EveryLimitsFixtureBandEndsWithTheReadmesText`; renaming the "Stuck in one C call" row fails `EveryLoopFixtureBandEndsWithTheReadmesText` without running the stuck call.

- Follow-up (owner decision 2026-09-28, option (a); see the Plan Change Log): `lib/lua/port/luai_throw.h`, force-included into the Lua units (`library.json`: `-I port`, `-include luai_throw.h`; `test/game_script/CMakeLists.txt` mirrors both entry kinds), defines `LUAI_TRY` as `ldo.c`'s ISO C form and `LUAI_THROW` as its `longjmp` form preceded, for `LUA_ERRMEM` only, by a call to the weak `luaport_memoryerror(L)`. Neither the firmware nor the host defines `LUA_USE_POSIX`, so `ldo.c` would have picked plain `setjmp`/`longjmp` on every target. A relative `-include port/...` did not work in PlatformIO (it passes the path unresolved and gcc runs from the project root); `-I port` is resolved against the library by PlatformIO, and `-include` then finds the header on the include chain.
- Weak reference, not a weak default definition: the header only declares the hook, so no Lua unit defines it and no env can fail to link. `CallGuard.cpp` defines it (`extern "C"`, declared in `CallGuard.h`); in the x4pro firmware `nm` shows `T luaport_memoryerror` and `w luaport_memoryerror` in `ldo.c.o`; the host suite fails without it (below). The hook returns unless `lua_gethook(L)` is `CallGuard::hook`: `lua_newstate` sets the hook to NULL before it can throw and before the extra space is set, and `install` comes after `setBindingContext`, so plain states (`luaL_newstate` in tests, `lua_newstate` in `ArenaAllocatorTest`) and a state being built are skipped. Then `CallGuard::recordMemory` (was `raiseMemory`, now it returns) records `Fault::Memory` and sets the hook count to 1; allocation-free.
- Simplified: the sandbox's `pcall` is Lua's again, `xpcall` is the base one with only its handler wrapped (as before da88f053), `finishProtected` and `raiseMemoryError` are gone, and require's `forgetAndRaise` is a plain `lua_error`. A memory error a pcall catches is already a fault, and the hook raises it at the script's next instruction, or `LuaGame` reads it after the call.
- Tests: `TheHeapCapInARequireSurvivesARaisingClose` became `ACloseAddedLaterCannotOutliveTheHeapCap`, 30 runs: five closable setups (a raising `__close`; `__close = error`; `__close = print` then `nil` before the bomb; `rawset` post hoc; `setmetatable(_G, {})` then `getmetatable(_G).__close`) x three placements (bomb in the protected function, in a required module, in a module `m` with its own `<close>` under `pcall(require, 'm')`) x pcall/xpcall. With the hook body emptied, all 30 fail (120 failed expectations) and so does `TheHeapCapStopsTheGameEvenUnderPcall`; the same two fail with the `-include` entry dropped. `ARefusalLuaRecoversFromIsNotAFault`: with the collector stopped, 1,000 1 KB strings make the cap refuse (`luaCapRefusals` rises), Lua's emergency collection recovers, the draw is Ok and the fault None.
- Review of 7e0899e6, item 4: `expectBandsMatchTheReadme` now ASSERTs the label match and each watchdog name before any tap; renaming "Stuck in one C call" in both the fixture and the README fails in 0 ms instead of tapping the stuck call.

## Plan Change Log

- 2026-09-28, owner decision on row 23 (option (a)): the Boundaries' "No change to Lua's sources or `lib/lua/library.json`" and Design Notes' "no Lua build change in this plan" are superseded for the throw hook: `library.json` gains `-I port` and `-include luai_throw.h`, and `lib/lua/port/luai_throw.h` is new; `src/` stays byte for byte. require's ERRMEM branch (row 18) and `finishProtected` become redundant and are removed. KEEP: the `__close` refusal, the string-metatable seal, `raiseStatic`, the README band cross-check, and every test of 7e0899e6 (the require-in-tbc test is widened, not dropped).

## Review Triage Log

Continues [plan-e2r-ai-13-runtime-boundary-fixes.md](plan-e2r-ai-13-runtime-boundary-fixes.md)'s log (rows 1-15).

Pass 0: the orchestrator's independent review of da88f053, triaged with the orchestrator's decisions. Counts: high 0, medium 1, low 6, false 0, maybe-false 0.

| # | Source | Finding | Verdict | Route / evidence |
|---|--------|---------|---------|------------------|
| 16 | orchestrator | A raising `__close` turns the heap cap's `LUA_ERRMEM` into `LUA_ERRRUN` under pcall/xpcall (via `setmetatable` or the string metatable) | medium | patch: `setmetatable` refuses `__close`, the string metatable is sealed (`LuaGameTest.AScriptCannotMakeAClosableValue`, `SandboxTest.SetmetatableRefusesGcFinalizers`, `ApiSurfaceTest.LibraryMembersMatchTheList`). Residual (a `__close` added after `setmetatable`, bomb in Lua code) confirmed by probe: defer, sent back to the orchestrator |
| 17 | orchestrator | `error("not enough memory", 0)` is uncatchable | low | accepted: api-level-1.txt says so; `TheHeapCapStopsTheGameEvenUnderPcall` pins it |
| 18 | orchestrator | require's `LUA_ERRMEM` branch is redundant and untested | low | rejected: it records the fault before a post-hoc `__close` can replace the error; `TheHeapCapInARequireSurvivesARaisingClose` fails without it; comment added |
| 19 | orchestrator | `requireHeadroom` formats on a short stack | low | patch: `CallGuard::raiseStatic`; `LogAndStoreNeedStackHeadroom` asserts the exact literal |
| 20 | orchestrator | Band tests do not tie fixture bands to README rows; watchdog bands skipped by text | low | patch: `bandLabels` parses `label = "..."`; rows and bands must match in order; watchdog bands skipped by name. Mutation: a deleted row and a renamed watchdog row each fail |
| 21 | orchestrator | `TheSessionAndScratchAreTakenFromTheArenaBeforeLua` no longer reaches the cap | low | accepted: comment points at `ArenaAllocatorRegionTest.TheReserveStaysIntactWhenLuaExhaustsItsRegion` |
| 22 | orchestrator | The contract omits pcall's limits, the table limit, `__close`, the seal | low | patch: game-api-seed.md section 6 and spine AD-6, marked Amended 2026-09-28 |

Pass 1 (lenses run in turn by the build agent: blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: high 0, medium 1, low 3, false 2, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
|---|------|---------|---------|------------------|
| 23 | edge-case (claim) | The AC "a script ... gets no closable value" is false for a `__close` added to the metatable after `setmetatable` | medium | defer: the path predates this change (da88f053 had it, wider); `deferred-work.md` `## e2r-ai-13b`, and the final report's blocking question. Closed in the follow-up by the owner's option (a), the throw hook (row 31) |
| 24 | blind | `ApiSurfaceTest` leaks its `lua_State` when `ASSERT_TRUE(lua_getmetatable…)` fails | low | patch: the loop runs under `if`, and `lua_close` always runs; an empty set fails the `EXPECT_EQ` |
| 25 | blind | `raiseStatic`'s comment says "a string literal" but `raise()` passes the guard's `text` | low | patch: comment names both |
| 26 | blind | `lua_tostring` on a non-string key would break `lua_next` in the raw metatable walk | false | lstrlib's metatable and the seal use string keys only (`lstrlib.c` `stringmetamethods`, `"__index"`, `"__metatable"`) |
| 27 | edge-case | `lua_getmetatable` in `openSandbox`'s seal could push nothing | false | `luaopen_string`, opened two lines above, always sets the string metatable (`lstrlib.c:1880-1885`) |
| 28 | edge-case (claim) | The task "asserts the fault is Binding" in `LogAndStoreNeedStackHeadroom` is not met (`tapWith` hides the game) | low | rejected: the ScriptError under a pcall that swallows errors shows the refusal is a guard fault |
| 29 | verification-gap | No gaps: the refactored `raise()` keeps its prefixed texts (existing `STREQ` frame-full and README tests), and each new behaviour has a mutation-checked test | no finding | |
| 30 | intent-alignment | Readings: (a) close the heap-cap-under-pcall hole, (b) apply the orchestrator's named mechanisms. The diff implements (b); (a) holds for the reported repros and the require path, not for a post-hoc `__close` with a bomb in Lua code (row 23) | no finding | descriptive only |

Pass 2: the orchestrator's independent review of 7e0899e6 (it verified the seal and the `__close` refusal against bypasses, string methods, the other stopping faults, and `raiseStatic`), triaged with the orchestrator's calls, together with the owner's option (a). Counts: high 0, medium 1, low 5, false 0, maybe-false 0.

| # | Source | Finding | Verdict | Route / evidence |
|---|--------|---------|---------|------------------|
| 31 | orchestrator | The added-later `__close` gap also reaches inside `require`: a module with its own `<close>` and a heap bomb, under `pcall(require, 'm')`, draws Ok with no fault (the module's own `lua_pcall` closes before require sees ERRMEM) | medium | patch: the throw hook (row 23 closed); the exact repro is the "module m" placement of `ACloseAddedLaterCannotOutliveTheHeapCap`, which fails without the hook |
| 32 | orchestrator | The gap needs no raising `__close`: `__close = print` then `nil` before the bomb; `rawset` post hoc; `setmetatable(_G, {})` then `getmetatable(_G).__close` | low | patch: each is a closable setup of the same test, under pcall and xpcall and in all three placements |
| 33 | orchestrator | game-api-seed §6, spine AD-6, and api-level-1.txt promise pcall cannot catch stopping faults | low | patch: true with the hook; api-level-1.txt's setmetatable comment and AD-6 now say where the memory fault is recorded, with no gap wording; "unknown icon or image name" left to epic-icon-library, per the orchestrator |
| 34 | orchestrator | Renaming the watchdog band consistently in `loop/main.lua` and the README passes the label check and taps into the stuck call | low | patch: fatal `ASSERT`s before any tap (Implementation Notes) |
| 35 | orchestrator | `raiseStatic` texts carry no line number, which game-api-seed §6 says the device shows | low | patch: §6 says faults found with the C stack nearly full show none |
| 36 | orchestrator | Lua's own "C stack overflow" (`LUAI_MAXCCALLS`) is an ordinary catchable error, while the `c_stack_levels_count` comment says the call fails | low | patch (comment only, runtime unchanged): the comment says pcall can catch it and that the runtime's headroom check is the one that stops the game |

Pass 3: the orchestrator's independent review of d7cc03e8 (it found the mechanism sound: the macros match stock ISO `setjmp`/`longjmp`, the weak call is null-guarded, the device `ldo.o` calls the hook in the linked ELF, the host build carries the `-include`, the hook is safe at every throw site). Docs and tests only; no runtime change. Counts: high 0, medium 1, low 2, false 0, maybe-false 0.

| # | Source | Finding | Verdict | Route / evidence |
|---|--------|---------|---------|------------------|
| 37 | orchestrator | "Lua throws a memory error only after its emergency collection" is false for lauxlib's buffers: `resizebox` calls the allocator directly, so a `string.rep`/`table.concat`/`gsub`/`format` result past `LUAL_BUFFERSIZE` (512 B on the device, 1024 B on the host) refused at the cap is a sticky fault with no collection (as in stock Lua); `ARefusalLuaRecoversFromIsNotAFault` used 1,000-byte `string.rep`, box path on the device only | medium | patch: `luai_throw.h` and spine AD-6 say which path collects first and that a game near its cap can end on a large string operation; the recovery test now grows tables by assignment (luaM only, any word size); new `ALibraryStringBufferRefusedAtTheCapStopsTheGame` pins the box path (near the cap with the collector stopped, `pcall(string.rep, 'x', 4096)` ends in Fault::Memory) |
| 38 | orchestrator | Each xpcall level costs two of the 30 C levels (the wrapper's `lua_call` and base xpcall's `lua_pcallk`) | low | patch: `api-level-1.txt`'s `c_stack_levels_count` comment says about 28 nested pcalls or 14 nested xpcalls fit |
| 39 | orchestrator | Darwin's ld64 rejects the undefined weak `luaport_memoryerror` where Lua links without `CallGuard.cpp` (`GameSaveStoreTest`) | low | patch: `LINKER:-U,_luaport_memoryerror` on `lua_vendored` for `APPLE`, with a comment (not run here: Linux host) |

## Design Notes

(Superseded by the owner's option (a): the next paragraph describes 7e0899e6; since the throw hook, require no longer needs its branch.)

Why require keeps its ERRMEM branch: `setmetatable` checks `__close` only when the metatable is set, but Lua looks `__close` up at the `<close>` declaration and again at close time. A script can set a metatable, then add `__close` to it, and close over it. A plain `lua_error` in require would re-raise ERRMEM, but the `__close` could still replace it while unwinding to the outer pcall; `raiseMemory` records the fault first, so the hook re-raises inside the `__close`. A test pins it. The general post-hoc case (a memory error raised in Lua code, not in require) is not closed by the orchestrator's fix; it goes back as a question (no Lua build change in this plan).

Seal value `false`: `getmetatable('')` then reads as "no metatable" to a truthiness test, and indexing it fails loudly.

## Verification

**Commands:**
- `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` -- all pass
- `flock <lock> pio run -e x4pro`, `flock <lock> pio run -e default` -- SUCCESS
- `./bin/clang-format-fix` twice -- nothing new in `git status`

**Evidence (2026-09-28, all builds under the shared lock):**
- Host tests: `cmake --build build/test` then `ctest --test-dir build/test -j8` -- 639/639 passed (637 before, plus `AScriptCannotMakeAClosableValue` and `TheHeapCapInARequireSurvivesARaisingClose`), after the review patches and the formatter.
- Mutation check: see Implementation Notes (five mutations, each failing its tests; restored, 178/178 in `GameScriptTest`).
- `pio run -e x4pro` -- SUCCESS (3:54 fresh, 1:25 after the review patches); `pio run -e default` -- SUCCESS (2:55 fresh, 0:35 after).
- `API_SURFACE_CRC` unchanged: `ApiLevelTest` and `ApiSurfaceTest.ListLoadsAndMatchesItsCrc` pass with only comment lines changed.
- Every touched path is fork-only (`lib/GameScript`, `test/game_script`, `docs/crosshatch`, `_bmad-output`), so the upstream-touch check does not apply.

**Evidence for the follow-up (option (a) and the review of 7e0899e6; 2026-09-28, under the lock):**
- Host tests: reconfigure, build, `ctest -j8` -- 640/640 passed. `ninja -t commands` shows `-I.../lib/lua/port -include luai_throw.h` on `ldo.c`; `nm` on `GameScriptTest` shows `T luaport_memoryerror`.
- Mutations (each alone, restored after): the `-include` entry dropped, or the hook body emptied, fails `TheHeapCapStopsTheGameEvenUnderPcall` and all 30 runs of `ACloseAddedLaterCannotOutliveTheHeapCap`; the watchdog band renamed in both fixture and README fails `EveryLoopFixtureBandEndsWithTheReadmesText` in 0 ms. Restored: 179/179 in `GameScriptTest`.
- `pio run -e x4pro`, `-e default`, `-e sticky` -- SUCCESS each. `pio run -e x4pro -v` shows `-Ilib/lua/port` and `-include luai_throw.h` on the Lua units; `xtensa-esp32s3-elf-nm` shows `T luaport_memoryerror` in the x4pro and sticky ELFs and `w luaport_memoryerror` in `ldo.c.o`.
- Every touched path is fork-only (`lib/lua` outside `src/`, `lib/GameScript`, `test/game_script`, `docs/crosshatch`, `_bmad-output`).

**Evidence for the review of d7cc03e8 (2026-09-28, under the lock):** reconfigure, build, `ctest -j8` -- 641/641 passed (`ARefusalLuaRecoversFromIsNotAFault` and the new `ALibraryStringBufferRefusedAtTheCapStopsTheGame` included); formatter twice, nothing new. The Apple link option is untested here (Linux host).

---
title: 'Runtime boundary fixes and the fault-fixture oracle (AI-13, AI-6)'
type: 'bugfix'
ticket: ''
created: '2026-09-28'
status: 'built'
baseline_revision: '69c04796a1248bd05e494b0de02c93996be657fb'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/epic-script-runtime-retrospective.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The epic-script-runtime retrospective found three defects at ticket boundaries and one test gap: `pcall` swallows `ch.gfx`'s frame-full and outside-draw errors, so a truncated frame is published (R1); a burst of touches can evict the queued `Timer` event after `takeDue` disarmed the timer, so the game's clock stops for good (R2); two SD failures in a row can delete the only saved copy of `ch.store` (R5); and fault fixtures are asserted only through `DirectGame`, so a contract change can leave them stale unseen (O4).

**Approach:** Raise both gfx faults through the guard's sticky path (`CallGuard::raise`); make `InputQueue::push` evict the oldest non-Timer event; make `GameSaveStore::saveStore` promote an orphaned tmp to `store.bin` before it writes; add one Session-path test whose table mirrors the fixtures README.

## Boundaries & Constraints

**Always:** Error texts stay byte-identical (the README and `limits` band texts must still match). Keep the queue's drop log line. Fork-only files only; no upstream file changes. Add tests for each fix.

**Never:** No change to `api-level-1.txt` entry lines (these errors are not described there, so `API_SURFACE_CRC` stays). No change to GameTimer's arm/take semantics. Do not model the 3 s watchdog bands on the host.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Overflow under pcall | draw calls `pcall(ch.gfx.rect, …)` 2,100 times | ScriptError "frame is full …"; frame not published | guard fault, sticky |
| gfx outside draw under pcall | setup or input runs `pcall(ch.gfx.clear, 'white')` | ScriptError "ch.gfx.clear called outside draw" | guard fault, sticky |
| Full queue, taps then timer, more taps | 7 taps + Timer, then 3 more taps | Timer survives; the oldest taps are dropped; push reports a drop | log line kept |
| Full queue of Timer events only | 8 Timers + 1 more | the oldest is dropped (only the newest can be current) | log line |
| Two SD failures | flush 1: rename fails (tmp only copy); flush 2: write fails | tmp promoted to `store.bin` first; `store.bin` keeps flush 1's bytes; restart restores them | slot stays dirty |
| Promote fails | store.bin missing, tmp exists, rename fails | flush returns false, tmp untouched | log line |

</frozen-after-approval>

## Code Map

- `lib/GameScript/ChBindings.cpp:37-47` -- `drawTarget` and `frameFull` use `luaL_error`; switch to `bindingContext(L)->guard->raise(L, message)` (format into a local `char[96]` first, as `storeSet` :181-193 does). `raise` prefixes `chunk:line:` from level 1 exactly as `luaL_where(L, 1)` does, so texts are unchanged.
- `lib/GameScript/CallGuard.h:15-17,75-78`, `CallGuard.cpp:52,80` -- `Fault::Codec` is the kind `raise` records; rename to `Fault::Binding` and reword its comments (no test references `Fault::Codec`).
- `lib/GameScript/GameInput.{h,cpp}` -- `InputQueue::push` drops `ring[head]` when full; evict the oldest non-Timer entry instead (shift later entries down one), falling back to the oldest when all are Timers. Update the header comment.
- `src/games/GameVM.cpp:173-176` (`postInput`, log line kept), `GameVM.h:61` comment -- reword the comment only.
- `src/games/GameSaveStore.cpp:74-107` (`saveStore`) -- before `openFileForWrite(tmpPath)`: if `!exists(storePath) && exists(tmpPath)`, rename tmp to store.bin; on failure log and return false.
- `docs/crosshatch/formats.md:138-142` -- the Writing paragraph: add the promotion sentence.
- Tests: `test/game_script/GfxBindingsTest.cpp` (near :169, :223), `DisplayListTest.cpp:216` (`InputQueueTest`), `HostBindingsTest.cpp` (`pollTimer`/`deliverNext` helpers :40-58, `timerGame` ~:100), `GameSaveStoreTest.cpp:222-234` + `save_store_stubs/HalStorage.h` (one-shot `failRename`, `failWrite`), `SessionGameTest.cpp:97` (model for the new fault test; `SessionGame`, `modelTaskStack`, `readFixture` in `LuaGameFixture.h`).
- `test/game_script/fixtures/README.md` "Fault bands" and "Fault scripts" tables -- the oracle; `loop/main.lua` bands at `TOP 100`, `BAND_HEIGHT 130`.

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameScript/CallGuard.{h,cpp}` -- rename `Fault::Codec` to `Fault::Binding`; comments say it covers every binding fault the contract says stops the game.
- [ ] `lib/GameScript/ChBindings.cpp` -- raise `drawTarget`'s and `frameFull`'s errors through `guard->raise`.
- [ ] `test/game_script/GfxBindingsTest.cpp` -- pcall cases: overflow under pcall (direct and around a Lua function) ends ScriptError, frameGen unchanged; gfx under pcall in setup and input ends ScriptError; `callGuard().fault() == Fault::Binding`.
- [ ] `lib/GameScript/GameInput.{h,cpp}`, `src/games/GameVM.h` -- evict the oldest non-Timer event.
- [ ] `test/game_script/DisplayListTest.cpp` -- queue tests: the timer survives a tap burst; all-Timer queue drops the oldest.
- [ ] `test/game_script/HostBindingsTest.cpp` -- through the `pollTimer` helper: a due timer queued behind taps survives 8 more taps and ticks the game.
- [ ] `src/games/GameSaveStore.cpp`, `docs/crosshatch/formats.md` -- promote an orphaned tmp before writing.
- [ ] `test/game_script/GameSaveStoreTest.cpp` -- two-failure test (rename fail then write fail) and promote-fail test.
- [ ] `test/game_script/SessionGameTest.cpp` -- `EveryFaultScriptAndLoopBandEndsWithTheReadmesText`: a table of the 22 scripts and the 3 host-modelable loop bands with the README text, run through `SessionGame` with `modelTaskStack`; `loop_input` and loop bands get a tap; stack-depth ones accept either stack message as the README says; the two watchdog bands are skipped with a comment.

**Acceptance Criteria:**
- Given a script that catches a gfx fault with pcall, when the call returns, then the outcome is ScriptError and no frame is published.
- Given the fixtures README, when a fault's text changes in code or fixture, then `SessionGameTest` fails.
- Given the whole host suite, when run, then all tests pass.

## Implementation Notes

- No subagent tool in this session, so the plan was implemented and reviewed directly (each lens run in turn over the staged diff).
- `Fault::Codec` became `Fault::Binding`; `CallGuard::raise` formats `chunk:line:` from level 1 as `luaL_where(L, 1)` does, so every error text is unchanged (the existing `STREQ` frame-full tests and the `limits` band test still pass).
- `InputQueue::push` shifts the entries ahead of the evicted one up by one slot, so order is kept. `GameVM::postInput`'s log line is kept verbatim.
- `GameSaveStore::saveStore` promotes an orphaned tmp before opening the tmp for writing; `formats.md`'s Writing paragraph says so.
- Mutation check: with the three source fixes reverted, the six new fix tests fail (`AFullQueueDropsTheOldestTouchNeverTheTimer`, both new `GfxBindingsTest` cases, `ATimerEventSurvivesATapBurstThatFillsTheQueue`, `TwoFailuresInARowKeepTheOnlyCopy`, `AFailedPromotionWritesNothingAndKeepsTheTmp`); `AQueueOfTimersDropsTheOldestTimer` pins behaviour that the old code shared.
- The spine's line "a full input queue drops the oldest event with a log line" (`ARCHITECTURE-SPINE.md:108`) now reads loosely: the queue drops the oldest non-Timer event. The spine is not edited here.

- Review fixes (pass 2): memory errors, table limits, and both headroom checks are guard faults now too, so every fault the contract says stops the game is sticky under `pcall`. `api-level-1.txt` gained a comment saying so (comments are outside `API_SURFACE_CRC`).

## Plan Change Log

## Review Triage Log

Pass 1 (lenses run in turn by the build agent: blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: high 0, medium 0, low 3, false 4, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
|---|------|---------|---------|------------------|
| 1 | edge-case | `SessionGameTest` compares sorted `onDisk` with an unsorted `listed`, so a row added out of order fails with a confusing mismatch | low | patch: `listed` is sorted too |
| 2 | blind | `drawTarget` dereferences `context.drawTarget` after `raise`, which is not `[[noreturn]]` | false | `raise` ends in `lua_error`, which longjmps; the replaced code had the same shape with `luaL_error`, and every caller is a binding inside a guarded call |
| 3 | blind | The guard's 128-byte message could cut the longer gfx texts | false | chunk names are at most 32 B + `.lua` (`SourceSpan::MAX_NAME_BYTES`), so `name.lua:NNNNN: ` + the 58-byte frame-full text is under 110 B |
| 4 | blind | `GameVM::postInput`'s "dropped the oldest event" is loose now that a Timer at the head is skipped | low | rejected: the brief asks to keep the line; it still reports a drop, and the header comments state the rule |
| 5 | blind | Stack-depth rows in the fault test accept either stack message and ignore their own `message` | false | the README allows exactly this for `deep_parens`, `nested_pcall`, `recursive_index`, and loop band 3 ("the device may stop them with the other of the two stack messages") |
| 6 | blind | The fault test hard-codes loop bands 1 to 3, so a new band in `loop/main.lua` is not checked | low | rejected: the loop fixture's band list changes rarely and a new band is also a README edit; guarding it means parsing the Lua table |
| 7 | edge-case | `guard` could be null in `drawTarget`/`frameFull` | false | `LuaGame::load` sets `bindings.guard` before `openChLibrary` runs any script, and `ch.store.set` already relies on it |
| 8 | verification-gap | `GameVM::pollTimer`/`postInput` and `GameMatchActivity`'s flush are not host-built, so the tests reach the fixes through `InputQueue`, the `HostBindingsTest` copy of `pollTimer`, and `GameSaveStore` | defer (pre-existing) | already in `deferred-work.md` (the GameVM/match harness gap, R9 via AI-11); not re-added |
| 9 | intent-alignment | The intent's four readings (guard raise; evict non-Timer or disarm on pop; promote or second name; Session-path oracle) are each implemented by their first option; the fault table mirrors the README rather than parsing it, which the brief allows | no finding | descriptive only |

Pass 2: the orchestrator's independent review of 67cd7b70 (it confirmed that the three fixes hold against xpcall, metamethods, gsub and sort callbacks, `__close`, and `pcall(require)`). Counts: high 0, medium 1, low 5, false 0, maybe-false 0.

| # | Source | Finding | Verdict | Route / evidence |
|---|--------|---------|---------|------------------|
| 10 | orchestrator | The fault test checks a hand-copied table and never reads `fixtures/README.md`, so the README can go stale (O4 one level up) | medium | patch: `SessionGameTest` now parses the README's `loop/`, `limits/`, and fault-script tables (`readmeTable`, `backticked`) and asserts their exact texts; `EveryLimitsFixtureBandIsAScriptError` became `EveryLimitsFixtureBandEndsWithTheReadmesText`. Editing any of three README texts made all three tests fail (checked, then reverted) |
| 11 | orchestrator | If the promotion rename keeps failing, no newer save is written (baseline kept the newest bytes in the tmp) | low | accepted as designed, per the orchestrator; `formats.md`'s Writing paragraph now states the trade-off |
| 12 | orchestrator | The fault test cannot tell loop bands 1 and 2 apart and never checks which step failed | low | patch: each band is tapped on its README label where the fixture drew it; each script's failing step (load, setup, first draw, tap) is checked against the README's "Where it fails" column |
| 13 | orchestrator | The heap cap, the table element limit (`tooManyElements`), and binding headroom (`requireHeadroom`) are still catchable with `pcall` | low | patch: the sandbox's `pcall` and `xpcall` are now lbaselib's line for line through `finishProtected`, which turns a `LUA_ERRMEM` into a sticky `Fault::Memory` (`CallGuard::raiseMemory`, allocation-free: static text, re-raises the error object already on the stack); `require` keeps a memory error one. `tooManyElements`, `requireHeadroom`, and require's parse headroom raise through the guard. Tests: `LuaGameTest.TheHeapCapStopsTheGameEvenUnderPcall` (pcall, xpcall, `pcall(require)`), `SandboxTest.TableLimitsStopTheGameEvenUnderPcall`, `SandboxTest.RequireNeedsParserHeadroom` (a swallowing variant), `HostBindingsTest.LogAndStoreNeedStackHeadroom` (now a ScriptError). An allocator-side flag was rejected: Lua retries a refused allocation after an emergency GC, and several callers (string-table growth, stack growth) tolerate a refusal, so a refusal is not yet an error. `SessionGameTest.TheSessionAndScratchAreTakenFromTheArenaBeforeLua` filled the heap under `pcall`; it now fills to 244 KiB with `collectgarbage('count')`, and its peak bound is the cap minus 16 KiB |
| 14 | orchestrator | `GameVM.cpp:177` still logs "dropped the oldest event" | low | patch: "dropped the oldest non-timer event"; the spine is left to the orchestrator |
| 15 | orchestrator | The R2 test drives the test's copy of `pollTimer`, not `GameVM` | low | accepted, per the orchestrator: the known R9 harness gap (AI-11) |

The review of da88f053 continues in [plan-e2r-ai-13b-sticky-fault-follow-ups.md](plan-e2r-ai-13b-sticky-fault-follow-ups.md) (rows 16 on).

## Design Notes

Queue eviction over "disarm on pop": the timer stays single-owner (`takeDue` stays as is) and the change is local to `InputQueue`. More than one Timer can be queued only when older ones are stale (a later arm bumped the serial), so when every entry is a Timer the oldest is the one to drop. The spine's line "a full input queue drops the oldest event with a log line" (ARCHITECTURE-SPINE.md:108) becomes "oldest non-timer event"; the spine is not edited here (report it).

Promotion over a second tmp name: loadStore already treats "store.bin missing, tmp present" as the tmp being the save, so renaming it to store.bin changes nothing a reader sees, and the write path stays one tmp name.

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass
- `pio run -e x4pro`, `pio run -e default` -- expected: SUCCESS
- `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- expected: builds

**Evidence (2026-09-28):**
- Host tests: `cmake --build build/test` then `ctest --test-dir build/test -j8` -- 634/634 passed, including the 8 new tests, after the review patch.
- Mutation check: with the three source fixes reverted, 6 of the new tests failed (listed in Implementation Notes); restored, all pass.
- `pio run -e x4pro` -- SUCCESS (3:53); `pio run -e default` -- SUCCESS (7:25).
- `sim.sh build x4pro` -- `simulator_x4pro` SUCCESS (1:44).
- `python3 scripts/check_upstream_touches.py` -- PASS (every touched path is fork-only).

**Evidence after the pass-2 review fixes (2026-09-28):**
- Host tests: 637/637 passed. Editing three README texts (a script, a loop band, and a limits band) failed all three README-driven tests; the README was then restored.
- `pio run -e x4pro` -- SUCCESS; `pio run -e default` -- SUCCESS.

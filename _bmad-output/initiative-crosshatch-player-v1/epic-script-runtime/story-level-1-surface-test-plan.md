---
title: 'Level-1 surface test'
type: 'feature'
ticket: '14'
created: '2026-09-27'
status: 'built'
baseline_revision: '9592a64265d7e79c0698679b2ec446ee44221826'
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

**Problem:** `api-level-1.txt` is only checked against its own grammar and CRC; nothing fails when the live `ch` table, the sandbox's globals and library members, the enums, events, `ctx`, or limits drift from it, and games can hit Lua's C-level limit (`LUAI_MAXCCALLS=30`) that the list does not name.

**Approach:** A `test/game_script` suite loads a real `LuaGame` (the device's `openSandbox` and `openChLibrary`, in `load()`'s order), has its script report the surface through `print`, drives `setup` and `input` for every mode and event, and compares each entry kind with the list in both directions; the list gains every library-table member and the C-level and pattern-depth limits, with `API_SURFACE_CRC` updated.

## Boundaries & Constraints

**Always:**
- Set equality both ways for `ch` paths (fn vs field, field types), globals (`ch`, dotless `fn`, dotless `lib`), each library table's members, enums, events (kind and field names), `ctx` names and types, and limit names; a scratch extra `ch` function or global fails with a message naming it.
- Removed globals (`load`, `loadfile`, `dofile`, `io`, `os`, `debug`, `coroutine`, `package`) are asserted absent by name.
- Each limit is pinned to its code constant; `c_stack_levels_count` and `pattern_depth_count` to `lib/lua/library.json`'s `LUAI_MAXCCALLS` and `MAXCCALLS`, passed in by the CMake that already parses that file for the Lua build.
- One list parser, shared by `ApiLevelTest` and the surface test; the list's header documents `lib <table>.<member>`.

**Never:** `src/games/GameSaveStore*`, `GameAssets*`, `GameMatchActivity*`, `lib/GameScript/StoreSlot*`, `docs/crosshatch/formats.md` (story 2.12); icon, `ch.d.lua`, or catalog checks (other epics); new behaviour in bindings or the sandbox; committing either scratch change.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| In sync | tree as committed | every surface test passes | — |
| Extra `ch` fn | scratch `ch.gfx.sprite` | fails: `ch.gfx.sprite` live, not listed | reverted |
| Extra global | scratch global `debugprint` | fails: global `debugprint` live, not listed | reverted |
| Listed, missing | list entry with no live value | fails naming the entry | — |
| Enum drift | a value accepted but unlisted, or listed but refused | fails naming set and value | — |

</frozen-after-approval>

## Code Map

- `test/game_core/ApiLevelTest.cpp` -- `Entry`, `parseEntry`, `loadSurface`, `crc32`, `limitsOf`: move into a shared header; widen `lib` to `name[.member]`.
- `test/game_script/LuaGameFixture.h` -- `LuaGameTest` (arena, frames, `CapturingLog log`, `FakeClock`), `DirectGame`, `useSource`; reuse, do not change.
- `lib/GameScript/LuaGame.cpp` -- `loadEntry` (sandbox then `ch`), `setupEntry` (ctx), `pushEvent`/`swipeDirName` (events, `dir`); public `load`/`setup`/`input`/`timer()`.
- `lib/GameScript/Sandbox.cpp` `openSandbox`, `ChBindings.cpp` `openChLibrary` and the file-local `COLOR_/SIZE_/ALIGN_/REFRESH_NAMES` option arrays.
- Limit constants: `Manifest::MAX_*_BYTES`, `GameCore::{SNAPSHOT,MOVE,REJECT_REASON}_BYTES`, `Codec::{STORE_LIMIT,MAX_DEPTH}`, `MAX_COMMANDS`, `MAX_BYTES`, `LUA_HEAP_BYTES`, `CallGuard::INSTRUCTION_BUDGET`, `TABLE_ELEMENTS_LIMIT`, `TIMER_MIN_MS`; `timers_pending_count` has no constant (one `GameTimer` slot).
- `GameCore::Mode` + `modeName` (Roster.h), `EventKind`/`SwipeDir` (GameEvent.h).
- `test/game_script/CMakeLists.txt` -- `LUA_DEFINES` from `library.json`; `GAME_SCRIPT_FIXTURES_DIR`.

## Tasks & Acceptance

**Execution:**
- [x] `test/game_core/ApiLevelList.h` -- parser, surface loader, CRC, limits map; `ApiLevelTest.cpp` includes it, grammar accepts `lib string.format`.
- [x] `docs/crosshatch/api-level-1.txt` -- `lib <table>.<member>` for every member of `math`, `string`, `table`, `utf8`; `limit c_stack_levels_count 30`, `limit pattern_depth_count 16` with comments; header grammar text.
- [x] `lib/GameCore/ApiLevel.h` -- new `API_SURFACE_CRC`.
- [x] `lib/GameScript/ChBindings.{h,cpp}` -- the four option-name arrays move to the header unchanged (values stay in the .cpp with a size `static_assert`), so the test reads the accepted set.
- [x] `test/game_script/ApiSurfaceTest.cpp`, `fixtures/surface/main.lua`, `CMakeLists.txt` -- the suite per Design Notes; `API_LEVEL_LIST_DIR`, `LUA_BUILD_LUAI_MAXCCALLS`, `LUA_BUILD_MAXCCALLS` definitions.

**Acceptance Criteria:**
- Given `ctest`, then all suites pass, and each scratch change of the matrix fails `ApiSurfaceTest` with the named entry (output recorded here, both reverted).
- Given the list changed without `API_SURFACE_CRC`, then `ApiLevelTest` and `ApiSurfaceTest` fail.
- Given `pio run -e x4pro` and `-e default`, then both build (ChBindings changed); `./bin/clang-format-fix` leaves no diff.

## Implementation Notes

- Implemented directly (no subagent tool in this session). Files: `test/game_core/ApiLevelList.h` (new; `ApiLevelTest.cpp` now includes it, grammar cases for `lib string.format`), `test/game_script/ApiSurfaceTest.cpp` (8 tests), `test/game_script/fixtures/surface/main.lua`, `test/game_script/CMakeLists.txt`, `docs/crosshatch/api-level-1.txt` (60 `lib <table>.<member>` entries, `limit c_stack_levels_count 30`, `limit pattern_depth_count 16`), `lib/GameCore/ApiLevel.h` (`API_SURFACE_CRC` `0xCAF107E5` -> `0x5DC1F144`, matching Python's `zlib.crc32`), `lib/GameScript/ChBindings.{h,cpp}` (option-name arrays moved to the header unchanged).
- The pattern-matcher depth (`MAXCCALLS=16`) is listed beside `LUAI_MAXCCALLS`: games hit it the same way ("pattern too complex"; `"(%w+)=(%w+)"` already takes 7 of the 16 levels).
- Lua 5.5.1 adds `table.create`; it is listed as the library ships it (one allocation, bounded by the heap cap, no C loop).
- The string metatable carries lstrlib's arithmetic metamethods as well as `__index`; the test pins that set to lstrlib's `stringmetamethods` rather than listing it (it is part of the unmodified `string` library, not a global or member).
- `./bin/clang-format-fix` also reordered the includes of `test/game_script/GameTouchTest.cpp` (drift from 13bcd449); kept, per the epic's format rule.
- Scratch failure output, each reverted afterwards (`git checkout -- lib/GameScript/Sandbox.cpp`, and the `ChBindings.cpp` diff re-read to hold only the array move):
  - `{"sprite", gfxClear}` in `GFX_FUNCTIONS`: `ApiSurfaceTest.cpp:47: Failure ... ch entry "ch.gfx.sprite function" is live but not in api-level-1.txt` / `[  FAILED  ] ApiSurfaceTest.ChTableMatchesTheList` (1 failed test).
  - `lua_setglobal(L, "debugprint")` in `openSandbox`: `ApiSurfaceTest.cpp:47: Failure ... global "debugprint" is live but not in api-level-1.txt` / `[  FAILED  ] ApiSurfaceTest.GlobalsMatchTheList` (1 failed test).
  - Scratch list edits (dropping `enum dir up`, `event over`, `lib table.create`, `enum size large`; `ctx api string`; `c_stack_levels_count 31`) failed the CRC, members, ctx, events, gfx-options, and limits tests, each naming its entry; list restored from a copy.

## Plan Change Log

## Review Triage Log

Pass 1 (lenses run in this session one at a time, no subagents): high 0, medium 1, low 3, false 2, maybe-false 0.

| Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|
| blind | Event and mode loops iterate hand-written enumerator lists, so a new enumerator given a `case` but not added to the list is never delivered, contrary to the Design Notes' "total by construction" | medium | patch | `named()` switches (no `default`, `-Wswitch` an error) feed `enumerators<E>()`, which walks all 256 values; the events and ctx tests iterate that. |
| blind | `pendingTimerCapacity` returns -1 on a failed start or input, so the limit check reports `"1" vs "-1"` without the script's error | low | patch | Added `EXPECT_GE(pendingTimers, 0)` with `errorMessage()`. |
| blind | The CRC check exists in both `ApiLevelTest` and `ApiSurfaceTest` | low | reject | Both call the shared `Surface::crc()`; the story asks the surface test to recompute it, and one source of the bytes remains. |
| blind | The string metatable set is pinned in the test, not the list | false | reject | It is lstrlib's own table, not a global or member; a sandbox change to it still fails the test. |
| edge | A cycle under `ch` would recurse forever in the fixture's `walk` | false | reject | `walk` is Lua recursion: the budget or Lua's stack limit ends it as a `ScriptError`, and `loadSurfaceGame`'s `ASSERT_EQ` fails with the message. |
| verification-gap | `seats_max` (the `GameHostCaps` provider in `src/games`) and `manifest` keys are not compared live; only their limits are | low | defer | Outside the story's listed kinds; `deferred-work.md`. |
| intent | Diff implements the only reading: the real `LuaGame` surface against the list, both directions, CRC recomputed, C-level limit listed and pinned to `library.json`; the one device-code change (option names moved to `ChBindings.h`) lets the enum check read what the binding accepts | — | none | Descriptive only. |

## Design Notes

The fixture's top level (before defining anything global) prints one line per fact: `G <name> <type>`, `M <table>.<member>`, `C <path> <fn|type>` walking `ch` recursively, `MT` lines for any metatable on `_G`, `ch`, its sub-tables, or strings beyond `__index = string`; `setup` prints `X <name> <type>` and `mode <value>`; `input` prints `E <kind> <sorted fields>` and `D <dir>`. The test parses `log.lines`.

Enumerations are total by construction: a `named()` `switch` per enum (`EventKind`, `SwipeDir`, `Mode`) under `#pragma GCC diagnostic error "-Wswitch"`, and the tests iterate every one of the 256 values that `named()` accepts, so a new enumerator breaks the build until the test covers it. `ch.gfx` enums: each listed value is accepted by the real binding inside `draw` and one unlisted value is refused; the header arrays equal the list. `timers_pending_count 1`: two `ch.timer.after` calls leave one due timer (only the later delay fires).

Kinds not checked live here: `manifest` (ManifestTest), `seats_max` (`GameHostCaps`, a `src/games` provider), `icon` (epic-icon-library).

## Verification

**Commands** (under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- Scratch `{"sprite", gfxClear}` in `GFX_FUNCTIONS`, then scratch `lua_setglobal(L, "debugprint")` in `openSandbox` -- `ApiSurfaceTest` fails naming each; reverted.
- `pio run -e x4pro`, `pio run -e default` -- SUCCESS; `./bin/clang-format-fix` -- no diff.

**Verification record** (2026-09-27, working tree before commit, on 9592a642):
- Host: `ctest` 585/585 passed after the review patch; `GameScriptTest --gtest_filter='ApiSurface*'` 8/8; `ApiLevelTest` passes with the widened `lib` grammar. Python `zlib.crc32` over the entry lines gives `0x5dc1f144`, equal to `API_SURFACE_CRC`.
- Scratch `ch.gfx.sprite` and scratch global `debugprint` each fail `ApiSurfaceTest` naming the entry (output in Implementation Notes); both reverted.
- `pio run -e x4pro` SUCCESS (88.5 % flash, 31.1 % RAM); `pio run -e default` SUCCESS (85.6 % flash, 17.7 % RAM).
- `scripts/check_api_freeze_test.py`, `fork_release_test.py`, `fork_common_test.py`: pass. `./bin/clang-format-fix`: no diff beyond the kept `GameTouchTest.cpp` include order.

---
title: 'Vendor Lua 5.5.1'
type: 'chore'
ticket: '4'
created: '2026-09-26'
status: done
baseline_revision: '41acf2fa6c7de2fb1ef2042f0cceeb8ba283d6fc'
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

**Problem:** The script runtime needs a Lua engine that every board compiles, pinned to the exact 5.5.1 release with 64-bit integers and the 5.5 language, and that later upgrades can replace wholesale; nothing is vendored yet.

**Approach:** Copy the 5.5.1 tarball's `src/` byte-for-byte into `lib/lua/src/`, add fork-owned `library.json` (srcFilter excluding the eight AD-4 files, compat off), `.clang-format` (`DisableFormat: true`) and a checksum README, pull it into every env through the anchor's `lua.hpp` include with the `lua_Integer` size assert, add the `pio check` suppress line, and build the same source list in `test/game_script` with a host test that runs Lua and asserts 64-bit integers and the 5.5 language.

## Boundaries & Constraints

**Always:**
- Tarball `https://www.lua.org/ftp/lua-5.5.1.tar.gz`, SHA-256 `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce` (matches the sum lua.org publishes on `/ftp/`). `diff -r lib/lua/src <tarball>/src` is empty (Makefile and `lua.hpp` included).
- srcFilter excludes exactly `lua.c`, `luac.c`, `linit.c`, `liolib.c`, `loslib.c`, `ldblib.c`, `loadlib.c`, `lcorolib.c`; Lua compiles as C (`.c` in both builds).
- Every `LUA_COMPAT_*` option is off. 5.5.1's `luaconf.h` turns `LUA_COMPAT_GLOBAL` on unless it is defined, so the only compat define allowed anywhere is `-DLUA_COMPAT_GLOBAL=0`, set in `lib/lua/library.json` `build.flags` (library-scoped) and read from there by the host build.
- `platformio.ini`: exactly one added line, `--suppress=*:*/lib/lua/*`, in the shared `check_flags`.
- The anchor stays whole-file guarded and definition-free; `<climits>` precedes `lua.hpp`; `static_assert(sizeof(lua_Integer) == 8, ...)`.
- No planning references in code, CMake, or JSON.

**Never:**
- Editing any vendored byte, `bin/clang-format-fix`, `ci.yml`, other `platformio.ini` lines, `test/CMakeLists.txt`, the `freeink-sdk` pointer, or `.skills/`.
- Opening Lua state, allocator, sandbox, or seed code in `lib/GameScript` (later stories).
- Vendoring `doc/` or the top-level Makefile/README.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| 64-bit ints | chunk returning `math.maxinteger`, `math.mininteger`, `(1<<62)+((1<<62)-1)`, `math.type(1<<62)`, `math.maxinteger+1 == math.mininteger` | INT64_MAX, INT64_MIN, INT64_MAX, `"integer"`, `true` | test fails on any mismatch or load/pcall error |
| `global` reserved | load `local global = 1` | syntax error | fails if it loads (compat on) |
| `global` keyword | load `global x; x = 5; return x` | 5 | — |
| read-only `for` var | load `for i = 1, 2 do i = 3 end` | syntax error | — |
| Filter drift | library.json names a file missing from `src/` | CMake configure fails naming it | FATAL_ERROR |

</frozen-after-approval>

## Code Map

- `src/games/GamesBuildAnchor.cpp` -- whole-file `#if FREEINK_CAP_GAMES`; includes `<GameCore.h>`, `<GameIcons.h>`, `<GameScript.h>`. `.clang-format` `IncludeBlocks: Regroup` puts `<*.h>` (priority 1) before `<climits>`/`<lua.hpp>` (priority 2, sorted), so `<climits>` stays ahead of `<lua.hpp>`. Update the comment to name `lib/lua`.
- `lib/miniz/library.json`, `lib/uzlib/library.json` -- precedent: `build.srcDir`/`includeDir` = `src`. PlatformIO 6.1.19 `piolib.py`: `build.srcFilter` relative to srcDir; `build.flags` apply to the library env; default `libArchive` true, so unreferenced objects never link.
- `platformio.ini` `[base] check_flags` -- insert after `--suppress=*:*/freeink-sdk/*`. `pio check` analyzes only `src/`+`include/` (default `check_src_filters`) of env `default`; Lua headers reach it through the anchor.
- `bin/clang-format-fix` -- formats every tracked `.c/.h/.hpp` with `-style=file`; the nearest `.clang-format` wins.
- `test/CMakeLists.txt` -- `project(... C CXX)` (C enabled), `cmake_minimum_required 3.16` (no `string(JSON)`: use regex), `crosspoint_test_common` adds `-Wall -Wextra -pedantic` (keep it off the Lua target). `test/game_script/CMakeLists.txt` builds `GameScriptTest`.
- `luaconf.h:344` -- `LUA_COMPAT_GLOBAL` defaults to 1; used only in `llex.c`/`lparser.c`. `LUA_COMPAT_MATHLIB`, `LUA_COMPAT_APIINTCASTS` default off. `linit.c` excluded, so tests open libs with `luaL_requiref(luaopen_base / luaopen_math)`.
- `docs/crosshatch/upstream-touches.md` -- `lib/lua` already a Game path and `platformio.ini` row 1 already ledgered; no change.
- Tools: `scripts/check_upstream_touches.py`, `scripts/check_flash_budget.py build on|off` + `compare`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/lua/src/*` -- extract the verified tarball's `src/` unchanged (63 files).
- [x] `lib/lua/library.json` -- `name` `lua`, `version` `5.5.1`, `frameworks`/`platforms` `*`, `build`: `srcDir`/`includeDir` `src`, `srcFilter` `+<*>` then the eight `-<file>`, `flags` `["-DLUA_COMPAT_GLOBAL=0"]`.
- [x] `lib/lua/.clang-format` -- `DisableFormat: true` (add `SortIncludes: Never` only if clang-format 21 still reorders includes without it).
- [x] `lib/lua/README.md` -- short fork-owned note: version, URL, SHA-256 and where it was checked, layout (`src/` = tarball `src/` verbatim), fork-owned files and why (filter, compat define), the `diff -r` and upgrade recipe, license pointer (`lua.h`).
- [x] `src/games/GamesBuildAnchor.cpp` -- add `<climits>`, `<lua.hpp>`, the static_assert; comment updated.
- [x] `platformio.ini` -- the one suppress line.
- [x] `test/game_script/CMakeLists.txt` -- `lua_vendored` STATIC C library: `file(GLOB CONFIGURE_DEPENDS lib/lua/src/*.c)` minus the `-<name>` entries regex-read from `library.json` (FATAL_ERROR if a named file is absent), compile definitions from its `-D` flags, `-Wall -Wextra`, public include `lib/lua/src`; `library.json` added to `CMAKE_CONFIGURE_DEPENDS`; link into `GameScriptTest`.
- [x] `test/game_script/LuaOnHostTest.cpp` -- `<climits>` before `<lua.hpp>`, the static_assert, `LUA_VERSION_RELEASE_NUM == 50501`, every I/O row except filter drift (text-mode `luaL_loadbufferx`, `lua_pcall`).

**Acceptance Criteria:**
- Given the tree, when `diff -r lib/lua/src <tarball>/src` runs, then it prints nothing, and `sha256sum` of the tarball equals the README's value.
- Given each of `x4pro`, `sticky`, `default`, `x4c`, `papermono`, when built, then it succeeds and the log names `lua` in the dependency graph and compiles `lib/lua/src/*.c` minus the eight; warnings are recorded.
- Given `pio project config` and the x4pro compile DB, when grepped for `LUA_COMPAT_`, then config has none and the DB has only `-DLUA_COMPAT_GLOBAL=0`, on `lib/lua` units only.
- Given ctest, when run, then all suites pass including the new Lua tests; `pio check` (timed, compared with the pre-change run) and `./bin/clang-format-fix` + `git diff --exit-code` are clean; `check_flash_budget.py compare` exits 0 and its difference is recorded; after commit `check_upstream_touches.py` exits 0.

## Implementation Notes

- Implemented directly (no subagent tool in this session). Files: `lib/lua/src/*` (63 files, tarball `src/` verbatim), `lib/lua/library.json`, `lib/lua/.clang-format`, `lib/lua/README.md`, `src/games/GamesBuildAnchor.cpp`, `platformio.ini` (one line), `test/game_script/CMakeLists.txt`, `test/game_script/LuaOnHostTest.cpp`.
- Tarball SHA-256 `1c4b4068…73dce` equals the `sum` column for `lua-5.5.1.tar.gz` on https://www.lua.org/ftp/ (398,643 B, 2026-07-24). `diff -r lib/lua/src <tarball>/src`: no output.
- `.clang-format` with only `DisableFormat: true` leaves all 62 `.c/.h/.hpp` files byte-identical under clang-format 21.1.8 (control: without it `lapi.c` would be rewritten); `SortIncludes: Never` not needed.
- Builds (4 cores): `x4pro` 89 s (via `build on`), `sticky` 550 s, `default` 359 s, `x4c` 158 s, `papermono` 79 s, all SUCCESS. Each log has `|-- lua @ 5.5.1` in the dependency graph and compiles exactly 26 units (`lib/lua/src` minus the eight). Ticket `unknown` (C warnings): zero warnings from any `lib/lua` file in all five logs; the x4pro compile DB shows Lua built by `xtensa-esp32s3-elf-gcc -std=gnu17` (C), with the shared `-fno-exceptions` passed through harmlessly; `-std=gnu++2a` goes to C++ only. Host build of the same list with `-Wall -Wextra`: zero warnings.
- `LUA_COMPAT_`: `pio project config` has none; the x4pro compile DB (557 units) has `-DLUA_COMPAT_GLOBAL=0` on exactly the 26 `lib/lua` units and on nothing else (not the anchor).
- Flash gate: `build on` / `build off` / `compare` → both `firmware.bin` 5,662,624 B, difference +0 B, exit 0 (the unreferenced library is archived and never linked).
- Ticket `unknown` (`pio check` time): 122 s cppcheck / 171 s wall before the change, 126 s / 179 s after, no defects. `pio check` analyzes only `src/`+`include/` on env `default`, where the anchor body is `#if`ed out, so Lua sources cost essentially nothing; the suppress line is preventive today (an x4pro check of `src/games` with and without it also finds no Lua defects).
- ctest: 378/378 pass (6 in `GameScriptTest`, 5 of them Lua). Mutations: `LUA_COMPAT_GLOBAL=1` in `library.json` fails `GlobalIsReserved`; a `-<nosuch.c>` entry stops CMake configure with the file named. Both reverted.
- Review patches (rows 1, 4-7, 9, 13 of the triage log) touched only `test/game_script/*` and `lib/lua/README.md`; ctest 378/378 after them, `clang-format-fix` no-op, `diff -r` still empty. Firmware inputs unchanged, so the five builds, compile DB, `pio check`, and flash results stand.
- The real error for `local global = 1` is `<name> expected near 'global'`; the test asserts that text.

## Plan Change Log

## Review Triage Log

Pass 1 (lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment; run as headless `claude -p` sessions). The review diff omitted the 32,444 vendored lines of `lib/lua/src/` (listed by name with a note; byte-identity checked by `diff -r`). Counts: high 0, medium 0, low 11, false 2, maybe-false 0, descriptive 1. Patches 5 (rows 4, 5, 6, 7, 9), deferred 0, loopbacks 0.

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | blind | Compiled base library still has `dofile`/`loadfile`/binary `load`/stdout `print`; README overstates the sandbox | low | patch (README) | The sandbox (AD-6) is a later story and the frozen Never excludes it; README now says the filter is only the first layer and the host picks libraries and base functions. |
| 2 | blind | `LUAL_BUFFERSIZE` (512 B) locals break the 256 B rule; `LUAI_MAXCCALLS` sized for desktops | false | reject | The spine's Memory convention exempts `lib/lua` from the 256 B rule (16 KB `GameVM` stack); stack tuning belongs to the VM-task story. |
| 3 | blind | No budgeted allocator | low | reject | AD-6 arena allocator is a later story; frozen Never excludes state/allocator code. |
| 4 | blind, edge | Host CMake regex reads the whole JSON; `-U`/`-O`/other flags or `-<dir/>`/glob exclusions silently drift | low | patch | Now every `"+…"`/`"-…"` entry must be `+<*>`, `-<name.c>` (must exist) or `-D…`, else FATAL_ERROR naming it; mutations `-UFOO`, `-<sub/>`, `-<nosuch.c>` each stop configure. |
| 5 | blind | Host passes `LUA_COMPAT_GLOBAL=0` PUBLIC to C++ tests; firmware scopes it to the library | low | patch | Now PRIVATE; `build.ninja` shows the define on the 26 Lua units only, matching the x4pro compile DB. |
| 6 | blind, gap, intent | Nothing proves PlatformIO applies `library.json`; README's "cannot drift" overclaims | low | patch (README) | Firmware application verified this run (compile DB: define on exactly the 26 `lib/lua` units; logs: 26 units, none of the eight). README narrowed to what the host check proves and where the firmware evidence is; a committed firmware-side guard would need a fork file inside `src/` of the vendored tree. |
| 7 | blind | `ForControlVariableIsReadOnly` passes on any load error | low | patch | Asserts `attempt to assign to const variable 'i'` (from `lparser.c:320`). |
| 8 | blind | No tests that `io`/`os`/`debug`/`package` are absent; string/table/utf8 unexercised | low | reject | Absent libraries are not compiled (build logs, CMake list); opening and testing the allowed set is the sandbox story's work. |
| 9 | blind | README upgrade steps miss the test's version check and new `.c` files | low | patch | Steps added. |
| 10 | blind | `<climits>`-first rationale missing | low | reject | The rule is AD-4's; the code follows it. |
| 11 | blind | No project-level third-party licence notice | low | reject | The repo keeps no such list for the other vendored C libraries (miniz, expat, uzlib); Lua's notice ships in `src/lua.h`. |
| 12 | blind | Doubles are soft-float on C3/S3 | false | reject | Not a defect of this change: `lua_Number` stays Lua's default by AD-4 (only integer width is pinned); the spike measured 5.4.7 on device. |
| 13 | edge | `lua_tostring` may return NULL for non-string error objects → UB assigning to `std::string` | low | patch | `takeError()` falls back to `luaL_typename`. |
| 14 | intent | Size assert only evaluated on game envs; tarball identity not enforced by CI | - | descriptive | By design: AD-2 whole-file guard; the ticket verifies byte identity by `diff -r` against the checksummed tarball (done). |
| - | gap | No verification gaps found | - | - | - |

## Design Notes

Decision (compat vs. verify wording): the ticket's verify says no `LUA_COMPAT_` define appears, written assuming 5.5.1 defaults are off. They are not: `LUA_COMPAT_GLOBAL` defaults on, making `global` an ordinary name, against AD-4's "every option off; `global` is reserved" and the owner's recorded "no LUA_COMPAT_GLOBAL". Without editing `luaconf.h`, only `-DLUA_COMPAT_GLOBAL=0` achieves AD-4, so it is the single allowed compat define and the host test proves `global` is reserved.

Decision (layout): `lib/lua/src/` holds the tarball's `src/` verbatim, Makefile included, so `diff -r` is empty and upgrades are a directory swap; PlatformIO and CMake compile only `.c`, so the Makefile and `lua.c`-style files are inert. Fork-owned files sit one level up.

Decision (C++ include): `lua.hpp`, the distribution's own `extern "C"` wrapper, rather than a hand-written wrapper; it also brings `lauxlib.h`/`lualib.h` that GameScript will need.

Decision (sync): `library.json` is the single source for the excluded files and the compat define; the host CMake reads it.

## Verification

**Commands:**
- `sha256sum`, `diff -r lib/lua/src <scratch>/lua-5.5.1/src` -- expected: sum matches, no diff.
- `pio run -e <env>` for the five envs, logs in scratchpad -- expected: success; `grep -E 'lua|lib/lua'` hits.
- `pio project config | grep LUA_COMPAT_`; `pio run -e x4pro -t compiledb` + grep -- expected as in AC.
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j4` -- expected: all pass.
- timed `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: no defects.
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare` -- expected: exit 0, difference recorded.
- `./bin/clang-format-fix && git diff --exit-code`; after commit `python3 scripts/check_upstream_touches.py` -- expected: clean, exit 0.

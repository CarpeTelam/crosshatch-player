---
title: 'Games build flag, empty game libraries, and host suites'
type: 'chore'
ticket: '1'
created: '2026-09-26'
status: done
baseline_revision: 'f6de4dac090044bc22e67007a43f46383fa49a69'
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

**Problem:** No game code can land yet: there is no `FREEINK_CAP_GAMES` guard, no game libraries, and no host suites, so every later epic would first have to invent the build wiring and risk breaking a board or an upstream merge.

**Approach:** Tracer bullet per AD-2: set `FREEINK_CAP_GAMES=1` on exactly the eight game envs, add three near-empty libraries (`lib/GameCore`, `lib/GameScript`, `lib/GameIcons`) that a whole-file-guarded `src/games/GamesBuildAnchor.cpp` pulls into every env's build, and wire `test/game_core` and `test/game_script` into the host GoogleTest build.

## Boundaries & Constraints

**Always:**
- Flag on exactly: `x4pro`, `x4pro-gh_release`, `x4pro-gh_release_rc`, `sticky`, `sticky-gh_release`, `sticky-gh_release_rc` (in `platformio.ini`), and `simulator_x4pro`, `simulator_sticky` (in fork-owned `simulator.ini`). Not `simulator` (X4), not any C3/x4c/papermono env.
- Upstream files touched: only `platformio.ini` (ledger row 1, one env-scoped line per env) and `test/CMakeLists.txt` (ledger row 3, two `add_subdirectory` lines). No shared `[base]` line, no `lib_deps` entry.
- AD-2 C3 rules in `lib/Game*`: no namespace-scope objects with non-trivial constructors, no static buffers over 64 B. `GameCore` includes no Arduino/ESP-IDF/HAL/`src/` header; `GameScript` no HAL/Arduino/`GfxRenderer` header.
- Upstream style (PascalCase files and namespaces, `#pragma once`), `./bin/clang-format-fix` clean.

**Never:**
- The `--suppress=*:*/lib/lua/*` `check_flags` line, `lib/lua`, or `lua.h` in the anchor (story 4).
- Any runtime behaviour, activity, i18n key, or reference to the libraries from upstream code.
- Moving the `freeink-sdk` pointer or editing `.skills/`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Game env | `pio run -e x4pro` / `sticky` | flag defined; anchor compiles its body; `GameCore`, `GameScript`, `GameIcons` in dependency graph and compiled | build fails loudly on any error |
| Non-game env (chain LDF) | `pio run -e default` / `x4c` / `papermono` | flag undefined; anchor body empty; the three libraries still found (chain ignores `#if`) and compiled, unreferenced | same |
| Simulator (deep+ LDF) | `sim.sh build x4pro` | flag defined in `simulator_x4pro`; libraries built; answer to the ticket `unknown` recorded | same |
| Host tests | ctest | `GameCoreTest` and `GameScriptTest` discovered and pass | ctest non-zero |

</frozen-after-approval>

## Code Map

- `platformio.ini` -- `[env:x4pro]`, `[env:x4pro-gh_release]`, `[env:x4pro-gh_release_rc]`, `[env:sticky]`, `[env:sticky-gh_release]`, `[env:sticky-gh_release_rc]` each spell out `build_flags` (`${base.build_flags}` + device flags); append `-DFREEINK_CAP_GAMES=1` after the device flag line. Leave `[base]`, `check_flags`, `lib_deps` untouched.
- `.claude/skills/run-crosshatch-player/simulator.ini` -- `[env:simulator_x4pro]` / `[env:simulator_sticky]` extend `env:simulator` and own their `build_flags`; add the flag there, not to `[env:simulator]`. `lib_ldf_mode = deep+` evaluates `#if`, so the anchor's includes only count where the flag is set. `sim.sh setup` copies this into gitignored `platformio.local.ini`.
- `test/CMakeLists.txt` -- `add_subdirectory(...)` list; per-suite pattern in `test/fs_helpers/CMakeLists.txt` (`add_executable` with `${REPO_ROOT}/lib/...` sources, link `crosspoint_test_common GTest::gtest_main`, `gtest_discover_tests`).
- `lib/FsHelpers/FsHelpers.h` -- upstream style reference: PascalCase namespace per library.
- `.gitignore` -- ignores `*.generated.h`; the skeleton icon header must not use that suffix.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/GameCore.h`, `lib/GameCore/GameCore.cpp` -- `namespace GameCore { const char* libraryName(); }` returning `"GameCore"`, with a header comment naming the library's role (AD-1 domain) -- one linkable symbol per skeleton.
- [x] `lib/GameScript/GameScript.h`, `.cpp` -- same shape, `"GameScript"`.
- [x] `lib/GameIcons/GameIcons.h`, `.cpp` -- same shape, `"GameIcons"`.
- [x] `src/games/GamesBuildAnchor.cpp` -- whole file inside `#if FREEINK_CAP_GAMES` ... `#endif`; includes `<GameCore.h>`, `<GameIcons.h>`, `<GameScript.h>`; comment explains AD-2 (chain LDF, linker drops unreferenced code, story 4 adds `lua.h`). No definitions needed beyond the includes.
- [x] `platformio.ini` -- `-DFREEINK_CAP_GAMES=1` in the six envs above.
- [x] `.claude/skills/run-crosshatch-player/simulator.ini` -- `-DFREEINK_CAP_GAMES=1` in `simulator_x4pro` and `simulator_sticky`.
- [x] `test/game_core/CMakeLists.txt`, `test/game_core/GameCoreTest.cpp` -- `GameCoreTest` builds `lib/GameCore/GameCore.cpp`; one test asserts `libraryName()`.
- [x] `test/game_script/CMakeLists.txt`, `test/game_script/GameScriptTest.cpp` -- `GameScriptTest` builds GameScript and GameIcons sources; one test asserts both names.
- [x] `test/CMakeLists.txt` -- append `add_subdirectory(game_core)` and `add_subdirectory(game_script)` in their own trailing block.

**Acceptance Criteria:**
- Given the change, when `pio project config` output is parsed per env, then `FREEINK_CAP_GAMES=1` appears on exactly the six platformio.ini game envs, and after `sim.sh setup` also on `simulator_x4pro` and `simulator_sticky` and on no other env.
- Given each of `x4pro`, `sticky`, `default`, `x4c`, `papermono`, when built, then the build succeeds and the log names `GameCore`, `GameScript`, and `GameIcons` (dependency graph and compile lines).
- Given `sim.sh build x4pro` (dependencies permitting), when built, then it succeeds and the log shows whether the three libraries were built under deep+.
- Given the host test build, when ctest runs, then `GameCoreTest` and `GameScriptTest` run and pass along with the existing suites.
- Given the tree, when `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and `./bin/clang-format-fix` + `git diff --exit-code` run, then both are clean.
- Given `git diff --stat upstream/develop...HEAD`-style comparison for this commit, then the only upstream-existing paths changed are `platformio.ini` and `test/CMakeLists.txt`.

## Implementation Notes

- Implemented directly from the plan (no subagent tool in this session). Files: `lib/GameCore/GameCore.{h,cpp}`, `lib/GameScript/GameScript.{h,cpp}`, `lib/GameIcons/GameIcons.{h,cpp}`, `src/games/GamesBuildAnchor.cpp`, `test/game_core/`, `test/game_script/`, `test/CMakeLists.txt`, `platformio.ini`, `.claude/skills/run-crosshatch-player/simulator.ini`.
- Ticket `unknown` resolved empirically: yes, deep+ honours the flag with no further `simulator.ini` change. `simulator_x4pro` (flag set) lists `GameCore`, `GameIcons`, `GameScript` in its dependency graph and compiles them; the flagless `simulator` env compiles `GamesBuildAnchor.cpp` (empty) and pulls in none of the three.
- Chain LDF confirmed on all five CI envs: each log's dependency graph and compile lines name the three libraries. x4pro image is 5,657,610 B with and without this change (linker drops the unreferenced skeletons).
- `pio project config` (after `sim.sh setup`): flag on exactly `x4pro`, `x4pro-gh_release`, `x4pro-gh_release_rc`, `sticky`, `sticky-gh_release`, `sticky-gh_release_rc`, `simulator_x4pro`, `simulator_sticky`.
- Environment fixes outside the repo, needed for the custom-sdkconfig envs (`sticky`, `default`): appended the egress gateway CA to the penv certifi bundle (uv reaches pypi.org directly, not via the agent proxy) and re-pinned `pioarduino==6.1.19` in `~/.platformio/penv`; installed `libsdl2-dev`, `xdotool`, `imagemagick` for the simulator and `libpcre3` for cppcheck.
- `./bin/clang-format-fix` formats only tracked files, so new files were marked intent-to-add before running it.

## Plan Change Log

## Review Triage Log

Pass 1 (lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: high 0, medium 0, low 3, false 9, maybe-false 0. Patches 0, deferred 1.

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | blind | `GameScriptTest` omits GameCore although GameScript is said to depend on it | false | reject | GameScript includes nothing from GameCore yet; the suite builds and passes. The story that adds the include adds the source. |
| 2 | blind | No `library.json` dependencies between the game libraries | false | reject | Chain/deep+ LDF follow library-to-library includes; AD-2 puts discovery on the anchor and forbids `lib_deps` additions. |
| 3 | blind | "Linker drops them" unverified; suggests `lib_ignore` on C3 | false | reject | x4pro image byte-identical to baseline (5,657,610 B); AD-2 chose compile-everywhere and its C3 rules ban static initialisers; `lib_ignore` would add upstream `platformio.ini` lines AD-3 does not allow. |
| 4 | blind | Flag copied into eight envs instead of a shared section | false | reject | AD-2 / AD-3 row 1 prescribe env-scoped lines; a shared section would reorganise upstream `platformio.ini`; `-Wundef` is not enabled. |
| 5 | blind | No `test/game_icons` suite | false | reject | The intent names exactly two suites; GameIcons is covered in `GameScriptTest`. |
| 6 | blind | Layering rules in headers not enforced by tests | low | reject | No violation exists; enforcement needs new targets or a lint (added complexity), unlikely to bite in this story. |
| 7 | blind | Tests pin a placeholder symbol later stories may remove | low | reject | Removing it means editing one test line; cosmetic. |
| 8 | blind | Simulator flag lives only in the skill's `simulator.ini` | false | reject | That file is the only simulator config; `sim.sh setup` copies it into `platformio.local.ini`. |
| 9 | edge | A clone that ran `sim.sh setup` before this change keeps a stale `platformio.local.ini` block without the flag | low | defer | Real, but pre-existing `sim.sh` behaviour for any `simulator.ini` edit (SKILL.md says re-run setup); fixing it changes `sim.sh`, outside this story. |
| 10 | edge | Anchor comment states deep+ behaviour as fact, unverified | false | reject | Verified: `simulator_x4pro` builds the three libraries, flagless `simulator` builds none. |
| 11 | edge | Plan records no deep+ result | false | reject | Fix is a plan edit (rejected by rule); result now recorded in Implementation Notes. |
| 12 | intent | Diff implements the literal AD-2 reading; host tests do not exercise the per-env build surface | false | reject | Descriptive; the per-env surface is verified by the five builds, the simulator builds, and `pio project config` above, as the plan's Verification requires. |
| - | gap | No verification gaps found | - | - | - |

## Design Notes

The `libraryName()` symbols are placeholders chosen to preempt no later API (AD-19 API level, AD-24 icon names, AD-25 `ForkRelease.h` all belong to later stories). They exist so each library has a translation unit the build log names and the host suites can link; later stories may keep or delete them. A plain function returning a string literal has no static-init cost on the C3.

Decision (ticket `unknown`): resolve empirically by building `simulator_x4pro` and reading the LDF dependency graph; record the result in Implementation Notes. The expected answer is that deep+ honours the flag from each simulator env's `build_flags`, so no further `simulator.ini` change is needed beyond the two flag lines.

## Verification

**Commands:**
- `pio project config --json-output` (parsed per env) -- expected: flag on exactly the eight game envs after `sim.sh setup`.
- `pio run -e x4pro`, `-e sticky`, `-e default`, `-e x4c`, `-e papermono` (logs in scratchpad) -- expected: success; `grep -E 'GameCore|GameScript|GameIcons'` hits in each log.
- `.claude/skills/run-crosshatch-player/sim.sh setup && sim.sh build x4pro` -- expected: success, libraries in graph.
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j4` -- expected: all pass including the two new suites.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: no defects.
- `./bin/clang-format-fix && git diff --exit-code` -- expected: clean.

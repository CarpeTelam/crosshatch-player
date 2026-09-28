---
title: 'Refactor sweep'
type: 'refactor'
ticket: '7'
created: '2026-09-28'
status: done
baseline_revision: '201b7c557da0353cf20d78ccdf0146d94e1fbe4f'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/upstream-touches.md'
  - '{project-root}/docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The epic-script-runtime retrospective's AI-11 hardening list and this epic's own deferred review findings
(`deferred-work.md` `## 3.1`–`## 3.6`) are open; the spine's Operational-envelope CI row misses the two fork jobs this
epic and the retro follow-up added; the epic's final flash and static RAM deltas are unrecorded.

**Approach:** Fix each small item in place (F5, F7, F8, F9, A4, A5, R3, R6, R9, 3.6's two), document the ones whose
fix is documentation (F6, R4, R10), leave A3 to epic-install-and-launcher and R8 to epic-game-api-docs, carry the
harness-, device- and SDK-bound findings as deferrals with a recommended reason, and measure the final x4pro deltas
from a fresh tree of the commit.

## Boundaries & Constraints

**Always:** upstream files only as the ledger allows (english.yaml: append `STR_GAMES_*` at the end, row 2);
fork-only code guarded as today; Screens never name `GameScript`; every gate change runs once from a fresh archive
tree of the commit; figures are measurements with their method; builds under the shared lock.

**Never:** edit `FrontlightPanelActivity.cpp`, `ActivityManager.*` beyond rows 4–5, `ci.yml`, `freeink-sdk`, `.skills/`,
generated files, or `API_LEVEL_FROZEN`; loosen a gate; change any icon name, drawing or the v1 set (entry 8's).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Play again with a step in flight | a step popped during Over publishes after Play again | the match renders only once the new round's first frame is published (R3) | none |
| Host failure in `LuaGame` | scratch/state allocation fails, or an entry runs before `load` | error view shows `tr()` text (`STR_GAMES_OUT_OF_MEMORY` / new `STR_GAMES_NOT_LOADED`), never host English (A5) | stays a ScriptError |
| Long host message | a message over 159 B ending in a multi-byte char | cut at a UTF-8 boundary (R6) | none |
| Lone surrogate | `game_codec.encode('\ud800')`, as value or key | `CodecError('bad_type')` / `CodecError('bad_key')` (F9) | no `UnicodeEncodeError` |
| Test file with no tests | a `*_test.py` that runs 0 tests and exits 0 | `Fork script tests` fails it (F7) | `::error::` line |
| Unguarded upstream game include | an `UPSTREAM_EDGES` include outside `#if FREEINK_CAP_GAMES` (or in its `#else`) | Layer check fails with path:line (3.6) | exit 1 |

</frozen-after-approval>

## Code Map

- Work only in `/home/user/wt-sweep` (branch `epic3/sweep`); run `git submodule update --init --recursive` first. Every `pio run`, `pio check`, and host-test CMake configure/build runs as `flock /tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/build.lock sh -c '<commands>'`; scratch under that scratchpad's `3.7/`. Toolchain installed; do not reinstall or touch certificates. Never commit `_bmad/render/`; do not commit at all. Implementation verifies with host tests, `scripts/*_test.py`, `check_layers.py`, `check_upstream_touches.py`, and `pio run -e x4pro` and `-e default`; the other envs, `pio check`, the fresh tree, and the measurement follow review.
- `scripts/check_flash_budget.py` -- `RAM_SECTIONS`/`REQUIRED_RAM_SECTIONS` (:77-80), docstring (:9-44); literal exits
  :280, :542, :569. `check_flash_budget_test.py` `RAM`, `REAL_SIZE_OUTPUT` (`.iram0.text 84091`).
- `.github/workflows/crosshatch-ci.yml` -- header comment :9 (RAM sections); `fork-script-tests` step :201-219.
- `scripts/check_upstream_touches.py:162`, `scripts/fork_release.py:814`, `scripts/fork_common.py` `PASS/FAIL/COULD_NOT_RUN`.
- `scripts/game_codec.py` `_as_bytes` (:92; callers :111 key, :178 value, :314/:324 blob magic).
- `scripts/check_layers.py` -- `upstream_problems` (:339), `blank`, `includes`, `UPSTREAM_EDGES`; `check_layers_test.py`.
- `docs/crosshatch/api-level-1.txt` header (:1-43) -- F8 line; R4 note by the `fn ch.gfx.*` entries (comment, no CRC change).
- `lib/GameScript/LuaGame.{h,cpp}` -- `fail` (:176, snprintf), host strings :191/:193 ("not enough memory"), `!L` guards
  ("game not started"); `utf8Cut` in `ChBindings.h`; `encodeTop` (:406) duplicates `ChBindings.cpp` `storeSet` (:249-258).
- `lib/GameScript/GameTimer.{h,cpp}` `takeDue`; `src/games/GameVM.{h,cpp}` `pollTimer`, `errorMessage`, `failedOutOfMemory`, `playAgain`.
- `lib/GameScript/SoloRounds.{h,cpp}` -- add a started-round count beside `ended`.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `handle` PlayAgain (:154-158), `loopPlaying` render request (:296-300),
  `vmHealthy` (:239), `stopStuckVm` (:230); `STOP_TIMEOUT_MS` (.h:54).
- `src/activities/games/GamesListActivity.cpp:25,27`, `src/games/GameAssets.cpp:17,60` -- `/.games`, `PATH_BUFFER = 96`.
- `test/game_script/LuaGameFixture.h` `SessionGame` (:178); `HostBindingsTest.cpp:40` `pollTimer`; `SoloRoundsTest.cpp` `tick`;
  `SessionGameTest.cpp:90-91` (Play again by hand); Lua-including tests without AD-4's assert: `ArenaAllocatorTest`,
  `SandboxTest`, `SessionGameTest`, `ApiSurfaceTest` (also no `<climits>`).
- Docs: `docs/crosshatch/game-icons.md` :83-92; `game-canvas.md` Solo states / Play again row; `upstream-touches.md`
  "enforces paths only"; spine `ARCHITECTURE-SPINE.md` :83 (RAM sections), :246 (load OOM text), :534 (CI row).

## Tasks & Acceptance

**Execution:**
- [x] `scripts/check_flash_budget.py` (+test, workflow comment, spine :83) -- F5: count `.iram0.*` sections present in the ELF
  (`.iram0.text` required) in the static-RAM total; F6: docstring names what the objects scan does not read (`lib/lua`,
  guarded fork code in ledgered upstream files, a game library not named `lib/Game*`); A4: exits via `fork_common`.
- [x] `scripts/check_upstream_touches.py`, `scripts/fork_release.py` -- A4: `fork_common` exit constants.
- [x] `.github/workflows/crosshatch-ci.yml` -- F7: capture each test's output; fail a file whose log has no `Ran [1-9]`;
  `docs/crosshatch/fork-scripts.md` says so.
- [x] `scripts/game_codec.py` (+test) -- F9: lone surrogates raise `CodecError`.
- [x] `docs/crosshatch/api-level-1.txt` -- F8 header line (behaviour is contract only when it is an entry); R4 comment
  (coordinates and sizes saturate to −32,768..32,767 before clipping); `game-canvas.md` says the same.
- [x] `lib/GameScript/LuaGame.*`, `ChBindings.*` -- R6: `fail` cuts with `utf8Cut`; A4: one encode-error message helper
  used by `encodeTop` and `storeSet`; A5: a host-failure kind (OutOfMemory, NotLoaded) beside the English log text.
- [x] `src/games/GameVM.*`, `GameMatchActivity.*`, `english.yaml` -- A5: `GameVM::failure()` enum; one
  `vmFailureText()` used by `vmHealthy` and `stopStuckVm`; append `STR_GAMES_NOT_LOADED`; spine :246 wording.
- [x] `SoloRounds.*`, `GameVM.*`, `GameMatchActivity.cpp` -- R3: `roundsStarted()`; Play again records it and the loop
  asks for no render until it moves; `game-canvas.md` Play again row.
- [x] `GameTimer.*`, `GameVM.cpp`, tests -- R9: `takeDueEvent` used by `pollTimer` and the tests; `SessionGame` steps
  through a `SoloRounds`; `SessionGameTest` Play again through it.
- [x] Four Lua-including tests -- A5: `<climits>` first and `static_assert(sizeof(lua_Integer) == 8)`.
- [x] `src/games/GamePaths.h` (new) -- A4: `/.games` and the path size for `GamesListActivity` and `GameAssets`;
  `GameMatchActivity.h` comment: `STOP_TIMEOUT_MS` and `GameVM::ABANDON_WAIT_MS` are independent (AI-4 owns the bound).
- [x] `scripts/check_layers.py` (+test), `upstream-touches.md` -- 3.6: an `UPSTREAM_EDGES` include must sit in the
  `FREEINK_CAP_GAMES` branch of an `#if`/`#ifdef`.
- [x] `game-canvas.md` -- R10: the match's loop (watchdog, timer poll, store flush) pauses while an overlay such as the
  light panel is open; fix deferred (needs files outside the ledger).
- [x] `docs/crosshatch/game-icons.md` -- 3.6: screens now draw library icons; renaming `game_controller` breaks x4pro/sticky.
- [x] Spine CI row -- add `Layer check`, `Icons up to date`.
- [x] `deferred-work.md` `## 3.7` -- new deferrals; record final deltas in `game-icons.md` Size.

**Acceptance Criteria:**
- Given the item list below, when the story is built, then every AI-11 item and every `## 3.x` finding has an outcome.
- Given the committed tree, when host tests, `scripts/*_test.py`, five envs, `pio check`, format and `check_layers.py`
  run, then all pass; the changed gates pass from a fresh archive tree.

## Implementation Notes

- F5: `compare` counts every `.iram0.*` section `size -A` reports (`.iram0.vectors`, `.iram0.text`, `.iram0.text_end`,
  `.iram0.data`, `.iram0.bss` on the x4pro ELF), with `.iram0.text` required; a section only one build has is 0 in the
  other (`ram_sections`, `ram_report`). Tests: IRAM growth alone fails, every `.iram0.*` counts, non-RAM sections do not.
- A5: `LuaGame::HostFailure` (None, OutOfMemory, NotLoaded) sits beside the English text; `GameVM::Failure` (None,
  Script, NoSession, OutOfMemory, NotLoaded) replaces `failedOutOfMemory()`; `vmFailureText()` in
  `GameMatchActivity.cpp` maps it to `tr()` text. Headlines unchanged: only NoSession (the Session never fit) is
  "could not start"; a `LuaGame::load` OOM or NotLoaded keeps "stopped with an error", as spine AD-14 says.
- R3: `SoloRounds::roundsStarted()` moves after a round's first draw publishes and before `countRoundEnd`; the match
  sets `roundsStartedAwaited = roundsStarted() + 1` on Play again and `loopPlaying` requests no render below it. The
  pause/overlay repaint path is not gated; recorded as a residual in `deferred-work.md` `## 3.7`.
- R9: `SessionGame` owns an `InputQueue` and a `SoloRounds`; `start()` is `load()` + `SoloRounds::start`, `step()` is
  `SoloRounds::step`, and `playAgain()` mirrors `GameVM::run`. `SessionGameTest.EveryFaultScriptEndsWithTheReadmesText`
  now runs the first round through `SoloRounds::start` and tells Setup from Draw by the Lua calls made (setup and
  status are one call each).
- 3.6: `check_layers.games_guarded_lines` tracks `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif` on comment-blanked
  text; an `UPSTREAM_EDGES` include must sit in a branch whose condition is the games flag (alone, `defined(...)`,
  `== 1`, or `&&`-joined, no `||`). An `#else`, `#ifndef`, negation, or other spelling fails visibly.
- Not done here, by the plan's own order (they follow review): the `x4c`, `papermono`, and `sticky` builds, `pio check`,
  the fresh archive tree run of the changed gates, and the final x4pro flash and static RAM deltas for `game-icons.md`
  Size (the last task stays unchecked).

## Plan Change Log

## Review Triage Log

Pass 1 (2026-09-28). The four lenses (blind hunter, edge-case hunter, verification gap, intent alignment) ran as
context-free subagents over the diff from `201b7c55` (this plan excluded). Verdicts: 0 high, 0 medium, 17 low,
3 false, 0 maybe-false; plus 3 verified gaps (2 defer, 1 patch) and the intent audit's divergences. Routes: 10 patch
(sent to the implementer), 2 defer, the rest rejected.

| # | Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- | --- |
| 1 | blind | Spine AD-14's new sentence says only Lua's message reaches the view untranslated; guard and binding messages (budget, heap cap, encode errors) do too | low | patch: scoped to the two load cases; messages raised inside a Lua call stay the script's |
| 2 | blind | `STR_GAMES_NOT_LOADED` / `Failure::NotLoaded` unreachable on GameVM's path | low | patch: `GameVM.h` says it is defensive (`run` calls entries only after `load()` is Ok) |
| 3 | blind, verif. gap | No test of `GameVM::failure()`, `vmFailureText`, the headline choice | gap | defer: `GameVM.cpp`/`GameMatchActivity.cpp` are not host-built; AI-2's harness (deferred-work 3.7) |
| 4 | blind | game-canvas.md error-view paragraph omits the load OOM / not-loaded text | low | patch: one sentence added |
| 5 | blind | After Play again the end-of-round menu stays visible while taps go to the new round | low | reject: pre-existing window (one `restart()`), queue cleared at Play again; the gate only adds an old step's tail |
| 6 | blind | fork-scripts.md local loop does not apply the `Ran` check | low | patch: the loop applies it |
| 6b | blind, edge | A file whose tests are all skipped passes; output buffered until the file ends | low | reject: no fork test skips; parsing skip counts adds a branch for a case never met |
| 7 | blind | `REAL_SIZE_OUTPUT` has only `.iram0.text`; `.iram0.text_end` is alignment padding | low | reject: the fixture is labelled shortened; the real ELF's sections and their delta are the fresh-tree measurement (Verification) |
| 8 | blind, edge | Fault-script test's call-count rule reports a start-time `over` delivery failure as Draw | low | patch: per-step `session->start()`/`draw()` restored for that test (it attributes README steps, not the loop) |
| 9 | blind | `static_assert(sizeof(lua_Integer) == 8)` only in tests; `<climits>` unused | false | `CallGuard.cpp`, `Codec.cpp`, `LuaGame.cpp`, `Sandbox.cpp`, `ChBindings.cpp`, `GamesBuildAnchor.cpp` already assert; AD-4 requires `<climits>` before `lua.h` |
| 10 | blind | api-level-1.txt says comments can change without the CRC, but the freeze job compares the whole file | low | patch: comments are outside the CRC but frozen with the file |
| 11 | blind | Saturation example: "ends at x = -1" vs "from −32,768 to −1"; it covers −32,768..−2 | low | patch: both say −32,768..−2 |
| 12 | blind, edge | `check_layers.py` misses `#elifdef`/`#elifndef`; an include after `#elifndef X` passes | low | patch: both recognised, with tests |
| 13 | blind | `GamePaths` does not tie `PATH_BYTES` to the id limit; `GameSaveStore` keeps its own path | low | reject: values unchanged from before; `/.games-data` is a different folder with its own size |
| 14 | blind | `TakeDueEventMakesTheTimerEventOnce` does not check `serial` directly | low | reject: `accepts()` true then false after re-arm pins the serial |
| 15 | blind | crosshatch-ci.yml header line ~169 columns | low | patch: rewrapped (introduced by the build agent's own reflow) |
| 16 | blind | Comments cite retro ids; "The bound on their sum is AI-4's" names no bound | low | patch: the AI-4 comment says what waits; other ids match the repo's existing comment style |
| 17 | blind | `encodeErrorMessage` cuts at 96 B where `luaL_error` did not | low | reject: longest message ("ch.store.set: the store cannot be encoded (float_key)") is well under 96 B; `LuaGame`'s pairs ("setup"/"state", "input"/"move") are shorter |
| 18 | edge | `blank()` blanks `\f`/`\v` inside comments, shifting `splitlines()` numbers | low | reject: needs a form feed in an upstream comment; fix adds complexity |
| 19 | edge | `utf8Cut` backs to 0 on 160+ continuation bytes, so the message is empty | low | reject: needs a game error of 160+ invalid bytes; a fallback adds a branch |
| 20 | edge | Load chunk running out of arena shows Lua's English "not enough memory" | false | That is Lua's own `LUA_ERRMEM` message inside a Lua call, a memory-cap ScriptError whose message AD-14 shows; the amendment covers only the scratch and state allocations |
| 21 | verif. gap | R3 render gate (`roundsStartedAwaited`, `loopPlaying`) untested; only the counter is | gap | defer: activity not host-built; AI-2's harness (deferred-work 3.7) |
| 22 | verif. gap | F7 gate has no negative-case check | gap | patch: the fresh-tree run of the step includes a scratch no-test file (Verification) |
| 23 | intent | Deltas not recorded; fresh-tree runs pending | -- | done after review (Verification, game-icons.md Size) |
| 24 | intent | Deferrals belong as `Assumption for entry 8:` lines in the epic Notes | false | the brief reserves the epic Notes for the orchestrator; the report lists them under "Deferred for entry 8:" |
| 25 | intent | R4 documented on a surface the F8 line calls non-binding | -- | the plan's Assumed choice, reported |
| 26 | intent | `HostBindingsTest::pollTimer` still mirrors `GameVM::pollTimer` | -- | accepted: `GameVM` is not host-built; both go through `takeDueEvent` |
| 27 | intent | Spine additions sit inside owner-labelled amendments | low | patch: each carries its own "amended 2026-09-28 (epic-icon-library entry 7)" marker |
| 28 | intent | A4's headroom duplicate outcome only in the plan | -- | accepted: the plan's Design Notes record it (`CallGuard::hasHeadroom`, AI-13) |

## Design Notes

Item outcomes. AI-11: F5 fixed; F6 documented; F7 fixed; F8 fixed (header line); F9 fixed; A3 left to
epic-install-and-launcher (trigger: `pack_game.py` importing the codec); A4 fixed (encode message, `/.games` + path
size, exit codes), headroom duplicate already gone (`CallGuard::hasHeadroom`, AI-13), the two 500 ms constants
documented as independent; A5 fixed (strings, asserts); R3 fixed; R4 documented; R6 fixed; R8 left to
epic-game-api-docs; R9 fixed; R10 documented, fix deferred. Epic findings: 3.6 icon doc and guard check fixed; 3.1
Icon replay test, 3.2 device-side tests and 3.6 tab-order test deferred to AI-2's harness (epic-install-and-launcher);
3.2 image replay timing deferred (needs a device; this epic has no device run); 3.2 budget counting stays a handoff;
3.5 `DialogOption` icon deferred (an upstream SDK proposal).

`GameVM::failure()` is a `GameVM` enum so Screens never name `GameScript`. R3: the round count moves after the new
round's first draw publishes, so gating renders on it skips any old-round frame; coalescing then shows the newest.

**Assumed (reversible):** R4 documented, not clipped in wide integers or made a `limit` entry, because the F8 line keeps
comments non-contract and a later fix stays free; the new key reads "The game did not load".

## Verification

**Commands:**
- `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- all pass
- `for t in scripts/*_test.py; do python3 "$t"; done` -- each passes, each `Ran` > 0
- five `pio run -e <env>` and `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` under the lock -- pass
- `python3 scripts/check_layers.py`, `check_upstream_touches.py`, `./bin/clang-format-fix` twice -- clean
- fresh archive tree: flash budget job steps (build on/off, compare, objects), the fork-script-tests step, Layer check -- pass; record deltas

**Results (build agent, after review; code at `965c55d7`, this commit adds only the measured figures to docs):**
- Host tests under the lock: 697/697 pass.
- `scripts/*_test.py`: all eight pass, each with `Ran` > 0 (9, 78, 37, 18, 27, 76, 22, 23).
- `pio run` under the lock: `sticky`, `x4c`, `papermono` SUCCESS (before the review patches, which touched only a test
  and comments in C++), `x4pro` and `default` SUCCESS after them; `sim.sh setup` + `sim.sh build x4pro` SUCCESS.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (the `default` env) and the same with
  `-e x4pro`: PASSED. The packaged cppcheck needs `libpcre.so.3`, which the container lacks; it ran with
  `LD_LIBRARY_PATH` at a copy extracted (`apt-get download libpcre3`, `dpkg-deb -x`) into the story's scratch, no
  system change.
- `check_layers.py` (358 edges, 90 files) and `check_api_freeze.py` (nothing frozen) pass; `check_upstream_touches.py`
  at `965c55d7`: PASS, trial merge of `upstream/develop` clean.
- `./bin/clang-format-fix` twice: no change, `git status` clean apart from `_bmad/render/`.
- Fresh tree: a `git archive 965c55d7` tree plus every submodule's archive (nested Lucide included, each at the
  commit's gitlink), since the three changed gates read no git history:
  - `Layer check`: `python3 scripts/check_layers.py` exit 0.
  - `Fork script tests`: the workflow step's shell, extracted from `crosshatch-ci.yml` and run with `bash -e`: exit 0,
    every file `Ran` > 0; with a scratch `scripts/zz_nomain_test.py` that has a test but no `unittest.main()`, exit 1
    with `::error::scripts/zz_nomain_test.py ran no tests ...` (review finding 22).
  - `x4pro flash budget` under the lock: `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`,
    `objects`: all exit 0. Flash: on 5,871,712 B, off 5,675,376 B, +196,336 B (59,664 B to spare; 3.4 measured
    +196,480 B at `7e54d5d6`; epic 2 closed at +150,448 B, so this epic adds +45,888 B; the base `1eacdc77` was not
    re-measured). Static internal RAM, the gate's new sum: +776 B of 1,024 (248 B to spare): `.dram0.bss` +8,
    `.iram0.text` +684, `.iram0.text_end` +84, the rest +0; the old `.dram0.*` + `.noinit` sum is +8 B as before. The
    IRAM is FreeRTOS task functions only games link (`vTaskSuspend`, `vTaskResume`, `eTaskGetState`,
    `uxTaskGetStackHighWaterMark`, `pxTaskGetStackStart`; 659 B by `objdump -t`) plus alignment padding. Objects:
    37 checked, no problems.


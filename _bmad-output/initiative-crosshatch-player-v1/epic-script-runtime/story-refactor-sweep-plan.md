---
title: 'Refactor sweep'
type: 'refactor'
ticket: '15'
created: '2026-09-27'
status: 'built'
baseline_revision: '932ef83571aaa6ab4904a710130e43ed8e5cdbe1'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/.skills/refactor-for-review/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Entries 2.1 to 2.14 and 2.17 left dead skeleton code, small duplications, stale comments, and deferred-work entries that later stories already resolved, plus two cheap test gaps.

**Approach:** Act on every item below marked IN, as cleanup with no behaviour change; record every OUT with its reason. Scope: each `deferred-work.md` entry whose source_plan is in this epic folder, each plan's Review Triage Log and Implementation Notes, the orchestrator's handoff notes, and a read of the epic's code.

### Items

Deferred-work entries (by source story):
1. 2.2 `fr notes` job summary untested: IN, close with b3f89a32 (`PublishTest.test_notes_go_to_the_file_and_the_job_summary`).
2. 2.1 GameVM, GameAssets, GamesList, GameMatch untested: OUT, needs a host harness over the simulator's FreeRTOS shim.
3. 2.1 tracer's English error strings: IN, already resolved by 2.13; add acf780ea.
4. 2.1 `libraryName()` skeletons: IN, delete `lib/GameCore/GameCore.{h,cpp}`, `lib/GameScript/GameScript.{h,cpp}`, `test/game_core/GameCoreTest.cpp`, the GameScript assertion in `GameScriptTest.cpp`, and their CMake lines; `GamesBuildAnchor.cpp` includes `<Session.h>` and `<LuaGame.h>` (the spine's AD-1 table keeps each library's role); close with this commit.
5. 2.4 list filter and `gameHostCaps()` untested: IN for `gameHostCaps()` (item 23); the filter stays OUT (Storage harness); update the entry, which stays open.
6. 2.4 CanvasClip format drift: IN, close with 03f2ef3c.
7. 2.6 codec C stack at depth 16: IN, close with a4212569 (measured in 2.8).
8. 2.7 owner decision on hook-blind C recursion: IN, close with 376a4c62 (Lua defines, `__gc` refusal) and 601c0718 (spine).
9. 2.7 `loop` fixture on an X4 Pro: OUT, device run (entry 16).
10. 2.8 codec stack (marked resolved): IN, add a4212569.
11. 2.8 heap cap vs arena (marked resolved): IN, add c25a2ff6.
12. 2.8 `GameVM::run` composition: OUT, harness (as 2).
13. 2.9 `onEnter` canvas: OUT, harness.
14. 2.9 `ch.text_width` vs kerning: IN, close with 13bcd449 (replay draws glyph by glyph at the table's advances).
15. 2.10 `pollTimer`, stale-timer drop, slot leak: OUT, harness.
16. 2.11 device-side replay wiring: OUT, harness.
17. 2.14 `seats_max` and `manifest` keys unpinned: IN for `seats_max` (item 23); keys OUT (Manifest has no key table; adding one changes Manifest); update the entry.
18. 2.12 restore and `flushIfDue` wiring: OUT, harness.
19. 2.13 match transition actions: OUT, harness.
20. 2.13 Home over the light panel: OUT, upstream `ActivityManager` is outside the ledger; unverified.

Handoff notes and plans:
21. Every handoff item is already done (df9b09b5, 376a4c62, a4212569, 13bcd449, c25a2ff6, 43c4e09f, e8420aaa, 5f8582c9): no action.
22. Triage rows stay as routed. Cleanup-shaped ones: the 2.4 test format drift is fixed (4779ab69); OUT are the two-import style in `check_flash_budget.py` (tests read module attributes), enum-typed `fn` parameters (new grammar), the publish job's `test/game_core` checkout (release-workflow change), message or CLI wording (2.5 row 7, 2.6 row 7, 2.7 `'?'`, 2.9 row 7, 2.10 row 6: behaviour), and `ReleaseJsonParser` asserts (upstream, unledgered).

Code sweep:
23. IN test: `GameCoreTest` builds `src/games/GameHostCaps.cpp` and pins `gameHostCaps()` to `API_LEVEL`, `API_MIN_LEVEL`, and the list's `seats_max`.
24. IN: one `utf8Cut` (ChBindings) replaces `copyReason`'s copy of the loop in LuaGame.cpp.
25. IN: FrameReplay uses `SIZE_NAMES`, not its own `TEXT_SIZE_NAMES`.
26. IN: one Sandbox helper pushes the two IRandom seed words (twice today).
27. IN: the "VM did not stop" log moves into `abandonVm()` (logged before both calls today).
28. IN: GameAssets.cpp's two `EXT = 4` become one constant.
29. IN: `fork_common.file_at(ref, path, cwd=None)` replaces the ls-tree-then-show copies in `check_api_freeze.py` and `fork_release.py`, with a test and a fork-scripts.md row.
30. IN comments: `TextMetrics::standIn`, `IRandom.h`, `StoreSlot.h`, `BindingContext`, `GameVM::postInput`, broken wraps in `GameVM.h` and `GameMatchActivity.h`, and story or entry names in test comments.
31. OUT: `InputKind`/`InputEvent` aliases, about 40 test lines use them.
32. OUT: `lua_Integer` asserts in six files, a per-file guard; one copy needs a new header.

## Boundaries & Constraints

**Always:** no behaviour change; every existing test passes unchanged except tests of removed code (edits to test files are comments only); fork-only paths.

**Never:** `api-level-1.txt` or `ApiLevel.h`; upstream files; `lib/lua`, workflows, `freeink-sdk`, `.skills`; new harnesses; reformatting beyond `./bin/clang-format-fix`.

</frozen-after-approval>

## Code Map

- `src/games/GamesBuildAnchor.cpp` -- keep `<GameIcons.h>`, `<lua.hpp>`, the assert; chain LDF resolves `Session.h` and `LuaGame.h` only in lib/GameCore and lib/GameScript (no other lib or the framework has them).
- `test/game_core/CMakeLists.txt` -- `GameCoreTest` sources; add `src/games/GameHostCaps.cpp` with `set_source_files_properties(... COMPILE_DEFINITIONS FREEINK_CAP_GAMES=1)` and include `src/games`. `ApiLevelTest.cpp` has `loadSurface()`; `seats_max` is an entry of kind `seats_max`, body the number.
- `lib/GameScript/LuaGame.cpp` `copyReason`, `ChBindings.cpp` `utf8Cut` (anon namespace) and `ChBindings.h` (`LOG_LINE_BYTES`, `SIZE_NAMES`).
- `src/games/FrameReplay.cpp` `TEXT_SIZE_NAMES` (log line only).
- `lib/GameScript/Sandbox.cpp` `guardedRandomseed`, `openSandbox` reseed.
- `src/activities/games/GameMatchActivity.cpp` `stopVm`, `stopStuckVm`, `abandonVm`.
- `src/games/GameAssets.cpp` `moduleNameOf`, `looksLikeLua`.
- `scripts/check_api_freeze.py` `read_file`; `scripts/fork_release.py` `tag_api_level`; `scripts/fork_common.py` (git helpers), `fork_common_test.py` (throwaway-repo fixture); `docs/crosshatch/fork-scripts.md` table.
- Comments: `lib/GameScript/TextMetrics.h:100`, `lib/GameCore/IRandom.h`, `lib/GameScript/StoreSlot.h:74`, `ChBindings.h` `BindingContext`, `src/games/GameVM.h` (33, 60, 107-130), `GameMatchActivity.h:21-27`, `test/game_core/ApiLevelTest.cpp:95`, `test/game_script/SandboxTest.cpp:53`, `CodecTest.cpp:197,425`.
- `_bmad-output/implementation-artifacts/deferred-work.md` -- lines 43-103.

## Tasks & Acceptance

**Execution:**
- [ ] Item 4 -- delete the skeletons and test, edit `GameScriptTest.cpp`, both CMake lists, the anchor.
- [ ] Item 23 -- `test/game_core/GameHostCapsTest.cpp` (new) plus CMake.
- [ ] Items 24-28 -- the C++ dedupes.
- [ ] Item 29 -- `fork_common.file_at`, callers, test, doc row.
- [ ] Item 30 -- comment edits.
- [ ] Items 1, 3, 5-8, 10, 11, 14, 17 -- `deferred-work.md` closed or updated in place.

**Acceptance Criteria:**
- Given the Verification commands, then each passes; `ctest` passes with only `GameCoreTest.LinksLibrary` gone and `GameHostCapsTest` new.
- Given `git grep libraryName -- lib/GameCore lib/GameScript test`, then nothing matches.

## Implementation Notes

- Implemented directly (no coding subagent in this session). Deleted: `lib/GameCore/GameCore.{h,cpp}`, `lib/GameScript/GameScript.{h,cpp}`, `test/game_core/GameCoreTest.cpp`. New: `test/game_core/GameHostCapsTest.cpp` (2 tests). Changed: `GamesBuildAnchor.cpp`, `ChBindings.{h,cpp}` (`utf8Cut` public), `LuaGame.cpp`, `Sandbox.cpp` (`pushSeed`), `FrameReplay.cpp`, `GameAssets.cpp` (`LUA_EXT_BYTES`), `GameMatchActivity.{h,cpp}`, comments in `TextMetrics.h`, `IRandom.h`, `StoreSlot.h`, `GameVM.h`, and three test files; `test/game_core/CMakeLists.txt`; `scripts/fork_common.py` (`file_at`), `fork_common_test.py` (`FileAtTest`, 3 tests), `check_api_freeze.py`, `fork_release.py`; `docs/crosshatch/fork-scripts.md`; `deferred-work.md` (items 1, 3 to 8, 10, 11, 14, 17).
- The Acceptance grep for `libraryName` still hits `test/game_script/GameScriptTest.cpp`: that is `GameIcons::libraryName`, the icon library's skeleton, which epic-icon-library owns; `lib/GameCore`, `lib/GameScript`, and every GameCore or GameScript test are free of it.
- `GameHostCaps.cpp` gets `FREEINK_CAP_GAMES=1` as a source property of `GameCoreTest` only; the test does not pin `nearby` (the simulator rule), so deferred item 5 stays open for that and for the list's filter.
- Item 4's deferred entry names "entry 15, the refactor sweep" rather than a hash, since the commit that closes it is this one.
- `./bin/clang-format-fix` changed nothing outside the edits above.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time, its prompt read fresh, judged against the diff only). Diff 48 kB, blind floor 7. Verdicts: high 0, medium 0, low 6, false 4, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind | The deleted `GameCore.h`/`GameScript.h` carried each library's role and dependency rule; no code comment states it now | low | Rejected: the spine's AD-1 table (paths, allowed dependencies) is the rule's source, and a new doc file would be more than a direct fix. |
| 2 | blind | `utf8Cut`, a generic UTF-8 helper, now lives in the bindings header | low | Rejected: both callers are GameScript files and LuaGame already includes `ChBindings.h`; a new header adds a file for one function. |
| 3 | blind | `utf8Cut` reads `text[room]` | false | Only when `length > room`, so the byte is inside the string; unchanged from both old copies. |
| 4 | blind | `GameHostCapsTest` does not pin `nearby` under `SIMULATOR` | low | Rejected: pinning it needs a simulator-flag build of the provider; recorded in deferred item 5, which stays open. |
| 5 | blind | `set_source_files_properties` in a subdirectory might not reach the target | false | Source properties are visible to targets created in the same directory; `GameCoreTest` is, and `GameHostCapsTest` links and passes (622/622). |
| 6 | blind, edge | `file_at` on a path naming a directory returns `git show`'s tree listing, not `None` | low | Rejected: unchanged from both removed copies; every caller passes a file path. |
| 7 | blind | `GameScriptTest.LinksLibraries` now checks only GameIcons, so its name misleads | low | Rejected: cosmetic, and it goes with GameIcons' skeleton in epic-icon-library. |
| 8 | blind | Deferred item 4 is closed without a commit hash | low | Rejected: the closing commit is this one; the orchestrator can add its hash. |
| 9 | edge (claim) | Acceptance says `git grep libraryName -- lib/GameCore lib/GameScript test` matches nothing, yet `GameScriptTest.cpp` still calls `GameIcons::libraryName` | false | The code is right (GameIcons is out of scope); the fix would edit this plan's wording, so rejected; noted in Implementation Notes. |
| 10 | edge | `copyReason` with an empty span underflows `out.size() - 1` | false | Guarded by `if (out.empty()) return;` before the call, as before. |

Verification-gap lens: no gaps. `utf8Cut` via `copyReason` is pinned by `SessionGameTest.ARejectionReachesInputAsAnEvent` (63-byte cut before a split e-acute) and via `chLog` by the `LOG_LINE_BYTES` cases in `HostBindingsTest`; `pushSeed` by `HostBindingsTest.RandomseedWithoutArgumentsNeverCallsTime`; `file_at` by `FileAtTest`, `check_api_freeze_test.py`, and `fork_release_test.py`'s tag cases (a tag before and after `ApiLevel.h`); the FrameReplay and `abandonVm` changes touch log text only. Deletion check: `read_file` and the skeletons have no remaining callers (repo grep). Intent alignment: the readings are (a) act on the listed IN items as cleanup and (b) a wider sweep; the diff implements (a), and its tests exercise the same surfaces the intent names.

## Verification

**Commands** (each build under the shared flock, one at a time):
- `pio run -e x4pro`, `-e sticky`, `-e default`, `-e x4c`, `-e papermono` -- SUCCESS.
- `cmake -S test -B <scratch>/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build <scratch>/build && ctest --test-dir <scratch>/build --output-on-failure -j` -- all pass.
- `./bin/clang-format-fix` twice -- no diff after the second.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (default) and `-e x4pro` -- no defects.
- every `scripts/*_test.py`; `check_upstream_touches.py`; `check_api_freeze.py --base-ref origin/develop` -- pass.
- `check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- pass; deltas recorded.
- `sim.sh build x4pro`, `build sticky` -- SUCCESS (no pio run in progress).

**Verification record** (2026-09-27, working tree before the commit, on 932ef835; each build under the shared flock):
- `pio run`: x4pro SUCCESS (flash 5,809,438 B, 88.6 %, 48 B under 2.13's 5,809,486 B; RAM 101,824 B, 31.1 %), sticky SUCCESS (5,697,215 B, RAM 20.8 %), default SUCCESS (5,613,047 B), x4c SUCCESS (5,637,351 B), papermono SUCCESS (5,665,474 B).
- Host: `ctest` 622/622 passed (2.13's 621, minus `GameCoreTest.LinksLibrary`, plus `GameHostCapsTest.ReportsTheApiLevels` and `MaxSeatsIsTheListedSeatsMax`).
- `./bin/clang-format-fix` twice: exit 0, no change on either run.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`: default PASSED, `-e x4pro` PASSED.
- `scripts/*_test.py`: check_api_freeze 10, check_flash_budget 63, check_upstream_touches 18, fork_common 26 (23 plus `FileAtTest`'s 3), fork_release 71, game_codec 21, all OK.
- `check_upstream_touches.py`: PASS (trial merge of upstream/develop clean). `check_api_freeze.py --base-ref origin/develop`: passed (nothing frozen at the merge-base). Both re-run on the commit.
- `check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`: all exit 0. Games on 5,814,448 B, off 5,664,000 B: flash +150,448 B (146.9 KiB of 250). Static internal RAM on 101,824 B, off 101,816 B: +8 B (of 1,024). Game objects checked: 35 (GameCore.cpp and GameScript.cpp no longer among them); no static initializer, no mutable static.
- `sim.sh build x4pro` and `build sticky`: SUCCESS (after every pio run had ended).

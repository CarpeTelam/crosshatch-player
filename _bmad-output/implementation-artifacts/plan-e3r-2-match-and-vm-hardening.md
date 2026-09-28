---
title: 'Match and VM hardening from the icon-library retro (e3r-2)'
type: 'bugfix'
ticket: ''
created: '2026-09-28'
status: 'built'
baseline_revision: '8bd18e86609d0e41b7bf22d95f9e95319449f228'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-icon-library/epic-icon-library-retrospective.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The icon-library retro's AI-11 list leaves six small defects in the match, the VM, and the loader: a tap made after Play again, while the Over dialog is still on screen, reaches the new round as game input (R9 a); `GameVM::errorMessage()`/`failureDetail()` read VM-task state without the `failed()` gate (R4); `failedToStart` calls only NoSession a failure "before any game code ran" though LuaGame::load's out-of-memory and NotLoaded are too (R3); a file whose name is too long for the loader's buffer is skipped with no log line (R5); `GameAssets::load` may break the 256 B locals rule (R8 a); and `/.games-data` is a literal outside `GamePaths.h` (A2).

**Approach:** Drop gestures in `loopPlaying` while the Play-again gate holds; gate `errorMessage()` on `failed()`; widen `failedToStart` to every host failure that precedes game code (NoSession, OutOfMemory, NotLoaded; owner decision 2026-09-28, AD-14 amended by the orchestrator); log over-long names; measure `load` with `-fstack-usage` and split its passes if over 256 B; move the data folder and its path size into `GamePaths.h`.

## Boundaries & Constraints

**Always:** Fork-only files only. Keep `pollTimer()` and `store.flushIfDue` running during the gate, and keep reading the gesture every pass so a dropped one is consumed. Keep every existing guard in the functions touched (Design Notes). Save-store paths byte-identical. English user text through `tr()`; no new keys needed. Allocations follow AGENTS.md.

**Never:** Do not edit `lib/GameScript/DisplayList*`, `ChBindings.cpp`, `FrameReplay.cpp`, `docs/crosshatch/api-level-1.txt`, `ApiLevel.h`, or `ApiSurfaceTest` (lane e3r-1). Do not commit `-fstack-usage`. No change to what the loader accepts or returns.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Tap in the Play-again gap | Play again chosen; tap while `roundsStarted() < roundsStartedAwaited` | Gesture read and dropped; new round starts with no input | none |
| Tap after the gate opens | new round's first frame published | Tap posted as before | none |
| Host failure headline | NoSession, OutOfMemory, or NotLoaded | "The game could not start" + tr() reason | none |
| Script failure headline | Lua error | "The game stopped with an error" + Lua message | none |
| errorMessage before failure | VM running | "" | none |
| Over-long file name | name ≥ 47 B (device: getName returns 0; simulator: cut to 47 B) | skipped, one LOG_ERR naming the folder, on both paths | load result unchanged |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.cpp:281-312` -- `loopPlaying`: gesture at :297-300 is posted before the render gate at :306. `:250-253` -- `vmHealthy` comment "Only a Session that never fit…" to update.
- `src/games/GameVM.h:97-113` -- `failure()` gated (f27dcefd); `failedToStart`, `failureDetail`, `errorMessage` (ungated). `src/games/GameVM.cpp:126` -- `run()` logs `errorMessage()` on the VM task *before* `scriptFailed`/`done` are set, so it must read the ungated text.
- `lib/GameScript/VmFailure.{h,cpp}` -- `failedToStart` (:57) and its comments (h:9-24). `LuaGame.cpp:197,199` are the only OutOfMemory sites (load, before the chunk runs); :221-276 NotLoaded (no Lua state).
- `test/game_script/VmFailureTest.cpp:61-66` -- `OnlyANoSessionFailedToStart` to rewrite.
- `docs/crosshatch/game-canvas.md:94-101` -- error-view prose saying load OOM and NotLoaded show "stopped with an error".
- `src/games/GameAssets.cpp:63-228` -- `load`, two passes; `NAME_BUFFER = 48`, `fits` at :96, `length == 0` skip at :95 and :175. SdFat `getName` returns 0 when the name does not fit (`ExFatName.cpp` `goto fail`); the simulator's cuts to `len-1`.
- `src/games/GamePaths.h` -- `GAMES_DIR`, `PATH_BYTES`. `src/games/GameSaveStore.{h:53,cpp:26-28}` -- the `/.games-data` literals and `PATH_BYTES = 64`. `GameSaveStoreTest` compiles `GameSaveStore.cpp` on host with `src/games` included.
- `test/game_script/fixtures/` -- `tracer/` is the model for a new fixture; README table lists fixtures.

## Tasks & Acceptance

**Execution:**
- [x] `src/activities/games/GameMatchActivity.cpp` -- in `loopPlaying`, read `awaitingRound = vm->roundsStarted() < roundsStartedAwaited` once; always `readGesture()`, post only when not awaiting; keep `pollTimer`/`flushIfDue`; reuse the flag for the render gate; update `vmHealthy`'s comment -- R9 (a), R3.
- [x] `src/games/GameVM.{h,cpp}` -- `errorMessage()` returns "" until `failed()`; a private ungated reader for `run()`'s log; comments -- R4.
- [x] `lib/GameScript/VmFailure.{h,cpp}`, `test/game_script/VmFailureTest.cpp`, `docs/crosshatch/game-canvas.md` -- `failedToStart` true for NoSession, OutOfMemory, NotLoaded; comments (also `GameVM.h`, `vmHealthy`); test every value; doc prose; cite AD-14 as amended; never edit ARCHITECTURE-SPINE.md -- R3.
- [x] `src/games/GameAssets.cpp` -- log a non-directory whose name is empty (SdFat's getName returns 0 when it does not fit) or fills the buffer (the simulator cuts it) (before the module/image branches); then, if the measured frame is over 256 B, split the passes into `[[gnu::noinline]]` helpers keeping every guard -- R5, R8 (a).
- [x] `src/games/GamePaths.h`, `src/games/GameSaveStore.{h,cpp}` -- `GAMES_DATA_DIR = "/.games-data"` and `DATA_PATH_BYTES = 64` with a `static_assert` that the longest path fits; GameSaveStore uses them -- A2.
- [x] `test/game_script/fixtures/slow-restart/` + README row -- a tracer-like game whose later rounds' `setup` spin about 2 s on `ch.time.ms()`, so the gap can be tapped in the simulator.

**Acceptance Criteria:**
- Given the `slow-restart` fixture over in the simulator, when Play again is tapped and the canvas is tapped during the 2 s restart, then the new round shows no tap (before the fix it shows one).
- Given a fault fixture, when it fails, then the error view still shows "The game stopped with an error" with Lua's message.
- Given the host suites, when run, then `VmFailureTest` and `GameSaveStoreTest` pass.
- Given `-fstack-usage` on x4pro, when `GameAssets.cpp` is compiled before and after, then every function in it is at most 256 B, with both figures recorded.

## Implementation Notes

- **R9 (a).** `loopPlaying` reads `awaitingRound` once after the round-end check, always calls `readGesture()`, posts only when not awaiting, keeps `pollTimer()` and `store.flushIfDue`, and reuses the flag for the render gate. `roundsStartedAwaited`'s comment in `GameMatchActivity.h` now says it also drops gestures.
- **R4.** `GameVM::errorMessage()` returns "" until `failed()`; `failureText()` (private) is the ungated reader, used only by `run()`'s log on the VM task. `failureDetail()` is gated through both `failure()` and `errorMessage()`.
- **R3.** Implemented as the owner's decision (Plan Change Log, second entry): `failedToStart` is true for NoSession, OutOfMemory, and NotLoaded (a switch over every value); `VmFailureTest` replaces `OnlyANoSessionFailedToStart` with `EveryHostFailureFailedToStart` and `AScriptFailureOrNoFailureDidNotFailToStart`, which together cover all five values. The spine was not edited.
- **R5.** Pass 1 logs `"<folder> holds a file whose name is 47 bytes or longer, or unreadable; it is not loaded"` (`logUnreadName`) for a non-directory whose `getName` returned 0 (before the branches), and `"<folder>/<prefix>... has a name of 47 bytes or longer; it is not loaded"` (`logLongName`, review patch) for a name that fills the buffer (`!fits`) when no existing branch logs it. Deviation from "before the module/image branches" for the fills-the-buffer path: a 47-byte name on the device is whole and not cut, and the existing `looksLikeLua` branch counts it as `misnamed` (BadSourceName) and logs it. Skipping it before the branches would turn that BadSourceName into NoSources, which the plan forbids ("no change to what the loader accepts or returns"), and logging it there as well would give two lines where the matrix asks for one. So each such file gets exactly one line, and the load results are unchanged. Pass 2 keeps its silent skip, with a comment saying pass 1 logged it.
- **R8 (a), measured.** `-fstack-usage` on x4pro, single-object builds of `GameAssets.cpp`. These are whole frames (`static`), not only locals. Baseline (`8bd18e86`, `su-base`): `load` **368 B**, `addImage` 112, `moduleNameOf` 32, `release` 32. Over 256, so the passes were split. After (`su-after`): `load` **240 B**, `scanFolder` (pass 1, `[[gnu::noinline]]`) 176, `readFolder` (pass 2, `[[gnu::noinline]]`, emitted as a `constprop` clone) 224, `addImage` 112, `moduleNameOf` 32, `unique_ptr::reset` (`release`) 32. `logUnreadName` is inlined. Every function is at most 256 B. The helpers run one after the other, so the deepest chain is `load` + `readFolder` = 464 B, against 368 B before.
- **Guards kept in the split** (Design Notes list): `release()` first; the folder missing, unopenable, or not a folder; `fits`; pass 1's `readFailed` → CannotRead; count 0, limits, and badImage, in the same order; PSRAM OOM; pass 2's `loaded == count` and `imagesLoaded == imageCount` stops, the `same` re-check, and the lost-file check. Every pass-2 CannotRead still releases the block: `load` calls `release()` on any non-Ok from `readFolder` and returns its result. The success log reports `scan.count` and `scan.budget.count`, which equal `loaded` and `imagesLoaded` on Ok.
- **A2.** `GamePaths::GAMES_DATA_DIR` and `DATA_PATH_BYTES = 64`. `GameSaveStore.cpp` has `static_assert(len(GAMES_DATA_DIR) + 1 + MAX_ID_BYTES + sizeof("/store.bin.tmp") <= DATA_PATH_BYTES)` (12 + 1 + 32 + 15 = 60). The `snprintf` formats produce byte-identical paths, and `GameSaveStoreTest` passes with its literal `/.games-data/counter/...` expectations.
- **Fixture.** `slow-restart/`: a three-tap tracer round. A chunk-level `round` counter makes every round after the first spin in `setup` for 2,000 ms on `ch.time.ms()`, around a backtracking `string.find` so the spin stays far under the 2 M instruction budget. It also draws "Round n". The simulator log shows PlayAgain at 31342 ms and the round started at 33348 ms.
- **Verification.**
  - Host suites: 708/708 passed (707 before, plus one net new VmFailure test).
  - `pio run -e x4pro` and `pio run -e default` both succeeded.
  - Simulator (x4pro, display :83), screenshots in `e3r-2-screenshots/`:
    - 00: the same tap sequence on a temporary build with the gate removed from the post condition. The gap tap reached round 2 ("taps: 1 of 3", square at y 700). The change was restored from a saved copy, and the simulator was rebuilt with it.
    - 01: the `lua_error` fault still shows "The game stopped with an error" with `main.lua:2: boom`.
    - 02 and 03: the round-1 Over dialog, still on screen 0.3 s after Play again.
    - 04: after the fix, round 2 starts at "taps: 0 of 3" with no square.
    - 05: a tap after the gate opens is posted ("taps: 1 of 3").
  - `check_layers.py` passes.
  - `check_upstream_touches.py` passes, but it checks HEAD, which holds none of this uncommitted change. Every changed path was checked with `git cat-file -e upstream/develop:<path>` and is fork-only.
  - `clang-format-fix` ran with clang-format 21.1.8 from the uv cache, on PATH through a scratch symlink, since the system copy is 18. It changed nothing.
  - Not run: `sticky`, `x4c`, `papermono`, and `pio check`.

## Plan Change Log

- 2026-09-28, orchestrator message during implementation (not a review finding): R3 changed from "widen `failedToStart`" to "keep NoSession only and correct the text", because the spine's AD-14 amendment decides it; the Intent's Approach, the matrix, the R3 task, and Design Notes were amended (the frozen block with the orchestrator's authority). Known-bad state avoided: code that contradicts a spine decision. R5 now names both skip paths (getName 0 on the device, a cut name in the simulator). KEEP: everything else in the plan.
- 2026-09-28, owner decision relayed by the orchestrator (reverses the entry above): widen `failedToStart` to NoSession, OutOfMemory, and NotLoaded, all "The game could not start"; code, VmFailureTest, comments, and `game-canvas.md` match; the spine's AD-14 is amended by the orchestrator, not here. Approach, matrix, R3 task, and Design Notes amended back. KEEP: the R5 two-path logging and everything else.

## Review Triage Log

Pass 1 (iteration 0). The four lenses ran as context-free subagents over `git diff 8bd18e86..` (working tree, `_bmad-output` excluded), and all four returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. Counts: high 0, medium 0, low 11, false 2, maybe-false 0 (intent-alignment is descriptive; its divergences are folded into the rows they match).

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| 1 | blind | `game-canvas.md` says only the script's error shows "stopped with an error", but a stuck call does too | low | patch | `stopStuckVm` always fails with `STR_GAMES_ERROR`. Reworded to name both. |
| 2 | blind | The Play-again row in `game-canvas.md` does not state that gap gestures are dropped, nor the refresh residual | low | patch | Row now says gestures before the new round's first frame is published are read and dropped, and that a tap during the refresh still reaches the round. |
| 3 | edge, intent, blind | The gate opens when the frame is published, not shown: a tap during the e-ink refresh, or a contact that began in the gap and lifts after it, still reaches the new round | low | defer | Real. It is the plan's recorded residual, and closing it needs a new "frame displayed" signal from the render task (new state). The orchestrator's task text is "drop gestures while the gate holds". Deferred (`## e3r-2`). |
| 4 | blind | The same gap exists before the first round's first frame (the Games list is still on screen) | low | defer | Real and pre-existing: `roundsStartedAwaited` is 0 until Play again. Deferred. |
| 5 | blind, intent | Splitting `load` deepens the peak chain from 368 B to 464 B; comment hid it | low | patch (comment) | The per-function rule (AGENTS.md) is met; +96 B on the loop task is negligible. The comment on `scanFolder` now states the chain. The restructure is rejected. |
| 6 | blind | The split loader ran only on slow-restart and lua_error; the image, bad-image, and long-name paths were not re-run | low | patch (verification) | Re-run after the review patches: `images` draws both images and clips (06), `bad-image` shows the load-failure view (07), and a 58-byte `.bmp` name logs `…/a_file_name_far_too_long_for_the_loader_buffer_... has a name of 47 bytes or longer; it is not loaded`. |
| 7 | blind, verif, intent | The gesture gate has no automated regression test | low | defer | `GameMatchActivity` has no host harness. A pure `started >= awaited` helper would pin a comparison but not the call site that R9 (a) is about. Verified in the simulator (00 vs 04/05). Deferred to retro AI-2's harness. |
| 8 | verif, blind | No host test compiles `GameAssets.cpp` after the split (result contract, `release()` on pass-2 failure, the new log branches) | low | defer | Needs a directory-iterating fake storage and a PSRAM stub, which is new test infrastructure. Simulator runs cover Ok, BadImage, and the long-name log. Deferred. |
| 9 | blind | `slow-restart/main.lua` comment says "Taps: 0"; the game draws "Round 2, taps: 0 of 3" | low | patch | Comment matches the drawn text. |
| 10 | blind | The `!fits` log drops the name prefix it has | low | patch | New `logLongName(path, name)` logs `folder/prefix...`. The `length == 0` path keeps the folder-only line. |
| 11 | edge | On the device an over-long `.lua` name (getName 0) makes an only-misnamed folder `NoSources`, not `BadSourceName` | low | reject | Pre-existing (the base skipped it the same way). It needs a name of 48 B or more, and a fix adds a counter and a branch. It is now logged. |
| 12 | edge | In the simulator a cut name that still ends in `.lua`/`.bmp` is counted as misnamed | false | reject | A cut name ends in `.lua` only if bytes 44-47 of the real name are `.lua`. On the device a 47-byte name is whole. Otherwise it is logged once, either way. |
| 13 | blind | `sticky`, `x4c`, `papermono`, and `pio check` were not run | false | reject | The brief asks build agents for `x4pro` and `default`; the orchestrator builds all five and runs `pio check` before the PR. |
| 14 | blind | `readFolder`'s `block` parameter shadows `GameAssets::block`; `read` sits beside `file.read` | false | reject | `readFolder` is a free function in an anonymous namespace, so nothing is shadowed. No named harm. |
| 15 | blind | The `static_assert` lives in `GameSaveStore.cpp`, not beside `DATA_PATH_BYTES`; `<string>` where `<string_view>` would do | low | reject | `GamePaths.h` does not include `Manifest.h` (the id size). The one user checks it, and the header's comment names where. The include is cosmetic. |
| 16 | blind | A2's other parts (clip helper, `ConverterBmpLayout.h`) are not re-deferred; the retro addendum is not updated | low | reject | Outside this run's scope (item 6 is `/.games-data` only). The retro's AI-11 still lists them. The plan and screenshots are committed with this change. |
| 17 | intent | R3 is tested only as the pure function; the new "could not start" headline for a load OOM or NotLoaded is not seen on screen | low | defer | `VmFailureTest` covers every value. Staging a `LuaGame::load` OOM needs an arena fault hook. Folded into `## 3.10`'s call-site gap (deferred). |

## Design Notes

Guards kept (from `git log -L`): `loopPlaying` -- Back first (pause, acf780ea); `vmHealthy` before any `vm->` use (a stuck VM is reset inside it); round-end check before input (Over, AD-21); gesture read every pass (it consumes the contact; 13bcd449); render gate (8e233695, R3). `failure()`'s `!failed()` early return (f27dcefd: `sessionOutOfMemory` and `hostFailure()` are VM-task writes published by `done`). `load` -- `release()` first; folder missing/unopenable/not a folder; `fits` (a cut name is never classified); pass-1 `readFailed` → CannotRead (f27dcefd); count 0 / limits / badImage; PSRAM OOM; pass 2's `loaded == count` and `imagesLoaded == imageCount` stops, `same` re-check, and the lost-file check (890c1a69). `GameSaveStore` ctor: `snprintf` truncation-safe paths (5f8582c9).

R3 (owner decision 2026-09-28): widen `failedToStart` to every host failure that precedes game code. The orchestrator amends the spine's AD-14 line on the main branch; this change does not edit the spine.

R9 (a) residual: the gate opens when the new round's first frame is *published*; the e-ink refresh that shows it follows. A tap in that refresh still reaches the new round (deferred).

## Verification

Shared machine: wrap every `pio`, `sim.sh setup`/`build`, and host-test CMake command in `flock /tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/build.lock sh -c '<commands>'` (the lock covers the whole chain); `pio` is `/root/.local/bin/pio`; scratch files go under `/tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/e3r-2/`. Do not reinstall packages or touch certificates. Do not commit.

**Commands:**
- `flock {lock} sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- all pass.
- `flock {lock} sh -c 'pio run -e x4pro && pio run -e default'` -- success.
- `-fstack-usage` single-object x4pro builds of `GameAssets.cpp`, never committed: the baseline one is already queued and writes `/tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/e3r-2/su-base/x4pro/src/games/GameAssets.cpp.su` (log `/tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/e3r-2/su-base.log`); the change's: `flock {lock} sh -c 'cd /home/user/wt-match && PLATFORMIO_BUILD_DIR=/tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/e3r-2/su-after PLATFORMIO_BUILD_FLAGS=-fstack-usage pio run -e x4pro -t /tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/e3r-2/su-after/x4pro/src/games/GameAssets.cpp.o'` -- every function's figure recorded in Implementation Notes; split `load` only if the baseline's `load` is over 256 B.
- `flock {lock} sh -c '.claude/skills/run-crosshatch-player/sim.sh build x4pro'`, then the Play-again gap tap and a fault's error view -- screenshots in `_bmad-output/implementation-artifacts/e3r-2-screenshots/`.
- `python3 scripts/check_upstream_touches.py`; `./bin/clang-format-fix` twice, `git status` clean after.

**Evidence (after the review patches, under the lock):**
- Host suites: `100% tests passed, 0 tests failed out of 708`.
- `pio run -e x4pro`: SUCCESS (RAM 101,824 B, Flash 5,898,218 B); `pio run -e default`: SUCCESS (RAM 57,912 B, Flash 5,624,161 B).
- `-fstack-usage` (x4pro, single-object, scratch build dir, flag not committed): baseline 8bd18e86 `load` 368 B; final tree `load` 240, `scanFolder` 176, `readFolder` (constprop) 224, `addImage` 112, `moduleNameOf` 32, `release` 32.
- `sim.sh build x4pro`: SUCCESS. Screenshots in `_bmad-output/implementation-artifacts/e3r-2-screenshots/`:
  - `00-before-fix-round2-gap-tap-reached.png`: gate removed from the post condition; the gap tap reached round 2 ("taps: 1 of 3", square).
  - `01-fault-lua-error.png`: a script error still shows "The game stopped with an error" with `main.lua:2: boom`.
  - `02-slow-restart-round1-over.png`, `03-after-play-again-gap.png`: the end-of-round menu, still on screen after Play again.
  - `04-after-fix-round2-no-tap.png`: with the fix, the gap tap is dropped; round 2 starts at "taps: 0 of 3".
  - `05-after-fix-tap-after-gate-posted.png`: a tap after the gate opens is posted.
  - `06-images-fixture-after-loader-split.png`: the split loader loads both images, with a 58-byte `.bmp` name beside them logged and skipped.
  - `07-bad-image-load-failure-view.png`: `bad-image` still shows "The game could not start" / "An image is damaged or too large".
- The R9 (a) decision is not a pure function: it is one counter comparison whose risk sits at the call site in `loopPlaying`, so it is verified in the simulator, not host-tested.

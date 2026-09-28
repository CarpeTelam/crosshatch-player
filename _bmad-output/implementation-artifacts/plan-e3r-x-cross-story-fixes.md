---
title: 'Cross-story review fixes for e3r-1..3 (e3r-x)'
type: 'bugfix'
ticket: ''
created: '2026-09-28'
status: 'built'
baseline_revision: '4d1367822e46c0a05303f0e4fd792db3a830fc04'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-icon-library/cross-story-review.md'
  - '{project-root}/_bmad-output/implementation-artifacts/plan-e3r-2-match-and-vm-hardening.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The cross-story review of e3r-1..3 (verified against 11402d1e) accepted seven findings: (1) after Play again, Back then Resume in the setup gap redraws the last round's board, on which every tap is dropped; (2) e3r-2's `GameAssets::load` split deepened the stack chain (load 240 + readFolder 224 + addImage 112 B) past the pre-split 368 + 112 = 480 B; (3) no host test loads `slow-restart`; (4) `DisplayList.h` and `api-level-1.txt` say the blit budget bounds a frame's replay work, but filled shapes are not charged; (5) the `Icons up to date` job gives re-pin advice for any generator failure; (6) a `core.autocrlf=true` checkout can turn every pinned SVG into a hash mismatch; (7) `sim_sh_test.py` skips all its tests when a tool is missing, which passes the `^Ran [1-9]` check.

**Approach:** One minimal fix per finding, as the orchestrator listed them: (1) `renderCanvas` keeps what is on screen while a round awaited after Play again has not started; (2) one heap scratch struct for the per-load buffers; (3) a manifest test over every fixture plus a slow-restart first-frame test; (4) reword to "icon and image replay only"; (5) generator exit 3 for a pin mismatch, re-pin advice only for 3; (6) `assets/game-icons/** -text`; (7) a missing tool fails the tests.

## Boundaries & Constraints

**Always:** Fork-only files (plus `.gitattributes`, the fork's own). Keep every guard of each changed function (Design Notes). Allocate with `makeUniqueNoThrow`, null → the existing `LoadResult::OutOfMemory`. Every function in `GameAssets.cpp` at most 256 B and the deepest load chain under 480 B, measured with `-fstack-usage` on x4pro exactly as plan-e3r-2 did (flag never committed). Use `fork_common.exit_code`; document code 3 in the generator's docstring, `fork-scripts.md`, and `game-icons.md`.

**Never:** No change to what the loader accepts or returns, to the level-1 entry lines (the reworded text is a `#` comment outside the CRC), to `ci.yml`, to `fork_common.py`, or to `freeink-sdk`/`.skills`. No `tickets.py mark`/`pull`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Resume in the gap | Play again, Back, Resume while `roundsStarted() < roundsStartedAwaited` | pause menu stays on screen; the last round's board is not drawn | none |
| Round starts after Resume | new round's first frame published | canvas drawn on a cleared screen, full refresh | none |
| Overlay closes in the gap | repaint render while awaiting | canvas not drawn; next frame clears the screen | none |
| Scratch OOM | `new (std::nothrow)` fails | `LoadResult::OutOfMemory`, one LOG_ERR | existing result |
| Edited or unlisted SVG | digest differs, SVG unlisted, or listed path unnamed | generator exit 3; CI: re-pin advice | exit 3 |
| Other generator failure | malformed map/sums, bad SVG | exit 1 or 2; CI: generic annotation with the code | exit 1/2 |
| Tool missing | no bash, awk, or git | every `SimShTest` case fails naming the tool | test failure |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.cpp:148-187` `handle()` -- Resume/Back into Playing requests a render (:172, :186); comment at :171 to extend. `:405-423` `renderCanvas()` -- add the await check after `if (!vm) return;`. `:296-319` `loopPlaying` gates its own render request on the same comparison; unchanged.
- `src/activities/games/GameMatchActivity.h:126-137` -- `roundsStartedAwaited` (plain `uint32_t`, loop task) becomes `std::atomic<uint32_t>` since render now reads it; `viewOnScreen` comment ("a view was drawn over the canvas") widened to "the screen does not hold the canvas". `GameVM::roundsStarted()` is an acquire load (`SoloRounds.h:39`), any task.
- `src/games/GameAssets.cpp` -- `addImage` :50 (62-byte `header` local), `scanFolder` :89 and `readFolder` :163 (`name[48]`, `Stem`, `ImageHeader` locals), `load` :225 (`path[GamePaths::PATH_BYTES = 96]`); stale comment :83-88. `lib/Memory/Memory.h` `makeUniqueNoThrow`; `src/games/MatchStore.cpp` includes `<Memory.h>`. Baseline `.su` (4d136782 = e3r-2 final): load 240, readFolder (constprop) 224, scanFolder 176, addImage 112.
- `test/game_core/ManifestTest.cpp`, `test/game_core/CMakeLists.txt` -- `GameCoreTest` builds the real `Manifest.cpp`; add a `GAME_SCRIPT_FIXTURES_DIR` definition (`test/game_script/CMakeLists.txt:125` has the same one). Fixtures with a `manifest.json`: every folder but `faults`, `modules`, `surface`; each id equals its folder name.
- `test/game_script/SessionGameTest.cpp:49-54` -- tracer first-frame pattern (`useSource`, `SessionGame`, `frontText`) to copy for slow-restart ("Round 1, taps: 0 of 3").
- `lib/GameScript/DisplayList.h:13-16`, `docs/crosshatch/api-level-1.txt:334-343` (comment), `test/game_script/GfxBindingsTest.cpp:800` ("The budget bounds the replay").
- `scripts/gen_game_icons.py` -- docstring exit list :40-47; `read_sums` :539 ("not an SVG names.txt names"), `check_sum` :565 (unlisted; digest differs); `main` :614. `scripts/gen_game_icons_test.py:380-420, 452` -- codes to update. `.github/workflows/crosshatch-ci.yml:29-34` (header comment) and `:279-292` (step). `docs/crosshatch/fork-scripts.md:17-19` (exit contract), `docs/crosshatch/game-icons.md:29-35`.
- `.gitattributes` (2 lines, fork's own); `docs/crosshatch/upstream-touches.md:88` describes its rules.
- `scripts/sim_sh_test.py:1-12` (docstring), `:42` (`skipUnless`); `crosshatch-ci.yml:224` is the `^Ran [1-9]` check.
- `docs/crosshatch/game-canvas.md:43` -- Paused → Playing row. `_bmad-output/implementation-artifacts/deferred-work.md:176` -- the `## 3.7` R3 residual to mark resolved.

## Tasks & Acceptance

**Execution:**
- [x] `src/activities/games/GameMatchActivity.{h,cpp}`, `docs/crosshatch/game-canvas.md` -- atomic `roundsStartedAwaited`; in `renderCanvas`, when `vm->roundsStarted() < roundsStartedAwaited`, set `viewOnScreen = true` and return (the screen keeps its view, and the round's first frame is drawn on a cleared screen); comments in `handle` and the header; doc row 43 -- item 1.
- [x] `src/games/GameAssets.cpp` -- `struct LoadScratch { char path[PATH_BYTES]; char name[NAME_BUFFER]; Stem stem; uint8_t headerBytes[IMAGE_HEADER_BYTES]; GameCore::ImageHeader header; }` allocated after `release()` with `makeUniqueNoThrow`; null → LOG_ERR + `OutOfMemory`; `scanFolder`/`readFolder`/`addImage` take it by reference and drop those locals; rewrite the :83-88 comment -- item 2.
- [x] `test/game_core/{ManifestTest.cpp,CMakeLists.txt}`, `test/game_script/SessionGameTest.cpp` -- every fixture `manifest.json` parses (`ManifestError::None`, id equals folder, slow-restart among them); slow-restart reaches its first frame -- item 3.
- [x] `lib/GameScript/DisplayList.h`, `docs/crosshatch/api-level-1.txt` (comment only), `test/game_script/GfxBindingsTest.cpp` comment -- "bounds icon and image replay; filled rects and circles are not charged" -- item 4.
- [x] `scripts/gen_game_icons{,_test}.py`, `crosshatch-ci.yml`, `fork-scripts.md`, `game-icons.md` -- `PIN_MISMATCH = 3` via a `PinMismatch(Failure)` raised for a differing digest, an unlisted SVG, and a listed unnamed path, caught in `main`'s step and returned as 3; tests assert 3 for those and 1 for the rest; CI gives re-pin advice for 3, "failed with exit N (its message is above)" otherwise -- item 5.
- [x] `.gitattributes`, `upstream-touches.md:88` -- `assets/game-icons/** -text` -- item 6.
- [x] `scripts/sim_sh_test.py` -- drop `skipUnless`; `setUp` fails naming each missing tool; docstring -- item 7.
- [x] `deferred-work.md` -- `## 3.7` R3 residual prefixed "Resolved by e3r-x (...)"; `## e3r-x` for anything deferred.

**Acceptance Criteria:**
- Given slow-restart over in the simulator, when Play again, Back, Resume are pressed in the 2 s gap, then the screen keeps the pause menu (not the round-1 board) until round 2's "taps: 0 of 3" appears.
- Given `-fstack-usage` on x4pro, when `GameAssets.cpp` is compiled before and after, then every function is ≤ 256 B and load + readFolder + addImage (and load + scanFolder + addImage) < 480 B.
- Given the host suites and every `scripts/*_test.py`, when run, then all pass with n > 0 tests each.
- Given a fresh tree of the commit, when the Icons up to date step and the Fork script tests loop run, then both pass, and `git add --renormalize .` changes nothing.

## Implementation Notes

The implementation subagent built items 1-7 and the deferred-work entry, and ran the host suites, every `scripts/*_test.py`, the "after" `-fstack-usage` build, and `pio run -e x4pro` / `-e default`, all under the build lock. Per the build agent's addendum it left the simulator run and screenshots, the fresh-clone gates (Icons up to date step, Fork script tests loop, doctored SVG, `git add --renormalize .`), `check_upstream_touches.py`, `clang-format-fix`, and the commit to the build agent.

- **Item 1.** `roundsStartedAwaited` is `std::atomic<uint32_t>` (review fix: `handle()` stores it for PlayAgain before `shown.store(to)`, so a render already queued never sees Playing with the old count; `shownFrame` and `vm->playAgain()` stay in the switch; `loopPlaying` and `renderCanvas` load it). `renderCanvas` checks `vm->roundsStarted() < roundsStartedAwaited.load()` right after `if (!vm) return;`, sets `viewOnScreen = true`, and returns before the view-covered clear, the repaint `forceFull`, and the `renderedFrame` store, so a skipped render changes no replay state. Comments: `handle()`'s Resume/Back line, the header's `roundsStartedAwaited` and `viewOnScreen` ("the screen does not hold the canvas"). `game-canvas.md`'s Paused → Playing row names the gap. The pause menu opened in the gap still draws the last round's frame beneath it (Design Notes).
- **Item 2.** `struct LoadScratch { path[PATH_BYTES]; name[NAME_BUFFER]; Stem stem; headerBytes[IMAGE_HEADER_BYTES]; ImageHeader header; }` (in the anonymous namespace), allocated in `load` with `makeUniqueNoThrow<LoadScratch>()` right after `release()`; null logs `OOM: <n> bytes to load <id>` and returns `LoadResult::OutOfMemory`. `scanFolder(dir, scratch, scan)`, `readFolder(dir, scratch, scan, layout, read)`, and `addImage(file, budget, scratch, readFailed)` take it by reference; `path`/`name` inside the passes are `const` aliases into it. Every guard of e3r-2's Design Notes list is unchanged (same statements, same order); the pass-1 comment (old :83-88) now says the buffers live in the scratch, on the heap. `<Memory.h>` included.
  - `-fstack-usage` (x4pro, single-object, `PLATFORMIO_BUILD_CACHE_DIR` pointed at an empty scratch dir, since the shared `.cache` otherwise returns a cached `.o` and writes no `.su`; flag not committed): **before** (`e3r-x/su-before`, HEAD 4d136782) `load` 240, `readFolder` (constprop) 224, `scanFolder` 176, `addImage` 112, `moduleNameOf` 32, `release` 32; chains load+readFolder+addImage 576 B, load+scanFolder+addImage 528 B. **After** (`e3r-x/su-after`): `load` 144, `readFolder` (constprop) 128, `scanFolder` 96, `addImage` 32, `moduleNameOf` 32, `release` 32; chains **304 B** and **272 B**, under 480 B. `sizeof(LoadScratch)` is on the heap for the load's duration only.
- **Item 3.** `ManifestTest.EveryFixtureManifestIsListed` iterates `GAME_SCRIPT_FIXTURES_DIR` (new definition in `test/game_core/CMakeLists.txt`, `${REPO_ROOT}/test/game_script/fixtures`) and applies `GamesListActivity::readManifest`'s rules to each `manifest.json`: parses (`ADD_FAILURE` + continue otherwise), id == folder, `check(gameHostCaps()).ok()`, solo among `check.modes`; the folders without one are exactly {faults, modules, surface}; `slow-restart` and `tracer` are among the listed. `SessionGameTest.TheSlowRestartReachesItsFirstFrame` starts the fixture and finds "Round 1, taps: 0 of 3".
- **Item 4.** `DisplayList.h`, the `api-level-1.txt` `#` comment above `limit frame_icon_image_pixels` (entry line unchanged; `ApiLevelTest.SurfaceCrcMatchesTheLists` passes), and `GfxBindingsTest.cpp`'s comment now say the budget bounds icon and image replay only; filled rects and circles are not charged.
- **Item 5.** `class PinMismatch(Failure)` and `PIN_MISMATCH = 3` in `gen_game_icons.py`; raised by `check_sum` (unlisted SVG; digest differs) and `read_sums` (a listed path names.txt does not name); a malformed or repeated line, or a non-UTF-8 SHA256SUMS, stays `Failure` (1). `main`'s generate step catches `PinMismatch`, prints `error: <message>`, and returns 3 through `fork_common.exit_code` (`fork_common.py` untouched); `--write-sums` cannot raise it. Docstring, `fork-scripts.md` (exit contract: a script may add a code above 2), and `game-icons.md` document 3. Tests: the edited-SVG case and the round trip assert 3; the SHA256SUMS drift table asserts 3 for unlisted/unnamed and 1 for the other seven. `crosshatch-ci.yml`: step captures the code; 3 gets the restore/re-pin annotation, anything else `failed with exit N (its message is above)`; header comment updated.
- **Item 6.** `.gitattributes` gains `assets/game-icons/** -text`; `upstream-touches.md`'s `.gitattributes` row names the rule and why. `git check-attr` shows `text: unset` for names.txt and the SVGs. Note: `assets/game-icons/phosphor/LICENSE` is committed with CRLF (i/crlf); `-text` keeps it byte-as-committed.
- **Item 7.** `sim_sh_test.py` drops `skipUnless`; `setUp` fails with `not on PATH: <tools> (these tests need bash, awk, and git)`; docstring says why. With `PATH=/nonexistent`, all 4 cases fail with that message.
- **Review fixes (orchestrator's review).** Also: `game-canvas.md`'s row says the pause menu is inert in the gap and an overlay closed there stays on screen too; `gen_game_icons.py`'s `PinMismatch` raises re-wrapped (aligned, at most 120 columns); the CI exit-3 annotation points at the message for the file and covers added, removed, or updated icons; the `.gitattributes` row in `upstream-touches.md` names the bytes it protects; the `frame_icon_image_pixels` comment in `api-level-1.txt` re-wrapped at 80 columns (same words; `ApiLevelTest` passes); the `slow-restart` README row and `main.lua` header name the Back/Resume-in-the-gap check. Re-run: `ManifestTest.*`, `ApiLevelTest.*`, the SessionGameTest slow-restart/tracer cases, `gen_game_icons_test.py` (OK), and a single-object x4pro compile of `GameMatchActivity.cpp` (SUCCESS).
- **deferred-work.md.** `## 3.7`'s R3 residual is prefixed "Resolved by e3r-x (...)" (the commit hash cannot name itself; the orchestrator may add it on merge). The review's two deferrals are under `## e3r-x`.
- **Verification run here (under the lock).** Host suites: `100% tests passed, 0 tests failed out of 719` (both new tests pass). `scripts/*_test.py`: 9 files, each OK with n > 0 (gen_game_icons 31, sim_sh 4). `gen_game_icons.py --out` to scratch: `cmp`-identical to the committed header. `pio run -e x4pro`: SUCCESS (RAM 101,824 B, Flash 5,898,850 B). `pio run -e default`: SUCCESS (RAM 57,912 B, Flash 5,624,161 B; games are off on the C3, so no change there). `check_upstream_touches.py`, `clang-format-fix`, the simulator, and the fresh-clone gates were not run here (build agent's).

## Plan Change Log

## Review Triage Log

Pass 1 (iteration 0). The four lenses ran as context-free subagents over `git diff 4d136782..` (working tree, this plan excluded), and all four returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. Counts: high 0, medium 0, low 14, false 3, maybe-false 0 (intent-alignment is descriptive; its divergences are folded into the rows they match). Patches went back to the implementation subagent in one message.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| 1 | blind, edge | `handle()` stored `shown` (Playing) before PlayAgain's `roundsStartedAwaited`, so a render already queued could run `renderCanvas` in between with the old count and draw the last round's board | low | patch | Real (a render requested by Over-menu navigation runs on the render task). PlayAgain now stores the count before `shown`; `playAgain()` still follows, so the value is the same. |
| 2 | edge | Back renders the pause view in the instant between the new first frame's publish and the `started` increment, storing `renderedFrame` = the new frame; a Resume in that same instant skips, and the loop then never re-requests | low | reject | Both renders must land between two adjacent VM-task statements (`SoloRounds.cpp`: publish, then `started.fetch_add`); a Resume after the count moves is a repaint and draws. The fix adds state. |
| 3 | blind, edge, verif, intent | After Resume in the gap the pause menu stays but is inert (routing closed, gestures dropped; Back pauses again) | low | patch (doc) + defer | Real; it is the orchestrator's chosen behaviour ("keep the current view until the new round publishes"). `game-canvas.md` now says the menu is inert. A "starting" view or staying in Paused is an owner UX choice: `## e3r-x`. |
| 4 | blind | An overlay closed in the gap stays on screen until the round's first frame, unrecorded | low | patch (doc) | Now in `game-canvas.md`'s row; the code comment already said so. Same deferral as row 3. |
| 5 | blind, verif | The gap skip has no automated test | low | defer | `test/` builds no `GameMatchActivity.cpp`; simulator screenshots 00-06. `## e3r-x`, with retro AI-2's harness. |
| 6 | verif | The rewritten image path of `GameAssets::load` never ran (slow-restart has no images) | low | patch (verification) | Simulator: `images` loads 2 images (1,380 B) and draws them (07); `bad-image` shows "An image is damaged or too large" (08). A host `GameAssetsTest` stays deferred (`## e3r-2`). |
| 7 | verif, blind | `EveryFixtureManifestParses` claims launcher listing but checks only `parse`; compares `m.id` after a failed parse; skips manifest-less folders silently | low | patch | Renamed `EveryFixtureManifestIsListed`: `check(gameHostCaps()).ok()`, solo mode, `ADD_FAILURE` + continue on a parse error, and the manifest-less folders must be exactly {faults, modules, surface}. |
| 8 | blind | `main()`'s nested `step()` is a second exit-code handler, which `fork-scripts.md` forbids | false | reject | It returns an int through `fork_common.exit_code`, the form `fork-scripts.md` allows ("return `FAIL` after printing every problem"); `fork_common.py` stays the one handler. |
| 9 | blind | `fork_common.py`'s docstring and the doc's API table still list only 0-2 | low | reject | `exit_code` already passes an int through, as both say; the exit-contract bullet documents codes above 2. No reader is misled into a wrong code. |
| 10 | blind | Re-wrapped `raise PinMismatch(...)` calls misaligned, two lines over 120 columns | low | patch | Re-wrapped, none over 120. |
| 11 | blind, intent | CI's exit-3 advice ("restore the SVG") does not fit an unnamed listed path or a new unpinned icon | low | patch | Annotation now points at the script's message for the file and covers an icon added, removed, or updated. A repeated line stays exit 1 (malformed file, not a pin disagreement). |
| 12 | edge | Finding 6's premise overclaims: `git ls-files --eol` shows the 110 SVGs as `i/none` (no line endings) | low | patch (doc) | True. The rule still stops conversion of `names.txt`, `SHA256SUMS`, `LICENSE` (i/crlf), and any future SVG with line breaks; the ledger row now says exactly that. |
| 13 | blind | Budget docs do not say what bounds filled shapes; one `api-level-1.txt` comment line far over the block's wrap | low | patch (wrap) / reject | Re-wrapped at 80 (same words, CRC test passes). The filled-shape bound is already deferred (`## e3r-1`, third entry). |
| 14 | blind, intent | Scratch allocated before `Storage.exists`: under OOM a missing folder reports OutOfMemory; scratch lives through `restoreInto` | false | reject | Needs a failed ~250 B internal allocation, after which the load cannot proceed anyway; "OutOfMemory" is then true. Freeing earlier saves nothing measurable. |
| 15 | blind | `deferred-work.md`'s resolved entry keeps its old evidence line; editing a `merge=union` line risks both versions surviving | low | reject | Same form as `## 3.7`'s earlier "Resolved by entry 10" entry. The union risk is the orchestrator's known merge step (brief step 4: read the result). |
| 16 | blind | The slow-restart README row and `main.lua` header name only the gap-tap check | low | patch | Both now describe the Back/Resume-in-the-gap check. |
| 17 | intent | Finding 2's measurement and finding 5's CI branch are not in the diff | false | reject | Recorded in Implementation Notes (`.su` before/after) and Verification (fresh-tree gate run, doctored SVG). |

## Design Notes

Guards kept (`git log -L`): `handle()` -- `lifecycle.apply` refusal first; `closeRouting` before any view; PlayAgain's `shownFrame`/`roundsStartedAwaited` set before `vm->playAgain()` (b64f455d, 8e233695); Leaving returns before `requestUpdate`. `renderCanvas()` -- `!vm` (freed by stop/abandon; 97dcf52f), the view-covered clear and `forceFull` (acf780ea), the repaint `forceFull` (8e233695), `drawFront` false → no refresh. The await check goes after `!vm` (it reads `vm`) and before the others, so a skipped render changes no replay state. `load` and its passes keep every guard e3r-2's Design Notes list (`release()` first; folder missing/unopenable/not a folder; `fits`; pass-1 `readFailed` → CannotRead; count 0, limits, badImage in order; PSRAM OOM; pass-2 stops, `same`, lost-file check; `release()` on a pass-2 failure). The scratch allocation goes after `release()` so a failed allocation leaves no block.

Setting `viewOnScreen` on a skipped render: the overlay's pixels, or the pause menu, are on screen, and the round's first frame is not a repaint (its `frameGen` differs from `renderedFrame`), so without it replay would draw incrementally over them.

The pause menu opened in the gap still draws the last round's frame beneath it, as the end-of-round menu does; only the bare canvas, where taps look live, is withheld.

## Verification

Wrap every `pio`, `sim.sh setup`/`build`, and host-test CMake in `flock /tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/build.lock sh -c '...'`; scratch under `.../scratchpad/e3r-x/`.

**Commands:**
- Host suites (AGENTS.md cmake/ctest) -- all pass. `for t in scripts/*_test.py; do python3 $t; done` -- each OK, n > 0.
- `-fstack-usage`: `PLATFORMIO_BUILD_DIR=<scratch>/e3r-x/su-{before,after} PLATFORMIO_BUILD_FLAGS=-fstack-usage pio run -e x4pro -t <dir>/x4pro/src/games/GameAssets.cpp.o` -- figures recorded.
- `pio run -e x4pro && pio run -e default` -- success. `sim.sh build x4pro`, slow-restart Play again → Back → Resume in the gap; screenshots to `_bmad-output/implementation-artifacts/e3r-x-screenshots/`.
- Fresh clone of the commit at `<scratch>/e3r-x/fresh`: the Icons up to date step's commands and the Fork script tests loop -- pass; plus a doctored SVG → exit 3 and the re-pin annotation. `git add --renormalize .` → `git status` clean.
- `python3 scripts/check_upstream_touches.py`; `./bin/clang-format-fix` twice, nothing new.

**Evidence (after the review patches, under the lock):**
- Host suites: `100% tests passed, 0 tests failed out of 719` (`ManifestTest.EveryFixtureManifestIsListed` and `SessionGameTest.TheSlowRestartReachesItsFirstFrame` new).
- `scripts/*_test.py`, each in its own process: 9 files OK, each `Ran n` with n > 0 (check_api_freeze 9, check_flash_budget 78, check_layers 51, check_upstream_touches 18, fork_common 27, fork_release 76, game_codec 22, gen_game_icons 31, sim_sh 4).
- `pio run -e x4pro`: SUCCESS; `pio run -e default`: SUCCESS; `sim.sh build x4pro`: SUCCESS.
- `-fstack-usage` (x4pro, single object, scratch build dir, flag not committed; `GameAssets.cpp` unchanged by the review patches): before (4d136782) `load` 240, `readFolder` 224, `scanFolder` 176, `addImage` 112, so load+readFolder+addImage 576 B; after `load` 144, `readFolder` 128, `scanFolder` 96, `addImage` 32, so 304 B (load+scanFolder+addImage 272 B), under e3r-2's pre-split 368 + 112 = 480 B.
- Simulator (x4pro, display :91), `_bmad-output/implementation-artifacts/e3r-x-screenshots/`:
  - `00-before-fix-resume-in-gap-shows-last-board.png`: a temporary build with the skip disabled (`if (false && ...)`, restored from a saved copy, then rebuilt): Resume in the gap shows round 1's board.
  - `01-before-fix-gap-tap-dropped-on-last-board.png`: same build, a tap on that board in the gap does nothing.
  - `02-round1-over.png`: slow-restart's round 1 over, end-of-round menu.
  - `03-play-again-then-back-paused-in-gap.png`: Play again, then Back within the gap: the pause menu.
  - `04-after-fix-resume-in-gap-keeps-pause-menu.png`: final build, Resume in the gap (log: Resume at 45459 ms, round started at 46719 ms): the pause menu stays.
  - `05-after-fix-gap-tap-dropped-menu-stays.png`: a tap in the gap is dropped; the menu stays.
  - `06-after-fix-round2-on-cleared-screen-no-tap.png`: round 2 drawn on a cleared screen, "taps: 0 of 3", no square.
  - `07-images-fixture-loads-through-load-scratch.png`: `images` loads through the scratch-based loader ("Loaded 1 Lua files (1549 bytes) and 2 images (1380 bytes)") and draws both images and the clip.
  - `08-bad-image-load-failure-view.png`: `bad-image` still fails with "An image is damaged or too large" (log: `broken.bmp is not a usable image: not 1 bit per pixel`).
- Fresh tree (items 5-7 change CI gates): a `git clone` of this worktree with commit 74435f5d checked out (the commit before this evidence was amended in; the amend changes only this plan), no submodules (neither job reads them). The `Icons up to date` step's `run` block and the `Fork script tests` loop, extracted verbatim from `crosshatch-ci.yml` and run with `bash -e`: icons exit 0 ("matches a fresh run"); scripts exit 0, 9 files, each `Ran n` with n > 0. Doctored SVG (`arrow-clockwise.svg` plus one byte): generator exit 3, the step prints the exit-3 re-pin annotation and exits 1. Malformed `SHA256SUMS` line: exit 1, generic "failed with exit 1" annotation. `sim_sh_test.py` with `awk` off `PATH`: 4 failures "not on PATH: awk", `FAILED (failures=4)`, so the loop's non-zero branch fails the job. `git add --renormalize .`: no change. A `core.autocrlf=true` clone: `names.txt` and `SHA256SUMS` stay `w/lf`, `LICENSE` `w/crlf`, SVGs `w/none`; `sha256sum -c SHA256SUMS` passes and the generator exits 0 (a control file, `README.md`, is converted to `w/crlf`).
- `check_upstream_touches.py` on 74435f5d: trial merge of `upstream/develop` clean, PASS. `./bin/clang-format-fix` (clang-format 21.1.8 from the uv cache on a scratch `PATH`) run twice: the diff hash is unchanged by the second run, and no file outside this change was touched.

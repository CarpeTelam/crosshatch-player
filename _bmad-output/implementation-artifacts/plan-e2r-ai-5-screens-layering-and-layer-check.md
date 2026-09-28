---
title: 'e2r-ai-5: restore the Screens layering and add a layer check'
type: 'refactor'
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
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
  - '{project-root}/docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Retro O3/A1 (action AI-5): `GameMatchActivity` (Screens) includes `lib/GameScript` headers (`StoreSlot.h`, `Codec.h`) and names `GameScript::StoreSlot`, `Codec::STORE_LIMIT`, `Canvas`, and `InputEvent`, which the spine's layer table forbids; dca1ddc2 had fixed this and e8420aaa undid it, because the rule lived only in a commit message. Owner decision 2026-09-28: restore the rule; the spine's Screens row stays.

**Approach:** Route the store slot, its limit, and the canvas through `src/games` so the screen names no `GameScript::` type, with identical behaviour; add a fork CI check, `scripts/check_layers.py`, that holds the spine's layer table as data and fails any `#include` edge (and any `GameScript::` name in Screens) the table does not allow.

## Boundaries & Constraints

**Always:** behaviour unchanged (same PSRAM block layout and size, same OOM text, same leak on an abandoned VM, same flush points); new `.cpp` whole-file guarded `#if FREEINK_CAP_GAMES`; fork-script conventions (fork_common, exit contract 0/1/2, sidecar test, ledger Game paths, standard library only, no submodule needed); the check's data cites the spine table and diagram.

**Never:** amend the spine's Screens row; edit `ci.yml`; a `src/games` alias that the screen then spells `GameScript::`; change `GameSaveStore`'s host-tested interface.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Fixed tree | this commit | `check_layers.py` prints the edges checked, exit 0 | none |
| Pre-fix screen | fixture with `GameMatchActivity` including `<StoreSlot.h>`/`<Codec.h>` | exit 1, each line `path:line` naming the edge and what Screens may use | none |
| Qualified name | Screens file naming `GameScript::` or `using namespace GameScript` (outside comments) | exit 1 | none |
| Unknown header | include that resolves to no repo file and no table entry | exit 1, asks to classify it | none |
| Per-file edge | `SecureHttpClient.h` outside `ForkReleaseProbe`, `lua.hpp` outside `GamesBuildAnchor.cpp` | exit 1 | none |
| Wrong root | `--root` without the game dirs | exit 2 | SetupError |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.h:3,7,16,101-105` -- drops `<HalMemory.h>`, `<StoreSlot.h>`, `games/GameSaveStore.h`; members `storeStorage`/`store`/`saves` become one `MatchStore store`, declared before `vm` (destroyed after it).
- `src/activities/games/GameMatchActivity.cpp:6,80-85,93-115,213-219,298,303,318` -- `<Codec.h>` goes; destructor calls `store.leak()`; onEnter uses `store.allocate`, `store.saves()`, `store.slot()`, `GameVM::create(assets, viewport, replay, id, slot)`; `GameCore::GameEvent` (what `GameTouch::toEvent` takes; `GameScript::InputEvent` aliases it); `store.flushIfDue`/`flush`.
- `src/games/MatchStore.{h,cpp}` (new) -- owns the PSRAM block (slot `Codec::STORE_LIMIT` bytes, then `GameSaveStore::BUFFER_BYTES`), the `StoreSlot`, and the `GameSaveStore`; `allocate`, `ready`, `slot`, `saves`, `flushIfDue`, `flush`, `leak` (releases slot and block, as the old destructor did).
- `src/games/GameVM.{h,cpp}:32-59` -- `create` takes `const GameViewport&` and `const FrameReplay&` and builds the `GameScript::Canvas` itself (width/height as `int16_t`, `replay.textMetrics()`); the private constructor keeps `Canvas`.
- `scripts/check_layers.py` / `_test.py` (new) -- pattern of `scripts/check_api_freeze.py`; resolves quoted includes against the file's dir then `src/`, angle includes against `lib/<L>/` and `lib/<L>/src/`, then tables of std, platform, and SDK headers.
- `.github/workflows/crosshatch-ci.yml` -- new `Layer check` job (checkout only, python3), in `Crosshatch Test Status` needs; header comment bullet.
- `docs/crosshatch/upstream-touches.md:87-116` -- Game paths gain the two scripts (`src/games` already covers `MatchStore`).

## Tasks & Acceptance

**Execution:**
- [ ] `src/games/MatchStore.h`, `src/games/MatchStore.cpp` -- add the class -- one owner for ch.store's slot and limit in `src/games`.
- [ ] `src/games/GameVM.h`, `src/games/GameVM.cpp` -- `create(assets, viewport, replay, gameId, slot)` -- canvas built in `src/games`.
- [ ] `src/activities/games/GameMatchActivity.{h,cpp}` -- use `MatchStore`, the new `create`, `GameCore::GameEvent`; no `GameScript` include or name -- the layering fix.
- [ ] `scripts/check_layers.py` -- layer table as data (`LAYERS`, per-file `ONLY_FROM`, std/platform/SDK tables), Screens `GameScript` name scan, exit contract -- the regression gate.
- [ ] `scripts/check_layers_test.py` -- fixture trees in a temp dir for every matrix row plus the pre-fix `GameMatchActivity` include lines; also runs the real tree -- tests the gate both ways.
- [ ] `.github/workflows/crosshatch-ci.yml`, `docs/crosshatch/upstream-touches.md`, `docs/crosshatch/fork-scripts.md` (if it lists scripts) -- job, needs, ledger rows.

**Acceptance Criteria:**
- Given the fixed tree, when `grep -n 'GameScript\|StoreSlot.h\|Codec.h' src/activities/games/*` runs, then it finds nothing outside comments.
- Given the fixed tree, when `python3 scripts/check_layers.py` runs, then it exits 0; given the parent commit, it exits 1 naming `GameMatchActivity.h:7` and `.cpp:6` and the `GameScript::` uses.
- Given the simulator, when Home → Games → the counter fixture is tapped and relaunched, then the count persists through `ch.store` as before.

## Implementation Notes

- Implemented directly (this build agent cannot start subagents).
- `src/games/MatchStore.{h,cpp}` holds what `GameMatchActivity` held (`storeStorage`, the `StoreSlot`, the `GameSaveStore`) with the same allocation order and sizes; `leak()` releases the slot and the block and lets the `GameSaveStore` go, as the old destructor did. The screen keeps `slotLeaked` and its comments.
- `GameVM::create` takes the viewport and `FrameReplay` and builds `GameScript::Canvas` itself; the screen still calls `replay.loadFonts` first. The screen's input event is `GameCore::GameEvent`, the type `GameTouch::toEvent` fills and `GameScript::InputEvent` aliases. `<Memory.h>` and `<span>` left the screen's `.cpp` with their last uses.
- `scripts/check_layers.py` resolves 320 include edges in 82 files on this tree; on the baseline tree it reports 7 problems (the two includes and five `GameScript::` names). Beyond the table's rows, it allows `lib/Logging`/`lib/Memory` (AGENTS.md conventions) to `src/games` and Screens, the platform headers (Arduino, ESP-IDF, FreeRTOS, POSIX, OpenSSL) to both, upstream `src/` and `lib/I18n` to Screens (screen infrastructure), the diagram's upstream node (`lib/hal`, `ZipFile`, `PngToBmpConverter`, `GfxRenderer`) to both, and `lua.hpp` to `GamesBuildAnchor.cpp` only (AD-2). SDK headers are classified by name, so the job needs no submodule.
- New CI job `Layer check` (checkout, python, the script), in `Crosshatch Test Status` needs; the sidecar runs in `Fork script tests`.

## Plan Change Log

## Review Triage Log

### Pass 1 (self-run lenses, one at a time: blind hunter, edge-case hunter, verification gap, intent alignment)

Counts: high 0, medium 0, low 3, false 4, maybe-false 0.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | blind | `STD_HEADERS` lists only the headers in use, so a routine `<map>` or `<queue>` in game code fails as unclassified | low | patch | Added the common containers and `condition_variable`, `bitset`; tests and the real tree still pass. |
| 2 | blind | `MatchStore::allocate` called twice frees the block before replacing the slot that points into it | false | reject | `onEnter` is its only caller and runs once per activity; `StoreSlot`'s destructor never touches its bytes. |
| 3 | edge-case | `flushIfDue`/`flush`/`slot` dereference without `ready()` | false | reject | As before the change: `flushStore` checks `ready()`; `flushIfDue` runs only in Playing/Paused/Over, which need a VM, which needs `allocate` to have succeeded (`onEnter` returns on failure). |
| 4 | edge-case (deletion) | `<Memory.h>` and `<span>` left the screen | false | reject | Their only uses (`makeUniqueNoThrow`, `std::span`) moved to `MatchStore.cpp`; x4pro, default, and simulator builds pass. |
| 5 | edge-case (claims) | "same leak on an abandoned VM" | false | reject | `leak()` releases exactly the slot and the block, as the old destructor released `store` and `storeStorage`; the `GameSaveStore` was and is freed. |
| 6 | verification gap | `MatchStore` and the new `GameVM::create` have no host test | low | reject | Device/simulator code, like the rest of `GameMatchActivity` (deferred-work, lifecycle entry); the simulator run (tap, Leave, reopen restores 4 taps) covers slot, restore, flush, and canvas. |
| 7 | verification gap | `LAYERS` can drift from the spine's table text; nothing compares them | low | defer | A loosened spine row fails CI loudly; a tightened one is silently unenforced. AI-10 is about to edit the table's edges; deferred-work `## e2r-ai-5`. |
| 8 | intent alignment | The screen still passes a `GameScript::StoreSlot&` (from `store.slot()`) into `GameAssets::load` and `GameVM::create` without naming it | false | reject | The owner's rule is that Screens reach GameScript only through `src/games`; the screen neither includes nor names a GameScript type, and the task names `GameVM`/`GameSaveStore`-owned routing as the fix. |

## Design Notes

A new `MatchStore` rather than growing `GameSaveStore`: `GameSaveStoreTest` builds `GameSaveStore` over a span, and the slot-plus-buffer block is the match's lifetime concern. Screens → HAL/Storage/GfxRenderer and the rest of the upstream node come from the spine diagram (`ACT --> HAL`); "upstream screen infrastructure" (any upstream `src/` file, `lib/I18n`) and the AGENTS.md conventions (`lib/Logging`, `lib/Memory`) are allowed for Screens and `src/games`; platform headers (Arduino, ESP-IDF, FreeRTOS, POSIX) for `src/games` and Screens only. AD-2's anchor gives `GamesBuildAnchor.cpp` its `lua.hpp` edge.

## Verification

**Evidence (2026-09-28):**
- Host suites under the lock: 637/637 passed.
- `pio run -e x4pro`: SUCCESS (RAM 31.1%, flash 5,821,314 B); `pio run -e default`: SUCCESS (flash 5,624,045 B). Both compile `MatchStore.cpp`.
- `sim.sh setup && sim.sh build x4pro`: SUCCESS. Simulator run with `test/game_script/fixtures/counter` in `fs_/.games/`: Home → Games → Counter, three taps (Taps 3, Saved taps 3; log "saved ch.store (11 bytes)"), a fourth tap, Back → pause menu, Leave, reopen: "restored ch.store (11 bytes)", "opened with 4 saved taps", screen Taps 4 (`e2r-ai-5-screenshots/`).
- Fresh tree: cloning is refused in this sandbox, so an archive tree of commit a9902351 (no submodule archives: the Layer check job checks out without the submodule, and the archive's `freeink-sdk/` is empty like CI's). The job's step `python3 scripts/check_layers.py` there: 320 edges in 82 files, exit 0; `check_layers_test.py` there: OK.
- `python3 scripts/check_upstream_touches.py` at a9902351: PASS, trial merge of upstream/develop clean.
- `./bin/clang-format-fix` twice: no changes.
- `for t in scripts/*_test.py`: all 7 OK (`check_layers_test.py` 20 tests).
- `python3 scripts/check_layers.py`: 320 edges in 82 files, exit 0; on an archive of the baseline's `lib/` and `src/` (93b1590f): exit 1, 7 problems (`GameMatchActivity.h:7` `<StoreSlot.h>`, `.cpp:6` `<Codec.h>`, `GameScript::` at `.h:104`, `.cpp:94,97,113,298`).

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` (under the lock) -- all pass.
- `pio run -e x4pro`, `pio run -e default` (under the lock) -- success.
- `sim.sh build x4pro`; run Home → Games → `test/game_script/fixtures/counter`, tap, leave, relaunch -- count restored.
- `for t in scripts/*_test.py; do python3 "$t"; done` -- all OK.
- `python3 scripts/check_layers.py` in a fresh archive tree of the commit -- exit 0; on the parent commit's tree -- exit 1.
- `python3 scripts/check_upstream_touches.py` -- exit 0.

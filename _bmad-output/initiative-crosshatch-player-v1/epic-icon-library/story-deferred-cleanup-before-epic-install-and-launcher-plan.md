---
title: 'Deferred cleanup before epic-install-and-launcher'
type: 'chore'
ticket: '10'
created: '2026-09-28'
status: 'built'
baseline_revision: 'ba4cf74b1b355896ee80f406990cbdfb76d967e3'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Four deferred items the owner chose (epic Notes, entry 8 answer 7) are open: nothing scripted reports a stale
simulator block in `platformio.local.ini`; the level-1 list's `manifest` keys are compared with nothing; `check_layers.py`'s
`LAYERS` mirrors the spine's layer table by hand with no check; and the error view's host-failure wording
(`GameVM::failure()`'s order, `vmFailureText`'s mapping, `vmHealthy`'s headline) has no host test.

**Approach:** (1) a `sim.sh check` that exits 1 on a missing or stale managed block, used by `build`; (2) a manifest key
table in `Manifest.h` that the reader looks names up in, compared with the list both ways in `ApiLevelTest`; (3)
`LAYERS` split into the table's own terms plus named additions, and a test that parses the spine's table (layer rows and
Upstream hooks) and compares; (4) a pure `lib/GameScript/VmFailure` mapping that `GameVM` and the match call, with host
tests. Each item is marked resolved in `deferred-work.md`.

## Boundaries & Constraints

**Always:** the match's behaviour and every string it shows stay identical; the manifest parser accepts and rejects
exactly what it does now; `check_layers.py`'s resulting `LAYERS` is unchanged; no new include edge; fork-script rules
(standard library, single quotes); `tr()` stays in the Screens file (GameScript and src/games may not include lib/I18n).

**Never:** touch 3.9's files (`names.txt`, `gen_game_icons.py`, `GameIcons.generated.h`, `ChBindings`, `DisplayList`,
`FrameReplay`, `GameIconBlit`, `GameViewIcons`, `ApiSurfaceTest.cpp`, `api-level-1.txt`, `ApiLevel.h`) or
`API_SURFACE_CRC`; take other deferred items; edit `.skills/` or the SDK.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Stale block | managed block differs from `simulator.ini` (or no block) | `sim.sh check` exits 1 naming it; `build` runs setup first | none |
| Current block | after `sim.sh setup` | `sim.sh check` exits 0 | none |
| Key drift | a key in the list but not the table, or the reverse | `ManifestKeysMatchTheParser` fails naming it | none |
| Spine tightened/loosened | a term removed from or added to a row | table test fails naming the row and edge | unknown spine term fails too |
| Host failure | NoSession, OutOfMemory, NotLoaded, Script, None | detail: OOM text, OOM text, NotLoaded text, script message, script message; headline "could not start" only for NoSession | none |

</frozen-after-approval>

## Code Map

- Work only in `/home/user/wt-cleanup` (branch `epic3/cleanup`); run `git submodule update --init --recursive` first.
  Every `pio run`, `sim.sh setup`/`build`, and host-test CMake configure/build runs as
  `flock /tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/build.lock sh -c '<commands>'`;
  scratch under that scratchpad's `3.10/`. Toolchain installed; do not reinstall. Never commit `_bmad/render/`; do not
  commit at all. Verify with host tests, the fork-scripts loop, `check_layers.py`, `check_upstream_touches.py`,
  `pio run -e x4pro` and `-e default`, `sim.sh build x4pro`; the fresh tree follows review.
- `.claude/skills/run-crosshatch-player/sim.sh` -- `managed_block()`, `cmd_build` already re-runs setup on a stale
  block (d2d8e81a, never marked resolved); add `cmd_check` + dispatch line; `build` calls it. `SKILL.md` "Setup and build".
- `lib/GameCore/Manifest.h/.cpp` -- `ManifestReader::onKey` if-chains on names (depth 1 and seats at depth 2); `Key` and
  `SeatKey` are private enums. Reuse them in a public table.
- `test/game_core/ApiLevelTest.cpp` -- beside `ManifestLimitsMatchTheParser`; `ApiLevelList::Surface::entries()` gives
  `manifest` entries with `name` = dotted key.
- `scripts/check_layers.py` -- `LAYERS` (:87), `CONVENTIONS`, `UPSTREAM_EDGES`, `ONLY_FROM`; component names.
  `scripts/check_layers_test.py` `TableTest`. Spine: `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md`
  :30-38 (tracked, so CI's checkout has it).
- `src/games/GameVM.h/.cpp` -- `enum class Failure`, `failure()` (:146), `errorMessage()`. `src/activities/games/GameMatchActivity.cpp`
  -- `vmFailureText` (:55), `vmHealthy` (:257), `stopStuckVm`. Screens may not write the word GameScript.
- `test/game_script/CMakeLists.txt` -- explicit test list; `lib/GameScript/*.cpp` globbed.
- `_bmad-output/implementation-artifacts/deferred-work.md` -- lines 1-3, 93-95, 123-125, 184-186.

## Tasks & Acceptance

**Execution:**
- [x] `sim.sh`, `SKILL.md` -- `check`: exit 0 when the block equals `simulator.ini`, else print why and exit 1; `build`
  runs setup when `check` fails; document `check`.
- [x] `Manifest.h/.cpp` -- public `Key`/`SeatKey`; `ManifestKey {path, key, seat}` and `MANIFEST_KEYS` (dotted paths);
  `onKey` looks both levels up in it. `ApiLevelTest.cpp` -- `ManifestKeysMatchTheParser` (both directions, no duplicates).
- [x] `check_layers.py` -- `TABLE` (the table's terms), `BEYOND_TABLE` (STD, platform, conventions, diagram edges, with
  reasons), `LAYERS = TABLE | BEYOND_TABLE`, `SPINE_TERMS`, `SPINE_PATH`; docstring. `check_layers_test.py` -- parse the
  spine's table; the layer rows equal `TABLE`, the hooks row equals `UPSTREAM_EDGES`, every term is known; doctored copies
  (tightened, loosened, unknown term) fail.
- [x] `lib/GameScript/VmFailure.h/.cpp` -- `VmFailure`, `vmFailure(failed, sessionOutOfMemory, host)`,
  `failedToStart`, `HostFailureTexts`, `failureDetail`. `GameVM` -- `using Failure = GameScript::VmFailure`,
  `failure()` delegates, `failedToStart()`, `failureDetail(texts)`. `GameMatchActivity.cpp` -- `vmFailureText` passes
  `tr()` texts; `vmHealthy` uses `failedToStart()`. `test/game_script/VmFailureTest.cpp` + CMake list.
- [x] `deferred-work.md` -- mark the four entries `Resolved by entry 10` (the hash is added at marking, as 213a59f5 did).

**Acceptance Criteria:**
- Given the committed tree, when host tests, every `scripts/*_test.py`, `check_layers.py`, `check_upstream_touches.py`,
  x4pro and default builds, and `sim.sh build x4pro` run, then all pass.
- Given a fresh archive tree of the commit, when the Fork script tests step's loop runs, then every file passes.

## Implementation Notes

- `sim.sh check` distinguishes three causes (no block, a begin marker without its end marker, a stale block); `build`
  runs `setup` when it fails, and `setup` still refuses a block without its end marker.
- `MANIFEST_KEYS` holds exactly the list's nine dotted keys (no bare `seats` row): `onKey` matches a top-level name
  against each path's first component and a seats name against the part after the dot, so the table and the list
  compare one to one. Removing `hidden` from the table fails `ManifestKeysMatchTheParser` (and `ManifestTest`).
- `LAYERS` is built per component as `TABLE[comp] | BEYOND_TABLE.get(comp, set())` (a dict `|` would replace, not
  merge); it compares equal, order included, to the hand-written one. `SPINE_TERMS['nothing']` is `None` (no edge).
  The hooks row's `never` clause is also split on ` or ` and must equal `GAME_SCRIPT_OR_LUA`.
- `GameVM::HostFailureTexts` aliases `GameScript::HostFailureTexts` at class scope, so Screens fill it without naming
  GameScript; `vmFailureText` now calls `tr()` for both texts up front (a lookup, no side effects).
- New files are untracked (no commit): `bin/clang-format-fix` formats only `git ls-files`, so they were formatted
  through it with a temporary `git add -N` that was reset afterwards; the committer must `git add` them.

## Plan Change Log

## Review Triage Log

Pass 1 (thorough; the four lenses ran as context-free subagents: blind-hunter, edge-case-hunter, verification-gap,
intent-alignment). Verdicts: high 0, medium 1, low 15, false 3, maybe-false 0. Routes: patch 8, defer 2, reject 9.

| # | Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- | --- |
| 1 | verif. gap, blind, intent | `GameVM::failure()`'s two adjacent bools, `vmFailureText`'s two slots, `vmHealthy`'s StrId are untested; the 3.7 entry says "Resolved" | medium | defer (`## 3.10`): `test/` cannot build `GameVM`/the match (AI-2 harness); the 3.7 entry's wording now says the call sites stay open |
| 2 | verif. gap, intent | `sim.sh check` has no automated test; CI never reaches its failing branches | low | defer (`## 3.10`): no sidecar harness outside `scripts/`; shown by hand in Verification |
| 3 | edge, blind, verif. other | `BEYOND_TABLE` keys outside `TABLE` are dropped; an edge can sit in both | low | patch: `TableTest` asserts keys subset and disjoint rows |
| 4 | blind | an edge moved from `TABLE` to `BEYOND_TABLE` escapes `SpineTest` | low | patch (docstring): `BEYOND_TABLE` is reviewed by hand, each edge with a reason; a pinned copy would be a second hand mirror |
| 5 | edge | dropping one of two terms naming one component (HAL/Storage) passes; the resolved text overstated it | low | patch: docstring and `deferred-work.md` say `SpineTest` compares components; the Layer check cannot enforce the difference either |
| 6 | edge | a spine hooks row without its "never" clause passes | low | patch: `parse_hooks` reports a missing clause, with a doctored case |
| 7 | edge | the sentence after "never ..." is not read | low | reject: the hook files are compared from the Lives-in cell, so another file fails there |
| 8 | edge | duplicate hook row numbers overwrite | low | reject: needs a new guard for an unlikely spine typo |
| 9 | blind | `ONLY_FROM`, `FILE_EDGES`, file scopes, Engine row, diagram not compared | low | patch (docstring names them); `test_secure_http_client_only_from_fork_release_probe` already fails an emptied `ONLY_FROM` |
| 10 | blind | `check()`'s failure text names only `TABLE` | low | patch: names `TABLE (or BEYOND_TABLE)` and the test |
| 11 | edge, blind | a nested `MANIFEST_KEYS` entry under a non-Seats key (or a new `SeatKey`) would never be read | low | patch: `static_assert` that seat and dot go with `Key::Seats` exactly |
| 12 | blind, intent | manifest types and `?` are not compared | false | Design Notes: the ticket names keys; the table holds no type the parser uses; `ManifestTest` pins each type |
| 13 | edge, blind | a block without its end marker: check offers no remedy; SKILL.md implies build repairs it | low | patch: message and SKILL.md say restore by hand |
| 14 | edge, blind | an end marker above the begin marker passes the presence test; setup drops lines | low | reject: pre-existing in `setup` (d2d8e81a), needs a hand-broken file |
| 15 | blind | `HostFailureTexts` defaults to `""` | low | reject: one caller sets both fields beside the declaration |
| 16 | blind | resolved entries lack the commit; evidence lines unchanged | false | the orchestrator adds the hash when marking (approved); earlier resolved entries keep their evidence |
| 17 | blind | plan file not in the diff | false | untracked at review time; committed with the story |
| 18 | blind | `VmFailure.h` includes all of `LuaGame.h` for one enum | low | reject: compile coupling only; splitting `HostFailure` out edits `LuaGame.h` for no behaviour |
| 19 | blind | `Key`/`SeatKey` made public | low | reject: the plan's chosen shape; a member table restructures more of the header |

## Design Notes

- Item 1 was already built in d2d8e81a; the gap is the scripted check. `check` is the chosen form (the ticket allows a
  test or a scripted check); a Python test would have no sidecar script in `scripts/`.
- Item 2 compares keys, not types: the table carries no types the parser uses, so a type column would be a second
  hand copy. `ManifestTest` pins each key's type handling.
- Item 3's parser strips backticks and parentheticals, splits on `,` `;` ` and ` ` / `, and maps each term through
  `SPINE_TERMS` (a path maps to itself). The Engine row (`lib/lua`, unscanned) is skipped by name.
- Item 4: the pure function takes the host texts as arguments so the `tr()` keys stay in Screens and no include edge is
  added; the detail choice (the reviewer's "OutOfMemory falls back to English" mutation) is then host-tested.

## Verification

**Commands:**
- `sim.sh check` on a doctored block -- exit 1; after `sim.sh setup` -- exit 0.
- host tests (AGENTS.md cmake/ctest) -- all pass, including the three new tests.
- the fork-scripts loop (`docs/crosshatch/fork-scripts.md`) -- no FAILED / RAN NO TESTS; once more in a fresh tree.
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py` -- exit 0.
- `pio run -e x4pro`, `pio run -e default`, `sim.sh build x4pro` (under the lock) -- succeed.

**Results (after the review patches, on the final tree):**
- `sim.sh check` with the current block: "managed simulator block is current", exit 0. With the two
  `-DFREEINK_CAP_GAMES=1` lines deleted from the managed block: "differs from simulator.ini (stale); run ... setup",
  exit 1. After `sim.sh setup`: exit 0 again. The implementer also saw exit 1 before any setup (no block) and
  `sim.sh build x4pro` print "running setup" on a doctored block.
- Host tests: 704/704 pass (`VmFailureTest` 6 cases, `ApiLevelTest.ManifestKeysMatchTheParser`). The table check
  fails when `hidden` is removed from `MANIFEST_KEYS`, and the `static_assert` fires on a `hidden.x` path (both
  tried and reverted by the implementer).
- Fork-scripts loop: 8 files, every one "Ran n tests" with n > 0 (`check_layers_test.py` 49, with `SpineTest`), no
  FAILED.
- `check_layers.py`: 362 edges in 92 game files, passed; `check_upstream_touches.py`: PASS.
- `pio run -e x4pro`, `pio run -e default`, `sim.sh build x4pro` (simulator_x4pro): SUCCESS. Flash and RAM: unmeasured.
- Fresh tree: `git clone` of the worktree checked out at 41036419 (no submodule, as CI's `Fork script tests` checkout),
  then the `Run every scripts/*_test.py` step's loop: 8 files, each "Ran n tests" (n > 0), exit 0; and the `Layer
  check` step's `python3 scripts/check_layers.py`: exit 0. The amended commit differs from 41036419 only by this line.

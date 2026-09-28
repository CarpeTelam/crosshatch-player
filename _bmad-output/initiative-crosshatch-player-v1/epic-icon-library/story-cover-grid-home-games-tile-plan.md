---
title: 'Cover-grid Home Games tile'
type: 'feature'
ticket: '6'
created: '2026-09-28'
status: 'built'
baseline_revision: '7329a0201859c240bcc0f3aee6829d3328e3b6b3'
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

**Problem:** In cover-grid mode Home hides Games (`HomeActivity::showsGamesItem()` returns false there), so on the
X4 Pro and Sticky cover-grid theme Games cannot be opened at all (epic R9).

**Approach:** Under `FREEINK_CAP_GAMES`, grow `CoverGridHomeUi`'s tab bar by one tab, placed before Settings, that
draws `GameIcons::GAME_CONTROLLER_32` with `renderer.drawIcon`; count and map Games in both Home modes in
`HomeActivity`; add the upstream `CoverGridHomeUi.cpp` → `lib/GameIcons` edge to the spine's layer table and to
`scripts/check_layers.py`, with a rule that an upstream file includes game headers only as its ledger row allows;
update ledger rows 6–9's text.

## Boundaries & Constraints

**Always:** every upstream-file change sits inside `#if FREEINK_CAP_GAMES` (the upstream lines stay verbatim in an
`#else` where a value changes), touching only `HomeActivity.h/.cpp` and `CoverGridHomeUi.h/.cpp` (ledger rows 6–9);
the Games tab sits where list mode puts its row (after File Transfer, before Settings), so `menuItemToIndex` /
`indexToMenuItem` serve both modes unchanged; `default`, `x4c`, `papermono` compile no game code here.

**Never:** a `lib/GameIcons` include in `CoverGridHomeUi.h`, `HomeActivity`, or any upstream file but
`CoverGridHomeUi.cpp`; a label or new string; `drawGameIcon`/`src/games` from the tile (the Notes pick
`renderer.drawIcon`); changes to the list theme's rendering; any edit to the spine beyond the layer table, its
amendment note, and the diagram.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Tap tile | cover grid, any OPDS state | Games list opens | none |
| Return | Leave Games → Home | Games tab selected (`goHome` maps `GamesList`) | none |
| Buttons | Left/Right walk tabs | Games reachable between Transfer and Settings; Confirm opens it | none |
| No OPDS | 5 tabs (Folder, Library, Transfer, Games, Settings) | painter skips Blocks as today | none |
| List theme | Lyra | unchanged: Games row with Blocks icon above Settings | none |
| C3 envs | no `FREEINK_CAP_GAMES` | upstream code compiled exactly | none |
| Check | an upstream file includes a game header its row does not allow | `check_layers.py` exit 1, path:line | printed |

</frozen-after-approval>

## Code Map

- `src/components/CoverGridHomeUi.h:63` -- `std::array<TabItem, 5> tabItems`; guarded 6 (row 8).
- `src/components/CoverGridHomeUi.cpp` `drawTabs` -- `ICONS[]` + `for (i < 5)` + painter `index>=2 ? index+1` OPDS skip;
  guarded 6-entry `ICONS` with `GameIcons::GAME_CONTROLLER_32` before `Settings2Icon`, loop over `std::size(ICONS)`
  under the guard; guarded `#include <GameIcons.generated.h>` (row 9). `tab.value` = books + count → HomeActivity's
  flat index.
- `src/activities/home/HomeActivity.h/.cpp` -- `showsGamesItem()` (static, `!hasCoverGridHome()`) gates the Games
  count/mapping/row; remove it so Games always counts (rows 6, 7). `loop()`'s cover-grid band uses `menuCount`.
- `src/activities/ActivityManager.cpp:32,340` -- row 5 includes `games/GamesListActivity.h`, maps `GamesList`/`GameMatch`
  back to `HomeMenuItem::GAMES`; unchanged.
- `lib/GameIcons/GameIcons.generated.h` -- `inline constexpr uint8_t GAME_CONTROLLER_32[SMALL_BYTES]`, drawIcon layout.
- `scripts/check_layers.py` -- `upstream_problems()` fails only GameScript/lua today; add `UPSTREAM_EDGES` (row 5
  `ActivityManager.cpp` → Screens; row 9 `CoverGridHomeUi.cpp` → `lib/GameIcons`; row 10 `OtaUpdater.cpp` →
  `lib/GameCore`, `src/games`) and fail any other upstream include of a game component (COMPONENTS).
- `scripts/check_layers_test.py` -- `BASE` fixture already has `OtaUpdater.cpp`; `TableTest`.
- `ARCHITECTURE-SPINE.md` lines 30–37 (layer table + amendment), 42–66 (diagram, "shows row 10").
- `docs/crosshatch/upstream-touches.md` rows 6–9.

## Tasks & Acceptance

**Execution:**
- [x] `src/components/CoverGridHomeUi.h`, `.cpp` -- guarded tab array, icon list, loop bound, include -- the tile.
- [x] `src/activities/home/HomeActivity.h`, `.cpp` -- drop `showsGamesItem()`; guarded Games always counted, mapped, and in the list row -- both modes open Games.
- [x] `lib/GameIcons/GameIcons.h` -- comment names the Home tile's direct use.
- [x] `scripts/check_layers.py` -- `UPSTREAM_EDGES`, rule, docstring -- R10 edge and ledger rule.
- [x] `scripts/check_layers_test.py` -- fixture `CoverGridHomeUi.cpp` and `ActivityManager.cpp` pass; `HomeActivity.cpp` including `GameIcons.h` fails; `CoverGridHomeUi.cpp` including `GameCore` fails; `TableTest`: every `UPSTREAM_EDGES` key is a Ledger path in `docs/crosshatch/upstream-touches.md`.
- [x] Spine -- layer-table row "Upstream hooks (AD-3 ledger rows)", dated amendment line, diagram node `CoverGridHomeUi.cpp` → ICO.
- [x] `docs/crosshatch/upstream-touches.md` -- rows 6–9 text.

**Acceptance Criteria:**
- Given the x4pro simulator in cover-grid theme, when Home shows, then a game-controller tab sits before Settings, and a tap on it opens Games.
- Given the Lyra theme, when Home shows, then it matches the pre-change list (Games row, Blocks icon).
- Given the tree, when `check_layers.py`, its test, and `check_upstream_touches.py` run, then all pass.

## Implementation Notes

- `check_layers.py`: a ledgered file that includes `lib/GameScript` or `lib/lua` still fails with the laundering
  message; `UPSTREAM_EDGES` never grants those (TableTest asserts it, and a fixture case covers OtaUpdater).
- The TableTest reads the ledger with `check_upstream_touches.parse_ledger` from the working tree's
  `docs/crosshatch/upstream-touches.md` (a file read, no git history).
- Spine: the prose under the diagram said "shows row 10"; it now says rows 9 and 10 and points at the Upstream hooks
  row (the plan's Code Map names that line).
- `check_upstream_touches.py` checks a ref, not the working tree; run against `git stash create`'s commit: PASS, with
  `CoverGridHomeUi.h/.cpp` listed as ledgered.
- Simulator (x4pro, no OPDS, 5 tabs): tab tap and Confirm open GamesList; Back from Games lands on the Games tab;
  Left/Right walk Transfer, Games, Settings. The OPDS (6-tab) case was not exercised in the simulator.
- After review (planner): the OPDS (6-tab) case ran on x4pro and sticky (`fs_/.crosspoint/opds.json` with one server,
  `uiTheme` 4): the controller tab opens GamesList, Back lands on it, the gear tab opens Settings; sim settings restored.
- `lib/GameIcons/GameIcons.h`'s comment was rewrapped to the 120-column limit (comment only).
- Assumed (reversible, for entry 8): the Upstream hooks row names rows 5 and 10 beside row 9, because the new rule
  must allow their as-built includes; `docs/crosshatch/game-icons.md`'s "Names the runtime screens will use" is left
  for after the Runtime lane merges (3.5 edits the same section).

## Plan Change Log

## Review Triage Log

Pass 1 (baseline `7329a020`, diff `scratchpad/3.6/review-diff-1.patch`). The four lenses (blind-hunter,
edge-case-hunter, verification-gap, intent-alignment) ran as context-free subagents, launched together. Verdicts:
high 0, medium 1, low 8, false 2, maybe-false 0; intent-alignment is descriptive (no verdicts).

| # | Lens | Finding | Verdict | Route | Evidence / action |
| --- | --- | --- | --- | --- | --- |
| 1 | verification-gap, blind 6 | The 6-tab (OPDS) layout and Sticky never ran; tab icon ↔ `indexToMenuItem` has no check | medium | patch | Ran x4pro and sticky with an OPDS server: controller tab → GamesList, Back → controller tab selected, gear → Settings (sim log); screenshots added. The host test is deferred (`## 3.6`). |
| 2 | edge-case | An upstream include that `resolve()` cannot classify (ambiguous basename) skips the new rule | low | reject | No header basename resolves to a game component and another today; the skip predates this change for GameScript/lua; a fix adds branches for an unlikely case. |
| 3 | edge-case | A stale `UPSTREAM_EDGES` grant stays after its include is removed | low | reject | Unlikely; the fix adds an unused-grant pass; review of the ledger row catches it. |
| 4 | edge-case | Spine amendment says the check fails "any other" upstream include, overclaiming for #2 | low | reject | Same root as #2; true for every include the check resolves. |
| 5 | blind 1 | `docs/crosshatch/game-icons.md` still says no screen draws a library icon and lists entry 6 as future | low | defer | Real; 3.5 (Runtime lane) edits the same section in parallel, and the Notes keep parallel entries off shared files; deferred to after the merge (`## 3.6`). |
| 6 | blind 2 | The layer check does not verify the `#if FREEINK_CAP_GAMES` guard around upstream game includes | low | defer | Pre-existing for rows 5 and 10; the ledger says the job enforces paths only and review holds the Guarded column (`## 3.6`). |
| 7 | blind 3 | Upstream file includes `GameIcons.generated.h`, not `GameIcons.h` | false | reject | Entry 1's description puts the per-name bitmaps entry 6 reaches in the generated header, and AD-24 fixes its name. |
| 8 | blind 4 | Renaming `game_controller` is caught only by an S3 build | false | reject | CI builds x4pro and sticky on every PR, so the rename fails CI. |
| 9 | blind 5 | Plan lacks `pio check`, format, build and host-test results | — | reject | Fix edits this plan; results are recorded in Verification below; `pio check` is the orchestrator's pre-PR step. |
| 10 | blind 7 | Ledger rows 5 and 10 do not say their includes are checked | low | patch | Prose sentence after the Ledger table; `parse_ledger` still reads the same 10 paths. |
| 11 | blind 7 | Spine diagram has no `ActivityManager.cpp` → Screens arrow | low | reject | The prose under the diagram says it shows rows 9 and 10 and points to the table row that lists every edge. |
| 12 | blind 8 | `for` header split across `#if/#else`; `static_assert` checks length only | low | reject | Deliberate: the upstream line stays verbatim (Design Notes); Blocks at index 2 is upstream's own assumption, unchanged. |
| 13 | blind 9 | No test for a header or an upstream lib/ file including GameIcons | low | patch | Two failing cases added (`CoverGridHomeUi.h`, `lib/hal/HalIcons.h`); 35 tests pass. |
| 14 | blind 9 | TableTest does not compare `UPSTREAM_EDGES` with the spine row; fixture uses `GameMatchActivity.h` | low | reject | Spine/`LAYERS` drift is already deferred (e2r-ai-5 entry); both fixture headers are Screens, so the edge is the same. |
| 15 | intent | Reading B (both devices, both OPDS states) was shown only for x4pro, 5 tabs | — | — | Closed by #1. D1 (hand-kept table tied to the ledger by TableTest) and the path-based scope are the plan's design. |

## Design Notes

Tab order follows list mode so one index mapping serves both; the upstream `for (i < 5)` line stays in the `#else`
branch. `ActivityManager.cpp` (row 5) and `OtaUpdater.cpp` (row 10) edges are as-built from epic 2; the new rule must
allow them, so the spine row names them beside the new row-9 edge.

## Verification

**Commands** (work only in `/home/user/wt-home`; wrap every `pio`, `sim.sh`, and CMake build as
`flock /tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/build.lock sh -c '<commands>'`; do not commit):
- `python3 scripts/check_layers.py && python3 scripts/check_layers_test.py` -- pass
- `python3 scripts/check_upstream_touches.py` -- exit 0
- `pio run -e default|x4pro|sticky|x4c|papermono` -- all succeed
- host suites (AGENTS.md cmake/ctest) -- pass
- `sim.sh build x4pro`; screenshots: cover-grid Home, Games after tap, Lyra Home

**Manual checks:** look at each screenshot; copy to `story-home-tile-screenshots/`.

**Results:**
- `pio run -e x4pro | sticky | default | x4c | papermono` under the lock: all SUCCESS (logs `scratchpad/build-*.log`).
- Host suites (cmake/ninja/ctest, under the lock): 683/683 passed.
- After the review fixes, on the final tree: `pio run -e x4pro` and `-e default` SUCCESS, host suites 683/683.
- `./bin/clang-format-fix` run twice last; the second run changed nothing.
- `check_layers.py` passed (351 edges, 88 files); `check_layers_test.py` 35 OK; every `scripts/*_test.py` passes;
  `check_upstream_touches.py` PASS on the commit (after commit, HEAD).
- `sim.sh build x4pro` and `sim.sh build sticky` succeeded; screenshots, each looked at, in `story-home-tile-screenshots/`:
  - `cover-grid-home.png` -- x4pro cover grid, no OPDS: Folder, Library, Transfer, game controller, Settings.
  - `games-after-tap.png` -- x4pro: the Games list after tapping the controller tab.
  - `cover-grid-return-from-games.png` -- x4pro: back from Games, the controller tab is selected.
  - `cover-grid-home-opds.png` -- x4pro with an OPDS server: six tabs, the controller before Settings.
  - `cover-grid-return-from-games-opds.png` -- x4pro, six tabs: back from Games lands on the controller tab.
  - `sticky-cover-grid-home-opds.png` -- Sticky with an OPDS server: six tabs fit.
  - `sticky-games-after-tap.png` -- Sticky: the Games list after tapping the controller tab.
  - `lyra-home.png` -- list theme (Lyra): unchanged, Games row with the Blocks icon above Settings.
- Flash: unmeasured (the tile references `GAME_CONTROLLER_32`, already linked through `GameScript`'s `ICONS`).

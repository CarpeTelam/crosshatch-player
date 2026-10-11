---
title: 'Epic 8 retrospective AI-9: the companion-folder and games-check test refactor'
type: 'refactor'
ticket: ''
created: '2026-10-11'
baseline_revision: '507f10d1c6384ad0793a20c53d08772da2c0fdf0'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: ['{project-root}/.agents/skills/refactor-for-review/SKILL.md', '{project-root}/.agents/skills/scope-discipline/SKILL.md']
warnings: []
deferred: []
---

<intent-contract>

## Intent

**Problem:** Epic 8's retrospective (AI-9; findings D1, D3, G2) left three kinds of duplication and misgrouping in the games-check tests:

- **D1:** Sudoku's companion folder has two draw recorders (`drawn.lua`'s `record` and `canvases.lua`'s); the UTTT and Battleship `trace.lua` headers drifted apart, though their code is identical.
- **D3:** `eq` is defined in Sudoku's `canvases.lua` and `checks.lua` although `pins.lua` exports one; Battleship's `checks.lua` and `draws.lua` both define `eq` and a byte-identical `withCanvas`.
- **G2:** `GamesCheckEngineTest.cpp` groups 13 tests and 2 helpers by ticket ("the cross-story review's fixes (e8-xr)", lines 1140-1484), not by behaviour.

**Approach:** One recorder and one `eq` per Sudoku folder, one `eq` and one `withCanvas` per Battleship folder, each defined once in a small module of that folder. Both `trace.lua` headers get one identical text. The e8-xr tests move into the behaviour sections they belong to, in the same file.

## Boundaries & Constraints

**Always:**
- Nothing is shared across games: no file or `require` crosses from one game's companion folder to another's, and no shipped file under `games/` changes.
- Behaviour-preserving: `ctest -L games-check` lists the same tests, with the same names and count, and all pass. The companion checks still run in their VMs within their limits (README, "The check VMs' limits").
- Load nesting (`games/sudoku/main.lua` header): a module a new module needs is already loaded when it is required, as `pins.lua` already requires the game's modules first.
- Test names, assertions and test count in `GamesCheckEngineTest.cpp` stay the same; only the order of the blocks changes.

**Never:**
- No merge of recorders across games (R2; owner Decision 2026-10-04).
- No firmware code change, so no `pio run`, `pio check` or simulator build.
- `pins.lua`'s `collectgarbage("generational")` at its load is not pulled into `canvases.lua` or `checks.lua`.

</intent-contract>

## Code Map

- `test/game_script/first_party/sudoku/basics.lua` -- NEW. `eq` (the `(what or "")` form of `pins.lua`) and `record(f, on_image, on_call)` (moved verbatim, with its TEST DOUBLE comment, from `drawn.lua:11-43`). Requires nothing.
- `.../sudoku/pins.lua` -- drop the local `eq`; `eq = basics.eq` in the export. Require `basics` after `solver`; header says a module requires `basics` before `pins`.
- `.../sudoku/drawn.lua` -- drop the local `record`; `local record = basics.record`; require `basics` before `pins`; keep `drawn.record` (marks.lua, interaction.lua:165 read it). Header: the recorder is `basics.lua`'s.
- `.../sudoku/rules.lua`, `interaction.lua`, `marks.lua` -- require `basics` before `pins`, so `pins` loads nothing nested.
- `.../sudoku/canvases.lua` -- drop `eq` and `record`; require `basics`; the three `record` calls pass `nil` for `on_image`. Header and the "fuller one" comment go. Still requires no pin module.
- `.../sudoku/checks.lua` -- drop `eq` (line 67); `require("basics")` in its require block. Does not require `pins`.
- `.../battleship/basics.lua` -- NEW. `eq` (the `what .. ": got"` form) and `withCanvas`, byte-identical to the two copies.
- `.../battleship/checks.lua`, `draws.lua` -- drop both local defs (checks 30-32 and 796-802; draws 21-23 and 320-326); require `basics`.
- `.../battleship/trace.lua`, `.../ultimate-tic-tac-toe/trace.lua` -- one header text for lines 9-14; after the edit the two files are byte-identical (`cmp`).
- `.../first_party/README.md` -- "Observing draw commands" (the recorders' list), "The split of Sudoku's checks and pins" (the module list and the canvases sentence).
- `test/game_script/harness/games_check/GamesCheckEngineTest.cpp` -- see Tasks. Not touched: `games_check.cmake`, any other file of the harness.
- Not touched: `games/`, `ultimate-tic-tac-toe/checks.lua` (its `eq` and `withCanvas` are each defined once in the folder).

## Tasks & Acceptance

**Execution:**
- [ ] Sudoku: `basics.lua`, `pins.lua`, `drawn.lua`, `rules.lua`, `interaction.lua`, `marks.lua`, `canvases.lua`, `checks.lua` -- one `eq`, one `record` -- D1, D3.
- [ ] Battleship: `basics.lua`, `checks.lua`, `draws.lua` -- one `eq`, one `withCanvas` -- D3.
- [ ] Both `trace.lua` -- identical headers, game-neutral about where the count is pinned -- D1.
- [ ] `first_party/README.md` -- the two passages -- D1.
- [ ] `GamesCheckEngineTest.cpp` -- regroup (below) -- G2.

The regrouping, each moved block keeping its leading comment, and the e8-xr heading going away:
- outcomes (renamed "outcomes and the end frame"): `endGame`, `ATextFrameStartedInsideTheHosts...`, `OnlyTheFrameTheDeviceShows...`.
- rounds and modes: `AnEntryOfRoundsThatIsNotARegularLuaFile...`, at the end.
- taps: `ATapOffTheCanvasUnderCheck...`, at the end.
- the hidden flow: `AHiddenShowsIsMetOnlyByAFrameTheFlowDrew...`, after `AHiddenMoveThatKeepsTheTurn...`.
- settings, seeds, and steps: `ARoundsStepsFunctionAndAModuleLoadSee...`, `TheModuleLoadingProbeAndTheChecksSee...`, after `AGameThatBreaksOnlyAtTheX4ProCanvas...`.
- a new section "Continue: the restore probe (row 6)": the four `TheRestoreProbe...` tests, after the settings section.
- the check VMs' limits (renamed "the heap margin and the check VMs' limits (rows 4 and 5)"): `heapGame`, `APlayedGameThatNeedsMoreThanTheCap...`, `TheMarginPlayDoesNotResolve...`, after the limits tests.
- `CANVASES` moves up to the file's constants, before `padGame`.

**Acceptance Criteria:**
- Given the tree after the change, when `ctest -L games-check -N` is listed, then the 129 names are the same set as before.
- Given the tree, when `ctest --test-dir build/test -j` runs, then every test passes.
- Given each Sudoku and Battleship `.lua`, when searched, then `local function eq` appears once per folder (and `withCanvas` once in Battleship), and `record` is defined once in Sudoku's folder.
- Given the two `trace.lua` files, when compared, then they are byte-identical.
- Given `GamesCheckEngineTest.cpp`, when searched, then no heading names a ticket or epic, and `grep -c '^TEST_F'` is 68.

## Implementation Notes

- Implemented by the step-03 subagent, then three review patches by the same subagent.
- Sudoku: `basics.lua` holds `eq` and `record` (verbatim from `drawn.lua`); `pins.lua`, `drawn.lua`, `canvases.lua`, `checks.lua` use it; `rules.lua`, `interaction.lua`, `marks.lua` require it before `pins`. Battleship: `basics.lua` holds `eq` and `withCanvas`. Both `trace.lua` files are byte-identical (`cmp`).
- `GamesCheckEngineTest.cpp` is a pure permutation: the sorted line multiset differs from the base only by the headings and one added comment (`The two canvases a check runs on.`). 68 `TEST_F`, the same sorted names; 129 `games-check` ctest names, the same set.
- Left alone on purpose: `sudoku/canvases.lua`'s `at_canvas` (no second copy in the Sudoku folder; Battleship's `withCanvas` is another game's), UTTT's `eq` and `withCanvas` (one copy each in its folder), and the other ticket-style headings of the test file (C2, D2, row 4 to 6), which the intent does not name; the plan's "no heading names a ticket or epic" acceptance line is read as the e8-xr block's heading.

## Plan Change Log

## Review Triage Log

### 2026-10-11 — Review pass 1 (blind-hunter, edge-case-hunter, verification-gap, intent-alignment)
- verdicts: 18 findings — high 0, medium 0, low 14, false 4, maybe-false 0
- findings:
  - `[low]` `reject` blind: Sudoku's `at_canvas` is the same code as Battleship's `withCanvas` — they are in two games' folders, and nothing may cross games (R2); inside Sudoku's folder there is one copy.
  - `[low]` `reject` blind: Battleship's `eq` fails on a nil `what` — unchanged from both removed copies; changing the message is not behaviour-preserving and no call omits `what`.
  - `[low]` `patch` blind, verification-gap: the bare `require("basics")` in `rules.lua`, `interaction.lua`, `marks.lua` looks like a dead import — a comment on each line, applied.
  - `[low]` `reject` blind: no test pins that `canvases.lua` and `checks.lua` load no pin module — the real-game rounds run in their VMs (129 pass); a new enforcement test is new scope.
  - `[low]` `reject` blind: the `trace.lua` header is vaguer — it had to be game-neutral to be identical; the README section it points to exists.
  - `[low]` `patch` blind, verification-gap: long lines in the `drawn.lua` header and README paragraph — re-wrapped, applied.
  - `[low]` `patch` blind: README does not mention Battleship's `basics.lua` — one sentence added.
  - `[low]` `reject` blind: `record`'s positional `nil` — `drawn.lua` and `marks.lua` already call it so; a wrapper or options table adds API for no named harm.
  - `[false]` `reject` blind: the C++ regroup is unrelated churn — G2 asks for it.
  - `[false]` `reject` blind: the heap-margin tests sit after an unrelated test — they are the end of the limits section, whose last test is the one with "4 of 5 checks passed"; section order is the heading's.
  - `[false]` `reject` edge: the `pins.lua` header's "none loads nested" is false — `taps.lua`, which every round loads first, has already loaded `grid`, `layout` and `solver`; the claim and its premise are the base's, and `basics` was the only new module.
  - `[low]` `reject` edge: headings (C2), (D2), (row 6), (rows 4 and 5) and one "row 12" comment name tickets or rows — the intent names only the e8-xr block, and rows are the cross-story review's labels.
  - `[low]` `reject` edge: `record`'s third-position `on_call` is unguarded against an old two-argument call — all three call sites converted and the rounds fail loudly on an empty digest (the digest pins count commands >= 5).
  - `[false]` `reject` verification-gap: no gaps found (its two other notes are the long lines, patched, and the unenforced order, covered by the comments).
  - `[low]` `reject` intent-alignment: D3 satisfied through `basics.lua`, not `pins.lua` — descriptive; `pins.lua` sets the collector mode and loads `solver` when loaded, which `canvases.lua`/`checks.lua` must not take (Design Notes); no observable difference.
  - `[low]` `reject` intent-alignment: the `trace.lua` headers are a third text — descriptive; identical text was the ask.
  - `[low]` `reject` intent-alignment: Battleship's `eq` also deduplicated — D3 names duplicated helpers in each folder; same class as `withCanvas`.
  - `[low]` `reject` intent-alignment: no new test for D1/D3 — the real companions' 129 `games-check` tests exercise them.

## Design Notes

- **Why a `basics.lua`, not `pins.lua`.** D3 says `pins.lua` already exports `eq`. But `pins.lua` sets `collectgarbage("generational")` when it loads and loads `solver`. `canvases.lua` was kept clear of the pin modules (README, "The split of Sudoku's checks and pins") and `checks.lua`'s `deep` relies on a collection leaving the stack its size, which a switch of collector mode could change. So `eq` and `record` live in a module with no side effects, `pins.lua` re-exports `eq` from it (`pins.eq` is unchanged for every pin module), and `canvases.lua` and `checks.lua` require it directly. The heavy `drawn.lua` is not a home for `record` either: `canvases.lua` would load it, `pins`, and the generational switch.
- **Why a module per folder, not one for all.** R2 and the 2026-10-04 Decision: no file or `require` crosses games. Each game's `basics.lua` is its own copy of what it needs; Sudoku's and Battleship's `eq` differ in text (`what or ""`), as they did.
- **Guards in the moved functions.** `record` (from `drawn.lua`) keeps all of its guards: it skips the count for `refresh`; it calls `on_image` only for `image` and `on_call` for every call, refresh included; it restores `ch.gfx` after `pcall` and re-raises with `error(err, 0)`; it takes the names from the real `ch.gfx`. `canvases.lua`'s own `record` had the same restore and the same key loop, and no count or image hook; the digest passes `nil` for `on_image` and ignores the returned count. `withCanvas` restores the screen size after `pcall` and re-raises with `error(err, 0)`. `eq` raises at level 2, so the failure names the caller; `basics.lua` keeps the level, as a call through a function value does not add a frame. `git log -L` on the moved ranges shows one targeted fix, `4fdc3176`'s `refresh` guard in `drawn.lua`'s `record` (a refresh is not counted), which is why that copy is the one kept; the other copies (`canvases.lua` `record`, both `withCanvas`, every `eq`) only moved or were added whole (`3fe30c4f`, `5cccf9cb`, `9c56ee81`).
- **Load nesting.** `drawn`, `rules`, `interaction` and `marks` each `require("basics")` before `pins`, so `pins`, loaded from them, finds `basics` loaded and nests nothing. `basics.lua` requires nothing.
- **Why the same file for G2.** The file's fixture class and helpers are shared by every test; a split would copy them into a header for no change to the tests. The brief allows a regroup in place.
- **Header text.** The two `trace.lua` headers differed only in where the count is pinned (`draws.recorder` against a check named in UTTT's `checks.lua`). The shared text points at the README's "Observing draw commands" instead, which is true for both and names no game's check.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake --build build/test && ctest --test-dir build/test -j --output-on-failure'` -- expected: all pass.
- `ctest --test-dir build/test -L games-check -N | sort` before and after -- expected: the same 129 names (diff of the two lists is empty).
- `cmp test/game_script/first_party/battleship/trace.lua test/game_script/first_party/ultimate-tic-tac-toe/trace.lua` -- expected: identical.
- `grep -c '^TEST_F' test/game_script/harness/games_check/GamesCheckEngineTest.cpp` -- expected: 68; a sorted list of the TEST_F names before and after is equal.
- `python3 scripts/check_upstream_touches.py`, then `./bin/clang-format-fix` twice -- expected: pass, nothing new.
- No firmware code changes: `pio run`, `pio check` and the simulator build are not run.

## Auto Run Result

Status: built. Route full; review thorough (four lenses). 18 findings: 3 low patched (comments on three `require("basics")` lines, re-wrap, a README sentence), 4 false, 11 low rejected, none deferred.

**Summary.** One `eq` and one recorder in Sudoku's companion folder (`basics.lua`), one `eq` and one `withCanvas` in Battleship's, identical `trace.lua` headers in UTTT's and Battleship's, and the e8-xr tests of `GamesCheckEngineTest.cpp` moved into their behaviour sections. No test changed, nothing crosses games, nothing under `games/` or firmware changed.

**Files.**
- `test/game_script/first_party/sudoku/basics.lua` (new): `eq` and `record`.
- `.../sudoku/{pins,drawn,canvases,checks,rules,interaction,marks}.lua`: use or preload `basics`.
- `.../battleship/basics.lua` (new): `eq` and `withCanvas`; `.../battleship/{checks,draws}.lua` use it.
- `.../battleship/trace.lua`, `.../ultimate-tic-tac-toe/trace.lua`: one header text.
- `test/game_script/first_party/README.md`: recorders and module list.
- `test/game_script/harness/games_check/GamesCheckEngineTest.cpp`: regrouped, no e8-xr heading; sections renamed "outcomes and the end frame", "the heap margin and the check VMs' limits (rows 4 and 5)", new "Continue: the restore probe (row 6)".
- This plan.

**Verification (on the tree before the commit).**
- `cmake --build build/test && ctest --test-dir build/test -j8`: 2052 of 2052 pass; `ctest -L games-check`: 129 tests.
- Sorted `ctest -L games-check -N` names before (at 507f10d1) and after: identical (`diff` empty). Sorted `TEST_F` list: 68, identical. The sorted line multiset of `GamesCheckEngineTest.cpp` differs from the base only in headings and one comment.
- `cmp` of the two `trace.lua`: identical. `grep` finds `local function eq` once per folder with a `basics.lua`, `record` defined once in Sudoku's.
- `python3 scripts/check_upstream_touches.py`: PASS. `./bin/clang-format-fix` twice: no change, `git status` shows only the intended files.
- No firmware code changed, so `pio run`, `pio check` and the simulator build were not run. No gate or workflow changed, so no fresh-tree run.
- No memory, flash or timing figure is claimed; the heap and instruction limits are held by the rounds and checks passing in their VMs, unmeasured beyond that.

**Residual risks.** The `basics`-before-`pins` order is a convention, now commented, not enforced by a test. The `GamesCheckEngineTest.cpp` diff is large (moved blocks) and reads best with `git diff --color-moved`. Formatting-only changes outside my paths: none.

**Follow-up review recommended:** false (no high, no medium patched).

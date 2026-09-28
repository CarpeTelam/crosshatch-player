---
title: 'Close the COMDAT static escape, stop the superseded-commit red, and let a release ship with a later preview'
type: 'bugfix'
ticket: ''
created: '2026-09-28'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '69c04796a1248bd05e494b0de02c93996be657fb'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Three epic-script-runtime retro follow-ups (AI-9/F2, AI-7/O9, owner's F4 decision). (1) `check_flash_budget.py objects` skips every COMDAT symbol before its `_ZGV` guard test and its >64 B mutable-static test, so a game's inline-function static, class-template static member, and function-template static escape AD-2. (2) `Crosshatch Test Status` fails on `cancelled`, and `cancel-in-progress` cancels a superseded commit's run, so that commit shows a red required check. (3) `fork_release.py preflight` refuses every release with `API_LEVEL_FROZEN` false after the freezing release, so a release is blocked while a later level is an open preview; the owner decided a release needs only the levels below the preview frozen.

**Approach:** (1) Test `_ZGV*` before the COMDAT skip; count a COMDAT symbol when its outermost mangled name is one the game objects themselves define (global, non-COMDAT); add sidecar tests for each escape. (2) Keep `if: always()`; in the fail step, excuse a cancel only when the run's PR head SHA is no longer the PR's current head (`gh api .../pulls/N --jq .head.sha`, `pull-requests: read`). (3) The preflight requires the commit's highest frozen level (`frozen_top`) to be at least every `-ch.N` tag's; the notes name the frozen levels below a preview.

## Boundaries & Constraints

**Always:** fork-only files; standard library and git only in scripts (`docs/crosshatch/fork-scripts.md`); fail closed (a failed `gh api` fails the status step; an unparsable header at a tag stays a SetupError); `failure` in any needed job stays fatal; `cancelled` stays fatal for the PR's current head.

**Never:** `!cancelled()` (a skipped required check counts as passing); edit `ci.yml`, the spine, or `ApiLevel.h`; count upstream/library COMDAT data (e.g. an upstream inline singleton's storage) as game statics; weaken the non-COMDAT rules.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Inline function static in a game namespace | COMDAT `GameCore::instance()::big` 4,096 B + guard; a game object defines a global `GameCore` symbol | `objects` exits 1 naming the guard and the static | none |
| Class-template static member | COMDAT `GameCore::Pool<int>::storage` 1,024 B | exits 1 | none |
| Function-template static | COMDAT `GameCore::scratch<int>()::buf` 256 B | exits 1 | none |
| Upstream COMDAT data | COMDAT `get()::instance` 216 B, no game object defines `get` | exits 0 | none |
| COMDAT guard of any name | `_ZGVZ3getvE8instance` in a group | exits 1 | none |
| Compiler clone of SDK code | LOCAL `freeink::...$isra$0` in a game object | `freeink` is not a game name | none |
| Cancelled job, superseded commit | run head SHA != PR head | status step exits 0 with a notice | none |
| Cancelled job, current head | SHAs equal | exits 1 | `gh api` failure or empty output: exits 1 |
| Failed job | any | exits 1 without calling `gh` | none |
| Release while a later level is a preview | tag froze level 1; commit level 2 preview | preflight passes | none |
| Reopened or lowered frozen level | tag froze level N; commit frozen_top < N (preview N, no header, frozen N-1) | preflight fails (dry run warns) | malformed header at a tag: exit 2 |
| Notes of a preview above frozen levels | level 2 preview, min 1 | "Game API 1 is frozen: it no longer changes." after the preview sentence | none |

</frozen-after-approval>

## Code Map

- `scripts/check_flash_budget.py` -- `object_problems` (COMDAT skip at the old :336 came before the `_ZGV` test); `inspect_object` now returns the parse; `check_objects` parses all game objects first, derives `game_names`, then checks each. New `outer_name` (Itanium first source-name via `MANGLED_OUTER`), `defined_symbol_section`, `Symbol.bind`.
- `scripts/check_flash_budget_test.py` -- `readelf_output` fixture writer (now an optional binding), `ParseTest` (real tool output), `ObjectsTest.game()` fixtures; the old `test_comdat_statics_are_not_the_objects_own` encoded the escape for guards and is split.
- `.github/workflows/crosshatch-ci.yml` -- `crosshatch-test-status` job; workflow-level `permissions: contents: read`; `concurrency.cancel-in-progress: true`.
- `scripts/fork_release.py` -- `freeze_problems` (preflight), `render_notes`, module docstring's preflight and notes lines.
- `scripts/check_api_freeze.py` -- already had `frozen_top` (highest frozen level = API_LEVEL if frozen else API_LEVEL-1); moved to `fork_common` since two scripts now need it (fork-scripts.md rule).
- `lib/GameCore/ApiLevel.h`, spine AD-19 -- read only: `API_LEVEL_FROZEN` describes `API_LEVEL`; every lower level is frozen.
- `docs/crosshatch/` -- no page states the 2.3 "COMDAT statics are still caught" claim; only the script docstrings did.

## Tasks & Acceptance

**Execution:**
- [x] `scripts/check_flash_budget.py` -- guard test before the COMDAT skip; COMDAT symbols count when `outer_name` is in the game names (global, non-COMDAT FUNC/OBJECT/COMMON/TLS definitions of the game objects); docstrings corrected -- closes F2.
- [x] `scripts/check_flash_budget_test.py` -- real xtensa readelf of the three escapes; `outer_name`, `game_names`, and one `objects` test per escape, the upstream case, and the COMDAT guard.
- [x] `.github/workflows/crosshatch-ci.yml` -- `permissions: pull-requests: read` on the status job; the fail step excuses a cancel on a superseded head only.
- [x] `scripts/fork_common.py`, `scripts/fork_common_test.py`, `scripts/check_api_freeze.py`, `scripts/check_api_freeze_test.py`, `docs/crosshatch/fork-scripts.md` -- move `frozen_top` to `fork_common` with its test and doc row.
- [x] `scripts/fork_release.py`, `scripts/fork_release_test.py` -- new freeze rule and messages; notes name the frozen levels below a preview; tests for each matrix row.

**Acceptance Criteria:**
- Given the real games-on x4pro build of this commit, when `objects` runs, then it exits 0 (no false positive from the new rule).
- Given every `scripts/*_test.py`, when run as CI's step does, then each exits 0.

## Implementation Notes

- Implemented directly in this session (no coding subagent available); the COMDAT rule was designed against the real games-on build's objects: 35 game objects, no COMDAT guard, 16 COMDAT data symbols, all read-only (`AG`), 13 of them in game names (vtables, `inline constexpr` name tables; the rest are `ForkRelease::LATEST_RELEASE_URL` and two `std::variant` tables). A first cut counted local symbols too and made `freeink` a game name through a compiler clone `freeink::ui::optionDialog<24>...$isra$0` in `GameMatchActivity.cpp.o`, and `C$0$0` constants; hence the GLOBAL-only rule.
- Guards: all COMDAT guards now fail, as AI-9 says, including an upstream inline singleton's guard in a game object (none exist today). Consequence: game code calling an upstream header's function-local-static singleton fails the gate.
- Known limit, in the docstring: a game name no game object defines out of line (header-only `GameTouch`, `ForkRelease`, a free inline function at global scope) is not recognized; today those hold only `constexpr` data and inline functions without statics.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents available: each lens run in-session, one at a time, against the staged diff). Verdicts: high 0, medium 0, low 5, false 1, maybe-false 1.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | blind, edge | `outer_name` does not know `_ZGR` (a lifetime-extended reference temporary), so a mutable one in a game namespace stays skipped | low | patch | Direct fix: `G[VR]` in `MANGLED_OUTER`, plus a `test_outer_name` case. |
| 2 | edge (claims) | The plan's Implementation Notes counted 13 COMDAT data symbols, 11 in game names; the analysis printed 16 and 13 | low | patch | Corrected the note (the plan is this build's own record, not the frozen block). |
| 3 | blind | A game name no game object defines out of line (header-only `GameTouch`, `ForkRelease`, a global-scope free inline function) is not recognized, so its COMDAT mutable statics still escape | low | defer | Pre-existing escape, now narrowed; today those names hold only `constexpr` data and inline functions without statics (real-build analysis). Documented in the module docstring; `deferred-work.md` `## e2r-ai-9`. |
| 4 | blind, verification | Whether the fail step actually runs in a cancelled run (the red could be the rollup job itself cancelled, which this fix cannot turn green) | maybe-false | defer | Retro O9 observed the fail step's red; the step logic is checked by a scratch harness under `bash -e` with a stand-in `gh` (6 cases). Settle on the epic PR: push twice in quick succession and read the superseded SHA's `Crosshatch Test Status`. Recorded medium-if-true in `deferred-work.md`. |
| 5 | blind | `freeze_problems` now reads every tag's `ApiLevel.h` even for a frozen commit, so a malformed header at an old tag blocks the release (exit 2) | low | reject | `prepare` parses the released commit's header with the same parser before any tag exists, so a released tag's header always parsed; the rule needs the max over all tags. |
| 6 | blind | A game `.cpp` defining a global symbol in an upstream namespace (an explicit specialization) would make that namespace a game name | low | reject | Fails over-strict (a visible `objects` failure), never an escape; the real build's game names hold no upstream name after the GLOBAL-only rule. |
| 7 | blind | A COMDAT guard from an upstream header's inline singleton now fails a game object that calls it | false | reject | Intended by AI-9 ("test `_ZGV*` before the COMDAT skip"); AD-2 forbids dynamic initialization from game code; none in the real build; test `test_comdat_guard_variable_fails_outside_game_names_too` pins it. |
| 8 | verification | The status step's script has no committed test | low | reject | The repo has no workflow-execution harness (existing workflow tests are source-text only, which would not count); scratch harness evidence recorded in Verification. |

Intent alignment (descriptive): readings of AI-9 are (a) all guards fatal before the COMDAT skip and (b) only game-scope guards; the diff implements (a). "Demangled name in a game namespace/class" is implemented on the mangled outermost source name, with game names derived from the game objects' own global non-COMDAT definitions rather than source parsing; the divergence is row 3. AI-7 and F4 match their stated readings; F4 follows the orchestrator's rule text (below).

## Design Notes

Game names are derived from the scan's own notion of game code (the objects of `lib/Game*`, `src/games`, `src/activities/games`) rather than a hand list or source parsing: forward declarations (`class GfxRenderer;`, `namespace fui = freeink::ui;`) define nothing, so upstream names stay out, and a new game namespace or class counts as soon as a game `.cpp` defines something in it.

Release rule, as the orchestrator gave it for AD-19: "A fork release is refused only when a level that an earlier fork release shipped as frozen is no longer frozen here (API_LEVEL below it, or equal to it with API_LEVEL_FROZEN false). A release whose API_LEVEL is an open preview above every level released as frozen is allowed." Implemented equivalently as: with `frozen_top(h)` = `API_LEVEL` when `API_LEVEL_FROZEN` is true, else `API_LEVEL - 1` (0 without `ApiLevel.h`), a publishing preflight passes when `frozen_top(commit) >= max(frozen_top(tag commit))` over every `-ch.N` tag. So after release of frozen level N, a release may carry `API_LEVEL_FROZEN false` only with `API_LEVEL > N`.

## Verification

**Commands:**
- `for t in scripts/*_test.py; do python3 "$t"; done` -- expected: every file OK.
- From a fresh clone of the commit (gate rule): `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`, and the fork-script test loop -- expected: exit 0 each.
- `actionlint` (with shellcheck) on `crosshatch-ci.yml` -- expected: no findings; a scratch harness runs the status step script under `bash -e` with a stand-in `gh` for each matrix row.

**Evidence (2026-09-28):**
- Sidecar tests, worktree: check_api_freeze 9, check_flash_budget 71, check_upstream_touches 18, fork_common 27, fork_release 75, game_codec 21 tests; all OK.
- Real device objects: the games-on x4pro build of this tree, `objects` exit 0 (35 objects, no problems). With a temporary probe in `src/games/GameArena.cpp` (a `GameScript` inline-function static with a constructor, a class-template static member of 1,024 B, a function-template static of 256 B), `objects` exit 1 with 4 problems (the guard and the three statics); the baseline script (69c04796) on the same build: exit 0, "No problems". Probe reverted and rebuilt; `objects` exit 0 again.
- Fresh clone of dddbb13a (code identical to the final commit, which adds only this evidence), submodules initialised, the flash budget job's steps in order: `build on` exit 0 (5,825,664 B), `build off` exit 0 (5,675,184 B), `compare --limit-kib 250 --ram-limit-bytes 1024` exit 0 (+150,480 B flash, 105,520 B to spare; +8 B RAM), `objects` exit 0 (35 objects, no problems); the fork-script test loop exit 0 (9, 71, 18, 27, 75, 21 tests). Fresh tree deleted.
- actionlint (from pip actionlint-py) with shellcheck on `crosshatch-ci.yml`: 0 errors. Status-step harness (scratch `status_step.py`, `bash -e`, stand-in `gh`): failure on head 1, failure superseded 1, cancelled on head 1, cancelled superseded 0 with the notice, `gh` failing 1, `gh` printing nothing 1.

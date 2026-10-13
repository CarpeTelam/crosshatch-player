---
title: 'Raise the flash gate to 300 KiB'
type: 'chore'
ticket: '18'
created: '2026-10-13'
baseline_revision: 'f8bf0306bdc367da49945984251f5576427ee704'
status: built
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
warnings: []
deferred: []
---

<intent-contract>

## Intent

**Problem:** The owner's flash-budget Decision of 2026-10-13 (epic-play-nearby Notes; R15) raises the x4pro games-on minus games-off flash gate from 290 KiB (296,960 B) to 300 KiB (307,200 B), and the gate in `scripts/check_flash_budget.py` and `crosshatch-ci.yml` still says 290. Entry 11's wiring (about +298,304 B combined) passes only the new limit.

**Approach:** Change the limit in `DEFAULT_LIMIT_KIB`, in the script's docstring (the default sentence and the usage line), in the workflow job's `FLASH_BUDGET_KIB` and its comment, and in the unit tests that pin the default or sit on the limit's edge. The static-RAM limit stays 1,024 B and no other part of the gate's method changes, so the epic's base Measurement (2026-10-11, `7bfd8991`) stands.

</intent-contract>

## Implementation Notes

Oneshot: a three-file constant change with no logic, about 15 lines, the same change entry 17 made at 290 KiB (plan `story-raise-the-flash-gate-to-290-kib-plan.md`, commit `864d0fc8`, read as the model). Intent sources read: the `tickets.toml` entry 18 (description, verify, touches, stays_out); epic-play-nearby.md Notes, Decision (owner, 2026-10-13, the flash budget after entry 10's stop) and R15; `scripts/check_flash_budget.py`, its test, and the `flash-budget` job. The planning artifacts (spine, SPEC) are in `stays_out`: the orchestrator already amended them.

Files touched (all in the entry's `touches`): `scripts/check_flash_budget.py` (`DEFAULT_LIMIT_KIB` 290 to 300, the docstring's default sentence and usage line), `.github/workflows/crosshatch-ci.yml` (`FLASH_BUDGET_KIB` and its comment only), `scripts/check_flash_budget_test.py` (the default-limit test renamed and re-pinned to 307,200 B; the exact-limit and over-budget edge tests moved to 307,200 / 307,201 B; `test_workflow_limit_matches_the_script_default` already ties the workflow value to the script default). A grep of `docs/`, `AGENTS.md`, and `.github/` found no other 290 KiB / 296,960 pin outside the three files. Host-side edits run: `check_flash_budget_test.py` 91 tests OK, every `scripts/*_test.py` (14 files) OK, `check_upstream_touches.py` PASS, `./bin/clang-format-fix` twice with nothing new in `git status`.

## Plan Change Log

## Review Triage Log

### 2026-10-13 - Review pass
Lenses ran as four context-free foreground subagents, launched in one message (blind hunter, edge-case hunter, verification gap, intent alignment) over the staged diff since `baseline_revision`.
- verdicts: 7 findings - high 0, medium 0, low 3, false 4, maybe-false 0
- findings:
  - `[low]` `[reject]` (blind) The Intent calls the +298,304 B figure "entry 11's wiring"; the epic's Measurement got it from entry 10's commit plus an uncommitted scratch reference to `makeEspNowLink()` - true, but the sentence sits in the read-only intent-contract (the orchestrator's wording) and the fix edits this build's plan; the Implementation Notes cite the epic Decision for the figure.
  - `[false]` `[reject]` (blind) No measurement backs "passes only the new limit" - the `compare` runs come after the review and before the commit; their figures are in Verification Results.
  - `[false]` `[reject]` (blind) The fresh-tree gate run is only promised - it is the last Verification command, run after the review; the result is in Verification Results.
  - `[false]` `[reject]` (blind) Review metadata inconsistent (empty logs with `lenses_ran` set) - the logs are written by this step after the launch; this entry is the record.
  - `[low]` `[reject]` (blind) Derive the edge tests from `DEFAULT_LIMIT_KIB * KIB` instead of the literals 1_307_200 / 1_307_201 - the literals are what pin the edge independent of the constant (`test_default_limit_is_300_kib_in_bytes` pins the constant to 307,200 B); deriving them makes the tests self-referential, and entry 17 rejected the same ask. The "loose accounting" and `tickets.toml` points of the same lens are `[false]` `[reject]`: the fix edits this plan, and the ticket file is the orchestrator's to update (never written by a build).
  - (edge-case) No findings.
  - (verification-gap) No gaps found: the constant, the workflow tie, and both edge sides are pinned by tests.
  - `[low]` `[reject]` (intent) No committed test runs `main()`'s default-limit branch or `--limit-bytes` below the difference - not caused by this change (the argparse wiring is untouched) and a new CLI test is more than a direct correction; the Verification `compare` runs on a real pair exercise exactly those CLI paths.
- routes: no intent_gap, bad_plan, patch, or defer entries. (The blind hunter's six bullets are counted as six findings: three of them rows above, with the last bullet's two parts sharing one row; the intent auditor's one divergence note is the seventh.)

## Verification

**Commands:**
- `python3 scripts/check_flash_budget_test.py` -- expected: passes, default-limit test at 300 KiB = 307,200 B, edge tests at 307,200 / 307,201 B
- every `python3 scripts/*_test.py` -- expected: pass
- `python3 scripts/check_upstream_touches.py` -- expected: pass
- `./bin/clang-format-fix` twice -- expected: second run changes nothing
- `check_flash_budget.py compare` on a games-on and games-off pair -- expected: passes at the new default; fails with `--limit-bytes` below the measured difference
- the flash-budget job's commands (`build on`, `build off`, `compare --limit-kib "$FLASH_BUDGET_KIB" --ram-limit-bytes "$RAM_BUDGET_BYTES"`, `objects`, `touch-line`) once from a fresh tree of the committed work -- expected: all exit 0

**Results (2026-10-13):**
- Host side, before the review: `check_flash_budget_test.py` 91 tests OK; every `scripts/*_test.py` (14 files) OK; `check_upstream_touches.py` PASS (no upstream file touched); `./bin/clang-format-fix` twice, nothing new in `git status`. The review changed no code, so these stand.
- Fresh-tree gate run. Tree kind: a `git archive` of the staged work's tree object `854cbb6287b17f3350d97bc66373ac36dab8eb0b` (written with `git write-tree` before the commit; it differs from the commit's tree only in this plan file's Results and Auto Run Result text) plus `git submodule foreach --recursive` archives of `freeink-sdk` (`32a265fa`) and its nested `lucide` (`c81680e0`), extracted into an empty directory with no `.git`, no `.pio`, and no `platformio.local.ini` (CI's footing); the machine's warm `~/.platformio` and the shared build cache `/home/user/crosshatch-player/.cache` reused, so some objects came from that cache; every command under `/tmp/crosshatch-build.lock`. `pio pkg install -e x4pro`, then the job's commands in its order:
  - `check_flash_budget.py build on` exit 0; `build off` exit 0.
  - `compare --limit-kib 300 --ram-limit-bytes 1024` (the job's command) exit 0: `firmware.bin` 6,244,176 B games on, 5,957,328 B off, **+286,848 B** (280.1 KiB), limit 307,200 B, within budget with 20,352 B to spare; static internal RAM 190,248 B on, 189,464 B off, **+784 B**, limit 1,024 B, 240 B to spare.
  - `compare` with no limit flag (the new default) exit 0. `--limit-bytes 200000` exit 1 ("Over budget by 86,848 B"); `--limit-bytes 286847` exit 1 ("Over budget by 1 B"); `--limit-bytes 286848` exit 0 (0 B to spare): the pass/fail edge sits at the measured difference.
  - `objects` exit 0 (no problems); `touch-line` exit 0 (no problems).
- Measurement method: same-tree on/off pair in one fresh tree, the gate's own method, so the epic's base Measurement stands. The +286,848 B / +784 B equals the orchestrator's measurement of the combined tree at entry 10 as committed (epic Notes, 2026-10-13), so the tree adds nothing to that. The difference is under the old 290 KiB limit too, so the new default is shown by the 307,200 B Limit row, the default-limit test, and the exact-limit and over-budget tests; entry 11's +298,304 B would pass only 300 KiB (that figure is the epic's, not re-measured here).
- No `pio run`, `pio check`, or `sim.sh build` ran for the worktree: the diff changes no C/C++ and no `platformio.ini`, only a Python constant, its tests, and a workflow env value; the fresh-tree run above is the firmware build for this story.

## Auto Run Result

**Summary:** The x4pro flash gate limit rises from 290 KiB (296,960 B) to 300 KiB (307,200 B), carrying the owner's flash-budget Decision of 2026-10-13 (R15) into the script and the CI job. The static-RAM limit stays 1,024 B and no other part of the gate changed.

**Files:**
- `scripts/check_flash_budget.py` - `DEFAULT_LIMIT_KIB` 300; docstring default sentence and usage line.
- `scripts/check_flash_budget_test.py` - default-limit test renamed and re-pinned to 307,200 B; exact-limit and over-budget edge tests at 307,200 / 307,201 B.
- `.github/workflows/crosshatch-ci.yml` - `FLASH_BUDGET_KIB: 300` and its comment (the flash-budget job's limit and comment only).
- This plan.

**Review breakdown:** four lenses ran (blind hunter, edge-case hunter, verification gap, intent alignment), 7 findings: 0 patched, 0 deferred, 7 rejected - four false (the `compare` and fresh-tree runs and the logs, all done after the launch) and three low (a plan-wording point in the read-only intent and plan accounting, edge tests that should derive from the constant, and an untested `main()` default branch). Reasons are in the Review Triage Log. `followup_review_recommended` is false (no patches).

**Verification:** see Results above. Screenshots: none (no screen changed).

**Formatting:** `./bin/clang-format-fix` made no change anywhere.

**Residual risks:** none new. Entry 11's projected +298,304 B passes only this limit and is the epic's figure, not measured here; its own build measures like for like.

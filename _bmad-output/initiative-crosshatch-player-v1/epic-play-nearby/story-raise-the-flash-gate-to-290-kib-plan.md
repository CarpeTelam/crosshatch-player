---
title: 'Raise the flash gate to 290 KiB'
type: 'chore'
ticket: '17'
created: '2026-10-12'
baseline_revision: 'd3cef13dd5614bacb123867f8fdd868188d967eb'
status: 'built'
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

**Problem:** The owner's flash-budget Decision of 2026-10-12 (epic-play-nearby Notes; R15) raises the x4pro games-on minus games-off flash gate from 270 KiB (276,480 B) to 290 KiB (296,960 B), and the gate in `scripts/check_flash_budget.py` and `crosshatch-ci.yml` still says 270.

**Approach:** Change the limit in `DEFAULT_LIMIT_KIB`, in the script's docstring (the default sentence and the usage line), in the workflow job's `FLASH_BUDGET_KIB` and its comment, and in the unit tests that pin the default or sit on the limit's edge. The static-RAM limit stays 1,024 B and no other part of the gate's method changes, so the epic's base Measurement (+258,048 B flash, +784 B static RAM at `c28120a7`) stands.

</intent-contract>

## Implementation Notes

Oneshot: a three-file constant change with no logic, about 15 lines. Intent sources read: `tickets.toml` entry 17 (description, verify, touches, stays_out); epic-play-nearby.md Notes, Decision (owner, 2026-10-12, the flash budget, after entry 5's stop), and R15; the spine's Flash budget row (amended 2026-10-12, already carries 290 KiB); `scripts/check_flash_budget.py`, its test, and the `flash-budget` job. Entries 1 to 5 plans were not needed beyond entry 5's measurement of the driver, which the Decision quotes.

Files touched (all in the entry's `touches`): `scripts/check_flash_budget.py` (`DEFAULT_LIMIT_KIB` 270 to 290, the docstring's default sentence and usage line), `.github/workflows/crosshatch-ci.yml` (`FLASH_BUDGET_KIB` and its comment only), `scripts/check_flash_budget_test.py` (the default-limit test renamed and re-pinned to 296,960 B, and the exact-limit and over-budget edge tests moved to 296,960 / 296,961 B; `test_workflow_limit_matches_the_script_default` already ties the workflow value to the script default). A grep of `docs/`, `AGENTS.md`, and `.github/` found no other 270 KiB / 276,480 pin. Host-side edits run: `check_flash_budget_test.py` 91 tests OK, every `scripts/*_test.py` OK, `check_upstream_touches.py` PASS, `./bin/clang-format-fix` twice with nothing new in `git status`.

## Plan Change Log

## Review Triage Log

### 2026-10-12 - Review pass
Lenses ran as four context-free foreground subagents, launched in one message (blind hunter, edge-case hunter, verification gap, intent alignment) over the diff of the staged work since `baseline_revision`.
- verdicts: 8 findings - high 0, medium 0, low 3, false 5, maybe-false 0
- findings:
  - `[false]` `[reject]` (blind) Plan front matter says `in-review` with `lenses_ran: []` - the lenses were running when it read the plan; `lenses_ran` is written by this step after the launch, and the front matter now holds the four ids.
  - `[false]` `[reject]` (blind) The plan never records the headroom the new limit leaves - a fix would only edit this build's plan, and the figure follows from the Decision (296,960 B minus the base +258,048 B is 38,912 B); the epic file already carries the share arithmetic.
  - `[false]` `[reject]` (blind) The plan understates what the story touches (it adds a plan file; historical 270 KiB text remains) - the plan names every file it changes (the plan file is the ticket's own deliverable), and a grep of `scripts/`, `.github/`, `docs/`, and `AGENTS.md` finds no live 270 KiB or 276,480 pin; the rest are dated historical records in `_bmad-output/` that the Decision's supersede markers cover. A fix would edit this plan.
  - `[false]` `[reject]` (blind) The plan reports no fresh-tree run of the gate - the run is the last Verification command and happens after the review and before the commit's results are recorded; the result is in Verification below.
  - `[low]` `[reject]` (blind) Add a test with a 276,481 B difference that passes at the default - `test_exact_limit_passes` at 296,960 B already fails under a 270 KiB default (276,480 B limit), so the new case would pin nothing more; adding a test is more than a direct correction for a defect nobody would meet.
  - `[low]` `[reject]` (blind) The workflow comment "set it below the measured difference" could quote the current difference - cosmetic; the figure changes every merge and a stale number would mislead.
  - (edge-case) No findings.
  - (verification-gap) No gaps found: every changed number is pinned by a test (`test_default_limit_is_290_kib_in_bytes`, `test_workflow_limit_matches_the_script_default`, the edge tests).
  - `[low]` `[reject]` (intent) The edge tests take their limit from `DEFAULT_LIMIT_KIB`, so they are self-referential - true, but `test_default_limit_is_290_kib_in_bytes` pins the constant to the literal 296,960 B, so a wrong constant fails there.
  - `[false]` `[reject]` (intent) The diff has no evidence of a real `compare` run or a fresh-tree run - those are Verification commands run after the review; their results are recorded below.
- routes: no intent_gap, bad_plan, patch, or defer entries.


## Verification

**Commands:**
- `python3 scripts/check_flash_budget_test.py` -- expected: passes, with the default-limit test at 290 KiB = 296,960 B and the edge tests at 296,960 / 296,961 B
- every `python3 scripts/*_test.py` -- expected: pass
- `python3 scripts/check_upstream_touches.py` -- expected: pass
- `./bin/clang-format-fix` twice -- expected: second run changes nothing
- `check_flash_budget.py compare` on a games-on and games-off pair -- expected: passes at the new default; fails with `--limit-bytes` below the measured difference
- the flash-budget job's commands (`build on`, `build off`, `compare --limit-kib "$FLASH_BUDGET_KIB" --ram-limit-bytes "$RAM_BUDGET_BYTES"`, `objects`, `touch-line`) once from a fresh tree of the committed work -- expected: all exit 0

**Results (2026-10-12):**
- Host side, before the review: `check_flash_budget_test.py` 91 tests OK; every `scripts/*_test.py` OK (14 files); `check_upstream_touches.py` PASS (no upstream file touched); `./bin/clang-format-fix` twice, nothing new in `git status`. The review changed no code, so these stand.
- Fresh-tree gate run. Tree kind: a `git archive` of the work's tree object `a7bd9ef12cb74ecd93070ceb580abcd92238ab71` (the staged files, written with `git write-tree` just before the commit) plus `git submodule foreach --recursive` archives of `freeink-sdk` (`32a265fa`) and its nested `lucide` (`c81680e0`), extracted into an empty directory with no `.git` and no `.pio` and no `platformio.local.ini` (CI's footing), the machine's warm `~/.platformio` and the shared build cache `/home/user/crosshatch-player/.cache` reused; every command under `/tmp/crosshatch-build.lock`. The commit's tree differs from that tree object only in this plan file (checked with `git diff --stat`, below). `pio pkg install -e x4pro`, then the job's commands in its order:
  - `check_flash_budget.py build on` exit 0; `build off` exit 0.
  - `compare --limit-kib 290 --ram-limit-bytes 1024` (the job's command) exit 0: `firmware.bin` 6,213,808 B games on, 5,955,312 B off, **+258,496 B** (252.4 KiB), limit 296,960 B, within budget with 38,464 B to spare; static internal RAM 190,248 B on, 189,464 B off, **+784 B**, limit 1,024 B, 240 B to spare.
  - `compare` with no limit flag (the new default) exit 0, same figures. `compare --limit-bytes 200000` (below the difference) exit 1, "Over budget by 58,496 B". `compare --limit-kib 270` exit 0, 17,984 B to spare: the difference of this tree is under the old gate too, so the new limit is shown by the 296,960 B Limit row, the default test, and the exact-limit and over-budget tests.
  - `objects` exit 0 (56 game objects, no problems); `touch-line` exit 0.
- Measurement method for the figures above: same-tree on/off pair (`build on`, `build off`, `compare`) in one fresh tree, the gate's own method, so the base Measurement stands unchanged. This tree's +258,496 B flash and +784 B static RAM equal the orchestrator's measurement after entries 1, 2, 3, and 12 (+258,496 B / +784 B at `244f3399`), so entry 5's merged adapter adds nothing to the x4pro image yet: nothing links `EspNowLink` into firmware before entry 11, and the +269,952 B the prompt quotes for the combined tree is not what this lane's tree measures (reported to the orchestrator; unexplained, not attributed).
- No `pio run`, `pio check`, or `sim.sh build` ran for the worktree: the diff changes no C/C++ and no `platformio.ini`, only a Python constant, its tests, and a workflow env value; the fresh-tree run above is the firmware build for this story.

## Auto Run Result

**Summary:** The x4pro flash gate limit rises from 270 KiB (276,480 B) to 290 KiB (296,960 B), carrying the owner's flash-budget Decision of 2026-10-12 (R15) into the script and the CI job. The static-RAM limit stays 1,024 B and no other part of the gate changed.

**Files:**
- `scripts/check_flash_budget.py` - `DEFAULT_LIMIT_KIB` 290; docstring default sentence and usage line.
- `scripts/check_flash_budget_test.py` - default-limit test renamed and re-pinned to 296,960 B; exact-limit and over-budget edge tests at 296,960 / 296,961 B.
- `.github/workflows/crosshatch-ci.yml` - `FLASH_BUDGET_KIB: 290` and its comment (the flash-budget job's limit and comment only).
- This plan.

**Review breakdown:** four lenses ran (blind hunter, edge-case hunter, verification gap, intent alignment), 8 findings: 0 patched, 0 deferred, 8 rejected - five false (plan front matter read mid-review; two asks that only edit this plan; the fresh-tree run, done; the `270` greps, nothing live left) and three low rejected (a redundant 276,481 B test, a comment that would quote a figure that changes every merge, and the edge tests deriving their limit from the constant, which another test pins to the literal 296,960). Reasons are in the Review Triage Log.

**Verification:** see Verification Results above. Screenshots: none (no screen changed).

**Formatting:** `./bin/clang-format-fix` made no change anywhere.

**Residual risks:** the combined-tree flash figure the prompt quotes (+269,952 B) does not match this lane's measured +258,496 B; the orchestrator's later measurement on the merged tree is the one that matters for the gate. `followup_review_recommended` is false (no patches).

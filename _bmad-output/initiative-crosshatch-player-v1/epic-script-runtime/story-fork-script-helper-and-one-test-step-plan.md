---
title: 'Fork script helper and one test step'
type: 'refactor'
ticket: '2'
created: '2026-09-27'
status: 'built'
baseline_revision: '799d5add2829300d2fe7368a5b3ba63790a67808'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The three fork scripts each define `SetupError`, the 0/1/2 exit contract, a git helper, the games-flag literal, and a step-summary writer (retro A1, A4), and their sidecar tests run as three separate steps in two jobs. Entries 3, 5, 6, and 17 add fork scripts and must copy one pattern (spine Consistency Conventions, Fork scripts; retro AI-5).

**Approach:** Add `scripts/fork_common.py` with a small documented API and its own sidecar test; make the three scripts use it with unchanged exit codes; replace the per-script test steps with one `Fork script tests` job that runs every `scripts/*_test.py`, listed in `Crosshatch Test Status` `needs`; write the conventions in `docs/crosshatch/fork-scripts.md`.

## Boundaries & Constraints

**Always:** Standard library only (Python 3.11+). Every existing `scripts/*_test.py` passes unchanged. Each script keeps its CLI, output text for rule results, and exit codes. `fork_common.py` and its test are fork-only Game paths in the ledger. Each test file runs in its own process in CI.

**Never:** Edit upstream's `ci.yml`, `crosshatch-release.yml`'s job layout, or any upstream script. No composite action for the PlatformIO setup (AI-5 optional part stays out). No new behaviour in the three scripts beyond the git-missing case below.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| step passes | step returns `None` or `0` | exit 0 | none |
| rule broken | step returns `1` or raises `Failure` | exit 1, `error: <msg>` on stderr for `Failure` | none |
| could not run | step raises `SetupError` | exit 2, `error: <msg>` on stderr; with a summary heading, `## <heading>` + "The check could not run: <msg>" appended to the step summary | none |
| git exit code outside `ok_codes` | e.g. unknown ref | `SetupError("git <args> failed (<code>): <stderr>")` | exit 2 |
| git not on PATH | `OSError` | `SetupError("cannot run git: ...")` | exit 2 |
| no `GITHUB_STEP_SUMMARY` | env unset, no path | summary write is a no-op | none |
| one test file fails | any `scripts/*_test.py` exits non-zero | the other files still run; the step fails | none |

</frozen-after-approval>

## Code Map

- `scripts/check_upstream_touches.py` -- `SetupError` (l.30), `git`/`git_text` (l.34-44, bytes, raises), handler in `main` (l.188-192). Keep `nul_list`, `resolve`, `sys.stdout.reconfigure`.
- `scripts/check_flash_budget.py` -- `FLAG`/`UNFLAG` (l.41-44), `SetupError` (l.50), summary writes in `compare` (l.166-168) and `main` (l.195-200). Its `pio` helper stays (only this script and `fr build` run pio; different shapes).
- `scripts/fork_release.py` -- `GAMES_FLAG` (l.55), `Failure`/`SetupError` (l.80-85), `git(repo_dir, ...)` returning `CompletedProcess` text (l.161-165), used at l.183 (`merge-base --is-ancestor`, codes 0/1), l.296 and l.435 (`rev-parse`); summary in `notes` (l.649-652); handler in `main` (l.731-741), which also maps `OSError`/`KeyError`/`TypeError` to 2. `write_outputs` (GITHUB_OUTPUT) stays local.
- `scripts/*_test.py` -- reference `cut.SetupError`, `cut.SDK_PATH`, `cfb.SetupError`, `cfb.UNFLAG`, `fr.GAMES_FLAG`, `fr.Failure`, `fr.SetupError`; keep those names as module attributes so the tests stay byte-identical. Exit codes are pinned: cut CLI 0/1/2 (11 cases), cfb CLI and `compare` 0/1/2, fr `main` 0/1/2 across prepare, check-images, pack-games, preflight, recheck, notes, expected-assets.
- `.github/workflows/crosshatch-ci.yml` -- test steps at l.46-53 (ledger job) and l.79-82 (flash budget job); header comment l.1-15; `crosshatch-test-status.needs` l.182-185.
- `.github/workflows/crosshatch-release.yml` -- runs `tools/scripts/fork_release*.py`; the publish job's sparse checkout includes all of `scripts`, so `fork_common.py` is present. Leave it unchanged.
- `docs/crosshatch/upstream-touches.md` -- Game paths list (fork-only, may be edited).

## Tasks & Acceptance

**Execution:**
- [x] `scripts/fork_common.py` -- create: `PASS`/`FAIL`/`COULD_NOT_RUN`, `Failure`, `SetupError`, `GAMES_MACRO`, `GAMES_BUILD_FLAG`, `git(*args, cwd=None, ok_codes=(0,))` → `(code, stdout bytes)`, `git_text(...)`, `write_step_summary(text, path=None)`, `exit_code(step, summary_heading=None)`; module docstring documents each and how to import it.
- [x] `scripts/fork_common_test.py` -- create: tests for every matrix row plus `cwd` and `ok_codes`.
- [x] `scripts/check_upstream_touches.py` -- use `fork_common.SetupError`, `git`, `git_text`, `exit_code`; keep the `SetupError` name bound.
- [x] `scripts/check_flash_budget.py` -- use `SetupError`, the flag constants (keep `FLAG`/`UNFLAG` aliases), `write_step_summary`, `exit_code(..., summary_heading='x4pro flash budget')`.
- [x] `scripts/fork_release.py` -- use `Failure`, `SetupError`, `GAMES_BUILD_FLAG` (keep `GAMES_FLAG` alias), `git` with `cwd` and `ok_codes`, `write_step_summary`, `exit_code`; keep its extra `OSError`/`KeyError`/`TypeError` → 2 handler around it.
- [x] `.github/workflows/crosshatch-ci.yml` -- add job `fork-script-tests` (`Fork script tests`): checkout, one step looping over `scripts/*_test.py` with `python3 "$t" -v`, grouping output, failing if any fails or none is found; drop the three per-script test steps; add it to `needs`; add a header bullet.
- [x] `docs/crosshatch/upstream-touches.md` -- add `scripts/fork_common.py` and `scripts/fork_common_test.py` to Game paths.
- [x] `docs/crosshatch/fork-scripts.md` -- create: where fork scripts live, Game paths entry, sidecar test, stdlib only, exit contract, the `fork_common` API, step summaries, the CI job, the local run command.

**Acceptance Criteria:**
- Given the extraction, when each existing `scripts/*_test.py` runs unchanged, then all pass.
- Given the committed tree, when grepping the three scripts, then none defines its own `SetupError`, `Failure`, git runner, flag literal, or summary writer.
- Given a fresh clone of the commit, when the new job's step commands run, then every test file runs and the step exits 0; a deliberately failing scratch test file makes it exit non-zero.
- Given `Crosshatch Test Status`, when reading its `needs`, then `fork-script-tests` is listed.

## Implementation Notes

- Implemented directly (no coding subagent available in this session). Files: `scripts/fork_common.py`, `scripts/fork_common_test.py` (19 tests), the three scripts, `.github/workflows/crosshatch-ci.yml`, `docs/crosshatch/fork-scripts.md`, `docs/crosshatch/upstream-touches.md`.
- The three existing test files are byte-identical to the baseline; the scripts keep `SetupError`, `Failure`, `FLAG`, `UNFLAG`, and `GAMES_FLAG` as module attributes (imported or aliased from `fork_common`), which is what the tests reference.
- Unknown settled: the sidecar tests already pin 0/1/2 through each CLI or `main` (cut 11 CLI cases, cfb CLI plus `compare`/`build`, fr `main` across seven subcommands), so passing them unchanged shows the exit codes did not move; `fork_common_test.py` pins the helper's mapping itself.
- `fr`'s `main` wraps its command in a local `step()` that discards the command's return value, as the old `commands[...](args); return 0` did, so a command returning a value can never become an exit code.
- The step's loop avoids bash arrays (`[ -f "$test" ] || continue` instead of `nullglob`), so it reads the same in any POSIX shell; GitHub runs it under `bash -e`, and `if ! python3 ...` keeps `-e` from stopping at the first failing file.
- No C/C++ changed, so no clang-format or firmware build applies.

## Plan Change Log

## Review Triage Log

Pass 1 (lenses run in this session one at a time: blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 0, medium 0, low 5, false 5, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind | Scripts import `fork_common` by name; running them another way breaks the import | false | Every caller runs them by path (`crosshatch-ci.yml`, `crosshatch-release.yml` `tools/scripts/...`, docs) or adds `scripts/` to `sys.path` (tests); the release publish job's sparse checkout includes all of `scripts`. |
| 2 | blind | The flash budget job lost its fail-fast test step | false | Intended (Design Notes); the new job runs the same test in parallel and fails `Crosshatch Test Status`. |
| 3 | blind | An upstream `scripts/*_test.py` needing packages would fail the job | low | Rejected: none exists upstream today, and the intent says every `scripts/*_test.py`. |
| 4 | blind | `exit_code` passes any non-None value through as an exit code | false | Documented contract ("an int passes through"); `cut.run` and `cfb` return ints, `fr` discards command results in `step()`. |
| 5 | blind | `git_text` decodes strictly as UTF-8 | false | Same as `cut`'s old `git_text`; its callers read SHAs, versions, and `ls-tree` of `freeink-sdk`. |
| 6 | blind, edge (deletion) | `fr`'s git setup errors lost their "cannot read the commit to release" / "cannot compare" wording | low | Rejected: still exit 2 and still name the git command and stderr; recorded in Design Notes. |
| 7 | blind | `fork_common_test.py`'s fixture repo reads the developer's global git config (commit signing would break it) | low | Patched: fixture git calls use `GIT_CONFIG_NOSYSTEM=1`, `GIT_CONFIG_GLOBAL=/dev/null`, as `check_upstream_touches_test.py` does. |
| 8 | blind | `check_flash_budget.py` uses both `import fork_common` and `from fork_common import SetupError` | low | Rejected: cosmetic; `fork_release.py` does the same, and `SetupError` stays a module attribute for the tests. |
| 9 | blind | The docs' local test loop exits 0 after a failure | low | Rejected: it is for a person and prints `FAILED: <file>`; CI's step is the gate. |
| 10 | edge | `git(cwd='')` runs `git -C ''` | false | Git treats an empty `-C` as the current directory; no caller passes it. |
| 11 | verification-gap | No test asserts `fr notes` writes its job-summary section | low | Defer (pre-existing gap on a re-plumbed line; helper covered by `fork_common_test.py`); recorded in `deferred-work.md`. |
| 12 | intent | Intent says "one step"; the diff adds a job with one step and removes three per-script steps | false | The ticket allows a new job in `needs`; the removed steps ran a subset of the new step's files. |

## Design Notes

`git` runs `git -C <cwd>` when `cwd` is given, as `fr` does today, and keeps `cut`'s error text, so `cut`'s messages do not change; `fr`'s two git setup messages change wording (still exit 2). `cut`'s old helper let a missing git escape as a traceback (exit 1); it now exits 2, which is what the contract says. This is the one intended behaviour change.

`exit_code` treats a returned `None` as `PASS` and passes an int through, so `cut.run` and `cfb.compare` keep returning 0/1 and `fr`'s commands keep returning nothing:

```python
def main():
    ...
    return fork_common.exit_code(lambda: run(args.ref, args.upstream))
```

A separate job, not a step in the ledger job: it needs no full history or upstream fetch, and one place runs all tests. The flash budget job loses its fail-fast test step; the new job reports the same failure in parallel.

## Verification

**Commands:**
- `for t in scripts/*_test.py; do python3 "$t" -v || exit 1; done` -- expected: every file OK.
- Fresh clone: `git clone <worktree> <scratch>/s2-2/fresh && git -C <scratch>/s2-2/fresh checkout <commit>`, then run the job's step script there -- expected: exit 0; with a scratch `scripts/zz_fail_test.py`, non-zero.
- `python3 -c "import yaml; yaml.safe_load(open('.github/workflows/crosshatch-ci.yml'))"` -- expected: parses, `needs` lists the job.
- `python3 scripts/check_upstream_touches.py` -- expected: exit 0 (only fork files change).

**Verification record (2026-09-27, Python 3.11.15, git 2.43.0):**
- Baseline `799d5add`: the three existing test files pass (24, 18, 50 tests). After the change, with those files unchanged: 24, 18, 50 pass, and `fork_common_test.py` passes 19; each file exits 0.
- Fresh tree (retro AI-8) of commit `62ccc69f9f695b374aedf41e13801caf6a3357e1`: this session's worktree guard refuses git commands aimed outside the worktree, so the tree came from `git archive <commit> | tar -x -C <scratch>/s2-2/fresh` (the committed files only, no untracked or ignored files; the tests need no `.git` in the checkout, as with CI's depth-1 checkout). The job's step block, run there under `set -eo pipefail`, exited 0 with all four files passing; with a scratch `scripts/aa_fail_test.py` that exits 1, it reported that file, still ran the other four, and exited 1; in a directory with an empty `scripts/`, it printed "no scripts/*_test.py found" and exited 1. The final commit differs from `62ccc69` only in this record, and the step was run again on it.
- `yaml.safe_load` of `crosshatch-ci.yml`: jobs `upstream-ledger`, `flash-budget`, `simulator-build`, `fork-script-tests`, `crosshatch-test-status`; `needs` lists `fork-script-tests`.
- `check_upstream_touches.py` not run end to end: this clone is shallow with no `upstream` remote, and every changed path is a Game path or a new fork-only file (`scripts/fork_common*.py`, `docs/crosshatch/`, `.github/workflows/crosshatch-ci.yml`, `_bmad-output/`), so the ledger rule does not apply; its 11 CLI integration cases in `check_upstream_touches_test.py` run the refactored script against throwaway repositories and pass. CI's ledger job runs it on the epic PR.

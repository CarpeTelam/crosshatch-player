---
title: 'Fix the release script''s game-packer path'
type: 'bugfix'
ticket: '12'
created: '2026-10-05'
status: done
baseline_revision: 'ffbb4c7fa65038e4ee797c78528aa4687a978d58'
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: []
deferred: []
---

<intent-contract>

## Intent

**Problem:** `scripts/fork_release.py` `pack_one` runs `scripts/pack_game.py` with `cwd=project_dir` but passes the packer as the path `pack_games` built from the same `project_dir`. With a relative `--project-dir src` (as `crosshatch-release.yml` runs it) the child looks for `src/src/scripts/pack_game.py`, and every game fails to pack (epic Notes, Finding of 2026-10-05). The tests all pass an absolute project directory, which hid it.

**Approach:** Resolve every path `pack_one` hands the packer subprocess to an absolute one, so it no longer depends on the child's changed directory, and add a `fork_release_test.py` case that runs `pack-games` with a relative project directory over a tree holding a game. No change to the workflow file.

</intent-contract>

## Implementation Notes

Oneshot: about 10 lines in one function and one test case, both in already-ledgered fork files. The `pack_one` subprocess is the only one `pack_games` starts. Its arguments are `sys.executable` (absolute), the packer (the bug), `games/<id>` (meant relative to the child's `cwd`, which is `project_dir`, so correct), and `out_dir` (a child of `tempfile.TemporaryDirectory`, absolute in practice but resolved too so a relative `TMPDIR` cannot break it). `cwd=project_dir` itself is resolved by the OS against the parent's directory, so it stays as is. `plan` and `dist` are used in-process only.

## Design Notes

- History: `git log -L626,647:scripts/fork_release.py` shows `pack_one` unchanged since `a2c1204a` apart from `.cpgame` to `.chgame` (`7af3fe19`); no earlier fix to preserve.
- Paths are made absolute with `Path.absolute()`, not `resolve()` (review patch): it cannot raise on a symlink loop outside the `try`, and it leaves a symlinked packer under its own path.
- Guards kept as they are: the `OSError` to `SetupError` (a packer that cannot start is exit 2, with the original path in the message); the non-zero exit, the last-line package-hash match, and the package-file check (each is a `Failure`, exit 1).
- Test double: `PACKER_OK` stands in for `scripts/pack_game.py` and agrees with it on what the release relies on: `argv[1]` is `games/<id>` relative to the project root (the child's cwd), `argv[2]` a directory the package `<id>.chgame` is written into, and the last stdout line is a 16-hex hash. It is more permissive than the real packer (no manifest checks); the verify step runs the real one. The new case pins the failure itself: the child not finding the packer file.

## Review Triage Log

### 2026-10-05 — Review pass
- verdicts: 13 findings — high 0, medium 0, low 6, false 7, maybe-false 0 (the intent-alignment lens reports readings, not findings, and the verification-gap lens reported none)
- findings:
  - `[low]` `patch` blind: the relative `out_dir` half of the fix is untested — added a direct `pack_one` call with a relative `out_dir` to the new test; reverting either half of the fix now fails it (checked).
  - `[false]` `reject` blind: the fix belongs in `pack_games` — `pack_one` is the function that starts the child, so it protects every caller; resolving `project_dir` there would leave `pack_one` unsafe on its own.
  - `[low]` `patch` blind: `resolve()` follows symlinks — switched to `Path.absolute()`.
  - `[false]` `reject` blind: the `OSError` message names the packer for a missing project dir — unchanged pre-existing text (`a2c1204a`), not caused by this change; a missing `project_dir` already fails earlier in `pack_games` (`games/` check).
  - `[false]` `reject` blind: `build`/`prepare` not checked — `build` runs `pio run -e <env>` with `cwd=args.project_dir` and passes no path, `prepare` passes none either (read at `fork_release.py` lines 441, 522, 564), and the dry run passed both; recorded under Implementation Notes.
  - `[low]` `patch` blind: the test swaps `self.dir` and registers its `chdir` restore as a cleanup — rewrote it with explicit paths and a `try/finally`; it also asserts `release/` did not land inside `src/`.
  - `[false]` `reject` blind: plan claims lack evidence and `deferred` is empty — the evidence is in Verification below; nothing in the review is left deferred (the test double's permissiveness is stated in Design Notes).
  - `[low]` `patch` edge: `.resolve()` sits outside the `try` that maps `OSError` to `SetupError` — gone with `absolute()`, which does not touch the filesystem.
  - `[low]` `patch` edge: the out_dir half is not exercised (same root as the first blind finding) — fixed by the same test.
  - `[low]` `patch` edge: `addCleanup(os.chdir)` runs after `tearDown`, deleting the cwd (Windows only) — fixed by the `try/finally` rewrite.
  - `[false]` `reject` edge: the package path is built from the unresolved `out_dir` while the child wrote to the absolute one — the same directory while the parent's cwd is unchanged, which nothing in `pack_games` changes; the new test pins that `package` points at the file the child wrote.
  - `[false]` `reject` edge (claim): a parent cwd change before `run` is unguarded — nothing in the script calls `chdir`.
  - `[false]` `reject` blind (style): comment lines near 120 columns — the repo's Python has longer lines and the formatter leaves it alone.

## Verification

**Commands (run after the review patches):**
- `python3 scripts/fork_release_test.py`: 84 tests OK. With the fix stashed, `test_relative_project_dir_packs` fails; with only the `out_dir` half reverted it also fails.
- Every `scripts/*_test.py`: all pass, each with `Ran n tests`.
- `python3 scripts/check_upstream_touches.py`: PASS. `./bin/clang-format-fix` twice: no change to `git status`.
- `git archive 89a08512e070` (a `git stash create` commit holding these scripts; `git diff` of `scripts` and `games` against the final commit is empty) extracted to `<scratch>/arch/src`, run from `<scratch>/arch` with `--plan plan.json --project-dir src --dist release/dist`: exit 0, three packages written. Hashes equal what `pack_game.py games/<id> <out>` prints in `src`: battleship `10a07de10cb14489`, sudoku `3c657cfcb2e461dc`, ultimate-tic-tac-toe `a7e63b542144938b`. The old `fork_release.py` over the same tree: exit 1, `.../src/src/scripts/pack_game.py` not found, all three games fail to pack (2).
- No firmware build: no firmware code changed. The workflow file is unchanged.

## Auto Run Result

**Summary:** `pack_one` hands the packer subprocess an absolute packer and `out_dir`, so `--project-dir src` (as CI runs it) works. The new test runs `pack-games` from the parent directory with relative `--project-dir`, `--plan`, and `--dist`, and calls `pack_one` with a relative `out_dir`.

**Files:**
- `scripts/fork_release.py`: `pack_one` makes `packer` and `out_dir` absolute.
- `scripts/fork_release_test.py`: `test_relative_project_dir_packs`.
- This plan.

**Review (lenses blind-hunter, edge-case-hunter, verification-gap, intent-alignment, each a context-free subagent):** 6 patches applied (4 distinct root causes, all low), 0 deferred, 7 rejected as false with the reasons in the log. Intent-alignment: the diff implements the narrow reading (fix in `pack_one`, in-process test with a stub packer); the end-to-end run with the real packer is the archive check above.

**Formatting changes outside this story's paths:** none.

**Follow-up review recommended:** false (no high or medium patched).

**Residual risks:** `PACKER_OK` is more permissive than the real packer (no manifest checks); the archive run covers the real one. CI's own run is the epic PR's dry run.

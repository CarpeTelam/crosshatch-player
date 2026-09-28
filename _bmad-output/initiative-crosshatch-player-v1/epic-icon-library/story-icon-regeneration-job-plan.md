---
title: 'Icon regeneration job'
type: 'chore'
ticket: '3'
created: '2026-09-28'
status: 'built'
baseline_revision: 'bc6adc548587ec84c1530e72f1fb6908dc13895c'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick']
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `lib/GameIcons/GameIcons.generated.h` is committed (spine AD-2, AD-24; epic R2), so nothing stops a
hand edit, or an SVG, map, or generator change committed without regenerating, from drifting away from what
`scripts/gen_game_icons.py` produces.

**Approach:** Add an `Icons up to date` job (`icons-up-to-date`) to `.github/workflows/crosshatch-ci.yml` that runs
entry 1's generator, unchanged, with `--out` pointing at a scratch path, compares the result with the committed
header byte for byte, and fails on any difference with an error naming the header, the first differing byte, a capped
diff, and the command that regenerates it; list the job in `Crosshatch Test Status`'s `needs` and describe it in the
workflow's header comment. No change to the generator, the header, `ci.yml`, or the spine (its CI row is entry 7's).

</frozen-after-approval>

## Implementation Notes

- Oneshot: one fork-only workflow file gains one job (about 25 lines of YAML and comment); nothing else changes.
- `.github/workflows/crosshatch-ci.yml` is fork-only (`git cat-file -e upstream/develop:<path>` fails), so the ledger
  is not involved.
- The job checks out without history or submodules and sets up Python 3.13, like `Layer check`: the generator reads
  only `assets/game-icons/`, `scripts/`, and writes to `$RUNNER_TEMP`, so the committed header is compared, never
  rewritten. `cmp`'s output (the first differing byte, or its EOF or missing-file line) goes into the `::error`
  annotation; `diff -u | head -n 200`, under a line saying it is capped, shows the change without flooding the log
  with hex rows. Inside `if !`, `bash -e` does not stop on `cmp`'s non-zero exit.
- Python versions for the same-bytes check: 3.11.15 (the system `python3`, which generated the committed header in
  entry 1) and 3.13.12 (`/usr/bin/python3.13`, CI's `setup-python` version); 3.10 and 3.12 checked as well.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-09-28). The oneshot route's `quick` lens ran as a context-free subagent over the diff from
`bc6adc54` (workflow and this plan). Verdicts: 0 high, 0 medium, 1 low, 0 false, 0 maybe-false. Route: 1 patch.

| # | Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- | --- |
| 1 | quick | The `::error` annotation names the header and the fix command but not the first differing byte, which `cmp` printed only as a separate log line; the capped diff had no marker saying it is capped | low | patch: `cmp`'s output (stdout and stderr, so the EOF and missing-file cases too) is captured into the annotation, and a line before the diff says it shows the first 200 lines; re-run in the worktree passes |

## Verification

**Commands:**
- `git clone /home/user/wt-ci <scratch>/3.3/fresh && git -C <scratch>/3.3/fresh checkout <commit>` (a clone without
  submodules, as the job's checkout is), then the job's `run` block, extracted from the committed workflow with
  PyYAML and run by `bash -e` with `RUNNER_TEMP` set to a scratch folder -- expected: exit 0 and the match line.
- The same after flipping one byte of the clone's `lib/GameIcons/GameIcons.generated.h` -- expected: exit 1, the
  `::error` line, `cmp`'s first difference, and the diff; then restore it.
- `python3.X scripts/gen_game_icons.py --out <scratch>/3.3/py3X.h` in the clone for 3.10, 3.11, 3.12, and 3.13, then
  `cmp` each with the committed header -- expected: all identical.
- The workflow loads with PyYAML; `crosshatch-test-status.needs` includes `icons-up-to-date`.
- `./bin/clang-format-fix` twice -- expected: `git status` unchanged by the second run.

**Results (2026-09-28), from a fresh tree:** a `git clone` of the worktree checked out at `12587af5` (the story's
commit before this Verification text was added; the amend changes only this plan), submodules left uninitialised as
the job's `actions/checkout` leaves them (`freeink-sdk/` empty). The job's `run` block was extracted from that tree's
workflow with PyYAML and run with `bash -e` and `RUNNER_TEMP` in the story's scratch folder.
- Workflow: loads; `crosshatch-test-status.needs` ends with `icons-up-to-date`; the job's steps are checkout,
  setup-python 3.13, and the compare step.
- Committed header: exit 0, "lib/GameIcons/GameIcons.generated.h matches a fresh run of scripts/gen_game_icons.py".
- One byte changed (byte 3000, `0x33` to `0x32`, a hex digit in `DIE_6_32`): exit 1; the annotation reads
  `::error file=lib/GameIcons/GameIcons.generated.h::differs from a fresh run of scripts/gen_game_icons.py
  (lib/GameIcons/GameIcons.generated.h <tmp>/GameIcons.generated.h differ: char 3001, line 49); run python3
  scripts/gen_game_icons.py and commit the header`, followed by the capped diff showing line 49. After
  `git checkout` of the header: exit 0 again.
- Same bytes under several Python versions: `/usr/bin/python3.10` (3.10.20), `python3.11` (3.11.15),
  `python3.12` (3.12.3), and `python3.13` (3.13.12) each wrote a header identical to the committed one (`cmp`; all
  sha256 `1aacb076942194de…`). The whole `run` block also passed with `python3` resolving to 3.13.12, CI's version.
- `python3 scripts/gen_game_icons_test.py` in the fresh tree: 23 tests OK.
- `./bin/clang-format-fix` twice: no change to any file either time.

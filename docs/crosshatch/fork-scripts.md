# Fork scripts

The fork's tooling in `scripts/` (the CI checks, the release pipeline, and the game tools) follows one pattern, so
each new script copies it instead of inventing its own. Upstream's scripts in the same folder keep their own style;
these rules apply only to fork scripts.

## Rules

- **Fork-only path.** Add the script and its test to the **Game paths** of
  [`upstream-touches.md`](upstream-touches.md), so an upstream merge can never add or change a file of the same name.
- **Standard library only**, Python 3.11 or later, plus `git` where the script needs it. A check then runs before
  any toolchain install, and its tests run anywhere.
- **Sidecar test.** `scripts/<name>.py` has `scripts/<name>_test.py`, a `unittest` file that runs on its own with
  `python3 scripts/<name>_test.py`. It asserts the exit code of every outcome, so a regression that makes a check
  always pass is caught. Build fixtures (throwaway git repositories, fake `pio` on `PATH`, sparse files) in a temp
  directory; never depend on this repository's history or submodule.
- **Exit contract.** 0: passed. 1: a rule is broken. 2: the check could not run (a missing file or ref, a failed
  tool, a shallow clone). CI fails on both 1 and 2; the difference tells the reader whether to fix the change or the
  setup. A script may add a code above 2 for a broken rule whose fix differs from the rest, so CI can give that fix
  alone: `gen_game_icons.py` exits 3 (`PIN_MISMATCH`) when the SVGs and `SHA256SUMS` disagree, and only then does the
  `Icons up to date` job advise re-pinning. The module docstring says what each code means for that script and how to
  run it locally.
- **Shared plumbing from `scripts/fork_common.py`.** Never define another `SetupError`, git runner, exit-code
  handler, step-summary writer, or copy of the games-flag literal.
- Style: single quotes; the whole-tree clang-format check does not cover Python.

## `fork_common.py`

A script imports it by name (`import fork_common` or `from fork_common import SetupError`). That works when the
script runs as `python3 scripts/<name>.py` and from its sidecar test, which puts `scripts/` on `sys.path`; the release
workflow's sparse checkout of `scripts` includes it too.

| Name | Use |
| --- | --- |
| `PASS`, `FAIL`, `COULD_NOT_RUN` | The exit codes 0, 1, 2. |
| `Failure` | Raise for a broken rule (exit 1). A script may instead return `FAIL` after printing every problem. |
| `SetupError` | Raise when the check could not run (exit 2). Turn expected environment errors (`OSError` from a missing tool or file) into it. |
| `exit_code(step, summary_heading=None)` | Calls `step()`: `None` is 0, an `int` passes through, `Failure` is 1, `SetupError` is 2, each printed as `error: <message>` on stderr. With a heading, a `SetupError` also goes to the job summary. Anything else propagates as a traceback. |
| `git(*args, cwd=None, ok_codes=(0,))` | Runs `git` (`git -C cwd` when given) and returns `(exit code, stdout bytes)`. An exit code outside `ok_codes`, or no `git` on `PATH`, is a `SetupError` naming the command and git's stderr. |
| `git_text(*args, cwd=None)` | git's stdout as stripped text; any non-zero exit code is a `SetupError`. |
| `file_at(ref, path, cwd=None)` | The bytes of `path` at `ref`, or `None` when `ref` has no such file; an unknown `ref` is a `SetupError`. |
| `write_step_summary(text, path=None)` | Appends Markdown to `path`, or else to `$GITHUB_STEP_SUMMARY`; a no-op outside Actions. |
| `GAMES_MACRO`, `GAMES_BUILD_FLAG` | `FREEINK_CAP_GAMES` and `-DFREEINK_CAP_GAMES=1`, as `platformio.ini` spells it. |
| `API_LEVEL_HEADER`, `api_list_path(level)` | `lib/GameCore/ApiLevel.h` and `docs/crosshatch/api-level-<level>.txt`, relative to the repository root. |
| `ApiLevel`, `parse_api_level(text)` | Reads `(level, min_level, frozen)` from the header's one-line `#define`s; a missing, repeated, or malformed define is a `SetupError`. |
| `frozen_top(level)` | The highest frozen level of an `ApiLevel`: `API_LEVEL` when `API_LEVEL_FROZEN` is true, else `API_LEVEL - 1`; 0 for `None` (no header) or when nothing is frozen. |

A script's `main()` parses its arguments and returns `exit_code(...)`; the file ends with
`if __name__ == '__main__': sys.exit(main())`:

```python
def main(argv=None):
    args = parser.parse_args(argv)
    return fork_common.exit_code(lambda: check(args.ref), summary_heading='My check')
```

Add a name to `fork_common.py` only when two fork scripts would otherwise both write it, and give it a test in
`scripts/fork_common_test.py`. A helper only one script needs, such as `pio` runs or `GITHUB_OUTPUT` values, stays in
that script.

## CI

The `Fork script tests` job in `.github/workflows/crosshatch-ci.yml` runs every `scripts/*_test.py` on every pull
request, each file in its own process, and reports each failing file. It also fails a file whose output has no
`Ran <n> tests` line with n above 0, so a test file that forgets `unittest.main()`, and so exits 0 having run
nothing, cannot pass. A new sidecar test is picked up with no workflow change; `scripts/sim_sh_test.py`, which tests
the simulator skill's `sim.sh setup` and `check` on scratch git repositories, runs there too. A script that is itself
a CI gate gets its own job there, listed in `Crosshatch Test Status`'s `needs`, and is run once from a fresh clone
before its ticket counts as built. `gen_game_icons.py` is one: the `Icons up to date` job runs it, and it checks the
vendored SVGs against `assets/game-icons/SHA256SUMS` before rendering, so an edited SVG fails that job
(`docs/crosshatch/game-icons.md`).

Run the same tests locally from the repository root:

```sh
for t in scripts/*_test.py; do
  out="$(python3 "$t" 2>&1)" || { echo "FAILED: $t"; continue; }
  printf '%s\n' "$out" | grep -Eq '^Ran [1-9][0-9]* tests? in ' || echo "RAN NO TESTS: $t"
done
```

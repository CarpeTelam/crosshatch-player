---
title: 'dev_setup.py reads a command stderr as its value (e6pre-16)'
type: 'bugfix'
ticket: ''
created: '2026-10-10'
status: 'done'
baseline_revision: 'ccb748bb5f4892ed1b27cb49077dab0709a905c4'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
deferred:
  - summary: 'run() in scripts/dev_setup.py has no timeout, so a hung child blocks setup'
    location: 'scripts/dev_setup.py run()'
    severity: low
  - summary: "The first apt-get install failure's output is discarded when the retry after apt-get update fails differently"
    location: scripts/dev_setup.py Setup.system_packages
    severity: low
review_loop_iteration: 0
context: ['{project-root}/docs/crosshatch/fork-scripts.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `run()` in `scripts/dev_setup.py` merges stderr into stdout, so a warning uv prints on stderr (`UV_NATIVE_TLS` is deprecated, set in cloud sessions) becomes part of every value a caller parses. `uv_tool_dir()` yields a path starting with the warning, so the `tool CA` step fails with "no pioarduino uv tool interpreter at warning: ...", and `find_pio()` builds a bad `uv tool dir --bin` path and only works through its PATH fallback.

**Approach:** `run()` captures stdout and stderr separately. Callers that parse output read stdout only; a failed command's error message shows stdout then stderr, as the tail does today. Update the module docstring and `run()` docstring; keep the exit contract (0, or 2 with no exit 1).

## Boundaries & Constraints

**Always:** Every existing test passes unchanged. Standard library only. Single quotes. `docs/crosshatch/fork-scripts.md` exit contract holds: exit 0, or 2 on a failed step, never 1.

**Never:** Change unrelated behaviour (step order, locks, stamp, the detached warm builds' own `stderr=STDOUT` into their log). Touch `fork_common.py`, `AGENTS.md`, or any file but `scripts/dev_setup.py`, `scripts/dev_setup_test.py`, and this plan.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| uv warns on stderr | `uv tool dir` prints a path on stdout, a warning on stderr | `tool CA` finds the interpreter; `find_pio()` returns the uv tool's `pio` over a `pio` on PATH | No `error:` line |
| Parsed command writes stderr | `uv tool list`, `<python> -c certifi`, the penv version query, `dpkg-query` | Parsed from stdout only: no wrong reinstall, no polluted path | none |
| Command fails | non-zero exit with output on both streams | `SetupError` names the command, the code, and the tail of stdout then stderr | exit 2 as today |

</frozen-after-approval>

## Code Map

Every `run` / `run_checked` caller, with what it parses and what it only shows:

- `Setup.uv_tools` -- `run_checked([uv, 'tool', 'list'])`: PARSES (regex of `name vX` lines) -> stdout.
- `uv_tool_dir` (caller `Setup.tool_ca`) -- `run_checked([uv, 'tool', 'dir', *flags])`: PARSES a path -> stdout.
- `find_pio` -- `run([uv, 'tool', 'dir', '--bin'])`: PARSES a path (exit code checked) -> stdout.
- `certifi_bundle` (callers `Setup.tool_ca`, `Setup.prepare_penv`) -- `run_checked([python, '-c', CERTIFI_WHERE])`: PARSES a path -> stdout.
- `Setup.prepare_penv` -- `run([penv_python, '-c', PIOARDUINO_VERSION_QUERY])`: PARSES a version (compared to the pin) -> stdout.
- `Setup.package_installed` -- `run(['dpkg-query', '-W', ...])`: PARSES the status string -> stdout.
- `Setup.pio_packages` -- `run(command)` twice (`pio pkg install`): SHOWS only (tail on failure); exit code is the value.
- `Setup.prepare_penv` -- `run_checked([uv, 'pip', 'install', ...])`: SHOWS only (tail on failure).
- `Setup.system_packages` -- `run(install)` (exit code only) and `run_checked(['apt-get', 'update'|install])`: SHOW only.
- `Setup.spawn` uses `subprocess.Popen` directly with `stderr=STDOUT` into a log: untouched.
- Tests: `scripts/dev_setup_test.py` fakes (`FAKE_MODULE`) and `SetupTestBase`; cases drive `dev_setup.main()` with fake tools on PATH.

## Tasks & Acceptance

**Execution:**
- [ ] `scripts/dev_setup.py` -- `run()` returns `(code, stdout, stderr)` from separate pipes; `run_checked()` returns stdout and raises with `tail(stdout, stderr)`; `tail()` joins its non-empty arguments; update each caller above to unpack three values (parsed ones use stdout; shown ones use `tail(stdout, stderr)` on failure); update the module docstring and `run()` docstring.
- [ ] `scripts/dev_setup_test.py` -- the fakes print `FAKE_STDERR` (a JSON map of tool name to stderr text) to stderr; new cases: uv warning (cold run and warm run: `tool CA` ok, no `error:`), `find_pio()` returns the uv tool's `pio` over a PATH `pio`, a stderr `pioarduino v6.1.19` line on `uv tool list` is not an installed tool, python stderr (certifi and penv version query) causes no failure and no re-pin, dpkg-query stderr causes no reinstall, `run()` returns stdout only, `run_checked()` failure shows stderr.

**Acceptance Criteria:**
- Given uv writes a warning to stderr, when `dev_setup.main` runs, then `tool CA` reports ok or appended and no `error:` line is printed.
- Given uv warns and a different `pio` is on PATH, when `find_pio()` runs, then it returns the uv tool's `pio`.
- Given each new test, when run against the old `dev_setup.py`, then it fails.
- Given the existing tests, when run, then they pass unchanged.
- Given `UV_NATIVE_TLS=1 python3 scripts/dev_setup.py` (no `--warm`) in the worktree, then `tool CA` is ok or appended and no `error:` line shows for it.

## Implementation Notes

Built by an implementation subagent from this plan, then one patch round (review findings 1 to 3) by the same subagent. Files: `scripts/dev_setup.py`, `scripts/dev_setup_test.py`. `run()` returns `(code, stdout, stderr)`; `run_checked()` returns stdout; `tail()` takes several texts. The fakes read `FAKE_STDERR` (a JSON map of tool name to stderr text). Nine `StderrTest` cases were added; the 28 existing cases are unchanged.

## Plan Change Log

## Review Triage Log

Pass 1. Each lens ran as a context-free subagent in the foreground (blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: 0 high, 1 medium, 3 low patched or fixed, 6 low rejected, 2 low deferred, 1 false group.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | verification-gap, blind | The `pio pkg install` failure messages ('left no penv', 'failed again') carry `tail(stdout, stderr)` but no test sets pio stderr; dropping stderr there would pass every test | medium | patch | Read the tests: only the 'failed again' prefix is asserted. Two cases added (pio fails with stderr marker, with and without a penv) |
| 2 | blind | Module docstring says commands run with separate pipes, but the warm builds still merge into their log | low | patch | True: `spawn` keeps `stderr=STDOUT`. Docstring reworded to name `run()` / `run_checked()` |
| 3 | blind | Docstring wording 'that are no part of the value' is awkward | low | patch | Reworded with #2 |
| 4 | blind, edge | Joined tail of 20 lines can push stdout out of a failure message when stderr has 20 or more lines | low | rejected | Needs a stderr of 20+ lines on a failing command; the error text is on stderr in the likely cases (pio, uv), and stderr is last so it survives. A per-stream tail adds a branch for a case nobody met |
| 5 | blind | Swallowing the UV_NATIVE_TLS warning hides the root cause; no note to switch to UV_SYSTEM_CERTS | low | rejected | The intent says a command's stderr is shown only in a failure message; surfacing warnings on success is a different behaviour |
| 6 | blind | No direct test of `uv_tool_dir()` / `tail()` variants | low | rejected | The cold/warm CA test fails on the old code (checked with HEAD's `dev_setup.py`), so a leak into the path is caught; `tail` is exercised by the order test |
| 7 | blind | Test hygiene: `mkdir` without `exist_ok`, `_` to `-` mapping in `stderr_of`, `FAKE_STDERR` restore | false / low | rejected | The temp HOME is fresh so `mkdir` cannot collide; `restore_environment` already handles a `None` saved value; no fake name contains `_` |
| 8 | blind | Related docs may quote the old merged behaviour | false | rejected | `grep` over docs, AGENTS.md, hooks, and the script found no other mention of "stdout and stderr" |
| 9 | blind | Same sentence repeated in three docstrings could drift | low | rejected | Three short docstrings each state the order for their own function; merging them is a refactor |
| 10 | edge | `run()` has no timeout | low | defer | Pre-existing: `subprocess.run` had none before this change |
| 11 | edge | The first `apt-get install` failure's output is dropped when the retry fails differently | low | defer | Pre-existing: the old code also discarded it (`code, _ = run(...)`) |
| 12 | intent | Divergence: tests use fakes, not the real uv | none | n/a | The live run `UV_NATIVE_TLS=1 python3 scripts/dev_setup.py` reported `tool CA: ok` with no `error:` line; the old-code check failed all 7 new cases |

## Design Notes

Guards in the rewritten functions, all kept: `run()` turns an `OSError` (missing tool) into `SetupError`; keeps `stdin=DEVNULL` (the hook's stdin carries JSON), `errors='replace'`, and `env` adding to `os.environ`. `run_checked()` raises on any non-zero code with the command and output tail. `find_pio()` accepts the uv path only when the exit code is 0 and `pio` is a file, else falls back to PATH. History (`git log -L`): `run` has two commits (50701eae, c7ff55b6); c7ff55b6 added `stdin` and `errors='replace'`, both kept.

Separate pipes lose the stdout/stderr interleaving that the merged form gave in a failure tail; the tail now shows stdout, then stderr, so the error text stays last. This is the one shown-output change.

## Verification

**Commands:**
- `python3 scripts/dev_setup_test.py` -- expected: all pass.
- Old-code check: new cases run against `git stash`ed `dev_setup.py` -- expected: each new case fails.
- `for t in scripts/*_test.py; do python3 "$t" || echo FAILED $t; done` -- expected: no FAILED.
- `python3 scripts/check_upstream_touches.py` -- expected: exit 0.
- `./bin/clang-format-fix` twice -- expected: nothing new in `git status`.
- `UV_NATIVE_TLS=1 python3 scripts/dev_setup.py` (under `/tmp/crosshatch-build.lock`, since the pio packages step may install) -- expected: `tool CA: ok` or `appended`, no `error:` line for it.

**Results (commit after the last patch, local evidence):**
- `python3 scripts/dev_setup_test.py`: 37 tests pass (28 existing, unchanged, plus 9 new). With HEAD's `dev_setup.py` restored, the first 7 new cases all failed; the 2 pio-message cases were mutation-checked (`tail(stdout)` in place of `tail(stdout, stderr)` fails both).
- Every `scripts/*_test.py` passes, each with a `Ran n tests` line.
- `python3 scripts/check_upstream_touches.py`: PASS. `./bin/clang-format-fix` run twice: exit 0, nothing new in `git status`.
- `UV_NATIVE_TLS=1 python3 scripts/dev_setup.py` in the worktree (no `--warm`): `tool CA: ok`, no `error:` line, `dev setup: done`. No firmware build, `pio check`, or host C++ suite applies (two Python files).

## Auto Run Result

**Summary:** `run()` keeps stdout and stderr on separate pipes; every parsed value reads stdout only; a failed command's message shows stdout then stderr.

**Files:** `scripts/dev_setup.py`, `scripts/dev_setup_test.py`, this plan.

**Review:** four lenses ran as context-free foreground subagents (see the Review Triage Log): 1 medium patched (pio failure messages untested), 2 low patched (docstrings), 6 low and 2 false rejected, 2 low deferred (pre-existing).

**Residual risks:** the failure tail is the last 20 lines of stdout plus stderr, so a stderr of 20 or more lines hides stdout in an error message. The interleaving of the two streams in a failure message is gone.

**Formatting-only changes outside my paths:** none.

---
title: 'Faster cloud cold start and a shorter verification loop'
type: 'chore'
ticket: ''
created: '2026-10-01'
status: 'draft'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A timed `/bmad-build` of deferred entry 114 took 53 min from a cold cloud container to ready-for-PR. About 8 min was hand-run setup: AGENTS.md describes it but scripts none of it, and the first package install always fails on TLS. About 29 min was firmware: all five envs, although only `x4pro` and `sticky` compile games code. Static analysis checks only `default`, so games code is never analyzed; `pio check -e x4pro` finds two defects today.

**Approach:**
- One idempotent setup script, run synchronously by a cloud-only SessionStart hook, so the container cache keeps its result. It then starts detached warm builds under a fixed build lock.
- AGENTS.md and the build brief: point to the script, build `x4pro` while testing (plus `default` for shared code), let CI build the rest, and run the review before firmware builds.
- Add `pio check -e x4pro` to fork CI, and fix the two defects it reports.

## Boundaries & Constraints

**Always:**
- `scripts/dev_setup.py` follows `docs/crosshatch/fork-scripts.md`: standard library only, `fork_common` exit contract, a sidecar test, and listed in the ledger's Game paths.
- Every step skips when already done. A failed step is reported, the remaining steps still run, and the exit is 2.
- The CA bundle is appended only when `/root/.ccr/ca-bundle.crt` exists and the target bundle lacks it.
- The hook does nothing unless `CLAUDE_CODE_REMOTE=true`, and always exits 0, so a failed setup never blocks a session.

**Never:**
- Edit upstream's `ci.yml`, `.gitignore`, `.skills/`, the `.claude/skills/bmad-*` workflow files, or the `freeink-sdk` pointer.
- Change `platformio.ini`.
- Install anything but `libsdl2-dev` and `libpcre3` system-wide (CI installs both).
- Run the setup outside cloud sessions without the user asking.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Cold container | no `pio`, no `~/.platformio`, shallow clone, empty `freeink-sdk` | every step runs; the penv gets the CA and the 6.1.19 pin before the retried install; exit 0 | -- |
| Warm container | everything present | only cheap checks run (no reinstall, no second CA append); exit 0 | -- |
| First install fails on TLS | `pio pkg install` exits non-zero and leaves `~/.platformio/penv` | patch the penv CA, pin it, retry once; exit 0 if the retry passes | retry fails: report the step, exit 2 |
| No `uv` | `uv` not on PATH | tool steps report a setup error naming `uv`; other steps still run | exit 2 |
| No proxy CA | `/root/.ccr/ca-bundle.crt` missing | no bundle is touched | -- |
| Not root or no `apt-get` | non-root user or no `apt-get` | system-package step is skipped with a one-line note | not a failure |
| `--warm` | flag given | starts detached `pio run -e x4pro` under `/tmp/crosshatch-build.lock` and the host-test build under `/tmp/crosshatch-hosttest.lock`, logging to `~/.cache/crosshatch/`, and returns at once | spawn failure: exit 2 |
| Hook outside cloud | `CLAUDE_CODE_REMOTE` unset | exits 0 having run nothing | -- |

</frozen-after-approval>

## Code Map

- `AGENTS.md` "Running and verifying" -- four setup bullets to replace with the script:
  - submodule init;
  - the pioarduino 6.1.19 / penv pin;
  - the TLS "UnknownIssuer" recipe;
  - libpcre for `pio check`.

  Also edit there: the "Iterate with" bullet (which envs to build) and the clang-format bullet (exits 1 below 21). Under "Known pitfalls", the `flock` bullet has no fixed lock path. AGENTS.md is fork-owned (ledger Allowlist).
- `docs/crosshatch/orchestrated-epics.md` -- lines 20-24 (`{lock}`), 108-110 (all five envs before the epic PR), 164-171 (Environment), 214-215 (envs per C/C++ change), and the review bullet (lenses) at about line 146.
- `scripts/fork_common.py` -- `SetupError`, `exit_code`, `PASS`/`COULD_NOT_RUN`. Model a new script and test on `scripts/check_layers.py` and `scripts/check_layers_test.py`, and fake tools on PATH the way `scripts/check_flash_budget_test.py` fakes `pio`.
- `.github/workflows/crosshatch-ci.yml` -- copy `flash-budget`'s setup steps (checkout with submodules through the penv pin, the same `pio-v3-...-x4pro` cache key). Then `apt-get install libpcre3` (as `ci.yml`'s cppcheck job) and `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`. Add the job to `crosshatch-test-status.needs`.
- `.claude/settings.json` -- keep `env`; add `hooks.SessionStart`.
- `docs/crosshatch/upstream-touches.md` "Game paths" -- add `scripts/dev_setup.py`, `scripts/dev_setup_test.py`, and `.claude/hooks`.
- `src/games/GameHash.cpp:33` -- the mbedTLS constructor. cppcheck takes `finish(uint8_t (&)[N])` in `GameHash.h:29` for a member variable (`uninitMemberVar`), a false positive. Suppress it inline, in the style of `src/activities/ActivityResult.h:82`.
- `src/games/GamePackageInstaller.cpp:296` -- `removeFolderMarkerLast`'s `name` is used only inside the first block. Move its declaration (and its comment) into the block; the frame size is unchanged.
- Facts measured this session:
  - The penv has no `pip`; it pins with `uv pip install --python ~/.platformio/penv/bin/python pioarduino==6.1.19`.
  - The certifi paths come from each interpreter (`<python> -c "import certifi; print(certifi.where())"`). The uv tool's interpreter is `$(uv tool dir)/pioarduino/bin/python`.
  - `uv tool install 'clang-format>=21,<22'` gives 21.1.8 in 0.2 s.
  - The C3 compiler (`toolchain-riscv32-esp`) installs on the first `default` build, not at `pio pkg install`.

## Tasks & Acceptance

**Execution:**
- [ ] `scripts/dev_setup.py` -- the steps, in order:
  1. submodules;
  2. uv tools;
  3. CA into the uv tool bundle;
  4. `pio pkg install -e x4pro -e default`, with the penv CA, the pin, and one retry;
  5. git: unshallow, the `upstream` remote, `fetch upstream develop`, and `merge.ours.driver`;
  6. system packages;
  7. `--warm`.

  The docstring gives the exit codes and the local usage.
- [ ] `scripts/dev_setup_test.py` -- one test per matrix row, with fake `uv`/`pio`/`git`/`apt-get`/`flock` on PATH, a temp HOME and a temp CA. Assert the exit codes, the commands run, and that the CA is appended exactly once over two runs.
- [ ] `.claude/hooks/session-start.sh` -- run `python3 "$CLAUDE_PROJECT_DIR/scripts/dev_setup.py" --warm` when remote, then `exit 0`.
- [ ] `.claude/settings.json` -- register the hook with `"timeout": 900`.
- [ ] `docs/crosshatch/upstream-touches.md` -- the three Game paths.
- [ ] `AGENTS.md`:
  - the setup bullets become one pointer to the script (keep the 6.1.19 reason in a clause);
  - while testing, build `x4pro`, plus `default` when the diff touches code outside `FREEINK_CAP_GAMES`;
  - CI builds all five and gates the merge;
  - verification order: host tests and fast checks, then the review lens, then firmware;
  - `pio check -e x4pro` for games code;
  - lock paths `/tmp/crosshatch-build.lock` (pio, sim) and `/tmp/crosshatch-hosttest.lock` (host-test CMake).
- [ ] `docs/crosshatch/orchestrated-epics.md` -- the same envs, order, and lock paths; replace `{lock}` with the fixed paths.
- [ ] `.github/workflows/crosshatch-ci.yml` -- the `x4pro-static-analysis` job, named `x4pro static analysis`, and its `needs` entry.
- [ ] `src/games/GameHash.cpp` and `src/games/GamePackageInstaller.cpp` -- the two defect fixes.

**Acceptance Criteria:**
- Given this branch, when `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` runs, then it passes.
- Given a container with `~/.platformio` and the uv tools removed, when the hook runs with `CLAUDE_CODE_REMOTE=true`, then it exits 0. Then `pio run -e x4pro`, `./bin/clang-format-fix` and the host-test build work with no other setup, and the cold time is recorded in this plan.
- Given the hook just ran, when it runs again, then it finishes in under 30 s (warm builds excluded) and changes no CA bundle.

## Design Notes

- **Synchronous setup:** the container is cached after the hook, so a later session finds everything installed.
- **Detached warm builds:** they run after the cache point, so they help only the current session. They take the same fixed locks as agents, so an agent's first build waits for the warm one instead of colliding with it.
- **apt for `libpcre3`,** as CI does, instead of extracting it into scratch: no `LD_LIBRARY_PATH` to carry into `pio check`.

## Verification

**Commands:**
- `python3 scripts/dev_setup_test.py -v`, then every `scripts/*_test.py` -- expected: pass.
- With `~/.platformio`, `~/.local/share/uv/tools/pioarduino` and the clang-format tool moved aside, in a fresh `git archive` tree with submodules (`docs/crosshatch/orchestrated-epics.md` recipe): `time CLAUDE_CODE_REMOTE=true .claude/hooks/session-start.sh`, then a second run -- expected: exit 0 both times, and the times recorded.
- `pio check -e x4pro` (flags above), then `pio run -e x4pro` -- expected: pass.
- Host tests (AGENTS.md command) -- expected: all pass.
- `python3 scripts/check_upstream_touches.py` -- expected: PASS.
- `./bin/clang-format-fix` twice -- expected: nothing new.

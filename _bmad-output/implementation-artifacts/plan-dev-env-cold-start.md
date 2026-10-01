---
title: 'Faster cloud cold start and a shorter verification loop'
type: 'chore'
ticket: ''
created: '2026-10-01'
status: 'built'
route: 'full'
route_source: 'auto'
baseline_revision: 'a84f54b4ed291318fb84afeb848f51b2dd473a2a'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
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
- [x] `scripts/dev_setup.py` -- the steps, in order:
  1. submodules;
  2. uv tools;
  3. CA into the uv tool bundle;
  4. `pio pkg install -e x4pro -e default`, with the penv CA, the pin, and one retry;
  5. git: unshallow, the `upstream` remote, `fetch upstream develop`, and `merge.ours.driver`;
  6. system packages;
  7. `--warm`.

  The docstring gives the exit codes and the local usage.
- [x] `scripts/dev_setup_test.py` -- one test per matrix row, with fake `uv`/`pio`/`git`/`apt-get`/`flock` on PATH, a temp HOME and a temp CA. Assert the exit codes, the commands run, and that the CA is appended exactly once over two runs.
- [x] `.claude/hooks/session-start.sh` -- run `python3 "$CLAUDE_PROJECT_DIR/scripts/dev_setup.py" --warm` when remote, then `exit 0`.
- [x] `.claude/settings.json` -- register the hook with `"timeout": 900`.
- [x] `docs/crosshatch/upstream-touches.md` -- the three Game paths.
- [x] `AGENTS.md`:
  - the setup bullets become one pointer to the script (keep the 6.1.19 reason in a clause);
  - while testing, build `x4pro`, plus `default` when the diff touches code outside `FREEINK_CAP_GAMES`;
  - CI builds all five and gates the merge;
  - verification order: host tests and fast checks, then the review lens, then firmware;
  - `pio check -e x4pro` for games code;
  - lock paths `/tmp/crosshatch-build.lock` (pio, sim) and `/tmp/crosshatch-hosttest.lock` (host-test CMake).
- [x] `docs/crosshatch/orchestrated-epics.md` -- the same envs, order, and lock paths; replace `{lock}` with the fixed paths.
- [x] `.github/workflows/crosshatch-ci.yml` -- the `x4pro-static-analysis` job, named `x4pro static analysis`, and its `needs` entry.
- [x] `src/games/GameHash.cpp` and `src/games/GamePackageInstaller.cpp` -- the two defect fixes.

**Acceptance Criteria:**
- Given this branch, when `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` runs, then it passes.
- Given a container with `~/.platformio` and the uv tools removed, when the hook runs with `CLAUDE_CODE_REMOTE=true`, then it exits 0. Then `pio run -e x4pro`, `./bin/clang-format-fix` and the host-test build work with no other setup, and the cold time is recorded in this plan.
- Given the hook just ran, when it runs again, then it finishes in under 30 s (warm builds excluded) and changes no CA bundle.

## Design Notes

- **Synchronous setup:** the container is cached after the hook, so a later session finds everything installed.
- **Detached warm builds:** they run after the cache point, so they help only the current session. They take the same fixed locks as agents, so an agent's first build waits for the warm one instead of colliding with it.
- **apt for `libpcre3`,** as CI does, instead of extracting it into scratch: no `LD_LIBRARY_PATH` to carry into `pio check`.

## Review Triage Log

Pass 1 (thorough: blind hunter, edge-case hunter, verification gap, intent alignment; all four ran as context-free subagents and returned). 7 medium, 5 low patched; 1 verification redone; 3 deferred; the rest rejected.

- medium, patch: the hook sent the script's `error:` lines and its own failure line to stderr, and a SessionStart hook's stdout is what reaches the agent. The hook now runs it with `2>&1` and echoes to stdout; `HookTest` asserts it.
- medium, patch: `append_proxy_ca` appended in place, and both certifi bundles are hardlinks into uv's cache (link count 2, checked). It now writes a temp file and `os.replace`s it; there is a hardlink test.
- medium, patch: `build_lock` waited with no deadline inside the 900 s hook. It now polls `LOCK_NB` for up to 120 s, then fails the step and says to rerun; there is a busy-lock test.
- medium, patch: each resume queued another warm build behind a blocking `flock`. Warm builds now skip a held lock (checked first, then `flock -n`). Checked live: a second hook run skipped the running x4pro build. The tests now wait for spawned fakes, which also fixes the `state.json` race the blind hunter saw (low).
- medium, patch (verification gap): nothing pinned that warm output goes to the log; a fake marker now asserts it.
- medium, patch (verification gap): a failed install writing no stamp was unpinned; the test now asserts no stamp and a reinstall on the next run.
- medium, patch (verification gap): apt-get's update-and-retry path was untested; now tested with a no-lists fake.
- low, patch (verification gap): reinstalling a uv tool at another version was untested; now tested with clang-format 20.1.0.
- low, patch: one failed network fetch skipped setting `merge.ours.driver`. The local actions now come first.
- low, patch: apt-get ran with the hook's stdin and no `DEBIAN_FRONTEND`. Subprocesses now get `stdin=DEVNULL`, and apt-get runs noninteractive.
- medium, verification redone: the new gate's `pio check -e x4pro` had run in a tree that was already built. Rerun in a fresh `git archive` tree of 50701eae with submodule archives and no `.pio`: PASSED in 71.5 s.
- low, defer: AGENTS.md does not say which lock `pio run -t unit-tests` takes, though it writes `build/test` like the host-test lock's builds (agent-context file).
- low, defer: AGENTS.md does not say where the warm logs are (`~/.cache/crosshatch/warm-*.log`) or give a fallback when setup fails (agent-context file).
- medium, defer: review-before-firmware is prose only. A plain `/bmad-build` still runs every Verification command, firmware included, before step 4. Binding it needs a `_bmad/custom/bmad-build.toml` override, which is outside this intent's docs scope.
- low, reject: `prepare_penv` runs outside the lock when a penv exists. It writes only when the penv is wrong, and then a running build is already broken.
- low, reject: an `upstream` remote with another URL, `PLATFORMIO_CORE_DIR`, a damaged packages dir with a matching stamp, `platformio.local.ini` in the stamp key, a missing cmake/ninja preflight, `clang-format` order on PATH, and non-`SetupError` exceptions. All are rare in a cloud container, and their fixes add branches.
- low, reject: the CI setup steps are duplicated a third time. That matches the flash-budget job's existing convention.
- false: GameHash should value-initialize `context` instead of suppressing the warning. cppcheck names `finish`, not `context`, and the suppressed tree passes the check.
- false: "two locks diverge from 'a fixed build lock'". The plan's Tasks, approved by the user, specify both.
- false: "sticky-only code is not built locally". That is by intent: CI builds all five envs.
- not a finding (intent alignment): the end-to-end session time and the container cache's persistence are not measured in-session; the first new cloud session after merge measures them.

## Verification

**Commands:**
- `python3 scripts/dev_setup_test.py -v`, then every `scripts/*_test.py` -- expected: pass.
- With `~/.platformio`, `~/.local/share/uv/tools/pioarduino` and the clang-format tool moved aside, in a fresh `git archive` tree with submodules (`docs/crosshatch/orchestrated-epics.md` recipe): `time CLAUDE_CODE_REMOTE=true .claude/hooks/session-start.sh`, then a second run -- expected: exit 0 both times, and the times recorded.
- `pio check -e x4pro` (flags above), then `pio run -e x4pro` -- expected: pass.
- Host tests (AGENTS.md command) -- expected: all pass.
- `python3 scripts/check_upstream_touches.py` -- expected: PASS.
- `./bin/clang-format-fix` twice -- expected: nothing new.

**Results (2026-10-01, implementation session):**
- `python3 scripts/dev_setup_test.py -v`: 20 tests pass; every `scripts/*_test.py` passes with a `Ran <n>` line. Mutations
  (no penv patch before the retry, appending every certificate, no build lock, no stamp check) each fail a test.
- Base `pio check -e x4pro` (flags above) at bb3c0c62 reports exactly the two defects (`GameHash.cpp:33`
  `uninitMemberVar`, `GamePackageInstaller.cpp:296` `variableScope`); with the fixes it passes ("No defects found").
  `pio run -e x4pro` passes on the working tree.
- Cold start, measured: a shallow (`--depth 1`) `file://` clone of an uncommitted-work commit, so the git steps had a
  repository to unshallow (a `git archive` tree has none, and the git and submodule steps would fail there), with
  `~/.platformio` and the pioarduino and clang-format uv tools (and their `~/.local/bin` links) moved aside, the uv
  cache's certifi entry cleaned (an earlier in-place append had reached it through uv's hardlinks), `libpcre3`
  removed, and `freeink-sdk` empty. `time CLAUDE_CODE_REMOTE=true .claude/hooks/session-start.sh`: exit 0 in
  3 min 32 s. Every step ran: submodule checkout, both uv tools, 13 certificates into the tool bundle, the first
  `pio pkg install` failing TLS, 13 certificates and the pin into the penv, a passing retry, unshallow, `upstream`
  added and fetched, `libpcre3` installed, both warm builds started. Second run: exit 0 in 0.9 s, both certifi
  bundles byte-identical (md5), the install skipped by its stamp. The warm `pio run -e x4pro` then passed (6 min 11 s
  cold), the warm host-test build passed, and in that tree `ctest` passed 1363/1363, `./bin/clang-format-fix` (21.1.8
  from the uv tool) changed nothing, and `pio check -e x4pro` (the new CI job's command) passed.
- Found in the first cold attempt and fixed: a second hook run's `pio pkg install` beside the first run's warm build
  left `toolchain-xtensa-esp-elf` half copied (no `lib/`), and both warm x4pro builds failed ("Dynconfig for target
  esp32s3 is not exist"). The install also took 45-65 s on every run, because `-e default` reinstalls the C3 tools
  until a `default` build has run. The install now holds `/tmp/crosshatch-build.lock` and writes
  `~/.platformio/crosshatch-pkg-install.stamp` (envs, pioarduino version, platformio.ini hash), and a run whose stamp
  matches skips it. The warm logs are now appended to, not truncated under a running build.
- Host tests on the working tree: 1363/1363 pass. `python3 scripts/check_upstream_touches.py --ref <commit of the
  working tree>`: PASS. `./bin/clang-format-fix` twice: nothing new.

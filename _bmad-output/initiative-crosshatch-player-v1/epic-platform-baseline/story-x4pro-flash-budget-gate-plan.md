---
title: 'x4pro flash budget gate'
type: 'chore'
ticket: '3'
created: '2026-09-26'
status: 'built'
baseline_revision: 'de5c18a5b3e36f3f23aeb9d16d3f0668e41d4baf'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Nothing measures the spine's flash cap: the game runtime may add at most 250 KB to the x4pro image, and without a gate a PR can blow that budget unnoticed.

**Approach:** A fork-only workflow builds x4pro twice on the PR's commit, once as the normal `x4pro` env (games on) and once with `FREEINK_CAP_GAMES` removed through PlatformIO's `PLATFORMIO_BUILD_UNFLAGS` into a separate build dir (games off), proves the flag state of each build, compares the two `firmware.bin` sizes, writes both sizes and the difference to the job summary, and fails when the difference exceeds 250 KiB.

## Boundaries & Constraints

**Always:**
- The "on" build is exactly `pio run -e x4pro` (default build dir). The "off" build is the same command with `PLATFORMIO_BUILD_UNFLAGS=-DFREEINK_CAP_GAMES=1` and `PLATFORMIO_BUILD_DIR=.pio/build-games-off`, so both images coexist. No config file is added or edited.
- Flag state is proved per build from `pio project metadata` run with the same environment: the on build's defines contain `FREEINK_CAP_GAMES=1`, the off build's contain no `FREEINK_CAP_GAMES`. Otherwise exit 2 (a misspelled unflag or a flag removed from `x4pro` would make the gate pass vacuously).
- Limit: 250 KiB = 250 × 1024 = 256,000 bytes; fail when (on − off) > limit. The workflow holds the limit in one `env` value (`FLASH_BUDGET_KIB: 250`); the script also takes `--limit-kib` / `--limit-bytes` (any integer, zero or negative allowed) so the failing case can be run locally.
- Exit 0 pass, 1 over budget, 2 setup error (missing image or metadata, wrong flag state, pio failure).
- Toolchain steps copied from `ci.yml`'s `build` job (checkout with recursive submodules, Python 3.13, uv, pioarduino 6.1.19 + pinned deps, `~/.platformio` cache with the x4pro key, penv pin).
- Workflow named `.github/workflows/crosshatch-flash-budget.yml`, job `flash-budget` named `x4pro flash budget`, trigger `pull_request` to `develop`, `contents: read`.
- Stdlib Python only; no planning references in the script, test, or workflow.

**Never:**
- Editing `ci.yml`, `platformio.ini`, or any upstream file; moving the `freeink-sdk` pointer; `.skills/`.
- Making the job required (owner step) or touching any remote.
- Measuring against a fixed baseline instead of the same commit.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Within budget | on − off ≤ limit | summary with both sizes and difference; exit 0 | — |
| Over budget | on − off > limit (e.g. limit below today's difference) | same summary, "over budget" line; exit 1 | — |
| Shrink | off larger than on | negative difference shown; exit 0 | — |
| Flag not on | on metadata lacks `FREEINK_CAP_GAMES=1` | message naming the build; exit 2 | — |
| Flag not off | off metadata has any `FREEINK_CAP_GAMES` define | message; exit 2 | — |
| Missing output | no `firmware.bin` or metadata for a build | message naming the path; exit 2 | — |

</frozen-after-approval>

## Code Map

- `.github/workflows/ci.yml` -- `build` job steps to copy verbatim for the toolchain (not edited). Its cache key `pio-v3-${{ runner.os }}-x4pro-${{ hashFiles('platformio.ini') }}` is reused so both workflows share one package cache.
- `.github/workflows/crosshatch-upstream-ledger.yml` -- fork workflow style: header comment, `concurrency` per PR, `timeout-minutes`, a test step before the check.
- `scripts/check_upstream_touches.py` / `_test.py` -- script and test style (docstring with usage, `argparse`, `main()` returning an exit code, stdlib `unittest` that imports the module).
- `docs/crosshatch/upstream-touches.md` `## Game paths` -- fork-only paths; add the two new script paths (the workflow is already covered by `.github/workflows/crosshatch-*.yml`).
- `src/games/GamesBuildAnchor.cpp` -- the only `FREEINK_CAP_GAMES` user today; whole file under `#if`.
- PlatformIO 6.1.19 (`project/config.py`): `PLATFORMIO_BUILD_UNFLAGS` is appended to `[base]`'s `build_unflags` (not a replacement); `PLATFORMIO_BUILD_DIR` replaces `build_dir`. `pio project metadata -e x4pro --json-output --json-output-path F` gives `defines` after unflags and `prog_path` (the `firmware.elf`; `firmware.bin` sits beside it), about 16 s.
- Measured in planning: compile DBs (531 units) for normal x4pro vs the unflag build are identical except the one define and the build dir; `PLATFORMIO_BUILD_FLAGS=-UFREEINK_CAP_GAMES` also lands after the `-D` today but leaves both flags on every command and depends on SCons ordering, so it is not used.
- App slot `app0` = 0x640000 = 6,400 KiB (`partitions.csv`); x4pro baseline image 5,657,610 B (86.3 %).

## Tasks & Acceptance

**Execution:**
- [x] `scripts/check_flash_budget.py` -- stdlib CLI with subcommands `build on|off` (runs `pio run -e x4pro` with the env above, then `pio project metadata` into `<build dir>/x4pro/flash-budget-metadata.json`; pio failure → exit 2) and `compare [--limit-kib N | --limit-bytes N]` (reads both metadata files, checks flag state, locates `firmware.bin` beside `prog_path`, prints and appends a Markdown table to `$GITHUB_STEP_SUMMARY` when set: games on, games off, difference in bytes and KiB, limit, verdict; exit 0/1/2). Docstring explains the KiB choice and the local recipe -- the gate.
- [x] `scripts/check_flash_budget_test.py` -- stdlib `unittest` of `compare` on fixture metadata and fake images in a temp dir: every I/O-matrix row, default limit is 256,000 bytes, exact-limit passes, summary file written -- committed guard for the failure branches.
- [x] `.github/workflows/crosshatch-flash-budget.yml` -- header comment (what, why same-commit, how to lower the limit), `env: FLASH_BUDGET_KIB: 250` with the KiB definition, toolchain steps from `ci.yml`, then: run the test file, `build on`, `build off`, `compare --limit-kib "$FLASH_BUDGET_KIB"` -- the check the owner marks required.
- [x] `docs/crosshatch/upstream-touches.md` -- add `scripts/check_flash_budget.py` and `scripts/check_flash_budget_test.py` under Game paths.

**Acceptance Criteria:**
- Given HEAD, when `build on` and `build off` run locally, then both succeed, the on metadata has `FREEINK_CAP_GAMES=1` and the off metadata none, and preprocessing `GamesBuildAnchor.cpp` with each build's compile command shows the game headers included only in the on build.
- Given both images, when `compare` runs with the default limit, then it exits 0 and prints both sizes and a difference near zero; with a limit below the measured difference (`--limit-bytes -1` if the difference is 0), it exits 1.
- Given the workflow, when parsed with `yaml.safe_load`, then it parses; its build steps are the ones run locally; local build times give the CI job-time estimate.
- Given the commit, when `python3 scripts/check_upstream_touches.py` runs, then it exits 0; `./bin/clang-format-fix` leaves `git diff` empty.

## Implementation Notes

- Implemented directly (no subagent tool in this session). Files: `scripts/check_flash_budget.py`, `scripts/check_flash_budget_test.py`, `.github/workflows/crosshatch-flash-budget.yml`, `docs/crosshatch/upstream-touches.md` (two Game paths lines).
- Metadata lands in `.pio/flash-budget/{on,off}.json`; `compare` finds each `firmware.bin` beside the metadata's `prog_path` and refuses two builds that point at the same image. `build` deletes its old metadata first, so a failed build never leaves a stale file for `compare`. After review: setup errors also go to the job summary, a missing `pio` exits 2, the metadata JSON stays out of the log; incremental re-run of both builds and `compare` gave the same results (0 at 250 KiB, 1 at -1 B).
- Flag state on HEAD: on metadata defines `FREEINK_CAP_GAMES=1`, off metadata none. Preprocessing `GamesBuildAnchor.cpp` with each build's compile command: on defines the macro and pulls in the three game headers (3 `libraryName` declarations); off defines nothing and includes none. Compile DBs (531 units) are identical apart from that define and the build dir.
- Sizes on HEAD: `firmware.bin` 5,662,624 B in both builds (difference 0; 71 bytes differ, the app descriptor's ELF hash); pio's "Flash used" line is 5,657,610 B of 6,553,600 (86.3 %), the figure quoted earlier as the baseline. `compare` exits 0 at 250 KiB, 0 at `--limit-kib 0` (0 ≤ 0), and 1 at `--limit-bytes -1` / `FLASH_BUDGET_KIB=-1`, since the measured difference is exactly 0.
- Ticket `unknown` (job time): locally, 4 cores and an empty object cache, `build on` took 226 s (`pio run` 210 s + metadata 16 s) and `build off` 223 s; about 7.5 min of builds. On a 4-vCPU `ubuntu-latest` runner, with setup and a warm package cache, expect roughly 10 to 12 min; a 2-vCPU runner roughly doubles the build part. `timeout-minutes: 60`.
- `PLATFORMIO_BUILD_FLAGS=-UFREEINK_CAP_GAMES` was also tried: the `-U` lands after the `-D` in all 531 commands, so it works today, but it rests on SCons flag ordering; not used.

## Plan Change Log

## Review Triage Log

Pass 1 (lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: high 0, medium 0, low 21, false 2, maybe-false 0. Patches 9 (rows 1, 4, 5, 8, 10, 13, 14, 18, 19), deferred 0, loopbacks 0.

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | blind | 250 set in both the workflow env and the script default; they can drift | low | patch | Test now asserts the workflow's `FLASH_BUDGET_KIB` equals `DEFAULT_LIMIT_KIB`. |
| 2 | blind | No check that the on image still fits the app slot | false | reject | `pio run` already fails when the image exceeds `board_upload.maximum_size`, so the on build step fails first. |
| 3 | blind | RAM growth not measured | low | reject | Intent is the flash cap only; RAM costs are governed elsewhere in the spine. |
| 4 | blind | `firmware.bin` name hardcoded | low | patch | Image path now `prog_path.with_suffix('.bin')`. |
| 5 | blind | Stdlib tests run after the toolchain install | low | patch | Step moved right after `setup-python`. |
| 6 | blind | Toolchain setup duplicated from `ci.yml`; a composite action would share it | low | reject | Copying is the orchestrator's instruction (the release workflow copies it too); a composite action adds new shared surface. |
| 7 | blind | No `paths` filter, so docs-only PRs pay for two builds | false | reject | A path-filtered required check stays pending on skipped PRs and blocks merging; the job is meant to be required. |
| 8 | blind, edge, intent | Setup errors (exit 2) leave the job summary empty | low | patch | `main` appends "The check could not run: ..." to `$GITHUB_STEP_SUMMARY`; tested. |
| 9 | blind | Unflag test reads only `[env:x4pro]`, not `extends` | low | reject | A moved define fails that test loudly; the metadata check is the real guard. |
| 10 | blind | Test name contradicts its zero-limit assertion | low | patch | Renamed `test_zero_difference_passes_at_zero_limit_and_fails_below`. |
| 11 | blind | `.pio/build-games-off` and `.pio/flash-budget` left behind | low | reject | Both under gitignored `.pio`; negligible. |
| 12 | blind | `build` success path untested with a fake pio | low | reject | Covered by the local real builds and every CI run; a recording fake adds test complexity. |
| 13 | edge | pio missing from PATH raises an uncaught `FileNotFoundError` (exit 1, reads as over budget) | low | patch | `pio()` maps `OSError` to `SetupError`; `PATH=/usr/bin:/bin ... build on` exits 2; tested. |
| 14 | edge | `prog_path` null raises outside the try | low | patch | Path built inside the try (with row 4); tested. |
| 15 | edge | mkdir/unlink or summary-write `OSError` exits 1 | low | reject | Unlikely on a runner; guards add branches. |
| 16 | edge | Stale `firmware.bin` older than its ELF measured | low | reject | Unreachable: metadata is written only after `pio run` succeeds, which rewrites the image, and `build` deletes old metadata first. |
| 17 | edge | Metadata saved at `.pio/flash-budget/{on,off}.json`, not `<build dir>/x4pro/flash-budget-metadata.json` as the task says | low | reject | Deliberate: `compare` needs the metadata before it knows either build dir; recorded in Implementation Notes. The fix would be a plan edit. |
| 18 | gap-other | `--json-output` also dumps the whole metadata into the job log | low | patch | Verified in `metadata.py`; the metadata call's stdout is discarded and a one-line "Saved" is printed (keeps `--json-output` so no dependency install runs). |
| 19 | gap-other | Comment says PlatformIO drops only an identical flag; a bare name also matches | low | patch | Verified `ProcessUnFlags`' `unflag == current[0]` branch; comment corrected. |
| 20 | intent | "250 KB" vs "250 KiB" readings | low | reject | Frozen Approach and Boundaries choose KiB (256,000 B); Design Notes give the reason. |
| 21 | intent | Builds the PR merge commit, not the head SHA | low | reject | Both builds use one tree, the property the budget needs; `ci.yml` builds the same ref. |
| 22 | intent | Flag proof comes from resolved config, not the image | low | reject | Local compile-DB and preprocessing checks tie config to compiled code; symbol checks are meaningless while the libraries are unreferenced. |
| 23 | intent | Merge blocking needs branch protection | low | reject | Owner step (hitl). |
| - | gap | No verification gaps found | - | - | - |

## Design Notes

KiB, not 1,000 bytes: ESP32 flash, the partition table, and the app slot are sized in binary units (app slot 0x640000 = 6,400 KiB exactly), and the spine's baseline percentage is of that slot. The decimal reading would be 6,000 bytes stricter.

Decision (ticket `unknown`, flag off without `platformio.ini`): `PLATFORMIO_BUILD_UNFLAGS` removes the define with PlatformIO's documented mechanism, leaving every other compile flag identical, and `PLATFORMIO_BUILD_DIR` keeps the two images apart; the metadata check makes the proof part of every run. Decision (job time): record local on/off build times and the metadata overhead in Implementation Notes.

## Verification

**Commands:**
- `python3 scripts/check_flash_budget_test.py -v` -- expected: all pass.
- `time python3 scripts/check_flash_budget.py build on` and `... build off` (logs in scratchpad) -- expected: exit 0; times recorded.
- `python3 scripts/check_flash_budget.py compare`, then `--limit-bytes <below diff>` -- expected: 0, then 1.
- anchor preprocess with each build's compile command (`-E`) -- expected: `GameCore.h` content only in on.
- `python3 -c 'import yaml,sys; yaml.safe_load(open(sys.argv[1]))' .github/workflows/crosshatch-flash-budget.yml` -- expected: no error.
- `python3 scripts/check_upstream_touches.py` after commit -- expected: PASS (exit 0).
- `./bin/clang-format-fix && git diff --exit-code` -- expected: clean.

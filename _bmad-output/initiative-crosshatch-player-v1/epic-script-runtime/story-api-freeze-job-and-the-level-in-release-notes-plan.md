---
title: 'API freeze job and the level in release notes'
type: 'feature'
ticket: '5'
created: '2026-09-27'
status: 'built'
baseline_revision: 'b4b8631a18459d33a47bdf974285b3a3cc10e4db'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context: ['{project-root}/docs/crosshatch/fork-scripts.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Nothing stops a PR from changing a frozen API level's list or un-freezing `API_LEVEL_FROZEN` (AD-19); `fork_release.py` reads the version vectors from the workflow's commit instead of the released one (AD-25) and never names the API level in the notes or guards publishing after the freeze; and `fr build` checks images only after every `pio run`, so PlatformIO's clean-on-checksum can erase an earlier env's image (retro AI-3).

**Approach:** A new fork script `scripts/check_api_freeze.py` and a `crosshatch-ci.yml` job `API freeze` (in `Crosshatch Test Status` `needs`) compare `ApiLevel.h` and the frozen levels' lists between the merge base and HEAD. `fork_release.py` takes the vectors and `ApiLevel.h` from `--project-dir`/`--repo-dir`, prints the level in the notes, refuses in preflight a preview once a frozen release tag exists, and `build` checks and copies each image right after its own `pio run`.

## Boundaries & Constraints

**Always:** `fork_common` exit contract and stdlib only; the `ApiLevel.h` reader lives in `fork_common.py` (two scripts need it) and reads 2.4's one-line define shape. The freeze script and its test are added at the end of the ledger's Game paths list. Preflight rules that only warn in a dry run (as the ref rule does) warn here too. Fresh-clone run of the new job and of the release steps that run locally (retro AI-8).

**Never:** Edit upstream's `ci.yml`, `lib/GameScript/`, `test/game_script/`, `scripts/game_codec*`, `docs/crosshatch/formats.md` (story 2.6), `ApiLevel.h` or `api-level-1.txt` content. No release title change in the publish job. No second build directory scheme.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| No level at base | merge base has no `ApiLevel.h` | exit 0, "nothing is frozen" | — |
| Preview change | base L1 not frozen, HEAD edits `api-level-1.txt` | exit 0 | — |
| Frozen list changed | base L1 frozen (or base L2), HEAD changes or deletes `api-level-1.txt` | exit 1 naming the file | — |
| Flag flip | base L1 frozen, HEAD L1 not frozen (or lower level, or header gone) | exit 1 | — |
| Next level opened | base L1 frozen, HEAD L2 preview, list 1 unchanged | exit 0 | — |
| Bad header | define missing, duplicated, or malformed | exit 2 | SetupError |
| Notes | plan from a commit with L1 not frozen / frozen / no header | "Game API 1 (preview)" / "Game API 1" / "No game API" line | — |
| Frozen release exists | a `-ch.N` tag whose commit has `API_LEVEL_FROZEN true`, released commit not frozen | publish: exit 1; dry run: warning, exit 0 | — |
| Vectors | released checkout's vectors differ from the tools copy | prepare and build use the released checkout's | missing file: exit 2 |
| Clean-on-checksum | a later `pio run` wipes `.pio/build` | every image already checked and in `--dist`; exit 0 | — |

</frozen-after-approval>

## Code Map

- `scripts/fork_release.py` -- `DEFAULT_VECTORS` (l.54, tools dir) and global `--vectors` (l.655); `load_rules` callers `prepare` (l.395) and `check_images` (l.486); `preflight` (l.278, dry-run warning pattern l.283-287); `build` (l.446) and `check_images` (l.485) merge into one `build --dist`; `render_notes` (l.601); docstring command list (l.9-36).
- `scripts/fork_release_test.py` -- `TempProject` (git repo with `platformio.ini`) needs the vectors and `ApiLevel.h` for prepare; `ImageTest.test_check_images_...` becomes a build test with a fake `pio`; `RefTest.preflight` for the frozen-tag rule; `PublishTest` for the notes line.
- `scripts/fork_common.py` / `_test.py` -- add `API_LEVEL_HEADER`, `ApiLevel(level, min_level, frozen)`, `parse_api_level(text)`; document in docstring and `docs/crosshatch/fork-scripts.md` table.
- `scripts/check_upstream_touches.py` -- CLI and `resolve` pattern to copy (`--ref`, base ref, merge-base, exit 2 on unknown ref).
- `.github/workflows/crosshatch-ci.yml` -- header bullets, `upstream-ledger` job (fetch-depth 0, system python3) as the job model, `crosshatch-test-status.needs`.
- `.github/workflows/crosshatch-release.yml` -- comment l.51-52; steps "Build the release envs" and "Check the images" (l.139-143); `src` checkout has `fetch-depth: 0`, so tags are present for preflight.
- `docs/crosshatch/upstream-touches.md` -- Game paths list ends at `.github/workflows/crosshatch-*.yml`; append after it.
- PlatformIO `clean_build_dir` (`platformio/run/helpers.py`): each `pio run` hashes the config plus the list of `.c/.h/...` paths under `src`, `include`, `lib`, and removes the whole `.pio/build` on a mismatch; the pre-scripts `gen_i18n.py` and `build_html.py` create gitignored headers during the first build on a fresh tree.

## Tasks & Acceptance

**Execution:**
- [x] `scripts/fork_common.py`, `scripts/fork_common_test.py` -- `parse_api_level` and constants, with tests for good, missing, duplicate, and malformed defines.
- [x] `scripts/check_api_freeze.py`, `scripts/check_api_freeze_test.py` -- the freeze rule per Design Notes; tests over throwaway repos for every matrix row of the job, exit codes 0/1/2.
- [x] `scripts/fork_release.py`, `scripts/fork_release_test.py` -- vectors and `ApiLevel.h` from the released checkout; plan `api`; notes line; frozen-release preflight rule; `build --dist` checking each image after its own run (old image removed first); tests incl. a fake `pio` that wipes `.pio/build` on every run.
- [x] `.github/workflows/crosshatch-ci.yml` -- job `api-freeze` (`API freeze`), header bullet, `needs`.
- [x] `.github/workflows/crosshatch-release.yml` -- fix l.51-52 comment; build step passes `--dist`, drop "Check the images".
- [x] `docs/crosshatch/upstream-touches.md`, `docs/crosshatch/fork-scripts.md` -- two Game paths at the end; `fork_common` table rows.

**Acceptance Criteria:**
- Given the commit, when every `scripts/*_test.py` runs, then all pass.
- Given a fresh clone of the commit, when the `API freeze` step's command runs against `origin/develop`, then it exits 0; with a scratch base commit that freezes level 1 and a HEAD that edits its list, it exits 1.
- Given a fresh clone with submodules, when preflight (dry run), prepare, `pio pkg install` per env, `build --dist`, pack-games, and notes run as the workflow runs them, then each exits 0, both images are in `dist`, and the notes contain "Game API 1 (preview)".
- Given that run, when an env's `.pio/build` image has been erased by a later env's `pio run` (natural or forced), then the baseline's separate `check-images` fails on that tree while the new `build` passed.

## Implementation Notes

- Implemented directly (no coding subagent in this session). Files: `scripts/fork_common.py` (+`_test.py`, 23 tests), `scripts/check_api_freeze.py` and `_test.py` (new, 10 tests over a throwaway repo), `scripts/fork_release.py` (+`_test.py`, 66 tests), `.github/workflows/crosshatch-ci.yml`, `.github/workflows/crosshatch-release.yml`, `docs/crosshatch/upstream-touches.md`, `docs/crosshatch/fork-scripts.md`.
- `check-images` is gone: `build --dist` runs `pio run`, checks, and copies each env's image before the next env builds, and deletes a leftover image before each run; the workflow's "Check the images" step is folded into "Build and check the release envs". `--vectors` now defaults to `<project-dir>/test/game_core/fork_version_vectors.json`; `DEFAULT_VECTORS` became `VECTORS_PATH` (relative).
- The notes' first line is `<tag> · Game API <n>` plus ` (preview)` while not frozen (the spine's example), followed by one sentence saying a preview may change; a commit without `ApiLevel.h` says "No game API". The job summary carries the same text, which is where the owner's dry run shows it.
- AI-3 finding (answers the retro's open question): on a fresh tree the headers that `gen_i18n.py` and `build_html.py` generate are already part of the stored `project.checksum` once the first env's `pio run` finishes (recomputed with PlatformIO's `compute_project_checksum` after the run: `e94c3071…`, equal to the stored value), so the second env's run keeps `.pio/build`; `pio pkg install` generates nothing and creates no `.pio/build`. That is why release runs 2 and 3 kept both images. Any other checksum change between two env runs (a new source or header, a config change) still removes the earlier image; the forced run in the Verification record shows the old flow failing and the new one passing.
- The 2.2 deferred gap "no test asserts `fr notes` writes its job-summary section" is now covered by `PublishTest.test_notes_go_to_the_file_and_the_job_summary`; the `deferred-work.md` entry is left for the orchestrator to close.
- No C/C++ changed, so no clang-format or firmware env build applies beyond the release envs built below.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time, its prompt read fresh, judged against the diff only). Diff 55.9 kB, blind floor 8. Verdicts: high 0, medium 0, low 7, false 4, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind, edge | `read_api_level` decodes strictly, so a non-UTF-8 `ApiLevel.h` escapes `main`'s handler as a traceback | low | Patched: `errors='replace'`, as the git-read paths already do; the define regex then fails loudly (exit 2). |
| 2 | blind | The frozen-release rule reads tags from the checkout; a checkout without tags would pass it silently | low | Rejected: the workflow's `src` checkout uses `fetch-depth: 0`, which fetches every tag (actions/checkout refspec for full history); the workflow comment now states the reliance; a second tag source adds a CLI input. |
| 3 | blind | Preflight reads the worktree's `ApiLevel.h`, not `HEAD`'s | low | Rejected: `prepare` reads the same worktree file, and CI's checkout is clean. |
| 4 | blind | A frozen level whose list is absent at the merge-base is skipped | low | Rejected: unreachable (2.4 ships the header and list together; freezing a level without its list cannot happen through the list's own test). |
| 5 | blind | A define with a trailing comment is reported as "no one-line #define" | low | Rejected: the header documents that shape; the message names the define and the rule, exit 2. |
| 6 | blind | The publish job still sparse-checks-out `test/game_core` | low | Rejected: pre-existing, harmless; not touched by this change. |
| 7 | edge | `freeze_problems` names the lexically first frozen tag, not the earliest release | low | Rejected: cosmetic; any frozen tag triggers the same rule. |
| 8 | blind | The `·` in the notes can fail on an ASCII stdout | false | Python 3.7+ coerces the C locale to UTF-8 (PEP 538/540); GitHub runners use C.UTF-8; files are written as UTF-8. |
| 9 | blind | `build` leaves a partial `dist` when a later env fails | false | The run fails at that step and the artifact upload step runs only on success. |
| 10 | edge (deletion) | Removing `check-images` drops its image checks and "missing image" error | false | Both moved into `build` (same `image_problems`, exit 2 for a missing image); `BuildTest` covers good, bad, missing, and stale images; no other caller (repo grep). |
| 11 | verification-gap | Workflow wiring (`--base-ref "origin/$GITHUB_BASE_REF"`, `build --dist`) has no test | false | Static workflow text; source-text tests would not count; covered by the fresh-clone runs (Verification). |
| 12 | intent | Readings: "turns FROZEN from true to false" literally vs. "the frozen levels shrink"; the diff implements the second, which lets level 2 open as a preview (AD-19); "refuse to publish" applies to publishing runs, dry runs warn; `build` checks per env rather than per-env build dirs | -- | Descriptive only; recorded in Design Notes. |

## Design Notes

**Frozen top.** `API_LEVEL_FROZEN` describes `API_LEVEL` only (AD-19), so the highest frozen level is `API_LEVEL` when the flag is true, else `API_LEVEL - 1`. The job fails when HEAD's frozen top is below the base's (the flag turned back, the level lowered, or the header removed) and when any `api-level-<n>.txt` for n ≤ the base's frozen top differs in bytes (or is missing) at HEAD. Opening level 2 as a preview after level 1 froze keeps the frozen top at 1 and passes, as AD-19's "any addition opens the next level as a preview" needs.

**Frozen release.** Preflight lists the released checkout's tags (the workflow's `fetch-depth: 0` checkout carries them) that match `-ch.N`, reads `ApiLevel.h` at each with git, and treats a tag whose header says frozen as a frozen release (the ticket's unknown). A tag or released commit without the header counts as not frozen, so an older commit can still be released until the freeze.

**AI-3.** Checking each image right after its own run and copying it to `--dist` makes any later wipe harmless; the notes read only the plan's firmware list, filled by `build`.

## Verification

**Commands:**
- `for t in scripts/*_test.py; do python3 "$t" || exit 1; done` -- all pass.
- Fresh clone (`git clone` of the main tree, `checkout <commit>`, submodules): `python3 scripts/check_api_freeze.py --base-ref origin/develop` -- exit 0; scratch frozen base -- exit 1.
- Same clone, under the shared flock: `fork_release.py preflight --dry-run true ...`, `prepare`, `pio pkg install -e <env>` per env, `build --dist`, `pack-games`, `notes` -- exit 0, notes line present; baseline `check-images` against the tree -- fails when an image was erased.
- `python3 scripts/check_upstream_touches.py` -- exit 0.

**Manual checks:**
- Owner (pending): Actions → "Fork release" → Run workflow from this branch with "Dry run" on → the run summary's notes show "Game API 1 (preview)".

**Verification record (2026-09-27, Python 3.11.15, git 2.43.0, pioarduino 6.1.19; builds under the shared flock):**
- Sidecar tests: all five `scripts/*_test.py` files pass (`check_api_freeze_test.py` 10, `check_flash_budget_test.py` 63, `check_upstream_touches_test.py` 18, `fork_common_test.py` 23, `fork_release_test.py` 66). Matrix audit: every row has a passing test (freeze rows in `CheckScriptTest`; notes in `PublishTest`; frozen release in `FreezeTest`; vectors in `PrepareTest`/`BuildTest`; clean-on-checksum in `BuildTest.test_a_later_run_that_cleans_the_build_dir_loses_no_image`).
- Fresh clone (retro AI-8) of commit `13bdac47e43a2292d02d990fe14dd2c10afdd223` (`git clone` of the main tree, `checkout`, `git submodule update --init --recursive`; no `.pio`, no `platformio.local.ini`), in `<scratch>/s2-5/fresh`:
  - API freeze step, `python3 scripts/check_api_freeze.py --base-ref "origin/$GITHUB_BASE_REF"` with `GITHUB_BASE_REF=develop`: exit 0 ("no lib/GameCore/ApiLevel.h" at the merge-base). Fork script tests step: exit 0. In a second scratch clone: a commit freezing level 1 as the base and a HEAD appending to `api-level-1.txt` exit 1 ("changed, but level 1 is frozen"); turning the flag back exits 1; a preview edit exits 0.
  - Release build job, steps as the workflow runs them: `preflight --dry-run true` (upstream `release.yml`/`release_candidate.yml` given as disabled, as the owner's settings make them) exit 0 with the two ref warnings; `prepare` with the clone's tags (`1.6.5-ch.1`, `ch.2`) exit 0, tag `1.6.5-ch.3`, "Game API 1 (preview)"; `pio pkg install -e` per env exit 0; `build --dist` exit 0, both images checked and copied (sticky 5,630,672 B, x4pro 5,742,944 B); `pack-games` exit 0 (no games); `notes` exit 0, first line `1.6.5-ch.3 · Game API 1 (preview)`, also in the job summary file.
  - AI-3, forced clean-on-checksum: `build --dist` again with a `pio` wrapper that adds `src/zz_checksum_probe.h` just before `pio run -e x4pro-gh_release` (the checksum change PlatformIO cleans on): exit 0, both images in `dist`, and `.pio/build/sticky-gh_release/firmware.bin` gone afterwards. The baseline (`b4b8631a`) `check-images` on that same tree exits 2: "cannot read .pio/build/sticky-gh_release/firmware.bin", which is the erasure the old build-then-check flow would have shipped as a failed release. The probe header was removed afterwards.
- Code after the review patch (`read_api_level` decodes with `errors='replace'`), commit `1abd64e4470fe33032d2becaa988bc72aa5df37e`, fresh clone: Fork script tests step exit 0, API freeze step exit 0, `preflight --dry-run true` exit 0, `prepare` exit 0 with "Game API 1 (preview)"; the build steps' code is unchanged from `13bdac47` apart from that patch. The final commit differs from `1abd64e4` only in this line.
- `python3 scripts/check_upstream_touches.py` on the commit: PASS (every changed path is a Game path or `_bmad-output/`).

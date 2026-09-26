---
title: 'Fork release workflow and first release'
type: 'feature'
ticket: '7'
created: '2026-09-26'
status: 'built'
baseline_revision: '311e4bb43812e4ff7d6fe56b53f2de99e9549669'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/reviews/review-ad25-adversary.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Game builds now update from the fork's own releases, but nothing makes a fork release: no tag numbering, no image that reports its own tag, no check that upstream's release workflows are off, and no packed games.

**Approach:** One fork-only `workflow_dispatch` workflow, `.github/workflows/crosshatch-release.yml`, doing AD-25's "Release workflow" bullet in order, with its logic in a stdlib script `scripts/fork_release.py` (unit-tested, run as an early step) and a thin YAML of `gh api` / `curl` calls; plus AD-25's upstream-merge dry-run rule beside ledger row 10.

## Boundaries & Constraints

**Always:**
- Order (AD-25): (1) the ref check (publish: dispatched from `develop`, released commit equal to or an ancestor of `origin/develop`; dry run: warn only) and the active-workflow check; (2) `N` = 1 + the largest integer after `-ch.` in any tag (API, paginated), 1 when none; (3) version line check, rewrite in the checkout only, resolved-config check, build; (4) image checks; (5) pack games; (6) publish: tag ref created via API without force (fails if it exists), draft release with `GITHUB_TOKEN`, upload every asset, verify the uploaded set, notes list each package hash, `PATCH draft=false, make_latest=true`.
- Active-workflow check: states from `GET /actions/workflows` (path, state); trigger text from the files in both the released commit and the workflow's own commit. A file counts as active unless the API lists its path with a non-`active` state. Fail when any file other than the running workflow (from `GITHUB_WORKFLOW_REF`) has `release` as a direct child of its top-level `on` (block, list, or inline form), has no readable `on`, or mentions `gh_release` anywhere.
- Tag and asset rules come from `test/game_core/fork_version_vectors.json` (`tag_grammar` with `re.fullmatch`, `max_tag_length`, `release_url`, `upstream_release_url_fragment`, `asset_names`); the script's asset formatter is checked against `asset_names` at run time and in tests; a name must fit OtaUpdater's 48-byte buffer.
- Version line: exactly one `version =` in `[crosspoint]` of `platformio.ini`, matching `[0-9]+.[0-9]+.[0-9]+` and equal to `pio project config`'s `crosspoint.version`; the tag must match the grammar. Fail when `PLATFORMIO_BUILD_FLAGS`, `PLATFORMIO_BUILD_UNFLAGS`, or `PLATFORMIO_SRC_BUILD_FLAGS` is set.
- Release envs: from `pio project config --json-output` after the rewrite, every `env:<board>-gh_release` (non-empty board) whose build_flags contain `-DFREEINK_CAP_GAMES=1`; empty list fails; each has exactly one `CROSSPOINT_VERSION` define, equal to `\"<tag>\"`.
- Image checks on `.pio/build/<env>/firmware.bin` (the app image OtaUpdater streams into an OTA slot, as `release.yml` uploads): `0xE9` at 0 and the app-descriptor magic `32 54 CD AB` at 32; `<tag>\0` not preceded by a digit, letter, or `.`; `release_url\0` present; upstream fragment absent; exactly one `CROSSPOINT-BOARD-V1:` and it names `<board>;`. Copied to `crosspoint-<tag>-<board>.bin`.
- Games: every directory under `games/` in the released commit; none → no-op; any with `scripts/pack_game.py` missing → fail. Contract: `python3 scripts/pack_game.py games/<id> <out-dir>` exits 0, writes `<out-dir>/<id>.cpgame`, and prints the package hash (16 lowercase hex) as its last stdout line; `games/` must be unchanged after packing.
- `dry_run` input defaults to `true`; `ref` input (optional) picks the commit. One concurrency group, `cancel-in-progress: false`. Build job `contents: read, actions: read`; publish job alone `contents: write`, runs only when `dry_run` is false, and rechecks `N` against a fresh tag list first.
- Tooling (script, vectors) runs from the workflow's own commit; the build uses the released commit, so a rollback to an older good commit works.
- Toolchain steps copied from `crosshatch-flash-budget.yml`/`ci.yml`. New script and test listed under Game paths. No planning references in script, test, or workflow.

**Never:** edit `release.yml`, `release_candidate.yml`, `ci.yml`, `platformio.ini`, or any other upstream file (the rewrite exists only in the runner's checkout); commit a rewritten version line; add a token secret or deploy key; fork prereleases; publish, tag, or change repository settings from here.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| First release | no `-ch.` tag, version `1.6.5` | tag `1.6.5-ch.1` | — |
| Gaps and odd tags | `1.6.5-ch.3`, `v1.6.5-ch.8`, `1.6.5-ch.08`, `v1.5.0` | N 9 | — |
| Tag too long / N ten digits | version `1234567.1234567.123` or max N 999999999 | no tag | exit 1 |
| Bad version line | `1.7.0-dev`, missing, duplicated, `${...}` | — | exit 1 |
| Upstream release workflows active | today's `release.yml`, `release_candidate.yml` | both named | exit 1 |
| Both disabled | states `disabled_manually` | pass | — |
| Unknown to the API | file in checkout, no API entry | treated active | exit 1 if it matches |
| No flagged env | games flag missing from `*-gh_release` | — | exit 1 |
| Image lacks tag / has upstream URL / wrong board / merged image | — | — | exit 1 naming env |
| Games | none / present, no packer / packer fails / bad hash line | no-op / fail / fail / fail | exit 1 |
| Publish, ref off develop | dispatched on another branch, or non-ancestor commit | dry run warns; publish fails | exit 1 |

</frozen-after-approval>

## Code Map

- `.github/workflows/release.yml` -- uploads `.pio/build/<env>/firmware.bin` as `crosspoint-<tag>-<device>.bin`; `on: release`; the C3 env is plain `gh_release`. Not edited.
- `.github/workflows/crosshatch-flash-budget.yml` -- toolchain steps and header style to copy.
- `lib/GameCore/ForkRelease.h` -- `formatAssetName`, `MAX_TAG_LEN`; `src/network/OtaUpdater.cpp` -- `assetName[48]`, writes the downloaded image into an OTA partition (app image).
- `src/network/FirmwareBoardTag.cpp` -- `CROSSPOINT-BOARD-V1:<board>;` once in each image; `x4pro`, `sticky`.
- `test/game_core/fork_version_vectors.json` -- the grammar (`[.]`, no backslashes) and vectors.
- `platformio.ini` -- `[crosspoint] version = 1.6.5`; `x4pro-gh_release`, `sticky-gh_release` set `FREEINK_CAP_GAMES=1` and `-DCROSSPOINT_VERSION=\"${crosspoint.version}\"`; `pio project config --json-output` returns `[[section, [[key, value]...]]...]` with build_flags as a list.
- `scripts/check_flash_budget.py` / `_test.py` -- script and test style (docstring, argparse subcommands, exit codes, `SetupError`).
- `docs/crosshatch/upstream-touches.md` -- `parse_ledger` reads only `|` rows and `- `/`* `/`+ ` bullets, so a prose paragraph under the table is safe; its tests assert 10 rows.

## Tasks & Acceptance

**Execution:**
- [x] `scripts/fork_release.py` -- subcommands `preflight`, `prepare` (N, version, rewrite, config, envs, assets → `plan.json`, `GITHUB_OUTPUT`), `build`, `check-images`, `pack-games`, `notes` (asset SHA-256s, package hashes; also step summary), `recheck`, `expected-assets`; exit 0 pass, 1 rule broken, 2 setup error.
- [x] `scripts/fork_release_test.py` -- stdlib `unittest` over every matrix row, the detector on fixture texts and on the repo's own workflows, the asset formatter against the vectors, image checks on synthetic images, packing with fake packers in a temp dir.
- [x] `.github/workflows/crosshatch-release.yml` -- the workflow above; header says how to run it and what is owner-only.
- [x] `docs/crosshatch/upstream-touches.md` -- dry-run rule beside row 10; two Game paths entries.

**Acceptance Criteria:**
- Given the unit tests and `check_upstream_touches_test.py`, when run, then all pass.
- Given the repo's workflows with every state `active`, when `preflight` runs, then it fails naming `release.yml` and `release_candidate.yml`; with those two disabled, it passes.
- Given `prepare` on this tree with a fake tag list, then it prints the expected tag and envs `sticky-gh_release`, `x4pro-gh_release`; `platformio.ini` is restored afterwards.
- Given `x4pro-gh_release` and `sticky-gh_release` built with the version line rewritten to `1.6.5-ch.1`, when `check-images` runs, then both pass; on a dev `x4pro` image it fails.
- Given the commit, when `check_upstream_touches.py` and `clang-format-fix` run, then PASS and no diff; the workflow parses with `yaml.safe_load`.

## Implementation Notes

- Implemented directly (no subagent tool in this session). Files: `scripts/fork_release.py`, `scripts/fork_release_test.py`, `.github/workflows/crosshatch-release.yml`, `docs/crosshatch/upstream-touches.md`.
- `fork_release_test.py`: 50 tests pass (48 before review patches), also run per PR by `crosshatch-upstream-ledger.yml`; `check_upstream_touches_test.py` 18 pass (doc still parses to 10 rows).
- `preflight` on this repo, every state `active`: exit 1 naming `release.yml` (triggers on `release`, mentions gh_release) and `release_candidate.yml` (mentions gh_release); with those two `disabled_manually`: exit 0 (9 workflow files). Off-develop dispatch and a non-ancestor commit: warnings in a dry run, exit 1 when publishing.
- `prepare` with tags `v1.5.0 1.6.0rc 1.6.5rc`: tag `1.6.5-ch.1`, envs `sticky-gh_release x4pro-gh_release`, derived from `pio project config`. `build` (both envs, 12 min on 4 cores, sticky 7.3 min of it) then `git checkout -- platformio.ini`; tree clean afterwards.
- `check-images` on both real images: pass. `strings` shows the version only as the tail of the `CrossPoint-ESP32-1.6.5-ch.1` user agent (the linker merges the standalone literal into it), which is why the tag check allows a `-` before the tag. Negative checks on the real x4pro image: tag `1.6.5-ch.2` fails, board `sticky` fails, `firmware.factory.bin` (merged image) fails on the missing app descriptor. A dev x4pro image was not rebuilt; the unrewritten case is the wrong-tag case (image reports `1.6.5`).
- `pack-games` with no `games/`: no-op; `notes` and `recheck` on the plan: exit 0.
- Asset naming: the release uploads the app image `firmware.bin`, as `release.yml` does and as `OtaUpdater::installUpdate` needs (it streams the download into an OTA partition); board names equal `board_tag::boardName()` because each image's `CROSSPOINT-BOARD-V1:<board>;` tag is checked against the env's board.
- Workflow YAML parses with `yaml.safe_load`.

## Plan Change Log

## Review Triage Log

Pass 1 (lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment; run as headless sessions, no subagent tool). Counts: high 0, medium 1, low 13, false 2, maybe-false 0. Patches 9, deferred 0, loopbacks 0.

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | gap | `fork_release_test.py` runs only on a dispatched release, never on PRs | medium | patch | Siblings run their tests per PR; added a step to `crosshatch-upstream-ledger.yml` (Python and git only, like its own test). |
| 2 | blind, edge | A queued run in the one concurrency group is replaced by a newer dispatch | low | patch | True of GitHub concurrency; a separate group would break the one-group rule, and the replaced run has done nothing. Comment corrected to say so. |
| 3 | blind, edge | A publish failing after the tag burns N; re-running the job fails `recheck` | low | patch | Intended by AD-25 (tag first, gaps allowed). Added `curl --retry 3` and the recovery (delete draft, run again) to the header. |
| 4 | blind | Uploaded assets compared by name only | low | patch | `expected-assets` now prints name and size; the job diffs them against the API's name and size. |
| 5 | blind, edge | A flow sequence on the line after `on:`, or over several lines, is missed (fails open) | low | patch | Reproduced; the flow form is now detected from the first child line and every line of the value is read; three test cases added. |
| 6 | edge | A `games/` directory with spaces or capitals breaks the unencoded upload after the tag exists | low | patch | Directory names now must match the spine's manifest id grammar `^[a-z0-9][a-z0-9-]{0,31}$`, checked before packing; tested. |
| 7 | edge | OS errors and plan `KeyError`s exit 1 (rule broken) with a traceback, not 2 | low | patch | `main` maps `OSError`/`KeyError`/`TypeError` to exit 2; tested with a missing and a partial plan. |
| 8 | blind | Local runs read the untracked `platformio.local.ini` | low | patch | Docstring now says so; CI has no such file. |
| 9 | blind | Dry-run rule does not say how to run on another branch | low | patch | Doc names "Use workflow from". |
| 10 | blind, edge | A stray tag with a ten-digit `-ch.N` blocks releases for good | low | reject | AD-25 counts every tag on purpose (hand-made tags must never be reused); the run fails loudly and the owner can lift the ruleset to delete it. |
| 11 | blind | Release boards not pinned, so a dropped flag silently releases fewer boards | low | reject | AD-25 derives the set from the flag, and adding or dropping it is an AD-2/AD-25 change; the run prints the envs it releases. |
| 12 | blind | Image size never checked against the OTA slot | false | reject | The platform sizes `checkprogsize` from the app partition (the build log reports x4pro "of 6,553,600", the 0x640000 slot), so an image that does not fit fails the build. |
| 13 | blind | Host GoogleTest not run on the released commit | low | reject | Every releasable commit is on develop and passed CI's unit-test job when merged. |
| 14 | blind | Dry-run rule omits partition tables and the SDK pointer | low | reject | The paragraph states AD-25's list; the SDK pointer cannot move at all under the ledger check. |
| 15 | blind | Notes have no changelog or compare link | low | reject | Not in the intent; adds surface. |
| 16 | edge | `prepare` leaves `platformio.ini` rewritten when a later check fails | low | reject | Only a local run's file (CI discards the checkout); a retry fails loudly on the X.Y.Z check, and the docstring gives the restore command. |
| 17 | edge | `download-artifact@v4` with `upload-artifact@v6` may not resolve | false | reject | Upstream's `release.yml` in this repo pairs the same majors; both use the v4+ artifact backend. |
| 18 | intent | Descriptive: machinery, not a published release; workflows detected, not disabled; rule documented, not enforced; publish YAML untested beyond parsing | low | reject | Those are the owner's hitl steps (Never list) and the CI-only surface; recorded in the final report. |

## Design Notes

Ticket `unknown`: the Actions API returns each workflow's path and state but not its triggers, so triggers are read from the files (both commits a release could run from) and states from the API. `github-actions[bot]` cannot be a ruleset bypass actor, so a tag ruleset cannot let only the workflow create `*-ch.*` tags; per the epic decision no token secret is added. The owner's ruleset restricts deletion and updates of `*-ch.*` tags (bypass list empty, or admins only), not creation, and the workflow refuses an existing tag and never reuses `N`, because `N` counts every tag.

Dry runs may run on any branch (an upstream-merge branch before it lands), so the ref rule fails only a publishing run.

## Verification

**Commands:**
- `python3 scripts/fork_release_test.py -v`; `python3 scripts/check_upstream_touches_test.py` -- all pass.
- `preflight` against `.github/workflows` with fake state files -- fails / passes as above.
- `prepare --project-dir . --tags <fake>` then `build`, `check-images`, `pack-games`, `notes`; then `git checkout -- platformio.ini` -- all exit 0; `git status` clean.
- `python3 -c 'import yaml,sys; yaml.safe_load(open(sys.argv[1]))' .github/workflows/crosshatch-release.yml` -- parses.
- `python3 scripts/check_upstream_touches.py` after commit -- PASS; `./bin/clang-format-fix && git diff --exit-code` -- clean.

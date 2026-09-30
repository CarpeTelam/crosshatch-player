---
title: 'Rename .cpgame to .chgame and pack the device-run set in CI (e4-y)'
type: 'refactor'
ticket: ''
created: '2026-09-30'
status: 'built'
baseline_revision: '770116ebf9206c6e366d314ec828984a9993aab0'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/epic-install-and-launcher.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The owner decided (epic Notes, 2026-09-30) that the game package extension becomes `.chgame`, replacing `.cpgame`, which was modelled on upstream's `.cpfont`; and that the entry-14 packet stops committing package files, a CI job packing the device-run set instead.

**Approach:** Rename everywhere outside `_bmad-output/` (the installer scans `*.chgame`; leftovers are `<name>.chgame.bad` and `<name>.chgame.installed`, then `.installed.2` to `.5`; the packer and the release contract say `<id>.chgame`); the installer takes no `.cpgame`. Change the spine's eight `.cpgame` mentions and nothing else in it. Add `scripts/pack_device_run.py` (with a sidecar test), a committed changed-counter fixture, and a `Game packages` job that runs only on a pull request labelled `package-games` and uploads the folder as the `game-packages` artifact.

## Boundaries & Constraints

**Always:** `API_LEVEL_FROZEN` stays false; the API surface list and `API_SURFACE_CRC` follow the rename. Buffers and `static_assert`s sized from the suffix still hold. Upstream files change only as the ledger allows. The new job stays out of `Crosshatch Test Status`'s `needs`. Fork scripts follow `docs/crosshatch/fork-scripts.md`.

**Never:** No `.cpgame` compatibility. No edit to `_bmad-output/` other than the spine's mentions, this plan, and a `## e4-y` heading in `deferred-work.md`. No change to `.skills/`, the SDK pointer, or `ci.yml`. No loosened gate.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| New extension | `/games/g.chgame`, any letter case | Installed; deleted, or set aside as `g.chgame.installed` | Invalid: `g.chgame.bad` |
| Old extension | `/games/old.cpgame` | Not installed, not renamed, not an inbox file | None |
| Device-run pack | Committed fixtures, generator, vector | Seven `.chgame` files and `HASHES.txt` | Packer refusal: exit 1; a missing file or an unwritable folder: exit 2 |
| Label absent | PR without `package-games` | `Game packages` skipped | A skipped job blocks nothing |

</frozen-after-approval>

## Code Map

- `src/games/GamePackageInstaller.cpp` -- `hasPackageExtension`'s `EXTENSION` (length taken by `sizeof`); `asidePath` is sized from `INBOX_PATH_BYTES` (name capacity, not the suffix), so `.chgame` (same length as `.cpgame`) changes no size.
- `src/games/GamePackageInstaller.h`, `GamePaths.h`, `ZipDirectory.h`, `src/activities/games/GamesLauncherActivity.h`, `lib/GameCore/PackageLimits.h` -- comments only.
- `scripts/pack_game.py`, `fork_release.py` (+ tests) -- the target name; `docs/crosshatch/formats.md`, `api-level-1.txt` (a comment line; the CRC covers entry lines), `fixtures/README.md`, the run skill's `SKILL.md`.
- `test/game_core/package_vector.cpgame` -> `.chgame` (`git mv`), `package_vectors.json`'s `package`; the harness files and `gen_hardening_packages.py`, `pack_fixtures.py`, `packed_fixtures.cmake`.
- `test/game_core/ManifestTest.cpp` -- `EveryFixtureManifestIsListed` lists the folders with no manifest; `changed/` joins them.
- `scripts/fork_common.py` (`exit_code`, `SetupError`, `Failure`) -- reused; `pack_game.py` is run as a subprocess (as `fork_release.pack_one` does), so the hash is the one it printed.

## Tasks & Acceptance

**Execution:**
- [x] Rename `cpgame`/`CPGAME`/`Cpgame` to the `ch` spelling in the files above (the only identifier was the test name `OnlyCpgameFilesAreInstalled`, now `OnlyChgameFilesAreInstalled`); `git mv` the vector.
- [x] `test/game_script/harness/GamePackageInstallerTest.cpp` -- `ACpgameFileIsIgnored`: `old.cpgame` and `OLD2.CPGAME` beside a `new.chgame`: only `new` installs, the old files keep their bytes and get no `.bad` or `.installed`, and `hasInbox()` is false without the `.chgame`.
- [x] Spine: the eight mentions only; `formats.md`: one sentence that `.cpgame` is ignored.
- [x] `test/game_script/fixtures/changed/counter/` -- `counter` at 1.0.1 with the title "Counter v2". Chosen because the packer needs folder name == manifest id, and a `changed/` parent keeps one folder per variant without a second fixtures root; the README row says what it is for.
- [x] `scripts/pack_device_run.py`, `scripts/pack_device_run_test.py`, two ledger rows (Game paths).
- [x] `.github/workflows/crosshatch-ci.yml` -- `labeled` added to the `pull_request` types, the `game-packages` job, the header comment; the fixtures README documents the label.

**Acceptance Criteria:**
- Given the built firmware, when the inbox holds `g.chgame` and `old.cpgame`, then only `g` is handled.
- Given a fresh archive tree of the commit, when `pack_device_run.py` runs, then it exits 0 with the seven files and `HASHES.txt`, and the packages equal the ones committed for entry 14.
- Given the workflow, when a PR has no `package-games` label, then only the `Game packages` job is skipped.

## Implementation Notes

The build agent implemented this directly: the rename is a mechanical `sed`, and the coordinator's mid-run change replaced an every-PR shell step in `Fork script tests` (built first, then removed with `git show 770116eb:` and re-edited) with the script and the labelled job.

- `.cpgame` check: `hasPackageExtension` matches `.chgame` with `strcasecmp`, so `OLD2.CPGAME` is also ignored (tested).
- The seven packages packed from the archive tree are byte-identical to the entry-14 files committed under `device-run/` (`cmp`), so the hashes in the packet stand.
- Adding `labeled` to `types` means any label added to a PR reruns every job, and `concurrency` (`cancel-in-progress`) cancels a run in progress for the same PR. The cancelled run's `Crosshatch Test Status` reports the cancellation, which fails on the head commit (`crosshatch-test-status`'s own rule), and the new run then reports the real result. Accepted by the coordinator ("acceptable").
- The packet (`device-run-packet.md`) and `device-run/*.cpgame` are the orchestrator's; not touched.

## Plan Change Log

- Coordinator, mid-build: the CI part changed from a step in `Fork script tests` to a script and a labelled job. KEEP: the packing commands and the name `game-packages`; the earlier step's hashes matched the committed packages.

## Review Triage Log

Pass 1, on the diff `770116eb..HEAD` (WIP commit `pack_device_run.py` and the labelled job). All four lenses ran as context-free subagents (blind hunter, edge case hunter, verification gap, intent alignment) and returned before triage. Counts: 3 medium/low patched, 4 low deferred or noted, 12 rejected or false, 1 accepted by instruction.

| # | Lens | Finding | Verdict, route | Evidence / action |
|---|------|---------|----------------|-------------------|
| 1 | edge, blind | An `<out-dir>` reused from an earlier run keeps its `HASHES.txt` when this run fails, so the docstring's "nothing written unless every package was made" is false | low, patch | Real (CI uses a fresh folder, a person may not). Fixed: the old `HASHES.txt` is removed first; docstring corrected; `test_a_failed_run_removes_the_hashes_an_earlier_run_left`. |
| 2 | edge | Undecodable packer output, or a `hash_vector` that is not an object, is a traceback (exit 1) instead of exit 2 | low, patch | Real but remote; the fix is one `errors='replace'` and one `TypeError`, with a test. A failed `TemporaryDirectory` (disk full) left as is. |
| 3 | blind | `changed/counter/main.lua` is a copy of `counter/main.lua` and can drift | low, patch | Fixed as a test: the real-tree test asserts the two scripts differ by the title alone and the manifests by the version alone. |
| 4 | edge | `vector_package` checks the committed vector by size only | low, defer | The vector's bytes and hash are pinned by `PackageHardeningTest` and `pack_game_test.py`; recomputing needs pack_game's hashing. Entry in `deferred-work.md`. |
| 5 | blind | The counter fixtures' header comment ("Copy this folder to /.games/counter/") is stale | low, defer | Pre-existing in `counter/`; editing either changes the hashes the packet records. Entry in `deferred-work.md`. |
| 6 | edge, blind, gap, intent | `labeled` reruns every job and `cancel-in-progress` cancels a run under way; the cancelled run's `Crosshatch Test Status` fails on the head SHA until the rerun reports | medium, accepted | The coordinator's instruction accepts the rerun ("acceptable, but say so"); stated in Implementation Notes and the workflow header. Not fixed: gating the other jobs on `github.event.action` or editing the status job's rule changes required-check logic beyond this story. Reported as a risk. |
| 7 | edge, blind, intent | The seven `device-run/*.cpgame` files, `device-run-packet.md`, the glossary, `SPEC.md` and older plans still say `.cpgame` | false for this diff | The work excludes `_bmad-output/` and hands the epic file and the packet to the orchestrator; old plans stay as history. Reported. |
| 8 | edge | `OnlyCpgameFilesAreInstalled` was renamed, against the plan's "no identifier" | low, patch | The rename is what the work asks ("Cpgame" becomes "Chgame"); the plan's sentence was wrong. Plan text corrected. |
| 9 | blind | The spine has no dated amendment note | false | The work allows the eight mentions and nothing else in the spine; the epic Notes hold the dated decision. |
| 10 | blind | "None shipped" is asserted, and AD-25 says the first release attaches `<id>.cpgame` | false | The owner's decision says "(none has shipped)"; no release of this fork exists to check against. |
| 11 | blind, edge | A `.cpgame` dropped in the inbox fails silently (no `.bad`, no log) | low, rejected | The owner chose no compatibility and the work asks for "ignored: not installed, not renamed". A log line is a new branch in shared code for a file that never shipped. |
| 12 | blind | The artifact has no commit SHA | low, rejected | It hangs off a workflow run that names the SHA; the work asks for a file-and-hash listing. |
| 13 | blind | The generator builds every hardening case to use one | low, rejected | The whole 22-test file, generator included, runs in about 3.5 s; the generator has no single-case option, and adding one edits a harness file for CI time. |
| 14 | blind | No timeout on the subprocesses; stderr of a successful pack is dropped | low, rejected | The job has `timeout-minutes: 10`; the packer prints only errors, and a refusal's stderr is shown. |
| 15 | blind | `changed/counter` is not installed by an installer-level test | low, rejected | `pack_game.py` refuses what the installer refuses and is run on it by the real-tree test; the replace-an-id cases are `GamePackageInstallerTest`'s and the Continue discard cases are the launcher suites'. |
| 16 | blind | A grep guard against `.cpgame` returning | low, rejected | `ACpgameFileIsIgnored` pins the behaviour; a guard test over docs is churn. |
| 17 | blind | The rename left older `_bmad-output/` docs on `.cpgame` | false | See 7. |
| 18 | gap | No verification gaps found; the workflow YAML is exercised by CI only | noted | The YAML parses (`yaml.safe_load`); the job's commands were run from a fresh archive tree (Verification). |
| 19 | intent | Readings differ on the vector: `package_vector.chgame` stays committed | noted | The work says to rename it and update every reference, so it stays; only the seven device-run copies leave the packet. |

## Verification

Builds and host tests ran under the shared lock on a work-in-progress commit made before the squash. `git diff <that commit> HEAD -- src lib test/game_script/harness test/game_core` is empty, so the firmware and the host suites measured are the final code (the review patches touched only `scripts/`, the plan, and `deferred-work.md`).

**Commands (all under `flock` where they build):**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test -j8` -- 1332/1332 passed; 10 further full `ctest -j8` runs, each 1332/1332 (`ACpgameFileIsIgnored` among them).
- Every `scripts/*_test.py` -- all exit 0: `check_api_freeze` 9, `check_flash_budget` 78, `check_layers` 52, `check_upstream_touches` 18, `fork_common` 27, `fork_release` 76, `game_codec` 22, `gen_game_icons` 49, `pack_device_run` 22, `pack_game` 86, `sim_sh` 5 tests.
- `python3 scripts/check_api_freeze.py` -- passed (nothing frozen; `API_LEVEL_FROZEN` untouched; `api-level-1.txt` changed only in a comment line, so `API_SURFACE_CRC` and `ApiLevelTest` stand). `check_layers.py` -- passed. `check_upstream_touches.py` -- PASS, trial merge of `upstream/develop` clean.
- `check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- all exit 0. Flash: games on 5,910,320 B, off 5,678,912 B, **+231,408 B** (limit 256,000; the last measurement was +231,408). Static RAM (`size -A`, DRAM plus IRAM sections): 187,848 vs 187,064, **+784 B** (limit 1,024; last +784). Both equal the last measurement, so the bar (+240,496 B, +808 B) is not touched. `objects`: 43 game objects, no problems. The `build on` run is the `pio run -e x4pro` build.
- `pio run -e default` (C3) -- SUCCESS in 6 min 42 s. `sim.sh build x4pro` -- SUCCESS. The `sticky`, `x4c`, and `papermono` envs were not built (not asked; the change to compiled code is one string literal).
- `./bin/clang-format-fix` twice, last step before the commit -- no change to any file; `git status` clean of new changes.
- Fresh archive tree of the committed work (`git archive HEAD` into a new folder; the job reads no git history and needs no submodule): `python3 scripts/pack_device_run_test.py` -- 22 tests OK; then the `Game packages` job's own run command, read from the tree's workflow (`python3 scripts/pack_device_run.py "$RUNNER_TEMP/game-packages"` under `bash -e`) -- exit 0. All seven packages are byte-identical (`cmp`) to the entry-14 files committed under `device-run/`, so the packet's hashes stand. `HASHES.txt`:

```
counter.chgame e8c3ac8dfb646d3b
loop.chgame e7ebc00d62486a0f
timing.chgame ea2741992f220fd8
pack-images.chgame 5f3b4f61e48de51b
counter-changed.chgame fa0d541ee5b21f13
invalid-binary-lua.chgame (invalid)
package_vector.chgame 0530a15766e91bf1
```
- `git grep -n -i cpgame -- . ':(exclude)_bmad-output'` -- only the deliberate references: `docs/crosshatch/formats.md:212-213` (the sentence that `.cpgame` was the earlier name and is ignored) and `test/game_script/harness/GamePackageInstallerTest.cpp:488-507` (the comment, the test name `ACpgameFileIsIgnored`, and its `old.cpgame`, `OLD2.CPGAME`, and the three "not renamed" checks).
- The spine: `git diff 770116eb HEAD` on it is eight lines changed, each only `cpgame` to `chgame`.

**Not exercised locally (CI only):** the workflow's trigger, the `if:` label gate, and `actions/upload-artifact@v6` (the file parses with `yaml.safe_load`; the job's command was run as above). No `Assumption for entry 14:` line was added.

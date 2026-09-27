---
epic: epic-platform-baseline
date: 2026-09-27
verdict: accepted-with-open-items
criteria: declared
headless: false
---

# Retrospective: epic-platform-baseline

## Epic summary

**Epic:** `epic-platform-baseline` (epic 1, "Game code builds everywhere and upstream merges stay clean"), folder `_bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/`. Resolved from the argument `epic 1` by matching `id = 1` in `tickets.py status`, then `find 1.1` → `epic_file`.

**Tickets** (build order from `tickets.py status`; all `status = built`, `state = review`; `pending_tickets` is empty; **all seven are still at `built`**, not `done`):

| Ref | Title | hitl | Covers | Plan baseline | Range (commits) |
|-----|-------|------|--------|---------------|-----------------|
| 1.1 | Games build flag, empty game libraries, and host suites | no | R3, R6 | `f6de4dac` | `f6de4dac..234d1c08` (a5681eb4, 234d1c08) |
| 1.2 | Upstream-touch ledger and its CI job | yes | R1, R2 | `234d1c08` | `234d1c08..de5c18a5` (de5c18a5) |
| 1.3 | x4pro flash budget gate | yes | R5 | `de5c18a5` | `de5c18a5..41acf2fa` (41acf2fa) |
| 1.4 | Vendor Lua 5.5.1 | no | R4, R3, R6 | `41acf2fa` | `41acf2fa..4e1a7c82` (4e1a7c82) |
| 1.6 | Fork update source | no | R7 | `4e1a7c82` | `4e1a7c82..311e4bb4` (311e4bb4) |
| 1.7 | Fork release workflow and first release | yes | R7 | `311e4bb4` | `311e4bb4..a2c1204a` (a2c1204a) |
| 1.5 | Refactor sweep | no | R1–R7 | `a2c1204a` | `a2c1204a..d578b4e3` **inferred** (d2d8e81a, 8e1bc5a4; merge 4154fb29 = PR #9; f3ba9e54, 20994d6a; merge d578b4e3 = PR #10) |

Baselines form one linear chain (each plan's baseline is the previous ticket's commit), so the ranges do not overlap. The last range runs to `HEAD` (`d578b4e3`, the PR #10 merge); nothing later has landed, so no cut was needed. PR #10's commits (f3ba9e54, 20994d6a) have no plan of their own. They are post-merge fixes to 1.3's gate and CI wiring, attributed to the last range by position.

**Delivery:** every ticket shipped in one PR, [CarpeTelam/crosshatch-player#9](https://github.com/CarpeTelam/crosshatch-player/pull/9) (merged 2026-09-26T23:13Z). Two fixes followed in [CarpeTelam/crosshatch-player#10](https://github.com/CarpeTelam/crosshatch-player/pull/10) (merged 2026-09-27T00:01Z).

**Evidence inventory**

| Evidence | Status | Source |
|----------|--------|--------|
| Epic file with Requirements R1–R7 and Done when 1–7 | present | `epic-platform-baseline.md` |
| Initiative requirements (CAP-11) | present | `../initiative-crosshatch-player-v1.md` |
| Ticket entries (description, verify, covers) | present, 7 | `tickets.toml`, `tickets.py find` |
| Story files | none; no ticket was refined (`story_file: null`) | `tickets.py find` |
| Plans | present, 7, all `status: built` | `story-*-plan.md` |
| Per-ticket code review | present for all 7 (six thorough 4-lens runs, a quick run for 1.5; 1.2 looped back once) | each plan's Review Triage Log |
| Deferred work | 3 entries (from 1.1, 1.2, 1.6) | `_bmad-output/implementation-artifacts/deferred-work.md` |
| Commit and diff evidence | present, from `git_evidence.py` per range; 13 commits (2 merges, both measured); 109 files in total, 63 of them vendored `lib/lua/src` | ranges above |
| CI results | present; PR #9 and PR #10 check runs | GitHub Actions |
| Fork release runs | run 1 (dispatch, `Build and check` success, `Tag and publish` skipped, so a dry run); run 2 (publish) success, 2026-09-27T00:39Z | Actions runs 36281463154, 36282530704 |
| Releases and tags on the fork | `1.6.5-ch.1`, published by run 2 from `d578b4e3` (no release or tag existed when this retro started) | [release 1.6.5-ch.1](https://github.com/CarpeTelam/crosshatch-player/releases/tag/1.6.5-ch.1) |
| Tag ruleset, branch protection | not readable from this session. **The owner confirmed on 2026-09-27** that the `*-ch.*` tag ruleset exists and branch protection on `develop` requires `Crosshatch Test Status` | owner, in this retro |
| Device OTA check | **passed (owner-confirmed, 2026-09-27).** An X4 Pro on `1.6.5-ch.1` was offered `1.6.5-ch.2` (published 00:56:35Z by Fork release run 3 from `d578b4e3`), installed it over the air, and was then offered nothing new | owner, in this retro; [release 1.6.5-ch.2](https://github.com/CarpeTelam/crosshatch-player/releases/tag/1.6.5-ch.2) |
| Upstream release workflows | `release.yml`, `release_candidate.yml`, `release-fonts.yml` are `disabled_manually` (since 2026-09-25) | Actions workflows API |
| Session logs | **not available** to this run. Commits name build session `session_01NpVfiShhiDk6wbQVKMqy8y`, but no transcript was read, so process lessons below rest on plans, commits, and CI only | commit trailers |
| Previous retrospective | none: this is the first epic in the `epics` order | `tickets.py status` |

## Findings

Each finding carries its source and two dispositions: **instance** (fix now / defer / accept) and **prevention** (the upstream lesson). Sources: `story-*-plan.md` means the plans in this folder; `cut`, `cfb`, and `fr` mean `scripts/check_upstream_touches.py`, `scripts/check_flash_budget.py`, and `scripts/fork_release.py`. Line numbers are at `d578b4e3`.

### Process and cross-ticket boundaries

**P1. All seven tickets shipped in one PR, so no per-ticket PR verification ran.** Medium.
- Evidence:
  - 1.1's verify begins "On its PR, CI builds all five envs…" (`tickets.toml`, entry 1).
  - 1.2's verify says "the workflow passes on its own PR"; 1.3's says "The job passes on its own PR".
  - The epic Notes say "land entry 2 first so later PRs show the ledger job passing".
  - The history is one branch: `a5681eb4`…`8e1bc5a4` merged as PR #9 (`4154fb29`), then PR #10.
- Effect: each gate first ran in CI with every ticket's code already stacked on top. Every "on its PR" check was replaced by a local run.
- Instance: **accept** (it is history).
- Prevention: either phrase `verify` for local evidence, or have the build loop open and merge one PR per ticket when a ticket's verify names CI.

**P2. PR #9 merged while `x4pro flash budget` was failing, with no review.** Medium.
- Evidence:
  - Check run 108504587384 failed at 23:09:21Z with exit 2, "missing games-on image .pio/build/x4pro/firmware.bin".
  - PR #9 merged at 23:13:24Z, and `get_reviews` returns none.
  - The gate was not yet a required check. Making it required was 1.3's hitl step.
- Now: fixed by PR #10 (`f3ba9e54`), whose check runs are all green. The owner confirms `Crosshatch Test Status` is required, and it rolls up both fork gates (`20994d6a`, `.github/workflows/crosshatch-ci.yml`).
- Instance: **accept** (resolved).
- Prevention: a gate ticket's hitl "make it required" step must be done before the PR that carries code the gate measures merges. More generally, never merge an epic PR with a red check that the epic itself introduced.

**P3. The flash gate's failure mode was ruled "unreachable" because only incremental local builds had verified it.** Medium.
- Evidence:
  - 1.3's triage row 16 rejected the stale-image risk: "Unreachable: metadata is written only after `pio run` succeeds" (`story-x4pro-flash-budget-gate-plan.md`, Review Triage Log).
  - On CI's fresh tree, `pio project metadata` emptied the build dir after the build (`f3ba9e54` message).
  - 1.3's Implementation Notes record only local runs, including an "incremental re-run".
- The code-lens review suspects the same clean-on-checksum behaviour could touch `fr build`: several `pio run` calls into one `.pio/build`, with images checked only afterwards (`fr:464-474`, `crosshatch-release.yml:139-142`). This is **unverified**. Runs 2 and 3 uploaded both images, so it does not fire today.
- Instance: **fix now** (small hardening, AI-3).
- Prevention: before a CI-only gate is declared built, run it from a fresh clone (the plan's Verification should say so).

**P4. 1.6 broke both simulator builds, and nothing caught it until the sweep.** Medium.
- Evidence:
  - `src/games/ForkReleaseProbe.cpp` is compiled under `FREEINK_CAP_GAMES`, which `simulator_x4pro` and `simulator_sticky` set. The simulator's `SecureHttpClient` lacks `setUserAgent` and the matching `GET` (`8e1bc5a4` message; `story-refactor-sweep-plan.md`, Implementation Notes).
  - 1.6's verify listed only the five firmware envs.
  - No workflow builds a simulator env (code-lens review, verified).
  - The fix excludes the probe by filename (`.claude/skills/run-crosshatch-player/simulator.ini:16-17`), so the next device-only file under `src/games/` fails the same way.
- Instance: **fix now** (AI-1). Later epics put UI under `src/activities/games/` that AGENTS.md says to check in the simulator.
- Prevention: add a simulator build to fork CI, and list the simulator envs in any ticket that adds code under `FREEINK_CAP_GAMES`.

**P5. The release validates tags against the tools commit's vectors while building firmware from `inputs.ref`.** Low; boundary between 1.6 and 1.7.
- Evidence: `fr:50-51` and `crosshatch-release.yml:53` read `test/game_core/fork_version_vectors.json` from the `tools` checkout. The images come from the `src` checkout of `inputs.ref`.
- Effect: a rollback release from an older commit, made after the grammar or `MAX_TAG_LEN` changed, is checked with rules its firmware does not use.
- Instance: **defer**. Nothing has changed the grammar yet, and the fix is to read the vector file from `src` or require the two copies to be identical.
- Prevention: a check that spans two commits should say which commit each input comes from.

### Verification gaps

**V1. Release env selection and the version-line rewrite are tested only against a fake config.** Low.
- Evidence: `scripts/fork_release_test.py:139-230` uses `fake_config`, and the per-PR step in `crosshatch-ci.yml:49` has no `pio`. `test_this_repository` (`fork_release_test.py:411-420`) checks only the two known upstream release workflows.
- Effect: an upstream merge that changes a `*-gh_release` env passes PR CI and fails at release time, unless the owner follows the manual dry-run rule (`docs/crosshatch/upstream-touches.md:41-48`).
- Instance: **defer**. The dry-run rule covers it.
- Prevention: none beyond the rule.

**V2. The dry-run rule omits `src/network/HttpDownloader.*`.** Low.
- Evidence: the probe relies on `HttpDownloader::fetchUrl` turning any non-200 into a failure, and it copies HttpDownloader's transport settings (`src/games/ForkReleaseProbe.cpp:25-27` against `src/network/HttpDownloader.cpp:75-83`). The rule's path list (`docs/crosshatch/upstream-touches.md:41-42`) does not name HttpDownloader.
- Instance: **fix now** (one doc line, AI-4).
- Prevention: none.

**V3. The image check cannot prove that the version string *starts* with the tag.** Low.
- Evidence: `fr:482-484` accepts `<tag>\0` after any byte other than `[0-9A-Za-z.]`, because the linker tail-merges the version into the user-agent string (1.7's Implementation Notes). A version such as `X-1.6.5-ch.2` would pass, and `runningBuildNumber` would read it as 0 (`lib/GameCore/ForkRelease.h:97-104`).
- Reaching that state needs a second version source that `check_overrides` does not cover. None exists today.
- Instance: **accept**. It is contrived, and the device check is the backstop.
- Prevention: none.

**V4. The flash gate has not yet measured Lua.** Informational.
- Evidence: the games-on minus games-off difference was 0 B at 1.3 and 1.4 (`story-x4pro-flash-budget-gate-plan.md` and `story-vendor-lua-5-5-1-plan.md`, Implementation Notes), because nothing references the libraries and the linker drops them.
- The engine's real cost first shows in epic-script-runtime.
- Instance: **accept**.
- Prevention: that epic's first ticket that references `lua_*` should record the measured difference.

### Aggregate views

**A1. Duplication across the three fork scripts.** Low.
- Evidence:
  - `SetupError` and the exit-code contract (0 pass, 1 fail, 2 could not run) are defined three times (`cut:30`, `cfb:50`, `fr:84`), each with its own handler (`cut:188-192`, `cfb:193-200`, `fr:730-741`).
  - There are two incompatible git helpers: `cut:34-44` returns bytes and raises; `fr:161-165` returns text and does not raise.
  - The games-flag literal appears twice (`cfb:41`, `fr:55`), as do the `pio run` loops (`cfb:69-78`, `fr:464-474`) and the step-summary writers.
  - The asset-name buffer size is mirrored with no cross-check: `fr:62`, `ASSET_NAME_BUFFER = 48`, against the upstream `OtaUpdater.cpp:56`, `char assetName[48]`.
- Instance: **defer, with a trigger.** epic-install-and-launcher adds `scripts/pack_game.py`, `game_codec.py`, and `gen_game_icons.py` (the ledger's Game paths), so extract a small fork-only helper module before those land (AI-5).
- Prevention: the build skill should look for an existing fork helper before writing a new script.

**A2. The toolchain setup is copied into two more workflows.** Low.
- Evidence: the PlatformIO Core install went from 4 copies to 6 (`crosshatch-ci.yml:87-96`, `crosshatch-release.yml:97-106`). The `pioarduino==6.1.19` penv pin went from 2 copies to 4 (`crosshatch-ci.yml:114`, `crosshatch-release.yml:136`).
- 1.3's triage row 6 rejected a composite action as new shared surface.
- Instance: **accept**. Upstream's `ci.yml` stays untouched by policy.
- Prevention: if the pin changes, all four copies must change. A fork-only composite action would keep the two fork copies in step (AI-5, optional).

**A3. Size growth: `fork_release.py` is 745 lines, but it is one pipeline, not a grab-bag.** Informational.
- Evidence: 39 top-level definitions in 7 sections, making up eight subcommands that pass one `plan.json` between them. The separable parts are a hand-rolled YAML `on:` parser (`fr:208-248`) and pack-games (`fr:532-613`).
- No other non-vendored file the epic touched grew past about 500 lines.
- Instance: **accept**.

**A4. Pattern divergence: the fork scripts set their own conventions.** Low.
- Evidence: sidecar `scripts/<name>_test.py` unittest files (no script in `scripts/` had one before), the 0/1/2 exit contract (upstream scripts use `sys.exit(1)`, e.g. `scripts/firmware_size_history.py:57`), and single quotes.
- The new C++ tests follow the repo layout (`test/game_core/ForkReleaseTest.cpp`).
- Instance: **accept**, and record them as the fork's script conventions so the next scripts copy one pattern (AI-5).

**A5. Architecture delta: the update path has a component the spine does not name.** Low; spec reconciliation.
- Evidence:
  - AD-25 says `OtaUpdater.cpp` "calls it [`ForkRelease.h`] only" (spine l.353); ledger row 10 says the same (spine l.87).
  - The as-built also calls `src/games/ForkReleaseProbe.{h,cpp}`, which depends on the SDK's `SecureHttpClient` (`OtaUpdater.cpp:21-28`, `ForkReleaseProbe.cpp:3-8`). Its rationale lives only in `story-fork-update-source-plan.md`, Design Notes.
  - The Structural Seed (spine ~l.446) also omits `GamesBuildAnchor.cpp`, the three scripts, and the vector file.
- Otherwise the delta is clean: `ForkRelease.h` includes only std headers, no lib depends on `src/`, there are no cycles, and every `src/games/*.cpp` is whole-file guarded.
- Instance: **spec reconciliation**, proposed as AI-6.

**A6. AD-2's "no static buffers over 64 B" against the 74-byte `constexpr` URL.** Low; spec reconciliation.
- Evidence: `lib/GameCore/ForkRelease.h:27-28` against spine l.68. The rule presumably targets RAM, while `constexpr` data sits in flash; only generated icon data is exempted.
- Instance: **spec reconciliation** (AI-6). Word the rule as mutable static storage, or exempt `constexpr` data.

### Spec-to-implementation reconciliation

| Req / Done when | As built | Evidence |
|---|---|---|
| R1 / DW2 | Met. 10 AD-3 rows and 5 allowlist entries; the trial merge with current `upstream/develop` (`4a6283db`) is clean | `docs/crosshatch/upstream-touches.md:30-39,56-60`; `cut --ref d578b4e3` exit 0 |
| R2 / DW1 | Met. Unledgered-path fail, ledgered pass, and `freeink-sdk` pointer fail are each tested; the job fetches full history | `cut:147-150`; `scripts/check_upstream_touches_test.py:156,162,179`; `crosshatch-ci.yml:32-41` |
| R3 / DW3 | Met. The flag is on exactly the 8 envs; all 5 CI envs build | `platformio.ini:280,292,304,326,406,424`; `simulator.ini:63,73`; PR #10 check runs |
| R4 / DW4 | Met. 5.5.1 is byte-identical (1.4's `diff -r`, SHA-256 recorded); 8 files excluded; `.clang-format` and the suppress line are in place | `lib/lua/library.json:11-24`; `lib/lua/.clang-format`; `platformio.ini:29` |
| R4 wording | **Accepted deviation.** 5.5.1 defaults `LUA_COMPAT_GLOBAL` on, so the build adds `-DLUA_COMPAT_GLOBAL=0` to make every option off, as R4 intends. Ticket 1.4's verify ("no `LUA_COMPAT_` define appears") could not be met literally | `lib/lua/src/luaconf.h:344-345`; `test/game_script/LuaOnHostTest.cpp:87`; `story-vendor-lua-5-5-1-plan.md`, Design Notes |
| R5 / DW5 | Met. On/off builds of one commit with a 250 KiB (256,000 B) limit | `cfb:13-14`; `crosshatch-ci.yml:63-65` |
| R5 wording | **Accepted deviation.** "250 KB" is read as KiB (6,000 B looser than decimal), because flash and partitions are sized in binary units | `story-x4pro-flash-budget-gate-plan.md`, Design Notes |
| R6 / DW4 | Met. Both suites run in the CI `unit-tests` job, including the Lua-on-host tests | `test/CMakeLists.txt:77-78`; PR #10 `unit-tests` green |
| R7 / DW6 | **Met.** `X.Y.Z-ch.N` releases publish (`1.6.5-ch.1`, `1.6.5-ch.2`); upstream release workflows are disabled; the tag ruleset is in place (owner). An X4 Pro on ch.1 was offered ch.2 over the air, installed it, and was not offered it again (owner-confirmed). The pack step exists but has no `games/` to pack yet | releases; `fr:565-613`; owner |
| DW7 | Met in the final state. PR #10 is green on every CI job, including both fork gates. PR #9 itself merged red (P2) | PR #10 check runs |
| Epic Note: owner makes ledger and size jobs required | **Accepted deviation, an improvement.** `20994d6a` rolled both into one `Crosshatch Test Status` job, so new fork checks need no settings change | `20994d6a` message; `AGENTS.md` |

### Deferred work carried by the tickets

| Item (`_bmad-output/implementation-artifacts/deferred-work.md`) | Status |
|---|---|
| 1.1: `sim.sh build` ignores a stale `platformio.local.ini` | **Resolved** in the sweep, `d2d8e81a` (`sim.sh` re-runs setup when the block lacks the current `simulator.ini`) |
| 1.2: AGENTS.md pointer to the ledger | **Resolved** in `d2d8e81a` (AGENTS.md Policy line) |
| 1.6: device check of 404 → no update, network failure → failed, and offer then no re-offer | **Partly resolved.** The offer, install, and no-re-offer part passed on device (ch.1 → ch.2, owner). The 404 and network-failure rows are still unverified on device (AI-2) |

### What the evidence confirms went well

- Upstream drift stayed exactly as designed. The epic changed four upstream files (`AGENTS.md`, `platformio.ini`, `test/CMakeLists.txt`, `src/network/OtaUpdater.cpp`), all ledgered, and the ledger job passes against current upstream.
- The per-ticket reviews caught real defects: 1.2's loopback added the committed failure-path tests the gate lacked, and 1.6 and 1.7 patched 14 findings between them.
- One vector file ties the C++ parser and the Python release checks together (`ForkReleaseTest.cpp:130-131`, `GrammarAgreesWithParser`). The code-lens review found them in agreement.
- The sweep did its job: it found and fixed P4 and cleared two deferred items.

## Behavior verification

This epic changed runtime behavior in one place: the update check in game builds (`OtaUpdater.cpp` through `ForkRelease.h` and the probe). The rest is build configuration and CI. What was exercised end to end:

| Flow | How | Observed |
|------|-----|----------|
| OTA from the fork's releases | Owner, on an X4 Pro: `1.6.5-ch.1` installed, then a check with `1.6.5-ch.2` published | ch.2 offered, installed over the air, then no update offered |
| Fork release workflow | Owner-dispatched runs 1 (dry run), 2 (`1.6.5-ch.1`), and 3 (`1.6.5-ch.2`), all from `d578b4e3` | all succeeded; image checks passed; two releases published |
| Ledger gate | `python3 scripts/check_upstream_touches.py --ref d578b4e3` against `upstream/develop` `4a6283db` (this retro) | exit 0; trial merge clean; 7 accepted paths |
| Script gates' failure paths | `check_upstream_touches_test.py`, `check_flash_budget_test.py`, `fork_release_test.py` (this retro) | 18, 24, and 50 pass |
| Host suites | `GameCoreTest` and `GameScriptTest` built and run in scratch (this retro) | 17/17 pass |
| Five-env build, format, cppcheck, unit tests, both fork gates | PR #10 CI on `20994d6a` | all green |
| Simulator build | only the sweep's local `sim.sh build x4pro`, after `8e1bc5a4` (`story-refactor-sweep-plan.md`) | builds. **No CI coverage** (P4, AI-1) |

Narrowed: the 404 → no-update and network-failure rows were not exercised (AI-2). `x4c`, `papermono`, and `pio check` were not rebuilt locally; PR #10 CI covers them.

### Addendum: local build check (completed after the owner's decision)

This ran in this session on `d578b4e3` from a fresh clone state; logs are in the session scratchpad. It confirms the verdict and changes no criterion.

| Check | Result |
|-------|--------|
| Host GoogleTest (full `ctest`) | 388/388 pass, including the 10 `ForkReleaseTest.*` and 5 `LuaOnHostTest.*` tests |
| Done when 1 cases on throwaway branches: README edit / ledger-doc edit / `freeink-sdk` moved to its parent | exit 1 / exit 0 / exit 1, as specified |
| `pio project config` after `sim.sh setup` | flag on exactly the 8 game envs |
| `pio run -e x4pro`, `-e default` (C3), `-e sticky` | all SUCCESS. x4pro Flash 86.1 %; default RAM 17.7 %, Flash 85.6 %. `lua @ 5.5.1` and the three game libraries are in every dependency graph, the C3 included, with no `lua_*` symbols linked |
| Flash gate on a fresh tree (`build on`, `build off`, `compare`) | exit 0. On 5,647,488 B, off 5,662,624 B: **difference −15,136 B** |
| Simulator `sim.sh build x4pro`, `start`, `ss` | builds and boots to Home |
| `./bin/clang-format-fix` (clang-format 21.1.8) | no changes |

Three observations refine the findings above:
- **V4 is sharper than recorded.** The games-on image is 15,136 B *smaller* than games-off. Only the fork's OTA path sits behind the flag, and upstream's `isUpdateNewer()` pulls in `sscanf`. The 250 KiB budget is therefore measured from −14.8 KiB today, and the engine's real cost is still unmeasured (AI-9).
- **P3's mechanism reproduced.** A `sim.sh setup` run during an x4pro build changed PlatformIO's project checksum, and PlatformIO wiped `.pio/build` mid-compile. This supports the clean-on-checksum hazard behind `f3ba9e54` and AI-3's concern for `fr build`. Serial reruns passed.
- **A recurring cloud-session setup gap.** The espressif32 platform's penv overrides `SSL_CERT_FILE` with its own certifi bundle, so package downloads fail TLS behind the agent proxy until the proxy CA is added to that bundle. 1.1's Implementation Notes record the same workaround. AGENTS.md does not mention it (AI-10).

## Previous-retro follow-through

Nothing to follow through: **no previous retrospective file exists**. `epic-platform-baseline` is the first epic in the `epics` order of `tickets.py status`, so there is no earlier epic folder to look in.

## Action items

All items are **proposed**; none was applied by this retro. Items marked *remediation* go to the normal dev loop as story-shaped work. Items marked *spec reconciliation* await the owner's application to the spine or epic.

| # | Kind | Action | From | Owner |
|---|------|--------|------|-------|
| AI-1 | remediation, fix now | Stop the simulator build from depending on a filename list. Add a `simulator_x4pro` build job to `.github/workflows/crosshatch-ci.yml`, listed in `Crosshatch Test Status`'s `needs`. Replace the per-file exclusion of `ForkReleaseProbe.cpp` with a guard that also excludes `SIMULATOR`, so the next device-only file under `src/games/` fails in CI and not in a later session | P4 | dev loop: a chore before or at the start of the next epic that adds `src/games` or `src/activities/games` code |
| AI-2 | deferred check | Decide the remaining device rows of 1.6's deferred check. For a network failure (e.g. Wi-Fi drops mid-check), the result should be "failed", not "no update". Now that releases exist, the 404 → "no update" row can only be exercised against a repository with no releases; accept it as covered by the host tests and the probe's design, or check it with a development build | deferred-work (1.6), DW6 | owner (hitl) |
| AI-3 | remediation, fix now | Harden `fr build` against PlatformIO's clean-on-checksum: check each image right after its own `pio run`, or give each env its own build dir, as the flash gate now does for games-off | P3 (unverified risk) | dev loop |
| AI-4 | remediation, fix now | Add `src/network/HttpDownloader.*` to the row-10 dry-run rule in `docs/crosshatch/upstream-touches.md` | V2 | dev loop (one line) |
| AI-5 | remediation, deferred with trigger | Before epic-install-and-launcher adds `pack_game.py`, `game_codec.py`, and `gen_game_icons.py`, extract a fork-only helper for the shared script plumbing. That covers `SetupError`, the 0/1/2 exit contract, the git helper, the games-flag literal, and the step-summary writer. Also write the fork's script conventions (sidecar `_test.py`, the exit contract) in `docs/crosshatch/` or `docs/contributing/`. Optionally, add a fork-only composite action for the PlatformIO setup that the two `crosshatch-*` workflows share | A1, A2, A4 | dev loop: first ticket of epic-install-and-launcher |
| AI-6 | spec reconciliation | Update ARCHITECTURE-SPINE.md in four places. (a) AD-25 and ledger row 10 name `src/games/ForkReleaseProbe` and why it exists: a non-200 status is unreachable through `HttpDownloader` without an unledgered edit. (b) The Structural Seed lists `src/games/GamesBuildAnchor.cpp`, the three fork scripts, and `test/game_core/fork_version_vectors.json`. (c) AD-2's 64 B rule says "mutable static storage", or exempts `constexpr` data. (d) Record R4's `-DLUA_COMPAT_GLOBAL=0` and R5's KiB reading as the as-built interpretations | A5, A6, spec table | owner, via `bmad-architecture` update |
| AI-7 | process lesson | Make ticket delivery match ticket wording. When a ticket's `verify` names "on its PR" or a CI result, either the build loop opens and merges a PR per ticket, or the verify is phrased as local evidence plus one epic PR. A gate ticket's "make it required" hitl step comes before merging the code the gate measures. Never merge a PR with a red check that the PR's own tickets introduced | P1, P2 | owner (process; candidate AGENTS.md pitfall or build-skill customization) |
| AI-8 | process lesson | Before a CI-only gate or workflow is marked built, run it once from a fresh clone and state that in the plan's Verification. Incremental local trees hid P3 | P3 | build skill / plan template (owner) |
| AI-9 | watch item | epic-script-runtime's first ticket that references `lua_*` from game code records the flash gate's games-on minus games-off difference. Until then the 250 KiB budget has not measured the engine; today's difference is −15,136 B (addendum) | V4 | dev loop: epic-script-runtime |
| AI-10 | process lesson | Add a Known pitfall to AGENTS.md for cloud sessions: the espressif32 penv's certifi bundle needs the agent-proxy CA (`/root/.ccr/ca-bundle.crt`) before `pio` can download packages. Also note in the simulator skill not to run `sim.sh setup` or a simulator build while a firmware build is running, since the checksum change wipes `.pio/build` | addendum; 1.1 Implementation Notes | owner, via `bmad-project-context` |

Not an action here: the seven tickets are still at `built`. Closing them (`done`) is the ticketing skill's job, confirmed by the owner. This retro changes no ticket status.

## Acceptance verdict

**Machine verdict: accepted-with-open-items**, criteria **declared** (the epic file's Done when 1–7). `pending_tickets` is empty.

| Done when | Result | Evidence |
|-----------|--------|----------|
| 1. The ledger job fails unledgered and SDK-pointer changes and passes ledgered ones | met | `scripts/check_upstream_touches_test.py` (18 pass); `cut --ref d578b4e3` exit 0 |
| 2. 10 AD-3 rows up front; trial merge with upstream clean in game paths | met | `docs/crosshatch/upstream-touches.md:30-39`; clean merge-tree against `4a6283db` |
| 3. Five envs build with `lib/lua` and the three skeletons; flag only on the 8 envs | met | PR #10 builds; `platformio.ini`/`simulator.ini` grep |
| 4. Host suites with a Lua smoke test; format and `pio check` pass with `lib/lua` excluded | met | PR #10 `unit-tests`, `clang-format`, `cppcheck`; `lib/lua/.clang-format`; `platformio.ini:29` |
| 5. Fork-only on/off x4pro size gate, fails above 250 KB | met (KiB reading, accepted deviation) | `crosshatch-ci.yml`; `cfb`; PR #10 `x4pro flash budget` green |
| 6. `X.Y.Z-ch.N` published; X4 Pro offered, installs, not re-offered | met | releases `1.6.5-ch.1`, `1.6.5-ch.2`; device check owner-confirmed |
| 7. Merged to `develop` with every gate green | met in the final state | PR #10 check runs; PR #9 merged red (P2), resolved by PR #10 |

No finding is blocking: each open item is a hardening, cleanup, or documentation change with a named owner. The verdict carries open items because AI-1, AI-3, and AI-4 are fix-now remediations, AI-2 is the remaining deferred device check, and AI-5, AI-6, and AI-9 are deferred or spec reconciliations. P1–P3 record that the path to the final state skipped the epic's own delivery plan (one PR instead of per-ticket PRs, and a merge over a red gate). That weighs on process (AI-7, AI-8), not on whether the final state meets the criteria.

**Human decision: accepted-with-open-items.** The owner accepted the machine verdict on 2026-09-27. The open items are AI-1 to AI-9 above. AI-10 was added after the decision, from the addendum; it is a process lesson and does not change the verdict.

## Open questions

- AI-2: is covering the 404 → "no update" row by host tests and the probe's design enough, now that the fork has releases and the real endpoint no longer returns 404?
- P3/AI-3: does PlatformIO's clean-on-checksum actually threaten `fr build`? It did not fire in three release runs, and no one has explained why.
- AI-7: should per-ticket PRs become the build loop's rule for epics whose tickets name CI in `verify`, or should the verify wording change instead?

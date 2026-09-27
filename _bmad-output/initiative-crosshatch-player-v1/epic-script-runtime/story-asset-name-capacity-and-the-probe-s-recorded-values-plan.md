---
title: 'Asset-name capacity and the probe''s recorded values'
type: 'chore'
ticket: '17'
created: '2026-09-27'
status: 'built'
baseline_revision: '67e077dd611ba01e46fac8d4b74ce87708c10460'
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

**Problem:** The 48-byte asset-name buffer is mirrored by hand (`OtaUpdater.cpp:56`, `fork_release.py` `ASSET_NAME_BUFFER`, literals in `ForkReleaseTest.cpp`), so an upstream merge that resizes it could strand fork devices unseen (AD-25); and `ForkReleaseProbe`'s transport values are recorded nowhere, while its comment wrongly says they match `HttpDownloader`.

**Approach:** Add `ForkRelease::ASSET_NAME_CAPACITY = 48`, mirror it as `asset_name_capacity` in `test/game_core/fork_version_vectors.json`, and make the C++ suite and `fork_release.py` size asset names from it, with an at-capacity and a one-over vector; tie it to `assetName` with a row-10-guarded `static_assert`; record the probe's four values (read from SDK `111fdcc7` and the fork sources as they are) beside ledger row 10 as prose plus a numbered re-check list, and correct the probe comment.

## Boundaries & Constraints

**Always:** `static_assert(sizeof(assetName) == ForkRelease::ASSET_NAME_CAPACITY)` inside `#if FREEINK_CAP_GAMES` right after the buffer; row 10's Change text names it. Probe behaviour unchanged; only its comment. Under `## Ledger`, no new line starts with `|`, `- `, `* `, or `+ ` (the parser reads those as paths). `fork_common` exit contract; stdlib only. Fresh-clone run of the fork script tests and the affected release steps (retro AI-8).

**Never:** `lib/GameScript/`, `src/games/GameVM*`, `src/games/GameArena*`, `test/game_script/` (story 2.7); `ReleaseJsonParser.*`, `HttpDownloader.*`, the `freeink-sdk` pointer, the spine, workflows.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| At capacity | 25-char tag + `sticky` (47 chars + NUL) | C++ and Python both form the name | — |
| One over | 25-char tag + 7-char board (48 chars + NUL) | both return `""` | — |
| Mirror drift | vectors' `asset_name_capacity` ≠ header constant | `ConstantsMatchVectors` fails | — |
| Old released commit | vectors without `asset_name_capacity` | Python uses 48, the buffer those firmwares have | — |
| Bad field | 0, negative, non-integer, or boolean | `SetupError`, exit 2 | — |
| Buffer drift | constant 47 against `assetName[48]` | `pio run -e x4pro` fails at the `static_assert` | — |

</frozen-after-approval>

## Code Map

- `lib/GameCore/ForkRelease.h` -- `MAX_TAG_LEN` comment (l.30-31) names "48-byte"; add the constant beside it; `formatAssetName` takes `outSize` (unchanged).
- `src/network/OtaUpdater.cpp` -- `char assetName[48]` l.56 in `checkForUpdate`; existing row-10 guards at l.21-28, 64-73; `ForkRelease.h` already included under the guard. Not built in the simulator.
- `lib/JsonParser/ReleaseJsonParser.h` -- `currentAssetName[48]`, `firmwareAssetName[48]` are private; name them in comments only (not ledgered).
- `test/game_core/ForkReleaseTest.cpp` -- `assetName(..., bufferSize = 64)` l.106; `VectorFileLoads` scalar list l.119; `ConstantsMatchVectors` l.127; `LongestAssetNameFitsUpdateBuffer` literal 48 l.199.
- `test/game_core/fork_version_vectors.json` -- scalars, `asset_names` (row 3 is already the at-capacity case); `description` string.
- `scripts/fork_release.py` -- `ASSET_NAME_BUFFER` l.69-70; `Rules.__init__` l.92; `asset_name` l.107; docstring l.24-28 lists what comes from the released commit.
- `scripts/fork_release_test.py` -- `RulesTest` l.126 (`test_asset_name_must_fit_the_update_buffer` hard-codes 48 by board length).
- `src/games/ForkReleaseProbe.cpp` l.24-26 -- the wrong comment.
- `freeink-sdk/libs/network/SecureNet/include/SecureHttpClient.h` (SDK `111fdcc7`) -- `_timeoutMs = 15000` (l.614), `_followRedirects = 0` (l.605; a 3xx comes back as the status), `setInsecure` skips peer verification (l.66-68), `_userAgent` default replaced by `setUserAgent`.
- `src/network/HttpDownloader.cpp` -- with `FREEINK_NET_WOLFSSL=1` from `[base]` (`platformio.ini` l.73) the release fetch is `runGetWolf`: `HTTP_TIMEOUT_MS` 60000, `MAX_REDIRECTS` 5 manual hops, `setInsecure`, UA `"CrossPoint-ESP32-" CROSSPOINT_VERSION`.
- `docs/crosshatch/upstream-touches.md` -- row 10 l.39 and its paragraph l.41-48; `scripts/check_upstream_touches.py` `parse_ledger` l.50 reads `|`/`-`/`*`/`+` lines until the next `## `.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/ForkRelease.h` -- `ASSET_NAME_CAPACITY = 48` with its comment; `MAX_TAG_LEN` comment refers to it.
- [x] `test/game_core/fork_version_vectors.json` -- `"asset_name_capacity": 48`; one-over vector (25-char tag, 7-char board, `""`); description mentions the field.
- [x] `test/game_core/ForkReleaseTest.cpp` -- helper default and literal 48 become the constant; field required and equal to it; at-capacity and one-over checked by size.
- [x] `scripts/fork_release.py`, `scripts/fork_release_test.py` -- read the field (legacy 48 when absent, `SetupError` when bad), size names from it; tests for every Python matrix row.
- [x] `src/network/OtaUpdater.cpp` -- guarded `static_assert`.
- [x] `src/games/ForkReleaseProbe.cpp` -- accurate comment pointing at the ledger record.
- [x] `docs/crosshatch/upstream-touches.md` -- row 10 Change text; probe values paragraph and numbered re-check list beside row 10.

**Acceptance Criteria:**
- Given the commit, when `test/game_core` and every `scripts/*_test.py` run, then all pass, including the at-capacity and one-over vectors.
- Given a scratch `ASSET_NAME_CAPACITY` of 47, when `pio run -e x4pro` runs, then it fails at the `static_assert`; with 48, x4pro, sticky, default, x4c, and papermono build.
- Given the commit, when `check_upstream_touches.py` runs, then it passes with no "no longer exists" warning and `parse_ledger` still yields exactly the 10 rows.
- Given a fresh clone of the commit, when the fork script tests, `preflight --dry-run true`, `prepare`, `pio pkg install`, `build --dist`, and `notes` run as the workflow runs them, then each exits 0.

## Implementation Notes

- Implemented directly (no coding subagent in this session). Files: `lib/GameCore/ForkRelease.h`, `src/network/OtaUpdater.cpp` (4 guarded lines), `src/games/ForkReleaseProbe.cpp` (comment only), `test/game_core/ForkReleaseTest.cpp`, `test/game_core/fork_version_vectors.json`, `scripts/fork_release.py`, `scripts/fork_release_test.py`, `docs/crosshatch/upstream-touches.md`.
- `ASSET_NAME_BUFFER` became `LEGACY_ASSET_NAME_CAPACITY`, used only when the released commit's vectors lack the field; `Rules.asset_name_capacity` sizes every name. A bool, float, string, null, zero, or negative field is a `SetupError` (exit 2).
- The one-over vector is the 25-character tag with board `sticky2`; the at-capacity vector (`sticky`, 47 characters) already existed. The C++ `AssetNames` test now formats into a buffer of exactly `ASSET_NAME_CAPACITY` bytes, so both vectors are checked at the real size, and `AssetVectorsReachTheCapacity` fails if either vector goes missing.
- Probe values, read at SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8` and the fork sources: timeout 15,000 ms (SDK `_timeoutMs`, applied to the TCP connect, the handshake, and each response wait), redirect limit 0 (SDK `_followRedirects`), wolfSSL with peer verification off (`setInsecure()`), user agent `CrossPoint-ESP32-` + `CROSSPOINT_VERSION`; `HttpDownloader`'s wolfSSL release fetch uses 60,000 ms and 5 hops, the same TLS mode and user agent. Recorded under `## Ledger` as prose plus a numbered list (the ledger parser reads only `|`, `- `, `* `, `+ ` lines).
- `./bin/clang-format-fix` also reformatted `test/game_core/ApiLevelTest.cpp` and `ManifestCheckTest.cpp` (from story 2.4, commit `3d4aec97`); left out of this change and reported, since the whole-tree format check will flag them on the epic PR.
- Spine not edited: AD-25 already names the guarded `static_assert` (the ticket's unknown).

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time, its prompt read fresh, judged against the diff only). Diff 19.8 kB, blind floor 5. Verdicts: high 0, medium 0, low 4, false 2, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind | `ReleaseJsonParser.h`'s `firmwareAssetName[48]`/`currentAssetName[48]` have no `static_assert`, only the header comment | low | Rejected: the frozen intent ties the constant to `assetName` and puts `ReleaseJsonParser.*` out of bounds (private members, not ledgered); a merge touching it already needs the release dry run (row-10 paragraph). |
| 2 | blind | The legacy 48 default could hide a vector file that lost its field | false | At HEAD `VectorFileLoads` requires `asset_name_capacity` and `ConstantsMatchVectors` compares it; the default applies only to commits released before the field. |
| 3 | blind | The re-check list omits `src/games/ForkReleaseProbe.cpp`, so a fork edit could leave the record stale | low | Rejected: the list is AD-25's upstream-merge triggers; the probe's own comment now points at the record, where a fork edit is made. |
| 4 | blind | The Python capacity test hard-codes the vector tag and `sticky2` | low | Rejected: it deliberately pins that the at-capacity and one-over vectors exist; the C++ `AssetVectorsReachTheCapacity` checks the same by length. |
| 5 | blind | "15,000" and "ms" wrapped onto two lines in the ledger prose | low | Patched: rewrapped with each value and its unit on one line; `parse_ledger` still yields 10 rows. |
| 6 | blind | Any huge `asset_name_capacity` is accepted | false | No bad outcome: the C++ suite fails unless the field equals the header constant, which the `static_assert` ties to the buffer. |
| 7 | edge | (path trace, deletion check, claims check) | -- | No unhandled path: non-dict data fails at `data['tag_grammar']` (TypeError, exit 2); `ASSET_NAME_BUFFER` has no other reference (repo grep); the four recorded values and the 60 s / 5-hop comparison match `SecureHttpClient.h` and `runGetWolf` (`hop <= MAX_REDIRECTS` follows 5 redirects). |
| 8 | verification-gap | -- | -- | "No verification gaps found." Ignoring the field fails `test_asset_names_follow_the_vectors_capacity` and `test_capacity_comes_from_the_released_checkout`; the `static_assert` is compiler-enforced (shown by the 47 build). |
| 9 | intent | Readings: record the probe as it is (implemented) vs. make it match `HttpDownloader` (excluded by "as they are" and "correct the comment"); the firmware still sizes by `sizeof(assetName)`, tied to the constant by the assert, while the suites size by the constant | -- | Descriptive only. |

## Design Notes

**Equality, not `>=`.** The constant describes the buffer exactly; a larger upstream buffer would only make the fork's release check stricter than needed, but `==` forces the mirror to be updated either way, and it is what makes the 47 build fail.

**Legacy default.** Releases read the vectors from the released commit (2.5). Commits released as `-ch.1`/`-ch.2` have no field, and AD-25 withdraws a bad release by releasing N+1 from a good, possibly older, commit; those firmwares have `assetName[48]`, so 48 is their true value, as 2.5 treats a commit without `ApiLevel.h` as "No game API".

**Spine.** AD-25 already names the guarded `static_assert`; AD-3 row 10's summary is left as is (the ticket's unknown), because the ledger file is the enforced text.

## Verification

**Commands:**
- `cmake --build build/test && ctest --test-dir build/test -R ForkRelease` (under flock) -- pass.
- `for t in scripts/*_test.py; do python3 "$t" || exit 1; done` -- pass.
- `flock ... pio run -e <env>` for x4pro, sticky, default, x4c, papermono -- success; x4pro with scratch 47 -- fails at the `static_assert`.
- `python3 scripts/check_upstream_touches.py` -- PASS, no warning; `parse_ledger` Ledger count 10.
- Fresh clone: the release build job's steps -- exit 0.

**Verification record (2026-09-27, Python 3.11.15, clang-format 21.1.8, pioarduino 6.1.19; builds under the shared flock, one at a time):**
- Host suites: `cmake --build build/test` and `ctest --test-dir build/test`: 459/459 pass, including the 11 `ForkReleaseTest` cases (`AssetNames` now at a 48-byte buffer, `AssetVectorsReachTheCapacity`, `ConstantsMatchVectors`). With the vectors' field set to 47 in a scratch edit, `ConstantsMatchVectors` fails (47 vs 48); reverted.
- Fork script tests: all six `scripts/*_test.py` pass (`fork_release_test.py` 71, with the new `RulesTest` capacity, legacy, and bad-field cases and `PrepareTest.test_capacity_comes_from_the_released_checkout`).
- Firmware: with a scratch `ASSET_NAME_CAPACITY = 47`, `pio run -e x4pro` fails with one error, `src/network/OtaUpdater.cpp:58:35: error: static assertion failed ... (48 == 47)`; reverted to 48. Then x4pro (1:23), sticky (7:48), default (2:47), x4c (3:33), and papermono (3:28) each build: SUCCESS.
- Ledger: `python3 scripts/check_upstream_touches.py --ref <snapshot>` against `upstream/develop` 6743b683: exit 0, PASS, no warning line; `parse_ledger` on the committed ledger yields exactly the 10 rows.
- Fresh clone (retro AI-8) of snapshot `35ebf4a2b1bbf210e4e0aa7a3c5705124917d158` (`git clone` of the main tree, `checkout`, `git submodule update --init --recursive`; no `.pio`, no `platformio.local.ini`): the Fork script tests step (every `scripts/*_test.py -v`) exit 0; `preflight --dry-run true` (upstream `release.yml`/`release_candidate.yml` as disabled) exit 0 with the two ref warnings; `prepare` with the clone's tags exit 0, tag `1.6.5-ch.3`, assets `crosspoint-1.6.5-ch.3-{sticky,x4pro}.bin`, "Game API 1 (preview)"; `pio pkg install -e` per env exit 0; `build --dist` exit 0, both images checked and copied (sticky 5,630,672 B, x4pro 5,742,944 B); `pack-games` exit 0 (no games); `notes` exit 0, first line `1.6.5-ch.3 · Game API 1 (preview)`. The final commit differs from the snapshot only in this plan file (the triage patch rewrap is in the snapshot).
- Environment incident: the first `build --dist` attempt failed on "No space left on device" while PlatformIO reinstalled the shared `framework-arduinoespressif32-libs` after the custom-sdkconfig sticky build, which left that package half-installed (no `esp32c3`). Under the flock: removed the fresh clone's, 2.5's and 2.3's scratch `.pio` dirs and the main tree's `.pio/build/{default,x4c,papermono}`, reinstalled the package with `pio pkg install -e default` (all chip dirs back, 2.1 GB), and re-ran the release steps above. The disk has about 3 GB free.

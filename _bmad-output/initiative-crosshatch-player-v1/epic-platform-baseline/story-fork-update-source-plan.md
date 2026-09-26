---
title: 'Fork update source'
type: 'feature'
ticket: '6'
created: '2026-09-26'
status: 'built'
baseline_revision: '4e1a7c82079ebcca339c803476c4566497d06b4f'
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

**Problem:** Game builds (`FREEINK_CAP_GAMES`) still check upstream's releases and compare upstream semver, so a fork device would update itself to games-less upstream firmware, and a fork release could never recognise itself as installed.

**Approach:** Per AD-25, add the pure header `lib/GameCore/ForkRelease.h` (fork release URL, asset name, `-ch.N` parse and compare), pin its behaviour with a data-only vector file that the later release workflow also reads, and route `OtaUpdater.cpp` through it only inside `#if FREEINK_CAP_GAMES` (ledger row 10), with a 404 from the fork's releases/latest reported as `NO_UPDATE`.

## Boundaries & Constraints

**Always:**
- Tag grammar exactly `^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)-ch\.([1-9]\d{0,8})$`, at most 25 characters. A running version yields `N` only when a prefix matches the grammar and is followed by end of string, `-`, or `+`; otherwise `N = 0` (every dev build). Only `N` decides newer: newer iff the latest tag is a valid tag and its `N` > the running `N`.
- Asset name `crosspoint-<tag>-<board>.bin`; URL `https://api.github.com/repos/CarpeTelam/crosshatch-player/releases/latest`.
- `ForkRelease.h` is pure (standard headers only) and AD-2-clean (`constexpr` data and functions, `std::string_view`; no non-trivial namespace-scope objects).
- `test/game_core/fork_version_vectors.json` is data only, readable by `jq`/Python: tag grammar (ERE, no backslashes), URLs, valid/invalid tags, running version → N, newer pairs, asset names. The GoogleTest suite loads it at run time via a CMake compile definition; no values copied into C++.
- `OtaUpdater.cpp`: every change inside `#if FREEINK_CAP_GAMES`, upstream lines kept byte-identical in the `#else`/unguarded path. `OtaUpdater.h` unchanged.
- Decision (ticket `unknown`, 404 handling): `HttpDownloader::fetchUrl` returns only `bool` (any non-200 → false), and `HttpDownloader.*`, `ReleaseJsonParser.*`, `FirmwareBoardTag.*` are not ledgered. So a fork-only, whole-file-guarded `src/games/ForkReleaseProbe.{h,cpp}` re-requests releases/latest with the SDK's `freeink::SecureHttpClient` (as `HttpDownloader` does) only after the fetch failed, and reports whether the status was 404; `OtaUpdater.cpp` maps that to `NO_UPDATE`, else keeps `HTTP_ERROR`.

**Never:** edits to `HttpDownloader.*`, `ReleaseJsonParser.*`, `FirmwareBoardTag.*`, `OtaUpdater.h`, `platformio.ini`, or release workflows; any release workflow or tag logic (entry 7); fork prereleases; planning references in code comments.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Newer fork release | latest `1.6.5-ch.8`, running `1.6.5-ch.7` | asset `crosspoint-1.6.5-ch.8-x4pro.bin`; `isUpdateNewer()` true | — |
| Just installed | latest == running tag | not newer | — |
| Dev / upstream-style build | running `1.6.5-x4pro`, `1.6.5-dev-<branch>-<sha>`, `1.6.5` | `N = 0`; any valid release offered | — |
| Rollback on older base | latest `1.6.5-ch.11`, running `1.7.0-ch.10` | newer (only `N`) | — |
| Invalid latest tag | `v1.6.5-ch.9`, `1.6.5-ch.09`, >25 chars | no asset name; not newer | `NO_UPDATE` or not newer |
| No fork release yet | releases/latest → 404 | `NO_UPDATE` | probe status 404 |
| Network failure | fetch fails, probe not 404 | `HTTP_ERROR` (as today) | — |
| Non-game env | `default`, `x4c`, `papermono` | upstream URL, asset, semver unchanged | — |

</frozen-after-approval>

## Code Map

- `src/network/OtaUpdater.cpp` -- L22 `latestReleaseUrl` in anon namespace (used once, L45); L51 `snprintf` of `crosspoint-%s%s.bin` with `assetSuffix` (`-x3-x4` or `-<board_tag::boardName()>`); L59 `if (!ok)` → `HTTP_ERROR`; L88-129 `isUpdateNewer()` semver body. Simulator excludes this file.
- `src/network/HttpDownloader.cpp` -- `runGetWolf` (all envs set `FREEINK_NET_WOLFSSL` in `[base]`): `SecureHttpClient`, `setInsecure()`, `setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION)`; non-200 → `HTTP_ERROR`, status discarded. Read-only reference.
- `freeink-sdk/libs/network/SecureNet/include/SecureHttpClient.h` -- header-only; `GET(onData)` returns status or -1; without wolfSSL https fails at connect (-1), so no `#if` needed.
- `lib/JsonParser/ReleaseJsonParser.*` -- `tagName[32]` (longer tags truncated, so never valid under the 25-char grammar); asset match by `strcmp` with name set via `setFirmwareAssetName`.
- `src/network/FirmwareBoardTag.h` -- `board_tag::boardName()` (not NUL-terminated) + `boardNameLen()`; `x4pro`, `sticky`.
- `lib/JsonParser/StreamingJsonParser.*` -- pure SAX parser already used by host tests; reuse it to load the vector file (no new dependency).
- `test/game_core/CMakeLists.txt`, `GameCoreTest.cpp` -- existing suite to extend.
- `lib/Memory/Memory.h` -- `makeUniqueNoThrow` for the probe's client (object > 256 B).
- `.github/workflows/release.yml` -- builds with `CROSSPOINT_RC_HASH=<sha> pio run -e <env>`; reuse locally for `x4pro-gh_release`.
- `scripts/check_upstream_touches.py`, `scripts/check_flash_budget.py` -- gates to run after commit.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/ForkRelease.h` -- `namespace ForkRelease`: `LATEST_RELEASE_URL`, `MAX_TAG_LEN`, `tagBuildNumber(sv)`, `runningBuildNumber(sv)`, `isNewer(latestTag, running)`, `formatAssetName(out, size, tag, board)` (false and `""` on invalid tag, empty board, or no room) -- the single owner of fork release rules.
- [x] `test/game_core/fork_version_vectors.json` -- the vectors listed in Always, incl. boundaries (25 vs 26 chars, 9 vs 10 digits, `ch.0`, leading zeros, `-rc+hash`, `+suffix`, garbage, empty).
- [x] `test/game_core/ForkReleaseTest.cpp` -- load JSON with `StreamingJsonParser`; assert every section non-empty and every vector; `release_url`/`max_tag_length` equal the header's; `std::regex(tag_grammar)` agrees with `tagBuildNumber` (incl. capture 4 == N); buffer-size edge for `formatAssetName`.
- [x] `test/game_core/CMakeLists.txt` -- add the test and `StreamingJsonParser.cpp`, include `lib/JsonParser`, `FORK_VERSION_VECTORS_PATH` compile definition.
- [x] `src/games/ForkReleaseProbe.h`, `.cpp` -- `bool ForkReleaseProbe::latestReleaseMissing()`; `.cpp` whole-file guarded; heap-allocated client, null-checked; discard body; `LOG_*`.
- [x] `src/network/OtaUpdater.cpp` -- guarded: includes; URL definition swap; asset-name call; 404 → `NO_UPDATE` in `if (!ok)`; `isUpdateNewer()` body.

**Acceptance Criteria:**
- Given the host test build, when ctest runs, then `GameCoreTest` passes with all vectors and the suite fails if the vector file is missing or a section is empty.
- Given `x4pro`, `sticky`, `default`, `x4c`, `papermono`, when built, then each succeeds.
- Given `x4pro-gh_release` built as `release.yml` does, when `strings` runs on `firmware.bin`, then it contains the fork URL and not `repos/crosspoint-reader/crosspoint-reader/releases`; given `default`, then the reverse.
- Given the commit, when `check_upstream_touches.py`, `pio check`, and `clang-format-fix` run, then all pass; the flash-budget on/off difference is reported.

## Implementation Notes

- Implemented directly (no subagent tool in this session). Files: `lib/GameCore/ForkRelease.h`, `test/game_core/fork_version_vectors.json`, `test/game_core/ForkReleaseTest.cpp`, `test/game_core/CMakeLists.txt`, `src/games/ForkReleaseProbe.{h,cpp}`, `src/network/OtaUpdater.cpp`.
- Ticket `unknown`: yes. The URL swaps at its definition (`#if` fork / `#else` the untouched upstream line), the asset-name `snprintf` and the `isUpdateNewer()` body sit in `#else` branches, and the 404 mapping is one guarded `if` in the existing `if (!ok)` block. `OtaUpdater.h` and every unledgered file are unchanged. `isX4`/`assetSuffix` stay computed but unused in game builds, to keep upstream lines untouched.
- 404: `HttpDownloader::fetchUrl` returns `bool`, and both transports turn any non-200 into `HTTP_ERROR`, so the status cannot reach `OtaUpdater.cpp` without an unledgered edit; the fork-only probe (Design Notes) re-requests the URL after a failed fetch. `curl` today: fork releases/latest 404, fork repo 200 (public, no release yet).
- The grammar string in the vector file uses `[.]` instead of `\.` so it needs no JSON escaping and works unchanged in `[[ =~ ]]`, `grep -E`, Python, and `std::regex::extended` (checked in bash and in the suite).
- Five envs built; `x4pro-gh_release` image holds only the fork URL, `default` only upstream's. `pio check` (default env, as CI) clean. Host suite 388/388.
- The matrix rows "No fork release yet" and "Network failure" have no automated test: the probe and `OtaUpdater.cpp` are device-only (deferred device check).

## Plan Change Log

## Review Triage Log

Pass 1 (lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: high 0, medium 0, low 11, false 5, maybe-false 0. Patches 5, deferred 1.

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | blind, edge, intent | Release envs report `1.6.5`, so a fork release would be re-offered forever | false | reject | AD-25 gives the version-line rewrite to the fork release workflow (entry 7); no fork image is released without it, and the parser side is what this change owns. |
| 2 | blind, gap | Header and vector file say the release workflow reads the file, but none does yet | low | patch | Reworded to "meant for" the workflow's checks. |
| 3 | blind | Python `re.match` with `$` accepts a trailing newline | low | patch | Description now asks for a whole-string match (`re.fullmatch`); newline vectors added for tags and running versions. |
| 4 | blind, edge | Invalid latest tag leaves `assetName` empty with a blank "No  asset" log; an unnamed asset could then match | low | patch | Return value checked and the rejected tag logged; a match still returns not-newer because `isNewer` rejects the tag. |
| 5 | blind | `isX4`/`assetSuffix` unused in game builds | low | reject | Guarding them edits more upstream lines; `pio check` runs the default env where they are used. |
| 6 | blind, edge | Probe adds a second request after every failed fetch | low | reject | Only on failure, 15 s default timeout; the status is unreachable without an unledgered edit (Design Notes). |
| 7 | blind, gap | Probe and guarded `OtaUpdater` wiring have no automated test | low | defer | Device-only code outside host suite and simulator; device check recorded in deferred-work. |
| 8 | blind | 48-byte buffers not tied to the header by a shared constant | low | reject | Buffers are in upstream/unledgered files; `formatAssetName` gets `sizeof(assetName)` and fails safe; sticky is the longest game board. |
| 9 | blind | `strings` image check not automated | low | reject | The fork release workflow's image check owns it (AD-25). |
| 10 | blind | `ForkRelease` belongs outside `GameCore` | false | reject | AD-25 names `lib/GameCore/ForkRelease.h` and `test/game_core`. |
| 11 | blind | `<string_view>` only included through `ForkRelease.h` | low | patch | Included directly in the guarded block. |
| 12 | edge | A renamed or private repo 404s forever as `NO_UPDATE` | low | reject | AD-25 mandates 404 → `NO_UPDATE`; logged at info level. |
| 13 | edge | `formatAssetName(nullptr, n>0, ...)` writes through null | false | reject | Only caller passes a stack array; contract is a writable buffer of `outSize`. |
| 14 | gap | Truncated vector file still loads as `loaded` | low | patch | Collector requires the top-level object to close; checked by truncating the file (suite fails). |
| 15 | intent | Probe observes a second request, not the first fetch's 404 | false | reject | Recorded trade-off in Design Notes; the only compliant way to read a status. |
| 16 | intent | Tests exercise the header, not the device decision | false | reject | Descriptive; same surface as row 7. |

## Design Notes

404 approach options considered: (a) edit `HttpDownloader` to expose status -- unledgered, rejected; (b) query `/releases?per_page=1` and read an empty list as "no release" -- infers rather than observes the 404, and would need a second URL; (c) re-implement the whole fetch in `OtaUpdater.cpp` -- duplicates redirect/TLS handling in an upstream file; (d) chosen: a fork-only probe that re-requests the same URL only after a failed fetch and reads the status. It costs one extra request on failure only, keeps `OtaUpdater.cpp`'s guarded diff to one `if`, and a spoofed 404 (TLS is unverified, as in `HttpDownloader`) can only hide an update, which a spoofed JSON can already do.

The URL is swapped at its definition (`#if` fork / `#else` upstream line unchanged), so the fetch call stays upstream's line and the upstream string is absent from game images.

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j4` -- expected: all pass.
- `pio run -e x4pro|sticky|default|x4c|papermono` -- expected: success.
- `CROSSPOINT_RC_HASH=$(git rev-parse --short=7 HEAD) pio run -e x4pro-gh_release`; `strings` on both images -- expected: URL checks above.
- `python3 scripts/check_upstream_touches.py`; `check_flash_budget.py build on|off|compare` -- expected: PASS, difference reported.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`; `./bin/clang-format-fix && git diff --exit-code` -- expected: clean.

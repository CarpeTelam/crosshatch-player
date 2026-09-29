---
title: 'Manifest icon grammar, library check, and the pass capability'
type: 'feature'
ticket: '7'
created: '2026-09-29'
status: built
baseline_revision: '76e0cc45daa4bc1fec5dc860b7d4ad92bd6627a9'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
context:
  - 'AGENTS.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `Manifest::parse` takes an `icon` in a looser grammar than the library's (it accepts `_`), has no `icon_weight`, and `Manifest::check` offers `pass` although no match can run it. The installer installs a package whose `icon` names nothing in the library, and `pack_game.py` accepts a non-square `icon.png` and a `.lua` total over `LUA_SOURCES_BYTES`, both of which the installer refuses.

**Approach:** Apply the owner's inception decisions (epic Notes, 2026-09-28) in one commit: the library's grammar for `icon` and an optional `icon_weight` (`regular` or `fill`) in `Manifest::parse`; `HostCaps::pass` (false in `src/games/GameHostCaps.cpp`) that `Manifest::check` reads; both manifest rules in `api-level-1.txt` with the CRC; the library-membership check through `GameIcons::find` in the installer with its own `Error` and reason; and the packer's remaining rules, with its tests.

## Boundaries & Constraints

**Always:** `GameCore` never includes `lib/GameIcons`; the packer's icon and `icon_weight` rules and `Manifest::parse`'s stay identical (one grammar, a test table on each side); `Manifest::check` stays the only place a mode disappears; a malformed `icon_weight` makes the package invalid; new string appended to `english.yaml` only; keep every guard in `Manifest::check` (Invalid before Unavailable) and in `validIcon` (empty, length cap).

**Never:** Touch `lib/GameIcons`, `scripts/gen_game_icons.py`, `src/activities/**` beyond the one approved `reasonText` case, entry 1's or entry 4's harness files, or `GameSaveStore.*`; edit `ci.yml`; change `API_LEVEL_FROZEN`; hand-edit generated files.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Library icon | `"icon": "game-controller"`, `"icon_weight": "fill"` | parses; installs; `iconWeight` is fill | none |
| Grammar edge | `x`, `a1`, `a-1`, 32 bytes of `x` | parse accepts | none |
| Outside grammar | `old_name`, `a--b`, `-a`, `a-`, `1a`, `Foo`, 33 bytes | `ManifestError::BadIcon`; installer `BadManifest` | package `.bad` |
| Unknown icon | well formed, not in the library (`no-such-icon`) | parse and check pass; installer `UnknownIcon` | package `.bad`, reason shown |
| Bad weight | `bold`, `Fill`, `""`, `1`, `null`, `["fill"]` | `BadIconWeight` (or `WrongType` for a non-string); installer `BadManifest` | package `.bad` |
| Pass host | host `pass` false, game `solo`+`pass` | `check` Ok, modes solo only | none |
| Pass-only game | host `pass` false | `Unavailable(NoHostMode)`; installs, launcher marks it | none |
| Packer | non-square `icon.png`; `.lua` total over 262,144 | exit 1 with the reason | package not written |

</frozen-after-approval>

## Code Map

- `lib/GameCore/Manifest.{h,cpp}` -- `validIcon` (grammar), `parseIconWeight`, `Key::IconWeight`, `MANIFEST_KEYS`, `Manifest::iconWeight`, `ManifestError::BadIconWeight`, `check` (pass), `fieldsValid`.
- `lib/GameCore/HostCaps.h`, `src/games/GameHostCaps.cpp` -- `pass` field, provider sets false.
- `src/games/GamePackageInstaller.{h,cpp}` -- `Error::UnknownIcon`, check after `Manifest::check` in `readManifest`; `src/activities/games/GamesListActivity.cpp` -- the one approved `case`.
- `docs/crosshatch/api-level-1.txt`, `lib/GameCore/ApiLevel.h` -- `manifest icon_weight string?`, `name manifest_icon <pattern>`, `API_SURFACE_CRC`.
- `scripts/pack_game.py` and `scripts/pack_game_test.py` -- square icon, `.lua` total; icon and `icon_weight` rules already stricter and now equal to C++.
- `test/game_core/package_vectors.json`, `PackageLimitsTest.cpp` -- `lua_sources_bytes` (approved additions).
- `test/game_core/{ManifestTest,ManifestCheckTest,GameHostCapsTest,ApiLevelTest}.cpp`, `test/game_script/ApiSurfaceTest.cpp` -- tests; `test/game_script/harness/installer_icons.cmake` and `InstallerIconTest.cpp` -- new installer suite.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/Manifest.*`, `HostCaps.h`, `src/games/GameHostCaps.cpp` -- grammar, `icon_weight`, `pass` -- R9, decisions
- [x] `src/games/GamePackageInstaller.*`, `GamesListActivity.cpp`, `english.yaml` -- `UnknownIcon` and reason -- R9 outside GameCore
- [x] `api-level-1.txt`, `ApiLevel.h` -- entries and CRC 0x9C54B7D6
- [x] `pack_game.py` and test, vectors, `PackageLimitsTest` -- resolve three deferred items
- [x] Tests for every matrix row

**Acceptance Criteria:**
- Given a manifest with `_` in `icon`, when parsed, then `BadIcon`; a host with `pass` false offering a solo-and-pass game gets solo only; an unknown icon ends `.bad`; the packer and the parser agree on the grammar.

## Implementation Notes

Implemented directly by the build agent (the plan was small enough that a subagent would have re-read everything the agent had already read); the orchestrator's brief allows either.

## Plan Change Log

## Review Triage Log

Pass 1. The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as context-free subagents and all returned. Verdicts: 0 high, 1 medium, 6 low, 6 false or carried, 0 maybe-false.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | blind, edge, gap | `<commit>` placeholders in `deferred-work.md` | false | reject | The brief says to leave the placeholder for the orchestrator (no commit exists before this one). |
| 2 | blind, gap | Plan Verification empty, `lenses_ran` empty | false | reject | Filled after the runs; this log is that record. |
| 3 | blind | No user-facing doc for the manifest change | low | reject | `formats.md` is outside `touches`; `api-level-1.txt` (the contract and its comments) and the packer docstring carry it. |
| 4 | blind, edge, gap | A game already under `/.games/` with `_` in its icon stops parsing and is not listed | medium | patch | Intended by the owner's decision; pinned by `InstallerIconTest.AFolderWithAnUnderscoreIconIsNotListed`. No released game exists (level 1 is a preview). |
| 5 | blind | "Same table" C++ and Python not enforced | low | patch | `ManifestTest` comment now says what is shared (the accept and reject cases) and that `ApiLevelTest.ManifestIconMatchesTheParser` ties the API pattern to the parser. |
| 6 | blind, edge | `icon_weight` without `icon` accepted silently | low | patch | Kept on purpose; `api-level-1.txt` now says it needs no icon and has no effect without one. |
| 7 | blind | Lost coverage: over-limit `icon.png` | low | patch | `test_icon_png_over_the_dimension_limit_is_refused` (2049 and 3073 squares). |
| 8 | blind | Brittle hard-coded near-miss names; `patterns.size()==2u` | low | patch | Near misses are generated from library names with a loud ASSERT; the size assert is dropped (`ApiLevelTest` checks the new pattern). `SetUp()` in loops is the suite's existing idiom. |
| 9 | blind | Unusable failure output in `ManifestIconMatchesTheParser` | low | patch | The name, hex-escaped, is in the message. |
| 10 | blind, edge | `installer_icons.cmake` mutates another suite's target | low | patch | The include folder moved into `installer.cmake` (entry 3's file, on this lane's path 3 to 6 to 7). |
| 11 | edge | API list comment says an unknown icon "makes the package invalid" though `parse` and `check` accept it | low | patch | The comment names the installer and `pack_game.py` as the checks. |
| 12 | gap | No verification gaps found | n/a | n/a | Lens returned none. |
| 13 | intent | Readings R-A to R-D; diff implements the package-flow reading; launcher use of `iconWeight` and hand-copied folders are outside it | n/a | defer | Recorded under `## 4.7` in `deferred-work.md` (launcher entries 8 and 9). |

Pass 2 (source: the orchestrator's independent review, adversarial, edge-case, and verification-gap lenses; a finding two lenses made appears once).

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 14 | vg 1, adversarial 3 | Packer's `ICON_NAME`, `MAX_ICON_BYTES`, `ICON_WEIGHTS` are a hand copy; no test ties them to the API list; weights have no anchor | medium | patch | `test_the_api_list_holds_the_packers_manifest_rules` reads `api-level-1.txt` (pattern, cap, manifest keys, weights) and sweeps edge names; new `enum icon_weight regular` and `fill` lines (CRC changed) with `ApiLevelTest.IconWeightsMatchTheParser` on the C++ side. |
| 15 | vg 2 | Unterminated 33-byte `icon` in `BrokenFieldsAreInvalid` missing | low | patch | Case added (`unterminatedIcon`). |
| 16 | edge 2 | `UnknownIcon` missing from `PackageHardeningTest`'s lists; no upgrade-over-installed case | low | patch | Added to both lists (and `icon-not-in-library` to `gen_hardening_packages.py`, which the first list requires; its stale `lua_sources_bytes` fallback went too); `InstallerIconTest.AnUpgradeWithAnUnknownIconLeavesTheInstalledGameAndItsDataAlone` snapshots `/.games` and `/.games-data`. |
| 17 | edge 4 | `circle-fill` gives no hint | low | patch | The packer says to use the stem with `icon_weight` `fill` when the stem is in the library; test `test_a_fill_name_points_to_icon_weight`. The device text is unchanged. |
| 18 | adversarial 5, vg 4 | Design Notes and `installer_icons.cmake` say `installer.cmake` is untouched; `formats.md` Install step 1 omits the library check; the `icon_weight` comment commits to "no effect" | low | patch | Design Notes and the cmake comment corrected; `formats.md` step 1 names the check; the API-list comment is neutral about a game without an icon. |
| 19 | adversarial 1 | The registry lists a hand-copied folder whose icon the library lacks | low | defer | Belongs to entry 8; kept under `## 4.7` in `deferred-work.md`. |
| 20 | vg 3 | `UnknownIcon` reason text untested | low | defer | Same as every reason; already under `## 4.6` (entry 5 or 8 pins the map). |

## Design Notes

`git log -L` on the rewritten functions. `validIcon` (9197d046, 97dcf52f): guards are the empty and length checks, kept; its accept-`_` rule is the one the decision reverses. `Manifest::check` (3d4aec97, 97dcf52f, ...): Invalid verdicts (`BadFields`, solo seats, nearby seats) come before Unavailable, so a broken package is rejected at install before its host fit is judged; kept unchanged. Only the `startable` line changes: pass is offered only when `host.pass`. `fieldsValid` also rejects an out-of-range `iconWeight` for a Manifest that did not come from `parse`.

The `name manifest_icon` pattern in `api-level-1.txt` is the grammar; the 32-byte cap is the existing `limit manifest_icon_bytes`. `ApiLevelTest.ManifestIconMatchesTheParser` runs both over edge names against `Manifest::parse`. The installer suite is a new file, `installer_icons.cmake`; `installer.cmake` (entry 3's file, on this lane's path) gains the one `lib/GameIcons` include folder `GamePackageInstaller.cpp` now needs. The follow-up commit anchors the packer to the API list: `pack_game_test.py` reads `api-level-1.txt` for the icon pattern, the byte cap, the manifest keys, and the new `enum icon_weight` lines, which `ApiLevelTest.IconWeightsMatchTheParser` ties to `Manifest::parse`.

## Verification

Follow-up commit (independent review): host suites 987 of 987 pass with `API_SURFACE_CRC` 0xF512998F (Python `zlib.crc32` agrees); every `scripts/*_test.py` OK (`pack_game_test.py` with the new API-list anchor); `check_layers.py` and `check_api_freeze.py` pass; `pio run -e x4pro` and `-e default` SUCCESS. The first-commit runs below stand for the rest.

**Commands** (host suites and the two firmware builds ran under `build.lock`, the tree at this commit's sources):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- 984 of 984 pass (966 on the base, plus the new ones): `ManifestTest.AcceptsLibraryIconNames`, `RejectsIconNamesOutsideTheGrammar`, `ParsesIconWeight`, `RejectsMalformedIconWeight`; `ManifestCheckTest.AHostWithoutPassOffersOnlySolo`, `PassOnlyOnAHostWithoutPassIsUnavailable`; `GameHostCapsTest.PassIsOffUntilPassAndPlay`; `ApiLevelTest.ManifestIconMatchesTheParser`; ten `InstallerIconTest` cases, including `AnIconTheLibraryLacksEndsBad` (`.bad`, `UnknownIcon`); the level-1 tests (`SurfaceCrcMatchesTheLists`, `ManifestKeysMatchTheParser`, `ApiSurfaceTest.ListLoadsAndMatchesItsCrc`) with `API_SURFACE_CRC` 0x9C54B7D6 (also computed with Python's `zlib.crc32` over the entry lines).
- `python3 scripts/pack_game_test.py` -- 73 tests OK, with the new square-icon, over-limit icon, and `.lua` total cases.
- `python3 scripts/check_layers.py` -- passed. `python3 scripts/check_api_freeze.py` -- passed. `python3 scripts/check_upstream_touches.py` -- PASS.
- `pio run -e x4pro` -- SUCCESS (RAM 101,824 B, flash 5,913,570 B used); `pio run -e default` -- SUCCESS (flash 5,625,797 B used). Unmeasured as size deltas: no base measurement was taken the same way; the orchestrator measures after entry 15.
- `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- SUCCESS (`src/games` changed).
- No CI gate or workflow changed, so no fresh-tree run applies.

**Manual checks:** none; no screen changed except one `reasonText` case, which the sim build compiles.

---
title: 'Raise the package file cap to 64'
type: 'feature'
ticket: '10'
created: '2026-10-05'
baseline_revision: '71b78fb155d1c20b85b852b76cf68c0eea752265'
status: done
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: []
deferred:
  - summary: >-
      The installer's Job is one contiguous internal-heap block, now 5,888 B (was 4,096 B); whether a fragmented S3 heap can still allocate it is unmeasured.
    evidence: |-
      makeUniqueNoThrow<Job> uses the internal heap, not PSRAM. installAll logs "OOM: installer" and reports OutOfMemory when it fails, so the failure is clean; the device run (entry 5) can log the largest free block at installAll to settle it.
    location: >-
      src/games/GamePackageInstaller.cpp:80
    severity: medium (unverified)
---

<intent-contract>

## Intent

**Problem:** API level 1's `package_members_count` is 32, and Sudoku's package (27 note images, `manifest.json`, 8 Lua modules, 36 members) cannot pack or install. The owner's Decision of 2026-10-05 (epic Notes) overrides R2 for this one limit.

**Approach:** Raise `package_members_count` from 32 to 64 everywhere the limit lives (installer constant, packer constant, API list and its CRC, formats doc, vectors, and the tests that pin it); `images_count` stays 32 and every other limit is unchanged. Measure the installer `Job` allocation's growth and the flash and static-RAM delta against the epic's base Measurement line (`eca7e6c7`).

</intent-contract>

## Implementation Notes

Oneshot: a constant moves in a handful of places (about 40 changed lines, mechanical), with no new behaviour, so no implementation subagent is used.

Files: `lib/GameCore/PackageLimits.h` (`PACKAGE_MEMBERS` 64); `scripts/pack_game.py` (`MAX_MEMBERS` 64); `docs/crosshatch/api-level-1.txt` (`limit package_members_count 64`); `lib/GameCore/ApiLevel.h` (`API_SURFACE_CRC` 0x0401CF0D to 0xEE049B84, the value `ApiLevelTest.SurfaceCrcMatchesTheLists` printed); `docs/crosshatch/formats.md` (Members row); `test/game_core/package_vectors.json` (members limit/at/over 64/64/65, which `PackageLimitsTest`, `ApiSurfaceTest` via the constant, and `pack_game_test.py` read); `scripts/pack_game_test.py` (the member-count test followed the vector except two literals, now `at` and `over`); `test/game_script/harness/GamePackageInstallerTest.cpp` (a `static_assert` pinning 64 in the test that installs exactly the limit and refuses one more); `src/games/GamePackageInstaller.cpp` (one comment, "at most 32 names"); and, beyond the ticket's `touches` list, `src/games/GameAssets.h` and `test/game_script/harness/GameAssetsLoadTest.cpp` (D2 below).

## Plan Change Log

## Review Triage Log

### 2026-10-05 — Review pass
- verdicts: 23 findings — high 0, medium 5, low 8, false 10, maybe-false 0 (the medium rows share two root causes: the unpinned `MAX_SOURCES` link, patched, and the internal-heap `Job` block, deferred)
- findings:
  - Blind hunter, 10 findings:
  - `[low]` `[reject]` No test packs and installs the motivating 36-member Sudoku shape (27 PNGs) — the ticket's Verify asks for 64 pack/install and 65 refused, which `MoreMembersThanTheLimitEndsBad` and `test_member_count` do; the real Sudoku package is entry 9's, and an image-conversion fixture is more than a direct correction.
  - `[medium]` `[patch]` `MAX_SOURCES` mirrors the member cap with no test pinning the link (same root cause as the verification-gap finding) — patched: `static_assert(GameAssets::MAX_SOURCES + 1 >= GameCore::PACKAGE_MEMBERS)` in `GameAssetsLoadTest.cpp`, which fails if the constant goes back to 32.
  - `[medium]` `[defer]` The `Job` growth (4,096 to 5,888 B) is one contiguous block of the internal heap, not PSRAM; the plan's D3 cited PSRAM as comfort — D3 reworded; whether a fragmented S3 heap can still find 5,888 B is unmeasured, so deferred (unverified) to the device run. The OOM path exists (`installAll` logs and reports `OutOfMemory`).
  - `[false]` `[reject]` Flash and static-RAM results are not recorded — Verification runs after this review by the workflow's order; the figures are recorded there.
  - `[false]` `[reject]` The CRC is asserted, not shown — `ApiLevelTest.SurfaceCrcMatchesTheLists` printed `0xEE049B84` and passes (host run above); the plan edit it asks for is not a code finding.
  - `[false]` `[reject]` D2 widens scope and needs sign-off beyond a plan sentence — the plan and the final report name it to the orchestrator; `check_upstream_touches.py` passes (both files are fork files).
  - `[false]` `[reject]` The packer test no longer pins the limit independently — `pack_game_test.py` asserts the vector equals `pg.MAX_MEMBERS`, `PackageLimitsTest` the vector equals the constant, and `ApiLevelTest` the API list equals the constant, whose CRC pins the literal 64; the chain ends at a literal.
  - `[low]` `[reject]` The insertion-sort comment omits that the work is quadratic — about 2,000 compares of 56 B copies at 64 members, trivial next to SD reads; a comment-only expansion has no named harm.
  - `[false]` `[reject]` Other "32" caps (`MAX_PER_RUN`, folders a visit) were not reviewed — they are independent of the member cap (`MAX_PER_RUN` counts inbox files, `finishRemovals` folders); nothing to fix.
  - `[low]` `[reject]` formats.md lacks rationale — the Members row and the unchanged Images row already state both limits; prose would restate the Decision.
  - Edge case hunter, 5 findings:
  - `[medium]` `[defer]` `Job` is one contiguous internal-heap block — grouped with the Blind-hunter heap finding above (same root cause).
  - `[false]` `[reject]` The CRC change breaks Play Nearby matching between preview builds — `API_SURFACE_CRC` has no reader in `src/` or `lib/` outside `ApiLevel.h` (grep), and earlier stories changed it the same way (17d02483, 353bafd0).
  - `[low]` `[reject]` The plan says `PackageLimits.h` documents installed-also-loads — it does so for the `.lua` byte total (`LUA_SOURCES_BYTES` comment); D2 reworded to say that. A wording fix in the plan, not a code defect.
  - `[low]` `[reject]` The failing range is 33 to 63 `.lua` members, not 62 — correct; D2 reworded. The code (`MAX_SOURCES` = 64) covers 63.
  - `[medium]` `[defer]` D3 cites PSRAM as mitigation for an internal-heap cost — grouped with the heap finding above; D3 reworded.
  - Verification gap, 2 findings:
  - `[medium]` `[patch]` Nothing pins `MAX_SOURCES` to `PACKAGE_MEMBERS` — same patch as above.
  - `[low]` `[reject]` The log-line assertion now follows the constants — the format text stays literal; the numbers are the constants' own, as the test's other counts are.
  - Intent alignment auditor, descriptive; rows for each divergence it names:
  - `[low]` `[reject]` Nothing packs or installs a real 36-member game — same as the first Blind-hunter row; `games/sudoku` holds 27 members until entry 9.
  - `[false]` `[reject]` Most tests follow the constant, so they pass at any value — covered by the pin chain above (literal in `api-level-1.txt` with its CRC, `static_assert` in the installer test).
  - `[low]` `[reject]` `MAX_SOURCES` is outside the ticket's file list — D2, named to the orchestrator in the report.
  - `[false]` `[reject]` Verification results are not in the diff — the firmware results are recorded in Verification after this pass.
  - `[false]` `[reject]` The ticket's 36-member Sudoku does not exist in the worktree yet — a fact about entry 9's ordering, not a defect.
  - `[false]` `[reject]` "Each" in "65 are refused by each" may mean packer, `ZipDirectory`, and installer — all three are covered (`test_member_count`, `ZipDirectoryTest`'s 65-entry test, `MoreMembersThanTheLimitEndsBad`).

## Design Notes

**D1, scope.** The epic's Notes Decision of 2026-10-05 (line "API level 1's `package_members_count` rises from 32 to 64") and the ticket settle the number, the files, and that `images_count` and every other limit stay. 64 members install and 65 are refused by the packer (`check_members`), by `ZipDirectory::read` (`Status::TooMany` at the 65th entry, visiting at most 64) and by the installer (`Error::TooManyMembers`); each already followed the constant, so the cap needs no code beyond the constant.

**D2, `GameAssets::MAX_SOURCES` (a surfaced choice).** The loader refuses a folder of more than `MAX_SOURCES` (32) `.lua` files as `TooLarge`. `GameAssets.h` says it mirrors the package limits (the epic-icon-library plan: "Images are capped at 32, mirroring AD-15's member cap as `MAX_SOURCES` does"), and `PackageLimits.h` says of the `.lua` byte total that a package that installs also loads; the same invariant is what a source count at the member cap keeps. With the member cap at 64 and `MAX_SOURCES` at 32, a package of 33 to 63 `.lua` members (the manifest takes one member) would install and then fail to start. `MAX_SOURCES` is not in `api-level-1.txt`, so it is not one of the "other limits" the Decision freezes; I set it to `PACKAGE_MEMBERS` so the installed-also-loads invariant holds. The other reading (leave it at 32, installed-but-unplayable at 33+ Lua members) breaks that invariant; a third (a new 32 `.lua` cap in the packer and installer) adds an API-visible limit the Decision does not name. No shipped game is near 32 Lua modules (Sudoku has 8), so nothing observable to a first-party game differs. The orchestrator is told, and may revert `GameAssets.h` and its test line if the owner prefers 32. The cost is the span table in the PSRAM block, 44 B a module (`SourceSpan`: 33 B name, 3 B padding, two `uint32_t`), only for the modules present, so at most 1,408 B more than 32 modules took.

**D3, the `Job` allocation.** `Member` is 56 B (`char name[37]`, 3 B padding, four `uint32_t`). Measured with `-fdump-lang-class` on the x4pro compile command of `GamePackageInstaller.cpp` (xtensa, `-fsyntax-only`): `sizeof(Job)` 4,096 B at `71b78fb1` and 5,888 B after, +1,792 B (32 x 56 B), as the arithmetic predicts. `installAll` allocates it once per call with `makeUniqueNoThrow` and logs an OOM and reports `OutOfMemory` if it fails; `makeUniqueNoThrow` allocates from the internal heap, not PSRAM, so the 1,792 B is paid there, as one contiguous 5,888 B block; whether a fragmented S3 heap can still find it is unmeasured (deferred to the device run). The installer is built only under `FREEINK_CAP_GAMES` (the S3 envs), so the C3 (`default`) never holds it. Locals stay under 256 B: no local array is sized by `PACKAGE_MEMBERS`; the largest is `sortMembers`'s `Member moving` (56 B), and `ZipDirectory::read`'s frame does not depend on the cap.

**D4, guards in the code that follows the cap.** `ZipDirectory::read` checks `++seen > total` (EOCD count, `CountMismatch`) before `seen > PACKAGE_MEMBERS` (`TooMany`), so a short directory is a count mismatch and a long one stops after 64 visits; the installer's callback checks `memberCount >= PACKAGE_MEMBERS` before it writes `members[memberCount]`, so no write goes past the array; `sortMembers` refuses duplicate names. None of these is edited, only `sortMembers`'s comment. `git log -L` was not needed for any function body since none changed.

**D5, the API CRC.** `API_SURFACE_CRC` covers the list's entry lines, so it changes with the `limit` line, as `ApiLevelTest.SurfaceCrcMatchesTheLists` and `ApiSurfaceTest` require.

## Verification

Run on the tree that became the commit (baseline `71b78fb1` is the lane head; every size is measured the same way at both).

- Host tests: `cmake -S test -B build/test -G Ninja ... && cmake --build build/test`, `ctest --test-dir build/test --output-on-failure -j` -- 100% passed, 1,757 of 1,757 (ApiLevelTest, ApiSurfaceTest, PackageLimitsTest, ZipDirectoryTest, GamePackageInstallerTest, GameAssetsLoadTest, and the games-check label's 79 among them), re-run after the review patch. Before the CRC edit, `ApiLevelTest.SurfaceCrcMatchesTheLists` failed with "update API_SURFACE_CRC ... to 0xEE049B84", the value now in `ApiLevel.h`.
- 64 and 65 at each enforcement point: `GamePackageInstallerTest.MoreMembersThanTheLimitEndsBad` (64 installs, the 65th is `TooManyMembers`; `static_assert(PACKAGE_MEMBERS == 64)`), `ZipDirectoryTest.MoreEntriesThanAPackageMayHold...` (65 entries: `TooMany` after 64 visits), `pack_game_test.py` `test_member_count` (64 packs and the zip holds 64, 65 refused as "65 members; at most 64"). `package_vectors.json` holds 64/64/65.
- `scripts/*_test.py`, all 13 -- pass (`pack_game_test.py` 91 tests). `python3 scripts/check_upstream_touches.py` -- PASS. `./bin/clang-format-fix` twice -- no change to any file.
- Firmware, under the build lock with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, logs `scratchpad/8.10/baseline.log` (before) and `after.log` (after):
  - `scripts/check_flash_budget.py build on`, `build off`, `compare`: exit 0 both times. x4pro `firmware.bin` games on 5,935,632 B, games off 5,680,016 B, +255,616 B against the 276,480 B limit (20,864 B to spare); static internal RAM 187,848 B on, 187,064 B off, +784 B against 1,024 B (240 B to spare). Identical at `71b78fb1` and after the change, and identical to the epic's base Measurement line (`eca7e6c7`: 5,935,632 B, 5,680,016 B, 187,848 B, 187,064 B). PlatformIO's x4pro summary flash is 5,930,618 B before and 5,930,622 B after (+4 B); the 16 B-padded image did not change. `objects` exit 0.
  - `pio run -e default`: exit 0, `firmware.bin` 5,644,192 B, RAM 57,920 B, flash 5,630,117 B before and after (against the base line's 5,644,176 B and 5,630,109 B: +16 B and +8 B from the lane head's other entries, not from this change).
  - `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`: default and `-e x4pro`, "No defects found", exit 0.
- `Job` allocation (`-fdump-lang-class`, `-fsyntax-only` on the x4pro compile command of `GamePackageInstaller.cpp`, xtensa): `sizeof(Job)` 4,096 B before, 5,888 B after (+1,792 B); `sizeof(Member)` 56 B both.
- No simulator screenshots: no screen changes.

## Auto Run Result

**Summary.** `package_members_count` is 64 in the installer constant, the packer, the API list (with its CRC), the formats doc, the vectors, and the tests; 64 members pack and install and the 65th is refused by the packer, `ZipDirectory`, and the installer. `images_count` and every other listed limit are unchanged. `GameAssets::MAX_SOURCES` now equals `PACKAGE_MEMBERS` (D2, outside the ticket's file list: the orchestrator may revert `src/games/GameAssets.h` and the test it edits if the owner prefers 32).

**Files.** `lib/GameCore/PackageLimits.h` (64), `lib/GameCore/ApiLevel.h` (CRC `0xEE049B84`), `docs/crosshatch/api-level-1.txt`, `docs/crosshatch/formats.md`, `scripts/pack_game.py` (`MAX_MEMBERS`), `scripts/pack_game_test.py`, `test/game_core/package_vectors.json`, `test/game_script/harness/GamePackageInstallerTest.cpp` (pins 64), `src/games/GamePackageInstaller.cpp` (a comment), `src/games/GameAssets.h` and `test/game_script/harness/GameAssetsLoadTest.cpp` (D2 and its pin), this plan.

**Review.** Four lenses ran (`blind-hunter`, `edge-case-hunter`, `verification-gap`, `intent-alignment`): 23 findings, 0 high, 5 medium (two root causes), 8 low, 10 false. Patched: one (the `MAX_SOURCES` link had no pin; a `static_assert` now fails if it returns to 32). Deferred: one (the 5,888 B `Job` is a contiguous internal-heap block; fragmentation unmeasured, medium unverified). Rejected, each with its reason in the Review Triage Log: the rest, including the 36-member Sudoku fixture (entry 9's package), the stale-comment and rationale-prose requests, and the Play Nearby CRC claim (no reader of `API_SURFACE_CRC` outside `ApiLevel.h`).

**Verification.** See Verification above: host suites 1,757 of 1,757; script tests, upstream-touches, clang-format twice clean; flash budget gate passes with every figure equal to the epic's base Measurement (+255,616 B and +784 B, unchanged); `default` and `x4pro` builds and `pio check` pass; `Job` 4,096 B to 5,888 B. No formatting-only change outside this story's paths. No screenshots.

**Residual risks.** The internal-heap `Job` block on a fragmented S3 heap (deferred; entry 5's device run can log the largest free block). `Job` is the only measured growth: the flash image did not change in size and static RAM did not change. A device that loads a game folder by hand with 33 to 63 `.lua` files now loads it (D2).

**followup_review_recommended: false** (one medium entry patched, no high).

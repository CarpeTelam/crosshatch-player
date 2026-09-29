---
title: 'Package hardening'
type: 'feature'
ticket: '6'
created: '2026-09-29'
status: built
baseline_revision: '34fae3ff7b29a0ea12050626b02803e6efaea957'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - 'AGENTS.md'
  - 'docs/crosshatch/formats.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The tracer installer checks names, the member count, and images only. A zip bomb, a member or package over its limit, a corrupt member, a binary `.lua`, ZIP64, an odd compression method, and an EOCD count that hides members all install today, and `ZipFile` keeps its EOCD count private, checks no CRC, and skips names of 256 bytes or more silently.

**Approach:** A pure host-tested EOCD and central-directory parser (`ZipDirectory.h`) feeds the installer every member's name, CRC, and declared size before anything is extracted; each member then streams through `ZipFile::readFileToStream` into a sink that caps the output at the declared size, refuses a Lua bytecode chunk, and checks `mz_crc32` at the end. Each rejection is an `Error` with its own `STR_GAMES_INSTALL_*` reason. The package limits join `api-level-1.txt`, and a C++ test ties the constants to `package_vectors.json`. A Python generator makes the crafted packages (zlib can deflate; the firmware's miniz cannot).

## Boundaries & Constraints

**Always:** All card access through `Storage` / `HalFile`; `makeUniqueNoThrow` and locals under 256 B; a rejection renames the file `.bad`, and `/.games/` and `/.games-data/` stay untouched (validation ends before the commit); a card or heap fault keeps the file (`SdCard`, `OutOfMemory`); `ZipFile`, `lib/miniz`, `scripts/pack_game.py`, and entry 3's harness files unchanged; the `src/games` to `lib/miniz` edge in the spine's table and `scripts/check_layers.py` in this commit; new strings appended to `english.yaml` only; keep `removeTmp` / `mayRemoveTmp` and their `.xlink` probe exactly; flash kept small (reuse `mz_crc32`, one rejection path, no new tables).

**Never:** Edit `src/activities/**` beyond the case labels a new `Error` value needs in `GamesListActivity::reasonText` (one hunk, reported), `lib/ZipFile`, `lib/miniz`, `GameSaveStore`, or entry 1's and entry 4's shared harness files; move `freeink-sdk`; change `API_LEVEL_FROZEN`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| At the limits | package 262,144 B; member 131,072 B; 32 members; image bytes 131,072 (2 images) | installs | none |
| One over | package 262,145 B / member 131,073 B / 33 members / image bytes 131,074 | `.bad` | `PackageTooBig` / `MemberTooBig` / `TooManyMembers` / `ImagesTooBig` |
| Zip bomb | declared size over the limit, or a small declared size the stream exceeds | `.bad` before or during extraction, output capped | `MemberTooBig` / `BadSize` |
| Names | `../x.lua`, `/x.lua`, `a/b.lua`, `a\b.lua`, folder, `.bmp`, upper case, duplicate, 300-byte name, NUL in name | `.bad` | `BadMember` |
| Binary chunk | a `.lua` whose first byte is ESC (0x1B), stored or deflated | `.bad` | `BinaryLua` |
| Zip layout | ZIP64 record or field, method 12 or 9, encrypted flag | `.bad` | `Unsupported` |
| Directory | EOCD count above or below the entries; entries-on-disk differs | `.bad` | `BadDirectory` |
| Stored sizes | stored member with compressed size not its uncompressed size | `.bad` | `BadSize` |
| CRC | a stored or deflated member with a wrong CRC | `.bad` | `BadCrc` |
| Damaged zip | no EOCD, a comment after it, offsets out of range | `.bad` | `NotAPackage` |
| Card fault | a read fails in the directory | file stays | `SdCard` |
| Leftover scratch | `/.games-tmp/` from a cut install | deleted when Games opens | none (pinned by entry 3's test) |

</frozen-after-approval>

## Code Map

- `src/games/ZipDirectory.h` -- new, header-only (entry 3's suite builds the installer from an explicit list, so a new `.cpp` would not link there): `ZipDirectory::read(reader, fileBytes, visit)` over a `read(offset, out, count)` reader; `Status`; `Entry {name, nameUsable, crc, uncompressedSize}`.
- `src/games/GamePackageInstaller.*` -- `listMembers` reads the directory; `judge`; `ManifestSink` and `FileSink` run each chunk through the guard; new `Error` values and `describe`; `ImagesTooBig` for `OverBudget` / `TooMany`.
- `src/games/MemberGuard.h` -- new, header-only: the cap at the declared size, the ESC byte of a `.lua`, the running `mz_crc32`; its own header so a test can drive it.
- `lib/GameCore/PackageLimits.h` -- `IMAGE_MAX_WIDTH`, `IMAGE_MAX_HEIGHT`; comment says what is enforced.
- `lib/I18n/translations/english.yaml` -- eight `STR_GAMES_INSTALL_*` keys. `src/activities/games/GamesListActivity.cpp` -- their `case` labels only.
- `scripts/check_layers.py`, the spine's layer table and diagram, AD-15 note -- `lib/miniz` for `src/games`.
- `docs/crosshatch/api-level-1.txt`, `lib/GameCore/ApiLevel.h`, `test/game_script/ApiSurfaceTest.cpp`, `test/game_core/ApiLevelTest.cpp` -- seven `limit` lines (the seventh, `lua_sources_bytes`, from the independent review), `API_SURFACE_CRC`, live values, a level-1 test tying them to `PackageLimits.h`.
- `test/game_core/PackageLimitsTest.cpp`, `CMakeLists.txt` -- constants against `package_vectors.json`.
- `test/game_script/harness/gen_hardening_packages.py`, `package_hardening.cmake`, `PackageHardeningTest.cpp`, `ZipDirectoryTest.cpp`, `MemberGuardTest.cpp` -- the generator (runs at build), and the suite `PackageHardeningTest` over `game_installer_src`.
- `test/game_script/harness/GamePackageInstallerTest.cpp` -- one test removed (see Design Notes).
- Reuse unchanged: `ZipFile::readFileToStream`, `InstallerSupport.h`, entry 3's other tests, `removeTmp`, `package_vectors.json`.

## Tasks & Acceptance

**Execution:**
- [x] `ZipDirectory.h`, `GamePackageInstaller.*`, `PackageLimits.h`, `english.yaml`, `GamesListActivity.cpp` -- as the Code Map says.
- [x] `check_layers.py`, the spine -- the `lib/miniz` edge.
- [x] `api-level-1.txt`, `ApiLevel.h`, the two level-1 tests, `PackageLimitsTest.cpp`, `test/game_core/CMakeLists.txt`.
- [x] `gen_hardening_packages.py`, `package_hardening.cmake`, the two test files -- one crafted package per matrix row, at-limit and one-over vectors, `/.games/` and `/.games-data/` compared before and after.
- [x] `deferred-work.md` under `## 4.6`.

**Acceptance Criteria:**
- Given each crafted package in the matrix, when Games opens, then the file ends `.bad` with its own reason and `/.games/` and `/.games-data/` are byte for byte as before.
- Given the at-limit vectors, when Games opens, then each installs.
- Given `x4pro` and `default`, when built, then both pass; `check_layers.py` and its test pass in the worktree and from a fresh archive tree; a simulator screenshot shows a `.bad` reason.

## Implementation Notes

The build agent wrote the code itself, as entry 3's did, having investigated first; no implementation subagent ran. Files outside `touches`, all approved by the orchestrator (reversible within the epic PR): `GamesListActivity.cpp` (the `case` labels), `GamePackageInstallerTest.cpp` (one test removed), and, at the independent review, `GameAssets.h` (its `MAX_SOURCE_BYTES` now names `GameCore::LUA_SOURCES_BYTES`, behaviour unchanged) and `docs/crosshatch/formats.md` (the stale package paragraph). Both are in Design Notes.

## Plan Change Log

## Review Triage Log

All four lenses ran as context-free subagents over the staged diff (`diff_review.patch`, 87 kB, less the plan and the screenshots) and returned before triage: blind hunter (13 findings; floor N = 10), edge-case hunter (11), verification gap (1), intent alignment (descriptive, 8 divergences). Verdicts: 0 high, 3 medium (rows 2, 7, 15), every other row low, and findings two lenses shared are one row. `review: thorough`. The orchestrator's independent review is triaged into this log when it arrives.

| # | Finding (lens) | Verdict | Route and evidence |
|---|----------------|---------|--------------------|
| 1 | `judge`'s comment says the card's fault is judged first, the code tests `binary` first (blind) | low | patch: the comment now gives the code's order and why (a refused chunk was never written). |
| 2 | A read error or failed 1 KB allocation inside `ZipFile::readFileToStream` is a bare `false`, judged `BadSize`, and a good package is renamed `.bad` (blind 2, edge 2 and 9, intent 4) | medium | defer, already the open half of `## 4.3`'s item: `lib/ZipFile` is unchanged by decision. The edge lens's `total == 0` to `SdCard` heuristic is rejected: a corrupt first block also has `total == 0`, so a bad package would keep its popup on every visit. |
| 3 | `listMembers` copies all 37 bytes of `entry.name`, only `kept + 1` of which are set (blind) | low | patch: it copies `strlen + 1`. |
| 4 | `ZipDirectory` and `ZipFile` can disagree: no check of the local header's name, method, or flags, overlapping members, `dataAt` discarded (blind) | low | false as a defect: `ZipFile` reads the central directory's name, method, and sizes and the local header's name and extra lengths only, and the parser validates every one of them; overlapping data is bounded by 32 members of 128 KB. A member ending exactly at the directory's start is every sound zip. |
| 5 | No cap on the sum of the members, up to about 4 MB extracted (blind 5, edge 1) | low | defer (`## 4.6`): AD-15's limits are per member and per package; a sum or free-space check is the owner's call. |
| 6 | The strict EOCD rules (no comment, directory ends at the EOCD) are undocumented and a commented zip is a bare `NotAPackage`; a zip from a common tool is refused (blind 6, edge 5) | low | patch: the `formats.md` deferral names both rules and the golden vector's install shows `pack_game.py`'s zips meet them. Rejected as a defect: accepting a comment safely means repeating `ZipFile`'s own backwards scan, so the two readers agree on which EOCD is real; the strict rule is that agreement at no code. |
| 7 | `MemberGuard`'s cap is unpinned and unreachable through `ZipFile`; the bytecode check duplicates the runtime's own (blind 7, edge 10, intent 3) | medium | patch: `MemberGuard.h` and `MemberGuardTest` (six tests; deleting the cap, the `total == 0` test, or the empty-write guard each fails one). The bytecode check stays: AD-15 and the entry name it. |
| 8 | `PackageLimitsTest` reads the vectors with unbounded `find` scans; a missing key reads the next object's number (blind) | low | patch: `objectOf` bounds each search to one object, a missing key is -1 or empty and fails the test. |
| 9 | The reasons give no numbers or remedy (blind) | low | rejected: a number in a string drifts from its constant (`STR_GAMES_NOT_RESPONDING` needs a `static_assert` for it); the reasons name what to look at. |
| 10 | The tombstone comment where `ImagesOverTheBudgetEndBad` stood; the C++ budget test could keep deflated PNGs (blind, edge 7) | low | patch: the comment is gone (the history is here and in `deferred-work.md`). Rejected: C++ has no deflate in the harness, which is why the generator is Python. |
| 11 | Spine amendment lacks an owner sign-off; `check_layers.py` reflow and long lines; the parser could live in `lib/GameCore` (blind) | low | patch: the comment and set lines rewrapped inside 120 columns, the diff limited to the miniz lines. Rejected: the amendment says what it is (the entry's description states the edge); `touches` puts the parser "beside" the installer. |
| 12 | One limit has three names; `pack_game.py` may not refuse what the list says (blind) | low | false: `api-level-1.txt` names end in their unit, the vectors and C++ keep theirs, and `PackageLimitsTest` and `ApiLevelTest.PackageLimitsMatchTheList` tie all three; `pack_game_test.py:404` asserts the packer's limits against the vectors (verification-gap lens). |
| 13 | An early return leaves the file open; the file is opened twice; a name goes to the log unescaped (blind) | low | rejected: `HalFile`'s destructor closes, one loop task owns the card, and the log line is a diagnostic. |
| 14 | A corrupt `manifest.json` stream now reports `BadSize` or `BadCrc`, not `BadManifest` (edge) | low | rejected: the damage is real and the reason says so. |
| 15 | An EOCD count above 32 over a shorter directory reported `TooMany`, not the `BadDirectory` the matrix promises (edge 4 and 11) | medium | patch: the walk stops at the entry past the count, or past 32; `ZipDirectoryTest` (counts 33, 100, 0xFFFE, a 40-count zip) and `count-above-the-limit`. |
| 16 | `MemberGuard::admit` on a zero-length write with a null pointer would reset the CRC (edge) | low | patch: an empty write returns true first, with a comment; `AnEmptyWriteChangesNothingEvenWithANullPointer`. |
| 17 | The plan says entry 3's tests are unchanged; one is removed (edge) | low | patch: the Code Map and Design Notes say so. |
| 18 | The `Error` to `STR_GAMES_INSTALL_*` mappings have no test (verification gap, intent 8) | low | defer (`## 4.6`): the map is in a screen no host suite builds; entry 5's screen tests should pin it. |
| 19 | Intent alignment: no device run, the parser and `ZipFile` are two readers, scope beyond the list (comment, adjacency), `pack_game.py` untouched, `formats.md` not updated | low | descriptive; each is in Design Notes or `deferred-work.md` (the device run is entry 14's; `pack_game.py` and `formats.md` are outside `touches`). |

The orchestrator's independent review (`scratchpad/4.6/review-all.md`: adversarial, edge-case, and verification-gap lenses over `34fae3ff..6f2aeb3f`) is triaged here and fixed in one follow-up commit. It approved the two out-of-`touches` changes above.

| # | Finding (source: orchestrator's independent review) | Verdict | Route and evidence |
|---|-----------------------------------------------------|---------|--------------------|
| 20 | Adversarial 1: the `.lua` members' sum is unchecked, so a package installs and then fails every launch with `STR_GAMES_SOURCES_TOO_LARGE` (`x-sum-lua-over-loader`, reproduced) | medium | patch: `LUA_SOURCES_BYTES` (moved from `GameAssets`), `Error::SourcesTooBig`, a `limit` line with the CRC and level-1 tests; `at-lua-sources`, `over-lua-sources`, `three-lua-over-the-loader`. Mutation (check removed) fails all three. |
| 21 | Adversarial 1: overlapping members extract about 3.9 MB from 2 KB (`x-overlap`, reproduced) | medium | patch: each member's [local header, data end) must not meet an earlier one's (`BadDirectory`); `overlap-members`. Mutation fails it. |
| 22 | Adversarial 2: the limit comment says members are counted "folders included" | low | patch: "a folder is not allowed". |
| 23 | Verification gap 1: the output cap is not observed end to end; removing both caps passes every crafted case | medium | patch: `cases.txt` carries the most bytes any scratch file may hold; the suite reads the fake card's entry list (a removed file keeps its bytes) and asserts it for `zip-bomb-understated` (100) and `zip-bomb-understated-large` (5,000). Removing both caps now fails both, plus two `MemberGuardTest`s. The `FileSink`-through-the-installer variant is not possible: `ZipFile` stops first, which is what the test observes. |
| 24 | Edge-case 1: `isStem`'s 32-character bound is unreachable (the stored-name buffer refuses a 33-character stem first) | low | patch: a `static_assert` ties `MEMBER_NAME_BYTES` to `MEMBER_STEM_BYTES + 4 + 1`, and `isStem`'s comment says it is a backstop. A direct test of `classify` is not possible (anonymous namespace); `at-member-stem` and `over-member-stem` pin the reachable behaviour. |
| 25 | Edge-case 2: `/.games/hardening/` is never seeded, so a rejected upgrade of the same id is untested | low | patch: `seedInstalledGame` seeds it; every rejection compares it before and after. |
| 26 | Verification gap 2: a stream failing after producing exactly the declared bytes is untested; the picture limits are not tied to the converter | low | patch: `deflate-unterminated` (all bytes, no final block: `BadSize`; removing `!streamed` from `judge` fails it) and `TheConverterAcceptsThePictureLimitsAndRefusesOneOver` (2048 and 3072 accepted, one more refused, by `PngToBmpConverter` itself). The visitor's own `TooManyMembers` stays as the array bound, dead behind the parser's. |
| 27 | Verification gap 3: the reason map has no test | low | record (`## 4.6`, row 18 above); entry 5 or 8 owns it. |

## Design Notes

**Functions rewritten (`git log -L` read; commits 5c49754c, d731afff, c47cceaa).** `listMembers` keeps: the first error wins, `classify` refuses off-whitelist names, `PACKAGE_MEMBERS` bounds the array before each copy, names are sorted (the hash order), duplicates, `manifest.json` and `main.lua` required. `extract` keeps: `hash.ok()` and `finish` failures are `OutOfMemory`, an unopenable output is `SdCard`, a failed write is `SdCard` before any other reason, a failed close is `SdCard`, PNGs convert after their member. `readManifest` keeps the parse and `Manifest::check` errors. `install` keeps `tmpDir` empty until `mkdir` succeeds and `renameTried`. `removeTmp`, `mayRemoveTmp`, `foldersShareClusters` are not touched.

**Parser rules.** The EOCD must be the last 22 bytes (no comment), so `ZipFile`'s own backwards scan for the signature lands on the same record (a fake signature inside a comment cannot make the two disagree). The central directory must end where the EOCD begins and hold exactly the EOCD's count (`BadDirectory`; entries-on-disk counts too, and a count above 32 over a shorter directory is a mismatch, not `TooMany`); the walk stops at the entry past the count or past 32, so it visits at most 32. ZIP64 is a sentinel field or a locator before the EOCD. Each entry's local header must fit and its data end inside the directory's start. A stored member's two sizes must match (the `## 4.3` item). A name with a NUL, or longer than fits, is unusable, so `ZipFile`'s 256-byte skip cannot hide a member. A read that fails is `ReadError` (`SdCard`, the file stays); this closes the parser's half of `## 4.3`'s read-error item, while a failure inside `ZipFile`'s streaming still reads as `BadSize`.

**Streaming.** `MemberGuard::admit` refuses a chunk past the declared size (`ZipFile` already stops a deflate overrun and reads a stored member for exactly its size, so through `ZipFile` the cap never fires; `MemberGuardTest` pins it), refuses ESC as a `.lua`'s first byte, and folds the chunk into `mz_crc32` (an empty write changes nothing: `mz_crc32` answers its initial value for a null pointer). `judge` orders: binary (the guard refused the chunk, so no write happened), card write fault, size, CRC.

**Entry 3's test.** `ImagesOverTheBudgetEndBad` built two 2048 x 511 PNGs from `makePng`'s stored zlib blocks, a package of about 1 MB, so it pinned an install the hardening rightly refuses (over `PACKAGE_BYTES`). It is removed; `at-images-bytes` and `over-images-bytes` (deflated PNGs, exactly at the budget and 2 bytes over) pin the same rule. C++ cannot deflate, so the test could not be kept as written.

**Reason mapping.** `GamesListActivity` maps `Error` to text and no host suite builds it; a new value needs its `case`, which is outside the ticket's `touches`. Assumption for entry 14: the eight labels are added there (one hunk, git merges it into entry 8's rename); moving the map into the installer would need a `src/games` to `lib/I18n` edge the spine does not have.

**Docs.** `docs/crosshatch/formats.md`'s package paragraph is rewritten (orchestrator-approved at the independent review).

**Sources total and overlap (independent review).** `GameAssets::load` refuses `.lua` files over 256 KB together, so a package under the per-member limit could install and then fail every launch. `LUA_SOURCES_BYTES` is now the one constant (`GameAssets::MAX_SOURCE_BYTES` names it), `SourcesTooBig` maps to the existing `STR_GAMES_SOURCES_TOO_LARGE`, and it is a `limit` line. Members whose byte ranges in the zip overlap are `BadDirectory`: the directory pass checks each new member against the earlier ones (at most 496 comparisons), which closes a 30-entries-on-one-stream amplification of about 1,900:1. `pack_game.py` and `package_vectors.json` are outside `touches`: entry 7 adds the source total to both (`## 4.6`); `gen_hardening_packages.py` reads the vector's `lua_sources_bytes` when it exists, and `PackageLimitsTest` does not depend on it, so it needs no tolerance code.

## Verification

Run in this worktree on the tree the commits hold (baseline `34fae3ff7b29a0ea12050626b02803e6efaea957` plus this story: `6f2aeb3f` and the follow-up for the independent review), every build under the shared lock. The figures are those of the follow-up's run; the first commit's were 959 tests and +240,784 B flash.

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass. Result: 966 of 966 passed (863 at the baseline; entry 3's `ImagesOverTheBudgetEndBad` is removed). New: `PackageHardeningTest` 99 (69 crafted packages, one test each; `HardeningTest` 6; `ZipDirectoryTest`; `MemberGuardTest`), `PackageLimitsTest` 4, `ApiLevelTest.PackageLimitsMatchTheList`. The follow-up's mutations (each fails a test): both output caps removed together, the overlap check, the `.lua` total check, `!streamed` in `judge`. Every I/O matrix row has crafted packages or a test: limits at and over (`at-*`, `over-*`), bombs (`zip-bomb-*`, `deflated-size-overstated`, `stored-size-mismatch`), names (`name-*`, `duplicate-member`), bytecode (`binary-*`, `escape-*`), zip layout (`zip64-*`, `method-*`, `encrypted`), directory (`count-*`), CRC (`crc-*`), damaged zips (`no-eocd` and five more), card faults (`ADirectoryThatCannotBeReadKeepsTheFileForTheNextTry`, `AFileThatWillNotOpenKeepsItToo`), and the leftover scratch folder (entry 3's `ALeftoverScratchFolderIsDeletedEvenWithNothingToInstall`, unchanged). Each rejection's test compares every file and folder under `/.games` and `/.games-data` before and after.
- Mutation run (a guard removed, the suites rebuilt, one at a time; the source restored after each): CRC compare, the binary check, the member limit, the package limit, the count check in the walk and at its end, the entries-on-disk check, the stored-size check, the method check, the encrypted flag, the ZIP64 locator, the NUL-in-name check, the local data end, the read-error mapping, the EOCD comment check, the directory-end check, the guard's cap, its first-byte test, and the empty-write guard each fail at least one test. Two survivors led to new cases (`name-nul-tail`, `escape-at-a-chunk-start-*`) and one to a tighter assertion (visits never exceed the EOCD's count); the guard's cap survived until `MemberGuardTest`.
- `pio run -e x4pro`, `pio run -e default` -- expected: build. Result: both SUCCESS (3:52 and 2:56).
- `python3 scripts/check_layers.py` and `python3 scripts/check_layers_test.py` -- expected: pass. Result in the worktree at the follow-up: 416 edges, 51 tests OK. From a fresh archive tree of the first commit's staged tree (`git archive` of the staged tree `bb27a6d77a862bdc75718b740eef640de6108a04`, plus `git submodule foreach --recursive git archive` for every submodule, nested ones included): the same, 415 edges, 51 tests OK (the follow-up adds one include edge, `GameAssets.h` to `PackageLimits.h`, inside the table; the worktree run covers it). The tree is an archive, not a clone, since the check reads no git history.
- `python3 scripts/check_upstream_touches.py` -- PASS (only `english.yaml`, ledger row 2, is an upstream file changed).
- `sim.sh build x4pro` -- SUCCESS (twice: before and after the review fixes).
- `./bin/clang-format-fix` twice -- nothing new in `git status` the second time; it changed only this story's files.

**Measurement** (`check_flash_budget.py`, x4pro, its four steps as written, on the staged tree the commit holds; the pass bar is at most +12,000 B flash and +32 B static RAM over the orchestrator's base):
- `build on` `firmware.bin` 5,917,952 B; `build off` 5,676,880 B; `compare --limit-kib 250 --ram-limit-bytes 1024`: +241,072 B (14,928 B under the 256,000 B gate); static internal RAM +776 B (`.dram0.bss` +8, `.iram0.text` +684, `.iram0.text_end` +84; 248 B under the gate); `objects`: 41 game objects, no problems.
- Over the base's games-on minus off (+228,496 B flash, +776 B RAM, from the brief): **+12,576 B flash, +0 B static RAM**. Entry 3's own measure was +9,312 B, so this story added +3,264 B (this tree's +241,072 B minus entry 3's +237,808 B measured at c47cceaa, both games on minus off; the review follow-up added 288 B of the total). The flash figure is 576 B over the 12,000 B pass bar, and the gate's headroom, 14,928 B, is under the 15,504 B trigger: by the owner's decision (epic Notes, 2026-09-29) the icon-compression story runs before entry 8 unless entry 7's measurement comes out otherwise. The eight new `STR_GAMES_INSTALL_*` keys are in both builds, so they do not show in the difference.

**Screenshots** (`story-hardening-screenshots/`, from `sim.sh build x4pro` and `start x4pro`, `counter` packed by `pack_game.py` and crafted packages from `build/test/game_script/harness/hardening_packages/` in `fs_/games/`; the second build only changed code the screenshots do not show):
- `bad-crc-reason-beside-installed-counter.png` -- Games opened with `counter.cpgame` and `crc-deflated.cpgame` in the inbox: Counter is installed and listed, and the popup reads "crc-deflated.cpgame: A file in it is damaged"; the file is now `crc-deflated.cpgame.bad`.
- `bad-zip-bomb-reason.png` -- Games opened with `zip-bomb-declared.cpgame` (a 10 MB member in 10 KB): the popup reads "zip-bomb-declared.cpgame: A file in it is too large", nothing is installed, and the file is `.bad`.

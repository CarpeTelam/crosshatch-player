---
title: 'Tracer: one package from the inbox to a started game'
type: 'feature'
ticket: '3'
created: '2026-09-29'
status: built
baseline_revision: '4db0787ecacba1a3af00d48f3fdd79f7013af095'
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

**Problem:** Games lists only hand-copied `/.games/<id>/` folders. A person cannot drop a `.cpgame` on the card and play it, and nothing writes the `.pkg` marker, the package hash, or the converted images the loader needs.

**Approach:** `GamePackageInstaller` installs every `/games/*.cpgame` when Games opens (AD-15/16, R1, R3, R4), `GameRegistry` lists installed games from `.pkg`-marked folders (R5), `GameHash` is the one SHA-256 helper, and `GamesListActivity` runs the installer and lists from the registry. A host suite installs entry 2's vector on the harness card; a simulator run plays a packed `counter`.

## Boundaries & Constraints

**Always:** All card access through `Storage` / `HalFile`; `makeUniqueNoThrow` for fallible allocations and locals under 256 B; `.pkg` written last and the inbox file deleted only after it; the old `/.games/<id>/` removed only once the new folder is whole; `/.games-data/<id>/` never touched; `Invalid` manifest ends `.cpgame.bad` with its reason shown once, `Unavailable` installs; a `.bmp` member is off the whitelist; user text through `tr()`, new keys appended to `english.yaml` only; any new include edge in the layer table and `check_layers.py` in the same commit; only files in the ticket's `touches`.

**Never:** Edit `lib/ZipFile`, `lib/miniz`, `lib/PngToBmpConverter`, `lib/hal`, `ActivityManager.*`, `src/activities/home/**`, `GameMatchActivity.*`, the epic file, or the shared harness files; enforce entry 6's hardening (package and member byte limits, zip bombs, CRC, EOCD count, bytecode, ZIP64), the launcher's layout, or resume.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Valid package | `/games/x.cpgame` (entry 2's vector) | files under `/.games/<id>/`, `.pkg` = `v1\n` + hash + `\n`, hash = vector, inbox file gone, registry lists it | none |
| Reinstall | `/.games/<id>/` and `/.games-data/<id>/` exist | old folder replaced, data kept | none |
| Unavailable | `api` above the host's | installs, registry lists it with `Unavailable` | none |
| Invalid manifest | not JSON, solo with 2 seats, bad id | `x.cpgame.bad`, no game, reason shown once | `BadManifest` |
| Bad member | `.bmp`, folder, `..`, duplicate, no `main.lua` | `.bad` | `BadMember` / `NoMain` |
| Images | square `icon.png`; `<name>.png` | `icon.bmp` 64x64; `<name>.bmp` own size, loader layout, PNGs dropped | non-square icon or failed conversion: `.bad` (`BadImage`) |
| Card failure | rename or write fails | file stays in the inbox, `/.games-tmp` removed | `SdCard` |
| Leftover scratch | `/.games-tmp/` from a cut install | deleted when Games opens | none |

</frozen-after-approval>

## Code Map

- `lib/GameCore/PackageLimits.h` -- new: package limits as constants (`PACKAGE_BYTES`, `PACKAGE_MEMBERS`, `MEMBER_BYTES`, name and icon sizes); `GameImages.h` keeps `IMAGES_BYTES` and `MAX_IMAGES`.
- `src/games/GameHash.*` -- SHA-256 (mbedTLS; OpenSSL under `SIMULATOR` or `GAME_HASH_OPENSSL`) and `GamePkg` (member start bytes, package hash, `.pkg` format and parse).
- `src/games/GameRegistry.*` -- `load` (folders with a valid `.pkg` and manifest id = folder, sorted, with `Manifest::check`) and `readPackageHash` (entry 11).
- `src/games/GamePackageInstaller.*` -- `hasInbox`, `installAll`, `Error`, `Report`; one heap `Job`; `ZipFile::enumerateFileEntries` for names, `readFileToStream` into a `ManifestSink` and a `MemberSink` (file + hash); `PngToBmpConverter::pngFileTo1BitBmpStreamWithSize` (target 0 = own size, 64 = icon); `checkImageHeader` / `ImageBudget` verify the output.
- `src/games/GamePaths.h` -- `INBOX_DIR`, `TMP_DIR`, `PKG_NAME`, name and path sizes.
- `src/activities/games/GamesListActivity.*` -- installs in `onEnter` (popup while it runs), lists the registry's solo-startable games, one dismissable failure popup.
- `test/game_script/harness/installer.sources.cmake`, `installer.cmake`, `installer_stubs/`, `InstallerSupport.h`, `GameHashTest.cpp`, `GameRegistryTest.cpp`, `GamePackageInstallerTest.cpp` -- the host suite `GameInstallerTest`.
- `docs/crosshatch/formats.md`, `test/game_script/fixtures/README.md`, `.claude/skills/run-crosshatch-player/SKILL.md`, `lib/I18n/translations/english.yaml` -- docs, fixture placement by packing, `STR_GAMES_INSTALL*` keys.
- Reuse unchanged: `Manifest::parse`/`check`, `ManifestReader`, `gameHostCaps()`, `ZipFile`, `PngToBmpConverter`, `scripts/pack_game.py`, `test/game_core/package_vector.cpgame` and `package_vectors.json`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/PackageLimits.h`, `src/games/GamePaths.h` -- limits and installer paths as named constants.
- [x] `src/games/GameHash.*`, `GameRegistry.*`, `GamePackageInstaller.*` -- as the Code Map says.
- [x] `src/activities/games/GamesListActivity.*`, `english.yaml` -- run the installer, list from the registry, `tr()` texts.
- [x] `test/game_script/harness/*` -- suite `GameInstallerTest`: one test per matrix row plus registry and hash cases.
- [x] `docs/crosshatch/formats.md`, fixtures README, `SKILL.md` -- package format, limits, `.pkg`, hash; fixtures packed into `/games/`; `images`, `bad-image`, `solo` host-only.

**Acceptance Criteria:**
- Given entry 2's vector in `/games/`, when Games opens, then the game is listed, `.pkg` holds the vector's hash, and the inbox and scratch folders are empty.
- Given `counter` packed with `pack_game.py` into `fs_/games/`, when Games opens in the simulator and the row is tapped, then the game plays and its tap changes the count.
- Given a package that fails, when Games opens, then `/.games/` is unchanged and the reason shows once.
- Given `x4pro`, `sticky`, and `default`, when built, then all pass; `check_layers.py` passes; the flash and RAM delta is measured.

## Implementation Notes

The build agent wrote the code itself while investigating, before this plan was drafted; no implementation subagent ran (the plan records the design as built). Files touched are the ticket's `touches`; the two exceptions are recorded under Design Notes.

## Plan Change Log

## Review Triage Log

All four lenses ran as context-free subagents over the staged diff (`diff_review.patch`, 110 kB) and returned before triage: blind hunter (12 findings), edge-case hunter (10), verification gap (2 gaps and 2 other findings), intent alignment (descriptive). Verdicts: 0 high, 3 medium, 9 low, 6 false or out of scope (rejected).

| # | Finding (lens) | Verdict | Route and evidence |
|---|----------------|---------|--------------------|
| 1 | `commit()` removes `/.games/<id>/` before the rename, so a card failure there loses the old game; the doc says a failed install never changes it (blind, edge, gap) | medium | patch: the doc now says only failures before the last step leave the old game whole; `ACardFailureInTheLastStepOfAReinstallIsRetried` pins the rest (game unlisted, file kept, next visit reinstalls). A rename-aside guard adds a step and flash for a rare card fault; AD-16 words the sequence as built, and `/.games-data/` is never at risk. |
| 2 | `PackageLimits.h` says the installer enforces the byte limits, `formats.md` says it does not (blind, edge) | low | patch: the header comment now says what is enforced today. The enforcement itself is entry 6 (frozen Never list). |
| 3 | No CRC check; a corrupt member installs and hashes (blind) | false as a defect of this entry | entry 6 owns CRC (Notes, source conflict); `formats.md` says so. |
| 4 | A `ZipFile` `false` (SD read error, its 1 KB buffers failing) renames a valid package `.bad` (blind, edge) | medium | defer: `lib/ZipFile` is outside `touches` and entry 6's own parser can tell I/O from format errors (`## 4.3`). |
| 5 | Only the first failure is shown; no "+N"; the reason is not kept; stuck retries repeat each visit (blind, edge) | low | defer for "first failure only" (`## 4.3`); the rest rejected: retry-per-visit is the intended card-failure policy, and a `.bad` name is the record. |
| 6 | A 63-byte-or-longer inbox name is skipped without a trace (blind, edge) | low | patch: one `LOG_INF` line. |
| 7 | An `Unavailable` game is installed but not shown by the tracer list (blind, edge, gap, intent) | false | as specified: the registry lists it and entry 8's launcher marks it; the tracer list shows startable games only. `formats.md` reworded to say so. |
| 8 | Same-id reinstall or downgrade overwrites with no notice (blind) | low | patch: `formats.md` states the rule (AD-16 reinstall replaces). |
| 9 | Popup input: other buttons move the list under the popup (edge) | low | rejected: Back, Confirm, and a tap dismiss it and nothing else acts on those inputs; a guard for scrolling beneath it adds a branch for no harm. |
| 10 | Two `hasInbox` scans, install runs synchronously in `onEnter` (blind) | low | rejected: three directory opens per visit are negligible next to the popup and install; the synchronous install is the designed "Installing" state (8 ms in the simulator for `counter`). |
| 11 | No `static_assert` tying `INBOX_PATH_BYTES` to the name size, `Report` bytes to `MAX_PER_RUN` (blind) | low | patch: both added. |
| 12 | `GAME_FIXTURES_DIR` defined, unused; nothing installs the README's fixtures (blind, gap) | medium | patch: `TheFixtureGamesTheReadmeListsInstallAndCanStartSolo` installs all eight through the installer and lists them as solo-startable. |
| 13 | `installer.cmake` patches a generated copy of the shared fake (blind) | low | defer, already logged (`## 4.3`): the shared fake is entry 1's file. |
| 14 | `BAD_MEMBER` text is ungrammatical, names no file (blind) | low | patch: "It holds a file that is not allowed"; the popup already leads with the inbox file's name. |
| 15 | A member name of 256 bytes or more is skipped by `ZipFile` and so dropped from install and hash (edge) | medium | rejected here: entry 6 compares the EOCD count with the enumerated count (epic Notes, Source conflict); the tracer's whitelist cannot see a name `ZipFile` hides. |
| 16 | More than 64 installed games are not all listed; 32 always-failing files starve later ones (edge) | low | rejected: the 64 cap is the list's existing bound, and card failures retry. |
| 17 | `GamesListActivity`'s wiring, filter, and note have no automated test (gap) | low | deferred to entry 5 (the list on the host), which owns screen tests; the simulator run covers the wiring (screenshots). |
| 18 | Hand-copied `/.games/<id>/` folders without `.pkg` no longer list (edge) | false | AD-16: the registry lists only `.pkg` folders; README and `SKILL.md` say so. |
| 19 | Intent alignment: host tests cover the library layer, the simulator run covers the screen (intent) | false | as intended; the run's screenshots are in `story-tracer-screenshots/`. |


## Design Notes

**Design.** `installAll` deletes `/.games-tmp`, lists up to `MAX_PER_RUN` (32) inbox names, then per file: names, manifest, extract in name order, commit. Each member streams once into its file and the package hash together, so the hash covers the zip's bytes (PNGs, not the converted `.bmp`). The installer's `Error` values `SdCard` (not `Storage`, which `HalStorage.h` defines as a macro) and `OutOfMemory` keep the file in the inbox; every other value renames it `.bad`. A reason maps to text in `GamesListActivity`, so `src/games` needs no `lib/I18n` edge; no new include edge was needed (`check_layers.py` unchanged).

**Functions moved.** `GamesListActivity::readManifest` and `loadGames` moved to `GameRegistry::load` (`git log -L` read: the guards are its `Storage.open` null check, `id == dirName`, `check.ok()`, the solo filter, the `dirName[0] != '.'` and length checks, the folder count before allocating, and the OOM early return). All are kept: `readManifest` keeps the open check, parse error, and id check; `dirName` checks and the count-then-allocate order stay; the `check.ok()` and solo filter stay in `GamesListActivity::loadGames` (`Unavailable` games are listed by the registry, not started by the tracer); the OOM return sets no rows. A folder without a valid `.pkg` is new.

**Non-square icon (R3).** `PngToBmpConverter`'s target size scales and never crops, so a non-square icon would come out non-64x64. The installer rejects it (`BadImage`, reason in the log) and also checks the converted header is 64x64. Assumption for entry 14: a non-square `icon.png` is rejected, not scaled and cropped; `scripts/pack_game.py` still accepts one (deferred to entry 7, which touches it).

**Harness.** `ZipFile` seeks relative (`HalFile::seekCur`), which the shared fake card lacks and this ticket may not edit (`test/game_script/harness/stubs/` is entry 1's). `installer.cmake` adds it to a generated copy of the fake's header and stops if the anchor line moves; the suite links no shared library. `PngToBmpConverter` and `InflateStream` build against `installer_stubs/` (panel size, `vTaskDelay`).

**Unknowns settled.** `ZipFile`, `PngToBmpConverter`, and `InflateStream` build and run in the harness (the conversion is tested on the host, sizes 1 to 200 for the icon). OpenSSL 3.0.13 headers are on the host and `find_package(OpenSSL REQUIRED)` finds them. Install time: 8 ms for `counter` and 10 ms for a package with a 32 x 32 `icon.png` in the simulator log (`Entering activity: GamesList` to `Installed ...`); on a device it is unmeasured.

## Verification

All commands ran from this worktree on the final tree (baseline `4db0787ecacba1a3af00d48f3fdd79f7013af095` plus this story's diff), each build under the shared lock.

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass. Result: 846 of 846 passed (794 before, 52 new in `GameInstallerTest`: 3 hash, 2 package-hash, 3 `.pkg`, 11 registry, 33 installer, including the vector install, `.pkg` bytes, hash equal to the vector, registry listing, a bad manifest ending `.bad`, non-square icon, the eight README fixtures).
- `pio run -e x4pro`, `-e sticky`, `-e default` -- expected: build. Result: all three SUCCESS (`x4pro` as `build on` below).
- `python3 scripts/check_layers.py` -- passed (406 edges; no new edge, so the layer table and script are unchanged); `python3 scripts/check_layers_test.py` -- 51 tests OK; `python3 scripts/check_upstream_touches.py` -- PASS (only `english.yaml`, ledger row 2, is an upstream file changed).
- `./bin/clang-format-fix`, twice -- nothing new in `git status` the second time; it changed only this story's files.
- `sim.sh build x4pro` -- SUCCESS (the `SIMULATOR` branch of `GameHash` builds against OpenSSL).

**Measurement** (`check_flash_budget.py`, x4pro, run as written, each step under the lock; the pass bar is at most +12,000 B flash and +32 B static RAM over the orchestrator's base at `962ae61`):
- `build on`: `firmware.bin` 5,912,768 B. `build off`: 5,676,064 B. `compare --limit-kib 250 --ram-limit-bytes 1024`: +236,704 B (19,296 B under the 256,000 B gate); static internal RAM +776 B (`.dram0.bss` +8, `.iram0.text` +684, `.iram0.text_end` +84; 248 B under the gate). `objects`: 41 game objects, no static initializer, no mutable static over 64 B.
- Over the base's games-on minus off (+228,496 B flash, +776 B RAM): **+8,208 B flash and +0 B static RAM**, within the bar with 3,792 B of the epic's 12,000 B left for entries 6, 8, 12, and 13. The seven new `STR_GAMES_INSTALL*` keys count in both builds, so the games-on `firmware.bin` grew 8,896 B over the base's 5,903,872 B (a comparison of two measurements at different commits); the difference measure is the gate's. An earlier build of this tree with `std::sort` instead of the insertion sort measured +237,552 B; the insertion sort saved 848 B.

**Screenshots** (`story-tracer-screenshots/`, from `sim.sh build x4pro` and `start x4pro`, `counter` packed with `pack_game.py` into `fs_/games/`):
- `games-lists-installed-counter.png` -- Games after the install lists Counter.
- `counter-playing-after-two-taps.png` -- the row tapped, the game running, two taps counted.
- `games-bad-package-reason.png` -- a junk `.cpgame` beside two good packages: the reason popup, over the list of the two installed games.
- `games-list-after-dismiss.png` -- after a tap dismisses the popup; the junk file is `...cpgame.bad`, and opening Games again shows no popup.

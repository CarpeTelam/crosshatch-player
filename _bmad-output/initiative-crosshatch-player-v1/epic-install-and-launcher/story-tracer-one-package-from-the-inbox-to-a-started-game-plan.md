---
title: 'Tracer: one package from the inbox to a started game'
type: 'feature'
ticket: '3'
created: '2026-09-29'
status: done
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

All four lenses ran as context-free subagents over the staged diff (`diff_review.patch`, 110 kB) and returned before triage: blind hunter (12 findings), edge-case hunter (10), verification gap (2 gaps and 2 other findings), intent alignment (descriptive). Verdicts: 0 high, 3 medium, 9 low, 6 false or out of scope (rejected). Rows 20 to 31 come from the orchestrator's later review.

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

The orchestrator's independent review (adversarial, edge-case, and verification-gap lenses, `scratchpad/4.3/review-all.md`) followed, and a follow-up commit fixes what it accepted. A finding two lenses shared is one row.

| # | Finding (source: orchestrator's independent review) | Verdict | Route and evidence |
|---|-----------------------------------------------------|---------|--------------------|
| 20 | Adversarial 1, edge-case 2: the old `/.games/<id>/` is removed while its `.pkg` is in place, so a stop partway lists a game with files missing (reproduced) | medium | patch: `commit` removes `<final>/.pkg` first (SdCard if that fails), then `removeDir`. `AReinstallTakesTheMarkerAwayBeforeItRemovesTheOldFiles` (removal fails at `manifest.json`: nothing listed, file kept, retry installs) and `AMarkerThatWillNotGoLeavesTheOldGameWhole`. |
| 21 | (Second half superseded by row 33.) Adversarial 2: a converter `false` (its own allocation, or a card write it ignores) renames a valid package `.bad` (reproduced) | medium | patch: `readPngSize` applies `pack_game.py`'s `png_size` rules to every image (less the IHDR CRC, which the converter skips); a header it refuses is `BadImage`. After a sound header a converter `false` is the new `Error::ConvertFailed` and a short output (`ImageCheck::Truncated`) is `SdCard`: the file stays. Deviation from the orchestrator's wording: a distinct code, not `SdCard`/`OutOfMemory`, because a damaged image is the third possible cause and `SdCard`'s text ("The SD card could not be written") would be the wrong reason; it reuses `STR_GAMES_BAD_IMAGE`, no new string. Tests: `APngTheConverterRefusesEndsBad`, `ADamagedPngWithASoundHeaderStaysInTheInboxForAnotherTry`, `AnImageWrittenShortIsTheCardsFaultNotThePackages`. A damaged image with a sound header now stays in the inbox and shows its reason on every visit until a person deletes it. |
| 22 | Adversarial 3, edge-case 1: `._g.cpgame` (AppleDouble) and other dot names install, fail, and show a popup (reproduced) | medium | patch: `forEachInboxFile` skips names starting with `.`. `HiddenAndSidecarNamesAreNotPackages`. |
| 23 | Adversarial 4: SdFat's folder rename is not atomic; a power loss leaves `/.games-tmp/<id>` and `/.games/<id>` on one cluster chain, and the recursive delete of either frees clusters the other uses | high (device safety) | patch, and Assumption for entry 14 (Design Notes): `removeTmp` now walks `/.games-tmp` and skips a folder whose `/.games/<id>` exists without a `.pkg` (`mayRemoveTmp`); the per-failure cleanup deletes `/.games-tmp/<id>` only when this install made it and has not yet tried the rename, or `mayRemoveTmp` allows it; a `mkdir` that fails leaves the folder alone. Installing that id then fails with `SdCard` each visit (logged "Keeping ..."), the file stays. `ScratchOfAGameWithoutAMarkerIsNeverRemoved`. |
| 24 | Adversarial 5: a failed inbox delete (counted installed) or a failed `.bad` rename is only logged, so the file repeats every visit | medium | patch: both return `SdCard` into `Report` (a delete that fails after a successful install is a failure, not an install). `AnInboxFileThatWillNotDeleteIsReportedAndKept`, `ABadPackageThatWillNotRenameIsReportedAsTheCardsFault`. |
| 25 | Edge-case 3: `/.games-tmp` as a file makes every `mkdir` under it fail (reproduced) | low | patch: `removeTmp` removes it. `AFileWhereTheScratchFolderGoesIsRemoved`. |
| 26 | Verification gap 1: the per-failure `removeDir(job->tmpDir)` is unpinned | low | patch: `AFailedPackageDoesNotBlockTheNextOneWithTheSameId`. |
| 27 | Verification gap 2: `failClose`, `failReadAt`, `failRemove` guards untested (survivors: member close, BMP close, header read) | low | patch: `ACloseThatFailsIsAStorageFailure`, `ReadAndRemoveFailuresOnTheConvertedImageAreStorageFailures`. Each of the three mutants (guard deleted, one at a time) now fails a test; the source was restored. |
| 28 | Verification gap 3: the simulator's cut names (`getNameCuts`) are never exercised | low | patch: `ANameTheSimulatorCutsIsSkippedToo`. |
| 29 | Verification gap 4: a stored member with a larger declared uncompressed size installs as its bytes plus the zip that follows (`ZipFile` reads `uncompressedSize` regardless) | medium | defer to entry 6 (its CRC check and parser); `## 4.3`. |
| 30 | Adversarial 6: `GameHash`'s mbedTLS branch never ran against the vector | low | defer: `## 4.3`; entry 14's device packet checks the vector's `.pkg` reads `0530a15766e91bf1`. Building mbedTLS on the host was not tried. |
| 31 | Approved exception: add `seekCur` to the shared fake and drop the configure-time patch | n/a | done: an orchestrator-approved change to entry 1's file (out of this ticket's `touches`), `stubs/HalStorage.h` gains `seekCur(int64_t)` as `HalFile::seekCur` in `lib/hal`, with `HarnessTest.SeekCurMovesRelativeToThePositionAndRefusesAMoveBeforeTheStart`; `installer.cmake` no longer generates a copy. One line recorded in entry 1's plan, Verification. |

A second round from the orchestrator (2ed5ee9 in its message, `d731afff` on the branch) found two behaviour problems in that follow-up.

| # | Finding (source: orchestrator) | Verdict | Route and evidence |
|---|-------------------------------|---------|--------------------|
| 32 | The row 23 guard also catches the safe case: a stop during `removeDir(finalDir)` leaves `/.games/<id>` without a `.pkg` beside a whole, independent `/.games-tmp/<id>`, so that game is stuck with SdCard each visit and no way out on the device | high | patch: `foldersShareClusters` probes through `Storage`: it makes an empty `.xlink` (off the whitelist) in `/.games-tmp/<id>`, looks for it in `/.games/<id>`, and removes it. Shown, not made, or not removable: keep, log, SdCard (unchanged). Not shown: the folders are independent, the scratch folder goes, the install proceeds. `ScratchBesideAnIndependentHalfRemovedGameIsRemovedAndTheInstallGoesOn`, `ScratchSharingClustersWithAGameWithoutAMarkerIsNeverRemoved`, `ScratchIsKeptWhenTheProbeFileCannotBeMadeOrRemoved`. The shared fake cannot alias two folders, and no shared-fake change was made: the suite models one chain by putting `.xlink` in `/.games/<id>` beforehand, which is what the probe would see. That model cannot show that two real paths on one chain share their directory data, which is the SdFat property the probe relies on (the residual, in the Assumption). |
| 33 | A damaged PNG with a sound header stays in the inbox and shows its reason every visit, against R1's "shown once" | medium | patch: `FileSink`, a `Print` around the converter's output `HalFile`, records a short write. After the converter: a short write or a close that fails is SdCard (file stays); a `false` without one is BadImage (renamed `.bad`, shown once); a `true` whose output is shorter than its header says stays SdCard (as before). `Error::ConvertFailed` and its mapping are gone. The orchestrator's "output shorter than 62 + rowBytes x height" applies to a converter `true` only: after a `false` a short file is what a decode failure leaves. `ADamagedPngWithASoundHeaderEndsBadAndIsShownOnce`, `ADamagedPngOnAFailingCardIsTheCardsFault`, `AnImageWrittenShortIsTheCardsFaultNotThePackages`. Residual: the converter running out of memory reads as BadImage; `## 4.3`. |


## Design Notes

**Design.** `installAll` deletes `/.games-tmp`, lists up to `MAX_PER_RUN` (32) inbox names, then per file: names, manifest, extract in name order, commit. Each member streams once into its file and the package hash together, so the hash covers the zip's bytes (PNGs, not the converted `.bmp`). The installer's `Error` values `SdCard` (not `Storage`, which `HalStorage.h` defines as a macro) and `OutOfMemory` keep the file in the inbox; every other value renames it `.bad`. A reason maps to text in `GamesListActivity`, so `src/games` needs no `lib/I18n` edge; no new include edge was needed (`check_layers.py` unchanged).

**Functions moved.** `GamesListActivity::readManifest` and `loadGames` moved to `GameRegistry::load` (`git log -L` read: the guards are its `Storage.open` null check, `id == dirName`, `check.ok()`, the solo filter, the `dirName[0] != '.'` and length checks, the folder count before allocating, and the OOM early return). All are kept: `readManifest` keeps the open check, parse error, and id check; `dirName` checks and the count-then-allocate order stay; the `check.ok()` and solo filter stay in `GamesListActivity::loadGames` (`Unavailable` games are listed by the registry, not started by the tracer); the OOM return sets no rows. A folder without a valid `.pkg` is new.

**Non-square icon (R3).** `PngToBmpConverter`'s target size scales and never crops, so a non-square icon would come out non-64x64. The installer rejects it (`BadImage`, reason in the log) and also checks the converted header is 64x64. Assumption for entry 14: a non-square `icon.png` is rejected, not scaled and cropped; `scripts/pack_game.py` still accepts one (deferred to entry 7, which touches it).

**Harness.** `installer.cmake` builds a standalone suite over the shared fake card, which gained `seekCur` (the real `ZipFile` seeks relative) as an orchestrator-approved change to entry 1's file, outside this ticket's `touches`. `PngToBmpConverter` and `InflateStream` build against `installer_stubs/` (panel size, `vTaskDelay`).

**Scratch folder safety.** SdFat renames a folder by making the new entry before it removes the old, so a power loss between leaves `/.games-tmp/<id>` and `/.games/<id>` on one cluster chain; a recursive delete of either frees clusters the other still uses. `/.games/<id>` without a `.pkg` is what that looks like, and it is also what an install stopped partway through the removal of the old folder looks like (the `.pkg` is now removed first), beside an independent scratch folder. The probe file (see the Assumption) tells the two apart through `Storage`. No recovery that frees nothing shared exists once a cross-link is seen, and no SDK change was made. Assumption for entry 14: after a power loss during the folder move, `/.games-tmp/<id>` and `/.games/<id>` (no `.pkg`) may share clusters. Where `/.games/<id>` has no `.pkg` the installer probes: it makes an empty `.xlink` in `/.games-tmp/<id>` and looks for it in `/.games/<id>` (folders on one chain share their directory data, so it shows in both), then removes it. Shown, or not makeable or removable: the installer never deletes that `/.games-tmp/<id>`, logs "Keeping /.games-tmp/<id>", leaves the inbox file, and reports the SD card reason on every visit that installs that id, until a person clears both folders from a computer (a filesystem check first is advisable). Not shown: the folders are independent (a stop during the removal of the old folder), the scratch folder is deleted, and the install goes on. The residual risks: the probe rests on SdFat's cross-linked entries sharing directory data, which the host suite can only model and a device run has not shown; and a card left cross-linked by the power loss itself, which the installer neither repairs nor worsens.

**Unknowns settled.** `ZipFile`, `PngToBmpConverter`, and `InflateStream` build and run in the harness (the conversion is tested on the host, sizes 1 to 200 for the icon). OpenSSL 3.0.13 headers are on the host and `find_package(OpenSSL REQUIRED)` finds them. Install time: 8 ms for `counter` and 10 ms for a package with a 32 x 32 `icon.png` in the simulator log (`Entering activity: GamesList` to `Installed ...`); on a device it is unmeasured.

## Verification

Three rounds ran, each on its final tree from this worktree with every build under the shared lock: the first commit (baseline `4db0787ecacba1a3af00d48f3fdd79f7013af095` plus the story), the follow-up that answers the orchestrator's independent review, and a second follow-up for its two behaviour findings (rows 32 and 33). The figures below are the last round's; earlier ones are noted where they differ. The second follow-up built `x4pro` and `default` only (`sticky` passed at the first follow-up; its changes since are `src/games` and the list screen, which `x4pro` compiles the same).

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass. Result: 863 of 863 passed (794 before this story; 846 at the first commit, 860 after the first follow-up; the second adds 3: `GameInstallerTest` 68, `GameHarnessTest` 76).
- `pio run -e x4pro`, `-e sticky`, `-e default` -- expected: build. Result: `x4pro` (as `build on` below) and `default` SUCCESS on the last round; `sticky` SUCCESS at the first follow-up.
- `python3 scripts/check_layers.py` -- passed (406 edges; no new edge, so the layer table and script are unchanged); `python3 scripts/check_layers_test.py` -- OK; `python3 scripts/check_upstream_touches.py` -- PASS (only `english.yaml`, ledger row 2, is an upstream file changed).
- `./bin/clang-format-fix`, twice -- nothing new in `git status` the second time; it changed only this story's files.
- `sim.sh build x4pro` -- SUCCESS at both commits (the `SIMULATOR` branch of `GameHash` builds against OpenSSL). The follow-up changes no screen and no string the screenshots show, so they are the first commit's.

**Measurement** (`check_flash_budget.py`, x4pro, run as written, each step under the lock; the pass bar is at most +12,000 B flash and +32 B static RAM over the orchestrator's base at `962ae61`):
- `build on`: `firmware.bin` 5,913,872 B. `build off`: 5,676,064 B. `compare --limit-kib 250 --ram-limit-bytes 1024`: +237,808 B (18,192 B under the 256,000 B gate); static internal RAM +776 B (`.dram0.bss` +8, `.iram0.text` +684, `.iram0.text_end` +84; 248 B under the gate). `objects`: 41 game objects, no static initializer, no mutable static over 64 B.
- Over the base's games-on minus off (+228,496 B flash, +776 B RAM): **+9,312 B flash and +0 B static RAM**, within the bar with 2,688 B of the epic's 12,000 B left for entries 6, 8, 12, and 13. The same measure was +8,208 B at the first commit, +9,120 B after the first follow-up, and +9,312 B now (the probe and the write-noting sink cost 192 B; `Error::ConvertFailed` going saved some of it). The seven `STR_GAMES_INSTALL*` keys count in both builds, so the games-on `firmware.bin` is 10,000 B over the base's 5,903,872 B (two measurements at different commits); the difference measure is the gate's.

**Screenshots** (`story-tracer-screenshots/`, from `sim.sh build x4pro` and `start x4pro`, `counter` packed with `pack_game.py` into `fs_/games/`; taken at the first commit):
- `games-lists-installed-counter.png` -- Games after the install lists Counter.
- `counter-playing-after-two-taps.png` -- the row tapped, the game running, two taps counted.
- `games-bad-package-reason.png` -- a junk `.cpgame` beside two good packages: the reason popup, over the list of the two installed games.
- `games-list-after-dismiss.png` -- after a tap dismisses the popup; the junk file is `...cpgame.bad`, and opening Games again shows no popup.

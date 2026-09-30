---
title: 'A remove that stops partway is finished at the next visit (e4-z2, A18)'
type: 'bugfix'
ticket: ''
created: '2026-09-30'
status: 'built'
baseline_revision: '07a95af23bc0e44d52796c4cb138444d13f77284'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GamePackageInstaller::remove(id)` deletes `/.games/<id>/.pkg` and then runs `removeDir`. A power loss or card fault between them leaves an unlisted `/.games/<id>/` with its files, and nothing reclaims the space until the same id is reinstalled (`deferred-work.md` `## 4.10` first item, `## 4.13`; owner accepted the fix as Assumption A18).

**Approach:** `remove` first writes an empty `/.games/<id>/.removing` marker through `Storage`; the installer's per-visit work (`installAll`, beside `removeTmp`) then finishes the remove of every `/.games/<id>/` that holds the marker, with every guard `remove` has. A folder without the marker is never touched.

## Boundaries & Constraints

**Always:** Marker before `.pkg`, `.pkg` before `removeDir` (which removes the marker). Keep every guard of `remove` (id validation, `/.games` opens, the `.xlink` cluster probe when there is no `.pkg` and `/.games-tmp/<id>` exists, `SdCard` without deleting). Marker write fails: `Error::SdCard`, nothing changed. Never touch `/.games-data/<id>/`. Per-visit work is bounded by `MAX_PER_RUN`. Every frame in `GamePackageInstaller.cpp` at 256 B or less on x4pro. No mutable static. Files: `src/games/GamePackageInstaller.{h,cpp}`, `src/games/GamePaths.h`, host installer tests and harness, `docs/crosshatch/formats.md`, this plan, `deferred-work.md` `## e4-z2`.

**Never:** A general sweep of `/.games` (it would delete hand-copied folders). No launcher, registry, or `Report` change. No upstream file.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Remove, whole game | listed game | ops: marker written, `.pkg` removed, files removed, marker removed with the folder; `None` | none |
| Marker will not write | `failOpenWrite` on marker, or `failClose` | `SdCard`; `.pkg`, files, listing unchanged; no marker left | logged |
| Stop after marker | `.pkg` will not go | `SdCard`; next `installAll` (card back) removes the folder | data kept |
| Stop after `.pkg` | a file will not go | `SdCard`, unlisted; next `installAll` removes the folder | data kept |
| Hand-copied folder | no `.pkg`, no marker | untouched by `installAll` | none |
| Marked folder, shared clusters | no `.pkg`, `/.games-tmp/<id>` exists, probe shows or fails | folder kept, `SdCard` logged; `remove` writes no marker | probe file removed |
| Marked folder, name not a vetted id | `/.games/Bad_Name/.removing` | untouched | none |
| More than `MAX_PER_RUN` marked folders | 40 marked folders | 32 finished this visit, the rest the next | none |

</frozen-after-approval>

## Code Map

- `src/games/GamePackageInstaller.cpp` -- `remove` (validate, open `/.games`, exists, `.pkg`, probe, `.pkg` first, `removeDir`), `foldersShareClusters` (`.xlink` probe), `mayRemoveTmp`/`removeTmp`, `installAll` (calls `removeTmp` first, returns early with no inbox). Reuse `MAX_PER_RUN`, the `name[INBOX_NAME_BYTES]` directory-scan idiom of `removeTmp`/`countInstalledGames`.
- `src/games/GamePackageInstaller.h` -- comments for `installAll` and `remove`; no signature change (`Report` is not extended).
- `src/games/GamePaths.h` -- add `REMOVING_NAME` beside `PKG_NAME`.
- `test/game_script/harness/GameRemoveTest.cpp` -- the host installer suite for remove (fake card, `installer.cmake`'s `game_installer_src`); new cases go here; `InstallerSupport.h` has `opIndex`, `exists`, `childrenOf`.
- `docs/crosshatch/formats.md` -- installer section (step 3, and the `/.games-tmp` paragraph); add the remove paragraph.
- Not changed: `GamesLauncherActivity` (it calls `installAll` on every build and `remove` on confirm), `GameRegistry`, `HalStorage`, the SDK's `removeDir`.

## Tasks & Acceptance

**Execution:**
- [ ] `src/games/GamePaths.h` -- `REMOVING_NAME = ".removing"` -- one name for code and tests
- [ ] `src/games/GamePackageInstaller.cpp` -- split `remove` into the id check and `removeVetted(id)`; add `writeRemovingMarker`, `hasRemovingMarker`, `finishRemovals`; call `finishRemovals()` in `installAll` before the inbox check -- the fix
- [ ] `src/games/GamePackageInstaller.h` -- document both -- contract
- [ ] `test/game_script/harness/GameRemoveTest.cpp` -- the eight matrix rows plus reinstall over a marked folder -- coverage
- [ ] `docs/crosshatch/formats.md` -- remove and `.removing` -- format record
- [ ] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## e4-z2`: "Resolved by e4-z2" plus anything deferred

**Acceptance Criteria:**
- Given a remove stopped after the marker or after the `.pkg`, when Games next opens, then the folder is gone and `/.games-data/<id>/` is whole.
- Given a folder with neither `.pkg` nor marker, when Games opens, then no card call names a path in it.
- Given the flash budget gate, then games on minus off is within +240,496 B flash and +808 B static RAM.

## Implementation Notes

Implemented directly in the foreground by the build agent (the orchestrator's brief asks for it; no implementation subagent). Route `full` by the plan-size rule, since the change is over 100 lines with its tests.

- `GamePaths.h`: `REMOVING_NAME`.
- `GamePackageInstaller.cpp`: `remove` is now the id check, the `/.games` open check, and `removeFolder(id)`; `removeFolder` is the old body with the marker step added after the probe; `isGameId`, `hasRemovingMarker`, `writeRemovingMarker`, `finishRemovals` are new; `installAll` calls `finishRemovals()` after `removeTmp()`. A `static_assert` ties `MAX_ID_BYTES` to the name buffer.
- Frames: `writeRemovingMarker` and `finishRemovals` are `[[gnu::noinline]]`. The first build inlined the marker writer into `removeFolder` and put that frame at exactly 256 B; noinline brought it to 160 B.
- Tests: 12 new cases and 2 one-line additions to existing cases in `GameRemoveTest.cpp`; `.removing` and `.pkg` join the off-whitelist member names in `GamePackageInstallerTest.cpp`. A mutation check (dropping the marker test from `finishRemovals`) fails 5 of them, among them the hand-copied folder case.
- The `_bmad/render/` folder the workflow renders is untracked scratch and is not committed.

## Plan Change Log

- Follow-up commit (orchestrator ruling on independent review finding ae 1): the frozen line "`.pkg` before `removeDir` (which removes the marker)" is superseded for the last step. The folder is now deleted with the marker last (`removeFolderMarkerLast`), because FAT slot reuse can list the marker ahead of other files. The frozen block is not edited; this entry and Design Notes carry the change. KEEP: marker before `.pkg`, `.pkg` before the rest, the probe before the marker, every guard of `remove`.

## Review Triage Log

The four lenses ran as context-free subagents, in the foreground, in one message, and all four returned: `blind-hunter` (11 findings), `edge-case-hunter` (10), `verification-gap` (none), `intent-alignment` (descriptive). Verdicts (high/medium/low/false): 0 / 0 / 11 / 4 over 15 rows (one row can merge several lenses' findings). Nothing routed to intent_gap or bad_plan.

| # | Lens | Finding | Verdict | Route and evidence |
|---|------|---------|---------|--------------------|
| 1 | blind | `source_plan` cites a plan that is not in the checkout | false | The plan is in this commit. |
| 2 | blind, edge | 32 or more permanently stuck marked folders starve the rest (a stuck folder uses a slot) | low | reject as unlikely (needs 32 stopped removes on a card that keeps refusing, of at most 64 games), fix adds branches. Design Notes wording corrected: "cannot hold back forever" became "a few". |
| 3 | blind | no check the name buffer fits an id | low | patch: `static_assert(MAX_ID_BYTES + 2 <= INBOX_NAME_BYTES)`. |
| 4 | blind | no stack measurement for the new chain | false | Frames measured with `-fstack-usage` (Verification). |
| 5 | blind, edge | deleting a folder while `/.games` is listed | false | `removeTmp` removes folders of `/.games-tmp` while listing it, and SDCardManager::removeDir removes children while listing; both run on devices. |
| 6 | blind, edge | every visit lists `/.games` and stats each folder | low | reject: bounded by the folders present (at most 64 games plus stray ones), like `countInstalledGames`; device cost unmeasured. |
| 7 | blind, edge, VG | a failed marker close whose cleanup delete also fails leaves a marker on a listed game | low | patch: the failed delete is logged, the comment says the marker then stays and the next visit finishes the remove; a test pins it. |
| 8 | blind, intent | a remove that reported `SdCard` after step 1 is finished at the next visit without a second request | low | reject: it is A18's design (finish every marked folder); `formats.md` and the header now say so. |
| 9 | blind | nothing tests that a package cannot carry `.removing` | low | patch: `.removing` and `.pkg` join `AMemberOffTheWhitelistEndsBad`. |
| 10 | blind | no log tells the unmarked-folder gap from an interrupted install | low | reject: `GameRegistry` is outside this ticket's files; the gap is deferred with its trigger. |
| 11 | blind | the stuck-folder test assumes the fake's listing order | low | patch: comment says the fake lists in creation order. |
| 12 | edge | a shared pair is probed twice a visit (`removeTmp`, then `finishRemovals`) | low | reject: rare state a person must clear; two small writes. |
| 13 | edge | acceptance line "no card call names a path in the hand-copied folder": `exists("<folder>/.removing")` does | low | reject: it is a stat; the test forbids remove, write, and rename, which is what the frozen line means. The frozen block is not edited. |
| 14 | edge | a card that cannot create the marker can no longer remove a game (a full or read-only card) | low | reject: the brief's rule ("cannot be written: `SdCard`, change nothing"); a one-entry create in an existing folder needs no free cluster in practice. |
| 15 | intent | expectation lives at real FAT power loss; tests use the fake with failing calls | false | descriptive; the residual span (marker deleted before the last files) is stated in Design Notes and deferred with a trigger. |

### Independent review of 1d13ad18 (adversarial and edge lens `ae`, verification-gap lens `vg`)

Verdict counts: 0 high, 0 medium, 12 low, 1 false. Fixed in a follow-up commit on top of 1d13ad18 (not an amend). The orchestrator ruled on ae 1, ae 3, vg F1, vg F2, and the records.

| # | Finding | Verdict | Route and action |
|---|---------|---------|------------------|
| ae 1 | `removeDir` can delete `.removing` before other files (FAT slot reuse; 9 to 13% of layouts); a stop leaves an unmarked folder | low, real | patch (orchestrator: must fix): `removeFolderMarkerLast`; test `AMarkerListedBeforeTheFilesIsTheLastEntryDeletedSoAStopIsStillFinished` (marker first in the fake's order, stopped mid-remove, finished next visit) and `ASubfolderInAMarkedFolderGoesBeforeTheMarker`; Design Notes and `formats.md` updated. |
| ae 2 | `.pkg` delete failed: launcher says "could not remove", the next visit deletes the game | low | record, no change: the person chose Remove; one line in `formats.md` and Design Notes. |
| ae 3 | `formats.md` sentence misplaced (reverses the resume rule); double-failure caveat missing | low | patch: paragraph rewritten, caveat restored. |
| ae 4 | as ae 3, the doc overstates "taken away again" | low | patch with ae 3. |
| ae 5 | full card: the marker may need a new directory cluster, so Remove cannot run | low | record (`deferred-work.md` `## e4-z2`); the brief's rule is `SdCard` and no change. |
| ae 6 | FAT matches `.REMOVING` without case; a hand-copied folder holding one is swept | low | record; a package cannot carry any variant (reviewer's test), so only a person-made folder could. |
| ae 7 | 32 permanently stuck folders starve the rest | low | already row 2 above. |
| ae 8 | no popup while finishing | low | record (`formats.md` says none is drawn). |
| vg F1 | nothing pins `finishRemovals` before the installs (mutation M24 survived) | low | patch: `AFinishedRemoveFreesRoomForAnInstallInTheSameVisit` (64 games, one marked, a new package installs in the same visit); with the call moved after the installs it fails. |
| vg F2 | nothing pins that a retry does not rewrite the marker (M13 survived) | low | patch: `ARetryOverAnExistingMarkerDoesNotRewriteIt` (`remove` and a visit, close failing on the marker); with the guard dropped it fails. |
| vg F3 | the fake appends entries, so FAT slot order never shows | low | covered by ae 1's test, which lists the marker first by construction; on-card order stays a device recheck. |
| vg prose | "3 one-line additions" | false as a count, low as a slip | patch: the diff has 2; corrected. |
| vg mutations M07b, M14, M15 | survivors judged equivalent by the reviewer | low | none needed. |

## Design Notes

History read with `git log -L` on `remove`, `removeTmp`, and `foldersShareClusters` (5c49754c, d731afff, c47cceaa, 21e3c7dc, a941ee1d, b976e765). Every guard of `remove` is kept, in `removeFolder` (the body both `remove` and `finishRemovals` run):

- **Id check** (`isGameId`, moved out of `remove` unchanged; 21e3c7dc, braces from a941ee1d): keeps a path such as `../.games-data` out of `/.games`. `remove` returns `BadManifest` and makes no card call. `finishRemovals` uses it as well, so a folder named `Bad_Name` or `-x` with a stray `.removing` is skipped.
- **`/.games` must open** (a941ee1d): "not there" needs a card that answers. It stays in `remove` only; `finishRemovals` has just opened `/.games` to list it.
- **Nothing there is done** (`None` when `/.games/<id>` is absent).
- **Cluster probe** (d731afff, c47cceaa, a941ee1d): with no `.pkg` and `/.games-tmp/<id>` present, `foldersShareClusters` decides; a shared or unsure pair returns `SdCard` and deletes nothing. It runs before the marker is written, because a file written into a folder that shares clusters would show in the scratch folder too, and the brief says a refused remove changes nothing.
- **`.pkg` before `removeDir`** (d731afff): `removeDir` deletes in directory order; the `.pkg` (written last) would otherwise go last and a stop would leave a listed game with files missing. The marker is written before it, so a stop after the `.pkg` leaves a folder the next visit can identify.
- **One path buffer** (b976e765): `removeFolder` reuses one `char[96]`. The marker helpers each carry their own 96 B buffer in their own frame.
- **`/.games-data` and `/.games-tmp` untouched**, except the probe's own file.

Order in `installAll`: `removeTmp()`, then `finishRemovals()`, then the inbox. `removeTmp` first, because it leaves an independent scratch folder gone before the resume probes, and a shared one kept either way. `finishRemovals` sits before `hasInbox()`'s early return, so it runs on every visit, and before installs, so a folder it clears frees room under `wouldBeOverTheLimit`.

Bound: `MAX_PER_RUN` (32) folders tried a visit, counting one that fails, so a few stuck folders cannot hold back those behind them (each costs one slot; only 32 or more permanently stuck folders in front would, which needs 32 removes stopped on a card that keeps refusing). The scan itself is one directory listing of `/.games` and one `exists` per folder name that passes `isGameId`.

Marker choices: it is an empty file (`REMOVING_NAME`). A marker whose `close()` fails is removed again, best effort, so a refused remove leaves the game whole and unmarked; if that delete fails too it is logged and the next visit removes the game, which the person asked for (a test pins it). `remove` on a folder that already has a marker (a retry) writes no second one. A listed game that holds a marker (a stop right after the marker) is removed at the next visit: the person asked for it and the marker is only written by a remove.

Marker last (follow-up commit, independent review ae 1): the first commit let `removeDir` delete `.removing` with the rest. FAT reuses freed directory slots, so a marker made after the game's files can be listed ahead of some of them (the reviewer's model of SdFat's first-fit allocation put it not last in 9 to 13% of layouts), and a stop after `removeDir` deleted it left an unmarked, unlisted folder that nothing reclaims. `removeFolderMarkerLast` now deletes every other entry (a subfolder with `removeDir`), then `.removing`, then the folder with `rmdir`, so the marker outlives every file it stands for. Its guards: a child that fails to delete returns false with the marker in place; the marker name is matched without case, as FAT does; a child whose name does not fit the buffer (`getName` gives 0, a name no game has) sends the whole folder to `removeDir`, which takes the marker in its own order, rather than leaving a folder that could never finish. `remove` and `finishRemovals` share it through `removeFolder`. `commit()` still uses `removeDir` for a reinstall: it has no marker, and its inbox file stays until the install succeeds, so the next visit retries.

A retry does not rewrite an existing marker (`!hasRemovingMarker(id) &&`): a rewrite whose close failed would delete the marker of a folder whose `.pkg` is already gone and strand it (independent review vg F2, pinned by `ARetryOverAnExistingMarkerDoesNotRewriteIt`). A `remove` whose `.pkg` delete failed is finished at the next visit though the launcher said "could not remove": the person chose Remove, and finishing it honours that (ae 2; `formats.md` says so).

The resume result is logged, not added to `Report`: the launcher's note shows inbox files, and a remove the person started earlier would print a reason with no file name on every visit. No signature or launcher change.


## Verification

Two rounds. Round 1 (commit 1d13ad18): 5 full host runs, all scripts, the four budget steps, both firmware builds, all green; figures in that commit's history (+232,096 B flash, +784 B RAM). Round 2 (the follow-up commit, at its final code; logs `scratchpad/e4-z2/h-*.log`), every build and test under the shared lock:

- Host suites: `cmake --build build/test` rc 0, then `ctest --test-dir build/test -j8` three times: each `100% tests passed, 0 tests failed out of 1348`. `GameRemoveTest` has 29 cases. Mutations against the new tests: `removeDir` instead of `removeFolderMarkerLast` fails `AMarkerListedBeforeTheFilesIs...` and `ASubfolderInAMarkedFolder...`; dropping `!hasRemovingMarker(id)` fails `ARetryOverAnExistingMarkerDoesNotRewriteIt`; `finishRemovals` after the installs fails `AFinishedRemoveFreesRoomForAnInstallInTheSameVisit`.
- `scripts/*_test.py`: all 11 exit 0. `check_layers.py`: passed (469 include edges in 107 game files). `check_upstream_touches.py`: `Result: PASS` (every touched file is fork-only).
- `pio run -e x4pro` (inside `build on`, with `-fstack-usage`): SUCCESS. `pio run -e default`: SUCCESS.
- `check_flash_budget.py`, four steps at the follow-up's final code (both builds with `PLATFORMIO_BUILD_FLAGS=-fstack-usage`, no code change): `build on` rc 0, `build off` rc 0, `compare --limit-kib 250 --ram-limit-bytes 1024` rc 0: flash on 5,911,488 B, off 5,678,912 B, **+232,576 B** (bar +240,496 B; 23,424 B to spare under the 256,000 B limit); static RAM on 187,848 B, off 187,064 B, **+784 B** (bar +808 B; 240 B to spare); `objects` rc 0: 43 game objects, largest mutable static 4 B, none added. Round 1's +232,096 B at 1d13ad18 was measured the same way, so the follow-up adds +480 B flash and no RAM.
- Frames of `GamePackageInstaller.cpp` on x4pro (`-fstack-usage`, final code). Touched or new: `removeFolderMarkerLast` 224 B (256 B with a 64 B name buffer, so the name buffer is `MEMBER_NAME_BYTES + 3`, with `static_assert`s), `removeFolder` 160 B, `finishRemovals` 144 B, `writeRemovingMarker` 144 B, `hasRemovingMarker` 144 B, `installAll` 176 B, `remove` 48 B. Untouched maximum: `removeTmp` and `foldersShareClusters` 240 B. Largest in the file: 240 B.
- `./bin/clang-format-fix` twice as the last step: see the final report (nothing new in `git status`).

Assumption for entry 14: remove a game from the Games list: it is removed as before, and nothing else changes (the list, the popup, and the saved data are as they were); a remove that a power loss or a card fault stopped partway is finished the next time Games opens, and a folder someone copied into `/.games/` by hand is left alone.

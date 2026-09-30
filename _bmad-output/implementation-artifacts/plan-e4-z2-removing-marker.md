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
- Tests: 12 new cases and 3 one-line additions in `GameRemoveTest.cpp`; `.removing` and `.pkg` join the off-whitelist member names in `GamePackageInstallerTest.cpp`. A mutation check (dropping the marker test from `finishRemovals`) fails 5 of them, among them the hand-copied folder case.
- The `_bmad/render/` folder the workflow renders is untracked scratch and is not committed.

## Plan Change Log

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

Known limit: `removeDir` (in `freeink-sdk`, not ours) deletes in directory order, so `.removing` can go before the last files; a stop in that span leaves an unmarked, unlisted folder (deferred, `## e4-z2`).

The resume result is logged, not added to `Report`: the launcher's note shows inbox files, and a remove the person started earlier would print a reason with no file name on every visit. No signature or launcher change.


## Verification

Scratch logs are under `scratchpad/e4-z2/` (`f-*.log`). Every build and test ran under the shared lock, at the final code (the review patches included).

- Host suites: `cmake --build build/test` rc 0, then `ctest --test-dir build/test -j8` five times: each `100% tests passed, 0 tests failed out of 1344`. `GameRemoveTest` has 25 cases (12 new); a mutation that sweeps every folder without the marker fails 5 of them, among them `AFolderWithNoMarkerIsNeverTouchedByAVisit`.
- `scripts/*_test.py`: all 11 exit 0 (`check_api_freeze_test`, `check_flash_budget_test`, `check_layers_test`, `check_upstream_touches_test`, `fork_common_test`, `fork_release_test`, `game_codec_test`, `gen_game_icons_test`, `pack_device_run_test`, `pack_game_test`, `sim_sh_test`).
- `check_layers.py`: "469 include edges in 107 game files follow the spine's layer table ... passed".
- `check_upstream_touches.py` (an `upstream` remote, `develop` fetched, not shallow): `Result: PASS`. Every file this change touches is fork-only (`git cat-file -e upstream/develop:<path>` fails for each).
- `pio run -e x4pro`: SUCCESS (with `-fstack-usage`, first at 4:27, again at 1:42 after the noinline change, and once more inside `build on`). `pio run -e default` (C3): SUCCESS, 3:04.
- `check_flash_budget.py`, four steps at the final code (both builds with `PLATFORMIO_BUILD_FLAGS=-fstack-usage`, which adds `.su` files and no code): `build on` rc 0, `build off` rc 0, `compare --limit-kib 250 --ram-limit-bytes 1024` rc 0: flash on 5,911,008 B, off 5,678,912 B, **+232,096 B** (bar +240,496 B, limit 256,000 B: 23,904 B to spare); static RAM on 187,848 B, off 187,064 B, **+784 B** (bar +808 B, limit 1,024 B: 240 B to spare; no mutable static added); `objects` rc 0: 43 game objects, largest mutable static 4 B, no static initializer. The recorded +231,408 B is a figure from an earlier commit measured the same way; the +688 B difference is not a like-for-like delta, since the base commit was not rebuilt here.
- Frames of `GamePackageInstaller.cpp` on x4pro (`-fstack-usage`, `GamePackageInstaller.cpp.su`, final code). Touched or new: `removeFolder` 160 B, `finishRemovals` 144 B, `writeRemovingMarker` 144 B, `hasRemovingMarker` 144 B, `isGameId` 32 B, `remove` 48 B, `installAll` 176 B. Untouched maximum: `removeTmp` and `foldersShareClusters` 240 B. Largest frame in the file: 240 B. The first build inlined the marker writer into `removeFolder` and made it 256 B; `noinline` on the writer and `finishRemovals` fixed it.
- `./bin/clang-format-fix` twice as the last step: see the final report (nothing new in `git status`).

Assumption for entry 14: remove a game from the Games list: it is removed as before, and nothing else changes (the list, the popup, and the saved data are as they were); a remove that a power loss or a card fault stopped partway is finished the next time Games opens, and a folder someone copied into `/.games/` by hand is left alone.

---
title: 'e5-r4: saveStore writes store.bin through the shared replaceFile'
type: 'refactor'
ticket: ''
created: '2026-10-03'
status: 'done'
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
baseline_revision: '1ebf2fb9bd659e9aef297169b9613d76decdeaa9'
context: ['AGENTS.md', 'docs/crosshatch/orchestrated-epics.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Retrospective finding D1: `GameSaveStore::saveStore` carries an inline copy of the tmp-then-rename sequence that `replaceFile` (used by `saveResume` and `savePrefs`) already holds, so a fix to one copy can miss the other.

**Approach:** `saveStore` builds its header and calls `replaceFile`; the sequence exists once. No behaviour, format, or log-text change. Touches only `src/games/GameSaveStore.cpp`, `.h`, `test/game_script/GameSaveStoreTest.cpp`, `ResumeMatchTest.cpp` (pinning only), and the `## e5-r4` deferred-work entry.

</frozen-after-approval>

## Implementation Notes

Oneshot: about 40 lines moved, mostly deleted. The `.h` and `ResumeMatchTest.cpp` needed no change.

Design Notes (guards before moving code; history read with `git log -L`: saveStore came in 5f8582c9, got its leftover-tmp promotion in fdd82fda; `replaceFile` was copied from it in 68ec417b and gained `removedOld` in c147fb1c):

- Guards kept, now once in `replaceFile`: (1) `ensureDirectoryExists` first, logging `cannot create <dir>`; (2) promotion: a tmp with no `path` is the only copy (a crash or failure between remove and rename), and opening the tmp for write truncates it, so it is renamed into place before the open (log `cannot rename <tmp> to <path>`, return false, nothing written); (3) write head then body, close, and on any failure log `cannot write <tmp>`, remove the tmp, return false with the old `path` untouched; (4) the old `path` is removed only when it exists (SdFat's rename refuses an existing target), a failed remove logs `cannot replace <path>` and keeps tmp and old; (5) rename tmp to path, log `cannot rename` on failure, tmp then holds the only copy and loaders read it.
- Kept in `saveStore`: the size gate (`empty` or over `STORE_LIMIT` logs `refused a N-byte store`, no card access) and the header build.
- Differences between the two copies: none deliberate. Same log strings, same step order, same tmp-removal and old-file-kept behaviour on each failure; `replaceFile` only adds `removedOld`, which `saveStore` does not pass. The header is now built before `ensureDirectoryExists` instead of after; building it touches no card. The two explanatory comments moved into `replaceFile`, reworded for `path`/loaders.

## Review Triage Log

All four lenses ran as context-free subagents and returned (edge-case-hunter returned no findings; verification-gap: none).

- blind-hunter 1, no reload after a failed rename: false, `AFailedRenameKeepsTheWholeTmpForTheNextLoad` reopens and checks the slot.
- blind-hunter 2, other replaceFile branches untested: false, existing tests pin promotion failure, the three write faults, remove failure and `removedOld` (`TheReplacementCounterMovesWhen...`).
- blind-hunter 3, equivalence unchecked: false, both bodies were compared line by line and `git log -L` read (Design Notes).
- blind-hunter 4, test comment names a ticket: low, patched (comment now says what the tests pin).
- blind-hunter 5, deferred entry unclear: low, patched (wording and trigger).
- intent-alignment: no divergence; notes that no test fails if a writer regains a private copy and that ResumeMatchTest.cpp is unchanged: low, rejected (no pinning was needed there; the host cannot test structure and the fix would add machinery).

## Verification

Pinning (before the refactor, green on unchanged code, 75 GameSaveStore tests from 71): `AFolderThatCannotBeCreatedStopsTheWriteBeforeAnyFileIsTouched`, `AFirstWriteHasNoRemoveAndRenamesTheTmpIntoPlace`, `AFailedRenameAfterTheRemoveIsLoggedAndTheTmpHoldsTheOnlyCopy`, `ALeftoverTmpBesideStoreBinIsOverwrittenNotPromoted`. Existing store tests already pin write faults (open, second write, close), rename failure, remove failure, promotion and its failure, two failures in a row, and the op order; none edited.

Results (all on the tree committed as this plan's commit; the diff of the tree is the commit's):
- Host: full ctest 1626/1626 pass (four pins added, 75 of 75 GameSaveStore tests green on the unchanged code first), `./bin/clang-format-fix` twice with a clean status, `check_upstream_touches.py` PASS.
- Review lenses: four context-free subagents (see the log).
- `pio run -e x4pro` and `-e default` succeed; `pio check` for default and `-e x4pro` at low, medium and high: "No defects found" both.
- Flash/RAM delta, method: `check_flash_budget.py` build on, build off, compare, objects, run once on this tree and once on base 1ebf2fb9 (GameSaveStore.cpp restored by `git stash`), same machine, same cache. x4pro firmware.bin games-on minus games-off: +253,584 B here, +253,888 B on base, a delta of -304 B; games-on flash used 5,928,398 B vs 5,928,690 B (-292 B); static RAM (the compare's second table) is equal on both (RAM 101,832 B on both). Games objects: no static initializer, largest mutable static 4 B.

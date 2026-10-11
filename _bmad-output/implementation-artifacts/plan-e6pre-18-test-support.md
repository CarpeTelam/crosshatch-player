---
title: 'e6pre-18 shared test support for the save and match suites'
type: 'refactor'
ticket: ''
created: '2026-10-10'
baseline_revision: '81cdfd3dad2a15a1d37af3ebba31eaaef17e6b49'
status: 'done'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
deferred: []
followup_review_recommended: false
context: ['{project-root}/.skills/refactor-for-review/SKILL.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The same test builders are defined in two or more suites (the 8-byte package hash and its one-byte twin, the `resume.bin` byte builders, `snapshotOf`, `manifestJson`, `addGame`, `dropMatch`, `holds`, `tapCell`), and Play Nearby's suites will add more copies.

**Approach:** Per the owner's Decision `e6pre-18` (epic-play-nearby Notes, 2026-10-10): one definition per helper in a support header, suites include it; two definitions merge only when one definition with defaults gives every call site the same bytes or result, otherwise both stay and this plan says why.

## Boundaries & Constraints

**Always:** `resume.bin` builders write the file's bytes by hand, never through `GameSaveStore`. Same `ctest -N` list before and after; every test asserts what it asserted. Helpers keep their meaning at every call site.

**Never:** Touch a production file. Touch D2's and D3's other duplication, `games_check/`, the companion folders, or any helper the D4 item does not name. Rename a name `ResumeMatchTest.cpp` or `GameMatchTest.cpp` uses (lane `e6pre-17` adds tests there).

</frozen-after-approval>

## Code Map

- `test/game_script/harness/SaveSupport.h` -- NEW, namespace `savefile`: `HASH_A`, `HASH_B`, `snapshotOf`, `resumeBytes`.
- `test/game_script/harness/MatchSupport.h` -- gains `match::holds`.
- `test/game_script/harness/LauncherSupport.h` -- gains `launcher::PKG` (the `.pkg` text), `manifestJson`, `addGame`, `dropMatch`.
- `test/game_script/CMakeLists.txt` -- `GameSaveStoreTest` gets `harness/` on its include path.
- Call sites: `GameSaveStoreTest.cpp` (`PKG`, `OTHER_PKG`, `resumeFile`), `ResumeMatchTest.cpp` (`HASH_A/B`, `snapshotOf`, `resumeBytes`, `holds`), `ModePickerTest.cpp` (`HASH`, `snapshotOf`, `resumeBytes`, `manifestJson`, `PKG`, `holds` lambda), `GamesLauncherTest.cpp` and `GameRemoveLauncherTest.cpp` (`PKG`, `manifestJson`, `addGame`, `dropMatch`), `GameMatchTest.cpp` (`holds`).
- Not touched: `InstallerSupport.h`, `GameRegistryTest.cpp`, `ModePickerTest.cpp::dropMatch`, both `tapCell`s.

## Tasks & Acceptance

**Execution:**
- [ ] `SaveSupport.h` -- one `resumeBytes(snapshot, ver = 7, hash = HASH_A, mode = 0, seats = 1, magic = "CHRS", fileVersion = 1, codecVersion = 1)`; keeps the names `ResumeMatchTest.cpp` uses.
- [ ] `GameSaveStoreTest.cpp` -- `PKG` -> `HASH_A`, `OTHER_PKG` -> `HASH_B`, `resumeFile` -> `resumeBytes`; local definitions go.
- [ ] `ResumeMatchTest.cpp`, `ModePickerTest.cpp` -- local hash, `snapshotOf`, `resumeBytes` go; ModePicker's six `resumeBytes(taps, ...)` calls take `snapshotOf(taps)` and `HASH_A` in the hash slot.
- [ ] `LauncherSupport.h` + the three launcher suites -- shared `PKG`, `manifestJson` (ModePicker's four calls gain `api` = 1), `addGame`, `dropMatch`; suites bring them in with `using launcher::...`.
- [ ] `MatchSupport.h` + `GameMatchTest.cpp`, `ResumeMatchTest.cpp`, `ModePickerTest.cpp` -- one `holds`.

**Acceptance Criteria:**
- Given the baseline and the result, when `ctest -N` is listed at both and diffed, then the lists are equal.
- Given the touched suites, when run with `--repeat until-fail:5`, then all pass; the full `ctest` passes.
- Given `git diff --stat`, then only files under `test/game_script/` and this plan changed.

## Implementation Notes

Implemented by one subagent from this plan. Beyond its wording: `[this] { dropMatch(); }` became `[] { dropMatch(); }` in the two launcher suites (no member to capture), `<algorithm>` joined `MatchSupport.h`, and ModePicker has seven `resumeBytes` calls, not six (the Tasks line is off by one; the diff changed all seven). Test doubles: none added or extended.

## Review Triage Log

All four lenses ran as context-free foreground subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: high 0, medium 0, low 2 (patched; the second from the orchestrator's review), false 0, rejected-low 9.

| Finding | Verdict | Evidence / action |
|---|---|---|
| Blind: `SaveSupport.h` cites docs/file-formats.md, which has no resume.bin section | low, patch | The layout is in docs/crosshatch/formats.md (`## resume.bin`); comment fixed. |
| Blind: mode bytes (`SOLO_BYTE`, `SAVED_SOLO`) not centralized | low, rejected | Not named by the Decision; out of scope. |
| Blind and edge: hash array size vs literal / `GamePkg::HASH_BYTES` (static_assert) | low, rejected | The sizes are asserted equal in `GameMatchActivity.cpp:74` and `GameModeActivity.cpp:29`; the old copies had the same shape. |
| Blind and edge: `PKG` text and `HASH_A` agree only by comment | low, rejected | Not new: `GameHashTest` pins "v1\n0530a15766e91bf1\n" to those bytes, and a drift fails the Continue tests. |
| Blind: `manifestJson` has three adjacent ints | low, rejected | Pre-existing signature of GamesLauncherTest's; the fix is a new type. |
| Blind: dedup partial (`installCounter`, `nSnapshotOf`, `installPkg`, `header()`) | low, rejected | None is named by the Decision. |
| Blind: third `Bytes` alias | low, rejected | Same type; `HarnessSupport.h` is not on GameSaveStoreTest's include path and pulls in gtest-side doubles. |
| Blind and edge: `magic` read as 4 bytes unchecked | low, rejected | Unchanged from the old `header()`; every call passes a literal of 4. |
| Blind: header includes all of `GameSaveStore.h` for a constant | low, rejected | The bytes are still hand-written; the include only supplies the size. |
| Edge: plan says six ModePicker calls, diff has seven | low, rejected | The fix edits this plan; recorded in Implementation Notes instead. |
| Orchestrator: `resumeBytes` built `out` with `Bytes out(magic, magic + 4)`, which adds a `-Wstringop-overflow=` warning ("writing 8 bytes into a region of size between 2 and 4") the old copies did not | low, patch (follow-up commit) | Real: the rebuild showed it from `SaveSupport.h`. `out` now starts from an initializer list of the six header bytes; bytes unchanged. |
| Verification-gap | none filed | Its other findings (ModePicker `dropMatch`, `.pkg` text in GameMatchTest, GameRegistryTest, GamePackageInstallerTest) are plan choices or unnamed helpers, with the reasons in Design Notes. |
| Intent-alignment | descriptive | Diff implements the literal reading (one definition each, reasons recorded for those that stay); `tapCell` and ModePicker's `dropMatch` stay, with the reason in Design Notes. |

## Design Notes

Merged: hash pair (`savefile::HASH_A/B`; bytes equal in all three suites); `resumeBytes` (every call gets the same bytes: ResumeMatch's passes snapshot, ver, hash and gets mode 0, seats 1; ModePicker's `(taps, ver, mode, seats)` becomes `(snapshotOf(taps), ver, HASH_A, mode, seats)`; SaveStore's already has these defaults); `snapshotOf` (identical); `manifestJson` of the three launcher suites (identical bytes; GamesLauncher's `extra` defaults to ""); `addGame` of GamesLauncher and GameRemoveLauncher (the latter is the former with `folder = ""` and `pkg = PKG`); the two static `holds` and ModePicker's lambda (identical bodies).

Both stay, with the reason:
- `InstallerSupport.h::manifestJson` (and `GameRegistryTest::addGame` over it): other bytes (`"version": "1"`, spaces, seats and modes passed as JSON text); installer tests hash and size these.
- `ModePickerTest::dropMatch`: also exits the entered match and clears the fixture's `entered`; the shared one would need that passed in at each call.
- `tapCell` (GameMatchTest, ResumeMatchTest): same four steps but over each fixture's own `tapCanvas`/`frame`/`showFrame`, which differ (ResumeMatch's waits for the first frame and clears `firstFramePending`); a shared one needs those as parameters, so every call site changes.
- `GameRegistryTest`'s `PKG` text: its suite includes only `InstallerSupport.h`.

`launcher::PKG` is needed because `addGame`'s default `pkg` lives with it; the three launcher suites' copies of the text go. Names follow `ResumeMatchTest.cpp` so lane `e6pre-17`'s new tests there still compile after the merge. Guards kept: `addGame`'s `if (!pkg.empty())` (a game without `.pkg` is not installed, tested), `dropMatch`'s `if (replacement)`. `git log -L` on `resumeBytes`, `dropMatch`, `holds` shows no targeted fix since their addition.

## Verification

**Commands:**
- `ctest -N` before and after, diffed -- expected: no difference
- host suites build and `ctest` -- expected: all pass; touched suites with `--repeat until-fail:5`
- `python3 scripts/check_upstream_touches.py`, `scripts/*_test.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new

**Results (host tests only; no firmware build is needed, the diff is under `test/game_script/`):**
- `ctest -N`: 1861 tests before (81cdfd3d) and after, `diff` of the two lists empty (`{scratch}/e6pre-18/ctest-N-before.txt`, `ctest-N-after.txt`).
- Full `ctest -j4`: 1861 of 1861 passed before and after.
- Touched suites (636 tests, regex over Match, Resume, Save, Launcher, Title, Pass, Hidden) with `--repeat until-fail:5`: all passed.
- `check_upstream_touches.py` PASS; every `scripts/*_test.py` passes; `./bin/clang-format-fix` twice, nothing new.
- After the orchestrator's follow-up (`resumeBytes` starts from an initializer list): `cmake --build build/test` rebuilt the three suites that include `SaveSupport.h`; `-Wstringop` warnings in that build output: 1, the `std::set<Bytes>` compare in `ResumeMatchTest.cpp` (`memcmp` bound exceeds maximum object size), none from `SaveSupport.h`. The baseline log `~/.cache/crosshatch/warm-hosttest.log` has 2 such lines, one per build pass in it, both from that same compare. `ctest -N` still 1861 and identical to the baseline list; full `ctest` 1861 of 1861; touched suites (636 tests) pass `--repeat until-fail:5`.

## Auto Run Result

Summary: the helpers the Decision names now have one definition each where one definition gives every call site the same result: `savefile::{HASH_A, HASH_B, snapshotOf, resumeBytes}` in the new `SaveSupport.h`, `match::holds`, and `launcher::{PKG, manifestJson, addGame, dropMatch}`. Stays split, with reasons in Design Notes: `InstallerSupport.h::manifestJson` and `GameRegistryTest`, ModePicker's `dropMatch`, both `tapCell`s.
Files: the ten under `test/game_script/` in `git diff --stat`, plus this plan. No production file touched.
Review: lenses_ran above; one low patch (a doc path in a comment).
Verification: as above. Formatting-only changes outside my paths: none.
Residual risks: the GCC build prints `-Wstringop` warnings pointing into the inline `resumeBytes`; not compared with the old copies, not errors. Lane `e6pre-17`'s new tests in `ResumeMatchTest.cpp` and `GameMatchTest.cpp` keep compiling only if they use the names that file used (`HASH_A`, `HASH_B`, `snapshotOf`, `resumeBytes`, `holds`).

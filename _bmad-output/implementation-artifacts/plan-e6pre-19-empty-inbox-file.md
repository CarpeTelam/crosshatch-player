---
title: 'e6pre-19: name an empty inbox file'
type: 'feature'
ticket: ''
created: '2026-10-10'
status: 'built'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick']
review_loop_iteration: 0
deferred: []
followup_review_recommended: false
baseline_revision: '8e5f4dd1decfa87c30e5fbad7006e9b68ee81fcb'
context: ['{project-root}/docs/crosshatch/orchestrated-epics.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The upload that fills the cards delivers 0-byte `.chgame` files, and the installer reports them as "not a readable zip" (the launcher: "Not a game package"), which sent the second device run looking for corruption (`deferred-work.md` `## e5-run2`).

**Approach:** As the owner's Decision `e6pre-19`, name an empty inbox file (epic-play-nearby Notes, 2026-10-10): a new `GamePackageInstaller::Error::Empty`, checked in `listMembers` before the zip is read, `describe` "the file is empty (0 bytes)", launcher note `STR_GAMES_INSTALL_EMPTY` "The file is empty", still renamed `<name>.bad`. A file of 1 byte or more that is not a zip stays `NotAPackage`, its log line keeps the prefix "<name> not installed: not a readable zip" and appends " (<n> bytes)". Every switch over the error and the tests' tables gain the value. Out of scope: why the upload delivered empty files, and when the inbox is scanned.

</frozen-after-approval>

## Implementation Notes

Oneshot: the change is about 25 production lines in one installer, one launcher switch, and one yaml key, the rest tests and docs, all mechanical.

Built: `Error::Empty` in `GamePackageInstaller.{h,cpp}` (`Job::fileBytes`, `listMembers`, `describe`, the `installAll` log line), `reasonText` and `STR_GAMES_INSTALL_EMPTY`, `formats.md`; tests in `GamePackageInstallerTest` (a non-zip log line with its size, an empty file ends `.bad` as `Empty`, a valid package beside it installs), `GamesLauncherTest`'s table and count (19 to 20), `PackageHardeningTest`'s description list. The crafted-package generator got no `empty-file` case: `CraftedPackageTest` asserts a non-empty file to catch a missing one, so an empty case fails it by design; `InstallerTest` covers the 0-byte file.

## Plan Change Log

## Review Triage Log

Quick lens run as one context-free foreground subagent (`general-purpose`), 2026-10-10. Verdicts: 0 high, 0 medium, 1 low, 1 false.

- low, patch: `PackageHardeningTest::errorsByName()` lacked `Empty` while the other two tables had it. Real as a gap against the Decision's "the test suites' tables"; fixed with a one-line row (it stays unused by `cases.txt`, with a comment saying why).
- false (not a defect, as the reviewer says): the launcher note was checked in two halves only. The simulator screenshot in Verification is the end-to-end check.

## Design Notes

- Settled by: epic-play-nearby.md Notes, Decision `e6pre-19` (error value, wording, log prefix and `(<n> bytes)`, tests, out of scope). It says nothing persists the enum's numeric values unless the plan finds otherwise: grep of `src`, `test`, `scripts`, `docs` finds no persisted or scripted use of the numbers; the launcher tests sweep every uint8_t value and the tables are by name.
- `Empty` is inserted after `NotAPackage` (same family: the file is not a package). Numbers shift for the later values; nothing depends on them.
- Guards in `listMembers` that stay: `openFileForRead` failure is `SdCard`; `fileBytes > PACKAGE_BYTES` is `PackageTooBig`, judged before the directory is read. `Empty` is checked right after the size is read and before the over-size check (0 is never over); neither guard moves. `file` is closed by the RAII `HalFile` on the early return; the existing `PackageTooBig` return does the same.
- The size for the log line is kept in `Job` (heap, 4 bytes), set by `listMembers`, because `installAll` logs after `install` returns. Only `NotAPackage` appends it; `Empty` already says 0 bytes in `describe`. The other reasons' lines are unchanged. `NotAPackage` is returned only from `listMembers` after the size is set.
- Test double: the fake card's `fileSize()` stands in for `HalFile::size()`; both give 0 for an empty file (the fake returns 0 for a missing entry as well, and the device's open would fail first, which `SdCard` covers).

## Verification

**Commands:**
- Host: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`, under the host-test lock -- expected: all pass.
- `scripts/*_test.py`, `scripts/check_layers.py`, `scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new in `git status` the second time.
- Under the build lock: `pio run -e x4pro`, `pio run -e default`, `check_flash_budget.py build on` then `objects`, `pio check` for `default` and `-e x4pro` -- expected: pass.
- `sim.sh build x4pro`.

**Manual checks:**
- Simulator: a 0-byte `empty.chgame` in the card's `/games/`, open Games, screenshot the note "empty.chgame: The file is empty".

**Evidence (run on this lane, 2026-10-10, after the review patch):**
- Host: fresh Ninja `build/test`, 1899 of 1899 tests pass (the 1900th, a crafted `empty-file` case, was dropped; see Implementation Notes). All 13 `scripts/*_test.py`, `check_layers.py` and `check_upstream_touches.py` pass; `./bin/clang-format-fix` run twice, the second changed nothing.
- Firmware, under the build lock: `pio run -e x4pro` and `-e default` succeed; `check_flash_budget.py build on` and `objects` exit 0; `pio check` passes for `default` and `-e x4pro` with no defects. Size delta: unmeasured (no base build taken at the baseline commit; the story adds about 20 bytes of strings and one branch).
- Simulator (`sim.sh build x4pro`, then a 0-byte `empty.chgame` in `fs_/games/`, Games opened): the log says `[GAME] empty.chgame not installed: the file is empty (0 bytes)`, the file became `empty.chgame.bad`, and the note shows the reason.

## Auto Run Result

**Summary:** A 0-byte inbox file now ends as `Error::Empty` (checked in `listMembers` before the zip is read), described "the file is empty (0 bytes)", shown by the launcher as "The file is empty" (`STR_GAMES_INSTALL_EMPTY`), and renamed `.bad` like any invalid package. A 1-byte-or-more non-zip stays `NotAPackage`, and its log line keeps "<name> not installed: not a readable zip" and appends " (<n> bytes)".

**Files:** `src/games/GamePackageInstaller.{h,cpp}`, `src/activities/games/GamesLauncherActivity.cpp`, `lib/I18n/translations/english.yaml` (a ledgered upstream file), `docs/crosshatch/formats.md`, `test/game_script/harness/{GamePackageInstallerTest,GamesLauncherTest,PackageHardeningTest}.cpp`, this plan, and the screenshot.

**Review:** quick lens, one context-free foreground subagent; 1 low (patched), 1 false. Nothing deferred (`deferred: []`).

**Residual risks:** the crafted-package generator has no empty case, by design. Formatting-only changes outside the story's paths: none.

**Screenshot:** `_bmad-output/initiative-crosshatch-player-v1/epic-play-nearby/story-empty-inbox-screenshots/empty-file-note.png` shows the Games screen with the note "empty.chgame: The file is empty".

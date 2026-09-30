---
title: 'Count installed games with the registry''s own per-folder test'
type: 'bugfix'
ticket: ''
created: '2026-09-30'
status: 'built'
route: 'oneshot'
route_source: 'auto'
baseline_revision: 'fa642c4ab22d2af55eba4aacef3350f2d93f27f9'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick']
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Deferred entry 114 (`_bmad-output/implementation-artifacts/deferred-work.md`): the installer's 65th-game check (`countInstalledGames` in `src/games/GamePackageInstaller.cpp`) counts every `/.games/<id>/` folder with a valid `.pkg`, but `GameRegistry::load` also skips a folder whose `manifest.json` is missing, invalid, or names another id. A card with such a folder refuses an install (`TooManyGames`) while fewer than 64 games are listed, and the skipped folder is not listed, so only a computer can clear it.

**Approach:** Give `GameRegistry` one public per-folder test (valid `.pkg` and a manifest that parses and names the folder's id), use it in `GameRegistry::load` and in `countInstalledGames`, so the installer counts exactly the folders the registry lists. The count reuses the `Job`'s `ManifestReader` and a heap scratch `Entry` in `Job`, so no stack growth.

</frozen-after-approval>

## Implementation Notes

Oneshot: about 70 lines in `src/games/GameRegistry.{h,cpp}`, `src/games/GamePackageInstaller.cpp`, and one host test in `test/game_script/harness/GamePackageInstallerTest.cpp` (`GameLimitTest`). All fork-only files, so no upstream-touch ledger row.

Cost accepted: the count, once per `installAll` call and only when a package adds a game, now also reads up to 64 manifests (each streamed through the 96 B chunk the registry already uses). `GameLimitTest.TheFoldersAreCountedOnceACallAndKeptAsInstallsLand` still pins one `.pkg` open per folder per call.

Red seen first: with `src/` at the baseline, `GameLimitTest.OnlyFoldersTheRegistryListsCount` failed (the 64th install refused, 63 listed). `git log -L` on `GameRegistry::load` (5c49754c) and `countInstalledGames` (b976e765, c147fb1c): the one ordering guard, `.pkg` before the manifest (the `.pkg` is written last, so it marks a whole game), is kept inside `readGame`.

Review fix: `wouldBeOverTheLimit`'s replacement test (`addsGame`) also uses `readGame`, so a package into a folder the registry skips counts as a new game (test `APackageIntoAFolderTheRegistrySkipsAddsAGame`). `docs/crosshatch/formats.md`'s install-limit paragraph and deferred entry 114 updated.

## Review Triage Log

Pass 1 (quick lens, context-free subagent): 1 high, 0 medium, 3 low, 0 false.
- high, patch: `addsGame` kept the `.pkg`-only test, so with 63 listed and a skipped folder `X`, `new` and `X` both installed and a 65th game was hidden. Now `readGame`; new test.
- low, patch: `formats.md` still said a skipped folder counts. Rewritten.
- low, patch: the trailing comment alignment in `Job`. `./bin/clang-format-fix` realigned it.
- low, patch: deferred entry 114 not marked resolved; `wouldBeOverTheLimit`'s "installed id" comment ambiguous. Both reworded.

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, including a new `GameLimitTest` case where 63 listed games plus one folder with a valid `.pkg` and a manifest naming another id still let a 64th game install, and the registry then lists 64.
- `pio run -e x4pro`, `-e sticky`, `-e default`, `-e x4c`, `-e papermono` -- expected: all build.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: no defects.
- `./bin/clang-format-fix` -- expected: no changes on a second run.

**Evidence (aedf9f7e, 2026-09-30, cloud container, 4 cores):**
- Host tests: 1363/1363 pass (`ctest -j`, 21.9 s); both new `GameLimitTest` cases included.
- `pio run` for `x4pro`, `default`, `sticky`, `x4c`, `papermono`: all SUCCESS.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`: PASSED (`default`, cppcheck).
- `sim.sh build x4pro`: SUCCESS.
- `scripts/check_flash_budget.py` build on/off, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`: within budget (static RAM +784 B of 1,024 B, games on minus off), no object problems.
- `check_upstream_touches.py` PASS, `check_layers.py` passed, `check_api_freeze.py --base-ref origin/develop` passed, every `scripts/*_test.py` passed.
- `./bin/clang-format-fix` twice: no changes on the second run.


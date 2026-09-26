<!-- bmad:context -->
<!-- Verified 2026-09-26 against a376afc. Managed by bmad-project-context; edits inside this block are replaced on refresh. Keep anything you want preserved outside the markers. -->

## crosshatch-player

Fork of CrossPoint Reader (`crosspoint-reader/crosspoint-reader`): e-reader firmware focused on ESP32-S3 touchscreen devices, the Xteink X4 Pro (`x4pro` env) and Seeed reTerminal Sticky (`sticky` env). C++20 on Arduino-ESP32 via pioarduino PlatformIO, built with `-fno-exceptions`; the hardware SDK is the `freeink-sdk` submodule. Contributor docs live in `docs/contributing/`; BMAD planning output goes to `_bmad-output/`.

## Policy

- Keep the diff against upstream `develop` minimal so upstream merges stay clean: put fork-only code in new files or behind `FREEINK_DEVICE_*` / `FREEINK_CAP_*` guards; never reformat, rename, or reorganize upstream code you are not otherwise changing.
- Never move the `freeink-sdk` submodule pointer or edit `.skills/` for fork-only work; propose those changes upstream.
- Shared code must still build and run on the ESP32-C3 (~380 KB RAM, no PSRAM); apply the memory and stack rules below in S3-only code too.
- Push feature branches to `origin` (this fork) and open PRs into its `develop`.
- Sync upstream by merging, with `upstream` = `https://github.com/crosspoint-reader/crosspoint-reader.git`: run `git config merge.ours.driver true` once per clone, then merge `upstream/develop` into `develop`. `AGENTS.md` is this fork's own; `.gitattributes` keeps our copy. Without the driver, resolve with `git checkout --ours AGENTS.md`; resolve an upstream `CLAUDE.md` change with `git rm CLAUDE.md`.
- Never hand-edit generated files: `lib/I18n/I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp` (from `lib/I18n/translations/*.yaml`) and `*.generated.h` (from `src/**/*.html` and `.js`); every `pio run` regenerates them.

## Where things are

- New or changed screen: read `docs/contributing/touch-and-ui.md` first; build on `UiListActivity`, `UiTabListActivity`, or `UiAppHost`, never `rowTouch` / `wasTapInRect`.
- Activity lifecycle and navigation: `docs/activity-manager.md`.
- Cache file formats and version history: `docs/file-formats.md`.
- Before allocating memory, touching storage/input/display/i18n, writing branching or state logic, adding a feature/setting/dependency, or refactoring, read the matching `.skills/<name>/SKILL.md` (`heap-discipline`, `hal-and-abstractions`, `control-flow-clarity`, `scope-discipline`, `refactor-for-review`).

## Running and verifying

- Run `git submodule update --init --recursive` before any firmware build; `freeink-sdk/` is empty in fresh and cloud clones, and every SDK lib dep symlinks into it.
- See a UI change running without hardware: the desktop simulator skill `.claude/skills/run-crosshatch-player/` (`sim.sh setup`, `build x4pro`, `start`, `tap`, `ss`) builds the firmware natively and screenshots it headless.
- Iterate with `pio run -e x4pro` and `pio run -e sticky`; bare `pio run` builds only the C3 `default` env. Before a PR also build `default`, `x4c`, and `papermono`: CI builds all five, and a fix for one board has broken another's build (049c2b5, 4598fa2).
- Use pioarduino PlatformIO Core 6.1.19, not `pip install platformio`, and pin `pioarduino==6.1.19` inside `~/.platformio/penv`; without it the custom-sdkconfig envs fail with "No module named 'SCons.Tool.FortranCommon'" (see `.github/workflows/ci.yml`).
- Unit tests are host GoogleTest, not `pio test`: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`. Delete `build/test` before switching to `pio run -t unit-tests`, which uses a different CMake generator.
- Static analysis matching CI: `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`.
- Format with `./bin/clang-format-fix` and no arguments before committing, matching CI's whole-tree check; `-g` skips staged and new files. It exits 1 below clang-format 21; never run `clang-format` directly, since the wrapper excludes generated and vendored sources.
- CI runs only on pull requests here (the `ci.yml` push trigger is `master`, which this fork does not use); open a PR to get a CI result.
- Never run `git clean -fdX`; it deletes the gitignored `platformio.local.ini`. For a stale-scaffold "multiple definition of 'app_main'" error, use the `rm -rf` in `platformio.ini`.

## Conventions that differ from defaults

- All SD card access goes through `Storage` / `HalFile` (`lib/hal/HalStorage.h`); never use SdFat, `FsFile`, or `SDCardManager` directly. SdFat is not thread-safe, and unserialized access trips a FreeRTOS assert and panics.
- Allocate with `makeUniqueNoThrow` (`lib/Memory/Memory.h`) or `new (std::nothrow)` and null-check; never bare `new` or `std::make_unique` for fallible allocations, because with `-fno-exceptions` a failed `new` calls `abort()`.
- Keep function locals under 256 bytes; put larger buffers on the heap.
- User-facing text goes through `tr(STR_…)`; add new keys to `lib/I18n/translations/english.yaml` only. Other languages fall back to English, and the build fails on a key used in code but missing there.
- Log with `LOG_ERR` / `LOG_INF` / `LOG_DBG` (`lib/Logging/Logging.h`), never `Serial.print*`.
- Read buttons through `MappedInputManager::Button`, never raw `HalGPIO::BTN_*`; users remap the front buttons.
- When a change alters section layout output, line breaking included, bump `SECTION_FILE_VERSION` in `lib/Epub/Epub/Section.cpp` and record it in `docs/file-formats.md`; otherwise devices keep serving stale cached pages.

## Known pitfalls

- Never take `RenderLock` in an activity destructor; `ActivityManager` already holds the non-recursive render mutex while destroying activities, so it deadlocks (12cc816).
- Refactors of shared helpers have silently dropped earlier targeted fixes: NFC filename normalization (#3600, fixed in #3630) and Hangul line breaking (#2288, fixed in #3700). Before rewriting a function, read its history with `git log -L`; cloud clones are shallow, so run `git fetch --unshallow` first.

<!-- /bmad:context -->

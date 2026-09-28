<!-- bmad:context -->
<!-- Verified 2026-09-26 against a376afc. Managed by bmad-project-context; edits inside this block are replaced on refresh. Keep anything you want preserved outside the markers. -->

## crosshatch-player

Fork of CrossPoint Reader (`crosspoint-reader/crosspoint-reader`): e-reader firmware focused on ESP32-S3 touchscreen devices, the Xteink X4 Pro (`x4pro` env) and Seeed reTerminal Sticky (`sticky` env). C++20 on Arduino-ESP32 via pioarduino PlatformIO, built with `-fno-exceptions`; the hardware SDK is the `freeink-sdk` submodule. Contributor docs live in `docs/contributing/`; BMAD planning output goes to `_bmad-output/`.

## Policy

- Keep the diff against upstream `develop` minimal so upstream merges stay clean: put fork-only code in new files or behind `FREEINK_DEVICE_*` / `FREEINK_CAP_*` guards; never reformat, rename, or reorganize upstream code you are not otherwise changing.
- Change a file that upstream also has only if `docs/crosshatch/upstream-touches.md` lists it; the `Upstream touch ledger` PR job enforces this, and `python3 scripts/check_upstream_touches.py` runs the same check locally; it needs an `upstream` remote (URL below) with `develop` fetched and an unshallowed clone (`git fetch --unshallow`).
- Never move the `freeink-sdk` submodule pointer or edit `.skills/` for fork-only work; propose those changes upstream.
- Shared code must still build and run on the ESP32-C3 (~380 KB RAM, no PSRAM); apply the memory and stack rules below in S3-only code too.
- Push feature branches to `origin` (this fork) and open PRs into its `develop`. Title every PR as a Conventional Commit (`type: subject`, types `feat` `fix` `docs` `style` `refactor` `perf` `test` `build` `ci` `chore` `revert`); the `Title Check` job (`amannn/action-semantic-pull-request`) fails any other title.
- Ship each epic as one PR: check a ticket's verify locally in its build session, check CI on the epic PR, and never merge an epic PR with a red check.
- Sync upstream by merging, with `upstream` = `https://github.com/crosspoint-reader/crosspoint-reader.git`: run `git config merge.ours.driver true` once per clone, then merge `upstream/develop` into `develop`. `AGENTS.md` is this fork's own; `.gitattributes` keeps our copy. Without the driver, resolve with `git checkout --ours AGENTS.md`; resolve an upstream `CLAUDE.md` change with `git rm CLAUDE.md`.
- Never hand-edit generated files: `lib/I18n/I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp` (from `lib/I18n/translations/*.yaml`) and `*.generated.h` (from `src/**/*.html` and `.js`); every `pio run` regenerates them.

## Where things are

- New or changed screen: read `docs/contributing/touch-and-ui.md` first; build on `UiListActivity`, `UiTabListActivity`, or `UiAppHost`, never `rowTouch` / `wasTapInRect`.
- Activity lifecycle and navigation: `docs/activity-manager.md`.
- Cache file formats and version history: `docs/file-formats.md`.
- Before allocating memory, touching storage/input/display/i18n, writing branching or state logic, adding a feature/setting/dependency, or refactoring, read the matching `.skills/<name>/SKILL.md` (`heap-discipline`, `hal-and-abstractions`, `control-flow-clarity`, `scope-discipline`, `refactor-for-review`).
- Game fixtures (test games and fault scripts) live in `test/game_script/fixtures/`, never `games/`, which the release packs.
- Orchestrated epics (one agent running parallel build agents): `docs/crosshatch/orchestrated-epics.md`; a build agent follows its Build-agent brief.

## Running and verifying

- Run `git submodule update --init --recursive` before any firmware build; `freeink-sdk/` is empty in fresh and cloud clones and in every new git worktree, and every SDK lib dep symlinks into it.
- See a UI change running without hardware: the desktop simulator skill `.claude/skills/run-crosshatch-player/` (`sim.sh setup`, `build x4pro`, `start`, `tap`, `ss`) builds the firmware natively and screenshots it headless.
- Iterate with `pio run -e x4pro` and `pio run -e sticky`; bare `pio run` builds only the C3 `default` env. Before a PR also build `default`, `x4c`, and `papermono`: CI builds all five, and a fix for one board has broken another's build (049c2b5, 4598fa2).
- Never run two builds at once against one checkout or one `~/.platformio`. When several agents share a machine, wrap every `pio run`, `pio check`, `pio project metadata`, `sim.sh setup`/`build`, and host-test CMake configure and build in one shared `flock <lock-file> <command>`.
- Use pioarduino PlatformIO Core 6.1.19 from PyPI (`uv tool install pioarduino==6.1.19`; the agent proxy returns 403 for GitHub archive URLs), not `pip install platformio`, and pin `pioarduino==6.1.19` inside `~/.platformio/penv`; without it the custom-sdkconfig envs fail with "No module named 'SCons.Tool.FortranCommon'" (see `.github/workflows/ci.yml`).
- Behind the agent proxy, `pio` package downloads fail TLS with "UnknownIssuer" because the espressif32 platform points `SSL_CERT_FILE` / `REQUESTS_CA_BUNDLE` at the penv's own certifi bundle; append the proxy CA (`/root/.ccr/ca-bundle.crt` in cloud sessions) to the `cacert.pem` that `~/.platformio/penv/bin/python -c "import certifi; print(certifi.where())"` prints (`~/.platformio/penv/lib/python3.11/site-packages/certifi/cacert.pem` here), and also to the certifi bundle of the environment that runs `pio` itself (`~/.local/share/uv/tools/pioarduino/lib/python3.11/site-packages/certifi/cacert.pem` for a `uv tool` install).
- Unit tests are host GoogleTest, not `pio test`: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`. Delete `build/test` before switching to `pio run -t unit-tests`, which uses a different CMake generator.
- Static analysis matching CI: `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`.
- Format with `./bin/clang-format-fix` and no arguments as the last step before committing, after every edit, matching CI's whole-tree check; then run it again and confirm `git status` shows nothing new. Keep any formatting-only change it makes outside your paths, never revert it, and name it (4779ab69, aebea6f4, 6165a741). `-g` skips staged and new files. It exits 1 below clang-format 21; never run `clang-format` directly, since the wrapper excludes generated and vendored sources.
- CI runs only on pull requests here (the `ci.yml` push trigger is `master`, which this fork does not use); open a PR to get a CI result.
- Add a fork-only CI check as a job in `.github/workflows/crosshatch-ci.yml` and list it in the `Crosshatch Test Status` job's `needs`; never edit upstream's `ci.yml`. Branch protection requires only `Test Status` and `Crosshatch Test Status`.
- A ticket that adds or changes a CI-only gate or workflow runs it once from a fresh clone, not an incremental tree, before it counts as built, and its plan's Verification says so; the flash budget gate passed incrementally and failed on CI's fresh tree (f3ba9e54). When `git clone` is unavailable, `git archive <commit> | tar -x` plus each submodule's archive counts, for a gate that reads no git history.
- Delete a finished worktree or scratch clone once its work is merged: each takes 1.5–1.8 GB, and a full disk half-installs shared `~/.platformio/packages`.
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
- Never run `sim.sh setup` or a simulator build while a firmware `pio run` is in progress; the changed `platformio.local.ini` changes PlatformIO's project checksum, and it wipes `.pio/build` mid-build. `pio project metadata` on a fresh tree likewise empties the env's build dir, which failed the flash budget job on PR #9 (f3ba9e54).

<!-- /bmad:context -->

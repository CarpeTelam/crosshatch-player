# Upstream touches

crosshatch-player merges CrossPoint Reader's `develop` branch regularly, so every change to a file that upstream
also has is a future merge conflict. This file is the cap on those changes (AD-3 in the architecture spine): a
change to a file that exists in `upstream/develop` is allowed only if the file is a row of the **Ledger** or an
entry of the **Allowlist** below. Adding a row needs an architecture spine update first.

The `Upstream touch ledger` CI job (`.github/workflows/crosshatch-ci.yml`) runs
`scripts/check_upstream_touches.py` on every pull request to `develop`. The script reads the three lists below
from this file, so keep the section headings as they are and keep each path in backticks as the first code span
of its row or bullet. It fails a pull request when:

- a path that exists in `upstream/develop` differs between `merge-base(HEAD, upstream/develop)` and `HEAD`
  (compared with `--no-renames`) and is in neither the Ledger nor the Allowlist;
- the `freeink-sdk` submodule pointer differs from the merge-base at all (propose SDK changes upstream instead);
- `upstream/develop` has any file under a **Game path**, or a trial merge of it into `HEAD` conflicts in one.
  Conflicts elsewhere, for example in a ledgered file, are printed but do not fail the job; they are resolved when
  upstream is merged.

The job enforces paths only. The Change and Guarded columns describe what each row may change, and review holds a
pull request to them, with one part checked by the `Layer check` job: every include of game code in an upstream
file sits in the `FREEINK_CAP_GAMES` branch of an `#if`, `#ifdef`, `#elif`, or `#elifdef` (not in its `#else`), whose
condition is `FREEINK_CAP_GAMES`, `defined(FREEINK_CAP_GAMES)`, or `FREEINK_CAP_GAMES == 1`, alone or joined to others by
`&&` with no `||`; `scripts/check_layers.py`'s docstring has the full rule.

## Ledger

Changes are `#if FREEINK_CAP_GAMES`-guarded where the language allows; the Guarded column marks the ones that
cannot be.

| # | Upstream file | Change | Guarded |
| --- | --- | --- | --- |
| 1 | `platformio.ini` | `FREEINK_CAP_GAMES=1` in the six x4pro/sticky envs; `--suppress=*:*/lib/lua/*` in the shared `check_flags` | env-scoped + one shared line |
| 2 | `lib/I18n/translations/english.yaml` | `STR_GAMES_*` keys added, changed, or removed (the prefix only; no key outside it changes) | prefix-scoped |
| 3 | `test/CMakeLists.txt` | `add_subdirectory(game_core)`, `add_subdirectory(game_script)` | no |
| 4 | `src/activities/ActivityManager.h` | `HomeMenuItem::Games`, `goToGames()` | yes |
| 5 | `src/activities/ActivityManager.cpp` | `goHome` mapping (one call to `GamesHomeRow::isGamesActivity`, the pure header listing the five names, `GamesLauncher`, `GameMode`, `GameOptions`, `GameLobby`, and `GameMatch`, which select Home's Games row), `goToGames()` opens `GamesLauncherActivity`; its includes (`GamesLauncherActivity.h`, `GamesHomeRow.h`) | yes |
| 6 | `src/activities/home/HomeActivity.h` | Games in the index mapping after File Transfer and before Settings, one mapping for list and cover-grid Home; `onGamesOpen()`; no game header | yes |
| 7 | `src/activities/home/HomeActivity.cpp` | Games in the item count in both Home modes, switch case, list-mode label; list mode reuses an existing `UIIcon`; no game header | yes |
| 8 | `src/components/CoverGridHomeUi.h` | tab array size, one more for the Games tab; no game header | yes |
| 9 | `src/components/CoverGridHomeUi.cpp` | Games tab before Settings, drawn from `GameIcons::GAME_CONTROLLER_32` with `renderer.drawIcon`; its `GameIcons.generated.h` include is the only upstream include of `lib/GameIcons` (`UPSTREAM_EDGES` in `scripts/check_layers.py`) | yes |
| 10 | `src/network/OtaUpdater.cpp` | calls into `ForkRelease.h` for the update URL, asset name, and build-number comparison, and into `games/ForkReleaseProbe.h` after a failed fetch; a `static_assert` that `assetName` is `ForkRelease::ASSET_NAME_CAPACITY` bytes (AD-25) | yes |
| 11 | `src/activities/boot_sleep/BootActivity.cpp` | the boot screen draws `CrosshatchMark120` and `STR_GAMES_PRODUCT_NAME` ("Crosshatch") in place of `Logo120` and `STR_CROSSPOINT`, same layout; one include and one `#if`/`#else` hunk; no game header (the bitmap is `src/images/CrosshatchMark120.h`) | yes |
| 12 | `src/activities/boot_sleep/SleepActivity.cpp` | `renderDefaultSleepScreen` draws the same mark and title, same layout and inversion; one include and one `#if`/`#else` hunk; no game header | yes |
| 13 | `lib/Logging/Logging.cpp` | the diagnostics log's hook: `#include <DiagLog.h>` and one `DiagLog::append(buf)` call after `addToLogRingBuffer(buf)` in `logPrintf`, each inside `#if CROSSHATCH_SD_LOG`; no game header, and with the define off (the default; no env sets it) neither is in the build and the firmware image is unchanged (the app descriptor's ELF digest and build stamp, and the image digest after it, differ; every loaded byte is the same); the library is `lib/DiagLog` | yes, by `CROSSHATCH_SD_LOG` |
| 14 | `src/main.cpp` | the diagnostics log's flush before sleep (owner Decision 2026-10-13, entry 19's flush before sleep, option a): `#include <DiagLog.h>` and one `DiagLog::flushBeforeSleep()` call before each of the three `Storage.prepareForDeepSleep()` calls (`enterDeepSleep` and the two early-sleep paths in `setup()`), each inside `#if CROSSHATCH_SD_LOG`; the call waits at most `DiagLog::SLEEP_FLUSH_TIMEOUT_MS` (250 ms) for the writer to put the buffered lines on the card, ignores the result, and so cannot hold sleep longer than that; and the `[MEM]` heap lines in `loop()` (entry 21): inside `#if CROSSHATCH_SD_LOG` their `if` tests only the 10 s period, so a run with no USB host still logs them to the card, and inside that `if`, in the same branch, one `[MEM]` battery line before them (cross-story review row 8): `Battery: <mV> mV, <percent>%`, the millivolts from a function-local `static const BatteryMonitor`'s `readMillivolts()` (0 when the read fails) and the percent from `powerManager.getBatteryPercentage()` (the status bar's), both read, and the line logged, only when a `RenderLock` taken with `RenderLock::Mode::Try` is owned (the themes read the same gauge, and the loop never waits out a push; a period whose try fails logs no battery line, the heap lines still go); it allocates nothing; the `#else` branch is upstream's comment and `Serial` / ROM-console condition unchanged; no game header, and with the define off neither the include, the calls, nor the `#if CROSSHATCH_SD_LOG` branch of the `[MEM]` `if` are in the build and the loaded firmware image is unchanged | yes, by `CROSSHATCH_SD_LOG` |

Rows 13 and 14 are guarded by the fork-only define `CROSSHATCH_SD_LOG`, not `FREEINK_CAP_GAMES`: the diagnostics log is independent of the games and builds on any board, the C3 included. Row 14 exists because the sleep paths unmount the card (`Storage.prepareForDeepSleep()`): a barrier before them is a call site in upstream code, which the hook alone (row 13) cannot be. It also holds the `[MEM]` condition, since upstream makes those lines only for a USB host or the ROM console, and the hook can write only lines that are made; and, in the same define-on branch, the battery line a cardless run records a match's battery cost from (millivolts and the status bar's percent, read at most once per 10 s, only when the render lock is free to try), which needs the same period and is made nowhere else.

Rows 11 and 12 (the brand swap, `STR_GAMES_PRODUCT_NAME` in row 2's prefix) make no game include: the mark is
`src/images/CrosshatchMark120.h`, generated by `scripts/gen_crosshatch_mark.py` with the games' 64 and 128 px bitmaps.

The game includes that rows 5, 9, and 10 make are held in `UPSTREAM_EDGES` in `scripts/check_layers.py`, and the Layer
check fails any other upstream include of game code, and any of these outside an `#if FREEINK_CAP_GAMES` branch.

Row 10 and fork releases (AD-25): an upstream merge that touches `src/network/OtaUpdater.*`,
`src/network/HttpDownloader.*`, `lib/JsonParser/ReleaseJsonParser.*`, `src/network/FirmwareBoardTag.*`, a release
workflow, or a `*-gh_release` env in `platformio.ini` is not done until a dry run of the fork release workflow
(`.github/workflows/crosshatch-release.yml`, "dry run" on, started from the Actions tab with the merge branch picked
under "Use workflow from") passes. The dry run builds the release envs and checks that each image still reports its
tag, holds the fork release URL and not upstream's, and carries its own board tag, so a clean merge that reroutes or
strands fork devices fails there instead of on a device. Upstream's `release.yml` and `release_candidate.yml` stay
disabled in the Actions tab and are never edited.

Row 10's release probe (AD-25). After a failed release fetch, `OtaUpdater.cpp` calls
`ForkReleaseProbe::latestReleaseMissing()` in `src/games/ForkReleaseProbe.cpp`, which requests
`ForkRelease::LATEST_RELEASE_URL` once more with its own `freeink::SecureHttpClient` and reads a 404 as "no release
yet". Read from `freeink-sdk` at `32a265fa86498c014e13a5bc16133bb021cb620c`
(`libs/network/SecureNet/include/SecureHttpClient.h`) and the fork sources, its four values are these. Timeout:
15,000 ms each for the TCP connect, the TLS handshake, and every wait on the response, the SDK default (`_timeoutMs`);
`HttpDownloader`'s release fetch sets 60,000 ms (`HTTP_TIMEOUT_MS`). Redirect limit: 0, the SDK default
(`_followRedirects`), so a 3xx comes back as the status and the probe answers false; `HttpDownloader` follows up to
5 hops (`kMaxRedirects` in the SDK's `ResumableFetch.h`, which its release fetch now runs through). TLS mode: HTTPS
through the SDK's wolfSSL client with peer verification off (`setInsecure()`), which needs `FREEINK_NET_WOLFSSL=1`
from `[base]` `build_flags` (without it the connect fails and the probe answers false); the same mode as
`HttpDownloader`'s wolfSSL path. User agent: `CrossPoint-ESP32-` followed by
`CROSSPOINT_VERSION` (`setUserAgent`), the same as `HttpDownloader`'s. The probe also opens a fresh connection
(`setReuse(false)`). These are written as prose and a numbered list on purpose: the check reads every table row and
bullet under this heading as a ledger path.

Probe re-check list. A merge that changes any of these re-reads the four values above against the probe, and
`HttpDownloader`'s handling of non-200 statuses, in the same merge, and updates the paragraph; the fork release dry
run alone does not cover them:

1. `src/network/HttpDownloader.*`: the release fetch's own values, and its reporting of every non-2xx final status as
   a bare failure, which is why the probe exists.
2. `[base]` `build_flags` in `platformio.ini`: `FREEINK_NET_WOLFSSL` and the wolfSSL defines choose the TLS stack of
   both.
3. The `freeink-sdk` submodule pointer: the timeout and redirect limit are `SecureHttpClient` defaults.

No reserve row remains.

## Allowlist

Upstream files the fork had already changed before the ledger existed.

- `AGENTS.md` -- the fork's own agent instructions; `.gitattributes` keeps our copy on merge.
- `.gitattributes` -- added by the fork for the `merge=ours` rule on `AGENTS.md`, the `merge=union` rule on `deferred-work.md`, and the `-text` rule on `assets/game-icons/**` (no line-ending conversion in `assets/game-icons`, so no checkout, `core.autocrlf=true` included, can change the bytes `SHA256SUMS` pins: `names.txt`, `SHA256SUMS`, `LICENSE`, and any SVG with line breaks); listed in case upstream adds one.
- `.gitignore` -- fork-local ignores.
- `.github/PULL_REQUEST_TEMPLATE.md` -- the fork's PR title rules.
- `CLAUDE.md` -- removed by the fork; an upstream change to it is resolved with `git rm CLAUDE.md`.

## Game paths

Fork-only paths. None of them may exist in upstream, so a merge of upstream never touches them. A path matches when
it, or one of its leading directories, matches an entry as a shell-style glob (Python `fnmatch`: `*` also matches
`/`, and `?` and `[...]` work too).

- `lib/Game*`
- `lib/DiagLog` -- the on-device diagnostics log (epic-play-nearby entry 19); it has no game code, and rows 13 and 14 are its upstream touches.
- `src/diagnostics` -- the log's file store over `Storage` (a project source, because `HalStorage.h` needs SdFat's include path, which a library in the `hal` -> `Logging` -> `DiagLog` cycle does not get).
- `lib/lua`
- `src/games`
- `src/activities/games`
- `games`
- `assets/game-icons`
- `docs/crosshatch`
- `test/game_core`
- `test/game_script`
- `test/diag_log` -- the diagnostics log's host suite, wired from `test/game_core/CMakeLists.txt` so row 3 stays as it is.
- `scripts/pack_game.py`
- `scripts/pack_game_test.py`
- `scripts/pack_device_run.py`
- `scripts/pack_device_run_test.py`
- `scripts/game_codec.py`
- `scripts/game_codec_test.py`
- `scripts/gen_game_icons.py`
- `scripts/gen_game_icons_test.py`
- `scripts/check_upstream_touches.py`
- `scripts/check_upstream_touches_test.py`
- `scripts/check_flash_budget.py`
- `scripts/check_flash_budget_test.py`
- `scripts/device_log.py` -- captures two devices' serial logs into one merged log (epic-play-nearby R13).
- `scripts/device_log_test.py`
- `scripts/fork_release.py`
- `scripts/fork_release_test.py`
- `scripts/fork_common.py` -- shared by the fork scripts; `docs/crosshatch/fork-scripts.md` has the conventions.
- `scripts/fork_common_test.py`
- `.github/workflows/crosshatch-*.yml` -- every fork-only workflow is named with this prefix.
- `scripts/check_api_freeze.py` -- the API freeze job's check (spine AD-19).
- `scripts/check_api_freeze_test.py`
- `scripts/check_layers.py` -- the layer check job's check of the spine's layer table (retro AI-5).
- `scripts/check_layers_test.py`
- `scripts/sim_sh_test.py` -- tests the simulator skill's `sim.sh` (epic-icon-library retrospective, AI-11).
- `scripts/dev_setup.py` -- the idempotent container setup the cloud SessionStart hook runs.
- `scripts/dev_setup_test.py`
- `.claude/hooks` -- the SessionStart hook that runs `scripts/dev_setup.py` in cloud sessions.

## Running the check locally

The check needs full history and git 2.38 or later:

```sh
git fetch --unshallow                 # only if `git rev-parse --is-shallow-repository` prints true
git remote add upstream https://github.com/crosspoint-reader/crosspoint-reader.git   # once per clone
git config merge.ours.driver true     # once per clone; the trial merge then keeps our AGENTS.md, as a real merge does
git fetch upstream develop
python3 scripts/check_upstream_touches.py                   # checks HEAD
python3 scripts/check_upstream_touches.py --ref my-branch   # or any other ref
python3 scripts/check_upstream_touches_test.py              # the script's own tests; need only git
```

Exit status 0 means the ref passes, 1 means it breaks a rule above, and 2 means the check could not run.

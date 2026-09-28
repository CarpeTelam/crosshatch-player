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
pull request to them.

## Ledger

Changes are `#if FREEINK_CAP_GAMES`-guarded where the language allows; the Guarded column marks the ones that
cannot be.

| # | Upstream file | Change | Guarded |
| --- | --- | --- | --- |
| 1 | `platformio.ini` | `FREEINK_CAP_GAMES=1` in the six x4pro/sticky envs; `--suppress=*:*/lib/lua/*` in the shared `check_flags` | env-scoped + one shared line |
| 2 | `lib/I18n/translations/english.yaml` | `STR_GAMES_*` keys appended | append-only |
| 3 | `test/CMakeLists.txt` | `add_subdirectory(game_core)`, `add_subdirectory(game_script)` | no |
| 4 | `src/activities/ActivityManager.h` | `HomeMenuItem::Games`, `goToGames()` | yes |
| 5 | `src/activities/ActivityManager.cpp` | `goHome` mapping, `goToGames()` | yes |
| 6 | `src/activities/home/HomeActivity.h` | Games in the index mapping after File Transfer and before Settings, one mapping for list and cover-grid Home; `onGamesOpen()`; no game header | yes |
| 7 | `src/activities/home/HomeActivity.cpp` | Games in the item count in both Home modes, switch case, list-mode label; list mode reuses an existing `UIIcon`; no game header | yes |
| 8 | `src/components/CoverGridHomeUi.h` | tab array size, one more for the Games tab; no game header | yes |
| 9 | `src/components/CoverGridHomeUi.cpp` | Games tab before Settings, drawn from `GameIcons::GAME_CONTROLLER_32` with `renderer.drawIcon`; its `GameIcons.generated.h` include is the only upstream include of `lib/GameIcons` (`UPSTREAM_EDGES` in `scripts/check_layers.py`) | yes |
| 10 | `src/network/OtaUpdater.cpp` | calls into `ForkRelease.h` for the update URL, asset name, and build-number comparison, and into `games/ForkReleaseProbe.h` after a failed fetch; a `static_assert` that `assetName` is `ForkRelease::ASSET_NAME_CAPACITY` bytes (AD-25) | yes |

The game includes that rows 5, 9, and 10 make are held in `UPSTREAM_EDGES` in `scripts/check_layers.py`, and the Layer
check fails any other upstream include of game code.

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
yet". Read from `freeink-sdk` at `111fdcc7f0176c3ee38391a160ee296bf492dbd8`
(`libs/network/SecureNet/include/SecureHttpClient.h`) and the fork sources, its four values are these. Timeout:
15,000 ms each for the TCP connect, the TLS handshake, and every wait on the response, the SDK default (`_timeoutMs`);
`HttpDownloader`'s release fetch sets 60,000 ms (`HTTP_TIMEOUT_MS`). Redirect limit: 0, the SDK default
(`_followRedirects`), so a 3xx comes back as the status and the probe answers false; `HttpDownloader` follows up to
5 hops (`MAX_REDIRECTS`). TLS mode: HTTPS through the SDK's wolfSSL client with peer verification off (`setInsecure()`),
which needs `FREEINK_NET_WOLFSSL=1` from `[base]` `build_flags` (without it the connect fails and the probe answers
false); the same mode as `HttpDownloader`'s wolfSSL path. User agent: `CrossPoint-ESP32-` followed by
`CROSSPOINT_VERSION` (`setUserAgent`), the same as `HttpDownloader`'s. The probe also opens a fresh connection
(`setReuse(false)`). These are written as prose and a numbered list on purpose: the check reads every table row and
bullet under this heading as a ledger path.

Probe re-check list. A merge that changes any of these re-reads the four values above against the probe, and
`HttpDownloader`'s handling of non-200 statuses, in the same merge, and updates the paragraph; the fork release dry
run alone does not cover them:

1. `src/network/HttpDownloader.*`: the release fetch's own values, and its reporting of every non-200 final status as
   a bare failure, which is why the probe exists.
2. `[base]` `build_flags` in `platformio.ini`: `FREEINK_NET_WOLFSSL` and the wolfSSL defines choose the TLS stack of
   both.
3. The `freeink-sdk` submodule pointer: the timeout and redirect limit are `SecureHttpClient` defaults.

No reserve row remains.

## Allowlist

Upstream files the fork had already changed before the ledger existed.

- `AGENTS.md` -- the fork's own agent instructions; `.gitattributes` keeps our copy on merge.
- `.gitattributes` -- added by the fork for the `merge=ours` rule on `AGENTS.md` and the `merge=union` rule on `deferred-work.md`; listed in case upstream adds one.
- `.gitignore` -- fork-local ignores.
- `.github/PULL_REQUEST_TEMPLATE.md` -- the fork's PR title rules.
- `CLAUDE.md` -- removed by the fork; an upstream change to it is resolved with `git rm CLAUDE.md`.

## Game paths

Fork-only paths. None of them may exist in upstream, so a merge of upstream never touches them. A path matches when
it, or one of its leading directories, matches an entry as a shell-style glob (Python `fnmatch`: `*` also matches
`/`, and `?` and `[...]` work too).

- `lib/Game*`
- `lib/lua`
- `src/games`
- `src/activities/games`
- `games`
- `assets/game-icons`
- `docs/crosshatch`
- `test/game_core`
- `test/game_script`
- `scripts/pack_game.py`
- `scripts/game_codec.py`
- `scripts/game_codec_test.py`
- `scripts/gen_game_icons.py`
- `scripts/gen_game_icons_test.py`
- `scripts/check_upstream_touches.py`
- `scripts/check_upstream_touches_test.py`
- `scripts/check_flash_budget.py`
- `scripts/check_flash_budget_test.py`
- `scripts/fork_release.py`
- `scripts/fork_release_test.py`
- `scripts/fork_common.py` -- shared by the fork scripts; `docs/crosshatch/fork-scripts.md` has the conventions.
- `scripts/fork_common_test.py`
- `.github/workflows/crosshatch-*.yml` -- every fork-only workflow is named with this prefix.
- `scripts/check_api_freeze.py` -- the API freeze job's check (spine AD-19).
- `scripts/check_api_freeze_test.py`
- `scripts/check_layers.py` -- the layer check job's check of the spine's layer table (retro AI-5).
- `scripts/check_layers_test.py`

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

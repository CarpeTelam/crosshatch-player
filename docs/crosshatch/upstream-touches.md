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
| 6 | `src/activities/home/HomeActivity.h` | index mapping, `onGamesOpen()` | yes |
| 7 | `src/activities/home/HomeActivity.cpp` | item count, switch case, label; list mode reuses an existing `UIIcon` | yes |
| 8 | `src/components/CoverGridHomeUi.h` | tab array size | yes |
| 9 | `src/components/CoverGridHomeUi.cpp` | Games tile drawn from a `GameIcons` bitmap | yes |
| 10 | `src/network/OtaUpdater.cpp` | calls into `ForkRelease.h` for the update URL, asset name, and build-number comparison, and into `games/ForkReleaseProbe.h` after a failed fetch (AD-25) | yes |

Row 10 and fork releases (AD-25): an upstream merge that touches `src/network/OtaUpdater.*`,
`src/network/HttpDownloader.*`, `lib/JsonParser/ReleaseJsonParser.*`, `src/network/FirmwareBoardTag.*`, a release
workflow, or a `*-gh_release` env in `platformio.ini` is not done until a dry run of the fork release workflow
(`.github/workflows/crosshatch-release.yml`, "dry run" on, started from the Actions tab with the merge branch picked
under "Use workflow from") passes. The dry run builds the release envs and checks that each image still reports its
tag, holds the fork release URL and not upstream's, and carries its own board tag, so a clean merge that reroutes or
strands fork devices fails there instead of on a device. Upstream's `release.yml` and `release_candidate.yml` stay
disabled in the Actions tab and are never edited.

No reserve row remains.

## Allowlist

Upstream files the fork had already changed before the ledger existed.

- `AGENTS.md` -- the fork's own agent instructions; `.gitattributes` keeps our copy on merge.
- `.gitattributes` -- added by the fork for the `merge=ours` rule on `AGENTS.md`; listed in case upstream adds one.
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
- `scripts/gen_game_icons.py`
- `scripts/check_upstream_touches.py`
- `scripts/check_upstream_touches_test.py`
- `scripts/check_flash_budget.py`
- `scripts/check_flash_budget_test.py`
- `scripts/fork_release.py`
- `scripts/fork_release_test.py`
- `scripts/fork_common.py` -- shared by the fork scripts; `docs/crosshatch/fork-scripts.md` has the conventions.
- `scripts/fork_common_test.py`
- `.github/workflows/crosshatch-*.yml` -- every fork-only workflow is named with this prefix.

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

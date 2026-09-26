---
title: 'Refactor sweep'
type: 'chore'
ticket: '5'
created: '2026-09-26'
status: 'built'
baseline_revision: 'a2c1204a66125b3d3505247d03c77032b32261b1'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick']
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The epic's build records and deferred review findings leave a few loose ends: `sim.sh build` silently uses a stale `platformio.local.ini` after `simulator.ini` changes (so `simulator_x4pro` can build without `FREEINK_CAP_GAMES`), agents only learn of the upstream-touch ledger when its CI job fails, the three game-library headers still cite "later stories", and one ledger-doc paragraph breaks the file's line wrapping.

**Approach:** Cleanup only, no change to firmware behaviour, CI pass/fail semantics, or the release process: `sim.sh build` re-runs `setup` when the managed block differs from `simulator.ini` (SKILL.md says so); one AGENTS.md Policy line points at `docs/crosshatch/upstream-touches.md` and `scripts/check_upstream_touches.py`; the three header comments lose the planning reference; the row-10 paragraph of the ledger doc is rewrapped with its wording unchanged.

Scope decision (from the six plans, `deferred-work.md`, and `git diff f6de4dac..HEAD` without `lib/lua/src/`):
- Taken: the `sim.sh` staleness fix and the AGENTS.md pointer (both deferred items); planning references (`lib/GameCore/GameCore.h`, `lib/GameIcons/GameIcons.h`, `lib/GameScript/GameScript.h`: "later stories may remove it"; no `AD-n`, epic, story, or entry mention remains elsewhere outside `docs/crosshatch/` and `_bmad-output/`); the ledger-doc rewrap.
- Rejected, shared Python module for the three fork scripts: the overlap is a three-line `SetupError` class and differently shaped `git`/`pio` helpers (bytes with allowed exit codes, text with a result object, streamed output); a module would add a Game path and import coupling between a PR gate and a release tool that runs from a separate checkout, for no line saved worth having. Test files share nothing but `sys.path` setup.
- Rejected, composite action for the toolchain setup: only two `crosshatch-*.yml` copies exist (`ci.yml`'s is upstream's and stays); they differ on purpose (release installs packages per derived env inside `src/`, after the version rewrite, with its own cache key), and the release job checks out into `tools/` and `src/`, so a local action would need inputs and a path prefix, not be simpler. Each copy says it follows `ci.yml`, which is what an upstream toolchain change needs.
- Rejected, collapsing the six script entries under Game paths into patterns: `scripts/check_*` could match a future upstream script and fail every PR through the upstream-overlap rule; per-file entries stay explicit and cost six lines.
- Not code, left for the owner: the device check of the games-enabled update path (404 → no update, network failure → failed, offered once then not again).

</frozen-after-approval>

## Implementation Notes

Oneshot route: about 30 changed lines across seven files, all scripts, docs, and comments; no C++ logic or build configuration changes.

- Files: `lib/GameCore/GameCore.h`, `lib/GameIcons/GameIcons.h`, `lib/GameScript/GameScript.h` (comment: the stub "can go once the library has other source files", which is also true of GameCore, whose `ForkRelease.h` is header-only); `.claude/skills/run-crosshatch-player/sim.sh` (`managed_block`, the check in `cmd_build`, and an end-marker guard in `cmd_setup`); `.claude/skills/run-crosshatch-player/SKILL.md`; `AGENTS.md` (one Policy line); `docs/crosshatch/upstream-touches.md` (row-10 paragraph rewrapped; `git diff --word-diff` shows no word changed).
- `sim.sh` exercised with a fake `pio` on PATH: up-to-date block → no setup; block without the games flag plus a user section → setup runs, flag back, user section kept; missing file → setup runs; unknown device → exit 1; begin marker without end marker and a user section after it → `build` and `setup` both exit 1 naming the markers, file unchanged.
- Real `sim.sh build x4pro` from a block with the flag removed: refresh message, block rewritten, `GameCore`/`GameIcons`/`GameScript` in the graph and compiled, then the build failed in `src/games/ForkReleaseProbe.cpp` (`SecureHttpClient` has no `setUserAgent`/matching `GET` in the simulator library). That is a bug from the fork update source entry, not from this sweep: the probe's only caller, `network/OtaUpdater.cpp`, is excluded from the simulator build but the probe is not, so `simulator_x4pro` and `simulator_sticky` have not built since. Fixed in a separate `fix:` commit that excludes the probe in `simulator.ini` beside `OtaUpdater.cpp`.
- ctest 388/388, `pio check` (default env) no defects (182 s), `pio run -e x4pro` SUCCESS, `clang-format-fix` no change, all three script suites pass.

## Plan Change Log

## Review Triage Log

Pass 1 (lens: quick, headless session). Counts: high 0, medium 0, low 3, false 0, maybe-false 0. Patches 3, deferred 0.

| # | Finding | Verdict | Route | Evidence |
|---|---------|---------|-------|----------|
| 1 | `GameCore.h`: "can go once the library has real code" is already met, since `ForkRelease.h` is real code | low | patch | True; `ForkRelease.h` is header-only, so `GameCore.cpp` is still the library's only translation unit. Reworded in all three headers to "once the library has other source files". |
| 2 | `build` also runs a first-time `setup`, which the message ("differs from its copy") and SKILL.md do not say | low | patch | Reproduced with no `platformio.local.ini`. Message now "lacks the current simulator.ini; running setup"; SKILL.md names both cases. |
| 3 | A managed block without its end marker makes `setup`, now reachable from every `build`, drop the user's settings after it; CRLF files get a second block | low | patch (marker), reject (CRLF) | Reproduced the drop. `cmd_setup` now refuses when the begin marker has no end marker (tested: exit 1, file unchanged). CRLF: the simulator runs on Linux, and a duplicated section makes PlatformIO's config parser fail loudly. |

## Verification

**Commands:**
- `python3 scripts/check_upstream_touches_test.py`, `scripts/check_flash_budget_test.py`, `scripts/fork_release_test.py` -- expected: 18, 22, 50 pass.
- `python3 scripts/check_upstream_touches.py` after the commit -- expected: PASS.
- `sim.sh setup`, then edit the managed block of `platformio.local.ini` by hand and run `sim.sh build x4pro` -- expected: it reports the refresh, rewrites the block, and the build defines `FREEINK_CAP_GAMES=1`; a second `build` reports nothing.
- `./bin/clang-format-fix && git diff --exit-code` -- expected: clean.
- `cmake ... && ctest --test-dir build/test -j4` -- expected: all pass.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: no defects.
- `pio run -e x4pro` -- expected: success (comment-only C++ change).

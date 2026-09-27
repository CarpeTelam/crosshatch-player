---
title: 'API level, HostCaps, and Manifest::check'
type: 'feature'
ticket: '4'
created: '2026-09-27'
status: 'built'
baseline_revision: 'cc87a39878ea2fa02be37bdf3513d05c824b7188'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The host has no API level, no written level-1 surface, and no startability check, so the Games list shows every parsed manifest, even one needing a newer API or more seats than the host has.

**Approach:** Add `lib/GameCore/ApiLevel.h`, `docs/crosshatch/api-level-1.txt` (typed entries, parseable line by line), `GameCore::HostCaps` with one `src/games` provider, and `Manifest::check(hostCaps)` returning `Invalid`, `Unavailable`, or `Ok` plus the host-startable modes (AD-15); the Games list keeps only `Ok` games that can start solo.

## Boundaries & Constraints

**Always:**
- `ApiLevel.h`: `API_LEVEL` 1, `API_MIN_LEVEL` 1, `API_LEVEL_FROZEN` false, `API_SURFACE_CRC`, each `#define NAME VALUE` on one line (story 2.5 reads them from Python).
- The list holds every level-1 entry this epic ships: R2's `ch` surface except `ch.gfx.icon`/`image`, `print`/`require`, the standard-library globals, events, `ctx` fields, enums, manifest keys, limits (including the owner's caps: `name` 64 B, `version` 32 B, `icon` 32 B), and `seats_max`. Its header states the grammar and that a duplicate known manifest key is invalid, so `pack_game.py` must reject duplicates too.
- `HostCaps` provider: `api`/`minApi` from `ApiLevel.h`, `maxSeats` 2, `nearby` false (epic-play-nearby turns it on for device builds; always false under `SIMULATOR`).
- `check` reads only the manifest and its argument; AD-2 guards and static rules; fork paths only (no ledger rows).

**Never:** `ch.api`/`ctx.api` (2.8, 2.10); the freeze job or release notes (2.5); the live surface test (2.14); icon entries; `docs/crosshatch/formats.md`, `lib/GameScript/`, `test/game_script/`, `scripts/game_codec*` (story 2.6 owns them); committing the `api = 2` scratch fixture.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| In range | `minApi` ≤ `api` ≤ `api`(host), `seats.min` ≤ `maxSeats`, a startable mode | `Ok`, `modes` = declared modes the host can start | — |
| API outside range | `api` = `minApi` − 1 or host `api` + 1 | `Unavailable` (`ApiTooOld` / `ApiTooNew`) | list skips, `LOG_INF` |
| Too many seats | `seats.min` = `maxSeats` + 1 | `Unavailable` (`TooManySeats`) | list skips |
| Nearby only, host without nearby | `modes` = [nearby] | `Unavailable` (`NoHostMode`) | list skips |
| Mode contradicts seats | `solo` with `seats.min` ≠ 1; `nearby` with `seats.max` < 2 | `Invalid` | list skips |
| Broken struct | default `Manifest`, `api` < 1, bad seats, no or unknown mode bits | `Invalid` (`BadFields`) | — |
| Ok without solo | `modes` = [pass], seats 2..2 | `Ok` (pass) but not listed (solo only until the launcher) | `LOG_INF` |

</frozen-after-approval>

## Code Map

- `lib/GameCore/Manifest.{h,cpp}` -- `Manifest` (fields, `MAX_*_BYTES`, `Mode` bits, `hasMode`), `ManifestError` + `describe`; parse already rejects bad seats ranges, empty modes, duplicate keys, and the four caps. Add `check` beside `parse`; leave `ManifestReader` alone.
- `lib/GameCore/ForkRelease.h` -- style reference for a pure header (`inline constexpr`).
- `src/games/GameRandom.{h,cpp}` -- provider pattern: whole-file `#if FREEINK_CAP_GAMES`, `#if defined(SIMULATOR)` branch.
- `src/activities/games/GamesListActivity.cpp` `loadGames`/`readManifest` -- filter point; keep capacity and sort logic.
- `test/game_core/CMakeLists.txt` -- add sources; the vectors path compile definition shows how to pass a data path.
- `lib/lua/src/lbaselib.c` `base_funcs` -- Lua 5.5.1 base globals; game-api-seed section 6 drops `load`, `loadfile`, `dofile`.
- `.claude/skills/run-crosshatch-player/sim.sh` -- `setup`, `build x4pro`, `start x4pro`, `tap`, `ss`, `log`; Games at 240,562; games live in `fs_/.games/`.

## Tasks & Acceptance

**Execution:**
- [ ] `docs/crosshatch/api-level-1.txt` -- grammar header and entries per Design Notes.
- [ ] `lib/GameCore/ApiLevel.h` -- the four defines, CRC computed from the list, `static_assert(API_MIN_LEVEL <= API_LEVEL)`.
- [ ] `lib/GameCore/HostCaps.h` -- `struct HostCaps {int32_t api, minApi, maxSeats; bool nearby;}`.
- [ ] `lib/GameCore/Manifest.{h,cpp}` -- `CheckStatus`, `CheckReason` + `describe`, `CheckResult{status, reason, modes}`, `Manifest::check(const HostCaps&) const`.
- [ ] `src/games/GameHostCaps.{h,cpp}` -- `GameCore::HostCaps gameHostCaps()`.
- [ ] `src/activities/games/GamesListActivity.cpp` -- keep a parsed game only when `check(gameHostCaps())` is `Ok` and `modes` has solo; log the reason otherwise.
- [ ] `test/game_core/{ManifestCheckTest,ApiLevelTest}.cpp`, `CMakeLists.txt` -- the matrix at each boundary; the list parses under the grammar, has no duplicate entries, its manifest limits equal `Manifest::MAX_*_BYTES`, and its CRC equals `API_SURFACE_CRC`.

**Acceptance Criteria:**
- Given `cmake`/`ctest` on `test/`, then every suite passes; editing any entry line without updating `API_SURFACE_CRC` fails `ApiLevelTest`.
- Given the simulator (x4pro) with the tracer and a scratch copy with `"api": 2` in `fs_/.games/`, when Home → Games opens, then only Tracer is listed and the log names the skipped game's reason.
- Given `pio run -e x4pro` and `-e default`, then both build; `./bin/clang-format-fix` leaves no diff.

## Implementation Notes

- Implemented directly (no subagent tool in this session). Files: `docs/crosshatch/api-level-1.txt` (96 entries), `lib/GameCore/{ApiLevel.h,HostCaps.h}`, `lib/GameCore/Manifest.{h,cpp}` (`CheckStatus`, `CheckReason` + `describe`, `CheckResult`, `check`), `src/games/GameHostCaps.{h,cpp}`, `src/activities/games/GamesListActivity.{h,cpp}`, `test/game_core/{ApiLevelTest,ManifestCheckTest}.cpp`, `test/game_core/CMakeLists.txt` (`API_LEVEL_LIST_DIR`, `API_LEVEL_HEADER_PATH`). All fork paths; no ledger row.
- `API_SURFACE_CRC` = `0x1A78D21F` (Python: `zlib.crc32` over the entry lines, each plus `\n`). A scratch `seats_max 3` failed `SurfaceCrcMatchesTheLists` with the new value in its message; a comment-only edit passed.
- `ApiLevelTest.DefinesStayOnSingleRegularLines` pins the define shape story 2.5 parses (`^#define (API_[A-Z_]+) (0|[1-9][0-9]*|true|false|0x[0-9A-F]{8})$`).
- `check` also re-validates the struct against the parse rules (`BadFields`), so a default `Manifest` is `Invalid`, never `Ok`. `pass` is startable whenever the earlier rules hold (no source sets a seat floor for it).
- Package limits (256 KB, 32 members, 128 KB) and module names are left for epic-install-and-launcher to append; the game table's entry points stay in AD-8.
- `./bin/clang-format-fix` also rewrote `lib/GameScript/CanvasClip.h` and `test/game_script/CanvasClipTest.cpp` (drift from dca1ddc2); reverted, since story 2.6 owns those paths, and deferred.

## Plan Change Log

## Review Triage Log

Pass 1 (lenses run in this session one at a time, no subagents): high 0, medium 0, low 3, false 3, maybe-false 1.

| Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|
| blind | `ApiLevelTest.LevelsAreInRange` pins `API_LEVEL == 1` and `API_LEVEL_FROZEN` false, so opening level 2 or freezing level 1 breaks an unrelated test | low | patch | Deleted the test; kept `static_assert(API_MIN_LEVEL == 1)` (spine AD-19). |
| blind | Unprefixed `API_*` macros may collide with other headers | false | reject | Only `GameHostCaps.cpp` and the tests include `ApiLevel.h`; x4pro, default, and simulator builds are clean; the names are the spine's. |
| blind | `pass` with `seats {1, 1}` counts as startable | maybe-false | reject | Needs a spine rule for pass seats (AD-11 says n within `seats` and the host maximum); if true it is only low (the launcher would offer a one-seat pass). |
| blind | Grammar does not map a `fn` parameter to its enum set (`refresh(mode?)` vs `enum refresh`) | low | reject | 2.14 checks entries, not parameter types; closing it adds grammar. |
| edge | A CRLF checkout of the list fails the grammar test | false | reject | Loud failure naming the line is the intended behavior; CI checks out LF. |
| verification-gap | The list filter and `gameHostCaps()` have no automated test | low | defer | No host harness for activities or `src/games`; simulator runs recorded below; `deferred-work.md`. |
| verification-gap | Provider rules (nearby off in the simulator) unpinned | false | reject | `NEARBY = NEARBY_BUILT && !IS_SIMULATOR` enforces it by construction; a test would restate constants. |
| intent | Diff implements the only reading (list keeps `Ok` + solo; `api = 2` absent); the added `field` kind is the list-grammar decision the entry's unknown hands to this story | — | none | Descriptive only. |

## Design Notes

**List grammar** (settles the entry's unknown). `#` lines and blank lines are ignored. Every other line is `<kind> <body>`, ASCII, single spaces, no trailing space, so `^(fn|field|enum|event|ctx|manifest|limit|lib|icon|seats_max) (.+)$` splits it:

```
fn ch.gfx.rect(x, y, w, h, color, filled?)     # path(params) [-> returns]; ? optional, ... varargs
fn ch.text_width(str, size) -> integer
field ch.screen.w integer                      # a non-function value under ch
enum color light                               # <set> <value>
event swipe x y dir                            # <kind> [fields]
ctx mode string
manifest seats.min integer                     # nested keys dotted; optional type ends in ?
limit store_bytes 4096                         # <name> <integer>; the unit is in the name
lib pairs                                      # one global from Lua 5.5.1's standard libraries, unmodified
seats_max 2
```

`field` is the one kind added to AD-19's set: `ch.api` and `ch.screen.w`/`h` are values, not functions. Globals are the `fn` entries without a dot (`print`, `require`), the `lib` entries, and `ch`; so 2.14 can fail an unlisted global. Library tables (`lib string`) stand for the whole unmodified table (AD-4). The game table's entry points stay in AD-8, not the list.

**Surface CRC.** CRC-32 (zlib polynomial) over the entry lines of `api-level-<API_MIN_LEVEL>.txt` through `api-level-<API_LEVEL>.txt` in order, each followed by `\n`, with comments and blank lines left out: a comment edit then never splits two devices' surfaces (AD-13). Python matches it with `zlib.crc32`; the test implements it bitwise.

**Check order.** `Invalid` first (struct rules, then mode/seat contradictions, per game-api-seed section 1), then `api` range, then `seats.min` against `maxSeats`, then modes: solo and pass start whenever the earlier rules hold; nearby needs `nearby` and `maxSeats` ≥ 2. `Ok` needs one startable mode.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `pio run -e default` -- SUCCESS.
- `sim.sh build x4pro`, `start x4pro`, Home → Games, `ss` -- screenshot saved in `story-api-level-screenshots/`.
- `./bin/clang-format-fix` -- no diff.

**Verification record** (2026-09-27, working tree before commit, on cc87a398):
- Host: `ctest` 446/446 passed after the review patch (`GameCoreTest` 48: `ApiLevelTest` 7, `ManifestCheckTest` 15).
- `pio run -e x4pro` SUCCESS (88.2 % flash, 31.1 % RAM); `pio run -e default` SUCCESS; `sim.sh build x4pro` SUCCESS.
- `pio check` (default, as CI) and `pio check -e x4pro`: no defects. `check_upstream_touches.py`: PASS. `./bin/clang-format-fix`: no diff in this story's files.
- Simulator x4pro, Home → Games with the tracer, a scratch `future` (`"api": 2`), and a scratch pass-only `duo` in `fs_/.games/`: only Tracer listed ([games-api-filter.png](story-api-level-screenshots/games-api-filter.png)); log `Skipping future: api newer than this host supports`, `Skipping duo: no solo mode on this host`, `Found 1 games`; Tracer still opens and draws ([tracer-still-opens.png](story-api-level-screenshots/tracer-still-opens.png)). Scratch games removed afterwards.

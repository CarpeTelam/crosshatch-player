# Review: epic 1 retro reconciliation of the spine (verification lens)

- Scope: the uncommitted diff to `ARCHITECTURE-SPINE.md` (changed lines only), checked against HEAD `66b60954`.
- Lens: is every changed claim confirmed against the web, the repository, or the tools as they are today, and not asserted from memory?
- Date: 2026-09-27

## Verdict

**Pass with minor fixes.** Every load-bearing technical claim in the diff holds against the code, the toolchain source, and the upstream Lua tarball. One claim is incomplete (the `fork_release.py` subcommand list). The new "KB here is KiB" sentence relabels a measurement that was taken in decimal bytes. Three wording overstatements and one lagging as-built doc remain. None of them changes a decision.

## Verified (no action)

| Changed claim | Evidence |
| --- | --- |
| Lua 5.5.1 is vendored byte-for-byte | Downloaded `https://www.lua.org/ftp/lua-5.5.1.tar.gz` today (HTTP 200); `diff -r lua-5.5.1/src lib/lua/src` shows no differences (63 files each). |
| 5.5.1 defaults `LUA_COMPAT_GLOBAL` on; `luaconf.h` unedited | `lib/lua/src/luaconf.h:344-345` (`#if !defined` … `#define LUA_COMPAT_GLOBAL 1`); the file matches the tarball. |
| `library.json` passes `-DLUA_COMPAT_GLOBAL=0` | `lib/lua/library.json:22-24`. |
| The flag reaches the Lua units only | PlatformIO Core 6.1.19 (`~/.platformio/penv/.../piolib.py`): each library builder clones the env (`:124`) and applies `build.flags` to that clone (`:298`). `build()` copies only `CPPPATH/LIBPATH/LIBS/LINKFLAGS` to dependents (`:479`), never `CPPDEFINES`. Host: `test/game_script/CMakeLists.txt` mirrors the manifest's `-D` entries as `PRIVATE` definitions on `lua_vendored` and stops configuration on any entry it cannot mirror. |
| It is the one compat define | The only `LUA_COMPAT_*` names in `lib/lua/src` are `GLOBAL`, `MATHLIB` (commented out, `luaconf.h:355`), and `APIINTCASTS` (never defined). `test/game_script/LuaOnHostTest.cpp:87` fails if `global` parses as a name. |
| Row 10 now also calls `games/ForkReleaseProbe.h` | `src/network/OtaUpdater.cpp:21-28` (guarded include) and `:81-90` (guarded call after `!ok`). |
| Probe requests the URL once more; any other failure stays `HTTP_ERROR` | `src/games/ForkReleaseProbe.cpp:18-33`: one GET, returns true only when the status is 404. Any other status, a connect failure, or OOM returns false, and `OtaUpdater.cpp:88-89` then returns `HTTP_ERROR`. |
| `HttpDownloader` is not in the ledger and is unchanged by the fork | `docs/crosshatch/upstream-touches.md:28-37` has no such row. `git show --stat 311e4bb4` (the AD-25 commit) does not touch `HttpDownloader.*`. |
| Probe is device-only and excluded beside `OtaUpdater.cpp` | `.claude/skills/run-crosshatch-player/simulator.ini:15-17`. The probe works on every games env because `FREEINK_NET_WOLFSSL=1` sits in `[base]` (`platformio.ini:73`). |
| `ForkRelease::LATEST_RELEASE_URL` needs the constexpr exemption | `lib/GameCore/ForkRelease.h:27`: `inline constexpr char[]` of 74 B (over the 64 B cap), in `.rodata` (flash on ESP32-S3). |
| `ForkRelease.h` is pure and header-only | `lib/GameCore/ForkRelease.h:3-6` includes only std headers; every function is `constexpr`. |
| `GamesBuildAnchor.cpp` in `src/games/` | `src/games/GamesBuildAnchor.cpp` exists, whole-file guarded, and includes each game library's header plus Lua. |
| `check_upstream_touches.py` exit codes 0/1/2 | `scripts/check_upstream_touches.py:5-9,178,192`. |
| `check_flash_budget.py`: on/off x4pro pair, 250 KiB; the limit is 256,000 B | `scripts/check_flash_budget.py:9-14,42-43,191` (250 × 1024 = 256,000); `crosshatch-ci.yml` sets `FLASH_BUDGET_KIB: 250`. |
| `fork_version_vectors.json` is read by the C++ suite and `fork_release.py` | `test/game_core/CMakeLists.txt` (`FORK_VERSION_VECTORS_PATH`), `test/game_core/ForkReleaseTest.cpp:14`, `scripts/fork_release.py:51`. Keys include `tag_grammar`, `valid_tags`, `invalid_tags`, `newer`, and `asset_names`. |
| Existing PR workflow builds all five envs and runs the host suites incl. game_core/game_script | `.github/workflows/ci.yml:85-97` (matrix default/sticky/x4pro/x4c/papermono), `:172-197` (ctest); `test/CMakeLists.txt:77-78`. |
| `crosshatch-ci.yml` rolls up under `Crosshatch Test Status`; `crosshatch-release.yml` exists | `crosshatch-ci.yml` (jobs `upstream-ledger`, `flash-budget`, `crosshatch-test-status` with both in `needs`). |
| The fork URL is live | `api.github.com/repos/CarpeTelam/crosshatch-player` answers 200 (public), and `releases/latest` answers 200 with `1.6.5-ch.2`. |

## Findings

### F1 (medium): the `fork_release.py` subcommand list is incomplete

- Spine: Structural Seed, `scripts/fork_release.py  # AD-25 release steps: preflight, prepare, build, check-images, pack-games, notes`
- Evidence: `scripts/fork_release.py:9-21` (docstring) and `:677-716` (`add_parser`) also define `recheck` and `expected-assets`. `crosshatch-release.yml:197` runs `recheck` before publishing, and `:216` runs `expected-assets`. `recheck` enforces "N is never reused", a rule the spine relies on.
- Fix: list all eight steps: `preflight, prepare, build, check-images, pack-games, notes, recheck, expected-assets`. Alternatively, write "release steps (preflight … notes, then recheck and expected-assets before publishing)".

### F2 (medium): "KB here is KiB" relabels a decimal measurement

- Spine: Operational envelope, Flash budget row, the new last sentence, which applies to "Lua alone measured +124 KB".
- Evidence: `_bmad-output/planning-artifacts/research/spike-script-engine-2026-09-26.md:90` records +124,232 B. That is 124.2 kB decimal but 121.3 KiB, so the row now misstates the spike. The 86.3% baseline is pio's "Flash used" figure (5,657,610 B of 6,553,600). The gate compares `firmware.bin` sizes, which were 5,662,624 B on the same commit (`story-x4pro-flash-budget-gate-plan.md:85`). The two are different metrics. Only the 250 KiB limit is confirmed by the script.
- Fix: scope the sentence to the limit: "The limit is 250 KiB (256,000 B) …; the spike figures are decimal (Lua +124,232 B ≈ 121 KiB)." Or restate the figures in KiB. Optionally note that the baseline % is pio's Flash-used figure and the gate compares `firmware.bin` sizes.

### F3 (low): "each fork script has a sidecar `<name>_test.py` run on every PR" is broader than the code

- Spine: Structural Seed, the comment under `fork_release.py`. Also the `.github/workflows/` line: "crosshatch-ci.yml (ledger, flash budget, fork script tests, …)".
- Evidence: sidecars exist for the three existing fork scripts: `scripts/check_upstream_touches_test.py`, `check_flash_budget_test.py`, `fork_release_test.py`. The same seed lists `scripts/pack_game.py`, `game_codec.py`, and `gen_game_icons.py`, none of which exist yet. Read as a rule, the comment commits each of them to a sidecar wired into CI. Read as a description, it is false for them. The tests are steps inside the two existing jobs (`crosshatch-ci.yml`: ledger job runs `check_upstream_touches_test.py` and `fork_release_test.py`; flash-budget job runs `check_flash_budget_test.py`), not a third job. They also run only on `pull_request` into `develop`.
- Fix: say either "each fork script so far (ledger, flash budget, release) has a sidecar `_test.py` that `crosshatch-ci.yml` runs on every PR to `develop`" or make it an explicit rule: "every fork script ships a sidecar `_test.py` and a step in `crosshatch-ci.yml`". In the workflows line, write "two jobs (ledger + release-script tests; flash budget + its tests)".

### F4 (low): the as-built ledger doc now lags the spine

- Spine: AD-3 row 10 (adds "and into `games/ForkReleaseProbe.h` after a failed fetch"), and AD-25 "Upstream workflows" (adds `HttpDownloader.*` to the dry-run trigger list).
- Evidence: `docs/crosshatch/upstream-touches.md:37` still has the old row-10 text. Its dry-run paragraph (`:39-41`) lists `OtaUpdater.*`, `ReleaseJsonParser.*`, `FirmwareBoardTag.*`, a release workflow, and `*-gh_release` envs, without `HttpDownloader.*`. The ledger check enforces paths only, so CI will not catch the drift. The file says it is the operational form of AD-3.
- Fix: in the same change or a follow-up, update `docs/crosshatch/upstream-touches.md` row 10 and the dry-run list to match. It is a fork-only file, so the ledger is unaffected.

### F5 (low): the required-checks claim is sourced, not confirmed

- Spine: Operational envelope, CI row: "branch protection requires `Test Status` and `Crosshatch Test Status`".
- Evidence: this matches `AGENTS.md` ("Branch protection requires only `Test Status` and `Crosshatch Test Status`") and the `crosshatch-ci.yml` header. The repository settings were not readable from this session (no `gh`; the GitHub MCP has no branch-protection read).
- Fix: none needed if AGENTS.md is trusted. Optionally cite AGENTS.md as the source, or confirm in Settings → Branches once.

### F6 (nit): two wording precision points

- AD-2: "includes a header from each game library and `lua.h`". `src/games/GamesBuildAnchor.cpp:16` includes `<lua.hpp>`, Lua's extern-"C" wrapper that pulls in `lua.h`, `lualib.h`, and `lauxlib.h`. The meaning holds. Write "`lua.hpp` (hence `lua.h`)" if exactness matters.
- AD-25: "`HttpDownloader` reports every non-200 as a bare failure". It follows 301/302/303/307/308 first (`src/network/HttpDownloader.cpp:45-46,107-121,186-196`), so the accurate wording is "every non-200 final status". This matters if the fork URL ever redirects: the probe does not follow redirects and would report `HTTP_ERROR`, which is the intended fallback.
- AD-4, for completeness: "every `LUA_COMPAT_*` option off" holds for the Lua units. C++ units that include `luaconf.h` (the anchor, later GameScript) see the default `LUA_COMPAT_GLOBAL 1`. This is harmless because only `llex.c:191` and `lparser.c:2117` read it. No change needed.

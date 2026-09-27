# Adversarial review: the epic-1 retrospective update to the spine

Reviewed 2026-09-27. Scope: the uncommitted diff to `ARCHITECTURE-SPINE.md`, which changes AD-2, AD-3 row 10, AD-4, AD-25 (Update source and Upstream workflows), the Structural Seed, and the CI and Flash budget rows. It is checked against `epic-platform-baseline-retrospective.md` (P4, P5, V2, A1, A4, A5, A6, AI-1 to AI-9) and the as-built code at `9ca639ca`:

- `lib/GameCore/ForkRelease.h`, `src/games/ForkReleaseProbe.{h,cpp}`, `src/games/GamesBuildAnchor.cpp`;
- `src/network/OtaUpdater.cpp`, `src/network/HttpDownloader.cpp`, `lib/lua/library.json`;
- `.claude/skills/run-crosshatch-player/simulator.ini`, `.github/workflows/crosshatch-ci.yml`;
- `scripts/fork_release.py`, `test/game_core/{ForkReleaseTest.cpp,fork_version_vectors.json}`;
- `docs/crosshatch/upstream-touches.md`, `.gitignore`, `bin/clang-format-fix`, `sdkconfig.defaults`.

Lens: for each hole, two units one level down that each obey every AD to the letter and still build incompatibly. The focus is on holes that the changed text opens, or that it touches and leaves open.

## Verdict

**Accept the diff as an accurate record of the as-built code, but not as a finished spine. Close H1 to H4 before the next epic session starts.**

Every sentence in the diff matches the code. The retro's AI-6 items (a) to (d) are all applied. The problem is that three of the new sentences codify as-built accidents as rules:

- the per-filename simulator exclusion that AI-1 is about to remove;
- a probe that the spine says copies HttpDownloader's transport, which it does not;
- a static-storage rule whose new wording lets `const`, function-local, and `src/games` storage through.

Independent sessions (script runtime, icons, install and launcher, pass-and-play, play-nearby) will read these sentences as permission. Nine holes follow. H1 to H4 are the ones two epics are likely to hit.

---

## H1 [High] AD-2's reworded storage rule constrains the wrong things, so RAM gets spent through five legal doors

**Changed text (AD-2):** "`lib/Game*` has no namespace-scope objects with non-trivial constructors and no mutable static storage over 64 B; `constexpr` data, which stays in flash, is exempt (generated icon data, `ForkRelease::LATEST_RELEASE_URL`)."

Background: on the C3 the rule protects little. PlatformIO archives each library (no `lib_archive = no` in `platformio.ini`), and the only include paths into game code are guarded, so no member of `libGameCore.a` and the others is ever pulled into a C3 link. The RAM this rule really guards is the S3's internal DRAM. The Operational envelope ("Internal RAM") says the only internal-RAM costs there are the `GameVM` and `GameLink` stacks and the Wi-Fi driver. The new wording leaves these doors open.

**Pair 1a: a `const` table with dynamic initialization.**
- Unit A (`lib/GameScript`, the `ch.gfx.icon` binding) resolves names through a function-local `static const std::unordered_map<std::string_view, IconRef> index = buildIndex();`. It is not namespace-scope and it is `const`, so it is not "mutable". AD-2 is obeyed.
- Unit B (`lib/GameCore::Manifest::check`) keeps a `static const std::string kModes[] = {"solo", "pass", "nearby"};` inside `check()`. That obeys AD-2 for the same reasons.
- Incompatibility:
  - Both allocate on first call from the **system heap**. Internal DRAM takes allocations up to 4 KB (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`), and the memory is never freed. That breaks the envelope.
  - Unit A's first call runs on the `GameVM` task, whose heap is supposed to be the arena alone (AD-6).
  - Worse, AD-5 lets the match **delete the `GameVM` task** after a 500 ms join timeout. If that happens while the task is inside a guarded static's initializer, the `__cxa_guard` stays pending. The next match's first icon draw then blocks forever in `__cxa_guard_acquire`.

**Pair 1b: the rule covers only `lib/Game*`.**
- Unit A, the play-nearby `EspNowLink` in `src/games`, declares `static Frame ring[16];`, which is about 24 KB of `.bss`. AD-2's storage clause does not reach `src/games`, and the Memory convention asks for "fixed-size receive buffers".
- Unit B, the script-runtime arena backend in `src/games`, declares `EXT_RAM_BSS_ATTR static uint8_t arena[256 * 1024];`. That is a legal PSRAM `.bss` array.
- Incompatibility:
  - A costs 24 KB of internal RAM on every x4pro and sticky boot, even when no game ever runs. That breaks the envelope's list of costs, and the 100 KB lobby threshold was set without it.
  - B reserves 256 KB of PSRAM permanently, which the reader's PSRAM features lose. It also contradicts AD-6: "Abandoning a VM frees the arena in one call" cannot free a static.

**Pair 1c: `constexpr` data in a header is copied per translation unit.**
- Unit A, the icon epic's `gen_game_icons.py`, emits `constexpr uint8_t kIcon32[...] = {...};` at namespace scope in `GameIcons.generated.h`. AD-2 exempts it.
- Units B, C, and D each include that header and ODR-use the arrays: the `GameScript` icon binding, `src/games/FrameReplay` for the blit, and `CoverGridHomeUi.cpp` (ledger row 9).
- Incompatibility: a namespace-scope `constexpr` variable is implicitly `const`, which gives it internal linkage. Each TU therefore keeps its own copy of the roughly 40 KB of icons, and GNU ld does not fold identical data. Three or four copies alongside Lua's +124 KB exceed the 250 KiB flash budget. The gate fails at the end of the icon epic, and no rule told the generator author what shape to emit.
- The parenthetical "which stays in flash" is also wrong for a block-scope `constexpr` array whose address is taken, since that one goes on the stack.

**Pair 1d: many small buffers.**
Ten units each add a 64 B mutable static. Each obeys the rule, and the total is unbudgeted. That is minor on its own, but no gate measures internal RAM at all. The flash gate measures `.bin` size only.

**Adjacent, left open by the parenthetical "generated icon data":** `.gitignore:12` has `*.generated.h`, and `git check-ignore` confirms that `lib/GameIcons/GameIcons.generated.h` is ignored. AD-24 requires that file to be "committed". The icon epic's `git add` will silently skip it. If it is force-added, the whole-tree `bin/clang-format-fix` then formats it, because the wrapper excludes only `builtinFonts`, `hyphenation/generated`, `uzlib`, and `miniz/third_party`. The next regeneration then shows up as a diff.

**Proposed rule (replaces the AD-2 sentence):**

> Game code (`lib/Game*`, `src/games`, `src/activities/games`) has no object of static or thread storage duration with dynamic initialization, at any scope (namespace, class, or function-local), and no static storage over 64 B in any memory region (DRAM, PSRAM `EXT_RAM_BSS_ATTR`, or IRAM). Every larger buffer is allocated per match by its AD-20 owner and freed on exit. Read-only tables are `inline constexpr` at namespace scope, or `static constexpr` inside a function or class, and are therefore constant-initialized, stored once, and in flash. Generated data follows the same rule: `GameIcons.generated.h` declares `inline constexpr` arrays, is un-ignored in `.gitignore` (an allowlisted fork file), and is excluded from `bin/clang-format-fix` by a `lib/GameIcons/.clang-format` with `DisableFormat: true`. The flash budget job also reports the games-on minus games-off `.data` + `.bss` difference for x4pro and fails above 1 KiB.

---

## H2 [High] The simulator exclusion is now spine law, and it is the mechanism AI-1 is removing

**Changed text (AD-25, Update source):** "The probe is device-only and is excluded from the simulator build beside `OtaUpdater.cpp`."

**Unchanged text (AD-2):** "Under `SIMULATOR`, `EspNowLink` and `nearby` are compiled out and the SHA-256 helper uses OpenSSL."

The spine now describes two mechanisms, a `build_src_filter` filename line in `simulator.ini:16-17` and "compiled out" (undefined). It names no owner for the seam between device-only and simulator-built code. The CI row lists no simulator build. Retro P4 is exactly this failure, and AI-1 plans to replace the filename line with a `!SIMULATOR` guard. The new AD-25 sentence then contradicts AI-1.

**Pair 2a: two ways to exclude a file.**
- Unit A, the play-nearby epic, follows the AD-25 precedent. It keeps `EspNowLink.cpp` whole-file `#if FREEINK_CAP_GAMES` (AD-2) and adds `-<games/EspNowLink.cpp>` to `simulator.ini`, which "compiles it out".
- Unit B, the same epic's `NearbySession.cpp`, and `GameLobbyActivity`, which constructs it, stay simulator-built, because only `EspNowLink` and "nearby" are named.
- Incompatibility: the simulator link fails with undefined `EspNowLink` symbols, and no CI job builds the simulator. The next UI session finds out when AGENTS.md tells it to screenshot a change (the P4 repeat).

**Pair 2b: two places decide whether Nearby is offered.**
- Unit A, the launcher's `GameModeActivity`, hides the Nearby button with `#ifndef SIMULATOR`.
- Unit B, `GameCore::Manifest::check(hostCaps)`, returns `Ok` with `modes = [nearby]` for a nearby-only game, because `hostCaps` has no radio field. AD-15 makes `check()` the only arbiter of the modes a host can satisfy.
- Incompatibility: in the simulator the launcher lists a nearby-only game as startable, and the mode step is skipped because the game offers one mode (AD-22). The tap goes straight to the lobby, whose radio is compiled out.

**Proposed rule (replaces the AD-25 sentence, and extends AD-2's last sentence):**

> Device-only game code (anything that needs the radio, mbedTLS, `SecureHttpClient`, or OTA) is wrapped whole-file in `#if FREEINK_CAP_GAMES && !defined(SIMULATOR)` in the file itself. `simulator.ini` excludes no file under `src/games/` or `src/activities/games/` by name. `EspNowLink`, `NearbySession`, `GameLobbyActivity`, and `ForkReleaseProbe` are device-only. Their simulator-built callers are guarded the same way. `hostCaps` carries `nearby` (false under `SIMULATOR`), and only `Manifest::check` removes a mode. `crosshatch-ci.yml` builds `simulator_x4pro` as a job in `Crosshatch Test Status`'s `needs`. Any ticket that adds code under `FREEINK_CAP_GAMES` lists the simulator env in its verification.

---

## H3 [High] `ForkReleaseProbe` is a second owner of the release request, and the spine lets it drift

**Changed text (AD-25):** "`HttpDownloader` reports every non-200 as a bare failure and is not in the ledger, so after a failed fetch `OtaUpdater.cpp` asks the fork-only `src/games/ForkReleaseProbe`, which requests the URL once more and reads the status…"

**Also changed:** ledger row 10 now includes `games/ForkReleaseProbe.h`, and the dry-run trigger list adds `HttpDownloader.*`.

Three facts:

1. **It already diverges.** `ForkReleaseProbe.cpp:23` says "Same transport settings as HttpDownloader's release fetch", but the settings differ:

   | Setting | HttpDownloader | ForkReleaseProbe |
   | --- | --- | --- |
   | Timeout | 60 s (`HTTP_TIMEOUT_MS`, `HttpDownloader.cpp:33,72`) | 15 s, the client default (`SecureHttpClient.h:614`) |
   | Redirects | up to 5 (`MAX_REDIRECTS`) | 0 |
   | Power save | wraps the request in `WifiPowerSaveGuard` | none |

   The two requests can therefore disagree about the same endpoint.
2. **The spine breaks its own dependency rule.** "Arrows are the only allowed dependencies." Yet the diff adds an edge from upstream `src/network/OtaUpdater.cpp` to `src/games/`, and an edge from `src/games` to the SDK's `SecureHttpClient`. The layer table allows `src/games` "HAL, Storage, ZipFile, PngToBmpConverter, ESP-NOW, mbedTLS", and not an HTTP client. Neither edge is in the table or the diagram.
3. **The new dry-run trigger cannot see the drift that matters.** The dry run builds images and checks tag, URL, and board strings. An upstream merge could make `fetchUrl` return `true` on 404 with an empty body, or add a header such as an `Accept` or auth token that GitHub now needs, or change the UA. Every such merge passes the dry run. Devices then report `JSON_PARSE_ERROR` instead of `NO_UPDATE`, or the probe alone fails.

   A related gap: `FREEINK_NET_WOLFSSL=1` sits in `[base]` build_flags (`platformio.ini:73`), not in a `*-gh_release` env. An upstream change there moves `HttpDownloader` to `esp_http_client`, and the probe then fails at connect. The dry run is not triggered at all.

**Pair 3a: a later epic needs HTTP.**
- Unit A, a later install-launcher ticket, wants to fetch first-party `.cpgame` assets from the fork release (Operational envelope, "Game delivery"). The probe is now the precedent, so it writes a second `SecureHttpClient` user in `src/games` with its own UA and timeout.
- Unit B is the probe itself.
- Both obey the letter of AD-25, which constrains only what the probe does, not who else may make HTTP requests. The result is three transport configurations and three UA literals. Release check V3 depends on the UA string tail-merging the version, so the UA literal is load-bearing.

**Proposed rule (AD-25, Update source, and the layer table):**

> `ForkReleaseProbe` is the only fork code that makes an HTTP request in v1, and the only `src/games` unit that may include `SecureHttpClient`. It takes its URL only from `ForkRelease::LATEST_RELEASE_URL`. It sets the same timeout, user agent, TLS mode, and redirect limit as `HttpDownloader`'s release fetch, and `docs/crosshatch/upstream-touches.md` records those four values next to row 10. An upstream merge that touches `src/network/HttpDownloader.*` or `[base]` `build_flags` re-checks those values and HttpDownloader's non-200 handling against the probe in the same merge. The dry run alone does not satisfy it. The layer table gains the edges `upstream OtaUpdater.cpp (row 10 only) → lib/GameCore/ForkRelease.h, src/games/ForkReleaseProbe.h` and `src/games/ForkReleaseProbe → SecureHttpClient`, and the diagram shows them.

Fix the false "Same transport settings" comment, or the settings themselves, in the AI-1/AI-4 chore.

---

## H4 [Medium-High] The tag grammar has four spellings and no named owner, and `N` has two readings

**Changed text (Seed):** "`fork_version_vectors.json`: the AD-25 tag grammar and cases, read by the C++ suite and `fork_release.py`."

**Unchanged text (AD-25):** "Tags and the firmware parser share one grammar, `^(0|[1-9]\d*)\.(0|[1-9]\d*)…$`… fixed by shared test vectors" and "sets `N` to one more than the largest `-ch.N` in any tag". The rule claims to prevent "two owners, or two readings, of the fork build number".

The grammar exists in four places:

| Where | Form |
| --- | --- |
| spine AD-25 | a regex with `\d` |
| `fork_version_vectors.json` `tag_grammar` | POSIX `[0-9]` |
| `ForkRelease.h:12` | a comment regex |
| `ForkRelease.h` `matchTagPrefix` | a hand-written matcher |

The Seed says the vector file is "read by the C++ suite". The firmware parser does not read it; the suite only checks agreement (`GrammarAgreesWithParser`). Nothing says which of the four is normative.

**Pair 4a: a new implementation copied from the spine.**
- Unit A, a future install-launcher or web tooling ticket that shows or validates a fork version, copies the regex from the spine, because the spine is what sessions read. In Python, `\d` matches any Unicode decimal digit (`'١'`), and with `re.match` a `$` accepts a trailing `\n`. The vector file's own description warns about this; the spine does not.
- Unit B, the firmware's `matchTagPrefix`, accepts ASCII only.
- The two readings disagree on input that both obey "the one grammar".

**Pair 4b: two readings of `N` inside one script.**
- `fork_release.py:60,316-320` computes the next `N` with `-ch\.([0-9]+)` over **any** tag. That is exactly what AD-25's sentence says, and it is a looser reading than the grammar.
- `fork_release.py:105` validates the new tag against the grammar, capped at 9 digits.
- Incompatibility: one stray `*-ch.*` tag with a 10-digit number, created by an admin bypass that AD-25's Tags bullet permits, makes every later next-`N` fail the grammar. The ruleset forbids deleting `*-ch.*` tags, and `N` never resets, so releases stay blocked for good.

**Pair 4c: two commits (retro P5, deferred).** `fork_release.py` reads the vectors from the tools checkout, while the firmware comes from `inputs.ref`. The Seed now names the file as the shared source but does not say which commit's copy counts. A grammar change followed by a rollback release produces a tag that the older firmware parses as `N = 0`. That firmware is then re-offered its own release forever, which AD-25 exists to prevent.

**Proposed rule (AD-25, Version):**

> `tag_grammar` and `max_tag_length` in `test/game_core/fork_version_vectors.json` are the normative grammar; the spine regex above is a copy written with `[0-9]`. `ForkRelease.h` is the only firmware implementation and must pass every vector. Python code uses only `re.fullmatch` with the file's grammar. Every reading of `N` goes through the grammar. The next `N` is one more than the largest `N` among tags that match it, and preflight fails if any `*-ch.*` tag does not match. The release reads the vector file from the commit it releases, and preflight fails if that copy differs from the tools commit's. The asset-name capacity (48, in `OtaUpdater.cpp:56` and `fork_release.py:62`) moves into `ForkRelease.h` as a constant, with a matching vector-file field, so that an upstream merge that changes the buffer cannot strand devices unseen.

---

## H5 [Medium] "KB here is KiB" makes every other KB in the spine ambiguous

**Changed text (Flash budget row):** "KB here is KiB: the limit is 256,000 B…"

Scoping the definition to "here" implies that the spine's other KB values may be decimal. Those values are byte limits that C++ and Python must enforce identically:

- AD-10: `ch.store` at most 4 KB.
- AD-15: package at most 256 KB, member at most 128 KB, images at most 128 KB.
- AD-6: arena 256 KB.
- AD-7: frame buffer 32 KB.
- Operational envelope: lobby threshold 100 KB.

**Pair 5a: the packer and the installer.**
- Unit A, `scripts/pack_game.py`, enforces 256 × 1000 B.
- Unit B, `GamePackageInstaller`, enforces 256 × 1024 B. That direction is benign.
- Flip the two readings, and a package that `pack_game.py` accepts is `Invalid` on the device.

**Pair 5b: the two codecs.** The C codec rejects a `ch.store` over 4,096 B, and `game_codec.py` rejects one over 4,000 B. The golden vectors pass unless someone writes a boundary case, because no rule says one is needed.

**Proposed rule (Consistency Conventions, new row "Units"):**

> Every KB in this spine is KiB (1,024 B). Each byte limit in AD-6, AD-7, AD-10, and AD-15 is one named constant in `lib/GameCore` (or `GameScript` for codec limits), mirrored in the vector files the Python tools read, with golden vectors at limit and limit + 1.

Then drop "here" from the Flash budget row.

---

## H6 [Medium] AD-4's "Lua units only" define pattern is unsafe for any define that changes a public header

**Changed text (AD-4):** "`library.json` passes `-DLUA_COMPAT_GLOBAL=0` to the Lua units only; that is the one compat define, and `luaconf.h` stays unedited."

This define is safe because it is read only in `llex.c:191` and `lparser.c:2117`. The sentence sets a precedent: configure Lua through `library.json` `flags`, which PlatformIO applies to the library's own sources and which `test/game_script/CMakeLists.txt` mirrors as `PRIVATE`. It is silent about defines that `lua.h` and `lauxlib.h` also read.

**Pair 6a: the budget ticket and the error-view ticket.**
- Unit A, a script-runtime budget ticket, adds `-DLUAI_MAXCCALLS=…` and `-DLUA_IDSIZE=40` to `library.json` `flags` to fit the 16 KB `GameVM` stack and to trim `lua_Debug`. It obeys AD-4: Lua units only, not a compat define, `luaconf.h` unedited.
- Unit B, the AD-14 error-view formatter in `lib/GameScript`, calls `lua_getinfo` into a `lua_Debug` compiled with the default `LUA_IDSIZE`, which is 60.
- Incompatibility: the two sides disagree on the size of `lua_Debug.short_src`. The Lua side writes 60 bytes into a 40-byte field, which is stack corruption on the VM task. The `static_assert(sizeof(lua_Integer) == 8)` guard does not catch it.

**Proposed rule (AD-4):**

> `library.json` `flags` may set only macros that no public Lua header reads (today `LUA_COMPAT_GLOBAL` and `LUAI_*` internals). A macro that `lua.h`, `lauxlib.h`, or `lualib.h` reads (`LUA_IDSIZE`, `LUA_32BITS`, `LUA_INT_*`, `LUA_FLOAT_*`, `LUA_EXTRASPACE`, `LUAI_MAXALIGN`, `LUA_COMPAT_APIINTCASTS`) is not set in v1. Each C++ file that includes `lua.h` asserts the defaults it depends on beside the `lua_Integer` assert.

---

## H7 [Medium] Fork script conventions are stated in the Seed but have no single owner

**Changed text (Seed):**
- "(each fork script has a sidecar `<name>_test.py` run on every PR)";
- "`check_upstream_touches.py` … exit 0 pass, 1 fail, 2 could not run";
- CI row: "fork-only jobs in `crosshatch-ci.yml` … run the fork script tests".

As built, the sidecars are named one by one inside two unrelated jobs: `fork_release_test.py` runs in the *ledger* job, and `check_flash_budget_test.py` in the *flash* job. There is no "fork script tests" job. `docs/crosshatch/upstream-touches.md` lists each script in Game paths by name. Retro A1 counts `SetupError` and the exit contract defined three times and two incompatible git helpers.

**Pair 7a: sidecars that never run.**
- Unit A, script-runtime, adds `scripts/game_codec.py` and `game_codec_test.py`, which carries the Python half of AD-10's golden vectors. The Seed says sidecars "run on every PR", so it adds nothing to CI.
- Unit B, the icon epic, adds `gen_game_icons.py` and its sidecar, and wires the sidecar into the flash job, following `check_flash_budget_test.py`.
- Incompatibility: AD-10's Python-side golden vectors never run in CI, and nothing fails. The sidecar files are also not Game paths.

**Pair 7b: the shared helper.** Units A and B each create their own helper for the 0/1/2 contract, or copy it from different scripts: bytes-and-raise from `check_upstream_touches.py`, text-and-return from `fork_release.py`. AI-5 expects one helper, but the Seed does not name it, so both helpers are equally valid.

**Proposed rule (Seed and Consistency Conventions):**

> Fork Python scripts share `scripts/crosshatch_tools.py`, which holds the 0/1/2 exit contract, `SetupError`, the git helper, and the games-flag literal. Every fork script has `scripts/<name>_test.py`. One `fork-script-tests` job in `crosshatch-ci.yml`, listed in `Crosshatch Test Status`'s `needs`, runs every `scripts/*_test.py` by glob. Game paths gain `scripts/*_test.py` and `scripts/crosshatch_tools.py`.

---

## H8 [Low-Medium] The spine and `docs/crosshatch/upstream-touches.md` now disagree about the ledger

AD-3 says "The ledger lives in `docs/crosshatch/upstream-touches.md`". The diff changes row 10 and the dry-run trigger list in the spine only. The doc's row 10 still says "calls into `ForkRelease.h`…" with no probe, and its dry-run list still omits `HttpDownloader.*` (AI-4 not yet applied).

**Pair 8a:** a later session edits `OtaUpdater.cpp` and adds a call into a second `src/games` helper. The review reads the spine's row 10, which already permits calling into `src/games`. The ledger CI job reads the doc, which checks paths only. Each reviewer works from a different ledger.

**Proposed rule (AD-3):**

> The doc is the ledger. The spine table is a copy, and every change to one is made to both in the same PR.

Apply the row-10 and `HttpDownloader.*` wording to the doc now.

---

## H9 [Low] The flash budget's "upstream growth never counts" is false for guarded ledger rows

**Changed row (Flash budget):** it now includes the KiB definition, and it still claims "measures it as … `FREEINK_CAP_GAMES` on minus … off, so upstream growth never counts against it".

The retro addendum measured the games-on image as 15,136 B **smaller** than games-off, because row 10's `#else` branch (upstream's `isUpdateNewer` with `sscanf`) exists only in the off build. The budget also covers AD-25 code that the row's list ("Lua, GameCore, GameScript, GameIcons, screens") leaves out.

**Pair 9a:**
- Unit A, script-runtime, lands Lua at +250 KiB − ε on top of today's −14.8 KiB credit, and the gate is green.
- Unit B, an upstream merge that simplifies `isUpdateNewer` in a guarded `#else`, removes the credit. The next unrelated fork PR goes red.
- Upstream growth has counted against the budget.

**Proposed rule (Flash budget row):**

> The budget covers every difference `FREEINK_CAP_GAMES` makes, including ledger rows' `#else` branches. Each epic's first ticket that links new game code records the measured difference in its plan (AI-9). An upstream merge that touches a guarded ledger row re-runs the gate.

---

## Rule text to add, consolidated

1. **AD-2 (H1):**
   - No dynamically initialized static objects anywhere in game code.
   - No static storage over 64 B in any memory region anywhere in game code; per-match allocation instead.
   - Read-only tables are `inline constexpr` or `static constexpr`.
   - `GameIcons.generated.h` is un-ignored and excluded from formatting.
   - A `.data` + `.bss` delta of at most 1 KiB, checked by the budget job.
2. **AD-2 / AD-25 (H2):**
   - Device-only files are guarded with `#if FREEINK_CAP_GAMES && !defined(SIMULATOR)`, never excluded by filename.
   - `hostCaps.nearby` is false under `SIMULATOR`.
   - A `simulator_x4pro` CI job.
3. **AD-25 and the layer table (H3):**
   - The probe is the only HTTP user in v1, and its URL comes from `ForkRelease.h`.
   - The four transport values are recorded in the ledger doc and re-checked on any `HttpDownloader.*` or `[base]` `build_flags` merge.
   - New dependency edges drawn.
4. **AD-25 Version (H4):**
   - The vector file's grammar is normative.
   - The spine uses `[0-9]`, and Python uses `re.fullmatch`.
   - The next `N` comes from grammar-matching tags only, and preflight fails on a non-matching `*-ch.*` tag.
   - The vectors are read from the released commit.
   - The asset-name capacity lives in `ForkRelease.h`.
5. **Conventions (H5):** every KB is KiB, and limits are single named constants with boundary vectors.
6. **AD-4 (H6):** no `library.json` define that a public Lua header reads.
7. **Seed / Conventions (H7):** one shared helper for fork scripts, and one glob-driven fork-script-tests job.
8. **AD-3 (H8):** the doc is the ledger; sync it now.
9. **Flash budget (H9):** the budget includes guarded `#else` branches; record the delta per epic.

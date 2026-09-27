# Review: AD-19 versioning and retro1 H1-H9 spine update (verification lens)

- Scope: the uncommitted diff to `ARCHITECTURE-SPINE.md` (changed lines only), plus the matching `.memlog.md` decisions and the `game-api-seed.md` line, against HEAD `f89a6c95`.
- Lens: is every changed claim confirmed against the repository, the toolchain, or the web today, rather than asserted? Which new rules are ahead of the code?
- Date: 2026-09-27

## Verdict

**Pass with fixes.** The facts the diff restates from the code hold: the grammar, the 25-character cap, the vector-driven tests, the `LUA_COMPAT_GLOBAL` scope, the simulator guard, the −15,136 B delta, and the probe's use of `SecureHttpClient`. Three changed rules do not survive a check:

1. As worded, the AD-2 static-storage cap forbids the two constexpr tables the same rule names as allowed.
2. The new `.data` + `.bss` gate cannot be measured by the current `firmware.bin` comparison.
3. "The release reads the vectors from the commit it releases" reverses a documented choice in `crosshatch-release.yml`.

Several other rules are new obligations that no code meets yet. They are listed separately at the end for the owner's follow-up.

## Verified (no action)

| Changed claim | Evidence |
| --- | --- |
| The spine's grammar is exactly `tag_grammar`, max 25 | Programmatic compare of spine line 358 with `test/game_core/fork_version_vectors.json:3-4`: identical string; `max_tag_length` 25. Boundary vectors exist: `1234567.1234567.12-ch.123` (25, valid, `:15`) and `…123-ch.123` (26, invalid, `:26`). |
| Whole-string match; Python needs `re.fullmatch` | `re.match(grammar, '1.6.5-ch.7\n')` matches, `re.fullmatch` does not (run locally). `fork_release.py:104-105` uses `fullmatch`; the C++ test uses `std::regex_match` with `std::regex::extended` (`ForkReleaseTest.cpp:153,161`), so `[0-9]`/`[.]` is needed for POSIX ERE. |
| `ForkRelease.h` is tested against the vectors | `ForkReleaseTest.cpp:127-165` (`ConstantsMatchVectors`, `ValidTags`, `InvalidTags`, `GrammarAgreesWithParser`), `:167-185` (running, newer, assets). `ctest -R ForkRelease`: 10/10 pass. |
| `fork_release.py` is tested against the vectors | `fork_release_test.py:20,110-126` (grammar over `valid_tags`/`invalid_tags`, asset vectors). `python3 scripts/fork_release_test.py`: 50 tests OK. It runs on every PR (`crosshatch-ci.yml:53`). |
| AD-4: `LUA_COMPAT_GLOBAL` is lexer/parser only | Read only at `lib/lua/src/llex.c:191` and `lparser.c:2117`. `LexState.glbn` is declared unconditionally (`llex.h:79`), so internal layout does not change either. `luaconf.h:344-345` only supplies the default. `lua.h`, `lauxlib.h`, and `lualib.h` never read it. Includers see the value 1, which changes no type. |
| AD-2 simulator: `simulator.ini` excludes no game file | `.claude/skills/run-crosshatch-player/simulator.ini:10-16` excludes only four upstream `network/`/`platform/` files. `ForkReleaseProbe.cpp:3` has `#if FREEINK_CAP_GAMES && !defined(SIMULATOR)`. `crosshatch-ci.yml` `simulator-build` builds both envs and is in `Crosshatch Test Status` `needs`. |
| Current game code has no static RAM or dynamic init | `xtensa-esp32s3-elf-nm -S` and `objdump -h` over every x4pro object in `src/games/` and `lib/Game*/` show no `b/B/d/D` symbol and no `.init_array`/`.ctors`. `lib/lua` has one 8 B `.bss` (`ldump.c`). |
| Diagram edges `OTA → CORE`, `OTA → ADP`, `ADP → SecureHttpClient` | `src/network/OtaUpdater.cpp:21-28` (guarded includes of `ForkRelease.h` and `games/ForkReleaseProbe.h`). `SecureHttpClient` is `freeink-sdk/libs/network/SecureNet/include/SecureHttpClient.h`. Among fork code, only `src/games/ForkReleaseProbe.cpp` includes it. The URL comes only from `ForkRelease::LATEST_RELEASE_URL` (`:30`). |
| Layer diagram renders | Parsed with `mermaid.parse` (mermaid 12.0.0 + jsdom): `flowchart-v2` OK. Node ids `ACT ADP CORE SCR ICO LUA STD HAL OTA HTTP` are unique. A malformed control input fails, so the parser is really checking. |
| −15,136 B at d578b4e3 | `epic-platform-baseline-retrospective.md:222` (on 5,647,488 / off 5,662,624). The local `.pio/build/x4pro/firmware.bin` is 5,647,488 B. |
| 250 KiB = 256,000 B; icons ≈ 40 KiB | 250 × 1,024 = 256,000. 64 × (32²/8 + 64²/8) = 40,960 B = 40 KiB exactly. |
| Fork scripts have sidecar tests run on every PR | `check_upstream_touches_test.py`, `fork_release_test.py`, `check_flash_budget_test.py`; `crosshatch-ci.yml:48,53,82`. |

## Findings

### F1. AD-2's 64 B cap now forbids the constexpr tables it names (medium)

- **Spine:** line 71: "no static storage over 64 B in any memory region … Read-only tables are `inline constexpr` or `static constexpr` (generated icon data, `ForkRelease::LATEST_RELEASE_URL`)".
- **Evidence:** the old text said "`constexpr` data, which stays in flash, is exempt". The new text drops that exemption and widens the cap to "any memory region". Constexpr objects have static storage duration. `ForkRelease::LATEST_RELEASE_URL` is 74 B (`nm`: `3c317b80 0000004a V`, DROM/flash rodata; `lib/GameCore/ForkRelease.h:27`). The icon tables will be about 40 KiB. Read literally, both examples break the rule they illustrate.
- **Fix:** "no *mutable* static storage over 64 B in any RAM region (DRAM `.dram0.*`/`.noinit`, IRAM, RTC, PSRAM `.ext_ram.bss`); read-only `inline constexpr` / `static constexpr` tables in flash rodata are exempt."

### F2. The `.data` + `.bss` gate cannot be measured from `firmware.bin` (medium)

- **Spine:** line 71: "The flash budget job also fails when the x4pro `.data` + `.bss` grows more than 1 KiB with games on"; line 501: "the gate compares `firmware.bin` sizes".
- **Evidence:**
  - `scripts/check_flash_budget.py:109-111,168+` reads only `firmware.bin` sizes. The image holds `.dram0.data` inside the image total and holds no `.bss`, so a `.bss` change is invisible to it.
  - Berkeley `xtensa-esp32s3-elf-size -B firmware.elf` is unusable on ESP32-S3: it reports data 3,420,023 and bss 7,879,978 because of the `.flash_rodata_dummy` and `.ext_ram.dummy` sections.
  - The meaningful figure is section-level. PlatformIO's `SIZEDATAREGEXP` is `.dram0.data|.dram0.bss|.noinit` (`~/.platformio/platforms/espressif32/builder/main.py:933`). x4pro today: 29,615 + 72,200 + 1 = 101,816 B, which matches the spike baseline (`spike-script-engine-2026-09-26.md:90`).
  - Headroom is not obvious. The spike measured Lua-only at +560 B static RAM (464 B engine-exclusive). ESP-NOW's own statics are about 75 B (`libespnow.a`: 11 data + 64 bss). That leaves about 400-500 B for GameCore, GameScript, screens, and libc pulls.
- **Fix:**
  - Name the sections in the spine, for example "`.dram0.data` + `.dram0.bss` + `.noinit` (plus `.iram0.data/.bss`, `.rtc*` data/bss, `.ext_ram.bss` if present)".
  - State that the gate uses games-on minus games-off, with the same `#else` caveat as H9.
  - Have `check_flash_budget.py` read `firmware.elf`, whose path comes from the metadata `prog_path` it already loads. It can use `<toolchain>-size -A` (derived from the metadata `cc_path`), or `esp_idf_size` on `firmware.map` (`esp-idf-size>=2.0.0` is already installed in the flash job, `crosshatch-ci.yml:99`).
  - Make 1 KiB a named constant with at-limit and one-over tests.

### F3. "The release reads the vectors from the commit it releases" contradicts the workflow (medium)

- **Spine:** line 358.
- **Evidence:** `.github/workflows/crosshatch-release.yml:51-53` states the reverse on purpose: "The script and the version vectors come from this workflow's commit; the firmware from the released commit, which may be an older one." `fork_release.py:50-51` sets `DEFAULT_VECTORS = TOOLS_DIR/…`, and no step passes `--vectors`. `Test the release script` (`:66`) tests the *tools* vectors. The recheck and expected-assets job checks out only `tools` (`:178-181`).
- **Fix:**
  - Keep the rule only as a deliberate reversal, recorded in the memlog.
  - Pass `--vectors src/test/game_core/fork_version_vectors.json` to `prepare` and `check-images`, and carry the path in the plan for later jobs.
  - Say what happens when the released commit predates a vectors field: a setup failure (exit 2) is acceptable if the spine says so.
  - Otherwise, reword the rule to "the workflow's commit" and drop the claim.

### F4. The asset-name capacity is owned by upstream and is not yet a `ForkRelease.h` constant or a vector (medium)

- **Spine:** line 358: "The asset-name buffer capacity is a `ForkRelease.h` constant mirrored in the vectors."
- **Evidence:**
  - `ForkRelease.h:30-31` only mentions "48-byte asset-name buffers" in a comment. Python hard-codes `ASSET_NAME_BUFFER = 48` (`fork_release.py:61-62`), and the C++ test uses the literal 48 (`ForkReleaseTest.cpp:195-200`). The vectors have no such field.
  - The real buffers are upstream's: `char assetName[48]` in `OtaUpdater.cpp:56` sits outside the guards (`upstream/develop` line 43), and `ReleaseJsonParser.h:68,72` has `currentAssetName[48]` and `firmwareAssetName[48]` in an unledgered file. `ReleaseJsonParser.h:62` `tagName[32]` also caps the tag, which `MAX_TAG_LEN` 25 satisfies, but nothing records that.
  - A fork constant can therefore only *mirror* upstream.
  - The longest valid vector name is 47 B. There is no one-over (48 B) vector, and none is possible with the current boards unless a board name of 7+ characters is used.
- **Fix:**
  - Reword to "mirrors upstream's 48-byte buffers".
  - Add `ASSET_NAME_CAPACITY = 48` to `ForkRelease.h` and `asset_name_capacity` to the vectors, and have both tests read it.
  - Add a `static_assert(sizeof(assetName) == ForkRelease::ASSET_NAME_CAPACITY)` inside row 10's guard.
  - Add an asset vector with a 7-character board whose name is 48 B and expects `""`.

### F5. The probe's transport values are not recorded, the recording will contradict the probe's comment, and the re-check trigger misses the SDK (medium)

- **Spine:** line 360: "Its timeout, redirect limit, TLS mode, and user agent are recorded beside ledger row 10 … an upstream merge touching `HttpDownloader.*` or `[base]` `build_flags` re-checks them."
- **Evidence:**
  - `docs/crosshatch/upstream-touches.md` row 10 and its note record none of the four values, and its trigger list has no `[base] build_flags`.
  - `ForkReleaseProbe.cpp:22` says "Same transport settings as HttpDownloader's release fetch", but it sets only `setInsecure`, `setReuse(false)`, and the user agent.
  - The timeout is the SDK default of 15,000 ms (`SecureHttpClient.h:614`), and redirects default to 0 hops (`:605`). `HttpDownloader` uses 60,000 ms (`HttpDownloader.cpp:33,74`) and 5 redirects (`:35`, manual loop).
  - Two of the four values are SDK defaults, so an upstream merge that moves `freeink-sdk` can change them without touching either listed trigger.
- **Fix:**
  - Record 15,000 ms (SDK default), 0 redirects, `setInsecure` over wolfSSL (`FREEINK_NET_WOLFSSL=1`, `platformio.ini:73` in `[base]`), and UA `CrossPoint-ESP32-<CROSSPOINT_VERSION>`.
  - Add the `freeink-sdk` pointer (or `SecureHttpClient.h`) to the triggers.
  - Correct the probe comment, or set the values explicitly so they stop depending on SDK defaults.

### F6. The Units rule relabels a decimal measurement, and the seed is outside it (low-medium)

- **Spine:** line 379 (Units), line 501: "Lua alone measured +124 KB".
- **Evidence:**
  - The source is +124,232 B (`spike-script-engine-2026-09-26.md:90`): 124.2 kB, which is 121.3 KiB. Under the new rule, "124 KB" reads as 126,976 B. The previous verify review (`review-retro1-verify.md`) raised the same point, and it is still open.
  - `game-api-seed.md:31,126,196` (256 KB package, 128 KB per file, 4 KB `ch.store`, 256 KB Lua memory) is edited in this diff but is not "this spine". AI authors will read it without the KiB definition.
  - "Each limit is one named constant": the flash limit is defined twice, as `DEFAULT_LIMIT_KIB = 250` (`check_flash_budget.py:43`) and `FLASH_BUDGET_KIB: 250` (`crosshatch-ci.yml:68`).
  - No conflicts: ESP-NOW 1,470 B, frame 1,400 B, and move 256 B are already in bytes; 250 KiB (256,000 B) and the icons figure are consistent.
- **Fix:**
  - "+121 KiB (124,232 B)".
  - Write limits in the seed and in `game-api.md` as KiB with bytes, for example "256 KiB (262,144 bytes)". `pack_game.py` will enforce 262,144, not 256,000.
  - Let CI pass no limit, or have the script own the only copy.

### F7. The diagram sentence forbids ledger rows 4-9 (low-medium)

- **Spine:** lines 54-56 and 59: "Arrows are the only allowed dependencies; upstream code reaches game code only through ledgered, guarded rows (AD-3)."
- **Evidence:** only row 10 has an arrow. Rows 4-7 (`ActivityManager.*`, `HomeActivity.*` → `src/activities/games`) and row 9 (`CoverGridHomeUi.cpp` → `GameIcons` bitmap; `upstream-touches.md` ledger) are ledgered upstream→game dependencies with no arrow, so by the first clause they are forbidden.
- **Fix:** add one node, `UP["upstream: ledgered rows 4-10 (guarded)"]`, with arrows to `ACT`, `ICO`, `CORE`, and `ADP`. Or say "the diagram shows row 10; every ledger row is an allowed upstream→game edge".

### F8. AD-19 freeze enforcement is not mechanically possible as worded (low-medium)

- **Spine:** line 281: "once it is true any change to that level's list fails."
- **Evidence:** the only check described is a host test that compares the list with the bindings. A PR that changes both the list and the bindings passes it. Nothing pins the frozen content.
- **Fix:** a CI check that diffs `docs/crosshatch/api-level-<n>.txt` against the release tag that froze it (tags are protected per AD-25). Or pin a digest in a place that PR review treats as frozen, and say which.

### F9. Smaller wording issues on changed lines (low)

- **AD-19, line 277:** "shows a package whose `api` exceeds the host's as unavailable". It now also applies below `hostCaps.minApi` (line 236 says so); make 277 match or point to 236.
- **AD-25, line 360:** "the only fork code that makes HTTP requests". Row 10's guarded code in `OtaUpdater.cpp` also points upstream's `HttpDownloader` fetch at the fork URL (`OtaUpdater.cpp` guarded `latestReleaseUrl`). Say "the only fork-owned file that opens its own HTTP client".
- **AD-2, line 71:** "a larger buffer is allocated per match by its AD-20 owner". Non-match game code (probe, installer, launcher) has no AD-20 owner, and the probe heap-allocates per call (`ForkReleaseProbe.cpp:18-23`). Add "or per call by its caller".
- **AD-2, line 71:** "grows more than 1 KiB with games on". Say "games-on minus games-off" (see F2).
- **Memlog / AD-2 / AD-24:** the memlog H1 decision includes un-ignoring `GameIcons.generated.h`, but the spine does not state it, and `.gitignore:12` `*.generated.h` still ignores the file (`git check-ignore -v`). AD-24 line 347 says it is committed. See follow-up 6 for the formatting consequence.

### Outside the changed lines (for information)

- The sequence diagram at spine line 438 does not parse in mermaid 12.0.0. The `;` in `peer → seat 2; shipped turn == 2? …` ends the statement. Replacing it with `,` parses. It predates this diff.

## Rule-ahead-of-code follow-ups

Each item is a changed spine rule that the code or docs do not meet yet.

1. **Vectors from the released commit (AD-25, line 358).** In `crosshatch-release.yml`, pass `--vectors` from the `src` checkout to `fork_release.py prepare` and `check-images`, and carry it through the plan to the recheck and expected-assets job. Fix the workflow comment at `:51-52`. Decide whether an older commit without the vectors file fails with exit 2. (F3)
2. **Asset-name capacity (AD-25, line 358).**
   - Add `ForkRelease::ASSET_NAME_CAPACITY` (48) and a vectors field `asset_name_capacity`.
   - `ConstantsMatchVectors` and `fork_release.py` read it, replacing `ASSET_NAME_BUFFER` and the test literal 48.
   - Add a guarded `static_assert` on `sizeof(assetName)` in `OtaUpdater.cpp` row 10.
   - Add a 48-byte one-over asset vector.
   - Note `ReleaseJsonParser` `tagName[32]` ≥ `MAX_TAG_LEN`+1. (F4)
3. **Probe transport record (AD-25, line 360).**
   - Add timeout 15,000 ms (SDK default), redirects 0, TLS `setInsecure` over wolfSSL, and UA beside row 10 in `docs/crosshatch/upstream-touches.md`.
   - Add `[base] build_flags` and the `freeink-sdk` pointer / `SecureHttpClient.h` to the row-10 re-check triggers.
   - Fix or align the `ForkReleaseProbe.cpp:22` "same transport settings" comment. (F5)
4. **Static RAM gate (AD-2, line 71).** Extend `check_flash_budget.py` (and `_test.py`) to measure named RAM sections from `firmware.elf`/`firmware.map` for on and off, with a 1 KiB named limit and at-limit and one-over tests. Wire it into the flash job. Run it once from a fresh clone per AGENTS.md. (F2)
5. **Static-storage enforcement (AD-2, line 71).** No check exists. Add a CI step over the x4pro objects of `lib/Game*`, `src/games`, and `src/activities/games` that fails on any `.init_array`/`.ctors` section, or on any `b/B/d/D` symbol over 64 B (after F1's wording fix, exempting flash rodata). The code passes today.
6. **Committed `GameIcons.generated.h` (AD-2 memlog, AD-24).**
   - Add `!lib/GameIcons/GameIcons.generated.h` to `.gitignore` (allowlisted).
   - Once tracked, `bin/clang-format-fix` (an upstream file, unledgered, so no exclusion can be added) and the formatting CI will format it. Add `lib/GameIcons/.clang-format` with `DisableFormat: true`, as `lib/lua` has, or make the generator output clang-format-clean.
   - Update AGENTS.md's "`*.generated.h` … every `pio run` regenerates them".
7. **`lib/GameCore/ApiLevel.h` (AD-19).** Create it with `API_LEVEL = 1` and `API_LEVEL_FROZEN = false`. `fork_release.py notes` must print "Game API 1 (preview)", and `pack-games` must refuse `api > API_LEVEL`. Both read `ApiLevel.h` from the **src** (released) checkout, not `tools`. Neither exists today (`fork_release.py` has no `ApiLevel`/`Game API` reference).
8. **`docs/crosshatch/api-level-1.txt` and host test (AD-19).** From epic-script-runtime on, plus a freeze mechanism that actually detects a change (F8).
9. **`hostCaps.minApi` and `hostCaps.nearby = false` under `SIMULATOR` (AD-2, AD-19).** In `Manifest::check`, which is not implemented yet. Add both to the manifest and launcher tickets.
10. **Fork scripts helper module (Conventions, line 380).** It does not exist. `check_upstream_touches.py`, `check_flash_budget.py` (`:48`), and `fork_release.py` each define their own `SetupError` and git/summary code. Create it before `pack_game.py`, `game_codec.py`, or `gen_game_icons.py` lands, and give it a sidecar test. Add it, plus `check_flash_budget.py` and `fork_release.py`, which are missing today, to the **Game paths** in `upstream-touches.md`.
11. **Units constants (Conventions, line 379).** Every Python-enforced limit (package 262,144 B, member 131,072 B, images 131,072 B, 32 members, …) is one named constant mirrored in a vector file with at-limit and one-over cases, when `pack_game.py` lands. Define the flash limit once (the script, or CI, not both).
12. **Per-epic delta record (line 501).** Each epic's retro or closing ticket records the games-on minus games-off flash delta, and the RAM delta once F2 lands. epic-script-runtime owes the first real engine figure (retro AI-9).
13. **Doc-only text fixes.** "+124 KB" becomes "+121 KiB (124,232 B)". The seed and `game-api.md` state KiB with bytes (F6). Update the diagram for rows 4-9 (F7).

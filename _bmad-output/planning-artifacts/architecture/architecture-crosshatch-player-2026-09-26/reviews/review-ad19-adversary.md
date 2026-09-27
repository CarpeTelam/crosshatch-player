# Adversarial review: AD-19 game API versioning and the retro1 H1-H9 answers

Reviewed 2026-09-27. Scope: commit `f89a6c95` ("docs: version the game API by frozen cumulative levels (AD-19)"), which is the diff in `spine-update2.diff`. It changes AD-2, AD-4, AD-15, AD-19, AD-25, the layer table and diagram, the Consistency Conventions (units, fork scripts), the Seed, the Flash budget row, and the `LUA_32BITS` Deferred row, plus one row of `game-api-seed.md`. It is checked against:

- the newest `.memlog.md` entries (lines 108-119);
- `reviews/review-retro1-adversary.md` (H1-H9);
- the untouched spine text (AD-10, AD-13, AD-15, AD-24, Deferred);
- `game-api-seed.md`;
- `specs/spec-crosshatch-player/glossary.md`;
- the eight epic files and `tickets.toml` in `initiative-crosshatch-player-v1/`;
- the as-built code: `lib/GameCore/ForkRelease.h`, `src/games/*`, `lib/lua/library.json` and headers, `scripts/fork_release.py`, `.github/workflows/crosshatch-{ci,release}.yml`, `.gitignore`, `bin/clang-format-fix`, and `docs/crosshatch/upstream-touches.md`.

Lens: for each hole, two units one level down that each obey every AD to the letter and still build incompatibly. The units are the epics script-runtime, icon-library, install-and-launcher, pass-and-play, play-nearby, game-api-docs, and first-party-games, plus the release workflow.

## Verdict

**Not ready to bind epic-script-runtime. The direction of AD-19 is right, but four of its mechanisms cannot yet be built consistently. Close H1-H4 before the script-runtime session starts, because that epic creates `ApiLevel.h`, `ch.api`, and the surface test. H5-H9 can be closed in the same edit.**

AD-19 now states the right policy: levels are cumulative, level 1 is a preview until v1, then it is frozen and new names go to the next level. Four of the mechanisms that carry that policy fail as written:

- **Play Nearby is blind to the preview.** Two devices at "level 1" on different builds match, then one device fails mid-match (H1).
- **The freeze has no single trigger.** The spine, the seed, and the game-api-docs epic name three different freeze moments. No workflow or test can detect the spine's moment, and a single `API_LEVEL_FROZEN` bool cannot say which level is frozen (H2).
- **The surface list is too narrow.** It holds names only, so most of what a level promises can change after the freeze and the test stays green. Four documents list the same names and nothing checks three of them (H3).
- **`hostCaps` is used in three ADs and defined in none.** Its fields, who builds it, and how it relates to `ApiLevel.h` are all unstated (H4).

The retro1 answers (AD-2, AD-4, AD-25, units, fork scripts, envelope) mostly land. Two of them cut the proposed rule text in ways that reopen the hole they close:

- The AD-2 "any memory region" wording forbids the `constexpr` data the next sentence names as allowed, and nothing enforces "dynamic initialization at any scope" (H6).
- AD-4's "type or struct layout" still lets through a define that changes what games see (H8).

Nothing already built conflicts with the new AD-2 rule except the 74 B `ForkRelease::LATEST_RELEASE_URL`, and only under a literal reading (H6).

---

## H1 [High] Play Nearby matches two "level 1" devices whose level 1 differs

**AD text both units obey:**
- AD-13: "Matching requires equal `proto` (which includes the codec version) and equal package hash. The firmware `api` level is not compared."
- AD-19: "Level 1 is a preview until initiative v1 closes: it may still grow, and a game written against it may break between builds."
- AD-19: "`ch.api` … read[s]" `API_LEVEL`.
- AD-15: `Unavailable` covers only "`api` above `hostCaps.api` or below `hostCaps.minApi`".

**Pair 1a: preview growth, the same package on two builds.**
- **Unit A (icon-library, or any later preview addition):** adds `ch.gfx.circle` (a *(draft)* name in the seed) or the icons `ship` and `hit` to level 1 in build `-ch.9`. The surface list is updated with the code, as AD-19 requires. `proto` is unchanged, because the codec and the frames did not change.
- **Unit B (first-party-games):** Battleship declares `"api": 1` and draws `ship`. It is packed by the `-ch.9` release.
- **The incompatibility:**
  - Device X on `-ch.5` and device Y on `-ch.9` both install the same `.cpgame`, so the package hashes are equal. `Manifest::check` returns `Ok` on both (api 1 ≤ 1), `proto` is equal, and the lobby matches them.
  - The first `draw` on X that reaches `ch.gfx.icon("ship", …)` is a `ScriptError` (AD-14: "unknown icon … name"), and Y gets `ABORT(script_error)` several moves into the match.
  - Nothing in the lobby could have refused the pair. `ch.api` is `1` on both, so the game cannot feature-detect either.
- **Why it matters:** "Preview games may break" was accepted for a single device. For two devices it becomes a match that starts and then fails. That happens exactly while the three first-party games and the AI-authoring trial are being played across devices.

**Pair 1b: feature detection on the authority (after the freeze).**
- **Unit A (a post-v1 game, api 1):** follows the cumulative rule. In `setup` it writes `state.fancy = ch.api >= 2` and draws with a level-2 name when the flag is set.
- **Unit B (play-nearby):** matches a level-2 host with a level-1 guest. Both pass `Manifest::check` for an api-1 game, and AD-13 does not compare levels.
- **The incompatibility:** the authority decided the state from *its own* level, and the guest's `draw` calls a name it does not have, which is a `ScriptError`. AD-9 makes state authority-only, but nothing says which device's level the state may assume.

**Proposed rule (AD-13 and AD-19):**

> `ADVERT` and `JOIN` carry `hostApi u8` and `surface u32`, the CRC-32 of the sender's `docs/crosshatch/api-level-<API_LEVEL>.txt` as compiled into `ApiLevel.h`. When either device has `API_LEVEL_FROZEN` false, matching also requires equal `hostApi` and equal `surface`, and a mismatch shows "update both devices" (`ABORT(version_mismatch)`). After the freeze only `proto` and the package hash are compared, as today. `ctx.api` (AD-8) is the lowest `hostApi` in the roster. `setup` and `apply` may branch only on `ctx.api`, while `draw` and `input` may branch on `ch.api`. The API docs say so.

(Changing the `ADVERT` and `JOIN` payloads is a `proto` bump, which is free before epic-play-nearby ships.)

---

## H2 [High] The freeze is neither one event nor a checkable state

**AD text both units obey:**
- AD-19 (Freeze): "It freezes in the fork release that closes v1, after the first-party games epic."
- AD-19 (Surface list): "While `API_LEVEL_FROZEN` is false the list changes with the code; once it is true any change to that level's list fails."
- AD-19 (One constant): `ApiLevel.h` holds `API_LEVEL` and `API_LEVEL_FROZEN`.
- AD-25: the release workflow "rewrites [the version line] in its checkout only", and so commits nothing.

**Three different freeze moments:**

| Source | Freeze moment |
| --- | --- |
| Spine AD-19 | "the fork release that closes v1, after the first-party games epic" |
| `epic-game-api-docs.md` Done when 4 | "Merged to `develop`; API level 1 is frozen." Epic 7 in `tickets.toml`, *before* epic 8, first-party-games |
| `game-api-seed.md` | "a preview until crosshatch v1 is released"; line 13 also says "*(draft)* can change until the runtime ships" |

**Pair 2a: game-api-docs and first-party-games.**
- **Unit A (game-api-docs):** meets its Done when 4 by setting `API_LEVEL_FROZEN = true` at epic close. That obeys the spine, since only a *release* freezes and a develop commit is not a release.
- **Unit B (the release workflow):** a routine dispatch (AD-25 allows any develop ancestor) releases that commit. Its notes read "Game API 1" without "(preview)", so level 1 is now a released, frozen level.
- **Unit C (first-party-games, epic 8):** Battleship needs a `ship` icon. AD-19 says level 1 "may still grow" until the release after this epic, but the frozen list makes the surface test fail. Unit C has three options:
  - un-freeze the constant, which no rule forbids;
  - open level 2, which contradicts its own Done when 3 and `first-party-games.md` ("stays within API level 1");
  - draw the ship as a package image.
- Each option obeys some AD and breaks another unit's premise.

**Pair 2b: the constant cannot say which level is frozen.**
- After v1, level 2 opens: `API_LEVEL = 2` and `API_LEVEL_FROZEN = false`.
- **Unit A (the surface test from script-runtime):** keys "frozen" on `API_LEVEL_FROZEN`, as AD-19 says. So `api-level-1.txt` may now change again.
- **Unit B (the release workflow):** releases level 2 while `FROZEN` is false, with notes "Game API 2 (preview)". AD-19 says "From then on a released level never changes", but nothing requires `FROZEN` to be true in a post-v1 release.
- The result is that level 1 can be edited and level 2 is released as a preview, while every AD is obeyed.

**Pair 2c: "any change fails" has no baseline.**
- A host test sees one tree. It can compare the list with the code, but it cannot see that the list *changed*. A PR that edits `api-level-1.txt` and the bindings together passes the test.
- AD-19 names only the host test. No CI job compares against the base branch or the freezing tag. The tag is not known at freeze time, because the flip is a commit and the release comes later.

**Proposed rule (AD-19, Freeze):**

> - Levels below `API_LEVEL` are always frozen. `API_LEVEL_FROZEN` describes `API_LEVEL` only.
> - Level 1 freezes in the commit that sets `API_LEVEL_FROZEN = true`, which is the last ticket of epic-first-party-games. epic-game-api-docs documents level 1 but does not freeze it.
> - Once set, `API_LEVEL_FROZEN` never returns to false for that `API_LEVEL`.
> - The first release whose commit has it true is the freezing release. Its notes say "Game API 1", and earlier notes say "Game API 1 (preview)".
> - After v1, the release workflow's preflight fails when the released commit has `API_LEVEL_FROZEN` false, so each post-v1 level freezes in the first release that carries it.
> - A `crosshatch-ci.yml` job, listed in `Crosshatch Test Status`'s `needs`, fails a PR that changes `docs/crosshatch/api-level-<n>.txt` between `merge-base(HEAD, develop)` and `HEAD` for any frozen `n`, or that turns `API_LEVEL_FROZEN` from true to false. The host test keeps checking that the list matches the code.

Then fix `epic-game-api-docs.md` Done when 4 ("documented", not "frozen"), `epic-first-party-games.md` Done when 3, and the seed's two sentences.

---

## H3 [High] The surface list freezes names, not the level, and four documents list the same names

**AD text both units obey:**
- AD-19: "`docs/crosshatch/api-level-<n>.txt` lists every `ch.*` name and icon name of level n. A host test compares it with the registered bindings and the icon table."
- AD-19: "a released level never changes".
- AD-24: "the API docs epic catalogs it".
- Conventions: "The game API reference is self-contained, with a LuaLS `---@meta` stub".

**What a level contains beyond `ch.*` names and icon names:**
- argument lists and defaults, such as `ch.gfx.text`'s *(draft)* `align` and `rect`'s `filled`;
- the string enums: colors, sizes, refresh modes, `align` values, swipe `dir`;
- event kinds and fields;
- the `ctx` fields;
- manifest keys and their rules;
- the limits: 1,400 B, 256 B, 4 KiB, 2,048 commands, 2 M instructions, 256 KiB arena, 1,000 ms timer minimum;
- the opened standard libraries and globals;
- `require` semantics;
- the maximum number of seats (the seed: "Devices at API level 1 support at most 2").

The Deferred table's own examples of later additions are mostly *not* names: "true grayscale" adds color strings, and the `coroutine` library adds a global outside `ch`.

**Pair 3a: an addition the list cannot see.**
- **Unit A (a post-freeze script-runtime ticket):** adds the color `"gray"` for true grayscale, a `{kind = "double_tap"}` event, or opens `coroutine`. No `ch.*` or icon name changes, so `api-level-1.txt` is untouched and the freeze check is green. The ticket does not bump the level.
- **Unit B (a game author):** uses the new color with `"api": 1`, and the game is valid everywhere.
- **The incompatibility:** on every earlier level-1 build the game fails with a `ScriptError`. "A released level never changes" was broken, and nothing noticed.
- The same happens when a limit is lowered or a default changes, for example `ch.timer.after`'s minimum.

**Pair 3b: is the list per level or cumulative?**
- "Lists every … name of level n" has two readings:
  - the names *added* at n;
  - the names *available* at n.
- **Unit A (the surface test, script-runtime):** compares `api-level-1.txt` to the registered bindings by set equality, which is correct only for the cumulative reading.
- **Unit B (the first post-v1 level author):** writes `api-level-2.txt` with the new names only, because "any addition is the next level".
- The test then fails, or gets "fixed" to compare with the file for `API_LEVEL` only, which silently drops the level-1 names from the check.
- `minApi` makes this worse: after a breaking change, the host's surface is levels `minApi..API_LEVEL`, and neither reading says how a name is removed.

**Pair 3c: three unchecked copies of the names.**
- **Unit A (game-api-docs, epic 7):** writes `game-api.md`, `ch.d.lua`, and the icon catalog from the icon name map as it stands at epic 7.
- **Unit B (icon-library, or first-party-games in preview):** adds three icons to level 1 after epic 7. That is legal until the freeze, per H2. `api-level-1.txt` is updated because the host test forces it, but the docs are not.
- **The incompatibility:** level 1 freezes with a `ch.d.lua` and a catalog that lack names the device has. Those files are what "move to the starter repo unchanged" and what AI authors read.
- The reverse also happens: the seed documents *(draft)* `ch.gfx.circle`. If the runtime never implements it, the docs promise a name that is not in the list.
- The icon names now live in four places: the AD-24 name map in `assets/game-icons/`, `GameIcons.generated.h`, `api-level-1.txt`, and the catalog. Only the middle two are compared.

**Proposed rule (AD-19, Surface list):**

> - `docs/crosshatch/api-level-<n>.txt` lists what level n *adds*, one entry per line, each a typed key:
>   - `fn ch.gfx.text(x,y,str,size,color,align?)`;
>   - `enum color gray`;
>   - `event double_tap{x,y}`;
>   - `ctx api`;
>   - `manifest icon`;
>   - `limit snapshot_bytes 1400`;
>   - `lib coroutine`;
>   - `icon ship`;
>   - `seats_max 2`.
> - A host's surface is the union of the lists `minApi..API_LEVEL`, minus `remove` lines, which only a level that raises `minApi` may add.
> - The host test builds a Lua state on the host and checks the union against the live `ch` table, including arities from a binding registry that carries the signature, the opened globals, the enum tables, the limit constants, and the icon table.
> - The same test checks that `ch.d.lua` declares exactly the `fn` and `enum` entries and that the icon catalog lists exactly the `icon` entries.
> - The list is the owner. The code, `ch.d.lua`, the catalog, and `game-api.md` follow it, and the AD-24 name map lists only names that are in it.

---

## H4 [Medium-High] `hostCaps` is used in three ADs and defined in none

**AD text both units obey:**
- AD-15: "`Manifest::check(hostCaps)` returns `Invalid(reason)` (rejected at install), `Unavailable(reason)` (installed but not startable: `api` above `hostCaps.api` or below `hostCaps.minApi`), or `Ok` with the modes this host can satisfy."
- AD-19: "`ch.api`, `Manifest::check` … read" `ApiLevel.h`; "raise `hostCaps.minApi` (1 in v1)".
- AD-2: "`hostCaps.nearby` is false under `SIMULATOR`".
- AD-11: "the player picks `n` within `seats` and the host maximum".
- The seed: "Devices at API level 1 support at most 2".

**Pair 4a: two builders of `hostCaps`.**
- AD-15 lists four callers of the parser: the installer, the registry, the launcher, and the lobby. Nothing says who builds `hostCaps`.
- **Unit A (install-and-launcher):** builds `{api = API_LEVEL, minApi = 1, nearby = !SIMULATOR}` in `GameModeActivity`.
- **Unit B (play-nearby):** builds its own `HostCaps{API_LEVEL, 1}` in `GameLobbyActivity`, where `nearby` defaults to true. It also carries `minApi` as a literal, because `ApiLevel.h` has no such constant, even though the new Units convention requires "one named constant".
- **The incompatibility:** when a breaking change raises `minApi` to 2, one screen shows the game as Unavailable and the other opens its lobby.

**Pair 4b: is `ApiLevel.h` or `hostCaps` the input?**
- **Unit A (install-and-launcher, `Manifest::check`):** compares `manifest.api` with `API_LEVEL` directly, because AD-19 says `Manifest::check` "reads" the constant.
- **Unit B (a host test in the same epic, or play-nearby's lobby test):** passes `hostCaps{api = 2}` to test "api above host".
- **The incompatibility:** the test and the code disagree about which input governs, and `GameCore` cannot see `SIMULATOR` anyway, since it is pure (AD-1).

**Pair 4c: the list of Unavailable reasons now reads as exhaustive.**
- The diff changed "e.g." to a colon list.
- **Unit A (install-and-launcher's installer):** finds that a nearby-only game on the simulator, or a game with `seats.min = 3` on an n ≤ 2 host, is neither "api above" nor "api below". So it is either `Invalid`, which renames it `.cpgame.bad` and deletes it from the inbox for good, or `Ok` with `modes = []`.
- **Unit B (the launcher's mode step):** AD-22 skips the step "when a game offers one mode" and says nothing about zero modes, so it starts the first mode or crashes on an empty list.
- A game that becomes valid on a later host (4 seats, per Deferred) should never have been rejected at install.

**Proposed rule (AD-15, and AD-19's One constant):**

> - `GameCore::HostCaps {api, minApi, maxSeats, nearby}` is a plain struct.
> - `ApiLevel.h` holds `API_LEVEL`, `API_MIN_LEVEL`, and `API_LEVEL_FROZEN`. Seat maximum is `GameCore::MAX_SEATS` (2 in v1).
> - One function, `src/games/HostCapsProvider::current()`, fills the struct: `nearby = !SIMULATOR`, as AD-2 requires. Every caller of `Manifest::check` uses it, and `Manifest::check` reads only its argument.
> - `Invalid` is for manifests that no host could accept: grammar, types, `modes` empty, `nearby` with `seats.max < 2`, `solo` with `seats.min > 1`.
> - Everything that depends on the host is `Unavailable(reason)`: `api`, `minApi`, `seats.min > maxSeats`, and an empty set of satisfiable modes.
> - `Ok` always carries at least one mode.

---

## H5 [Medium] Python reads `ApiLevel.h` from an unnamed checkout, and `pack-games` becomes a second manifest reader

**AD text both units obey:**
- AD-19: "the release notes … and the release's `pack-games` step read it; `pack-games` refuses a game whose `api` is above it."
- AD-25: the release runs "on `develop` HEAD or an ancestor ref", and "the release reads the vectors from the commit it releases". That covers the *vectors* only.
- AD-16: "`scripts/pack_game.py` computes the same value" (the packer is install-and-launcher's).

**Pair 5a: the tools checkout and the source checkout.**
- `crosshatch-release.yml` checks out `tools` (the workflow's commit) and `src` (`inputs.ref`). `fork_release.py:51` resolves its data from `TOOLS_DIR`.
- **Unit A (whoever adds the `ApiLevel.h` read to `fork_release.py`):** follows that pattern and reads `tools/lib/GameCore/ApiLevel.h`.
- **Unit B (a rollback release of an ancestor):** releases a commit made before level 2 opened.
- **The incompatibility:**
  - The notes say "Game API 2" for level-1 firmware.
  - `pack-games` accepts a game with `api` 2 that the released firmware shows as Unavailable.
  - A preview or frozen label taken from the wrong commit contradicts H2's freezing-release rule.

**Pair 5b: two Python readers of `manifest.json`.**
- **Unit A (`fork_release.py pack-games`):** needs `api`, so it `json.load`s the manifest itself and compares `manifest['api'] > API_LEVEL`. A manifest with `"api": 1.0` or `true` passes, because Python compares `1.0 > 1` and `True > 1` as false.
- **Unit B (`GameCore::Manifest::parse` on the device):** requires an integer ≥ 1 and returns `Invalid`.
- **The incompatibility:** the release attaches a package that every device rejects. AD-15's "one manifest parser" is honoured on the device, while the release has a third reader next to `pack_game.py`.
- In addition, nothing refuses `api < API_MIN_LEVEL`, and nothing checks that a game uses only names from the level it declares. A post-v1 first-party game that declares `"api": 1` and calls a level-2 name packs, installs, and fails only on level-1 devices.

**Pair 5c: no owner for the release changes.**
- AD-19's Binds are script-runtime, api-docs, and package-install-launcher.
- The `fork_release.py` changes (notes and `pack-games`) sit in the release workflow owned by epic-platform-baseline, which is built.
- The surface list is edited by icon-library, and the freeze is flipped by first-party-games (H2). Neither is bound.
- An epic session reads its bound ADs first, so these edits have no owner.

**Proposed rule (AD-19, One constant; AD-25; Binds):**

> - Every tool that reads `ApiLevel.h` or `api-level-*.txt` reads the copy in the commit it releases or packs.
> - `scripts/pack_game.py` is the only Python reader of `manifest.json`. It takes `--api-range MIN..MAX` and fails when `api` is not an integer in range, or when the sources name a `ch.*` entry or a string-literal icon name outside the union of the lists `MIN..api`. The icon check is best effort, by token scan.
> - `pack-games` passes the released commit's `API_MIN_LEVEL..API_LEVEL`.
> - AD-19 binds all.
> - epic-script-runtime owns `ApiLevel.h`, the surface test, and the `fork_release.py` notes and `pack-games` change.

---

## H6 [Medium] The AD-2 storage rule forbids what it allows, and nothing enforces it

**AD text both units obey (AD-2):**
- "no static-storage object with dynamic initialization at any scope and no static storage over 64 B in any memory region; a larger buffer is allocated per match by its AD-20 owner."
- "Read-only tables are `inline constexpr` or `static constexpr` (generated icon data, `ForkRelease::LATEST_RELEASE_URL`)."

**Pair 6a: "any memory region" includes flash.**
- `ForkRelease::LATEST_RELEASE_URL` is 74 B (73 characters and a NUL), already built in `ForkRelease.h:27`. The icon tables are about 40 KiB.
- Both are static storage over 64 B in the flash region. So are string literals, which have static storage duration: every `luaL_error` message longer than 64 characters in the `ch.*` bindings is one.
- **Unit A (the script-runtime reviewer or a lint ticket):** implements the first sentence literally, with a size check over all static objects. It flags the URL, the icons, and long error strings.
- **Unit B (icon-library):** relies on the second sentence.
- The two sentences give opposite answers, and the retro1 H1 text this replaces said "(DRAM, PSRAM `EXT_RAM_BSS_ATTR`, or IRAM)".

**Pair 6b: `static constexpr` at namespace scope.**
- **Unit A (icon-library's `gen_game_icons.py`):** emits `static constexpr uint8_t kIcon_mark_x_32[] = {…};` at namespace scope in the header. That form is allowed as written.
- **Units B, C, and D:** the `GameScript` binding, `FrameReplay`, and `CoverGridHomeUi.cpp` (row 9) each include the header and ODR-use the tables.
- **The incompatibility:** internal linkage gives one copy per translation unit, which is the H1 pair 1c flash blow-up. The retro1 proposal said "`inline constexpr` at namespace scope, or `static constexpr` inside a function or class". The spine dropped the scoping.

**Pair 6c: no enforcement for "dynamic initialization at any scope".**
- No build flag, lint, or CI step is named. GCC has no `-Wglobal-constructors`.
- **Unit A (play-nearby):** writes `static const std::array<Handler, 8> handlers = makeHandlers();` in a function. **Unit B (install-and-launcher):** does the same kind of thing. Both are reviewed by eye.
- The `.data`/`.bss` gate does not see either one, because guard variables are 8 B and the objects are small. The AD-5 abandon path can still kill the `GameVM` task inside `__cxa_guard_acquire` (retro1 H1 pair 1a).

**Pair 6d: "per match by its AD-20 owner" is the wrong owner for some buffers.**
- **Unit A (play-nearby):** needs the AD-18 "fixed ring buffer in `EspNowLink`" while the *lobby* owns `NearbySession`, before any match exists (AD-18: "The lobby creates the session and passes it by move"). A literal reading puts the ring in `GameMatchActivity`, so the lobby cannot receive `JOIN`.
- **Unit B (install-and-launcher):** the installer's inflate window and the registry's list are per install and per launcher visit, and have no AD-20 owner.
- Once the ring moves from static storage to the heap, AD-18 must also say that the receive callback is unregistered (`ESP_NOW.end()`, or removing the peer) before the ring is freed. Otherwise the Wi-Fi task can write into freed memory.

**Pair 6e: the retro1 H1 generated-header fixes are in the memlog but not in the spine.**
- Memlog line 112: "`lib/GameIcons/GameIcons.generated.h` is un-ignored in .gitignore". The spine does not say so. `.gitignore:12` `*.generated.h` still matches, as `git check-ignore` confirms.
- **Unit A (icon-library):** runs `git add`, which silently skips the header, so CI builds without it. Or it force-adds the file.
- **Unit B (`bin/clang-format-fix`):** formats every tracked `.h` not under its four exclusions (lines 45-50), so it reformats the generated file. The next regeneration then shows as a diff, and any "regenerate and diff" check fails for ever.

**Proposed rule (AD-2, replacing the storage sentences):**

> - Game code has no *writable* static storage over 64 B in any RAM region (`.dram0.data`, `.dram0.bss`, `.ext_ram.bss`, `.iram0.*`, `.rtc.*`, `.noinit`).
> - Read-only data in flash is limited only by the flash budget, and is `inline constexpr` at namespace scope or `static constexpr` inside a function or class.
> - Every other variable of static or thread storage duration in game code is declared `constinit` (C++20) and is trivially destructible, so the compiler rejects dynamic initialization.
> - The flash budget job also fails when any game object file (`lib/Game*`, `src/games`, `src/activities/games`) defines a `_GLOBAL__sub_I_*` symbol or references `__cxa_guard_acquire`.
> - A larger buffer is heap-allocated by the object that uses it and freed with it: the lobby, then the match (AD-18, AD-20), for session buffers; the installer and launcher for their own.
> - `EspNowLink` unregisters its receive callback before freeing its ring.
> - `.gitignore` gains `!lib/GameIcons/GameIcons.generated.h`, and `lib/GameIcons/.clang-format` sets `DisableFormat: true`.
> - A `crosshatch-ci.yml` step regenerates the header and fails on a diff.

---

## H7 [Medium] The `.data` + `.bss` gate has no defined measurement

**AD text:** AD-2: "The flash budget job also fails when the x4pro `.data` + `.bss` grows more than 1 KiB with games on."

There are no ELF sections literally named `.data` and `.bss` on the ESP32-S3. The candidates are:

| Measurement | What it counts |
| --- | --- |
| GNU `size` in Berkeley format | "data" and "bss" include `.ext_ram.bss` (PSRAM), `.rtc.*`, and `.iram0.bss` |
| `esp-idf-size` (already installed in the flash job) | reports DRAM, IRAM, and PSRAM separately |
| `firmware.bin` size, the only thing `check_flash_budget.py` reads today | contains `.data`'s initial image but no `.bss` |

**Pair 7a:**
- **Unit A (the flash-budget follow-up ticket):** implements the gate as Berkeley `data + bss` from `firmware.elf`.
- **Unit B (play-nearby):** adds no static of its own, as H6 requires. The framework objects that its code pulls in still count: the Arduino `ESP_NOW` global, with its constructor and peer table, and `libespnow`'s `.bss` land in the games-on image once play-nearby links.
- **The incompatibility:** play-nearby goes red on framework `.bss` it cannot avoid, and the fix is to raise the limit, which is a spine edit with no rule for it. Or Unit A measures DRAM only (`esp-idf-size`), and a PSRAM static slips through, which H6's rule forbids and the gate cannot see.
- Neither Unit A nor the spine says whether the 1 KiB is games-on minus games-off of the same commit (the flash method) or growth against the previous PR.

**Proposed rule (Flash budget row):**

> The RAM gate reads `firmware.elf` of the games-on and games-off x4pro builds of the same commit with `esp-idf-size --format json`. It fails when the sum of `.dram0.data`, `.dram0.bss`, `.iram0.data`, `.iram0.bss`, `.rtc.data`, `.rtc.bss`, `.noinit`, and `.ext_ram.bss` grows by more than `RAM_BUDGET_BYTES = 1024`, framework objects included. The limit is a constant in `check_flash_budget.py`, with boundary vectors per the Units convention. Each epic records the figure beside its flash delta.

---

## H8 [Low-Medium] AD-4's "type or struct layout" still admits header-affecting defines

**AD text both units obey (AD-4):** "`library.json` passes no define that changes a type or struct layout seen through `lua.h`, `luaconf.h`, `lauxlib.h`, or `lualib.h`, because includers compile without it."

The public headers read other overridable macros that change neither a type nor a layout:
- `LUA_FAILISFALSE` (`lauxlib.h:171`) switches `luaL_pushfail` between `nil` and `false`.
- `LUA_USER_H` (`lua.h:149`) injects an arbitrary header.
- `lua_getlocaledecpoint` and `luai_likely` (`luaconf.h:643,654`) are also overridable.
- `LUA_USE_C89` (`luaconf.h:151`) changes `lua_Integer`. That one is a type, so it is caught.

**Pair 8a:**
- **Unit A (a script-runtime ticket):** sets `-DLUA_FAILISFALSE` in `library.json` so that `string.find` misses return `false`. It is not a type or layout change, so AD-4 is obeyed.
- **Unit B (the `ch.*` bindings, compiled as C++ without the flag):** return failure with `luaL_pushfail`, which pushes `nil`.
- **The incompatibility:** games see two failure values in one API level, and the behaviour depends on which translation unit ran. It is also a level-surface change that H3's list would not catch.

**Proposed rule (AD-4):**

> `library.json` `flags` sets only `LUA_COMPAT_GLOBAL=0`, plus `LUAI_*` macros that no public header (`lua.h`, `luaconf.h`, `lauxlib.h`, `lualib.h`) mentions. A host test greps the four headers for every macro in `flags` and fails on a match other than `LUA_COMPAT_GLOBAL`.

---

## H9 [Low] Contradictions with untouched text, and loose ends from retro1

Each item below is a place where two units reading different texts build different things.

1. **AD-24:** "a level only adds names, and never renames, removes, or redraws". Under AD-19 a *released* level never changes, and additions go to the *next* level. An icon-library session reading AD-24 adds names to frozen level 1.
   - Fix: "levels only add: a released level never gains, renames, removes, or redraws a name; new names go to the next level (AD-19)".
2. **`specs/spec-crosshatch-player/glossary.md:15`:** "additive only within a level". AD-19 deliberately removed that phrase. Sync the glossary.
3. **Seed:**
   - Line 13, "*(draft)* can change until the runtime ships", and line 53, "until crosshatch v1 is released", are two further freeze moments (H2).
   - Line 163, "new levels only add names", is fine.
   - Line 54 ties the seat maximum to the API level ("Devices at API level 1 support at most 2"), but the spine treats it as a host maximum (AD-11) with no level. Pick one; H3's `seats_max` list entry and H4's `maxSeats` make it both, explicitly.
   - External authors cannot observe "crosshatch v1 is released": tags are `X.Y.Z-ch.N`. The seed should point at the release notes' "(preview)" label.
4. **AD-19:** "A host at level L runs every game with `api` ≤ L unchanged" is unconditional, and the next bullet says preview games may break. Add "from the level's freeze".
5. **Diagram and layer table:** "Arrows are the only allowed dependencies" now sits beside new edges for row 10 only. Rows 4-7 (`ActivityManager` / `HomeActivity` → `src/activities/games`) and row 9 (`CoverGridHomeUi` → `lib/GameIcons`) are ledgered dependencies with no arrow. A reviewer applying the arrow rule blocks the icon-library's row-9 change.
   - Fix: draw one "upstream, ledgered rows" node with edges to ACT (4-7), ICO (8-9), and CORE/ADP (10), or state that the diagram covers game-internal edges only.
6. **Ledger doc:** AD-25 says the probe's timeout, redirect limit, TLS mode, and user agent "are recorded beside ledger row 10 in `docs/crosshatch/upstream-touches.md`". They are not (grep finds no timeout or UA there), and the probe's comment "Same transport settings as HttpDownloader's release fetch" (`ForkReleaseProbe.cpp:23`) is still false. This repeats retro1 H8.
7. **Memlog and spine drift on H4:** memlog line 115 says a malformed `-ch` tag "is removed by an admin lifting the ruleset", while AD-25 Tags says only that admins are the bypass. A release session reading only the spine does not know that recovery path exists.
8. **Fork scripts:**
   - The helper module is still unnamed ("one fork-only helper module"), so two scripts can each create "the" helper (retro1 H7 pair 7b).
   - Sidecar wiring is still per step. `crosshatch-ci.yml` runs `check_upstream_touches_test.py` and `fork_release_test.py` in the ledger job and `check_flash_budget_test.py` in the flash job, not by glob, so a new `game_codec_test.py` runs only if its author remembers.
   - Fix: name `scripts/crosshatch_tools.py` and run `scripts/*_test.py` by glob in one job.
9. **Units row:** "mirrored in a vector file" does not name which one. `pack_game.py`'s package limits and `game_codec.py`'s value limits need a named file, for example `test/game_script/limits_vectors.json`, or two sessions create two files.

---

## Rule text to add, consolidated

1. **AD-13 / AD-19 (H1):**
   - `ADVERT` and `JOIN` carry `hostApi` and the surface CRC.
   - While either device is preview, matching requires equal values.
   - `ctx.api` is the roster minimum; `setup` and `apply` branch only on `ctx.api`.
2. **AD-19 Freeze (H2):**
   - Levels below `API_LEVEL` are frozen, and `FROZEN` describes `API_LEVEL` only.
   - The flip is the last ticket of first-party-games and never reverts.
   - The first release with it set is the freezing release. Post-v1 releases need `FROZEN` true.
   - A CI job diffs frozen lists against the merge base.
   - Sync the game-api-docs and first-party-games Done-when lines and the seed.
3. **AD-19 Surface list (H3):**
   - Per-level, typed entries cover functions and signatures, enums, events, `ctx`, manifest keys, limits, libraries, icons, and seats.
   - The host surface is the union from `minApi` to `API_LEVEL`.
   - The host test also checks `ch.d.lua` and the catalog. The list is the owner.
4. **AD-15 / AD-19 (H4):**
   - `HostCaps {api, minApi, maxSeats, nearby}`, built by one provider.
   - `API_MIN_LEVEL` goes in `ApiLevel.h`.
   - `check()` reads only its argument.
   - Host-dependent failures are `Unavailable`, and `Ok` has at least one mode.
5. **AD-19 / AD-25 (H5):**
   - Tools read `ApiLevel.h` and the lists from the released commit.
   - `pack_game.py` is the only Python manifest reader, checks the api range and (best effort) the names used.
   - AD-19 binds all, and script-runtime owns the release-script change.
6. **AD-2 (H6):**
   - Writable RAM statics are at most 64 B, and read-only flash data is free, with the scoping on `static constexpr`.
   - Static variables are `constinit` and trivially destructible, with an `nm` check.
   - Buffers are owned by their user (lobby, then match; installer; launcher), and the ESP-NOW callback is unregistered before the ring is freed.
   - The generated header is un-ignored, has `DisableFormat`, and gets a regenerate-and-diff check.
7. **Flash budget row (H7):** the RAM gate is defined by `esp-idf-size` sections, games-on minus games-off, with framework objects included and a named constant.
8. **AD-4 (H8):** no `library.json` define that any public Lua header mentions, except `LUA_COMPAT_GLOBAL`, enforced by a grep test.
9. **Sync (H9):**
   - AD-24 wording, the glossary, and the seed's freeze and seat lines.
   - AD-19 "unchanged from the freeze".
   - Diagram edges for rows 4-9.
   - The ledger doc's transport values and the false probe comment.
   - The malformed-tag recovery in AD-25.
   - A named fork-script helper, and sidecars run by glob.
   - A named limits vector file.

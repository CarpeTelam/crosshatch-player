---
name: 'crosshatch-player v1 game platform'
type: architecture-spine
purpose: build-substrate
altitude: feature
paradigm: 'Hexagonal host (pure GameCore domain with ports) + reducer-style script contract with host-authoritative full-state replication'
scope: 'v1 game runtime for simple turn-based games on x4pro and sticky: Lua script runtime, icon library, seat-based multiplayer (solo, pass-and-play, Play Nearby over ESP-NOW), .cpgame packages, SD-inbox install, Home launcher'
status: final
created: '2026-09-26'
updated: '2026-09-28'
binds: [script-runtime, multiplayer-layer, package-install-launcher, first-party-games, api-docs]
sources:
  - '_bmad-output/planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/brief.md'
  - '_bmad-output/planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/addendum.md'
  - '_bmad-output/planning-artifacts/research/spike-script-engine-2026-09-26.md'
companions:
  - 'game-api-seed.md'
---

# Architecture Spine: crosshatch-player v1 game platform

## Design Paradigm

**Hexagonal host.** `lib/GameCore` is the domain: roster, session, turns, snapshots, the wire protocol, the reliable link, and manifests. It is host-testable C++20 with no device dependencies. It declares ports; everything touching Lua, the radio, the SD card, or the display is an adapter outside it.

**Reducer contract for scripts.** A game is a set of functions over one serializable `state`. Only the **authority** device runs `setup` and `apply`. Every device runs `draw` and `input`. The runtime replicates the whole state after each move, so scripts contain no radio code and one script runs in every mode.

**Built for simple games.** The engine serves turn-based pen-and-paper, board, card, dice, word, puzzle, and parlor games (AD-23). It is not a real-time or high-performance engine, and that scope is what makes the event-driven, whole-frame, e-ink-first design above sufficient.

| Layer | Lives in | May depend on |
| --- | --- | --- |
| Domain | `lib/GameCore/` | C++ standard library, `lib/Memory`, `lib/JsonParser` |
| Icon data | `lib/GameIcons/` | nothing (generated data only) |
| Script adapter | `lib/GameScript/` | `GameCore`, `GameIcons` (names), `lib/lua`, `lib/Utf8` (`TextMetrics`) |
| Engine (vendored) | `lib/lua/` | C standard library |
| Device adapters | `src/games/` | `GameCore`, `GameScript`, `GameIcons`, HAL, `Storage`, `ZipFile`, `PngToBmpConverter`, ESP-NOW, mbedTLS; `lib/Utf8`; `lib/EpdFont` and `src/fontIds.h` (`FrameReplay`'s built-in fonts, AD-7); the SDK's `FreeInkUICore.h` (`GameTouch.h`'s touch types, AD-20); `SecureHttpClient` in `ForkReleaseProbe` only (AD-25) |
| Screens | `src/activities/games/` | `src/games/`, `GameCore`, `GfxRenderer`, `UiListActivity` / `UiAppHost` |

**Amended 2026-09-28 (owner):** the `lib/Utf8` edges (`GameScript`'s `TextMetrics.h`, for UTF-8 decoding in `ch.text_width`, and `src/games/FrameReplay.cpp`), the `lib/EpdFont` / `src/fontIds.h` edge (`src/games/FrameReplay.cpp`), and the SDK `FreeInkUICore.h` edge (`src/games/GameTouch.h`) are as built and reviewed (epic-script-runtime retro A1); the diagram below shows them. The rule for Screens is unchanged: they reach `GameScript` only through `src/games` (retro AI-5 restores the code to it).

## Invariants & Rules

```mermaid
flowchart TD
  %% Amended 2026-09-28 (owner): the Games list, and the Utf8, EpdFont/fontIds.h, and FreeInkUICore.h edges (retro A1).
  ACT["src/activities/games<br/>(Games list / launcher, mode picker, lobby, GameMatchActivity)"] --> ADP["src/games<br/>(GameLink task, EspNowLink, NearbySession, installer, registry,<br/>GameSaveStore, FrameReplay, GameViewport, arena, GameAssets)"]
  ACT --> CORE["lib/GameCore<br/>(Roster, Session, Protocol, ReliableLink, Manifest, ports)"]
  ADP --> SCR["lib/GameScript<br/>(VM host, sandbox, ch.* bindings, codec, frame buffers, LuaGame)"]
  ADP --> CORE
  ADP --> ICO["lib/GameIcons<br/>(GameIcons.generated.h)"]
  SCR --> CORE
  SCR --> ICO
  SCR --> LUA["lib/lua<br/>(Lua 5.5.1, unmodified)"]
  CORE --> STD["lib/Memory, lib/JsonParser"]
  ADP --> HAL["upstream: HAL, Storage, ZipFile, PngToBmpConverter, GfxRenderer"]
  ACT --> HAL
  OTA["upstream: network/OtaUpdater.cpp<br/>(ledger row 10, guarded)"] --> CORE
  OTA --> ADP
  ADP --> HTTP["SDK: SecureHttpClient<br/>(ForkReleaseProbe only)"]
  SCR --> UTF["upstream: lib/Utf8"]
  ADP --> UTF
  ADP --> FNT["upstream: lib/EpdFont, src/fontIds.h<br/>(FrameReplay)"]
  ADP --> FUI["SDK: FreeInkUICore.h<br/>(GameTouch.h)"]
```

Arrows are the only allowed dependencies among fork code. Upstream code reaches game code only through the ledgered, guarded rows of AD-3; the diagram shows row 10, which exists today. `GameCore` includes no Arduino, ESP-IDF, Lua, HAL, or `src/` header. `GameScript` includes no `GfxRenderer`, HAL, or Arduino header; platform services (arena, sources, text metrics, randomness) reach it through injected ports.

### AD-1: Hexagonal host, reducer scripts [ADOPTED]

- **Binds:** all
- **Prevents:** game rules or radio logic split between C++ and Lua; a core that only runs on the device.
- **Rule:** game logic lives only in scripts. Roster, session, turn, sync, and protocol logic lives only in `GameCore`, behind the ports `IGameRules`, `ILink`, `IClock`, `IRandom`, and `ISnapshotStore`. Adapters implement ports and hold no game rules. Every `GameCore` unit has a host GoogleTest suite; link and session suites run over a `FakeLink` that drops, delays, duplicates, and reorders frames.

### AD-2: One build guard, C3-safe [ADOPTED]

- **Binds:** all
- **Prevents:** game code costing flash or RAM on C3 builds, or breaking them.
- **Rule:** `FREEINK_CAP_GAMES=1` is set for `x4pro`, `sticky`, their `-gh_release` / `-gh_release_rc` variants, and the fork-owned simulator envs only. Every include of game code in an upstream file sits inside `#if FREEINK_CAP_GAMES`. Every `.cpp` under `src/games/` and `src/activities/games/` is wrapped whole-file in `#if FREEINK_CAP_GAMES`. In game code (`lib/Game*`, `src/games/`, `src/activities/games/`) every static-storage variable, at any scope, is `constexpr` or `constinit`, so none is dynamically initialized, and no mutable one is over 64 B. A larger mutable buffer is allocated by the component that owns its lifetime (the match, the lobby, the installer) and freed with it. Read-only data may be any size: it is `constexpr` in one `.cpp`, or `inline constexpr` in a header, never `static` or unnamed-namespace `constexpr` in a header, which would copy it into every includer (generated icon data, `ForkRelease::LATEST_RELEASE_URL`). `lib/GameIcons/GameIcons.generated.h` is committed (AD-24): `.gitignore` un-ignores it and the format check skips it. The flash budget job also fails when the x4pro ELF's internal-RAM data and bss (`.dram0.data` + `.dram0.bss` + `.noinit`, by section name from the toolchain's `size -A`; the image size does not show them) grow more than 1 KiB with games on. All game libraries compile, unreferenced, for `default`, `x4c`, and `papermono`: the fork-only `src/games/GamesBuildAnchor.cpp`, whole-file guarded like every file there, includes a header from each game library and `lua.h`; PlatformIO's default `chain` dependency finder does not evaluate `#if`, so every env builds the libraries and the linker drops them where nothing references them. No `lib_deps` entry is added. Device-only game code is wrapped whole-file in `#if FREEINK_CAP_GAMES && !defined(SIMULATOR)`; `simulator.ini` excludes no game file by name, and fork CI builds `simulator_x4pro` and `simulator_sticky`. Under `SIMULATOR`, `EspNowLink` is compiled out, `hostCaps.nearby` is false so `Manifest::check` is the only place a mode disappears, and the SHA-256 helper uses OpenSSL.

### AD-3: The upstream-touch ledger is the cap [ADOPTED]

- **Binds:** all
- **Prevents:** the fork drifting back into CrossPlay's merge pain.
- **Rule:** v1 changes only the upstream files in the ledger below. Row 10 used the 1 reserve slot (AD-25), so no reserve remains. Each change is `#if FREEINK_CAP_GAMES`-guarded where the language allows; unguardable changes are marked as such. The ledger lives in `docs/crosshatch/upstream-touches.md`. A fork-only CI job fails a PR when a path that exists in `upstream/develop` differs between `merge-base(HEAD, upstream/develop)` and `HEAD` and is in neither the ledger nor a baseline allowlist of pre-existing fork files (`AGENTS.md`, `.gitattributes`, `.gitignore`, `.github/PULL_REQUEST_TEMPLATE.md`, the removed `CLAUDE.md`); the job fetches `upstream/develop` with full history. Any further upstream file needs a spine update first.

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

  The vendored engine is kept out of the whole-tree format check by a new `lib/lua/.clang-format` with `DisableFormat: true`, not by editing `bin/clang-format-fix`.

### AD-4: Lua 5.5.1, unmodified, 64-bit integers [ADOPTED]

- **Binds:** script-runtime, api-docs, first-party-games
- **Prevents:** a patched engine that blocks upgrades; games or saves that depend on a language level or integer width that later changes.
- **Rule:** PUC Lua 5.5.1 is vendored byte-for-byte in `lib/lua/` and compiled as C, with every `LUA_COMPAT_*` option off. 5.5.1 defaults `LUA_COMPAT_GLOBAL` on, so `library.json` passes `-DLUA_COMPAT_GLOBAL=0` to the Lua units only; that is the one compat define, and `luaconf.h` stays unedited. `library.json` passes exactly that define; any other, `LUA_USER_H` included, needs a spine update, because includers of the public Lua headers compile without it. A fork-owned `lib/lua/library.json` `srcFilter` excludes `lua.c`, `luac.c`, `linit.c`, `liolib.c`, `loslib.c`, `ldblib.c`, `loadlib.c`, and `lcorolib.c`; `test/game_script` builds the same source list. Games target the Lua 5.5 language (`global` is reserved; `for` control variables are read-only) with 64-bit integers; `LUA_32BITS` would break older games and needs a spine update with a plan for them (AD-19). `lua_newstate`'s hash seed comes from `IRandom`. Every C++ file that includes `lua.h` includes `<climits>` first and `static_assert`s `sizeof(lua_Integer) == 8`. No spike re-run gates API level 1: 5.5.1 is taken to perform at least as well as the spiked 5.4.7, and a problem found during implementation is fixed when it surfaces. Reverting the pin to 5.4.9 stays the fallback, and it is cheapest before API level 1 freezes.
- **Amended 2026-09-27 (owner):** `library.json` also passes, to the Lua units only, `LUAI_MAXCCALLS` and `MAXCCALLS` (lstrlib's pattern-matcher depth) sized so parser and pattern recursion fit the 16 KB `GameVM` stack, and an `l_randomizePivot` define so `table.sort` never calls `time()`. Each is read only inside Lua's own `.c` files and internal headers, never by `lua.h`, `luaconf.h`, or `lauxlib.h`, so includers stay consistent; `test/game_script` passes the same defines, and the Lua sources stay byte-for-byte.
- **Amended 2026-09-28 (owner):** `library.json` also force-includes a fork header, `lib/lua/port/luai_throw.h` (`-I port`, `-include luai_throw.h`), into the Lua units only, and `test/game_script` mirrors it. It defines `LUAI_TRY` and `LUAI_THROW` as `ldo.c` would on these builds (ISO C `setjmp`/`longjmp`; no build defines `LUA_USE_POSIX`), except that `LUAI_THROW` first calls `luaport_memoryerror(L)` for a memory error. That weak symbol is defined in `lib/GameScript` (`CallGuard.cpp`), which records a sticky memory fault in the state's guard (AD-6); a build that links Lua without it skips the call. The macros are read only by `ldo.c`, so includers of the public headers stay consistent, and the Lua sources stay byte-for-byte.

### AD-5: The VM task owns the Lua state and nothing else [ADOPTED]

- **Binds:** script-runtime, multiplayer-layer
- **Prevents:** two tasks entering one `lua_State`; a teardown deadlock on `RenderLock` (pitfall 12cc816); a hung script freezing the device.
- **Rule:**
  - At most one game VM exists. A dedicated `GameVM` task (priority 1, core 1, 16 KB stack) creates, calls, and destroys it. `GameCore::Session` is confined to the same task, and `Session` and codec scratch are allocated from the VM's arena.
  - The `GameVM` task never takes `RenderLock`, never calls `ActivityManager`, and never touches `Storage`. Before the VM starts, the match activity (on the loop task) loads everything the game needs into PSRAM and hands it over through a `GameScript` port: all `*.lua` sources as one blob with a name-to-span table, converted images (AD-24), the `ch.store` blob, and any resume snapshot.
  - The `GameVM` task exchanges work through depth-bounded queues that carry handles to PSRAM buffers, not frame copies; a full input queue drops the oldest event that is not a timer event, with a log line, so a pending timer is never lost (**Amended 2026-09-28 (owner):** retro R2).
  - While a callback runs, the match's `skipLoopDelay()` returns true, which keeps the CPU at full clock for the budget.
  - **Amended 2026-09-28 (owner):** stopping is cooperative: the match sets an atomic cancel flag that the count hook turns into a Lua error, sets an atomic quit flag, and notifies the task (a FreeRTOS task notification, which wakes it from its wait for input). The VM task unwinds, closes the state, and sets an atomic `finished` flag; the match joins by polling it every 5 ms. If the join has not happened 500 ms after cancel (a script stuck inside a C library call), the match **abandons** the VM without calling `lua_close` (`GameVM::abandon`), polling about 100 times at 5 ms (`ABANDON_WAIT_MS` 500; the bound is not wall-clock, since a poll can also wait up to 10 ticks for the task to suspend; retro AI-4): each poll first deletes the `GameVM` normally if the task has finished; otherwise it suspends the task and deletes it only when it is inside Lua, outside a locked binding, and `inSwap` is clear, then frees the arena, frame buffers, and sources and leaks only the `GameVM` object; otherwise it resumes the task. If no poll can delete it (and always in the simulator, which cannot stop a thread), everything is leaked, and the match keeps the `ch.store` slot alive for the leaked task, still flushing it on Leave and in `onExit()`.
  - **Amended 2026-09-28 (owner):** a cancel yields the outcome `Cancelled`, which is distinct from `ScriptError`; a user or forced exit's cancel shows nothing, while the watchdog's cancel (below) shows AD-14's error view.
  - **Amended 2026-09-27 (owner):** a callback still running 3 s after it started (wall clock) is a stuck script, whatever its instruction count: the match cancels it, abandons it 500 ms later if it has not joined, and shows AD-14's error view. This covers C loops that run no Lua instructions (`table.move` over a huge range, pattern backtracking).

### AD-6: Sandbox and budgets [ADOPTED from the spike, amended]

- **Binds:** script-runtime
- **Prevents:** a game crashing the device, starving internal RAM, or running forever.
- **Rule:**
  - **Amended 2026-09-28 (owner):** each VM's heap is one 464 KiB PSRAM block (`ARENA_BYTES` in `lib/GameScript/ArenaAllocator.h`) managed by a small in-tree allocator: a 448 KiB Lua region behind a counting `lua_Alloc` that caps Lua at 256 KiB of requested bytes (`LUA_HEAP_BYTES`, `limit lua_heap_bytes 262144`), and a 16 KiB reserve for the Session and codec scratch (see the 2026-09-27 amendment below). `src/games` supplies the block as a port. Abandoning a VM (AD-5) frees the block in one call.
  - Every entry into the VM, including script load and setup, goes through a `lua_pcall` trampoline.
  - A sticky count hook enforces 2 M instructions per callback. A script's own `pcall` cannot swallow the budget error.
  - Libraries: base (without `load`, `loadfile`, `dofile`; `print` maps to `ch.log`), table, string, math, utf8. `require` resolves only against the source table from AD-5.
  - Every chunk is loaded from memory in text mode (`"t"`); binary chunks are rejected at install and at load.
  - `math.random` is seeded from `IRandom` (backed by `esp_random()`) when the VM is created.
  - Bindings are C-style functions. No binding holds an RAII object across a call into Lua, and no binding opens files.
  - **Amended 2026-09-27 (owner):** `setmetatable` refuses a metatable with a `__gc` field, because finalizers run with hooks off and escape the budget and the stack check; `table.move`, `table.insert`, and `table.remove` refuse element counts past a sandbox limit; until `print` maps to `ch.log` it is a no-op, never a stdout write. A binding that takes a lock marks itself so an abandon never deletes the task while it holds one.
  - **Amended 2026-09-28 (owner):** every fault the game contract says stops the game (the budget, the heap cap, stack headroom, a value or table over its limit, a full frame, `ch.gfx` outside `draw`) is sticky, so a script's own `pcall` or `xpcall` cannot outlive it. The heap cap's fault is recorded where Lua throws the memory error (AD-4's throw hook), before a `__close` that raises while the error unwinds could turn it into an ordinary error. A refusal on Lua's own allocation path (`luaM`) is retried after an emergency collection and is a fault only if that fails too; the library string buffers (`lauxlib`'s `resizebox`: `string.rep`, `table.concat`, `gsub`, `format`, and the like past `LUAL_BUFFERSIZE`) are refused at the cap with no collection, as in stock Lua, so a game near its cap can end on a large string operation. `setmetatable` also refuses a `__close` field, and the sandbox seals the string metatable (`getmetatable("")` is `false`).
  - **Amended 2026-09-27 (owner):** the 256 KB is Lua's cap, counted in the bytes Lua requests, not the arena's size. Lua gets its own PSRAM region sized as the cap plus a block-header and fragmentation margin (448 KiB), so the cap always binds first on the host and the device alike; beside it a separate reserve (16 KiB) holds the Session and codec scratch, which Lua can never touch. Both regions are slices of one PSRAM block, freed in one call.

### AD-7: Drawing is a display list; FrameReplay owns refresh [ADOPTED]

- **Binds:** script-runtime, first-party-games
- **Prevents:** Lua touching the framebuffer from the wrong task; lost or doubled refresh escalations; taps landing away from what was drawn; layout that differs between devices.
- **Rule:**
  - `ch.gfx.*` appends commands to a back frame buffer in PSRAM (at most 2,048 commands / 32 KB; overflow is a script error). Calling `ch.gfx` outside `draw` is a script error.
  - When `draw` returns, the VM task swaps front and back buffers under a frame mutex (setting `inSwap` for the duration) and bumps an atomic `frameGen`. The match's `loop()` polls `frameGen` and calls `requestUpdate()`. The render task takes the frame mutex only inside `render()`. Lock order is always `RenderLock`, then the frame mutex.
  - The runtime calls `draw` after every new snapshot, after every `input` call, and after hand-off or resume. There is no frame loop; `ch.timer` (AD-23) is the only time-driven trigger. A frame identical to the one on screen is not refreshed.
  - **Amended 2026-09-28 (owner):** `FrameReplay` applies the only escalation policy, `GameScript::RefreshPolicy` (`lib/GameScript/RefreshPolicy.h`, pure and host-tested). Its inputs are the frame's hint (`fast`, `half`, `full`), a `forceFull` flag set by the match activity, and its own fast-refresh counter. Coalesced frames keep the maximum hint (`full` > `half` > `fast`). v1 refreshes whole frames.
  - Color: fills take `white`, `light`, `dark`, `black` (`light` and `dark` render as dithered fills); lines, text, icons, and images take `white` or `black`.
  - Text sizes `small`, `medium`, `large` map to built-in flash fonts only. The match passes per-size advance tables into `GameScript` at VM start, so `ch.text_width` is pure and callable in any callback. Script code never names panel sizes or firmware font IDs.
  - One `GameViewport` in `src/games` defines the script canvas (rotation, offset, the size exposed as `ch.screen`, excluded bezel insets). `FrameReplay` (logical to panel) and the input builder (panel to logical) both use it. The script owns the whole canvas; the runtime draws over it only with its own views (AD-20).

### AD-8: The game contract [ADOPTED]

- **Binds:** script-runtime, multiplayer-layer, first-party-games, api-docs
- **Prevents:** games that each invent their own lifecycle, turn signalling, or rejection handling.
- **Rule:** `main.lua` returns a table with exactly these entry points:

  | Function | Runs on | Returns |
  | --- | --- | --- |
  | `setup(ctx)` | authority | the initial `state`; `ctx = {seats = n, mode = "solo" \| "pass" \| "nearby", api = n}` (AD-19) |
  | `status(state)` | authority; any device inside `draw` | `{turn = seat}` or `{over = true, winners = {seat…}}`; must be a pure function of `state` |
  | `apply(state, seat, move)` | authority | the new `state`, or `nil, reason` to reject |
  | `draw(state, seat, ui)` | every device | nothing; draws via `ch.gfx` |
  | `input(state, seat, ui, event)` | every device | a `move`, or `nil` |

  - `ui` is a table per local seat (one per seat in `pass`, one in `solo` and `nearby`), plus a separate shared table for seat 0 at game over in `pass`. The script may mutate it freely; it is never synced or saved.
  - Events delivered to `input`: `tap`, `long_press`, `swipe` (touch); `rejected` (a move was refused, in every mode; the runtime never draws the reason itself); `over` (once per local seat per round, for per-device records such as wins); `timer` (AD-23). There are no drag events.
  - **Amended 2026-09-28 (owner):** the `rejected` event's reason is cut to 64 B at a UTF-8 boundary in every mode (`limit reject_reason_bytes 64`, `GameCore::REJECT_REASON_BYTES`), the size `REJECT` carries (AD-13); a longer reason is not an error.
  - While a move is awaiting its `STATE` or `REJECT`, the runtime still calls `input` but discards any move it returns.
  - Computer opponents, if a game has one, run inside `apply`.
  - The script decides turn order through `status`; the runtime enforces it (AD-11). This is a deliberate change from the brief's "runtime owns turn order".

### AD-9: The snapshot is the source of truth [ADOPTED]

- **Binds:** multiplayer-layer, script-runtime
- **Prevents:** host and guest diverging; `draw` or `input` mutating game state as a side effect; two devices disagreeing on whose turn it is.
- **Rule:** the canonical state is the encoded snapshot held by `GameCore::Session` on the authority. Each `apply` receives a fresh decode of it, and its result is re-encoded to become the new snapshot. `draw`, `input`, and `status` receive a decoded copy whose changes are discarded. The authority computes `status` after each accepted move and ships it inside `STATE`; guests gate input and the end-of-round flow on that shipped status and never call `status` for control flow.

### AD-10: One codec, one size limit, every mode [ADOPTED]

- **Binds:** multiplayer-layer, script-runtime, first-party-games
- **Prevents:** a game that works in pass-and-play and breaks in Play Nearby; a firmware update that silently corrupts saves; tooling that can't reproduce device bytes.
- **Rule:**
  - One C codec in `GameScript` encodes state, moves, and `ch.store`. It accepts nil, boolean, integer, float, string, and tables with string or integer keys: acyclic, depth at most 16, no metatables, functions, or userdata. Integral float keys are normalized to integers; other float keys and NaN keys are errors.
  - **Codec v1 bytes** (little-endian): one tag byte per value (`nil`, `false`, `true`, int, float, string, table); int is zigzag LEB128 of an i64; float is IEEE-754 binary64; string is a LEB128 length plus bytes; a table is `LEB128 narr`, `narr` values for keys `1..narr` (the longest non-nil run from 1), `LEB128 nrec`, then `nrec` key/value pairs sorted by key (integers ascending, then strings bytewise). The encoding is canonical: equal values give equal bytes.
  - Limits apply to codec output: snapshot at most **1,400 B**, move at most **256 B**, `ch.store` at most **4 KB**. They are enforced in every mode, and a breach is a script error.
  - The codec version is part of `proto`. Every persisted codec blob starts with `{magic, fileVersion, codecVersion}`; an unknown version is discarded with a log line, never decoded.
  - A Python reference codec in `scripts/` and the C codec pass the same encode and decode golden vectors in `test/game_script/`. The byte formats are recorded in `docs/crosshatch/formats.md`.

### AD-11: Roster, seats, and authority [ADOPTED]

- **Binds:** multiplayer-layer, package-install-launcher
- **Prevents:** two seat maps; a guest claiming another seat; two-player assumptions baked into the core; moves accepted out of turn.
- **Rule:** a `Roster {mode, n, localSeats, peers[seat] → link id}` is fixed before `setup` and immutable for the match, including rematches.
  - **Solo:** `n = 1`; local input is seat 1. Offered only when `seats.min == 1`.
  - **Pass:** the player picks `n` within `seats` and the host maximum. `draw` and `input` get `status.turn`, except in the hidden-game `Result` state (AD-12) and at game over (seat 0).
  - **Nearby:** the host is seat 1, and guests get seats in `JOIN` arrival order, carried in `ACCEPT`. `n` is the number of seats filled when the host starts (at least 2). The roster moves into the match with `NearbySession`.
  - The seat of a remote move comes from the peer's link identity, never from the payload.
  - A `status` naming a turn outside `1..n` is a script error. Moves are applied strictly one at a time.
  - v1 hosts support n ≤ 2; nothing in `GameCore` may assume n = 2.

### AD-12: The runtime owns the hand-off [ADOPTED from the brief, amended]

- **Binds:** multiplayer-layer
- **Prevents:** hidden information leaking through e-ink ghosting, sleep, or resume.
- **Rule:** in `pass` mode with manifest `hidden = true`:
  - After an accepted move that changes the turn seat, the match enters `Result`: `draw` gets `seat = mover`, any move `input` returns is discarded, and a runtime banner reads "Tap to pass to player N". A tap shows the blank hand-off screen with a full refresh, and a tap there draws the next seat.
  - The hand-off screen also comes before the first draw after `setup` (including Play again) and after resume.
  - The match's `onExit()`, including the sleep path, renders the blank hand-off screen and pushes it with `displayBuffer(HALF_REFRESH)`, under the `RenderLock` it already holds and without taking it again.
  - Scripts cannot draw during or suppress the hand-off. In `nearby` mode each device draws only its own seat.

### AD-13: Wire protocol [ADOPTED]

- **Binds:** multiplayer-layer
- **Prevents:** the link and the session reading one counter two ways; mismatched payload layouts; matches between devices running different game code.
- **Rule:**
  - Frame: `{magic "XH", proto u8, type u8, session u32, seq u16, ack u16, payload}`, little-endian. `GameCore::Protocol` is the only encoder and decoder of frames and payloads.
  - `seq`/`ack` belong to `ReliableLink` alone: `seq` counts reliable frames per direction, and `ack` is cumulative. `ADVERT`, `PING`, and `ACK` are unreliable and consume no `seq`. Unacked frames are resent every 400 ms; a peer silent for 10 s is dropped.
  - Payloads:

    | Type | Payload |
    | --- | --- |
    | `ADVERT` | `gameId (len8, ≤ 32 B), pkgHash[8], manifestApi u8, hostApi u8, surface u32, seatsMax u8, seatsTaken u8, hostLabel (len8, ≤ 16 B)` |
    | `JOIN` | `pkgHash[8], hostApi u8, surface u32` |
    | `ACCEPT` | `seat u8, n u8`, sent to every guest when the host starts |
    | `MOVE` | `ver u16, move blob` |
    | `STATE` | `ver u16, turn u8 (0 when over), over u8, winners u16 bitmask, snapshot blob` |
    | `REJECT` | `ver u16, reason (len8, ≤ 64 B)` |
    | `ABORT` | `reason u8`: `left`, `script_error`, `timeout`, `full`, `version_mismatch` |
    | `PING` | empty |

  - `ver` is owned by `Session`: it increases with every snapshot for the life of the `session` id and never resets, including on rematch. `STATE` is latest-wins: the link may replace an unacked `STATE` with a newer one, and a guest ignores any `STATE` whose `ver` is not newer than its own.
  - The host draws the `session` id from `IRandom` when the lobby opens; frames with any other `session` are dropped. The host stops `ADVERT` when the match starts and answers a late `JOIN` with `ABORT(full)`.
  - Matching requires equal `proto` (which includes the codec version) and equal package hash. `hostApi` is each device's `API_LEVEL` and `surface` its `API_SURFACE_CRC` (AD-19). While either device's level is a preview, matching also requires equal `surface`, else `ABORT(version_mismatch)`. The host sets `ctx.api` to the lowest `hostApi` in the roster.
  - Only `ADVERT` is broadcast. Everything else is unicast to a peer registered on the STA interface. ESP-NOW v2, fixed channel 1, no encryption.

### AD-14: A script failure ends the session [ADOPTED]

- **Binds:** script-runtime, multiplayer-layer
- **Prevents:** half-recovered VMs and inconsistent error handling across callbacks.
- **Rule:** a `ScriptError` is any Lua error, budget breach, memory cap breach, codec limit breach, frame buffer overflow, unknown icon or image name, or invalid `status`. It ends the session: the VM is stopped (AD-5), a peer is sent `ABORT(script_error)`, the error is logged with `LOG_ERR`, and the match shows its error view: a short `tr()` message, the game name, the Lua message in small type, and a single Back control. There is no automatic retry. A `Cancelled` outcome shows nothing, **amended 2026-09-28 (owner):** except the 3 s watchdog's (AD-5).
  - **Amended 2026-09-28 (owner):** the error view also shows two failures that are not `ScriptError`s. A stuck script, cancelled by the 3 s watchdog, shows "The game stopped with an error" with a `tr()` "stopped responding" line in place of the Lua message (or the Lua message, if the script failed on its own meanwhile). A failure before the VM starts (the game's folder or sources missing, misnamed, unreadable, or too large, or memory for the store slot, the sources, the arena and frame buffers, or the task running out), or the arena's reserve having no room for the Session, shows "The game could not start" with a `tr()` reason in place of the Lua message; no game code has run. Running out of memory inside `LuaGame::load` (its scratch or the Lua state) is a `ScriptError` and shows "The game stopped with an error" with "not enough memory".

### AD-15: Package format and the one manifest parser [ADOPTED]

- **Binds:** package-install-launcher, first-party-games, api-docs
- **Prevents:** several package shapes and manifest dialects; path traversal and zip bombs; a package that installs but never lists.
- **Rule:**
  - A game is one `.cpgame` file: a zip (stored or deflate, no ZIP64) read through `lib/ZipFile`. Members are flat and whitelisted: `manifest.json`, `main.lua`, `[a-z0-9_]{1,32}.lua`, `[a-z0-9_]{1,32}.png` (non-interlaced; `icon.png` is the package icon, the rest are images for `ch.gfx.image`). Anything else, including directories, makes the package invalid.
  - Limits: package at most 256 KB, at most 32 members, each member at most 128 KB uncompressed, converted images at most 128 KB in total. The installer reads the EOCD entry count and rejects a package whose enumerated count differs; it checks each member's inflated size before extracting and verifies each CRC over the streamed output.
  - `require("name")` loads only `name.lua` from the package root.
  - `GameCore::Manifest::parse()` is the only manifest parser; the installer, registry, launcher, and lobby all call it. `Manifest::check(hostCaps)` reads only its argument and returns `Invalid(reason)` (a malformed package, rejected at install), `Unavailable(reason)` (well-formed but not startable on this host: `api` outside `minApi..api`, `seats.min` above `maxSeats`, or no mode this host supports), or `Ok` with at least one mode this host can satisfy. `GameCore::HostCaps {api, minApi, maxSeats, nearby}` is filled by one `src/games` provider from `ApiLevel.h` and the build; the installer, registry, launcher, and lobby all take it from there.
  - Manifest keys: `id` (matching `^[a-z0-9][a-z0-9-]{0,31}$`), `name`, `version`, `api` (integer ≥ 1), `seats {min, max}`, `modes` (non-empty, from `solo`, `pass`, `nearby`), `hidden` (default `false`), `icon` (optional library icon name, used when there is no `icon.png`). Unknown keys are ignored.

### AD-16: One installer, one registry, the SD inbox [ADOPTED]

- **Binds:** package-install-launcher
- **Prevents:** install paths that validate differently; half-installed or phantom games; reinstall deleting saves; two devices seeing different identities for the same game.
- **Rule:**
  - `GamePackageInstaller` is the only code that installs or removes games. In v1 its only entry point is the SD inbox: the launcher installs every `/games/*.cpgame` when it opens. Users put files there with the existing web file manager or USB. A web Games page is deferred; `/api/games*` is reserved for it.
  - It validates the package (AD-15), extracts to `/.games-tmp/<id>/`, converts `icon.png` to a 1-bit 64×64 `icon.bmp` and every other image to a 1-bit `<name>.bmp` at its own size with `PngToBmpConverter` (a failed conversion makes the package `Invalid`), removes any old `/.games/<id>/`, renames the new directory into place, and writes `.pkg` last as the commit marker. A leftover `/.games-tmp/` is deleted when the launcher opens.
  - `.pkg` holds `v1\n` followed by the 16 lowercase hex digits of the package hash and `\n`. The package hash is the first 8 bytes of a SHA-256 over the zip members sorted by name, each as `name \0 u32le(length) uncompressed-bytes`. `scripts/pack_game.py` computes the same value, and both pass a shared test vector. SHA-256 is reached through one helper in `src/games`.
  - The registry lists only directories under `/.games/` that have a valid `.pkg` and whose name equals `manifest.id`. There is no separate index.
  - An inbox file is deleted after a successful install. A failed file is renamed `*.cpgame.bad`, and the launcher shows the reason once.
  - Removing a game (from the launcher) deletes `/.games/<id>/` and keeps `/.games-data/<id>/`.
  - All file access goes through `Storage` / `HalFile`.

### AD-17: The runtime owns persistence [ADOPTED]

- **Binds:** script-runtime, multiplayer-layer, package-install-launcher
- **Prevents:** scripts writing arbitrary files; resume formats that differ between games; SD writes from the VM task; lost saved data when the VM stops first.
- **Rule:**
  - Scripts have no file API. `src/games/GameSaveStore` implements `ISnapshotStore` and is the only reader and writer of `/.games-data/<id>/resume.bin` and `store.bin`. It runs on the loop task and writes to a `.tmp` file, then renames.
  - `resume.bin` holds `{magic, fileVersion, codecVersion, pkgHash[8], mode, n, ver u16}` followed by the snapshot. It is written after every committed snapshot in `solo` and `pass` and deleted when the round ends. The launcher calls `GameSaveStore::peek()` to offer "Continue". A save whose package hash or codec version differs is discarded. `nearby` matches are not saved.
  - `ch.store` is one table per game per device. `get` works in every callback. `set` encodes and validates at once (AD-10) and posts the blob to a latest-wins PSRAM slot owned by the match; the store is dirty only when the bytes differ. `GameSaveStore` writes a dirty store at most every 5 s, at round end, and in the match's `onExit()` (including sleep), which is the only SD write allowed in `onExit()`. Writes made in `apply` land only on the authority.

### AD-18: Radio ownership [ADOPTED]

- **Binds:** multiplayer-layer
- **Prevents:** ESP-NOW and the web server fighting over Wi-Fi; a radio left on after a match; GameCore running on the Wi-Fi task; an upstream screen pushed over the match stalling the link.
- **Rule:**
  - One `NearbySession` owns Wi-Fi and ESP-NOW. Bring-up order: `WiFi.mode(WIFI_STA)`, set channel 1, wait for STA start, `ESP_NOW.begin`, register peers on the STA interface.
  - The ESP-NOW receive callback only copies frames into a fixed ring buffer in `EspNowLink`; it calls no `GameCore` code.
  - A dedicated `GameLink` task (priority 1, core 1, 4 KB stack) drains the ring buffer, runs `ReliableLink` timers, and exchanges session events with the `GameVM` task through queues. It keeps running whatever activity is on top (for example the upstream frontlight panel), so no `Activity::loop()` pumps the radio.
  - The lobby creates the session and passes it by move into `GameMatchActivity`'s constructor, switching with Replace; a moved-from lobby tears down nothing. It never coexists with the web server or any other Wi-Fi activity.
  - The lobby and a `nearby` match set `preventAutoSleep`.
  - Teardown never takes `RenderLock`. If internal heap does not recover after teardown, leaving the match to Home may `silentRestart()`; nothing restarts between rounds.

### AD-19: One API namespace, versioned [ADOPTED]

- **Binds:** all (script-runtime, multiplayer-layer, package-install-launcher, api-docs, first-party-games, the release workflow)
- **Prevents:** host functions scattered across globals; games silently running on a host too old, too new, or on a different preview of the same level; a level that changes after games depend on it.
- **Rule:** all host functions live under one reserved global table, `ch` (`ch.api`, `ch.screen`, `ch.gfx`, `ch.text_width`, `ch.timer`, `ch.store`, `ch.time`, `ch.log`). The API is versioned by an integer `api` level; the icon set (AD-24) is part of it.
  - **The surface list owns the level.** `docs/crosshatch/api-level-<n>.txt` lists what level n adds, one typed entry per line: `fn` (with signature), `enum` value, `event`, `ctx` field, `manifest` key, `limit`, `lib`, `icon`, `seats_max`. A host's surface is the union of the lists from `API_MIN_LEVEL` to `API_LEVEL`. A host test checks the live `ch` table, the Lua globals and libraries, enums, limits, and the icon table against that union, and checks `ch.d.lua` and the icon catalog against it; `game-api.md` describes it. `API_SURFACE_CRC` in `ApiLevel.h` is the CRC-32 of those lists, and the test recomputes it.
  - **Levels are cumulative.** A host runs every game with `API_MIN_LEVEL` ≤ `api` ≤ `API_LEVEL` unchanged. Levels only add. A breaking change (for example `LUA_32BITS`) needs a spine update that states the plan for older games: run them unchanged, or raise `API_MIN_LEVEL` so they show as unavailable.
  - **Freeze.** `lib/GameCore/ApiLevel.h` holds `API_LEVEL`, `API_MIN_LEVEL` (1), `API_LEVEL_FROZEN`, and `API_SURFACE_CRC`. `API_LEVEL_FROZEN` describes `API_LEVEL` only; every level below it is frozen. Level 1 is a preview: it may still grow, and a game written against it may break between builds. The last ticket of epic-first-party-games sets `API_LEVEL_FROZEN`, which closes v1; it never reverts, and the first fork release from a commit with it set is the freezing release. From then on a frozen level never changes, and any addition opens the next level as a preview. A `crosshatch-ci` job fails a PR that changes a frozen level's list against the merge base or turns `API_LEVEL_FROZEN` from true to false. **Amended 2026-09-28 (owner, retro F4):** after v1, a release needs only the levels below an open preview to be frozen. A fork release is refused only when a level that an earlier fork release shipped as frozen is no longer frozen here (`API_LEVEL` below it, or equal to it with `API_LEVEL_FROZEN` false). A release whose `API_LEVEL` is an open preview above every level released as frozen is allowed. (Pending the matching `fork_release.py` preflight change in the same PR.)
  - **Readers.** `ch.api` reports `API_LEVEL`. `Manifest::check` sees it through `HostCaps` (AD-15). The release reads `ApiLevel.h` from the commit it releases: its notes name the level ("1.6.5-ch.9 · Game API 1 (preview)"), and `pack-games` refuses a game whose `api` is outside `API_MIN_LEVEL..API_LEVEL`, using `scripts/pack_game.py`, the only Python reader of `manifest.json`. The firmware version (`-ch.N`, AD-25) carries no compatibility meaning. epic-script-runtime creates `ApiLevel.h`, level 1's list and its test, the CI job, and the release script's reading of the level.
  - **Mixed levels in one match.** In Play Nearby two previews must share a surface (AD-13). `ctx.api` is the lowest host level in the roster; `setup` and `apply` branch only on `ctx.api`, while `draw` and `input` may use the local `ch.api`.

### AD-20: The match activity owns the session [ADOPTED]

- **Binds:** script-runtime, multiplayer-layer
- **Prevents:** a pushed screen starving the match; teardown order races that lose `ABORT`; two owners of the VM or the radio.
- **Rule:** one `GameMatchActivity` (built on `UiAppHost` for its chrome) owns the `GameVM` task, the `Session` inside it, the `NearbySession` and its `GameLink` task, the frame buffers, the loaded game assets, and `GameViewport`, from the first `setup` or resume until exit. Result, hand-off, pause menu, end-of-round menu, "waiting for host", "player left", and the error view are **views inside it**, never separate activities.
  - A user exit (Leave, or Back from the error or "player left" view) runs from `loop()` in a `Leaving` state: queue `ABORT(left)`, flush the link for at most 800 ms, stop the VM (AD-5), tear down the radio, then `goToGames()`.
  - A forced `onExit()` (sleep, or any Replace) takes the best-effort path under the `RenderLock` it holds: send `ABORT(left)` once without flushing, cancel then join or abandon the VM, radio off, blank screen per AD-12, flush a dirty `ch.store` (AD-17).
  - The game canvas is a declared exception to `touch-and-ui.md`: canvas taps come from `touchSnapshotFrom` through `GameViewport` and bypass the FreeInkUI interaction table, which serves only the runtime views; the exception is recorded in `docs/crosshatch/`.

### AD-21: Match lifecycle [ADOPTED]

- **Binds:** script-runtime, multiplayer-layer, package-install-launcher
- **Prevents:** each screen author inventing what Back, Home, sleep, game over, and rematch do.
- **Rule:** the match follows this state machine. From every non-terminal state, an `ABORT` or 10 s of peer silence goes to `PeerGone`, a `ScriptError` goes to `Error`, and sleep takes the forced exit (AD-20). Every other transition not shown is invalid.
  - **Amended 2026-09-28 (owner):** a match starts in `Starting` until its VM task has started (then the initial state below) or a failure before that, in the match's `onEnter()`, sends it to `Error` (AD-14). A failure inside the VM task (the Session not fitting, `LuaGame::load`, `setup`) comes after that, so the match goes from `Playing` to `Error`. In `Paused`, Back closes the pause menu like Resume. The solo machine is `GameCore::MatchLifecycle` (`lib/GameCore/MatchLifecycle.h`).

  ```mermaid
  stateDiagram-v2
    [*] --> Lobby: nearby
    [*] --> HandOff: pass + hidden (new or resumed)
    [*] --> Playing: solo, open pass, nearby after lobby
    Lobby --> Playing: host starts (ACCEPT)
    Lobby --> Leaving: Cancel
    Playing --> Result: pass + hidden, turn seat changes
    Result --> HandOff: tap
    HandOff --> Playing: tap
    Playing --> Paused: Back or Home
    Result --> Paused: Back or Home
    HandOff --> Paused: Back or Home
    Paused --> Playing: Resume or Back
    Paused --> Leaving: Leave
    Playing --> Over: shipped status over
    Over --> HandOff: Play again, pass + hidden
    Over --> Playing: Play again, otherwise
    Over --> Leaving: Leave
    Error --> Leaving: Back
    PeerGone --> Leaving: Back
    Leaving --> [*]
  ```

  - Back and Home never leave a match directly; they open the pause menu (the match overrides `handleHomeGesture()`). Resume returns to the state that was paused.
  - Entering `Over` delivers the `over` event, deletes the resume save, and flushes `ch.store`. Only the authority offers Play again; a guest's end-of-round menu shows "waiting for host" and Leave.
  - Wake from sleep always lands on Home; resume goes through the launcher's "Continue".

### AD-22: Player journey [ADOPTED]

- **Binds:** package-install-launcher, multiplayer-layer, first-party-games
- **Prevents:** a flow a child can't finish alone (the brief's restaurant test).
- **Rule:** from Home, starting a `solo` or `pass` game takes at most 3 taps: Games, the game, and a mode (the mode step is skipped when a game offers one mode). The launcher is a paged `UiListActivity` of icon and name, with "Continue" first when a save exists. No v1 flow needs text entry. Runtime views use `GameIcons`, short `tr()` text, and large touch targets.

### AD-23: Built for simple games [ADOPTED]

- **Binds:** all
- **Prevents:** the engine and its API growing toward real-time play that e-ink and a 2 M-instruction budget can't serve, at the cost of the simple games it exists for.
- **Rule:**
  - The engine's scope is turn-structured games that work with pen and paper, a board, cards, dice, or words, plus puzzles and parlor games, in the spirit of CrossPlay's catalog. Real-time, action, physics, platformer, and animation-driven games are out of scope.
  - An API addition, at any level, must serve a game in scope. An addition whose main use is real-time play (a frame loop, sprites, scrolling, drag input, sub-second timers, sound) is rejected.
  - Performance targets are stated per move (a move answers within about 1 s, AD-6), never as frame rates.
  - `ch.timer.after(ms)` is the finest time source: at least 1,000 ms, one pending timer per device (a new call replaces it; `ch.timer.cancel()` clears it). It fires a `timer` event into `input` for the local seat, or the current turn seat in `pass`. A timeout that changes the game is returned as a move and goes through `apply`, never through `status` or the clock.

### AD-24: An icon library sets the design language [ADOPTED]

- **Binds:** script-runtime, first-party-games, api-docs, package-install-launcher
- **Prevents:** every game drawing its own suits, dice, pieces, and arrows in a different style; runtime screens and games looking unrelated.
- **Rule:**
  - `lib/GameIcons` is a fork-owned, curated icon set drawn from one source: Phosphor Icons (MIT), fill and regular weights, pinned at `@phosphor-icons/core` 2.1.1 (**amended 2026-09-28 (owner):** regular weight added, so outline marks such as `mark_x` and `mark_o` for Ultimate tic-tac-toe come from Phosphor's regular `x` and `circle`; the name map records each name's weight). The chosen SVGs are vendored in `assets/game-icons/` with Phosphor's license and recorded in `docs/crosshatch/`. A glyph Phosphor lacks is drawn as original work in the style of the weight it joins; icons from other libraries are not mixed in. A committed list in `assets/game-icons/` maps each crosshatch name to its source file, so a Phosphor rename or version bump never changes an API name. `scripts/gen_game_icons.py` renders each at 32 and 64 px as 1-bit bitmaps into a committed `GameIcons.generated.h`, which is never hand-edited.
  - Scripts draw icons with `ch.gfx.icon(name, x, y, size, color)`: `small` (32 px), `medium` (64 px), or `large` (128 px, the 64 px bitmap doubled). An unknown name is a script error.
  - Icon names are lowercase `snake_case` and part of the API level: a level only adds names, and never renames, removes, or redraws one into a different meaning. The v1 set covers marks, card suits, dice faces, board pieces, player markers, and common controls; the exact list is fixed in the icon library epic, which the launcher, runtime views, and Home tile need first; the API docs epic catalogs it.
  - The launcher, the runtime views, the Home cover-grid Games tile, and first-party games use the same set. A manifest may name a library icon as the game's icon instead of shipping `icon.png`.
  - Games may ship their own graphics as package images (AD-15), drawn at native size with `ch.gfx.image(name, x, y, color)`; an unknown name is a script error. Icons and images are drawn in `white` or `black` only.

### AD-25: Fork releases and the update source [ADOPTED]

- **Binds:** all (release pipeline, `OtaUpdater`, first-party game assets)
- **Prevents:** a fork device updating itself to upstream firmware without games; a released image that never recognises itself as installed and re-offers its own release forever; two owners, or two readings, of the fork build number; a clean upstream merge that silently reroutes or strands fork devices; releases that ship without their games or change every package hash.
- **Rule:**
  - **Version.** A fork version is `<upstream X.Y.Z>-ch.<N>`, for example `1.6.5-ch.7`, where `X.Y.Z` is upstream's `[crosspoint] version` at the released commit. Tags and the firmware parser share one grammar, `^(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)-ch[.]([1-9][0-9]{0,8})$` (at most 25 characters), matched against the whole string. `test/game_core/fork_version_vectors.json` owns it: its `tag_grammar` is normative, and `ForkRelease.h` and `fork_release.py` are tested against its vectors; the release script and its checks come from the workflow's own commit, but the data describing the firmware, these vectors and `ApiLevel.h` (AD-19), come from the commit it releases. The asset-name capacity (48 B, the size of upstream's buffers in `OtaUpdater.cpp` and `ReleaseJsonParser.h`) is a `ForkRelease.h` constant mirrored in the vectors, tied to `OtaUpdater.cpp`'s buffer by a guarded `static_assert`. A running version yields `N` only when a prefix matches the grammar and is followed by the end of the string, `-`, or `+`; anything else, every development build included, is `N = 0`, so any release is offered to it. Release images report exactly the tag through `CROSSPOINT_VERSION` from the rewritten version line; no second `-DCROSSPOINT_VERSION` is passed. Assets are `crosspoint-<tag>-<board>.bin` and `<id>.cpgame`.
  - **Newer.** `N` strictly increases across fork releases and never resets, including when the upstream base changes; gaps are allowed. Only `N` decides whether a release is newer. v1 makes no fork prereleases.
  - **Update source.** One pure fork-only header, `lib/GameCore/ForkRelease.h`, holds the fork release URL (`CarpeTelam/crosshatch-player` releases/latest), the asset-name function, and the `N` parse and compare, and is tested in `test/game_core`. `OtaUpdater.cpp` calls it only inside its row-10 `#if FREEINK_CAP_GAMES` guards; `OtaUpdater.h` is unchanged. A 404 from releases/latest is `NO_UPDATE`. `HttpDownloader` reports every non-200 final status (after redirects) as a bare failure and is not in the ledger, so after a failed fetch `OtaUpdater.cpp` asks the fork-only `src/games/ForkReleaseProbe`, which requests the URL once more and reads the status; any other failure stays `HTTP_ERROR`. The probe is device-only (AD-2 simulator rule). It is the only fork code that makes HTTP requests in v1 and takes its URL only from `ForkRelease::LATEST_RELEASE_URL`. Its timeout, redirect limit, TLS mode, and user agent are recorded beside ledger row 10 in `docs/crosshatch/upstream-touches.md`, and an upstream merge touching `HttpDownloader.*`, `[base]` `build_flags`, or the `freeink-sdk` pointer (two of the values are SDK defaults) re-checks them; the dry run alone does not. Builds without the flag keep upstream's source and comparison.
  - **Boards.** The fork releases exactly the `*-gh_release` envs that set `FREEINK_CAP_GAMES=1` (today `x4pro-gh_release` and `sticky-gh_release`). Every other board (C3, x4c, papermono) follows upstream. Adding the flag to an env is an AD-2 and AD-25 change together.
  - **Release workflow.** One fork-only `workflow_dispatch` workflow, owned by epic-platform-baseline, makes every fork release; no release is published by hand. It runs in one concurrency group without cancelling, on `develop` HEAD or an ancestor ref. In order it: fails if another active workflow triggers on `release` or builds a `*-gh_release*` env; sets `N` to one more than the largest `-ch.N` in any tag, read through the API; checks that upstream's version line matches `X.Y.Z`, rewrites it in its checkout only, and builds; checks that each image contains its tag and the fork URL and not upstream's; packs every `games/<id>/` with `scripts/pack_game.py` byte-for-byte, zero games being valid and a failed package failing the run; then pushes the tag without force, creates a draft release with `GITHUB_TOKEN`, uploads every asset, lists each package hash in the notes, and publishes with `make_latest=true`.
  - **Tags.** A repository tag ruleset forbids deleting `*-ch.*` tags and lets only the workflow create them; where a ruleset cannot single out the workflow, repository admins are its only bypass. A bad release is withdrawn by releasing `N+1` from a good commit. `N` counts every `-ch.N` tag, so a hand-made or malformed one is never reused; removing one needs an admin to lift the ruleset.
  - **Upstream workflows.** Upstream's `release.yml` and `release_candidate.yml` are disabled in the fork's Actions tab, never edited. An upstream merge that touches `OtaUpdater.*`, `HttpDownloader.*`, `ReleaseJsonParser.*`, `FirmwareBoardTag.*`, a release workflow, or a `*-gh_release` env is not done until a dry run of the fork release workflow (build and checks, no tag) passes.

## Consistency Conventions

| Concern | Convention |
| --- | --- |
| C++ naming and files | Follow upstream style: PascalCase types and files, one class per file pair; fork code only in new files under the dirs in the Structural Seed. |
| Lua API naming | `snake_case` functions, fields, and icon names; seats are 1-based integers (0 = everyone at game over in `pass`); colors, sizes, refresh modes, and event kinds are lowercase strings. |
| Input events | `{kind = "tap" \| "long_press", x, y}`, `{kind = "swipe", x, y, dir}` (start point plus `"left" \| "right" \| "up" \| "down"`), `{kind = "rejected", reason}`, `{kind = "over"}`, `{kind = "timer"}`. Edge gestures belong to the runtime and never reach scripts: Back (right-swipe from the left 25%) and Home (up-swipe from the bottom 14%) open the pause menu; Menu (down-swipe from the top) opens the device's light panel on frontlit boards. |
| Errors | C++: `bool` or enum results, no exceptions. Lua: `apply` rejects with `nil, reason`; host API misuse raises a Lua error. Runtime text goes through `tr(STR_GAMES_*)`. |
| Logging | `LOG_ERR` / `LOG_INF` / `LOG_DBG` with tags `GAME`, `LUA`, `LINK`; `ch.log` and `print` map to `LOG_INF` tagged with the game id. |
| Memory | VM arena, frame buffers, loaded game assets, and queue payloads in PSRAM; fixed-size receive buffers; `makeUniqueNoThrow` elsewhere. `lib/lua` is exempt from the 256 B locals rule because it runs only on the 16 KB `GameVM` stack. The installer streams entries (`readFileToStream`), never whole members into memory. |
| Strings and i18n | Runtime keys prefixed `STR_GAMES_`, added to `english.yaml` only; game text is the game's own. |
| Screens | Launcher and mode picker on `UiListActivity`; lobby and `GameMatchActivity` on `UiAppHost` (AD-20 exception for the canvas). |
| SD layout | `/games/` inbox, `/.games/<id>/` installs, `/.games-tmp/` staging, `/.games-data/<id>/` saves. Nothing under `/.crosspoint/`. |
| Units and limits | Every KB and MB in this spine is KiB and MiB (1 KB = 1,024 B). Each limit is one named constant; where Python tooling also enforces it, the constant is mirrored in a vector file with cases at the limit and one over. |
| Fork scripts | `scripts/` fork tools share `scripts/fork_common.py` for the exit contract (0 pass, 1 fail, 2 could not run), git calls, and step summaries; it is created before the next fork script lands. Each has a sidecar `<name>_test.py`, and one `crosshatch-ci.yml` step runs every `scripts/*_test.py` on every PR. |
| Docs | Fork docs in `docs/crosshatch/` (`upstream-touches.md`, `formats.md`, `game-api.md`, icon catalog and attributions), never in upstream docs. The game API reference is self-contained, with a LuaLS `---@meta` stub for `ch`, so both move to the starter repo unchanged. |

## Stack

| Name | Version |
| --- | --- |
| C++ / C | C++20 with `-fno-exceptions`; Lua built as C |
| Lua | 5.5.1 (PUC, vendored) |
| pioarduino platform | 55.03.311 (Arduino-ESP32 3.3.11, ESP-IDF 5.5.5), follows upstream's pin |
| PlatformIO Core | pioarduino 6.1.19 |
| ESP-NOW | v2 (1,470 B max payload, 20 peers) |
| Zip / inflate, PNG | in-tree `lib/ZipFile` + `lib/miniz`, `lib/PngToBmpConverter` |
| Icons | Phosphor Icons 2.1.1 (`@phosphor-icons/core`, MIT), fill and regular weights (amended 2026-09-28, AD-24) |
| SHA-256 | mbedTLS (bundled with ESP-IDF 5.5) on device, OpenSSL in the simulator, behind one helper |
| Host tests | GoogleTest 1.17.0 via CMake, follows upstream's pin |

## Structural Seed

Task view of a match:

```mermaid
flowchart LR
  subgraph WiFiTask["Wi-Fi task (core 0)"]
    RX["ESP-NOW receive callback<br/>→ fixed ring buffer"]
  end
  subgraph LinkTask["GameLink task (core 1, prio 1, 4 KB)"]
    NS["NearbySession<br/>(EspNowLink + ReliableLink)"]
  end
  subgraph LoopTask["Arduino loop task"]
    MA["GameMatchActivity loop()<br/>(asset load, input builder, views, frameGen poll)"]
    SS["GameSaveStore"]
  end
  subgraph VMTask["GameVM task (core 1, prio 1, 16 KB)"]
    %% Amended 2026-09-28 (owner): SoloRounds and RefreshPolicy named (retro A2).
    VM["LuaGame (lua_State, codec, bindings);<br/>SoloRounds (the solo round loop)"]
    SE["GameCore::Session (roster, snapshot, ver)"]
  end
  subgraph RenderTask["ActivityManagerRender task"]
    RP["FrameReplay (RefreshPolicy) → GfxRenderer (under RenderLock)"]
  end
  RX --> NS
  NS -- "MOVE / STATE / REJECT (queue of PSRAM handles)" --> SE
  SE -- "outbound frames" --> NS
  MA -- "assets at start, input events (queue)" --> VM
  SE -- "snapshots to save, store blob, outcomes" --> MA
  MA --> SS
  VM -- "front/back swap under frame mutex, frameGen++" --> RP
  SE <--> VM
```

A Play Nearby turn, guest to host:

```mermaid
sequenceDiagram
  participant G as Guest (seat 2)
  participant H as Host (seat 1, authority)
  G->>G: input(state, 2, ui, tap) → move (further moves held)
  G->>H: MOVE {ver, move}
  H->>H: peer → seat 2, shipped turn == 2? apply(decode(snapshot), 2, move)
  alt accepted
    H->>H: encode → snapshot, ver+1, status
    H->>G: STATE {ver+1, turn, over, winners, snapshot}
    G->>G: draw(state, 2, ui)
  else rejected
    H->>G: REJECT {ver, reason}
    G->>G: input(state, 2, ui, {kind = "rejected"})
  end
  H->>H: draw(state, 1, ui)
```

Source tree (**amended 2026-09-28 (owner):** the names epic-script-runtime built; the rest is still the plan):

```text
lib/
  lua/                    # Lua 5.5.1, unmodified; fork-owned library.json (srcFilter) and .clang-format (DisableFormat)
  GameCore/               # Roster, Session, Protocol, ReliableLink, Manifest, ports (IGameRules, ILink, IClock,
                          # IRandom, ISnapshotStore, IGameLog); GameEvent.h (input events), HostCaps.h, MatchLifecycle (AD-21);
                          # ApiLevel.h (AD-19); ForkRelease.h (AD-25, pure, header-only)
  GameIcons/              # GameIcons.generated.h (names + 32/64 px 1-bit bitmaps)
  GameScript/             # LuaGame (the VM host), ArenaAllocator, CallGuard (budget and stack hook), Sandbox, ChBindings (ch.*),
                          # Codec, BlobHeader, DisplayList, FrameBuffers, CanvasClip, TextMetrics, RefreshPolicy (AD-7),
                          # GameInput, GameSources, GameTimer, StoreSlot, SoloRounds (the solo round loop)
src/
  games/                  # GameVM (the task, AD-5), GameArena (PSRAM arena backend), GameAssets (source/image/store loader),
                          # FrameReplay, GameViewport, GameTouch.h (touch to input events), GameSaveStore, GameHostCaps,
                          # MatchStore (amended 2026-09-28, retro AI-5: one match's ch.store, its StoreSlot and
                          # GameSaveStore in one PSRAM block, so Screens name no GameScript type),
                          # GameClock, GameRandom, GameLog (port providers); GameLink task, EspNowLink, NearbySession,
                          # GamePackageInstaller, GameRegistry, Sha256 helper,
                          # GamesBuildAnchor.cpp (AD-2: makes every env compile the game libraries and lua via lua.hpp),
                          # ForkReleaseProbe (AD-25: reads the releases/latest status after a failed fetch; device-only)
  activities/games/       # GamesListActivity (the minimal Games list; epic-install-and-launcher replaces it with
                          # GamesLauncherActivity), GameModeActivity, GameLobbyActivity, GameMatchActivity
assets/game-icons/        # vendored Phosphor fill and regular SVGs, name map, original additions, license
games/<id>/               # first-party game sources (manifest.json, main.lua, *.png)
scripts/pack_game.py      # games/<id>/ → <id>.cpgame, validates, prints package hash
scripts/game_codec.py     # reference codec for golden vectors and tooling
scripts/gen_game_icons.py # assets/game-icons/*.svg → lib/GameIcons/GameIcons.generated.h
scripts/check_upstream_touches.py  # AD-3 ledger check and trial merge; exit 0 pass, 1 fail, 2 could not run
scripts/check_flash_budget.py      # games-on/off x4pro build pair, the 250 KiB and 1 KiB static-RAM compares, game objects
scripts/check_api_freeze.py        # AD-19 frozen-level check against the merge base
scripts/check_layers.py            # amended 2026-09-28 (retro AI-5): the layer table above as data; fails a disallowed
                                   # #include edge and a GameScript name in Screens
scripts/fork_common.py             # the fork scripts' shared exit contract, repository calls, and step summaries
scripts/fork_release.py            # AD-25 release steps: preflight, prepare, build, check-images, pack-games, notes,
                                   # expected-assets, recheck (never reuses N)
                                   # (each fork script has a sidecar <name>_test.py; crosshatch-ci.yml runs them on every PR)
test/game_core/           # host suites incl. FakeLink, protocol, manifest, hash vector, ForkRelease;
                          # fork_version_vectors.json: the AD-25 tag grammar and cases, read by the C++ suite and fork_release.py
test/game_script/         # host suites incl. Lua on host, codec golden vectors, sandbox cases
docs/crosshatch/          # upstream-touches.md, formats.md, game-api.md + ch.d.lua, icon catalog, api-level-<n>.txt (AD-19)
.github/workflows/        # fork-only, new files: crosshatch-ci.yml (ledger, flash budget, simulator build, fork script
                          # tests, API freeze, rolled up under the required Crosshatch Test Status job); crosshatch-release.yml (AD-25)
.claude/skills/run-crosshatch-player/simulator.ini   # fork-owned; simulator envs set FREEINK_CAP_GAMES
```

On the SD card:

```text
/games/*.cpgame           # install inbox; *.cpgame.bad after a failed install
/.games/<id>/             # installed package: manifest.json, *.lua, icon.bmp, <image>.bmp, .pkg
/.games-tmp/<id>/         # install staging, deleted on launcher open
/.games-data/<id>/        # resume.bin, store.bin
```

Operational envelope:

| Concern | v1 answer |
| --- | --- |
| Firmware delivery | The fork release workflow (AD-25) publishes `X.Y.Z-ch.N` firmware for the game envs; fork devices update over the air from the fork's releases, or by SD. Moving from upstream firmware to the fork, and back, is by SD from a release asset. |
| Game delivery | `.cpgame` files. First-party games are attached to each fork release by the fork release workflow (AD-25) and installed through the inbox like any other game. |
| CI | The existing PR workflow builds all five envs and runs the host suites, including `test/game_core` and `test/game_script`; **amended 2026-09-28 (owner):** the fork-only jobs in `crosshatch-ci.yml` are `Upstream touch ledger`, `x4pro flash budget` (flash, static internal RAM, and the game-objects check), `Simulator build` (`simulator_x4pro` and `simulator_sticky`), `Fork script tests`, and `API freeze`, rolled up under `Crosshatch Test Status`; branch protection requires `Test Status` and `Crosshatch Test Status`. |
| Flash budget | The whole runtime (Lua, GameCore, GameScript, GameIcons, screens) adds at most 250 KB to the x4pro image (baseline 86.3% of the app slot; Lua alone measured +121 KiB (124,232 B); icons about 40 KB for 64 icons at two sizes). A fork-only CI job measures it as the x4pro image with `FREEINK_CAP_GAMES` on minus the same commit with it off, so upstream growth never counts against it. The 250 KB limit is 250 KiB (256,000 B), since flash and the app slot are sized in binary units; the gate compares `firmware.bin` sizes. The delta includes guarded `#else` branches in ledgered upstream files (at the end of epic 1 it was −15,136 B, since games builds drop upstream's version compare); each epic records its delta. |
| Internal RAM | The `GameVM` (16 KB) and `GameLink` (4 KB) stacks and the Wi-Fi/ESP-NOW driver are the internal-RAM costs; everything else is in PSRAM. A `nearby` lobby refuses to open below 100 KB free internal heap. |
| Observability | Serial log (`GAME`, `LUA`, `LINK`) and the match error view; no telemetry. |
| Security | Sandboxed scripts (AD-6), text-only chunks, validated flat packages (AD-15), jailed files (AD-16, AD-17), unencrypted radio in a cooperative room (AD-13). |

## Capability → Architecture Map

| Capability / area | Lives in | Governed by |
| --- | --- | --- |
| Script runtime (sandbox, touch, drawing, refresh, timers, error isolation) | `lib/GameScript`, `lib/lua`, `src/games` (FrameReplay, GameViewport, arena, GameAssets) | AD-4 to AD-8, AD-14, AD-19, AD-23 |
| Icon library and game graphics | `lib/GameIcons`, `assets/game-icons/`, `scripts/gen_game_icons.py`, installer image conversion | AD-15, AD-16, AD-24 |
| Multiplayer layer (roster, pass-and-play, Play Nearby, hand-off, rematch) | `lib/GameCore`, `src/games` (GameLink, EspNowLink, NearbySession), `GameMatchActivity` | AD-8 to AD-13, AD-18, AD-20, AD-21 |
| Package, install, launcher | `src/games` (installer, registry), `src/activities/games`, Home hook | AD-2, AD-3, AD-15, AD-16, AD-19, AD-22 |
| Game persistence | `lib/GameCore` (ISnapshotStore), `src/games/GameSaveStore` | AD-10, AD-17, AD-21 |
| First-party games: a solo puzzle, an open-information 2P game, a hidden-information 2P game (titles chosen in the games epic) | `games/<id>/`, `scripts/pack_game.py`, release assets | AD-8, AD-10, AD-15, AD-22, AD-23, AD-24 |
| API docs for AI authors | `docs/crosshatch/game-api.md` + `ch.d.lua` + icon catalog (seeded by `game-api-seed.md`) | AD-8, AD-19, AD-23, AD-24 |
| Clean upstream merges | the upstream-touch ledger and its CI check | AD-2, AD-3 |
| Fork releases and over-the-air updates | fork release workflow, guarded `OtaUpdater` change | AD-3, AD-25 |
| The restaurant test | launcher, `GameMatchActivity` views | AD-21, AD-22 |

## Deferred

| Item | Why it can wait |
| --- | --- |
| Web Games page (upload, validate, delete) | v1 installs through the SD inbox; `/api/games*` is reserved, and adding it costs `CrossPointWebServer.cpp` plus nav pages in the ledger. |
| Dirty-region refresh; true grayscale; image scaling | Deliberate v1 trims of the brief; each is an additive `api` change within AD-23's scope. |
| `LUA_32BITS` | Changes integer width for games, the codec, and saves, so it breaks older games; needs a spine update with a plan for them (AD-19) and a `proto` bump. |
| `-O2` for Lua; trusted bytecode for first-party games | Tuning; bytecode needs its own trusted path, since AD-6 rejects binary chunks. |
| `coroutine` library | Not needed by v1 games; additive later. |
| More than 2 seats in the lobby and UI; simultaneous turns | The roster allows N; v1 ships 2 and sequential turns. Party games for 3 to 8 players are the first use. |
| State over 1,400 B (fragmentation) | Only if a real game hits the cap. |
| Reconnect or rejoin after a drop | v1 ends the match after 10 s of silence. |
| Save migration across package versions | v1 discards saves when the package hash changes. |
| Simulator fake link for Play Nearby | `FakeLink` host suites cover link logic; a simulator link is later tooling. |
| Aligning with upstream "web plugins" | No upstream code exists; revisit if it ships. |
| Starter repo | Post-v1; the API docs, LuaLS stub, and icon catalog are written to move there unchanged. |
| Fork prereleases; a fork web flasher; fork C3 firmware | v1 releases only the game envs, makes no prereleases, and moves devices by SD. |
| Bumps to pioarduino 55.03.312+ and GoogleTest 1.18 | Follow upstream's pins through merges. |
| Open: ESP-NOW reliability, battery cost, Sticky and mixed-device behaviour | Measure between two devices before tuning 400 ms / 10 s and the 100 KB threshold. Only one device is on hand, so this waits for the second and blocks nothing: link and session code is built against the `FakeLink` suites until then. |
| Open: internal heap after ESP-NOW teardown | Measurable on one device (radio up and down, no peer); AD-18 already bounds where a `silentRestart()` may happen. |

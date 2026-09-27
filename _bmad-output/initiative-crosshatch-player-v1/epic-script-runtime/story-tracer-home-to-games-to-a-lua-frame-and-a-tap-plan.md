---
title: 'Tracer: Home to Games to a Lua frame and a tap'
type: 'feature'
ticket: '1'
created: '2026-09-27'
status: 'built'
baseline_revision: '799d5add2829300d2fe7368a5b3ba63790a67808'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
  - '{project-root}/docs/contributing/touch-and-ui.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** No path yet runs a Lua game: Home has no Games entry, nothing parses a manifest, and no VM, frame buffer, or refresh path exists for later runtime stories (2.4, 2.6 to 2.14) to extend.

**Approach:** A thin end-to-end slice through every layer of the spine: guarded Home list-mode item (ledger rows 4 to 7) to a `GamesListActivity` of hand-placed `/.games/<id>/` games, to a `GameMatchActivity` that loads the sources into PSRAM, runs `LuaGame` on the `GameVM` task in a 256 KB arena, and replays its display list through `GameViewport` and `FrameReplay`; a tap reaches `input` and redraws.

## Boundaries & Constraints

**Always:**
- AD-2: every `src/games` and `src/activities/games` `.cpp` whole-file `#if FREEINK_CAP_GAMES`; upstream edits only in ledger rows 2 and 4 to 7, each guarded (row 2 append-only); game statics `constexpr`/`constinit`, none mutable over 64 B; `lib/Game*` build for every env.
- `GameCore` includes no Lua/HAL/Arduino/`src` header; `GameScript` no HAL/Arduino/`GfxRenderer`/`Logging` (it returns outcomes and messages). Files that include `lua.h` include `<climits>` first and `static_assert(sizeof(lua_Integer) == 8)`.
- AD-5/6/7 as far as this slice goes: `GameVM` task core 1, priority 1, 16 KB; it never takes `RenderLock`, calls `ActivityManager`, or touches `Storage`; every Lua entry (open, load, `setup`, `draw`, `input`) runs inside one `lua_pcall` trampoline; input queue depth 8, drop-oldest with a log line; frame swap under the frame mutex bumping `frameGen`; lock order `RenderLock` then frame mutex; full refresh per frame.
- Allocation: `makeUniqueNoThrow` / `new (std::nothrow)` with null checks; arena, frame buffers, and sources in PSRAM via `HalMemory::allocatePsram`; locals under 256 B; `tr(STR_GAMES_*)`; `LOG_*` tags `GAME`/`LUA`.
- Fixture only in `test/game_script/fixtures/`.

**Never:** cover-grid Home tile (rows 8, 9); `ApiLevel.h`, `HostCaps`, `Manifest::check` (2.4); codec, `Session`, `apply`, `status` (2.6, 2.8); count hook, cancel, abandon, sandbox stripping, `require` (2.7); `line`/`circle`/`refresh`/`align`/`ch.screen`/`text_width`, light/dark (2.9); timers, store (2.10, 2.12); refresh escalation, long press, swipe (2.11); pause/over/error views (2.13); moving `freeink-sdk`; editing `.skills/` or `ci.yml`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Valid manifest | every AD-15 key plus unknown scalar/object/array keys | `Manifest` filled; unknown keys ignored; `hidden` defaults false | — |
| Invalid manifest | bad `id`, missing required key, wrong type, `api` < 1 or non-integer, `seats.min` > `max` or < 1, empty/unknown `modes`, duplicate key, not an object, truncated | parse fails with a `ManifestError` | game skipped with `LOG_INF` |
| Dir/id mismatch | `/.games/foo/` holding `"id": "bar"` | not listed | `LOG_INF` |
| No games | `/.games/` missing or empty | list shows `STR_GAMES_EMPTY` | — |
| Open fixture | tap tracer row | frame drawn full-refresh; tap adds to its counter | — |
| Script error | error in `setup`/`draw`/`input`, `ch.gfx` outside `draw`, missing `main.lua`, non-table result | VM ends; match shows `STR_GAMES_ERROR` + Lua message; Back to Games | `LOG_ERR` |
| No PSRAM / OOM | arena, frames, or sources allocation fails | same error screen, "out of memory" detail | `LOG_ERR` with size |
| Input burst | more than 8 taps queued | oldest dropped | `LOG_INF` |
| Leave | Back, Home gesture, or sleep | VM quits and joins within 500 ms, memory freed | join timeout: `LOG_ERR`, VM memory leaked on purpose (2.7 adds abandon) |

</frozen-after-approval>

## Code Map

- `src/activities/ActivityManager.{h,cpp}` -- `HomeMenuItem` (one-line enum), `goTo*` wrappers with `makeUniqueNoThrow` + OOM log (`goToLibrary`), `goHome` name-to-item mapping.
- `src/activities/home/HomeActivity.{h,cpp}` -- static `menuItemToIndex`/`indexToMenuItem`, `getMenuItemCount`, `loop` switch, list-mode `render` menu vectors; cover-grid mode shares the count and index helpers, so Games must stay out of it.
- `UITheme::hasCoverGridHome()` (static) -- the list/cover-grid predicate.
- `src/activities/UiListActivity.h`, `settings/LanguageSelectActivity.cpp` (skeleton), `home/FileBrowserActivity.cpp` (`loadFiles` two-pass dir scan, `screen.centeredText` empty state) -- Games list model.
- `src/components/UiAppHost.h`, `UiAppHelpers.h` `touchSnapshotFrom` -- match host and canvas tap source (AD-20).
- `lib/hal/HalMemory.h` `allocatePsram` -- PSRAM on device and simulator; `lib/hal/HalStorage.h` `open`/`openNextFile`/`getName`/`read`.
- `lib/GfxRenderer/GfxRenderer.h` -- `fillRect`, `drawRect`, `drawText` (y = line top), `getClipRect`/`setClipRect`, `getOrientedViewableTRBL`, `displayBuffer(HalDisplay::FULL_REFRESH)`; `src/fontIds.h` `UI_12_FONT_ID`.
- `lib/JsonParser/StreamingJsonParser.h` -- callback parser under the manifest reader; lax (no comma or trailing-garbage checks), 512 B token buffer that drops longer tokens.
- `lib/lua` -- 5.5.1; `lua_newstate(alloc, ud, seed)`; no `linit.c`, so libraries open via `luaL_requiref`; `lua_getextraspace` holds a pointer.
- `test/game_core/CMakeLists.txt`, `test/game_script/CMakeLists.txt` -- add sources; `lua_vendored` target exists.
- `src/games/GamesBuildAnchor.cpp` -- keep; the `libraryName()` skeletons stay (deferred to 2.15).
- Simulator (`crosspoint-simulator@8699595`): FreeRTOS shim in `src/freertos/*.h`, `esp_heap_caps.h`.

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameCore/Manifest.{h,cpp}`, `IRandom.h` -- `Manifest` (fixed fields: `id` ≤ 32, `name` ≤ 64 B, `version` ≤ 32 B, `icon` ≤ 32 B; `api`, `seatsMin`, `seatsMax`; `modes` bitmask; `hidden`), streaming `ManifestReader` (feed/finish) and `Manifest::parse(string_view)`, `ManifestError` + `describe`; `IRandom` port.
- [ ] `lib/GameScript/{ArenaAllocator,DisplayList,FrameBuffers,GameInput,GameSources,ChBindings,LuaGame}.{h,cpp}` -- per Design Notes.
- [ ] `test/game_core/ManifestTest.cpp`, `test/game_script/{ArenaAllocatorTest,DisplayListTest,FrameBuffersTest,GameInputTest,LuaGameTest}.cpp`, both `CMakeLists.txt`, `test/game_script/fixtures/tracer/{manifest.json,main.lua}` -- I/O-matrix manifest cases; arena alloc/free/realloc/coalesce/exhaustion/alignment and zero use after `lua_close`; command round trip and both caps; drop-oldest; `LuaGame` `setup`+`draw` and tap-then-`draw` through the trampoline, each script-error row.
- [ ] `src/games/{GameRandom,GameArena,GameAssets,GameViewport,FrameReplay,GameVM}.{h,cpp}` -- device adapters and the task.
- [ ] `src/activities/games/{GamesListActivity,GameMatchActivity}.{h,cpp}` -- the two screens.
- [ ] `lib/I18n/translations/english.yaml` -- append `STR_GAMES_TITLE`, `STR_GAMES_EMPTY`, `STR_GAMES_ERROR`.
- [ ] `ActivityManager.{h,cpp}`, `HomeActivity.{h,cpp}` -- guarded `HomeMenuItem::GAMES`, `goToGames()`, `goHome` mapping for `GamesList`/`GameMatch`, `showsGamesItem()` (list mode only), Games row (reused `Blocks` icon) before Settings.
- [ ] `.claude/skills/run-crosshatch-player/SKILL.md` -- Home landmark row gains Games.

**Acceptance Criteria:**
- Given the simulator (`simulator_x4pro`) with the fixture copied to `fs_/.games/tracer/`, when Home → Games → Tracer is tapped and then the canvas, then screenshots show the list, the frame, and the counter at 1 with a square at the tap.
- Given `cmake`/`ctest` on `test/`, then every suite passes; given `pio run -e x4pro` and `-e default`, then both build.
- Given the flash gate commands, then the games-on minus games-off `firmware.bin` difference is recorded below.
- Given the commit, `check_upstream_touches.py` exits 0 and `./bin/clang-format-fix` leaves no diff.

## Design Notes

**Seams for later stories.**
- `GameCore::Manifest` is the only parser (installer, lobby reuse it). Duplicate keys are invalid so `pack_game.py` (last-wins JSON) must reject them too. The four string caps are new level-1 `limit`s for 2.4's list.
- `GameCore::IRandom` seeds `lua_newstate` now; 2.7 also seeds `math.random`. Other ports (`IGameRules` 2.8, `IClock` 2.10, `ISnapshotStore` 2.12, `ILink`) arrive with their users.
- `ArenaAllocator`: boundary-tag first-fit over one caller block (`reset(base, size)`), `luaAlloc` counting `bytesInUse`/`peak`; the block is the port, so `GameArena` (PSRAM) and tests (`malloc`) differ only there. 2.7 adds the cap policy on the counter.
- `DisplayList`: packed commands (`op` byte, int16 coordinates, color/size bytes, text as u16 length + bytes + NUL) over caller storage, `MAX_COMMANDS` 2048 and `MAX_BYTES` 32 KiB; a `Reader` yields `DrawCommand`. 2.9 adds opcodes and the frame hint; append failure already raises a Lua error.
- `FrameBuffers`: two lists, `std::mutex`, atomic `frameGen` and `inSwap`; VM writes `back()` and calls `publish()`, render uses `readFront(fn)`.
- `InputQueue` (`GameInput.h`): fixed ring, `push` reports a drop; `InputEvent{kind, x, y, dir}` with `Tap` only; 2.10 posts `Timer`, 2.11 adds `LongPress`/`Swipe`.
- `GameSources`: view of `SourceSpan{name, offset, length}` plus text; `find("main")`; 2.7's `require` resolves here.
- `ChBindings`: builds `ch` from `constexpr luaL_Reg` tables; C functions reach a `BindingContext` (draw target now; later metrics, clock, timer, store slot) through `lua_getextraspace`, never `LuaGame`.
- `LuaGame`: `start()` (open base/table/string/math/utf8, `ch`, load `@main.lua` in text mode, run it, require a table, `setup({seats=1, mode="solo"})`, make seat 1's `ui`), `draw()` (publish only on success), `input(ev)` (move ignored until 2.8), `close()`. Each goes through `enter(Entry)`: push message handler, trampoline, light userdata, `lua_pcall`; nothing that can raise runs outside it. Returns `Outcome{Ok, ScriptError}` plus a 160 B message; 2.7 adds `Cancelled` and the hook around the same `pcall`.
- `GameVM` owns everything the task touches (assets, arena, frame storage, queue, `LuaGame`), so 2.7's abandon frees one object. Task: `start`, `draw`, then wait on the task notification; per event `input` then `draw`; on quit or error `close`, log the stack high-water mark, set `finished`, `vTaskDelete(nullptr)`. `stop(ms)`: quit, notify, poll `finished`.
- `GameMatchActivity` (`Activity` + `UiAppHost`, whose views 2.13 adds): `onEnter` loads assets and starts the VM; `loop` maps Back to `goToGames()`, taps through `GameViewport` to `postInput`, polls `frameGen` and `finished`; `render` skips until the first frame, replays under the frame mutex, then refreshes outside it; `onExit` stops the VM. `GameViewport` is the logical screen minus `getOrientedViewableTRBL` (portrait), used by replay (clip, offset) and taps; `FrameReplay` is a class so 2.11 can keep its counter.

**Simulator answer (the entry's unknown).** The pinned simulator's shim makes `xTaskCreatePinnedToCore` a `std::thread` (core, priority, and stack ignored, so the 16 KB budget is checked only on the device), `xTaskNotify`/`ulTaskNotifyTake` a condvar counter, and `vTaskDelete(nullptr)` a no-op, so the task function returns to end. The shim has no queues and no `vSemaphoreDelete`, so the queue is a mutex ring woken by task notifications and nothing is created per match. `vTaskDelete(other)` only detaches a thread and cannot kill it, so 2.7's abandon cannot stop a stuck script in the simulator. PSRAM: `HalMemory::allocatePsram` is `malloc` when `BOARD_HAS_PSRAM` is set; `simulator_x4pro` sets it, `simulator_sticky` does not, so 2.11 adds it there. Host tests use a `malloc`'d block.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `pio run -e default` -- SUCCESS.
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare` -- difference recorded.
- `sim.sh setup`, `build x4pro`, `start x4pro`, `tap`, `ss` -- screenshots saved beside this plan.
- `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` -- exit 0, no diff.

**Manual checks:**
- X4 Pro device run of the same path -- pending the owner's device run.

**Verification record** (2026-09-27, working tree before commit):
- Host: `ctest` 418/418 passed (GameCoreTest 25, GameScriptTest 23 after the review patches).
- `pio run -e x4pro` SUCCESS (88.2 % flash, 31.1 % RAM); `pio run -e default` SUCCESS (compiles GameCore and GameScript unreferenced); `sim.sh build x4pro` and `build sticky` SUCCESS.
- `pio check` (default, as CI) and `pio check -e x4pro`: no defects.
- Flash gate: +120,496 B games on minus off (Implementation Notes).
- Simulator x4pro, screenshots in [story-tracer-screenshots/](story-tracer-screenshots/): `home.png` (Games row), `games-list.png`, `match-frame.png`, `match-tap.png` and `match-tap2.png` (counter 1 and 2, square at each tap), `match-tap-recheck.png` (after the review patches), `error-view.png` (scratch game erroring in `draw`), `games-empty.png`; a scratch `mismatch` folder was skipped with a log line; Back, the header back arrow on the error view, and `key sleep` during play each stopped the VM within one poll and reached their screens.
- Simulator sticky (no `BOARD_HAS_PSRAM`): `sticky-oom.png`, "out of memory" error view, Back to Games.
- X4 Pro device: pending the owner's device run.

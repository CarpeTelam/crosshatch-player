---
title: 'Rubric review: crosshatch-player v1 architecture spine'
reviewed: 'ARCHITECTURE-SPINE.md (draft, 2026-09-26), skimmed game-api-seed.md'
reviewer: 'good-spine checklist, feature altitude, brownfield'
date: '2026-09-26'
---

# Rubric review: crosshatch-player v1 architecture spine

## Verdict

**Revise before it binds epics.** The paradigm (pure `GameCore` + reducer scripts + host-authoritative full-state replication) is the right one, and most ADs are terse, enforceable decisions. But the spine leaves the game **lifecycle** (sleep, exit mid-match, game over, rematch) undecided. It also has no **VM teardown and hang rule** that fits the existing `RenderLock` protocol, and its **upstream-touch cap (AD-3) is already exceeded** by touches the spine implies but never counts. Each of these lets two epics build incompatible pieces.

## Checklist scorecard

| # | Criterion | Result | Summary |
| --- | --- | --- | --- |
| 1 | Fixes the real divergence points for every epic, misses none | **Partial** | Runtime, package, and protocol seams are well covered. Missing: lifecycle/exit/rematch, the move-rejection path in `nearby`, `ui` scoping in `pass`, where `ch.store` runs, package validation details, and icon format. |
| 2 | Every Rule is enforceable and prevents its divergence | **Partial** | AD-6 does not prevent hangs (C functions skip the count hook). AD-5 has no teardown rule, and a naive one deadlocks on `RenderLock`. AD-3 is enforceable but cannot be met as the spine stands. AD-2 misses that all of `src/` compiles and links on the C3. |
| 3 | Nothing Deferred can let two units diverge | **Partial** | "Open: silentRestart after a match" and "Open: where the Games entry lives" each change other epics' flows or budgets. The `LUA_32BITS` deferral silently changes the wire and save formats. |
| 4 | Ratifies, does not contradict, brownfield + AGENTS.md | **Partial** | Good: Storage/HalFile, `tr()`, LOG tags, UiListActivity, and the CAP-flag precedent are all followed. Conflicts: "GameCore = std lib only" vs the `makeUniqueNoThrow` rule; "zip scratch in PSRAM" vs `ZipFile`'s internal `malloc`; a raw game canvas vs `touch-and-ui.md`'s "one supported way"; vendored Lua vs the whole-tree clang-format check. |
| 5 | Every owned dimension decided / deferred / open | **Fail** | Lifecycle is largely absent. Threading misses teardown, task priority, the ESP-NOW receive-callback context, and the frame handoff primitive. Memory has placement rules but no internal-RAM budget and no frame-buffer cap. The operational envelope has no RAM figure. |
| 6 | Rules are terse decisions, not rationale | **Mostly pass** | A few Rules carry rationale (AD-2 "because the LDF…", AD-17 "for things like high scores"). Otherwise clean. |

## Code facts spot-checked

| Spine relies on | Checked | Finding |
| --- | --- | --- |
| Render task runs `render()` under `RenderLock` on core 1 | `src/activities/ActivityManager.cpp:34-89` | Confirmed: `ActivityManagerRender`, core 1, priority 1, 8 KB/16 KB stack. |
| Activity teardown context | `ActivityManager.cpp:222-225` (`exitActivity` calls `onExit()` with the lock held), `:312-315` (`goToSleep` → `replaceActivity` + `loop()`) | The match screen's `onExit`/destructor **runs with `RenderLock` held**, including on the deep-sleep path. See F1. |
| `requestUpdateAndWait` constraints | `ActivityManager.cpp:401-425` | Asserts if called while holding `RenderLock`. Only one waiter is allowed at a time. |
| Sleep behaviour | `src/main.cpp:262-300`, `:720-751` | Sleep is **deep sleep, a chip reset on wake**. Wake routes only to Home or the reader (`main.cpp:539-560`). No game can "resume on wake" without an upstream touch. |
| `preventAutoSleep` hook exists | `src/activities/Activity.h:45` and many activities | Confirmed. |
| `lib/hal/HalStorage.h` has `rename`, `mkdir`, `removeDir` | `HalStorage.h:52-67` | Confirmed; the temp-dir-then-rename install is feasible. |
| `lib/hal/HalMemory.h` PSRAM allocator | `HalMemory.h:21` | Only `allocatePsram(bytes)`, which returns a single buffer. **No realloc/free-by-pointer API** for a `lua_Alloc`. See F6. |
| `lib/ZipFile` stored + deflate | `ZipFile.cpp:385-530` | Confirmed (`InflateStream`). Scratch buffers come from plain `malloc`, not caller-supplied PSRAM. See F6. |
| Fonts routes to model Games routes on | `src/network/CrossPointWebServer.cpp:181-184` | Confirmed: `/fonts`, `/api/fonts`, `/api/fonts/upload`, `/api/fonts/delete`. |
| Web nav | `HomePage.html:107`, `FilesPage.html:1510`, `SettingsPage.html:288`, `FontsPage.html:132` | The nav is hard-coded in **4 HTML pages**, which cannot be `#if`-guarded (they become generated headers). See F3. |
| `FREEINK_CAP_*` precedent | `platformio.ini:322,348,…` (`FREEINK_CAP_USB_MSC`); `FREEINK_CAP_TOUCH` comes from the SDK | Confirmed for ini-set flags. x4pro and sticky each have `-gh_release` and `-gh_release_rc` variants (6 envs). |
| No `build_src_filter` | `platformio.ini` | Confirmed: every `src/**/*.cpp` compiles **and links** in every env, C3 included. See F7. |
| Formatting / static analysis | `bin/clang-format-fix:45-50`; `platformio.ini` `check_flags`; `.github/workflows/ci.yml:78` | The whole-tree format excludes only `builtinFonts`, `hyphenation/generated`, `uzlib`, `miniz/third_party`. `lib/lua54` would be reformatted, which breaks "byte-for-byte". See F3. |
| Host tests | `test/CMakeLists.txt` | Every suite is an explicit `add_subdirectory`, so a new suite touches an upstream file. |
| touch-and-ui rules | `docs/contributing/touch-and-ui.md:5,17,25,100,105-113` | "One supported way… never hand-roll coordinate math". The Back gesture is a **right-swipe starting in the left 25%**, and Home is an up-swipe from the bottom 14%. |
| Home menu | `HomeActivity.cpp:510`, `ActivityManager.{h,cpp}`, `main.cpp`, … | Consistent with the spine's "about 6 files". |
| `freeink-sdk/` | empty in this clone | SDK-side claims (FreeInkUI `InputSnapshot`, CAP auto-enable) could not be verified. |

## Findings

Severity: critical = the spine cannot bind epics as written; high = two epics will build incompatible pieces, or the device can hang or crash; medium = a real divergence or contradiction with a cheap fix; low = polish.

### F1 (high): AD-5, AD-18: VM teardown and cross-task rules conflict with the `RenderLock` protocol

- **Gap.** AD-5 says who owns the `lua_State`, but not how the match screen stops the VM. `ActivityManager::exitActivity` and `goToSleep` call `onExit()` while **holding the non-recursive render mutex** (`ActivityManager.cpp:222-225, 312-315`). Picture a match screen that sets the cancel flag and blocks joining the GameVM task. If the VM task is in any path that takes `RenderLock` or calls `requestUpdateAndWait()`, that is the 12cc816 deadlock again. The same thing happens if the VM task ever waits on a queue the loop task drains.
- **Also undecided.**
  - The GameVM task **priority**. It must not exceed the loop task's (1), or a 1 s callback starves the Home/Back gestures AD-5 claims to protect. Core 0 is ruled out by the IDLE0 task watchdog.
  - The **frame handoff primitive** between the VM task and the render task. "Reads only committed frames" needs a named mechanism, such as a double buffer with an atomic index or a dedicated mutex, and it must never be `RenderLock`.
  - Who calls `requestUpdate()` after a commit. `requestUpdate(false)` only sets a flag that the loop task drains, and `requestUpdate(true)` notifies directly.
  - The **ESP-NOW receive callback context**. It runs on the Wi-Fi task on core 0, not the loop task that the Structural Seed shows.
- **Fix.** Add these to the AD-5 Rule:
  - The GameVM task runs at priority 1 on core 1.
  - It never takes `RenderLock`, never calls `requestUpdateAndWait`, and never touches `Storage` directly (see F2).
  - It publishes a frame by swapping a double buffer under its own spinlock, then sets an atomic "frame ready" flag. The match screen's `loop()` turns that flag into `requestUpdate()`.
  - `onExit` sets the cancel flag and waits at most N ms for the task to exit. It does not wait on anything the render task holds.

  Add to AD-18: the ESP-NOW receive callback only copies into a fixed-size ring buffer in `EspNowLink` and calls no `GameCore` code.

### F2 (high): AD-6: the count hook does not bound time spent in C, so a script can hang the device

- **Gap.** A count hook fires only between VM instructions. One call into `string.find`/`match`/`gmatch`/`gsub` with a backtracking pattern runs entirely in C and never yields to the hook. For example, `("a"):rep(1e5):find(".-.-.-.-b")` is polynomial with a large exponent. `table.sort` with a pathological comparator is milder but similar. With 2 M instructions the budget holds; the wall clock does not. The cancel flag in AD-5 is checked in the same hook, so it cannot fire either. Combined with F1, `onExit` then waits forever with `RenderLock` held, and the device freezes until a hard reset. That breaks AD-6's "Prevents: … hanging".
- **Fix.** Make the per-VM allocator an **arena**: one PSRAM block of the 256 KB cap managed by a small in-tree allocator such as TLSF or o1heap. Then add a Rule: "if a callback exceeds the cancel deadline, the runtime abandons the VM (`vTaskDelete` the GameVM task and free the arena, never `lua_close`) and shows the AD-14 error screen." This requires the F1 rule that the VM task holds no locks or other resources, and bindings must not open files. As a lower-cost alternative, wrap the four pattern functions with a subject-length × pattern-length cap. That puts a small shim outside the unmodified `lib/lua54`, so AD-4 still holds.

### F3 (high): AD-3: the 10-file cap is already exceeded by touches the spine implies but does not count

- **Gap.** Adding up what the spine needs:
  - `platformio.ini` (flags plus a cppcheck suppression for `lib/lua54`)
  - `english.yaml`
  - `CrossPointWebServer.cpp` (route-registration hook)
  - **4 HTML pages** for a "Games" nav link (not `#if`-guardable, so they ship in C3 builds, where the link would 404)
  - `test/CMakeLists.txt` (two `add_subdirectory` lines)
  - `bin/clang-format-fix` (exclude `lib/lua54`, or CI's whole-tree format check rewrites the "byte-for-byte" vendored engine)

  That is 9 or more files **before** the Home entry (~6 files: `HomeActivity.cpp`, `ActivityManager.{h,cpp}`, `main.cpp`, …) and before any `silentRestart` target for returning to the launcher. AD-3's "each change is a guarded hook" is also impossible for the HTML, CMake, and shell-script edits.
- **Fix.** Put a pre-filled **touch budget table** in the spine: file, reason, guardable yes/no, count. Then make the budget decisions now:
  - Web Games page reachable from one link on `FilesPage.html`, or injected by a fork-only JS file.
  - Games entry through a single existing surface, not a new Home item, unless the budget is raised.
  - Reword AD-3 to say "guarded where the language allows; unguardable touches (HTML/CMake/scripts) are listed as such".

  Resolve the "Open: where the Games entry lives" deferral inside this table.

### F4 (high): new AD needed: game lifecycle (sleep, exit mid-match, game over, rematch)

- **Gap.** Checklist item 5 names this dimension, and the spine barely touches it:
  - **Sleep mid-game.** Sleep is deep sleep (a reset). `goToSleep` runs `onExit` and then sleeps immediately (`main.cpp:262-300`). AD-17 says "auto-saves" but not *when*. A save written in `onExit` races a mid-callback VM and adds SD writes on the sleep path.
    - Decide: the save is written after every committed snapshot, and `onExit` never writes.
    - Decide: wake returns to Home and resume goes through the launcher. Otherwise the boot routing in `main.cpp` becomes an upstream touch.
    - Decide: sleeping during `nearby` sends a best-effort `ABORT` before Wi-Fi goes off.
    - Decide: whether the lobby, as well as the match, sets `preventAutoSleep`.
  - **Exit mid-match.**
    - Back (a right-swipe from the left 25%, easy to trigger by accident in a swipe game) and Home (handled in `ActivityManager::loop`, overridable only via `handleHomeGesture()`) both destroy the match.
    - Decide: whether the runtime confirms before leaving, and whether `nearby` needs the confirm.
    - Decide: what each mode does on exit (solo/pass already saved; nearby sends `ABORT`).
  - **Game over.**
    - `status` returns `over`. Then what: a runtime result overlay, or the script's own `draw`?
    - Which `seat` does `draw` receive in `pass` mode when there is no turn seat?
    - The save is deleted when the game ends.
    - Is `ch.store` written?
  - **Rematch.** In `nearby`, does a rematch reuse the `NearbySession` with a new `session` u32 and `setup()` re-run on the host? Who consents? Or is rematch explicitly deferred as "back to lobby"?
- **Fix.** Add AD-20 "Session lifecycle" with a state table (Lobby → Playing → HandOff → Over → {Rematch | Exit}, plus Error). Give each transition: who triggers it, what is saved, what is sent on the wire, and which refresh. Then move whatever is not decided into Deferred explicitly.

### F5 (medium): AD-8, AD-13: contract gaps that make one script behave differently per mode

- **Rejected moves.** `apply` may return `nil, reason`, but AD-13 has no `REJECT` frame. In `nearby` the guest's move is link-ACKed and then nothing happens. In `pass` the spine does not say whether `reason` is shown. Fix: add `REJECT {snapshot_seq, reason ≤ 64 B}`, or rule that rejected moves are dropped silently in every mode and `reason` goes to `LOG_DBG` only.
- **Two different "seq" values.** AD-13 has a link-level `seq`/`ack` in the header, and "MOVE tagged with the snapshot seq it saw". Rule that the snapshot seq is a separate payload field, so `ReliableLink` and `Session` do not share a counter.
- **`ui` scope in `pass` mode.** "Per-device" means seat 2 sees seat 1's `ui` (cursor, pending selection), which leaks hidden information in exactly the games AD-12 protects. Fix: one `ui` table **per local seat**, and the runtime passes the table for the seat being drawn.
- **`ch.store`.** Which callbacks may call it, and on which device? If `apply` records a high score, only the authority records it. Fix: callable from `draw`/`input` only (local device), or from anywhere with the write deferred until the callback returns. The VM task never touches `Storage` (see F1/F2).
- **Validity of `status`.** Rule that `turn` outside `1..N`, or `turn` naming a seat that no local or remote player holds (e.g. seat 2 in `solo`, where N = 1), is an AD-14 script error.
- **When `draw` runs.** Rule: "after every `input` call and every new snapshot, coalesced". There is no periodic tick in v1; say so, since the seed advertises `ch.time.ms()` "for timing".

### F6 (medium): layering table vs AGENTS.md and existing libraries

- **Gaps.**
  - "GameCore depends on the C++ standard library only" contradicts AGENTS.md's mandatory `makeUniqueNoThrow` (`lib/Memory/Memory.h`, std-only and host-safe).
  - Manifest parsing needs JSON. `lib/JsonParser/StreamingJsonParser` is std-only and already host-tested.
  - "GameScript includes no HAL header" means its PSRAM `lua_Alloc` cannot use `HalMemory`, and `HalMemory` has no realloc anyway (`HalMemory.h:21`).
  - "Zip scratch in PSRAM" is not achievable: `ZipFile` `malloc`s its own buffers (`ZipFile.cpp:387,405,465,494,500`).
- **Fix.**
  - Allow `GameCore → lib/Memory, lib/JsonParser`.
  - Make the VM allocator backend an injected port: `GameScript` takes an `{alloc, realloc, free}` table, and `src/games` supplies the `heap_caps_*(MALLOC_CAP_SPIRAM)` or arena implementation from F2. This keeps `HalMemory` untouched.
  - Drop "zip scratch in PSRAM" from the Memory convention, or accept `ZipFile`'s few KB of internal heap. The installer must use `readFileToStream`, never `readFileToMemory`.

### F7 (medium): AD-2: `src/` is always compiled and linked, so the guard rule is incomplete

- **Gap.** There is no `build_src_filter`, so `src/games/*.cpp` and `src/activities/games/*.cpp` compile and link into the C3 `default` image. `--gc-sections` drops unreferenced functions, but not namespace-scope objects with constructors, static buffers, or anything reachable from them. Those would cost RAM on the 380 KB C3.
- **Fix.** Rule: "every `.cpp` under `src/games/` and `src/activities/games/` is wrapped whole-file in `#if FREEINK_CAP_GAMES`; `lib/Game*` has no namespace-scope objects with non-trivial constructors or static buffers larger than 64 B." Move the LDF rationale out of the Rule.

### F8 (medium): AD-15, AD-16: package validation and inbox semantics are left to the implementer

- **Gaps.**
  - Nothing bounds zip entry names. A `../` or nested path in a `.cpgame` could escape `/.games/<id>/`.
  - No limits on package size, entry count, or uncompressed size (zip bomb).
  - No module-name grammar for `require`.
  - The spine does not say whether an inbox file is deleted or moved after install, or what happens to a failed one. A kept file re-installs every time the launcher opens.
  - The `icon.png` format (size, bit depth) is "draft", yet `pack_game.py`, the installer, and the launcher each need it.
- **Fix.**
  - Accept only root-level entries matching `manifest.json | main.lua | icon.png | [a-z0-9_]{1,32}\.lua`, and reject anything else.
  - Cap the package at 256 KB, 32 entries, and 128 KB uncompressed per entry.
  - Name each module after its file, so `require` resolves only those names.
  - Delete the inbox file after a successful install. Leave a failed one with the reason logged.
  - Fix the icon spec now, or have the launcher scale any size and threshold to 1-bit.
  - `pack_game.py` runs the same checks.

### F9 (medium): operational envelope and memory: no RAM budget, no frame-buffer cap

- **Gap.** The memory convention says *where* things go, but nothing gives an internal-RAM budget for a `nearby` match. Wi-Fi + ESP-NOW driver, the 8 KB GameVM stack, queues, and `Session` buffers all come from internal RAM, against the spike's 188 KB free at start. There is also no frame-buffer size or command cap, and frame overflow is not listed among AD-14's error causes.
- **Fix.**
  - Add an envelope row: "a nearby match costs ≤ X KB internal; the match refuses to start below Y KB free".
  - Add to AD-7: "frame buffer ≤ N KB / M commands; overflow is an AD-14 script error in every mode".
  - Size the input and link queues and state their overflow policy (drop oldest, logged).

### F10 (medium): Deferred items that change other epics

- **"Open: silentRestart after a match".** If the radio epic ends up needing the upstream `silentRestart()` (as Wi-Fi activities do), then the AD-14 error screen, game-over screen, and rematch after `nearby` are wiped by the reboot. The reboot lands on Home, since `SILENT_REBOOT_TARGET_*` has no Games target, which would be another upstream touch. Fix: rule now that any restart happens only when the user leaves the post-match screen, and lands on Home.
- **`LUA_32BITS`.** Deferred as "tuning", but it changes the integer width in the codec, snapshots, saves, and the protocol, and AD-4 `static_assert`s 64-bit. Fix: note that switching it bumps `proto` and the save format version.
- **"Open: where the Games entry lives".** Moves into F3's budget table.

### F11 (medium): AD-7 and Screens convention vs `touch-and-ui.md`

- **Gap.** The guide allows exactly one way to build screens and forbids hand-rolled coordinate math. Its only listed exception is `KeyboardEntryActivity` ("raise it in the PR first"). A Lua canvas that receives raw `x, y` is a second exception. `UiAppHost`'s interaction-table handshake has nothing to hit-test on a game canvas.
- **Fix.** State in the Screens convention that the match screen is a declared raw-canvas exception. It hosts through `UiAppHost` for chrome only and takes input via `touchSnapshotFrom`. Note it in `docs/crosshatch/`, not in the upstream guide.
- **Also.** Tell script authors about the reserved gesture zones: right-swipes starting in the left 25% and up-swipes from the bottom 14% never arrive. The seed does not say this.

### F12 (low): new SD file formats carry no version

The save file, `.pkg` hash file, and `ch.store` file are new persistent formats. AGENTS.md tracks format versions (`docs/file-formats.md`). Fix: give each file a magic + version header, and record them in `docs/crosshatch/` rather than the upstream `file-formats.md`, which would cost a cap slot.

### F13 (low): AD-7 color semantics vs `GfxRenderer`

`GfxRenderer` lines and text take a `bool` (black/white). Gray exists only as dithered fills (`fillRectDither`, `Color::LightGray/DarkGray`) or the multi-pass grayscale mode. Rule how `light`/`dark` map: dithered fills in BW mode, with lines and text snapped to black/white or rejected. Otherwise the replay adapter and the first-party games will guess differently.

### F14 (low): smaller contract and convention nits

- Seed of `math.random`: on-device Lua 5.4 seeds from `time()` and an address, both of which can repeat after boot on these boards. Rule that the runtime seeds from `esp_random()` at VM creation.
- The API seed's swipe event omits `x, y`, while the spine includes them. `ctx` fields (`seats`, `mode`) are defined only in the seed; the contract belongs in AD-8.
- The seed says "a new version discards saves", but the rule is hash-based: any byte change discards them.
- AD-13 "lobby matches on equal … `api`": say whether this is the host API level or the manifest `api`.
- Web page file name: the upstream convention is `GamesPage.html`, not `games.html`.
- Vendored Lua uses `luaL_Buffer` (~512 B of stack locals), which conflicts with the 256-B locals rule. Ratify an exemption for `lib/lua54` on the dedicated VM task (the spike measured a 3.3 KB high-water mark).
- AD-16's non-atomic replace (remove old, rename new): add "the registry scan deletes stale temp dirs".
- Rationale inside Rules (AD-2 "because…", AD-17 "for things like high scores"): move it to Prevents.

## What is good and should stay

- Hexagonal `GameCore` with named ports and host GoogleTest suites for every unit (AD-1) fits the repo's host-test setup.
- The reducer contract with authority-only `setup`/`apply`, whole-state replication, and "snapshot is the source of truth" (AD-8, AD-9) removes a whole class of desync bugs.
- The same codec and size limits in every mode (AD-10) directly serve "one script, both modes".
- A single installer and a scan-only registry, with data outside the install dir (AD-16), is simple and robust.
- Radio ownership in one object, never coexisting with the web server, with teardown that never takes `RenderLock` (AD-18), follows the known pitfall.
- The Deferred list is honest, and most entries really cannot cause divergence.

## Suggested order of fixes

1. F4 (lifecycle AD) together with F1 and F2 (teardown, hang, abandonment). They share one state machine and one task protocol.
2. F3 (touch budget table). It decides the Games entry point and the web nav approach.
3. F5 and F8 (contract and package details). The docs and first-party-games epics depend on them.
4. F6, F7, F9, F10, F11. Then the lows.

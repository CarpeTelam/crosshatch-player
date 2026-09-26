# SUMI teardown — round 1
Clone HEAD: 1a1c47c (2026-05-30, "0.6.4"). Code citations below are `@1a1c47c`, pub_date 2026-05-30 unless noted.

## Findings
- claim: SUMI is MIT-licensed custom firmware for the Xteink X4/X3 (ESP32-C3, ~380 KB RAM, 5 buttons, no touch), LICENSE copyright "Dave Allie (CrossPoint Reader / Papyrix)" + "SUMI Contributors" — code is reusable in an MIT project with notice retained.
  source: LICENSE @1a1c47c; README.md @1a1c47c
  publisher: psychoplath9450 (SUMI)
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: license
- claim: Lineage is CrossPoint Reader -> Papyrix (Pavel Liashkov) -> SUMI; SUMI removed WiFi (~4,400 lines) to free ~100 KB heap and added ~28,000 lines (plugin framework, 20 C++ plugins, Lua 5.4, BLE, dictionary...).
  source: README.md @1a1c47c ("What comes from where")
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Lua implementation is stock PUC Lua 5.4.7 vendored as C sources in lib/lua54 (860 KB of source, lua.c/luac.c excluded), compiled with LUA_32BITS=1 (32-bit ints and floats); bindings are hand-written lua_CFunctions (no binding library like sol2/LuaBridge).
  source: lib/lua54/library.json, lib/lua54/luaconf.h:126, src/plugins/LuaBindings.h @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Lua runs as C (setjmp/longjmp error handling), so it coexists with a C++ -fno-exceptions build; errors are caught by lua_pcall around every callback.
  source: src/plugins/LuaPlugin.cpp (callLuaFunc) @1a1c47c; extern "C" includes
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: medium (inferred from code; no explicit statement)
  class: architecture
- claim: Discovery: at plugin-list entry, firmware scans /custom/*.lua (flat, no subfolders), max 8 Lua plugins (MAX_LUA_PLUGINS=8, 8 hard-coded factory functions), display name derived from filename (underscores->spaces, capitalized, 24-byte cap). No manifest, no icon, no metadata file.
  source: src/states/PluginListState.h:61, src/states/PluginListState.cpp:45-80, src/plugins/LuaPlugin.cpp ctor @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Lifecycle hooks are global Lua functions: required draw() and onButton(btn) (returns true to consume; "back" returning false exits), optional init(w,h) and update() (called at 10 Hz by host, return true to request redraw). Script executed once via luaL_dostring to define globals.
  source: src/plugins/LuaPlugin.cpp (loadScript, handleInput, update); src/states/PluginHostState.cpp:288 @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: API surface is 46 flat globals: 13 drawing primitives (pixel/line/rect/roundrect/h/vline/circle/triangle, filled variants), 9 text (setCursor, setTextColor, setTextSize, text, textLine, textWidth, lineHeight, cursorX/Y), 7 UI helpers (drawHeader/Footer/Cursor/TextCentered/MenuItem/Dialog/GameOver), width/height, millis/random/delay(capped 1000 ms), 4 sandboxed file ops (readFile/writeFile/fileExists/listDir), time (getTime/getTimeStr/getDateStr), battery (2), read-only settings (3). Colors are booleans BLACK/WHITE only (1-bit); no bitmap/sprite/image drawing, no grayscale, no fonts selection beyond setTextSize.
  source: src/plugins/LuaBindings.h registerAll() @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: Input is button-only (up/down/left/right/center/back/power as strings); no touch, no button-release events for Lua (C++ plugins have handleRelease/handleChar; Lua does not), no BLE keyboard char input for Lua.
  source: src/plugins/PluginInterface.h; src/plugins/LuaPlugin.cpp buttonName() @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: Scripts have no e-ink refresh control: host does clearScreen + full draw() on every render, FAST_REFRESH normally and a FULL_REFRESH every 30 renders; the SelfRefresh/Animation/partial-region modes exist in PluginInterface but LuaPlugin only returns Simple or WithUpdate.
  source: src/states/PluginHostState.cpp:313-345; src/plugins/LuaPlugin.cpp runMode() @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Resource limits: custom lua_Alloc caps VM at 40 KB (LUA_MEM_LIMIT) on the regular heap (no PSRAM on C3); script file max 16 KB (read fully into malloc'd buffer then luaL_dostring — source, not bytecode); instruction-count hook raises a Lua error after 100,000 VM instructions per callback; no GC tuning (collectgarbage removed). A v0.6.0 audit fixed an unsigned-underflow bug in the allocator's shrink path that had crashed plugins "on innocuous garbage collection".
  source: src/plugins/LuaPlugin.h, src/plugins/LuaPlugin.cpp luaAlloc() comments @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: performance
- claim: Error handling: load or runtime errors set hasError_ and the plugin draws a "Lua Error" screen with plugin name + wrapped message (80-char buffer) and "Back: exit" footer — no device crash. Missing draw()/onButton() is reported the same way.
  source: src/plugins/LuaPlugin.cpp showError(), loadScript() @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: Sandboxing: luaL_openlibs loads everything, then globals are nil'd: dofile, loadfile, load, loadstring, rawget/rawset/rawequal/rawlen, collectgarbage, and whole io/os/debug/package tables (closed in v0.6.0 as a "sandbox escape"). File access only via readFile/writeFile under /custom/<name>_data/ (rejects absolute, "..", backslash); writes are atomic; LUA_FILE_MAX per file. `require` global is not in the nil list (package table removed but require left) — possible gap, unverified at runtime.
  source: src/plugins/LuaBindings.h sandboxGlobals(), resolveSandboxPath(), l_writeFile @1a1c47c; CHANGELOG.md:99
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high (require gap: low)
  class: architecture
- claim: Docs contradict code on namespacing: README, CHANGELOG 0.6.3, and PLUGIN_BRIDGE.md use `sumi.getTime()`, `sumi.readFile`, `sumi.writeFile`, and `os.time()`, but the firmware registers only flat globals and nils `os` — no `sumi` table is registered anywhere in src/. Issue #20 (2026-05-02, kamil428) "lua API problem with read/save file on sdcard" was closed with no maintainer reply.
  source: README.md "Plugin Bridge" section; docs/PLUGIN_BRIDGE.md; grep of src/ @1a1c47c; https://github.com/psychoplath9450/SUMI/issues/20
  publisher: psychoplath9450 / GitHub
  pub_date: 2026-05
  accessed: 2026-09-26
  confidence: high (mismatch) / low (that mismatch caused #20)
  class: sentiment
- claim: Networking for Lua = "Plugin Bridge" (v0.5.1): `bridge.publish/on/connected/keep_awake/remaining_awake` exchanges ≤480-byte JSON messages over a custom BLE GATT service with a Web-Bluetooth page (sumi.page/bridge); 4-slot ring buffers each way; BLE lazy-inited on first bridge call. No WiFi, no device-to-device/multiplayer, no sockets.
  source: src/plugins/LuaBridgeBindings.h; docs/PLUGIN_BRIDGE.md; CHANGELOG.md:138 @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-04 (0.5.1)
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: The built-in games (Chess, Sudoku 1695 LOC, Minesweeper 299, Checkers 276, Solitaire 459, 2048 407, SumiBoy GB emulator ~713+ LOC) are C++ plugins compiled into firmware, not Lua. The only Lua example in the repo is docs/plugin_examples/doorbell.lua (86 lines) + doorbell.html; the authoring prompt contains a text-importer example. No Lua games ship in-repo.
  source: src/plugins/*.h, docs/plugin_examples/ @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: catalog
- claim: Developer experience is AI-first: docs/PLUGIN_AUTHORING_PROMPT.md is a copy-paste LLM system prompt with the full API; sumi.page/plugins offers describe -> paste AI Lua -> validate in a browser playground running Fengari (Lua 5.3, not the device's 5.4.7) with virtual buttons -> BLE transfer to /custom/; eight template ideas (Pomodoro, Snake, Chess Clock, Dice Roller, Tic Tac Toe, Calculator, Reaction Timer, Breathing Guide). No gallery/sharing mechanism; install requires reboot ("appears in Apps list on next boot").
  source: https://sumi.page/plugins/ ; docs/PLUGIN_AUTHORING_PROMPT.md @1a1c47c
  publisher: sumi.page (SUMI author)
  pub_date: unknown
  accessed: 2026-09-26
  confidence: medium (page summarized by fetch model)
  class: feature
- claim: No in-repo simulator/emulator or hot reload for Lua; host tests (MinGW, `make test-build`) cover reader libs, not Lua. v0.6.3 added BLE upload to custom/<name>_data/ for plugin data sync.
  source: grep tools/ scripts/ test/ Makefile @1a1c47c; CHANGELOG.md:31
  publisher: psychoplath9450
  pub_date: 2026-05
  accessed: 2026-09-26
  confidence: medium
  class: feature
- claim: Maturity: single author (all 47 commits by psychoplath9450), commits are release dumps ("0.6.4", "fricked up the binary"), repo started 2026-01-15, last commit 2026-05-30 (~4 months quiet at access); 181 stars, 6 forks, 11 open issues, 0 open PRs; recent open issues (Jun–Jul 2026) about EPUB, flashcards, Arabic, a PalmOS-emulator suggestion, broken web converters — unanswered.
  source: git log @1a1c47c; https://github.com/psychoplath9450/SUMI ; https://github.com/psychoplath9450/SUMI/issues
  publisher: GitHub
  pub_date: 2026-09-26 snapshot
  accessed: 2026-09-26
  confidence: high
  class: trajectory
- claim: A stripped "Folio" variant (SUMI without the apps) appeared on the flasher page by May 2026; a user said "I had always turned off most of the apps in Sumi anyway". No maintainer reply.
  source: https://github.com/psychoplath9450/SUMI/issues/24
  publisher: GitHub user AbuMaia01
  pub_date: 2026-05-21
  accessed: 2026-09-26
  confidence: medium
  class: sentiment
- claim: Third-party summaries: SUMI's Lua is "the only genuine on-device scriptable plugins in the scene" (PocketInk); "Crosspoint is currently more stable, but SUMI has so much promise... quieter lately, last release May 2026".
  source: https://pocketink.io/firmware/sumi/ (via search snippet); search snippet aggregating user reviews
  publisher: PocketInk
  pub_date: unknown
  accessed: 2026-09-26
  confidence: low (search-engine snippet, not read directly)
  class: sentiment

## Leads worth chasing
- pocketink.io/firmware/sumi/ and hackster.io article for dated user voice.
- Reddit r/xteink / r/ereader threads on SUMI Lua / games.
- sumi.page/bridge and sumi.page Lua API reference page — which namespace do they document (flat vs sumi.*)?
- Are any community Lua games published anywhere (Snake/Tetris referenced in code comment l_writeFile)?
- Folio variant: separate repo?

## Looked for, not found
- Script manifest/icon format: none (filename only).
- Touch input, sprite/bitmap drawing, sound, partial-refresh control from Lua: none.
- Lua-related issues on performance/memory other than #20: none on the tracker.
- GitHub API data (contributors/releases) — API access not enabled this session; used HTML pages instead.

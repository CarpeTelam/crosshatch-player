# feasibility (scripting engines, multi-device transport, pass-and-play) — round 2
## Findings
- claim: On ESP32-C3/S3 (shared controller Kconfig), BT_CTRL_BLE_MAX_ACT has range 1–10, default 6, and counts ALL BLE activities ("connections, scan, sync and adv"), each costing 828 bytes; so a BLE central that also scans/advertises can hold at most ~8–9 concurrent connections, not the 70 shown for SOC_ESP_NIMBLE_CONTROLLER chips. NimBLE host default max connections is 3 (round 1).
  source: https://raw.githubusercontent.com/espressif/esp-idf/master/components/bt/controller/esp32c3/Kconfig.in
  publisher: Espressif (esp-idf master)
  pub_date: unknown (master at access)
  accessed: 2026-09-26
  confidence: high (range/default/828 B); medium (that S3 uses this Kconfig, inferred from NimBLE help text "For ESP32-C3 or ESP32-S3 ... configure BT_CTRL_BLE_MAX_ACT")
  class: feature
- claim: Xteink X4 Pro is "ESP32-S3 with 8MB PSRAM" per a third-party firmware project's device-support issue that links the CrossPoint commit and freeink-sdk BoardConfig header.
  source: https://github.com/clackups/draftling/issues/40
  publisher: clackups/draftling (GitHub issue)
  pub_date: 2026-08-18
  accessed: 2026-09-26
  confidence: medium (two independent secondary sources agree: this + PocketInk snippet "ESP32-S3R8"; no vendor page read)
  class: catalog
- claim: SoftAP max_connection has been set to 15 on ESP32-S3 in the field; an IDF v5.1-dev bug (ESP32-S3-WROOM-1-N16R2) allowed only one STA ("max connection, deauth!"), marked Done by Espressif. The hard upper bound (ESP_WIFI_MAX_CONN_NUM) was not quoted from docs this run.
  source: https://github.com/espressif/esp-idf/issues/10511
  publisher: espressif/esp-idf (GitHub issue IDFGH-9108)
  pub_date: 2023-01 (approx, IDF v5.1-dev era)
  accessed: 2026-09-26
  confidence: medium
  class: feature
- claim: Tasmota's official Berry docs: "The RAM usage starts at ~10KB"; Berry uses PSRAM when available; precompiled .bec bytecode loads faster than .be source from the filesystem; "Berry code, when it is running, blocks the rest of Tasmota ... try to never block more than 50ms"; large apps use build-time "solidification" to put bytecode in flash.
  source: https://tasmota.github.io/docs/Berry/
  publisher: Tasmota project docs
  pub_date: unknown (current docs)
  accessed: 2026-09-26
  confidence: high
  class: performance
- claim: Tasmota community reports ESP32 Berry controllers become unstable below ~20KB free heap; there is an open-ish discussion (#24523) on whether Berry bytecode on ESP32-S3 can live in PSRAM instead of DRAM — i.e., Berry is the ESP32 scripting engine with the largest production user base (Tasmota), with S3/PSRAM tuning still discussed.
  source: https://github.com/arendst/Tasmota/discussions/19494 ; https://github.com/arendst/Tasmota/discussions/24523 (search snippets, not read)
  publisher: arendst/Tasmota discussions
  pub_date: unknown
  accessed: 2026-09-26
  confidence: low
  class: sentiment
- claim: Espressif's developer blog shows Lua (now 5.5.0) as an ESP-IDF component, MIT-licensed, loading scripts from a filesystem via package.path (SPIFFS), with luaL_dofile error capture — but publishes no heap/flash numbers.
  source: https://developer.espressif.com/blog/using-lua-as-esp-idf-component-with-esp32/
  publisher: Espressif Developer Portal
  pub_date: 2024-10-22 (updated 2026-04-29)
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: NodeMCU (Lua 5.1 on ESP8266) runtime can have "as little as 17KB RAM" for apps; eLua LTR (read-only ROM tables) "reduces the RAM footprint by some 20-25KB"; node.compile() strips debug info shrinking bytecode to ~60%.
  source: https://nodemcu.readthedocs.io/en/dev-esp32/lua-developer-faq/
  publisher: NodeMCU docs
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high (as project claim; Lua 5.1 not 5.4)
  class: performance
- claim: MicroPython embedding example provides an 8 KB static heap (static char heap[8*1024]) via mp_embed_init(heap, size, &stack_top), runs code with mp_embed_exec_str, cleans up with mp_embed_deinit; GC scans the C stack from stack_top, so the host must supply a correct stack top (a concern if called from multiple FreeRTOS tasks).
  source: https://raw.githubusercontent.com/micropython/micropython/master/examples/embedding/main.c
  publisher: MicroPython
  pub_date: unknown (master)
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: E-ink ghosting leaks prior-screen content: on a Kobo Libra Colour, typed password keys stayed visible "in a washed out manner"; ghosting clears on a full refresh. For pass-and-play hidden info on e-ink, the hand-off screen must use a full (flashing) refresh, not partial, or the previous player's hand may remain faintly visible.
  source: https://goodereader.com/blog/kobo-ereader-news/e-ink-ghosting-effect-can-reveal-sensitive-info-such-as-passwords
  publisher: Good e-Reader
  pub_date: 2024-05-08
  accessed: 2026-09-26
  confidence: high (ghosting leak); medium (full-refresh mitigation is our design inference)
  class: architecture
- claim: DuelBox (open-source game platform) spec'd a pass-and-play hand-off as a full blackout overlay + "pass to player" prompt + confirm, requirement "No leak of hidden state in any frame during hand-off", opt-in per game via the game manifest.
  source: https://github.com/DuelBox/DuelBox-Web/issues/134
  publisher: DuelBox (GitHub issue)
  pub_date: 2026-08-19
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Tabletopia hotseat hides the current player's hand on "End Turn": "Note that your hand will now be hidden and your opponent will not see it."
  source: https://help.tabletopia.com/knowledge-base/game-modes-solo-hotseat-online/
  publisher: Tabletopia help center
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: Shipped mobile board-game apps offering pass & play include Ticket to Ride and Nature (up to 4 players on one device); boardspace.net supports pass-and-play for 100+ games.
  source: https://apps.apple.com/us/app/nature-board-game/id6738703558 ; https://thetabletopfamily.com/20-great-board-game-apps/ (search snippets)
  publisher: App Store / The Tabletop Family
  pub_date: unknown
  accessed: 2026-09-26
  confidence: medium
  class: catalog
- claim: Public ESP32 multiplayer game projects found are overwhelmingly 2-player (ESP-NOW Tetris pair, 2-player Pong over ESP-NOW or UDP) or SoftAP+browser hubs where phones join (gamebox-esp32, ESP-Gaming-station ~27 games); no 3–8-device ESP-NOW turn-based game reference implementation found.
  source: https://github.com/thieu-b55/ESP32-wireless-game-console-MESH-network ; https://github.com/nirinovich/gamebox-esp32 ; https://github.com/Tinshemet/ESP-Gaming-station (search snippets)
  publisher: GitHub hobby projects
  pub_date: unknown
  accessed: 2026-09-26
  confidence: medium
  class: catalog
## Leads worth chasing
- ESP_WIFI_MAX_CONN_NUM exact value for ESP32-S3 (believed 15; ESP32 classic 10) from esp_wifi_types header or Kconfig.
- Whether ESP-NOW v2 (1470 B) is available in Arduino-ESP32 3.x (needs IDF >= 5.4) — check arduino-esp32 ESP_NOW library.
- Berry C-level error mechanism (be_exec.c: likely setjmp/longjmp; C++ mode?).
- Measured Lua 5.4 lua_newstate + openlibs heap on ESP32 (believed ~20–30KB; unverified).
- QuickJS / mJS on ESP32-S3 footprint and license (mJS is GPLv2/commercial like Elk, unverified).
- WAMR ESP-IDF port footprint numbers.
## Looked for, not found
- WAMR concrete footprint numbers (memory_usage.md is methodology only).
- Any benchmark comparing Lua vs Berry vs MicroPython vs wasm3 on ESP32-S3 with numbers.
- 3–8 player ESP-NOW turn-based game project; host-authoritative vs P2P write-ups specific to ESP32.
- Game-design essay specifically on pass-and-play hand-off screens (only product docs/issues found).
- Primary vendor spec page for X4 Pro (Xteink store not retrieved; GBAtemp returned 403).

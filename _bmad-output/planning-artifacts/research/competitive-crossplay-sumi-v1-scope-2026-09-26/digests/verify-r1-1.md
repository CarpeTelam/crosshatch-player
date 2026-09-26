# verification — round 1
- claim: C1 CrossPlay Play Nearby session is hard-coded to 2 players (single peer_ address, boolean myTurn_/isHost_).
  outcome: verified
  check_source: crossplay src/apps_local/link/LinkSession.h@31d94db1 (lines 100-106, 158-174)
  publisher: CrossPlay repo (branch xteink)
  pub_date: unknown (commit 31d94db1)
  accessed: 2026-09-26
  note: One `Address peer_`, `peerAddress()`, `bool isHost_`, `bool myTurn_`, single `peerName_`; no peer-count/max-player constant exists in LinkSession.h or LinkProtocol.h. Strictly 1:1 host/guest.
- claim: C2 CrossPlay Play Nearby uses ESP-NOW on fixed channel 1, unencrypted, max packet 204 bytes.
  outcome: verified
  check_source: crossplay src/apps_local/link/LinkRadio.h:28, LinkRadio.cpp:186,221-223,294-296, LinkProtocol.h:9,39-41@31d94db1
  publisher: CrossPlay repo (branch xteink)
  pub_date: unknown (commit 31d94db1)
  accessed: 2026-09-26
  note: `kChannel = 1`, `esp_wifi_set_channel(kChannel,…)`, `peer.encrypt = false`; kHeaderBytes 12 + kMaxPayloadBytes 192 = kMaxPacketBytes 204 ("under ESP-NOW v1's 250-byte limit"). Fixed channel means it cannot coexist with a WiFi STA connection (LinkRadio.h:48).
- claim: C3 CrossPlay pass-and-play (hot-seat) exists only in Chess and Go.
  outcome: disputed
  check_source: crossplay src/apps_local/{chess/ChessScreens.h:39-43, go/GoFlow.h:45, insider/InsiderActivity.h:3, wavelength/WavelengthCore.h:68 + WavelengthScreens.cpp:552, forehead/ForeheadActivity.h:3}@31d94db1
  publisher: CrossPlay repo (branch xteink)
  pub_date: unknown (commit 31d94db1)
  accessed: 2026-09-26
  note: True for two-player board games: Chess `Opponent {Computer, PassAndPlay, FaceToFace}` (FaceToFace = flat, un-rotated shared board) and Go `Opponent {Computer, Human}` ("2 PLAYERS"). But shared-device party games also exist: Insider ("party game for four to eight people and one device", kMinPlayers 4), Wavelength (CoOp/Teams modes, "PASS THE DEVICE" screen), Forehead (holder-guesses party game). Knucklebones/SeaSalt describe two-player rules but no human-vs-human mode enum found. ToyBattle is Solo|Link (ESP-NOW), not hot-seat.
- claim: C4 SUMI Lua: VM heap 40 KB, scripts <=16 KB, 100,000-instruction hook, linit trimmed to base/table/string/math/utf8, max 8 scripts in /custom/*.lua, button-only input.
  outcome: verified
  check_source: sumi src/plugins/LuaPlugin.h:36-42, LuaPlugin.cpp:159,282, lib/lua54/linit.c loadedlibs[], src/states/PluginListState.h:61, config.h:75@1a1c47c
  publisher: SUMI repo
  pub_date: unknown (commit 1a1c47c)
  accessed: 2026-09-26
  note: LUA_MEM_LIMIT 40*1024, MAX_SCRIPT_SIZE 16*1024, INSTRUCTION_LIMIT 100000 via lua_sethook(LUA_MASKCOUNT); linit loads only base/table/string/math/utf8; MAX_LUA_PLUGINS 8 scanned from PLUGINS_CUSTOM_DIR "/custom". No "touch" in LuaBindings.h/LuaPlugin.cpp (absence-of-evidence check only). README says 46 API functions.
- claim: C5 ESP-NOW limits: max 20 peers, 250-byte v1 payload, 1470-byte v2 payload; plus Arduino-ESP32 ESP_NOW v2 support/version.
  outcome: verified
  check_source: https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_wifi/include/esp_now.h ; https://raw.githubusercontent.com/espressif/arduino-esp32/master/libraries/ESP_NOW/src/ESP32_NOW.cpp ; https://github.com/espressif/arduino-esp32/pull/11524 ; https://newreleases.io/project/github/espressif/arduino-esp32/release/3.3.0 ; https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/esp-now.html
  publisher: Espressif
  pub_date: PR #11524 merged 2025-06-30; others unknown/master
  accessed: 2026-09-26
  note: esp_now.h: ESP_NOW_MAX_TOTAL_PEER_NUM 20, ESP_NOW_MAX_ENCRYPT_PEER_NUM 6 (encrypted subset), ESP_NOW_MAX_DATA_LEN 250, ESP_NOW_MAX_DATA_LEN_V2 1470; v1 receivers truncate/drop >250-byte v2 packets. Arduino ESP_NOW library exposes v2: `getVersion()`, `getMaxDataLen()` (returns 250 or 1470 by esp_now_get_version). Added by PR #11524 "feat(esp_now): Add support for ESP NOW V2" (milestone 3.2.1, merged 2025-06-30); release listings attribute it to arduino-esp32 3.3.0 (based on ESP-IDF v5.5.0). IDF-side v2 arrived in ESP-IDF v5.4 (v5.4/v5.4.1 used 1490, corrected to 1470 in v5.4.2, per ESP-FAQ via search snippet — not directly fetched).
- claim: C6 CrossMux publishes builds for Seeed reTerminal Sticky and Xteink X4 Pro, has compiled-in games (Sudoku, Gomoku, Minesweeper, 2048, Chinese Chess…), no multiplayer or scripting.
  outcome: verified
  check_source: https://github.com/0x1abin/crossmux/releases ; https://raw.githubusercontent.com/0x1abin/crossmux/main/README.md
  publisher: 0x1abin (CrossMux)
  pub_date: latest stable 1.6.0 2026-09-20; nightly #161 2026-09-25
  accessed: 2026-09-26
  note: Releases ship separate sticky-*.bin assets; README lists X3/X4 (C3), Sticky, X4 Pro, Paper Mono, eego A4, Murphy M4, Waveshare 3.97, Metalio E-Ink 4. Games: Sudoku, Gomoku, Chinese Chess, Minesweeper, 2048, plus Electronic Woodfish, Ugly Avatar. No mention of multiplayer, ESP-NOW, Lua or plugins in README/releases (absence in docs, source not grepped).
- claim: C7 Reference Lua compiled as C uses setjmp/longjmp for errors, so it builds under -fno-exceptions.
  outcome: verified
  check_source: https://www.lua.org/manual/5.4/manual.html §4.4
  publisher: Lua.org (PUC-Rio)
  pub_date: unknown (Lua 5.4 manual)
  accessed: 2026-09-26
  note: "Internally, Lua uses the C longjmp facility to handle errors. (Lua will use exceptions if you compile it as C++; search for LUAI_THROW…)". Caveat: must be compiled as C (SUMI's lib/lua54 is .c) — compiling as C++ would switch to try/throw and fail under -fno-exceptions; longjmp also skips C++ destructors in bindings.
- claim: C8a Berry is MIT-licensed.
  outcome: verified
  check_source: https://raw.githubusercontent.com/berry-lang/berry/master/LICENSE ; https://berry-lang.github.io/
  publisher: berry-lang (Guan Wenliang)
  pub_date: LICENSE copyright 2018-2020
  accessed: 2026-09-26
  note: MIT License, "Copyright (c) 2018-2020 Guan Wenliang"; docs site: "distributed under the MIT license".
- claim: C8b Berry RAM usage starts ~10 KB on ESP32.
  outcome: unverified
  check_source: https://berry-lang.github.io/
  publisher: berry-lang
  pub_date: unknown
  accessed: 2026-09-26
  note: Independent source gives a different metric only: core <40 KiB code, "can run on less than 4KiB heap (on ARM Cortex M4…)". The ~10 KB ESP32 figure traces solely to Tasmota docs (Tasmota32 integration, includes its extensions); no independent confirmation found.
- claim: C9 Xteink X4 Pro uses an ESP32-S3 with 8 MB PSRAM.
  outcome: verified
  check_source: https://raw.githubusercontent.com/crosspoint-reader/crosspoint-reader/develop/platformio.ini ([env:x4pro] lines 311-331)
  publisher: crosspoint-reader (upstream)
  pub_date: unknown (develop HEAD at access)
  accessed: 2026-09-26
  note: `board = esp32-s3-devkitc1-n16r8` (16 MB flash / 8 MB PSRAM variant), `memory_type = dio_opi` (octal PSRAM), `-DBOARD_HAS_PSRAM`. Build config, not a vendor datasheet; the board profile implies but does not measure 8 MB. Xteink product page not checked.

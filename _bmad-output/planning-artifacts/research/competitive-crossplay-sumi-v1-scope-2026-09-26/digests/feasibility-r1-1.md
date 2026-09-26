# feasibility (scripting engines, multi-device transport, pass-and-play) — round 1
## Findings
- claim: ESP-NOW supports at most 20 peers total; encrypted peers default 7, configurable up to 17 via CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM; unencrypted = total minus encrypted.
  source: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html
  publisher: Espressif (ESP-IDF v6.1 stable docs)
  pub_date: unknown (v6.1 docs)
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: ESP-NOW v1.0 max payload is 250 bytes (ESP_NOW_MAX_DATA_LEN); v2.0 max is 1470 bytes (ESP_NOW_MAX_DATA_LEN_V2); v2 devices receive both v1 and v2 packets, v1 devices only receive v2 packets that are <=250 bytes (truncated otherwise).
  source: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html
  publisher: Espressif
  pub_date: unknown (v6.1 docs)
  accessed: 2026-09-26
  confidence: high
  class: version
- claim: ESP-NOW broadcast is supported, but the broadcast MAC (FF:FF:FF:FF:FF:FF) must be added as a peer before sending; broadcast needs no pairing with individual receivers, so it is the natural discovery/lobby beacon mechanism for >2 players.
  source: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html
  publisher: Espressif
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high (broadcast support); medium (lobby-pattern inference)
  class: feature
- claim: ESP-NOW send callback reports only MAC-layer delivery (ESP_NOW_SEND_SUCCESS/FAIL); "It is not guaranteed that application layer can receive the data" — so app-level sequence numbers/acks are required for turn sync.
  source: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html
  publisher: Espressif
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: ESP-NOW peer channel must equal the channel the station/softAP is on (0 = current channel); ESP-NOW power saving (esp_now_set_wake_window + connectionless wake interval) is station-mode only.
  source: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html
  publisher: Espressif
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: SoftAP max_connection is affected by ESP-NOW encrypted peers: "soft-AP and ESP-NOW share the same encryption hardware keys, so the max_connection parameter will be affected by CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM."
  source: https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_wifi/include/esp_wifi_types_generic.h
  publisher: Espressif (esp-idf master)
  pub_date: unknown (master as of access)
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: NimBLE BT_NIMBLE_MAX_CONNECTIONS defaults to 3; on ESP32-S3 the user must also raise BT_CTRL_BLE_MAX_ACT in the controller menu. The fetched Kconfig shows "range 1 70 if SOC_ESP_NIMBLE_CONTROLLER" but S3 uses the legacy BT controller, so the S3 upper bound is NOT confirmed (believed ~9; unverified).
  source: https://raw.githubusercontent.com/espressif/esp-idf/master/components/bt/host/nimble/Kconfig.in
  publisher: Espressif
  pub_date: unknown
  accessed: 2026-09-26
  confidence: medium (default 3), low (S3 max)
  class: feature
- claim: Reference Lua ldo.c uses C++ throw/try-catch for errors when compiled as C++ unless LUA_USE_LONGJMP is defined; compiled as C it uses setjmp/longjmp (_setjmp/_longjmp under LUA_USE_POSIX). So Lua works under -fno-exceptions if compiled as C (or C++ with LUA_USE_LONGJMP), but a Lua error longjmps over any C++ frames in bound functions without running destructors.
  source: https://github.com/lua/lua/blob/master/ldo.c
  publisher: Lua.org (lua/lua GitHub mirror, master)
  pub_date: unknown (master)
  accessed: 2026-09-26
  confidence: high (code); medium (destructor-skip consequence is standard C/C++ semantics, not quoted)
  class: architecture
- claim: sol2 supports SOL_NO_EXCEPTIONS, but that "will also disable sol::protected_function's ability to catch C++ errors you throw from C++ functions bound to Lua"; SOL_EXCEPTIONS_SAFE_PROPAGATION is auto-on only when Lua is compiled as C++.
  source: https://sol2.readthedocs.io/en/latest/exceptions.html
  publisher: sol2 docs (ThePhD)
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Berry: interpreter core "less than 40KiB" code, "can run on less than 4KiB heap" (Cortex-M4), one-pass compiler + register-based VM, ANSI C99, mark-sweep GC, MIT license.
  source: https://github.com/berry-lang/berry
  publisher: berry-lang (README)
  pub_date: unknown (README current as of access)
  accessed: 2026-09-26
  confidence: high (as vendor claim)
  class: performance
- claim: wasm3 is in "minimal maintenance phase" — maintainer "unable to continue the development of new features" but will review/merge PRs; needs ~64KB code and ~10KB RAM; ESP32 supported; MIT.
  source: https://github.com/wasm3/wasm3
  publisher: wasm3 (README)
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: trajectory
- claim: Elk JS engine: ~20KB flash, ~100 bytes core VM RAM, 100-iteration loop 2ms on 240MHz ESP32; lacks arrays, closures, while/switch, var/const, this/new; license AGPLv3 or commercial (a copyleft trap for a permissively-licensed firmware or user games).
  source: https://github.com/cesanta/elk
  publisher: Cesanta (README)
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: license
- claim: MicroPython has an official "embed" port that generates self-contained C sources (mpconfigport.h + embed.mk) for inclusion in a larger C project; example at examples/embedding.
  source: https://github.com/micropython/micropython/tree/master/ports/embed
  publisher: MicroPython
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: reTerminal Sticky: ESP32-S3R8 (dual LX7 240MHz), 8MB PSRAM, 32MB flash, 3.97" 800x480 4-level grey ePaper, capacitive touch (GT911 per aggregator), 750mAh, Wi-Fi 4 + BLE 5.0; supported by CrossPoint, TRMNL, ESPHome, OpenDisplay.
  source: https://www.cnx-software.com/2026/07/31/reterminal-sticky-3-97-inch-magnetic-touch-epaper-display-is-supported-by-four-open-source-firmware-projects/
  publisher: CNX Software
  pub_date: 2026-07-31
  accessed: 2026-09-26
  confidence: high
  class: catalog
- claim: Xteink X4 Pro reportedly uses ESP32-S3R8 with 8MB PSRAM (search-engine snippet; underlying pocketink.io page content not retrievable; GBAtemp review 403).
  source: https://pocketink.io/devices/compare/ (snippet via web search)
  publisher: PocketInk (aggregator)
  pub_date: unknown
  accessed: 2026-09-26
  confidence: low
  class: catalog
- claim: A hands-on X4 Pro review says only "runs on an ESP32" and firmware uses "a few hundred kilobytes of RAM" — no PSRAM detail.
  source: https://abstractnonsense.xyz/microblog/2026-09-07-xteink-x4-pro-review/
  publisher: Abstract Nonsense (blog)
  pub_date: 2026-09-07
  accessed: 2026-09-26
  confidence: medium
  class: catalog
- claim: A pass-and-play hand-off pattern: hidden-info games on one device need a full blackout overlay with "pass to <player>" prompt and confirm, guaranteeing no frame of the other player's state renders during the transition.
  source: https://github.com/DuelBox/DuelBox-Web/issues/134 (via search snippet)
  publisher: DuelBox (GitHub issue)
  pub_date: unknown
  accessed: 2026-09-26
  confidence: low (snippet only, not yet read)
  class: architecture
## Leads worth chasing
- Confirm ESP32-S3 BLE max connections (BT_CTRL_BLE_MAX_ACT range in bt/Kconfig for S3 controller).
- SoftAP max stations numeric limit on S3 (docs say "max 10"/15? not yet quoted).
- X4 Pro PSRAM from a primary source (Xteink store page, teardown, draftling issue #40).
- Lua 5.4 RAM on ESP32 (NodeMCU LFS docs), Berry in Tasmota memory numbers; MicroPython embed heap; WAMR ESP32 footprint.
- Pass-and-play in shipped apps (Ticket to Ride, Hanabi, Root) and e-ink ghosting leaking hidden info on partial refresh.
- ESP-NOW multiplayer projects with 3–8 players.
## Looked for, not found
- Berry's C-level error mechanism (README silent) — likely setjmp/longjmp, unverified.
- wasm3 last release date and benchmark numbers on README.
- MicroPython embed heap requirement and license text on the port page.

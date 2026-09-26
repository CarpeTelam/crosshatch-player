---
review: verify
lens: 'Every committed decision web-researched or reality-checked, not asserted from training data'
target: ARCHITECTURE-SPINE.md (Stack table + AD-1..AD-19)
date: 2026-09-26
verdict: 'Pass with fixes: all named versions and technologies exist, fit, and match the repo. One committed number (the 8 KB VM stack) is not supported by the evidence cited for it, and three design details were asserted without checking the real API or code behaviour.'
---

# Verification review: crosshatch-player v1 game platform spine

## Verdict

**Pass with fixes.** Every version in the Stack table exists today. Each one matches either the repo pin or the upstream `develop` pin, and each technology fits the use the spine gives it. The Lua 5.4.9 pin is safe: the public C API is byte-identical to the spiked 5.4.7.

The gaps are in claims that were carried over without being checked:

- **AD-5, 8 KB VM stack.** The spike never exercised Lua's C-recursive paths, so its 3.3–3.7 KB high-water mark does not show that 8 KB is enough.
- **AD-13, broadcast semantics.** The protocol assumes broadcast frames behave like unicast ones, but 802.11 broadcasts get no MAC ACK and no retries.
- **AD-15/16, package validation.** The spine assumes validation that `lib/ZipFile` does not perform.
- **Stack table.** Staleness is not recorded: newer pioarduino, Arduino-ESP32 and GoogleTest releases exist, and the IDF 6 / mbedTLS 4 transition affects the SHA-256 choice.

## Findings

| # | Severity | Decision | Finding | Fix |
|---|---|---|---|---|
| 1 | **High** | AD-5 (8 KB stack), AD-6 ("prevents a game crashing the device") | The spike measured 3.3–3.7 KB on a 16 KB stack. Its workload was a board-game search that recurses in Lua, which costs no C stack in 5.4. It never ran the C-recursive paths. Lua 5.4.9 caps these with desktop-sized limits: `LUAI_MAXCCALLS 200` (`llimits.h:254`) for parser and C-call nesting, and `MAXCCALLS 200` (`lstrlib.c:378`) for pattern-match recursion. A host `-fstack-usage` build of 5.4.9 gives `match` 80 B/frame (200 levels ≈ 16 KB), `subexpr` 112 B and `statement` 144 B per nesting level, and `str_find_aux` 672 B. Xtensa frames differ, but they are the same order of magnitude. The repo has already been caught once: `src/activities/ActivityManager.cpp:40-46` records "a measured ~8KB peak that trips the canary on an 8KB stack" and moved the render task to 16 KB. On ESP-IDF a stack overflow panics and reboots the device, which is exactly what AD-6 promises cannot happen. | Choose one: (a) give the VM task 16 KB, as the spike did (the S3 had about 170 KB of internal RAM free), or (b) keep 8 KB and set `-DLUAI_MAXCCALLS=<n> -DMAXCCALLS=<n>` in the `lib/lua54` build flags. Both macros are `#if !defined` guarded, so the source stays byte-for-byte unmodified and AD-4 still holds. Either way, add worst-case stack cases to the spike harness (deep `?`/capture patterns, deeply nested parentheses at load time, `table.sort` with an erroring comparator, codec at depth 16, `LOG_ERR` from a binding) and record the high-water mark. Until then, tag the 8 KB figure `[ASSUMPTION]` rather than "plausible from the spike". |
| 2 | **Medium** | AD-13 ("broadcasts `STATE` after each accepted move"), AD-18 | 802.11 broadcast frames get no MAC ACK and no retries. IDF 5.5.5 documents the send callback as reporting receipt "on the MAC layer", which only means something for unicast. Sent as broadcasts, `STATE`/`MOVE`/`ACK` lose MAC retries and fall back on the 400 ms app-level resend alone. They also reach every crosshatch device on channel 1. The spine also never states the radio bring-up sequence. The real API has two traps. First, Arduino 3.3.11's `ESP_NOW_Peer` defaults its interface to `WIFI_IF_AP` (`ESP32_NOW.h:72`); in `WIFI_STA` mode that returns `ESP_ERR_ESPNOW_IF`. Second, `esp_wifi_set_channel` must be called after `esp_wifi_start` and not while the station is scanning or connecting (`esp_wifi.h` v5.5.5, attention 1–2). | Broadcast only `ADVERT`. After `JOIN`/`ACCEPT`, add the peer with `ifidx = WIFI_IF_STA`, `channel = 1`, and send everything else unicast. Write down the bring-up sequence from the 3.3.11 example (`ESP_NOW_Broadcast_Master.ino:70-72`): `WiFi.mode(WIFI_STA)`, `WiFi.setChannel(1)`, wait for `WiFi.STA.started()`, `ESP_NOW.begin()`. The repo already calls `WiFi.persistent(false)` (`WifiSelectionActivity.cpp:466`), so no NVS auto-connect will move the channel. Keep that invariant. The broadcast no-ACK behaviour comes from the 802.11 standard; I could not retrieve an Espressif page that states it outright. |
| 3 | **Medium** | AD-15, AD-16 ("validates the package") | Checked against the code. `lib/ZipFile` supports stored (0) and deflate (8) (`ZipFile.cpp:17-18, 393-439, 463-570`). `readFileToStream(name, Print&, chunk)` writes straight to a `HalFile`, because `HalFile : public Print` (`HalStorage.h:82`). **But** ZipFile checks no CRC-32 on extraction, does not sanitize entry names (anything under 256 chars is accepted, including `../` and `/`), and supports no ZIP64. "Validates" is therefore work the installer has to do itself, and the spine does not say so. There is also a hidden hash-matching dependency: the lobby matches on SHA-256 of the `.cpgame` bytes, so two builds of the same sources only match if `pack_game.py` zips deterministically (fixed timestamps and entry order). | In AD-16, spell out the installer's checks. Accept only flat entry names matching `manifest.json`, `main.lua`, `icon.png`, `[a-z0-9_]+\.lua`. Check each entry's CRC against the central-directory CRC (`enumerateFileEntries` returns it, and `crosspoint_mz_crc32` is compiled in `lib/miniz`). Cap entry count and total uncompressed size. Require `pack_game.py` to produce byte-reproducible zips. |
| 4 | **Low** | AD-15 (`icon.png`), Deferred ("image assets beyond the icon") | An in-tree PNG decoder exists. `lib/PngToBmpConverter` (inflate-based) writes a scaled 1-bit or 2-bit BMP stream (`pngFileTo1BitBmpStreamWithSize`, `pngFileToBmpStreamWithSize`) and is already used for EPUB cover thumbnails (`lib/Epub/Epub.cpp:751, 868`). There is also `PngToFramebufferConverter` over `bitbank2/PNGdec @ 1.1.6`. **But** `PngToBmpConverter` rejects interlaced PNGs (`PngToBmpConverter.cpp:446-447`). | Convert the icon to a BMP thumbnail at install time, following the Epub pattern. State "non-interlaced PNG" in the manifest docs, and have `pack_game.py` reject or re-encode interlaced icons. |
| 5 | **Low** | Stack table, AD-16 (SHA-256) | None of the pins is the latest release. pioarduino **55.03.312** (2026-09-20, Arduino-ESP32 **3.3.12**, same IDF 5.5.5) and **61.04.00-RC1** (2026-09-23, Arduino **4.0.0-RC1** on IDF **6.1**) both exist, as does GoogleTest **v1.18.0**. All current pins match upstream `develop` (`platform …/55.03.311/…`, `GIT_TAG v1.17.0`), so keeping them is correct under the fork policy, but the spine gives no reason for them. mbedTLS SHA-256 works today: `src/network/FirmwareFlasher.cpp:8,163-239` already builds `mbedtls_sha256_*` on 3.3.11. IDF 6.0 ships mbedTLS 4, which removes the legacy `mbedtls_sha*` API in favour of PSA, and Arduino 4.0 is already at RC. | Add a note to the Stack table: "pins inherited from upstream `develop`; never bumped in the fork". Call SHA-256 from one small `src/games` helper, so the IDF 6 migration touches a single function (or reuses whatever upstream does to `FirmwareFlasher`). |

## Verified claims (with evidence)

### Lua 5.4.9 (AD-4, Stack)

- **Exists.** lua.org/news: "Lua 5.4.9 released … bug-fix release", 25 Aug 2026. lua.org/versions: "5.4.9, released on 25 Aug 2026. There will be no further releases of Lua 5.4." Lua 5.5.0 shipped 22 Dec 2025 and 5.5.1 on 03 Aug 2026. These match the author's check. (The ftp listing shows tarball file dates of 2026-08-10 for 5.4.9 and 2026-07-24 for 5.5.1; the news dates are the announcement dates.)
- **Tarball SHA-256** (downloaded and checked): 5.4.9 = `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`. The spike's 5.4.7 = `9fbf5e28…cfbf1e30` matches the ftp listing.
- **C API delta 5.4.7 → 5.4.9** (full `diff -r src/`, 271 lines):
  - `lua.h`: only `LUA_VERSION_RELEASE` "7"→"9", `LUA_VERSION_RELEASE_NUM`, and the copyright year. No signature changes.
  - `luaconf.h`, `lauxlib.h`, `lualib.h`: **byte-identical**. The `<climits>` + `sizeof(lua_Integer)==8` rule and the stock config are unaffected.
  - Behaviour changes relevant to embedding:
    - `lua_load` now runs `luaC_checkGC` first, so a GC step can hit the counting allocator during script load. That is harmless, but the spike's "VM heap after load" figure may move slightly.
    - `luaE_extendCI` now reports allocation failure instead of asserting, which makes OOM under the 256 KB cap more robust.
    - `lua_close` / `lua_closethread` clear `errfunc` and empty the stack before running finalizers.
    - Finalizers are skipped when there is not enough stack (`luaD_checkminstack`).
    - "error in error handling" is now built at throw time.
    - `lua_newuserdatauv` caps `nuvalue` at `SHRT_MAX`.
    - Fixes: GC weak-table metatable (5.4.8 bug list), `__newindex` anchor, `utf8` decode, and a parser constructor-overflow check.
  - None of these changes the spike's design (allocator, count hook, pcall trampoline, library set).
- **Recommendation.** Promote AD-4's version tag from ASSUMPTION to verified, pin the SHA-256 above in `lib/lua54/README`, and re-run the spike harness once on 5.4.9. The flash (80,770 B) and stack numbers were measured on 5.4.7. In the Deferred row, also note that 5.4 is end-of-life.

### ESP-NOW v2 on Arduino-ESP32 3.3.11 / IDF 5.5.5 (AD-13, AD-18, Stack)

Checked against `esp-idf/v5.5.5/components/esp_wifi/include/esp_now.h`, the v5.5.5 ESP32-S3 ESP-NOW docs, and `arduino-esp32/3.3.11/libraries/ESP_NOW`:

- `ESP_NOW_MAX_DATA_LEN_V2 1470` and `ESP_NOW_MAX_TOTAL_PEER_NUM 20`: **confirmed**. `ESP_NOW_MAX_ENCRYPT_PEER_NUM` is 6 in the header, which is irrelevant because the spine uses no encryption.
- **Frame budget.** The AD-13 envelope is 2+1+1+4+2+2 = **12 B**. A 1,400 B snapshot makes a 1,412 B frame, leaving **58 B** of headroom under 1,470. This is consistent, as long as `STATE` carries nothing beyond the snapshot.
- **v1 interop.** A v1 device "will either truncate the data to the first 250 bytes or discard the packet entirely". Confirmed; this does not matter while every device runs crosshatch.
- **Channel.** The peer `channel` must be 0 (use the current channel) or equal the channel the station is on; otherwise `ESP_ERR_ESPNOW_CHAN`. `esp_wifi_set_channel` works in STA mode only after `esp_wifi_start` and not while scanning or connecting. A fixed channel 1 with an unconnected STA is valid.
- **Broadcast.** The broadcast MAC must be added as a peer before sending; receivers need no peer entry. See Finding 2 on ACKs.
- **API change to note for implementers.** In IDF 5.5, `esp_now_send_cb_t` is `(const esp_now_send_info_t *tx_info, esp_now_send_status_t)`; in IDF 5.4 it was `(const uint8_t *mac_addr, …)`. Snippets from older examples will not compile. The Arduino wrapper handles both (`ESP32_NOW.cpp:148-151`).
- **Power.** IDF says ESP-NOW sleep is supported only in station mode, via `esp_now_set_wake_window` plus a connectionless wake interval. The spine's open battery question stands; the default is unmeasured.
- The Arduino `ESP_NOW.getVersion()` / `getMaxDataLen()` API exists at 3.3.11 (`ESP32_NOW.h:37-38`), as the brief said.

### pioarduino 55.03.311 (Stack)

- `platform.json` at tag 55.03.311 has version `55.03.311`, `framework-arduinoespressif32` 3.3.11, `framework-espidf` v5.5.5, and toolchain `xtensa-esp-elf-14.2.0_20260121`. The release zip returns HTTP 200.
- The repo pins exactly this (`platformio.ini:10`), and so does upstream `develop`.
- Newer releases exist (Finding 5). 3.3.12's notes mention web-server hardening and nothing about ESP-NOW or PSRAM.

### PlatformIO Core 6.1.19

This matches `AGENTS.md` and CI; there is nothing further to verify.

### GoogleTest 1.17.0 (Stack)

- `test/CMakeLists.txt:14-17` fetches googletest with `GIT_TAG v1.17.0`, and upstream `develop` pins the same.
- 1.17.x requires C++17; the suite sets `CMAKE_CXX_STANDARD 20` (`test/CMakeLists.txt:4`), which is compatible.
- v1.18.0 exists. The release page states that C++17 is required and recommends living at head. The publish date the fetch tool returned looked unreliable, so it is not quoted here.

### mbedTLS SHA-256 (AD-16)

It is available and already used in-tree (`FirmwareFlasher.cpp`, `mbedtls_sha256_init/starts/update/free`). This compiles for every env on 3.3.11 (IDF 5.5, mbedTLS 3.x). The IDF 6.0 security migration guide confirms that legacy `mbedtls_sha*` is removed with mbedTLS 4 (Finding 5). wolfSSL 5.7.2 is also linked (`FREEINK_NET_WOLFSSL`) and offers `wc_Sha256` as an alternative. The hash must stay out of `GameCore` (AD-1), and the spine already puts the installer in `src/games`.

### lib/ZipFile + lib/miniz (AD-15, Stack)

- Stored and deflate: yes.
- Entry to file: yes, through `readFileToStream(…, HalFile&, chunk)`.
- Inflate backend: `lib/miniz/src/InflateStream` (tinfl) with a 32 KB window in streaming mode. It allocates with plain `malloc`, not through `HalMemory`, so the spine's "zip scratch in PSRAM via HalMemory" convention cannot hold without editing upstream `ZipFile`/`InflateStream`, which would count against AD-3. Placement is decided by the heap's SPIRAM malloc policy. Soften that convention row.
- Note: `ZipFile` stores `const std::string& filePath`, so the caller must keep the path string alive for the object's lifetime.

### PNG decoder for the icon (AD-15)

It exists in-tree (Finding 4).

### FreeRTOS GameVM task on core 1 with an 8 KB stack (AD-5)

- ESP-IDF task stack sizes are in bytes, and the repo's render task uses `8192`/`16384` bytes (`ActivityManager.cpp:40-51`).
- The spike's high-water mark (3,288–3,704 B of 16,384) makes 8 KB plausible **for the spiked workload only** (Finding 1).
- Scheduling caveat: core 1 already runs the Arduino loop task and `ActivityManagerRender`, both at priority 1. A CPU-bound GameVM at the same priority will round-robin with them, so its throughput will be below the spike's 2.17 M instr/s, which was measured with the VM running alone. That affects the "2 M instructions ≈ 1 s per tap" budget in AD-6. Measure it with the render task active.
- ESP-NOW callbacks run on the Wi-Fi task, so posting to the queues AD-5 describes is correct.

### Other spine assertions checked

- **AD-2, LDF behaviour.** PlatformIO docs: `chain` (the default) "does not evaluate C/C++ Preprocessor conditional syntax"; `chain+` does. `platformio.ini` sets no `lib_ldf_mode`, so the default applies. Confirmed: game libraries must compile for C3 even though nothing references them there.
- **PSRAM on both target envs.** `x4pro` sets `-DBOARD_HAS_PSRAM`. `sticky` gets it from the board JSON (`esp32-s3-devkitc1-n16r8.json` `extra_flags`, checked at 55.03.311). The AD-6 PSRAM VM heap is valid on both.
- **AD-2 precedent.** `FREEINK_CAP_USB_MSC` is env-scoped in `x4pro`, `x4c` and `papermono`, not `sticky`. The pattern the spine cites (a CAP flag set per env in `platformio.ini`) holds.

## Not verifiable here

- The `freeink-sdk` submodule is not initialised in this clone, so I could not check FreeInkUI `InputSnapshot` fields (Consistency Conventions: input events) or the `UiAppHost` contract against the SDK source.
- Whether ESP-NOW v2 broadcasts carry payloads above 250 B exactly as unicast does is not stated in the IDF docs I retrieved. The spine only broadcasts `ADVERT`, which should stay under 250 B, so this matters only if Finding 2's fix is not applied.

## Sources

- [lua.org news](https://www.lua.org/news.html), [versions](https://www.lua.org/versions.html), [ftp](https://www.lua.org/ftp/), [bugs](https://www.lua.org/bugs.html); tarballs diffed locally
- [ESP-IDF v5.5.5 ESP-NOW (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/network/esp_now.html), [v5.4 ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/network/esp_now.html); raw `esp_now.h` and `esp_wifi.h` at tag v5.5.5
- raw `arduino-esp32` 3.3.11 `libraries/ESP_NOW/src/ESP32_NOW.{h,cpp}` and `examples/ESP_NOW_Broadcast_Master`
- raw `pioarduino/platform-espressif32` 55.03.311 `platform.json` and board JSON; [pioarduino releases](https://github.com/pioarduino/platform-espressif32/releases); [arduino-esp32 releases](https://github.com/espressif/arduino-esp32/releases), [3.3.12](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12)
- [googletest releases](https://github.com/google/googletest/releases), [v1.18.0](https://github.com/google/googletest/releases/tag/v1.18.0)
- [ESP-IDF v6.0 security migration guide](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/migration-guides/release-6.x/6.0/security.html)
- [PlatformIO LDF modes](https://docs.platformio.org/en/latest/librarymanager/ldf.html)
- upstream `crosspoint-reader/crosspoint-reader` `develop`: `platformio.ini`, `test/CMakeLists.txt` (raw)

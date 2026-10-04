# Internal baseline (lead, from named project docs; NOT research evidence)

Granted documents at crosshatch-player@93d34ede (2026-10-04):
- B1 docs/crosshatch/formats.md
- B2 _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md (AD-6, AD-15, AD-16, AD-19, AD-25, Deferred)
- B3 docs/crosshatch/api-level-1.txt
- B4 .github/workflows/crosshatch-game-packages.yml
- B5 _bmad-output/planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/addendum.md ("Later: game starter repo")

Facts:
- Package: one `.chgame` file = flat zip (stored/deflate, no ZIP64, no comment); exact member whitelist manifest.json, main.lua, [a-z0-9_]{1,32}.lua, [a-z0-9_]{1,32}.png (icon/title/handoff reserved). Limits 256 KB package, 32 members, 128 KB/member, 256 KB Lua total, 128 KB converted images. [B1 §Package, B2 AD-15]
- Installer: zip directory pre-check (EOCD, size, compression, overlapping members), streaming extract with size guard, rejects Lua bytecode (0x1B), CRC-32 check; PNG→1-bit BMP conversion at install; extract to /.games-tmp/<id>/, rename into /.games/<id>/, `.pkg` commit marker written last; `.removing` marker for crash-safe remove; FAT cross-link probe (`.xlink`). [B1 §Install]
- Only entry point v1: SD inbox `/games/*.chgame`, installed when Games opens; files arrive by existing web file manager or USB. Web Games page deferred; /api/games* reserved. [B2 AD-16, Deferred]
- Identity: manifest `id`; package hash = first 8 bytes of SHA-256 over sorted members, stored in `.pkg`; used to bind resume.bin to an exact package. Reinstall of same id replaces regardless of version. Saves in /.games-data/<id>/ survive reinstall/remove. Save migration across package versions deferred (saves discarded when hash changes). [B1, B2 Deferred]
- Compatibility: integer API level (ch.api), manifest `api`, Manifest::check(HostCaps) returns Invalid / Unavailable(reason) / Ok; installed-but-unavailable games listed with reason. Surface list file + API_SURFACE_CRC; levels additive and frozen. [B2 AD-19, AD-15]
- Manifest: id, name, version, api, seats{min,max}, modes(solo/pass/nearby), hidden, icon (library name), icon_weight, default_mode, settings[≤4]. [B2 AD-15]
- Sandbox: Lua 5.5.1 unmodified; 256 KiB Lua heap cap in a 464 KiB PSRAM arena; 2M instructions/callback; no io/os/load/debug; text chunks only; no file API — runtime owns persistence (store.bin ≤4 KB, resume.bin, prefs.bin). [B2 AD-6, B1]
- Distribution today: CI artifact `game-packages` on PRs labelled package-games (30-day retention), not a release asset or catalog. Firmware releases via fork workflow + OTA from GitHub releases/latest. [B4, B2 AD-25]
- Targets: x4pro, sticky only (FREEINK_CAP_GAMES); C3 not supported for games. [B2 AD-25 Boards]
- Post-v1 intent: starter repo (API ref, example game, package build script, off-device test). [B5, B2 Deferred]

## Addendum for the marketplace run (lead, 2026-10-04; internal, not evidence)
- B7 lib/JsonParser/StreamingJsonParser.h: callback-based streaming JSON parser, 512 B token buffer, max nesting 32 — the device can parse a catalog streamed from SD without holding it in RAM.
- B8 src/games/GameHash.cpp: device SHA-256 via mbedTLS already in firmware (package hash).
- Not checked: whether the shipped mbedTLS/wolfSSL config exposes ECDSA P-256 or Ed25519 *verify* to application code. A signed-index design needs a spike to confirm (open question).
- Prior run (internal): competitive-crosssmudge-app-store-vs-crosshatch-game-2026-10-04/research.md recommended a "Get games" catalog that verifies SHA-256 and drops into the /games/ inbox; noted our HttpDownloader's wolfSSL path calls setInsecure() (B6).

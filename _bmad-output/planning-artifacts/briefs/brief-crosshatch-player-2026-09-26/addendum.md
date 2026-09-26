---
title: 'Addendum: crosshatch-player v1 brief'
created: '2026-09-26'
updated: '2026-09-26'
---

# Addendum: detail for the PRD and architecture

This holds detail that belongs downstream of the brief. Source numbers such as [23] refer to the research at `_bmad-output/planning-artifacts/research/competitive-crossplay-sumi-v1-scope-2026-09-26/research.md`.

## Why not a CrossPoint plugin

The author asked whether the game system could be built as a CrossPoint plugin. A check of this repo on 2026-09-26 found:

- **No plugin system exists.** "Web plugins" appears only under "Coming soon" in `README.md`, with no code behind it.
- **The "Calibre plugin" is not a firmware plugin.** It runs on the Calibre side and uploads books over the documented WebSocket protocol.
- **The closest precedent is the SD-card font pipeline.** The web Fonts page accepts `.cpfont` uploads, validates the family name, filename, and magic bytes, installs to `/.fonts/`, and refreshes the font registry (`docs/webserver.md`). A game package install can follow the same pattern.

So the runtime is fork code, and games are the plugins. If upstream later ships web plugins, reassess alignment.

## Script runtime starting points

- **SUMI's sandbox recipe (MIT) [22]–[25]:**
  - PUC Lua 5.4 compiled as C, so errors unwind with `setjmp`/`longjmp` and need no C++ exceptions.
  - Every callback runs through `lua_pcall`.
  - A per-VM allocator cap and an instruction-count hook.
  - A trimmed `linit`: no `io`, `os`, `debug`, or `require`.
  - A jailed data folder.
- **Known Lua pitfall:** a Lua error longjmps over C++ frames without running their destructors. Bindings must not hold RAII objects across calls into Lua [39].
- **Gaps in SUMI to fill:**
  - touch events
  - grayscale and bitmaps
  - refresh control (dirty regions and requesting a full refresh)
  - a manifest (name, icon, player counts, supported modes)
  - a paged launcher
  - a PSRAM-backed heap instead of the 40 KB cap
  - a seat and state-sync API
- **The alternative engine:** Berry (MIT, used by Tasmota). Its reported footprint of about 10 KB of RAM is unverified; settle Lua vs. Berry with a footprint spike on the S3.
- **Host UI:** build the launcher and host screens on `UiListActivity` / `UiAppHost`, per `docs/contributing/touch-and-ui.md`.

## Multiplayer layer starting points

**Radio:**

- ESP-NOW with a broadcast lobby [34].
- The host device holds the authoritative state and sends the whole state each turn, with sequence numbers and app-level acks, as CrossPlay does with its 400 ms resend and 10 s drop [7].
- Payloads are 250 B in ESP-NOW v1 and 1,470 B in v2. ESP-NOW v2 needs Arduino-ESP32 3.2.1 or later [69]. Checked 2026-09-26: every env extends `[base]`, which pins pioarduino platform 55.03.311, bundling Arduino-ESP32 3.3.11 on ESP-IDF 5.5.5. The Arduino `ESP_NOW` library at that tag exposes `getVersion()` and `getMaxDataLen()`, and IDF 5.5.5's `esp_now.h` defines `ESP_NOW_MAX_DATA_LEN_V2` as 1470 and caps peers at 20. A v1 peer truncates or drops v2 packets over 250 B, which doesn't matter while every device runs crosshatch.

**Patterns to borrow from CrossPlay, as ideas rather than code [8]:**

- a fixed receive buffer that never allocates
- link logic with no Arduino dependency
- a fake-link host test harness that drops, delays, and reorders packets

**Other constraints:**

- Wi-Fi: ESP-NOW takes over Wi-Fi, so a match cannot run while the web server is up [5].
- Security: nothing is encrypted; the design assumes everyone in the room cooperates.
- Hidden information: the hand-off screen needs a full refresh, because a partial refresh leaves ghosting that leaks information [48].
- Scale: two seats in v1. Keep turn order and the lobby N-seat-shaped; ESP-NOW allows 20 peers [34].

## Candidate first-party games

The research derived these from game rules, not usage data:

- **Open-information two-player:** tic-tac-toe, Ultimate tic-tac-toe, Dots and Boxes, Nine Men's Morris, Gomoku.
- **Hidden information:** Hangman, Mastermind / Bulls and Cows, Battleship.
- **Solo:** Sudoku, nonograms, Minesweeper. Simon Tatham's puzzles are MIT-licensed, but each needs rework for e-ink.

## Later: game starter repo

The author's post-v1 goal is a starter repo: a template that anyone with an AI assistant can point at to build a game. It would hold:

- the API reference
- an example game
- a package build script
- a way to test off-device

The v1 API docs should be written so they can move into this repo unchanged.

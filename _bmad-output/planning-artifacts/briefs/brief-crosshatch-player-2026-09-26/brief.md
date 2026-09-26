---
title: 'Product Brief: crosshatch-player'
status: final
created: '2026-09-26'
updated: '2026-09-26'
inputs:
  - '_bmad-output/planning-artifacts/research/competitive-crossplay-sumi-v1-scope-2026-09-26/research.md'
---

# Product Brief: crosshatch-player

## Executive Summary

crosshatch-player turns an e-reader in your pocket into a game console you can program. It is a fork of CrossPoint Reader for two ESP32-S3 touchscreen devices, the Xteink X4 Pro and the Seeed reTerminal Sticky. It adds a small game runtime to the firmware. The games themselves are packages you write in a scripting language and upload through CrossPoint's existing web file manager. Adding a game never means rebuilding or re-forking the firmware.

The runtime ships with a multiplayer layer that every scripted game can use. A game declares its players and its state, and the runtime provides both ways to play together: passing one device across the table (pass-and-play) and playing on two devices nearby (Play Nearby). v1 supports two players, but the model is built for more.

This is a passion project. The first user is the author, a developer who writes games with an AI assistant. The first players are friends and family, above all kids handed a device at a restaurant instead of a phone.

## The Problem

Today the only way to add a game to these devices is to change the firmware.

- **CrossPlay** has the best game catalog on these boards, but every game is more than 1,200 lines of hand-written C++. It modifies 169 upstream CrossPoint files. The author forked it to add games, and keeping that fork merged with upstream became a nightmare. CrossPlay's own automated merges have silently deleted fork code.
- **SUMI** proved that a sandboxed Lua runtime works on this class of hardware. But it targets button-only ESP32-C3 devices, has no touch input and no multiplayer, and ships no scripted games.
- **CrossMux** runs on both target boards, but its games are compiled in, and it has no scripting.
- **Upstream CrossPoint** closed a request for games as not planned. It has no plugin system; "web plugins" appears only as a "coming soon" line in its README.

So anyone with a game idea has to become a firmware maintainer first. That cost is what kills small, fun ideas.

## The Solution

v1 has three parts, all in the firmware and all additive to upstream code:

1. **A script runtime.** It is a sandboxed interpreter. The working choice is Lua 5.4 compiled as C, following SUMI's MIT-licensed recipe, pending a one-day footprint spike against Berry. Its API is designed for touch and e-ink from the start: touch events, grayscale drawing, and control over which parts of the screen refresh and when. A script error shows an error screen instead of crashing the device.
2. **A multiplayer layer.** Every game is written against *seats*. A seat is filled by a person on this device or a person on a nearby device. The script owns the rules and a small, serializable game state; the runtime owns turn order and the ESP-NOW radio link, and it keeps the two devices in sync. The runtime also supplies a hand-off screen with a full refresh, so pass-and-play games with hidden information (such as Hangman or Battleship) don't leak through e-ink ghosting.
3. **Install by upload.** A game is a single package: a manifest, the script, and an icon; the exact format will be decided in architecture. You upload it through the web file manager, or a Games page modeled on the existing `.cpfont` Fonts page. The firmware validates it and lists it in a games launcher, with no reboot.

Because a game is a file on the SD card, the games act as plugins without CrossPoint needing a plugin system.

## What Makes This Different

There is no technical moat here, and the brief doesn't claim one. The difference is a combination nobody on these boards offers:

- **Games are content, not code in the firmware.** You can write and install a game without touching the firmware, which CrossPlay and CrossMux can't offer.
- **Multiplayer comes free to every game.** In CrossPlay, each game implements multiplayer by hand, and pass-and-play exists only in Chess and Go.
- **Built for touch and e-ink.** SUMI's scripts have no touch input and no control over screen refresh.
- **Fork discipline.** The runtime stays additive behind device and capability guards, so the fork keeps merging cleanly with upstream. Upstream merge pain is the exact failure that started this project.

## Who This Serves

- **Primary: the author.** A developer who builds games with an AI assistant, wants to go from idea to playable in an evening, and wants to stop fighting merges.
- **Players: friends and family.** Especially kids at a restaurant table: pick up the device, choose a game, play alone or pass it across the table, with no phone and no help needed.
- **Later: other developers with AI.** They reach it through a game starter repo they can point an AI at. The starter repo is out of v1 scope, but it shapes the v1 API, which must be documented well enough for an AI to write against.

## Success Criteria

- **Idea to device without firmware.** A new game goes from idea to playing on a device with no firmware build.
- **One script, both modes.** A two-player game plays in both pass-and-play and Play Nearby from the same script, with no radio code in the script.
- **Clean upstream merges.** Merging upstream `develop` touches no game code, and the runtime's changes to upstream files stay under a cap set during architecture.
- **The restaurant test.** A child can launch a game and finish a round without adult help.
- **The API proves itself.** At least three first-party games ship as scripts, covering a solo puzzle, an open-information two-player game, and a hidden-information two-player game.

## Scope

**In v1:**
- A script runtime with touch, drawing, refresh control, a sandbox, and error isolation.
- A seat-based multiplayer layer for two players, with pass-and-play and Play Nearby, including the hand-off screen.
- A package format, install through web upload, and a games launcher that pages when the list grows.
- API docs written for developers working with AI.
- A few first-party scripted games that exercise every mode.

**Out of v1:**
- More than two players (the seat model is built for it; it ships later).
- A game starter repo, a gallery, and sharing.
- A browser playground or simulator.
- A computer opponent provided by the runtime; a script may implement its own.
- Playing against CrossPlay devices.
- Porting CrossPlay's C++ games.
- Real-time and drag-heavy games.

**Constraints:**
- S3-only code sits behind `FREEINK_CAP_*` guards.
- Shared code must still build for the ESP32-C3 targets.
- The fork never moves the `freeink-sdk` submodule pointer.

## Risks and Open Questions

- **Engine footprint.** How much memory and speed do Lua and Berry really cost on the S3 with PSRAM? A one-day spike settles it.
- **Radio.** Is ESP-NOW reliable between two of these devices, and what does a match cost in battery? CrossPlay never measured it. (The pinned Arduino-ESP32 3.3.11 does support ESP-NOW v2's larger payloads; checked 2026-09-26.)
- **State size.** Does a typical game's state fit in one radio payload? A payload is 250 B in ESP-NOW v1 and 1,470 B in v2.
- **Upstream plugins.** If upstream ships its "web plugins", does crosshatch align with them or stay separate?
- **Demand.** Whether anyone beyond the author wants this is unknown, and for a passion project that is acceptable.

## Vision

- **A starter repo:** anyone with an AI assistant points it at the repo and has their own game on their device the same day.
- **More players:** multiplayer grows to three to eight seats for party games.
- **Sharing:** a small gallery lets friends trade games.
- **A browser simulator:** a simulator of the real firmware lets authors test a Play Nearby match without two devices.

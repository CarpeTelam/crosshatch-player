---
title: "Solo fixture game on the X4 Pro"
ticket: 16
status: done
---

# Solo fixture game on the X4 Pro

Closing end-to-end run of epic-script-runtime, done by the owner on an X4 Pro.

## Setup

- Firmware: x4pro built from `6634faae` (sha256 `888c308cfed7b00ca445c3030f9d746ab1cf58e8c1744006b2d502d817f628e5`), the same commit whose PR CI (CarpeTelam/crosshatch-player#13) ran green on all 16 checks.
- SD card: `/.games/` from `test/game_script/fixtures/`: `solo` (added for this run, 6634faae), `tracer`, `counter`, `timer`, `gallery`, `loop`, `limits`, and each `faults/*.lua` as `f-<name>/` with the README's manifest template (29 games).
- Run order: `test/game_script/fixtures/README.md`, "Closing device run".

## Verification

- Owner, 2026-09-27: passed. Done when 1 (solo played to game over with drawing, text, tap, long press, swipe, refresh hints, `ch.timer`, and `ch.store` surviving a restart), 2 (every fault fixture ends in the error view, Back returns to Games, the device stays responsive), and 4 (pause menu from Back and Home, Resume, Leave, Play again, sleep during play without deadlock) confirmed on the device.
- Epic PR CI: green on 6634faae (Test Status, Crosshatch Test Status, five env builds, unit tests, clang-format, cppcheck, ledger, flash budget with RAM and object checks, API freeze, fork script tests, simulator build, title).
- Flash and RAM deltas are recorded in the epic's Notes.

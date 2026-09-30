---
title: 'Device run and owner sign-off'
type: 'chore'
ticket: '14'
created: '2026-09-30'
status: 'in-progress'
---

# Device run and owner sign-off (entry 14)

The owner ran the packet (`device-run-packet.md` in this folder) on an X4 Pro. The orchestrator recorded each result here as the owner reported it.

## Setup

- **Firmware:** the CI build of `firmware-x4pro.bin` for `134afb4`, from PR #20's "Firmware builds" comment, run 36655380445. The firmware is identical to `216ccbd3`: every commit after it changes only `_bmad-output/`.
- **Packages:** the `game-packages` artifact from run 36653510328, packed at `07c17486`. Its `HASHES.txt` matches the packet.
- **Serial log:** not tethered for steps 0–8, by the owner's choice (2026-09-30). The log is captured only for steps 9 and 10, which need it: the `VM stopped` lines and the forced-exit skip line.
- **Card:** `/games/`, `/.games/`, and `/.games-data/` were cleared first.

## Results

| Step | Result | Notes |
| --- | --- | --- |
| 1. Install two ways (R1, Done when 1) | pass | `counter.chgame` was uploaded with the web file manager, and the other five were copied from a computer. Opening Games showed the "Installing" popup and installed every valid package with no reboot. Five games were listed: Counter, Runaway scripts (`loop`), Packed images, Frame timing, and Package vector. The one failure note read "A Lua file is compiled, not source". The note did not return when Games was opened again. `/games/` then held `invalid-binary-lua.chgame.bad` and no `.chgame`. |
| 2. The list (R5, R8, Done when 3) | pass | Packed images showed its own rings icon, and the other four showed `game-controller`. No row had a second line, because every game can start. Counter opened straight into the match from its row, with no mode picker (Home, Games, the game: three taps). Back, then Leave, returned to Games. The mode picker cannot open on this firmware (`pass` and `nearby` are off), so it stays host-verified only. |
| 4. Play, sleep, Continue (R10, Done when 4) | pass | Counter was played to Taps: 4, then the device was put to sleep mid-match by holding power. The wake landed on Home. In Games, the first row was "Counter" with "Continue" as its second line. A tap on it opened straight to the board at Taps: 4, with no mode step. After Leave, the Continue row was selected. This answers A1 and the "Continue row selected after leaving" assumption on the device. |
| 5. Reinstall the same package (R5) | pass | Before the reinstall, `/.games-data/counter/` held `resume.bin` (29 B) and `store.bin` (17 B). `counter.chgame` was uploaded again and installed when Games opened. The Continue row stayed, because the hash was unchanged. Afterwards the folder held the same two files at the same sizes. |
| 6. Install the changed package (R5, R10, Done when 4) | pass | `counter-changed.chgame` (1.0.1) was uploaded and installed. The Continue row was gone, because the save was from the old package. `/.games-data/counter/` still held `store.bin` and `resume.bin`: the launcher hides an old package's save and does not delete it, as the Notes' `peek` assumption says. Counter opened as a New match titled "Counter v2", and its `ch.store` count carried over (14). |

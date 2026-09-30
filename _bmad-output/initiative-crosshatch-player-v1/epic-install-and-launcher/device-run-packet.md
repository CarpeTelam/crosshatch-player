# Device-run packet: the retrospective's recheck

This packet holds what the owner needs for the device recheck that the owner deferred from entry 14 to the epic-install-and-launcher retrospective (`deferred-work.md` `## owner-e4-entry14`; entry 14's plan, "Deferred to the retro session"). It covers the firmware to flash, the packages, four checks (e4-z1, e4-z2, e4-z3, and A8), and what to record. Record results in the retrospective, `epic-install-and-launcher-retrospective.md` in this folder, under Behavior verification, each with the firmware commit.

This packet was regenerated at the merged commit on 2026-09-30. The entry-14 version (firmware `216ccbd3`, the full run of steps 1 to 11, and the A1–A32 assumption table) is in git history at `d4b30b64`. Its results and the owner's answers are in `story-device-run-and-owner-sign-off-plan.md`.

## Firmware

- **Commit:** `d4b30b64`, the head of CarpeTelam/crosshatch-player#20, merged into `develop` as `fa642c4a` with the same tree. It carries e4-z1, e4-z2, and e4-z3. The entry-14 firmware `216ccbd3` has none of them.
- **Download it:** the PR's "Firmware builds" comment links `firmware-x4pro.bin` from run 36678813150 (artifact 11081730162, built at `d4b30b6`, kept until 2026-12-29). Extract the `.bin` from the ZIP and flash it with CrossPoint Reader Flash Tools.
- **Or build and flash it:** from a checkout of `develop` at `fa642c4a`, run `git submodule update --init --recursive`, then `pio run -e x4pro -t upload`.
- **Serial log (needed for R1):** `pio device monitor -e x4pro` (115200 baud), with `--filter log2file` or piped through `tee recheck.log`. Start it before R1 and keep it running to the end. The lines this recheck reads are all tagged `GAME`:
  - `a call ran over 3000 ms; stopping the VM`
  - `VM cancelled`
  - `VM stopped; arena peak ... bytes, stack high-water ... bytes free, least at a hook ... bytes`
  - `VM did not stop within 500 ms of cancel; abandoning it`
  - `Abandoned the stuck VM: freed its PSRAM, leaked 1032 bytes`
- **Size at this commit** (`check_flash_budget.py`, x4pro, games on minus off): +232,912 B flash and +784 B static RAM, measured at `7e7bcc48`; `d4b30b64` changes only `_bmad-output/` after it. The pass bar is +240,496 B and +808 B.

## Packages

The packages are not committed. They are packed from the fixtures at the firmware commit.

- **From the PR:** run 36678813162 of the `Crosshatch game packages` workflow packed them at `d4b30b64`. Its `game-packages` artifact (11080014576) is kept until 2026-10-30. Open it from the PR's Checks tab.
- **Locally:** from a checkout at `fa642c4a` (or `d4b30b64`), `python3 scripts/pack_device_run.py <out-dir>` writes the same seven files and `HASHES.txt`. The retrospective ran it on 2026-09-30, and it printed the hashes below.

| File | Game | Hash | Used in |
| --- | --- | --- | --- |
| `counter.chgame` | `counter`, version 1.0.0 | `e8c3ac8dfb646d3b` | R1 (installed), R2 |
| `loop.chgame` | `loop` (Runaway scripts) | `c74273851f270beb` | R1 (was `e7ebc00d62486a0f` before e4-z1's recalibration) |
| `timing.chgame` | `timing` (Frame timing) | `ea2741992f220fd8` | R1 (installed), R2 (optional marker check) |
| `pack-images.chgame` | `pack-images` (Packed images) | `5f3b4f61e48de51b` | R1 (installed) |
| `counter-changed.chgame` | `counter`, version 1.0.1 | `fa0d541ee5b21f13` | not used in this recheck |
| `invalid-binary-lua.chgame` | `hardening` | (invalid) | R3 |
| `package_vector.chgame` | `package-vector` | `0530a15766e91bf1` | R4 |

After install, `/.games/<id>/.pkg` holds the hash on its second line. `/.games/loop/.pkg` must read `c74273851f270beb`. The old hash means the card still has the entry-14 package.

## Checks

Start from a card with no `/games/`, `/.games/`, or `/.games-data/` folders, so every result comes from this run. Run the checks in order, because R2 and R4 use games that R1 installs.

### R1. e4-z1: the watchdog cancels "Slow C calls forever" (tethered)

1. Put `loop.chgame`, `counter.chgame`, `timing.chgame`, `pack-images.chgame`, and `package_vector.chgame` in `/games/`, then open Home, then Games. Five games are listed.
2. Check that `/.games/loop/.pkg` reads `v1` / `c74273851f270beb`.
3. Open Runaway scripts, tap "Slow C calls forever", and let it run to the error view.
4. **Expect:**
   - The error view reads `It stopped responding: one step ran over 3 seconds`, after about 3 s, not 3.5 s.
   - The serial log reads `a call ran over 3000 ms; stopping the VM`, then `VM cancelled`, then `VM stopped; arena peak ..., stack high-water ... bytes free, least at a hook ... bytes`.
   - There is no `abandoning it` line and no `Abandoned the stuck VM` line.
5. Tap Back, open Runaway scripts again, and tap "Stuck in one C call". It must still be abandoned: the log shows `abandoning it` and `leaked 1032 bytes`, and the error view comes after about 3.5 s.
6. If step 4 is still abandoned, the device's call is over 500 ms. Record the log lines. The fix is a smaller `n` (e4-z1's plan suggests `backtrackArgs(6, 8)`), and it becomes a new item, not part of this recheck.

### R2. e4-z2: removing a game leaves no folder and no marker

1. Play Counter to a few taps, then leave. Its `/.games-data/counter/` now holds `store.bin` and `resume.bin`. Note both sizes.
2. In Games, long-press Counter's own row (not its Continue row), or hold Confirm on it, then choose Remove.
3. **Expect:**
   - Counter's row and its Continue row are both gone.
   - From a computer or the web file manager, `/.games/counter/` does not exist, and no file named `.removing` is anywhere under `/.games/`.
   - `/.games-data/counter/` still holds `store.bin` and `resume.bin`, at the same sizes.
4. **Optional: a remove that stopped partway.** This checks the marker path on a real FAT card, which the host fake and the simulator cannot show.
   - From a computer, create an empty file `/.games/timing/.removing`, and put the card back.
   - Open Games. Frame timing is not listed, `/.games/timing/` is gone, and `/.games-data/timing/` (if the game made one) is untouched.
   - A folder copied into `/.games/` by hand, with no `.removing` and no `.pkg`, is left in place.

### R3. e4-z3: two invalid packages give one reason and "and 1 more"

1. Put `invalid-binary-lua.chgame` in `/games/`, together with a second copy of it under another name, such as `invalid-2.chgame`. Open Games.
2. **Expect:**
   - The note shows one file's reason (`<file>: A Lua file is compiled, not source`).
   - Below it, on a line of its own, the note shows `and 1 more`, not run into the reason's last word.
   - Both files become `.chgame.bad`.
   - The note does not come back when Games is opened again.
3. Take a photo of the note.

### R4. A8: Games opened from Home lands on the last game's page

This check needs a game on page 2 that has no save.

- **Page size:** the X4 Pro launcher shows eight rows per page (`story-remove-screenshots/page1.png`), not four as the entry-14 record says. Continue rows come first, then games sorted by name.
- **Why Package vector:** it saves nothing, since it stops at once with "game stopped". With eight extra games named "Aa extra 1" to "Aa extra 8" sorted ahead of it, it lands on page 2 whatever else is installed.

1. From a checkout at `fa642c4a`, pack the eight extra games. Each is the `counter` fixture under a new id and name:

   ```sh
   mkdir -p extra
   for i in 1 2 3 4 5 6 7 8; do
     d=$(mktemp -d)/extra$i
     cp -r test/game_script/fixtures/counter "$d"
     sed -i "s/\"id\": \"counter\"/\"id\": \"extra$i\"/; s/\"name\": \"Counter\"/\"name\": \"Aa extra $i\"/" "$d/manifest.json"
     python3 scripts/pack_game.py "$d" extra/extra$i.chgame
   done
   ```

   On macOS, write `sed -i ''` in place of `sed -i`. The retrospective packed them on 2026-09-30, and the hashes printed were `457203b4c567adc8`, `3c36bd82a9a27878`, `ff7b3ca0e1663fc9`, `c5e336a3a155ab6d`, `f0fa2793e31f83d7`, `8ff5f0c7bb727378`, `8e67ec8ef629e495`, and `cc566037021bbf1c`.
2. Put the eight files in `/games/` and open Games. The list now runs past one page, and Package vector is on page 2.
3. Page to page 2, and open Package vector. It shows the "game stopped" error view. Press Back to return to Games. The launcher is on page 2 with Package vector selected (entry 10's return to the page).
4. Press Back to Home, then open Games from Home.
5. **Expect:** the launcher opens on page 2 with Package vector selected, not on page 1 (A8).
6. If a Continue row for Package vector appears after step 3, the game did save. Then the launcher correctly selects that Continue row on page 1 (A12). Record it, and repeat step 3 with the last game on page 2 that has no Continue row.
7. Remove the extra games afterwards if you like. Removing is R2's path, so each remove also rechecks e4-z2.

## What to record

- The firmware you flashed: the CI artifact, or a local build of `fa642c4a`.
- R1: the serial lines from the tap to the error view for both bands, and the time to the error view.
- R2: the `/.games/` and `/.games-data/counter/` listings before and after, and whether the optional marker check was run and passed.
- R3: the photo of the note, and the names of the two `.bad` files.
- R4: which page opened and which row was selected, with a photo.
- Anything else seen: a reset, a watchdog banner, or a `forced exit past 1500 ms` line.

## Owner question (from `deferred-work.md` `## e4-z3`)

When the first failure in a visit is the 64-game limit, the note reads "Too many games are installed; remove one first" and then "and N more". This can read as "remove N+1 games". The retrospective records a recommended answer under Open questions. The device cannot show this case without 64 installed games, so the recheck does not include it.

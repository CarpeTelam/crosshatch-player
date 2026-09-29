# Device-run packet: entry 14

This packet holds what the owner needs for entry 14 of epic-install-and-launcher: the firmware to flash, the packages, the steps, what to record, and every `Assumption for entry 14:` line to answer. Record results in entry 14's plan, each with the firmware commit (entry 14's verify line). A failure becomes a new story in this epic's PR, with its own combined-diff review (the Notes' unattended-run Decision).

## Firmware

- **Commit:** `b34aca51` on `claude/epic-install-launcher-orchestrate-s01gz1` (the epic PR's branch). Later commits on the branch that change only `_bmad-output/` leave the firmware identical; if code changes after this packet, the PR says so and this line is updated.
- **Build and flash** an X4 Pro from a checkout at that commit: `git submodule update --init --recursive`, then `pio run -e x4pro -t upload`.
- **Serial log:** start it before the first step and keep it for the whole run, with `pio device monitor -e x4pro` (115200 baud). Add `--filter log2file`, or pipe it through `tee device-run.log`. It carries:
  - the installer's lines and reasons (tag `GAME`);
  - `VM stopped; arena peak ... bytes, stack high-water ... bytes free, least at a hook ... bytes` after every match ends (epic-script-runtime retro AI-3);
  - a `forced exit past 1500 ms; skipped ...` line if the forced exit ran out of time;
  - any watchdog or reset banner.
- **Size at this epic's end** (`check_flash_budget.py`, x4pro, games on minus off): see the Notes' last `Measurement` line. The pass bar is +240,496 B flash and +808 B static RAM.

## Packages (`device-run/`)

Each package's hash is the last line `scripts/pack_game.py` printed; `.pkg` on the card holds it after install.

| File | Game | Hash | Why |
| --- | --- | --- | --- |
| `counter.cpgame` | `counter`, version 1.0.0 | `e8c3ac8dfb646d3b` | `ch.store`, the save, Continue, reinstall of the same package |
| `counter-changed.cpgame` | `counter`, version 1.0.1, title "Counter v2" | `fa0d541ee5b21f13` | A changed package: its Continue row must go, and `/.games-data/counter/` must stay |
| `loop.cpgame` | `loop` (Runaway scripts) | `e7ebc00d62486a0f` | The abandon path, the "VM stopped" line, and a sleep during a stuck call |
| `timing.cpgame` | `timing` | `ea2741992f220fd8` | The three replay bands (`test/game_script/fixtures/README.md`, "Timing run") |
| `pack-images.cpgame` | `pack-images` (Packed images) | `5f3b4f61e48de51b` | A package with `icon.png` (96 x 96 rings, converted to the 64 px row icon) and two images |
| `invalid-binary-lua.cpgame` | `hardening` | (invalid) | From entry 6's generator (`binary-lua-stored`): must become `.bad` with "A Lua file is compiled, not source" |
| `package_vector.cpgame` | `package-vector` | `0530a15766e91bf1` | The shared hash vector: the device's mbedTLS hash must match it (R4) |

## Steps

Start from a card with no `/.games/`, `/.games-data/`, or `/games/` folders, so every result is this run's.

1. **Install two ways (R1, Done when 1).**
   - Put `counter.cpgame` in `/games/` with the web file manager.
   - Copy `pack-images.cpgame`, `loop.cpgame`, `timing.cpgame`, `invalid-binary-lua.cpgame`, and `package_vector.cpgame` onto the card's `/games/` from a computer.
   - Open Home, Games. Record:
     - whether every valid package installs with no reboot;
     - the "Installing" popup;
     - the one `.bad` note, with its reason, shown once and not again on the next visit;
     - `/games/invalid-binary-lua.cpgame.bad` on the card.
2. **The list (R5, R8, Done when 3).**
   - Check that each row shows its icon: `pack-images` its own rings, the others their manifest icon or `game-controller`.
   - Open `counter` from Home in at most 3 taps (Home, Games, the game; the mode picker is skipped with one mode).
   - The mode picker cannot open on this firmware (`pass` and `nearby` are off), so R8's picker is host-verified only (the cross-story review's row 12).
3. **The hash on the device (R4, Done when 2).** On the card, `/.games/package-vector/.pkg` must read `v1` on one line and `0530a15766e91bf1` on the next. Also check `/.games/counter/.pkg` against the table above.
4. **Play, sleep, Continue (R10, Done when 4).**
   - Play `counter` to a few taps. Put the device to sleep mid-match, then wake it: it must land on Home (R10).
   - Open Games: "Counter / Continue" is the first row.
   - Tap it: the board shows the same taps, with no mode step.
   - Leave: the launcher selects the Continue row, on its page.
5. **Reinstall the same package (R5).**
   - Put `counter.cpgame` in `/games/` again and open Games.
   - Record that `/.games-data/counter/` (`store.bin`, `resume.bin`) is unchanged: same files and sizes, checked from a computer or the web file manager.
   - The Continue row stays, since the hash is the same.
6. **Install the changed package (R5, R10, Done when 4).**
   - Put `counter-changed.cpgame` in `/games/` and open Games.
   - The Continue row is gone (a save from a changed package is discarded), and `/.games-data/counter/` still holds `store.bin`.
   - Open it: the title reads "Counter v2", and "Saved taps" still shows the kept `ch.store` count.
7. **Remove (R5, Done when 2).**
   - Long-press `pack-images`' row (or hold Confirm on it). The confirmation names the game, and Cancel is focused.
   - Choose Remove: the row goes, `/.games/pack-images/` is gone, and `/.games-data/pack-images/` (if the game made one) is untouched.
   - Also remove `counter`, and check that `/.games-data/counter/` survives.
8. **Timing run (e3r-1, retro AI-10).** Follow `test/game_script/fixtures/README.md`, "Timing run", steps 0–3, for `timing`. Record each band's tap-to-picture time, the menu's time, and any reset or watchdog line.
9. **Runaway scripts and the "VM stopped" line (epic-script-runtime retro AI-3).**
   - Open `loop` and run each band from `test/game_script/fixtures/README.md`, "Fault bands".
   - Each must end in the error view with its text, Back must return to Games, and the device must stay responsive.
   - After each, copy the serial `VM stopped; ...` line: the arena peak, the stack high-water, and the least free at a hook.
   - For "Stuck in one C call", record the abandon path: about 3.5 s to the error view.
10. **Sleep during a stuck call (R11).**
    - Open `loop`, start "Stuck in one C call", and put the device to sleep within the first 3 s.
    - Record the time from the sleep press to the sleep screen: a phone video counted in frames is enough.
    - Record whether a `forced exit past 1500 ms` line appears.
    - The bound in `docs/crosshatch/game-canvas.md` ("The forced exit") is about 1,030 ms of `RenderLock` before the SD steps, and no SD step starts after 1,500 ms.
11. **Answer every assumption** in the table below: agree, or say what should change.

## What to record

- The firmware commit, above.
- For each step: pass or fail, and what the device showed, with photos where a screen says more.
- Step 1: which packages installed and the `.bad` reason text.
- Step 3: the two `.pkg` files' contents.
- Step 5 to step 7: the `/.games-data/<id>/` listings, before and after.
- Steps 8 to 10: the timings, the `VM stopped` lines, and any reset.
- The answer to each assumption.

## What the host cannot show

These rest on this run (the cross-story review's rows 12 and 13):
- R10's wake on Home;
- R4's mbedTLS hash;
- R11's device bound;
- R15;
- R1's install-then-list path joined on real hardware.

The mode picker (R8) is host-only on this firmware.

## Owner decision needed

The forced exit also retries a `resume.bin` delete that Over could not finish. Spine AD-17 lists only the resume write and the `ch.store` flush among `onExit()`'s SD writes. Decide whether AD-17 and AD-20 are amended to name the retry, or the retry goes. The Notes' assumption line for the cross-story review's row 11 carries this.

## Assumptions for entry 14

Every `Assumption for entry 14:` line in the epic Notes, verbatim, in order. Answer each in the last column.

| # | Assumption | Owner's answer |
| --- | --- | --- |
| A1 | the launcher shows one "Continue" row per game with a valid save, above the game rows and sorted by name, and a tap resumes the match with no mode step (inception, 2026-09-28). | |
| A2 | a game with neither `icon.png` nor a manifest `icon` shows `game-controller` in its launcher row (inception, 2026-09-28). | |
| A3 | a manifest `icon_weight` other than `regular` or `fill`, like any other malformed manifest value, makes the package invalid (inception architecture update's open question, 2026-09-28). | |
| A4 | a non-square `icon.png` is rejected, not scaled and cropped; `scripts/pack_game.py` still accepts one (deferred to entry 7, which touches it). (entry 3, 2026-09-29) | |
| A5 | after a power loss during the folder move, `/.games-tmp/<id>` and `/.games/<id>` (no `.pkg`) may share clusters. Where `/.games/<id>` has no `.pkg` the installer probes: it makes an empty `.xlink` in `/.games-tmp/<id>` and looks for it in `/.games/<id>` (folders on one chain share their directory data, so it shows in both), then removes it. Shown, or not makeable or removable: the installer never deletes that `/.games-tmp/<id>`, logs "Keeping /.games-tmp/<id>", leaves the inbox file, and reports the SD card reason on every visit that installs that id, until a person clears both folders from a computer (a filesystem check first is advisable). Not shown: the folders are independent (a stop during the removal of the old folder), the scratch folder is deleted, and the install goes on. The residual risks: the probe rests on SdFat's cross-linked entries sharing directory data, which the host suite can only model and a device run has not shown; and a card left cross-linked by the power loss itself, which the installer neither repairs nor worsens. (entry 3, 2026-09-29) | |
| A6 | installer reasons are mapped to `tr()` text in the Games screen (`GamesListActivity`, renamed `GamesLauncherActivity` by entry 8), where entry 6 added its eight labels with the orchestrator's approval; moving the map into the installer would need a `src/games` to `lib/I18n` edge the spine does not have (entry 6, 2026-09-29). | |
| A7 | the forced exit's SD work is bounded by a deadline of 1,500 ms from the start of `onExit()` (`GameMatchActivity::FORCED_EXIT_DEADLINE_MS`). The resume write, the `resume.bin` delete retry, and the `ch.store` flush each start only inside it; past it each is skipped and logged (`forced exit past 1500 ms; skipped ...`), so a slow card can lose the last move or the last `ch.store` write rather than hold `RenderLock`. Inside it the total is the VM waits (about 1,030 ms on a device whose polls run on time) plus the SD steps that started before the deadline, each one tmp write and rename at the card's speed. The owner confirms the figure on the device (AI-3). (entry 11, 2026-09-29) | |
| A8 | opening Games from Home also lands on the page of the last game opened, until that game is removed or the device restarts (the fingerprint is not cleared by Back to Home; the owner confirms it on the device). (entry 10, 2026-09-29) | |
| A9 | a long-press (or a Confirm hold) on a Continue row does nothing; removing a game is on the game's own row, so a person is never asked to remove a game from the row that resumes it. (entry 12, 2026-09-29) | |
| A10 | a Continue row shows the game's name with "Continue" as the second line, in place of an unavailable reason (a game that cannot start has no Continue row); it uses the game's icon. (entry 12, 2026-09-29) | |
| A11 | no Continue row for a game whose `check` is not Ok; its save is not read. (entry 12, 2026-09-29) | |
| A12 | after leaving a match of a game that still has a save, the launcher selects that game's Continue row, on the page holding it (the orchestrator's answer to a review finding: selecting the game's own row let one Confirm replace the save). (entry 12, 2026-09-29) | |
| A13 | a tap on a game's own row while it has a save starts a New match, which replaces the save (one save slot per game, entry 11); there is no confirmation. (entry 12, 2026-09-29) | |
| A14 | the Continue list is rebuilt, with its `peek`s, on entry and after every remove; it is not carried over from the previous listing. (entry 12, 2026-09-29) | |
| A15 | `## e3r-x`'s inert pause menu: deferred because a new string and view state are a UX choice; recommend a one-line "Starting the next round" under the paused headline. (entry 13, 2026-09-29) | |
| A16 | `## e3r-1`'s unbounded fills (`rect`, `clear`, `circle`): deferred to the `timing` fixture's band 3 (2,048 full-canvas rects), since a bound is a new limit before the freeze; the run decides. (entry 13, 2026-09-29) | |
| A17 | Retro R10 (watchdog, timer poll, store flush pause under the light panel): deferred with the trigger "the next ledger change" (`FrontlightPanelActivity.cpp`), as decided at inception; R8 (d) waits for the next row-8 touch. (entry 13, 2026-09-29) | |
| A18 | `## 4.10`'s unlisted folder after a partial remove: deferred because a sweep deletes hand-copied folders; recommend a `.removing` marker written before the `.pkg` goes, and finish only marked folders. (entry 13, 2026-09-29) | |
| A19 | `## 4.6`'s extracted-sum bound: deferred because a sum is a new limit in the contract; recommend a free-space check from the directory pass's declared sizes. (entry 13, 2026-09-29) | |
| A20 | `## 4.11`'s save kept after a rejection at the first call: deferred because a heap error is a script error, so deleting could lose a good save; trigger a distinct run-time out-of-memory failure kind. (entry 13, 2026-09-29) | |
| A21 | `ZipFile` and `PngToBmpConverter` bare-`false` reasons: deferred because both are upstream files without a ledger row; recommend one row and an enum beside the `bool` API. (entry 13, 2026-09-29) | |
| A22 | `## 4.3`'s only-the-first-failure popup: deferred as a UX change; recommend "and N more" from the counted `Report.failed`. (entry 13, 2026-09-29) | |
| A23 | The `goHome` mapping test and the `## 4.4`/`## 4.1` drift guards and layout pins: deferred because each needs an upstream edit beyond row 5 or a `src/` seam; add at the next touch, and use `sim.sh ss` diffs for layout. (entry 13, 2026-09-29) | |
| A24 | `## 4.5`'s launcher-with-the-installer-scripted and Home-doubles items: deferred because a joined suite needs the converter's stubs and the screen doubles in one target (they clash) and the firmware build already compiles the real Home; entry 14's install-then-open-Games visit covers the joined path once. (entry 13, 2026-09-29) | |
| A25 | Mode passing and pass-and-play Continue: deferred to epic-pass-and-play and epic-play-nearby, unreachable while `pass` and `nearby` are off. (entry 13, 2026-09-29) | |
| A26 | Device measurements (resume write cost, `peek` time at entry, how much of the panel's refresh is behind `displayBuffer`, `GameHash`'s mbedTLS read-back of `package_vector.cpgame` as `v1` then `0530a15766e91bf1`): entry 14's steps; the fixtures README's "Timing run" lists them. (entry 13, 2026-09-29) | |
| A27 | `## 4.10`'s fixed row height and a queued second Remove tap: deferred, not shown at the shipped fonts; trigger a theme change or a device report. (entry 13, 2026-09-29) | |
| A28 | a valid package that would be the 65th game stays in `/games` with its reason shown on every visit until a game is removed; it is not renamed `.bad`, since it is not invalid. (e4-x, 2026-09-29) | |
| A29 | a package that arrives while 64 games are installed is refused with "too many games" right after its manifest is read, whether or not the rest of it is valid; an invalid one is judged, and set aside as `.bad` with its own reason, only once a game has been removed and there is room. (e4-x, 2026-09-29) | |
| A30 | an installed package whose inbox file will not delete is moved to `<name>.cpgame.installed` (or `.installed.2` to `.installed.5` when an earlier copy under that name will not go) and counted as installed; the leftover file is harmless and a person may delete it from a computer. (e4-x, 2026-09-29) | |
| A31 | a power loss during `resume.bin`'s rename can leave it and `resume.bin.tmp` on one cluster chain (SdFat writes the new directory entry before it removes the old one), and the next write would then free the save's clusters; `store.bin` has had the same exposure since before this epic. Deferred: a guard is a storage design change (the cross-story review's row 10). (orchestrator, 2026-09-29) | |
| A32 | the forced exit also retries the `resume.bin` delete (a finished round's save that did not delete at Over), which spine AD-17 does not list among `onExit()`'s SD writes; the Notes and `formats.md` record it. The owner decides whether AD-17 and AD-20 are amended to say so (the cross-story review's row 11). (orchestrator, 2026-09-29) | |

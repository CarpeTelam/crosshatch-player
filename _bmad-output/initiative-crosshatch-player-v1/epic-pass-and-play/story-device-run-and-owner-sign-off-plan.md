---
title: 'Device run and owner sign-off'
type: 'chore'
ticket: '11'
created: '2026-10-03'
status: in-progress
---

# Device run and owner sign-off (entry 11)

The owner ran the packet (`device-run-packet.md` in this folder) on an X4 Pro, over USB serial with the Chrome Labs Serial Terminal at 115200. Each result is recorded as the owner reported it, and the timings are read from the two serial logs the owner pasted. A step the logs do not show is marked "owner's word".

## Setup

- **Firmware:** a CI build of PR #24 at or after `64842706` (the commit that added the `pushed in` and `dropped` log lines, which both logs contain). The exact commit and run were not recorded. Its firmware sources equal the packet's `6cf7f65b` plus those log lines.
- **Packages:** the `game-packages` artifact of the label-gated `Crosshatch game packages` workflow, which packs `slow-restart`, `pass-title` and `pass-store` as well since `2dadb2fd`. The device listed 11 games. The hashes were not compared on the device (not recorded).
- **Serial log:** three pastes: Part A (to 538 s after boot), Part B's first part (to the deep sleep that ended it), and Part B's rest, from a fresh boot (`pass-store`, `pass-title`, `slow-restart`). The terminal was connected after the installs, so the install notes of R2 and R3 are not in either.

## Results

| Part | Result | Evidence |
| --- | --- | --- |
| A, R1 "Slow C calls forever" | pass, now calibrated | `loop: a call ran over 3000 ms; stopping the VM`, `VM cancelled`, `VM stopped` 5 ms later, then the error view; no `abandoning it` |
| A, R1 "Stuck in one C call" | abandoned as expected, leak differs | `VM did not stop within 500 ms of cancel; abandoning it` 506 ms after the 3,000 ms line; `leaked 1240 bytes`, where the packet and entry 14 say 1,032 |
| A, R2, R3, R4 and the rest | owner's word: nothing has changed | R2 and R3 are not in the log; R4 was not staged (the eight extra games are hand-packed and not in the artifact) |
| B0–B8 | owner's word: working as expected; logs back most steps | The logs show matches of `pass-open`, `pass-hidden`, `pass-art`, `pass-store`, `pass-title` and `slow-restart`. B5.2: `pass-title` read `title.bmp` for the hand-off band (480x480). B1.4: Options cycled and wrote `prefs.bin` (30 bytes). B8.1: `slow-restart` Play again to round start took 2,911 ms and 2,919 ms with no error view, so its `setup` stayed under the 3 s watchdog (the call's own time is not logged). B7.5: `ch.store` was saved after `Playing -> Result` and before the Result screen finished its push (at 198409, push done 198432), so a store write did land in the window. **Not in any log: B7.6**, a sleep with a dirty store (the `pass-store` matches ended by Leave) |
| C, the unreadable `.pkg` | not staged (owner, 2026-10-03) | The host test `ResumeMatchTest.AContinueWhosePkgWillNotReadStopsInTheErrorViewAndKeepsTheSave` covers it |

## Timings

| ID | Measure | Result |
| --- | --- | --- |
| T1 | `resume.bin` write after a move (`Playing -> Result` to `saved resume.bin`) | 91–92 ms in 8 of 9 hand-offs (one 50 ms) |
| T2 | `peek` added when the title screen opens | 7–9 ms, with a save or none |
| T3 | Half refresh inside the forced exit (`VM stopped` to `blank screen pushed`) | 1,654 ms (`lib/hal/HalDisplay.h` says 1,720 ms) |
| T4 | The forced exit's window | `onExit()` started at 1086823 and the blank finished at 1088479: 1,656 ms, past `FORCED_EXIT_DEADLINE_MS` (1,500 ms). No `skipped` line, because no SD step was pending (`pass-hidden` writes no `ch.store`, and its resume was saved) |
| T5 | Loop stalls in Result and the hand-off | measurable for one match: after the wake the bar read 228 ms (third log, 190385), and `pass-store`'s first match (193371 to 206930), with its store writes in the window, logged no new maximum, so no pass there ran over 228 ms (the time guard's late-pass slack was 250 ms). The next line, 1,884 ms at 207156, is the Leave's blank push before the launcher, not Result or the hand-off, and it hides later stalls. Earlier matches ran with the bar already at 1,034 ms (Part A log, line 570) |
| T6 | A `ch.store` write | the move (`Playing -> Result`, or `Round over`) to `saved ch.store`, four writes: 42 ms and 96 ms, then 489 ms and 656 ms where the write came after the resume save and before the Result push finished (upper bounds, as the packet says) |

Push times, from the new log lines: the hand-off screen 1,678 ms (`pass-hidden`) and 1,720–1,722 ms (`pass-art`, with its band image), both full refreshes; the first frame after "I'm ready" and after a launch 1,661–1,664 ms (full); the Result banner, later frames and the Paused screen 671–688 ms (fast); the Error screen 1,655–1,658 ms (full).

## Findings

1. **Taps are dropped during a full refresh (owner report, 2026-10-03).** "I'm ready" and the first move after it need several taps. The two Part B logs have 28 hand-off screens, and show 50 touches dropped on the move screen before its frame was on the panel, 34 taps on the hand-off screen before it was on the panel, 7 that began before its push finished, 7 on the Result banner, and 11 made under another seat's frame (the last is the existing guard working). One hand-off dropped 7 taps in 1.5 s. Every drop lies inside a push, 1.66–1.72 s long. The match drops a tap until `displayBuffer` returns. The guard that stops the second tap of a double tap from passing the hand-off must stay, since a looser one would show seat 2's secret to seat 1. Options: a fast refresh for the first frame after the hand-off (about 675 ms instead of 1,663 ms, at the risk of a ghost of the hand-off band; B3's ghost check raised none); keep the hand-off screen's full refresh, which clears seat 1's secret.

   **Outcome (Owner Decision, 2026-10-03; `story-plain-tap-targets-for-the-hand-off-plan.md`, ticket 13):** neither option. The time guard is removed instead: the banner, "I'm ready" and Confirm are plain tap targets that act on release even mid-refresh, and the first move tap on a seat's frame after "I'm ready" is accepted during that frame's push (a contact whose touch-down preceded the hand-off passing is still dropped). The full refreshes stay, the layout is unchanged, and an open-pass touch begun under one seat's frame and lifted under another's is still dropped. B3 is re-run on the device (the owner).
2. **The blank's half refresh outlasts the forced exit's window (P4, P10; deferral 5.6).** T3 and T4 above. A pending `ch.store` flush or resume write after the blank would be skipped. The log has no such case, so this is unconfirmed; B7.6 (`pass-store`, two moves under 5 s apart, then sleep at once) would show `skipped the ch.store flush`. If it does, the owner decides between accepting the skip, `displayBufferAsync`, and widening the window.
3. **The abandoned VM leaks 1,240 bytes, not 1,032.** The abandon itself works. Cause not investigated.

## Open

- B7.6 run (`pass-store`, two moves under 5 s apart, then sleep at once), or the owner's decision to accept the P4 risk without it.
- The owner's decision on Finding 1.
- The owner's answers to P1–P20, the Decision-121 line, and each deferral row (the packet's two tables).
- The firmware commit and artifact run actually flashed, if the owner wants them recorded.

## Sign-off

Pending.

---
title: 'e6pre-3: record the link-on-its-own-task decision (R10 option b)'
type: 'chore'
ticket: ''
created: '2026-10-04'
status: 'done'
route: 'full'
route_source: 'pinned'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '1a094c94'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Retro R10: under an overlay (the light panel) only the overlay's `loop()` runs, so the match loop's watchdog, timer poll, and store flush pause. For Play Nearby, a link pumped from that loop would stall and drop the peer.

**Approach:** The owner chose option (b) (2026-10-04): the link pump and peer-silence detection run on the `GameLink` task, so no `ActivityManager` change and no ledger row. Record it in `game-canvas.md` Overlays, the epic-play-nearby Notes, and `deferred-work.md` (`## e6pre-3`). Docs only; the link is not built; the spine is untouched.

</frozen-after-approval>

## Implementation Notes

- `docs/crosshatch/game-canvas.md` (fork file): Overlays gains the decision, what pauses, what must not, and the task's constraint (no `RenderLock`, no activity state).
- `epic-play-nearby.md` Notes: one Decision line.
- `deferred-work.md`: `## e6pre-3`, two entries (nearby part decided; solo/pass pause stays open with its trigger). Existing R10 entries are not edited (union merge); the new entry names them.
- Spine sentences for a later pass: `ARCHITECTURE-SPINE.md` lines 236 (ReliableLink drop at 10 s), 305 (Prevents), 309 (GameLink bullet), 328-329 (AD-20 Prevents and Rule), 338 (10 s silence to PeerGone); recorded as a `deferred-work.md` entry.

## Review Triage Log

One context-free subagent ran the three lenses over the staged diff and returned. Accepted and fixed: unfinished plan (this log), spine list not durable (deferred entry), PeerGone transition still waits for loop() (stated in all three places), queue overflow (stated as an epic-play-nearby item), AD-11 miscitation (dropped), mixed deferred entry (split). Verified-true: loopPlaying, 3 s watchdog, pollTimer/flushIfDue, ledger rows 4 and 5.

## Verification

`./bin/clang-format-fix` exit 0, no new changes; `python3 scripts/check_upstream_touches.py` PASS. No code changed, so no builds or host tests.

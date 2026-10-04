---
type: epic
title: "Two devices play one match over ESP-NOW"
parent: initiative-crosshatch-player-v1
covers: [CAP-5, CAP-7]
after: []
assignee: ""
risk: high
---

# Two devices play one match over ESP-NOW

## Description

Adds Play Nearby: the wire protocol and reliable link in `GameCore`, tested over a lossy fake link, the ESP-NOW adapter and link task, the host lobby and guest join, and clean endings when a peer leaves or goes silent. It measures the spec's open radio questions.

## Outcome

Two players each on their own device finish a round of an unmodified game; the nearby part of CAP-5 and the peer-left part of CAP-7 are the signal.

## Requirements

Completed at inception. This epic owns the Play Nearby part of CAP-5 and the peer-left part of CAP-7.

## Done when

1. `Protocol`, `ReliableLink`, and `Session` pass host suites over a `FakeLink` that drops, delays, duplicates, and reorders frames.
2. Two X4 Pros finish a round of the same unmodified package in Play Nearby: host lobby, guest join, seat assignment, moves, a rejected move, and Play again.
3. A peer that leaves or is silent for 10 s brings up "player left" on the other device, and a script error on one device ends the match on both.
4. The lobby never opens while the web server or other Wi-Fi is up, refuses to open below 100 KB free internal heap, and the radio is off after leaving; the simulator envs build with `EspNowLink` and nearby compiled out.
5. Reliability, battery cost, and internal heap after teardown are measured, and the 400 ms, 10 s, and 100 KB values are confirmed or changed and recorded in `docs/crosshatch/` and the spine's Deferred rows.
6. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

`GameCore` Protocol, ReliableLink, and FakeLink; `EspNowLink`, `NearbySession`, and the GameLink task in `src/games`; `GameLobbyActivity`; the Lobby and PeerGone states. Not reconnect, more than two seats, or saving nearby matches (spec Non-goals).

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-11, AD-13, AD-18, AD-20, AD-21, Deferred
- spec — _bmad-output/specs/spec-crosshatch-player/SPEC.md, Open Questions

## Notes

- Unknown: ESP-NOW reliability, battery cost, Sticky and mixed-pair behaviour, and heap recovery after teardown (spec Open Questions); Done when items 2, 3, and 5 wait on a second device, and link and session work proceeds against `FakeLink` until it arrives.
- Risk high: a person confirms the two-device round on hardware.
- Waits on epic-install-and-launcher because: the mode picker and `Manifest` checks for the lobby.
- Waits on epic-pass-and-play because: the N-seat `Session` and `Roster` and the Over / Play again flow.
- Budget (epic-install-and-launcher inception, owner, 2026-09-28): this epic's share of the flash and static-RAM headroom left on PR #18's final tree (27,504 B and 248 B) is 9,504 B flash and 160 B static RAM; epic-install-and-launcher has 12,000 B and 32 B, and an unallocated reserve of 2,000 B and 24 B remains. The icon-compression story runs as soon as a measurement leaves less flash headroom than the unstarted epics' shares plus the reserve. Measure this epic's delta against a base measured before its first story (`docs/crosshatch/orchestrated-epics.md`).
- Budget update (epic-pass-and-play inception, owner, 2026-10-01): this epic's share is unchanged at 9,504 B flash and 160 B static RAM. Epic-install-and-launcher left 7,152 B and 24 B unspent: the flash went to epic-pass-and-play (11,152 B / 32 B) and the static RAM to the reserve, now 2,000 B / 48 B (spine Operational envelope, Flash budget row).
- Decision (owner, 2026-10-04, e6pre-3, retro R10 option (b)): the ESP-NOW link pump and the peer-silence ("silent for 10 s") detection run on the `GameLink` task, never in `GameMatchActivity::loop()`, so an overlay such as the light panel cannot pause them (the match's move to `PeerGone` still waits for `loop()`, so for the overlay's duration). Constraint: the `GameLink` task never takes `RenderLock` and never touches activity state; it hands events to the match through the session's queue, whose overflow policy for a long overlay this epic states (a lost-peer event is never dropped). No upstream `ActivityManager` change and no new ledger row. The watchdog, timer poll, and store flush still pause under an overlay in solo and pass matches (`docs/crosshatch/game-canvas.md`, Overlays; `deferred-work.md`, `## e6pre-3`).
- Decision (owner, 2026-10-04, e6pre-8): the move to `PeerGone` runs on the match's next `loop()`, so it waits while an overlay (the light panel) is open; the owner accepts it. The risk is an overlay held open indefinitely, with the peer already gone. Spine AD-18, AD-20, and AD-21 say so (amended 2026-10-04).
- Requirement for the first story's design (e6pre-8, from e6pre-3's review): the session queue between the `GameLink` task and the match is depth-bounded, so this epic states its overflow policy before building it. A lost-peer event is never dropped; the rest of the policy is this epic's to state.
- First story (e6pre-8; `deferred-work.md`, `## e5-xr`, "Blocks epic-play-nearby"): a roster with some seats local and some not stalls on four paths, and this epic fixes and tests them before anything else. (1) A local move that passes the turn to a non-local seat is never drawn (`SoloRounds::step` → `drawShown`, `NO_SEAT`). (2) A hidden hand-off to a non-local turn seat leaves the VM in HandOff with the request unserved (`GameVM::showSeatNow`). (3) A timer due while the turn seat is non-local is dropped (`SoloRounds::step`, `NO_SEAT`). (4) A round whose first turn is non-local publishes no frame, so `roundsStarted` never moves. The first story also decides what a device shows while a remote seat moves.
- Measure first (e6pre-8): before the first story, measure the x4pro flash and static-RAM base (`scripts/check_flash_budget.py`, as the Budget lines above say) on the commit this epic starts from; this epic's 9,504 B and 160 B are deltas over that base, and the earlier epics' figures are over different bases (`docs/crosshatch/orchestrated-epics.md`).
- Spine as built (e6pre-8): `GameScript::MatchRounds` (renamed from `SoloRounds` in e6pre-12) already runs any roster; `MatchLifecycle` has no `Lobby` or `PeerGone` yet, which this epic adds (AD-21).

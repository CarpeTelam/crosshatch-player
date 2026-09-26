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

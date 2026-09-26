---
type: epic
title: "One device passes between players and hidden information stays hidden"
parent: initiative-crosshatch-player-v1
covers: [CAP-5, CAP-6, CAP-7]
after: []
assignee: ""
risk: medium
---

# One device passes between players and hidden information stays hidden

## Description

Adds multi-seat play on one device: the roster and turn enforcement for N seats, per-seat `ui`, the runtime-owned hand-off for hidden games, game over with seat 0, Play again, and resume of pass matches.

## Outcome

Two players share one device and neither sees the other's private view; the CAP-6 success check and the pass part of CAP-5 are the signal.

## Requirements

Completed at inception. This epic owns the pass-and-play part of CAP-5, CAP-6, and the pass part of CAP-7, plus the pass half of CAP-4's 3-tap check.

## Done when

1. A hidden two-player fixture game and an open one each play a round in pass-and-play, started from Home in at most 3 taps, with turns enforced from `status` and out-of-turn moves impossible.
2. In a `hidden = true` game every change of turn seat, every new or resumed round, and every sleep shows the blank hand-off screen with a full or half refresh before the next seat is drawn, and a script cannot suppress it, checked on an X4 Pro.
3. Play again, game over with seat 0, and one `over` event per local seat work.
4. Sleeping mid-match and choosing Continue restores a pass match, through the hand-off when the game is hidden.
5. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

`Roster` for pass with n chosen within `seats`, `Session` for N seats with no n = 2 assumption, per-seat `ui`, the Result and HandOff states, the `onExit()` blank push, and the mode picker's seat choice. Not the radio (epic-play-nearby).

Handoffs: epic-play-nearby consumes the N-seat `Session` and `Roster` and the Over / Play again flow.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-8, AD-9, AD-11, AD-12, AD-17, AD-21, AD-22

## Notes

- Waits on epic-script-runtime because: `Session` and the match lifecycle.
- Waits on epic-install-and-launcher because: the mode picker and resume.

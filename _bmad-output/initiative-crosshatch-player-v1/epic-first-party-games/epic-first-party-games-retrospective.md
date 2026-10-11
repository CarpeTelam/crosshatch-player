---
epic: epic-first-party-games
date: 2026-10-11
verdict: rejected
criteria: declared
headless: false
---

# Retrospective: epic-first-party-games

Status: in progress (Phase 1 done). Run interactively. The machine verdict is below, and a human decision overrides it.

## Epic summary

Epic: `epic-first-party-games` (id 8), "Three first-party games ship with each release". Risk low. Criteria: declared (the epic file has six Done when items).

Tickets (from `tickets.py status`, build order):

| ref | title | status | state |
|-----|-------|--------|-------|
| 8.8 | The games check | done | done |
| 8.1 | Tracer: the games check and Ultimate tic-tac-toe | done | done |
| 8.7 | Pass resume when a move keeps the turn | done | done |
| 8.2 | Battleship | done | done |
| 8.3 | Sudoku | done | done |
| 8.4 | Refactor sweep | done | done |
| 8.10 | Raise the package file cap to 64 | done | done |
| 8.9 | Sudoku digit notes in three bold sets, and the greys swapped | done | done |
| 8.11 | Check every game at the X4 Pro's 466-pixel canvas | done | done |
| 8.12 | Fix the release script's game-packer path | done | done |
| 8.13 | Stop the game clock while the match is paused | done | done |
| 8.14 | Log each game touch, and give Battleship and Sudoku their launcher icons | done | done |
| 8.15 | Count a tap in the margin beside a game's edge cells as that cell | done | done |
| 8.16 | Show Sudoku's play time in the header, with a TIMER toggle in the menu | done | done |
| 8.17 | Make Sudoku's NOTES AS default to DIGITS | done | done |
| 8.5 | Device run and owner sign-off | done | done |
| 8.6 | Fork release with the three games | — | **planned** |

`pending_tickets`: **8.6** ("Fork release with the three games", `hitl`, after 8.5). On 2026-10-11 the owner accepted running the retro over it. Ticket 8.6 is the post-merge publishing release (R13, Done when 4), so it could not have run before the merge. It has no plan file yet. No tickets are still at `built`.

Delivery: the epic landed on `develop` as merge `39061f44` (CarpeTelam/crosshatch-player#35, branch `claude/stoic-bardeen-0768gs`, head `5cfd4489`). The base was `0bf0b3d0`, which is the epic base `eca7e6c7` plus four `develop` doc commits merged in at `715e67c7`. The diff `0bf0b3d0..5cfd4489` is 269 files, +22,209 / -108.

### Plan ranges

Every plan's `baseline_revision` lies on the epic branch's first-parent spine, in ancestor order (checked with `git merge-base --is-ancestor`). Those baselines are orchestrator commits, though, and the builds ran in two lanes and landed later through merges. So a baseline-to-baseline range holds whatever merged in between, not the ticket's own work. For example, 8.7's baseline range `eca7e6c7..d6aa0d3c` has one docs commit. This retro therefore attributes each ticket by its `Merge entry 8.N …` commit, using the range `M^1..M^2`. The baseline ranges were run too, and their output is kept in the scratchpad (`ev/`).

| ticket | baseline | merge | range | commits | files | +/- |
|--------|----------|-------|-------|---------|-------|-----|
| 8.7 | `eca7e6c7` | `146a657d` | `146a657d^1..^2` | 1 | 8 | +289 / -5 |
| 8.8 | `d6aa0d3c` | `a9e8596f` | `a9e8596f^1..^2` | 2 | 21 | +4,470 / -109 |
| 8.1 | `d3795e88` | `344da6c5` | `344da6c5^1..^2` | 1 | 17 | +974 / -120 |
| 8.2 | `d0d488a2` | `3c61b84b` | `3c61b84b^1..^2` | 1 | 19 | +2,058 / -0 |
| 8.3 | `847e7bc2` | `1f06d3a7` | `1f06d3a7^1..^2` | 2 | 63 | +3,887 / -23 |
| 8.4 | `4137d947` | `068ab322` | `068ab322^1..^2` | 2 | 28 | +1,617 / -104 |
| 8.10 | `71b78fb1` | `bb1efc43` | `bb1efc43^1..^2` | 1 | 12 | +146 / -17 |
| 8.9 | `af31d765` | `cf16fe5c` | `cf16fe5c^1..^2` | 3 (1 merge) | 44 | +778 / -205 (27 PNGs unmeasured) |
| 8.11 | `7699d2e0` | `c1fff07a` | `c1fff07a^1..^2` | 2 | 70 | +2,111 / -896 |
| 8.12 | `ffbb4c7f` | none (on the spine) | `54c2a4ea` | 1 | — | — |
| 8.5 | `daedbeab` | `0f0bb0ca` | `0f0bb0ca^1..^2` | 3 | 10 | +726 / -55 (docs only) |
| e8-xr | — | `14428a56` | `14428a56^1..^2` | 1 | 47 | +2,420 / -336 |
| 8.13 | `9fa5e0c2` | `0c149819` | `0c149819^1..^2` | 2 | 22 | +801 / -60 |
| 8.14 | `1ccef0db` | `4a87ed90` | `4a87ed90^1..^2` | 2 | 31 | +1,362 / -101 |
| 8.15 | `9c9fd3df` | `00699a05` | `00699a05^1..^2` | 1 | 14 | +818 / -10 |
| 8.16 | `4171f69c` | `1ac08331` | `1ac08331^1..^2` | 1 | 18 | +508 / -34 |
| 8.17 | `fced14c2` | `e4940989` | `e4940989^1..^2` | 1 | 9 | +160 / -27 |
| 8.6 | — | — | — | — | — | not started |

Notes on the table:

- The figures come from `git_evidence.py` `files`, which counts non-merge commits only and includes `_bmad-output/`.
- The spine also carries orchestrator docs and chore commits between the merges, such as decisions, packet repacks and dry-run records.

### Evidence inventory

Available:

- the epic file
- the initiative requirements
- 16 plans
- `cross-story-review.md` (over `eca7e6c7..daedbeab`, with the `e8-xr` fix review)
- `device-run-packet.md` and its three device-run records
- `deferred-work.md` sections `## 8.*`, `## e8-xr`, `## e5-run2` and `## e5-run4`
- the previous retrospective (`epic-pass-and-play-retrospective.md`)
- per-merge git evidence
- the merge commit, and CarpeTelam/crosshatch-player#35

Missing:

- Session logs for the build dispatches. The orchestrator's scratchpads (`xr/*.md`, `e8-xr/lenses/`) are not in the repo. So process-lesson analysis is limited to what the plans and Notes record.
- A plan for 8.6.
- A diff for 8.5 (docs only).

## Findings

_Phase 2 pending._

## Behavior verification

_Phase 2 pending._

## Previous-retro follow-through

_Phase 4 pending._

## Action items

_Phase 4 pending._

## Acceptance verdict

_Phase 4 pending._

## Open questions

_Phase 4 pending._

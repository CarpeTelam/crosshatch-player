---
epic: epic-platform-baseline
date: 2026-09-27
verdict: rejected
criteria: declared
headless: false
---

# Retrospective: epic-platform-baseline

> Working draft. Phase 1 (Gather) is complete; Phase 2 (Analyze) is in progress. The `verdict` above is a placeholder, not a judgment; Phase 4 sets it.

## Epic summary

**Epic:** `epic-platform-baseline` (epic 1, "Game code builds everywhere and upstream merges stay clean"), folder `_bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/`. Resolved from the argument `epic 1` by matching `id = 1` in `tickets.py status`, then `find 1.1` → `epic_file`.

**Tickets** (build order from `tickets.py status`; all `status = built`, `state = review`; `pending_tickets` is empty; **all seven are still at `built`**, not `done`):

| Ref | Title | hitl | Covers | Plan baseline | Range (commits) |
|-----|-------|------|--------|---------------|-----------------|
| 1.1 | Games build flag, empty game libraries, and host suites | no | R3, R6 | `f6de4dac` | `f6de4dac..234d1c08` (a5681eb4, 234d1c08) |
| 1.2 | Upstream-touch ledger and its CI job | yes | R1, R2 | `234d1c08` | `234d1c08..de5c18a5` (de5c18a5) |
| 1.3 | x4pro flash budget gate | yes | R5 | `de5c18a5` | `de5c18a5..41acf2fa` (41acf2fa) |
| 1.4 | Vendor Lua 5.5.1 | no | R4, R3, R6 | `41acf2fa` | `41acf2fa..4e1a7c82` (4e1a7c82) |
| 1.6 | Fork update source | no | R7 | `4e1a7c82` | `4e1a7c82..311e4bb4` (311e4bb4) |
| 1.7 | Fork release workflow and first release | yes | R7 | `311e4bb4` | `311e4bb4..a2c1204a` (a2c1204a) |
| 1.5 | Refactor sweep | no | R1–R7 | `a2c1204a` | `a2c1204a..d578b4e3` **inferred** (d2d8e81a, 8e1bc5a4; merge 4154fb29 = PR #9; f3ba9e54, 20994d6a; merge d578b4e3 = PR #10) |

Baselines form one linear chain (each plan's baseline is the previous ticket's commit), so the ranges do not overlap. The last range runs to `HEAD` (`d578b4e3`, the PR #10 merge); nothing later has landed, so no cut was needed. PR #10's commits (f3ba9e54, 20994d6a) have no plan of their own. They are post-merge fixes to 1.3's gate and CI wiring, attributed to the last range by position.

**Delivery:** every ticket shipped in one PR, [CarpeTelam/crosshatch-player#9](https://github.com/CarpeTelam/crosshatch-player/pull/9) (merged 2026-09-26T23:13Z). Two fixes followed in [CarpeTelam/crosshatch-player#10](https://github.com/CarpeTelam/crosshatch-player/pull/10) (merged 2026-09-27T00:01Z).

**Evidence inventory**

| Evidence | Status | Source |
|----------|--------|--------|
| Epic file with Requirements R1–R7 and Done when 1–7 | present | `epic-platform-baseline.md` |
| Initiative requirements (CAP-11) | present | `../initiative-crosshatch-player-v1.md` |
| Ticket entries (description, verify, covers) | present, 7 | `tickets.toml`, `tickets.py find` |
| Story files | none; no ticket was refined (`story_file: null`) | `tickets.py find` |
| Plans | present, 7, all `status: built` | `story-*-plan.md` |
| Per-ticket code review | present for all 7 (six thorough 4-lens runs, a quick run for 1.5; 1.2 looped back once) | each plan's Review Triage Log |
| Deferred work | 3 entries (from 1.1, 1.2, 1.6) | `_bmad-output/implementation-artifacts/deferred-work.md` |
| Commit and diff evidence | present, from `git_evidence.py` per range; 13 commits (2 merges, both measured); 109 files in total, 63 of them vendored `lib/lua/src` | ranges above |
| CI results | present; PR #9 and PR #10 check runs | GitHub Actions |
| Fork release runs | run 1 (dispatch, `Build and check` success, `Tag and publish` skipped, so a dry run); run 2 **in progress** during this retro | Actions runs 36281463154, 36282530704 |
| Releases and tags on the fork | **none** at the time of this retro | GitHub releases and tags API |
| Upstream release workflows | `release.yml`, `release_candidate.yml`, `release-fonts.yml` are `disabled_manually` (since 2026-09-25) | Actions workflows API |
| Session logs | **not available** to this run. Commits name build session `session_01NpVfiShhiDk6wbQVKMqy8y`, but no transcript was read, so process lessons below rest on plans, commits, and CI only | commit trailers |
| Previous retrospective | none: this is the first epic in the `epics` order | `tickets.py status` |

## Findings

_Pending: Phase 2._

## Behavior verification

_Pending: Phase 2._

## Previous-retro follow-through

_Pending: Phase 4._

## Action items

_Pending: Phase 4._

## Acceptance verdict

_Pending: Phase 4._

## Open questions

_Pending._

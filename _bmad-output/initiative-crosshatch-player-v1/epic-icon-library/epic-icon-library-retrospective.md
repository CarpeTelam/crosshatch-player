---
epic: epic-icon-library
date: 2026-09-28
verdict: pending
criteria: declared
headless: false
---

# Retrospective: epic-icon-library

## Epic summary

**Epic:** `epic-icon-library` (epic 3, "Games and runtime screens share one icon library"), folder `_bmad-output/initiative-crosshatch-player-v1/epic-icon-library/`, covering CAP-8 except the launcher rows and first-party drawing.

**Tickets.** Ten, in build order from `tickets.py status`. Every one has `status = done` and `state = done`. `pending_tickets` is empty and none is left at `built`. Entries 9 and 10 were added during the owner's entry-8 review (87dae8ea, ba4cf74b). Commits are attributed by subject and plan, because the ranges below misattribute them.

| Ref | Title | hitl | Covers | Plan baseline | Its commits |
|-----|-------|------|--------|---------------|-------------|
| 3.1 | Tracer: a Phosphor icon from SVG to the game canvas | no | R1, R2, R4, R6, R7 | `1eacdc77` | b562ad3c, 4018b825, bc6adc54 (mark) |
| 3.2 | ch.gfx.image and package images | no | R5, R6, R7 | `bc6adc54` | 890c1a69, 068a9ad0 (mark) |
| 3.3 | Icon regeneration job | no | R2 | `bc6adc54` | 3cd86dfa, 3fed681a (mark) |
| 3.4 | The v1 icon set | no | R1, R3, R7, R10 | `068a9ad0` | 7e54d5d6, 7329a020 (mark) |
| 3.5 | Runtime views draw from the library | no | R8 | `7329a020` | 491486bf, 0b6612ea (mark) |
| 3.6 | Cover-grid Home Games tile | no | R9, R10 | `7329a020` | 39467f07, 201b7c55 (mark) |
| 3.7 | Refactor sweep | no | R10, R11 | `201b7c55` | 8e233695, 720fe299 (mark) |
| 3.8 | Owner review of the icon set and screens | **yes** | R1–R3, R7–R11 | none (the plan has no `baseline_revision`) | d252cbef, 38000ed2 (packet), 87dae8ea (answers; adds 3.9), ba4cf74b (adds 3.10), e39c29ab, f7be3b4a, dc167d9c, c82ab12c (sign-off), 0ef5a900 |
| 3.9 | Phosphor names and both weights | no | R1–R4, R7–R10 | `87dae8ea` | 9197d046, ba2ae079 (mark), 2ba81907 |
| 3.10 | Deferred cleanup before epic-install-and-launcher | no | R11 | `ba4cf74b` | 0520b146, 0409fafd (mark) |
| — | Cross-story review (not a ticket) | no | — | `ba2ae079` (reviewed `1eacdc77..ba2ae079`) | f27dcefd |

**Ranges.**
- **Baseline order**, oldest first by ancestry: `1eacdc77` (3.1), `bc6adc54` (3.2, 3.3), `068a9ad0` (3.4), `7329a020` (3.5, 3.6), `201b7c55` (3.7), `87dae8ea` (3.9), `ba4cf74b` (3.10). `git_evidence.py` ran once per distinct range and once for the whole epic (scratchpad `ev/*.json`).
- **Why the rule's ranges misattribute.** Lanes ran in worktrees and were merged into the epic branch. 3.10's baseline (`ba4cf74b`) is newer than 3.9's (`87dae8ea`), but 3.10's commit (0520b146) landed before 3.9's (9197d046), so 3.9's range holds only 3.8's docs commits and 3.10's range holds 3.10, 3.9, and the cross-story fix. The table attributes by subject and plan.
- **The epic-wide range** is `1eacdc77..d719a379`: 36 commits, 6 merges (1 measured on the first-parent spine: PR #17), 424 files, +15,693 / −3,206 in non-merge commits. 7,246 / 2,097 of that is the generated header `lib/GameIcons/GameIcons.generated.h`.

**Delivery.** One PR, [CarpeTelam/crosshatch-player#17](https://github.com/CarpeTelam/crosshatch-player/pull/17), merged 2026-09-28T15:32:49Z by the owner. Its 19 check runs were all `success` on the head `e6bed5f7`, including `Test Status`, `Crosshatch Test Status`, the five env builds, `unit-tests`, `clang-format`, `cppcheck`, `Upstream touch ledger`, `x4pro flash budget`, `Layer check`, `Icons up to date`, `API freeze`, `Fork script tests`, `Simulator build`, and `Title Check`. `Crosshatch Test Status` finished at 15:31:41Z, so the PR did not merge red.

**Evidence inventory**

| Evidence | Status | Source |
|----------|--------|--------|
| Epic file with R1–R11 (amended 2026-09-28), Done when 1–5, dated owner decisions, 27 `Assumption for entry 8:` lines and their answers, two `Measurement` lines | present | `epic-icon-library.md` |
| Initiative requirements (CAP-8) | present | `../initiative-crosshatch-player-v1.md` |
| Ticket entries | present, 10 | `tickets.toml`, `tickets.py status` |
| Story files | none; no ticket was refined | `tickets.py status` (`refined: false`) |
| Plans | present, 10, all `status: done`; 3.8's is a short record with no baseline and no review | `story-*-plan.md` |
| Cross-story review | present: 8 findings, fixed in f27dcefd | `cross-story-review.md` |
| Review packet and measured deltas | present | `review-packet/README.md`, `review-packet/{icons,views,home}/` |
| Simulator screenshots | present, 6 story folders plus the packet | `story-*-screenshots/` |
| Deferred work | `## 3.1`–`## 3.10` sections, and entries marked resolved | `_bmad-output/implementation-artifacts/deferred-work.md` |
| Handoffs to later epics | present | `../epic-install-and-launcher/epic-install-and-launcher.md:50-57`; `../epic-game-api-docs/epic-game-api-docs.md` Notes |
| CI | PR #17: 19/19 green on the head; earlier runs include one `cppcheck` failure on `f7be3b4a` (attempt 1), passed on re-run | GitHub Actions run 36423204077 |
| Commit and diff evidence | present | scratchpad `ev/` |
| Device runs | none, by the owner's decision ("no closing device run", epic Notes) | epic Notes |
| Orchestration record | **not in the repo.** 30 of 36 commits name the orchestrating session `session_01DhiSSwkAq51abArzAsjThg`; its transcript and the build agents' transcripts were not available to this retro. Process findings rest on plans, commits, CI, and the epic Notes; a claim resting only on the owner's prompt is marked "(owner's observation, no repo evidence)" | commit trailers |
| Previous retrospective | present | `../epic-script-runtime/epic-script-runtime-retrospective.md` |

**Going-in concerns.** The owner named six orchestration observations to test against the evidence (the invocation of this retro): build agents handing back early, the cross-story race, measurement against a recorded figure, the late entry-8 scope change, environment friction, and the flash headroom left for epic-install-and-launcher. Each is answered under Process and orchestration, as a finding where it has a source.

## Findings

_Pending (Phase 2)._

## Behavior verification

_Pending._

## Previous-retro follow-through

_Pending._

## Action items

_Pending (Phase 4)._

## Acceptance verdict

_Pending (Phase 4)._

## Open questions

_Pending._

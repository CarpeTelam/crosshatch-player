---
title: 'e6pre-14-flash-and-static-ram-base: measure the x4pro flash and static-RAM base before epic-play-nearby'
type: 'chore'
ticket: ''
created: '2026-10-10'
status: 'done'
route: 'oneshot'
route_source: 'pinned'
baseline_revision: 'ccb748bb5f4892ed1b27cb49077dab0709a905c4'
review: 'quick'
review_source: 'auto'
lenses_ran: [quick]
review_loop_iteration: 0
context:
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Epic-play-nearby's 9,504 B flash and 160 B static-RAM share are deltas over a base measured before its first story, and the only recorded base (`9b0262ad`, 2026-10-04) predates epic-first-party-games' merge, whose Notes said it changed no firmware although entries 10, 13 and 14 changed `src/` and `lib/`.

**Approach:** Run the gate's own method (`scripts/check_flash_budget.py` `build on`, `build off`, `compare`, under the build lock, both builds in one tree) at `0bf0b3d0` and `39061f44`, and write the figures as dated `Measurement` lines in the Notes of `epic-play-nearby.md` and `epic-first-party-games.md`, as the epic-play-nearby Note "Decision, `e6pre-14`" states. The third measurement (the cleanup branch's head after e6pre-15) is sent by the orchestrator later and lands as a second commit. The spine's allocations do not change; the plan states the figures and any shortfall. No source file changes.

</frozen-after-approval>

## Implementation Notes

Oneshot: the work is a measurement and two Notes lines, so the risk is a wrong or mislabelled figure, not design. The intent is settled by the dated Decision in `epic-play-nearby.md` (Notes, "Pre-epic cleanup, second round", owner, 2026-10-10); no intent gap. The build agent brief (`docs/crosshatch/orchestrated-epics.md`) answers the human gates under `bmad-build`: Keep all goals, Keep full plan, Checkpoint 1 approve.

Measured (2026-10-10, x4pro, the gate's own method; the full `compare` outputs are in `/tmp/claude-0/-home-user-crosshatch-player/820241fa-642e-5ce6-ab5e-d09ffe0842f3/scratchpad/e6pre-14/0bf0b3d0-compare.txt` and `39061f44-compare.txt`, the build logs beside them). Each commit was checked out detached in this worktree, `git submodule update --init --recursive` run, `git rev-parse HEAD` read back (`0bf0b3d0ae9c0836dfd9a14de4754a472d6c62bd`, `39061f4416dc01a8e1df393752252f2ed76144a3`) with only this untracked plan in `git status`, and then `build on`, `build off`, `compare` ran in one shell under `flock /tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, so both builds of a commit share a tree. The tree was incremental (`.pio` was reused across the two commits, as in the 2026-10-04 Measurement); `compare` exited 0 at both.

| Commit | `firmware.bin` games on | games off | Flash diff (gate 276,480 B) | Static RAM on / off | RAM diff (gate 1,024 B) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `0bf0b3d0` | 5,935,632 B | 5,680,016 B | +255,616 B (20,864 B under) | 187,848 B / 187,064 B | +784 B (240 B under) |
| `39061f44` | 5,937,008 B | 5,680,016 B | +256,992 B (19,488 B under) | 187,848 B / 187,064 B | +784 B (240 B under) |

- epic-first-party-games's own delta, `39061f44` minus `0bf0b3d0`: +1,376 B flash (games-on only; games off is unchanged), +0 B static RAM (games-on `.iram0.text` +76 B, `.iram0.text_end` -76 B). Each term is a measurement at its own commit.
- Against the spine's Flash budget row as written (epic-play-nearby 9,504 B / 160 B, the reserve 2,000 B / 48 B, unallocated 11,536 B / 0 B; 23,040 B / 208 B in all): flash headroom is 2,176 B short at `0bf0b3d0` and 3,552 B short at `39061f44`; static RAM has 32 B to spare. The row predates two owner decisions in `epic-pass-and-play.md` (its share raised from 20,000 B to 21,000 B on 2026-10-03; its 40 B overage covered from the unallocated headroom on 2026-10-04), which make the unallocated 10,496 B and the allocations 22,000 B, the 22,000 B under the gate that epic recorded at `55a8d9bf` (recorded, not re-measured; the commit no longer exists). On that footing the shortfall is 1,136 B at `0bf0b3d0` and 2,512 B at `39061f44`; the 1,136 B is growth between that record and `0bf0b3d0`, unattributed (unmeasured). Either way this epic's share plus the reserve (11,504 B / 208 B) fit under the headroom, leaving 7,984 B / 32 B after them. The allocations are not changed here; who pays is the owner's call.
- `0bf0b3d0`'s flash figures equal the 2026-10-04 Measurement at `9b0262ad` and the base Measurement at `eca7e6c7` (whose `src/`, `lib/` and `platformio.ini` match); this shows the runs reproduce, not that they equal CI's numbers.
- What this is not: `objects` was not run (the 2026-10-04 line's "49 game objects" is not repeated), and the trees were incremental with the shared cache, not a fresh clone. The Decision asks for the figures of these builds only.
- Head measurement (follow-up, 2026-10-11, taken after the orchestrator merged e6pre-15 to e6pre-19 and the upstream sync #36 into the cleanup branch; `git merge --ff-only` took this branch to `c582ce7e`; `git rev-parse HEAD` was recorded beside each run in `/tmp/claude-0/-home-user-crosshatch-player/820241fa-642e-5ce6-ab5e-d09ffe0842f3/scratchpad/e6pre-14/<commit>.head`, with a clean tree; `pio pkg install -e x4pro` ran first because `platformio.ini` changed upstream (not logged); the builds were not equally incremental (`f0ac4376`: 394 compile lines per build, near a full rebuild; `c582ce7e` games on: 4 compiled, the rest from the shared cache); same method and caveats as above; `compare` exit 0 at both):

| Commit | `firmware.bin` games on | games off | Flash diff | Static RAM on / off | RAM diff |
| --- | ---: | ---: | ---: | ---: | ---: |
| `f0ac4376` (develop after the sync) | 6,214,000 B | 5,955,232 B | +258,768 B (17,712 B under) | 190,248 B / 189,464 B | +784 B (240 B under) |
| `c582ce7e` (cleanup head) | 6,214,032 B | 5,955,328 B | +258,704 B (17,776 B under) | 190,248 B / 189,464 B | +784 B (240 B under) |

  - The sync (`f0ac4376` minus `39061f44`): +1,776 B on the flash difference (games on +276,992 B, games off +275,216 B), +0 B on the static RAM difference (both totals +2,400 B, upstream's own growth). The cleanup (`c582ce7e` minus `f0ac4376`): -64 B (games on +32 B, games off +96 B), +0 B. These cleanup figures cover e6pre-15 to e6pre-19 together, not e6pre-15 alone. Head minus `39061f44` (the Decision's "head minus"): +1,712 B flash difference, +0 B static RAM; the sync's cause is unattributed (unmeasured).
  - Headroom at `c582ce7e`: 17,776 B flash, 240 B static RAM; after this epic's share and the reserve (11,504 B / 208 B), 6,272 B / 32 B. Short of the spine row's 23,040 B by 5,264 B, of the 22,000 B footing by 4,224 B. Allocations unchanged. `c582ce7e` is this epic's base unless more firmware lands before inception.

## Review Triage Log

Lens: quick, run as a context-free subagent in the foreground; it returned. Every figure and delta was found correct against the two `compare` outputs.
- Shortfall bookkeeping used the spine row alone and omitted epic-pass-and-play's raise to 21,000 B (2026-10-03): medium, real; both Notes and this plan now give the row as written (as the Decision asks) and the figures after the two pass-and-play decisions, and say the 1,136 B before epic-first-party-games is unattributed.
- Source of the 1,376 B named entries 10, 13 and 14 only: low, real (entry 11's `3fe30c4f` is in the range); the epic-first-party-games line now lists the six commits.
- `0bf0b3d0` called the epic's base while its Measurement names `eca7e6c7`: low, real; relabelled, noting the two have the same `src/`, `lib/` and `platformio.ini`.
- "The gate's own method" overstated (no `objects`, incremental): low, real; both Notes lines and this plan say so.
- "Byte for byte ... the method matches" overstated: low, real; reworded to reproducibility.
- "Unspent allocations" wording: low, real; reworded to "the allocations".
- No `git rev-parse` in the raw outputs: low, real; the plan now records it was read after each checkout (it was; the outputs are in this run's shell history, not in the scratch files).
- Line 106's "every delta ... should be zero" and R2 left uncorrected: rejected; entry 10's Measurement already overrode it and the Decision forbids more than Measurement lines.

Follow-up round (2026-10-11): lens quick, a second context-free subagent, foreground; it returned. Every figure and delta correct against the four `compare` outputs.
- "Upstream growth the gate does not count" gave an unmeasured cause: low, real; Notes now say unattributed and unmeasured.
- Plan's sync bullet read "+0 B static RAM" without the +2,400 B totals: low, real; added.
- "Same method" hid that `f0ac4376` was near a full rebuild and `c582ce7e` mostly cache: low, real; one-clause caveat in both texts.
- `pio pkg install` not in the raw files: low, real; marked "not logged".
- The Decision's "head minus `39061f44`" not stated (+1,712 B): low, real; added to both.
- Earlier line saying the base is the head after `e6pre-15`: low, real; it now points to the 2026-10-11 line.

## Verification

- `git diff --stat ccb748bb c745e9e2` (first commit) and `git diff --stat c745e9e2` (follow-up) -- expected: the first commit changes the two epic files' Notes (appended lines only) and this plan, the follow-up only epic-play-nearby's Notes and this plan; no source file.
- Each figure in the two Notes lines read against the `compare` outputs in the scratch folder (`0bf0b3d0`, `39061f44`, and for the follow-up `f0ac4376`, `c582ce7e`); the review lenses did this.
- Deltas recomputed: 256,992 - 255,616 = 1,376; 276,480 - 255,616 = 20,864; 276,480 - 256,992 = 19,488; 23,040 - 20,864 = 2,176; 23,040 - 19,488 = 3,552; 19,488 - 11,504 = 7,984; 11,536 - 1,000 - 40 = 10,496; 9,504 + 2,000 + 10,496 = 22,000; 22,000 - 20,864 = 1,136; 22,000 - 19,488 = 2,512. Follow-up: 258,768 - 256,992 = 1,776; 6,214,000 - 5,937,008 = 276,992; 5,955,232 - 5,680,016 = 275,216; 258,704 - 258,768 = -64; 276,480 - 258,704 = 17,776; 17,776 - 11,504 = 6,272; 23,040 - 17,776 = 5,264; 22,000 - 17,776 = 4,224; 1,376 + 1,776 - 64 = 3,088 = 258,704 - 255,616.
- `./bin/clang-format-fix` twice, then `git status` -- expected: nothing new. `python3 scripts/check_upstream_touches.py` -- expected: pass (no non-fork file touched).

---
title: 'Merge keeps an aligned boot''s times when the device has earlier boots'
type: 'bugfix'
ticket: '23'
created: '2026-10-13'
status: done
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/docs/crosshatch/fork-scripts.md'
warnings: [oversized]
deferred:
  - summary: >-
      A clock note on the first boot of a group of aligned boots that no STATE links to the reference says "not aligned by STATEs; placed after boot N" (or "no STATE pair with another device") although the boot has STATE pairs.
    evidence: |-
      Text unchanged from a1ffc5b5. In a cardless run the first device's match boot is such a root (smoke run: "A boot 4: clock: not aligned by STATEs; placed after boot 3"), so the note misleads on every run whose first device has earlier boots. Renaming it changes no placement; test_a_devices_segments_join_in_order_and_a_second_boot_is_its_own_clock counts the "placed after boot 1 of the same device" substring.
    location: >-
      scripts/device_log.py:align_clocks (first pass, the floating-root notes)
    severity: low
  - summary: >-
      A floating aligned root is placed after its device's earlier boots (or at 0) without looking at that device's later boots already placed by another component, so it can overlap a later aligned boot unnoted and the clamp then holds the later boot's lines.
    evidence: |-
      Pre-existing placement; reachable with three devices, or two when a device's first boot aligns only with a boot the reference does not reach (edge-case lens probe: A1, B1-C1, A1-B2 freezes B2's lines). Not reachable in the two-device smoke-run shape.
    location: >-
      scripts/device_log.py:align_clocks (first pass)
    severity: low
baseline_revision: 'b221dab0c8742d8a998eddca3e5a5700bd6b1fac'
---

<intent-contract>

## Intent

**Problem:** In entry 11's smoke run, `device_log.py merge` aligned device B's match boot (boot 4, offset -1229 ms from A boot 4, 22 STATE pairs) yet stamped its ~120 s of lines within 16 ms: `align_clocks` chains B's unaligned boots 1 to 3 forward from the first device's start, they end (~265 s) after boot 4's aligned start (~64 s), and `place`'s per-device monotone clamp holds every boot-4 line at boot 3's end. Every cardless run has earlier boots (closing the Wi-Fi file transfer restarts the device).

**Approach:** An aligned boot keeps its offset. A device's unaligned boots are placed to end no later than its next aligned boot starts, chained backwards from it; with no aligned boot after them, after the previous boot as now. The `[device_log] clock:` notes say which way each boot was placed, and say when one overlaps an earlier boot; any overlap left is resolved in favour of the aligned boots' times. `fork-scripts.md`'s Alignment paragraph says how.

## Boundaries & Constraints

**Always:** Python standard library only; `fork-scripts.md` conventions. Keep every guard of `align_clocks` (Design Notes). Only `merged.log`'s stamps and order and the clock notes change; the `summary` figures do not. Every existing merge and capture test passes unchanged, or the plan states why. The test lays out its card files with `card_text`, `link_line`, and `exchange`, as the existing merge cases do.

**Never:** Commit or quote the smoke run's own card files (they hold the owner's network name and IP addresses). Touch firmware, `lib/`, `src/`, any upstream file, or the capture mode. Change how an aligned boot's offset is estimated, the receive-after-send correction, or the reference boot. Rename the floating-root note ("not aligned by STATEs; placed after boot N of the same device") that existing tests count.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Smoke-run shape | A: boot 1 unaligned, boot 2 aligned with B boot 4; B: boots 1-3 unaligned (summed length longer than A boot 1), boot 4 aligned | B boot 4 keeps its offset and its lines' spacing; B boot 3 ends when boot 4 starts, boot 2 when 3 starts, boot 1 when 2 starts; their notes say so; every send before its receive | n/a |
| Unaligned boot after the last aligned one | B: boot 1 aligned, boot 2 unaligned | boot 2 placed after boot 1, today's note | n/a |
| Device with no aligned boot | B: one or more unaligned boots only | first starts with the first device, the rest after the previous, today's notes | n/a |
| Gap shorter than the boot | B: boot 1 aligned, boot 2 unaligned and longer than the gap, boot 3 aligned | boots 1 and 3 keep their times; boot 2 ends when boot 3 starts; its note says it overlaps boot 1 by N ms; its lines before boot 1's end are held at boot 1's last stamp | n/a |
| Empty boot file | an unaligned boot with no log lines | length 0, chained like any other | n/a |

</intent-contract>

## Code Map

- `scripts/device_log.py:align_clocks` (~line 891) -- the fix. Today: walk from the reference, then for each unplaced node in node order: reference at 0, else after its device's previous placed boot (`offsets[before] + max millis of before`), else 0; then `walk(node)` aligns its component. A node is "aligned" when it has a STATE pair with another device (`neighbours[node]` non-empty).
- `scripts/device_log.py:place` (~964) and `merge_cards` (~977) -- the per-device monotone clamp and the receive-after-send loop; `floor` already handles negative offsets. Read-only: the clamp already favours the earlier boot.
- `scripts/device_log_test.py` -- `card_text` (~943), `link_line`, `exchange`, `MergeTest` (`card`, `merge`, `merged_lines`, `section`), and `test_the_summary_figures_equal_a_serial_capture_of_the_same_run`'s stamp masking to reuse.
- `docs/crosshatch/fork-scripts.md` (~161-170) -- "Alignment" bullet, sentence "A boot with no shared `STATE` is placed after ...".

## Tasks & Acceptance

**Execution:**
- [x] `scripts/device_log.py` -- `align_clocks`: place aligned components first (reference, then each unplaced aligned node as a root placed after its device's earlier boots, chaining forward through any unplaced unaligned ones), then each device's unaligned boots: a run of them followed by an aligned boot chains backwards from it, a trailing run forward as today; notes for the backward placement and any overlap; docstring updated -- the fix.
- [x] `scripts/device_log_test.py` -- new `MergeTest` cases: the smoke-run shape (red against today's `merge`: B's last boot one burst), the overlap shape, and the summary figures of the smoke-run case (stamps masked) equal to a serial `summary` of the same lines, plus a case per remaining matrix row (an unaligned boot after the last aligned one; an empty unaligned boot file chained backwards; the no-aligned-boot row may rely on `test_devices_that_share_no_state_are_merged_unaligned_and_the_note_says_so`) -- the verify's case and the matrix.
- [x] `docs/crosshatch/fork-scripts.md` -- Alignment bullet says how unaligned boots are placed and what an overlap does.

**Acceptance Criteria:**
- Given the smoke-run-shaped cards, when `merge` runs on today's code, then the new case fails (B's aligned boot's stamps span under 100 ms); after the fix, B's aligned boot's offset is the STATE estimate, its lines keep their millis spacing (within the 1 ms receive moves), B's earlier boots' last stamps are at or before its first, every send precedes its receive, and B's boots 1-3 notes say they were placed to end when the next boot starts.
- Given the overlap shape, when `merge` runs, then both aligned boots keep their spacing, the unaligned boot's note names the boot it overlaps and by how many ms, and no line of the unaligned boot is stamped after the next aligned boot's first line.
- Given every existing case, when `python3 scripts/device_log_test.py` runs, then each passes unchanged.

## Implementation Notes

- `align_clocks` runs three passes: aligned components (reference, then each unplaced aligned node as a root; a root
  with earlier boots starts at the end of the last placed one plus the lengths of the unplaced ones after it, which is
  today's position), then unaligned boots chained backwards from the next placed boot in reverse node order, then the
  trailing unaligned boots forward with today's code and notes. The overlap is measured against the device's last boot
  placed in the first pass before the run (an aligned boot, or the reference), so every boot of a run longer than the
  gap that starts before that boot's end says so.
- The smoke-run summary case drops blank lines when it compares sections: B's earlier boots now start before A's, so
  `summary` lists B first from `merged.log` and A first from the raw logs; the figures are equal.
- Red check: with `scripts/device_log.py` at HEAD, the smoke-run case fails (B boot 4's 17 lines stamped within 2 ms),
  and so do the overlap and empty-boot cases (their notes); with the fix all 70 pass.
- After review: two cases pin the floating root's forward chain through unaligned boots and the overlap note of a
  two-boot run (each boot names the last aligned boot before the run with its own ms); the note formats the overlap
  with `:.10g`, so it never goes to e-notation; the Alignment bullet says how a group not linked to the reference is
  placed. 72 tests pass; the `else start` and `kept[0]` mutations each fail one case.
- Smoke-run cards (scratch only): B boot 4 now spans 166.1 s of stamps for millis 2825..168925; B boots 1-3 notes say
  they end when the next boot starts; all 22 STATE keys send before receive.

## Plan Change Log

## Review Triage Log

### 2026-10-13 — Review pass
- Four lenses ran as context-free foreground subagents (blind hunter, edge-case hunter, verification gap, intent alignment) over the diff since `baseline_revision` (`docs/` and `scripts/`, 18,423 B; the plan itself not in it). Intent alignment is descriptive; its four divergences are logged as rows. No intent_gap, no bad_plan, no loopback. Patches went to the re-engaged implementation subagent; the Verification was then re-run (all pass).
- verdicts: 23 findings — high 0, medium 4, low 16, false 3, maybe-false 0
- findings:
  - `[low]` `[reject]` blind: a boot's length ignores its files' `opened_ms`, so an empty backward-chained boot whose file opened after millis 0 puts its card-file mark after the next boot starts — real, but the writer lays a file's header down with its first batch, so a boot file with no lines does not occur on a device, and a fix needs the file marks passed into `align_clocks` (new plumbing).
  - `[low]` `[defer]` blind: the root of a component the reference does not reach keeps the note "not aligned by STATEs; placed after boot N" (or "no STATE pair" with no earlier boots) although it has STATE pairs — the text is unchanged from `a1ffc5b5` (pre-existing); deferred (D1 in `deferred`).
  - `[medium]` `[patch]` blind: the floating root's new forward chain through unplaced unaligned boots has no test — same root cause as the verification-gap row below; added `test_a_root_not_reached_from_the_reference_starts_after_its_devices_unaligned_boots`.
  - `[low]` `[patch]` blind: the docs say every boot that shares a `STATE` keeps the offset the `STATE`s give it, which misdescribes a group not linked to the reference — added the clause on that group's placement.
  - `[medium]` `[patch]` blind: the overlap note is pinned only for a one-boot run — same root cause as the verification-gap row; added `test_each_boot_of_an_unaligned_run_that_reaches_back_past_an_aligned_boot_says_by_how_much`.
  - `[false]` `[reject]` blind: crossing pairs (A1 with B2, A2 with B1) would place a device's boots out of order — two devices' restarts cannot interleave that way (B1 before B2 and A1 before A2 make a send in A1 received in B2 and one in A2 received in B1 contradict each other); only mislabelled cards reach it, and placement of aligned boots is the walk's, unchanged.
  - `[low]` `[patch]` blind: the edited docs bullet ended in a short line — reflowed to the file's 120-column wrap.
  - `[low]` `[reject]` blind: the test helper `stamps()` keys lines by text and calls `group()` unchecked — every `boot_body` line carries its boot label and millis, so no two lines collide, and a non-matching line already fails the test (an exception is a failure); a cosmetic test-helper change.
  - `[low]` `[reject]` blind: `boots()` rescans `nodes` (quadratic) and `neighbours[node]` inserts into a defaultdict — a run has a handful of boots, and `walk` already reads `neighbours[node]` the same way; no outcome changes.
  - `[low]` `[defer]` edge: a floating root with no earlier boots goes to 0 even when its device's later boot is already placed, overlapping it unnoted — pre-existing placement of floating roots (needs three devices or a reference that is not its device's first match boot); deferred (D2).
  - `[low]` `[defer]` edge: a floating root placed after its earlier boots can run past a later aligned boot of its device already placed by another walk — same as the row above, pre-existing; deferred (D2).
  - `[low]` `[defer]` edge: the "no STATE pair" note on a floating root with no earlier boots — pre-existing text; deferred (D1 in `deferred`).
  - `[low]` `[defer]` edge: the "not aligned by STATEs" note on a floating root — pre-existing text; deferred (D1 in `deferred`).
  - `[low]` `[reject]` edge: an empty boot with `opened_ms > 0` stamps its card-file mark after the next boot starts — as the first row.
  - `[low]` `[patch]` edge: the overlap formatted with `:g` prints e-notation from 1,000,000 ms — now `:.10g` ("5000" unchanged, 86400000 printed in full).
  - `[low]` `[defer]` edge (claim): "any overlap resolved in favour of the aligned boots" does not cover an aligned floating root overlapping a later aligned boot (probe with three devices) — pre-existing, the clamp favours the earlier boot as before; deferred (D2).
  - `[low]` `[defer]` edge (claim): a floating root with no earlier boots gets offset 0 and the "no STATE pair" note whatever its device's later aligned boots say — pre-existing; deferred (D1, D2).
  - `[medium]` `[patch]` verification gap: a floating root after unplaced unaligned boots is placed by a branch no test runs (mutant `else start` green) — new case above; the mutant now fails it.
  - `[medium]` `[patch]` verification gap: the overlap note is pinned only for a one-boot run after a single aligned boot (mutants `kept[0]` and first-boot-only green) — new case above (boot 3 overlaps boot 2 by 13000 ms, boot 4 by 3000 ms); the `kept[0]` mutant now fails it.
  - `[low]` `[defer]` intent alignment: on the smoke run's own shape the first device's match boot (a floating root) reads "not aligned by STATEs" — pre-existing note text; the placement path it describes is now tested (row above); deferred (D1 in `deferred`).
  - `[low]` `[reject]` intent alignment: `summary.txt` can list devices in a new order (B first when its earlier boots now start first) — the section order follows each device's first stamp, which is `merged.log`'s order the intent lets change; every figure is equal (the summary case compares them per section).
  - `[false]` `[reject]` intent alignment: the verify says the earlier boots "end before" the aligned boot starts while the code ends them exactly at its start — the description fixes "end no later than its next aligned boot starts"; the boot starts at its millis 0, and a device's first line comes after that (the tests' bodies log at millis 0, hence equal stamps); file order keeps boot 3's last line before boot 4's first.
  - `[false]` `[reject]` intent alignment: aligned-against-aligned overlap within one device is not resolved in favour of either — between two aligned boots there is no "aligned boot's times" to prefer; the entry's overlap rule is about an unaligned boot, and the floating-root case is deferred as D2.

## Design Notes

- **Settled by the sources.** Backward chaining, the forward fallback, overlap kept in favour of aligned times, notes saying which way: the entry's description. The between-two-aligned case: the entry's `unknown` answers it ("keep the aligned times, and say in its note that it overlaps"). "Aligned" means sharing a STATE pair with another device's boot, even when that component is not anchored to the reference: the entry calls B boot 4 aligned while its partner A boot 4 is itself placed after A boot 3. Boot start is its millis 0, end its last line, as entry 19's forward chain uses (Design Notes, "Merge alignment"): backwards, `offset_k = offset_next - end_k`.
- **Floating aligned components.** A component not reached from the reference keeps today's placement: its first node (in node order) goes after its device's earlier boots. Earlier unaligned boots of that device are now placed later, so the root's position chains forward through their lengths from the last placed boot (or 0); the backward chain then puts them exactly between, the same place as today.
- **Guards kept** (`git log -L`: one commit, `a1ffc5b5`): `if node in offsets: continue` and `walk`'s same check (each node placed once); `node == nodes[0]` (the reference is 0); `node[0] != send.node[0]` (no pair within one device); ambiguous sends and `NO_SESSION` anchor nothing (`state_sends`, `state_anchor`, unchanged).
- **Note texts.** Backward: `clock: not aligned by STATEs; placed to end when boot N of the same device starts`; overlap appends `; it overlaps boot P by X ms, whose times are kept`. Forward texts unchanged.
- **Overlap.** `place`'s clamp holds the unaligned boot's earliest lines at the earlier boot's last stamp; its later lines end by the next aligned boot's start, so neither aligned boot moves.

## Verification

**Commands:**
- `python3 scripts/device_log_test.py` -- expected: all pass, the new cases included; the smoke-run case red with the `align_clocks` change stashed.
- `python3 scripts/device_log.py merge --out /tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/6.23/out A=/tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/6.23/ev/cardA4 B=/tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/6.23/ev/cardB` on the copied smoke-run cards (scratch only, never committed) -- expected: B boot 4's lines span its own ~169 s, B boots 1-3 notes say they end before the next.
- `for t in scripts/*_test.py; do python3 "$t" || echo FAIL $t; done` -- expected: no FAIL.
- `python3 scripts/check_upstream_touches.py` -- expected: exit 0.
- `./bin/clang-format-fix` twice -- expected: no change to tracked files.
- No firmware build or `pio check`: no C/C++ or firmware file changes.

## Auto Run Result

**Summary:** `device_log.py merge` no longer stamps an aligned boot as one burst when its device has earlier boots. `align_clocks` places the aligned boots first (each keeps its STATE offset; a group not linked to the reference starts after its device's earlier boots, as before), then chains a device's unaligned boots backwards from its next aligned boot so each ends when the next starts, and places the ones after the last aligned boot forward as before. Their `[device_log] clock:` notes say which way, and a boot chained back past the previous aligned boot's end says by how many ms it overlaps it; the aligned boot keeps its times. On the smoke run's cards (scratch copies, not committed) B boot 4 now spans 166.1 s of stamps for its 166.1 s of millis, instead of under 16 ms.

**Files:**
- `scripts/device_log.py` -- `align_clocks`: the three-pass placement, the backward and overlap notes, the docstring.
- `scripts/device_log_test.py` -- seven `MergeTest` cases (smoke-run shape, its summary figures against a serial capture, overlap, overlap of a two-boot run, a floating root after unaligned boots, an unaligned boot after the last aligned one, an empty unaligned boot) and their helpers.
- `docs/crosshatch/fork-scripts.md` -- the Alignment bullet says how each boot is placed and what an overlap does.

**Review:** 23 findings (0 high, 4 medium, 16 low, 3 false). Patched: 2 medium entries (two test gaps, 4 rows) and 3 low (the docs clause and wrap, the overlap number's format). Deferred: 2 low entries (8 rows: D1, the floating root's note text; D2, a floating root overlapping a later aligned boot), both pre-existing. Rejected: 3 false (crossing pairs, "end before", aligned-against-aligned overlap) and 5 low (an empty boot's file mark, the `stamps()` helper, `boots()` cost, the summary's section order, and the empty-boot mark again from the edge lens), each with its reason in the Review Triage Log. Follow-up review: `false`. Two medium entries were patched, but both were missing tests whose mutants now fail; no unverified risk remains to name.

**Verification:**
- `python3 scripts/device_log_test.py`: 72 tests pass. Every existing case passes unchanged.
- Red check: with `scripts/device_log.py` at the baseline, the smoke-run case fails (B boot 4's 17 lines stamped within 2 ms), and the overlap and empty-boot cases fail on their notes.
- Mutants `else start` and `kept[0]` each fail one new case.
- `python3 scripts/device_log.py merge` on scratch copies of the smoke-run cards: B boots 1-3 notes say "placed to end when boot N+1 ... starts"; B boot 4 stays at -1229 ms from A boot 4 (22 pairs) and spans 166.1 s; every STATE is sent before it is received.
- Every `scripts/*_test.py`: no failure.
- `python3 scripts/check_upstream_touches.py`: PASS.
- `./bin/clang-format-fix`, run twice: no change to any tracked file, and no formatting change outside this entry's paths.
- No firmware build or `pio check`: no C/C++ or firmware file changed.

**Residual risks:**
- `summary.txt` may list the devices in a different order (by first stamp). The figures are unchanged.
- The first device's match boot still carries the old "not aligned by STATEs" note (D1 in `deferred`).
- An overlap between two aligned boots of one device is unhandled, as before (D2 in `deferred`).
- No timing, memory, or flash figure is claimed.


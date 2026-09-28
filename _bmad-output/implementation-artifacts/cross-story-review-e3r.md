# Cross-story review: the epic-icon-library retrospective's follow-up (e3r-1 to e3r-3)

The follow-up's cross-story review (`docs/crosshatch/orchestrated-epics.md`, "Before the epic PR") looked at the three
lanes' combined change at once, to catch what each lane's own review could not.

## How it ran

- **The diff:** one context-free reviewer over `git diff 8bd18e86..11402d1e`, excluding `_bmad-output`, PNGs, the
  generated icon header, and `SHA256SUMS`, weighted on the boundaries between the lanes.
- **The lenses:** it ran bmad-review's adversarial, edge-case, and verification-gap lenses as subagents.
  - They were started with `run_in_background: false` but still ran in the background.
  - The reviewer handed back once before any lens returned and was resumed.
  - All three then returned; none was re-run.
- **Checking:** the reviewer checked each finding against the source at `11402d1e`.
- **The fix:** the orchestrator triaged the findings and sent the accepted ones to the `e3r-x` fix build. That fix
  commit gets its own review before the push.

## Triage

| # | Severity | Lane | Finding | Outcome |
| --- | --- | --- | --- | --- |
| 1 | low–medium | e3r-2 × older Resume render | After Play again, Back then Resume in the setup gap renders the last round's board (`GameMatchActivity::handle` → `renderCanvas`), and since e3r-2 every tap on it is dropped silently | fix (e3r-x); closes `## 3.7`'s R3 residual |
| 2 | low | e3r-2 | Splitting `GameAssets::load` raised the deepest stack chain from about 480 B to 576 B plus SD. That is about 2 % of the 24 KB loop stack, but the split's purpose was headroom. The comment at `GameAssets.cpp:86-88` is stale | fix (e3r-x): one scratch struct for the load, measured with `-fstack-usage` |
| 3 | low | e3r-2 × fixtures tests | `fixtures/slow-restart` is the only fixture game no host test loads | fix (e3r-x): parse every fixture manifest |
| 4 | low | e3r-1 | `DisplayList.h:13-16` and `api-level-1.txt:339-340` say the budget "bounds a frame's replay work"; filled rects and circles are not charged | fix (e3r-x): reword |
| 5 | low | e3r-3 | `Icons up to date` gives re-pin advice for any generator failure | fix (e3r-x): a distinct exit code for a checksum mismatch |
| 6 | low | e3r-3 | No `.gitattributes` rule for `assets/game-icons/**`, so `core.autocrlf=true` would fail every pinned SVG | fix (e3r-x): `-text` |
| 7 | low | e3r-3 | `sim_sh_test.py` skips the class when a tool is missing, and "Ran 4 … OK (skipped=4)" passes the job's `Ran [1-9]` check | fix (e3r-x): fail instead of skip |
| 8 | low | e3r-1 | The pixel charge counts only this host's canvas, so a host with another canvas charges the same game differently | owner decision before the freeze (epic-first-party-games Notes) |
| 9 | low | e3r-1 | `limit icon_*_side_pixels` records exact values under a kind whose other entries are maximums; the grammar has no exact-value kind | owner decision before the freeze (epic-first-party-games Notes) |
| 10 | low | e3r-2 fixture | `slow-restart` spins 2 s in `setup` against the 3 s watchdog | accept (fixture only); revisit if it flakes |
| 11 | low | e3r-2 | The tap gate opens when the new round's first frame is published, not when the e-ink refresh ends | accept; already deferred (`## e3r-2`) |
| 12 | low | e3r-3 | `sim.sh`'s exact-line marker match treats CRLF or trailing blanks as "no markers" (pre-existing). The end-marker check at `sim.sh:85` is now unreachable | defer (`## e3r-x`) |
| 13 | low | e3r-3 | A games guard whose comment spans lines fails with the same message as an unguarded include | defer (`## e3r-x`) |

- **Refuted:** that the device and simulator diverge on long names. Only a cosmetic "..." remains.
- **Already deferred:**
  - a gesture pressed in the gap and lifted after the gate opens;
  - no host test for `GameAssets`, `GameVM`'s wrappers, or `GameMatchActivity`;
  - `frontBlitFills` copying the replay dispatch.
- **Checked clean:**
  - the budget arithmetic and its reset per frame;
  - the charge matching the 1:1 replay;
  - the `icon_image_pixels` fixture's arithmetic (the 65th large icon fails);
  - the budget fault versus the widened `failedToStart`;
  - the `errorMessage` gate versus `run()`'s log;
  - the layer check against the lanes' new includes;
  - the moved data paths (`GameSaveStoreTest`);
  - the icon header regenerating byte-identically.

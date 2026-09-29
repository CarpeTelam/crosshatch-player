# Cross-story review: epic-install-and-launcher

The cross-story review (`docs/crosshatch/orchestrated-epics.md`, "Before the epic PR") looked at every story's change at once, to catch what each story's own review could not.

## How it ran

- **The diff:** `git diff 962ae61c..9f6e015c`, the epic base to entry 13's merge. It excluded `_bmad-output`, PNGs, the generated icon header, the host tests' raw icon reference (`test/game_script/GameIconsRaw.h`), and the binary package vector: 169 files, about 22,000 added lines.
- **The weighting:** the boundaries between stories.
  - The save lifecycle (4.3, 4.10, 4.11, 4.12, 4.13).
  - The installer and the launcher (4.3, 4.6, 4.7 against 4.8, 4.10, 4.12).
  - The packer and the installer (4.2, 4.7, 4.13 against 4.3, 4.6, 4.7).
  - Icons (4.7, 4.8, 4.13, 4.15).
  - The activity stack and the upstream touches.
  - The shared harness doubles.
  - Concurrency.
  - The budgets.
- **The lenses:** three context-free reviewers, one per bmad-review lens: adversarial, edge-case, and verification-gap.
  - Each verified its findings in a scratch copy where it could.
  - The edge-case reviewer's host build timed out behind the build lock, so its findings are traced, except finding 1, which the adversarial reviewer verified with the real converter.
  - The verification-gap reviewer ran the full host suite ten times on `9f6e015c`: 1,302 of 1,302 passed every time.
- **What came back clean:** no path deletes or overwrites a valid save the person did not choose to replace, beyond the recorded one: a New match from a game's own row replaces its save.
  - Every `Error` maps to its own reason.
  - Only ledgered upstream files changed, and `english.yaml` only grew.
  - The only new mutable static is `lastOpened` (4 B, `constinit`).
  - PackBits decoding stays in bounds, and `GAME_CONTROLLER_32` is byte-identical to the base.
  - Every new suite runs in CI.
- **The fix:** the orchestrator triaged the findings below and sent the accepted ones to one fix build, `e4-x`. That fix commit gets its own review before the push.

## Triage

| # | Severity | Stories | Finding | Outcome |
| --- | --- | --- | --- | --- |
| 1 | medium | 4.2, 4.7 × 4.3, 4.6 | `pack_game.py` accepts any square `icon.png`, but the converter scales in `float` and turns 280 of the sides 1–2,048 (41, 47, 55, 61, 82, …) into 63 px, which the installer rejects as `BadImage`: the package packs cleanly and lands on the card as `.bad` (adversarial 1, edge 1; verified with the real converter at side 41) | fixed (e4-x, `b976e765`): the packer refuses such a side with its reason, by the converter's own float arithmetic; tests on both sides pin a failing side |
| 2 | medium | 4.13 × 4.12 × 4.10 × 4.11 | The flake cure `letStartedMatchesGo` deletes the save the forced exit writes, so no test covers the everyday path: a New match left mid-round, then the launcher selects its new Continue row and Confirm resumes it. Three `ContinueTest` cases pin a state that production never reaches (verification-gap 1; verified by disabling the delete) | fixed (e4-x, `b976e765`): the helper reports what it removed; a test for New → move → Leave → Continue selected → resume; the three tests state their save state |
| 3 | medium | 4.2, 4.7, 4.13 × 4.3, 4.6 | No test installs `pack_game.py`'s live output with the C++ installer. The bridge is one committed vector with no image or icon, so a packer framing change would ship with every suite green (verification-gap 2) | fixed (e4-x, `b976e765`): the host build packs fixture folders (one with `icon.png` and a `.png`) and `GameInstallerTest` installs them |
| 4 | low | 4.3 × 4.10 | When an install succeeds but the inbox file will not delete, the file stays in `/games` and reinstalls on every launcher entry, undoing a Remove (adversarial 2, traced) | fixed (e4-x, `b976e765`): an installed inbox file that cannot be deleted is renamed out of the inbox, with a test |
| 5 | low | 4.12 × 4.10 × 4.13 | If the Continue list's allocation fails, every Continue row disappears and the launcher selects the game's own row, where one Confirm replaces the save (adversarial 3, traced) | fixed (e4-x, `b976e765`): the list is a fixed member array (128 B, inside the heap-allocated activity), so there is no failure path |
| 6 | low | 4.11 × 4.13 | Play again clears the pending `resume.bin` delete before the new round writes. If the Over delete failed and the rematch's first write fails or its setup errors, Leave keeps the finished round's save and Continue offers it (edge 2, traced) | fixed (e4-x, `b976e765`): the pending delete clears only once the new round's first write succeeds |
| 7 | low | 4.3 × 4.5, 4.8, 4.10, 4.12 | Past 64 games the installer still installs, but the registry lists the first 64 in directory order, so a listed game (and its Continue row) can silently vanish and cannot be removed from the launcher (edge 4, traced) | fixed (e4-x, `b976e765`): the installer refuses a 65th game with its own `Error` and reason |
| 8 | low | 4.10 × 4.3 | `GamePackageInstaller::remove` has 288 B of locals, over AGENTS.md's 256 B (adversarial 4) | fixed (e4-x, `b976e765`): `remove`'s x4pro frame is 160 B, and every frame in `GamePackageInstaller.cpp` is now at most 240 B on x4pro (`c147fb1c`) |
| 9 | low | 4.13 × 4.3 | `pack_game.py`'s manifest nesting limit is a hand copy of `StreamingJsonParser::MAX_NESTING`, with no shared vector (verification-gap 3) | fixed (e4-x, `b976e765`): a nesting case in `package_vectors.json`, checked on both sides |
| 10 | low | 4.11 × 4.3 | A power loss during `resume.bin`'s rename can leave it and `resume.bin.tmp` on one cluster chain; the next write then frees the save's clusters. `store.bin` has had the same exposure since before this epic (edge 3, traced through SdFat) | deferred: an `Assumption for entry 14:` line in the Notes, beside the installer's cross-link one; a guard is a storage design change |
| 11 | low | 4.11 × spine | The forced exit also retries the `resume.bin` delete, but spine AD-17 says the resume write and the `ch.store` flush are the only SD writes in `onExit()`; the Notes and `formats.md` record the retry, the spine does not (adversarial 5) | owner decision at entry 14: amend AD-17 and AD-20 (the spine changes only by owner decision) |
| 12 | low | 4.7 × 4.9 | The mode picker cannot open in shipped firmware (`pass` and `nearby` are off), so R8's picker is verified on the host only (adversarial 6) | record: stated in the entry-14 packet |
| 13 | low | all | R10's "wake from sleep lands on Home", R4's mbedTLS hash, R11's device bound, R15, and R1's joined install-then-list path rest on the simulator or entry 14 (verification-gap 4) | record: each is an entry-14 step in the packet |

## The fix and its review

`e4-x` built rows 1–9 (plan `_bmad-output/implementation-artifacts/plan-e4-x-cross-story-fixes.md`) in three commits, each reviewed by context-free reviewers before the next:

- `b976e765`, the fixes. Its adversarial and edge-case review found:
  - a read-only inbox file could still loop through a stuck `.installed` name;
  - a pending delete could remove the rematch's half-written save;
  - waiting 65th-game packages could starve later ones.
  Its verification-gap review killed a mutation of every row and found that the installer's 528 B frame was this epic's own, not inherited.
- `c147fb1c` fixed all of those. Along the way:
  - it added numbered aside names for `.installed` and `.bad`;
  - it cleared the pending delete once the finished round's file is gone;
  - it brought every installer frame to at most 240 B on x4pro;
  - it counted games once per run.
  Its review compared every guard in the refactor line by line and found them intact. It also found that a commit failing after the folder rename left a game uncounted (verified: 65 games, one hidden), and that waiting packages were extracted on every visit.
- `93371981` recounts after a late failed commit and refuses a waiting package right after its manifest is read, with no card writes. It also bounds the aside names with a `static_assert`.

At `93371981`: 1,331 host tests pass; 20 full runs had no failure; each new or changed test passed 200 repeated runs.

---
title: 'Owner review of the icon set and screens'
type: 'review'
ticket: '8'
created: '2026-09-28'
status: done
route: 'hitl'
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-icon-library/epic-icon-library.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-icon-library/review-packet/README.md'
---

## Intent

The owner reviews the v1 icon set, its names and drawings, legibility at 32 px in black and white, the runtime views,
and the Home tile from the review packet the orchestrator built after entry 7. The owner answers every
`Assumption for entry 8:` line in the epic Notes, entry 7's deferral reasons included. The changes the owner asks for
are applied while level 1 is a preview.

## Owner's answers (2026-09-28)

The owner answered the numbered list of the 27 `Assumption for entry 8:` lines in this session. Each answer is dated in
the epic Notes (`Decision (entry 8...)` lines).

- **1-10, 12, 16-20:** approved as written.
- **11, 13, 14:** every icon takes its exact Phosphor 2.1.1 name, kebab-case included, with no category prefixes and no
  action names, so developers can look each one up on the Phosphor site. On 14 the owner confirmed the Phosphor names
  (`waves`, `dot-outline`).
- **15:** every icon ships in both the regular and fill weights. `ch.gfx.icon` takes an optional sixth argument,
  `weight`: `"regular"` (the default) or `"fill"`.
- **Chess pieces:** dropped, the originals `pawn` and `bishop` included. A chess game ships its own images through
  `ch.gfx.image`.
- **Flash:** option A. The icon-data cap rises from 48 KiB to 96 KiB, and compression stays a later, API-neutral option.
- **Scope:** the names-and-weights change is built on this PR as entry 9, and the owner chose four deferred items as
  entry 10.
- **21-27** (entry 7's deferrals, R4, the new load-failure string): the orchestrator's recommendations were accepted.
  - R10 goes to epic-install-and-launcher, with a ledger-row decision.
  - The R3 residual and the device-only coverage wait for the AI-2 harness.
  - Dithered-image timing waits for a device run.
  - The two handoffs (the installer's budget rule, the SDK `DialogOption` icon) are carried in the downstream epics'
    Notes.
  - R4 is to be settled before the freeze.
  - "The game did not load" is kept.
- **Static RAM:** the 248 B of headroom was the real figure before this epic too. The base `1eacdc77`, measured with
  IRAM counted, is already +776 B, so this epic adds 0 B.
- **Before the next epic:**
  - The orchestrator carried the open handoffs into epic-install-and-launcher's and epic-game-api-docs' Notes.
  - A cross-story review of the combined diff ran, with four lenses as context-free subagents. Its eight verified
    findings are fixed in `f27dcefd` and triaged in `cross-story-review.md`.
- **Sign-off:** the owner delegated the final look to the orchestrator: once entries 9 and 10 were complete and the
  tree was in good shape for the next epic, push and merge the PR with auto-merge (2026-09-28).

## Changes applied

- **Entry 9** (`9197d046`): Phosphor names, both weights, the `weight` argument, and the chess pieces removed.
  - Icon data is 71,975 B of the 96 KiB cap.
  - x4pro games on minus off is +227,408 B of flash and +776 B of static RAM.
- **Entry 10** (`0520b146`):
  - `sim.sh check`;
  - the manifest-key surface test;
  - the spine-vs-`LAYERS` test;
  - the pure host-failure wording mapping.
- **Cross-story review fixes** (`f27dcefd`):
  - the `GameVM::failure()` data race;
  - an image-header read error mapped to `CannotRead`;
  - the "Game pieces" label;
  - a manifest `_` test;
  - four doc corrections.
- **Epic file:** R1, R3, R4 and Done when 1 are amended to match.

## Verification

- **Owner's answers:** recorded, dated, in the epic Notes and in this plan.
- **Combined tree after entries 9 and 10** (`ba2ae079`), all passing:
  - `default`, `x4pro`, `sticky`, `x4c` and `papermono` build;
  - `pio check` (low, medium, high) reports no defects;
  - 706/706 host tests;
  - every `scripts/*_test.py`;
  - the icon regeneration is byte-identical;
  - `check_layers.py` and `check_upstream_touches.py`.
- **After the review fixes** (`f27dcefd`, the fix agent's run):
  - 707/707 host tests;
  - `x4pro` and `default` build;
  - `sim.sh build x4pro`;
  - x4pro games on minus off is +227,712 B of flash (28,288 B under the gate) and +776 B of static RAM (248 B under the
    gate), measured on the fix worktree.
- **CI on the PR:** green on every head through `2ba81907`. The final head's checks gate the auto-merge.
- **Screenshots:** the review packet (`review-packet/`) is rebuilt on the final tree. 3.9's
  `story-names-weights-screenshots/` show every icon in both weights, the three views on x4pro and Sticky, and the Home
  tile.

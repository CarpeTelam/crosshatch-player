---
title: 'Fork release with the three games'
type: 'chore'
ticket: '6'
created: '2026-10-11'
status: done
baseline_revision: 'f84399ed2da8728b6f4ba4b2e2c85aa41a0d8391'
route: 'oneshot'
route_source: 'owner'
review: 'none'
review_source: 'hitl'
context: []
warnings: []
deferred: []
---

<intent-contract>

## Intent

**Problem:** Done when 4 and R13's second half need a published fork release, made by `crosshatch-release.yml` (AD-25), with `sudoku.chgame`, `ultimate-tic-tac-toe.chgame` and `battleship.chgame` attached, each packed byte for byte from `games/<id>/` at the released commit.

**Approach:** The owner confirmed the target at the epic-first-party-games retrospective on 2026-10-11. The workflow was dispatched from `develop` with `dry_run` off and `ref` empty, which releases `develop`'s head, `f84399ed`. That commit is the epic (merged at `39061f44`) plus the upstream sync (CarpeTelam/crosshatch-player#36) and the e6pre cleanup (#37). `games/` and `scripts/pack_game.py` are unchanged from `39061f44` to `f84399ed` (`git diff --stat 39061f44 f84399ed -- games scripts/pack_game.py` is empty), so the games are the ones the epic shipped. The sync's own dry run, run 38100498514, had passed. No repository file changes.

</intent-contract>

## Verification

- **Run:** https://github.com/CarpeTelam/crosshatch-player/actions/runs/38106383670 (run 14, `workflow_dispatch` on `develop` at `f84399ed`, 2026-10-11 02:49 to 03:10 UTC). Both jobs succeeded:
  - "Build and check": the release envs built, then "Pack the games".
  - "Tag and publish": the tag was free, then the tag was created, then the draft was created with every asset, then it was published as the latest release.
- **Release:** https://github.com/CarpeTelam/crosshatch-player/releases/tag/1.6.5-ch.4. The tag `1.6.5-ch.4` resolves to `f84399ed2da8728b6f4ba4b2e2c85aa41a0d8391`. The release was published at 03:10:15 UTC, and is neither a draft nor a prerelease.
- **Assets:** `battleship.chgame` (10,167 B), `sudoku.chgame` (42,695 B), `ultimate-tic-tac-toe.chgame` (5,361 B), `crosspoint-1.6.5-ch.4-x4pro.bin` (6,169,776 B), and `crosspoint-1.6.5-ch.4-sticky.bin` (6,052,112 B).
- **Hashes:** each package was packed locally with `python3 scripts/pack_game.py games/<id> <out>` over `git archive 1.6.5-ch.4` (`games`, `scripts`, `lib/GameCore/ApiLevel.h`, `assets/game-icons`). Five sources give the same values:
  - the release notes' Games table
  - GitHub's asset `digest`
  - the local pack's last line (package hash) and `sha256sum`
  - the device-run packet's table
  - dry run 38025954968's table

| Package | Package hash | SHA-256 |
| --- | --- | --- |
| `sudoku.chgame` | `f31b5a02a67a712a` | `a54e37cf7bf4a0dd9cae9162c81b30eda2e57a2d4a17abd615adec3c6b077108` |
| `ultimate-tic-tac-toe.chgame` | `cd11a6224228acd9` | `f7689860932dff3a7112efe07f61dc2a63d3b5fe8f87bb8a996b9a87add48b84` |
| `battleship.chgame` | `d3fecc1e5fc245f6` | `8fe47f261cab5df7dddfcb4c619d7066b5f5afdfe85f591af3802ed1d7500d63` |

The device-run checks still owed (`deferred-work.md` `## e5-run4`, and the retrospective's AI-7) can now run on this release's images and packages.

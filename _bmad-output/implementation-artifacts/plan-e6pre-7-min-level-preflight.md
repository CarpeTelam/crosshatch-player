---
title: 'e6pre-7: release preflight refuses an API_MIN_LEVEL raise over a frozen level'
type: 'feature'
ticket: ''
created: '2026-10-04'
status: 'in-review'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: []
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `freeze_problems` compares only `frozen_top` (API_LEVEL, API_LEVEL_FROZEN), so a release may raise `API_MIN_LEVEL` above a level an earlier release shipped frozen, and games of that level stop running (deferred entry e2r-ai-9; AD-19).

**Approach:** Owner decision (a), 2026-10-04: the preflight also refuses it. An earlier release's frozen runnable levels are `API_MIN_LEVEL..frozen_top` of each `-ch.N` tag's `ApiLevel.h` (already read by `tag_api_level`); the commit's minimum may not exceed the lowest such level.

</frozen-after-approval>

## Implementation Notes

Oneshot: about 40 lines in one function plus tests. `freeze_problems` keeps its first refusal and its text unchanged and returns a second message for the minimum. A tag whose own minimum is above its frozen top ran no frozen level and never triggers it; a tag without the header is skipped; a commit without the header gets only the existing refusal. Guards kept: tags sorted by build number, non-`-ch.N` tags ignored, unreadable header at a tag is a SetupError.

## Review Triage Log

Quick review ran as one context-free subagent. Finding 1 (high, patched): comparing against every tag's own minimum refused forever after an earlier drop; now compares against the highest minimum among releases that ran a frozen level; test added. Finding 2 (low, patched): tests for multi-tag history and tags without a frozen run level or header. Finding 3 (low, patched): docstring line length.

## Verification

**Commands:**
- `python3 scripts/fork_release_test.py` -- expected: OK
- all `scripts/*_test.py`, `scripts/check_layers.py`, `scripts/check_upstream_touches.py`, `./bin/clang-format-fix` -- expected: pass

**AD-19 sentence for the later spine pass:** the Freeze bullet's "Amended 2026-09-28" sentence ending "A release whose `API_LEVEL` is an open preview above every level released as frozen is allowed." should add: "A fork release is also refused when its `API_MIN_LEVEL` is above a level an earlier fork release ran and shipped frozen, because games of that level would stop running; raising the minimum is allowed only over levels no release shipped frozen." The "(Pending the matching `fork_release.py` preflight change in the same PR.)" tail can go.

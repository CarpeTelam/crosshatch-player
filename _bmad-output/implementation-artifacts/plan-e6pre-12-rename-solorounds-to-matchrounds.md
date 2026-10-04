# e6pre-12: rename SoloRounds to MatchRounds

Intent: pure rename of `GameScript::SoloRounds` (files, class, test suite, wiring, docs, comments) to `MatchRounds`; byte-identical logic. Unrelated "solo" concepts (solo mode, solo match, the solo fixture) keep their names.

## Design Notes
No function is moved or rewritten, so no guard is touched. Files moved with `git mv`. Not edited: the spine, older plans, retros, earlier deferred-work entries.

## Review Triage Log
One quick lens, run in the build agent's own context (no subagent; the diff is a mechanical rename): a word-level `git diff -M` shows only SoloRounds/MatchRounds, SoloRoundsTest/MatchRoundsTest, the two include lines, and one comment word (solo) changed. No findings.

## Verification
Host ctest 1676/1676 passed (same suite, renamed); `check_layers.py` and `check_upstream_touches.py` pass; `./bin/clang-format-fix` changed nothing; `pio run -e x4pro` and `-e default` succeed; `pio check` (default, and -e x4pro) pass; `sim.sh build x4pro` succeeds. `grep -rn SoloRounds lib src test docs scripts .github` is empty.

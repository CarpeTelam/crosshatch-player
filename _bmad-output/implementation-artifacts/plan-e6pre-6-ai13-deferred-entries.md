---
title: 'e6pre-6-ai13-deferred-entries: the three deferred-work entries epic 4 retro AI-13 dropped'
type: 'chore'
ticket: ''
created: '2026-10-04'
status: 'done'
route: 'oneshot'
route_source: 'pinned'
review: 'quick'
review_source: 'auto'
lenses_ran: [quick]
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Epic 4 retro AI-13 (findings spec-5, rev-7, rev-9) asked for three `deferred-work.md` entries; none exists, so A3, the over-long inbox name, and the aside-names-exhausted reinstall are tracked nowhere.

**Approach:** Append the three entries under `## e6pre-6` at the end of `deferred-work.md`, each verified against code at HEAD with file:line, with a trigger that can fire. No code, no edits to old entries.

</frozen-after-approval>

## Implementation Notes

Oneshot: a docs-only append whose only risk is a wrong claim, so the work is the verification below. The heading is `## e6pre-6` as the orchestrator's prompt names it (the brief's `## {ref}` would be the longer ref).

Verified at HEAD 1a094c94:
- A3: `scripts/pack_game.py:41-55` imports no codec (stdlib plus `fork_common`); `game_codec` is referenced only by `game_codec_test.py`, `test/game_script/{CodecTest.cpp,CMakeLists.txt,codec_vectors.json}`, `formats.md`, and the ledger. The notation parser is `game_codec.py:341-600` (`_Parser` 401, `parse_value` 541, `parse_hex` 546) in a 775-line file. The old trigger is in the entry at `deferred-work.md:179` ("triggered by `pack_game.py` first importing the codec"); the packer plan forbade the import (`story-pack-game-py-and-the-package-vectors-plan.md:31`, per retro spec-5).
- rev-7: `GamePaths.h:26` `INBOX_NAME_BYTES = 64`; `forEachInboxFile` (`GamePackageInstaller.cpp:171-187`) accepts `0 < length < 63` (`:178`), so a name of 63 bytes or more is skipped with only a `LOG_INF` (`:181-182`); nothing reaches the screen, and `hasInbox` (`:926`) goes through the same function.
- rev-9: `ASIDE_NAMES = 5` (`:58`); `moveAside` (`:763-780`) tries `<name>.installed`, `.2` to `.5`, and returns false when each exists and will not delete (`:770-773`, `:779`). `install` calls it after a delete failure (`:866`), so the file stays in the inbox and the call reports `SdCard`. `commit` (`:733-754`) replaces `/.games/<id>` unconditionally, with no check of the `.pkg` hash against the file, so the next visit installs the game again, including one the person removed. The claim holds as recorded; the existing test `WhenEveryAsideNameIsTakenAndStuckTheFileIsReportedAndKept` (`test/game_script/harness/GamePackageInstallerTest.cpp:978-994`) pins the keep and the report but not the second visit.

## Review Triage Log

Lens: quick, run as a context-free subagent; it returned.
- Wrong line range for the `moveAside` skip (`:770-773`): low, real; patched in the entry and this plan.
- Packer plan cited without a directory: low, real; full path now.
- "Only ... name the codec" omitted `CMakeLists.txt`: low, real; added.
- A3 trigger does not say why `CodecTest.cpp` does not count: low, real; the entry now says it reads the JSON, not the notation.
- Retro asks to widen `## e4-x` in place: not changed, since the task forbids editing old entries; the new entry cross-references it by quote.
- "Data-integrity bug" stronger than the retro's Low: rejected, the task's wording and the mechanism (a Remove undone) support it.
- Host-test prediction is unrun: accepted, the entry words it as a pin to write.

## Verification

- `git diff --stat` -- expected: `deferred-work.md` (appended lines only) and this plan; no code.
- `./bin/clang-format-fix` then `git status` -- expected: nothing new.

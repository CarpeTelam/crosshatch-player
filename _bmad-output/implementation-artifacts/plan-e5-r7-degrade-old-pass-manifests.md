---
title: 'e5-r7: an installed game that lists pass with one seat keeps its solo mode'
type: 'bugfix'
ticket: ''
created: '2026-10-03'
status: 'done'
route: 'full'
route_source: 'pinned'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '16a0889b84be568b48b5f41d6f9d0ec4a9c957af'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Epic pass-and-play's R10 made `Manifest::check` return Invalid / `NearbyNeedsTwoSeats` for any manifest with pass or nearby and `seats.max` below 2. A package installed before the epic that listed `pass` with one seat was inert then and is Invalid now, which hides the whole game, solo included, behind an error row (retrospective F7).

**Approach:** The owner decided (2026-10-03) "Degrade to solo": such an already-installed game keeps working in the modes that do work; the pass or nearby claim is unavailable (not startable), with the reason logged. New packages with that manifest may still be rejected where they are packed (`scripts/pack_game.py` keeps its R10 check, unweakened). No API-level entry, no `API_LEVEL_FROZEN` change.

</frozen-after-approval>

## Implementation Notes

- `lib/GameCore/Manifest.cpp` `check`: the one rule changes. When the manifest claims pass or nearby with `seatsMax < 2` (new `Manifest::claimsUnseatedMode()`), it is Invalid / `NearbyNeedsTwoSeats` only when it has no solo mode (nothing is left to start: the existing pass-only test keeps its Invalid row and its two log lines). With solo it falls through the API and seat checks and the startable computation leaves pass and nearby out, so the result is Ok with `modes == MODE_SOLO` and `reason == NearbyNeedsTwoSeats`, the dropped claim, for the caller to log. `CheckResult` gained no field: Ok with a reason other than None is the new documented state (header comment). With `seatsMax < 2` pass and nearby are both unseated, so solo is the only mode that can remain.
- Guards read in `check` before editing (`git log -L` pitfall; the function dates from epic-install-and-launcher and R10): the `fieldsValid` and `SoloNeedsOneSeat` Invalid returns (kept, first, so other invalid fields stay Invalid); the API and `TooManySeats` Unavailable returns (kept, still before the degrade, so an api the host lacks is Unavailable, not degraded); the nearby `host.maxSeats >= 2` and `host.pass` conditions (kept, inside `!unseated`); the `startable == 0` NoHostMode return (kept; unreachable for a degraded manifest since solo is in `modes`).
- The installer (decision, the owner's words "already-installed games degrade; `pack_game.py` still rejects new ones"): `GamePackageInstaller.cpp` shared `check` and rejected on Invalid, so the change alone would have made it accept the package. Kept strict by a small split: `Manifest::claimsUnseatedMode()` is public, and the installer rejects on `Invalid || claimsUnseatedMode()` with `BadManifest` and the same log text. Registry load degrades, install rejects (the orchestrator's recommendation). Not-yet-reinstalled games on the card are unaffected.
- `src/games/GameRegistry.cpp`: logs one `LOG_INF` per degraded game on load ("<id>: pass and nearby need seats.max 2 or more; its other modes still work"). The launcher needed no change: Ok rows list as before (their modes line comes from `check.modes`, which now holds solo only), `GameModeActivity` reads `check.modes`, `GameSaveStore::startableFor` reads `ok()` and `modes` (so a stale pass save for such a game is not resumable, as for any mode a host cannot start).
- Tests: `ManifestCheckTest` (`NearbyNeedsSeatsMaxTwo`, `PassNeedsSeatsMaxTwo` rewritten: solo+pass/solo+nearby degrade, pass-only and pass+nearby stay Invalid on every host, `ADegradedGameStillFailsOnItsOtherFields`, `ClaimsUnseatedModeIsTheClaimCheckDrops`); `RegistryTest` (two new, listed-and-solo with the log, pass-only Invalid); `InstallerTest.AManifestThatBreaksItsOwnRulesEndsBad` (two new rejected cases); `TitleScreenTest.ASoloAndPassGameWithOneSeatOpensItsTitleScreenInSolo` (launcher row Ok, title screen solo, no Options, log line, no "Invalid"). `TitleScreenTest.APassOnlyGameWithOneSeatOpensNothingFromTheLauncher` is unchanged but for its comment. Run first on the old `check`: 5 of the new or rewritten tests failed (the installer cases pass either way: they pin that installs stay strict).
- Consequence of strict installs, for the owner: updating a degraded game with a package whose manifest still lists pass with one seat is rejected (`BadManifest`); the author fixes the manifest, as the packer requires anyway.
- `scripts/pack_game.py`: unchanged (R10 check kept). `docs/crosshatch/api-level-1.txt`: states no seat rule for pass (its `seats_max 2` line is an unrelated example), unchanged; no API-level entry.
- Epic R10 text ("`Manifest::check` makes a manifest that declares `pass` with `seats.max` below 2 invalid") no longer matches: `check` now makes it invalid only with no solo mode, and degrades it otherwise. The epic file is not edited; for the owner to reconcile.
- `CheckReason::NearbyNeedsTwoSeats`'s `describe` text stays (the existing test pins it); GamesLauncherActivity's switch over reasons is unchanged.

## Review Triage Log

All four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as context-free subagents and returned.

1. Edge-case: the installer logs the seat-rule reason even when the manifest is also BadFields or SoloNeedsOneSeat. Verdict low, real (misleading diagnosis); patched: the installer logs `verdict.reason` when Invalid, the seat rule only otherwise.
2. Blind and edge-case: Ok now carries a non-None reason (mixed state); a caller reading `reason` without `status` would misread it. Verdict low (no such caller: the launcher and registry test status first, `GameSaveStore` uses `ok()`); documented in the header and recorded in deferred-work `## e5-r7`. A new field was rejected as more API for no present caller.
3. Edge-case and blind: the registry's LOG_INF repeats on every launcher open. Verdict false as a defect: the launcher's Invalid and Unavailable lines log on every load the same way.
4. Edge-case and blind: updating a degraded game with the same manifest is rejected. Verdict low, intended (strict installs, the owner's "pack_game.py still rejects new ones"); stated in Implementation Notes.
5. Edge-case: a future install path that calls `check` alone would accept it. Verdict low, `claimsUnseatedMode()` is the named predicate and the header says installs use it; the only install path today is `GamePackageInstaller::judge`. Rejected.
6. Blind: the seat rule lives in `claimsUnseatedMode()`, `check`, and the installer. Verdict false: both callers use the one predicate; the installer's `Invalid ||` is the existing check.
7. Blind and verification: no installer or registry case for nearby with one seat; "every host" overclaimed. Verdict low; patched: installer case "solo and nearby with one seat" added; the plan says "stay Invalid" (tests cover HOST, NO_PASS_HOST, NEARBY_HOST).
8. Blind: no test of `GameSaveStore::startableFor` or `GameModeActivity` for a degraded game. Verdict low: both read `check.modes`/`ok()`, which `ManifestCheckTest` and the new `TitleScreenTest` pin (the title test shows solo and no Options). Rejected.
9. Blind: no visible row note. Verdict low, owner asked for "logged" only; deferred-work `## e5-r7`.
10. Blind: epic R10 and docs not reconciled. Verdict as the brief says: the epic file is not edited (note below); `api-level-1.txt` states no seat rule.
11. Blind: plan pending, no build evidence. Verdict false, filled in below.
12. Verification-gap: no gaps found. Intent-alignment: reading A (verdict-level degrade) plus strict installs implemented; divergences are the installer rejection (the orchestrator's recommended decision, recorded) and "logged, not shown" (the owner's words).

## Verification

**Commands (worktree /home/user/wt/lane-b):**
- Host tests: `cmake --build build/test` and `ctest --test-dir build/test -j` -- 1634 of 1634 passed after the last patch. Reproduced first: with `Manifest.cpp` and `GamePackageInstaller.cpp` reverted to base, 5 of 46 selected tests failed (`ManifestCheckTest.NearbyNeedsSeatsMaxTwo`, `.PassNeedsSeatsMaxTwo`, `.ADegradedGameStillFailsOnItsOtherFields`, `RegistryTest.AGameThatListsPassWithOneSeatIsListedAndStartsSolo`, `TitleScreenTest.ASoloAndPassGameWithOneSeatOpensItsTitleScreenInSolo`).
- `scripts/check_upstream_touches.py` -- PASS. `./bin/clang-format-fix` twice -- no change; `git status` shows only this commit's files.
- `pio run -e x4pro`, `pio run -e default` -- both exit 0. `pio check` (default) and `pio check -e x4pro`, each with `--fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- both exit 0.
- `check_flash_budget.py` `build on`, `build off`, `compare`, `objects` -- exit 0 on both trees. Measurement (method: the script's four steps on the base commit `16a0889b` in a separate `git worktree` under the scratchpad, `freeink-sdk` copied from this tree, and the same four steps on this tree with the patched, uncommitted work; no figure from an earlier run): games-on `firmware.bin` base 5,933,568 B, this tree 5,933,664 B, so **+96 B flash** (games-off 5,679,824 B on both); static internal RAM games-on minus games-off +784 B on both, so **+0 B**. `objects`: no static initializer.
- Epic R10's text ("`Manifest::check` makes a manifest that declares `pass` with `seats.max` below 2 invalid") no longer matches (now invalid only without solo); the epic file is not edited, for the owner to reconcile.

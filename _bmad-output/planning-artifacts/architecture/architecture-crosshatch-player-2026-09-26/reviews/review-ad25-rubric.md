# Rubric review: AD-25 update (fork releases and the update source)

Reviewed 2026-09-26 against `ARCHITECTURE-SPINE.md` (updated 18:03), `.memlog.md` entries 74 to 92, `SPEC.md`, the initiative file, `epic-platform-baseline/` (epic and `tickets.toml`), and the codebase (`src/network/OtaUpdater.cpp`, `lib/JsonParser/ReleaseJsonParser.h`, `src/network/FirmwareBoardTag.cpp`, `platformio.ini`, `scripts/git_branch.py`, `.github/workflows/*.yml`).

## Verdict

**Pass with fixes.** AD-25 resolves the initiative's source conflict. Its rules are mostly enforceable, and the design fits the existing OTA code: the asset name, the board suffixes `x4pro` and `sticky`, the tag and asset buffer sizes, and prereleases being skipped by `releases/latest` all check out. One statement contradicts `platformio.ini`. Two divergence points are left open. The spec and the platform-baseline epic still describe a 9-file ledger with a reserve slot.

## Findings

### F1 [High] The spec Constraint is stale

`SPEC.md` line 61 reads: "Upstream changes are limited to the 9-file ledger in AD-3 plus 1 reserve file." The spec calls itself the canonical contract, so after AD-3 row 10 this sentence permits an 11th file. That contradicts AD-3, which now says no reserve remains.

**Fix** in `_bmad-output/specs/spec-crosshatch-player/SPEC.md`, Constraints: replace the sentence with "Upstream changes are limited to the 10-file ledger in AD-3, with no reserve left."

### F2 [Medium] AD-25 contradicts `platformio.ini` on the reported version

AD-25 bullet 1 says "the x4pro firmware reports `1.6.5-ch.7-x4pro`". That is wrong for the build AD-25 releases:

- `[env:x4pro-gh_release]` (platformio.ini:405) and `[env:sticky-gh_release]` (:290) both set `-DCROSSPOINT_VERSION=\"${crosspoint.version}\"` with no suffix, so a release build reports exactly `1.6.5-ch.7`.
- Only the dev `[env:x4pro]` (:326) appends `-x4pro`.
- Dev `sticky` gets `X.Y.Z-dev-<branch>-<sha>` from `scripts/git_branch.py`.

Memlog entry 84 carries the same error ("x4pro appends -x4pro"). A unit that writes the `-ch.N` parser or its tests from this example would build the wrong expectation.

There is a related gap. A dev branch name can contain `-ch.` (for example `feat-ch.3`), so an unanchored substring search would read a false `N` from a dev build. The rule does not pin how `N` is parsed.

**Fix** in `ARCHITECTURE-SPINE.md` AD-25 bullet 1: replace "and the x4pro firmware reports `1.6.5-ch.7-x4pro`" with "and `x4pro-gh_release` and `sticky-gh_release` firmware report exactly the tag". Replace bullet 2's last sentence with: "`N` is the decimal run immediately after a leading `X.Y.Z-ch.`; any other version string, including every dev build, is `N = 0`."

### F3 [Medium] Where the `-ch.N` logic lives and how it is tested are unpinned

Row 10 says only "fork release source and fork build-number comparison". Two implementers could diverge:

- One inlines a parser in `OtaUpdater.cpp`, which widens the upstream conflict surface against AGENTS.md's "fork-only code in new files".
- Another writes a fork-only helper.

Neither needs a host test. A new test directory would also need a third `add_subdirectory` line, but ledger row 3 lists exactly two.

**Fix:** add an AD-25 bullet: "The URL and the `-ch.N` parse and compare live in one pure, fork-only header with no device includes, tested in `test/game_core`. `OtaUpdater.cpp` gets only guarded call sites: the URL constant and the body of `isUpdateNewer()`." Add the header to the Structural Seed source tree.

### F4 [Medium] Initiative and platform-baseline files are stale, and row 10 has no owner

- `epic-platform-baseline.md` R1: "lists the 9 AD-3 rows, the 1 reserve slot". Change it to "lists the 10 AD-3 rows and the baseline allowlist".
- `epic-platform-baseline.md` Done when 2: "all 9 rows of AD-3". Change it to "all 10 rows of AD-3".
- `epic-platform-baseline/tickets.toml` entry 2: "with the 9 AD-3 rows, the reserve slot, and the allowlist". Change it to "with the 10 AD-3 rows and the allowlist".
- `initiative-crosshatch-player-v1.md`:
  - References: change "AD-1 to AD-24" to "AD-1 to AD-25".
  - The Notes "Source conflict" bullet is still written as open. Mark it resolved by AD-25.
  - Boundaries has no touch point for ledger row 10 (`OtaUpdater.cpp`) or for the fork release workflow. Add one that names an owner, for example: "Touch point: `src/network/OtaUpdater.cpp`, the fork release workflow — fork update source and `-ch.N` releases (ledger row 10, AD-25); owner: epic-platform-baseline". Without an owner, first-party release assets (epic-first-party-games) have nothing to attach to.
- `architecture-walkthrough.html` still says "AD-1 to AD-24" (line 110) and "nine files" (lines 517, 533). Memlog entry 92's direction to update it is not done yet.

### F5 [Low] AD-25 names C3 but not x4c or papermono

"C3 devices keep following upstream" leaves out x4c and papermono. Those are S3 boards that the fork also builds without `FREEINK_CAP_GAMES`.

**Fix:** replace it with "Every other board (C3, x4c, papermono) keeps following upstream."

### F6 [Low] The release workflow's ordering and concurrency are open

A release that becomes `latest` before both firmware files are attached makes `checkForUpdate()` return `NO_UPDATE`. Two dispatches running at once can both compute the same `N`. Upstream's `release.yml` guards the first case by checking the asset count before upload.

**Fix:** add to AD-25 bullet 5: "It runs in a single concurrency group and creates the tag and release only after both firmware images and every `.cpgame` are built, attaching them all in one step."

### F7 [Low] Prose and bookkeeping

- AD-3: "Row 10 used the 1 reserve slot (AD-25), so no reserve remains." This narrates history. Replace it with "The ledger has 10 rows and no reserve."
- AD-25 Binds: "all (release pipeline, `OtaUpdater`, first-party game assets)". The parenthetical lists artifacts, not units. Use "all", as AD-2 and AD-3 do, or name the units: first-party-games and package-install-launcher.
- The Operational envelope CI row still names only the ledger job. Add the flash-budget job and point to the release workflow (AD-25).
- The frontmatter `scope:` does not mention fork releases or the OTA source. Append "fork releases and OTA update source".
- The memlog has no closing "(event) Update applied to ARCHITECTURE-SPINE.md (AD-3 row 10, AD-25, …)" entry like entries 76 and 78.

## Checklist

| Criterion | Result |
| --- | --- |
| Divergence points fixed for the level below | Mostly. Version format, `N` source, comparison, update URL, asset naming, board scope, and the single release workflow are pinned. The parse anchor (F2), where the code lives and how it is tested (F3), and publish ordering (F6) are open. |
| Rules enforceable and prevent the stated divergence | Yes, except "disabled in GitHub Actions settings", which no CI job checks. That is acceptable: a release created with `GITHUB_TOKEN` does not trigger `release.yml` anyway. |
| Deferred cannot let units diverge | Yes. OTA for fork prereleases and fork C3 firmware are both fully excluded by active rules. |
| Ratifies the brownfield code | Yes, except F2. Checked: the `latestReleaseUrl` constant, `crosspoint-<tag><suffix>.bin` with the `x4pro` and `sticky` suffixes from `FirmwareBoardTag.cpp`, `tagName[32]` and `assetName[48]` fit `1.6.5-ch.NNNNN`, the tag has no leading `v` (matching `OtaUpdater`'s use of the raw tag), `release.yml` and `release_candidate.yml` are unedited, and the `[crosspoint] version` line stays upstream's. |
| Spec capabilities and Constraints covered | CAP-10's "each fork release" is now defined. The Constraint on the upstream-touch cap is stale (F1). |
| Memlog to spine, both directions | Entries 83 to 91 all landed. The spine adds nothing without a memlog basis. Entry 84's error propagated (F2), and entry 92 is pending (F4). |
| Internal consistency of counts and "reserve" | The spine is consistent. The spec, epic, tickets, initiative, and walkthrough are stale (F1, F4). |
| Terse, declarative prose | Mostly. See F7. |

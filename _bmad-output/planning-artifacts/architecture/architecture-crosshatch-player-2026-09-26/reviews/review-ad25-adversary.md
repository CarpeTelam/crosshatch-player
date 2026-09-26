# Adversarial review: AD-25 (fork releases and the update source)

Reviewed 2026-09-26 against `ARCHITECTURE-SPINE.md` (AD-2, AD-3 row 10, AD-15, AD-16, AD-17, AD-25, Operational envelope, Deferred), `src/network/OtaUpdater.cpp`, `lib/JsonParser/ReleaseJsonParser.h`, `src/network/FirmwareBoardTag.cpp`, `platformio.ini`, `scripts/git_branch.py`, `.github/workflows/*.yml`, the initiative, `epic-platform-baseline/`, `epic-first-party-games/`, and `reviews/review-ad25-rubric.md`.

Lens: for each hole, two units that each obey every AD to the letter and still produce an incompatible whole. Where the rubric review already raised a point (the `-x4pro` suffix, where the parser lives, the row-10 owner, concurrency), this review adds only the collision it misses and the Rule text that closes it.

## Verdict

**Not yet safe to build.** The shape of AD-25 is right: the fork's own source, `N` as the only thing compared, a single workflow, and upstream's `platformio.ini` version line left alone. But the rule has no end-to-end invariant tying *the string the firmware reports* to *the tag it was published under*. Every failure below comes from that gap. The worst one, a released image that reports no `-ch.N`, makes every device re-install the same release forever, and nothing in the spine would catch it. There are also three unguarded ways for `N` to be reused or skipped, and the ownership of the workflow is split with epic 8. Six Rule additions close all of this. They are consolidated at the end.

## Findings

### A1 [Critical] A release whose image doesn't carry its own `-ch.N` is an endless update loop

- **Unit A, the release workflow** (AD-25 bullet 5): "rewrites the version line in its own build checkout only". It does this with a `sed` on `^version = ` like upstream's `release.yml`, builds, tags `1.7.0-ch.12`, and publishes.
- **Unit B, a later upstream merge** (AD-3 lets upstream own `platformio.ini`): upstream moves the version to another section, renames the key, reads it from a file, or appends a suffix (`version = 1.7.0-dev` on `develop`). Any of these makes the `sed` a silent no-op, or yields `1.7.0-dev-ch.12`.
- **Unit C, the OTA parser** (AD-25 bullet 2): "A build with no `-ch.N` counts as `N = 0`."

**Result.** The image reports `1.7.0` (or an unparseable string), so its `N` is 0. A device installs `1.7.0-ch.12`, reboots, sees that 12 > 0, and is offered the same release again, forever. Every rule was obeyed. The same loop follows from any build of a fork tag that bypasses the rewrite. The two likeliest such builds are:

- a hand upload of a `pio run -e x4pro-gh_release` image;
- a re-enabled or renamed upstream `release.yml` (see A3).

**Proposed Rule** (AD-25, new bullet):

> One version grammar governs tags, images, and the parser: `^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-ch\.([1-9][0-9]{0,8})$`. A running version yields `N` only when its prefix up to the end of `-ch.<digits>` matches this grammar and the next character is the end of the string, `-`, or `+`. Otherwise `N = 0`. Before it creates any tag, the workflow must check three things:
> - upstream's `[crosspoint] version` matches `^\d+\.\d+\.\d+$`;
> - after the rewrite, `pio project config -e <env>` shows `CROSSPOINT_VERSION` equal to the tag for both release envs;
> - each built `firmware.bin` contains the tag as a NUL-terminated string.
>
> If any check fails, the run fails. Shared test vectors, run by both the workflow's parser and the firmware's, fix the grammar in one place, as the pack hash does in AD-16.

### A2 [High] A textually clean upstream merge quietly re-routes fork devices to upstream, or strands them

- **Unit A, the row-10 change**: a guarded `#if FREEINK_CAP_GAMES` swap of the `latestReleaseUrl` constant and of `isUpdateNewer()`'s body.
- **Unit B, a later upstream merge**. There are two cases:
  - **B1.** Upstream moves the URL into a new file or a settings-driven "update channel", or renames the constant. The fork's `#if` block merges cleanly but guards a dead constant, and fork x4pro/sticky images again check `crosspoint-reader/crosspoint-reader`. That is the exact failure AD-25 exists to prevent: a fork device updates itself to firmware without games.
  - **B2.** Upstream changes its asset naming, for example `crosspoint-<board>-<tag>.bin` or a `v` prefix, in `OtaUpdater.cpp` and `release.yml` together. The fork's workflow is a fork-only file, so it never merges that change and keeps the old names. Every fork device then gets `NO_UPDATE`, silently.

AD-3's ledger CI checks *which files* differ. It cannot see either failure.

**Proposed Rule** (AD-25, new bullet):

> The fork release URL, the asset-name function (`crosspoint-<tag>-<board>.bin`), and the `-ch.N` parse and compare live in one pure fork-only header, tested in `test/game_core` against the shared vectors. `OtaUpdater.cpp` calls it only inside the row-10 guards. Before publishing, the workflow checks each built image:
> - it contains `api.github.com/repos/CarpeTelam/crosshatch-player/releases/latest`;
> - it does not contain `repos/crosspoint-reader/crosspoint-reader/releases`.
>
> It names each asset with the same vectors the header is tested against. Any upstream merge that touches `OtaUpdater.*`, `ReleaseJsonParser.*`, `FirmwareBoardTag.*`, `release.yml`, or a `*-gh_release` env is not done until a dry run of the release workflow passes (build and checks, no tag).

### A3 [High] "Disabled in settings, not edited" does not survive a renamed upstream workflow

- **Unit A, the owner**: disables `release.yml` and `release_candidate.yml` in Actions settings, as AD-25 bullet 6 says.
- **Unit B, a later upstream merge**: renames `release.yml` (say to `release-firmware.yml`) or adds a second `on: release` workflow. GitHub keys a disabled workflow to its file, so the new file arrives **enabled**.
- **Unit C, the fork workflow**: creates the release with a PAT. AD-25 does not say which token to use.

**Result.** The new upstream workflow fires on the fork's `release: published`. It checks out the tag with the *unrewritten* version, builds all five boards, and runs `gh release upload --clobber` over `crosspoint-1.7.0-ch.12-x4pro.bin` with an image that reports `1.7.0`, which triggers A1's loop. Today, only upstream's "Validate release version" step (tag must equal `platformio.ini` version) prevents this. That step is upstream's to delete.

**Proposed Rule** (AD-25, replacing bullet 6):

> The fork workflow creates the tag and release with `GITHUB_TOKEN`, so no `release`-triggered workflow runs. Its first step lists the repository's active workflows. It fails if any active workflow other than itself triggers on `release`, or builds a `*-gh_release*` env. Upstream's `release.yml` and `release_candidate.yml` stay disabled in settings and are never edited.

### A4 [High] `N` can be reused, and "latest" can point at the wrong release

AD-25 says `N` is "the highest existing `-ch.N` tag plus one" and "never resets". Four pairs break that:

1. **Withdrawal × numbering.** The owner deletes the bad release `1.7.0-ch.9` *and its tag* to pull it. The next run computes `N = 9` again. Devices that already installed the bad ch.9 compare 9 > 9, get false, and are stuck on the bad image.
2. **Hand-made tags × the parser.** Nothing reserves the tag space. The fork already carries upstream tags `v1.5.0`, `1.6.0rc`, and `1.6.5rc` (from the fork's creation or upstream fetches). A hand tag such as `v1.6.5-ch.8` or `1.6.5-ch.08` is counted differently by the workflow's regex and the firmware's parser, which leads to either a duplicate `N` or a gap.
3. **Rollback × GitHub's "latest".** To roll back, the owner re-releases an older commit on an older base, say `1.6.5-ch.11` after `1.7.0-ch.10`. Unless the latest flag is set explicitly, GitHub may choose by date and semantic version. Under SemVer, `X.Y.Z-ch.N` is a *pre-release of X.Y.Z*, so `1.7.0-ch.10` can stay "latest" and the rollback is never offered. AD-25 also gives no way to build an older commit: `workflow_dispatch` runs on whatever branch was picked.
4. **Branch × release.** `workflow_dispatch` runs on any branch, so a feature branch can become the fleet's firmware.

Concurrency, where two dispatches both pick `N` (rubric F6), is the fifth path. And "increases by one" invites a builder to write a contiguity check, or release notes that diff against `N-1`, which a failed run will break.

**Proposed Rule** (AD-25, replacing bullet 5's numbering clause):

> `N` strictly increases across fork releases. Gaps are allowed; nothing may assume `N-1` exists. The workflow computes `N` as 1 + the largest integer following `-ch.` in **any** tag or release name in the repository, fetched through the API, not from a shallow checkout. It runs in a single concurrency group with `cancel-in-progress: false`. It pushes the tag without force and fails if the tag exists.
>
> A repository tag ruleset lets only the workflow create `*-ch.*` tags and forbids deleting them. A bad release is withdrawn by publishing `N+1` built from a good commit. Its tag is never deleted.
>
> The workflow builds `develop`'s HEAD, or a `ref` input that is an ancestor of `develop`, and fails on any other branch. It publishes with `make_latest=true`.
>
> No release is published in the fork except by this workflow.

### A5 [Medium] Prereleases have no numbering rule, and the `-rc` envs already carry the games flag

- **Unit A, AD-2**: gives `FREEINK_CAP_GAMES` to `x4pro-gh_release_rc` and `sticky-gh_release_rc`. Those envs report `${version}-rc+<hash>`.
- **Unit B, AD-25**: "One … workflow makes every fork release", and "Fork prereleases are never offered over the air". Deferred adds: "prereleases are installed by SD or web flasher". So prereleases exist, but no rule says who numbers them.
- A builder writes the prerelease path as tag `1.6.5-ch.8-rc`, and the numbering regex anchored with `$` ignores it. The next release is then also `ch.8`. A tester on `1.6.5-ch.8-rc+abc1234` parses `N = 8`, the release's 8 is not greater, and the tester is never moved onto the release. Another builder counts rc tags, and the two disagree.

**Proposed Rule** (AD-25, replacing bullet 4's prerelease clause):

> A fork prerelease comes from the same workflow with `prerelease=true`, takes a fresh `N` like a release, and is tagged `X.Y.Z-ch.N-rc`. It is built from the `-gh_release_rc` envs with the version line rewritten to `X.Y.Z-ch.N`, so it reports `X.Y.Z-ch.N-rc+<sha>`. It is published as a GitHub prerelease, never marked latest, and never offered over the air.

Alternatively, state outright: "v1 makes no fork prereleases; the `-gh_release_rc` envs are unused in the fork", and drop the Deferred line's reference to installing them.

### A6 [Medium] Two owners of the release workflow, and a second workflow that never fires

- **Unit A, AD-25**: one workflow makes every release: tag, release, firmware, and `.cpgame` files. It gives no owner.
- **Unit B, `epic-first-party-games`**: its Boundaries list "the fork-only release workflow", and Done-when 4 reads "A fork release has the three `.cpgame` files attached by a fork-only workflow". This epic comes **last** (epic 8), yet AD-25's firmware releases and row 10 are needed from the first release a fork device takes. A builder of epic 8 who finds a firmware-only workflow already in place can meet Done-when 4 with a second workflow, `on: release: published`, that attaches `.cpgame` files. Two things go wrong:
  - That breaks "one workflow".
  - Under A3's `GITHUB_TOKEN` rule the second workflow **never runs**, so releases ship without games, silently.

**Proposed Rule** (AD-25, new bullet, plus ticket edits):

> The release workflow and the row-10 change belong to epic-platform-baseline. From its first run, the workflow packs and attaches every `games/<id>/` present, and zero games is valid. epic-first-party-games adds only `games/<id>/` sources and adds no workflow.

Change epic-first-party-games' Boundaries and Done-when 4 to match. This goes beyond the rubric's F4, which named an owner but not the second-workflow trap.

### A7 [Medium] Release-time packing can change every game's package hash on every release

- **Unit A, the release workflow**: "attaches every first-party `.cpgame`". A reasonable builder stamps the release into each package, for example setting `manifest.version` to the tag, adding a build note, or regenerating `icon.png`.
- **Unit B, AD-16/AD-17/AD-13**: the package hash covers every member's uncompressed bytes. AD-17 discards a save whose hash differs, and AD-13 refuses a match between different hashes.

**Result.** Each fork release changes the hash of all three games even when no game changed. So re-installing from the new release wipes every resume save, and two devices whose inbox copies come from different releases cannot play Nearby. Both effects follow from rules obeyed to the letter. A related gap: a `games/<id>/` that fails `pack_game.py` validation has no defined effect on the release.

**Proposed Rule** (AD-25 or AD-16, new bullet):

> The workflow packs each `games/<id>/` at the released commit with `scripts/pack_game.py`, byte-for-byte. Neither the workflow nor the packer writes generated or release-specific content into a member. `manifest.version` is edited by hand, in source, only when the game changes. Assets are named `<id>.cpgame`. The release notes list each package hash. A package that fails validation fails the run before any tag is created.

### A8 [Medium] The update source is keyed to the *games* flag, but the release matrix is hard-coded

- **Unit A, a later AD-2 change**: enables games on another board, for example `papermono`, which is an S3 touch board, by adding `FREEINK_CAP_GAMES` to its envs. That is one env-scoped edit within ledger row 1.
- **Unit B, AD-25**: "The fork releases only x4pro and sticky firmware".

**Result.** papermono images now check the fork's releases, where there is never a `-papermono.bin`. They get `NO_UPDATE` forever and have also left upstream's update path. The reverse also happens: a fork build that turns games off, such as the flash-budget job's "off" image or a future games-less S3 variant, silently becomes an upstream updater. If it is ever flashed to a device, that device leaves the fork on its next check.

**Proposed Rule** (AD-25, replacing bullet 4's first sentence):

> The fork releases exactly the `*-gh_release` envs that set `FREEINK_CAP_GAMES=1`. The workflow derives its matrix from `pio project config` and fails if a flagged env has no board suffix. Adding the flag to an env is an AD-2 and AD-25 change together.

Also state that the flash-budget job's "off" image is never published.

### A9 [Low] The first move and the way out are unspecified

- **User on upstream firmware.** The envelope says the first move is "by SD or web flasher". Upstream's web flasher serves upstream releases, and the fork has none. Specify: "SD, from a fork release asset; there is no fork web flasher in v1." Alternatively, add a fork web flasher to Deferred.
- **Leaving the fork.** A fork device never sees upstream releases. Going back to upstream means flashing an upstream image by SD. Say so, because it is the only path.
- **Before the first fork release**, `releases/latest` returns 404. `fetchUrl` fails, and the device shows `HTTP_ERROR` on every check. The guarded code should map a 404 to `NO_UPDATE`, or the first release should precede the first fork image in users' hands.
- **Dev builds.** A dev x4pro build reports `1.6.5-x4pro` (the dev env suffix; the release env has none, per rubric F2), and a dev sticky build reports `1.6.5-dev-<branch>-<sha>`. Under A1's grammar both are correctly `N = 0`, including a branch named `feat-ch.3`. That the device is always offered the latest release is intended; state it so nobody "fixes" it.
- **Length limit.** `ReleaseJsonParser::tagName[32]` and `OtaUpdater`'s `assetName[48]` cap the tag at 25 characters (`crosspoint-` + tag + `-sticky.bin`). The grammar in A1 fits, but note the limit beside the grammar.

## Silent decisions a builder must make today

1. How `N` is parsed: anchored where, what may follow it, and whether leading zeros are allowed. Closed by A1.
2. Where the runtime string comes from. It is `CROSSPOINT_VERSION`, via the `[crosspoint] version` line in the release envs. AD-25 should say so, and forbid a second `-DCROSSPOINT_VERSION` passed through `PLATFORMIO_BUILD_FLAGS`, whose precedence depends on flag order.
3. Where the parse and compare code lives, and its host test. Rubric F3; A2 extends it with the URL and asset name.
4. Whether `OtaUpdater.h` may change. Row 10 lists only `.cpp`, and no reserve is left. State "no header change; new state goes in the fork header".
5. Which token creates the release. Closed by A3.
6. Whether prereleases consume `N`. Closed by A5.
7. Which ref is released, and how a rollback is built. Closed by A4.
8. What happens to a release that fails partway: tag pushed, assets missing. Build and check everything first, then create a draft release, upload all assets, and publish last with `make_latest=true`. A run that fails after pushing the tag leaves a gap, which A4 permits.
9. Whether a `games/` directory holding a work-in-progress game ships. As written, "every first-party `.cpgame`" means every `games/*/` directory.

## Consolidated AD-25 Rule (proposed)

- A fork version is `<upstream X.Y.Z>-ch.<N>`. Tags, images, and the parser share one grammar: `^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)-ch\.([1-9]\d{0,8})$`, with shared test vectors.
  - A running version yields `N` only when its prefix matches that grammar and is followed by the end of the string, `-`, or `+`. Otherwise `N = 0`.
  - Release images report exactly the tag, with no board suffix. Prerelease images report `<tag>-rc+<sha>`.
  - Assets are `crosspoint-<tag>-<board>.bin` and `<id>.cpgame`. Tags are at most 25 characters.
- `N` strictly increases across every fork release and prerelease and never resets. Gaps are allowed. Only `N` decides "newer", and dev builds are `N = 0` by design.
- One pure fork-only header holds the release URL, the asset-name function, and the parse and compare. It is tested in `test/game_core`. `OtaUpdater.cpp` calls it only inside the row-10 `#if FREEINK_CAP_GAMES` guards, and `OtaUpdater.h` is unchanged.
- The fork releases exactly the `*-gh_release` envs that set `FREEINK_CAP_GAMES=1`. Every other board follows upstream.
- One fork-only `workflow_dispatch` workflow, owned by epic-platform-baseline, makes every fork release and prerelease. It:
  - runs in one concurrency group, on `develop` HEAD or an ancestor `ref`;
  - fails if another active workflow triggers on `release` or builds a `*-gh_release*` env;
  - computes `N` from all tags through the API;
  - checks the upstream version line, rewrites it in its checkout only, and builds;
  - verifies that each image embeds its tag and the fork URL and not upstream's;
  - packs `games/*/` byte-for-byte;
  - then pushes the tag without force, creates a draft release with `GITHUB_TOKEN`, uploads every asset, and publishes with `make_latest=true`.
- `*-ch.*` tags are ruleset-protected: only the workflow creates them, and nobody deletes them. A bad release is withdrawn by releasing `N+1`. No release is published in the fork by hand.
- Upstream's `release.yml` and `release_candidate.yml` stay disabled in settings and are never edited. An upstream merge touching the OTA, parser, board-tag, release workflow, or `*-gh_release` files is done only after a dry run of the fork workflow passes.

Also update the Operational envelope's "Firmware delivery" row (SD from a fork asset is the only first move; leaving the fork is by SD) and add "fork web flasher" to Deferred.

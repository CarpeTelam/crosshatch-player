# Review: AD-25 reality check (verify lens)

Scope: AD-25, AD-3 ledger row 10, Operational envelope rows "Firmware delivery" and "Game delivery", the Deferred row on OTA fork prereleases and fork C3 firmware, and the Capability map row. Checked against `src/network/OtaUpdater.cpp`, `src/network/HttpDownloader.cpp`, `src/network/FirmwareBoardTag.*`, `lib/JsonParser/ReleaseJsonParser.*`, `platformio.ini`, `.github/workflows/release.yml`, `release_candidate.yml`, `release-fonts.yml`, `scripts/git_branch.py`, and GitHub documentation.

**Verdict:** mostly sound. The mechanism works. One committed claim is false (the x4pro version string), and three rules need sharper wording before implementation: how `N` is parsed, how the workflow numbers and publishes releases, and what disabling a workflow actually protects against.

---

## Findings

### F1 (high): "the x4pro firmware reports `1.6.5-ch.7-x4pro`" is false for release builds

`platformio.ini:405` (`x4pro-gh_release`) sets `-DCROSSPOINT_VERSION=\"${crosspoint.version}\"` with no board suffix. So does `sticky-gh_release` (line 290). Only the development env `x4pro` (line 326) appends `-x4pro`. If the workflow rewrites `version = 1.6.5-ch.7`, then **both** release images report exactly `1.6.5-ch.7`. The claim goes back to a user decision in `.memlog.md:86` that assumed a suffix these envs do not have.

Other version strings, for reference:
- dev `x4pro`: `1.6.5-x4pro`
- dev `sticky` (from `git_branch.py`): `1.6.5-dev-<branch>-<sha>`
- `*-gh_release_rc`: `1.6.5-rc+<hash>`

None of these contains `-ch.`, so all of them correctly parse as `N = 0`.

Nothing breaks because of this. The asset suffix comes from `board_tag::boardName()` (`OtaUpdater.cpp:37-42`), not from the version string, and the tag equality check (`latestVersion == CROSSPOINT_VERSION`) works better without a suffix.

**Fix:** replace the clause with "both x4pro and sticky release firmware report the tag itself (`1.6.5-ch.7`); the board comes from the board tag, not the version". Tell the user the decision in `.memlog.md:86` rested on this assumption. Giving x4pro a suffix would mean editing an upstream env line or rewriting a second line in the workflow, and neither is worth it.

### F2 (medium): the `N` parse is unspecified, and an unanchored parse can misread dev builds

"A build with no `-ch.N` counts as `N = 0`" is right in intent. But dev sticky builds embed the git branch name (`1.6.5-dev-<branch>-<sha>`), so a naive `strstr(v, "-ch.")` would read `N` from a branch named, say, `fix-ch.3-x`.

**Fix:** state the grammar. `N` is parsed only as `^\d+\.\d+\.\d+-ch\.(\d+)$` (for example `sscanf("%d.%d.%d-ch.%d%n")`, then require that the whole string was consumed). Anything else is `N = 0`, on both the running and the latest side. Add a host GoogleTest next to `test/release_json_parser/` covering `1.6.5-ch.7`, `1.6.5`, `1.6.5-x4pro`, `1.6.5-rc+abc1234`, `1.6.5-dev-fix-ch.3-abc`, and `v1.6.5-ch.7`.

### F3 (medium): the release workflow's numbering and publishing steps are under-specified

- **Tags are not fetched by default.** `actions/checkout` fetches no tags. "Highest existing `-ch.N` tag" needs `git fetch --tags`, `git ls-remote --tags`, or `gh release list`, and it should read from the fork remote only.
- **Two concurrent dispatches pick the same `N`.** Nothing stops this today. **Fix:** add `concurrency: { group: fork-release, cancel-in-progress: false }`.
- **Publish order.** Create the release as a draft, upload all assets, then publish. `gh release create <tag> <files...>` already does this: the release stays a draft until the assets are uploaded ([gh manual](https://cli.github.com/manual/gh_release_create)). Otherwise a device polling in the gap sees a release without its asset and gets `NO_UPDATE`. That failure is harmless but confusing.
- **Mark the release latest explicitly.** Pass `--latest` / `make_latest: true` (see F4).
- **Never prefix tags with `v`.** `release.yml` strips a leading `v` when it names assets, but `OtaUpdater` builds the asset name from the raw `tag_name`. With a `v` tag the device would look for `crosspoint-v1.6.5-ch.7-x4pro.bin`, which does not exist.
- **Prereleases.** AD-25 says "Fork prereleases are never offered over the air", and the Deferred table says prereleases are installed by SD. Neither says whether the single workflow can make a prerelease, or whether a prerelease consumes an `N`. State one or the other.

### F4 (low): how GitHub picks "latest" (question 1)

The REST docs say `GET /repos/{o}/{r}/releases/latest` returns "the most recent non-prerelease, non-draft release, sorted by the `created_at` attribute" ([docs](https://docs.github.com/en/rest/releases/releases)). Separately, `make_latest` (`true` / `false` / `legacy`) defaults to `true` for newly published releases, and a maintainer can re-flag a release as Latest by hand. The docs contradict each other on this point ([github/docs#23508](https://github.com/github/docs/issues/23508)). In practice the endpoint follows the Latest flag.

Consequences for AD-25:
- **Prereleases and drafts are excluded.** "Fork prereleases are never offered" is therefore correct by construction.
- **A manual re-flag cannot cause a downgrade.** If someone marks an older release Latest, devices see a smaller `N` and are offered nothing. Only `N` decides, and that holds. It also means OTA has no rollback path, which is fine but worth a sentence.
- **A non-firmware full release strands OTA.** If anyone publishes a full release without firmware (for example a games-only one), it becomes Latest. Devices then get `NO_UPDATE` until the next workflow release. AD-25's rule that one workflow makes every fork release covers this. Add "and marks it latest" to the rule.

### F5 (low): the `+` rationale is right in outcome but imprecise; the hyphen form is clean (question 2)

The hyphen form works:
- `1.6.5-ch.7` and `crosspoint-1.6.5-ch.7-x4pro.bin` contain only `[0-9a-z.-]`, so `browser_download_url` needs no escaping.
- The asset name is 31 chars (sticky: 32). It fits `assetName[48]`, `currentAssetName[48]`, and `firmwareAssetName[48]`.
- The tag fits `tagName[32]`: up to 31 chars, so even `10.20.30-ch.123456` fits.
- The parser matches `name` exactly and takes `browser_download_url` as-is.

The real problem with `+` is not URL-encoding in general. The asset-upload API takes the file name in a query string, where an unencoded `+` decodes to a space, and GitHub then rewrites the space to `.` ([cli/cli#10585](https://github.com/cli/cli/issues/10585), [rest-api-description#2968](https://github.com/github/rest-api-description/issues/2968)). The stored name would then no longer equal the `crosspoint-<tag><suffix>.bin` that the device builds, and OTA would silently find no asset. A `+` in the tag would also appear as `%2B` in download URLs.

**Fix (optional):** reword the rationale to "`+` is rewritten in uploaded asset names, which breaks the device's exact asset-name match".

### F6 (low): what disabling `release.yml` protects against, and how to keep it disabled (question 3)

- **Release events do fire in a fork.** Once Actions is enabled, the fork's own workflows run on `release: published` events in the fork. Only *scheduled* workflows are disabled by default in forks ([docs](https://docs.github.com/en/actions/managing-workflow-runs-and-deployments/managing-workflow-runs/disabling-and-enabling-a-workflow)).
- **The fork workflow's own releases never trigger `release.yml`.** A release created with `GITHUB_TOKEN` does not start workflow runs; only `workflow_dispatch` and `repository_dispatch` are exempt ([docs](https://docs.github.com/en/actions/writing-workflows/choosing-when-your-workflow-runs/triggering-a-workflow)). So disabling `release.yml` only guards against releases a person publishes by hand, or releases created with a PAT.
- **It would fail anyway.** Its "Validate release version" step (`test "$version" = "$configured_version"`) fails for any `-ch.N` tag, so even if it fired it would stop before attaching anything.
- **Where to disable.** It is done per workflow from the **Actions tab** (workflow, then the `...` menu, then Disable), not from Settings > Actions. The disabled state belongs to the workflow's record, which is keyed by file path. Upstream edits merged into `release.yml` keep it disabled. If upstream **renames or adds** a release workflow, the new path is a new workflow and starts enabled.
- **`release_candidate.yml` never runs by itself.** It is `workflow_dispatch`-only, so disabling it is hygiene only.

**Fix:** reword to "disabled from the Actions tab (per-workflow, survives edits; a renamed or new upstream workflow must be disabled again)". Add a line to the upstream-merge checklist to look for new files under `.github/workflows/`.

### F7 (low): HttpDownloader needs no change for another repo, but it does not verify certificates (question 5)

`fetchUrl` → `runGetSecure` has nothing specific to one repo. It uses the same GitHub hosts (api.github.com, then github.com, then the release-asset CDN) and follows redirects manually on the wolfSSL path, up to `MAX_REDIRECTS`. Changing only the URL constant is sufficient.

However, `FREEINK_NET_WOLFSSL=1` is set in `[base]` (`platformio.ini:72`), and `runGetWolf` calls `http.setInsecure()` (`HttpDownloader.cpp:75`). So OTA currently downloads firmware without certificate verification on every env. This predates the fork and is not AD-25's doing. But "fork devices update over the air" inherits it.

**Fix:** none required for AD-25. Optionally add a Deferred row or an upstream issue for certificate-pinned OTA.

### F8 (low): "C3 devices keep following upstream" is too narrow

Every build without `FREEINK_CAP_GAMES` keeps upstream's source and comparison. That includes the fork's `x4c` and `papermono` S3 builds, not only C3.

**Fix:** "Boards without games (C3 `default`, `x4c`, `papermono`) keep following upstream."

### F9 (low): other checks, which passed

- **Rewriting the version in the checkout works (question 4).** Changing `[crosspoint] version` in the checkout propagates through `${crosspoint.version}` to both release envs. `git_branch.py` only injects into `default` and `sticky` dev envs, so it does not interfere. No other script reads the line.
- **Game delivery.** Attaching `.cpgame` files next to the firmware is harmless to the parser, which matches exact names and streams, so extra assets add only JSON bytes.
- **First move to the fork.** "The first move from upstream firmware to the fork is by SD or web flasher" is correct, because upstream firmware hard-codes `crosspoint-reader/crosspoint-reader` (`OtaUpdater.cpp:22`).
- **Web flasher.** Which web flasher accepts an arbitrary fork `.bin` was not verified here. Name it in `docs/crosshatch/` when the release epic lands.
- **Ledger row 10.** Row 10 (`OtaUpdater.cpp`, guarded) is consistent with AD-3's rule that "Row 10 used the 1 reserve slot". The change fits in one `#if FREEINK_CAP_GAMES` block around `latestReleaseUrl` and `isUpdateNewer`.

---

## Question-by-question summary

| # | Question | Result |
| --- | --- | --- |
| 1 | `releases/latest` semantics | Returns the newest non-draft, non-prerelease release flagged Latest (default `make_latest: true`). A manual re-flag changes it but cannot cause a downgrade under N-only comparison. See F4. |
| 2 | Hyphen tag and asset name | Clean in URLs, the parser, and the buffers. `+` really is a hazard, because GitHub rewrites it in uploaded asset names. See F5. |
| 3 | Fork workflows and disabling | Fork workflows fire on `release: published`, but not for releases made with `GITHUB_TOKEN`. Disabling is per workflow from the Actions tab and survives edits but not renames. See F6. |
| 4 | Version rewrite | Works. Both release envs produce the bare `<new>` string with no `-x4pro`. The N parse must not depend on a suffix and must be anchored. See F1 and F2. |
| 5 | TLS and redirects | Unchanged and repo-agnostic. The wolfSSL path does not verify certificates, which is pre-existing. See F7. |

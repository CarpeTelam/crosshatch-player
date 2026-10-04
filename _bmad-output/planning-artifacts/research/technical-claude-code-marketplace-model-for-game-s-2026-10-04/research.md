---
title: 'technical research: Claude Code marketplace model for the crosshatch game store'
type: 'technical'
topic: 'Claude Code marketplace model for the crosshatch game store'
decision: 'Whether to model the post-v1 crosshatch game store on the Claude Code plugin-marketplace pattern, and what to adapt for an ESP32 device'
source: 'native run'
status: complete
claims_verified: 8
claims_unverified: 5
claims_overturned: 0
sources: 30
preset: 'standard'
validation: 'normal'
created: '2026-10-04'
updated: '2026-10-04'
---

# Technical research: Claude Code marketplace model for the crosshatch game store

**Decision this research serves:** Whether to model the post-v1 crosshatch game store on the Claude Code plugin-marketplace pattern, and what to adapt for an ESP32 device

Numbers in brackets cite external sources. B and P labels cite internal project documents, which are design facts, not research evidence. The source appendix lists both.

## Executive summary

**Recommendation.** Yes, model the store on the Claude Code marketplace, but only on its git-free subset, and add three things it lacks.

**What to copy.** Claude Code can already run a marketplace with no git client:
- The catalog is a single `marketplace.json` fetched from an HTTPS URL [1].
- Each entry is an `archive` source: a zip URL plus a `sha256` pin, and the client refuses a download that does not match [1].
- The installed version is the explicit `version` if one is set, otherwise a prefix of that `sha256`. An update happens only when the computed version changes [2].

That maps almost one to one onto a `.chgame` catalog whose entries carry a URL, a size, and a SHA-256, with update detection by package hash. Anyone can host a catalog, users add catalogs by URL, and each catalog has a unique name. Those parts carry over too.

**What to add.**
1. **A signed catalog.** Claude Code does not sign catalogs or packages; it relies on HTTPS plus hash pins [1][5]. That is not enough for us. On our game boards the wolfSSL transport replaces the certificate-checking path entirely, so every HTTPS fetch is unverified (B9). Anyone on the network could swap the catalog, and with it the hashes. F-Droid's design fixes this: a small signed root pins the hash and size of everything else, and a timestamp and an expiry date prevent rollback and stale copies [19][20][29]. A third-party catalog is added by URL plus key fingerprint [22].
2. **API-level gating in each entry.** Claude Code catalogs say nothing about which client version an entry needs. Flipper Zero, the closest device analogue, requires each app's major API version to match the firmware's, and its catalog keeps one build per app version and API version [24][25]. F-Droid gates on integer `minSdkVersion` [21]. Our integer `api` level slots straight in (B2 AD-19).
3. **No automatic install.** Claude Code leaves auto-update off for every marketplace except Anthropic's official ones and those added from claude.ai, and a catalog cannot turn it on [2][3]. Its zero-click exploit came through background auto-update [14]. On a battery-powered device, show "Update available" and install only when the player asks.

**What to drop:** the items in "Do not adopt" in section 8. They are git, npm, and command sources; plugin dependencies; merging manifests with catalog entries; a version cache; background auto-update; and enterprise allowlists.

**Biggest caveat.** Whether the shipped mbedTLS or wolfSSL build exposes Ed25519 or ECDSA P-256 signature verification to application code was not checked. A short spike must confirm it before we commit to signing.

## 1. How the Claude Code marketplace works

**Takeaway.** A marketplace is one JSON file that lists plugins and where to fetch each one. The client works out a version for each plugin, caches by that version, and updates when the version changes. Hash pins are the only integrity mechanism.

**The catalog.**
- The file is `.claude-plugin/marketplace.json`. It requires `name`, `owner.name`, and `plugins[]`. Each entry requires `name` and `source`; `description`, `version`, `category`, `tags`, `displayName`, and a free-form `metadata` are optional [1].
- Each entry is validated on its own, and unknown keys are ignored at load time [1].
- Lifecycle fields:
  - `renames`, an append-only map from old names to new, where `null` means removed [1][3].
  - `forceRemoveDeletedPlugins`, which uninstalls delisted plugins and shows them under "Flagged" [3].
- A user can register only one marketplace per name. Many official-looking names are reserved and accepted only from `github.com/anthropics/` sources [1][5].
- A plugin is installed as `plugin@marketplace` [1][2].

**Where plugins come from.**
- A plugin's source can be a relative path, `github`, a git `url`, `git-subdir`, `npm`, `archive`, or `command` [1].
- Git sources can pin a full 40-character `sha`; when both a `sha` and a `ref` are set, the `sha` wins [1].
- The `archive` source arrived in v2.1.224 on 2026-08-07 [9][10]. It takes an HTTPS zip and an optional `sha256`, and refuses a download that does not match [1].
- Archive downloads and extraction have fixed caps on size, time, redirects, entry count, and compression ratio [1][3]. Our own package limits are far smaller (B1).

**Where the catalog comes from.**
- A marketplace can itself be a plain HTTPS URL to a `marketplace.json` [1][3].
- When the marketplace is a URL, the client fetches only the JSON (up to 5 MiB), so every entry needs a source it can fetch on its own [1][3].
- Users add one with `/plugin marketplace add <owner/repo | git URL | path | https URL>` [7].

**Versions, cache, and updates.**
- The version is resolved in order: the plugin manifest's `version`, then the catalog entry's `version`, then a value derived from the source. For an archive that is the first 12 characters of the SHA-256 [2].
- Each version is cached in `cache/<marketplace>/<plugin>/<version>/`. Per-plugin data in `data/` survives updates, and old versions are swept after 14 days [2].
- Auto-update defaults are set per marketplace [2]:
  - on for Anthropic's official marketplaces, except `knowledge-work-plugins` and `first-party-plugins`
  - on for marketplaces added from claude.ai
  - off for every other marketplace

  A catalog cannot turn auto-update on itself [3].
- Claude Code has no release channels. The documented workaround is two catalogs pointing at different refs [3].

**Trust.**
- Installing shows the same warning for every source: Anthropic "cannot verify that they will work as intended or that they won't change" [5].
- Admins get allowlists and blocklists of marketplaces (`strictKnownMarketplaces`, `blockedMarketplaces`) and can force plugins on or off [6].
- Anthropic does not review third-party marketplaces [8].
- No signing of catalogs or packages is described anywhere in the docs [1][2][5] (confidence: medium, an absence finding).

**Validation.** `claude plugin validate [--strict] [--json]` checks a plugin or marketplace on disk [4]. Errors fetching a source appear only after install, not in validation [1].

**The official catalog in practice.** It lists 315 plugins. All 262 entries with an external source pin a 40-character `sha`, and 97 of them also set a `ref` [11] (confidence: medium, unverified).

## 2. How it holds up in practice

**Takeaway.** The format is sound, but the client has needed constant repair for a year. Much of the trouble is in caching, updates, and the state files. That is exactly where a device store would also be fragile.

**Rapid change.**
- Marketplaces shipped in v2.0.12 on 2025-10-09 [9][10].
- The CHANGELOG has more than 200 marketplace and plugin-update entries since then. Cache and update fixes are still landing in the latest releases, up to v2.1.289 on 2026-10-03 [9][10].

**Stale caches and missed updates** are the largest cluster of problems.
- Issues #14061 ("/plugin update does not invalidate plugin cache") and #17361 are still open, despite partial fixes up to 2.1.289 [12][9] (confidence: medium, unverified).
- A plugin that sets an explicit `version` ships no update to users until the author bumps it, because the cache is keyed by version [2][5].

**Schema drift.** Older clients failed to load the entire official catalog because one entry used a source type they did not know (#33172). Since 2.1.120 the client shows that entry and asks the user to update instead [13][9].

**State corruption.** These were all fixed between April and September 2026 [9]:
- concurrent writes at startup silently unregistered a marketplace
- a failed fetch deleted the local copy of a GitHub marketplace named after its repo
- an auto-update left the official catalog broken while files were held open

**Name collisions.**
- `marketplace add` silently replaced a marketplace with the same name. Since 2.1.284 it says so and how to undo it, but it still replaces [9].
- IDs differing only in punctuation or case collided until 2.1.285 [9].

**Security.**
- Plugin4Shell, disclosed 2026-09-17, defeated commit pinning: an attacker made a branch named after the pinned SHA. Background auto-update made the attack zero-click for every marketplace with auto-update on. Claude Code patched it in 2.1.179 [14][15][2].
- Prompt Security published a proof of concept in which a marketplace skill hijacked dependency installs [16].
- No confirmed malicious plugin was found in the wild (confidence: medium, an absence finding).

**Ecosystem and practice.**
- An aggregator claims more than 2,700 marketplaces [17] (confidence: medium, self-reported).
- A practitioner advises pinning commit SHAs rather than tags, keeping the catalog repo separate from plugin source repos, and running stable and canary as distinct pinned SHAs [18] (confidence: medium, prescriptive rather than retrospective).
- Publishing guides recommend having CI regenerate the catalog from each package's manifest, so the two never drift [30] (confidence: low, read through a search summary).

## 3. Prior art from other package catalogs

**Takeaway.** Catalogs built for clients without git share three habits. They sign a small root that pins the hashes of everything else, gate packages on an integer compatibility level, and keep the top-level index small.

**F-Droid.**
- Its live `entry.json` is signed with both a JAR signature and a GPG signature [19][20]. It holds a `timestamp`, `maxAge: 14`, the `sha256` and `size` of the full index, and a list of diffs.
- The full index is 62.6 MB for 4,525 packages, far too large for a microcontroller to hold. The small signed entry point is the part a device could use [20].
- Each package version in the index carries `file{name, sha256, size}` and `usesSdk{minSdkVersion, targetSdkVersion}` [21].
- A third-party repo is added by URL plus key fingerprint, as one string: `https://host/repo?fingerprint=<sha256>` [22].
- A documented failure: a fingerprint left in the stored repo URL was not caught when the repo was added, and verification failed only later, at install [22]. The lesson is to verify the key when a source is added.

**Flipper Zero.** The closest device analogue.
- Authors submit by pull request. The manifest pins a source `commit_sha`, and each `id` must be globally unique [23]. Versions must strictly increase [23] (confidence: medium, unverified).
- The catalog builds every package itself [23].
- The device's App Loader requires the app's major API version to equal the firmware's [25]. The catalog keeps one build per app version, API version, and target [24].

**Homebrew taps.** The git analogue of a Claude Code marketplace.
- `brew tap user/repo` clones a git repo. A formula's short name resolves to Homebrew core, and `user/repo/formula` selects a tap's copy [26].
- Since Homebrew 6.0.0, a non-official tap needs explicit `brew trust` before it loads [26] (confidence: medium, the version date is unverified). That is the git-world version of adding a catalog by fingerprint.
- Prebuilt binaries pin one `sha256` per platform. On a mismatch, Homebrew falls back to building from source [27].

**MicroPython mip.** An installer that runs on a microcontroller.
- The ABI version is part of the index path, `package/{mpy_version}/{name}/{version}.json`, and files are content-addressed [28].
- It appears to use the hash only to skip files it already has, not to verify a fresh download [28] (confidence: medium). That is the counterexample: verify each file after downloading it.

**TUF.** The Update Framework is the formal model. Its spec, v1.0.36 (2026-08-05), chains timestamp → snapshot → targets metadata [29]:
- expiry stops a stale ("freeze") attack
- versions that only increase stop rollback
- length limits stop endless downloads
- hash matches stop substituted files

F-Droid's signed `entry.json` is a lightweight form of the same idea.

## 4. Our baseline (internal, not research evidence)

From the project documents labeled B and P in the source appendix. These are design facts, not research claims.

- **Packages.** A `.chgame` is one zip of at most 256 KB, with a whitelist of members. The installer reads only the SD inbox `/games/` and stages each install. It writes a `.pkg` file as its commit marker; the file holds the first 8 bytes of a SHA-256 hash of the members (B1, B2 AD-15, AD-16).
- **API level.** Each game declares an integer `api` level. `Manifest::check` decides whether this firmware can start it (B2 AD-19).
- **Parsing.** The device has a streaming JSON parser with a 512-byte token buffer, so it can parse a catalog streamed from the SD card without holding it in RAM (B7).
- **Hashing.** The device computes SHA-256 with mbedTLS already (B8).
- **TLS.** With `FREEINK_NET_WOLFSSL` on, which `[base]` sets, only the wolfSSL transport is compiled, and it calls `setInsecure()`. The certificate-verifying `esp_http_client` path is compiled out, so no HTTPS fetch on our game boards verifies the server today (B9, B6).
- **Earlier recommendation.** P1 proposed a "Get games" browser: download a `.chgame` from a catalog, verify its SHA-256, and put it in the inbox.

## 5. Mapping: Claude Code concept to crosshatch

**Takeaway.** Of 17 Claude Code concepts, 7 carry over as they are, 6 need adapting, 3 are dropped, and 1 is added. Signing is the one change our hardware forces (B9).

| Claude Code | Crosshatch store | Verdict |
|---|---|---|
| `marketplace.json` with `name`, `owner`, entry list [1] | `catalog.json` with `name`, `owner`, `games[]` | Adopt |
| `url` marketplace source: one JSON over HTTPS [1][3] | The device fetches the catalog by URL, streams it to SD, and parses it from there (B7) | Adopt |
| `archive` entry: `url` plus `sha256` [1] | Each build carries `url`, `size`, `sha256`, and the 8-byte package hash | Adopt |
| Relative, git, `git-subdir`, `npm`, and `command` sources [1] | None; the device has no git client and runs nothing outside the sandbox | Drop |
| Computed version: explicit `version`, else hash prefix [2] | "Update available" when the catalog's package hash differs from the installed `.pkg` (B1) | Adapt |
| `plugin@marketplace` IDs; one marketplace per name [1] | A game's `id` stays global on the device (`/.games/<id>/`). The device records which catalog installed a game, and installing the same ID from a different catalog asks first | Adapt |
| Reserved official names, accepted only from Anthropic's sources [1][5] | The official catalog name is accepted only with the built-in key | Adopt |
| HTTPS plus hash pins, no signing [1][5] | Detached signature over `catalog.json`, plus a monotonic `version` and an `expires` time [19][20][29] | Adapt, required by B9 |
| Add a marketplace by source with a generic trust warning [5][7] | Add a catalog by URL plus key fingerprint, and check the key when the catalog is added [22][26] | Adapt |
| Auto-update defaults per marketplace [2][3] | No automatic install; show "Update available" and install on request | Adapt |
| Two catalogs for stable and canary [3] | A "preview" catalog for games on a preview API level beside the stable one (B2 AD-19) | Adopt |
| No client-compatibility field | Each build carries `api`; the device hides any build `Manifest::check` would refuse [21][24][25] | Add |
| Per-entry validation; an unknown source shows a prompt instead of failing the catalog [1][13] | Validate each entry on its own; skip an unknown field or source and list the entry as "needs newer firmware" | Adopt |
| `renames`, `forceRemoveDeletedPlugins`, "Flagged" [1][3] | A game in the `delisted` list shows "no longer listed" but is never removed, because saves live under its ID (B1). No renames: the ID keys the saves | Adapt |
| Version cache with a 14-day sweep [2] | None; the installer keeps one copy and saves live elsewhere (B1) | Drop |
| `claude plugin validate` [4]; CI regenerates the catalog [30]; a catalog that builds its own packages [23] | `pack_game.py` plus a catalog build-and-sign step in CI, which generates `catalog.json` from the packed `.chgame` files | Adopt |
| Allowlists and blocklists for admins [6] | Not needed on a personal device | Drop |

## 6. Sketch: catalog and device flow

This sketch is the proposal the research points to. It is not a decided design.

**Catalog** (`catalog.json`, with a detached `catalog.json.sig` over its exact bytes):

```json
{
  "format": 1,
  "name": "crosshatch-official",
  "owner": {"name": "CarpeTelam"},
  "version": 42,
  "generated": "2026-11-01T00:00:00Z",
  "expires": "2026-11-29T00:00:00Z",
  "games": [
    {
      "id": "dots-and-boxes",
      "name": "Dots and Boxes",
      "author": "CarpeTelam",
      "description": "Claim the most boxes.",
      "version": "1.2",
      "icon": "grid-four",
      "modes": ["solo", "pass", "nearby"],
      "seats": {"min": 2, "max": 2},
      "builds": [
        {"api": 1, "url": "https://github.com/CarpeTelam/crosshatch-games/releases/download/dots-and-boxes-1.2/dots-and-boxes-api1.chgame",
         "size": 48213, "sha256": "<64 hex>", "pkg": "<16 hex>"}
      ]
    }
  ],
  "delisted": []
}
```

`id`, `name`, `version`, `icon`, `modes`, and `seats` mirror existing manifest keys (B2 AD-15). `author` and `description` are new (recommendation 5a). `pkg` is the 8-byte package hash from section 4.

**Device flow:**
1. Download `catalog.json` and `catalog.json.sig` to a temp folder on SD.
2. Hash the file as it is read back from SD, then verify the signature with the catalog's key: built in for the official catalog, or stored when a third-party catalog was added.
3. Refuse the catalog if its `version` is lower than the last one accepted, or if `expires` has passed (see open question 3). Keep the previous copy when a fetch or check fails [9].
4. Parse it from SD with the streaming parser. For each game, pick the newest build that `Manifest::check` accepts on this host, and list the game.
5. To install: download the `.chgame` to a temp file and check `size` and `sha256` before using it [28]. Rename it into `/games/` and run the existing installer, then confirm that the `.pkg` it writes matches the build's `pkg`.

**Publishing flow:**
1. An author opens a pull request to a catalog repo that adds either a package or a source pinned to a commit.
2. CI runs `pack_game.py`, attaches the `.chgame` to a release, regenerates `catalog.json`, and signs it with a key held in repository secrets.

This mirrors Flipper's catalog, which builds packages from pinned sources [23].

## 7. Cross-dimension insights

- **Copy the part of Claude Code that does not need git.** Much of its trouble is in git transport, credentials, and cache refresh [9][12]. The path a device can use, a URL catalog with `archive` entries, is one of its newest parts [1][9]. The design most worth copying is also among the least proven, so we should borrow from the older device designs in section 3 as well.
- **A pin is only as trustworthy as the file that carries it.** With no verified TLS (B9), the catalog's signature is what makes every hash in it mean anything [14][19][29].
- **Background updates carry the sharpest risk.** The zero-click exploit and an auto-update that broke the official catalog both came through background updates [9][14].
- **Our API level is the compatibility field Claude Code lacks.** F-Droid and Flipper both list builds per SDK or API level, so a client can choose one [21][24]. A `builds[]` list keyed by `api` lets a game keep serving older firmware after a new level ships.

## 8. Recommendations

Ranked by value. Item 1 has to land before the "Get games" browser ships.

1. **Spike signature verification on the device.**
   - **What:** check that the shipped mbedTLS or wolfSSL exposes Ed25519 or ECDSA P-256 verification to application code, and measure its time and RAM on x4pro.
   - **Feeds:** the catalog-trust decision.
   - **Basis:** B9, [19][29].
   - **Effort:** small.
2. **Write a spine decision for the store: the catalog format and trust.**
   - **What:**
     - first re-read [1] and [2], because their claims go stale after 2026-11-04
     - the format in section 6
     - a signed catalog with a monotonic `version` and an `expires` time
     - third-party catalogs added by URL plus fingerprint
     - per-entry validation, with unknown entries tolerated
     - `builds[]` keyed by `api`
     - no automatic install
   - **Feeds:** a new architecture decision beside AD-16 and AD-19, plus the "Get games" epic (B2).
   - **Basis:** [1][2][3][19][21][22][24][25][29], B9.
   - **Effort:** small to write; the epic is medium.
3. **Build the catalog in CI from packed games, and sign it there.**
   - **What:** a catalog repo; `pack_game.py` builds each package; CI writes and signs `catalog.json` and publishes the packages as release assets, not raw branch files.
   - **Feeds:** the release workflow (B2 AD-25) and the starter repo.
   - **Basis:** [23][30], P1.
   - **Effort:** medium.
4. **Keep a known-good catalog.**
   - **What:** never replace the cached catalog with one that fails to download, verify, or parse. Write catalog state with the same tmp-then-rename pattern as our saves (B1).
   - **Feeds:** the "Get games" epic.
   - **Basis:** [9][12].
   - **Effort:** small, inside the epic.
5. **Add catalog fields and a preview catalog.**
   - **5a. `author` and `description` manifest keys,** for the catalog detail view. Feeds the next API level (B2 AD-19). Basis: [1]. Effort: small.
   - **5b. A `preview` catalog** for games on a preview API level, beside the stable one. Feeds the "Get games" epic. Basis: [3]. Effort: small.

**Do not adopt:** git, npm, or command sources; plugin dependencies; merging manifests with catalog entries; a version cache; background auto-update; enterprise allowlists.

## 9. Open questions

**These block the design:**
- **Can the device verify signatures, and at what cost?** See recommendation 1.
- **Could the catalog fetch use verified TLS instead?** Re-enabling the `esp_http_client` path for one fetch would touch upstream networking code and the upstream-touch ledger (B9). Even then, a signature keeps protecting the catalog if a mirror or a GitHub account is compromised. We did not evaluate this option.
- **How should the device know the time for `expires`?** A device without a synced clock cannot check expiry. We did not research whether to use NTP, the HTTP `Date` header, or a fallback to the version check alone.

**Background:**
- **How does Flipper's client pick a build, and what happens when no build matches?** The catalog's API query parameters were not read.
- **Are stale-cache reports still open on Claude Code 2.1.28x?** The latest comments on #14061 and #17361 were not read; the GitHub API was blocked in this session.
- **How do update-time hook attacks work?** The PromptArmor and arXiv write-ups on hooks changed by an update were found but not read.

## Source appendix

Claude Code doc pages are live documents without a last-updated date; the current release when read was v2.1.289 (2026-10-03).

| # | Supports | Publisher | Pub date | Accessed | Confidence |
|---|---|---|---|---|---|
| 1 | Catalog schema, name rules, reserved names, plugin source types, `archive` with `sha256`, URL marketplace fetching only the JSON, extraction caps, source errors appearing only at install | [Anthropic: Marketplace reference](https://code.claude.com/docs/en/plugins/marketplace-reference) | live doc | 2026-10-04 | high (lead re-read) |
| 2 | Version resolution order, cache layout, auto-update defaults, update only on version change | [Anthropic: How plugins load](https://code.claude.com/docs/en/plugins/loading) | live doc | 2026-10-04 | high (lead re-read) |
| 3 | Hosting a URL marketplace, `renames`, `forceRemoveDeletedPlugins`, no catalog field for auto-update, two catalogs as channels | [Anthropic: Host a marketplace](https://code.claude.com/docs/en/plugins/host-marketplace) | live doc | 2026-10-04 | high |
| 4 | `plugin.json` fields, `claude plugin validate` | [Anthropic: Manifest reference](https://code.claude.com/docs/en/plugins/manifest-reference) | live doc | 2026-10-04 | high |
| 5 | Trust warning, name-tier enforcement, no signing described, threat surface | [Anthropic: Plugin security](https://code.claude.com/docs/en/plugins/security) | live doc | 2026-10-04 | high |
| 6 | Managed allowlists and blocklists, `enabledPlugins` | [Anthropic: Plugins for organizations](https://code.claude.com/docs/en/plugins/org) | live doc | 2026-10-04 | high |
| 7 | `/plugin marketplace add` forms | [Anthropic: Install plugins](https://code.claude.com/docs/en/plugins/install) | live doc | 2026-10-04 | high |
| 8 | Anthropic does not review third-party marketplaces | [Anthropic: Anthropic marketplaces](https://code.claude.com/docs/en/plugins/anthropic-marketplaces) | live doc | 2026-10-04 | medium |
| 9 | Feature timeline, 200+ marketplace entries, cache, state, and name-collision changes | [Anthropic: claude-code CHANGELOG](https://raw.githubusercontent.com/anthropics/claude-code/main/CHANGELOG.md) | 2025-10 to 2026-10 | 2026-10-04 | high |
| 10 | Release dates for CHANGELOG versions | [npm: @anthropic-ai/claude-code](https://registry.npmjs.org/@anthropic-ai/claude-code) | 2025-10 to 2026-10 | 2026-10-04 | high |
| 11 | Official catalog: 315 plugins; 262 external entries pin a `sha`, 97 also a `ref` | [Anthropic: claude-plugins-official marketplace.json](https://raw.githubusercontent.com/anthropics/claude-plugins-official/main/.claude-plugin/marketplace.json) | live file | 2026-10-04 | medium |
| 12 | Open stale-cache and missed-update issues | [GitHub: anthropics/claude-code #14061](https://github.com/anthropics/claude-code/issues/14061), #17361, #45810 | 2025-12 to 2026-10 | 2026-10-04 | medium |
| 13 | One unknown source type broke the whole catalog on older clients | [GitHub: anthropics/claude-code #33172](https://github.com/anthropics/claude-code/issues/33172) | 2026-04 | 2026-10-04 | high |
| 14 | Plugin4Shell: SHA-named branch bypassed pinning, zero-click via auto-update, patched 2.1.179 | [Air Security: Plugin4Shell](https://www.air.security/blog-posts/plugin4shell) | 2026-09-17 | 2026-10-04 | high |
| 15 | Independent coverage of Plugin4Shell | [The Register](https://www.theregister.com/security/2026/09/17/ai-coding-agents-0-click-rce-flaw-could-hand-attackers-keys-to-the-kingdom/5297335) | 2026-09-17 | 2026-10-04 | high |
| 16 | Proof of concept: marketplace skill hijacking dependency installs | [Prompt Security](https://prompt.security/blog/when-your-plugin-starts-picking-your-dependencies-marketplace-skills-and-dependency-hijack-in-claude-code) | 2026-01-05 | 2026-10-04 | high |
| 17 | Aggregator's claim of 2,700+ marketplaces | [claudemarketplaces.com](https://claudemarketplaces.com/) | 2026-10-04 | 2026-10-04 | medium (self-reported) |
| 18 | Practitioner advice: pin SHAs, separate catalog repo, stable and canary as distinct pinned SHAs | [The Cloud Codex: Your Claude plugin marketplace needs more than a git repo](https://www.mpt.solutions/your-claude-plugin-marketplace-needs-more-than-a-git-repo/) | 2026-04-20 | 2026-10-04 | medium |
| 19 | Signed JAR and GPG index, signed `entry` point | [F-Droid: All our APIs](https://f-droid.org/docs/All_our_APIs/) | undated | 2026-10-04 | high |
| 20 | Live signed entry: `timestamp`, `maxAge`, index `sha256` and `size`, 62.6 MB for 4,525 packages | [F-Droid: entry.json](https://f-droid.org/repo/entry.json) | live file | 2026-10-04 | high (lead fetched) |
| 21 | Per-version `file{sha256, size}` and `usesSdk` integer gating | [fdroidserver: index-v2 test fixture](https://gitlab.com/fdroid/fdroidserver/-/raw/master/tests/repo/index-v2.json) | 2023-02 fixture | 2026-10-04 | high |
| 22 | Repos added by URL plus `?fingerprint=`; the fingerprint failure surfacing at install | [F-Droid client issue #2078](https://gitlab.com/fdroid/fdroidclient/-/issues/2078) | undated | 2026-10-04 | medium |
| 23 | Submission by pull request, `commit_sha` pin, unique IDs, increasing versions, catalog builds packages | [Flipper: catalog Manifest.md](https://github.com/flipperdevices/flipper-application-catalog/blob/main/documentation/Manifest.md) and [Contributing.md](https://github.com/flipperdevices/flipper-application-catalog/blob/main/documentation/Contributing.md) | live doc | 2026-10-04 | medium |
| 24 | One build per app version, API version, and target | [Flipper catalog: application page](https://catalog.flipperzero.one/application/67f379d625a4a6f1fb4a57f7/page) | live page | 2026-10-04 | high |
| 25 | App Loader requires major API version to match the firmware | [Flipper: AppsOnSDCard.md](https://raw.githubusercontent.com/flipperdevices/flipperzero-firmware/dev/documentation/AppsOnSDCard.md) | dev branch | 2026-10-04 | high (lead re-read) |
| 26 | Taps as git repos, fully qualified names, tap trust since 6.0.0 | [Homebrew: Taps](https://docs.brew.sh/Taps) and [Tap-Trust](https://docs.brew.sh/Tap-Trust) | current docs | 2026-10-04 | medium |
| 27 | Per-platform `sha256` for bottles, fallback on mismatch | [Homebrew: Bottles](https://docs.brew.sh/Bottles) | current docs | 2026-10-04 | high |
| 28 | ABI version in the index path, content-addressed files, hash used only to skip existing files | [MicroPython: mip source](https://raw.githubusercontent.com/micropython/micropython-lib/master/micropython/mip/mip/__init__.py) | master | 2026-10-04 | medium |
| 29 | Timestamp, snapshot, and targets roles; expiry; monotonic versions; length limits | [TUF specification v1.0.36](https://theupdateframework.github.io/specification/latest/) | 2026-08-05 | 2026-10-04 | high |
| 30 | Guides recommending CI regenerate the catalog from package manifests | [systemprompt.io: Publish a plugin to a Claude marketplace](https://systemprompt.io/guides/publish-plugin-claude-marketplace) | 2026 | 2026-10-04 | low (search summary) |

**Internal documents (crosshatch-player at `f1413185`; the cited lines are unchanged at later commits; not evidence):**
- B1 `docs/crosshatch/formats.md`
- B2 `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md` (AD-15, AD-16, AD-19, AD-25)
- B6 `src/network/HttpDownloader.cpp` `runGetWolf` calls `setInsecure()`; `platformio.ini` `[base]` sets `-DFREEINK_NET_WOLFSSL=1`
- B7 `lib/JsonParser/StreamingJsonParser.h`
- B8 `src/games/GameHash.cpp`
- B9 `src/network/HttpDownloader.cpp` lines 12–28, 66–136, 138–244, 253–260: with wolfSSL on, the CA-bundle path is compiled out
- P1 `_bmad-output/planning-artifacts/research/competitive-crosssmudge-app-store-vs-crosshatch-game-2026-10-04/research.md`

## Staleness map

Computed with `recon_kit.py staleness` from the claims ledger, using the technical research pack's windows: versions and features 1 month, failures and ecosystem 6 months, patterns 2 years. Nothing is stale today.

| Claims | Class | Re-check by |
|---|---|---|
| `archive` source and `sha256`, URL catalogs, version resolution, auto-update defaults, no signing | version, feature | 2026-11-04 |
| Plugin4Shell, open stale-cache issues, size of the official catalog | failure, ecosystem | 2027-03-01 to 2027-04-04 |
| F-Droid signed entry, Flipper API gating and catalog, TUF roles, the unknown-entry lesson | pattern | 2028-04-01 to 2028-10-04 |

The earliest re-check is **2026-11-04**. Claude Code changes its marketplace rules almost weekly, so re-read the marketplace reference and loading pages before the store decision is written into the spine.

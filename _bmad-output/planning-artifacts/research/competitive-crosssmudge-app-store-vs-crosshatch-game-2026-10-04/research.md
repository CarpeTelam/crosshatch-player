---
title: 'competitive research: CrossSmudge App Store vs crosshatch-player game packages'
type: 'competitive'
topic: 'CrossSmudge App Store vs crosshatch-player game packages'
decision: 'What crosshatch-player should adopt from the CrossSmudge app/App Store model after v1, and where our game-package model should stay different'
source: 'native run'
status: complete
claims_verified: 9
claims_unverified: 4
claims_overturned: 0
sources: 21
preset: 'standard'
validation: 'normal'
created: '2026-10-04'
updated: '2026-10-04'
---

# Competitive research: CrossSmudge App Store vs crosshatch-player game packages

**Decision this research serves:** What crosshatch-player should adopt from the CrossSmudge app/App Store model after v1, and where our game-package model should stay different

## Executive summary

**Recommendation.** After v1, add an on-device "Get games" browser. It fetches a catalog of `.chgame` packages, checks each download against a SHA-256 hash listed in the catalog, and saves it into the existing `/games/` inbox. The installer we already have then does the rest. The hash check is required, not optional: our `HttpDownloader`, inherited from upstream, skips TLS certificate verification on the wolfSSL transport that our `[base]` build flags enable (B6).

This gives players CrossSmudge's best feature—installing over Wi-Fi without a computer—without taking on its weak spots. Pair it with the deferred web Games page and the planned starter repo. Do not copy CrossSmudge's packaging, runtime, or trust model.

**Three findings drive this.**

1. **CrossSmudge's App Store is convenient but not trustworthy.**
   - The store reads a hand-edited `catalog.json` straight from the repo's `main` branch on raw GitHub. The URL is hard-coded [1][2].
   - It downloads each file of an app directly into the live app folder, with no staging folder or rename. An app counts as installed once its `manifest.json` exists, and 9 of the 10 catalog entries list that file first, so an interrupted install usually still shows as installed [1][2].
   - The catalog has no hashes or sizes [2]. The download transport it tries first turns TLS certificate checks off [3].
   - The apps it installs run with the `os` library open and file functions that accept any absolute path, so any app can read or delete anything on the SD card [9].
2. **Our package design already solves the hard part of a store.**
   - A `.chgame` is one file with a strict member whitelist and size limits (B1).
   - The installer stages each install, commits it with a `.pkg` marker written last, and recovers from a crash at any step. Saves survive a reinstall (B1, B2 AD-16).
   - The package hash we already compute can also flag updates. That avoids CrossSmudge's version check, a plain string comparison that labels a downgrade as an upgrade [1].
3. **The store model has no traction yet.**
   - It shipped in one commit on 2026-10-03 [11]. All 10 catalog apps are by the repo owner [2].
   - The repo has 8 stars, 0 forks, and no open pull requests [12]. No community mention turned up in web searches or the readme.club directory [13][14].
   - So the evidence shows the store is easy to use, not that it attracts authors. That supports our order: starter repo and authoring tools first, catalog after.

**Biggest caveat.** CrossSmudge is one day into its app model, so its traction says little either way. Re-check it in 60–90 days.

## 1. CrossSmudge packaging and distribution

**Takeaway.** An app is a loose folder of files, and the store copies those files one at a time from GitHub's `main` branch. The pipeline is simple and quick to publish to. It has no integrity checks, no atomic install, and no way to say which device an app targets.

**Bundle.**
- An app is a folder: `manifest.json`, `main.lua`, an optional `icon.raw`, and any extra `.lua` modules or data files, including in subfolders [5].
- `icon.raw` is a 128-byte, headerless, 32×32 1-bit bitmap [5][4].
- The device has no single-file package format. Only the web portal accepts a `.zip`, and the browser unpacks it before uploading the files [7].
- The manifest has six string fields: `id`, `name`, `version`, `author`, `description`, and `entry` [5].
- The firmware treats every manifest field as optional. A folder holding just a `main.lua` is listed as an app [4] (confidence: medium, unverified).
- The README says `id` must match the folder name, but the firmware does not check this, and it does not check `entry` for path traversal [4][5] (confidence: medium, unverified).

**Catalog.**
- The schema is `{repo, branch, apps:[{id, name, version, author, description, files[]}]}`. It has no sizes, hashes, signatures, minimum firmware version, device fields, or categories [2].
- It is maintained by hand and published by merging a pull request [5].
- `generate_release_catalog.py` is a separate script: it builds the firmware over-the-air (OTA) update catalog, which does carry `firmware_sha256`, `size`, and `supported_devices` [8]. So CrossSmudge already verifies firmware downloads in a way it does not verify app downloads.
- The store fetches the catalog and every file from `raw.githubusercontent.com/Mumfee/CrossSmudge/main/apps/`, a URL hard-coded in the firmware [1]. A merge to `main` goes live on every device at once, and nothing pins catalog content to a firmware version.

**On-device store.**
- The store is a state machine: check Wi-Fi, fetch the catalog, list apps, show detail, download.
- It streams the catalog to a temp file on SD and parses it from there [1].
- It unloads SD-card fonts first to free DRAM for TLS. The CHANGELOG says this frees 20–30 KB on the C3 [6].
- Icons load lazily, which the CHANGELOG says cut catalog load time from more than 30 s to 1–2 s [6].
- Install writes each file straight to its final path [1]. Version 1.6.5.1 added three attempts per file, alternating between two transports with a 500 ms backoff, separate storage and network error messages, and a Retry button [6][1]. It cannot resume a partly downloaded file.
- Updates are found by a plain string `!=` on the version, so a downgrade is labeled "Upgrade" [1].
- The only integrity check is that the byte count matches `Content-Length` [3].

**Web portal.**
- Five `/api/applications*` routes list apps, list an app's files, download, upload, and delete, beside an `/applications` page [7].
- When you drop a `.zip`, JSZip unpacks it in the browser, and the browser uploads the files one at a time.
- The device checks the app id and rejects path traversal. It does not check that a manifest is present and sets no size limit [7] (confidence: medium, unverified).

**Trust.**
- A pull-request review on the repo is the only gate. There is no signing, no publisher identity, and no permission declaration [5].
- The device store does not sanitize the `id` and file names it gets from the catalog before joining them into SD paths [1].

**Device targeting.**
- Neither the manifest nor the catalog says which device or input type an app supports, so every app is listed on every device [2][5].
- C3 compatibility is left to developers: the README asks them to test in a 75 KB simulator mode [5].

## 2. CrossSmudge runtime and app API

**Takeaway.** The `smudge.*` API is broad and pragmatic. Behind it, though, the runtime has no sandbox in the sense that matters: there is no file jail, no instruction budget, and no API versioning.

**Engine.**
- Vendored PUC Lua 5.4.7, compiled as C, with `LUA_32BITS` on [10].
- It opens five libraries: base, table, string, math, and **os**. It removes nothing from them, so `load` (which also accepts binary chunks), `string.dump`, and `os.remove`/`os.rename` all stay available [9].
- A counting allocator caps the Lua heap at 220 KB on a C3 (it also refuses any allocation that would leave less than 3 KB of free heap) and at 2 MB with PSRAM [9]. The README presents 75 KB as the C3's working limit, a figure the code uses only in a simulator mode, so the docs and the code disagree [9][5].
- Nothing limits instructions or run time (`lua_sethook` is never called), so a runaway loop blocks the device [9] (confidence: medium, an absence finding).

**API surface.** About 49 C functions, exposed under about 70 names because `ink.*` remains an alias, cover:
- 1-bit drawing primitives, sprites, a two-level dither, and fixed built-in fonts.
- Fast or full refresh.
- System header, button hints, and popups.
- Key-value `save`/`load` and a streaming section reader.
- Raw file read, write, list, and delete.
- Time, random number, battery, and memory queries.

It has no networking, audio, timers, keyboard, or true grayscale [9].

**File access.**
- The file functions pass absolute paths through unchanged and do not reject `..` [9].
- Together with the open `os` library, this gives every installed app full read, write, and delete access to the whole SD card.

**Modules.** Multi-file apps use `dofile`, which recompiles on every call. The large Codex app loads its screens lazily [20], and the CHANGELOG says it had to keep each function under 128 identifiers and 256 bytecode instructions to avoid compiler memory spikes on the C3 [6] (confidence: medium, self-reported).

**Lifecycle.**
- Callbacks are optional globals: `on_init`, `on_update` (called on every loop tick), `on_draw`, `on_button` (button releases only), `on_tap`, `on_swipe`, `on_touch`, and `on_exit` [9].
- Back is passed to the app, which must call `smudge.exit()` itself.
- There is no suspend callback [9][17].
- The first error is sticky and shows a crash screen [17].

**Versioning.** None. No manifest field and no load-time check guards against an app needing newer firmware. A call to a missing function crashes the app with "attempt to call a nil value" [4][9] (confidence: medium, unverified).

**Developer experience.**
- The docs contradict the code on the strings `get_device()` returns and on the heap cap [5][9]. The research also reported a mismatch in the touch callback's signature, which the citation check did not re-confirm.
- `get_device()` tests macros that nothing in the repo defines, so on hardware it can never return `sticky` or `x4pro` [9][15] (confidence: medium, unverified).
- The simulator docs say it is set up for macOS on Apple Silicon only [16]. We found no Lua lint or test tooling.

**Native code.** All 10 apps are Lua. Ten earlier native C++ app headers, about 8,700 lines, are still in the tree, but no source file includes them [21][6].

## 3. CrossSmudge ecosystem and trajectory

**Takeaway.** One maintainer converted the apps to Lua and built the store in the last week. Nobody else has contributed to the app model yet.

**Catalog and authors.**
- Ten apps, all by `Mumfee`, all at version 1.0.0 [2].
- Four are not games: Dice Roller, Life Counter, Holy Rosary, and Divine Worship (a liturgical daily-prayer app) [2].
- The v1.6.5 CHANGELOG lists 11 apps including a Tally Counter, while the same entry says that app was removed, so the CHANGELOG contradicts itself [6]. The v1.6.5 commit message calls the set a "community app suite" [11].
- To submit an app, an author forks the repo, adds a folder and a catalog entry, and opens a pull request [5]. GitHub shows no open pull requests [12], and git history has no merged pull request touching `apps/` [11].

**Cadence.**
- CrossSmudge is a GitHub fork of CrossInk ("Personal fork of CrossInk"), which in turn merges CrossPoint [12][11].
- Its own releases are four tags [11][18]:
  - v1.4.0.1.1 (2026-08-01): apps in C++
  - v1.6.0 (2026-09-30): a CrossInk merge
  - v1.6.5 (2026-10-03, pre-release): added the Lua engine and App Store
  - v1.6.5.1 (same day): hardened downloads
- The owner has made 17 commits in bursts, 12 of them between 2026-09-29 and 2026-10-03 [11].
- There is no stated roadmap. `SCOPE.md` is copied unchanged from CrossPoint and still lists games as out of scope [19].

**License and upstream.**
- MIT, with CrossPoint's copyright line unchanged. No terms cover submitted apps [19].
- Compared with the CrossInk v1.6.0 commit it merged, it changes 390 files, 283 of them outside `apps/`. It edits upstream files in place, including `CrossPointWebServer.cpp` (+413/−1 lines) and `OtaUpdater.cpp`, and has no minimal-diff rule [11][19].

**Traction.**
- 8 stars, 0 forks, and 0 open pull requests [12].
- It is missing from readme.club's directory of 51 Xteink firmwares, where CrossPoint shows about 8,000 stars and CrossInk about 1,100 [14].
- Web searches found no Reddit, forum, or blog mentions [13].
- The GitHub counts and the absence from readme.club and web searches agree that outside traction is negligible as of 2026-10-04. The store is a day old, so that is expected.

## 4. Our baseline (internal, not research evidence)

This section restates the parts of our design that the side-by-side table in section 5 does not carry, from project documents B1–B6. These are design facts, not research claims.

- **Limits.** A `.chgame` package is at most 256 KB, with at most 32 members of at most 128 KB each. The installer checks the zip central directory before extracting, rejects Lua bytecode, and verifies CRC-32 per member (B1).
- **One entry point.** The installer reads only the SD inbox `/games/*.chgame`, when Games opens. Files reach it through the web file manager or USB (B2 AD-16).
- **API surface.** `api-level-1.txt` lists every function, key, and limit a level offers, and a CRC over it freezes the level (B2 AD-19, B3).
- **Distribution today.** Packages exist only as a CI artifact on pull requests labeled `package-games` (B4). The firmware updates itself over the air from the fork's GitHub releases (B2 AD-25). A web Games page (`/api/games*` reserved) and a starter repo are deferred to after v1 (B2 Deferred, B5).
- **Targets.** x4pro and sticky only, behind `FREEINK_CAP_GAMES` (B2 AD-25).

## 5. Side by side

**Takeaway.** CrossSmudge is ahead on wireless install, a remote catalog, and update detection. Every other row favors our design.

| | CrossSmudge apps | crosshatch-player games |
|---|---|---|
| Unit of distribution | Folder of loose files [5] | One `.chgame` zip with whitelisted members (B1) |
| Manifest | Six optional strings; no validation [4] | Typed keys, one parser, an `Invalid`/`Unavailable`/`Ok` verdict (B2 AD-15) |
| Compatibility gate | None [9] | Integer API level plus a frozen surface CRC (B2 AD-19, B3) |
| Device and input targeting | None; apps branch at runtime [5] | `HostCaps` (api, seats, pass, nearby); games-only build environments (B2 AD-15, AD-25) |
| Install atomicity | None; files written in place [1] | Staged in a temp folder, renamed, `.pkg` written last (B1) |
| Integrity | `Content-Length` only; unverified TLS on the main path [3] | CRC per member plus a SHA-256 package hash, at install only; no download path yet (B1) |
| Update detection | String `!=` on version [1] | None yet; the package hash would identify the exact build (B1) |
| Wireless install | On-device store plus web portal [1][7] | Web file manager to the inbox; no store (B2 AD-16) |
| Remote catalog | Raw `main` branch, hard-coded [1] | None |
| Runtime isolation | `os` open, any SD path writable, no budget [9] | No file API; no `os`, `io`, or `load`; 2 million instructions per callback (B2 AD-6) |
| Saves | Key-value `save`/`load` plus raw file I/O, both under app control [9] | Owned by the runtime, crash-safe, kept across reinstall (B1) |
| Lua | 5.4.7, 32-bit numbers [10] | 5.5.1, 64-bit integers (B2 AD-4) |
| Non-game apps | Four of the ten catalog apps [2] | Out of scope; our scope is simple turn-based games (B2 AD-23) |
| Devices | C3 and S3 [5] | S3 (x4pro, sticky) only (B2 AD-25) |
| Authoring docs | README that disagrees with the code [5][9] | Surface list checked against the live `ch` table (B2 AD-19) |

## 6. Cross-dimension insights

- **The store and the runtime decide each other's risk.** CrossSmudge's store is risky because it combines three things: no hashes and unverified TLS [3], writes into the live folder [1], and apps with full SD access [9]. Our runtime gives a game no file access at all, so a malicious package can do far less harm. That smaller risk could make a user-configurable catalog URL (an open-ecosystem feature) reasonable for us even though it is not for them. Whether to offer one is a product choice, not a finding. Either way, our own unverified-TLS path (B6) means any catalog of ours must hash-check the downloaded bytes.
- **Convenience was built before supply.** CrossSmudge built the store and the browser portal before any outside author existed [2][12]. Our deferral list puts the starter repo after v1 too (B5). Nothing here shows that a store creates authors. The only evidence is that one person wrote all 10 apps, first in C++ from August, then rewrote them in Lua and built the store in one week [11][2]. That points to the authoring loop mattering more than the store.
- **A one-file package makes the store simple.** CrossSmudge's least reliable code is the per-file download loop and the half-installed states it can leave [1]. A store that downloads one `.chgame` to the inbox reuses our staged, committed installer unchanged and keeps AD-16's "one installer" rule (B2).

## 7. Recommendations (after v1)

Ranked by value. Item 4 sets the delivery order: the starter repo ships before, or alongside, the catalog. Effort is a rough estimate.

1. **"Get games": an on-device catalog browser that saves to the inbox.**
   - **What:**
     - The catalog is a JSON file published as a release asset or on GitHub Pages, not a branch file.
     - Each entry carries `id`, `name`, `version`, `api`, `modes`, `seats`, the package URL, `size`, the full `sha256`, and the 8-byte package hash.
     - The device hides any game that `Manifest::check` would mark `Unavailable` on this host.
     - Install streams the download to a temp file, verifies size and SHA-256, renames the file into `/games/`, then runs `installAll`.
     - "Update available" means the catalog's package hash differs from the installed `.pkg`. That needs no version ordering and is never fooled by a downgrade.
   - **Feeds:** a new epic and an AD-16 amendment (B2).
   - **Basis:** the problems in [1][2][3], the existing installer (B1), and the TLS gap (B6). Confidence: high on the risks it avoids.
   - **Effort:** medium. It is a new activity plus a small catalog parser. Reusing `HttpDownloader` unedited keeps that file off the upstream-touch ledger.
2. **Build the deferred web Games page (`/api/games*`).**
   - **What:** upload a `.chgame` to the inbox, list games with the installer's verdict, remove a game, and download a package back to the computer. It stays one upload per package, because our package is already a single file.
   - **Feeds:** the web Games page row in the Deferred table of the architecture spine (B2).
   - **Basis:** CrossSmudge's portal shows that people want install and export in the browser [7].
   - **Effort:** medium. It touches `CrossPointWebServer.cpp`, which needs a ledger entry.
3. **Add optional `author` and `description` keys to the manifest.**
   - **What:** keys that a catalog detail view can show, as CrossSmudge's does [1][2].
   - **Feeds:** the next API level. Manifest keys are level entries, so under AD-19 adding them means defining a new level (B2).
   - **Basis:** CrossSmudge's detail view [1] and catalog schema [2].
   - **Effort:** small.
4. **Ship the starter repo before, or alongside, the catalog.**
   - **What:** lead with a simulator loop, a pack script, and one example game.
   - **Feeds:** the brief addendum's "Later: game starter repo" (B5).
   - **Basis:** CrossSmudge's docs drifted from its code within a day [5][9]. Our surface list and CRC already prevent that drift, so keeping them is the part to protect.
   - **Effort:** medium.
5. **Reuse CrossSmudge's download-handling ideas in that work.**
   - **What:** stream the catalog to SD rather than RAM, release SD fonts before opening TLS, load catalog icons lazily, and retry a failed download through the other transport.
   - **Feeds:** the notes of the item 1 epic.
   - **Basis:** [1][6]. The CHANGELOG reports the timings (confidence: medium, self-reported).
   - **Effort:** small, inside item 1.

**Keep different (CrossSmudge choices we do not adopt):**
- packaging apps as loose folders
- writing installs into the live folder
- distributing from a raw branch
- giving scripts `os`, `load`, or file access
- running without an instruction budget
- shipping without API versioning
- comparing versions as strings
- `LUA_32BITS`, which the architecture spine (B2) already defers for good reason

**Decide separately (open, not recommended here):**
- Whether to accept companion tools that are not games, such as a dice roller or life counter. Four of CrossSmudge's ten apps are such tools [2], and they may fit AD-23's "parlor" scope.
- Whether to support third-party catalog URLs.
- Where to host our catalog: a release asset, a Pages site, or a separate catalog repo.

## 8. Open questions

- **Does a store bring in authors?** It is too early to tell. Re-check CrossSmudge's catalog authors, pull requests, and stars on the date in the Staleness map (a Refresh run).
- **Two CrossSmudge runtime behaviors are unconfirmed:** whether going to sleep runs `on_exit` (if not, an app loses unsaved state), and whether the task watchdog resets a runaway Lua loop. Either answer changes only how this document describes CrossSmudge, not our design.
- **Is the Astro docs site at mumfee.github.io/CrossSmudge live,** and does it have an app gallery? We did not fetch it.
- **Is CrossSmudge discussed on Reddit?** We did not search Reddit directly. A search of r/xteink would settle it.
- **Has anyone ever filed an issue or a closed pull request?** The GitHub page we read shows open pull requests only and no issue count. The GitHub API, which would settle it, was blocked in this session.

## Source appendix

CrossSmudge files are cited at commit `4dcb7fdc67f08a34d711946586e79bc050726018` (2026-10-03). GitHub and web pages were accessed 2026-10-04.

| # | Supports | Publisher | Pub date | Accessed | Confidence |
|---|---|---|---|---|---|
| 1 | Store catalog/base URLs hard-coded to raw `main`; per-file install into live folder; installed status from `manifest.json`; string version compare; retries; no path sanitization | [Mumfee/CrossSmudge `src/activities/apps/AppStoreActivity.h`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/src/activities/apps/AppStoreActivity.h) | 2026-10-03 | 2026-10-04 | high (lead re-read) |
| 2 | Catalog schema without hashes/sizes/device fields; 10 apps, all by Mumfee; `files[]` order | [Mumfee/CrossSmudge `apps/catalog.json`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/apps/catalog.json) | 2026-10-03 | 2026-10-04 | high (lead parsed) |
| 3 | WolfSSL transport `setInsecure()`; Content-Length-only integrity | [Mumfee/CrossSmudge `src/network/HttpDownloader.cpp`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/src/network/HttpDownloader.cpp) | 2026-10-03 | 2026-10-04 | high (lead re-read) |
| 4 | Manifest fields optional with defaults; id/entry unchecked; no API version field | [Mumfee/CrossSmudge `src/activities/apps/lua/AppPackage.h`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/src/activities/apps/lua/AppPackage.h) | 2026-10-03 | 2026-10-04 | medium |
| 5 | Bundle layout, manifest table, icon format, submission by PR, 75 KB guidance, documented API | [Mumfee/CrossSmudge `apps/README.md`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/apps/README.md) | 2026-10-03 | 2026-10-04 | high |
| 6 | v1.6.5/v1.6.5.1 changes: font release for TLS, lazy icons, retries, Codex compiler limits, conflicting Tally Counter entries | [Mumfee/CrossSmudge `CHANGELOG.md`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/CHANGELOG.md) | 2026-10-03 | 2026-10-04 | medium (self-reported) |
| 7 | Web portal routes; browser-side JSZip; upload checks | [Mumfee/CrossSmudge `web/pages/applications.js`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/web/pages/applications.js) and `src/network/CrossPointWebServer.cpp` | 2026-10-03 | 2026-10-04 | medium |
| 8 | Firmware OTA catalog has sha256, size, supported_devices | [Mumfee/CrossSmudge `scripts/generate_release_catalog.py`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/scripts/generate_release_catalog.py) | 2026-10-03 | 2026-10-04 | high |
| 9 | Libraries opened incl. `os`; heap caps 220 KB / 2 MB; no hook; absolute paths pass through; API surface; callbacks; get_device macros | [Mumfee/CrossSmudge `src/activities/apps/lua/LuaRunner.cpp`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/src/activities/apps/lua/LuaRunner.cpp) | 2026-10-03 | 2026-10-04 | high (lead re-read) |
| 10 | Lua 5.4.7 vendored as C; `LUA_32BITS` | [Mumfee/CrossSmudge `lib/Lua/src/luaconf.h`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/lib/Lua/src/luaconf.h) | 2026-10-03 | 2026-10-04 | high |
| 11 | Release tags, owner commits, App Store in commit 292e09a5 and its message, no merged PRs touching `apps/`, divergence from CrossInk v1.6.0 | [Mumfee/CrossSmudge commit history](https://github.com/Mumfee/CrossSmudge/commits/main) | 2026-08-01 to 2026-10-03 | 2026-10-04 | high |
| 12 | 8 stars, 0 forks, 0 open PRs; fork of uxjulia/CrossInk | [GitHub: Mumfee/CrossSmudge](https://github.com/Mumfee/CrossSmudge) | live page | 2026-10-04 | medium (HTML render; API blocked) |
| 13 | No web, Reddit or forum mentions found | Web searches by the researcher: "CrossSmudge" (extended), "CrossSmudge firmware reddit", "CrossSmudge xteink" | n/a | 2026-10-04 | medium (absence) |
| 14 | Absent from a directory of 51 Xteink firmwares; CrossPoint about 8,000 and CrossInk about 1,100 stars | [readme.club firmware directory](https://www.readme.club/firmware) | live page | 2026-10-04 | medium |
| 15 | `CROSSINK_APP_DEVICE_*` capability macros | [Mumfee/CrossSmudge `include/AppCapabilities.h`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/include/AppCapabilities.h) | 2026-10-03 | 2026-10-04 | medium |
| 16 | Simulator configured for macOS Apple Silicon only | [Mumfee/CrossSmudge `docs/simulator.md`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/docs/simulator.md) | 2026-10-03 | 2026-10-04 | medium |
| 17 | Back handed to app, no suspend callback, sticky error and crash screen | [Mumfee/CrossSmudge `src/activities/apps/lua/LuaAppActivity.h`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/src/activities/apps/lua/LuaAppActivity.h) | 2026-10-03 | 2026-10-04 | high |
| 18 | v1.6.5 pre-release and v1.6.5.1 release pages | [GitHub: Mumfee/CrossSmudge releases](https://github.com/Mumfee/CrossSmudge/releases) | 2026-10-03 | 2026-10-04 | high |
| 19 | MIT license unchanged; SCOPE.md lists games out of scope; merge policy without minimal-diff rule | [Mumfee/CrossSmudge `SCOPE.md`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/SCOPE.md), `LICENSE`, `AGENTS.md` | 2026-10-03 | 2026-10-04 | high |
| 20 | Codex loads its modules with `dofile`, some lazily | [Mumfee/CrossSmudge `apps/codex/main.lua`](https://github.com/Mumfee/CrossSmudge/blob/4dcb7fdc67f08a34d711946586e79bc050726018/apps/codex/main.lua) | 2026-10-03 | 2026-10-04 | high (lead re-read) |
| 21 | Ten native app headers (about 8,700 lines) that no source file includes | [Mumfee/CrossSmudge `src/activities/apps/`](https://github.com/Mumfee/CrossSmudge/tree/4dcb7fdc67f08a34d711946586e79bc050726018/src/activities/apps) | 2026-10-03 | 2026-10-04 | high (lead counted and grepped) |

**Internal baseline documents (crosshatch-player at `93d34ede`, not evidence):**
- B1 `docs/crosshatch/formats.md`
- B2 `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md` (AD-4, AD-6, AD-15, AD-16, AD-19, AD-23, AD-25, Deferred)
- B3 `docs/crosshatch/api-level-1.txt`
- B4 `.github/workflows/crosshatch-game-packages.yml`
- B5 `_bmad-output/planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/addendum.md`
- B6 `src/network/HttpDownloader.cpp` (`runGetWolf` calls `setInsecure()`) and `platformio.ini` `[base]` `-DFREEINK_NET_WOLFSSL=1`

## Staleness map

Computed with `recon_kit.py staleness` from the claims ledger, using the competitive research pack's staleness windows: features 3 months, trajectory and traction 6 months. Nothing is stale today.

| Claims | Class | Re-check by |
|---|---|---|
| Store URLs, catalog schema, install path, TLS and integrity, version compare, runtime libraries and heap caps, file jail, API versioning, `get_device`, web portal | feature | 2027-01-03 |
| Catalog authorship, stars, forks, issues, pull requests, outside mentions | traction | 2027-04-03 |
| Release cadence, one-commit arrival of the app model | trajectory | 2027-04-03 |

The earliest computed re-check is **2027-01-03**. Re-check traction then too, with the feature claims, rather than waiting for 2027-04-03: CrossSmudge rebuilt its whole app model in one week.

# Citation check r1-1

Report: `research.md` (this folder). CrossSmudge clone `/home/user/mumfee/crosssmudge` at `4dcb7fdc` (verified HEAD; clone is not shallow). Paths below are relative to that clone unless prefixed `ch:` (crosshatch-player). Line numbers are research.md's.

Format: `[n] | sentence (short) | verdict | evidence path:line | note`

## Executive summary (L24-42)

- [1][2] | L29 store reads hand-edited catalog.json from raw `main`, URL hard-coded | supported | src/activities/apps/AppStoreActivity.h:31-32; apps/catalog.json (`branch: main`) | 
- [1] | L30 downloads each file into live app folder, manifest.json first; no staging/rename; interrupted install shows installed | partly | AppStoreActivity.h:667-670, 690-786 (writes to `targetDir/<file>`), 459-475 (installed = manifest.json exists) | Order comes from catalog `files[]`, not the code. 9 of 10 apps list `manifest.json` first; `codex` lists `card_ui.lua` first. "manifest.json first" is true for 9 of 10 apps, not as a rule.
- [2] | L31 catalog has no hashes or sizes | supported | apps/catalog.json (keys: id,name,version,author,description,files) |
- [3] | L31 transport tried first turns TLS cert checks off | supported | src/network/HttpDownloader.cpp:150 (`http.setInsecure()` in runGetWolfSsl); AppStoreActivity.h:714-716, 748-749 (WOLFSSL attempt 0 and 2); platformio.ini:60 (`-DFREEINK_NET_WOLFSSL=1`) |
- [9] | L32 `os` library open, file functions accept any absolute path → read/delete anything | supported | src/activities/apps/lua/LuaRunner.cpp:136-143, 1101-1115, 1719-1758 | `write_file`/`delete_file`/`read_file` use the path as is when it starts with `/`.
- B1 | L34 `.chgame` one file, strict whitelist, size limits | supported (spot-check) | ch:docs/crosshatch/formats.md:322, 337-339, 353-356 |
- B1,B2 | L35 staged install, `.pkg` last, crash recovery, saves survive | supported (spot-check) | ch:docs/crosshatch/formats.md:394-396, 424-429 |
- [1] | L36 CrossSmudge string-compare bug | supported | AppStoreActivity.h:884, 950, 1010, 1023 (`installedVersion != version` → "Upgrade") |
- [11] | L38 store shipped in one commit on 2026-10-03 | supported | git: 292e09a5 2026-10-03 Mumfee "feat: add App Store, ..." (adds AppStoreActivity.h, catalog.json) |
- [2] | L38 all 10 catalog apps by repo owner | supported | apps/catalog.json: 10 apps, author `Mumfee`, all 1.0.0 |
- [12] | L39 8 stars, no outside PR or issue ever filed | partly | github.com/Mumfee/CrossSmudge (fetched 2026-10-04): 8 stars, 0 forks, 0 pull requests; issues count not shown on the page | The page shows open PR count only. "Ever been filed" (closed ones included) and "0 issues" are not shown by the cited page.
- [13][14] | L39 no community mention; absent from readme.club | supported | readme.club/firmware fetched: 51 firmwares, CrossSmudge not listed | [13] is an absence record (text only claims absence: OK).
- B6 | L42 our HttpDownloader skips TLS verification on wolfSSL, enabled in `[base]` | supported (spot-check) | ch:src/network/HttpDownloader.cpp:67 (runGetWolf), :75 (`setInsecure()`); ch:platformio.ini:73 in `[base]` |

## Section 1 (spot-checks)

- [5][4] | L50 icon.raw 128 bytes, 32x32 1-bit | supported | apps/README.md:47, 84-87; AppPackage.h:113-120 |
- [5] | L52 six manifest string fields | supported | apps/README.md:58-78 |
- [4] | L53 every manifest field optional; folder with just main.lua listed | supported | src/activities/apps/lua/AppPackage.h:79-107 | Defaults set at 88-94; manifest keys only override.
- [4][5] | L54 README says id must match folder; firmware does not check; entry not checked | supported | apps/README.md:73; AppPackage.h:101, 106 | id from manifest overrides folder name with no comparison.
- [2] | L57 schema `{repo, branch, apps:[...]}` without sizes/hashes/etc. | supported | apps/catalog.json |
- [8] | L59 OTA catalog carries firmware_sha256, size, supported_devices | supported | scripts/generate_release_catalog.py:105-124 |
- [1] | L64 catalog streamed to temp file on SD and parsed | supported | AppStoreActivity.h:354-420 |
- [6] | L65 font release frees 20-30 KB on C3 | supported | CHANGELOG.md:72 (v1.6.5 section) |
- [6] | L66 lazy icons cut load from 30+ s to 1-2 s | supported | CHANGELOG.md:74 |
- [6][1] | L67 v1.6.5.1: 3 attempts, alternating transports, 500 ms backoff, error messages, Retry | supported | CHANGELOG.md:8-10; AppStoreActivity.h:744-779 |
- [1] | L67 cannot resume a partly downloaded file | supported | AppStoreActivity.h:711-717 (no `resumePartial` set); HttpDownloader.cpp:504 (resume only when option set) | The downloader supports resume; the store does not enable it. Sentence is accurate as worded.
- [3] | L69 only integrity check is byte count vs Content-Length | supported | HttpDownloader.cpp:182-183, 583-589 |
- [7] | L72 six `/api/applications*` endpoints | partly | src/network/CrossPointWebServer.cpp:390-395 | Five `/api/applications*` routes (list, files, download, upload, delete) plus the `/applications` HTML page. Six routes, five API endpoints.
- [7] | L73 JSZip in browser | supported | web/pages/applications.js (6 jszip references); CrossPointWebServer.cpp:575 serves jszip |
- [5] | L82 README asks devs to test in 75 KB simulator mode | supported | apps/README.md:529-533, 552-554 |

## Section 2

- [10] | L89 Lua 5.4.7, `LUA_32BITS` on | supported | lib/Lua/src/lua.h:21-24; lib/Lua/src/luaconf.h:125 |
- [9] | L90 five libraries incl. os, nothing removed | supported | LuaRunner.cpp:136-145; no setglobal/pushnil removal of `load`, `dofile` or os entries | `dofile` is replaced by l_dofile (498-505), not removed.
- [9][5] | L91 220 KB on C3 (stops below 3 KB free), 2 MB PSRAM; README's 75 KB only a simulator setting; docs understate real limit | partly | LuaRunner.cpp:73-76, 104-117; apps/README.md:338, 407-408 | The cap figures are exact. But the README presents 75 KB as the C3's real usable DRAM, and the code also stops at live free heap minus 3 KB. The effective C3 limit is min(220 KB, free heap − 3 KB), so "understate the real limit" is an interpretation; the docs understate the configured cap, not necessarily the usable heap.
- [9] | L92 no lua_sethook | supported (absence) | grep `sethook` in src/activities/apps: no hits |
- [9] | L94 about 49 C functions | supported (approx.) | LuaRunner.cpp: 47 `LuaRunner::l_*` definitions; 60 `lua_pushcfunction` registrations |
- [9] | L104 absolute paths pass through, `..` not rejected | supported | LuaRunner.cpp:1109-1111, 1211-1213, 1352-1354, 1729-1732, 1753-1756 |
- [20][6] | L107 Codex dofile lazily; ≤128 identifiers, ≤256 instructions | partly | CHANGELOG.md:107 (budgets); [20] apps/codex/main.lua not opened | Budget claim supported by [6]. "10 game screens" in source. [20] not checked (budget).
- [9][17] | L110-112 callbacks list; on_button release only; Back handed to app | supported | LuaRunner.cpp:206-320 (on_init, on_update, on_draw, on_button, on_touch, on_tap, on_swipe, on_exit); LuaAppActivity.h ~60-100 (`wasReleased` → `onButton(...,true)`, Back → `onButton("back")`) | Callback names are in [9] LuaRunner.cpp, not [17].
- [4][9] | L115 no API version field | supported | AppPackage.h:101-106 (only six keys read) |
- [5][9] | L118 docs contradict code: touch signature, get_device strings, heap cap | partly | README.md:158-161 (`on_touch(event,x,y)`), :326-327 ("Xteink X3", "Simulator"...) vs LuaRunner.cpp:1704-1715 ("x3","simulator"...); heap cap README:338 vs LuaRunner.cpp:116 | get_device and heap-cap contradictions confirmed; touch signature not checked against LuaRunner.cpp:270.
- [9][15] | L119 get_device tests macros nothing defines; can never return sticky/x4pro | supported | LuaRunner.cpp:1707-1712; include/AppCapabilities.h:24-28 (only X4CLASSIC defined); repo-wide grep finds no STICKY/X4PRO define | Extra: X4CLASSIC is always defined (to 0 or 1), so `#elif defined(...)` would return "x4" on any build where AppCapabilities.h reaches LuaRunner.cpp; LuaRunner.cpp does not include it directly. Not a citation problem.
- [16] | L120 simulator macOS Apple Silicon only | supported | docs/simulator.md:12-18 | Doc says "currently configured for" and lists Linux/Intel adjustments; "set up for" matches.
- [17][6] | L122 ~9.7k lines dead native headers | not verified | — | Not checked within budget; [17] LuaAppActivity.h is an odd source for this claim.

## Section 3

- [2] | L129-130 ten apps, all Mumfee, all 1.0.0; half non-games | supported | apps/catalog.json |
- [6] | L131 v1.6.5 CHANGELOG calls them "community app suite" of 11 apps incl. Tally Counter, which does not exist | partly | CHANGELOG.md:32 (11 apps incl. Tally Counter), CHANGELOG.md:166 (same v1.6.5 section: "Permanently removed the extraneous Tally Counter application from firmware and package catalogs") | "community app suite" is not in CHANGELOG.md; it is in tag v1.6.5's commit subject (b5af2930). The same CHANGELOG entry records Tally Counter's removal, so the CHANGELOG is internally inconsistent rather than claiming an app that never existed.
- [5][12] | L132 submission by fork+PR; none opened | partly | apps/README.md:580-583; GitHub page shows 0 PRs | See [12] above: only the open count is shown.
- [12][11] | L135 fork of CrossInk ("Personal fork of CrossInk") | supported | GitHub page: "forked from uxjulia/CrossInk", description "Personal fork of CrossInk" |
- [11][18] | L136 four tags with dates | supported | git tags: v1.4.0.1.1 2026-08-01, v1.6.0 2026-09-30, v1.6.5 2026-10-03, v1.6.5.1 2026-10-03 | v1.6.0 tag points at a CI-fix commit (636a422c); the CrossInk v1.6.0 merge is d0a7f795 (2026-09-29). [18] releases page not fetched.
- [11] | L137 owner 17 commits, 12 between 2026-09-29 and 2026-10-03 | supported | `git log --author=Mumfee`: 17 commits; 1+4+1+6 = 12 in that window |
- [19] | L138 SCOPE.md lists games out of scope | supported | SCOPE.md:41 |
- [19] | L141 MIT, CrossPoint copyright unchanged | supported | LICENSE:1-3 ("Copyright (c) 2025 Dave Allie") |
- [11][19] | L142 vs CrossInk v1.6.0: 390 files, 283 outside apps/; CrossPointWebServer.cpp +414; OtaUpdater.cpp edited | supported (minor) | `git diff --stat b25beb13 4dcb7fdc`: 390 files; 283 outside apps/; CrossPointWebServer.cpp 413 insertions, 1 deletion; OtaUpdater.cpp 11/11 | +413, not +414. Base is the CrossInk v1.6.0 commit merged in d0a7f795 (b25beb13), not CrossSmudge's own `v1.6.0` tag (vs that tag: 218 files).
- [14] | L146 readme.club: 51 firmwares, CrossPoint ~8k, CrossInk ~1.1k | supported | readme.club/firmware: 51; CrossPoint 8016; CrossInk 1110 |

## Section 5 table

- [5] Folder of loose files | supported | README.md:45-47
- [4] 6 optional strings, no validation | supported | AppPackage.h:88-107
- [9] Compatibility gate none | supported | no version check in LuaRunner.cpp/AppPackage.h
- [5] Device targeting none, apps branch at runtime | supported | README.md:323-327 (has_touch/get_device); catalog has no device field
- [1] Install atomicity none | supported | AppStoreActivity.h:690-786
- [3] Content-Length only, unverified TLS on main path | supported | HttpDownloader.cpp:150, 583-589
- [1] String != | supported | AppStoreActivity.h:1010, 1023
- [1][7] On-device store plus web portal | supported | AppStoreActivity.h; CrossPointWebServer.cpp:390-395
- [1] Raw main, hard-coded | supported | AppStoreActivity.h:31-32
- [9] os open, any SD path writable, no budget | supported | LuaRunner.cpp:140, 1719-1758
- [9] Saves owned by app, raw file I/O | partly | LuaRunner.cpp:1072-1091 | There is also a runtime-managed key/value `save`/`load` in `saveDir_`; apps own saves but not only through raw file I/O.
- [10] 5.4.7, 32-bit | supported | lua.h:21; luaconf.h:125
- [2] Half catalog non-games | supported | catalog.json
- [5] Devices C3 and S3 | supported | README.md:338, 407-408
- [5][9] README disagrees with code | supported | see L118
- B2 rows (AD-16 inbox, 2 M instructions, Lua 5.5.1, AD-23 scope) | supported (spot-check) | ch:_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md:114-118, 140-142, 279-284, 388

## Sections 6-7

- [3][1][9] | L204 three risk factors | supported | as above
- [2][12] | L205 store and portal built before any outside author | supported | catalog.json; GitHub page
- B5 | L205 starter repo deferred | not checked (spot-check budget used on B1, B2, B6)
- [11][2] | L205 10 apps by one person in a week | partly | git log; catalog.json | Apps date back to v1.4.0.1.1 (2026-08-01, "feat: Add applications", native C++); the Lua conversion and store landed in the last week. "Written in a week" overstates; "converted/packaged in a week" fits.
- [1] | L206 per-file loop, half-installed states | supported | AppStoreActivity.h:690-786
- B6 | L207 shared TLS gap | supported | ch:src/network/HttpDownloader.cpp:67-75; ch:platformio.ini:73
- [1][2][3] | L219 basis of rec 1 | supported
- [7] | L223 portal shows install/export in browser | supported | CrossPointWebServer.cpp:393-394
- [1][2] | L226 detail view uses author/description | supported | AppStoreActivity.h:436-438; catalog.json
- [5][9] | L231 docs drifted from code within a day | supported | see L118
- [1][6] | L239 download-handling ideas | supported | AppStoreActivity.h:354-420, 667, 744-749; CHANGELOG.md:72-74
- [2] | L252 half the catalog non-game companions | supported | catalog.json

# Citation check r1-1 (fresh-context verifier, 2026-10-04)

Format: `[n] | sentence (short) | verdict | evidence | note`. Sources fetched live 2026-10-04 (Claude Code docs as .md, CHANGELOG raw, npm registry, F-Droid, Flipper, TUF, Air Security, mpt.solutions, official marketplace.json). GitHub issue pages (#14061, #17361, #33172) blocked in this session (gh API 403, web returns 378-byte stub). Not fetched (budget): [15], [16], [17], [26], [27], [28].

## Executive summary
[1][2] | Catalog is one marketplace.json fetched over HTTPS | supported | host-marketplace/marketplace-reference: `url` source = link to marketplace.json, downloads only that file; loading page covers it | [3] is the more direct page
[1] | archive entry = zip URL + sha256, refuses mismatch | supported | marketplace-reference "archive plugin source": "When you set it, Claude Code refuses a download that doesn't match" | sha256 is optional
[2] | Version = explicit version else sha256 prefix; update only when computed version changes | supported | loading "How Claude Code computes the version": manifest version, then entry version, then archive SHA-256 shortened to 12 chars; update not applied when computed version matches | 
[1][5] | Claude Code does not sign catalogs/packages; relies on HTTPS + hash pins | supported (absence) | no signing in marketplace-reference or security pages; security lists archive integrity check as the integrity mechanism |
B9 | wolfSSL transport replaces cert-checking path; HTTPS unverified | supported | HttpDownloader.cpp L12-19 includes, L66-136 `#if defined(FREEINK_NET_WOLFSSL)` with `http.setInsecure()` at L75, L138-244 `#if !defined(FREEINK_NET_WOLFSSL)` esp_http_client + crt_bundle_attach, L253-260 dispatch; platformio.ini L73 `-DFREEINK_NET_WOLFSSL=1` | line numbers match at HEAD c28df263 (report cites f1413185)
[19][20][29] | F-Droid: signed small root pins hash+size; timestamp and expiry block rollback/stale | supported | [19] "separate entry point, signed by a JAR and a GPG signature"; [20] entry.json has timestamp, maxAge 14, index sha256 + size; [29] freeze/rollback protections | expiry/rollback language is TUF's, not F-Droid's
[19][22] | Third-party catalog added by URL plus key fingerprint | partly | [22] title shows a repo URL with `?fingerprint` query; [19] mentions fingerprints only for the signer index of packages, not the `repo?fingerprint=` add form | [19] does not support the URL+fingerprint form
[24][25] | Flipper: major API must match firmware; one build per app version and API version | supported | [25] "App Loader checks if the app's major API version matches the firmware's major API version"; [24] table rows: version 1.3.5 x SDK 88.2/87.1/86.0 x target f7 |
[21] | F-Droid gates on integer minSdkVersion | supported | fixture: usesSdk {minSdkVersion: 14, targetSdkVersion: 21} |
B2 | integer api level (AD-19) | not checked | internal, out of scope |
[2] | Auto-update off by default for every third-party catalog | partly | loading: default "on for Anthropic's official marketplaces such as claude-plugins-official, off for knowledge-work-plugins and first-party-plugins, on for marketplaces added from claude.ai, and off for every other marketplace" | claude.ai-added marketplaces default ON; some official ones default OFF
[12][14][15] | Update path is where worst bugs and zero-click exploit lived | partly | [14] supports zero-click via background auto-update; [12] unverifiable (blocked); [15] not fetched |

## Section 1
[1] | Required name, owner.name, plugins[]; entry requires name+source; optional description, version, category, tags, displayName, metadata | supported | marketplace-reference field tables |
[1] | Each entry validated on its own; unknown keys ignored | supported | "Each entry is validated on its own, so one invalid entry doesn't fail the marketplace"; "ignores an unknown top-level key or plugin-entry key" |
[1][3] | renames append-only, null = removed | supported | host-marketplace "Treat renames as append-only history"; null when the plugin is gone |
[3] | forceRemoveDeletedPlugins uninstalls, lists under Flagged | supported | host-marketplace L290-294 |
[1][5] | One marketplace per name; reserved names accepted only from github.com/anthropics/ | supported | marketplace-reference L35; security "accepts the official and community names only for marketplaces sourced from github.com/anthropics/" |
[1] | Source types relative, github, url, git-subdir, npm, archive, command | supported | plugin sources table |
[1] | Full 40-char sha; sha wins over ref | supported | "When you set both ref and sha, Claude Code checks out sha" |
[9][10] | archive arrived v2.1.224 on 2026-08-07 | supported | CHANGELOG 2.1.224 "Added archive plugin source"; npm time 2026-08-07T01:36Z |
[1][3] | Download caps 256 MiB/120 s/5 redirects; extraction 100,000 entries, 512 MiB, 1 GiB, 50:1 | supported | host-marketplace download-limit table and extraction list |
[1][3] | URL marketplace fetches only JSON (5 MiB) | supported | host-marketplace table: marketplace.json 5 MiB, 10 s |
[7] | /plugin marketplace add forms | supported | install page table: owner/repo, git URL, local path, https marketplace.json |
[2] | Version resolution order, 12 chars for archive | supported | see above |
[2] | cache/<marketplace>/<plugin>/<version>/, data/ survives, 14-day sweep | supported | loading L169-170, L201 |
[2] | Auto-update on for official, off for every other; catalog cannot turn it on | partly | default list as above; "marketplace.json has no field to turn it on" is in [3] host-marketplace, not [2] | exceptions: claude.ai-added ON, knowledge-work-plugins/first-party-plugins OFF
[3] | No release channels; two catalogs | supported | "Claude Code has no release-channel concept"; host two marketplaces pointing at different refs |
[1][2] | plugin@marketplace | supported | |
[5] | Same warning for every source, quoted text | supported | "The warning reads the same whatever marketplace the plugin comes from" + exact quote |
[6] | strictKnownMarketplaces/blockedMarketplaces, force plugins on/off | supported | org page |
[8] | Anthropic does not review third-party marketplaces | supported | "Anthropic doesn't review third-party marketplaces" |
[4] | validate checks local files only; wrong remote source surfaces only at add/install | partly | the explicit statement is in [1] marketplace-reference L484 ("Errors fetching a source also appear only after install, not in validation"); manifest-reference [4] does not say it | wrong page cited
[11] | 315 plugins; all 262 external entries pin both ref and 40-char sha | partly | live file: 315 plugins, 262 object sources, all 262 have a 40-char sha, only 97 also set ref | "both a ref and" is wrong

## Section 2
[9][10] | Marketplaces shipped v2.0.12 on 2025-10-09 | supported | CHANGELOG 2.0.12 "Plugin System Released"; npm 2025-10-09 |
[9][10] | >200 marketplace/plugin-update entries; fixes up to 2.1.289 (2026-10-03) | supported | 246 CHANGELOG bullets mention marketplace or plugin update/cache/install; 2.1.289 fixes stale copy in plugin list/update; npm 2026-10-03 | count depends on regex
[12][9] | #14061, #17361 still open | not verifiable | GitHub blocked | already flagged unverified
[2][5] | explicit version ships nothing until bumped | supported ([2]) | loading L300 | [5] not needed
[13][9] | #33172 one unknown source broke catalog; since 2.1.120 shows entry and prompts | supported via [9] | CHANGELOG 2.1.120 "Fixed /plugin marketplace failing to load when one entry uses an unrecognized source format - that entry is shown but installing it prompts you to update"; [13] blocked |
[9] | State-corruption fixes Apr-Sep 2026 | supported | 2.1.232 (2026-08-13) startup race unregistering via concurrent writes; 2.1.275 (2026-09-17) marketplace update deleting local copy on failed fetch; 2.1.105 (2026-04-13) auto-update broke official marketplace while files held open | 2.1.275 bug was narrower: only GitHub marketplaces named after their repo
[9] | Name collisions fixed late Sept: same-name replace silent (2.1.284) | partly | 2.1.284: "Improved claude plugin marketplace add to say when it replaces a marketplace already added under the same name ... and how to undo it" | it now warns; replacement still happens, so "fixed" overstates
[9] | ids differing in punctuation/case collided (2.1.285) | supported | 2.1.285 "ids differ only in ., -, @ or (macOS, Windows) capitals; the install is now refused" |
[14][15] | Plugin4Shell 2026-09-17, SHA-named branch, zero-click via auto-update, patched 2.1.179 | supported ([14]) | Air Security post dated September 17, 2026; branch named bbb...bbb set as default; "What makes it 0-click is plugin auto-update"; "Anthropic patched it after our disclosure, in 2.1.179" | 2.1.179 released 2026-06-16, CHANGELOG entry does not mention it; [14] also says background auto-update is "the default" in Claude Code, which [2] limits to official marketplaces; [15] not fetched
[16] | Prompt Security PoC | not checked | budget |
[17] | 2,700+ marketplaces | not checked | budget |
[18] | Practitioner advice: pin SHAs, separate repos, stable/canary, CI generates catalog | partly | blog: "Pin to a commit hash, not a tag"; "Two repos, separated"; "branches with distinct pinned SHAs. Developers opt into canary"; no text found about CI generating the catalog from package manifests (searched generat/CI/pipeline/drift) | CI-generation claim unsupported; canary advice is branches, not necessarily two catalogs

## Section 3 (spot-check)
[19][20] | entry.json signed JAR + GPG; timestamp, maxAge 14, sha256+size, diffs | supported | [19] text; [20] live: maxAge 14, index size 62,626,210, numPackages 4525, 10 diffs |
[20] | 62.6 MB for 4,525 packages | supported | as above |
[21] | file{name,sha256,size}, usesSdk | supported | fixture |
[19] | repo added as https://host/repo?fingerprint=<sha256> | partly | not on the All_our_APIs page; form appears in [22] | 
[22] | fingerprint in stored URL passed at add, failed at install | supported | issue title: "repo failed to verify" on apk install if the repo URL still contains ?fingerprint query string |
[23] | PR submission, commit_sha pin, globally unique id, strictly increasing versions | partly | Manifest.md: commit_sha required; id "Must be globally unique"; bundler script builds the package; no text found on strictly increasing versions or PR submission | already flagged unverified
[23] | catalog builds every package | supported | "used by the bundler script to build the app package ... Archivarius, which manages app builds" |
[25] | major API equals firmware | supported | quoted above |
[24] | one build per app version, API version, target | supported | build table |
[26][27][28] | Homebrew, mip | not checked | budget |
[29] | TUF v1.0.36 (2026-08-05), freeze/rollback/endless-data/hash | supported | spec header "Version: 1.0.36 Last modified: 5 August 2026"; attack list |

## Section 4 (internal)
B7 | streaming JSON parser, 512-byte token buffer | supported | lib/JsonParser/StreamingJsonParser.h L21 `TOKEN_BUF_SIZE = 512`, L59 `char tokenBuf[TOKEN_BUF_SIZE]` | "parse a catalog from SD without holding it in RAM" is inference
B9 | see Executive summary | supported | |

## Section 5 table
[1] | marketplace.json name/owner/entries | supported | |
[1][3] | url source: one JSON over HTTPS | supported | |
[1] | archive url+sha256 | supported | |
[1] | relative, git, git-subdir, npm, command sources | supported | |
[2] | computed version | supported | |
[1] | plugin@marketplace; one marketplace per name | supported | |
[1][5] | reserved official names only from Anthropic sources | supported | |
[1][5] | HTTPS + hash pins, no signing | supported (absence) | |
[19][20][29] | detached signature, monotonic version, expires | supported | |
[5][7] | add by source, generic trust warning | supported | |
[19][22] | add by URL + fingerprint, check at add | partly | [19] lacks the URL+fingerprint form; [22] supports it |
[2] | auto-update per marketplace, off for third parties | partly | claude.ai-added marketplaces default on |
[3][18] | two catalogs for stable/canary | supported ([3]) / partly ([18]) | [18] describes canary branches |
[24][25][21] | builds keyed by api | supported | |
[1][13] | per-entry validation; unknown source prompts | supported | via [1] and CHANGELOG 2.1.120 |
[1][3] | renames, forceRemoveDeletedPlugins, Flagged | supported | |
[2] | version cache, 14-day sweep | supported | |
[4][18] | validate; CI regenerates catalog | partly | [4] supports validate; [18] has no CI-regeneration advice found |
[6] | admin allowlists/blocklists | supported | |

## Section 7
[9][12] | Most trouble in git transport, credentials, cache refresh | partly | CHANGELOG has many git/cache fixes but also many non-git ones; [12] unverifiable | interpretive
[1][9] | archive/URL path is newest and smallest part | partly | archive 2.1.224 (2026-08-07), but `command` source (2.1.229) and metadata.pluginRoot (2.1.239) are newer | "newest" is not accurate
[1][5] | pins in catalog, catalog trusted via HTTPS | supported | |
[14] | Plugin4Shell: pins fail when resolver tricked | supported | |
[19][29] | F-Droid and TUF build on signed metadata | supported | |
[9][12][14] | zero-click, stale cache, state corruption all involve background updates | partly | zero-click and the held-open-files bug involve auto-update; the concurrent-write unregister was a startup race and the deleted-local-copy bug was in manual `marketplace update`; [12] unverifiable |
[13] | Claude Code fails or prompts per entry when client too old | supported via [9] 2.1.120 | [13] itself blocked |
[21][24] | Flipper and F-Droid pick a compatible build from catalog | partly | sources show per-SDK builds and per-version minSdkVersion; neither shows client-side selection | report's own open question admits Flipper selection unread

## Section 8 (basis lines)
B9,[19][29] | rec 1 | supported | |
[1][2][3][19][21][24][25][29],B9 | rec 2 | supported | |
[18][23],P1 | rec 3 CI build-and-sign | partly | [23] supports catalog building packages; [18] CI-generation not found |
[9][12][14] | rec 4 keep known-good catalog | supported via [9] | failed-fetch deletion bug (2.1.275) |
[1][3] | rec 5 author/description, preview catalog | supported | entry description field; channels as two catalogs |

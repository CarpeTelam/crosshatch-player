# SUMI teardown — round 2
Clone HEAD: 1a1c47c (2026-05-30). Corrects one round-1 claim (require gap).

## Findings
- claim: CORRECTION to r1 — SUMI's vendored linit.c is trimmed to base, table, string, math, utf8 only; liolib.c, loslib.c, ldblib.c, loadlib.c and lcorolib.c are not in lib/lua54 at all. So `require`, io, os, debug, package and **coroutine** do not exist; the sandboxGlobals() nil-ing is belt-and-braces. Verified by compiling the vendored sources on host with the same nil list: `type(require)` == nil.
  source: lib/lua54/linit.c, lib/lua54/ file list @1a1c47c; host test (scratchpad/luatest/t.c)
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: No coroutines for script authors (lcorolib absent), so game logic must be written as explicit state machines driven by the 10 Hz update() tick and onButton().
  source: lib/lua54/linit.c @1a1c47c
  publisher: psychoplath9450
  pub_date: 2026-05-30
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: sumi.page/plugins' browser playground uses fengari-web@0.1.4 (Lua 5.3) with the API registered as flat globals (matching firmware, not the repo docs' `sumi.*`); its readFile stub just pushes nil, so file I/O/save-state is not simulated. Its AI prompt says "Lua 5.4 VM on an ESP32 ... All drawing functions are pre-registered globals — do NOT use require()". Templates include a Snake game spec.
  source: https://sumi.page/plugins/ (raw HTML/JS)
  publisher: sumi.page (SUMI author)
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: sumi.page/bridge does not itself document the Lua API or list community plugins; built-in handlers are Text Importer and Doorbell only.
  source: https://sumi.page/bridge/
  publisher: sumi.page
  pub_date: unknown
  accessed: 2026-09-26
  confidence: medium
  class: catalog
- claim: PocketInk rates SUMI "Risk: Medium", "Best for: Tinkerers who want a do-it-all offline device, plus the most genuine 'plugin' story in the scene"; "The only genuine on-device scriptable plugins in the scene"; "A younger project than CrossPoint, and quieter lately: the last release (v0.6.3) landed in May 2026." X3 support partial.
  source: https://pocketink.io/firmware/sumi/
  publisher: PocketInk
  pub_date: unknown (references v0.6.3, so ≥2026-05)
  accessed: 2026-09-26
  confidence: high
  class: sentiment
- claim: ReadMe.club lists SUMI for X4 with 179 stars / 6 forks; no user reviews on the page text. The round-1 "Crosspoint is currently more stable, but SUMI has so much promise" quote could not be traced to a primary page (search-engine synthesis) — treat as unverified.
  source: https://www.readme.club/firmware/sumi
  publisher: ReadMe.club
  pub_date: unknown
  accessed: 2026-09-26
  confidence: medium
  class: sentiment

## Leads worth chasing
- Reddit user voice (blocked for both WebSearch domain filter and reddit JSON from this sandbox).
- Actual on-device idle footprint of the Lua VM inside the 40 KB cap (not measured; host build is 64-bit, not representative).

## Looked for, not found
- Community Lua game gallery or shared-script repository: none found (sumi.page, GitHub, PocketInk).
- Hackster.io article: HTTP 403.
- Reddit threads: inaccessible from this environment.
- Any Lua-specific performance/crash issue other than #20 (file I/O, closed without reply).

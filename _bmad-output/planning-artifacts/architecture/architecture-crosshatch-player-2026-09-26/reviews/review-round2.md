---
title: 'Round-2 review: crosshatch-player v1 game platform spine (revised)'
reviews: '../ARCHITECTURE-SPINE.md'
companion: '../game-api-seed.md'
prior: 'review-adversary.md, review-rubric.md, review-reconcile.md, review-verify.md'
lens: 'Did the revision close round 1; what new integration holes, internal contradictions, spine/seed conflicts, and unenforceable rules did it introduce. Facts checked against the repo at 3aee15c.'
created: '2026-09-26'
verdict: 'Nearly a build substrate. 10 of 14 round-1 high/blocking findings closed, 4 partly, 0 open. Three new high holes must be fixed before fan-out: the seat used for the hidden pass-and-play result frame, how script sources reach a VM that may not touch Storage, and the upstream frontlight push that stalls the match loop and the radio link.'
---

# Round-2 review

## Verdict

**Fix three things, then fan out.** The revision took round 1 seriously. The wire protocol, roster, codec bytes, package identity, the VM task protocol, and the player journey are now decided and consistent with the repo. What remains is narrower:

- The hidden pass-and-play hand-off contradicts the seat rule.
- The VM has no sanctioned way to read its own `.lua` files.
- An upstream gesture pushes an activity over the match, which is the exact failure AD-20 was written to prevent.

The rest are medium and low fixes of one to three lines each.

Counts for round-1 high/blocking: **closed 10, partly 4, open 0.**

## 1. Round-1 high/blocking findings

| Finding | Status | Note |
| --- | --- | --- |
| Adversary H1: link `seq` vs snapshot `ver` | closed | AD-13 splits them. `ver` is monotonic across rematch, and `STATE` is latest-wins. |
| Adversary H2: teardown deadlock, lost ABORT, cancel vs error | closed | AD-5: no `RenderLock`, no `ActivityManager` or `Storage` calls from the VM; frame mutex plus `frameGen`; `Cancelled` ≠ `ScriptError`. Residual: N14. |
| Adversary H3: pushed screens vs a NearbySession pumped from the match loop | **partly** | Views live inside the match (AD-20), but upstream still pushes `FrontlightPanelActivity` over any activity (N3). Where the exit sequence runs is also unstated (N9). |
| Adversary H4: two seat owners; solo and game-over seats | closed | AD-11 roster. Residuals: N1, N15, and seat-0 `ui` (N12). |
| Adversary H5: manifest parser, validity, hash, commit, inbox | closed | AD-15/16. Residuals: N16, N17. |
| Adversary H6: codec bytes unspecified | closed | AD-10 v1 layout, version in `proto`, headers. Residual: encode is not canonical (N11). |
| Rubric F1: VM teardown and cross-task rules vs `RenderLock` | closed | AD-5, AD-7 lock order, AD-18 receive ring buffer. |
| Rubric F2: count hook doesn't bound time spent in C | closed | Arena plus abandon after 500 ms (AD-5/6). Residual: N14. |
| Rubric F3: upstream-touch cap already exceeded | **partly** | The 9-file ledger, reserve slot, CI job, and `.clang-format` shield landed. Still missing: the Home icon enum, the cppcheck suppression, and a CI check that survives upstream syncs (N8). |
| Rubric F4: no lifecycle AD | **partly** | AD-21 exists, but its "every transition not shown is invalid" diagram forbids required transitions (N4), and it contradicts AD-17 on sleep writes (N6). |
| Reconcile A1: 10-file cap infeasible | **partly** | Made feasible by dropping the web page. Same residuals as F3 (N8). |
| Reconcile A2: restaurant test has no owner | closed | AD-22 plus AD-21 (pause on Back/Home, end-of-round menu, single-Back error view). |
| Reconcile A3: bytecode hole | closed | AD-6: text mode only, checked at install and load. |
| Verify 1: 8 KB VM stack unproven | closed | 16 KB, plus a worst-case C-stack re-run before API level 1 freezes (AD-4/5). |

Round-1 mediums, briefly:

- **Closed:** H7, H8, H9, H11, H13, H14, H15, F5–F11, A4, A5, A7, A8, A9, V2, V3.
- **Partly:**
  - H10 (the Quick Resume fast refresh ghosts; N10)
  - H12 and F5 (`ch.store` byte ownership; N6)
  - A6 (the power lock collides; N5)
  - A10 (`text_width` and seed guidance; N7, N12)
  - V4 (the icon conversion step isn't stated; N17)

## 2. New or remaining holes

Severity: **high** = two units that obey every AD integrate incompatibly, or the spine contradicts itself on a first-party-game path. **Medium** = real divergence or rework with a cheap fix. **Low** = a small gap.

### N1 (high): hidden pass-and-play result frame: which seat draws it?

- AD-12: after an accepted turn-changing move, "the mover sees the result frame" with a runtime "Pass to player N" control.
- AD-11 (pass: "local input is `status.turn`"), AD-8, and the seed ("in `pass` mode [`seat`] is the seat whose turn it is") all say `draw` gets the *new* turn seat once the move is applied.
- **Result.** Unit A follows AD-11 and draws `draw(state, 2, ui2)` with the Pass control on top, so the mover sees seat 2's secret board (Battleship's ships). Unit B follows AD-12 and draws seat 1, breaking the seed's documented contract. Nothing says whether `input` runs during this frame, or where the Pass control sits on a canvas "the script owns whole" (AD-7).
- **Fix (AD-12, AD-8, AD-21, seed):**
  - In pass mode with `hidden`, after a turn-changing move the runtime draws and delivers input with `seat = mover` until Pass is tapped, and discards any returned moves.
  - Add a `Result` state to AD-21: `Playing → Result → HandOff`.
  - The Pass control lives in a runtime band that `GameViewport` excludes in pass+hidden (`ch.screen` is constant for a match), or make it "tap anywhere".
  - Update the seed's `seat` row to match.

### N2 (high): script sources have no path into the VM

- AD-5: the VM task "never touches `Storage`". AD-6: "no binding opens files". AD-15: `require("name")` loads `name.lua`.
- **Result.** The VM-host author cannot run `luaL_loadbufferx` on `main.lua`, or serve `require`, without reading the SD card from the VM task. One author reads files there anyway (breaking AD-5, and SdFat isn't thread-safe unless every access goes through `Storage`). Another expects the match to hand sources over. The same gap exists for the initial `store.bin` and a resume snapshot.
- **Fix (AD-5/AD-15):**
  - Before creating the VM, the match (loop task) reads every `*.lua` member of `/.games/<id>/` into one PSRAM source blob. AD-15 already caps the total at 256 KB.
  - It passes the blob with a `name → span` table, plus the `store.bin` blob and the optional resume snapshot, through a `GameScript` port.
  - `require` resolves only against that table, and chunks load with mode `"t"` from memory.

### N3 (high): upstream pushes `FrontlightPanelActivity` over the match and stalls the link

- `ActivityManager::loop()` (`src/activities/ActivityManager.cpp:115-128`) runs `pushActivity(FrontlightPanelActivity)` on `mappedInput.wasLightPanelGesture()`, which is a top-edge down-swipe on frontlit boards (the X4 Pro). It does this for every activity except the panel itself, *before* activity input, and there is no override hook (unlike `handleHomeGesture()`).
- A Push suspends the match's `loop()`. The Structural Seed pumps `NearbySession`/`ReliableLink` from that loop, so ACKs, resends, and PINGs stop, and the peer drops the match after 10 s.
- This is what AD-20's "Prevents: a pushed screen starving the radio link" and the convention "Menu … belong[s] to the runtime" rule out, and the seed tells authors that swipe "belongs to the device".
- **Fix (pick one and record it):**
  - (a) Pump `NearbySession` (ring-buffer drain plus `ReliableLink` timers) from its own small task or `esp_timer`, independent of `Activity::loop()`, and post only session events to the match. This changes AD-18 and the task-view seed.
  - (b) Spend the reserve ledger slot on an `Activity::handleLightPanelGesture()` hook: `Activity.h`, plus `ActivityManager.cpp`, which is already row 5.
  - Either way, update the seed's gesture paragraph: on X4 Pro, Menu opens the light panel, and in nearby it must not stall the match.

### N4 (medium): the AD-21 state machine forbids transitions the design needs

"Every transition not shown is invalid", but these are missing:

- **`Paused → PeerGone` and `Paused → Error`.** The host's VM keeps applying guest moves while the host is paused, and an `ABORT` can arrive at any time.
- **`Over → PeerGone`.** The guest tapping Leave at round end is the most common way a nearby match ends.
- **`HandOff → Paused`.** Back or Home during the blank screen.
- **`Over → HandOff` for pass+hidden Play again.** AD-12 requires a hand-off before the first draw after `setup`, but the diagram goes `Over → Playing`, so the two ADs contradict.
- **`Lobby → [*]` and `Lobby → PeerGone`.** Cancel, or a guest leaving the lobby.
- **Sleep from any state.**

**Fix:** add these edges, or add one rule: "`ABORT`/silence → `PeerGone`, `ScriptError` → `Error`, and sleep → exit apply from every non-terminal state". Also add the `Result` state from N1.

### N5 (medium): `HalPowerManager::Lock` is single-holder, and the render task already takes one per frame

- `HalPowerManager::Lock::Lock()` (`lib/hal/HalPowerManager.cpp:162-177`) logs `"Lock already held, ignore"` and becomes a no-op if another `Lock` exists.
- `renderTaskLoop()` takes a `Lock` around every `render()` (`ActivityManager.cpp:72`).
- **Result.** AD-5's "match holds a `HalPowerManager::Lock` while a callback is running" either:
  - is silently invalid, because a render held the lock first; when that render finishes the mode returns to `None`, and after 3 s idle the CPU drops to 80 MHz mid-callback; or
  - makes every render during a callback log `LOG_ERR`.

  The `Lock` is also non-movable, so holding it across `loop()` calls needs an optional member.
- **Fix:** drop the `Lock` from AD-5. Instead the match overrides `Activity::skipLoopDelay()` to return true while a callback is in flight. `main.cpp:805` then calls `setPowerSaving(false)` on every loop pass. No upstream touch. (In nearby, Wi-Fi already forces full clock, per `HalPowerManager::setPowerSaving`.)

### N6 (medium): `ch.store` persistence contradicts itself and has no byte owner

- AD-17 Prevents "SD writes … on the sleep path", but its Rule writes a dirty store "at round end, match exit, and sleep". AD-21 says "Sleep: `solo` and `pass` are already saved".
- `set` runs on the VM task. If the VM is stopped or abandoned before the loop task writes, a writer that asks the VM for the bytes gets nothing.
- **Fix (AD-17):**
  - `ch.store.set` encodes on the VM task and posts the blob to a latest-wins PSRAM slot owned by the match and `GameSaveStore`.
  - The store is marked dirty only when the encoded bytes differ.
  - Choose one sleep behaviour and edit Prevents to match: either allow this one small write in `onExit` (`enterDeepSleep` already writes the SD card after `goToSleep`, at `main.cpp:279-286`), or never write on sleep and accept up to 5 s of loss.

### N7 (medium): `ch.gfx.text_width` breaks AD-5 and the layering, and can't be used in `input`

- The seed puts `text_width` under `ch.gfx`, so AD-7 makes it an error outside `draw`. `input` then can't hit-test text layout.
- Measuring on the VM task needs `GfxRenderer` metrics, which `GameScript` may not include. `GfxRenderer` can also fault SD fonts in (`GfxRenderer.cpp:115-152`, `ensureSdCardFontReady`), which is `Storage` access from the VM task.
- **Fix:**
  - Map `small`/`medium`/`large` to built-in flash fonts only.
  - The loop task builds per-size advance tables for the supported glyph range and passes them into `GameScript` at VM start through a port.
  - `text_width` becomes pure and callable in any callback: move it to `ch.text_width`, or exempt it from the draw-only rule.

### N8 (medium): upstream-touch ledger gaps

- **(a) Icon.** List-mode Home icons are `UIIcon` enum values (`src/components/themes/BaseTheme.h:131`), and only `LyraTheme.cpp` (`iconForName`) maps them to bitmaps. A new Games icon costs two unlisted upstream files, one more than the reserve allows. **Fix:** rule that list mode reuses an existing `UIIcon` (for example `Blocks`), and cover-grid mode uses a bitmap from a new fork header in `src/components/icons/`. Otherwise add both files to the ledger now.
- **(b) cppcheck.** `pio check` analyzes `lib/`: vendored `lib/expat/xmlparse.c:1280` carries an inline `cppcheck-suppress`. CI runs `--enable=all --check-level=exhaustive` with fail-on-defect low. Unmodified Lua 5.5.1 can't carry inline suppressions. **Fix:** add `--suppress=*:*/lib/lua/*` to the common `check_flags` in `platformio.ini`. That is not "env-scoped" as ledger row 1 claims; amend the row.
- **(c) The CI check as worded.** "Fails any PR that changes an upstream file not on it" fails every upstream sync-merge PR, and it trips on pre-existing fork deltas (`AGENTS.md`, `.gitattributes`, the `CLAUDE.md` removal). **Fix:** define the check as "paths that differ between `merge-base(HEAD, upstream/develop)` and `HEAD` and exist in `upstream/develop` ⊆ ledger ∪ baseline allowlist". The job fetches `upstream/develop` with full history, because clones here are shallow.
- **(d) Engine exclusions.** AD-4's excluded modules (`lua.c`, `luac.c`, `linit.c`, `liolib.c`, `loslib.c`, `ldblib.c`, `loadlib.c`, `lcorolib.c`) are only excluded if `lib/lua/library.json` has a `srcFilter`. Name that fork-owned file in the seed tree, together with the matching source list for `test/game_script`.

### N9 (medium): AD-20 fixes the exit order but not where it runs

- On the sleep path, and on any forced Replace, the sequence runs in `onExit()` under `RenderLock` (`ActivityManager.cpp:184-193, 222-225, 312-315`).
- An 800 ms flush plus a 500 ms join means about 1.3 s with the render task blocked, and the link must be pumped from inside `onExit`.
- The spine also doesn't say where Leave navigates.
- **Fix:**
  - User exits (Leave, Back from Error or PeerGone) run the sequence from `loop()` in a `Leaving` state, then call `goToGames()`.
  - A forced `onExit` does the best-effort path only: send `ABORT` once without flushing, cancel then join or abandon, radio off, blank hand-off.

### N10 (low-medium): the Quick Resume sleep leaks hidden information through ghosting

- AD-12 draws the blank screen into the framebuffer in `onExit`.
- Quick Resume's `SleepActivity::renderLastScreenSleepScreen()` then pushes that buffer with a **FAST** differential refresh (`SleepActivity.cpp:878-888`), which ghosts the secret board.
- **Fix:** in pass+hidden, the match's `onExit` calls `renderer.displayBuffer(HALF_REFRESH)` or FULL itself after drawing the blank. It is on the loop task holding `RenderLock`, the same pattern `SleepActivity` uses when it draws from `onEnter`.

### N11 (low): the codec encoding isn't canonical

- AD-10 leaves the order of the `nrec` pairs to `lua_next`, so the C and Python encoders can't produce byte-identical golden vectors for tables with string keys. Only decode vectors are testable.
- **Fix:** sort the record keys: integers ascending, then strings bytewise.

### N12 (low): seed guidance invites double counting; seat-0 `ui` is undefined

- The seed says "record per-player results in `draw` or `input`". But `draw` runs after every snapshot, every input, every hand-off, and every resume, so `wins = wins + 1` in `draw` over-counts.
- AD-8 gives one `ui` per local seat, but pass mode at game over calls `draw(state, 0, ui)` with no seat-0 table defined.
- **Fix:**
  - Deliver a one-shot `input` event `{kind = "over"}` per local seat per round (additive to the event list), and point the seed at it.
  - Define seat 0's `ui` as a separate shared table.

### N13 (low): the simulator can't show any game screen

- `.claude/skills/run-crosshatch-player/simulator.ini` (fork-owned) doesn't set `FREEINK_CAP_GAMES`. The native build has no ESP-NOW and no mbedTLS (it links `-lssl -lcrypto`).
- **Fix:** add one line saying the simulator envs set the flag; `EspNowLink` and nearby are compiled out under `SIMULATOR`; and the `Sha256` helper has an OpenSSL branch there. No ledger cost.

### N14 (low): abandonment residue

- `Session` is confined to the VM task, but it allocates outside the arena through `makeUniqueNoThrow`, so it leaks when the VM is abandoned.
- Abandoning while the VM holds the frame mutex (during the swap) would block the render task under `RenderLock` forever.
- **Fix:** allocate `Session` and codec scratch from the arena, or have the match own them. Abandon only when an atomic `inSwap` is clear.

### N15 (low): seats are assigned "in `ACCEPT` order"

- `ACCEPT` goes to every guest at once when the host starts, so it has no order.
- **Fix:** assign seats in `JOIN` arrival order and carry the seat in `ACCEPT`.

### N16 (low): `lib/ZipFile` enumeration gaps for the whitelist and the caps

- `enumerateFileEntries` silently skips names of 256 characters or more (`lib/ZipFile/ZipFile.h:128-133`), and its callback gives only `crc32` and `compressedSize`. `zipDetails.totalEntries` is private.
- **Fix:** the installer reads the EOCD entry count itself and rejects the package when it differs from the enumerated count. It checks uncompressed size per member with `getInflatedFileSize`, and computes CRCs over the `readFileToStream` output (`crosspoint_mz_crc32`).

### N17 (low): the icon conversion step isn't in AD-16

- The SD layout lists `icon.bmp`, but AD-16 never converts it.
- **Fix:** the installer converts `icon.png` to a BMP of fixed size N with `PngToBmpConverter` (`pngFileTo1BitBmpStreamWithSize`). A failed conversion is `Invalid`. The package hash still covers `icon.png`.

## 3. Checked and consistent (no action)

- `onExit()` and the destructor run under `RenderLock` on Replace and on sleep (`ActivityManager.cpp:184-193, 222-225, 312-315`). AD-12's "without taking it again" is correct.
- `handleHomeGesture()` exists (`Activity.h:53`) and is consulted before `goHome()` for non-Home activities (`ActivityManager.cpp:105-110`). AD-21 works as written.
- `silentRestart()` is already a no-op once sleep starts (`main.cpp:179`), so AD-18's restart can't pre-empt deep sleep.
- Ledger rows 4–9 match the Home code: the `HomeMenuItem` enum, `goHome` name mapping, `menuItemToIndex`/`indexToMenuItem`, `count = 4` with the menu vectors, and `tabItems` as `std::array<…, 5>` with the `ICONS[]` index math. The one exception is the icon enum (N8a). Upstream spells enumerators in SCREAMING_CASE, so use `HomeMenuItem::GAMES`.
- The `lib/lua/.clang-format` shield works, because `bin/clang-format-fix` runs `-style=file`.
- `lib/Memory` and `lib/JsonParser` are standard-library only, so the GameCore dependency is valid. `HalStorage` has `rename`, `removeDir`, and `readFileToStream`.
- The six envs exist, and the sticky `firmware_tuned` selective compilation keeps the ESP_NOW library.
- Wi-Fi being on already forces full clock (`HalPowerManager::setPowerSaving`).

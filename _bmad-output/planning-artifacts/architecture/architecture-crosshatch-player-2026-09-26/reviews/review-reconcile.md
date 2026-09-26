---
title: 'Reconciliation review: architecture spine and API seed vs brief, addendum, spike'
reviewed:
  - 'ARCHITECTURE-SPINE.md'
  - 'game-api-seed.md'
against:
  - '_bmad-output/planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/brief.md'
  - '_bmad-output/planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/addendum.md'
  - '_bmad-output/planning-artifacts/research/spike-script-engine-2026-09-26.md'
created: '2026-09-26'
---

# Reconciliation review

**Verdict:** mostly landed. The spike's recommendation and almost all of the addendum's radio and sandbox detail made it into the spine. Before this is a build substrate, fix three things: the upstream-touch cap looks infeasible as designed, the restaurant test has no architectural owner, and the sandbox has a bytecode hole. The seed also has a broken example and disagrees with the spine on when saves are discarded.

Scope: this lists only things that did **not** land, landed wrongly, or contradict. Items that landed cleanly are omitted. Repo facts cited were checked in this clone on 2026-09-26.

Severity: **high** = blocks a success criterion or a stated invariant; **medium** = an unowned requirement, or a disagreement that will cause rework; **low** = wording, a missing detail, or a nice-to-have.

---

## A. Input requirements the spine or seed misses, gets wrong, or contradicts

### A1. HIGH: the 10-file upstream-touch cap is probably infeasible as designed (success criterion "Clean upstream merges")

- **Source:** brief, Success Criteria ("changes to upstream files stay under a cap set during architecture"); brief, What Makes This Different ("Fork discipline").
- **Spine:** AD-3 sets the cap at 10 files. Deferred says a Home menu item "costs about 6 of the 10 upstream files", and it leaves the Games entry open.
- **Problem:** the spine already commits to touches that nearly use up the budget before any launcher entry exists:
  - `platformio.ini` (the `FREEINK_CAP_GAMES` flags): 1
  - `lib/I18n/translations/english.yaml` (the `STR_GAMES_*` keys): 1
  - `src/network/CrossPointWebServer.cpp`, and probably `.h`, for the `/games` and `/api/games*` routes (fonts precedent at lines 181–184): 1–2
  - The web nav: `FilesPage.html`, `FontsPage.html`, `HomePage.html`, and `SettingsPage.html` each hard-code the nav links (`/files`, `/settings`, `/fonts`), so a linked Games page touches 4 more upstream files.
  - `test/CMakeLists.txt` lists suites with explicit `add_subdirectory`, so `test/game_core` and `test/game_script` touch it (the operational envelope promises CI runs these suites): 1
  - Subtotal: about 8–9 files. Add the spine's own estimate of about 6 for a Home entry and v1 lands at about 14–15, which breaks AD-3 on day one.
- **Also missing:** nothing enforces the cap. The ledger `docs/crosshatch/upstream-touches.md` is manual, and no CI check compares the diff against `upstream/develop`.
- **Fix:** enumerate the planned ledger in the spine now, file by file. Then either:
  - (a) pick a design that fits: no web nav edits (the Games page is reached from a link on the Games launcher or from `/fonts` only), and a single guarded Home hook; or
  - (b) set the cap from the enumerated list, for example 12, and state which entries it reserves.

  Either way, add a small CI step that counts the files changed against `upstream/develop` outside the fork-only directories.

### A2. HIGH: the restaurant test has no architectural owner (success criterion; audience "kids at a restaurant … no help needed")

- **Source:** brief, Success Criteria ("A child can launch a game and finish a round without adult help"); brief, Who This Serves ("pick up the device, choose a game, play alone or pass it across the table, with no phone and no help needed").
- **Spine:** no AD, convention, or capability-map row references this criterion. The specific gaps:
  - **Launch.** Where the Games entry lives is deferred as "Open". A child can't launch what they can't find, so this is the first step of the criterion, not a detail.
  - **Finish a round.** The AD-8 contract ends at `status → {over = true}`. Nothing says what happens next: who shows the result (runtime or script), and whether there is a "Play again" (which re-runs `setup` on the authority, and in `nearby` rebroadcasts) or only "Back to games". Without a runtime-owned end-of-round flow, every game invents its own, or the child is stuck on a final board.
  - **Accidents.** Edge gestures always exit (Consistency Conventions: Input events). In `nearby`, where AD-17 saves nothing, one stray edge swipe by a child ends the match for both devices. The spine has no confirm-on-exit rule.
  - **Failure.** AD-14's error screen shows "the game name and the Lua message". That is right for the author, but a child needs one obvious way back to the launcher. The spine also doesn't say whether the solo/pass autosave from before the failing call survives, so the player can resume.
  - **Idle.** A child thinking, or a device mid-pass, can hit auto-sleep in `solo` or `pass`. The resume offer covers this only if wake returns to the game or to a resume prompt, and the spine doesn't say which.
- **Fix:** add an AD, for example "AD-20: Player journey".
  - Home to the game list in one tap, and the list to play in at most two more (mode picker skipped when only one mode applies).
  - A runtime-owned end-of-round screen built from `status.winners`, with Play again and Back.
  - Confirm before an edge gesture ends a `nearby` match.
  - A child-safe error screen with a single Back action.
  - A resumed save is offered on wake.

  Then resolve the Games-entry question inside the A1 budget.

### A3. HIGH: the sandbox doesn't forbid precompiled bytecode, which undoes "a script error shows an error screen instead of crashing"

- **Source:** brief, The Solution §1 ("A script error shows an error screen instead of crashing the device"); spike, Carry into v1 item 5 (bytecode "loaded only from trusted first-party packages").
- **Spine:** AD-6 removes `load`, `loadfile`, and `dofile`, and adds a package-jailed `require`. Deferred lists "bytecode precompile". Nothing says the runtime's own loads (of `main.lua` and `require`d modules) must refuse binary chunks. `luaL_loadbufferx` with a NULL or `"bt"` mode accepts Lua bytecode, and PUC Lua doesn't verify bytecode, so a malformed or hostile `main.lua` in a `.cpgame` can corrupt memory and crash the device. It never gets near `lua_pcall`.
- **Fix:** AD-6: "Every chunk load (main and `require`) uses mode `"t"`; binary chunks are rejected at install and at load." If the bytecode follow-up is adopted later, it gets its own trusted path (for example, only packages built into the firmware image).

### A4. MEDIUM: refresh control narrowed to whole-frame hints; "which parts of the screen refresh and when" is lost

- **Source:** brief, The Solution §1 ("control over which parts of the screen refresh and when"); addendum, gaps in SUMI ("refresh control (dirty regions and requesting a full refresh)").
- **Spine:** AD-7 has only a per-frame hint (`fast`, `half`, `full`). There are no dirty regions, and the runtime doesn't promise to diff frames and refresh only a bounding box. "When" is also unowned, because nowhere does it say when the runtime calls `draw`. The seed says "The screen shows what you drew when `draw` returns" but never says what triggers `draw`: every input event, only a state change, or a change to `ui`. Every trigger costs about 0.67 s of panel time (spike) and battery, and the rule decides whether a cursor move in `ui` redraws at all.
- **Fix:** AD-7 or AD-8:
  - (a) Define the draw triggers: after setup or resume, after each accepted snapshot, and after any `input` call. Optionally let `input` return `nil, false` to mean "no redraw".
  - (b) Either add `ch.gfx.refresh(mode, x, y, w, h)` or commit the runtime to diffing successive display lists and refreshing their bounding box. Record whichever is chosen as the answer to the brief's "which parts".

### A5. MEDIUM: turn order moved from the runtime to the script, contradicting the brief's split of ownership

- **Source:** brief, The Solution §2 ("The script owns the rules and a small, serializable game state; the runtime owns turn order and the ESP-NOW radio link"); addendum, "Keep turn order and the lobby N-seat-shaped".
- **Spine:** AD-8 and AD-11 have the script decide whose turn it is (`status(state).turn`), and the runtime only enforces it. That is defensible, since Dots and Boxes needs extra turns, but it reverses a stated split and isn't recorded as a deliberate deviation.
- **Fix:** mark it in AD-11 as a deliberate change from the brief, with the reason. Or keep the brief's model: the runtime rotates seats by default, and `status` may override the rotation.

### A6. MEDIUM: the spike's 240 MHz assumption isn't carried; the speed budget can silently triple

- **Source:** spike, Workload → CPU clock ("held at full clock with `HalPowerManager::Lock` … 80 would mean power saving slipped in"); Carry into v1 item 4 ("about 2–3M instructions per second").
- **Spine/seed:** AD-6 budgets 2 M instructions per callback, and the seed promises "about 2 million … roughly one second". `lib/hal/HalPowerManager.h` drops to `LOW_POWER_FREQ = 80` MHz after `IDLE_POWER_SAVING_MS = 3000`. After a child thinks for more than 3 s, a budget-sized `apply` (with a computer opponent) takes about 3 s instead of about 1 s, unless the VM task raises the clock. The spine never says who holds the lock.
- **Fix:** AD-5: the `GameVM` task holds a `HalPowerManager::Lock` around each trampoline call, outside any Lua frame (the lock is RAII, and AD-6 bans RAII across Lua calls), and releases it between callbacks.

### A7. MEDIUM: `LUA_32BITS` is deferred as "tuning", but it is an API and save-format decision that AD-4 locks out

- **Source:** spike, Carry into v1 item 5 (`LUA_32BITS` "needs a one-line `luaconf.h` edit").
- **Spine:** AD-4 requires the engine to be "byte-for-byte unmodified" and `static_assert(sizeof(lua_Integer) == 8)`. Deferred calls `LUA_32BITS` tuning. But integer width is visible to game authors (overflow, `//`, `math.maxinteger`) and to the AD-10 codec (integers in snapshots, `ch.store`, and saves). Changing it after API level 1 ships breaks saves and possibly games.
- **Fix:** run the one measurement before API level 1 freezes. Or state in AD-4 and AD-19 that integers are 64-bit for API level 1, and that `LUA_32BITS` is off the table until a new `api` level.

### A8. MEDIUM: shipping first-party games to devices is unspecified (success criterion "The API proves itself"; the restaurant test)

- **Source:** brief, Success Criteria ("At least three first-party games ship as scripts"); Scope ("A few first-party scripted games").
- **Spine:** it has `games/<id>/` sources and `scripts/pack_game.py`, but no delivery. Are the `.cpgame` files release assets? Are they embedded in the firmware image and extracted on first boot, which would bend "games are content"? Or does each user upload them? A child-ready device needs games already installed. The capability map row also doesn't name which three games cover solo puzzle, open 2P, and hidden 2P.
- **Fix:** add a row to the operational envelope, for example "first-party `.cpgame` files attached to each GitHub release; the README install step copies them to `/games/`". Name the three v1 games, for example Minesweeper or Sudoku, tic-tac-toe or Dots and Boxes, and Battleship or Hangman.

### A9. MEDIUM: install-by-upload lifecycle gaps ("validates it and lists it … with no reboot")

- **Source:** brief, The Solution §3; addendum, font-pipeline precedent (validate name, filename, and magic; install; refresh the registry).
- **Spine:** AD-16 covers the installer, but:
  - "No reboot" is only implied by "registry is a scan". It isn't stated as an invariant, and the spine doesn't say when the launcher rescans.
  - The validation checklist isn't enumerated: zip magic, manifest schema, `id` regex, `main.lua` present, total and per-file size caps, text-only chunks (A3), and api level.
  - For the SD inbox (`/games/*.cpgame`), nothing says what happens to the file after a successful install (deleted? moved?). If it stays, every launcher open re-hashes and re-extracts it. Nothing says where an invalid inbox file's error is shown, or what happens to that file.
- **Fix:** add to AD-16:
  - "Install and removal update the launcher without a reboot; the launcher rescans on entry."
  - A validation list.
  - "A successful inbox install deletes the source; a failure renames it `*.cpgame.bad` and the launcher shows the reason once."

### A10. MEDIUM: AI-authorability gaps in the seed (brief: "documented well enough for an AI to write against")

- **Source:** brief, Who This Serves (Later); Scope ("API docs written for developers working with AI"); addendum, Later: game starter repo.
- **Seed gaps an AI will trip on:**
  - **Solo and seats semantics.** `seats.min = 1` with `modes` including `solo` implies one seat, but a two-player game with a computer opponent needs to say "the computer wins". `winners` holds only seats, so a computer win can only be expressed as a draw (`winners = {}`) or as a phantom seat 2. Define it.
  - **Standard library surface.** The seed lists what's missing as `io`, `os`, `debug`, `load`, `loadfile`, and `dofile`, but AD-6 also drops `coroutine`, `utf8`, and `package`. An AI will reach for `os.time()` to seed randomness, `utf8.len`, or coroutines. The base library (`ipairs`, `pcall`, `setmetatable`, `print`) goes unmentioned, and `print` goes to the raw UART, bypassing `LOG_*`. Either map `print` to `ch.log` or remove it.
  - **Randomness.** The seed says `math.random` is "safe" (for sync) but not seeded well. Lua 5.4 seeds from `time()` and an address, and these devices may have no wall clock at boot, so it can repeat across boots (the same Hangman word every time). Fix it in the runtime: seed from `esp_random()` at VM creation, and document that.
  - **Coordinate origin, and when `draw` runs.** See A4.
  - **Only one example, and it's broken.** See B2. There's no hidden-information or solo example, though the brief makes both success criteria. An AI copies examples.
  - **The author loop.** How to see `ch.log` output and error text, and how fast the reinstall-and-retry loop is. The brief's goal is "idea to playable in an evening".
- **Fix:** add these to the seed. Consider shipping a LuaLS `---@meta` stub for `ch` alongside `game-api.md`, which gives AI assistants a machine-readable surface. It moves to the starter repo unchanged.

### A11. LOW: hidden-information hand-off has UX holes

- **Source:** brief, The Solution §2 (the hand-off screen for Hangman and Battleship); addendum, Other constraints (hidden information).
- **Spine:** AD-12 fires the hand-off as soon as the turn seat changes. So in Battleship, the moving player never sees whether their shot hit before the screen blanks. Resuming a saved hidden `pass` game (AD-17) must also go through the hand-off screen, or the resume draws the wrong player's secrets on a cold screen. Neither case is covered.
- **Fix:** AD-12: the hand-off shows after the mover confirms (the runtime shows "Done, pass the device"), and every resume of a hidden `pass` game starts with the hand-off.

### A12. LOW: grayscale is promised, but the renderer does gray fills by dithering in BW mode

- **Source:** brief, The Solution §1 ("grayscale drawing"); addendum, gaps in SUMI ("grayscale and bitmaps").
- **Spine:** AD-7 gives four levels. `GfxRenderer` has `fillRectDither` for gray in BW mode, and true grayscale needs the separate `GRAYSCALE_LSB/MSB` passes, `displayGrayscaleBase`, and preconditioning. The spine doesn't say which one `light` and `dark` map to under each refresh hint. Bitmaps are deferred, a deliberate trim of an addendum gap that should be recorded as a deviation from the brief, not only listed as "can wait".
- **Fix:** AD-7: "`light` and `dark` render dithered under `fast` and `half`, and true gray only under `full`" (or whatever the renderer supports). Note the bitmap deferral as a brief deviation.

### A13. LOW: the spike's conditions aren't carried: platform, version, stack, priority

- **Lua version.** The spike vendored and measured 5.4.7 (SHA-256 pinned). AD-4 and Stack choose 5.4.9, which the spike never ran. Deferred says to re-run the harness before switching to 5.5.1, but it doesn't apply the same rule to 5.4.7 → 5.4.9.
  - **Fix:** re-run the harness on the vendored version, record its tarball SHA-256 in AD-4, or stay on 5.4.7.
- **Sticky is unmeasured.** Every device number comes from the X4 Pro. `sticky` builds with `firmware_tuned`'s custom sdkconfig (`CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n`, a different Arduino core build), which may change ESP-NOW latency and PSRAM behaviour. A mixed X4 Pro–Sticky match is also unmeasured.
  - **Fix:** extend the radio open item to cover both boards and a mixed pair.
- **Stack.** The spike's 3.3–3.7 KB high-water covers its glue only. AD-5 also puts `GameCore::Session` and the recursive codec (depth 16) on the same 8 KB task.
  - **Fix:** re-measure the high-water with Session and codec in place before freezing 8 KB. The spike's priority 1 isn't stated either.
- **Flash budget.** The spike reports 86.3% of the app slot used by the baseline and +124 KB for Lua alone. The spine sets no flash or internal-RAM budget for the whole runtime (Lua, GameCore, GameScript, screens, and the web page).
  - **Fix:** add an envelope row, for example "runtime ≤ 300 KB flash; internal free-heap low-water ≥ N KB during a `nearby` match".

### A14. LOW: fake-link test harness from the addendum is only implied

- **Source:** addendum, Patterns to borrow ("a fake-link host test harness that drops, delays, and reorders packets").
- **Spine:** AD-1 says every `GameCore` unit gets a GoogleTest suite, and Deferred says "GameCore host tests cover link logic", but the drop/delay/reorder harness isn't named.
- **Fix:** name a `FakeLink` (implementing `ILink`) in the Structural Seed and AD-1.

### A15. LOW: fork-discipline constraints from the brief and AGENTS.md aren't restated

- **Source:** brief, Constraints ("The fork never moves the `freeink-sdk` submodule pointer"); AGENTS.md (no `.skills/` edits).
- **Spine:** it never mentions the SDK. The input model depends on FreeInkUI's `InputSnapshot` (`long_press`, `swipe`), and any gap there would tempt an SDK patch.
- **Also:** AD-2 guards only *includes in upstream files*. Fork `.cpp` files under `src/games/` and `src/activities/games/` are always compiled for C3 envs too. Any global object there would be linked into C3 firmware and use its DRAM. The in-repo precedent (`src/platform/UsbSerialJtagHandoff.cpp`) wraps the whole file in its capability guard.
- **Fix:** add to AD-2:
  - "Every fork source file under `src/games/` and `src/activities/games/` is wrapped whole in `#if FREEINK_CAP_GAMES`."
  - "No `freeink-sdk` or `.skills/` changes; SDK gaps go upstream."

### A16. LOW: the icon is required by the brief but optional in the spine, and its render path is unspecified

- **Source:** brief, The Solution §3 ("a manifest, the script, and an icon"); addendum (manifest includes icon).
- **Spine:** AD-15 makes `icon.png` optional, and the seed leaves its size and colors as draft. It isn't said whether the launcher list, built on `UiListActivity`, shows icons at all. PNG decoding would go through `lib/PngToBmpConverter`, which isn't named.
- **Fix:** state the fallback icon, the decode path, and whether v1 list rows show icons. Record "optional" as a deliberate relaxation.

### A17. LOW: "paged launcher" is met by scrolling

- **Source:** brief, Scope ("a games launcher that pages when the list grows"); addendum ("a paged launcher").
- **Spine:** the launcher builds on `UiListActivity`, which per `docs/contributing/touch-and-ui.md` scrolls the viewport on swipe. That likely satisfies the intent on e-ink, but the spine doesn't say so.
- **Fix:** one line in Screens: "the launcher's paging is `UiListActivity` viewport scrolling".

---

## B. Seed and spine disagree (or the seed contradicts itself)

### B1. MEDIUM: when saves are discarded

- **Seed §1:** `id`: "Installing a package with the same `id` replaces the old one and **keeps its saves**." `version`: "A **new version** discards unfinished saved games."
- **Spine AD-17:** "discards a save whose **package hash** differs". The hash is the first 8 bytes of the SHA-256 of the whole `.cpgame` (AD-16). Any byte change discards the resume save, even at the same `version` (a one-line bug fix, or a re-zip with new timestamps).
- **Fix:** rewrite the seed: "Reinstalling a package that differs in any byte discards an unfinished game; `ch.store` data is kept." Or change the spine to key on `version`, but hash is safer for `nearby` equality.

### B2. MEDIUM: the seed's only example breaks in one of the modes it declares

- **Seed §1** declares tic-tac-toe with `"seats": {"min": 1, "max": 2}` and `"modes": ["solo", "pass", "nearby"]`.
- **Seed §6** is "Two-player tic-tac-toe that works in every mode", but has no computer opponent. In `solo`, `ctx.seats = 1`. After seat 1 moves, `state.turn = 2`, a seat that doesn't exist, so the game deadlocks. Per AD-11, the runtime accepts moves only from the seat `status.turn` names.
- The example also ignores `ctx`, so it can't adapt. AI assistants copy examples.
- **Fix:** either change the manifest to `min: 2` with `modes: ["pass", "nearby"]`, or add a bounded computer opponent inside `apply` for `ctx.seats == 1` (which also demonstrates the spike's "bounded AI" guidance). Add a hidden-info example (see A10).

### B3. LOW: the swipe event's fields

- **Spine (Conventions):** `{kind = "tap" | "long_press" | "swipe", x, y, dir}`.
- **Seed §2:** a swipe has `dir` only, with no `x` or `y`.
- **Fix:** pick one. `x` and `y` of the swipe start are cheap and useful for board games.

### B4. LOW: the seed understates the per-call budget's real latency

- **Spike:** "a per-callback budget of roughly 1–2M instructions to keep a tap under a second".
- **Spine** picks the top of that range (2 M). The **seed** says "at most about 2 million, roughly one second".
- A budget-sized call plus a 0.67 s fast refresh is about 1.6 s from tap to screen, and more under A6.
- **Fix:** in the seed, advise designing for ≤ 1 M per call, with 2 M as the hard stop.

### B5. LOW: the spine leaves unspecified things the seed documents as fact

- The seed defines `ctx.seats` and `ctx.mode`, and it defines `seat` in `pass` as "the seat whose turn it is". The spine defines neither.
- Neither document says which `seat` `draw` receives in `pass` after `status.over`.
- The seed promises "shows the message with its line number", while the spine's AD-14 says only "the Lua message".
- **Fix:** lift `ctx` and the per-mode `seat` rules into AD-8 so the spine stays authoritative ("where the two disagree, the spine wins").

---

## C. Spine internal inconsistencies surfaced while reconciling

### C1. LOW: user cancel is treated as a script failure

- AD-5 turns a user cancel (Back or Home during a long callback) into a Lua error via the count hook. AD-14 says "any Lua error … ends the session … shows its error screen".
- So a user-initiated exit would show an error screen, and it isn't clear whether the pre-call autosave is kept.
- **Fix:** AD-14: a cancel is a distinct status. It skips the error screen and keeps the last committed save.

### C2. LOW: two sequence numbers share one name

- AD-13's frame header has a link-level `seq` and `ack`, and a `MOVE` is also "tagged with the snapshot seq it saw". The sequence diagram then shows `STATE {seq+1, snapshot}`.
- Link seq and snapshot seq are different counters, and nothing names them apart.
- **Fix:** name them separately (`link_seq` in the header, `snap_seq` in the `MOVE` and `STATE` payloads).

### C3. LOW: the frame buffer's size and overflow behaviour are undefined

- AD-7 has a "bounded frame buffer", but gives no size and no rule for overflow. AD-14's list of session-ending failures doesn't include a display-list overflow.
- **Fix:** give a size, and make overflow an AD-14 failure.

---
title: 'Adversarial review: crosshatch-player v1 game platform spine'
reviews: '../ARCHITECTURE-SPINE.md'
companion: '../game-api-seed.md'
lens: 'Build two units one level down that each obey every AD to the letter and still build incompatibly; each such pair is a hole to close with a new or tightened AD.'
created: '2026-09-26'
verdict: 'Not yet a build substrate. The layering and script contract hold up, but the spine leaves ownership open for the session lifecycle across screens, the task hand-offs, seat assignment, the snapshot sequence number, byte formats (codec, save, .pkg, ADVERT), and game-over. Teams can build each unit to the letter and still fail to integrate.'
---

# Adversarial review: crosshatch-player v1 game platform spine

## Verdict

**Not ready to fan out.** The paradigm holds: a hexagonal `GameCore`, a reducer contract, and host-authoritative full-state replication. AD-1, AD-2, AD-4, AD-6, AD-10 and AD-15 are tight. However, the spine names *what* exists without saying *who owns it and in what byte layout* at eight seams. For each seam I could write two units that obey every AD and still fail to integrate. The first six holes below block implementation. Every one of them will be re-decided differently by whoever builds the story first. The rest are cheaper, but they belong in the spine before first-party games are written against API level 1, because level 1 is additive-only (AD-19).

Units attacked (one level below the spine): **GameCore** (Session, Seats, Protocol, ReliableLink, Manifest), **GameScript** (VM host, codec, bindings, frame buffer, LuaGame), **EspNowLink + NearbySession**, **GamePackageInstaller + GameRegistry + GamesWebApi**, **screens** (launcher, mode picker, lobby, match, hand-off, error), **first-party games**, and **`scripts/pack_game.py`**.

Codebase facts the review relies on (checked 2026-09-26):

- `ActivityManager::loop()` takes `RenderLock` before `exitActivity()` on a Replace, so the outgoing activity's `onExit()` **and** destructor both run while the render mutex is held (`src/activities/ActivityManager.cpp:184-193`). `goToSleep()` is a Replace (`:312`).
- On a Push, the parent is moved onto the stack without `onExit()`. Its `loop()` is **not called** until the child finishes.
- `requestUpdate()` sets an atomic flag that only the loop task consumes (`:386-395`). It is not a cross-task notification.
- A power-button press or the inactivity timeout calls `enterDeepSleep()`. That runs the current activity's `onExit()` through `goToSleep()`. Quick Resume then saves the current framebuffer, and the panel keeps showing it (`src/main.cpp:262-300`).
- `lib/JsonParser/StreamingJsonParser` depends only on the standard library, but it is an upstream `lib/`, not "the C++ standard library".
- `UiAppHost` routes touch through a FreeInkUI interaction table that the render task rebuilds, and applies per-board bezel insets (`docs/contributing/touch-and-ui.md`).

---

## Blocking holes

### H1. Two meanings of `seq`: ReliableLink frame counter vs Session snapshot version

**Severity: blocking.**

- **Unit A (ReliableLink, GameCore).** It reads AD-13 as a classic ARQ: `seq` is a per-sender frame counter that goes up on every reliable frame (MOVE, STATE, ABORT, PING, JOIN...), and `ack` is the last `seq` received in order. It resends every 400 ms until the frame is acked.
- **Unit B (Session, GameCore).** It reads "a guest sends MOVE tagged with the snapshot seq it saw" and the sequence diagram's `STATE {seq+1, snapshot}` literally. The snapshot version *is* the envelope `seq`, and a MOVE is stale when its `seq` differs from the current snapshot version.
- **Result.** A guest's PINGs and ACKs advance A's counter, so B rejects every MOVE after the first PING as stale. Or B writes snapshot versions into `seq` and A sees gaps and duplicates and never acks cleanly. Both follow AD-13 word for word.

There are related gaps with the same root:

- Does a resent STATE count as new? The ADs don't say whether STATE is latest-wins, which would let the link drop a superseded unacked STATE.
- On rematch (H9), a Session that restarts versions at 0 will have its new states dropped by any "ignore `ver` ≤ last" dedupe.

**Fix: tighten AD-13.**

> The envelope `seq`/`ack` belong to `ReliableLink` alone. `seq` is a per-direction frame counter and `ack` is cumulative. `PING`, `ACK` and `ADVERT` are unreliable and carry no new `seq`. Snapshot versioning is a `u16 ver` field in the `STATE` and `MOVE` payloads, owned by `Session`. It increases monotonically for the life of a `session` id and never resets, not even on rematch. `STATE` is latest-wins: the link may replace an unacked `STATE` with a newer one, and a guest ignores any `STATE` whose `ver` is not newer than its own.

---

### H2. Match-screen teardown vs the GameVM task: deadlock and a lost ABORT, all within the ADs

**Severity: blocking.**

- **Unit A (GameScript frame commit).** AD-7 says the render task reads committed frames "under RenderLock". So the natural commit takes `RenderLock`, swaps the front and back buffers, and calls `activityManager.requestUpdate()`. The AD doesn't forbid either step for the VM task. AD-18 forbids `RenderLock` only in *NearbySession* teardown.
- **Unit B (Match screen).** On Back, error or sleep it destroys the VM in `onExit()` or its destructor. It sets AD-5's cancel flag and joins the GameVM task.
- **Result.** `onExit()` already holds the render mutex (Replace path). The VM task is blocked in commit waiting for `RenderLock`, so the join never returns and the device deadlocks. This is the known pitfall 12cc816 in a new place. A variant: if B uses `vTaskDelete` instead of joining, and the VM task is inside a `Storage` write for `ch.store` or auto-save, the SD mutex is never released.
- **Unit C (AD-5 cancel vs AD-14).** The cancel flag "turns into a Lua error" in the count hook. AD-14 says *any* Lua error ends the session with the error screen. So pressing Back during a long `apply` shows a "script error" screen and sends ABORT, while a quick Back does not. Two screen authors will handle this two ways.
- **Also.** A `requestUpdate()` call from the VM task only sets a flag that the loop task consumes later. It works by accident and doesn't wake anything.

**Fix: new AD-20, "Cross-task hand-off and VM shutdown".**

> - The GameVM task never takes `RenderLock` and never calls `ActivityManager`.
> - Frames use a double buffer owned by `GameScript`, guarded by a small frame mutex. Lock order is `RenderLock` → frame mutex. The render task takes the frame mutex only inside `render()`. The VM task takes only the frame mutex, to swap buffers, and bumps an atomic `frameGen`. The match screen's `loop()` polls `frameGen` and calls `requestUpdate()`.
> - Shutdown is cooperative. The match sets `cancel`, posts `Quit`, and waits on a join semaphore. The VM task unwinds through `lua_pcall`, closes the state, flushes `ch.store` (H12), signals, and exits. Shutdown never uses `vTaskDelete` on a task that may hold a lock.
> - Cancellation produces the status `Cancelled`, which is distinct from `ScriptError`. Only `ScriptError` triggers AD-14.

---

### H3. Screens as separate activities vs a NearbySession pumped from the match loop

**Severity: blocking.**

The spine lists "launcher, mode picker, lobby, match, hand-off, error screens" as separate screens. AD-18 has NearbySession "handed to the match screen" and torn down "when that screen exits". The structural seed puts NearbySession on the loop task, pumped by the match's `loop()`.

- **Unit A (Screens author).** Builds hand-off, error and an in-game menu as separate `UiAppHost` activities. Hand-off and menu are pushed with `startActivityForResult()`; error uses `replaceActivity()`.
- **Unit B (NearbySession author).** Pumps resends, PINGs and inbound frames from the match screen's `loop()`, as the structural seed shows.
- **Result.**
  - While the menu is pushed in a `nearby` match, the match's `loop()` stops. Nothing is acked or pinged, and the peer drops the match after 10 s.
  - On a script error, the match queues ABORT (AD-14) and then replaces itself with the error screen. `onExit()` tears down NearbySession (AD-18) before the loop task drains the outbound queue, so the ABORT is never sent. The peer sees 10 s of silence instead of a clean end.
  - The lobby hands NearbySession to the match. If the lobby uses Replace, its `onExit()` runs after the match is constructed. A lobby that "tears down its radio in `onExit()`", as every current Wi-Fi activity does, kills the session it just handed over. If the lobby uses Push, the lobby stays alive under the match, and the ADs don't say who tears down on return.

**Fix: new AD-21, "The match activity owns the session".**

> One `GameMatchActivity` (a `UiAppHost`) owns the `GameVM` task, the `Session` (through it), the `NearbySession` (moved in at construction), and the frame buffer, from the first `setup` or resume until exit.
> - Hand-off, the in-game menu, the result/rematch prompt, "waiting for host" and the error view are **views inside that activity**, never pushed or replaced activities.
> - The lobby transfers NearbySession by `std::move` into the match's constructor and switches with Replace. After the move, the lobby's `onExit()` finds a null session and does nothing.
> - The exit order is fixed: queue `ABORT`, flush the link (bounded to 2 × 400 ms), stop the VM (AD-20), then tear down the radio.
> - The error view is shown from the match's `loop()` after the VM has reported `ScriptError`.

---

### H4. Two owners of seats: lobby/NearbySession vs Session. Solo and game-over seats are undefined.

**Severity: blocking.**

- **Unit A (Lobby + NearbySession, loop task).** Handles JOIN and ACCEPT before any VM or Session exists (AD-5 creates the VM in the match). It hands out seats in JOIN order and keeps a MAC → seat map.
- **Unit B (Session, GameVM task).** AD-11 makes seats a GameCore concept, so it also assigns seats, and it reads the seat of an incoming MOVE from a `seat` byte in the payload.
- **Result.** There are two seat maps, and a guest can claim any seat it likes in the payload. AD-11 says "only from the seat that status names", but not *how the seat of a remote move is known*.

The same gap shows up in each mode:

- **Solo.** The seed's tic-tac-toe declares `seats {1,2}` and `modes` including `solo`, and "works in every mode". In solo, `status` names seat 2 after the first move. Unit B maps local input to seat 1 and rejects every move, so the game is stuck. A different author maps local input to `status.turn`, making solo the same as pass. Both follow AD-11. `ctx.seats` in solo is also undefined (1 or `seats.max`?).
- **Pass at game over.** The seed says `seat` in pass mode "is the seat whose turn it is". At `{over = true}` there is no turn. One runtime passes the last mover, another passes `nil`, another passes 1. A hidden game (Battleship) then either reveals only one player's board or crashes on `nil`. This is part of API level 1 and can't be changed additively later.
- **Setup timing.** Is `setup(ctx)` called when the host opens the lobby (so `ctx.seats` = `seats.max`) or after ACCEPT (so `ctx.seats` = seats actually filled)? Can a `nearby` match start with one seat?

**Fix: new AD-22, "Roster".**

> A `Roster {mode, n, localSeat, peers[seat] → link id}` is fixed before `setup` and is immutable for the match.
> - **Solo:** `n = 1`, and local input is seat 1. `status` naming any seat other than 1 is a script error. A game offers `solo` only if `seats.min == 1`, and a computer opponent lives inside `apply` (AD-8).
> - **Pass:** the user picks `n` in `[seats.min, min(seats.max, host max)]`, and local input is `status.turn`.
> - **Nearby:** the lobby (NearbySession) assigns seats in ACCEPT order, with host = 1. `n` = seats filled when the host presses Start, and `n ≥ 2`. The match receives the roster with NearbySession.
> - The seat of a remote `MOVE` comes from the link peer identity, never from the payload.
> - `setup` runs once in the match activity after the roster is fixed.
> - At `over`, pass mode calls `draw` and `input` with `seat = 0` ("everyone"), and any move returned is ignored. Nearby and solo keep the local seat. Document `seat = 0` in API level 1.

---

### H5. Manifest and package identity: installer, registry, launcher and lobby each decide validity, hash and commit differently

**Severity: blocking.**

**(a) Parser location.** AD-15 and the structural seed put `Manifest` in GameCore, which the invariants limit to "C++ standard library only". JSON parsing is then either hand-rolled in GameCore or done in `src/games` with `StreamingJsonParser`.

- **Unit A (Installer).** Parses in `src/games` with `StreamingJsonParser`. A missing `hidden` defaults to false, and `modes` with an unknown value is rejected.
- **Unit B (GameCore `Manifest`, used by launcher and lobby).** Hand-rolled. A missing `hidden` is invalid, and unknown modes are ignored ("unknown keys are ignored" read loosely).
- **Result.** A package installs and then never appears in the launcher, or appears and fails to start.

**(b) Validation and availability.** AD-16's "validates the package" and AD-19's "shows as unavailable" don't say which checks are install-time rejections and which are launch-time greying:

- `api > host`
- `seats.max > 2` on a v1 host
- `modes` containing `solo` with `seats.min = 2`
- `nearby` with `seats.max = 1`

So the installer rejects a package the launcher would have greyed out, or the reverse.

**(c) Temp dir vs registry scan.** "Extracts to a temp dir and renames it to `/.games/<id>/`" plus "the registry is a scan of `/.games/*/manifest.json`":

- An installer that extracts to `/.games/.tmp-foo/` produces a phantom game if power is lost.
- On FAT, replacing an existing directory is not atomic (remove, then rename). A crash in between leaves no game, or two.
- Neither unit checks that the directory name equals `manifest.id`.

**(d) Package hash.**

- "First 8 bytes of SHA-256 of the package" hashes the *zip bytes*. `pack_game.py` built with default `zipfile` stamps mtimes, so two CI runs of the same `games/<id>/` source produce different hashes.
- A user who re-zips a game (macOS adds `__MACOSX/`) gets a new identity. Nearby matches then fail between devices running the "same" game.
- The hash is also stored in `.pkg`, with no format given. The installer writes 16 hex characters, while `GameSaveStore` or the lobby reads 8 raw bytes, and every save is discarded.

**(e) Inbox policy.** AD-16 doesn't say what happens to `/games/x.cpgame` after install (delete, keep, rename), or to a failed one. A launcher that keeps the file reinstalls it on every open, deleting nothing but rewriting the install directory and changing the timestamp. A launcher that deletes failed files loses them silently.

**(f) Subdirectories and `require`.** AD-15 allows "optional `*.lua` modules". `pack_game.py` packs `games/<id>/` recursively, including `lib/util.lua`. The installer extracts flat, or rejects nested paths. The jailed `require("lib.util")` maps dots to `/`, or doesn't.

**Fix: tighten AD-15 and AD-16, and let GameCore use `lib/JsonParser`.**

> - `GameCore` may depend on `lib/JsonParser` (standard-library only). `GameCore::Manifest::parse()` is the **only** manifest parser. Installer, registry, launcher, lobby and `GamesWebApi` all call it.
> - `Manifest::check(hostCaps)` returns one of:
>   - `Invalid(reason)`: malformed JSON, bad `id`, missing required key, `seats.min > seats.max`. The installer rejects it.
>   - `Unavailable(reason)`: `api` above the host, `seats.min` above the host maximum, no mode this host supports. The package is installed and greyed out.
>   - `Ok`, with the mode list filtered to modes that are satisfiable.
> - Defaults: `hidden = false`; `modes` required and non-empty.
> - Package members are flat. Only `manifest.json`, `main.lua`, `[a-z0-9_]+.lua` and `icon.png` at the zip root are allowed. Anything else, including directories, is `Invalid`. `require("name")` loads `name.lua` from the package root only.
> - Limits (values to be fixed in the spine): package size, uncompressed total, member count.
> - Extraction goes to `/.games-tmp/<id>/`, outside the scanned directory. `.pkg` is written **last** as the commit marker. The registry lists only directories that have a valid `.pkg` and whose name equals `manifest.id`. At boot or launcher open, any leftover `/.games-tmp/` is removed.
> - `.pkg` format: `v1\n` followed by 16 lowercase hex characters and `\n`.
> - The package hash is a **content hash**: SHA-256 over the members sorted by name, each as `name \0 u32le(len) bytes` of the *uncompressed* data, truncated to 8 bytes. `pack_game.py` computes and prints the same value, and a shared test vector lives in `test/game_core/`.
> - The inbox file is deleted after a successful install. A failed file is renamed `*.cpgame.bad`, and the launcher shows the reason once.

---

### H6. The codec's bytes are unspecified but persisted and shared: GameScript codec vs SaveStore, `ch.store`, and tooling

**Severity: blocking.**

AD-10 defines the codec's *domain* (value kinds, depth, limits) but no byte layout and no version. Its bytes still end up in four places: STATE and MOVE on the radio, the resume save, the `ch.store` file, and golden fixtures in `test/game_script`. Any host tool (simulator, a future `pack_game.py --validate-state`, the starter repo's test harness) also needs them.

- **Unit A (GameScript codec).** Tags plus a zigzag varint for integers, map entries in `lua_next` order, no header.
- **Unit B (A save/store adapter or Python fixture writer).** Writes fixed 8-byte integers, or expects a leading version byte.
- **Result.** Both follow AD-10. A firmware update that "improves" the codec also silently corrupts every `ch.store` high-score table and resume save. AD-17 discards saves only on a package-hash change, not on a codec change. Two devices with equal `proto` but different codec builds also exchange unreadable STATEs.

Other gaps in the same area:

- Where the 1,400 B limit is measured: codec output, or the STATE payload including `ver` and the H8 status bytes?
- Whether decode order is deterministic. `#t` on tables with holes depends on how the table was built. It matches between host and guest only if decode is deterministic.
- Whether float-valued integral keys (`t[2.0]`) are normalized or rejected.

**Fix: tighten AD-10, and add codec v1 to `docs/file-formats.md`.**

> Codec v1 byte layout, little-endian:
> - One tag byte per value: `nil`, `false`, `true`, int (zigzag LEB128 of i64), float (IEEE-754 binary64), string (LEB128 length + bytes), table.
> - A table is `LEB128 narr`, then `narr` values for keys `1..narr`, then `LEB128 nrec`, then key/value pairs. `narr` is the longest run of non-nil values from key 1.
> - Integral float keys are normalized to integers. Other float keys, and NaN, are errors.
> - Decode pre-sizes with `lua_createtable(narr, nrec)` and inserts in stream order.
>
> Limits: the 1,400 B / 256 B / 4 KB limits apply to codec output alone. The protocol reserves its own header room (envelope 12 B + payload header ≤ 16 B, well under 1,470 B).
>
> Versioning: the codec version is part of `proto`, so bumping the codec bumps `proto`. Every persisted codec blob (save, `ch.store`) starts with a `{magic, codecVersion}` header. An unknown version is discarded with a log line, never decoded.
>
> A Python reference encoder/decoder lives in `scripts/` and is tested against the same golden vectors as the C codec.

---

## Significant holes

### H7. Guest feedback: a rejected or stale MOVE is invisible, and double taps race

- **Unit A (Host Session).** Rejects a stale or out-of-turn MOVE, or `apply` returns `nil, reason`. AD-13 says only "the host rejects" and "broadcasts STATE after each *accepted* move". It sends nothing back.
- **Unit B (Guest match screen).** After sending a MOVE it shows "waiting…" until a STATE arrives.
- **Result.** The guest waits forever, or until a timeout that no AD defines.
- **Pass and solo too.** The `reason` from `apply` has no owner. Is it a runtime toast over the script's frame (which conflicts with the script owning the screen, H14)? Is it logged? Dropped? The first-party games will each invent a way to show it.
- **Double taps.** A guest can double-tap and send two MOVEs with the same `ver`. That's harmless only if the guest runtime holds further moves while one is in flight, and nothing says it must.

**Fix: tighten AD-13 and AD-8.**

> Add frame type `REJECT {ver, reason ≤ 64 B}`, sent to the mover for a stale, out-of-turn, or `apply`-rejected move.
> - In every mode, a rejection reaches the script as an input event `{kind = "rejected", reason = "..."}`, delivered to `input` for the mover's seat. The script can show it through `ui`. The runtime never draws over the frame to show it.
> - While a MOVE is in flight, the guest runtime still calls `input` (so `ui` updates), but discards returned moves until the matching `STATE` or `REJECT` arrives.

### H8. The guest's `status`: two sources of truth for whose turn it is and who won

- **Unit A (Guest runtime).** Calls `status(state)` locally, as AD-8 says it runs on every device, to gate input and detect game over.
- **Unit B (Host).** Also calls `status` on its own copy.
- **Result.** Nothing requires `status` to be pure. A script that reads `ch.time.ms()` or `math.random` in `status` (for example, "time's up" or a random tie-break) makes host and guest disagree on the turn or the winner. The guest then gates input differently from the host's authority checks.
- **Error handling.** A `status` error on the guest is a guest-side AD-14 and sends ABORT, even though the authority's state is fine.

**Fix: tighten AD-8, AD-9 and AD-13.**

> - `STATE` carries the host-computed status: `turn u8` (0 when over), `over u8`, `winners` as a bitmask `u16`.
> - Guest runtimes gate input and drive the game-over flow from that status, and never call `status` for control flow. Scripts may still call `game.status` inside `draw`.
> - `status` must be a pure function of `state`. Document it, and have the pass/solo runtime call it twice in debug builds and compare the results.

### H9. Game-over, result and rematch: no owner and no frame type

- **Unit A (Match screen).** When `over` arrives, it shows the script's last frame, deletes the resume save, and offers "Play again", which calls `setup` with a fresh Session: `ver = 0`, same `session` id.
- **Unit B (Guest / ReliableLink).** Drops any STATE whose `ver` is not newer, per H1. Alternatively it treats `over` as "match ended" and tears down.
- **Result.** The guest never sees the rematch, or it has already left the match. A rematch can't be requested from the guest side. `over` in solo or pass also leaves open whether the save is deleted, kept for "view final board" on resume, or overwritten.

**Fix: new AD-23, "End of match".**

> - When `over` is reached, the authority deletes the resume save.
> - The match activity keeps running and shows the script's frame (drawn with `seat = 0` in pass mode, per H4), plus a runtime-owned menu offering "Play again" (authority only) and "Leave".
> - "Play again" calls `setup` on the same Roster and broadcasts `STATE` with `ver` continuing.
> - A guest's "Leave", or a host's "Leave", sends `ABORT` with reason `left`. The peer shows a "player left" view, not the AD-14 error view.
> - `ABORT` carries a reason byte: `left`, `script_error`, `timeout`, `version_mismatch`.

### H10. Hidden information leaks through sleep and resume, despite AD-12

- **Unit A (Match screen, `pass` + `hidden`).** Obeys AD-12 exactly: a full-refresh hand-off screen appears *when the turn seat changes*.
- **Unit B (Upstream sleep).** Power button or inactivity timeout (auto-sleep is allowed in pass mode). With Quick Resume, the current frame, showing seat 1's secret board, stays on the panel while the device sleeps and is saved as the resume frame (`src/main.cpp:266-282`).
- **Result.** The next player picks up the device and sees seat 1's ships.
- **Resume.** Resume and the first draw after `setup` aren't "a seat change" either, so AD-12 shows no hand-off. The device opens directly on whoever's turn it is.

**Fix: tighten AD-12.**

> In `pass` mode with `hidden = true`, the runtime shows the hand-off screen, with a full refresh:
> - before the first draw after `setup`;
> - before the first draw after resume;
> - on every turn-seat change.
>
> In the match activity's `onExit()`, including the sleep path, it renders the hand-off/blank screen into the framebuffer before returning, so Quick Resume and the retained panel image never show a seat's view. That work runs under the `RenderLock` that `onExit()` already holds, and calls no `RenderLock` itself.

### H11. Refresh escalation has no single owner, and coalesced frames drop their hint

- **Unit A (GameScript frame commit).** If a new frame is committed before the render task has shown the previous one, the new frame overwrites it: latest wins, and so does the latest hint.
- **Unit B (FrameReplay, `src/games`).** Escalates to `half` every N fast frames, using its own counter.
- **Unit C (Match screen).** Forces `full` after a hand-off or overlay, using a second counter.
- **Result.** All three follow AD-7 ("the runtime may escalate"). A `"full"` requested for a new round is lost when a later `"fast"` frame, such as a cursor move, replaces it before replay. That's a downgrade, which AD-7 forbids but nothing enforces. Two counters also double-escalate.

**Fix: tighten AD-7.**

> `FrameReplay` is the only escalation policy. Its inputs are the frame's hint, a `forceFull` flag set by the match activity (after hand-off, overlay close, or resume), and its own fast-refresh counter. When frames coalesce, the committed hint is the maximum of the coalesced hints (`full` > `half` > `fast`). `GameScript` and the screens never escalate on their own.

### H12. Call-context rules for `ch.store` and `ch.gfx`

- **Unit A (First-party solo puzzle).** Calls `ch.store.set({best = ...})` inside `apply`, which is the only place state changes.
- **Unit B (First-party 2P game).** Calls `ch.store.set` in `draw` when `status.over`, so each player's device records its own win.
- **Result.** In `nearby`, A records scores only on the host, because `apply` never runs on the guest (AD-8). B writes to SD on every redraw, wearing the card and blocking the VM task. Neither breaks an AD.
- **`ch.gfx`.** The seed says "call these only inside draw". The spine doesn't say whether a `ch.gfx` call inside `input` is an error, is ignored, or leaks into the next frame.

**Fix: tighten AD-17, AD-7 and the seed.**

> - `ch.store` is local to the device. `get` and `set` are allowed in every callback.
> - `set` validates against the codec and the 4 KB limit immediately (AD-14 on a breach), then marks the store dirty. The runtime writes it at most once per 5 s, and at session end, exit and sleep, but only if the bytes changed.
> - Document that writes made in `apply` land only on the authority.
> - A `ch.gfx.*` call outside `draw` raises a Lua error.

### H13. Save-file ownership and format

- **Unit A (GameCore `ISnapshotStore` in the VM task).** Writes the snapshot after each accepted move, in its own layout.
- **Unit B (Launcher).** Needs to know "is there a resumable save, and does its hash match?" without starting a VM. It reads the file itself, assuming a different layout. `docs/file-formats.md` has no entry for it.
- **Result.** AD-17 lists the contents (snapshot, seats, mode, hash) but no format, no atomicity rule, and no owner.
- **Seed conflict.** The seed says "a new **version** discards unfinished saves". The spine discards on a **hash** change. A re-zip or rebuild of the same version therefore discards saves according to the spine but not according to the seed.

**Fix: tighten AD-17.**

> `src/games/GameSaveStore` implements `ISnapshotStore` and is the only reader and writer of `/.games-data/<id>/resume.bin` and `store.bin`. The format goes in `docs/file-formats.md`:
> - resume: `magic, fileVersion, codecVersion, pkgHash[8], mode, n, ver u16`, then the snapshot blob;
> - store: `magic, fileVersion, codecVersion`, then the blob.
>
> Writes go to a `.tmp` file followed by a rename. The launcher uses `GameSaveStore::peek()` to decide whether to offer resume. The seed wording changes to "a changed package discards unfinished saves".

### H14. The coordinate space and input path have two owners: `UiAppHost` vs the script canvas

- **Unit A (Match screen on `UiAppHost`, per the conventions table).** Routes taps through the FreeInkUI interaction table (tap flash, header chrome), applies per-board bezel insets, and uses whatever orientation the user set.
- **Unit B (FrameReplay + input builder).** Maps logical portrait `ch.screen` to the panel for drawing. The input builder converts raw `InputSnapshot` points into script coordinates with its *own* transform.
- **Result.** Taps land offset from what was drawn on the device where the transforms differ (bezel insets, landscape Sticky orientation). A header, if the host draws one, covers the script's top rows.
- **Swipe shape.** The spine's event shape includes `x, y` for swipes; the seed's does not.

**Fix: tighten AD-7 and the input convention.**

> One `GameViewport` (in `src/games`) defines the script canvas: rotation, offset, the size exposed as `ch.screen`, and the bezel insets it excludes. Both `FrameReplay` (logical → panel) and the input builder (panel → logical) use it.
> - The script owns the whole canvas. The runtime draws nothing over it except runtime views (hand-off, menu, error).
> - Canvas touches bypass the FreeInkUI interaction table. Only the edge gestures and runtime views use it.
> - Swipe events carry the start `x, y` as well as `dir`.

### H15. ADVERT, JOIN and ACCEPT payloads, the `api` comparison, and the session id are unspecified

- **Unit A (Lobby author).** Treats "equal `api`" in AD-13 as the *firmware* `ch.api`. A host that has an additive level-2 firmware then can't play a level-1 game with a level-1 guest.
- **Unit B (GameCore Protocol author).** Treats it as the *manifest* `api`, which the package hash already makes redundant.
- **Payload layouts.** They're left to whoever writes EspNowLink first, so the lobby and the Protocol codec disagree on field order.
- **Session id.** Nobody owns generating the `session u32`. GameCore has no RNG port.
- **Other gaps.** Adverts don't stop after ACCEPT. JOIN when the game is full has no reply. Frames from a stale session are not required to be dropped.

**Fix: tighten AD-13.**

> Payload layouts are defined in `GameCore::Protocol`, the only encoder and decoder:
> - `ADVERT {gameId len8+≤32 B, pkgHash[8], manifestApi u8, seatsMax u8, seatsTaken u8, hostLabel len8+≤16 B}`;
> - `JOIN {pkgHash[8]}`;
> - `ACCEPT {seat u8, n u8}`, sent to every guest when the host presses Start;
> - `ABORT {reason u8}`;
> - `MOVE {ver u16, move blob}`;
> - `STATE {ver u16, turn u8, over u8, winners u16, snapshot blob}`;
> - `REJECT` as in H7.
>
> Matching requires equal `proto` (which covers the codec version) and equal package hash. Firmware `ch.api` is not compared: each device already refuses packages above its own level (AD-19).
>
> The host draws the `session` id from an `IRandom` port when the lobby opens. Frames with any other `session` are dropped. The host stops `ADVERT` at ACCEPT. A `JOIN` to a full or started session gets `ABORT{full}`.

## Lower-severity notes

- **VM task priority and queue memory (AD-5).** The GameVM task shares core 1 with `ActivityManagerRender` at priority 1, but its priority isn't fixed. At a higher priority, a 2 M-instruction `apply` starves rendering; at a lower one, it misses frames. The MOVE/STATE queues between NearbySession and Session hold frames of up to 1.47 KB. A default FreeRTOS queue puts them in internal RAM, which is exactly what AD-6 is trying to protect. **Fix:** fix the priority at 1, and make the queues depth-2 carrying PSRAM buffer handles rather than frame copies.
- **When `draw` is called.** No AD says. **Fix:** after every snapshot change, after every `input` call (`ui` may have changed), and after hand-off. Frames with identical bytes are skipped.
- **Frame buffer overflow (AD-7).** "Bounded" has no size, and overflow has no behavior. **Fix:** give the size in the spine, and treat overflow as a script error (AD-14) rather than silent truncation, so authors find it in solo play.
- **Delete vs data (AD-16).** Does `/api/games/delete` keep `/.games-data/<id>/`? AD-16 says data survives *reinstall*; delete should say the same, or offer "also delete data".
- **Wi-Fi teardown (AD-18).** Upstream Wi-Fi activities `silentRestart()` in `onExit()`, and the spine leaves open whether games do. If the answer is yes, H9's "Play again" must not pass through `onExit()`. That's one more reason for H3's single-activity rule.
- **Seed example vs the rules.** Tic-tac-toe lists `solo` but has no computer opponent. Under H4 it must drop `solo` or implement a computer player in `apply`. Its `draw` also assumes `s.turn` is set when not over. That's fine, but add the `seat = 0` case.

## Proposed spine changes, summarized

| Change | Closes |
| --- | --- |
| AD-13: split link `seq`/`ack` from payload `ver`; STATE latest-wins; `ver` never resets; add `REJECT`; `ABORT` reason; fixed payload layouts; host status in STATE; match on `proto` + hash only; `IRandom` session id | H1, H7, H8, H9, H15 |
| **New AD-20** Cross-task hand-off and VM shutdown (lock order, frame mutex, `frameGen` poll, cooperative join, `Cancelled` ≠ `ScriptError`) | H2 |
| **New AD-21** The match activity owns VM, Session and NearbySession; runtime screens are views; fixed exit order with ABORT flush | H3 |
| **New AD-22** Roster (solo `n = 1`, pass `n` picked, nearby assigned by lobby, seat from peer identity, `seat = 0` at `over` in pass) | H4 |
| AD-15/16: single `Manifest::parse` + `check()` tri-state, flat members, `/.games-tmp`, `.pkg` commit marker and format, content hash shared with `pack_game.py`, inbox disposition | H5 |
| AD-10: codec v1 byte layout, codec version inside `proto`, headers on persisted blobs, Python reference codec + golden vectors | H6 |
| **New AD-23** End of match (save deletion, rematch by authority, Leave/ABORT reasons) | H9 |
| AD-12: hand-off on start, resume and sleep/`onExit()` | H10 |
| AD-7: `FrameReplay` sole escalation owner, max-hint coalescing, `GameViewport`, `ch.gfx` outside `draw` is an error | H11, H12, H14 |
| AD-17: `GameSaveStore` sole owner, file formats recorded, atomic writes; `ch.store` local, dirty-flag flush; seed wording fixed | H12, H13 |

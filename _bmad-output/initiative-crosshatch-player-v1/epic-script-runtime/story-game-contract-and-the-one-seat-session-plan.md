---
title: 'Game contract and the one-seat Session'
type: 'feature'
ticket: '8'
created: '2026-09-27'
status: done
baseline_revision: '124ac2cd49f3d0bfcc3ec6430248d717fbcd07f2'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The VM runs `setup`, `draw`, and `input` on a live Lua state: nothing holds an encoded snapshot or `ver`, `status` and `apply` are never called, moves are dropped, `ctx` lacks `api`, the codec limits are not enforced, and the arena's scratch share is unreserved (2.7 review L2), so Lua can starve it.

**Approach:** Add `GameCore::Session` (roster, snapshot bytes, `ver`, status, pending move, over-delivered flag) driving the `IGameRules` port; make `LuaGame` implement it with a fresh codec decode per call and a re-encode of `setup`/`apply` results and moves under the AD-10 limits; reserve Session and one codec scratch in the arena before Lua starts; make the tracer move-driven.

## Boundaries & Constraints

**Always:**
- AD-8/9/10/11/14 as written: `apply` gets a fresh decode, its result is re-encoded (1,400 B); moves encode at 256 B whether or not they are then used; `draw`/`input`/`status` get discarded decodes; a turn or winner outside `1..n` is a `ScriptError`; `rejected` and `over` (once per local seat per round) reach `input`; a move returned while one is pending, after `over`, or off-turn is discarded; `ctx = {seats, mode, api}` with `api = API_LEVEL`; one `ui` table per local seat, never encoded.
- Every Lua entry (the new `status`, `apply`, and the codec work) runs inside the one trampoline under the CallGuard; nothing that can raise runs outside it. Locals under 256 B; no allocation outside the arena on the VM task.
- `GameCore` includes no Lua/GameScript header; GameScript `static_assert`s its codec limits equal GameCore's constants.
- Session and codec scratch are allocated from the arena before `lua_newstate`; `ARENA_BYTES = LUA_HEAP_BYTES + SCRATCH_RESERVE_BYTES`, the reserve `static_assert`ed to cover both with block headers (handoff 2.11/L2).
- The 3 s watchdog still measures one Lua call, not a Session step of several calls.
- Fixtures only in `test/game_script/fixtures/`.

**Never:** pass/nearby seats, hand-off, seat 0, `resume.bin`, the link (later epics); the Over/Paused/Error views and Play again (2.13); `ch.api` (2.10); timer/long-press/swipe events (2.10, 2.11); editing `lib/lua`, `ci.yml`, `.skills/`, or the submodule pointer.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Accepted move | tap → `input` returns a table; `apply` returns a table | snapshot replaced, `ver` + 1, `status` recomputed, redraw | — |
| Draw mutates state | `draw` sets `state.x = 1` | next `draw`/`apply` sees the old snapshot | — |
| Rejection | `apply` returns `nil, reason` | `input` gets `{kind = "rejected", reason}` (≤ 64 B, cut at a UTF-8 boundary; nil → `""`); its move discarded | non-string reason → `ScriptError` |
| Game over | `status` → `{over = true, winners = {1}}` | `input` gets `{kind = "over"}` once; later moves discarded | — |
| Pending | second move before the first is applied | second move discarded | — |
| Bad status | `{turn = 2}` in solo; winners `{0}`; not a table | — | `ScriptError` naming the field |
| Oversized | state > 1,400 B; move > 256 B | — | `ScriptError` "... is too large (over N bytes)" |
| Bad values | `setup`/`apply` result not a table; `input` returns a non-table non-nil; unencodable (cycle, function, metatable) | — | `ScriptError` with the codec error name |
| Heap near the cap | Lua fills its 256 KiB | encoding still works (scratch reserved) | Lua's own "not enough memory" |

</frozen-after-approval>

## Code Map

- `lib/GameScript/LuaGame.{h,cpp}` -- `enter()`/`trampoline`/`Call`; `Call` carries only `const InputEvent*` (handoff 2.8): generalize it to carry state/move spans, seat, event, ctx, roster, and result slots. `start()` = load + setup today; `uiRef` is one table.
- `lib/GameScript/Codec.h` -- `encode(L, idx, limit, scratch, size)` (never raises), `decode(...)` (raises only memory errors), `errorName`, `scratchBytes`; a larger scratch serves any limit.
- `lib/GameScript/ArenaAllocator.{h,cpp}` -- `ARENA_BYTES`, `LUA_HEAP_BYTES`; `ALIGN`/`HEADER` are private to the .cpp.
- `lib/GameScript/GameInput.h` -- `InputKind`/`InputEvent` (Tap only), queue payload.
- `lib/GameCore/{ApiLevel.h,IRandom.h}` -- `API_LEVEL`; port style.
- `src/games/GameVM.{h,cpp}` -- `run()` loop (`start`, `draw`, per event `input` + `draw`), `callStartMs`/`runningForMs` stamped per GameVM op; `GameArena` sizes by `ARENA_BYTES`.
- `test/game_script/LuaGameFixture.h`, `LuaGameTest.cpp`, `CallGuardTest.cpp`, `SandboxTest.cpp` -- about 45 `start()`/`draw()`/`input()` call sites on the old API; `CMakeLists.txt` globs only `lib/GameScript`.
- `test/game_core/{CMakeLists.txt,ApiLevelTest.cpp}` -- add Session sources; limits pinned against constants at line 157.
- `docs/crosshatch/api-level-1.txt`, `ApiLevel.h` `API_SURFACE_CRC` -- limits section.
- `test/game_script/fixtures/{tracer,loop}/main.lua` -- simulator games; `loop` has no `status`/`apply`.
- `deferred-work.md` -- 2.6's codec stack item points here.

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameCore/{GameEvent.h,Roster.h,IGameRules.h,Session.h,Session.cpp}` -- per Design Notes.
- [ ] `lib/GameScript/GameInput.h` -- alias `InputKind`/`InputEvent` to GameCore's.
- [ ] `lib/GameScript/ArenaAllocator.{h,cpp}` -- `SCRATCH_RESERVE_BYTES`, new `ARENA_BYTES`, public `blockBytes(size)`, `create<T>`/`destroy<T>`.
- [ ] `lib/GameScript/LuaGame.{h,cpp}` -- implement `IGameRules`; `load()`; scratch from the arena; per-seat `ui`; `callSerial()`; `Outcome` becomes GameCore's.
- [ ] `src/games/GameVM.{h,cpp}` -- Session in the arena before `load()`; loop `handle` → `applyPending` → `draw`; log round over; watchdog by call serial.
- [ ] `test/game_script/fixtures/tracer/main.lua`, `loop/main.lua` -- move-driven tracer; `loop` gains `status`/`apply`.
- [ ] `test/game_core/SessionTest.cpp`, `CMakeLists.txt` -- FakeRules: `ver`, pending discard, rejected delivery, over once, off-turn/after-over discard, rematch keeps `ver`.
- [ ] `test/game_script/{SessionGameTest.cpp,LuaGameFixture.h,CMakeLists.txt}`, and the three existing suites moved to the new API -- every matrix row through Lua; codec stack cost at depth 16 on a painted stack.
- [ ] `docs/crosshatch/api-level-1.txt`, `lib/GameCore/ApiLevel.h`, `test/game_core/ApiLevelTest.cpp` -- `limit reject_reason_bytes 64`, new CRC, limits pinned to constants.
- [ ] `_bmad-output/implementation-artifacts/deferred-work.md` -- close or update the codec stack item.

**Acceptance Criteria:**
- Given `ctest`, then every suite passes; given `pio run -e x4pro` and `-e default`, then both build; `pio check` finds no defects.
- Given the simulator with the tracer in `fs_/.games/tracer/`, when the banner is tapped once and the canvas five times, then screenshots show the rejection message, the count rising, and "Game over" with one over event.
- `./bin/clang-format-fix` leaves no diff; `check_upstream_touches.py` passes.

## Implementation Notes

- Implemented directly (no coding subagent in this session). New: `lib/GameCore/{GameEvent.h,Roster.{h,cpp},IGameRules.h,Session.{h,cpp}}`, `test/game_core/SessionTest.cpp` (9 tests over a fake rules), `test/game_script/SessionGameTest.cpp` (8 tests through Lua). Changed: `ArenaAllocator` (reserve, `blockBytes`, `create`/`destroy`), `GameInput.h` (aliases), `LuaGame` (rules adapter), `GameVM`, the move-driven tracer, `loop` (gains `status`/`apply`), the three existing suites, both test `CMakeLists.txt`, the API list and CRC (`0xCAF107E5`), `ApiLevelTest` (Session limits pinned), and two `deferred-work.md` entries.
- `LuaGame::start()` became `load()` (no setup); the existing suites use a test-only `DirectGame` subclass that runs load + setup and passes its snapshot with seat 1, so they kept their fixtures (none needs `status`/`apply`). When setup fails its snapshot is `{}`, so the stack-measurement test can still draw its globals.
- A move is kept only for touch events; a move returned for `rejected` or `over` is discarded (Session.h). A rejection reason is cut to 64 B at a UTF-8 boundary in every mode; `limit reject_reason_bytes 64` is listed.
- Codec C stack (entry 6's deferred item): on the host a 16-deep encode plus decode needs at least 2,808 B beyond a flat state (about 187 B a level, painted pthread stack); on the ESP32-S3 (`-Os -fstack-usage`) `Encoder::value` is 80 B and `Decoder::value` 112 B a level with the table helpers inlined, so at most about 1.8 KB, run in the trampoline near the stack base.
- Heap cap (handoff L2): the reserve makes Session and scratch safe, but measured on the host the arena still fills before the Lua counter reaches 256 KiB (Lua bytes at the failure: 205,727 for empty tables, 198,206 for short strings, 241,818 for 1 KB strings), because of 16 B block headers; recorded as an owner decision in `deferred-work.md`.
- Watchdog: `LuaGame::callSerial()` counts entries; `GameVM::runningForMs` restarts its clock when it changes, replacing `callStartMs`.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time against `review.diff`, 97 kB: blind-hunter with a floor of 10, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 0, medium 0, low 8, false 4, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind, edge | `Roster::isSeat` accepts any seat up to `seats` (a `uint8_t`), so a roster past 16 seats would shift `1u << (seat - 1)` out of the u16 winners mask (undefined past 31) | low | Patched: `isSeat` also requires `seat <= MAX_SEATS`; `SessionTest.RosterSeats` pins it. Unreachable in solo today. |
| 2 | verification-gap, blind | `LuaGame::callSerial()`, which the watchdog now relies on, has no test; a counter that stopped moving would make every multi-call step look like one call | low | Patched: `LuaGameTest.EachEntryIntoLuaBumpsTheCallSerial` counts load, setup, draw, input, and close. |
| 3 | verification-gap, blind | `ArenaAllocator::blockBytes`, which sizes the reserve's `static_assert`, is not checked against what `allocate` takes; `create`/`destroy` untested alone | low | Patched: `BlockBytesBoundsWhatAnAllocationTakes` (sizes 0 to 13,720 and an unsplittable fit) and `CreateConstructsInTheArenaAndDestroyFrees`. |
| 4 | verification-gap | `GameVM::run`'s composition (Session in the arena, `handle` → `applyPending` → `draw`) and `runningForMs` run only in the simulator; the host `SessionGame` fixture mirrors, not runs, it | low | Defer: covered by entry 1's deferred item (GameVM has no host harness); simulator runs here cover the path and the watchdog. |
| 5 | blind | `readStatus` treats any truthy `over` (`0`, `"no"`) as over | low | Rejected: Lua truthiness, as `if s.over` reads in a script; a type check adds a branch for an unlikely value. |
| 6 | blind | The watchdog now starts timing at the loop's first poll that sees a call, up to one loop delay after it began | low | Rejected: at most a loop pass late against 3 s; the exact start would need a clock port in GameScript. |
| 7 | blind | `winners` longer than `n` (e.g. `{1, 1}` in solo) is an error although duplicates fit the mask | low | Rejected: deliberate (Design Notes), it keeps the C loop bounded by `n`, and the message says why. |
| 8 | intent | Discarding a move returned for `rejected` or `over` narrows the seed's "input returns a move or nil" | low | Rejected as a defect: a deliberate rule (Session.h, Implementation Notes) that stops a reject-and-retry loop inside one step; raised in the report for the owner. |
| 9 | blind | `SCRATCH_BYTES` is sized for the store limit (13,720 B) while this entry encodes at most 1,400 B | false | Intentional: one scratch serves every encode, and entry 10's `ch.store` reuses it (Design Notes, Arena). |
| 10 | blind | `Session::keepState` failing returns `ScriptError` with an empty message | false | Unreachable through `LuaGame`, which encodes under the same limit; only a broken adapter (the fake in `AStateOverTheLimitIsRefused`) reaches it. |
| 11 | blind | `DirectGame` hides a failed setup behind an empty-table snapshot | false | Test-only and documented, so the stack test can draw its globals; `start()` still returns the failure. |
| 12 | blind | The "Round over" log waits for the loop to cycle | false | The loop returns to its top right after the step's draw, before it blocks, so the line comes with that step. |

Edge-case claims check: the Intent and Tasks claims held against the code (trampoline for every entry, locals under 256 B, reserve `static_assert`, per-call watchdog, limits at and one over). Intent-alignment: readings are (a) Session plus the rules port, codec limits, reserve taken first, move-driven tracer, and (b) also making the Lua cap bind (the handoff's "never binds"). The diff implements (a); (b) is measured and recorded as an owner decision in `deferred-work.md`. The intent's expectations live at the GameVM task; the diff's tests exercise GameCore with a fake rules and GameScript through a fixture that mirrors `GameVM::run`, with the task itself in the simulator.

After the patches: `ctest` 504/504; `pio run -e x4pro` and `-e default` SUCCESS; `pio check -e x4pro` no defects; `./bin/clang-format-fix` clean.

## Design Notes

**GameCore.** `GameEvent{kind (Tap, Rejected, Over), x, y, reason}` (reason: NUL-terminated, valid for the call). `Roster{mode, seats, localSeats bitmask, api}` with `solo()` and `isSeat(int64)`, `MAX_SEATS` 16 (STATE's u16 winners). `Status{over, turn, winners mask}`. `IGameRules` returns `Outcome{Ok, ScriptError, Cancelled}`:

```cpp
Outcome setup(const GameContext&, std::span<const uint8_t>& state);           // encoded, valid until the next call
Outcome status(std::span<const uint8_t> state, const Roster&, Status& out);   // validated against the roster
Outcome apply(std::span<const uint8_t> state, uint8_t seat, std::span<const uint8_t> move,
              std::span<const uint8_t>& next, std::span<char> reason);        // next empty = rejected
Outcome draw(std::span<const uint8_t> state, uint8_t seat);
Outcome input(std::span<const uint8_t> state, uint8_t seat, const GameEvent&, std::span<const uint8_t>& move);
```

`Session(roster, rules)` owns `snapshot[1400]`, `move[256]`, `reason[65]`, `ver`, `status`, `pending`, `overDelivered`. `start()`: setup → copy → `++ver` (never reset, so a rematch continues) → status → deliver over if already over. `handle(event)`: input for the local seat; keep the move unless pending, over, off-turn, or the event is `rejected`/`over` (discarding those also stops a reject-retry loop within one step). `applyPending()`: apply → accepted: copy, `++ver`, status, over once; rejected: input(`rejected`), move discarded. `draw()`: the local seat. Solo applies in the same GameVM step; nearby will split them.

**Status in the adapter.** The reader walks the Lua table and checks seats with `Roster::isSeat`, so the rule lives in GameCore and one error path (a Lua error in the trampoline) carries the message: `status` must be a table; truthy `over` needs a `winners` list of at most `n` seats; otherwise `turn` must be a seat.

**Arena.** `SCRATCH_RESERVE_BYTES` 16 KiB holds `Session` (about 1.8 KB) and one scratch of `scratchBytes(STORE_LIMIT)` = 13,720 B, shared by every encode (none overlap; 2.10's store reuses it). Lua's 16 B block headers still make a small-object heap fill its arena share before the byte counter reaches 256 KiB; either ends as "not enough memory", and the reserve is untouchable because it is taken first.

**Watchdog.** `LuaGame::callSerial()` increments on every entry (and `close`); `GameVM::runningForMs` restarts its clock when the serial changes, so each Lua call gets 3 s.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `pio run -e default`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (default and `-e x4pro`) -- SUCCESS, no defects.
- `sim.sh build x4pro`, `start x4pro`, `tap`, `ss`, `log` -- screenshots in `story-session-screenshots/`.
- `./bin/clang-format-fix`, `python3 scripts/check_upstream_touches.py` -- no diff, PASS.

**Manual checks:**
- X4 Pro: the tracer to game over; the "VM stopped" high-water line (owner's device run).

**Verification record** (2026-09-27, working tree before commit, on 124ac2cd):
- Host: `ctest` 501/501 passed before review, 504/504 after its patches (`GameCoreTest` 59 with 9 `SessionTest` and `SessionLimitsMatchTheList`; `GameScriptTest` 74 with 8 `SessionGameTest`). `[stack] codec at depth 16: at least 2808 B, about 187 B a level`; tracer headroom at a hook 14,912 of 16,384 B.
- `pio run -e x4pro` SUCCESS (RAM 31.1 %, Flash 88.4 %, 5,791,386 B); `pio run -e default` SUCCESS (RAM 17.7 %, Flash 85.6 %); `pio check` (default) and `pio check -e x4pro` with the three `--fail-on-defect` flags: no defects.
- `./bin/clang-format-fix`: exit 0; `check_upstream_touches.py`: PASS.
- Simulator x4pro, [story-session-screenshots/](story-session-screenshots/): `start.png` (Taps 0 of 5, "solo, 1 seat, api 1"); `rejected.png` (banner tap: "Not on the banner"); `four.png`; `over.png` (Taps 5 of 5, "Game over", "Over events: 1"; log "Round over at ver 6; winners mask 0x1"); `after-over.png` (a further tap changes nothing); `bigstate-error.png` (scratch game: "apply: state is too large (over 1400 bytes)"); `loop-budget.png` and `watchdog.png` (the `loop` fixture's budget and 3 s watchdog still end in the error view). Back from the tracer logged "VM stopped" and returned to Games.

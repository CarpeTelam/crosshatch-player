---
title: 'GameSaveStore for store.bin'
type: 'feature'
ticket: '12'
created: '2026-09-27'
status: 'built'
baseline_revision: '9592a64265d7e79c0698679b2ec446ee44221826'
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

**Problem:** `ch.store` lives only in the match's PSRAM slot (2.10), so a game's saved table is lost when the match ends; nothing reads or writes `/.games-data/<id>/store.bin`.

**Approach:** Add the `GameCore::ISnapshotStore` port and `src/games/GameSaveStore`, its only reader and writer of `store.bin` (6-byte blob header, then the codec payload), which restores a valid saved table into the slot while `GameAssets` loads the game, and writes the slot through `store.bin.tmp` and a rename, from the loop task only; a Lua-free `Codec::check` validates what is restored. The match wires the at-most-every-5 s flush; the round-end and `onExit()` flushes are public calls entry 13 makes.

## Boundaries & Constraints

**Always:**
- AD-17: only `GameSaveStore` touches `store.bin`, on the loop task, through `Storage`/`HalFile`; the VM task never touches `Storage` (AD-5).
- AD-10: restore only a file of at most 6 + 4,096 B whose header is `{"CHST", fileVersion 1, Codec::VERSION}` and whose payload passes `Codec::check` under `STORE_LIMIT` as a table; anything else is discarded with a `LOG_ERR` naming the reason and is never decoded; the slot then starts empty.
- `Codec::check` applies exactly `decode`'s rules and error order (one decoder, two sinks) and needs no Lua state.
- Write: `store.bin.tmp` in full, close, remove `store.bin`, rename (SdFat's rename refuses an existing target). A failed write keeps the slot dirty and removes the partial tmp; a failed rename keeps the complete tmp, which load reads when `store.bin` is missing.
- Periodic flush: only when the slot is dirty and at least `FLUSH_INTERVAL_MS` (5,000) since the last write or the restore.
- Buffers in PSRAM beside the slot; locals under 256 B; no static initializers or mutable statics; `src/games` `.cpp` whole-file `#if FREEINK_CAP_GAMES`.

**Never:** `resume.bin` or `peek()` (epic-install-and-launcher); calling the flush at round end or in `onExit()`, Over, Paused, Leaving (entry 13); deleting a discarded `store.bin`; `api-level-1.txt`, `ApiLevel.h`, `test/game_core/ApiLevelTest.cpp` (2.14); `lib/lua`, `ci.yml`, `.skills/`, the submodule pointer.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Round trip | slot dirty with `{taps = 3}`, flush; new match | `store.bin` = header + payload, no tmp left; slot restored, clean | — |
| No save | no `store.bin`, no tmp | slot empty | none logged |
| Bad header | short, wrong magic, file version 2, codec version 2 | slot empty | `LOG_ERR` with the status name |
| Bad payload | trailing byte, torn table, non-table (`03 02`) | slot empty | `LOG_ERR` with the codec error name or "not a table" |
| Size | payload 4,096 B table; file 6 + 4,097 B | restored; discarded unread | `LOG_ERR` "too large" |
| Crash left tmp | tmp valid, no `store.bin`; tmp torn, `store.bin` valid | restored from tmp (logged); `store.bin` used | — |
| Write fails | open, write, or rename fails | returns false; slot dirty again | `LOG_ERR`; partial tmp removed, complete tmp kept |
| Throttle | dirty at +4,999 ms / +5,000 ms; clean at +5,000 | no write / write / no write | — |
| Immediate flush | dirty, 1 ms after a write | written | — |

</frozen-after-approval>

## Code Map

- `lib/GameScript/Codec.{h,cpp}` -- `Decoder` (anonymous namespace, lines 326-490) pushes through `lua_*` and tests nil with `lua_isnil`; `decode()` at 537. Refactor to a sink template; add `check`.
- `lib/GameScript/BlobHeader.h` -- `writeBlobHeader`, `checkBlobHeader`, `blobHeaderStatusName`, `BLOB_HEADER_BYTES`.
- `lib/GameScript/StoreSlot.{h,cpp}` -- `restore`, `read`, `takeIfDirty`, `dirty`; add `markDirty()` for a failed write.
- `lib/GameCore/IClock.h` -- port style for the new `ISnapshotStore.h`.
- `src/games/GameAssets.{h,cpp}` -- `load(gameId)` on the loop task; moved into `GameVM::create` afterwards.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `onEnter` allocates `storeStorage` (PSRAM, `STORE_LIMIT`) and `store`; `loop` Playing pass; `abandonVm` leaks `store`/`storeStorage` with a live task.
- `lib/hal/HalStorage.h` -- `openFileForRead/Write` (write is `O_RDWR|O_CREAT|O_TRUNC`), `exists`, `remove`, `rename`, `ensureDirectoryExists` (creates parents).
- `test/game_script/CodecTest.cpp` -- `vectors().section(...)`, `parseHex`, `limitOf`; `test/game_script/CMakeLists.txt` -- one executable today; `test/library_builder/stubs/` shows the fake-storage stub pattern.
- `docs/crosshatch/formats.md` -- blob header section; add `store.bin`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameScript/Codec.{h,cpp}` -- `Decoder<Sink>` with a Lua sink and a checking sink; `Error check(data, length, limit, bool& isTable)`.
- [x] `lib/GameScript/StoreSlot.{h,cpp}` -- `markDirty()` (no-op when empty).
- [x] `lib/GameCore/ISnapshotStore.h` -- `loadStore(span out) -> size_t`, `saveStore(span) -> bool`.
- [x] `src/games/GameSaveStore.{h,cpp}` -- per Design Notes.
- [x] `src/games/GameAssets.{h,cpp}` -- `load(gameId, saves, slot)` restores the store after the sources.
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- one PSRAM block for slot and save buffer; `GameSaveStore` member; `flushIfDue` each Playing pass.
- [x] `test/game_script/{CodecTest,StoreSlotTest}.cpp` -- `check` agrees with every encode and decode-error vector; `markDirty`.
- [x] `test/game_script/GameSaveStoreTest.cpp`, `test/game_script/save_store_stubs/{HalStorage.h,Logging.h}`, `CMakeLists.txt` -- second executable over a fake SD whose rename refuses an existing target and whose log is captured; every matrix row.
- [x] `test/game_script/fixtures/counter/{manifest.json,main.lua}` -- tap increments a saved counter; draws it.
- [x] `docs/crosshatch/formats.md` -- `store.bin` section.

**Acceptance Criteria:**
- Given the simulator with `fixtures/counter` in `fs_/.games/counter/`, when the counter is tapped, 5 s pass, and the game is left and reopened, and again after a simulator restart, then it shows the saved count.
- Given a `store.bin` with codec version 2, when the game opens, then it starts from zero and the log shows the discard line.
- Given `ctest`, `pio run -e x4pro` and `-e default`, `pio check` on both, `check_upstream_touches.py`, `./bin/clang-format-fix`, then all pass with no diff.

## Implementation Notes

- Implemented directly (no coding subagent in this session). New: `lib/GameCore/ISnapshotStore.h`, `src/games/GameSaveStore.{h,cpp}`, `test/game_script/GameSaveStoreTest.cpp`, `test/game_script/save_store_stubs/{HalStorage.h,Logging.h}`, `test/game_script/fixtures/counter/`. Changed: `Codec` (`Decoder<Sink>` with `LuaSink` and `CheckSink`, `check`), `StoreSlot` (`markDirty`), `GameAssets` (`load(gameId, saves, store)`), `GameMatchActivity`, `CodecTest`, `StoreSlotTest`, `HostBindingsTest` (the counter fixture from a restored slot), `test/game_script/CMakeLists.txt` (second executable, no change to the upstream `test/CMakeLists.txt`), `docs/crosshatch/formats.md`.
- Deviation from the Design Notes sketch: the flush interval starts in the constructor (`GameSaveStore(gameId, buffer, startMs)`), so `restoreInto(slot)` takes no time and `GameAssets::load` needs no clock.
- `store.bin` = `43 48 53 54 01 01` + payload, nothing else; a length or checksum would repeat what the file size and the canonical, prefix-free codec already catch (a torn payload is `truncated`).
- The round-end and `onExit()` flushes are `GameSaveStore::flush`, not called yet: until entry 13, leaving within 5 s of a `set` loses that set (the counter fixture says so on screen).
- `./bin/clang-format-fix` also reordered the includes of `test/game_script/GameTouchTest.cpp` (from 2.11, formatting only); kept per the orchestrator's rule.
- x4pro flash 5,803,546 B (entry 10: 5,796,314 B, with entry 11 in between); RAM unchanged at 31.1 %.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time against `review.diff`, 51 kB: blind-hunter with a floor of 8, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 0, medium 0, low 7, false 2, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind | Leaving (Back) within 5 s of a `set` loses it: `onExit()` does not flush | low | Rejected, out of scope by the intent: the round-end and `onExit()` flushes are public calls entry 13 makes (`GameSaveStore::flush`); noted in Implementation Notes. |
| 2 | blind | The first `set` after a discarded newer-codec `store.bin` overwrites it | low | Rejected: by design (Design Notes, formats.md); keeping both would need a second file name. |
| 3 | blind | `ensureDirectoryExists` runs on every write | low | Rejected: one exists check per write, at most every 5 s. |
| 4 | blind | After a failed check, the save buffer holds the discarded file's bytes | false | `loadStore` returns 0 and `restoreInto` restores zero bytes; the buffer is scratch, overwritten by the next take. |
| 5 | blind | A game id over 32 bytes is truncated into a wrong path | false | `Manifest::parse` refuses ids over `MAX_ID_BYTES` (32) and `id` holds 33 bytes; the match passes `manifest.id`. |
| 6 | blind | `loop()` ignores `flushIfDue`'s result | low | Rejected: `saveStore` logs every failure and the slot stays dirty for the retry; the match has nothing else to do with it. |
| 7 | blind, verification-gap | Dropping `saves.restoreInto(store)` from `GameAssets::load` or `flushIfDue` from `GameMatchActivity::loop` fails no host test (`grep` of `test/` finds neither class) | low | Defer (`deferred-work.md`): entry 1's harness gap for `src/games`/`src/activities` wiring; the simulator run above covers both. |
| 8 | blind | The diff reorders `GameTouchTest.cpp`'s includes | low | Rejected: whole-tree formatter output, kept by the orchestrator's rule. |
| 9 | edge | A buffer under `BUFFER_BYTES` makes `flush` never write and `loadStore` discard | low | Rejected: the only caller passes `BUFFER_BYTES`, as the constructor's comment requires; a guard adds a branch for no reachable input. |

Edge-case claims check: the Intent and Tasks claims held (restore during `GameAssets::load`, tmp then rename from the loop task, `Codec::check` on the restore path, the periodic flush wired, `flush` public, one PSRAM block of 2 x `STORE_LIMIT`). Deletion check: the removed allocation block moved ahead of `GameAssets::load` unchanged; `lua_isnil` became the decoded tag, equivalent because a value is nil exactly when its tag is. Intent-alignment: readings are (a) the adapter, the restore while assets load, and the periodic flush, with round-end and `onExit()` calls left to entry 13, and (b) the same with those calls wired now; the intent names (a) and the diff implements it. The intent's expectations live at the SD card and the game's `ch.store`; the host tests exercise `GameSaveStore` over a fake card and the counter fixture through `LuaGame`, and the asset-phase and loop wiring in the simulator.

## Design Notes

**Why `check`.** The loop task has no Lua state, and a second state just to validate is heavier than the check: the same `Decoder` runs over a sink that builds nothing, so the check can never drift from `decode`. The nil test moves from `lua_isnil` to the decoded tag.

**GameSaveStore** (`gameId`, a caller buffer of `STORE_LIMIT` B):

```cpp
size_t loadStore(std::span<uint8_t> out) override;  // valid payload or 0 (logged)
bool saveStore(std::span<const uint8_t> payload) override;  // tmp, remove, rename
void restoreInto(GameScript::StoreSlot& slot, uint32_t nowMs);
bool flushIfDue(GameScript::StoreSlot& slot, uint32_t nowMs);  // periodic
bool flush(GameScript::StoreSlot& slot, uint32_t nowMs);  // round end, onExit (entry 13)
```

`flush`: `takeIfDirty` into the buffer, `saveStore`, `markDirty` on failure. The match's PSRAM block is `2 * STORE_LIMIT`: slot first, save buffer second; both leak together with a live VM.

**Discard, not delete.** A discarded file stays until the game's next `set` replaces it, so a store written by a newer codec survives a firmware downgrade that never writes.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `pio run -e default`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (default and `-e x4pro`) -- SUCCESS, no defects.
- `sim.sh build x4pro`, `start`, tap the counter, wait, leave, reopen, restart, `ss`, `log`; then a codec-version-2 `store.bin` -- screenshots beside this plan.
- `./bin/clang-format-fix`, `python3 scripts/check_upstream_touches.py` -- no diff, PASS.

**Verification record** (2026-09-27, working tree before commit, on 9592a642):
- Host: `ctest` 597/597 passed: new `GameSaveStoreTest` 17 (every matrix row: round trip, no save, the four header statuses, six bad payloads, 4,096 B at the limit and 4,097 B discarded unread, whole and torn tmp, the tmp-then-rename op sequence, folder creation, open/write/close failures, rename and remove failures, the 5 s interval and its retry, immediate flush, refused sizes); `CodecVectorsTest.CheckAgreesWithDecodeOnEveryVector`; `StoreSlotTest.MarkDirtyRetriesTheLatestContents`; `HostBindingsTest.TheCounterFixtureStartsFromTheRestoredStore`.
- `pio run -e x4pro` SUCCESS (RAM 31.1 %, Flash 88.6 %, 5,803,546 B); `pio run -e default` SUCCESS (RAM 17.7 %, Flash 85.6 %); `pio check` (default, as CI) and `pio check -e x4pro` with the three `--fail-on-defect` flags: PASSED, no defects.
- `./bin/clang-format-fix`: exit 0, no diff on a second run (it reordered `GameTouchTest.cpp`'s includes once); `check_upstream_touches.py`: PASS (trial merge clean; fork-only paths).
- Simulator x4pro (`sim.sh build x4pro` SUCCESS), `fixtures/counter` in `fs_/.games/counter/`, screenshots in [story-save-store-screenshots/](story-save-store-screenshots/): `open-fresh.png` (Taps 0), `tapped3.png` (Taps 3, saved 3; log `counter: saved ch.store (11 bytes)`, file `43 48 53 54 01 01 06 00 01 05 04 74 61 70 73 03 06`), `reopened.png` (after Back and reopening: `restored ch.store (11 bytes)`, `opened with 3 saved taps`), `restart-b.png` (two simulator restarts with taps between: Taps 6, file ending `03 0c`), `unknown-codec.png` (codec byte set to 2: log `[ERR] [GAME] counter: discarded /.games-data/counter/store.bin: unknown_codec_version`, Taps 0).


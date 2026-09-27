---
title: 'Codec and golden vectors'
type: 'feature'
ticket: '6'
created: '2026-09-27'
status: done
baseline_revision: 'cc87a39878ea2fa02be37bdf3513d05c824b7188'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
  - '{project-root}/docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** AD-10 requires one codec for state, moves, and `ch.store`, with canonical bytes, fixed limits, a versioned blob header, and a Python reference that reproduces device bytes; none exists, and entries 7, 8, 10, and 12 build on it.

**Approach:** Add the C codec (Lua value to codec v1 bytes and back) and the blob header helper in `lib/GameScript`, `scripts/game_codec.py` on `fork_common.py`, one JSON golden vector file in `test/game_script/` that the C host test and the Python sidecar test both run, and the codec section of `docs/crosshatch/formats.md`.

## Boundaries & Constraints

**Always:**
- AD-10 exactly: types nil, boolean, integer, float, string, table with integer or string keys; acyclic, depth at most 16, no metatables, functions, userdata, or threads; integral float keys normalized to integers, other float keys and NaN keys errors; tag byte per value, zigzag LEB128 i64, binary64 LE, LEB128 string length, table as `narr`, array values, `nrec`, sorted pairs (integers ascending, then strings bytewise); limits 1,400 / 256 / 4,096 B on codec output, each a named constant mirrored in the vector file with at-limit and one-over cases.
- The encoder never allocates and never raises: it writes into caller scratch (`Codec::scratchBytes(limit)`, which entry 8 takes from the VM arena). The decoder allocates only through the state's allocator (the arena) and may raise only a Lua memory error, so it runs inside the trampoline. Neither changes the stack on error; no `std::vector`, `new`, or `malloc` in `lib/GameScript`.
- `GameScript` includes no HAL, Arduino, or `Logging` header; `lua.h` users include `<climits>` first and `static_assert(sizeof(lua_Integer) == 8)`; locals under 256 B; recursion bounded by the depth limit.
- `game_codec.py`: standard library only, single quotes, exit contract through `fork_common.exit_code`, sidecar `scripts/game_codec_test.py` picked up by `Fork script tests`.

**Never:** `Session`, `apply`, `ch.store`, limit-breach `ScriptError` wiring (2.8, 2.10), `store.bin` layout or magic (2.12), `proto` (epic-play-nearby); editing `lib/GameCore/`, `test/game_core/`, `src/games/HostCaps*`, `GamesListActivity`, `api-level-1.txt`, `ci.yml`, `.skills/`, or the submodule pointer.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Scalars | nil, booleans, ints (0, ±1, 63, 64, -65, min, max), floats (1.5, 1.0, -0.0, ±inf, NaN) | vector bytes; NaN written as `7ff8000000000000`; subtype kept | — |
| Tables | array run, holes, mixed keys, nested, shared subtable | canonical bytes; a shared subtable is written twice | — |
| Key order and normalization | `[2.0]`, `[-0.0]`, `[-2^63 float]`, strings with high bytes | same bytes as the integer or sorted form | — |
| Encode faults | cycle, 17 levels, metatable, function, boolean or table key, `1.5` / inf / `2^63` key | no output, stack unchanged | `cycle`, `too_deep`, `metatable`, `bad_type`, `bad_key`, `float_key` |
| Limits | output of limit and limit + 1 bytes, for each of the three | at limit encodes and decodes; one over fails both ways | `too_large` |
| Decode faults | empty, short, unknown tag, trailing bytes, overlong or 65-bit varint, non-canonical NaN, nil in array or as value, unsorted or duplicate keys, key that belongs in the array, float/NaN/bool/table key, 17 levels | nothing pushed | `truncated`, `bad_tag`, `trailing`, `non_canonical`, `overflow`, `bad_key`, `too_deep` |
| Blob header | 6 bytes `{magic[4], fileVersion u8, codecVersion u8}` | `ok`, payload at offset 6 | `truncated`, `bad_magic`, `unknown_file_version`, `unknown_codec_version` |
| Python only | NaN key, `str`/`bytes` key collision, int outside i64, unsupported object | — | `nan_key`, `bad_key`, `overflow`, `bad_type` |

</frozen-after-approval>

## Code Map

- `lib/GameScript/ArenaAllocator.h` -- `ArenaAllocator::allocate` and `luaAlloc`; tests back the state and the scratch with one arena, as entry 8 will.
- `lib/GameScript/LuaGame.{h,cpp}` -- trampoline pattern (`enter`); read only, not changed. `ChBindings.cpp` shows the `<climits>` / `static_assert` header order.
- `test/game_script/CMakeLists.txt` -- globs `lib/GameScript/*.cpp`; add `CodecTest.cpp`, `lib/JsonParser/StreamingJsonParser.cpp`, its include dir, and `CODEC_VECTORS_PATH`.
- `test/game_core/ForkReleaseTest.cpp` `VectorCollector` -- pattern for flat JSON records via `StreamingJsonParser` (512 B token buffer: a longer string is silently dropped, so every vector string stays under 512 B).
- `scripts/fork_common.py`, `docs/crosshatch/fork-scripts.md` -- `exit_code`, `SetupError`, `Failure`; test puts `scripts/` on `sys.path`.
- `docs/crosshatch/upstream-touches.md` Game paths -- has `scripts/game_codec.py`; add `scripts/game_codec_test.py`.

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameScript/Codec.{h,cpp}` -- `namespace GameScript::Codec`: `VERSION`, the three limits, `MAX_DEPTH`, `MAX_LIMIT` (65,535), `Error` + `errorName`, `scratchBytes`, `encode`, `decode`, per Design Notes.
- [ ] `lib/GameScript/BlobHeader.{h,cpp}` -- `BLOB_HEADER_BYTES`, `writeBlobHeader`, `checkBlobHeader`, `BlobHeaderStatus` + name.
- [ ] `test/game_script/codec_vectors.json` -- limits as scalars; sections `encode`, `encode_errors`, `decode_errors`, `headers` covering the matrix.
- [ ] `test/game_script/CodecTest.cpp`, `CMakeLists.txt` -- constants equal the scalars; every vector through an arena-backed state; C-only: light userdata and thread, small or misaligned scratch, stack balance.
- [ ] `scripts/game_codec.py` -- `encode`, `decode`, header helpers, notation parser, `run_vectors`; CLI `check [FILE]`, `encode VALUE`, `decode HEX` with `--limit`.
- [ ] `scripts/game_codec_test.py` -- the vector file passes; each Python-only row; CLI 0/1/2; every vector string under 512 B.
- [ ] `docs/crosshatch/formats.md` -- create: codec v1 bytes, canonical and decode rules, limits, errors, blob header, vector file and notation.
- [ ] `docs/crosshatch/upstream-touches.md` -- add `scripts/game_codec_test.py` to Game paths.

**Acceptance Criteria:**
- Given `cmake`/`ctest` on `test/` and `python3 scripts/game_codec_test.py`, then both pass every vector from the one file.
- Given `pio run -e x4pro` and `-e default`, then both build; `./bin/clang-format-fix` leaves no diff; `check_upstream_touches.py` passes.

## Implementation Notes

- Implemented directly (no coding subagent in this session). Files: `lib/GameScript/{Codec,BlobHeader}.{h,cpp}`, `test/game_script/{CodecTest.cpp,codec_vectors.json,CMakeLists.txt}`, `scripts/game_codec{,_test}.py`, `docs/crosshatch/formats.md`, and `scripts/game_codec_test.py` added to the ledger's Game paths (`game_codec.py` was already there).
- The encode vectors' hex was produced with `game_codec.py encode` and checked by hand for the varint, zigzag, float, key-order, and limit cases; the C codec, written separately, then passed all of them unchanged, and a three-vector mutation (one per kind) failed both codecs.
- Lua already turns integral float keys into integers and refuses NaN keys, so from Lua the C encoder only ever sees non-integral or out-of-range float keys; its normalization and `nan_key` branch are there for parity. The NaN-key case both codecs share is the decode vector (a float key tag is `bad_key`); the encode side is Python-only (`game_codec_test.py`).
- `Codec::errorName` gives the vector names; entry 8 builds its `ScriptError` message from it. The C-only `scratch` and `no_memory` errors are caller or arena faults, never vectors.
- `./bin/clang-format-fix` also reformatted `lib/GameScript/CanvasClip.h` and `test/game_script/CanvasClipTest.cpp` from entry 1's fix commit (comment alignment only), at the orchestrator's request.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time against `review.diff`: blind-hunter with a floor of 10, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 0, medium 0, low 10, false 1, maybe-false 1.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind | `run_vectors` calls `same()` on an encode vector's value even when the codec refused it; `same()` then raises `CodecError`, so `check` dies with a traceback instead of a failure line | low | Patched: the comparison goes through `_outcome`, so a refused value counts as a mismatch. |
| 2 | blind | `Codec.h` and formats.md say encode "never allocates", but `lua_checkstack` can grow the Lua stack through the state's allocator | low | Patched: both now say encode allocates nothing itself and can only grow the Lua stack through the arena, reporting `no_memory` if that fails. |
| 3 | blind | `CodecTest::push` drops Lua's message when a vector fails to load | low | Patched: load and run failures share one `ADD_FAILURE` with the message. |
| 4 | blind | The codec recurses up to 16 tables deep on the 16 KB `GameVM` stack; its C stack use is not measured | maybe-false | Deferred (unverified, medium if true): about 200 B a level by reading, so about 3.2 KB at depth 16 plus `std::sort`; entry 8 runs the codec on the task and can measure the high-water mark with a depth-16 state. |
| 5 | blind, edge | `Decoder::table` casts `narr` to `int` for `lua_createtable`; a caller limit past `INT_MAX` could overflow it | low | Rejected: `narr` is at most the input length, which is at most the caller's limit, and every caller passes one of the three constants (at most 4,096); a guard adds a branch for no reachable input. |
| 6 | blind | `writeBlobHeader` and `checkBlobHeader` read four bytes from `magic`, so a shorter C string reads past its end | low | Rejected: the header documents the first four bytes; entry 12 passes a four-letter literal; an array-reference parameter would not take the test's runtime strings. |
| 7 | blind | `game_codec.py encode -math.huge` is read by argparse as an option | low | Rejected: standard argparse behaviour; `encode -- -math.huge` works. |
| 8 | blind | A formats.md paragraph was wrapped past the file's line width | low | Patched with 2's rewrite. |
| 9 | blind | `CodecTest`'s `parseHex` does not reject malformed hex | low | Rejected: test-only, and `game_codec_test.py` rejects malformed hex in the same file in the same CI run. |
| 10 | blind, verification-gap | The documented Lua memory error from `decode` in a full arena has no test | low | Patched: `DecodeInAFullArenaRaisesAMemoryError` fills the arena, decodes a 4 KB string in a protected call, and expects `LUA_ERRMEM` without `decode` returning. |
| 11 | edge | `decode(nullptr, n)` with `n > 0` reads through a null pointer | false | A caller contract, not a reachable path: every caller passes a buffer it owns; `(nullptr, 0)` is handled (`truncated`, covered by the empty-input vector). |
| 12 | intent | Intent readings: (a) codec, header helper, Python twin, shared vectors, formats.md codec section; (b) the same plus enforcing the limits as `ScriptError`s | low | Rejected: the diff implements (a); (b) belongs to entries 8 and 10 per the plan's Never list and the ticket. The diff also reformats `CanvasClip.h` and its test, outside the intent, at the orchestrator's request; formats.md's header and vector sections serve the intent's "records the bytes". |

Claims check (edge lens): every Tasks & Acceptance claim held against the code; none falsified. Verification-gap lens: no gaps beyond 10; the new suite is registered (`gtest_discover_tests`) and `game_codec_test.py` is picked up by the `Fork script tests` loop.

## Design Notes

**Byte details (recorded in formats.md).** Tags: nil 0, false 1, true 2, int 3, float 4, string 5, table 6. The outermost table is depth 1. Decoding accepts only canonical bytes, so `encode(decode(b)) == b` for every accepted `b`: overlong varints, non-canonical NaN, nil array or record values, and record keys out of order, duplicated, or in `1..narr+1` are `non_canonical`. `-0.0` keeps its sign (observable, platform-independent); NaN bits are not, so they are fixed. A shared subtable is acyclic and allowed. Check order is fixed so both codecs report the same error for one fault: decode reads bytes strictly in order (length over limit first; `narr > remaining` and `nrec > remaining / 3` are `truncated`); on entering a table encode checks cycle, then depth, then metatable. A value with several faults may report any of them; vectors carry one fault each.

**Encoder without allocation.** Scratch = pair spans (`{uint16 start, uint16 length}`, `limit / 3 + MAX_DEPTH + 1` of them) + output (`limit`) + sort area (`limit`); 13,720 B for the store, 4,732 B for a snapshot. Per table: one `lua_next` pass validates and counts record keys, array values follow, then each record pair is written in `lua_next` order with its span pushed; `std::sort` (in place) orders the spans by the key bytes, and the pairs are copied through the sort area back into place. A full span stack means the output would pass the limit, so it is `too_large`.

```cpp
const size_t size = Codec::scratchBytes(Codec::SNAPSHOT_LIMIT);
void* scratch = arena.allocate(size);  // entry 8; null-check
const Codec::Encoded out = Codec::encode(L, -1, Codec::SNAPSHOT_LIMIT, scratch, size);
if (out.error != Codec::Error::None) luaL_error(L, "state %s", Codec::errorName(out.error));
```

**Vector notation.** Values are a Lua subset that the C test runs with `load` in an env holding the helpers, and `game_codec.py` parses: literals, `'`/`"` strings with `\\ \' \" \n \t \xHH`, table constructors, `a / b`, `math.huge|maxinteger|mininteger`, `string.rep`, `setmetatable`, `fn()`, `nest(n, v)`, `shared(t)`, `cycle(t, k...)`. Hex is space-separated byte groups with an optional `*N` repeat (`05 f5 0a 61*1397`). Python models strings as `bytes` (`str` accepted), tables as `dict`/`list` or `Table` (a `dict` with `metatable`, hashable by identity); a strict `same()` compares subtype, NaN, and the sign of zero.

**Blob header.** Four magic bytes chosen by each file's owner (entry 12 picks `store.bin`'s), `fileVersion` u8, `codecVersion` u8 = `Codec::VERSION`; checks run in that order.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `for t in scripts/*_test.py; do python3 "$t" || echo FAILED; done`; `python3 scripts/game_codec.py check` -- exit 0.
- `pio run -e x4pro`, `pio run -e default` -- SUCCESS.
- `./bin/clang-format-fix`, `python3 scripts/check_upstream_touches.py` -- no diff, PASS.

**Verification record** (2026-09-27, working tree after the review patches):
- Host: `ctest` 436/436 passed (the 12 codec tests: 4 `CodecVectorsTest`, 8 `CodecTest`, every vector in `codec_vectors.json` through an arena-backed state).
- `python3 scripts/game_codec_test.py`: 21 tests OK; `python3 scripts/game_codec.py check`: exit 0; every `scripts/*_test.py` passes.
- Mutation check: changing one encode, one decode-error, and one header vector made both `GameScriptTest` and `game_codec.py check` fail on exactly those vectors (exit 1).
- `pio run -e x4pro` SUCCESS (Flash 88.2 %, RAM 31.1 %); `pio run -e default` SUCCESS; `pio check` (default, as CI) and `pio check -e x4pro`: no defects.
- `./bin/clang-format-fix`: no diff after the run; `check_upstream_touches.py`: run on the commit (see the report).

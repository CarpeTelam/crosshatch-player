# Game byte formats

The fork's byte formats for games. Each section names the code that is the only writer and reader of its bytes.

## Codec v1

One codec encodes game state, moves, and `ch.store` in every mode (AD-10). `lib/GameScript/Codec.{h,cpp}` is the
device codec; `scripts/game_codec.py` is the reference codec for tooling. Both pass the golden vectors in
`test/game_script/codec_vectors.json` (see [Golden vectors](#golden-vectors)), so a change to either that alters one
byte fails a test.

### Values

The codec takes nil, booleans, integers, floats, strings, and tables whose keys are integers or strings. A table
must be acyclic, have no metatable, and be nested at most 16 deep (the outermost table is depth 1). A table reached
twice without a cycle is allowed and written twice; it decodes as two tables. Functions, userdata, and threads are
refused.

Integers are Lua's 64-bit integers and floats are IEEE-754 binary64; the codec keeps the subtype, so `1` and `1.0`
differ. A float key with an integral value inside int64 is written as that integer (`[2.0]` and `[-0.0]` are keys
`2` and `0`); any other float key, and a NaN key, is refused.

### Bytes

Every value is one tag byte and a payload. Multi-byte numbers are little-endian.

| Tag | Value | Payload |
| --- | --- | --- |
| `00` | nil | none |
| `01` | false | none |
| `02` | true | none |
| `03` | integer | zigzag of the int64 (`(n << 1) ^ (n >> 63)`), as a LEB128 varint |
| `04` | float | 8 bytes, binary64; every NaN is written as `00 00 00 00 00 00 f8 7f` |
| `05` | string | LEB128 byte length, then the bytes (any bytes; no encoding is implied) |
| `06` | table | LEB128 `narr`, `narr` values, LEB128 `nrec`, `nrec` key/value pairs |

A LEB128 varint holds 7 bits per byte, low bits first, with `0x80` set on every byte but the last; it has at most 10
bytes. In a table, `narr` is the length of the longest run of non-nil values at keys `1, 2, ...`; those values are
written in key order without their keys. Every other entry is a record pair: the key (tag `03` or `05` and its
payload) and then the value, with pairs sorted by key: integers ascending, then strings bytewise (shorter first when
one is a prefix of the other).

Examples: `{}` is `06 00 00`; `{1, nil, 3}` is `06 01 03 02 01 03 06 03 06` (one array value, then key `3`);
`{a = 1}` is `06 00 01 05 01 61 03 02`.

### Canonical form

The encoding is canonical: a value has exactly one encoding, so equal states give equal bytes on every device. The
sign of zero is kept (`-0.0` is observable in Lua and the same everywhere); NaN bits are not, so they are fixed.

The decoder accepts only canonical bytes: for every input it accepts, encoding its result gives the input back. It
refuses an overlong varint, a NaN with other bits, nil in the array run or as a record value, record keys out of
order or repeated, and a record key in `1..narr + 1` (it belongs in, or would extend, the array run).

### Limits

Limits apply to codec output, and the decoder refuses a longer input before reading it:

| Use | Constant | Limit |
| --- | --- | --- |
| State snapshot | `Codec::SNAPSHOT_LIMIT` | 1,400 B |
| Move | `Codec::MOVE_LIMIT` | 256 B |
| `ch.store` | `Codec::STORE_LIMIT` | 4,096 B |

Nesting is limited to `Codec::MAX_DEPTH` (16) tables. The vector file repeats each constant, and both codecs check
their own constants against it.

### Errors

Both codecs report failures by the same names.

| Name | Encode | Decode |
| --- | --- | --- |
| `too_large` | output over the limit | input over the limit (checked first) |
| `too_deep` | a 17th nested table | a 17th nested table |
| `cycle` | a table inside itself | |
| `metatable` | a table with a metatable | |
| `bad_type` | a function, userdata, or thread | |
| `bad_key` | a key that is not an integer or string | a key tag other than `03` or `05` |
| `float_key` | a non-integral or out-of-range float key | |
| `nan_key` | a NaN key (Python only; Lua refuses NaN keys itself) | |
| `overflow` | an integer outside int64 (Python only) | a varint over 64 bits |
| `truncated` | | the input ends inside a value |
| `bad_tag` | | an unknown tag |
| `trailing` | | bytes after the value |
| `non_canonical` | | bytes the encoder would not write |

The decoder reads strictly in order and reports the first fault it meets; both codecs check in the same order. When
encoding, a table is checked for a cycle, then depth, then a metatable, before its keys and values; a value with
several faults may report any of them. The C codec also has `scratch` (its caller's buffer is too small or
misaligned) and `no_memory` (the Lua stack cannot grow), which no vector produces.

### Memory

`Codec::encode` never raises a Lua error and allocates nothing itself; the only memory it can take is a larger Lua
stack, through the state's allocator (the arena), and it reports `no_memory` when that fails. It writes into caller
scratch of `Codec::scratchBytes(limit)` bytes (4,732 B for a snapshot, 920 B for a move, 13,720 B for the store): the
output, an equal area for reordering record pairs, and one 4-byte span per possible pair. `Codec::decode` allocates
its tables and strings through the state's allocator, so the VM arena holds everything; a full arena raises a Lua
memory error, so it runs inside a protected call.

## Blob header

Every persisted codec blob starts with a 6-byte header (`lib/GameScript/BlobHeader.{h,cpp}`,
`game_codec.blob_header` and `check_blob_header`):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic, four bytes chosen by the file's owner |
| 4 | 1 | file version, the layout of this file |
| 5 | 1 | codec version, `Codec::VERSION` (1) |

A reader checks, in order, that the header is whole (`truncated`), that the magic is its file's (`bad_magic`), that
the file version is the one it reads (`unknown_file_version`), and that the codec version is this codec's
(`unknown_codec_version`). Anything but `ok` is discarded with a log line and never decoded. The file's own fields
and the codec payload follow at offset 6.

## store.bin

`/.games-data/<id>/store.bin` holds a game's `ch.store` table on this device (AD-17). `src/games/GameSaveStore` is
its only reader and writer, on the loop task only; games have no file API.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 6 | blob header: magic `CHST` (`43 48 53 54`), file version 1, codec version 1 |
| 6 | 3 to 4,096 | the store table's codec bytes (at most `Codec::STORE_LIMIT`; `{}` is 3 bytes) |

The file has no other fields; the payload runs to the end of the file. `{taps = 3}` is saved as
`43 48 53 54 01 01 06 00 01 05 04 74 61 70 73 03 06`.

**Reading** happens once, before the game's VM starts, and fills the match's `ch.store` slot. The save is used only
when the file is at most 4,102 bytes (checked before it is read), its header checks `ok`, and its payload passes
`Codec::check` (the decoder's rules without a Lua state) under `STORE_LIMIT` as a table. Anything else is discarded
with a `LOG_ERR` line naming the header status, the codec error, `too large`, or `not a table`, and the game starts
with an empty store. A discarded file is left in place until the game's next write replaces it, so a store written
by a newer codec survives a firmware that cannot read it and never writes.

**Writing** replaces the whole file: the header and payload go to `store.bin.tmp`, which is closed, then `store.bin`
is removed and the tmp renamed over it (SdFat's rename refuses an existing target). A failed write removes the
partial tmp and leaves `store.bin` as it was. Once the tmp is whole, a stop before the rename leaves it in place, and
the next read uses `store.bin.tmp` when `store.bin` is missing; a torn tmp fails the payload check. While the game
runs, the match writes a changed store at most every 5 s (`GameSaveStore::flushIfDue`); `GameSaveStore::flush`
writes it at once, for round end and the match's `onExit()` (AD-17).

## Golden vectors

`test/game_script/codec_vectors.json` holds the limits as top-level numbers and four lists of flat records:

- `encode`: `value` encodes to `hex` within `limit`, and `hex` decodes to a value equal to `value` (same subtype, NaN
  equal to NaN, sign of zero counted).
- `encode_errors`: `value` fails with `error`.
- `decode_errors`: `hex` fails with `error`.
- `headers`: `hex` checked against `magic` and `file_version` gives `status`; for `ok`, writing the header gives its
  first 6 bytes.

`limit` is `snapshot`, `move`, or `store` (the default). Every error vector breaks exactly one rule. Every string in
the file stays under 512 bytes, because the C test's JSON reader drops longer tokens; `game_codec_test.py` checks
this.

`hex` is space-separated groups of hex byte pairs, each optionally followed by `*N` to repeat the group N times:
`05 f5 0a 61*1397` is a 1,397-byte string of `a`, and `0601*17` is 17 copies of `06 01`.

`value` is a Lua expression in a small subset, which the C test runs with Lua itself and `game_codec.py` parses:

- `nil`, `true`, `false`; decimal numbers (a decimal integer past int64 is a float, as in Lua); unary `-`; `/`
  between numbers (always a float, so `0/0` is NaN); `math.huge`, `math.maxinteger`, `math.mininteger`.
- Strings in `'` or `"` with the escapes `\\`, `\'`, `\"`, `\n`, `\t`, `\xHH`, and decimal `\ddd`.
- Table constructors with positional fields, `name = v`, and `[k] = v`; a nil value leaves the key out. Do not mix
  positional fields with explicit keys they would overwrite.
- `string.rep(s, n)`; `setmetatable(t, mt)`; `fn()`, a function; `nest(n, v)`, `v` inside `n` tables at key 1;
  `shared(t)`, `{t, t}`; `cycle(t, k1, ..., kn)`, which sets `t[k1]...[kn] = t` and returns `t`.

`python3 scripts/game_codec.py encode VALUE` prints the bytes of a value in this notation and `decode HEX` prints a
value, which is how new vectors are written; `check` runs the whole file.

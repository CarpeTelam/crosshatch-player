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
(`unknown_codec_version`). Anything but `ok` is discarded with a log line and never decoded, with one exception:
`resume.bin` keeps a save whose file or codec version is newer than this firmware's, as one this host cannot start
(see [resume.bin](#resumebin)). The file's own fields and the codec payload follow at offset 6. A later layout of
`resume.bin` keeps this 6-byte header, and a codec-only bump keeps the package hash at its offset, which that rule
relies on.

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
the next read uses `store.bin.tmp` when `store.bin` is missing; a torn tmp fails the payload check. A write that
finds `store.bin` missing and the tmp present first renames the tmp to `store.bin` (and fails without writing if
that rename fails), so a failed write never truncates or removes the only copy. The trade-off: while that rename
keeps failing, no newer store is written; the older copy stays, and the slot stays dirty, so every flush retries.
While the game
runs, the match writes a changed store at most every 5 s (`GameSaveStore::flushIfDue`); `GameSaveStore::flush`
writes it at once, for round end and the match's `onExit()` (AD-17).

## resume.bin

`/.games-data/<id>/resume.bin` holds the latest snapshot of a solo or pass match, so the match can continue after the
device sleeps (AD-17); a nearby match keeps none. `src/games/GameSaveStore` is its only reader and writer, on the loop
task only; the VM task never touches Storage (AD-5). It sits beside `store.bin` and is written and read the same way.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 6 | blob header: magic `CHRS` (`43 48 52 53`), file version 1, codec version 1 |
| 6 | 8 | package hash: the 8 bytes of `.pkg`'s hash (see [`.pkg` and the package hash](#pkg-and-the-package-hash)) |
| 14 | 1 | mode: 0 (solo) or 1 (pass); any other byte is refused as `unknown mode` |
| 15 | 1 | seats saved, n: 1 for solo; for pass, the match's seat count (2 to 16) |
| 16 | 2 | ver, unsigned 16-bit, little-endian: the low 16 bits of the Session's ver |
| 18 | 1 to 1,400 | the snapshot's codec bytes (at most `Codec::SNAPSHOT_LIMIT`) |

The snapshot runs to the end of the file. For the package hash `0530a15766e91bf1`, a save of ver 7 holding
`{taps = 3}` is `43 48 52 53 01 01 05 30 a1 57 66 e9 1b f1 00 01 07 00 06 00 01 05 04 74 61 70 73 03 06`; the same
save of a two-seat pass match has `01 02` for its mode and n. The mode bytes are the file's own constants
(`RESUME_MODE_SOLO`, `RESUME_MODE_PASS`), never `GameCore::Mode`'s values.

The file version stays 1: pass saves added a mode value, not a field, so a solo save from before them is the same bytes
and still loads. A firmware from before pass saves refuses a pass save as `not a solo save` and keeps the file.

`GameSaveStore::setRoster` tells the store who plays the match, before its first write: a solo roster (the default
until it is called) writes mode 0 and n 1, a pass roster mode 1 and its seat count, and a nearby roster turns
`resume.bin` off, so nothing is read, written, or deleted, as without the package hash. A match that resumes a save
through the new `loadResume` (below) writes the roster it loaded, so a resumed pass match stays a pass save; a
`setRoster` after that load wins.

Each of `peek` and `loadResume` has two forms. The older ones, `peek(id, pkgHash)` and `loadResume(ver, unreadable)`,
accept a solo save only; no firmware code calls them (the host suites still do). The newer ones,
`peek(game, pkgHash, host)` and `loadResume(ver, unreadable, game, host, saved)`, take the game's manifest and the
host's caps. The game's title screen calls the new `peek` once, when it opens. The match's Continue calls the new
`loadResume`; when that loads nothing, the match calls `peekResume(game, host)` to learn why (below). `peekResume` is
the new `peek` for the store's own id and package hash. It reads through the store's buffer, which nothing references
after a `loadResume` that loaded nothing, so it allocates nothing while the match holds its assets; it never changes the
store's roster; and it reads nothing without the package hash or with a nearby roster, as `loadResume` does. Both forms
accept a save that game can start on that host: solo with n 1, or pass with n from max(2, `seats.min`) to the least of
`seats.max`, the host's `maxSeats`, and 16 (`Roster::MAX_SEATS`), each mode only where `Manifest::check(host)` starts
it. A pass game with no seat count that fits the host cannot start pass there, so its pass save is `mode not startable`.
The new `loadResume` gives the saved roster in `saved` (`Roster::solo()` or `Roster::pass(n)`, and `Roster::solo()` when
it loads nothing).

**Usable** is what `GameSaveStore::peek` answers `Valid` for, and it is exactly what the `loadResume` of the same form
accepts: the header checks `ok`, the package hash is the installed package's, the mode and n are ones the form accepts
(above; checked after the package hash, so another package's save is `other package` whatever its mode), and the
snapshot is 1 to 1,400 bytes that pass `Codec::check` as canonical codec. Of a file longer than 1,418 bytes only the
fixed part is read. The static `peek` reads the whole file into a buffer allocated for the call. It answers one of four
things:

- `Valid`: the game's title screen offers its Continue row (the title screen calls `peek` once, when it opens);
- `Unstartable`: a save of this package that the form, game, or host cannot resume: a well-formed mode or seat count it
  cannot start (`mode not startable`, `seats not startable`), or a later firmware's save: a mode byte this firmware does
  not write (`unknown mode`, 2 to 255), a file version above 1 (`newer file version`; its layout is unknown, so its
  package is not checked: it sits in this game's folder), or, with this package's hash, a codec version above this
  firmware's (`newer codec version`; another package's save is `other package` whatever its codec version) or a
  snapshot over 1,400 bytes (`too large`). `newer codec version` and `too large` are checked after the mode and seat
  count, the codec first: such a save with a `bad seat count` is `None`, one with a mode or seat count this host cannot
  start is kept under that reason, and one with both a newer codec and an oversized snapshot is `newer codec version`.
  One `LOG_INF` line says it is kept.
  The title screen offers no Continue row for it, but it is a save: a New row asks before it replaces it, as over any
  save;
- `None`: no file, or one that was read and is not usable for another reason (the header status, an older or garbage
  version included, `other package`, `bad seat count`, `empty snapshot`, `truncated`, or the codec's error).
  `bad seat count` is a seat count no save of its mode has (solo n other than 1, pass n below 2 or over 16), a malformed
  file. One log line names why, at `LOG_ERR`, except a save of another package, which is `LOG_INF` because the title
  screen asks again each time it opens and the save is not broken. `loadResume` logs these refusals at `LOG_ERR`
  ("discarded"), and an `Unstartable` save at `LOG_INF`, as kept, as `peek` does. The file stays, so a save of another
  package, or a malformed one, survives until the game's next match replaces it;
- `Unreadable`: a file is there and could not be checked, because it would not open (`cannot open`) or read
  (`cannot read`) or the buffer could not be allocated, whatever the save's mode. A card or heap fault may pass, so it
  is not `None`: the title screen still offers the Continue row, so that a new match is not the only choice offered
  over a save that may be good.

The match applies the same checks when it starts with `Start::Resume` (`GameSaveStore::loadResume`), and plays the
roster the save records, whatever roster Continue passed. When `loadResume` loads nothing, the match asks `peekResume`
why. A `None` save starts a new match, since nothing usable is lost; that match plays the roster Continue passed, the
title screen's first New row that can start (solo for a game that starts solo). An `Unstartable` save stops the match in
the error view with its own reason, "This saved game cannot be continued on this device", and `resume.bin` is left as it
was. So does a save the match cannot read: `Unreadable` from either read, or `Valid` from the second, which means the
file changed between the two reads. So does a save the VM refuses although `peek` accepted it. Those show the error view
"The saved match could not be resumed", with `resume.bin` left as it was. Back then goes to Games, where the game's row
opens its title screen: Continue can be tried again there, and a New row replaces the save on purpose, after asking.
(`GameVM::setResume` and `Session::restore` refuse only an empty or oversized snapshot, which `peek` already excludes,
so the VM's refusal cannot happen today; the game's own rules run at its first call, below.) Without a valid `.pkg`
there is no hash, and a new match neither reads, writes, nor deletes `resume.bin` (a file there is not known to be this
package's, so entering Over leaves it); a Continue whose `.pkg` will not read stops in the same error view rather than
play new.

A game that fails on the resumed state (a script error at its first `status` or `draw`) does not delete the save either.
The failure cannot tell a game that rejects the state, every time, from a transient fault (a callback that runs out of
heap is a script error too), and deleting on the second would lose a good save. Continue shows the error view again on
each try until a new match, started from a New row of the game's title screen, replaces the file with its first
snapshot.

**Writing** is `store.bin`'s: the header and snapshot go to `resume.bin.tmp`, which is closed, then `resume.bin` is
removed and the tmp renamed over it. A failed write removes the partial tmp and leaves `resume.bin` as it was, a stop
after the tmp is whole leaves the tmp as the copy that is read while `resume.bin` is missing, and a write that finds
that state renames the tmp first. The VM hands each snapshot it commits to the loop task through a latest-wins mailbox
(`src/games/SnapshotMailbox.h`), and the loop task writes the newest one on its next pass, so a pass that finds two
snapshots writes the later. A failed write keeps the snapshot pending: the loop retries `FLUSH_INTERVAL_MS` (5 s) after
the failure, Leave and the forced exit included. The match writes only in Playing and Paused, only for a
snapshot whose status is not over, and deletes `resume.bin` and its tmp instead when the latest snapshot is over or
when it enters Over (a finished round never resumes; a delete the card refuses is retried at most every 5 s in Over and Paused, and at Leave and the forced exit regardless, until it succeeds; the forced exit's SD steps stop starting 1,500 ms after it began). Leave keeps the file. The `ver` a resumed match continues from
is the file's, so a match that has passed 65,535 snapshots wraps in the file (the spine's `u16`) and only there.

## prefs.bin

`/.games-data/<id>/prefs.bin` holds a game's remembered choices on this device (AD-17, as amended 2026-10-02): the mode
last started and the value chosen for each setting the manifest declares (AD-15). `src/games/GameSaveStore` is its only
reader and writer (`loadPrefs`, `savePrefs`), on the loop task only, never in `render()` or `onExit()`; the game's
title screen reads it when it opens and writes it when its Options screen closes with a change and when New game starts
a mode other than the one the file holds (a missing file holds none). The match never touches it. It is not a codec
blob, so it has no blob header.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `CHPF` (`43 48 50 46`) |
| 4 | 1 | file version, 1 |
| 5 | 1 | mode: a `GameCore::Manifest::Mode` bit (1 solo, 2 pass, 4 nearby), or 0 for none |
| 6 | 1 | count: the settings that follow, 0 to 4 |
| 7 | | per setting: the id's length (1 to 16), the id, the value's length (1 to 16), the value |

The file ends after the last setting, so it is at most 143 bytes (`GameSaveStore::PREFS_MAX_BYTES`). Ids and values are
the manifest's strings, stored as text so that a manifest that changes is resolved value by value. The mode byte is the
manifest's mode bit, not `resume.bin`'s mode byte (where 0 is solo), so that 0 can mean none. For a pass match with
`level` = `Hard` and `sound` = `Off` the file is
`43 48 50 46 01 02 02 05 6c 65 76 65 6c 04 48 61 72 64 05 73 6f 75 6e 64 03 4f 66 66`.

**Reading** (`loadPrefs`) reads the fields straight into the caller's `GameSaveStore::Prefs`, with no buffer of its
own, from `prefs.bin`, or from a whole `prefs.bin.tmp` when `prefs.bin` is missing. It answers `None` (no file),
`Loaded`, `Malformed` (a file over 143 bytes, shorter than its head, with another magic or version, more than four
settings, an empty or overlong id or value, a length past the end, or bytes after the last setting), or `Unreadable`
(the file would not open or read). None of them is an error: `Malformed` and `Unreadable` log one `LOG_INF` line, every
answer but `Loaded` leaves the choices empty, and the file stays until the next write replaces it.

**Resolving** (`GameSaveStore::resolvePrefs`) turns what the file holds into the title screen's current choices, value
by value: the mode is `Manifest::startMode` (the remembered mode when this host can start it, else `default_mode` when
it can, else the first it can start in solo, pass, nearby order; a mode byte that is not one bit is never one it can
start), and each setting the manifest declares takes the value stored under its id when the manifest still lists that
value, else its default. A stored setting the manifest no longer declares is ignored. Each fallback is logged at
`LOG_DBG`.

**Writing** (`savePrefs`) is `store.bin`'s: the bytes go to `prefs.bin.tmp`, which is closed, then `prefs.bin` is
removed and the tmp renamed over it, and a write that finds only the tmp renames it first. A failed write is logged at
`LOG_ERR` and leaves `prefs.bin` as it was; the title screen keeps the choice until it closes. Removing the game keeps
the file, as it keeps the rest of `/.games-data/<id>/`.

## Game package (`.chgame`)

A game ships as one `.chgame` file (AD-15). `scripts/pack_game.py` writes it; `src/games/GamePackageInstaller` is the
only code that installs one (AD-16). Opening Games installs every `/games/*.chgame` (any letter case for the
extension; a name that starts with `.`, such as a Mac's `._name.chgame` sidecar, is skipped), so a person puts packages there with the web file manager or USB. The name of the file means nothing:
the manifest's `id` names the game. The extension was `.cpgame` until 2026-09-30; none shipped, and the installer
ignores a `.cpgame` file (it is neither installed nor renamed).

### Package

A package is a zip whose members are stored or deflated (no ZIP64) and sit at the top level. The whitelist is exact:

| Member | Rule |
| --- | --- |
| `manifest.json` | required; parsed by `GameCore::Manifest::parse`, the one manifest parser |
| `main.lua` | required |
| `<name>.lua` | `<name>` is `[a-z0-9_]{1,32}`; loaded by `require("<name>")` |
| `<name>.png` | `<name>` is `[a-z0-9_]{1,32}`; a non-interlaced PNG; `icon.png` is the launcher icon, `title.png` the title screen's splash, `handoff.png` the hidden hand-off page (the reserved images), any other is a game image |

Anything else makes the package invalid: a folder, a path with `/` or `\`, `..`, an upper-case name, a name over the
limit, a `.bmp` (images are `.png` only; the installer converts them), and the same name twice.

| Limit | Constant (`lib/GameCore/PackageLimits.h`, `GameImages.h`) | Value |
| --- | --- | --- |
| Package file | `PACKAGE_BYTES` | 262,144 B |
| Members | `PACKAGE_MEMBERS` | 32 |
| One member, uncompressed | `MEMBER_BYTES` | 131,072 B |
| The `.lua` members together, uncompressed (`GameAssets::MAX_SOURCE_BYTES`) | `LUA_SOURCES_BYTES` | 262,144 B |
| Name of a `.lua` or `.png` member, without its extension | `MEMBER_STEM_BYTES` | 32 |
| Converted images, all `.bmp` files but `icon.bmp` | `IMAGES_BYTES` | 131,072 B |
| Converted images | `MAX_IMAGES` | 32 |
| Side of `icon.bmp` | `ICON_PIXELS` | 64 |
| Width x height of `title.png` (`GameImages.h`) | `TITLE_IMAGE_WIDTH` x `TITLE_IMAGE_HEIGHT` | 480 x 480 |
| Width x height of `handoff.png` (`GameImages.h`) | `HANDOFF_IMAGE_WIDTH` x `HANDOFF_IMAGE_HEIGHT` | 480 x 800 |
| Width x height of a `.png` | `IMAGE_MAX_WIDTH` x `IMAGE_MAX_HEIGHT` | 2,048 x 3,072 |
| Nesting of `manifest.json`, the root object included (`lib/JsonParser/StreamingJsonParser.h`) | `StreamingJsonParser::MAX_NESTING` | 32 |

The installer counts converted images as the game loader does: each `.bmp` is 62 + ceil(width / 32) * 4 * height
bytes, header included, and `icon.bmp` is left out.

The installer reads the zip's directory itself (`src/games/ZipDirectory.h`) before it extracts anything, and rejects,
each with its own reason: a file over `PACKAGE_BYTES`; a member declaring more than `MEMBER_BYTES`; `.lua` members
over `LUA_SOURCES_BYTES` together; more members than `PACKAGE_MEMBERS`; ZIP64, encryption, or a compression method
other than stored and deflate; a stored member whose two sizes differ; an EOCD entry count that differs from the
directory's; two members that share bytes of the file. The EOCD must be the last 22 bytes (a zip comment is refused,
and `pack_game.py` writes none) and the central directory must end where it begins. Each member then streams
through `ZipFile` into a guard that never lets more than the declared size reach the card, refuses a `.lua` whose
first byte is Lua's bytecode signature (ESC, 0x1B), and checks the CRC-32. `pack_game.py` refuses a package over the
limits it knows, including the `.lua` total.

### Images

The installer converts every `.png` with `PngToBmpConverter` into the 1-bit layout `GameCore::checkImageHeader` reads
(a 62-byte header, top-down rows padded to 4 bytes, most significant bit first, bit 1 white) and deletes the `.png`.
`icon.png` becomes a 64 x 64 `icon.bmp`, so it must be square (a non-square icon makes the package invalid); it is
scaled to 64 x 64. The converter sizes the result as `int(side * (64.0f / side))` in single precision, which comes out
at 63, not 64, for 280 of the sides 1 to 2,048 (41, 47, 55, 61, 82, 83, 94, 97, ...); the installer refuses a result
that is not 64 x 64, and `pack_game.py` refuses such a side up front with the sides nearby that work. Every power of two
from 1 to 2,048 works, so 64, 128, and 256 are always safe. Any other `<name>.png` becomes `<name>.bmp` at its own size.
`title.png` and `handoff.png`, the reserved pages (AD-15, as amended 2026-10-02), are converted the same way but may be
at most 480 x 480 and 480 x 800; a larger one makes the package invalid (checked from its header before it is
converted). Both count toward the images budget, as game images do, but no game draws them: `GameCore::imageNameOf`
refuses `icon`, `title`, and `handoff`, so `ch.gfx.image` treats them as unknown names, and the game loader skips their
`.bmp` files without a log line. Only the runtime draws them, on the title screen and the hidden hand-off screen.
A PNG whose header the converter refuses (not a PNG, interlaced, over 2,048 x 3,072, an impossible colour type or bit
depth), a converted file `checkImageHeader` does not accept, or images over the budget make the package invalid. The
converter answers only true or false and ignores failed writes, so its output goes through a wrapper that notes a short
write: a short write, or an output shorter than its own header says, is the card's fault (the file stays in the inbox
and the SD card reason is shown); any other failure, memory running out inside the converter included, makes the package
invalid.

### Install

For each inbox file the installer:

1. lists the members and checks them against the whitelist, reads `manifest.json` (`Manifest::parse`), and applies
   `Manifest::check` with this host's capabilities: `Invalid` makes the package invalid, as does an `icon` that is not in
   the game icon library (`GameIcons::find`; `Manifest::parse` cannot see the library), while `Unavailable` (an `api`
   this firmware cannot run, for one) installs, and the registry lists it with that verdict for the launcher, which
   lists every installed game and gives the reason under one it cannot start;
2. extracts the members, in name order, to `/.games-tmp/<id>/`, converting images as it goes, and hashes them;
3. removes any `/.games/<id>/` (its `.pkg` first, so a removal that stops partway leaves no listed game), renames
   `/.games-tmp/<id>/` to `/.games/<id>/`, and writes `.pkg` last;
4. deletes the inbox file; if the card will not delete it, renames it `<name>.chgame.installed` (replacing an earlier
   one; one that will not go, such as a file the card marks read-only, leaves `.installed.2`, then `.3`, up to `.5`),
   which the inbox scan ignores, so the installed game does not install again on every visit and undo a Remove. An
   invalid package's `.bad` name works the same way. The leftover file is harmless; renaming it back to `.chgame`
   installs it again, and it may be deleted from a computer.

Right after the manifest is read (before anything is written), the installer refuses an install that would make more
than 64 games (`GameRegistry::MAX_GAMES`, the most the registry lists, in directory order): a package whose `id` is not
installed, with 64 games installed already, stays in the inbox with its own reason ("Too many games are installed;
remove one first") and installs once a game is removed. It is not renamed `.bad`, since nothing has judged it invalid:
an invalid package that arrives at the limit says the same until there is room, and then gets its own reason. A package
that replaces a listed `id` is always allowed. A folder counts as a game when the registry lists it: a valid `.pkg` and
a `manifest.json` that parses and names the folder (`GameRegistry::readGame`, the test the registry applies), so a
folder it skips neither refuses an install nor lets a package into it past the limit.
The folders are counted once per visit and the count follows the installs (and is taken again after an install that
failed once its folder had moved, which can leave a valid `.pkg`). A package that waits for room costs a read of its
directory and manifest and writes nothing, and it does not use up the 32 of a visit, so one behind them (an update of an
installed game, say) is still reached.

`/.games-data/<id>/` is never touched, so a reinstall keeps a game's saved data. A package whose `id` is already
installed replaces it whatever the `version`, and of two inbox files with one `id` the last installed wins.

A package that is invalid is renamed `<name>.chgame.bad` (replacing an earlier `.bad` of that name) and its reason is
shown once. A failure that is the card's or the device's (a write, rename, or delete error, out of memory) leaves the
file in the inbox for the next try, and so does an inbox file that will neither delete nor rename to `.installed` after
its game installed, or an invalid one that will not rename to `.bad`: each is reported as an SD card failure, so a stuck
file shows its reason on every visit and is not silent. A package that fails before the last step never changes
`/.games/<id>/`; a card failure inside it (removing the old folder, the rename, or writing `.pkg`) can leave the game
unlisted until the file installs again, which the next visit to Games retries.

`/.games-tmp/` is emptied whenever Games opens and after every run, folder by folder, with one exception. SdFat moves
a folder by making the new entry before it removes the old one, so a power loss in between leaves
`/.games-tmp/<id>` and `/.games/<id>` on one cluster chain, and freeing either frees clusters the other still uses.
Where `/.games/<id>` exists without a `.pkg` (which is that state, but is also what a removal of the old folder that
stopped partway leaves, beside an independent scratch folder), the installer makes an empty `.xlink` file in
`/.games-tmp/<id>` and looks for it in `/.games/<id>`: two folders on one chain share their directory data, so it
shows in both. It removes the file either way. If it shows, or could not be made or removed, the scratch folder is
kept, and an install of that id fails with the SD card reason each visit until a person clears both from a computer;
if it does not show, the folders are independent and the scratch folder goes as usual, so the install proceeds.
A file where `/.games-tmp` belongs is removed. At most 32 inbox files are installed per visit; the rest wait for the
next.

An installed game is `/.games/<id>/` holding `manifest.json`, `main.lua`, each `<name>.lua`, each `<name>.bmp`,
`icon.bmp` when the package had an `icon.png`, and `.pkg`.

Removing a game (the Games launcher's Remove, `GamePackageInstaller::remove`) deletes `/.games/<id>/` in this order:

1. `.removing`, an empty file, is written in the folder. If the card will not take it, nothing else changes and the
   remove reports the SD card reason. The marker is not a member a package can carry, so only a remove writes one. A
   folder that has the marker already (a retry) is not written again.
2. `.pkg` is deleted, so the game is no longer listed. If it will not go, the marker this remove wrote in step 1 is
   taken away again, best effort, so the game stays listed and whole, and the remove reports the SD card reason.
3. Every other entry of the folder is deleted, then `.removing`, then the folder. The marker goes last because FAT
   reuses freed directory slots, so a marker made after the game's files can be listed ahead of some of them, and a
   delete in directory order would take it first and leave an unmarked folder that nothing reclaims. The one exception
   is a file whose name is over 39 bytes (the installer writes none; only a person could put one there): the folder
   then goes to a plain recursive delete, which takes the marker in directory order.

`/.games-data/<id>/` is never touched. A remove that stops after step 1, 2, or during 3 leaves a folder holding
`.removing`; a stop after the marker's own delete leaves an empty folder with no marker, which is not listed, holds one
directory cluster, and goes with a reinstall of that id (harmless, and nothing else reclaims it). The next time Games opens, `installAll` (before it looks at the inbox, and after it empties
`/.games-tmp`) finishes the remove of every `/.games/<id>/` that holds `.removing`, at most 32 folders a visit, the rest
on the next. It uses the same guards as the remove: the folder's name must be an id a manifest could carry, and a folder
with no `.pkg` beside a `/.games-tmp/<id>` goes through the `.xlink` probe below and is kept, with the SD card reason
logged, when the probe shows shared clusters or fails. A folder without `.removing` is never swept, so a folder someone
copied into `/.games/` by hand stays as it was, listed or not. A marker that could not be written or closed is taken
away again, so a remove refused at step 1 leaves the game whole; if that delete fails too, the marker stays and the next
visit finishes the remove. A remove whose step 2 fails with the `.pkg` still there likewise takes away the marker it
wrote in step 1, so the game the launcher says it could not remove stays listed and whole, and the next visit leaves it;
if that delete fails too, the marker stays as above. The marker also stays in three other cases: when it was there
before the remove began (an earlier remove that stopped partway, which the next visit finishes, so a person's retry over
it that fails at step 2 is still finished then); when the `.pkg`'s delete reports failure after its entry went (a failed
sync), which leaves an unlisted folder that only the marker lets the next visit reclaim; and when the card cannot say
whether the `.pkg` is still there (a card fault reads as "not there"), which the remove treats as gone, the safe side
for a folder that may be unlisted. A remove that cannot finish is logged and tried again on the next visit; it is not
shown to the person, and no popup is drawn while it runs. "The next visit" includes one more moment: right after a remove that
succeeds while `/games/` holds a package, the launcher runs `installAll` again (so a package that waited for room takes
the freed place), and that run tries to finish marked folders as a visit does (at most 32, a folder that will not go
logged and kept). A game whose marker stayed after a remove that failed earlier therefore goes then, and the listing
read after the install no longer shows it; no note names it.

### `.pkg` and the package hash

`.pkg` marks a folder as an installed game, and is written last, so a folder without one (an install cut short) is not
a game. It is exactly 20 bytes: `v1\n`, the package hash as 16 lowercase hex digits, and `\n`. A file that is longer,
holds upper-case hex, or names another version is not a valid `.pkg`.

The package hash is the first 8 bytes of a SHA-256 over the package's members sorted by name (bytewise), each as
`name`, one `00` byte, the member's uncompressed length as 4 little-endian bytes, then its uncompressed bytes. It is
computed over the members as packaged (the `.png` files, not the converted `.bmp` files). `src/games/GameHash` is the
one SHA-256 helper in the firmware (mbedTLS on the device, OpenSSL in the simulator and the host tests) and
`scripts/pack_game.py` computes the same value. Both pass one vector: `test/game_core/package_vectors.json`
(`hash_vector`) and `package_vector.chgame`, whose hash is `0530a15766e91bf1`. The registry
(`src/games/GameRegistry`) lists a folder of `/.games/` only when its `.pkg` is valid and its `manifest.json` names the
folder's own id; it has no index. `resume.bin` records the 8 hash bytes to tell a changed package ([resume.bin](#resumebin)).

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

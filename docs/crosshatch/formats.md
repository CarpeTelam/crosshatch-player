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
the next read uses `store.bin.tmp` when `store.bin` is missing; a torn tmp fails the payload check. A write that
finds `store.bin` missing and the tmp present first renames the tmp to `store.bin` (and fails without writing if
that rename fails), so a failed write never truncates or removes the only copy. The trade-off: while that rename
keeps failing, no newer store is written; the older copy stays, and the slot stays dirty, so every flush retries.
While the game
runs, the match writes a changed store at most every 5 s (`GameSaveStore::flushIfDue`); `GameSaveStore::flush`
writes it at once, for round end and the match's `onExit()` (AD-17).

## resume.bin

`/.games-data/<id>/resume.bin` holds the latest snapshot of a solo match, so the match can continue after the device
sleeps (AD-17). `src/games/GameSaveStore` is its only reader and writer, on the loop task only; the VM task never
touches Storage (AD-5). It sits beside `store.bin` and is written and read the same way.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 6 | blob header: magic `CHRS` (`43 48 52 53`), file version 1, codec version 1 |
| 6 | 8 | package hash: the 8 bytes of `.pkg`'s hash (see [`.pkg` and the package hash](#pkg-and-the-package-hash)) |
| 14 | 1 | mode: 0 (solo) |
| 15 | 1 | seats saved, n: 1 |
| 16 | 2 | ver, unsigned 16-bit, little-endian: the low 16 bits of the Session's ver |
| 18 | 1 to 1,400 | the snapshot's codec bytes (at most `Codec::SNAPSHOT_LIMIT`) |

The snapshot runs to the end of the file. For the package hash `0530a15766e91bf1`, a save of ver 7 holding
`{taps = 3}` is `43 48 52 53 01 01 05 30 a1 57 66 e9 1b f1 00 01 07 00 06 00 01 05 04 74 61 70 73 03 06`.

**Usable** is what `GameSaveStore::peek(id, pkgHash)` answers `Valid` for, and it is exactly what loading accepts: the
header checks `ok`, the package hash is the installed package's, the mode is 0 and n is 1, and the snapshot is 1 to
1,400 bytes that pass `Codec::check` as canonical codec. A file longer than 1,418 bytes is refused before it is read.
`peek` reads the whole file into a buffer allocated for the call. It answers one of three things:

- `Valid`: a Continue row is listed for the game (the launcher calls `peek` once per game when it builds its list);
- `None`: no file, or one that was read and is not usable (the header status, `other package`, `not a solo save`,
  `empty snapshot`, `truncated`, `too large`, or the codec's error). One log line names why, at `LOG_ERR`, except a save
  of another package, which is `LOG_INF` because every launcher build asks again. The file stays, so a save of another
  package survives until the game's next match replaces it;
- `Unreadable`: a file is there and could not be checked, because it would not open (`cannot open`) or read
  (`cannot read`) or the buffer could not be allocated. A card or heap fault may pass, so it is not `None`: the launcher
  still lists the Continue row, so that a new match is not the only choice offered over a save that may be good.

The match applies the same checks when it starts with `Start::Resume` (`GameSaveStore::loadResume`). A save that is
`None` then starts a new match, since nothing usable is lost. A save it cannot read (`Unreadable`), or that the VM
refuses although `peek` accepted it, does not: the match shows the error view "The saved match could not be resumed",
`resume.bin` is untouched, and Back returns to the list, where Continue can be tried again or the game's own row starts a
new match on purpose. (`GameVM::setResume` and `Session::restore` refuse only an empty or oversized snapshot, which
`peek` already excludes, so the VM's refusal cannot happen today; the game's own rules run at its first call, below.) Without a
valid `.pkg` there is no hash, and a new match neither reads, writes, nor deletes `resume.bin` (a file there is not known
to be this package's, so entering Over leaves it); a Continue whose `.pkg` will not read stops in the same error view
rather than play new.

A game that fails on the resumed state (a script error at its first `status` or `draw`) does not delete the save either.
The failure cannot tell a game that rejects the state, every time, from a transient fault (a callback that runs out of
heap is a script error too), and deleting on the second would lose a good save. Continue shows the error view again on
each try until a new match, started from the game's own row, replaces the file with its first snapshot.

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
| `<name>.png` | `<name>` is `[a-z0-9_]{1,32}`; a non-interlaced PNG; `icon.png` is the launcher icon, any other is a game image |

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
limits it knows (the `.lua` total is added there in a later entry).

### Images

The installer converts every `.png` with `PngToBmpConverter` into the 1-bit layout `GameCore::checkImageHeader` reads
(a 62-byte header, top-down rows padded to 4 bytes, most significant bit first, bit 1 white) and deletes the `.png`.
`icon.png` becomes a 64 x 64 `icon.bmp`, so it must be square (a non-square icon makes the package invalid); it is
scaled to 64 x 64. The converter sizes the result as `int(side * (64.0f / side))` in single precision, which comes out
at 63, not 64, for 280 of the sides 1 to 2,048 (41, 47, 55, 61, 82, 83, 94, 97, ...); the installer refuses a result
that is not 64 x 64, and `pack_game.py` refuses such a side up front with the sides nearby that work. Every power of two
from 1 to 2,048 works, so 64, 128, and 256 are always safe. Any other `<name>.png` becomes `<name>.bmp` at its own size.
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
that replaces an installed `id` is always allowed. A folder counts as a game when it holds a valid `.pkg` (the first
test the registry applies; a folder whose manifest the registry then skips still counts, so the count can only be high).
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
   remove reports the SD card reason. The marker is not a member a package can carry, so only a remove writes one.
2. `.pkg` is deleted, so the game is no longer listed.
3. The rest of the folder goes, `.removing` with it (SdFat deletes in directory order, so it may go before the last
   files; a stop in that short span is the one that still leaves an unmarked folder).

`/.games-data/<id>/` is never touched. A remove that stops after step 1 or 2 leaves a folder holding `.removing`, and
the next time Games opens, `installAll` (before it looks at the inbox, and after it empties `/.games-tmp`) finishes
the remove of every `/.games/<id>/` that holds `.removing`, at most 32 folders a visit, the rest on the next. It uses
the same guards as the remove: the folder's name must be an id a manifest could carry, and a folder with no `.pkg`
beside a `/.games-tmp/<id>` goes through the `.xlink` probe below and is kept, with the SD card reason logged, when
the probe shows shared clusters or fails. A folder without `.removing` is never swept, so a folder someone copied
into `/.games/` by hand stays as it was, listed or not. This includes a remove that reported the SD card reason after step 1 (the person asked for it, and the game is
finished at the next visit); a marker that could not be written or closed is taken away again, so a remove refused at
step 1 leaves the game whole. A remove that cannot finish is logged and tried again on the next visit; it is not shown
to the person.

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

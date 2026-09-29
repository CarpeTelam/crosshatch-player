#!/usr/bin/env python3
"""
Packs one game folder into a `.cpgame` package (spine AD-15, AD-16) and prints its package hash.

    python3 scripts/pack_game.py <dir> <out_dir>

`<dir>` is `games/<id>/`, whose name must equal the manifest's `id`; the package is written to `<out_dir>/<id>.cpgame`
(the directory is created when the package is valid). On success stdout ends with the 16-hex package hash, which the
release workflow reads (`fork_release.py` `pack_one`). This is the only Python reader of `manifest.json`: it applies
`Manifest::parse`'s rules, `Manifest::check`'s solo and nearby seat rules, R9's icon check, and the API range of
`lib/GameCore/ApiLevel.h`. It refuses what it can see in the folder that the installer would refuse; it does not
decode: a PNG is checked through its IHDR only (a truncated or corrupt image is left to the installer's converter),
a Lua file only for a leading binary-chunk signature, and a manifest is not depth-limited. The `icon` grammar and the
`icon_weight` values are R9's (the spine's AD-15 amendment), stricter than `Manifest.cpp`'s `validIcon` until
epic-install-and-launcher's entry 6 aligns it.

Exit codes: 0 packed. 1 the package is invalid: each problem is printed as `error: ...` on stderr, nothing is
written, and a package an earlier run left at `<out_dir>/<id>.cpgame` is deleted. 2 the packer could not run: `<dir>`
is missing, `ApiLevel.h` or `assets/game-icons/names.txt` (read only when the manifest has an `icon`) is unreadable,
`<out_dir>` cannot be created, or `<out_dir>` is the game folder itself; nothing is written.

The manifest is read the way the device reads it: a UTF-8 BOM is skipped, and a `\\u` escape is refused, because the
device's JSON parser keeps `\\uXXXX` as six literal characters where Python would decode it.

The package is a flat zip of the whitelisted members (`manifest.json`, `main.lua`, `[a-z0-9_]{1,32}.lua`, and
`[a-z0-9_]{1,32}.png`), sorted by name, with fixed timestamps and permissions, deflated at level 9 unless that does
not shrink the member. The same folder gives the same bytes on one toolchain (deflate output can differ between zlib
builds). The package hash is the first 8 bytes of a SHA-256 over the members sorted by name, each as
`name \\0 u32le(length) bytes`, in lowercase hex. Standard library only.
"""

import argparse
import contextlib
import hashlib
import io
import json
import os
import pathlib
import re
import struct
import sys
import zipfile
import zlib

import fork_common
from fork_common import SetupError

ROOT = pathlib.Path(__file__).resolve().parent.parent
NAMES_PATH = 'assets/game-icons/names.txt'

# The limits the installer enforces (R2); test/game_core/package_vectors.json mirrors each with a case at the limit
# and one over.
PACKAGE_BYTES = 256 * 1024
MEMBER_BYTES = 128 * 1024
MAX_MEMBERS = 32
MAX_MEMBER_NAME_CHARS = 32  # the stem of a .lua or .png member: [a-z0-9_]{1,32}
IMAGES_BYTES = 128 * 1024  # GameCore::IMAGES_BYTES
MAX_IMAGES = 32  # GameCore::MAX_IMAGES
IMAGE_HEADER_BYTES = 62  # the converter's 1-bit BMP header (GameCore::IMAGE_HEADER_BYTES)
MAX_IMAGE_WIDTH = 2048  # PngToBmpConverter's safety limits
MAX_IMAGE_HEIGHT = 3072

# Manifest::parse's text caps.
MAX_NAME_BYTES = 64
MAX_VERSION_BYTES = 32
MAX_ICON_BYTES = 32
MAX_MANIFEST_INT = 999_999_999
MODES = ('solo', 'pass', 'nearby')
ICON_WEIGHTS = ('regular', 'fill')
KNOWN_KEYS = ('id', 'name', 'version', 'api', 'seats', 'modes', 'hidden', 'icon', 'icon_weight')
REQUIRED_KEYS = ('id', 'name', 'version', 'api', 'seats', 'modes')

GAME_ID = re.compile(r'[a-z0-9][a-z0-9-]{0,31}')
ICON_NAME = re.compile(r'[a-z][a-z0-9]*(-[a-z0-9]+)*')
LUA_MEMBER = re.compile(rf'[a-z0-9_]{{1,{MAX_MEMBER_NAME_CHARS}}}\.lua')
PNG_MEMBER = re.compile(rf'[a-z0-9_]{{1,{MAX_MEMBER_NAME_CHARS}}}\.png')
# A \u escape: an odd run of backslashes, then u. The device's StreamingJsonParser keeps `\uXXXX` as six literal
# characters where Python decodes it, so the two would read different strings.
UNICODE_ESCAPE = re.compile(r'(?<!\\)(?:\\\\)*\\u')
# The PNG colour types the converter accepts, each with the bit depths the PNG format allows for it.
PNG_DEPTHS = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}
MANIFEST_MEMBER = 'manifest.json'
MAIN_MEMBER = 'main.lua'
ICON_MEMBER = 'icon.png'
LUA_BINARY_CHUNK = b'\x1bLua'
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'

ZIP_DATE = (1980, 1, 1, 0, 0, 0)
ZIP_UNIX = 3
ZIP_ATTR = 0o100644 << 16
DEFLATE_LEVEL = 9


class _Object(dict):
    """A parsed JSON object that remembers the keys it repeated."""

    duplicates = ()


def _object(pairs):
    result = _Object()
    duplicates = []
    for key, value in pairs:
        if key in result:
            duplicates.append(key)
        result[key] = value
    result.duplicates = tuple(duplicates)
    return result


def _reject_constant(name):
    raise ValueError(f'{name} is not JSON')


def is_count(value):
    """Whether value is a JSON integer Manifest::parse accepts: 0 to 999,999,999, not a boolean."""
    return isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= MAX_MANIFEST_INT


def is_text(value, limit):
    """Whether value is a string of at most `limit` UTF-8 bytes (a lone surrogate is not UTF-8)."""
    if not isinstance(value, str):
        return False
    try:
        return len(value.encode('utf-8')) <= limit
    except UnicodeEncodeError:
        return False


def read_manifest(data, dir_name, api_range, load_icons):
    """(manifest, problems) for the bytes of a manifest.json.

    `api_range` is (API_MIN_LEVEL, API_LEVEL); `load_icons()` returns the set of library icon names and is called only
    when the manifest has a well-formed `icon`. manifest is None when the JSON is unusable.
    """
    try:
        manifest = json.loads(
            data.decode('utf-8-sig'),  # the device's parser skips a BOM
            object_pairs_hook=_object,
            parse_constant=_reject_constant,
        )
    except (UnicodeDecodeError, ValueError, RecursionError) as exc:
        return None, [f'{MANIFEST_MEMBER}: not valid JSON ({exc})']
    if not isinstance(manifest, _Object):
        return None, [f'{MANIFEST_MEMBER}: not a JSON object']

    problems = []
    if UNICODE_ESCAPE.search(data.decode('utf-8-sig')):
        problems.append(
            f'{MANIFEST_MEMBER}: has a \\u escape; the device reads it as six literal characters, so write the '
            'character itself as UTF-8'
        )

    def bad(message):
        problems.append(f'{MANIFEST_MEMBER}: {message}')

    for key in manifest.duplicates:
        if key in KNOWN_KEYS:
            bad(f'duplicate key {key!r}')
    for key in REQUIRED_KEYS:
        if key not in manifest:
            bad(f'missing key {key!r}')

    if 'id' in manifest:
        value = manifest['id']
        if not isinstance(value, str) or not GAME_ID.fullmatch(value):
            bad('id must match [a-z0-9][a-z0-9-]{0,31}')
        elif value != dir_name:
            bad(f'id {value!r} is not the folder name {dir_name!r}')
    if 'name' in manifest:
        value = manifest['name']
        if not is_text(value, MAX_NAME_BYTES) or not value:
            bad(f'name must be 1 to {MAX_NAME_BYTES} bytes of UTF-8 text')
    if 'version' in manifest and not is_text(manifest['version'], MAX_VERSION_BYTES):
        bad(f'version must be at most {MAX_VERSION_BYTES} bytes of UTF-8 text')
    if 'api' in manifest:
        value = manifest['api']
        if not is_count(value) or value < 1:
            bad(f'api must be an integer from 1 to {MAX_MANIFEST_INT}')
        elif not api_range[0] <= value <= api_range[1]:
            bad(f'api {value} is outside the levels this host supports, {api_range[0]} to {api_range[1]}')

    seats = manifest.get('seats')
    seat_min = seat_max = None
    if 'seats' in manifest:
        if not isinstance(seats, _Object):
            bad('seats must be an object with min and max')
        else:
            for key in seats.duplicates:
                if key in ('min', 'max'):
                    bad(f'duplicate key seats.{key}')
            for key in ('min', 'max'):
                if key not in seats:
                    bad(f'missing key seats.{key}')
                elif not is_count(seats[key]):
                    bad(f'seats.{key} must be an integer from 0 to {MAX_MANIFEST_INT}')
            seat_min = seats['min'] if is_count(seats.get('min')) else None
            seat_max = seats['max'] if is_count(seats.get('max')) else None
            if seat_min is not None and seat_min < 1:
                bad('seats.min must be at least 1')
            if seat_min is not None and seat_max is not None and seat_max < seat_min:
                bad('seats.max must be at least seats.min')

    modes = manifest.get('modes')
    if 'modes' in manifest:
        if not isinstance(modes, list) or not modes:
            bad('modes must be a non-empty array')
        elif any(not isinstance(mode, str) or mode not in MODES for mode in modes):
            bad(f'modes may hold only {", ".join(MODES)}')
        else:
            if 'solo' in modes and seat_min is not None and seat_min != 1:
                bad('a solo game needs seats.min 1')
            if 'nearby' in modes and seat_max is not None and seat_max < 2:
                bad('a nearby game needs seats.max of 2 or more')

    if 'hidden' in manifest and not isinstance(manifest['hidden'], bool):
        bad('hidden must be true or false')
    if 'icon' in manifest:
        value = manifest['icon']
        if not isinstance(value, str) or not is_text(value, MAX_ICON_BYTES) or not ICON_NAME.fullmatch(value):
            bad(f'icon must be a library icon name, [a-z][a-z0-9]*(-[a-z0-9]+)* of at most {MAX_ICON_BYTES} bytes')
        elif value not in load_icons():
            bad(f'icon {value!r} is not in the game icon library')
    if 'icon_weight' in manifest and manifest['icon_weight'] not in ICON_WEIGHTS:
        bad(f'icon_weight must be {" or ".join(repr(w) for w in ICON_WEIGHTS)}')
    return manifest, problems


def load_icon_names(root=ROOT):
    """The library's icon names: the first column of each line of names.txt that is not blank or a comment."""
    path = pathlib.Path(root) / NAMES_PATH
    try:
        text = path.read_text(encoding='utf-8')
    except (OSError, UnicodeDecodeError) as exc:
        raise SetupError(f'cannot read {NAMES_PATH}: {exc}')
    names = set()
    for line in text.splitlines():
        fields = line.split()
        if fields and not fields[0].startswith('#'):
            names.add(fields[0])
    return names


def png_size(data):
    """(width, height) from a PNG's IHDR; ValueError with the reason when the PNG is one the converter would refuse."""
    if data[:8] != PNG_SIGNATURE:
        raise ValueError('not a PNG (bad signature)')
    if len(data) < 33 or data[8:12] != b'\x00\x00\x00\x0d' or data[12:16] != b'IHDR':
        raise ValueError('malformed PNG (no 13-byte IHDR chunk first)')
    if struct.unpack('>I', data[29:33])[0] != zlib.crc32(data[12:29]):
        raise ValueError('malformed PNG (IHDR CRC mismatch)')
    width, height = struct.unpack('>II', data[16:24])
    depth, color, compression, filter_method, interlace = data[24], data[25], data[26], data[27], data[28]
    if depth not in PNG_DEPTHS.get(color, ()):
        raise ValueError(f'unsupported PNG colour type {color} at {depth} bits')
    if compression != 0 or filter_method != 0:
        raise ValueError('unsupported PNG compression or filter method')
    if interlace != 0:
        raise ValueError('interlaced PNGs are not supported')
    if width == 0 or height == 0 or width > MAX_IMAGE_WIDTH or height > MAX_IMAGE_HEIGHT:
        raise ValueError(f'{width}x{height} is empty or over {MAX_IMAGE_WIDTH}x{MAX_IMAGE_HEIGHT}')
    return width, height


def image_bytes(width, height):
    """The bytes of the .bmp the installer writes for a width x height image: a header and rows padded to 4 bytes."""
    return IMAGE_HEADER_BYTES + -(-width // 32) * 4 * height


def check_images(sizes):
    """Problems (strings) for a list of (width, height) images that count toward the budget (icon.png excluded)."""
    problems = []
    if len(sizes) > MAX_IMAGES:
        problems.append(f'{len(sizes)} images; at most {MAX_IMAGES}')
    total = sum(image_bytes(width, height) for width, height in sizes)
    if total > IMAGES_BYTES:
        problems.append(f'the images convert to {total:,} bytes; at most {IMAGES_BYTES:,}')
    return problems


def package_hash(members):
    """The 16-hex package hash of {name: bytes}: SHA-256 over `name \\0 u32le(len) bytes` by name, first 8 bytes."""
    digest = hashlib.sha256()
    for name in sorted(members):
        data = members[name]
        digest.update(name.encode('utf-8') + b'\x00' + struct.pack('<I', len(data)) + data)
    return digest.digest()[:8].hex()


def deflate(data):
    packer = zlib.compressobj(DEFLATE_LEVEL, zlib.DEFLATED, -15)
    return packer.compress(data) + packer.flush()


def build_package(members):
    """The bytes of a flat zip of {name: bytes}: sorted, fixed timestamps, deflated only where that shrinks a member."""
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as package:
        for name in sorted(members):
            data = members[name]
            info = zipfile.ZipInfo(name, date_time=ZIP_DATE)
            info.create_system = ZIP_UNIX
            info.external_attr = ZIP_ATTR
            method = zipfile.ZIP_DEFLATED if len(deflate(data)) < len(data) else zipfile.ZIP_STORED
            package.writestr(info, data, compress_type=method, compresslevel=DEFLATE_LEVEL)
    return out.getvalue()


def member_problem(name, is_file):
    """Why a directory entry may not be in a package, or None when it may."""
    if not is_file:
        return 'is not a regular file (a package is flat: no subfolders or links)'
    if name.lower().endswith('.bmp'):
        return 'is a .bmp; images are .png only, and the installer converts them'
    if name in (MANIFEST_MEMBER, MAIN_MEMBER) or LUA_MEMBER.fullmatch(name) or PNG_MEMBER.fullmatch(name):
        return None
    return 'is not a package member (manifest.json, main.lua, [a-z0-9_]{1,32}.lua, [a-z0-9_]{1,32}.png)'


def read_members(directory):
    """({name: bytes} of the whitelisted members, problems); an unreadable file is a SetupError."""
    members = {}
    problems = []
    try:
        entries = sorted(os.scandir(directory), key=lambda entry: entry.name)
    except OSError as exc:
        raise SetupError(f'cannot read {directory}: {exc}')
    for entry in entries:
        why = member_problem(entry.name, entry.is_file(follow_symlinks=False))
        if why:
            problems.append(f'{entry.name}: {why}')
            continue
        try:
            with open(entry.path, 'rb') as handle:
                members[entry.name] = handle.read()
        except OSError as exc:
            raise SetupError(f'cannot read {entry.path}: {exc}')
    return members, problems


def check_members(members, dir_name, api_range, load_icons):
    """Every problem with the members of one package: the rules the installer and the game loader enforce."""
    problems = []
    for name in (MANIFEST_MEMBER, MAIN_MEMBER):
        if name not in members:
            problems.append(f'{name}: missing')
    if len(members) > MAX_MEMBERS:
        problems.append(f'{len(members)} members; at most {MAX_MEMBERS}')
    for name, data in members.items():
        if len(data) > MEMBER_BYTES:
            problems.append(f'{name}: {len(data):,} bytes; at most {MEMBER_BYTES:,} per member')
        if name.endswith('.lua') and data.startswith(LUA_BINARY_CHUNK):
            problems.append(f'{name}: is a precompiled Lua chunk; only source is accepted')
    if MANIFEST_MEMBER in members:
        problems += read_manifest(members[MANIFEST_MEMBER], dir_name, api_range, load_icons)[1]
    sizes = []
    for name, data in members.items():
        if not name.endswith('.png'):
            continue
        try:
            size = png_size(data)
        except ValueError as exc:
            problems.append(f'{name}: {exc}')
            continue
        if name != ICON_MEMBER:
            sizes.append(size)
    problems += check_images(sizes)
    return problems


def pack(directory, out_dir):
    """Pack `directory` into `out_dir`; PASS, or FAIL after printing each problem. A SetupError when it cannot run."""
    directory = pathlib.Path(directory)
    if not directory.is_dir():
        raise SetupError(f'{directory} is not a directory')
    header = ROOT / fork_common.API_LEVEL_HEADER
    try:
        level = fork_common.parse_api_level(header.read_text(encoding='utf-8'))
    except (OSError, UnicodeDecodeError) as exc:
        raise SetupError(f'cannot read {fork_common.API_LEVEL_HEADER}: {exc}')
    dir_name = directory.resolve().name
    if pathlib.Path(out_dir).resolve() == directory.resolve():
        raise SetupError(f'{out_dir} is the game folder; a package is never written into it')
    target = pathlib.Path(out_dir) / f'{dir_name}.cpgame'

    members, problems = read_members(directory)
    problems += check_members(members, dir_name, (level.min_level, level.level), load_icon_names)
    package = build_package(members)
    if len(package) > PACKAGE_BYTES:
        problems.append(f'the package is {len(package):,} bytes; at most {PACKAGE_BYTES:,}')
    if problems:
        for problem in problems:
            print(f'error: {problem}', file=sys.stderr)
        # An earlier run's package for this game must not outlive the folder that no longer packs.
        if target.is_file():
            with contextlib.suppress(OSError):
                target.unlink()
        return fork_common.FAIL

    temporary = target.with_name(target.name + '.tmp')
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary.write_bytes(package)
        os.replace(temporary, target)
    except OSError as exc:
        with contextlib.suppress(OSError):
            temporary.unlink(missing_ok=True)
        raise SetupError(f'cannot write {target}: {exc}')
    print(f'packed {target} ({len(package)} bytes)')
    print(package_hash(members))
    return fork_common.PASS


def main(argv=None):
    parser = argparse.ArgumentParser(description='Pack games/<id>/ into <out_dir>/<id>.cpgame and print its hash.')
    parser.add_argument('directory', help='the game folder, games/<id>/')
    parser.add_argument('out_dir', help='the directory the package is written to')
    args = parser.parse_args(argv)
    return fork_common.exit_code(lambda: pack(args.directory, args.out_dir))


if __name__ == '__main__':
    sys.exit(main())

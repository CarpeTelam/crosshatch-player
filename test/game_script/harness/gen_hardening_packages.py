#!/usr/bin/env python3
"""Writes the crafted .cpgame packages the installer's hardening suite (PackageHardeningTest) installs.

Python makes them because the firmware's miniz cannot deflate, and a zip bomb, a deflated member at its
limit, and most of the malformed layouts need real deflate streams or hand-set header fields. Standard
library only. Each case is a file `<name>.cpgame` plus a line `<name>.cpgame <expected>` in `cases.txt`, where
`<expected>` is `Ok` (it installs) or the name of the `GamePackageInstaller::Error` that rejects it; an optional third
field is the most bytes any file under /.games-tmp may ever hold while it is installed. The at-limit
and one-over cases take their numbers from test/game_core/package_vectors.json (the file scripts/pack_game.py's
test reads, and test/game_core/PackageLimitsTest.cpp), so a limit changes in one place.

Usage: python3 gen_hardening_packages.py <output folder>
Exit 0: written. Exit 2: the output folder or the vectors could not be used.
"""

import json
import pathlib
import struct
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
VECTORS = HERE.parent.parent / 'game_core' / 'package_vectors.json'

MANIFEST = json.dumps({
    'id': 'hardening', 'name': 'Hardening', 'version': '1', 'api': 1,
    'seats': {'min': 1, 'max': 1}, 'modes': ['solo'],
}).encode()
MAIN = b'return {}\n'
BYTECODE = b'\x1bLua\x54\x00\x19\x93\r\n\x1a\n'  # the start of a precompiled chunk


def crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def deflate(data):
    packer = zlib.compressobj(9, zlib.DEFLATED, -15)
    return packer.compress(data) + packer.flush()


class Member:
    """A zip member. `declared` overrides the fields the directory states (crc, csize, usize, method, flags)."""

    def __init__(self, name, data, method=8, payload=None, alias_of=None, **declared):
        self.alias_of = alias_of  # a member whose local header and data this one's directory entry shares
        self.name = name if isinstance(name, bytes) else name.encode()
        self.method = method
        self.payload = payload if payload is not None else (deflate(data) if method == 8 else data)
        self.fields = {'crc': crc(data), 'csize': len(self.payload), 'usize': len(data), 'method': method,
                       'flags': 0, 'extra': b''}
        self.fields.update(declared)


def stored(name, data, **declared):
    return Member(name, data, method=0, **declared)


def local_header(member):
    f = member.fields
    return (struct.pack('<IHHHHHIIIHH', 0x04034B50, 20, f['flags'], f['method'], 0, 0x21, f['crc'], f['csize'],
                        f['usize'], len(member.name), len(f['extra'])) + member.name + f['extra'])


def central_header(member, offset):
    f = member.fields
    return (struct.pack('<IHHHHHHIIIHHHHHII', 0x02014B50, 20, 20, f['flags'], f['method'], 0, 0x21, f['crc'],
                        f['csize'], f['usize'], len(member.name), len(f['extra']), 0, 0, 0, 0, offset)
            + member.name + f['extra'])


def zip_of(members, total=None, disk_total=None, before_eocd=b'', comment=b'', eocd_comment_len=None,
           directory_gap=0):
    """A zip of `members` in the order given. `total` and `disk_total` override the EOCD's counts, `before_eocd`
    goes between the central directory and the EOCD, `comment` after it, `directory_gap` moves the EOCD's
    directory offset."""
    body = b''
    directory = b''
    offsets = {}
    for member in members:
        if member.alias_of is not None:
            directory += central_header(member, offsets[id(member.alias_of)])
            continue
        offsets[id(member)] = len(body)
        directory += central_header(member, len(body))
        body += local_header(member) + member.payload
    count = len(members)
    eocd = struct.pack('<IHHHHIIH', 0x06054B50, 0, 0, count if disk_total is None else disk_total,
                       count if total is None else total, len(directory), len(body) + directory_gap,
                       len(comment) if eocd_comment_len is None else eocd_comment_len)
    return body + directory + before_eocd + eocd + comment


def game(*extra, main=MAIN, manifest=MANIFEST, method=8):
    """A sound package's members: manifest, main.lua, and `extra`."""
    return [Member('manifest.json', manifest, method), Member('main.lua', main, method), *extra]


def lua(name, data=b'-- filler\n', method=8):
    return Member(name, data, method)


def png(width, height):
    """An 8-bit greyscale PNG of black and white blocks (small when deflated, whatever its size)."""
    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', crc(body))
    black = b'\x00' * 8
    white = b'\xff' * 8
    rows = []
    for y in range(height):
        pattern = (black + white) if (y // 8) % 2 == 0 else (white + black)
        rows.append(b'\x00' + (pattern * (width // 16 + 1))[:width])
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(b''.join(rows), 9))
            + chunk(b'IEND', b''))


def filled_lua(size):
    """A `size`-byte Lua comment (never a bytecode chunk)."""
    return (b'--' + b'x' * (size - 3) + b'\n') if size >= 3 else b'-' * size


def package_of_size(size, limit):
    """A stored zip of exactly `size` bytes: two stored members share what the framing leaves."""
    def build(first, second):
        return zip_of(game(stored('a.lua', filled_lua(first)), stored('b.lua', filled_lua(second)), method=0))
    room = size - len(build(0, 0))
    first = min(limit, room)
    second = room - first
    assert 0 <= second <= limit, (size, room)
    package = build(first, second)
    assert len(package) == size, (len(package), size)
    return package


def cases(vectors):
    limits = vectors['limits']
    out = []

    def add(name, expected, data, held=None):
        out.append((name, expected, data, held))

    # ---- at the limits, and one over (package_vectors.json) ----
    package = limits['package_bytes']
    member = limits['member_bytes']
    add('at-package-bytes', 'Ok', package_of_size(package['at'], member['limit']))
    add('over-package-bytes', 'PackageTooBig', package_of_size(package['over'], member['limit']))
    add('at-member-bytes', 'Ok', zip_of(game(lua('big.lua', filled_lua(member['at'])))))
    add('over-member-bytes', 'MemberTooBig', zip_of(game(lua('big.lua', filled_lua(member['over'])))))
    add('at-members', 'Ok', zip_of(game(*[lua(f'f{i}.lua') for i in range(limits['members']['at'] - 2)])))
    add('over-members', 'TooManyMembers',
        zip_of(game(*[lua(f'f{i}.lua') for i in range(limits['members']['over'] - 2)])))
    stem = limits['member_name_chars']
    add('at-member-stem', 'Ok', zip_of(game(lua('a' * stem['at'] + '.lua'))))
    add('over-member-stem', 'BadMember', zip_of(game(lua('a' * stem['over'] + '.lua'))))
    images = limits['images_bytes']
    for label, case in (('at', images['at']), ('over', images['over'])):
        pictures = [Member(f'img{i}.png', png(w, h)) for i, (w, h) in enumerate(case['images'])]
        add(f'{label}-images-bytes', 'Ok' if label == 'at' else 'ImagesTooBig', zip_of(game(*pictures)))
    width = limits['image_width']
    height = limits['image_height']
    add('at-image-width', 'Ok', zip_of(game(Member('wide.png', png(width['at'], 1)))))
    add('over-image-width', 'BadImage', zip_of(game(Member('wide.png', png(width['over'], 1)))))
    add('at-image-height', 'Ok', zip_of(game(Member('tall.png', png(1, height['at'])))))
    add('over-image-height', 'BadImage', zip_of(game(Member('tall.png', png(1, height['over'])))))

    # ---- zip bombs ----
    add('zip-bomb-declared', 'MemberTooBig', zip_of(game(lua('bomb.lua', b'0' * (10 * 1024 * 1024)))))
    # The stream is cut off at the declared size: nothing past it may reach the card (held: at most 100 bytes).
    add('zip-bomb-understated', 'BadSize', zip_of(game(Member('bomb.lua', b'0' * (300 * 1024), usize=100))), 100)
    add('zip-bomb-understated-large', 'BadSize',
        zip_of(game(Member('bomb.lua', b'0' * (300 * 1024), usize=5000))), 5000)
    # All its bytes arrive, then the deflate stream ends without its final block: no success.
    text = b'-- a\n' * 20
    unfinished = b'\x00' + struct.pack('<HH', len(text), len(text) ^ 0xFFFF) + text
    add('deflate-unterminated', 'BadSize', zip_of(game(Member('a.lua', text, payload=unfinished))))
    add('deflated-size-overstated', 'BadSize', zip_of(game(Member('a.lua', b'-- a\n' * 20, usize=200))))
    add('stored-size-mismatch', 'BadSize', zip_of(game(stored('a.lua', b'-- hi\n', usize=10))))

    # ---- the .lua members together (GameAssets::load's limit; entry 7 moves it into the vectors) ----
    sources = limits.get('lua_sources_bytes', {'limit': 262144, 'at': 262144, 'over': 262145})
    for label, total in (('at', sources['at']), ('over', sources['over'])):
        rest = total - len(MAIN) - member['limit']
        add(f'{label}-lua-sources', 'Ok' if label == 'at' else 'SourcesTooBig',
            zip_of(game(lua('a.lua', filled_lua(member['limit'])), lua('b.lua', filled_lua(rest)))))
    add('three-lua-over-the-loader', 'SourcesTooBig',
        zip_of(game(*[lua(f'p{i}.lua', filled_lua(100000)) for i in range(3)])))
    # Thirty directory entries on one local header and one 128 KB stream: about 3.9 MB from 2 KB.
    shared = lua('o0.lua', filled_lua(member['limit']))
    add('overlap-members', 'BadDirectory',
        zip_of(game(shared, *[Member(f'o{i}.lua', b'', alias_of=shared, crc=shared.fields['crc'],
                                     csize=shared.fields['csize'], usize=shared.fields['usize'])
                              for i in range(1, 30)])))

    # ---- names ----
    for name, label in ((b'../evil.lua', 'dotdot'), (b'/abs.lua', 'absolute'), (b'sub/x.lua', 'subdir'),
                        (b'a\\b.lua', 'backslash'), (b'dir/', 'folder'), (b'icon.bmp', 'bmp'),
                        (b'Main2.lua', 'uppercase'), (b'..', 'dots'), (b'x' * 300 + b'.lua', 'name-300'),
                        (b'ok\x00.lua', 'nul'), (b'util.lua\x00', 'nul-tail'), (b'.lua', 'no-stem'),
                        (b'manifest.json ', 'trailing-space')):
        add(f'name-{label}', 'BadMember', zip_of(game(lua(name))))
    add('duplicate-member', 'BadMember', zip_of(game(lua('util.lua'), lua('util.lua', b'-- other\n'))))

    # ---- Lua bytecode ----
    add('binary-lua-deflated', 'BinaryLua', zip_of(game(lua('b.lua', BYTECODE))))
    add('binary-lua-stored', 'BinaryLua', zip_of(game(stored('b.lua', BYTECODE), method=0)))
    add('binary-main-lua', 'BinaryLua', zip_of(game(main=BYTECODE)))
    # ESC anywhere but the first byte is ordinary text (a terminal colour code in a string, say).
    add('escape-inside-lua', 'Ok', zip_of(game(lua('ansi.lua', b'-- \x1b[0m\nreturn {}\n'))))
    # ... including the first byte of a later chunk: the installer streams in 1,024-byte chunks.
    later = b'-- ' + b'x' * 1020 + b'\n' + b'\x1b[0m\nreturn {}\n'
    add('escape-at-a-chunk-start-deflated', 'Ok', zip_of(game(lua('ansi.lua', later))))
    add('escape-at-a-chunk-start-stored', 'Ok', zip_of(game(stored('ansi.lua', later), method=0)))

    # ---- zip features this device does not read ----
    sound = game()
    zip64_record = struct.pack('<IQHHIIQQQQ', 0x06064B50, 44, 45, 45, 0, 0, len(sound), len(sound), 0, 0)
    zip64_locator = struct.pack('<IIQI', 0x07064B50, 0, 0, 1)
    add('zip64-records', 'Unsupported', zip_of(sound, before_eocd=zip64_record + zip64_locator))
    add('zip64-sentinels', 'Unsupported', zip_of(sound, total=0xFFFF, disk_total=0xFFFF))
    add('zip64-entry', 'Unsupported',
        zip_of(game(Member('a.lua', b'-- a\n', usize=0xFFFFFFFF, extra=struct.pack('<HHQQ', 1, 16, 5, 5)))))
    for method, label in ((12, 'bzip2'), (9, 'deflate64'), (14, 'lzma'), (1, 'shrunk')):
        add(f'method-{label}', 'Unsupported', zip_of(game(Member('a.lua', b'-- a\n', method=method,
                                                                 payload=b'\x00\x00'))))
    add('encrypted', 'Unsupported', zip_of(game(Member('a.lua', b'-- a\n', flags=1))))

    # ---- the EOCD count against the directory ----
    four = game(lua('a.lua'), lua('b.lua'))
    add('count-below', 'BadDirectory', zip_of(four, total=3, disk_total=3))
    add('count-above', 'BadDirectory', zip_of(four, total=6, disk_total=6))
    add('count-zero', 'BadDirectory', zip_of(four, total=0, disk_total=0))
    add('count-disk-differs', 'BadDirectory', zip_of(four, disk_total=3))
    add('count-only-total-differs', 'BadDirectory', zip_of(four, total=5))
    add('count-above-the-limit', 'BadDirectory', zip_of(four, total=40, disk_total=40))

    # ---- CRCs ----
    add('crc-deflated', 'BadCrc', zip_of(game(Member('a.lua', b'-- a\n', crc=0x12345678))))
    add('crc-stored', 'BadCrc', zip_of(game(stored('a.lua', b'-- a\n', crc=0x12345678), method=0)))
    add('crc-manifest', 'BadCrc', zip_of([Member('manifest.json', MANIFEST, crc=1), Member('main.lua', MAIN)]))
    add('crc-main', 'BadCrc', zip_of([Member('manifest.json', MANIFEST), Member('main.lua', MAIN, crc=1)]))

    # ---- a zip that is not sound ----
    whole = zip_of(game())
    add('no-eocd', 'NotAPackage', whole[:-22] + b'\x00' * 22)
    add('too-short', 'NotAPackage', b'PK\x05\x06')
    add('zip-comment', 'NotAPackage', zip_of(game(), comment=b'a comment'))
    add('directory-gap', 'NotAPackage', zip_of(game(), directory_gap=4))
    add('junk-before-eocd', 'NotAPackage', zip_of(game(), before_eocd=b'junk'))
    add('bad-local-signature', 'NotAPackage', b'XXXX' + whole[4:])
    add('offset-out-of-range', 'NotAPackage', zip_of(game(Member('a.lua', b'-- a\n', csize=50000))))
    return out


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        vectors = json.loads(VECTORS.read_text())
        out = pathlib.Path(argv[1])
        out.mkdir(parents=True, exist_ok=True)
        lines = []
        for name, expected, data, held in cases(vectors):
            (out / f'{name}.cpgame').write_bytes(data)
            lines.append(f'{name}.cpgame {expected}' + (f' {held}' if held is not None else '') + '\n')
        (out / 'cases.txt').write_text(''.join(lines))
    except (OSError, KeyError, ValueError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

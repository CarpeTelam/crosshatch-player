#!/usr/bin/env python3
"""Tests for scripts/pack_game.py. Standard library only. Run: python3 scripts/pack_game_test.py"""

import contextlib
import hashlib
import io
import json
import os
import pathlib
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fork_release  # noqa: E402
import pack_game as pg  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parent.parent
SCRIPTS = REPO / 'scripts'
VECTORS_PATH = REPO / 'test' / 'game_core' / 'package_vectors.json'
VECTORS = json.loads(VECTORS_PATH.read_text(encoding='utf-8'))
LIMITS = VECTORS['limits']
FIXTURES = REPO / 'test' / 'game_script' / 'fixtures'

HASH_LINE = re.compile(r'[0-9a-f]{16}')
ICONS = {'game-controller', 'x', 'dot-outline'}
LEVELS = (2, 3)  # (API_MIN_LEVEL, API_LEVEL) of the temp project


DROP = object()  # a manifest_text value that leaves the key out (None is JSON null)


def manifest_text(**changes):
    manifest = {
        'id': 'demo',
        'name': 'Demo',
        'version': '1.0.0',
        'api': 3,
        'seats': {'min': 1, 'max': 1},
        'modes': ['solo'],
    }
    manifest.update(changes)
    return json.dumps({key: value for key, value in manifest.items() if value is not DROP})


def png(width, height, interlace=0, compression=0, signature=pg.PNG_SIGNATURE, bad_crc=False):
    """A PNG with only what the packer reads: the signature, IHDR, and IEND."""
    body = b'IHDR' + struct.pack('>IIBBBBB', width, height, 1, 0, compression, 0, interlace)
    crc = zlib.crc32(body) ^ (1 if bad_crc else 0)
    return signature + struct.pack('>I', 13) + body + struct.pack('>I', crc) + b'\x00\x00\x00\x00IEND\xaeB`\x82'


def incompressible(size, seed=1):
    """Lua-named bytes deflate cannot shrink, so a package holds them stored and its size is exact."""
    data = random.Random(seed).randbytes(size)
    return b'-' + data[1:] if size else data


def api_header(level, min_level):
    return (f'#pragma once\n#define API_LEVEL {level}\n#define API_MIN_LEVEL {min_level}\n'
            f'#define API_LEVEL_FROZEN false\n#define API_SURFACE_CRC 0x00000000\n')


def r4_hash(members):
    digest = hashlib.sha256()
    for name in sorted(members):
        digest.update(name.encode() + b'\x00' + struct.pack('<I', len(members[name])) + members[name])
    return digest.hexdigest()


class Project:
    """A throwaway project: the packer, fork_common.py, ApiLevel.h, names.txt, and games/."""

    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        (self.root / 'scripts').mkdir()
        for name in ('pack_game.py', 'fork_common.py'):
            shutil.copy(SCRIPTS / name, self.root / 'scripts' / name)
        (self.root / 'lib' / 'GameCore').mkdir(parents=True)
        (self.root / 'lib' / 'GameCore' / 'ApiLevel.h').write_text(api_header(LEVELS[1], LEVELS[0]))
        (self.root / 'assets' / 'game-icons').mkdir(parents=True)
        lines = ['# comment', ''] + [f'{name} {weight} phosphor/{weight}/{name}.svg' for name in sorted(ICONS)
                                     for weight in ('regular', 'fill')]
        (self.root / 'assets' / 'game-icons' / 'names.txt').write_text('\n'.join(lines) + '\n')
        self.out = self.root / 'out'

    def close(self):
        self.tmp.cleanup()

    def game(self, files=None, game_id='demo', manifest=None):
        """Write games/<game_id>/ with `files` ({name: bytes or str}) and a manifest unless one is given."""
        folder = self.root / 'games' / game_id
        shutil.rmtree(folder, ignore_errors=True)
        folder.mkdir(parents=True)
        files = {'main.lua': b'-- main\n', 'manifest.json': manifest_text(id=game_id) if manifest is None else manifest,
                 **(files or {})}
        for name, data in files.items():
            if data is None:
                continue
            (folder / name).write_bytes(data.encode() if isinstance(data, str) else data)
        return folder

    def run(self, folder, out=None):
        result = subprocess.run(
            [sys.executable, str(self.root / 'scripts' / 'pack_game.py'), str(folder), str(out or self.out)],
            cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return result.returncode, result.stdout, result.stderr

    def nothing_written(self):
        return not self.out.exists() or not any(self.out.iterdir())


class PackerTestCase(unittest.TestCase):
    def setUp(self):
        self.project = Project()
        self.addCleanup(self.project.close)

    def assertRefused(self, folder, *fragments, code=1):
        got, out, err = self.project.run(folder)
        self.assertEqual(got, code, err)
        self.assertEqual(out, '')
        self.assertTrue(self.project.nothing_written(), 'a refused package must write nothing')
        for fragment in fragments:
            self.assertIn(fragment, err)
        return err


class PackTest(PackerTestCase):
    def test_packs_a_valid_folder(self):
        folder = self.project.game({'util.lua': b'return {}\n', 'badge.png': png(64, 64), 'icon.png': png(64, 64)})
        code, out, err = self.project.run(folder)
        self.assertEqual((code, err), (0, ''))
        lines = out.splitlines()
        self.assertRegex(lines[-1], HASH_LINE)
        target = self.project.out / 'demo.cpgame'
        self.assertEqual(lines[-2], f'packed {target} ({target.stat().st_size} bytes)')
        with zipfile.ZipFile(target) as package:
            self.assertEqual(package.namelist(), ['badge.png', 'icon.png', 'main.lua', 'manifest.json', 'util.lua'])
            members = {name: package.read(name) for name in package.namelist()}
        self.assertEqual(lines[-1], r4_hash(members)[:16])
        self.assertEqual(members['util.lua'], b'return {}\n')

    def test_package_records_fixed_metadata(self):
        info = zipfile.ZipFile(io.BytesIO(pg.build_package({'main.lua': b'a', 'manifest.json': b'{}'}))).infolist()
        for entry in info:
            self.assertEqual(entry.date_time, (1980, 1, 1, 0, 0, 0))
            self.assertEqual(entry.create_system, 3)
            self.assertEqual(entry.external_attr, 0o100644 << 16)

    def test_deflates_only_what_shrinks(self):
        repeated = b'-- the same text again\n' * 50
        package = zipfile.ZipFile(io.BytesIO(pg.build_package({'a.lua': repeated, 'b.lua': b'x', 'c.lua': b''})))
        methods = {entry.filename: entry.compress_type for entry in package.infolist()}
        self.assertEqual(methods, {'a.lua': zipfile.ZIP_DEFLATED, 'b.lua': zipfile.ZIP_STORED,
                                   'c.lua': zipfile.ZIP_STORED})
        self.assertEqual(package.read('a.lua'), repeated)

    def test_members_are_sorted_whatever_the_order_given(self):
        members = {'z.lua': b'z', 'main.lua': b'm', 'manifest.json': b'{}', 'a.png': b'p'}
        names = zipfile.ZipFile(io.BytesIO(pg.build_package(members))).namelist()
        self.assertEqual(names, sorted(members))

    def test_same_folder_twice_is_byte_identical(self):
        files = {'main.lua': b'-- text\n' * 100, 'util.lua': b'return 1\n', 'b.lua': b'-- b\n', 'a.png': png(8, 8)}
        folder = self.project.game(files)
        self.assertEqual(self.project.run(folder, self.project.root / 'one')[0], 0)
        # Recreate the folder in the opposite write order with other timestamps.
        shutil.rmtree(folder)
        folder.mkdir()
        for name in reversed(list(files) + ['manifest.json']):
            data = manifest_text() if name == 'manifest.json' else files[name]
            (folder / name).write_bytes(data.encode() if isinstance(data, str) else data)
            os.utime(folder / name, (1_000_000_000, 1_000_000_000))
        self.assertEqual(self.project.run(folder, self.project.root / 'two')[0], 0)
        self.assertEqual((self.project.root / 'one' / 'demo.cpgame').read_bytes(),
                         (self.project.root / 'two' / 'demo.cpgame').read_bytes())

    def test_out_dir_is_created(self):
        folder = self.project.game()
        out = self.project.root / 'a' / 'b'
        self.assertEqual(self.project.run(folder, out)[0], 0)
        self.assertTrue((out / 'demo.cpgame').is_file())
        self.assertEqual([p.name for p in out.iterdir()], ['demo.cpgame'])

    def test_folder_name_is_read_after_resolving(self):
        folder = self.project.game()
        code, _, err = self.project.run(folder / '..' / 'demo')
        self.assertEqual((code, err), (0, ''))

    def test_hash_matches_the_r4_definition_for_every_order(self):
        members = {'main.lua': b'm', 'manifest.json': b'{}', 'a.lua': b'a'}
        expected = hashlib.sha256(
            b'a.lua\x00\x01\x00\x00\x00a' + b'main.lua\x00\x01\x00\x00\x00m' + b'manifest.json\x00\x02\x00\x00\x00{}'
        ).hexdigest()[:16]
        self.assertEqual(pg.package_hash(members), expected)
        self.assertEqual(pg.package_hash(dict(reversed(list(members.items())))), expected)


class BadPackageTest(PackerTestCase):
    def test_files_that_are_not_members(self):
        cases = {
            'a .bmp': ({'badge.bmp': b'BM'}, 'badge.bmp: is a .bmp'),
            'an upper case .BMP': ({'BADGE.BMP': b'BM'}, 'BADGE.BMP: is a .bmp'),
            'another file': ({'notes.txt': b'hi'}, 'notes.txt: is not a package member'),
            'a hidden file': ({'.DS_Store': b'x'}, '.DS_Store: is not a package member'),
            'an upper case name': ({'Util.lua': b'x'}, 'Util.lua: is not a package member'),
            'a hyphenated name': ({'my-util.lua': b'x'}, 'my-util.lua: is not a package member'),
            'a name of 33 characters': ({'a' * 33 + '.lua': b'x'}, 'is not a package member'),
            'a second manifest': ({'manifest.JSON': b'{}'}, 'manifest.JSON: is not a package member'),
        }
        for label, (files, fragment) in cases.items():
            with self.subTest(label):
                self.assertRefused(self.project.game(files), fragment)

    def test_subfolder_and_link(self):
        folder = self.project.game()
        (folder / 'sub').mkdir()
        (folder / 'sub' / 'x.lua').write_bytes(b'x')
        self.assertRefused(folder, 'sub: is not a regular file')
        shutil.rmtree(folder / 'sub')
        os.symlink(folder / 'main.lua', folder / 'link.lua')
        self.assertRefused(folder, 'link.lua: is not a regular file')

    def test_missing_members(self):
        self.assertRefused(self.project.game({'main.lua': None}), 'main.lua: missing')
        self.assertRefused(self.project.game(manifest='{}', files={'manifest.json': None}), 'manifest.json: missing')

    def test_binary_lua_chunk(self):
        self.assertRefused(self.project.game({'util.lua': b'\x1bLuaS\x00'}), 'util.lua: is a precompiled Lua chunk')
        self.assertRefused(self.project.game({'main.lua': b'\x1bLuaS\x00'}), 'main.lua: is a precompiled Lua chunk')

    def test_bad_pngs(self):
        cases = {
            'interlaced': (png(8, 8, interlace=1), 'interlaced'),
            'bad signature': (png(8, 8, signature=b'\x89PNX\r\n\x1a\n'), 'bad signature'),
            'bad crc': (png(8, 8, bad_crc=True), 'CRC mismatch'),
            'truncated': (png(8, 8)[:20], 'malformed'),
            'empty': (b'', 'bad signature'),
            'unknown compression': (png(8, 8, compression=1), 'compression'),
            'zero width': (png(0, 8), 'empty or over'),
            'zero height': (png(8, 0), 'empty or over'),
            'too wide': (png(pg.MAX_IMAGE_WIDTH + 1, 1), 'empty or over'),
            'too tall': (png(1, pg.MAX_IMAGE_HEIGHT + 1), 'empty or over'),
        }
        for label, (data, fragment) in cases.items():
            for name in ('badge.png', 'icon.png'):
                with self.subTest(label, name=name):
                    self.assertRefused(self.project.game({name: data}), f'{name}: ', fragment)

    def test_every_problem_is_reported(self):
        folder = self.project.game({'a.bmp': b'x', 'b.txt': b'x', 'c.png': b'x', 'main.lua': None},
                                   manifest=manifest_text(api=9))
        err = self.assertRefused(folder)
        self.assertEqual(len(err.strip().splitlines()), 5, err)
        self.assertTrue(all(line.startswith('error: ') for line in err.strip().splitlines()))

    def test_api_range(self):
        for api, ok in ((1, False), (2, True), (3, True), (4, False)):
            with self.subTest(api=api):
                folder = self.project.game(manifest=manifest_text(api=api))
                if ok:
                    self.assertEqual(self.project.run(folder)[0], 0)
                    shutil.rmtree(self.project.out)
                else:
                    self.assertRefused(folder, 'outside the levels this host supports, 2 to 3')

    def test_manifest_rules_refuse_the_package(self):
        cases = {
            'solo with two seats': manifest_text(seats={'min': 2, 'max': 2}),
            'nearby with one seat': manifest_text(modes=['nearby'], seats={'min': 1, 'max': 1}),
            'id is not the folder': manifest_text(id='other'),
            'unknown icon': manifest_text(icon='no-such-icon'),
            'bad icon weight': manifest_text(icon='x', icon_weight='bold'),
            'not JSON': '{',
        }
        for label, text in cases.items():
            with self.subTest(label):
                self.assertRefused(self.project.game(manifest=text), 'manifest.json: ')


class LimitTest(PackerTestCase):
    def package_files(self, total_payload):
        """Two incompressible Lua members holding `total_payload` bytes between them."""
        first = min(total_payload, pg.MEMBER_BYTES)
        return {'main.lua': incompressible(first, 1), 'b.lua': incompressible(total_payload - first, 2)}

    def package_overhead(self):
        members = {'manifest.json': manifest_text().encode(), **self.package_files(1000)}
        return len(pg.build_package(members)) - 1000

    def test_package_bytes(self):
        payload = LIMITS['package_bytes']['at'] - self.package_overhead()
        code, _, err = self.project.run(self.project.game(self.package_files(payload)))
        self.assertEqual((code, err), (0, ''))
        self.assertEqual((self.project.out / 'demo.cpgame').stat().st_size, LIMITS['package_bytes']['at'])
        shutil.rmtree(self.project.out)
        payload = LIMITS['package_bytes']['over'] - self.package_overhead()
        self.assertRefused(self.project.game(self.package_files(payload)), '262,145 bytes; at most 262,144')

    def test_member_bytes(self):
        at, over = LIMITS['member_bytes']['at'], LIMITS['member_bytes']['over']
        self.assertEqual(self.project.run(self.project.game({'main.lua': incompressible(at)}))[0], 0)
        shutil.rmtree(self.project.out)
        self.assertRefused(self.project.game({'main.lua': incompressible(over)}), 'main.lua: 131,073 bytes')
        self.assertRefused(self.project.game({'util.lua': b'x' * over}), 'util.lua: 131,073 bytes')
        self.assertRefused(self.project.game(manifest=manifest_text(version='x' * over)), 'bytes; at most 131,072 per member')

    def member_files(self, count):
        return {f'm{i}.lua': b'-- m\n' for i in range(count - 2)}  # main.lua and manifest.json make up the rest

    def test_member_count(self):
        at, over = LIMITS['members']['at'], LIMITS['members']['over']
        self.assertEqual(self.project.run(self.project.game(self.member_files(at)))[0], 0)
        with zipfile.ZipFile(self.project.out / 'demo.cpgame') as package:
            self.assertEqual(len(package.namelist()), 32)
        shutil.rmtree(self.project.out)
        self.assertRefused(self.project.game(self.member_files(over)), '33 members; at most 32')

    def images_files(self, sizes):
        return {f'i{i}.png': png(width, height) for i, (width, height) in enumerate(sizes)}

    def test_images_bytes_from_png_dimensions(self):
        case = LIMITS['images_bytes']
        for label in ('at', 'over'):
            sizes = [tuple(size) for size in case[label]['images']]
            self.assertEqual(sum(pg.image_bytes(*size) for size in sizes), case[label]['bytes'])
        at = self.project.game(self.images_files(case['at']['images']))
        self.assertEqual(self.project.run(at)[0], 0)
        shutil.rmtree(self.project.out)
        over = self.project.game(self.images_files(case['over']['images']))
        self.assertRefused(over, 'the images convert to 131,074 bytes; at most 131,072')

    def test_icon_png_is_not_in_the_image_budget(self):
        files = self.images_files(LIMITS['images_bytes']['at']['images'])
        files['icon.png'] = png(64, 64)
        self.assertEqual(self.project.run(self.project.game(files))[0], 0)

    def test_image_dimensions(self):
        for key, at_png, over_png in (
            ('image_width', png(LIMITS['image_width']['at'], 1), png(LIMITS['image_width']['over'], 1)),
            ('image_height', png(1, LIMITS['image_height']['at']), png(1, LIMITS['image_height']['over'])),
        ):
            with self.subTest(key):
                self.assertEqual(self.project.run(self.project.game({'icon.png': at_png}))[0], 0)
                shutil.rmtree(self.project.out)
                self.assertRefused(self.project.game({'icon.png': over_png}), 'empty or over 2048x3072')

    def test_images_count_at_function_level(self):
        # The member cap binds first (a package holds 30 images at most), so the count is tested on check_images.
        at, over = LIMITS['images']['at'], LIMITS['images']['over']
        self.assertEqual(pg.check_images([(1, 1)] * at), [])
        self.assertEqual(len(pg.check_images([(1, 1)] * over)), 1)
        self.assertIn('33 images; at most 32', pg.check_images([(1, 1)] * over)[0])
        self.assertEqual(self.project.run(self.project.game(self.images_files([(1, 1)] * 30)))[0], 0)

    def test_vector_limits_equal_the_packer_constants(self):
        self.assertEqual(LIMITS['package_bytes']['limit'], pg.PACKAGE_BYTES)
        self.assertEqual(LIMITS['member_bytes']['limit'], pg.MEMBER_BYTES)
        self.assertEqual(LIMITS['members']['limit'], pg.MAX_MEMBERS)
        self.assertEqual(LIMITS['images']['limit'], pg.MAX_IMAGES)
        self.assertEqual(LIMITS['images_bytes']['limit'], pg.IMAGES_BYTES)
        self.assertEqual(LIMITS['image_width']['limit'], pg.MAX_IMAGE_WIDTH)
        self.assertEqual(LIMITS['image_height']['limit'], pg.MAX_IMAGE_HEIGHT)
        formula = VECTORS['image_formula']
        self.assertEqual(formula['header_bytes'], pg.IMAGE_HEADER_BYTES)
        self.assertEqual((pg.PACKAGE_BYTES, pg.MEMBER_BYTES, pg.IMAGES_BYTES), (262144, 131072, 131072))
        for key, case in LIMITS.items():
            with self.subTest(key):
                if isinstance(case['at'], int):
                    self.assertEqual((case['at'], case['over']), (case['limit'], case['limit'] + 1))
                else:
                    self.assertLessEqual(case['at']['bytes'], case['limit'])
                    self.assertGreater(case['over']['bytes'], case['limit'])
        self.assertEqual(formula['icon_excluded'], pg.ICON_MEMBER)


class CannotRunTest(PackerTestCase):
    def test_missing_folder(self):
        self.assertRefused(self.project.root / 'games' / 'nothing', 'is not a directory', code=2)

    def test_folder_is_a_file(self):
        (self.project.root / 'games').mkdir()
        (self.project.root / 'games' / 'demo').write_text('x')
        self.assertRefused(self.project.root / 'games' / 'demo', 'is not a directory', code=2)

    def test_unreadable_api_level_header(self):
        folder = self.project.game()
        (self.project.root / 'lib' / 'GameCore' / 'ApiLevel.h').unlink()
        self.assertRefused(folder, 'cannot read lib/GameCore/ApiLevel.h', code=2)

    def test_malformed_api_level_header(self):
        folder = self.project.game()
        (self.project.root / 'lib' / 'GameCore' / 'ApiLevel.h').write_text('#define API_LEVEL 1\n')
        self.assertRefused(folder, 'API_MIN_LEVEL', code=2)

    def test_names_are_read_only_for_a_manifest_with_an_icon(self):
        (self.project.root / 'assets' / 'game-icons' / 'names.txt').unlink()
        self.assertEqual(self.project.run(self.project.game())[0], 0)
        shutil.rmtree(self.project.out)
        self.assertRefused(self.project.game(manifest=manifest_text(icon='x')), 'cannot read assets/game-icons/names.txt',
                           code=2)

    def test_out_dir_that_cannot_be_created(self):
        folder = self.project.game()
        blocker = self.project.root / 'blocker'
        blocker.write_text('a file')
        got, out, err = self.project.run(folder, blocker / 'out')
        self.assertEqual((got, out), (2, ''), err)
        self.assertIn('cannot write', err)
        self.assertEqual(blocker.read_text(), 'a file')

    def test_an_invalid_package_writes_no_out_dir(self):
        folder = self.project.game({'notes.txt': b'x'})
        self.assertEqual(self.project.run(folder)[0], 1)
        self.assertFalse(self.project.out.exists())

    def test_nothing_is_left_behind_by_a_write_that_fails(self):
        folder = self.project.game()
        self.project.out.mkdir()
        (self.project.out / 'demo.cpgame').mkdir()  # os.replace onto a directory fails
        got, _, _ = self.project.run(folder)
        self.assertEqual(got, 2)
        self.assertEqual([p.name for p in self.project.out.iterdir()], ['demo.cpgame'])


class ReadManifestTest(unittest.TestCase):
    def read(self, text, dir_name='demo', levels=LEVELS, icons=ICONS):
        data = text if isinstance(text, bytes) else text.encode('utf-8', 'surrogatepass')
        return pg.read_manifest(data, dir_name, levels, lambda: icons)[1]

    def refused(self, text, fragment=None, **kwargs):
        problems = self.read(text, **kwargs)
        self.assertTrue(problems, f'{text!r} should be refused')
        if fragment:
            self.assertTrue(any(fragment in problem for problem in problems), problems)

    def test_valid_manifests(self):
        self.assertEqual(self.read(manifest_text()), [])
        full = manifest_text(hidden=True, icon='game-controller', icon_weight='fill', modes=['solo', 'pass'],
                             extra={'a': [1, {'b': None}]})
        self.assertEqual(self.read(full), [])
        self.assertEqual(self.read(manifest_text(seats={'min': 2, 'max': 4}, modes=['pass', 'nearby'])), [])
        self.assertEqual(self.read(manifest_text(icon_weight='regular')), [])
        self.assertEqual(self.read('{"id":"demo","name":"D","version":"","api":2,"seats":{"min":1,"max":1,"x":1},'
                                   '"modes":["solo","solo"]}'), [])

    def test_returns_the_parsed_manifest(self):
        manifest, problems = pg.read_manifest(manifest_text().encode(), 'demo', LEVELS, lambda: ICONS)
        self.assertEqual((manifest['id'], manifest['api'], problems), ('demo', 3, []))
        self.assertIsNone(pg.read_manifest(b'[]', 'demo', LEVELS, lambda: ICONS)[0])

    def test_not_an_object(self):
        for text in ('', '{', '[]', '"x"', '1', 'null', '{"id": "demo"} x', '{"id": NaN}', '{"api": Infinity}'):
            with self.subTest(text=text):
                self.refused(text)
        self.refused(b'\xff\xfe{}', 'not valid JSON')
        self.refused(b'\xef\xbb\xbf' + manifest_text().encode(), 'not valid JSON')

    def test_missing_keys(self):
        for key in pg.REQUIRED_KEYS:
            manifest = json.loads(manifest_text())
            del manifest[key]
            with self.subTest(key=key):
                self.refused(json.dumps(manifest), f'missing key {key!r}')

    def test_duplicate_known_keys(self):
        text = manifest_text(hidden=False)
        for key in ('"id": "demo"', '"name": "Demo"', '"api": 3', '"modes": ["solo"]', '"hidden": false'):
            with self.subTest(key=key):
                self.refused(text[:-1] + ', ' + key + '}', 'duplicate key')
        self.refused(text.replace('"min": 1', '"min": 1, "min": 1'), 'duplicate key seats.min')
        self.refused(text.replace('"max": 1', '"max": 1, "max": 1'), 'duplicate key seats.max')

    def test_duplicate_unknown_keys_are_ignored(self):
        text = manifest_text()
        self.assertEqual(self.read(text[:-1] + ', "x": 1, "x": {"y": 1, "y": 2}}'), [])
        self.assertEqual(self.read(text.replace('"min": 1', '"z": 1, "z": 2, "min": 1')), [])

    def test_id(self):
        for bad in ('', 'Demo', '-demo', 'de mo', 'd' * 33, 'démo', 1, None, ['demo']):
            with self.subTest(bad=bad):
                self.refused(manifest_text(id=bad), 'id ')
        self.refused(manifest_text(id='other'), 'not the folder name')
        self.assertEqual(self.read(manifest_text(id='a-b-9'), dir_name='a-b-9'), [])
        self.assertEqual(self.read(manifest_text(id='d' * 32), dir_name='d' * 32), [])

    def test_name_and_version_are_counted_in_bytes(self):
        self.assertEqual(self.read(manifest_text(name='n' * 64)), [])
        self.assertEqual(self.read(manifest_text(name='é' * 32)), [])
        self.refused(manifest_text(name='n' * 65), 'name ')
        self.refused(manifest_text(name='é' * 33), 'name ')
        self.refused(manifest_text(name=''), 'name ')
        self.refused(manifest_text(name=5), 'name ')
        self.refused(manifest_text(name='\ud800'), 'name ')
        self.assertEqual(self.read(manifest_text(version='v' * 32)), [])
        self.assertEqual(self.read(manifest_text(version='')), [])
        self.refused(manifest_text(version='v' * 33), 'version ')
        self.refused(manifest_text(version=1), 'version ')

    def test_api(self):
        for bad in (0, -1, 1.0, 2.5, '3', True, None, [], 10**9, 10**12):
            with self.subTest(bad=bad):
                self.refused(manifest_text(api=bad), 'api ')
        self.refused('{"id":"demo","name":"D","version":"","api":03,"seats":{"min":1,"max":1},"modes":["solo"]}')
        self.refused('{"id":"demo","name":"D","version":"","api":1e0,"seats":{"min":1,"max":1},"modes":["solo"]}')
        self.assertEqual(self.read(manifest_text(api=3)), [])
        self.assertEqual(self.read(manifest_text(api=pg.MAX_MANIFEST_INT), levels=(1, pg.MAX_MANIFEST_INT)), [])

    def test_api_range(self):
        self.refused(manifest_text(api=1), 'outside the levels', levels=(2, 3))
        self.refused(manifest_text(api=4), 'outside the levels', levels=(2, 3))
        self.assertEqual(self.read(manifest_text(api=1), levels=(1, 1)), [])

    def test_seats(self):
        for seats in (None, [], 1, {}, {'min': 1}, {'max': 1}, {'min': 0, 'max': 1}, {'min': 2, 'max': 1},
                      {'min': 1.5, 'max': 2}, {'min': '1', 'max': 1}, {'min': 1, 'max': None}, {'min': True, 'max': 1},
                      {'min': 1, 'max': 10**9}, {'min': -1, 'max': 1}):
            with self.subTest(seats=seats):
                self.refused(manifest_text(seats=seats, modes=['pass']), 'seats')
        self.assertEqual(self.read(manifest_text(seats={'min': 1, 'max': 10**9 - 1}, modes=['pass'])), [])
        self.assertEqual(self.read(manifest_text(seats={'min': 3, 'max': 3}, modes=['pass'])), [])

    def test_modes(self):
        for modes in (None, [], {}, 'solo', ['duo'], ['Solo'], [1], [None], ['solo', 'duo'], [['solo']]):
            with self.subTest(modes=modes):
                self.refused(manifest_text(modes=modes), 'modes ')
        for modes in (['solo'], ['pass'], ['nearby'], ['solo', 'pass', 'nearby']):
            with self.subTest(modes=modes):
                self.assertEqual(self.read(manifest_text(modes=modes, seats={'min': 1, 'max': 2})), [])

    def test_solo_and_nearby_seat_rules(self):
        self.refused(manifest_text(modes=['solo', 'pass'], seats={'min': 2, 'max': 2}), 'solo game needs seats.min 1')
        self.refused(manifest_text(modes=['nearby'], seats={'min': 1, 'max': 1}), 'nearby game needs seats.max')
        self.refused(manifest_text(modes=['solo', 'nearby'], seats={'min': 1, 'max': 1}), 'nearby game needs seats.max')
        self.assertEqual(self.read(manifest_text(modes=['pass'], seats={'min': 2, 'max': 2})), [])
        self.assertEqual(self.read(manifest_text(modes=['nearby'], seats={'min': 2, 'max': 2})), [])

    def test_hidden(self):
        for bad in (0, 1, 'true', None, []):
            with self.subTest(bad=bad):
                self.refused(manifest_text(hidden=bad), 'hidden ')
        self.assertEqual(self.read(manifest_text(hidden=False)), [])

    def test_icon_grammar(self):
        for good in ('x', 'a1', 'game-controller', 'a-1', 'a1-b2-c3', 'x' * 32):
            with self.subTest(good=good):
                self.assertEqual(self.read(manifest_text(icon=good), icons={good}), [])
        for bad in ('', 'Foo', 'a_b', 'a--b', '-a', 'a-', '1a', 'a b', 'a.b', 'x' * 33, 'é', 1, None, ['x']):
            with self.subTest(bad=bad):
                self.refused(manifest_text(icon=bad), 'icon must be', icons={bad} if isinstance(bad, str) else set())

    def test_icon_must_be_in_the_library(self):
        self.refused(manifest_text(icon='no-such-icon'), "icon 'no-such-icon' is not in the game icon library")
        self.assertEqual(self.read(manifest_text(icon='dot-outline')), [])

    def test_the_library_is_not_loaded_without_an_icon(self):
        def fail():
            raise AssertionError('names.txt was read')

        self.assertEqual(pg.read_manifest(manifest_text().encode(), 'demo', LEVELS, fail)[1], [])
        pg.read_manifest(manifest_text(icon='Bad').encode(), 'demo', LEVELS, fail)

    def test_icon_weight(self):
        for bad in ('bold', 'Regular', '', 'thin', 1, None, True, ['fill']):
            with self.subTest(bad=bad):
                self.refused(manifest_text(icon_weight=bad), 'icon_weight ')
        for good in ('regular', 'fill'):
            self.assertEqual(self.read(manifest_text(icon_weight=good)), [])

    def test_unknown_keys_are_ignored(self):
        self.assertEqual(self.read(manifest_text(color='red', tags=['a', 1, None], nested={'a': {'b': [1]}})), [])


class PngSizeTest(unittest.TestCase):
    def test_reads_the_ihdr(self):
        self.assertEqual(pg.png_size(png(64, 32)), (64, 32))
        self.assertEqual(pg.png_size(png(2048, 3072)), (2048, 3072))
        self.assertEqual(pg.png_size(png(1, 1)), (1, 1))

    def test_refuses_what_the_converter_refuses(self):
        for data in (png(1, 1, interlace=1), png(0, 1), png(1, 0), png(2049, 1), png(1, 3073), png(1, 1, compression=2),
                     png(1, 1, bad_crc=True), png(1, 1, signature=bytes(8)), png(1, 1)[:32], b'', b'\x89PNG'):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                pg.png_size(data)

    def test_ihdr_must_be_first_and_13_bytes(self):
        good = png(4, 4)
        body = b'IHDR' + good[16:29]
        wrong_length = pg.PNG_SIGNATURE + struct.pack('>I', 12) + body + good[29:33]
        wrong_type = pg.PNG_SIGNATURE + good[8:12] + b'IDAT' + good[16:]
        for data in (wrong_length, wrong_type):
            with self.assertRaises(ValueError):
                pg.png_size(data)

    def test_image_bytes(self):
        self.assertEqual(pg.image_bytes(1, 1), 66)
        self.assertEqual(pg.image_bytes(32, 1), 66)
        self.assertEqual(pg.image_bytes(33, 1), 70)
        self.assertEqual(pg.image_bytes(64, 64), 62 + 8 * 64)


class VectorTest(PackerTestCase):
    def setUp(self):
        super().setUp()
        self.vector = VECTORS['hash_vector']
        self.members = {name: text.encode() for name, text in self.vector['members'].items()}
        self.package_path = VECTORS_PATH.parent / self.vector['package']

    def test_the_vector_hash_is_the_r4_hash(self):
        self.assertEqual(r4_hash(self.members), self.vector['sha256'])
        self.assertEqual(self.vector['package_hash'], self.vector['sha256'][:16])
        self.assertRegex(self.vector['package_hash'], HASH_LINE)
        self.assertEqual(pg.package_hash(self.members), self.vector['package_hash'])

    def test_the_committed_package_holds_the_members(self):
        data = self.package_path.read_bytes()
        self.assertEqual(len(data), self.vector['package_bytes'])
        with zipfile.ZipFile(io.BytesIO(data)) as package:
            self.assertEqual(package.namelist(), sorted(self.members))
            self.assertEqual({name: package.read(name) for name in package.namelist()}, self.members)
            methods = {entry.filename: entry.compress_type for entry in package.infolist()}
        self.assertEqual(methods['util.lua'], zipfile.ZIP_STORED)
        self.assertEqual(methods['main.lua'], zipfile.ZIP_DEFLATED)

    def test_the_committed_package_hashes_to_the_vector(self):
        with zipfile.ZipFile(self.package_path) as package:
            members = {name: package.read(name) for name in package.namelist()}
        self.assertEqual(r4_hash(members), self.vector['sha256'])

    def test_the_packer_packs_the_vector_members(self):
        (self.project.root / 'lib' / 'GameCore' / 'ApiLevel.h').write_text(api_header(1, 1))
        folder = self.project.game(game_id=self.vector['id'],
                                   files={name: data for name, data in self.members.items()},
                                   manifest=self.vector['members']['manifest.json'])
        code, out, err = self.project.run(folder)
        self.assertEqual((code, err), (0, ''))
        self.assertEqual(out.splitlines()[-1], self.vector['package_hash'])
        packed = (self.project.out / f'{self.vector["id"]}.cpgame').read_bytes()
        # Content, methods, and hash, never the compressed bytes: deflate output can differ between zlib builds.
        with zipfile.ZipFile(io.BytesIO(packed)) as package:
            self.assertEqual({name: package.read(name) for name in package.namelist()}, self.members)
            methods = {entry.filename: entry.compress_type for entry in package.infolist()}
        self.assertEqual(methods['util.lua'], zipfile.ZIP_STORED)
        self.assertEqual(methods['main.lua'], zipfile.ZIP_DEFLATED)


class ReleaseAndFixtureTest(PackerTestCase):
    def test_fork_release_pack_one_runs_the_real_packer(self):
        self.project.game()
        out = self.project.root / 'staging'
        out.mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            package, package_hash = fork_release.pack_one(
                self.project.root, self.project.root / 'scripts' / 'pack_game.py', 'demo', out)
        self.assertEqual(package, out / 'demo.cpgame')
        self.assertRegex(package_hash, HASH_LINE)
        with zipfile.ZipFile(package) as archive:
            self.assertEqual(package_hash, r4_hash({n: archive.read(n) for n in archive.namelist()})[:16])

    def test_fork_release_pack_one_fails_on_an_invalid_package(self):
        self.project.game({'notes.txt': b'x'})
        out = self.project.root / 'staging'
        out.mkdir()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(fork_release.Failure):
                fork_release.pack_one(self.project.root, self.project.root / 'scripts' / 'pack_game.py', 'demo', out)

    def run_real(self, fixture, out):
        result = subprocess.run([sys.executable, str(SCRIPTS / 'pack_game.py'), str(FIXTURES / fixture), str(out)],
                                cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return result.returncode, result.stdout, result.stderr

    def test_the_counter_fixture_packs(self):
        code, out, err = self.run_real('counter', self.project.out)
        self.assertEqual((code, err), (0, ''))
        self.assertRegex(out.splitlines()[-1], HASH_LINE)
        with zipfile.ZipFile(self.project.out / 'counter.cpgame') as package:
            self.assertEqual(package.namelist(), ['main.lua', 'manifest.json'])

    def test_the_images_fixture_is_refused_for_its_bmp_files(self):
        code, out, err = self.run_real('images', self.project.out)
        self.assertEqual((code, out), (1, ''))
        for name in ('badge.bmp', 'dot.bmp', 'icon.bmp'):
            self.assertIn(f'{name}: is a .bmp', err)
        self.assertFalse(self.project.out.exists())

    def test_real_names_file_holds_the_icons_the_fixtures_use(self):
        names = pg.load_icon_names()
        self.assertIn('game-controller', names)
        self.assertNotIn('#', names)

    def test_load_icon_names_reads_the_first_column(self):
        names = pg.load_icon_names(self.project.root)
        self.assertEqual(names, ICONS)
        with self.assertRaises(pg.SetupError):
            pg.load_icon_names(self.project.root / 'nowhere')


if __name__ == '__main__':
    unittest.main()

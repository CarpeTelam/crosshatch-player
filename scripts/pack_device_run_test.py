#!/usr/bin/env python3
"""Tests for scripts/pack_device_run.py. Standard library only. Run: python3 scripts/pack_device_run_test.py"""

import contextlib
import hashlib
import io
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fork_common  # noqa: E402
import pack_device_run as pdr  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parent.parent
SCRIPT = REPO / 'scripts' / 'pack_device_run.py'
HASH_LINE = re.compile(r'(\S+\.chgame) ([0-9a-f]{16}|\(invalid\))')
FILE_ORDER = [
    'counter.chgame',
    'loop.chgame',
    'timing.chgame',
    'pack-images.chgame',
    'counter-changed.chgame',
    'pass-open.chgame',
    'invalid-binary-lua.chgame',
    'package_vector.chgame',
]

# A stand-in for scripts/pack_game.py: writes <out>/<folder name>.chgame and prints a hash of the folder's main.lua.
# FAKE_MODE (a file next to it) picks a failure for the folder named in it: "<folder> <exit code or 'nohash' or 'nofile'>".
FAKE_PACKER = '''\
import hashlib, pathlib, sys
folder, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
mode = pathlib.Path(__file__).with_name('fake_mode.txt')
if mode.is_file():
    name, what = mode.read_text().split()
    if name == folder.name and what.isdigit():
        print('error: refused', file=sys.stderr)
        sys.exit(int(what))
out.mkdir(parents=True, exist_ok=True)
if not (mode.is_file() and mode.read_text().split() == [folder.name, 'nofile']):
    (out / (folder.name + '.chgame')).write_bytes(b'PK' + (folder / 'main.lua').read_bytes())
print('packed', folder.name)
if mode.is_file() and mode.read_text().split() == [folder.name, 'nohash']:
    print('done')
else:
    print(hashlib.sha256((folder / 'main.lua').read_bytes()).hexdigest()[:16])
'''
FAKE_GENERATOR = '''\
import pathlib, sys
out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
(out / 'other.chgame').write_bytes(b'other')
(out / 'binary-lua-stored.chgame').write_bytes(b'BAD')
(out / 'cases.txt').write_text('other.chgame Ok\\nbinary-lua-stored.chgame BinaryLua\\n')
'''
VECTOR_BYTES = b'vector-package'
VECTOR_HASH = '0123456789abcdef'
VECTOR_FILE = 'the_vector.chgame'  # not the committed name: the script must read it from the JSON


def fake_hash(text):
    return hashlib.sha256(text.encode()).hexdigest()[:16]


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(content, bytes):
        path.write_bytes(content)
    else:
        path.write_text(content, encoding='utf-8')


def make_root(base):
    """A scratch repository with the files the script reads, backed by fakes for the packer and the generator."""
    root = pathlib.Path(base) / 'repo'
    write(root / pdr.PACKER, FAKE_PACKER)
    write(root / pdr.GENERATOR, FAKE_GENERATOR)
    for folder, _ in pdr.GAMES:
        write(root / pdr.FIXTURES / folder / 'main.lua', f'-- {folder}\n')
    write(root / pdr.VECTORS.parent / VECTOR_FILE, VECTOR_BYTES)
    vectors = {'hash_vector': {'package_hash': VECTOR_HASH, 'package_bytes': len(VECTOR_BYTES), 'package': VECTOR_FILE}}
    write(root / pdr.VECTORS, json.dumps(vectors))
    return root


def call(root, out):
    """(exit code, stdout, stderr) of pack_device_run(root, out) under the exit contract."""
    stdout, stderr = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        code = fork_common.exit_code(lambda: pdr.pack_device_run(root, out))
    return code, stdout.getvalue(), stderr.getvalue()


def hashes_of(out):
    lines = (pathlib.Path(out) / 'HASHES.txt').read_text(encoding='utf-8').splitlines()
    return [HASH_LINE.fullmatch(line).groups() for line in lines]


class FakeTreeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = make_root(self.tmp.name)
        self.out = pathlib.Path(self.tmp.name) / 'out'

    def fake_mode(self, text):
        write(self.root / 'scripts' / 'fake_mode.txt', text)

    def test_writes_every_package_and_lists_each_hash_in_order(self):
        code, stdout, _ = call(self.root, self.out)
        self.assertEqual(code, fork_common.PASS)
        expected = [
            ('counter.chgame', fake_hash('-- counter\n')),
            ('loop.chgame', fake_hash('-- loop\n')),
            ('timing.chgame', fake_hash('-- timing\n')),
            ('pack-images.chgame', fake_hash('-- pack-images\n')),
            ('counter-changed.chgame', fake_hash('-- changed/counter\n')),
            ('pass-open.chgame', fake_hash('-- pass-open\n')),
            ('invalid-binary-lua.chgame', '(invalid)'),
            (VECTOR_FILE, VECTOR_HASH),
        ]
        self.assertEqual(hashes_of(self.out), expected)
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), sorted(FILE_ORDER[:-1] + [VECTOR_FILE, 'HASHES.txt']))
        self.assertEqual((self.out / 'counter-changed.chgame').read_bytes(), b'PK-- changed/counter\n')
        self.assertEqual((self.out / 'invalid-binary-lua.chgame').read_bytes(), b'BAD')
        self.assertEqual((self.out / VECTOR_FILE).read_bytes(), VECTOR_BYTES)
        self.assertIn('counter-changed.chgame ' + fake_hash('-- changed/counter\n'), stdout)

    def test_an_existing_out_dir_is_reused_and_the_files_replaced(self):
        write(self.out / 'counter.chgame', b'old')
        self.assertEqual(call(self.root, self.out)[0], fork_common.PASS)
        self.assertEqual((self.out / 'counter.chgame').read_bytes(), b'PK-- counter\n')

    def test_a_game_the_packer_refuses_is_exit_1_and_writes_nothing(self):
        self.fake_mode('loop 1')
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.FAIL)
        self.assertIn('loop failed to pack', stderr)
        self.assertIn('refused', stderr)
        self.assertFalse((self.out / 'HASHES.txt').exists())

    def test_a_failed_run_removes_the_hashes_an_earlier_run_left(self):
        write(self.out / 'HASHES.txt', 'counter.chgame 0123456789abcdef\n')
        self.fake_mode('loop 1')
        self.assertEqual(call(self.root, self.out)[0], fork_common.FAIL)
        self.assertFalse((self.out / 'HASHES.txt').exists())

    def test_a_vectors_file_whose_hash_vector_is_not_an_object_is_exit_2(self):
        write(self.root / pdr.VECTORS, json.dumps({'hash_vector': 'x'}))
        self.assertEqual(call(self.root, self.out)[0], fork_common.COULD_NOT_RUN)

    def test_a_packer_that_could_not_run_is_exit_2(self):
        self.fake_mode('timing 2')
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.COULD_NOT_RUN)
        self.assertIn('could not pack timing', stderr)

    def test_a_packer_without_a_hash_line_is_exit_1(self):
        self.fake_mode('counter nohash')
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.FAIL)
        self.assertIn("'done' is not a package hash", stderr)
        self.assertFalse((self.out / 'HASHES.txt').exists())

    def test_a_packer_that_wrote_no_package_is_exit_1(self):
        self.fake_mode('pack-images nofile')
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.FAIL)
        self.assertIn('wrote no pack-images.chgame', stderr)

    def test_a_missing_packer_is_exit_2(self):
        (self.root / pdr.PACKER).unlink()
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.COULD_NOT_RUN)
        self.assertIn('pack_game.py is missing', stderr)

    def test_a_missing_fixture_folder_is_exit_2(self):
        (self.root / pdr.FIXTURES / 'changed' / 'counter' / 'main.lua').unlink()
        (self.root / pdr.FIXTURES / 'changed' / 'counter').rmdir()
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.COULD_NOT_RUN)
        self.assertIn('is not a directory', stderr)

    def test_a_generator_that_fails_is_exit_2(self):
        write(self.root / pdr.GENERATOR, 'import sys\nprint("boom", file=sys.stderr)\nsys.exit(2)\n')
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.COULD_NOT_RUN)
        self.assertIn('boom', stderr)

    def test_a_generator_without_the_case_is_exit_2(self):
        write(self.root / pdr.GENERATOR, 'import pathlib, sys\npathlib.Path(sys.argv[1]).mkdir(parents=True)\n')
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.COULD_NOT_RUN)
        self.assertIn('wrote no binary-lua-stored.chgame', stderr)

    def test_a_case_that_no_longer_expects_binary_lua_is_exit_1(self):
        gen = FAKE_GENERATOR.replace('binary-lua-stored.chgame BinaryLua', 'binary-lua-stored.chgame NotAPackage')
        write(self.root / pdr.GENERATOR, gen)
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.FAIL)
        self.assertIn('cases.txt has no line "binary-lua-stored.chgame BinaryLua"', stderr)
        self.assertFalse((self.out / 'HASHES.txt').exists())

    def test_a_generator_without_cases_txt_is_exit_2(self):
        write(self.root / pdr.GENERATOR, FAKE_GENERATOR.replace("(out / 'cases.txt')", "(out / 'cases.txt.x')"))
        self.assertEqual(call(self.root, self.out)[0], fork_common.COULD_NOT_RUN)

    def test_the_vector_file_is_the_one_the_json_names_and_a_path_is_refused(self):
        vectors = {'hash_vector': {'package_hash': VECTOR_HASH, 'package_bytes': 1, 'package': '../repo/x.chgame'}}
        write(self.root / pdr.VECTORS, json.dumps(vectors))
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.COULD_NOT_RUN)
        self.assertIn('is not a file name', stderr)
        self.assertEqual(call(make_root(self.tmp.name + '/two'), self.out)[0], fork_common.PASS)

    def test_a_missing_generator_is_exit_2(self):
        (self.root / pdr.GENERATOR).unlink()
        self.assertEqual(call(self.root, self.out)[0], fork_common.COULD_NOT_RUN)

    def test_a_vector_package_of_another_size_is_exit_1(self):
        write(self.root / pdr.VECTORS.parent / VECTOR_FILE, VECTOR_BYTES + b'x')
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.FAIL)
        self.assertIn('the_vector.chgame is 15 bytes', stderr)
        self.assertFalse((self.out / 'HASHES.txt').exists())

    def test_a_missing_vector_package_or_vectors_file_is_exit_2(self):
        (self.root / pdr.VECTORS.parent / VECTOR_FILE).unlink()
        self.assertEqual(call(self.root, self.out)[0], fork_common.COULD_NOT_RUN)
        write(self.root / pdr.VECTORS.parent / VECTOR_FILE, VECTOR_BYTES)
        (self.root / pdr.VECTORS).unlink()
        self.assertEqual(call(self.root, self.out)[0], fork_common.COULD_NOT_RUN)

    def test_a_vector_hash_that_is_not_a_hash_is_exit_2(self):
        vectors = {'hash_vector': {'package_hash': 'XYZ', 'package_bytes': len(VECTOR_BYTES), 'package': VECTOR_FILE}}
        write(self.root / pdr.VECTORS, json.dumps(vectors))
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.COULD_NOT_RUN)
        self.assertIn("package_hash 'XYZ'", stderr)

    def test_an_out_dir_that_is_a_file_is_exit_2(self):
        write(self.out, 'not a folder')
        code, _, stderr = call(self.root, self.out)
        self.assertEqual(code, fork_common.COULD_NOT_RUN)
        self.assertIn('cannot write', stderr)


class RealTreeTest(unittest.TestCase):
    """The committed fixtures, the real packer, the real generator, and the committed vector."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = pathlib.Path(cls.tmp.name) / 'game-packages'
        cls.result = subprocess.run([sys.executable, str(SCRIPT), str(cls.out)], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_command_line_exits_0_and_prints_the_hashes(self):
        self.assertEqual(self.result.returncode, 0, self.result.stderr)
        self.assertTrue(self.result.stdout.endswith((self.out / 'HASHES.txt').read_text(encoding='utf-8')))

    def test_hashes_lists_the_eight_files_in_order(self):
        rows = hashes_of(self.out)
        self.assertEqual([name for name, _ in rows], FILE_ORDER)
        for name, package_hash in rows:
            if name != 'invalid-binary-lua.chgame':
                self.assertRegex(package_hash, r'[0-9a-f]{16}', name)
        self.assertEqual(dict(rows)['invalid-binary-lua.chgame'], '(invalid)')
        self.assertEqual(len(set(package_hash for _, package_hash in rows)), 8)

    def test_the_vector_is_the_committed_file_with_its_recorded_hash(self):
        vectors = json.loads((REPO / pdr.VECTORS).read_text(encoding='utf-8'))['hash_vector']
        self.assertEqual(dict(hashes_of(self.out))['package_vector.chgame'], vectors['package_hash'])
        self.assertEqual((self.out / 'package_vector.chgame').read_bytes(), (REPO / pdr.VECTORS.parent / 'package_vector.chgame').read_bytes())

    def test_the_changed_counter_is_counter_at_1_0_1_titled_counter_v2(self):
        def members(name):
            with zipfile.ZipFile(self.out / name) as package:
                return {member: package.read(member) for member in package.namelist()}

        counter, changed = members('counter.chgame'), members('counter-changed.chgame')
        self.assertEqual(json.loads(counter['manifest.json'])['id'], 'counter')
        self.assertEqual(json.loads(changed['manifest.json'])['id'], 'counter')
        self.assertEqual(json.loads(changed['manifest.json'])['version'], '1.0.1')
        self.assertIn(b'ch.gfx.text(40, 50, "Counter v2", "large", "black")', changed['main.lua'])
        self.assertIn(b'ch.gfx.text(40, 50, "Counter", "large", "black")', counter['main.lua'])
        # The two scripts differ by the title alone, so the variant cannot drift from the counter it changes.
        self.assertEqual(counter['main.lua'].replace(b'"Counter", "large"', b'"Counter v2", "large"'), changed['main.lua'])
        self.assertEqual(json.loads(counter['manifest.json']), {**json.loads(changed['manifest.json']), 'version': '1.0.0'})

    def test_the_invalid_package_holds_compiled_lua(self):
        with zipfile.ZipFile(self.out / 'invalid-binary-lua.chgame') as package:
            lua = [package.read(member) for member in package.namelist() if member.endswith('.lua')]
        self.assertTrue(any(source.startswith(b'\x1bLua') for source in lua))


if __name__ == '__main__':
    unittest.main()

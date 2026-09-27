#!/usr/bin/env python3
"""
Tests for game_codec.py. Standard library only:

    python3 scripts/game_codec_test.py [-v]

The golden vectors in test/game_script/codec_vectors.json are the same file the C host test (CodecTest.cpp) runs, so
passing here and there means both codecs write and accept the same bytes. The rest covers what only Python can
express and the exit code of every command outcome.
"""

import contextlib
import io
import json
import math
import os
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import game_codec as gc  # noqa: E402

# StreamingJsonParser drops a string of 512 bytes or more, so CodecTest.cpp would never see it.
C_READER_TOKEN_LIMIT = 512


def run_main(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = gc.main(list(argv))
    return code, out.getvalue(), err.getvalue()


def load_default():
    return gc.load_vectors(gc.DEFAULT_VECTORS)


class GoldenVectorTest(unittest.TestCase):
    def test_every_vector_passes(self):
        self.assertEqual(gc.run_vectors(load_default()), [])

    def test_every_string_fits_the_c_reader(self):
        def walk(node):
            if isinstance(node, str):
                yield node
            elif isinstance(node, dict):
                for key, value in node.items():
                    yield key
                    yield from walk(value)
            elif isinstance(node, list):
                for item in node:
                    yield from walk(item)

        for text in walk(load_default()):
            self.assertLess(len(text.encode('utf-8')), C_READER_TOKEN_LIMIT, text[:60])

    def test_every_error_the_c_codec_can_raise_has_a_vector(self):
        data = load_default()
        encode_errors = {r['error'] for r in data['encode_errors']}
        decode_errors = {r['error'] for r in data['decode_errors']}
        self.assertLessEqual({'cycle', 'too_deep', 'metatable', 'bad_type', 'bad_key', 'float_key', 'too_large'},
                             encode_errors)
        self.assertLessEqual({'truncated', 'bad_tag', 'trailing', 'non_canonical', 'overflow', 'bad_key', 'too_deep',
                              'too_large'}, decode_errors)

    def test_each_limit_has_cases_at_the_limit_and_one_over(self):
        data = load_default()
        for name, limit in gc.LIMITS.items():
            with self.subTest(limit=name):
                def with_limit(section):
                    return [r for r in data[section] if r.get('limit', 'store') == name]

                at = [r for r in with_limit('encode') if len(gc.parse_hex(r['hex'])) == limit]
                self.assertTrue(at, 'no encode vector of exactly the limit')
                over = [r for r in with_limit('encode_errors') if r['error'] == 'too_large'
                        and len(gc.encode(gc.parse_value(r['value']), limit=1 << 20)) == limit + 1]
                self.assertTrue(over, 'no encode_errors vector of one byte over')
                decode_over = [r for r in with_limit('decode_errors') if r['error'] == 'too_large'
                               and len(gc.parse_hex(r['hex'])) == limit + 1]
                self.assertTrue(decode_over, 'no decode_errors vector of one byte over')

    def test_decoded_values_print_in_a_notation_that_reads_back(self):
        for record in load_default()['encode']:
            with self.subTest(record['name']):
                limit = gc.LIMITS[record.get('limit', 'store')]
                data = gc.parse_hex(record['hex'])
                printed = gc.format_value(gc.decode(data, limit))
                self.assertEqual(gc.encode(gc.parse_value(printed), limit), data, printed)


class PythonOnlyTest(unittest.TestCase):
    def assert_error(self, name, value, limit=gc.STORE_LIMIT):
        with self.assertRaises(gc.CodecError) as caught:
            gc.encode(value, limit)
        self.assertEqual(caught.exception.name, name)

    def test_nan_key(self):
        self.assert_error('nan_key', {math.nan: 1})

    def test_key_types(self):
        self.assert_error('bad_key', {True: 1})
        self.assert_error('bad_key', {(1, 2): 1})
        self.assert_error('bad_key', {'a': 1, b'a': 2})  # the same Lua key twice
        self.assert_error('float_key', {0.5: 1})
        self.assert_error('overflow', {1 << 63: 1})

    def test_value_types(self):
        self.assert_error('overflow', 1 << 63)
        self.assert_error('overflow', -(1 << 63) - 1)
        self.assert_error('bad_type', object())
        self.assert_error('bad_type', {1, 2})

    def test_python_containers(self):
        self.assertEqual(gc.encode([1, None, 3]), gc.parse_hex('06 01 03 02 01 03 06 03 06'))
        self.assertEqual(gc.encode((True,)), gc.parse_hex('06 01 02 00'))
        self.assertEqual(gc.encode({'a': None}), gc.parse_hex('06 00 00'))
        self.assertEqual(gc.encode({2.0: 'x'}), gc.encode({2: b'x'}))
        self.assertEqual(gc.encode('é'), gc.parse_hex('05 02 c3 a9'))

    def test_metatable_and_cycle(self):
        table = gc.Table()
        table.metatable = gc.Table()
        self.assert_error('metatable', {'t': table})
        looped = []
        looped.append(looped)
        self.assert_error('cycle', looped)

    def test_decode_returns_plain_types(self):
        self.assertEqual(gc.decode(gc.encode({'a': [1.5, 'b']})), {b'a': {1: 1.5, 2: b'b'}})

    def test_same_is_strict(self):
        self.assertFalse(gc.same(1, 1.0))
        self.assertFalse(gc.same(True, 1))
        self.assertFalse(gc.same(0.0, -0.0))
        self.assertTrue(gc.same(math.nan, math.nan))
        self.assertTrue(gc.same({2.0: b'x'}, {2: b'x'}))
        self.assertFalse(gc.same({1: b'x'}, {1: b'x', 2: b'y'}))

    def test_notation(self):
        self.assertEqual(gc.parse_value("{1, n = 'a\\x41\\65\\'', [-2] = -1.5}"), {1: 1, b'n': b"aAA'", -2: -1.5})
        self.assertEqual(gc.parse_value('-math.mininteger'), gc.INT_MIN)  # wraps, as in Lua
        self.assertIsInstance(gc.parse_value('9223372036854775808'), float)
        self.assertTrue(math.isinf(gc.parse_value('1/0')))
        for bad in ('{', '{1 2}', 'unknown()', "'\\q'", 'nest(1)', '-true', "'\\300'", '[1]', '1 1'):
            with self.subTest(bad), self.assertRaises(ValueError):
                gc.parse_value(bad)

    def test_hex_notation(self):
        self.assertEqual(gc.parse_hex('06 00*2 0102*2'), bytes([6, 0, 0, 1, 2, 1, 2]))
        self.assertEqual(gc.format_hex(bytes([5, 0, 0, 0, 0, 7, 7])), '05 00*4 07 07')
        for bad in ('0', 'zz', '00*x', '00*'):
            with self.subTest(bad), self.assertRaises(ValueError):
                gc.parse_hex(bad)

    def test_blob_header_fields(self):
        self.assertEqual(gc.blob_header('ABCD', 3), b'ABCD\x03\x01')
        with self.assertRaises(ValueError):
            gc.blob_header('ABC', 1)
        with self.assertRaises(ValueError):
            gc.blob_header('ABCD', 256)


class CommandLineTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def write_vectors(self, data):
        path = os.path.join(self.tmp.name, 'vectors.json')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(data if isinstance(data, str) else json.dumps(data))
        return path

    def test_check_passes(self):
        code, out, _ = run_main('check')
        self.assertEqual(code, 0)
        self.assertIn('passes', out)

    def test_check_fails_on_a_wrong_vector(self):
        data = load_default()
        data['encode'][0]['hex'] = '01'
        code, out, _ = run_main('check', self.write_vectors(data))
        self.assertEqual(code, 1)
        self.assertIn(data['encode'][0]['name'], out)

    def test_check_fails_on_a_wrong_constant(self):
        data = load_default()
        data['move_limit'] = 257
        self.assertEqual(run_main('check', self.write_vectors(data))[0], 1)

    def test_check_cannot_run(self):
        self.assertEqual(run_main('check', os.path.join(self.tmp.name, 'missing.json'))[0], 2)
        self.assertEqual(run_main('check', self.write_vectors('{'))[0], 2)
        data = load_default()
        data['encode'][0]['value'] = '{'
        self.assertEqual(run_main('check', self.write_vectors(data))[0], 2)
        del data['headers']
        self.assertEqual(run_main('check', self.write_vectors(data))[0], 2)

    def test_encode(self):
        self.assertEqual(run_main('encode', '{a = 1}')[:2], (0, '06 00 01 05 01 61 03 02\n'))
        self.assertEqual(run_main('encode', '--limit', 'move', "string.rep('a', 254)")[0], 1)
        self.assertEqual(run_main('encode', '{')[0], 2)

    def test_decode(self):
        self.assertEqual(run_main('decode', '06 01 03 02 01 05 01 62 03 04')[:2], (0, "{1, ['b'] = 2}\n"))
        self.assertEqual(run_main('decode', 'ff')[0], 1)
        self.assertEqual(run_main('decode', 'zz')[0], 2)


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""
Reference codec v1 for game state, moves, and ch.store (AD-10), the Python twin of lib/GameScript/Codec.cpp.
docs/crosshatch/formats.md records the bytes; both codecs pass test/game_script/codec_vectors.json.

    python3 scripts/game_codec.py check [VECTORS]          run every golden vector through this codec
    python3 scripts/game_codec.py encode VALUE [--limit L]  print the bytes of a value written in the vector notation
    python3 scripts/game_codec.py decode HEX [--limit L]    print the value the bytes hold, in the vector notation

L is snapshot (1,400 B), move (256 B), or store (4,096 B, the default). Exit codes: 0 passed (check) or done
(encode, decode); 1 a vector fails (check) or the codec refuses the value or bytes (encode, decode); 2 the vector file
is missing or malformed, or VALUE or HEX cannot be read.

Values in Python: None, bool, int (int64), float, bytes (str is encoded as UTF-8), and tables as dict, list, or
Table. decode() returns strings as bytes and tables as dict with int or bytes keys. Standard library only.
"""

import argparse
import json
import math
import pathlib
import re
import struct
import sys

import fork_common
from fork_common import Failure, SetupError

VERSION = 1
SNAPSHOT_LIMIT = 1400
MOVE_LIMIT = 256
STORE_LIMIT = 4 * 1024
MAX_DEPTH = 16
LIMITS = {'snapshot': SNAPSHOT_LIMIT, 'move': MOVE_LIMIT, 'store': STORE_LIMIT}

BLOB_MAGIC_BYTES = 4
BLOB_HEADER_BYTES = BLOB_MAGIC_BYTES + 2

TAG_NIL, TAG_FALSE, TAG_TRUE, TAG_INT, TAG_FLOAT, TAG_STRING, TAG_TABLE = range(7)
TAG_COUNT = 7
CANONICAL_NAN = 0x7FF8000000000000
MAX_VARINT_BYTES = 10
MIN_PAIR_BYTES = 3
INT_MIN = -(1 << 63)
INT_MAX = (1 << 63) - 1
MASK64 = (1 << 64) - 1

DEFAULT_VECTORS = pathlib.Path(__file__).resolve().parent.parent / 'test' / 'game_script' / 'codec_vectors.json'


class CodecError(Exception):
    """The codec refused a value or bytes; `name` is the error's name in the vector file."""

    def __init__(self, name):
        super().__init__(name)
        self.name = name


class Table(dict):
    """A Lua table: a dict that may carry a metatable and, like a Lua table, is hashable by identity."""

    __hash__ = object.__hash__

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.metatable = None


# ---------------------------------------------------------------------------------------------------------------
# Encoding


def _zigzag(value):
    return ((value << 1) ^ (value >> 63)) & MASK64


def _unzigzag(value):
    return (value >> 1) ^ -(value & 1)


def _put_varint(out, value):
    while True:
        low = value & 0x7F
        value >>= 7
        if value:
            out.append(low | 0x80)
        else:
            out.append(low)
            return


def _as_bytes(text):
    return text.encode('utf-8') if isinstance(text, str) else bytes(text)


def normalize_key(key):
    """A table key as the codec writes it: an int or bytes. Integral floats inside int64 become ints (AD-10)."""
    if isinstance(key, bool):
        raise CodecError('bad_key')
    if isinstance(key, int):
        if not INT_MIN <= key <= INT_MAX:
            raise CodecError('overflow')
        return key
    if isinstance(key, float):
        if math.isnan(key):
            raise CodecError('nan_key')
        if not (-2.0**63 <= key < 2.0**63) or not key.is_integer():
            raise CodecError('float_key')
        return int(key)
    if isinstance(key, (str, bytes, bytearray)):
        return _as_bytes(key)
    raise CodecError('bad_key')


def _key_order(key):
    """Integers ascending, then strings bytewise."""
    return (1, 0, key) if isinstance(key, bytes) else (0, key, b'')


def _entries(table):
    """A table's entries by normalized key; nil values are absent, as in Lua."""
    items = enumerate(table, 1) if isinstance(table, (list, tuple)) else table.items()
    entries = {}
    for key, value in items:
        if value is None:
            continue
        normalized = normalize_key(key)
        if normalized in entries:
            raise CodecError('bad_key')  # e.g. 'a' and b'a', or 2 and 2.0 from a Table built by hand
        entries[normalized] = value
    return entries


def _encode_table(out, table, path):
    # The same checks, in the same order, as Encoder::table in Codec.cpp.
    if any(open_table is table for open_table in path):
        raise CodecError('cycle')
    if len(path) == MAX_DEPTH:
        raise CodecError('too_deep')
    if getattr(table, 'metatable', None) is not None:
        raise CodecError('metatable')
    entries = _entries(table)
    narr = 0
    while narr + 1 in entries:
        narr += 1
    inner = path + [table]
    out.append(TAG_TABLE)
    _put_varint(out, narr)
    for index in range(1, narr + 1):
        _encode_value(out, entries[index], inner)
    records = sorted((key for key in entries if isinstance(key, bytes) or not 1 <= key <= narr), key=_key_order)
    _put_varint(out, len(records))
    for key in records:
        if isinstance(key, bytes):
            out.append(TAG_STRING)
            _put_varint(out, len(key))
            out += key
        else:
            out.append(TAG_INT)
            _put_varint(out, _zigzag(key))
        _encode_value(out, entries[key], inner)


def _encode_value(out, value, path):
    if value is None:
        out.append(TAG_NIL)
    elif isinstance(value, bool):
        out.append(TAG_TRUE if value else TAG_FALSE)
    elif isinstance(value, int):
        if not INT_MIN <= value <= INT_MAX:
            raise CodecError('overflow')
        out.append(TAG_INT)
        _put_varint(out, _zigzag(value))
    elif isinstance(value, float):
        out.append(TAG_FLOAT)
        out += struct.pack('<Q', CANONICAL_NAN) if math.isnan(value) else struct.pack('<d', value)
    elif isinstance(value, (str, bytes, bytearray)):
        data = _as_bytes(value)
        out.append(TAG_STRING)
        _put_varint(out, len(data))
        out += data
    elif isinstance(value, (dict, list, tuple)):
        _encode_table(out, value, path)
    else:
        raise CodecError('bad_type')


def encode(value, limit=STORE_LIMIT):
    """The canonical codec v1 bytes of value; CodecError when it breaks a rule or the output passes limit."""
    out = bytearray()
    _encode_value(out, value, [])
    if len(out) > limit:
        raise CodecError('too_large')
    return bytes(out)


# ---------------------------------------------------------------------------------------------------------------
# Decoding


class _Reader:
    """Reads strictly in order, as Decoder in Codec.cpp does, so both report the same first fault."""

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def remaining(self):
        return len(self.data) - self.pos

    def byte(self):
        if self.pos == len(self.data):
            raise CodecError('truncated')
        self.pos += 1
        return self.data[self.pos - 1]

    def varint(self):
        value = 0
        for i in range(MAX_VARINT_BYTES):
            b = self.byte()
            if i == MAX_VARINT_BYTES - 1 and b > 1:
                raise CodecError('overflow')
            value |= (b & 0x7F) << (7 * i)
            if not b & 0x80:
                if i > 0 and b == 0:
                    raise CodecError('non_canonical')  # overlong
                return value
        raise CodecError('overflow')  # not reached

    def string(self):
        length = self.varint()
        if length > self.remaining():
            raise CodecError('truncated')
        self.pos += length
        return self.data[self.pos - length:self.pos]

    def value(self, depth):
        tag = self.byte()
        if tag >= TAG_COUNT:
            raise CodecError('bad_tag')
        if tag == TAG_NIL:
            return None
        if tag in (TAG_FALSE, TAG_TRUE):
            return tag == TAG_TRUE
        if tag == TAG_INT:
            return _unzigzag(self.varint())
        if tag == TAG_FLOAT:
            if self.remaining() < 8:
                raise CodecError('truncated')
            raw = self.data[self.pos:self.pos + 8]
            self.pos += 8
            number = struct.unpack('<d', raw)[0]
            if math.isnan(number) and struct.unpack('<Q', raw)[0] != CANONICAL_NAN:
                raise CodecError('non_canonical')
            return number
        if tag == TAG_STRING:
            return self.string()
        return self.table(depth)

    def table(self, depth):
        if depth == MAX_DEPTH:
            raise CodecError('too_deep')
        narr = self.varint()
        if narr > self.remaining():
            raise CodecError('truncated')
        table = {}
        for index in range(1, narr + 1):
            value = self.value(depth + 1)
            if value is None:
                raise CodecError('non_canonical')  # the run holds no nil
            table[index] = value
        nrec = self.varint()
        if nrec > self.remaining() // MIN_PAIR_BYTES:
            raise CodecError('truncated')
        previous = None
        for i in range(nrec):
            tag = self.byte()
            if tag == TAG_INT:
                key = _unzigzag(self.varint())
                if 1 <= key <= narr + 1:
                    raise CodecError('non_canonical')  # the array part, or what would extend it
            elif tag == TAG_STRING:
                key = self.string()
            else:
                raise CodecError('bad_key')
            if i > 0 and not _key_order(previous) < _key_order(key):
                raise CodecError('non_canonical')  # unsorted or duplicate
            value = self.value(depth + 1)
            if value is None:
                raise CodecError('non_canonical')  # a table cannot hold nil
            table[key] = value
            previous = key
        return table


def decode(data, limit=STORE_LIMIT):
    """The value in canonical codec v1 bytes; CodecError for anything encode() would not have written."""
    data = bytes(data)
    if len(data) > limit:
        raise CodecError('too_large')
    reader = _Reader(data)
    value = reader.value(0)
    if reader.remaining():
        raise CodecError('trailing')
    return value


# ---------------------------------------------------------------------------------------------------------------
# Blob header


def blob_header(magic, file_version):
    """The header of a persisted codec blob: four magic bytes, the file version, and VERSION."""
    magic = _as_bytes(magic)
    if len(magic) != BLOB_MAGIC_BYTES or not 0 <= file_version <= 0xFF:
        raise ValueError(f'bad blob header fields: {magic!r}, {file_version}')
    return magic + bytes([file_version, VERSION])


def check_blob_header(data, magic, file_version):
    """'ok', or why the blob must be discarded; checked in the order checkBlobHeader in BlobHeader.cpp uses."""
    if len(data) < BLOB_HEADER_BYTES:
        return 'truncated'
    if data[:BLOB_MAGIC_BYTES] != _as_bytes(magic):
        return 'bad_magic'
    if data[BLOB_MAGIC_BYTES] != file_version:
        return 'unknown_file_version'
    if data[BLOB_MAGIC_BYTES + 1] != VERSION:
        return 'unknown_codec_version'
    return 'ok'


# ---------------------------------------------------------------------------------------------------------------
# Vector notation: a Lua subset (formats.md). The C host test runs the same text with Lua itself.

_TOKEN = re.compile(r"""
    (?P<space>\s+)
  | (?P<number>(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)
  | (?P<name>[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)?)
  | (?P<string>'(?:[^'\\\n]|\\.)*'|"(?:[^"\\\n]|\\.)*")
  | (?P<punct>[{}\[\](),;=/-])
""", re.X)
_ESCAPE = re.compile(r"\\(x[0-9A-Fa-f]{2}|\d{1,3}|.)", re.S)
_SIMPLE_ESCAPES = {'\\': b'\\', "'": b"'", '"': b'"', 'n': b'\n', 't': b'\t'}
_CONSTANTS = {'nil': None, 'true': True, 'false': False, 'math.huge': math.inf, 'math.maxinteger': INT_MAX,
              'math.mininteger': INT_MIN}


def _unescape(body):
    out = bytearray()
    last = 0
    for match in _ESCAPE.finditer(body):
        out += body[last:match.start()].encode('utf-8')
        escape = match.group(1)
        if escape[0] == 'x':
            out.append(int(escape[1:], 16))
        elif escape.isdigit():
            if int(escape) > 0xFF:
                raise ValueError(f'escape \\{escape} is over 255')
            out.append(int(escape))
        elif escape in _SIMPLE_ESCAPES:
            out += _SIMPLE_ESCAPES[escape]
        else:
            raise ValueError(f'unsupported escape \\{escape}')
        last = match.end()
    out += body[last:].encode('utf-8')
    return bytes(out)


def _number(text):
    if re.fullmatch(r'\d+', text) and int(text) <= INT_MAX:
        return int(text)
    return float(text)  # a decimal integer past int64 is a float in Lua too


def _divide(a, b):
    """Lua's `/`: always float, IEEE-754 on division by zero."""
    a, b = float(a), float(b)
    if b != 0:
        return a / b
    if a == 0 or math.isnan(a):
        return math.nan
    return math.copysign(math.inf, a) * math.copysign(1.0, b)


def _negate(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError('unary minus needs a number')
    if isinstance(value, float):
        return -value
    return ((-value - INT_MIN) & MASK64) + INT_MIN  # Lua integers wrap


class _Parser:
    def __init__(self, text):
        self.tokens = []
        pos = 0
        while pos < len(text):
            match = _TOKEN.match(text, pos)
            if not match:
                raise ValueError(f'cannot read {text[pos:pos + 10]!r}')
            if match.lastgroup != 'space':
                self.tokens.append((match.lastgroup, match.group()))
            pos = match.end()
        self.pos = 0

    def peek(self, offset=0):
        index = self.pos + offset
        return self.tokens[index] if index < len(self.tokens) else ('end', '')

    def accept(self, text):
        if self.peek() == ('punct', text):
            self.pos += 1
            return True
        return False

    def expect(self, text):
        if not self.accept(text):
            raise ValueError(f'expected {text!r}, found {self.peek()[1]!r}')

    def parse(self):
        value = self.expr()
        if self.peek()[0] != 'end':
            raise ValueError(f'unexpected {self.peek()[1]!r}')
        return value

    def expr(self):
        value = self.unary()
        while self.accept('/'):
            value = _divide(value, self.unary())
        return value

    def unary(self):
        if self.accept('-'):
            return _negate(self.unary())
        return self.primary()

    def primary(self):
        kind, text = self.peek()
        self.pos += 1
        if kind == 'number':
            return _number(text)
        if kind == 'string':
            return _unescape(text[1:-1])
        if (kind, text) == ('punct', '{'):
            return self.table()
        if kind == 'name' and self.accept('('):
            return self.call(text)
        if kind == 'name' and text in _CONSTANTS:
            return _CONSTANTS[text]
        raise ValueError(f'unexpected {text!r}')

    def table(self):
        table = Table()
        index = 1
        while not self.accept('}'):
            if self.accept('['):
                key = self.expr()
                self.expect(']')
                self.expect('=')
                _assign(table, key, self.expr())
            elif self.peek()[0] == 'name' and '.' not in self.peek()[1] and self.peek(1) == ('punct', '='):
                key = self.peek()[1].encode('ascii')
                self.pos += 2
                _assign(table, key, self.expr())
            else:
                _assign(table, index, self.expr())
                index += 1
            if not (self.accept(',') or self.accept(';')):
                self.expect('}')
                break
        return table

    def call(self, name):
        args = []
        while not self.accept(')'):
            args.append(self.expr())
            if not self.accept(','):
                self.expect(')')
                break
        return _call(name, args)


def _assign(table, key, value):
    if key is None or (isinstance(key, float) and math.isnan(key)):
        raise ValueError('table index is nil or NaN')
    if value is None:
        table.pop(key, None)
    else:
        table[key] = value


def _cycle(table, *keys):
    """Makes table reachable from itself: table[k1]...[kn] = table."""
    if not keys:
        raise ValueError('cycle needs a key')
    holder = table
    for key in keys[:-1]:
        holder = holder[key]
    holder[keys[-1]] = table
    return table


def _nest(depth, value):
    for _ in range(depth):
        value = Table({1: value})
    return value


def _setmetatable(table, metatable):
    table.metatable = metatable
    return table


_CALLS = {
    'setmetatable': (2, _setmetatable),
    'cycle': (None, _cycle),
    'fn': (0, lambda: (lambda: None)),
    'nest': (2, _nest),
    'shared': (1, lambda table: Table({1: table, 2: table})),
    'string.rep': (2, lambda text, count: text * count),
}


def _call(name, args):
    if name not in _CALLS:
        raise ValueError(f'unknown function {name}')
    arity, function = _CALLS[name]
    if arity is not None and len(args) != arity:
        raise ValueError(f'{name} takes {arity} arguments')
    return function(*args)


def parse_value(text):
    """The value a vector's notation describes; ValueError when the text is outside the subset."""
    return _Parser(text).parse()


def parse_hex(text):
    """Bytes from the vector hex notation: space-separated hex groups, each optionally followed by *N."""
    out = bytearray()
    for group in text.split():
        data, star, count = group.partition('*')
        if not re.fullmatch(r'(?:[0-9a-fA-F]{2})+', data) or (star and not re.fullmatch(r'[1-9]\d*', count)):
            raise ValueError(f'bad hex group {group!r}')
        out += bytes.fromhex(data) * (int(count) if star else 1)
    return bytes(out)


def format_hex(data):
    """The hex notation for data, with runs of four or more equal bytes as XX*N."""
    groups = []
    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and data[i + run] == data[i]:
            run += 1
        groups.append(f'{data[i]:02x}*{run}' if run >= 4 else ' '.join([f'{data[i]:02x}'] * run))
        i += run
    return ' '.join(groups)


def _format_string(data):
    out = []
    for b in data:
        char = chr(b)
        out.append(char if 0x20 <= b < 0x7F and char not in "'\\" else f'\\x{b:02x}')
    return "'" + ''.join(out) + "'"


def format_value(value):
    """value in the vector notation, as decode() returns it."""
    if value is None or isinstance(value, bool):
        return {None: 'nil', True: 'true', False: 'false'}[value]
    if isinstance(value, int):
        return 'math.mininteger' if value == INT_MIN else str(value)  # -9223372036854775808 reads as a float
    if isinstance(value, float):
        if math.isnan(value):
            return '0/0'
        if math.isinf(value):
            return 'math.huge' if value > 0 else '-math.huge'
        return repr(value)
    if isinstance(value, bytes):
        return _format_string(value)
    narr = 0
    while narr + 1 in value:
        narr += 1
    parts = [format_value(value[i]) for i in range(1, narr + 1)]
    for key in sorted((k for k in value if isinstance(k, bytes) or not 1 <= k <= narr), key=_key_order):
        parts.append(f'[{format_value(key)}] = {format_value(value[key])}')
    return '{' + ', '.join(parts) + '}'


# ---------------------------------------------------------------------------------------------------------------
# Golden vectors


def same(a, b):
    """Strict equality: equal type and subtype, NaN equals NaN, and the sign of zero counts."""
    if isinstance(a, (dict, list, tuple)) or isinstance(b, (dict, list, tuple)):
        if not (isinstance(a, (dict, list, tuple)) and isinstance(b, (dict, list, tuple))):
            return False
        ea, eb = _entries(a), _entries(b)
        return ea.keys() == eb.keys() and all(same(ea[k], eb[k]) for k in ea)
    if type(a) is not type(b):
        return False
    if isinstance(a, float):
        if math.isnan(a) or math.isnan(b):
            return math.isnan(a) and math.isnan(b)
        return a == b and math.copysign(1.0, a) == math.copysign(1.0, b)
    return a == b


def load_vectors(path):
    try:
        with open(path, encoding='utf-8') as handle:
            return json.load(handle)
    except OSError as exc:
        raise SetupError(f'cannot read {path}: {exc}')
    except json.JSONDecodeError as exc:
        raise SetupError(f'{path} is not valid JSON: {exc}')


def _field(record, section, name):
    if not isinstance(record, dict) or not isinstance(record.get(name), str):
        raise SetupError(f'{section} vector {record!r} has no string field {name!r}')
    return record[name]


def _parsed(record, section):
    """(name, limit) of a record, with its value and hex parsed when it has them."""
    name = _field(record, section, 'name')
    limit_name = record.get('limit', 'store')
    if limit_name not in LIMITS:
        raise SetupError(f'{section} {name}: unknown limit {limit_name!r}')
    parsed = {'name': name, 'limit': LIMITS[limit_name]}
    try:
        if 'value' in record:
            parsed['value'] = parse_value(_field(record, section, 'value'))
        if 'hex' in record:
            parsed['hex'] = parse_hex(_field(record, section, 'hex'))
    except ValueError as exc:
        raise SetupError(f'{section} {name}: {exc}')
    return parsed


def _outcome(function, *args):
    """(result, None) or (None, error name)."""
    try:
        return function(*args), None
    except CodecError as exc:
        return None, exc.name


def _section(data, name):
    records = data.get(name)
    if not isinstance(records, list) or not records:
        raise SetupError(f'vector file has no {name!r} list')
    return records


def run_vectors(data):
    """Runs every vector through this codec; returns one line per failure (empty when all pass)."""
    if not isinstance(data, dict):
        raise SetupError('vector file is not a JSON object')
    failures = []
    constants = {'codec_version': VERSION, 'snapshot_limit': SNAPSHOT_LIMIT, 'move_limit': MOVE_LIMIT,
                 'store_limit': STORE_LIMIT, 'max_depth': MAX_DEPTH, 'blob_header_bytes': BLOB_HEADER_BYTES}
    for name, constant in constants.items():
        if data.get(name) != constant:
            failures.append(f'{name}: the vector file says {data.get(name)!r}, the codec {constant}')

    for record in _section(data, 'encode'):
        v = _parsed(record, 'encode')
        encoded, error = _outcome(encode, v['value'], v['limit'])
        if error or encoded != v['hex']:
            got = error or format_hex(encoded)
            failures.append(f"encode {v['name']}: got {got}, expected {format_hex(v['hex'])}")
        decoded, error = _outcome(decode, v['hex'], v['limit'])
        matches, _ = _outcome(same, decoded, v['value'])  # a value the codec refuses matches nothing
        if error or not matches:
            failures.append(f"decode {v['name']}: got {error or format_value(decoded)}")

    for record in _section(data, 'encode_errors'):
        v = _parsed(record, 'encode_errors')
        expected = _field(record, 'encode_errors', 'error')
        encoded, error = _outcome(encode, v['value'], v['limit'])
        if error != expected:
            failures.append(f"encode_errors {v['name']}: got {error or format_hex(encoded)}, expected {expected}")

    for record in _section(data, 'decode_errors'):
        v = _parsed(record, 'decode_errors')
        expected = _field(record, 'decode_errors', 'error')
        decoded, error = _outcome(decode, v['hex'], v['limit'])
        if error != expected:
            failures.append(f"decode_errors {v['name']}: got {error or format_value(decoded)}, expected {expected}")

    for record in _section(data, 'headers'):
        v = _parsed(record, 'headers')
        magic = _field(record, 'headers', 'magic')
        expected = _field(record, 'headers', 'status')
        file_version = record.get('file_version')
        if isinstance(file_version, bool) or not isinstance(file_version, int) or not 0 <= file_version <= 0xFF:
            raise SetupError(f"headers {v['name']}: file_version is not a number from 0 to 255")
        status = check_blob_header(v['hex'], magic, file_version)
        if status != expected:
            failures.append(f"headers {v['name']}: got {status}, expected {expected}")
        if expected == 'ok' and blob_header(magic, file_version) != v['hex'][:BLOB_HEADER_BYTES]:
            failures.append(f"headers {v['name']}: blob_header() does not write the vector's bytes")
    return failures


# ---------------------------------------------------------------------------------------------------------------
# Command line


def check(path):
    data = load_vectors(path)
    failures = run_vectors(data)
    for line in failures:
        print(line)
    if failures:
        raise Failure(f'{len(failures)} vector check(s) failed in {path}')
    print(f'every vector in {path} passes')


def encode_command(text, limit):
    try:
        value = parse_value(text)
    except ValueError as exc:
        raise SetupError(f'cannot read the value: {exc}')
    encoded, error = _outcome(encode, value, LIMITS[limit])
    if error:
        raise Failure(error)
    print(format_hex(encoded))


def decode_command(text, limit):
    try:
        data = parse_hex(text)
    except ValueError as exc:
        raise SetupError(f'cannot read the bytes: {exc}')
    decoded, error = _outcome(decode, data, LIMITS[limit])
    if error:
        raise Failure(error)
    print(format_value(decoded))


def main(argv=None):
    parser = argparse.ArgumentParser(description='Reference codec v1 for game state, moves, and ch.store.')
    commands = parser.add_subparsers(dest='command', required=True)
    check_parser = commands.add_parser('check', help='run every golden vector')
    check_parser.add_argument('vectors', nargs='?', default=str(DEFAULT_VECTORS))
    for name, help_text, argument in (('encode', 'print the bytes of a value', 'value'),
                                      ('decode', 'print the value in some bytes', 'hex')):
        sub = commands.add_parser(name, help=help_text)
        sub.add_argument(argument)
        sub.add_argument('--limit', choices=sorted(LIMITS), default='store')
    args = parser.parse_args(argv)
    if args.command == 'check':
        return fork_common.exit_code(lambda: check(args.vectors))
    if args.command == 'encode':
        return fork_common.exit_code(lambda: encode_command(args.value, args.limit))
    return fork_common.exit_code(lambda: decode_command(args.hex, args.limit))


if __name__ == '__main__':
    sys.exit(main())

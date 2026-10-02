#!/usr/bin/env python3
"""
Check how much flash and internal RAM the game runtime adds to the X4 Pro firmware, and that game objects hold no
static initializer or large mutable static (ARCHITECTURE-SPINE AD-2).

Builds the x4pro env twice on the same commit, as configured (FREEINK_CAP_GAMES=1) and with that define removed,
and fails when the build with games is more than a limit larger than the build without. Both builds come from the
same commit, so upstream growth never counts against the budget.

  build on    `pio run -e x4pro`, the normal build in .pio/build
  build off   the same with PLATFORMIO_BUILD_UNFLAGS=-DFREEINK_CAP_GAMES=1, built into .pio/build-games-off
  compare     compare the two firmware.bin sizes against the flash limit, and the two ELFs' static internal RAM
              (.dram0.data + .dram0.bss + .noinit + every .iram0.* section, from the toolchain's `size -A`) against
              the RAM limit; IRAM counts because it shares internal SRAM with DRAM on the S3
  objects     read every object the games-on build compiled from lib/Game*, src/games, and src/activities/games,
              and fail on a static initializer (a .ctors, .init_array, or .preinit_array entry, or the guard
              variable of a dynamically initialized local static) or a mutable static over 64 B

The flash limit is in KiB (1 KiB = 1,024 bytes) because ESP32 flash and the app slot in partitions.csv are sized in
binary units (the slot is 0x640000 bytes = 6,400 KiB). The default 270 KiB is 276,480 bytes. The RAM limit is
1 KiB, 1,024 bytes; the image size does not show RAM, since .dram0.bss takes no space in firmware.bin.

A mutable static is an object symbol in a writable section (or a common symbol): constexpr data lands in .rodata
and is never counted, while constinit, DRAM_ATTR, .noinit, and PSRAM .ext_ram.bss buffers are. A larger buffer
belongs to the component that owns its lifetime and is allocated with it. A guard variable fails wherever it is,
COMDAT groups included: a game object that calls an upstream inline function with a dynamically initialized local
static fails too, deliberately (AD-2 forbids dynamic initialization at any scope in game code). Other symbols in
COMDAT groups (header inline functions' statics, inline variables, and template statics, emitted into every object
that uses them) count when any source name in the mangled name, template arguments included, is a game name; the
rest are upstream's and the libraries'. The game names are the outermost names of the global symbols game objects
define outside COMDAT groups (GameCore, GameArena, and free functions such as gameHostCaps), and the namespaces and
classes game headers declare at file scope (the header-only GameTouch and ForkRelease). A game name that is also an
upstream or library name, such as a free function sharing a common name, makes that code's COMDAT statics count as
well: a visible failure, never an escape. A free inline function at global scope in a game header, with no out-of-line
definition, is still not recognized.

What `objects` does not read, consistent with AD-2's wording: `lib/lua`, the vendored Lua sources; fork code inside
the ledgered upstream files of docs/crosshatch/upstream-touches.md (the `#if FREEINK_CAP_GAMES` blocks in
`ActivityManager.cpp`, `CoverGridHomeUi.cpp`, `OtaUpdater.cpp`, and the like), whose objects are upstream's; and a
game library whose directory is not named `lib/Game*`, which stays unread until GAME_LIB_GLOB or GAME_SOURCE_DIRS
names it. Review holds those; `compare`'s RAM limit still counts any static they add to internal RAM.

Each build first saves `pio project metadata` for itself. `compare` checks from it that the "on" build defines
FREEINK_CAP_GAMES=1 and the "off" build does not define it at all: a misspelled unflag, or the flag dropped from
the env, would make both builds the same and the check pass without measuring anything. The metadata also names
the ELF, the toolchain (`size` sits beside `cc_path`), and the build directory `objects` reads.

Exit 0: within the limits. 1: over a limit, or a game object breaks a rule. 2: the check could not run (build
failed, missing output, wrong flag state, missing toolchain, game sources the build did not compile).

Local run, the same commands as the CI job (each build takes several minutes):
    python3 scripts/check_flash_budget.py build on
    python3 scripts/check_flash_budget.py build off
    python3 scripts/check_flash_budget.py compare [--limit-kib 270 | --limit-bytes N] [--ram-limit-bytes 1024]
    python3 scripts/check_flash_budget.py objects
A limit below the measured difference (zero or negative is allowed) shows the failing case without a rebuild.
"""

import argparse
import collections
import json
import os
import pathlib
import re
import subprocess
import sys

import fork_common
from fork_common import SetupError

PIO_ENV = 'x4pro'
FLAG = fork_common.GAMES_MACRO
# Written as the define appears in platformio.ini; compare checks the result, so an unflag that stops matching fails
# the job instead of passing it.
UNFLAG = fork_common.GAMES_BUILD_FLAG
KIB = 1024
DEFAULT_LIMIT_KIB = 270
DEFAULT_RAM_LIMIT_BYTES = 1 * KIB
PROJECT_DIR = pathlib.Path(__file__).resolve().parent.parent
OFF_BUILD_DIR = PROJECT_DIR / '.pio' / 'build-games-off'
DEFAULT_METADATA_DIR = PROJECT_DIR / '.pio' / 'flash-budget'
STATES = ('on', 'off')

# Static internal RAM, by ELF section name. .noinit may be absent when empty; the other two never are, so a missing
# one means the section names changed and the gate would measure nothing. IRAM shares internal SRAM with DRAM on the
# S3, so every .iram0.* section present counts too (.iram0.vectors, .iram0.text, .iram0.text_end padding,
# .iram0.data, .iram0.bss); .iram0.text is always there, and one build having an .iram0.* section the other lacks
# counts it as 0 bytes in the other.
RAM_SECTIONS = ('.dram0.data', '.dram0.bss', '.noinit')
IRAM_SECTION_PREFIX = '.iram0.'
REQUIRED_RAM_SECTIONS = ('.dram0.data', '.dram0.bss', '.iram0.text')

# Game code (AD-2): source directories and their objects under the build directory.
GAME_SOURCE_DIRS = ('src/games', 'src/activities/games')
GAME_LIB_GLOB = 'lib/Game*'
SOURCE_SUFFIXES = ('.c', '.cc', '.cpp', '.S')
MUTABLE_STATIC_LIMIT = 64
# This toolchain puts static constructors in .ctors; .init_array and .preinit_array are the same for other targets.
INITIALIZER_SECTIONS = ('.ctors', '.init_array', '.preinit_array')
GUARD_PREFIX = '_ZGV'  # Itanium C++ ABI guard variable of a dynamically initialized static

Build = collections.namedtuple('Build', 'defines elf image cc_path')


def build_environment(state, base=None):
    """Process environment for one build. Both start without the two overrides so "on" is the plain env."""
    env = dict(os.environ if base is None else base)
    env.pop('PLATFORMIO_BUILD_UNFLAGS', None)
    env.pop('PLATFORMIO_BUILD_DIR', None)
    if state == 'off':
        env['PLATFORMIO_BUILD_UNFLAGS'] = UNFLAG
        env['PLATFORMIO_BUILD_DIR'] = str(OFF_BUILD_DIR)
    return env


def metadata_path(metadata_dir, state):
    return pathlib.Path(metadata_dir) / f'{state}.json'


def pio(args, env, quiet=False):
    print(f'$ pio {" ".join(args)}', flush=True)
    try:
        code = subprocess.run(
            ['pio', *args], cwd=PROJECT_DIR, env=env, stdout=subprocess.DEVNULL if quiet else None
        ).returncode
    except OSError as exc:
        raise SetupError(f'cannot run pio: {exc}')
    if code != 0:
        raise SetupError(f'pio {" ".join(args)} failed ({code})')


def build(state, metadata_dir):
    env = build_environment(state)
    if state == 'off':
        print(f'Games off: PLATFORMIO_BUILD_UNFLAGS={UNFLAG} PLATFORMIO_BUILD_DIR={OFF_BUILD_DIR}', flush=True)
    target = metadata_path(metadata_dir, state)
    target.parent.mkdir(parents=True, exist_ok=True)
    # A stale file from an earlier run must never stand in for this build's metadata.
    target.unlink(missing_ok=True)
    # Metadata first: on a fresh tree `pio project metadata` empties the env's build dir, so run after the build it
    # deletes the image it is meant to describe. --json-output skips the dependency install; quiet keeps the whole
    # metadata out of the log.
    pio(['project', 'metadata', '-e', PIO_ENV, '--json-output', '--json-output-path', str(target)], env, quiet=True)
    print(f'Saved {target}', flush=True)
    pio(['run', '-e', PIO_ENV], env)
    image = load_build(metadata_dir, state).image
    if not image.is_file():
        raise SetupError(f'the games-{state} build left no image at {image}')
    print(f'Built {image} ({image.stat().st_size:,} bytes)', flush=True)


def load_build(metadata_dir, state):
    """Return the Build (defines, ELF, firmware.bin, compiler path) for one build, read from its saved metadata."""
    path = metadata_path(metadata_dir, state)
    try:
        data = json.loads(path.read_text())
        entry = data[PIO_ENV]
        defines = list(entry['defines'])
        elf = pathlib.Path(entry['prog_path'])
        cc_path = entry.get('cc_path')
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        raise SetupError(f'no usable metadata for the games-{state} build at {path} ({exc}); run "build {state}"')
    # The image is written beside the ELF under the same name (firmware.elf -> firmware.bin).
    return Build(defines, elf, elf.with_suffix('.bin'), cc_path)


def check_flag(state, defines):
    ours = [d for d in defines if d == FLAG or d.startswith(FLAG + '=')]
    if state == 'on' and ours != [f'{FLAG}=1']:
        raise SetupError(f'the games-on build must define exactly {FLAG}=1, found {ours or "none"}')
    if state == 'off' and ours:
        raise SetupError(f'the games-off build still defines {", ".join(ours)}; is the flag spelled {UNFLAG}?')


def toolchain_tool(build_record, state, name):
    """The binutils tool beside the build's compiler: xtensa-esp32s3-elf-gcc -> xtensa-esp32s3-elf-<name>."""
    cc_path = build_record.cc_path
    if not isinstance(cc_path, str) or not cc_path.endswith('gcc'):
        raise SetupError(f'cannot find the toolchain: the games-{state} metadata has cc_path {cc_path!r}')
    return cc_path[: -len('gcc')] + name


def run_tool(command):
    try:
        proc = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding='utf-8', errors='replace'
        )
    except OSError as exc:
        raise SetupError(f'cannot run {command[0]}: {exc}')
    if proc.returncode != 0:
        raise SetupError(f'{" ".join(command)} failed ({proc.returncode}): {proc.stderr.strip()}')
    return proc.stdout


def section_sizes(size_output):
    """Section name -> size from `size -A` output (rows of name, size, address)."""
    sizes = {}
    for line in size_output.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1].isdigit() and fields[2].isdigit():
            sizes[fields[0]] = int(fields[1])
    return sizes


def ram_sections(sizes):
    """The static internal RAM sections of one `size -A` result: each RAM_SECTIONS name (0 when absent), then every
    .iram0.* section present, in the tool's order."""
    counted = {name: sizes.get(name, 0) for name in RAM_SECTIONS}
    counted.update((name, size) for name, size in sizes.items() if name.startswith(IRAM_SECTION_PREFIX))
    return counted


def static_ram(build_record, state):
    """Bytes in each static internal RAM section of one build's ELF (ram_sections)."""
    if not build_record.elf.is_file():
        raise SetupError(f'missing games-{state} ELF {build_record.elf}; run "build {state}"')
    size_tool = toolchain_tool(build_record, state, 'size')
    sizes = section_sizes(run_tool([size_tool, '-A', str(build_record.elf)]))
    missing = [name for name in REQUIRED_RAM_SECTIONS if name not in sizes]
    if missing:
        raise SetupError(f'the games-{state} ELF {build_record.elf} has no {", ".join(missing)} section')
    return ram_sections(sizes)


def signed(value):
    return f'{value:+,}'


def verdict(diff, limit_bytes):
    if diff <= limit_bytes:
        return True, f'**Within budget**: {limit_bytes - diff:,} B to spare.'
    return False, f'**Over budget** by {diff - limit_bytes:,} B.'


def report(on_size, off_size, limit_bytes):
    diff = on_size - off_size
    within, line = verdict(diff, limit_bytes)
    lines = [
        '## x4pro flash budget',
        '',
        '| x4pro `firmware.bin` | Bytes | KiB |',
        '| --- | ---: | ---: |',
        f'| Games on (`{FLAG}=1`) | {on_size:,} | {on_size / KIB:,.1f} |',
        f'| Games off | {off_size:,} | {off_size / KIB:,.1f} |',
        f'| Difference | {signed(diff)} | {diff / KIB:+,.1f} |',
        f'| Limit | {limit_bytes:,} | {limit_bytes / KIB:,.1f} |',
        '',
        line,
        '',
    ]
    return within, '\n'.join(lines)


def ram_report(on_ram, off_ram, limit_bytes):
    on_total = sum(on_ram.values())
    off_total = sum(off_ram.values())
    within, line = verdict(on_total - off_total, limit_bytes)
    lines = [
        '## x4pro static internal RAM',
        '',
        '| x4pro `firmware.elf` section | Games on | Games off | Difference |',
        '| --- | ---: | ---: | ---: |',
    ]
    # An .iram0.* section only one build has is 0 bytes in the other.
    names = list(on_ram) + [name for name in off_ram if name not in on_ram]
    for name in names:
        on, off = on_ram.get(name, 0), off_ram.get(name, 0)
        lines.append(f'| `{name}` | {on:,} | {off:,} | {signed(on - off)} |')
    lines += [
        f'| Total | {on_total:,} | {off_total:,} | {signed(on_total - off_total)} |',
        f'| Limit | | | {limit_bytes:,} |',
        '',
        line,
        '',
    ]
    return within, '\n'.join(lines)


def compare(metadata_dir, limit_bytes, summary_path=None, ram_limit_bytes=DEFAULT_RAM_LIMIT_BYTES):
    """Return 0 within both limits, 1 over either; raise SetupError when the two builds cannot be compared."""
    sizes = {}
    images = {}
    ram = {}
    for state in STATES:
        build_record = load_build(metadata_dir, state)
        check_flag(state, build_record.defines)
        image = build_record.image
        if not image.is_file():
            raise SetupError(f'missing games-{state} image {image}; run "build {state}"')
        images[state] = image.resolve()
        sizes[state] = image.stat().st_size
        ram[state] = static_ram(build_record, state)
    if images['on'] == images['off']:
        raise SetupError(f'both builds point at the same image {images["on"]}')

    within, text = report(sizes['on'], sizes['off'], limit_bytes)
    ram_within, ram_text = ram_report(ram['on'], ram['off'], ram_limit_bytes)
    text = f'{text}\n{ram_text}'
    print(text)
    if summary_path:
        fork_common.write_step_summary(text + '\n', summary_path)
    return fork_common.PASS if within and ram_within else fork_common.FAIL


# `readelf -W -S -s` rows. A section header: [Nr] Name Type Address Off Size ES Flg Lk Inf Al (the name is blank for
# section 0); a symbol: Num: Value Size Type Bind Vis Ndx Name. readelf prints a size of 100,000 or more in hex.
SECTION_ROW = re.compile(
    r'^\s*\[\s*(?P<index>\d+)\] (?P<name>.*?) +(?P<type>\S+) +[0-9a-f]{8,} [0-9a-f]{6,} (?P<size>[0-9a-f]{6,}) '
    r'[0-9a-f]{2,} (?P<flags>[A-Za-z ]*?) *\d+ +\d+ +\d+$'
)
SYMBOL_ROW = re.compile(
    r'^\s*\d+: [0-9a-f]+ +(?P<size>0x[0-9a-f]+|\d+) (?P<type>\S+) +(?P<bind>\S+) +\S+ +(?P<ndx>\S+) ?(?P<name>\S*)$'
)
# The counts readelf announces, so a row the patterns above misread fails the check instead of being skipped.
SECTION_COUNT = re.compile(r'^There are (\d+) section headers')
SYMBOL_COUNT = re.compile(r"^Symbol table '\.symtab' contains (\d+) entr")
INITIALIZER_TYPES = ('INIT_ARRAY', 'PREINIT_ARRAY')
MUTABLE_TYPES = ('OBJECT', 'COMMON', 'TLS')
DEFINING_TYPES = MUTABLE_TYPES + ('FUNC',)
# The start of an Itanium C++ ABI mangled name, up to the length of its first <source-name>: an optional special name
# (GV guard variable, GR reference temporary, TH/TW thread-local init and wrapper, TV/TT/TI/TS vtable, VTT, and type
# info), then any of Z (a local entity's enclosing function), N (a nested name), L (internal linkage), and a nested
# name's qualifiers. A name that goes on with anything else (St for std::, a substitution) has no outermost source
# name here.
MANGLED_OUTER = re.compile(r'_Z(?:G[VR]|T[HWVTIS])?[ZNLrVKRO]*(\d+)')
# A <source-name> length anywhere in a mangled name. Every digit run is read as one, so an integer template argument
# (Lj24E) yields a stray candidate too; a stray candidate matters only if it spells a game name exactly.
SOURCE_NAME_LENGTH = re.compile(r'(?<!\d)(\d+)')
HEADER_SUFFIXES = ('.h', '.hpp')
# File-scope declarations in a game header, on a line with comments and literals removed: a named namespace opened
# there (namespace A or A::B), and a class or struct defined there (a forward declaration ends in ';' and is not one).
HEADER_NAMESPACE = re.compile(r'\s*namespace\s+(\w+)(?:::\w+)*\s*\{')
HEADER_CLASS = re.compile(r'\s*(?:template\s*<.*>\s*)?(?:class|struct)\s+(\w+)(?:\s+final)?\s*(?:[:{].*)?$')
LITERALS_AND_COMMENTS = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//.*|/\*.*?\*/')
Section = collections.namedtuple('Section', 'name type flags size')
Symbol = collections.namedtuple('Symbol', 'name type size ndx bind')


def parse_readelf(output):
    """Return ({index: Section}, [Symbol], (announced sections, announced symbols)) from `readelf -W -S -s` output;
    an announced count is None when readelf printed no such line."""
    sections = {}
    symbols = []
    counts = [None, None]
    for line in output.splitlines():
        for slot, pattern in enumerate((SECTION_COUNT, SYMBOL_COUNT)):
            announced = pattern.match(line)
            if announced:
                counts[slot] = int(announced[1])
        row = SECTION_ROW.match(line)
        if row:
            sections[int(row['index'])] = Section(row['name'], row['type'], row['flags'].strip(), int(row['size'], 16))
            continue
        row = SYMBOL_ROW.match(line)
        if row:
            size = row['size']
            symbols.append(Symbol(row['name'], row['type'], int(size, 16) if size.startswith('0x') else int(size),
                                  row['ndx'], row['bind']))
    return sections, symbols, tuple(counts)


def outer_name(symbol_name):
    """The outermost name a symbol belongs to: the first namespace or class of a mangled name, or the function that
    holds a local static, or the entity itself when it is not nested (an unmangled name is returned whole). None when
    the name does not start with a source name (std::, a substitution, an unknown special name).

    _ZZN8GameCore8instanceEvE3big (GameCore::instance()::big) and its guard _ZGVZN8GameCore8instanceEvE3big give
    'GameCore'; _ZN8GameCore4PoolIiE7storageE (GameCore::Pool<int>::storage) gives 'GameCore'.
    """
    if not symbol_name.startswith('_Z'):
        return symbol_name
    match = MANGLED_OUTER.match(symbol_name)
    if not match:
        return None
    start = match.end()
    name = symbol_name[start : start + int(match[1])]
    return name if len(name) == int(match[1]) else None


def source_names(symbol_name):
    """Every source name in a mangled name, template arguments included (an unmangled name is its only one):
    _ZZN16PersistableStoreI9GameStoreE11getInstanceEvE8instance gives PersistableStore, GameStore, getInstance, and
    instance."""
    if not symbol_name.startswith('_Z'):
        return {symbol_name}
    names = set()
    for match in SOURCE_NAME_LENGTH.finditer(symbol_name, 2):
        name = symbol_name[match.end() : match.end() + int(match[1])]
        if len(name) == int(match[1]):
            names.add(name)
    return names


def declared_game_names(project_dir=PROJECT_DIR):
    """The namespaces and classes the game headers (lib/Game*, src/games, src/activities/games) declare at file
    scope, so header-only game code counts too. A line is read after removing comments and string literals; braces
    track the scope, and preprocessor lines are skipped."""
    project_dir = pathlib.Path(project_dir)
    roots = [project_dir / d for d in GAME_SOURCE_DIRS] + sorted(project_dir.glob(GAME_LIB_GLOB))
    names = set()
    for root in roots:
        if not root.is_dir():
            continue
        for header in sorted(p for p in root.rglob('*') if p.suffix in HEADER_SUFFIXES and p.is_file()):
            try:
                text = header.read_text(encoding='utf-8', errors='replace')
            except OSError as exc:
                raise SetupError(f'cannot read {header} ({exc})')
            # Block comments spanning lines go first; the rest are removed line by line.
            text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m[0].count('\n'), text, flags=re.S)
            depth = 0
            for line in text.splitlines():
                if line.lstrip().startswith('#'):
                    continue
                line = LITERALS_AND_COMMENTS.sub('""', line)
                if depth == 0:
                    declared = HEADER_NAMESPACE.match(line) or HEADER_CLASS.match(line)
                    if declared:
                        names.add(declared[1])
                depth = max(0, depth + line.count('{') - line.count('}'))
    return names


def defined_symbol_section(sections, symbol):
    """The section a symbol is defined in (a common symbol gets a writable stand-in), or None when undefined, absolute,
    or in a section readelf did not list."""
    if symbol.ndx == 'COM':
        return Section('common', '', 'WA', symbol.size)
    if symbol.ndx.isdigit() and int(symbol.ndx) in sections:
        return sections[int(symbol.ndx)]
    return None


def game_names(parsed_objects):
    """The outermost names of the global symbols that game objects define outside COMDAT groups: the game's namespaces,
    classes, and free functions and variables. Local symbols do not count: they include the compiler's clones of
    upstream functions (a name ending in $isra$0 or .constprop.0) and its constants. parsed_objects: (sections,
    symbols) pairs."""
    names = set()
    for sections, symbols in parsed_objects:
        for symbol in symbols:
            section = defined_symbol_section(sections, symbol)
            if section is None or 'G' in section.flags or symbol.type not in DEFINING_TYPES or symbol.bind != 'GLOBAL':
                continue
            name = outer_name(symbol.name)
            if name:
                names.add(name)
    return names


def object_problems(sections, symbols, game_scopes=frozenset()):
    """Return (problems, largest mutable static as (size, name) or None) for one object's parsed readelf output.

    A guard variable fails wherever it is. Other symbols in a COMDAT group (flag G) count only when one of their
    source names (source_names, template arguments included) is in game_scopes: the others are header inline
    functions' statics and inline variables of upstream or library code, emitted into every object that uses them.
    A game's own inline function statics, class-template static members, and function-template statics are COMDAT
    too, and count, as do an upstream template's statics instantiated with a game type, such as
    PersistableStore<GameStore>::getInstance()'s instance.
    """
    problems = []
    for section in sections.values():
        if section.type in INITIALIZER_TYPES or section.name.startswith(INITIALIZER_SECTIONS):
            if section.size > 0:
                problems.append(f'static initializer: {section.name} holds {section.size:,} B of entries')
    largest = None
    for symbol in symbols:
        section = defined_symbol_section(sections, symbol)
        if section is None:
            continue
        if symbol.name.startswith(GUARD_PREFIX):
            problems.append(f'static initializer: guard variable {symbol.name} of a dynamically initialized static')
        if 'G' in section.flags and not source_names(symbol.name) & game_scopes:
            continue
        if symbol.type not in MUTABLE_TYPES or 'W' not in section.flags:
            continue  # code, or read-only data such as constexpr
        if largest is None or symbol.size > largest[0]:
            largest = (symbol.size, symbol.name)
        if symbol.size > MUTABLE_STATIC_LIMIT:
            problems.append(
                f'mutable static {symbol.name} is {symbol.size:,} B in {section.name} (limit {MUTABLE_STATIC_LIMIT} B)'
            )
    return problems, largest


def inspect_object(readelf, path):
    """Parse one object with readelf into (sections, symbols); raise SetupError when a row did not parse (a format
    change)."""
    sections, symbols, (section_count, symbol_count) = parse_readelf(run_tool([readelf, '-W', '-S', '-s', str(path)]))
    if not sections or section_count != len(sections) or symbol_count not in (None, len(symbols)):
        raise SetupError(
            f'could not read {path} with {readelf}: parsed {len(sections)} of {section_count} section headers and '
            f'{len(symbols)} of {symbol_count} symbols'
        )
    return sections, symbols


def game_objects(build_dir, project_dir=PROJECT_DIR):
    """Every object compiled from a game source that still exists; raise SetupError when a game directory has sources
    but the build holds no object for any of them (a changed build layout would otherwise pass)."""
    build_dir = pathlib.Path(build_dir)
    project_dir = pathlib.Path(project_dir)
    roots = [(project_dir / d, [build_dir / d]) for d in GAME_SOURCE_DIRS]
    # A library's objects are in .pio/build/<env>/lib<hash>/<library directory name>/.
    roots += [(lib, sorted(build_dir.glob(f'lib*/{lib.name}'))) for lib in sorted(project_dir.glob(GAME_LIB_GLOB))]
    objects = []
    for source_dir, object_dirs in roots:
        if not source_dir.is_dir():
            continue
        # X.cpp compiles to X.cpp.o at the same relative path (a library's src/ folder is dropped). An object whose
        # source is gone is stale output of an incremental build, which never deletes it, so it is not checked.
        found = sorted(
            o
            for d in object_dirs
            if d.is_dir()
            for o in d.rglob('*.o')
            if any((base / o.relative_to(d).with_suffix('')).is_file() for base in (source_dir, source_dir / 'src'))
        )
        has_sources = any(p.suffix in SOURCE_SUFFIXES and p.is_file() for p in source_dir.rglob('*'))
        if has_sources and not found:
            raise SetupError(
                f'{source_dir.relative_to(project_dir)} has sources but {build_dir} holds no object for it; '
                'did the games-on build compile it?'
            )
        objects += found
    return objects


def check_objects(metadata_dir, summary_path=None, project_dir=PROJECT_DIR):
    """Return 0 when no game object breaks AD-2's static rules, 1 when one does."""
    build_record = load_build(metadata_dir, 'on')
    check_flag('on', build_record.defines)
    build_dir = build_record.elf.parent
    if not build_dir.is_dir():
        raise SetupError(f'missing games-on build directory {build_dir}; run "build on"')
    readelf = toolchain_tool(build_record, 'on', 'readelf')
    objects = game_objects(build_dir, project_dir)
    parsed = [inspect_object(readelf, path) for path in objects]
    scopes = game_names(parsed) | declared_game_names(project_dir)
    problems = []
    largest = None
    for path, (sections, symbols) in zip(objects, parsed):
        found, biggest = object_problems(sections, symbols, scopes)
        name = path.relative_to(build_dir)
        problems += [f'`{name}`: {problem}' for problem in found]
        if biggest and (largest is None or biggest[0] > largest[0]):
            largest = (biggest[0], biggest[1], name)
    largest_text = f'{largest[0]:,} B (`{largest[1]}` in `{largest[2]}`)' if largest else 'none'
    lines = [
        '## x4pro game objects',
        '',
        f'Game objects checked: {len(objects)}. Largest mutable static: {largest_text}; '
        f'the limit is {MUTABLE_STATIC_LIMIT} B, and no static initializer is allowed.',
        '',
    ]
    if problems:
        lines += [f'**{len(problems)} problem(s):**', ''] + [f'- {p}' for p in problems] + ['']
    else:
        lines += ['**No problems.**', '']
    text = '\n'.join(lines)
    print(text)
    if summary_path:
        fork_common.write_step_summary(text + '\n', summary_path)
    return fork_common.FAIL if problems else fork_common.PASS


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--metadata-dir', default=str(DEFAULT_METADATA_DIR), help=argparse.SUPPRESS)
    parser.add_argument('--project-dir', default=str(PROJECT_DIR), help=argparse.SUPPRESS)
    sub = parser.add_subparsers(dest='command', required=True)
    build_parser = sub.add_parser('build', help='build x4pro with games on or off')
    build_parser.add_argument('state', choices=STATES)
    compare_parser = sub.add_parser('compare', help='compare the two builds against the flash and RAM limits')
    limit = compare_parser.add_mutually_exclusive_group()
    limit.add_argument('--limit-kib', type=int, help=f'flash limit in KiB (default {DEFAULT_LIMIT_KIB})')
    limit.add_argument('--limit-bytes', type=int, help='flash limit in bytes')
    compare_parser.add_argument(
        '--ram-limit-bytes',
        type=int,
        default=DEFAULT_RAM_LIMIT_BYTES,
        help=f'static internal RAM limit in bytes (default {DEFAULT_RAM_LIMIT_BYTES})',
    )
    sub.add_parser('objects', help='check the games-on build\'s game objects for static initializers and statics')
    args = parser.parse_args(argv)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')

    def step():
        if args.command == 'build':
            build(args.state, args.metadata_dir)
            return fork_common.PASS
        if args.command == 'objects':
            return check_objects(args.metadata_dir, summary, args.project_dir)
        if args.limit_bytes is not None:
            limit_bytes = args.limit_bytes
        else:
            limit_bytes = (DEFAULT_LIMIT_KIB if args.limit_kib is None else args.limit_kib) * KIB
        return compare(args.metadata_dir, limit_bytes, summary, args.ram_limit_bytes)

    # A SetupError also goes to the job summary, so a check that could not run does not look like an empty report.
    heading = 'x4pro game objects' if args.command == 'objects' else 'x4pro flash budget'
    return fork_common.exit_code(step, summary_heading=heading)


if __name__ == '__main__':
    sys.exit(main())

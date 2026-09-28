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
              (.dram0.data + .dram0.bss + .noinit, from the toolchain's `size -A`) against the RAM limit
  objects     read every object the games-on build compiled from lib/Game*, src/games, and src/activities/games,
              and fail on a static initializer (a .ctors, .init_array, or .preinit_array entry, or the guard
              variable of a dynamically initialized local static) or a mutable static over 64 B

The flash limit is in KiB (1 KiB = 1,024 bytes) because ESP32 flash and the app slot in partitions.csv are sized in
binary units (the slot is 0x640000 bytes = 6,400 KiB). The default 250 KiB is 256,000 bytes. The RAM limit is
1 KiB, 1,024 bytes; the image size does not show RAM, since .dram0.bss takes no space in firmware.bin.

A mutable static is an object symbol in a writable section (or a common symbol): constexpr data lands in .rodata
and is never counted, while constinit, DRAM_ATTR, .noinit, and PSRAM .ext_ram.bss buffers are. A larger buffer
belongs to the component that owns its lifetime and is allocated with it. A guard variable fails wherever it is,
COMDAT groups included. Other symbols in COMDAT groups (header inline functions' statics, inline variables, and
template statics, emitted into every object that uses them) count only when their outermost namespace, class, or
function is one the game objects define outside COMDAT groups as global symbols, such as GameCore or GameArena; the
rest are upstream's and the libraries'. A game name that no game object defines out of line, a header-only
namespace or a free inline function at global scope, is not recognized, so its mutable statics are left to review.

Each build first saves `pio project metadata` for itself. `compare` checks from it that the "on" build defines
FREEINK_CAP_GAMES=1 and the "off" build does not define it at all: a misspelled unflag, or the flag dropped from
the env, would make both builds the same and the check pass without measuring anything. The metadata also names
the ELF, the toolchain (`size` sits beside `cc_path`), and the build directory `objects` reads.

Exit 0: within the limits. 1: over a limit, or a game object breaks a rule. 2: the check could not run (build
failed, missing output, wrong flag state, missing toolchain, game sources the build did not compile).

Local run, the same commands as the CI job (each build takes several minutes):
    python3 scripts/check_flash_budget.py build on
    python3 scripts/check_flash_budget.py build off
    python3 scripts/check_flash_budget.py compare [--limit-kib 250 | --limit-bytes N] [--ram-limit-bytes 1024]
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
DEFAULT_LIMIT_KIB = 250
DEFAULT_RAM_LIMIT_BYTES = 1 * KIB
PROJECT_DIR = pathlib.Path(__file__).resolve().parent.parent
OFF_BUILD_DIR = PROJECT_DIR / '.pio' / 'build-games-off'
DEFAULT_METADATA_DIR = PROJECT_DIR / '.pio' / 'flash-budget'
STATES = ('on', 'off')

# Static internal RAM, by ELF section name. .noinit may be absent when empty; the other two never are, so a missing
# one means the section names changed and the gate would measure nothing.
RAM_SECTIONS = ('.dram0.data', '.dram0.bss', '.noinit')
REQUIRED_RAM_SECTIONS = ('.dram0.data', '.dram0.bss')

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


def static_ram(build_record, state):
    """Bytes in each RAM_SECTIONS section of one build's ELF."""
    if not build_record.elf.is_file():
        raise SetupError(f'missing games-{state} ELF {build_record.elf}; run "build {state}"')
    size_tool = toolchain_tool(build_record, state, 'size')
    sizes = section_sizes(run_tool([size_tool, '-A', str(build_record.elf)]))
    missing = [name for name in REQUIRED_RAM_SECTIONS if name not in sizes]
    if missing:
        raise SetupError(f'the games-{state} ELF {build_record.elf} has no {", ".join(missing)} section')
    return {name: sizes.get(name, 0) for name in RAM_SECTIONS}


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
    for name in RAM_SECTIONS:
        lines.append(f'| `{name}` | {on_ram[name]:,} | {off_ram[name]:,} | {signed(on_ram[name] - off_ram[name])} |')
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
    return 0 if within and ram_within else 1


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

    A guard variable fails wherever it is. Other symbols in a COMDAT group (flag G) count only when their outermost
    name (outer_name) is in game_scopes: the others are header inline functions' statics and inline variables of
    upstream or library code, emitted into every object that uses them, such as upstream's
    PersistableStore<T>::getInstance() instance. A game's own inline function statics, class-template static members,
    and function-template statics are COMDAT too, and count.
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
        if 'G' in section.flags and outer_name(symbol.name) not in game_scopes:
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
    scopes = game_names(parsed)
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
    return 1 if problems else 0


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
            return 0
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

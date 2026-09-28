#!/usr/bin/env python3
"""
Check that the game code's #include edges follow the spine's layer table (retro AI-5).

The layer table in _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/
ARCHITECTURE-SPINE.md (Design Paradigm, and the diagram under Invariants & Rules) says what each game component may
depend on. LAYERS below holds it as data. Every #include in lib/GameCore, lib/GameIcons, lib/GameScript, src/games,
and src/activities/games is resolved to what it reaches:
  - a quoted include to the including file's folder, then to src/;
  - any include to a header under lib/<name>/ or lib/<name>/src/ (the component lib/<name>);
  - an angle include to a src/ path (`<games/GameVM.h>`);
  - otherwise to the C/C++ standard library, the platform (Arduino, ESP-IDF, FreeRTOS, POSIX; its radio and crypto
    headers apart), or an SDK header, by the tables below. A header none of these know is reported, so a new edge is
    classified on purpose.
A repository path under src/ outside the game folders is upstream (`src (upstream)`), except src/fontIds.h, which the
table names on its own. Screens may include upstream src/ code (their screen infrastructure), so the check also:
  - fails any other source file under src/ or lib/ (outside the game folders and the vendored lib/lua) that includes
    a lib/GameScript or lib/lua header, so no upstream header can launder that edge;
  - fails any such file that includes any other game component unless UPSTREAM_EDGES gives that file that component:
    upstream code reaches game code only through its ledger rows (docs/crosshatch/upstream-touches.md, AD-3), and
    UPSTREAM_EDGES is the spine's "Upstream hooks" row;
  - fails a Screens file that uses the word GameScript anywhere outside comments and literals (GameScript::,
    `using namespace`, a namespace alias, a #define): Screens reach lib/GameScript only through src/games;
  - fails a src/games header that re-exports GameScript at global scope (`using namespace GameScript;`,
    `using X = GameScript::Y;`, `using GameScript::Y;`, `namespace X = GameScript;`).
It reads #include, #include_next, and #import lines. Deliberate evasions are out of scope: digraphs (`%:include`), a
directive split by line continuations, an include through a macro (which fails as a computed include when it is a
plain `#include MACRO`), and raw string literals or other text that fools the comment and literal blanking.

Exit 0: every edge is allowed. Exit 1: an edge the table does not allow, an include it cannot classify, or one of the
GameScript uses above; each is printed as path:line. Exit 2: the check could not run (no such root, a game folder
missing, an unreadable file).

Usage: python3 scripts/check_layers.py [--root <repository root>]   # default: this script's repository
       python3 scripts/check_layers_test.py                        # the script's own tests; standard library only

When the spine's table changes, change LAYERS (and FILE_EDGES / ONLY_FROM / UPSTREAM_EDGES) in the same commit.
"""

import argparse
import fnmatch
import os
import pathlib
import re
import sys

import fork_common
from fork_common import Failure, SetupError

SUMMARY_HEADING = 'Layer check'

STD = 'std'
PLATFORM = 'platform'
RADIO_CRYPTO = 'platform radio/crypto'
UPSTREAM_SRC = 'src (upstream)'
FONT_IDS = 'src/fontIds.h'
SCREENS = 'src/activities/games'
ADAPTERS = 'src/games'

# The game components the check scans, most specific first (src/activities/games is not under src/games, but a
# longer prefix must still win over a shorter one).
COMPONENTS = (SCREENS, ADAPTERS, 'lib/GameCore', 'lib/GameIcons', 'lib/GameScript')
# The vendored engine: not scanned, and no upstream source file may include it or lib/GameScript.
UNSCANNED = ('lib/lua',)
GAME_SCRIPT_OR_LUA = {'lib/GameScript', 'lib/lua'}

# Edges the rows below add to the spine's table, each a convention rather than a design decision:
#   - lib/Logging and lib/Memory for src/games and Screens: AGENTS.md makes device code log with LOG_* and allocate
#     with makeUniqueNoThrow;
#   - lib/I18n for Screens: AGENTS.md puts user-facing text through tr();
#   - the standard library for GameIcons: its generated data uses the fixed-width integer types;
#   - lua.hpp for src/games/GamesBuildAnchor.cpp only (FILE_EDGES): AD-2's build anchor.
CONVENTIONS = {'lib/Logging', 'lib/Memory'}

# The spine's layer table (ARCHITECTURE-SPINE.md, Design Paradigm) and its diagram, one row per game component: what
# each may include besides its own headers.
LAYERS = {
    # Domain: C++ standard library, lib/Memory, lib/JsonParser.
    'lib/GameCore': {STD, 'lib/Memory', 'lib/JsonParser'},
    # Icon data: nothing (generated data only); standard integer types.
    'lib/GameIcons': {STD},
    # Script adapter: GameCore, GameIcons (names), lib/lua, lib/Utf8 (TextMetrics).
    'lib/GameScript': {STD, 'lib/GameCore', 'lib/GameIcons', 'lib/lua', 'lib/Utf8'},
    # Device adapters: GameCore, GameScript, GameIcons, HAL and Storage (lib/hal), ZipFile, PngToBmpConverter, ESP-NOW
    # and mbedTLS (the platform, radio and crypto included); lib/Utf8; lib/EpdFont and src/fontIds.h (FrameReplay); the
    # SDK's FreeInkUICore.h (GameTouch.h); SecureHttpClient (ForkReleaseProbe only, ONLY_FROM). GfxRenderer from the
    # diagram's upstream node.
    ADAPTERS: {
        STD, PLATFORM, RADIO_CRYPTO, 'lib/GameCore', 'lib/GameScript', 'lib/GameIcons', 'lib/hal', 'lib/ZipFile',
        'lib/PngToBmpConverter', 'lib/GfxRenderer', 'lib/Utf8', 'lib/EpdFont', FONT_IDS, 'sdk:FreeInkUICore.h',
        'sdk:SecureHttpClient.h', *CONVENTIONS,
    },
    # Screens: src/games, GameCore, GfxRenderer, UiListActivity / UiAppHost and the rest of upstream's screen
    # infrastructure (src/ outside the game folders, lib/I18n), the diagram's upstream node (HAL, Storage, ZipFile,
    # PngToBmpConverter), and the platform without the radio and crypto the table gives only to src/games. Never
    # lib/GameScript or lib/lua: those only through src/games.
    SCREENS: {
        STD, PLATFORM, ADAPTERS, 'lib/GameCore', 'lib/GfxRenderer', UPSTREAM_SRC, 'lib/I18n', 'lib/hal',
        'lib/ZipFile', 'lib/PngToBmpConverter', *CONVENTIONS,
    },
}

# Edges the table allows only from the files it names (the spine says "SecureHttpClient in ForkReleaseProbe only").
ONLY_FROM = {
    'sdk:SecureHttpClient.h': {'src/games/ForkReleaseProbe.h', 'src/games/ForkReleaseProbe.cpp'},
}

# The spine's "Upstream hooks (AD-3 ledger rows)" row: each upstream file that includes game code, and the components
# its ledger row lets it include. Any other upstream include of a game component (COMPONENTS) fails; lib/GameScript
# and lib/lua fail from every upstream file.
UPSTREAM_EDGES = {
    # Row 5: goHome's mapping and goToGames() open the Games list.
    'src/activities/ActivityManager.cpp': {SCREENS},
    # Row 9: Home's cover-grid Games tab draws a GameIcons bitmap.
    'src/components/CoverGridHomeUi.cpp': {'lib/GameIcons'},
    # Row 10: ForkRelease.h and games/ForkReleaseProbe.h for the fork's release (AD-25).
    'src/network/OtaUpdater.cpp': {'lib/GameCore', ADAPTERS},
}

# Edges of one file that its component's row does not have. AD-2: GamesBuildAnchor.cpp includes a header of each game
# library and lua.h, so every env compiles them.
FILE_EDGES = {
    ('src/games/GamesBuildAnchor.cpp', 'lib/lua'),
}

# Headers outside the repository. The C and C++ standard library headers the game code may use.
STD_HEADERS = {
    'algorithm', 'array', 'atomic', 'bit', 'bitset', 'cassert', 'cctype', 'cerrno', 'cfloat', 'charconv', 'chrono',
    'cinttypes', 'climits', 'cmath', 'compare', 'concepts', 'condition_variable', 'cstdarg', 'cstdbool', 'cstddef',
    'cstdint', 'cstdio', 'cstdlib', 'cstring', 'ctime', 'cwchar', 'deque', 'functional', 'initializer_list',
    'iterator', 'limits', 'list', 'map', 'memory', 'mutex', 'new', 'numeric', 'optional', 'queue', 'ratio', 'set',
    'span', 'stack', 'string', 'string_view', 'tuple', 'type_traits', 'unordered_map', 'unordered_set', 'utility',
    'variant', 'vector',
    'assert.h', 'ctype.h', 'errno.h', 'float.h', 'inttypes.h', 'limits.h', 'math.h', 'setjmp.h', 'stdarg.h',
    'stdbool.h', 'stddef.h', 'stdint.h', 'stdio.h', 'stdlib.h', 'string.h', 'time.h',
}
# The platform's radio, network, and crypto headers (ESP-NOW, Wi-Fi, mbedTLS, and OpenSSL, the simulator's stand-in
# for mbedTLS, AD-2), which the table gives to src/games only. Matched before PLATFORM_PATTERNS.
RADIO_CRYPTO_PATTERNS = (
    'WiFi*.h', 'esp_now*.h', 'esp_wifi*.h', 'esp_netif*.h', 'esp_http*.h', 'esp_tls*.h', 'lwip/*', 'mbedtls/*',
    'openssl/*',
)
# The rest of the platform: Arduino, ESP-IDF (esp_timer, esp_random, ...), FreeRTOS, and POSIX.
PLATFORM_PATTERNS = ('Arduino.h', 'esp_*.h', 'freertos/*', 'sys/*', 'strings.h', 'unistd.h')
# SDK headers (the freeink-sdk submodule, which the check does not need checked out), each its own edge.
SDK_HEADERS = {'FreeInkUICore.h', 'SecureHttpClient.h'}

SOURCE_SUFFIXES = {'.h', '.hh', '.hpp', '.inl', '.ipp', '.c', '.cc', '.cpp', '.cxx'}
HEADER_SUFFIXES = {'.h', '.hh', '.hpp', '.inl', '.ipp'}
INCLUDE = re.compile(r'^\s*#\s*(?:include_next|include|import)\b\s*(.*)$')
INCLUDE_TARGET = re.compile(r'(<([^>]+)>|"([^"]+)")')
# Comments, then string and character literals, blanked so a mention in prose or text is no use.
COMMENT_OR_LITERAL = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
GAME_SCRIPT_WORD = re.compile(r'\bGameScript\b')
PREPROCESSOR_LINE = re.compile(r'^[ \t]*#[^\n]*', re.M)
REEXPORT_START = re.compile(r'\s*(using\b|namespace\s+\w+\s*=)')


def component_of(rel):
    """The component a repository path (posix, relative) belongs to."""
    for comp in COMPONENTS:
        if rel == comp or rel.startswith(comp + '/'):
            return comp
    if rel == FONT_IDS:
        return FONT_IDS
    parts = rel.split('/')
    if parts[0] == 'lib' and len(parts) > 2:
        return f'lib/{parts[1]}'
    if parts[0] == 'src':
        return UPSTREAM_SRC
    return rel


def lib_index(root):
    """{include name: set of repository paths} for every header under lib/<name>/ and lib/<name>/src/."""
    index = {}
    lib = root / 'lib'
    if not lib.is_dir():
        return index
    for entry in sorted(lib.iterdir()):
        if not entry.is_dir():
            continue
        bases = [entry] + ([entry / 'src'] if (entry / 'src').is_dir() else [])
        for dirpath, _, filenames in os.walk(entry, followlinks=True):
            for name in filenames:
                path = pathlib.Path(dirpath) / name
                if path.suffix not in HEADER_SUFFIXES:
                    continue
                rel = f'lib/{entry.name}/{path.relative_to(entry).as_posix()}'
                for base in bases:
                    try:
                        key = path.relative_to(base).as_posix()
                    except ValueError:
                        continue
                    index.setdefault(key, set()).add(rel)
    return index


def classify_external(name):
    """std, platform, platform radio/crypto, sdk:<name>, or None for a header outside the repository."""
    if name in STD_HEADERS:
        return STD
    if any(fnmatch.fnmatchcase(name, pattern) for pattern in RADIO_CRYPTO_PATTERNS):
        return RADIO_CRYPTO
    if any(fnmatch.fnmatchcase(name, pattern) for pattern in PLATFORM_PATTERNS):
        return PLATFORM
    if name in SDK_HEADERS:
        return f'sdk:{name}'
    return None


def in_repo(root, candidate):
    """candidate, normalized, when it names a file inside root; else None."""
    candidate = os.path.normpath(candidate).replace(os.sep, '/')
    if candidate.startswith('../') or not (root / candidate).is_file():
        return None
    return candidate


def resolve(root, rel_file, name, quoted, index):
    """(target component, None) or (None, why it cannot be classified) for one include."""
    if quoted:
        for base in (pathlib.PurePosixPath(rel_file).parent, pathlib.PurePosixPath('src')):
            candidate = in_repo(root, str(base / name))
            if candidate:
                return component_of(candidate), None
    found = index.get(name)
    if found:
        targets = {component_of(path) for path in found}
        if len(targets) > 1:
            return None, f'"{name}" matches headers in {", ".join(sorted(targets))}; include it by a unique path'
        return targets.pop(), None
    # An angle include of a src/ path (`<games/GameVM.h>`), which the build finds as it finds a quoted one.
    candidate = None if quoted else in_repo(root, f'src/{name}')
    if candidate and candidate.startswith('src/'):
        return component_of(candidate), None
    target = classify_external(name)
    if target:
        return target, None
    return None, (f'<{name}> is not a header in lib/ or src/ and not in the standard, platform, or SDK tables of '
                  'scripts/check_layers.py; classify it there (and in the spine if it is a new edge)')


def allowed(rel_file, comp, target):
    if target == comp:
        return True
    if (rel_file, target) in FILE_EDGES:
        return True
    if target not in LAYERS[comp]:
        return False
    return target not in ONLY_FROM or rel_file in ONLY_FROM[target]


def describe_allowed(comp):
    return ', '.join(sorted(LAYERS[comp]))


def read_text(path):
    try:
        return path.read_bytes().decode('utf-8', errors='replace')
    except OSError as exc:
        raise SetupError(f'cannot read {path}: {exc}')


def source_files(root, top):
    for dirpath, dirnames, filenames in os.walk(root / top):
        dirnames.sort()
        for name in sorted(filenames):
            path = pathlib.Path(dirpath) / name
            if path.suffix in SOURCE_SUFFIXES:
                yield path.relative_to(root).as_posix(), path


def is_game_or_unscanned(rel):
    return any(rel == top or rel.startswith(top + '/') for top in (*COMPONENTS, *UNSCANNED))


def includes(text):
    """(line number, INCLUDE_TARGET match or None for a computed include, the line) for each include line."""
    for number, line in enumerate(text.splitlines(), start=1):
        match = INCLUDE.match(line)
        if match:
            yield number, INCLUDE_TARGET.match(match.group(1)), line


def target_name(target):
    """(name, quoted) of an INCLUDE_TARGET match."""
    quoted = target.group(3) is not None
    return (target.group(3) if quoted else target.group(2)), quoted


def blank(text, literals):
    """text with each comment blanked (its line breaks kept), and each literal too when `literals`."""

    def replace(match):
        found = match.group(0)
        if found.startswith(('"', "'")):
            return '""' if literals else found
        return re.sub(r'[^\n]', ' ', found)

    return COMMENT_OR_LITERAL.sub(replace, text)


def screen_name_problems(rel, text):
    """path:line problems for the word GameScript in a Screens file, outside comments and literals."""
    problems = []
    for number, line in enumerate(blank(text, literals=True).splitlines(), start=1):
        if GAME_SCRIPT_WORD.search(line):
            problems.append(f'{rel}:{number}: Screens name lib/GameScript ({line.strip()}); reach it only through '
                            'src/games, without naming GameScript')
    return problems


def reexport_problems(rel, text):
    """path:line problems for a global-scope using or namespace alias into GameScript in a src/games header."""
    code = PREPROCESSOR_LINE.sub(lambda m: ' ' * len(m.group(0)), blank(text, literals=True))
    problems = []
    depth = 0
    start = 0
    for index, char in enumerate(code):
        if char not in '{};':
            continue
        if char == '{':
            depth += 1
        elif char == '}':
            depth = max(depth - 1, 0)
        elif depth == 0:
            statement = code[start:index]
            if REEXPORT_START.match(statement) and GAME_SCRIPT_WORD.search(statement):
                first = start + len(statement) - len(statement.lstrip())
                number = code.count('\n', 0, first) + 1
                problems.append(f'{rel}:{number}: src/games header re-exports lib/GameScript at global scope '
                                f'({" ".join(statement.split())};); name the GameScript type where it is used')
        if depth == 0:
            start = index + 1
    return problems


def upstream_problems(root, index):
    """path:line problems for a source file outside the game folders that includes lib/GameScript or lib/lua, or a
    game component its UPSTREAM_EDGES entry does not give it."""
    problems = []
    for top in ('src', 'lib'):
        for rel, path in source_files(root, top):
            if is_game_or_unscanned(rel):
                continue
            for number, target, _ in includes(read_text(path)):
                if target is None:
                    continue
                name, quoted = target_name(target)
                reached, _ = resolve(root, rel, name, quoted, index)
                if reached in GAME_SCRIPT_OR_LUA:
                    problems.append(f'{rel}:{number}: upstream code may not include {target.group(1)} ({reached}); '
                                    'Screens may include it, so it would launder the edge; go through src/games')
                elif reached in COMPONENTS and reached not in UPSTREAM_EDGES.get(rel, ()):
                    problems.append(f'{rel}:{number}: upstream code may not include {target.group(1)} ({reached}); '
                                    'an upstream file includes game code only as its ledger row allows (UPSTREAM_EDGES '
                                    'in scripts/check_layers.py, the spine\'s "Upstream hooks" row)')
    return problems


def check(root):
    root = pathlib.Path(root).resolve()
    missing = [comp for comp in COMPONENTS if not (root / comp).is_dir()]
    if missing:
        raise SetupError(f'{root} is not a crosshatch-player tree: no {", ".join(missing)}')
    index = lib_index(root)
    problems = []
    files = edges = 0
    for comp in COMPONENTS:
        for rel, path in source_files(root, comp):
            files += 1
            text = read_text(path)
            for number, target_match, line in includes(blank(text, literals=False)):
                if not target_match:
                    problems.append(f'{rel}:{number}: cannot check a computed include ({line.strip()})')
                    continue
                edges += 1
                name, quoted = target_name(target_match)
                target, why = resolve(root, rel, name, quoted, index)
                if target is None:
                    problems.append(f'{rel}:{number}: {why}')
                elif not allowed(rel, comp, target):
                    problems.append(f'{rel}:{number}: {comp} may not include {target_match.group(1)} ({target}); '
                                    f'the spine\'s layer table allows {comp}: {describe_allowed(comp)}')
            if comp == SCREENS:
                problems.extend(screen_name_problems(rel, text))
            if comp == ADAPTERS and path.suffix in HEADER_SUFFIXES:
                problems.extend(reexport_problems(rel, text))
    problems.extend(upstream_problems(root, index))
    if problems:
        for problem in problems:
            print(problem)
        fork_common.write_step_summary(f'## {SUMMARY_HEADING}\n\n' + ''.join(f'- `{p}`\n' for p in problems))
        raise Failure(f'{len(problems)} problem(s) against the spine\'s layer table; fix the code, or change the '
                      'spine and LAYERS in scripts/check_layers.py together')
    print(f'{edges} include edges in {files} game files follow the spine\'s layer table, and other source files '
          'include game code only as their ledger rows allow; passed.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', default=str(pathlib.Path(__file__).resolve().parent.parent),
                        help='the repository root (default: the one holding this script)')
    args = parser.parse_args(argv)
    return fork_common.exit_code(lambda: check(args.root), summary_heading=SUMMARY_HEADING)


if __name__ == '__main__':
    sys.exit(main())

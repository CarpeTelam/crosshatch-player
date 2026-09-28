#!/usr/bin/env python3
"""
Check that the game code's #include edges follow the spine's layer table (retro AI-5).

The layer table in _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/
ARCHITECTURE-SPINE.md (Design Paradigm, and the diagram under Invariants & Rules) says what each game component may
depend on. TABLE below holds that table in its own terms (SPINE_TERMS says what each term names), BEYOND_TABLE the
edges the check allows beyond it, each with its reason, and LAYERS the two together. check_layers_test.py's SpineTest
parses the spine's table (its layer rows and its Upstream hooks row) and fails when the components a row reaches differ
from TABLE's or UPSTREAM_EDGES's row, or when the row uses a term SPINE_TERMS does not know. It compares components,
not terms: dropping one of two terms that name the same component (HAL / Storage, ESP-NOW / mbedTLS, UiListActivity /
UiAppHost) changes nothing it can see. It does not compare the table's parenthetical file scopes, ONLY_FROM,
FILE_EDGES, the Engine row, the diagram, or BEYOND_TABLE; those are reviewed by hand, each with its reason here.
Every #include in lib/GameCore, lib/GameIcons, lib/GameScript, src/games,
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
  - fails such an include that UPSTREAM_EDGES allows but that is not in the FREEINK_CAP_GAMES branch of an #if,
    #ifdef, #elif, or #elifdef (AD-2: every include of game code in an upstream file is guarded). The branch's
    condition is FREEINK_CAP_GAMES, defined(FREEINK_CAP_GAMES), or FREEINK_CAP_GAMES == 1, alone or joined to others
    by && and with no ||; an #else, an #ifndef or #elifndef, a negation, or any other spelling does not count, so an
    unusual guard fails visibly instead of passing;
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

When the spine's table changes, change TABLE (and SPINE_TERMS, FILE_EDGES, ONLY_FROM, UPSTREAM_EDGES) in the same
commit; check_layers_test.py fails until they agree.
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

# The spine's layer table, where it lives in the repository (tracked, so CI's checkout has it).
SPINE_PATH = ('_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/'
              'ARCHITECTURE-SPINE.md')

# What each term of the spine's table names, as the check spells it. A repository path in the table (`lib/Utf8`,
# `src/fontIds.h`, `src/games/`) names itself and is not listed. check_layers_test.py parses the table through this map
# and fails on a term it does not know.
SPINE_TERMS = {
    'C++ standard library': STD,
    'nothing': None,  # the Icon data row: no edge at all
    'GameCore': 'lib/GameCore',
    'GameScript': 'lib/GameScript',
    'GameIcons': 'lib/GameIcons',
    'HAL': 'lib/hal',
    'Storage': 'lib/hal',
    'ZipFile': 'lib/ZipFile',
    'PngToBmpConverter': 'lib/PngToBmpConverter',
    'ESP-NOW': RADIO_CRYPTO,
    'mbedTLS': RADIO_CRYPTO,
    'the SDK\'s FreeInkUICore.h': 'sdk:FreeInkUICore.h',
    'SecureHttpClient in ForkReleaseProbe only': 'sdk:SecureHttpClient.h',  # and ONLY_FROM below
    'GfxRenderer': 'lib/GfxRenderer',
    'UiListActivity': UPSTREAM_SRC,
    'UiAppHost': UPSTREAM_SRC,
}

# The spine's layer table (ARCHITECTURE-SPINE.md, Design Paradigm), in its own terms through SPINE_TERMS: one row per
# game component, what each may include besides its own headers. check_layers_test.py compares it with the spine.
TABLE = {
    # Domain: C++ standard library, lib/Memory, lib/JsonParser.
    'lib/GameCore': {STD, 'lib/Memory', 'lib/JsonParser'},
    # Icon data: nothing (generated data only).
    'lib/GameIcons': set(),
    # Script adapter: GameCore, GameIcons (names), lib/lua, lib/Utf8 (TextMetrics).
    'lib/GameScript': {'lib/GameCore', 'lib/GameIcons', 'lib/lua', 'lib/Utf8'},
    # Device adapters: GameCore, GameScript, GameIcons, HAL and Storage (lib/hal), ZipFile, PngToBmpConverter, ESP-NOW
    # and mbedTLS (radio and crypto); lib/Utf8; lib/EpdFont and src/fontIds.h (FrameReplay); the SDK's FreeInkUICore.h
    # (GameTouch.h); SecureHttpClient (ForkReleaseProbe only, ONLY_FROM).
    ADAPTERS: {
        'lib/GameCore', 'lib/GameScript', 'lib/GameIcons', 'lib/hal', 'lib/ZipFile', 'lib/PngToBmpConverter',
        RADIO_CRYPTO, 'lib/Utf8', 'lib/EpdFont', FONT_IDS, 'sdk:FreeInkUICore.h', 'sdk:SecureHttpClient.h',
    },
    # Screens: src/games, GameCore, GfxRenderer, UiListActivity / UiAppHost (upstream's screen infrastructure, src/
    # outside the game folders). Never lib/GameScript or lib/lua: those only through src/games.
    SCREENS: {ADAPTERS, 'lib/GameCore', 'lib/GfxRenderer', UPSTREAM_SRC},
}

# Edges BEYOND_TABLE adds to the spine's table, each a convention rather than a design decision:
#   - lib/Logging and lib/Memory for src/games and Screens: AGENTS.md makes device code log with LOG_* and allocate
#     with makeUniqueNoThrow;
#   - lib/I18n for Screens: AGENTS.md puts user-facing text through tr();
#   - lua.hpp for src/games/GamesBuildAnchor.cpp only (FILE_EDGES): AD-2's build anchor.
CONVENTIONS = {'lib/Logging', 'lib/Memory'}

# What each component may include beyond TABLE's row, with the reason for each.
BEYOND_TABLE = {
    # The standard integer types of its generated data.
    'lib/GameIcons': {STD},
    # The C++ standard library, which the Domain row names and the adapter needs as much.
    'lib/GameScript': {STD},
    # The standard library; the platform (Arduino, ESP-IDF, FreeRTOS) that ESP-NOW and mbedTLS run on; GfxRenderer from
    # the diagram's upstream node; the conventions.
    ADAPTERS: {STD, PLATFORM, 'lib/GfxRenderer', *CONVENTIONS},
    # The standard library; the platform without the radio and crypto the table gives only to src/games; lib/I18n for
    # tr(); the diagram's upstream node (HAL, Storage, ZipFile, PngToBmpConverter); the conventions.
    SCREENS: {STD, PLATFORM, 'lib/I18n', 'lib/hal', 'lib/ZipFile', 'lib/PngToBmpConverter', *CONVENTIONS},
}

# What each game component may include besides its own headers: the spine's row and the additions above.
LAYERS = {comp: TABLE[comp] | BEYOND_TABLE.get(comp, set()) for comp in TABLE}

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
CONDITIONAL = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|elifdef|elifndef|else|endif)\b(.*)$')
GAMES = re.escape(fork_common.GAMES_MACRO)
# One &&-joined part of an #if or #elif condition that requires the games flag.
GAMES_CONJUNCT = re.compile(rf'(?:{GAMES}(?:\s*==\s*1)?|defined\s*\(\s*{GAMES}\s*\)|defined\s+{GAMES})')
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


def games_branch(kind, condition):
    """True when the branch #<kind> <condition> opens is compiled only with the games flag on (GAMES_CONJUNCT)."""
    condition = condition.strip()
    if kind in ('ifdef', 'elifdef'):
        return condition == fork_common.GAMES_MACRO
    if kind not in ('if', 'elif') or '||' in condition:
        return False
    return any(GAMES_CONJUNCT.fullmatch(part.strip()) for part in condition.split('&&'))


def games_guarded_lines(text):
    """The line numbers of text inside the games branch of an #if, #ifdef, #elif, or #elifdef, at any depth
    (comments do not count as directives)."""
    guarded = set()
    branches = []  # one per open conditional: whether its current branch is a games branch
    for number, line in enumerate(blank(text, literals=False).splitlines(), start=1):
        match = CONDITIONAL.match(line)
        if not match:
            if any(branches):
                guarded.add(number)
            continue
        kind, rest = match.groups()
        if kind in ('if', 'ifdef', 'ifndef'):
            branches.append(games_branch(kind, rest))
        elif branches and kind in ('elif', 'elifdef', 'elifndef'):
            branches[-1] = games_branch(kind, rest)
        elif branches and kind == 'else':
            branches[-1] = False
        elif branches and kind == 'endif':
            branches.pop()
    return guarded


def upstream_problems(root, index):
    """path:line problems for a source file outside the game folders that includes lib/GameScript or lib/lua, or a
    game component its UPSTREAM_EDGES entry does not give it, or one it does give it outside a games branch."""
    problems = []
    for top in ('src', 'lib'):
        for rel, path in source_files(root, top):
            if is_game_or_unscanned(rel):
                continue
            text = read_text(path)
            guarded = None  # computed for the few files that include game code
            for number, target, _ in includes(text):
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
                elif reached in COMPONENTS:
                    if guarded is None:
                        guarded = games_guarded_lines(text)
                    if number not in guarded:
                        problems.append(f'{rel}:{number}: upstream code includes {target.group(1)} ({reached}) '
                                        f'outside an #if {fork_common.GAMES_MACRO} branch; AD-2 and its ledger row '
                                        'guard every upstream include of game code')
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
                      'spine and TABLE (or BEYOND_TABLE) in scripts/check_layers.py together; '
                      'scripts/check_layers_test.py compares TABLE with the spine')
    print(f'{edges} include edges in {files} game files follow the spine\'s layer table, and other source files '
          f'include game code only as their ledger rows allow, inside #if {fork_common.GAMES_MACRO}; passed.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', default=str(pathlib.Path(__file__).resolve().parent.parent),
                        help='the repository root (default: the one holding this script)')
    args = parser.parse_args(argv)
    return fork_common.exit_code(lambda: check(args.root), summary_heading=SUMMARY_HEADING)


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""
Shared plumbing for the fork's scripts/ tools (docs/crosshatch/fork-scripts.md has the conventions).

Standard library only. A script in scripts/ imports it by name, which works both when the script is run as
`python3 scripts/<name>.py` (Python puts scripts/ on sys.path) and from its sidecar test (which adds scripts/):

    import fork_common
    from fork_common import SetupError

The API, kept small on purpose; add to it only what two fork scripts would otherwise both write:

  PASS, FAIL, COULD_NOT_RUN   the exit contract: 0 passed, 1 a rule is broken, 2 the check could not run
  Failure                     raise for a broken rule (exit 1)
  SetupError                  raise when the check could not run: missing file or ref, failed tool (exit 2)
  exit_code(step, summary_heading=None)
                              call step() and map it to the exit contract; a script's main() returns this
  git(*args, cwd=None, ok_codes=(0,))
                              run git, return (exit code, stdout bytes); any other exit code is a SetupError
  git_text(*args, cwd=None)   git's stdout as stripped text; any non-zero exit code is a SetupError
  file_at(ref, path, cwd=None)
                              the bytes of path at ref, or None when ref has no such file
  write_step_summary(text, path=None)
                              append Markdown to the GitHub Actions job summary; a no-op outside Actions
  GAMES_MACRO                 'FREEINK_CAP_GAMES', the macro name as the compiler sees it
  GAMES_BUILD_FLAG            '-DFREEINK_CAP_GAMES=1', the flag as platformio.ini spells it
  API_LEVEL_HEADER            'lib/GameCore/ApiLevel.h', the game API level's header (spine AD-19)
  api_list_path(level)        'docs/crosshatch/api-level-<level>.txt', that level's surface list
  ApiLevel                    (level, min_level, frozen) as read from the header
  parse_api_level(text)       read an ApiLevel from the header's text; a missing, repeated, or malformed define is a
                              SetupError

Run `python3 scripts/fork_common_test.py` for its own tests.
"""

import collections
import os
import re
import subprocess
import sys

PASS = 0
FAIL = 1
COULD_NOT_RUN = 2

GAMES_MACRO = 'FREEINK_CAP_GAMES'
GAMES_BUILD_FLAG = f'-D{GAMES_MACRO}=1'

API_LEVEL_HEADER = 'lib/GameCore/ApiLevel.h'
# The header keeps each define on one line as `#define NAME VALUE` (test/game_core/ApiLevelTest.cpp pins the shape).
API_DEFINE = re.compile(r'#define (API_[A-Z_]+) (\S+)')
API_INTEGER = re.compile(r'[1-9][0-9]*')

ApiLevel = collections.namedtuple('ApiLevel', 'level min_level frozen')


class Failure(Exception):
    """A rule the script checks is broken (exit 1)."""


class SetupError(Exception):
    """The script could not run: a missing input, a failed tool, or a wrong environment (exit 2)."""


def git(*args, cwd=None, ok_codes=(0,)):
    """Run git (as `git -C cwd` when cwd is given) and return (exit code, stdout bytes).

    An exit code outside ok_codes, or git missing from PATH, raises SetupError with git's stderr.
    """
    command = ['git', *(['-C', str(cwd)] if cwd is not None else []), *args]
    try:
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError as exc:
        raise SetupError(f'cannot run git: {exc}')
    if proc.returncode not in ok_codes:
        err = proc.stderr.decode(errors='replace').strip()
        raise SetupError(f'git {" ".join(args)} failed ({proc.returncode}): {err}')
    return proc.returncode, proc.stdout


def git_text(*args, cwd=None):
    """git's stdout decoded and stripped; any non-zero exit code raises SetupError."""
    return git(*args, cwd=cwd)[1].decode().strip()


def file_at(ref, path, cwd=None):
    """The bytes of path at ref, or None when ref has no such file; an unknown ref raises SetupError."""
    if not git_text('ls-tree', '--name-only', ref, '--', path, cwd=cwd):
        return None
    return git('show', f'{ref}:{path}', cwd=cwd)[1]


def write_step_summary(text, path=None):
    """Append text to path, or else to $GITHUB_STEP_SUMMARY; do nothing when neither is set."""
    path = path or os.environ.get('GITHUB_STEP_SUMMARY')
    if path:
        with open(path, 'a', encoding='utf-8') as summary:
            summary.write(text)


def api_list_path(level):
    """The surface list of one API level, relative to the repository root."""
    return f'docs/crosshatch/api-level-{level}.txt'


def parse_api_level(text):
    """ApiLevel(level, min_level, frozen) from the text of ApiLevel.h.

    Each of API_LEVEL, API_MIN_LEVEL (positive integers, min <= level) and API_LEVEL_FROZEN (true or false) must be
    defined exactly once; anything else is a SetupError, so a reformatted header never reads as a different level.
    """
    values = {}
    for line in text.splitlines():
        match = API_DEFINE.fullmatch(line.strip())
        if not match:
            continue
        name, value = match.groups()
        if name in values:
            raise SetupError(f'{API_LEVEL_HEADER} defines {name} more than once')
        values[name] = value
    missing = [name for name in ('API_LEVEL', 'API_MIN_LEVEL', 'API_LEVEL_FROZEN') if name not in values]
    if missing:
        raise SetupError(f'{API_LEVEL_HEADER} has no one-line #define for {", ".join(missing)}')
    for name in ('API_LEVEL', 'API_MIN_LEVEL'):
        if not API_INTEGER.fullmatch(values[name]):
            raise SetupError(f'{API_LEVEL_HEADER}: {name} is {values[name]!r}, not a positive integer')
    if values['API_LEVEL_FROZEN'] not in ('true', 'false'):
        raise SetupError(f'{API_LEVEL_HEADER}: API_LEVEL_FROZEN is {values["API_LEVEL_FROZEN"]!r}, not true or false')
    level, min_level = int(values['API_LEVEL']), int(values['API_MIN_LEVEL'])
    if min_level > level:
        raise SetupError(f'{API_LEVEL_HEADER}: API_MIN_LEVEL {min_level} is above API_LEVEL {level}')
    return ApiLevel(level, min_level, values['API_LEVEL_FROZEN'] == 'true')


def exit_code(step, summary_heading=None):
    """Call step() and return the exit code for it.

    A returned None is PASS and a returned int is passed through, so a step may return FAIL itself. Failure is FAIL
    and SetupError is COULD_NOT_RUN, each printed as `error: <message>` on stderr. With summary_heading, a SetupError
    is also written to the job summary, so a check that could not run does not look like an empty report. Any other
    exception propagates.
    """
    try:
        result = step()
    except Failure as exc:
        print(f'error: {exc}', file=sys.stderr)
        return FAIL
    except SetupError as exc:
        print(f'error: {exc}', file=sys.stderr)
        if summary_heading:
            write_step_summary(f'## {summary_heading}\n\nThe check could not run: {exc}\n')
        return COULD_NOT_RUN
    return PASS if result is None else result

#!/usr/bin/env python3
"""
Check that a ref leaves every frozen game API level unchanged (spine AD-19).

lib/GameCore/ApiLevel.h holds API_LEVEL and API_LEVEL_FROZEN. The flag describes API_LEVEL only and every level below
it is frozen, so the highest frozen level is API_LEVEL when the flag is true, else API_LEVEL - 1. Compared from the
merge-base of the ref and the base ref, the check fails (exit 1) when the ref:
  - changes or deletes docs/crosshatch/api-level-<n>.txt of a level the merge-base has frozen;
  - has a lower highest frozen level than the merge-base: API_LEVEL_FROZEN turned from true to false, API_LEVEL
    lowered, or ApiLevel.h removed.
Opening the next level as a preview (API_LEVEL + 1, flag false) keeps the frozen levels and passes. A merge-base
without ApiLevel.h has nothing frozen. Exit 2 means the check could not run (unknown ref, no merge-base in a shallow
clone, a header that cannot be read).

Usage: python3 scripts/check_api_freeze.py [--ref HEAD] [--base-ref origin/develop]
       python3 scripts/check_api_freeze_test.py      # the script's own tests; need only git
"""

import argparse
import sys

import fork_common
from fork_common import API_LEVEL_HEADER, Failure, SetupError, api_list_path

SUMMARY_HEADING = 'API freeze'


def resolve(ref, hint):
    code, out = fork_common.git('rev-parse', '--verify', '--quiet', f'{ref}^{{commit}}', ok_codes=(0, 1))
    if code != 0:
        raise SetupError(f'cannot resolve {ref}; {hint}')
    return out.decode().strip()


def read_file(commit, path):
    """The bytes of path at commit, or None when the commit has no such file."""
    listed = fork_common.git_text('ls-tree', '--name-only', commit, '--', path)
    if not listed:
        return None
    return fork_common.git('show', f'{commit}:{path}')[1]


def read_level(commit):
    data = read_file(commit, API_LEVEL_HEADER)
    return None if data is None else fork_common.parse_api_level(data.decode('utf-8', errors='replace'))


def frozen_top(level):
    """The highest frozen level: 0 when nothing is frozen."""
    if level is None:
        return 0
    return level.level if level.frozen else level.level - 1


def describe(level):
    if level is None:
        return f'no {API_LEVEL_HEADER}'
    return f'API level {level.level} ({"frozen" if level.frozen else "preview"}), min {level.min_level}'


def check(ref, base_ref):
    head = resolve(ref, 'is it a commit?')
    base_tip = resolve(base_ref, 'fetch it first (the CI job checks out with full history)')
    code, out = fork_common.git('merge-base', head, base_tip, ok_codes=(0, 1))
    if code != 0:
        raise SetupError(f'{ref} and {base_ref} have no merge-base; is this a shallow clone?')
    merge_base = out.decode().strip()

    before, after = read_level(merge_base), read_level(head)
    print(f'Merge-base {merge_base[:12]}: {describe(before)}')
    print(f'{ref} {head[:12]}: {describe(after)}')
    top = frozen_top(before)
    if top == 0:
        print('Nothing is frozen at the merge-base; passed.')
        return

    problems = []
    if frozen_top(after) < top:
        problems.append(
            f'level {top} is frozen at the merge-base but not at {ref} ({describe(after)}); '
            'API_LEVEL_FROZEN never reverts and a frozen level never reopens'
        )
    checked = []
    for n in range(1, top + 1):
        path = api_list_path(n)
        frozen_list = read_file(merge_base, path)
        if frozen_list is None:
            continue
        checked.append(path)
        now = read_file(head, path)
        if now is None:
            problems.append(f'{path} is deleted, but level {n} is frozen')
        elif now != frozen_list:
            problems.append(f'{path} changed, but level {n} is frozen; add to a new level instead')
    if problems:
        raise Failure('\n'.join(problems))
    print(f'Frozen levels 1..{top} unchanged ({", ".join(checked) or "no list files"}); passed.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--ref', default='HEAD', help='the ref to check (default HEAD)')
    parser.add_argument('--base-ref', default='origin/develop', help='the branch it merges into')
    args = parser.parse_args(argv)
    return fork_common.exit_code(lambda: check(args.ref, args.base_ref), summary_heading=SUMMARY_HEADING)


if __name__ == '__main__':
    sys.exit(main())

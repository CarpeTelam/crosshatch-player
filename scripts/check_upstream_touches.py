#!/usr/bin/env python3
"""
Check a ref against the fork's upstream-touch ledger (docs/crosshatch/upstream-touches.md).

Fails (exit 1) when the ref, compared from its merge-base with upstream:
  - changes a path that exists upstream and is in neither the Ledger nor the Allowlist;
  - moves the freeink-sdk submodule pointer;
  - would conflict in a Game path when upstream is merged into it, or upstream has any path under a Game path.
Exit 2 means the check could not run (shallow clone, missing upstream ref, no ledger, old git).

The ledger is read from the checked ref itself, so a local run and the CI job see the same lists.

Usage: python3 scripts/check_upstream_touches.py [--ref HEAD] [--upstream upstream/develop]
"""

import argparse
import fnmatch
import re
import sys

from fork_common import SetupError, exit_code, git, git_text

LEDGER_PATH = 'docs/crosshatch/upstream-touches.md'
SDK_PATH = 'freeink-sdk'
SECTIONS = ('Ledger', 'Allowlist', 'Game paths')
# `git merge-tree --write-tree` (a trial merge that touches no worktree, index, or ref) arrived in git 2.38.
MIN_GIT = (2, 38)
CODE_SPAN = re.compile(r'`([^`]+)`')


def nul_list(data):
    # surrogateescape: a path that is not valid UTF-8 must still be listed, not crash the check.
    return [p.decode(errors='surrogateescape') for p in data.split(b'\0') if p]


def check_git_version():
    text = git_text('--version')
    match = re.search(r'(\d+)\.(\d+)', text)
    if not match or (int(match.group(1)), int(match.group(2))) < MIN_GIT:
        raise SetupError(f'{text} is too old; git {MIN_GIT[0]}.{MIN_GIT[1]} or later is needed')


def resolve(ref, hint):
    code, out = git('rev-parse', '--verify', '--quiet', f'{ref}^{{commit}}', ok_codes=(0, 1))
    if code != 0:
        raise SetupError(f'cannot resolve {ref}; {hint}')
    return out.decode().strip()


def parse_ledger(text):
    """Return {section: [first code span of each table row or bullet]} for the three sections."""
    lists = {name: [] for name in SECTIONS}
    current = None
    in_fence = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith(('```', '~~~')):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if line.startswith('## '):
            heading = line[3:].strip()
            current = heading if heading in lists else None
            continue
        if current is None or not stripped.startswith(('|', '- ', '* ', '+ ')):
            continue
        match = CODE_SPAN.search(stripped)
        if match:
            lists[current].append(match.group(1).strip().rstrip('/'))
    empty = [name for name, entries in lists.items() if not entries]
    if empty:
        raise SetupError(f'{LEDGER_PATH} has no entries under: {", ".join("## " + n for n in empty)}')
    return lists


def is_game_path(path, patterns):
    parts = path.split('/')
    prefixes = ['/'.join(parts[:i]) for i in range(1, len(parts) + 1)]
    return any(fnmatch.fnmatchcase(prefix, pattern) for pattern in patterns for prefix in prefixes)


def gitlink(commit, path):
    """Return the submodule commit recorded at path, or None when the tree has no gitlink there."""
    out = git_text('ls-tree', commit, '--', path)
    fields = out.split()
    return fields[2] if len(fields) >= 3 and fields[1] == 'commit' else None


def run(ref, upstream):
    check_git_version()
    if git_text('rev-parse', '--is-shallow-repository') == 'true':
        raise SetupError('this clone is shallow; run `git fetch --unshallow` (CI: checkout with fetch-depth: 0)')
    head = resolve(ref, 'pass an existing --ref')
    up = resolve(upstream, 'add the upstream remote and run `git fetch upstream develop`')
    code, out = git('merge-base', head, up, ok_codes=(0, 1))
    if code != 0:
        raise SetupError(f'{ref} and {upstream} share no history; fetch full history of both')
    base = out.decode().strip()

    code, ledger_text = git('show', f'{head}:{LEDGER_PATH}', ok_codes=(0, 128))
    if code != 0:
        raise SetupError(f'{LEDGER_PATH} does not exist at {ref}')
    lists = parse_ledger(ledger_text.decode(errors='replace'))
    allowed = set(lists['Ledger']) | set(lists['Allowlist'])

    print(f'Checking {ref} ({head[:12]}) against {upstream} ({up[:12]}), merge-base {base[:12]}')
    failed = False

    game_patterns = lists['Game paths']
    upstream_paths = set(nul_list(git('ls-tree', '-r', '-z', '--name-only', up)[1]))
    # Allowlist entries are not checked: some, like .gitattributes, are fork-added and listed in case upstream adds one.
    for row in lists['Ledger']:
        if row not in upstream_paths:
            print(f'warning: ledger row `{row}` no longer exists in {upstream}; update {LEDGER_PATH}')

    # Game paths are reported by the upstream-overlap check below, which names the real cause.
    changed = nul_list(git('diff', '--no-renames', '--no-ext-diff', '--name-only', '-z', base, head)[1])
    touched = sorted(
        p for p in changed if p in upstream_paths and p != SDK_PATH and not is_game_path(p, game_patterns))
    accepted = [p for p in touched if p in allowed]
    unledgered = [p for p in touched if p not in allowed]
    print(f'Upstream paths changed and ledgered or allowlisted: {", ".join(accepted) or "none"}')
    if unledgered:
        failed = True
        print(f'\nFAIL: {len(unledgered)} upstream path(s) changed that are in neither the Ledger nor the Allowlist')
        print(f'of {LEDGER_PATH}. Revert them, or move the change into a fork-only file:')
        for path in unledgered:
            print(f'  {path}')

    old_sdk, new_sdk = gitlink(base, SDK_PATH), gitlink(head, SDK_PATH)
    if old_sdk != new_sdk:
        failed = True
        print(f'\nFAIL: the {SDK_PATH} pointer moved ({old_sdk} -> {new_sdk}); propose SDK changes upstream instead.')

    # A clean merge would still pull these upstream files into paths the fork owns.
    upstream_game = sorted(p for p in upstream_paths if is_game_path(p, game_patterns))
    if upstream_game:
        failed = True
        print(f'\nFAIL: {upstream} has files under game paths, which must stay fork-only. Move the fork\'s files, or')
        print(f'narrow the Game paths entry in {LEDGER_PATH} if the match is only a shared prefix:')
        for path in upstream_game:
            print(f'  {path}')

    code, out = git('merge-tree', '--write-tree', '--name-only', '-z', '--no-messages', head, up, ok_codes=(0, 1))
    conflicts = sorted(set(nul_list(out)[1:])) if code == 1 else []
    game_conflicts = [p for p in conflicts if is_game_path(p, game_patterns)]
    other_conflicts = [p for p in conflicts if p not in game_conflicts]
    if other_conflicts:
        print(f'\nnote: a merge of {upstream} would conflict outside game paths (resolve when merging upstream):')
        for path in other_conflicts:
            print(f'  {path}')
    if game_conflicts:
        failed = True
        print(f'\nFAIL: a merge of {upstream} would conflict in game path(s); upstream now has these paths too:')
        for path in game_conflicts:
            print(f'  {path}')
    elif not conflicts:
        print(f'Trial merge of {upstream}: clean')

    print('\nResult: ' + ('FAIL' if failed else 'PASS'))
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument('--ref', default='HEAD', help='ref to check (default: HEAD)')
    parser.add_argument('--upstream', default='upstream/develop', help='upstream ref (default: upstream/develop)')
    args = parser.parse_args()
    # Paths that are not valid UTF-8 arrive surrogate-escaped; print them escaped rather than crash.
    sys.stdout.reconfigure(errors='backslashreplace')
    return exit_code(lambda: run(args.ref, args.upstream))


if __name__ == '__main__':
    sys.exit(main())

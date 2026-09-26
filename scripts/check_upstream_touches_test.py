#!/usr/bin/env python3
"""
Tests for check_upstream_touches.py. Standard library and git only:

    python3 scripts/check_upstream_touches_test.py [-v]

The integration cases build a throwaway repository with an "upstream" and a "fork" history in a temp directory
and assert the script's exit code for each rule, so a regression that makes the check always pass is caught.
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / 'check_upstream_touches.py'
sys.path.insert(0, str(HERE))

import check_upstream_touches as cut  # noqa: E402

SDK_A = '1111111111111111111111111111111111111111'
SDK_B = '2222222222222222222222222222222222222222'

FIXTURE_LEDGER = """# Fixture ledger

Intro bullets are ignored:
- `README.md`

## Ledger

| # | Upstream file | Change |
| --- | --- | --- |
| 1 | `platformio.ini` | first line |

## Allowlist

- `AGENTS.md` -- fork's own

## Game paths

- `lib/Game*`
- `games`
- `scripts/pack_game.py`
"""


class ParseLedgerTest(unittest.TestCase):
    def test_first_code_span_of_rows_and_bullets(self):
        lists = cut.parse_ledger(
            '## Ledger\n| 1 | `a.ini` | `not-this` |\n## Allowlist\n- `b.md` -- `not-this`\n## Game paths\n- `g`\n')
        self.assertEqual(lists, {'Ledger': ['a.ini'], 'Allowlist': ['b.md'], 'Game paths': ['g']})

    def test_fences_and_other_sections_are_skipped(self):
        text = ('## Ledger\n| 1 | `a.ini` |\n```\n| 2 | `fenced.ini` |\n```\n~~~\n| 3 | `tilde.ini` |\n~~~\n'
                '## Allowlist\n- `b.md`\n## Notes\n- `other.md`\n## Game paths\n- `g`\n')
        lists = cut.parse_ledger(text)
        self.assertEqual(lists['Ledger'], ['a.ini'])
        self.assertEqual(lists['Allowlist'], ['b.md'])

    def test_bullet_styles_and_trailing_slash(self):
        lists = cut.parse_ledger('## Ledger\n| 1 | `a.ini` |\n## Allowlist\n* `b.md`\n## Game paths\n+ `src/games/`\n')
        self.assertEqual(lists['Allowlist'], ['b.md'])
        self.assertEqual(lists['Game paths'], ['src/games'])

    def test_empty_section_is_a_setup_error(self):
        with self.assertRaises(cut.SetupError):
            cut.parse_ledger('## Ledger\n| 1 | `a.ini` |\n## Allowlist\n## Game paths\n- `g`\n')

    def test_committed_ledger(self):
        lists = cut.parse_ledger((HERE.parent / cut.LEDGER_PATH).read_text())
        self.assertEqual(len(lists['Ledger']), 10)
        self.assertIn('src/network/OtaUpdater.cpp', lists['Ledger'])
        self.assertEqual(
            set(lists['Allowlist']),
            {'AGENTS.md', '.gitattributes', '.gitignore', '.github/PULL_REQUEST_TEMPLATE.md', 'CLAUDE.md'})
        # The check's own files must be fork-only paths, or an upstream merge could rewrite the check.
        for own in ('scripts/check_upstream_touches.py', 'scripts/check_upstream_touches_test.py',
                    '.github/workflows/crosshatch-upstream-ledger.yml', cut.LEDGER_PATH):
            self.assertTrue(cut.is_game_path(own, lists['Game paths']), own)


class IsGamePathTest(unittest.TestCase):
    PATTERNS = ['lib/Game*', 'lib/lua', 'scripts/pack_game.py', '.github/workflows/crosshatch-*.yml']

    def test_matches(self):
        for path in ('lib/GameCore/GameCore.h', 'lib/lua/lapi.c', 'lib/lua', 'scripts/pack_game.py',
                     '.github/workflows/crosshatch-size.yml'):
            self.assertTrue(cut.is_game_path(path, self.PATTERNS), path)

    def test_rejects(self):
        for path in ('src/main.cpp', 'lib/luax/a.c', 'scripts/pack_game.pyc', '.github/workflows/ci.yml'):
            self.assertFalse(cut.is_game_path(path, self.PATTERNS), path)


class CheckScriptTest(unittest.TestCase):
    """Runs the script against a throwaway repository: branch `up` plays upstream/develop, `fork` the fork."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='upstream-touches-test-')
        cls.repo = os.path.join(cls.tmp, 'repo')
        cls.env = dict(os.environ, GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull, GIT_AUTHOR_NAME='t',
                       GIT_AUTHOR_EMAIL='t@example.invalid', GIT_COMMITTER_NAME='t',
                       GIT_COMMITTER_EMAIL='t@example.invalid')
        os.makedirs(cls.repo)
        cls.git('init', '-q')
        cls.git('checkout', '-q', '-b', 'main')
        cls.commit(None, {'README.md': 'readme\n', 'platformio.ini': 'base\nrest\n', 'AGENTS.md': 'agents\n'},
                   sdk=SDK_A)
        cls.commit('up', {'src/new.cpp': 'upstream progress\n'}, start='main')
        cls.commit('fork', {cut.LEDGER_PATH: FIXTURE_LEDGER, 'platformio.ini': 'fork\nrest\n',
                            'AGENTS.md': 'fork agents\n', 'lib/GameCore/a.h': '#pragma once\n'}, start='main')
        cls.commit('unledgered', {'README.md': 'fork readme\n'}, start='fork')
        cls.commit('sdk-move', {}, start='fork', sdk=SDK_B)
        cls.commit('fork-pack', {'scripts/pack_game.py': 'fork\n'}, start='fork')
        cls.commit('up-pack', {'scripts/pack_game.py': 'upstream\n'}, start='up')
        cls.commit('up-games', {'games/x/main.lua': 'upstream\n'}, start='up')
        cls.commit('up-ini', {'platformio.ini': 'upstream\nrest\n'}, start='up')
        cls.commit('up-rm-ini', {}, start='up', remove=['platformio.ini'])
        cls.commit('fork-rm', {}, start='fork', remove=['README.md'])
        cls.commit('fork-rename', {'docs/README.md': 'readme\n'}, start='fork', remove=['README.md'])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    @classmethod
    def git(cls, *args, cwd=None):
        subprocess.run(['git', *args], cwd=cwd or cls.repo, env=cls.env, check=True, stdout=subprocess.DEVNULL)

    @classmethod
    def commit(cls, branch, files, start=None, sdk=None, remove=()):
        if branch:
            cls.git('checkout', '-q', '-b', branch, start)
        for path in remove:
            cls.git('rm', '-q', '--', path)
        for path, content in files.items():
            full = os.path.join(cls.repo, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'w') as f:
                f.write(content)
            cls.git('add', '--', path)
        if sdk:
            cls.git('update-index', '--add', '--cacheinfo', f'160000,{sdk},{cut.SDK_PATH}')
        cls.git('commit', '-q', '-m', branch or 'base')

    def check(self, ref, upstream='up', cwd=None):
        proc = subprocess.run([sys.executable, str(SCRIPT), '--ref', ref, '--upstream', upstream],
                              cwd=cwd or self.repo, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return proc.returncode, proc.stdout.decode()

    def test_ledgered_allowlisted_and_game_paths_pass(self):
        code, out = self.check('fork')
        self.assertEqual(code, 0, out)
        self.assertIn('AGENTS.md, platformio.ini', out)
        self.assertNotIn('warning:', out)

    def test_unledgered_upstream_path_fails(self):
        code, out = self.check('unledgered')
        self.assertEqual(code, 1, out)
        self.assertIn('  README.md', out)

    def test_deleting_or_renaming_an_upstream_file_fails(self):
        for ref in ('fork-rm', 'fork-rename'):
            code, out = self.check(ref)
            self.assertEqual(code, 1, out)
            self.assertIn('  README.md', out)

    def test_ledger_row_missing_upstream_only_warns(self):
        code, out = self.check('fork', upstream='up-rm-ini')
        self.assertEqual(code, 0, out)
        self.assertIn('warning: ledger row `platformio.ini` no longer exists', out)
        self.assertNotIn('neither the Ledger nor the Allowlist', out)

    def test_sdk_pointer_move_fails(self):
        code, out = self.check('sdk-move')
        self.assertEqual(code, 1, out)
        self.assertIn(f'{SDK_A} -> {SDK_B}', out)

    def test_game_path_conflict_fails(self):
        code, out = self.check('fork-pack', upstream='up-pack')
        self.assertEqual(code, 1, out)
        self.assertIn('would conflict in game path(s)', out)
        self.assertNotIn('neither the Ledger nor the Allowlist', out)

    def test_upstream_file_under_game_path_fails(self):
        code, out = self.check('fork', upstream='up-games')
        self.assertEqual(code, 1, out)
        self.assertIn('  games/x/main.lua', out)

    def test_conflict_outside_game_paths_is_only_a_note(self):
        code, out = self.check('fork', upstream='up-ini')
        self.assertEqual(code, 0, out)
        self.assertIn('conflict outside game paths', out)
        self.assertIn('  platformio.ini', out)

    def test_missing_ledger_is_a_setup_error(self):
        self.assertEqual(self.check('main')[0], 2)

    def test_missing_upstream_is_a_setup_error(self):
        self.assertEqual(self.check('fork', upstream='no-such-ref')[0], 2)

    def test_shallow_clone_is_a_setup_error(self):
        shallow = os.path.join(self.tmp, 'shallow')
        self.git('clone', '-q', '--depth', '1', '--branch', 'fork', f'file://{self.repo}', shallow, cwd=self.tmp)
        code, out = self.check('HEAD', cwd=shallow)
        self.assertEqual(code, 2, out)
        self.assertIn('shallow', out)


if __name__ == '__main__':
    unittest.main()

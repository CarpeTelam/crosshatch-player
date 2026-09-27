#!/usr/bin/env python3
"""
Tests for check_api_freeze.py. Standard library and git only:

    python3 scripts/check_api_freeze_test.py [-v]

Each case checks a branch of a throwaway repository against the branch it forked from, and asserts the script's exit
code, so a regression that makes the check always pass is caught.
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / 'check_api_freeze.py'
sys.path.insert(0, str(HERE))

import check_api_freeze as caf  # noqa: E402

HEADER = caf.API_LEVEL_HEADER
LIST_1 = caf.api_list_path(1)
LIST_2 = caf.api_list_path(2)
LIST_1_TEXT = '# level 1\nfn ch.gfx.clear()\nlimit state_bytes 1400\n'


def header(level, frozen, min_level=1):
    return (f'#pragma once\n// comment\n#define API_LEVEL {level}\n#define API_MIN_LEVEL {min_level}\n'
            f'#define API_LEVEL_FROZEN {"true" if frozen else "false"}\n#define API_SURFACE_CRC 0x00000000\n')


class FrozenTopTest(unittest.TestCase):
    def test_frozen_top(self):
        self.assertEqual(caf.frozen_top(None), 0)
        self.assertEqual(caf.frozen_top(caf.fork_common.ApiLevel(1, 1, False)), 0)
        self.assertEqual(caf.frozen_top(caf.fork_common.ApiLevel(1, 1, True)), 1)
        self.assertEqual(caf.frozen_top(caf.fork_common.ApiLevel(3, 1, False)), 2)


class CheckScriptTest(unittest.TestCase):
    """Branches of a throwaway repository; each head is checked against the base it forked from."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='api-freeze-test-')
        cls.repo = os.path.join(cls.tmp, 'repo')
        cls.env = dict(os.environ, GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull, GIT_AUTHOR_NAME='t',
                       GIT_AUTHOR_EMAIL='t@example.invalid', GIT_COMMITTER_NAME='t',
                       GIT_COMMITTER_EMAIL='t@example.invalid')
        cls.env.pop('GITHUB_STEP_SUMMARY', None)
        os.makedirs(cls.repo)
        cls.git('init', '-q')
        cls.git('checkout', '-q', '-b', 'nolevel')
        cls.commit(None, {'README.md': 'readme\n'})
        cls.commit('preview', {HEADER: header(1, False), LIST_1: LIST_1_TEXT}, start='nolevel')
        cls.commit('preview-change', {LIST_1: LIST_1_TEXT + 'fn ch.gfx.line(x1, y1, x2, y2)\n'}, start='preview')
        cls.commit('frozen', {HEADER: header(1, True)}, start='preview')
        cls.commit('frozen-list-change', {LIST_1: LIST_1_TEXT + 'enum color gray\n'}, start='frozen')
        cls.commit('frozen-comment-change', {LIST_1: '# level 1, reworded\n' + LIST_1_TEXT[10:]}, start='frozen')
        cls.commit('frozen-list-delete', {}, start='frozen', remove=[LIST_1])
        cls.commit('unfreeze', {HEADER: header(1, False)}, start='frozen')
        cls.commit('header-removed', {}, start='frozen', remove=[HEADER])
        cls.commit('next-level', {HEADER: header(2, False), LIST_2: 'fn ch.vibrate()\n'}, start='frozen')
        cls.commit('next-level-and-change', {HEADER: header(2, False), LIST_2: 'fn ch.vibrate()\n',
                                             LIST_1: LIST_1_TEXT + 'enum color gray\n'}, start='frozen')
        cls.commit('level-2-preview', {HEADER: header(2, False), LIST_2: 'fn ch.vibrate()\n'}, start='preview')
        cls.commit('level-2-edit-1', {LIST_1: LIST_1_TEXT + 'enum color gray\n'}, start='level-2-preview')
        cls.commit('level-2-edit-2', {LIST_2: 'fn ch.vibrate(ms)\n'}, start='level-2-preview')
        cls.commit('level-2-lowered', {HEADER: header(1, False)}, start='level-2-preview')
        cls.commit('bad-header', {HEADER: '#define API_LEVEL 1\n'}, start='frozen')
        cls.git('checkout', '-q', '--orphan', 'unrelated')
        cls.commit(None, {'other.txt': 'x\n'})

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    @classmethod
    def git(cls, *args):
        subprocess.run(['git', *args], cwd=cls.repo, env=cls.env, check=True, stdout=subprocess.DEVNULL)

    @classmethod
    def commit(cls, branch, files, start=None, remove=()):
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
        cls.git('commit', '-q', '-m', branch or 'base')

    def check(self, ref, base, env=None):
        proc = subprocess.run([sys.executable, str(SCRIPT), '--ref', ref, '--base-ref', base], cwd=self.repo,
                              env=env or self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return proc.returncode, proc.stdout.decode()

    def assert_code(self, ref, base, code, fragment=None):
        got, out = self.check(ref, base)
        self.assertEqual(got, code, out)
        if fragment:
            self.assertIn(fragment, out)

    def test_nothing_frozen_at_the_merge_base_passes(self):
        self.assert_code('preview', 'nolevel', 0, 'Nothing is frozen')
        self.assert_code('frozen', 'nolevel', 0)

    def test_preview_change_passes(self):
        self.assert_code('preview-change', 'preview', 0, 'Nothing is frozen')
        # Freezing a level in the same PR that last changes its list is fine: the merge-base has it as a preview.
        self.assert_code('frozen', 'preview', 0)

    def test_frozen_list_change_fails(self):
        self.assert_code('frozen-list-change', 'frozen', 1, f'{LIST_1} changed')
        self.assert_code('frozen-comment-change', 'frozen', 1, f'{LIST_1} changed')
        self.assert_code('frozen-list-delete', 'frozen', 1, f'{LIST_1} is deleted')

    def test_unchanged_frozen_level_passes(self):
        self.assert_code('frozen', 'frozen', 0, 'Frozen levels 1..1 unchanged')

    def test_frozen_flag_turned_back_fails(self):
        self.assert_code('unfreeze', 'frozen', 1, 'API_LEVEL_FROZEN never reverts')
        self.assert_code('header-removed', 'frozen', 1, 'never reverts')

    def test_opening_the_next_level_passes(self):
        self.assert_code('next-level', 'frozen', 0, 'Frozen levels 1..1 unchanged')
        self.assert_code('next-level-and-change', 'frozen', 1, f'{LIST_1} changed')

    def test_levels_below_api_level_are_frozen(self):
        self.assert_code('level-2-edit-1', 'level-2-preview', 1, f'{LIST_1} changed')
        self.assert_code('level-2-edit-2', 'level-2-preview', 0)
        self.assert_code('level-2-lowered', 'level-2-preview', 1, 'never reverts')

    def test_could_not_run(self):
        self.assert_code('nope', 'frozen', 2, 'cannot resolve nope')
        self.assert_code('frozen', 'origin/develop', 2, 'cannot resolve origin/develop')
        self.assert_code('unrelated', 'frozen', 2, 'no merge-base')
        self.assert_code('bad-header', 'frozen', 2, 'API_MIN_LEVEL')

    def test_could_not_run_goes_to_the_step_summary(self):
        summary = pathlib.Path(self.tmp) / 'summary.md'
        code, _ = self.check('nope', 'frozen', env=dict(self.env, GITHUB_STEP_SUMMARY=str(summary)))
        self.assertEqual(code, 2)
        self.assertIn('## API freeze', summary.read_text())


if __name__ == '__main__':
    unittest.main()

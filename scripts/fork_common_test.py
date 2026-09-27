#!/usr/bin/env python3
"""Tests for scripts/fork_common.py. Standard library only; needs git on PATH. Run: python3 scripts/fork_common_test.py"""

import contextlib
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fork_common as fc  # noqa: E402


def run_quietly(step, **kwargs):
    """(exit code, stderr text) of fc.exit_code(step)."""
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        code = fc.exit_code(step, **kwargs)
    return code, err.getvalue()


def raise_(exc):
    def step():
        raise exc

    return step


class ExitCodeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.summary = pathlib.Path(self.tmp.name) / 'summary.md'
        patcher = mock.patch.dict(os.environ, {'GITHUB_STEP_SUMMARY': str(self.summary)})
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        self.tmp.cleanup()

    def test_contract_values(self):
        self.assertEqual((fc.PASS, fc.FAIL, fc.COULD_NOT_RUN), (0, 1, 2))

    def test_none_passes_and_an_int_is_passed_through(self):
        self.assertEqual(run_quietly(lambda: None), (0, ''))
        self.assertEqual(run_quietly(lambda: 0), (0, ''))
        self.assertEqual(run_quietly(lambda: fc.FAIL), (1, ''))

    def test_failure_is_fail(self):
        code, err = run_quietly(raise_(fc.Failure('rule broken')))
        self.assertEqual((code, err), (1, 'error: rule broken\n'))

    def test_setup_error_is_could_not_run(self):
        code, err = run_quietly(raise_(fc.SetupError('no ref')))
        self.assertEqual((code, err), (2, 'error: no ref\n'))
        self.assertFalse(self.summary.exists())

    def test_setup_error_goes_to_the_summary_with_a_heading(self):
        code, _ = run_quietly(raise_(fc.SetupError('no ref')), summary_heading='My check')
        self.assertEqual(code, 2)
        self.assertEqual(self.summary.read_text(), '## My check\n\nThe check could not run: no ref\n')

    def test_failure_writes_no_summary(self):
        run_quietly(raise_(fc.Failure('rule broken')), summary_heading='My check')
        self.assertFalse(self.summary.exists())

    def test_other_exceptions_propagate(self):
        with self.assertRaises(KeyError):
            run_quietly(raise_(KeyError('x')))


class StepSummaryTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_appends_to_the_given_path(self):
        path = self.dir / 'given.md'
        with mock.patch.dict(os.environ, {'GITHUB_STEP_SUMMARY': str(self.dir / 'env.md')}):
            fc.write_step_summary('a\n', path)
            fc.write_step_summary('b\n', str(path))
        self.assertEqual(path.read_text(), 'a\nb\n')
        self.assertFalse((self.dir / 'env.md').exists())

    def test_falls_back_to_the_environment(self):
        path = self.dir / 'env.md'
        with mock.patch.dict(os.environ, {'GITHUB_STEP_SUMMARY': str(path)}):
            fc.write_step_summary('## x\n')
        self.assertEqual(path.read_text(), '## x\n')

    def test_no_path_and_no_environment_is_a_no_op(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            fc.write_step_summary('ignored\n')
        self.assertEqual(list(self.dir.iterdir()), [])


class GitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.repo = pathlib.Path(cls.tmp.name) / 'repo'
        cls.repo.mkdir()
        # No user or system config, so a developer's commit signing or hooks cannot break the fixture.
        env = dict(os.environ, GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull)
        subprocess.run(['git', '-C', str(cls.repo), 'init', '-q'], check=True, env=env)
        subprocess.run(['git', '-C', str(cls.repo), '-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q',
                        '--allow-empty', '-m', 'init'], check=True, env=env)
        cls.head = subprocess.run(['git', '-C', str(cls.repo), 'rev-parse', 'HEAD'], check=True, capture_output=True,
                                  text=True, env=env).stdout.strip()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_returns_code_and_stdout_bytes(self):
        self.assertEqual(fc.git('rev-parse', 'HEAD', cwd=self.repo), (0, (self.head + '\n').encode()))

    def test_git_text_strips(self):
        self.assertEqual(fc.git_text('rev-parse', 'HEAD', cwd=str(self.repo)), self.head)

    def test_runs_in_the_current_directory_without_cwd(self):
        cwd = os.getcwd()
        os.chdir(self.repo)
        try:
            self.assertEqual(fc.git_text('rev-parse', 'HEAD'), self.head)
        finally:
            os.chdir(cwd)

    def test_ok_codes(self):
        code, out = fc.git('rev-parse', '--verify', '--quiet', 'no-such-ref', cwd=self.repo, ok_codes=(0, 1))
        self.assertEqual((code, out), (1, b''))

    def test_code_outside_ok_codes_is_a_setup_error(self):
        with self.assertRaisesRegex(fc.SetupError, r'^git rev-parse --verify no-such-ref failed \(128\): fatal: '):
            fc.git('rev-parse', '--verify', 'no-such-ref', cwd=self.repo)
        with self.assertRaises(fc.SetupError):
            fc.git_text('merge-base', '--is-ancestor', self.head, 'no-such-ref', cwd=self.repo)

    def test_missing_directory_is_a_setup_error(self):
        with self.assertRaises(fc.SetupError):
            fc.git('status', cwd=self.repo / 'missing')

    def test_missing_git_is_a_setup_error(self):
        with tempfile.TemporaryDirectory() as empty, mock.patch.dict(os.environ, {'PATH': empty}):
            with self.assertRaisesRegex(fc.SetupError, '^cannot run git: '):
                fc.git('--version')


class GamesFlagTest(unittest.TestCase):
    def test_flag_spellings(self):
        self.assertEqual(fc.GAMES_MACRO, 'FREEINK_CAP_GAMES')
        self.assertEqual(fc.GAMES_BUILD_FLAG, '-DFREEINK_CAP_GAMES=1')

    def test_build_flag_is_in_platformio_ini(self):
        text = (pathlib.Path(__file__).resolve().parent.parent / 'platformio.ini').read_text()
        self.assertIn(fc.GAMES_BUILD_FLAG, text.split())


if __name__ == '__main__':
    unittest.main()

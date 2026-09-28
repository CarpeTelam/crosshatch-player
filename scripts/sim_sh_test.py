#!/usr/bin/env python3
"""
Tests for the simulator skill's `sim.sh setup` and `sim.sh check` (.claude/skills/run-crosshatch-player/sim.sh;
the epic-icon-library retrospective's R9 (c), AI-11). Standard library, bash, awk, and git only:

    python3 scripts/sim_sh_test.py [-v]

sim.sh works in the root of the git repository that holds it, so each case copies sim.sh and simulator.ini into a
throwaway git repository with fs_/books/ already seeded, writes that repository's platformio.local.ini (never this
repository's), and asserts both commands' exit codes and the file's bytes afterwards. setup accepts a file with no
markers or with exactly one begin marker followed by one end marker; any other shape is refused byte-unchanged,
since its block replace would drop the user's lines. A missing bash, awk, or git fails every case, naming the tool,
rather than skipping it: a skip would still print CI's "Ran <n> tests" line and pass.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SKILL_REL = pathlib.Path('.claude/skills/run-crosshatch-player')
SKILL = HERE.parent / SKILL_REL
BEGIN = '; >>> crosshatch simulator (managed by .claude/skills/run-crosshatch-player/sim.sh setup)\n'
END = '; <<< crosshatch simulator\n'
USER_BEFORE = '[env:mine]\nbuild_flags = -DMINE=1\n'
USER_AFTER = '; kept after the block\n[platformio]\ndefault_envs = x4pro\n'
OLD = '[env:simulator]\nplatform = native\n'

# The shapes setup refuses and check reports, each with the user's lines around it.
MALFORMED = {
    'begin only': USER_BEFORE + BEGIN + OLD + USER_AFTER,
    'end only': USER_BEFORE + OLD + END + USER_AFTER,
    'end before begin': USER_BEFORE + END + OLD + BEGIN + USER_AFTER,
    'two begins then an end': BEGIN + USER_BEFORE + BEGIN + OLD + END + USER_AFTER,
    'two blocks': BEGIN + OLD + END + USER_BEFORE + BEGIN + OLD + END + USER_AFTER,
    'begin then two ends': BEGIN + OLD + END + USER_AFTER + END,
}

TOOLS = ('bash', 'awk', 'git')


class SimShTest(unittest.TestCase):

    def setUp(self):
        missing = [tool for tool in TOOLS if not shutil.which(tool)]
        if missing:
            self.fail(f'not on PATH: {", ".join(missing)} (these tests need bash, awk, and git)')
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix='sim-sh-test-'))
        subprocess.run(['git', 'init', '-q', str(self.tmp)], check=True)
        skill = self.tmp / SKILL_REL
        skill.mkdir(parents=True)
        for name in ('sim.sh', 'simulator.ini'):
            shutil.copy2(SKILL / name, skill / name)
        books = self.tmp / 'fs_' / 'books'
        books.mkdir(parents=True)
        (books / 'seed.epub').write_bytes(b'seed')
        self.ini = self.tmp / 'platformio.local.ini'
        simulator = (SKILL / 'simulator.ini').read_text()
        self.assertTrue(simulator.endswith('\n'))
        self.block = BEGIN + simulator + END

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def sim(self, command):
        env = dict(os.environ)
        env.pop('GIT_DIR', None)
        env.pop('GIT_WORK_TREE', None)
        proc = subprocess.run(['bash', str(self.tmp / SKILL_REL / 'sim.sh'), command], cwd=self.tmp, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return proc.returncode, proc.stdout + proc.stderr

    def assert_sim(self, command, code, text):
        got, out = self.sim(command)
        self.assertEqual(got, code, out)
        self.assertIn(text, out)

    def test_no_file(self):
        self.assert_sim('check', 1, 'has no managed simulator block')
        self.assert_sim('setup', 0, 'simulator envs installed')
        self.assertEqual(self.ini.read_text(), self.block)
        self.assert_sim('check', 0, 'is current')

    def test_no_markers_keeps_the_user_lines(self):
        self.ini.write_text(USER_BEFORE + USER_AFTER)
        self.assert_sim('check', 1, 'has no managed simulator block')
        self.assert_sim('setup', 0, 'simulator envs installed')
        self.assertEqual(self.ini.read_text(), USER_BEFORE + USER_AFTER + self.block)
        self.assert_sim('check', 0, 'is current')

    def test_one_block_is_replaced_and_the_user_lines_kept(self):
        self.ini.write_text(USER_BEFORE + BEGIN + OLD + END + USER_AFTER)
        self.assert_sim('check', 1, 'differs from simulator.ini (stale)')
        self.assert_sim('setup', 0, 'simulator envs installed')
        self.assertEqual(self.ini.read_text(), USER_BEFORE + USER_AFTER + self.block)
        self.assert_sim('check', 0, 'is current')
        self.assert_sim('setup', 0, 'simulator envs installed')
        self.assertEqual(self.ini.read_text(), USER_BEFORE + USER_AFTER + self.block)

    def test_malformed_markers_are_refused(self):
        for name, text in MALFORMED.items():
            with self.subTest(name):
                self.ini.write_bytes(text.encode())
                self.assert_sim('setup', 1, 'restore it by hand')
                self.assertEqual(self.ini.read_bytes(), text.encode())
                self.assert_sim('check', 1, 'restore it by hand')
                self.assertEqual(self.ini.read_bytes(), text.encode())


if __name__ == '__main__':
    unittest.main()

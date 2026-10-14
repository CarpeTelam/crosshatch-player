#!/usr/bin/env python3
"""
Tests for the numbered simulator instances of `sim.sh` (.claude/skills/run-crosshatch-player/sim.sh; entry 16 of
epic-play-nearby). Standard library, bash, awk, and git only:

    python3 .claude/skills/run-crosshatch-player/sim_instance_test.py [-v]

SIM_INSTANCE=N (1..9) gives a simulator its own display, state directory, SD root, web server port, and inbox, and every
instance of one worktree shares one UDP port base (CROSSHATCH_UDP_PORT_BASE; the caller's, else derived from the repo
root). Like scripts/sim_sh_test.py, each case copies sim.sh and simulator.ini into a throwaway git repository and runs
it there, never in this repository; `where` is the command that prints what an instance would use, without creating
anything. The folder holds this test (not scripts/) because scripts/sim_sh_test.py is outside entry 16's file list; CI's
scripts/*_test.py glob does not run it, so run it by hand (deferred: move or merge it into scripts/).
A missing bash, awk, or git fails every case, naming the tool, rather than skipping it.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SKILL_REL = pathlib.Path('.claude/skills/run-crosshatch-player')
TOOLS = ('bash', 'awk', 'git')
# The packer stands in as a stub that records its arguments and writes the package the real one would.
PACK_STUB = '''import pathlib, sys
src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
(out / (src.name + '.chgame')).write_text('packed from ' + str(src))
print('0123456789abcdef')
'''


def scratch_repo(parent, name):
    """A throwaway git repository holding sim.sh and simulator.ini, and the packer stub."""
    root = parent / name
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    skill = root / SKILL_REL
    skill.mkdir(parents=True)
    for file in ('sim.sh', 'simulator.ini'):
        shutil.copy2(HERE / file, skill / file)
    (root / 'scripts').mkdir()
    (root / 'scripts' / 'pack_game.py').write_text(PACK_STUB)
    (root / 'fs_' / 'books').mkdir(parents=True)
    (root / 'fs_' / 'books' / 'seed.epub').write_bytes(b'seed')
    return root


def sim(root, *args, instance=None, env=None):
    """(exit code, stdout + stderr) of `sim.sh <args>` run in `root`, with SIM_INSTANCE when given."""
    full = dict(os.environ)
    for name in ('GIT_DIR', 'GIT_WORK_TREE', 'SIM_INSTANCE', 'SIM_DISPLAY', 'CROSSHATCH_UDP_PORT_BASE'):
        full.pop(name, None)
    if instance is not None:
        full['SIM_INSTANCE'] = instance
    full.update(env or {})
    proc = subprocess.run(['bash', str(root / SKILL_REL / 'sim.sh'), *args], cwd=root, env=full,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def where(root, **kwargs):
    code, out = sim(root, 'where', **kwargs)
    return code, dict(line.split('=', 1) for line in out.splitlines() if '=' in line)


def tree(root):
    """Every path under root outside .git."""
    found = set()
    for path in root.rglob('*'):
        relative = path.relative_to(root)
        if relative.parts[0] != '.git':
            found.add(relative.as_posix())
    return found


class SimInstanceTest(unittest.TestCase):

    def setUp(self):
        missing = [tool for tool in TOOLS if not shutil.which(tool)]
        if missing:
            self.fail(f'not on PATH: {", ".join(missing)} (these tests need bash, awk, and git)')
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix='sim-instance-test-')).resolve()
        self.root = scratch_repo(self.tmp, 'repo')

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_the_default_instance_is_as_before(self):
        code, got = where(self.root)
        self.assertEqual(code, 0)
        self.assertEqual(got['instance'], 'default')
        self.assertEqual(got['display'], ':77')
        self.assertEqual(got['state'], 'build/sim')
        self.assertEqual(got['shots'], 'build/sim/shots')
        self.assertEqual(got['sd'], f'{self.root}/fs_')
        self.assertEqual(got['inbox'], f'{self.root}/fs_/games')
        self.assertEqual(got['http_port'], '8080')
        self.assertEqual(got['udp_base'], where(self.root, instance='1')[1]['udp_base'],
                         'the default instance shares the numbered ones\' base, so they find each other')
        self.assertEqual(where(self.root, instance='')[1], got, 'an empty SIM_INSTANCE is none')

    def test_each_instance_has_its_own_display_state_sd_inbox_and_port(self):
        seen = {}
        for number in (1, 2, 9):
            code, got = where(self.root, instance=str(number))
            self.assertEqual(code, 0)
            seen[number] = got
            self.assertEqual(got['instance'], str(number))
            self.assertEqual(got['display'], f':{77 + number}')
            self.assertEqual(got['state'], f'build/sim/inst-{number}')
            self.assertEqual(got['shots'], f'build/sim/inst-{number}/shots')
            self.assertEqual(got['sd'], f'{self.root}/build/sim/inst-{number}/fs_')
            self.assertEqual(got['inbox'], f'{self.root}/build/sim/inst-{number}/fs_/games')
            self.assertEqual(got['http_port'], str(8080 + 10 * number))
        self.assertEqual(seen[1]['display'], ':78')
        self.assertEqual(seen[9]['http_port'], '8170')
        for key in ('display', 'state', 'sd', 'inbox', 'http_port'):
            self.assertEqual(len({seen[n][key] for n in seen}), 3, key)
        # Every instance of the worktree shares the UDP base: it is what lets them find each other.
        self.assertEqual(len({seen[n]['udp_base'] for n in seen}), 1)

    def test_sim_display_overrides_the_instances_display(self):
        self.assertEqual(where(self.root, instance='2', env={'SIM_DISPLAY': ':99'})[1]['display'], ':99')

    def test_an_invalid_instance_exits_1_naming_it(self):
        for bad in ('0', '10', 'a', '-1', '01', '1 ', 'x2'):
            with self.subTest(bad):
                code, out = sim(self.root, 'where', instance=bad)
                self.assertEqual(code, 1, out)
                self.assertIn('SIM_INSTANCE', out)
                self.assertIn(f"'{bad}'", out)

    def test_the_udp_base_is_the_callers_else_derived_from_the_root(self):
        code, got = where(self.root, instance='1', env={'CROSSHATCH_UDP_PORT_BASE': '30000'})
        self.assertEqual((code, got['udp_base']), (0, '30000'))
        self.assertEqual(where(self.root, env={'CROSSHATCH_UDP_PORT_BASE': '31000'})[1]['udp_base'], '31000')
        base = int(where(self.root, instance='1')[1]['udp_base'])
        self.assertEqual(base, int(where(self.root, instance='2')[1]['udp_base']))
        self.assertEqual(base, int(where(self.root)[1]['udp_base']), 'the default instance too')
        self.assertTrue(21000 <= base <= 28992 and (base - 21000) % 8 == 0, base)

    def test_two_different_roots_give_two_different_bases(self):
        bases = {}
        for index in range(20):  # a collision of two roots is 1 in 1000; twenty roots do not all collide
            root = scratch_repo(self.tmp, f'root-{index}')
            bases[root] = int(where(root, instance='1')[1]['udp_base'])
        for base in bases.values():
            self.assertTrue(21000 <= base <= 28999 and (base - 21000) % 8 == 0, base)
        self.assertGreater(len(set(bases.values())), 1)
        first = next(iter(bases))
        self.assertEqual(int(where(first, instance='1')[1]['udp_base']), bases[first], 'the same root, the same base')

    def test_where_creates_nothing(self):
        before = tree(self.root)
        for instance in (None, '1', '9'):
            where(self.root, instance=instance)
        sim(self.root, 'where', instance='0')
        self.assertEqual(tree(self.root), before)

    def test_inbox_packs_into_the_instances_games_folder(self):
        game = self.root / 'nearby'
        game.mkdir()
        code, out = sim(self.root, 'inbox', 'nearby', instance='2')
        self.assertEqual(code, 0, out)
        packed = self.root / 'build/sim/inst-2/fs_/games/nearby.chgame'
        self.assertEqual(packed.read_text(), 'packed from nearby')
        code, out = sim(self.root, 'inbox', 'nearby')
        self.assertEqual(code, 0, out)
        self.assertTrue((self.root / 'fs_/games/nearby.chgame').is_file())
        self.assertEqual(sim(self.root, 'inbox', 'missing', instance='2')[0], 1)
        self.assertEqual(sim(self.root, 'inbox', instance='2')[0], 1)

    def test_setup_and_check_are_unchanged_by_an_instance(self):
        other = scratch_repo(self.tmp, 'other')
        for command in ('check', 'setup', 'check'):
            plain = sim(self.root, command)
            numbered = sim(other, command, instance='3')
            self.assertEqual(numbered[0], plain[0], command)
            # The messages name the script's own path, which differs between the two scratch repositories.
            self.assertEqual(numbered[1].replace(str(other), 'ROOT'), plain[1].replace(str(self.root), 'ROOT'), command)
            self.assertEqual((other / 'platformio.local.ini').exists(), (self.root / 'platformio.local.ini').exists())
        self.assertEqual((other / 'platformio.local.ini').read_bytes(), (self.root / 'platformio.local.ini').read_bytes())


if __name__ == '__main__':
    unittest.main()

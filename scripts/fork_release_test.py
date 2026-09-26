#!/usr/bin/env python3
"""Tests for scripts/fork_release.py. Standard library only; needs git on PATH. Run: python3 scripts/fork_release_test.py"""

import contextlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fork_release as fr  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parent.parent
RULES = fr.load_rules(fr.DEFAULT_VECTORS)
URL = RULES.release_url.encode()
UPSTREAM = b'https://api.github.com/' + RULES.upstream_fragment.encode() + b'/latest'


def quiet(function, *args):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return function(*args)


def app_image(*strings, magic=0xE9, desc=fr.APP_DESC_MAGIC):
    header = bytes([magic]) + bytes(31) + desc + bytes(60)
    return header + b''.join(b'\x00' + s + b'\x00' for s in strings)


def ini(version_lines='version = 1.6.5\n'):
    return f'[platformio]\ndefault_envs = default\n\n[crosspoint]\n{version_lines}\n[base]\nboard = x ; comment\n'


def fake_config(envs=('x4pro', 'sticky'), version_flag='\\"${v}\\"', games=True):
    """A pio_config stand-in that resolves ${v} from the project's platformio.ini, as PlatformIO would."""

    def config(project_dir):
        text = (pathlib.Path(project_dir) / 'platformio.ini').read_text()
        version = next(line.split('=', 1)[1].strip() for line in text.splitlines() if line.startswith('version'))
        result = {'crosspoint': {'version': version}, 'env:default': {'build_flags': ['-DX=1']}}
        for board in envs:
            flags = ['-DFREEINK_DEVICE=1', f'-DCROSSPOINT_VERSION={version_flag.replace("${v}", version)}']
            if games:
                flags.append(fr.GAMES_FLAG)
            result[f'env:{board}-gh_release'] = {'build_flags': flags}
            result[f'env:{board}-gh_release_rc'] = {'build_flags': flags + ['-DRC']}
        result['env:x4c-gh_release'] = {'build_flags': [f'-DCROSSPOINT_VERSION=\\"{version}\\"']}
        result['env:gh_release'] = {'build_flags': [f'-DCROSSPOINT_VERSION=\\"{version}\\"', 'gh_release']}
        return result

    return config


class TempProject:
    """A git repo with platformio.ini and a first commit."""

    def __init__(self, ini_text=None):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        (self.dir / 'platformio.ini').write_text(ini() if ini_text is None else ini_text)
        self.git('init', '-q', '-b', 'develop')
        self.git('add', '-A')
        self.git('-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-qm', 'init')

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.dir), *args], check=True, capture_output=True, text=True).stdout

    def close(self):
        self.tmp.cleanup()


class BuildNumberTest(unittest.TestCase):
    def test_first_release_is_one(self):
        self.assertEqual(fr.next_build_number([]), 1)
        self.assertEqual(fr.next_build_number(['v1.5.0', '1.6.0rc', '1.6.5rc']), 1)

    def test_largest_number_after_ch_in_any_tag(self):
        self.assertEqual(fr.next_build_number(['1.6.5-ch.3', 'v1.6.5-ch.8', '1.6.5-ch.08', 'v1.5.0']), 9)
        self.assertEqual(fr.next_build_number(['1.7.0-ch.2', '1.6.5-ch.12-rc', 'x-ch.']), 13)


class VersionLineTest(unittest.TestCase):
    def test_reads_the_one_line(self):
        index, value = fr.find_version_line(ini())
        self.assertEqual(value, '1.6.5')
        self.assertEqual(fr.rewrite_version(ini(), index, '1.6.5-ch.1'), ini('version = 1.6.5-ch.1\n'))

    def test_bad_lines_fail(self):
        for lines in ('version = 1.7.0-dev\n', '', 'version = 1.6.5\nversion = 1.6.6\n', 'version = ${sysenv.V}\n',
                      'version = 1.6\n', 'version = 1.6.5 ; note\n'):
            with self.subTest(lines=lines), self.assertRaises(fr.Failure):
                fr.find_version_line(ini(lines))

    def test_version_in_another_section_does_not_count(self):
        with self.assertRaises(fr.Failure):
            fr.find_version_line('[crosspoint]\n\n[other]\nversion = 1.6.5\n')

    def test_rewrite_keeps_crlf(self):
        text = '[crosspoint]\r\nversion = 1.6.5\r\n[base]\r\n'
        index, _ = fr.find_version_line(text)
        self.assertEqual(fr.rewrite_version(text, index, '1.6.5-ch.2'), '[crosspoint]\r\nversion = 1.6.5-ch.2\r\n[base]\r\n')


class RulesTest(unittest.TestCase):
    def test_script_names_assets_as_the_vectors_say(self):
        RULES.check_asset_vectors()
        for vector in RULES.asset_vectors:
            self.assertEqual(RULES.asset_name(vector['tag'], vector['board']), vector['asset'])

    def test_disagreeing_vector_is_a_setup_error(self):
        data = json.loads(fr.DEFAULT_VECTORS.read_text())
        data['asset_names'][0]['asset'] = 'crosspoint-other.bin'
        with self.assertRaises(fr.SetupError):
            fr.Rules(data).check_asset_vectors()

    def test_tags(self):
        data = json.loads(fr.DEFAULT_VECTORS.read_text())
        for vector in data['valid_tags']:
            self.assertTrue(RULES.is_tag(vector['tag']), vector)
        for vector in data['invalid_tags']:
            self.assertFalse(RULES.is_tag(vector['tag']), vector)

    def test_asset_name_must_fit_the_update_buffer(self):
        # 'crosspoint-1.6.5-ch.1-' + board + '.bin' plus a NUL must fit 48 bytes.
        self.assertEqual(RULES.asset_name('1.6.5-ch.1', 'b' * 21), 'crosspoint-1.6.5-ch.1-' + 'b' * 21 + '.bin')
        self.assertEqual(RULES.asset_name('1.6.5-ch.1', 'b' * 22), '')

    def test_missing_field_is_a_setup_error(self):
        with self.assertRaises(fr.SetupError):
            fr.Rules({'tag_grammar': 'x'})


class ReleaseEnvsTest(unittest.TestCase):
    def test_flagged_gh_release_envs_only(self):
        envs = fr.release_envs(self.config('1.6.5-ch.1'), '1.6.5-ch.1', RULES)
        self.assertEqual([e['env'] for e in envs], ['sticky-gh_release', 'x4pro-gh_release'])
        self.assertEqual(envs[1], {'env': 'x4pro-gh_release', 'board': 'x4pro', 'asset': 'crosspoint-1.6.5-ch.1-x4pro.bin'})

    def config(self, version, **kwargs):
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / 'platformio.ini').write_text(ini(f'version = {version}\n'))
            return fake_config(**kwargs)(tmp)

    def test_no_flagged_env_fails(self):
        with self.assertRaises(fr.Failure):
            fr.release_envs(self.config('1.6.5-ch.1', games=False), '1.6.5-ch.1', RULES)

    def test_version_define_must_be_exactly_the_tag(self):
        for flag in ('\\"${v}-x4pro\\"', '\\"1.6.5\\"'):
            with self.subTest(flag=flag), self.assertRaises(fr.Failure):
                fr.release_envs(self.config('1.6.5-ch.1', version_flag=flag), '1.6.5-ch.1', RULES)

    def test_second_version_define_fails(self):
        config = self.config('1.6.5-ch.1')
        config['env:x4pro-gh_release']['build_flags'].append('-DCROSSPOINT_VERSION=\\"x\\"')
        with self.assertRaises(fr.Failure):
            fr.release_envs(config, '1.6.5-ch.1', RULES)

    def test_overrides_fail(self):
        fr.check_overrides({})
        with self.assertRaises(fr.Failure):
            fr.check_overrides({'PLATFORMIO_BUILD_FLAGS': '-DCROSSPOINT_VERSION=\\"x\\"'})


class PrepareTest(unittest.TestCase):
    def run_prepare(self, project, tags, config=None):
        tags_file = project.dir / 'tags.txt'
        tags_file.write_text(''.join(t + '\n' for t in tags))
        plan = project.dir / 'out' / 'plan.json'
        args = ['prepare', '--project-dir', str(project.dir), '--tags', str(tags_file), '--plan', str(plan)]
        with mock.patch.object(fr, 'pio_config', config or fake_config()), mock.patch.dict(os.environ, clear=False):
            for name in fr.BUILD_OVERRIDES + ('GITHUB_OUTPUT',):
                os.environ.pop(name, None)
            code = quiet(fr.main, args)
        return code, (json.loads(plan.read_text()) if plan.exists() else None)

    def setUp(self):
        self.project = TempProject()

    def tearDown(self):
        self.project.close()

    def test_first_release(self):
        code, plan = self.run_prepare(self.project, ['v1.5.0'])
        self.assertEqual(code, 0)
        self.assertEqual(plan['tag'], '1.6.5-ch.1')
        self.assertEqual(plan['commit'], self.project.git('rev-parse', 'HEAD').strip())
        self.assertEqual([e['asset'] for e in plan['envs']], ['crosspoint-1.6.5-ch.1-sticky.bin', 'crosspoint-1.6.5-ch.1-x4pro.bin'])
        self.assertIn('version = 1.6.5-ch.1\n', (self.project.dir / 'platformio.ini').read_text())

    def test_gaps_and_odd_tags(self):
        code, plan = self.run_prepare(self.project, ['1.6.5-ch.3', 'v1.6.5-ch.8', '1.6.5-ch.08', 'v1.5.0'])
        self.assertEqual((code, plan['tag']), (0, '1.6.5-ch.9'))

    def test_ten_digit_n_fails_and_leaves_the_file(self):
        code, plan = self.run_prepare(self.project, ['1.6.5-ch.999999999'])
        self.assertEqual((code, plan), (1, None))
        self.assertIn('version = 1.6.5\n', (self.project.dir / 'platformio.ini').read_text())

    def test_too_long_tag_fails(self):
        (self.project.dir / 'platformio.ini').write_text(ini('version = 1234567.1234567.12345\n'))  # 26 characters with -ch.1
        self.assertEqual(self.run_prepare(self.project, [])[0], 1)

    def test_bad_version_line_fails(self):
        (self.project.dir / 'platformio.ini').write_text(ini('version = 1.7.0-dev\n'))
        self.assertEqual(self.run_prepare(self.project, [])[0], 1)

    def test_already_rewritten_file_fails(self):
        self.assertEqual(self.run_prepare(self.project, [])[0], 0)
        self.assertEqual(self.run_prepare(self.project, [])[0], 1)

    def test_pio_reading_another_version_fails(self):
        def config(project_dir):
            result = fake_config()(project_dir)
            result['crosspoint']['version'] = '9.9.9'
            return result

        self.assertEqual(self.run_prepare(self.project, [], config)[0], 1)

    def test_no_flagged_env_fails(self):
        self.assertEqual(self.run_prepare(self.project, [], fake_config(games=False))[0], 1)


class ImageTest(unittest.TestCase):
    TAG = '1.6.5-ch.1'

    def good(self, board='x4pro'):
        return app_image(b'CrossPoint-ESP32-' + self.TAG.encode(), URL, b'CROSSPOINT-BOARD-V1:' + board.encode() + b';')

    def test_good_image(self):
        self.assertEqual(fr.image_problems(self.good(), self.TAG, 'x4pro', RULES), [])
        self.assertEqual(fr.image_problems(app_image(self.TAG.encode(), URL, b'CROSSPOINT-BOARD-V1:sticky;'), self.TAG, 'sticky', RULES), [])

    def check_one_problem(self, data, board, fragment):
        problems = fr.image_problems(data, self.TAG, board, RULES)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn(fragment, problems[0])

    def test_missing_tag(self):
        self.check_one_problem(app_image(b'1.6.5', URL, b'CROSSPOINT-BOARD-V1:x4pro;'), 'x4pro', 'tag')
        self.check_one_problem(app_image(b'11.6.5-ch.1', URL, b'CROSSPOINT-BOARD-V1:x4pro;'), 'x4pro', 'tag')
        self.check_one_problem(app_image(b'1.6.5-ch.12', URL, b'CROSSPOINT-BOARD-V1:x4pro;'), 'x4pro', 'tag')

    def test_urls(self):
        self.check_one_problem(app_image(self.TAG.encode(), b'CROSSPOINT-BOARD-V1:x4pro;'), 'x4pro', 'fork release URL')
        self.check_one_problem(self.good() + UPSTREAM, 'x4pro', 'upstream')

    def test_board_tag(self):
        self.check_one_problem(self.good('sticky'), 'x4pro', "'sticky'")
        self.check_one_problem(self.good() + b'CROSSPOINT-BOARD-V1:x4pro;', 'x4pro', '2 board tags')
        self.check_one_problem(app_image(self.TAG.encode(), URL), 'x4pro', '0 board tags')

    def test_not_an_app_image(self):
        self.check_one_problem(app_image(self.TAG.encode(), URL, b'CROSSPOINT-BOARD-V1:x4pro;', magic=0), 'x4pro', 'application image')
        merged = app_image(self.TAG.encode(), URL, b'CROSSPOINT-BOARD-V1:x4pro;', desc=b'\x00\x00\x00\x00')
        self.check_one_problem(merged, 'x4pro', 'descriptor')
        self.assertTrue(fr.image_problems(b'', self.TAG, 'x4pro', RULES))

    def test_check_images_copies_assets_and_fails_on_any_bad_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            plan = {'tag': self.TAG, 'envs': [{'env': 'x4pro-gh_release', 'board': 'x4pro', 'asset': 'a-x4pro.bin'},
                                              {'env': 'sticky-gh_release', 'board': 'sticky', 'asset': 'a-sticky.bin'}]}
            fr.save_plan(tmp / 'plan.json', plan)
            for env, board in (('x4pro-gh_release', 'x4pro'), ('sticky-gh_release', 'sticky')):
                (tmp / '.pio' / 'build' / env).mkdir(parents=True)
                (tmp / '.pio' / 'build' / env / 'firmware.bin').write_bytes(self.good(board))
            args = ['check-images', '--plan', str(tmp / 'plan.json'), '--project-dir', str(tmp), '--dist', str(tmp / 'dist')]
            self.assertEqual(quiet(fr.main, args), 0)
            self.assertEqual(sorted(p.name for p in (tmp / 'dist').iterdir()), ['a-sticky.bin', 'a-x4pro.bin'])
            self.assertEqual(len(json.loads((tmp / 'plan.json').read_text())['firmware']), 2)
            (tmp / '.pio' / 'build' / 'sticky-gh_release' / 'firmware.bin').write_bytes(self.good('x4pro'))
            self.assertEqual(quiet(fr.main, args), 1)
            (tmp / '.pio' / 'build' / 'sticky-gh_release' / 'firmware.bin').unlink()
            self.assertEqual(quiet(fr.main, args), 2)


PACKER_OK = '''
import pathlib, sys
game, out = sys.argv[1], pathlib.Path(sys.argv[2])
(out / (pathlib.Path(game).name + '.cpgame')).write_bytes(b'PK' + pathlib.Path(game, 'main.lua').read_bytes())
print('packed ' + game)
print('0123456789abcdef')
'''


class GamesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        self.plan = self.dir / 'plan.json'
        fr.save_plan(self.plan, {'packages': None})

    def tearDown(self):
        self.tmp.cleanup()

    def pack(self):
        args = ['pack-games', '--plan', str(self.plan), '--project-dir', str(self.dir), '--dist', str(self.dir / 'dist')]
        return quiet(fr.main, args)

    def add_game(self, game_id):
        (self.dir / 'games' / game_id).mkdir(parents=True)
        (self.dir / 'games' / game_id / 'main.lua').write_text(f'-- {game_id}\n')

    def packer(self, source):
        (self.dir / 'scripts').mkdir(exist_ok=True)
        (self.dir / 'scripts' / 'pack_game.py').write_text(textwrap.dedent(source))

    def test_no_games_is_valid(self):
        self.assertEqual(self.pack(), 0)
        (self.dir / 'games').mkdir()
        (self.dir / 'games' / 'README').write_text('not a game')
        self.assertEqual(self.pack(), 0)
        self.assertEqual(json.loads(self.plan.read_text())['packages'], [])

    def test_directory_that_is_not_a_game_id_fails(self):
        self.packer(PACKER_OK)
        for bad in ('Mines', 'my game', '-dots', 'x' * 33):
            with self.subTest(bad=bad):
                self.add_game(bad)
                self.assertEqual(self.pack(), 1)
                (self.dir / 'games' / bad / 'main.lua').unlink()
                (self.dir / 'games' / bad).rmdir()

    def test_games_without_packer_fail(self):
        self.add_game('mines')
        self.assertEqual(self.pack(), 1)

    def test_packs_every_game(self):
        self.add_game('mines')
        self.add_game('dots')
        self.packer(PACKER_OK)
        self.assertEqual(self.pack(), 0)
        packages = json.loads(self.plan.read_text())['packages']
        self.assertEqual([p['asset'] for p in packages], ['dots.cpgame', 'mines.cpgame'])
        self.assertEqual({p['package_hash'] for p in packages}, {'0123456789abcdef'})
        self.assertEqual((self.dir / 'dist' / 'mines.cpgame').read_bytes(), b'PK-- mines\n')

    def test_failing_packer_fails(self):
        self.add_game('mines')
        self.packer('import sys\nprint("0123456789abcdef")\nsys.exit(3)\n')
        self.assertEqual(self.pack(), 1)

    def test_bad_hash_line_fails(self):
        self.add_game('mines')
        self.packer(PACKER_OK.replace("print('0123456789abcdef')", "print('0123456789ABCDEF')"))
        self.assertEqual(self.pack(), 1)

    def test_missing_package_fails(self):
        self.add_game('mines')
        self.packer("print('0123456789abcdef')\n")
        self.assertEqual(self.pack(), 1)

    def test_packer_changing_games_fails(self):
        self.add_game('mines')
        self.packer(PACKER_OK + "pathlib.Path(game, 'version.txt').write_text('stamped')\n")
        self.assertEqual(self.pack(), 1)


RELEASE_ON = 'name: Release\non:\n  release:\n    types: [published]\njobs: {}\n'


class WorkflowTest(unittest.TestCase):
    def test_triggers(self):
        cases = {
            RELEASE_ON: ['release'],
            'on: release\n': ['release'],
            'on: [push, release]\n': ['push', 'release'],
            '"on":\n  - push\n  - release\n': ['push', 'release'],
            'on:\n- release\njobs:\n': ['release'],
            "on:\n  'release':\n": ['release'],
            'on:  # comment\n  push:\n    branches: [release]\n    tags:\n      - release\n  pull_request:\njobs:\n  release:\n': ['push', 'pull_request'],
            'on:\n  workflow_run:\n    workflows: [release]\n    types: [released]\n': ['workflow_run'],
            'on: [push,\n  release]\njobs:\n  build: {}\n': ['push', 'release'],
            'on:\n  [push, release]\njobs:\n': ['push', 'release'],
            'on:\n  {release: {types: [published]}}\n': ['release', 'types', 'published'],
            'name: x\n# on: release\non:\n  workflow_dispatch:\n': ['workflow_dispatch'],
        }
        for text, triggers in cases.items():
            with self.subTest(text=text):
                self.assertEqual(fr.top_level_triggers(text), triggers)
        self.assertIsNone(fr.top_level_triggers('name: x\njobs: {}\n'))

    def test_conflicts(self):
        self.assertEqual(fr.workflow_conflicts(RELEASE_ON), ['triggers on `release`'])
        self.assertEqual(fr.workflow_conflicts('on: workflow_dispatch\nrun: pio run -e x4pro-gh_release_rc\n'),
                         ['mentions a gh_release env'])
        self.assertEqual(fr.workflow_conflicts('jobs: {}\n'), ['has no top-level `on:` that could be read'])
        self.assertEqual(fr.workflow_conflicts('on: pull_request\n'), [])

    def test_states_and_self(self):
        files = {'.github/workflows/a.yml': [RELEASE_ON], '.github/workflows/b.yml': [RELEASE_ON],
                 '.github/workflows/me.yml': [RELEASE_ON], '.github/workflows/c.yml': ['on: push\n', RELEASE_ON]}
        states = fr.parse_states(['.github/workflows/a.yml\tdisabled_manually', '.github/workflows/c.yml\tactive',
                                  'dynamic/pages\tactive'])
        problems = fr.workflow_problems(files, states, '.github/workflows/me.yml')
        self.assertEqual([p.split()[0] for p in problems], ['.github/workflows/b.yml', '.github/workflows/c.yml'])

    def test_workflow_path(self):
        ref = 'CarpeTelam/crosshatch-player/.github/workflows/crosshatch-release.yml@refs/heads/develop'
        self.assertEqual(fr.workflow_path(ref, 'CarpeTelam/crosshatch-player'), '.github/workflows/crosshatch-release.yml')
        for bad in ('', 'other/repo/.github/workflows/x.yml@refs/heads/develop', 'CarpeTelam/crosshatch-player/x.yml'):
            with self.subTest(bad=bad), self.assertRaises(fr.SetupError):
                fr.workflow_path(bad, 'CarpeTelam/crosshatch-player')

    def test_this_repository(self):
        files = fr.workflow_files([REPO])
        problems = fr.workflow_problems(files, {}, '.github/workflows/crosshatch-release.yml')
        flagged = {p.split()[0] for p in problems}
        for upstream in ('.github/workflows/release.yml', '.github/workflows/release_candidate.yml'):
            if upstream in files:
                self.assertIn(upstream, flagged)
        self.assertFalse({p for p in flagged if p.startswith('.github/workflows/crosshatch-')})
        states = {path: 'disabled_manually' for path in flagged}
        self.assertEqual(fr.workflow_problems(files, states, '.github/workflows/crosshatch-release.yml'), [])


class RefTest(unittest.TestCase):
    def setUp(self):
        self.project = TempProject()
        self.project.git('checkout', '-qb', 'side')
        (self.project.dir / 'x').write_text('x')
        self.project.git('add', 'x')
        self.project.git('-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-qm', 'side')
        self.states = self.project.dir / 'states.tsv'
        self.states.write_text('')
        (self.project.dir / '.github' / 'workflows').mkdir(parents=True)
        (self.project.dir / '.github' / 'workflows' / 'ci.yml').write_text('on: pull_request\n')

    def tearDown(self):
        self.project.close()

    def preflight(self, dry_run, dispatch_ref='refs/heads/develop'):
        return quiet(fr.main, ['preflight', '--repo-dir', str(self.project.dir), '--develop-ref', 'develop',
                               '--dispatch-ref', dispatch_ref, '--dry-run', dry_run,
                               '--workflow-states', str(self.states), '--workflow-ref', 'o/r/.github/workflows/me.yml@x',
                               '--repository', 'o/r', '--checkout', str(self.project.dir)])

    def test_publish_needs_develop_or_an_ancestor(self):
        self.assertEqual(self.preflight('false'), 1)  # HEAD is the side commit
        self.assertEqual(self.preflight('true'), 0)
        self.project.git('checkout', '-q', 'develop')
        self.assertEqual(self.preflight('false'), 0)
        self.assertEqual(self.preflight('false', 'refs/heads/side'), 1)
        self.assertEqual(self.preflight('true', 'refs/heads/side'), 0)

    def test_active_release_workflow_fails_even_in_a_dry_run(self):
        self.project.git('checkout', '-q', 'develop')
        (self.project.dir / '.github' / 'workflows' / 'release.yml').write_text(RELEASE_ON)
        self.assertEqual(self.preflight('true'), 1)
        self.states.write_text('.github/workflows/release.yml\tdisabled_manually\n')
        self.assertEqual(self.preflight('false'), 0)

    def test_unknown_develop_ref_is_a_setup_error(self):
        self.assertEqual(self.preflight_with_develop('nope'), 2)

    def preflight_with_develop(self, develop):
        return quiet(fr.main, ['preflight', '--repo-dir', str(self.project.dir), '--develop-ref', develop,
                               '--dry-run', 'true', '--workflow-ref', 'o/r/.github/workflows/me.yml@x',
                               '--repository', 'o/r', '--checkout', str(self.project.dir)])


class PublishTest(unittest.TestCase):
    PLAN = {'tag': '1.6.5-ch.4', 'build_number': 4, 'base_version': '1.6.5', 'commit': 'abc', 'envs': [{}],
            'firmware': [{'asset': 'crosspoint-1.6.5-ch.4-x4pro.bin', 'size': 1234, 'sha256': 'f' * 64}],
            'packages': []}

    def test_recheck(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            fr.save_plan(tmp / 'plan.json', self.PLAN)
            tags = tmp / 'tags.txt'
            args = ['recheck', '--plan', str(tmp / 'plan.json'), '--tags', str(tags)]
            with mock.patch.dict(os.environ, {'GITHUB_OUTPUT': str(tmp / 'out')}):
                tags.write_text('1.6.5-ch.3\nv1.5.0\n')
                self.assertEqual(quiet(fr.main, args), 0)
                self.assertEqual((tmp / 'out').read_text(), 'tag=1.6.5-ch.4\ncommit=abc\n')
                tags.write_text('1.6.5-ch.3\n1.7.0-ch.4\n')
                self.assertEqual(quiet(fr.main, args), 1)
                tags.write_text('1.6.5-ch.9\n')
                self.assertEqual(quiet(fr.main, args), 1)

    def test_notes_and_assets(self):
        text = fr.render_notes(self.PLAN)
        self.assertIn('No first-party games', text)
        self.assertIn('`crosspoint-1.6.5-ch.4-x4pro.bin` | 1,234 | `' + 'f' * 64, text)
        plan = dict(self.PLAN, packages=[{'asset': 'dots.cpgame', 'package_hash': '0123456789abcdef', 'size': 9,
                                          'sha256': 'e' * 64}])
        self.assertIn('| `dots.cpgame` | `0123456789abcdef` |', fr.render_notes(plan))
        self.assertEqual(fr.expected_assets(plan), ['crosspoint-1.6.5-ch.4-x4pro.bin\t1234', 'dots.cpgame\t9'])

    def test_unreadable_plan_is_a_setup_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            fr.save_plan(pathlib.Path(tmp) / 'plan.json', {'tag': 'x'})
            self.assertEqual(quiet(fr.main, ['expected-assets', '--plan', f'{tmp}/plan.json']), 2)
            self.assertEqual(quiet(fr.main, ['expected-assets', '--plan', f'{tmp}/missing.json']), 2)

    def test_notes_need_checked_firmware(self):
        with tempfile.TemporaryDirectory() as tmp:
            fr.save_plan(pathlib.Path(tmp) / 'plan.json', dict(self.PLAN, firmware=[]))
            self.assertEqual(quiet(fr.main, ['notes', '--plan', f'{tmp}/plan.json', '--out', f'{tmp}/notes.md']), 2)


if __name__ == '__main__':
    unittest.main()

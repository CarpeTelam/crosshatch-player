#!/usr/bin/env python3
"""
Tests for dev_setup.py and the SessionStart hook that runs it. Standard library only, nothing installed:

    python3 scripts/dev_setup_test.py [-v]

Each case puts fake `git`, `uv`, `dpkg-query`, `apt-get`, and `flock` (and, once uv installs it, a fake `pio`) on
PATH, with a temp HOME and a temp proxy CA, runs main(), and asserts the exit code, the commands run (each fake logs
its arguments), and the CA bundles. The fakes keep their state in one JSON file. The fake pio fails like the real one
behind the proxy: without the proxy CA in the uv tool's bundle it fails before building the penv, and without the CA
and the pioarduino pin in the penv it fails after building it.
"""

import contextlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
HOOK = HERE.parent / '.claude' / 'hooks' / 'session-start.sh'
sys.path.insert(0, str(HERE))

import dev_setup  # noqa: E402

PUBLIC_CERT = '-----BEGIN CERTIFICATE-----\nUFVCTElDUk9PVA==\n-----END CERTIFICATE-----\n'
PROXY_CERT = '-----BEGIN CERTIFICATE-----\nUFJPWFlDQQ==\n-----END CERTIFICATE-----\n'
PROXY_BODY = 'UFJPWFlDQQ=='

# The fakes' shared code. Each fake tool is a script that imports it and calls main(<tool name>).
FAKE_MODULE = r'''
import json, os, pathlib, re, sys

STATE = pathlib.Path(os.environ['FAKE_STATE'])
HOME = pathlib.Path(os.environ['HOME'])
HERE = pathlib.Path(__file__).resolve().parent
TOOL_DIR = HOME / '.local' / 'share' / 'uv' / 'tools'
TOOL_BIN = HOME / '.local' / 'bin'
PENV = HOME / '.platformio' / 'penv'
PUBLIC_CERT = %(public)r
PROXY_BODY = %(proxy_body)r


def make_tool(path, tool):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f'#!{sys.executable}\nimport sys\nsys.path.insert(0, {str(HERE)!r})\nimport fake\n'
                    f'fake.main({tool!r})\n')
    path.chmod(0o755)


def bundle(prefix):
    return prefix / 'certifi' / 'cacert.pem'


def make_python(prefix):
    make_tool(prefix / 'bin' / 'python', 'python:' + str(prefix))
    bundle(prefix).parent.mkdir(parents=True, exist_ok=True)
    bundle(prefix).write_text(PUBLIC_CERT)


def has_ca(prefix):
    return bundle(prefix).is_file() and PROXY_BODY in bundle(prefix).read_text()


def main(tool):
    args = sys.argv[1:]
    with open(os.environ['FAKE_LOG'], 'a') as log:
        log.write(json.dumps([tool.split(':')[0], *args]) + '\n')
    s = json.loads(STATE.read_text())
    code = 0
    if tool.startswith('python:'):
        prefix = pathlib.Path(tool.split(':', 1)[1])
        if 'certifi' in args[1]:
            print(bundle(prefix))
        else:
            print(s['penv_pioarduino'])
    elif tool == 'uv':
        if args[:2] == ['tool', 'list']:
            for name, version in s['uv_tools'].items():
                print(f'{name} v{version}\n- {name}')
        elif args[:2] == ['tool', 'dir']:
            print(TOOL_BIN if '--bin' in args else TOOL_DIR)
        elif args[:2] == ['tool', 'install']:
            name = re.split('[=<>]', args[2])[0]
            s['uv_tools'][name] = {'pioarduino': '6.1.19', 'clang-format': '21.1.8'}[name]
            if name == 'pioarduino':
                make_python(TOOL_DIR / 'pioarduino')
                make_tool(TOOL_BIN / 'pio', 'pio')
        elif args[:2] == ['pip', 'install']:
            s['penv_pioarduino'] = args[-1].split('==')[1]
    elif tool == 'pio':
        s['installs'] += 1
        if s['pio'] in ('ok', 'fail'):
            if not PENV.exists():
                make_python(PENV)
            code = 0 if s['pio'] == 'ok' else 1
        elif not has_ca(TOOL_DIR / 'pioarduino'):  # 'tls': the platform download fails first
            code = 1
        else:
            if not PENV.exists():
                make_python(PENV)
            code = 0 if has_ca(PENV) and s['penv_pioarduino'] == '6.1.19' else 1
    elif tool == 'git':
        if args[0] == '-C':
            args = args[2:]
        if args[:2] == ['submodule', 'status']:
            print(' abc123 freeink-sdk (heads/main)' if s['submodules'] else '-abc123 freeink-sdk')
        elif args[:2] == ['submodule', 'update']:
            s['submodules'] = True
        elif args[0] == 'rev-parse':
            print('true' if s['shallow'] else 'false')
        elif args[:2] == ['fetch', '--unshallow']:
            s['shallow'] = False
        elif args[:2] == ['remote', 'get-url']:
            if s['upstream']:
                print('https://example.invalid/upstream.git')
            else:
                print("error: No such remote 'upstream'", file=sys.stderr)
                code = 2
        elif args[:2] == ['remote', 'add']:
            s['upstream'] = True
        elif args[0] == 'config':
            if len(args) == 3:
                s['driver'] = True
            elif s['driver']:
                print('true')
            else:
                code = 1
    elif tool == 'dpkg-query':
        if args[-1] in s['packages']:
            print('install ok installed', end='')
        else:
            code = 1
    elif tool == 'apt-get':
        if args[0] == 'install':
            s['packages'] += [arg for arg in args[1:] if not arg.startswith('-')]
    STATE.write_text(json.dumps(s))
    sys.exit(code)
'''

COLD = {
    'uv_tools': {},
    'penv_pioarduino': '6.1.20',
    'installs': 0,
    'pio': 'tls',
    'submodules': False,
    'shallow': True,
    'upstream': False,
    'driver': False,
    'packages': [],
}
WARM_TOOLS = ('git', 'uv', 'dpkg-query', 'apt-get', 'flock')


class SetupTestBase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        self.bin = self.dir / 'bin'
        self.bin.mkdir()
        self.home = self.dir / 'home'
        self.home.mkdir()
        self.repo = self.dir / 'repo'
        self.repo.mkdir()
        (self.repo / 'platformio.ini').write_text('[env:x4pro]\n')
        self.ca = self.dir / 'proxy-ca.crt'
        self.ca.write_text(PUBLIC_CERT + PROXY_CERT)
        self.state = self.dir / 'state.json'
        self.log = self.dir / 'calls.log'
        self.log.touch()
        (self.bin / 'fake.py').write_text(FAKE_MODULE % {'public': PUBLIC_CERT, 'proxy_body': PROXY_BODY})
        saved = {name: os.environ.get(name) for name in ('PATH', 'HOME', 'FAKE_STATE', 'FAKE_LOG')}
        self.addCleanup(self.restore_environment, saved)
        os.environ.update(PATH=str(self.bin), HOME=str(self.home), FAKE_STATE=str(self.state),
                          FAKE_LOG=str(self.log))
        for name, value in (('is_root', mock.Mock(return_value=True)), ('BUILD_LOCK', str(self.dir / 'build.lock'))):
            patch = mock.patch.object(dev_setup, name, value)
            patch.start()
            self.addCleanup(patch.stop)
        self.set_state()
        self.fakes(*WARM_TOOLS)

    @staticmethod
    def restore_environment(saved):
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value

    def tearDown(self):
        self.tmp.cleanup()

    def set_state(self, **changes):
        self.state.write_text(json.dumps({**COLD, **changes}))

    def read_state(self):
        return json.loads(self.state.read_text())

    def fakes(self, *tools):
        for tool in tools:
            path = self.bin / tool
            path.write_text(f'#!{sys.executable}\nimport sys\nsys.path.insert(0, {str(self.bin)!r})\nimport fake\n'
                            f'fake.main({tool!r})\n')
            path.chmod(0o755)

    def remove(self, tool):
        (self.bin / tool).unlink()

    def run_setup(self, *args, proxy_ca=None):
        """main()'s exit code, with stdout and stderr captured; the calls log starts empty for each run."""
        self.log.write_text('')
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = dev_setup.main(['--root', str(self.repo), '--proxy-ca', str(proxy_ca or self.ca), *args])
        self.out, self.err = out.getvalue(), err.getvalue()
        return code

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def ran(self, *prefix):
        return [call for call in self.calls() if call[:len(prefix)] == list(prefix)]

    def tool_bundle(self):
        return self.home / '.local' / 'share' / 'uv' / 'tools' / 'pioarduino' / 'certifi' / 'cacert.pem'

    def penv_bundle(self):
        return self.home / '.platformio' / 'penv' / 'certifi' / 'cacert.pem'

    def stamp(self):
        return self.home / '.platformio' / dev_setup.STAMP_NAME


class ColdAndWarmTest(SetupTestBase):
    def test_cold_container_runs_every_step_and_patches_the_penv_before_the_retry(self):
        self.assertEqual(self.run_setup(), 0, self.err)
        self.assertTrue(self.ran('git', '-C', str(self.repo), 'submodule', 'update', '--init', '--recursive'))
        self.assertEqual([call[3] for call in self.ran('uv', 'tool', 'install')],
                         ['pioarduino==6.1.19', 'clang-format>=21,<22'])
        self.assertTrue(self.ran('git', '-C', str(self.repo), 'fetch', '--unshallow'))
        self.assertTrue(self.ran('git', '-C', str(self.repo), 'remote', 'add', 'upstream', dev_setup.UPSTREAM_URL))
        self.assertTrue(self.ran('git', '-C', str(self.repo), 'fetch', '--no-tags', 'upstream',
                                 dev_setup.UPSTREAM_REFSPEC))
        self.assertTrue(self.ran('git', '-C', str(self.repo), 'config', 'merge.ours.driver', 'true'))
        self.assertEqual(self.ran('apt-get', 'install'),
                         [['apt-get', 'install', '-y', '--no-install-recommends', 'libsdl2-dev', 'libpcre3']])
        # The first install fails, the penv is pinned, and the retry passes.
        calls = self.calls()
        installs = [i for i, call in enumerate(calls) if call[:3] == ['pio', 'pkg', 'install']]
        pins = [i for i, call in enumerate(calls) if call[:3] == ['uv', 'pip', 'install']]
        self.assertEqual(len(installs), 2)
        self.assertEqual(len(pins), 1)
        self.assertLess(installs[0], pins[0])
        self.assertLess(pins[0], installs[1])
        self.assertEqual(calls[installs[0]], ['pio', 'pkg', 'install', '-e', 'x4pro', '-e', 'default'])
        self.assertEqual(calls[pins[0]][-1], 'pioarduino==6.1.19')
        self.assertEqual(self.tool_bundle().read_text().count(PROXY_BODY), 1)
        self.assertEqual(self.penv_bundle().read_text().count(PROXY_BODY), 1)
        # Only the proxy certificate was missing; the public one already in the bundle is not appended again.
        self.assertEqual(self.penv_bundle().read_text().count('UFVCTElDUk9PVA=='), 1)
        self.assertFalse(self.ran('flock'))

    def test_warm_container_only_checks_and_appends_no_second_ca(self):
        self.assertEqual(self.run_setup(), 0, self.err)
        tool_bundle, penv_bundle = self.tool_bundle().read_text(), self.penv_bundle().read_text()
        self.assertEqual(self.run_setup(), 0, self.err)
        self.assertEqual(self.tool_bundle().read_text(), tool_bundle)
        self.assertEqual(self.penv_bundle().read_text(), penv_bundle)
        self.assertEqual(self.penv_bundle().read_text().count(PROXY_BODY), 1)
        for prefix in (('uv', 'tool', 'install'), ('uv', 'pip', 'install'), ('apt-get',)):
            self.assertFalse(self.ran(*prefix), prefix)
        for git_args in (('submodule', 'update'), ('fetch', '--unshallow'), ('remote', 'add'),
                         ('config', 'merge.ours.driver', 'true')):
            self.assertFalse(self.ran('git', '-C', str(self.repo), *git_args), git_args)
        # The stamp the first run wrote skips the install.
        self.assertFalse(self.ran('pio'))
        self.assertIn('pio packages: ok: packages installed for this platformio.ini', self.out)

    def test_a_changed_platformio_ini_runs_the_install_again(self):
        self.assertEqual(self.run_setup(), 0, self.err)
        self.assertTrue(self.stamp().is_file())
        (self.repo / 'platformio.ini').write_text('[env:x4pro]\nplatform = newer\n')
        self.assertEqual(self.run_setup(), 0, self.err)
        self.assertEqual(len(self.ran('pio', 'pkg', 'install')), 1)

    def test_the_install_waits_for_the_build_lock(self):
        self.set_state(pio='ok')
        holder = subprocess.Popen(
            [sys.executable, '-c',
             'import fcntl, json, sys, time\n'
             'lock = open(sys.argv[1], "a")\n'
             'fcntl.flock(lock, fcntl.LOCK_EX)\n'
             'print("locked", flush=True)\n'
             'time.sleep(1)\n'
             'open(sys.argv[2], "a").write(json.dumps(["holder", "released"]) + "\\n")\n',
             dev_setup.BUILD_LOCK, str(self.log)],
            stdout=subprocess.PIPE, text=True)
        self.addCleanup(holder.wait)
        self.assertEqual(holder.stdout.readline().strip(), 'locked')
        self.assertEqual(self.run_setup(), 0, self.err)
        calls = self.calls()
        self.assertLess(calls.index(['holder', 'released']), calls.index(['pio', 'pkg', 'install', '-e', 'x4pro', '-e',
                                                                           'default']))
        holder.stdout.close()

    def test_a_submodule_at_another_commit_is_left_alone(self):
        self.set_state(submodules=True)
        self.assertEqual(self.run_setup(), 0, self.err)
        self.assertFalse(self.ran('git', '-C', str(self.repo), 'submodule', 'update'))


class FailureTest(SetupTestBase):
    def test_a_retry_that_fails_again_exits_2_and_the_other_steps_still_run(self):
        self.set_state(pio='fail')
        self.assertEqual(self.run_setup(), 2)
        self.assertIn('error: pio packages: pio pkg install failed again (1)', self.err)
        self.assertIn('1 step(s) failed: pio packages', self.err)
        self.assertEqual(len(self.ran('pio', 'pkg', 'install')), 2)
        self.assertEqual(self.penv_bundle().read_text().count(PROXY_BODY), 1)
        self.assertTrue(self.ran('uv', 'pip', 'install'))
        self.assertTrue(self.ran('git', '-C', str(self.repo), 'config', 'merge.ours.driver', 'true'))
        self.assertTrue(self.ran('apt-get', 'install'))

    def test_without_uv_the_tool_steps_fail_naming_uv_and_the_rest_still_run(self):
        self.remove('uv')
        self.assertEqual(self.run_setup(), 2)
        self.assertIn('error: uv tools: uv is not on PATH', self.err)
        self.assertIn('error: tool CA: uv is not on PATH', self.err)
        self.assertIn('error: pio packages: pio is not installed', self.err)
        self.assertIn('uv tool install pioarduino==6.1.19', self.err)
        self.assertTrue(self.ran('git', '-C', str(self.repo), 'submodule', 'update', '--init', '--recursive'))
        self.assertTrue(self.ran('git', '-C', str(self.repo), 'config', 'merge.ours.driver', 'true'))
        self.assertTrue(self.ran('apt-get', 'install'))

    def test_a_failed_git_step_exits_2(self):
        self.set_state(pio='ok')
        self.remove('git')
        self.assertEqual(self.run_setup(), 2)
        self.assertIn('error: submodules: cannot run git', self.err)
        self.assertIn('error: git: cannot run git', self.err)


class ProxyCaTest(SetupTestBase):
    def test_without_the_proxy_ca_no_bundle_is_touched(self):
        self.set_state(pio='ok')
        self.assertEqual(self.run_setup(proxy_ca=self.dir / 'missing.crt'), 0, self.err)
        self.assertEqual(self.tool_bundle().read_text(), PUBLIC_CERT)
        self.assertEqual(self.penv_bundle().read_text(), PUBLIC_CERT)
        self.assertEqual(len(self.ran('pio', 'pkg', 'install')), 1)

    def test_only_missing_certificates_are_appended_and_whitespace_does_not_matter(self):
        bundle = self.dir / 'bundle.pem'
        bundle.write_text('# a comment\n' + PUBLIC_CERT.replace('UFVC', 'UFVC\n'))  # no trailing newline
        bundle.write_text(bundle.read_text().rstrip('\n'))
        with contextlib.redirect_stdout(io.StringIO()):
            dev_setup.append_proxy_ca('test', self.ca, bundle)
            dev_setup.append_proxy_ca('test', self.ca, bundle)
        text = bundle.read_text()
        self.assertEqual(text.count('BEGIN CERTIFICATE'), 2)
        self.assertEqual(text.count(PROXY_BODY), 1)
        self.assertIn('-----END CERTIFICATE-----\n-----BEGIN CERTIFICATE-----', text)


class SystemPackagesTest(SetupTestBase):
    def setUp(self):
        super().setUp()
        self.set_state(pio='ok')

    def test_not_root_skips_the_system_packages_with_a_note(self):
        with mock.patch.object(dev_setup, 'is_root', return_value=False):
            self.assertEqual(self.run_setup(), 0, self.err)
        self.assertIn('system packages: skipped: not root', self.out)
        self.assertFalse(self.ran('apt-get'))
        self.assertFalse(self.ran('dpkg-query'))

    def test_no_apt_get_skips_the_system_packages_with_a_note(self):
        self.remove('apt-get')
        self.assertEqual(self.run_setup(), 0, self.err)
        self.assertIn('system packages: skipped: no apt-get', self.out)

    def test_only_a_missing_package_is_installed(self):
        self.set_state(pio='ok', packages=['libsdl2-dev'])
        self.assertEqual(self.run_setup(), 0, self.err)
        self.assertEqual(self.ran('apt-get', 'install'),
                         [['apt-get', 'install', '-y', '--no-install-recommends', 'libpcre3']])


class WarmTest(SetupTestBase):
    def setUp(self):
        super().setUp()
        self.set_state(pio='ok')

    def wait_for_flock_calls(self, count):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            calls = self.ran('flock')
            if len(calls) >= count:
                return calls
            time.sleep(0.05)
        self.fail(f'expected {count} flock calls, got {self.ran("flock")}')

    def test_warm_starts_both_builds_under_the_fixed_locks(self):
        self.assertEqual(self.run_setup('--warm'), 0, self.err)
        calls = self.wait_for_flock_calls(2)
        pio = self.home / '.local' / 'bin' / 'pio'
        self.assertCountEqual(calls, [
            ['flock', dev_setup.BUILD_LOCK, 'sh', '-c', f'{pio} run -e x4pro'],
            ['flock', '/tmp/crosshatch-hosttest.lock', 'sh', '-c', dev_setup.HOSTTEST_BUILD],
        ])
        self.assertIn('cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release', dev_setup.HOSTTEST_BUILD)
        logs = self.home / '.cache' / 'crosshatch'
        self.assertTrue((logs / 'warm-x4pro.log').is_file())
        self.assertTrue((logs / 'warm-hosttest.log').is_file())
        # A second run appends, so a warm build still running keeps its log.
        self.assertEqual(self.run_setup('--warm'), 0, self.err)
        self.wait_for_flock_calls(2)
        self.assertEqual((logs / 'warm-x4pro.log').read_text().count('=== '), 2)

    def test_warm_spawn_failure_exits_2(self):
        self.remove('flock')
        self.assertEqual(self.run_setup('--warm'), 2)
        self.assertIn('error: warm: cannot start', self.err)

    def test_cli_exit_code(self):
        # No uv, so no pio: the subprocess never takes the real build lock.
        self.remove('flock')
        self.remove('uv')
        env = dict(os.environ)
        proc = subprocess.run(
            [sys.executable, str(HERE / 'dev_setup.py'), '--warm', '--root', str(self.repo), '--proxy-ca', str(self.ca)],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        # Not root under CI, which only skips the system packages; the missing flock alone fails.
        self.assertEqual(proc.returncode, 2, proc.stderr)
        self.assertIn('error: warm: cannot start', proc.stderr)


class LockPathTest(unittest.TestCase):
    def test_the_locks_are_the_fixed_paths_agents_md_gives(self):
        self.assertEqual(dev_setup.BUILD_LOCK, '/tmp/crosshatch-build.lock')
        self.assertEqual(dev_setup.HOSTTEST_LOCK, '/tmp/crosshatch-hosttest.lock')


class HookTest(unittest.TestCase):
    """The hook runs the project's dev_setup.py with --warm only in a cloud session, and always exits 0."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.project = pathlib.Path(self.tmp.name)
        (self.project / 'scripts').mkdir()
        self.marker = self.project / 'ran'
        (self.project / 'scripts' / 'dev_setup.py').write_text(
            'import pathlib, sys\n'
            f'pathlib.Path({str(self.marker)!r}).write_text(" ".join(sys.argv[1:]))\n'
            'sys.exit(2)\n')

    def run_hook(self, remote):
        env = {name: value for name, value in os.environ.items() if name != 'CLAUDE_CODE_REMOTE'}
        env['CLAUDE_PROJECT_DIR'] = str(self.project)
        if remote is not None:
            env['CLAUDE_CODE_REMOTE'] = remote
        return subprocess.run(['sh', str(HOOK)], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def test_outside_cloud_the_hook_runs_nothing(self):
        for remote in (None, 'false', ''):
            proc = self.run_hook(remote)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertFalse(self.marker.exists(), remote)

    def test_in_cloud_a_failed_setup_still_exits_0(self):
        proc = self.run_hook('true')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(self.marker.read_text(), '--warm')
        self.assertIn('scripts/dev_setup.py failed (2)', proc.stderr)

    def test_settings_register_the_hook_with_a_900_s_timeout(self):
        settings = json.loads((HERE.parent / '.claude' / 'settings.json').read_text())
        hooks = [hook for entry in settings['hooks']['SessionStart'] for hook in entry['hooks']]
        self.assertEqual(len(hooks), 1)
        self.assertIn('.claude/hooks/session-start.sh', hooks[0]['command'])
        self.assertEqual(hooks[0]['timeout'], 900)
        self.assertIn('CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH', settings['env'])


if __name__ == '__main__':
    unittest.main()

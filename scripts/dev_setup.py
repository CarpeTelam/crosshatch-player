#!/usr/bin/env python3
"""
Set up a development container for this repository: the steps AGENTS.md used to describe for a hand-run setup.

A cloud session runs it from the SessionStart hook (.claude/hooks/session-start.sh, only when CLAUDE_CODE_REMOTE is
true), synchronously, so the cached container keeps what it installs. Every step checks first and skips when its work
is already done, so a second run only checks. The steps, in order:

  1. submodules     `git submodule update --init --recursive` when a submodule is not checked out (`freeink-sdk/` is
                    empty in a fresh clone or worktree); a submodule at another commit is left alone.
  2. uv tools       `uv tool install pioarduino==6.1.19` and `clang-format>=21,<22` when missing or at another version
                    (pioarduino from PyPI: the agent proxy refuses GitHub archive URLs; ./bin/clang-format-fix needs 21).
  3. tool CA        appends the proxy CA (--proxy-ca, default /root/.ccr/ca-bundle.crt) to the certifi bundle of the
                    uv tool's own interpreter, each certificate only when the bundle lacks it; nothing when the proxy
                    CA file does not exist.
  4. pio packages   `pio pkg install -e x4pro -e default`. The espressif32 platform builds ~/.platformio/penv on the
                    first install and points SSL_CERT_FILE at the penv's certifi bundle, so that install fails TLS
                    behind the proxy; the penv then gets the proxy CA and `pioarduino==6.1.19` (its open-ended pin
                    otherwise resolves to a core whose tool-scons pin fails with "No module named
                    'SCons.Tool.FortranCommon'"), and the install is retried once. An existing penv gets both first.
                    The install holds /tmp/crosshatch-build.lock (an install beside a running build left the xtensa
                    toolchain half copied), waiting at most 120 s for it, and then writes ~/.platformio/crosshatch-pkg-install.stamp; while the stamp
                    matches platformio.ini, a later run skips the install, which would otherwise reinstall the C3
                    tools (about 45 s) on every run until a `default` build has run.
  5. git            the `upstream` remote and `merge.ours.driver`, then `git fetch --unshallow` in a shallow clone and
                    a fetch of upstream's develop (the AGENTS.md policy for scripts/check_upstream_touches.py and merges).
  6. system         `apt-get install libsdl2-dev libpcre3` (noninteractive) (the simulator and the packaged cppcheck of `pio check`,
                    as CI installs them) when one is missing; skipped with a note when not root or without apt-get.
  7. --warm         starts two detached builds and returns at once: `pio run -e x4pro` under
                    /tmp/crosshatch-build.lock and the host-test build under /tmp/crosshatch-hosttest.lock, the locks
                    AGENTS.md gives every agent, so an agent's first build waits for the warm one. A build whose lock
                    is already held is skipped (`flock -n`), never queued. Their logs go to
                    ~/.cache/crosshatch/warm-x4pro.log and warm-hosttest.log, appended to, each run headed by a line.

A failed step is reported and the remaining steps still run.

Exit 0: every step passed or was skipped. Exit 2: at least one step failed (each is printed as `error: <step>: ...`,
with the tail of the failing command's output), or a warm build could not be started. There is no exit 1: this script
checks no rule.

Usage: python3 scripts/dev_setup.py [--warm] [--root <repository root>] [--proxy-ca <file>]
       python3 scripts/dev_setup_test.py                     # the script's own tests; standard library only

Outside a cloud session nothing runs it; run it by hand only when you want this machine set up the same way (it
installs uv tools, ~/.platformio, and, as root, two apt packages).
"""

import argparse
import contextlib
import fcntl
import hashlib
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import time

import fork_common
from fork_common import SetupError

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROXY_CA = pathlib.Path('/root/.ccr/ca-bundle.crt')
UPSTREAM_URL = 'https://github.com/crosspoint-reader/crosspoint-reader.git'
UPSTREAM_REFSPEC = '+refs/heads/develop:refs/remotes/upstream/develop'

PIOARDUINO_VERSION = '6.1.19'
# (tool name, `uv tool install` requirement, the version prefix `uv tool list` must show)
UV_TOOLS = (
    ('pioarduino', f'pioarduino=={PIOARDUINO_VERSION}', f'v{PIOARDUINO_VERSION}'),
    ('clang-format', 'clang-format>=21,<22', 'v21.'),
)
PIO_ENVS = ('x4pro', 'default')
SYSTEM_PACKAGES = ('libsdl2-dev', 'libpcre3')
APT_ENV = {'DEBIAN_FRONTEND': 'noninteractive'}  # no debconf prompt

BUILD_LOCK = '/tmp/crosshatch-build.lock'
HOSTTEST_LOCK = '/tmp/crosshatch-hosttest.lock'
# Written in ~/.platformio after a passing install; a later run whose key matches skips the install, which otherwise
# reinstalls the C3 tools each time (about 45 s) until a `default` build has run.
STAMP_NAME = 'crosshatch-pkg-install.stamp'
BUILD_LOCK_WAIT_S = 120
LOCK_POLL_S = 0.2
# The warm processes started by this run (the tests wait for them).
spawned = []
HOSTTEST_BUILD = 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'

CERTIFI_WHERE = 'import certifi; print(certifi.where())'
PIOARDUINO_VERSION_QUERY = 'import importlib.metadata as m; print(m.version("pioarduino"))'
PEM_BLOCK = re.compile(rb'-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----', re.S)
OUTPUT_TAIL_LINES = 20


def read_text(path):
    """The text of path, or None when it cannot be read."""
    try:
        return path.read_text()
    except OSError:
        return None


def try_lock(lock):
    """Take an exclusive flock on the open file lock without waiting; False when another process holds it."""
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return False
    return True


@contextlib.contextmanager
def build_lock():
    """Hold BUILD_LOCK, the lock every pio command takes (AGENTS.md), waiting up to BUILD_LOCK_WAIT_S for a running
    build: the hook has a 900 s timeout, and a kill mid-install would leave the packages half installed."""
    try:
        lock = open(BUILD_LOCK, 'a')
    except OSError as exc:
        raise SetupError(f'cannot open {BUILD_LOCK}: {exc}')
    with lock:
        deadline = time.monotonic() + BUILD_LOCK_WAIT_S
        while not try_lock(lock):
            if time.monotonic() >= deadline:
                raise SetupError(f'the build lock {BUILD_LOCK} is busy (a build is running); rerun '
                                 '`python3 scripts/dev_setup.py` when it is done')
            time.sleep(LOCK_POLL_S)
        yield


def lock_held(path):
    """True when another process holds the flock on path."""
    try:
        with open(path, 'a') as lock:
            return not try_lock(lock)  # closing the file releases a lock taken here
    except OSError as exc:
        raise SetupError(f'cannot open {path}: {exc}')


def is_root():
    return os.geteuid() == 0


def say(step, message):
    print(f'{step}: {message}', flush=True)


def tail(text):
    lines = text.strip().splitlines()[-OUTPUT_TAIL_LINES:]
    return '\n'.join(f'    {line}' for line in lines)


def run(command, cwd=None, env=None):
    """Run command, capturing stdout and stderr together; return (exit code, output). A missing tool is a SetupError.

    stdin is /dev/null: the hook's stdin carries its JSON input, which no command may read. env adds to os.environ.
    """
    try:
        proc = subprocess.run(command, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, errors='replace',
                              env=None if env is None else {**os.environ, **env})
    except OSError as exc:
        raise SetupError(f'cannot run {command[0]}: {exc}')
    return proc.returncode, proc.stdout


def run_checked(command, cwd=None, env=None):
    """run(), with a non-zero exit code a SetupError carrying the command and the tail of its output."""
    code, output = run(command, cwd=cwd, env=env)
    if code != 0:
        raise SetupError(f'{shlex.join(str(part) for part in command)} failed ({code}):\n{tail(output)}')
    return output


def require_uv():
    uv = shutil.which('uv')
    if uv is None:
        raise SetupError('uv is not on PATH; install it (https://docs.astral.sh/uv/) and run this again')
    return uv


def uv_tool_dir(uv, *flags):
    return pathlib.Path(run_checked([uv, 'tool', 'dir', *flags]).strip())


def find_pio():
    """The uv tool's pio when uv knows it, else pio on PATH, else None."""
    uv = shutil.which('uv')
    if uv is not None:
        code, output = run([uv, 'tool', 'dir', '--bin'])
        if code == 0:
            candidate = pathlib.Path(output.strip()) / 'pio'
            if candidate.is_file():
                return str(candidate)
    return shutil.which('pio')


def certifi_bundle(python):
    """The certifi bundle path the interpreter `python` uses."""
    return pathlib.Path(run_checked([str(python), '-c', CERTIFI_WHERE]).strip())


def pem_bodies(data):
    """The base64 bodies of the PEM certificates in data, with whitespace removed, in order."""
    return [re.sub(rb'\s+', b'', body) for body in PEM_BLOCK.findall(data)]


def append_proxy_ca(step, proxy_ca, bundle):
    """Append each certificate of proxy_ca that bundle lacks; say what was done. No proxy_ca file: touch nothing."""
    if not proxy_ca.is_file():
        say(step, f'skipped: no proxy CA at {proxy_ca}')
        return
    try:
        ca_data = proxy_ca.read_bytes()
        bundle_data = bundle.read_bytes()
    except OSError as exc:
        raise SetupError(f'cannot read a CA bundle: {exc}')
    present = set(pem_bodies(bundle_data))
    missing = [match.group(0) for match in PEM_BLOCK.finditer(ca_data)
               if re.sub(rb'\s+', b'', match.group(1)) not in present]
    if not missing:
        say(step, f'ok: {bundle} has the proxy CA')
        return
    addition = b'\n'.join(missing) + b'\n'
    if bundle_data and not bundle_data.endswith(b'\n'):
        addition = b'\n' + addition
    # A new file renamed over the bundle, never an append: uv installs certifi as a hardlink into its cache, so an
    # in-place write would also change the cached copy and every environment that links to it.
    temp = bundle.with_name(f'.{bundle.name}.crosshatch-tmp')
    try:
        temp.write_bytes(bundle_data + addition)
        shutil.copymode(bundle, temp)
        os.replace(temp, bundle)
    except OSError as exc:
        temp.unlink(missing_ok=True)
        raise SetupError(f'cannot add the proxy CA to {bundle}: {exc}')
    say(step, f'appended {len(missing)} certificate(s) from {proxy_ca} to {bundle}')


class Setup:
    def __init__(self, root, proxy_ca):
        self.root = pathlib.Path(root)
        self.proxy_ca = pathlib.Path(proxy_ca)
        self.penv_python = pathlib.Path.home() / '.platformio' / 'penv' / 'bin' / 'python'
        self.failed = []

    def step(self, name, action):
        """Run one step; a SetupError is reported and recorded, and the next step still runs."""
        try:
            action(name)
        except SetupError as exc:
            print(f'error: {name}: {exc}', file=sys.stderr, flush=True)
            self.failed.append(name)

    # 1
    def submodules(self, step):
        status = fork_common.git_text('submodule', 'status', '--recursive', cwd=self.root)
        missing = [line.split()[1] for line in status.splitlines() if line.startswith('-')]
        if not missing:
            say(step, 'ok: every submodule is checked out')
            return
        say(step, f'checking out {", ".join(missing)}')
        fork_common.git('submodule', 'update', '--init', '--recursive', cwd=self.root)

    # 2
    def uv_tools(self, step):
        uv = require_uv()
        listing = run_checked([uv, 'tool', 'list'])
        installed = dict(re.findall(r'^(\S+) (v\S+)$', listing, re.M))
        for name, requirement, version in UV_TOOLS:
            if installed.get(name, '').startswith(version):
                say(step, f'ok: {name} {installed[name]}')
                continue
            say(step, f'installing {requirement}')
            run_checked([uv, 'tool', 'install', requirement])

    # 3
    def tool_ca(self, step):
        if not self.proxy_ca.is_file():
            say(step, f'skipped: no proxy CA at {self.proxy_ca}')
            return
        uv = require_uv()
        python = uv_tool_dir(uv) / 'pioarduino' / 'bin' / 'python'
        if not python.is_file():
            raise SetupError(f'no pioarduino uv tool interpreter at {python}')
        append_proxy_ca(step, self.proxy_ca, certifi_bundle(python))

    # 4
    def pio_packages(self, step):
        pio = find_pio()
        if pio is None:
            raise SetupError('pio is not installed; the uv tools step installs it with uv (uv tool install '
                             f'pioarduino=={PIOARDUINO_VERSION})')
        prepared = False
        if self.penv_python.is_file():
            self.prepare_penv(step)
            prepared = True
        stamp = self.penv_python.parents[2] / STAMP_NAME
        key = self.stamp_key()
        if prepared and read_text(stamp) == key:
            say(step, f'ok: packages installed for this platformio.ini ({stamp})')
            return
        command = [pio, 'pkg', 'install', *(arg for env in PIO_ENVS for arg in ('-e', env))]
        # Under the build lock: an install beside a running build can leave a toolchain half copied.
        with build_lock():
            say(step, f'running {shlex.join(command[1:])}')
            code, output = run(command, cwd=self.root)
            if code != 0:
                if not self.penv_python.is_file():
                    raise SetupError(f'pio pkg install failed ({code}) and left no penv:\n{tail(output)}')
                say(step, f'pio pkg install failed ({code}); patching the penv and retrying once')
                self.prepare_penv(step)
                prepared = True
                code, output = run(command, cwd=self.root)
                if code != 0:
                    raise SetupError(f'pio pkg install failed again ({code}):\n{tail(output)}')
        if not prepared:
            self.prepare_penv(step)
        try:
            stamp.write_text(key)
        except OSError as exc:
            raise SetupError(f'cannot write {stamp}: {exc}')
        say(step, 'ok: packages installed')

    def stamp_key(self):
        """What the install stamp records: the envs, the pioarduino version, and a hash of platformio.ini."""
        try:
            ini = (self.root / 'platformio.ini').read_bytes()
        except OSError as exc:
            raise SetupError(f'cannot read platformio.ini: {exc}')
        return f'{" ".join(PIO_ENVS)} pioarduino {PIOARDUINO_VERSION} platformio.ini {hashlib.sha256(ini).hexdigest()}\n'

    def prepare_penv(self, step):
        """Give ~/.platformio/penv the proxy CA and the pioarduino pin."""
        if self.proxy_ca.is_file():
            append_proxy_ca(step, self.proxy_ca, certifi_bundle(self.penv_python))
        code, output = run([str(self.penv_python), '-c', PIOARDUINO_VERSION_QUERY])
        if code == 0 and output.strip() == PIOARDUINO_VERSION:
            say(step, f'ok: the penv has pioarduino {PIOARDUINO_VERSION}')
            return
        uv = require_uv()
        say(step, f'pinning pioarduino=={PIOARDUINO_VERSION} in the penv')
        run_checked([uv, 'pip', 'install', '--python', str(self.penv_python), f'pioarduino=={PIOARDUINO_VERSION}'])

    # 5
    def git_setup(self, step):
        # The local settings first, so a failed network fetch below still leaves them done.
        if fork_common.git('remote', 'get-url', 'upstream', cwd=self.root, ok_codes=(0, 2))[0] != 0:
            say(step, f'adding the upstream remote {UPSTREAM_URL}')
            fork_common.git('remote', 'add', 'upstream', UPSTREAM_URL, cwd=self.root)
        code, driver = fork_common.git('config', 'merge.ours.driver', cwd=self.root, ok_codes=(0, 1))
        if code != 0 or driver.decode().strip() != 'true':
            fork_common.git('config', 'merge.ours.driver', 'true', cwd=self.root)
        if fork_common.git_text('rev-parse', '--is-shallow-repository', cwd=self.root) == 'true':
            say(step, 'unshallowing the clone')
            fork_common.git('fetch', '--unshallow', cwd=self.root)
        fork_common.git('fetch', '--no-tags', 'upstream', UPSTREAM_REFSPEC, cwd=self.root)
        say(step, 'ok: full history, upstream/develop fetched, merge.ours.driver set')

    # 6
    def system_packages(self, step):
        if not is_root():
            say(step, f'skipped: not root, so {" and ".join(SYSTEM_PACKAGES)} are not installed')
            return
        if shutil.which('apt-get') is None:
            say(step, f'skipped: no apt-get, so {" and ".join(SYSTEM_PACKAGES)} are not installed')
            return
        missing = [package for package in SYSTEM_PACKAGES if not self.package_installed(package)]
        if not missing:
            say(step, f'ok: {" and ".join(SYSTEM_PACKAGES)} are installed')
            return
        install = ['apt-get', 'install', '-y', '--no-install-recommends', *missing]
        say(step, f'installing {" ".join(missing)}')
        code, _ = run(install, env=APT_ENV)
        if code != 0:  # a fresh image has no package lists yet
            run_checked(['apt-get', 'update'], env=APT_ENV)
            run_checked(install, env=APT_ENV)

    @staticmethod
    def package_installed(package):
        code, output = run(['dpkg-query', '-W', '-f=${Status}', package])
        return code == 0 and output.strip() == 'install ok installed'

    # 7
    def warm(self, step):
        logs = pathlib.Path.home() / '.cache' / 'crosshatch'
        try:
            logs.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            raise SetupError(f'cannot create {logs}: {exc}')
        pio = find_pio()
        if pio is not None:
            self.spawn(step, BUILD_LOCK, f'{shlex.quote(pio)} run -e x4pro', logs / 'warm-x4pro.log')
        self.spawn(step, HOSTTEST_LOCK, HOSTTEST_BUILD, logs / 'warm-hosttest.log')
        if pio is None:
            raise SetupError('pio is not installed, so the x4pro warm build was not started')

    def spawn(self, step, lock, command, log):
        """Start command under lock, detached, unless the lock is held: a running or waiting build (an agent's, or
        an earlier warm one) makes a warm one pointless, and queueing it would delay the agent's first build."""
        stamp = time.strftime('%Y-%m-%d %H:%M:%S')
        try:
            with log.open('a') as out:  # append: an earlier warm build may still be writing to it
                if lock_held(lock):
                    out.write(f'=== {stamp} {command}: skipped, {lock} is held\n')
                    say(step, f'skipped `{command}`: {lock} is held by a running build')
                    return
                out.write(f'=== {stamp} {command} (under {lock})\n')
                out.flush()
                # -n: if a build took the lock since the check above, skip rather than queue.
                spawned.append(subprocess.Popen(['flock', '-n', lock, 'sh', '-c', command], cwd=self.root,
                                                stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                                start_new_session=True))
        except OSError as exc:
            raise SetupError(f'cannot start `{command}` under {lock}: {exc}')
        say(step, f'started `{command}` under {lock}; log {log}')

    def all(self, warm):
        self.step('submodules', self.submodules)
        self.step('uv tools', self.uv_tools)
        self.step('tool CA', self.tool_ca)
        self.step('pio packages', self.pio_packages)
        self.step('git', self.git_setup)
        self.step('system packages', self.system_packages)
        if warm:
            self.step('warm', self.warm)
        if self.failed:
            raise SetupError(f'{len(self.failed)} step(s) failed: {", ".join(self.failed)}')
        print('dev setup: done', flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description='Set up this machine to build, test, and check the firmware.')
    parser.add_argument('--warm', action='store_true', help='start detached x4pro and host-test builds at the end')
    parser.add_argument('--root', default=ROOT, help='the repository root (default: this script\'s repository)')
    parser.add_argument('--proxy-ca', default=PROXY_CA, help=f'the agent proxy CA bundle (default: {PROXY_CA})')
    args = parser.parse_args(argv)
    setup = Setup(args.root, args.proxy_ca)
    return fork_common.exit_code(lambda: setup.all(args.warm))


if __name__ == '__main__':
    sys.exit(main())

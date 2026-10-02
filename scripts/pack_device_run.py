#!/usr/bin/env python3
"""
Packs the entry-14 device-run games of epic-install-and-launcher into one folder, with the hash of each.

    python3 scripts/pack_device_run.py <out-dir>

Writes `<out-dir>/` (created if missing):
  counter.chgame, loop.chgame, timing.chgame, pack-images.chgame   the fixtures of the same names
  counter-changed.chgame       test/game_script/fixtures/changed/counter: the same id at version 1.0.1, "Counter v2"
  pass-open.chgame             the fixture of the same name: noughts and crosses, solo or an open pass match
  pass-hidden.chgame           the fixture of the same name: a hidden pass match's hand-off
  pass-art.chgame              the fixture of the same name: title.png, handoff.png, default_mode, and settings
  invalid-binary-lua.chgame    the hardening generator's `binary-lua-stored` case, which the installer must set aside
  package_vector.chgame        the shared hash vector, copied: the file package_vectors.json's `hash_vector.package` names,
                               next to it (test/game_core/package_vector.chgame)
  HASHES.txt                   one `<file> <hash>` line per file, in that order: the hash scripts/pack_game.py printed,
                               the vector's recorded `package_hash` for the vector, and `(invalid)` for the invalid one

Each game is packed by running scripts/pack_game.py, as a person or the release workflow runs it. The `Game packages`
job of .github/workflows/crosshatch-game-packages.yml runs this script on a pull request labelled `package-games` and uploads the
folder as the `game-packages` artifact. HASHES.txt is written last, only when every package was made, and one left in
`<out-dir>` by an earlier run is removed first, so a failed run leaves none.

Exit codes: 0 written. 1 a rule is broken: the packer refused a game or printed no hash, or the committed vector no
longer matches package_vectors.json. 2 the script could not run: a file it needs is missing, the generator failed,
or `<out-dir>` cannot be written. Standard library only.
"""

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import fork_common
from fork_common import Failure, SetupError

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURES = pathlib.Path('test/game_script/fixtures')
PACKER = pathlib.Path('scripts/pack_game.py')
GENERATOR = pathlib.Path('test/game_script/harness/gen_hardening_packages.py')
VECTORS = pathlib.Path('test/game_core/package_vectors.json')

# (game folder under the fixtures, file name without .chgame), in the order HASHES.txt lists them.
GAMES = (
    ('counter', 'counter'),
    ('loop', 'loop'),
    ('timing', 'timing'),
    ('pack-images', 'pack-images'),
    ('changed/counter', 'counter-changed'),
    ('pass-open', 'pass-open'),
    ('pass-hidden', 'pass-hidden'),
    ('pass-art', 'pass-art'),
)
INVALID_CASE = 'binary-lua-stored.chgame'
INVALID_EXPECTED = 'BinaryLua'  # what cases.txt says the installer must do with it
INVALID_NAME = 'invalid-binary-lua.chgame'
HASHES_NAME = 'HASHES.txt'
HASH = re.compile(r'[0-9a-f]{16}')


def run(command, what):
    """The completed process of a command, or a SetupError when it cannot start."""
    try:
        return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors='replace')
    except OSError as exc:
        raise SetupError(f'cannot run {what}: {exc}')


def pack_game(root, folder, name, staging):
    """(package path, hash) of one fixture game, packed by scripts/pack_game.py into its own folder of staging."""
    source = root / FIXTURES / folder
    if not source.is_dir():
        raise SetupError(f'{source} is not a directory')
    packer = root / PACKER
    if not packer.is_file():
        raise SetupError(f'{packer} is missing')
    out = staging / name
    result = run([sys.executable, str(packer), str(source), str(out)], str(packer))
    sys.stdout.write(result.stdout)
    if result.returncode == fork_common.COULD_NOT_RUN:
        raise SetupError(f'{PACKER} could not pack {folder}: {result.stderr.strip()}')
    if result.returncode != 0:
        raise Failure(f'{folder} failed to pack ({result.returncode}): {result.stderr.strip()}')
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    package_hash = lines[-1] if lines else ''
    if not HASH.fullmatch(package_hash):
        raise Failure(f'{folder}: the packer\'s last line {package_hash!r} is not a package hash')
    package = out / f'{pathlib.PurePath(folder).name}.chgame'
    if not package.is_file():
        raise Failure(f'{folder}: the packer wrote no {package.name}')
    return package, package_hash


def invalid_package(root, staging):
    """The hardening generator's package that must install as .bad."""
    generator = root / GENERATOR
    if not generator.is_file():
        raise SetupError(f'{generator} is missing')
    out = staging / 'hardening'
    result = run([sys.executable, str(generator), str(out)], str(generator))
    if result.returncode != 0:
        raise SetupError(f'{GENERATOR} exited {result.returncode}: {result.stderr.strip()}')
    package = out / INVALID_CASE
    if not package.is_file():
        raise SetupError(f'{GENERATOR} wrote no {INVALID_CASE}')
    try:
        cases = [line.split() for line in (out / 'cases.txt').read_text(encoding='utf-8').splitlines()]
    except OSError as exc:
        raise SetupError(f'{GENERATOR} wrote no readable cases.txt: {exc}')
    if [INVALID_CASE, INVALID_EXPECTED] not in cases:
        raise Failure(f'cases.txt has no line "{INVALID_CASE} {INVALID_EXPECTED}"; {INVALID_CASE} is no longer the '
                      f'package the installer must set aside as a compiled Lua file')
    return package


def vector_package(root):
    """(committed vector package, its recorded hash); a Failure when the file is not the one the vectors describe."""
    vectors = root / VECTORS
    try:
        vector = json.loads(vectors.read_text(encoding='utf-8'))['hash_vector']
        recorded, size, name = vector['package_hash'], vector['package_bytes'], vector['package']
        if not isinstance(name, str) or not name or pathlib.PurePath(name).name != name:
            raise ValueError(f'package {name!r} is not a file name')
        package = vectors.parent / name
        actual = package.stat().st_size
    except (OSError, KeyError, TypeError, ValueError) as exc:
        raise SetupError(f'cannot read the package vector: {exc}')
    if not isinstance(recorded, str) or not HASH.fullmatch(recorded):
        raise SetupError(f'{VECTORS}: package_hash {recorded!r} is not a package hash')
    if actual != size:
        raise Failure(f'{package} is {actual} bytes; {VECTORS} says {size}')
    return package, recorded


def pack_device_run(root, out_dir):
    """Write the set and HASHES.txt to out_dir; a Failure or SetupError says why not."""
    out_dir = pathlib.Path(out_dir)
    try:
        (out_dir / HASHES_NAME).unlink(missing_ok=True)
    except OSError as exc:
        raise SetupError(f'cannot write {out_dir}: {exc}')
    with tempfile.TemporaryDirectory() as scratch:
        staging = pathlib.Path(scratch)
        files = []  # (file name, source path, hash text)
        for folder, name in GAMES:
            package, package_hash = pack_game(root, folder, name, staging)
            files.append((f'{name}.chgame', package, package_hash))
        files.append((INVALID_NAME, invalid_package(root, staging), '(invalid)'))
        package, recorded = vector_package(root)
        files.append((package.name, package, recorded))
        try:
            out_dir.mkdir(parents=True, exist_ok=True)
            for file_name, source, _ in files:
                shutil.copyfile(source, out_dir / file_name)
            hashes = ''.join(f'{file_name} {package_hash}\n' for file_name, _, package_hash in files)
            (out_dir / HASHES_NAME).write_text(hashes, encoding='utf-8')
        except OSError as exc:
            raise SetupError(f'cannot write {out_dir}: {exc}')
    print(f'{HASHES_NAME}:')
    print(hashes, end='')


def main(argv=None):
    parser = argparse.ArgumentParser(description='Pack the entry-14 device-run games and list their hashes.')
    parser.add_argument('out_dir', help='the folder the packages and HASHES.txt are written to')
    args = parser.parse_args(argv)
    return fork_common.exit_code(lambda: pack_device_run(ROOT, args.out_dir))


if __name__ == '__main__':
    sys.exit(main())

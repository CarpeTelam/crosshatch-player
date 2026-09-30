#!/usr/bin/env python3
"""Packs fixture games with the release packer, for the suite that installs its live output (PackedFixturesTest).

Each `<fixtures>/<id>` is packed by `python3 scripts/pack_game.py <folder> <out>`, exactly as a person or the release
workflow runs it, into `<out>/<id>.chgame`, and the hash the packer printed is written beside it as `<out>/<id>.hash`
(one line of 16 hex digits). The installer's own hash of the installed game is compared with that line, so a change to
the packer's zip framing, member order, or hash formula that the C++ installer does not follow fails the suite.

Usage: python3 pack_fixtures.py <pack_game.py> <fixtures folder> <output folder> <id>...
Exit 0: every package written. Exit 1: the packer refused a fixture (its messages are passed on). Exit 2: usage or an
output that could not be written.
"""

import pathlib
import subprocess
import sys


def main(argv):
    if len(argv) < 5:
        print('usage: pack_fixtures.py <pack_game.py> <fixtures folder> <output folder> <id>...', file=sys.stderr)
        return 2
    packer, fixtures, out = argv[1], pathlib.Path(argv[2]), pathlib.Path(argv[3])
    out.mkdir(parents=True, exist_ok=True)
    for name in argv[4:]:
        result = subprocess.run([sys.executable, packer, str(fixtures / name), str(out)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            print(f'pack_fixtures: {name}: pack_game.py exited {result.returncode}\n{result.stderr}', file=sys.stderr)
            return 1 if result.returncode == 1 else 2
        try:
            (out / f'{name}.hash').write_text(result.stdout.splitlines()[-1] + '\n', encoding='utf-8')
        except (OSError, IndexError) as exc:
            print(f'pack_fixtures: {name}: cannot write the hash: {exc}', file=sys.stderr)
            return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

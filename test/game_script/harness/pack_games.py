#!/usr/bin/env python3
"""Packs the games the games check plays with the release packer, for GamesCheckTest (games_check.cmake).

Each `<games root>/<id>` is packed by `python3 scripts/pack_game.py <folder> <out>`, exactly as a person or the release
workflow runs it, into `<out>/<id>.chgame`; the hash the packer printed is written beside it as `<out>/<id>.hash` (one line
of 16 hex digits), or, when the packer refused the folder, the packer's stderr as `<out>/<id>.packerror`. The package
test of that id then fails with that text, and the other ids are still packed and played. A last `<out>/packed.stamp` is
what the build's custom command declares as its output.

This script never fails the build for a game's fault: a game that does not pack is a red test, not a build error. Its
own exit codes: 0 after every id was tried, 2 for usage or an output folder that cannot be written. Beside
`pack_fixtures.py`, the fixture games' twin; the engine tests run it too, through `std::system`, over scratch trees.

Usage: python3 pack_games.py <pack_game.py> <games root> <output folder> [<id>...]
"""

import pathlib
import subprocess
import sys

OUTPUT_SUFFIXES = ('.chgame', '.hash', '.packerror')


def main(argv):
    if len(argv) < 4:
        print('usage: pack_games.py <pack_game.py> <games root> <output folder> [<id>...]', file=sys.stderr)
        return 2
    packer, games, out = argv[1], pathlib.Path(argv[2]), pathlib.Path(argv[3])
    try:
        out.mkdir(parents=True, exist_ok=True)
        # A run starts clean, so an id removed since the last one leaves no package behind to be played.
        for stale in out.iterdir():
            if stale.suffix in OUTPUT_SUFFIXES or stale.name == 'packed.stamp':
                stale.unlink()
    except OSError as exc:
        print(f'pack_games: cannot prepare {out}: {exc}', file=sys.stderr)
        return 2
    for name in argv[4:]:
        result = subprocess.run([sys.executable, packer, str(games / name), str(out)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        lines = result.stdout.splitlines()
        try:
            if result.returncode == 0 and lines:
                (out / f'{name}.hash').write_text(lines[-1] + '\n', encoding='utf-8')
            else:
                message = result.stderr.strip() or result.stdout.strip() or 'no output'
                (out / f'{name}.packerror').write_text(
                    f'pack_game.py exited {result.returncode} for {name}:\n{message}\n', encoding='utf-8')
        except OSError as exc:
            print(f'pack_games: {name}: cannot write its result: {exc}', file=sys.stderr)
            return 2
    try:
        (out / 'packed.stamp').write_text('', encoding='utf-8')
    except OSError as exc:
        print(f'pack_games: cannot write the stamp: {exc}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

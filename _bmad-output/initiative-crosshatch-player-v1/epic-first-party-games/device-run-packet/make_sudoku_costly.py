#!/usr/bin/env python3
"""
Packs `sudoku-costly.chgame`: Sudoku with its first deal fixed to the costliest Expert grid the bank can give, for the
device run's HINT and CHECK timing (epic-first-party-games entry 5). A packet file only: it is not a game of the
repository, the packer's rules apply to it, and nothing under `games/` changes.

    python3 _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet/make_sudoku_costly.py <out-dir>

It copies `games/sudoku/` to a scratch folder named `sudoku-costly`, then changes two files and nothing else:
  manifest.json   id `sudoku-costly`, name `Sudoku costly`, the Difficulty default `Expert` (so the row cannot be
                  mistaken for the shipped game and a save of one never meets the other); the file is rewritten
                  through `json.dumps`, so its layout changes and its content does not. The Difficulty setting stays
                  in the manifest but does nothing: `setup` ignores it.
  main.lua        `game.setup` still loads the bank (`require("puzzles")`, so the VM's heap carries it as in the
                  shipped game) and returns GRID as the level 4 (Expert) deal, with no random pick and no symmetry.
GRID is bank puzzle `0034ee8363e5` (Expert, index 59, the costliest Expert call the bank tool recorded: 224,973 host
instructions in its filed orientation) under the symmetry, out of 3,000 random ones, that made the first HINT or CHECK
cost most: 302,052 host instructions (the solver's one-time table building, 11,023, plus `answer` 246,397 and `hint`
44,632), counted one by one by a host Lua 5.5.1 as `make_bank.py` counts. SOLUTION is its one solution, which the
game's own solver and a separate backtracking search agree on; the script itself re-checks, with a backtracking
search, that GRID has exactly one solution and that it is SOLUTION.

Prints the package hash as `scripts/pack_game.py` does. Exit codes: 0 packed. 1 the package is invalid or an edit
matched nothing (the game moved since this was written). 2 the script could not run. Standard library only.
"""

import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
GAME = ROOT / 'games' / 'sudoku'
PACKER = ROOT / 'scripts' / 'pack_game.py'
ID = 'sudoku-costly'
GRID = '400007000015000720000010003006700080050001009200005060004050008600080900009600040'
SOLUTION = '463927815915348726872516493196732584358461279247895361724159638631284957589673142'
SETUP = re.compile(r'^function game\.setup\(ctx\)\n.*?^end\n', re.S | re.M)


def solutions(grid, limit=2):
    """The solutions of an 81-character grid, at most `limit` of them, by plain backtracking."""
    cells, found = [int(c) for c in grid], []

    def fits(i, d):
        r, c = divmod(i, 9)
        for k in range(9):
            if cells[r * 9 + k] == d or cells[k * 9 + c] == d:
                return False
        br, bc = r // 3 * 3, c // 3 * 3
        return all(cells[(br + a) * 9 + bc + b] != d for a in range(3) for b in range(3))

    def walk():
        if len(found) >= limit:
            return
        if 0 not in cells:
            found.append(''.join(map(str, cells)))
            return
        i = cells.index(0)
        for d in range(1, 10):
            if fits(i, d):
                cells[i] = d
                walk()
                cells[i] = 0

    walk()
    return found


def fail(message, code):
    print(f'error: {message}', file=sys.stderr)
    sys.exit(code)


def main():
    if len(sys.argv) != 2:
        fail('usage: make_sudoku_costly.py <out-dir>', 2)
    if len(GRID) != 81 or len(SOLUTION) != 81 or any(g != '0' and g != s for g, s in zip(GRID, SOLUTION)):
        fail('GRID and SOLUTION disagree', 1)
    if solutions(GRID) != [SOLUTION]:
        fail('GRID does not have SOLUTION as its one solution', 1)
    if not GAME.is_dir() or not PACKER.is_file():
        fail(f'{GAME} or {PACKER} is missing: run it from a checkout', 2)
    with tempfile.TemporaryDirectory() as scratch:
        folder = pathlib.Path(scratch) / ID
        shutil.copytree(GAME, folder)
        try:
            manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
            manifest['id'], manifest['name'] = ID, 'Sudoku costly'
            levels = [s for s in manifest.get('settings', []) if s.get('id') == 'level']
        except (ValueError, AttributeError, TypeError) as exc:
            fail(f'manifest.json is not the shape this script edits ({exc}): the game moved', 1)
        if len(levels) != 1 or 'Expert' not in levels[0]['values']:
            fail('manifest.json has no level setting with Expert: the game moved', 1)
        levels[0]['default'] = 'Expert'
        (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
        main_lua = (folder / 'main.lua').read_text(encoding='utf-8')
        deal = ('function game.setup(ctx)\n'
                '  require("puzzles")\n'
                f'  return {{ l = 4, v = "{GRID}", n = string.rep("\\0", 162), u = "", t = 0 }}\n'
                'end\n')
        main_lua, hits = SETUP.subn(lambda _: deal, main_lua, count=1)
        if hits != 1:
            fail('main.lua has no `function game.setup(ctx)` block: the game moved', 1)
        (folder / 'main.lua').write_text(main_lua, encoding='utf-8')
        result = subprocess.run([sys.executable, str(PACKER), str(folder), sys.argv[1]], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            sys.exit(result.returncode)


if __name__ == '__main__':
    main()

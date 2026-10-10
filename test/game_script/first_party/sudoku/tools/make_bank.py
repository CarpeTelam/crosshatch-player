#!/usr/bin/env python3
"""
Writes `games/sudoku/puzzles.lua`: the puzzles of Sudoku, filed under the band the game's own solver needs. Standard
library only, plus a C compiler when no host Lua is given. Not run by CI; run it again whenever `solver.lua` changes,
because the band a puzzle is filed under is the hardest technique the shipped solver needs.

    git clone https://github.com/grantm/sudoku-exchange-puzzle-bank /tmp/bank        # public domain
    git -C /tmp/bank checkout d8c8ebaee0c08c412cfba96af1923dfa61c83317
    python3 test/game_script/first_party/sudoku/tools/make_bank.py --bank /tmp/bank

The source is the Sudoku Exchange puzzle bank (`easy.txt`, `medium.txt`, `hard.txt`; `diabolical.txt` is never read:
the ladder finishes none of it). A record is `hash12 SP digits81 SP rating4`, the hash being the first 12 hex digits of
the SHA-1 of the digit string; a record whose hash is not that is dropped. The three files are merged in hash order (the
bank's own order for randomising), and the candidates are graded in that order, in batches, by `grade.lua` under a host
Lua 5.5.1 built from `lib/lua/src` (the VM the device runs, so instruction counts carry over), until every band holds
`--size` puzzles. A candidate the ladder cannot finish is left out; one whose cost passes `--cap` is left out too. The
cost is the worst single call a player's game makes with the solver: the larger of `grade` (what the checks call) and
`answer` plus `hint` (the first HINT or CHECK), in instructions counted one by one, plus the solver's one-time table
building. The file is deterministic: the same bank, solver, and settings give the same bytes.

`--lua` names a host `lua` to use; without it the tool builds one into a temporary folder (one `cc` command over every
`lib/lua/src/*.c` except `luac.c`, with `-DLUA_USE_LINUX -DLUA_COMPAT_GLOBAL=0 -include lib/lua/port/luai_throw.h`, as the
repository's own build does), which needs `cc`. A summary of the measurements is written into the file's header and
printed.

Exit codes: 0 written. 1 the bank cannot fill every band, or its commit is not the one `--commit` names. 2 the tool could
not run (a missing file, no compiler, a failed grading run).
"""

import argparse
import hashlib
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[5]
TOOLS = pathlib.Path(__file__).resolve().parent
GAME = ROOT / 'games' / 'sudoku'
COUNTER = TOOLS.parent / 'counter.lua'

SOURCE_URL = 'https://github.com/grantm/sudoku-exchange-puzzle-bank'
SOURCE_COMMIT = 'd8c8ebaee0c08c412cfba96af1923dfa61c83317'
FILES = ('easy.txt', 'medium.txt', 'hard.txt')
BANDS = ('easy', 'medium', 'hard', 'expert')
BAND_NAMES = ('Easy', 'Medium', 'Hard', 'Expert')
RECORD = re.compile(r'^([0-9a-f]{12}) ([0-9.]{81}) +[0-9.]+$')
RECORD_WIDTH = 12 + 81


class ToolError(Exception):
    def __init__(self, message, code):
        super().__init__(message)
        self.code = code


def bank_commit(bank):
    result = subprocess.run(['git', '-C', str(bank), 'rev-parse', 'HEAD'], capture_output=True, text=True)
    if result.returncode != 0:
        raise ToolError(f'{bank} is not a git clone: {result.stderr.strip()}', 2)
    return result.stdout.strip()


def read_records(bank):
    """({hash: digits}, number of records read, number dropped for a bad hash or shape), from the three files."""
    records = {}
    read = dropped = 0
    for name in FILES:
        path = bank / name
        if not path.is_file():
            raise ToolError(f'{path} is missing', 2)
        with path.open('r', encoding='ascii') as handle:
            for line in handle:
                read += 1
                match = RECORD.match(line.rstrip('\n'))
                if not match:
                    dropped += 1
                    continue
                digits = match.group(2).replace('.', '0')
                if hashlib.sha1(digits.encode('ascii')).hexdigest()[:12] != match.group(1):
                    dropped += 1
                    continue
                records.setdefault(match.group(1), digits)
    return records, read, dropped


def build_lua(directory):
    """Builds a host Lua from lib/lua/src into directory and returns its path."""
    sources = sorted(str(path) for path in (ROOT / 'lib' / 'lua' / 'src').glob('*.c') if path.name != 'luac.c')
    target = pathlib.Path(directory) / 'lua'
    command = ['cc', '-O2', '-DLUA_USE_LINUX', '-DLUA_COMPAT_GLOBAL=0', '-I', str(ROOT / 'lib' / 'lua' / 'src'),
               '-include', str(ROOT / 'lib' / 'lua' / 'port' / 'luai_throw.h'), *sources, '-o', str(target), '-lm', '-ldl']
    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except FileNotFoundError:
        raise ToolError('no C compiler (cc) to build the host Lua with; pass --lua', 2)
    if result.returncode != 0:
        raise ToolError(f'building the host Lua failed:\n{result.stderr}', 2)
    return target


def grade_batch(lua, solver, batch):
    """[(hash, band, rung, cost, count cost, solutions)] and the build cost for a list of (hash, digits)."""
    text = ''.join(f'{h} {d}\n' for h, d in batch)
    result = subprocess.run([str(lua), str(TOOLS / 'grade.lua'), str(solver), str(COUNTER)], input=text,
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise ToolError(f'grade.lua failed ({result.returncode}): {result.stderr.strip()}', 2)
    lines = result.stdout.splitlines()
    if not lines or not lines[0].startswith('build ') or len(lines) != len(batch) + 1:
        raise ToolError('grade.lua printed an unexpected result', 2)
    build = int(lines[0].split()[1])
    graded = []
    for (h, _), line in zip(batch, lines[1:]):
        fields = line.split()
        if fields[0] != h:
            raise ToolError('grade.lua answered a different candidate', 2)
        band, rung, grade_cost, answer_cost, hint_cost, count_cost, solutions = (int(x) for x in fields[1:])
        graded.append((h, band, rung, max(grade_cost, answer_cost + hint_cost), count_cost, solutions))
    return graded, build


def select(records, lua, solver, size, cap, count_cap, batch_size):
    """The kept puzzles per band (lists of (hash, digits, cost)) and the measurements."""
    order = sorted(records)
    kept = [[] for _ in BANDS]
    examined = [0] * len(BANDS)
    over_cap = [0] * len(BANDS)
    not_countable = [0] * len(BANDS)
    most_examined = [0] * len(BANDS)
    unfinished = 0
    graded_total = 0
    build = 0
    for start in range(0, len(order), batch_size):
        batch = [(h, records[h]) for h in order[start:start + batch_size]]
        graded, build = grade_batch(lua, solver, batch)
        for h, band, _, cost, count_cost, solutions in graded:
            if all(len(k) >= size for k in kept):
                break  # the count stops where the last band filled, so the batch size changes nothing
            graded_total += 1
            if band == 0:
                unfinished += 1
                continue
            b = band - 1
            if len(kept[b]) >= size:
                continue
            examined[b] += 1
            cost += build
            most_examined[b] = max(most_examined[b], cost)
            if cost > cap:
                over_cap[b] += 1
            elif solutions != 1 or count_cost > count_cap:
                not_countable[b] += 1
            else:
                kept[b].append((h, records[h], cost))
        if all(len(k) >= size for k in kept):
            break
    return kept, {'examined': examined, 'over_cap': over_cap, 'not_countable': not_countable,
                  'most_examined': most_examined,
                  'unfinished': unfinished, 'graded': graded_total, 'build': build}


def render(commit, kept, stats, args, read, dropped):
    """The text of puzzles.lua."""
    out = []
    out.append('-- puzzles.lua: the puzzles of Sudoku, filed under the band the shipped solver needs. Generated by')
    out.append('-- make_bank.py (kept with the game\'s checks); edit nothing here by hand.')
    out.append('--')
    out.append(f'-- Source: the Sudoku Exchange puzzle bank, {SOURCE_URL}, commit {commit}.')
    out.append('-- Puzzles from the Sudoku Exchange puzzle bank (sudokuexchange.com), public domain.')
    out.append(f'-- Records: easy.txt, medium.txt, hard.txt ({read} read, {dropped} dropped for a hash that is not the first 12 hex')
    out.append('-- digits of the SHA-1 of the digits); merged in hash order and graded in that order.')
    out.append(f'-- Settings: {args.size} per band, cap {args.cap} instructions, count cap {args.count_cap}, {args.per} records to a string,')
    out.append('-- solver.lua.')
    out.append('-- A record is hash12 .. digits81 (0 for an empty cell). A band is the hardest technique the solver needs: Easy')
    out.append('-- singles, Medium locked candidates, Hard pairs, Expert anything above. A puzzle the solver cannot finish, or whose')
    out.append('-- worst single call (grade, or answer then hint, plus the solver\'s one-time table building) passes the cap, is left out;')
    out.append('-- so is one the checks\' independent counter does not find exactly one solution of within the count cap.')
    out.append('-- Measured with a host Lua 5.5.1, instructions counted one by one: the solver\'s one-time table building takes')
    out.append(f'-- {stats["build"]}. Candidates graded: {stats["graded"]}, of which {stats["unfinished"]} are not finished by the ladder.')
    for b, name in enumerate(BAND_NAMES):
        top = max(cost for _, _, cost in kept[b])
        out.append(f'-- {name}: examined {stats["examined"][b]}, kept {len(kept[b])}, over the cap {stats["over_cap"][b]}, '
                   f'not countable {stats["not_countable"][b]}, largest worst call kept {top}, examined {stats["most_examined"][b]}.')
    out.append('')
    out.append('local puzzles = {')
    out.append(f'  commit = "{commit}",')
    out.append(f'  per = {args.per}, -- records to a string')
    out.append(f'  width = {RECORD_WIDTH}, -- characters in a record')
    out.append(f'  cap = {args.cap}, -- the most instructions a worst single call may take')
    out.append('  count = { ' + ', '.join(str(len(k)) for k in kept) + ' }, -- puzzles per band')
    out.append('  most = { ' + ', '.join(str(max(cost for _, _, cost in k)) for k in kept) + ' }, -- largest worst-call cost kept')
    out.append('  -- the indices of the costliest few puzzles of each band, the costliest first')
    out.append('  costly = {')
    for k in kept:
        ranked = sorted(range(len(k)), key=lambda i: (-k[i][2], i))[:args.costly]
        out.append('    { ' + ', '.join(str(i + 1) for i in ranked) + ' },')
    out.append('  },')
    for b, key in enumerate(BANDS):
        out.append(f'  {key} = {{')
        for i in range(0, len(kept[b]), args.per):
            out.append('    "' + ''.join(h + d for h, d, _ in kept[b][i:i + args.per]) + '",')
        out.append('  },')
    out.append('}')
    out.append('')
    out.append('-- puzzles.get(band, i) -> hash12, digits81: puzzle i (from 1) of band 1 (Easy) to 4 (Expert).')
    out.append('local KEYS = { "easy", "medium", "hard", "expert" }')
    out.append('function puzzles.get(band, i)')
    out.append('  local chunk = puzzles[KEYS[band]][(i - 1) // puzzles.per + 1]')
    out.append('  local at = ((i - 1) % puzzles.per) * puzzles.width + 1')
    out.append('  return chunk:sub(at, at + 11), chunk:sub(at + 12, at + puzzles.width - 1)')
    out.append('end')
    out.append('')
    out.append('return puzzles')
    return '\n'.join(out) + '\n'


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--bank', type=pathlib.Path, required=True, help='a clone of the puzzle bank')
    parser.add_argument('--commit', default=SOURCE_COMMIT, help='the commit the clone must be at')
    parser.add_argument('--size', type=int, default=100, help='puzzles per band (default 100)')
    parser.add_argument('--cap', type=int, default=1000000, help='the most instructions of a worst call (default 1000000)')
    parser.add_argument('--count-cap', type=int, default=1500000, dest='count_cap',
                        help='the most instructions the checks\' solution counter may take on a puzzle (default 1500000)')
    parser.add_argument('--per', type=int, default=20, help='records to a string (default 20)')
    parser.add_argument('--costly', type=int, default=4, help='costliest puzzles listed per band (default 4)')
    parser.add_argument('--batch', type=int, default=250, help='candidates per grading run (default 250)')
    parser.add_argument('--lua', type=pathlib.Path, help='a host lua to use instead of building one')
    parser.add_argument('--solver', type=pathlib.Path, default=GAME / 'solver.lua', help='the solver to grade with')
    parser.add_argument('--out', type=pathlib.Path, default=GAME / 'puzzles.lua', help='the file to write')
    args = parser.parse_args(argv)
    try:
        commit = bank_commit(args.bank)
        if commit != args.commit:
            print(f'error: the bank is at {commit}, not {args.commit}', file=sys.stderr)
            return 1
        records, read, dropped = read_records(args.bank)
        with tempfile.TemporaryDirectory() as scratch:
            lua = args.lua if args.lua else build_lua(scratch)
            kept, stats = select(records, lua, args.solver, args.size, args.cap, args.count_cap, args.batch)
        short = [BAND_NAMES[b] for b in range(len(BANDS)) if len(kept[b]) < args.size]
        if short:
            print(f'error: the bank holds fewer than {args.size} usable puzzles for: {", ".join(short)}', file=sys.stderr)
            return 1
        text = render(commit, kept, stats, args, read, dropped)
    except ToolError as error:
        print(f'error: {error}', file=sys.stderr)
        return error.code
    args.out.write_text(text, encoding='ascii')
    for line in text.splitlines():
        if line.startswith('-- ') and ('examined' in line or 'Measured' in line or 'graded' in line):
            print(line[3:])
    print(f'wrote {args.out} ({len(text)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

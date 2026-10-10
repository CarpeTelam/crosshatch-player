#!/usr/bin/env python3
"""Finds the move lists the Ultimate tic-tac-toe rounds hold and prints them in Lua syntax.

Run it by hand, from anywhere (stdlib only, not run by CI):

    python3 test/game_script/first_party/ultimate-tic-tac-toe/tools/make_rounds.py

It prints one `local moves = { {b, c}, ... }` block per round (small board b, cell c, 1..9, numbered row by row), with
the seed that found it, to paste into `rounds/<name>.lua`. The searches are seeded, so the output is the same on every
run.

This file is an independent implementation of the rules: it shares no code and no constants with games/ultimate-
tic-tac-toe/main.lua. That is the point. Each round's `winners` is what this file says the lists lead to, and the games
check plays them through the real game, so a rule the game gets wrong (or this file does) fails a round.

The rules, as the game states them: nine small boards of nine cells. Players alternate, player 1 (X) first. A move in
small board b, cell c is legal when board b is not yet won or full, the cell is empty, and (unless the previous move
sent the player to "any board") b is the cell the previous move was played in. A move in cell c sends the other player
to small board c, or to any open board when board c is won or full. Three marks in a line win a small board (checked
before "full"); three won small boards in a line win the game; when no line of the big board can still be completed
by either player, the game is a draw.
"""
import random

# Cells of a 3x3 grid, numbered 0..8 row by row here (the printed lists use 1..9).
THREES = [(0, 1, 2), (3, 4, 5), (6, 7, 8), (0, 3, 6), (1, 4, 7), (2, 5, 8), (0, 4, 8), (2, 4, 6)]


class Game:
    def __init__(self):
        self.cells = [[0] * 9 for _ in range(9)]  # cells[board][cell]: 0 empty, 1 or 2
        self.result = [0] * 9  # per board: 0 open, 1 or 2 won, 9 full without a line
        self.target = None  # the board the next move must be in, None for any open board
        self.moves = 0

    def player(self):
        return 1 + self.moves % 2

    def winner(self):
        """1, 2, or None, by the won boards."""
        for a, b, c in THREES:
            if self.result[a] in (1, 2) and self.result[a] == self.result[b] == self.result[c]:
                return self.result[a]
        return None

    def drawn(self):
        """No line of the big board can still be completed by either player."""
        for line in THREES:
            for who in (1, 2):
                if all(self.result[i] in (0, who) for i in line):
                    return False
        return True

    def over(self):
        return self.winner() is not None or self.drawn()

    def open_boards(self):
        if self.over():
            return []
        if self.target is not None:
            return [self.target]
        return [i for i in range(9) if self.result[i] == 0]

    def legal(self):
        return [(b, c) for b in self.open_boards() for c in range(9) if self.cells[b][c] == 0]

    def play(self, b, c):
        assert (b, c) in self.legal(), (b, c)
        who = self.player()
        self.cells[b][c] = who
        small = self.cells[b]
        if any(small[x] == small[y] == small[z] == who for x, y, z in THREES):
            self.result[b] = who
        elif all(small):
            self.result[b] = 9
        self.target = c if self.result[c] == 0 else None
        self.moves += 1


def playout(seed):
    """A seeded random game to its end: the moves played and the game."""
    rng = random.Random(seed)
    game, moves = Game(), []
    while not game.over():
        b, c = rng.choice(game.legal())
        game.play(b, c)
        moves.append((b, c))
    return moves, game


def shortest(outcome, tries=4000):
    """(seed, moves) of the shortest of `tries` playouts whose result is `outcome`: 1 or 2 (that player wins), 0 (a draw
    with a small board still open)."""
    best = None
    for seed in range(tries):
        moves, game = playout(seed)
        won = game.winner()
        if (won or 0) != outcome:
            continue
        if outcome == 0 and 0 not in game.result:
            continue  # a draw that leaves a small board open: it ends by "no line left", not by filling the board
        if best is None or len(moves) < len(best[1]):
            best = (seed, moves)
    return best


def forced_board(tries=4000):
    """(seed, rows) of the shortest forced-board round found; a row is (b, c, played).

    A random first move, then a tap in a board the opponent may not use (played False: refused), then random legal moves
    until one sends the opponent to a board that was already won before the move; then a tap in that won board (refused)
    and a free choice in an open board other than the one just played, where the list ends.
    """
    best = None
    for seed in range(tries):
        rng = random.Random(seed)
        game, rows = Game(), []
        b, c = rng.choice(game.legal())
        game.play(b, c)
        rows.append((b, c, True))
        wrong = [(x, y) for x in range(9) if x != game.target for y in range(9)]
        rows.append((*rng.choice(wrong), False))
        while len(rows) < 60 and not game.over():
            b, c = rng.choice(game.legal())
            was_closed = game.result[c] in (1, 2)
            game.play(b, c)
            rows.append((b, c, True))
            if was_closed and not game.over():
                closed = [(c, y) for y in range(9) if game.cells[c][y] == 0]
                if closed:
                    rows.append((*rng.choice(closed), False))
                free = [(x, y) for x, y in game.legal() if x != b]
                if free:
                    x, y = rng.choice(free)
                    game.play(x, y)
                    rows.append((x, y, True))
                    if best is None or len(rows) < len(best[1]):
                        best = (seed, rows)
                break
    return best


def rejected_move():
    """Moves (b, c) leading to a position where board 5 is the forced board and its cell 5 is taken, checked by playing
    them, then the legal reply the round ends with."""
    game = Game()
    moves = [(4, 4), (4, 0), (0, 4)]  # board 5 cell 5, board 5 cell 1, board 1 cell 5: the next move is in board 5
    for b, c in moves:
        game.play(b, c)
    assert game.target == 4 and game.cells[4][4] != 0
    reply = (4, 1)  # board 5, cell 2: empty
    assert reply in game.legal()
    return moves, (4, 4), reply


def lua_list(items, width=110):
    """`items` (strings) as the lines of a Lua table constructor, wrapped at `width` columns."""
    lines, line = [], "  "
    for item in items:
        piece = item + ","
        if len(line) + len(piece) + 1 > width and line.strip():
            lines.append(line.rstrip())
            line = "  "
        line += piece + " "
    lines.append(line.rstrip())
    return "{\n" + "\n".join(lines) + "\n}"


def lua_moves(moves):
    return "local moves = " + lua_list("{%d, %d}" % (b + 1, c + 1) for b, c in moves)


def main():
    for name, outcome in (("won-by-seat-1", 1), ("won-by-seat-2", 2), ("drawn", 0)):
        found = shortest(outcome)
        if found is None:
            print("-- %s: no playout found" % name)
            continue
        seed, moves = found
        print("-- %s: seed %d, %d moves" % (name, seed, len(moves)))
        print(lua_moves(moves))
    found = forced_board()
    if found is not None:
        seed, rows = found
        print("-- forced-board: seed %d, rows (b, c, played)" % seed)
        print("local rows = " + lua_list("{%d, %d, %s}" % (b + 1, c + 1, "true" if ok else "false") for b, c, ok in rows))
    moves, taken, reply = rejected_move()
    print("-- rejected-move: moves, the occupied cell to tap, the legal reply")
    print(lua_moves(moves))
    print("-- taken {%d, %d}, reply {%d, %d}" % (taken[0] + 1, taken[1] + 1, reply[0] + 1, reply[1] + 1))


if __name__ == "__main__":
    main()

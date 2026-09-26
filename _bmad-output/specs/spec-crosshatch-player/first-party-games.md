# First-party games (CAP-10)

v1 ships at least one game in each category below. Together they must exercise `solo`, `pass`, `nearby`, and a `hidden = true` pass game. Final titles are chosen in the games epic.

The research derived these candidates from game rules, not usage data:

| Category | Candidates | Modes it proves |
| --- | --- | --- |
| Solo puzzle | Sudoku, nonograms, Minesweeper | `solo` |
| Open-information 2P | tic-tac-toe, Ultimate tic-tac-toe, Dots and Boxes, Nine Men's Morris, Gomoku | `pass`, `nearby` |
| Hidden-information 2P | Hangman, Mastermind / Bulls and Cows, Battleship | `pass` with hand-off, `nearby` |

- Simon Tatham's puzzles are MIT-licensed but each one needs rework for e-ink.
- Every first-party game stays within API level 1: turn-structured, a snapshot of at most 1,400 B, library icons or 1-bit package images.
- Sources live in `games/<id>/`. `scripts/pack_game.py` packs them into `.cpgame` files, which are attached to each fork release and installed through the inbox, never embedded in the firmware.

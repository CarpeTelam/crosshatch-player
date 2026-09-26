# First-party games (CAP-10)

v1 ships these three. Together they exercise `solo`, `pass`, `nearby`, and a `hidden = true` pass game.

| Category | Game | Modes | Manifest |
| --- | --- | --- | --- |
| Solo puzzle | Sudoku | `solo` | `seats {1, 1}`, `hidden = false` |
| Open-information 2P | Ultimate tic-tac-toe, the crosshatch namesake | `pass`, `nearby` | `seats {2, 2}`, `hidden = false` |
| Hidden-information 2P | Battleship | `pass`, `nearby` | `seats {2, 2}`, `hidden = true` |

## Fit notes

- **Sudoku:** digits come from a tap number picker, not text entry. Ship a bank of puzzles in the package (81 characters each) instead of generating unique-solution puzzles inside the 2 M-instruction budget. Pack pencil marks into strings to keep the snapshot small.
- **Ultimate tic-tac-toe:** v1 keeps it simple. The whole board is drawn like Sudoku's, the small board(s) the player may play in are highlighted, and the player taps a cell directly. There is no zoomed view.
- **Shared board:** Sudoku and Ultimate tic-tac-toe use the same 9×9 board of 3×3 blocks, so its drawing and tap-to-cell mapping are written once and copied into both packages (`require` loads only from a package's own root). Its cells are about 50 px on the 480 px X4 Pro width, small for children's fingers; an enlarged-board view is a later upgrade for both games.
- **Battleship:** there is no drag, so ships are placed by tapping with a rotate control, or placed at random. Pack each board (for example as a string) to stay well under the 1,400 B snapshot limit. In `nearby`, each device draws only its own seat.

## Common rules

- Every first-party game stays within API level 1: turn-structured, a snapshot of at most 1,400 B, library icons or 1-bit package images.
- Sources live in `games/<id>/`. `scripts/pack_game.py` packs them into `.cpgame` files, which are attached to each fork release and installed through the inbox, never embedded in the firmware.
- Candidates set aside: Minesweeper, nonograms, tic-tac-toe (the API docs example), Dots and Boxes, Nine Men's Morris, Gomoku, Mastermind, and Hangman (setting a word needs text entry).

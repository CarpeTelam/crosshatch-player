# field (wider ecosystem, game catalog, user voice, hardware) — round 2
## Findings
- claim: CrossMux already publishes Nightly builds for Seeed Sticky (ESP32-S3) AND Xteink X4 Pro (ESP32-S3), plus M5Stack Paper Mono, eego A4, Murphy M4, Waveshare ePaper 3.97, Metalio E-Ink 4; X3/X4 (C3) get Stable; README says the target table "is not a claim that every feature has passed hardware acceptance".
  source: https://raw.githubusercontent.com/0x1abin/crossmux/HEAD/README.md
  publisher: 0x1abin / CrossMux
  pub_date: 2026-09 (HEAD at access)
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: CrossMux apps are compiled-in Activities registered in a table (`kAppEntries`, persisted 32-bit `hiddenAppsMask`, IDs up to 16 used); catalog includes Sudoku, Gomoku, Minesweeper, 2048, Chinese Chess, Ugly Avatar, Buddy, Sokoban, Pixel Switch, Calculator, Woodfish, AirPage; shared `GameUi` and `GameSaveDebouncer` (1.5 s save debounce); `OptionPopup` owns touch hit-testing + button nav; fresh settings HIDE Chinese Chess, Minesweeper, 2048, Ugly Avatar, Buddy, Sokoban, Pixel Switch, Woodfish by default. No scripting, no multiplayer mentioned.
  source: https://raw.githubusercontent.com/0x1abin/crossmux/HEAD/src/activities/apps/README.md
  publisher: CrossMux
  pub_date: 2026-09 (HEAD at access)
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: CrossInk Games Edition is X4 Pro-only (ESP32-S3), OTA-updates from its own GitHub releases, A/B slot install with SD-card picker recovery (Down+Power) because "automatic rollback is not reliable on this hardware, and the device has no USB-C port"; MIT; states "Games are deliberately out of scope upstream" (CrossInk and CrossPoint). Game list not published in README.
  source: https://raw.githubusercontent.com/LegendaryDrogan/crossink-games-edition/HEAD/README.md
  publisher: LegendaryDrogan
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: trajectory
- claim: KOReader has a large game-plugin ecosystem: t2ym5u/koreader-plugins indexes 70 plugins (Lua), incl. battleship, connect4, hangman, mastermind, gomoku, go, othello, checkers, chess, backgammon (2-player), dice, pickomino, memory, solitaire, wordle, word ladder, boggle, ~20 sudoku/pencil-puzzle variants (nonogram, slitherlink, kenken, nurikabe, tents, masyu...), and "party" multiplayer titles Boggle Party, Pictionary Party, Quiz Party, Taboo Party; tested on Kobo only; ships a shared `game-common` lib and an on-device Plugin Manager.
  source: https://github.com/t2ym5u/koreader-plugins (README.md, manifest.json, docs/README.md @ ad3d4af)
  publisher: t2ym5u
  pub_date: 2026 (HEAD ad3d4af at access)
  accessed: 2026-09-26
  confidence: high
  class: catalog
- claim: Other KOReader game plugins: Casual Chess (chess + checkers, reversi, Fox & Hounds, per-player chess clock, PGN), chess.koplugin, SudokuPlus (logical solver/tutor), sudoku.koplugin ("touch-friendly ... resume later"), plus a sudoku in KOReader's official contrib.
  source: https://github.com/MJCopper/casualkochess.koplugin ; https://github.com/Borisvl/sudokuplus.koplugin ; https://github.com/omer-faruq/sudoku.koplugin (WebSearch snippets)
  publisher: GitHub authors
  pub_date: unknown
  accessed: 2026-09-26
  confidence: medium
  class: catalog
- claim: KOReader sudoku plugin announcement drew one reply ("This is great") — thin public sentiment signal.
  source: https://www.mobileread.com/forums/showthread.php?t=370734
  publisher: MobileRead Forums
  pub_date: 2025-11-15
  accessed: 2026-09-26
  confidence: medium
  class: sentiment
- claim: Tatham puzzles (41 puzzles, all single-player; Guess=Mastermind, Mines=Minesweeper, Pattern=nonogram, Solo=sudoku) have e-ink ports: PocketPuzzles (PocketBook, MIT, ~1.5 MB app, <100 KB state) which needed per-game rework: colors->greyscale/textures, "Modify games with dragging for better handling of eInk screen limitations", thicker/dotted error lines; author: "eInk screens are limited in response time and color availability, so most of the games need individual tweaking to make them fun to play". Excludes unfinished Group/Slide/Sokoban.
  source: https://github.com/SteffenBauer/PocketPuzzles (README.md, ToDo.md)
  publisher: Steffen Bauer
  pub_date: 2020-2026 (HEAD at access)
  accessed: 2026-09-26
  confidence: high
  class: performance
- claim: Kindle port (kbarni/kindlepuzzles): "Some games however won't work or are difficult to play. Some need keyboard and some need right click to play correctly" — right-click/keyboard dependence is the main touch-port hazard.
  source: https://github.com/kbarni/kindlepuzzles
  publisher: kbarni
  pub_date: unknown
  accessed: 2026-09-26
  confidence: high
  class: performance
- claim: reMarkable has a Tatham port (rmkit "puzzles" v0.2.2-1 via toltec) with saved state, drag/drop gestures, fast grayscale drawing; official Tatham page lists third-party ports incl. Rockbox (embedded low-RAM targets) — evidence the core is portable to constrained C frontends. No ESP32 port found.
  source: https://rmkit.dev/apps/puzzles ; https://www.chiark.greenend.org.uk/~sgtatham/puzzles/
  publisher: rmkit ; Simon Tatham
  pub_date: unknown ; page references 2023-09-20
  accessed: 2026-09-26
  confidence: medium
  class: catalog
- claim: M5Paper (ESP32, 540x960 4.7", GT911 cap touch) and M5PaperS3 are ESP32 e-ink touch devices; a 60 fps GameBoy emulator ran on M5PaperS3 (Hackster); no ESP-NOW e-ink multiplayer game project found.
  source: https://docs.m5stack.com/en/core/m5paper ; https://www.hackster.io/wenting-zhang/60fps-eink-gameboy-emulator-on-m5papers3-57e4e5
  publisher: M5Stack ; Hackster
  pub_date: unknown
  accessed: 2026-09-26
  confidence: medium
  class: catalog
- claim: X4 Pro review: touchscreen "responsive, with smooth scrolling support via swiping"; refresh "by no means instantaneous, but it's fast enough that it's not bothersome"; lower resolution hurts typography; praises "ecosystem of forks and plugins".
  source: https://abstractnonsense.xyz/microblog/2026-09-07-xteink-x4-pro-review/
  publisher: Abstract Nonsense (personal blog)
  pub_date: 2026-09-07
  accessed: 2026-09-26
  confidence: medium
  class: sentiment
- claim: Cross-fork convergence on the same small set: Sudoku (CrossPet, CrossMux, KOReader), Minesweeper (CrossPet, CrossMux, noah-ing/X4), 2048 (CrossPet, CrossMux, noah-ing/X4), Chess/Chinese Chess (CrossPet, noah-ing/X4, CrossMux), Gomoku (CrossMux, KOReader). None of the Xteink forks found offer multiplayer (device-to-device or pass-and-play) or pen-and-paper 2P games like dots-and-boxes/ultimate tic-tac-toe.
  source: synthesis of https://pocketink.io/firmware/crosspet/, CrossMux apps README, https://github.com/noah-ing/X4, koreader-plugins manifest
  publisher: derived
  pub_date: 2026-08..09
  accessed: 2026-09-26
  confidence: medium
  class: catalog
- claim: Candidate catalog (rules-derived, not usage-evidenced) for slow-refresh touch: Turn-based, no hidden info, 2P pass-and-play friendly: tic-tac-toe, ultimate tic-tac-toe, dots-and-boxes (2+), SOS (2+), Connect Four, Gomoku, Nine Men's Morris, Sprouts, Othello, Checkers, Chess (per-move redraw small). Hidden info (needs device-to-device or hand-off screen): Battleship, Hangman (setter), Mastermind/Bulls-and-Cows code-setter, most card games. 3-6 players: dots-and-boxes, SOS, Hangman (rotating), party word games (KOReader precedent: Boggle/Pictionary/Taboo/Quiz Party). Single-player puzzles map to Tatham MIT set (sudoku/Solo, nonograms/Pattern, Mines, Mastermind/Guess, Loopy, Bridges). Poor fit: real-time (Snake/Tetris/Flappy) and drag-heavy/animation puzzles (PocketPuzzles had to rewrite blitter/drag games).
  source: derived from above sources + standard game rules
  publisher: derived
  pub_date: 2026-09-26
  accessed: 2026-09-26
  confidence: low (no most-played data)
  class: catalog
## Leads worth chasing
- CrossMux S3/Sticky build maturity (docs/engineering/device-variants.md) — nearest competitor on our exact targets.
- CrossInk Games Edition release notes (GitHub releases API returned non-list; check manually) for its X4 Pro game list/touch handling.
- r/xteink threads (reddit blocked to this agent's tools; needs a human or alternate mirror).
- Tatham devel docs ch.3 (drawing API/blitter) to size an ESP32 frontend; Rockbox port as closest embedded precedent.
## Looked for, not found
- Reddit user-voice threads (reddit.com not accessible to search/fetch tools).
- Any CrossPoint upstream issue/discussion about a plugin/app framework, Lua, or multiplayer (GitHub semantic issue search returned 0 for these queries; only #678 games request, closed not planned).
- Any ESP32 port of Tatham puzzles; any ESP-NOW/BLE e-ink multiplayer game project.
- Usage/popularity data (downloads, plays) for any e-ink game.
- Vendor-official X4 Pro panel resolution and touch-controller part.

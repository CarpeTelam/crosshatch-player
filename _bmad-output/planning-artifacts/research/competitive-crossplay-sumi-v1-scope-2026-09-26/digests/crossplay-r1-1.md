# CrossPlay teardown — round 1
Clone: ma-r-s/crossplay branch `xteink` @ 31d94db1 (2026-09-22). All `file@31d94db1` citations have pub_date 2026-09-22.

## Findings
- claim: Shelf registry ships 22 games: Chess, Battleship, Connections, Solitaire, Hearts, D&Diagrams, Insider, Jaipur, Sea Salt, Murdle, Checkers, Connect Four, Yahtzee, Knucklebones, Minesweeper, Sudoku, Picross, Toy Battle, Forehead, Trivia, Wavelength, Go (README also claims 9 apps: Study, Hacker News, xkcd, Get Books, Instapaper, Wallpapers, Wikipedia, Calculator, Notes; plus a "Live" app landing 2026-09-21).
  source: src/apps_local/Shelf.cpp@31d94db1 lines 55-76; README.md@31d94db1
  publisher: ma-r-s (Mario Ruiz)
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: catalog
- claim: Pen-and-paper / simple abstract games in the catalog: Connect Four, Checkers, Chess, Go (9x9/13x13), Battleship, Minesweeper, Sudoku, Picross/D&Diagrams (nonograms), Murdle (logic grid), Yahtzee, Knucklebones. No tic-tac-toe, dots-and-boxes, hangman, mancala or Guess Who (Guess Who is a design doc only, docs/apps/guesswho.md, no src dir).
  source: src/apps_local/ (dir listing)@31d94db1; docs/apps/guesswho.md@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: catalog
- claim: Exactly 10 games have PLAY NEARBY (registered GameIds): Chess, Connect Four, Battleship, Jaipur, Checkers, Yahtzee, Knucklebones, Sea Salt, Toy Battle, Go. The rest are solo/vs-AI or single-device party games.
  source: src/apps_local/link/LinkPlay.h@31d94db1 (enum GameId, kAllGameIds); README.md@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: Pass-and-play (hot-seat) exists only in Chess (Opponent {Computer, PassAndPlay, FaceToFace}; board rotates 180° in PassAndPlay) and Go (Opponent {Computer, Human}, "2 PLAYERS"). Checkers/Connect Four/Yahtzee/Knucklebones/Battleship/Jaipur/Sea Salt/Toy Battle are solo-vs-computer or nearby only; Toy Battle's Mode is {Solo, Link} and its flow says it "does not need a pass-the-device screen badly enough to earn one".
  source: src/apps_local/chess/ChessScreens.h@31d94db1 L39-43; src/apps_local/go/GoFlow.h@31d94db1 L42-45; src/apps_local/toybattle/ToyBattleFlow.h@31d94db1 L24-25
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: medium (grep-based; other games' mode enums not exhaustively read)
  class: feature
- claim: Single-device party games for N people: Insider, Forehead ("one device passed around a room", no per-player scores), Wavelength (one device passed round a table; Mode {CoOp, Teams}), Trivia (read aloud). Hearts is 1 human + 3 AI with no link play: "the radio seats two, and two humans plus two brains is not a shape the DS would have shipped".
  source: docs/apps/forehead.md@31d94db1 L424-426; docs/apps/wavelength.md@31d94db1 L3; src/apps_local/wavelength/WavelengthCore.h@31d94db1 L68; docs/apps/hearts.md@31d94db1 intro
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: feature
- claim: Play Nearby transport is ESP-NOW on fixed Wi-Fi channel 1 (no AP/router/DHCP), broadcast peer for discovery, esp_now_set_wake_window(50ms) power save (unmeasured). It takes over Wi-Fi in STA mode so it cannot coexist with a Wi-Fi connection. Simulator uses UDP on localhost; the browser (Emscripten) build passes packets between two WASM instances via JS (tools_local/wasm/src/link_browser.cpp) into the same receive ring.
  source: src/apps_local/link/LinkRadio.h@31d94db1; src/apps_local/link/LinkRadio.cpp@31d94db1 L190-256, L326-335
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Wire protocol: custom binary, 12-byte header (magic, version=1, type, gameId u16, sequence u16, toss u8, payload len) + payload ≤192 bytes → max 204 bytes, deliberately under ESP-NOW v1's 250-byte limit. Packet types Hello(broadcast), Join, State, Ack(=heartbeat), Bye, Say (1-byte out-of-turn note, e.g. rematch), SayAck. Game state is a trivially-copyable struct sent as raw bytes (no byte-order/padding handling, assumes identical builds); layout changes require bumping GameId.
  source: src/apps_local/link/LinkProtocol.h@31d94db1; src/apps_local/link/LinkPlay.h@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Sync model: whole shared state (not moves) is sent each turn; turns strictly alternate so one sequence counter suffices; first mover by coin toss (toss byte in every packet, ties by lower MAC); unacked State retransmitted every 400ms; heartbeat Ack every 1s; peer declared lost after 10s silence; Hello every 400ms, Join every 200ms; discovery candidates forgotten after 3s. No resume/reconnect: a match lives only while both apps are open, Bye on exit/lock, nothing persisted. Games needing random deals (Jaipur, Sea Salt) have "exactly one device deals".
  source: src/apps_local/link/LinkSession.h@31d94db1 L1-60; src/apps_local/jaipur/JaipurLink.h@31d94db1 L49
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Hard-coded to exactly 2 players: Session holds a single `peer_` Address, a single `candidate_` slot ("One slot rather than a table -- we only ever want the lowest, and everyone picking the lowest is what makes three devices resolve without a cycle"), boolean myTurn_/isHost_, one alternating sequence. Pairing with >2 devices present resolves to disjoint pairs, not a group. Going >2 would need a peer table, turn-order ring instead of bool, per-peer ack tracking, a lobby/host model, and screens (LinkScreens "draws the two seats").
  source: src/apps_local/link/LinkSession.h@31d94db1 L180-205; src/apps_local/link/LinkActivity.h@31d94db1 L1-10
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Receive path is heap-free: SPSC ring of 6 × 204-byte datagrams (~1.3KB) filled by the ESP-NOW callback, drained by main loop; the link core (Protocol/Session/Play) is freestanding C++ (no Arduino, time passed in) so host-tests/link run thousands of matches against a FakeLink that drops/delays/duplicates/reorders.
  source: src/apps_local/link/LinkRadio.h@31d94db1; src/apps_local/link/LinkTransport.h@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Adding a game = new dir under src/apps_local/<game>/ with a freestanding Core (rules) + Screens builder + Activity, one row {"NAME", &icon, &X::create} in Shelf.cpp, an icon; for multiplayer inherit linkplay::LinkActivity (implement ~12 virtuals: linkState, linkGameTitle, linkHeadline, onMatchStart, takeOpponentState, onRematch, onLinkEnded, matchGameOver, onMatchEnded, gameLoop, gameRender) holding a Play<Board> (≤192 bytes), add a GameId, call enterLink(). loop()/render() are final; searching/disconnect/rematch screens and sleep suppression come free. No scripting or data-driven game definition found — all games are hand-written C++.
  source: docs/building-apps.md@31d94db1 L577-635, L931-957; src/apps_local/Shelf.cpp@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Non-blank LOC per game dir (cpp+h, top level): Connect Four ~1.5k, Knucklebones ~1.4k, Checkers ~1.4k, Minesweeper ~1.3k, Solitaire ~1.5k, Yahtzee ~1.9k, Battleship ~1.9k, Sudoku ~2.6k, Chess ~3.1k, Hearts ~3.1k, Jaipur ~3.8k, Go ~3.8k, Sea Salt ~4.2k, Murdle ~4.7k, Toy Battle ~5.8k; link layer ~2.5k; Toybox UI kit ~3.4k (+126KB icon header). Simplest abstract games cost ~1.3–1.5k lines each.
  source: wc over src/apps_local/*/@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: medium (includes embedded data tables in some dirs)
  class: architecture
- claim: Games are built via an LLM-agent cycle: freestanding core + host tests, then three "cold" critic agents (rules fuzzer, look critic on screenshots, play critic with scripted sim walkthroughs), max three rounds, human (Mario) arbitrates. Commit log shows "Claude" as an author and ~1,370 of the visible commits by the maintainer; commits land many per hour.
  source: docs/games-at-scale.md@31d94db1 Phase 4; git shortlog@31d94db1
  publisher: ma-r-s
  pub_date: 2026-08-08 (doc drafted) / 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: trajectory
- claim: Shelf UI scaling limits documented: GAMES folder fits 9 rows, ShelfFolderActivity had a silent 16-item cap, scrolled icons misplaced, and no touch route below the fold — i.e. a flat menu breaks around 10-16 games; they chose a paged flat folder (2026-08-08).
  source: docs/games-at-scale.md@31d94db1 L42-150
  publisher: ma-r-s
  pub_date: 2026-08-08
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: Fork isolation: all fork code in new dirs (src/apps_local/, host-tests/, scripts_local/, tools_local/, assets_local/, site/, server/); LOCAL_SCOPE.md overrides upstream SCOPE.md (which says games are out of scope) and lists upstream "seams". Measured vs last merged upstream commit ce2b4fcd (2026-09-19): 1,606 files changed, +422,429/-1,996; 1,430 files added, 169 upstream files modified (≈95 under src/ and lib/ excluding translations, incl. ActivityManager, Activity.h, main.cpp, HAL, themes, Epub Section/ParsedText, GfxRenderer, web server). Guards are CROSSPLAY_* macros plus FREEINK_DEVICE_* for boards.
  source: git diff ce2b4fcd..31d94db1 (clone); LOCAL_SCOPE.md@31d94db1 L1-60
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high (counts include large generated assets)
  class: architecture
- claim: Upstream merges are frequent and agent-run: a daily cloud-routine runbook merges crosspoint/develop on a sync/upstream-<date> branch; visible sync merges 2026-09-11 (#182) and 2026-09-19 (#207, "CrossPoint 1.6.5 merge"). A 2026-09-19 merge silently deleted a fork seam (finishWifiSessionWithoutRestart), so they added a mechanical "every fork-added line still present" check. freeink-sdk submodule is pinned to Mario's own fork because the upstream PR was closed.
  source: docs/workflow/upstream-sync.md@31d94db1; git log@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: trajectory
- claim: Memory/perf notes: target boards are S3 with 8MB PSRAM; fork enables -DBOARD_HAS_PSRAM on sticky (upstream left it off); Go's MCTS search tree lives in PSRAM (hundreds of KB); Wikipedia zstd contexts in PSRAM. E-ink: partial refresh ~0.3s, full 1-2s; chess spends exactly two partial refreshes per move; 60-move game ≈120 partials = fraction of a percent of 1100mAh; link retry 400ms chosen to hide under ~500ms repaint.
  source: platformio.ini@31d94db1 L297-310; docs/apps/go.md@31d94db1 L179; docs/building-apps.md@31d94db1 L274-283
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: performance
- claim: Browser build: the real src/ and lib/ compiled with em++ (emscripten, CI workflow crossplay-emulator.yml) and published as a GitHub release asset, served on crossplay.ma-r-s.com (Vercel); can run two devices that find each other; network faked from snapshot, sleep off.
  source: README.md@31d94db1; site/README.md@31d94db1; .github/workflows/crossplay-emulator.yml@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: architecture
- claim: License MIT (copyright CrossPoint Reader organization 2025 + Mario Ruiz 2026). Game names used unofficially (Sea Salt & Paper, Jaipur, Toy Battle, Knucklebones) with a disclaimer.
  source: LICENSE@31d94db1; docs/apps/seasalt.md@31d94db1
  publisher: ma-r-s
  pub_date: 2026-09-22
  accessed: 2026-09-26
  confidence: high
  class: license

## Leads worth chasing
- GitHub stars/forks/issues/releases: GitHub API and MCP are blocked for this repo in this session; try WebFetch of github.com pages and docs/release-notes.md in clone.
- User voice: Reddit r/xteink, r/eink; docs/workflow/what-mario-reported.md and docs/open-items.md (people-found defects).
- ESP-NOW peer limits (Espressif docs: 20 peers, 6 encrypted) to size >2-player feasibility.
- Rematch/Say vocabulary and LinkScreens two-seat assumption detail.

## Looked for, not found
- Any scripting/data-driven game definition (none).
- Pass-and-play in Checkers/Connect Four/Yahtzee (not found by grep).
- Any N>2 multiplayer or peer table (none; explicitly 2).

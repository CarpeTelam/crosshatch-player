---
title: 'competitive research: CrossPlay and SUMI teardown for crosshatch-player v1 scope'
type: 'competitive'
topic: 'CrossPlay and SUMI teardown for crosshatch-player v1 scope'
decision: 'What crosshatch-player v1 should include and where it should diverge from CrossPlay'
source: 'native run'
status: draft
preset: 'standard'
validation: 'normal'
created: '2026-09-26'
updated: '2026-09-26'
---

# competitive research: CrossPlay and SUMI teardown for crosshatch-player v1 scope

**Decision this research serves:** What crosshatch-player v1 should include and where it should diverge from CrossPlay


## Executive summary

**Recommendation.** Scope v1 as a **social pen-and-paper game platform**. The core is one multiplayer model in which each player's "seat" can be a person on this device, a person on a nearby device, or the computer. Pass-and-play and Play Nearby for more than 2 players then become the same feature. On top of it, add a Lua scripting layer that is touch-first and multiplayer-aware.

Nobody on these boards combines those pieces:
- **CrossPlay** has Play Nearby, but it is built for exactly two players [7]. Pass-and-play exists only in Chess and Go [3], and every game is hand-written C++ of about 1.3k lines or more [9][10].
- **CrossMux** already ships builds for the Sticky and the X4 Pro, but only with single-player built-in games [53][54].
- **SUMI**, the scripting reference, targets button-only ESP32-C3 devices, ships no Lua games, and has been quiet since May [21][29][31].

**Three findings drive this.**
1. **The gap is social play, not catalog size.** Upstream CrossPoint keeps games out of scope [57][55]. Game forks converge on the same few single-player titles (Sudoku, Minesweeper, 2048, Chess) [52][54][56]. Apart from CrossPlay's 2-player mode, no Xteink fork found offers local multiplayer or pass-and-play [52].
2. **Going beyond 2 players is a design job the radio allows.** ESP-NOW allows 20 peers and broadcast [34][68], but CrossPlay's session, turn order and screens all assume two seats [7][4]. No ESP32 project with 3–8 devices was found [51], so this would be new engineering.
3. **What's worth taking from SUMI is its sandbox recipe, not a library of scripted games.** That recipe is Lua 5.4 compiled as C, which avoids needing C++ exceptions, plus a per-script heap cap, an instruction limit, trimmed libraries and a jailed data folder [22][23][25][70]. It is MIT-licensed [21], but its API is buttons-only and 1-bit, with no refresh control and no multiplayer [24][27][28]. Most of crosshatch-player's scripting API would be new design.

**Biggest caveat.** Demand is not evidenced. Reddit was unreachable, CrossPlay has 12 issues and none are about multiplayer [19], and no usage data exists for any e-ink game. Battery cost of multiplayer is also unmeasured, even by CrossPlay [14].


## 1. CrossPlay teardown

**What it is.** CrossPlay is an MIT-licensed CrossPoint fork by Mario Ruiz for the same two ESP32-S3 boards crosshatch-player targets. Everything below is read from its source at `xteink@31d94db1` (2026-09-22) [1][16].

**Catalog.** The launcher registers 22 games [1]. The pen-and-paper and abstract ones are Connect Four, Checkers, Chess, Go (9×9 and 13×13), Battleship, Minesweeper, Sudoku, Picross, D&Diagrams, Murdle, Yahtzee and Knucklebones [1]. It ships none of the classic paper-and-pencil games: tic-tac-toe, dots-and-boxes, hangman, SOS, mancala. Guess Who exists only as a design doc [1].

**Multiplayer modes.**
- **Play Nearby** covers 10 games: Chess, Connect Four, Battleship, Jaipur, Checkers, Yahtzee, Knucklebones, Sea Salt, Toy Battle and Go [2].
- **Pass-and-play on one device.** Among the two-player board games, only Chess (Computer, Pass-and-play and Face-to-face modes, with the board rotated in pass-and-play) and Go (Computer and Human modes) offer it [3][67]. The other seven board games are solo, against the computer, or Nearby only [3].
- **Games for a room with one device:** Forehead, Wavelength, Insider and Trivia are played by handing one device around. Insider takes 4–8 people, and Wavelength shows a "PASS THE DEVICE" screen [4][67].
- **Hearts** is one human against three computer players. It declines link play because "the radio seats two" [4].

**How Play Nearby works.**
- **Radio and discovery.** It uses ESP-NOW on fixed Wi-Fi channel 1, with no router. Devices find each other through a broadcast Hello and a coin toss decides who moves first. It takes over Wi-Fi, so a match can't run alongside a Wi-Fi connection [5].
- **Messages.** Each packet is a custom binary header plus a payload of at most 192 bytes, 204 bytes in total, which keeps it under ESP-NOW v1's 250-byte limit. The payload is the raw bytes of a C++ struct, so both devices must run the same build [6].
- **Keeping in sync.** Each turn sends the whole game state, not the move. An unacknowledged state is resent every 400 ms, and a silent peer is dropped after 10 s. There is no reconnect or resume [7].
- **No security.** Nothing is encrypted or authenticated, so the design assumes everyone in the room is cooperating [5].

**It is built for exactly two players.** The session stores one peer and uses a single true/false flag for whose turn it is and who hosts. When three devices are present, pairing splits them into separate pairs rather than a group, and the lobby screen draws two seats [7][4].

The ESP-NOW radio itself allows 20 peers [34]. So the limit is in CrossPlay's software design. Going past 2 players means redesigning the session, the turn order and the screens; it is not a matter of changing a constant [7].

Two engineering choices are worth copying:
- **Nothing is allocated when a message arrives.** Incoming packets go into a fixed six-slot buffer.
- **The link logic runs without hardware.** It has no Arduino dependency, so host tests can play thousands of matches over a fake link that drops, delays and reorders packets [8].

**Adding a game.** Every game is hand-written C++. A multiplayer game implements about 12 methods of a shared `LinkActivity` base class, keeps its state in 192 bytes or less, and gets a game ID plus one line in the launcher's game list. The link layer supplies the searching, disconnect and rematch screens [9]. The simplest abstract games take about 1.3–1.5k lines of code each; Chess and Go take about 3–4k [10]. There is no scripting or data-driven way to define a game [9].

**Scaling problems and process.**
- **Launcher.** It broke at 10–16 games: a silent 16-item cap and no touch route to items below the fold. The fix was a paged folder [11].
- **Development process.** Games are built by LLM agents, with "critic" agents checking rules, looks and play [11].
- **Drift from upstream.** Measured against the last merged upstream commit, the fork adds 1,430 files and modifies 169 upstream files [12]. An agent merges upstream daily. One such merge silently deleted a piece of fork code the fork relied on, and they then added a check that every fork-added line is still present [13].

**Hardware use.** On the Sticky, the fork turns on PSRAM (`-DBOARD_HAS_PSRAM`), which upstream leaves off. Go's search tree lives in PSRAM. Partial refreshes take about 0.3 s and full refreshes 1–2 s, and Chess spends exactly two partial refreshes per move [14]. A WebAssembly build of the real firmware runs in a browser, including two devices playing each other [15].

**Momentum and users.**
- **Size:** 74 stars and 16 forks at access [17].
- **Releases** are frequent and cut by a bot, but the recent ones are about "Live", a phone-to-device picture app. The last Play Nearby commits in the fetched history are from 2026-09-04 [18] (confidence: medium, since the clone only goes back 200 commits).
- **User feedback is thin:** 12 issues, none about multiplayer. The only game request (#202) asks for word lists for party games [19].
- **Roadmap:** the maintainer's own records show 1 of 378 work cards came from a user other than the maintainer. Most were found by LLM agents [20].

## 2. SUMI teardown: what its Lua system actually is

**What it is.** SUMI is MIT-licensed firmware for the ESP32-C3 Xteink X4/X3. Those devices have buttons and no touchscreen, so SUMI does not target crosshatch-player's boards [21]. Its lineage is CrossPoint → Papyrix → SUMI [21].

**The engine.**
- **Lua build.** It uses stock PUC Lua 5.4.7, compiled as C with `LUA_32BITS`, and connects it to the firmware with hand-written C functions rather than a binding library [22].
- **Error handling.** Every callback into a script runs through `lua_pcall`. Because Lua is compiled as C, errors use setjmp/longjmp rather than C++ exceptions [23][38], which is the pattern that works with crosshatch-player's `-fno-exceptions` build.
- **Trimmed libraries.** Lua's startup list of libraries is cut to base, table, string, math and utf8. So `require`, `io`, `os`, `debug` and coroutines don't exist, and game logic must be written as explicit state machines [25].

**Limits and sandboxing.**
- **Memory and time.** A custom allocator caps each script's VM at 40 KB of regular heap. Scripts are at most 16 KB of source, and a hook raises an error after 100,000 VM instructions in one callback [23].
- **Errors.** Any error shows a "Lua Error" screen instead of crashing the device [23].
- **Files.** Scripts can only read and write under `/custom/<name>_data/`, with path traversal rejected and atomic writes [24].
- **Removed globals.** `load`, `dofile` and the `raw*` functions are set to nil [24].

**How scripts plug in.**
- **Discovery.** The firmware scans `/custom/*.lua` (flat, no subfolders) and loads at most 8 scripts. There is no manifest or icon; a script's display name comes from its filename [26].
- **Callbacks.** A script must define `draw()` and `onButton(btn)`. `init(w,h)` is optional, and so is `update()`, which runs at 10 Hz [23].
- **API.** It has 46 flat globals: 1-bit drawing primitives, text, 7 UI helpers, `millis`/`random`, sandboxed file functions, time and battery [24].
- **What's missing.** There is no touch input, no button-release events, no bitmaps or sprites and no greyscale [24]. Scripts can't control the e-ink refresh: the host redraws everything on every render and forces a full refresh every 30 renders [27].
- **Networking.** The only networking is a BLE "bridge" that swaps JSON messages of up to 480 bytes with a web page. There is no device-to-device or multiplayer support [28].

**What ships in Lua is small.**
- **Games.** Every game SUMI ships (Chess, Sudoku, Minesweeper, Checkers, 2048, a Game Boy emulator) is C++. The only Lua example in the repo is an 86-line doorbell script [29].
- **Author tooling.** Authoring is AI-first: a copy-paste LLM prompt, plus a browser playground running Fengari. Fengari is Lua 5.3, not the device's 5.4, and it doesn't simulate file I/O. There is no hot reload, and installing a script requires a reboot [30].
- **Sharing.** There is no gallery or sharing mechanism [30].
- **Docs vs firmware.** The repo docs describe a `sumi.*` namespace and `os.time()`, but the firmware exposes neither, and a file-I/O bug report (#20) was closed without a reply [32].

**Maturity.** It is one author and 47 commits, each a release dump. The last commit was 2026-05-30, about four months before this research. It has about 180 stars [31]. PocketInk calls it "the only genuine on-device scriptable plugins in the scene" [33].

**Takeaway.** What's worth borrowing from SUMI is its sandbox recipe and its MIT code, not a library of scripted games, because none exists. crosshatch-player would be the first project to actually build a touch-first, multiplayer-capable scripted game platform on these devices.

## 3. Feasibility on ESP32-S3: scripting engines, multi-device radio, pass-and-play

### Scripting engines

| Engine | Footprint (vendor-reported) | Works with `-fno-exceptions` | License | ESP32 track record | Watch-outs |
|---|---|---|---|---|---|
| **Lua 5.4** (compiled as C) | Not measured on ESP32 in this run. SUMI caps each VM at 40 KB [23]. NodeMCU reports as little as 17 KB of free RAM for apps under Lua 5.1 [46] | Yes, when compiled as C (setjmp/longjmp) [38][70][23] | MIT [45] | SUMI [21], NodeMCU [46], Espressif's own IDF component guide [45] | A Lua error jumps over C++ stack frames without running their destructors. sol2 without exceptions loses error catching for bound C++ functions [39] |
| **Berry** | Core under 40 KiB of code [40]. "Less than 4KiB heap" on a Cortex-M4 [72]. On ESP32, Tasmota says RAM usage "starts at ~10KB", unverified by a second source; it uses PSRAM [41] | Not confirmed in this run | MIT [40][72] | Tasmota, the largest ESP32 scripting deployment found [41] | Tasmota advises scripts never block for more than 50 ms [41]. Much smaller author community than Lua |
| **MicroPython** (embed port) | 8 KB heap in the official example [44] | C library | MIT | Many ESP32 users, but the embed port is young | The host must supply the correct C stack top for its garbage collector [44] |
| **wasm3** | About 64 KB of code and 10 KB RAM [42] | C | MIT | Supports ESP32 | In "minimal maintenance" [42] |
| **Elk (JS)** | About 20 KB of flash [43] | C | **AGPLv3 or commercial** [43] | Small | No arrays or closures [43]. Unsuitable |

The engine comparison rests on vendor-reported numbers. No benchmark comparing these engines on the S3 was found (see open questions).

### Radios for multiplayer

**ESP-NOW limits.**
- **Peers:** at most 20. Some of them can be encrypted: the default is 6–7 (the header and the docs differ) and the configurable maximum is 17 [34][68].
- **Payload size:** 250 B in v1 and 1,470 B in v2. A v1 device only receives v2 packets of 250 B or less [34][68]. Arduino-ESP32 exposes v2 through `getVersion()`/`getMaxDataLen()`, added in PR #11524 and listed in the 3.3.0 release, which is built on ESP-IDF 5.5 [69].
- **Broadcast:** supported, and it needs no pairing, so it can carry a lobby [34].
- **Delivery:** only acknowledged at the radio (MAC) layer, so games need their own sequence numbers and acks [34].
- **Shared hardware keys:** the SoftAP station slots and encrypted ESP-NOW peers share the same encryption keys, so each reduces what's left for the other [35].

**BLE on S3.** At most 10 BLE activities in total: connections, scanning and advertising combined, each using about 828 B. The NimBLE host defaults to 3 connections [36][37]. A device acting as a hotspot (SoftAP) has been run with 15 stations on S3 [47] (confidence: medium).

**What exists already.** Public ESP32 multiplayer projects are almost all 2-player, or a hotspot that phones join through a browser. No 3–8-device turn-based ESP-NOW game was found [51].

**Our inference (not a sourced claim).** ESP-NOW broadcast for discovery, with one host device keeping the authoritative game state, is the best fit for 3–8 players on these boards. That's also the direction CrossPlay's existing design would extend in.

### Pass-and-play

**Existing designs.** Board-game apps with hidden hands hide the current player's hand at "End Turn" [50]. DuelBox's spec requires a full blackout "pass to <player>" screen and "no leak of hidden state in any frame during hand-off", and each game opts in through its manifest [49]. Ticket to Ride and Nature ship pass-and-play for up to 4 players [66] (confidence: medium).

**E-ink ghosting leaks hidden information.** On a Kobo, typed password keys stayed faintly visible after a partial refresh until the next full refresh [48]. So a hand-off screen that hides information has to use a full refresh. That's our design inference from [48].

## 4. The wider field and the game catalog

**Competitors.**
- **CrossMux** (0x1abin, 219 stars) is a CrossPoint fork already publishing Nightly builds for the **Seeed Sticky and the X4 Pro**. It says the target list "is not a claim that every feature has passed hardware acceptance" [53]. Its apps are compiled in and listed in one table: Sudoku, Gomoku, Minesweeper, 2048, Chinese Chess, Sokoban and others. Most of the games are hidden on a fresh install. It has no scripting and no multiplayer [54].
- **CrossInk Games Edition** is X4 Pro-only, created 2026-09-14, with 1 star. It notes that games are "deliberately out of scope" in both CrossInk and CrossPoint [55].
- **Upstream CrossPoint** closed issue #678 ("Add Quick Mental Games") as not planned [57]. Games have to live in the fork.
- **Other Xteink forks** (CrossPet, noah-ing/X4) all converge on Sudoku, Minesweeper, 2048 and Chess [52][56]. None of the Xteink forks found has local multiplayer, pass-and-play, or 2-player pen-and-paper games [52][54][56].
- **KOReader** is the precedent for scripted games on e-ink. One index lists 70 Lua game plugins, including Battleship, Connect4, Hangman, Mastermind, Gomoku, backgammon, about 20 pencil-puzzle variants, and four single-device "Party" games (Boggle, Pictionary, Quiz, Taboo). It also has a shared game library and an on-device plugin manager [58].

**Borrowing puzzles.** Simon Tatham's Portable Puzzle Collection is MIT-licensed [59] and has 41 puzzles, all single-player. The e-ink ports needed rework for each game: greyscale instead of colour, no dragging, thicker lines [60]. The Kindle port notes that some games need right-click or a keyboard [61]. No ESP32 port was found [60][61].

**Hardware.**
- **reTerminal Sticky:** ESP32-S3R8 with 8 MB PSRAM and 32 MB flash. 3.97" 800×480 display with 4 grey levels, capacitive touch, Wi-Fi 4 and BLE 5.0, 750 mAh battery [63].
- **X4 Pro:** 4.3" touchscreen at 219 PPI, 1,100 mAh battery, charged through pogo pins. Its chip was not disclosed by the vendor; reports say ESP32-S3 [62]. Upstream CrossPoint's `x4pro` build targets an ESP32-S3 board profile with 16 MB flash and 8 MB octal PSRAM, and turns PSRAM on [71]. A third-party issue agrees [65]. Both are build settings, not a datasheet.
- **Refresh in use:** a hands-on review calls the X4 Pro's touch responsive and its refresh "fast enough that it's not bothersome" [64].

**Candidate catalog.** This is derived from game rules, not usage data, so confidence is low.

| Fit | Games | Players | Hidden info → needs |
|---|---|---|---|
| Pen-and-paper, open information | Tic-tac-toe, Ultimate tic-tac-toe, Dots and Boxes, SOS, Sprouts, Nine Men's Morris, Connect Four\*, Gomoku, Othello, Checkers\* | 2; Dots and Boxes and SOS work with 3+ | None. Pass-and-play is trivial |
| Hidden information | Battleship\*, Hangman, Mastermind / Bulls and Cows, most card games | 2; Hangman can rotate the setter among 3+ | Play Nearby, or a pass-and-play hand-off with a full refresh |
| Solo puzzles (Tatham, MIT) | Sudoku, nonograms, Minesweeper, Mastermind/Guess, Loopy, Bridges | 1 | None |
| Poor fit | Real-time games (Snake, Tetris, Flappy), drag-heavy puzzles | — | — |

\* Already in CrossPlay [1].

## Cross-dimension insights

1. **Pass-and-play, Play Nearby and more-than-2-player play fit one "seat" model.** CrossPlay built them as three separate mechanisms:
   - a 2-player radio link [7]
   - per-game opponent settings for hot-seat play [3]
   - games with no scores where one device is handed around [4]

   A model where each seat is a person on this device, a person nearby or the computer would give every game all three modes at once. It also gives hidden-information games one shared hand-off screen with a full refresh [48][49]. This is our inference from the sources, not a pattern any of them documents.
2. **Scripted games and multiplayer fit together naturally.** CrossPlay syncs the whole game state, capped at 192 B, rather than individual moves [6][7]. A script that declares its game state as a small serializable table could get Play Nearby and pass-and-play from the host without ever touching the radio. SUMI shows that sending messages from a script works, but only to a phone over BLE [28]. This is inference, and it needs a spike to confirm the state fits in the payload (1,470 B with ESP-NOW v2 [69]).
3. **E-ink refresh constrains every area.**
   - CrossPlay timed its 400 ms resend to hide under a repaint [14].
   - SUMI scripts can't control refresh at all [27].
   - Ghosting leaks hidden information [48].
   - Tatham ports needed rework for each game [60].

   So refresh policy must be part of the script API (dirty regions and a way to request a full refresh), not left entirely to the host.
4. **Catalog size breaks launchers early.** CrossPlay's launcher broke at 10–16 games [11], and SUMI caps scripts at 8 with no manifest [26]. A scripted catalog needs manifest metadata (name, icon, player counts, modes) and a paged launcher from the start.
5. **Fork discipline is a competitive factor.** CrossPlay modifies 169 upstream files and has already lost fork code in an automated merge [12][13]. Upstream will not take games [57]. So all game work lives in the fork permanently, and keeping it purely additive is what keeps upstream merges cheap.

## Recommendations

Each recommendation names its confidence and the downstream document it feeds.

1. **V1 scope: position around social play.** This feeds the product brief's positioning and the PRD's differentiation. The pitch would be: "pen-and-paper games you play *together*, on one device or across several nearby, and new games anyone can script." Confidence: medium. The absence of competitors is well evidenced [52][53][54]; demand is not [19].
2. **Build the seat-based multiplayer core first.** This is an architecture spine decision. The design would be:
   - **Discovery:** an ESP-NOW broadcast lobby.
   - **State:** one host device holds the authoritative game state and sends it all each turn, with sequence numbers and app-level acks [34][7].
   - **Players:** 2–8 seats, each a person on this device, a person nearby or the computer.
   - **Hand-off:** hidden-information games get a full-refresh hand-off screen [48][49].

   Borrow CrossPlay's proven patterns as ideas, not code: a message buffer that never allocates, link logic that runs without hardware, and a fake-link test harness [8]. Its session code assumes two players throughout, so reuse is limited [7]. Confidence: medium on the radio limits [34][68]; low on 3–8-player reliability and battery, which nobody has measured [51][14].
3. **Scripting: Lua 5.4 compiled as C, in S3-only code behind a capability guard.** This is an architecture constraint. Start from SUMI's MIT sandbox recipe [22][23][25]. Then fill its gaps:
   - touch events
   - control over which screen regions refresh
   - greyscale and bitmaps
   - a manifest with an icon
   - a heap in PSRAM rather than a 40 KB cap (both boards have 8 MB PSRAM [63][71])
   - a seat and state-sync API

   Confidence: medium. Lua has the ecosystem precedent: SUMI [21], about 70 KOReader Lua games [58], and Espressif's own IDF component [45]. Berry is the credible alternative, with the stronger ESP32 production record [41], but its ~10 KB RAM figure is unverified. Settle the choice with a footprint spike on the S3 before committing.
4. **Author experience beats API breadth.** This feeds the PRD's non-functional requirements. SUMI's weak points were a playground running a different Lua version, docs that contradict the firmware, reboot-to-install and no gallery [30][32]. CrossPlay's in-browser WebAssembly build, with two devices playing each other, is the model to match for testing scripts [15]. Confidence: medium.
5. **First-party v1 catalog.** This feeds the PRD's feature list. Pick games that demonstrate each mode:
   - **More than 2 players:** Dots and Boxes and SOS.
   - **Hidden information:** Hangman and Mastermind / Bulls and Cows, to exercise the hand-off screen and Play Nearby.
   - **Quick 2-player games:** tic-tac-toe and Ultimate tic-tac-toe.
   - **Optionally:** Nine Men's Morris and Sprouts.

   Avoid real-time and drag-heavy games [60][61]. Write at least two of these as Lua scripts, to prove the scripting API. Confidence: low. The catalog is derived from game rules; there is no usage data.
6. **Watch CrossMux.** It is the nearest competitor on the same hardware and has more stars than CrossPlay [53][17]. Recheck it by 2026-12-25 for any move into multiplayer or scripting. Confidence: high on its current state [53][54].

## Open questions

| Question | Why it matters | How to answer |
|---|---|---|
| Do X4 Pro and Sticky owners want multiplayer or scripting? | Recommendation 1 rests on a competitive gap, not demonstrated demand | Run a user-voice research pass from a network that can reach Reddit (r/xteink, r/eink), or post a short poll |
| Lua or Berry: real footprint and speed on the S3 with PSRAM | Recommendation 3; no benchmark on the S3 was found | A 1-day spike: run the same small game in both engines and measure heap and frame time |
| Is ESP-NOW reliable with 3–8 devices, and what does a match cost in battery? | Recommendation 2; there is no reference project, and CrossPlay never measured battery | A prototype lobby tested on real devices, plus a fake-link test harness |
| Which Arduino-ESP32 and ESP-IDF version does this repo's pioarduino build pin? | ESP-NOW v2 (1,470 B payloads) needs Arduino-ESP32 3.3.0 or later with IDF 5.5 [69] | Check `platformio.ini`. This is a project check for the architect, not web research |
| Should crosshatch-player devices be able to play with CrossPlay devices? | CrossPlay's protocol assumes identical builds and raw struct payloads [6] | A product decision. Probably not in v1 |
| X4 Pro panel resolution and touch controller | These affect layouts and touch handling | Read the freeink-sdk board config, or measure on the device |

## Source appendix

| # | Claim or finding it supports | Publisher | Pub date | Accessed | Confidence |
|---|---|---|---|---|---|
| [1] | 22-game launcher list and pen-and-paper subset | [ma-r-s/crossplay, src/apps_local/Shelf.cpp and README](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/Shelf.cpp) | 2026-09-22 | 2026-09-26 | high |
| [2] | 10 Play Nearby games (GameId enum) | [ma-r-s/crossplay, link/LinkPlay.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkPlay.h) | 2026-09-22 | 2026-09-26 | high |
| [3] | Pass-and-play only in Chess and Go | [ma-r-s/crossplay, chess/ChessScreens.h and go/GoFlow.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/chess/ChessScreens.h) | 2026-09-22 | 2026-09-26 | medium |
| [4] | Single-device party games; Hearts "the radio seats two"; the lobby screen draws two seats | [ma-r-s/crossplay, docs/apps/hearts.md, forehead.md, wavelength.md, link/LinkScreens.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/apps/hearts.md) | 2026-09-22 | 2026-09-26 | high |
| [5] | ESP-NOW on channel 1, takes over Wi-Fi, unencrypted peers | [ma-r-s/crossplay, link/LinkRadio.cpp](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkRadio.cpp) | 2026-09-22 | 2026-09-26 | high |
| [6] | 12-byte header plus 192-byte payload (204 B max); raw struct payload | [ma-r-s/crossplay, link/LinkProtocol.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkProtocol.h) | 2026-09-22 | 2026-09-26 | high |
| [7] | Whole state sent each turn, 400 ms resend, 10 s drop, no resume; single peer and true/false turn flag | [ma-r-s/crossplay, link/LinkSession.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkSession.h) | 2026-09-22 | 2026-09-26 | high |
| [8] | Fixed six-slot receive buffer; link logic runs without hardware; FakeLink host tests | [ma-r-s/crossplay, link/LinkRadio.h and LinkTransport.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/link/LinkTransport.h) | 2026-09-22 | 2026-09-26 | high |
| [9] | Adding a game: LinkActivity with about 12 methods, state of 192 B or less, one line in the launcher; no scripting | [ma-r-s/crossplay, docs/building-apps.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/building-apps.md) | 2026-09-22 | 2026-09-26 | high |
| [10] | About 1.3–1.5k lines per simple game (line counts measured on the clone) | [ma-r-s/crossplay, src/apps_local](https://github.com/ma-r-s/crossplay/tree/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local) | 2026-09-22 | 2026-09-26 | medium |
| [11] | LLM-agent build cycle; launcher broke at 10–16 games | [ma-r-s/crossplay, docs/games-at-scale.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/games-at-scale.md) | 2026-08-08 | 2026-09-26 | high |
| [12] | 1,430 files added and 169 upstream files modified vs upstream ce2b4fcd (git diff on the clone) | [ma-r-s/crossplay, LOCAL_SCOPE.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/LOCAL_SCOPE.md) | 2026-09-22 | 2026-09-26 | high |
| [13] | Daily agent-run upstream merges; one silently deleted fork code | [ma-r-s/crossplay, docs/workflow/upstream-sync.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/workflow/upstream-sync.md) | 2026-09-22 | 2026-09-26 | high |
| [14] | PSRAM turned on for the Sticky; refresh timings; two partial refreshes per chess move | [ma-r-s/crossplay, docs/building-apps.md and platformio.ini](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/platformio.ini) | 2026-09-22 | 2026-09-26 | high |
| [15] | WebAssembly browser build with two devices playing each other | [ma-r-s/crossplay, README.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/README.md) | 2026-09-22 | 2026-09-26 | high |
| [16] | MIT license | [ma-r-s/crossplay, LICENSE](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/LICENSE) | 2026-09-22 | 2026-09-26 | high |
| [17] | 74 stars, 16 forks | [GitHub, ma-r-s/crossplay repo page](https://github.com/ma-r-s/crossplay) | 2026-09-26 | 2026-09-26 | medium |
| [18] | Release cadence; recent work on the "Live" app | [GitHub, ma-r-s/crossplay releases](https://github.com/ma-r-s/crossplay/releases) | 2026-09-22 | 2026-09-26 | medium |
| [19] | 12 issues, none about multiplayer; #202 asks for party-game word lists | [GitHub, ma-r-s/crossplay issue #202](https://github.com/ma-r-s/crossplay/issues/202) | 2026-09-13 | 2026-09-26 | medium |
| [20] | 1 of 378 work cards from a non-maintainer user | [ma-r-s/crossplay, docs/workflow/what-mario-reported.md](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/docs/workflow/what-mario-reported.md) | 2026-09-06 | 2026-09-26 | high |
| [21] | SUMI: MIT, ESP32-C3 X4/X3, buttons only; CrossPoint → Papyrix → SUMI | [psychoplath9450/SUMI, LICENSE and README](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/README.md) | 2026-05-30 | 2026-09-26 | high |
| [22] | PUC Lua 5.4.7 compiled as C, LUA_32BITS, hand-written bindings | [psychoplath9450/SUMI, lib/lua54](https://github.com/psychoplath9450/SUMI/tree/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/lib/lua54) | 2026-05-30 | 2026-09-26 | high |
| [23] | 40 KB VM cap, 16 KB scripts, 100k-instruction hook, lua_pcall, error screen, callbacks | [psychoplath9450/SUMI, src/plugins/LuaPlugin.cpp](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/plugins/LuaPlugin.cpp) | 2026-05-30 | 2026-09-26 | high |
| [24] | 46-global API; sandboxed file paths; no touch or bitmaps | [psychoplath9450/SUMI, src/plugins/LuaBindings.h](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/plugins/LuaBindings.h) | 2026-05-30 | 2026-09-26 | high |
| [25] | linit trimmed: no require, io, os, debug or coroutines | [psychoplath9450/SUMI, lib/lua54/linit.c](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/lib/lua54/linit.c) | 2026-05-30 | 2026-09-26 | high |
| [26] | Flat /custom/*.lua scan, at most 8 scripts, no manifest | [psychoplath9450/SUMI, src/states/PluginListState.cpp](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/states/PluginListState.cpp) | 2026-05-30 | 2026-09-26 | high |
| [27] | Host redraws everything; full refresh every 30 renders; no refresh control for scripts | [psychoplath9450/SUMI, src/states/PluginHostState.cpp](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/states/PluginHostState.cpp) | 2026-05-30 | 2026-09-26 | high |
| [28] | BLE bridge with 480 B JSON; no device-to-device play | [psychoplath9450/SUMI, docs/PLUGIN_BRIDGE.md](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/docs/PLUGIN_BRIDGE.md) | 2026-04 | 2026-09-26 | high |
| [29] | Built-in games are C++; the only Lua example is doorbell.lua | [psychoplath9450/SUMI, docs/plugin_examples](https://github.com/psychoplath9450/SUMI/tree/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/docs/plugin_examples) | 2026-05-30 | 2026-09-26 | high |
| [30] | AI-prompt authoring; Fengari (Lua 5.3) playground; reboot to install; no gallery | [sumi.page, plugins](https://sumi.page/plugins/) | unknown | 2026-09-26 | high |
| [31] | One author, 47 commits, last commit 2026-05-30, about 180 stars | [GitHub, psychoplath9450/SUMI](https://github.com/psychoplath9450/SUMI) | 2026-09-26 | 2026-09-26 | high |
| [32] | Docs describe sumi.* and os.time, which the firmware doesn't expose; #20 closed with no reply | [GitHub, SUMI issue #20](https://github.com/psychoplath9450/SUMI/issues/20) | 2026-05-02 | 2026-09-26 | high |
| [33] | "The only genuine on-device scriptable plugins in the scene" | [PocketInk, SUMI firmware page](https://pocketink.io/firmware/sumi/) | 2026-05 or later | 2026-09-26 | high |
| [34] | ESP-NOW: 20 peers (7 to 17 encrypted), 250/1470 B payloads, broadcast, delivery acked only at the radio layer | [Espressif, ESP-IDF ESP-NOW API reference (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_now.html) | unknown | 2026-09-26 | high |
| [35] | SoftAP and ESP-NOW share encryption keys | [Espressif, esp-idf esp_wifi_types_generic.h](https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_wifi/include/esp_wifi_types_generic.h) | unknown | 2026-09-26 | high |
| [36] | S3/C3 BLE controller: at most 10 activities, 828 B each | [Espressif, esp-idf bt/controller/esp32c3/Kconfig.in](https://raw.githubusercontent.com/espressif/esp-idf/master/components/bt/controller/esp32c3/Kconfig.in) | unknown | 2026-09-26 | medium |
| [37] | NimBLE host defaults to 3 connections | [Espressif, esp-idf nimble Kconfig.in](https://raw.githubusercontent.com/espressif/esp-idf/master/components/bt/host/nimble/Kconfig.in) | unknown | 2026-09-26 | medium |
| [38] | Lua compiled as C uses setjmp/longjmp; compiled as C++ uses throw | [Lua.org, lua/ldo.c](https://github.com/lua/lua/blob/master/ldo.c) | unknown | 2026-09-26 | high |
| [39] | sol2 SOL_NO_EXCEPTIONS disables catching errors from bound C++ functions | [sol2 docs, exceptions](https://sol2.readthedocs.io/en/latest/exceptions.html) | unknown | 2026-09-26 | high |
| [40] | Berry core under 40 KiB, MIT | [berry-lang, README](https://github.com/berry-lang/berry) | unknown | 2026-09-26 | high |
| [41] | Berry RAM "starts at ~10KB", uses PSRAM, don't block more than 50 ms | [Tasmota, Berry docs](https://tasmota.github.io/docs/Berry/) | unknown | 2026-09-26 | high |
| [42] | wasm3 in minimal maintenance; about 64 KB code and 10 KB RAM | [wasm3, README](https://github.com/wasm3/wasm3) | unknown | 2026-09-26 | high |
| [43] | Elk is AGPLv3 or commercial; no arrays or closures | [Cesanta, elk README](https://github.com/cesanta/elk) | unknown | 2026-09-26 | high |
| [44] | MicroPython embed port; 8 KB heap example; host supplies the stack top | [MicroPython, examples/embedding/main.c](https://raw.githubusercontent.com/micropython/micropython/master/examples/embedding/main.c) | unknown | 2026-09-26 | high |
| [45] | Lua as an ESP-IDF component, MIT, scripts loaded from a filesystem | [Espressif Developer Portal, Lua as an ESP-IDF component](https://developer.espressif.com/blog/using-lua-as-esp-idf-component-with-esp32/) | 2024-10-22 (updated 2026-04-29) | 2026-09-26 | high |
| [46] | NodeMCU Lua 5.1: as little as 17 KB of RAM for apps; LTR saves 20–25 KB | [NodeMCU, Lua developer FAQ](https://nodemcu.readthedocs.io/en/dev-esp32/lua-developer-faq/) | unknown | 2026-09-26 | high |
| [47] | SoftAP with 15 stations on S3 in the field | [espressif/esp-idf, issue #10511](https://github.com/espressif/esp-idf/issues/10511) | 2023-01 | 2026-09-26 | medium |
| [48] | E-ink ghosting leaked typed keys until a full refresh | [Good e-Reader](https://goodereader.com/blog/kobo-ereader-news/e-ink-ghosting-effect-can-reveal-sensitive-info-such-as-passwords) | 2024-05-08 | 2026-09-26 | high |
| [49] | Pass-and-play hand-off: full blackout, no hidden state in any frame, opt-in per game through its manifest | [DuelBox, issue #134](https://github.com/DuelBox/DuelBox-Web/issues/134) | 2026-08-19 | 2026-09-26 | high |
| [50] | Hotseat hides the current player's hand at End Turn | [Tabletopia help center](https://help.tabletopia.com/knowledge-base/game-modes-solo-hotseat-online/) | unknown | 2026-09-26 | high |
| [51] | ESP32 multiplayer projects are 2-player or phones-join-a-hotspot | [GitHub hobby projects, e.g. gamebox-esp32](https://github.com/nirinovich/gamebox-esp32) | unknown | 2026-09-26 | medium |
| [52] | About 20 CrossPoint forks listed; game forks converge on the same games; most forks drop BLE on the C3 | [PocketInk, firmware directory](https://pocketink.io/firmware/) | 2026-08-24 | 2026-09-26 | high |
| [53] | CrossMux Nightly builds for Sticky and X4 Pro | [0x1abin/crossmux, README](https://github.com/0x1abin/crossmux) | 2026-09 | 2026-09-26 | high |
| [54] | CrossMux games compiled in, hidden by default; no scripting or multiplayer | [0x1abin/crossmux, src/activities/apps/README.md](https://github.com/0x1abin/crossmux/blob/HEAD/src/activities/apps/README.md) | 2026-09 | 2026-09-26 | high |
| [55] | CrossInk Games Edition X4 Pro-only; games out of scope in CrossInk and CrossPoint | [LegendaryDrogan/crossink-games-edition, README](https://github.com/LegendaryDrogan/crossink-games-edition) | 2026-09-22 | 2026-09-26 | high |
| [56] | noah-ing/X4: Chess, Minesweeper, Snake, 2048 on the C3 | [noah-ing/X4](https://github.com/noah-ing/X4) | 2026-08-20 | 2026-09-26 | medium |
| [57] | CrossPoint #678 (games request) closed as not planned | [crosspoint-reader, issue #678](https://github.com/crosspoint-reader/crosspoint-reader/issues/678) | 2026-02-03 | 2026-09-26 | medium |
| [58] | 70 KOReader Lua game plugins, including party games and a plugin manager | [t2ym5u/koreader-plugins](https://github.com/t2ym5u/koreader-plugins) | 2026 | 2026-09-26 | high |
| [59] | Tatham puzzles are MIT | [Simon Tatham, licence](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/licence.html) | unknown | 2026-09-26 | high |
| [60] | PocketBook port needed rework for each game (greyscale, no dragging) | [SteffenBauer/PocketPuzzles](https://github.com/SteffenBauer/PocketPuzzles) | 2020–2026 | 2026-09-26 | high |
| [61] | Kindle port: some games need right-click or a keyboard | [kbarni/kindlepuzzles](https://github.com/kbarni/kindlepuzzles) | unknown | 2026-09-26 | high |
| [62] | X4 Pro specs; chip not disclosed by the vendor (reported as ESP32-S3) | [CNX Software, X4 Pro](https://www.cnx-software.com/2026/07/23/99-xteink-x4-pro-4-3-inch-touchscreen-ereader-to-support-crosspoint-reader-open-source-firmware/) | 2026-07-23 | 2026-09-26 | medium |
| [63] | reTerminal Sticky: ESP32-S3R8, 8 MB PSRAM, 800×480, touch, BLE 5 | [CNX Software, reTerminal Sticky](https://www.cnx-software.com/2026/07/31/reterminal-sticky-3-97-inch-magnetic-touch-epaper-display-is-supported-by-four-open-source-firmware-projects/) | 2026-07-31 | 2026-09-26 | high |
| [64] | X4 Pro touch is responsive; refresh "not bothersome" | [Abstract Nonsense, X4 Pro review](https://abstractnonsense.xyz/microblog/2026-09-07-xteink-x4-pro-review/) | 2026-09-07 | 2026-09-26 | medium |
| [65] | X4 Pro is an ESP32-S3 with 8 MB PSRAM | [clackups/draftling, issue #40](https://github.com/clackups/draftling/issues/40) | 2026-08-18 | 2026-09-26 | medium |
| [66] | Ticket to Ride and Nature ship pass-and-play for up to 4 players | [App Store, Nature board game](https://apps.apple.com/us/app/nature-board-game/id6738703558) | unknown | 2026-09-26 | medium |
| [67] | Verification: pass-and-play modes in Chess and Go; Insider (4–8 people) and Wavelength's "PASS THE DEVICE" | [ma-r-s/crossplay, wavelength/WavelengthCore.h and insider/InsiderActivity.h](https://github.com/ma-r-s/crossplay/blob/31d94db1e10f6fd40a44cf6fb6a6768edc6efa00/src/apps_local/wavelength/WavelengthCore.h) | 2026-09-22 | 2026-09-26 | high |
| [68] | esp_now.h: 20 total peers, 6 encrypted by default, 250/1470 B payloads | [Espressif, esp-idf esp_now.h](https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_wifi/include/esp_now.h) | unknown | 2026-09-26 | high |
| [69] | Arduino-ESP32 ESP-NOW v2 support (PR #11524, merged 2025-06-30; listed in the 3.3.0 release) | [espressif/arduino-esp32, PR #11524](https://github.com/espressif/arduino-esp32/pull/11524) | 2025-06-30 | 2026-09-26 | high |
| [70] | Lua uses longjmp for errors, or exceptions if compiled as C++ | [Lua.org, Lua 5.4 manual §4.4](https://www.lua.org/manual/5.4/manual.html) | unknown | 2026-09-26 | high |
| [71] | Upstream x4pro build: esp32-s3-devkitc1-n16r8 board profile, octal PSRAM, BOARD_HAS_PSRAM | [crosspoint-reader, platformio.ini (develop)](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/platformio.ini) | 2026-09 | 2026-09-26 | high |
| [72] | Berry is MIT; runs on "less than 4KiB heap" on a Cortex-M4 | [berry-lang docs site](https://berry-lang.github.io/) | unknown | 2026-09-26 | high |

## Staleness map

This was computed with `recon_kit.py staleness` using the pack's freshness windows: features 3 months, trajectory 6, sentiment 12, versions 1, architecture 24, performance and catalog 12.

| Claim | Class | Published | Recheck by |
|---|---|---|---|
| [69] Arduino-ESP32 ESP-NOW v2 support | version | 2025-06-30 | **overdue**: recheck against the pinned platform before relying on it |
| [71] X4 Pro is an ESP32-S3 with 8 MB PSRAM | version | 2026-09 | 2026-10-26 |
| [31] SUMI quiet since May | trajectory | 2026-05-30 | 2026-11-30 |
| [3] Pass-and-play only in Chess and Go (CrossPlay) | feature | 2026-09-22 | 2026-12-22 |
| [53] CrossMux on Sticky and X4 Pro, no multiplayer | feature | 2026-09-25 | 2026-12-25 |
| [34] ESP-NOW peer and payload limits | feature | 2026-09 | 2026-12-26 |
| [18] CrossPlay's momentum shifted to "Live" | trajectory | 2026-09-22 | 2027-03-22 |
| [57] Upstream rejects games | sentiment | 2026-02-03 | 2027-02-03 |
| [52] No other Xteink fork has local multiplayer | catalog | 2026-08-24 | 2027-08-24 |
| [19] CrossPlay user feedback is thin | sentiment | 2026-09-24 | 2027-09-24 |
| [41] Berry uses about 10 KB of RAM on ESP32 | performance | 2026-09 | 2027-09-26 |
| [23] SUMI Lua limits and API | architecture | 2026-05-30 | 2028-05-30 |
| [7] CrossPlay is hard-coded to 2 players | architecture | 2026-09-22 | 2028-09-22 |

The earliest recheck is **now**, for [69]. The next are [71] on 2026-10-26 and [3], [53] and [34] around 2026-12-25. Competitor features move fastest, so run a Refresh before the PRD is finalized if it slips past December.
